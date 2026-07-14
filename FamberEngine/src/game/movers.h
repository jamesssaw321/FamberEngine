// Moving brush entities: func_door (slide open on approach), func_plat
// (rise when ridden), func_train (follow a path_corner chain), and
// func_rotating (spin forever). One xforms vector feeds the renderer;
// translation movers also clip movement and carry riders. In multiplayer
// the server simulates translation movers and snapshots their offsets;
// clients lerp toward them (rotators spin locally, they are deterministic).
#pragma once
#include "../core/math.h"
#include "../physics/pmove.h"
#include "../world/bsp.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct Door {
    int model = 0;
    Vec3 openOffset{};
    Vec3 cur{};
    float speed = 100;
    float wait = 3;        // -1 = stay open
    float waitLeft = 0;
    int state = 0;         // 0 closed, 1 opening, 2 open, 3 closing
    Vec3 mins{}, maxs{};   // closed bounds, world space
    std::string targetname; // set = trigger-controlled, no proximity opening
    std::string moveWav, stopWav;
};

struct Button { // func_button: pressed with +use, fires its target
    int model = 0;
    Vec3 openOffset{};     // press-in travel
    Vec3 cur{};
    float speed = 40;
    float wait = 1;        // -1 = stays pressed
    float waitLeft = 0;
    int state = 0;         // like Door
    Vec3 mins{}, maxs{};
    std::string target;
    std::string moveWav, stopWav; // press sound in moveWav
};

struct Plat {
    int model = 0;
    float height = 0;      // travel below the spawned (top) position
    Vec3 cur{};            // starts down: {0,0,-height}
    float speed = 150;
    float waitLeft = 0;
    int state = 0;         // 0 down, 1 rising, 2 top, 3 lowering
    Vec3 mins{}, maxs{};   // spawned bounds (top position)
    std::string moveWav, stopWav;
};

struct Train {
    int model = 0;
    std::vector<Vec3> stops;   // corner -> desired model offset
    std::vector<float> waits;  // pause at each corner
    std::vector<int> nexts;    // corner chain (index into stops)
    int to = 0;
    Vec3 cur{};
    float speed = 100;
    float waitLeft = 0;
    Vec3 mins{}, maxs{};
    std::string targetname;    // set = starts parked, trigger toggles it
    std::string moveWav, stopWav;
};

struct Rot { // func_rotating: decorative, spins forever
    int model = 0;
    Vec3 origin{};         // rotation pivot
    bool originRel = false; // origin brush: model verts stored pivot-relative
    int axis = 2;          // 0=x 1=y 2=z
    float speed = 100;     // deg/s
    float angle = 0;
    Vec3 mins{}, maxs{};
};

struct Movers {
    std::vector<Door> doors;
    std::vector<Plat> plats;
    std::vector<Train> trains;
    std::vector<Button> buttons;
    std::vector<Rot> rots;
    std::vector<Vec3> offsets, prevOffsets; // per bsp submodel (translation)
    std::vector<Mat4> xforms;               // per bsp submodel (render)
    std::vector<int> solidModels;           // translation movers (clip at offset)
    const Bsp* world = nullptr;
    std::function<void(const std::string&)> chainFire; // Logic resolves mm/relay chains

    static void bounds(const dmodel_t& dm, Vec3& mn, Vec3& mx) {
        mn = {dm.mins[0], dm.mins[1], dm.mins[2]};
        mx = {dm.maxs[0], dm.maxs[1], dm.maxs[2]};
    }

