#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include "physics/molecules.h"
#include "physics/save_load.h"
#include "physics/sim_helpers.h"
#include "stb_image_write.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <map>
#include <unordered_map>
#ifdef HAS_OPENMP
#include <omp.h>
#endif
#ifdef PORTABLE_BUILD
#include "embedded_resources.h"
#endif

namespace fs = std::filesystem;


// ── Scroll callback ──────────────────────────────────────────────────────────

static PhysicsSimulation* g_phys_sim = nullptr;

static void scroll_callback(GLFWwindow*, double, double yoffset) {
    if (!g_phys_sim) return;
    if (ImGui::GetIO().WantCaptureMouse) return;
    float& zoom = g_phys_sim->cfg.camera_zoom;
    zoom *= (yoffset > 0) ? 1.1f : (1.0f / 1.1f);
    zoom = std::clamp(zoom, 0.05f, 500.0f);
}

void PhysicsSim_RegisterScrollCallback(GLFWwindow* window, PhysicsSimulation* sim) {
    g_phys_sim = sim;
    glfwSetScrollCallback(window, scroll_callback);
}

// ── Spatial Grid ────────────────────────────────────────────────────────────

void SpatialGrid::build(const std::vector<glm::vec2>& positions,
                         const std::vector<float>& energies, uint32_t n)
{
    indices.resize(n);
    std::memset(cell_count, 0, sizeof(cell_count));

    // Count particles per cell
    for (uint32_t i = 0; i < n; ++i) {
        if (energies[i] < 0.01f) continue;
        int col = std::clamp(static_cast<int>(positions[i].x / CELL_SIZE), 0, COLS - 1);
        int row = std::clamp(static_cast<int>(positions[i].y / CELL_SIZE), 0, ROWS - 1);
        cell_count[row * COLS + col]++;
    }

    // Prefix sum → cell_start
    uint32_t acc = 0;
    for (int c = 0; c < TOTAL_CELLS; ++c) {
        cell_start[c] = acc;
        acc += cell_count[c];
    }

    // Scatter — use a temporary offset array
    uint32_t offsets[TOTAL_CELLS];
    std::memcpy(offsets, cell_start, sizeof(cell_start));

    for (uint32_t i = 0; i < n; ++i) {
        if (energies[i] < 0.01f) continue;
        int col = std::clamp(static_cast<int>(positions[i].x / CELL_SIZE), 0, COLS - 1);
        int row = std::clamp(static_cast<int>(positions[i].y / CELL_SIZE), 0, ROWS - 1);
        int cell = row * COLS + col;
        indices[offsets[cell]++] = i;
    }
}

// ── GPU grid build (100px cells, prefix-sum format for shader) ──────────────

void GPUGrid::build(const std::vector<glm::vec2>& positions,
                    const std::vector<float>& energies, uint32_t n)
{
    cell_start.assign(TOTAL_CELLS + 1, 0);
    sorted_indices.resize(n);

    // Pass 1: count per cell (offset by 1 for prefix-sum)
    for (uint32_t i = 0; i < n; ++i) {
        if (energies[i] < 0.001f) continue;  // skip dormant
        int col = std::clamp(static_cast<int>(positions[i].x / CELL_SIZE), 0, COLS - 1);
        int row = std::clamp(static_cast<int>(positions[i].y / CELL_SIZE), 0, ROWS - 1);
        cell_start[static_cast<size_t>(row * COLS + col) + 1]++;
    }

    // Pass 2: prefix sum
    for (int c = 1; c <= TOTAL_CELLS; ++c)
        cell_start[c] += cell_start[c - 1];

    // Pass 3: scatter using temporary offsets
    std::vector<uint32_t> offsets(cell_start.begin(), cell_start.begin() + TOTAL_CELLS);
    for (uint32_t i = 0; i < n; ++i) {
        if (energies[i] < 0.001f) continue;
        int col = std::clamp(static_cast<int>(positions[i].x / CELL_SIZE), 0, COLS - 1);
        int row = std::clamp(static_cast<int>(positions[i].y / CELL_SIZE), 0, ROWS - 1);
        int cell = row * COLS + col;
        sorted_indices[offsets[cell]++] = i;
    }
}

// ── Init / Destroy ───────────────────────────────────────────────────────────

void PhysicsSimulation::init(GLFWwindow* window) {
    // Defaults — all forces active, 1K temperature, all fields visualized
    cfg.particle_count     = 100000;
    cfg.particle_types     = PHYS_PARTICLE_TYPES;
    cfg.start_empty        = true;
    cfg.environment_mode   = 0;  // Lab Mode
    cfg.pool_size          = 100000;
    cfg.temperature_kelvin = 1.0f;
    cfg.temperature        = 0.30f;
    cfg.thermo_coupling    = 1.0f;
    cfg.dampening          = 0.990f;
    cfg.repulsion_radius   = 1.0f;
    cfg.interaction_radius = 200.0f;
    cfg.pressure_resistance = 100.0f;
    cfg.gravity_strength   = 1.0f;
    cfg.lorentz_strength   = 1.0f;
    cfg.magnetic_coupling  = 1.0f;
    cfg.radius             = 1.0f;
    cfg.density_limit      = 0.0f;
    cfg.local_density_cap  = 0.5f;
    cfg.viscosity_strength = 0.0f;
    cfg.string_tension     = 100.0f;
    cfg.weak_coupling      = 1.0f;
    cfg.higgs_vev          = 246.0f;
    cfg.virtual_pair_threshold    = 2.1f;
    cfg.virtual_pair_max_per_tick = 2;
    cfg.time_scale         = 1.0f;
    cfg.field_flags        = 0;  // assembled from iface bools each frame

    iface.init();
    iface.achievements_ptr = &achievements;
    iface.audio_ptr = &audio;
    cfg.generation_seed = static_cast<uint32_t>(iface.seed_value);

    // Load persistent achievements from disk
    fs::create_directories("saves");
    achievements.load("saves/achievements.ppach");

    vk.init(window);

#ifdef PORTABLE_BUILD
    // Register embedded shaders so file-based loading falls back to built-in data
    vk.register_embedded_shader(COMPUTE_SPV, physics_spv_data, physics_spv_size);
    vk.register_embedded_shader(VERT_SPV,    fullscreen_vert_spv_data, fullscreen_vert_spv_size);
    vk.register_embedded_shader(FRAG_SPV,    fullscreen_frag_spv_data, fullscreen_frag_spv_size);
#endif

    compute.init(vk, COMPUTE_SPV);
    renderer.init(vk, window, compute);

    // Give interface a pointer to VulkanContext for thumbnail loading
    iface.set_vk_ctx(&vk);

    // Background music — try multiple paths (CWD may be project root or build/)
    if (!audio.init("assets/sound.mp3"))
        audio.init("../assets/sound.mp3");
    audio.set_volume(iface.prefs.music_volume);
    if (iface.prefs.music_muted) audio.pause();

    reset();
}

