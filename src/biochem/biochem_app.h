#pragma once
// ── Biochemical Simulator — Application ─────────────────────────────────────

#include "biochem/biochem_types.h"
#include "common/simple_renderer.h"
#include <vector>

struct GLFWwindow;

class BiochemApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();
    void reset_simulation();

    VulkanContext   vk;
    SimpleRenderer  renderer;
    BiochemConfig   cfg;
    BiochemState    state;
    bool            paused = false;

    // Fullscreen overlay state
    bool  show_splash     = true;
    bool  show_pause_menu = false;
    bool  request_quit    = false;

    // Input state (public for GLFW callbacks)
    int selected_entity = -1;

private:
    void render_ui();
    void render_entities();
    void step_simulation(float dt);
    void spawn_nutrient();

    // Fullscreen overlay screens
    void draw_splash_screen();
    void draw_pause_menu();
    void draw_menu_background();
    void draw_spawn_menu();

    // Simulation subsystems
    void process_cell_division();
    void process_virus_infection(float dt);
    void process_antibody_response(float dt);
    void process_repulsion();

    float nutrient_timer_ = 0.0f;

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
    float spawn_energy_ = 100.0f;
};