    void spawn(const Bsp& bsp) {
        world = &bsp;
        offsets.assign(bsp.models.size(), Vec3{0, 0, 0});
        prevOffsets = offsets;
        xforms.assign(bsp.models.size(), mat4Identity());

        std::map<std::string, const BspEntity*> byName; // path_corners
        for (const BspEntity& e : bsp.entities)
            if (e.get("classname") == "path_corner" && !e.get("targetname").empty())
                byName[e.get("targetname")] = &e;

        for (const BspEntity& e : bsp.entities) {
            std::string cls = e.get("classname");
            std::string m = e.get("model");
            if (m.size() < 2 || m[0] != '*') continue;
            int mi = atoi(m.c_str() + 1);
            if (mi <= 0 || mi >= (int)bsp.models.size()) continue;
            float speed = (float)atof(e.get("speed").c_str());

            if (cls == "func_door" || cls == "func_button") {
                Vec3 mn, mx;
                bounds(bsp.models[mi], mn, mx);
                float ang = 0; bool haveAng = false;
                std::string a = e.get("angle");
                if (!a.empty()) { ang = (float)atof(a.c_str()); haveAng = true; }
                else {
                    a = e.get("angles");
                    float p, y, r;
                    if (!a.empty() && sscanf(a.c_str(), "%f %f %f", &p, &y, &r) == 3) { ang = y; haveAng = true; }
                }
                Vec3 dir{1, 0, 0};
                if (haveAng && ang == -1) dir = {0, 0, 1};
                else if (haveAng && ang == -2) dir = {0, 0, -1};
                else { float rad = ang * 3.14159265f / 180.0f; dir = {std::cos(rad), std::sin(rad), 0}; }
                float lip = (float)atof(e.get("lip").c_str());
                Vec3 size = mx - mn;
                float dist = std::fabs(size.x * dir.x) + std::fabs(size.y * dir.y) +
                             std::fabs(size.z * dir.z) - lip;
                if (dist < 1) dist = 1;
                std::string w = e.get("wait");
                if (cls == "func_door") {
                    Door d; d.model = mi; d.mins = mn; d.maxs = mx;
                    d.openOffset = dir * dist;
                    if (speed > 0) d.speed = speed;
                    if (!w.empty()) d.wait = (float)atof(w.c_str());
                    d.targetname = e.get("targetname");
                    int ms = atoi(e.get("movesnd").c_str()), ss = atoi(e.get("stopsnd").c_str());
                    if (ms >= 1 && ms <= 10) d.moveWav = "doors/doormove" + std::to_string(ms) + ".wav";
                    if (ss >= 1 && ss <= 8) d.stopWav = "doors/doorstop" + std::to_string(ss) + ".wav";
                    doors.push_back(d);
                } else {
                    Button b; b.model = mi; b.mins = mn; b.maxs = mx;
                    int flags = atoi(e.get("spawnflags").c_str());
                    b.openOffset = (flags & 1) ? Vec3{0, 0, 0} : dir * dist; // 1 = don't move
                    if (speed > 0) b.speed = speed;
                    if (!w.empty()) b.wait = (float)atof(w.c_str());
                    b.target = e.get("target");
                    int snd = atoi(e.get("sounds").c_str());
                    if (snd >= 1 && snd <= 14) b.moveWav = "buttons/button" + std::to_string(snd) + ".wav";
                    buttons.push_back(b);
                }
                solidModels.push_back(mi);
            } else if (cls == "func_plat") {
                Plat pl; pl.model = mi;
                bounds(bsp.models[mi], pl.mins, pl.maxs);
                float h = (float)atof(e.get("height").c_str());
                pl.height = h > 0 ? h : (pl.maxs.z - pl.mins.z) - 8;
                if (speed > 0) pl.speed = speed;
                pl.cur = {0, 0, -pl.height}; // spawns at the bottom
                if (atoi(e.get("sounds").c_str()) != 0) {
                    pl.moveWav = "plats/bigmove1.wav"; pl.stopWav = "plats/bigstop1.wav";
                }
                plats.push_back(pl);
                solidModels.push_back(mi);
            } else if (cls == "func_train") {
                Train t; t.model = mi;
                bounds(bsp.models[mi], t.mins, t.maxs);
                if (speed > 0) t.speed = speed;
                Vec3 center = (t.mins + t.maxs) * 0.5f; // HL moves the center to corners

                std::vector<const BspEntity*> chain;
                std::map<const BspEntity*, int> seen;
                std::string tgt = e.get("target");
                while (!tgt.empty() && byName.count(tgt)) {
                    const BspEntity* c = byName[tgt];
                    if (seen.count(c)) break; // loop closed
                    seen[c] = (int)chain.size();
                    chain.push_back(c);
                    tgt = c->get("target");
                }
                if (chain.empty()) continue;
                int loopTo = (!tgt.empty() && byName.count(tgt)) ? seen[byName[tgt]] : -1;
                for (int i = 0; i < (int)chain.size(); i++) {
                    t.stops.push_back(chain[i]->origin() - center);
                    std::string w = chain[i]->get("wait");
                    t.waits.push_back(w.empty() ? 0.0f : (float)atof(w.c_str()));
                    bool last = i + 1 == (int)chain.size();
                    t.nexts.push_back(last ? (loopTo >= 0 ? loopTo : i) : i + 1);
                }
                t.cur = t.stops[0];       // teleport to the first corner
                t.to = t.nexts[0];
                t.waitLeft = t.waits[0];
                if (t.nexts[0] == 0) t.waitLeft = 1e9f; // dead-end single stop
                t.targetname = e.get("targetname");
                if (!t.targetname.empty()) t.waitLeft = 1e9f; // parked until triggered
                if (atoi(e.get("sounds").c_str()) != 0) {
                    t.moveWav = "plats/bigmove1.wav"; t.stopWav = "plats/bigstop1.wav";
                }
                trains.push_back(t);
                solidModels.push_back(mi);
            } else if (cls == "func_rotating") {
                Rot r; r.model = mi;
                bounds(bsp.models[mi], r.mins, r.maxs);
                if (!e.get("origin").empty()) {
                    // origin brush: compiler stored the verts relative to it
                    r.origin = e.origin();
                    r.originRel = true;
                } else r.origin = (r.mins + r.maxs) * 0.5f;
                int flags = atoi(e.get("spawnflags").c_str());
                r.axis = (flags & 4) ? 0 : (flags & 8) ? 1 : 2;
                r.speed = speed > 0 ? speed : 100;
                if (flags & 2) r.speed = -r.speed; // reverse
                rots.push_back(r);
            }
        }
        printf("[movers] %d doors, %d plats, %d trains, %d buttons, %d rotating\n",
               (int)doors.size(), (int)plats.size(), (int)trains.size(),
               (int)buttons.size(), (int)rots.size());
    }

