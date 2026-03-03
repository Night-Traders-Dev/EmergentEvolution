#pragma once
// ── Cosmic Sandbox — Application ────────────────────────────────────────────

#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_raytracer.h"
#include "common/simple_renderer.h"
#include <glm/glm.hpp>
#include <vector>

struct GLFWwindow;

class CosmosApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();
    void reset_simulation();

    VulkanContext   vk;
    SimpleRenderer  renderer;
    CosmosConfig    cfg;
    CosmosState     state;
    OrbitCamera     camera;
    bool            paused = false;

    // Fullscreen overlay state
    bool  show_splash     = true;
    bool  show_pause_menu = false;
    bool  request_quit    = false;

    // Input state (public for GLFW callbacks)
    bool   mouse_dragging = false;
    double last_mouse_x = 0, last_mouse_y = 0;
    int    selected_body = -1;
    int    spawn_type = CTYPE_PLANET;
    float  spawn_mass = 1.0f;

private:
    void render_ui();
    void render_overlay();
    void step_physics(float dt);

    // Fullscreen overlay screens
    void draw_splash_screen();
    void draw_pause_menu();
    void draw_menu_background();
    void draw_spawn_menu();

    // 3D projection helpers (for overlay drawing only)
    struct Projected { float sx, sy, depth; bool visible; };
    Projected project(const glm::vec3& world_pos, const glm::mat4& vp,
                      float screen_w, float screen_h) const;
    float screen_radius(float world_radius, float depth, float fov_rad,
                        float screen_h) const;

    CosmosRaytracer raytracer_;
    float sim_time_ = 0.0f;

    // Splash + menu background particles
    float splash_time_ = 0.0f;
    struct MenuParticle {
        float x, y, vx, vy, radius;
        float r, g, b, alpha;
        float trail_x[12], trail_y[12];
    };
    std::vector<MenuParticle> menu_particles_;
    bool  menu_bg_inited_ = false;
    float menu_bg_time_ = 0.0f;

    // Spawn menu
    bool spawn_menu_visible_ = true;
    bool spawn_in_orbit_ = false;
};
