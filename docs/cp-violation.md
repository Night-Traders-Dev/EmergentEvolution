# CP Violation

CP violation &mdash; the asymmetry between matter and antimatter &mdash; is the mechanism that
explains why the universe contains more matter than antimatter. The simulation implements three
aspects of CP violation in the neutral meson sector.

## Neutral Meson Oscillation

Three neutral meson pairs undergo quantum flavor oscillation (particle &harr; antiparticle mixing):

| Pair | Oscillation Rate (&Delta;m) | Width Ratio (&Delta;&Gamma;/&Gamma;) |
|---|---|---|
| K&deg; &harr; K&#772;&deg; | 0.03 | 2.0 (large K_S/K_L splitting) |
| B&deg; &harr; B&#772;&deg; | 0.05 | 0.001 (negligible) |
| B_s&deg; &harr; B&#772;_s&deg; | 0.10 (fastest) | 0.13 (modest) |

**Oscillation probability**: P(M &rarr; M&#772;) = &frac12;(1 &minus; cos(&Delta;m &middot; t))
&middot; exp(&minus;&Delta;&Gamma; &middot; t/2), clamped to [0, 0.5]. The cosine term gives
oscillatory mixing; the exponential damps as the short-lived mass eigenstate decays away. Birth
frame is preserved across oscillations to maintain coherent timing.

Implemented in `src/physics/cp_violation.cpp`, called from `tick()` before meson decay processing.

## Direct CP Violation

Branching ratio asymmetry in meson decays &mdash; matter and antimatter versions of the same meson
decay at slightly different rates into the same final states:

| System | CP Parameter | Magnitude |
|---|---|---|
| Kaon | &epsilon;&prime; (direct) | 0.05 (exaggerated from 1.66&times;10&sup3;) |
| B&deg; | sin(2&beta;) | 0.699 |
| B_s&deg; | sin(2&beta;_s) | 0.036 |

Applied as a shift to the branching ratio roll in `src/physics/meson_decays.cpp`.

## Tracking & Achievements

Session counters track meson oscillations, CP violations, and matter/antimatter excess.
Three achievements:

| Achievement | Condition |
|---|---|
| **Quantum Mixing** | Observe a neutral meson oscillation |
| **Broken Symmetry** | Observe CP violation in a meson decay |
| **Why We Exist** | Accumulate matter-antimatter asymmetry from CP violation |

Lifetime statistics (persisted in `.ppstats`): total meson oscillations, total CP violations,
lifetime matter asymmetry.

CP violation events appear in the decay log under the **CP Viol** category (violet).
Togglable via `cp_violation_enabled` in SimConfig.
