// Renders a studiomodel: textured + simple lambert. Re-uploads posed verts
// each frame (models are small), grouped by skin texture.
#pragma once
#include "../platform/gl.h"
#include "../core/math.h"
#include "../world/mdl.h"
#include <vector>

static const char* M_VERT =
    "#version 120\n"
    "attribute vec3 aPos; attribute vec3 aNor; attribute vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "varying vec3 vNor; varying vec2 vUV;\n"
    "void main(){ vNor=aNor; vUV=aUV; gl_Position=uMVP*vec4(aPos,1.0); }\n";

static const char* M_FRAG =
    "#version 120\n"
    "uniform sampler2D uTex; uniform vec3 uLightDir;\n"
    "varying vec3 vNor; varying vec2 vUV;\n"
    "void main(){\n"
    "  float d=max(dot(normalize(vNor),-uLightDir),0.0)*0.55+0.6;\n"
    "  vec3 c=texture2D(uTex,vUV).rgb*d;\n"
    "  gl_FragColor=vec4(c,1.0);\n"
    "}\n";

struct ModelGL {
    GLuint prog = 0, vbo = 0;
    GLint uMVP = -1, uTex = -1, uLight = -1, aPos = -1, aNor = -1, aUV = -1;
    std::vector<GLuint> texIds;
    struct Range { int start, count; GLuint tex; };
    std::vector<Range> ranges;

    void init(const Mdl& m) {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &M_VERT, nullptr); glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &M_FRAG, nullptr); glCompileShader(fs);
        prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        uMVP = glGetUniformLocation(prog, "uMVP");
        uTex = glGetUniformLocation(prog, "uTex");
        uLight = glGetUniformLocation(prog, "uLightDir");
        aPos = glGetAttribLocation(prog, "aPos");
        aNor = glGetAttribLocation(prog, "aNor");
        aUV = glGetAttribLocation(prog, "aUV");
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (const MdlTex& t : m.textures) {
            GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, t.w, t.h, 0, GL_RGB, GL_UNSIGNED_BYTE, t.rgb.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            texIds.push_back(id);
        }
        glGenBuffers(1, &vbo);
        upload(m);
    }

    void upload(const Mdl& m) {
        // group verts by texture
        int ntex = (int)texIds.size();
        std::vector<std::vector<float>> byTex(ntex > 0 ? ntex : 1);
        for (const MdlVert& v : m.verts) {
            int t = (v.tex >= 0 && v.tex < ntex) ? v.tex : 0;
            auto& g = byTex[t];
            g.push_back(v.px); g.push_back(v.py); g.push_back(v.pz);
            g.push_back(v.nx); g.push_back(v.ny); g.push_back(v.nz);
            g.push_back(v.u); g.push_back(v.v);
        }
        std::vector<float> all; ranges.clear();
        for (int t = 0; t < (int)byTex.size(); t++) {
            if (byTex[t].empty()) continue;
            int start = (int)all.size() / 8;
            all.insert(all.end(), byTex[t].begin(), byTex[t].end());
            ranges.push_back({start, (int)byTex[t].size() / 8, t < ntex ? texIds[t] : 0});
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(all.size() * sizeof(float)),
                     all.empty() ? nullptr : all.data(), GL_STATIC_DRAW);
    }

    void draw(const Mat4& mvp, const Vec3& lightDir) {
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
        glUseProgram(prog);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
        glUniform1i(uTex, 0);
        Vec3 ld = normalize(lightDir);
        glUniform3fv(uLight, 1, &ld.x);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(aPos, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(aPos);
        glVertexAttribPointer(aNor, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(aNor);
        glVertexAttribPointer(aUV, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(aUV);
        for (const Range& r : ranges) {
            glBindTexture(GL_TEXTURE_2D, r.tex);
            glDrawArrays(GL_TRIANGLES, r.start, r.count);
        }
    }
};
