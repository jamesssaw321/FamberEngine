// GoldSrc studiomodel (.mdl) loader: bones, skinned meshes, palette skins,
// and posing from a sequence's animation (bone quats per frame).
#pragma once
#include "../core/math.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct studiohdr_t {
    int32_t id, version;
    char name[64];
    int32_t length;
    float eyeposition[3], min[3], max[3], bbmin[3], bbmax[3];
    int32_t flags;
    int32_t numbones, boneindex;
    int32_t numbonecontrollers, bonecontrollerindex;
    int32_t numhitboxes, hitboxindex;
    int32_t numseq, seqindex;
    int32_t numseqgroups, seqgroupindex;
    int32_t numtextures, textureindex, texturedataindex;
    int32_t numskinref, numskinfamilies, skinindex;
    int32_t numbodyparts, bodypartindex;
    int32_t numattachments, attachmentindex;
    int32_t soundtable, soundindex, soundgroups, soundgroupindex;
    int32_t numtransitions, transitionindex;
};
struct mstudiobone_t {
    char name[32];
    int32_t parent, flags, bonecontroller[6];
    float value[6], scale[6];
};
struct mstudioseqdesc_t {
    char label[32];
    float fps; int32_t flags, activity, actweight, numevents, eventindex, numframes;
    int32_t numpivots, pivotindex, motiontype, motionbone;
    float linearmovement[3];
    int32_t automoveposindex, automoveangleindex;
    float bbmin[3], bbmax[3];
    int32_t numblends, animindex, blendtype[2];
    float blendstart[2], blendend[2];
    int32_t blendparent, seqgroup, entrynode, exitnode, nodeflags, nextseq;
};
struct mstudioanim_t { uint16_t offset[6]; };
struct mstudiobodyparts_t { char name[64]; int32_t nummodels, base, modelindex; };
struct mstudiomodel_t {
    char name[64];
    int32_t type; float boundingradius;
    int32_t nummesh, meshindex, numverts, vertinfoindex, vertindex;
    int32_t numnorms, norminfoindex, normindex, numgroups, groupindex;
};
struct mstudiomesh_t { int32_t numtris, triindex, skinref, numnorms, normindex; };
struct mstudiotexture_t { char name[64]; int32_t flags, width, height, index; };
union mstudioanimvalue_t { struct { uint8_t valid, total; } num; int16_t value; };
#pragma pack(pop)

struct MdlTex { int w = 0, h = 0; std::vector<unsigned char> rgb; };
struct MdlVert { float px, py, pz, nx, ny, nz, u, v; int tex; };

struct Mdl {
    std::vector<MdlTex> textures;
    std::vector<MdlVert> verts;   // posed, triangulated
    Vec3 center; float radius = 64;
    int numframes = 1; float fps = 10;

    std::vector<char> data;          // main file
    std::vector<char> texData;       // optional <name>T.mdl
    bool load(const char* path);
    void pose(int seq, float frame); // rebuild verts for a frame
};

// ---- bone math (3x4 matrices, quaternions) ----
typedef float Mat34[3][4];

inline void angleQuat(const float a[3], float q[4]) {
    float sr = std::sin(a[0] * 0.5f), cr = std::cos(a[0] * 0.5f);
    float sp = std::sin(a[1] * 0.5f), cp = std::cos(a[1] * 0.5f);
    float sy = std::sin(a[2] * 0.5f), cy = std::cos(a[2] * 0.5f);
    q[0] = sr * cp * cy - cr * sp * sy;
    q[1] = cr * sp * cy + sr * cp * sy;
    q[2] = cr * cp * sy - sr * sp * cy;
    q[3] = cr * cp * cy + sr * sp * sy;
}
inline void quatMat(const float q[4], Mat34 m) {
    m[0][0] = 1 - 2 * (q[1] * q[1] + q[2] * q[2]);
    m[1][0] = 2 * (q[0] * q[1] + q[2] * q[3]);
    m[2][0] = 2 * (q[0] * q[2] - q[1] * q[3]);
    m[0][1] = 2 * (q[0] * q[1] - q[2] * q[3]);
    m[1][1] = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
    m[2][1] = 2 * (q[1] * q[2] + q[0] * q[3]);
    m[0][2] = 2 * (q[0] * q[2] + q[1] * q[3]);
    m[1][2] = 2 * (q[1] * q[2] - q[0] * q[3]);
    m[2][2] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
    m[0][3] = m[1][3] = m[2][3] = 0;
}
inline void concat(const Mat34 a, const Mat34 b, Mat34 o) {
    for (int i = 0; i < 3; i++) {
        o[i][0] = a[i][0] * b[0][0] + a[i][1] * b[1][0] + a[i][2] * b[2][0];
        o[i][1] = a[i][0] * b[0][1] + a[i][1] * b[1][1] + a[i][2] * b[2][1];
        o[i][2] = a[i][0] * b[0][2] + a[i][1] * b[1][2] + a[i][2] * b[2][2];
        o[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3];
    }
}
inline Vec3 xform(const Mat34 m, const Vec3& v) {
    return {v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2] + m[0][3],
            v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2] + m[1][3],
            v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2] + m[2][3]};
}
inline Vec3 xformDir(const Mat34 m, const Vec3& v) {
    return {v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2],
            v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2],
            v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2]};
}

