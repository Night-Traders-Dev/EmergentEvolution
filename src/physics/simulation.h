#pragma once

#include "types.h"
#include "vulkan_context.h"
#include "particles.h"
#include "compute_pipeline.h"
#include "renderer.h"
#include "physics/interface.h"
#include "physics/achievements.h"
#include "physics/audio.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <deque>
#include <random>
#include <cstring>
#include <cmath>

// ── Spatial acceleration grid for O(N) neighbor queries ─────────────────────
struct SpatialGrid {
    static constexpr float CELL_SIZE    = 30.0f;
    static constexpr int   COLS         = static_cast<int>(WORLD_W / CELL_SIZE) + 1;  // 342
    static constexpr int   ROWS         = static_cast<int>(WORLD_H / CELL_SIZE) + 1;  // 193
    static constexpr int   TOTAL_CELLS  = COLS * ROWS;

    uint32_t cell_start[TOTAL_CELLS];
    uint32_t cell_count[TOTAL_CELLS];
    std::vector<uint32_t> indices;

    void build(const std::vector<glm::vec2>& positions,
               const std::vector<float>& energies, uint32_t n);

    template<typename Fn>
    void query(float cx, float cy, float radius, Fn&& fn) const {
        int min_col = std::max(0, static_cast<int>(std::floor((cx - radius) / CELL_SIZE)));
        int max_col = std::min(COLS - 1, static_cast<int>(std::floor((cx + radius) / CELL_SIZE)));
        int min_row = std::max(0, static_cast<int>(std::floor((cy - radius) / CELL_SIZE)));
        int max_row = std::min(ROWS - 1, static_cast<int>(std::floor((cy + radius) / CELL_SIZE)));

        for (int row = min_row; row <= max_row; ++row) {
            for (int col = min_col; col <= max_col; ++col) {
                int cell = row * COLS + col;
                uint32_t start = cell_start[cell];
                uint32_t count = cell_count[cell];
                for (uint32_t k = 0; k < count; ++k) {
                    fn(indices[start + k]);
                }
            }
        }
    }
};

// ── Undo/Redo snapshot — full simulation state for one point in time ─────────
struct UndoSnapshot {
    SimConfig cfg;
    // Per-particle data (from GPU readback + CPU)
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;
    std::vector<float>     energies;
    std::vector<uint32_t>  types;
    std::vector<float>     angles;
    std::vector<float>     angular_velocities;
    std::vector<float>     genomes;
    std::vector<uint32_t>  birth_frames;
    std::vector<int32_t>   orbital_parent;
    std::vector<int8_t>    orbital_shell;
    std::vector<uint16_t>  excitation_timer;
    std::vector<uint32_t>  entangled_partner;
    std::vector<uint8_t>   cascade_tag;
    // Per-type data
    std::vector<float>     forces;           // MAX_PARTICLE_TYPES²
    std::vector<glm::vec4> colors;           // MAX_PARTICLE_TYPES
    uint32_t behavior_flags[MAX_PARTICLE_TYPES];
    float    trait_scales[MAX_PARTICLE_TYPES];
    float    structure_integrity[MAX_PARTICLE_TYPES];
    // Force objects
    ForceObject force_objects[MAX_FORCE_OBJECTS];
    uint32_t    force_object_count;
    // Bond data
    std::vector<uint32_t> bond_data;
    // Frame counter
    uint32_t frame_counter;
    // UI field state
    bool  field_em, field_strong, field_weak, field_gravity, field_higgs, field_dark_energy;
    float field_intensity, log_temperature;
};

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
    AchievementManager achievements{};
    AudioPlayer        audio{};

    // Force objects (stationary force emitters)
    ForceObject  force_objects_[MAX_FORCE_OBJECTS] = {};
    uint32_t     force_object_count_ = 0;

