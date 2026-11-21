// ============================================================================
//  LiteEngine gfx - higher-quality rendering helpers (still OpenGL 1.x, no shaders)
//  sky dome + sun/moon/stars, stencil shadows, procedural textures, shaped car
//  bodies (3 types) with glass/rims/lights, detailed people, trees, street lamps
//  and additive glow. Optional real .obj car model support (assets/car.obj).
// ============================================================================
#pragma once
#include "engine.h"
#include "model.h"
#include <algorithm>
#include <vector>

namespace lite {
namespace gfx {

inline Model& carModel() { static Model m; return m; }

// ------------------------------------------------------------ primitives --
inline void sphere(float r, int slices, int stacks) {
    for (int i = 0; i < stacks; i++) {
        float a0 = PI * (float)i / stacks - PI / 2, a1 = PI * (float)(i + 1) / stacks - PI / 2;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            float b = 2 * PI * (float)j / slices;
            float x0 = cosf(a0) * cosf(b), y0 = sinf(a0), z0 = cosf(a0) * sinf(b);
            float x1 = cosf(a1) * cosf(b), y1 = sinf(a1), z1 = cosf(a1) * sinf(b);
            glNormal3f(x0, y0, z0); glVertex3f(r * x0, r * y0, r * z0);
            glNormal3f(x1, y1, z1); glVertex3f(r * x1, r * y1, r * z1);
        }
        glEnd();
    }
}
// cylinder along the X axis, centred (wheels)
inline void cylX(float r, float len, int seg) {
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= seg; i++) {
        float a = 2 * PI * i / seg, y = cosf(a), z = sinf(a);
        glNormal3f(0, y, z); glVertex3f(-len / 2, r * y, r * z); glVertex3f(len / 2, r * y, r * z);
    }
    glEnd();
    for (int s = -1; s <= 1; s += 2) {
        glBegin(GL_TRIANGLE_FAN);
        glNormal3f((float)s, 0, 0);
        glVertex3f(s * len / 2, 0, 0);
        for (int i = 0; i <= seg; i++) {
            float a = 2 * PI * (s > 0 ? i : seg - i) / seg;
            glVertex3f(s * len / 2, r * cosf(a), r * sinf(a));
        }
        glEnd();
    }
}
// vertical cylinder from y=0 to y=h
inline void cylY(float r, float h, int seg) {
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= seg; i++) {
        float a = 2 * PI * i / seg, x = cosf(a), z = sinf(a);
        glNormal3f(x, 0, z); glVertex3f(r * x, 0, r * z); glVertex3f(r * x, h, r * z);
    }
    glEnd();
}

// -------------------------------------------------------------- textures --
inline GLuint uploadTex(int w, int h, const unsigned char* px) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, 4, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return t;
}
inline unsigned char clampb(float v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// kind: 0 asphalt, 1 sidewalk tiles, 2 grass
inline GLuint makeGroundTexture(int kind) {
    const int S = 64;
    static unsigned char px[S * S * 4];
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            unsigned char* o = &px[(y * S + x) * 4];
            float r, g, b;
            if (kind == 0) {
                float v = 40 + frand() * 12 + (frand() < 0.02f ? 28 : 0);
                r = v; g = v; b = v + 2;
            } else if (kind == 1) {
                float v = 150 + frand() * 14;
                if (x % 16 == 0 || y % 16 == 0) v -= 38;
                r = v; g = v; b = v - 4;
            } else {
                float v = frand();
                r = 52 + v * 26; g = 100 + v * 34; b = 40 + v * 16;
            }
            o[0] = clampb(r); o[1] = clampb(g); o[2] = clampb(b); o[3] = 255;
        }
    return uploadTex(S, S, px);
}

