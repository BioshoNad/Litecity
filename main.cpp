// ============================================================================
//  LITE CITY  v0.2 (beta)  -  sample open-world game running on LiteEngine
//  Story mode with John & Jason, phone, map, radio, multiplayer, shadows & sky.
// ============================================================================
#include "engine.h"
#include "audio.h"
#include "net.h"
#include "gfx.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
using namespace lite;
namespace au = lite::audio;

// ------------------------------------------------------------- constants ---
static const int   N = 10;                     // N x N city blocks
static const float PITCH = 62.f;               // block + road
static const float HALF = N * PITCH * 0.5f;    // 310
static const float DAY_LEN = 300.f;            // seconds per full day
static const int   MAXCARS = 80, MAXPEDS = 90;
enum { S_SUPER, S_AMMO, S_JOB, S_BILLS, S_HOSP, S_GYM };

struct Building { float x, z, hx, hz, h, r, g, b; int style; };
struct Spot { int type; float x, z, r, g, b; const char* name; };
struct Car {
    bool used = false, dead = false;
    int kind = 0;  // 0 traffic, 1 police, 2 owned/player, 3 stalled
    int type = 0;  // body shape: 0 sedan, 1 sports, 2 SUV
    float x = 0, z = 0, heading = 0, speed = 0, hp = 100, r = 1, g = 1, b = 1;
    int axis = 0, dir = 1;
    float lineC = 0, maxSpd = 12, stuck = 0, rev = 0, revSteer = 1, cool = 0, wreck = 0, idle = 0, despawn = 0;
};
struct Ped {
    bool alive = false;
    float x = 0, z = 0, heading = 0, t = 0, fear = 0, timer = 0, r = 1, g = 1, b = 1, phase = 0;
    float pr = 0.1f, pg = 0.1f, pb = 0.2f, hr = 0.1f, hg = 0.07f, hb = 0.05f;
    int skin = 0;
};
struct Particle { float x, y, z, vx, vy, vz, life, maxLife, r, g, b; };
struct Job { bool active = false; float x = 0, z = 0, time = 0; int reward = 0; };
struct Toast { std::string s; float t; };
struct Game {
    float x = 0, z = 0, heading = 0, health = 100, heat = 0, billsDue = 0, billTimer = 0, clock = 0;
    float deadTimer = 0, shotCool = 0, tracerT = 0, tx0 = 0, tz0 = 0, tx1 = 0, tz1 = 0;
    float camH = 0, walk = 0, copCool = 0, trafCool = 0, hurt = 0, lastHealth = 100, uptime = 0, swapCool = 0;
    int money = 500, ammo = 24, driving = -1, state = 0, prevStars = 0;  // state: 0 play, 1 dead, 2 evicted
    bool paused = false;
    Job job;
};

// two playable characters (the active one lives in `g`, the other is stored here)
struct Person {
    const char* name = "";
    float x = 0, z = 0, heading = 0, health = 100;
    int ammo = 24, driving = -1;
    gfx::PersonLook look;
};
struct Npc { const char* name; float x, z, heading; gfx::PersonLook look; };
struct SMS { std::string from, text; float time; bool read; };
struct DLine { const char* who; const char* text; };

static Game g;
static std::vector<Building> blds;
static std::vector<Spot> spots;
static Car cars[MAXCARS];
static Ped peds[MAXPEDS];
static std::vector<Particle> parts;
static std::vector<Toast> toasts;
static std::vector<float> lamps;  // x, z, faceX, faceZ per lamp
static GLuint cityList = 0, texAsph = 0, texWalk = 0, texGrass = 0, texB[3] = {0, 0, 0};

static Person chars[2];
static int cur = 0;
static bool jasonUnlocked = false;
static Npc friends[2];
static std::vector<SMS> inbox;

static int scene = 0;  // 0 main menu, 1 in game, 2 multiplayer menu
static float menuTime = 0.f, g_dt = 0.016f, g_nearCop = 1e9f;
static float hoverT[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static std::string ipBuf = "127.0.0.1", menuMsg;
static bool mapOpen = false, wpOn = false;
static float wpx = 0, wpz = 0;

struct Dialog { bool active = false; std::vector<DLine> lines; size_t idx = 0; };
static Dialog dlg;
struct Phone { bool open = false; int app = -1; int tab = 0; int callWho = -1; float callT = 0; std::string note; };
static Phone phone;

static const int NSTEPS = 6;  // number of story steps (see STEPS[] below)
static int storyStep = 0;
static bool storyWasWanted = false;

static bool inputLocked() { return phone.open || mapOpen || dlg.active; }
static bool kd(int vk) { return !inputLocked() && key(vk); }
static bool kt(int vk) { return !inputLocked() && tapped(vk); }

static void toast(const std::string& s) {
    toasts.push_back({s, 4.5f});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}
static void addHeat(float v) { g.heat = std::min(100.f, g.heat + v); }
static int stars() { return g.heat <= 0.01f ? 0 : std::min(5, 1 + (int)(g.heat / 20.f)); }
static float blockC(int i) { return (i - N / 2 + 0.5f) * PITCH; }
static float dist2(float ax, float az, float bx, float bz) { return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }

// ---------------------------------------------------------------- world ----
static void addSpecial(int bi, int bj, int type, float r, float gg, float b, const char* name, bool gymBehind = false) {
    float cx = blockC(bi), cz = blockC(bj);
    blds.push_back({cx, cz, 19.f, 19.f, 9.f, r, gg, b, 2});
    spots.push_back({type, cx, cz - 22.f, r, gg, b, name});
    if (gymBehind) spots.push_back({S_GYM, cx, cz + 22.f, 0.85f, 0.2f, 0.6f, "GYM"});
}

// draws one tier of a building from y0 up to y0+h
static void drawTier(const Building& b, float hx, float hz, float y0, float h, float cr, float cg, float cb) {
    float x0 = b.x - hx, x1 = b.x + hx, z0 = b.z - hz, z1 = b.z + hz, y1 = y0 + h;
    float u = (hx * 2) / 16.f, w = (hz * 2) / 16.f, v0 = y0 / 20.f, v1 = y1 / 20.f;
    glColor3f(cr, cg, cb);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texB[b.style]);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glTexCoord2f(0, v0); glVertex3f(x0, y0, z1); glTexCoord2f(u, v0); glVertex3f(x1, y0, z1); glTexCoord2f(u, v1); glVertex3f(x1, y1, z1); glTexCoord2f(0, v1); glVertex3f(x0, y1, z1);
    glNormal3f(0, 0, -1); glTexCoord2f(0, v0); glVertex3f(x1, y0, z0); glTexCoord2f(u, v0); glVertex3f(x0, y0, z0); glTexCoord2f(u, v1); glVertex3f(x0, y1, z0); glTexCoord2f(0, v1); glVertex3f(x1, y1, z0);
    glNormal3f(1, 0, 0);  glTexCoord2f(0, v0); glVertex3f(x1, y0, z1); glTexCoord2f(w, v0); glVertex3f(x1, y0, z0); glTexCoord2f(w, v1); glVertex3f(x1, y1, z0); glTexCoord2f(0, v1); glVertex3f(x1, y1, z1);
    glNormal3f(-1, 0, 0); glTexCoord2f(0, v0); glVertex3f(x0, y0, z0); glTexCoord2f(w, v0); glVertex3f(x0, y0, z1); glTexCoord2f(w, v1); glVertex3f(x0, y1, z1); glTexCoord2f(0, v1); glVertex3f(x0, y1, z0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glColor3f(cr * 0.5f, cg * 0.5f, cb * 0.5f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0); glVertex3f(x0, y1, z1); glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0); glVertex3f(x0, y1, z0);
    glEnd();
}

static void drawBuildingFull(const Building& b) {
    bool special = (b.style == 2 && b.h < 10.f);
    if (special) {  // shops: flat roof, awning strip, signboard colour
        drawTier(b, b.hx, b.hz, 0, b.h, b.r, b.g, b.b);
        glColor3f(b.r * 0.7f, b.g * 0.7f, b.b * 0.7f);
        box(b.x, b.h + 0.6f, b.z, b.hx * 2 + 0.6f, 1.2f, b.hz * 2 + 0.6f);
        return;
    }
    float h = b.h;
    if (h > 32.f) {  // setback tower
        float h1 = h * 0.72f;
        drawTier(b, b.hx, b.hz, 0, h1, b.r, b.g, b.b);
        drawTier(b, b.hx * 0.72f, b.hz * 0.72f, h1, h - h1, b.r * 0.95f, b.g * 0.95f, b.b * 0.98f);
    } else {
        drawTier(b, b.hx, b.hz, 0, h, b.r, b.g, b.b);
    }
    // rooftop props
    glColor3f(0.45f, 0.46f, 0.5f);
    box(b.x + b.hx * 0.3f, h + 0.6f, b.z - b.hz * 0.25f, 2.6f, 1.2f, 1.8f);
    box(b.x - b.hx * 0.3f, h + 0.4f, b.z + b.hz * 0.3f, 1.6f, 0.8f, 1.6f);
    if (h > 30.f) {
        glPushMatrix();
        glTranslatef(b.x, h, b.z);
        glColor3f(0.7f, 0.7f, 0.72f);
        gfx::cylY(0.12f, 7.f, 5);
        glPopMatrix();
    }
}

static void quadTex(float x0, float z0, float x1, float z1, float y, float uvScale) {
    glNormal3f(0, 1, 0);
    glTexCoord2f(x0 / uvScale, z0 / uvScale); glVertex3f(x0, y, z0);
    glTexCoord2f(x1 / uvScale, z0 / uvScale); glVertex3f(x1, y, z0);
    glTexCoord2f(x1 / uvScale, z1 / uvScale); glVertex3f(x1, y, z1);
    glTexCoord2f(x0 / uvScale, z1 / uvScale); glVertex3f(x0, y, z1);
}

static void buildWorld() {
    srand(1234);
    texAsph = gfx::makeGroundTexture(0);
    texWalk = gfx::makeGroundTexture(1);
    texGrass = gfx::makeGroundTexture(2);
    for (int i = 0; i < 3; i++) texB[i] = gfx::makeBuildingTexture(i);
    // special blocks (the gym sits right behind the ammo store)
    addSpecial(2, 2, S_SUPER, 0.20f, 0.80f, 0.30f, "SUPERMARKET");
    addSpecial(7, 2, S_AMMO, 0.95f, 0.50f, 0.10f, "AMMO STORE", true);
    addSpecial(5, 4, S_JOB, 0.20f, 0.50f, 0.95f, "JOB CENTER");
    addSpecial(2, 7, S_BILLS, 0.95f, 0.85f, 0.20f, "BILLS OFFICE");
    addSpecial(7, 7, S_HOSP, 0.95f, 0.95f, 0.95f, "HOSPITAL");
    // regular blocks: 2x2 lots
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            bool special = false;
            for (const Spot& s : spots)
                if (s.type != S_GYM && fabsf(s.x - blockC(i)) < 1 && fabsf(s.z + 22.f - blockC(j)) < 1) special = true;
            if (special) continue;
            for (int a = -1; a <= 1; a += 2)
                for (int c = -1; c <= 1; c += 2) {
                    float dc = dist2(blockC(i), blockC(j), 0, 0) / HALF;  // downtown is taller
                    float h = (frand() < 0.2f) ? frange(8, 16) : frange(14, 38 + 34 * (1.f - dc));
                    int style = rand() % 3;
                    float tone = style == 1 ? frange(0.8f, 1.0f) : frange(0.62f, 0.92f);
                    blds.push_back({blockC(i) + a * 11.f, blockC(j) + c * 11.f, 9.5f, 9.5f, h,
                                    tone * frange(0.92f, 1.05f), tone * frange(0.94f, 1.0f), tone * frange(0.96f, 1.08f), style});
                }
        }
    // compile the static city into a display list
    cityList = glGenLists(1);
    glNewList(cityList, GL_COMPILE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texGrass);
    glBegin(GL_QUADS);
    glColor3f(1, 1, 1);
    quadTex(-900, -900, 900, 900, -0.05f, 18.f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, texAsph);
    glBegin(GL_QUADS);
    glColor3f(1, 1, 1);
    for (int k = 0; k <= N; k++) {
        float L = k * PITCH - HALF, e = HALF + 7.f;
        quadTex(-e, L - 7, e, L + 7, 0.02f, 8.f);
        quadTex(L - 7, -e, L + 7, e, 0.021f, 8.f);
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, texWalk);
    glBegin(GL_QUADS);
    glColor3f(1, 1, 1);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) quadTex(blockC(i) - 24, blockC(j) - 24, blockC(i) + 24, blockC(j) + 24, 0.151f, 6.f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    // curbs (block side walls)
    glColor3f(0.62f, 0.62f, 0.62f);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) box(blockC(i), 0.05f, blockC(j), 48.f, 0.2f, 48.f);
    // lane dashes + crosswalks
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glColor3f(0.92f, 0.82f, 0.3f);
    for (int k = 0; k <= N; k++) {
        float L = k * PITCH - HALF;
        for (float t = -HALF; t < HALF; t += 10.f) {
            float d = fmodf(t + HALF, PITCH);
            if (std::min(d, PITCH - d) < 11.f) continue;
            glVertex3f(t, 0.05f, L - 0.18f); glVertex3f(t + 4, 0.05f, L - 0.18f); glVertex3f(t + 4, 0.05f, L + 0.18f); glVertex3f(t, 0.05f, L + 0.18f);
            glVertex3f(L - 0.18f, 0.05f, t); glVertex3f(L + 0.18f, 0.05f, t); glVertex3f(L + 0.18f, 0.05f, t + 4); glVertex3f(L - 0.18f, 0.05f, t + 4);
        }
    }
    glColor3f(0.92f, 0.92f, 0.92f);
    for (int a = 0; a <= N; a++)
        for (int b2 = 0; b2 <= N; b2++) {
            float X = a * PITCH - HALF, Z = b2 * PITCH - HALF;
            for (int s = -1; s <= 1; s += 2)
                for (float o = -5.4f; o <= 5.5f; o += 1.2f) {
                    float cx = X + s * 9.5f, cz = Z + s * 9.5f;
                    if (fabsf(cx) <= HALF + 6.f) {  // zebra bars on the arms of the x-road
                        glVertex3f(cx - 1.4f, 0.052f, Z + o - 0.3f); glVertex3f(cx + 1.4f, 0.052f, Z + o - 0.3f); glVertex3f(cx + 1.4f, 0.052f, Z + o + 0.3f); glVertex3f(cx - 1.4f, 0.052f, Z + o + 0.3f);
                    }
                    if (fabsf(cz) <= HALF + 6.f) {  // ... and of the z-road
                        glVertex3f(X + o - 0.3f, 0.052f, cz - 1.4f); glVertex3f(X + o + 0.3f, 0.052f, cz - 1.4f); glVertex3f(X + o + 0.3f, 0.052f, cz + 1.4f); glVertex3f(X + o - 0.3f, 0.052f, cz + 1.4f);
                    }
                }
        }
    glEnd();
    for (const Building& b : blds) drawBuildingFull(b);
    // trees and street lamps
    lamps.clear();
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float cx = blockC(i), cz = blockC(j);
            for (int side = 0; side < 4; side++)
                for (float t = -16.f; t <= 16.f; t += 8.f) {
                    float tx = (side == 0) ? cx + t : (side == 1) ? cx + t : (side == 2) ? cx - 22.7f : cx + 22.7f;
                    float tz = (side == 0) ? cz - 22.7f : (side == 1) ? cz + 22.7f : cz + t;
                    bool near = false;
                    for (const Spot& s : spots) if (dist2(s.x, s.z, tx, tz) < 6.5f) near = true;
                    if (near || frand() < 0.4f) continue;
                    gfx::drawTree(tx, tz, frange(0.8f, 1.15f), frange(0.8f, 1.2f));
                }
            const float sx[2] = {-1, 1}, sz[2] = {1, -1};
            for (int k = 0; k < 2; k++) {
                float lx = cx + sx[k] * 23.4f, lz = cz + sz[k] * 23.4f;
                gfx::drawLamp(lx, lz, sx[k], sz[k]);
                lamps.push_back(lx + sx[k] * 1.8f / 1.41f); lamps.push_back(lz + sz[k] * 1.8f / 1.41f);
                lamps.push_back(sx[k]); lamps.push_back(sz[k]);
            }
        }
    glEndList();
}

