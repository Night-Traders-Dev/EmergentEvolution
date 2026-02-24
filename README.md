<div align="center">

# ⚛ Emergent Evolution

**A GPU-accelerated quantum chemistry and particle physics sandbox**

Real atoms · Persistent bonds · Radioactive decay · Standard Model particles · Emergent life

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-NTD--2026-green.svg)]()

</div>

---

Emergent Evolution simulates up to **22,500 particles** in real time using Vulkan compute shaders.
Eighteen elements from hydrogen to uranium form persistent covalent and ionic bonds, undergo
radioactive decay, emit Standard Model particles, and spontaneously self-organise into molecular
aggregates, organic molecules, and evolving proto-organisms — all governed by the same four
fundamental forces found in nature.

---

## Table of Contents

- [Physics Engine](#physics-engine)
- [Periodic Table](#periodic-table--18-elements)
- [Standard Model Particles](#standard-model-particles)
- [Radioactive Decay](#radioactive-decay)
- [Quantum Field Effects](#quantum-field-effects)
- [Chemistry & Bonding](#chemistry--bonding)
- [Organisms & Evolution](#organisms--evolution)
- [Sub-Atomic LOD](#sub-atomic-lod)
- [F3 Lab Spawn Picker](#f3-lab-spawn-picker)
- [UI Reference](#ui-reference)
- [Controls](#controls)
- [Build](#build)
- [Architecture](#architecture)

---

## Physics Engine

The simulation runs entirely on the GPU via a single GLSL compute shader dispatched each frame.

| Property | Detail |
|---|---|
| Particle count | Up to **22,500** simultaneous particles |
| Force algorithm | O(n²) pairwise, per-frame on GPU |
| World | Toroidal 2560 × 1440 (seamless wrap) |
| Buffers | Double-buffered ping-pong (position, velocity, angle, ω, energy, genome) |

**All four fundamental forces are active simultaneously:**

| Force | Implementation |
|---|---|
| **Electromagnetic** | Coulomb charge interactions + Lorentz magnetic deflection |
| **Weak nuclear** | Stochastic β/α radioactive decay + neutrino CEvNS scattering |
| **Strong nuclear** | Yukawa nuclear potential (sub-atomic LOD) · covalent bond springs (macro) |
| **Gravity** | Optional Newtonian 1/r² between heavy particles — tunable via slider |

Each particle carries an **energy value (0–1)** updated every frame. Brightness reflects energy.
Particles that reach zero energy lose all bonds and revert to hydrogen.

---

## Periodic Table — 18 Elements

Three nucleosynthesis groups spanning the periodic table, each with distinct electrochemistry:

<table>
<thead><tr><th>#</th><th>Element</th><th>Group</th><th>Valence</th><th>Special Behaviour</th></tr></thead>
<tbody>
<tr><td>0</td><td><b>H</b> Hydrogen</td><td>Biogenic</td><td>1</td><td>Polar — dipole rotation</td></tr>
<tr><td>1</td><td><b>C</b> Carbon</td><td>Biogenic</td><td>4</td><td>Neutral backbone</td></tr>
<tr><td>2</td><td><b>N</b> Nitrogen</td><td>Biogenic</td><td>3</td><td>Electron donor</td></tr>
<tr><td>3</td><td><b>O</b> Oxygen</td><td>Biogenic</td><td>2</td><td>Polar + electron acceptor</td></tr>
<tr><td>4</td><td><b>P</b> Phosphorus</td><td>Biogenic</td><td>5</td><td>Heavy · enzymatic catalyst</td></tr>
<tr><td>5</td><td><b>S</b> Sulfur</td><td>Biogenic</td><td>2</td><td>Heavy</td></tr>
<tr><td>6</td><td><b>Na</b> Sodium</td><td>Stellar</td><td>1</td><td>Heavy · ionic (+) · adhesive</td></tr>
<tr><td>7</td><td><b>Cl</b> Chlorine</td><td>Stellar</td><td>1</td><td>Heavy · ionic (−) · adhesive</td></tr>
<tr><td>8</td><td><b>Fe</b> Iron</td><td>Stellar</td><td>3</td><td>Heavy · polar · redox-active</td></tr>
<tr><td>9</td><td><b>Ni</b> Nickel</td><td>Stellar</td><td>2</td><td>Heavy · catalyst · β⁺ unstable → Fe</td></tr>
<tr><td>10</td><td><b>Si</b> Silicon</td><td>Stellar</td><td>4</td><td>Heavy · silicate network former</td></tr>
<tr><td>11</td><td><b>Ca</b> Calcium</td><td>Stellar</td><td>2</td><td>Heavy · ionic (+)</td></tr>
<tr><td>12</td><td><b>Ti</b> Titanium</td><td>Stellar</td><td>4</td><td>Heavy · refractory</td></tr>
<tr><td>13</td><td><b>Sr</b> Strontium</td><td>r-process</td><td>2</td><td>Heavy · ionic · β⁻ unstable → Ca</td></tr>
<tr><td>14</td><td><b>Au</b> Gold</td><td>r-process</td><td>1</td><td>Heavy · adhesive · noble</td></tr>
<tr><td>15</td><td><b>Pb</b> Lead</td><td>r-process</td><td>4</td><td>Heavy · stable decay endpoint</td></tr>
<tr><td>16</td><td><b>Eu</b> Europium</td><td>r-process</td><td>3</td><td>Heavy · radical · β⁻ unstable → Fe</td></tr>
<tr><td>17</td><td><b>U</b> Uranium</td><td>r-process</td><td>6</td><td>Heavy · radical · catalyst · α unstable → Pb</td></tr>
</tbody>
</table>

> **Spawn abundance** — H 40% · C 25% · O 15% · N 10% · Na/Cl 3% · P/S 2% · r-process ≤ 0.5%
>
> Particles initialise in **three well-separated seed clusters** (triangle formation) so distinct
> chemistry zones evolve independently before merging.

---

## Standard Model Particles

Five fundamental particle types emerge naturally from decay chains and vacuum fluctuations,
or can be placed manually via **F3 → Atoms**:

| # | Particle | Symbol | Source | Behaviour |
|---|---|---|---|---|
| 19 | **Alpha** | α | U α-decay | He-4 nucleus · heavy · +2 charge · ~200 px/s |
| 20 | **Free Electron** | e⁻ | β⁻ decay · virtual pairs | Lepton · ionic (−) · fast |
| 21 | **Positron** | e⁺ | β⁺ decay · virtual pairs | Lepton · ionic (+) · annihilates e⁻ → 2γ |
| 22 | **Electron Neutrino** | ν | μ decay · vacuum | Near-zero interaction · CEvNS scatter |
| 23 | **Muon** | μ | Decay chain | Heavy lepton · decays → e⁻ + ν in ~0.5 s |

---

## Radioactive Decay

A half-life stochastic engine runs CPU-side every 2 frames. Unstable isotopes transmute
probabilistically; daughter particles are injected into the simulation with conserved momentum.

| Parent | Mode | Daughter | Emitted | Q-value | γ ray |
|---|---|---|---|---|---|
| **U** (17) | α-decay | Pb (15) | α (19) | 0.45 | ✓ |
| **Eu** (16) | β⁻-decay | Fe (8) | e⁻ (20) | 0.30 | ✓ |
| **Sr** (13) | β⁻-decay | Ca (11) | e⁻ (20) | 0.25 | ✓ |
| **Ni** (9) | β⁺-decay | Fe (8) | e⁺ (21) | 0.20 | — |
| **μ** (23) | μ-decay | e⁻ (20) | ν (22) | 0.15 | — |
| **e⁺ + e⁻** | annihilation | — | 2 γ | 1.02 | — |

Half-lives are scaled to 25–50 simulation-seconds so chains unfold visibly at runtime.
Cumulative decay events are displayed in the **Quantum Physics** panel.

---

## Quantum Field Effects

Enable the **Vacuum Energy** slider to activate QFT-inspired vacuum fluctuations:

- **Virtual photon pairs** — Zero-point energy radiation. Two counter-propagating γ photons emerge
  from random vacuum points at rate `vacuum_energy × 0.8` pairs/cycle. Lifetime ~2–5 s.

- **Virtual e⁺/e⁻ pairs** — Fermion pair production. Short-lived positron–electron pairs that
  annihilate back to photons on the next decay cycle. Rate: `vacuum_energy × 0.15` pairs/cycle.

- **ZPE energy floor** — Every particle receives a baseline energy gain of `vacuum_energy × 0.003/s`,
  preventing complete thermodynamic death.

- **Neutrino CEvNS** — Neutrinos scatter weakly off heavy nuclei via Coherent Elastic
  neutrino–Nucleus Scattering (coupling constant 0.004, ~10⁻⁴ relative to EM). Always active.

---

## Chemistry & Bonding

### Persistent Bonds

Every 2 frames the CPU bond manager (spatial hash, O(N)) evaluates the particle population:

- **Formation** — Two compatible atoms within `bond_form_radius` with free valence slots snap together
- **Breaking** — A bond stretched beyond `rest_length × break_factor` snaps and emits a photon
- **Spring force** — `F = k_eff × extension` where `k_eff = bond_spring_k × clamp(bond_str + 0.5, 0.2, 1.5)`

Bond compatibility (`BOND_COMPAT`) respects real chemistry:
`C–C · C–N · C–O · O–H` (covalent) · `Na–Cl` (ionic) · `Fe–O · Fe–S · Si–O · Au–S · U–O · P–O · N–H`

### Genome

Each particle carries four float genes passed to the GPU each frame:

| Gene | Range | Effect |
|---|---|---|
| Charge | −1.0 → +1.0 | Coulomb + Lorentz weighting |
| Electronegativity | 0.2 → 2.0 | Electron-transfer energy yield |
| Reactivity | 0.2 → 2.0 | Bond-strain cost; coupled to nuclear stability |
| Bond strength | −0.5 → +0.5 | Spring constant multiplier |

### Energy Metabolism

| Source | Rate |
|---|---|
| Ambient gain | +0.010 / s |
| Passive drain | −0.015 / s |
| Movement cost | −speed × 0.00015 / s |
| Crowding penalty | −(density − limit) × 0.005 / s |
| Symbiotic gain | +attraction × proximity × 0.005 / pair / s |
| Catalyst boost | +0.008 × neighbour catalysts / s |
| Donor → Acceptor transfer | +electronegativity × proximity / s |
| Bond strain cost | −\|ext\|/rest × 0.002 / s |
| ZPE floor | +vacuum_energy × 0.003 / s |

---

## Organisms & Evolution

Every 5 frames, DBSCAN clustering (spatial hash) groups bonded atoms into **molecular aggregates**.
Each aggregate is classified, tracked across frames, and evolves via trait feedback.

| Class | Classification Rule | Colour |
|---|---|---|
| **H₂O** | H > ¾ cluster & O > 1 | Light blue |
| **LIPID** | (C+H) > ⅔ cluster | Yellow |
| **AACD** amino acid | (N+O)×2 > size & C > 0 | Green |
| **NUCL** nucleotide | P×3 > size | Violet |
| **RAD!** radical | any RADICAL member | Red |
| **POLY** polymer | C > ½ cluster & size > 20 | Orange |
| **INRG** inorganic | otherwise | Grey |

Aggregates accumulate **generation**, **kills**, and **divisions** counters across their lineage.
**Trait feedback** nudges each member's genome toward the chemical role of the aggregate class
(e.g. WATER → ↑ electronegativity · LIPID → ↑ bond strength · RADICAL → ↑ reactivity).

**Darwinian evolution operates on three levels:**

| Mechanism | Detail |
|---|---|
| **Division mutation** | On each detected division, ±3% random drift is applied to every member's electronegativity and reactivity genome values — heritable, stochastic variation |
| **Fitness-driven force adaptation** | Each organism update, the top-3 organisms by fitness score (kills × 3 + divisions × 2 + energy × 10 + size × 0.01) nudge their dominant type's self-cohesion force +0.004 toward attraction — successful types gradually form tighter clusters |
| **Trait-scale amplification** | Bond-rich, large organisms boost their type's entire force row by up to **2.5×** (raised from 1.8×), widening the gap between thriving and marginal types |

**Force randomness** (UI slider, default 0.25) blends random variation into the chemistry defaults on reset.
At 0 every run is identical; at 0.25 each seed produces a unique force landscape — different attractors,
different winning strategies, genuinely distinct evolutionary paths.

---

## Sub-Atomic LOD

Hover over any particle and zoom in to drill down through three levels of detail:

| Zoom | View | Physics |
|---|---|---|
| **1×–20×** | Normal macro particle | Full pairwise force simulation |
| **> 20×** | **Nucleon view** — Bohr-model nucleus with protons (red), neutrons (grey), electrons (blue) in real shell configurations | Yukawa nuclear + Coulomb + Pauli hard-core repulsion · Binding energy + nuclear stability displayed · Instability feeds back to macro reactivity genome |
| **> 150×** | **Quark view** — click any nucleon to see its three constituent quarks (p = uud · n = udd) | Cornell potential V(r) = −α/r + σr · models QCD confinement + asymptotic freedom |

All 18 elements show correct name, symbol, proton/neutron count, and electron shell configuration.
Heavy elements (Sr → U) cap at 56 visual nucleons for performance.

---

## F3 Lab Spawn Picker

Press **F3** to open the Spawn Picker. Select a template, then **left-click in the world** to place it.
Newly placed particles are **spawn-protected** (90 frames normally · 600 frames in lab mode) so
subsequent clicks never recycle them.

**Lab mode** (`Start Empty` checkbox): the simulation resets to a completely blank world with a
fixed 10,000-particle capacity. The particle-count slider is hidden — capacity is unlimited up to
that pool. Background auto-spawn is disabled so manual placements are never overwritten.
The **Particle Types** slider remains active to control how many element types appear in the
force matrix.

### Atoms

All **18 elements + 5 SM particles** in a colour-coded grid. Configurable count (×1 – ×50) and scatter radius.

### Groups — 14 Inorganic / Small-Molecule Templates

| | Template | Structure |
|---|---|---|
| 💧 | **H2O** | Water — O + 2H, bent ~105° |
| ⛽ | **CH4** | Methane — C + 4H, tetrahedral |
| 🧂 | **NaCl** | Salt — Na–Cl ionic pair |
| 💨 | **NH3** | Ammonia — N + 3H, trigonal pyramidal |
| 🌬 | **CO2** | Carbon dioxide — O=C=O, linear |
| 🧬 | **Gly** | Glycine — N–C–C(=O) amino acid backbone |
| 🔷 | **C6H6** | Benzene — aromatic 6-ring + 6H |
| 🪨 | **SiO4** | Silicate — Si + 4O tetrahedral unit |
| 🔩 | **Fe2O3** | Hematite — 2 Fe + 3O iron oxide |
| 🍺 | **EtOH** | Ethanol — C2H5OH |
| 🪨 | **CaCO3** | Calcite — Ca + C + 3O limestone |
| ✨ | **Au3** | Gold trimer — 3 Au metallic nano-cluster |
| ☢️ | **UO2** | Uranium oxide — U + 2O nuclear fuel |
| 💛 | **FeS2** | Pyrite — Fe + 2S (fool's gold) |

### Organics — 8 Bio-Molecule Templates

<table>
<thead><tr><th>Category</th><th>Template</th><th>Structure</th></tr></thead>
<tbody>
<tr><td rowspan="2"><b>Proteins</b></td>
  <td><b>Gly</b></td><td>Glycine — NH₂–CH₂–COOH, simplest amino acid (8 atoms)</td></tr>
<tr><td><b>Ala</b></td><td>Alanine — NH₂–CH(CH₃)–COOH, methyl side-chain (10 atoms)</td></tr>
<tr><td rowspan="2"><b>Carbohydrates</b></td>
  <td><b>Glc</b></td><td>Glucose — hexose ring C₆H₁₂O₆ (5C + ring-O + 5 OH, 11 atoms)</td></tr>
<tr><td><b>Rib</b></td><td>Ribose — pentose ring C₅H₁₀O₄ (4C + ring-O + 4 OH, 9 atoms)</td></tr>
<tr><td rowspan="2"><b>Lipids</b></td>
  <td><b>ButAc</b></td><td>Butyric acid — short fatty acid CH₃CH₂CH₂COOH (12 atoms)</td></tr>
<tr><td><b>GlyP</b></td><td>Glycerophosphate — lipid head group P + 4O + 3C + N (10 atoms)</td></tr>
<tr><td rowspan="2"><b>Nucleic Acids</b></td>
  <td><b>Ade</b></td><td>Adenine — purine fused 6+5 ring C₅H₅N₅ (10 atoms)</td></tr>
<tr><td><b>Cyt</b></td><td>Cytosine — pyrimidine 6-ring + NH₂ C₄H₅N₃O (8 atoms)</td></tr>
</tbody>
</table>

### Organisms

Clone any live molecular aggregate, or place one of three predefined templates:
**Water Cluster** (5× H₂O pentagon) · **Salt Lattice** (4× NaCl) · **Lipid Stub** (C₆ fatty acid)

---

## UI Reference

<details>
<summary><b>Generation Settings</b></summary>

| Control | Effect |
|---|---|
| Particle Count | Pool size (hidden in lab mode — fixed at 10,000) |
| Particle Types | How many element types appear in the force matrix (1–18) |
| Reset Colors on next run | Restore CPK palette on reset |
| Reset Forces on next run | Re-randomise the force matrix on reset |
| **Force Randomness** | 0–1 blend from pure chemistry defaults to pure random forces; 0.2–0.4 recommended for emergent dynamics |
| Seed | Deterministic RNG seed — same seed + same Force Randomness → identical run |
| Start Empty (lab mode) | Clear world, fixed 10,000-particle pool, no auto-spawn |

</details>

<details>
<summary><b>Particle Values (Force Grid)</b></summary>

26×26 interaction matrix rendered as a colour-coded button grid. Element symbols on headers.
- **Hover + scroll** — adjust attraction / repulsion
- **Right-click** — zero the force

</details>

<details>
<summary><b>Particle Archetypes</b></summary>

Nine behaviour presets per particle type:
`Default` · `Repeller` · `Polar` · `Heavy` · `Catalyst` · `Adhesive` · `Radical` · `Donor` · `Acceptor`

</details>

<details>
<summary><b>Chemical Bonds</b></summary>

| Slider | Effect |
|---|---|
| Bond Form Radius | Distance within which compatible atoms snap together |
| Bond Rest Length | Equilibrium spring length |
| Bond Break Factor | Stretch multiplier at which bond snaps |
| Bond Spring k | Base spring constant |

</details>

<details>
<summary><b>Physics Sliders</b></summary>

| Slider | Range | Default | Effect |
|---|---|---|---|
| Temperature | 0 – 2 | 0.3 | Thermal noise per frame |
| Gravity | 0 – 5 | 0 | Newtonian 1/r² between heavy particles |
| Magnetism | 0 – 2 | 0 | Lorentz force on charged particle pairs |
| Vacuum Energy | 0 – 1 | 0 | ZPE floor + virtual photon/fermion pair rate |

</details>

<details>
<summary><b>Organisms Panel</b></summary>

- Live aggregate counts: **Alive / Dust / Deaths**
- Population history graph (300-frame ring buffer)
- Per-type trait-feedback force-scale bars (max 2.5×)
- Top-8 table: type · class · size · bonds · speed · gen · kills · divs · energy

</details>

<details>
<summary><b>Quantum Physics Panel</b></summary>

Live stats for all four forces:
- Cumulative **radioactive decay** events
- Cumulative **vacuum pair injections**
- Decay chain reference: `U→Pb+α` · `Eu→Fe+e⁻` · `Sr→Ca+e⁻` · `Ni→Fe+e⁺` · `μ→e⁻+ν` · `e⁺+e⁻→2γ`
- Gauge interaction summary (EM · Weak · Strong · Gravity)

</details>

---

## Controls

| Key / Input | Action |
|---|---|
| `F1` | Toggle settings panel |
| `F2` | Reset simulation |
| `F3` | Open / close Spawn Picker |
| `Space` | Pause / unpause |
| `F11` | Toggle fullscreen |
| `Esc` | Quit |
| `W A S D` | Pan camera |
| Left drag | Pan camera (mouse) |
| Scroll wheel | Zoom in / out |

> **Force grid** — hover a cell and scroll to tune; right-click to zero it.
>
> **Sub-atomic view** — hover a particle and zoom past 20× for the nucleon panel;
> zoom past 150× and click a nucleon to enter quark view.

---

## Build

### Dependencies (Ubuntu / Debian)

```bash
sudo apt install libvulkan-dev vulkan-tools glslang-tools
sudo apt install libglfw3-dev libglm-dev cmake g++
```

### Compile

```bash
# Load Vulkan SDK environment (adjust version as needed)
source ~/vulkan/1.4.341.1/setup-env.sh

mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Compiled SPIR-V shaders are written to `build/shaders/`.

### Run

```bash
./build/particle_life
```

---

## Architecture

```
EmergentEvolution/
├── src/
│   ├── types.h               # SimConfig · PushConstants (84 b) · ATOM_VALENCE[26] · behaviour flags
│   ├── particles.h/.cpp      # CPU arrays · CPK colours · electrochemistry force matrix · genome defaults
│   ├── bond_manager.h/.cpp   # Spatial-hash bond formation/breaking · BOND_COMPAT matrix
│   ├── decay_manager.h/.cpp  # Stochastic half-life decay · annihilation · DECAY_TABLE[26]
│   ├── organism.h/.cpp       # DBSCAN clustering · MoleculeClass inference · trait feedback · lineage
│   ├── sub_atomic.h/.cpp     # Sub-atomic LOD · Bohr nucleon/electron · Cornell quark · LOD↔macro coupling
│   ├── vulkan_context.h/.cpp # Vulkan instance · device · swapchain · buffer helpers
│   ├── compute_pipeline.h/.cpp # 17-binding descriptor layout · buffer lifecycle · readback
│   ├── renderer.h/.cpp       # Fullscreen-quad pipeline · ImGui integration · swapchain sync
│   ├── interface.h/.cpp      # All Dear ImGui panels · F3 spawn picker · quantum physics panel
│   ├── simulation.h/.cpp     # Main loop · input · camera · readback · spawn protection · orchestration
│   └── main.cpp              # Entry point · GLFW window
├── shaders/
│   ├── compute.comp          # GPU physics: forces · bonds · metabolism · SM particle paths · all 4 forces
│   ├── fullscreen.vert       # Fullscreen triangle vertex shader
│   └── fullscreen.frag       # Particle texture → swapchain blit
└── CMakeLists.txt
```

### Compute Shader Bindings

| Binding | Buffer | R/W |
|---|---|---|
| 0 | position A (ping) | read |
| 1 | velocity A (ping) | read |
| 2 | type | read |
| 3 | force matrix | read |
| 4 | colour table | read |
| 5 | position B (pong) | write |
| 6 | velocity B (pong) | write |
| 7 | render texture | image write |
| 8 | behaviour flags | read |
| 9–12 | angle / angular velocity A+B | read/write |
| 13 | energy A (ping) | read |
| 14 | energy B (pong) | write |
| 15 | genome | read |
| 16 | bond partners | read (CPU-managed) |

A/B buffers ping-pong each tick. The bond buffer (binding 16) is shared across both descriptor sets
since the CPU writes it only while the GPU is idle.

---

<div align="center">

Made with C++20, Vulkan & Dear ImGui · Night-Traders-Dev 2026

</div>
