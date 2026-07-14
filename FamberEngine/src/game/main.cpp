// FamberEngine entry point: window, level/BSP, game loop, headless modes.
#include "../platform/window.h"
#include "../platform/gl.h"
#include "../core/math.h"
#include "../world/level.h"
#include "../world/map.h"
#include "../world/bsp.h"
#include "../world/mdl.h"
#include "../physics/pmove.h"
#include "../render/renderer.h"
#include "../render/model.h"
#include "../core/cvar.h"
#include "../core/files.h"
#include "../render/font.h"
#include "entities.h"
#include "audio.h"
#include "console.h"
#include "movers.h"
#include "logic.h"
#include "triggers.h"
#include "server.h"
#include "client.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static float g_fov = 90.0f, g_sens = 3.0f, g_showfps = 0.0f, g_novis = 0.0f, g_predict = 1.0f;
static float g_autofwd = 0.0f, g_autojump = 0.0f; // scripted input (demos/tests)
static float g_showpos = 0.0f, g_crosshair = 1.0f;
static Player* g_player = nullptr;        // for the noclip command
static NetClient* g_ncl = nullptr;        // for the say command
static bool g_netActive = false;
static bool g_nomouse = false;            // -nomouse: keyboard only (tests)
static float g_shotDelay = 0;             // screenshot <file> [sec]: capture countdown
static std::string g_shotPath;
static float g_recLeft = 0, g_recStep = 0, g_recAccum = 0, g_recDelay = 0; // record cmd
static int g_recFrame = 0;
static std::string g_recPrefix;
static bool g_startCmdsDone = false; // +cmds fire once, not on every changelevel
static bool g_toggleCon = false;          // toggleconsole cmd -> gameLoop

static void registerConsole() {
    con::reg("sv_gravity", &pm::GRAVITY, "gravity, u/s^2 (default 800)");
    con::reg("sv_maxspeed", &pm::MAXSPEED, "max ground speed (default 320)");
    con::reg("sv_accelerate", &pm::ACCEL, "ground acceleration (default 10)");
    con::reg("sv_airaccelerate", &pm::AIRACCEL, "air acceleration (default 10)");
    con::reg("sv_friction", &pm::FRICTION, "ground friction (default 4)");
    con::reg("sv_stopspeed", &pm::STOPSPEED, "friction floor speed (default 100)");
    con::reg("sv_jump", &pm::JUMP, "jump impulse (default 268)");
    con::reg("sv_stepsize", &pm::STEPSIZE, "max step height (default 18)");
    con::reg("volume", &snd::g_master, "master volume 0..1");
    con::reg("fov", &g_fov, "field of view, degrees (default 90)");
    con::reg("sensitivity", &g_sens, "mouse sensitivity (default 3)");
    con::reg("cl_showfps", &g_showfps, "draw fps + drawn faces (0/1)");
    con::reg("r_novis", &g_novis, "disable PVS culling (0/1)");
    con::reg("cl_predict", &g_predict, "client-side movement prediction (0/1)");
    con::reg("cl_autoforward", &g_autofwd, "hold forward automatically (0/1)");
    con::reg("cl_autojump", &g_autojump, "hold jump automatically (0/1)");
    con::reg("cl_showpos", &g_showpos, "draw player origin/yaw (0/1)");
    con::reg("crosshair", &g_crosshair, "draw the crosshair (0/1)");

    con::regCmd("quit", [](const con::Args&) { plat::run = false; }, "exit the game");
    con::regCmd("echo", [](const con::Args& a) {
        std::string s;
        for (size_t i = 1; i < a.size(); i++) { if (i > 1) s += " "; s += a[i]; }
        con::print("%s", s.c_str());
    }, "print text");
    con::regCmd("clear", [](const con::Args&) { con::log().clear(); }, "clear console log");
    con::regCmd("toggleconsole", [](const con::Args&) { g_toggleCon = true; }, "open/close the console");
    con::regCmd("setview", [](const con::Args& a) {
        if (g_player && a.size() >= 3) {
            g_player->pitch = clampf((float)atof(a[1].c_str()), -89, 89);
            g_player->yaw = (float)atof(a[2].c_str());
        }
    }, "set view angles: setview <pitch> <yaw>");
    con::regCmd("say", [](const con::Args& a) {
        std::string s;
        for (size_t i = 1; i < a.size(); i++) { if (i > 1) s += " "; s += a[i]; }
        if (g_ncl && g_ncl->connected) g_ncl->sendChat(s.c_str());
        else con::print("say: not connected");
    }, "chat: say <text>");
    con::regCmd("setpos", [](const con::Args& a) {
        if (g_player && a.size() >= 4) {
            g_player->origin = {(float)atof(a[1].c_str()), (float)atof(a[2].c_str()), (float)atof(a[3].c_str())};
            g_player->velocity = {0, 0, 0};
        }
    }, "teleport: setpos <x> <y> <z>");
    con::regCmd("noclip", [](const con::Args&) {
        if (g_netActive) { con::print("noclip: not available in multiplayer"); return; }
        if (!g_player) return;
        g_player->noclip = !g_player->noclip;
        con::print("noclip %s", g_player->noclip ? "ON" : "OFF");
    }, "toggle fly-through-walls");
    con::regCmd("screenshot", [](const con::Args& a) {
        g_shotPath = a.size() > 1 ? a[1] : "shot.bmp";
        g_shotDelay = a.size() > 2 ? (float)atof(a[2].c_str()) : 0.25f;
        con::print("screenshot -> %s in %.1fs", g_shotPath.c_str(), g_shotDelay);
    }, "save a BMP screenshot: screenshot <file> [delay sec]");
    con::regCmd("record", [](const con::Args& a) {
        g_recPrefix = a.size() > 1 ? a[1] : "frame";
        float sec = a.size() > 2 ? (float)atof(a[2].c_str()) : 5.0f;
        float fps = a.size() > 3 ? (float)atof(a[3].c_str()) : 12.0f;
        g_recDelay = a.size() > 4 ? (float)atof(a[4].c_str()) : 0.0f;
        g_recLeft = sec;
        g_recStep = 1.0f / clampf(fps, 1, 60);
        g_recAccum = g_recStep; // first frame immediately
        g_recFrame = 0;
        con::print("recording %s_####.bmp: %.1fs @ %.0f fps, delay %.1fs",
                   g_recPrefix.c_str(), sec, fps, g_recDelay);
    }, "record frames: record <prefix> [sec] [fps] [delay]");
    con::regCmd("help", [](const con::Args&) {
        con::print("--- commands ---");
        for (const con::Cmd& c : con::cmds()) con::print("  %-14s %s", c.name.c_str(), c.help.c_str());
        con::print("--- cvars ---");
        for (const con::Cvar& v : con::cvars()) con::print("  %-16s %-8g %s", v.name.c_str(), v.get(), v.help.c_str());
    }, "list commands and cvars");
}

