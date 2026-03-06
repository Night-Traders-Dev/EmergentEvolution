# Cosmic Sandbox

The Cosmic Sandbox is a 3D celestial mechanics simulator with GPU-raytraced rendering, N-body gravity, stellar evolution, and procedurally generated planets. Bodies interact via Newtonian gravity with optional general relativity corrections.

## Celestial Body Types

23 body types organized into five categories:

### Core Types

| Type | ID | Description |
|---|---|---|
| Star | 0 | Generic star (auto-classifies by mass/temperature) |
| Planet | 1 | Rocky, oceanic, gaseous, or icy world |
| Moon | 2 | Natural satellite orbiting a planet |
| Asteroid | 3 | Small rocky body |
| Comet | 4 | Icy body with tail near heat sources |
| Black Hole | 5 | Generic black hole |
| Nebula | 6 | Gas/dust cloud |
| Dust | 22 | Ring/disk dust grain aggregate |

### Star Spectral Classes (Types 7–17)

| Class | ID | Temperature | Description |
|---|---|---|---|
| O | 7 | >30,000 K | Blue supergiant |
| B | 8 | 10,000–30,000 K | Blue-white |
| A | 9 | 7,500–10,000 K | White |
| F | 10 | 6,000–7,500 K | Yellow-white |
| G | 11 | 5,200–6,000 K | Yellow (Sun-like) |
| K | 12 | 3,700–5,200 K | Orange |
| M | 13 | 2,400–3,700 K | Red dwarf |
| L | 14 | 1,300–2,400 K | Brown dwarf |
| T | 15 | 500–1,300 K | Cool brown dwarf |
| Y | 16 | <500 K | Ultra-cool brown dwarf |
| WR | 17 | Variable | Wolf-Rayet |

### Black Hole Subtypes (Types 18–21)

| Subtype | ID | Mass Range |
|---|---|---|
| Stellar | 18 | 3–20 solar masses |
| Intermediate | 19 | 100–100,000 solar masses |
| Supermassive | 20 | 10⁶–10¹⁰ solar masses |
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

Stars undergo supernova events at end-of-life, producing fragments, neutron stars, or black holes depending on mass.

## Procedural Planet Generation

Planets and moons are procedurally generated from a deterministic seed, mass, and temperature. Properties include:

### Surface Composition
- **Rocky** — barren terrain (Mercury/Mars-like)
- **Liquid** — water world with deep global oceans
- **Frozen** — ice-covered surface
- **Gas** — no solid surface (gas/ice giants)
- **Mixed** — continents with oceans (Earth-like)

### Planet Classes
- **Dwarf** — small, low-mass body (<0.18 Earth masses)
- **Terrestrial** — rocky world with possible atmosphere
- **Ocean** — liquid-water-dominated surface
- **Super-Earth** — massive terrestrial (>2.4 Earth masses)
- **Ice Giant** — intermediate gas/ice body
- **Gas Giant** — massive hydrogen/helium body (>45 Earth masses)

### Atmosphere System
13 atmospheric gas species modeled: N₂, O₂, CO₂, H₂, He, CH₄, NH₃, H₂O vapor, Ar, Ne, SO₂, CO, H₂S. Atmosphere properties include:
- Pressure (atmospheres)
- Cloud coverage and weather intensity
- Up to 8 atmospheric layers with individual altitude, thickness, pressure, temperature offset, opacity, and IR emissivity
- Greenhouse effect computed from gas composition, pressure, and cloud coverage
- Venus-like thick CO₂ atmospheres, Titan-like methane hazes, Earth-like N₂/O₂ compositions

### Oceans
5 ocean types: None, Water, Methane, Ammonia, Lava. Temperature-dependent — changing a planet's temperature can melt ice into oceans or freeze them over.

### Terrain Features
Mountains, valleys, continents, islands, rivers, ice sheets, and vegetation. Coverage and heights are seed-deterministic but temperature-responsive. Vegetation requires water oceans, atmospheric O₂, and temperate conditions (240–340 K).

### Weather
4 weather types: Storms, Rain, Snow, Dust. Driven by atmospheric pressure, ocean coverage, and temperature.

## Physics Systems

### N-Body Gravity
Pairwise gravitational interaction with configurable gravitational constant scaling and softening. Supports:
- **Barnes-Hut** tree approximation for large body counts
- **GPU compute** Barnes-Hut acceleration via Vulkan compute shader
- **Parallel gravity** via OpenMP

### Integrators
6 integration methods:

| Integrator | Description |
|---|---|
| Velocity Verlet | Symplectic, second-order (default) |
| Euler Explicit | First-order, fast but less stable |
| Euler Semi-Implicit | First-order symplectic |
| RK2 | Midpoint method, second-order |
| Forest-Ruth | Fourth-order symplectic |
| PEFRL | Position-Extended Forest-Ruth-Like, fourth-order |

