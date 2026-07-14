// trigger_* brush volumes: trigger_multiple/once fire their target when the
// player enters, trigger_teleport moves them to the destination entity, and
// trigger_changelevel requests loading the next map (with landmark carry).
#pragma once
#include "../core/math.h"
#include "../physics/pmove.h"
#include "../world/bsp.h"
#include "movers.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct TriggerVol {
    enum Kind { MULTIPLE, ONCE, TELEPORT, CHANGELEVEL };
    Kind kind = MULTIPLE;
    Vec3 mins{}, maxs{};
    std::string target;    // fire target / teleport destination
    std::string map;       // changelevel destination map
    std::string landmark;  // changelevel landmark name
    float wait = 1, cooldown = 0;
    bool armed = true;     // false while the player is inside from spawn
    bool dead = false;     // ONCE fired
};

struct Triggers {
    std::vector<TriggerVol> list;
    std::vector<std::pair<Vec3, Vec3>> ladders; // func_ladder volumes
    std::vector<int> waterModels;               // func_water submodels
    const Bsp* bspRef = nullptr;
    std::map<std::string, Vec3> teleDest;   // info_teleport_destination
    std::map<std::string, float> teleYaw;
    std::map<std::string, Vec3> landmarks;  // info_landmark
    std::function<void(const std::string&)> fireFn; // Logic chain (falls back to movers)

    // set when a changelevel fires; main tears the map down and reloads
    bool changeWanted = false;
    std::string changeMap, changeLandmark;

    void spawn(const Bsp& bsp) {
        bspRef = &bsp;
        for (const BspEntity& e : bsp.entities) {
            std::string cls = e.get("classname");
            if (cls == "info_teleport_destination") {
                teleDest[e.get("targetname")] = e.origin();
                teleYaw[e.get("targetname")] = e.yaw();
                continue;
            }
            if (cls == "info_landmark") {
                landmarks[e.get("targetname")] = e.origin();
                continue;
            }
            std::string m = e.get("model");
            if (m.size() < 2 || m[0] != '*') continue;
            int mi = atoi(m.c_str() + 1);
            if (mi <= 0 || mi >= (int)bsp.models.size()) continue;
            if (cls == "func_ladder") {
                const dmodel_t& dm = bsp.models[mi];
                ladders.push_back({{dm.mins[0], dm.mins[1], dm.mins[2]},
                                   {dm.maxs[0], dm.maxs[1], dm.maxs[2]}});
                continue;
            }
            if (cls == "func_water") { waterModels.push_back(mi); continue; }
            if (cls.rfind("trigger_", 0) != 0) continue;

            TriggerVol t;
            const dmodel_t& dm = bsp.models[mi];
            t.mins = {dm.mins[0], dm.mins[1], dm.mins[2]};
            t.maxs = {dm.maxs[0], dm.maxs[1], dm.maxs[2]};
            if (cls == "trigger_multiple") t.kind = TriggerVol::MULTIPLE;
            else if (cls == "trigger_once") t.kind = TriggerVol::ONCE;
            else if (cls == "trigger_teleport") t.kind = TriggerVol::TELEPORT;
            else if (cls == "trigger_changelevel") t.kind = TriggerVol::CHANGELEVEL;
            else continue; // transitions, hurt, push... later
            t.target = e.get("target");
            t.map = e.get("map");
            t.landmark = e.get("landmark");
            std::string w = e.get("wait");
            if (!w.empty()) t.wait = (float)atof(w.c_str());
            list.push_back(t);
        }
        printf("[triggers] %d volumes, %d ladders, %d water, %d teleport dests, %d landmarks\n",
               (int)list.size(), (int)ladders.size(), (int)waterModels.size(),
               (int)teleDest.size(), (int)landmarks.size());
    }

    static bool touching(const TriggerVol& t, const Player& p) {
        return p.origin.x + p.maxs.x > t.mins.x && p.origin.x + p.mins.x < t.maxs.x &&
               p.origin.y + p.maxs.y > t.mins.y && p.origin.y + p.mins.y < t.maxs.y &&
               p.origin.z + p.maxs.z > t.mins.z && p.origin.z + p.mins.z < t.maxs.z;
    }

    // call once right after spawn so volumes we start inside don't fire
    void armAll(const Player& p) {
        for (TriggerVol& t : list) t.armed = !touching(t, p);
    }

    bool inWater(const Vec3& p) const { // CONTENTS_WATER = -3 (also lava/slime -4/-5)
        if (!bspRef) return false;
        int c = bspRef->contents(p);
        if (c <= -3 && c >= -5) return true;
        for (int m : waterModels) {
            int mc = bspRef->modelContents(m, p);
            if (mc <= -3 && mc >= -5) return true;
        }
        return false;
    }

    bool onLadder(const Player& p) const {
        for (const auto& L : ladders)
            if (p.origin.x + p.maxs.x > L.first.x - 2 && p.origin.x + p.mins.x < L.second.x + 2 &&
                p.origin.y + p.maxs.y > L.first.y - 2 && p.origin.y + p.mins.y < L.second.y + 2 &&
                p.origin.z + p.maxs.z > L.first.z && p.origin.z + p.mins.z < L.second.z)
                return true;
        return false;
    }

    void update(float dt, Player& p, Movers& movers) {
        for (TriggerVol& t : list) {
            if (t.dead) continue;
            if (t.cooldown > 0) { t.cooldown -= dt; continue; }
            bool in = touching(t, p);
            if (!in) { t.armed = true; continue; }
            if (!t.armed) continue;

            if (t.kind == TriggerVol::CHANGELEVEL) {
                if (!t.map.empty() && !changeWanted) {
                    changeWanted = true;
                    changeMap = t.map;
                    changeLandmark = t.landmark;
                }
                continue;
            }
            if (t.kind == TriggerVol::TELEPORT) {
                auto it = teleDest.find(t.target);
                if (it != teleDest.end()) {
                    p.origin = it->second + Vec3{0, 0, 4};
                    p.velocity = {0, 0, 0};
                    p.yaw = teleYaw[t.target];
                    t.cooldown = 0.5f;
                }
                continue;
            }
            if (fireFn) fireFn(t.target); else movers.fire(t.target); // MULTIPLE / ONCE
            if (t.kind == TriggerVol::ONCE) t.dead = true;
            else t.cooldown = t.wait > 0 ? t.wait : 0.2f;
        }
    }
};
