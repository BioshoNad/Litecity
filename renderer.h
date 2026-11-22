// ============================================================================
//  LiteEngine renderer (v0.3) - shader pipeline layered on top of the classic
//  immediate-mode drawing code:
//    * per-pixel lighting (sun + sky/ground ambient + contact AO)
//    * real sun shadow map (2048^2, 3x3 PCF, texel-snapped)
//    * sky reflections + Fresnel + sun glints on glossy surfaces (car paint, glass)
//    * emissive windows at night, ACES tone mapping, gamma-correct output, fog
//    * 4x MSAA, bloom, vignette, colour grade, dithering
//  If anything is unsupported (old GPU / driver) init() returns false, writes the
//  reason to sample_gl_log.txt and the game keeps using the classic renderer.
// ============================================================================
#pragma once
#include "engine.h"
#include <cstdio>
#include <functional>
#include <string>

namespace lite {
namespace rt {

// -------------------------------------------------------------- matrices ---
struct Mat4 { float m[16]; };  // column-major, like OpenGL
inline Mat4 ident() { Mat4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f; return r; }
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
inline Mat4 lookAt(float ex, float ey, float ez, float cx, float cy, float cz) {
    Mat4 r;
    lookAtMatrix(r.m, ex, ey, ez, cx, cy, cz, 0, 1, 0);
    return r;
}
inline Mat4 ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 m = ident();
    m.m[0] = 2 / (r - l); m.m[5] = 2 / (t - b); m.m[10] = -2 / (f - n);
    m.m[12] = -(r + l) / (r - l); m.m[13] = -(t + b) / (t - b); m.m[14] = -(f + n) / (f - n);
    return m;
}
inline Mat4 invRigid(const Mat4& v) {  // inverse of a rotation + translation matrix
    Mat4 r = ident();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[j * 4 + i] = v.m[i * 4 + j];
    float tx = v.m[12], ty = v.m[13], tz = v.m[14];
    for (int i = 0; i < 3; i++) r.m[12 + i] = -(r.m[0 * 4 + i] * tx + r.m[1 * 4 + i] * ty + r.m[2 * 4 + i] * tz);
    return r;
}

struct Params {
    Mat4 view = ident();
    float sunDir[3] = {0.4f, 0.8f, 0.5f};  // world space, pointing TOWARD the sun (normalised)
    float sunCol[3] = {3, 3, 3}, skyCol[3] = {0.5f, 0.6f, 0.8f}, groundCol[3] = {0.2f, 0.2f, 0.2f};
    float horizon[3] = {0.6f, 0.75f, 0.95f}, zenith[3] = {0.15f, 0.35f, 0.8f}, fogCol[3] = {0.6f, 0.75f, 0.95f};
    float fogDen = 0.0025f, night = 0.f, exposure = 1.f, bloom = 0.5f;
    float focus[3] = {0, 0, 0};  // the shadow map is centred here
};

// ----------------------------------------------------------------- state ---
struct State {
    bool ok = false;
    int w = 0, h = 0, samples = 0, shSize = 2048;
    bool active = false, savedActive = false;
    GLuint progScene = 0, progBright = 0, progBlur = 0, progComp = 0;
    GLuint shTex = 0, shFbo = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    GLuint scnFbo = 0, scnTex = 0, brFbo = 0, brTex = 0, blFbo[2] = {0, 0}, blTex[2] = {0, 0};
    Mat4 lightVP = ident();
    std::string log, info;
};
inline State& S() { static State s; return s; }
inline bool ok() { return S().ok; }

// --------------------------------------------------------------- shaders ---
static const char* SCENE_VS = R"GLSL(
#version 120
uniform mat4 uInvView;
uniform mat4 uShadowMat;
varying vec3 vN;
varying vec3 vPosE;
varying vec3 vWorld;
varying vec4 vShadow;
varying vec4 vColor;
varying vec2 vUV;
void main() {
    vec4 pe = gl_ModelViewMatrix * gl_Vertex;
    vPosE = pe.xyz;
    vN = gl_NormalMatrix * gl_Normal;
    vWorld = (uInvView * pe).xyz;
    vShadow = uShadowMat * pe;
    vColor = gl_Color;
    vUV = gl_MultiTexCoord0.xy;
    gl_Position = gl_ProjectionMatrix * pe;
}
)GLSL";

