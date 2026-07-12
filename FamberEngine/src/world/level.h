// Level = brushes + lights + spawn. Built-in test level lives here.
#pragma once
#include "brush.h"
#include <vector>

struct Light {
    Vec3 pos;
    Vec3 color{1, 1, 1};
    float intensity = 1.0f;
    float radius = 700.0f;
};

struct Level {
    std::vector<Brush> brushes;
    std::vector<Light> lights;
    Vec3 spawn{0, -300, 80};
};

inline Level buildLevel() {
    Level lv;
    auto& w = lv.brushes;
    w.push_back(makeAABB({-512, -512, -32}, {512, 512, 0}, "FLOOR"));
    w.push_back(makeAABB({-512, -512, 0}, {512, -480, 320}, "BRICK"));
    w.push_back(makeAABB({-512, 480, 0}, {512, 512, 320}, "BRICK"));
    w.push_back(makeAABB({-512, -480, 0}, {-480, 480, 320}, "BRICK"));
    w.push_back(makeAABB({480, -480, 0}, {512, 480, 320}, "BRICK"));
    w.push_back(makeAABB({-96, -96, 0}, {96, 96, 64}, "METAL"));    // platform
    w.push_back(makeAABB({256, 256, 0}, {320, 320, 256}, "CONCRETE")); // pillar
    for (int i = 0; i < 8; i++) {                                  // staircase
        float x0 = -448 + i * 32;
        w.push_back(makeAABB({x0, -320, 0}, {x0 + 32, -192, 16.0f * (i + 1)}, "CONCRETE"));
    }
    { // ramp
        Vec3 p0{100, 160, 0}, p1{300, 160, 128};
        Vec3 topN = normalize(cross(normalize(p1 - p0), Vec3{0, 1, 0}));
        Brush r;
        r.faces.push_back({makePlane(topN, p0), "METAL"});
        r.faces.push_back({{{0, 0, -1}, 0}, "METAL"});
        r.faces.push_back({{{1, 0, 0}, 300}, "METAL"});
        r.faces.push_back({{{-1, 0, 0}, -90}, "METAL"});
        r.faces.push_back({{{0, 1, 0}, 220}, "METAL"});
        r.faces.push_back({{{0, -1, 0}, -100}, "METAL"});
        w.push_back(r);
    }

    lv.lights.push_back({{0, 0, 290}, {1.0f, 0.96f, 0.88f}, 1.0f, 760});
    lv.lights.push_back({{220, 160, 220}, {1.0f, 0.7f, 0.45f}, 0.9f, 560});
    lv.lights.push_back({{-320, -260, 240}, {0.55f, 0.7f, 1.0f}, 0.8f, 620});
    return lv;
}