    int count() const { return (int)(doors.size() + plats.size() + trains.size() + buttons.size()); }

    Vec3 curOf(int i) const { // flatten order: doors, plats, trains, buttons
        if (i < (int)doors.size()) return doors[i].cur;
        i -= (int)doors.size();
        if (i < (int)plats.size()) return plats[i].cur;
        i -= (int)plats.size();
        if (i < (int)trains.size()) return trains[i].cur;
        i -= (int)trains.size();
        return buttons[i].cur;
    }

    int modelOf(int i) const {
        if (i < (int)doors.size()) return doors[i].model;
        i -= (int)doors.size();
        if (i < (int)plats.size()) return plats[i].model;
        i -= (int)plats.size();
        if (i < (int)trains.size()) return trains[i].model;
        i -= (int)trains.size();
        return buttons[i].model;
    }

    // trigger everything named `name`: doors toggle, parked trains start/stop
    void fire(const std::string& name) {
        if (name.empty()) return;
        for (Door& d : doors) {
            if (d.targetname != name) continue;
            if (d.state == 0 || d.state == 3) d.state = 1;
            else if (d.wait < 0) d.state = 3; // toggle stay-open doors shut
        }
        for (Train& t : trains) {
            if (t.targetname != name) continue;
            t.waitLeft = t.waitLeft >= 1e8f ? 0.0f : 1e9f;
        }
    }

    // +use: press a button the player stands near and roughly faces
    bool use(const Player& p) {
        Vec3 fwd;
        angleVectors(p.pitch, p.yaw, &fwd, nullptr, nullptr);
        for (Button& b : buttons) {
            Vec3 c = (b.mins + b.maxs) * 0.5f + b.cur;
            Vec3 d = c - p.origin;
            float dist = length(d);
            if (dist > 96) continue;
            if (dist > 8 && dot(d * (1.0f / dist), fwd) < 0.3f) continue;
            if (b.state == 0) {
                b.state = 1;
                if (chainFire) chainFire(b.target); else fire(b.target);
            }
            return true;
        }
        return false;
    }