static const char* SCENE_FS = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform sampler2DShadow uShadow;
uniform mat4 uInvView;
uniform vec3 uSunE;
uniform vec3 uSunW;
uniform vec3 uSunCol;
uniform vec3 uSkyCol;
uniform vec3 uGroundCol;
uniform vec3 uHorizon;
uniform vec3 uZenith;
uniform vec3 uFogCol;
uniform float uFogDen;
uniform float uNight;
uniform float uExposure;
uniform float uShTexel;
varying vec3 vN;
varying vec3 vPosE;
varying vec3 vWorld;
varying vec4 vShadow;
varying vec4 vColor;
varying vec2 vUV;

vec3 toLin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 aces(vec3 x) {
    const float a = 2.51; const float b = 0.03; const float c = 2.43; const float d = 0.59; const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 skyEnv(vec3 d) {
    float h = d.y;
    vec3 sky = mix(toLin(uHorizon), toLin(uZenith), pow(clamp(h, 0.0, 1.0), 0.5));
    vec3 gnd = uGroundCol * 0.7;
    vec3 c = mix(gnd, sky, smoothstep(-0.10, 0.06, h));
    float s = max(dot(d, uSunW), 0.0);
    c += uSunCol * (pow(s, 700.0) * 5.0 + pow(s, 28.0) * 0.12);
    return c;
}
float shadowTerm(vec4 sc) {
    vec3 p = sc.xyz / sc.w;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float bias = 0.0012;
    float s = 0.0;
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
            s += shadow2D(uShadow, vec3(p.xy + vec2(float(i), float(j)) * uShTexel * 1.3, p.z - bias)).r;
    s /= 9.0;
    vec2 e = abs(p.xy - 0.5) * 2.0;
    float f = smoothstep(0.82, 1.0, max(e.x, e.y));
    return mix(s, 1.0, f);
}
void main() {
    vec4 t = texture2D(uTex, vUV);
    vec3 albedo = toLin(vColor.rgb * t.rgb);
    float refl = clamp(1.0 - vColor.a, 0.0, 1.0);   // vertex alpha < 1 marks glossy / reflective surfaces
    vec3 N = normalize(vN);
    vec3 V = normalize(-vPosE);
    vec3 L = normalize(uSunE);
    float ndl = max(dot(N, L), 0.0);
    float sh = (ndl > 0.001) ? shadowTerm(vShadow) : 1.0;
    vec3 Nw = normalize(mat3(uInvView) * N);
    vec3 amb = mix(uGroundCol, uSkyCol, Nw.y * 0.5 + 0.5);
    float ao = mix(0.55, 1.0, smoothstep(0.0, 10.0, vWorld.y));
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 R = normalize(mat3(uInvView) * reflect(-V, N));
    vec3 env = skyEnv(R);
    float gloss = mix(12.0, 220.0, refl);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), gloss) * (gloss + 8.0) / 25.0 * (0.05 + 0.95 * refl);
    vec3 diff = albedo * (uSunCol * ndl * sh + amb * ao);
    float rk = clamp(refl * (0.25 + fres * 1.4), 0.0, 0.92);
    vec3 col = mix(diff, env * mix(vec3(1.0), albedo * 2.0 + 0.2, 0.25), rk);
    col += uSunCol * spec * sh * ndl;
    col += albedo * t.a * uNight * 1.7;                 // lit windows
    col *= uExposure;
    col = pow(aces(col), vec3(1.0 / 2.2));
    float d = length(vPosE);
    float f = 1.0 - exp(-pow(d * uFogDen, 1.6));
    col = mix(col, uFogCol, clamp(f, 0.0, 1.0));
    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

static const char* QUAD_VS = R"GLSL(
#version 120
varying vec2 vUV;
void main() {
    vUV = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

static const char* BRIGHT_FS = R"GLSL(
#version 120
uniform sampler2D uTex;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(uTex, vUV).rgb;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float k = smoothstep(0.78, 1.15, l);
    gl_FragColor = vec4(c * k, 1.0);
}
)GLSL";

static const char* BLUR_FS = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform vec2 uDir;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(uTex, vUV).rgb * 0.227027;
    c += texture2D(uTex, vUV + uDir * 1.3846154).rgb * 0.3162162;
    c += texture2D(uTex, vUV - uDir * 1.3846154).rgb * 0.3162162;
    c += texture2D(uTex, vUV + uDir * 3.2307692).rgb * 0.0702703;
    c += texture2D(uTex, vUV - uDir * 3.2307692).rgb * 0.0702703;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

static const char* COMP_FS = R"GLSL(
#version 120
uniform sampler2D uTex;
uniform sampler2D uBloom;
uniform float uBloomAmt;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(uTex, vUV).rgb;
    c += texture2D(uBloom, vUV).rgb * uBloomAmt;
    vec2 q = vUV - 0.5;
    c *= 1.0 - 0.55 * dot(q, q);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.10);
    c = (c - 0.5) * 1.05 + 0.5;
    c += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

