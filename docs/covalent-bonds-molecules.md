# Covalent Bonds & Molecules

Atoms share valence electrons to form covalent bonds. Bond formation and breaking run CPU-side;
GPU spring forces maintain geometry.

**Formation**: nuclei within search radius (default 28px) with compatible valence and sufficient
activation energy form bonds. Up to 6 bonds per particle, max 10 new bonds per frame.

**GPU spring physics**: Hooke's law (K=80, rest=22px) with dashpot damping (0.3). Bonds break
when stretched beyond rest &times; 2.2. Rendered as pale blue glowing lines.

**Molecule detection**: Union-Find groups bonded atoms each frame. Displayed as Hill system
molecular formulas (C first, H second, rest alphabetical) with total energy, age, and ionic charge.
Chirality is detected automatically: atoms (C, Si, N, P, S) bonded to 3+ different element types
are counted as chiral centers. Chiral molecules display a purple badge in the molecule bestiary
and show center count in the detail card.

**Molecule spawn**: Type a formula in the Spawn Picker to place complete molecules with correct
geometry. ~50 templates from H&#8322; through glucose (C&#8326;H&#8321;&#8322;O&#8326;).
Quick-access buttons for common molecules (H&#8322;O, CO&#8322;, NH&#8323;, CH&#8324;, etc.).
Geometry includes tetrahedral, trigonal planar, bent, linear, and ring configurations.

All bond parameters (spring K, rest length, break factor, form radius, activation energy) are
tunable in the Nuclear Debug window.
