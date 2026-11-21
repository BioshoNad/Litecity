// ============================================================================
//  LiteEngine model loader - Wavefront .OBJ (v, vn, f with triangles/quads/n-gons)
//  Usage:  Model m; loadOBJ("assets/car.obj", m, 1.0f);  ...  m.draw();
//  The model is compiled into an OpenGL display list. It is auto-fitted so the
//  car's length is `targetLen` (z axis) and it sits on the ground at the origin.
// ============================================================================
#pragma once
#include "engine.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace lite {

struct Model {
    GLuint list = 0;
    bool ok = false;
    int triangles = 0;
    void draw() const { if (ok) glCallList(list); }
};

inline bool loadOBJ(const char* path, Model& out, float targetLen = 4.4f) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    std::vector<float> v, vn;
    struct Idx { int v, n; };
    std::vector<std::vector<Idx>> faces;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) { v.push_back(x); v.push_back(y); v.push_back(z); }
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z;
            if (sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3) { vn.push_back(x); vn.push_back(y); vn.push_back(z); }
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::vector<Idx> face;
            char* p = line + 2;
            while (*p) {
                while (*p == ' ' || *p == '\t') p++;
                if (*p == '\n' || *p == '\r' || !*p) break;
                int a = 0, b = 0, c = 0;
                int vi = 0, ni = 0;
                if (sscanf(p, "%d/%d/%d", &a, &b, &c) == 3) { vi = a; ni = c; }
                else if (sscanf(p, "%d//%d", &a, &c) == 2) { vi = a; ni = c; }
                else if (sscanf(p, "%d/%d", &a, &b) == 2) { vi = a; }
                else if (sscanf(p, "%d", &a) == 1) { vi = a; }
                else break;
                int nv = (int)(v.size() / 3), nn = (int)(vn.size() / 3);
                if (vi < 0) vi = nv + vi + 1;
                if (ni < 0) ni = nn + ni + 1;
                face.push_back({vi, ni});
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
            }
            if (face.size() >= 3) faces.push_back(face);
        }
    }
    fclose(f);
    if (v.size() < 9 || faces.empty()) return false;
    // bounds -> fit
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < v.size(); i += 3)
        for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], v[i + k]); mx[k] = std::max(mx[k], v[i + k]); }
    float len = mx[2] - mn[2];
    if (len < 1e-4f) len = std::max(mx[0] - mn[0], mx[1] - mn[1]);
    float sc = len > 1e-4f ? targetLen / len : 1.f;
    float cx = (mn[0] + mx[0]) * 0.5f, cz = (mn[2] + mx[2]) * 0.5f, by = mn[1];
    out.list = glGenLists(1);
    glNewList(out.list, GL_COMPILE);
    glBegin(GL_TRIANGLES);
    out.triangles = 0;
    int nv = (int)(v.size() / 3), nn = (int)(vn.size() / 3);
    auto P = [&](int i, float* o) {  // OBJ is 1-based
        i -= 1;
        if (i < 0 || i >= nv) { o[0] = o[1] = o[2] = 0; return; }
        o[0] = (v[i * 3] - cx) * sc; o[1] = (v[i * 3 + 1] - by) * sc; o[2] = (v[i * 3 + 2] - cz) * sc;
    };
    for (const auto& face : faces) {
        for (size_t t = 1; t + 1 < face.size(); t++) {
            const Idx tri[3] = {face[0], face[t], face[t + 1]};
            float p[3][3];
            for (int k = 0; k < 3; k++) P(tri[k].v, p[k]);
            float e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
            float e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
            float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            for (int k = 0; k < 3; k++) {
                int ni = tri[k].n - 1;
                if (ni >= 0 && ni < nn) glNormal3f(vn[ni * 3], vn[ni * 3 + 1], vn[ni * 3 + 2]);
                else glNormal3f(fn[0], fn[1], fn[2]);
                glVertex3f(p[k][0], p[k][1], p[k][2]);
            }
            out.triangles++;
        }
    }
    glEnd();
    glEndList();
    out.ok = true;
    return true;
}

}  // namespace lite
