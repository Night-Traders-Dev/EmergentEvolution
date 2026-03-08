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

static glm::vec3 viewport_spawn_position(CosmosApp* app, GLFWwindow* window, double mx, double my) {
    int fb_w, fb_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    float W = (float)fb_w, H = (float)fb_h;
    float aspect = W / H;

    // Unproject screen point to a ray, then intersect with a plane through
    // camera target and perpendicular to the camera view direction.
    glm::dmat4 inv_vp = glm::inverse(app->camera.proj_matrix_d(aspect) * app->camera.view_matrix_d());
    float ndc_x = ((float)mx / W) * 2.0f - 1.0f;
    float ndc_y = 1.0f - ((float)my / H) * 2.0f;

    glm::dvec4 near_clip = inv_vp * glm::dvec4(ndc_x, ndc_y, -1.0, 1.0);
    glm::dvec4 far_clip  = inv_vp * glm::dvec4(ndc_x, ndc_y,  1.0, 1.0);
    glm::dvec3 near_pt_d = glm::dvec3(near_clip) / near_clip.w;
    glm::dvec3 far_pt_d  = glm::dvec3(far_clip) / far_clip.w;
    glm::dvec3 ray_dir_d = glm::normalize(far_pt_d - near_pt_d);

    glm::dvec3 eye = app->camera.eye_position_d();
    glm::dvec3 plane_normal = glm::normalize(glm::dvec3(app->camera.target) - eye);
    double denom = glm::dot(ray_dir_d, plane_normal);
    glm::vec3 spawn_pos = app->camera.target;
    if (std::abs(denom) > 1e-9) {
        double t = glm::dot(glm::dvec3(app->camera.target) - near_pt_d, plane_normal) / denom;
        if (t > 0.0)
            spawn_pos = glm::vec3(near_pt_d + ray_dir_d * t);
    }
    return spawn_pos;
}

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

    // ── Left-click: drag to orbit, click to select, double-click to focus ──
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            app->mouse_dragging = true;
            app->camera.begin_orbit();
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
                    app->camera.focus_on(b.pos, app->click_candidate_, b.radius);
                    app->camera.target_distance = std::max(b.radius * 8.0f, 30.0f);
                } else {
                    // Single click on body → select. Empty space → spawn (if spawn menu open) or deselect.
                    if (app->click_candidate_ >= 0) {
                        app->selected_body = app->click_candidate_;
                    } else if (app->is_spawn_mode()) {
                        app->selected_body = -1;
                        glm::vec3 spawn_pos = viewport_spawn_position(app, window, mx, my);
                        int spawned = app->spawn_preview_body(spawn_pos);
                        if (spawned >= 0 && spawned < (int)app->state.bodies.size()) {
                            const auto& b = app->state.bodies[spawned];
                            app->selected_body = spawned;
                            app->camera.focus_on(b.pos, spawned, b.radius);
                        }
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
            app->camera.end_orbit();
        }
    }

    // ── Right-click drag: pan camera. Right-click release (no drag): re-roll in spawn mode ──
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        static float rclick_start_x = 0, rclick_start_y = 0;
        if (action == GLFW_PRESS) {
            app->mouse_panning = true;
            app->last_mouse_x = mx;
            app->last_mouse_y = my;
            rclick_start_x = (float)mx;
            rclick_start_y = (float)my;
        } else {
            float rdist = std::sqrt(
                (float)((mx - rclick_start_x) * (mx - rclick_start_x) +
                        (my - rclick_start_y) * (my - rclick_start_y)));
            // If barely moved, treat as right-click → re-roll spawn preview
            if (rdist < 5.0f && app->is_spawn_mode()) {
                app->reroll_spawn_preview();
            }
            app->mouse_panning = false;
        }
    }

    // ── Middle-click drag: pan camera ──
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            app->mouse_panning = true;
            app->last_mouse_x = mx;
            app->last_mouse_y = my;
        } else {
            app->mouse_panning = false;
        }
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

    // Pan (middle-drag)
    if (app->mouse_panning) {
        app->camera.pan((float)dx, (float)dy);
        return;
    }

    // Orbit (right-drag)
    if (app->mouse_dragging) {
        app->camera.orbit((float)dx, (float)dy);
    }
}

