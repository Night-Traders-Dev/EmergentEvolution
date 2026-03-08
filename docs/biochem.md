# Biochemical Simulator

The Biochemical Simulator is a 3D cellular biology sandbox with GPU SDF-raytraced rendering. Entities interact through proximity-based rules covering metabolism, reproduction, infection, immune response, antibiotic warfare, and phagocytic cleanup. Eight environment presets model distinct biological milieus with terrain-scale SDF structures, CPU-side collision detection, gene-driven entity behavior, and heritable mutations.

## Entity Types

9 biological entity types with 19 morphological variants across 3 visual families:

| Type | ID | Variants | Visual Families | Description |
|---|---|---|---|---|
| Cell | 0 | 8 (Generic Animal, Type II Pneumocyte, Ciliated Epithelial, Enterocyte, Neuron, Astrocyte, Fibroblast, Amoeboid) | Animal, Epithelial, Amoeboid | Eukaryotic cell &mdash; metabolizes nutrients, divides via mitosis with telomere tracking |
| Bacterium | 1 | 6 (Staphylococcus aureus, Streptococcus pneumoniae, Escherichia coli, Pseudomonas aeruginosa, Bacillus subtilis, Vibrio cholerae) | Cocci, Bacilli, Spiral | Prokaryote &mdash; binary fission, secretes antibiotic defense films, faster metabolism |
| Virus | 2 | 5 (Human adenovirus C5, SARS-CoV-2, Influenza A H1N1, Influenza A H3N2, Enterobacteria phage T4) | Capsid, Corona, Influenza, Phage | Viral particle &mdash; infects cells/bacteria, replicates internally, causes lysis burst |
| Nutrient | 3 | &mdash; | &mdash; | Food / glucose molecule &mdash; consumed by cells and bacteria for energy |
| Toxin | 4 | &mdash; | &mdash; | Harmful chemical &mdash; damages nearby entities on contact |
| Antibody | 5 | &mdash; | &mdash; | Immune protein &mdash; seeks and neutralizes viruses on contact |
| Red Blood Cell | 6 | &mdash; | &mdash; | Erythrocyte &mdash; passive oxygen transport, brownian drift |
| White Blood Cell | 7 | &mdash; | &mdash; | Leukocyte &mdash; immune response, seeks and engulfs pathogens, can divide |
| Phagocyte | 8 | &mdash; | &mdash; | Cleanup cell &mdash; scavenges corpse husks, auto-spawns when corpses accumulate |

## Entity Properties

Each entity has:

- **Position** and **velocity** in 3D space
- **Radius** (default 8.0 units)
- **Energy** (health / metabolic energy, default 100)
- **Age** (seconds alive)
- **Generation** and **parent ID** for lineage tracking
- **Genome** tag (integer for tracking mutations)
- **Morphology** variant (determines visual shape via SDF rendering)
- **Genes** (18 behavioral/metabolic traits &mdash; see Gene System)
- **Shape** parameters (aspect ratio, noise amplitude, phase)
- **Organelle health** (0&ndash;1, cellular vitality)
- **Nutrient reserve** (0&ndash;1, stored food)
- **Telomere state** (0&ndash;1, replicative lifespan for cells/WBC)
- **Antibiotic film** (0&ndash;1, bacterial defense secretion)
- **Infection state** (progress, virion load, source ID)
- **Mitosis progress** (0&ndash;1, division animation state)
- **ATP** pool (cellular energy currency, default 80 for cells, 50 for bacteria)
- **Quorum signal** (autoinducer concentration, bacteria only)
- **Complement tag** (opsonization level 0&ndash;1, marks targets for immune clearance)
- **Resistance level** (accumulated antibiotic resistance, bacteria only)
- **Immune subtype** (WBC only: generic, T cell, or B cell)
- **Corpse** flag and **corpse age** (dead husk state)
- **Alive** flag

## Gene System

Each entity carries a `BioGenes` struct with 18 heritable traits that modulate behavior:

| Gene | Range | Default | Effect |
|---|---|---|---|
| `seek` | 0.05&ndash;2.50 | 1.0 | Multiplier on nutrient-seeking strength |
| `flee` | 0.05&ndash;2.50 | 1.0 | Multiplier on threat-avoidance strength |
| `spacing` | 0.05&ndash;2.50 | 1.0 | Multiplier on same-type neighbor spacing |
| `brownian` | 0.00&ndash;2.50 | 1.0 | Multiplier on random thermal drift |
| `energy` | 0.35&ndash;2.25 | 1.0 | Multiplier on metabolic efficiency |
| `telomere` | 0.00&ndash;2.25 | 1.0 | Telomere length multiplier (cells/WBC only) |
| `mitotic_clock` | 0.25&ndash;2.50 | 1.0 | Division speed multiplier |
| `metabolism_efficiency` | 0.35&ndash;2.25 | 1.0 | Energy usage rate multiplier |
| `nutrient_affinity` | 0.25&ndash;2.50 | 1.0 | Nutrient attraction strength |
| `stress_tolerance` | 0.35&ndash;2.50 | 1.0 | Resistance to environmental damage |
| `defense` | 0.35&ndash;2.50 | 1.0 | Pathogen resistance multiplier |
| `sensing` | 0.20&ndash;2.50 | 1.0 | Detection range multiplier |
| `mutation_stability` | 0.25&ndash;2.50 | 1.0 | Mutation susceptibility (higher = more stable) |
| `antibiotic_type` | 0.00&ndash;1.00 | varies | Antibiotic spectrum band (bacteria only) |
| `antibiotic_yield` | 0.00&ndash;2.50 | varies | Antibiotic potency (bacteria only) |
| `antibiotic_diversity` | 0.00&ndash;1.00 | varies | Antibiotic spectrum width (bacteria only) |
| `resistance` | 0.00&ndash;2.50 | 0.0 | Antibiotic resistance level (bacteria only) |
| `quorum_threshold` | 0.00&ndash;1.00 | 0.5 | Quorum sensing activation threshold (bacteria only) |

During cell division, each gene has a chance to mutate (scaled by `mutation_rate`, default 1%), plus a 25% chance of morphology shift, creating heritable variation in the population.

## Environment Presets

8 environment presets, each configuring temperature, acidity, oxygen, nutrients, flow, toxicity, immune pressure, and fluid damping:

| Preset | Temp | pH | O&#8322; | Immune | Description |
|---|---|---|---|---|---|
| Human Lung | 37.0 &deg;C | 7.25 | 0.98 | Active | Warm, oxygen-rich tissue with active immune surveillance and rhythmic airflow. Bronchiole tunnels and alveolar sac structures. |
| Pond Water | 18.0 &deg;C | 6.70 | 0.58 | Inactive | Cool, nutrient-rich water (density 1.45) with suspended toxins, weak immunity, and slow currents. Massive rocks and reed stem pillars. |
| Petri Dish | 30.0 &deg;C | 7.05 | 0.76 | Inactive | Engineered culture media with high nutrient availability (density 1.65), minimal flow, and weak immunity. Glass rim, agar floor, and streak zones. |
| Cat Brain | 38.2 &deg;C | 7.32 | 0.88 | Active | Warm, protected neural tissue with highest metabolic demand and selective immunity. Cortical fold walls and cell-scale capillaries. |
| Gut Microbiome | 37.0 &deg;C | 6.80 | 0.08 | Active | Warm, anaerobic intestinal lumen with dense microbial communities. Intestinal villi, crypts of Lieberkuhn, mucus membranes, peristaltic currents. |
| Blood Stream | 37.0 &deg;C | 7.40 | 0.95 | Active | Fast-flowing arterial blood with tight pH buffering and intense immune surveillance. Vessel endothelium wall, valve leaflets, strong laminar flow. |
| Soil Rhizosphere | 18.0 &deg;C | 6.20 | 0.32 | Inactive | Cool, variable-oxygen ground with organic matter and root exudates. Mineral grain boulders, branching plant roots, slow percolation. |
| Wound Site | 37.5 &deg;C | 6.40 | 0.25 | Active | Inflamed, hypoxic tissue with high immune infiltration and bacterial colonization. Ragged wound-edge tissue, fibrin mesh with platelet aggregates. |

Each preset also defines a visual tint, flow axis and strength, and whether the immune system is active.

### Environment Features

Environments contain placed features that locally modify conditions:

| Feature | Effect |
|---|---|
| Membrane | Semi-permeable zone &mdash; 8% organelle protection, reduces local toxin stress |
| Nutrient | Local nutrient-rich zone &mdash; spawn location bias, 18% nutrient relief |
| Toxin | Local toxic zone &mdash; 55% toxin stress increase |
| Current | Directed fluid flow overlay |
| Structure | Terrain-scale SDF solid (16 shapes) with GPU raytraced rendering and CPU collision detection |

