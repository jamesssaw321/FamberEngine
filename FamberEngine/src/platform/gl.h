// Loads OpenGL 2.0+ entry points via wglGetProcAddress. GL 1.1 links directly.
#pragma once
#include <windows.h>
#include <GL/gl.h>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_ARRAY_BUFFER    0x8892
#define GL_STATIC_DRAW     0x88E4
#define GL_STREAM_DRAW     0x88E0
#define GL_TEXTURE0        0x84C0
#define GL_TEXTURE1        0x84C1
#define GL_CLAMP_TO_EDGE   0x812F

typedef GLuint (APIENTRY* PFN_glCreateShader)(GLenum);
typedef void   (APIENTRY* PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY* PFN_glCompileShader)(GLuint);
typedef void   (APIENTRY* PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY* PFN_glCreateProgram)(void);
typedef void   (APIENTRY* PFN_glAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY* PFN_glLinkProgram)(GLuint);
typedef void   (APIENTRY* PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY* PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (APIENTRY* PFN_glUseProgram)(GLuint);
typedef void   (APIENTRY* PFN_glDeleteShader)(GLuint);
typedef void   (APIENTRY* PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void   (APIENTRY* PFN_glBindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY* PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef GLint  (APIENTRY* PFN_glGetAttribLocation)(GLuint, const GLchar*);
typedef void   (APIENTRY* PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void   (APIENTRY* PFN_glEnableVertexAttribArray)(GLuint);
typedef void   (APIENTRY* PFN_glDisableVertexAttribArray)(GLuint);
typedef GLint  (APIENTRY* PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (APIENTRY* PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (APIENTRY* PFN_glUniform1f)(GLint, GLfloat);
typedef void   (APIENTRY* PFN_glUniform2f)(GLint, GLfloat, GLfloat);
typedef void   (APIENTRY* PFN_glUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY* PFN_glUniform1i)(GLint, GLint);
typedef void   (APIENTRY* PFN_glActiveTexture)(GLenum);

extern PFN_glCreateShader            glCreateShader;
extern PFN_glShaderSource            glShaderSource;
extern PFN_glCompileShader           glCompileShader;
extern PFN_glGetShaderiv             glGetShaderiv;
extern PFN_glGetShaderInfoLog        glGetShaderInfoLog;
extern PFN_glCreateProgram           glCreateProgram;
extern PFN_glAttachShader            glAttachShader;
extern PFN_glLinkProgram             glLinkProgram;
extern PFN_glGetProgramiv            glGetProgramiv;
extern PFN_glGetProgramInfoLog       glGetProgramInfoLog;
extern PFN_glUseProgram              glUseProgram;
extern PFN_glDeleteShader            glDeleteShader;
extern PFN_glGenBuffers              glGenBuffers;
extern PFN_glBindBuffer              glBindBuffer;
extern PFN_glBufferData              glBufferData;
extern PFN_glGetAttribLocation       glGetAttribLocation;
extern PFN_glVertexAttribPointer     glVertexAttribPointer;
extern PFN_glEnableVertexAttribArray glEnableVertexAttribArray;
extern PFN_glDisableVertexAttribArray glDisableVertexAttribArray;
extern PFN_glGetUniformLocation      glGetUniformLocation;
extern PFN_glUniformMatrix4fv        glUniformMatrix4fv;
extern PFN_glUniform3fv              glUniform3fv;
extern PFN_glUniform1f               glUniform1f;
extern PFN_glUniform2f               glUniform2f;
extern PFN_glUniform3f               glUniform3f;
extern PFN_glUniform1i               glUniform1i;
extern PFN_glActiveTexture           glActiveTexture;

bool loadGL(); // call after wglMakeCurrent