static void runSelfTest(const std::vector<Brush>& world) {
    TraceFn tf = [&](const Vec3& s, const Vec3& e, const Vec3& mn, const Vec3& mx) {
        return traceBox(s, e, mn, mx, world);
    };
    printf("=== physics selftest ===\n");
    const float STEP = 1.0f / 100.0f;
    { Player p; p.origin = {0, -300, 120};
      for (int i = 0; i < 200; i++) { MoveInput in; PM_Move(p, in, STEP, tf); }
      printf("[fall]   z=%.2f onground=%d  (expect ~0, grounded)\n", p.origin.z, p.onground); }
    { Player p; p.origin = {350, -300, 8}; p.yaw = 90;
      for (int i = 0; i < 400; i++) { MoveInput in; in.forward = 1; PM_Move(p, in, STEP, tf); }
      printf("[wall]   y=%.2f  (expect ~464)\n", p.origin.y); }
    { Player p; p.origin = {-464, -256, 40}; p.yaw = 0; float maxZ = 0;
      for (int i = 0; i < 260; i++) { MoveInput in; in.forward = 1; PM_Move(p, in, STEP, tf);
        if (p.origin.x < -180 && p.origin.z > maxZ) maxZ = p.origin.z; }
      printf("[stairs] max height=%.1f  (expect ~128)\n", maxZ); }
    { Player p; p.origin = {0, -300, 0}; MoveInput j; j.jump = true; PM_Move(p, j, STEP, tf);
      printf("[jump]   vel.z=%.1f  (expect ~268)\n", p.velocity.z); }
    { Player p; p.origin = {40, 160, 8}; p.yaw = 0; float maxZ = 0;
      for (int i = 0; i < 400; i++) { MoveInput in; in.forward = 1; PM_Move(p, in, STEP, tf);
        if (p.origin.x > 110 && p.origin.x < 300 && p.origin.z > maxZ) maxZ = p.origin.z; }
      printf("[ramp]   max height=%.1f  (expect smooth climb ~120)\n", maxZ); }
    printf("=== done ===\n");
}

static int runModelViewer(const char* path, const char* shot) {
    Mdl mdl;
    if (!mdl.load(path)) return 1;
    if (!plat::init(1280, 720, "FamberEngine - model")) return 1;
    if (!loadGL()) { printf("GL 2.0 functions unavailable\n"); return 1; }
    printf("GL_VERSION: %s\n", (const char*)glGetString(GL_VERSION));

    ModelGL mg; mg.init(mdl);
    Vec3 c = mdl.center;
    float dist = mdl.radius * 1.7f + 20;
    Vec3 ld = normalize(Vec3{-0.4f, -0.6f, -0.5f});

    auto render = [&](float yaw, float frame) {
        int w, h; plat::size(w, h);
        mdl.pose(0, frame); mg.upload(mdl);
        Vec3 eye = c + Vec3{std::cos(yaw) * dist, std::sin(yaw) * dist, dist * 0.12f};
        Mat4 proj = mat4Perspective(55.0f * 3.14159f / 180.0f, (float)w / (h > 0 ? h : 1), 2, 8192);
        Mat4 mvp = mat4Mul(proj, mat4LookAt(eye, c, {0, 0, 1}));
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        mg.draw(mvp, ld);
    };

    if (shot) {
        plat::show();
        float fr = mdl.numframes > 1 ? mdl.numframes * 0.25f : 0;
        for (int i = 0; i < 2; i++) { render(0.7f, fr); plat::swap(); }
        glFinish();
        plat::saveBMP(shot);
        return 0;
    }

    plat::show();
    printf("Model viewer: auto-rotate + animation. ESC quit.\n");
    LARGE_INTEGER freq, prev; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    float t = 0;
    while (plat::running()) {
        plat::poll();
        if (!plat::running()) break;
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = (float)((now.QuadPart - prev.QuadPart) / (double)freq.QuadPart); prev = now;
        t += dt;
        render(t * 0.6f, t * mdl.fps);
        plat::swap();
    }
    plat::shutdown();
    return 0;
}

