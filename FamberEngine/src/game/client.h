// Network client: sends usercmds (with redundancy), predicts its own
// movement with the same PM_Move the server runs, and on every snapshot
// rewinds to the server state and replays unacked cmds (reconciliation).
// Remote players render as models/player.mdl, or a colored box fallback.
#pragma once
#include "../core/cvar.h"
#include "../net/net.h"
#include "../net/protocol.h"
#include "../physics/pmove.h"
#include "../render/model.h"
#include <cstring>
#include <deque>
#include <string>

struct NetClient {
    net::Socket sock;
    net::Addr server;
    bool started = false, connected = false;
    int id = -1;
    uint32_t seq = 0;
    float retry = 0, silence = 0;
    std::string mapName;
    TraceFn trace;
    float* predict = nullptr; // cl_predict cvar

    std::deque<proto::UserCmd> pending; // sent, not yet acked
    double now = 0;                     // client clock (drives interpolation)
    std::vector<Vec3> moverTargets;     // snapshot mover offsets
    std::vector<std::pair<float, std::string>> chat; // ttl, line (overlay)

    struct Remote {
        bool active = false;
        struct Snap { double t; Vec3 pos; float yaw; };
        std::deque<Snap> buf;  // snapshot history, rendered ~100 ms in the past
        Vec3 render{}, vel{};
        float renderYaw = 0;
        bool onground = true;
        int seq = -1;          // current animation sequence
        float animTime = 0;
    } rem[proto::MAX_PLAYERS];

    bool begin(const char* host, unsigned short port, const std::string& map, const TraceFn& tf) {
        if (!sock.open(0)) return false;
        if (!net::resolve(host, port, server)) return false;
        mapName = map; trace = tf; started = true;
        con::print("[cl] connecting to %s:%d...", host, port);
        return true;
    }

    void stop() {
        if (connected) {
            proto::Hdr h{proto::MAGIC, proto::C2S_LEAVE};
            sock.send(server, &h, sizeof(h));
        }
        if (started) sock.close();
        started = connected = false;
    }

    void reconcile(Player& p, const proto::Snapshot& s) {
        // drop acked cmds, rewind to server truth, replay the rest
        while (!pending.empty() && pending.front().seq <= s.ack) pending.pop_front();
        for (int i = 0; i < s.num; i++) {
            const proto::PlayerState& ps = s.pl[i];
            if (ps.id == id) {
                float saveYaw = p.yaw, savePitch = p.pitch;
                p.origin = {ps.pos[0], ps.pos[1], ps.pos[2]};
                p.velocity = {ps.vel[0], ps.vel[1], ps.vel[2]};
                if (predict && *predict > 0)
                    for (const proto::UserCmd& u : pending) runCmd(p, u);
                p.yaw = saveYaw; p.pitch = savePitch;
            } else if (ps.id < proto::MAX_PLAYERS) {
                Remote& r = rem[ps.id];
                Vec3 pos{ps.pos[0], ps.pos[1], ps.pos[2]};
                r.vel = {ps.vel[0], ps.vel[1], ps.vel[2]};
                r.onground = (ps.flags & proto::PSF_ONGROUND) != 0;
                if (!r.active) { r.render = pos; r.renderYaw = ps.yaw; r.buf.clear(); }
                r.buf.push_back({now, pos, ps.yaw});
                while (r.buf.size() > 32 || (r.buf.size() > 2 && r.buf.front().t < now - 1.0))
                    r.buf.pop_front();
                r.active = true;
            }
        }
        // players missing from the snapshot are gone
        for (int i = 0; i < proto::MAX_PLAYERS; i++) {
            if (i == id || !rem[i].active) continue;
            bool found = false;
            for (int k = 0; k < s.num; k++) if (s.pl[k].id == i) { found = true; break; }
            if (!found) { rem[i].active = false; rem[i].buf.clear(); }
        }
        // mover offsets
        moverTargets.resize(s.nmovers);
        for (int i = 0; i < s.nmovers; i++)
            moverTargets[i] = {s.mv[i].off[0], s.mv[i].off[1], s.mv[i].off[2]};
    }

