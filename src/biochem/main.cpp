#include "biochem/biochem_app.h"
#include "common/error_dialog.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// ── GLFW input callbacks ───────────────────────────────────────────────────

static void mouse_button_callback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        // Find closest entity to click
        int best = -1;
        float best_dist = 30.0f;
        for (size_t i = 0; i < app->state.entities.size(); i++) {
            const auto& e = app->state.entities[i];
            if (!e.alive) continue;
            float dx = e.pos.x - (float)mx;
            float dy = e.pos.y - (float)my;
            float d = std::sqrt(dx * dx + dy * dy);
            float pick_r = std::max(e.radius, 8.0f);
            if (d < pick_r && d < best_dist) {
                best_dist = d;
                best = (int)i;
            }
        }
        app->selected_entity = best;
    }
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_SPACE)
        app->paused = !app->paused;
}

// ── Main ────────────────────────────────────────────────────────────────────

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
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetKeyCallback(window, key_callback);

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
