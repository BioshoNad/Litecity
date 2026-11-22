# SAMPLE v0.3 on LiteEngine

An open-world sample game with a story, running on your own from-scratch engine
(Win32 + OpenGL, **zero dependencies**). Builds to a single `LiteCity.exe` (rename it if you like).

## Build
1. Install a compiler once (**MSYS2/MinGW-w64** or **Visual Studio Build Tools**) and double-click `build.bat`
   - or upload everything to GitHub; the included GitHub Action builds it (Actions tab -> Artifacts).
2. Run the exe.

Manual MinGW command:
`g++ -std=c++17 -O2 main.cpp -o LiteCity.exe -mwindows -static -lopengl32 -lgdi32 -luser32 -lwinmm -lws2_32`

## What is new in v0.3: the graphics engine
- **Shader renderer**: per-pixel lighting, sun + sky ambient light, contact darkening near the ground
- **Real shadow maps** from the sun (buildings, cars, people, trees), soft edges
- **Sky reflections** on car paint, glass towers and windows, with sun glints and Fresnel
- **ACES tone mapping**, gamma-correct colour, distance haze that blends into the sky
- **4x antialiasing, bloom, vignette and colour grading**
- Night: lit windows, street-lamp light pools, headlights, taillights and police lights that glow
- Sharper 256px facade textures (glass offices, brick apartments, modern towers), anisotropic filtering
- **Automatic fallback**: if the graphics card/driver cannot run shaders, the game uses the classic renderer
  and writes the reason to `sample_gl_log.txt`. **F2** switches HIGH / BASIC quality while playing
  (use BASIC if the game runs slowly).

## Controls
| Key | On foot | In car |
|---|---|---|
| W / S | walk | gas / brake + reverse |
| A / D | turn | steer |
| SHIFT | sprint | - |
| SPACE | shoot | handbrake |
| F | enter nearest car | exit car |
| E | shop / job / bills / talk | same |
| H | - | horn |
| Q | switch John <-> Jason | |
| J | phone (LP Media, Messages, Calls, G Files) | |
| M | full city map (click = waypoint, X = clear) | |
| F2 | graphics quality HIGH / BASIC | |
| P / ESC | pause / back to menu | |

## Story, phone, sound, multiplayer
Same as v0.2: John, Marcus and Dante at the gym (behind the Ammo Store), Jason joins you, six story steps;
phone apps; synthesized sound and three radio stations; host/join two-player presence on UDP port 27015.

## Real car models
Drop a Wavefront OBJ at `assets/car.obj` (see assets/README.txt). It replaces the sedan body.

## Engine layout
`engine.h` window/input/timer/2D helpers | `glapi.h` OpenGL layer + modern-GL loader | `renderer.h` shaders, shadow map,
MSAA, bloom | `gfx.h` sky, textures, cars, people, trees, lamps | `audio.h` sound | `net.h` multiplayer | `model.h` OBJ loader |
`main.cpp` the game.

## Honest limits
This is a real modern lighting pipeline, but the models are still simple shapes and textures are generated in code, so it
looks like a stylised early-2010s game, not Unity 6 / Unreal 5. The biggest remaining jump needs real art assets
(OBJ models, photo textures), which the engine can already load.
