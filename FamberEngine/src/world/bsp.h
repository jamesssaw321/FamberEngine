// GoldSrc BSP v30 loader: geometry + embedded miptex textures + baked
// lightmaps + entity spawn + clipnode (hull 1) collision.
#pragma once
#include "../core/math.h"
#include "../core/files.h"
#include "trace.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <map>
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
struct wadheader_t { char id[4]; int32_t numlumps, infotableofs; };
struct wadlump_t { int32_t filepos, disksize, size; char type, compression, pad[2]; char name[16]; };
struct dnode_t {
    int32_t planenum;
    int16_t children[2]; // >=0 node, else -1-leaf
    int16_t mins[3], maxs[3];
    uint16_t firstface, numfaces;
};
struct dleaf_t {
    int32_t contents, visofs; // visofs -1 = no vis
    int16_t mins[3], maxs[3];
    uint16_t firstmark, nummark;
    uint8_t ambient[4];
};
#pragma pack(pop)

enum { LUMP_ENTITIES, LUMP_PLANES, LUMP_TEXTURES, LUMP_VERTEXES, LUMP_VISIBILITY,
       LUMP_NODES, LUMP_TEXINFO, LUMP_FACES, LUMP_LIGHTING, LUMP_CLIPNODES,
       LUMP_LEAFS, LUMP_MARKSURFACES, LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS };

struct BspTex {
    int w = 0, h = 0;
    bool masked = false; // '{' textures: palette index 255 = transparent
    std::string name;
    std::vector<unsigned char> rgba;
};

struct BspFace {
    std::vector<float> verts; // pos3, uv2, lm2
    int tex = 0;
    int src = -1;    // dface index (for PVS lookup)
    int bmodel = 0;  // owning submodel (0 = world, >0 = brush entity)
    bool sky = false;   // "sky" texture: skip geometry, the skybox shows instead
    bool water = false; // '!' liquid texture: translucent pass
    int lmW = 0, lmH = 0;
    std::vector<unsigned char> lm; // RGB, empty = fullbright
};

struct BspEntity {
    std::vector<std::pair<std::string, std::string>> kv;
    std::string get(const char* key) const {
        for (auto& p : kv) if (p.first == key) return p.second;
        return "";
    }
    Vec3 origin() const {
        Vec3 v{0, 0, 0};
        sscanf(get("origin").c_str(), "%f %f %f", &v.x, &v.y, &v.z);
        return v;
    }
    float yaw() const {
        std::string a = get("angles"); // "pitch yaw roll"
        float p, y, r;
        if (!a.empty() && sscanf(a.c_str(), "%f %f %f", &p, &y, &r) == 3) return y;
        a = get("angle");
        return a.empty() ? 0.0f : (float)atof(a.c_str());
    }
};

struct Bsp {
    std::vector<BspTex> textures;
    std::vector<BspFace> faces;
    std::vector<BspEntity> entities;
    Vec3 spawn{0, 0, 64};
    float spawnYaw = 0;
    std::string skyname; // worldspawn "skyname" (HL default: desert)

    // collision (hull 1)
    std::vector<dplane_t> planes;
    std::vector<dclipnode_t> clipnodes;
    int hullHead = 0;

    // visibility (hull 0 render BSP)
    std::vector<dnode_t> nodes;
    std::vector<dleaf_t> leafs;
    std::vector<uint16_t> marks;
    std::vector<unsigned char> visdata;
    std::vector<dmodel_t> models; // 0 = world, rest = brush entities
    int renderHead = 0;
    int dfaceCount = 0;
    int worldFaceEnd = 0; // dfaces below this belong to the world model

    bool load(const char* path);
    TraceResult hullTrace(const Vec3& start, const Vec3& end) const;
    TraceResult hullTraceModel(int m, const Vec3& start, const Vec3& end) const;

    int leafFromNode(int num, const Vec3& p) const {
        while (num >= 0) {
            const dnode_t& n = nodes[num];
            const dplane_t& pl = planes[n.planenum];
            float d = p.x * pl.normal[0] + p.y * pl.normal[1] + p.z * pl.normal[2] - pl.dist;
            num = n.children[d >= 0 ? 0 : 1];
        }
        return -1 - num;
    }

    int leafForPoint(const Vec3& p) const {
        return nodes.empty() ? 0 : leafFromNode(renderHead, p);
    }

