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
3D celestial mechanics simulator with GPU-raytraced sphere rendering. Stars, planets, black
holes, asteroids, and comets interact via Newtonian gravity with real-time orbital trails.
Bodies are rendered as lit 3D spheres in a fullscreen fragment shader with two lighting modes:
star lighting (point lights with shadow rays, diffuse/specular shading) and uniform lighting.
Orbit camera with mouse drag, scroll zoom, and body selection.

**Biochemical Simulator** &mdash;
2D cellular biology sandbox with 8 entity types: cells, bacteria, viruses, nutrients, toxins,
antibodies, red blood cells, and white blood cells. Features metabolism, cell division with
mutations, viral infection, immune response, and fluid dynamics. Entities interact through
proximity-based rules with configurable parameters.

---

## Table of Contents

**General**

- [Controls](docs/controls.md)
- [Build](docs/build.md)
  - [CMake Options](docs/build.md#cmake-options)
  - [Steam Integration](docs/build.md#steam-integration-optional)
- [Architecture](docs/architecture.md)

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
