#pragma once
// ── Cosmic Sandbox — Application ────────────────────────────────────────────

#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_raytracer.h"
#include "common/simple_renderer.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct GLFWwindow;

class CosmosApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();
    void reset_simulation();
    void spawn_at(glm::vec3 pos);

    // Save/Load
    bool save_simulation(const std::string& path);
    bool load_simulation(const std::string& path);

    // Import/Export individual bodies
    bool export_body(int index, const std::string& path);
    bool import_body(const std::string& path);

    VulkanContext   vk;
    SimpleRenderer  renderer;
    CosmosConfig    cfg;
    CosmosState     state;
    OrbitCamera     camera;
    bool            paused = false;

    // Fullscreen overlay state
    bool  show_splash     = true;
    bool  show_pause_menu = false;
    bool  request_quit     = false;
    bool  request_launcher = false;

    // Input state (public for GLFW callbacks)
    bool   mouse_dragging = false;
    bool   mouse_panning  = false;     // middle-drag or shift+left-drag panning
    double last_mouse_x = 0, last_mouse_y = 0;
    int    selected_body = -1;
    int    spawn_type = CTYPE_PLANET;
    float  spawn_mass = 1.0f;

    // Hit-test: find body under screen coordinates
    int pick_body(float mx, float my, float W, float H) const;

    // Double-click detection
    double last_click_time_ = 0.0;
    float  last_click_x_ = 0, last_click_y_ = 0;

    // Click-to-select: deferred selection (applied on release if no drag)
    bool   click_pending_ = false;
    int    click_candidate_ = -1;
    float  click_start_x_ = 0, click_start_y_ = 0;

private:
    void render_ui();
    void render_overlay();
    void step_physics(float dt);

    // Physics subsystems
    void process_collisions(float dt);
    void process_roche_limit(float dt);
    void process_temperature(float dt);
    void process_evaporation(float dt);
    void process_stellar_evolution(float dt);
    void cleanup_bodies();
    void spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count,
                        uint32_t parent_generation = 0, float source_temperature = 300.0f,
                        glm::vec3 impact_axis = glm::vec3(0.0f), float ejecta_speed = 0.0f);

    // Fullscreen overlay screens
    void draw_splash_screen();
    void draw_pause_menu();
    void draw_menu_background();
    void draw_spawn_menu();

    // 3D projection helpers (for overlay drawing only)
    struct Projected { float sx, sy, depth; bool visible; };
    Projected project(const glm::vec3& world_pos, const glm::dmat4& vp,
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

    // Bottom bar
    float bottom_bar_offset_ = 1.0f;  // 0=visible, 1=hidden (start hidden)
    bool  show_menu_popup_   = false;
    bool  settings_visible_  = true;
    bool  body_list_visible_ = true;
    void  draw_bottom_bar();

    // Inspector panel
    bool  inspector_visible_ = false;  // shown on click, hidden on deselect
    void  draw_inspector();

    // Save/Load UI state
    bool  show_save_dialog_   = false;
    bool  show_load_dialog_   = false;
    bool  show_export_dialog_ = false;
    bool  show_import_dialog_ = false;
    char  file_path_buf_[512] = {};
    std::string last_save_status_;
    float save_status_timer_   = 0.0f;
    void  draw_file_dialog();
};