// collision: push a circle out of all buildings. returns true if pushed.
static bool pushOut(float& x, float& z, float r, float* nx = nullptr, float* nz = nullptr) {
    bool hit = false;
    float ax = 0, az = 0;
    for (const Building& b : blds) {
        if (fabsf(x - b.x) > b.hx + r || fabsf(z - b.z) > b.hz + r) continue;
        float cx = clampf(x, b.x - b.hx, b.x + b.hx), cz = clampf(z, b.z - b.hz, b.z + b.hz);
        float dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        float px, pz;
        if (d2 > 1e-6f) {
            float d = sqrtf(d2);
            px = dx / d * (r - d); pz = dz / d * (r - d);
        } else {
            float ex = b.hx - fabsf(x - b.x), ez = b.hz - fabsf(z - b.z);
            if (ex < ez) { px = (x < b.x ? -1.f : 1.f) * (ex + r); pz = 0; }
            else { pz = (z < b.z ? -1.f : 1.f) * (ez + r); px = 0; }
        }
        x += px; z += pz; ax += px; az += pz; hit = true;
    }
    if (hit && nx) {
        float l = sqrtf(ax * ax + az * az);
        *nx = l > 1e-6f ? ax / l : 0; *nz = l > 1e-6f ? az / l : 0;
    }
    return hit;
}

static float rayBox(float ox, float oz, float dx, float dz, const Building& b) {
    float tmin = 0, tmax = 1e9f;
    float o[2] = {ox, oz}, d[2] = {dx, dz}, lo[2] = {b.x - b.hx, b.z - b.hz}, hi[2] = {b.x + b.hx, b.z + b.hz};
    for (int i = 0; i < 2; i++) {
        if (fabsf(d[i]) < 1e-6f) { if (o[i] < lo[i] || o[i] > hi[i]) return 1e9f; }
        else {
            float t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) return 1e9f;
        }
    }
    return tmin;
}
static bool losClear(float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return true;
    dx /= len; dz /= len;
    for (const Building& b : blds) if (rayBox(ax, az, dx, dz, b) < len) return false;
    return true;
}

// -------------------------------------------------------------- effects ----
static void burst(float x, float y, float z, int n, float r, float gg, float b, float spd) {
    for (int i = 0; i < n; i++) {
        Particle p;
        float a = frand() * 2 * PI, s = frand() * spd;
        p.x = x; p.y = y; p.z = z; p.vx = cosf(a) * s; p.vz = sinf(a) * s; p.vy = frange(2.f, spd);
        p.life = p.maxLife = frange(0.5f, 1.4f); p.r = r; p.g = gg; p.b = b;
        parts.push_back(p);
    }
}

// ----------------------------------------------------------------- cars ----
static int freeCar() { for (int i = 0; i < MAXCARS; i++) if (!cars[i].used) return i; return -1; }

static void setLanePos(Car& c, float a) {
    c.heading = (c.axis == 0) ? (c.dir > 0 ? PI * 0.5f : -PI * 0.5f) : (c.dir > 0 ? 0.f : PI);
    float rcx = -cosf(c.heading), rcz = sinf(c.heading);
    if (c.axis == 0) { c.x = a; c.z = c.lineC + 3.6f * rcz; }
    else { c.z = a; c.x = c.lineC + 3.6f * rcx; }
}

static void spawnTraffic(bool farFromPlayer) {
    int i = freeCar();
    if (i < 0) return;
    for (int tries = 0; tries < 15; tries++) {
        Car c;
        c.used = true; c.kind = 0; c.hp = 100; c.type = rand() % 3;
        c.axis = rand() % 2; c.dir = (rand() % 2) ? 1 : -1;
        c.lineC = (rand() % (N + 1)) * PITCH - HALF;
        c.r = frange(0.2f, 1.f); c.g = frange(0.2f, 1.f); c.b = frange(0.2f, 1.f);
        c.maxSpd = frange(9.f, 15.f); c.speed = c.maxSpd;
        setLanePos(c, frange(-HALF, HALF));
        if (dist2(c.x, c.z, g.x, g.z) < (farFromPlayer ? 90.f : 35.f)) continue;
        cars[i] = c;
        return;
    }
}

static void spawnPolice() {
    int i = freeCar();
    if (i < 0) return;
    for (int tries = 0; tries < 25; tries++) {
        Car c;
        c.used = true; c.kind = 1; c.hp = 150; c.type = 0;
        c.axis = rand() % 2; c.dir = (rand() % 2) ? 1 : -1;
        c.lineC = (rand() % (N + 1)) * PITCH - HALF;
        setLanePos(c, frange(-HALF, HALF));
        float d = dist2(c.x, c.z, g.x, g.z);
        if (d < 90.f || d > 170.f) continue;
        c.heading = atan2f(g.x - c.x, g.z - c.z);
        c.speed = 8.f; c.r = 0.08f; c.g = 0.08f; c.b = 0.1f;
        cars[i] = c;
        return;
    }
}

static void explode(int i, bool byPlayer) {
    Car& c = cars[i];
    c.dead = true; c.wreck = 14.f; c.speed = 0;
    burst(c.x, 1.2f, c.z, 45, 1.f, 0.55f, 0.1f, 9.f);
    burst(c.x, 1.0f, c.z, 25, 0.2f, 0.2f, 0.2f, 6.f);
    au::play(au::SFX_EXPLODE, clampf(1.2f - dist2(c.x, c.z, g.x, g.z) / 160.f, 0.12f, 1.f));
    float d = dist2(c.x, c.z, g.x, g.z);
    if (g.driving == i) {
        g.health -= 55.f; g.driving = -1; g.x = c.x + 3.f; g.z = c.z;
        toast("Your car exploded!");
    } else if (d < 7.f && g.state == 0) {
        g.health -= 40.f * (1.f - d / 7.f);
    }
    if (byPlayer) {
        if (c.kind == 1) { addHeat(30); toast("Officer down!"); }
        else addHeat(15);
    }
}
static void damageCar(int i, float d, bool byPlayer) {
    Car& c = cars[i];
    if (c.dead) return;
    c.hp -= d;
    if (c.hp <= 0) explode(i, byPlayer);
}

static void moveCar(int idx, float dt) {
    Car& c = cars[idx];
    c.x += sinf(c.heading) * c.speed * dt;
    c.z += cosf(c.heading) * c.speed * dt;
    float nx = 0, nz = 0;
    if (pushOut(c.x, c.z, 2.0f, &nx, &nz)) {
        float fx = sinf(c.heading), fz = cosf(c.heading);
        float into = fabsf(fx * nx + fz * nz);
        float impact = fabsf(c.speed) * into;
        if (impact > 7.f) {
            damageCar(idx, (impact - 7.f) * 3.f, idx == g.driving);
            if (idx == g.driving) g.health -= (impact - 7.f) * 0.5f;
            burst(c.x, 1.f, c.z, 6, 1.f, 0.8f, 0.3f, 4.f);
            au::play(au::SFX_HIT, 0.6f);
        }
        c.speed *= (1.f - 0.8f * into);
    }
    c.x = clampf(c.x, -HALF - 14, HALF + 14);
    c.z = clampf(c.z, -HALF - 14, HALF + 14);
}

static void driveInput(Car& c, float dt) {
    bool fw = kd('W') || kd(VK_UP), bk = kd('S') || kd(VK_DOWN);
    bool lf = kd('A') || kd(VK_LEFT), rt = kd('D') || kd(VK_RIGHT), hb = kd(VK_SPACE);
    const float MAXF = 36.f, MAXR = 10.f;
    if (fw) { if (c.speed < 0) c.speed += 32 * dt; else c.speed += (16.f * (1.f - c.speed / MAXF) + 3.f) * dt; }
    else if (bk) { if (c.speed > 0.5f) c.speed -= 30 * dt; else c.speed -= 9 * dt; }
    else { float d = 5.f * dt; if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d); }
    if (hb) { float d = 28 * dt; if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d); }
    c.speed = clampf(c.speed, -MAXR, MAXF);
    float s = (lf ? 1.f : 0.f) - (rt ? 1.f : 0.f);
    c.heading += s * (hb ? 1.6f : 1.f) * 2.1f / (1.f + fabsf(c.speed) / 16.f) * clampf(c.speed / 4.f, -1.f, 1.f) * dt;
}

static void updateTraffic(Car& c, int idx, float dt) {
    float fx = sinf(c.heading), fz = cosf(c.heading), rx = -cosf(c.heading), rz = sinf(c.heading);
    bool block = false;
    for (int j = 0; j < MAXCARS && !block; j++) {
        if (j == idx || !cars[j].used) continue;
        float dx = cars[j].x - c.x, dz = cars[j].z - c.z, al = dx * fx + dz * fz;
        if (al < 0.5f || al > 14.f) continue;
        if (fabsf(dx * rx + dz * rz) < 2.6f) block = true;
    }
    for (int j = 0; j < MAXPEDS && !block; j++) {
        if (!peds[j].alive) continue;
        float dx = peds[j].x - c.x, dz = peds[j].z - c.z, al = dx * fx + dz * fz;
        if (al < 0.5f || al > 10.f) continue;
        if (fabsf(dx * rx + dz * rz) < 1.8f) block = true;
    }
    if (g.state == 0 && g.driving < 0) {  // pedestrian player
        float dx = g.x - c.x, dz = g.z - c.z, al = dx * fx + dz * fz;
        if (al > 0.5f && al < 10.f && fabsf(dx * rx + dz * rz) < 1.8f) block = true;
    }
    float target = block ? 0.f : c.maxSpd;
    c.speed += clampf(target - c.speed, -25.f * dt, 8.f * dt);
    float a = ((c.axis == 0) ? c.x : c.z) + c.speed * dt * c.dir;
    if (a > HALF + 12) a = -HALF - 12;
    if (a < -HALF - 12) a = HALF + 12;
    setLanePos(c, a);
}

static void updatePolice(Car& c, int idx, float dt) {
    int st = stars();
    if (st == 0) {
        c.speed += clampf(0.f - c.speed, -10.f * dt, 0.f);
        c.despawn += dt;
        if (c.despawn > 6.f) c.used = false;
        moveCar(idx, dt);
        return;
    }
    c.despawn = 0;
    float dx = g.x - c.x, dz = g.z - c.z, d = sqrtf(dx * dx + dz * dz);
    float diff = wrapAngle(atan2f(dx, dz) - c.heading);
    float steer = clampf(diff * 2.f, -1.f, 1.f);
    float target = (d < 7.f) ? 0.f : 22.f + st * 2.5f;
    if (c.rev > 0) { c.rev -= dt; target = -7.f; steer = c.revSteer; }
    c.speed += clampf(target - c.speed, -26.f * dt, 15.f * dt);
    c.heading += steer * 2.4f * clampf(fabsf(c.speed) / 5.f, 0.f, 1.f) * (c.speed >= 0 ? 1.f : -1.f) * dt;
    if (fabsf(c.speed) < 2.f && d > 10.f && c.rev <= 0) {
        c.stuck += dt;
        if (c.stuck > 0.7f) { c.rev = 1.1f; c.stuck = 0; c.revSteer = (rand() % 2) ? 1.f : -1.f; }
    } else if (c.rev <= 0) c.stuck = 0;
    moveCar(idx, dt);
    // gunfire at the player
    if (g.state == 0 && d < 34.f && losClear(c.x, c.z, g.x, g.z)) {
        float dps = (1.2f + st * 0.25f) * (1.f - d / 40.f) * (g.driving >= 0 ? 0.5f : 1.f);
        g.health -= dps * dt;
    }
    // melee bump on foot
    if (g.state == 0 && g.driving < 0 && d < 2.8f && fabsf(c.speed) > 3.f) {
        g.health -= 22.f * dt * 4.f;
    }
}

