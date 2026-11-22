// ============================================================================
//  LiteEngine  -  a tiny, dependency-free 3D game engine for Windows
//  Platform : Win32 + OpenGL 1.x (fixed pipeline)  - no SDL, no GLFW, no GLEW
//  Provides : window + GL context, input, timing, math helpers, cube/box
//             drawing, procedural textures, bitmap text, 2D overlay helpers.
//  The game (main.cpp) is built ON TOP of this header.
// ============================================================================
#pragma once
#include "glapi.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace lite {

// ------------------------------------------------------------------ math ---
constexpr float PI = 3.14159265f;
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float frand() { return (float)rand() / (float)RAND_MAX; }
inline float frange(float a, float b) { return a + (b - a) * frand(); }
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

// -------------------------------------------------------------- platform ---
struct Platform {
#ifdef _WIN32
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    LARGE_INTEGER freq, last;
#endif
    int w = 1280, h = 720;
    bool running = true;
    bool down[256] = {};
    bool pressed[256] = {};
    GLuint fontSmall = 0, fontMid = 0, fontBig = 0;
    int mx = 0, my = 0;
    bool mouseDown = false, mousePressed = false;
    std::string typed;  // characters typed this frame
};
inline Platform& plat() { static Platform p; return p; }

inline bool key(int vk) { return plat().down[vk & 255]; }
inline bool tapped(int vk) { return plat().pressed[vk & 255]; }
inline float mouseX() { return (float)plat().mx; }
inline float mouseY() { return (float)plat().my; }
inline bool mouseClicked() { return plat().mousePressed; }
inline bool mouseHeld() { return plat().mouseDown; }