void PhysicsSimulation::destroy() {
    audio.destroy();
    achievements.save("saves/achievements.ppach");
    vkDeviceWaitIdle(vk.device);
    compute.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

// ── Undo/Redo ────────────────────────────────────────────────────────────────

UndoSnapshot PhysicsSimulation::capture_snapshot() {
    // Ensure fresh GPU readback
    uint32_t n = cfg.particle_count;
    if ((readback_positions_.size() != n || !readback_fresh_) && compute.is_ready()) {
        readback_positions_.resize(n);
        readback_velocities_.resize(n);
        readback_energies_.resize(n);
        compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        readback_fresh_ = true;
    }

    UndoSnapshot snap;
    snap.cfg = cfg;
    snap.positions = readback_positions_;
    snap.velocities = readback_velocities_;
    snap.energies = readback_energies_;
    snap.types = particles.types;
    snap.angles = particles.angles;
    snap.angular_velocities = particles.angular_velocities;
    snap.genomes = particles.genomes;
    snap.birth_frames = particles.birth_frames;
    snap.orbital_parent = particles.orbital_parent;
    snap.orbital_shell = particles.orbital_shell;
    snap.excitation_timer = particles.excitation_timer;
    snap.entangled_partner = particles.entangled_partner;
    snap.cascade_tag = particles.cascade_tag;
    snap.forces = particles.forces;
    snap.colors = particles.colors;
    std::memcpy(snap.behavior_flags, particles.behavior_flags, sizeof(snap.behavior_flags));
    std::memcpy(snap.trait_scales, particles.trait_scales, sizeof(snap.trait_scales));
    std::memcpy(snap.structure_integrity, particles.structure_integrity, sizeof(snap.structure_integrity));
    std::memcpy(snap.force_objects, force_objects_, sizeof(snap.force_objects));
    snap.force_object_count = force_object_count_;
    snap.bond_data = bond_data_;
    snap.frame_counter = frame_counter_;
    snap.field_em = iface.field_em;
    snap.field_strong = iface.field_strong;
    snap.field_weak = iface.field_weak;
    snap.field_gravity = iface.field_gravity;
    snap.field_higgs = iface.field_higgs;
    snap.field_dark_energy = iface.field_dark_energy;
    snap.field_intensity = iface.field_intensity;
    snap.log_temperature = iface.log_temperature;
    return snap;
}

void PhysicsSimulation::push_undo_snapshot() {
    undo_stack_.push_back(capture_snapshot());
    if (undo_stack_.size() > MAX_UNDO_SNAPSHOTS)
        undo_stack_.pop_front();
    redo_stack_.clear();
}

void PhysicsSimulation::apply_snapshot(const UndoSnapshot& snap) {
    vkDeviceWaitIdle(vk.device);
    cfg = snap.cfg;
    // Restore per-particle CPU data
    particles.positions = snap.positions;
    particles.velocities = snap.velocities;
    particles.types = snap.types;
    particles.energies = snap.energies;
    particles.angles = snap.angles;
    particles.angular_velocities = snap.angular_velocities;
    particles.genomes = snap.genomes;
    particles.birth_frames = snap.birth_frames;
    particles.orbital_parent = snap.orbital_parent;
    particles.orbital_shell = snap.orbital_shell;
    particles.excitation_timer = snap.excitation_timer;
    particles.entangled_partner = snap.entangled_partner;
    particles.cascade_tag = snap.cascade_tag;
    // Restore per-type data
    std::memcpy(particles.forces.data(), snap.forces.data(),
                MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
    std::memcpy(particles.colors.data(), snap.colors.data(),
                MAX_PARTICLE_TYPES * sizeof(glm::vec4));
    std::memcpy(particles.behavior_flags, snap.behavior_flags,
                MAX_PARTICLE_TYPES * sizeof(uint32_t));
    std::memcpy(particles.trait_scales, snap.trait_scales,
                MAX_PARTICLE_TYPES * sizeof(float));
    std::memcpy(particles.structure_integrity, snap.structure_integrity,
                MAX_PARTICLE_TYPES * sizeof(float));
    // Restore force objects
    std::memcpy(force_objects_, snap.force_objects, sizeof(force_objects_));
    force_object_count_ = snap.force_object_count;
    // Restore bond data
    bond_data_ = snap.bond_data;
    if (!bond_data_.empty()) {
        particles.bond_partners_ptr = bond_data_.data();
        particles.bond_partners_count = static_cast<uint32_t>(bond_data_.size());
    } else {
        particles.bond_partners_ptr = nullptr;
        particles.bond_partners_count = 0;
    }
    // Restore frame counter
    frame_counter_ = snap.frame_counter;
    // Restore UI field state
    iface.field_em = snap.field_em;
    iface.field_strong = snap.field_strong;
    iface.field_weak = snap.field_weak;
    iface.field_gravity = snap.field_gravity;
    iface.field_higgs = snap.field_higgs;
    iface.field_dark_energy = snap.field_dark_energy;
    iface.field_intensity = snap.field_intensity;
    iface.log_temperature = snap.log_temperature;
    // Re-upload to GPU
    compute.clear_buffers(vk);
    compute.create_buffers(vk, particles);
    // Reset interaction state
    iface.selected_force_obj_idx = -1;
    iface.force_obj_placement_mode = false;
    iface.force_obj_move_mode = false;
    iface.selected_particle_idx = -1;
    iface.particle_move_mode = false;
    iface.select_mode = false;
    readback_positions_.clear();
    readback_velocities_.clear();
    readback_energies_.clear();
    readback_fresh_ = false;
}

void PhysicsSimulation::perform_undo() {
    if (undo_stack_.empty()) {
        std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
        strncpy(iface.save_load_message, "Nothing to undo", sizeof(iface.save_load_message) - 1);
        iface.save_load_msg_timer = 2.0f;
        return;
    }
    redo_stack_.push_back(capture_snapshot());
    UndoSnapshot snap = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    apply_snapshot(snap);

    char msg[64];
    snprintf(msg, sizeof(msg), "Undo (%d remaining)", static_cast<int>(undo_stack_.size()));
    std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
    strncpy(iface.save_load_message, msg, sizeof(iface.save_load_message) - 1);
    iface.save_load_msg_timer = 2.0f;
}

void PhysicsSimulation::perform_redo() {
    if (redo_stack_.empty()) {
        std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
        strncpy(iface.save_load_message, "Nothing to redo", sizeof(iface.save_load_message) - 1);
        iface.save_load_msg_timer = 2.0f;
        return;
    }
    undo_stack_.push_back(capture_snapshot());
    UndoSnapshot snap = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    apply_snapshot(snap);

    char msg[64];
    snprintf(msg, sizeof(msg), "Redo (%d remaining)", static_cast<int>(redo_stack_.size()));
    std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
    strncpy(iface.save_load_message, msg, sizeof(iface.save_load_message) - 1);
    iface.save_load_msg_timer = 2.0f;
}

// ── Reset ────────────────────────────────────────────────────────────────────

void PhysicsSimulation::reset() {
    vkDeviceWaitIdle(vk.device);
    frame_counter_ = 0;
    emergent_temperature_ = 1.0f;
    emergent_bfield_ = 0.0f;
    prev_total_ke_ = 0.0f;
    prev_total_pe_ = 0.0f;
    prev_total_energy_ = 0.0f;
    initial_total_energy_ = -1.0f;
    energy_injected_rate_ = 0.0f;
    energy_dissipated_rate_ = 0.0f;
    energy_conservation_ratio_ = 1.0f;
    energy_drift_rate_ = 0.0f;
    system_entropy_ = 0.0f;
    prev_entropy_ = 0.0f;
    entropy_trend_ = 0;
    entropy_trend_ema_ = 0.0f;

    // Clear force objects
    force_object_count_ = 0;
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i)
        force_objects_[i] = {};
    iface.selected_force_obj_idx = -1;
    iface.force_obj_placement_mode = false;
    iface.force_obj_move_mode = false;
    iface.selected_particle_idx = -1;
    iface.particle_move_mode = false;
    iface.request_delete_particle = false;
    iface.select_mode = false;
    iface.accel_mode = false;
    iface.accel_phase = 0;
    iface.accel_source_idx = -1;
    iface.accel_stream_timer = 0;
    iface.mirror_placement_mode = false;
    iface.mirror_placement_phase = 0;
    iface.thermo_probe_placement_mode = false;
    iface.velocity_meter_mode = false;
    iface.ruler_placement_mode = false;
    iface.ruler_placement_phase = 0;
    iface.density_counter_placement_mode = false;
    iface.thermo_probes.clear();
    iface.velocity_meters.clear();
    iface.distance_rulers.clear();
    iface.density_counters.clear();
    iface.trajectory_history.clear();
    iface.force_contributions.clear();
    iface.gw_rings.clear();
    for (auto& s : iface.type_stats) s = {};
    for (auto& s : iface.element_stats) s = {};
    iface.molecule_bestiary_session++;
    prev_velocities_.clear();
    iface.show_trajectory_tracer = false;
    iface.show_energy_heatmap = false;
    iface.show_velocity_field = false;
    iface.show_force_vectors = false;
    entangled_pair_count_ = 0;

    physics_gen_data(particles, cfg);
    cfg.particle_count = static_cast<uint32_t>(particles.positions.size());

    // Particle Accelerator: auto-place EM force objects as bending magnets
    if (cfg.environment_mode == 10) {
        float cx = static_cast<float>(WORLD_W) * 0.5f;
        float cy = static_cast<float>(WORLD_H) * 0.5f;
        float rx = static_cast<float>(REGION_W) * 0.35f;
        float ry = static_cast<float>(REGION_H) * 0.35f;
        // Place 8 EM magnets evenly around the ring
        for (uint32_t m = 0; m < MAX_FORCE_OBJECTS; ++m) {
            float theta = static_cast<float>(m) * 6.2831853f / static_cast<float>(MAX_FORCE_OBJECTS);
            force_objects_[m].x = cx + std::cos(theta) * rx;
            force_objects_[m].y = cy + std::sin(theta) * ry;
            force_objects_[m].strength = 3.0f;
            force_objects_[m].radius = 120.0f;
            force_objects_[m].force_type = FORCE_OBJ_EM_FIELD;
            force_objects_[m].active = 1;
        }
        recount_force_objects();
    }

    // Reset bond data
    bond_data_.clear();
    particles.bond_partners_ptr = nullptr;
    particles.bond_partners_count = 0;

    compute.clear_buffers(vk);
    compute.create_buffers(vk, particles);
}

// ── Input handling ───────────────────────────────────────────────────────────

void PhysicsSimulation::handle_input(GLFWwindow* window, double dt) {
    // Camera smoothing
    cfg.current_camera_zoom += (cfg.camera_zoom - cfg.current_camera_zoom)
                               * std::min(1.0f, static_cast<float>(dt) * 8.0f);

    // Mouse handling
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    glm::vec2 mouse_pos(static_cast<float>(mx), static_cast<float>(my));
    mouse_change_ = mouse_pos - last_mouse_pos_;
    smooth_mouse_change_ = glm::mix(smooth_mouse_change_, mouse_change_, 0.3f);
    last_mouse_pos_ = mouse_pos;

    bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    // Camera pan (right mouse)
    if (rmb && !ImGui::GetIO().WantCaptureMouse) {
        cfg.camera_origin -= smooth_mouse_change_ / cfg.current_camera_zoom;
    }

    // WASD camera movement
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        float cam_speed = 400.0f / cfg.current_camera_zoom * static_cast<float>(dt);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cfg.camera_origin.y -= cam_speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cfg.camera_origin.y += cam_speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cfg.camera_origin.x -= cam_speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cfg.camera_origin.x += cam_speed;
    }

    // F3 toggle spawn menu
    static bool f3_was_down = false;
    bool f3_down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
    if (f3_down && !f3_was_down) {
        iface.spawn_menu_visible = !iface.spawn_menu_visible;
    }
    f3_was_down = f3_down;

    // F4 toggle select mode
    static bool f4_was_down = false;
    bool f4_down = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
    if (f4_down && !f4_was_down) {
        iface.select_mode = !iface.select_mode;
        if (iface.select_mode) { iface.pending_spawn = false; iface.force_obj_placement_mode = false; }
    }
    f4_was_down = f4_down;

    // Escape to toggle pause/settings menu
    static bool esc_was = false;
    bool esc_now = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (esc_now && !esc_was) {
        if (iface.show_settings_menu) {
            iface.show_settings_menu = false;
            iface.show_pause_menu = true;
            iface.save_prefs();
        } else if (iface.show_achievements_panel) {
            iface.show_achievements_panel = false;
            iface.show_pause_menu = true;
        } else {
            iface.show_pause_menu = !iface.show_pause_menu;
            if (iface.show_pause_menu) is_active = false;  // pause on open
        }
    }
    esc_was = esc_now;

    // Space to toggle sim
    static bool space_was = false;
    bool space_now = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    if (space_now && !space_was) is_active = !is_active;
    space_was = space_now;

    // F2 reset
    static bool f2_was = false;
    bool f2_now = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
    if (f2_now && !f2_was) {
        push_undo_snapshot();
        if (!cfg.start_empty) {
            int pc = static_cast<int>(std::max(2.0f,
                std::pow(iface.particle_count_slider, 2.0f)));
            cfg.particle_count = static_cast<uint32_t>(pc);
        } else {
            cfg.particle_count = cfg.pool_size;
        }
        reset();
    }
    f2_was = f2_now;

    // Click handling priority:
    // 1. Force obj move  1.5. Element move  2. Particle move  3. Force obj place
    // 4. Particle spawn  5. Select force obj or particle
    if (lmb && !lmb_down_ && !ImGui::GetIO().WantCaptureMouse) {
        int win_w, win_h;
        glfwGetFramebufferSize(window, &win_w, &win_h);
        // Scale from screen coords to render texture coords (render texture is REGION_W × REGION_H)
        float sx = static_cast<float>(REGION_W) / static_cast<float>(win_w);
        float sy = static_cast<float>(REGION_H) / static_cast<float>(win_h);
        glm::vec2 world_pos = cfg.camera_origin
            + (mouse_pos - glm::vec2(win_w * 0.5f, win_h * 0.5f)) * glm::vec2(sx, sy) / cfg.current_camera_zoom;

        if (iface.force_obj_move_mode && iface.selected_force_obj_idx >= 0) {
            push_undo_snapshot();
            // 1. Reposition selected force object
            int idx = iface.selected_force_obj_idx;
            if (force_objects_[idx].force_type == FORCE_OBJ_MIRROR) {
                // Translate both endpoints by delta from midpoint
                glm::vec2 mid((force_objects_[idx].x + force_objects_[idx]._pad0) * 0.5f,
                              (force_objects_[idx].y + force_objects_[idx]._pad1) * 0.5f);
                glm::vec2 delta = world_pos - mid;
                force_objects_[idx].x     += delta.x;
                force_objects_[idx].y     += delta.y;
                force_objects_[idx]._pad0 += delta.x;
                force_objects_[idx]._pad1 += delta.y;
            } else {
                force_objects_[idx].x = world_pos.x;
                force_objects_[idx].y = world_pos.y;
            }
            iface.force_obj_move_mode = false;
        }
        else if (iface.element_move_mode && iface.element_card_nucleus_rep >= 0) {
            push_undo_snapshot();
            // 1.5. Move entire element to click position
            int32_t nuc_rep = iface.element_card_nucleus_rep;
            glm::vec2 center(0.0f);
            int count = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(readback_positions_.size()); ++i) {
                if (readback_energies_[i] <= 0.0f) continue;
                if (static_cast<int32_t>(i) == nuc_rep || particles.orbital_parent[i] == nuc_rep) {
                    center += readback_positions_[i];
                    count++;
                }
            }
            if (count > 0) {
                center /= static_cast<float>(count);
                glm::vec2 delta = world_pos - center;
                float rw = static_cast<float>(WORLD_W);
                float rh = static_cast<float>(WORLD_H);
                for (uint32_t i = 0; i < static_cast<uint32_t>(readback_positions_.size()); ++i) {
                    if (readback_energies_[i] <= 0.0f) continue;
                    if (static_cast<int32_t>(i) == nuc_rep || particles.orbital_parent[i] == nuc_rep) {
                        glm::vec2 p = readback_positions_[i] + delta;
                        p.x = std::fmod(p.x + rw, rw);
                        p.y = std::fmod(p.y + rh, rh);
                        readback_positions_[i] = p;
                    }
                }
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }
            iface.element_move_mode = false;
        }
        else if (iface.particle_move_mode && iface.selected_particle_idx >= 0) {
            push_undo_snapshot();
            // 2. Reposition selected particle
            uint32_t pi = static_cast<uint32_t>(iface.selected_particle_idx);
            if (pi < readback_positions_.size()) {
                readback_positions_[pi] = world_pos;
                readback_velocities_[pi] = glm::vec2(0.0f);  // stop it
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }
            iface.particle_move_mode = false;
        }
        else if (iface.accel_mode) {
            // 3.5. Particle Accelerator (fire AT the selected target or free-fire)
            if (iface.accel_phase == 0) {
                // Select target particle
                float snap_r = std::max(cfg.radius * 3.0f + 4.0f, 15.0f / cfg.current_camera_zoom);
                float min_d2 = snap_r * snap_r;
                int32_t best = -1;
                for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                    if (pi < readback_energies_.size() && readback_energies_[pi] <= 0.0f) continue;
                    glm::vec2 d = readback_positions_[pi] - world_pos;
                    float d2 = d.x * d.x + d.y * d.y;
                    if (d2 < min_d2) { min_d2 = d2; best = static_cast<int32_t>(pi); }
                }
                if (best >= 0) {
                    iface.accel_source_idx = best;
                    iface.accel_phase = 1;
                    iface.push_notification("Target set - aim and fire!",
                                            ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
                }
            } else if (iface.accel_phase == 1 && iface.accel_fire_mode != 2) {
                // Single or Triple shot on click
                push_undo_snapshot();
                do_accelerator_fire(world_pos);
            }
        }
        else if (iface.mirror_placement_mode) {
            // 3.6. Mirror placement (two-click)
            if (iface.mirror_placement_phase == 0) {
                iface.mirror_endpoint1 = world_pos;
                iface.mirror_placement_phase = 1;
            } else {
                push_undo_snapshot();
                place_mirror(iface.mirror_endpoint1, world_pos);
                iface.mirror_placement_phase = 0;
                iface.mirror_placement_mode = false;
            }
        }
        else if (iface.thermo_probe_placement_mode) {
            // 3.7. Thermometer probe placement (single click)
            if (iface.thermo_probes.size() < 8) {
                ThermometerProbe probe;
                probe.world_pos = world_pos;
                iface.thermo_probes.push_back(probe);
            }
            iface.thermo_probe_placement_mode = false;
        }
        else if (iface.velocity_meter_mode) {
            // 3.8. Velocity meter (click on particle)
            float snap_r = std::max(cfg.radius * 3.0f + 4.0f, 15.0f / cfg.current_camera_zoom);
            float min_d2 = snap_r * snap_r;
            int32_t best = -1;
            for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                if (pi < readback_energies_.size() && readback_energies_[pi] <= 0.0f) continue;
                glm::vec2 d = readback_positions_[pi] - world_pos;
                float d2 = d.x * d.x + d.y * d.y;
                if (d2 < min_d2) { min_d2 = d2; best = static_cast<int32_t>(pi); }
            }
            if (best >= 0) {
                VelocityMeterTarget vm;
                vm.particle_idx = best;
                iface.velocity_meters.push_back(vm);
            }
            iface.velocity_meter_mode = false;
        }
        else if (iface.ruler_placement_mode) {
            // 3.9. Distance ruler placement (two-click)
            if (iface.ruler_placement_phase == 0) {
                iface.ruler_point_a = world_pos;
                iface.ruler_placement_phase = 1;
            } else {
                DistanceRuler ruler;
                ruler.point_a = iface.ruler_point_a;
                ruler.point_b = world_pos;
                ruler.distance = glm::length(ruler.point_b - ruler.point_a);
                iface.distance_rulers.push_back(ruler);
                iface.ruler_placement_phase = 0;
                iface.ruler_placement_mode = false;
            }
        }
        else if (iface.density_counter_placement_mode) {
            // 3.10. Density counter placement (single click)
            if (iface.density_counters.size() < 8) {
                DensityCounter dc;
                dc.world_pos = world_pos;
                iface.density_counters.push_back(dc);
            }
            iface.density_counter_placement_mode = false;
        }
        else if (iface.force_obj_placement_mode) {
            // 3. Place new force object
            push_undo_snapshot();
            place_force_object(world_pos, static_cast<ForceObjectType>(iface.force_obj_placement_type));
            iface.force_obj_placement_mode = false;
        }
        else if (iface.pending_spawn) {
            // 4. Spawn particle
            push_undo_snapshot();
            do_spawn_at_world(world_pos);
        }
        else if (iface.select_mode) {
            // 5. Try to select a force object first, then a particle
            int fo_hit = hit_test_force_objects(world_pos, 15.0f / cfg.current_camera_zoom);
            if (fo_hit >= 0) {
                iface.selected_force_obj_idx = fo_hit;
                iface.selected_particle_idx = -1;
            } else {
                iface.selected_force_obj_idx = -1;
                // Try to select a particle (same snap radius as hover)
                float snap_r = std::max(cfg.radius * 3.0f + 4.0f, 15.0f / cfg.current_camera_zoom);
                float min_d2 = snap_r * snap_r;
                int32_t best = -1;
                for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                    if (pi < readback_energies_.size() && readback_energies_[pi] <= 0.0f) continue;
                    glm::vec2 d = readback_positions_[pi] - world_pos;
                    float d2 = d.x * d.x + d.y * d.y;
                    if (d2 < min_d2) { min_d2 = d2; best = static_cast<int32_t>(pi); }
                }
                iface.selected_particle_idx = best;
                iface.particle_move_mode = false;
            }
        }
    }
    lmb_down_ = lmb;

    // ── Accelerator stream mode (continuous fire while LMB held) ────────────
    if (lmb && lmb_down_ && iface.accel_mode && iface.accel_phase == 1
        && iface.accel_fire_mode == 2 && !ImGui::GetIO().WantCaptureMouse)
    {
        iface.accel_stream_timer++;
        if (iface.accel_stream_timer >= iface.accel_stream_interval) {
            iface.accel_stream_timer = 0;
            // Recompute world_pos from current mouse
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            int ww, wh;
            glfwGetFramebufferSize(window, &ww, &wh);
            float asx = static_cast<float>(REGION_W) / static_cast<float>(ww);
            float asy = static_cast<float>(REGION_H) / static_cast<float>(wh);
            glm::vec2 aim_world = cfg.camera_origin +
                (glm::vec2(static_cast<float>(mx), static_cast<float>(my))
                 - glm::vec2(ww * 0.5f, wh * 0.5f)) * glm::vec2(asx, asy) / cfg.current_camera_zoom;
            do_accelerator_fire(aim_world);
        }
    }
    if (!lmb) iface.accel_stream_timer = 0;
}


// ── Force object management ──────────────────────────────────────────────────

void PhysicsSimulation::place_force_object(glm::vec2 world_pos, ForceObjectType type) {
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i) {
        if (!force_objects_[i].active) {
            force_objects_[i].x          = world_pos.x;
            force_objects_[i].y          = world_pos.y;
            force_objects_[i].strength   = 1.0f;
            force_objects_[i].radius     = 80.0f;
            force_objects_[i].force_type = static_cast<uint32_t>(type);
            force_objects_[i].active     = 1;
            force_objects_[i]._pad0      = 0.0f;
            force_objects_[i]._pad1      = 0.0f;
            recount_force_objects();
            iface.selected_force_obj_idx = static_cast<int>(i);
            try_unlock(ACH_FIRST_FORCE_OBJ);
            return;
        }
    }
}

void PhysicsSimulation::place_mirror(glm::vec2 endpoint1, glm::vec2 endpoint2) {
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i) {
        if (!force_objects_[i].active) {
            force_objects_[i].x          = endpoint1.x;
            force_objects_[i].y          = endpoint1.y;
            force_objects_[i].strength   = 1.0f;   // elasticity (1.0 = perfect bounce)
            force_objects_[i].radius     = 5.0f;    // collision thickness
            force_objects_[i].force_type = FORCE_OBJ_MIRROR;
            force_objects_[i].active     = 1;
            force_objects_[i]._pad0      = endpoint2.x;
            force_objects_[i]._pad1      = endpoint2.y;
            recount_force_objects();
            iface.selected_force_obj_idx = static_cast<int>(i);
            iface.push_notification("Mirror placed",
                                    ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
            try_unlock(ACH_FIRST_MIRROR);
            return;
        }
    }
    iface.push_notification("Max force objects reached!",
                            ImVec4(1.0f, 0.4f, 0.3f, 1.0f));
}

