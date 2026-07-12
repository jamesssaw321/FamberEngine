// Quake/GoldSrc player movement. Collision goes through a TraceFn so the
// same code works against brushes or a BSP hull.
#pragma once
#include "../core/math.h"
#include "../world/trace.h"
#include <functional>
#include <cmath>

// trace a box (mins..maxs relative to origin) from start to end
using TraceFn = std::function<TraceResult(const Vec3&, const Vec3&, const Vec3&, const Vec3&)>;

struct Player {
    Vec3 origin{0, 0, 64};
    Vec3 velocity{0, 0, 0};
    float pitch = 0, yaw = 0;
    bool onground = false;
    Vec3 mins{-16, -16, 0};
    Vec3 maxs{16, 16, 72};
};

struct MoveInput {
    float forward = 0;
    float side = 0;
    bool jump = false;
};

namespace pm {
const float GRAVITY = 800.0f;
const float MAXSPEED = 320.0f;
const float ACCEL = 10.0f;
const float AIRACCEL = 10.0f;
const float FRICTION = 4.0f;
const float STOPSPEED = 100.0f;
const float STEPSIZE = 18.0f;
const float JUMP = 268.0f;
const float MAX_AIR_WISH = 30.0f;
}

inline bool onFloorTrace(const Player& p, const TraceFn& trace) {
    TraceResult tr = trace(p.origin + Vec3{0, 0, 0.25f}, p.origin + Vec3{0, 0, -2.0f}, p.mins, p.maxs);
    return tr.fraction < 1.0f && tr.normal.z > 0.7f;
}

inline void PM_Friction(Player& p, float dt) {
    Vec3 v = p.velocity;
    float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (speed < 1.0f) { p.velocity.x = 0; p.velocity.y = 0; return; }
    float control = speed < pm::STOPSPEED ? pm::STOPSPEED : speed;
    float drop = control * pm::FRICTION * dt;
    float newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;
    p.velocity = v * (newspeed / speed);
}

inline void PM_Accelerate(Player& p, const Vec3& wishdir, float wishspeed, float accel, float dt) {
    float addspeed = wishspeed - dot(p.velocity, wishdir);
    if (addspeed <= 0) return;
    float accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;
    p.velocity += wishdir * accelspeed;
}

inline void PM_AirAccelerate(Player& p, const Vec3& wishdir, float wishspeed, float accel, float dt) {
    float wishspd = wishspeed > pm::MAX_AIR_WISH ? pm::MAX_AIR_WISH : wishspeed; // strafe trick
    float addspeed = wishspd - dot(p.velocity, wishdir);
    if (addspeed <= 0) return;
    float accelspeed = accel * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    p.velocity += wishdir * accelspeed;
}

inline void PM_SlideMove(Player& p, float dt, const TraceFn& trace) {
    float timeLeft = dt;
    for (int bump = 0; bump < 4 && timeLeft > 0; bump++) {
        Vec3 end = p.origin + p.velocity * timeLeft;
        TraceResult tr = trace(p.origin, end, p.mins, p.maxs);
        if (tr.fraction > 0) p.origin += p.velocity * (timeLeft * tr.fraction);
        if (tr.fraction >= 1.0f) break;
        timeLeft -= timeLeft * tr.fraction;
        p.velocity = clipVelocity(p.velocity, tr.normal, 1.0f);
    }
}

inline float horizLen(const Vec3& a) { return std::sqrt(a.x * a.x + a.y * a.y); }

inline void PM_StepSlide(Player& p, float dt, const TraceFn& trace) {
    Vec3 startO = p.origin, startV = p.velocity;

    PM_SlideMove(p, dt, trace);
    Vec3 downO = p.origin, downV = p.velocity;

    p.origin = startO; p.velocity = startV;
    TraceResult up = trace(startO, startO + Vec3{0, 0, pm::STEPSIZE}, p.mins, p.maxs);
    p.origin = startO + Vec3{0, 0, pm::STEPSIZE} * up.fraction;
    PM_SlideMove(p, dt, trace);
    Vec3 target = p.origin - Vec3{0, 0, pm::STEPSIZE + 2.0f};
    TraceResult dn = trace(p.origin, target, p.mins, p.maxs);
    p.origin += (target - p.origin) * dn.fraction;

    bool steppedOnFloor = dn.fraction < 1.0f && dn.normal.z > 0.7f;
    if (!steppedOnFloor || horizLen(downO - startO) > horizLen(p.origin - startO)) {
        p.origin = downO; p.velocity = downV;
    } else {
        p.velocity.z = downV.z;
    }
}

inline void PM_Move(Player& p, const MoveInput& in, float dt, const TraceFn& trace) {
    p.onground = onFloorTrace(p, trace);

    if (in.jump && p.onground) { p.velocity.z = pm::JUMP; p.onground = false; }
    if (p.onground) PM_Friction(p, dt);

    Vec3 fwd, right;
    angleVectors(0, p.yaw, &fwd, &right, nullptr);
    fwd.z = 0; right.z = 0;
    fwd = normalize(fwd); right = normalize(right);
    Vec3 wishvel = fwd * (in.forward * pm::MAXSPEED) + right * (in.side * pm::MAXSPEED);
    wishvel.z = 0;
    float wishspeed = horizLen(wishvel);
    Vec3 wishdir = wishspeed > 1e-4f ? wishvel * (1.0f / wishspeed) : Vec3{0, 0, 0};
    if (wishspeed > pm::MAXSPEED) wishspeed = pm::MAXSPEED;

    if (p.onground) PM_Accelerate(p, wishdir, wishspeed, pm::ACCEL, dt);
    else            PM_AirAccelerate(p, wishdir, wishspeed, pm::AIRACCEL, dt);

    if (!p.onground) p.velocity.z -= pm::GRAVITY * dt;

    PM_StepSlide(p, dt, trace);
}
