// Minimal 3D math. Z-up, column-major matrices for GL.
#pragma once
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : Vec3{0, 0, 0};
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Euler angles (degrees) -> basis vectors. Z-up: yaw about Z, pitch tilts.
inline void angleVectors(float pitchDeg, float yawDeg, Vec3* fwd, Vec3* right, Vec3* up) {
    const float d2r = 3.14159265358979f / 180.0f;
    float p = pitchDeg * d2r, y = yawDeg * d2r;
    float cp = std::cos(p), sp = std::sin(p);
    float cy = std::cos(y), sy = std::sin(y);
    if (fwd)   *fwd   = {cp * cy, cp * sy, -sp};
    if (right) *right = {sy, -cy, 0};
    if (up)    *up    = {sp * cy, sp * sy, cp};
}

// Mat4, column-major: m[col*4 + row].
struct Mat4 { float m[16]; };

inline Mat4 mat4Identity() {
    Mat4 r{};
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}
inline Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
inline Mat4 mat4Translate(const Vec3& t) {
    Mat4 r = mat4Identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
inline Mat4 mat4RotZ(float deg) {
    float a = deg * 3.14159265f / 180.0f;
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[0] = c; r.m[1] = s;
    r.m[4] = -s; r.m[5] = c;
    return r;
}
inline Mat4 mat4RotX(float deg) {
    float a = deg * 3.14159265f / 180.0f;
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[5] = c; r.m[6] = s;
    r.m[9] = -s; r.m[10] = c;
    return r;
}
inline Mat4 mat4RotY(float deg) {
    float a = deg * 3.14159265f / 180.0f;
    float c = std::cos(a), s = std::sin(a);
    Mat4 r = mat4Identity();
    r.m[0] = c; r.m[2] = -s;
    r.m[8] = s; r.m[10] = c;
    return r;
}
inline Mat4 mat4Perspective(float fovyRad, float aspect, float znear, float zfar) {
    Mat4 r{};
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}
inline Mat4 mat4LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = mat4Identity();
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}
