#include "gl.h"
#include <cstdio>
#include <cstdint>

PFN_glCreateShader            glCreateShader = nullptr;
PFN_glShaderSource            glShaderSource = nullptr;
PFN_glCompileShader           glCompileShader = nullptr;
PFN_glGetShaderiv             glGetShaderiv = nullptr;
PFN_glGetShaderInfoLog        glGetShaderInfoLog = nullptr;
PFN_glCreateProgram           glCreateProgram = nullptr;
PFN_glAttachShader            glAttachShader = nullptr;
PFN_glLinkProgram             glLinkProgram = nullptr;
PFN_glGetProgramiv            glGetProgramiv = nullptr;
PFN_glGetProgramInfoLog       glGetProgramInfoLog = nullptr;
PFN_glUseProgram              glUseProgram = nullptr;
PFN_glDeleteShader            glDeleteShader = nullptr;
PFN_glGenBuffers              glGenBuffers = nullptr;
PFN_glBindBuffer              glBindBuffer = nullptr;
PFN_glBufferData              glBufferData = nullptr;
PFN_glGetAttribLocation       glGetAttribLocation = nullptr;
PFN_glVertexAttribPointer     glVertexAttribPointer = nullptr;
PFN_glEnableVertexAttribArray glEnableVertexAttribArray = nullptr;
PFN_glDisableVertexAttribArray glDisableVertexAttribArray = nullptr;
PFN_glGetUniformLocation      glGetUniformLocation = nullptr;
PFN_glUniformMatrix4fv        glUniformMatrix4fv = nullptr;
PFN_glUniform3fv              glUniform3fv = nullptr;
PFN_glUniform1f               glUniform1f = nullptr;
PFN_glUniform2f               glUniform2f = nullptr;
PFN_glUniform3f               glUniform3f = nullptr;
PFN_glUniform1i               glUniform1i = nullptr;
PFN_glActiveTexture           glActiveTexture = nullptr;

static void* getProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    // wgl returns 0/1/2/3/-1 on failure
    if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 ||
        p == (void*)0x3 || p == (void*)(intptr_t)-1)
        return nullptr;
    return p;
}

bool loadGL() {
    bool ok = true;
    auto L = [&](const char* name) -> void* {
        void* p = getProc(name);
        if (!p) { printf("[gl] failed to load %s\n", name); ok = false; }
        return p;
    };
    glCreateShader            = (PFN_glCreateShader)L("glCreateShader");
    glShaderSource            = (PFN_glShaderSource)L("glShaderSource");
    glCompileShader           = (PFN_glCompileShader)L("glCompileShader");
    glGetShaderiv             = (PFN_glGetShaderiv)L("glGetShaderiv");
    glGetShaderInfoLog        = (PFN_glGetShaderInfoLog)L("glGetShaderInfoLog");
    glCreateProgram           = (PFN_glCreateProgram)L("glCreateProgram");
    glAttachShader            = (PFN_glAttachShader)L("glAttachShader");
    glLinkProgram             = (PFN_glLinkProgram)L("glLinkProgram");
    glGetProgramiv            = (PFN_glGetProgramiv)L("glGetProgramiv");
    glGetProgramInfoLog       = (PFN_glGetProgramInfoLog)L("glGetProgramInfoLog");
    glUseProgram              = (PFN_glUseProgram)L("glUseProgram");
    glDeleteShader            = (PFN_glDeleteShader)L("glDeleteShader");
    glGenBuffers              = (PFN_glGenBuffers)L("glGenBuffers");
    glBindBuffer              = (PFN_glBindBuffer)L("glBindBuffer");
    glBufferData              = (PFN_glBufferData)L("glBufferData");
    glGetAttribLocation       = (PFN_glGetAttribLocation)L("glGetAttribLocation");
    glVertexAttribPointer     = (PFN_glVertexAttribPointer)L("glVertexAttribPointer");
    glEnableVertexAttribArray = (PFN_glEnableVertexAttribArray)L("glEnableVertexAttribArray");
    glDisableVertexAttribArray = (PFN_glDisableVertexAttribArray)L("glDisableVertexAttribArray");
    glGetUniformLocation      = (PFN_glGetUniformLocation)L("glGetUniformLocation");
    glUniformMatrix4fv        = (PFN_glUniformMatrix4fv)L("glUniformMatrix4fv");
    glUniform3fv              = (PFN_glUniform3fv)L("glUniform3fv");
    glUniform1f               = (PFN_glUniform1f)L("glUniform1f");
    glUniform2f               = (PFN_glUniform2f)L("glUniform2f");
    glUniform3f               = (PFN_glUniform3f)L("glUniform3f");
    glUniform1i               = (PFN_glUniform1i)L("glUniform1i");
    glActiveTexture           = (PFN_glActiveTexture)L("glActiveTexture");
    return ok;
}
