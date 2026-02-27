<div align="center">

# Particle Playground

**A GPU-accelerated quantum particle physics sandbox**

Standard Model + Beyond · Nuclear fusion & fission · Covalent Bonds & Molecules · Molecule Spawn by Formula · Hadronization & Color Confinement · Gluon Interactions · Photon-matter interactions · Orbital mechanics · Emergent thermodynamics · Quantum entanglement · Wave-particle duality · Achievements · Tools · Save/Load

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-NTD--2026-green.svg)]()

</div>

---

Particle Playground is a real-time particle physics sandbox powered by Vulkan compute shaders.
33 particle types spanning the Standard Model and beyond interact through all four fundamental
forces: Coulomb + Yukawa + QCD + weak, with centrifugal barrier orbitals, nuclear fusion/fission,
radioactive decay with realistic isotope half-lives, photoelectric effect, Compton scattering,
nuclear spallation, photodisintegration, pair production, pion production, vector meson dominance,
hard-sphere collisions, covalent bonds with spring forces and molecular detection (Hill system
formulas, molecule detail cards), molecule spawn by formula (type "H2O" or "CH4" and click to
place complete molecules with correct geometry — bonds auto-form), hadronization & color confinement (meson formation, baryon
condensation, vacuum instability, string breaking), gluon interactions (quark absorption,
self-coupling, confinement splitting), emergent thermodynamics, virtual particle pair creation,
quantum entanglement, antimatter element detection, electron cloud visualization, a persistent
event log, an achievement system, measurement tools (thermometer, velocity meter, ruler with
nanometer scale, density counter), visualization overlays (energy heatmap, velocity field,
trajectory tracer, force vectors, atom grid, wave-particle duality mode), interactive tools
(particle accelerator, mirrors), 12 UI themes, 12 environment presets, element export/import, six
per-force multiplier knobs, relativistic energy readouts in electronvolts (PDG rest masses,
E = γm₀c²), a browsable particle list and element list with live molecular formula grouping
and energy/age tracking, and an animated particle splash screen.

Simulates up to **100,000 particles** in real time on a toroidal 2560 x 1440 world. GPU compute
shaders handle O(n^2) pairwise forces; CPU-side physics uses a **spatial acceleration grid** for
O(n) neighbor queries with **OpenMP** parallelization across all available cores.

---

## Table of Contents