### Environment Structures

16 SDF structure shapes provide terrain-scale solid geometry in each environment. Each has a matching GPU shader SDF for rendering and a CPU-side SDF for collision detection and spawn avoidance:

| Shape | Environment | Description |
|---|---|---|
| Lung Branch | Human Lung | Branching bronchiole with cartilage rings and mucosal texture |
| Alveolar Cluster | Human Lung | Grape-like hollow alveolar sacs with capillary network |
| Pond Reed | Pond Water | Towering plant stem column with cell wall and vascular bundle texture |
| Pond Rock | Pond Water | Massive submerged boulder with erosion channels and biofilm |
| Petri Rim | Petri Dish | Glass dish wall with base disk and lid |
| Petri Agar | Petri Dish | Flat agar gel floor with streak marks and colony dimples |
| Brain Fold | Cat Brain | Cortical gyrus wall with sulcus grooves and neuronal texture |
| Brain Vessel | Cat Brain | Branching arteriole with bifurcation and terminal capillaries |
| Gut Villus | Gut Microbiome | Finger-like intestinal projection with brush border microvilli and goblet cells |
| Gut Crypt | Gut Microbiome | Tubular crypt of Lieberkuhn with Paneth cell granules |
| Blood Wall | Blood Stream | Curved vessel endothelium with glycocalyx and smooth muscle layers |
| Blood Valve | Blood Stream | Venous valve leaflet cusps with endothelial covering |
| Soil Grain | Soil Rhizosphere | Weathered mineral boulder with crystal facets and biofilm |
| Soil Root | Soil Rhizosphere | Branching plant root with lateral roots and root cap |
| Wound Fibrin | Wound Site | Criss-crossing fibrin strands with trapped platelet aggregates |
| Wound Tissue | Wound Site | Ragged wound-edge tissue with exposed collagen fibers |

## Simulation Systems

### Metabolism

Entities consume energy at a rate combining base metabolism, environmental stress, starvation, and senescence:

- **Energy drain**: `base_metabolism * dt * (1 + stress + starvation * 1.45 + senescence)`
- **Starvation**: increases when `nutrient_reserve < 0.18`, accelerating energy drain
- **Organelle health**: tracks cellular vitality (0&ndash;1 scale), degrades under infection and antibiotic pressure
- **Nutrient reserve**: replenished by feeding, depleted by metabolism and antibiotics

### ATP-Based Metabolism

Cells and bacteria maintain an ATP pool that drives cellular processes:

