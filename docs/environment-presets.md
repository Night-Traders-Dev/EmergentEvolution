# Environment Presets

Fourteen presets spanning vacuum to the Big Bang, selectable from the **Environment** dropdown.

<details>
<summary><b>All presets</b></summary>

| # | Environment | Temperature | Description |
|---|---|---|---|
| 0 | Lab Mode | 1 K | Empty vacuum, manual spawning |
| 1 | Hydrogen Plasma | 1.5&times;10&#8311; K | Ionized hydrogen, fusion conditions |
| 2 | Neutron Star | 10&#8313; K | Ultra-dense neutron matter |
| 3 | Solar Core | 1.5&times;10&#8311; K | Hydrogen + gravity + auto-placed potential well |
| 4 | Particle Soup | 5&times;10&#179; K | Mixed light particles |
| 5 | Alpha Emitter | 300 K | Heavy nuclei at room temp |
| 6 | Heavy Nucleus | 100 K | Cold dense nuclear matter |
| 7 | Quark-Gluon Plasma | 2&times;10&#185;&#178; K | Deconfined quarks, hadronization off |
| 8 | Electroweak Era | 10&#185;&#8309; K | W/Z/Higgs above symmetry breaking |
| 9 | Meson Factory | 5&times;10&#185;&#185; K | Spawns diverse PDG meson types (&pi;, K, &rho;, &omega;, &eta;, &phi;, D, B, J/&psi;, &Upsilon;) with real decay channels |
| 10 | Particle Accelerator | 10&#8312; K | High-energy protons + synchrotron |
| 11 | Dark Sector | 10&#179; K | 40% DM, 30% p, 15% e, 10% DE, 5% graviton |
| 12 | SUSY Sector | 10&#179; K | Neutralino/selectron/smuon/squark/gluino mix |
| 13 | Big Bang | 2&times;10&#185;&#8309; K | Singularity-point quark-gluon plasma with Hubble expansion |

</details>

The **Solar Core** preset auto-places a **potential well** (harmonic trap, F = &minus;kr) at the
world center to simulate the gravitational pressure of the overlying stellar envelope. Inside a
uniform-density sphere, gravitational acceleration is linear with radius &mdash; the harmonic
restoring force models this confinement, preventing particles from dispersing and enabling
sustained fusion at high temperatures.

The **Particle Accelerator** preset auto-places 8 **EM bending magnets** in a ring around the
center (strength 3.0, radius 120px) to guide charged particles in circular orbits.

The **Big Bang** preset models the quark epoch (~10&#8315;&#185;&#178; to 10&#8315;&#8310; s).
All particles spawn from a tight central point (&sigma; = 3% of screen) with radial outward
velocities (Hubble-like expansion). Particle mix: 40% quarks (u/d + antiquarks), 15% gluons,
10% photons, 15% leptons (all 3 generations), 8% W/Z bosons, 2% Higgs, 10% BSM (graviton,
dark matter, dark energy). Hadronization is disabled (quarks are deconfined). As the system cools
via dampening, quarks confine into hadrons and structure forms under gravity.
