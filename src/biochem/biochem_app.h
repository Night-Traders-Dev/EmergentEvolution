#pragma once
// ── Biochemical Simulator — Application ─────────────────────────────────────

#include "biochem/biochem_types.h"
#include "biochem/biochem_raytracer.h"
#include "common/simple_renderer.h"
#include "common/app_settings.h"
#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

class BiochemApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();
    void reset_simulation();
    void spawn_at(glm::vec3 pos);

    VulkanContext    vk;
    SimpleRenderer   renderer;
    BiochemConfig    cfg;
    BiochemState     state;
    BiochemEnvironment environment_;
    OrbitCamera      camera;
    bool             paused = false;

    // Fullscreen overlay state
    bool  show_splash     = true;
    bool  show_pause_menu = false;
    bool  request_quit     = false;
    bool  request_launcher = false;

    // Input state (public for GLFW callbacks)
    int    selected_entity = -1;
    bool   mouse_dragging  = false;
    double last_mouse_x = 0, last_mouse_y = 0;
    double mouse_down_x = 0, mouse_down_y = 0;

private:
    void rebuild_spatial_index();
    void query_spatial_neighbors(const glm::vec3& pos, float radius, std::vector<int>& out,
                                 bool include_corpses = false) const;
    void apply_environment_preset(BioEnvironmentType env, bool reseed_population);
    void regenerate_environment(bool advance_seed);
    void reset_camera_pose();
    void render_ui();
    void render_overlay();
    void step_simulation(float dt);
    void process_autospawn(float dt);
    void spawn_nutrient();
    bool spawn_autospawn_entity();
    size_t count_alive_matching_spawn_selection() const;
    float compute_autospawn_rate(size_t matching_alive) const;
    void assign_entity_identity(BioEntity& e, uint32_t generation = 0, uint32_t parent_id = 0);
    void push_event(uint32_t type, const std::string& text);
    void mark_entity_corpse(BioEntity& e, uint32_t event_type, const std::string& reason);
    void reset_population_metrics();

    // Fullscreen overlay screens
    void draw_splash_screen();
    void draw_pause_menu();
    void draw_menu_background();
    void draw_spawn_menu();

    // Simulation subsystems
    void process_cell_division(float dt);
    void process_bacteria_antibiotics(float dt);
    void process_bacterial_colonization(float dt);
    void process_gene_exchange(float dt);
    void process_virus_infection(float dt);
    void process_antibody_response(float dt);
    void process_complement_cascade(float dt);
    void process_phagocyte_cleanup(float dt);
    void process_repulsion();
    void process_structure_collision();
    void process_ai_movement(float dt);

    float nutrient_timer_ = 0.0f;
    float autospawn_timer_ = 0.0f;
    uint32_t next_entity_id_ = 1;
    uint32_t next_species_key_ = 1024;
    uint32_t ever_infected_total_ = 0;
    std::array<uint32_t, BIO_TYPE_COUNT> ever_infected_by_type_{};
    std::unordered_map<uint64_t, uint32_t> ever_infected_by_subtype_;
    std::array<uint32_t, BIO_TYPE_COUNT> ever_pathogen_infections_by_type_{};
    std::unordered_map<uint64_t, uint32_t> ever_pathogen_infections_by_subtype_;
    float spatial_cell_size_ = 18.0f;
    float spatial_max_radius_ = 0.0f;
    std::unordered_map<int64_t, std::vector<int>> spatial_buckets_;

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
    int  spawn_bio_type_ = BIO_CELL;
    int  spawn_variant_ = BIO_CELL_GENERIC_ANIMAL;
    float spawn_energy_ = 100.0f;

    // 3D rendering
    BiochemRaytracer raytracer_;
    float sim_time_ = 0.0f;

    // Bottom bar
    float bottom_bar_offset_ = 1.0f;  // 0=visible, 1=hidden (start hidden)
    bool  show_menu_popup_   = false;
    bool  settings_visible_  = true;
    bool  population_visible_ = true;
    bool  genomics_visible_ = true;
    bool  event_log_visible_ = true;
    bool  event_log_auto_scroll_ = true;
    bool  event_log_dirty_ = false;
    struct EventLogEntry {
        float time = 0.0f;
        uint32_t type = 0;
        std::string text;
    };
    std::deque<EventLogEntry> event_log_;
    void  draw_bottom_bar();

    // Shared app settings (display, audio, accessibility, controls)
    AppSettings app_settings_;
    bool  show_settings_menu_ = false;
    int   settings_tab_ = 0;
};