// "<dir>/maps/x.bsp" -> "<dir>" (strip the file and one directory level)
static std::string modDirOf(const char* path) {
    std::string d = path;
    for (int i = 0; i < 2; i++) {
        size_t p = d.find_last_of("/\\");
        d = p != std::string::npos ? d.substr(0, p) : "";
    }
    return d;
}

static Mat4 makeViewProj(const Vec3& eye, const Vec3& fwd, int w, int h) {
    float fov = clampf(g_fov, 30, 150);
    Mat4 proj = mat4Perspective(fov * 3.14159265f / 180.0f, (float)w / (h > 0 ? h : 1), 4.0f, 16384.0f);
    return mat4Mul(proj, mat4LookAt(eye, eye + fwd, {0, 0, 1}));
}

static void gameLoop(Renderer& r, const TraceFn& trace, Player& p, float eyeZ, WorldModels* wm, GameAudio* audio,
                     const std::vector<std::string>& startCmds,
                     GameServer* sv = nullptr, NetClient* ncl = nullptr, RemoteDraw* rd = nullptr,
                     Movers* movers = nullptr, Triggers* trig = nullptr, Logic* logic = nullptr) {
    LARGE_INTEGER freq, prev; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    double accum = 0.0, time = 0.0; const double STEP = 1.0 / 100.0;
    printf("Controls: WASD move, mouse look, SPACE jump (hold to bhop), E use, ` console, ESC quit.\n");

    TextGL text; text.init();
    Console cons;
    g_player = &p;
    con::print("FamberEngine console. 'help' lists commands, ` closes.");
    double fps = 0;
    bool prevUse = false;

    while (plat::running()) {
        plat::poll();
        if (!plat::running()) break;

        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart; prev = now;
        if (dt > 0.1) dt = 0.1;
        time += dt;
        if (dt > 1e-6) fps = fps * 0.95 + (1.0 / dt) * 0.05;

        cons.update((float)dt);
        if (g_toggleCon) { g_toggleCon = false; cons.toggle(); }
        if (!g_startCmdsDone && time > 0.3) { // +commands from the command line
            g_startCmdsDone = true;
            for (const std::string& c : startCmds) { con::print("] %s", c.c_str()); con::exec(c); }
        }

        MoveInput in;
        if (!cons.open) {
            if (!g_nomouse) {
                float dyaw, dpitch; plat::mouseLook(0.04f * g_sens, dyaw, dpitch);
                p.yaw += dyaw; p.pitch = clampf(p.pitch + dpitch, -89, 89);
            }
            in.forward = (plat::key('W') ? 1.f : 0.f) - (plat::key('S') ? 1.f : 0.f);
            in.side    = (plat::key('D') ? 1.f : 0.f) - (plat::key('A') ? 1.f : 0.f);
            in.jump    = plat::key(VK_SPACE);
            in.use     = plat::key('E');
        }
        if (g_autofwd > 0) in.forward = 1;
        if (g_autojump > 0) in.jump = true;
        if (movers && !ncl && in.use && !prevUse) movers->use(p); // MP: via usercmd buttons
        prevUse = in.use;

        if (logic && (!ncl || sv)) logic->update((float)dt); // delayed chain fires
        p.onLadder = trig && trig->onLadder(p);
        p.inWater = trig && trig->inWater(p.origin);
        if (sv) sv->update((float)dt); // in host mode the server drives the movers
        if (movers) {
            if (sv) movers->carryPlayer(p);        // predicted local player rides too
            else if (ncl) movers->netApply(ncl->moverTargets, (float)dt, p);
            else movers->update((float)dt, p);
        }
        if (ncl) ncl->frame(p, in, (float)dt); // predict + send + reconcile
        else {
            accum += dt; int guard = 0;
            while (accum >= STEP && guard++ < 8) { PM_Move(p, in, (float)STEP, trace); accum -= STEP; }
        }
        if (trig && movers && !ncl) {
            trig->update((float)dt, p, *movers);
            if (trig->changeWanted) {
                con::print("[level] %s...", trig->changeMap.c_str());
                break;
            }
        }

        int w, h; plat::size(w, h);
        Vec3 fwd, right; angleVectors(p.pitch, p.yaw, &fwd, &right, nullptr);
        Vec3 eye = p.origin + Vec3{0, 0, eyeZ};
        if (audio) audio->update(p, eye, right, (float)dt);
        if (audio && movers) audio->updateMovers(*movers, eye, right);
        r.fov = clampf(g_fov, 30, 150);
        r.draw(eye, fwd, w, h);
        if (wm) wm->draw(makeViewProj(eye, fwd, w, h), (float)time);
        if (ncl && rd) rd->draw(makeViewProj(eye, fwd, w, h), *ncl, (float)dt);

        text.begin(w, h);
        if (trig && trig->inWater(eye)) // underwater view tint
            text.rect(0, 0, (float)w, (float)h, 0.06f, 0.28f, 0.42f, 0.35f);
        if (g_crosshair > 0) {
            float cx = w * 0.5f, cy2 = h * 0.5f;
            text.rect(cx - 8, cy2 - 1, 5, 2, 0.7f, 1.0f, 0.7f, 0.8f);
            text.rect(cx + 3, cy2 - 1, 5, 2, 0.7f, 1.0f, 0.7f, 0.8f);
            text.rect(cx - 1, cy2 - 8, 2, 5, 0.7f, 1.0f, 0.7f, 0.8f);
            text.rect(cx - 1, cy2 + 3, 2, 5, 0.7f, 1.0f, 0.7f, 0.8f);
        }
        if (g_showfps > 0) {
            char b[64]; snprintf(b, sizeof(b), "%3.0f fps  %d/%d faces", fps, r.drawnFaces, (int)r.faces.size());
            text.text(w - 8.0f * 2 * (float)strlen(b) - 6, 6, 2, b, 1.0f, 0.8f, 0.2f, 1.0f);
        }
        if (g_showpos > 0) {
            char b[80]; snprintf(b, sizeof(b), "pos %.0f %.0f %.0f  yaw %.0f", p.origin.x, p.origin.y, p.origin.z, p.yaw);
            text.text(6, 6, 2, b, 0.6f, 1.0f, 0.6f, 1.0f);
        }
        if (ncl) { // chat overlay, newest at the bottom
            float cy = h - 150.0f;
            for (int i = (int)ncl->chat.size() - 1; i >= 0 && cy > 40; i--, cy -= 20)
                text.text(6, cy, 2, ncl->chat[i].second.c_str(), 1.0f, 1.0f, 0.7f,
                          clampf(ncl->chat[i].first, 0, 1));
        }
        cons.draw(text, w, h);
        text.draw();

        if (g_shotDelay > 0) {
            g_shotDelay -= (float)dt;
            if (g_shotDelay <= 0) plat::saveBMP(g_shotPath.c_str());
        }
        if (g_recDelay > 0) g_recDelay -= (float)dt;
        else if (g_recLeft > 0) {
            g_recLeft -= (float)dt;
            g_recAccum += (float)dt;
            if (g_recAccum >= g_recStep) {
                g_recAccum -= g_recStep;
                char fn[512];
                snprintf(fn, sizeof(fn), "%s_%04d.bmp", g_recPrefix.c_str(), g_recFrame++);
                plat::saveBMP(fn);
            }
        }
        plat::swap();
    }
    g_player = nullptr;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // diagnostics survive redirects/kills
    const char* shot = nullptr; const char* mapPath = nullptr;
    const char* genPath = nullptr; const char* bspPath = nullptr; const char* mdlPath = nullptr;
    bool selftest = false, sndtest = false, nettest = false, host = false;
    const char* connectTo = nullptr;
    int port = proto::DEFAULT_PORT;
    std::vector<std::string> startCmds; // "+cmd arg ..." run after startup
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "-map") && i + 1 < argc) mapPath = argv[++i];
        else if (!strcmp(argv[i], "-bsp") && i + 1 < argc) bspPath = argv[++i];
        else if (!strcmp(argv[i], "-mdl") && i + 1 < argc) mdlPath = argv[++i];
        else if (!strcmp(argv[i], "-genmap") && i + 1 < argc) genPath = argv[++i];
        else if (!strcmp(argv[i], "-selftest")) selftest = true;
        else if (!strcmp(argv[i], "-sndtest")) sndtest = true;
        else if (!strcmp(argv[i], "-nettest")) nettest = true;
        else if (!strcmp(argv[i], "-nomouse")) g_nomouse = true;
        else if (!strcmp(argv[i], "-host")) host = true;
        else if (!strcmp(argv[i], "-connect") && i + 1 < argc) connectTo = argv[++i];
        else if (!strcmp(argv[i], "-port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (argv[i][0] == '+') startCmds.push_back(argv[i] + 1);
        else if (!startCmds.empty()) startCmds.back() += std::string(" ") + argv[i];
    }
    registerConsole();

    if (nettest) { // headless loopback server + predicted client; with -bsp also movers
        Level tl;
        Bsp nb; bool useNb = false;
        Movers svM, clM; // server-side truth, client-side lerped copy
        TraceFn tf;
        Vec3 spawn; float syaw = 90; Vec3 mn, mx;
        if (bspPath) {
            fs::mount(modDirOf(bspPath));
            useNb = nb.load(bspPath);
        }
        if (useNb) {
            svM.spawn(nb); clM.spawn(nb);
            tf = [&](const Vec3& s, const Vec3& e, const Vec3&, const Vec3&) {
                TraceResult tr = nb.hullTrace(s, e); svM.clip(tr, s, e); return tr;
            };
            spawn = nb.spawn; syaw = nb.spawnYaw; mn = {-16, -16, -36}; mx = {16, 16, 36};
        } else {
            tl = buildLevel();
            tf = [&](const Vec3& s, const Vec3& e, const Vec3& bmn, const Vec3& bmx) {
                return traceBox(s, e, bmn, bmx, tl.brushes);
            };
            spawn = tl.spawn; mn = {-16, -16, 0}; mx = {16, 16, 72};
        }
        GameServer sv;
        if (!sv.start(27099, "nettest", tf, spawn, syaw, mn, mx)) return 1;
        if (useNb) sv.movers = &svM;
        NetClient c;
        float pred = 1; c.predict = &pred;
        if (!c.begin("127.0.0.1", 27099, "nettest", tf)) return 1;
        Player p; p.origin = spawn; p.yaw = syaw; p.mins = mn; p.maxs = mx;
        for (int i = 0; i < 500; i++) {
            MoveInput in;
            in.forward = i > 100 && i < 350 ? 1.0f : 0.0f; // run, then settle
            in.jump = i > 300 && i < 315;                  // one hop on the way
            if (i == 250) c.sendChat("privet mir");        // chat mid-run
            sv.update(0.01f);
            c.frame(p, in, 0.01f);
            if (useNb) clM.netApply(c.moverTargets, 0.01f, p);
            Sleep(1);
        }
        const GameServer::SvClient* scl = nullptr;
        for (const auto& s : sv.cl) if (s.active) { scl = &s; break; }
        if (!c.connected || !scl) { printf("[nettest] FAIL: not connected\n"); return 1; }
        float moved = length(scl->pl.origin - spawn);
        float err = length(p.origin - scl->pl.origin);
        printf("[nettest] moved %.0f units, prediction error %.4f\n", moved, err);
        bool ok = moved > (useNb ? 20 : 200) && err < 0.5f;
        bool chatOK = false;
        for (const std::string& l : con::log())
            if (l.find("privet mir") != std::string::npos && l.find("player0") != std::string::npos) chatOK = true;
        printf("[nettest] chat broadcast %s\n", chatOK ? "OK" : "FAIL");
        ok = ok && chatOK;
        if (useNb && svM.count() > 0) {
            float maxDiff = 0;
            for (int i = 0; i < svM.count(); i++) {
                float d = length(svM.curOf(i) - clM.offsets[clM.modelOf(i)]);
                if (d > maxDiff) maxDiff = d;
            }
            printf("[nettest] mover sync: %d movers, max client lag %.1f units\n", svM.count(), maxDiff);
            ok = ok && maxDiff < 50;
        }
        printf("[nettest] %s\n", ok ? "PASS" : "FAIL");
        c.stop(); sv.stop();
        return ok ? 0 : 1;
    }

    if (sndtest) { // headless: init mixer, play the procedural sounds
        GameAudio audio;
        audio.init(nullptr, "");
        if (!audio.on) { printf("[sndtest] mixer init FAILED\n"); return 1; }
        printf("[sndtest] mixer up, %d step sounds, jump %d samples\n",
               (int)audio.steps.size(), (int)audio.jumpSnd.mono.size());
        audio.playOne(audio.jumpSnd, 0.8f);
        Sleep(250);
        for (int i = 0; i < 4; i++) { audio.playOne(audio.steps[i], 0.9f); Sleep(330); }
        Sleep(300);
        audio.shutdown();
        printf("[sndtest] OK\n");
        return 0;
    }

    if (genPath) { writeMap(genPath, buildLevel()); printf("[map] wrote %s\n", genPath); return 0; }
    if (mdlPath) { fs::mount(modDirOf(mdlPath)); return runModelViewer(mdlPath, shot); }

    Bsp bsp; bool useBsp = false;
    Level lv;
    std::string modDir;
    if (bspPath) {
        modDir = modDirOf(bspPath); // mount paks before touching the map
        fs::mount(modDir);
        if (bsp.load(bspPath)) useBsp = true;
        else { printf("[bsp] load failed, using built-in\n"); modDir.clear(); }
    }
    if (!useBsp) {
        if (mapPath) {
            Level tmp;
            if (loadMap(mapPath, tmp) && !tmp.brushes.empty()) { lv = tmp;
                printf("[map] loaded %s (%d brushes, %d lights)\n", mapPath, (int)lv.brushes.size(), (int)lv.lights.size()); }
            else { printf("[map] failed, using built-in\n"); lv = buildLevel(); }
        } else lv = buildLevel();
    }

    if (selftest) {
        if (useBsp) {
            TraceFn tf = [&](const Vec3& s, const Vec3& e, const Vec3&, const Vec3&) { return bsp.hullTrace(s, e); };
            Player p; p.mins = {-16, -16, -36}; p.maxs = {16, 16, 36}; p.origin = bsp.spawn;
            for (int i = 0; i < 300; i++) { MoveInput in; PM_Move(p, in, 1.0f / 100, tf); }
            printf("[bsp drop] origin=%.0f %.0f %.0f onground=%d\n", p.origin.x, p.origin.y, p.origin.z, p.onground);
            int leaf = bsp.leafForPoint(p.origin);
            std::vector<uint8_t> fv;
            if (bsp.pvsFaces(p.origin, fv)) {
                int vis = 0;
                for (const BspFace& bf : bsp.faces) if (bf.src >= 0 && bf.src < (int)fv.size() && fv[bf.src]) vis++;
                printf("[bsp pvs] leaf=%d visible %d/%d faces\n", leaf, vis, (int)bsp.faces.size());
            } else printf("[bsp pvs] no vis data (leaf=%d)\n", leaf);
            int nMasked = 0, nSky = 0;
            for (const BspFace& bf : bsp.faces) {
                if (bf.sky) nSky++;
                if (bf.tex >= 0 && bf.tex < (int)bsp.textures.size() && bsp.textures[bf.tex].masked) {
                    if (!nMasked) { // first masked face: where to point a camera
                        Vec3 c{};
                        int nv = (int)bf.verts.size() / 7;
                        for (int v = 0; v < nv; v++)
                            c += Vec3{bf.verts[v * 7], bf.verts[v * 7 + 1], bf.verts[v * 7 + 2]} * (1.0f / nv);
                        printf("[tex] first masked face: '%s' at %.0f %.0f %.0f\n",
                               bsp.textures[bf.tex].name.c_str(), c.x, c.y, c.z);
                    }
                    nMasked++;
                }
            }
            printf("[tex] masked faces: %d, sky faces: %d\n", nMasked, nSky);
            Movers movers; movers.spawn(bsp);
            for (size_t di = 0; di < movers.doors.size(); di++) {
                const Door& d = movers.doors[di];
                printf("[movers]  door #%d model *%d center %.0f %.0f %.0f size %.0fx%.0fx%.0f travel %.0f\n",
                       (int)di, d.model,
                       (d.mins.x + d.maxs.x) / 2, (d.mins.y + d.maxs.y) / 2, (d.mins.z + d.maxs.z) / 2,
                       d.maxs.x - d.mins.x, d.maxs.y - d.mins.y, d.maxs.z - d.mins.z,
                       length(d.openOffset));
            }
            for (size_t ti = 0; ti < movers.trains.size(); ti++) {
                const Train& t = movers.trains[ti];
                printf("[movers]  train #%d model *%d at %.0f %.0f %.0f size %.0fx%.0fx%.0f stops %d speed %.0f\n",
                       (int)ti, t.model,
                       (t.mins.x + t.maxs.x) / 2 + t.cur.x, (t.mins.y + t.maxs.y) / 2 + t.cur.y,
                       (t.mins.z + t.maxs.z) / 2 + t.cur.z,
                       t.maxs.x - t.mins.x, t.maxs.y - t.mins.y, t.maxs.z - t.mins.z,
                       (int)t.stops.size(), t.speed);
            }
            for (size_t ri = 0; ri < movers.rots.size(); ri++) {
                const Rot& rt = movers.rots[ri];
                printf("[movers]  rotating #%d model *%d pivot %.0f %.0f %.0f axis %c speed %.0f deg/s bounds (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
                       (int)ri, rt.model, rt.origin.x, rt.origin.y, rt.origin.z,
                       "xyz"[rt.axis], rt.speed,
                       rt.mins.x, rt.mins.y, rt.mins.z, rt.maxs.x, rt.maxs.y, rt.maxs.z);
            }
            Player far2; far2.origin = {99999, 99999, 99999};
            int td = -1; // first proximity door (no targetname)
            for (size_t di = 0; di < movers.doors.size(); di++)
                if (movers.doors[di].targetname.empty()) { td = (int)di; break; }
            if (td >= 0) { // stand at the door, then walk away
                const Door& d0 = movers.doors[td];
                Vec3 at = (d0.mins + d0.maxs) * 0.5f;
                Player sp; sp.origin = at;
                for (int i = 0; i < 300; i++) movers.update(0.01f, sp);
                float opened = length(movers.doors[td].cur);
                for (int i = 0; i < 800; i++) movers.update(0.01f, far2);
                float closed = length(movers.doors[td].cur);
                printf("[movers] door %d: open travel=%.1f (expect %.1f), after leave=%.1f (expect 0; wait=%g)\n",
                       td, opened, length(movers.doors[td].openOffset), closed, d0.wait);
                Vec3 out = at + Vec3{(d0.maxs.x - d0.mins.x) / 2 + 60, 0, 0}; // clear the expanded hull
                TraceResult tr = bsp.hullTraceModel(d0.model, out, at);
                printf("[movers] ray into closed door %d: fraction=%.2f (expect <1 = blocked)\n", td, tr.fraction);
            }
            if (!movers.rots.empty()) { // rotation: pivot fixed, offset point orbits
                for (int i = 0; i < 50; i++) movers.update(0.01f, far2); // 0.5s
                const Rot& r0 = movers.rots[0];
                const Mat4& M = movers.xforms[r0.model];
                auto xf = [&](const Vec3& v) {
                    return Vec3{M.m[0] * v.x + M.m[4] * v.y + M.m[8] * v.z + M.m[12],
                                M.m[1] * v.x + M.m[5] * v.y + M.m[9] * v.z + M.m[13],
                                M.m[2] * v.x + M.m[6] * v.y + M.m[10] * v.z + M.m[14]};
                };
                Vec3 lp = r0.originRel ? Vec3{0, 0, 0} : r0.origin;      // pivot in vertex space
                Vec3 probe = lp + Vec3{10, 0, 0};
                Vec3 rest = r0.originRel ? r0.origin + Vec3{10, 0, 0} : probe;
                printf("[movers] rotating 0 after 0.5s: angle=%.0f, pivot drift=%.3f (expect 0), probe moved %.1f (expect >0)\n",
                       r0.angle, length(xf(lp) - r0.origin), length(xf(probe) - rest));
            }
            Triggers trig; trig.spawn(bsp);
            for (const TriggerVol& t : trig.list)
                if (t.kind == TriggerVol::CHANGELEVEL)
                    printf("[triggers]  changelevel -> %s (landmark '%s') at %.0f %.0f %.0f\n",
                           t.map.c_str(), t.landmark.c_str(),
                           (t.mins.x + t.maxs.x) / 2, (t.mins.y + t.maxs.y) / 2, (t.mins.z + t.maxs.z) / 2);
            for (size_t bi = 0; bi < movers.buttons.size(); bi++) { // button->door pairs
                const Button& b = movers.buttons[bi];
                bool hits = false;
                if (!b.target.empty())
                    for (const Door& d : movers.doors) if (d.targetname == b.target) hits = true;
                printf("[movers]  button #%d at %.0f %.0f %.0f -> '%s'%s\n", (int)bi,
                       (b.mins.x + b.maxs.x) / 2, (b.mins.y + b.maxs.y) / 2, (b.mins.z + b.maxs.z) / 2,
                       b.target.c_str(), hits ? " (a door)" : "");
            }
            Logic logic; logic.spawn(bsp, movers);
            if (!movers.buttons.empty()) { // fire every button, watch the chains
                for (size_t bi = 0; bi < movers.buttons.size() && bi < 3; bi++) {
                    const std::string& t = movers.buttons[bi].target;
                    printf("[logic]  button %d -> '%s'", (int)bi, t.c_str());
                    auto rng = logic.chains.equal_range(t);
                    for (auto it = rng.first; it != rng.second; ++it)
                        printf("  => %s (%.1fs)", it->second.first.c_str(), it->second.second);
                    printf("\n");
                }
                int doorEdges = 0;
                for (const auto& c : logic.chains)
                    for (const Door& d : movers.doors)
                        if (!d.targetname.empty() && d.targetname == c.second.first) {
                            doorEdges++;
                            printf("[logic]  edge '%s' => door '%s' (delay %.1f)\n",
                                   c.first.c_str(), c.second.first.c_str(), c.second.second);
                        }
                printf("[logic] chain edges leading to doors: %d\n", doorEdges);
                int pressed = -1; // press one door-button once, like a player would
                for (size_t bi = 0; bi < movers.buttons.size() && pressed < 0; bi++)
                    for (const Door& d : movers.doors)
                        if (!movers.buttons[bi].target.empty() && d.targetname == movers.buttons[bi].target) {
                            pressed = (int)bi;
                            break;
                        }
                if (pressed >= 0) {
                    logic.fire(movers.buttons[pressed].target);
                    std::vector<uint8_t> hit(movers.doors.size(), 0);
                    float maxTravel = 0;
                    for (int i = 0; i < 1500; i++) {
                        logic.update(0.01f);
                        movers.update(0.01f, far2);
                        for (size_t di = 0; di < movers.doors.size(); di++) {
                            if (movers.doors[di].state != 0) hit[di] = 1;
                            float tr2 = length(movers.doors[di].cur);
                            if (tr2 > maxTravel) maxTravel = tr2;
                        }
                    }
                    int act = 0;
                    for (uint8_t h : hit) act += h;
                    printf("[logic] pressed button %d ('%s'): %d doors moved, max travel %.0f\n",
                           pressed, movers.buttons[pressed].target.c_str(), act, maxTravel);

                    Button& ub = movers.buttons[pressed]; // aim +use at it from 60 units
                    Vec3 bc = (ub.mins + ub.maxs) * 0.5f + ub.cur;
                    Player up; up.mins = {-16, -16, -36}; up.maxs = {16, 16, 36};
                    up.origin = bc + Vec3{60, 0, 0};
                    up.yaw = 180;
                    bool got = movers.use(up);
                    printf("[logic] +use aimed at button %d: %s (state=%d, expect 1)\n",
                           pressed, got ? "pressed" : "missed", ub.state);
                }
            }
            if (!trig.waterModels.empty()) { // swim up inside the first pool
                const dmodel_t& wmod = bsp.models[trig.waterModels[0]];
                Vec3 wc{(wmod.mins[0] + wmod.maxs[0]) / 2, (wmod.mins[1] + wmod.maxs[1]) / 2,
                        (wmod.mins[2] + wmod.maxs[2]) / 2};
                printf("[water] func_water #0 center %.0f %.0f %.0f, inWater=%d (expect 1)\n",
                       wc.x, wc.y, wc.z, (int)trig.inWater(wc));
                Player wp; wp.mins = {-16, -16, -36}; wp.maxs = {16, 16, 36};
                wp.origin = wc; wp.pitch = -60; wp.inWater = true;
                float z0 = wp.origin.z;
                for (int i = 0; i < 200; i++) { MoveInput in; in.forward = 1; PM_Move(wp, in, 0.01f, tf); }
                printf("[water] swim: dz=%.0f in 2s (expect >0)\n", wp.origin.z - z0);
            }
            if (!trig.ladders.empty()) { // climb the first ladder
                const auto& L = trig.ladders[0];
                Player lp; lp.mins = {-16, -16, -36}; lp.maxs = {16, 16, 36};
                lp.origin = (L.first + L.second) * 0.5f;
                lp.pitch = -45; lp.onLadder = true;
                float z0 = lp.origin.z;
                for (int i = 0; i < 200; i++) { MoveInput in; in.forward = 1; PM_Move(lp, in, 0.01f, tf); }
                printf("[triggers] ladder climb: dz=%.0f in 2s (expect >0)\n", lp.origin.z - z0);
            }
            if (!movers.trains.empty()) { // trains run by themselves and carry riders
                Train& t0 = movers.trains[0];
                Player rider;
                rider.origin = {(t0.mins.x + t0.maxs.x) / 2 + t0.cur.x,
                                (t0.mins.y + t0.maxs.y) / 2 + t0.cur.y,
                                t0.maxs.z + t0.cur.z + 36.1f};
                Vec3 tStart = t0.cur, rStart = rider.origin;
                for (int i = 0; i < 300; i++) movers.update(0.01f, rider);
                Vec3 tDelta = t0.cur - tStart, rDelta = rider.origin - rStart;
                printf("[movers] train 0: moved %.0f units in 3s; rider carried %.0f (drift %.2f)\n",
                       length(tDelta), length(rDelta), length(tDelta - rDelta));
            }
        } else runSelfTest(lv.brushes);
        return 0;
    }

    if (!plat::init(1280, 720, "FamberEngine")) return 1;
    if (!loadGL()) { printf("GL 2.0 functions unavailable\n"); return 1; }
    printf("GL_VERSION: %s\n", (const char*)glGetString(GL_VERSION));

    // changelevel carries the player into the next map via the landmark
    bool carrying = false;
    Vec3 carryVel{}, carryLmOff{};
    float carryYaw = 0, carryPitch = 0;
    std::string carryLm;

    for (;;) { // one iteration per map (trigger_changelevel loops back)
    Renderer r;
    r.novis = &g_novis;
    TraceFn trace;
    Player p;
    float eyeZ;
    WorldModels wm;
    Movers movers;
    Triggers trig;
    Logic logic;
    if (useBsp) {
        r.initBSP(bsp, modDir);
        wm.spawn(bsp, modDir);
        wm.initGL();
        movers.spawn(bsp);
        trig.spawn(bsp);
        logic.spawn(bsp, movers); // wires movers.chainFire
        trig.fireFn = movers.chainFire;
        r.modelXforms = &movers.xforms;
        r.hiddenModels.assign(bsp.models.size(), 0); // invisible trigger brushes
        for (const BspEntity& e : bsp.entities) {
            std::string cls = e.get("classname");
            if (cls.rfind("trigger_", 0) == 0 || cls == "func_ladder") {
                std::string m = e.get("model");
                int mi = m.size() > 1 && m[0] == '*' ? atoi(m.c_str() + 1) : 0;
                if (mi > 0 && mi < (int)bsp.models.size()) r.hiddenModels[mi] = 1;
            }
        }
        trace = [&](const Vec3& s, const Vec3& e, const Vec3&, const Vec3&) {
            TraceResult tr = bsp.hullTrace(s, e);
            movers.clip(tr, s, e); // movers collide at their current offsets
            return tr;
        };
        p.mins = {-16, -16, -36}; p.maxs = {16, 16, 36};
        p.origin = bsp.spawn; p.yaw = bsp.spawnYaw; eyeZ = 28;
        if (carrying) {
            p.velocity = carryVel; p.yaw = carryYaw; p.pitch = carryPitch;
            auto lm = trig.landmarks.find(carryLm);
            if (!carryLm.empty() && lm != trig.landmarks.end()) p.origin = lm->second + carryLmOff;
            carrying = false;
        }
        trig.armAll(p); // volumes we spawn inside must not fire immediately
    } else {
        r.init(lv);
        trace = [&](const Vec3& s, const Vec3& e, const Vec3& mn, const Vec3& mx) { return traceBox(s, e, mn, mx, lv.brushes); };
        p.mins = {-16, -16, 0}; p.maxs = {16, 16, 72};
        p.origin = lv.spawn; p.yaw = 90; eyeZ = 64;
    }

    if (shot) {
        plat::show();
        Vec3 eye, fwd; int w, h; plat::size(w, h);
        if (useBsp) {
            eye = p.origin + Vec3{0, 0, eyeZ};
            angleVectors(0, p.yaw, &fwd, nullptr, nullptr);
            if (!wm.instances.empty()) { // aim at the nearest model
                float best = 1e18f; Vec3 target{};
                for (const ModelInstance& mi : wm.instances) {
                    float d = length(mi.origin - eye);
                    if (d < best) { best = d; target = mi.origin; }
                }
                fwd = normalize(target + Vec3{0, 0, 24} - eye);
            }
        } else { eye = {-360, -380, 320}; fwd = normalize(Vec3{0, 0, 48} - eye); }
        for (int i = 0; i < 2; i++) {
            r.draw(eye, fwd, w, h);
            if (useBsp) wm.draw(makeViewProj(eye, fwd, w, h), 0.0f);
            plat::swap();
        }
        glFinish();
        plat::saveBMP(shot);
        return 0;
    }

    // multiplayer: -host serves + plays via loopback, -connect joins
    GameServer sv;
    NetClient ncl;
    RemoteDraw rdraw;
    bool netMode = host || connectTo;
    if (netMode) {
        std::string mapBase = "builtin";
        const char* src = bspPath ? bspPath : mapPath;
        if (src) {
            mapBase = src;
            size_t sp = mapBase.find_last_of("/\\");
            if (sp != std::string::npos) mapBase = mapBase.substr(sp + 1);
        }
        if (host && !sv.start((unsigned short)port, mapBase, trace, p.origin, p.yaw, p.mins, p.maxs))
            return 1;
        if (host && useBsp) sv.movers = &movers;
        std::string addr = host ? "127.0.0.1" : connectTo;
        size_t colon = addr.find(':');
        int cport = port;
        if (colon != std::string::npos) { cport = atoi(addr.c_str() + colon + 1); addr = addr.substr(0, colon); }
        ncl.predict = &g_predict;
        if (!ncl.begin(addr.c_str(), (unsigned short)cport, mapBase, trace)) return 1;
        rdraw.init(modDir, p.mins.z == 0 ? 0.0f : p.mins.z);
        g_ncl = &ncl;
        g_netActive = true;
    }

    plat::show();
    GameAudio audio;
    audio.init(useBsp ? &bsp : nullptr, modDir);
    if (useBsp) audio.initMovers(movers, modDir);
    gameLoop(r, trace, p, eyeZ, useBsp ? &wm : nullptr, &audio, startCmds,
             host ? &sv : nullptr, netMode ? &ncl : nullptr, netMode ? &rdraw : nullptr,
             useBsp ? &movers : nullptr, useBsp && !netMode ? &trig : nullptr,
             useBsp ? &logic : nullptr);
    ncl.stop();
    sv.stop();
    audio.shutdown();

    if (!useBsp || netMode || !trig.changeWanted || !plat::running()) break;
    // carry the player relative to the landmark into the next map
    carryLm = trig.changeLandmark;
    auto lm = trig.landmarks.find(carryLm);
    carryLmOff = lm != trig.landmarks.end() ? p.origin - lm->second : Vec3{0, 0, 0};
    if (lm == trig.landmarks.end()) carryLm.clear();
    carryVel = p.velocity; carryYaw = p.yaw; carryPitch = p.pitch;
    carrying = true;
    std::string next = modDir + "/maps/" + trig.changeMap + ".bsp";
    bsp = Bsp();
    if (!bsp.load(next.c_str())) { printf("[level] cannot load %s\n", next.c_str()); break; }
    } // for(;;)

    plat::shutdown();
    return 0;
}
