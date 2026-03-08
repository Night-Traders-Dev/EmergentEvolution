# Cosmic Sandbox

The Cosmic Sandbox is a 3D celestial mechanics simulator with GPU-raytraced sphere rendering, N-body gravity, stellar evolution, and procedurally generated planets. Bodies interact via Newtonian gravity with optional general relativity corrections. A Spawn Studio provides full control over body creation with live 3D ghost previews.

## Celestial Body Types

23 body types organized into five categories:

### Core Types

| Type | ID | Description |
|---|---|---|
| Star | 0 | Generic star (auto-classifies by mass/temperature) |
| Planet | 1 | Rocky, oceanic, gaseous, or icy world |
| Moon | 2 | Natural satellite orbiting a planet |
| Asteroid | 3 | Small rocky body (C, S, M, or Icy taxonomic class) |
| Comet | 4 | Icy body with coma and tail near heat sources |
| Black Hole | 5 | Generic black hole |
| Nebula | 6 | Gas/dust cloud (spawns as particle cloud of 40&ndash;300 dust bodies) |
| Dust | 22 | Ring/disk dust grain aggregate |

### Star Spectral Classes (Types 7&ndash;17)

| Class | ID | Temperature | Description |
|---|---|---|---|
| O | 7 | >30,000 K | Blue supergiant |
| B | 8 | 10,000&ndash;30,000 K | Blue-white |
| A | 9 | 7,500&ndash;10,000 K | White |
| F | 10 | 6,000&ndash;7,500 K | Yellow-white |
| G | 11 | 5,200&ndash;6,000 K | Yellow (Sun-like) |
| K | 12 | 3,700&ndash;5,200 K | Orange |
| M | 13 | 2,400&ndash;3,700 K | Red dwarf |
| L | 14 | 1,300&ndash;2,400 K | Brown dwarf |
| T | 15 | 500&ndash;1,300 K | Cool brown dwarf |
| Y | 16 | <500 K | Ultra-cool brown dwarf |
| WR | 17 | Variable | Wolf-Rayet |

### Black Hole Subtypes (Types 18&ndash;21)

| Subtype | ID | Mass Range |
|---|---|---|
| Stellar | 18 | 3&ndash;20 solar masses |
| Intermediate | 19 | 100&ndash;100,000 solar masses |
| Supermassive | 20 | 10&#8310;&ndash;10&#185;&#8304; solar masses |
| Primordial | 21 | Sub-stellar mass |

## Stellar Evolution

Stars evolve through 9 stages based on fuel consumption, mass, and temperature:

| Stage | ID | Description |
|---|---|---|
| Main Sequence | 0 | Hydrogen fusion (stable) |
| Subgiant | 1 | Core hydrogen exhausted |
| Red Giant | 2 | Shell hydrogen fusion, envelope expansion |
| Horizontal Branch | 3 | Helium core fusion |
| Asymptotic Giant Branch | 4 | Double shell burning |
| Supergiant | 5 | Massive star late stage |
| Hypergiant | 6 | Extreme luminosity, near Eddington limit |
| White Dwarf | 7 | Degenerate remnant (low/intermediate mass) |
| Neutron Star | 8 | Degenerate remnant (high mass) |

Stars undergo supernova events at end-of-life, producing fragments, neutron stars, or black holes depending on mass. Collision-triggered supernovae are also supported (thermonuclear or core-collapse).

### Magnetic Signatures

Stars and planets with iron cores or gas dynamos generate magnetic fields. Computed properties include:

- **Magnetic field strength** &mdash; driven by spin rate, density, convective zone, and stellar stage
- **Magnetosphere size** &mdash; scaled from body radius and field strength
- **Particle trapping** &mdash; Van Allen belt analog
- **Particle jets** &mdash; polar outflows from neutron stars and white dwarfs with strong fields
- **Pulsar detection** &mdash; rapidly spinning neutron stars with jets

## Procedural Planet Generation

Planets and moons are procedurally generated from a deterministic seed, mass, and temperature. Temperature band hysteresis prevents per-frame property flashing. Properties include:

### Surface Composition
- **Rocky** &mdash; barren terrain (Mercury/Mars-like)
- **Liquid** &mdash; water world with deep global oceans
- **Frozen** &mdash; ice-covered surface
- **Gas** &mdash; no solid surface (gas/ice giants)
- **Mixed** &mdash; continents with oceans (Earth-like)

