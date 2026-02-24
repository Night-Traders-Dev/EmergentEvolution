#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include "compute_pipeline.h"
#include "renderer.h"
#include "interface.h"
#include "organism.h"
#include "bond_manager.h"
#include "sub_atomic.h"
#include "decay_manager.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <unordered_set>

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
    BondManager     bond_manager{};
    SubAtomicSim    sub_atomic_sim{};
    DecayManager    decay_manager{};

private:
    // Input state
    glm::vec2 last_mouse_pos_      = {};
    glm::vec2 mouse_change_        = {};
    glm::vec2 smooth_mouse_change_ = {};
    bool      lmb_down_            = false;

    // Organism / bond tracking
    int                    organism_tick_counter_ = 0;
    int                    bond_tick_counter_     = 0;
    std::vector<glm::vec2> readback_positions_;
    std::vector<glm::vec2> readback_velocities_;
    std::vector<float>     readback_energies_;

    // Spawn protection — prevents recently placed particles from being recycled
    std::unordered_set<uint32_t> spawn_protect_ids_;
    int                          spawn_protect_ttl_ = 0;

    // Periodic particle spawn
    double                 spawn_timer_ = 0.0;

    // FPS rolling average
    double   fps_acc_       = 0.0;
    int      fps_frame_cnt_ = 0;

    // Shader SPIRV paths (relative to working directory = build dir)
    static constexpr const char* COMPUTE_SPV  = "shaders/compute.spv";
    static constexpr const char* VERT_SPV     = "shaders/fullscreen.vert.spv";
    static constexpr const char* FRAG_SPV     = "shaders/fullscreen.frag.spv";

    // Vacuum fluctuation counter (displayed in UI)
    uint32_t vacuum_total_injections_ = 0;

    void handle_input(GLFWwindow* window, double dt);
    void do_particle_spawn();
    void inject_photons(const std::vector<PhotonEvent>& events);
    void do_spawn_at_world(glm::vec2 world_pos);
    void inject_vacuum_fluctuations();
    void inject_cosmic_rays();
};
