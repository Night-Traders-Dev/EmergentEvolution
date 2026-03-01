# Emergent Thermodynamics

Two feedback systems measure bulk properties from particle kinetics:

- **Temperature** (Berendsen thermostat): EMA of average kinetic energy. Coupling slider blends
  between slider-controlled and fully emergent temperature.
- **B-field**: EMA of charged current magnitude (&Sigma;|q&middot;v|). Feeds back into effective
  Lorentz strength for magnetic interactions.

Both use exponential moving averages (&alpha; = 0.02).
