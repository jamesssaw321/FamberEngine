// Swept box vs convex brushes (Quake-style clip). Also used for shadow rays.
#pragma once
#include "brush.h"
#include <vector>

struct TraceResult {
    float fraction = 1.0f;
    Vec3 normal{0, 0, 0};
    bool startsolid = false;
};

inline Vec3 clipVelocity(const Vec3& v, const Vec3& normal, float overbounce = 1.0f) {
    return v - normal * (dot(v, normal) * overbounce);
}

inline void boxTraceBrush(const Vec3& start, const Vec3& end,
                          const Vec3& pmins, const Vec3& pmaxs,
                          const Brush& b, TraceResult& tr) {
    if (b.faces.empty()) return;
    const float EPS = 0.03f;
    float enterFrac = -1.0f, leaveFrac = 1.0f;
    Vec3 clipN{0, 0, 0};
    bool startsOut = false;

    for (const Face& fc : b.faces) {
        const Plane& p = fc.plane;
        Vec3 ofs{p.n.x < 0 ? pmaxs.x : pmins.x,
                 p.n.y < 0 ? pmaxs.y : pmins.y,
                 p.n.z < 0 ? pmaxs.z : pmins.z};
        float dist = p.d - dot(ofs, p.n);
        float d1 = dot(start, p.n) - dist;
        float d2 = dot(end, p.n) - dist;
        if (d1 > 0) startsOut = true;
        if (d1 > 0 && d2 >= 0) return;
        if (d1 <= 0 && d2 <= 0) continue;
        if (d1 > d2) { float f = (d1 - EPS) / (d1 - d2); if (f > enterFrac) { enterFrac = f; clipN = p.n; } }
        else         { float f = (d1 + EPS) / (d1 - d2); if (f < leaveFrac) leaveFrac = f; }
    }

    if (!startsOut) { tr.startsolid = true; return; }
    if (enterFrac < leaveFrac && enterFrac > -1.0f) {
        float f = enterFrac < 0 ? 0 : enterFrac;
        if (f < tr.fraction) { tr.fraction = f; tr.normal = clipN; }
    }
}

inline TraceResult traceBox(const Vec3& start, const Vec3& end,
                            const Vec3& pmins, const Vec3& pmaxs,
                            const std::vector<Brush>& brushes) {
    TraceResult tr;
    for (const Brush& b : brushes) boxTraceBrush(start, end, pmins, pmaxs, b, tr);
    return tr;
}

// True if the segment a->b is unobstructed (for shadow rays).
inline bool visible(const Vec3& a, const Vec3& b, const std::vector<Brush>& brushes) {
    TraceResult tr = traceBox(a, b, {0, 0, 0}, {0, 0, 0}, brushes);
    return tr.fraction >= 1.0f && !tr.startsolid;
}