### Planet Classes
- **Dwarf** &mdash; small, low-mass body (<0.18 Earth masses)
- **Terrestrial** &mdash; rocky world with possible atmosphere
- **Ocean** &mdash; liquid-water-dominated surface
- **Super-Earth** &mdash; massive terrestrial (>2.4 Earth masses)
- **Ice Giant** &mdash; intermediate gas/ice body
- **Gas Giant** &mdash; massive hydrogen/helium body (>45 Earth masses)

### Planet Look Presets

The Spawn Studio supports forced surface overrides:

| Preset | Effect |
|---|---|
| Auto | Procedurally determined from mass/temperature |
| Rocky | Barren terrain, minimal clouds, no oceans |
| Water | Ocean coverage &ge;82%, heavy clouds |
| Ice | Ice sheets &ge;70%, no vegetation |
| Earth-like | N&#8322;/O&#8322; atmosphere, 40&ndash;75% ocean, vegetation, weather |
| Gas Giant | H&#8322;/He atmosphere &ge;15 atm, storm weather, banding |

### Atmosphere System
13 atmospheric gas species modeled: N&#8322;, O&#8322;, CO&#8322;, H&#8322;, He, CH&#8324;, NH&#8323;, H&#8322;O vapor, Ar, Ne, SO&#8322;, CO, H&#8322;S. Atmosphere properties include:
- Pressure (atmospheres)
- Cloud coverage and weather intensity
- Up to 8 atmospheric layers with individual altitude, thickness, pressure, temperature offset, opacity, and IR emissivity
- Greenhouse effect computed from gas composition, pressure, and cloud coverage
- Venus-like thick CO&#8322; atmospheres, Titan-like methane hazes, Earth-like N&#8322;/O&#8322; compositions
- Atmosphere retention tracking &mdash; cumulative erosion strips clouds, weather, and surface features

### Oceans
5 ocean types: None, Water, Methane, Ammonia, Lava. Temperature-dependent &mdash; changing a planet's temperature can melt ice into oceans or freeze them over.

### Terrain Features
Mountains, valleys, continents, islands, rivers, ice sheets, and vegetation. Coverage and heights are seed-deterministic but temperature-responsive. Vegetation requires water oceans, atmospheric O&#8322;, and temperate conditions (240&ndash;340 K).

### Weather
4 weather types: Storms, Rain, Snow, Dust. Driven by atmospheric pressure, ocean coverage, and temperature.

### Material Phases

7 material phases with temperature-driven transitions:

| Phase | Description |
|---|---|
| Solid | Default rocky/metallic state |
| Liquid | Melted surface |
| Ice | Sub-freezing with ice expansion |
| Gas | Sublimated/evaporated atmosphere |
| Molten | Magma surface |
| Plasma | Ionized stellar interior |
| Collapsing | Gravitational collapse (nebula cores) |

## Physics Systems

### N-Body Gravity
Pairwise gravitational interaction with configurable gravitational constant scaling and softening. Supports:
- **Barnes-Hut** tree approximation for large body counts (configurable theta = 0.72, min bodies = 128)
- **GPU compute** Barnes-Hut acceleration via Vulkan compute shader (`cosmos_bh.comp`)
- **Parallel gravity** via std::thread chunked parallelization
- **Configurable softening** to prevent singularities at close approach

### Integrators
6 integration methods, runtime-selectable:

| Integrator | Description |
|---|---|
| Velocity Verlet | Symplectic, second-order (default) |
| Euler Explicit | First-order, fast but less stable |
| Euler Semi-Implicit | First-order symplectic |
| RK2 | Midpoint method, second-order |
| Forest-Ruth | Fourth-order symplectic |
| PEFRL | Position-Extended Forest-Ruth-Like, fourth-order |

Adaptive substepping available with configurable tolerance (position error), safety factor, and max substep count (up to 512). Orbital-period-aware substep refinement ensures at least 32 integration steps per shortest orbital period, using precomputed orbital elements with a Kepler estimate fallback for bodies without computed periods.

### General Relativity Corrections
Optional GR effects:
- **Perihelion precession** &mdash; orbit precession scaling
- **Gravitational time dilation** &mdash; time rate near massive bodies
- **Frame dragging** &mdash; Lense-Thirring effect from spinning bodies
- Configurable speed of light in simulation units (default 300)

