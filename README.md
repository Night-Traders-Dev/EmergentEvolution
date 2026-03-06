<div align="center">

# Emergent Evolution

**GPU-accelerated science simulation suite**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

</div>

---

Emergent Evolution is a suite of GPU-accelerated science simulations sharing a common Vulkan
rendering framework. A unified launcher lets you pick between three sandbox applications:

**Particle Physics** &mdash;
Real-time particle physics sandbox simulating 282 particle types: the full Standard Model,
Beyond Standard Model, 34 hypothetical particles, 7 quasiparticles, and 188 PDG meson states.
All four fundamental forces run simultaneously on Vulkan compute shaders with optional general
relativity extensions. CPU-side processes handle nuclear reactions, decay, orbital mechanics,
covalent bonding, chirality detection, hadronization, carrier exchange, and more. Up to
**100,000 particles** in real time with O(n) spatial-grid neighbor queries and OpenMP parallelization.

**Cosmic Sandbox** &mdash;
3D celestial mechanics simulator with GPU-raytraced sphere rendering. 23 celestial body types
including 11 star spectral classes (O through Y plus Wolf-Rayet), 4 black hole subtypes
(stellar, intermediate, supermassive, primordial), and procedurally generated planets with
terrain, oceans, clouds, vegetation, atmospheric rim lighting, and city lights on populated
worlds. Bodies interact via N-body gravity (CPU or GPU Barnes-Hut compute shader) with
optional general relativity corrections (perihelion precession, gravitational time dilation,
frame dragging). 6 numerical integrators from Euler through fourth-order PEFRL. Nebulae
spawn as gravitationally collapsing particle clouds of 40&ndash;300 dust bodies. Procedural GPU
textures are driven by physical properties &mdash; changing a planet&rsquo;s temperature melts ice
into oceans, and gas giants display Jupiter-like banding with great spots. Stellar evolution
through 9 stages with supernova events, Roche limit tidal disruption, Hawking radiation,
tidal locking, and collision physics (merging, fragmentation, SPH). A logarithmic timestep
system spans 30 orders of magnitude from nanoseconds/s to trillions of years/s. Orbit
camera with WASD panning, mouse drag, scroll zoom, and auto-hiding bottom taskbar.

**Biochemical Simulator** &mdash;
3D cellular biology sandbox with GPU SDF-raytraced rendering and 8 entity types (cells,
bacteria, viruses, nutrients, toxins, antibodies, red blood cells, white blood cells), each
with morphological variants and unique SDF shapes showing organelle interiors. Features
metabolism, cell division with heritable gene mutations (5 behavioral traits), viral
infection, immune response, AI-driven entity movement (seek/flee/spacing behaviors), and
fluid dynamics. 4 environment presets (Human Lung, Pond Water, Petri Dish, Cat Brain) with
distinct temperature, pH, oxygen, and immune conditions. WASD camera panning and auto-hiding
bottom taskbar with panel management.

---

## Table of Contents

**General**

- [Controls](docs/controls.md)
- [Build](docs/build.md)
  - [CMake Options](docs/build.md#cmake-options)
  - [Steam Integration](docs/build.md#steam-integration-optional)
- [Architecture](docs/architecture.md)

**Cosmic Sandbox**

- [Cosmic Sandbox](docs/cosmos.md)
  - [Celestial Body Types](docs/cosmos.md#celestial-body-types)
  - [Stellar Evolution](docs/cosmos.md#stellar-evolution)
  - [Procedural Planet Generation](docs/cosmos.md#procedural-planet-generation)
  - [Physics Systems](docs/cosmos.md#physics-systems)
  - [Rendering](docs/cosmos.md#rendering)

**Biochemical Simulator**

- [Biochemical Simulator](docs/biochem.md)
  - [Entity Types](docs/biochem.md#entity-types)
  - [Simulation Systems](docs/biochem.md#simulation-systems)
  - [Configuration](docs/biochem.md#configuration)

**Particle Physics**

- [Physics Engine](docs/physics-engine.md)
- [Particle Types](docs/particle-types.md)
- [Forces](docs/forces.md)
  - [General Relativity Extensions](docs/forces.md#general-relativity-extensions)
- [Orbital Mechanics](docs/orbital-mechanics.md)
- [Covalent Bonds & Molecules](docs/covalent-bonds-molecules.md)
- [Nuclear Reactions](docs/nuclear-reactions.md)
- [Radioactive Decay & Isotopes](docs/radioactive-decay-isotopes.md)
- [Photon-Matter Interactions](docs/photon-matter-interactions.md)
- [Spallation & Photonuclear Processes](docs/spallation-photonuclear.md)
- [Hadronization & Color Confinement](docs/hadronization-color-confinement.md)
- [Gluon Interactions](docs/gluon-interactions.md)
- [Carrier Exchange](docs/carrier-exchange.md)
- [Quasiparticles](docs/quasiparticles.md)
- [Virtual Particles & Casimir Effect](docs/virtual-particles-casimir.md)
- [Quantum Entanglement](docs/quantum-entanglement.md)
- [CP Violation](docs/cp-violation.md)
- [Emergent Thermodynamics](docs/emergent-thermodynamics.md)
- [UI & Visualization](docs/ui-visualization.md)
- [Tutorial & Onboarding](docs/tutorial-onboarding.md)
- [Scenarios & Gameplay](docs/scenarios-gameplay.md)
- [Environment Presets](docs/environment-presets.md)
- [Save / Load](docs/save-load.md)
  - [Molecule Tools](docs/save-load.md#molecule-tools)
- [Online Repository](docs/online-repository.md)

---

<div align="center">

MIT License &middot; Night-Traders-Dev 2026

</div>
