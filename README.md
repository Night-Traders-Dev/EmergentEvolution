<div align="center">

# Particle Playground

**A GPU-accelerated quantum particle physics and chemistry sandbox**

Standard Model + Beyond · Nuclear fusion & fission · Orbital mechanics · Emergent thermodynamics · Save/Load

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-NTD--2026-green.svg)]()

</div>

---

Particle Playground ships two simulation modes sharing the same Vulkan compute engine:

- **Particle Physics** (`particle_physics`) — 33 particle types spanning the Standard Model and
  beyond, with real quantum mechanics: Coulomb + Yukawa + QCD forces, centrifugal barrier orbitals,
  nuclear fusion/fission, radioactive decay with realistic isotope half-lives, Compton scattering,
  hard-sphere collisions, emergent thermodynamics, virtual particle pair creation, element
  detection with info cards, and six per-force multiplier knobs
- **Particle Chemistry** (`particle_life`) — 18 elements with persistent covalent/ionic bonds,
  molecular aggregates, vesicles, proto-cells, and Darwinian evolution

Both simulate up to **22,500 particles** in real time on a toroidal 2560 x 1440 world using O(n^2)
pairwise GPU compute shaders.

---

## Table of Contents

- [Physics Engine](#physics-engine)
- [Particle Physics Mode](#particle-physics-mode)
  - [Standard Model + Beyond — 33 Particle Types](#standard-model--beyond--33-particle-types)
  - [Four Fundamental Forces + Multipliers](#four-fundamental-forces--multipliers)
  - [Orbital Mechanics](#orbital-mechanics)
  - [Nuclear Fusion](#nuclear-fusion)
  - [Nuclear Fission](#nuclear-fission)
  - [Radioactive Decay](#radioactive-decay)
  - [Isotope Half-Lives](#isotope-half-lives)
  - [Element Detection & Info Cards](#element-detection--info-cards)
  - [Element List & Event Notifications](#element-list--event-notifications)
  - [Compton Scattering](#compton-scattering)
  - [Hard-Sphere Collisions](#hard-sphere-collisions)
  - [Virtual Particle Pairs](#virtual-particle-pairs)
  - [Emergent Thermodynamics](#emergent-thermodynamics)
  - [Field Visualization](#field-visualization)
  - [Environment Presets](#environment-presets-physics)
  - [Spawn Picker — Physics](#spawn-picker--physics)
  - [Save / Load](#save--load)
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
| Push Constants | 128 bytes (Vulkan guaranteed minimum) — all simulation parameters per frame |

---

# Particle Physics Mode

A particle sandbox where protons, neutrons, electrons, quarks, gauge bosons, and hypothetical
particles interact through all four fundamental forces. Electrons orbit nuclei via quantum-mechanical
centrifugal barriers, nucleons fuse under extreme temperature and pressure, heavy nuclei undergo
fission when struck by fast neutrons, and virtual particle-antiparticle pairs spontaneously appear
from high-energy encounters.

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
| **Gravity** | Newtonian 1/r^2 between massive particles | 0.0-2.0 slider | -- |
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

## Element Detection & Info Cards

Nuclei are dynamically detected each frame by BFS clustering protons and neutrons within
a 10px radius. Each cluster yields an element identity (Z, N, A) with bound electrons
counted by proximity.

**Particle Info Card** (bottom-right notification):
- Shows particle type, charge, spin, mass, energy, age, momentum
- If the particle belongs to a nucleus: shows element name, composition, and a clickable
  link to the **Element Detail Card**

**Element Detail Card** (bottom-right, left of info card):
- Full element name and symbol (all 118 elements)
- Composition: Z, N, A, electron count, shell configuration
- Net charge, total mass, momentum, age
- **Stability indicator** with isotope half-life lookup (color-coded)
- Clickable nucleon/electron particle list for navigation
- **Move**: relocate entire element (all constituent particles) by clicking
- **Delete**: remove all particles in the element
- **Duplicate**: spawn a copy of the element nearby with correct orbital structure

---

## Element List & Event Notifications

**Element List** — The bottom bar displays a clickable **"Elements: N"** counter showing
the total number of detected elements in the simulation. Clicking opens a centered,
scrollable window listing every element with:

- **Stability dot**: green (stable), yellow (long-lived), orange (medium), red (short-lived)
- **Composition**: symbol, mass number (A), element name, charge, electron count
- **Tooltip**: Z, N, electrons, decay mode and half-life for unstable isotopes
- **Click to inspect**: clicking any row navigates the camera to that element and opens
  its Element Detail Card

**Event Notifications** — Toast-style notifications appear in the top-right corner when
nuclear events occur, stacking vertically with a 5-second timeout and fade-out:

| Event | Example | Color |
|---|---|---|
| **Fusion** | `Fusion: p + p → d + e⁺ + ν` | Cyan |
| **Fission** | `Fission: 8-nucleon cluster split + 3n` | Orange |
| **Particle Decay** | `Decay: μ⁻ → e⁻ + νμ + ν̄e` | Warm yellow |
| **Nuclear Decay** | `α Decay: U-238 → Th-234 + He-4` | Red-orange |

Up to 8 notifications can stack simultaneously; oldest are dropped when the limit is reached.

---

## Compton Scattering

High-energy photons interact bidirectionally with charged matter:

**Photon fast-path** (GPU):
- Scans nearby charged particles (strided sampling, 30px radius)
- Photon deflects toward closest charged particle, losing energy proportional to proximity
- Maintains light speed after deflection

**Matter response** (GPU j-loop):
- Photon radiation pressure pushes charged matter along photon travel direction
- Photon's oscillating B-field exerts Lorentz force: `F = q * Bz * (vy, -vx)`
- Both scale with photon energy / r^2

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

<a name="environment-presets-physics"></a>
## Environment Presets (Physics)

Twelve presets spanning vacuum to dark sector. Select from the **Environment** dropdown.

| # | Environment | Temperature | Key Features |
|---|---|---|---|
| 0 | **Lab Mode** | 1 K | Empty vacuum — manual spawning only |
| 1 | **Hydrogen Plasma** | 1.5 x 10^7 K | Hot ionized hydrogen, fusion conditions |
| 2 | **Neutron Star** | 10^9 K | Ultra-dense neutron matter |
| 3 | **Solar Core** | 1.5 x 10^7 K | Hydrogen + gravity — stellar fusion |
| 4 | **Particle Soup** | 5 x 10^3 K | Mixed light particles at moderate energy |
| 5 | **Alpha Emitter** | 300 K | Heavy nuclei, room temperature |
| 6 | **Heavy Nucleus** | 100 K | Cold dense nuclear matter |
| 7 | **Quark-Gluon Plasma** | 2 x 10^12 K | Deconfined quarks and gluons |
| 8 | **Electroweak Era** | 10^15 K | W/Z/Higgs bosons above symmetry breaking |
| 9 | **Meson Factory** | 5 x 10^11 K | Quark-antiquark pairs forming mesons |
| 10 | **Particle Accelerator** | 10^8 K | High-energy protons + synchrotron radiation |
| 11 | **Dark Sector** | 10^3 K | 40% DM, 30% p, 15% e, 10% DE, 5% gravitons |

---

## Spawn Picker — Physics

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

Each section has configurable count (1-100), energy (0.1-1.0), and scatter radius (1-100px).

---

## Save / Load

Simulation state can be saved and loaded as binary `.ppsg` files.

| Feature | Detail |
|---|---|
| **Hotkeys** | `Ctrl+S` save, `Ctrl+L` load |
| **UI** | Save/Load buttons in bottom bar and pause menu |
| **Format** | Binary `.ppsg` (magic `0x47535050`, version 1) |
| **Contents** | Full SimConfig, particle positions/velocities/energies/types/angles/genomes, per-type data (forces, colors, behavior flags), force objects, UI field state |

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

---

## Controls

| Key / Input | Action |
|---|---|
| `Escape` | Pause menu (Resume / New / Save / Load / About / Quit) |
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
| Left click | Place particle (spawn mode) / Select particle (select mode) |

> **Info Card** — Select a particle to see its type, charge, spin, energy, age, momentum,
> temperature, magnetic moment, orbital parent, and element membership. If the particle
> belongs to a nucleus, click the element button to open the **Element Detail Card** with
> full composition, stability info, and Move / Delete / Duplicate actions.
>
> **Element List** — Click the gold **"Elements: N"** counter in the bottom bar to open a
> scrollable list of all detected elements. Each row shows symbol, mass number, name,
> charge, electrons, and a stability indicator. Click any element to navigate to it and
> open its detail card.

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
│   ├── types.h                  # SimConfig, PushConstants (128 bytes), shared constants
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
│   ├── phys_particles.h/.cpp    # 33 particle types, masses, charges, decay rates, isotope table, environments
│   ├── interface.h/.cpp         # Physics ImGui: spawn picker, force multipliers, element cards, save/load
│   ├── simulation.h/.cpp        # Physics main loop: fusion, fission, decay, nuclear isotope decay, orbitals
│   ├── save_load.h/.cpp         # Binary .ppsg save/load serialization
│   └── main.cpp                 # Physics entry point (borderless maximized window)
├── shaders/
│   ├── compute.comp             # Chemistry GPU: forces, bonds, metabolism
│   ├── physics.comp             # Physics GPU: 7 forces, centrifugal barrier, hard-sphere, 5 field viz
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
| 84-87 | field_flags | Field visualization bitmask |
| 88-91 | weak_coupling | Weak force strength |
| 92-95 | string_tension | QCD confinement |
| 96-99 | higgs_vev | Higgs VEV |
| 100-103 | force_object_count | Active force emitters |
| 104-127 | force multipliers | coulomb, yukawa, pauli, alpha_s, compton, annihilation (6 x float) |

---

<div align="center">

Made with C++20, Vulkan & Dear ImGui · Night-Traders-Dev 2026

</div>
