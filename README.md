<div align="center">

# Emergent Evolution

**GPU-accelerated science simulation suite**

[![Version](https://img.shields.io/badge/version-1.0.0-orange.svg)]()
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

| Simulation | Version |
| --- | --- |
| Particle Physics | v1.2.0 |
| Cosmic Sandbox | v1.1.0 |
| Biochemical Simulator | v0.3.0 |

</div>

---

A suite of GPU-accelerated science simulations built on a shared Vulkan framework.
A unified launcher lets you pick between three sandbox applications:

**Particle Physics** &mdash;
282 particle types (full Standard Model, BSM, 188 PDG mesons) with all four fundamental
forces on Vulkan compute shaders. Nuclear reactions, decay, orbital mechanics, covalent
bonding, chirality, and hadronization. Up to 100k particles in real time.

**Cosmic Sandbox** &mdash;
3D celestial mechanics with GPU-raytraced rendering. 23 body types, procedural planet
generation, N-body gravity with optional GR corrections, stellar evolution, nebula collapse,
and 25 preset scenarios. Logarithmic timestep spans nanoseconds to trillions of years.

**Biochemical Simulator** &mdash;
Cellular biology sandbox with GPU SDF-raytraced rendering. 9 entity types with 19 morphological
variants, heritable 18-gene system, ATP metabolism, viral infection, immune response with
T/B cell differentiation, quorum sensing, and complement cascade. 8 environment presets.

---

## Table of Contents

**General**

- [Changelog](UPDATES.md)
- [Controls](docs/controls.md)
- [Build](docs/build.md)
  - [CMake Options](docs/build.md#cmake-options)
  - [Steam Integration](docs/build.md#steam-integration-optional)
- [Architecture](docs/architecture.md)

**Cosmic Sandbox**

- [Changelog](docs/UPDATES-cosmos.md)
- [Cosmic Sandbox](docs/cosmos.md)
  - [Celestial Body Types](docs/cosmos.md#celestial-body-types)
  - [Stellar Evolution](docs/cosmos.md#stellar-evolution)
  - [Procedural Planet Generation](docs/cosmos.md#procedural-planet-generation)
  - [Physics Systems](docs/cosmos.md#physics-systems)
  - [Spawn Studio](docs/cosmos.md#spawn-studio)
  - [Preset Scenarios](docs/cosmos.md#preset-scenarios)
  - [Numerical Stability](docs/cosmos.md#numerical-stability)
  - [Rendering](docs/cosmos.md#rendering)

**Biochemical Simulator**

- [Changelog](docs/UPDATES-biochem.md)
- [Biochemical Simulator](docs/biochem.md)
  - [Entity Types](docs/biochem.md#entity-types)
  - [Gene System](docs/biochem.md#gene-system)
  - [Environment Presets](docs/biochem.md#environment-presets)
  - [Simulation Systems](docs/biochem.md#simulation-systems)
  - [Configuration](docs/biochem.md#configuration)
  - [Rendering](docs/biochem.md#rendering)

**Particle Physics**

- [Changelog](docs/UPDATES-physics.md)
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
