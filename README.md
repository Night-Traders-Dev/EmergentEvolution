<div align="center">

# Particle Playground

**GPU-accelerated particle physics sandbox**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

</div>

---

Particle Playground is a real-time physics sandbox that simulates 282 particle types: the full
Standard Model, Beyond Standard Model, 34 hypothetical particles, 7 quasiparticles, and 188 PDG
meson states (light unflavored, strange, charmed, bottom, charmonium, bottomonium, and exotic
candidates). All four fundamental forces run simultaneously on Vulkan compute shaders with optional
general relativity extensions (mass-energy equivalence, frame dragging, gravitational waves with
physical tidal forces). CPU-side processes handle nuclear reactions, decay, meson decay with PDG
branching ratios, orbital mechanics, covalent bonding, chirality detection, hadronization to
specific meson types, carrier exchange, and quasiparticle dynamics.

Up to **100,000 particles** in real time. GPU handles O(n&#178;) pairwise forces; CPU physics
uses a **spatial acceleration grid** with **OpenMP** parallelization for O(n) neighbor queries.

---

## Table of Contents

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
- [Emergent Thermodynamics](docs/emergent-thermodynamics.md)
- [UI & Visualization](docs/ui-visualization.md)
  - [Top Stats Bar](docs/ui-visualization.md#top-stats-bar)
  - [Bottom Taskbar](docs/ui-visualization.md#bottom-taskbar)
  - [Custom Particle Textures](docs/ui-visualization.md#custom-particle-textures)
  - [Display Settings](docs/ui-visualization.md#display-settings)
  - [Accessibility](docs/ui-visualization.md#accessibility)
  - [Achievements](docs/ui-visualization.md#achievements)
  - [Lifetime Statistics](docs/ui-visualization.md#lifetime-statistics)
  - [Sound Effects](docs/ui-visualization.md#sound-effects)
  - [Gamepad Support](docs/ui-visualization.md#gamepad-support)
  - [Error Dialogs](docs/ui-visualization.md#error-dialogs)
- [Tutorial & Onboarding](docs/tutorial-onboarding.md)
- [Scenarios & Gameplay](docs/scenarios-gameplay.md)
- [Environment Presets](docs/environment-presets.md)
- [Save / Load](docs/save-load.md)
  - [Molecule Tools](docs/save-load.md#molecule-tools)
- [Online Repository](docs/online-repository.md)
- [Controls](docs/controls.md)
- [Build](docs/build.md)
  - [CMake Options](docs/build.md#cmake-options)
  - [Steam Integration](docs/build.md#steam-integration-optional)
- [Architecture](docs/architecture.md)

---

<div align="center">

MIT License &middot; Night-Traders-Dev 2026

</div>