// style: 0 glass office, 1 apartment (brick), 2 modern bands
inline GLuint makeBuildingTexture(int style) {
    const int S = 64;
    static unsigned char px[S * S * 4];
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            unsigned char* o = &px[(y * S + x) * 4];
            float r = 200, g = 200, b = 205;
            if (style == 0) {  // 8x8 grid of glass panes
                int cx = x / 8, cy = y / 8, lx = x % 8, ly = y % 8;
                bool win = lx >= 1 && lx <= 6 && ly >= 1 && ly <= 6;
                if (win) {
                    unsigned h = (unsigned)(cx * 73856093u ^ cy * 19349663u);
                    bool lit = (h % 100) < 22;
                    if (lit) { r = 255; g = 220; b = 130; }
                    else { float s = 0.7f + (h % 40) / 100.f; r = 60 * s; g = 110 * s; b = 160 * s; }
                    if (ly == 1) { r += 25; g += 25; b += 25; }  // glass glint
                } else { r = 150; g = 152; b = 158; }
            } else if (style == 1) {  // brick + small windows (4 across, 4 floors)
                int cx = x / 16, cy = y / 16, lx = x % 16, ly = y % 16;
                bool win = lx >= 4 && lx <= 11 && ly >= 4 && ly <= 12;
                int row = y / 4;
                bool mortar = (y % 4 == 0) || (x + (row % 2) * 4) % 8 == 0;
                float n = frand() * 18;
                r = mortar ? 150 : 175 + n; g = mortar ? 140 : 100 + n * 0.5f; b = mortar ? 130 : 85;
                if (win) {
                    unsigned h = (unsigned)(cx * 83492791u ^ cy * 297657u);
                    bool lit = (h % 100) < 38;
                    if (lit) { r = 255; g = 205; b = 110; } else { r = 38; g = 48; b = 66; }
                    if (lx == 4 || lx == 11 || ly == 4 || ly == 12) { r = 215; g = 215; b = 215; }  // frame
                }
            } else {  // modern: horizontal glass bands over concrete floors
                int cy = y / 16, ly = y % 16, cx = x / 16, lx = x % 16;
                if (ly < 4) { r = 205; g = 205; b = 200; }
                else {
                    unsigned h = (unsigned)(cx * 2654435761u ^ cy * 40503u);
                    bool lit = (h % 100) < 28;
                    if (lit) { r = 250; g = 225; b = 150; }
                    else { r = 35; g = 70; b = 105; }
                    if (lx == 0) { r = 170; g = 170; b = 175; }  // mullion
                    if (ly == 4) { r += 30; g += 30; b += 30; }
                }
            }
            o[0] = clampb(r); o[1] = clampb(g); o[2] = clampb(b); o[3] = 255;
        }
    return uploadTex(S, S, px);
}

// ------------------------------------------------------------ car bodies --
struct Pt { float z, y, hw; };
static const Pt SHAPES[3][8] = {
    // 0 sedan
    {{-2.25f, 0.45f, 0.92f}, {-2.25f, 0.95f, 0.92f}, {-1.55f, 1.00f, 0.90f}, {-1.00f, 1.42f, 0.76f}, {0.35f, 1.45f, 0.76f}, {1.00f, 1.00f, 0.90f}, {2.20f, 0.88f, 0.90f}, {2.25f, 0.55f, 0.88f}},
    // 1 sports coupe
    {{-2.30f, 0.45f, 0.98f}, {-2.30f, 0.82f, 0.98f}, {-1.30f, 0.92f, 0.95f}, {-0.75f, 1.22f, 0.78f}, {0.35f, 1.24f, 0.78f}, {1.00f, 0.86f, 0.95f}, {2.30f, 0.68f, 0.95f}, {2.35f, 0.50f, 0.92f}},
    // 2 SUV
    {{-2.30f, 0.50f, 0.98f}, {-2.30f, 1.50f, 0.98f}, {-2.00f, 1.65f, 0.94f}, {-1.75f, 1.85f, 0.86f}, {0.70f, 1.88f, 0.86f}, {1.25f, 1.25f, 0.94f}, {2.30f, 0.98f, 0.94f}, {2.35f, 0.55f, 0.92f}}};

struct CarLook {
    int type = 0;
    float r = 1, g = 1, b = 1;
    bool police = false, dead = false, lights = false, siren = false;
    float t = 0;
};

