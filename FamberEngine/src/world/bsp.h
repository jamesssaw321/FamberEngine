// GoldSrc BSP v30 loader: geometry + embedded miptex textures + baked
// lightmaps + entity spawn + clipnode (hull 1) collision.
#pragma once
#include "../core/math.h"
#include "trace.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct dlump_t { int32_t offset, length; };
struct dheader_t { int32_t version; dlump_t lumps[15]; };
struct dplane_t { float normal[3]; float dist; int32_t type; };
struct dvert_t { float x, y, z; };
struct dedge_t { uint16_t v[2]; };
struct dface_t {
    uint16_t plane, side;
    int32_t firstedge;
    uint16_t numedges;
    int16_t texinfo;
    uint8_t styles[4];
    int32_t lightofs;
};
struct dtexinfo_t { float s[4], t[4]; uint32_t miptex, flags; };
struct dmiptex_t { char name[16]; uint32_t width, height, offsets[4]; };
struct dmodel_t { float mins[3], maxs[3], origin[3]; int32_t headnode[4]; int32_t visleafs, firstface, numfaces; };
struct dclipnode_t { int32_t planenum; int16_t children[2]; };
#pragma pack(pop)

enum { LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXTURES, LUMP_VERTEXES, LUMP_VISIBILITY,
       LUMP_NODES, LUMP_TEXINFO, LUMP_FACES, LUMP_LIGHTING, LUMP_CLIPNODES,
       LUMP_LEAFS, LUMP_MARKSURFACES, LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS };

struct BspTex { int w = 0, h = 0; std::vector<unsigned char> rgb; };

struct BspFace {
    std::vector<float> verts; // pos3, uv2, lm2
    int tex = 0;
    int lmW = 0, lmH = 0;
    std::vector<unsigned char> lm; // RGB, empty = fullbright
};

struct Bsp {
    std::vector<BspTex> textures;
    std::vector<BspFace> faces;
    Vec3 spawn{0, 0, 64};
    float spawnYaw = 0;

    // collision (hull 1)
    std::vector<dplane_t> planes;
    std::vector<dclipnode_t> clipnodes;
    int hullHead = 0;

    bool load(const char* path);
    TraceResult hullTrace(const Vec3& start, const Vec3& end) const;
};

template <class T>
static std::vector<T> bspLump(const std::vector<char>& d, const dlump_t& l) {
    std::vector<T> v(l.length > 0 ? l.length / (int)sizeof(T) : 0);
    if (l.length > 0) memcpy(v.data(), d.data() + l.offset, v.size() * sizeof(T));
    return v;
}

