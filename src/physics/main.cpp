#include "physics/simulation.h"
#include "stb_image.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib>
#ifdef HAS_OPENMP
#include <omp.h>
#endif
#ifdef PORTABLE_BUILD
#include "embedded_resources.h"
#endif
#ifdef _WIN32
#include <windows.h>
#endif

static PhysicsSimulation* g_sim_resize = nullptr;

static void framebuffer_resize_callback(GLFWwindow*, int, int) {
    if (g_sim_resize)
        g_sim_resize->renderer.swapchain_dirty = true;
}

int main() {
#ifdef _WIN32
    // Set CWD to the executable's directory so relative paths (saves/, assets/) work
    // regardless of how the exe was launched (double-click, shortcut, cmd, etc.)
    {
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) {
            char* last_sep = strrchr(exe_path, '\\');
            if (last_sep) { *last_sep = '\0'; SetCurrentDirectoryA(exe_path); }
        }
    }
#endif

#ifdef HAS_OPENMP
    omp_set_num_threads(omp_get_max_threads());  // initial default; updated by settings after prefs load
#endif

#ifndef _WIN32
    // Suppress GTK libdecor plugin (crashes in fontconfig on some systems).
    // Window is borderless (GLFW_DECORATED=FALSE) so decorations aren't needed.
    setenv("LIBDECOR_PLUGIN_DIR", "/nonexistent", 0);
#endif

    if (!glfwInit()) {
        std::cerr << "Failed to initialise GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    GLFWwindow* window = glfwCreateWindow(
        mode->width, mode->height,
        "Particle Playground",
        nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        std::cerr << "Failed to create GLFW window\n";
        return 1;
    }

    // Set window icon — multiple sizes for best quality
    // Try files first, fall back to embedded data (portable build)
    {
        GLFWimage icons[3];
        int w32, h32, c32, w64, h64, c64, w256, h256, c256;
        unsigned char* p32  = stbi_load("assets/icon_32.png",  &w32,  &h32,  &c32,  4);
        unsigned char* p64  = stbi_load("assets/icon_64.png",  &w64,  &h64,  &c64,  4);
        unsigned char* p256 = stbi_load("assets/icon_256.png", &w256, &h256, &c256, 4);
#ifdef PORTABLE_BUILD
        if (!p32)  p32  = stbi_load_from_memory(icon_32_png_data,  (int)icon_32_png_size,  &w32,  &h32,  &c32,  4);
        if (!p64)  p64  = stbi_load_from_memory(icon_64_png_data,  (int)icon_64_png_size,  &w64,  &h64,  &c64,  4);
        if (!p256) p256 = stbi_load_from_memory(icon_256_png_data, (int)icon_256_png_size, &w256, &h256, &c256, 4);
#endif
        int count = 0;
        if (p32)  { icons[count++] = { w32,  h32,  p32  }; }
        if (p64)  { icons[count++] = { w64,  h64,  p64  }; }
        if (p256) { icons[count++] = { w256, h256, p256 }; }
        if (count > 0) glfwSetWindowIcon(window, count, icons);
        if (p32)  stbi_image_free(p32);
        if (p64)  stbi_image_free(p64);
        if (p256) stbi_image_free(p256);
    }

    PhysicsSimulation sim;
    g_sim_resize = &sim;

    glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);
    PhysicsSim_RegisterScrollCallback(window, &sim);

    try {
        sim.init(window);
    } catch (const std::exception& e) {
        std::cerr << "Init error: " << e.what() << "\n";
        return 1;
    }

    using Clock = std::chrono::high_resolution_clock;
    auto last_time = Clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        if (dt > 0.1) dt = 0.1;

        try {
            sim.tick(window, dt);
        } catch (const std::exception& e) {
            std::cerr << "Tick error: " << e.what() << "\n";
            break;
        }

        // FPS cap — sleep to maintain target frame time
        int cap = sim.iface.prefs.fps_cap;
        if (cap > 0) {
            auto frame_end = Clock::now();
            double frame_time = std::chrono::duration<double>(frame_end - now).count();
            double target = 1.0 / cap;
            if (frame_time < target) {
                std::this_thread::sleep_for(std::chrono::duration<double>(target - frame_time));
            }
        }
    }

    try {
        sim.destroy();
    } catch (const std::exception& e) {
        std::cerr << "Cleanup error: " << e.what() << "\n";
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