inline void drawCar(const CarLook& c) {
    const Pt* p = SHAPES[c.type % 3];
    float br = c.r, bg = c.g, bb = c.b;
    if (c.police) { br = 0.06f; bg = 0.06f; bb = 0.09f; }
    if (c.dead) { br = 0.07f; bg = 0.06f; bb = 0.06f; }
    GLfloat spec[4] = {0.55f, 0.55f, 0.6f, 1.f}, nospec[4] = {0, 0, 0, 1};
    const float BOT = 0.32f;
    if (carModel().ok && !c.dead && c.type == 0) {  // optional real model
        glColor3f(br, bg, bb);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40.f);
        carModel().draw();
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, nospec);
    } else {
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.f);
        for (int i = 0; i < 7; i++) {
            bool glass = (i == 2 || i == 4) && !c.dead;
            float dz = p[i + 1].z - p[i].z, dy = p[i + 1].y - p[i].y, l = sqrtf(dz * dz + dy * dy);
            float ny = dz / l, nz = -dy / l;
            if (glass) glColor3f(0.08f, 0.12f, 0.18f); else glColor3f(br, bg, bb);
            glBegin(GL_QUADS);
            glNormal3f(0, ny, nz);
            glVertex3f(-p[i].hw, p[i].y, p[i].z); glVertex3f(p[i].hw, p[i].y, p[i].z);
            glVertex3f(p[i + 1].hw, p[i + 1].y, p[i + 1].z); glVertex3f(-p[i + 1].hw, p[i + 1].y, p[i + 1].z);
            glEnd();
            if (glass) glColor3f(br, bg, bb);
            for (int s = -1; s <= 1; s += 2) {
                glBegin(GL_QUADS);
                glNormal3f((float)s, 0.15f, 0);
                glVertex3f(s * p[i].hw, p[i].y, p[i].z); glVertex3f(s * p[i + 1].hw, p[i + 1].y, p[i + 1].z);
                glVertex3f(s * p[i + 1].hw, BOT, p[i + 1].z); glVertex3f(s * p[i].hw, BOT, p[i].z);
                glEnd();
            }
        }
        // side windows (with a B-pillar)
        if (!c.dead) {
            float belt = p[5].y + 0.02f, top = p[3].y - 0.05f;
            float zb0 = p[2].z + 0.12f, zb1 = p[5].z - 0.08f, zt0 = p[3].z + 0.12f, zt1 = p[4].z - 0.12f;
            auto xAt = [&](float y) { return lerpf(p[5].hw, p[3].hw, clampf((y - belt) / (top + 0.05f - belt), 0.f, 1.f)) + 0.012f; };
            for (int s = -1; s <= 1; s += 2) {
                glColor3f(0.08f, 0.12f, 0.18f);
                glBegin(GL_QUADS);
                glNormal3f((float)s, 0.3f, 0);
                glVertex3f(s * xAt(belt), belt, zb0); glVertex3f(s * xAt(belt), belt, zb1);
                glVertex3f(s * xAt(top), top, zt1); glVertex3f(s * xAt(top), top, zt0);
                glEnd();
                float zm = (zb0 + zb1) * 0.5f, zmt = (zt0 + zt1) * 0.5f;
                glColor3f(br, bg, bb);
                glBegin(GL_QUADS);
                glVertex3f(s * (xAt(belt) + 0.004f), belt, zm - 0.05f); glVertex3f(s * (xAt(belt) + 0.004f), belt, zm + 0.05f);
                glVertex3f(s * (xAt(top) + 0.004f), top, zmt + 0.05f); glVertex3f(s * (xAt(top) + 0.004f), top, zmt - 0.05f);
                glEnd();
            }
        }
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, nospec);
        // bumpers, grille, plates
        glColor3f(0.12f, 0.12f, 0.13f);
        box(0, 0.45f, p[7].z + 0.03f, 1.9f, 0.2f, 0.12f);
        box(0, 0.45f, p[0].z - 0.03f, 1.9f, 0.2f, 0.12f);
        if (!c.dead) {
            glColor3f(0.02f, 0.02f, 0.02f);
            box(0, 0.68f, p[6].z + 0.02f, 0.9f, 0.14f, 0.05f);
            glColor3f(0.92f, 0.92f, 0.85f);
            box(0, 0.44f, p[7].z + 0.1f, 0.5f, 0.12f, 0.02f);
            box(0, 0.5f, p[0].z - 0.1f, 0.5f, 0.12f, 0.02f);
            glColor3f(br, bg, bb);  // mirrors
            for (int s = -1; s <= 1; s += 2) box(s * (p[5].hw + 0.1f), p[5].y + 0.12f, p[5].z - 0.35f, 0.14f, 0.1f, 0.2f);
        }
        if (c.police && !c.dead) {
            glColor3f(0.94f, 0.94f, 0.94f);
            for (int s = -1; s <= 1; s += 2) {
                glBegin(GL_QUADS);
                glNormal3f((float)s, 0, 0);
                glVertex3f(s * 0.935f, 0.52f, -0.9f); glVertex3f(s * 0.935f, 0.52f, 0.9f);
                glVertex3f(s * 0.935f, 0.93f, 0.9f); glVertex3f(s * 0.935f, 0.93f, -0.9f);
                glEnd();
            }
        }
    }
    // wheels
    float wr = (c.type == 2) ? 0.44f : 0.38f, wz = (c.type == 1) ? 1.45f : 1.4f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            glPushMatrix();
            glTranslatef(sx * (p[2].hw - 0.04f), wr, sz * wz);
            glColor3f(0.04f, 0.04f, 0.04f);
            cylX(wr, 0.3f, 14);
            glColor3f(c.dead ? 0.2f : 0.72f, c.dead ? 0.2f : 0.72f, c.dead ? 0.2f : 0.76f);
            glTranslatef(sx * 0.02f, 0, 0);
            cylX(wr * 0.58f, 0.31f, 10);
            glPopMatrix();
        }
    if (c.dead) return;
    // emissive lights (unlit)
    glDisable(GL_LIGHTING);
    float fz = p[6].z + 0.01f, rz = p[1].z - 0.01f;
    glColor3f(c.lights ? 1.f : 0.8f, c.lights ? 0.97f : 0.8f, c.lights ? 0.72f : 0.65f);
    for (int s = -1; s <= 1; s += 2) box(s * 0.68f, p[6].y - 0.1f, fz, 0.42f, 0.2f, 0.06f);
    glColor3f(c.lights ? 1.f : 0.75f, 0.04f, 0.04f);
    for (int s = -1; s <= 1; s += 2) box(s * 0.7f, p[1].y - 0.12f, rz, 0.4f, 0.16f, 0.06f);
    if (c.police && c.siren) {
        bool a = sinf(c.t * 14.f) > 0;
        float ry = p[3].y + 0.1f;
        glColor3f(a ? 1.f : 0.25f, 0.08f, 0.08f); box(-0.4f, ry, -0.35f, 0.62f, 0.16f, 0.36f);
        glColor3f(0.08f, 0.2f, a ? 0.25f : 1.f); box(0.4f, ry, -0.35f, 0.62f, 0.16f, 0.36f);
    }
    glEnable(GL_LIGHTING);
}

