// Spawns renderable .mdl models from BSP entities (monster_* etc).
#pragma once
#include "../core/math.h"
#include "../world/bsp.h"
#include "../world/mdl.h"
#include "../render/model.h"
#include <map>
#include <string>
#include <vector>

struct ModelInstance { int model; Vec3 origin; float yaw; };

// classname -> model path (game-code knowledge, GoldSrc keeps it in DLLs)
inline const char* classModel(const std::string& cls) {
    static const std::map<std::string, const char*> table = {
        {"monster_scientist", "models/scientist.mdl"},
        {"monster_sitting_scientist", "models/scientist.mdl"},
        {"monster_barney", "models/barney.mdl"},
        {"monster_gman", "models/gman.mdl"},
        {"monster_zombie", "models/zombie.mdl"},
        {"monster_headcrab", "models/headcrab.mdl"},
        {"monster_houndeye", "models/houndeye.mdl"},
        {"monster_alien_slave", "models/islave.mdl"},
        {"monster_alien_grunt", "models/agrunt.mdl"},
        {"monster_human_grunt", "models/hgrunt.mdl"},
        {"monster_bullchicken", "models/bullsquid.mdl"},
    };
    auto it = table.find(cls);
    return it != table.end() ? it->second : nullptr;
}

struct WorldModels {
    std::vector<Mdl> mdls;        // unique models
    std::vector<ModelGL> gls;
    std::vector<ModelInstance> instances;

    void spawn(const Bsp& bsp, const std::string& modDir) {
        std::map<std::string, int> loaded;
        for (const BspEntity& e : bsp.entities) {
            std::string cls = e.get("classname");
            const char* rel = classModel(cls);
            std::string path;
            if (rel) path = rel;
            else {
                std::string m = e.get("model");
                if (m.rfind("models/", 0) == 0 && m.find(".mdl") != std::string::npos) path = m;
            }
            if (path.empty()) continue;

            int idx;
            auto it = loaded.find(path);
            if (it != loaded.end()) idx = it->second;
            else {
                Mdl m;
                if (!m.load((modDir + "/" + path).c_str())) { loaded[path] = -1; continue; }
                idx = (int)mdls.size();
                mdls.push_back(std::move(m));
                loaded[path] = idx;
            }
            if (idx < 0) continue;
            instances.push_back({idx, e.origin(), e.yaw()});
        }
        printf("[ents] %d model instances (%d unique models)\n", (int)instances.size(), (int)mdls.size());
    }

    void initGL() {
        gls.resize(mdls.size());
        for (size_t i = 0; i < mdls.size(); i++) gls[i].init(mdls[i]);
    }

    void draw(const Mat4& viewProj, float time) {
        Vec3 light{-0.35f, -0.5f, -0.8f};
        for (size_t i = 0; i < mdls.size(); i++) {
            mdls[i].pose(0, time * mdls[i].fps);
            gls[i].upload(mdls[i]);
        }
        for (const ModelInstance& mi : instances) {
            Mat4 model = mat4Mul(mat4Translate(mi.origin), mat4RotZ(mi.yaw));
            gls[mi.model].draw(viewProj, light, model);
        }
    }
};