// ----------------------------------------------------------------- peds ----
static void spawnPed(Ped& p, bool anywhere) {
    for (int tries = 0; tries < 20; tries++) {
        int i = rand() % N, j = rand() % N, side = rand() % 4;
        float cx = blockC(i), cz = blockC(j), e = 22.4f, t = frange(-22.f, 22.f);
        p.x = (side == 0) ? cx + t : (side == 1) ? cx + t : (side == 2) ? cx - e : cx + e;
        p.z = (side == 0) ? cz - e : (side == 1) ? cz + e : cz + t;
        if (side == 3) p.z = cz + t;
        if (anywhere || dist2(p.x, p.z, g.x, g.z) > 25.f) break;
    }
    p.alive = true; p.heading = frange(-PI, PI); p.t = frange(1, 5); p.fear = 0;
    p.r = frange(0.15f, 1.f); p.g = frange(0.15f, 1.f); p.b = frange(0.15f, 1.f);
    p.pr = frange(0.05f, 0.45f); p.pg = frange(0.05f, 0.4f); p.pb = frange(0.08f, 0.5f);
    float hv = frand();
    p.hr = hv < 0.5f ? 0.08f : (hv < 0.8f ? 0.35f : 0.75f); p.hg = hv < 0.5f ? 0.06f : (hv < 0.8f ? 0.22f : 0.6f); p.hb = hv < 0.5f ? 0.05f : (hv < 0.8f ? 0.12f : 0.3f);
    p.skin = rand() % 4;
}

static void killPed(Ped& p, bool byPlayer) {
    p.alive = false; p.timer = 15.f;
    burst(p.x, 1.f, p.z, 10, 0.7f, 0.05f, 0.05f, 3.f);
    if (byPlayer) {
        int cash = 10 + rand() % 51;
        g.money += cash;
        addHeat(12);
        toast("+$" + std::to_string(cash));
        au::play(au::SFX_CASH, 0.7f);
    }
}

// ----------------------------------------------------------------- game ----
static void shoot() {
    g.ammo--; g.shotCool = 0.18f; addHeat(3.f);
    au::play(au::SFX_SHOT, 0.8f);
    float fx = sinf(g.heading), fz = cosf(g.heading), ox = g.x + fx * 0.6f, oz = g.z + fz * 0.6f;
    float bt = 60.f;
    for (const Building& b : blds) { float t = rayBox(ox, oz, fx, fz, b); if (t < bt) bt = t; }
    int hitCar = -1, hitPed = -1;
    for (int i = 0; i < MAXCARS; i++) {
        const Car& c = cars[i];
        if (!c.used || c.dead || i == g.driving) continue;
        float rx = c.x - ox, rz = c.z - oz, al = rx * fx + rz * fz, lat = fabsf(rx * fz - rz * fx);
        if (al > 0 && al < bt && lat < 1.6f + al * 0.05f) { bt = al; hitCar = i; hitPed = -1; }
    }
    for (int i = 0; i < MAXPEDS; i++) {
        const Ped& p = peds[i];
        if (!p.alive) continue;
        float rx = p.x - ox, rz = p.z - oz, al = rx * fx + rz * fz, lat = fabsf(rx * fz - rz * fx);
        if (al > 0 && al < bt && lat < 0.7f + al * 0.05f) { bt = al; hitPed = i; hitCar = -1; }
    }
    g.tracerT = 0.06f; g.tx0 = ox; g.tz0 = oz; g.tx1 = ox + fx * bt; g.tz1 = oz + fz * bt;
    for (int i = 0; i < MAXPEDS; i++)  // gunfire scares pedestrians
        if (peds[i].alive && dist2(peds[i].x, peds[i].z, g.x, g.z) < 35.f) peds[i].fear = 5.f;
    if (hitPed >= 0) {
        burst(peds[hitPed].x, 1.2f, peds[hitPed].z, 4, 0.7f, 0.05f, 0.05f, 2.f);
        killPed(peds[hitPed], true);
    } else if (hitCar >= 0) {
        Car& c = cars[hitCar];
        burst(c.x, 1.2f, c.z, 5, 1.f, 0.9f, 0.4f, 3.f);
        if (c.kind == 0) { c.kind = 3; addHeat(5.f); }
        if (c.kind == 1) addHeat(8.f);
        damageCar(hitCar, 30.f, true);
    }
}

static void startJob() {
    for (int tries = 0; tries < 30; tries++) {
        int i = rand() % N, j = rand() % N;
        float x = blockC(i), z = blockC(j) - 22.f;
        bool special = false;
        for (const Spot& s : spots) if (fabsf(s.x - x) < 1 && fabsf(s.z - z) < 1) special = true;
        float d = dist2(x, z, g.x, g.z);
        if (special || d < 140.f) continue;
        g.job.active = true; g.job.x = x; g.job.z = z;
        g.job.time = d / 9.f + 30.f; g.job.reward = 80 + (int)(d * 0.6f);
        toast("Job accepted: deliver the cargo by car!");
        return;
    }
}

