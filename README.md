<div align="center">

# Emergent Evolution

**A GPU-accelerated quantum chemistry and particle physics sandbox**

Real atoms · Standard Model particles · Nuclear fusion & fission · Orbital mechanics · Emergent life

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-NTD--2026-green.svg)]()

</div>

---

Emergent Evolution ships two simulation modes sharing the same Vulkan compute engine:

- **Particle Chemistry** (`particle_life`) — 18 elements with persistent covalent/ionic bonds,
  molecular aggregates, vesicles, proto-cells, and Darwinian evolution
- **Particle Physics** (`particle_physics`) — 30 Standard Model particle types with real quantum
  mechanics: Coulomb + Yukawa + QCD forces, centrifugal barrier orbitals, nuclear fusion/fission,
  radioactive decay, and five independent field visualizations

Both simulate up to **22,500 particles** in real time on a toroidal 2560 x 1440 world using O(n^2)
pairwise GPU compute shaders.

---

## Table of Contents

- [Physics Engine](#physics-engine)
- [Particle Physics Mode](#particle-physics-mode)
  - [Standard Model — 30 Particle Types](#standard-model--30-particle-types)
  - [Four Fundamental Forces](#four-fundamental-forces)
  - [Orbital Mechanics](#orbital-mechanics)
  - [Nuclear Fusion](#nuclear-fusion)
  - [Nuclear Fission](#nuclear-fission)
  - [Radioactive Decay](#radioactive-decay)
  - [Field Visualization](#field-visualization)
  - [Environment Presets](#environment-presets-physics)
  - [Spawn Picker — Physics](#spawn-picker--physics)
- [Particle Chemistry Mode](#particle-chemistry-mode)
  - [Periodic Table — 18 Elements](#periodic-table--18-elements)
  - [Environment Templates](#environment-templates)
  - [Chemistry & Bonding](#chemistry--bonding)
  - [Aggregates & Cells](#aggregates--cells)
  - [Spawn Picker — Chemistry](#spawn-picker--chemistry)
- [Controls](#controls)
- [Build](#build)
- [Architecture](#architecture)

---

## Physics Engine

Both modes share the same Vulkan compute pipeline dispatched each frame.

| Property | Detail |
|---|---|
| Particle count | Up to **22,500** simultaneous particles |
| Force algorithm | O(n^2) pairwise, per-frame on GPU |
| World | Toroidal 2560 x 1440 (seamless wrap) |
| Buffers | Double-buffered ping-pong (position, velocity, angle, angular velocity, energy, genome) |
| Genome | 4 floats per particle: charge, spin, color charge / orbital L, decay rate |

---

# Particle Physics Mode

A Standard Model particle sandbox where protons, neutrons, electrons, quarks, and gauge bosons
interact through all four fundamental forces. Electrons orbit nuclei via quantum-mechanical
centrifugal barriers, nucleons fuse under extreme temperature and pressure, and heavy nuclei
undergo fission when struck by fast neutrons.

---

## Standard Model — 30 Particle Types

<table>
<thead><tr><th>Family</th><th>#</th><th>Particle</th><th>Mass (inv)</th><th>Charge</th><th>Spin</th><th>Notes</th></tr></thead>
<tbody>
<tr><td rowspan="3"><b>Nucleons</b></td>
  <td>0</td><td><b>Proton</b> p</td><td>0.025</td><td>+1</td><td>+0.5</td><td>Stable</td></tr>
<tr><td>1</td><td><b>Neutron</b> n</td><td>0.025</td><td>0</td><td>-0.5</td><td>Stable (bound)</td></tr>
<tr><td>5</td><td><b>Antiproton</b> p&#773;</td><td>0.025</td><td>-1</td><td>-0.5</td><td>Annihilates with p</td></tr>
<tr><td rowspan="6"><b>Gen-1 Leptons</b></td>
  <td>2</td><td><b>Electron</b> e&#8315;</td><td>1.0</td><td>-1</td><td>+0.5</td><td>Stable, orbits nuclei</td></tr>
<tr><td>4</td><td><b>Positron</b> e&#8314;</td><td>1.0</td><td>+1</td><td>-0.5</td><td>Annihilates with e&#8315;</td></tr>
<tr><td>6</td><td><b>Electron Neutrino</b> &nu;e</td><td>100.0</td><td>0</td><td>+0.5</td><td>Near-zero interaction</td></tr>
<tr><td>3</td><td><b>Photon</b> &gamma;</td><td>100.0</td><td>0</td><td>+1</td><td>Decays over time</td></tr>
<tr><td>7</td><td><b>Muon</b> &mu;&#8315;</td><td>0.005</td><td>-1</td><td>+0.5</td><td>Decays to e&#8315; + &nu;</td></tr>
<tr><td>8</td><td><b>Anti-muon</b> &mu;&#8314;</td><td>0.005</td><td>+1</td><td>-0.5</td><td>Decays to e&#8314; + &nu;</td></tr>
<tr><td rowspan="4"><b>Gen-2/3 Leptons</b></td>
  <td>9</td><td><b>Tau</b> &tau;&#8315;</td><td>0.0003</td><td>-1</td><td>+0.5</td><td>Instant decay</td></tr>
<tr><td>10</td><td><b>Anti-tau</b> &tau;&#8314;</td><td>0.0003</td><td>+1</td><td>-0.5</td><td>Instant decay</td></tr>
<tr><td>11</td><td><b>Muon Neutrino</b> &nu;&mu;</td><td>100.0</td><td>0</td><td>+0.5</td><td>Near-zero interaction</td></tr>
<tr><td>12</td><td><b>Tau Neutrino</b> &nu;&tau;</td><td>100.0</td><td>0</td><td>+0.5</td><td>Near-zero interaction</td></tr>
<tr><td rowspan="6"><b>Quarks</b></td>
  <td>13</td><td><b>Up</b> u</td><td>0.2</td><td>+2/3</td><td>+0.5</td><td>Stable, confined</td></tr>
<tr><td>14</td><td><b>Down</b> d</td><td>0.2</td><td>-1/3</td><td>-0.5</td><td>Stable, confined</td></tr>
<tr><td>15</td><td><b>Strange</b> s</td><td>0.05</td><td>-1/3</td><td>-0.5</td><td>Slow decay</td></tr>
<tr><td>16</td><td><b>Charm</b> c</td><td>0.002</td><td>+2/3</td><td>+0.5</td><td>Fast decay</td></tr>
<tr><td>17</td><td><b>Top</b> t</td><td>0.000003</td><td>+2/3</td><td>+0.5</td><td>Instant decay</td></tr>
<tr><td>18</td><td><b>Bottom</b> b</td><td>0.0005</td><td>-1/3</td><td>-0.5</td><td>Fast decay</td></tr>
<tr><td rowspan="6"><b>Antiquarks</b></td>
  <td>19</td><td><b>Anti-up</b> u&#773;</td><td>0.2</td><td>-2/3</td><td>-0.5</td><td>Annihilates with u</td></tr>
<tr><td>20</td><td><b>Anti-down</b> d&#773;</td><td>0.2</td><td>+1/3</td><td>+0.5</td><td>Annihilates with d</td></tr>
<tr><td>21</td><td><b>Anti-strange</b> s&#773;</td><td>0.05</td><td>+1/3</td><td>+0.5</td><td>Annihilates with s</td></tr>
<tr><td>22</td><td><b>Anti-charm</b> c&#773;</td><td>0.002</td><td>-2/3</td><td>-0.5</td><td>Annihilates with c</td></tr>
<tr><td>23</td><td><b>Anti-top</b> t&#773;</td><td>0.000003</td><td>-2/3</td><td>-0.5</td><td>Annihilates with t</td></tr>
<tr><td>24</td><td><b>Anti-bottom</b> b&#773;</td><td>0.0005</td><td>+1/3</td><td>+0.5</td><td>Annihilates with b</td></tr>
<tr><td rowspan="5"><b>Gauge Bosons</b></td>
  <td>25</td><td><b>Gluon</b> g</td><td>100.0</td><td>0</td><td>+1</td><td>Color confinement mediator</td></tr>
<tr><td>26</td><td><b>W+</b></td><td>0.00012</td><td>+1</td><td>+1</td><td>Instant decay to lepton + &nu;</td></tr>
<tr><td>27</td><td><b>W-</b></td><td>0.00012</td><td>-1</td><td>-1</td><td>Instant decay to lepton + &nu;</td></tr>
<tr><td>28</td><td><b>Z0</b></td><td>0.00011</td><td>0</td><td>0</td><td>Instant decay to e&#8315; + e&#8314;</td></tr>
<tr><td>29</td><td><b>Higgs</b> H0</td><td>0.00008</td><td>0</td><td>0</td><td>Instant decay to 2&gamma;</td></tr>
</tbody>
</table>

---

## Four Fundamental Forces

All four forces act simultaneously in the compute shader:

| Force | Implementation | Key Constants |
|---|---|---|
| **Electromagnetic** | Coulomb attraction/repulsion (K=1200) + Biot-Savart magnetic deflection (always on) | K_COULOMB=1200, SOFTEN=8px |
| **Strong nuclear** | Yukawa potential (attractive, 8px range) + Pauli hard-core repulsion (6px) | YUKAWA=2000, PAULI=12000 |
| **Weak nuclear** | Stochastic decay channels (CPU-side) + tunable coupling constant | Coupling 0.0-2.0 |
| **Gravity** | Newtonian 1/r^2 between massive particles, tunable strength | 0.0-2.0 slider |

**QCD Color Confinement**: Quarks carry RGB color charge (genome[2]). The Cornell potential
`V(r) = -alpha/r + sigma*r` confines quarks — the linear string tension term prevents free quarks.
String tension is tunable (0-200, default 50).

**Higgs Field**: Tunable VEV (0-500) provides mass coupling to heavy particles.

---

## Orbital Mechanics

Electrons orbit nuclei using real quantum-mechanical centrifugal barriers, not artificial springs.

**GPU side (physics.comp):**
- Each electron tracks the nearest nucleon and accumulates the total nuclear charge (Z)
- **Centrifugal barrier**: `F = L_eff^2 / (r^3 + 1.0)` — derivative of the QM effective potential,
  applied once per electron (not per-nucleon)
- **Spin-orbit coupling**: `F_SO = spin * L * K_SPIN_ORBIT / (r^4 + 1)` — fine structure correction
- L_eff = max(L_actual, L_ground), where L_ground is computed CPU-side per orbital shell
- Force capped at 300 to prevent catapulting on close approach

**CPU side (update_orbitals):**
- BFS clusters nucleons into nuclei (10px cluster radius)
- Assigns electrons to nearest nucleus within 60px binding radius
- Sorts by distance, fills shells: **1s** (2), **2s2p** (8), **3s3p3d** (18)
- Computes L_ground per shell using the Bohr model with screening:
  - `R_target = n^2 * R_BOHR / Z_eff` where `Z_eff = Z - inner_electrons`
  - `L_ground = sqrt(Z_eff * K_COULOMB * R^3 / (R^2 + SOFTEN^2))`
- Stores L_ground in genome[2] for the shader to read

**Equilibrium**: For hydrogen, Coulomb attraction `1200/(r^2+64)` balances centrifugal
`120^2/(r^3+1)` at ~15px — the Bohr radius of the simulation.

---

## Nuclear Fusion

CPU-side fusion reactions trigger when particles have sufficient kinetic energy to overcome the
Coulomb barrier. Max 5 fusions per frame to prevent chain reactions.

| Reaction | Threshold | Products |
|---|---|---|
| **Proton-proton chain** (p + p) | Energy > 0.8, relative speed > 60 px/frame | p + n + e&#8314; + &nu;e (one proton converts to neutron) |
| **Deuteron formation** (p + n) | Energy > 0.6, relative speed > 30 px/frame | Bound p-n pair (matched velocities, 3px separation) |
| **He-4 formation** | Implicit | Two bound p-n pairs form helium-4 nucleus |

Fusion radius is 8px (within strong force range). Relative velocity thresholds simulate Coulomb
barrier tunneling — freshly spawned cold nuclei will not fuse spontaneously.

---

## Nuclear Fission

Fast neutrons striking heavy nuclei trigger fission. Max 2 fissions per frame.

| Condition | Detail |
|---|---|
| **Trigger** | Neutron with energy > 0.6 and speed > 50 px/frame |
| **Target** | Cluster of 6+ nucleons within 12px |
| **Products** | Cluster splits in half (80 px/s separation impulse) + 2-3 free neutrons spawned at 0.7 energy |

Emitted neutrons can trigger further fissions, producing visible chain reactions in sufficiently
dense nuclear matter.

---

## Radioactive Decay

A decay engine runs CPU-side each frame. Particles that drop below 0.08 energy undergo
probabilistic decay based on per-type decay rates.

| Parent | Decay Rate | Products |
|---|---|---|
| **Top** t | 0.50 | Bottom + W+ |
| **W+/W-** | 0.50 | Lepton + Neutrino |
| **Z0** | 0.50 | e&#8315; + e&#8314; |
| **Higgs** H0 | 0.40 | 2 Photons |
| **Tau** &tau; | 0.20 | e&#8315; + &nu;&tau; |
| **Charm** c | 0.12 | Strange + W+ |
| **Bottom** b | 0.10 | Charm + W- |
| **Strange** s | 0.02 | Up + W- |
| **Muon** &mu; | 0.01 | e&#8315; + &nu;&mu; + &nu;e |

**Matter-antimatter annihilation** runs every frame at 5px contact radius:
e&#8315;+e&#8314;, p+p&#773;, &mu;&#8315;+&mu;&#8314;, &tau;&#8315;+&tau;&#8314;, and quark-antiquark pairs all
annihilate to photons.

**Decay cascades** unfold naturally: Top -> Bottom + W+ -> Charm + W- + lepton + nu -> ...
producing showers of lighter particles from a single heavy parent.

---

## Field Visualization

Five independent quantum field overlays, each toggled separately in the **Field Visualization**
panel. All render as smooth gradients over the world.

| Field | Color | Source | Range |
|---|---|---|---|
| **Electromagnetic** | Red (+) / Blue (-) | Charged particles | Coulomb 1/r^2 falloff |
| **Strong Nuclear** | Cyan / Green | Nucleons + color-charged quarks | Yukawa exponential (8px range) |
| **Weak Force** | Purple | W/Z bosons | Very short range (0.8px decay) |
| **Gravity** | Grey | All massive particles | 1/r falloff |
| **Higgs** | Gold | Mass coupling | Exponential (30px range) |

Field intensity is adjustable (0.05x - 2.0x). Gravity field renders independently of the
gravity strength slider — you can visualize the field even with gravity turned off.

---

<a name="environment-presets-physics"></a>
## Environment Presets (Physics)

Ten presets spanning vacuum to Big Bang conditions. Select from the **Environment** dropdown.

| # | Environment | Temperature | Key Features |
|---|---|---|---|
| 0 | **Lab Mode** | 2.7 K | Empty vacuum — manual spawning only |
| 1 | **Hydrogen Plasma** | 1.5 x 10^7 K | Hot ionized hydrogen, fusion conditions |
| 2 | **Neutron Star** | 10^9 K | Ultra-dense neutron matter |
| 3 | **Solar Core** | 1.5 x 10^7 K | Hydrogen + gravity — stellar fusion |
| 4 | **Particle Soup** | 5 x 10^3 K | Mixed light particles at moderate energy |
| 5 | **Alpha Emitter** | 300 K | Heavy nuclei, room temperature |
| 6 | **Heavy Nucleus** | 100 K | Cold dense nuclear matter |
| 7 | **Quark-Gluon Plasma** | 2 x 10^12 K | Deconfined quarks and gluons |
| 8 | **Electroweak Era** | 10^15 K | W/Z/Higgs bosons above symmetry breaking |
| 9 | **Meson Factory** | 5 x 10^11 K | Quark-antiquark pairs forming mesons |

Temperature uses a logarithmic slider from 1 K to 10^13 K. The conversion to simulation
noise amplitude follows `T_amp = min(2.0, 0.10 * (T/300)^0.25)`.

---

## Spawn Picker — Physics

Press **F3** to open the spawn menu with four tabs:

### Leptons
Gen-1: e&#8315;, e&#8314;, &nu;e | Gen-2: &mu;&#8315;, &mu;&#8314;, &nu;&mu; | Gen-3: &tau;&#8315;, &tau;&#8314;, &nu;&tau; | Composites: p, n, p&#773;

### Quarks
Matter: u, d, s, c, t, b | Antimatter: u&#773;, d&#773;, s&#773;, c&#773;, t&#773;, b&#773;

### Bosons
Gauge: &gamma; (photon), g (gluon) | Weak: W+, W-, Z0 | Scalar: H0 (Higgs)

### Atoms (Group Templates)

12 composite templates that spawn complete atomic structures with correct proton/neutron/electron
counts and orbital velocities:

| Template | Composition | Particles |
|---|---|---|
| **Hydrogen** H | 1p + 1e | 2 |
| **Deuterium** D | 1p + 1n + 1e | 3 |
| **Helium-4** He | 2p + 2n + 2e | 6 |
| **Lithium-7** Li | 3p + 4n + 3e | 10 |
| **Carbon-12** C | 6p + 6n + 6e | 18 |
| **Oxygen-16** O | 8p + 8n + 8e | 24 |
| **Positronium** | e&#8315; + e&#8314; | 2 |
| **Anti-Hydrogen** | p&#773; + e&#8314; | 2 |
| **Anti-Helium-4** | 2p&#773; + 2n + 2e&#8314; | 6 |
| **Pion+** &pi;+ | u + d&#773; | 2 |
| **Pion-** &pi;- | d + u&#773; | 2 |
| **Kaon+** K+ | u + s&#773; | 2 |

Each tab has configurable count (1-100), energy (0.1-1.0), and scatter radius (1-100px).

---

# Particle Chemistry Mode

The chemistry simulation models 18 elements forming persistent bonds, molecular aggregates,
and evolving proto-organisms — all governed by the same four fundamental forces.

---

## Periodic Table — 18 Elements

Three nucleosynthesis groups spanning the periodic table:

<table>
<thead><tr><th>#</th><th>Element</th><th>Group</th><th>Valence</th><th>Special Behaviour</th></tr></thead>
<tbody>
<tr><td>0</td><td><b>H</b> Hydrogen</td><td>Biogenic</td><td>1</td><td>Polar — dipole rotation</td></tr>
<tr><td>1</td><td><b>C</b> Carbon</td><td>Biogenic</td><td>4</td><td>Neutral backbone</td></tr>
<tr><td>2</td><td><b>N</b> Nitrogen</td><td>Biogenic</td><td>3</td><td>Electron donor</td></tr>
<tr><td>3</td><td><b>O</b> Oxygen</td><td>Biogenic</td><td>2</td><td>Polar + electron acceptor</td></tr>
<tr><td>4</td><td><b>P</b> Phosphorus</td><td>Biogenic</td><td>5</td><td>Heavy, enzymatic catalyst</td></tr>
<tr><td>5</td><td><b>S</b> Sulfur</td><td>Biogenic</td><td>2</td><td>Heavy</td></tr>
<tr><td>6</td><td><b>Na</b> Sodium</td><td>Stellar</td><td>1</td><td>Heavy, ionic (+), adhesive</td></tr>
<tr><td>7</td><td><b>Cl</b> Chlorine</td><td>Stellar</td><td>1</td><td>Heavy, ionic (-), adhesive</td></tr>
<tr><td>8</td><td><b>Fe</b> Iron</td><td>Stellar</td><td>3</td><td>Heavy, polar, redox-active</td></tr>
<tr><td>9</td><td><b>Ni</b> Nickel</td><td>Stellar</td><td>2</td><td>Heavy, catalyst, beta+ unstable to Fe</td></tr>
<tr><td>10</td><td><b>Si</b> Silicon</td><td>Stellar</td><td>4</td><td>Heavy, silicate network former</td></tr>
<tr><td>11</td><td><b>Ca</b> Calcium</td><td>Stellar</td><td>2</td><td>Heavy, ionic (+)</td></tr>
<tr><td>12</td><td><b>Ti</b> Titanium</td><td>Stellar</td><td>4</td><td>Heavy, refractory</td></tr>
<tr><td>13</td><td><b>Sr</b> Strontium</td><td>r-process</td><td>2</td><td>Heavy, ionic, beta- unstable to Ca</td></tr>
<tr><td>14</td><td><b>Au</b> Gold</td><td>r-process</td><td>1</td><td>Heavy, adhesive, noble</td></tr>
<tr><td>15</td><td><b>Pb</b> Lead</td><td>r-process</td><td>4</td><td>Heavy, stable decay endpoint</td></tr>
<tr><td>16</td><td><b>Eu</b> Europium</td><td>r-process</td><td>3</td><td>Heavy, radical, beta- unstable to Fe</td></tr>
<tr><td>17</td><td><b>U</b> Uranium</td><td>r-process</td><td>6</td><td>Heavy, radical, catalyst, alpha unstable to Pb</td></tr>
</tbody>
</table>

Particles initialise in **three well-separated seed clusters** so distinct chemistry zones
evolve independently before merging.

---

## Environment Templates

Nine environment presets control particle abundance, temperature, dampening, and physics on reset.

| # | Environment | Temp | Dampening | Key Atoms | Special |
|---|---|---|---|---|---|
| 0 | **Lab Mode** | 27 C | 0.85 | *(empty)* | Use F3 to place structures |
| 1 | **Tide Pool** | 27 C | 0.93 | H, O, Na, Cl, C, N | Salt water + organics |
| 2 | **Hydrothermal Vent** | 350 C | 0.90 | H, O, S, Fe, Si, Ca | Hot, mineral-rich water |
| 3 | **Primordial Soup** | 80 C | 0.88 | H, C, N, O, P, S | Early Earth organics |
| 4 | **Freshwater Pond** | 20 C | 0.91 | H, O, Ca, Na | Pure water + trace minerals |
| 5 | **Deep Space** | -270 C | 0.99 | H, C, N, O | Sparse, cosmic ray bombardment |
| 6 | **Nebula** | -250 C | 0.98 | H, C, N, O, Si, Fe | Dense hydrogen cloud, gravity = 0.1 |
| 7 | **Asteroid Surface** | -50 C | 0.95 | Fe, Si, Ni, Ca, O, Ti | Rocky metallic body |
| 8 | **Comet** | -100 C | 0.97 | H, O, C, N, Si, Fe, S, P | Ice + dust + organics |

---

## Chemistry & Bonding

### Persistent Bonds

Every 2 frames the CPU bond manager (spatial hash, O(N)) evaluates the particle population:

- **Formation** — Two compatible atoms within `bond_form_radius` with free valence slots snap together
- **Breaking** — A bond stretched beyond `rest_length * break_factor` snaps and emits a photon
- **Spring force** — `F = k_eff * extension` where `k_eff = bond_spring_k * clamp(bond_str + 0.5, 0.2, 1.5)`

Bond compatibility respects real chemistry:
`C-C, C-N, C-O, O-H` (covalent), `Na-Cl` (ionic), `Fe-O, Fe-S, Si-O, Au-S, U-O, P-O, N-H`

### Genome (Chemistry)

| Gene | Range | Effect |
|---|---|---|
| Charge | -1.0 to +1.0 | Coulomb + Lorentz weighting |
| Electronegativity | 0.2 to 2.0 | Electron-transfer energy yield |
| Reactivity | 0.2 to 2.0 | Bond-strain cost; coupled to nuclear stability |
| Bond strength | -0.5 to +0.5 | Spring constant multiplier |

### Energy Metabolism

| Source | Rate |
|---|---|
| Ambient gain | +0.010/s |
| Passive drain | -0.015/s |
| Movement cost | -speed * 0.00015/s |
| Crowding penalty | -(density - limit) * 0.005/s |
| Symbiotic gain | +attraction * proximity * 0.005/pair/s |
| Catalyst boost | +0.008 * neighbour catalysts/s |
| Donor to Acceptor transfer | +electronegativity * proximity/s |
| Bond strain cost | -|ext|/rest * 0.002/s |
| ZPE floor | +vacuum_energy * 0.003/s |

---

## Aggregates & Cells

Every 5 frames, DBSCAN clustering groups nearby atoms into **molecular aggregates**.

### Molecular Classification

| Class | Rule |
|---|---|
| **H2O** water | H > 3/4 cluster & O > 1 |
| **LIPID** | (C+H) > 2/3 cluster |
| **AACD** amino acid | (N+O)*2 > size & C > 0 |
| **NUCL** nucleotide | P*3 > size |
| **RAD!** radical | any RADICAL member |
| **POLY** polymer | C > 1/2 cluster & size > 20 |
| **INRG** inorganic | otherwise |

### Biological Complexity Hierarchy

| Tier | Label | Criteria |
|---|---|---|
| AGGREGATE | -- | Any cluster |
| **VESICLE** | `VSIC` | LIPID cluster with ring topology (ring_factor > 0.65), size >= 8 |
| **PROTO-CELL** | `PCLL` | Vesicle enclosing at least one non-lipid cluster |
| **CELL** | `CELL` | Proto-cell enclosing a nucleotide cluster (DNA/RNA analog) |

### Darwinian Evolution (vesicle+ only)

| Mechanism | Detail |
|---|---|
| **Division mutation** | On division, +/-3% drift to electronegativity and reactivity |
| **Fitness-driven adaptation** | Top-3 vesicle+ structures nudge their type's self-cohesion force |
| **Trait-scale amplification** | Bond-rich cell-class clusters boost their type's force row up to 2.5x |

---

## Spawn Picker — Chemistry

Press **F3** to open the spawn picker with tabs for **Atoms** (18 elements), **Groups**
(14 inorganic / small-molecule templates), **Organics** (8 bio-molecule templates), and
**Organisms** (clone aggregates or place predefined templates).

Group templates include: H2O, CH4, NaCl, NH3, CO2, Glycine, Benzene, SiO4, Fe2O3, EtOH,
CaCO3, Au3, UO2, FeS2.

Organic templates include: Glycine, Alanine, Glucose, Ribose, Butyric Acid, Glycerophosphate,
Adenine, Cytosine.

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

> **Force grid** (chemistry mode) — hover a cell and scroll to tune; right-click to zero it.

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
# Chemistry simulation
./build/particle_life

# Physics simulation
./build/particle_physics
```

---

## Architecture

```
EmergentEvolution/
├── src/
│   ├── types.h                  # SimConfig, PushConstants (100 bytes), shared constants
│   ├── particles.h/.cpp         # CPU arrays, CPK colours, electrochemistry force matrix
│   ├── bond_manager.h/.cpp      # Spatial-hash bond formation/breaking, BOND_COMPAT
│   ├── decay_manager.h/.cpp     # Stochastic half-life decay, annihilation, DECAY_TABLE
│   ├── organism.h/.cpp          # DBSCAN clustering, molecule classification, trait feedback
│   ├── sub_atomic.h/.cpp        # Sub-atomic LOD: Bohr nucleon/electron, Cornell quark
│   ├── vulkan_context.h/.cpp    # Vulkan instance, device, swapchain, buffer helpers
│   ├── compute_pipeline.h/.cpp  # 17-binding descriptor layout, buffer lifecycle, readback
│   ├── renderer.h/.cpp          # Fullscreen-quad pipeline, ImGui integration
│   ├── interface.h/.cpp         # Chemistry ImGui panels, F3 spawn picker
│   ├── simulation.h/.cpp        # Chemistry main loop, input, camera, orchestration
│   └── main.cpp                 # Chemistry entry point
├── src/physics/
│   ├── phys_particles.h/.cpp    # 30 Standard Model types, masses, charges, decay rates
│   ├── interface.h/.cpp         # Physics ImGui panels, 4-tab spawn picker, field viz
│   ├── simulation.h/.cpp        # Physics main loop: fusion, fission, decay, orbitals
│   └── main.cpp                 # Physics entry point
├── shaders/
│   ├── compute.comp             # Chemistry GPU: forces, bonds, metabolism
│   ├── physics.comp             # Physics GPU: 4 forces, centrifugal barrier, 5 field viz
│   ├── fullscreen.vert          # Fullscreen triangle vertex shader
│   └── fullscreen.frag          # Particle texture blit
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
| 9-12 | angle / angular velocity A+B | read/write |
| 13 | energy A (ping) | read |
| 14 | energy B (pong) | write |
| 15 | genome | read |
| 16 | bond partners | read (CPU-managed) |

A/B buffers ping-pong each tick. All buffers are HOST_VISIBLE + HOST_COHERENT for CPU readback.

---

<div align="center">

Made with C++20, Vulkan & Dear ImGui · Night-Traders-Dev 2026

</div>