// ----------------------------------------------------------------- people --
struct PersonLook {
    float shirt[3] = {0.2f, 0.4f, 0.8f}, pants[3] = {0.12f, 0.12f, 0.2f}, hair[3] = {0.1f, 0.07f, 0.05f};
    int skin = 0;
    bool armed = false, jacket = false;
    float phase = 0, moving = 1.f;
};
inline void drawPerson(const PersonLook& p) {
    static const float SK[4][3] = {{0.92f, 0.74f, 0.6f}, {0.78f, 0.56f, 0.4f}, {0.55f, 0.38f, 0.27f}, {0.38f, 0.26f, 0.2f}};
    const float* sk = SK[p.skin & 3];
    float sw = sinf(p.phase) * 0.55f * p.moving;
    for (int s = -1; s <= 1; s += 2) {  // legs + shoes
        glPushMatrix();
        glTranslatef(s * 0.16f, 0.85f, 0);
        glRotatef(s * sw * 55.f, 1, 0, 0);
        glColor3fv(p.pants);
        box(0, -0.4f, 0, 0.23f, 0.8f, 0.24f);
        glColor3f(0.08f, 0.08f, 0.08f);
        box(0, -0.84f, 0.05f, 0.24f, 0.12f, 0.36f);
        glPopMatrix();
    }
    glColor3fv(p.shirt);
    box(0, 1.28f, 0, 0.58f, 0.66f, 0.3f);
    if (p.jacket) { glColor3f(p.shirt[0] * 0.6f, p.shirt[1] * 0.6f, p.shirt[2] * 0.6f); box(0, 1.28f, 0.005f, 0.6f, 0.2f, 0.31f); }
    for (int s = -1; s <= 1; s += 2) {  // arms
        glPushMatrix();
        glTranslatef(s * 0.4f, 1.55f, 0);
        glRotatef(-s * sw * 45.f - (p.armed && s > 0 ? 70.f : 0.f), 1, 0, 0);
        glColor3fv(p.shirt);
        box(0, -0.25f, 0, 0.14f, 0.5f, 0.16f);
        glColor3fv(sk);
        box(0, -0.55f, 0, 0.12f, 0.12f, 0.12f);
        glPopMatrix();
    }
    glColor3fv(sk);
    glPushMatrix(); glTranslatef(0, 1.8f, 0); gfx::sphere(0.19f, 8, 6); glPopMatrix();
    glColor3fv(sk);
    box(0, 1.62f, 0, 0.1f, 0.1f, 0.1f);  // neck
    glColor3fv(p.hair);
    glPushMatrix(); glTranslatef(0, 1.88f, -0.02f); glScalef(1.05f, 0.75f, 1.08f); gfx::sphere(0.2f, 8, 5); glPopMatrix();
    if (p.armed) { glColor3f(0.1f, 0.1f, 0.1f); box(0.4f, 1.3f, 0.4f, 0.09f, 0.13f, 0.4f); }
}