static int nearestSpot(float maxD) {
    int best = -1; float bd = maxD;
    for (size_t i = 0; i < spots.size(); i++) {
        float d = dist2(spots[i].x, spots[i].z, g.x, g.z);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

static void doInteract(int si) {
    switch (spots[si].type) {
    case S_SUPER:
        if (g.money < 25) toast("Not enough cash ($25)");
        else if (g.health >= 100) toast("You are not hungry");
        else { g.money -= 25; g.health = std::min(100.f, g.health + 50.f); toast("Ate a burger: +50 health, -$25"); }
        break;
    case S_AMMO:
        if (g.money < 60) toast("Not enough cash ($60)");
        else { g.money -= 60; g.ammo += 30; toast("Bought 30 rounds, -$60"); }
        break;
    case S_JOB:
        if (g.job.active) toast("Finish your current job first");
        else startJob();
        break;
    case S_BILLS:
        if (g.billsDue <= 0) toast("No bills due. Nice.");
        else {
            int pay = std::min(g.money, (int)g.billsDue);
            if (pay <= 0) toast("You have no cash!");
            else { g.money -= pay; g.billsDue -= pay; toast("Paid $" + std::to_string(pay) + " in bills"); }
        }
        break;
    case S_GYM:
        if (storyStep >= NSTEPS) toast("Marcus: We run this town now. Watch your bills!");
        else toast("Marcus: Need anything? Hit J and call me for hints.");
        break;
    case S_HOSP:
        if (g.money < 50) toast("Treatment costs $50");
        else if (g.health >= 100) toast("You are healthy");
        else { g.money -= 50; g.health = 100; toast("Treated: full health, -$50"); }
        break;
    }
}

// ------------------------------------------------- characters & story -----
static void saveCur() {
    Person& p = chars[cur];
    p.x = g.x; p.z = g.z; p.heading = g.heading; p.health = g.health; p.ammo = g.ammo; p.driving = g.driving;
}
static void loadCur() {
    Person& p = chars[cur];
    g.x = p.x; g.z = p.z; g.heading = p.heading; g.health = p.health; g.ammo = p.ammo; g.driving = p.driving;
    g.lastHealth = g.health; g.camH = g.heading;
}
static void swapCharacter() {
    if (!jasonUnlocked) { toast("Jason is not with you yet."); return; }
    if (g.state != 0 || g.swapCool > 0) return;
    saveCur();
    cur = 1 - cur;
    loadCur();
    g.swapCool = 0.6f;
    au::play(au::SFX_DOOR, 0.8f);
    toast(std::string("Now playing: ") + chars[cur].name);
}

static void sendSms(const char* from, const char* text) {
    inbox.push_back({from, text, g.clock, false});
    au::play(au::SFX_MSG, 0.9f);
    toast(std::string("New message from ") + from + "  (press J)");
}

static const std::vector<std::vector<DLine>> DIALOGS = {
    {{"Marcus", "John! Look who finally came back to town."},
     {"John", "Marcus. Dante. It has been a long time."},
     {"Dante", "Long enough that your bills are stacking up, I bet."},
     {"John", "Something like that. Got any work?"},
     {"Marcus", "Maybe. First you need wheels. Our buddy Jason is waiting at the supermarket."},
     {"Marcus", "Steal whatever you can find and go pick him up."}},
    {{"Jason", "John! Nice ride. Marcus told me everything."},
     {"John", "Then you know the plan."},
     {"Jason", "We need cash and a distraction. Count me in."},
     {"Jason", "Press Q any time to swap between you and me."}},
    {{"Dante", "Clean getaway! You still have it, John."},
     {"Dante", "Bad news though. Marcus got clipped leaving the scene."}},
    {{"Marcus", "Just a scratch. Listen, the real job is at the docks."},
     {"Marcus", "A crate arrives tonight. Get there before the other crew does."},
     {"Jason", "We will handle it. Rest up, Marcus."}},
    {{"Marcus", "(phone) Is it there?"},
     {"John", "Package secured."},
     {"Marcus", "Perfect. This city is ours now."},
     {"Jason", "CHAPTER 1 COMPLETE - to be continued..."}}};

enum { ST_SPOT, ST_DRIVE_SPOT, ST_STARS, ST_LOSE, ST_POS };
struct Step { int type, spot; float x, z; int stars; const char* obj; const char* from; const char* sms; int reward, dlg; };
static const Step STEPS[] = {
    {ST_SPOT, S_GYM, 0, 0, 0, "Meet your friends at the GYM (behind the Ammo Store)", "Marcus", "Yo John, you are back? Come to the gym. I got work for you. -Marcus", 150, 0},
    {ST_DRIVE_SPOT, S_SUPER, 0, 0, 0, "Steal a car and drive to the SUPERMARKET to pick up Jason", "Jason", "Bro! I am at the supermarket. Come get me, I need a ride!", 100, 1},
    {ST_STARS, 0, 0, 0, 2, "Make some noise: reach a 2-star wanted level", "Marcus", "Cops are too relaxed. Wake them up. 2 stars. We will watch.", 0, -1},
    {ST_LOSE, 0, 0, 0, 0, "Lose the cops (get your wanted level to zero)", "Dante", "Nice! Now ditch them. Use the side streets.", 300, 2},
    {ST_SPOT, S_HOSP, 0, 0, 0, "Marcus got hurt. Go to the HOSPITAL", "Dante", "Marcus got clipped. He is at the hospital. Hurry.", 100, 3},
    {ST_POS, 0, -248, -248, 0, "Drive to the DOCKS (south-west corner of the map)", "Marcus", "Docks. South-west corner. The crate arrives tonight. Go!", 1000, 4}};
static_assert(NSTEPS == (int)(sizeof(STEPS) / sizeof(STEPS[0])), "update NSTEPS when you add story steps");

static void startDialog(int id) {
    if (id < 0 || id >= (int)DIALOGS.size()) return;
    dlg.active = true; dlg.lines = DIALOGS[id]; dlg.idx = 0;
}

static bool storyTarget(float& tx, float& tz) {
    if (storyStep >= NSTEPS) return false;
    const Step& s = STEPS[storyStep];
    if (s.type == ST_POS) { tx = s.x; tz = s.z; return true; }
    if (s.type == ST_SPOT || s.type == ST_DRIVE_SPOT) {
        for (const Spot& sp : spots) if (sp.type == s.spot) { tx = sp.x; tz = sp.z; return true; }
    }
    return false;
}

static void completeStep() {
    const Step& s = STEPS[storyStep];
    if (s.reward > 0) {
        g.money += s.reward;
        au::play(au::SFX_CASH);
        toast("Mission complete!  +$" + std::to_string(s.reward));
    }
    if (storyStep == 1) { jasonUnlocked = true; toast("Jason joined you. Press Q to switch characters."); }
    startDialog(s.dlg);
    storyWasWanted = (s.type == ST_STARS);  // the 2-star step hands over a wanted player to the 'lose the cops' step
    storyStep++;
    if (storyStep < NSTEPS) sendSms(STEPS[storyStep].from, STEPS[storyStep].sms);
    else toast("CHAPTER 1 COMPLETE. Keep playing: jobs, bills and the cops never stop.");
}

static void updateStory(float) {
    if (storyStep >= NSTEPS) return;
    const Step& s = STEPS[storyStep];
    float tx = 0, tz = 0;
    bool done = false;
    switch (s.type) {
    case ST_SPOT: if (storyTarget(tx, tz)) done = dist2(tx, tz, g.x, g.z) < 6.5f; break;
    case ST_DRIVE_SPOT: if (storyTarget(tx, tz)) done = g.driving >= 0 && dist2(tx, tz, g.x, g.z) < 7.5f; break;
    case ST_STARS: done = stars() >= s.stars; break;
    case ST_LOSE: if (stars() > 0) storyWasWanted = true; done = storyWasWanted && stars() == 0; break;
    case ST_POS: done = g.driving >= 0 && dist2(s.x, s.z, g.x, g.z) < 10.f; break;
    }
    if (done) completeStep();
}

// ------------------------------------------------------------ save / load --
static const char* SAVE_FILE = "litecity.sav";
static bool saveGame() {
    saveCur();
    FILE* f = fopen(SAVE_FILE, "w");
    if (!f) return false;
    fprintf(f, "%d %d %d %d %.2f %.2f %.1f\n", g.money, storyStep, jasonUnlocked ? 1 : 0, cur, g.billsDue, g.clock, g.heat);
    for (int i = 0; i < 2; i++) fprintf(f, "%.2f %.2f %.3f %.1f %d\n", chars[i].x, chars[i].z, chars[i].heading, chars[i].health, chars[i].ammo);
    fclose(f);
    return true;
}
static bool loadGame() {
    FILE* f = fopen(SAVE_FILE, "r");
    if (!f) return false;
    int money, step, jas, c;
    float bills, clk, heat;
    if (fscanf(f, "%d %d %d %d %f %f %f", &money, &step, &jas, &c, &bills, &clk, &heat) != 7) { fclose(f); return false; }
    Person tmp[2] = {chars[0], chars[1]};
    for (int i = 0; i < 2; i++)
        if (fscanf(f, "%f %f %f %f %d", &tmp[i].x, &tmp[i].z, &tmp[i].heading, &tmp[i].health, &tmp[i].ammo) != 5) { fclose(f); return false; }
    fclose(f);
    chars[0] = tmp[0]; chars[1] = tmp[1];
    for (int i = 0; i < 2; i++) chars[i].driving = -1;
    g.money = money; storyStep = std::min(std::max(step, 0), NSTEPS); jasonUnlocked = jas != 0; cur = (c == 1) ? 1 : 0;
    g.billsDue = bills; g.clock = clk; g.heat = 0; g.state = 0; g.driving = -1;
    loadCur();
    for (int i = 0; i < MAXCARS; i++) if (cars[i].kind == 1) cars[i].used = false;
    return true;
}

// ------------------------------------------------------------------ reset --
static void resetGame() {
    for (int i = 0; i < MAXCARS; i++) cars[i] = Car();
    for (int i = 0; i < MAXPEDS; i++) peds[i] = Ped();
    parts.clear(); toasts.clear(); inbox.clear();
    dlg = Dialog(); phone = Phone(); mapOpen = false; wpOn = false;
    float clock = DAY_LEN * 0.08f;
    g = Game();
    g.clock = clock;
    // the two characters
    chars[0] = Person(); chars[1] = Person();
    chars[0].name = "John";
    chars[0].look.shirt[0] = 0.25f; chars[0].look.shirt[1] = 0.32f; chars[0].look.shirt[2] = 0.45f;
    chars[0].look.pants[0] = 0.1f; chars[0].look.pants[1] = 0.12f; chars[0].look.pants[2] = 0.2f;
    chars[0].look.hair[0] = 0.08f; chars[0].look.hair[1] = 0.06f; chars[0].look.hair[2] = 0.05f;
    chars[0].look.skin = 1; chars[0].look.jacket = true;
    chars[1].name = "Jason";
    chars[1].look.shirt[0] = 0.75f; chars[1].look.shirt[1] = 0.12f; chars[1].look.shirt[2] = 0.12f;
    chars[1].look.pants[0] = 0.25f; chars[1].look.pants[1] = 0.22f; chars[1].look.pants[2] = 0.16f;
    chars[1].look.hair[0] = 0.8f; chars[1].look.hair[1] = 0.68f; chars[1].look.hair[2] = 0.3f;
    chars[1].look.skin = 0;
    chars[0].x = -9.f; chars[0].z = 0.f; chars[0].heading = 0.f; chars[0].ammo = 24;
    float jx = 0, jz = 0, gx = 0, gz = 0;
    for (const Spot& s : spots) {
        if (s.type == S_SUPER) { jx = s.x + 3.f; jz = s.z - 1.f; }
        if (s.type == S_GYM) { gx = s.x; gz = s.z; }
    }
    chars[1].x = jx; chars[1].z = jz; chars[1].heading = 3.14f; chars[1].ammo = 12;
    cur = 0; jasonUnlocked = false;
    loadCur();
    // gym friends
    friends[0] = {"MARCUS", gx - 2.6f, gz - 1.5f, 3.14f, gfx::PersonLook()};
    friends[0].look.shirt[0] = 0.1f; friends[0].look.shirt[1] = 0.1f; friends[0].look.shirt[2] = 0.1f; friends[0].look.skin = 2; friends[0].look.jacket = true;
    friends[1] = {"DANTE", gx + 2.6f, gz - 1.5f, 3.4f, gfx::PersonLook()};
    friends[1].look.shirt[0] = 0.85f; friends[1].look.shirt[1] = 0.85f; friends[1].look.shirt[2] = 0.2f; friends[1].look.skin = 3;
    friends[1].look.hair[0] = 0.02f; friends[1].look.hair[1] = 0.02f; friends[1].look.hair[2] = 0.02f;
    // the player's own sports car
    Car pc; pc.used = true; pc.kind = 2; pc.type = 1; pc.x = -5.5f; pc.z = 2.f; pc.heading = 0.f; pc.hp = 120;
    pc.r = 0.85f; pc.g = 0.1f; pc.b = 0.1f;
    cars[0] = pc;
    for (int i = 0; i < 28; i++) spawnTraffic(false);
    for (int i = 0; i < MAXPEDS; i++) spawnPed(peds[i], false);
    storyStep = 0; storyWasWanted = false;
    toast("Welcome to LITE CITY, John.");
    sendSms(STEPS[0].from, STEPS[0].sms);
}

static void respawnPlayer() {
    float hx = 0, hz = 0;
    for (const Spot& s : spots) if (s.type == S_HOSP) { hx = s.x; hz = s.z - 5.f; }
    g.x = hx; g.z = hz; g.health = 60; g.heat = 0; g.driving = -1; g.state = 0;
    int fee = std::min(g.money, 150);
    g.money -= fee; g.lastHealth = g.health;
    for (int i = 0; i < MAXCARS; i++) if (cars[i].kind == 1) cars[i].used = false;
    toast("Treated at the hospital. Fee: $" + std::to_string(fee));
}

static void updateWorld(float dt) {
    // --- cars
    for (int i = 0; i < MAXCARS; i++) {
        Car& c = cars[i];
        if (!c.used) continue;
        if (c.cool > 0) c.cool -= dt;
        if (c.dead) {
            c.wreck -= dt;
            if (frand() < 0.5f) burst(c.x, 1.f, c.z, 1, 0.9f, 0.4f, 0.1f, 2.f);
            if (c.wreck <= 0) c.used = false;
            continue;
        }
        if (i == g.driving && g.state == 0) { driveInput(c, dt); moveCar(i, dt); }
        else if (c.kind == 0) updateTraffic(c, i, dt);
        else if (c.kind == 1) updatePolice(c, i, dt);
        else {  // owned-but-parked or stalled
            float d = 8.f * dt;
            if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d);
            moveCar(i, dt);
            if (c.kind == 3) {
                c.idle += dt;
                if (c.idle > 25.f && dist2(c.x, c.z, g.x, g.z) > 70.f) c.used = false;
            }
        }
    }
    // --- car vs car
    for (int i = 0; i < MAXCARS; i++) {
        if (!cars[i].used || cars[i].dead) continue;
        for (int j = i + 1; j < MAXCARS; j++) {
            if (!cars[j].used || cars[j].dead) continue;
            Car &a = cars[i], &b = cars[j];
            if (a.kind == 0 && b.kind == 0) continue;
            float dx = a.x - b.x, dz = a.z - b.z, d = sqrtf(dx * dx + dz * dz);
            if (d >= 4.2f || d < 0.001f) continue;
            float nx = dx / d, nz = dz / d, push = (4.2f - d) * 0.5f;
            a.x += nx * push; a.z += nz * push; b.x -= nx * push; b.z -= nz * push;
            float vax = sinf(a.heading) * a.speed, vaz = cosf(a.heading) * a.speed;
            float vbx = sinf(b.heading) * b.speed, vbz = cosf(b.heading) * b.speed;
            float closing = (vbx - vax) * nx + (vbz - vaz) * nz;
            if (closing > 2.f && a.cool <= 0 && b.cool <= 0) {
                a.cool = b.cool = 0.3f;
                bool pa = (i == g.driving), pb = (j == g.driving);
                damageCar(i, closing * 1.6f, pb);
                damageCar(j, closing * 1.6f, pa);
                a.speed *= 0.65f; b.speed *= 0.65f;
                burst((a.x + b.x) * 0.5f, 1.f, (a.z + b.z) * 0.5f, 6, 1.f, 0.8f, 0.3f, 4.f);
                if (pa || pb) {
                    Car& o = pa ? b : a;
                    g.health -= closing * 0.25f;
                    if (o.kind == 0) { o.kind = 3; if (closing > 4.f) addHeat(8.f); }
                    if (o.kind == 1) addHeat(5.f);
                }
                if ((a.kind == 1 && pb) || (b.kind == 1 && pa)) g.health -= closing * 0.6f;
            }
        }
    }
    // --- peds
    for (int i = 0; i < MAXPEDS; i++) {
        Ped& p = peds[i];
        if (!p.alive) { p.timer -= dt; if (p.timer <= 0) spawnPed(p, false); continue; }
        p.t -= dt;
        float speed = 1.3f;
        if (p.fear > 0) { p.fear -= dt; speed = 4.8f; p.heading = atan2f(p.x - g.x, p.z - g.z); }
        else if (p.t <= 0) { p.heading = frange(-PI, PI); p.t = frange(2.f, 6.f); }
        p.x += sinf(p.heading) * speed * dt; p.z += cosf(p.heading) * speed * dt;
        p.phase += speed * 2.2f * dt;
        pushOut(p.x, p.z, 0.5f);
        p.x = clampf(p.x, -HALF - 10, HALF + 10); p.z = clampf(p.z, -HALF - 10, HALF + 10);
        for (int j = 0; j < MAXCARS; j++) {  // run over
            const Car& c = cars[j];
            if (!c.used || c.dead || fabsf(c.speed) < 4.f) continue;
            float d = dist2(c.x, c.z, p.x, p.z);
            if (d < 10.f && fabsf(c.speed) > 8.f) p.fear = 3.f;
            if (d < 2.3f) { killPed(p, j == g.driving); break; }
        }
    }
    // --- particles
    for (size_t i = 0; i < parts.size();) {
        Particle& p = parts[i];
        p.life -= dt; p.vy -= 9.f * dt;
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        if (p.y < 0.05f) { p.y = 0.05f; p.vy = 0; p.vx *= 0.8f; p.vz *= 0.8f; }
        if (p.life <= 0) { parts[i] = parts.back(); parts.pop_back(); } else i++;
    }
    // --- police + traffic population
    int st = stars();
    if (st > 0 && st > g.prevStars) toast(std::string("WANTED LEVEL ") + std::string(st, '*'));
    if (st == 0 && g.prevStars > 0) toast("You lost the cops.");
    g.prevStars = st;
    int cops = 0, traf = 0;
    float nearCop = 1e9f;
    for (int i = 0; i < MAXCARS; i++)
        if (cars[i].used && !cars[i].dead) {
            if (cars[i].kind == 1) { cops++; nearCop = std::min(nearCop, dist2(cars[i].x, cars[i].z, g.x, g.z)); }
            if (cars[i].kind == 0) traf++;
        }
    g.copCool -= dt; g.trafCool -= dt;
    g_nearCop = nearCop;
    if (st > 0 && cops < std::min(10, st * 2) && g.copCool <= 0) { spawnPolice(); g.copCool = 1.5f; }
    if (traf < 30 && g.trafCool <= 0) { spawnTraffic(true); g.trafCool = 0.7f; }
    if (g.heat > 0) g.heat = std::max(0.f, g.heat - ((nearCop < 45.f) ? 0.4f : 2.5f) * dt);
}

// ----------------------------------------------------------- player/update --
static const char* PED_LINES[] = {"Nice day, huh?", "Watch where you are going!", "Did you see the cops earlier?", "I love this city.",
                                  "Bills, bills, bills...", "Have you tried the burgers at the supermarket?", "Careful on the roads tonight.", "Leave me alone, man."};
static void talkToNpc() {
    for (int i = 0; i < 2; i++)
        if (dist2(friends[i].x, friends[i].z, g.x, g.z) < 5.f) {
            if (storyStep >= NSTEPS) toast(std::string(friends[i].name) + ": We run this town now. Keep an eye on the bills!");
            else toast(std::string(friends[i].name) + ": Hit J and call me if you need a hint.");
            return;
        }
    int best = -1; float bd = 3.5f;
    for (int i = 0; i < MAXPEDS; i++)
        if (peds[i].alive) { float d = dist2(peds[i].x, peds[i].z, g.x, g.z); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) toast(std::string("Pedestrian: \"") + PED_LINES[rand() % 8] + "\"");
}

static void updatePlayer(float dt) {
    bool fw = kd('W') || kd(VK_UP), bk = kd('S') || kd(VK_DOWN), lf = kd('A') || kd(VK_LEFT), rt = kd('D') || kd(VK_RIGHT);
    if (g.shotCool > 0) g.shotCool -= dt;
    if (g.tracerT > 0) g.tracerT -= dt;
    if (g.swapCool > 0) g.swapCool -= dt;
    if (kt('Q')) swapCharacter();
    if (g.driving < 0) {
        g.heading += ((lf ? 1.f : 0.f) - (rt ? 1.f : 0.f)) * 2.6f * dt;
        float sp = kd(VK_SHIFT) ? 9.5f : 5.5f, mv = ((fw ? 1.f : 0.f) - (bk ? 0.6f : 0.f)) * sp;
        g.x += sinf(g.heading) * mv * dt; g.z += cosf(g.heading) * mv * dt;
        g.walk += fabsf(mv) * 1.3f * dt;
        pushOut(g.x, g.z, 0.6f);
        g.x = clampf(g.x, -HALF - 14, HALF + 14); g.z = clampf(g.z, -HALF - 14, HALF + 14);
        if (kd(VK_SPACE) && g.ammo > 0 && g.shotCool <= 0) shoot();
        if (kt('F')) {
            int best = -1; float bd = 5.5f;
            for (int i = 0; i < MAXCARS; i++) {
                if (!cars[i].used || cars[i].dead || cars[i].kind == 1) continue;
                float d = dist2(cars[i].x, cars[i].z, g.x, g.z);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) {
                if (cars[best].kind != 2) { addHeat(10.f); toast("Carjacked!"); }
                cars[best].kind = 2; cars[best].speed *= 0.5f; g.driving = best;
                au::play(au::SFX_DOOR);
            }
        }
    } else {
        Car& c = cars[g.driving];
        g.x = c.x; g.z = c.z; g.heading = c.heading;
        if (kt('H')) {
            au::play(au::SFX_HORN, 0.7f);
            for (int i = 0; i < MAXPEDS; i++) if (peds[i].alive && dist2(peds[i].x, peds[i].z, c.x, c.z) < 18.f) peds[i].fear = 2.f;
        }
        if (kt('F') && fabsf(c.speed) < 9.f) {
            g.x = c.x + cosf(c.heading) * 2.6f; g.z = c.z - sinf(c.heading) * 2.6f;
            g.driving = -1;
            au::play(au::SFX_DOOR);
        }
    }
    int si = nearestSpot(5.5f);
    if (kt('E')) { if (si >= 0) doInteract(si); else talkToNpc(); }
    // job delivery
    if (g.job.active) {
        g.job.time -= dt;
        if (g.driving >= 0 && dist2(g.job.x, g.job.z, g.x, g.z) < 6.f) {
            g.money += g.job.reward; au::play(au::SFX_CASH); toast("Delivered! +$" + std::to_string(g.job.reward)); g.job.active = false;
        } else if (g.job.time <= 0) { g.job.active = false; toast("Job failed: out of time."); }
    }
    // bills
    g.billTimer += dt;
    if (g.billTimer >= 75.f) {
        g.billTimer = 0; g.billsDue += 100.f;
        toast("New bill: $100. Pay it at the BILLS OFFICE.");
    }
    if (g.billsDue >= 600.f) g.state = 2;
    // damage flash / death
    if (g.health < g.lastHealth) g.hurt = 0.3f;
    g.lastHealth = g.health;
    if (g.health <= 0 && g.state == 0) { g.state = 1; g.deadTimer = 3.5f; g.health = 0; g.driving = -1; }
}

static void audioFrame() {
    au::Mixer& m = au::M();
    float ev = 0, pitch = 0;
    if (scene == 1 && g.driving >= 0 && g.state == 0 && !g.paused) {
        ev = 0.5f; pitch = clampf(fabsf(cars[g.driving].speed) / 36.f, 0.f, 1.f);
    }
    m.engineVol = ev; m.enginePitch = pitch;
    float sv = 0;
    if (scene == 1 && stars() > 0 && g_nearCop < 170.f && !g.paused) sv = clampf(1.f - g_nearCop / 170.f, 0.f, 1.f) * 0.9f;
    m.sirenVol = sv;
    m.musicVol = (phone.callWho >= 0) ? 0.10f : 0.32f;
}

static void update(float dt) {
    g_dt = dt;
    if (scene != 1) {  // main menu / multiplayer menu: keep the city alive in the background
        menuTime += dt;
        g.clock += dt * 2.f;
        updateWorld(dt);
        if (tapped(VK_ESCAPE)) { if (scene == 2) { scene = 0; menuMsg.clear(); } else quit(); }
        audioFrame();
        return;
    }
    if (tapped(VK_ESCAPE)) {
        if (phone.open) phone.open = false;
        else if (mapOpen) mapOpen = false;
        else { net::stop(); scene = 0; menuMsg.clear(); audioFrame(); return; }
    }
    if (tapped('J') && g.state != 2) { phone.open = !phone.open; phone.app = -1; phone.callWho = -1; mapOpen = false; au::play(au::SFX_CLICK, 0.7f); }
    if (tapped('M') && !phone.open) { mapOpen = !mapOpen; au::play(au::SFX_CLICK, 0.7f); }
    if (tapped('P') && g.state == 0 && !inputLocked()) g.paused = !g.paused;
    if (g.state == 2 && tapped('R')) { resetGame(); return; }
    if (g.paused) { audioFrame(); return; }
    g.clock += dt; g.uptime += dt;
    if (g.hurt > 0) g.hurt -= dt;
    for (size_t i = 0; i < toasts.size();) { toasts[i].t -= dt; if (toasts[i].t <= 0) toasts.erase(toasts.begin() + i); else i++; }
    if (phone.callWho >= 0) phone.callT += dt;
    if (dlg.active) {  // the world holds still while people talk
        if (tapped('E') || tapped(VK_RETURN) || tapped(VK_SPACE) || mouseClicked()) {
            au::play(au::SFX_CLICK, 0.6f);
            if (++dlg.idx >= dlg.lines.size()) dlg.active = false;
        }
        audioFrame();
        return;
    }
    if (g.state == 0) { updatePlayer(dt); if (g.state == 0) updateStory(dt); }
    else if (g.state == 1) { g.deadTimer -= dt; if (g.deadTimer <= 0) respawnPlayer(); }
    if (g.state != 2) updateWorld(dt);
    g.camH += wrapAngle(g.heading - g.camH) * std::min(1.f, 4.f * dt);
    // multiplayer presence
    if (net::N().active) {
        net::Packet pk;
        memset(&pk, 0, sizeof(pk));
        pk.ch = (uint8_t)cur;
        if (g.driving >= 0) {
            const Car& c = cars[g.driving];
            pk.inCar = 1; pk.carType = (uint8_t)c.type; pk.x = c.x; pk.z = c.z; pk.heading = c.heading; pk.speed = c.speed;
            pk.cr = c.r; pk.cg = c.g; pk.cb = c.b;
        } else { pk.x = g.x; pk.z = g.z; pk.heading = g.heading; pk.phase = g.walk * 4.f; }
        net::tick(dt, pk);
    }
    audioFrame();
}

// ------------------------------------------------------------------- UI ----
static std::string clockStr() {
    float p = fmodf(g.clock / DAY_LEN, 1.f), hour = fmodf(6.f + p * 24.f, 24.f);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", (int)hour, (int)((hour - floorf(hour)) * 60.f));
    return b;
}
static std::vector<std::string> wrapText(const std::string& s, int cols) {
    std::vector<std::string> out;
    std::string line, word;
    for (size_t i = 0; i <= s.size(); i++) {
        char c = i < s.size() ? s[i] : ' ';
        if (c == ' ') {
            if (line.size() + word.size() + (line.empty() ? 0 : 1) > (size_t)cols) { if (!line.empty()) out.push_back(line); line = word; }
            else line += (line.empty() ? "" : " ") + word;
            word.clear();
        } else word += c;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}
static bool uiBtn(float x, float y, float w, float h, const std::string& label, float r, float gg, float b, float a = 1.f) {
    bool hov = inRect(mouseX(), mouseY(), x, y, w, h);
    float k = hov ? 1.25f : 1.f;
    roundRect(x, y, w, h, 9, std::min(1.f, r * k), std::min(1.f, gg * k), std::min(1.f, b * k), a);
    text(x + (w - textWidth(label)) * 0.5f, y + h * 0.5f + 5.f, label, 1, 1, 1);
    bool click = hov && mouseClicked();
    if (click) au::play(au::SFX_CLICK, 0.7f);
    return click;
}
static bool menuButton(int id, float cx, float cy, const char* label) {
    float w = 380, h = 62, x = cx - w / 2, y = cy - h / 2;
    bool hov = inRect(mouseX(), mouseY(), x, y, w, h);
    float prev = hoverT[id];
    hoverT[id] += ((hov ? 1.f : 0.f) - hoverT[id]) * std::min(1.f, 14.f * g_dt);
    if (hov && prev < 0.05f) au::play(au::SFX_HOVER);
    float k = hoverT[id];
    roundRect(x - 6 - 30 * k, y - 4 - 9 * k, w + 12 + 60 * k, h + 8 + 18 * k, 18 + 6 * k, 0.10f, 0.45f, 1.0f, 0.55f + 0.4f * k);  // blue shape grows on hover
    roundRect(x, y, w, h, 14, 0.07f, 0.08f, 0.11f, 0.95f);
    std::string l = label;
    textMid(cx - textWidthMid(l) * 0.5f, cy + 9.f, l, 1, 1, 1);
    bool click = hov && mouseClicked();
    if (click) au::play(au::SFX_CLICK);
    return click;
}

static void drawMenu(float W, float H) {
    rect(0, 0, W, H, 0.0f, 0.0f, 0.04f, 0.45f);
    const std::string title = "LITE CITY";
    float tx = W * 0.5f - textWidthBig(title) * 0.5f;
    textBig(tx + 3, H * 0.19f + 3, title, 0.0f, 0.0f, 0.1f);
    textBig(tx, H * 0.19f, title, 1.f, 1.f, 1.f);
    std::string sub = "built on LiteEngine  -  beta v0.2";
    text(W * 0.5f - textWidth(sub) * 0.5f, H * 0.19f + 28, sub, 0.7f, 0.8f, 1.f);
    if (scene == 0) {
        if (menuButton(0, W * 0.5f, H * 0.42f, "SINGLE PLAYER")) { resetGame(); scene = 1; }
        if (menuButton(1, W * 0.5f, H * 0.42f + 84, "MULTIPLAYER")) { scene = 2; menuMsg.clear(); }
        if (menuButton(2, W * 0.5f, H * 0.42f + 168, "QUIT")) quit();
        text(W * 0.5f - 130, H - 26, "In game: J phone   M map   Q switch character", 0.8f, 0.8f, 0.8f);
    } else {
        std::string lab = "Host IP (for JOIN):";
        text(W * 0.5f - 190, H * 0.33f, lab, 0.8f, 0.9f, 1.f);
        roundRect(W * 0.5f - 190, H * 0.33f + 8, 380, 42, 10, 0.05f, 0.06f, 0.09f, 0.95f);
        bool blink = fmodf(menuTime, 1.f) < 0.5f;
        textMid(W * 0.5f - 178, H * 0.33f + 38, ipBuf + (blink ? "_" : ""), 1, 1, 1);
        for (char c : plat().typed) if ((c >= '0' && c <= '9') || c == '.') { if (ipBuf.size() < 15) ipBuf.push_back(c); }
        if (tapped(VK_BACK) && !ipBuf.empty()) ipBuf.pop_back();
        if (menuButton(3, W * 0.5f, H * 0.33f + 118, "HOST GAME")) {
            if (net::open(true, "")) { resetGame(); scene = 1; } else menuMsg = net::N().status;
        }
        if (menuButton(4, W * 0.5f, H * 0.33f + 202, "JOIN GAME")) {
            if (net::open(false, ipBuf)) { resetGame(); scene = 1; } else menuMsg = net::N().status;
        }
        if (menuButton(5, W * 0.5f, H * 0.33f + 286, "BACK")) { scene = 0; menuMsg.clear(); }
        if (!menuMsg.empty()) text(W * 0.5f - textWidth(menuMsg) * 0.5f, H - 50, menuMsg, 1.f, 0.5f, 0.4f);
        std::string hint = "Both players run their own city and see each other. Host: share your IP.";
        text(W * 0.5f - textWidth(hint) * 0.5f, H - 24, hint, 0.7f, 0.7f, 0.7f);
    }
}

static int unreadCount() { int n = 0; for (const SMS& m : inbox) if (!m.read) n++; return n; }

static const char* PHONE_APPS[4] = {"LP Media", "Messages", "Calls", "G Files"};
static void drawPhone(float W, float H) {
    float ph = std::min(620.f, H - 30.f), pw = ph * 0.54f, x = W - pw - 24.f, y = (H - ph) * 0.5f;
    roundRect(x - 8, y - 8, pw + 16, ph + 16, 36, 0.02f, 0.02f, 0.03f);
    roundRect(x, y, pw, ph, 30, 0.08f, 0.09f, 0.13f);
    text(x + 20, y + 26, clockStr(), 1, 1, 1);
    text(x + pw - 100, y + 26, "LP-NET 5G", 0.6f, 0.8f, 1.f);
    float top = y + 44;
    static const float COL[4][3] = {{0.55f, 0.3f, 0.9f}, {0.2f, 0.72f, 0.35f}, {0.2f, 0.5f, 0.95f}, {0.95f, 0.6f, 0.15f}};
    if (phone.app < 0) {
        float isz = (pw - 60) / 2;
        for (int i = 0; i < 4; i++) {
            float ix = x + 20 + (i % 2) * (isz + 20), iy = top + 40 + (i / 2) * (isz + 50);
            bool hov = inRect(mouseX(), mouseY(), ix, iy, isz, isz);
            float k = hov ? 1.2f : 1.f;
            roundRect(ix, iy, isz, isz, 24, std::min(1.f, COL[i][0] * k), std::min(1.f, COL[i][1] * k), std::min(1.f, COL[i][2] * k));
            std::string ini(1, PHONE_APPS[i][0]);
            textBig(ix + isz * 0.5f - 14, iy + isz * 0.5f + 18, ini, 1, 1, 1);
            text(ix + (isz - textWidth(PHONE_APPS[i])) * 0.5f, iy + isz + 20, PHONE_APPS[i], 1, 1, 1);
            if (i == 1 && unreadCount() > 0) {
                roundRect(ix + isz - 22, iy - 8, 32, 24, 11, 0.95f, 0.15f, 0.15f);
                text(ix + isz - 11, iy + 9, std::to_string(unreadCount()), 1, 1, 1);
            }
            if (hov && mouseClicked()) {
                phone.app = i; phone.tab = 0; phone.callWho = -1; phone.note.clear();
                au::play(au::SFX_CLICK, 0.7f);
                if (i == 1) for (SMS& m : inbox) m.read = true;
            }
        }
        text(x + 20, y + ph - 22, "J or ESC: close phone", 0.6f, 0.6f, 0.7f);
        return;
    }
    int a = phone.app;
    roundRect(x + 12, top, pw - 24, 34, 10, COL[a][0], COL[a][1], COL[a][2]);
    textMid(x + 24, top + 26, PHONE_APPS[a], 1, 1, 1);
    float cy = top + 52, left = x + 16, wide = pw - 32;
    if (a == 0) {  // ---------------- LP Media
        int st = au::M().station;
        roundRect(left, cy, wide, 92, 12, 0.14f, 0.12f, 0.22f);
        if (st == 0) text(left + 14, cy + 40, "Radio is off", 0.8f, 0.8f, 0.9f);
        else {
            text(left + 14, cy + 26, "NOW PLAYING", 0.6f, 0.7f, 1.f);
            textMid(left + 14, cy + 56, au::STATIONS[st - 1].name, 1, 1, 1);
            text(left + 14, cy + 80, au::STATIONS[st - 1].track, 0.8f, 0.8f, 0.9f);
        }
        const char* names[4] = {"OFF", "LITE FM", "NIGHT DRIVE", "CHILL ROADS"};
        for (int i = 0; i < 4; i++) {
            bool sel = (st == i);
            if (uiBtn(left, cy + 106 + i * 46, wide, 38, names[i], sel ? 0.55f : 0.2f, sel ? 0.3f : 0.2f, sel ? 0.9f : 0.3f)) au::M().station = i;
        }
        float vy = cy + 106 + 4 * 46 + 8;
        text(left, vy + 14, "VOLUME", 1, 1, 1);
        float master = au::M().master;
        if (uiBtn(left + 80, vy - 4, 36, 28, "-", 0.3f, 0.3f, 0.4f)) au::M().master = clampf(master - 0.1f, 0.05f, 1.f);
        if (uiBtn(left + wide - 36, vy - 4, 36, 28, "+", 0.3f, 0.3f, 0.4f)) au::M().master = clampf(master + 0.1f, 0.05f, 1.f);
        rect(left + 124, vy + 4, wide - 170, 12, 0.2f, 0.2f, 0.25f); rect(left + 124, vy + 4, (wide - 170) * master, 12, 0.6f, 0.4f, 1.f);
    } else if (a == 1) {  // ---------------- Messages
        if (inbox.empty()) text(left, cy + 20, "No messages yet.", 0.8f, 0.8f, 0.8f);
        float yy = cy;
        for (int i = (int)inbox.size() - 1; i >= 0; i--) {
            auto lines = wrapText(inbox[i].text, (int)(wide / 8.f) - 3);
            float hh = 34 + lines.size() * 17;
            if (yy + hh > y + ph - 64) break;
            roundRect(left, yy, wide, hh, 10, 0.16f, 0.2f, 0.17f);
            text(left + 10, yy + 20, inbox[i].from, 0.5f, 1.f, 0.6f);
            for (size_t k = 0; k < lines.size(); k++) text(left + 10, yy + 38 + k * 17, lines[k], 1, 1, 1);
            yy += hh + 8;
        }
    } else if (a == 2) {  // ---------------- Calls
        static const char* WHO[3] = {"Marcus", "Jason", "Dante"};
        if (phone.callWho < 0) {
            text(left, cy + 14, "Contacts", 0.8f, 0.9f, 1.f);
            for (int i = 0; i < 3; i++)
                if (uiBtn(left, cy + 30 + i * 56, wide, 46, std::string("Call ") + WHO[i], 0.2f, 0.45f, 0.3f)) {
                    phone.callWho = i; phone.callT = 0; au::play(au::SFX_RING, 0.6f);
                }
        } else {
            textMid(left + 10, cy + 40, WHO[phone.callWho], 1, 1, 1);
            bool connected = phone.callT > 1.6f;
            text(left + 10, cy + 66, connected ? "Connected" : "Calling...", connected ? 0.4f : 0.8f, 1.f, connected ? 0.4f : 0.8f);
            if (connected) {
                std::string reply;
                if (phone.callWho == 0) reply = storyStep < NSTEPS ? std::string("Hint: ") + STEPS[storyStep].obj : "All quiet. Take a job and pay your bills.";
                else if (phone.callWho == 1) reply = jasonUnlocked ? "Press Q and I will cover you." : "Where are you? I am at the supermarket!";
                else reply = "Watch the cops, John. Stay off the main roads.";
                auto lines = wrapText(reply, (int)(wide / 8.f) - 2);
                for (size_t k = 0; k < lines.size(); k++) text(left + 10, cy + 110 + k * 18, lines[k], 1, 1, 1);
            }
            if (uiBtn(left, y + ph - 108, wide, 40, "HANG UP", 0.8f, 0.15f, 0.15f)) phone.callWho = -1;
        }
    } else {  // ---------------- G Files
        const char* tabs[4] = {"MISSION", "STATS", "SAVE", "HELP"};
        float tw = wide / 4.f;
        for (int i = 0; i < 4; i++)
            if (uiBtn(left + i * tw, cy, tw - 4, 30, tabs[i], phone.tab == i ? 0.85f : 0.3f, phone.tab == i ? 0.5f : 0.3f, 0.15f)) { phone.tab = i; phone.note.clear(); }
        float yy = cy + 50;
        if (phone.tab == 0) {
            text(left, yy, "CURRENT OBJECTIVE", 1.f, 0.8f, 0.3f);
            auto lines = wrapText(storyStep < NSTEPS ? STEPS[storyStep].obj : "Chapter 1 complete. Free roam!", (int)(wide / 8.f) - 1);
            for (size_t k = 0; k < lines.size(); k++) text(left, yy + 22 + k * 18, lines[k], 1, 1, 1);
            yy += 40 + lines.size() * 18;
            text(left, yy, "Completed: " + std::to_string(storyStep) + " / " + std::to_string(NSTEPS), 0.6f, 1.f, 0.6f);
            for (int i = 0; i < storyStep && i < NSTEPS; i++) {
                auto l2 = wrapText(std::string("- ") + STEPS[i].obj, (int)(wide / 8.f) - 1);
                for (size_t k = 0; k < l2.size() && yy < y + ph - 90; k++) { yy += 18; text(left, yy + 4, l2[k], 0.6f, 0.7f, 0.6f); }
            }
        } else if (phone.tab == 1) {
            char b[96];
            snprintf(b, sizeof(b), "Character : %s", chars[cur].name); text(left, yy, b, 1, 1, 1);
            snprintf(b, sizeof(b), "Cash      : $%d", g.money); text(left, yy + 24, b, 0.5f, 1.f, 0.5f);
            snprintf(b, sizeof(b), "Health    : %d", (int)g.health); text(left, yy + 48, b, 1, 1, 1);
            snprintf(b, sizeof(b), "Wanted    : %d star(s)", stars()); text(left, yy + 72, b, 1, 1, 1);
            snprintf(b, sizeof(b), "Bills due : $%d", (int)g.billsDue); text(left, yy + 96, b, 1, 0.7f, 0.5f);
            snprintf(b, sizeof(b), "Play time : %d min", (int)(g.uptime / 60.f)); text(left, yy + 120, b, 1, 1, 1);
            snprintf(b, sizeof(b), "Time      : %s", clockStr().c_str()); text(left, yy + 144, b, 1, 1, 1);
        } else if (phone.tab == 2) {
            if (uiBtn(left, yy, wide, 44, "SAVE GAME", 0.2f, 0.5f, 0.3f)) phone.note = saveGame() ? "Saved to litecity.sav" : "Save failed";
            if (uiBtn(left, yy + 56, wide, 44, "LOAD GAME", 0.2f, 0.35f, 0.6f)) phone.note = loadGame() ? "Game loaded" : "No save file found";
            if (!phone.note.empty()) text(left, yy + 128, phone.note, 1, 1, 0.6f);
        } else {
            const char* help[] = {"W/S gas or walk", "A/D steer or turn", "SHIFT sprint", "SPACE shoot / handbrake", "F enter or exit car", "E shop / job / talk",
                                  "H horn", "Q switch John/Jason", "J phone   M map", "P pause   ESC menu"};
            for (int i = 0; i < 10; i++) text(left, yy + i * 22, help[i], 1, 1, 1);
        }
    }
    if (phone.callWho < 0 || a != 2)
        if (uiBtn(x + 20, y + ph - 52, pw - 40, 36, "< HOME", 0.25f, 0.25f, 0.35f)) { phone.app = -1; phone.callWho = -1; }
}

static void drawMap(float W, float H) {
    rect(0, 0, W, H, 0.02f, 0.03f, 0.05f, 0.9f);
    float S = std::min(W - 340.f, H - 70.f), mx = W - S - 36.f, my = (H - S) * 0.5f, ext = HALF + 40.f, sc = S / (2 * ext);
    auto MX = [&](float x) { return mx + (x + ext) * sc; };
    auto MY = [&](float z) { return my + (ext - z) * sc; };
    rect(mx - 4, my - 4, S + 8, S + 8, 0.3f, 0.36f, 0.48f);
    rect(mx, my, S, S, 0.09f, 0.09f, 0.11f);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) rect(MX(blockC(i) - 24), MY(blockC(j) + 24), 48 * sc, 48 * sc, 0.25f, 0.28f, 0.3f);
    for (const Spot& s : spots) {
        rect(MX(s.x - 18), MY(s.z + (s.type == S_GYM ? 3.f : 18.f)), 36 * sc, 21 * sc, s.r * 0.8f, s.g * 0.8f, s.b * 0.8f);
    }
    for (const Spot& s : spots) {
        rect(MX(s.x) - 5, MY(s.z) - 5, 10, 10, 0, 0, 0);
        rect(MX(s.x) - 4, MY(s.z) - 4, 8, 8, s.r, s.g, s.b);
        text(MX(s.x) + 9, MY(s.z) + 4, s.name, 1, 1, 1);
    }
    float tx, tz;
    if (storyTarget(tx, tz)) {
        rect(MX(tx) - 7, MY(tz) - 7, 14, 14, 0, 0, 0); rect(MX(tx) - 5, MY(tz) - 5, 10, 10, 1.f, 0.9f, 0.1f);
        text(MX(tx) + 10, MY(tz) - 8, "OBJECTIVE", 1.f, 0.9f, 0.2f);
    }
    if (g.job.active) { rect(MX(g.job.x) - 5, MY(g.job.z) - 5, 10, 10, 0.2f, 1.f, 1.f); text(MX(g.job.x) + 9, MY(g.job.z), "DELIVERY", 0.3f, 1.f, 1.f); }
    if (wpOn) { rect(MX(wpx) - 5, MY(wpz) - 5, 10, 10, 1.f, 0.2f, 0.9f); text(MX(wpx) + 9, MY(wpz), "WAYPOINT", 1.f, 0.4f, 0.9f); }
    for (int i = 0; i < MAXCARS; i++)
        if (cars[i].used && !cars[i].dead && cars[i].kind == 1) {
            bool bl = sinf(g.uptime * 10.f) > 0;
            rect(MX(cars[i].x) - 3, MY(cars[i].z) - 3, 6, 6, bl ? 1.f : 0.2f, 0.2f, bl ? 0.2f : 1.f);
        }
    if (chars[1 - cur].driving < 0 && (jasonUnlocked || cur == 1)) {
        rect(MX(chars[1 - cur].x) - 4, MY(chars[1 - cur].z) - 4, 8, 8, 0.4f, 0.7f, 1.f);
        text(MX(chars[1 - cur].x) + 8, MY(chars[1 - cur].z) + 4, chars[1 - cur].name, 0.5f, 0.8f, 1.f);
    }
    if (net::N().active && net::N().remote.seen) {
        rect(MX(net::N().remote.x) - 4, MY(net::N().remote.z) - 4, 8, 8, 0.3f, 1.f, 0.3f);
        text(MX(net::N().remote.x) + 8, MY(net::N().remote.z) + 4, "PLAYER 2", 0.4f, 1.f, 0.4f);
    }
    {  // you (arrow)
        float px = MX(g.x), py = MY(g.z), fx = sinf(g.heading), fy = -cosf(g.heading), sx = fy * -1.f, sy = fx;
        glColor3f(1.f, 0.95f, 0.1f);
        glBegin(GL_TRIANGLES);
        glVertex2f(px + fx * 11, py + fy * 11);
        glVertex2f(px - fx * 6 + sx * 7, py - fy * 6 + sy * 7);
        glVertex2f(px - fx * 6 - sx * 7, py - fy * 6 - sy * 7);
        glEnd();
    }
    // legend
    roundRect(24, my, 270, S, 14, 0.06f, 0.07f, 0.1f, 0.95f);
    textMid(40, my + 36, "CITY MAP", 1, 1, 1);
    text(40, my + 62, "M or ESC: close", 0.7f, 0.7f, 0.8f);
    text(40, my + 86, "Click the map: set waypoint", 1.f, 0.6f, 0.9f);
    text(40, my + 106, "X: clear waypoint", 0.8f, 0.6f, 0.8f);
    float ly = my + 140;
    for (const Spot& s : spots) { rect(40, ly - 9, 12, 12, s.r, s.g, s.b); text(62, ly + 2, s.name, 1, 1, 1); ly += 22; }
    rect(40, ly - 9, 12, 12, 1.f, 0.9f, 0.1f); text(62, ly + 2, "Story objective", 1, 1, 1); ly += 22;
    rect(40, ly - 9, 12, 12, 0.2f, 1.f, 1.f); text(62, ly + 2, "Delivery job", 1, 1, 1); ly += 22;
    rect(40, ly - 9, 12, 12, 1.f, 0.2f, 0.2f); text(62, ly + 2, "Police", 1, 1, 1); ly += 22;
    if (mouseClicked() && inRect(mouseX(), mouseY(), mx, my, S, S)) {
        wpx = (mouseX() - mx) / sc - ext; wpz = ext - (mouseY() - my) / sc; wpOn = true;
        toast("Waypoint set"); au::play(au::SFX_CLICK, 0.8f);
    }
    if (tapped('X')) { wpOn = false; toast("Waypoint cleared"); }
}

static void drawMinimap(float W) {
    float S = 210, mx = W - S - 12, my = 12, R = 150.f, sc = S / (2 * R);
    scissor(mx, my, S, S);
    rect(mx, my, S, S, 0.07f, 0.08f, 0.1f, 0.9f);
    auto MX = [&](float x) { return mx + (x - g.x + R) * sc; };
    auto MY = [&](float z) { return my + (R - (z - g.z)) * sc; };
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            if (fabsf(blockC(i) - g.x) > R + 30 || fabsf(blockC(j) - g.z) > R + 30) continue;
            rect(MX(blockC(i) - 24), MY(blockC(j) + 24), 48 * sc, 48 * sc, 0.27f, 0.3f, 0.32f);
        }
    for (const Spot& s : spots) rect(MX(s.x) - 4, MY(s.z) - 4, 8, 8, s.r, s.g, s.b);
    float tx, tz;
    float pulse = 0.5f + 0.5f * sinf(g.uptime * 4.f);
    auto edgeMark = [&](float wx, float wz, float cr, float cg, float cb) {  // objective marker, pinned to the edge when far away
        float dx = (wx - g.x) * sc, dz = -(wz - g.z) * sc, m = S * 0.5f - 6.f;
        float k = std::max(fabsf(dx), fabsf(dz));
        if (k > m) { dx *= m / k; dz *= m / k; }
        rect(mx + S * 0.5f + dx - 5, my + S * 0.5f + dz - 5, 10, 10, cr, cg, cb, 0.5f + 0.5f * pulse);
    };
    if (storyTarget(tx, tz)) edgeMark(tx, tz, 1.f, 0.9f, 0.1f);
    if (g.job.active) edgeMark(g.job.x, g.job.z, 0.2f, 1.f, 1.f);
    if (wpOn) edgeMark(wpx, wpz, 1.f, 0.2f, 0.9f);
    for (int i = 0; i < MAXCARS; i++)
        if (cars[i].used && !cars[i].dead && cars[i].kind == 1) {
            bool bl = sinf(g.uptime * 10.f) > 0;
            rect(MX(cars[i].x) - 2.5f, MY(cars[i].z) - 2.5f, 5, 5, bl ? 1.f : 0.2f, 0.2f, bl ? 0.2f : 1.f);
        }
    if (net::N().active && net::N().remote.seen) rect(MX(net::N().remote.x) - 3, MY(net::N().remote.z) - 3, 6, 6, 0.3f, 1.f, 0.3f);
    float px = mx + S * 0.5f, py = my + S * 0.5f, fx = sinf(g.heading), fy = -cosf(g.heading), sx = -fy, sy = fx;
    glColor3f(1.f, 0.95f, 0.1f);
    glBegin(GL_TRIANGLES);
    glVertex2f(px + fx * 9, py + fy * 9); glVertex2f(px - fx * 5 + sx * 5.5f, py - fy * 5 + sy * 5.5f); glVertex2f(px - fx * 5 - sx * 5.5f, py - fy * 5 - sy * 5.5f);
    glEnd();
    noScissor();
    rect(mx - 2, my - 2, S + 4, 2, 0.3f, 0.4f, 0.6f); rect(mx - 2, my + S, S + 4, 2, 0.3f, 0.4f, 0.6f);
    rect(mx - 2, my, 2, S, 0.3f, 0.4f, 0.6f); rect(mx + S, my, 2, S, 0.3f, 0.4f, 0.6f);
}