// Decode one animation channel (RLE) at a frame.
inline float animValue(const mstudioanim_t* anim, int frame, int dof, float scale, float def) {
    if (anim->offset[dof] == 0) return def;
    const mstudioanimvalue_t* av =
        (const mstudioanimvalue_t*)((const uint8_t*)anim + anim->offset[dof]);
    int k = frame;
    while (av->num.total <= k) { k -= av->num.total; av += av->num.valid + 1; }
    int16_t v;
    if (av->num.valid > k) v = av[k + 1].value;
    else v = av[av->num.valid].value;
    return def + v * scale;
}

inline bool Mdl::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[mdl] cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    data.resize(n); if (fread(data.data(), 1, n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);

    const studiohdr_t* h = (const studiohdr_t*)data.data();
    if (h->id != 0x54534449) { printf("[mdl] bad id\n"); return false; } // "IDST"

    // textures: embedded, or in <name>T.mdl
    const char* texbase = data.data();
    const studiohdr_t* th = h;
    if (h->numtextures == 0) {
        std::string tp = path;
        size_t dot = tp.rfind(".mdl");
        if (dot != std::string::npos) tp = tp.substr(0, dot) + "T.mdl";
        FILE* tf = fopen(tp.c_str(), "rb");
        if (tf) {
            fseek(tf, 0, SEEK_END); long tn = ftell(tf); fseek(tf, 0, SEEK_SET);
            texData.resize(tn); size_t rd = fread(texData.data(), 1, tn, tf); fclose(tf);
            (void)rd; texbase = texData.data(); th = (const studiohdr_t*)texData.data();
        }
    }
    for (int i = 0; i < th->numtextures; i++) {
        const mstudiotexture_t* t = (const mstudiotexture_t*)(texbase + th->textureindex) + i;
        MdlTex mt; mt.w = t->width; mt.h = t->height;
        const uint8_t* idx = (const uint8_t*)(texbase + t->index);
        const uint8_t* pal = idx + t->width * t->height;
        mt.rgb.resize(t->width * t->height * 3);
        for (int p = 0; p < t->width * t->height; p++) {
            unsigned char c = idx[p];
            mt.rgb[p * 3 + 0] = pal[c * 3 + 0];
            mt.rgb[p * 3 + 1] = pal[c * 3 + 1];
            mt.rgb[p * 3 + 2] = pal[c * 3 + 2];
        }
        textures.push_back(mt);
    }

    center = {(h->min[0] + h->max[0]) * 0.5f, (h->min[1] + h->max[1]) * 0.5f, (h->min[2] + h->max[2]) * 0.5f};
    float dx = h->max[0] - h->min[0], dy = h->max[1] - h->min[1], dz = h->max[2] - h->min[2];
    radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    if (radius < 1) radius = 64;

    if (h->numseq > 0) {
        const mstudioseqdesc_t* s = (const mstudioseqdesc_t*)(data.data() + h->seqindex);
        numframes = s->numframes > 0 ? s->numframes : 1;
        fps = s->fps > 0 ? s->fps : 10;
    }
    pose(0, 0);
    if (!verts.empty()) { // real bounds from posed geometry
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const MdlVert& v : verts) {
            lo.x = std::fmin(lo.x, v.px); hi.x = std::fmax(hi.x, v.px);
            lo.y = std::fmin(lo.y, v.py); hi.y = std::fmax(hi.y, v.py);
            lo.z = std::fmin(lo.z, v.pz); hi.z = std::fmax(hi.z, v.pz);
        }
        center = (lo + hi) * 0.5f;
        radius = 0.5f * length(hi - lo);
        if (radius < 1) radius = 64;
    }
    printf("[mdl] loaded %s: %d bones, %d textures, %d verts, %d frames\n",
           path, h->numbones, (int)textures.size(), (int)verts.size(), numframes);
    return true;
}

