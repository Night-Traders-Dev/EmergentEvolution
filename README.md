# Particle Life – C++ / Vulkan Port

Standalone C++ port of the Godot 4 GDScript particle-life simulation.
Replaces Godot's `RenderingDevice` API with raw Vulkan compute shaders,
and Dear ImGui for the settings panel.

## Requirements (Ubuntu / Debian)

```bash
# Vulkan SDK (includes glslc shader compiler)
sudo apt install libvulkan-dev vulkan-tools glslang-tools

# Or install the full LunarG Vulkan SDK:
# https://vulkan.lunarg.com/sdk/home#linux

# GLFW + GLM
sudo apt install libglfw3-dev libglm-dev

# CMake 3.20+
sudo apt install cmake

# C++20 compiler
sudo apt install g++-12   # or clang-14+
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

The compiled SPIR-V shaders are placed in `build/shaders/`.
Run the binary **from the build directory** so it can find them:

```bash
./particle_life
```

## Controls

| Key / Mouse        | Action                          |
|--------------------|---------------------------------|
| **F1**             | Toggle settings panel           |
| **F2**             | Reset simulation                |
| **Space**          | Pause / unpause                 |
| **F11**            | Toggle fullscreen               |
| **Esc**            | Quit                            |
| **Left drag**      | Pan camera                      |
| **Scroll wheel**   | Zoom in / out                   |

In the **Particle Values** grid:
- **Hover + scroll** over a cell → adjust the force between those two types  
- **Right-click** a cell → zero the force

## Architecture

| File                       | Mirrors                  | Responsibility                                     |
|----------------------------|--------------------------|----------------------------------------------------|
| `src/types.h`              | –                        | Shared constants, `PushConstants`, `SimConfig`     |
| `src/particles.h/.cpp`     | `particles.gd`           | CPU-side particle arrays, seeded generation        |
| `src/vulkan_context.h/.cpp`| –                        | Vulkan instance, device, swapchain, helpers        |
| `src/compute_pipeline.h/.cpp` | `pipeline.gd`         | Compute pipeline, double-buffered particle buffers |
| `src/renderer.h/.cpp`      | –                        | Fullscreen-quad pipeline, ImGui, swapchain sync    |
| `src/interface.h/.cpp`     | `interface.gd`           | Dear ImGui settings panel, particle grid           |
| `src/simulation.h/.cpp`    | `simulation.gd`          | Main loop, input, camera, orchestration            |
| `src/main.cpp`             | –                        | Entry point, GLFW window, main loop                |
| `shaders/compute.comp`     | `compute.glsl`           | GPU physics + particle-to-texture rendering        |
| `shaders/fullscreen.vert`  | –                        | Fullscreen triangle (no vertex buffer)             |
| `shaders/fullscreen.frag`  | –                        | Sample particle texture → swapchain                |

## Key differences from the Godot version

- **No Godot RenderingDevice wrapper** – Vulkan descriptors, pipelines, and
  barriers are managed directly.
- **Compute submitted separately** – physics + render-to-texture run via a
  one-time command buffer (`vkQueueWaitIdle`) before the graphics render pass,
  keeping synchronisation simple without semaphore chains.
- **ImGui replaces Godot Control nodes** – the settings panel is rendered via
  `imgui_impl_vulkan` inside the same render pass as the fullscreen quad.
- **Glow** – the original used a Godot `WorldEnvironment` glow effect; this
  port retains the checkbox but does not implement bloom (hook into a
  post-processing pass if desired).