static void drawDialog(float W, float H) {
    if (!dlg.active || dlg.idx >= dlg.lines.size()) return;
    const DLine& l = dlg.lines[dlg.idx];
    float bw = std::min(W - 80.f, 860.f), bh = 150, bx = (W - bw) / 2, by = H - bh - 40;
    roundRect(bx, by, bw, bh, 16, 0.04f, 0.05f, 0.08f, 0.92f);
    bool john = std::string(l.who) == "John", jason = std::string(l.who) == "Jason";
    float r = john ? 0.4f : jason ? 1.f : 1.f, gg = john ? 0.7f : jason ? 0.4f : 0.85f, b = john ? 1.f : jason ? 0.4f : 0.3f;
    textMid(bx + 24, by + 38, l.who, r, gg, b);
    auto lines = wrapText(l.text, (int)((bw - 60) / 9.f));
    for (size_t k = 0; k < lines.size(); k++) text(bx + 24, by + 74 + k * 22, lines[k], 1, 1, 1);
    text(bx + bw - 200, by + bh - 14, "[E] / click: continue", 0.7f, 0.7f, 0.8f);
}

// --------------------------------------------------------------- render ----
static float groundY(float x, float z) {  // sidewalk slabs are slightly raised
    float u = (x + HALF) / PITCH, v = (z + HALF) / PITCH;
    if (u < 0 || v < 0 || u > N || v > N) return 0.f;
    float fu = u - floorf(u), fv = v - floorf(v);
    return (fabsf(fu - 0.5f) < 0.387f && fabsf(fv - 0.5f) < 0.387f) ? 0.15f : 0.f;
}
static void drawPersonAt(float x, float z, float h, const gfx::PersonLook& look, float phase, bool armed, float moving) {
    gfx::PersonLook L = look;
    L.phase = phase; L.armed = armed; L.moving = moving;
    glPushMatrix();
    glTranslatef(x, groundY(x, z), z);
    glRotatef(h * 180.f / PI, 0, 1, 0);
    gfx::drawPerson(L);
    glPopMatrix();
}
static void drawCarObj(const Car& c, bool night, float t, bool alarm) {
    gfx::CarLook L;
    L.type = c.type; L.r = c.r; L.g = c.g; L.b = c.b;
    L.police = (c.kind == 1); L.dead = c.dead; L.lights = night; L.siren = alarm; L.t = t;
    glPushMatrix();
    glTranslatef(c.x, groundY(c.x, c.z), c.z);
    glRotatef(c.heading * 180.f / PI, 0, 1, 0);
    gfx::drawCar(L);
    glPopMatrix();
}