### Collision Physics
- **Merging** &mdash; low-speed collisions combine bodies (mass, momentum, temperature conserved)
- **Fragmentation** &mdash; high-speed collisions shatter bodies into configurable fragment count (1&ndash;12), with generation tracking (max 2 re-fragmentations)
- **SPH-like** soft-body pressure/viscosity for impacts (configurable pressure, viscosity, heat multipliers)
- **Rigid-body dynamics** &mdash; impulse/depenetration response with configurable restitution (0.35) and separation scale
- **Supernova triggering** &mdash; stellar collisions can trigger supernovae (thermonuclear or core-collapse)
- **Spatial hash broadphase** &mdash; optional spatial hashing for efficient collision detection

### Roche Limit
Tidal disruption of bodies that approach within the Roche limit of a more massive body. Supports both fluid and rigid body Roche limits with independent scaling multipliers. Disrupted bodies form ring systems or fragment clouds. Tidal heating is applied during close encounters.

### Planetary Rings
Ring systems generated from Roche limit disruption or spawned manually. 7 ring styles:

| Style | Description |
|---|---|
| Saturn-like | Classic multi-gap system |
| Uranus-like | Narrow tilted rings |
| Neptune-like | Thin, sparse arcs |
| Torus | Thick donut-shaped ring |
| Realistic Disk | Default, physically motivated distribution |
| Geometric | Exaggerated visual geometry |
| Resonance Gaps | Kirkwood-style cleared lanes |

Rings are composed of non-attracting dust particles (minimum 8) with configurable ice fraction, density, inner/outer radius, and global scale multipliers. A mixture of dust, asteroid, and comet particle types populates each ring. Dynamic budget caps ensure ring spawning doesn't degrade performance.

### Temperature System
- Radiative cooling (Stefan-Boltzmann)
- Collision heating (configurable KE-to-heat fraction)
- Star luminosity heating of nearby bodies
- Material phase transitions driven by temperature changes

### Evaporation & Mass Loss
Bodies lose mass through stellar wind, atmospheric erosion, and evaporation. Atmosphere retention tracks cumulative erosion &mdash; stripped atmospheres reduce clouds, weather, and surface features. Escaped mass, energy, and momentum are tracked globally.

Stellar evolution fuel burn and wind mass loss are rate-capped per step to prevent catastrophic star death at extreme time scales. Wind mass loss is capped at 0.005% of stellar mass per step, and fuel burn at 0.05% per step, ensuring stable planetary systems even at time scales of 1 Myr/s and beyond.

### Stellar Wind Pressure
Luminous stars exert radiation/wind pressure on nearby bodies, with configurable strength scaling. Wind and Yarkovsky forces use a physical beta-ratio check (radiation pressure / gravitational acceleration) and are only applied when the ratio exceeds 0.1%, preventing spurious velocity kicks on planets at high time scales.

### Tidal Locking
Bodies orbiting close to a massive primary experience tidal torque and evolve toward synchronous rotation. Configurable locking rate.

### Hawking Radiation
Black holes lose mass through Hawking radiation, with evaporation rate inversely proportional to mass squared. Primordial black holes can evaporate entirely. Configurable rate scaling.

### Spin Fragmentation
Bodies spinning faster than their structural integrity allows will fragment due to centrifugal forces. Configurable threshold ratio.

### Space Weather
Dynamic processes including stellar mass loss, atmospheric stripping, and impact effects (craters, ejecta, heat signatures).

### Orbital Mechanics
- **Orbital element computation** &mdash; eccentricity tracking for each body
- **Dominant primary detection** &mdash; identifies the strongest gravitational influence on each body
- **Orbital resonance detection** &mdash; identifies bodies in resonant orbits (optional)
- **Lagrange point computation** &mdash; L1&ndash;L5 point display (optional)
- **Habitable zone display** &mdash; renders habitable zone indicators around stars (optional)

## Nebula Particle Clouds

Nebulae spawn as clouds of 40&ndash;300 small dust particles arranged in a 3D Gaussian ellipsoidal distribution with random axis ratios. Each particle receives:
- Power-law mass distribution from the total cloud mass
- Turbulent velocity component plus bulk rotational motion
- Gravitational attraction (particles are attracting bodies), enabling natural gravitational collapse into protostars

### Nebula Collapse & Sink Formation

