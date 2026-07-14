// Scene renderer: draws base*lightmap. Feeds from either a brush Level
// (procedural textures + baked lightmaps) or a loaded BSP.
#pragma once
#include "../platform/gl.h"
#include "../core/math.h"
#include "../world/level.h"
#include "../world/bsp.h"
#include "texture.h"
#include "lightmap.h"
#include "sky.h"
#include <vector>
#include <cstdio>

static const char* R_VERT =
    "#version 120\n"
    "attribute vec3 aPos; attribute vec2 aUV; attribute vec2 aLM;\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "varying vec2 vUV; varying vec2 vLM;\n"
    "void main(){ vUV=aUV; vLM=aLM; gl_Position=uMVP*(uModel*vec4(aPos,1.0)); }\n";

static const char* R_FRAG =
    "#version 120\n"
    "uniform sampler2D uBase; uniform sampler2D uLight; uniform float uScale; uniform float uAlpha;\n"
    "varying vec2 vUV; varying vec2 vLM;\n"
    "void main(){\n"
    "  vec4 b=texture2D(uBase,vUV);\n"
    "  if(b.a<0.5) discard;\n" // '{' masked textures
    "  vec3 l=texture2D(uLight,vLM).rgb;\n"
    "  vec3 c=b.rgb*l*uScale;\n"
    "  gl_FragColor=vec4(pow(c, vec3(0.73)),uAlpha);\n" // mild gamma lift
    "}\n";

struct FaceDraw {
    int start, count;
    GLuint baseTex, lmTex;
    int src = -1;
    int bmodel = 0;
    bool sky = false;
    bool water = false;
};

struct Renderer {
    GLuint prog = 0, vbo = 0, whiteTex = 0;
    GLint uMVP = -1, uBase = -1, uLight = -1, uScale = -1, uModel = -1, uAlpha = -1,
          aPos = -1, aUV = -1, aLM = -1;
    const std::vector<Mat4>* modelXforms = nullptr; // brush entity transforms (movers)
    std::vector<uint8_t> hiddenModels; // trigger/ladder brushes: never drawn
    std::vector<FaceDraw> faces;
    TextureCache textures;
    float scale = 1.7f;
    float fov = 90.0f;

    SkyBox skybox;

