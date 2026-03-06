Organelles in the shader are still rendered procedurally, but this folder now holds the reference asset set for lifecycle staging.

Stage mapping:
- `Young`: high reserve, low starvation, strong organelle health
- `Mature`: stable reserve and membrane integrity
- `Senescent`: shortened telomeres, high division wear, organelle collapse
- `Dead Husk`: corpse state after starvation or terminal senescence

Assets:
- `cell_nucleus_lifecycle.svg`
- `cell_mitochondrion_lifecycle.svg`
- `cell_golgi_lifecycle.svg`
- `cell_vesicle_lifecycle.svg`
- `cell_er_lifecycle.svg`
- `cell_ribosome_lifecycle.svg`
- `cell_er_ribosome_lifecycle.svg`
- `bacteria_nucleoid_lifecycle.svg`
- `bacteria_plasmid_lifecycle.svg`
- `bacteria_granule_lifecycle.svg`
- `bacteria_ribosome_lifecycle.svg`
- `cell_dead_husk.svg`
- `bacteria_dead_husk.svg`

The runtime shader uses the same stage progression through `organelle_health`, `telomere_state`, `division_count`, and `corpse`.