int PhysicsSimulation::hit_test_force_objects(glm::vec2 world_pos, float snap_radius) {
    float best_d2 = snap_radius * snap_radius;
    int best = -1;
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i) {
        if (!force_objects_[i].active) continue;
        float d2;
        if (force_objects_[i].force_type == FORCE_OBJ_MIRROR) {
            // Point-to-segment distance for mirrors
            glm::vec2 a(force_objects_[i].x, force_objects_[i].y);
            glm::vec2 b(force_objects_[i]._pad0, force_objects_[i]._pad1);
            glm::vec2 ab = b - a;
            float ab_len2 = glm::dot(ab, ab);
            float t = (ab_len2 > 0.001f)
                ? glm::clamp(glm::dot(world_pos - a, ab) / ab_len2, 0.0f, 1.0f) : 0.0f;
            glm::vec2 closest = a + t * ab;
            glm::vec2 diff = world_pos - closest;
            d2 = glm::dot(diff, diff);
        } else {
            glm::vec2 d(force_objects_[i].x - world_pos.x, force_objects_[i].y - world_pos.y);
            d2 = d.x * d.x + d.y * d.y;
        }
        if (d2 < best_d2) { best_d2 = d2; best = static_cast<int>(i); }
    }
    return best;
}

void PhysicsSimulation::recount_force_objects() {
    force_object_count_ = 0;
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i)
        if (force_objects_[i].active) force_object_count_++;
}

// ── Achievement helpers ──────────────────────────────────────────────────────

void PhysicsSimulation::try_unlock(AchievementID id) {
    if (achievements.unlock(id)) {
        const auto& def = ACHIEVEMENT_DEFS[id];
        char buf[256];
        snprintf(buf, sizeof(buf), "Achievement Unlocked: %s — %s", def.name, def.description);
        iface.push_notification(buf, ImVec4(1.0f, 0.843f, 0.0f, 1.0f));
        // Auto-save achievements on unlock
        achievements.save("saves/achievements.ppach");
    }
}

// ── Gravitational wave tidal forces ──────────────────────────────────────────
// GW wavefronts exert quadrupole tidal acceleration on massive particles:
//   - Radial stretch (along source→particle direction)
//   - Tangential compression (perpendicular to radial)
// Amplitude falls off as 1/r.  Only particles within the thin wavefront shell
// (±SHELL_WIDTH of ring.radius) feel the force each tick — a transient pulse.
void PhysicsSimulation::apply_gw_tidal_forces(double dt) {
    if (iface.gw_rings.empty() || cfg.particle_count == 0) return;

    constexpr float SHELL_WIDTH    = 30.0f;   // wavefront thickness (world units)
    constexpr float TIDAL_STRENGTH = 0.15f;   // overall coupling constant
    constexpr uint32_t MASSLESS_MASK = BEHAVIOR_PHOTON | BEHAVIOR_NEUTRINO
                                     | BEHAVIOR_GRAVITON | BEHAVIOR_GLUON;

    float scaled_dt = static_cast<float>(dt) * cfg.time_scale;
    bool any_modified = false;

    for (const auto& ring : iface.gw_rings) {
        if (ring.radius < 1.0f) continue;  // ring hasn't started propagating

        // Effective strain at the wavefront: amplitude with 1/r falloff
        float strain = ring.amplitude / std::max(1.0f, ring.radius * 0.01f);
        if (strain < 0.001f) continue;  // too weak to matter

        float r_inner = ring.radius - SHELL_WIDTH;
        float r_outer = ring.radius + SHELL_WIDTH;
        float r_inner_sq = r_inner * r_inner;
        float r_outer_sq = r_outer * r_outer;

        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            if (readback_energies_[i] <= 0.0f) continue;  // dormant

            uint32_t pt = particles.types[i];
            if (pt >= PHYS_PARTICLE_TYPES) continue;

            // Skip massless particles — GW don't deflect light in linearized GR
            uint32_t flags = particles.behavior_flags[pt];
            if (flags & MASSLESS_MASK) continue;

            // Distance from ring origin to particle
            glm::vec2 delta = readback_positions_[i] - ring.origin;
            float dist_sq = glm::dot(delta, delta);

            // Is particle within the wavefront shell?
            if (dist_sq < r_inner_sq || dist_sq > r_outer_sq) continue;
            float dist = std::sqrt(dist_sq);
            if (dist < 1.0f) continue;

            // Radial and tangential unit vectors
            glm::vec2 radial = delta / dist;
            glm::vec2 tangent = glm::vec2(-radial.y, radial.x);

            // Tangential velocity component (determines squeeze direction)
            float v_tangent = glm::dot(readback_velocities_[i], tangent);

            // Quadrupole tidal acceleration (+ polarization):
            //   Radial: stretch (positive kick outward)
            //   Tangential: compress (negative kick inward)
            // Smooth envelope within the shell: peaks at wavefront center
            float shell_pos = (dist - ring.radius) / SHELL_WIDTH;  // -1..+1
            float envelope = 1.0f - shell_pos * shell_pos;         // parabolic peak

            float kick = TIDAL_STRENGTH * strain * envelope * scaled_dt;

            // Apply tidal deformation to velocity
            readback_velocities_[i] += radial  * (kick * 0.5f);   // radial stretch
            readback_velocities_[i] -= tangent * (kick * 0.3f * (v_tangent > 0.0f ? 1.0f : -1.0f));  // tangential squeeze

            any_modified = true;
        }
    }

    if (any_modified)
        cpu_particles_dirty_ = true;
}

void PhysicsSimulation::check_achievements() {
    // ── Temperature thresholds ──────────────────────────────────────────
    float temp = emergent_temperature_;
    if (temp > achievements.peak_temperature) achievements.peak_temperature = temp;

    if (temp >= 1000.0f)       try_unlock(ACH_TEMP_1000K);
    if (temp >= 1000000.0f)    try_unlock(ACH_TEMP_1MK);
    if (temp >= 1000000000.0f) try_unlock(ACH_TEMP_1GK);
    if (temp >= 10000000000.0f) try_unlock(ACH_TEMP_10GK);
    if (temp <= 2.0f && temp > 0.0f && frame_counter_ > 120)
        try_unlock(ACH_ABSOLUTE_ZERO);

    // ── Active particle count ───────────────────────────────────────────
    uint32_t active = iface.active_particle_display;
    if (active > achievements.peak_active_particles)
        achievements.peak_active_particles = active;
    if (active >= 1000)  try_unlock(ACH_PARTICLES_1000);
    if (active >= 5000)  try_unlock(ACH_PARTICLES_5000);
    if (active >= 10000) try_unlock(ACH_PARTICLES_10000);

    // ── Entangled pairs ─────────────────────────────────────────────────
    if (entangled_pair_count_ > 0)        try_unlock(ACH_FIRST_ENTANGLED);
    if (entangled_pair_count_ > achievements.peak_entangled_pairs)
        achievements.peak_entangled_pairs = entangled_pair_count_;
    if (entangled_pair_count_ >= 10)      try_unlock(ACH_ENTANGLED_10);

    // ── Fusion count thresholds ─────────────────────────────────────────
    if (achievements.total_fusions >= 10)  try_unlock(ACH_FUSION_10);
    if (achievements.total_fusions >= 100) try_unlock(ACH_FUSION_100);

    // ── Chain reaction detection ────────────────────────────────────────
    if (achievements.fission_recent_count >= 3) try_unlock(ACH_CHAIN_REACTION);

    // ── Counter-based milestones ─────────────────────────────────────────
    if (achievements.total_annihilations >= 100) try_unlock(ACH_ANNIHILATIONS_100);
    if (achievements.total_nuclear_decays >= 50) try_unlock(ACH_NUCLEAR_DECAYS_50);

    // ── Element discovery from detected nuclei ──────────────────────────
    for (const auto& nuc : detected_nuclei_) {
        if (nuc.Z <= 0 || nuc.Z >= 120) continue;

        // Check if this element has bound electrons (orbital assignment)
        bool has_electrons = false;
        for (uint32_t pi = 0; pi < cfg.particle_count; ++pi) {
            if (particles.orbital_parent[pi] == static_cast<int32_t>(nuc.rep)
                && particles.types[pi] == ELECTRON_TYPE_PHYS) {
                has_electrons = true;
                break;
            }
        }

        if (!achievements.elements_discovered[nuc.Z]) {
            achievements.elements_discovered[nuc.Z] = true;
            achievements.distinct_elements_count++;
        }

        if (has_electrons) try_unlock(ACH_FIRST_ELEMENT);

        switch (nuc.Z) {
            case 1:  try_unlock(ACH_HYDROGEN); break;
            case 2:  try_unlock(ACH_HELIUM);   break;
            case 3:  try_unlock(ACH_LITHIUM);  break;
            case 6:  try_unlock(ACH_CARBON);   break;
            case 8:  try_unlock(ACH_OXYGEN);   break;
            case 26: try_unlock(ACH_IRON);     break;
            case 79: try_unlock(ACH_GOLD);     break;
            case 92: try_unlock(ACH_URANIUM);  break;
            default: break;
        }

        // Check for antimatter element
        if (nuc.is_anti && has_electrons) try_unlock(ACH_ANTIMATTER_ELEMENT);
    }
    if (achievements.distinct_elements_count >= 10) try_unlock(ACH_ELEMENTS_10);
    if (achievements.distinct_elements_count >= 25) try_unlock(ACH_ELEMENTS_25);

    // ── Particle zoo: check for specific types present ──────────────────
    const uint32_t* tc = iface.type_counts_display;
    if (tc[POSITRON_TYPE_PHYS] > 0)   try_unlock(ACH_FIRST_POSITRON);
    if (tc[NEUTRINO_TYPE_PHYS] > 0 || tc[MU_NEUTRINO_TYPE_PHYS] > 0
        || tc[TAU_NEUTRINO_TYPE_PHYS] > 0)
        try_unlock(ACH_FIRST_NEUTRINO);
    if (tc[MUON_TYPE_PHYS] > 0 || tc[ANTIMUON_TYPE_PHYS] > 0)
        try_unlock(ACH_FIRST_MUON);
    if (tc[TAU_TYPE_PHYS] > 0 || tc[ANTITAU_TYPE_PHYS] > 0)
        try_unlock(ACH_FIRST_TAU);
    if (tc[ANTIPROTON_TYPE_PHYS] > 0)
        try_unlock(ACH_FIRST_ANTIPROTON);
    // Quarks (types 13-24)
    for (uint32_t q = UP_QUARK_TYPE; q <= ANTI_BOTTOM_TYPE; ++q) {
        if (tc[q] > 0) { try_unlock(ACH_FIRST_QUARK); break; }
    }
    // Bosons: W+, W-, Z, Higgs
    if (tc[W_PLUS_TYPE_PHYS] > 0 || tc[W_MINUS_TYPE_PHYS] > 0
        || tc[Z_BOSON_TYPE_PHYS] > 0 || tc[HIGGS_TYPE_PHYS] > 0)
        try_unlock(ACH_FIRST_BOSON);
    if (tc[DARK_MATTER_TYPE_PHYS] > 0) try_unlock(ACH_DARK_MATTER);

    // Full particle zoo: all 33 types present at once
    {
        bool all_present = true;
        for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
            if (tc[t] == 0) { all_present = false; break; }
        }
        if (all_present) try_unlock(ACH_PARTICLE_ZOO);
    }

    // ── Environment exploration ─────────────────────────────────────────
    uint32_t env = cfg.environment_mode;
    if (env < 12) {
        achievements.environments_tried[env] = true;
        bool all_tried = true;
        for (int e = 0; e < 12; ++e) {
            if (!achievements.environments_tried[e]) { all_tried = false; break; }
        }
        if (all_tried) try_unlock(ACH_TRY_ALL_ENVIRONMENTS);
    }

    // ── Molecule checks (bonded atom groups) ─────────────────────────────
    if (!bond_data_.empty() && !detected_nuclei_.empty()) {
        for (const auto& nuc : detected_nuclei_) {
            if (nuc.Z == 0) continue;
            uint32_t rep = nuc.rep;
            if (rep >= cfg.particle_count) continue;
            uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                if (base + s < bond_data_.size() && bond_data_[base + s] != 0xFFFFFFFFu) {
                    try_unlock(ACH_FIRST_MOLECULE);
                    // BFS to count bonded atoms for 5-atom check
                    if (!achievements.is_unlocked(ACH_MOLECULE_5_ATOMS)) {
                        std::vector<uint32_t> visited;
                        std::vector<uint32_t> stack = {rep};
                        while (!stack.empty() && visited.size() < 6) {
                            uint32_t cur = stack.back(); stack.pop_back();
                            bool found = false;
                            for (auto v : visited) if (v == cur) { found = true; break; }
                            if (found) continue;
                            visited.push_back(cur);
                            uint32_t b = cur * MAX_BONDS_PER_PARTICLE;
                            for (uint32_t ss = 0; ss < MAX_BONDS_PER_PARTICLE; ++ss) {
                                if (b + ss < bond_data_.size() && bond_data_[b + ss] != 0xFFFFFFFFu)
                                    stack.push_back(bond_data_[b + ss]);
                            }
                        }
                        if (visited.size() >= 5) try_unlock(ACH_MOLECULE_5_ATOMS);
                    }
                    goto done_molecule_check;  // only need to find one bonded nucleus
                }
            }
        }
        done_molecule_check:;
    }

    // ── Graviton observed ────────────────────────────────────────────────
    if (tc[GRAVITON_TYPE_PHYS] > 0) try_unlock(ACH_GRAVITON_OBSERVED);

    // ── Element count milestones ─────────────────────────────────────────
    if (achievements.distinct_elements_count >= 100) try_unlock(ACH_HUNDRED_ELEMENTS);

    // ── Speed demon: time_scale at max ───────────────────────────────────
    if (cfg.time_scale >= 4.9f) try_unlock(ACH_SPEED_DEMON);

    // ── Long play: 10+ minutes ───────────────────────────────────────────
    achievements.session_time_ += ImGui::GetIO().DeltaTime;
    if (achievements.session_time_ >= 600.0f) try_unlock(ACH_LONG_PLAY);

    // ── Fission chain reaction window (60 frame window) ─────────────────
    if (frame_counter_ - achievements.fission_window_start > 60) {
        achievements.fission_recent_count = 0;
        achievements.fission_window_start = frame_counter_;
    }
}

// ── Per-frame tick ───────────────────────────────────────────────────────────

