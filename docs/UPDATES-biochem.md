# Biochemical Simulator Changelog

All notable changes to the Biochemical Simulator.

---

## v0.3.0 - 2026-03-08

Immune system expansion and advanced biology.

### Added

- ATP-based metabolism: aerobic (36 ATP) vs anaerobic (2 ATP), O2-dependent
- Quorum sensing with autoinducer signals and threshold-gated defense
- Antibiotic resistance: heritable gene + adaptive epigenetic resistance from exposure
- Immune subtypes: T-cell (prioritizes infected hosts), B-cell (produces antibodies)
- Complement cascade: opsonization (classical/lectin pathways), MAC damage at >70%
- 2 new genome traits: resistance, quorum_threshold (18 total)
- Documentation updates

### Changed

- Gene count expanded from 16 to 18
- Immune spawning split into 40% generic, 30% T-cell, 30% B-cell

---

## v0.2.0 - 2026-03-07

Entity systems, environment presets, and UI polish.

### Added

- Multi-stage viral infection with replication gates and lysis bursts (30-120 virions)
- Bacterial antibiotic film warfare
- Phagocyte corpse cleanup
- AI-driven entity movement (seek/flee/spacing with passive Brownian diffusion for viruses)
- 8 environment presets (Human Lung, Pond Water, Petri Dish, Cat Brain, Gut Microbiome, Blood Stream, Soil Rhizosphere, Wound Site)
- 16 SDF structure shapes with CPU collision detection
- Color-coded event log (division, infection, immune, lifecycle)
- Collapsible settings sections with parameter tooltips
- Auto-hiding bottom taskbar

### Fixed

- Entity spawning and lifecycle bugs
- Spatial index performance (single rebuild per frame)

---

## v0.1.0 - 2026-03-06

Initial biochemical simulator.

### Added

- GPU SDF-raytraced rendering engine
- 9 entity types: Cell, Bacterium, Virus, Nutrient, Toxin, Antibody, RBC, WBC, Phagocyte
- 19 morphological variants (8 cell, 6 bacteria, 5 virus) with unique SDF shapes
- 16-gene heritable genome system (metabolic efficiency, telomere length, defense, sensing)
- Cell division with telomere tracking and senescence
- Immune system: WBC/antibody spawning
- WASD camera panning
- Basic settings UI
