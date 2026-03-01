# Orbital Mechanics

Electrons orbit nuclei via a quantum-mechanical centrifugal barrier (always active) that models the
Heisenberg uncertainty principle &mdash; confining an electron increases its kinetic energy, preventing
collapse into the nucleus. An optional **Orbital Drive** adds tangential velocity drive and boost.

**GPU** (physics.comp):
- **Centrifugal barrier** (always active): F = L&#178;&#8901;m_inv / (r&#179; + 1) &mdash; quantum ground state analog, prevents electron collapse
- **Orbital Drive** (toggle, Menu &gt; Visualization): tangential velocity drive, per-shell boost with compensating F_bind, asymmetric radial damping
- Spin-orbit coupling: fine structure correction proportional to spin &middot; L / r&#8308;
- Spin magnetic moment: dipole force &mu;&middot;&nabla;B in external field

**CPU** (update_orbitals):
- BFS clusters nucleons into nuclei (16px radius)
- Assigns electrons to nearest nucleus within 60px binding radius
- Fills shells: **1s** (2), **2s2p** (8), **3s3p3d** (18), **4s4p4d4f** (32)
- Computes L_ground per shell via Bohr model with Slater screening:
  R_target = n&#178; &middot; R_BOHR / Z_eff, where Z_eff = Z &minus; inner_electrons

**Atom spawning** (spawning.cpp):
- Nucleons placed via **force-relaxation**: hex-packed initial positions are iteratively adjusted until Yukawa, Pauli, and Coulomb forces balance to near-zero (up to 80 iterations, matches shader constants exactly)
- Electrons placed at Bohr orbital radii with velocity matching the centrifugal barrier equilibrium (Orbital Drive OFF) or boosted target velocity (Orbital Drive ON)
