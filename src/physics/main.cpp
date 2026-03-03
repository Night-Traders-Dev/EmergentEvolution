#include "physics/core/simulation.h"
#include "common/error_dialog.h"
#include "physics/features/steam_integration.h"
#include "third_party/stb_image.h"
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
#include "common/embedded_resources.h"
#endif
#ifdef _WIN32
#include <windows.h>
#endif

static PhysicsSimulation* g_sim_resize = nullptr;

static void framebuffer_resize_callback(GLFWwindow*, int, int) {
    if (g_sim_resize)
        g_sim_resize->renderer.swapchain_dirty = true;
}

// ── Fullscreen toggle (Alt+Enter) ────────────────────────────────────────────

static GLFWmonitor* get_preferred_monitor(int preferred_idx) {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (monitors && preferred_idx >= 0 && preferred_idx < count)
        return monitors[preferred_idx];
    return glfwGetPrimaryMonitor();
}

static void toggle_fullscreen(GLFWwindow* window, PhysicsSimulation& sim) {
    GLFWmonitor* monitor = get_preferred_monitor(sim.iface.prefs.preferred_monitor);
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    if (sim.is_fullscreen_) {
        // Switch to windowed
        int w = sim.saved_window_w_;
        int h = sim.saved_window_h_;
        int x = sim.saved_window_x_;
        int y = sim.saved_window_y_;
        if (w <= 0 || h <= 0) { w = mode->width * 3 / 4; h = mode->height * 3 / 4; }
        if (x <= 0 || y <= 0) { x = (mode->width - w) / 2; y = (mode->height - h) / 2; }
        glfwSetWindowMonitor(window, nullptr, x, y, w, h, 0);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        sim.is_fullscreen_ = false;
        sim.iface.prefs.window_mode = 1;
    } else {
        // Save windowed position/size before going fullscreen
        glfwGetWindowPos(window, &sim.saved_window_x_, &sim.saved_window_y_);
        glfwGetWindowSize(window, &sim.saved_window_w_, &sim.saved_window_h_);
        sim.iface.prefs.window_w = sim.saved_window_w_;
        sim.iface.prefs.window_h = sim.saved_window_h_;
        // Switch to borderless fullscreen
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(window, nullptr, 0, 0, mode->width, mode->height, 0);
        glfwMaximizeWindow(window);
        sim.is_fullscreen_ = true;
        sim.iface.prefs.window_mode = 0;
    }
    sim.renderer.swapchain_dirty = true;
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
    if (action == GLFW_PRESS && key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT)) {
        if (g_sim_resize)
            toggle_fullscreen(window, *g_sim_resize);
    }
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

    // Steam integration — init before window creation.
    // Non-Steam builds: init() is a no-op that returns true.
    if (!steam::init()) {
        // Steam requested app relaunch via Steam client — exit immediately
        return 0;
    }

    if (!glfwInit()) {
        show_error_dialog("Startup Error",
            "Failed to initialize GLFW.\n\n"
            "Please ensure your graphics drivers are installed and up to date.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
    GLFWwindow* window = glfwCreateWindow(
        mode->width, mode->height,
        "Particle Playground v" APP_VERSION,
        nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        show_error_dialog("Startup Error",
            "Failed to create application window.\n\n"
            "Your system may not support Vulkan or the display may be unavailable.");
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
    glfwSetKeyCallback(window, key_callback);
    PhysicsSim_RegisterScrollCallback(window, &sim);

    try {
        sim.init(window);
    } catch (const std::exception& e) {
        std::string msg = std::string("Initialization failed:\n\n") + e.what() +
            "\n\nThis usually means your GPU does not support Vulkan, "
            "or the Vulkan driver is not installed.";
        show_error_dialog("Vulkan Error", msg.c_str());
        return 1;
    }

    // Apply saved window mode + preferred monitor after prefs are loaded
    {
        GLFWmonitor* pref_mon = get_preferred_monitor(sim.iface.prefs.preferred_monitor);
        const GLFWvidmode* pref_mode = glfwGetVideoMode(pref_mon);

        if (sim.iface.prefs.window_mode == 1) {
            // User prefers windowed mode
            int w = sim.iface.prefs.window_w;
            int h = sim.iface.prefs.window_h;
            if (w <= 0 || h <= 0) { w = pref_mode->width * 3 / 4; h = pref_mode->height * 3 / 4; }
            int mx, my;
            glfwGetMonitorPos(pref_mon, &mx, &my);
            int x = mx + (pref_mode->width - w) / 2;
            int y = my + (pref_mode->height - h) / 2;
            glfwSetWindowMonitor(window, nullptr, x, y, w, h, 0);
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
            sim.is_fullscreen_ = false;
            sim.saved_window_w_ = w;
            sim.saved_window_h_ = h;
            sim.saved_window_x_ = x;
            sim.saved_window_y_ = y;
        } else if (pref_mon != monitor) {
            // Fullscreen on non-primary monitor: reposition window
            int mx, my;
            glfwGetMonitorPos(pref_mon, &mx, &my);
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(window, nullptr, mx, my, pref_mode->width, pref_mode->height, 0);
            glfwMaximizeWindow(window);
            sim.renderer.swapchain_dirty = true;
        }
    }

    using Clock = std::chrono::high_resolution_clock;
    auto last_time = Clock::now();
    bool device_lost_recovery = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        steam::run_callbacks();

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        if (dt > 0.1) dt = 0.1;

        try {
            sim.tick(window, dt);
        } catch (const std::exception& e) {
            std::cerr << "Tick error: " << e.what() << "\n";
            if (!device_lost_recovery) {
                // Attempt one recovery (e.g., device lost)
                device_lost_recovery = true;
                sim.is_active = false;
                std::cerr << "Simulation paused due to error. Attempting recovery...\n";
                try {
                    vkDeviceWaitIdle(sim.vk.device);
                } catch (...) {}
                continue;
            }
            show_error_dialog("Runtime Error",
                "The simulation encountered an unrecoverable error.\n\n"
                "This may be caused by a GPU driver crash or hardware issue.");
            break;
        }
        device_lost_recovery = false;

        // FPS cap — sleep to maintain target frame time
        int cap = sim.iface.prefs.fps_cap;
        if (cap > 0) {
            auto frame_end = Clock::now();
            double frame_time = std::chrono::duration<double>(frame_end - now).count();
            double target = 1.0 / cap;
            if (frame_time < target) {
                std::this_thread::sleep_for(std::chrono::duration<double>(target - frame_time));
            }
        } else if (!sim.is_active) {
            // When paused with no FPS cap, yield to avoid busy-spinning
            // (prevents CPU saturation and compositor contention on Linux)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    try {
        sim.destroy();
    } catch (const std::exception& e) {
        std::cerr << "Cleanup error: " << e.what() << "\n";
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    steam::shutdown();
    return 0;
}
