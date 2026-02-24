# Emergent Evolution

A GPU-accelerated quantum chemistry and particle physics simulation where real atoms form persistent
covalent and ionic bonds, undergo radioactive decay, emit Standard Model particles, and self-organise
into molecular aggregates, organic molecules, and evolving organisms.
Built with C++20, Vulkan compute shaders, and Dear ImGui.

---

## Features

### Particle Physics (GPU)

- Up to ~22,500 particles simulated in real time with O(n²) pairwise forces in a GLSL compute shader
- **Infinite world** — toroidal 2560×1440 space; particles wrap seamlessly at edges
- All four **fundamental forces** active simultaneously:
  - **Electromagnetic** — Coulomb charge interactions + Lorentz magnetic deflection (slider)
  - **Weak nuclear** — radioactive β/α decay + neutrino CEvNS scattering
  - **Strong nuclear** — Yukawa nuclear potential (sub-atomic LOD) + covalent bond springs (macro)
  - **Gravity** — optional Newtonian gravity between heavy particles (slider)
- Double-buffered ping-pong buffers: position, velocity, angle, angular velocity, energy, genome
- Configurable repulsion radius, interaction radius, dampening, density limiting, viscosity
- Soft-body pressure: exponential density falloff creates emergent elasticity

### Atom Types — 18-Element Periodic Table (CPK colours)

**Biogenic elements** (H, C, N, O, P, S), **alpha-process / stellar** (Na, Cl, Fe, Ni, Si, Ca, Ti),
and **r-process / kilonova** (Sr, Au, Pb, Eu, U):

| # | Element | Valence | Behaviour |
|---|---------|---------|-----------|
| 0 | **H** Hydrogen | 1 | Polar — dipole rotation |
| 1 | **C** Carbon | 4 | Neutral backbone |
| 2 | **N** Nitrogen | 3 | Electron donor |
| 3 | **O** Oxygen | 2 | Polar + electron acceptor |
| 4 | **P** Phosphorus | 5 | Heavy + enzymatic catalyst |
| 5 | **S** Sulfur | 2 | Heavy |
| 6 | **Na** Sodium | 1 | Heavy + ionic (+) + adhesive |
| 7 | **Cl** Chlorine | 1 | Heavy + ionic (−) + adhesive |
| 8 | **Fe** Iron | 3 | Heavy + polar (redox-active) |
| 9 | **Ni** Nickel | 2 | Heavy + catalyst — β⁺ unstable |
| 10 | **Si** Silicon | 4 | Heavy (silicate network former) |
| 11 | **Ca** Calcium | 2 | Heavy + ionic (+) |
| 12 | **Ti** Titanium | 4 | Heavy (refractory) |
| 13 | **Sr** Strontium | 2 | Heavy + ionic (+) — β⁻ unstable (→ Ca) |
| 14 | **Au** Gold | 1 | Heavy + adhesive (noble, surface-reactive) |
| 15 | **Pb** Lead | 4 | Heavy (stable decay endpoint) |
| 16 | **Eu** Europium | 3 | Heavy + radical — β⁻ unstable (→ Fe) |
| 17 | **U** Uranium | 6 | Heavy + radical + catalyst — α unstable (→ Pb) |

Spawn abundance: H 40%, C 25%, O 15%, N 10%, Na/Cl 3% each, P/S 2% each; r-process ≤ 0.5% (cosmically rare).
Initial particles spawn in **three well-separated seed clusters** arranged in a triangle, so distinct
chemistry zones develop independently before interacting.

### Standard Model Particles (types 19–23)

Beyond atoms, five fundamental-particle types emerge from radioactive decay and vacuum fluctuations:

| # | Particle | Symbol | Origin | Behaviour |
|---|----------|--------|--------|-----------|
| 19 | **Alpha** particle | α | U α-decay | He-4 nucleus; heavy, +2 charge; emitted at ~200 px/s |
| 20 | **Free Electron** | e⁻ | β⁻ decay (Sr, Eu); virtual pairs | Lepton, ionic (−); light, fast |
| 21 | **Positron** | e⁺ | β⁺ decay (Ni); virtual pairs | Lepton, ionic (+); annihilates e⁻ → 2γ |
| 22 | **Electron Neutrino** | ν | Muon decay; virtual | Near-zero interaction; CEvNS scatter off heavy nuclei |
| 23 | **Muon** | μ | Cosmic / decay chain | Heavy lepton; decays → e⁻ + ν in ~0.5 s |

SM particles can also be spawned manually via **F3 → Atoms tab**.

### Radioactive Decay Chains

A half-life stochastic decay engine runs CPU-side every 2 frames, transforming unstable heavy nuclei:

| Parent | Mode | Daughter | Product | Q-energy | γ photon |
|--------|------|----------|---------|----------|---------|
| **U** (17) | α-decay | Pb (15) | α (19) | 0.45 | yes |
| **Eu** (16) | β⁻-decay | Fe (8) | e⁻ (20) | 0.30 | yes |
| **Sr** (13) | β⁻-decay | Ca (11) | e⁻ (20) | 0.25 | yes |
| **Ni** (9) | β⁺-decay | Fe (8) | e⁺ (21) | 0.20 | no |
| **μ** (23) | μ-decay | e⁻ (20) | ν (22) | 0.15 | no |
| **e⁺+e⁻** | annihilation | — | 2 γ | 1.02 | — |

Decay half-lives are scaled to simulation-seconds (25–50 s) so chains unfold visibly.
A **Quantum Physics** panel in the settings tracks cumulative decays.

### Vacuum Energy / Quantum Field Theory

Enable the **Vacuum Energy** slider to activate quantum field effects:

- **Virtual photon pairs** — ZPE radiation; two counter-propagating γ photons spawned from random vacuum
  points. Rate: `vacuum_energy × 0.8` pairs per bond-update cycle. Photon lifetime ~2–5 s.
- **Virtual e⁺/e⁻ pairs** — fermion vacuum fluctuations; short-lived positron–electron pairs that
  annihilate back to photons on the next decay cycle. Rate: `vacuum_energy × 0.15` pairs per cycle.
- **Zero-point energy floor** — every particle gains a small baseline energy (`vacuum_energy × 0.003/s`)
  preventing complete thermodynamic death.
- **Neutrino CEvNS** — neutrinos scatter weakly off HEAVY-flagged nuclei via Coherent Elastic
  neutrino–Nucleus Scattering (coupling 0.004, ~10⁻⁴ relative to EM).

### Persistent Covalent & Ionic Bonds

Every 2 frames the CPU forms/breaks bonds using a spatial hash:

- **Bond formation** — two atoms within `bond_form_radius` with free valence slots snap together if bond-compatible
- **Bond breaking** — bond snaps when stretched beyond `rest_length × break_factor`
- **Bond spring** — `F = k_eff × extension` on GPU; `k_eff = bond_spring_k × clamp(bond_strength + 0.5, 0.2, 1.5)`
- BOND_COMPAT matrix respects chemistry: C–C/C–N/C–O/O–H (covalent), Na–Cl (ionic), Fe–O/Fe–S, Si–O, Au–S, U–O, P–O, N–H…
- Bond-formation events **emit photons** — massless energy quanta (γ, type 18) at 200 px/s

### Genome System

Each particle carries 4 float genes:

| Gene | Range | Effect |
|------|-------|--------|
| Charge | −1 → +1 | Coulomb + Lorentz weighting |
| Electronegativity | 0.2 → 2.0 | Electron-transfer energy gain |
| Reactivity | 0.2 → 2.0 | Bond-strain energy cost; boosted by nuclear instability (LOD coupling) |
| Bond strength | −0.5 → +0.5 | Spring constant scale |

### Metabolism & Energy

Each particle carries energy **0.0–1.0**, updated on the GPU each frame. Brightness reflects energy.

