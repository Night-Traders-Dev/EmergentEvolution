#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include "compute_pipeline.h"
#include "renderer.h"
#include "interface.h"
#include "organism.h"
#include <GLFW/glfw3.h>
#include <vector>

class Simulation;
// Call once after init() to hook the GLFW scroll callback
void Simulation_RegisterScrollCallback(GLFWwindow* window, Simulation* sim);
// Owns all subsystems, manages the main loop, handles input.

class Simulation {
public:
    bool is_active = true;

    void init(GLFWwindow* window);
    void destroy();

    // Called every frame from main()
    void tick(GLFWwindow* window, double dt);

    // Resets particle data and rebuilds GPU buffers
    void reset();

    SimConfig       cfg{};
    Particles       particles{};
    VulkanContext   vk{};
    ComputePipeline compute{};
    Renderer        renderer{};
    Interface       iface{};
    OrganismManager organism_manager{};

private:
    // Input state
    glm::vec2 last_mouse_pos_      = {};
    glm::vec2 mouse_change_        = {};
    glm::vec2 smooth_mouse_change_ = {};
    bool      lmb_down_            = false;

    // Organism tracking
    int                    organism_tick_counter_ = 0;
    std::vector<glm::vec2> readback_positions_;
    std::vector<glm::vec2> readback_velocities_;
    std::vector<float>     readback_energies_;

    // Shader SPIRV paths (relative to working directory = build dir)
    static constexpr const char* COMPUTE_SPV  = "shaders/compute.spv";
    static constexpr const char* VERT_SPV     = "shaders/fullscreen.vert.spv";
    static constexpr const char* FRAG_SPV     = "shaders/fullscreen.frag.spv";

    void handle_input(GLFWwindow* window, double dt);
};
