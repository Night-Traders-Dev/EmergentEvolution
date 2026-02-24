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

**18 elements** across three groups: biogenic, alpha-process (supernova), and r-process (neutron star merger):

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
| 8 | **Fe** Iron | Rust | 3 | Heavy + polar (redox-active) |
| 9 | **Ni** Nickel | Pale green | 2 | Heavy + catalyst |
| 10 | **Si** Silicon | Sandy tan | 4 | Heavy (silicate network former) |
| 11 | **Ca** Calcium | Lime | 2 | Heavy + ionic (+) |
| 12 | **Ti** Titanium | Silver | 4 | Heavy (refractory) |
| 13 | **Sr** Strontium | Teal | 2 | Heavy + ionic (+) — first confirmed kilonova r-process product |
| 14 | **Au** Gold | Gold | 1 | Heavy + adhesive (noble, surface-reactive) |
| 15 | **Pb** Lead | Dark slate | 4 | Heavy (r-process end-point) |
| 16 | **Eu** Europium | Aqua | 3 | Heavy + radical (neutron star merger lanthanide) |
| 17 | **U** Uranium | Steel blue | 6 | Heavy + radical + catalyst (actinide, r-process endpoint) |

Spawn abundance: H 40%, C 25%, O 15%, N 10%, Na/Cl 3% each, P/S 2% each; r-process elements ≤ 0.5% (cosmically rare).

### Persistent Covalent Bonds

Every 2 frames the CPU checks for new bonds to form and overstretched bonds to break:

- **Bond formation** — two atoms within `bond_form_radius` that are compatible (BOND_COMPAT matrix)
  and have free valence slots snap together
- **Bond breaking** — a bond stretches beyond `rest_length × break_factor` and snaps
- **Bond spring force** — bonded pairs experience a Hookean restoring force on the GPU:
  `F = k_eff × extension`, where `k_eff = bond_spring_k × clamp(bond_strength + 0.5, 0.2, 1.5)`
- Bond compatibility respects real chemistry: C–C/C–N/C–O/O–H (covalent), Na–Cl (ionic), Fe–O (oxide), Si–O (silicate), etc.
- All bonds are held in a CPU-managed flat array uploaded to the GPU each frame (binding 16)

### Electrochemistry Force Matrix

The 18×18 force matrix is seeded from electronegativity and charge data:

- **O attracts H** strongly → water clusters form spontaneously
- **Na ↔ Cl** strong mutual attraction → ionic pair adhesion
- **Si strongly attracts O** → silicate tetrahedra self-assemble
- **Fe attracts O and S** → iron oxide and iron sulfide minerals
- **Au attracts S** → gold-sulfide surface chemistry
- **U attracts O** → uranium oxide nuclear fuel analog
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

| Flag | Bit | Atoms | Effect |
|------|-----|-------|--------|
| REPEL | 0 | — | Always repels every type |
| POLAR | 1 | H, O, Fe | Dipole rotation (angle-dependent attraction) |
| HEAVY | 2 | P, S, Na, Cl, Fe–U | Force response 0.25× — slow structural nucleus |
| CATALYST | 3 | P, Ni, U | Boosts neighbours' energy each frame |
| RADICAL | 4 | Eu, U | Aggressively chases all neighbours, drains energy |
| ADHESIVE | 5 | Na, Cl, Au | 1.8× self-attraction bonus |
| DONOR | 6 | N | Emits electrons toward ACCEPTOR neighbours |
| ACCEPTOR | 7 | O | Absorbs electrons from DONOR neighbours |
| IONIC_POS | 8 | Na, Ca, Sr | Ionic repulsion from other cations |
| IONIC_NEG | 9 | Cl | Ionic repulsion from other anions; attraction to cations |
| PHOTON | 10 | γ (photon) | Massless energy carrier; skips normal force physics |

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

### Photon System

Bond-formation events emit **photons** (type 18) — massless energy carriers that travel at 200 px/s
and drain 0.07 energy/s (4–8 s lifetime). Photons skip normal force physics and show as bright
yellow-white glowing dots.

### Sub-Atomic LOD (Level of Detail)

Zoom in past **20×** while hovering a particle to reveal a floating **Sub-Atomic View** panel:

- **Nucleon view** (zoom > 20×) — Bohr-model nucleus with protons (red), neutrons (grey), and electrons (blue)
  orbiting in real shell configurations. Physics: Yukawa nuclear force, Coulomb electrostatics, Pauli hard-core repulsion.
