# Emergent Evolution

A GPU-accelerated particle life simulation with biologically-inspired emergent behaviour.
Thousands of particles interact through configurable force matrices, self-organise into
organisms, inherit traits across generations, reproduce, mutate, and evolve.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Particle Physics
- Up to ~22,500 particles simulated in real time on the GPU
- O(n²) pairwise force calculation in a GLSL compute shader
- **Toroidal world** — particles wrap across all four edges (2560 × 1440)
- Configurable repulsion radius, interaction radius, dampening, density limiting, and viscosity
- Soft-body pressure field: exponential density falloff creates emergent elasticity
- Double-buffered ping-pong buffers (position, velocity, angle, angular velocity, energy)

### Metabolism & Energy System

Each particle carries an energy value **0.0 → 1.0**, updated entirely on the GPU each frame.
Particle brightness reflects energy — dim particles are starving.

| Source | Rate |
|--------|------|
| Ambient food gain (baseline) | +0.010 / s |
| Passive metabolic drain | −0.015 / s |
| Movement cost | −speed × 0.00015 / s |
| Crowding penalty | −(density − limit) × 0.005 / s |
| Symbiotic gain (cross-type attraction) | +attraction × proximity × 0.005 / pair / s |
| Predator gain (when adjacent to prey) | +0.006 / s |
| Prey drain (when adjacent to predator) | −0.006 / s |
| Catalyst neighbour boost | +0.008 × catalysts / s |
| Photosynth in open space | +light × 0.12 / s |
| Dust recovery (type 0) | +0.15 / s |

Particles that reach zero energy turn to **dust** (type 0) and drift until claimed by a
reproductive organism. Dust recovers energy quickly and re-enters the food web.

### Force Matrix
- Up to 10 particle types; each ordered pair has an independent attraction/repulsion scalar
- Interactive grid: hover + scroll to adjust, right-click to zero
- Per-type colour pickers
- **Trait feedback**: organisms with accumulated kills amplify their dominant type's force row
  (up to 1.8×), feeding directly back into GPU physics

### Particle Archetypes

Eleven behaviour archetypes, selectable per type from the **Particle Archetypes** panel.
Each preset also seeds a sensible force-matrix row which can be hand-edited.

| Archetype | Behaviour |
|-----------|-----------|
| **Default** | Force matrix only |
| **Repeller** | Always repels every type (models toxins, charged ions) |
| **Polar** | Magnetic dipole — attraction modulated by dipole alignment; chains and rings form |
| **Heavy** | Force response 0.25×; acts as a slow structural nucleus |
| **Catalyst** | Neighbours gain extra energy each frame (enzyme / metabolic booster) |
| **Membrane** | Force preset only: strong self-cohesion, repels others; forms bilayer rings |
| **Viral** | Converts type of adjacent non-viral particles every 5 frames (CPU-driven) |
| **Adhesive** | 1.8× self-attraction bonus — forms dense, stable colonies |
| **Secretor** | Emits a chemical halo that pushes nearby particles toward it |
| **Photosynth** | Drifts toward low-density space; gains energy in proportion to available "light" |
| **Predator** | Chases non-self types; gains energy from proximity to prey |
| **Reproductive** | Converts nearby recovered dust into new particles; drives population growth |

### Default Ecosystem

On every reset a **default food web** is automatically applied:

| Type | Role | Archetype |
|------|------|-----------|
| 0 — Cyan | Dust / resource | *(passive)* |
| 1 — Red | Primary producer | Photosynth |
| 2 — Green | Colonial reproducer | Adhesive + Reproductive |
| 3 — Magenta | Predator | Predator |
| 4 — Yellow | Energy catalyst | Catalyst |

This provides a working ecological loop immediately. Individual types can be overridden via
the archetype panel at any time.

### Evolution & Mutation

Evolutionary pressure emerges from a closed birth–death cycle:

1. **Death** — particles reaching zero energy revert to dust
2. **Recovery** — dust rapidly regains energy (biomass recycling)
3. **Birth** — a REPRODUCTIVE particle with energy ≥ 0.55 and a nearby recovered dust
   particle converts that dust into a new particle of its own type (up to 20 births per
   organism tick)