// ------------------------------------------------------- trees and lamps --
inline void drawTree(float x, float z, float s, float tone) {
    glPushMatrix();
    glTranslatef(x, 0.15f, z);
    glScalef(s, s, s);
    glColor3f(0.36f, 0.24f, 0.14f);
    cylY(0.2f, 2.3f, 6);
    glColor3f(0.12f * tone, 0.42f * tone, 0.14f * tone);
    glPushMatrix(); glTranslatef(0, 3.4f, 0); sphere(1.7f, 7, 5); glPopMatrix();
    glColor3f(0.16f * tone, 0.5f * tone, 0.16f * tone);
    glPushMatrix(); glTranslatef(0.2f, 4.6f, 0.1f); sphere(1.2f, 7, 4); glPopMatrix();
    glPopMatrix();
}
inline void drawLamp(float x, float z, float faceX, float faceZ) {  // arm points toward (faceX,faceZ)
    glPushMatrix();
    glTranslatef(x, 0.15f, z);
    glColor3f(0.2f, 0.2f, 0.22f);
    cylY(0.09f, 7.f, 6);
    float len = sqrtf(faceX * faceX + faceZ * faceZ);
    if (len > 0.001f) { faceX /= len; faceZ /= len; }
    box(faceX * 0.9f, 7.05f, faceZ * 0.9f, fabsf(faceX) * 1.8f + 0.12f, 0.12f, fabsf(faceZ) * 1.8f + 0.12f);
    glColor3f(0.9f, 0.9f, 0.75f);
    box(faceX * 1.8f, 6.95f, faceZ * 1.8f, 0.5f, 0.14f, 0.5f);
    glPopMatrix();
}