    // sound view of mover i (moving = offset changed last frame)
    struct View { Vec3 center; bool moving; const std::string* moveWav; const std::string* stopWav; };
    View view(int i) const {
        View v{};
        const std::string *mw = nullptr, *sw = nullptr;
        Vec3 mn, mx;
        if (i < (int)doors.size()) { const Door& d = doors[i]; mn = d.mins; mx = d.maxs; mw = &d.moveWav; sw = &d.stopWav; }
        else {
            int k = i - (int)doors.size();
            if (k < (int)plats.size()) { const Plat& p = plats[k]; mn = p.mins; mx = p.maxs; mw = &p.moveWav; sw = &p.stopWav; }
            else if (k - (int)plats.size() < (int)trains.size()) {
                const Train& t = trains[k - (int)plats.size()];
                mn = t.mins; mx = t.maxs; mw = &t.moveWav; sw = &t.stopWav;
            } else {
                const Button& b = buttons[k - (int)plats.size() - (int)trains.size()];
                mn = b.mins; mx = b.maxs; mw = &b.moveWav; sw = &b.stopWav;
            }
        }
        int m = modelOf(i);
        v.center = (mn + mx) * 0.5f + offsets[m];
        Vec3 d = offsets[m] - prevOffsets[m];
        v.moving = std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-4f;
        v.moveWav = mw; v.stopWav = sw;
        return v;
    }

    // clip a movement trace against every translation mover + static rotators
    void clip(TraceResult& tr, const Vec3& s, const Vec3& e) const {
        if (!world) return;
        for (int mi : solidModels) {
            const Vec3& off = offsets[mi];
            TraceResult td = world->hullTraceModel(mi, s - off, e - off);
            if (td.fraction < tr.fraction) tr = td;
        }
        for (const Rot& r : rots) { // unrotated hull: good enough for fans
            Vec3 off = r.originRel ? r.origin : Vec3{0, 0, 0};
            TraceResult td = world->hullTraceModel(r.model, s - off, e - off);
            if (td.fraction < tr.fraction) tr = td;
        }
    }

    bool standingOn(int mi, const Vec3& off, const Player& p) const {
        TraceResult tr = world->hullTraceModel(mi, p.origin + Vec3{0, 0, 0.25f} - off,
                                               p.origin + Vec3{0, 0, -2.0f} - off);
        return tr.fraction < 1.0f && tr.normal.z > 0.7f;
    }

    static bool nearBox(const Vec3& mn, const Vec3& mx, const Vec3& p, float R) {
        return p.x > mn.x - R && p.x < mx.x + R &&
               p.y > mn.y - R && p.y < mx.y + R &&
               p.z > mn.z - R && p.z < mx.z + R;
    }

    static void moveToward(Vec3& cur, const Vec3& target, float step, bool& reached) {
        Vec3 delta = target - cur;
        float len = length(delta);
        if (len <= step) { cur = target; reached = true; }
        else { cur += delta * (step / len); reached = false; }
    }

    void rebuildXforms(float dt) {
        for (int mi : solidModels) xforms[mi] = mat4Translate(offsets[mi]);
        for (Rot& r : rots) {
            r.angle += r.speed * dt;
            if (r.angle > 360) r.angle -= 360;
            if (r.angle < -360) r.angle += 360;
            Mat4 rot = r.axis == 0 ? mat4RotX(r.angle) : r.axis == 1 ? mat4RotY(r.angle) : mat4RotZ(r.angle);
            xforms[r.model] = r.originRel
                ? mat4Mul(mat4Translate(r.origin), rot) // verts already pivot-relative
                : mat4Mul(mat4Translate(r.origin), mat4Mul(rot, mat4Translate(r.origin * -1.0f)));
        }
    }

    void carryPlayers(Player* const* players, int n) {
        for (int mi : solidModels) {
            Vec3 delta = offsets[mi] - prevOffsets[mi];
            if (delta.x == 0 && delta.y == 0 && delta.z == 0) continue;
            for (int k = 0; k < n; k++)
                if (standingOn(mi, prevOffsets[mi], *players[k])) players[k]->origin += delta;
        }
    }
    void carryPlayer(Player& p) { Player* a[1] = {&p}; carryPlayers(a, 1); }

