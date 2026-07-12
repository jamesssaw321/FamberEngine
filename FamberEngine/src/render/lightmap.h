// Bake a per-face lightmap: for each luxel, sum diffuse from each light with
// a shadow ray traced against the world. CPU-only; renderer uploads the pixels.
#pragma once
#include "../world/brush.h"
#include "../world/trace.h"
#include "../world/level.h"
#include <vector>
#include <cmath>

struct Lightmap {
    int w = 1, h = 1;
    std::vector<unsigned char> px;   // RGB
    Vec3 right, up;                  // face basis
    float minU = 0, minV = 0, sizeU = 1, sizeV = 1; // world-space extent
};

// Map a world position on the face to lightmap UV (0..1).
inline void lmUV(const Lightmap& lm, const Vec3& p, float& u, float& v) {
    u = (dot(p, lm.right) - lm.minU) / lm.sizeU;
    v = (dot(p, lm.up) - lm.minV) / lm.sizeV;
}

inline Lightmap bakeFaceLightmap(const Brush& b, int fi, const std::vector<Vec3>& poly,
                                 const Level& lv, const std::vector<Brush>& world) {
    const Vec3 ambient{0.14f, 0.15f, 0.18f};
    const float LUXEL = 16.0f;

    Vec3 n = b.faces[fi].plane.n;
    float d = b.faces[fi].plane.d;
    Vec3 right, up; faceBasis(n, right, up);

    float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
    for (const Vec3& p : poly) {
        float u = dot(p, right), v = dot(p, up);
        minU = std::fmin(minU, u); maxU = std::fmax(maxU, u);
        minV = std::fmin(minV, v); maxV = std::fmax(maxV, v);
    }

    Lightmap lm;
    lm.right = right; lm.up = up;
    lm.minU = minU; lm.minV = minV;
    lm.sizeU = std::fmax(maxU - minU, 1.0f);
    lm.sizeV = std::fmax(maxV - minV, 1.0f);
    lm.w = (int)clampf(lm.sizeU / LUXEL + 1, 1, 32);
    lm.h = (int)clampf(lm.sizeV / LUXEL + 1, 1, 32);
    lm.px.resize(lm.w * lm.h * 3);

    Vec3 p0 = n * d; // a point on the plane
    for (int j = 0; j < lm.h; j++)
        for (int i = 0; i < lm.w; i++) {
            float u = minU + (i + 0.5f) / lm.w * lm.sizeU;
            float v = minV + (j + 0.5f) / lm.h * lm.sizeV;
            Vec3 wp = p0 + right * (u - dot(p0, right)) + up * (v - dot(p0, up));
            Vec3 sample = wp + n * 0.5f;

            Vec3 c = ambient;
            for (const Light& L : lv.lights) {
                Vec3 dir = L.pos - sample;
                float dist = length(dir);
                if (dist > L.radius || dist < 1e-3f) continue;
                Vec3 ld = dir * (1.0f / dist);
                float ndl = dot(n, ld);
                if (ndl <= 0) continue;
                if (!visible(sample, L.pos, world)) continue;
                float atten = ndl * L.intensity * (1.0f - dist / L.radius);
                c += L.color * atten;
            }
            int idx = (j * lm.w + i) * 3;
            lm.px[idx + 0] = (unsigned char)clampf(c.x * 255, 0, 255);
            lm.px[idx + 1] = (unsigned char)clampf(c.y * 255, 0, 255);
            lm.px[idx + 2] = (unsigned char)clampf(c.z * 255, 0, 255);
        }
    return lm;
}