    int contents(const Vec3& p) const { // world: -1 empty, -2 solid, -3 water...
        if (nodes.empty()) return -1;
        int leaf = leafFromNode(renderHead, p);
        return leaf >= 0 && leaf < (int)leafs.size() ? leafs[leaf].contents : -1;
    }

    int modelContents(int m, const Vec3& p) const { // brush entity (func_water)
        if (m <= 0 || m >= (int)models.size() || nodes.empty()) return -1;
        int leaf = leafFromNode(models[m].headnode[0], p);
        return leaf >= 0 && leaf < (int)leafs.size() ? leafs[leaf].contents : -1;
    }

    // Fill faceVis (indexed by dface) with the PVS of the leaf holding eye.
    // false = no usable vis info; caller should draw everything.
    bool pvsFaces(const Vec3& eye, std::vector<uint8_t>& faceVis) const {
        if (nodes.empty() || leafs.empty() || visdata.empty() || marks.empty()) return false;
        int leaf = leafForPoint(eye);
        if (leaf <= 0 || leaf >= (int)leafs.size()) return false; // solid/outside
        const dleaf_t& lf = leafs[leaf];
        if (lf.visofs < 0 || lf.visofs >= (int)visdata.size()) return false;

        faceVis.assign(dfaceCount, 0);
        for (int f = worldFaceEnd; f < dfaceCount; f++) faceVis[f] = 1; // brush entities: always

        const unsigned char* in = visdata.data() + lf.visofs;
        const unsigned char* end = visdata.data() + visdata.size();
        int numLeafs = (int)leafs.size();
        for (int cur = 1; cur < numLeafs && in < end;) {
            unsigned char b = *in++;
            if (b == 0) { if (in >= end) break; cur += 8 * (*in++); continue; } // RLE zeros
            for (int bit = 0; bit < 8 && cur < numLeafs; bit++, cur++) {
                if (!(b & (1 << bit))) continue;
                const dleaf_t& vl = leafs[cur];
                for (int m = 0; m < vl.nummark; m++) {
                    size_t mi = vl.firstmark + m;
                    if (mi < marks.size() && marks[mi] < faceVis.size()) faceVis[marks[mi]] = 1;
                }
            }
        }
        return true;
    }
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
        std::string block = ents.substr(open + 1, close - open - 1);
        i = close + 1;

        BspEntity e;
        size_t p = 0; // parse "key" "value" pairs
        while (true) {
            size_t k0 = block.find('"', p);
            if (k0 == std::string::npos) break;
            size_t k1 = block.find('"', k0 + 1);
            if (k1 == std::string::npos) break;
            size_t v0 = block.find('"', k1 + 1);
            if (v0 == std::string::npos) break;
            size_t v1 = block.find('"', v0 + 1);
            if (v1 == std::string::npos) break;
            e.kv.push_back({block.substr(k0 + 1, k1 - k0 - 1), block.substr(v0 + 1, v1 - v0 - 1)});
            p = v1 + 1;
        }
        if (e.kv.empty()) continue;

        std::string cls = e.get("classname");
        if (cls == "info_player_start" || cls == "info_player_deathmatch") {
            bsp.spawn = e.origin() + Vec3{0, 0, 20};
            bsp.spawnYaw = e.yaw();
        }
        if (cls == "worldspawn") bsp.skyname = e.get("skyname");
        bsp.entities.push_back(std::move(e));
    }
}

inline std::string bspLower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// WAD3 archives referenced by worldspawn "wad" (multiplayer maps keep
// their textures there instead of embedding them)
struct WadSet {
    std::vector<std::vector<char>> files;
    std::vector<std::map<std::string, int32_t>> dirs; // lower name -> filepos

    void load(const std::string& modDir, const std::string& wadKey) {
        size_t p = 0;
        while (p < wadKey.size()) {
            size_t e = wadKey.find(';', p);
            std::string w = wadKey.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? wadKey.size() : e + 1;
            size_t sl = w.find_last_of("/\\");
            if (sl != std::string::npos) w = w.substr(sl + 1);
            if (w.empty()) continue;
            std::vector<char> data;
            if (!fs::read(modDir + "/" + w, data) || data.size() < sizeof(wadheader_t)) continue;
            wadheader_t h; memcpy(&h, data.data(), sizeof(h));
            if (memcmp(h.id, "WAD3", 4) != 0) continue;
            std::map<std::string, int32_t> dir;
            for (int i = 0; i < h.numlumps; i++) {
                size_t off = h.infotableofs + i * sizeof(wadlump_t);
                if (off + sizeof(wadlump_t) > data.size()) break;
                wadlump_t l; memcpy(&l, data.data() + off, sizeof(l));
                if (l.type != 0x43) continue; // miptex only
                char nm[17] = {}; memcpy(nm, l.name, 16);
                dir[bspLower(nm)] = l.filepos;
            }
            printf("[wad] %s: %d textures\n", w.c_str(), (int)dir.size());
            files.push_back(std::move(data));
            dirs.push_back(std::move(dir));
        }
    }

