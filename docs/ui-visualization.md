# UI & Visualization

## Info Cards

Clicking a particle shows its type, charge, spin, mass, age, momentum, temperature, relativistic
energy (eV, PDG rest masses, E = &gamma;m&#8320;c&#178;), intrinsic magnetic moment
(anomalous moments for nucleons, Dirac g=2 for leptons), and helicity (left/right-handed for
spin-1/2 fermions). If part of a nucleus, a clickable link opens the **Element Detail Card**
with full composition, stability info, magnetic moment, and Move/Delete/Duplicate/Export actions.
Molecules open a **Molecule Detail Card** with Hill formula, bond count, chirality status
(chiral center count or achiral), and clickable constituent atoms.

## Top Stats Bar

The top bar (42px) displays simulation state, timescale slider with preset buttons
(0.25&times;&ndash;4&times;), temperature, B-field, FPS, energy, entropy trend, and clickable
counters (Events, Particles, Atoms/Molecules) that open scrollable windows. Tool status
indicators show the active mode ([SELECT], [THERMOMETER], etc.).

## Bottom Taskbar

The bottom bar functions as a window taskbar/dock:

- **Menu button** (left) opens the popup organized into Simulation, File, View, Visualization,
  Measurement, and Tools sections
- **Open windows** appear as taskbar entries (e.g., Spawn, Settings, Elements, Particles)
- **Click** an entry to minimize/restore the window
- **Right-click** an entry to close the window entirely
- Active windows have a bright background with blue underline; minimized windows are dimmed

The taskbar auto-hides when the mouse is away from the bottom edge.

## Particle & Element Lists

Particle and element lists are accessible from **Menu > View**. The event log tracks
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

Six quantum field overlays: electromagnetic (red/blue), strong nuclear (cyan/green), weak (purple),
gravity (grey), Higgs (gold), dark energy (crimson). Each renders via a per-pixel gather shader
using the GPU spatial grid for O(k) neighbor lookup.

**Quality levels** (combo box, visible when any field is enabled):

| Level | Resolution | Performance | Notes |
|---|---|---|---|
| **Low** | 640&times;360 (1/4) | ~16&times; faster | Blocky 4&times;4 pixel blocks |
| **Medium** | 1280&times;720 (1/2) | ~4&times; faster | Mild 2&times;2 blockiness |
| **High** | 2560&times;1440 (full) | Default | Per-pixel accuracy |
| **Ultra** | 2560&times;1440 (full) | Most expensive | 2&times; sampling radius (120px) |

**Brightness** slider (0.05&ndash;2.0) controls overall field intensity.

## Measurement Tools

Four instruments from **Menu > Measurement**: thermometer probe (local KE average in radius),
velocity meter (tracks single particle), distance ruler (nanometer scale), density counter
(particles per area). Up to 8 probes/counters with adjustable radii.

## Tools

- **Force Objects**: EM field (proper Lorentz F=q(v&times;B), curves charged particles without speed loss),
  strong nuclear, weak, gravity well (logarithmic strength 0.1&ndash;1000, black hole regime at 100+),
  heat source, Coulomb point charge, vortex cyclotron, potential well (harmonic trap)
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

Each particle type can be rendered with a custom PNG texture instead of (or blended with) the
default procedural shader rendering. 67 base particle types (0&ndash;66) ship with custom textures;
mesons (types 74&ndash;261) and quasiparticles (67&ndash;73) use procedural rendering by default.
Textures are stored as a Vulkan `sampler2DArray` (128&times;128 per layer, RGBA8).

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

## Splash Screens

Each application has a themed animated splash screen that displays on startup and is dismissed
by any key or mouse click (after a 0.3-second delay to prevent accidental dismissal).

| App | Theme | Visual |
| --- | --- | --- |
| **Particle Physics** | Four random variants: atom, blue orb, nebula, collider | Animated sequence with fade transition |
| **Cosmic Sandbox** | Golden cosmic theme | Central star glow with orbiting planet dots, "Cosmic Sandbox" title |
| **Biochemical Simulator** | Green biological theme | Central cell glow with membrane ring and orbiting organelles |

All splash screens share a common pattern: animated particle background, glowing title text with
shadow layers, subtitle badge, and pulsing "Press any key" hint.

## Pause Menus

Pressing `Escape` opens a fullscreen pause menu overlay in each application. All pause menus
share a consistent layout: semi-transparent background, centered button column (Resume, New
Simulation, Quit), and a "Press Escape to resume" hint. The Quit button is tinted red.

| App | Theme Color |
| --- | --- |
| **Particle Physics** | Blue / cyan |
| **Cosmic Sandbox** | Gold / orange |
| **Biochemical Simulator** | Green |

## Spawn Picker (F3) — Particle Physics

Categorized spawning: leptons, quarks, bosons, hypothetical particles, composite atoms
(H through Fe as complete atoms with force-relaxed nuclei and Bohr-model electron shells), and
molecules by formula. Downloaded repository molecules appear with a `[repo]` tag. Nucleon
positions are computed via iterative force relaxation matching GPU shader constants. Configurable
count, energy, and scatter radius.

## Spawn Menu — Cosmic Sandbox

Collapsible panel with 7 celestial body types (Star, Planet, Gas Giant, Moon, Asteroid, Comet,
Black Hole) displayed as color-coded buttons. Features a logarithmic mass slider, orbital
velocity checkbox (spawns with circular orbit velocity), and quick presets (Solar System, Binary
Stars, Asteroid Belt).

## Spawn Menu — Biochemical Simulator

Collapsible panel with 8 biological entity types (Cell, Bacterium, Virus, Nutrient, Toxin,
Antibody, Red Blood Cell, White Blood Cell) displayed as color-coded buttons. Features an
energy slider and quick presets (Cell Colony, Virus Outbreak, Nutrient Burst, Immune Response).

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

239 milestones across 9 categories:

| Category | Count | Examples |
|---|---|---|
| **Nuclear Physics** | 19 | First Light, Chain Reaction, Reactor Core, Braking Radiation, Quantum Leap |
| **Element Creation** | 17 | Simplest Atom, Iron Peak, Beyond Uranium, Half the Table, Centurion |
| **Particle Zoo** | 19 | Ghost Particle, Dark Side, Supersymmetry, Plasma Wave, Force Mediator |
| **Thermodynamics** | 6 | Getting Warm, Quark Epoch, Absolute Zero, Planck Epoch |
| **Milestones** | 28 | CERN at Home, Collider Veteran, Galaxy, Marathon Physicist, Lore Master |
| **Chemistry** | 9 | Chemical Bond, Molecular Library, Universal Solvent, Bond Builder |
| **Chirality** | 5 | Mirror Molecule, Stereochemist, Broken Mirror, Homochiral World, Enantiomer |
| **Scenarios** | 5 | First Steps, Nuclear Physicist, Chemist, Scenario Master |
| **Periodic Table** | 118 | One achievement per element (Z=1 Hydrogen through Z=118 Oganesson) |

The **Periodic Table** category renders as an interactive 18-column grid matching the standard
periodic table layout (10 rows including lanthanides/actinides). Discovered elements light up
gold with tooltips showing the element name, symbol, and atomic number.

Persist via `.ppach` file (v6 format, backward-compatible with v1&ndash;v5). Steam achievement
integration ready (optional, builds without Steamworks SDK).

## Lifetime Statistics

Career statistics accumulate across all sessions and persist in a `.ppstats` file (auto-saved
every 30 seconds). The statistics panel is accessible from the achievements screen and displays:

| Section | Metrics |
|---|---|
| **Career Totals** | Play time, simulations, total ticks, particles spawned, types observed |
| **Nuclear Reactions** | Fusions, fissions, annihilations, decays, spallations, pair productions, photoelectric |
| **Quantum & EM** | Virtual pairs, carrier exchanges, shell transitions, bremsstrahlung, neutrino oscillations, meson decays, left-handed weak decays |
| **Chirality & Symmetry** | Chiral molecules found, distinct chiral formulas, parity violations |
| **Chemistry** | Bonds formed, distinct molecules, total molecules formed, largest molecule (atoms) |
| **Records** | Peak temperature, peak particles, peak entangled, heaviest element created |
| **Gameplay** | Accelerator fires, scenarios completed, environments explored |
| **Top Particles** | Top 10 particle types by total spawned (with peak counts) |
| **Top Elements** | Top 10 elements by total created (with peak counts) |

Per-particle-type tracking covers all 74 physics types; per-element tracking covers Z=1&ndash;118.
Session counters persist in `.ppach` (v6); lifetime aggregates persist in `.ppstats` (v3).

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
- **Filter** buttons: All, Cached, New, Chiral, Achiral (last two on Molecules tab only)
- **Chirality column** (Molecules tab): shows "Chiral" badge for chiral molecules from cached v3+ files
- **Download / Import** workflow: select an entry, preview its metadata, download to local cache,
  then import directly into the simulation

See [docs/online-repository.md](online-repository.md) for protocol and caching details.

## Window Management

All ImGui windows (info cards, settings panels, repository browser, event log, etc.) dynamically
clamp their position to screen edges so they never open partially or fully off-screen. This
applies on first open, after resolution changes, and when restoring saved window positions.

Windows can be **minimized** via the bottom taskbar &mdash; clicking a window's taskbar entry
toggles between visible and minimized states. Minimized windows remain in the taskbar (dimmed)
and can be restored by clicking again. Right-clicking closes the window entirely.

## Error Dialogs

Fatal errors (Vulkan init failure, no GPU, device lost) show native OS message boxes
(MessageBox on Windows, zenity/kdialog/xmessage on Linux) instead of silent stderr output.
