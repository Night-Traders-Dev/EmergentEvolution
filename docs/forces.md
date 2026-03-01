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