    const char* find(const std::string& name) const {
        std::string k = bspLower(name);
        for (size_t i = 0; i < dirs.size(); i++) {
            auto it = dirs[i].find(k);
            if (it != dirs[i].end() && it->second >= 0 &&
                (size_t)it->second + sizeof(dmiptex_t) <= files[i].size())
                return files[i].data() + it->second;
        }
        return nullptr;
    }
};

inline bool Bsp::load(const char* path) {
    std::vector<char> d;
    if (!fs::read(path, d) || d.size() < sizeof(dheader_t)) {
        printf("[bsp] cannot open %s\n", path);
        return false;
    }

    dheader_t hdr; memcpy(&hdr, d.data(), sizeof(hdr));
    if (hdr.version != 30) { printf("[bsp] version %d (expected 30)\n", hdr.version); return false; }

    // entities first: worldspawn lists the .wad files for external textures
    std::string ents(d.data() + hdr.lumps[LUMP_ENTITIES].offset, hdr.lumps[LUMP_ENTITIES].length);
    bspParseEntities(ents, *this);
    std::string modDir = path;
    for (int i = 0; i < 2; i++) {
        size_t sp = modDir.find_last_of("/\\");
        modDir = sp != std::string::npos ? modDir.substr(0, sp) : "";
    }
    WadSet wads;
    for (const BspEntity& e : entities)
        if (e.get("classname") == "worldspawn") { wads.load(modDir, e.get("wad")); break; }

    planes = bspLump<dplane_t>(d, hdr.lumps[LUMP_PLANES]);
    clipnodes = bspLump<dclipnode_t>(d, hdr.lumps[LUMP_CLIPNODES]);
    auto verts = bspLump<dvert_t>(d, hdr.lumps[LUMP_VERTEXES]);
    auto edges = bspLump<dedge_t>(d, hdr.lumps[LUMP_EDGES]);
    auto surfedges = bspLump<int32_t>(d, hdr.lumps[LUMP_SURFEDGES]);
    auto texinfos = bspLump<dtexinfo_t>(d, hdr.lumps[LUMP_TEXINFO]);
    auto dfaces = bspLump<dface_t>(d, hdr.lumps[LUMP_FACES]);
    models = bspLump<dmodel_t>(d, hdr.lumps[LUMP_MODELS]);
    nodes = bspLump<dnode_t>(d, hdr.lumps[LUMP_NODES]);
    leafs = bspLump<dleaf_t>(d, hdr.lumps[LUMP_LEAFS]);
    marks = bspLump<uint16_t>(d, hdr.lumps[LUMP_MARKSURFACES]);
    visdata = bspLump<unsigned char>(d, hdr.lumps[LUMP_VISIBILITY]);
    const dlump_t& lightLump = hdr.lumps[LUMP_LIGHTING];
    const unsigned char* light = (const unsigned char*)d.data() + lightLump.offset;

    dfaceCount = (int)dfaces.size();
    worldFaceEnd = dfaceCount;
    if (!models.empty()) {
        hullHead = models[0].headnode[1]; // hull 1
        renderHead = models[0].headnode[0];
        worldFaceEnd = models[0].firstface + models[0].numfaces;
    }

    // ---- textures (embedded miptex + palette) ----
    {
        const dlump_t& tl = hdr.lumps[LUMP_TEXTURES];
        const char* base = d.data() + tl.offset;
        int32_t count = 0; if (tl.length >= 4) memcpy(&count, base, 4);
        const int32_t* offs = (const int32_t*)(base + 4);
        auto fill = [](BspTex& t, int wh, unsigned char v) {
            t.w = t.h = wh;
            t.rgba.assign(wh * wh * 4, v);
            for (int p = 0; p < wh * wh; p++) t.rgba[p * 4 + 3] = 255;
        };
        // decode a miptex (embedded in the BSP or from a WAD) into RGBA
        auto decode = [](BspTex& t, const char* mp) {
            dmiptex_t mt; memcpy(&mt, mp, sizeof(mt));
            t.w = mt.width; t.h = mt.height;
            if (mt.offsets[0] == 0 || t.w <= 0 || t.h <= 0 || t.w > 2048 || t.h > 2048) return false;
            const unsigned char* idx = (const unsigned char*)(mp + mt.offsets[0]);
            int mip3 = (t.w / 8) * (t.h / 8);
            const unsigned char* pal = (const unsigned char*)(mp + mt.offsets[3]) + mip3 + 2;
            int n = t.w * t.h;
            // average opaque color: fill transparent texels so linear
            // filtering does not bleed the blue key at the edges
            int64_t ar = 0, ag = 0, ab = 0, cnt = 0;
            if (t.masked) {
                for (int p = 0; p < n; p++) {
                    unsigned char c = idx[p];
                    if (c == 255) continue;
                    ar += pal[c * 3]; ag += pal[c * 3 + 1]; ab += pal[c * 3 + 2]; cnt++;
                }
                if (!cnt) cnt = 1;
            }
            t.rgba.resize(n * 4);
            for (int p = 0; p < n; p++) {
                unsigned char c = idx[p];
                if (t.masked && c == 255) {
                    t.rgba[p * 4 + 0] = (unsigned char)(ar / cnt);
                    t.rgba[p * 4 + 1] = (unsigned char)(ag / cnt);
                    t.rgba[p * 4 + 2] = (unsigned char)(ab / cnt);
                    t.rgba[p * 4 + 3] = 0;
                } else {
                    t.rgba[p * 4 + 0] = pal[c * 3 + 0];
                    t.rgba[p * 4 + 1] = pal[c * 3 + 1];
                    t.rgba[p * 4 + 2] = pal[c * 3 + 2];
                    t.rgba[p * 4 + 3] = 255;
                }
            }
            return true;
        };
        int fromWad = 0, missing = 0;
        for (int i = 0; i < count; i++) {
            BspTex t;
            if (offs[i] < 0) { fill(t, 16, 128); textures.push_back(t); continue; }
            const char* mp = base + offs[i];
            dmiptex_t mt; memcpy(&mt, mp, sizeof(mt));
            char nm[17] = {}; memcpy(nm, mt.name, 16);
            t.name = nm;
            t.masked = nm[0] == '{';
            if (mt.offsets[0] != 0) { // embedded
                if (!decode(t, mp)) fill(t, 16, 150);
            } else { // external: look in the map's WADs
                const char* wp = wads.find(t.name);
                if (wp && decode(t, wp)) fromWad++;
                else { fill(t, 16, 150); missing++; }
            }
            textures.push_back(t);
        }
        if (fromWad || missing) printf("[bsp] textures from wads: %d, missing: %d\n", fromWad, missing);
    }

    // ---- faces ----
    std::vector<int> faceModel(dfaces.size(), 0);
    for (int m = 0; m < (int)models.size(); m++)
        for (int k = 0; k < models[m].numfaces; k++) {
            int fi = models[m].firstface + k;
            if (fi >= 0 && fi < (int)faceModel.size()) faceModel[fi] = m;
        }
    for (int fi = 0; fi < (int)dfaces.size(); fi++) {
        const dface_t& df = dfaces[fi];
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

        BspFace bf; bf.tex = texIdx; bf.src = fi; bf.bmodel = faceModel[fi];
        const std::string& tn = textures[texIdx].name;
        bf.sky = tn.size() >= 3 &&
                 (tn[0] == 's' || tn[0] == 'S') && (tn[1] == 'k' || tn[1] == 'K') &&
                 (tn[2] == 'y' || tn[2] == 'Y');
        bf.water = !tn.empty() && tn[0] == '!';
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

    printf("[bsp] loaded %s: %d faces, %d textures, %d leafs, vis %d bytes\n",
           path, (int)faces.size(), (int)textures.size(), (int)leafs.size(), (int)visdata.size());
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

// trace against a brush entity's hull-1 (start/end already in model space)
inline TraceResult Bsp::hullTraceModel(int m, const Vec3& start, const Vec3& end) const {
    TraceResult tr;
    if (m <= 0 || m >= (int)models.size() || clipnodes.empty()) return tr;
    bspHullRecurse(*this, models[m].headnode[1], 0, 1, start, end, tr);
    return tr;
}