- **Quark view** (zoom > 150×) — click a nucleon to drill into its three quarks.
  Proton = **uud** (2 up + 1 down); Neutron = **udd**.
  Physics: Cornell potential V(r) = −α/r + σr (QCD-inspired asymptotic freedom + confinement).
- Heavy elements (Sr, Au, Pb, Eu, U) show a proportionally scaled nucleus (capped at 56 visual nucleons for performance).

---

## F3 Spawn Picker

Press **F3** to open the Spawn Picker, then left-click anywhere in the world to place:

### Atoms tab
All **18 element types** in a 6-wide grid with CPK colours. Select count (×1 → ×50) and scatter radius.
Recently placed particles are **spawn-protected** for 90 frames so a second click never overwrites the first.

### Groups tab — 14 molecule templates

| Template | Structure |
|----------|-----------|
| **H2O** | Water — O + 2H bent ~105° |
| **CH4** | Methane — C + 4H tetrahedral |
| **NaCl** | Salt — Na–Cl ionic pair |
| **NH3** | Ammonia — N + 3H trigonal pyramidal |
| **CO2** | Carbon dioxide — O=C=O linear |
| **Gly** | Glycine — N–C–C(=O) amino acid backbone |
| **C6H6** | Benzene — 6-carbon aromatic ring + 6 H |
| **SiO4** | Silicate — Si + 4O tetrahedral (rock-forming unit) |
| **Fe2O3** | Hematite — 2 Fe + 3O (iron oxide mineral) |
| **EtOH** | Ethanol — C2H5OH (2C + 6H + 1O) |
| **CaCO3** | Calcite — Ca + C + 3O (limestone mineral) |
| **Au3** | Gold trimer — 3 Au metallic nano-cluster |
| **UO2** | Uranium oxide — U + 2O (nuclear fuel analog) |
| **FeS2** | Pyrite — Fe + 2S (fool's gold mineral) |

### Organisms tab
- **Live organism list** — clone any detected molecular aggregate
- **Predefined templates**: Water Cluster (5× H2O pentagon), Salt Lattice (4× NaCl), Lipid Stub (C6H12O2 chain)

---

## UI Panels

### Particle Values (Force Grid)
- N×N grid of coloured buttons for the force matrix (N = active particle types)
- Element symbols overlaid on each row/column header
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

## Controls

| Input | Action |
|-------|--------|
| **F1** | Toggle settings panel |
| **F2** | Reset simulation |
| **F3** | Open / close spawn picker |
| **Space** | Pause / unpause |
| **F11** | Toggle fullscreen |
| **Esc** | Quit |
| **Left drag** | Pan camera |
| **Scroll wheel** | Zoom in/out |

**Force grid** (Particle Values section):
- Hover a cell + scroll → adjust attraction / repulsion
- Right-click a cell → zero the force

**Sub-atomic view**:
- Hover over any particle and zoom past 20× to open the Sub-Atomic panel
- Zoom past 150× and click a nucleon to switch to quark view

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

## Architecture

```
src/
  types.h               — Constants, SimConfig, ATOM_VALENCE[18], ParticleBehavior flags
  particles.h/.cpp      — CPU particle arrays, CPK colours for 18 elements,
                          18×18 electrochemistry force matrix, atom abundance spawn,
                          archetype presets
  bond_manager.h/.cpp   — CPU bond formation/breaking with spatial hash O(N);
                          BOND_COMPAT compatibility matrix; bond_partners flat array
  vulkan_context.h/.cpp — Vulkan instance, device, swapchain, buffer/image helpers
  compute_pipeline.h/.cpp — 17-binding descriptor layout, buffer lifecycle,
                            bond buffer upload, energy readback
  renderer.h/.cpp       — Fullscreen-quad graphics pipeline, ImGui, swapchain sync
  interface.h/.cpp      — Dear ImGui panels: force grid, archetypes, bond params,
                          F3 spawn picker (18 atoms + 14 molecule templates + organisms),
                          molecular aggregate table with element symbol overlays
  organism.h/.cpp       — DBSCAN clustering, MoleculeClass inference, trait feedback,
                          lineage tracking
  sub_atomic.h/.cpp     — Sub-atomic LOD: Bohr nucleon/electron view + Cornell quark view;
                          18-element NUCLEUS/SHELLS tables; Yukawa + Coulomb + Cornell physics
  simulation.h/.cpp     — Main loop, input, camera, shared CPU readback, spawn protection,
                          photon injection, F3 molecule templates, orchestration
  main.cpp              — Entry point, GLFW window

shaders/
  compute.comp          — GPU physics (pairwise forces, bond spring forces, energy
                          metabolism, ionic/radical/donor-acceptor interactions,
                          photon propagation) + particle renderer
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
