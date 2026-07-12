// Load/save Quake/Valve .map files into a Level. Face winding is auto-fixed
// from an interior point, so point order in the file does not matter.
#pragma once
#include "brush.h"
#include "level.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

inline bool readFileAll(const char* path, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.clear();
    if (n > 0) { out.resize(n); size_t r = fread(&out[0], 1, n, f); out.resize(r); }
    fclose(f);
    return true;
}

inline void tokenize(const std::string& s, std::vector<std::string>& t) {
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') i++; continue; }
        if (c == '"') { size_t j = i + 1; std::string v = "\""; while (j < n && s[j] != '"') v += s[j++]; i = j + 1; t.push_back(v); continue; }
        if (c == '{' || c == '}' || c == '(' || c == ')') { t.push_back(std::string(1, c)); i++; continue; }
        size_t j = i;
        while (j < n && !isspace((unsigned char)s[j]) && s[j] != '{' && s[j] != '}' && s[j] != '(' && s[j] != ')') j++;
        t.push_back(s.substr(i, j - i)); i = j;
    }
}

inline std::string stripq(const std::string& s) { return (!s.empty() && s[0] == '"') ? s.substr(1) : s; }

inline bool loadMap(const char* path, Level& lv) {
    std::string s;
    if (!readFileAll(path, s)) return false;
    std::vector<std::string> tk;
    tokenize(s, tk);
    size_t i = 0;
    auto num = [&]() -> float { return i < tk.size() ? (float)atof(tk[i++].c_str()) : 0.0f; };

    while (i < tk.size()) {
        if (tk[i++] != "{") continue;
        std::string classname, lightVal;
        Vec3 origin{0, 0, 0}; bool hasOrigin = false;
        std::vector<Brush> ents;

        while (i < tk.size() && tk[i] != "}") {
            if (tk[i] == "{") {
                i++;
                Brush b; std::vector<Vec3> pts;
                while (i < tk.size() && tk[i] != "}") {
                    if (tk[i] == "(") {
                        Vec3 p[3];
                        for (int k = 0; k < 3; k++) {
                            if (i < tk.size() && tk[i] == "(") i++;
                            p[k] = {num(), num(), num()};
                            if (i < tk.size() && tk[i] == ")") i++;
                        }
                        std::string tex = (i < tk.size() && tk[i] != "(" && tk[i] != "}") ? tk[i++] : "DEV";
                        Vec3 nrm = normalize(cross(p[0] - p[1], p[2] - p[1]));
                        b.faces.push_back({{nrm, dot(nrm, p[0])}, tex});
                        for (auto& q : p) pts.push_back(q);
                    } else i++;
                }
                if (i < tk.size()) i++;
                if (!b.faces.empty()) {
                    Vec3 c{0, 0, 0};
                    for (auto& q : pts) c += q;
                    c = c * (1.0f / (float)pts.size());
                    for (auto& fc : b.faces)
                        if (dot(fc.plane.n, c) - fc.plane.d > 0) { fc.plane.n = fc.plane.n * -1.0f; fc.plane.d = -fc.plane.d; }
                    ents.push_back(b);
                }
            } else {
                std::string key = stripq(tk[i++]);
                std::string val = i < tk.size() ? stripq(tk[i++]) : "";
                if (key == "classname") classname = val;
                else if (key == "origin") { sscanf(val.c_str(), "%f %f %f", &origin.x, &origin.y, &origin.z); hasOrigin = true; }
                else if (key == "_light") lightVal = val;
            }
        }
        if (i < tk.size()) i++;

        for (auto& b : ents) lv.brushes.push_back(b);
        if (classname == "info_player_start" && hasOrigin) lv.spawn = origin;
        if (classname == "light" && hasOrigin) {
            Light L; L.pos = origin;
            float r = 255, g = 255, bl = 255, bright = 200;
            if (!lightVal.empty()) sscanf(lightVal.c_str(), "%f %f %f %f", &r, &g, &bl, &bright);
            L.color = {r / 255.0f, g / 255.0f, bl / 255.0f};
            L.intensity = bright / 200.0f;
            L.radius = clampf(bright * 3.3f, 300.0f, 1400.0f);
            lv.lights.push_back(L);
        }
    }
    return true;
}

inline void writeMap(const char* path, const Level& lv) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "{\n\"classname\" \"worldspawn\"\n");
    for (const Brush& b : lv.brushes) {
        fprintf(f, "{\n");
        for (int i = 0; i < (int)b.faces.size(); i++) {
            std::vector<Vec3> poly = facePolygon(b, i);
            if (poly.size() < 3) continue;
            Vec3 a = poly[0], c = poly[poly.size() / 3], d = poly[2 * poly.size() / 3];
            fprintf(f, "( %g %g %g ) ( %g %g %g ) ( %g %g %g ) %s 0 0 0 1 1\n",
                    a.x, a.y, a.z, c.x, c.y, c.z, d.x, d.y, d.z, b.faces[i].tex.c_str());
        }
        fprintf(f, "}\n");
    }
    fprintf(f, "}\n");
    for (const Light& L : lv.lights) {
        fprintf(f, "{\n\"classname\" \"light\"\n\"origin\" \"%g %g %g\"\n\"_light\" \"%d %d %d %d\"\n}\n",
                L.pos.x, L.pos.y, L.pos.z, (int)(L.color.x * 255), (int)(L.color.y * 255),
                (int)(L.color.z * 255), (int)(L.intensity * 200));
    }
    fprintf(f, "{\n\"classname\" \"info_player_start\"\n\"origin\" \"%g %g %g\"\n}\n",
            lv.spawn.x, lv.spawn.y, lv.spawn.z);
    fclose(f);
}
