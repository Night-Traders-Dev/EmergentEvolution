#pragma once
// ── Cosmic Sandbox — Application ────────────────────────────────────────────

#include "cosmos/cosmos_types.h"
#include "cosmos/rendering/cosmos_raytracer.h"
#include "cosmos/rendering/cosmos_mesh_renderer.h"
#include "cosmos/rendering/cosmos_gravity_compute.h"
#include "cosmos/terrain/cosmos_terrain.h"
#include "common/simple_renderer.h"
#include "common/app_settings.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

struct GLFWwindow;

class CosmosApp {
public:
    // progress_cb(fraction 0..1, stage_label) — called between init stages
    using ProgressCB = std::function<void(float, const char*)>;
    void init(GLFWwindow* window, ProgressCB progress_cb = nullptr);
    void tick(GLFWwindow* window, float dt);
    void destroy();
    void reset_simulation();
    void load_preset(int index);
    int  spawn_at(glm::vec3 pos);  // returns index of primary spawned body, or -1
    int  spawn_preview_body(glm::vec3 pos); // spawn the current preview body at pos

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
    int pick_body(float mx, float my, float W, float H, bool skip_fragments = false) const;

    // Double-click detection
    double last_click_time_ = 0.0;
    float  last_click_x_ = 0, last_click_y_ = 0;

    // Click-to-select: deferred selection (applied on release if no drag)
    bool   click_pending_ = false;
    int    click_candidate_ = -1;
    float  click_start_x_ = 0, click_start_y_ = 0;

    // Spawn preview: ghost body shown at cursor position when spawn menu is open
    glm::vec3 spawn_preview_pos_{0.0f};
    bool is_spawn_mode() const { return spawn_menu_visible_; }

    // Spawn drag — click-and-hold to place, drag up/down to set inclination/height
    bool      spawn_dragging_ = false;       // currently in a spawn drag
    glm::vec3 spawn_drag_base_pos_{0.0f};    // XZ plane position from initial click
    float     spawn_drag_y_offset_ = 0.0f;   // accumulated Y offset from dragging

    // Re-roll the spawn preview (called on right-click in spawn mode)
    void reroll_spawn_preview();

    // QoL features (public for GLFW callbacks)
    void  duplicate_selected_body();
    bool  show_velocity_arrows_ = false;
    bool  screenshot_requested_ = false;
    bool  shortcuts_visible_ = false;
    void  capture_screenshot(GLFWwindow* window);

    // Compute spawn world position from screen coordinates
    glm::vec3 screen_to_spawn_pos(double mx, double my, int fb_w, int fb_h) const;

private:
    void render_ui();
    void render_overlay();
    void step_physics(float dt);

    // Physics subsystems
    void process_collisions(float dt);
    void process_roche_limit(float dt);
    void process_spin_fragmentation(float dt);
    void process_temperature(float dt);
    void process_material_phases(float dt);
    void process_space_weather(float dt);
    void process_evaporation(float dt);
    void process_stellar_evolution(float dt);
    void process_tidal_locking(float dt);
    void process_orbital_elements();
    void process_hawking_radiation(float dt);
    void process_yarkovsky(float dt);
    void trigger_stellar_supernova(size_t index, float dt, bool thermonuclear = false,
                                   glm::vec3 impact_axis = glm::vec3(0.0f),
                                   float ejecta_speed = 0.0f);
    bool handle_stellar_collision_supernova(size_t i, size_t j, float rel_speed,
                                            float impact_energy, float escape_speed,
                                            const glm::vec3& impact_axis, float dt);
    void update_body_tracking_cache();
    int  dominant_primary_for(int body_index) const;
    void reset_bottom_bar_menu_defaults();
    glm::vec3 verlet_auto_orbit_velocity(const CelestialBody& body, const CelestialBody& primary,
                                         float radial_scale, float tangential_scale) const;
    bool spawn_dust_ring(int host_index, float total_mass, float inner_radius, float outer_radius,
                         float density, float ice_fraction, uint32_t seed_hint = 0u,
                         int ring_style = 4);
    int  spawn_nebula_cloud(glm::vec3 center, glm::vec3 base_vel, float total_mass,
                            float cloud_radius, uint32_t seed);
    int  spawn_galaxy_cloud(glm::vec3 center, glm::vec3 base_vel, float total_mass,
                            float galaxy_radius, uint32_t galaxy_type, uint32_t seed);
    void spawn_moons_for_host(int host_index, int moon_count,
                              int orbit_layout = 0,
                              float inclination_deg = 8.0f,
                              float spacing_scale = 1.0f);
    void spawn_ring_for_host(int host_index, float inner_mult, float outer_mult,
                             float density, float ice_fraction, int ring_style = 4);
    void apply_dust_debug_mode();
    void cleanup_bodies();
    void account_escaped_mass(const CelestialBody& source, float amount,
                              float thermal_energy = 0.0f);
    bool validate_body_state(const char* context, bool pause_on_invalid = true);
    void debug_logf(const char* fmt, ...) const;
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
    CosmosMeshRenderer mesh_renderer_;
    CosmosGravityCompute gravity_compute_;
    CosmosTerrain terrain_;

