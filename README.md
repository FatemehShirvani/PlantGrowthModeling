# Season-Aware Procedural Tree Animation

Real-time procedural plant growth and seasonal animation system written in C++ and OpenGL. The project integrates stochastic L-system branching, staged growth activation, leaf lifecycle simulation, wind response, alpha-tested foliage rendering, and shadow mapping in one interactive application.

This was developed as the final project for **Fundamentals of Computer Graphics (IGR202)** at Institut Polytechnique de Paris, taught by [Amal Dev Parakkat](https://scholar.google.com/citations?hl=en&user=a-Lm3LgAAAAJ) and [Kiwon Um](https://scholar.google.com/citations?hl=en&user=H2Omi3wAAAAJ).

Project report: [`docs/project-report.pdf`](docs/project-report.pdf)

## What It Demonstrates

- Procedural branch generation with an L-system style hierarchy.
- Progressive growth stages controlled interactively.
- Seasonal transitions across spring, summer, fall, and winter.
- Persistent leaf identities across attachment, detachment, airborne motion, landing, and decay.
- Filtered wind dynamics for branch bending and leaf flutter.
- Shadow mapping with consistent alpha testing for foliage silhouettes.
- Textured bark and leaf atlases with per-leaf visual variation.

The main implementation focus is simulation coherence: keeping growth, wind, leaf shedding, and rendering synchronized so seasonal transitions do not create visible popping, jitter, or disconnected leaf motion.

## Controls

| Key | Action |
| --- | --- |
| `G` | Unlock next growth stage |
| `T` | Toggle continuous growth animation |
| `C` | Advance season |
| `W` | Toggle wind physics |
| `R` | Restart plant growth with a new seed |
| `Shift + S` | Save shadow maps as PPM files |

## Build

Requirements:

- CMake 3.14+
- A C++11 compiler
- OpenGL-capable GPU/driver

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run from the repository root so the executable can find `data/` and shader files:

```powershell
.\build\Release\plant_growth.exe
```

On single-config generators, the executable may be under `build/plant_growth`.

## Repository Layout

- `src/` - C++ source and GLSL shaders.
- `data/` - runtime texture assets used by the renderer.
- `dep/glad/` - local GLAD loader source.
- `docs/project-report.pdf` - project report.

## Notes

The report includes the design chronology, related work, implementation details, and evaluation notes for the final integrated behavior.
