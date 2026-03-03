# Architecture

The project builds five executables from a shared Vulkan framework:

| Target | Description |
| --- | --- |
| `pp_launcher` | Expansion picker — animated menu to launch any simulation |
| `particle_physics` | Full particle physics sandbox (282 types, compute shaders) |
| `particle_cosmos` | 3D celestial mechanics with GPU raytraced rendering |
| `particle_biochem` | 2D cellular biology sandbox |
| `pp_common` | Static library shared by all targets (Vulkan, renderer, audio, etc.) |

```text
EmergentEvolution/
├── src/common/                        # pp_common static library
│   ├── vulkan_context.h/.cpp          # Vulkan instance, device, swapchain, buffers
│   ├── simple_renderer.h/.cpp         # Fullscreen-quad pipeline, ImGui integration (shared)
│   ├── audio.h/.cpp                   # Background music + SFX via miniaudio
│   ├── error_dialog.h/.cpp            # Native OS error dialogs (MessageBox / zenity)
│   ├── http_client.h/.cpp             # HTTP client for repository browser
│   ├── paths.h                        # Platform-appropriate data directory (XDG / AppData)
│   ├── pp_common.h                    # Shared includes and forward declarations
│   ├── embedded_resources.h           # Portable build embedded shader/icon data
│   └── stb_image*_impl.cpp           # Image loading/writing implementations
├── src/launcher/
│   └── main.cpp                       # Expansion picker with animated particle background
├── src/physics/                       # Particle Physics application
│   ├── core/
│   │   ├── simulation.h/.cpp          # PhysicsSimulation: tick loop, init, reset, spatial grid
│   │   ├── types.h                    # SimConfig, PushConstants, shared constants
│   │   ├── phys_particles.h/.cpp      # 282 particle types, masses, charges, environments
│   │   └── sim_helpers.h              # Shared inline helpers (Lorentz gamma, energy, etc.)
│   ├── processes/
│   │   ├── nuclear.cpp                # Annihilation, fusion, fission, spallation, carrier exchange
│   │   ├── orbital.cpp                # Orbital assignment, bonds, shell transitions
│   │   ├── decay.cpp                  # Particle decay, hadronization, bremsstrahlung
│   │   ├── meson_decays.cpp           # Meson decay with PDG branching ratios (~60 channels)
│   │   ├── quantum.cpp                # Virtual pairs, neutrino oscillations, entanglement
│   │   ├── cp_violation.cpp           # Neutral meson oscillation and CP violation
│   │   └── spawning.cpp              # Accelerator fire, atom/particle spawning
│   ├── rendering/
│   │   ├── compute_pipeline.h/.cpp    # 25-binding descriptor layout, buffer lifecycle, bloom
│   │   ├── renderer.h/.cpp            # Physics-specific fullscreen-quad pipeline
│   │   ├── particles.h/.cpp           # CPU particle arrays and type data
│   │   └── particle_textures.h/.cpp   # Per-type texture array (sampler2DArray, hot-reload)
│   ├── ui/
│   │   ├── interface.h/.cpp           # PhysicsInterface: init, preferences, themes, dispatcher
│   │   ├── ui_panels.cpp              # Top bar, bottom bar, settings panel, spawn menu
│   │   ├── ui_cards.cpp               # Particle info card, element card, molecule card
│   │   ├── ui_lists.cpp               # Element/particle lists, bestiaries
│   │   ├── ui_dialogs.cpp             # Splash screen, pause menu, save/load dialog
│   │   ├── ui_tools.cpp               # Decay log, accelerator, force objects, measurement
│   │   ├── ui_overlays.cpp            # Visualization overlays (heatmap, fields, trajectories)
│   │   ├── ui_repository.cpp          # Online repository browser
│   │   └── ui_data.h                  # Shared UI data tables (names, colors, formatting)
│   ├── data/
│   │   ├── meson_data.h               # 188 PDG meson definitions (types 74–261)
│   │   ├── molecules.h                # ~50 molecule templates with geometry
│   │   └── encyclopedia.h             # Particle type descriptions and metadata
│   ├── features/
│   │   ├── save_load.h/.cpp           # Binary .ppsg/.ppel/.ppmol serialization
│   │   ├── repository.h/.cpp          # Online repository client
│   │   └── steam_integration.h/.cpp   # Optional Steamworks SDK wrapper
│   ├── gameplay/
│   │   ├── achievements.h/.cpp        # 239 achievements, lifetime stats, persistence
│   │   ├── scenarios.h/.cpp           # 20 guided scenarios with goals
│   │   ├── tutorial.h/.cpp            # 10-step interactive tutorial system
│   │   └── cutscenes.h/.cpp           # Animated cutscene sequences
│   └── main.cpp                       # Physics entry point
├── src/cosmos/                        # Cosmic Sandbox application
│   ├── cosmos_app.h/.cpp              # CosmosApp: init, tick, UI, splash, pause, spawn
│   ├── cosmos_types.h                 # CosmosConfig, CosmosState, CelestialBody, OrbitCamera
│   ├── cosmos_raytracer.h/.cpp        # GPU fullscreen sphere raytracer (Vulkan pipeline)
│   └── main.cpp                       # Cosmos entry point
├── src/biochem/                       # Biochemical Simulator application
│   ├── biochem_app.h/.cpp             # BiochemApp: init, tick, UI, splash, pause, spawn
│   ├── biochem_types.h                # BiochemConfig, BiochemState, BioEntity types
│   └── main.cpp                       # Biochem entry point
├── src/third_party/                   # Vendored headers
│   ├── miniaudio.h                    # Single-header audio (MP3 decode + playback)
│   ├── stb_image.h                    # Image loading
│   ├── stb_image_write.h             # Image writing
│   └── cjson/cJSON.h/.c              # JSON parser
├── shaders/
│   ├── physics.comp                   # GPU: forces, collisions, bonds, fields, bloom
│   ├── fullscreen.vert/.frag          # Physics render pipeline
│   ├── overlay.vert/.frag             # Physics overlay pipeline
│   ├── cosmos_rt.vert/.frag           # Cosmos GPU sphere raytracer
├── assets/                            # Icons, music, Windows resources
│   ├── particles/                     # 67 custom particle texture PNGs (128x128 RGBA)
│   └── sfx/                           # 10 procedural WAV sound effects
├── tools/
│   ├── ppmol/
│   │   ├── ppmol_gen.cpp              # Molecule generator/loader/renderer (.ppmol tool)
│   │   └── ppmol_gen.py               # Python SDF/PubChem → .ppmol converter (RDKit)
│   ├── gen_particle_textures.cpp      # Standalone texture generator (stb_image_write)
│   └── gen_sound_effects.cpp          # Procedural WAV synthesizer (10 SFX)
├── steam/sdk/                         # Steamworks SDK (headers + redistributable binaries)
├── cmake/                             # FindSteamworks.cmake, embed_resource.cmake
├── docs/                              # Documentation
├── CREDITS.md                         # Third-party library credits and licenses
└── CMakeLists.txt
```

<details>
<summary><b>Compute shader bindings</b></summary>

| Binding | Buffer | R/W |
|---|---|---|
| 0-1 | Position A/B (ping-pong) | read / write |
| 2 | Type | read |
| 3 | Force matrix | read |
| 4 | Colour table | read |
| 5-6 | Velocity A/B | read / write |
| 7 | Render texture | image write |
| 8 | Behaviour flags | read |
| 9-12 | Angle / angular velocity A/B | read / write |
| 13-14 | Energy A/B | read / write |
| 15 | Genome | read |
| 16 | Bond partners | read (CPU-managed) |
| 17 | Force objects | read |
| 18 | Mass inverse + ZPE table | read |
| 19 | GPU spatial grid cell starts | read (CPU-built) |
| 20 | GPU spatial grid sorted indices | read (CPU-built) |
| 21 | Bloom texture A (fine) | image read/write |
| 22 | Bloom texture B (wide) | image read/write |
| 23 | Particle texture array (sampler2DArray) | sampled read |
| 24 | Per-type render modes | read (CPU-managed) |

Particle buffers use DEVICE_LOCAL memory on discrete GPUs with staging buffers for CPU
readback. A/B buffers ping-pong each tick.

</details>
