#pragma once
// ── Biochemical Simulator — Application ─────────────────────────────────────

#include "biochem/biochem_types.h"
#include "common/simple_renderer.h"

struct GLFWwindow;

class BiochemApp {
public:
    void init(GLFWwindow* window);
    void tick(GLFWwindow* window, float dt);
    void destroy();

    VulkanContext   vk;
    SimpleRenderer  renderer;
    BiochemConfig   cfg;
    BiochemState    state;
    bool            paused = false;

    // Input state (public for GLFW callbacks)
    int selected_entity = -1;

private:
    void render_ui();
    void render_entities();
    void step_simulation(float dt);
    void spawn_nutrient();

    // Simulation subsystems
    void process_cell_division();
    void process_virus_infection(float dt);
    void process_antibody_response(float dt);
    void process_repulsion();

    float nutrient_timer_ = 0.0f;
};
