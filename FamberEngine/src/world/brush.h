// Convex brushes (Quake/GoldSrc style): each face is a plane plus a texture
// name. Inside a brush: dot(n,x) <= d for every face plane.
#pragma once
#include "../core/math.h"
#include <string>
#include <vector>
#include <cmath>

struct Plane { Vec3 n; float d; };
struct Face { Plane plane; std::string tex; };
struct Brush { std::vector<Face> faces; };

inline Plane makePlane(const Vec3& n, const Vec3& pointOn) {
    Vec3 nn = normalize(n);
    return {nn, dot(nn, pointOn)};
}

inline Brush makeAABB(const Vec3& mn, const Vec3& mx, const std::string& tex = "DEV") {
    Brush b;
    Plane ps[6] = {{{1, 0, 0}, mx.x}, {{-1, 0, 0}, -mn.x},
                   {{0, 1, 0}, mx.y}, {{0, -1, 0}, -mn.y},
                   {{0, 0, 1}, mx.z}, {{0, 0, -1}, -mn.z}};
    for (auto& p : ps) b.faces.push_back({p, tex});
    return b;
}

inline std::vector<Vec3> clipPoly(const std::vector<Vec3>& poly, const Plane& p) {
    std::vector<Vec3> out;
    const float EPS = 0.01f;
    int n = (int)poly.size();
    for (int i = 0; i < n; i++) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % n];
        float da = dot(p.n, a) - p.d;
        float db = dot(p.n, b) - p.d;
        if (da <= EPS) out.push_back(a);
        if ((da > EPS) != (db > EPS)) {
            float t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

// Face polygon = the face plane clipped by all other planes of the brush.
inline std::vector<Vec3> facePolygon(const Brush& b, int i) {
    Vec3 n = b.faces[i].plane.n;
    float d = b.faces[i].plane.d;
    Vec3 up = std::fabs(n.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    Vec3 right = normalize(cross(up, n));
    up = normalize(cross(n, right));
    Vec3 c = n * d;
    const float S = 8192.0f;
    std::vector<Vec3> poly = {c + right * S + up * S, c - right * S + up * S,
                              c - right * S - up * S, c + right * S - up * S};
    for (int j = 0; j < (int)b.faces.size() && poly.size() >= 3; j++)
        if (j != i) poly = clipPoly(poly, b.faces[j].plane);
    return poly;
}

// Tangent basis for a face (matches facePolygon).
inline void faceBasis(const Vec3& n, Vec3& right, Vec3& up) {
    up = std::fabs(n.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    right = normalize(cross(up, n));
    up = normalize(cross(n, right));
}