inline void Mdl::pose(int seq, float frame) {
    verts.clear();
    const studiohdr_t* h = (const studiohdr_t*)data.data();
    const mstudiobone_t* bones = (const mstudiobone_t*)(data.data() + h->boneindex);

    // per-bone anim for this sequence
    const mstudioanim_t* anims = nullptr;
    int nframes = 1;
    if (seq < h->numseq) {
        const mstudioseqdesc_t* sd = (const mstudioseqdesc_t*)(data.data() + h->seqindex) + seq;
        nframes = sd->numframes > 0 ? sd->numframes : 1;
        if (sd->seqgroup == 0) anims = (const mstudioanim_t*)(data.data() + sd->animindex);
    }
    int fi = (int)frame % (nframes > 0 ? nframes : 1);

    static Mat34 world[128];
    for (int i = 0; i < h->numbones && i < 128; i++) {
        float pos[3], ang[3];
        for (int d = 0; d < 6; d++) {
            float val = bones[i].value[d];
            if (anims) val = animValue(anims + i, fi, d, bones[i].scale[d], bones[i].value[d]);
            if (d < 3) pos[d] = val; else ang[d - 3] = val;
        }
        float q[4]; angleQuat(ang, q);
        Mat34 local; quatMat(q, local);
        local[0][3] = pos[0]; local[1][3] = pos[1]; local[2][3] = pos[2];
        if (bones[i].parent < 0) memcpy(world[i], local, sizeof(Mat34));
        else concat(world[bones[i].parent], local, world[i]);
    }

    const int16_t* skinref = (const int16_t*)(data.data() + h->skinindex);
    const mstudiobodyparts_t* bp = (const mstudiobodyparts_t*)(data.data() + h->bodypartindex);
    for (int b = 0; b < h->numbodyparts; b++) {
        const mstudiomodel_t* mod = (const mstudiomodel_t*)(data.data() + bp[b].modelindex);
        const Vec3* mverts = (const Vec3*)(data.data() + mod->vertindex);
        const Vec3* mnorms = (const Vec3*)(data.data() + mod->normindex);
        const uint8_t* vbone = (const uint8_t*)(data.data() + mod->vertinfoindex);
        const uint8_t* nbone = (const uint8_t*)(data.data() + mod->norminfoindex);

        // pre-transform verts/norms to world
        std::vector<Vec3> wv(mod->numverts), wn(mod->numnorms);
        for (int i = 0; i < mod->numverts; i++) wv[i] = xform(world[vbone[i]], mverts[i]);
        for (int i = 0; i < mod->numnorms; i++) wn[i] = xformDir(world[nbone[i]], mnorms[i]);

        const mstudiomesh_t* meshes = (const mstudiomesh_t*)(data.data() + mod->meshindex);
        for (int m = 0; m < mod->nummesh; m++) {
            int tex = 0; float tw = 64, th = 64;
            if (h->numskinref > 0) { int r = skinref[meshes[m].skinref]; if (r >= 0 && r < (int)textures.size()) { tex = r; tw = textures[r].w; th = textures[r].h; } }
            const int16_t* cmd = (const int16_t*)(data.data() + meshes[m].triindex);
            int c;
            while ((c = *cmd++) != 0) {
                bool fan = c < 0; int cnt = fan ? -c : c;
                std::vector<MdlVert> strip(cnt);
                for (int i = 0; i < cnt; i++) {
                    int vi = cmd[0], ni = cmd[1], s = cmd[2], t = cmd[3]; cmd += 4;
                    Vec3 p = wv[vi], nn = wn[ni];
                    strip[i] = {p.x, p.y, p.z, nn.x, nn.y, nn.z, s / tw, t / th, tex};
                }
                for (int i = 2; i < cnt; i++) {
                    MdlVert a, b2, cc;
                    if (fan) { a = strip[0]; b2 = strip[i - 1]; cc = strip[i]; }
                    else if (i & 1) { a = strip[i - 1]; b2 = strip[i - 2]; cc = strip[i]; }
                    else { a = strip[i - 2]; b2 = strip[i - 1]; cc = strip[i]; }
                    verts.push_back(a); verts.push_back(b2); verts.push_back(cc);
                }
            }
        }
    }
}