    void runCmd(Player& p, const proto::UserCmd& u) {
        p.yaw = u.yaw; p.pitch = u.pitch;
        MoveInput in;
        in.forward = (float)u.fwd;
        in.side = (float)u.side;
        in.jump = (u.buttons & proto::BTN_JUMP) != 0;
        PM_Move(p, in, clampf(u.msec / 1000.0f, 0.001f, 0.05f), trace);
    }

    void sendChat(const char* txt) {
        if (!connected) { con::print("say: not connected"); return; }
        proto::ChatC2S c{{proto::MAGIC, proto::C2S_CHAT}, (uint8_t)id, {}};
        strncpy(c.text, txt, 111);
        sock.send(server, &c, sizeof(c));
    }

    void frame(Player& p, const MoveInput& in, float dt) {
        if (!started) return;
        now += dt;
        for (size_t i = 0; i < chat.size();) {
            chat[i].first -= dt;
            if (chat[i].first <= 0) chat.erase(chat.begin() + i); else i++;
        }

        // receive
        net::Addr from;
        unsigned char buf[1400];
        int n;
        while ((n = sock.recv(from, buf, sizeof(buf))) >= 0) {
            if (!(from == server) || n < (int)sizeof(proto::Hdr)) continue;
            proto::Hdr h; memcpy(&h, buf, sizeof(h));
            if (h.magic != proto::MAGIC) continue;
            if (h.type == proto::S2C_ACCEPT && n >= (int)sizeof(proto::Accept) && !connected) {
                proto::Accept a; memcpy(&a, buf, sizeof(a));
                id = a.id; connected = true; silence = 0;
                p.origin = {a.spawn[0], a.spawn[1], a.spawn[2]};
                p.velocity = {0, 0, 0};
                p.yaw = a.yaw;
                con::print("[cl] connected, id %d, map %s", id, a.map);
                if (strncmp(a.map, mapName.c_str(), 31) != 0)
                    con::print("[cl] WARNING: server runs '%s', you loaded '%s'", a.map, mapName.c_str());
            } else if (h.type == proto::S2C_REJECT && n >= (int)sizeof(proto::Reject)) {
                proto::Reject r; memcpy(&r, buf, sizeof(r));
                con::print("[cl] rejected: %s", r.reason);
                started = false;
            } else if (h.type == proto::S2C_SNAPSHOT && connected && n >= proto::snapshotSize(0)) {
                proto::Snapshot s; memcpy(&s, buf, n < (int)sizeof(s) ? n : (int)sizeof(s));
                if (s.num <= proto::MAX_PLAYERS && s.nmovers <= proto::MAX_MOVERS &&
                    n >= proto::snapshotSize(s.nmovers)) {
                    silence = 0;
                    reconcile(p, s);
                }
            } else if (h.type == proto::S2C_CHAT && n >= (int)sizeof(proto::ChatS2C)) {
                proto::ChatS2C c; memcpy(&c, buf, sizeof(c));
                c.line[127] = 0;
                con::print("%s", c.line);
                chat.push_back({8.0f, c.line});
                while (chat.size() > 6) chat.erase(chat.begin());
            }
        }

        if (!connected) { // keep knocking
            retry -= dt;
            if (retry <= 0) {
                retry = 0.5f;
                proto::Connect c{{proto::MAGIC, proto::C2S_CONNECT}, {}, {}};
                strncpy(c.name, "player", 15);
                strncpy(c.map, mapName.c_str(), 31);
                sock.send(server, &c, sizeof(c));
            }
            return;
        }

        silence += dt;
        if (silence > 5.0f) {
            con::print("[cl] connection lost");
            connected = false;
            return;
        }

        // build + predict + send this frame's cmd
        proto::UserCmd u{};
        u.seq = ++seq;
        u.msec = (uint8_t)clampf(dt * 1000.0f, 1, 50);
        u.fwd = (int8_t)(in.forward > 0.5f ? 1 : (in.forward < -0.5f ? -1 : 0));
        u.side = (int8_t)(in.side > 0.5f ? 1 : (in.side < -0.5f ? -1 : 0));
        u.buttons = (in.jump ? proto::BTN_JUMP : 0) | (in.use ? proto::BTN_USE : 0);
        u.yaw = p.yaw; u.pitch = p.pitch;
        pending.push_back(u);
        while (pending.size() > 128) pending.pop_front();

        if (predict && *predict > 0) runCmd(p, u);

        proto::CmdPacket cp{};
        cp.h = {proto::MAGIC, proto::C2S_CMD};
        cp.id = (uint8_t)id;
        int count = (int)pending.size() < proto::CMD_REDUNDANCY ? (int)pending.size() : proto::CMD_REDUNDANCY;
        for (int i = 0; i < count; i++) cp.cmds[i] = pending[pending.size() - count + i];
        cp.count = (uint8_t)count;
        sock.send(server, &cp, proto::cmdPacketSize(count));

        // remote players render ~100 ms in the past, interpolating snapshots
        double rt = now - 0.1;
        for (Remote& r : rem) {
            if (!r.active || r.buf.empty()) continue;
            const Remote::Snap* a = &r.buf.front();
            const Remote::Snap* b = a;
            for (const Remote::Snap& s : r.buf) {
                if (s.t <= rt) a = &s;
                b = &s;
                if (s.t >= rt) break;
            }
            if (b->t <= a->t) { r.render = b->pos; r.renderYaw = b->yaw; }
            else {
                float f = clampf((float)((rt - a->t) / (b->t - a->t)), 0, 1);
                r.render = a->pos + (b->pos - a->pos) * f;
                float dy = b->yaw - a->yaw; // shortest arc
                while (dy > 180) dy -= 360;
                while (dy < -180) dy += 360;
                r.renderYaw = a->yaw + dy * f;
            }
        }
    }
};

