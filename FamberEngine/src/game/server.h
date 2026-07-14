// Authoritative game server. Movement is the same PM_Move the client
// predicts with: each received usercmd runs immediately with its msec
// (GoldSrc-style async movement, no fixed tick). Snapshots go out at 20 Hz.
#pragma once
#include "../core/cvar.h"
#include "../net/net.h"
#include "../net/protocol.h"
#include "../physics/pmove.h"
#include "movers.h"
#include <cstring>
#include <string>

struct GameServer {
    net::Socket sock;
    bool running = false;
    std::string mapName;
    TraceFn trace;
    Vec3 spawn{};
    float spawnYaw = 0;
    Vec3 pmins{}, pmaxs{};
    float snapTimer = 0;
    Movers* movers = nullptr; // server owns mover simulation in MP

    struct SvClient {
        bool active = false;
        net::Addr addr;
        char name[16] = {};
        Player pl;
        uint32_t lastSeq = 0;
        uint8_t lastButtons = 0;
        float idle = 0;
    } cl[proto::MAX_PLAYERS];

    bool start(unsigned short port, const std::string& map, const TraceFn& tf,
               const Vec3& sp, float syaw, const Vec3& mins, const Vec3& maxs) {
        if (!sock.open(port)) return false;
        mapName = map; trace = tf; spawn = sp; spawnYaw = syaw; pmins = mins; pmaxs = maxs;
        running = true;
        con::print("[sv] listening on port %d, map %s", port, map.c_str());
        return true;
    }

    void stop() { if (running) sock.close(); running = false; }

    int findByAddr(const net::Addr& a) {
        for (int i = 0; i < proto::MAX_PLAYERS; i++)
            if (cl[i].active && cl[i].addr == a) return i;
        return -1;
    }

    void handleConnect(const net::Addr& from, const proto::Connect& c) {
        int id = findByAddr(from);
        if (id < 0) {
            for (int i = 0; i < proto::MAX_PLAYERS; i++)
                if (!cl[i].active) { id = i; break; }
            if (id < 0) {
                proto::Reject r{{proto::MAGIC, proto::S2C_REJECT}, "server full"};
                sock.send(from, &r, sizeof(r));
                return;
            }
            SvClient& c2 = cl[id];
            c2 = SvClient{};
            c2.active = true;
            c2.addr = from;
            memcpy(c2.name, c.name, 15);
            c2.pl.mins = pmins; c2.pl.maxs = pmaxs;
            Vec3 f; angleVectors(0, spawnYaw, &f, nullptr, nullptr);
            c2.pl.origin = spawn + f * (float)(id * 80); // don't stack players
            c2.pl.yaw = spawnYaw;
            con::print("[sv] %s connected, id %d", c2.name, id);
            if (strncmp(c.map, mapName.c_str(), 31) != 0)
                con::print("[sv] warning: client map '%s' != server map '%s'", c.map, mapName.c_str());
        }
        proto::Accept a{{proto::MAGIC, proto::S2C_ACCEPT}, (uint8_t)id, {}, {}, cl[id].pl.yaw};
        strncpy(a.map, mapName.c_str(), 31);
        a.spawn[0] = cl[id].pl.origin.x; a.spawn[1] = cl[id].pl.origin.y; a.spawn[2] = cl[id].pl.origin.z;
        sock.send(from, &a, sizeof(a));
    }

    void handleCmds(int id, const proto::CmdPacket& cp, int count) {
        SvClient& c = cl[id];
        c.idle = 0;
        for (int i = 0; i < count; i++) {
            const proto::UserCmd& u = cp.cmds[i];
            if (u.seq <= c.lastSeq) continue; // already ran (redundant resend)
            c.lastSeq = u.seq;
            c.pl.yaw = u.yaw; c.pl.pitch = u.pitch;
            if ((u.buttons & proto::BTN_USE) && !(c.lastButtons & proto::BTN_USE) && movers)
                movers->use(c.pl);
            c.lastButtons = u.buttons;
            MoveInput in;
            in.forward = (float)u.fwd;
            in.side = (float)u.side;
            in.jump = (u.buttons & proto::BTN_JUMP) != 0;
            float dt = clampf(u.msec / 1000.0f, 0.001f, 0.05f);
            PM_Move(c.pl, in, dt, trace);
        }
    }

