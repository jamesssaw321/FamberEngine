// Procedural textures (stand-ins for WAD art) + GL upload with a small cache.
#pragma once
#include "../platform/gl.h"
#include <string>
#include <vector>
#include <map>
#include <cmath>

inline float texHash(int x, int y) {
    int n = x * 374761393 + y * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    return ((n ^ (n >> 16)) & 0xffff) / 65535.0f;
}

// Generate a 64x64 RGB texture for a name.
inline std::vector<unsigned char> genTexture(const std::string& name, int S = 64) {
    std::vector<unsigned char> px(S * S * 3);
    auto set = [&](int x, int y, float r, float g, float b) {
        int i = (y * S + x) * 3;
        px[i] = (unsigned char)clampf(r * 255, 0, 255);
        px[i + 1] = (unsigned char)clampf(g * 255, 0, 255);
        px[i + 2] = (unsigned char)clampf(b * 255, 0, 255);
    };
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float n = texHash(x, y) * 0.5f + texHash(x / 2, y / 2) * 0.5f;
            if (name == "BRICK") {
                int row = y / 16;
                int bx = x + (row % 2) * 16;
                bool mortar = (y % 16 < 2) || (bx % 32 < 2);
                if (mortar) set(x, y, 0.20f, 0.20f, 0.22f);
                else { float v = 0.85f + texHash(bx / 32, row) * 0.3f; set(x, y, 0.42f * v, 0.26f * v, 0.22f * v); }
            } else if (name == "FLOOR") {
                bool grout = (x % 32 < 2) || (y % 32 < 2);
                float t = (((x / 32) + (y / 32)) % 2) ? 0.5f : 0.42f;
                if (grout) set(x, y, 0.18f, 0.18f, 0.2f);
                else set(x, y, t + n * 0.06f, t + n * 0.06f, t * 1.05f + n * 0.06f);
            } else if (name == "METAL") {
                float streak = 0.32f + texHash(0, y) * 0.12f + n * 0.05f;
                set(x, y, streak, streak, streak * 1.1f);
            } else if (name == "DEV") {
                bool line = (x % 16 < 1) || (y % 16 < 1);
                float t = (((x / 16) + (y / 16)) % 2) ? 0.42f : 0.30f;
                if (line) set(x, y, 0.15f, 0.16f, 0.19f);
                else set(x, y, t, t, t * 1.08f);
            } else { // CONCRETE / WALL / default
                float v = 0.5f + (n - 0.5f) * 0.22f;
                set(x, y, v, v, v * 1.02f);
            }
        }
    return px;
}

inline GLuint uploadTexture(const std::vector<unsigned char>& px, int w, int h) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return id;
}

struct TextureCache {
    std::map<std::string, GLuint> byName;
    GLuint get(const std::string& name) {
        auto it = byName.find(name);
        if (it != byName.end()) return it->second;
        GLuint id = uploadTexture(genTexture(name), 64, 64);
        byName[name] = id;
        return id;
    }
};