void PhysicsSimulation::tick(GLFWwindow* window, double dt) {
    handle_input(window, dt);
    frame_counter_++;

    // Apply thread count from settings (only when changed to avoid runtime overhead)
#ifdef HAS_OPENMP
    {
        static int last_threads = -1;
        int sys_max = omp_get_max_threads();
        int threads = (iface.prefs.max_threads <= 0) ? sys_max : std::clamp(iface.prefs.max_threads, 1, sys_max);
        if (threads != last_threads) {
            omp_set_num_threads(threads);
            last_threads = threads;
        }
    }
#endif

    // ── Temperature kelvin → noise amplitude (Berendsen thermostat) ─────────
    // Negative feedback: when system is hotter than target → reduce noise (cool)
    //                    when system is cooler than target → increase noise (heat)
    float effective_kelvin = cfg.temperature_kelvin;
    if (cfg.thermo_feedback_enabled && emergent_temperature_ > 0.1f) {
        float correction = cfg.temperature_kelvin / emergent_temperature_;
        // coupling controls correction strength: 0=no feedback, 1=full thermostat
        effective_kelvin = cfg.temperature_kelvin
                         * (1.0f + cfg.thermo_coupling * (correction - 1.0f));
    }
    cfg.temperature = std::min(2.0f,
        0.10f * std::pow(effective_kelvin / 300.0f, 0.25f));

    // ── Effective Lorentz strength (with magnetic feedback) ──────────────────
    cfg.effective_lorentz = cfg.lorentz_strength;
    if (cfg.magnetic_feedback_enabled) {
        cfg.effective_lorentz = cfg.lorentz_strength * (1.0f - cfg.magnetic_coupling)
                              + emergent_bfield_ * cfg.magnetic_coupling;
    }

    // ── Assemble field_flags bitfield from UI booleans ───────────────────────
    cfg.field_flags = 0;
    if (iface.field_em)      cfg.field_flags |= (1u << 0);
    if (iface.field_strong)  cfg.field_flags |= (1u << 1);
    if (iface.field_weak)    cfg.field_flags |= (1u << 2);
    if (iface.field_gravity) cfg.field_flags |= (1u << 3);
    if (iface.field_higgs)   cfg.field_flags |= (1u << 4);
    if (iface.wave_mode)     cfg.field_flags |= (1u << 5);
    if (iface.show_collision_radii) cfg.field_flags |= (1u << 6);
    if (iface.hide_virtual_trails)  cfg.field_flags |= (1u << 7);
    if (iface.hide_bond_visuals)    cfg.field_flags |= (1u << 8);
    if (iface.field_dark_energy) cfg.field_flags |= (1u << 12);
    // GR gravity extensions
    if (iface.gr_mass_energy)       cfg.field_flags |= (1u << 9);
    if (iface.gr_frame_dragging)    cfg.field_flags |= (1u << 10);
    if (iface.gr_grav_waves)        cfg.field_flags |= (1u << 11);

    // Pass field intensity via legacy density_limit/local_density_cap path
    cfg.density_limit    = (cfg.field_flags != 0) ? 1.0f : 0.0f;
    cfg.local_density_cap = iface.field_intensity;

    // Upload dynamic GPU data
    cfg.force_object_count = force_object_count_;
    if (is_active) {
        compute.upload_dynamic_data(vk, particles);
        compute.upload_force_objects(vk, force_objects_);
    }

    // Dispatch compute shader
    if (is_active && compute.is_ready()) {
        VkCommandBuffer compute_cmd = vk.begin_single_command();
        float scaled_dt = static_cast<float>(dt) * cfg.time_scale;
        compute.record(compute_cmd, cfg, scaled_dt);
        vk.end_single_command(compute_cmd);
        readback_fresh_ = false;  // GPU state changed, readback is stale
    }

    // Hover detection (skip when fullscreen UI overlays are active)
    {
        iface.hover_particle_idx = -1;
        if (!readback_positions_.empty() && !ImGui::GetIO().WantCaptureMouse
            && !iface.show_pause_menu && !iface.show_splash && !iface.show_settings_menu) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            int win_w, win_h;
            glfwGetFramebufferSize(window, &win_w, &win_h);
            float hsx = static_cast<float>(REGION_W) / static_cast<float>(win_w);
            float hsy = static_cast<float>(REGION_H) / static_cast<float>(win_h);
            glm::vec2 mw = cfg.camera_origin
                + (glm::vec2(float(mx), float(my)) - glm::vec2(win_w * 0.5f, win_h * 0.5f))
                * glm::vec2(hsx, hsy) / cfg.current_camera_zoom;

            float snap_r = std::max(cfg.radius * 3.0f + 4.0f, 15.0f / cfg.current_camera_zoom);
            float min_d2 = snap_r * snap_r;
            for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                // Skip dormant particles (energy <= 0)
                if (pi < readback_energies_.size() && readback_energies_[pi] <= 0.0f) continue;
                glm::vec2 d = readback_positions_[pi] - mw;
                float d2 = d.x * d.x + d.y * d.y;
                if (d2 < min_d2) { min_d2 = d2; iface.hover_particle_idx = static_cast<int32_t>(pi); }
            }
        }
    }

    // FPS tracking
    fps_acc_ += dt;
    fps_frame_cnt_++;
    if (fps_acc_ >= 0.5) {
        iface.fps_display = static_cast<float>(fps_frame_cnt_ / fps_acc_);
        fps_acc_ = 0.0;
        fps_frame_cnt_ = 0;
    }

    // Readback for statistics, annihilation, and decay
    if (compute.is_ready() && is_active) {
        // Physics quality and skip settings (computed before readback to gate it)
        int quality = iface.prefs.physics_quality;
        int skip    = iface.prefs.physics_skip;
        bool run_physics = (skip == 0) || (frame_counter_ % static_cast<uint32_t>(skip + 1) == 0);

        // Throttled readback: skip if data is already fresh and no CPU work needed
        bool need_readback = !readback_fresh_ || run_physics
            || iface.request_save || iface.request_delete_particle
            || iface.request_element_delete || iface.request_element_duplicate
            || iface.request_halt_velocities
            || iface.request_remove_massless || iface.request_remove_massive;

        if (need_readback) {
            readback_positions_.resize(cfg.particle_count);
            readback_velocities_.resize(cfg.particle_count);
            readback_energies_.resize(cfg.particle_count);
            compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            readback_fresh_ = true;

            // Build GPU spatial grid and upload for shader neighbor lookup
            gpu_grid_.build(readback_positions_, readback_energies_, cfg.particle_count);
            compute.upload_gpu_grid(vk, gpu_grid_.cell_start, gpu_grid_.sorted_indices);
        }

        if (run_physics) {
            // Build spatial grid for O(N) neighbor queries
            if (iface.prefs.spatial_grid)
                grid_.build(readback_positions_, readback_energies_, cfg.particle_count);

            // Core physics — always run (frequency reduced by quality level)
            bool frame2 = (frame_counter_ % 2 == 0);
            bool frame3 = (frame_counter_ % 3 == 0);
            bool frame4 = (frame_counter_ % 4 == 0);

            if (quality >= 2 || (quality == 1 && frame2) || (quality == 0 && frame4))
                check_annihilation();
            if (quality >= 2 || (quality == 1 && frame2) || (quality == 0 && frame4))
                check_fusion();
            if (quality >= 2 || (quality == 1 && frame2) || (quality == 0 && frame4))
                check_fission();
            if (quality >= 2 || (quality == 1) || (quality == 0 && frame2))
                check_decay();
            if (quality >= 2 || (quality == 1) || (quality == 0 && frame2))
                check_pion_decay();
            if (quality >= 2 || (quality == 1 && frame2) || (quality == 0 && frame4))
                check_hadronization();

            // Medium+ interactions
            if (quality >= 1) {
                if (cfg.virtual_pairs_enabled && (quality >= 2 || frame4))
                    check_virtual_pairs();
                if (cfg.entanglement_enabled)
                    update_entanglement();
                if (quality >= 2 || frame3) {
                    check_photoelectric();
                    check_spallation();
                    check_bremsstrahlung();
                }
                if (quality >= 1 && frame4) {
                    check_neutrino_scattering();
                    check_weak_flavor_change();
                }
            }
            if (frame_counter_ % 10 == 0)
                check_neutrino_oscillations();

            // Orbital update (always needed for element detection)
            if (quality >= 2 || (quality == 1 && frame2) || (quality == 0 && frame4)) {
                update_orbitals();
                if (cfg.shell_transitions_enabled)
                    check_shell_transitions();
                repel_distinct_nuclei();
            }

            // Covalent bond formation/breaking (after orbitals detect nuclei)
            if (cfg.bonds_enabled && (frame_counter_ % cfg.bond_update_interval == 0))
                update_bonds();

            check_nuclear_decay();

            // GW tidal forces: wavefronts stretch/compress passing particles
            if (iface.gr_grav_waves && !iface.gw_rings.empty())
                apply_gw_tidal_forces(dt);

            check_achievements();

            // ── Single batched GPU sync for all CPU physics modifications ──
            if (cpu_particles_dirty_) {
                vkDeviceWaitIdle(vk.device);
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
                compute.upload_dynamic_data(vk, particles);
                cpu_particles_dirty_ = false;
            }
        }

        // Handle particle delete request from UI
        if (iface.request_delete_particle && iface.selected_particle_idx >= 0) {
            uint32_t pi = static_cast<uint32_t>(iface.selected_particle_idx);
            if (pi < cfg.particle_count) {
                readback_energies_[pi] = 0.0f;
                readback_velocities_[pi] = glm::vec2(0.0f);
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }
            iface.selected_particle_idx = -1;
            iface.particle_move_mode = false;
            iface.request_delete_particle = false;
        }
        iface.request_delete_particle = false;  // always clear

        // Handle element delete request — kill all particles belonging to the element
        if (iface.request_element_delete && iface.element_card_nucleus_rep >= 0) {
            int32_t nuc_rep = iface.element_card_nucleus_rep;
            bool changed = false;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                if (readback_energies_[i] <= 0.0f) continue;
                if (static_cast<int32_t>(i) == nuc_rep || particles.orbital_parent[i] == nuc_rep) {
                    readback_energies_[i] = 0.0f;
                    readback_velocities_[i] = glm::vec2(0.0f);
                    changed = true;
                }
            }
            if (changed)
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            iface.element_card_nucleus_rep = -1;
            iface.element_move_mode = false;
            iface.request_element_delete = false;
        }
        iface.request_element_delete = false;

        // Handle element duplicate request — spawn a copy of the element nearby
        if (iface.request_element_duplicate && iface.element_card_nucleus_rep >= 0) {
            int32_t nuc_rep = iface.element_card_nucleus_rep;
            std::mt19937 rng(frame_counter_ * 2718281828u + 42u);
            // Count composition and find center
            int Z_dup = 0, N_dup = 0;
            glm::vec2 center(0.0f);
            int member_count = 0;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                if (readback_energies_[i] <= 0.0f) continue;
                if (static_cast<int32_t>(i) == nuc_rep || particles.orbital_parent[i] == nuc_rep) {
                    uint32_t pt = particles.types[i];
                    if (pt == PROTON_TYPE) Z_dup++;
                    else if (pt == NEUTRON_TYPE) N_dup++;
                    center += readback_positions_[i];
                    member_count++;
                }
            }
            if (Z_dup > 0 && member_count > 0) {
                center /= static_cast<float>(member_count);
                // Offset the duplicate 80px to the right
                glm::vec2 dup_origin = center + glm::vec2(80.0f, 0.0f);
                float rw = static_cast<float>(WORLD_W);
                float rh = static_cast<float>(WORLD_H);
                auto wrap = [&](glm::vec2 p) -> glm::vec2 {
                    p.x = std::fmod(p.x + rw, rw);
                    p.y = std::fmod(p.y + rh, rh);
                    return p;
                };
                auto find_dormant = [&](uint32_t start) -> uint32_t {
                    for (uint32_t i = start; i < cfg.particle_count; ++i)
                        if (readback_energies_[i] < 0.01f) return i;
                    return UINT32_MAX;
                };

                uint32_t search = 0;
                int A_dup = Z_dup + N_dup;
                const float NUC_SP = 3.8f;

                // Hex spiral positions for nucleons
                struct NPos { float x, y; };
                std::vector<NPos> npos;
                npos.reserve(A_dup);
                npos.push_back({0.0f, 0.0f});
                for (int ring = 1; static_cast<int>(npos.size()) < A_dup; ++ring) {
                    for (int side = 0; side < 6 && static_cast<int>(npos.size()) < A_dup; ++side) {
                        for (int step = 0; step < ring && static_cast<int>(npos.size()) < A_dup; ++step) {
                            float a0 = 3.14159265f / 3.0f * side + 3.14159265f / 2.0f;
                            float a1 = 3.14159265f / 3.0f * (side + 1) + 3.14159265f / 2.0f;
                            float t = static_cast<float>(step) / static_cast<float>(ring);
                            float px = ring * NUC_SP * std::cos(a0) * (1.0f - t) + ring * NUC_SP * std::cos(a1) * t;
                            float py = ring * NUC_SP * std::sin(a0) * (1.0f - t) + ring * NUC_SP * std::sin(a1) * t;
                            npos.push_back({px, py});
                        }
                    }
                }

                // Interleave protons and neutrons
                const float DUP_ENERGY = 0.7f;
                const float e_scale_dup = std::sqrt(DUP_ENERGY);
                int p_rem = Z_dup, n_rem = N_dup;
                float nuc_ext = 0.0f;
                std::vector<uint32_t> dup_slots;
                for (int k = 0; k < A_dup; ++k) {
                    uint32_t slot = find_dormant(search);
                    if (slot == UINT32_MAX) break;
                    search = slot + 1;
                    readback_positions_[slot] = wrap(dup_origin + glm::vec2(npos[k].x, npos[k].y));
                    readback_velocities_[slot] = glm::vec2(0.0f);  // nucleons stay bound
                    readback_energies_[slot] = DUP_ENERGY;
                    uint32_t ptype;
                    if (k % 2 == 0) {
                        if (p_rem > 0) { ptype = PROTON_TYPE; --p_rem; }
                        else           { ptype = NEUTRON_TYPE; --n_rem; }
                    } else {
                        if (n_rem > 0) { ptype = NEUTRON_TYPE; --n_rem; }
                        else           { ptype = PROTON_TYPE; --p_rem; }
                    }
                    write_spawn_genome(particles, slot, ptype, rng, frame_counter_);
                    dup_slots.push_back(slot);
                    float r2 = npos[k].x * npos[k].x + npos[k].y * npos[k].y;
                    nuc_ext = std::max(nuc_ext, std::sqrt(r2));
                }
                nuc_ext += NUC_SP * 0.5f;

                // Place electrons — match update_orbitals formula
                const float NUC_CLR = nuc_ext + 4.0f;
                int e_left = Z_dup;
                int shell_fill_dup[4] = {0, 0, 0, 0};
                for (int sh = 0; sh < 4 && e_left > 0; ++sh) {
                    int cap = std::min(SHELL_CAP_O[sh], e_left);
                    float n_sh = static_cast<float>(sh + 1);
                    int inner_e = 0;
                    for (int s = 0; s < sh; ++s) inner_e += shell_fill_dup[s];
                    float Z_eff = std::max(1.0f, static_cast<float>(Z_dup) - static_cast<float>(inner_e));
                    float R_bohr = n_sh * n_sh * R_BOHR / Z_eff;
                    float R_target = std::max(R_bohr, 8.0f);
                    R_target = std::max(R_target, NUC_CLR + sh * 8.0f);
                    float R3 = R_target * R_target * R_target;
                    float R2_soft = R_target * R_target + SOFTEN_SQ_O;
                    float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
                    float orbit_boost_d = cfg.orbit_boost[sh];
                    float v_orbital = (L_ground / R_target) * orbit_boost_d;
                    float shell_offset_d = sh * cfg.orbital_shell_offset;

                    for (int e = 0; e < cap; ++e) {
                        uint32_t slot = find_dormant(search);
                        if (slot == UINT32_MAX) break;
                        search = slot + 1;
                        float angle = shell_offset_d + 2.0f * 3.14159265f * static_cast<float>(e) / static_cast<float>(cap);
                        glm::vec2 offset(R_target * std::cos(angle), R_target * std::sin(angle));
                        glm::vec2 tangent(-std::sin(angle), std::cos(angle));
                        readback_positions_[slot] = wrap(dup_origin + offset);
                        readback_velocities_[slot] = tangent * v_orbital * e_scale_dup;
                        readback_energies_[slot] = DUP_ENERGY;
                        write_spawn_genome(particles, slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                        particles.genomes[slot * GENOME_SIZE + 2] = L_ground * e_scale_dup;
                        particles.genomes[slot * GENOME_SIZE + 3] = orbit_boost_d;
                        if (slot < particles.orbital_shell.size()) {
                            particles.orbital_shell[slot] = static_cast<int8_t>(sh);
                            particles.excitation_timer[slot] = 0;
                        }
                        dup_slots.push_back(slot);
                    }
                    shell_fill_dup[sh] = cap;
                    e_left -= cap;
                }
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }
            iface.request_element_duplicate = false;
        }
        iface.request_element_duplicate = false;

        // Handle halt velocities request
        if (iface.request_halt_velocities) {
            for (uint32_t i = 0; i < cfg.particle_count; ++i)
                readback_velocities_[i] = glm::vec2(0.0f);
            compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            iface.request_halt_velocities = false;
        }

        // Handle remove massless request (photon, neutrinos, gluon, graviton, dark energy)
        if (iface.request_remove_massless) {
            bool changed = false;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                uint32_t t = particles.types[i];
                if (t == PHOTON_TYPE_PHYS || t == NEUTRINO_TYPE_PHYS
                    || t == MU_NEUTRINO_TYPE_PHYS || t == TAU_NEUTRINO_TYPE_PHYS
                    || t == GLUON_TYPE_PHYS || t == GRAVITON_TYPE_PHYS
                    || t == DARK_ENERGY_TYPE_PHYS) {
                    if (readback_energies_[i] > 0.0f) {
                        readback_energies_[i] = 0.0f;
                        readback_velocities_[i] = glm::vec2(0.0f);
                        changed = true;
                    }
                }
            }
            if (changed)
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            iface.request_remove_massless = false;
        }

        // Handle remove massive request (everything except massless types)
        if (iface.request_remove_massive) {
            bool changed = false;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                uint32_t t = particles.types[i];
                bool massless = (t == PHOTON_TYPE_PHYS || t == NEUTRINO_TYPE_PHYS
                    || t == MU_NEUTRINO_TYPE_PHYS || t == TAU_NEUTRINO_TYPE_PHYS
                    || t == GLUON_TYPE_PHYS || t == GRAVITON_TYPE_PHYS
                    || t == DARK_ENERGY_TYPE_PHYS);
                if (!massless && readback_energies_[i] > 0.0f) {
                    readback_energies_[i] = 0.0f;
                    readback_velocities_[i] = glm::vec2(0.0f);
                    changed = true;
                }
            }
            if (changed)
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            iface.request_remove_massive = false;
        }

        // ── Fused stats + temperature + B-field (single parallel reduction) ──
        {
            uint32_t active = 0;
            float total_energy = 0.0f;
            uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
            float total_ke = 0.0f;
            uint32_t ke_count = 0;
            float total_current = 0.0f;
            uint32_t charged_count = 0;
            const uint32_t n = cfg.particle_count;

            #pragma omp parallel for \
                reduction(+:active, total_energy, total_ke, ke_count, \
                            total_current, charged_count, \
                            type_counts[:MAX_PARTICLE_TYPES]) \
                if(n > 2000)
            for (uint32_t i = 0; i < n; ++i) {
                float e = readback_energies_[i];
                if (e < 0.01f) continue;

                active++;
                total_energy += e;

                uint32_t t = particles.types[i];
                if (t < MAX_PARTICLE_TYPES) type_counts[t]++;

                glm::vec2 v = readback_velocities_[i];
                total_ke += 0.5f * glm::dot(v, v);
                ke_count++;

                if (t < PHYS_PARTICLE_TYPES) {
                    float q = PHYS_CHARGE[t];
                    if (std::abs(q) > 0.01f) {
                        total_current += std::abs(q) * glm::length(v);
                        charged_count++;
                    }
                }
            }

            iface.active_particle_display = active;
            iface.dormant_particle_display = n - active;
            iface.total_energy_display = total_energy;
            iface.avg_energy_display = (active > 0) ? total_energy / active : 0.0f;
            for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t)
                iface.type_counts_display[t] = type_counts[t];

            // ── Per-type bestiary stats ──────────────────────────────────
            {
                static uint32_t prev_type_counts[MAX_PARTICLE_TYPES] = {};
                for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
                    auto& s = iface.type_stats[t];
                    // New spawns this tick
                    if (type_counts[t] > prev_type_counts[t])
                        s.total_spawned += (type_counts[t] - prev_type_counts[t]);
                    // Deaths this tick — accumulate approximate lifetime
                    if (prev_type_counts[t] > type_counts[t]) {
                        uint32_t deaths = prev_type_counts[t] - type_counts[t];
                        s.lifetime_count += deaths;
                        // Use frame_counter as rough upper-bound age estimate
                        s.lifetime_sum += deaths * static_cast<double>(frame_counter_);
                    }
                    s.peak_count = std::max(s.peak_count, type_counts[t]);
                    prev_type_counts[t] = type_counts[t];
                }
                // Per-type energy and speed (from living particles)
                double type_energy[MAX_PARTICLE_TYPES] = {};
                double type_speed[MAX_PARTICLE_TYPES] = {};
                for (uint32_t i = 0; i < n; ++i) {
                    if (readback_energies_[i] < 0.01f) continue;
                    uint32_t t = particles.types[i];
                    if (t >= MAX_PARTICLE_TYPES) continue;
                    type_energy[t] += readback_energies_[i];
                    type_speed[t] += glm::length(readback_velocities_[i]);
                }
                for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
                    iface.type_stats[t].energy_sum = type_energy[t];
                    iface.type_stats[t].speed_sum = type_speed[t];
                }
            }

            // ── Per-element bestiary stats ───────────────────────────────
            {
                // Reset per-tick fields
                for (int z = 0; z < 119; ++z) {
                    iface.element_stats[z].current_count = 0;
                    iface.element_stats[z].energy_sum = 0.0;
                }
                // Accumulate from current element_list
                for (const auto& elem : iface.element_list) {
                    int z = elem.Z;
                    if (z < 1 || z > 118) continue;
                    iface.element_stats[z].current_count++;
                    iface.element_stats[z].energy_sum += elem.energy_MeV;
                }
                // Track spawns/deaths via frame-over-frame delta
                static uint32_t prev_elem_counts[119] = {};
                for (int z = 1; z <= 118; ++z) {
                    auto& s = iface.element_stats[z];
                    uint32_t cur = s.current_count;
                    if (cur > prev_elem_counts[z])
                        s.total_spawned += (cur - prev_elem_counts[z]);
                    if (prev_elem_counts[z] > cur) {
                        uint32_t deaths = prev_elem_counts[z] - cur;
                        s.lifetime_count += deaths;
                        s.lifetime_sum += deaths * static_cast<double>(frame_counter_);
                    }
                    s.peak_count = std::max(s.peak_count, cur);
                    prev_elem_counts[z] = cur;
                }
            }

            float avg_ke = (ke_count > 0) ? total_ke / ke_count : 0.0f;
            float measured_temp = avg_ke * 0.1f;
            emergent_temperature_ = emergent_temperature_ * 0.98f + measured_temp * 0.02f;
            iface.emergent_temp_display = emergent_temperature_;

            float avg_current = (charged_count > 0) ? total_current / charged_count : 0.0f;
            float measured_bfield = avg_current * 0.02f;
            emergent_bfield_ = emergent_bfield_ * 0.98f + measured_bfield * 0.02f;
            iface.emergent_bfield_display = emergent_bfield_;

            // ── First Law: Energy conservation ledger ─────────────────────
            {
                float current_ke = total_ke * 0.1f;   // same scale as emergent temp
                float current_pe = total_energy;       // energy buffer sum
                float current_total = current_ke + current_pe;

                if (initial_total_energy_ < 0.0f && current_total > 0.01f)
                    initial_total_energy_ = current_total;

                // Estimate injection from thermal noise: N × temperature² × 2
                float noise_power = cfg.temperature * cfg.temperature * 2.0f * active;
                // Estimate dissipation from damping: KE × (1 - dampening²)
                float damp_loss = current_ke * (1.0f - cfg.dampening * cfg.dampening);

                energy_injected_rate_ = energy_injected_rate_ * 0.98f + noise_power * 0.02f;
                energy_dissipated_rate_ = energy_dissipated_rate_ * 0.98f + damp_loss * 0.02f;

                if (initial_total_energy_ > 0.01f)
                    energy_conservation_ratio_ = current_total / initial_total_energy_;

                float delta_e = current_total - prev_total_energy_;
                energy_drift_rate_ = energy_drift_rate_ * 0.95f + delta_e * 0.05f;

                prev_total_ke_ = current_ke;
                prev_total_pe_ = current_pe;
                prev_total_energy_ = current_total;

                iface.energy_kinetic_display = current_ke;
                iface.energy_potential_display = current_pe;
                iface.energy_conservation_ratio_display = energy_conservation_ratio_;
                iface.energy_injected_rate_display = energy_injected_rate_;
                iface.energy_dissipated_rate_display = energy_dissipated_rate_;
                iface.energy_drift_rate_display = energy_drift_rate_;
            }

            // ── Second Law: Entropy calculation (2D Sackur-Tetrode) ───────
            {
                constexpr uint32_t E_GRID_W = 16;
                constexpr uint32_t E_GRID_H = 9;
                constexpr uint32_t E_CELLS = E_GRID_W * E_GRID_H;
                float cell_w = static_cast<float>(REGION_W) / E_GRID_W;
                float cell_h = static_cast<float>(REGION_H) / E_GRID_H;
                float cell_area = cell_w * cell_h;

                float cell_ke[E_CELLS] = {};
                uint32_t cell_count[E_CELLS] = {};

                for (uint32_t i = 0; i < n; ++i) {
                    if (readback_energies_[i] < 0.01f) continue;
                    int gx = std::clamp(static_cast<int>(readback_positions_[i].x / cell_w),
                                        0, static_cast<int>(E_GRID_W) - 1);
                    int gy = std::clamp(static_cast<int>(readback_positions_[i].y / cell_h),
                                        0, static_cast<int>(E_GRID_H) - 1);
                    int idx = gy * E_GRID_W + gx;
                    cell_ke[idx] += 0.5f * glm::dot(readback_velocities_[i], readback_velocities_[i]);
                    cell_count[idx]++;
                }

                float S = 0.0f;
                for (uint32_t c = 0; c < E_CELLS; ++c) {
                    if (cell_count[c] < 2) continue;
                    float N_cell = static_cast<float>(cell_count[c]);
                    float T_cell = cell_ke[c] / N_cell;
                    if (T_cell < 1e-6f) continue;
                    S += N_cell * (1.0f + std::log(cell_area / N_cell) + std::log(T_cell));
                }

                prev_entropy_ = system_entropy_;
                system_entropy_ = S;

                float dS = system_entropy_ - prev_entropy_;
                entropy_trend_ema_ = entropy_trend_ema_ * 0.95f + dS * 0.05f;
                if (entropy_trend_ema_ > 0.1f) entropy_trend_ = 1;
                else if (entropy_trend_ema_ < -0.1f) entropy_trend_ = -1;
                else entropy_trend_ = 0;

                iface.system_entropy_display = system_entropy_;
                iface.entropy_trend_display = entropy_trend_;
            }
        }
    }

    // Thread readback data to interface for info card display
    iface.frame_counter_display = frame_counter_;
    iface.readback_velocities = readback_velocities_.data();
    iface.readback_count = cfg.particle_count;
    iface.nuclear_decay_count_display = nuclear_decay_count_;
    iface.entangled_pair_count_display = entangled_pair_count_;
    iface.readback_positions_ptr = readback_positions_.data();
    iface.entangled_partners_ptr = particles.entangled_partner.data();
    iface.readback_energies_ptr = readback_energies_.data();
    iface.readback_types_ptr = particles.types.data();
    iface.bond_data_ptr = bond_data_.empty() ? nullptr : bond_data_.data();
    iface.bond_data_count = static_cast<uint32_t>(bond_data_.size());

    // ── Measurement tool updates ─────────────────────────────────────────────
    // Thermometer probes (OpenMP parallelized inner loop)
    for (auto& probe : iface.thermo_probes) {
        float total_ke = 0.0f;
        uint32_t cnt = 0;
        float r2 = probe.radius * probe.radius;
        #pragma omp parallel for reduction(+:total_ke,cnt) if(cfg.particle_count > 2000)
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            if (readback_energies_[i] <= 0.0f) continue;
            glm::vec2 d = readback_positions_[i] - probe.world_pos;
            if (glm::dot(d, d) < r2) {
                total_ke += 0.5f * glm::dot(readback_velocities_[i], readback_velocities_[i]);
                cnt++;
            }
        }
        probe.local_count = cnt;
        probe.local_temp = (cnt > 0) ? (total_ke / cnt) * 0.1f : 0.0f;
    }

    // Density counters (OpenMP parallelized inner loop)
    for (auto& dc : iface.density_counters) {
        uint32_t cnt = 0;
        float r2 = dc.radius * dc.radius;
        #pragma omp parallel for reduction(+:cnt) if(cfg.particle_count > 2000)
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            if (readback_energies_[i] <= 0.0f) continue;
            glm::vec2 d = readback_positions_[i] - dc.world_pos;
            if (glm::dot(d, d) < r2) cnt++;
        }
        dc.count = cnt;
        dc.density = static_cast<float>(cnt) / (3.14159265f * dc.radius * dc.radius);
    }

    // Velocity meters: prune dead targets
    for (auto& vm : iface.velocity_meters) {
        if (vm.particle_idx >= 0 && static_cast<uint32_t>(vm.particle_idx) < cfg.particle_count) {
            if (readback_energies_[vm.particle_idx] <= 0.0f)
                vm.active = false;
        }
    }
    iface.velocity_meters.erase(
        std::remove_if(iface.velocity_meters.begin(), iface.velocity_meters.end(),
                       [](const VelocityMeterTarget& v) { return !v.active; }),
        iface.velocity_meters.end());

    // ── Visualization grid (heatmap + velocity field) — OpenMP parallelized ──
    if (iface.show_velocity_field || iface.show_magnetic_field) {
        iface.vis_grid = VisGrid{};
        float cell_w = static_cast<float>(WORLD_W) / VIS_GRID_W;
        float cell_h = static_cast<float>(WORLD_H) / VIS_GRID_H;

        #pragma omp parallel if(cfg.particle_count > 2000)
        {
            VisGrid local_grid{};
            #pragma omp for nowait
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                if (readback_energies_[i] <= 0.0f) continue;
                int gx = std::clamp(static_cast<int>(readback_positions_[i].x / cell_w), 0, static_cast<int>(VIS_GRID_W) - 1);
                int gy = std::clamp(static_cast<int>(readback_positions_[i].y / cell_h), 0, static_cast<int>(VIS_GRID_H) - 1);
                int idx = gy * VIS_GRID_W + gx;

                glm::vec2 v = readback_velocities_[i];
                local_grid.energy[idx] += 0.5f * glm::dot(v, v);
                local_grid.vel_x[idx] += v.x;
                local_grid.vel_y[idx] += v.y;
                local_grid.count[idx]++;
            }
            #pragma omp critical
            {
                for (uint32_t c = 0; c < VIS_GRID_CELLS; ++c) {
                    iface.vis_grid.energy[c] += local_grid.energy[c];
                    iface.vis_grid.vel_x[c]  += local_grid.vel_x[c];
                    iface.vis_grid.vel_y[c]  += local_grid.vel_y[c];
                    iface.vis_grid.count[c]  += local_grid.count[c];
                }
            }
        }

        // ── Magnetic field grid — compute Bz at each cell center from all particles ──
        if (iface.show_magnetic_field) {
            constexpr float B_CUTOFF = 200.0f;
            constexpr float B_CUTOFF_SQ = B_CUTOFF * B_CUTOFF;
            constexpr float MU_P = 2.793f;
            constexpr float MU_N = -1.913f;
            constexpr float K_MAG = 1.0f;
            constexpr float K_DIP = 2.0f;

            #pragma omp parallel for if(VIS_GRID_CELLS > 100)
            for (uint32_t ci = 0; ci < VIS_GRID_CELLS; ++ci) {
                float cx = (ci % VIS_GRID_W + 0.5f) * cell_w;
                float cy = (ci / VIS_GRID_W + 0.5f) * cell_h;
                float bz = 0.0f;

                for (uint32_t pi = 0; pi < cfg.particle_count; ++pi) {
                    if (readback_energies_[pi] <= 0.0f) continue;
                    float dx = cx - readback_positions_[pi].x;
                    float dy = cy - readback_positions_[pi].y;
                    float r_sq = dx * dx + dy * dy;
                    if (r_sq >= B_CUTOFF_SQ) continue;
                    float r = std::sqrt(r_sq);
                    float inv_r = 1.0f / std::max(r, 0.5f);
                    float nx = dx * inv_r, ny = dy * inv_r;

                    uint32_t pt = particles.types[pi];
                    float charge = particles.genomes[pi * GENOME_SIZE + 0];
                    float spin_v = particles.genomes[pi * GENOME_SIZE + 1];

                    // Biot-Savart: B_z = charge * (v × r̂) / (r² + 1)
                    if (std::abs(charge) > 0.01f) {
                        glm::vec2 vel = readback_velocities_[pi];
                        float cross = vel.x * (-ny) - vel.y * (-nx);
                        bz += K_MAG * charge * cross / (r_sq + 1.0f);
                    }

                    // Nucleon intrinsic magnetic dipole: B_z = mu * spin / (r³ + 1)
                    float mu_i = 0.0f;
                    if (pt == PROTON_TYPE)               mu_i = MU_P;
                    else if (pt == ANTIPROTON_TYPE_PHYS) mu_i = -MU_P;
                    else if (pt == NEUTRON_TYPE)         mu_i = MU_N;
                    if (std::abs(mu_i) > 0.01f) {
                        float r3 = r * r_sq + 1.0f;
                        bz += K_DIP * mu_i * spin_v / r3;
                    }
                }
                iface.vis_grid.bfield_z[ci] = bz;
            }

            // ── Collect magnetic sources for field line visualization ──
            // No cap — every qualifying particle gets field lines. Camera-cull
            // off-screen particles to keep draw cost manageable.
            // Intrinsic dipole moment (spin) and Biot-Savart (momentum) are stored
            // separately so the visualization can apply correct spatial patterns:
            //   Dipole: 1/r³ falloff, axis from spin/velocity
            //   Biot-Savart: 1/r² falloff, azimuthal around velocity direction
            iface.magnetic_sources.clear();
            float vis_hw = static_cast<float>(REGION_W) / (2.0f * cfg.current_camera_zoom)
                         + cfg.interaction_radius;   // half-width + margin
            float vis_hh = static_cast<float>(REGION_H) / (2.0f * cfg.current_camera_zoom)
                         + cfg.interaction_radius;
            for (uint32_t pi = 0; pi < cfg.particle_count; ++pi) {
                if (readback_energies_[pi] <= 0.0f) continue;

                // Camera-cull: skip particles far outside the viewport
                glm::vec2 rel = readback_positions_[pi] - cfg.camera_origin;
                if (std::abs(rel.x) > vis_hw || std::abs(rel.y) > vis_hh) continue;

                uint32_t pt = particles.types[pi];
                float charge = particles.genomes[pi * GENOME_SIZE + 0];
                float spin_v = particles.genomes[pi * GENOME_SIZE + 1];
                glm::vec2 vel = readback_velocities_[pi];
                float speed = glm::length(vel);
                float mass_mev = (pt < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[pt] : 100.0f;

                // Intrinsic magnetic dipole moment (spin contribution)
                // Nucleons: anomalous moments (in nuclear magnetons)
                // Charged leptons: Dirac moment μ = q·ħ/(2m) ∝ charge/mass
                float mu_int = 0.0f;
                if (pt == PROTON_TYPE)               mu_int = MU_P * spin_v;
                else if (pt == ANTIPROTON_TYPE_PHYS) mu_int = -MU_P * spin_v;
                else if (pt == NEUTRON_TYPE)         mu_int = MU_N * spin_v;
                else if (std::abs(charge) > 0.01f && std::abs(spin_v) > 0.01f) {
                    // Dirac moment: μ ∝ charge × spin / mass
                    // Normalized so electron ≈ 1.0 Bohr magneton
                    mu_int = charge * spin_v / std::max(mass_mev, 0.1f) * 0.511f;
                }

                // Include particle if it has intrinsic moment OR is a moving charge
                bool has_dipole = std::abs(mu_int) > 0.01f;
                bool has_current = std::abs(charge) > 0.01f && speed > 0.5f;
                if (has_dipole || has_current) {
                    iface.magnetic_sources.push_back({
                        readback_positions_[pi], vel, mu_int, charge, mass_mev
                    });
                }
            }
        }
    }

    // ── Gravitational wave ripple propagation & emission ────────────────────
    {
        constexpr float C_SIM = 300.0f;
        float scaled_dt = static_cast<float>(dt) * cfg.time_scale;

        // Propagate existing rings outward at c, remove dead ones
        for (int ri = static_cast<int>(iface.gw_rings.size()) - 1; ri >= 0; --ri) {
            auto& ring = iface.gw_rings[ri];
            ring.radius += C_SIM * scaled_dt;
            if (ring.radius >= ring.max_radius) {
                iface.gw_rings.erase(iface.gw_rings.begin() + ri);
            }
        }

        // Emit new rings from strongly accelerating massive particles
        if (iface.show_grav_waves && prev_velocities_.size() == cfg.particle_count) {
            constexpr int   MAX_RINGS_TOTAL   = 512;
            constexpr int   MAX_EMIT_PER_TICK = 8;
            constexpr float ACCEL_THRESHOLD   = 5.0f;  // minimum |Δv/dt| to emit
            float inv_dt = (scaled_dt > 1e-6f) ? 1.0f / scaled_dt : 0.0f;

            int emitted = 0;
            for (uint32_t pi = 0; pi < cfg.particle_count && emitted < MAX_EMIT_PER_TICK; ++pi) {
                if (readback_energies_[pi] <= 0.0f) continue;
                uint32_t pt = particles.types[pi];
                if (pt >= PHYS_PARTICLE_TYPES) continue;

                float mass = PHYS_REST_MASS_MEV[pt];
                if (mass < 10.0f) continue;  // only heavy particles emit visible GW

                glm::vec2 dv = readback_velocities_[pi] - prev_velocities_[pi];
                float accel = glm::length(dv) * inv_dt;
                if (accel < ACCEL_THRESHOLD) continue;

                // GW amplitude ∝ mass × acceleration (quadrupole formula analog)
                float amp = std::min(mass * accel * 0.0001f, 3.0f);
                if (amp < 0.05f) continue;

                if (static_cast<int>(iface.gw_rings.size()) >= MAX_RINGS_TOTAL) break;

                // GW propagate across the world (half-diagonal as max extent)
                float world_diag = std::sqrt(float(WORLD_W) * float(WORLD_W)
                                           + float(WORLD_H) * float(WORLD_H));
                iface.gw_rings.push_back({
                    readback_positions_[pi],
                    0.0f,           // starts at source
                    amp,
                    world_diag * 0.5f  // propagate across the simulation
                });
                emitted++;
            }
        }

        // Store current velocities for next-tick acceleration detection
        prev_velocities_ = readback_velocities_;
    }

    // ── Trajectory tracer update ─────────────────────────────────────────────
    if (iface.show_trajectory_tracer) {
        if (iface.trajectory_history.size() != cfg.particle_count)
            iface.trajectory_history.resize(cfg.particle_count);

        int max_pts = iface.trajectory_max_points;
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            auto& hist = iface.trajectory_history[i];
            if (readback_energies_[i] <= 0.0f) {
                hist.clear();
                continue;
            }
            hist.push_back(readback_positions_[i]);
            if (static_cast<int>(hist.size()) > max_pts)
                hist.erase(hist.begin());
        }
    } else if (!iface.trajectory_history.empty()) {
        iface.trajectory_history.clear();
    }

    // ── Force vector computation for selected particle ───────────────────────
    if (iface.show_force_vectors && iface.selected_particle_idx >= 0) {
        uint32_t si = static_cast<uint32_t>(iface.selected_particle_idx);
        iface.force_contributions.clear();

        if (si < cfg.particle_count && readback_energies_[si] > 0.0f) {
            glm::vec2 pos_i = readback_positions_[si];
            float q_i = particles.genomes[si * GENOME_SIZE + 0];

            glm::vec2 f_coulomb(0), f_yukawa(0), f_gravity(0);

            for (uint32_t j = 0; j < cfg.particle_count; ++j) {
                if (j == si || readback_energies_[j] <= 0.0f) continue;
                glm::vec2 d = readback_positions_[j] - pos_i;
                // Toroidal wrapping
                if (d.x > WORLD_W * 0.5f) d.x -= WORLD_W;
                if (d.x < -WORLD_W * 0.5f) d.x += WORLD_W;
                if (d.y > WORLD_H * 0.5f) d.y -= WORLD_H;
                if (d.y < -WORLD_H * 0.5f) d.y += WORLD_H;

                float r2 = glm::dot(d, d);
                float r = std::sqrt(r2);
                if (r < 1.0f || r > cfg.interaction_radius) continue;
                glm::vec2 dir = d / r;

                float q_j = particles.genomes[j * GENOME_SIZE + 0];

                // Coulomb: q_i * q_j / r^2
                if (std::abs(q_i) > 0.01f && std::abs(q_j) > 0.01f)
                    f_coulomb += dir * (q_i * q_j * 1200.0f / r2);

                // Yukawa (short-range nuclear)
                if (r < 30.0f) {
                    float yukawa = 800.0f * std::exp(-r / 5.0f) / r2;
                    f_yukawa += dir * yukawa;
                }

                // Gravity
                f_gravity += dir * (cfg.gravity_strength / r2);
            }

            auto push_force = [&](glm::vec2 f, const char* name, ImVec4 col) {
                float mag = glm::length(f);
                if (mag > 0.001f)
                    iface.force_contributions.push_back({f / mag, mag, name, col});
            };

            push_force(f_coulomb, "Coulomb",  ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            push_force(f_yukawa,  "Yukawa",   ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            push_force(f_gravity, "Gravity",  ImVec4(0.5f, 0.5f, 1.0f, 1.0f));
        }
    } else {
        iface.force_contributions.clear();
    }

    // Populate element list and electron cloud data for UI
    iface.element_list.clear();
    iface.nucleus_clouds.clear();
    iface.orbit_paths.clear();
    for (auto& nuc : detected_nuclei_) {
        if (nuc.Z <= 0) continue;
        int bound_leptons = 0;
        float elem_energy_MeV = 0.0f;
        uint32_t elem_oldest_birth = UINT32_MAX;
        // Count bound electrons for matter nuclei, bound positrons for antinuclei
        // Also accumulate relativistic energy and track oldest birth for all constituents
        uint32_t lepton_type = nuc.is_anti ? POSITRON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
        // Uses PHYS_REST_MASS_MEV[] and C_SIM from phys_particles.h
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            int32_t par = particles.orbital_parent[i];
            if (par != static_cast<int32_t>(nuc.rep) && static_cast<int32_t>(i) != static_cast<int32_t>(nuc.rep)) continue;
            uint32_t pt = particles.types[i];
            bool is_nucleon = (pt == PROTON_TYPE || pt == NEUTRON_TYPE || pt == ANTIPROTON_TYPE_PHYS);
            bool is_lepton = (pt == lepton_type);
            if (!is_nucleon && !is_lepton) continue;
            if (is_lepton) bound_leptons++;
            // Accumulate relativistic energy
            float m0 = PHYS_REST_MASS_MEV[pt];
            if (m0 > 0.0f && i < static_cast<uint32_t>(readback_velocities_.size())) {
                float spd = glm::length(readback_velocities_[i]);
                float beta = std::min(spd / C_SIM, 0.9999f);
                float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                elem_energy_MeV += (gamma - 1.0f) * m0;  // KE = (γ-1)m₀c²
            }
            // Track oldest constituent
            if (i < static_cast<uint32_t>(particles.birth_frames.size()) &&
                particles.birth_frames[i] < elem_oldest_birth)
                elem_oldest_birth = particles.birth_frames[i];
        }
        iface.element_list.push_back({nuc.Z, nuc.N, bound_leptons, nuc.rep, nuc.is_anti, elem_energy_MeV, elem_oldest_birth});

        // Populate cloud info for electron shell visualization
        if (iface.show_electron_cloud) {
            PhysicsInterface::NucleusCloudInfo cloud{};
            cloud.center = nuc.center;
            cloud.Z = nuc.Z;
            cloud.electrons = bound_leptons;
            cloud.is_anti = nuc.is_anti;

            // Compute Bohr-model shell radii (same constants as update_orbitals)
            int e_remaining = std::min(bound_leptons, 60);
            int shell_fill_tmp[4] = {0, 0, 0, 0};

            // Fill shells from inner to outer
            for (int s = 0; s < 4 && e_remaining > 0; ++s) {
                shell_fill_tmp[s] = std::min(e_remaining, SHELL_CAP_O[s]);
                e_remaining -= shell_fill_tmp[s];
            }

            for (int s = 0; s < 4; ++s) {
                cloud.shell_fill[s] = shell_fill_tmp[s];
                cloud.shell_cap[s] = SHELL_CAP_O[s];

                // Compute Bohr radius with Slater screening
                float screening = 0.0f;
                if (s == 1) screening = static_cast<float>(SHELL_CAP_O[0]) * 0.85f;
                else if (s == 2) screening = static_cast<float>(SHELL_CAP_O[0]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[1]) * 0.85f;
                else if (s == 3) screening = static_cast<float>(SHELL_CAP_O[0]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[1]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[2]) * 0.85f;
                float Z_eff = std::max(1.0f, static_cast<float>(nuc.Z) - screening);
                float n_shell = static_cast<float>(s + 1);
                cloud.shell_radii[s] = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
            }

            iface.nucleus_clouds.push_back(cloud);
        }
    }

    // ── Compute orbit paths for bound electrons ─────────────────────────────
    if (iface.show_orbit_paths && !detected_nuclei_.empty()) {
        constexpr int MAX_ORBIT_PATHS = 300;

        // Build map: nucleus rep → NucleusInfo index
        std::unordered_map<uint32_t, uint32_t> rep_to_nuc;
        rep_to_nuc.reserve(detected_nuclei_.size());
        for (uint32_t ni = 0; ni < detected_nuclei_.size(); ++ni)
            rep_to_nuc[detected_nuclei_[ni].rep] = ni;

        int orbit_count = 0;
        for (uint32_t i = 0; i < cfg.particle_count && orbit_count < MAX_ORBIT_PATHS; ++i) {
            uint32_t pt = particles.types[i];
            if (pt != ELECTRON_TYPE_PHYS && pt != POSITRON_TYPE_PHYS) continue;
            if (particles.orbital_parent[i] < 0) continue;  // unbound

            uint32_t parent = static_cast<uint32_t>(particles.orbital_parent[i]);
            auto it = rep_to_nuc.find(parent);
            if (it == rep_to_nuc.end()) continue;
            const auto& nuc = detected_nuclei_[it->second];

            // Enforce correct matter/antimatter pairing:
            // electrons orbit matter nuclei, positrons orbit antinuclei
            if (pt == ELECTRON_TYPE_PHYS && nuc.is_anti) continue;
            if (pt == POSITRON_TYPE_PHYS && !nuc.is_anti) continue;

            int shell = (i < particles.orbital_shell.size())
                      ? static_cast<int>(particles.orbital_shell[i]) : -1;
            if (shell < 0 || shell > 3) continue;

            // Compute effective nuclear charge with Slater screening
            float screening = 0.0f;
            if (shell == 1) screening = static_cast<float>(SHELL_CAP_O[0]) * 0.85f;
            else if (shell == 2) screening = static_cast<float>(SHELL_CAP_O[0]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[1]) * 0.85f;
            else if (shell == 3) screening = static_cast<float>(SHELL_CAP_O[0]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[1]) * 1.0f
                                           + static_cast<float>(SHELL_CAP_O[2]) * 0.85f;
            float Z_eff = std::max(1.0f, static_cast<float>(nuc.Z) - screening);

            // Relative position and velocity
            glm::vec2 r_vec = readback_positions_[i] - nuc.center;
            glm::vec2 v_vec = readback_velocities_[i];
            // Subtract nucleus velocity (avg of protons) for better accuracy
            if (parent < readback_velocities_.size())
                v_vec -= readback_velocities_[parent];

            float r = glm::length(r_vec);
            if (r < 1.0f) continue;  // too close to center

            float v_sq = glm::dot(v_vec, v_vec);

            // Read stored orbit boost from genome[3] (matches shader ORBIT_BOOST)
            float stored_boost = 0.0f;
            if (i * GENOME_SIZE + 3 < particles.genomes.size())
                stored_boost = particles.genomes[i * GENOME_SIZE + 3];
            float orbit_boost = (stored_boost > 0.5f) ? stored_boost : cfg.orbit_boost[shell];

            // Effective K includes extra binding: Coulomb + (BOOST²-1)×K/r²
            // Shader applies F_total = K×Z_eff/r² + (BOOST²-1)×K/r²
            float K_eff = K_COULOMB_O * (Z_eff + orbit_boost * orbit_boost - 1.0f);

            // Angular momentum (scalar in 2D): L = r x v
            float L = r_vec.x * v_vec.y - r_vec.y * v_vec.x;

            // Total energy: E = ½v² - K/r  (mass=1 in simulation units)
            float E = 0.5f * v_sq - K_eff / r;

            // Only draw bound orbits (E < 0)
            if (E >= 0.0f) continue;

            // Keplerian orbit parameters
            float L_sq = L * L;
            float p = L_sq / K_eff;           // semi-latus rectum
            float e_sq = 1.0f + 2.0f * E * L_sq / (K_eff * K_eff);
            float ecc = (e_sq > 0.0f) ? std::sqrt(e_sq) : 0.0f;

            if (ecc >= 1.0f) continue;  // not a bound ellipse

            float a = p / (1.0f - ecc * ecc);  // semi-major axis
            float b = a * std::sqrt(1.0f - ecc * ecc);  // semi-minor axis

            // Sanity: skip degenerate orbits
            if (a < 2.0f || b < 1.0f || a > 500.0f) continue;

            // Orientation: angle of periapsis from Runge-Lenz vector
            //   A = v x L - K * r_hat
            float r_inv = 1.0f / r;
            float A_x = v_vec.y * L - K_eff * r_vec.x * r_inv;
            float A_y = -v_vec.x * L - K_eff * r_vec.y * r_inv;
            float orientation = std::atan2(A_y, A_x);

            // The ellipse center is offset from the focus (nucleus) by a*e along orientation
            glm::vec2 center_offset(std::cos(orientation) * a * ecc,
                                    std::sin(orientation) * a * ecc);
            glm::vec2 ellipse_center = nuc.center + center_offset;

            PhysicsInterface::OrbitPath path{};
            path.center = ellipse_center;
            path.semi_major = a;
            path.semi_minor = b;
            path.orientation = orientation;
            path.eccentricity = ecc;
            path.shell = shell;
            path.is_anti = nuc.is_anti;
            iface.orbit_paths.push_back(path);
            ++orbit_count;
        }
    }

    // ── Build molecule list from covalent bonds ────────────────────────────
    iface.molecule_list.clear();
    if (cfg.bonds_enabled && !bond_data_.empty() && !iface.element_list.empty()) {
        // Map nucleus rep → element_list index
        std::unordered_map<uint32_t, uint32_t> rep_to_elem;
        rep_to_elem.reserve(iface.element_list.size());
        for (uint32_t ei = 0; ei < iface.element_list.size(); ++ei) {
            rep_to_elem[iface.element_list[ei].rep] = ei;
        }

        // Union-Find to group bonded atoms
        std::vector<uint32_t> parent(iface.element_list.size());
        for (uint32_t i = 0; i < parent.size(); ++i) parent[i] = i;

        auto find = [&parent](uint32_t x) -> uint32_t {
            while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
            return x;
        };
        auto unite = [&](uint32_t a, uint32_t b) {
            a = find(a); b = find(b);
            if (a != b) parent[a] = b;
        };

        // Merge atoms connected by bonds
        for (uint32_t ei = 0; ei < iface.element_list.size(); ++ei) {
            uint32_t rep = iface.element_list[ei].rep;
            if (rep >= cfg.particle_count) continue;
            uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                if (base + s >= bond_data_.size()) break;
                uint32_t partner_rep = bond_data_[base + s];
                if (partner_rep == 0xFFFFFFFFu) continue;
                auto it = rep_to_elem.find(partner_rep);
                if (it == rep_to_elem.end()) continue;
                unite(ei, it->second);
            }
        }

        // Group elements by root
        std::unordered_map<uint32_t, std::vector<uint32_t>> groups;
        for (uint32_t ei = 0; ei < iface.element_list.size(); ++ei) {
            groups[find(ei)].push_back(ei);
        }

        // Build molecule entries (formula string built in UI where ELEMENT_SYMBOLS is available)
        for (auto& [root, members] : groups) {
            PhysicsInterface::MoleculeSummary mol;
            mol.atom_indices = members;

            // Aggregate energy, age, charge
            for (uint32_t ei : members) {
                auto& elem = iface.element_list[ei];
                mol.total_energy_MeV += elem.energy_MeV;
                if (elem.oldest_birth < mol.oldest_birth)
                    mol.oldest_birth = elem.oldest_birth;
                mol.total_charge += elem.Z - elem.electrons;
            }

            iface.molecule_list.push_back(std::move(mol));
        }

        // Sort: molecules (>1 atom) first, then single atoms
        std::sort(iface.molecule_list.begin(), iface.molecule_list.end(),
                  [](const PhysicsInterface::MoleculeSummary& a,
                     const PhysicsInterface::MoleculeSummary& b) {
            return a.atom_indices.size() > b.atom_indices.size();
        });

        // ── Molecule bestiary discovery tracking ─────────────────────────
        bool bestiary_changed = false;
        for (const auto& mol : iface.molecule_list) {
            if (mol.atom_indices.size() < 2) continue;
            // Build formula: count Z values, sort C first, H second, then alphabetical
            std::map<int, int> z_counts;
            for (uint32_t ei : mol.atom_indices)
                z_counts[iface.element_list[ei].Z]++;
            struct FE { const char* sym; int count; int Z; };
            std::vector<FE> entries;
            for (auto& [z, cnt] : z_counts) {
                const char* sym = (z >= 1 && z <= 118) ? element_symbol(z) : "?";
                entries.push_back({sym, cnt, z});
            }
            std::sort(entries.begin(), entries.end(), [](const FE& a, const FE& b) {
                if (a.Z == 6 && b.Z != 6) return true;
                if (b.Z == 6 && a.Z != 6) return false;
                if (a.Z == 1 && b.Z != 1) return true;
                if (b.Z == 1 && a.Z != 1) return false;
                return a.Z < b.Z;
            });
            std::string formula;
            for (auto& e : entries) {
                formula += e.sym;
                if (e.count > 1) formula += std::to_string(e.count);
            }
            // Search existing bestiary
            bool found = false;
            for (auto& be : iface.molecule_bestiary) {
                if (be.formula == formula) {
                    be.times_seen++;
                    found = true;
                    break;
                }
            }
            if (!found) {
                PhysicsInterface::MoleculeBestiaryEntry entry;
                entry.formula = formula;
                // Try template lookup first, then dynamic naming
                int tmpl = find_molecule_template(formula.c_str());
                if (tmpl >= 0) {
                    entry.name = MOLECULE_TEMPLATES[tmpl].name;
                } else {
                    std::vector<std::pair<int,int>> components;
                    for (auto& e : entries)
                        components.push_back({e.Z, e.count});
                    entry.name = name_molecule_dynamic(components);
                }
                entry.times_seen = 1;
                entry.atom_count = static_cast<uint32_t>(mol.atom_indices.size());
                entry.first_seen_session = iface.molecule_bestiary_session;
                entry.first_seen_time = static_cast<int64_t>(std::time(nullptr));
                iface.molecule_bestiary.push_back(std::move(entry));
                bestiary_changed = true;
            }
        }
        if (bestiary_changed)
            iface.save_molecule_bestiary();
    }

    // ── Accelerator: track source particle position ─────────────────────────
    if (iface.accel_mode && iface.accel_source_idx >= 0) {
        uint32_t si = static_cast<uint32_t>(iface.accel_source_idx);
        if (si < readback_positions_.size() && readback_energies_[si] > 0.01f) {
            iface.accel_source_world_pos = readback_positions_[si];
        } else {
            iface.accel_phase = 0;
            iface.accel_source_idx = -1;
            iface.push_notification("Target particle lost!", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        }
    }

    // ── Camera navigation (set by info card click) ─────────────────────────
    if (iface.navigate_to_particle >= 0 &&
        iface.navigate_to_particle < static_cast<int32_t>(cfg.particle_count)) {
        glm::vec2 target = readback_positions_[iface.navigate_to_particle];
        // camera_origin is the world-space center of the viewport
        cfg.camera_origin += (target - cfg.camera_origin) * 0.15f;

        // Snap when close enough, then select the target particle
        if (glm::length(target - cfg.camera_origin) < 2.0f) {
            cfg.camera_origin = target;
            iface.selected_particle_idx = iface.navigate_to_particle;
            iface.navigate_to_particle = -1;
        }
    }

    // ImGui render
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool request_reset = false;
    iface.sim_running = is_active;
    iface.render_imgui(cfg, particles, force_objects_, request_reset);
    // Read back sim_running — pause menu Resume button sets it to true
    is_active = iface.sim_running;

    if (request_reset) {
        push_undo_snapshot();
        if (!cfg.start_empty) {
            int pc = static_cast<int>(std::max(2.0f,
                std::pow(iface.particle_count_slider, 2.0f)));
            cfg.particle_count = static_cast<uint32_t>(pc);
        } else {
            cfg.particle_count = cfg.pool_size;
        }
        reset();
    }

    // Quit request from pause menu
    if (iface.request_quit) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // ── Save request ─────────────────────────────────────────────────────────
    if (iface.request_save) {
        iface.request_save = false;
        // Ensure we have fresh readback data
        if (readback_positions_.empty() && compute.is_ready()) {
            readback_positions_.resize(cfg.particle_count);
            readback_velocities_.resize(cfg.particle_count);
            readback_energies_.resize(cfg.particle_count);
            compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        }
        auto result = save_simulation(
            iface.save_filename, cfg, particles,
            readback_positions_, readback_velocities_, readback_energies_,
            force_objects_, force_object_count_,
            iface.field_em, iface.field_strong, iface.field_weak,
            iface.field_gravity, iface.field_higgs, iface.field_dark_energy,
            iface.field_intensity, iface.log_temperature);
        std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
        strncpy(iface.save_load_message, result.message.c_str(), sizeof(iface.save_load_message) - 1);
        iface.save_load_msg_timer = 3.0f;
        if (result.success) {
            try_unlock(ACH_FIRST_SAVE);
            // Capture thumbnail alongside save file
            std::string thumb_path = std::string(iface.save_filename) + ".thumb.png";
            capture_thumbnail(thumb_path);
        }
    }

    // ── Load request ─────────────────────────────────────────────────────────
    if (iface.request_load) {
        iface.request_load = false;
        auto r = load_simulation(iface.save_filename);
        if (r.success) {
            push_undo_snapshot();
            try_unlock(ACH_FIRST_LOAD);
            vkDeviceWaitIdle(vk.device);
            cfg = r.cfg;
            // Restore particle CPU data
            particles.positions = r.positions;
            particles.velocities = r.velocities;
            particles.types = r.types;
            particles.energies = r.energies;
            particles.angles = r.angles;
            particles.angular_velocities = r.angular_velocities;
            particles.genomes = r.genomes;
            // Restore per-type data
            std::memcpy(particles.forces.data(), r.forces.data(),
                        MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
            std::memcpy(particles.colors.data(), r.colors.data(),
                        MAX_PARTICLE_TYPES * sizeof(glm::vec4));
            std::memcpy(particles.behavior_flags, r.behavior_flags.data(),
                        MAX_PARTICLE_TYPES * sizeof(uint32_t));
            // Restore force objects
            force_object_count_ = r.force_object_count;
            std::memcpy(force_objects_, r.force_objects.data(),
                        MAX_FORCE_OBJECTS * sizeof(ForceObject));
            // Restore UI state
            iface.field_em        = r.field_em;
            iface.field_strong    = r.field_strong;
            iface.field_weak      = r.field_weak;
            iface.field_gravity   = r.field_gravity;
            iface.field_higgs       = r.field_higgs;
            iface.field_dark_energy = r.field_dark_energy;
            iface.field_intensity = r.field_intensity;
            iface.log_temperature = r.log_temperature;
            // Re-upload to GPU
            compute.clear_buffers(vk);
            compute.create_buffers(vk, particles);
            // Reset interaction state
            iface.selected_force_obj_idx = -1;
            iface.force_obj_placement_mode = false;
            iface.force_obj_move_mode = false;
            iface.selected_particle_idx = -1;
            iface.particle_move_mode = false;
            iface.select_mode = false;
            readback_positions_.clear();
            readback_velocities_.clear();
            readback_energies_.clear();
            frame_counter_ = 0;
        }
        std::memset(iface.save_load_message, 0, sizeof(iface.save_load_message));
        strncpy(iface.save_load_message, r.message.c_str(), sizeof(iface.save_load_message) - 1);
        iface.save_load_msg_timer = 3.0f;
    }

    // ── Undo/Redo requests ─────────────────────────────────────────────────
    if (iface.request_undo) {
        iface.request_undo = false;
        perform_undo();
    }
    if (iface.request_redo) {
        iface.request_redo = false;
        perform_redo();
    }

    // ── Element export request ───────────────────────────────────────────────
    if (iface.request_element_export) {
        iface.request_element_export = false;
        int32_t rep = iface.export_element_rep;

        // Find nucleus in detected_nuclei_
        const NucleusInfo* nuc = nullptr;
        for (const auto& n : detected_nuclei_) {
            if (n.rep == static_cast<uint32_t>(rep)) { nuc = &n; break; }
        }

        if (nuc && !readback_positions_.empty()) {
            glm::vec2 center = nuc->center;
            std::vector<ElementExportData> edata;

            // Gather nucleons
            for (uint32_t idx : nuc->proton_indices) {
                if (idx >= cfg.particle_count) continue;
                ElementExportData d{};
                d.dx = readback_positions_[idx].x - center.x;
                d.dy = readback_positions_[idx].y - center.y;
                d.vx = readback_velocities_[idx].x;
                d.vy = readback_velocities_[idx].y;
                d.energy = readback_energies_[idx];
                d.type = particles.types[idx];
                for (uint32_t g = 0; g < GENOME_SIZE; g++)
                    d.genome[g] = particles.genomes[idx * GENOME_SIZE + g];
                edata.push_back(d);
            }
            for (uint32_t idx : nuc->neutron_indices) {
                if (idx >= cfg.particle_count) continue;
                ElementExportData d{};
                d.dx = readback_positions_[idx].x - center.x;
                d.dy = readback_positions_[idx].y - center.y;
                d.vx = readback_velocities_[idx].x;
                d.vy = readback_velocities_[idx].y;
                d.energy = readback_energies_[idx];
                d.type = particles.types[idx];
                for (uint32_t g = 0; g < GENOME_SIZE; g++)
                    d.genome[g] = particles.genomes[idx * GENOME_SIZE + g];
                edata.push_back(d);
            }

            // Gather bound electrons
            int electron_count = 0;
            for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                if (particles.types[i] == ELECTRON_TYPE_PHYS &&
                    particles.orbital_parent[i] == rep) {
                    ElementExportData d{};
                    d.dx = readback_positions_[i].x - center.x;
                    d.dy = readback_positions_[i].y - center.y;
                    d.vx = readback_velocities_[i].x;
                    d.vy = readback_velocities_[i].y;
                    d.energy = readback_energies_[i];
                    d.type = particles.types[i];
                    for (uint32_t g = 0; g < GENOME_SIZE; g++)
                        d.genome[g] = particles.genomes[i * GENOME_SIZE + g];
                    edata.push_back(d);
                    electron_count++;
                }
            }

            // Build filename: saves/Element_Symbol_A.ppel
            std::error_code ec;
            fs::create_directories("saves", ec);

            // Element symbol lookup
            static const char* SYM[] = {
                "n","H","He","Li","Be","B","C","N","O","F","Ne",
                "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
                "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
                "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
                "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
                "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd"
            };
            int Z = nuc->Z, N = nuc->N, A = Z + N;
            const char* sym = (Z >= 0 && Z <= 60) ? SYM[Z] : "X";
            char fname[128];
            snprintf(fname, sizeof(fname), "saves/%s-%d.ppel", sym, A);

            auto result = export_element(fname, Z, N, electron_count, edata);
            iface.push_notification(
                result.success ? "Element exported!" : "Export failed",
                result.success ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (result.success) try_unlock(ACH_FIRST_EXPORT);
        }
    }

    // ── Molecule export request ─────────────────────────────────────────────
    if (iface.request_molecule_export) {
        iface.request_molecule_export = false;
        int32_t atom_rep = iface.export_molecule_atom_rep;

        // Resolve atom rep → molecule_list index
        int32_t mol_idx = -1;
        for (size_t mi = 0; mi < iface.molecule_list.size(); ++mi) {
            for (uint32_t ei : iface.molecule_list[mi].atom_indices) {
                if (ei < iface.element_list.size() &&
                    static_cast<int32_t>(iface.element_list[ei].rep) == atom_rep) {
                    mol_idx = static_cast<int32_t>(mi);
                    break;
                }
            }
            if (mol_idx >= 0) break;
        }

        if (mol_idx >= 0 && !readback_positions_.empty()) {
            auto& mol = iface.molecule_list[static_cast<size_t>(mol_idx)];

            // Element symbol table (shared with element export)
            static const char* SYM[] = {
                "n","H","He","Li","Be","B","C","N","O","F","Ne",
                "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
                "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
                "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
                "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
                "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd"
            };

            // Compute molecule center-of-mass
            glm::vec2 mol_com(0.0f);
            int atom_n = 0;
            for (uint32_t ei : mol.atom_indices) {
                if (ei >= iface.element_list.size()) continue;
                auto& elem = iface.element_list[ei];
                // Find nucleus center from detected_nuclei_
                for (const auto& nuc : detected_nuclei_) {
                    if (nuc.rep == elem.rep) {
                        mol_com += nuc.center;
                        atom_n++;
                        break;
                    }
                }
            }
            if (atom_n > 0) mol_com /= static_cast<float>(atom_n);

            // Build per-atom data and formula
            std::vector<MoleculeAtomData> atom_data;
            std::map<std::string, int> formula_counts;  // sorted by symbol

            for (uint32_t ei : mol.atom_indices) {
                if (ei >= iface.element_list.size()) continue;
                auto& elem = iface.element_list[ei];

                const NucleusInfo* nuc = nullptr;
                for (const auto& n : detected_nuclei_) {
                    if (n.rep == elem.rep) { nuc = &n; break; }
                }
                if (!nuc) continue;

                MoleculeAtomData ad{};
                ad.Z = elem.Z;
                ad.N = elem.N;
                ad.electrons = elem.electrons;
                ad.cx_offset = nuc->center.x - mol_com.x;
                ad.cy_offset = nuc->center.y - mol_com.y;

                // Gather nucleons
                for (uint32_t idx : nuc->proton_indices) {
                    if (idx >= cfg.particle_count) continue;
                    ElementExportData d{};
                    d.dx = readback_positions_[idx].x - nuc->center.x;
                    d.dy = readback_positions_[idx].y - nuc->center.y;
                    d.vx = readback_velocities_[idx].x;
                    d.vy = readback_velocities_[idx].y;
                    d.energy = readback_energies_[idx];
                    d.type = particles.types[idx];
                    for (uint32_t g = 0; g < GENOME_SIZE; g++)
                        d.genome[g] = particles.genomes[idx * GENOME_SIZE + g];
                    ad.particles.push_back(d);
                }
                for (uint32_t idx : nuc->neutron_indices) {
                    if (idx >= cfg.particle_count) continue;
                    ElementExportData d{};
                    d.dx = readback_positions_[idx].x - nuc->center.x;
                    d.dy = readback_positions_[idx].y - nuc->center.y;
                    d.vx = readback_velocities_[idx].x;
                    d.vy = readback_velocities_[idx].y;
                    d.energy = readback_energies_[idx];
                    d.type = particles.types[idx];
                    for (uint32_t g = 0; g < GENOME_SIZE; g++)
                        d.genome[g] = particles.genomes[idx * GENOME_SIZE + g];
                    ad.particles.push_back(d);
                }

                // Gather bound electrons
                for (uint32_t i = 0; i < cfg.particle_count; ++i) {
                    if (particles.types[i] == ELECTRON_TYPE_PHYS &&
                        particles.orbital_parent[i] == static_cast<int32_t>(elem.rep)) {
                        ElementExportData d{};
                        d.dx = readback_positions_[i].x - nuc->center.x;
                        d.dy = readback_positions_[i].y - nuc->center.y;
                        d.vx = readback_velocities_[i].x;
                        d.vy = readback_velocities_[i].y;
                        d.energy = readback_energies_[i];
                        d.type = particles.types[i];
                        for (uint32_t g = 0; g < GENOME_SIZE; g++)
                            d.genome[g] = particles.genomes[i * GENOME_SIZE + g];
                        ad.particles.push_back(d);
                    }
                }

                const char* sym = (elem.Z >= 1 && elem.Z <= 60) ? SYM[elem.Z] : "X";
                formula_counts[sym]++;
                atom_data.push_back(std::move(ad));
            }

            // Build formula string (e.g. "H2O")
            std::string formula;
            for (auto& [sym, cnt] : formula_counts) {
                formula += sym;
                if (cnt > 1) formula += std::to_string(cnt);
            }

            // Scan bonds between atoms (by nucleus rep)
            std::vector<MoleculeBondData> bond_list;
            for (size_t ai = 0; ai < mol.atom_indices.size(); ++ai) {
                uint32_t ei_a = mol.atom_indices[ai];
                if (ei_a >= iface.element_list.size()) continue;
                uint32_t rep_a = iface.element_list[ei_a].rep;
                if (rep_a >= cfg.particle_count) continue;
                uint32_t base = rep_a * MAX_BONDS_PER_PARTICLE;
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                    if (base + s >= bond_data_.size()) break;
                    uint32_t partner = bond_data_[base + s];
                    if (partner == 0xFFFFFFFFu) continue;
                    // Find partner atom index
                    for (size_t bi = ai + 1; bi < mol.atom_indices.size(); ++bi) {
                        uint32_t ei_b = mol.atom_indices[bi];
                        if (ei_b >= iface.element_list.size()) continue;
                        if (iface.element_list[ei_b].rep == partner) {
                            bond_list.push_back({static_cast<int32_t>(ai), static_cast<int32_t>(bi)});
                            break;
                        }
                    }
                }
            }

            // Save file
            std::error_code ec;
            fs::create_directories("saves", ec);
            char fname[128];
            snprintf(fname, sizeof(fname), "saves/%s.ppmol", formula.c_str());

            auto result = export_molecule(fname, formula, atom_data, bond_list);
            if (result.success) try_unlock(ACH_FIRST_MOLECULE_EXPORT);
            iface.push_notification(
                result.success ? "Molecule exported!" : "Export failed",
                result.success ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        }
    }

    // ── Element import request ───────────────────────────────────────────────
    if (iface.request_import) {
        iface.request_import = false;
        auto r = import_element(iface.save_filename);
        if (r.success && !r.particles.empty()) {
            // Spawn at camera center
            glm::vec2 spawn_pos = cfg.camera_origin;

            // Ensure we have readback data
            if (readback_energies_.empty() && compute.is_ready()) {
                readback_positions_.resize(cfg.particle_count);
                readback_velocities_.resize(cfg.particle_count);
                readback_energies_.resize(cfg.particle_count);
                compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }

            uint32_t n = cfg.particle_count;
            uint32_t search_start = 0;
            int placed = 0;

            for (const auto& p : r.particles) {
                // Find dormant slot
                uint32_t slot = UINT32_MAX;
                for (uint32_t i = search_start; i < n; ++i) {
                    if (readback_energies_[i] < 0.01f) { slot = i; break; }
                }
                if (slot == UINT32_MAX) break;
                search_start = slot + 1;

                readback_positions_[slot] = (glm::vec2(spawn_pos.x + p.dx, spawn_pos.y + p.dy));
                readback_velocities_[slot] = glm::vec2(p.vx, p.vy);
                readback_energies_[slot] = p.energy;
                particles.types[slot] = p.type;
                for (uint32_t g = 0; g < GENOME_SIZE; g++)
                    particles.genomes[slot * GENOME_SIZE + g] = p.genome[g];
                particles.orbital_parent[slot] = -1;  // will be reassigned by update_orbitals
                particles.orbital_shell[slot] = -1;
                particles.excitation_timer[slot] = 0;
                placed++;
            }

            // Upload modified data to GPU
            if (placed > 0) {
                vkDeviceWaitIdle(vk.device);
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
                compute.upload_dynamic_data(vk, particles);
            }

            char msg[128];
            snprintf(msg, sizeof(msg), "Imported Z=%d (A=%d) — %d particles", r.Z, r.Z + r.N, placed);
            iface.push_notification(msg, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
            try_unlock(ACH_FIRST_IMPORT);
        } else {
            iface.push_notification(
                r.message.c_str(),
                ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        }
    }

    // ── Molecule import request ──────────────────────────────────────────────
    if (iface.request_molecule_import) {
        iface.request_molecule_import = false;
        auto r = import_molecule(iface.save_filename);
        if (r.success && !r.atoms.empty()) {
            glm::vec2 spawn_pos = cfg.camera_origin;

            // Ensure we have readback data
            if (readback_energies_.empty() && compute.is_ready()) {
                readback_positions_.resize(cfg.particle_count);
                readback_velocities_.resize(cfg.particle_count);
                readback_energies_.resize(cfg.particle_count);
                compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);
            }

            uint32_t n = cfg.particle_count;
            uint32_t search_start = 0;
            int placed = 0;

            // Track the first proton slot per atom (for bond reconstruction)
            std::vector<uint32_t> atom_reps(r.atoms.size(), UINT32_MAX);

            for (size_t ai = 0; ai < r.atoms.size(); ++ai) {
                auto& atom = r.atoms[ai];
                glm::vec2 atom_center(spawn_pos.x + atom.cx_offset, spawn_pos.y + atom.cy_offset);
                uint32_t first_proton_slot = UINT32_MAX;

                for (const auto& p : atom.particles) {
                    // Find dormant slot
                    uint32_t slot = UINT32_MAX;
                    for (uint32_t i = search_start; i < n; ++i) {
                        if (readback_energies_[i] < 0.01f) { slot = i; break; }
                    }
                    if (slot == UINT32_MAX) break;
                    search_start = slot + 1;

                    readback_positions_[slot] = (glm::vec2(atom_center.x + p.dx, atom_center.y + p.dy));
                    readback_velocities_[slot] = glm::vec2(p.vx, p.vy);
                    readback_energies_[slot] = p.energy;
                    particles.types[slot] = p.type;
                    for (uint32_t g = 0; g < GENOME_SIZE; g++)
                        particles.genomes[slot * GENOME_SIZE + g] = p.genome[g];

                    // Track first proton as nucleus rep
                    if (p.type == PROTON_TYPE && first_proton_slot == UINT32_MAX)
                        first_proton_slot = slot;

                    // Set orbital_parent for electrons to nucleus rep
                    if (p.type == ELECTRON_TYPE_PHYS && first_proton_slot != UINT32_MAX)
                        particles.orbital_parent[slot] = static_cast<int32_t>(first_proton_slot);
                    else
                        particles.orbital_parent[slot] = -1;
                    particles.orbital_shell[slot] = -1;  // assigned by update_orbitals
                    particles.excitation_timer[slot] = 0;

                    placed++;
                }

                atom_reps[ai] = first_proton_slot;
            }

            // Reconstruct bonds between atoms
            if (!bond_data_.empty()) {
                for (const auto& b : r.bonds) {
                    if (b.atom_a < 0 || b.atom_b < 0) continue;
                    size_t a = static_cast<size_t>(b.atom_a);
                    size_t bx = static_cast<size_t>(b.atom_b);
                    if (a >= atom_reps.size() || bx >= atom_reps.size()) continue;
                    uint32_t rep_a = atom_reps[a];
                    uint32_t rep_b = atom_reps[bx];
                    if (rep_a == UINT32_MAX || rep_b == UINT32_MAX) continue;

                    // Add bond A→B
                    uint32_t base_a = rep_a * MAX_BONDS_PER_PARTICLE;
                    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                        if (base_a + s >= bond_data_.size()) break;
                        if (bond_data_[base_a + s] == 0xFFFFFFFFu) {
                            bond_data_[base_a + s] = rep_b;
                            break;
                        }
                    }
                    // Add bond B→A
                    uint32_t base_b = rep_b * MAX_BONDS_PER_PARTICLE;
                    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                        if (base_b + s >= bond_data_.size()) break;
                        if (bond_data_[base_b + s] == 0xFFFFFFFFu) {
                            bond_data_[base_b + s] = rep_a;
                            break;
                        }
                    }
                }
            }

            // Upload modified data to GPU
            if (placed > 0) {
                vkDeviceWaitIdle(vk.device);
                compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
                compute.upload_dynamic_data(vk, particles);
            }

            char msg[128];
            snprintf(msg, sizeof(msg), "Imported %s (%d atoms, %d particles)",
                     r.formula.c_str(), static_cast<int>(r.atoms.size()), placed);
            iface.push_notification(msg, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        } else {
            iface.push_notification(
                r.message.c_str(),
                ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        }
    }

    ImGui::Render();

    // Handle render scale change (supersampling)
    {
        int scale = std::max(1, std::min(4, iface.prefs.render_scale));
        uint32_t want_w = REGION_W * static_cast<uint32_t>(scale);
        uint32_t want_h = REGION_H * static_cast<uint32_t>(scale);
        if (want_w != compute.render_w || want_h != compute.render_h) {
            compute.resize_render_texture(vk, want_w, want_h);
            renderer.update_texture_binding(vk, compute);
        }
    }

    // Handle swapchain resize
    if (renderer.swapchain_dirty) {
        renderer.on_resize(vk, window, compute);
        renderer.swapchain_dirty = false;
    }

    // Draw
    if (!renderer.draw_frame(vk, window, compute, is_active)) {
        renderer.on_resize(vk, window, compute);
    }
    if (is_active) {
        vkQueueWaitIdle(vk.queue);
    }
}

// ── Screenshot capture for save thumbnails ──────────────────────────────────

bool PhysicsSimulation::capture_thumbnail(const std::string& png_path,
                                          uint32_t thumb_w, uint32_t thumb_h) {
    if (!compute.is_ready()) return false;

    vkDeviceWaitIdle(vk.device);

    uint32_t src_w = compute.render_w;
    uint32_t src_h = compute.render_h;
    VkDeviceSize full_size = static_cast<VkDeviceSize>(src_w) * src_h * 4 * sizeof(float);

    // Create staging buffer (HOST_VISIBLE) to receive the image data
    Buffer staging = vk.create_buffer(
        full_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy image to staging buffer
    VkCommandBuffer cmd = vk.begin_single_command();

    // Transition GENERAL -> TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = compute.particle_texture.handle;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { src_w, src_h, 1 };
    vkCmdCopyImageToBuffer(cmd, compute.particle_texture.handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle, 1, &region);

    // Transition back TRANSFER_SRC_OPTIMAL -> GENERAL
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vk.end_single_command(cmd);

    // Map staging buffer and downsample to thumbnail
    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, full_size, 0, &mapped);
    const float* src_pixels = reinterpret_cast<const float*>(mapped);

    std::vector<uint8_t> thumb(thumb_w * thumb_h * 4);

    // Nearest-neighbor downsample RGBA32F -> RGBA8
    for (uint32_t ty = 0; ty < thumb_h; ++ty) {
        uint32_t sy = ty * src_h / thumb_h;
        for (uint32_t tx = 0; tx < thumb_w; ++tx) {
            uint32_t sx = tx * src_w / thumb_w;
            uint32_t src_idx = (sy * src_w + sx) * 4;
            uint32_t dst_idx = (ty * thumb_w + tx) * 4;
            auto f2b = [](float f) -> uint8_t {
                if (f <= 0.0f) return 0;
                if (f >= 1.0f) return 255;
                return static_cast<uint8_t>(f * 255.0f + 0.5f);
            };
            thumb[dst_idx + 0] = f2b(src_pixels[src_idx + 0]);
            thumb[dst_idx + 1] = f2b(src_pixels[src_idx + 1]);
            thumb[dst_idx + 2] = f2b(src_pixels[src_idx + 2]);
            thumb[dst_idx + 3] = 255;  // opaque
        }
    }

    vkUnmapMemory(vk.device, staging.memory);
    vk.destroy_buffer(staging);

    // Write PNG
    int result = stbi_write_png(png_path.c_str(), static_cast<int>(thumb_w),
                                static_cast<int>(thumb_h), 4, thumb.data(),
                                static_cast<int>(thumb_w * 4));
    return result != 0;
}
