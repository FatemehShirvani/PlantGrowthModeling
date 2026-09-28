# Season-Aware Procedural Tree Animation

A real-time C++/OpenGL system that couples procedural tree growth, four-season behavior, wind response, leaf detachment, and shadowed foliage rendering. The central engineering goal is temporal coherence: growth, recoloring, shedding, wind, and rendering must remain synchronized without visible popping, pumping, or disconnected leaf motion.

Developed as the final project for **Fundamentals of Computer Graphics (IGR202)** at Institut Polytechnique de Paris, taught by [Amal Dev Parakkat](https://perso.telecom-paristech.fr/parakkat/) and [Kiwon Um](https://perso.telecom-paristech.fr/kum/).

## System Architecture

The application combines three continuously interacting subsystems:

- **Plant state:** branch hierarchy, activation stages, attached-leaf state, and fallen-leaf particles.
- **Scene state:** seasonal parameters, materials, light-space transforms, and rendering passes.
- **Interaction loop:** input callbacks, simulation updates, mesh rebuilds, and draw calls.

Each frame updates growth and seasonal interpolation, advances wind and detached leaves, rebuilds geometry only when required, then renders a shadow-map pass followed by the main shaded pass.

## Procedural Growth

Tree topology is generated from a stochastic L-system and interpreted with a 3D turtle and stack. Each branch records its endpoints, direction, depth, radius, parent attachment, age, and activation stage. The complete hierarchy is generated first, but branches become visible progressively, allowing parent segments to continue growing while later stages are unlocked.

Variation in child count, branching angle, azimuth, taper, and length prevents mechanically repeated silhouettes. Deterministic random seeds keep a generated tree reproducible while still allowing new variants through the runtime controls.

## Foliage Distribution

Leaves are generated along active branches using deterministic per-leaf identities. Placement combines canopy density controls with local separation: candidates are approximated by small spheres and softly repelled from accepted leaves to reduce obvious intersections and clusters.

Each identity stores size, atlas variant, color variation, local basis, and lifecycle state. Preserving that identity is important when a leaf moves from an attached branch to the airborne simulation and finally to ground litter.

## Seasonal Controller

The controller interpolates between four parameter sets:

- **Spring:** fresh green tint and small emerging leaves.
- **Summer:** full size and canopy density.
- **Fall:** gradual color variation, reduced density, and staged detachment.
- **Winter:** sparse branches and aging ground litter.

Transitions use smoothstep and smootherstep easing rather than direct state replacement. Winter-to-Spring regrowth is gated until old leaves have detached or cleared, preventing old and new canopies from appearing simultaneously.

## Leaf Lifecycle

Detached leaves inherit their attached state, including size, orientation, texture variant, and color. Their motion then advances through airborne, landing, settling, aging, and removal phases. Delayed orientation blending prevents an abrupt change from branch alignment to tumbling.

Ground contact uses the lowest transformed leaf corner rather than its center, followed by damped settling. Capacity management removes the oldest landed leaves first so new detachment events remain visible. The resulting lifecycle is continuous: grow, mature, recolor, detach, settle, and disappear.

## Wind Model

The final wind model replaces uniform sinusoidal sway with filtered stochastic direction and gust states. A second-order damped response introduces phase lag, while flexibility varies with branch depth and radius so the trunk remains comparatively stable and branch tips respond more strongly.

Leaves inherit the deformation of their parent branch before receiving a small local flutter with per-leaf phase variation. Separating hierarchical branch motion from local leaf motion avoids both rigidly synchronized canopies and the impression that leaves are detached from moving branches.

## Rendering

The renderer uses textured bark and alpha-cutout foliage in a two-pass shadow-mapping pipeline. The same alpha threshold is applied in the depth and shading passes so transparent parts of leaf cards do not cast rectangular shadows. Stable light-space bounds and a calibrated depth bias reduce flicker on thin foliage.

The fall atlas uses explicit UV rectangles because its variants are not arranged as a uniform grid. Per-leaf color variation distributes yellow, orange, brown, and muted red tones across the canopy instead of applying one uniform fall color.

## Evaluation

Evaluation used repeatable growth, season, and wind sequences under fixed camera conditions. Early and final versions were compared through screenshots and video captures, focusing on temporal continuity, attachment cues, motion plausibility, and stability when all subsystems run together.

The integrated version substantially reduced transition flashing, synchronized detachment, atlas artifacts, abrupt ground contact, and phase-locked wind motion. The result is qualitative rather than a full performance benchmark; systematic frame-time and scalability measurements remain future work.

## Limitations

- Growth parameters are procedural heuristics rather than calibrated elm allometry.
- The seasonal schedule is stylized and does not model climate-dependent phenology.
- Several update stages remain CPU-heavy, limiting large forests.
- The renderer is raster-based and designed for one detailed interactive tree.

## Controls

| Key | Action |
| --- | --- |
| `G` | Unlock next growth stage |
| `T` | Toggle continuous growth animation |
| `C` | Advance season |
| `W` | Toggle wind physics |
| `R` | Restart plant growth with a new seed |
| `L` | Apply one Loop subdivision step |
| `U` | Undo one subdivision step |
| `Shift + S` | Save shadow maps as PPM files |

## Build

Requirements:

- CMake 3.14+
- A C++11 compiler
- An OpenGL-capable GPU and driver

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

## References

1. Prusinkiewicz, P. and Lindenmayer, A. *The Algorithmic Beauty of Plants*. Springer-Verlag, 1990.
2. Runions, A., Lane, B., and Prusinkiewicz, P. [Modeling Trees with a Space Colonization Algorithm](https://doi.org/10.2312/NPH/NPH07/063-070). NPH, 2007.
3. Palubicki, W. et al. [Self-Organizing Tree Models for Image Synthesis](https://doi.org/10.1145/1531326.1531364). ACM Transactions on Graphics, 2009.
4. Habel, R., Kusternig, A., and Wimmer, M. [Physically Guided Animation of Trees](https://doi.org/10.1111/j.1467-8659.2009.01391.x). Computer Graphics Forum, 2009.
5. Williams, L. [Casting Curved Shadows on Curved Surfaces](https://doi.org/10.1145/965139.807402). SIGGRAPH, 1978.
6. Reeves, W. T. and Blau, R. [Approximate and Probabilistic Algorithms for Shading and Rendering Structured Particle Systems](https://doi.org/10.1145/325334.325234). SIGGRAPH, 1985.
