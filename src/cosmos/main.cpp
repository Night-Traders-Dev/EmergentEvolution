#include "cosmos/cosmos_app.h"
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

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto* app = static_cast<CosmosApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    // Splash: any click dismisses (before ImGui capture check)
    if (app->show_splash && action == GLFW_PRESS) {
        app->show_splash = false;
        return;
    }

    if (ImGui::GetIO().WantCaptureMouse) return;

    // No interaction during pause menu (ImGui handles its own buttons)
    if (app->show_pause_menu) return;

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    // ── Left-click: orbit drag OR click-to-select / double-click-to-focus ──
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // Check for shift+left = pan
            if (mods & GLFW_MOD_SHIFT) {
                app->mouse_panning = true;
                app->last_mouse_x = mx;
                app->last_mouse_y = my;
            } else {
                app->mouse_dragging = true;
                app->last_mouse_x = mx;
                app->last_mouse_y = my;

                // Set up click candidate for click-to-select
                app->click_pending_ = true;
                app->click_start_x_ = (float)mx;
                app->click_start_y_ = (float)my;

                int fb_w, fb_h;
                glfwGetFramebufferSize(window, &fb_w, &fb_h);
                app->click_candidate_ = app->pick_body((float)mx, (float)my,
                                                        (float)fb_w, (float)fb_h);
            }
        } else { // RELEASE
            float drag_dist = std::sqrt(
                (float)((mx - app->click_start_x_) * (mx - app->click_start_x_) +
                        (my - app->click_start_y_) * (my - app->click_start_y_)));

            // If the user barely moved the mouse, treat as a click (not a drag)
            if (app->click_pending_ && drag_dist < 5.0f) {
                double now = glfwGetTime();
                float click_dist = std::sqrt(
                    (app->click_start_x_ - app->last_click_x_) *
                    (app->click_start_x_ - app->last_click_x_) +
                    (app->click_start_y_ - app->last_click_y_) *
                    (app->click_start_y_ - app->last_click_y_));

                bool is_double = (now - app->last_click_time_ < 0.4) && (click_dist < 20.0f);

                if (is_double && app->click_candidate_ >= 0) {
                    // Double-click on body → focus camera on it
                    const auto& b = app->state.bodies[app->click_candidate_];
                    app->selected_body = app->click_candidate_;
                    app->camera.focus_on(b.pos, app->click_candidate_);
                    // Zoom to a comfortable distance based on body radius
                    app->camera.target_distance = b.radius * 8.0f;
                } else {
                    // Single click → select/deselect
                    if (app->click_candidate_ >= 0) {
                        app->selected_body = app->click_candidate_;
                    } else {
                        app->selected_body = -1;
                    }
                }

                app->last_click_time_ = now;
                app->last_click_x_ = app->click_start_x_;
                app->last_click_y_ = app->click_start_y_;
            }

            app->click_pending_ = false;
            app->mouse_dragging = false;
            app->mouse_panning = false;
        }
    }

    // ── Right-click drag: pan camera ──
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            app->mouse_panning = true;
            app->last_mouse_x = mx;
            app->last_mouse_y = my;
        } else {
            app->mouse_panning = false;
        }
    }

    // ── Middle-click: spawn entity at clicked 3D position ──
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        float W = (float)fb_w, H = (float)fb_h;
        float aspect = W / H;

        // Unproject screen point to a ray, then intersect with the plane
        // through camera.target perpendicular to the view direction
        glm::dmat4 inv_vp = glm::inverse(app->camera.proj_matrix_d(aspect) * app->camera.view_matrix_d());
        float ndc_x = ((float)mx / W) * 2.0f - 1.0f;
        float ndc_y = 1.0f - ((float)my / H) * 2.0f;

        glm::dvec4 near_clip = inv_vp * glm::dvec4(ndc_x, ndc_y, -1.0, 1.0);
        glm::dvec4 far_clip  = inv_vp * glm::dvec4(ndc_x, ndc_y,  1.0, 1.0);
        glm::dvec3 near_pt_d = glm::dvec3(near_clip) / near_clip.w;
        glm::dvec3 far_pt_d  = glm::dvec3(far_clip) / far_clip.w;
        glm::dvec3 ray_dir_d = glm::normalize(far_pt_d - near_pt_d);

        // Plane through camera.target, normal = view direction (eye → target)
        glm::dvec3 eye = app->camera.eye_position_d();
        glm::dvec3 plane_normal = glm::normalize(glm::dvec3(app->camera.target) - eye);
        double denom = glm::dot(ray_dir_d, plane_normal);
        glm::vec3 spawn_pos = app->camera.target;
        if (std::abs(denom) > 1e-9) {
            double t = glm::dot(glm::dvec3(app->camera.target) - near_pt_d, plane_normal) / denom;
            if (t > 0.0)
                spawn_pos = glm::vec3(near_pt_d + ray_dir_d * t);
        }

        app->spawn_at(spawn_pos);
    }
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<CosmosApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    double dx = xpos - app->last_mouse_x;
    double dy = ypos - app->last_mouse_y;
    app->last_mouse_x = xpos;
    app->last_mouse_y = ypos;

    // Pan (right-drag or shift+left-drag)
    if (app->mouse_panning) {
        app->camera.pan((float)dx, (float)dy);
        return;
    }

    // Orbit (left-drag)
    if (app->mouse_dragging) {
        app->camera.orbit((float)dx, (float)dy);
    }
}

static void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<CosmosApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    app->camera.zoom((float)yoffset);
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<CosmosApp*>(glfwGetWindowUserPointer(window));
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
    }
    if (key == GLFW_KEY_F && app->selected_body >= 0 &&
        app->selected_body < (int)app->state.bodies.size()) {
        // F key: focus on selected body
        const auto& b = app->state.bodies[app->selected_body];
        app->camera.focus_on(b.pos, app->selected_body);
        app->camera.target_distance = b.radius * 8.0f;
    }
    if (key == GLFW_KEY_DELETE && app->selected_body >= 0 &&
        app->selected_body < (int)app->state.bodies.size()) {
        app->state.bodies.erase(app->state.bodies.begin() + app->selected_body);
        app->state.trails.erase(app->state.trails.begin() + app->selected_body);
        app->selected_body = -1;
        app->camera.release_focus();
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
        "Cosmic Sandbox v" APP_VERSION,
        nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        show_error_dialog("Startup Error", "Failed to create window.");
        return 1;
    }

    CosmosApp app;

    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) {
        auto* a = static_cast<CosmosApp*>(glfwGetWindowUserPointer(w));
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