// ------------------------------------------------------------- GL helpers ---
inline GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = gxCreateShader(type);
    gxShaderSource(s, 1, &src, nullptr);
    gxCompileShader(s);
    GLint okc = 0;
    gxGetShaderiv(s, GL_COMPILE_STATUS, &okc);
    if (!okc) {
        char buf[4096];
        buf[0] = 0;
        gxGetShaderInfoLog(s, 4096, nullptr, buf);
        S().log += std::string("[") + name + "] shader compile error:\n" + buf + "\n";
        return 0;
    }
    return s;
}
inline GLuint program(const char* vs, const char* fs, const char* name) {
    GLuint v = compile(GL_VERTEX_SHADER, vs, name), f = compile(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) return 0;
    GLuint p = gxCreateProgram();
    gxAttachShader(p, v);
    gxAttachShader(p, f);
    gxLinkProgram(p);
    GLint okl = 0;
    gxGetProgramiv(p, GL_LINK_STATUS, &okl);
    if (!okl) {
        char buf[4096];
        buf[0] = 0;
        gxGetProgramInfoLog(p, 4096, nullptr, buf);
        S().log += std::string("[") + name + "] link error:\n" + buf + "\n";
        return 0;
    }
    return p;
}
inline GLint U(GLuint prog, const char* n) { return gxGetUniformLocation(prog, n); }

inline GLuint makeColorTex(int w, int h) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
inline bool makeTexFbo(GLuint& fbo, GLuint& tex, int w, int h) {
    tex = makeColorTex(w, h);
    gxGenFramebuffers(1, &fbo);
    gxBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gxFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return gxCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

inline void freeTargets() {
    State& s = S();
    GLuint f[5] = {s.msFbo, s.scnFbo, s.brFbo, s.blFbo[0], s.blFbo[1]};
    GLuint t[4] = {s.scnTex, s.brTex, s.blTex[0], s.blTex[1]};
    GLuint r[2] = {s.msColor, s.msDepth};
    if (gxDeleteFramebuffers) for (int i = 0; i < 5; i++) if (f[i]) gxDeleteFramebuffers(1, &f[i]);
    for (int i = 0; i < 4; i++) if (t[i]) glDeleteTextures(1, &t[i]);
    if (gxDeleteRenderbuffers) for (int i = 0; i < 2; i++) if (r[i]) gxDeleteRenderbuffers(1, &r[i]);
    s.msFbo = s.scnFbo = s.brFbo = s.blFbo[0] = s.blFbo[1] = 0;
    s.scnTex = s.brTex = s.blTex[0] = s.blTex[1] = 0;
    s.msColor = s.msDepth = 0;
}

inline bool buildTargets(int w, int h) {
    State& s = S();
    freeTargets();
    s.w = w; s.h = h;
    bool good = false;
    const int tryS[3] = {4, 2, 0};
    for (int a = 0; a < 3 && !good; a++) {
        int smp = tryS[a];
        gxGenRenderbuffers(1, &s.msColor);
        gxBindRenderbuffer(GL_RENDERBUFFER, s.msColor);
        if (smp > 0) gxRenderbufferStorageMultisample(GL_RENDERBUFFER, smp, GL_RGBA8, w, h);
        else gxRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        gxGenRenderbuffers(1, &s.msDepth);
        gxBindRenderbuffer(GL_RENDERBUFFER, s.msDepth);
        if (smp > 0) gxRenderbufferStorageMultisample(GL_RENDERBUFFER, smp, GL_DEPTH24_STENCIL8, w, h);
        else gxRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        gxGenFramebuffers(1, &s.msFbo);
        gxBindFramebuffer(GL_FRAMEBUFFER, s.msFbo);
        gxFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s.msColor);
        gxFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s.msDepth);
        if (gxCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) { good = true; s.samples = smp; }
        else {
            gxBindFramebuffer(GL_FRAMEBUFFER, 0);
            gxDeleteFramebuffers(1, &s.msFbo); gxDeleteRenderbuffers(1, &s.msColor); gxDeleteRenderbuffers(1, &s.msDepth);
            s.msFbo = s.msColor = s.msDepth = 0;
        }
    }
    if (!good) { s.log += "Could not create the main render target.\n"; return false; }
    if (!makeTexFbo(s.scnFbo, s.scnTex, w, h) || !makeTexFbo(s.brFbo, s.brTex, w / 2, h / 2) ||
        !makeTexFbo(s.blFbo[0], s.blTex[0], w / 4, h / 4) || !makeTexFbo(s.blFbo[1], s.blTex[1], w / 4, h / 4)) {
        s.log += "Could not create post-processing targets.\n";
        return false;
    }
    gxBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

