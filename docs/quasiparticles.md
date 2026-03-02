# Quasiparticles

Seven collective excitations (types 67&ndash;73) that emerge from many-body interactions in
condensed matter environments. Unlike fundamental particles, quasiparticles are not point-like
objects &mdash; they represent collective behavior of the underlying medium.

## Quasiparticle Types

| Type | Particle | Spawn Condition | Physics Effect |
|---|---|---|---|
| 67 | **Electron Hole** | High electron density | Attracts nearby electrons (positive charge carrier) |
| 68 | **Plasmon** | Hot dense electron plasma | Oscillating push/pull on nearby charged particles |
| 69 | **Phonon** | Dense nucleon lattice | Kicks nucleons along travel direction, transfers energy |
| 70 | **Magnon** | Strong B-field + fast charges | Lorentz-like deflection on nearby charged particles |
| 71 | **Polaron** | Electrons surrounded by ions | Attracts nearby positive ions (lattice distortion) |
| 72 | **Cooper Pair** | Cold slow neutron pairs | Superfluidity boost &mdash; accelerates slow nearby nucleons |
| 73 | **Roton** | Energetic Cooper pairs | Tangential vortex velocity field on neighbors |

## GPU Shader Fast-Path

Quasiparticles skip the full pairwise force calculation. Their shader behavior:

- **No nuclear/EM/strong forces** &mdash; collective excitations don't scatter off individual nuclei
- **Weak gravity only** (0.5&times; coupling) via grid-based neighbor scan
- **Velocity damping** (0.92&times; per tick) &mdash; excitations dissipate in the medium
- **Accelerated energy decay** (minimum 0.02 decay rate) &mdash; short-lived
- **Brownian thermal kicks** proportional to emergent temperature

Other particles also skip quasiparticles in their pairwise neighbor loop &mdash; a plasmon does
not Coulomb-scatter off a proton.

## CPU Physics Effects

Each living quasiparticle actively affects nearby particles within a ~120px radius via the
spatial acceleration grid:

- **Plasmon**: oscillating radial field (push/pull cycle) on charged particles
- **Phonon**: directional energy transfer along travel direction to nucleons
- **Magnon**: perpendicular Lorentz-like deflection of charged particles
- **Polaron**: attractive drag on nearby positive ions (lattice distortion)
- **Cooper pair**: superfluidity &mdash; gently boosts slow nucleons, reducing viscosity
- **Roton**: tangential vortex field, swirling neighbors around the roton center

## Spawn Conditions

Quasiparticles spawn from `check_quasiparticles()` in `nuclear.cpp`, limited to 3 per tick.
Each type requires specific environmental conditions:

| Type | Temperature | Density | Other |
|---|---|---|---|
| Electron Hole | Any | High electron count | Random vacancy in electron sea |
| Plasmon | > 1000 K | Dense electron clusters | Electron density above threshold |
| Phonon | Any | Dense nucleon clusters | Nucleon lattice vibrations |
| Magnon | Any | Fast charged particles | Emergent B-field > threshold |
| Polaron | Any | Electrons near ions | Electron-ion proximity |
| Cooper Pair | < 500 K | Slow neutron pairs | Both neutrons below velocity threshold |
| Roton | Any | Energetic Cooper pairs | Cooper pair energy above threshold |

## Toggle

**Menu > Quasi Mode** (default: ON). When disabled, `check_quasiparticles()` is skipped and
no quasiparticles are spawned. Existing quasiparticles decay naturally via the shader fast-path.

## Event Notifications

Quasiparticle spawns generate clickable decay log entries with detailed information including
source particle IDs, energies, velocities, and environmental conditions.
