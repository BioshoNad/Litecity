// ============================================================================
//  LiteEngine OpenGL layer
//  * Windows: uses the system <GL/gl.h> (OpenGL 1.1) and loads everything newer
//    (shaders, framebuffers, ...) at run time through wglGetProcAddress.
//  * Linux (only used for automated tests): declares the same GL 1.x functions.
//  No GLEW / GLAD / GLU needed.
// ============================================================================
#pragma once
#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#else
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat, GLclampf;
typedef double GLdouble, GLclampd;
typedef unsigned char GLubyte, GLboolean;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_LIGHTING 0x0B50
#define GL_POINT_SMOOTH 0x0B10
#define GL_COLOR_MATERIAL 0x0B57
#define GL_FOG 0x0B60
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_NORMALIZE 0x0BA1
#define GL_VIEWPORT 0x0BA2
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_POSITION 0x1203
#define GL_SHININESS 0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_COMPILE 0x1300
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_DEPTH_COMPONENT 0x1902
#define GL_SMOOTH 0x1D01
#define GL_KEEP 0x1E00
#define GL_INCR 0x1E02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_LIGHT0 0x4000
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_NONE 0
// key codes (identical to the Windows VK_ values) so the game code compiles unchanged for the tests
#define VK_BACK 8
#define VK_RETURN 13
#define VK_SHIFT 16
#define VK_ESCAPE 27
#define VK_SPACE 32
#define VK_LEFT 37
#define VK_UP 38
#define VK_RIGHT 39
#define VK_DOWN 40
#define VK_F2 0x71
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
extern "C" {
void glBegin(GLenum); void glEnd(void);
void glVertex2f(GLfloat, GLfloat); void glVertex3f(GLfloat, GLfloat, GLfloat);
void glNormal3f(GLfloat, GLfloat, GLfloat);
void glColor3f(GLfloat, GLfloat, GLfloat); void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat); void glColor3fv(const GLfloat*);
void glTexCoord2f(GLfloat, GLfloat);
void glBindTexture(GLenum, GLuint); void glBlendFunc(GLenum, GLenum);
void glCallList(GLuint); void glCallLists(GLsizei, GLenum, const void*);
void glClear(GLbitfield); void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
void glColorMaterial(GLenum, GLenum); void glColorMask(GLboolean, GLboolean, GLboolean, GLboolean);
void glDepthMask(GLboolean); void glDisable(GLenum); void glEnable(GLenum);
void glEndList(void); void glNewList(GLuint, GLenum); GLuint glGenLists(GLsizei); void glListBase(GLuint);
void glFogf(GLenum, GLfloat); void glFogfv(GLenum, const GLfloat*); void glFogi(GLenum, GLint);
void glGenTextures(GLsizei, GLuint*); void glDeleteTextures(GLsizei, const GLuint*);
void glGetDoublev(GLenum, GLdouble*); void glGetIntegerv(GLenum, GLint*);
void glLightModelfv(GLenum, const GLfloat*); void glLightfv(GLenum, GLenum, const GLfloat*);
void glLineWidth(GLfloat); void glPointSize(GLfloat);
void glLoadIdentity(void); void glLoadMatrixf(const GLfloat*); void glMultMatrixf(const GLfloat*);
void glMaterialf(GLenum, GLenum, GLfloat); void glMaterialfv(GLenum, GLenum, const GLfloat*);
void glMatrixMode(GLenum);
void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
void glFrustum(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
void glPopMatrix(void); void glPushMatrix(void);
void glRasterPos2f(GLfloat, GLfloat);
void glRotatef(GLfloat, GLfloat, GLfloat, GLfloat); void glScalef(GLfloat, GLfloat, GLfloat); void glTranslatef(GLfloat, GLfloat, GLfloat);
void glScissor(GLint, GLint, GLsizei, GLsizei); void glViewport(GLint, GLint, GLsizei, GLsizei);
void glShadeModel(GLenum);
void glStencilFunc(GLenum, GLint, GLuint); void glStencilOp(GLenum, GLenum, GLenum);
void glTexParameteri(GLenum, GLenum, GLint); void glTexParameterf(GLenum, GLenum, GLfloat); void glTexParameterfv(GLenum, GLenum, const GLfloat*);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
void glPixelStorei(GLenum, GLint);
void glPolygonOffset(GLfloat, GLfloat);
void glDrawBuffer(GLenum); void glReadBuffer(GLenum);
const unsigned char* glGetString(GLenum);
}
#endif

// ---- constants newer than GL 1.1 (missing from the Windows header) --------
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RGBA8 0x8058
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_R_TO_TEXTURE 0x884E
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_NONE
#define GL_NONE 0
#endif

// ---- modern GL functions, loaded at run time ------------------------------
#define LITE_GL_FUNCS(X)                                                                                  \
    X(GLuint, CreateShader, (GLenum))                                                                     \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                          \
    X(void, CompileShader, (GLuint))                                                                      \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                        \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                       \
    X(GLuint, CreateProgram, (void))                                                                      \
    X(void, AttachShader, (GLuint, GLuint))                                                               \
    X(void, LinkProgram, (GLuint))                                                                        \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                       \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                      \
    X(void, UseProgram, (GLuint))                                                                         \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                 \
    X(void, Uniform1i, (GLint, GLint))                                                                    \
    X(void, Uniform1f, (GLint, GLfloat))                                                                  \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                         \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                                \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                                \
    X(void, ActiveTexture, (GLenum))                                                                      \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                          \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                            \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                    \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                                         \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                           \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                                      \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                  \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                           \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                 \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                                \
    X(void, GenerateMipmap, (GLenum))

namespace lite {
#define LITE_DECL(ret, name, args) inline ret(*gx##name) args = nullptr;
LITE_GL_FUNCS(LITE_DECL)
#undef LITE_DECL

// getProc: wglGetProcAddress on Windows, eglGetProcAddress in the headless tests
inline bool loadModernGL(void* (*getProc)(const char*)) {
    bool all = true;
#define LITE_LOAD(ret, name, args)                                   \
    gx##name = (decltype(gx##name))getProc("gl" #name);              \
    if (!gx##name) all = false;
    LITE_GL_FUNCS(LITE_LOAD)
#undef LITE_LOAD
    return all;
}
}  // namespace lite
