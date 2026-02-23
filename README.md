# Emergent Evolution

A GPU-accelerated molecular chemistry simulation where real atoms form persistent covalent and ionic bonds,
self-organise into molecular aggregates, and evolve their interaction patterns over time.
Built with C++20, Vulkan compute shaders, and Dear ImGui.

---

## Features

### Particle Physics
- Up to ~22,500 particles simulated in real time on the GPU
- O(n²) pairwise force calculation in a GLSL compute shader
- **Infinite world** — no boundary wrapping; particles drift freely through unbounded space
- Configurable repulsion radius, interaction radius, dampening, density limiting, and viscosity
- Soft-body pressure field: exponential density falloff creates emergent elasticity
- Double-buffered ping-pong buffers (position, velocity, angle, angular velocity, energy)

### Atom Types (CPK colours)

Particles represent the eight most biologically relevant atoms:

| Type | Element | Colour | Valence | Behaviour |
|------|---------|--------|---------|-----------|
| 0 | **H** Hydrogen | White | 1 | Polar — dipole rotation |
| 1 | **C** Carbon | Dark grey | 4 | Neutral backbone |
| 2 | **N** Nitrogen | Blue | 3 | Electron donor |
| 3 | **O** Oxygen | Red | 2 | Polar + electron acceptor |
| 4 | **P** Phosphorus | Orange | 5 | Heavy + enzymatic catalyst |
| 5 | **S** Sulfur | Yellow | 2 | Heavy |
| 6 | **Na** Sodium | Violet | 1 | Heavy + ionic (+) + adhesive |
| 7 | **Cl** Chlorine | Green | 1 | Heavy + ionic (−) + adhesive |

Spawn abundance approximates biological prevalence: H 40 %, C 25 %, O 15 %, N 10 %, Na/Cl 3 % each, P/S 2 % each.

### Persistent Covalent Bonds

Every 2 frames the CPU checks for new bonds to form and overstretched bonds to break:

- **Bond formation** — two atoms within `bond_form_radius` that are compatible (BOND_COMPAT matrix)
  and have free valence slots snap together
- **Bond breaking** — a bond stretches beyond `rest_length × break_factor` and snaps
- **Bond spring force** — bonded pairs experience a Hookean restoring force on the GPU:
  `F = k_eff × extension`, where `k_eff = bond_spring_k × clamp(bond_strength + 0.5, 0.2, 1.5)`
- Bond compatibility respects real chemistry: C–C/C–N/C–O/O–H (covalent), Na–Cl (ionic), etc.
- All bonds are held in a CPU-managed flat array uploaded to the GPU each frame (binding 16)

### Electrochemistry Force Matrix

The initial force matrix is seeded from electronegativity and charge data:

- **O attracts H** strongly → water clusters form spontaneously
- **Na ↔ Cl** strong mutual attraction → ionic pair adhesion
- **Na/Cl repel** same-charge ions → charge separation
- **C–C moderate attraction** → carbon backbone aggregation
- The matrix can be hand-tuned at runtime via the force grid

### Genome System

Each particle carries 4 float genes uploaded to the GPU:

| Index | Gene | Effect |
|-------|------|--------|
| 0 | Charge (−1 → +1) | Modulates ionic interactions |
| 1 | Electronegativity (0.2 → 2.0) | Scales electron-transfer energy gain |
| 2 | Reactivity (0.2 → 2.0) | Controls bond-strain energy cost |
| 3 | Bond strength (−0.5 → +0.5) | Scales spring constant |

Genome defaults are set per atom type with chemical accuracy and drift via trait feedback.

### Metabolism & Energy System

Each particle carries an energy value **0.0 → 1.0**, updated entirely on the GPU each frame.
Particle brightness reflects energy — dim particles are starving.

| Source | Rate |
|--------|------|
| Ambient gain (baseline) | +0.010 / s |
| Passive metabolic drain | −0.015 / s |
| Movement cost | −speed × 0.00015 / s |
| Crowding penalty | −(density − limit) × 0.005 / s |
| Symbiotic gain (cross-type attraction) | +attraction × proximity × 0.005 / pair / s |
| Catalyst neighbour boost | +0.008 × catalysts / s |
| Donor → Acceptor electron transfer | +electronegativity × proximity / s |
| Bond strain cost | −abs(extension) / rest × 0.002 × dt |
| Ionic attraction | +proximity bonus for opposite-charge pairs |
| Radical chase | +proximity × 0.004 for RADICAL atoms |

Particles that reach zero energy lose all bonding energy and revert to **H (type 0)**.

### Behaviour Flags