private:
    // ── Undo/Redo ────────────────────────────────────────────────────────
    static constexpr uint32_t MAX_UNDO_SNAPSHOTS = 50;
    std::deque<UndoSnapshot> undo_stack_;
    std::deque<UndoSnapshot> redo_stack_;
    UndoSnapshot capture_snapshot();
    void push_undo_snapshot();
    void apply_snapshot(const UndoSnapshot& snap);
    void perform_undo();
    void perform_redo();

    glm::vec2 last_mouse_pos_      = {};
    glm::vec2 mouse_change_        = {};
    glm::vec2 smooth_mouse_change_ = {};
    bool      lmb_down_            = false;

    std::vector<glm::vec2> readback_positions_;
    std::vector<glm::vec2> readback_velocities_;
    std::vector<glm::vec2> prev_velocities_;      // previous tick velocities (for GW accel)
    std::vector<float>     readback_energies_;

    SpatialGrid grid_;
    bool cpu_particles_dirty_ = false;
    std::uniform_real_distribution<float> angle_dist_{0.0f, 6.2831853f};

    float emergent_temperature_ = 1.0f;   // EMA of kinetic temperature (Kelvin)
    float emergent_bfield_      = 0.0f;   // EMA of emergent B-field magnitude

    // ── Thermodynamic tracking (First Law: energy conservation) ─────────
    float prev_total_ke_        = 0.0f;
    float prev_total_pe_        = 0.0f;
    float prev_total_energy_    = 0.0f;
    float initial_total_energy_ = -1.0f;  // first-frame snapshot (set once)
    float energy_injected_rate_ = 0.0f;   // EMA of energy input per frame
    float energy_dissipated_rate_ = 0.0f; // EMA of energy loss per frame
    float energy_conservation_ratio_ = 1.0f;
    float energy_drift_rate_    = 0.0f;   // dE/dt (positive=gain, negative=loss)

    // ── Thermodynamic tracking (Second Law: entropy) ────────────────────
    float system_entropy_       = 0.0f;
    float prev_entropy_         = 0.0f;
    int   entropy_trend_        = 0;      // +1 increasing, 0 stable, -1 decreasing
    float entropy_trend_ema_    = 0.0f;

    // Detected nuclei (populated by update_orbitals each frame)
    struct NucleusInfo {
        glm::vec2 center;
        int Z = 0, N = 0;
        uint32_t rep = 0;
        bool is_anti = false;       // antimatter nucleus (antiprotons)
        int shell_fill[4] = {0, 0, 0, 0};  // electron shell occupancy (1s,2s2p,3s3p3d,4s4p4d4f)
        std::vector<uint32_t> proton_indices;
        std::vector<uint32_t> neutron_indices;
    };
    std::vector<NucleusInfo> detected_nuclei_;
    uint32_t nuclear_decay_count_ = 0;
    uint32_t entangled_pair_count_ = 0;

    // Covalent bond tracking (CPU-authoritative, uploaded to GPU each frame)
    std::vector<uint32_t> bond_data_;  // [particle_count * MAX_BONDS_PER_PARTICLE]

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
    void check_pion_decay();
    void check_virtual_pairs();
    void update_entanglement();
    void update_orbitals();
    void repel_distinct_nuclei();
    void update_bonds();
    void check_nuclear_decay();
    void check_photoelectric();
    void check_shell_transitions();
    void check_spallation();
    void check_hadronization();
    void check_bremsstrahlung();
    void check_neutrino_scattering();
    void check_neutrino_oscillations();
    void check_weak_flavor_change();
    void apply_gw_tidal_forces(double dt);

    uint32_t spawn_atom_at(glm::vec2 pos, int Z, int N, std::mt19937& rng, uint32_t& search_start);
    void place_force_object(glm::vec2 world_pos, ForceObjectType type);
    void place_mirror(glm::vec2 endpoint1, glm::vec2 endpoint2);
    int  hit_test_force_objects(glm::vec2 world_pos, float snap_radius);
    void recount_force_objects();
    void do_accelerator_fire(glm::vec2 aim_world_pos);
    void check_achievements();
    void try_unlock(AchievementID id);

    // Capture current render texture as a PNG thumbnail
    bool capture_thumbnail(const std::string& png_path,
                           uint32_t thumb_w = 320, uint32_t thumb_h = 180);
};
