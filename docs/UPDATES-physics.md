# Particle Physics Changelog

All notable changes to the Particle Physics simulation.

---

## v1.2.0 - 2026-03-08

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

## v1.1.0 - 2026-03-02

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

## v1.0.0 - 2026-02-28

First stable release with full Standard Model, Steam integration, custom rendering.

### Added

- Steam SDK integration with achievements
- Custom particle texture rendering engine
- Win64 cross-compilation support
- Performance optimizations (OpenMP parallelization, spatial grid tuning)
- Dark Energy repulsive force updates
- Wave mode rendering

### Changed

- Major renderer overhaul for particle textures
- libcurl replaced with winhttp for win64 builds
- Build script refactored, `.gitignore` added
- Documentation refactored and expanded

### Fixed

- Wave mode rendering artifacts
- Load/Save dialog crash on Linux
- Various stability fixes

---

## v0.8.0 - 2026-02-27

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

## v0.5.0 - 2026-02-26

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

## v0.1.0 - 2026-02-24

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
