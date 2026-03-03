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

private:
    void render_ui();
    void step_simulation(float dt);
    void spawn_nutrient();

    float nutrient_timer_ = 0.0f;
};
