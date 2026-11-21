// ============================================================================
//  LiteEngine audio - everything is synthesized in code (no sound files needed)
//  * software mixer (mono, 22050 Hz, 16-bit)
//  * one-shot sound effects, engine hum, police siren, 3 music "radio stations"
//  * Windows output through waveOut (winmm) on a background thread
// ============================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>
#ifdef _WIN32
#include "engine.h"
#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif
#endif

namespace lite {
namespace audio {

const int RATE = 22050;
const float TAU = 6.2831853f;

enum Sfx { SFX_SHOT, SFX_HIT, SFX_EXPLODE, SFX_HORN, SFX_CLICK, SFX_HOVER, SFX_RING, SFX_MSG, SFX_CASH, SFX_DOOR, SFX_COUNT };

struct Voice { const std::vector<short>* s = nullptr; size_t pos = 0; float vol = 1.f; volatile bool active = false; };

struct Mixer {
    std::vector<short> sfx[SFX_COUNT];
    Voice voices[16];
    // live parameters (written by the game thread, read by the audio thread)
    volatile float engineVol = 0.f, enginePitch = 0.f, sirenVol = 0.f;
    volatile int station = 1;  // 0 = off
    volatile float musicVol = 0.35f, master = 0.85f;
    float enginePhase = 0.f, sirenPhase = 0.f, sirenLfo = 0.f;
    double musicT = 0.0;
    float oscPhase[4] = {0, 0, 0, 0};
    uint32_t rng = 12345u;
    bool ready = false;
};
inline Mixer& M() { static Mixer m; return m; }

inline float noise() {
    Mixer& m = M();
    m.rng = m.rng * 1664525u + 1013904223u;
    return ((m.rng >> 8) & 0xFFFF) / 32768.f - 1.f;
}
inline short toS(float v) {
    if (v > 1.f) v = 1.f;
    if (v < -1.f) v = -1.f;
    return (short)(v * 32000.f);
}

// ---------------------------------------------------------------- sfx ----
inline void makeSfx() {
    Mixer& m = M();
    auto gen = [&](Sfx id, float secs, float (*fn)(float t, float n)) {
        int n = (int)(secs * RATE);
        m.sfx[id].resize(n);
        for (int i = 0; i < n; i++) m.sfx[id][i] = toS(fn(i / (float)RATE, secs));
    };
    gen(SFX_SHOT, 0.30f, [](float t, float) { float e = expf(-t * 18.f); return (noise() * 0.8f + sinf(TAU * 90.f * t) * 0.6f) * e; });
    gen(SFX_HIT, 0.12f, [](float t, float) { float e = expf(-t * 35.f); return (noise() * 0.7f + sinf(TAU * 140.f * t) * 0.3f) * e; });
    gen(SFX_EXPLODE, 1.4f, [](float t, float) {
        static float lp = 0.f;
        lp += (noise() - lp) * 0.08f;
        return (lp * 2.4f + sinf(TAU * 45.f * t) * 0.5f) * expf(-t * 2.6f);
    });
    gen(SFX_HORN, 0.55f, [](float t, float d) {
        float e = (t < 0.03f ? t / 0.03f : 1.f) * (t > d - 0.08f ? (d - t) / 0.08f : 1.f);
        return (sinf(TAU * 392.f * t) + 0.7f * sinf(TAU * 494.f * t) + 0.3f * sinf(TAU * 784.f * t)) * 0.3f * e;
    });
    gen(SFX_CLICK, 0.05f, [](float t, float) { return sinf(TAU * 1100.f * t) * expf(-t * 70.f) * 0.7f; });
    gen(SFX_HOVER, 0.04f, [](float t, float) { return sinf(TAU * 700.f * t) * expf(-t * 90.f) * 0.35f; });
    gen(SFX_RING, 1.0f, [](float t, float) {
        float on = (fmodf(t, 0.5f) < 0.38f) ? 1.f : 0.f;
        return (sinf(TAU * 440.f * t) + sinf(TAU * 480.f * t)) * 0.25f * on;
    });
    gen(SFX_MSG, 0.5f, [](float t, float) {
        float f = t < 0.18f ? 880.f : 1175.f;
        float lt = t < 0.18f ? t : t - 0.18f;
        return sinf(TAU * f * t) * expf(-lt * 9.f) * 0.5f;
    });
    gen(SFX_CASH, 0.35f, [](float t, float) {
        float f = t < 0.1f ? 1200.f : 1800.f;
        float lt = t < 0.1f ? t : t - 0.1f;
        return sinf(TAU * f * t) * expf(-lt * 12.f) * 0.45f;
    });
    gen(SFX_DOOR, 0.15f, [](float t, float) { return (noise() * 0.5f + sinf(TAU * 110.f * t) * 0.5f) * expf(-t * 30.f); });
    m.ready = true;
}

inline void play(Sfx id, float vol = 1.f) {
    Mixer& m = M();
    if (!m.ready) return;
    for (int i = 0; i < 16; i++)
        if (!m.voices[i].active) {
            Voice& v = m.voices[i];
            v.s = &m.sfx[id]; v.pos = 0; v.vol = vol; v.active = true;
            return;
        }
}

// ------------------------------------------------------------- music -----
// Three stations. Each: bpm, 4 chord roots (MIDI), 32-step lead pattern (semitones above root, -99 = rest)
struct Station { const char* name; const char* track; float bpm; int roots[4]; int lead[32]; int kick; int minor; };
static const Station STATIONS[3] = {
    {"LITE FM", "Downtown Rush - The Blocks", 124.f, {57, 53, 48, 55},
     {12, -99, 16, -99, 19, -99, 16, 12, 14, -99, 17, -99, 21, -99, 17, 14, 12, -99, 16, -99, 19, 24, 19, 16, 14, -99, 12, -99, 11, -99, 14, -99}, 1, 0},
    {"NIGHT DRIVE", "Neon Overpass - Kora", 100.f, {57, 53, 60, 55},
     {12, -99, -99, 15, -99, 19, -99, -99, 17, -99, -99, 14, -99, 17, -99, -99, 15, -99, -99, 19, -99, 22, -99, 19, 17, -99, 15, -99, 14, -99, -99, -99}, 1, 1},
    {"CHILL ROADS", "Slow Lane - Moon Cafe", 78.f, {60, 57, 53, 55},
     {16, -99, 12, -99, 14, -99, -99, -99, 12, -99, 9, -99, 11, -99, -99, -99, 14, -99, 16, -99, 19, -99, -99, -99, 16, -99, 14, -99, 12, -99, -99, -99}, 0, 0}};

inline float midiHz(int n) { return 440.f * powf(2.f, (n - 69) / 12.f); }
inline float saw(float ph) { return 2.f * (ph - floorf(ph)) - 1.f; }
inline float tri(float ph) { return fabsf(4.f * (ph - floorf(ph)) - 2.f) - 1.f; }
inline float sqr(float ph) { return (ph - floorf(ph)) < 0.5f ? 1.f : -1.f; }

inline float musicSample(Mixer& m, int st) {
    const Station& S = STATIONS[st - 1];
    double beat = m.musicT * S.bpm / 60.0;
    int step = (int)(beat * 2.0);        // eighth notes
    float within = (float)(beat * 2.0 - step);  // 0..1 inside the step
    int bar = (step / 8) % 4;
    int root = S.roots[bar];
    float stepSec = 30.f / S.bpm;
    float tin = within * stepSec;
    float out = 0.f;
    // bass: root note, octave down, plucked on every 2nd eighth
    {
        float f = midiHz(root - 24);
        m.oscPhase[0] += f / RATE;
        float env = expf(-tin * 4.f) * ((step % 2 == 0) ? 1.f : 0.f);
        out += (sqr(m.oscPhase[0]) * 0.5f + saw(m.oscPhase[0]) * 0.3f) * env * 0.35f;
    }
    // pad chord (root, third, fifth)
    {
        int third = S.minor ? 3 : 4;
        float fa = midiHz(root), fb = midiHz(root + third), fc = midiHz(root + 7);
        m.oscPhase[1] += fa / RATE;
        m.oscPhase[2] += fb / RATE;
        m.oscPhase[3] += fc / RATE;
        out += (tri(m.oscPhase[1]) + tri(m.oscPhase[2]) + tri(m.oscPhase[3])) * 0.07f;
    }
    // lead
    {
        int n = S.lead[step % 32];
        static float lph = 0.f;
        if (n > -90) {
            lph += midiHz(root + n) / RATE;
            float env = expf(-tin * 5.5f);
            out += tri(lph) * env * 0.22f + sinf(TAU * lph * 2.f) * env * 0.05f;
        }
    }
    // drums
    if (S.kick) {
        float bt = (float)(beat - floor(beat)) * (60.f / S.bpm);
        out += sinf(TAU * (55.f + 90.f * expf(-bt * 30.f)) * bt) * expf(-bt * 9.f) * 0.55f;
        if (step % 2 == 1) out += noise() * expf(-tin * 40.f) * 0.12f;  // hat
    } else {
        if (step % 4 == 2) out += noise() * expf(-tin * 25.f) * 0.10f;  // soft snare-ish
    }
    m.musicT += 1.0 / RATE;
    return out;
}

// ------------------------------------------------------------- mixing ----
inline void mix(short* out, int frames) {
    Mixer& m = M();
    for (int i = 0; i < frames; i++) {
        float acc = 0.f;
        for (int v = 0; v < 16; v++) {
            Voice& vo = m.voices[v];
            if (!vo.active) continue;
            if (vo.pos >= vo.s->size()) { vo.active = false; continue; }
            acc += (*vo.s)[vo.pos++] / 32768.f * vo.vol;
        }
        float ev = m.engineVol;
        if (ev > 0.001f) {
            float f = 38.f + m.enginePitch * 95.f;
            m.enginePhase += f / RATE;
            float ph = m.enginePhase;
            acc += (saw(ph) * 0.5f + sqr(ph * 2.f) * 0.25f + sinf(TAU * ph * 3.f) * 0.2f) * ev * 0.35f;
        }
        float sv = m.sirenVol;
        if (sv > 0.001f) {
            m.sirenLfo += 0.9f / RATE;
            float f = 700.f + 450.f * (0.5f + 0.5f * sinf(TAU * m.sirenLfo));
            m.sirenPhase += f / RATE;
            acc += sinf(TAU * m.sirenPhase) * sv * 0.22f;
        }
        int st = m.station;
        if (st >= 1 && st <= 3) acc += musicSample(m, st) * m.musicVol;
        out[i] = toS(acc * m.master);
    }
}

// ----------------------------------------------------- Windows output ----
#ifdef _WIN32
struct WaveOut {
    HWAVEOUT hwo = nullptr;
    WAVEHDR hdr[4];
    short buf[4][1024];
    bool queued[4] = {false, false, false, false};
    HANDLE thread = nullptr;
    volatile bool run = false;
};
inline WaveOut& WO() { static WaveOut w; return w; }

inline DWORD WINAPI audioThread(LPVOID) {
    WaveOut& w = WO();
    while (w.run) {
        for (int i = 0; i < 4; i++) {
            if (!w.queued[i] || (w.hdr[i].dwFlags & WHDR_DONE)) {
                mix(w.buf[i], 1024);
                waveOutWrite(w.hwo, &w.hdr[i], sizeof(WAVEHDR));
                w.queued[i] = true;
            }
        }
        Sleep(4);
    }
    return 0;
}

inline bool start() {
    makeSfx();
    WaveOut& w = WO();
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = RATE * 2;
    if (waveOutOpen(&w.hwo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    for (int i = 0; i < 4; i++) {
        memset(&w.hdr[i], 0, sizeof(WAVEHDR));
        w.hdr[i].lpData = (LPSTR)w.buf[i];
        w.hdr[i].dwBufferLength = sizeof(w.buf[i]);
        waveOutPrepareHeader(w.hwo, &w.hdr[i], sizeof(WAVEHDR));
    }
    w.run = true;
    w.thread = CreateThread(nullptr, 0, audioThread, nullptr, 0, nullptr);
    return w.thread != nullptr;
}
inline void stop() {
    WaveOut& w = WO();
    w.run = false;
    if (w.thread) { WaitForSingleObject(w.thread, 500); }
    if (w.hwo) waveOutReset(w.hwo);
}
#else
inline bool start() { makeSfx(); return true; }
inline void stop() {}
#endif

}  // namespace audio
}  // namespace lite