Dense, converging nebula cores can spawn protostars when enabled:
- **Collapse metric** computed from density and convergence velocity
- **Sink threshold** controls when protostars form (higher = slower collapse)
- **Mass fractions** control how much host-cloud mass is consumed vs. converted to the new star
- **Minimum sink mass** prevents formation of insignificant protostars

## Timestep System

Logarithmic timescale spanning 30 orders of magnitude:
- Range: 10&#8315;&#8313; to 10&#178;&#185; simulated seconds per real second
- Human-readable labels from "1 ns/s" through "31.7 trillion years/s"
- Adaptive timestepping available &mdash; dynamically limits dt for stability with configurable safety factor, min/max bounds
- Reverse time support for rewinding simulations
- Physics substeps per rendered frame (configurable)

## Preset Scenarios

25 built-in scenarios covering a range of astrophysical systems:

| # | Name | Description |
|---|---|---|
| 0 | Solar System | Sun with 4 planets, a moon, and an asteroid belt |
| 1 | Binary Stars | Two stars in mutual orbit with circumbinary planets |
| 2 | TRAPPIST-1 | Red dwarf with 7 tightly packed rocky/water worlds |
| 3 | Hot Jupiter | Gas giant dangerously close to its star |
| 4 | Giant Impact | Earth and Theia moments before the Moon-forming collision |
| 5 | Stellar Graveyard | Neutron star, white dwarf, and stellar black hole |
| 6 | Protoplanetary Disk | Young star surrounded by a disk of dust and forming planets |
| 7 | Ringed Worlds | Gas giants and rocky planets with spectacular ring systems |
| 8 | Star Cluster | A dozen diverse stars in a loose open cluster |
| 9 | Comet Shower | Inner solar system under bombardment from Oort cloud comets |
| 10 | Rogue Planet | A wandering gas giant with moons and captured asteroids |
| 11 | Supermassive Black Hole | Galactic center with stars orbiting a 4-million solar mass black hole |
| 12 | Habitable Zone Tour | Four different habitable worlds around various star types |
| 13 | Stellar Evolution | Stars at every life stage from main sequence to neutron star |
| 14 | Figure Eight | Three equal-mass stars in a stable figure-8 choreography |
| 15 | Asteroid Belt | Rocky planets, a dense asteroid belt, and outer gas giants |
| 16 | Wolf-Rayet Star | Massive dying star shedding its outer layers |
| 17 | Collision Course | Two star systems on a direct approach toward each other |
| 18 | Nebula Collapse | Giant gas cloud collapsing under gravity to form stars |
| 19 | Pulsar Binary | Millisecond pulsar stripping mass from a white dwarf companion |
| 20 | Trojan Asteroids | Jupiter-like planet with asteroid swarms at L4 and L5 points |
| 21 | Exomoon System | Gas giant with volcanic, ocean, and icy moons in detail |
| 22 | Hierarchical Triple | Close binary star orbited by a distant third star with planets |
| 23 | Tatooine | Habitable world orbiting twin suns with two moons |
| 24 | Black Hole Accretion | Stellar black hole tearing apart a blue supergiant companion |

All presets initialize bodies with physically correct orbital velocities (circular Keplerian for single-star systems, center-of-mass balanced for binaries). Camera position and distance are set to frame the scene appropriately.

## Numerical Stability

Several safeguards prevent numerical blow-up at extreme time scales and close encounters:

### Acceleration Clamping
Gravitational accelerations are clamped to 10&#8312; per step. Non-finite accelerations (NaN/Inf from close encounters) are discarded entirely.

### Velocity & Position Sanitization
After each integration step, all body velocities are clamped to 10&#8310;. NaN velocities revert to their pre-integration values; NaN positions revert and zero the velocity.

### Orbital-Period Substepping
When time acceleration is high, the integrator ensures a minimum of 32 steps per shortest orbital period using precomputed orbital elements or a Kepler estimate (T = 2&pi;&radic;(r&sup3;/GM)). Substeps are capped at 512 to bound computational cost.

### Stellar Evolution Rate Caps
Per-step fuel burn (max 0.05%) and wind mass loss (max 0.005% of stellar mass) prevent catastrophic stellar aging at high time scales. Without these caps, a TRAPPIST-1-class red dwarf would lose 96% of its mass in under one second at 1 yr/s time scale.