inline bool init() {
    State& s = S();
    s.ok = false;
    s.log.clear();
    if (!gxCreateShader || !gxGenFramebuffers || !gxBlitFramebuffer || !gxRenderbufferStorageMultisample || !gxActiveTexture) {
        s.log += "This graphics driver does not provide shaders / framebuffers (needs OpenGL 3.0+).\n";
        return false;
    }
    const unsigned char* ver = glGetString(GL_VERSION);
    const unsigned char* ren = glGetString(GL_RENDERER);
    s.info = std::string("GL ") + (ver ? (const char*)ver : "?") + " | " + (ren ? (const char*)ren : "?");
    s.progScene = program(SCENE_VS, SCENE_FS, "scene");
    s.progBright = program(QUAD_VS, BRIGHT_FS, "bright");
    s.progBlur = program(QUAD_VS, BLUR_FS, "blur");
    s.progComp = program(QUAD_VS, COMP_FS, "composite");
    if (!s.progScene || !s.progBright || !s.progBlur || !s.progComp) return false;
    // shadow map
    glGenTextures(1, &s.shTex);
    glBindTexture(GL_TEXTURE_2D, s.shTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, s.shSize, s.shSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat border[4] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    gxGenFramebuffers(1, &s.shFbo);
    gxBindFramebuffer(GL_FRAMEBUFFER, s.shFbo);
    gxFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s.shTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    bool shOk = gxCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gxBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!shOk) { s.log += "Shadow map framebuffer is not supported.\n"; return false; }
    if (!buildTargets(plat().w, plat().h)) return false;
    s.ok = true;
    return true;
}

// ------------------------------------------------------------ frame steps ---
// 1) render the sun's shadow map. draw() must issue the geometry that casts shadows.
inline void shadowPass(const Params& P, const std::function<void()>& draw) {
    State& s = S();
    if (!s.ok) return;
    float c[3] = {P.focus[0], P.focus[1], P.focus[2]};
    float ex = c[0] + P.sunDir[0] * 250.f, ey = c[1] + P.sunDir[1] * 250.f, ez = c[2] + P.sunDir[2] * 250.f;
    Mat4 lv = lookAt(ex, ey, ez, c[0], c[1], c[2]);
    const float R = 105.f;
    Mat4 lp = ortho(-R, R, -R, R, 20.f, 600.f);
    float lx = lv.m[0] * c[0] + lv.m[4] * c[1] + lv.m[8] * c[2] + lv.m[12];
    float ly = lv.m[1] * c[0] + lv.m[5] * c[1] + lv.m[9] * c[2] + lv.m[13];
    float texel = 2.f * R / (float)s.shSize;
    lv.m[12] -= fmodf(lx, texel);
    lv.m[13] -= fmodf(ly, texel);
    s.lightVP = mul(lp, lv);
    gxBindFramebuffer(GL_FRAMEBUFFER, s.shFbo);
    glViewport(0, 0, s.shSize, s.shSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND); glDisable(GL_FOG); glDisable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.f, 4.f);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(lp.m);
    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(lv.m);
    draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gxBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 2) bind the multisampled target the whole scene is drawn into
inline void beginScene(int w, int h) {
    State& s = S();
    if (!s.ok) return;
    if (w != s.w || h != s.h) {
        if (!buildTargets(w, h)) { s.ok = false; return; }
    }
    gxBindFramebuffer(GL_FRAMEBUFFER, s.msFbo);
    glViewport(0, 0, w, h);
}

