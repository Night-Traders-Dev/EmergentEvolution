#include "biochem/biochem_app.h"
#include "physics/error_dialog.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <chrono>
#include <thread>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    {
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH)) {
            char* last_sep = strrchr(exe_path, '\\');
            if (last_sep) { *last_sep = '\0'; SetCurrentDirectoryA(exe_path); }
        }
    }
#endif

#ifndef _WIN32
    setenv("LIBDECOR_PLUGIN_DIR", "/nonexistent", 0);
#endif

    if (!glfwInit()) {
        show_error_dialog("Startup Error", "Failed to initialize GLFW.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED,  GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED,  GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    GLFWwindow* window = glfwCreateWindow(
        mode->width, mode->height,
        "Biochemical Simulator v" APP_VERSION,
        nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        show_error_dialog("Startup Error", "Failed to create window.");
        return 1;
    }

    BiochemApp app;

    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        auto* a = static_cast<BiochemApp*>(glfwGetWindowUserPointer(w));
        if (a) a->renderer.swapchain_dirty = true;
    });

    try {
        app.init(window);
    } catch (const std::exception& e) {
        std::string msg = std::string("Initialization failed:\n\n") + e.what();
        show_error_dialog("Vulkan Error", msg.c_str());
        glfwDestroyWindow(window);
        glfwTerminate();
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
            app.tick(window, static_cast<float>(dt));
        } catch (const std::exception& e) {
            std::cerr << "Tick error: " << e.what() << "\n";
            break;
        }

        if (app.paused)
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    try { app.destroy(); } catch (...) {}
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
