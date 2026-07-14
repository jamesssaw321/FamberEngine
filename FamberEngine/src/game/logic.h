// Map logic chains: multi_manager fans one event out to many targets with
// per-target delays, trigger_relay forwards with an optional delay. fire()
// applies to movers and walks the chain; delayed edges sit in a queue.
#pragma once
#include "../world/bsp.h"
#include "movers.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

struct Logic {
    std::multimap<std::string, std::pair<std::string, float>> chains; // src -> (target, delay)
    std::vector<std::pair<float, std::string>> queue;                 // pending delayed fires
    Movers* movers = nullptr;
    int depth = 0;

    void spawn(const Bsp& bsp, Movers& m) {
        movers = &m;
        int mms = 0, relays = 0;
        for (const BspEntity& e : bsp.entities) {
            std::string cls = e.get("classname");
            std::string tn = e.get("targetname");
            if (tn.empty()) continue;
            if (cls == "multi_manager") {
                mms++;
                for (const auto& kv : e.kv) {
                    const std::string& k = kv.first;
                    if (k == "classname" || k == "targetname" || k == "origin" ||
                        k == "spawnflags" || k == "angles" || k == "wait") continue;
                    std::string name = k;
                    size_t h = name.find('#'); // duplicate-key suffix
                    if (h != std::string::npos) name = name.substr(0, h);
                    chains.insert({tn, {name, (float)atof(kv.second.c_str())}});
                }
            } else if (cls == "trigger_relay") {
                relays++;
                std::string tgt = e.get("target");
                if (!tgt.empty())
                    chains.insert({tn, {tgt, (float)atof(e.get("delay").c_str())}});
            }
        }
        printf("[logic] %d multi_managers, %d relays, %d chain edges\n",
               mms, relays, (int)chains.size());
        m.chainFire = [this](const std::string& n) { fire(n); };
    }

    void fire(const std::string& name) {
        if (name.empty() || depth > 16) return; // guard mm loops
        depth++;
        if (movers) movers->fire(name);
        auto range = chains.equal_range(name);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second.second <= 0) fire(it->second.first);
            else queue.push_back({it->second.second, it->second.first});
        }
        depth--;
    }

    void update(float dt) {
        for (size_t i = 0; i < queue.size();) {
            queue[i].first -= dt;
            if (queue[i].first <= 0) {
                std::string n = queue[i].second;
                queue.erase(queue.begin() + i);
                fire(n); // may push new entries; restart scan position
            } else i++;
        }
    }
};