| Flag | Bit | Atom(s) | Effect |
|------|-----|---------|--------|
| REPEL | 0 | — | Always repels every type |
| POLAR | 1 | H, O | Dipole rotation (angle-dependent attraction) |
| HEAVY | 2 | P, S, Na, Cl | Force response 0.25× — slow structural nucleus |
| CATALYST | 3 | P | Boosts neighbours' energy each frame |
| RADICAL | 4 | — | Aggressively chases all neighbours, drains energy |
| ADHESIVE | 5 | Na, Cl | 1.8× self-attraction bonus |
| DONOR | 6 | N | Emits electrons toward ACCEPTOR neighbours |
| ACCEPTOR | 7 | O | Absorbs electrons from DONOR neighbours |
| IONIC_POS | 8 | Na | Ionic repulsion from other cations |
| IONIC_NEG | 9 | Cl | Ionic repulsion from other anions; attraction to cations |

### Molecular Aggregate System

Every 5 frames, particles are clustered using **DBSCAN** with a spatial-hash spatial index.
Clusters of ≥ 3 atoms become **molecular aggregates** classified by atom composition:

| Class | Classification Rule | Colour |
|-------|-------------------|--------|
| **H₂O** | H > ¾ cluster, O > 1 | Light blue |
| **LIPID** | (C+H) > ⅔ cluster | Yellow |
| **AACD** amino acid | (N+O) × 2 > size & C > 0 | Green |
| **NUCL** nucleotide | P × 3 > size | Violet |
| **RAD!** radical | any RADICAL member present | Red |
| **POLY** polymer | C > ½ cluster & size > 20 | Orange |
| **INRG** inorganic | otherwise | Grey |

Aggregates are matched frame-to-frame; division and consumption events are tracked and
accumulate into **generation**, **kills**, and **divisions** counters.

**Trait feedback** — each aggregate nudges its members' genomes toward their molecular role
(e.g. WATER → ↑ electronegativity; LIPID → ↑ bond strength; RADICAL → ↑ reactivity, ↓ bond strength).

---

## UI Panels

### Particle Values (Force Grid)
- N×N grid of coloured buttons for the force matrix (N = active particle types)
- Element symbols (H / C / N / O / P / S / Na / Cl) overlaid on each row/column header
- Hover + scroll to adjust a force; right-click to zero it

### Particle Archetypes
Set a behaviour preset per type (overrides behaviour flags and force row):
Default · Repeller · Polar · Heavy · Catalyst · Adhesive · Radical · Donor · Acceptor

### Chemical Bonds
Runtime sliders for bond dynamics:
- **Bond Form Radius** — distance within which atoms can bond
- **Bond Rest Length** — equilibrium bond length (spring target)
- **Bond Break Factor** — multiplier on rest length at which bond snaps
- **Bond Spring k** — base spring constant for bond force

### Organisms (Molecular Aggregates)
- Live counts: **Alive / Dust / Deaths** (updated each tick)
- Population history graph (300-sample ring buffer)
- Per-type force-scale bars (trait feedback multipliers, up to 1.8×)
- Population bar chart — each type's share of all particles
- Top-8 aggregate table: **type swatch · class · size · bonds · speed · gen · kills · divs · energy**

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
Run the binary from the project root:

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
  types.h               — Constants, SimConfig, ATOM_VALENCE, ParticleBehavior flags
  particles.h/.cpp      — CPU particle arrays, CPK colours, electrochemistry force matrix,
                          atom abundance spawn, archetype presets
  bond_manager.h/.cpp   — CPU bond formation/breaking with spatial hash O(N);
                          BOND_COMPAT compatibility matrix; bond_partners flat array
  vulkan_context.h/.cpp — Vulkan instance, device, swapchain, buffer/image helpers
  compute_pipeline.h/.cpp — 17-binding descriptor layout, buffer lifecycle,
                            bond buffer upload, energy readback
  renderer.h/.cpp       — Fullscreen-quad graphics pipeline, ImGui, swapchain sync
  interface.h/.cpp      — Dear ImGui panels: force grid, archetypes, bond params,
                          molecular aggregate table with element symbol overlays
  organism.h/.cpp       — DBSCAN clustering, MoleculeClass inference, trait feedback,
                          lineage tracking
  simulation.h/.cpp     — Main loop, input, camera, shared CPU readback, orchestration
  main.cpp              — Entry point, GLFW window

shaders/
  compute.comp          — GPU physics (pairwise forces, bond spring forces, energy
                          metabolism, ionic/radical/donor-acceptor interactions) +
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
| 15 | genome | in (readonly) |
| 16 | bond partners | in (readonly, CPU-managed) |

All paired buffers are A/B ping-pong double-buffered. The bond buffer (binding 16) is a single
flat `uint32_t` array — both descriptor sets point to the same buffer since the CPU writes it
only while the GPU is idle.

---

## License

Night-Traders-Dev 2026