| Source | Rate |
|--------|------|
| Ambient gain | +0.010 / s |
| Passive drain | −0.015 / s |
| Movement cost | −speed × 0.00015 / s |
| Crowding penalty | −(density − limit) × 0.005 / s |
| Symbiotic gain | +attraction × proximity × 0.005 / pair / s |
| Catalyst boost | +0.008 × catalysts / s |
| Donor → Acceptor transfer | +electronegativity × proximity / s |
| Bond strain cost | −abs(ext)/rest × 0.002 × dt |
| ZPE floor | +vacuum_energy × 0.003 / s |

Particles at zero energy lose bonds and revert to H (type 0).

### Molecular Aggregate System

Every 5 frames, DBSCAN with spatial hashing clusters atoms into **molecular aggregates**:

| Class | Rule | Colour |
|-------|------|--------|
| **H₂O** | H > ¾ cluster, O > 1 | Light blue |
| **LIPID** | (C+H) > ⅔ cluster | Yellow |
| **AACD** amino acid | (N+O)×2 > size & C > 0 | Green |
| **NUCL** nucleotide | P×3 > size | Violet |
| **RAD!** radical | any RADICAL member | Red |
| **POLY** polymer | C > ½ cluster & size > 20 | Orange |
| **INRG** inorganic | otherwise | Grey |

Division/consumption events track **generation**, **kills**, and **divisions** counters.
Trait feedback nudges genomes toward each molecular role; decay manager clears bonds on transmutation.

### Sub-Atomic LOD

Zoom past **20×** while hovering a particle to reveal the Sub-Atomic View:

- **Nucleon view** (zoom > 20×) — Bohr-model nucleus with protons (red), neutrons (grey), and
  electrons (blue) in real shell configurations. Physics: Yukawa nuclear force + Coulomb + Pauli hard-core.
  Displays **binding energy** (accumulated Yukawa+Coulomb potential) and **nuclear stability** (0–1 scale).
  Nuclear instability nudges the hovered particle's reactivity genome upward (LOD→macro coupling).
- **Quark view** (zoom > 150×) — click a nucleon for its three quarks.
  Proton = **uud**, Neutron = **udd**.
  Physics: Cornell potential V(r) = −α/r + σr (QCD asymptotic freedom + confinement).
- All 18 elements show correct element name, symbol, and nucleus composition.
  Heavy elements (Sr–U) scale their visual nucleus proportionally (capped at 56 nucleons for performance).

### Behaviour Flags

| Flag | Bit | Atoms | Effect |
|------|-----|-------|--------|
| REPEL | 0 | — | Always repels every type |
| POLAR | 1 | H, O, Fe | Dipole rotation (angle-dependent attraction) |
| HEAVY | 2 | P, S, Na, Cl, Fe–U | Force response 0.25× — slow structural nucleus; gravity target |
| CATALYST | 3 | P, Ni, U | Boosts neighbours' energy |
| RADICAL | 4 | Eu, U | Aggressively chases all neighbours |
| ADHESIVE | 5 | Na, Cl, Au | 1.8× self-attraction bonus |
| DONOR | 6 | N | Emits electrons toward ACCEPTORs |
| ACCEPTOR | 7 | O | Absorbs electrons from DONORs |
| IONIC_POS | 8 | Na, Ca, Sr, α | Repels other cations |
| IONIC_NEG | 9 | Cl, e⁻, μ | Repels other anions; attracted to cations |
| PHOTON | 10 | γ | Massless energy carrier; skips force physics |
| ALPHA | 11 | α (19) | He-4 nucleus decay product |
| LEPTON | 12 | e⁻, μ | Free lepton; light and fast |
| NEUTRINO | 13 | ν (22) | Near-zero interaction; CEvNS only |
| POSITRON | 14 | e⁺ (21) | Annihilates with LEPTON on contact |
| MUON | 15 | μ (23) | Heavy lepton; decays to e⁻ + ν |

---

## F3 Spawn Picker

Press **F3** to open the Spawn Picker, then left-click in the world to place.
Recently placed particles are **spawn-protected** for 90 frames.

### Atoms tab

All **18 elements + 5 SM particles** in a grid with CPK colours.
Select count (×1 → ×50) and scatter radius.