// 3) activate the lit-surface shader (call once the camera matrix is loaded)
inline void bindScene(const Params& P) {
    State& s = S();
    if (!s.ok) return;
    GLuint p = s.progScene;
    gxUseProgram(p);
    Mat4 inv = invRigid(P.view);
    Mat4 bias = ident();
    bias.m[0] = 0.5f; bias.m[5] = 0.5f; bias.m[10] = 0.5f; bias.m[12] = 0.5f; bias.m[13] = 0.5f; bias.m[14] = 0.5f;
    Mat4 shm = mul(bias, mul(s.lightVP, inv));
    gxUniformMatrix4fv(U(p, "uInvView"), 1, GL_FALSE, inv.m);
    gxUniformMatrix4fv(U(p, "uShadowMat"), 1, GL_FALSE, shm.m);
    const float* v = P.view.m;
    const float* d = P.sunDir;
    gxUniform3f(U(p, "uSunE"), v[0] * d[0] + v[4] * d[1] + v[8] * d[2], v[1] * d[0] + v[5] * d[1] + v[9] * d[2], v[2] * d[0] + v[6] * d[1] + v[10] * d[2]);
    gxUniform3f(U(p, "uSunW"), d[0], d[1], d[2]);
    gxUniform3f(U(p, "uSunCol"), P.sunCol[0], P.sunCol[1], P.sunCol[2]);
    gxUniform3f(U(p, "uSkyCol"), P.skyCol[0], P.skyCol[1], P.skyCol[2]);
    gxUniform3f(U(p, "uGroundCol"), P.groundCol[0], P.groundCol[1], P.groundCol[2]);
    gxUniform3f(U(p, "uHorizon"), P.horizon[0], P.horizon[1], P.horizon[2]);
    gxUniform3f(U(p, "uZenith"), P.zenith[0], P.zenith[1], P.zenith[2]);
    gxUniform3f(U(p, "uFogCol"), P.fogCol[0], P.fogCol[1], P.fogCol[2]);
    gxUniform1f(U(p, "uFogDen"), P.fogDen);
    gxUniform1f(U(p, "uNight"), P.night);
    gxUniform1f(U(p, "uExposure"), P.exposure);
    gxUniform1f(U(p, "uShTexel"), 1.f / (float)s.shSize);
    gxUniform1i(U(p, "uTex"), 0);
    gxUniform1i(U(p, "uShadow"), 1);
    gxActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s.shTex);
    gxActiveTexture(GL_TEXTURE0);
    s.active = true;
}
inline void unbindScene() { if (S().ok) { gxUseProgram(0); S().active = false; } }
// emissive parts are drawn flat: pause the lit shader around them (no-op outside the main scene pass)
inline void unlitBegin() { State& s = S(); s.savedActive = s.ok && s.active; if (s.savedActive) gxUseProgram(0); }
inline void unlitEnd() { State& s = S(); if (s.savedActive) gxUseProgram(s.progScene); }

inline void fullscreenQuad() {
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
}

// 4) resolve MSAA, bloom, colour-grade and write the final picture to the window
inline void endScene(int w, int h, float bloomAmt) {
    State& s = S();
    if (!s.ok) return;
    gxUseProgram(0);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_STENCIL_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    // resolve
    gxBindFramebuffer(GL_READ_FRAMEBUFFER, s.msFbo);
    gxBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.scnFbo);
    gxBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glColor4f(1, 1, 1, 1);
    // bright pass (half size)
    gxBindFramebuffer(GL_FRAMEBUFFER, s.brFbo);
    glViewport(0, 0, w / 2, h / 2);
    gxUseProgram(s.progBright);
    gxUniform1i(U(s.progBright, "uTex"), 0);
    glBindTexture(GL_TEXTURE_2D, s.scnTex);
    fullscreenQuad();
    // blur (quarter size, 2 iterations)
    gxUseProgram(s.progBlur);
    gxUniform1i(U(s.progBlur, "uTex"), 0);
    GLint uDir = U(s.progBlur, "uDir");
    int qw = w / 4, qh = h / 4;
    glViewport(0, 0, qw, qh);
    GLuint src = s.brTex;
    for (int it = 0; it < 2; it++) {
        gxBindFramebuffer(GL_FRAMEBUFFER, s.blFbo[0]);
        glBindTexture(GL_TEXTURE_2D, src);
        gxUniform2f(uDir, 1.f / (float)qw, 0.f);
        fullscreenQuad();
        gxBindFramebuffer(GL_FRAMEBUFFER, s.blFbo[1]);
        glBindTexture(GL_TEXTURE_2D, s.blTex[0]);
        gxUniform2f(uDir, 0.f, 1.f / (float)qh);
        fullscreenQuad();
        src = s.blTex[1];
    }
    // composite to the window
    gxBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    gxUseProgram(s.progComp);
    gxUniform1i(U(s.progComp, "uTex"), 0);
    gxUniform1i(U(s.progComp, "uBloom"), 1);
    gxUniform1f(U(s.progComp, "uBloomAmt"), bloomAmt);
    gxActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s.blTex[1]);
    gxActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.scnTex);
    fullscreenQuad();
    gxUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// writes the reason a fallback happened so it can be sent to the developer
inline void writeLog(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "Sample graphics log\n%s\n\n%s\n", S().info.c_str(), S().log.empty() ? "(no errors)" : S().log.c_str());
    fclose(f);
}

}  // namespace rt
}  // namespace lite
