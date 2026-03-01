# Nuclear Reactions

## Fusion

Triggers when particles have sufficient kinetic energy to overcome the Coulomb barrier
(Gamow tunneling probability). All parameters exposed in the UI.

| Reaction | Products |
|---|---|
| **p + p** (pp chain) | p + n + e&#8314; + &nu;e |
| **p + n** (deuteron) | Bound p-n pair |
| **d + d** (implicit) | He-4 nucleus |

Configurable: Coulomb barrier (default 550 keV), fusion radius, max/frame, binding energy,
leptonic Q-value, product separation.

## Fission

Fast neutrons striking heavy nuclei trigger fission. The **Bohr-Wheeler fissility parameter**
(Z&#178;/A) gates which nuclei can fission &mdash; only nuclei above the threshold (default 35.0)
are unstable. U-235 (Z&#178;/A = 36.0) fissions; Carbon-12, Oxygen-16, and Iron-56 do not.

Products: cluster splits in half with energy kick + free neutrons (chain reaction fuel).
Emitted neutrons can trigger further fissions in dense nuclear matter.

Configurable: neutron threshold, min cluster size, fissility threshold, barrier energy,
fragment energy, free neutron count.
