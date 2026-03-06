# Biochemical Simulator

The Biochemical Simulator is a 3D cellular biology sandbox with GPU-raytraced rendering. Entities interact through proximity-based rules covering metabolism, reproduction, infection, and immune response.

## Entity Types

8 biological entity types:

| Type | ID | Description |
|---|---|---|
| Cell | 0 | Generic eukaryotic cell — metabolizes nutrients, divides when energy threshold reached |
| Bacterium | 1 | Prokaryote — competes with cells for nutrients |
| Virus | 2 | Viral particle — infects nearby cells/bacteria within infection radius |
| Nutrient | 3 | Food / glucose molecule — consumed by cells and bacteria for energy |
| Toxin | 4 | Harmful chemical — damages nearby entities |
| Antibody | 5 | Immune system agent — neutralizes viruses and bacteria |
| Red Blood Cell | 6 | Erythrocyte — oxygen transport |
| White Blood Cell | 7 | Leukocyte — immune response, seeks and destroys pathogens |

## Entity Properties

Each entity has:
- **Position** and **velocity** in 3D space
- **Radius** (default 8.0 units)
- **Energy** (health / metabolic energy, default 100)
- **Age** (seconds alive)
- **Genome** tag (simple integer for tracking mutations)
- **Alive** flag

## Simulation Systems

### Metabolism
Entities consume energy at a configurable rate (`metabolism_rate`). Cells and bacteria gain energy by absorbing nearby nutrients. When energy reaches zero, entities die.

### Cell Division
When a cell or bacterium accumulates energy above `division_energy` (default 150), it divides into two daughter cells. Each division has a configurable `mutation_rate` (default 1%) chance of altering the genome tag.

### Viral Infection
Viruses infect cells and bacteria within `infection_radius` (default 20 units) with probability `infection_rate` (default 50%) per contact per tick. Infected cells lose energy and eventually die.

### Immune Response
When enabled, antibodies and white blood cells seek out and neutralize viruses and bacteria. Effectiveness scales with `immune_strength` (default 1.0).

### AI Movement
Entities use behavior-based AI steering:
- **Seek** — cells/bacteria move toward nutrients (`seek_strength` = 40)
- **Flee** — entities move away from threats like toxins (`flee_strength` = 60)
- **Spacing** — maintain minimum distance from same-type neighbors (`spacing_strength` = 20)
- **Brownian motion** — random drift simulating thermal motion (`brownian_strength` = 15)

### Fluid Dynamics
Velocity damping (`viscosity` = 0.98) simulates fluid drag. Entities experience continuous deceleration proportional to their speed.

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

## Rendering

### GPU Sphere Raytracing
All entities are rendered as spheres via a GPU fragment shader (`biochem_rt.frag`). Each entity type has distinct coloring. Lighting uses a configurable ambient strength (default 0.12).

### Orbit Camera
- Mouse drag to orbit
- Scroll to zoom
- WASD to pan
- Click to select entities

## User Interface

- **Bottom taskbar**: auto-hiding bar with spawn menu, settings, and population graph toggles
- **Spawn menu**: select entity type and initial energy, click to place
- **Settings panel**: all simulation parameters adjustable in real-time
- **Population panel**: live entity type counts
- **Entity selection**: click to select and inspect individual entities
- **Splash screen**: dismissible startup screen
- **Pause menu**: resume, new simulation, quit, return to launcher
