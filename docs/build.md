# Build

A unified `build.sh` handles both Linux native and Windows cross-compilation.

## Dependencies (Ubuntu / Debian)

```bash
# Linux build
sudo apt install libvulkan-dev vulkan-tools glslang-tools
sudo apt install libglfw3-dev libglm-dev cmake g++

# Windows cross-build (additional)
sudo apt install mingw-w64 g++-mingw-w64-x86-64
```

## Usage

```bash
source ~/vulkan/1.4.341.1/setup-env.sh

./build.sh                          # Linux x64 Release (default)
./build.sh linux --steam            # Linux Release with Steam SDK
./build.sh linux --debug --sanitize # Debug with ASan + UBSan
./build.sh linux --lto --native     # Release with LTO + native tuning
./build.sh win64 --steam            # Windows x64 (MinGW cross-compile)
./build.sh all --steam --lto        # Both targets, Steam + LTO
./build.sh tools                    # Build standalone tools (gen_sfx, gen_textures, ppmol_gen)
./build.sh package win64 --steam    # Build + distributable zip
./build.sh clean [TARGET]           # Wipe build dirs (linux, win64, or all)
```

Build flags: `--steam`, `--debug`, `--release`, `--reldbg`, `--o2`, `--o3`, `--lto`, `--native`, `--sanitize`, `--no-steam`.

## Build Targets

The project produces five executables and one static library:

| Target | Description |
| --- | --- |
| `pp_common` | Static library (Vulkan context, renderer, audio, error dialogs) |
| `pp_launcher` | Expansion picker — choose which simulation to launch |
| `particle_physics` | Full particle physics sandbox |
| `particle_cosmos` | 3D celestial mechanics with GPU raytracing |
| `particle_biochem` | 2D cellular biology sandbox |

All application targets link against `pp_common`. Shader compilation (`physics.comp`,
`fullscreen.vert/frag`, `overlay.vert/frag`, `cosmos_rt.vert/frag`) is handled by the
`shaders` custom target, which all binaries depend on.

## Run

```bash
# Linux
./build/pp_launcher                   # Launch the expansion picker
./build/particle_physics              # Particle physics directly
./build/particle_cosmos               # Cosmic sandbox directly
./build/particle_biochem              # Biochemical simulator directly

# Windows (or from dist-win64/)
./build-win64/pp_launcher.exe
./build-win64/particle_physics.exe
./build-win64/particle_cosmos.exe
./build-win64/particle_biochem.exe
```

The Windows portable exes bundle SPIR-V shaders and icons via `PORTABLE_BUILD`.
Users need Vulkan GPU drivers installed. Place `assets/sound.mp3` next to the exe for
background music.

## CMake Options

| Option | Default | Description |
|---|---|---|
| `PORTABLE_BUILD` | OFF | Embed shaders and icons into the executable |
| `PORTABLE_PATHS` | OFF | Use relative `saves/` directory instead of platform-standard paths |
| `STEAMWORKS_SDK_DIR` | &mdash; | Path to Steamworks SDK for optional Steam integration |

## Steam Integration (Optional)

Steam achievements and cloud saves are supported via optional Steamworks SDK linkage.
The SDK is bundled at `steam/sdk/`. The build compiles and runs without it &mdash; all
Steam calls are no-ops when `HAS_STEAM` is not defined.

```bash
# Build with Steam support (using bundled SDK)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEAMWORKS_SDK_DIR="$(pwd)/steam/sdk"

# Or point to an external SDK
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSTEAMWORKS_SDK_DIR=/path/to/sdk
```

For development testing, place a `steam_appid.txt` (containing your App ID) next to the
executable. The bundled default uses App ID 480 (Spacewar, Valve's test app).
