# Architecture

```
EmergentEvolution/
├── src/
│   ├── types.h                  # SimConfig, PushConstants, shared constants
│   ├── particles.h/.cpp         # CPU particle arrays and type data
│   ├── vulkan_context.h/.cpp    # Vulkan instance, device, swapchain, buffers
│   ├── compute_pipeline.h/.cpp  # 25-binding descriptor layout, buffer lifecycle, readback, bloom
│   ├── renderer.h/.cpp          # Fullscreen-quad pipeline, ImGui integration
│   ├── particle_textures.h/.cpp # Per-type custom texture array (sampler2DArray, hot-reload)
│   ├── stb_image*.h/.cpp        # Image loading/writing (icons, thumbnails)
│   └── miniaudio.h              # Single-header audio (MP3 decode + playback)
├── src/physics/
│   ├── simulation.h             # PhysicsSimulation class definition
│   ├── simulation.cpp           # Core: tick loop, init, reset, input, spatial grid, achievements
│   ├── nuclear.cpp              # Annihilation, fusion, fission, nuclear decay, photoelectric,
│   │                            #   pion decay, spallation, carrier exchange, quasiparticles
│   ├── orbital.cpp              # Orbital assignment, nucleus repulsion, bonds, shell transitions
│   ├── decay.cpp                # Particle decay, hadronization, bremsstrahlung, weak flavor change
│   ├── quantum.cpp              # Virtual pairs, neutrino scattering/oscillations, entanglement
│   ├── spawning.cpp             # Accelerator fire, atom/particle spawning
│   ├── sim_helpers.h            # Shared inline helpers (Lorentz gamma, energy conversion, etc.)
│   ├── interface.h              # PhysicsInterface class definition
│   ├── interface.cpp            # Core: init, preferences, themes, render_imgui dispatcher
│   ├── ui_panels.cpp            # Top bar, bottom bar, settings panel, spawn menu
│   ├── ui_cards.cpp             # Particle info card, element card, molecule card
│   ├── ui_lists.cpp             # Element/particle lists, bestiaries
│   ├── ui_dialogs.cpp           # Splash screen, pause menu, settings menu, save/load dialog
│   ├── ui_tools.cpp             # Decay log, nuclear debug, accelerator, force objects, measurement
│   ├── ui_overlays.cpp          # Visualization overlays (heatmap, fields, trajectories, etc.)
│   ├── ui_data.h                # Shared UI data tables (elements, particle names/colors, formatting)
│   ├── phys_particles.h/.cpp    # 74 particle types, masses, charges, environments
│   ├── molecules.h              # ~50 molecule templates with geometry
│   ├── achievements.h/.cpp      # 126 achievements, persistence, Steam API names
│   ├── audio.h/.cpp             # Background music + 10 SFX channels via miniaudio
│   ├── save_load.h/.cpp         # Binary .ppsg/.ppel/.ppmol serialization
│   ├── paths.h                  # Platform-appropriate data directory (XDG / AppData)
│   ├── error_dialog.h/.cpp      # Native OS error dialogs (MessageBox / zenity)
│   ├── steam_integration.h/.cpp # Optional Steamworks SDK wrapper (no-op stubs)
│   ├── tutorial.h/.cpp          # 10-step interactive tutorial system
│   ├── scenarios.h/.cpp         # 18 guided scenarios with goals (Cosmic Evolution arc)
│   ├── encyclopedia.h           # Particle type descriptions and metadata
│   └── main.cpp                 # Entry point
├── shaders/
│   ├── physics.comp             # GPU: forces, collisions, bonds, fields, bloom, wave rendering
│   ├── fullscreen.vert/.frag    # Render pipeline
├── assets/                      # Icons, music, Windows resources
│   ├── particles/              # 74 custom particle texture PNGs (128x128 RGBA)
│   └── sfx/                    # 10 procedural WAV sound effects
├── tools/
│   ├── ppmol/
│   │   ├── ppmol_gen.cpp        # Molecule generator/loader/renderer (.ppmol tool)
│   │   └── ppmol_gen.py         # Python SDF/PubChem → .ppmol converter (RDKit)
│   ├── gen_particle_textures.cpp # Standalone texture generator (stb_image_write)
│   └── gen_sound_effects.cpp     # Procedural WAV synthesizer (10 SFX)
├── steam/sdk/                   # Steamworks SDK (headers + redistributable binaries)
├── cmake/                       # FindSteamworks.cmake, embed_resource.cmake
├── CREDITS.md                   # Third-party library credits and licenses
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