### General Relativity Corrections
Optional GR effects:
- **Perihelion precession** — orbit precession scaling
- **Gravitational time dilation** — time rate near massive bodies
- **Frame dragging** — Lense-Thirring effect from spinning bodies
- Configurable speed of light in simulation units

### Collision Physics
- **Merging** — low-speed collisions combine bodies (mass, momentum, temperature conserved)
- **Fragmentation** — high-speed collisions shatter bodies into configurable fragment count (1–12)
- **SPH-like** soft-body pressure/viscosity for impacts
- **Rigid-body dynamics** — impulse/depenetration response
- **Supernova triggering** — stellar collisions can trigger supernovae

### Roche Limit
Tidal disruption of bodies that approach within the Roche limit of a more massive body. Supports both fluid and rigid body Roche limits. Disrupted bodies form ring systems or fragment clouds.

### Planetary Rings
Ring systems generated from Roche limit disruption or spawned manually. 7 ring styles: Saturn-like, Uranus-like, Neptune-like, torus, realistic disk, geometric, resonance gaps. Rings are composed of dust particles with configurable ice fraction, density, and extent.

### Temperature System
- Radiative cooling (Stefan-Boltzmann)
- Collision heating (kinetic energy conversion)
- Star luminosity heating of nearby bodies
- Material phase transitions: Solid, Liquid, Ice, Gas, Molten, Plasma, Collapsing

### Evaporation & Mass Loss
Bodies lose mass through stellar wind, atmospheric erosion, and evaporation. Atmosphere retention tracks cumulative erosion — stripped atmospheres reduce clouds, weather, and surface features.

### Space Weather
Dynamic processes including stellar mass loss, atmospheric stripping, and impact effects (craters, ejecta, heat signatures).

## Timestep System

Logarithmic timescale spanning 30 orders of magnitude:
- Range: 10⁻⁹ to 10²¹ simulated seconds per real second
- Human-readable labels from "1 ns/s" through "31.7 trillion years/s"
- Adaptive timestepping available — dynamically limits dt for stability with configurable safety factor, min/max bounds

## Rendering

### GPU Sphere Raytracing
All bodies rendered via GPU raytracing in a fragment shader (`cosmos_rt.frag`). Per-body visual properties include:
- Terrain amplitude/frequency, ridge amplitude, crater density
- Rock/ice/metal/dust material fractions
- Atmospheric haze (Rayleigh + Mie scattering)
- Cloud detail, weather effects, volcanic activity
- Aurora, stellar flares, corona, starspots, pulsation
- Comet coma and tail
- Black hole accretion disk, relativistic jets, gravitational lensing
- Continent/island/river/ice sheet surface detail

### Background Presets
12 background styles: Realistic, Deep Black, Nebula, Warm Dust, Blue Haze, Aurora Veil, Crimson Rift, Galactic Core, Monochrome, Emerald Sea, Infrared Dust, Deep Field.

### Quality Levels
4 quality presets: Low, Balanced, High, Ultra.

### Nebula Rendering
3 render modes: raymarch, raymarch+compute, advanced particles. Nebula particles advect with gravity fields and can collapse into protostars.

## Procedural Naming

Bodies receive procedurally generated names based on type:
- **Stars**: catalog-style ("HD 4521", "HIP 7832") or Greek+constellation ("Alpha Centauri", "Gamma Orionis")
- **Black holes**: catalog-style ("Sgr 1547", "NGC 3021", "Cyg 8842")
- **Planets/moons/asteroids**: syllable-based names from onset+nucleus+coda pools with capitalization

## Spawn System

The spawn menu supports:
- Body type selection (all 23 types)
- Mass, temperature, radius, rotation period overrides
- Custom material composition (iron/silicate/ice/hydrogen fractions)
- Planet surface look presets (auto, rocky, water, ice, earth-like, gas giant)
- Stellar stage hints
- Orbit spawning (auto-calculate orbital velocity around nearest body)
- Moon spawning with configurable count, orbit layout (prograde disk, compact, wide, resonant chain, isotropic cloud), inclination, and spacing
- Ring spawning with style selection, inner/outer radius, density, ice fraction
- Asteroid/comet batch spawning with layout options (random, sphere, cube, torus)

## Dynamic Performance Budget

Automatic body count management to maintain target framerate:
- Attracting fragment cap (default 300)
- Non-attracting debris cap (default 900)
- Per-event explosion density allocation
- Automatic culling when over budget

## Save / Load

Binary save format for full simulation state. Supports individual body export/import.

## User Interface

- **Bottom taskbar**: auto-hiding bar with spawn menu, settings, body list toggles
- **Inspector panel**: detailed body properties shown on selection (click to select, double-click to track)
- **Orbit camera**: mouse drag to orbit, middle-drag/shift+drag to pan, scroll to zoom, WASD panning
- **Orbital trails**: configurable trail length per body
- **Space fabric visualization**: optional grid overlay showing gravitational field curvature
- **Splash screen**: dismissible startup screen
- **Pause menu**: resume, new simulation, save, load, quit
