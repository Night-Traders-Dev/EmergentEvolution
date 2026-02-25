#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include "compute_pipeline.h"
#include "renderer.h"
#include "physics/interface.h"
#include <GLFW/glfw3.h>
#include <vector>

class PhysicsSimulation;
void PhysicsSim_RegisterScrollCallback(GLFWwindow* window, PhysicsSimulation* sim);

class PhysicsSimulation {
public:
    bool is_active = true;

    void init(GLFWwindow* window);
    void destroy();
    void tick(GLFWwindow* window, double dt);
    void reset();

    SimConfig       cfg{};
    Particles       particles{};
    VulkanContext   vk{};
    ComputePipeline compute{};
    Renderer        renderer{};
    PhysicsInterface iface{};

private:
    glm::vec2 last_mouse_pos_      = {};
    glm::vec2 mouse_change_        = {};
    glm::vec2 smooth_mouse_change_ = {};
    bool      lmb_down_            = false;

    std::vector<glm::vec2> readback_positions_;
    std::vector<glm::vec2> readback_velocities_;
    std::vector<float>     readback_energies_;

    double fps_acc_       = 0.0;
    int    fps_frame_cnt_ = 0;
    uint32_t frame_counter_ = 0;

    static constexpr const char* COMPUTE_SPV = "shaders/physics.spv";
    static constexpr const char* VERT_SPV    = "shaders/fullscreen.vert.spv";
    static constexpr const char* FRAG_SPV    = "shaders/fullscreen.frag.spv";

    void handle_input(GLFWwindow* window, double dt);
    void do_spawn_at_world(glm::vec2 world_pos);
    void check_annihilation();
    void check_fusion();
    void check_fission();
    void check_decay();
    void update_orbitals();
};
