#include "biochem/biochem_app.h"
#include "common/error_dialog.h"
#include "common/launch_utils.h"
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
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    // Splash: any click dismisses (before ImGui capture check)
    if (app->show_splash && action == GLFW_PRESS) {
        app->show_splash = false;
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse) return;

    // No interaction during pause menu (ImGui handles its own buttons)
    if (app->show_pause_menu) return;

    // Left mouse: orbit camera drag
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            app->mouse_dragging = true;
            glfwGetCursorPos(window, &app->last_mouse_x, &app->last_mouse_y);
        } else {
            app->mouse_dragging = false;
        }
    }

    // Right mouse: pick closest entity
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        float W = (float)fb_w, H = (float)fb_h;
        float aspect = W / H;

        glm::mat4 vp = app->camera.proj_matrix(aspect) * app->camera.view_matrix();

        int best = -1;
        float best_dist = 30.0f;
        for (size_t i = 0; i < app->state.entities.size(); i++) {
            const auto& e = app->state.entities[i];
            if (!e.alive) continue;

            // Project to screen
            glm::vec4 clip = vp * glm::vec4(e.pos, 1.0f);
            if (clip.w <= 0.0f) continue;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float sx = (ndc.x * 0.5f + 0.5f) * W;
            float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * H;

            float dx = sx - (float)mx;
            float dy = sy - (float)my;
            float d = std::sqrt(dx * dx + dy * dy);

            float fov_rad = glm::radians(app->camera.fov);
            float sr = (e.radius / clip.w) * (H / (2.0f * std::tan(fov_rad * 0.5f)));
            float pick_r = std::max(sr, 10.0f);
            if (d < pick_r && d < best_dist) {
                best_dist = d;
                best = (int)i;
            }
        }
        app->selected_entity = best;
    }

    // Middle-click: spawn entity at clicked 3D position
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        float W = (float)fb_w, H = (float)fb_h;
        float aspect = W / H;

        glm::mat4 inv_vp = glm::inverse(app->camera.proj_matrix(aspect) * app->camera.view_matrix());
        float ndc_x = ((float)mx / W) * 2.0f - 1.0f;
        float ndc_y = 1.0f - ((float)my / H) * 2.0f;

        glm::vec4 near_clip = inv_vp * glm::vec4(ndc_x, ndc_y, -1.0f, 1.0f);
        glm::vec4 far_clip  = inv_vp * glm::vec4(ndc_x, ndc_y,  1.0f, 1.0f);
        glm::vec3 near_pt = glm::vec3(near_clip) / near_clip.w;
        glm::vec3 far_pt  = glm::vec3(far_clip) / far_clip.w;
        glm::vec3 ray_dir = glm::normalize(far_pt - near_pt);

        glm::vec3 eye = app->camera.eye_position();
        glm::vec3 plane_normal = glm::normalize(app->camera.target - eye);
        float denom = glm::dot(ray_dir, plane_normal);
        glm::vec3 spawn_pos = app->camera.target;
        if (std::abs(denom) > 1e-6f) {
            float t = glm::dot(app->camera.target - near_pt, plane_normal) / denom;
            if (t > 0.0f)
                spawn_pos = near_pt + ray_dir * t;
        }

        app->spawn_at(spawn_pos);
    }
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app || !app->mouse_dragging) return;

    double dx = xpos - app->last_mouse_x;
    double dy = ypos - app->last_mouse_y;
    app->last_mouse_x = xpos;
    app->last_mouse_y = ypos;

    app->camera.azimuth   -= (float)dx * 0.005f;
    app->camera.elevation += (float)dy * 0.005f;

    float limit = 1.5f;
    if (app->camera.elevation >  limit) app->camera.elevation =  limit;
    if (app->camera.elevation < -limit) app->camera.elevation = -limit;
}

static void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    float factor = (yoffset > 0) ? 0.9f : 1.1f;
    app->camera.distance *= factor;
    if (app->camera.distance < 50.0f) app->camera.distance = 50.0f;
    if (app->camera.distance > 3000.0f) app->camera.distance = 3000.0f;
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<BiochemApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    // Splash: any key dismisses (before ImGui capture check)
    if (app->show_splash) {
        app->show_splash = false;
        return;
    }

    // Escape always works (even when ImGui has focus)
    if (key == GLFW_KEY_ESCAPE) {
        if (app->show_pause_menu) {
            app->show_pause_menu = false;
            app->paused = false;
        } else {
            app->show_pause_menu = true;
            app->paused = true;
        }
        return;
    }

    // Only block shortcuts when a text input widget is active
    if (ImGui::GetIO().WantTextInput) return;

    if (key == GLFW_KEY_SPACE && !app->show_pause_menu)
        app->paused = !app->paused;
    if (key == GLFW_KEY_R && !app->show_pause_menu) {
        app->camera = OrbitCamera{};
        app->camera.distance = 500.0f;
        app->camera.elevation = 0.4f;
        app->camera.fov = 50.0f;
        app->camera.near_clip = 0.5f;
        app->camera.far_clip = 5000.0f;
    }
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
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
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

        if (app.request_quit)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        // ~60 FPS cap when paused or showing overlays
        if (app.paused || app.show_splash || app.show_pause_menu)
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    bool launch = app.request_launcher;
    try { app.destroy(); } catch (...) {}
    glfwDestroyWindow(window);
    glfwTerminate();

    if (launch)
        launch_sibling_exe("pp_launcher");

    return 0;
}