4. **Mutation** — 8% of births produce a random non-dust type instead of copying the parent
5. **Force row drift** — when a mutant birth occurs, the destination type's force row is
   nudged ±0.05 per entry via an LCG hash, causing types to gradually evolve their
   interaction patterns over generations

### Organism System

Every 5 frames, particles are clustered into **organisms** using DBSCAN with a
toroidal-aware spatial hash:

- Cluster radius configurable (default 40 px); all distance comparisons use shortest-path
  across the toroidal boundary
- Clusters of ≥ 3 particles become organisms with measured traits:
  - Size, average speed, type composition, dominant type, mean energy
- Organisms are matched frame-to-frame by centroid proximity (toroidal)
- **Division** detected when a tracked organism splits into two nearby clusters
- **Consumption** detected when a tracked organism grows > 20% and an unmatched neighbour
  disappears nearby
- **Lineage** tracked: generation, parent ID, kills, divisions accumulate and pass to children

### Population Statistics UI

The **Organisms** panel displays:
- Live counts: **Alive / Dust / Births / Deaths** (updated each organism tick)
- **Population bar chart** — segmented bar showing each type's share of all particles
- **Population history graph** — 300-sample rolling chart of alive particle count
- Per-type force-scale bars (trait feedback)
- Top-8 organism table: size, speed, generation, kills, divisions, energy bar

---

## Requirements

### Linux (Ubuntu / Debian)

```bash
sudo apt install libvulkan-dev vulkan-tools glslang-tools
sudo apt install libglfw3-dev libglm-dev
sudo apt install cmake g++
```

---

## Build

```bash
source ~/vulkan/<version>/setup-env.sh

mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Compiled SPIR-V shaders are placed in `build/shaders/`.
Run the binary from the build directory:

```bash
./build/particle_life
```

---

## Controls

| Input | Action |
|-------|--------|
| **F1** | Toggle settings panel |
| **F2** | Reset simulation |
| **Space** | Pause / unpause |
| **F11** | Toggle fullscreen |
| **Esc** | Quit |
| **Left drag** | Pan camera |
| **Scroll wheel** | Zoom |

**Force grid** (Particle Values section):
- Hover a cell + scroll → adjust attraction / repulsion
- Right-click a cell → zero the force

---

## Architecture

```
src/
  types.h               — Constants, SimConfig, ParticleBehavior flags
  particles.h/.cpp      — CPU particle arrays, generation, archetype presets,
                          default ecosystem setup
  vulkan_context.h/.cpp — Vulkan instance, device, swapchain, buffer/image helpers
  compute_pipeline.h/.cpp — Compute pipeline, 15-binding descriptor layout,
                            buffer lifecycle, energy readback
  renderer.h/.cpp       — Fullscreen-quad graphics pipeline, ImGui, swapchain sync
  interface.h/.cpp      — Dear ImGui panel: force grid, archetypes, population stats
  organism.h/.cpp       — Toroidal DBSCAN clustering, trait tracking, birth/death,
                          mutation, force row evolution
  simulation.h/.cpp     — Main loop, input, camera, orchestration
  main.cpp              — Entry point, GLFW window

shaders/
  compute.comp          — GPU physics (forces, energy metabolism, archetypes) +
                          particle renderer
  fullscreen.vert       — Fullscreen triangle vertex shader
  fullscreen.frag       — Samples particle render texture → swapchain
```

### Descriptor bindings (compute shader)

| Binding | Buffer | Direction |
|---------|--------|-----------|
| 0 | position (ping) | in |
| 1 | velocity (ping) | in |
| 2 | type | in (readonly) |
| 3 | force matrix | in |
| 4 | colour table | in |
| 5 | position (pong) | out |
| 6 | velocity (pong) | out |
| 7 | render texture | image write |
| 8 | behavior flags | in (readonly) |
| 9 | angle (ping) | in |
| 10 | angular velocity (ping) | in |
| 11 | angle (pong) | out |
| 12 | angular velocity (pong) | out |
| 13 | energy (ping) | in |
| 14 | energy (pong) | out |

All paired buffers (position, velocity, angle, angular velocity, energy) are A/B ping-pong
double-buffered. Behavior flags and the force matrix are shared and reuploaded each frame
via `upload_dynamic_data()`.

---

## License

Night-Traders-Dev 2026