// ---- rendering of remote players ----

static const char* B_VERT =
    "#version 120\n"
    "attribute vec3 aPos; attribute vec3 aNor;\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "varying vec3 vNor;\n"
    "void main(){ vNor=mat3(uModel)*aNor; gl_Position=uMVP*(uModel*vec4(aPos,1.0)); }\n";

static const char* B_FRAG =
    "#version 120\n"
    "uniform vec3 uCol;\n"
    "varying vec3 vNor;\n"
    "void main(){\n"
    "  float d=max(dot(normalize(vNor),vec3(0.37,0.53,0.76)),0.0)*0.5+0.4;\n"
    "  gl_FragColor=vec4(uCol*d,1.0);\n"
    "}\n";

struct RemoteDraw {
    Mdl mdl;
    ModelGL mgl;
    bool hasMdl = false;
    GLuint prog = 0, vbo = 0;
    GLint uMVP = -1, uModel = -1, uCol = -1, aPos = -1, aNor = -1;
    float feetZ = 0; // player origin -> feet offset (box fallback only:
                     // player.mdl geometry is centered like the hull)
    int seqIdle = 0, seqWalk = 0, seqRun = 0, seqJump = 0;

    int findSeq(const char* sub) const {
        for (int i = 0; i < mdl.seqCount(); i++) {
            std::string l = mdl.seqLabel(i);
            for (char& c : l) if (c >= 'A' && c <= 'Z') c += 32;
            if (l.find(sub) != std::string::npos) return i;
        }
        return 0;
    }