### Groups tab — 14 molecule templates

| Template | Structure |
|----------|-----------|
| **H2O** | Water — O + 2H bent ~105° |
| **CH4** | Methane — C + 4H tetrahedral |
| **NaCl** | Salt — Na–Cl ionic pair |
| **NH3** | Ammonia — N + 3H trigonal pyramidal |
| **CO2** | Carbon dioxide — O=C=O linear |
| **Gly** | Glycine — N–C–C(=O) amino acid backbone |
| **C6H6** | Benzene — 6-carbon aromatic ring + 6H |
| **SiO4** | Silicate — Si + 4O tetrahedral |
| **Fe2O3** | Hematite — 2 Fe + 3O iron oxide |
| **EtOH** | Ethanol — C2H5OH |
| **CaCO3** | Calcite — Ca + C + 3O limestone |
| **Au3** | Gold trimer — 3 Au metallic nano-cluster |
| **UO2** | Uranium oxide — U + 2O nuclear fuel |
| **FeS2** | Pyrite — Fe + 2S (fool's gold) |

### Organics tab — 8 bio-molecule templates

Biological macromolecule building blocks organised by category:

**Proteins:**
| Template | Structure |
|----------|-----------|
| **Gly** | Glycine — NH₂–CH₂–COOH, simplest amino acid (8 atoms) |
| **Ala** | Alanine — NH₂–CH(CH₃)–COOH, amino acid w/ methyl side-chain (10 atoms) |

**Carbohydrates:**
| Template | Structure |
|----------|-----------|
| **Glc** | Glucose — hexose sugar ring C₆H₁₂O₆ (5C + ring-O + 5 OH, 11 atoms) |
| **Rib** | Ribose — pentose sugar ring C₅H₁₀O₄ (4C + ring-O + 4 OH, 9 atoms) |

**Lipids:**
| Template | Structure |
|----------|-----------|
| **ButAc** | Butyric acid — short fatty acid chain CH₃CH₂CH₂COOH (12 atoms) |
| **GlyP** | Glycerophosphate — lipid head group (P + 4O + 3C + N, 10 atoms) |

**Nucleic Acids:**
| Template | Structure |
|----------|-----------|
| **Ade** | Adenine — purine nucleobase, fused 6+5 ring, C₅H₅N₅ (10 atoms) |
| **Cyt** | Cytosine — pyrimidine nucleobase, 6-membered ring + NH₂, C₄H₅N₃O (8 atoms) |

### Organisms tab

- **Live organism list** — clone any detected molecular aggregate by selecting it and clicking in the world
- **Predefined templates**: Water Cluster (5× H₂O pentagon), Salt Lattice (4× NaCl), Lipid Stub (C₆ fatty acid)

---

## UI Panels

### Particle Values (Force Grid)
- N×N grid of coloured buttons for the 18×18 (extended to 26×26) force matrix
- Element symbols on row/column headers
- Hover + scroll to adjust; right-click to zero

### Particle Archetypes
Set a behaviour preset per type:
Default · Repeller · Polar · Heavy · Catalyst · Adhesive · Radical · Donor · Acceptor

### Chemical Bonds
- **Bond Form Radius** — snap distance for bond formation
- **Bond Rest Length** — equilibrium spring target
- **Bond Break Factor** — stretch multiplier before bond snaps
- **Bond Spring k** — base spring constant

### Physics Sliders
- **Temperature** — thermal noise injected each frame
- **Gravity** — Newtonian 1/r² attraction between HEAVY particles (0 = off)
- **Magnetism** — Lorentz force on charged particles from neighbours' motion (0 = off)
- **Vacuum Energy** — ZPE floor + virtual photon & fermion pair rate (0 = off)

### Organisms (Molecular Aggregates)
- Live counts: Alive / Dust / Deaths
- Population history graph (300-sample ring buffer)
- Per-type force-scale bars (trait feedback multipliers, up to 1.8×)
- Top-8 aggregate table: type · class · size · bonds · speed · gen · kills · divs · energy

### Quantum Physics Panel
Summary of all four fundamental forces + live stats:
- **Total decays** counter (cumulative radioactive events)
- **Total vacuum injections** counter (virtual particle pairs spawned)
- Decay chain reference: U→Pb+α, Eu→Fe+e⁻, Sr→Ca+e⁻, Ni→Fe+e⁺, μ→e⁻+ν, e⁺+e⁻→2γ

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

**Force grid:** hover a cell + scroll to adjust; right-click to zero.

**Sub-atomic view:** hover any particle and zoom past 20× to open the nucleon panel;
zoom past 150× and click a nucleon to drill into quark view.

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
  types.h               — Constants, SimConfig, PushConstants (84 bytes), ATOM_VALENCE[26],
                          26 behaviour flags (EM+Weak+SM), SM type-index constants
  particles.h/.cpp      — CPU arrays, CPK colours for 18 elements + 5 SM types,
                          18×18 electrochemistry force matrix, abundance-weighted spawn,
                          archetype presets, clustered seed generation
  bond_manager.h/.cpp   — CPU bond formation/breaking (spatial hash O(N)); BOND_COMPAT matrix
  decay_manager.h/.cpp  — Half-life stochastic decay; positron annihilation; muon TTL;
                          DECAY_TABLE[26]; DecayEvent queue processed in tick()
  vulkan_context.h/.cpp — Vulkan instance, device, swapchain, buffer/image helpers
  compute_pipeline.h/.cpp — 17-binding descriptor layout, buffer lifecycle,
                             bond buffer upload, energy readback
  renderer.h/.cpp       — Fullscreen-quad graphics pipeline, ImGui, swapchain sync
  interface.h/.cpp      — Dear ImGui panels: force grid, archetypes, bond/physics sliders,
                          F3 spawn picker (18 atoms + 5 SM + 14 molecule + 8 organics + organisms),
                          molecular aggregate table, Quantum Physics panel
  organism.h/.cpp       — DBSCAN clustering, MoleculeClass inference, trait feedback, lineage
  sub_atomic.h/.cpp     — Sub-atomic LOD: Bohr nucleon/electron + Cornell quark views;
                          18-element NUCLEUS/SHELLS tables; Yukawa + Coulomb + Cornell physics;
                          binding energy accumulation + nuclear stability LOD→macro coupling
  simulation.h/.cpp     — Main loop, input, camera, CPU readback, spawn protection,
                          photon/vacuum injection, decay event processing, F3 templates
  main.cpp              — Entry point, GLFW window

shaders/
  compute.comp          — GPU physics: pairwise forces, bond springs, energy metabolism,
                          ionic/POLAR/DONOR-ACCEPTOR/photon fast-paths, neutrino CEvNS,
                          gravity, Lorentz magnetic force, ZPE floor, SM particle paths
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

All paired buffers are A/B ping-pong double-buffered. The bond buffer (binding 16) is a flat
`uint32_t` array; both descriptor sets point to the same buffer (CPU writes only while GPU is idle).

### PushConstants layout (84 bytes)

| Offset | Field | Description |
|--------|-------|-------------|
| 0 | world_size | vec2 — toroidal world dimensions |
| 8 | particle_count | uint — active particle count |
| 12 | interaction_radius | float |
| 16 | repulsion_radius | float |
| 20 | repulsion_strength | float |
| 24 | damping | float |
| 28 | dt | float — timestep |
| 32 | force_matrix_stride | uint |
| 36 | density_limit | float |
| 40 | viscosity | float |
| 44 | bond_spring_k | float |
| 48 | bond_rest_length | float |
| 52 | bond_break_factor | float |
| 56 | bond_form_radius | float |
| 60 | tick_index | uint |
| 64 | temperature | float — thermal noise |
| 68 | gravity_strength | float — 0 = off |
| 72 | lorentz_strength | float — 0 = off |
| 76 | vacuum_energy | float — ZPE floor + virtual pair rate |
| 80 | (padding to 84) | |

---

## License

Night-Traders-Dev 2026
