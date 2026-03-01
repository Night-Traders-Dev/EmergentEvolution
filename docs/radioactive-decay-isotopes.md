# Radioactive Decay & Isotopes

## Particle Decay

Particles below 0.08 energy undergo probabilistic decay based on per-type rates.

<details>
<summary><b>Decay channels</b></summary>

| Parent | Rate | Products |
|---|---|---|
| Top t | 0.50 | Bottom + real W+ (only quark heavy enough) |
| W+/W- | 0.50 | Lepton + Neutrino (on-shell W decay) |
| Z0 | 0.50 | e&#8315; + e&#8314; |
| Higgs H0 | 0.40 | 2&gamma; |
| Tau &tau; | 0.20 | e&#8315; + &nu;&tau; |
| Bottom b | 0.10 | Daughter + f f&#773; (3-body via virtual W&#8315;*, CKM weighted) |
| Charm c | 0.12 | Daughter + f f&#773; (3-body via virtual W&#8314;*, CKM weighted) |
| Strange s | 0.02 | Up + f f&#773; (3-body via virtual W&#8315;*, Q &asymp; 91 MeV) |
| Muon &mu; | 0.01 | e&#8315; + &nu;&mu; + &nu;e |

</details>

Matter-antimatter annihilation runs every frame at 5px contact radius: e&#8315;+e&#8314;,
p+p&#773;, &mu;+&mu;, &tau;+&tau;, and quark-antiquark pairs annihilate to photons.

## Isotope Half-Lives

Nuclei are identified via BFS clustering of protons and neutrons. Realistic half-life decay
is applied based on (Z, N) composition.

<details>
<summary><b>Decay modes & key isotopes</b></summary>

**Modes**: alpha (&alpha;), beta-minus (&beta;&#8315;), beta-plus (&beta;&#8314;),
neutron emission, proton emission.

| Isotope | Z | N | Mode | Sim Half-Life | Real Half-Life |
|---|---|---|---|---|---|
| Free neutron | 0 | 1 | &beta;&#8315; | 10 s | 10 min |
| Tritium H-3 | 1 | 2 | &beta;&#8315; | 1 min | 12.3 yr |
| He-5 | 2 | 3 | n-emit | instant | 7&times;10&#8315;&#178;&#178; s |
| Be-8 | 4 | 4 | &alpha; | instant | 6.7&times;10&#8315;&#185;&#8311; s |
| C-14 | 6 | 8 | &beta;&#8315; | 5 min | 5730 yr |
| U-235 | 92 | 143 | &alpha; | 3.3 min | 704 Myr |
| U-238 | 92 | 146 | &alpha; | 5 min | 4.5 Gyr |

~50 isotopes in the explicit table. Nuclei not listed fall through to general stability rules:
Z &gt; 83 alpha-decays, N/Z &gt; 1.5 beta-minus, N/Z &lt; 0.7 beta-plus, A = 5 or 8 instant
disintegration.

</details>