- **ATP production**: generated from nutrient reserves at a rate proportional to O&#8322; availability
  - Aerobic (O&#8322; &ge; 0.3): up to 36 ATP per nutrient unit (oxidative phosphorylation efficiency)
  - Anaerobic (O&#8322; &lt; 0.3): ~2 ATP per nutrient unit (glycolysis only)
- **ATP consumption**: proportional to metabolic demand (movement, defense, division)
- **Low ATP stress**: when ATP &lt; 20, organelle health degrades (mitochondrial damage)
- **ATP depletion death**: entity dies if ATP reaches 0
- **Division requirement**: cells need &ge; 35 ATP to divide, bacteria need &ge; 20 ATP

### Environmental Stresses

Entities experience stress from environmental conditions:

- **Temperature**: `max(0, |actual - preferred| - 4) * 0.035`
- **pH**: `max(0, |actual - preferred| - 0.2) * 0.7`
- **Oxygen**: `max(0, preferred - actual) * 1.1`
- **Toxicity**: `toxicity * type_sensitivity`

### Cell Division

When a cell or bacterium accumulates energy above `division_energy` (default 150) and cooldown has elapsed, it divides:

- **Duration**: 8&ndash;22 seconds depending on entity type and `mitotic_clock` gene (bacteria ~8s, cells ~18s, WBC ~22s)
- **Process**: `mitosis_progress` ramps 0&rarr;1 over the duration, visually morphing the entity
- **Completion**: parent keeps 50% energy, child spawned with 50%
- **Mutation**: 1% base chance per gene, 25% chance of morphology shift
- **Cooldown**: 10&ndash;35 seconds post-division (bacteria ~10s, cells ~25s, WBC ~35s)
- **Telomere cost**: each division shortens telomeres by `1/capacity` (capacity = 10 + gene*8 for cells, ~7 for WBC)
- **Senescence**: at telomere &le; 12%, entities enter late-life senescence with +30% damage multiplier

### Viral Infection

Viruses infect cells and bacteria through a multi-stage process:

- **Contact**: virus within `infection_radius` (default 20 units) of uninfected cell
- **Probability**: modulated by temperature alignment, immune drag, O&#8322; levels, and membrane shelter
- **Stages**: ingress (0.04&rarr;0.22), replication (0.14&rarr;0.82), burst (&ge;0.96 load or progress &ge; 1.08)
- **Viral burst**: spawns 30&ndash;120 virions at random directions, killing the host cell
- **Energy drain**: 4.4 + replication_progress * 2.2 + load * 1.35 per tick
- **Organelle damage**: 0.012 + replication_progress * 0.09 per tick
- **Mutation boost**: virions from burst have 50% higher mutation rate

### Bacterial Colonization

Bacteria can colonize cells through a slower infection pathway:
- Biofilm formation and fimbrial adhesion mechanisms
- Toxin-mediated cell entry
- Independent tracking from viral infection (dual infection possible)

### Gene Exchange

Horizontal gene transfer between bacteria in close proximity:
- Species-specific exchange groups (10&ndash;13 per bacterial type)
- Contact-based genetic mixing with blending algorithm
- Supports plasmid-like trait sharing between compatible strains

### Antibacterial Defense

Bacteria secrete antibiotic films in a quorum-sensing analog:

- **Spectrum**: 1&ndash;4 antibiotic bands per bacterium based on `antibiotic_diversity` gene
- **Band signature**: circular distance in [0,1) space from `antibiotic_type` gene
- **Potency**: `(1.8 + yield * 3.8 + diversity * 1.8) * pressure * dt`
- **Resistance**: bacteria with different `antibiotic_type` genes resist neighboring bacteria's antibiotics
- **Damage**: energy drain, nutrient reserve depletion, and organelle damage proportional to film strength

### Quorum Sensing

Bacteria accumulate autoinducer signal based on local kin density:

- **Signal accumulation**: increases when same-species bacteria are nearby, decays otherwise
- **Threshold**: gated by `quorum_threshold` gene (0.25&ndash;0.75, heritable)
- **Quorum-activated effects**:
  - Upregulated antibiotic secretion (1.6&times; potency when quorum reached)
  - Biofilm-mode ATP conservation (reduced metabolic cost)
  - Coordinated defense behavior across the colony

### Antibiotic Resistance

Bacteria develop resistance through two mechanisms:

- **Heritable resistance gene**: `resistance` gene (0&ndash;2.5) reduces antibiotic damage proportionally
- **Adaptive resistance**: bacteria that survive antibiotic exposure accumulate `resistance_level` (epigenetic)
  - Exposure-proportional: stronger antibiotic pressure &rarr; faster adaptation
  - Partial protection: resistance reduces but does not eliminate damage
  - Complement MAC damage is also partially blocked by resistance gene

### Immune Response

When enabled, the immune system spawns defenders and attacks pathogens:

- **WBC spawning**: when virus count exceeds WBC count and immune pressure > 0.4
- **Antibody spawning**: when viruses present and immune pressure > 0.4
- **WBC behavior**: seeks nearest virus/toxin, engulfs on contact (energy += 10, target dies)
- **Antibody behavior**: seeks viruses within infection radius, neutralizes on contact
- **Auto-cap**: immune spawning stops at 1200 total entities

### T Cell / B Cell Differentiation

White blood cells spawn with one of three immune subtypes:

| Subtype | Probability | Behavior |
|---|---|---|
| Generic (neutrophil) | 30% | Attacks any pathogen or toxin on contact |
| T Cell | 40% | Prioritizes infected host cells; 1.2&times; chase speed; 35% better viral clearance |
| B Cell | 30% | Detects viruses and produces antibodies (costs 8 energy + 5 ATP per antibody) |

All subtypes inherit their `immune_subtype` through division.

### Complement Cascade

Innate immune opsonization system that tags pathogens for enhanced clearance:

- **Classical pathway**: antibody-mediated &mdash; entities near antibodies receive complement tagging faster
- **Lectin pathway**: bacteria tagged 1.3&times; faster than other entity types
- **Opsonization tag**: accumulates from 0&rarr;1 on pathogen surfaces; decays slowly
- **MAC damage**: at complement tag &gt; 0.70, membrane attack complex deals direct damage
  - Resistance gene partially blocks MAC damage
- **Immune priority**: complement-tagged targets get 0.4&times; effective distance for WBC targeting (prioritized)
- **Clearance bonus**: WBC get enhanced clearance rate against tagged targets

### Phagocyte Cleanup

Phagocytes (janitor cells) manage corpse accumulation:

- **Auto-spawning**: when `corpse_count > phagocyte_count * 2` and entity count < 1200
- **Behavior**: seeks nearest corpse, removes it on contact (energy += 12)
- **Fallback**: brownian drift when no corpses available

### AI Movement

Entities use behavior-based AI steering, modulated by individual gene values:

| Entity | Seek Target | Flee Target | Max Speed |
|---|---|---|---|
| Cell | Nearest nutrient (150u) | Virus/toxin (100u) | 60 |
| Bacterium | Nearest nutrient (150u) | Virus/toxin (100u) | 80 |
| Virus | Uninfected cells (25u, weak drift) | &mdash; | 35 (passive Brownian diffusion) |
| White Blood Cell | Virus/toxin (unlimited) | &mdash; | 80 |
| Antibody | Viruses (120u) | &mdash; | 70 |
| Phagocyte | Corpses (unlimited) | &mdash; | 68 |
| Red Blood Cell | &mdash; (brownian only) | &mdash; | 40 |
| Nutrient | &mdash; (gentle brownian) | &mdash; | 15 |

Steering forces are reduced proportionally to `mitosis_progress` during division.

### Death and Corpses

Entities die from starvation (energy &le; 0), telomere exhaustion, viral lysis, or antibiotic damage. Dead entities become corpse husks that shrink over time and are cleaned up by phagocytes. When dead count exceeds 50, auto-culling removes oldest dead entities.

### Fluid Dynamics

- **Velocity damping**: `vel *= viscosity * fluid_damping` per tick
- **World boundary**: sphere wrap with elastic reflection and 0.8&times; damping
- **Environment flow**: directional current from preset `flow_axis` and `flow_strength`

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
| `world_radius` | 200.0 | 3D world bounds radius |
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

All entities are rendered via a GPU fragment shader (`biochem_rt.frag`, ~1,527 lines) using signed distance field (SDF) raymarching. Each entity type and morphological variant has a unique SDF shape:

- **Cells**: smooth blobs with nucleus and organelle interiors visible; mitosis morphs to two-sphere
- **Bacteria**: cocci (spheres), bacilli (capsules), spirals (helical); antibiotic film expands radius
- **Viruses**: icosahedral capsids, corona spike proteins, bacteriophage legs
- **Blood cells**: biconcave discs (RBC), irregular amoeboid shapes (WBC)
- **Corpses**: shrunk (0.42&ndash;0.76&times; scale) and darkened

SDF primitives: sphere, capsule, ellipsoid, octahedron, torus. Raymarching uses up to 256 steps with adaptive sizing, soft shadows, subsurface scattering for organic appearance, and ambient occlusion.

Visual encoding per entity:
- Infection swelling: `radius *= 1 + min(0.36, progress * 0.20 + load * 0.012)`
- Antibiotic cloud: `radius *= 1 + min(1.35, film * (0.85 + yield * 0.28))`
- Telomere aging darkens surface as it depletes
- Organelle health tints interior glow

### Pipeline

- Fullscreen triangle (no vertex buffer)
- Two SSBOs: sphere data (max 1,024 entities) and environment features (max 128)
- Per-frame upload of entity positions, states, and environment features
- Inverse VP matrix for ray generation from camera

### Orbit Camera

- Mouse drag to orbit around target
- Scroll to zoom
- WASD/QE to pan
- Click to select entities (F4 select mode)

## User Interface

- **Top bar**: environment name, live entity/corpse/feature counts, pause toggle, FPS
- **Settings panel**: environment selector, 18 environment sliders, AI behavior sliders, reset camera
- **Population panel**: live counts per entity type (9 types)
- **Event log**: color-coded lifecycle events (division, infection, immune, lifecycle, user, system) with auto-scroll, max 160 entries
- **Entity inspector**: full property view on selection &mdash; energy, age, genes, infection state, telomere state, lineage, stress metrics; kill/cull buttons
- **Spawn menu**: entity type selector (9 types), variant combo, energy slider, quick-spawn presets (cell colony, virus outbreak, nutrient burst, immune response, reseed)
- **Splash screen**: animated cell nucleus with orbiting organelles, dismissible
- **Pause menu**: resume, new simulation, empty simulation, return to launcher, quit
- **Bottom bar**: auto-hiding with green bio-themed styling
