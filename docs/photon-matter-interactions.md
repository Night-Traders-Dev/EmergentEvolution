# Photon-Matter Interactions

High-energy photons interact with matter through GPU and CPU channels. Max 8 per frame.

**GPU (Compton scattering)**: photon deflects toward nearest charge within 30px, losing energy.
Radiation pressure pushes matter along photon direction. Oscillating B-field exerts Lorentz force.

**CPU processes**:

| Process | Threshold | Effect |
|---|---|---|
| Photoelectric effect | E&gamma; &ge; 1.5 &times; binding | Photon absorbed, electron ionized |
| Compton (bound) | E&gamma; &ge; 0.6 &times; binding | 40% energy transfer, shell promotion or ionization |
| Free electron scatter | E&gamma; &ge; 0.3 | 30% energy transfer |
| Nuclear Compton | E&gamma; &ge; 0.25 | 8% energy transfer to nucleon |

Shell-dependent binding scales with &radic;Z. Inner shells require more energy to ionize.
