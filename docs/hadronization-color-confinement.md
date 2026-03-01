# Hadronization & Color Confinement

CPU-side confinement prevents free quarks from existing outside QGP conditions. Newly created
quark-antiquark pairs are **immediately bound as mesons** &mdash; free quarks never persist.
Runs each tick in five phases:

| Phase | Description | Rate limit |
|---|---|---|
| **Free quark detection** | Quarks/antiquarks without a partner within 45px are flagged free | &mdash; |
| **Meson formation** | Free quark + free antiquark with complementary color bind into mesons | 24/frame |
| **Baryon condensation** | Below Hagedorn temperature (1.7&times;10&#185;&#178; K), RGB triplets condense into protons or neutrons | 16/frame |
| **Vacuum instability** | Free quarks with E &ge; 0.3 generate a partner from vacuum &mdash; partner spawns close (2px) and is **immediately entangled** as a meson | 2/frame |
| **String breaking** | Bound pairs stretched beyond 55px break the color flux tube (Lund model) &mdash; produces **2 mesons**, each pairing one original quark with one new quark | 2/frame |

A free quark population cap (24) suppresses pair creation when confinement is already behind,
allowing meson formation and baryon condensation to catch up.

**QGP exception**: above 2&times;10&#185;&#178; K, quarks are deconfined and hadronization is
suppressed. The QGP environment preset disables it automatically.

Toggle in **Settings > Strong Nuclear > Hadronization**.