### Beta-Ratio Gating
Stellar wind and Yarkovsky forces are only applied when the radiation-to-gravity acceleration ratio exceeds 0.1%, preventing spurious velocity kicks on massive bodies.

### NaN Guards
Tidal locking, YORP torque, and merger momentum calculations include explicit NaN checks with safe fallback values, preventing NaN propagation through angular velocity and position fields.

## Rendering

### GPU Sphere Raytracing
All bodies rendered via GPU raytracing in a fragment shader (`cosmos_rt.frag`, ~2,870 lines). 7 render classes: Star, Planet, Moon, Asteroid, Comet, Black Hole, Nebula. Per-body visual properties include:
- Terrain amplitude/frequency, ridge amplitude, crater density
- Rock/ice/metal/dust material fractions
- Atmospheric haze (Rayleigh + Mie scattering)
- Cloud detail, weather effects, volcanic activity
- Aurora, stellar flares, corona, starspots, pulsation, differential rotation
- Comet coma and tail
- Black hole accretion disk, relativistic jets, gravitational lensing
- Continent/island/river/ice sheet surface detail
- City lights on populated worlds

### Nebula Compute Shader
A dedicated Vulkan compute shader (`cosmos_nebula.comp`, ~260 lines) handles nebula particle advection and density field computation. Gravity-coupled advection with configurable collapse and compression scales.

### Nebula Rendering
3 render modes:

| Mode | Description |
|---|---|
| Raymarch | CPU-driven volumetric rendering |
| Raymarch + Compute | Hybrid with GPU density computation |
| Advanced Particles | Full GPU particle system |

### Background Presets
20 background styles:

| # | Name | Description |
|---|---|---|
| 0 | Realistic | Natural starfield with subtle nebulosity |
| 1 | Deep Black | Minimal stars, near-black sky |
| 2 | Nebula | Colorful emission nebula backdrop |
| 3 | Warm Dust | Reddish interstellar dust clouds |
| 4 | Blue Haze | Cool blue-tinted atmosphere |
| 5 | Aurora Veil | Shimmering aurora-like curtains |
| 6 | Crimson Rift | Deep red nebula with dark lanes |
| 7 | Galactic Core | Dense central bulge starfield |
| 8 | Monochrome | Grayscale starfield |
| 9 | Emerald Sea | Green-tinted nebulosity |
| 10 | Infrared Dust | Warm infrared false-color view |
| 11 | Deep Field | Dense distant galaxy field |
| 12 | Milky Way Panorama | Bright galactic plane with central bulge and dark molecular clouds |
| 13 | Orion Nebula | H-alpha pink/magenta emission with blue reflection nebulosity |
| 14 | Carina Nebula | Gold and teal pillars with ionization fronts |
| 15 | Cosmic Microwave Background | Dipole anisotropy with temperature fluctuations |
| 16 | Void | Near-black with sparse isolated stars |
| 17 | Eagle Nebula | Pillars of Creation dark columns against OII/OIII emission |
| 18 | Supernova Remnant | Filamentary shock shell with hot gas |
| 19 | Stellar Nursery | Warm molecular cloud with embedded protostars |

### Quality Levels
4 quality presets: Low, Balanced, High, Ultra. Controls shader detail, shadow quality, and corona effects.

### Lighting System
- **Star lighting** &mdash; stars act as point light sources for nearby bodies
- **Uniform lighting** &mdash; everything uniformly illuminated (optional fallback)
- **Fast star lighting** &mdash; strongest-star direct lighting optimization
- **Configurable ambient** &mdash; base ambient light level
- **Per-feature toggles** &mdash; corona, comet tails, black hole lensing, background starfield
- **Per-feature strength** &mdash; independent intensity scales for corona, tails, lensing, starfield

### Space Fabric
Optional gravitational field visualization grid overlay with configurable grid size and strength.

## Procedural Naming

Bodies receive procedurally generated names based on type:
- **Stars**: catalog-style ("HD 4521", "HIP 7832") or Greek+constellation ("Alpha Centauri", "Gamma Orionis")
- **Black holes**: catalog-style ("Sgr 1547", "NGC 3021", "Cyg 8842")
- **Planets/moons/asteroids**: syllable-based names from onset+nucleus+coda pools with capitalization

## Spawn Studio

The Spawn Studio provides comprehensive body creation with 5 catalog tabs:

### Catalog Tabs

