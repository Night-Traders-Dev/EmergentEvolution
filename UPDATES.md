# Changelog

All notable changes to Emergent Evolution are documented in this file.
Per-simulation changelogs: [Particle Physics](docs/UPDATES-physics.md) | [Cosmic Sandbox](docs/UPDATES-cosmos.md) | [Biochemical Simulator](docs/UPDATES-biochem.md)

Versions follow [Semantic Versioning](https://semver.org/). Dates are YYYY-MM-DD.

---

## [1.0.0] - 2026-03-08

**Emergent Evolution** reaches v1.0.0 as a unified suite with launcher and three simulations.

### Project-Wide
- Unified launcher for selecting between Particle Physics, Cosmic Sandbox, and Biochemical Simulator
- Per-simulation versioning (Particle Physics v1.2.0, Cosmic Sandbox v1.1.0, Biochemical Simulator v0.3.0)
- Shared Vulkan rendering framework (`pp_common` library)
- Cross-platform support (Linux, Windows)
- Steam integration (optional)
- Comprehensive documentation suite (README, architecture, per-feature docs)

---

## Particle Physics v1.2.0 - 2026-03-08

Physics realism audit and version update.

### Changed
- Running QCD coupling corrected for asymptotic freedom (weakens at short distance)
- W boson decay now uses CKM matrix branching ratios from `phys_particles.h`
- Annihilation photon energies use proper relativistic Doppler with energy renormalization
- Bremsstrahlung adds momentum recoil to charged particle
- Muonic/tauonic atom Bohr radii scaled by lepton mass ratio in shader
- Splash screen and About dialog updated from "67 Particle Types" to "282 Particle Types"

### Fixed
- Division-by-zero guard in `two_body_decay_momentum` for near-zero parent mass
- NaN guard in annihilation aberration formula (zero-length velocity vectors)
- Reduced mass overflow fix using harmonic mean form
- NucleusInfo.center initialized to prevent undefined behavior
- Auxiliary particle vectors (excitation_timer, cascade_tag, entangled_partner) auto-resized on spawn
- All auxiliary vectors properly sized in `reset()`
- GPU grid buffers pre-allocated to avoid per-frame reallocation
- Save/load forward compatibility: files from newer versions with more particle types load correctly
- Null guard for `glfwGetVideoMode` on headless systems
- UI: Temperature slider shows `10^X K` format, scatter slider uses logarithmic scale, particle count has reset button and tooltip

---

## Particle Physics v1.1.0 - 2026-03-02

Meson system, scenarios, chirality detection, cutscenes.

### Added
- 188 PDG meson states (types 74-261): light unflavored, strange, charmed, bottom, charmonium, bottomonium, exotic candidates
- Hadronization system: quark-antiquark pairs form specific meson types via `quark_pair_to_meson()`
- Meson decay tables with PDG-accurate branching ratios
- Meson shader fast-path (no Cornell/Pauli/nuclear-Yukawa, only Coulomb+residual-strong+gravity)
- Chirality detection: molecule chiral center identification (C/Si/N/P/S bonded to 3+ different Z)
- Helicity tracking for fermions
- 5 chirality achievements
- Cutscene system with scripted camera sequences
- Voiceover support
- 20 guided scenarios with objectives and achievements
- Particle repository with online `.ppel`/`.ppmol` format support
- `ppel_gen` tool for generating element files
- Neutrino oscillations and scattering
- Weak flavor-changing interactions
- Meson oscillation (B/K mixing)
- Carrier exchange system for 7 force-carrier types
- Quasiparticle spawning (environment/temperature/B-field driven)
- Updated metrics and achievement tracking (239 total achievements)

### Changed
- MAX_PARTICLE_TYPES expanded from 74 to 282
- Save format version bumped to v6 for meson support
- Molecule format version bumped to v3 (chirality fields)
- Top and bottom UI bars refactored
- UX polish: settings persistence, accelerator tool

---

## Particle Physics v1.0.0 - 2026-02-28

First stable release with full Standard Model, Steam integration, custom rendering.

### Added
- Steam SDK integration with achievements
- Custom particle texture rendering engine
- Win64 cross-compilation support
- `.gitignore` and build script refactoring
- Performance optimizations (OpenMP parallelization, spatial grid tuning)
- Dark Energy repulsive force updates
- Wave mode rendering

### Changed
- Major renderer overhaul for particle textures
- Documentation refactored and expanded
- README reformatted

### Fixed
- Wave mode rendering artifacts
- Load/Save dialog crash on Linux
- Various stability fixes

---

## Particle Physics v0.8.0 - 2026-02-27

Major physics overhaul with hypothetical particles and thermodynamics.

### Added
- 34 hypothetical/BSM particles (types 33-66): DM candidates, SUSY sparticles, exotic force carriers
- Thermodynamic feedback system: emergent temperature (Berendsen thermostat), emergent B-field
- Energy conservation tracking (First Law) and entropy monitoring (Second Law)
- BEHAVIOR_SUSY and BEHAVIOR_EXOTIC flags
- SUSY Sector environment preset
- Dark Sector environment preset
- 3 new shader forces: SIMP self-gravity, Tachyon superluminal, Monopole radial B-field

### Changed
- Complete simulation overhaul (2 passes): physics accuracy, GPU shader pipeline
- Particle count expanded to support hypothetical types

---

## Particle Physics v0.5.0 - 2026-02-26

Core gameplay systems: save/load, achievements, covalent bonds, quarks.

### Added
- Save/Load system (binary `.ppsg` format with magic number and versioning)
- Import/Export for sharing simulation states
- Achievement system with unlockable milestones
- Covalent bonding between atoms (GPU spring forces, CPU bond management)
- Full quark system (6 flavors + antiquarks, color charge)
- Settings menu with persistent preferences
- Force/Energy visualization tools
- Electron orbital mechanics

### Changed
- UI improvements for tools and interaction
- Debug tooling added

### Fixed
- Electron orbital stability

---

## Particle Physics v0.1.0 - 2026-02-24

Initial particle physics simulation.

### Added
- Vulkan compute shader pipeline for particle forces
- Standard Model particles: protons, neutrons, electrons, photons, positrons, antiprotons, neutrinos, muons, taus
- Four fundamental forces on GPU: electromagnetic (Coulomb), strong (Yukawa/Cornell), weak, gravity
- Nuclear reactions: fusion, fission, annihilation, decay
- Virtual pair production
- Orbital mechanics (shell filling, Bohr radius)
- WASD camera movement
- Lab mode with spawn tools
- Basic UI with particle info cards

---

## Cosmic Sandbox v1.1.0 - 2026-03-08

Rendering realism audit and spawn menu redesign.

### Changed
- ACES filmic tone mapping replaces raw gamma (prevents highlight clipping on stars/nebulae)
- Gas giant limb darkening: view-dependent `pow(mu, 0.4)` darkens edges realistically
- Planetary rings: Cassini Division (42-48%), Encke Gap (88-90%), radial opacity profile (thin C ring, thick B ring, moderate A ring)
- Ring lighting uses actual primary star position instead of hardcoded direction
- Ring forward/back scattering via Henyey-Greenstein phase function (g=0.3)
- Nebula self-shadowing: 4-step shadow rays toward light source give dark cloud interiors and bright lit edges
- Spawn menu redesigned: 1080x490 fixed panel reduced to compact 85%-width x 210px bottom strip
- Spawn cards shrunk from 148x68 to 120x52, properties panel from 320px to 260px
- Tab sidebar narrowed from 52px to 40px

---

## Cosmic Sandbox v1.0.0 - 2026-03-06

Full-featured celestial mechanics simulator.

### Added (Mar 3-6)
- GPU-raytraced sphere rendering with procedural textures
- 23 celestial body types: 11 star spectral classes, 4 black hole subtypes, planets, moons, asteroids, comets
- N-body gravity (CPU and GPU Barnes-Hut compute shader)
- General relativity corrections: perihelion precession, gravitational time dilation, frame dragging
- 6 numerical integrators (Euler through PEFRL) with adaptive substepping (up to 512 substeps)
- Stellar evolution through 9 stages with supernova events
- Procedural planet generation: terrain, oceans, clouds, vegetation, atmospheric rim lighting, city lights
- Nebula spawning as gravitationally collapsing particle clouds with protostar sink formation
- Vulkan compute shader for gravity (full rewrite from CPU)
- Spacetime grid visualization
- Magnetic field rendering (magnetospheres, pulsars, particle jets)
- Roche limit tidal disruption and Hawking radiation
- Tidal locking, stellar wind pressure, collision physics (merging, fragmentation, SPH)
- 7 planetary ring styles
- Logarithmic timestep spanning 30 orders of magnitude
- 20 procedural background presets
- Spawn Studio with 5 catalog tabs, live 3D ghost preview, planet presets, moon/ring spawning
- Known astronomical objects and star systems catalog
- 25 preset scenarios (Solar System, TRAPPIST-1, nebula collapse, figure-eight choreography, etc.)
- Raymarching for volumetric nebulae
- Body labels, search filter, velocity arrows
- Orbit camera with WASD panning, scroll zoom, body tracking
- Multithreading and parallel-safe gravity accumulation
- Fast star-lighting toggle
- Collision engine with fragmentation
- Dust rings and gas clouds
- Verlet integration option
- Solar flare effects
- Save/load (`.cssim` format v16)
- Screenshot capture (F12)
- Loading screen with progress bar

### Fixed

- Black hole rendering and physics
- Moon and planet orbit stability
- Solar fuel consumption bug
- Sim time reset
- Label positioning
- Camera zoom behavior
- Rotation calculations
- Nebula rendering quality

---

## Biochemical Simulator v0.3.0 - 2026-03-08

Immune system expansion and advanced biology.

### Added

- ATP-based metabolism: aerobic (36 ATP) vs anaerobic (2 ATP), O2-dependent
- Quorum sensing with autoinducer signals and threshold-gated defense
- Antibiotic resistance: heritable gene + adaptive epigenetic resistance from exposure
- Immune subtypes: T-cell (prioritizes infected hosts), B-cell (produces antibodies)
- Complement cascade: opsonization (classical/lectin pathways), MAC damage at >70%
- 2 new genome traits: resistance, quorum_threshold (18 total)

---

## Biochemical Simulator v0.2.0 - 2026-03-07

Entity systems, environment presets, and UI polish.

### Added

- Multi-stage viral infection with replication gates and lysis bursts (30-120 virions)
- Bacterial antibiotic film warfare
- Phagocyte corpse cleanup
- AI-driven entity movement (seek/flee/spacing with passive Brownian diffusion)
- 8 environment presets (Human Lung, Pond Water, Petri Dish, Cat Brain, Gut Microbiome, Blood Stream, Soil Rhizosphere, Wound Site)
- 16 SDF structure shapes with CPU collision detection
- Color-coded event log
- Collapsible settings sections with parameter tooltips

---

## Biochemical Simulator v0.1.0 - 2026-03-06

Initial biochemical simulator.

### Added

- GPU SDF-raytraced rendering engine
- 9 entity types: Cell, Bacterium, Virus, Nutrient, Toxin, Antibody, RBC, WBC, Phagocyte
- 19 morphological variants (8 cell, 6 bacteria, 5 virus) with unique SDF shapes
- 16-gene heritable genome system
- Cell division with telomere tracking and senescence
- Immune system: WBC/antibody spawning
- WASD camera panning

---

## Pre-release (Particle Life) - 2026-02-22 to 2026-02-24

Original particle life simulator (precursor to Particle Physics).

### Added
- Initial particle life framework with configurable particle types
- Soft-body physics
- Metabolic engine (multiple iterations)
- Genome engine with heritable traits
- Bond manager for particle connections
- Subatomic engine foundation
- R-Process / Supernova element generation
- Four fundamental force prototype
- Organic molecule support
- Lab mode with spawn tools
- WASD movement and camera controls
- Emergence rules system

### Fixed
- Atomic bond stability
- Metabolic engine reworking (3 iterations)

---

## Launcher v1.0.0 - 2026-03-07

Unified application launcher.

### Added
- Expansion picker UI for selecting simulations
- Version badge display
- Layout and visual polish

---

[1.0.0]: https://github.com/Night-Traders-Dev/EmergentEvolution/releases/tag/v1.0.0