    // Per-body terrain mesh cache (indexed by body index, regenerated on demand)
    std::vector<TerrainMeshEntry> terrain_cache_;
    bool terrain_meshes_dirty_ = true;
    std::vector<bool> mesh_covered_bodies_; // bodies covered by terrain mesh this frame
    void rebuild_terrain_cache();
    void upload_terrain_meshes();
    void compute_mesh_coverage(float screen_w, float screen_h);

    float sim_time_ = 0.0f;
    float smoothed_fps_ = 60.0f;
    double displayed_time_rate_ = 10.0;
    int adaptive_substeps_last_ = 1;
    int adaptive_substeps_required_ = 1;
    bool adaptive_substeps_saturated_ = false;
    bool adaptive_substep_refining_ = false;
    double escaped_mass_total_ = 0.0;
    double radiated_energy_total_ = 0.0;
    double escaped_energy_total_ = 0.0;
    glm::dvec3 escaped_momentum_total_{0.0};
    bool  reverse_time_ = false;
    std::vector<int> tracked_primary_;
    std::vector<int> tracked_children_count_;
    std::vector<float> tracked_eccentricity_;
    float cached_shortest_period_ = std::numeric_limits<float>::max();
    int   cached_shortest_period_frame_ = -1;

    // Reusable scratch buffers for step_physics (avoid per-frame heap allocations)
    std::vector<glm::vec3> scratch_pos0_, scratch_vel0_;
    std::vector<uint8_t>   scratch_source_active_;
    std::vector<float>     scratch_source_mass_, scratch_source_spin_y_;
    std::vector<float>     scratch_source_radius_, scratch_source_angular_vel_;
    std::vector<glm::vec3> scratch_source_spin_axis_;
    std::vector<size_t>    scratch_source_indices_;
    std::vector<glm::vec3> scratch_accel0_;
    // Integrator scratch
    std::vector<glm::vec3> scratch_int_pos_, scratch_int_vel_;
    std::vector<glm::vec3> scratch_int_accel_, scratch_int_accel2_;
    std::vector<glm::vec3> scratch_int_pos2_, scratch_int_vel2_;
    // Adaptive substepping scratch
    std::vector<glm::vec3> scratch_adapt_full_pos_, scratch_adapt_full_vel_;
    std::vector<glm::vec3> scratch_adapt_half_pos_, scratch_adapt_half_vel_;
    std::vector<glm::vec3> scratch_adapt_half2_pos_, scratch_adapt_half2_vel_;

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
    bool spawn_grid_was_visible_ = false;  // tracks spawn menu visibility for grid auto-toggle
    bool grid_was_on_before_spawn_ = false; // restore grid state when spawn menu closes

    // Preview body for spawn ghost (fully initialized CelestialBody)
    CelestialBody preview_body_;
    bool preview_body_valid_ = false;
    uint32_t preview_reroll_counter_ = 0;
    int  preview_last_type_ = -1;
    float preview_last_mass_ = -1.0f;
    uint32_t preview_last_draft_hash_ = 0;
    uint32_t draft_settings_hash() const;
    void build_preview_body();
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
        int star_stage_hint = -1; // -1 auto, else StellarStage enum
        int planet_look = 0; // 0 auto, 1 rocky, 2 water, 3 ice, 4 earth-like, 5 gas giant
        bool spawn_rings = false;
        bool spawn_moons = false;
        int moon_count = 1;
        int moon_orbit_layout = 0; // 0 prograde disk, 1 compact disk, 2 wide disk, 3 resonant chain, 4 isotropic cloud
        float moon_inclination_deg = 8.0f;
        float moon_spacing_scale = 1.0f;
        bool override_ring_layout = false;
        int ring_layout_type = 4; // 0=saturn, 1=uranus, 2=neptune, 3=torus, 4=realistic disk, 5=unrealistic geometries, 6=resonance gaps
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

    // Body list search filter
    char  body_search_buf_[64] = {};

    // Keyboard shortcuts overlay
    void  draw_shortcuts_overlay();

    // Save/Load UI state
    bool  show_save_dialog_   = false;
    bool  show_load_dialog_   = false;
    bool  show_export_dialog_ = false;
    bool  show_import_dialog_ = false;
    char  file_path_buf_[512] = {};
    std::string last_save_status_;
    float save_status_timer_   = 0.0f;
    bool diagnostics_enabled_ = true;
    bool diagnostics_pause_on_invalid_ = true;
    uint64_t diagnostics_step_counter_ = 0;
    void  draw_file_dialog();

    // Debug window
    bool  debug_window_visible_ = false;
    void  draw_debug_window();

    // Shared app settings (display, audio, accessibility, controls)
    AppSettings app_settings_;
    bool  show_settings_menu_ = false;
    int   settings_tab_ = 0;
};