    void init(const std::string& modDir, float feet) {
        feetZ = feet;
        if (!modDir.empty() && mdl.load((modDir + "/models/player.mdl").c_str())) {
            mgl.init(mdl);
            hasMdl = true;
            seqIdle = findSeq("idle");
            seqWalk = findSeq("walk");
            seqRun = findSeq("run");
            seqJump = findSeq("jump");
            printf("[cl] player anims: idle %d, walk %d, run %d, jump %d\n",
                   seqIdle, seqWalk, seqRun, seqJump);
            return;
        }
        // fallback: a player-sized box
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &B_VERT, nullptr); glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &B_FRAG, nullptr); glCompileShader(fs);
        prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        uMVP = glGetUniformLocation(prog, "uMVP");
        uModel = glGetUniformLocation(prog, "uModel");
        uCol = glGetUniformLocation(prog, "uCol");
        aPos = glGetAttribLocation(prog, "aPos");
        aNor = glGetAttribLocation(prog, "aNor");
        float X = 16, Y = 16, H = 72;
        float f[] = { // 6 quads: x,y,z, nx,ny,nz
            -X,-Y,0,  0,-1,0,  X,-Y,0,  0,-1,0,  X,-Y,H,  0,-1,0,  -X,-Y,0, 0,-1,0,  X,-Y,H, 0,-1,0, -X,-Y,H, 0,-1,0,
            -X, Y,0,  0, 1,0,  X, Y,H,  0, 1,0,  X, Y,0,  0, 1,0,  -X, Y,0, 0, 1,0, -X, Y,H, 0, 1,0,  X, Y,H, 0, 1,0,
            -X,-Y,0, -1,0,0, -X, Y,H, -1,0,0, -X, Y,0, -1,0,0,  -X,-Y,0, -1,0,0, -X,-Y,H, -1,0,0, -X, Y,H, -1,0,0,
             X,-Y,0,  1,0,0,  X, Y,0,  1,0,0,  X, Y,H,  1,0,0,   X,-Y,0,  1,0,0,  X, Y,H,  1,0,0,  X,-Y,H,  1,0,0,
            -X,-Y,H,  0,0,1,  X,-Y,H,  0,0,1,  X, Y,H,  0,0,1,  -X,-Y,H,  0,0,1,  X, Y,H,  0,0,1, -X, Y,H,  0,0,1,
            -X,-Y,0,  0,0,-1, X, Y,0,  0,0,-1, X,-Y,0,  0,0,-1, -X,-Y,0,  0,0,-1, -X, Y,0, 0,0,-1,  X, Y,0,  0,0,-1,
        };
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(f), f, GL_STATIC_DRAW);
    }

    void draw(const Mat4& viewProj, NetClient& nc, float dt) {
        Vec3 light{-0.35f, -0.5f, -0.8f};
        for (int i = 0; i < proto::MAX_PLAYERS; i++) {
            NetClient::Remote& r = nc.rem[i];
            if (!r.active || i == nc.id) continue;
            Mat4 model = mat4Mul(mat4Translate(r.render + Vec3{0, 0, hasMdl ? 0.0f : feetZ}),
                                 mat4RotZ(r.renderYaw));
            if (hasMdl) {
                // pick a sequence from movement state
                float hs = std::sqrt(r.vel.x * r.vel.x + r.vel.y * r.vel.y);
                int want = !r.onground ? seqJump
                         : hs > 180 ? seqRun
                         : hs > 20 ? seqWalk
                         : seqIdle;
                if (want != r.seq) { r.seq = want; r.animTime = 0; }
                r.animTime += dt;
                mdl.pose(r.seq, r.animTime * mdl.seqFps(r.seq));
                mgl.upload(mdl);
                mgl.draw(viewProj, light, model);
            } else if (prog) {
                static const float cols[8][3] = {{0.9f,0.4f,0.2f},{0.2f,0.6f,0.9f},{0.3f,0.8f,0.3f},{0.9f,0.8f,0.2f},
                                                 {0.8f,0.3f,0.8f},{0.3f,0.8f,0.8f},{0.9f,0.5f,0.6f},{0.7f,0.7f,0.7f}};
                glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
                glUseProgram(prog);
                glUniformMatrix4fv(uMVP, 1, GL_FALSE, viewProj.m);
                glUniformMatrix4fv(uModel, 1, GL_FALSE, model.m);
                glUniform3fv(uCol, 1, cols[i]);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
                glEnableVertexAttribArray(aPos);
                glVertexAttribPointer(aNor, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
                glEnableVertexAttribArray(aNor);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
    }
};
