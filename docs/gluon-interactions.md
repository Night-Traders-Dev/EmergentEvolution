# Gluon Interactions

Gluons carry SU(3) color-anticolor pairs (8 octet states).

**GPU** (compute shader):
- Cornell potential with running coupling + linear confinement
- Casimir enhancement: gluon-gluon receives 2.25&times; force (C_A/C_F)
- Trilinear vertex: gluon-gluon attraction when anticolor matches color
- Quark coupling: absorption (&minus;1.2) and emission (&minus;0.8) vertices
- Effective mass: E/c&#178; relativistic inertia (asymptotic freedom at high energy)

**CPU** (hadronization phase 5, max 4 events/frame):

| Interaction | Condition |
|---|---|
| **Color-aware absorption** | Gluon within 12px of quark with matching color vertex &mdash; quark color rotates |
| **Color-conserving merge** (g+g &rarr; g) | Two gluons within 12px with compatible colors (trilinear vertex) |
| **Confinement splitting** (g &rarr; q+q&#773;) | Gluon with E &ge; 0.2, no colored particle within 40px &mdash; products converted to a specific PDG meson type via `quark_pair_to_meson()` |