// -------------------------------------------------------------------- sky --
inline void drawSky(const float hz[3], float day, const float sunDir[3], float time) {
    glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE); glDisable(GL_DEPTH_TEST);
    float zen[3] = {lerpf(0.01f, 0.14f, day), lerpf(0.015f, 0.34f, day), lerpf(0.05f, 0.78f, day)};
    const int RINGS = 6, SEG = 24;
    const float R = 600.f;
    for (int i = 0; i < RINGS; i++) {
        float e0 = (PI / 2) * i / RINGS, e1 = (PI / 2) * (i + 1) / RINGS;
        float k0 = powf((float)i / RINGS, 0.65f), k1 = powf((float)(i + 1) / RINGS, 0.65f);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= SEG; j++) {
            float a = 2 * PI * j / SEG;
            glColor3f(lerpf(hz[0], zen[0], k0), lerpf(hz[1], zen[1], k0), lerpf(hz[2], zen[2], k0));
            glVertex3f(R * cosf(e0) * cosf(a), R * sinf(e0) - 20.f, R * cosf(e0) * sinf(a));
            glColor3f(lerpf(hz[0], zen[0], k1), lerpf(hz[1], zen[1], k1), lerpf(hz[2], zen[2], k1));
            glVertex3f(R * cosf(e1) * cosf(a), R * sinf(e1) - 20.f, R * cosf(e1) * sinf(a));
        }
        glEnd();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // stars
    static std::vector<float> stars;
    if (stars.empty()) {
        for (int i = 0; i < 260; i++) {
            float a = frand() * 2 * PI, e = frange(0.08f, 1.5f);
            stars.push_back(cosf(e) * cosf(a)); stars.push_back(sinf(e)); stars.push_back(cosf(e) * sinf(a));
        }
    }
    float sa = clampf(1.f - day * 2.2f, 0.f, 1.f);
    if (sa > 0.01f) {
        glPointSize(2.f);
        glBegin(GL_POINTS);
        for (size_t i = 0; i < stars.size(); i += 3) {
            float tw = 0.6f + 0.4f * sinf(time * 2.f + (float)i);
            glColor4f(1, 1, 1, sa * tw);
            glVertex3f(stars[i] * 580.f, stars[i + 1] * 580.f, stars[i + 2] * 580.f);
        }
        glEnd();
    }
    // sun / moon billboard
    float dx = sunDir[0], dy = sunDir[1], dz = sunDir[2];
    float l = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= l; dy /= l; dz /= l;
    float rx = dz, rz = -dx, rl = sqrtf(rx * rx + rz * rz);
    rx /= rl; rz /= rl;  // right = dir x up (flattened)
    float ux = dy * rz, uy = dz * rx - dx * rz, uz = -dy * rx;  // up = right x dir
    float ul = sqrtf(ux * ux + uy * uy + uz * uz);
    ux /= ul; uy /= ul; uz /= ul;
    auto disc = [&](float rad, float cr, float cg, float cb, float ca) {
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(cr, cg, cb, ca);
        glVertex3f(dx * 540.f, dy * 540.f, dz * 540.f);
        glColor4f(cr, cg, cb, 0.f);
        for (int i = 0; i <= 20; i++) {
            float a = 2 * PI * i / 20, c = cosf(a) * rad, s = sinf(a) * rad;
            glVertex3f(dx * 540.f + rx * c + ux * s, dy * 540.f + uy * s, dz * 540.f + rz * c + uz * s);
        }
        glEnd();
    };
    if (day > 0.15f) {
        disc(150.f, 1.f, 0.85f, 0.5f, 0.35f * day);
        disc(60.f, 1.f, 0.95f, 0.8f, 0.9f * day);
    } else {
        disc(70.f, 0.6f, 0.7f, 1.f, 0.25f * (1.f - day));
        disc(24.f, 0.9f, 0.93f, 1.f, 0.95f * (1.f - day));
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------- shadows --
struct ShadowState { float m[16]; };
inline ShadowState& shadowState() { static ShadowState s; return s; }

// L = direction TOWARD the light. h = height of the ground plane to project onto.
inline void shadowBegin(float lx, float ly, float lz, float h, float alpha) {
    float k = lx / ly, q = lz / ly;
    float* m = shadowState().m;
    float t[16] = {1, 0, 0, 0, -k, 0, -q, 0, 0, 0, 1, 0, k * h, h, q * h, 1};
    for (int i = 0; i < 16; i++) m[i] = t[i];
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_FOG);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glColor4f(0.f, 0.f, 0.f, alpha);
}
inline void shadowPush() { glPushMatrix(); glMultMatrixf(shadowState().m); }
inline void shadowPop() { glPopMatrix(); }
inline void shadowEnd() {
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_FOG);
}

// ------------------------------------------------------------------- glow --
inline void glowBegin(float size) {
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glEnable(GL_POINT_SMOOTH);
    glPointSize(size);
    glBegin(GL_POINTS);
}
inline void glowEnd() {
    glEnd();
    glDisable(GL_POINT_SMOOTH);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

}  // namespace gfx
}  // namespace lite
