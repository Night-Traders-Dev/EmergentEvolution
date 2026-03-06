# Biochemical Simulator

The Biochemical Simulator is a 3D cellular biology sandbox with GPU-raytraced rendering. Entities interact through proximity-based rules covering metabolism, reproduction, infection, and immune response. Four environment presets model distinct biological milieus with gene-driven entity behavior.

## Entity Types

8 biological entity types with morphological variants:

| Type | ID | Morphologies | Description |
|---|---|---|---|
| Cell | 0 | Animal, Epithelial, Amoeboid | Generic eukaryotic cell &mdash; metabolizes nutrients, divides when energy threshold reached |
| Bacterium | 1 | Cocci, Bacilli, Spiral | Prokaryote &mdash; competes with cells for nutrients |
| Virus | 2 | Classical Capsid, Coronavirus, Bacteriophage | Viral particle &mdash; infects nearby cells/bacteria within infection radius |
| Nutrient | 3 | &mdash; | Food / glucose molecule &mdash; consumed by cells and bacteria for energy |
| Toxin | 4 | &mdash; | Harmful chemical &mdash; damages nearby entities |
| Antibody | 5 | &mdash; | Immune system agent &mdash; neutralizes viruses and bacteria |
| Red Blood Cell | 6 | &mdash; | Erythrocyte &mdash; oxygen transport |
| White Blood Cell | 7 | &mdash; | Leukocyte &mdash; immune response, seeks and destroys pathogens |

## Entity Properties

Each entity has:

- **Position** and **velocity** in 3D space
- **Radius** (default 8.0 units)
- **Energy** (health / metabolic energy, default 100)
- **Age** (seconds alive)
- **Genome** tag (simple integer for tracking mutations)
- **Morphology** variant (determines visual shape via SDF rendering)
- **Genes** (5 behavioral traits: seek, flee, spacing, brownian, energy)
- **Shape** parameters (aspect ratio, noise amplitude, phase)
- **Alive** flag

## Gene System

Each entity carries a `BioGenes` struct with 5 heritable traits that modulate behavior:

| Gene | Default | Effect |
|---|---|---|
| `seek` | 1.0 | Multiplier on nutrient-seeking strength |
| `flee` | 1.0 | Multiplier on threat-avoidance strength |
| `spacing` | 1.0 | Multiplier on same-type neighbor spacing |
| `brownian` | 1.0 | Multiplier on random thermal drift |
| `energy` | 1.0 | Multiplier on metabolic efficiency |

During cell division, each gene has a chance to mutate (scaled by `mutation_rate`), creating heritable variation in the population.

## Environment Presets

4 environment presets, each configuring temperature, acidity, oxygen, nutrients, flow, toxicity, immune pressure, and fluid damping:

| Preset | Temp | pH | O&#8322; | Description |
|---|---|---|---|---|
| Human Lung | 37.0 &deg;C | 7.25 | 0.98 | Warm, oxygen-rich tissue with active immune surveillance and rhythmic airflow |
| Pond Water | 18.0 &deg;C | 6.70 | 0.58 | Cool, nutrient-rich water with suspended toxins, weak immunity, and slow currents |
| Petri Dish | 30.0 &deg;C | 7.05 | 0.76 | Engineered culture media with high nutrient availability, low flow, and weak immunity |
| Cat Brain | 38.2 &deg;C | 7.32 | 0.88 | Warm, protected neural tissue with high metabolic demand and selective immunity |

Each preset also defines a visual tint, flow axis and strength, and whether the immune system is active.

### Environment Features

Environments can contain placed features that locally modify conditions:

| Feature | Effect |
|---|---|
| Membrane | Semi-permeable barrier |
| Nutrient | Local nutrient-rich zone |
| Toxin | Local toxic zone |
| Current | Directed fluid flow |

## Simulation Systems

### Metabolism

Entities consume energy at a configurable rate (`metabolism_rate`). Cells and bacteria gain energy by absorbing nearby nutrients. When energy reaches zero, entities die.

### Cell Division

When a cell or bacterium accumulates energy above `division_energy` (default 150), it divides into two daughter cells. Each division has a configurable `mutation_rate` (default 1%) chance of altering the genome tag and gene values.