#ifdef _WIN32
inline LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Platform& p = plat();
    switch (m) {
    case WM_CLOSE: p.running = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        if (wp < 256) { if (!p.down[wp]) p.pressed[wp] = true; p.down[wp] = true; }
        return 0;
    case WM_KEYUP:
        if (wp < 256) p.down[wp] = false;
        return 0;
    case WM_MOUSEMOVE: p.mx = (short)LOWORD(lp); p.my = (short)HIWORD(lp); return 0;
    case WM_LBUTTONDOWN: p.mouseDown = true; p.mousePressed = true; p.mx = (short)LOWORD(lp); p.my = (short)HIWORD(lp); return 0;
    case WM_LBUTTONUP: p.mouseDown = false; return 0;
    case WM_CHAR:
        if (wp >= 32 && wp < 127) p.typed.push_back((char)wp);
        return 0;
    case WM_KILLFOCUS: memset(p.down, 0, sizeof(p.down)); return 0;
    case WM_SIZE: p.w = LOWORD(lp); p.h = HIWORD(lp); return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

inline GLuint makeFont(HDC hdc, int height) {
    HFONT f = CreateFontA(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    SelectObject(hdc, f);
    GLuint base = glGenLists(96);
    wglUseFontBitmapsA(hdc, 32, 96, base);
    return base;
}

inline bool init(const char* title, int w, int h) {
    Platform& p = plat();
    p.w = w; p.h = h;
    HINSTANCE hi = GetModuleHandleA(nullptr);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "LiteEngineWnd";
    RegisterClassA(&wc);
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    p.hwnd = CreateWindowExA(0, "LiteEngineWnd", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, hi, nullptr);
    if (!p.hwnd) return false;
    p.hdc = GetDC(p.hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(p.hdc, &pfd);
    if (!pf || !SetPixelFormat(p.hdc, pf, &pfd)) return false;
    p.rc = wglCreateContext(p.hdc);
    if (!p.rc || !wglMakeCurrent(p.hdc, p.rc)) return false;
    typedef BOOL(WINAPI * SwapFn)(int);
    SwapFn sw = (SwapFn)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (sw) sw(1);  // vsync
    p.fontSmall = makeFont(p.hdc, 15);
    p.fontMid = makeFont(p.hdc, 26);
    p.fontBig = makeFont(p.hdc, 54);
    QueryPerformanceFrequency(&p.freq);
    QueryPerformanceCounter(&p.last);
    return true;
}

// Pump window messages, compute delta time. Returns false when the window closed.
inline bool pump(float& dt) {
    Platform& p = plat();
    memset(p.pressed, 0, sizeof(p.pressed));
    p.mousePressed = false;
    p.typed.clear();
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) p.running = false;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    dt = (float)(now.QuadPart - p.last.QuadPart) / (float)p.freq.QuadPart;
    p.last = now;
    if (dt > 0.05f) dt = 0.05f;
    return p.running;
}
inline void present() { SwapBuffers(plat().hdc); }
#endif
inline void quit() { plat().running = false; }

// ------------------------------------------------- small GLU replacements ---
inline void gluPerspective(double fovy, double aspect, double zn, double zf) {
    double t = zn * tan(fovy * 3.14159265358979 / 360.0);
    glFrustum(-t * aspect, t * aspect, -t, t, zn, zf);
}
inline void lookAtMatrix(float* m, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= sl; sy /= sl; sz /= sl;
    float vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    m[0] = sx; m[4] = sy; m[8] = sz;   m[12] = -(sx * ex + sy * ey + sz * ez);
    m[1] = vx; m[5] = vy; m[9] = vz;   m[13] = -(vx * ex + vy * ey + vz * ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = (fx * ex + fy * ey + fz * ez);
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}
inline void gluLookAt(double ex, double ey, double ez, double cx, double cy, double cz, double ux, double uy, double uz) {
    float m[16];
    lookAtMatrix(m, (float)ex, (float)ey, (float)ez, (float)cx, (float)cy, (float)cz, (float)ux, (float)uy, (float)uz);
    glMultMatrixf(m);
}
inline int gluProject(double x, double y, double z, const double* mv, const double* pr, const int* vp, double* wx, double* wy, double* wz) {
    double a[4] = {x, y, z, 1}, b[4], c[4];
    for (int r = 0; r < 4; r++) b[r] = mv[r] * a[0] + mv[4 + r] * a[1] + mv[8 + r] * a[2] + mv[12 + r] * a[3];
    for (int r = 0; r < 4; r++) c[r] = pr[r] * b[0] + pr[4 + r] * b[1] + pr[8 + r] * b[2] + pr[12 + r] * b[3];
    if (c[3] == 0.0) return 0;
    for (int r = 0; r < 3; r++) c[r] /= c[3];
    *wx = vp[0] + (1 + c[0]) * vp[2] * 0.5;
    *wy = vp[1] + (1 + c[1]) * vp[3] * 0.5;
    *wz = (1 + c[2]) * 0.5;
    return 1;
}
// builds all mip levels (RGBA / unsigned byte only) with a box filter
inline int gluBuild2DMipmaps(GLenum target, GLint internalFmt, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void* data) {
    std::vector<unsigned char> cur((const unsigned char*)data, (const unsigned char*)data + (size_t)w * h * 4), next;
    int level = 0;
    for (;;) {
        glTexImage2D(target, level, internalFmt, w, h, 0, fmt, type, cur.data());
        if (w == 1 && h == 1) break;
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        next.assign((size_t)nw * nh * 4, 0);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1), y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                    int sum = cur[((size_t)y0 * w + x0) * 4 + c] + cur[((size_t)y0 * w + x1) * 4 + c] + cur[((size_t)y1 * w + x0) * 4 + c] + cur[((size_t)y1 * w + x1) * 4 + c];
                    next[((size_t)y * nw + x) * 4 + c] = (unsigned char)(sum / 4);
                }
        cur.swap(next);
        w = nw; h = nh; level++;
    }
    return 0;
}

// -------------------------------------------------------------- renderer ---
inline void begin3D(float fov, float zn, float zf) {
    Platform& p = plat();
    glViewport(0, 0, p.w, p.h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, (double)p.w / (double)(p.h ? p.h : 1), zn, zf);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
inline void begin2D() {
    Platform& p = plat();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, p.w, p.h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
inline void end2D() {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

inline void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}
inline void text(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    glListBase(plat().fontSmall - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
}
inline void textBig(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    glListBase(plat().fontBig - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
}
inline void textMid(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    glListBase(plat().fontMid - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
}
inline float textWidthBig(const std::string& s) { return (float)s.size() * 29.f; }
inline float textWidthMid(const std::string& s) { return (float)s.size() * 14.f; }
inline float textWidth(const std::string& s) { return (float)s.size() * 8.f; }

// filled rounded rectangle (2D pass)
inline void roundRect(float x, float y, float w, float h, float rad, float r, float g, float b, float a = 1.f) {
    rad = rad > w * 0.5f ? w * 0.5f : (rad > h * 0.5f ? h * 0.5f : rad);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x + rad, y); glVertex2f(x + w - rad, y); glVertex2f(x + w - rad, y + h); glVertex2f(x + rad, y + h);
    glVertex2f(x, y + rad); glVertex2f(x + rad, y + rad); glVertex2f(x + rad, y + h - rad); glVertex2f(x, y + h - rad);
    glVertex2f(x + w - rad, y + rad); glVertex2f(x + w, y + rad); glVertex2f(x + w, y + h - rad); glVertex2f(x + w - rad, y + h - rad);
    glEnd();
    const float cx[4] = {x + rad, x + w - rad, x + w - rad, x + rad};
    const float cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    for (int c = 0; c < 4; c++) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx[c], cy[c]);
        for (int i = 0; i <= 8; i++) {
            float a2 = (c * 90.f + i * 90.f / 8.f + 180.f) * PI / 180.f;
            glVertex2f(cx[c] + cosf(a2) * rad, cy[c] + sinf(a2) * rad);
        }
        glEnd();
    }
}
inline bool inRect(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
inline void scissor(float x, float y, float w, float h) {  // top-left origin
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x, (GLint)((float)plat().h - y - h), (GLsizei)w, (GLsizei)h);
}
inline void noScissor() { glDisable(GL_SCISSOR_TEST); }

// unit cube centred on the origin (-0.5..0.5), with normals
inline void cube() {
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glVertex3f(-.5f, -.5f, .5f);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(.5f, .5f, .5f);   glVertex3f(-.5f, .5f, .5f);
    glNormal3f(0, 0, -1); glVertex3f(.5f, -.5f, -.5f);  glVertex3f(-.5f, -.5f, -.5f); glVertex3f(-.5f, .5f, -.5f); glVertex3f(.5f, .5f, -.5f);
    glNormal3f(1, 0, 0);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(.5f, -.5f, -.5f);  glVertex3f(.5f, .5f, -.5f);  glVertex3f(.5f, .5f, .5f);
    glNormal3f(-1, 0, 0); glVertex3f(-.5f, -.5f, -.5f); glVertex3f(-.5f, -.5f, .5f);  glVertex3f(-.5f, .5f, .5f);  glVertex3f(-.5f, .5f, -.5f);
    glNormal3f(0, 1, 0);  glVertex3f(-.5f, .5f, .5f);   glVertex3f(.5f, .5f, .5f);    glVertex3f(.5f, .5f, -.5f);  glVertex3f(-.5f, .5f, -.5f);
    glNormal3f(0, -1, 0); glVertex3f(-.5f, -.5f, -.5f); glVertex3f(.5f, -.5f, -.5f);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(-.5f, -.5f, .5f);
    glEnd();
}
// axis-aligned box: centre + full size
inline void box(float cx, float cy, float cz, float sx, float sy, float sz) {
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glScalef(sx, sy, sz);
    cube();
    glPopMatrix();
}

// Procedural window texture for buildings (8x8 windows, ~30% lit)
inline GLuint makeWindowTexture() {
    const int S = 64;
    static unsigned char px[S * S * 4];
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 8; cx++) {
            bool lit = frand() < 0.30f;
            unsigned char wr = lit ? 255 : 45, wg = lit ? 215 : 60, wb = lit ? 120 : 88;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    unsigned char* o = &px[((cy * 8 + y) * S + (cx * 8 + x)) * 4];
                    bool win = x >= 2 && x <= 5 && y >= 2 && y <= 5;
                    o[0] = win ? wr : 225; o[1] = win ? wg : 225; o[2] = win ? wb : 232; o[3] = 255;
                }
        }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, 4, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

// world position -> window pixel (call while the 3D matrices are still active)
inline bool project(float x, float y, float z, float& sx, float& sy) {
    GLdouble mv[16], pr[16], wx, wy, wz;
    GLint vp[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (gluProject(x, y, z, mv, pr, vp, &wx, &wy, &wz) == GL_FALSE) return false;
    if (wz >= 1.0) return false;
    sx = (float)wx;
    sy = (float)(plat().h - wy);
    return true;
}

}  // namespace lite
