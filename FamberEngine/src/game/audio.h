// Game-side audio: footstep/jump/land sounds and looping ambient_generic
// sources from BSP entities, spatialized (GoldSrc-style distance attenuation
// + stereo pan). Falls back to synthesized sounds when WAVs are missing.
#pragma once
#include "../platform/sound.h"
#include "../core/math.h"
#include "../world/bsp.h"
#include "../physics/pmove.h"
#include "movers.h"
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

struct GameAudio {
    bool on = false;
    std::vector<snd::Sound> steps;
    snd::Sound jumpSnd;

    struct Ambient {
        snd::Sound s;
        Vec3 origin{};
        float vol = 0.5f;
        float atten = 1.25f;      // gain = vol * (1 - dist * atten / 1000)
        bool everywhere = false;
        int chan = -1;
    };
    std::vector<Ambient> ambients;

    float stepTimer = 0;
    bool prevGround = true;
    float prevVelZ = 0;
    unsigned rng = 20260713;

    // ---- procedural fallbacks ----
    static void synthThud(snd::Sound& s, unsigned seed, float ms, float lpk, float amp) {
        int n = (int)(snd::RATE * ms / 1000.0f);
        s.mono.resize(n);
        unsigned r = seed; float lp = 0;
        for (int i = 0; i < n; i++) {
            r = r * 1664525u + 1013904223u;
            float noise = ((int)((r >> 16) & 0x7fff) - 16384) / 16384.0f;
            lp += lpk * (noise - lp);
            float env = std::exp(-12.0f * i / n);
            s.mono[i] = (int16_t)(lp * env * amp);
        }
    }

    void init(const Bsp* bsp, const std::string& modDir) {
        on = snd::init();
        if (!on) return;
        std::string sdir = modDir.empty() ? "" : modDir + "/sound/";

        for (int i = 1; i <= 4; i++) {
            snd::Sound s;
            if (!sdir.empty() && snd::loadWav(sdir + "player/pl_step" + std::to_string(i) + ".wav", s))
                steps.push_back(std::move(s));
        }
        if (steps.empty()) {
            steps.resize(4);
            for (int i = 0; i < 4; i++) synthThud(steps[i], 7u + i * 31u, 60, 0.22f, 20000);
        }
        if (sdir.empty() || !snd::loadWav(sdir + "player/pl_jump1.wav", jumpSnd))
            synthThud(jumpSnd, 991u, 90, 0.5f, 9000);

        if (bsp) {
            for (const BspEntity& e : bsp->entities) {
                if (e.get("classname") != "ambient_generic") continue;
                std::string msg = e.get("message");
                if (msg.empty() || msg.find(".wav") == std::string::npos) continue;
                int flags = atoi(e.get("spawnflags").c_str());
                if (flags & 16) continue;  // start silent
                if (flags & 32) continue;  // not looped (trigger one-shots)
                Ambient a;
                if (sdir.empty() || !snd::loadWav(sdir + msg, a.s)) continue;
                a.origin = e.origin();
                std::string h = e.get("health");
                if (!h.empty()) a.vol = clampf(atoi(h.c_str()) / 10.0f, 0.0f, 1.0f);
                a.everywhere = (flags & 1) != 0;
                a.atten = (flags & 2) ? 2.0f : (flags & 8) ? 0.8f : 1.25f; // small/large/medium
                ambients.push_back(std::move(a));
            }
            // start channels only once the vector stops reallocating:
            // the mixer keeps pointers into these Sound objects
            for (Ambient& a : ambients) a.chan = snd::play(a.s, 0, 0, true);
            printf("[snd] %d ambient sources\n", (int)ambients.size());
        }
    }

    void shutdown() { if (on) snd::shutdown(); }

    // ---- mover sounds: loop while moving, thunk on stop ----
    struct MvSnd { snd::Sound move, stop; bool hasMove = false, hasStop = false; int chan = -1; bool was = false; };
    std::vector<MvSnd> mv;

    void initMovers(const Movers& m, const std::string& modDir) {
        if (!on) return;
        std::string sdir = modDir.empty() ? "" : modDir + "/sound/";
        mv.resize(m.count());
        if (sdir.empty()) return;
        for (int i = 0; i < m.count(); i++) {
            Movers::View v = m.view(i);
            if (!v.moveWav->empty()) mv[i].hasMove = snd::loadWav(sdir + *v.moveWav, mv[i].move);
            if (!v.stopWav->empty()) mv[i].hasStop = snd::loadWav(sdir + *v.stopWav, mv[i].stop);
        }
    }

    static void spatial(const Vec3& pos, float vol, const Vec3& ear, const Vec3& right, float& L, float& R) {
        Vec3 d = pos - ear;
        float dist = length(d);
        float gain = vol * (1.0f - dist / 1000.0f);
        if (gain <= 0) { L = R = 0; return; }
        float pan = dist > 1 ? dot(d * (1.0f / dist), right) : 0;
        L = gain * std::sqrt((1 - pan) * 0.5f);
        R = gain * std::sqrt((1 + pan) * 0.5f);
    }

    void updateMovers(const Movers& m, const Vec3& ear, const Vec3& right) {
        if (!on) return;
        for (int i = 0; i < (int)mv.size() && i < m.count(); i++) {
            Movers::View v = m.view(i);
            MvSnd& s = mv[i];
            float L, R;
            if (v.moving && !s.was && s.hasMove && s.chan < 0)
                s.chan = snd::play(s.move, 0, 0, true);
            if (s.chan >= 0) {
                spatial(v.center, 0.8f, ear, right, L, R);
                snd::setVolume(s.chan, L, R);
            }
            if (!v.moving && s.was) {
                if (s.chan >= 0) { snd::stop(s.chan); s.chan = -1; }
                if (s.hasStop) {
                    spatial(v.center, 0.9f, ear, right, L, R);
                    snd::play(s.stop, L, R, false);
                }
            }
            s.was = v.moving;
        }
    }

    void playOne(const snd::Sound& s, float vol) {
        snd::play(s, vol * 0.5f, vol * 0.5f, false);
    }

    int pick() { rng = rng * 1664525u + 1013904223u; return (rng >> 16) % (unsigned)steps.size(); }

    void update(const Player& p, const Vec3& ear, const Vec3& right, float dt) {
        if (!on) return;

        for (Ambient& a : ambients) {
            if (a.chan < 0) continue;
            float L = 0, R = 0;
            if (a.everywhere) L = R = a.vol * 0.7f;
            else {
                Vec3 d = a.origin - ear;
                float dist = length(d);
                float gain = a.vol * (1.0f - dist * a.atten / 1000.0f);
                if (gain > 0) {
                    float pan = dist > 1 ? dot(d * (1.0f / dist), right) : 0;
                    L = gain * std::sqrt((1 - pan) * 0.5f);
                    R = gain * std::sqrt((1 + pan) * 0.5f);
                }
            }
            snd::setVolume(a.chan, L, R);
        }

        float hspeed = std::sqrt(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y);
        if (p.onground && hspeed > 100) {
            stepTimer -= dt;
            if (stepTimer <= 0) {
                stepTimer = hspeed > 220 ? 0.30f : 0.42f;
                playOne(steps[pick()], 0.8f);
            }
        } else stepTimer = 0.12f; // fresh step shortly after landing / starting

        if (prevGround && !p.onground && p.velocity.z > 200) playOne(jumpSnd, 0.7f);
        if (!prevGround && p.onground && prevVelZ < -250) playOne(steps[pick()], 1.0f);
        prevGround = p.onground;
        prevVelZ = p.velocity.z;
    }
};
