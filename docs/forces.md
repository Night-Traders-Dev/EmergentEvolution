# Forces

All forces act simultaneously in the compute shader. Six have independent **multiplier sliders**
(0&ndash;3&times;, default 1.0 = Standard Model values) exposed in the Force Multipliers panel.

| Force | Implementation | Multiplier |
|---|---|---|
| **Electromagnetic** | Coulomb 1/r&#178; + Biot-Savart B-field + Lorentz force F=q(v&times;B) | `coulomb_strength` |
| **Strong nuclear** | Yukawa attractive (16px range, smoothstep window 12&ndash;16px) + Pauli hard-core repulsion (6px) + nuclear surface tension + velocity damping | `yukawa_strength`, `pauli_multiplier` |
| **QCD color** | Cornell potential with running coupling &alpha;_eff = &alpha;_s &middot; max(0.3, 1 + 0.3 ln r) | `alpha_s_scale` |
| **Weak** | Short-range Yukawa (0.8px) + stochastic decay (CPU) | &mdash; |
| **Gravity** | Newtonian 1/r&#178; with optional GR extensions (see below) | &mdash; |
| **Compton** | Photon radiation pressure + oscillating B-field on charges (30px) | `compton_strength` |
| **Annihilation** | Matter-antimatter attraction at contact (6px) | `annihilation_strength` |

Additional force behaviors:

- **Dark matter** &mdash; gravity only (20&times; self-gravity, 5&times; DM-normal), no EM/strong/weak
- **SIMP** &mdash; 30&times; enhanced self-gravity, clusters unlike cold DM
- **Dark energy** &mdash; universal repulsion growing with distance (cosmological constant analog)
- **Magnetic monopole** &mdash; static radial B-field (g/r&#178;), Lorentz deflection on charges
- **Tachyon** &mdash; superluminal floor (v &gt; 1.33c), rapid decay to &gamma;&gamma;
- **Higgs field** &mdash; tunable VEV (0&ndash;500), mass coupling to heavy particles
- **Hard-sphere collisions** &mdash; elastic position correction + velocity impulse (restitution 0.95)
- **Synchrotron radiation** &mdash; charged particles radiate energy proportional to q&#178;&gamma;&#178;v&#178; (subtle long-term drain)
- **Quasiparticles** (types 67&ndash;73) &mdash; GPU fast-path: skip nuclear/EM/strong forces, weak gravity only (0.5&times;), 0.92&times; velocity damping, Brownian thermal kicks, accelerated energy decay. CPU effects: plasmon oscillating field, phonon lattice kicks, magnon Lorentz deflection, polaron ion drag, Cooper pair superfluidity boost, roton tangential vortex
- **Mesons** (types 74&ndash;261) &mdash; GPU fast-path: color-neutral bound states skip QCD Cornell, Pauli, and nuclear Yukawa. Charged mesons get full Coulomb; all mesons get residual strong interaction with nucleons (~20px Yukawa, 5&times; weaker), gravity, hard-sphere repulsion, 0.98&times; velocity damping, and genome-driven energy decay. CPU: PDG branching ratio decays via `check_meson_decays()`

## General Relativity Extensions

Three GR corrections toggled from **Menu > Visualization** (all enabled by default). Zero additional
push-constant bytes (encoded in `field_flags` bits 9&ndash;11).

| Extension | Physics |
|---|---|
| **Mass-Energy Gravity** | E=mc&#178;: gravitational mass = rest mass &times; &gamma;. Fast particles attract more strongly. |
| **Frame Dragging** | Gravitomagnetic Lense-Thirring analog: spinning masses drag nearby movers tangentially (~1% of Newtonian). |
| **Gravitational Waves** | Finite-speed gravity (retarded-time correction). Force direction lags behind fast-moving sources. |

When gravitational waves are enabled, accelerating massive particles emit **GW ripple rings** that
propagate at c across the entire simulation (1/r amplitude falloff). These rings are not just visual
&mdash; they exert physical **tidal forces** on particles they pass through:

- **Radial stretch**: outward kick along the source&rarr;particle axis
- **Tangential compression**: perpendicular squeeze (quadrupole "+" polarization)
- Amplitude falls off as 1/r, matching real GW strain decay
- Massless particles (photons, gravitons, gluons, neutrinos) are unaffected
- Wavefront shell is 30px thick &mdash; force is a transient pulse, not constant

## Force Objects

Nine placeable force objects (**Menu > Tools > Force Objects**) with per-object strength and radius
sliders:

| Type | Formula | Notes |
|---|---|---|
| **EM Field** | Lorentz F=q(v&times;B) | Curves charged particles, no speed loss |
| **Strong Nuclear** | Yukawa exp(-r/5)/r | Short-range attraction on baryons |
| **Weak** | Yukawa exp(-r/5)/r&#178; | Short-range boost |
| **Gravity Well** | F=S&middot;m&middot;200/(r&#178;+&epsilon;) | 1/r&#178; attraction; adaptive softening &epsilon; shrinks from 25 to 1 as strength rises above 100 |
| **Heat Source** | Thermal noise boost | Random velocity kicks in radius |
| **Mirror** | Elastic line segment | Swept collision with configurable elasticity |
| **Coulomb** | K&middot;q/r&#178; | Pure electrostatic point charge |
| **Vortex** | F&perp; &prop; m/r | Cyclotron-like tangential force |
| **Potential Well** | F=-k&middot;r | Harmonic trap (linear restoring force) |

**Strength slider**: logarithmic, 0.1&ndash;1000. Tooltip guide: 0.1&ndash;10 normal,
10&ndash;100 neutron star, 100&ndash;1000 black hole. At black hole strength (&gt;100), the gravity
well softening drops toward 1px, approaching a true 1/r&#178; singularity where particles within
~20px accelerate to c in under a second.