### Viral Infection

Viruses infect cells and bacteria within `infection_radius` (default 20 units) with probability `infection_rate` (default 50%) per contact per tick. Infected cells lose energy and eventually die.

### Immune Response

When enabled, antibodies and white blood cells seek out and neutralize viruses and bacteria. Effectiveness scales with `immune_strength` (default 1.0) and `immune_pressure` from the environment preset.

### AI Movement

Entities use behavior-based AI steering, modulated by individual gene values:

- **Seek** &mdash; cells/bacteria move toward nutrients (`seek_strength` = 40)
- **Flee** &mdash; entities move away from threats like toxins (`flee_strength` = 60)
- **Spacing** &mdash; maintain minimum distance from same-type neighbors (`spacing_strength` = 20)
- **Brownian motion** &mdash; random drift simulating thermal motion (`brownian_strength` = 15)

### Fluid Dynamics

Velocity damping (`viscosity` = 0.98) simulates fluid drag. Entities experience continuous deceleration proportional to their speed. Environment presets provide a `fluid_damping` value and directional `flow_axis` for currents.

### Repulsion

Hard-sphere-like repulsion prevents entities from overlapping.

### Nutrient Spawning

Nutrients spawn at a configurable rate (`nutrient_rate` = 2.0/second) at random positions within the world volume.

## Configuration

| Parameter | Default | Description |
|---|---|---|
| `entity_count` | 200 | Initial entity count |
| `nutrient_rate` | 2.0 | Nutrients spawned per second |
| `metabolism_rate` | 1.0 | Energy consumption rate |
| `division_energy` | 150.0 | Energy threshold for cell division |
| `mutation_rate` | 0.01 | Per-division mutation probability |
| `infection_radius` | 20.0 | Virus infection range (units) |
| `infection_rate` | 0.5 | Infection probability per contact |
| `immune_strength` | 1.0 | Antibody effectiveness multiplier |
| `viscosity` | 0.98 | Fluid damping (1.0 = no damping) |
| `dt_scale` | 1.0 | Simulation speed multiplier |
| `world_radius` | 200.0 | 3D world bounds radius (entities wrap) |
| `ai_movement` | true | Enable AI steering behaviors |
| `immune_system` | true | Enable immune response |
| `show_energy_bars` | true | Display energy bars above entities |
| `temperature_c` | 37.0 | Environment temperature (&deg;C) |
| `acidity_ph` | 7.25 | Environment acidity (pH) |
| `oxygen_level` | 0.98 | Dissolved oxygen level (0&ndash;1) |
| `nutrient_density` | 0.95 | Ambient nutrient availability |
| `flow_strength` | 24.0 | Fluid current strength |
| `toxicity` | 0.04 | Background toxin level |
| `immune_pressure` | 1.10 | Immune system aggressiveness |
| `ambient_strength` | 0.12 | Lighting ambient strength |

## Rendering

### GPU SDF Raytracing

All entities are rendered via a GPU fragment shader (`biochem_rt.frag`, ~815 lines) using signed distance field (SDF) raymarching. Each entity type and morphological variant has a unique SDF shape:

- **Cells**: smooth blobs with nucleus and organelle interiors visible
- **Bacteria**: cocci (spheres), bacilli (capsules), spirals (helical)
- **Viruses**: icosahedral capsids, corona spike proteins, bacteriophage legs
- **Blood cells**: biconcave discs (RBC), irregular amoeboid shapes (WBC)

Lighting uses configurable ambient strength (default 0.12) with diffuse and specular components.

### Orbit Camera

- Mouse drag to orbit
- Scroll to zoom
- WASD/QE to pan
- Click to select entities

## User Interface

- **Bottom taskbar**: auto-hiding bar with spawn menu, settings, and population graph toggles
- **Spawn menu**: select entity type, morphological variant, and initial energy; click to place
- **Settings panel**: all simulation parameters and environment preset selection adjustable in real-time
- **Population panel**: live entity type counts
- **Entity selection**: click to select and inspect individual entities
- **Splash screen**: dismissible startup screen
- **Pause menu**: resume, new simulation, quit, return to launcher