struct Label { float x, y; std::string s; float r, g, b; };

static void render() {
    Platform& pl = plat();
    float W = (float)pl.w, H = (float)pl.h;
    bool playing = (scene == 1);
    float clk = g.clock;
    float p = fmodf(clk / DAY_LEN, 1.f);
    float sunH = sinf(p * 2.f * PI);
    float day = clampf(sunH * 1.8f + 0.4f, 0.f, 1.f);
    float dusk = clampf(1.f - fabsf(sunH) * 4.f, 0.f, 1.f);
    float sky[4] = {lerpf(0.02f, 0.55f, day) + dusk * 0.35f, lerpf(0.03f, 0.75f, day) + dusk * 0.12f, lerpf(0.08f, 0.95f, day), 1.f};
    for (int i = 0; i < 3; i++) sky[i] = clampf(sky[i], 0.f, 1.f);
    glClearColor(sky[0], sky[1], sky[2], 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    begin3D(62.f, 0.3f, 900.f);

    // ---- camera
    bool inCar = playing && g.driving >= 0;
    float spd = inCar ? fabsf(cars[g.driving].speed) : 0.f;
    float ex, ey, ez, lx, ly, lz;
    if (!playing) {
        float a = menuTime * 0.06f;
        ex = sinf(a) * 300.f; ez = cosf(a) * 300.f; ey = 95.f; lx = 0; ly = 14.f; lz = 0;
    } else {
        float dist = inCar ? 10.5f + spd * 0.12f : 6.5f, hgt = inCar ? 4.6f : 3.4f, ch = g.camH;
        ex = g.x - sinf(ch) * dist; ez = g.z - cosf(ch) * dist; ey = hgt;
        lx = g.x + sinf(ch) * 4.f; ly = 1.6f; lz = g.z + cosf(ch) * 4.f;
    }
    gluLookAt(ex, ey, ez, lx, ly, lz, 0, 1, 0);

    // ---- sky, lights, fog
    float sunDir[3] = {0.4f, 0.25f + 0.75f * std::max(sunH, 0.f), 0.5f};
    glPushMatrix(); glTranslatef(ex, ey, ez); gfx::drawSky(sky, day, sunDir, clk); glPopMatrix();
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    GLfloat lpos[4] = {sunDir[0], sunDir[1], sunDir[2], 0.f};
    GLfloat ldif[4] = {lerpf(0.20f, 1.0f, day) + dusk * 0.1f, lerpf(0.22f, 0.95f, day), lerpf(0.40f, 0.85f, day), 1.f};
    GLfloat lspc[4] = {day * 0.85f, day * 0.82f, day * 0.7f, 1.f};
    GLfloat lamb[4] = {0, 0, 0, 1};
    GLfloat gamb[4] = {lerpf(0.10f, 0.38f, day), lerpf(0.11f, 0.40f, day), lerpf(0.20f, 0.45f, day), 1.f};
    glLightfv(GL_LIGHT0, GL_POSITION, lpos); glLightfv(GL_LIGHT0, GL_DIFFUSE, ldif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lspc); glLightfv(GL_LIGHT0, GL_AMBIENT, lamb);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gamb);
    glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogfv(GL_FOG_COLOR, sky);
    glFogf(GL_FOG_START, lerpf(90.f, 220.f, day)); glFogf(GL_FOG_END, lerpf(380.f, 700.f, day));

    glCallList(cityList);

    // ---- stencil shadows (buildings, vehicles, people)
    {
        float sly = 0.4f + 0.6f * std::max(sunH, 0.f);
        gfx::shadowBegin(sunDir[0], sly, sunDir[2], 0.16f, 0.10f + 0.28f * day);
        for (const Building& b : blds) {
            if (dist2(b.x, b.z, ex, ez) > 230.f) continue;
            gfx::shadowPush(); box(b.x, b.h * 0.5f, b.z, b.hx * 2, b.h, b.hz * 2); gfx::shadowPop();
        }
        for (int i = 0; i < MAXCARS; i++) {
            const Car& c = cars[i];
            if (!c.used || dist2(c.x, c.z, ex, ez) > 160.f) continue;
            gfx::shadowPush(); glTranslatef(c.x, 0, c.z); glRotatef(c.heading * 180.f / PI, 0, 1, 0);
            box(0, 0.9f, 0, 2.0f, 1.3f, 4.5f); gfx::shadowPop();
        }
        for (int i = 0; i < MAXPEDS; i++) {
            if (!peds[i].alive || dist2(peds[i].x, peds[i].z, ex, ez) > 90.f) continue;
            gfx::shadowPush(); box(peds[i].x, 0.9f, peds[i].z, 0.5f, 1.8f, 0.4f); gfx::shadowPop();
        }
        if (playing && g.driving < 0) { gfx::shadowPush(); box(g.x, 0.9f, g.z, 0.5f, 1.8f, 0.4f); gfx::shadowPop(); }
        gfx::shadowEnd();
    }

    // ---- vehicles & people
    glEnable(GL_LIGHTING);
    bool night = day < 0.55f, alarm = playing && stars() > 0;
    std::vector<Label> labels;
    for (int i = 0; i < MAXCARS; i++)
        if (cars[i].used && dist2(cars[i].x, cars[i].z, ex, ez) < 380.f) drawCarObj(cars[i], night, clk, alarm);
    for (int i = 0; i < MAXPEDS; i++)
        if (peds[i].alive && dist2(peds[i].x, peds[i].z, ex, ez) < 200.f) {
            const Ped& d = peds[i];
            gfx::PersonLook L;
            L.shirt[0] = d.r; L.shirt[1] = d.g; L.shirt[2] = d.b; L.pants[0] = d.pr; L.pants[1] = d.pg; L.pants[2] = d.pb;
            L.hair[0] = d.hr; L.hair[1] = d.hg; L.hair[2] = d.hb; L.skin = d.skin;
            drawPersonAt(d.x, d.z, d.heading, L, d.phase, false, 1.f);
        }
    if (playing || true) {
        for (int i = 0; i < 2; i++) {  // gym friends
            drawPersonAt(friends[i].x, friends[i].z, friends[i].heading, friends[i].look, 0, false, 0.f);
            float sx, sy;
            if (dist2(friends[i].x, friends[i].z, ex, ez) < 90.f && project(friends[i].x, 2.4f, friends[i].z, sx, sy))
                labels.push_back({sx, sy, friends[i].name, 1.f, 0.9f, 0.4f});
        }
        // the character you are NOT controlling
        Person& other = chars[1 - cur];
        if (other.driving < 0) {
            drawPersonAt(other.x, other.z, other.heading, other.look, 0, false, 0.f);
            float sx, sy;
            if (project(other.x, 2.4f, other.z, sx, sy)) labels.push_back({sx, sy, other.name, 0.5f, 0.8f, 1.f});
        }
    }
    if (playing && g.driving < 0 && g.state != 1) drawPersonAt(g.x, g.z, g.heading, chars[cur].look, g.walk * 4.f, g.ammo > 0, (kd('W') || kd('S') || kd(VK_UP) || kd(VK_DOWN)) ? 1.f : 0.f);
    else if (playing && g.state == 1) { glPushMatrix(); glTranslatef(g.x, 0.2f, g.z); glRotatef(90, 1, 0, 0); glColor3f(0.5f, 0.1f, 0.1f); box(0, 0, 0, 0.6f, 1.6f, 0.3f); glPopMatrix(); }
    if (playing && net::N().active && net::N().remote.seen) {  // the other human player
        const net::Remote& r = net::N().remote;
        if (r.inCar) {
            Car rc; rc.used = true; rc.type = r.carType; rc.x = r.x; rc.z = r.z; rc.heading = r.heading; rc.r = r.cr; rc.g = r.cg; rc.b = r.cb; rc.kind = 2;
            drawCarObj(rc, night, clk, false);
        } else drawPersonAt(r.x, r.z, r.heading, chars[r.ch & 1].look, r.phase, false, 1.f);
        float sx, sy;
        if (project(r.x, r.inCar ? 3.f : 2.5f, r.z, sx, sy)) labels.push_back({sx, sy, std::string("PLAYER 2 (") + chars[r.ch & 1].name + ")", 0.3f, 1.f, 0.3f});
    }

    // ---- effects (unlit)
    glDisable(GL_LIGHTING);
    float pulse = 0.5f + 0.5f * sinf(clk * 4.f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    for (const Spot& s : spots) {
        if (dist2(s.x, s.z, ex, ez) > 260.f) continue;
        glColor4f(s.r, s.g, s.b, 0.30f + 0.15f * pulse);
        box(s.x, 5.f, s.z, 1.8f, 10.f, 1.8f);
        float sx, sy;
        if (dist2(s.x, s.z, ex, ez) < 150.f && project(s.x, 11.f, s.z, sx, sy)) labels.push_back({sx, sy, s.name, 1.f, 1.f, 1.f});
    }
    if (playing) {
        float tx, tz, sx, sy;
        if (storyTarget(tx, tz)) {
            glColor4f(1.f, 0.9f, 0.1f, 0.35f + 0.2f * pulse); box(tx, 9.f, tz, 2.6f, 18.f, 2.6f);
            if (project(tx, 19.f, tz, sx, sy)) labels.push_back({sx, sy, "OBJECTIVE", 1.f, 0.9f, 0.2f});
        }
        if (g.job.active) {
            glColor4f(0.2f, 1.f, 1.f, 0.35f + 0.2f * pulse); box(g.job.x, 7.f, g.job.z, 2.4f, 14.f, 2.4f);
            if (project(g.job.x, 15.f, g.job.z, sx, sy)) labels.push_back({sx, sy, "DELIVERY", 0.3f, 1.f, 1.f});
        }
        if (wpOn) {
            glColor4f(1.f, 0.2f, 0.9f, 0.35f + 0.2f * pulse); box(wpx, 9.f, wpz, 2.0f, 18.f, 2.0f);
            if (project(wpx, 19.f, wpz, sx, sy)) labels.push_back({sx, sy, "WAYPOINT", 1.f, 0.4f, 0.9f});
        }
    }
    glDepthMask(GL_TRUE);
    if (night) {  // headlight pools on the road + glowing lamps/lights
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        float a = (0.55f - day) * 0.55f;
        for (int i = 0; i < MAXCARS; i++) {
            const Car& c = cars[i];
            if (!c.used || c.dead || dist2(c.x, c.z, ex, ez) > 140.f) continue;
            glPushMatrix();
            glTranslatef(c.x, 0.17f, c.z); glRotatef(c.heading * 180.f / PI, 0, 1, 0);
            glBegin(GL_QUADS);
            glColor4f(1.f, 0.95f, 0.7f, a); glVertex3f(-0.9f, 0, 2.3f); glVertex3f(0.9f, 0, 2.3f);
            glColor4f(1.f, 0.95f, 0.7f, 0.f); glVertex3f(5.f, 0, 22.f); glVertex3f(-5.f, 0, 22.f);
            glEnd();
            glPopMatrix();
        }
        glDepthMask(GL_TRUE);
        float nf = clampf(1.f - day * 1.8f, 0.f, 1.f);
        gfx::glowBegin(26.f);
        for (size_t i = 0; i + 3 < lamps.size(); i += 4) {
            if (dist2(lamps[i], lamps[i + 1], ex, ez) > 170.f) continue;
            glColor4f(1.f, 0.85f, 0.5f, 0.85f * nf);
            glVertex3f(lamps[i], 6.9f, lamps[i + 1]);
        }
        gfx::glowEnd();
        gfx::glowBegin(11.f);
        for (int i = 0; i < MAXCARS; i++) {
            const Car& c = cars[i];
            if (!c.used || c.dead || dist2(c.x, c.z, ex, ez) > 130.f) continue;
            float fx = sinf(c.heading), fz = cosf(c.heading), rx = -cosf(c.heading), rz = sinf(c.heading);
            for (int s = -1; s <= 1; s += 2) {
                glColor4f(1.f, 0.95f, 0.75f, 0.9f); glVertex3f(c.x + fx * 2.3f + rx * s * 0.68f, 0.85f, c.z + fz * 2.3f + rz * s * 0.68f);
                glColor4f(1.f, 0.1f, 0.1f, 0.8f); glVertex3f(c.x - fx * 2.3f + rx * s * 0.7f, 0.85f, c.z - fz * 2.3f + rz * s * 0.7f);
            }
        }
        gfx::glowEnd();
    }
    if (alarm) {  // police light glow
        gfx::glowBegin(30.f);
        bool a = sinf(clk * 14.f) > 0;
        for (int i = 0; i < MAXCARS; i++) {
            const Car& c = cars[i];
            if (!c.used || c.dead || c.kind != 1 || dist2(c.x, c.z, ex, ez) > 150.f) continue;
            float rx = -cosf(c.heading), rz = sinf(c.heading), fx = sinf(c.heading), fz = cosf(c.heading);
            glColor4f(1.f, 0.1f, 0.1f, a ? 0.9f : 0.15f); glVertex3f(c.x - rx * 0.4f - fx * 0.35f, 1.95f, c.z - rz * 0.4f - fz * 0.35f);
            glColor4f(0.1f, 0.2f, 1.f, a ? 0.15f : 0.9f); glVertex3f(c.x + rx * 0.4f - fx * 0.35f, 1.95f, c.z + rz * 0.4f - fz * 0.35f);
        }
        gfx::glowEnd();
    }
    glDisable(GL_BLEND);
    glPointSize(5.f);
    glBegin(GL_POINTS);
    for (const Particle& pt : parts) { float f = pt.life / pt.maxLife; glColor3f(pt.r * f + 0.1f, pt.g * f, pt.b * f); glVertex3f(pt.x, pt.y, pt.z); }
    glEnd();
    if (playing && g.tracerT > 0) {
        glLineWidth(2.5f);
        glColor3f(1.f, 0.9f, 0.4f);
        glBegin(GL_LINES); glVertex3f(g.tx0, 1.3f, g.tz0); glVertex3f(g.tx1, 1.3f, g.tz1); glEnd();
    }

    // ------------------------------------------------------------ 2D ----
    begin2D();
    if (!playing) { drawMenu(W, H); end2D(); return; }
    for (const Label& l : labels) text(l.x - (float)l.s.size() * 4.f, l.y, l.s, l.r, l.g, l.b);
    // main panel
    rect(10, 10, 300, 128, 0, 0, 0, 0.45f);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s   $%d", chars[cur].name, g.money);
    text(20, 34, buf, 0.4f, 1.f, 0.4f);
    float hp = clampf(g.health / 100.f, 0.f, 1.f);
    text(20, 56, "HEALTH", 1, 1, 1);
    rect(95, 43, 200, 14, 0.2f, 0.2f, 0.2f, 0.9f); rect(95, 43, 200 * hp, 14, 0.85f, 0.15f, 0.15f, 1.f);
    snprintf(buf, sizeof(buf), "AMMO  %d", g.ammo);
    text(20, 78, buf, 1.f, 0.85f, 0.4f);
    int st = stars();
    std::string ws = "WANTED  ";
    ws += st > 0 ? std::string(st, '*') : "-";
    bool flashOn = st == 0 || sinf(g.uptime * 10.f) > 0;
    text(20, 100, ws, flashOn ? 1.f : 0.4f, 0.4f, flashOn ? 0.4f : 1.f);
    snprintf(buf, sizeof(buf), "BILLS DUE  $%d  (next in %ds)", (int)g.billsDue, (int)(75.f - g.billTimer));
    text(20, 122, buf, g.billsDue >= 400 ? 1.f : 0.9f, g.billsDue >= 400 ? 0.3f : 0.9f, g.billsDue >= 400 ? 0.3f : 0.9f);
    drawMinimap(W);
    text(W - 222, 245, clockStr(), 1, 1, 1);
    if (unreadCount() > 0 && sinf(g.uptime * 6.f) > -0.3f) text(W - 222, 265, "J: " + std::to_string(unreadCount()) + " new message(s)", 0.5f, 1.f, 0.6f);
    // objective banner
    {
        std::string obj = storyStep < NSTEPS ? std::string("OBJECTIVE: ") + STEPS[storyStep].obj : "Free roam: take jobs, pay bills, dodge the cops";
        float w = textWidth(obj) + 28;
        rect(W * 0.5f - w * 0.5f, 10, w, 28, 0.f, 0.f, 0.f, 0.55f);
        text(W * 0.5f - w * 0.5f + 14, 29, obj, 1.f, 0.9f, 0.3f);
        float oy = 46;
        if (net::N().active) { text(W * 0.5f - textWidth(net::N().status) * 0.5f, oy + 12, net::N().status, 0.5f, 1.f, 0.5f); oy += 20; }
        if (wpOn) {
            snprintf(buf, sizeof(buf), "WAYPOINT  %dm  (X clears)", (int)dist2(wpx, wpz, g.x, g.z));
            text(W * 0.5f - textWidth(buf) * 0.5f, oy + 12, buf, 1.f, 0.5f, 0.9f); oy += 20;
        }
    }
    if (g.job.active) {
        float d = dist2(g.job.x, g.job.z, g.x, g.z);
        snprintf(buf, sizeof(buf), "JOB: deliver cargo  %dm  %ds left  reward $%d", (int)d, (int)g.job.time, g.job.reward);
        rect(10, 146, 450, 24, 0, 0, 0, 0.45f);
        text(20, 164, buf, 0.4f, 1.f, 1.f);
    }
    if (inCar) {
        snprintf(buf, sizeof(buf), "%d km/h", (int)(spd * 3.6f));
        textBig(W - 280, H - 40, buf, 1, 1, 1);
        float chp = clampf(cars[g.driving].hp / 120.f, 0.f, 1.f);
        text(W - 280, H - 62, "CAR", 1, 1, 1);
        rect(W - 235, H - 74, 140, 10, 0.2f, 0.2f, 0.2f, 0.9f); rect(W - 235, H - 74, 140 * chp, 10, 0.95f, 0.6f, 0.1f, 1.f);
    }
    // prompts
    int si = nearestSpot(5.5f);
    std::string prompt;
    if (si >= 0 && g.state == 0) {
        switch (spots[si].type) {
        case S_SUPER: prompt = "[E] Buy burger  +50 health  ($25)"; break;
        case S_AMMO: prompt = "[E] Buy 30 rounds  ($60)"; break;
        case S_JOB: prompt = "[E] Take a delivery job"; break;
        case S_BILLS: prompt = "[E] Pay your bills"; break;
        case S_HOSP: prompt = "[E] Full treatment  ($50)"; break;
        case S_GYM: prompt = "[E] Talk to your friends"; break;
        }
    } else if (g.state == 0 && g.driving < 0) {
        for (int i = 0; i < MAXCARS; i++)
            if (cars[i].used && !cars[i].dead && cars[i].kind != 1 && dist2(cars[i].x, cars[i].z, g.x, g.z) < 5.5f) { prompt = "[F] Enter vehicle"; break; }
    } else if (g.state == 0 && g.driving >= 0 && fabsf(spd) < 9.f) prompt = "[F] Exit vehicle    [H] Horn";
    if (!prompt.empty() && !dlg.active) {
        float w = textWidth(prompt) + 28;
        rect(W * 0.5f - w * 0.5f, H - 96, w, 28, 0, 0, 0, 0.55f);
        text(W * 0.5f - w * 0.5f + 14, H - 76, prompt, 1, 1, 0.5f);
    }
    float ty = 110;
    for (const Toast& t : toasts) {
        float w = textWidth(t.s) + 28;
        rect(W * 0.5f - w * 0.5f, ty - 18, w, 26, 0, 0, 0, 0.5f * std::min(1.f, t.t));
        text(W * 0.5f - w * 0.5f + 14, ty, t.s, 1, 1, 1);
        ty += 30;
    }
    if (g.uptime < 25.f) {
        rect(10, H - 132, 560, 120, 0, 0, 0, 0.45f);
        text(20, H - 110, "W/S gas+brake (walk)   A/D steer (turn)   SHIFT sprint   H horn", 1, 1, 1);
        text(20, H - 90, "SPACE shoot (on foot) / handbrake (car)   F enter/exit car", 1, 1, 1);
        text(20, H - 70, "E shop/job/talk   J phone   M map   Q switch character", 1, 1, 1);
        text(20, H - 50, "P pause   ESC main menu", 1, 1, 1);
        text(20, H - 28, "Cops, bills and hunger are all trying to ruin your day.", 0.8f, 0.8f, 0.8f);
    }
    if (g.hurt > 0) rect(0, 0, W, H, 0.8f, 0.f, 0.f, clampf(g.hurt, 0.f, 0.3f));
    if (g.state == 1) {
        rect(0, 0, W, H, 0.3f, 0, 0, 0.5f);
        textBig(W * 0.5f - textWidthBig("WASTED") * 0.5f, H * 0.5f, "WASTED", 0.9f, 0.1f, 0.1f);
    }
    if (g.state == 2) {
        rect(0, 0, W, H, 0, 0, 0, 0.7f);
        std::string a = "EVICTED - GAME OVER";
        textBig(W * 0.5f - textWidthBig(a) * 0.5f, H * 0.5f - 10, a, 1.f, 0.3f, 0.3f);
        std::string b = "You ignored your bills. Press R to start over, ESC for the menu.";
        text(W * 0.5f - textWidth(b) * 0.5f, H * 0.5f + 30, b, 1, 1, 1);
    }
    if (g.paused) {
        rect(0, 0, W, H, 0, 0, 0, 0.5f);
        textBig(W * 0.5f - textWidthBig("PAUSED") * 0.5f, H * 0.5f, "PAUSED", 1, 1, 1);
    }
    drawDialog(W, H);
    if (mapOpen) drawMap(W, H);
    if (phone.open) drawPhone(W, H);
    end2D();
}

// ----------------------------------------------------------------- main ----
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!init("LITE CITY (beta) - LiteEngine", 1280, 720)) {
        MessageBoxA(nullptr, "Could not create an OpenGL window.", "LiteEngine", MB_OK | MB_ICONERROR);
        return 1;
    }
    au::start();
    au::M().station = 1;
    loadOBJ("assets/car.obj", gfx::carModel(), 4.4f);  // optional: drop your own model here
    buildWorld();
    resetGame();
    scene = 0;
    float dt = 0.016f;
    while (pump(dt)) {
        update(dt);
        render();
        present();
    }
    au::stop();
    net::stop();
    return 0;
}