    // authoritative simulation (server / singleplayer): any player triggers
    void update(float dt, Player* const* players, int n) {
        if (!world) return;
        prevOffsets = offsets;
        bool reached;
        auto anyNear = [&](const Vec3& mn, const Vec3& mx) {
            for (int k = 0; k < n; k++) if (nearBox(mn, mx, players[k]->origin, 80)) return true;
            return false;
        };
        auto anyOn = [&](int model, const Vec3& off) {
            for (int k = 0; k < n; k++) if (standingOn(model, off, *players[k])) return true;
            return false;
        };

        for (Door& d : doors) {
            // named doors are trigger-controlled, not proximity-controlled
            bool nr = d.targetname.empty() && anyNear(d.mins, d.maxs);
            if (d.state == 0) { if (nr) d.state = 1; }
            else if (d.state == 1) {
                moveToward(d.cur, d.openOffset, d.speed * dt, reached);
                if (reached) { d.state = 2; d.waitLeft = d.wait; }
            } else if (d.state == 2) {
                if (d.wait >= 0) { d.waitLeft -= dt; if (d.waitLeft <= 0 && !nr) d.state = 3; }
            } else {
                if (nr) d.state = 1;
                else { moveToward(d.cur, {0, 0, 0}, d.speed * dt, reached); if (reached) d.state = 0; }
            }
            offsets[d.model] = d.cur;
        }

        for (Plat& pl : plats) {
            bool on = anyOn(pl.model, pl.cur);
            if (pl.state == 0) { if (on) pl.state = 1; }
            else if (pl.state == 1) {
                moveToward(pl.cur, {0, 0, 0}, pl.speed * dt, reached);
                if (reached) { pl.state = 2; pl.waitLeft = 3; }
            } else if (pl.state == 2) {
                if (on) pl.waitLeft = 3;
                else { pl.waitLeft -= dt; if (pl.waitLeft <= 0) pl.state = 3; }
            } else {
                if (on) pl.state = 1;
                else {
                    moveToward(pl.cur, {0, 0, -pl.height}, pl.speed * dt, reached);
                    if (reached) pl.state = 0;
                }
            }
            offsets[pl.model] = pl.cur;
        }

        for (Train& t : trains) {
            if (t.waitLeft > 0) { t.waitLeft -= dt; offsets[t.model] = t.cur; continue; }
            moveToward(t.cur, t.stops[t.to], t.speed * dt, reached);
            if (reached) {
                t.waitLeft = t.waits[t.to];
                int at = t.to;
                t.to = t.nexts[at];
                if (t.to == at) t.waitLeft = 1e9f;
            }
            offsets[t.model] = t.cur;
        }

        for (Button& b : buttons) {
            if (b.state == 1) { // pressing in
                moveToward(b.cur, b.openOffset, b.speed * dt, reached);
                if (reached) { b.state = 2; b.waitLeft = b.wait; }
            } else if (b.state == 2) {
                if (b.wait >= 0) { b.waitLeft -= dt; if (b.waitLeft <= 0) b.state = 3; }
            } else if (b.state == 3) {
                moveToward(b.cur, {0, 0, 0}, b.speed * dt, reached);
                if (reached) b.state = 0;
            }
            offsets[b.model] = b.cur;
        }

        carryPlayers(players, n);
        rebuildXforms(dt);
    }
    void update(float dt, Player& p) { Player* a[1] = {&p}; update(dt, a, 1); }

    // network client: lerp toward snapshot offsets, spin rotators locally
    void netApply(const std::vector<Vec3>& targets, float dt, Player& p) {
        if (!world) return;
        prevOffsets = offsets;
        float k = clampf(dt * 10.0f, 0, 1);
        int n = count() < (int)targets.size() ? count() : (int)targets.size();
        for (int i = 0; i < n; i++) {
            int mi = modelOf(i);
            offsets[mi] += (targets[i] - offsets[mi]) * k;
        }
        carryPlayer(p);
        rebuildXforms(dt);
    }
};
