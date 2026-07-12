# FamberEngine

A native game engine in **C++ + OpenGL** (pure Win32 + WGL, no external
dependencies), aiming for GoldSource-level features. **MIT** licensed.

Loads and renders real Half-Life `.bsp` maps and `.mdl` models.

## Features

- **M1 — Rendering, camera, Quake physics.** Win32 + WGL, custom GL 2.0+ loader,
  shader renderer, VBO. Quake/GoldSrc player movement: acceleration/friction,
  air-strafe & **bhop**, jump, wall sliding, step-up. FPS mouse-look camera.
- **M2 — Convex plane brushes.** Brushes as plane sets with swept box-vs-brush
  tracing (Quake-style) → sloped surfaces and ramps.
- **M3 — `.map` levels.** Hammer/Quake `.map` parser (winding auto-corrected),
  spawn from `info_player_start`, plus a `.map` exporter.
- **M4 — Textures & lightmaps.** Procedural per-face textures (named from `.map`),
  face-aligned UVs. **Baked per-face lightmaps**: point lights with shadow rays
  (reusing the box trace), colored light, falloff, ambient. `base * lightmap`.
- **M5 — Real GoldSrc `.bsp` (v30).** Parses all lumps, embedded miptex textures
  with palettes, embedded lightmaps, UVs from texinfo, spawn from entities.
  Collision via **hull-1 clipnodes** (recursive point trace). Tested on real
  Half-Life maps (`boot_camp`, `c0a0`).
- **M6 — `.mdl` studiomodels.** Bones, **skeletal animation** (sequence decode,
  quaternions), palette skins (including the separate `<name>T.mdl`), triangle
  strips/fans. Model viewer with auto-rotate and animation. Tested on `gman`,
  `barney`.

## Building

Requires MinGW-w64 (in `C:\mingw64`). From `D:\engine`: run `build.bat`, or:

```
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ ^
    src\game\main.cpp src\platform\gl.cpp -o FamberEngine.exe -lopengl32 -lgdi32 -luser32
```

## Running

- `FamberEngine.exe` — play the built-in level. **WASD** move, mouse look,
  **SPACE** jump (hold to bhop), **ESC** quit.
- `FamberEngine.exe -map maps\test.map` — load a `.map` level.
- `FamberEngine.exe -bsp "...\valve\maps\c0a0.bsp"` — a real Half-Life map.
- `FamberEngine.exe -mdl "...\valve\models\gman.mdl"` — model viewer.
- `FamberEngine.exe -selftest` — headless physics checks.
- `FamberEngine.exe -genmap out.map` — export the built-in level to `.map`.
- `FamberEngine.exe -shot out.bmp` — render one frame to a file.

## Architecture (`src/`)

```
core/      math.h                  — vectors / matrices (Z-up, column-major)
platform/  gl.{h,cpp}  window.h    — GL loader, window/context/input/screenshot
world/     brush.h trace.h         — convex brushes, collision, shadow rays
           level.h map.h           — level (brushes+lights+spawn), .map loader
           bsp.h                   — GoldSrc BSP v30 loader + hull collision
           mdl.h                   — .mdl (studiomodel) loader + skeleton
physics/   pmove.h                 — Quake player physics (via a TraceFn)
render/    texture.h lightmap.h    — procedural textures, lightmap baking
           renderer.h model.h      — GL renderer for the level and models
game/      main.cpp                — entry point, loop, modes
```

## Roadmap

1. **M1–M6 (done)** — rendering, physics, `.map`, textures, lightmaps, `.bsp`, `.mdl`.
2. Entity system, spawning models in the world, sound, console/cvars, PVS.
3. Client-server networking with prediction.

## License

MIT — see [LICENSE](LICENSE).
