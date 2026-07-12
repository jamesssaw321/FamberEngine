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

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

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

static void gameLoop(Renderer& r, const TraceFn& trace, Player p, float eyeZ) {
    LARGE_INTEGER freq, prev; QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&prev);
    double accum = 0.0; const double STEP = 1.0 / 100.0;
    printf("Controls: WASD move, mouse look, SPACE jump (hold to bhop), ESC quit.\n");

    while (plat::running()) {
        plat::poll();
        if (!plat::running()) break;

        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart; prev = now;
        if (dt > 0.1) dt = 0.1;

        float dyaw, dpitch; plat::mouseLook(0.12f, dyaw, dpitch);
        p.yaw += dyaw; p.pitch = clampf(p.pitch + dpitch, -89, 89);

        MoveInput in;
        in.forward = (plat::key('W') ? 1.f : 0.f) - (plat::key('S') ? 1.f : 0.f);
        in.side    = (plat::key('D') ? 1.f : 0.f) - (plat::key('A') ? 1.f : 0.f);
        in.jump    = plat::key(VK_SPACE);

        accum += dt; int guard = 0;
        while (accum >= STEP && guard++ < 8) { PM_Move(p, in, (float)STEP, trace); accum -= STEP; }

        int w, h; plat::size(w, h);
        Vec3 fwd; angleVectors(p.pitch, p.yaw, &fwd, nullptr, nullptr);
        r.draw(p.origin + Vec3{0, 0, eyeZ}, fwd, w, h);
        plat::swap();
    }
}

int main(int argc, char** argv) {
    const char* shot = nullptr; const char* mapPath = nullptr;
    const char* genPath = nullptr; const char* bspPath = nullptr; const char* mdlPath = nullptr;
    bool selftest = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "-map") && i + 1 < argc) mapPath = argv[++i];
        else if (!strcmp(argv[i], "-bsp") && i + 1 < argc) bspPath = argv[++i];
        else if (!strcmp(argv[i], "-mdl") && i + 1 < argc) mdlPath = argv[++i];
        else if (!strcmp(argv[i], "-genmap") && i + 1 < argc) genPath = argv[++i];
        else if (!strcmp(argv[i], "-selftest")) selftest = true;
    }

    if (genPath) { writeMap(genPath, buildLevel()); printf("[map] wrote %s\n", genPath); return 0; }
    if (mdlPath) return runModelViewer(mdlPath, shot);

    Bsp bsp; bool useBsp = false;
    Level lv;
    if (bspPath) {
        if (bsp.load(bspPath)) useBsp = true;
        else { printf("[bsp] load failed, using built-in\n"); }
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
        } else runSelfTest(lv.brushes);
        return 0;
    }

    if (!plat::init(1280, 720, "FamberEngine")) return 1;
    if (!loadGL()) { printf("GL 2.0 functions unavailable\n"); return 1; }
    printf("GL_VERSION: %s\n", (const char*)glGetString(GL_VERSION));

    Renderer r;
    TraceFn trace;
    Player p;
    float eyeZ;
    if (useBsp) {
        r.initBSP(bsp);
        trace = [&](const Vec3& s, const Vec3& e, const Vec3&, const Vec3&) { return bsp.hullTrace(s, e); };
        p.mins = {-16, -16, -36}; p.maxs = {16, 16, 36};
        p.origin = bsp.spawn; p.yaw = bsp.spawnYaw; eyeZ = 28;
    } else {
        r.init(lv);
        trace = [&](const Vec3& s, const Vec3& e, const Vec3& mn, const Vec3& mx) { return traceBox(s, e, mn, mx, lv.brushes); };
        p.mins = {-16, -16, 0}; p.maxs = {16, 16, 72};
        p.origin = lv.spawn; p.yaw = 90; eyeZ = 64;
    }

    if (shot) {
        plat::show();
        Vec3 eye, fwd; int w, h; plat::size(w, h);
        if (useBsp) { eye = p.origin + Vec3{0, 0, eyeZ}; angleVectors(0, p.yaw, &fwd, nullptr, nullptr); }
        else { eye = {-360, -380, 320}; fwd = normalize(Vec3{0, 0, 48} - eye); }
        for (int i = 0; i < 2; i++) { r.draw(eye, fwd, w, h); plat::swap(); }
        glFinish();
        plat::saveBMP(shot);
        return 0;
    }

    plat::show();
    gameLoop(r, trace, p, eyeZ);
    plat::shutdown();
    return 0;
}
