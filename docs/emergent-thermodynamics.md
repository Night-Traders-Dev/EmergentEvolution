# Emergent Thermodynamics

Two feedback systems measure bulk properties from particle kinetics:

- **Temperature** (Berendsen thermostat): EMA of average kinetic energy. Coupling slider blends
  between slider-controlled and fully emergent temperature.
- **B-field**: EMA of charged current magnitude (&Sigma;|q&middot;v|). Feeds back into effective
  Lorentz strength for magnetic interactions.

Both use exponential moving averages (&alpha; = 0.02).

Temperature uses the **relativistic kinetic energy** formula KE = (&gamma; &minus; 1)c&#178; rather
than the classical &frac12;v&#178;. This allows emergent temperatures to reach millions of Kelvin
when particles approach relativistic speeds (v &rarr; c). The Lorentz factor &gamma; is clamped at
1/(1 &minus; 10&#8315;&#8310;)&#189; &asymp; 1000, giving a theoretical maximum of ~9 million K.

The noise amplitude is capped at 50.0 (up from the original 2.0), allowing the thermostat to drive
particles deep into the relativistic regime when the temperature slider is cranked high.