    void update(float dt) {
        if (!running) return;
        net::Addr from;
        unsigned char buf[1400];
        int n;
        while ((n = sock.recv(from, buf, sizeof(buf))) >= 0) {
            if (n < (int)sizeof(proto::Hdr)) continue;
            proto::Hdr h; memcpy(&h, buf, sizeof(h));
            if (h.magic != proto::MAGIC) continue;
            if (h.type == proto::C2S_CONNECT && n >= (int)sizeof(proto::Connect)) {
                proto::Connect c; memcpy(&c, buf, sizeof(c));
                handleConnect(from, c);
            } else if (h.type == proto::C2S_CMD && n >= proto::cmdPacketSize(0)) {
                proto::CmdPacket cp; memcpy(&cp, buf, n < (int)sizeof(cp) ? n : (int)sizeof(cp));
                int id = cp.id;
                if (id < 0 || id >= proto::MAX_PLAYERS || !cl[id].active || !(cl[id].addr == from)) continue;
                int count = cp.count < proto::CMD_REDUNDANCY ? cp.count : proto::CMD_REDUNDANCY;
                if (n < proto::cmdPacketSize(count)) continue;
                handleCmds(id, cp, count);
            } else if (h.type == proto::C2S_LEAVE) {
                int id = findByAddr(from);
                if (id >= 0) { cl[id].active = false; con::print("[sv] %s left", cl[id].name); }
            } else if (h.type == proto::C2S_CHAT && n >= (int)sizeof(proto::ChatC2S)) {
                proto::ChatC2S c; memcpy(&c, buf, sizeof(c));
                int id = findByAddr(from);
                if (id < 0 || id != c.id) continue;
                c.text[111] = 0;
                proto::ChatS2C out{{proto::MAGIC, proto::S2C_CHAT}, {}};
                snprintf(out.line, sizeof(out.line), "%s%d: %s", cl[id].name, id, c.text);
                for (SvClient& sc : cl)
                    if (sc.active) sock.send(sc.addr, &out, sizeof(out));
            }
        }

        if (movers) { // authoritative mover simulation, triggered by any player
            Player* pl[proto::MAX_PLAYERS]; int n2 = 0;
            for (SvClient& c : cl) if (c.active) pl[n2++] = &c.pl;
            movers->update(dt, pl, n2);
        }

        for (SvClient& c : cl) {
            if (!c.active) continue;
            c.idle += dt;
            if (c.idle > 5.0f) { c.active = false; con::print("[sv] %s timed out", c.name); }
        }

        snapTimer += dt;
        if (snapTimer >= 0.05f) { // 20 Hz
            snapTimer = 0;
            proto::Snapshot s{};
            s.h = {proto::MAGIC, proto::S2C_SNAPSHOT};
            s.num = 0;
            s.nmovers = 0;
            if (movers) {
                int nm = movers->count();
                if (nm > proto::MAX_MOVERS) nm = proto::MAX_MOVERS;
                for (int i = 0; i < nm; i++) {
                    Vec3 c = movers->curOf(i);
                    s.mv[i] = {{c.x, c.y, c.z}};
                }
                s.nmovers = (uint8_t)nm;
            }
            for (int i = 0; i < proto::MAX_PLAYERS && s.num < proto::MAX_PLAYERS; i++) {
                if (!cl[i].active) continue;
                proto::PlayerState& ps = s.pl[s.num++];
                ps.id = (uint8_t)i; ps.active = 1;
                ps.flags = cl[i].pl.onground ? proto::PSF_ONGROUND : 0;
                ps.pos[0] = cl[i].pl.origin.x; ps.pos[1] = cl[i].pl.origin.y; ps.pos[2] = cl[i].pl.origin.z;
                ps.vel[0] = cl[i].pl.velocity.x; ps.vel[1] = cl[i].pl.velocity.y; ps.vel[2] = cl[i].pl.velocity.z;
                ps.yaw = cl[i].pl.yaw;
            }
            for (SvClient& c : cl) {
                if (!c.active) continue;
                s.ack = c.lastSeq;
                sock.send(c.addr, &s, proto::snapshotSize(s.nmovers));
            }
        }
    }
};