| Tab | Contents |
|---|---|
| Basic | Planet, Moon, Asteroid, Comet, Dust, Nebula |
| Stars | Generic Star + 11 spectral classes (O through Y, Wolf-Rayet) |
| Black Holes | Generic + Stellar, Intermediate, Supermassive, Primordial |
| Known Objects | Real astronomical objects with accurate properties (Solar System bodies, famous stars, known black holes) |
| Existing Objects | Clone/modify existing simulation bodies |

### Spawn Controls

- **Mass** slider (logarithmic, 10&#8315;&#185;&#179; to 500 solar masses)
- **Temperature** override
- **Radius** override
- **Rotation period** override
- **Custom velocity** in km/s
- **Material composition** &mdash; iron/silicate/ice/hydrogen fractions
- **Planet surface look** presets (auto, rocky, water, ice, earth-like, gas giant)
- **Stellar stage** hints
- **Orbit spawning** &mdash; auto-calculate orbital velocity around nearest body

### Moon Spawning

Configurable moon systems:
- Moon count
- Orbit layout: prograde disk, compact disk, wide disk, resonant chain, isotropic cloud
- Inclination and spacing scale

### Ring Spawning

Manual ring creation with:
- 7 ring style selection
- Inner/outer radius multipliers
- Density and ice fraction
- Override or auto-generate layout

### Small Body Batch Spawning

Asteroid/comet/dust batch spawning:
- Count selection
- Layout options: random, sphere, cube, torus

### Ghost Preview

A translucent 3D ghost body is rendered at the spawn position, showing the exact appearance of the body that will be spawned. Right-click in spawn mode re-rolls the procedural seed. The preview rebuilds automatically when type, mass, or any draft setting changes.

## Dynamic Performance Budget

Automatic body count management to maintain target framerate:
- **Attracting body cap** (default 300)
- **Non-attracting debris cap** (default 900)
- **Per-event explosion density allocation** (0&ndash;100% of non-attracting budget)
- **Automatic culling** when over budget with configurable reduction percentage
- **Target FPS** tracking (default 60)

## Save / Load

Binary `.cssim` save format (version 16) for full simulation state including all body properties, configuration, camera state, and spawn draft settings. Supports individual body export/import.

## User Interface

- **Bottom taskbar**: auto-hiding bar with menu, spawn, settings, body list, and inspector toggles
- **Settings panel**: all simulation parameters adjustable in real-time &mdash; physics, collisions, temperature, rendering, GR, nebula, performance
- **Body list panel**: scrollable list of all bodies with type-colored entries, click to select, double-click to track
- **Inspector panel**: detailed body properties on selection &mdash; mass, radius, temperature, velocity, orbital elements, atmosphere, terrain, magnetic signature; track button for camera follow
- **Orbit camera**: mouse drag to orbit, middle-drag/shift+drag to pan, scroll to zoom, WASD/QE panning; smooth focus tracking on selected bodies
- **Orbital trails**: configurable trail length, opacity, and width per body
- **Body labels**: optional floating name labels with distance culling and opacity control
- **Space fabric visualization**: optional grid overlay showing gravitational field curvature
- **Debug window**: diagnostics, body state validation, step counter, conservation tracking (escaped mass/energy/momentum)
- **Splash screen**: animated particle background, dismissible on any key/click
- **Pause menu**: resume, new simulation, save, load, quit, return to launcher
- **Loading screen**: progress bar with stage labels during initialization

### Quality of Life Features

- **Lock/pin bodies** &mdash; locked bodies are immune to gravity, collisions, and integration; velocity zeroed on lock; "LOCKED" badge overlay displayed
- **Body duplication** &mdash; Ctrl+D clones the selected body with an offset position, new seed, and automatic selection of the duplicate
- **Body search filter** &mdash; case-insensitive name and type search in the body list panel
- **Velocity arrows** &mdash; toggle (V key) shows per-body velocity vectors as arrows with blue-to-red color shift proportional to speed
- **Keyboard shortcuts overlay** &mdash; F1 toggles a centered overlay listing all camera, simulation, body, and UI shortcuts
- **Screenshot capture** &mdash; F12 saves a timestamped PNG screenshot via Vulkan swapchain readback with BGRA-to-RGBA conversion
- **Camera-inside-sphere transparency** &mdash; bodies are skipped when the camera is inside their bounding sphere, preventing black-screen rendering