- [Physics Engine](#physics-engine)
- [Standard Model + Beyond — 33 Particle Types](#standard-model--beyond--33-particle-types)
- [Four Fundamental Forces + Multipliers](#four-fundamental-forces--multipliers)
- [Orbital Mechanics](#orbital-mechanics)
- [Covalent Bonds & Molecules](#covalent-bonds--molecules)
- [Nuclear Fusion](#nuclear-fusion)
- [Nuclear Fission](#nuclear-fission)
- [Radioactive Decay](#radioactive-decay)
- [Isotope Half-Lives](#isotope-half-lives)
- [Photon-Matter Interactions](#photon-matter-interactions)
- [Nuclear Spallation & Photonuclear Processes](#nuclear-spallation--photonuclear-processes)
- [Element Detection & Info Cards](#element-detection--info-cards)
- [Particle List, Element List & Event Log](#particle-list-element-list--event-log)
- [Electron Cloud Visualization](#electron-cloud-visualization)
- [Hard-Sphere Collisions](#hard-sphere-collisions)
- [Virtual Particle Pairs](#virtual-particle-pairs)
- [Hadronization & Color Confinement](#hadronization--color-confinement)
- [Gluon Interactions](#gluon-interactions)
- [Quantum Entanglement](#quantum-entanglement)
- [Emergent Thermodynamics](#emergent-thermodynamics)
- [Field Visualization](#field-visualization)
- [Environment Presets](#environment-presets)
- [Spawn Picker](#spawn-picker)
- [Measurement Tools](#measurement-tools)
- [Visualization Tools](#visualization-tools)
- [Tools](#tools)
- [Achievements](#achievements)
- [Save / Load](#save--load)
- [Performance](#performance)
- [UI Themes](#ui-themes)
- [Controls](#controls)
- [Build](#build)
- [Architecture](#architecture)

---

## Physics Engine

The Vulkan compute pipeline is dispatched each frame.

| Property | Detail |
|---|---|
| Particle count | Up to **100,000** simultaneous particles |
| GPU forces | O(n^2) pairwise, per-frame on Vulkan compute shader |
| CPU physics | O(n) via **spatial acceleration grid** (30px cells, 86x49 = 4,214 cells) |
| Parallelism | **OpenMP** across all cores for statistics, measurement probes, and visualization grid |
| GPU sync | Batched — single `vkDeviceWaitIdle` per frame (was 8-10 before) |
| World | Toroidal 2560 x 1440 (seamless wrap) |
| Buffers | Double-buffered ping-pong (position, velocity, angle, angular velocity, energy, genome) |
| Genome | 4 floats per particle: charge, spin, color charge / orbital L, decay rate |
| Push Constants | 128 bytes (Vulkan guaranteed minimum) — all simulation parameters per frame |

---

## Standard Model + Beyond — 33 Particle Types

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
<tr><td>3</td><td><b>Photon</b> &gamma;</td><td>100.0</td><td>0</td><td>+1</td><td>Compton scatters off charges</td></tr>
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
<tr><td rowspan="3"><b>Beyond SM</b></td>
  <td>30</td><td><b>Graviton</b> G</td><td>100.0</td><td>0</td><td>+2</td><td>Massless, ballistic (hypothetical)</td></tr>
<tr><td>31</td><td><b>Dark Matter</b> DM</td><td>0.001</td><td>0</td><td>+0.5</td><td>WIMP — gravity only, no EM/strong</td></tr>
<tr><td>32</td><td><b>Dark Energy</b> DE</td><td>100.0</td><td>0</td><td>0</td><td>Universal repulsive field quantum</td></tr>
</tbody>
</table>

---

## Four Fundamental Forces + Multipliers

All four forces act simultaneously in the compute shader. Each force has an independent
**multiplier slider** (0.0x - 3.0x, default 1.0 = Standard Model) in the Force Multipliers panel.

| Force | Implementation | Key Constants | Multiplier |
|---|---|---|---|
| **Electromagnetic** | Coulomb attraction/repulsion + Biot-Savart magnetic deflection (1/r^2 falloff) | K_COULOMB=1200, K_MAGNETIC=3.0 | `coulomb_strength` |
| **Strong nuclear** | Yukawa potential (attractive, 8px range) + Pauli hard-core repulsion (6px) | YUKAWA=2000, PAULI=12000 | `yukawa_strength`, `pauli_multiplier` |
| **QCD color** | Cornell potential with running coupling (asymptotic freedom) | alpha_s running, string_tension 0-200 | `alpha_s_scale` |
| **Weak nuclear** | Phenomenological short-range Yukawa (0.8px range) + stochastic decay (CPU) | Coupling 0.0-2.0 | -- |
| **Gravity** | Newtonian 1/r^2 between massive particles | 0.0-2.0 slider (default 1.0) | -- |
| **Compton** | Photon radiation pressure + oscillating B-field on charged matter | 30px range | `compton_strength` |
| **Annihilation** | Matter-antimatter attraction at contact range | 6px radius | `annihilation_strength` |

**Running QCD coupling**: `alpha_eff = alpha_s * max(0.3, 1 + 0.3 * ln(r))` — quarks interact
weakly at short distances (asymptotic freedom) and strongly at long distances (confinement).

**Dark matter**: Only interacts via gravity (self-gravity always on at 20x, DM-normal at 5x).
No electromagnetic, strong, or weak forces.

**Dark energy**: Universal repulsive force that grows with distance (cosmological constant analog).

**Higgs Field**: Tunable VEV (0-500) provides mass coupling to heavy particles.

---

## Orbital Mechanics

Electrons orbit nuclei using real quantum-mechanical centrifugal barriers, not artificial springs.

**GPU side (physics.comp):**
- Each charged lepton (e/mu/tau) tracks the nearest nucleon
- **Centrifugal barrier**: `F = L_eff^2 * mass_inv / (r^3 + 1.0)` — heavier leptons orbit tighter
- **Spin-orbit coupling**: `F_SO = spin * L * K_SPIN_ORBIT / (r^4 + 1)` — fine structure correction
- **Spin magnetic moment**: `F = mu * B_accumulated * grad_scale * (vy, -vx)` — dipole in external B-field
- L_eff = max(L_actual, L_ground), where L_ground is computed CPU-side per orbital shell

**CPU side (update_orbitals):**
- BFS clusters nucleons into nuclei (10px cluster radius)
- Tracks orbital parent relationships (clickable in info card)
- Assigns electrons to nearest nucleus within 60px binding radius
- Sorts by distance, fills shells: **1s** (2), **2s2p** (8), **3s3p3d** (18)
- Computes L_ground per shell using the Bohr model with screening:
  - `R_target = n^2 * R_BOHR / Z_eff` where `Z_eff = Z - inner_electrons`
  - `L_ground = sqrt(Z_eff * K_COULOMB * R^3 / (R^2 + SOFTEN^2))`

---

## Covalent Bonds & Molecules

Atoms share valence electrons to form covalent bonds, producing stable molecules (H₂, H₂O, CH₄,
etc.). Bond formation and breaking run CPU-side; GPU spring forces maintain bond geometry.

### Bond Formation

| Property | Detail |
|---|---|
| **Valence** | Period 1-3 elements: H=1, Be=2, B=3, C=4, N=3, O=2, F=1; noble gases=0 |
| **Search radius** | Configurable (default 28px) — nuclei within range are bond candidates |
| **Activation energy** | Relative KE between nuclei must exceed threshold (default 0.02) but not be too high (scattering) |
| **Capacity** | Up to 6 bonds per particle (`MAX_BONDS_PER_PARTICLE`); limited by valence |
| **Rate** | Max 10 new bonds per frame to prevent runaway clustering |

### Bond Physics (GPU)

| Property | Detail |
|---|---|
| **Spring force** | Hooke's law: `F = K * (d - rest_length)` along bond axis (K=80, rest=22px) |
| **Damping** | Dashpot: `F_damp = 0.3 * relative_velocity_along_bond` — prevents oscillation |
| **Breaking** | Bonds break when distance exceeds `rest_length * break_factor` (default 2.2x) |

### Bond Rendering

Bonds render as **pale blue glowing lines** between bonded atoms in the compute shader, using
energy-dependent brightness. Only the lower-index partner renders each bond to avoid double-drawing.
An ImGui overlay also draws solid blue lines for bonds visible on screen.

### Molecule Detection

Bonded atoms are grouped into molecules using a **Union-Find** algorithm each frame. Molecules
are displayed with **Hill system molecular formulas** (C first, H second, rest alphabetical) in
the Element List and bottom bar. Each molecule tracks total energy, age, and net ionic charge.

### Molecule Spawn by Formula

Type a molecular formula in the **Molecules** section of the Spawn Picker and click in the world
to place a complete molecule with atoms at correct geometry. A database of **~50 common molecules**
covers diatomics through glucose (C₆H₁₂O₆):

- **Text input** with Enter-to-spawn and live formula/name matching
- **Autocomplete** dropdown showing up to 8 matching molecules (prefix + substring search)
- **Quick-access buttons**: H₂O, CO₂, NH₃, CH₄, NaCl, EtOH, C₆H₆, Glucose
- **Geometry**: atoms placed at bond-rest-length spacing (22px) with correct angles — tetrahedral
  (CH₄, 90° cross), trigonal planar (BF₃, 120°), bent (H₂O, 104.5°), linear (CO₂), ring (C₆H₆)
- **Auto-bonding**: bonds form within 2-4 frames via existing `update_bonds()` — no special wiring needed

### Tuning (Nuclear Debug Window)

| Parameter | Range | Default | Effect |
|---|---|---|---|
| **Enable Bonds** | on/off | on | Master toggle for covalent bonding |
| **Spring K** | 10 - 200 | 80 | Bond stiffness |
| **Rest Length** | 8 - 60 px | 22 | Equilibrium bond length |
| **Break Factor** | 1.5 - 5.0 | 2.2 | Distance multiplier before bond breaks |
| **Form Radius** | 15 - 80 px | 28 | Search radius for new bond partners |
| **Activation Energy** | 0.001 - 0.2 | 0.02 | Minimum KE for bond formation |

---

## Nuclear Fusion

CPU-side fusion reactions trigger when particles have sufficient kinetic energy to overcome the
Coulomb barrier. Max 5 fusions per frame to prevent chain reactions.

| Reaction | Threshold | Products |
|---|---|---|
| **Proton-proton chain** (p + p) | Energy > 0.8, relative speed > 60 px/frame | p + n + e&#8314; + &nu;e (one proton converts to neutron) |
| **Deuteron formation** (p + n) | Energy > 0.6, relative speed > 30 px/frame | Bound p-n pair (matched velocities, 3px separation) |
| **He-4 formation** | Implicit | Two bound p-n pairs form helium-4 nucleus |

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
annihilate to photons (+ neutrinos for lepton pairs).

**Decay cascades** unfold naturally: Top -> Bottom + W+ -> Charm + W- + lepton + nu -> ...
producing showers of lighter particles from a single heavy parent.

---

## Isotope Half-Lives

A nuclear isotope decay system identifies nuclei dynamically via BFS clustering of
protons and neutrons, then applies realistic half-life decay based on (Z, N) composition.

### Decay Modes

| Mode | Symbol | Process | Products |
|---|---|---|---|
| **Alpha** | &alpha; | Emit He-4 nucleus | 2p + 2n ejected at high velocity |
| **Beta-minus** | &beta;&#8315; | n &rarr; p + e&#8315; + &nu;&#773;e | Proton count increases by 1 |
| **Beta-plus** | &beta;&#8314; | p &rarr; n + e&#8314; + &nu;e | Proton count decreases by 1 |
| **Neutron emission** | n-emit | Eject free neutron | Occurs at nuclear gaps (A=5) |
| **Proton emission** | p-emit | Eject free proton | Proton-drip-line nuclei |

### Key Isotopes

| Isotope | Z | N | Mode | Sim Half-Life | Real Half-Life |
|---|---|---|---|---|---|
| Free neutron | 0 | 1 | &beta;&#8315; | 10 s | 10 min |
| Tritium H-3 | 1 | 2 | &beta;&#8315; | 1 min | 12.3 yr |
| He-5 | 2 | 3 | n-emit | instant | 7&times;10&#8315;&#178;&#178; s |
| Be-8 | 4 | 4 | &alpha; | instant | 6.7&times;10&#8315;&#185;&#8311; s |
| C-14 | 6 | 8 | &beta;&#8315; | 5 min | 5730 yr |
| N-13 | 7 | 6 | &beta;&#8314; | 10 s | 10 min |
| Co-60 | 27 | 33 | &beta;&#8315; | 2 min | 5.3 yr |
| Sr-90 | 38 | 52 | &beta;&#8315; | 2 min | 28.8 yr |
| U-235 | 92 | 143 | &alpha; | 3.3 min | 704 Myr |
| U-238 | 92 | 146 | &alpha; | 5 min | 4.5 Gyr |

Full table includes ~50 isotopes. Nuclei not in the explicit table fall through to
**general stability rules**:

- **Z > 83** (above bismuth): always alpha-decay
- **N/Z > 1.5**: beta-minus decay toward stability valley
- **N/Z < 0.7** (Z &ge; 3): beta-plus decay
- **A = 5 or A = 8**: instant disintegration (known nuclear gaps)

Decay probability per frame: P = 1 &minus; exp(&minus;ln(2) / t&frac12;)

---

## Photon-Matter Interactions

High-energy photons interact with matter through multiple CPU-side physics channels, in addition
to GPU-side Compton radiation pressure. Max 8 interactions per frame.

### GPU-Side (Compton Scattering)

- Scans nearby charged particles (strided sampling, 30px radius)
- Photon deflects toward closest charged particle, losing energy proportional to proximity
- Radiation pressure pushes charged matter along photon travel direction
- Photon's oscillating B-field exerts Lorentz force: `F = q * Bz * (vy, -vx)`
- Both scale with photon energy / r^2

### CPU-Side Processes

| Process | Threshold | Effect |
|---|---|---|
| **Photoelectric Effect** | E&gamma; &ge; 1.5 &times; binding energy | Photon fully absorbed, electron ionized (ejected from orbit) |
| **Compton Scattering** (bound) | E&gamma; &ge; 0.6 &times; binding energy | 40% energy transfer; electron kicked to higher shell or ionized |
| **Free Electron Scattering** | E&gamma; &ge; 0.3 | 30% energy transfer, momentum along original photon direction |
| **Nuclear Compton** | E&gamma; &ge; 0.25 | Photon scatters off free nucleon; 8% energy transfer + momentum kick |

**Shell-dependent binding energy**: Binding scales with &radic;Z (heavier atoms bind tighter).
Inner shells (1s) require more energy to ionize than outer shells (3s3p3d).

**Shell promotion**: When Compton transfer is below the ionization threshold, the electron is
promoted to a higher shell by boosting its orbital angular momentum (L) and applying a radial
kick outward from the nucleus.

---

## Nuclear Spallation & Photonuclear Processes

High-energy particles and photons can shatter or transform nuclei through several distinct
processes. Max 3 events per frame.

### Massive Particle Spallation

| Property | Detail |
|---|---|
| **Trigger** | Any massive particle with speed > 120 px/frame, energy > 0.5 |
| **Target** | Nucleus with 2+ nucleons (detected via BFS clustering) |
| **Hit radius** | 10 px from nucleus center |
| **Damage** | Proportional to projectile kinetic energy; scales from 1 nucleon to total disintegration |
| **Products** | Ejected nucleons + scattered electrons; projectile loses ~70% energy |

### High-Energy Photon-Nucleus Interactions

Processes ordered by energy threshold. A given photon triggers at most one per frame, selected
probabilistically (higher energy unlocks more channels):

| Process | E&gamma; Threshold | Products | Color |
|---|---|---|---|
| **Photodisintegration** | &ge; 0.50 | &gamma; + A &rarr; (A&minus;1) + nucleon (giant dipole resonance); ejects 1&ndash;2 nucleons | Purple |
| **Pair Production** | &ge; 0.60 | &gamma; &rarr; e&#8314; + e&#8315; in nuclear Coulomb field (15px interaction radius) | Blue |
| **Photopion Production** | &ge; 0.80 | &gamma; + N &rarr; N' + &pi; via &Delta; resonance; pion as quark-antiquark pair (u+d&#773; or u&#773;+d) | Green |
| **Vector Meson Dominance** | &ge; 0.85 | &gamma; &rarr; &rho;&#8304; meson &rarr; hadronic shower: multiple nucleon ejections + quark-antiquark debris | Magenta |

**Pair production** requires a nearby nucleus for momentum conservation. The electron and positron
open in directions roughly perpendicular to the photon path with a slight forward boost.

**Photopion production** models the &Delta;(1232) resonance: the photon excites a nucleon to a
&Delta; baryon, which immediately decays into a nucleon (isospin-flipped) + pion.

**Vector meson dominance** is the highest-energy channel: the photon fluctuates into a virtual
&rho;&#8304; meson that interacts hadronically, producing a shower of nucleon fragments and
quark-antiquark debris.

---

## Element Detection & Info Cards

Nuclei are dynamically detected each frame by BFS clustering protons and neutrons within
a 10px radius. Each cluster yields an element identity (Z, N, A) with bound electrons
counted by proximity. Both matter and **antimatter elements** (antiproton nuclei with positron
clouds) are detected and displayed with distinct cyan-tinted UI styling.

**Particle Info Card** (bottom-right notification):
- Shows particle type, charge, spin, mass, age, momentum
- **Relativistic energy** displayed in electronvolts (eV/keV/MeV/GeV/TeV) computed from
  real PDG rest masses and E = γm₀c² with β = v/c_sim
- Temperature, magnetic moment
- If the particle belongs to a nucleus: shows element name, composition, and a clickable
  link to the **Element Detail Card**
- Antimatter elements display as "Anti-Hydrogen", "Anti-Helium", etc. with cyan accent

**Element Detail Card** (bottom-right, left of info card):
- Full element name and symbol (all 118 elements + antimatter variants)
- Composition: Z, N, A, electron count, shell configuration
- Net charge, total mass, momentum, age
- **Total relativistic energy** in eV — sum of γm₀c² for all constituent particles
- **Stability indicator** with isotope half-life lookup (color-coded)
- Clickable nucleon/electron particle list for navigation
- **Move**: relocate entire element (all constituent particles) by clicking
- **Delete**: remove all particles in the element
- **Duplicate**: spawn a copy of the element nearby with correct orbital structure
- **Export**: save the element to a `.ppel` file for import into other simulations

**Molecule Detail Card** (opens from Element List when clicking a multi-atom molecule):
- **Molecular formula** in Hill system notation (C first, H second, rest alphabetical)
- Composition: total protons, neutrons, electrons, covalent bond count
- Net ionic charge, total mass, particle count
- Center-of-mass speed, total momentum, total relativistic energy
- Age (from oldest constituent particle)
- **Clickable atom buttons** — each constituent atom opens its Element Detail Card
- **Navigate**: pan camera to molecule center
- **Close**: dismiss card

---

## Particle List, Element List & Event Log

**Particle List** — The bottom bar displays a clickable **"Particles: N"** counter showing
all active particles. Clicking opens a scrollable window with particles grouped by type
in collapsible tree nodes:

- **Per-type headers**: particle type name with count (e.g. "Proton (24)")
- **Per-particle row**: ID, type, speed (as fraction of c), relativistic energy (eV), age
- **Color dot**: type-colored indicator matching the simulation rendering
- **Click to inspect**: clicking any row navigates the camera to that particle and selects
  it for the info card
- **Hover tooltip**: speed, energy, age summary

**Element List** — The bottom bar displays clickable **"Atoms: N"** and **"Mol: M"** counters
showing detected atoms and molecules. Clicking opens a centered, scrollable window listing
all atoms and molecules:

- **Molecules** (blue dot): displayed with **Hill system molecular formula** (e.g. CH₄, H₂O,
  NaCl). Sorted largest first. Tooltip shows constituent atoms. Click to open **Molecule
  Detail Card**
- **Free atoms** (stability dot): green (stable), yellow (long-lived), orange (medium),
  red (short-lived). Shows symbol, mass number (A), element name, charge
- **Relativistic energy**: total E = Σγm₀c² across all constituents (eV/keV/MeV/GeV)
- **Age**: time since the oldest constituent was spawned
- **Tooltip**: Z, N, electrons, decay mode and half-life for unstable isotopes
- **Click to inspect**: clicking a molecule opens its Molecule Detail Card; clicking a
  free atom navigates to it and opens its Element Detail Card

**Event Notifications** — Toast-style notifications appear in the top-right corner when
physics events occur, rendering above all other windows. Stacks vertically with a 5-second
timeout and fade-out (max 8 visible).

**Decay / Event Log** — Click the **"Events: N"** counter in the bottom bar to open a
persistent, scrollable log of all physics events. The log tracks up to **10,000** entries
across 11 event categories, each stamped with a wall-clock timestamp (`HH:MM:SS`):

| Category | Examples | Color |
|---|---|---|
| **Particle Decay** | t &rarr; b + W&#8314;, &mu;&#8315; &rarr; e&#8315; + &nu;&mu; + &nu;&#773;e | Warm yellow |
| **Nuclear Decay** | &alpha; Decay: U-238 &rarr; Th-234 + He-4, &beta;&#8315; Decay | Red-orange / Blue |
| **Fusion** | p + p &rarr; d + e&#8314; + &nu;, p + n &rarr; d | Cyan |
| **Fission** | Cluster split + free neutrons | Orange |
| **Annihilation** | e&#8314; + e&#8315; &rarr; &gamma;&gamma; | Red |
| **Photoelectric** | &gamma; absorbed, e&#8315; ionized; Compton scatter | Blue |
| **Spallation** | Nucleus disintegrated; N nucleons ejected | Red-orange |
| **Pair Production** | &gamma; &rarr; e&#8314; + e&#8315; | Blue |
| **Pion Production** | &gamma; + p &rarr; n + &pi;&#8314; | Green |
| **Vector Meson Dominance** | &rho;&#8304; meson shower | Magenta |
| **Photodisintegration** | &gamma; ejected nucleon from nucleus | Purple |

The log window displays a summary bar with per-category counts and supports a **Clear** button
to reset the log. Events are listed newest-first with wall-clock timestamps and color-coded type
tags.

---

## Electron Cloud Visualization

Toggle via **Menu > Visualization > Electron Cloud** to overlay Bohr-model orbital shell rings
around all detected nuclei.

| Property | Detail |
|---|---|
| **Shell display** | Up to 3 concentric rings per nucleus: 1s (2), 2s2p (8), 3s3p3d (18) |
| **Radii** | Computed from Bohr model with Slater screening: `R = n^2 * R_BOHR / Z_eff` |
| **Fill indicator** | Solid ring = full shell, dashed = empty, partial arc = partially filled |
| **Labels** | Each ring shows N/M (electrons present / shell capacity) |
| **Center label** | Element symbol displayed at nucleus center |
| **Antimatter** | Anti-elements show cyan-tinted rings with positron shell counts |
| **Colors** | Matter: red/green/blue shells; Antimatter: cyan/magenta/yellow shells |

---

## Hard-Sphere Collisions

Massive particles undergo elastic hard-sphere collisions in addition to force-based physics:

- Position correction: accumulated from ALL overlapping neighbors, applied as half-correction
  (both particles push independently, net = full separation)
- Velocity response: momentum-conserving elastic collision along strongest collision normal
- Coefficient of restitution: 0.95 (slightly inelastic for stability)
- Minimum separation: `2 * particle_radius`

---

## Virtual Particle Pairs

QFT vacuum fluctuations modeled as spontaneous particle-antiparticle pair creation:

| Interaction Type | Virtual Pair | Condition |
|---|---|---|
| Charged particles | e&#8315; + e&#8314; (Schwinger) | Combined energy > 1.5 |
| Quark-quark | Gluon + gluon | QCD interaction |
| Weak bosons present | W+ + W- | Weak sector |
| Gravity active | Graviton + graviton | Gravity > 0 |
| Default (charged) | &gamma; + &gamma; | QED vacuum |

Virtual particles have high decay rate (genome[3] = 0.08) giving ~15 frame lifetime. They
render with a flickering translucent effect. Configurable: energy threshold (0.8-5.0),
max pairs per tick (1-16).

---

## Hadronization & Color Confinement

CPU-side color confinement enforcement that prevents free quarks from existing in isolation.
Quarks must form color-neutral hadrons (mesons or baryons). Runs each tick with five phases:

| Phase | Description |
|---|---|
| **1. Free quark detection** | Any quark/antiquark without a partner within 45px confinement radius is flagged as free |
| **2. Meson formation** | Free quark + free antiquark with complementary colors (R+anti-R, etc.) pair into mesons — velocity impulse pushes them together |
| **2b. Baryon condensation** | Below the Hagedorn temperature (1.7 x 10^12 K), RGB triplets of quarks within 15px condense into protons or neutrons. Down-type quark count (d,s,b) determines baryon: 0-1 down = proton, 2-3 down = neutron. Antimatter triplets form antiprotons |
| **3. Vacuum instability** | Remaining free quarks with energy >= 0.3 spontaneously generate a partner quark from the vacuum (Schwinger-like mechanism) |
| **4. String breaking** | Bound quark-antiquark pairs stretched beyond 55px break the color flux tube (Lund model) — a new quark-antiquark pair spawns at the midpoint |

**QGP exception**: Above 2 x 10^12 K, quarks are deconfined and hadronization is suppressed
(Quark-Gluon Plasma regime). The QGP environment preset automatically disables hadronization.

Toggle in **Settings > Strong Nuclear > Hadronization**. Max 8 mesons, 8 baryons, 4 vacuum pairs,
and 3 string breaks per frame to prevent chain reactions.

---

## Gluon Interactions

CPU-side gluon interaction physics complementing the GPU-side Cornell potential. Three interaction
types enforced per tick:

| Interaction | Description | Condition |
|---|---|---|
| **Quark absorption** | Gluon absorbed by nearby quark — 50% energy transfer + 30% momentum kick | Gluon within 12px of quark |
| **Self-coupling** (g+g -> g) | Two gluons merge into one — non-abelian trilinear vertex | Two gluons within 12px |
| **Confinement splitting** (g -> q+q-bar) | Isolated gluon splits into light quark-antiquark pair | Gluon with energy >= 0.2, no nearby colored particle within 40px |

Max 6 gluon events per frame. The killed gluon's energy is transferred to the products.

---

## Quantum Entanglement

An optional subsystem that entangles particle pairs created during virtual pair production.
Entangled pairs exhibit non-local correlations — "spooky action at a distance". Toggle on/off
in the **Entanglement** settings panel.

| Property | Detail |
|---|---|
| **Creation** | Virtual particle pairs are automatically entangled at birth with anti-correlated spins |
| **Velocity coupling** | Fraction of velocity difference applied mutually each tick — entangled particles mirror each other's motion |
| **Spin anti-correlation** | Entangled partners maintain opposite spin values (if one flips, the other follows) |
| **Decoherence** | Stochastic per-tick probability of entanglement breaking |
| **Death** | Entanglement breaks instantly if either partner dies |
| **Visualization** | Dashed blue lines connect entangled pairs on screen |

**Settings** (Entanglement panel):

| Parameter | Range | Default | Effect |
|---|---|---|---|
| Enable Entanglement | on/off | on | Master toggle for entanglement |
| Coupling | 0.0 - 0.5 | 0.15 | Velocity coupling fraction — higher = stronger non-local correlation |
| Decoherence | 0.0 - 0.05 | 0.005 | Probability per tick of breaking — 0 = permanent, higher = faster decay |

The active entangled pair count is displayed in the panel. Pairs are transient — they form
naturally when virtual particles spawn and decay over time through decoherence or particle death.

---

## Emergent Thermodynamics

Two emergent feedback systems measure bulk properties from particle kinetics and feed them
back into the simulation:

**Emergent Temperature** (Berendsen thermostat):
- Measures average kinetic energy: `T_measured = EMA(0.5 * |v|^2) * 0.1`
- Thermostat correction: when system is hotter than target, reduce thermal noise (cool);
  when cooler, increase noise (heat)
- Coupling slider: 0 = slider only, 1 = fully emergent, 0.5 = blended

**Emergent B-Field**:
- Measures average charged current: `B_measured = EMA(|q| * |v|) * 0.02`
- Feeds back into effective Lorentz strength for magnetic interactions
- Moving charges generate the magnetic field that deflects other charges

Both use exponential moving averages (alpha = 0.02) for smooth temporal filtering.

---

## Field Visualization

Five independent quantum field overlays, each toggled separately.

| Field | Color | Source | Range |
|---|---|---|---|
| **Electromagnetic** | Red (+) / Blue (-) | Charged particles | Coulomb 1/r^2 falloff |
| **Strong Nuclear** | Cyan / Green | Nucleons + color-charged quarks | Yukawa exponential (8px range) |
| **Weak Force** | Purple | W/Z bosons | Very short range (0.8px decay) |
| **Gravity** | Grey | All massive particles | 1/r falloff |
| **Higgs** | Gold | Mass coupling | Exponential (30px range) |

---

<a name="environment-presets"></a>
## Environment Presets

Twelve presets spanning vacuum to dark sector. Select from the **Environment** dropdown.

| # | Environment | Temperature | Key Features |
|---|---|---|---|
| 0 | **Lab Mode** | 1 K | Empty vacuum — manual spawning only, hadronization enabled |
| 1 | **Hydrogen Plasma** | 1.5 x 10^7 K | Hot ionized hydrogen, fusion conditions |
| 2 | **Neutron Star** | 10^9 K | Ultra-dense neutron matter |
| 3 | **Solar Core** | 1.5 x 10^7 K | Hydrogen + gravity — stellar fusion |
| 4 | **Particle Soup** | 5 x 10^3 K | Mixed light particles at moderate energy |
| 5 | **Alpha Emitter** | 300 K | Heavy nuclei, room temperature |
| 6 | **Heavy Nucleus** | 100 K | Cold dense nuclear matter |
| 7 | **Quark-Gluon Plasma** | 2 x 10^12 K | Deconfined quarks and gluons, hadronization disabled |
| 8 | **Electroweak Era** | 10^15 K | W/Z/Higgs bosons above symmetry breaking |
| 9 | **Meson Factory** | 5 x 10^11 K | Quark-antiquark pairs forming mesons |
| 10 | **Particle Accelerator** | 10^8 K | High-energy protons + synchrotron radiation |
| 11 | **Dark Sector** | 10^3 K | 40% DM, 30% p, 15% e, 10% DE, 5% gravitons |

---

## Spawn Picker

Press **F3** to open the spawn menu with categorized sections:

### Leptons
Gen-1: e&#8315;, e&#8314;, &nu;e | Gen-2: &mu;&#8315;, &mu;&#8314;, &nu;&mu; | Gen-3: &tau;&#8315;, &tau;&#8314;, &nu;&tau; | Composites: p, n, p&#773;

### Quarks
Matter: u, d, s, c, t, b | Antimatter: u&#773;, d&#773;, s&#773;, c&#773;, t&#773;, b&#773;

### Bosons
Gauge: &gamma; (photon), g (gluon) | Weak: W+, W-, Z0 | Scalar: H0 (Higgs)

### Hypothetical
G (Graviton), DM (Dark Matter), DE (Dark Energy)

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

### Molecules

Type a molecular formula to spawn a complete molecule with correct 2D geometry. A database of
~50 templates covers common molecules from H₂ through C₆H₁₂O₆ (glucose). Live autocomplete
matches by formula prefix and name substring. Quick-access buttons provide one-click spawn for
H₂O, CO₂, NH₃, CH₄, NaCl, EtOH, C₆H₆, and Glucose. Atoms are placed at bond-rest-length
spacing (22px) and auto-bond within a few frames.

### Periodic Table

26 elements (H through Fe) can be spawned as complete atoms with hexagonal close-packed nuclei
and Bohr-model electron shells. Select an element, then click in the world to place.

Each section has configurable count (1-100), energy (0.1-1.0), and scatter radius (1-100px).
The **energy slider** directly affects spawn velocity: electron orbital speeds and single
particle thermal velocities scale as √E, so low-energy spawns produce visibly slower particles.

---

## Measurement Tools

Four measurement instruments accessible from **Menu > Measurement**. Each is a click-to-place
tool; only one placement mode can be active at a time. Placed instruments persist until removed
and update in real time. A right-side **Measurements** panel shows readings and controls for all
placed instruments.

### Thermometer Probe

Click to place a local temperature probe. Shows average kinetic energy of particles within an
adjustable radius, displayed as Kelvin alongside the particle count.

| Property | Detail |
|---|---|
| **Placement** | Single click to place (max 8 probes) |
| **Display** | Orange circle overlay + "T: NNN K (count)" label |
| **Radius** | Adjustable 20-300 px via Measurements panel slider |
| **Computation** | `T = avg(0.5 * |v|^2) * 0.1` for particles within radius |

### Velocity Meter

Click a particle to track its velocity in real time. Shows a directional arrow, speed value,
and kinetic energy. Automatically removed when the tracked particle dies.

| Property | Detail |
|---|---|
| **Placement** | Click on a particle to attach |
| **Display** | Green arrow from particle + "v=NNN KE=N.NNN" label |
| **Tracking** | Follows particle position each frame; auto-removes on death |

### Distance Ruler

Two-click placement: set start point, then end point. Displays distance in real-world
nanometer scale as a labeled line with tick marks at each endpoint.

| Property | Detail |
|---|---|
| **Placement** | Two clicks to define endpoints |
| **Display** | Yellow line with perpendicular tick marks + distance label in nanometers |
| **Measurement** | Euclidean distance converted to nanometers (1 Bohr radius = 0.0529 nm) |
| **Units** | Smart formatting: picometers for < 0.01 nm, 3 decimals for < 1 nm, 2 decimals for larger |

### Density Counter

Click to place a circular counting region. Shows the number of active particles within the
radius and the area density (count / pi*r^2).

| Property | Detail |
|---|---|
| **Placement** | Single click to place (max 8 counters) |
| **Display** | Purple dashed circle + "n=NNN rho=N.NNNN" label |
| **Radius** | Adjustable 20-300 px via Measurements panel slider |
| **Computation** | Count active particles within radius; density = count / (pi * r^2) |

### Measurements Panel

When any measurement instruments are placed, a right-side panel appears showing:
- Per-instrument readings with color-coded headers
- Radius sliders for thermometer probes and density counters
- Individual remove buttons (X) for each instrument
- **Clear All** button to remove everything

---

## Visualization Tools

Eight visualization overlays accessible from **Menu > Visualization**. Toggle each independently
via checkbox menu items. Overlays render on the foreground draw list above particles but below
UI windows.

| Overlay | Description |
|---|---|
| **Show Trails** | GPU-side particle path fade effect |
| **Electron Cloud** | Bohr-model orbital shell rings around nuclei (see [Electron Cloud](#electron-cloud-visualization)) |
| **Wave Mode** | Renders all particles as de Broglie wave packets instead of point particles. Wavelength inversely proportional to momentum (lambda = h/p). Gaussian-enveloped oscillating waves with time-animated phase, additive blending |
| **Atom Grid** | Overlay grid where each cell equals one hydrogen atom diameter (2 x Bohr radius = 0.106 nm). Camera-aware with auto-hide when cells < 4px. Every 10th line highlighted. Scale label at bottom-left |
| **Trajectory Tracer** | Records last 120 positions per particle, draws fading polylines. Useful for tracking individual particle paths through interactions |
| **Energy Heatmap** | 32x18 grid overlay colored blue-to-red by average kinetic energy density. Opacity adjustable (default 0.3) |
| **Velocity Field** | Arrow grid showing average velocity direction and magnitude per cell. Arrow length scales with speed |
| **Force Vectors** | Shows approximate force breakdown on the selected particle: Coulomb (red), Yukawa (green), Gravity (blue). Arrows use logarithmic magnitude scaling with labeled tips |

---

## Tools

Interactive tools accessible from **Menu > Tools** in the top menu bar. Only one placement tool
can be active at a time; activating a tool disables select mode and spawn mode.

### Particle Accelerator

Fire high-energy projectiles at a target particle.

1. Activate via **Menu > Tools > Accelerator**
2. **Click a particle** to set it as the target (gold crosshair indicator)
3. **Click anywhere** to fire projectiles from that position toward the target

The target particle is tracked across frames with a persistent gold crosshair. If the target
dies or is lost, a notification appears and you can select a new target.

| Setting | Range | Default | Effect |
|---|---|---|---|
| Projectile Type | Any particle type | Proton | Type of fired projectile |
| Speed | 50 - 500 px/frame | 200 | Launch velocity toward target |
| Fire Mode | Single / Triple / Stream | Single | Projectiles per shot |

- **Single**: one projectile per click
- **Triple**: three projectiles in a narrow spread pattern
- **Stream**: continuous fire while holding left mouse button (configurable interval)

A dashed aim line is drawn from the mouse position toward the target, with an arrowhead
indicating the fire direction.

### Mirror

Place reflective line segments that particles bounce off with specular reflection.

1. Activate via **Menu > Tools > Mirror**
2. **Click** to place the first endpoint
3. **Click again** to place the second endpoint — the mirror appears immediately

Mirrors are force objects and persist until deleted. They render as silver-blue glowing lines
with a shimmer animation. Particles reflect with configurable elasticity (coefficient of
restitution). Mirrors can be moved and deleted from the **Force Objects** panel like any other
force object.

| Property | Detail |
|---|---|
| **Reflection** | Specular — velocity component normal to mirror surface is reversed |
| **Elasticity** | Configurable coefficient of restitution (default 0.9) |
| **Rendering** | Silver-blue core with glow falloff and animated shimmer |
| **GPU-side** | Post-integration reflection in compute shader (not a force — particles bounce) |

### Utility Tools

Additional tools available in the **Menu > Tools** popup:

| Tool | Effect |
|---|---|
| **Halt Velocities** | Instantly zeroes all particle velocities (freeze-frame) |
| **Remove Massless** | Deletes all photons, gluons, gravitons, and neutrinos |
| **Remove Massive** | Deletes all massive particles |

Element import is available from **Menu > File > Import Element**.

---

## Achievements

An achievement system tracks **52 milestones** across 5 categories. Achievements persist across
sessions via a `.ppach` save file. Open the achievements panel from **Menu > Achievements**.

### Categories

| Category | Achievements | Examples |
|---|---|---|
| **Nuclear Physics** | 11 | First Fusion, First Fission, First Annihilation, Chain Reaction, 100 Fusions, Nuclear Demolition (spallation), Einstein's Nobel (photoelectric), Something from Nothing (pair production) |
| **Element Creation** | 11 | Create Hydrogen, Helium, Lithium, Carbon, Oxygen, Iron (peak binding energy), Gold, Uranium; 10/25 distinct elements |
| **Particle Zoo** | 9 | First Positron, First Neutrino, First Muon, First Tau, First Antiproton, First Quark, First Boson, Dark Matter, All 33 types simultaneously |
| **Thermodynamics** | 5 | Reach 1,000 K, 1 MK, 1 GK, 10 GK (Quark Epoch); Cool below 2 K |
| **Milestones** | 16 | 1000/5000/10000 particles, Entangled Pairs, First Force Object, First Mirror, CERN at Home (accelerator), First Save/Load, Element Export/Import, 100 Annihilations, 50 Nuclear Decays, Antimatter Atom, Try All 12 Environments |

When an achievement unlocks, a toast notification appears. The achievements panel shows progress
with unlocked/locked status and descriptions for each achievement.

---

## Save / Load

Simulation state can be saved and loaded in two binary formats.

### Simulation Files (.ppsg)

| Feature | Detail |
|---|---|
| **Hotkeys** | `Ctrl+S` save, `Ctrl+L` load |
| **UI** | Save/Load buttons in bottom bar, pause menu, and Tools popup |
| **Format** | Binary `.ppsg` (magic `0x47535050`, version 1) |
| **Contents** | Full SimConfig, particle positions/velocities/energies/types/angles/genomes, per-type data (forces, colors, behavior flags), force objects, UI field state |
| **File browser** | Built-in file browser with directory navigation, file size display, and path editing |

### Element Files (.ppel)

| Feature | Detail |
|---|---|
| **Export** | From Element Detail Card > Export button |
| **Import** | From Menu > Tools > Import Element |
| **Format** | Binary `.ppel` (magic `0x4C455050`, version 1) |
| **Contents** | Z, N, electron count, all constituent particles with positions (relative to nucleus center), velocities, energies, types, genomes |
| **Portability** | Positions stored as offsets from centroid — elements can be imported into any simulation at any location |

---

## Performance

The CPU-side physics engine uses several optimization strategies to maintain real-time performance
at high particle counts.

### Spatial Acceleration Grid

A uniform 2D grid (30px cells, 86 x 49 = 4,214 cells) accelerates all neighbor queries from
O(n^2) to O(n * k) where k is the average number of particles per cell.

| Property | Detail |
|---|---|
| **Cell size** | 30px (covers largest search radius used by any physics check) |
| **Build** | Single O(n) pass per frame: count, prefix sum, scatter |
| **Functions accelerated** | `check_annihilation` (5px), `check_fusion` (8px), `check_fission` (12px), `check_photoelectric` (25px), `update_orbitals` BFS (10px), `update_bonds` (28px form radius), `check_virtual_pairs` (15px), `check_hadronization` (45px confinement, 15px baryon, 12px gluon) |
| **Fallback** | Brute-force O(n^2) when spatial grid is disabled in settings |

### Batched GPU Synchronization

All CPU-side physics modifications (annihilation, fusion, fission, decay, nuclear decay,
photoelectric, spallation, virtual pairs, hadronization) set a `cpu_particles_dirty_` flag instead of
individually syncing with the GPU. A single `vkDeviceWaitIdle` + buffer write occurs at the
end of each frame, reducing GPU stalls from 8-10 per frame to at most 1.

### OpenMP Parallelization

| Loop | Strategy |
|---|---|
| **Statistics** (type counts, energy totals) | `parallel for` with array reduction |
| **Thermometer probes** (per-probe O(n) scan) | `parallel for` with `reduction(+:total_ke,cnt)` |
| **Density counters** (per-counter O(n) scan) | `parallel for` with `reduction(+:cnt)` |
| **Visualization grid** (32x18 binning) | Thread-local grids merged via `critical` section |

All parallel loops use `if(n > 2000)` guards to avoid overhead on small particle counts.

### Performance Settings

Three tuning knobs in **Settings > Performance**:

| Setting | Range | Default | Effect |
|---|---|---|---|
| **Physics Quality** | Low / Medium / High | High | Controls which CPU physics checks run and at what frequency. Low skips expensive checks (photoelectric, virtual pairs, spallation); Medium runs them at reduced frequency |
| **Physics Skip** | 0 - 4 | 0 | Skip CPU physics every N frames. 0 = every frame (best quality), higher = better FPS |
| **Spatial Grid** | on / off | on | Enable spatial acceleration grid for neighbor queries. Disable only for debugging |

---

## UI Themes

Eight color themes are available in **Settings > Theme**:

| # | Theme | Background | Accent |
|---|---|---|---|
| 0 | **Dark Navy** | Navy | Cyan |
| 1 | **Midnight** | Deep navy | Violet |
| 2 | **Slate** | Grey-blue | Teal |
| 3 | **Ember** | Charcoal | Orange |
| 4 | **Synthwave** | Dark magenta | Hot pink |
| 5 | **Forest** | Dark green | Lime green |
| 6 | **Arctic** | Dark steel-blue | Ice white-blue |
| 7 | **Solar** | Near-black warm | Golden yellow |

User preferences (theme, temperature unit, FPS cap, thread count, UI scale, physics quality,
physics skip, spatial grid) are persisted across sessions.

---

## Controls

| Key / Input | Action |
|---|---|
| `Escape` | Pause menu (Resume / New / Save / Load / About / Quit). About re-shows animated splash |
| `F1` | Toggle settings panel |
| `F2` | Reset simulation |
| `F3` | Open / close Spawn Picker |
| `F4` | Toggle Select mode (click to inspect particles) |
| `Space` | Pause / unpause |
| `Ctrl+S` | Save simulation |
| `Ctrl+L` | Load simulation |
| `W A S D` | Pan camera |
| Left drag | Pan camera (mouse) |
| Scroll wheel | Zoom in / out |
| Left click | Place particle (spawn mode) / Select particle (select mode) / Fire accelerator / Place mirror endpoint |

> **Tools** — Open **Menu > Tools** to access the Particle Accelerator, Mirror, Electron Cloud,
> Halt Velocities, trail visualization, and particle removal utilities.

> **Info Card** — Select a particle to see its type, charge, spin, energy, age, momentum,
> temperature, magnetic moment, orbital parent, and element membership. If the particle
> belongs to a nucleus, click the element button to open the **Element Detail Card** with
> full composition, stability info, and Move / Delete / Duplicate / Export actions.
>
> **Element List** — Click the **"Atoms: N"** or **"Mol: M"** counter in the bottom bar to
> open a scrollable list of all detected atoms and molecules. Molecules display Hill system
> molecular formulas (e.g. H₂, CH₄). Free atoms show symbol, mass number, name, charge,
> and a stability indicator. Click any molecule to open its Molecule Detail Card, or any
> free atom to navigate to it and open its Element Detail Card.
>
> **Event Log** — Click the **"Events: N"** counter in the bottom bar to open the decay/event
> log showing all physics events (decays, fusions, fissions, annihilations, photoelectric,
> spallation, pair production, and more) with frame timestamps and color-coded type tags.

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

Compiled SPIR-V shaders are written to both `build/shaders/` and `shaders/` (source directory).

### Run

```bash
./build/particle_physics
```

---

## Architecture

```
EmergentEvolution/
├── src/
│   ├── types.h                  # SimConfig, PushConstants (128 bytes), shared constants
│   ├── particles.h/.cpp         # CPU arrays, particle type data
│   ├── vulkan_context.h/.cpp    # Vulkan instance, device, swapchain, buffer helpers
│   ├── compute_pipeline.h/.cpp  # 18-binding descriptor layout, buffer lifecycle, readback
│   ├── renderer.h/.cpp          # Fullscreen-quad pipeline, ImGui integration
│   ├── stb_image.h              # Single-header image loading (window icon)
│   └── stb_image_impl.cpp       # stb_image implementation unit
├── src/physics/
│   ├── phys_particles.h/.cpp    # 33 particle types, masses, charges, decay rates, isotope table, environments
│   ├── interface.h/.cpp         # ImGui: animated splash screen, spawn picker, molecule spawn UI, force multipliers, element cards, molecule cards, bond overlay, tools, event log, achievements
│   ├── simulation.h/.cpp        # Main loop: fusion, fission, decay, orbitals, covalent bonds, molecule detection, molecule spawn, entanglement, photoelectric, spallation, hadronization, spatial grid
│   ├── molecules.h              # ~50 molecule templates with 2D geometry, formula parser, database lookup
│   ├── achievements.h/.cpp      # 36 achievements across 5 categories with persistence
│   ├── save_load.h/.cpp         # Binary .ppsg/.ppel save/load/export/import serialization
│   └── main.cpp                 # Entry point (borderless maximized, window icon from assets/)
├── assets/
│   ├── banner.html              # Animated splash screen reference (HTML5 canvas)
│   ├── icon_32/64/128/256/512.png  # Window icons (multiple sizes)
│   └── particle_playground.ico  # Windows ICO format
├── shaders/
│   ├── physics.comp             # GPU: 7 forces, covalent bond springs, centrifugal barrier, hard-sphere, mirror reflection, bond line rendering, 5 field viz, wave packet rendering
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
| 17 | force objects | read (gravity wells, repulsors, attractors, heat sources, vortices, mirrors) |

A/B buffers ping-pong each tick. All buffers are HOST_VISIBLE + HOST_COHERENT for CPU readback.

### Push Constants (128 bytes)

| Offset | Field | Description |
|---|---|---|
| 0-7 | region_size | World dimensions (vec2) |
| 8-15 | camera_origin | Camera center (vec2) |
| 16-19 | particle_count | Active particles |
| 20-23 | particle_types | MAX_PARTICLE_TYPES |
| 24-27 | dt | Timestep |
| 28-31 | step | Dispatch step (0=physics, 1=render) |
| 32-35 | camera_zoom | Current zoom level |
| 36-71 | physics params | radius, dampening, repulsion, interaction, density, viscosity, pressure, density_cap, temperature |
| 72-75 | gravity_strength | Gravity multiplier |
| 76-79 | lorentz_strength | Effective magnetic field |
| 80-83 | vacuum_energy | ZPE floor |
| 84-87 | field_flags | Field visualization bitmask (bits 0-4: field types, bit 5: wave mode) |
| 88-91 | weak_coupling | Weak force strength |
| 92-95 | string_tension | QCD confinement |
| 96-99 | higgs_vev | Higgs VEV |
| 100-103 | force_object_count | Active force emitters |
| 104-127 | force multipliers | coulomb, yukawa, pauli, alpha_s, compton, annihilation (6 x float) |

---

<div align="center">

Made with C++20, Vulkan & Dear ImGui · Night-Traders-Dev 2026

</div>
