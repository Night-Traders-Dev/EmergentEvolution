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
    float  spawn_mass = 3.003e-6f;

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
    void process_material_phases(float dt);
    void process_space_weather(float dt);
    void process_evaporation(float dt);
    void process_stellar_evolution(float dt);
    void trigger_stellar_supernova(size_t index, float dt, bool thermonuclear = false,
                                   glm::vec3 impact_axis = glm::vec3(0.0f),
                                   float ejecta_speed = 0.0f);
    bool handle_stellar_collision_supernova(size_t i, size_t j, float rel_speed,
                                            float impact_energy, float escape_speed,
                                            const glm::vec3& impact_axis, float dt);
    void update_body_tracking_cache();
    int  dominant_primary_for(int body_index) const;
    glm::vec3 verlet_auto_orbit_velocity(const CelestialBody& body, const CelestialBody& primary,
                                         float radial_scale, float tangential_scale) const;
    bool spawn_dust_ring(int host_index, float total_mass, float inner_radius, float outer_radius,
                         float density, float ice_fraction, uint32_t seed_hint = 0u);
    void spawn_moons_for_host(int host_index, int moon_count);
    void spawn_ring_for_host(int host_index, float inner_mult, float outer_mult,
                             float density, float ice_fraction);
    void apply_dust_debug_mode();
    void cleanup_bodies();
    void load_persistent_settings();
    void save_persistent_settings() const;
    void spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count,
                        uint32_t parent_generation = 0, float source_temperature = 300.0f,
                        glm::vec3 impact_axis = glm::vec3(0.0f), float ejecta_speed = 0.0f,
                        const CelestialBody* source_body = nullptr, float shock_ratio = 0.0f);

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
    float smoothed_fps_ = 60.0f;
    bool  reverse_time_ = false;
    std::vector<int> tracked_primary_;
    std::vector<int> tracked_children_count_;
    std::vector<float> tracked_eccentricity_;

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
    struct SpawnDraftSettings {
        bool override_temperature = false;
        float temperature = 300.0f;
        bool override_radius = false;
        float radius = 8.0f;
        bool override_rotation = false;
        float rotation_hours = 24.0f;
        bool override_velocity = false;
        glm::vec3 velocity_kms{0.0f};
        bool override_material = false;
        float material_iron = 0.20f;
        float material_silicate = 0.60f;
        float material_ice = 0.20f;
        float material_hydrogen = 0.0f;
        int planet_look = 0; // 0 auto, 1 rocky, 2 water, 3 ice, 4 earth-like
        bool spawn_rings = false;
        bool spawn_moons = false;
        int moon_count = 1;
        bool override_ring_layout = false;
        float ring_inner_mult = 1.6f;
        float ring_outer_mult = 3.0f;
        float ring_density = 0.35f;
        float ring_ice_fraction = 0.55f;
        int small_body_spawn_count = 1; // asteroid/comet/dust only
        int small_body_layout = 0; // 0=random, 1=sphere, 2=cube, 3=torus
    };
    SpawnDraftSettings spawn_draft_;

    // Bottom bar
    float bottom_bar_offset_ = 1.0f;  // 0=visible, 1=hidden (start hidden)
    bool  bottom_bar_autohide_ = true;
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
