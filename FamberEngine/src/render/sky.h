// GoldSrc skybox: six gfx/env/<skyname><side>.tga images drawn as a cube
// around the eye (Quake st_to_vec orientation), before the world, depth off.
// Includes a small TGA loader (type 2 raw / 10 RLE, 24/32 bpp).
#pragma once
#include "../core/files.h"
#include "../platform/gl.h"
#include "../core/math.h"
#include <cstring>
#include <string>
#include <vector>

inline bool loadTGA(const std::string& path, std::vector<unsigned char>& out, int& w, int& h) {
    std::vector<char> d;
    if (!fs::read(path, d) || d.size() < 18) return false;
    const unsigned char* p = (const unsigned char*)d.data();
    int idlen = p[0], cmap = p[1], type = p[2];
    w = p[12] | (p[13] << 8);
    h = p[14] | (p[15] << 8);
    int bpp = p[16], desc = p[17];
    if (cmap != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32)) return false;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    int bypp = bpp / 8;
    size_t src = 18 + idlen, need = (size_t)w * h * bypp;
    std::vector<unsigned char> px(need);
    if (type == 2) {
        if (d.size() < src + need) return false;
        memcpy(px.data(), p + src, need);
    } else { // RLE
        size_t o = 0;
        while (o < need && src < d.size()) {
            unsigned char hd = p[src++];
            size_t n = (hd & 127) + 1;
            if (hd & 128) { // run packet
                if (src + bypp > d.size()) return false;
                for (size_t i = 0; i < n && o + bypp <= need; i++) { memcpy(&px[o], p + src, bypp); o += bypp; }
                src += bypp;
            } else { // raw packet
                size_t len = n * bypp;
                if (src + len > d.size() || o + len > need) return false;
                memcpy(&px[o], p + src, len);
                o += len; src += len;
            }
        }
        if (o < need) return false;
    }
    bool topdown = (desc & 0x20) != 0;
    out.resize((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        int sy = topdown ? y : h - 1 - y;
        for (int x = 0; x < w; x++) {
            const unsigned char* s = &px[((size_t)sy * w + x) * bypp];
            unsigned char* dst = &out[((size_t)y * w + x) * 3];
            dst[0] = s[2]; dst[1] = s[1]; dst[2] = s[0]; // BGR -> RGB
        }
    }
    return true;
}

static const char* SKY_VERT =
    "#version 120\n"
    "attribute vec3 aPos; attribute vec2 aUV;\n"
    "uniform mat4 uMVP; uniform vec3 uEye;\n"
    "varying vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=uMVP*vec4(aPos*64.0+uEye,1.0); }\n";

static const char* SKY_FRAG =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUV;\n"
    "void main(){ gl_FragColor=vec4(texture2D(uTex,vUV).rgb,1.0); }\n";

struct SkyBox {
    GLuint prog = 0, vbo = 0, tex[6] = {};
    GLint aPos = -1, aUV = -1, uMVP = -1, uEye = -1, uTex = -1;
    bool ok = false;

    // Quake sky side orientation: side order rt,bk,lf,ft,up,dn;
    // v[j] = ±(s,t,1)[|k|-1] for k = st2v[side][j]
    static void skyVec(float s, float t, int axis, float* v) {
        static const int st2v[6][3] = {{3,-1,2}, {-3,1,2}, {1,3,2}, {-1,-3,2}, {-2,-1,3}, {2,-1,-3}};
        float b[3] = {s, t, 1};
        for (int j = 0; j < 3; j++) {
            int k = st2v[axis][j];
            v[j] = k < 0 ? -b[-k - 1] : b[k - 1];
        }
    }

    bool init(const std::string& modDir, std::string skyname) {
        if (skyname.empty()) skyname = "desert";
        static const char* suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
        for (int i = 0; i < 6; i++) {
            std::vector<unsigned char> px; int w, h;
            if (!loadTGA(modDir + "/gfx/env/" + skyname + suf[i] + ".tga", px, w, h)) {
                printf("[sky] missing %s%s.tga\n", skyname.c_str(), suf[i]);
                return false;
            }
            glGenTextures(1, &tex[i]);
            glBindTexture(GL_TEXTURE_2D, tex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &SKY_VERT, nullptr); glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &SKY_FRAG, nullptr); glCompileShader(fs);
        prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        aPos = glGetAttribLocation(prog, "aPos");
        aUV = glGetAttribLocation(prog, "aUV");
        uMVP = glGetUniformLocation(prog, "uMVP");
        uEye = glGetUniformLocation(prog, "uEye");
        uTex = glGetUniformLocation(prog, "uTex");

        std::vector<float> v; // 6 sides x 2 triangles, pos3 + uv2
        const float C[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        for (int side = 0; side < 6; side++)
            for (int k = 0; k < 6; k++) {
                float s = C[k][0], t = C[k][1], p[3];
                skyVec(s, t, side, p);
                v.push_back(p[0]); v.push_back(p[1]); v.push_back(p[2]);
                v.push_back((s + 1) * 0.5f);
                v.push_back(1.0f - (t + 1) * 0.5f);
            }
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        printf("[sky] %s loaded\n", skyname.c_str());
        return ok = true;
    }

    void draw(const Mat4& mvp, const Vec3& eye) {
        if (!ok) return;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glUseProgram(prog);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
        glUniform3f(uEye, eye.x, eye.y, eye.z);
        glUniform1i(uTex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 20, (void*)0);
        glEnableVertexAttribArray(aPos);
        glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, 20, (void*)12);
        glEnableVertexAttribArray(aUV);
        for (int i = 0; i < 6; i++) {
            glBindTexture(GL_TEXTURE_2D, tex[i]);
            glDrawArrays(GL_TRIANGLES, i * 6, 6);
        }
        glDisableVertexAttribArray(aUV);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
};
