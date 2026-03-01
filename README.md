<div align="center">

# Particle Playground

**GPU-accelerated particle physics sandbox**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

</div>

---

Particle Playground is a real-time physics sandbox that simulates 67 particle types across the
Standard Model, Beyond Standard Model, and 34 hypothetical particles. All four fundamental forces
run simultaneously on Vulkan compute shaders with optional general relativity extensions
(mass-energy equivalence, frame dragging, gravitational waves with physical tidal forces).
CPU-side processes handle nuclear reactions, decay, orbital mechanics, covalent bonding,
hadronization, and more.

Up to **100,000 particles** in real time. GPU handles O(n&#178;) pairwise forces; CPU physics
uses a **spatial acceleration grid** with **OpenMP** parallelization for O(n) neighbor queries.

---

## Table of Contents

- [Physics Engine](#physics-engine)
- [Particle Types](#particle-types)
- [Forces](#forces)
  - [General Relativity Extensions](#general-relativity-extensions)
- [Orbital Mechanics](#orbital-mechanics)
- [Covalent Bonds & Molecules](#covalent-bonds--molecules)
- [Nuclear Reactions](#nuclear-reactions)
- [Radioactive Decay & Isotopes](#radioactive-decay--isotopes)
- [Photon-Matter Interactions](#photon-matter-interactions)
- [Spallation & Photonuclear Processes](#spallation--photonuclear-processes)
- [Hadronization & Color Confinement](#hadronization--color-confinement)
- [Gluon Interactions](#gluon-interactions)
- [Virtual Particles & Casimir Effect](#virtual-particles--casimir-effect)
- [Quantum Entanglement](#quantum-entanglement)
- [Emergent Thermodynamics](#emergent-thermodynamics)
- [UI & Visualization](#ui--visualization)
  - [Display Settings](#display-settings)
  - [Accessibility](#accessibility)
  - [Achievements](#achievements)
  - [Sound Effects](#sound-effects)
  - [Gamepad Support](#gamepad-support)
  - [Error Dialogs](#error-dialogs)
- [Tutorial & Onboarding](#tutorial--onboarding)
- [Scenarios & Gameplay](#scenarios--gameplay)
- [Environment Presets](#environment-presets)
- [Save / Load](#save--load)
- [Controls](#controls)
- [Build](#build)
  - [CMake Options](#cmake-options)
  - [Steam Integration](#steam-integration-optional)
- [Architecture](#architecture)

---

## Physics Engine

| Property | Detail |
|---|---|
| Particle count | Up to **100,000** simultaneous particles |
| GPU forces | O(n&#178;) pairwise per-frame on Vulkan compute shader |
| CPU physics | O(n) via **spatial acceleration grid** (30px cells, 342&times;193 = 65,906 cells) |
| Parallelism | **OpenMP** across all cores for grid builds, entropy, statistics, measurement, B-field visualization |
| GPU sync | Single `vkQueueWaitIdle` per frame (batched dirty flag) |
| GPU post-processing | **Half-resolution bloom** (extract + H/V Gaussian blur + composite), zoom-adaptive particle sizing |
| World | 10,240 &times; 5,760 px toroidal space |
| Buffers | Double-buffered ping-pong (position, velocity, angle, angular velocity, energy, genome) |
| Genome | 4 floats per particle: charge, spin, color charge / orbital L, decay rate |
| Push Constants | 128 bytes &mdash; full simulation parameters per frame |

---

## Particle Types

67 types across 14 families. Types 0&ndash;32 cover the Standard Model and Beyond Standard Model;
types 33&ndash;66 are hypothetical (dark matter candidates, SUSY sparticles, GUT bosons,
theoretical extremes).

<details>
<summary><b>Full particle table</b> (click to expand)</summary>

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
  <td>25</td><td><b>Gluon</b> g</td><td>100.0</td><td>0</td><td>+1</td><td>SU(3) color-anticolor, 8 octet states</td></tr>
<tr><td>26</td><td><b>W+</b></td><td>0.00012</td><td>+1</td><td>+1</td><td>Instant decay to lepton + &nu;</td></tr>
<tr><td>27</td><td><b>W-</b></td><td>0.00012</td><td>-1</td><td>-1</td><td>Instant decay to lepton + &nu;</td></tr>
<tr><td>28</td><td><b>Z0</b></td><td>0.00011</td><td>0</td><td>0</td><td>Instant decay to e&#8315; + e&#8314;</td></tr>
<tr><td>29</td><td><b>Higgs</b> H0</td><td>0.00008</td><td>0</td><td>0</td><td>Instant decay to 2&gamma;</td></tr>
<tr><td rowspan="3"><b>Beyond SM</b></td>
  <td>30</td><td><b>Graviton</b> G</td><td>100.0</td><td>0</td><td>+2</td><td>Massless, ballistic</td></tr>
<tr><td>31</td><td><b>Dark Matter</b> DM</td><td>0.001</td><td>0</td><td>+0.5</td><td>WIMP &mdash; gravity only</td></tr>
<tr><td>32</td><td><b>Dark Energy</b> DE</td><td>100.0</td><td>0</td><td>0</td><td>Universal repulsive field</td></tr>
<tr><td rowspan="6"><b>DM Candidates</b></td>
  <td>33</td><td><b>Axino</b></td><td>5.0</td><td>0</td><td>+0.5</td><td>SUSY axion partner</td></tr>
<tr><td>34</td><td><b>WIMPzilla</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Ultra-heavy, gravity only</td></tr>
<tr><td>35</td><td><b>SIMP</b></td><td>0.001</td><td>0</td><td>+0.5</td><td>Self-interacting DM, 30&times; self-gravity</td></tr>
<tr><td>36</td><td><b>Sterile Neutrino</b></td><td>100.0</td><td>0</td><td>+0.5</td><td>Gravity-only neutrino</td></tr>
<tr><td>37</td><td><b>Dark Photon</b> A'</td><td>100.0</td><td>0</td><td>+1</td><td>Kinetically mixed</td></tr>
<tr><td>38</td><td><b>Q-Ball</b></td><td>~0</td><td>+1</td><td>0</td><td>SUSY soliton, ultra-heavy</td></tr>
<tr><td rowspan="11"><b>SUSY Sparticles</b></td>
  <td>39</td><td><b>Selectron</b></td><td>~0</td><td>-1</td><td>0</td><td>Decays to e + neutralino</td></tr>
<tr><td>40</td><td><b>Smuon</b></td><td>~0</td><td>-1</td><td>0</td><td>Decays to &mu; + neutralino</td></tr>
<tr><td>41</td><td><b>Stau</b></td><td>~0</td><td>-1</td><td>0</td><td>Decays to &tau; + neutralino</td></tr>
<tr><td>42</td><td><b>Squark</b></td><td>~0</td><td>+2/3</td><td>0</td><td>Color-charged, decays to q + gluino</td></tr>
<tr><td>43</td><td><b>Gluino</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Decays to g + neutralino</td></tr>
<tr><td>44</td><td><b>Photino</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Decays to &gamma; + neutralino</td></tr>
<tr><td>45</td><td><b>Wino</b></td><td>~0</td><td>+1</td><td>+0.5</td><td>Decays to W + neutralino</td></tr>
<tr><td>46</td><td><b>Zino</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Decays to Z + neutralino</td></tr>
<tr><td>47</td><td><b>Higgsino</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Decays to H + neutralino</td></tr>
<tr><td>48</td><td><b>Neutralino</b> N&#8321;</td><td>~0</td><td>0</td><td>+0.5</td><td>LSP, stable DM candidate</td></tr>
<tr><td>49</td><td><b>Sneutrino</b></td><td>~0</td><td>0</td><td>0</td><td>Decays to &nu; + neutralino</td></tr>
<tr><td rowspan="6"><b>Force Carriers</b></td>
  <td>50</td><td><b>Gravitino</b></td><td>100.0</td><td>0</td><td>+1.5</td><td>Spin-3/2 graviton partner</td></tr>
<tr><td>51</td><td><b>X Boson</b></td><td>~0</td><td>+4/3</td><td>+1</td><td>GUT leptoquark</td></tr>
<tr><td>52</td><td><b>Y Boson</b></td><td>~0</td><td>+1/3</td><td>+1</td><td>GUT leptoquark</td></tr>
<tr><td>53</td><td><b>Magnetic Monopole</b></td><td>~0</td><td>0</td><td>0</td><td>Radial B-field (g/r&#178;)</td></tr>
<tr><td>54</td><td><b>Radion</b></td><td>~0</td><td>0</td><td>0</td><td>Extra-dimension scalar</td></tr>
<tr><td>55</td><td><b>Dilaton</b></td><td>~0</td><td>0</td><td>0</td><td>String theory scalar</td></tr>
<tr><td rowspan="9"><b>Theoretical Extremes</b></td>
  <td>56</td><td><b>Tachyon</b></td><td>50.0</td><td>0</td><td>0</td><td>Superluminal (v &gt; 1.33c)</td></tr>
<tr><td>57</td><td><b>Preon</b></td><td>~0</td><td>0</td><td>+0.5</td><td>Sub-quark constituent</td></tr>
<tr><td>58</td><td><b>Inflaton</b></td><td>~0</td><td>0</td><td>0</td><td>DE-like repulsion</td></tr>
<tr><td>59</td><td><b>Majoron</b></td><td>100.0</td><td>0</td><td>0</td><td>Goldstone boson</td></tr>
<tr><td>60</td><td><b>Odderon</b></td><td>0.002</td><td>0</td><td>+3</td><td>C-odd gluonic composite</td></tr>
<tr><td>61</td><td><b>Glueball</b></td><td>0.003</td><td>0</td><td>0</td><td>Pure glue bound state</td></tr>
<tr><td>62</td><td><b>Skyrmion</b></td><td>0.005</td><td>+1</td><td>+0.5</td><td>Topological soliton</td></tr>
<tr><td>63</td><td><b>X17</b></td><td>100.0</td><td>0</td><td>+1</td><td>Anomalous boson (17 MeV)</td></tr>
<tr><td>64</td><td><b>Chameleon</b></td><td>100.0</td><td>0</td><td>0</td><td>DE-like scalar field</td></tr>
<tr><td rowspan="2"><b>New Class</b></td>
  <td>65</td><td><b>Paraparticle</b></td><td>~0</td><td>0</td><td>+0.33</td><td>Exotic statistics</td></tr>
<tr><td>66</td><td><b>Dyn. Axion QP</b></td><td>100.0</td><td>0</td><td>0</td><td>Condensed-matter axion analog</td></tr>
</tbody>
</table>

</details>

---

## Forces

All forces act simultaneously in the compute shader. Six have independent **multiplier sliders**
(0&ndash;3&times;, default 1.0 = Standard Model values) exposed in the Force Multipliers panel.

| Force | Implementation | Multiplier |
|---|---|---|
| **Electromagnetic** | Coulomb 1/r&#178; + Biot-Savart B-field + Lorentz force F=q(v&times;B) | `coulomb_strength` |
| **Strong nuclear** | Yukawa attractive (16px range, smoothstep window 12&ndash;16px) + Pauli hard-core repulsion (6px) + nuclear surface tension + velocity damping | `yukawa_strength`, `pauli_multiplier` |
| **QCD color** | Cornell potential with running coupling &alpha;_eff = &alpha;_s &middot; max(0.3, 1 + 0.3 ln r) | `alpha_s_scale` |
| **Weak** | Short-range Yukawa (0.8px) + stochastic decay (CPU) | &mdash; |
| **Gravity** | Newtonian 1/r&#178; with optional GR extensions (see below) | &mdash; |
| **Compton** | Photon radiation pressure + oscillating B-field on charges (30px) | `compton_strength` |
| **Annihilation** | Matter-antimatter attraction at contact (6px) | `annihilation_strength` |

Additional force behaviors:

- **Dark matter** &mdash; gravity only (20&times; self-gravity, 5&times; DM-normal), no EM/strong/weak
- **SIMP** &mdash; 30&times; enhanced self-gravity, clusters unlike cold DM
- **Dark energy** &mdash; universal repulsion growing with distance (cosmological constant analog)
- **Magnetic monopole** &mdash; static radial B-field (g/r&#178;), Lorentz deflection on charges
- **Tachyon** &mdash; superluminal floor (v &gt; 1.33c), rapid decay to &gamma;&gamma;
- **Higgs field** &mdash; tunable VEV (0&ndash;500), mass coupling to heavy particles
- **Hard-sphere collisions** &mdash; elastic position correction + velocity impulse (restitution 0.95)
- **Synchrotron radiation** &mdash; charged particles radiate energy proportional to q&#178;&gamma;&#178;v&#178; (subtle long-term drain)

### General Relativity Extensions

Three GR corrections toggled from **Menu > Visualization** (all enabled by default). Zero additional
push-constant bytes (encoded in `field_flags` bits 9&ndash;11).

| Extension | Physics |
|---|---|
| **Mass-Energy Gravity** | E=mc&#178;: gravitational mass = rest mass &times; &gamma;. Fast particles attract more strongly. |
| **Frame Dragging** | Gravitomagnetic Lense-Thirring analog: spinning masses drag nearby movers tangentially (~1% of Newtonian). |
| **Gravitational Waves** | Finite-speed gravity (retarded-time correction). Force direction lags behind fast-moving sources. |

When gravitational waves are enabled, accelerating massive particles emit **GW ripple rings** that
propagate at c across the entire simulation (1/r amplitude falloff). These rings are not just visual
&mdash; they exert physical **tidal forces** on particles they pass through:

- **Radial stretch**: outward kick along the source&rarr;particle axis
- **Tangential compression**: perpendicular squeeze (quadrupole "+" polarization)
- Amplitude falls off as 1/r, matching real GW strain decay
- Massless particles (photons, gravitons, gluons, neutrinos) are unaffected
- Wavefront shell is 30px thick &mdash; force is a transient pulse, not constant

---

## Orbital Mechanics

Electrons orbit nuclei via a quantum-mechanical centrifugal barrier (always active) that models the
Heisenberg uncertainty principle &mdash; confining an electron increases its kinetic energy, preventing
collapse into the nucleus. An optional **Orbital Drive** adds tangential velocity drive and boost.

**GPU** (physics.comp):
- **Centrifugal barrier** (always active): F = L&#178;&#8901;m_inv / (r&#179; + 1) &mdash; quantum ground state analog, prevents electron collapse
- **Orbital Drive** (toggle, Menu &gt; Visualization): tangential velocity drive, per-shell boost with compensating F_bind, asymmetric radial damping
- Spin-orbit coupling: fine structure correction proportional to spin &middot; L / r&#8308;
- Spin magnetic moment: dipole force &mu;&middot;&nabla;B in external field

**CPU** (update_orbitals):
- BFS clusters nucleons into nuclei (16px radius)
- Assigns electrons to nearest nucleus within 60px binding radius
- Fills shells: **1s** (2), **2s2p** (8), **3s3p3d** (18), **4s4p4d4f** (32)
- Computes L_ground per shell via Bohr model with Slater screening:
  R_target = n&#178; &middot; R_BOHR / Z_eff, where Z_eff = Z &minus; inner_electrons

**Atom spawning** (spawning.cpp):
- Nucleons placed via **force-relaxation**: hex-packed initial positions are iteratively adjusted until Yukawa, Pauli, and Coulomb forces balance to near-zero (up to 80 iterations, matches shader constants exactly)
- Electrons placed at Bohr orbital radii with velocity matching the centrifugal barrier equilibrium (Orbital Drive OFF) or boosted target velocity (Orbital Drive ON)

---

## Covalent Bonds & Molecules

Atoms share valence electrons to form covalent bonds. Bond formation and breaking run CPU-side;
GPU spring forces maintain geometry.

**Formation**: nuclei within search radius (default 28px) with compatible valence and sufficient
activation energy form bonds. Up to 6 bonds per particle, max 10 new bonds per frame.

**GPU spring physics**: Hooke's law (K=80, rest=22px) with dashpot damping (0.3). Bonds break
when stretched beyond rest &times; 2.2. Rendered as pale blue glowing lines.

**Molecule detection**: Union-Find groups bonded atoms each frame. Displayed as Hill system
molecular formulas (C first, H second, rest alphabetical) with total energy, age, and ionic charge.

**Molecule spawn**: Type a formula in the Spawn Picker to place complete molecules with correct
geometry. ~50 templates from H&#8322; through glucose (C&#8326;H&#8321;&#8322;O&#8326;).
Quick-access buttons for common molecules (H&#8322;O, CO&#8322;, NH&#8323;, CH&#8324;, etc.).
Geometry includes tetrahedral, trigonal planar, bent, linear, and ring configurations.

All bond parameters (spring K, rest length, break factor, form radius, activation energy) are
tunable in the Nuclear Debug window.

---

## Nuclear Reactions

### Fusion

Triggers when particles have sufficient kinetic energy to overcome the Coulomb barrier
(Gamow tunneling probability). All parameters exposed in the UI.

| Reaction | Products |
|---|---|
| **p + p** (pp chain) | p + n + e&#8314; + &nu;e |
| **p + n** (deuteron) | Bound p-n pair |
| **d + d** (implicit) | He-4 nucleus |

Configurable: Coulomb barrier (default 550 keV), fusion radius, max/frame, binding energy,
leptonic Q-value, product separation.

### Fission

Fast neutrons striking heavy nuclei trigger fission. The **Bohr-Wheeler fissility parameter**
(Z&#178;/A) gates which nuclei can fission &mdash; only nuclei above the threshold (default 35.0)
are unstable. U-235 (Z&#178;/A = 36.0) fissions; Carbon-12, Oxygen-16, and Iron-56 do not.

Products: cluster splits in half with energy kick + free neutrons (chain reaction fuel).
Emitted neutrons can trigger further fissions in dense nuclear matter.

Configurable: neutron threshold, min cluster size, fissility threshold, barrier energy,
fragment energy, free neutron count.

---

## Radioactive Decay & Isotopes

### Particle Decay

Particles below 0.08 energy undergo probabilistic decay based on per-type rates.

<details>
<summary><b>Decay channels</b></summary>

| Parent | Rate | Products |
|---|---|---|
| Top t | 0.50 | Bottom + real W+ (only quark heavy enough) |
| W+/W- | 0.50 | Lepton + Neutrino (on-shell W decay) |
| Z0 | 0.50 | e&#8315; + e&#8314; |
| Higgs H0 | 0.40 | 2&gamma; |
| Tau &tau; | 0.20 | e&#8315; + &nu;&tau; |
| Bottom b | 0.10 | Daughter + f f&#773; (3-body via virtual W&#8315;*, CKM weighted) |
| Charm c | 0.12 | Daughter + f f&#773; (3-body via virtual W&#8314;*, CKM weighted) |
| Strange s | 0.02 | Up + f f&#773; (3-body via virtual W&#8315;*, Q &asymp; 91 MeV) |
| Muon &mu; | 0.01 | e&#8315; + &nu;&mu; + &nu;e |

</details>

Matter-antimatter annihilation runs every frame at 5px contact radius: e&#8315;+e&#8314;,
p+p&#773;, &mu;+&mu;, &tau;+&tau;, and quark-antiquark pairs annihilate to photons.

### Isotope Half-Lives

Nuclei are identified via BFS clustering of protons and neutrons. Realistic half-life decay
is applied based on (Z, N) composition.

<details>
<summary><b>Decay modes & key isotopes</b></summary>

**Modes**: alpha (&alpha;), beta-minus (&beta;&#8315;), beta-plus (&beta;&#8314;),
neutron emission, proton emission.

| Isotope | Z | N | Mode | Sim Half-Life | Real Half-Life |
|---|---|---|---|---|---|
| Free neutron | 0 | 1 | &beta;&#8315; | 10 s | 10 min |
| Tritium H-3 | 1 | 2 | &beta;&#8315; | 1 min | 12.3 yr |
| He-5 | 2 | 3 | n-emit | instant | 7&times;10&#8315;&#178;&#178; s |
| Be-8 | 4 | 4 | &alpha; | instant | 6.7&times;10&#8315;&#185;&#8311; s |
| C-14 | 6 | 8 | &beta;&#8315; | 5 min | 5730 yr |
| U-235 | 92 | 143 | &alpha; | 3.3 min | 704 Myr |
| U-238 | 92 | 146 | &alpha; | 5 min | 4.5 Gyr |

~50 isotopes in the explicit table. Nuclei not listed fall through to general stability rules:
Z &gt; 83 alpha-decays, N/Z &gt; 1.5 beta-minus, N/Z &lt; 0.7 beta-plus, A = 5 or 8 instant
disintegration.

</details>

---

## Photon-Matter Interactions

High-energy photons interact with matter through GPU and CPU channels. Max 8 per frame.

**GPU (Compton scattering)**: photon deflects toward nearest charge within 30px, losing energy.
Radiation pressure pushes matter along photon direction. Oscillating B-field exerts Lorentz force.

**CPU processes**:

| Process | Threshold | Effect |
|---|---|---|
| Photoelectric effect | E&gamma; &ge; 1.5 &times; binding | Photon absorbed, electron ionized |
| Compton (bound) | E&gamma; &ge; 0.6 &times; binding | 40% energy transfer, shell promotion or ionization |
| Free electron scatter | E&gamma; &ge; 0.3 | 30% energy transfer |
| Nuclear Compton | E&gamma; &ge; 0.25 | 8% energy transfer to nucleon |

Shell-dependent binding scales with &radic;Z. Inner shells require more energy to ionize.

---

## Spallation & Photonuclear Processes

Max 3 events per frame.

**Massive particle spallation**: any massive particle with speed &gt; 120 px/frame and energy &gt; 0.5
hitting a nucleus (2+ nucleons, 10px radius) ejects nucleons proportional to projectile KE.

**Photonuclear channels** (by energy threshold):

| Process | E&gamma; | Products |
|---|---|---|
| Photodisintegration | &ge; 0.50 | (A&minus;1) + nucleon |
| Pair production | &ge; 0.60 | e&#8314; + e&#8315; near nucleus |
| Photopion (&Delta; resonance) | &ge; 0.80 | N' + &pi; |
| Vector meson dominance | &ge; 0.85 | Hadronic shower via virtual &rho;&#8304; |

---

## Hadronization & Color Confinement

CPU-side confinement prevents free quarks from existing outside QGP conditions. Newly created
quark-antiquark pairs are **immediately bound as mesons** &mdash; free quarks never persist.
Runs each tick in five phases:

| Phase | Description | Rate limit |
|---|---|---|
| **Free quark detection** | Quarks/antiquarks without a partner within 45px are flagged free | &mdash; |
| **Meson formation** | Free quark + free antiquark with complementary color bind into mesons | 24/frame |
| **Baryon condensation** | Below Hagedorn temperature (1.7&times;10&#185;&#178; K), RGB triplets condense into protons or neutrons | 16/frame |
| **Vacuum instability** | Free quarks with E &ge; 0.3 generate a partner from vacuum &mdash; partner spawns close (2px) and is **immediately entangled** as a meson | 2/frame |
| **String breaking** | Bound pairs stretched beyond 55px break the color flux tube (Lund model) &mdash; produces **2 mesons**, each pairing one original quark with one new quark | 2/frame |

A free quark population cap (24) suppresses pair creation when confinement is already behind,
allowing meson formation and baryon condensation to catch up.

**QGP exception**: above 2&times;10&#185;&#178; K, quarks are deconfined and hadronization is
suppressed. The QGP environment preset disables it automatically.

Toggle in **Settings > Strong Nuclear > Hadronization**.

---

## Gluon Interactions

Gluons carry SU(3) color-anticolor pairs (8 octet states).

**GPU** (compute shader):
- Cornell potential with running coupling + linear confinement
- Casimir enhancement: gluon-gluon receives 2.25&times; force (C_A/C_F)
- Trilinear vertex: gluon-gluon attraction when anticolor matches color
- Quark coupling: absorption (&minus;1.2) and emission (&minus;0.8) vertices
- Effective mass: E/c&#178; relativistic inertia (asymptotic freedom at high energy)

**CPU** (hadronization phase 5, max 4 events/frame):

| Interaction | Condition |
|---|---|
| **Color-aware absorption** | Gluon within 12px of quark with matching color vertex &mdash; quark color rotates |
| **Color-conserving merge** (g+g &rarr; g) | Two gluons within 12px with compatible colors (trilinear vertex) |
| **Confinement splitting** (g &rarr; q+q&#773;) | Gluon with E &ge; 0.2, no colored particle within 40px &mdash; products **immediately entangled** as meson |

---

## Virtual Particles & Casimir Effect

Virtual particle-antiparticle pairs spontaneously appear from the quantum vacuum within the
camera's visible region.

| Virtual Pair | Weight | Condition |
|---|---|---|
| &gamma; + &gamma; | 1.0 | Always |
| Gluon + gluon | 2.0 | Always |
| Graviton + graviton | 0.15 | Gravity enabled |
| e&#8315; + e&#8314; | 0&ndash;0.4 | Vacuum energy &gt; 0.3 |
| W&#8314; + W&#8315; | 0.08 &times; weak | Vacuum energy &gt; 0.9 |

**Casimir effect**: when a spawn point falls near real particles, the mode is suppressed. Each
suppressed spawn applies a 1/d&#179; attractive impulse to nearby particles &mdash; the missing
radiation pressure emerges as the Casimir force.

Lifetimes follow Heisenberg uncertainty: massless pairs ~120 frames, e&#8314;e&#8315; ~15 frames,
W&#8314;W&#8315; ~2 frames.

Configurable: vacuum energy, max pairs/tick, Casimir radius and strength, pair scatter, virtual
trail visibility.

---

## Quantum Entanglement

Virtual pair products are automatically entangled with anti-correlated spins. Entangled partners
exhibit velocity coupling (configurable fraction applied mutually each tick) and maintain opposite
spin values. Stochastic decoherence or partner death breaks entanglement. Visualized as dashed
blue lines.

Settings: enable toggle, coupling strength (default 0.15), decoherence rate (default 0.005/tick).

---

## Emergent Thermodynamics

Two feedback systems measure bulk properties from particle kinetics:

- **Temperature** (Berendsen thermostat): EMA of average kinetic energy. Coupling slider blends
  between slider-controlled and fully emergent temperature.
- **B-field**: EMA of charged current magnitude (&Sigma;|q&middot;v|). Feeds back into effective
  Lorentz strength for magnetic interactions.

Both use exponential moving averages (&alpha; = 0.02).

---

## UI & Visualization

### Info Cards

Clicking a particle shows its type, charge, spin, mass, age, momentum, temperature, relativistic
energy (eV, PDG rest masses, E = &gamma;m&#8320;c&#178;), and intrinsic magnetic moment
(anomalous moments for nucleons, Dirac g=2 for leptons). If part of a nucleus, a clickable link
opens the **Element Detail Card** with full composition, stability info, magnetic moment,
and Move/Delete/Duplicate/Export actions. Molecules open a **Molecule Detail Card** with Hill
formula, bond count, and clickable constituent atoms.

### Particle & Element Lists

Bottom bar shows simulation state, timescale, temperature, B-field, FPS, energy, and entropy.
Clickable counters (Events, Particles, Atoms/Molecules) open scrollable windows. The **Menu**
popup organizes commands into Simulation, File, View, Visualization, Measurement, and Tools
sections. Particle and element lists are accessible from **Menu > View**. The event log tracks
up to 10,000 entries across 13 categories with timestamps (configurable limit and disk logging
in Settings).

### Visualization Overlays

Overlays toggled from **Menu > Visualization**:

| Overlay | Description |
|---|---|
| **Trails** | GPU-side particle path fade |
| **Electron Cloud** | Bohr-model shell rings with fill indicators and element labels |
| **Orbit Paths** | Predicted Keplerian ellipses for bound electrons, computed from angular momentum, energy, and Runge-Lenz vector |
| **Magnetic Field** | B-field heatmap from Biot-Savart + nucleon dipoles (32&times;18 grid, OpenMP) |
| **Wave Mode** | de Broglie wave packets (&lambda; = h/p) with Gaussian envelope |
| **Atom Grid** | Hydrogen-diameter grid (2 &times; Bohr radius = 0.106 nm per cell) |
| **Trajectory Tracer** | Last 120 positions per particle as fading polylines |
| **Energy Heatmap** | 32&times;18 KE density grid (blue to red) |
| **Velocity Field** | Arrow grid showing average velocity per cell |
| **Force Vectors** | Coulomb/Yukawa/Gravity breakdown on selected particle |
| **GW Ripples** | Expanding gravitational wave rings from accelerating masses (1/r amplitude, gold&rarr;violet) |
| **Gravity Map** | Gravitational mass density heatmap (supports relativistic mass when E=mc&#178; enabled) |

### Field Visualization

Five quantum field overlays: electromagnetic (red/blue), strong nuclear (cyan/green), weak (purple),
gravity (grey), Higgs (gold).

### Measurement Tools

Four instruments from **Menu > Measurement**: thermometer probe (local KE average in radius),
velocity meter (tracks single particle), distance ruler (nanometer scale), density counter
(particles per area). Up to 8 probes/counters with adjustable radii.

### Tools

- **Force Objects**: EM field (proper Lorentz F=q(v&times;B), curves charged particles without speed loss),
  strong nuclear, weak, gravity well, heat source
- **Particle Accelerator**: fire projectiles at a target (single, triple, stream modes)
- **Mirror**: reflective line segments with configurable elasticity (GPU-side reflection)
- **Nuclear Debug**: tune reaction thresholds and rates in real time
- **Halt Velocities / Remove Massless / Remove Massive**: utility actions

### Visual Quality

- **Half-resolution bloom**: brightness extract (2&times;2 downsample) &rarr; horizontal Gaussian blur &rarr; vertical Gaussian blur &rarr; full-res composite (nearest-neighbor upscale). Runs at half render resolution for performance. Off by default; togglable in Display settings.
- **Rim lighting**: particles have bright edge highlights for depth illusion
- **Sub-pixel anti-aliasing**: adaptive AA band width (`max(1px, 15% radius)`) ensures smooth edges at all zoom levels
- **Zoom-adaptive sizing**: `mix(zoom, sqrt(zoom), 0.5)` &mdash; particles stay visible when zoomed out, don't overlap when zoomed in
- **Camera shake**: fusion, fission, and annihilation events trigger exponentially decaying camera shake (8&ndash;12px intensity)
- **Wobbly windows**: subtle sinusoidal floating animation on UI panels (2px amplitude, togglable)

### Spawn Picker (F3)

Categorized spawning: leptons, quarks, bosons, hypothetical particles, composite atoms
(H through Fe as complete atoms with force-relaxed nuclei and Bohr-model electron shells), and
molecules by formula. Nucleon positions are computed via iterative force relaxation matching
GPU shader constants. Configurable count, energy, and scatter radius.

### Experiment Presets

Quick-apply buttons in the Environment settings panel:

| Preset | Temperature | Description |
|---|---|---|
| Cold Lab | 10 K | Cold vacuum with low dampening |
| Hot Plasma | 10 MK | Extreme thermal energy |
| Nuclear Fuel | 100 MK | Fusion-ready proton gas |
| Antimatter | 10 K | Matter + antimatter mix with virtual pairs |
| Dark Universe | 100 K | Dark matter + dark energy dominated |

### Themes

14 built-in color themes (Dark Navy, Midnight, Slate, Ember, Synthwave, Forest, Arctic, Solar,
High Contrast, Solarized Dark, Dracula, Monokai, Universe Sandbox, Ubuntu Yaru) plus custom
theme import via `.pptheme` files in the `themes/` directory. Settings are organized into five
tabs: Display, Performance, Theme, Accessibility, and Audio & Log. User preferences persist
across sessions in platform-appropriate locations (`~/.local/share/particle_playground/` on
Linux, `%APPDATA%\ParticlePlayground\` on Windows).

### Display Settings

- **Quality presets**: Low, Medium, High, Ultra (auto-sets render scale, bloom, physics quality)
- **VSync**: toggle between FIFO (vsync on) and MAILBOX (vsync off) present modes
- **Multi-monitor**: select which display to use for fullscreen (when multiple monitors detected)
- **GPU selection**: choose which Vulkan-capable GPU to use, with VRAM display

### Accessibility

- **Colorblind modes**: Protanopia, Deuteranopia, Tritanopia (Daltonize-style color correction)
- **High contrast**: brighter text, stronger borders for improved readability
- **Reduced motion**: disables wobbly windows, splash animations, and GW ripple effects
- **Mouse sensitivity**: adjustable camera pan speed (0.1&ndash;3.0&times;)

### Achievements

64 milestones across 6 categories (Nuclear Physics, Element Creation, Particle Zoo,
Thermodynamics, Milestones, Chemistry). Persist via `.ppach` file. Steam achievement
integration ready (optional, builds without Steamworks SDK).

### Sound Effects

Six one-shot sound effects (achievement unlock, spawn, decay, fusion, fission, UI click)
with independent volume and mute controls. Background music loops via miniaudio.

### Gamepad Support

GLFW gamepad polling with standard mapping:

| Input | Action |
|---|---|
| Left stick | Camera pan |
| Triggers | Zoom in / out |
| Start | Pause menu |
| A | Play / pause |
| B | Back / escape |
| Bumpers | Cycle settings tabs |

### Error Dialogs

Fatal errors (Vulkan init failure, no GPU, device lost) show native OS message boxes
(MessageBox on Windows, zenity/kdialog/xmessage on Linux) instead of silent stderr output.

---

## Tutorial & Onboarding

A 10-step interactive tutorial guides new users through the simulation:

1. **Welcome** &mdash; introduction and basic controls
2. **Spawning Particles** &mdash; click to place particles
3. **Camera Controls** &mdash; scroll to zoom, drag to pan
4. **Spawn Menu** &mdash; open the categorized spawn picker
5. **Elements** &mdash; create nuclei from protons and neutrons
6. **Accelerator** &mdash; fire high-energy projectiles
7. **Force Objects** &mdash; place EM fields and gravity wells
8. **Time Control** &mdash; speed up and slow down simulation
9. **Saving** &mdash; save and load simulation state
10. **Explore** &mdash; congratulations, begin free play

### Particle Encyclopedia

Every particle type has an encyclopedia entry accessible via the **?** button on info cards.
Entries include: name, symbol, category, mass (MeV), charge, spin, physics description,
and discovery history.

---

## Scenarios & Gameplay

12 guided scenarios with goals across four categories:

| Category | Scenario | Goal |
|---|---|---|
| **Nuclear** | First Light | Create your first photon |
| **Nuclear** | Hydrogen Factory | Build 5 hydrogen atoms |
| **Nuclear** | Solar Core | Trigger hydrogen fusion |
| **Nuclear** | Chain Reaction | Cause a fission chain reaction |
| **Nuclear** | Antimatter Lab | Observe matter-antimatter annihilation |
| **Chemistry** | Water World | Form H&#8322;O molecules |
| **Chemistry** | Chemistry Set | Create 3 different molecules |
| **Cosmology** | Dark Sector | Observe dark matter clustering |
| **Cosmology** | Particle Zoo | Discover 10 particle types |
| **Cosmology** | Stellar Nucleosynthesis | Build elements up to Iron |
| **Sandbox** | Free Play: Lab | Open sandbox (no goal) |
| **Sandbox** | Free Play: Space | Space environment sandbox |

Accessible from the pause menu. Active scenarios display a goal HUD with progress,
hints, and completion notifications.

---

## Environment Presets

Fourteen presets spanning vacuum to the Big Bang, selectable from the **Environment** dropdown.

<details>
<summary><b>All presets</b></summary>

| # | Environment | Temperature | Description |
|---|---|---|---|
| 0 | Lab Mode | 1 K | Empty vacuum, manual spawning |
| 1 | Hydrogen Plasma | 1.5&times;10&#8311; K | Ionized hydrogen, fusion conditions |
| 2 | Neutron Star | 10&#8313; K | Ultra-dense neutron matter |
| 3 | Solar Core | 1.5&times;10&#8311; K | Hydrogen + gravity |
| 4 | Particle Soup | 5&times;10&#179; K | Mixed light particles |
| 5 | Alpha Emitter | 300 K | Heavy nuclei at room temp |
| 6 | Heavy Nucleus | 100 K | Cold dense nuclear matter |
| 7 | Quark-Gluon Plasma | 2&times;10&#185;&#178; K | Deconfined quarks, hadronization off |
| 8 | Electroweak Era | 10&#185;&#8309; K | W/Z/Higgs above symmetry breaking |
| 9 | Meson Factory | 5&times;10&#185;&#185; K | Quark-antiquark meson formation |
| 10 | Particle Accelerator | 10&#8312; K | High-energy protons + synchrotron |
| 11 | Dark Sector | 10&#179; K | 40% DM, 30% p, 15% e, 10% DE, 5% graviton |
| 12 | SUSY Sector | 10&#179; K | Neutralino/selectron/smuon/squark/gluino mix |
| 13 | Big Bang | 2&times;10&#185;&#8309; K | Singularity-point quark-gluon plasma with Hubble expansion |

</details>

The **Big Bang** preset models the quark epoch (~10&#8315;&#185;&#178; to 10&#8315;&#8310; s).
All particles spawn from a tight central point (&sigma; = 3% of screen) with radial outward
velocities (Hubble-like expansion). Particle mix: 40% quarks (u/d + antiquarks), 15% gluons,
10% photons, 15% leptons (all 3 generations), 8% W/Z bosons, 2% Higgs, 10% BSM (graviton,
dark matter, dark energy). Hadronization is disabled (quarks are deconfined). As the system cools
via dampening, quarks confine into hadrons and structure forms under gravity.

---

## Save / Load

Three binary formats for simulation state:

| Format | Extension | Contents | Access |
|---|---|---|---|
| **Simulation** | `.ppsg` | Full state: config, particles, types, force objects, UI state | Ctrl+S / Ctrl+L, bottom bar, pause menu |
| **Element** | `.ppel` | Single element: Z, N, electrons, relative positions | Element Detail Card > Export; Menu > Tools > Import |
| **Molecule** | `.ppmol` | Molecule: formula, atoms, bond list, relative positions | Molecule Detail Card > Export; Menu > Tools > Import |

Simulation saves capture a 320&times;180 PNG thumbnail. The load dialog displays saves as a
card grid with preview images, names, dates, and file sizes. Element and molecule files store
positions as offsets from centroid for portability.

**Auto-save**: configurable interval (Off / 2 / 5 / 10 minutes) saves automatically during
simulation. Interval is set in **Settings > Performance**.

**Data directory**: saves, settings, and achievements are stored in platform-standard locations:
- **Linux**: `~/.local/share/particle_playground/`
- **Windows**: `%APPDATA%\ParticlePlayground\`
- **Portable**: `saves/` relative to executable (build with `-DPORTABLE_PATHS=ON`)

Existing saves in the old `saves/` directory are automatically migrated on first run.

---

## Controls

| Key | Action |
|---|---|
| `Escape` | Pause menu |
| `F1` | Settings |
| `F2` | Reset simulation |
| `F3` | Spawn Picker |
| `F4` | Select mode |
| `Space` | Pause / unpause |
| `[` / `]` | Halve / double time scale (0.0625&times;&ndash;16&times;) |
| `Alt+Enter` | Toggle borderless fullscreen / windowed |
| `Ctrl+S` / `Ctrl+L` | Save / Load |
| `W A S D` | Pan camera |
| Left drag | Pan camera |
| Scroll wheel | Zoom |
| Left click | Spawn / select / fire / place (context-dependent) |

Gamepad support is automatic when a controller is connected (see [Gamepad Support](#gamepad-support)).

---

## Build

A unified `build.sh` handles both Linux native and Windows cross-compilation.

### Dependencies (Ubuntu / Debian)

```bash
# Linux build
sudo apt install libvulkan-dev vulkan-tools glslang-tools
sudo apt install libglfw3-dev libglm-dev cmake g++

# Windows cross-build (additional)
sudo apt install mingw-w64 g++-mingw-w64-x86-64
```

### Usage

```bash
source ~/vulkan/1.4.341.1/setup-env.sh

./build.sh                  # Linux x64 (default)
./build.sh win64            # Windows x64 (MinGW cross-compile)
./build.sh all              # both targets
./build.sh package win64    # build + distributable zip
./build.sh clean [TARGET]   # wipe build dirs (linux, win64, or all)
```

### Run

```bash
./build/particle_physics              # Linux
./build-win64/particle_physics.exe    # Windows (or from dist-win64/)
```

The Windows portable exe bundles SPIR-V shaders and icons via `PORTABLE_BUILD`.
Users need Vulkan GPU drivers installed. Place `assets/sound.mp3` next to the exe for
background music.

### CMake Options

| Option | Default | Description |
|---|---|---|
| `PORTABLE_BUILD` | OFF | Embed shaders and icons into the executable |
| `PORTABLE_PATHS` | OFF | Use relative `saves/` directory instead of platform-standard paths |
| `STEAMWORKS_SDK_DIR` | &mdash; | Path to Steamworks SDK for optional Steam integration |

### Steam Integration (Optional)

Steam achievements and cloud saves are supported via optional Steamworks SDK linkage.
The build compiles and runs without the SDK &mdash; all Steam calls are no-ops when
`HAS_STEAM` is not defined.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEAMWORKS_SDK_DIR=/path/to/sdk
```

---

## Architecture

```
EmergentEvolution/
├── src/
│   ├── types.h                  # SimConfig, PushConstants, shared constants
│   ├── particles.h/.cpp         # CPU particle arrays and type data
│   ├── vulkan_context.h/.cpp    # Vulkan instance, device, swapchain, buffers
│   ├── compute_pipeline.h/.cpp  # 23-binding descriptor layout, buffer lifecycle, readback, bloom
│   ├── renderer.h/.cpp          # Fullscreen-quad pipeline, ImGui integration
│   ├── stb_image*.h/.cpp        # Image loading/writing (icons, thumbnails)
│   └── miniaudio.h              # Single-header audio (MP3 decode + playback)
├── src/physics/
│   ├── simulation.h             # PhysicsSimulation class definition
│   ├── simulation.cpp           # Core: tick loop, init, reset, input, spatial grid, achievements
│   ├── nuclear.cpp              # Annihilation, fusion, fission, nuclear decay, photoelectric,
│   │                            #   pion decay, spallation
│   ├── orbital.cpp              # Orbital assignment, nucleus repulsion, bonds, shell transitions
│   ├── decay.cpp                # Particle decay, hadronization, bremsstrahlung, weak flavor change
│   ├── quantum.cpp              # Virtual pairs, neutrino scattering/oscillations, entanglement
│   ├── spawning.cpp             # Accelerator fire, atom/particle spawning
│   ├── sim_helpers.h            # Shared inline helpers (Lorentz gamma, energy conversion, etc.)
│   ├── interface.h              # PhysicsInterface class definition
│   ├── interface.cpp            # Core: init, preferences, themes, render_imgui dispatcher
│   ├── ui_panels.cpp            # Top bar, bottom bar, settings panel, spawn menu
│   ├── ui_cards.cpp             # Particle info card, element card, molecule card
│   ├── ui_lists.cpp             # Element/particle lists, bestiaries
│   ├── ui_dialogs.cpp           # Splash screen, pause menu, settings menu, save/load dialog
│   ├── ui_tools.cpp             # Decay log, nuclear debug, accelerator, force objects, measurement
│   ├── ui_overlays.cpp          # Visualization overlays (heatmap, fields, trajectories, etc.)
│   ├── ui_data.h                # Shared UI data tables (elements, particle names/colors, formatting)
│   ├── phys_particles.h/.cpp    # 67 particle types, masses, charges, environments
│   ├── molecules.h              # ~50 molecule templates with geometry
│   ├── achievements.h/.cpp      # 64 achievements, persistence, Steam API names
│   ├── audio.h/.cpp             # Background music + 6 SFX channels via miniaudio
│   ├── save_load.h/.cpp         # Binary .ppsg/.ppel/.ppmol serialization
│   ├── paths.h                  # Platform-appropriate data directory (XDG / AppData)
│   ├── error_dialog.h/.cpp      # Native OS error dialogs (MessageBox / zenity)
│   ├── steam_integration.h/.cpp # Optional Steamworks SDK wrapper (no-op stubs)
│   ├── tutorial.h/.cpp          # 10-step interactive tutorial system
│   ├── scenarios.h/.cpp         # 12 guided scenarios with goals
│   ├── encyclopedia.h           # Particle type descriptions and metadata
│   └── main.cpp                 # Entry point
├── shaders/
│   ├── physics.comp             # GPU: forces, collisions, bonds, fields, bloom, wave rendering
│   ├── fullscreen.vert/.frag    # Render pipeline
├── assets/                      # Icons, music, SFX, Windows resources
├── cmake/                       # FindSteamworks.cmake, embed_resource.cmake
├── CREDITS.md                   # Third-party library credits and licenses
└── CMakeLists.txt
```

<details>
<summary><b>Compute shader bindings</b></summary>

| Binding | Buffer | R/W |
|---|---|---|
| 0-1 | Position A/B (ping-pong) | read / write |
| 2 | Type | read |
| 3 | Force matrix | read |
| 4 | Colour table | read |
| 5-6 | Velocity A/B | read / write |
| 7 | Render texture | image write |
| 8 | Behaviour flags | read |
| 9-12 | Angle / angular velocity A/B | read / write |
| 13-14 | Energy A/B | read / write |
| 15 | Genome | read |
| 16 | Bond partners | read (CPU-managed) |
| 17 | Force objects | read |
| 18 | Mass inverse + ZPE table | read |
| 19 | GPU spatial grid cell starts | read (CPU-built) |
| 20 | GPU spatial grid sorted indices | read (CPU-built) |
| 21 | Bloom texture A (fine) | image read/write |
| 22 | Bloom texture B (wide) | image read/write |

Particle buffers use DEVICE_LOCAL memory on discrete GPUs with staging buffers for CPU
readback. A/B buffers ping-pong each tick.

</details>

---

<div align="center">

MIT License &middot; Night-Traders-Dev 2026

</div>
