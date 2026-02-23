# Emergent Evolution

A GPU-accelerated particle life simulation with biologically-inspired emergent behaviour.
Thousands of particles interact through configurable force matrices, self-organise into
organisms, inherit traits across generations, and exhibit specialised behaviour through
typed archetypes.

Written in C++20 with Vulkan compute shaders and Dear ImGui.

---

## Features

### Particle Physics
- Up to ~22,500 particles simulated in real time on the GPU
- O(n²) pairwise force calculation in a GLSL compute shader
- Toroidal world wrapping (2560 × 1440 simulation region)
- Configurable repulsion radius, interaction radius, dampening, and density limiting
- **Energy-based metabolism system** (passive drain, movement cost, feeding gain)
- Double-buffered ping-pong position/velocity/energy buffers for data-race-free updates

### Metabolism & Energy System
Each particle maintains an energy value in the range **0.0 → 1.0**, updated entirely on the GPU:

- **Passive metabolic drain** every frame  
- **Movement cost** proportional to velocity  
- **Density penalty** in overcrowded regions  
- **Energy gain from interactions**:
  - Attraction-based feeding (symbiosis)
  - Scavenging dead particles
  - Predation on nearby different types  
- Particles with zero energy become “dust” (type 0) and drift until consumed  
- Particle colour brightness reflects current energy

This system creates natural ecological pressures, enabling stable food webs, predator–prey
dynamics, scavengers, and energy-driven clustering.

### Force Matrix
- Up to 10 particle types, each pair with an independent attraction/repulsion scalar
- Interactive grid: hover + scroll to adjust force, right-click to zero
- Per-type colour pickers
- Trait feedback: organisms with kill/division history amplify their type's forces (up to 1.8×)

### Particle Archetypes
Six behaviours selectable per type from the **Particle Archetypes** panel:

| Archetype | Shader flag | Effect |
|-----------|-------------|--------|
| **Default** | — | Force matrix only |
| **Repeller** | `REPEL` | Overrides force matrix — always repels all types; models toxins or charged ions |
| **Polar** | `POLAR` | Magnetic dipole: attraction modulated by relative dipole alignment; chains form; north pole rendered as yellow dot, south hemisphere tinted blue |
| **Heavy** | `HEAVY` | Force response scaled to 0.25×; acts as a structural nucleus that barely moves |
| **Catalyst** | `CATALYST` | Nearby particles lose less energy each step; models enzymes or metabolic boosters |
| **Membrane** | — | Force matrix preset only: strong self-attraction (+0.7), repels others (−0.4); spontaneously forms rings and bilayer sheets |
| **Viral** | `VIRAL` (CPU) | Converts the type of adjacent non-viral particles every 5 frames; infection spreads |

Archetype presets also seed sensible force-matrix defaults, which the user can then hand-edit.

### Organism System
Particles are clustered into organisms every 5 frames using a spatial hash + union-find algorithm:

- **Cluster radius** is configurable (default 40 px)
- Clusters of ≥ 3 particles become organisms with measured traits:
  - Size, average speed, type composition, dominant type
- Organisms are matched frame-to-frame by centroid proximity
- **Division** detected when one tracked organism becomes two nearby clusters
- **Consumption** detected when a tracked organism grows > 20% and an unmatched neighbour disappears
- **Trait inheritance**: kills and division counts accumulate and are passed to children
- **Trait feedback**: kill bonuses (+0.1 per kill, max +0.5) and division bonuses (+0.03 per division, max +0.3) scale the force row of the dominant type, feeding back into GPU physics the next frame

The **Organisms** panel shows active organism count, per-type force-scale bars, and a top-8 table (size, speed, generation, kills, divisions).

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
Run the binary from the build directory so it can locate them:

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
- Hover a cell + scroll → adjust attraction/repulsion
- Right-click a cell → zero the force

---

## Architecture

```
src/
  types.h               — Constants, PushConstants, SimConfig, ParticleBehavior flags
  particles.h/.cpp      — CPU particle arrays, random generation, archetype presets
  vulkan_context.h/.cpp — Vulkan instance, device, swapchain, buffer/image helpers
  compute_pipeline.h/.cpp — Compute pipeline, 15-binding descriptor layout, buffer lifecycle
  renderer.h/.cpp       — Fullscreen-quad graphics pipeline, ImGui, swapchain sync
  interface.h/.cpp      — Dear ImGui panel: force grid, archetypes, organism monitor
  organism.h/.cpp       — Spatial-hash clustering, trait tracking, viral infection
  simulation.h/.cpp     — Main loop, input, camera, orchestration
  main.cpp              — Entry point, GLFW window

shaders/
  compute.comp          — GPU physics (forces, metabolism, archetypes) + particle render
  fullscreen.vert       — Fullscreen triangle
  fullscreen.frag       — Samples particle texture → swapchain
```

### Descriptor bindings (compute shader)

| Binding | Buffer | Direction |
|---------|--------|-----------|
| 0 | position (ping) | in |
| 1 | velocity (ping) | in |
| 2 | type | in |
| 3 | force matrix | in |
| 4 | colour table | in |
| 5 | position (pong) | out |
| 6 | velocity (pong) | out |
| 7 | render texture | image write |
| 8 | behavior flags | in |
| 9 | angle (ping) | in |
| 10 | angular velocity (ping) | in |
| 11 | angle (pong) | out |
| 12 | angular velocity (pong) | out |
| 13 | energy (ping) | in |
| 14 | energy (pong) | out |

Position, velocity, angle, angular velocity, and energy buffers are double-buffered (A/B ping-pong).
Behavior flags and the force matrix are shared and reuploaded each frame.

---

## License

Night-Traders-Dev 2026