static void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    auto* app = static_cast<CosmosApp*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    // Zoom toward cursor position (Universe Sandbox style)
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    int fb_w, fb_h;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);

    glm::vec3 world_pt = app->camera.screen_to_world_on_target_plane(
        (float)mx, (float)my, (float)fb_w, (float)fb_h);
    app->camera.zoom_toward((float)yoffset, world_pt);
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods) {
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
        const auto& b = app->state.bodies[app->selected_body];
        app->camera.focus_on(b.pos, app->selected_body, b.radius);
        app->camera.target_distance = std::max(b.radius * 8.0f, 30.0f);
    }
    if (key == GLFW_KEY_DELETE && app->selected_body >= 0 &&
        app->selected_body < (int)app->state.bodies.size()) {
        app->state.bodies.erase(app->state.bodies.begin() + app->selected_body);
        app->state.trails.erase(app->state.trails.begin() + app->selected_body);
        app->selected_body = -1;
        app->camera.release_focus();
    }
    // Ctrl+D: duplicate selected body
    if (key == GLFW_KEY_D && (mods & GLFW_MOD_CONTROL) && !app->show_pause_menu) {
        app->duplicate_selected_body();
    }
    // F1: toggle keyboard shortcuts overlay
    if (key == GLFW_KEY_F1) {
        app->shortcuts_visible_ = !app->shortcuts_visible_;
    }
    // V: toggle velocity arrows
    if (key == GLFW_KEY_V && !app->show_pause_menu) {
        app->show_velocity_arrows_ = !app->show_velocity_arrows_;
    }
    // L: lock/unlock selected body
    if (key == GLFW_KEY_L && !app->show_pause_menu &&
        app->selected_body >= 0 && app->selected_body < (int)app->state.bodies.size()) {
        auto& b = app->state.bodies[app->selected_body];
        b.locked = !b.locked;
        if (b.locked) b.vel = glm::vec3(0.0f);
    }
    // F12: screenshot
    if (key == GLFW_KEY_F12) {
        app->screenshot_requested_ = true;
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

    // ── Loading screen state ────────────────────────────────────────────────
    float loading_progress = 0.0f;
    const char* loading_label = "Starting...";
    bool renderer_ready = false;

    auto render_loading_frame = [&]() {
        // Always process window events to prevent the OS from marking us as unresponsive
        glfwPollEvents();
        if (!renderer_ready) return;
        if (!app.renderer.begin_frame(app.vk, window)) return;

        ImGuiIO& io = ImGui::GetIO();
        float W = io.DisplaySize.x;
        float H = io.DisplaySize.y;

        // Dark background
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({W, H});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.03f, 0.06f, 1.0f));
        ImGui::Begin("##Loading", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoInputs);

        // Title
        ImGui::PushFont(nullptr);
        {
            const char* title = "Cosmic Sandbox";
            ImVec2 ts = ImGui::CalcTextSize(title);
            ImGui::SetCursorPos({(W - ts.x) * 0.5f, H * 0.36f});
            ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "%s", title);
        }

        // Status label
        {
            ImVec2 ls = ImGui::CalcTextSize(loading_label);
            ImGui::SetCursorPos({(W - ls.x) * 0.5f, H * 0.50f});
            ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "%s", loading_label);
        }

        // Progress bar
        float bar_w = W * 0.35f;
        float bar_h = 6.0f;
        float bar_x = (W - bar_w) * 0.5f;
        float bar_y = H * 0.56f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background track
        dl->AddRectFilled({bar_x, bar_y}, {bar_x + bar_w, bar_y + bar_h},
                          IM_COL32(40, 40, 55, 200), 3.0f);
        // Filled portion
        float fill_w = bar_w * std::clamp(loading_progress, 0.0f, 1.0f);
        if (fill_w > 1.0f) {
            ImU32 col_left  = IM_COL32(60, 100, 200, 255);
            ImU32 col_right = IM_COL32(120, 160, 255, 255);
            dl->AddRectFilledMultiColor({bar_x, bar_y}, {bar_x + fill_w, bar_y + bar_h},
                                        col_left, col_right, col_right, col_left);
        }
        // Percentage text
        {
            char pct[16];
            snprintf(pct, sizeof(pct), "%d%%", (int)(loading_progress * 100.0f));
            ImVec2 ps = ImGui::CalcTextSize(pct);
            ImGui::SetCursorPos({(W - ps.x) * 0.5f, bar_y + bar_h + 8.0f});
            ImGui::TextColored(ImVec4(0.45f, 0.5f, 0.6f, 1.0f), "%s", pct);
        }
        ImGui::PopFont();

        ImGui::End();
        ImGui::PopStyleColor();

        app.renderer.end_frame(app.vk);
    };

    try {
        app.init(window, [&](float frac, const char* label) {
            loading_progress = frac;
            loading_label = label;
            // Can only render after renderer.init() completes (frac > 0.15)
            if (frac > 0.16f && !renderer_ready)
                renderer_ready = true;
            render_loading_frame();
        });
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
