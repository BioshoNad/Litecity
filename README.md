# LITE CITY v0.2 (beta) on LiteEngine

A from-scratch 3D game engine (Win32 + OpenGL, **zero dependencies**) and an open-world
sample game with a story. Builds to a single `LiteCity.exe`.

## Build
1. Install a compiler once (**MSYS2/MinGW-w64** or **Visual Studio Build Tools**) and double-click `build.bat`
   - or upload everything to GitHub and let the included GitHub Action build it (Actions tab -> Artifacts).
2. Run `LiteCity.exe`.

Manual MinGW command:
`g++ -std=c++17 -O2 main.cpp -o LiteCity.exe -mwindows -static -lopengl32 -lglu32 -lgdi32 -luser32 -lwinmm -lws2_32`

## Main menu
**SINGLE PLAYER** / **MULTIPLAYER** / **QUIT** - the blue shape behind a button grows when the mouse hovers over it.
Multiplayer: one player picks HOST GAME, the other types the host's IP and picks JOIN GAME (UDP port 27015;
open it in the firewall / forward it for internet play). You each play your own copy of the city and see each other.

## Controls
| Key | On foot | In car |
|---|---|---|
| W / S | walk | gas / brake + reverse |
| A / D | turn | steer |
| SHIFT | sprint | - |
| SPACE | shoot | handbrake |
| F | enter nearest car | exit car (below ~30 km/h) |
| E | shop / job / bills / talk | same |
| H | - | horn |
| **Q** | **switch John <-> Jason** | |
| **J** | **phone** (LP Media, Messages, Calls, G Files) | |
| **M** | **full city map** (click = waypoint, X = clear) | |
| P / ESC | pause / back to menu | |

## Story (Chapter 1)
You are **John**. Your friends **Marcus** and **Dante** hang out at the **GYM**, which is right behind the Ammo Store.
Follow the yellow OBJECTIVE marker: meet them, steal a car and pick up **Jason** (then press Q to play as him),
cause trouble, lose the cops, visit Marcus in hospital, and get to the docks. After the chapter you keep free-roaming.

## Phone (J)
- **LP Media** - pick a radio station (LITE FM, NIGHT DRIVE, CHILL ROADS) and set the volume
- **Messages** - texts from your friends (they arrive as the story moves on)
- **Calls** - call Marcus/Jason/Dante for hints
- **G Files** - mission log, stats, SAVE / LOAD (litecity.sav), controls help

## Sound
All synthesized in code: gunshots, explosions, horn, phone rings, engine hum, police siren, and 3 music stations.

## Graphics in v0.2
Sky dome with sun, moon and stars; stencil shadows; textured roads, sidewalks, grass; 3 building styles with
setback towers; trees; street lamps with night glow; crosswalks; 3 shaped car bodies with glass, rims and lights;
detailed characters. **Real car models:** drop an OBJ at `assets/car.obj` (see assets/README.txt).

## Engine layout
- `engine.h` window, GL context, input (keyboard+mouse+text), timer, 2D/3D helpers
- `audio.h`  software mixer + synthesized sounds + music (waveOut output)
- `net.h`    UDP host/join presence for multiplayer
- `model.h`  OBJ model loader
- `gfx.h`    sky, shadows, textures, cars, people, trees, lamps, glow
- `main.cpp` the game: world, AI, police, economy, story, phone, map, menus, HUD

## Roadmap
Shader-based lighting and reflections, more story chapters and characters, interiors, mouse-look camera,
real shared-world multiplayer (synchronised NPCs/police), more vehicle types.