    // PVS culling (BSP mode)
    const Bsp* bspSrc = nullptr;
    float* novis = nullptr;    // r_novis cvar binds here
    std::vector<uint8_t> faceVis;
    int lastLeaf = -2;         // -2 = force recompute
    bool havePVS = false;
    int drawnFaces = 0;

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); printf("[shader] %s\n", log); }
        return s;
    }

    static GLuint uploadRGB(const unsigned char* px, int w, int h, GLint wrap) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        return id;
    }

    static GLuint uploadRGBA(const unsigned char* px, int w, int h, GLint wrap) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        return id;
    }

    void setupProgram() {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        GLuint vs = compile(GL_VERTEX_SHADER, R_VERT), fs = compile(GL_FRAGMENT_SHADER, R_FRAG);
        prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        uMVP = glGetUniformLocation(prog, "uMVP");
        uBase = glGetUniformLocation(prog, "uBase");
        uLight = glGetUniformLocation(prog, "uLight");
        uScale = glGetUniformLocation(prog, "uScale");
        uModel = glGetUniformLocation(prog, "uModel");
        uAlpha = glGetUniformLocation(prog, "uAlpha");
        aPos = glGetAttribLocation(prog, "aPos");
        aUV = glGetAttribLocation(prog, "aUV");
        aLM = glGetAttribLocation(prog, "aLM");
        unsigned char white[3] = {255, 255, 255};
        whiteTex = uploadRGB(white, 1, 1, GL_CLAMP_TO_EDGE);
    }

    void finishBuffer(const std::vector<float>& verts) {
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
    }

    // ---- brush level (procedural textures + baked lightmaps) ----
    void init(const Level& lv) {
        setupProgram();
        scale = 1.7f;
        std::vector<float> verts;
        for (const Brush& b : lv.brushes)
            for (int i = 0; i < (int)b.faces.size(); i++) {
                std::vector<Vec3> poly = facePolygon(b, i);
                if (poly.size() < 3) continue;
                Vec3 right, up; faceBasis(b.faces[i].plane.n, right, up);
                Lightmap lm = bakeFaceLightmap(b, i, poly, lv, lv.brushes);
                GLuint lmTex = uploadRGB(lm.px.data(), lm.w, lm.h, GL_CLAMP_TO_EDGE);
                GLuint baseTex = textures.get(b.faces[i].tex);
                int start = (int)verts.size() / 7;
                auto emit = [&](const Vec3& p) {
                    float u, v; lmUV(lm, p, u, v);
                    verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
                    verts.push_back(dot(p, right) / 64.0f); verts.push_back(dot(p, up) / 64.0f);
                    verts.push_back(u); verts.push_back(v);
                };
                for (size_t k = 1; k + 1 < poly.size(); k++) { emit(poly[0]); emit(poly[k]); emit(poly[k + 1]); }
                faces.push_back({start, (int)verts.size() / 7 - start, baseTex, lmTex});
            }
        finishBuffer(verts);
    }

    // ---- loaded BSP ----
    void initBSP(const Bsp& bsp, const std::string& modDir = "") {
        setupProgram();
        scale = 1.0f; // GoldSrc lightmaps are ~1:1
        bspSrc = &bsp;
        std::vector<GLuint> texIds;
        for (const BspTex& t : bsp.textures) texIds.push_back(uploadRGBA(t.rgba.data(), t.w, t.h, GL_REPEAT));

        std::vector<float> verts;
        bool anySky = false;
        for (const BspFace& bf : bsp.faces) {
            if (bf.verts.empty()) continue;
            int start = (int)verts.size() / 7;
            verts.insert(verts.end(), bf.verts.begin(), bf.verts.end());
            GLuint lmTex = bf.lm.empty() ? whiteTex : uploadRGB(bf.lm.data(), bf.lmW, bf.lmH, GL_CLAMP_TO_EDGE);
            GLuint baseTex = (bf.tex >= 0 && bf.tex < (int)texIds.size()) ? texIds[bf.tex] : whiteTex;
            faces.push_back({start, (int)bf.verts.size() / 7, baseTex, lmTex, bf.src, bf.bmodel, bf.sky, bf.water});
            anySky = anySky || bf.sky;
        }
        finishBuffer(verts);
        if (anySky && !modDir.empty()) skybox.init(modDir, bsp.skyname);
    }

    void draw(const Vec3& eye, const Vec3& fwd, int w, int h) {
        glViewport(0, 0, w, h);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (float)w / (h > 0 ? h : 1);
        Mat4 proj = mat4Perspective(fov * 3.14159265f / 180.0f, aspect, 4.0f, 16384.0f);
        Mat4 view = mat4LookAt(eye, eye + fwd, {0, 0, 1});
        Mat4 mvp = mat4Mul(proj, view);

        skybox.draw(mvp, eye); // behind everything, sky faces are skipped below

        bool usePVS = bspSrc && !(novis && *novis > 0);
        if (usePVS) {
            int leaf = bspSrc->leafForPoint(eye);
            if (leaf != lastLeaf) { lastLeaf = leaf; havePVS = bspSrc->pvsFaces(eye, faceVis); }
        } else lastLeaf = -2; // recompute when re-enabled

        glUseProgram(prog);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
        glUniform1i(uBase, 0);
        glUniform1i(uLight, 1);
        glUniform1f(uScale, scale);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(aPos);
        glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(aUV);
        glVertexAttribPointer(aLM, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(aLM);

        drawnFaces = 0;
        Mat4 ident = mat4Identity();
        glUniformMatrix4fv(uModel, 1, GL_FALSE, ident.m);
        glUniform1f(uAlpha, 1.0f);
        int curModel = 0;
        bool anyWater = false;
        auto drawFace = [&](const FaceDraw& fd) {
            if (fd.bmodel != curModel) {
                curModel = fd.bmodel;
                const Mat4* xf = &ident;
                if (modelXforms && curModel > 0 && curModel < (int)modelXforms->size())
                    xf = &(*modelXforms)[curModel];
                glUniformMatrix4fv(uModel, 1, GL_FALSE, xf->m);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fd.baseTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, fd.lmTex);
            glDrawArrays(GL_TRIANGLES, fd.start, fd.count);
        };
        auto visible = [&](const FaceDraw& fd) {
            if (fd.sky) return false; // the skybox shows through
            if (fd.bmodel > 0 && fd.bmodel < (int)hiddenModels.size() && hiddenModels[fd.bmodel]) return false;
            if (usePVS && havePVS && fd.src >= 0 && (size_t)fd.src < faceVis.size() && !faceVis[fd.src]) return false;
            return true;
        };
        for (const FaceDraw& fd : faces) {
            if (!visible(fd)) continue;
            if (fd.water) { anyWater = true; continue; } // translucent pass below
            drawnFaces++;
            drawFace(fd);
        }
        if (anyWater) { // water surfaces: blended, no depth writes
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glUniform1f(uAlpha, 0.6f);
            for (const FaceDraw& fd : faces) {
                if (!fd.water || !visible(fd)) continue;
                drawnFaces++;
                drawFace(fd);
            }
            glUniform1f(uAlpha, 1.0f);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }
};
