# UI & Visualization

## Info Cards

Clicking a particle shows its type, charge, spin, mass, age, momentum, temperature, relativistic
energy (eV, PDG rest masses, E = &gamma;m&#8320;c&#178;), and intrinsic magnetic moment
(anomalous moments for nucleons, Dirac g=2 for leptons). If part of a nucleus, a clickable link
opens the **Element Detail Card** with full composition, stability info, magnetic moment,
and Move/Delete/Duplicate/Export actions. Molecules open a **Molecule Detail Card** with Hill
formula, bond count, and clickable constituent atoms.

## Particle & Element Lists

Bottom bar shows simulation state, timescale, temperature, B-field, FPS, energy, and entropy.
Clickable counters (Events, Particles, Atoms/Molecules) open scrollable windows. The **Menu**
popup organizes commands into Simulation, File, View, Visualization, Measurement, and Tools
sections. Particle and element lists are accessible from **Menu > View**. The event log tracks
up to 10,000 entries across 13 categories with timestamps (configurable limit and disk logging
in Settings).

## Visualization Overlays

Overlays toggled from **Menu > Visualization**:

| Overlay | Description |
|---|---|
| **Trails** | GPU-side particle path fade |
| **Electron Cloud** | Bohr-model shell rings with fill indicators and element labels |
| **Orbit Paths** | Predicted Keplerian ellipses for bound electrons, computed from angular momentum, energy, and Runge-Lenz vector |
| **Magnetic Field** | B-field heatmap from Biot-Savart + nucleon dipoles (32&times;18 grid, OpenMP) |
| **Wave Mode** | de Broglie wave packets (&lambda; = h/p) with Gaussian envelope |
| **Atom Grid** | Hydrogen-diameter grid (2 &times; Bohr radius = 0.106 nm per cell) |
| **Trajectory Tracer** | Last 120 positions per particle as fading polylines |
| **Energy Heatmap** | 32&times;18 KE density grid (blue to red) |
| **Velocity Field** | Arrow grid showing average velocity per cell |
| **Force Vectors** | Coulomb/Yukawa/Gravity breakdown on selected particle |
| **GW Ripples** | Expanding gravitational wave rings from accelerating masses (1/r amplitude, gold&rarr;violet) |
| **Gravity Map** | Gravitational mass density heatmap (supports relativistic mass when E=mc&#178; enabled) |

## Field Visualization

Five quantum field overlays: electromagnetic (red/blue), strong nuclear (cyan/green), weak (purple),
gravity (grey), Higgs (gold).

## Measurement Tools

Four instruments from **Menu > Measurement**: thermometer probe (local KE average in radius),
velocity meter (tracks single particle), distance ruler (nanometer scale), density counter
(particles per area). Up to 8 probes/counters with adjustable radii.

## Tools

- **Force Objects**: EM field (proper Lorentz F=q(v&times;B), curves charged particles without speed loss),
  strong nuclear, weak, gravity well, heat source
- **Particle Accelerator**: fire projectiles at a target (single, triple, stream modes)
- **Mirror**: reflective line segments with configurable elasticity (GPU-side reflection)
- **Nuclear Debug**: tune reaction thresholds and rates in real time
- **Halt Velocities / Remove Massless / Remove Massive**: utility actions

## Visual Quality

- **Half-resolution bloom**: brightness extract (2&times;2 downsample) &rarr; horizontal Gaussian blur &rarr; vertical Gaussian blur &rarr; full-res composite (nearest-neighbor upscale). Runs at half render resolution for performance. Off by default; togglable in Display settings.
- **Rim lighting**: particles have bright edge highlights for depth illusion
- **Sub-pixel anti-aliasing**: adaptive AA band width (`max(1px, 15% radius)`) ensures smooth edges at all zoom levels
- **Zoom-adaptive sizing**: `mix(zoom, sqrt(zoom), 0.5)` &mdash; particles stay visible when zoomed out, don't overlap when zoomed in
- **Camera shake**: fusion, fission, and annihilation events trigger exponentially decaying camera shake (8&ndash;12px intensity)
- **Wobbly windows**: subtle sinusoidal floating animation on UI panels (2px amplitude, togglable)

## Custom Particle Textures

Each of the 67 particle types can be rendered with a custom PNG texture instead of (or blended
with) the default procedural shader rendering. Textures are stored as a Vulkan `sampler2DArray`
(128&times;128 per layer, RGBA8, ~4.5 MB total VRAM).

**Render modes** (per-type, switchable at runtime via Settings &rarr; Particle Textures):

| Mode | Description |
|---|---|
| **Procedural** | Default compute shader rendering (circles, glow, effects) |
| **Textured** | Custom PNG texture only, energy-modulated brightness |
| **Blended** | Texture blended with procedural lighting (adjustable blend factor) |

**Included textures** &mdash; 67 procedurally generated PNGs in `assets/particles/`, each with a
unique visual style:

| Category | Style |
|---|---|
| Proton, Neutron, Antiproton | 3D sphere with visible quark color spots (uud/udd) |
| Electron, Muon, Tau | Solid sphere with specular highlight and rim lighting |
| Positron, Anti-quarks, Anti-leptons | Sphere with distinctive antimatter halo ring |
| Neutrinos | Translucent wispy ghost effect (FBM noise) |
| Quarks (u, d, s, c, t, b) | Small sphere with color-charge glow ring |
| Photon, Gluon, Graviton | Starburst with radial rays and white-hot core |
| W&plusmn;, Z&deg; | Concentric wave rings with angular modulation |
| Higgs, Inflaton | Golden sphere with pulsing concentric field rings |
| Dark Matter, Dark Energy | Turbulent halo with fractal Brownian motion noise |
| SUSY sparticles | Wobbled-edge sphere with shimmer (tilde distortion) |
| Exotic / BSM | Fractal-edge turbulent sphere with FBM noise |
| Monopole | Sphere with 12 radial magnetic field lines |
| Tachyon | Elongated speed-streaked glow (motion blur) |

**Custom textures**: drop any 128&times;128 (or larger) RGBA PNG into `assets/particles/` using
the naming convention from `type_to_filename()` (lowercase, spaces &rarr; `_`,
`+` &rarr; `_plus`, `-` &rarr; `_minus`). Example: `dark_matter.png`, `w_plus.png`.
Hot-reload supported at runtime.

**Texture generator**: `tools/gen_particle_textures.cpp` &mdash; standalone C++ program that
regenerates all 67 textures procedurally using value noise, FBM, and per-type rendering styles.

```bash
g++ -O2 -std=c++17 -o build/gen_textures tools/gen_particle_textures.cpp -lm
./build/gen_textures   # outputs to assets/particles/
```

## Splash Screen

Four random splash screen variants are selected at startup: **atom**, **blue orb**, **nebula**,
and **collider**. Each plays a short animated sequence before fading into the simulation. Any
key or mouse click dismisses the splash immediately.

## Spawn Picker (F3)

Categorized spawning: leptons, quarks, bosons, hypothetical particles, composite atoms
(H through Fe as complete atoms with force-relaxed nuclei and Bohr-model electron shells), and
molecules by formula. Downloaded repository molecules appear with a `[repo]` tag. Nucleon
positions are computed via iterative force relaxation matching GPU shader constants. Configurable
count, energy, and scatter radius.

## Experiment Presets

Quick-apply buttons in the Environment settings panel:

| Preset | Temperature | Description |
|---|---|---|
| Cold Lab | 10 K | Cold vacuum with low dampening |
| Hot Plasma | 10 MK | Extreme thermal energy |
| Nuclear Fuel | 100 MK | Fusion-ready proton gas |
| Antimatter | 10 K | Matter + antimatter mix with virtual pairs |
| Dark Universe | 100 K | Dark matter + dark energy dominated |

## Themes

14 built-in color themes (Dark Navy, Midnight, Slate, Ember, Synthwave, Forest, Arctic, Solar,
High Contrast, Solarized Dark, Dracula, Monokai, Universe Sandbox, Ubuntu Yaru) plus custom
theme import via `.pptheme` files in the `themes/` directory. Settings are organized into five
tabs: Display, Performance, Theme, Accessibility, and Audio & Log. User preferences persist
across sessions in platform-appropriate locations (`~/.local/share/particle_playground/` on
Linux, `%APPDATA%\ParticlePlayground\` on Windows).

## Display Settings

- **Quality presets**: Low, Medium, High, Ultra (auto-sets render scale, bloom, physics quality)
- **VSync**: toggle between FIFO (vsync on) and MAILBOX (vsync off) present modes
- **Multi-monitor**: select which display to use for fullscreen (when multiple monitors detected)
- **GPU selection**: choose which Vulkan-capable GPU to use, with VRAM display

## Accessibility

- **Colorblind modes**: Protanopia, Deuteranopia, Tritanopia (Daltonize-style color correction)
- **High contrast**: brighter text, stronger borders for improved readability
- **Reduced motion**: disables wobbly windows, splash animations, and GW ripple effects
- **Mouse sensitivity**: adjustable camera pan speed (0.1&ndash;3.0&times;)

## Achievements

64 milestones across 6 categories (Nuclear Physics, Element Creation, Particle Zoo,
Thermodynamics, Milestones, Chemistry). Persist via `.ppach` file. Steam achievement
integration ready (optional, builds without Steamworks SDK).

## Sound Effects

Ten procedurally-generated one-shot sound effects (achievement, spawn, decay, fusion, fission,
click, annihilation, bond, collision, photon) with per-channel cooldown to prevent audio spam.
Independent SFX volume and mute controls. Background music loops via miniaudio. All WAV files
are synthesized by `tools/gen_sound_effects.cpp` (16-bit PCM mono, 44.1 kHz).

## Gamepad Support

GLFW gamepad polling with standard mapping:

| Input | Action |
|---|---|
| Left stick | Camera pan |
| Triggers | Zoom in / out |
| Start | Pause menu |
| A | Play / pause |
| B | Back / escape |
| Bumpers | Cycle settings tabs |

## Repository Browser

The online repository browser (**Menu > Tools > Repository**) connects to a remote GitHub-hosted
collection of `.ppel` and `.ppmol` files. Features:

- **Search** bar for filtering entries by name or formula
- **Elements / Molecules** tabs
- **Filter** buttons: All, Cached, New
- **Download / Import** workflow: select an entry, preview its metadata, download to local cache,
  then import directly into the simulation

See [docs/online-repository.md](online-repository.md) for protocol and caching details.

## Window Management

All ImGui windows (info cards, settings panels, repository browser, event log, etc.) dynamically
clamp their position to screen edges so they never open partially or fully off-screen. This
applies on first open, after resolution changes, and when restoring saved window positions.

## Error Dialogs

Fatal errors (Vulkan init failure, no GPU, device lost) show native OS message boxes
(MessageBox on Windows, zenity/kdialog/xmessage on Linux) instead of silent stderr output.