inline void bspParseEntities(const std::string& ents, Bsp& bsp) {
    size_t i = 0;
    while (i < ents.size()) {
        size_t open = ents.find('{', i);
        if (open == std::string::npos) break;
        size_t close = ents.find('}', open);
        if (close == std::string::npos) break;
        std::string block = ents.substr(open, close - open);
        i = close + 1;
        auto val = [&](const char* key) -> std::string {
            std::string k = std::string("\"") + key + "\"";
            size_t p = block.find(k);
            if (p == std::string::npos) return "";
            p = block.find('"', p + k.size());
            if (p == std::string::npos) return "";
            size_t e = block.find('"', p + 1);
            return block.substr(p + 1, e - p - 1);
        };
        std::string cls = val("classname");
        if (cls == "info_player_start" || cls == "info_player_deathmatch") {
            std::string o = val("origin");
            Vec3 v; if (sscanf(o.c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3) bsp.spawn = v + Vec3{0, 0, 20};
            std::string a = val("angle");
            if (!a.empty()) bsp.spawnYaw = (float)atof(a.c_str());
        }
    }
}

inline bool Bsp::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[bsp] cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<char> d(n);
    if (fread(d.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);

    dheader_t hdr; memcpy(&hdr, d.data(), sizeof(hdr));
    if (hdr.version != 30) { printf("[bsp] version %d (expected 30)\n", hdr.version); return false; }

    planes = bspLump<dplane_t>(d, hdr.lumps[LUMP_PLANES]);
    clipnodes = bspLump<dclipnode_t>(d, hdr.lumps[LUMP_CLIPNODES]);
    auto verts = bspLump<dvert_t>(d, hdr.lumps[LUMP_VERTEXES]);
    auto edges = bspLump<dedge_t>(d, hdr.lumps[LUMP_EDGES]);
    auto surfedges = bspLump<int32_t>(d, hdr.lumps[LUMP_SURFEDGES]);
    auto texinfos = bspLump<dtexinfo_t>(d, hdr.lumps[LUMP_TEXINFO]);
    auto dfaces = bspLump<dface_t>(d, hdr.lumps[LUMP_FACES]);
    auto models = bspLump<dmodel_t>(d, hdr.lumps[LUMP_MODELS]);
    const dlump_t& lightLump = hdr.lumps[LUMP_LIGHTING];
    const unsigned char* light = (const unsigned char*)d.data() + lightLump.offset;

    if (!models.empty()) hullHead = models[0].headnode[1]; // hull 1

    // ---- textures (embedded miptex + palette) ----
    {
        const dlump_t& tl = hdr.lumps[LUMP_TEXTURES];
        const char* base = d.data() + tl.offset;
        int32_t count = 0; if (tl.length >= 4) memcpy(&count, base, 4);
        const int32_t* offs = (const int32_t*)(base + 4);
        for (int i = 0; i < count; i++) {
            BspTex t;
            if (offs[i] < 0) { t.w = t.h = 16; t.rgb.assign(16 * 16 * 3, 128); textures.push_back(t); continue; }
            const char* mp = base + offs[i];
            dmiptex_t mt; memcpy(&mt, mp, sizeof(mt));
            t.w = mt.width; t.h = mt.height;
            if (mt.offsets[0] == 0 || t.w <= 0 || t.h <= 0 || t.w > 2048 || t.h > 2048) {
                t.w = t.h = 16; t.rgb.assign(16 * 16 * 3, 150);
            } else {
                const unsigned char* idx = (const unsigned char*)(mp + mt.offsets[0]);
                int mip3 = (t.w / 8) * (t.h / 8);
                const unsigned char* pal = (const unsigned char*)(mp + mt.offsets[3]) + mip3 + 2;
                t.rgb.resize(t.w * t.h * 3);
                for (int p = 0; p < t.w * t.h; p++) {
                    unsigned char c = idx[p];
                    t.rgb[p * 3 + 0] = pal[c * 3 + 0];
                    t.rgb[p * 3 + 1] = pal[c * 3 + 1];
                    t.rgb[p * 3 + 2] = pal[c * 3 + 2];
                }
            }
            textures.push_back(t);
        }
    }

    // ---- faces ----
    for (const dface_t& df : dfaces) {
        if (df.texinfo < 0 || df.texinfo >= (int)texinfos.size()) continue;
        const dtexinfo_t& ti = texinfos[df.texinfo];
        int tw = 16, th = 16, texIdx = 0;
        if (ti.miptex < textures.size()) { texIdx = ti.miptex; tw = textures[texIdx].w; th = textures[texIdx].h; }

        std::vector<Vec3> poly;
        std::vector<float> su, sv;
        float minS = 1e9f, maxS = -1e9f, minT = 1e9f, maxT = -1e9f;
        for (int e = 0; e < df.numedges; e++) {
            int se = surfedges[df.firstedge + e];
            int vi = se >= 0 ? edges[se].v[0] : edges[-se].v[1];
            Vec3 p{verts[vi].x, verts[vi].y, verts[vi].z};
            float s = p.x * ti.s[0] + p.y * ti.s[1] + p.z * ti.s[2] + ti.s[3];
            float t = p.x * ti.t[0] + p.y * ti.t[1] + p.z * ti.t[2] + ti.t[3];
            poly.push_back(p); su.push_back(s); sv.push_back(t);
            minS = std::fmin(minS, s); maxS = std::fmax(maxS, s);
            minT = std::fmin(minT, t); maxT = std::fmax(maxT, t);
        }
        if (poly.size() < 3) continue;

        BspFace bf; bf.tex = texIdx;
        float fminS = std::floor(minS / 16) * 16, fmaxS = std::ceil(maxS / 16) * 16;
        float fminT = std::floor(minT / 16) * 16, fmaxT = std::ceil(maxT / 16) * 16;
        bf.lmW = (int)clampf((fmaxS - fminS) / 16 + 1, 1, 512);
        bf.lmH = (int)clampf((fmaxT - fminT) / 16 + 1, 1, 512);
        bool hasLM = df.lightofs >= 0 && df.styles[0] != 255 &&
                     df.lightofs + bf.lmW * bf.lmH * 3 <= lightLump.length;
        if (hasLM) bf.lm.assign(light + df.lightofs, light + df.lightofs + bf.lmW * bf.lmH * 3);

        auto emit = [&](int k) {
            const Vec3& p = poly[k];
            float lu = (su[k] - fminS + 8) / (bf.lmW * 16.0f);
            float lv = (sv[k] - fminT + 8) / (bf.lmH * 16.0f);
            bf.verts.push_back(p.x); bf.verts.push_back(p.y); bf.verts.push_back(p.z);
            bf.verts.push_back(su[k] / tw); bf.verts.push_back(sv[k] / th);
            bf.verts.push_back(lu); bf.verts.push_back(lv);
        };
        for (size_t k = 1; k + 1 < poly.size(); k++) { emit(0); emit(k); emit(k + 1); }
        faces.push_back(std::move(bf));
    }

    std::string ents(d.data() + hdr.lumps[LUMP_ENTITIES].offset, hdr.lumps[LUMP_ENTITIES].length);
    bspParseEntities(ents, *this);

    printf("[bsp] loaded %s: %d faces, %d textures\n", path, (int)faces.size(), (int)textures.size());
    return true;
}

// Recursive point trace through hull-1 clipnodes (hull is pre-expanded for
// the player box, so a point trace == box collision).
inline bool bspHullRecurse(const Bsp& b, int num, float p1f, float p2f,
                           Vec3 p1, Vec3 p2, TraceResult& tr) {
    if (num < 0) {
        if (num != -2) { // not solid
            return true;
        }
        tr.startsolid = tr.startsolid; // solid: caller handles via fraction
        return false;
    }
    const dclipnode_t& node = b.clipnodes[num];
    const dplane_t& pl = b.planes[node.planenum];
    Vec3 n{pl.normal[0], pl.normal[1], pl.normal[2]};
    float t1 = dot(n, p1) - pl.dist;
    float t2 = dot(n, p2) - pl.dist;
    if (t1 >= 0 && t2 >= 0) return bspHullRecurse(b, node.children[0], p1f, p2f, p1, p2, tr);
    if (t1 < 0 && t2 < 0) return bspHullRecurse(b, node.children[1], p1f, p2f, p1, p2, tr);

    const float EPS = 0.03f;
    float frac = (t1 < 0) ? (t1 + EPS) / (t1 - t2) : (t1 - EPS) / (t1 - t2);
    frac = clampf(frac, 0, 1);
    float midf = p1f + (p2f - p1f) * frac;
    Vec3 mid = p1 + (p2 - p1) * frac;
    int side = t1 < 0 ? 1 : 0;

    if (!bspHullRecurse(b, node.children[side], p1f, midf, p1, mid, tr)) return false;

    // did we cross into solid on the far side?
    if (bspHullRecurse(b, node.children[side ^ 1], midf, p2f, mid, p2, tr)) return true;

    if (tr.fraction <= midf) return false; // already hit closer
    tr.fraction = midf;
    Vec3 nrm{pl.normal[0], pl.normal[1], pl.normal[2]};
    if (side) nrm = nrm * -1.0f;
    tr.normal = nrm;
    return false;
}

inline TraceResult Bsp::hullTrace(const Vec3& start, const Vec3& end) const {
    TraceResult tr;
    if (clipnodes.empty()) return tr;
    bspHullRecurse(*this, hullHead, 0, 1, start, end, tr);
    return tr;
}
