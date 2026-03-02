# Carrier Exchange

Force-carrier mediation between interacting particles. When enabled (**Carrier Mode** toggle,
on by default), the simulation spawns short-lived virtual carriers to visualize force interactions.

## Carrier Types

| Carrier | Type | Force Mediated | Lifetime |
|---|---|---|---|
| **Photon** | 3 | Electromagnetic | ~20 frames |
| **Gluon** | 25 | Strong (QCD color) | ~15 frames |
| **W+** | 26 | Weak (charged current) | ~8 frames |
| **W-** | 27 | Weak (charged current) | ~8 frames |
| **Z0** | 28 | Weak (neutral current) | ~8 frames |
| **Graviton** | 30 | Gravity | ~25 frames |
| **Higgs** | 29 | Mass coupling | ~6 frames |

## Behavior

- Carriers spawn between interacting particle pairs at rates proportional to interaction strength
- Each carrier travels from source to target, delivering a visual trace of the force exchange
- Carriers are marked with `BEHAVIOR_VIRTUAL` and high `genome[3]` decay rate
- Maximum 3 carrier spawns per tick to prevent particle budget overflow
- Carriers do not themselves exert forces &mdash; they are visualization aids

## Toggle

**Menu > Carrier Mode** (default: ON). When disabled, `check_carrier_exchange()` is skipped
entirely and no carrier particles are spawned.
