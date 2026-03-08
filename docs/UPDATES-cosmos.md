# Cosmic Sandbox Changelog

All notable changes to the Cosmic Sandbox simulation.

---

## v1.1.0 - 2026-03-08

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

## v1.0.0 - 2026-03-06

Full-featured celestial mechanics simulator.

### Added

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

## v0.5.0 - 2026-03-05

Major overhaul with GPU gravity, new physics algorithms, and content.

### Added

- GPU gravity compute shader (Barnes-Hut on Vulkan)
- Verlet integration option
- Dust rings and gas clouds
- Solar flare rendering
- Known celestial objects and star systems catalog
- Nebula raymarching with volumetric rendering
- New simulation algorithms (orbital mechanics, engine updates)

### Changed

- Cosmos overhaul: physics engine, integrators, rendering pipeline
- Code modularized (cosmos_app split into multiple files)
- Star rendering quality improved
- Nebula visual tweaks and quality pass

### Fixed

- Rotation calculations
- Body labeling

---

## v0.3.0 - 2026-03-04

Vulkan shader rewrite with new physics features.

### Added

- Vulkan fragment shader rewrite (full raytraced rendering pipeline)
- Spacetime grid visualization
- Magnetic field rendering (magnetospheres, pulsars, particle jets)
- Procedural planet noise improvements
- Fragmentation physics for collisions

### Changed

- Rendering pipeline rewritten from CPU to GPU raytracing
- Updates to rendering quality and Vulkan code

### Fixed

- Black hole rendering and physics
- Moon and planet orbital stability
- Solar fuel consumption bug

---

## v0.1.0 - 2026-03-03

Initial cosmos simulation.

### Added

- Basic celestial body types (stars, planets, moons, asteroids, comets, nebulae, black holes)
- CPU N-body gravity simulation
- Multithreading for gravity accumulation (parallel-safe design)
- Raytracing hot path optimizations for large body counts
- Fast star-lighting toggle
- Camera zoom and orbit controls
- Initial preset scenarios
