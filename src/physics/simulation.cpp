#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include "physics/molecules.h"
#include "physics/save_load.h"
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

// ── Orbital constants (single source of truth — must match shader K_COULOMB, SOFTEN_MIN²) ──
static constexpr float R_BOHR      = 15.0f;   // base Bohr radius (hydrogen ground state in px)
static constexpr float K_COULOMB_O = 1200.0f;  // Coulomb constant (px-based units)
static constexpr float SOFTEN_SQ_O = 64.0f;    // softening = SOFTEN_MIN² = 8²
static constexpr int   SHELL_CAP_O[] = {2, 8, 18, 32};
static constexpr int   NUM_SHELLS_O  = 4;

// ── SM particle labels (for notifications & event log) ──────────────────────
static const char* const SM_LABELS[] = {
    "p","n","e\xe2\x81\xbb","\xce\xb3","e\xe2\x81\xba","p\xcc\x84",
    "\xce\xbd" "e","\xce\xbc\xe2\x81\xbb","\xce\xbc\xe2\x81\xba",
    "\xcf\x84\xe2\x81\xbb","\xcf\x84\xe2\x81\xba","\xce\xbd\xce\xbc","\xce\xbd\xcf\x84",
    "u","d","s","c","t","b",
    "u\xcc\x84","d\xcc\x84","s\xcc\x84","c\xcc\x84","t\xcc\x84","b\xcc\x84",
    "g","W\xe2\x81\xba","W\xe2\x81\xbb","Z\xe2\x81\xb0","H",
    "G","DM","DE"
};

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
            // 3.5. Particle Accelerator (fire AT the selected target)
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
                do_accelerator_fire(world_pos);
            }
        }
        else if (iface.mirror_placement_mode) {
            // 3.6. Mirror placement (two-click)
            if (iface.mirror_placement_phase == 0) {
                iface.mirror_endpoint1 = world_pos;
                iface.mirror_placement_phase = 1;
            } else {
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
            place_force_object(world_pos, static_cast<ForceObjectType>(iface.force_obj_placement_type));
            iface.force_obj_placement_mode = false;
        }
        else if (iface.pending_spawn) {
            // 4. Spawn particle
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

// ── Relativistic kinematics helpers ───────────────────────────────────────────

static float compute_gamma(const glm::vec2& vel) {
    float beta = std::min(glm::length(vel) / C_SIM, 0.9999f);
    return 1.0f / std::sqrt(1.0f - beta * beta);
}

// Total relativistic energy E = γm₀c² (in MeV)
static float total_rel_energy(uint32_t type, const glm::vec2& vel) {
    float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
    if (m0 < 0.001f) return glm::length(vel) / C_SIM * 1.0f; // massless: E ∝ β
    return compute_gamma(vel) * m0;
}

// Convert kinetic energy (MeV) to sim speed for a given particle type
static float ke_to_speed(float KE_MeV, uint32_t type) {
    float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
    if (m0 < 0.001f) return C_SIM;  // massless always at c
    float gamma = 1.0f + KE_MeV / m0;
    float beta = std::sqrt(std::max(0.0f, 1.0f - 1.0f / (gamma * gamma)));
    return beta * C_SIM;
}

// Convert MeV to energy buffer [0, 1]
static float mev_to_ebuf(float MeV) {
    return std::clamp(MeV / E_SCALE_MEV, 0.0f, 1.0f);
}

// 2-body decay kinematics: returns child momentum magnitude in parent rest frame (MeV/c)
static float two_body_decay_momentum(float M, float m1, float m2) {
    float sum = m1 + m2, diff = m1 - m2;
    float arg = (M * M - sum * sum) * (M * M - diff * diff);
    return (arg > 0.0f) ? std::sqrt(arg) / (2.0f * M) : 0.0f;
}

// ── Helper: write genome for a particle type ─────────────────────────────────

static void write_spawn_genome(Particles& particles, uint32_t slot, uint32_t type,
                               std::mt19937& rng, uint32_t frame = 0) {
    float charge = (type < PHYS_PARTICLE_TYPES) ? PHYS_CHARGE[type] : 0.0f;
    float spin   = (type < PHYS_PARTICLE_TYPES) ? PHYS_SPIN[type] : 0.0f;
    float decay  = (type < PHYS_PARTICLE_TYPES) ? PHYS_DECAY_RATE[type] : 0.0f;
    float color  = 0.0f;

    // Randomize spin sign for fermions
    if (spin == 0.5f) {
        std::uniform_int_distribution<int> coin(0, 1);
        spin = coin(rng) ? 0.5f : -0.5f;
    }

    // Random color charge for quarks
    if (type >= UP_QUARK_TYPE && type <= BOTTOM_QUARK_TYPE) {
        std::uniform_int_distribution<int> rgb(1, 3);
        color = static_cast<float>(rgb(rng));  // 1=R, 2=G, 3=B
    } else if (type >= ANTI_UP_TYPE && type <= ANTI_BOTTOM_TYPE) {
        std::uniform_int_distribution<int> rgb(1, 3);
        color = static_cast<float>(-rgb(rng)); // -1, -2, -3
    } else if (type == GLUON_TYPE_PHYS) {
        // SU(3) octet: encode as (color*10 + anticolor), e.g. 1.2 = red-antigreen
        static const float GLUON_COLORS[8] = {1.2f,1.3f,2.1f,2.3f,3.1f,3.2f,1.1f,2.2f};
        std::uniform_int_distribution<int> oct(0, 7);
        color = GLUON_COLORS[oct(rng)];
    }

    particles.types[slot] = type;
    particles.genomes[slot * GENOME_SIZE + 0] = charge;
    particles.genomes[slot * GENOME_SIZE + 1] = spin;
    particles.genomes[slot * GENOME_SIZE + 2] = color;
    particles.genomes[slot * GENOME_SIZE + 3] = decay;

    // Stamp birth frame for age tracking
    if (slot < particles.birth_frames.size())
        particles.birth_frames[slot] = frame;
}

// ── Accelerator fire ─────────────────────────────────────────────────────────

void PhysicsSimulation::do_accelerator_fire(glm::vec2 aim_world_pos) {
    if (!compute.is_ready()) return;
    int32_t src = iface.accel_source_idx;
    if (src < 0 || src >= static_cast<int32_t>(cfg.particle_count)) return;

    uint32_t n = cfg.particle_count;
    if (readback_positions_.size() != n) {
        readback_positions_.resize(n);
        readback_velocities_.resize(n);
        readback_energies_.resize(n);
    }
    compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

    // Validate target is still alive
    if (readback_energies_[src] < 0.01f) {
        iface.accel_phase = 0;
        iface.accel_source_idx = -1;
        iface.push_notification("Target particle died!", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        return;
    }

    glm::vec2 target_pos = readback_positions_[src];
    glm::vec2 dir = target_pos - aim_world_pos;  // from click toward target
    float dist = glm::length(dir);
    if (dist < 1.0f) return;  // too close
    dir /= dist;

    uint32_t fire_type = static_cast<uint32_t>(iface.accel_fire_type);
    float speed = iface.accel_speed;

    // Build shot directions
    std::vector<glm::vec2> shot_dirs;
    if (iface.accel_fire_mode == 1) {
        // Triple: -5°, 0°, +5°
        float spread = 5.0f * 3.14159265f / 180.0f;
        for (int s = -1; s <= 1; ++s) {
            float a = static_cast<float>(s) * spread;
            float cs = std::cos(a), sn = std::sin(a);
            shot_dirs.push_back({dir.x * cs - dir.y * sn,
                                 dir.x * sn + dir.y * cs});
        }
    } else {
        shot_dirs.push_back(dir);
    }

    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    float offset_dist = cfg.radius * 4.0f + 8.0f;

    std::mt19937 rng(static_cast<uint32_t>(aim_world_pos.x * 1000.0f + aim_world_pos.y + frame_counter_));
    uint32_t search_from = 0;
    bool any_spawned = false;

    for (auto& sd : shot_dirs) {
        // Find dormant slot
        uint32_t slot = UINT32_MAX;
        for (uint32_t i = search_from; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) { slot = i; break; }
        }
        if (slot == UINT32_MAX) break;
        search_from = slot + 1;

        glm::vec2 spawn_pos = aim_world_pos + sd * offset_dist;
        spawn_pos.x = std::fmod(spawn_pos.x + rw, rw);
        spawn_pos.y = std::fmod(spawn_pos.y + rh, rh);

        readback_positions_[slot] = spawn_pos;

        // Massless particles always travel at c
        float m0 = (fire_type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[fire_type] : 0.0f;
        float actual_speed = (m0 < 0.001f) ? C_SIM : speed;
        readback_velocities_[slot] = sd * actual_speed;

        // Relativistic energy: E = γm₀c² for massive, E ∝ β for massless
        float beta = std::min(actual_speed / C_SIM, 0.9999f);
        float E_MeV;
        if (m0 < 0.001f) {
            E_MeV = beta;  // massless: energy proxy
        } else {
            float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
            E_MeV = gamma * m0;  // total relativistic energy
        }
        readback_energies_[slot] = mev_to_ebuf(E_MeV);

        write_spawn_genome(particles, slot, fire_type, rng, frame_counter_);
        any_spawned = true;
    }

    if (any_spawned) {
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
        try_unlock(ACH_FIRST_ACCELERATOR);
        // Cosmic ray: fire at near-lightspeed
        if (speed >= 295.0f) try_unlock(ACH_COSMIC_RAY);
    }
}

// ── Spawn a single atom (nucleus + electrons) at world position ──────────────

uint32_t PhysicsSimulation::spawn_atom_at(glm::vec2 pos, int Z, int N,
                                          std::mt19937& rng, uint32_t& search_start) {
    uint32_t n = cfg.particle_count;

    auto find_dormant = [&](uint32_t start_from) -> uint32_t {
        for (uint32_t i = start_from; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) return i;
        }
        return UINT32_MAX;
    };

    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    auto wrap_pos = [&](glm::vec2 p) -> glm::vec2 {
        p.x = std::fmod(p.x + rw, rw);
        p.y = std::fmod(p.y + rh, rh);
        return p;
    };

    if (N < 0) N = Z;
    int A = Z + N;

    const float NUC_SPACING = 3.8f;

    struct NucPos { float x, y; };
    std::vector<NucPos> nuc_positions;
    nuc_positions.reserve(A);

    uint32_t first_nucleon_slot = UINT32_MAX;

    nuc_positions.push_back({0.0f, 0.0f});
    int ring = 1;
    while (static_cast<int>(nuc_positions.size()) < A) {
        for (int side = 0; side < 6 && static_cast<int>(nuc_positions.size()) < A; ++side) {
            for (int step = 0; step < ring && static_cast<int>(nuc_positions.size()) < A; ++step) {
                float angle0 = 3.14159265f / 3.0f * side + 3.14159265f / 2.0f;
                float angle1 = 3.14159265f / 3.0f * (side + 1) + 3.14159265f / 2.0f;
                float t = static_cast<float>(step) / static_cast<float>(ring);
                float px = ring * NUC_SPACING * std::cos(angle0) * (1.0f - t)
                         + ring * NUC_SPACING * std::cos(angle1) * t;
                float py = ring * NUC_SPACING * std::sin(angle0) * (1.0f - t)
                         + ring * NUC_SPACING * std::sin(angle1) * t;
                nuc_positions.push_back({px, py});
            }
        }
        ++ring;
    }

    std::vector<uint32_t> nuc_types;
    nuc_types.reserve(A);
    int p_left = Z, n_left = N;
    for (int k = 0; k < A; ++k) {
        if (k % 2 == 0) {
            if (p_left > 0) { nuc_types.push_back(PROTON_TYPE); --p_left; }
            else            { nuc_types.push_back(NEUTRON_TYPE); --n_left; }
        } else {
            if (n_left > 0) { nuc_types.push_back(NEUTRON_TYPE); --n_left; }
            else            { nuc_types.push_back(PROTON_TYPE); --p_left; }
        }
    }

    float nuc_extent = 0.0f;
    for (int k = 0; k < A; ++k) {
        float r2 = nuc_positions[k].x * nuc_positions[k].x
                  + nuc_positions[k].y * nuc_positions[k].y;
        nuc_extent = std::max(nuc_extent, std::sqrt(r2));
    }
    nuc_extent += NUC_SPACING * 0.5f;

    for (int k = 0; k < A; ++k) {
        uint32_t slot = find_dormant(search_start);
        if (slot == UINT32_MAX) break;
        search_start = slot + 1;

        if (first_nucleon_slot == UINT32_MAX) first_nucleon_slot = slot;

        readback_positions_[slot] = wrap_pos(pos + glm::vec2(nuc_positions[k].x, nuc_positions[k].y));
        readback_velocities_[slot] = glm::vec2(0.0f);
        readback_energies_[slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[nuc_types[k]]);

        write_spawn_genome(particles, slot, nuc_types[k], rng, frame_counter_);
    }

    const float NUC_CLEAR = nuc_extent + 4.0f;

    int electrons_left = Z;
    int shell_fill_spawn[4] = {0, 0, 0, 0};
    for (int shell = 0; shell < 4 && electrons_left > 0; ++shell) {
        int cap = std::min(SHELL_CAP_O[shell], electrons_left);
        float n_shell = static_cast<float>(shell + 1);

        int inner_e = 0;
        for (int s = 0; s < shell; ++s)
            inner_e += shell_fill_spawn[s];

        float Z_eff = std::max(1.0f, static_cast<float>(Z) - static_cast<float>(inner_e));
        float R_bohr = n_shell * n_shell * R_BOHR / Z_eff;
        float R_target = std::max(R_bohr, 8.0f);
        R_target = std::max(R_target, NUC_CLEAR + shell * 8.0f);

        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);

        // Per-shell boost from SimConfig
        float orbit_boost = cfg.orbit_boost[shell];
        float v_orbital = (L_ground / R_target) * orbit_boost;

        // Stagger shells by configurable offset for cloud appearance
        float shell_offset = shell * cfg.orbital_shell_offset;

        for (int e = 0; e < cap; ++e) {
            uint32_t slot = find_dormant(search_start);
            if (slot == UINT32_MAX) break;
            search_start = slot + 1;

            float angle = shell_offset + 2.0f * 3.14159265f * static_cast<float>(e) / static_cast<float>(cap);
            glm::vec2 offset(R_target * std::cos(angle), R_target * std::sin(angle));
            glm::vec2 tangent(-std::sin(angle), std::cos(angle));

            readback_positions_[slot] = wrap_pos(pos + offset);
            readback_velocities_[slot] = tangent * v_orbital;

            float e_speed = v_orbital;
            float e_beta = std::min(e_speed / C_SIM, 0.9999f);
            float e_gamma = 1.0f / std::sqrt(1.0f - e_beta * e_beta);
            readback_energies_[slot] = mev_to_ebuf(e_gamma * PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS]);

            write_spawn_genome(particles, slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
            particles.genomes[slot * GENOME_SIZE + 2] = L_ground;
            particles.genomes[slot * GENOME_SIZE + 3] = orbit_boost;  // stored for shader
            if (slot < particles.orbital_shell.size()) {
                particles.orbital_shell[slot] = static_cast<int8_t>(shell);
                particles.excitation_timer[slot] = 0;
            }
        }

        shell_fill_spawn[shell] = cap;
        electrons_left -= cap;
    }

    return first_nucleon_slot;
}

// ── Spawn at world position ──────────────────────────────────────────────────

void PhysicsSimulation::do_spawn_at_world(glm::vec2 world_pos) {
    if (!compute.is_ready()) return;

    uint32_t n = cfg.particle_count;
    if (readback_positions_.size() != n) {
        readback_positions_.resize(n);
        readback_velocities_.resize(n);
        readback_energies_.resize(n);
    }
    compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

    uint32_t type = static_cast<uint32_t>(iface.spawn_type);

    int count = iface.spawn_count;
    float scatter = iface.spawn_scatter;
    std::mt19937 rng(static_cast<uint32_t>(world_pos.x * 1000.0f + world_pos.y));
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // ── Helper: wrap position into toroidal world ─────────────────────────
    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    auto wrap_pos = [&](glm::vec2 p) -> glm::vec2 {
        p.x = std::fmod(p.x + rw, rw);
        p.y = std::fmod(p.y + rh, rh);
        return p;
    };

    // ── Molecule spawn ───────────────────────────────────────────────────
    if (iface.spawn_molecule_idx >= 0 &&
        iface.spawn_molecule_idx < MOLECULE_TEMPLATE_COUNT) {
        const auto& mol = MOLECULE_TEMPLATES[iface.spawn_molecule_idx];
        uint32_t search_start = 0;

        // Nuclear extent: radius of hex-packed nucleus (NUC_SPACING=3.8)
        auto nuc_extent = [](int Z) -> float {
            int N_def = (Z <= 2) ? Z : Z + static_cast<int>(std::round(Z * 0.31f));
            int A = Z + N_def;
            if (A <= 0) return 0.0f;
            int ring = 0, placed = 1;
            while (placed < A) { ++ring; placed += 6 * ring; }
            return ring * 3.8f + 1.9f;
        };

        // Scale template so no bonded atom pair overlaps within nuclear force range.
        // Templates designed with ~22px spacing; we need at least:
        //   actual_dist >= extent_A + extent_B + SAFE_MARGIN
        // where SAFE_MARGIN = YUKAWA_RANGE(8) + 2px buffer = 10
        const float TEMPLATE_SPACING = 22.0f;
        const float SAFE_MARGIN = 10.0f;  // keep nucleons outside Yukawa range
        float min_scale = cfg.bond_rest_length / TEMPLATE_SPACING;
        for (uint32_t ai = 0; ai < mol.atom_count; ++ai) {
            for (uint32_t aj = ai + 1; aj < mol.atom_count; ++aj) {
                float dx = mol.atoms[ai].dx - mol.atoms[aj].dx;
                float dy = mol.atoms[ai].dy - mol.atoms[aj].dy;
                float tdist = std::sqrt(dx * dx + dy * dy);
                if (tdist < 1.0f) continue;
                float safe = nuc_extent(mol.atoms[ai].Z) + nuc_extent(mol.atoms[aj].Z)
                           + SAFE_MARGIN;
                min_scale = std::max(min_scale, safe / tdist);
            }
        }
        float scale = min_scale;

        // Track spawned atom reps for pre-bonding
        struct SpawnedAtom { uint32_t rep; glm::vec2 pos; int Z; };
        std::vector<SpawnedAtom> spawned_atoms;
        spawned_atoms.reserve(mol.atom_count);

        for (uint32_t ai = 0; ai < mol.atom_count; ++ai) {
            int Z = mol.atoms[ai].Z;
            int N = default_neutron_count(Z);
            glm::vec2 atom_pos = wrap_pos(world_pos +
                glm::vec2(mol.atoms[ai].dx * scale, mol.atoms[ai].dy * scale));
            uint32_t rep = spawn_atom_at(atom_pos, Z, N, rng, search_start);
            if (rep != UINT32_MAX)
                spawned_atoms.push_back({rep, atom_pos, Z});
        }

        // Pre-create covalent bonds between nearby atoms (before GPU forces scatter them)
        if (cfg.bonds_enabled && spawned_atoms.size() > 1) {
            size_t required = static_cast<size_t>(cfg.particle_count) * MAX_BONDS_PER_PARTICLE;
            if (bond_data_.size() < required)
                bond_data_.resize(required, 0xFFFFFFFFu);

            const float FORM_R_SQ = cfg.bond_form_radius * cfg.bond_form_radius;

            // Count existing bonds for a rep
            auto count_bonds = [&](uint32_t rep) -> int {
                int cnt = 0;
                uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s)
                    if (bond_data_[base + s] != 0xFFFFFFFFu) ++cnt;
                return cnt;
            };

            // Add one bond slot (returns true if added)
            auto add_slot = [&](uint32_t rep, uint32_t partner) -> bool {
                uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                    if (bond_data_[base + s] == 0xFFFFFFFFu) {
                        bond_data_[base + s] = partner;
                        return true;
                    }
                }
                return false;
            };

            // Sort atoms by valence (highest first) so carbons bond first
            std::vector<size_t> order(spawned_atoms.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return valence_from_Z(spawned_atoms[a].Z) > valence_from_Z(spawned_atoms[b].Z);
            });

            for (size_t ii = 0; ii < order.size(); ++ii) {
                size_t i = order[ii];
                int val_a = valence_from_Z(spawned_atoms[i].Z);
                if (val_a <= 0) continue;
                if (count_bonds(spawned_atoms[i].rep) >= val_a) continue;

                // Collect candidate partners sorted by distance
                struct Cand { size_t idx; float d2; };
                std::vector<Cand> cands;
                for (size_t j = 0; j < spawned_atoms.size(); ++j) {
                    if (j == i) continue;
                    int val_b = valence_from_Z(spawned_atoms[j].Z);
                    if (val_b <= 0) continue;
                    glm::vec2 delta = spawned_atoms[j].pos - spawned_atoms[i].pos;
                    float d2 = glm::dot(delta, delta);
                    if (d2 <= FORM_R_SQ)
                        cands.push_back({j, d2});
                }
                std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
                    return a.d2 < b.d2;
                });

                for (auto& c : cands) {
                    if (count_bonds(spawned_atoms[i].rep) >= val_a) break;
                    int val_b = valence_from_Z(spawned_atoms[c.idx].Z);
                    if (count_bonds(spawned_atoms[c.idx].rep) >= val_b) continue;

                    // Create bidirectional bond
                    if (add_slot(spawned_atoms[i].rep, spawned_atoms[c.idx].rep))
                        add_slot(spawned_atoms[c.idx].rep, spawned_atoms[i].rep);
                }
            }

            // Upload bond data for GPU
            particles.bond_partners_ptr = bond_data_.data();
            particles.bond_partners_count = static_cast<uint32_t>(bond_data_.size());
        }

        iface.spawn_molecule_idx = -1;
    }

    // ── Dynamic atom spawn (periodic table) ───────────────────────────────
    else if (iface.spawn_atom_Z > 0) {
        uint32_t search_start = 0;
        spawn_atom_at(world_pos, iface.spawn_atom_Z, iface.spawn_atom_N, rng, search_start);
    }

    // ── Group template spawn ──────────────────────────────────────────────
    else {
    const GroupTemplate* resolved_tmpl = nullptr;
    if (iface.spawn_group >= 0 && iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &GROUP_TEMPLATES[iface.spawn_group];
    } else if (iface.spawn_group >= GROUP_TEMPLATE_COUNT_VAL &&
               iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL + HADRON_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &HADRON_TEMPLATES[iface.spawn_group - GROUP_TEMPLATE_COUNT_VAL];
    }

    if (resolved_tmpl) {
        const auto& tmpl = *resolved_tmpl;

        // Find nucleus center and count protons for Z
        glm::vec2 nucleus_center(0.0f);
        int nucleon_count = 0;
        int template_Z = 0;
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t t = tmpl.atoms[a].type;
            if (t == PROTON_TYPE || t == NEUTRON_TYPE || t == ANTIPROTON_TYPE_PHYS) {
                nucleus_center += glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy);
                nucleon_count++;
                if (t == PROTON_TYPE) template_Z++;
            }
        }
        if (nucleon_count > 0) nucleus_center /= static_cast<float>(nucleon_count);

        // Collect electron offsets sorted by distance (for shell assignment)
        struct ElectronEntry { float dx, dy, dist; uint32_t type; };
        std::vector<ElectronEntry> electron_entries;
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t t = tmpl.atoms[a].type;
            if (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS) {
                glm::vec2 d = glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy) - nucleus_center;
                electron_entries.push_back({tmpl.atoms[a].dx, tmpl.atoms[a].dy,
                                            glm::length(d), t});
            }
        }
        std::sort(electron_entries.begin(), electron_entries.end(),
            [](const ElectronEntry& a, const ElectronEntry& b) { return a.dist < b.dist; });

        // Pre-compute shell assignments for electrons
        int shell_fill[4] = {0, 0, 0, 0};
        struct ShellInfo { float L_ground; int shell; float boost; };
        std::vector<ShellInfo> electron_shells;
        for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
            int shell = -1;
            for (int s = 0; s < 4; ++s) {
                if (shell_fill[s] < SHELL_CAP_O[s]) { shell = s; break; }
            }
            if (shell < 0) {
                electron_shells.push_back({120.0f, 0, cfg.orbit_boost[0]});
                continue;
            }
            shell_fill[shell]++;
            int inner = 0;
            for (int s = 0; s < shell; ++s) inner += shell_fill[s];
            float Z_eff = std::max(1.0f, static_cast<float>(template_Z - inner));
            float n_shell = static_cast<float>(shell + 1);
            float R_target = n_shell * n_shell * R_BOHR / Z_eff;
            R_target = std::max(R_target, 8.0f);
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_O;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
            electron_shells.push_back({L_ground, shell, cfg.orbit_boost[shell]});
        }

        // Spawn all particles
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) {
                if (readback_energies_[i] < 0.01f) { slot = i; break; }
            }
            if (slot == UINT32_MAX) break;

            readback_positions_[slot] = wrap_pos(
                world_pos + glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy));

            uint32_t t = tmpl.atoms[a].type;
            bool is_electron = (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS);
            float part_speed = 0.0f;

            if (is_electron && nucleon_count > 0) {
                glm::vec2 to_electron = glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy)
                                      - nucleus_center;
                float r = glm::length(to_electron);
                if (r > 1.0f) {
                    // Find this electron's shell info
                    float L_g = 120.0f;
                    for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
                        if (std::abs(electron_entries[ei].dx - tmpl.atoms[a].dx) < 0.1f &&
                            std::abs(electron_entries[ei].dy - tmpl.atoms[a].dy) < 0.1f) {
                            L_g = electron_shells[ei].L_ground;
                            break;
                        }
                    }
                    float v_orbital = L_g / std::max(r, 3.0f);
                    glm::vec2 radial = to_electron / r;
                    glm::vec2 tangent(-radial.y, radial.x);
                    readback_velocities_[slot] = tangent * v_orbital;
                    part_speed = v_orbital;
                } else {
                    readback_velocities_[slot] = glm::vec2(0.0f);
                }
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            // Energy from rest mass + kinetic energy
            float m0_t = (t < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[t] : 0.0f;
            if (m0_t < 0.001f) {
                readback_energies_[slot] = mev_to_ebuf(std::max(part_speed / C_SIM, 0.1f));
            } else {
                float beta_t = std::min(part_speed / C_SIM, 0.9999f);
                float gamma_t = 1.0f / std::sqrt(1.0f - beta_t * beta_t);
                readback_energies_[slot] = mev_to_ebuf(gamma_t * m0_t);
            }

            write_spawn_genome(particles, slot, t, rng, frame_counter_);

            // Write L_ground + boost + shell to genome for electrons
            if (is_electron && nucleon_count > 0) {
                for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
                    if (std::abs(electron_entries[ei].dx - tmpl.atoms[a].dx) < 0.1f &&
                        std::abs(electron_entries[ei].dy - tmpl.atoms[a].dy) < 0.1f) {
                        particles.genomes[slot * GENOME_SIZE + 2] = electron_shells[ei].L_ground;
                        particles.genomes[slot * GENOME_SIZE + 3] = electron_shells[ei].boost;
                        if (slot < particles.orbital_shell.size()) {
                            particles.orbital_shell[slot] = static_cast<int8_t>(electron_shells[ei].shell);
                            particles.excitation_timer[slot] = 0;
                        }
                        break;
                    }
                }
            }
        }
    } else {
        // Single particle spawn
        bool is_massless = (particles.behavior_flags[type] & (BEHAVIOR_PHOTON | BEHAVIOR_GLUON)) != 0;
        float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
        float KE_MeV = iface.spawn_energy_mev;
        // Convert kinetic energy → sim velocity
        float spawn_speed = (is_massless) ? C_SIM : ke_to_speed(KE_MeV, type);

        for (int c = 0; c < count; ++c) {
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) {
                if (readback_energies_[i] < 0.01f) { slot = i; break; }
            }
            if (slot == UINT32_MAX) break;

            glm::vec2 offset(0.0f);
            if (count > 1)
                offset = glm::vec2(gauss(rng) * scatter, gauss(rng) * scatter);

            readback_positions_[slot] = wrap_pos(world_pos + offset);

            // Velocity: random direction at KE-derived speed (massless always at c)
            if (spawn_speed > 0.01f) {
                float angle = angle_dist_(rng);
                glm::vec2 dir(std::cos(angle), std::sin(angle));
                readback_velocities_[slot] = dir * spawn_speed;
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            // Energy buffer: rest mass + kinetic energy
            // Massless particles (photon, graviton, gluon) always travel at c — need non-zero energy
            if (m0 < 0.001f) {
                readback_energies_[slot] = mev_to_ebuf(std::max(KE_MeV, 1.0f));  // minimum 1 MeV for massless
            } else {
                readback_energies_[slot] = mev_to_ebuf(m0 + KE_MeV);
            }

            write_spawn_genome(particles, slot, type, rng, frame_counter_);
        }
    }
    }  // end outer else (spawn_atom_Z not active)

    vkDeviceWaitIdle(vk.device);
    compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
    compute.upload_dynamic_data(vk, particles);
}

// ── CPU-side annihilation product conversion ─────────────────────────────────

void PhysicsSimulation::check_annihilation() {
    if (!cfg.annihilation_enabled) return;
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float CONTACT_RADIUS = cfg.annihilation_radius;
    const float CONTACT_RADIUS_SQ = CONTACT_RADIUS * CONTACT_RADIUS;

    bool any_annihilated = false;
    std::vector<bool> consumed(n, false);

    // Annihilation pairs: {antimatter_type, matter_type}
    struct AnnihilPair { uint32_t anti; uint32_t matter; };
    static const AnnihilPair PAIRS[] = {
        { POSITRON_TYPE_PHYS,   ELECTRON_TYPE_PHYS },
        { ANTIPROTON_TYPE_PHYS, PROTON_TYPE },
        { ANTIMUON_TYPE_PHYS,   MUON_TYPE_PHYS },
        { ANTITAU_TYPE_PHYS,    TAU_TYPE_PHYS },
        // Quark-antiquark annihilation
        { ANTI_UP_TYPE,      UP_QUARK_TYPE },
        { ANTI_DOWN_TYPE,    DOWN_QUARK_TYPE },
        { ANTI_STRANGE_TYPE, STRANGE_QUARK_TYPE },
        { ANTI_CHARM_TYPE,   CHARM_QUARK_TYPE },
        { ANTI_TOP_TYPE,     TOP_QUARK_TYPE },
        { ANTI_BOTTOM_TYPE,  BOTTOM_QUARK_TYPE },
    };
    static constexpr uint32_t PAIR_COUNT = sizeof(PAIRS) / sizeof(PAIRS[0]);

    std::mt19937 rng(frame_counter_ * 1664525u + 1013904223u);

    for (uint32_t i = 0; i < n; ++i) {
        if (consumed[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;

        uint32_t type_i = particles.types[i];

        // Check if this is an antimatter particle
        uint32_t target_type = UINT32_MAX;
        for (uint32_t p = 0; p < PAIR_COUNT; ++p) {
            if (type_i == PAIRS[p].anti) { target_type = PAIRS[p].matter; break; }
        }
        if (target_type == UINT32_MAX) continue;

        // Find nearest counterpart (spatial grid accelerated)
        float best_dist_sq = CONTACT_RADIUS_SQ;
        uint32_t best_j = UINT32_MAX;
        auto annihil_search = [&](uint32_t j) {
            if (j == i || consumed[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            if (particles.types[j] != target_type) return;

            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_j = j;
            }
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, CONTACT_RADIUS, annihil_search);
        else
            for (uint32_t j = 0; j < n; ++j) annihil_search(j);

        if (best_j == UINT32_MAX) continue;

        consumed[i] = true;
        consumed[best_j] = true;
        any_annihilated = true;
        achievements.total_annihilations++;
        try_unlock(ACH_FIRST_ANNIHILATION);
        {
            const char* name_i = (type_i < PHYS_PARTICLE_TYPES) ? SM_LABELS[type_i] : "?";
            const char* name_j = (target_type < PHYS_PARTICLE_TYPES) ? SM_LABELS[target_type] : "?";
            char amsg[128], adetail[384];
            snprintf(amsg, sizeof(amsg), "%s + %s \xe2\x86\x92 \xce\xb3\xce\xb3", name_i, name_j);
            float spd_i = glm::length(readback_velocities_[i]);
            float spd_j = glm::length(readback_velocities_[best_j]);
            snprintf(adetail, sizeof(adetail),
                     "%s #%u  E=%.3f MeV  v=%.4fc\n"
                     "%s #%u  E=%.3f MeV  v=%.4fc\n"
                     "Total rest mass: %.3f MeV",
                     name_i, i, readback_energies_[i], spd_i / C_SIM,
                     name_j, best_j, readback_energies_[best_j], spd_j / C_SIM,
                     (type_i < PHYS_PARTICLE_TYPES ? PHYS_REST_MASS_MEV[type_i] : 0.0f) +
                     (target_type < PHYS_PARTICLE_TYPES ? PHYS_REST_MASS_MEV[target_type] : 0.0f));
            iface.push_notification(amsg, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            iface.push_decay_event(amsg, PhysicsInterface::DEVT_ANNIHILATION,
                ImVec4(1.0f, 0.3f, 0.3f, 1.0f), std::string(adetail));
        }

        glm::vec2 mid = (readback_positions_[i] + readback_positions_[best_j]) * 0.5f;

        // COM frame kinematics for back-to-back photon emission
        float m_i = (type_i < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type_i] : 0.0f;
        float m_j = (target_type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[target_type] : 0.0f;
        float gamma_i = compute_gamma(readback_velocities_[i]);
        float gamma_j = compute_gamma(readback_velocities_[best_j]);
        glm::vec2 p_i = readback_velocities_[i] * (gamma_i * m_i / (C_SIM * C_SIM));
        glm::vec2 p_j = readback_velocities_[best_j] * (gamma_j * m_j / (C_SIM * C_SIM));
        glm::vec2 p_total = p_i + p_j;
        float E_total = gamma_i * m_i + gamma_j * m_j;

        // COM velocity (β_cm = p_total c / E_total)
        glm::vec2 v_cm = (E_total > 0.001f) ? p_total * (C_SIM * C_SIM / E_total) : glm::vec2(0.0f);
        float beta_cm = std::min(glm::length(v_cm) / C_SIM, 0.9999f);
        float gamma_cm = 1.0f / std::sqrt(1.0f - beta_cm * beta_cm);

        // Random isotropic direction in COM frame
        std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
        float angle = angle_dist(rng);
        glm::vec2 dir_com(std::cos(angle), std::sin(angle));

        // Boost photon directions from COM to lab frame
        glm::vec2 dir1, dir2;
        if (beta_cm > 0.001f) {
            glm::vec2 beta_hat = glm::normalize(v_cm);
            float cos_com = glm::dot(dir_com, beta_hat);
            // Aberration formula: tan(θ_lab) = sin(θ_com) / (γ(cos(θ_com) + β))
            float denom1 = gamma_cm * (cos_com + beta_cm);
            dir1 = glm::normalize(beta_hat * denom1 + (dir_com - beta_hat * cos_com));
            float denom2 = gamma_cm * (-cos_com + beta_cm);
            dir2 = glm::normalize(beta_hat * denom2 - (dir_com - beta_hat * cos_com));
        } else {
            dir1 = dir_com;
            dir2 = -dir_com;
        }

        // Doppler-shifted photon energies
        float E_com_half = E_total / (2.0f * gamma_cm);
        float cos_theta = (beta_cm > 0.001f) ? glm::dot(dir_com, glm::normalize(v_cm)) : 0.0f;
        float E_photon1 = E_com_half * gamma_cm * (1.0f + beta_cm * cos_theta);
        float E_photon2 = E_total - E_photon1;

        // All annihilation → 2 photons (+ optional neutrino for baryon pairs)
        particles.types[i] = PHOTON_TYPE_PHYS;
        particles.genomes[i * GENOME_SIZE + 0] = 0.0f;
        particles.genomes[i * GENOME_SIZE + 1] = 1.0f;  // photon spin
        particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
        particles.genomes[i * GENOME_SIZE + 3] = 0.0f;
        readback_positions_[i] = mid;
        readback_velocities_[i] = dir1 * C_SIM;
        readback_energies_[i] = mev_to_ebuf(E_photon1);

        particles.types[best_j] = PHOTON_TYPE_PHYS;
        particles.genomes[best_j * GENOME_SIZE + 0] = 0.0f;
        particles.genomes[best_j * GENOME_SIZE + 1] = 1.0f;
        particles.genomes[best_j * GENOME_SIZE + 2] = 0.0f;
        particles.genomes[best_j * GENOME_SIZE + 3] = 0.0f;
        readback_positions_[best_j] = mid;
        readback_velocities_[best_j] = dir2 * C_SIM;
        readback_energies_[best_j] = mev_to_ebuf(E_photon2);

        // Baryon annihilation produces extra neutrino (carries ~5% of energy)
        if (type_i == ANTIPROTON_TYPE_PHYS) {
            uint32_t nu_slot = UINT32_MAX;
            for (uint32_t k = 0; k < n; ++k) {
                if (!consumed[k] && readback_energies_[k] < 0.01f) {
                    nu_slot = k; consumed[k] = true; break;
                }
            }
            if (nu_slot != UINT32_MAX) {
                glm::vec2 dir3(-dir1.y, dir1.x);
                write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                readback_positions_[nu_slot] = mid;
                readback_velocities_[nu_slot] = dir3 * C_SIM * 0.9999f;
                readback_energies_[nu_slot] = mev_to_ebuf(E_total * 0.05f);
            }
        }
    }

    if (any_annihilated) {
        cpu_particles_dirty_ = true;
    }
}

// ── CPU-side nuclear fusion ──────────────────────────────────────────────────
// Detects nucleon pairs close enough and energetic enough to fuse.
// Implements: p+p chain, deuteron formation (p+n), and He-4 formation.

void PhysicsSimulation::check_fusion() {
    if (readback_positions_.empty()) return;
    if (!cfg.fusion_enabled) return;

    const uint32_t n = cfg.particle_count;
    const float FUSION_RADIUS = cfg.fusion_radius;
    const float FUSION_RADIUS_SQ = FUSION_RADIUS * FUSION_RADIUS;
    const int MAX_FUSIONS_PER_FRAME = cfg.max_fusions_per_frame;

    // Coulomb barrier energy in MeV (user-configurable, realistic ~550 keV)
    const float E_barrier_MeV = cfg.fusion_threshold_keV * 0.001f;

    int fusion_count = 0;
    bool any_fused = false;
    std::vector<bool> used(n, false);
    std::mt19937 rng(frame_counter_ * 3141592653u);
    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

    auto rand_dir = [&]() -> glm::vec2 {
        float a = angle_dist_(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    // Check if a particle's nucleus is covalently bonded (part of a molecule)
    auto is_bonded = [&](uint32_t idx) -> bool {
        if (bond_data_.empty()) return false;
        int32_t par = particles.orbital_parent[idx];
        uint32_t rep = (par >= 0) ? static_cast<uint32_t>(par) : idx;
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (base + s >= bond_data_.size()) break;
            if (bond_data_[base + s] != 0xFFFFFFFFu) return true;
        }
        return false;
    };

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k) {
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        }
        for (uint32_t k = 0; k < start; ++k) {
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        }
        return UINT32_MAX;
    };

    // ── Helper: compute center-of-mass kinetic energy (MeV) ──────────────
    // Non-relativistic: KE_cm = ½ μ v_rel²  where μ = m₁m₂/(m₁+m₂)
    // v_rel in sim units, convert to fraction of c for MeV calculation
    auto cm_kinetic_energy = [](float m1_MeV, float m2_MeV, float v_rel_sq) -> float {
        float mu = (m1_MeV * m2_MeV) / (m1_MeV + m2_MeV);  // reduced mass (MeV/c²)
        float beta_rel_sq = v_rel_sq / (C_SIM * C_SIM);
        return 0.5f * mu * beta_rel_sq;  // KE_cm in MeV
    };

    // ── Helper: Gamow tunneling probability ──────────────────────────────
    // P(E) = exp(-sqrt(E_barrier / E_cm)) for charged pairs
    // Returns 1.0 for uncharged pairs (no Coulomb barrier)
    auto gamow_probability = [&](float KE_cm_MeV, bool has_coulomb_barrier) -> float {
        if (!has_coulomb_barrier) return 1.0f;  // no barrier for p+n
        if (E_barrier_MeV < 0.001f) return 1.0f;  // barrier disabled
        if (KE_cm_MeV < 1e-6f) return 0.0f;  // essentially zero energy
        float gamow_exp = std::sqrt(E_barrier_MeV / KE_cm_MeV);
        return std::exp(-gamow_exp);
    };

    // ── Pass 1: Proton-proton chain (p + p → p + n + e⁺ + νe) ───────────
    // Requires sufficient CM kinetic energy to overcome Coulomb barrier
    // with quantum tunneling probability (Gamow factor)
    const float FUSION_MIN_E = cfg.fusion_min_energy;
    float m_proton = PHYS_REST_MASS_MEV[PROTON_TYPE];
    for (uint32_t i = 0; i < n && fusion_count < MAX_FUSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < FUSION_MIN_E) continue;
        if (particles.types[i] != PROTON_TYPE) continue;
        if (is_bonded(i)) continue;  // atoms in molecules don't fuse

        uint32_t best_pp = UINT32_MAX;
        float best_KE_cm = 0.0f;
        auto pp_search = [&](uint32_t j) {
            if (best_pp != UINT32_MAX) return;
            if (j <= i || used[j]) return;
            if (readback_energies_[j] < FUSION_MIN_E) return;
            if (particles.types[j] != PROTON_TYPE) return;
            if (is_bonded(j)) return;  // atoms in molecules don't fuse
            // Skip nucleons already bound in the same nucleus
            if (particles.orbital_parent[i] >= 0 &&
                particles.orbital_parent[i] == particles.orbital_parent[j]) return;
            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FUSION_RADIUS_SQ) return;

            // Center-of-mass kinetic energy check
            glm::vec2 rel_vel = readback_velocities_[j] - readback_velocities_[i];
            float v_rel_sq = glm::dot(rel_vel, rel_vel);
            float KE_cm = cm_kinetic_energy(m_proton, m_proton, v_rel_sq);

            // Gamow tunneling probability
            float p_tunnel = gamow_probability(KE_cm, true);
            if (p_tunnel < 1e-6f) return;  // negligible probability
            if (prob_dist(rng) > p_tunnel) return;  // probabilistic check

            best_pp = j;
            best_KE_cm = KE_cm;
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, FUSION_RADIUS, pp_search);
        else
            for (uint32_t j = 0; j < n; ++j) pp_search(j);
        if (best_pp == UINT32_MAX) continue;
        {
            uint32_t j = best_pp;
            used[i] = true;
            used[j] = true;
            any_fused = true;
            fusion_count++;

            write_spawn_genome(particles, j, NEUTRON_TYPE, rng, frame_counter_);

            float Q_binding = cfg.fusion_binding_mev;
            readback_energies_[i] = std::min(readback_energies_[i] + mev_to_ebuf(Q_binding * 0.5f), 1.0f);
            readback_energies_[j] = std::min(readback_energies_[j] + mev_to_ebuf(Q_binding * 0.5f), 1.0f);

            float Q_leptonic = cfg.fusion_leptonic_q_mev;
            uint32_t e_slot = find_dormant(j + 1);
            if (e_slot != UINT32_MAX) {
                used[e_slot] = true;
                glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
                glm::vec2 dir = rand_dir();
                write_spawn_genome(particles, e_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_positions_[e_slot] = mid;
                readback_velocities_[e_slot] = dir * ke_to_speed(Q_leptonic * 0.5f, POSITRON_TYPE_PHYS);
                readback_energies_[e_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[POSITRON_TYPE_PHYS] + Q_leptonic * 0.5f);
            }

            uint32_t nu_slot = find_dormant((e_slot != UINT32_MAX) ? e_slot + 1 : j + 1);
            if (nu_slot != UINT32_MAX) {
                used[nu_slot] = true;
                glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
                glm::vec2 dir = rand_dir();
                write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                readback_positions_[nu_slot] = mid;
                readback_velocities_[nu_slot] = dir * C_SIM * 0.9999f;
                readback_energies_[nu_slot] = mev_to_ebuf(Q_leptonic * 0.5f);
            }

            char msg[64];
            snprintf(msg, sizeof(msg), "Fusion: p+p (%.1f keV)", best_KE_cm * 1000.0f);
            iface.push_notification(msg, ImVec4(0.4f, 0.9f, 1.0f, 1.0f));
            {
                char fd[256];
                snprintf(fd, sizeof(fd),
                         "Proton #%u + Proton #%u\n"
                         "CM kinetic energy: %.1f keV\n"
                         "Products: deuteron + e+ (#%u) + ve (#%u)\n"
                         "Q (binding): 2.22 MeV  Q (leptonic): 0.42 MeV",
                         i, j,
                         best_KE_cm * 1000.0f,
                         e_slot != UINT32_MAX ? e_slot : 0,
                         nu_slot != UINT32_MAX ? nu_slot : 0);
                iface.push_decay_event("p + p \xe2\x86\x92 d + e\xe2\x81\xba + \xce\xbd",
                    PhysicsInterface::DEVT_FUSION, ImVec4(0.4f, 0.9f, 1.0f, 1.0f), std::string(fd));
            }
            achievements.total_fusions++;
            try_unlock(ACH_FIRST_FUSION);
        }
    }

    // ── Pass 2: Deuteron formation (p + n → bound pair) ──────────────────
    // No Coulomb barrier (neutron is uncharged), but requires proximity.
    // In reality, neutron capture cross-section is huge for slow neutrons.
    float m_neutron = PHYS_REST_MASS_MEV[NEUTRON_TYPE];
    for (uint32_t i = 0; i < n && fusion_count < MAX_FUSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < FUSION_MIN_E) continue;
        if (particles.types[i] != PROTON_TYPE) continue;
        if (is_bonded(i)) continue;  // atoms in molecules don't fuse

        uint32_t best_pn = UINT32_MAX;
        glm::vec2 best_pn_delta{};
        auto pn_search = [&](uint32_t j) {
            if (best_pn != UINT32_MAX) return;
            if (j == i || used[j]) return;
            if (readback_energies_[j] < FUSION_MIN_E) return;
            if (particles.types[j] != NEUTRON_TYPE) return;
            if (is_bonded(j)) return;  // atoms in molecules don't fuse
            // Skip nucleons already bound in the same nucleus
            if (particles.orbital_parent[i] >= 0 &&
                particles.orbital_parent[i] == particles.orbital_parent[j]) return;
            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FUSION_RADIUS_SQ) return;

            // No Coulomb barrier for p+n — just need proximity
            // But require minimum relative velocity to prevent every
            // nearby neutron from instantly binding
            glm::vec2 rel_vel = readback_velocities_[j] - readback_velocities_[i];
            float v_rel_sq = glm::dot(rel_vel, rel_vel);
            float KE_cm = cm_kinetic_energy(m_proton, m_neutron, v_rel_sq);
            if (KE_cm < cfg.fusion_pn_min_ke_keV * 0.001f) return;  // keV → MeV threshold

            best_pn = j;
            best_pn_delta = delta;
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, FUSION_RADIUS, pn_search);
        else
            for (uint32_t j = 0; j < n; ++j) pn_search(j);
        if (best_pn == UINT32_MAX) continue;
        {
            uint32_t j = best_pn;
            glm::vec2 delta = best_pn_delta;

            used[i] = true;
            used[j] = true;
            any_fused = true;
            fusion_count++;

            glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
            glm::vec2 avg_vel = (readback_velocities_[i] + readback_velocities_[j]) * 0.5f;

            glm::vec2 sep = glm::normalize(delta + glm::vec2(0.001f, 0.0f)) * cfg.fusion_product_separation;
            readback_positions_[i] = mid - sep * 0.5f;
            readback_positions_[j] = mid + sep * 0.5f;
            readback_velocities_[i] = avg_vel;
            readback_velocities_[j] = avg_vel;

            float E_bind = cfg.fusion_binding_mev;
            readback_energies_[i] = std::min(readback_energies_[i] + mev_to_ebuf(E_bind * 0.5f), 1.0f);
            readback_energies_[j] = std::min(readback_energies_[j] + mev_to_ebuf(E_bind * 0.5f), 1.0f);
            iface.push_notification("Fusion: p + n \xe2\x86\x92 deuteron",
                                    ImVec4(0.4f, 0.9f, 1.0f, 1.0f));
            {
                glm::vec2 rv = readback_velocities_[j] - readback_velocities_[i];
                float ke_cm = cm_kinetic_energy(m_proton, m_neutron, glm::dot(rv, rv));
                char fd[256];
                snprintf(fd, sizeof(fd),
                         "Proton #%u + Neutron #%u\n"
                         "CM kinetic energy: %.2f keV\n"
                         "Binding energy: 2.22 MeV",
                         i, j, ke_cm * 1000.0f);
                iface.push_decay_event("p + n \xe2\x86\x92 deuteron",
                    PhysicsInterface::DEVT_FUSION, ImVec4(0.4f, 0.9f, 1.0f, 1.0f), std::string(fd));
            }
            achievements.total_fusions++;
            try_unlock(ACH_FIRST_FUSION);
            break;
        }
    }

    if (any_fused) {
        for (uint32_t i = 0; i < n; ++i) {
            readback_energies_[i] = std::clamp(readback_energies_[i], 0.0f, 1.0f);
        }
        cpu_particles_dirty_ = true;
    }
}

// ── CPU-side nuclear fission ─────────────────────────────────────────────────
// Fast neutrons hitting heavy nuclei (6+ nucleons) trigger splitting.

void PhysicsSimulation::check_fission() {
    if (!cfg.fission_enabled) return;
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float CLUSTER_RADIUS = cfg.fission_cluster_radius;
    const float CLUSTER_RADIUS_SQ = CLUSTER_RADIUS * CLUSTER_RADIUS;
    const float NEUTRON_ENERGY_THRESHOLD = cfg.fission_neutron_threshold;
    const int MIN_CLUSTER_SIZE = cfg.min_fission_cluster;
    const int MAX_FISSIONS_PER_FRAME = cfg.max_fissions_per_frame;

    int fission_count = 0;
    bool any_fissioned = false;
    std::vector<bool> used(n, false);
    std::mt19937 rng(frame_counter_ * 2718281828u);

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k) {
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        }
        for (uint32_t k = 0; k < start; ++k) {
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        }
        return UINT32_MAX;
    };

    auto rand_dir = [&]() -> glm::vec2 {
        float a = angle_dist_(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    for (uint32_t i = 0; i < n && fission_count < MAX_FISSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < NEUTRON_ENERGY_THRESHOLD) continue;
        if (particles.types[i] != NEUTRON_TYPE) continue;

        // Compute neutron relativistic kinetic energy (MeV)
        // Must exceed fission barrier: ~6 MeV for heavy nuclei (U-235),
        // ~1 MeV for fast fission threshold. We use 1 MeV as minimum.
        float neutron_speed = glm::length(readback_velocities_[i]);
        float beta_n = std::min(neutron_speed / C_SIM, 0.9999f);
        float gamma_n = 1.0f / std::sqrt(1.0f - beta_n * beta_n);
        float neutron_KE_MeV = (gamma_n - 1.0f) * PHYS_REST_MASS_MEV[NEUTRON_TYPE];
        if (neutron_KE_MeV < cfg.fission_barrier_mev) continue;  // below fission barrier

        // Count nucleons near this fast neutron
        std::vector<uint32_t> cluster;
        cluster.reserve(64);
        cluster.push_back(i);
        auto fission_search = [&](uint32_t j) {
            if (j == i || used[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            uint32_t t = particles.types[j];
            if (t != PROTON_TYPE && t != NEUTRON_TYPE) return;
            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < CLUSTER_RADIUS_SQ) {
                cluster.push_back(j);
            }
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, CLUSTER_RADIUS, fission_search);
        else
            for (uint32_t j = 0; j < n; ++j) fission_search(j);

        if (static_cast<int>(cluster.size()) < MIN_CLUSTER_SIZE) continue;

        // Fission requires protons — Coulomb repulsion is what destabilizes heavy nuclei.
        // Pure neutron clusters (neutron star matter) are stable against fission.
        int proton_count = 0;
        for (uint32_t idx : cluster)
            if (particles.types[idx] == PROTON_TYPE) proton_count++;
        if (proton_count < cfg.fission_min_protons) continue;  // need protons for Coulomb instability

        // Bohr-Wheeler fissility check: Z²/A must exceed threshold (~35 for real nuclei).
        // This prevents light elements (C, O, Fe, etc.) from fissioning — only heavy
        // nuclei like uranium (Z²/A ≈ 36) are unstable against fission.
        float A = static_cast<float>(cluster.size());
        float fissility = static_cast<float>(proton_count * proton_count) / A;
        if (fissility < cfg.fission_fissility_threshold) continue;

        // Fission! Split cluster in half with separation impulse
        any_fissioned = true;
        fission_count++;
        for (uint32_t idx : cluster) used[idx] = true;

        glm::vec2 dir = rand_dir();
        uint32_t half = static_cast<uint32_t>(cluster.size()) / 2;

        // Fission releases ~1 MeV/nucleon as fragment KE (~200 MeV total for heavy nuclei)
        // Scale kick by mass-energy: ~200 MeV split among fragments
        float E_fission_MeV = static_cast<float>(cluster.size()) * cfg.fission_energy_per_nucleon;
        float kick_speed = ke_to_speed(E_fission_MeV / static_cast<float>(cluster.size()), PROTON_TYPE);

        for (uint32_t c = 0; c < static_cast<uint32_t>(cluster.size()); ++c) {
            uint32_t idx = cluster[c];
            if (c < half) {
                readback_velocities_[idx] += dir * kick_speed;
            } else {
                readback_velocities_[idx] -= dir * kick_speed;
            }
            readback_energies_[idx] = std::min(readback_energies_[idx] + mev_to_ebuf(cfg.fission_fragment_energy_mev), 1.0f);
        }

        // Spawn free neutrons (chain reaction fuel)
        std::uniform_int_distribution<int> neutron_dist(cfg.fission_free_neutrons_min, cfg.fission_free_neutrons_max);
        int free_neutrons = neutron_dist(rng);
        glm::vec2 center = readback_positions_[cluster[0]];
        float neutron_KE = cfg.fission_neutron_ke_mev;
        for (int f = 0; f < free_neutrons; ++f) {
            uint32_t slot = find_dormant(0);
            if (slot == UINT32_MAX) break;
            used[slot] = true;
            write_spawn_genome(particles, slot, NEUTRON_TYPE, rng, frame_counter_);
            readback_positions_[slot] = (center + rand_dir() * cfg.fission_spawn_radius);
            readback_velocities_[slot] = rand_dir() * ke_to_speed(neutron_KE, NEUTRON_TYPE);
            readback_energies_[slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE] + neutron_KE);
        }

        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Fission: %d-nucleon cluster split + %dn",
                     static_cast<int>(cluster.size()), free_neutrons);
            iface.push_notification(msg, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            char detail[512];
            snprintf(detail, sizeof(detail),
                "Neutron #%u KE: %.2f MeV\nCluster size: %d nucleons (p=%d)\nFragments: 2 x %d nucleons\nFree neutrons ejected: %d (%.2f MeV each)\nE_fission total: %.2f MeV",
                i, neutron_KE_MeV,
                static_cast<int>(cluster.size()), proton_count,
                static_cast<int>(half), free_neutrons,
                neutron_KE, E_fission_MeV);
            iface.push_decay_event(msg, PhysicsInterface::DEVT_FISSION, ImVec4(1.0f, 0.6f, 0.2f, 1.0f), std::string(detail));
            achievements.total_fissions++;
            achievements.fission_recent_count++;
            try_unlock(ACH_FIRST_FISSION);
        }
    }

    if (any_fissioned) {
        cpu_particles_dirty_ = true;
    }
}

// ── CPU-side orbital assignment ──────────────────────────────────────────────
// Clusters nucleons into nuclei, assigns electrons to orbital shells,
// and writes the ground-state angular momentum (L_ground) to genome[2].
// The shader reads this to apply the correct centrifugal barrier per electron.

void PhysicsSimulation::update_orbitals() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float NUCLEAR_CLUSTER_RADIUS = 10.0f;
    const float NUCLEAR_CLUSTER_RADIUS_SQ = NUCLEAR_CLUSTER_RADIUS * NUCLEAR_CLUSTER_RADIUS;
    // Tighter clustering radius for nucleons from different atoms —
    // electron clouds keep distinct atoms apart; only explicit fusion
    // (high energy collision) should merge nucleons from different atoms.
    const float CROSS_ATOM_CLUSTER_RADIUS = 3.0f;
    const float CROSS_ATOM_CLUSTER_RADIUS_SQ = CROSS_ATOM_CLUSTER_RADIUS * CROSS_ATOM_CLUSTER_RADIUS;
    const float BINDING_RADIUS = cfg.orbital_binding_radius;
    static const int MAX_ELECTRONS = 2 + 8 + 18 + 32;  // 60

    // Save previous orbital_parent before clearing (used to detect cross-atom clustering)
    std::vector<int32_t> prev_orbital_parent(particles.orbital_parent.begin(),
                                              particles.orbital_parent.end());
    prev_orbital_parent.resize(n, -1);

    // Clear orbital parent mapping for all particles
    particles.orbital_parent.resize(n, -1);
    std::fill(particles.orbital_parent.begin(), particles.orbital_parent.end(), -1);

    // Ensure orbital_shell and excitation_timer are sized (preserve previous values)
    particles.orbital_shell.resize(n, -1);
    particles.excitation_timer.resize(n, 0);

    // Save previous shell assignments before clearing
    std::vector<int8_t> prev_orbital_shell(particles.orbital_shell.begin(),
                                            particles.orbital_shell.end());

    // Helper: get nucleus representative for a nucleon from previous frame
    auto prev_nuc_rep = [&](uint32_t idx) -> int32_t {
        if (idx >= prev_orbital_parent.size()) return -1;
        int32_t p = prev_orbital_parent[idx];
        return (p >= 0) ? p : static_cast<int32_t>(idx);  // self if not in a nucleus
    };

    // ── Step 1: Cluster nucleons into nuclei ────────────────────────────
    struct Nucleus {
        glm::vec2 center;
        int Z;          // proton count
        int total;      // total nucleon count
        uint32_t rep;   // representative proton index (for orbital_parent tracking)
    };
    std::vector<Nucleus> nuclei;
    std::vector<bool> clustered(n, false);
    detected_nuclei_.clear();

    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];
        if (t != PROTON_TYPE && t != NEUTRON_TYPE) continue;

        // BFS: gather all nearby nucleons into one nucleus
        std::vector<uint32_t> members;
        members.reserve(64);
        members.push_back(i);
        clustered[i] = true;

        for (size_t front = 0; front < members.size(); ++front) {
            uint32_t mi = members[front];
            auto bfs_search = [&](uint32_t j) {
                if (clustered[j]) return;
                if (readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj != PROTON_TYPE && tj != NEUTRON_TYPE) return;
                glm::vec2 d = (readback_positions_[j] - readback_positions_[mi]);
                float d2 = glm::dot(d, d);

                // Use tighter radius only if BOTH nucleons were in established,
                // *different* atoms last frame. New/free particles (orbital_parent == -1)
                // use the normal radius so freshly spawned atoms can cluster properly.
                int32_t prev_mi = (mi < prev_orbital_parent.size()) ? prev_orbital_parent[mi] : -1;
                int32_t prev_j  = (j  < prev_orbital_parent.size()) ? prev_orbital_parent[j]  : -1;
                bool cross_atom = (prev_mi >= 0 && prev_j >= 0
                                   && prev_nuc_rep(mi) != prev_nuc_rep(j));
                float threshold = cross_atom ? CROSS_ATOM_CLUSTER_RADIUS_SQ
                                             : NUCLEAR_CLUSTER_RADIUS_SQ;
                if (d2 < threshold) {
                    members.push_back(j);
                    clustered[j] = true;
                }
            };
            if (iface.prefs.spatial_grid)
                grid_.query(readback_positions_[mi].x, readback_positions_[mi].y, NUCLEAR_CLUSTER_RADIUS, bfs_search);
            else
                for (uint32_t j = 0; j < n; ++j) bfs_search(j);
        }

        // Compute nucleus centroid and proton count
        Nucleus nuc{};
        nuc.rep = UINT32_MAX;
        for (uint32_t mi : members) {
            nuc.center += readback_positions_[mi];
            nuc.total++;
            if (particles.types[mi] == PROTON_TYPE) {
                nuc.Z++;
                if (nuc.rep == UINT32_MAX) nuc.rep = mi;  // first proton = representative
            }
        }
        nuc.center /= static_cast<float>(nuc.total);

        // Mark nucleon members as belonging to this nucleus
        if (nuc.Z > 0) {
            for (uint32_t mi : members)
                particles.orbital_parent[mi] = static_cast<int32_t>(nuc.rep);
            nuclei.push_back(nuc);

            // Store detailed nucleus info for nuclear decay
            NucleusInfo ni;
            ni.center = nuc.center;
            ni.Z = nuc.Z;
            ni.N = nuc.total - nuc.Z;
            ni.rep = nuc.rep;
            for (uint32_t mi : members) {
                if (particles.types[mi] == PROTON_TYPE) ni.proton_indices.push_back(mi);
                else ni.neutron_indices.push_back(mi);
            }
            detected_nuclei_.push_back(std::move(ni));
        }
    }

    // Also track free neutrons (Z=0 clusters or unclustered single neutrons) for decay
    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        if (particles.types[i] != NEUTRON_TYPE) continue;
        // Free neutron — add as Z=0, N=1 nucleus entry for decay tracking
        NucleusInfo fn;
        fn.center = readback_positions_[i];
        fn.Z = 0; fn.N = 1;
        fn.rep = i;
        fn.neutron_indices.push_back(i);
        detected_nuclei_.push_back(std::move(fn));
    }

    // ── Step 1b: Cluster antiprotons into antinuclei ────────────────────
    // Antinuclei are made of antiprotons (no antineutron type exists yet,
    // so Z = antiproton count, N = 0 for now).
    size_t anti_start_in_detected = detected_nuclei_.size();
    std::vector<Nucleus> antinuclei;
    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        if (particles.types[i] != ANTIPROTON_TYPE_PHYS) continue;

        // BFS: gather all nearby antiprotons
        std::vector<uint32_t> members;
        members.reserve(32);
        members.push_back(i);
        clustered[i] = true;

        for (size_t front = 0; front < members.size(); ++front) {
            uint32_t mi = members[front];
            auto anti_bfs_search = [&](uint32_t j) {
                if (clustered[j]) return;
                if (readback_energies_[j] < 0.01f) return;
                if (particles.types[j] != ANTIPROTON_TYPE_PHYS) return;
                glm::vec2 d = (readback_positions_[j] - readback_positions_[mi]);
                if (glm::dot(d, d) < NUCLEAR_CLUSTER_RADIUS_SQ) {
                    members.push_back(j);
                    clustered[j] = true;
                }
            };
            if (iface.prefs.spatial_grid)
                grid_.query(readback_positions_[mi].x, readback_positions_[mi].y, NUCLEAR_CLUSTER_RADIUS, anti_bfs_search);
            else
                for (uint32_t j = 0; j < n; ++j) anti_bfs_search(j);
        }

        Nucleus anuc{};
        anuc.rep = members[0];
        anuc.Z = static_cast<int>(members.size());
        anuc.total = anuc.Z;
        for (uint32_t mi : members) {
            anuc.center += readback_positions_[mi];
            particles.orbital_parent[mi] = static_cast<int32_t>(anuc.rep);
        }
        anuc.center /= static_cast<float>(anuc.total);
        antinuclei.push_back(anuc);

        NucleusInfo ani;
        ani.center = anuc.center;
        ani.Z = anuc.Z;
        ani.N = 0;
        ani.rep = anuc.rep;
        ani.is_anti = true;
        for (uint32_t mi : members)
            ani.proton_indices.push_back(mi);
        detected_nuclei_.push_back(std::move(ani));
    }

    // ── Step 2: Find electrons and assign to nearest nucleus ────────────
    //            Also find positrons and assign to nearest antinucleus.
    struct ElectronBind {
        uint32_t idx;
        int nuc_idx;
        float dist;
        bool assigned = false;
    };
    std::vector<ElectronBind> bindings;
    std::vector<ElectronBind> anti_bindings;  // positrons → antinuclei

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];

        // Positrons orbit antinuclei only (antimatter counterpart of electrons→protons)
        if (t == POSITRON_TYPE_PHYS) {
            if (!antinuclei.empty()) {
                float best_d_sq = BINDING_RADIUS * BINDING_RADIUS;
                int best_anuc = -1;
                for (int ai = 0; ai < static_cast<int>(antinuclei.size()); ++ai) {
                    glm::vec2 d = readback_positions_[i] - antinuclei[ai].center;
                    float d_sq = glm::dot(d, d);
                    if (d_sq < best_d_sq) {
                        best_d_sq = d_sq;
                        best_anuc = ai;
                    }
                }
                if (best_anuc >= 0) {
                    anti_bindings.push_back({i, best_anuc, std::sqrt(best_d_sq)});
                    particles.orbital_parent[i] = static_cast<int32_t>(antinuclei[best_anuc].rep);
                    continue;
                }
            }
            // Free positron (no antinucleus nearby) — clear orbital data
            particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
            particles.orbital_shell[i] = -1;
            particles.excitation_timer[i] = 0;
            continue;
        }

        // Only electrons orbit proton-nuclei (no e-e orbiting)
        if (t != ELECTRON_TYPE_PHYS) {
            // Clear orbital data for non-electrons that might have stale genome[2]
            if (t == MUON_TYPE_PHYS || t == ANTIMUON_TYPE_PHYS ||
                t == TAU_TYPE_PHYS || t == ANTITAU_TYPE_PHYS) {
                particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
            }
            continue;
        }

        float best_d_sq = BINDING_RADIUS * BINDING_RADIUS;
        int best_nuc = -1;
        for (int ni = 0; ni < static_cast<int>(nuclei.size()); ++ni) {
            glm::vec2 d = readback_positions_[i] - nuclei[ni].center;
            float d_sq = glm::dot(d, d);
            if (d_sq < best_d_sq) {
                best_d_sq = d_sq;
                best_nuc = ni;
            }
        }

        if (best_nuc >= 0) {
            bindings.push_back({i, best_nuc, std::sqrt(best_d_sq)});
            particles.orbital_parent[i] = static_cast<int32_t>(nuclei[best_nuc].rep);
        } else {
            particles.genomes[i * GENOME_SIZE + 2] = 0.0f;  // free electron
            particles.orbital_shell[i] = -1;
            particles.excitation_timer[i] = 0;
        }
    }

    // Sort by distance (closest electrons get inner shells first)
    std::sort(bindings.begin(), bindings.end(), [](const ElectronBind& a, const ElectronBind& b) {
        if (a.nuc_idx != b.nuc_idx) return a.nuc_idx < b.nuc_idx;
        return a.dist < b.dist;
    });

    // ── Step 3: Assign orbital shells and compute L_ground ──────────────
    // Two-pass approach: first retain electrons in their previous shells,
    // then assign newly-captured electrons to available slots.
    // shell_fill[nuc_idx * NUM_SHELLS_O + shell] = count of electrons in that shell
    std::vector<int> shell_fill(nuclei.size() * NUM_SHELLS_O, 0);

    // Helper: compute and write L_ground + boost for an electron in a given shell
    auto write_orbital_params = [&](uint32_t idx, int nuc_idx, int shell) {
        int Z = nuclei[nuc_idx].Z;
        int inner_electrons = 0;
        for (int s = 0; s < shell; ++s)
            inner_electrons += shell_fill[nuc_idx * NUM_SHELLS_O + s];
        float Z_eff = std::max(1.0f, static_cast<float>(Z - inner_electrons));
        float n_shell = static_cast<float>(shell + 1);
        float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
        particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
        particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
        particles.orbital_shell[idx] = static_cast<int8_t>(shell);
    };

    // Pass 1: retain electrons that were already bound to the same nucleus
    for (auto& b : bindings) {
        int Z = nuclei[b.nuc_idx].Z;
        int8_t prev_shell = (b.idx < prev_orbital_shell.size()) ? prev_orbital_shell[b.idx] : -1;
        int32_t prev_parent = (b.idx < prev_orbital_parent.size()) ? prev_orbital_parent[b.idx] : -1;
        bool was_bound_here = (prev_parent >= 0 &&
                               prev_parent == static_cast<int32_t>(nuclei[b.nuc_idx].rep) &&
                               prev_shell >= 0 && prev_shell < NUM_SHELLS_O);
        if (!was_bound_here) continue;

        // Check capacity: total assigned to this nucleus vs max allowed
        int total_assigned = 0;
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            total_assigned += shell_fill[b.nuc_idx * NUM_SHELLS_O + s];
        if (total_assigned >= std::min(Z, MAX_ELECTRONS)) continue;

        // Try to keep in same shell
        int offset = b.nuc_idx * NUM_SHELLS_O + prev_shell;
        if (shell_fill[offset] < SHELL_CAP_O[prev_shell]) {
            shell_fill[offset]++;
            write_orbital_params(b.idx, b.nuc_idx, prev_shell);
            // Preserve excitation_timer — don't reset it
            b.assigned = true;
        }
    }

    // Pass 2: assign newly-captured or displaced electrons to lowest available shell
    for (auto& b : bindings) {
        if (b.assigned) continue;
        int Z = nuclei[b.nuc_idx].Z;

        int total_assigned = 0;
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            total_assigned += shell_fill[b.nuc_idx * NUM_SHELLS_O + s];

        if (total_assigned >= std::min(Z, MAX_ELECTRONS)) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;  // excess, free
            particles.orbital_shell[b.idx] = -1;
            continue;
        }

        // Find first shell with room
        int shell = -1;
        for (int s = 0; s < NUM_SHELLS_O; ++s) {
            if (shell_fill[b.nuc_idx * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                shell = s;
                break;
            }
        }
        if (shell < 0) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
            particles.orbital_shell[b.idx] = -1;
            continue;
        }

        shell_fill[b.nuc_idx * NUM_SHELLS_O + shell]++;
        write_orbital_params(b.idx, b.nuc_idx, shell);
        particles.excitation_timer[b.idx] = 0;  // newly captured = ground state
    }

    // Copy shell_fill into NucleusInfo for matter nuclei
    for (size_t ni = 0; ni < nuclei.size() && ni < detected_nuclei_.size(); ++ni) {
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            detected_nuclei_[ni].shell_fill[s] = shell_fill[ni * NUM_SHELLS_O + s];
    }

    // ── Step 3b: Assign positron orbital shells around antinuclei ─────────
    if (!anti_bindings.empty()) {
        std::sort(anti_bindings.begin(), anti_bindings.end(),
                  [](const ElectronBind& a, const ElectronBind& b) {
                      if (a.nuc_idx != b.nuc_idx) return a.nuc_idx < b.nuc_idx;
                      return a.dist < b.dist;
                  });
        std::vector<int> anti_shell_fill(antinuclei.size() * NUM_SHELLS_O, 0);

        // Helper for antinuclei orbital params
        auto write_anti_orbital_params = [&](uint32_t idx, int anuc_idx, int shell) {
            int Z = antinuclei[anuc_idx].Z;
            int inner = 0;
            for (int s = 0; s < shell; ++s)
                inner += anti_shell_fill[anuc_idx * NUM_SHELLS_O + s];
            float Z_eff = std::max(1.0f, static_cast<float>(Z - inner));
            float n_shell = static_cast<float>(shell + 1);
            float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_O;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
            particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
            particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
            particles.orbital_shell[idx] = static_cast<int8_t>(shell);
        };

        // Pass 1: retain positrons already bound to same antinucleus
        for (auto& b : anti_bindings) {
            int Z = antinuclei[b.nuc_idx].Z;
            int8_t prev_shell = (b.idx < prev_orbital_shell.size()) ? prev_orbital_shell[b.idx] : -1;
            int32_t prev_parent = (b.idx < prev_orbital_parent.size()) ? prev_orbital_parent[b.idx] : -1;
            bool was_bound_here = (prev_parent >= 0 &&
                                   prev_parent == static_cast<int32_t>(antinuclei[b.nuc_idx].rep) &&
                                   prev_shell >= 0 && prev_shell < NUM_SHELLS_O);
            if (!was_bound_here) continue;

            int total_assigned = 0;
            for (int s = 0; s < NUM_SHELLS_O; ++s)
                total_assigned += anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s];
            if (total_assigned >= std::min(Z, MAX_ELECTRONS)) continue;

            int offset = b.nuc_idx * NUM_SHELLS_O + prev_shell;
            if (anti_shell_fill[offset] < SHELL_CAP_O[prev_shell]) {
                anti_shell_fill[offset]++;
                write_anti_orbital_params(b.idx, b.nuc_idx, prev_shell);
                b.assigned = true;
            }
        }

        // Pass 2: assign newly-captured positrons
        for (auto& b : anti_bindings) {
            if (b.assigned) continue;
            int Z = antinuclei[b.nuc_idx].Z;

            int total_assigned = 0;
            for (int s = 0; s < NUM_SHELLS_O; ++s)
                total_assigned += anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s];

            if (total_assigned >= std::min(Z, MAX_ELECTRONS)) {
                particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
                particles.orbital_shell[b.idx] = -1;
                continue;
            }

            int shell = -1;
            for (int s = 0; s < NUM_SHELLS_O; ++s) {
                if (anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                    shell = s;
                    break;
                }
            }
            if (shell < 0) {
                particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
                particles.orbital_shell[b.idx] = -1;
                continue;
            }

            anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + shell]++;
            write_anti_orbital_params(b.idx, b.nuc_idx, shell);
            particles.excitation_timer[b.idx] = 0;
        }

        // Copy anti_shell_fill into NucleusInfo for antinuclei
        for (size_t ai = 0; ai < antinuclei.size(); ++ai) {
            size_t di = anti_start_in_detected + ai;
            if (di < detected_nuclei_.size()) {
                for (int s = 0; s < NUM_SHELLS_O; ++s)
                    detected_nuclei_[di].shell_fill[s] = anti_shell_fill[ai * NUM_SHELLS_O + s];
            }
        }
    }
}

// ── Inter-nucleus Coulomb repulsion ──────────────────────────────────────────
// Prevents distinct UNBONDED atoms from drifting close enough for their
// nucleons to overlap and be merged by the BFS clustering in update_orbitals().
// Applies both position correction and velocity impulse proportional to Z₁·Z₂.
// Bonded atom pairs are skipped — they're held at bond length by the spring.

void PhysicsSimulation::repel_distinct_nuclei() {
    if (detected_nuclei_.size() < 2) return;

    const float REPEL_RADIUS    = 40.0f;   // start repelling when centers this close
    const float REPEL_RADIUS_SQ = REPEL_RADIUS * REPEL_RADIUS;
    const float DANGER_DIST     = 16.0f;   // below this, hard position push (> cluster radius 10)
    const float MIN_DIST        = 3.0f;    // softening floor
    const float VEL_K           = 80.0f;   // velocity impulse strength (was 1.5; needs to overcome drift)
    const float POS_K           = 1.2f;    // position correction strength (for close nuclei)
    const uint32_t n = cfg.particle_count;
    bool any_modified = false;

    // Helper: check if two nuclei are covalently bonded
    auto nuclei_bonded = [&](uint32_t rep_a, uint32_t rep_b) -> bool {
        if (bond_data_.empty()) return false;
        size_t required = static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE;
        if (bond_data_.size() < required) return false;
        uint32_t base = rep_a * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (base + s >= bond_data_.size()) break;
            if (bond_data_[base + s] == rep_b) return true;
        }
        return false;
    };

    for (size_t a = 0; a < detected_nuclei_.size(); ++a) {
        auto& na = detected_nuclei_[a];
        if (na.Z <= 0) continue;

        for (size_t b = a + 1; b < detected_nuclei_.size(); ++b) {
            auto& nb = detected_nuclei_[b];
            if (nb.Z <= 0) continue;

            // Skip bonded atom pairs — spring force handles them
            if (nuclei_bonded(na.rep, nb.rep) || nuclei_bonded(nb.rep, na.rep))
                continue;

            glm::vec2 delta = nb.center - na.center;
            float d2 = glm::dot(delta, delta);
            if (d2 > REPEL_RADIUS_SQ || d2 < 0.001f) continue;

            float dist = std::sqrt(d2);
            float eff_dist = std::max(dist, MIN_DIST);
            glm::vec2 dir = delta / dist;

            // Mass-weighted fractions (lighter nucleus moves more)
            float mass_a = static_cast<float>(na.Z + static_cast<int>(na.neutron_indices.size()));
            float mass_b = static_cast<float>(nb.Z + static_cast<int>(nb.neutron_indices.size()));
            float total_mass = mass_a + mass_b;
            float frac_a = mass_b / total_mass;
            float frac_b = mass_a / total_mass;

            // ── Velocity impulse: Z₁·Z₂ / r², smooth fade at edge ──────
            float t = dist / REPEL_RADIUS;  // 0..1
            float fade = (1.0f - t) * (1.0f - t);  // quadratic fade to zero at edge
            float zz = static_cast<float>(na.Z * nb.Z);
            float impulse = VEL_K * zz * fade / (eff_dist * eff_dist);
            impulse = std::min(impulse, 4.0f);

            glm::vec2 vel_a = -dir * impulse * frac_a;
            glm::vec2 vel_b =  dir * impulse * frac_b;

            // ── Position correction: hard push when dangerously close ───
            glm::vec2 pos_a(0.0f), pos_b(0.0f);
            if (dist < DANGER_DIST) {
                float penetration = DANGER_DIST - dist;
                float pos_push = POS_K * penetration * std::min(zz, 10.0f);
                pos_push = std::min(pos_push, 3.0f);  // cap per-frame correction
                pos_a = -dir * pos_push * frac_a;
                pos_b =  dir * pos_push * frac_b;
            }

            // Apply to all nucleons in each nucleus
            for (uint32_t idx : na.proton_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_a;
                    readback_positions_[idx] += pos_a;
                }
            }
            for (uint32_t idx : na.neutron_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_a;
                    readback_positions_[idx] += pos_a;
                }
            }
            for (uint32_t idx : nb.proton_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_b;
                    readback_positions_[idx] += pos_b;
                }
            }
            for (uint32_t idx : nb.neutron_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_b;
                    readback_positions_[idx] += pos_b;
                }
            }
            any_modified = true;
        }
    }

    if (any_modified) cpu_particles_dirty_ = true;
}

// ── CPU-side covalent bond formation / breaking ─────────────────────────────
// Detects atoms (from detected_nuclei_) with unfilled valence shells and forms
// bonds between nearby atoms.  Bond data is uploaded to GPU for spring forces.

void PhysicsSimulation::update_bonds() {
    if (readback_positions_.empty() || detected_nuclei_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float FORM_RADIUS    = cfg.bond_form_radius;
    const float BREAK_DIST     = cfg.bond_rest_length * cfg.bond_break_factor;
    const float BREAK_DIST_SQ  = BREAK_DIST * BREAK_DIST;
    const float ACTIVATION_E   = cfg.bond_activation_energy;
    const int   MAX_NEW_BONDS  = 10;

    // Ensure bond_data_ is sized correctly
    size_t required = static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE;
    if (bond_data_.size() != required) {
        bond_data_.assign(required, 0xFFFFFFFFu);
    }

    // Build rep → nucleus index map for quick lookup
    std::unordered_map<uint32_t, uint32_t> rep_to_nuc;
    rep_to_nuc.reserve(detected_nuclei_.size());
    for (uint32_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        rep_to_nuc[detected_nuclei_[ni].rep] = ni;
    }

    // Helper: count current bonds for a particle
    auto bond_count = [&](uint32_t rep) -> int {
        int cnt = 0;
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] != 0xFFFFFFFFu) ++cnt;
        }
        return cnt;
    };

    // Helper: check if a already bonded to b
    auto has_bond = [&](uint32_t rep_a, uint32_t rep_b) -> bool {
        uint32_t base = rep_a * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == rep_b) return true;
        }
        return false;
    };

    // Helper: add bond slot (returns true if added)
    auto add_bond_slot = [&](uint32_t rep, uint32_t partner) -> bool {
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == 0xFFFFFFFFu) {
                bond_data_[base + s] = partner;
                return true;
            }
        }
        return false;
    };

    // Helper: remove bond slot
    auto remove_bond_slot = [&](uint32_t rep, uint32_t partner) {
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == partner) {
                bond_data_[base + s] = 0xFFFFFFFFu;
                return;
            }
        }
    };

    bool any_changed = false;

    // Element symbols for event logging
    static const char* BSYM[] = {
        "n","H","He","Li","Be","B","C","N","O","F","Ne",
        "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
        "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
        "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
        "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
        "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd"
    };
    static const int BSYM_COUNT = static_cast<int>(sizeof(BSYM) / sizeof(BSYM[0]));
    auto bsym = [&](int Z) -> const char* {
        return (Z >= 0 && Z < BSYM_COUNT) ? BSYM[Z] : "?";
    };

    // ── Break pass: remove bonds where atoms are too far apart ───────────
    for (uint32_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        const auto& nuc = detected_nuclei_[ni];
        if (nuc.Z == 0) continue;
        uint32_t rep = nuc.rep;
        if (rep >= n) continue;

        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            uint32_t partner = bond_data_[base + s];
            if (partner == 0xFFFFFFFFu || partner >= n) continue;

            // Check if partner is still a valid nucleus rep
            if (rep_to_nuc.find(partner) == rep_to_nuc.end()) {
                // Partner nucleus no longer exists — break bond
                bond_data_[base + s] = 0xFFFFFFFFu;
                remove_bond_slot(partner, rep);
                any_changed = true;
                {
                    char desc[128], detail[256];
                    int A_a = nuc.Z + nuc.N;
                    snprintf(desc, sizeof(desc), "Bond broken: %s-%d — ? (partner lost)",
                             bsym(nuc.Z), A_a);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Partner rep #%u no longer exists\n"
                             "Cause: partner nucleus destroyed",
                             bsym(nuc.Z), A_a, rep, nuc.Z, nuc.N, partner);
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_BROKEN,
                        ImVec4(0.9f, 0.45f, 0.3f, 1.0f), std::string(detail));
                }
                continue;
            }

            glm::vec2 delta = (readback_positions_[partner] - readback_positions_[rep]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > BREAK_DIST_SQ) {
                bond_data_[base + s] = 0xFFFFFFFFu;
                remove_bond_slot(partner, rep);
                any_changed = true;
                {
                    auto& nuc_b = detected_nuclei_[rep_to_nuc[partner]];
                    char desc[128], detail[256];
                    int A_a = nuc.Z + nuc.N, A_b = nuc_b.Z + nuc_b.N;
                    float dist = std::sqrt(d2);
                    snprintf(desc, sizeof(desc), "Bond broken: %s-%d — %s-%d",
                             bsym(nuc.Z), A_a, bsym(nuc_b.Z), A_b);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Atom B: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Separation: %.1f px (threshold: %.1f px)",
                             bsym(nuc.Z), A_a, rep, nuc.Z, nuc.N,
                             bsym(nuc_b.Z), A_b, partner, nuc_b.Z, nuc_b.N,
                             dist, std::sqrt(BREAK_DIST_SQ));
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_BROKEN,
                        ImVec4(0.9f, 0.45f, 0.3f, 1.0f), std::string(detail));
                }
            }
        }
    }

    // ── Form pass: create new bonds between nearby atoms with unfilled valence ─
    int new_bonds = 0;
    for (uint32_t ni = 0; ni < detected_nuclei_.size() && new_bonds < MAX_NEW_BONDS; ++ni) {
        const auto& nuc = detected_nuclei_[ni];
        if (nuc.Z == 0) continue;

        int valence = valence_from_Z(nuc.Z);
        if (valence <= 0) continue;

        uint32_t rep_a = nuc.rep;
        if (rep_a >= n) continue;

        int current_bonds_a = bond_count(rep_a);
        if (current_bonds_a >= valence) continue;

        // Search for nearby atoms
        glm::vec2 center_a = nuc.center;

        auto bond_search = [&](uint32_t j) {
            if (new_bonds >= MAX_NEW_BONDS) return;
            if (current_bonds_a >= valence) return;

            // j must be a nucleus rep
            auto it = rep_to_nuc.find(j);
            if (it == rep_to_nuc.end()) return;

            uint32_t nj = it->second;
            const auto& nuc_b = detected_nuclei_[nj];
            if (nuc_b.Z == 0) return;
            if (nuc_b.is_anti != nuc.is_anti) return;  // no matter-antimatter bonds
            if (nuc_b.rep == rep_a) return;  // same nucleus

            // Already bonded?
            if (has_bond(rep_a, nuc_b.rep)) return;

            // Partner has room?
            int valence_b = valence_from_Z(nuc_b.Z);
            if (valence_b <= 0) return;
            if (bond_count(nuc_b.rep) >= valence_b) return;

            // Distance check (use nucleus centers)
            glm::vec2 delta = nuc_b.center - center_a;
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FORM_RADIUS * FORM_RADIUS) return;

            // Activation energy: relative KE between nucleus centers
            glm::vec2 vel_a = readback_velocities_[rep_a];
            glm::vec2 vel_b = readback_velocities_[nuc_b.rep];
            glm::vec2 rel_v = vel_b - vel_a;
            float v_rel_sq = glm::dot(rel_v, rel_v);
            if (v_rel_sq > ACTIVATION_E * 100.0f) return;  // too fast = scattering, not bonding
            // (no minimum check — thermal capture is fine)

            // Form the bond
            if (add_bond_slot(rep_a, nuc_b.rep) && add_bond_slot(nuc_b.rep, rep_a)) {
                current_bonds_a++;
                new_bonds++;
                any_changed = true;
                try_unlock(ACH_FIRST_BOND);
                {
                    char desc[128], detail[384];
                    int A_a = nuc.Z + nuc.N, A_b = nuc_b.Z + nuc_b.N;
                    float dist = std::sqrt(d2);
                    snprintf(desc, sizeof(desc), "Bond formed: %s-%d — %s-%d",
                             bsym(nuc.Z), A_a, bsym(nuc_b.Z), A_b);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d, valence %d, bonds %d)\n"
                             "Atom B: %s-%d (rep #%u, Z=%d N=%d, valence %d, bonds %d)\n"
                             "Distance: %.1f px",
                             bsym(nuc.Z), A_a, rep_a, nuc.Z, nuc.N,
                             valence, current_bonds_a,
                             bsym(nuc_b.Z), A_b, nuc_b.rep, nuc_b.Z, nuc_b.N,
                             valence_b, bond_count(nuc_b.rep),
                             dist);
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_FORMED,
                        ImVec4(0.3f, 0.85f, 0.5f, 1.0f), std::string(detail));
                }
            }
        };

        grid_.query(center_a.x, center_a.y, FORM_RADIUS, bond_search);
    }

    // Update particle bond pointers for GPU upload
    particles.bond_partners_ptr = bond_data_.data();
    particles.bond_partners_count = static_cast<uint32_t>(bond_data_.size());

    if (any_changed) {
        cpu_particles_dirty_ = true;
    }
}

// ── CPU-side particle decay ──────────────────────────────────────────────────
// Detects particles whose energy has been drained below threshold by the shader
// (via genome[3] decay_rate) and converts them to appropriate decay products.

void PhysicsSimulation::check_decay() {
    if (!cfg.decay_enabled) return;
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float DECAY_THRESHOLD = cfg.decay_threshold;

    bool any_decayed = false;
    std::mt19937 rng(frame_counter_ * 2654435761u);

    // Helper to find a dormant slot
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k) {
            if (readback_energies_[k] < 0.01f) return k;
        }
        for (uint32_t k = 0; k < start; ++k) {
            if (readback_energies_[k] < 0.01f) return k;
        }
        return UINT32_MAX;
    };

    // Random direction helper
    auto rand_dir = [&]() -> glm::vec2 {
        float a = angle_dist_(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    for (uint32_t i = 0; i < n; ++i) {
        float energy = readback_energies_[i];
        if (energy < 0.01f || energy > DECAY_THRESHOLD) continue;

        uint32_t type = particles.types[i];
        if (type >= PHYS_PARTICLE_TYPES) continue;
        if (PHYS_DECAY_RATE[type] < 0.001f) continue;  // stable particle

        glm::vec2 pos = readback_positions_[i];
        glm::vec2 dir = rand_dir();

        any_decayed = true;

        switch (type) {
            // ── Top quark → W + bottom ──
            case TOP_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                // M=172760, m1=4180(b), m2=80379(W) → 2-body kinematics
                float p_dec = two_body_decay_momentum(PHYS_REST_MASS_MEV[TOP_QUARK_TYPE],
                    PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE], PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS]);
                float KE_b = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE] * PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE]) - PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE];
                float KE_w = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS] * PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS]) - PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS];
                write_spawn_genome(particles, i, BOTTOM_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE] + KE_b);
                readback_velocities_[i] = dir * ke_to_speed(KE_b, BOTTOM_QUARK_TYPE);
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * ke_to_speed(KE_w, W_PLUS_TYPE_PHYS);
                    readback_energies_[w_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS] + KE_w);
                }
                iface.push_notification("Decay: t \xe2\x86\x92 b + W\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: top quark #%u\nParent energy: %.4f\nDecay momentum: %.2f MeV/c\nProduct b #%u KE: %.2f MeV\nProduct W+ #%u KE: %.2f MeV",
                        i, readback_energies_[i],
                        p_dec, i, KE_b, w_slot, KE_w);
                    iface.push_decay_event("t \xe2\x86\x92 b + W\xe2\x81\xba", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }
            case ANTI_TOP_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                float p_dec = two_body_decay_momentum(PHYS_REST_MASS_MEV[ANTI_TOP_TYPE],
                    PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE], PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS]);
                float KE_b = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE] * PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE]) - PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE];
                float KE_w = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS] * PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS]) - PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS];
                write_spawn_genome(particles, i, ANTI_BOTTOM_TYPE, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE] + KE_b);
                readback_velocities_[i] = dir * ke_to_speed(KE_b, ANTI_BOTTOM_TYPE);
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * ke_to_speed(KE_w, W_MINUS_TYPE_PHYS);
                    readback_energies_[w_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS] + KE_w);
                }
                iface.push_notification("Decay: \xc4\xab \xe2\x86\x92 b\xcc\x84 + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: anti-top #%u\nParent energy: %.4f\nDecay momentum: %.2f MeV/c\nProduct b-bar #%u KE: %.2f MeV\nProduct W- #%u KE: %.2f MeV",
                        i, readback_energies_[i],
                        p_dec, i, KE_b, w_slot, KE_w);
                    iface.push_decay_event("\xc4\xab \xe2\x86\x92 b\xcc\x84 + W\xe2\x81\xbb", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── W+ → hadronic (67%) or leptonic (33%) ──
            case W_PLUS_TYPE_PHYS: {
                uint32_t p2_slot = find_dormant(i + 1);
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float roll = br(rng);
                uint32_t type1, type2;
                const char* notif_msg;
                const char* event_msg;
                if (roll < 0.32f) {
                    type1 = UP_QUARK_TYPE; type2 = ANTI_DOWN_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 u + d\xcc\x84";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 u + d\xcc\x84";
                } else if (roll < 0.335f) {
                    type1 = UP_QUARK_TYPE; type2 = ANTI_STRANGE_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 u + s\xcc\x84";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 u + s\xcc\x84";
                } else if (roll < 0.35f) {
                    type1 = CHARM_QUARK_TYPE; type2 = ANTI_DOWN_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 c + d\xcc\x84";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 c + d\xcc\x84";
                } else if (roll < 0.67f) {
                    type1 = CHARM_QUARK_TYPE; type2 = ANTI_STRANGE_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 c + s\xcc\x84";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 c + s\xcc\x84";
                } else if (roll < 0.78f) {
                    type1 = POSITRON_TYPE_PHYS; type2 = NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd" "e";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd" "e";
                } else if (roll < 0.89f) {
                    type1 = ANTIMUON_TYPE_PHYS; type2 = MU_NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 \xce\xbc\xe2\x81\xba + \xce\xbd\xce\xbc";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 \xce\xbc\xe2\x81\xba + \xce\xbd\xce\xbc";
                } else {
                    type1 = ANTITAU_TYPE_PHYS; type2 = TAU_NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xba \xe2\x86\x92 \xcf\x84\xe2\x81\xba + \xce\xbd\xcf\x84";
                    event_msg = "W\xe2\x81\xba \xe2\x86\x92 \xcf\x84\xe2\x81\xba + \xce\xbd\xcf\x84";
                }
                float p_dec = two_body_decay_momentum(PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS],
                    PHYS_REST_MASS_MEV[type1], PHYS_REST_MASS_MEV[type2]);
                float KE_1 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type1] * PHYS_REST_MASS_MEV[type1]) - PHYS_REST_MASS_MEV[type1];
                float KE_2 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type2] * PHYS_REST_MASS_MEV[type2]) - PHYS_REST_MASS_MEV[type2];
                write_spawn_genome(particles, i, type1, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[type1] + KE_1);
                readback_velocities_[i] = dir * ke_to_speed(KE_1, type1);
                if (p2_slot != UINT32_MAX) {
                    write_spawn_genome(particles, p2_slot, type2, rng, frame_counter_);
                    readback_positions_[p2_slot] = pos;
                    readback_velocities_[p2_slot] = -dir * ke_to_speed(KE_2, type2);
                    readback_energies_[p2_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[type2] + KE_2);
                }
                iface.push_notification(notif_msg, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: W+ #%u (M=%.0f MeV)\nBranch roll: %.3f\nDecay momentum: %.2f MeV/c\nProduct1 type=%u #%u KE: %.2f MeV\nProduct2 type=%u #%u KE: %.2f MeV",
                        i, PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS],
                        roll, p_dec,
                        type1, i, KE_1,
                        type2, p2_slot, KE_2);
                    iface.push_decay_event(event_msg, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── W- → hadronic (67%) or leptonic (33%) ──
            case W_MINUS_TYPE_PHYS: {
                uint32_t p2_slot = find_dormant(i + 1);
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float roll = br(rng);
                uint32_t type1, type2;
                const char* notif_msg;
                const char* event_msg;
                if (roll < 0.32f) {
                    type1 = DOWN_QUARK_TYPE; type2 = ANTI_UP_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 d + u\xcc\x84";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 d + u\xcc\x84";
                } else if (roll < 0.335f) {
                    type1 = STRANGE_QUARK_TYPE; type2 = ANTI_UP_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 s + u\xcc\x84";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 s + u\xcc\x84";
                } else if (roll < 0.35f) {
                    type1 = DOWN_QUARK_TYPE; type2 = ANTI_CHARM_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 d + c\xcc\x84";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 d + c\xcc\x84";
                } else if (roll < 0.67f) {
                    type1 = STRANGE_QUARK_TYPE; type2 = ANTI_CHARM_TYPE;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 s + c\xcc\x84";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 s + c\xcc\x84";
                } else if (roll < 0.78f) {
                    type1 = ELECTRON_TYPE_PHYS; type2 = NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcc\x84" "e";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcc\x84" "e";
                } else if (roll < 0.89f) {
                    type1 = MUON_TYPE_PHYS; type2 = MU_NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbd\xcc\x84\xce\xbc";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbd\xcc\x84\xce\xbc";
                } else {
                    type1 = TAU_TYPE_PHYS; type2 = TAU_NEUTRINO_TYPE_PHYS;
                    notif_msg = "Decay: W\xe2\x81\xbb \xe2\x86\x92 \xcf\x84\xe2\x81\xbb + \xce\xbd\xcc\x84\xcf\x84";
                    event_msg = "W\xe2\x81\xbb \xe2\x86\x92 \xcf\x84\xe2\x81\xbb + \xce\xbd\xcc\x84\xcf\x84";
                }
                float p_dec = two_body_decay_momentum(PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS],
                    PHYS_REST_MASS_MEV[type1], PHYS_REST_MASS_MEV[type2]);
                float KE_1 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type1] * PHYS_REST_MASS_MEV[type1]) - PHYS_REST_MASS_MEV[type1];
                float KE_2 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type2] * PHYS_REST_MASS_MEV[type2]) - PHYS_REST_MASS_MEV[type2];
                write_spawn_genome(particles, i, type1, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[type1] + KE_1);
                readback_velocities_[i] = dir * ke_to_speed(KE_1, type1);
                if (p2_slot != UINT32_MAX) {
                    write_spawn_genome(particles, p2_slot, type2, rng, frame_counter_);
                    readback_positions_[p2_slot] = pos;
                    readback_velocities_[p2_slot] = -dir * ke_to_speed(KE_2, type2);
                    readback_energies_[p2_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[type2] + KE_2);
                }
                iface.push_notification(notif_msg, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: W- #%u (M=%.0f MeV)\nBranch roll: %.3f\nDecay momentum: %.2f MeV/c\nProduct1 type=%u #%u KE: %.2f MeV\nProduct2 type=%u #%u KE: %.2f MeV",
                        i, PHYS_REST_MASS_MEV[W_MINUS_TYPE_PHYS],
                        roll, p_dec,
                        type1, i, KE_1,
                        type2, p2_slot, KE_2);
                    iface.push_decay_event(event_msg, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── Z0 → hadronic (70.7%) + leptonic (10.2%) + invisible (19.1%) ──
            case Z_BOSON_TYPE_PHYS: {
                uint32_t p2_slot = find_dormant(i + 1);
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float roll = br(rng);
                uint32_t type1, type2;
                const char* notif_msg;
                const char* event_msg;
                if (roll < 0.156f) {
                    type1 = UP_QUARK_TYPE; type2 = ANTI_UP_TYPE;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 u + u\xcc\x84";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 u + u\xcc\x84";
                } else if (roll < 0.312f) {
                    type1 = DOWN_QUARK_TYPE; type2 = ANTI_DOWN_TYPE;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 d + d\xcc\x84";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 d + d\xcc\x84";
                } else if (roll < 0.434f) {
                    type1 = STRANGE_QUARK_TYPE; type2 = ANTI_STRANGE_TYPE;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 s + s\xcc\x84";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 s + s\xcc\x84";
                } else if (roll < 0.556f) {
                    type1 = CHARM_QUARK_TYPE; type2 = ANTI_CHARM_TYPE;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 c + c\xcc\x84";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 c + c\xcc\x84";
                } else if (roll < 0.707f) {
                    type1 = BOTTOM_QUARK_TYPE; type2 = ANTI_BOTTOM_TYPE;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 b + b\xcc\x84";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 b + b\xcc\x84";
                } else if (roll < 0.741f) {
                    type1 = ELECTRON_TYPE_PHYS; type2 = POSITRON_TYPE_PHYS;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 e\xe2\x81\xbb + e\xe2\x81\xba";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 e\xe2\x81\xbb + e\xe2\x81\xba";
                } else if (roll < 0.775f) {
                    type1 = MUON_TYPE_PHYS; type2 = ANTIMUON_TYPE_PHYS;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbc\xe2\x81\xba";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbc\xe2\x81\xba";
                } else if (roll < 0.809f) {
                    type1 = TAU_TYPE_PHYS; type2 = ANTITAU_TYPE_PHYS;
                    notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 \xcf\x84\xe2\x81\xbb + \xcf\x84\xe2\x81\xba";
                    event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 \xcf\x84\xe2\x81\xbb + \xcf\x84\xe2\x81\xba";
                } else {
                    // νν̄ invisible (19.1%) — split 3 flavors
                    float nu_roll = br(rng);
                    if (nu_roll < 0.333f) {
                        type1 = NEUTRINO_TYPE_PHYS; type2 = NEUTRINO_TYPE_PHYS;
                        notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd" "e + \xce\xbd\xcc\x84" "e";
                        event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd" "e\xce\xbd\xcc\x84" "e";
                    } else if (nu_roll < 0.667f) {
                        type1 = MU_NEUTRINO_TYPE_PHYS; type2 = MU_NEUTRINO_TYPE_PHYS;
                        notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd\xce\xbc + \xce\xbd\xcc\x84\xce\xbc";
                        event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd\xce\xbc\xce\xbd\xcc\x84\xce\xbc";
                    } else {
                        type1 = TAU_NEUTRINO_TYPE_PHYS; type2 = TAU_NEUTRINO_TYPE_PHYS;
                        notif_msg = "Decay: Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd\xcf\x84 + \xce\xbd\xcc\x84\xcf\x84";
                        event_msg = "Z\xe2\x81\xb0 \xe2\x86\x92 \xce\xbd\xcf\x84\xce\xbd\xcc\x84\xcf\x84";
                    }
                }
                float p_dec = two_body_decay_momentum(PHYS_REST_MASS_MEV[Z_BOSON_TYPE_PHYS],
                    PHYS_REST_MASS_MEV[type1], PHYS_REST_MASS_MEV[type2]);
                float KE_1 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type1] * PHYS_REST_MASS_MEV[type1]) - PHYS_REST_MASS_MEV[type1];
                float KE_2 = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[type2] * PHYS_REST_MASS_MEV[type2]) - PHYS_REST_MASS_MEV[type2];
                write_spawn_genome(particles, i, type1, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[type1] + KE_1);
                readback_velocities_[i] = dir * ke_to_speed(KE_1, type1);
                if (p2_slot != UINT32_MAX) {
                    write_spawn_genome(particles, p2_slot, type2, rng, frame_counter_);
                    readback_positions_[p2_slot] = pos;
                    readback_velocities_[p2_slot] = -dir * ke_to_speed(KE_2, type2);
                    readback_energies_[p2_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[type2] + KE_2);
                }
                iface.push_notification(notif_msg, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: Z0 #%u (M=%.0f MeV)\nBranch roll: %.3f\nDecay momentum: %.2f MeV/c\nProduct1 type=%u #%u KE: %.2f MeV\nProduct2 type=%u #%u KE: %.2f MeV",
                        i, PHYS_REST_MASS_MEV[Z_BOSON_TYPE_PHYS],
                        roll, p_dec,
                        type1, i, KE_1,
                        type2, p2_slot, KE_2);
                    iface.push_decay_event(event_msg, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── Higgs → branching: bb̄(40%), WW*(25%), ττ̄(15%), ZZ*(10%), γγ(10%) ──
            case HIGGS_TYPE_PHYS: {
                uint32_t p2_slot = find_dormant(i + 1);
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float roll = br(rng);
                float M_H = PHYS_REST_MASS_MEV[HIGGS_TYPE_PHYS];

                if (roll < 0.40f) {
                    // H → bb̄ (dominant channel, BR 58% real, ~40% sim)
                    float p_dec = two_body_decay_momentum(M_H, PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE], PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE]);
                    float KE_b = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE] * PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE]) - PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE];
                    write_spawn_genome(particles, i, BOTTOM_QUARK_TYPE, rng, frame_counter_);
                    readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE] + KE_b);
                    readback_velocities_[i] = dir * ke_to_speed(KE_b, BOTTOM_QUARK_TYPE);
                    if (p2_slot != UINT32_MAX) {
                        write_spawn_genome(particles, p2_slot, ANTI_BOTTOM_TYPE, rng, frame_counter_);
                        readback_positions_[p2_slot] = pos;
                        readback_velocities_[p2_slot] = -dir * ke_to_speed(KE_b, ANTI_BOTTOM_TYPE);
                        readback_energies_[p2_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE] + KE_b);
                    }
                    iface.push_notification("Decay: H \xe2\x86\x92 b + b\xcc\x84", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: Higgs #%u (M=%.0f MeV)\nChannel: H->bb-bar (BR~40%%)\nDecay momentum: %.2f MeV/c\nb #%u KE: %.2f MeV\nb-bar #%u KE: %.2f MeV",
                            i, M_H, p_dec, i, KE_b, p2_slot, KE_b);
                        iface.push_decay_event("H \xe2\x86\x92 bb\xcc\x84", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else if (roll < 0.65f) {
                    // H → W⁺W⁻* (off-shell, M_H < 2*M_W; split energy evenly)
                    float E_each = M_H * 0.5f;
                    write_spawn_genome(particles, i, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_energies_[i] = mev_to_ebuf(E_each);
                    readback_velocities_[i] = dir * ke_to_speed(E_each * 0.3f, W_PLUS_TYPE_PHYS);
                    if (p2_slot != UINT32_MAX) {
                        write_spawn_genome(particles, p2_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                        readback_positions_[p2_slot] = pos;
                        readback_velocities_[p2_slot] = -dir * ke_to_speed(E_each * 0.3f, W_MINUS_TYPE_PHYS);
                        readback_energies_[p2_slot] = mev_to_ebuf(E_each);
                    }
                    iface.push_notification("Decay: H \xe2\x86\x92 W\xe2\x81\xba + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: Higgs #%u (M=%.0f MeV)\nChannel: H->WW* off-shell (BR~25%%)\nE_each: %.2f MeV\nW+ #%u, W- #%u",
                            i, M_H, E_each, i, p2_slot);
                        iface.push_decay_event("H \xe2\x86\x92 W\xe2\x81\xbaW\xe2\x81\xbb", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else if (roll < 0.80f) {
                    // H → τ⁺τ⁻
                    float p_dec = two_body_decay_momentum(M_H, PHYS_REST_MASS_MEV[TAU_TYPE_PHYS], PHYS_REST_MASS_MEV[ANTITAU_TYPE_PHYS]);
                    float KE_t = std::sqrt(p_dec * p_dec + PHYS_REST_MASS_MEV[TAU_TYPE_PHYS] * PHYS_REST_MASS_MEV[TAU_TYPE_PHYS]) - PHYS_REST_MASS_MEV[TAU_TYPE_PHYS];
                    write_spawn_genome(particles, i, TAU_TYPE_PHYS, rng, frame_counter_);
                    readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[TAU_TYPE_PHYS] + KE_t);
                    readback_velocities_[i] = dir * ke_to_speed(KE_t, TAU_TYPE_PHYS);
                    if (p2_slot != UINT32_MAX) {
                        write_spawn_genome(particles, p2_slot, ANTITAU_TYPE_PHYS, rng, frame_counter_);
                        readback_positions_[p2_slot] = pos;
                        readback_velocities_[p2_slot] = -dir * ke_to_speed(KE_t, ANTITAU_TYPE_PHYS);
                        readback_energies_[p2_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[ANTITAU_TYPE_PHYS] + KE_t);
                    }
                    iface.push_notification("Decay: H \xe2\x86\x92 \xcf\x84\xe2\x81\xbb + \xcf\x84\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: Higgs #%u (M=%.0f MeV)\nChannel: H->tau+tau- (BR~15%%)\nDecay momentum: %.2f MeV/c\ntau- #%u KE: %.2f MeV\ntau+ #%u KE: %.2f MeV",
                            i, M_H, p_dec, i, KE_t, p2_slot, KE_t);
                        iface.push_decay_event("H \xe2\x86\x92 \xcf\x84\xcf\x84\xcc\x84", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else if (roll < 0.90f) {
                    // H → ZZ* (off-shell, M_H < 2*M_Z; split energy evenly)
                    float E_each = M_H * 0.5f;
                    write_spawn_genome(particles, i, Z_BOSON_TYPE_PHYS, rng, frame_counter_);
                    readback_energies_[i] = mev_to_ebuf(E_each);
                    readback_velocities_[i] = dir * ke_to_speed(E_each * 0.3f, Z_BOSON_TYPE_PHYS);
                    if (p2_slot != UINT32_MAX) {
                        write_spawn_genome(particles, p2_slot, Z_BOSON_TYPE_PHYS, rng, frame_counter_);
                        readback_positions_[p2_slot] = pos;
                        readback_velocities_[p2_slot] = -dir * ke_to_speed(E_each * 0.3f, Z_BOSON_TYPE_PHYS);
                        readback_energies_[p2_slot] = mev_to_ebuf(E_each);
                    }
                    iface.push_notification("Decay: H \xe2\x86\x92 Z + Z", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: Higgs #%u (M=%.0f MeV)\nChannel: H->ZZ* off-shell (BR~10%%)\nE_each: %.2f MeV\nZ1 #%u, Z2 #%u",
                            i, M_H, E_each, i, p2_slot);
                        iface.push_decay_event("H \xe2\x86\x92 ZZ", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else {
                    // H → γγ (rare but iconic, BR 0.23% real, ~10% sim)
                    float E_photon = M_H * 0.5f;
                    write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_energies_[i] = mev_to_ebuf(E_photon);
                    readback_velocities_[i] = dir * C_SIM;
                    if (p2_slot != UINT32_MAX) {
                        write_spawn_genome(particles, p2_slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                        readback_positions_[p2_slot] = pos;
                        readback_velocities_[p2_slot] = -dir * C_SIM;
                        readback_energies_[p2_slot] = mev_to_ebuf(E_photon);
                    }
                    iface.push_notification("Decay: H \xe2\x86\x92 \xce\xb3 + \xce\xb3", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    try_unlock(ACH_PHOTON_EMISSION);
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: Higgs #%u (M=%.0f MeV)\nChannel: H->gamma+gamma (BR~10%%, rare)\nE_photon each: %.2f MeV\ngamma1 #%u, gamma2 #%u",
                            i, M_H, E_photon, i, p2_slot);
                        iface.push_decay_event("H \xe2\x86\x92 \xce\xb3\xce\xb3", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                }
                break;
            }

            // ── Tau → lepton + ντ + ν̄l (3-body, branching: ~50% electronic, ~50% muonic) ──
            case TAU_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                // Branching: τ⁻ → e⁻ + ν̄ₑ + ντ (BR 17.8%) or τ⁻ → μ⁻ + ν̄μ + ντ (BR 17.4%)
                // Normalized to leptonic channels only: ~50/50
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                bool muonic = (br(rng) < 0.5f);
                uint32_t lepton_type = muonic ? MUON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
                uint32_t antinu_flavor = muonic ? MU_NEUTRINO_TYPE_PHYS : NEUTRINO_TYPE_PHYS;
                float Q = PHYS_REST_MASS_MEV[TAU_TYPE_PHYS] - PHYS_REST_MASS_MEV[lepton_type];
                float KE_l = Q * 0.33f;
                float KE_nu1 = Q * 0.33f;
                float KE_nu2 = Q * 0.34f;
                write_spawn_genome(particles, i, lepton_type, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[lepton_type] + KE_l);
                readback_velocities_[i] = dir * ke_to_speed(KE_l, lepton_type);
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, TAU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * C_SIM * 0.9999f;
                    readback_energies_[nu1] = mev_to_ebuf(KE_nu1);
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, antinu_flavor, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu2] = mev_to_ebuf(KE_nu2);
                }
                if (muonic) {
                    iface.push_notification("Decay: \xcf\x84\xe2\x81\xbb \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbd\xcf\x84 + \xce\xbd\xcc\x84\xce\xbc", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: tau- #%u (M=%.2f MeV)\nChannel: muonic (BR~50%%)\nQ=%.2f MeV\nmu- #%u KE: %.2f MeV\nnu_tau #%u E: %.2f MeV\nnu-bar_mu #%u E: %.2f MeV",
                            i, PHYS_REST_MASS_MEV[TAU_TYPE_PHYS], Q,
                            i, KE_l, nu1, KE_nu1, nu2, KE_nu2);
                        iface.push_decay_event("\xcf\x84\xe2\x81\xbb \xe2\x86\x92 \xce\xbc\xe2\x81\xbb + \xce\xbd\xcf\x84 + \xce\xbd\xcc\x84\xce\xbc", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else {
                    iface.push_notification("Decay: \xcf\x84\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcf\x84 + \xce\xbd\xcc\x84" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: tau- #%u (M=%.2f MeV)\nChannel: electronic (BR~50%%)\nQ=%.2f MeV\ne- #%u KE: %.2f MeV\nnu_tau #%u E: %.2f MeV\nnu-bar_e #%u E: %.2f MeV",
                            i, PHYS_REST_MASS_MEV[TAU_TYPE_PHYS], Q,
                            i, KE_l, nu1, KE_nu1, nu2, KE_nu2);
                        iface.push_decay_event("\xcf\x84\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcf\x84 + \xce\xbd\xcc\x84" "e", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                }
                break;
            }
            case ANTITAU_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                bool muonic = (br(rng) < 0.5f);
                uint32_t lepton_type = muonic ? ANTIMUON_TYPE_PHYS : POSITRON_TYPE_PHYS;
                uint32_t nu_flavor = muonic ? MU_NEUTRINO_TYPE_PHYS : NEUTRINO_TYPE_PHYS;
                float Q = PHYS_REST_MASS_MEV[ANTITAU_TYPE_PHYS] - PHYS_REST_MASS_MEV[lepton_type];
                float KE_l = Q * 0.33f;
                float KE_nu1 = Q * 0.33f;
                float KE_nu2 = Q * 0.34f;
                write_spawn_genome(particles, i, lepton_type, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[lepton_type] + KE_l);
                readback_velocities_[i] = dir * ke_to_speed(KE_l, lepton_type);
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, TAU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * C_SIM * 0.9999f;
                    readback_energies_[nu1] = mev_to_ebuf(KE_nu1);
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, nu_flavor, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu2] = mev_to_ebuf(KE_nu2);
                }
                if (muonic) {
                    iface.push_notification("Decay: \xcf\x84\xcc\x84 \xe2\x86\x92 \xce\xbc\xe2\x81\xba + \xce\xbd\xcf\x84 + \xce\xbd\xce\xbc", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: tau+ #%u (M=%.2f MeV)\nChannel: muonic (BR~50%%)\nQ=%.2f MeV\nmu+ #%u KE: %.2f MeV\nnu_tau #%u E: %.2f MeV\nnu_mu #%u E: %.2f MeV",
                            i, PHYS_REST_MASS_MEV[ANTITAU_TYPE_PHYS], Q,
                            i, KE_l, nu1, KE_nu1, nu2, KE_nu2);
                        iface.push_decay_event("\xcf\x84\xcc\x84 \xe2\x86\x92 \xce\xbc\xe2\x81\xba + \xce\xbd\xcf\x84 + \xce\xbd\xce\xbc", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                } else {
                    iface.push_notification("Decay: \xcf\x84\xcc\x84 \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xcf\x84 + \xce\xbd" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                    {
                        char detail[512];
                        snprintf(detail, sizeof(detail),
                            "Parent: tau+ #%u (M=%.2f MeV)\nChannel: electronic (BR~50%%)\nQ=%.2f MeV\ne+ #%u KE: %.2f MeV\nnu_tau #%u E: %.2f MeV\nnu_e #%u E: %.2f MeV",
                            i, PHYS_REST_MASS_MEV[ANTITAU_TYPE_PHYS], Q,
                            i, KE_l, nu1, KE_nu1, nu2, KE_nu2);
                        iface.push_decay_event("\xcf\x84\xcc\x84 \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xcf\x84 + \xce\xbd" "e", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                    }
                }
                break;
            }

            // ── Bottom → daughter + f f̄ (3-body via virtual W⁻*) ──
            case BOTTOM_QUARK_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_b = PHYS_REST_MASS_MEV[BOTTOM_QUARK_TYPE];
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float ckm_roll = br(rng);
                float norm = CKM_BR[0][2] + CKM_BR[1][2]; // Vub² + Vcb²
                uint32_t daughter = (ckm_roll < CKM_BR[1][2] / norm) ? CHARM_QUARK_TYPE : UP_QUARK_TYPE;
                const char* d_name = (daughter == CHARM_QUARK_TYPE) ? "c" : "u";
                float Q = M_b - PHYS_REST_MASS_MEV[daughter];
                float KE_each = Q / 3.0f;
                // Virtual W⁻* products: hadronic (67%) d+ū, leptonic (33%) ℓ⁻+ν̄
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                float wroll = br(rng);
                if (wroll < 0.67f) {
                    f1_type = DOWN_QUARK_TYPE;   f1_name = "d";
                    f2_type = ANTI_UP_TYPE;      f2_name = "u\xcc\x84";
                } else if (wroll < 0.89f) {
                    f1_type = ELECTRON_TYPE_PHYS; f1_name = "e\xe2\x81\xbb";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                } else {
                    f1_type = MUON_TYPE_PHYS;        f1_name = "\xce\xbc\xe2\x81\xbb";
                    f2_type = MU_NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd\xce\xbc";
                }
                write_spawn_genome(particles, i, daughter, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[daughter] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, daughter);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: b \xe2\x86\x92 %s + %s + %s", d_name, f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: b quark #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\n%s #%u + %s #%u + %s #%u",
                        i, M_b, Q, KE_each, d_name, i, f1_name, slot1, f2_name, slot2);
                    char ev[64]; snprintf(ev, sizeof(ev), "b \xe2\x86\x92 %s %s%s", d_name, f1_name, f2_name);
                    iface.push_decay_event(ev, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }
            case ANTI_BOTTOM_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_b = PHYS_REST_MASS_MEV[ANTI_BOTTOM_TYPE];
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float ckm_roll = br(rng);
                float norm = CKM_BR[0][2] + CKM_BR[1][2];
                uint32_t daughter = (ckm_roll < CKM_BR[1][2] / norm) ? ANTI_CHARM_TYPE : ANTI_UP_TYPE;
                const char* d_name = (daughter == ANTI_CHARM_TYPE) ? "c\xcc\x84" : "u\xcc\x84";
                float Q = M_b - PHYS_REST_MASS_MEV[daughter];
                float KE_each = Q / 3.0f;
                // Virtual W⁺* products: hadronic (67%) u+d̄, leptonic (33%) ℓ⁺+ν
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                float wroll = br(rng);
                if (wroll < 0.67f) {
                    f1_type = UP_QUARK_TYPE;     f1_name = "u";
                    f2_type = ANTI_DOWN_TYPE;    f2_name = "d\xcc\x84";
                } else if (wroll < 0.89f) {
                    f1_type = POSITRON_TYPE_PHYS; f1_name = "e\xe2\x81\xba";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                } else {
                    f1_type = ANTIMUON_TYPE_PHYS;    f1_name = "\xce\xbc\xe2\x81\xba";
                    f2_type = MU_NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd\xce\xbc";
                }
                write_spawn_genome(particles, i, daughter, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[daughter] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, daughter);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: b\xcc\x84 \xe2\x86\x92 %s + %s + %s", d_name, f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: b-bar #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\n%s #%u + %s #%u + %s #%u",
                        i, M_b, Q, KE_each, d_name, i, f1_name, slot1, f2_name, slot2);
                    char ev[64]; snprintf(ev, sizeof(ev), "b\xcc\x84 \xe2\x86\x92 %s %s%s", d_name, f1_name, f2_name);
                    iface.push_decay_event(ev, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── Charm → daughter + f f̄ (3-body via virtual W⁺*) ──
            // CKM: s (95.0%), d (5.0%) — b excluded (M_b > M_c)
            case CHARM_QUARK_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_c = PHYS_REST_MASS_MEV[CHARM_QUARK_TYPE];
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float ckm_roll = br(rng);
                float norm_ckm = CKM_BR[1][1] + CKM_BR[1][0]; // Vcs² + Vcd² (exclude Vcb — M_b > M_c)
                uint32_t daughter = (ckm_roll < CKM_BR[1][1] / norm_ckm) ? STRANGE_QUARK_TYPE : DOWN_QUARK_TYPE;
                const char* d_name = (daughter == STRANGE_QUARK_TYPE) ? "s" : "d";
                float Q = M_c - PHYS_REST_MASS_MEV[daughter];
                float KE_each = Q / 3.0f;
                // Virtual W⁺* products: hadronic (67%) u+d̄, leptonic (33%) ℓ⁺+ν
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                float wroll = br(rng);
                if (wroll < 0.67f) {
                    f1_type = UP_QUARK_TYPE;     f1_name = "u";
                    f2_type = ANTI_DOWN_TYPE;    f2_name = "d\xcc\x84";
                } else if (wroll < 0.89f) {
                    f1_type = POSITRON_TYPE_PHYS; f1_name = "e\xe2\x81\xba";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                } else {
                    f1_type = ANTIMUON_TYPE_PHYS;    f1_name = "\xce\xbc\xe2\x81\xba";
                    f2_type = MU_NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd\xce\xbc";
                }
                write_spawn_genome(particles, i, daughter, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[daughter] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, daughter);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: c \xe2\x86\x92 %s + %s + %s", d_name, f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: charm #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\n%s #%u + %s #%u + %s #%u",
                        i, M_c, Q, KE_each, d_name, i, f1_name, slot1, f2_name, slot2);
                    char ev[64]; snprintf(ev, sizeof(ev), "c \xe2\x86\x92 %s %s%s", d_name, f1_name, f2_name);
                    iface.push_decay_event(ev, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }
            case ANTI_CHARM_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_c = PHYS_REST_MASS_MEV[ANTI_CHARM_TYPE];
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                float ckm_roll = br(rng);
                float norm_ckm = CKM_BR[1][1] + CKM_BR[1][0];
                uint32_t daughter = (ckm_roll < CKM_BR[1][1] / norm_ckm) ? ANTI_STRANGE_TYPE : ANTI_DOWN_TYPE;
                const char* d_name = (daughter == ANTI_STRANGE_TYPE) ? "s\xcc\x84" : "d\xcc\x84";
                float Q = M_c - PHYS_REST_MASS_MEV[daughter];
                float KE_each = Q / 3.0f;
                // Virtual W⁻* products: hadronic (67%) d+ū, leptonic (33%) ℓ⁻+ν̄
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                float wroll = br(rng);
                if (wroll < 0.67f) {
                    f1_type = DOWN_QUARK_TYPE;   f1_name = "d";
                    f2_type = ANTI_UP_TYPE;      f2_name = "u\xcc\x84";
                } else if (wroll < 0.89f) {
                    f1_type = ELECTRON_TYPE_PHYS; f1_name = "e\xe2\x81\xbb";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                } else {
                    f1_type = MUON_TYPE_PHYS;        f1_name = "\xce\xbc\xe2\x81\xbb";
                    f2_type = MU_NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd\xce\xbc";
                }
                write_spawn_genome(particles, i, daughter, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[daughter] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, daughter);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: c\xcc\x84 \xe2\x86\x92 %s + %s + %s", d_name, f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: anti-charm #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\n%s #%u + %s #%u + %s #%u",
                        i, M_c, Q, KE_each, d_name, i, f1_name, slot1, f2_name, slot2);
                    char ev[64]; snprintf(ev, sizeof(ev), "c\xcc\x84 \xe2\x86\x92 %s %s%s", d_name, f1_name, f2_name);
                    iface.push_decay_event(ev, PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── Strange → u + f f̄ (3-body via virtual W⁻*) ──
            // Only s → u allowed (M_c >> M_s, charm forbidden)
            // Q ≈ 91 MeV: hadronic u+d̄ OK, leptonic e+ν only (μ mass > Q)
            case STRANGE_QUARK_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_s = PHYS_REST_MASS_MEV[STRANGE_QUARK_TYPE];
                float Q = M_s - PHYS_REST_MASS_MEV[UP_QUARK_TYPE];
                float KE_each = Q / 3.0f;
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                // Virtual W⁻* products: hadronic (67%) d+ū, leptonic (33%) e⁻+ν̄e
                // (μ mass 105.7 > Q ≈ 91, so only electron channel)
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                if (br(rng) < 0.67f) {
                    f1_type = DOWN_QUARK_TYPE;    f1_name = "d";
                    f2_type = ANTI_UP_TYPE;       f2_name = "u\xcc\x84";
                } else {
                    f1_type = ELECTRON_TYPE_PHYS; f1_name = "e\xe2\x81\xbb";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                }
                write_spawn_genome(particles, i, UP_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[UP_QUARK_TYPE] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, UP_QUARK_TYPE);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: s \xe2\x86\x92 u + %s + %s", f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: strange #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\nu #%u + %s #%u + %s #%u",
                        i, M_s, Q, KE_each, i, f1_name, slot1, f2_name, slot2);
                    iface.push_decay_event("s \xe2\x86\x92 u + ff\xcc\x84", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }
            case ANTI_STRANGE_TYPE: {
                uint32_t slot1 = find_dormant(i + 1);
                uint32_t slot2 = (slot1 != UINT32_MAX) ? find_dormant(slot1 + 1) : UINT32_MAX;
                float M_s = PHYS_REST_MASS_MEV[ANTI_STRANGE_TYPE];
                float Q = M_s - PHYS_REST_MASS_MEV[ANTI_UP_TYPE];
                float KE_each = Q / 3.0f;
                std::uniform_real_distribution<float> br(0.0f, 1.0f);
                // Virtual W⁺* products: hadronic (67%) u+d̄, leptonic (33%) e⁺+ν
                uint32_t f1_type, f2_type;
                const char* f1_name; const char* f2_name;
                if (br(rng) < 0.67f) {
                    f1_type = UP_QUARK_TYPE;      f1_name = "u";
                    f2_type = ANTI_DOWN_TYPE;     f2_name = "d\xcc\x84";
                } else {
                    f1_type = POSITRON_TYPE_PHYS; f1_name = "e\xe2\x81\xba";
                    f2_type = NEUTRINO_TYPE_PHYS; f2_name = "\xce\xbd" "e";
                }
                write_spawn_genome(particles, i, ANTI_UP_TYPE, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[ANTI_UP_TYPE] + KE_each);
                readback_velocities_[i] = dir * ke_to_speed(KE_each, ANTI_UP_TYPE);
                if (slot1 != UINT32_MAX) {
                    write_spawn_genome(particles, slot1, f1_type, rng, frame_counter_);
                    readback_positions_[slot1] = pos;
                    readback_energies_[slot1] = mev_to_ebuf(PHYS_REST_MASS_MEV[f1_type] + KE_each);
                    readback_velocities_[slot1] = glm::vec2(-dir.y, dir.x) * ke_to_speed(KE_each, f1_type);
                }
                if (slot2 != UINT32_MAX) {
                    write_spawn_genome(particles, slot2, f2_type, rng, frame_counter_);
                    readback_positions_[slot2] = pos;
                    readback_energies_[slot2] = mev_to_ebuf(PHYS_REST_MASS_MEV[f2_type] + KE_each);
                    readback_velocities_[slot2] = -dir * ke_to_speed(KE_each, f2_type);
                }
                char notif[128]; snprintf(notif, sizeof(notif), "Decay: s\xcc\x84 \xe2\x86\x92 u\xcc\x84 + %s + %s", f1_name, f2_name);
                iface.push_notification(notif, ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Parent: anti-strange #%u (M=%.0f MeV)\n3-body via virtual W*\nQ=%.1f MeV (KE ~%.1f each)\nu\xcc\x84 #%u + %s #%u + %s #%u",
                        i, M_s, Q, KE_each, i, f1_name, slot1, f2_name, slot2);
                    iface.push_decay_event("s\xcc\x84 \xe2\x86\x92 u\xcc\x84 + ff\xcc\x84", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ── Muon → electron + neutrino_mu + neutrino_e (3-body) ──
            case MUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                // M=105.658, products: e(0.511) + νμ(0) + ν̄e(0)
                // Available KE = M - me = 105.147 MeV, split ~1/3 each (phase space average)
                float Q_mu = PHYS_REST_MASS_MEV[MUON_TYPE_PHYS] - PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS];
                float KE_e = Q_mu * 0.33f;   // electron gets ~1/3
                float KE_nu1 = Q_mu * 0.33f;  // νμ
                float KE_nu2 = Q_mu * 0.34f;  // ν̄e
                // Check cascade chain before overwriting type
                bool from_cascade = (i < particles.cascade_tag.size() && particles.cascade_tag[i] == 3);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS] + KE_e);
                readback_velocities_[i] = dir * ke_to_speed(KE_e, ELECTRON_TYPE_PHYS);
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * C_SIM * 0.9999f;
                    readback_energies_[nu1] = mev_to_ebuf(KE_nu1);
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu2] = mev_to_ebuf(KE_nu2);
                }
                // Clear cascade tag — chain complete
                if (i < particles.cascade_tag.size()) particles.cascade_tag[i] = 0;
                if (from_cascade) {
                    iface.push_notification("Cascade: \xCE\xB3+N \xE2\x86\x92 \xCF\x80 \xE2\x86\x92 \xCE\xBC\xE2\x81\xBB \xE2\x86\x92 e\xE2\x81\xBB", ImVec4(0.3f, 1.0f, 0.6f, 1.0f));
                } else {
                    iface.push_notification("Decay: \xce\xbc\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xce\xbc + \xce\xbd\xcc\x84" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                }
                {
                    char detail[512];
                    int len = snprintf(detail, sizeof(detail),
                        "Parent: \xCE\xBC\xE2\x81\xBB #%u (M=%.3f MeV)\nQ=%.3f MeV\ne\xE2\x81\xBB #%u KE: %.3f MeV\n\xCE\xBD\xCE\xBC #%u E: %.3f MeV\n\xCE\xBD\xCC\x84" "e #%u E: %.3f MeV",
                        i, PHYS_REST_MASS_MEV[MUON_TYPE_PHYS], Q_mu,
                        i, KE_e, nu1, KE_nu1, nu2, KE_nu2);
                    if (from_cascade)
                        snprintf(detail + len, sizeof(detail) - len,
                            "\n\xE2\x94\x80 CASCADE COMPLETE: \xCE\xB3+N \xE2\x86\x92 \xCF\x80\xE2\x81\xBB \xE2\x86\x92 \xCE\xBC\xE2\x81\xBB \xE2\x86\x92 e\xE2\x81\xBB+\xCE\xBD\xCE\xBC+\xCE\xBD\xCC\x84" "e");
                    iface.push_decay_event(
                        from_cascade
                            ? "\xCE\xBC\xE2\x81\xBB \xE2\x86\x92 e\xE2\x81\xBB (cascade)"
                            : "\xce\xbc\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xce\xbc + \xce\xbd\xcc\x84" "e",
                        PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }
            case ANTIMUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                float Q_mu = PHYS_REST_MASS_MEV[ANTIMUON_TYPE_PHYS] - PHYS_REST_MASS_MEV[POSITRON_TYPE_PHYS];
                float KE_e = Q_mu * 0.33f;
                float KE_nu1 = Q_mu * 0.33f;
                float KE_nu2 = Q_mu * 0.34f;
                // Check cascade chain before overwriting type
                bool from_cascade = (i < particles.cascade_tag.size() && particles.cascade_tag[i] == 3);
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[POSITRON_TYPE_PHYS] + KE_e);
                readback_velocities_[i] = dir * ke_to_speed(KE_e, POSITRON_TYPE_PHYS);
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * C_SIM * 0.9999f;
                    readback_energies_[nu1] = mev_to_ebuf(KE_nu1);
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu2] = mev_to_ebuf(KE_nu2);
                }
                // Clear cascade tag — chain complete
                if (i < particles.cascade_tag.size()) particles.cascade_tag[i] = 0;
                if (from_cascade) {
                    iface.push_notification("Cascade: \xCE\xB3+N \xE2\x86\x92 \xCF\x80 \xE2\x86\x92 \xCE\xBC\xE2\x81\xBA \xE2\x86\x92 e\xE2\x81\xBA", ImVec4(0.3f, 1.0f, 0.6f, 1.0f));
                } else {
                    iface.push_notification("Decay: \xce\xbc\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xce\xbc + \xce\xbd" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                }
                {
                    char detail[512];
                    int len = snprintf(detail, sizeof(detail),
                        "Parent: \xCE\xBC\xE2\x81\xBA #%u (M=%.3f MeV)\nQ=%.3f MeV\ne\xE2\x81\xBA #%u KE: %.3f MeV\n\xCE\xBD\xCE\xBC #%u E: %.3f MeV\n\xCE\xBD" "e #%u E: %.3f MeV",
                        i, PHYS_REST_MASS_MEV[ANTIMUON_TYPE_PHYS], Q_mu,
                        i, KE_e, nu1, KE_nu1, nu2, KE_nu2);
                    if (from_cascade)
                        snprintf(detail + len, sizeof(detail) - len,
                            "\n\xE2\x94\x80 CASCADE COMPLETE: \xCE\xB3+N \xE2\x86\x92 \xCF\x80\xE2\x81\xBA \xE2\x86\x92 \xCE\xBC\xE2\x81\xBA \xE2\x86\x92 e\xE2\x81\xBA+\xCE\xBD\xCE\xBC+\xCE\xBD" "e");
                    iface.push_decay_event(
                        from_cascade
                            ? "\xCE\xBC\xE2\x81\xBA \xE2\x86\x92 e\xE2\x81\xBA (cascade)"
                            : "\xce\xbc\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xce\xbc + \xce\xbd" "e",
                        PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.8f, 0.5f, 1.0f), std::string(detail));
                }
                break;
            }

            // ═══════════════════════════════════════════════════════════════
            //  HYPOTHETICAL PARTICLE DECAYS
            // ═══════════════════════════════════════════════════════════════

            // ── Axino → photon + neutralino (SUSY cascade) ──
            case AXINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 5.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: Axino -> gamma + N1", ImVec4(0.6f, 0.3f, 0.9f, 1.0f));
                iface.push_decay_event("Axino -> gamma + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.6f, 0.3f, 0.9f, 1.0f), "");
                break;
            }

            // ── Dark Photon → e+ e- ──
            case DARK_PHOTON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 80.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 80.0f;
                    readback_energies_[slot] = 0.6f;
                }
                iface.push_notification("Decay: A' -> e+ e-", ImVec4(0.5f, 0.2f, 0.8f, 1.0f));
                iface.push_decay_event("A' -> e+ e-", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.5f, 0.2f, 0.8f, 1.0f), "");
                break;
            }

            // ── Selectron → electron + neutralino ──
            case SELECTRON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 100.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: e~ -> e + N1", ImVec4(0.8f, 0.6f, 0.9f, 1.0f));
                iface.push_decay_event("e~ -> e + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.8f, 0.6f, 0.9f, 1.0f), "");
                break;
            }

            // ── Smuon → muon + neutralino ──
            case SMUON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, MUON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 60.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: mu~ -> mu + N1", ImVec4(0.8f, 0.5f, 0.9f, 1.0f));
                iface.push_decay_event("mu~ -> mu + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.8f, 0.5f, 0.9f, 1.0f), "");
                break;
            }

            // ── Stau → tau + neutralino ──
            case STAU_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, TAU_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 30.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: tau~ -> tau + N1", ImVec4(0.7f, 0.5f, 0.9f, 1.0f));
                iface.push_decay_event("tau~ -> tau + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.7f, 0.5f, 0.9f, 1.0f), "");
                break;
            }

            // ── Squark → quark + gluino ──
            case SQUARK_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, UP_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 50.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, GLUINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 5.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: q~ -> q + g~", ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
                iface.push_decay_event("q~ -> q + g~", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.9f, 0.7f, 0.5f, 1.0f), "");
                break;
            }

            // ── Gluino → gluon + neutralino ──
            case GLUINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, GLUON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 5.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: g~ -> g + N1", ImVec4(0.9f, 0.6f, 0.4f, 1.0f));
                iface.push_decay_event("g~ -> g + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.9f, 0.6f, 0.4f, 1.0f), "");
                break;
            }

            // ── Photino → photon + neutralino ──
            case PHOTINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: y~ -> gamma + N1", ImVec4(0.9f, 0.8f, 0.5f, 1.0f));
                iface.push_decay_event("y~ -> gamma + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.9f, 0.8f, 0.5f, 1.0f), "");
                break;
            }

            // ── Wino → W + neutralino ──
            case WINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 20.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: W~ -> W + N1", ImVec4(0.8f, 0.7f, 0.5f, 1.0f));
                iface.push_decay_event("W~ -> W + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.8f, 0.7f, 0.5f, 1.0f), "");
                break;
            }

            // ── Zino → Z + neutralino ──
            case ZINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, Z_BOSON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 20.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: Z~ -> Z + N1", ImVec4(0.8f, 0.7f, 0.6f, 1.0f));
                iface.push_decay_event("Z~ -> Z + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.8f, 0.7f, 0.6f, 1.0f), "");
                break;
            }

            // ── Higgsino → Higgs + neutralino ──
            case HIGGSINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, HIGGS_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 10.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: H~ -> H + N1", ImVec4(0.7f, 0.8f, 0.5f, 1.0f));
                iface.push_decay_event("H~ -> H + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.7f, 0.8f, 0.5f, 1.0f), "");
                break;
            }

            // ── Sneutrino → neutrino + neutralino ──
            case SNEUTRINO_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM * 0.9999f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRALINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 10.0f;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: v~ -> nu + N1", ImVec4(0.6f, 0.8f, 0.5f, 1.0f));
                iface.push_decay_event("v~ -> nu + N1", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.6f, 0.8f, 0.5f, 1.0f), "");
                break;
            }

            // ── X Boson → quark + lepton (GUT proton decay mediator) ──
            case X_BOSON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, UP_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 100.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 100.0f;
                    readback_energies_[slot] = 0.7f;
                }
                iface.push_notification("Decay: X -> u + e+", ImVec4(0.0f, 1.0f, 0.8f, 1.0f));
                iface.push_decay_event("X -> u + e+", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "");
                break;
            }

            // ── Y Boson → quark + neutrino ──
            case Y_BOSON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, DOWN_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = 0.7f;
                readback_velocities_[i] = dir * 100.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM * 0.9999f;
                    readback_energies_[slot] = 0.5f;
                }
                iface.push_notification("Decay: Y -> d + nu", ImVec4(0.0f, 0.9f, 0.7f, 1.0f));
                iface.push_decay_event("Y -> d + nu", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.0f, 0.9f, 0.7f, 1.0f), "");
                break;
            }

            // ── Radion → photon pair ──
            case RADION_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM;
                    readback_energies_[slot] = 0.5f;
                }
                iface.push_notification("Decay: Radion -> gamma gamma", ImVec4(0.0f, 0.8f, 0.9f, 1.0f));
                iface.push_decay_event("Radion -> gamma gamma", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.0f, 0.8f, 0.9f, 1.0f), "");
                break;
            }

            // ── Dilaton → photon pair ──
            case DILATON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.4f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM;
                    readback_energies_[slot] = 0.4f;
                }
                iface.push_notification("Decay: Dilaton -> gamma gamma", ImVec4(0.0f, 0.7f, 0.8f, 1.0f));
                iface.push_decay_event("Dilaton -> gamma gamma", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.0f, 0.7f, 0.8f, 1.0f), "");
                break;
            }

            // ── Tachyon → photon pair (rapid decay) ──
            case TACHYON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM;
                    readback_energies_[slot] = 0.5f;
                }
                iface.push_notification("Decay: Tachyon -> gamma gamma", ImVec4(1.0f, 0.0f, 0.5f, 1.0f));
                iface.push_decay_event("Tachyon -> gamma gamma", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.0f, 0.5f, 1.0f), "");
                break;
            }

            // ── Inflaton → photon pair (reheating) ──
            case INFLATON_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.8f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM;
                    readback_energies_[slot] = 0.8f;
                }
                iface.push_notification("Decay: Inflaton -> gamma gamma", ImVec4(1.0f, 0.9f, 0.0f, 1.0f));
                iface.push_decay_event("Inflaton -> gamma gamma", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.9f, 0.0f, 1.0f), "");
                break;
            }

            // ── Odderon → 3 gluons (QCD exotic) ──
            case ODDERON_TYPE_PHYS: {
                uint32_t s1 = find_dormant(i + 1);
                uint32_t s2 = (s1 != UINT32_MAX) ? find_dormant(s1 + 1) : UINT32_MAX;
                write_spawn_genome(particles, i, GLUON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (s1 != UINT32_MAX) {
                    write_spawn_genome(particles, s1, GLUON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[s1] = pos;
                    readback_velocities_[s1] = glm::vec2(-dir.y, dir.x) * C_SIM;
                    readback_energies_[s1] = 0.5f;
                }
                if (s2 != UINT32_MAX) {
                    write_spawn_genome(particles, s2, GLUON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[s2] = pos;
                    readback_velocities_[s2] = -dir * C_SIM;
                    readback_energies_[s2] = 0.5f;
                }
                iface.push_notification("Decay: Odderon -> 3g", ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
                iface.push_decay_event("Odderon -> 3g", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.8f, 0.4f, 0.0f, 1.0f), "");
                break;
            }

            // ── Glueball → 2 gluons ──
            case GLUEBALL_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, GLUON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.5f;
                readback_velocities_[i] = dir * C_SIM;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, GLUON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM;
                    readback_energies_[slot] = 0.5f;
                }
                iface.push_notification("Decay: Glueball -> gg", ImVec4(0.7f, 0.5f, 0.0f, 1.0f));
                iface.push_decay_event("Glueball -> gg", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.7f, 0.5f, 0.0f, 1.0f), "");
                break;
            }

            // ── X17 → e+ e- (anomalous transition) ──
            case X17_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 80.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * 80.0f;
                    readback_energies_[slot] = 0.6f;
                }
                iface.push_notification("Decay: X17 -> e+ e-", ImVec4(0.9f, 0.3f, 0.6f, 1.0f));
                iface.push_decay_event("X17 -> e+ e-", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.9f, 0.3f, 0.6f, 1.0f), "");
                break;
            }

            // ── Paraparticle → W + neutrino ──
            case PARAPARTICLE_TYPE_PHYS: {
                uint32_t slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = 0.6f;
                readback_velocities_[i] = dir * 20.0f;
                if (slot != UINT32_MAX) {
                    write_spawn_genome(particles, slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[slot] = pos;
                    readback_velocities_[slot] = -dir * C_SIM * 0.9999f;
                    readback_energies_[slot] = 0.5f;
                }
                iface.push_notification("Decay: Pp -> W + nu", ImVec4(0.5f, 0.9f, 0.5f, 1.0f));
                iface.push_decay_event("Pp -> W + nu", PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "");
                break;
            }

            default:
                any_decayed = false;  // unknown type, skip
                break;
        }
    }

    if (any_decayed) {
        cpu_particles_dirty_ = true;
    }
}

// ── Nuclear isotope decay ────────────────────────────────────────────────────

void PhysicsSimulation::check_nuclear_decay() {
    if (!cfg.nuclear_decay_enabled) return;
    if (detected_nuclei_.empty() || readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;

    // Nuclear decay energies from mass deficits (MeV)
    const float ALPHA_KE = cfg.alpha_ke_mev;
    const float NUCLEON_EMIT_KE = cfg.nucleon_emit_ke_mev;

    std::mt19937 rng(frame_counter_ * 1337u + 7919u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    auto rand_dir = [&]() -> glm::vec2 {
        float a = angle_dist_(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    bool any_decayed = false;

    for (auto& nuc : detected_nuclei_) {
        // Skip single protons (hydrogen-1, stable)
        if (nuc.Z == 1 && nuc.N == 0) continue;
        // Skip pure neutron clusters with 0 protons handled separately
        // Look up explicit isotope table first
        const IsotopeDecayEntry* entry = lookup_isotope_decay(nuc.Z, nuc.N);
        NuclearDecayMode mode = NDECAY_NONE;
        float half_life = 0.0f;

        if (entry) {
            mode = entry->mode;
            half_life = entry->half_life_frames;
        } else {
            mode = general_stability_rule(nuc.Z, nuc.N, half_life);
        }

        if (mode == NDECAY_NONE) continue;

        // Apply global decay rate multiplier (< 1 = faster decay, > 1 = slower)
        half_life /= std::max(cfg.decay_rate_multiplier, 0.01f);

        // Probability of decay this frame: P = 1 - exp(-ln(2)/t½)
        float p_decay = 1.0f - std::exp(-0.693147f / half_life);
        if (unit(rng) > p_decay) continue;

        // Execute decay
        glm::vec2 dir = rand_dir();

        switch (mode) {
            case NDECAY_ALPHA: {
                // Eject 2 protons and 2 neutrons as alpha particle
                if (nuc.Z < 2 || nuc.N < 2) break;
                // Pick 2 protons and 2 neutrons from the nucleus
                uint32_t alpha_p[2], alpha_n[2];
                alpha_p[0] = nuc.proton_indices.back(); nuc.proton_indices.pop_back();
                alpha_p[1] = nuc.proton_indices.back(); nuc.proton_indices.pop_back();
                alpha_n[0] = nuc.neutron_indices.back(); nuc.neutron_indices.pop_back();
                alpha_n[1] = nuc.neutron_indices.back(); nuc.neutron_indices.pop_back();

                // Alpha KE ~5 MeV split among 4 nucleons → ~1.25 MeV each
                float alpha_speed = ke_to_speed(ALPHA_KE / 4.0f, PROTON_TYPE);
                float alpha_ebuf = mev_to_ebuf(PHYS_REST_MASS_MEV[PROTON_TYPE] + ALPHA_KE / 4.0f);
                for (int k = 0; k < 2; ++k) {
                    readback_velocities_[alpha_p[k]] = dir * alpha_speed;
                    readback_energies_[alpha_p[k]] = alpha_ebuf;
                    particles.orbital_parent[alpha_p[k]] = -1;
                    readback_velocities_[alpha_n[k]] = dir * alpha_speed;
                    readback_energies_[alpha_n[k]] = alpha_ebuf;
                    particles.orbital_parent[alpha_n[k]] = -1;
                }
                // Recoil on remaining nucleus: p_recoil = p_alpha (momentum conservation)
                float recoil_speed = ke_to_speed(ALPHA_KE * 4.0f / static_cast<float>(std::max(1, nuc.Z + nuc.N)), PROTON_TYPE);
                if (!nuc.proton_indices.empty()) {
                    for (uint32_t pi : nuc.proton_indices)
                        readback_velocities_[pi] += -dir * recoil_speed;
                    for (uint32_t ni : nuc.neutron_indices)
                        readback_velocities_[ni] += -dir * recoil_speed;
                }
                nuc.Z -= 2; nuc.N -= 2;
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                achievements.total_alpha_decays++;
                try_unlock(ACH_FIRST_ALPHA_DECAY);
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 4;
                    snprintf(msg, sizeof(msg), "\xce\xb1 Decay: %s-%d \xe2\x86\x92 %s-%d + He-4",
                             element_symbol(nuc.Z + 2), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                    char alpha_detail[512];
                    snprintf(alpha_detail, sizeof(alpha_detail),
                        "Nucleus: rep #%u, Z=%d N=%d\nDaughter: Z=%d N=%d\nAlpha KE: %.2f MeV (~%.2f MeV/nucleon)\nRecoil speed: %.4f",
                        nuc.rep, nuc.Z + 2, nuc.N + 2,
                        nuc.Z, nuc.N,
                        ALPHA_KE, ALPHA_KE / 4.0f, recoil_speed);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(1.0f, 0.5f, 0.3f, 1.0f), std::string(alpha_detail));
                }
                break;
            }

            case NDECAY_BETA_MINUS: {
                // Convert neutron → proton, emit electron + antineutrino
                // Q = mn - mp - me = 939.565 - 938.272 - 0.511 = 0.782 MeV
                if (nuc.neutron_indices.empty()) break;
                uint32_t ni = nuc.neutron_indices.back();
                nuc.neutron_indices.pop_back();

                float Q_beta = 0.782f;  // MeV (free neutron Q-value)
                // Transmute neutron to proton
                write_spawn_genome(particles, ni, PROTON_TYPE, rng, frame_counter_);
                readback_energies_[ni] = mev_to_ebuf(PHYS_REST_MASS_MEV[PROTON_TYPE]);
                nuc.proton_indices.push_back(ni);
                nuc.Z++; nuc.N--;

                // Spawn electron — gets ~1/3 of Q (beta spectrum average)
                uint32_t e_slot = find_dormant(ni + 1);
                if (e_slot != UINT32_MAX) {
                    float KE_e = Q_beta * 0.33f;
                    write_spawn_genome(particles, e_slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[e_slot] = readback_positions_[ni];
                    readback_velocities_[e_slot] = dir * ke_to_speed(KE_e, ELECTRON_TYPE_PHYS);
                    readback_energies_[e_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS] + KE_e);
                }
                // Spawn antineutrino — gets remaining ~2/3 of Q
                uint32_t nu_slot = find_dormant(e_slot != UINT32_MAX ? e_slot + 1 : ni + 1);
                if (nu_slot != UINT32_MAX) {
                    float KE_nu = Q_beta * 0.67f;
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = readback_positions_[ni];
                    readback_velocities_[nu_slot] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu_slot] = mev_to_ebuf(KE_nu);
                }
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                achievements.total_beta_decays++;
                try_unlock(ACH_FIRST_BETA_DECAY);
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "\xce\xb2\xe2\x81\xbb Decay: %s-%d \xe2\x86\x92 %s-%d + e\xe2\x81\xbb",
                             element_symbol(nuc.Z - 1), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                    char bminus_detail[512];
                    snprintf(bminus_detail, sizeof(bminus_detail),
                        "Nucleus: rep #%u, Z=%d->%d N=%d->%d\nn->p transmutation\nQ=%.3f MeV\ne- slot %u KE: %.3f MeV\nnu-bar slot %u E: %.3f MeV",
                        nuc.rep, nuc.Z - 1, nuc.Z, nuc.N + 1, nuc.N,
                        Q_beta, e_slot, Q_beta * 0.33f, nu_slot, Q_beta * 0.67f);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(0.5f, 0.8f, 1.0f, 1.0f), std::string(bminus_detail));
                }
                break;
            }

            case NDECAY_BETA_PLUS: {
                // Convert proton → neutron, emit positron + neutrino
                // Q = mp - mn - me + nuclear binding difference (typically ~1-5 MeV in nuclei)
                if (nuc.proton_indices.empty()) break;
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();

                float Q_beta_plus = 1.5f;  // MeV (typical nuclear β+ Q-value)
                // Transmute proton to neutron
                write_spawn_genome(particles, pi, NEUTRON_TYPE, rng, frame_counter_);
                readback_energies_[pi] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE]);
                nuc.neutron_indices.push_back(pi);
                nuc.Z--; nuc.N++;

                // Spawn positron
                uint32_t pos_slot = find_dormant(pi + 1);
                if (pos_slot != UINT32_MAX) {
                    float KE_e = Q_beta_plus * 0.33f;
                    write_spawn_genome(particles, pos_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[pos_slot] = readback_positions_[pi];
                    readback_velocities_[pos_slot] = dir * ke_to_speed(KE_e, POSITRON_TYPE_PHYS);
                    readback_energies_[pos_slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[POSITRON_TYPE_PHYS] + KE_e);
                }
                // Spawn neutrino
                uint32_t nu_slot = find_dormant(pos_slot != UINT32_MAX ? pos_slot + 1 : pi + 1);
                if (nu_slot != UINT32_MAX) {
                    float KE_nu = Q_beta_plus * 0.67f;
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = readback_positions_[pi];
                    readback_velocities_[nu_slot] = -dir * C_SIM * 0.9999f;
                    readback_energies_[nu_slot] = mev_to_ebuf(KE_nu);
                }
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                achievements.total_beta_decays++;
                try_unlock(ACH_FIRST_BETA_DECAY);
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "\xce\xb2\xe2\x81\xba Decay: %s-%d \xe2\x86\x92 %s-%d + e\xe2\x81\xba",
                             element_symbol(nuc.Z + 1), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                    char bplus_detail[512];
                    snprintf(bplus_detail, sizeof(bplus_detail),
                        "Nucleus: rep #%u, Z=%d->%d N=%d->%d\np->n transmutation\nQ=%.3f MeV\ne+ slot %u KE: %.3f MeV\nnu slot %u E: %.3f MeV",
                        nuc.rep, nuc.Z + 1, nuc.Z, nuc.N - 1, nuc.N,
                        Q_beta_plus, pos_slot, Q_beta_plus * 0.33f, nu_slot, Q_beta_plus * 0.67f);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(0.5f, 0.8f, 1.0f, 1.0f), std::string(bplus_detail));
                }
                break;
            }

            case NDECAY_NEUTRON_EMISSION: {
                // Eject one neutron from nucleus — ~2 MeV KE
                if (nuc.neutron_indices.empty()) break;
                uint32_t ni = nuc.neutron_indices.back();
                nuc.neutron_indices.pop_back();
                readback_velocities_[ni] = dir * ke_to_speed(NUCLEON_EMIT_KE, NEUTRON_TYPE);
                readback_energies_[ni] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE] + NUCLEON_EMIT_KE);
                particles.orbital_parent[ni] = -1;
                nuc.N--;
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 1;
                    snprintf(msg, sizeof(msg), "n Emission: %s-%d \xe2\x86\x92 %s-%d + n",
                             element_symbol(nuc.Z), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
                    char nemit_detail[512];
                    snprintf(nemit_detail, sizeof(nemit_detail),
                        "Nucleus: rep #%u, Z=%d N=%d->%d\nNeutron #%u ejected\nNeutron KE: %.2f MeV\nDaughter A=%d",
                        nuc.rep, nuc.Z, nuc.N + 1, nuc.N,
                        ni, NUCLEON_EMIT_KE, nuc.Z + nuc.N);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(0.7f, 0.7f, 1.0f, 1.0f), std::string(nemit_detail));
                }
                break;
            }

            case NDECAY_PROTON_EMISSION: {
                // Eject one proton from nucleus — ~2 MeV KE
                if (nuc.proton_indices.empty()) break;
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();
                readback_velocities_[pi] = dir * ke_to_speed(NUCLEON_EMIT_KE, PROTON_TYPE);
                readback_energies_[pi] = mev_to_ebuf(PHYS_REST_MASS_MEV[PROTON_TYPE] + NUCLEON_EMIT_KE);
                particles.orbital_parent[pi] = -1;
                nuc.Z--;
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 1;
                    snprintf(msg, sizeof(msg), "p Emission: %s-%d \xe2\x86\x92 %s-%d + p",
                             element_symbol(nuc.Z + 1), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
                    char pemit_detail[512];
                    snprintf(pemit_detail, sizeof(pemit_detail),
                        "Nucleus: rep #%u, Z=%d->%d N=%d\nProton #%u ejected\nProton KE: %.2f MeV\nDaughter A=%d",
                        nuc.rep, nuc.Z + 1, nuc.Z, nuc.N,
                        pi, NUCLEON_EMIT_KE, nuc.Z + nuc.N);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(0.7f, 0.7f, 1.0f, 1.0f), std::string(pemit_detail));
                }
                break;
            }

            case NDECAY_GAMMA: {
                // Excited nuclear state → ground state + γ photon
                // Typical nuclear gamma: 0.1–3 MeV
                float E_gamma = 0.662f;  // MeV — Ba-137m typical (Cs-137 daughter)
                if (nuc.Z == 43 && nuc.N == 56) E_gamma = 0.140f;  // Tc-99m
                uint32_t g_slot = find_dormant(0);
                if (g_slot != UINT32_MAX) {
                    write_spawn_genome(particles, g_slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[g_slot] = nuc.center;
                    readback_velocities_[g_slot] = dir * C_SIM;
                    readback_energies_[g_slot] = mev_to_ebuf(E_gamma);
                }
                // Nucleus recoil is negligible for gamma emission (p=E/c, tiny for MeV photon vs GeV nucleus)
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "\xce\xb3 Decay: %s-%d* \xe2\x86\x92 %s-%d + \xce\xb3",
                             element_symbol(nuc.Z), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
                    char gamma_detail[512];
                    snprintf(gamma_detail, sizeof(gamma_detail),
                        "Nucleus: rep #%u, Z=%d N=%d (A=%d)\nGamma E: %.3f MeV\nPhoton slot: %u\nNuclear recoil: negligible",
                        nuc.rep, nuc.Z, nuc.N, A,
                        E_gamma, g_slot);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(1.0f, 1.0f, 0.4f, 1.0f), std::string(gamma_detail));
                }
                break;
            }

            case NDECAY_ELECTRON_CAPTURE: {
                // p + e⁻ (orbital) → n + νₑ
                // Captures an orbital electron, converts proton to neutron
                if (nuc.proton_indices.empty()) break;

                // Find nearest orbital electron to this nucleus
                uint32_t best_e = UINT32_MAX;
                float best_dist_sq = 900.0f;  // 30px search radius squared
                for (uint32_t j = 0; j < n; ++j) {
                    if (particles.types[j] != ELECTRON_TYPE_PHYS) continue;
                    if (readback_energies_[j] < 0.01f) continue;
                    glm::vec2 delta = (readback_positions_[j] - nuc.center);
                    float d2 = delta.x * delta.x + delta.y * delta.y;
                    if (d2 < best_dist_sq) {
                        best_dist_sq = d2;
                        best_e = j;
                    }
                }
                if (best_e == UINT32_MAX) break;  // no nearby electron to capture

                // Capture the electron (kill it)
                readback_energies_[best_e] = 0.0f;
                readback_velocities_[best_e] = glm::vec2(0.0f);
                particles.orbital_parent[best_e] = -1;
                particles.orbital_shell[best_e] = -1;
                particles.excitation_timer[best_e] = 0;

                // Transmute proton → neutron
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();
                write_spawn_genome(particles, pi, NEUTRON_TYPE, rng, frame_counter_);
                readback_energies_[pi] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE]);
                nuc.neutron_indices.push_back(pi);
                nuc.Z--; nuc.N++;

                // Emit neutrino carrying the Q-value (~0.86 MeV for Be-7)
                float Q_ec = 0.86f;
                uint32_t nu_slot = find_dormant(0);
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = nuc.center;
                    readback_velocities_[nu_slot] = dir * C_SIM * 0.9999f;
                    readback_energies_[nu_slot] = mev_to_ebuf(Q_ec);
                }
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "EC: %s-%d + e\xe2\x81\xbb \xe2\x86\x92 %s-%d + \xce\xbd",
                             element_symbol(nuc.Z + 1), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(0.6f, 1.0f, 0.8f, 1.0f));
                    char ec_detail[512];
                    snprintf(ec_detail, sizeof(ec_detail),
                        "Nucleus: rep #%u, Z=%d->%d N=%d->%d\nCaptured e- #%u\nDist^2: %.1f px^2\nQ_EC: %.3f MeV\nNeutrino slot %u E: %.3f MeV",
                        nuc.rep, nuc.Z + 1, nuc.Z, nuc.N - 1, nuc.N,
                        best_e, best_dist_sq,
                        Q_ec, nu_slot, Q_ec);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(0.6f, 1.0f, 0.8f, 1.0f), std::string(ec_detail));
                }
                break;
            }

            case NDECAY_SPONTANEOUS_FISSION: {
                // Heavy nucleus splits into two roughly equal fragments + 2-3 free neutrons
                // Typical: Cf-252 → Ba + Mo + 3n, ~200 MeV total KE
                int A = nuc.Z + nuc.N;
                if (A < 10) break;  // too small to fission

                // Split roughly 60/40 (asymmetric fission is more common than symmetric)
                int Z1 = static_cast<int>(nuc.Z * 0.58f + 0.5f);
                int N1 = static_cast<int>(nuc.N * 0.55f + 0.5f);
                int free_neutrons = std::min(3, nuc.N - N1 - (nuc.N - N1) / 2);
                if (free_neutrons < 0) free_neutrons = 0;
                int Z2 = nuc.Z - Z1;
                int N2 = nuc.N - N1 - free_neutrons;
                if (Z2 < 1) { Z2 = 1; Z1 = nuc.Z - 1; }
                if (N2 < 0) { N2 = 0; free_neutrons = nuc.N - N1; }

                // Fragment 1 gets Z1 protons + N1 neutrons (stays in place)
                // Fragment 2 gets Z2 protons + N2 neutrons (ejected)
                // Free neutrons ejected in random directions
                float fission_KE = 200.0f;  // MeV total (typical for actinide fission)
                float frag_KE = fission_KE * 0.85f * 0.5f;  // 85% to fragments
                float neutron_KE = fission_KE * 0.15f / std::max(1, free_neutrons);

                // Assign fragment 2 particles: eject last Z2 protons + N2 neutrons
                int protons_to_eject = std::min(Z2, static_cast<int>(nuc.proton_indices.size()));
                int neutrons_to_eject = std::min(N2 + free_neutrons, static_cast<int>(nuc.neutron_indices.size()));
                float frag2_speed = ke_to_speed(frag_KE / std::max(1, Z2 + N2), PROTON_TYPE);

                // Eject fragment 2 protons
                for (int k = 0; k < protons_to_eject; ++k) {
                    uint32_t pi = nuc.proton_indices.back();
                    nuc.proton_indices.pop_back();
                    readback_velocities_[pi] = dir * frag2_speed;
                    readback_energies_[pi] = mev_to_ebuf(PHYS_REST_MASS_MEV[PROTON_TYPE] + frag_KE / std::max(1, Z2 + N2));
                    particles.orbital_parent[pi] = -1;
                }
                // Eject fragment 2 neutrons + free neutrons
                int neutrons_ejected = 0;
                for (int k = 0; k < neutrons_to_eject; ++k) {
                    if (nuc.neutron_indices.empty()) break;
                    uint32_t ni = nuc.neutron_indices.back();
                    nuc.neutron_indices.pop_back();
                    if (neutrons_ejected < N2) {
                        // Fragment 2 neutron
                        readback_velocities_[ni] = dir * frag2_speed;
                        readback_energies_[ni] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE] + frag_KE / std::max(1, Z2 + N2));
                    } else {
                        // Free prompt neutron — random direction, higher KE
                        glm::vec2 ndir = rand_dir();
                        readback_velocities_[ni] = ndir * ke_to_speed(neutron_KE, NEUTRON_TYPE);
                        readback_energies_[ni] = mev_to_ebuf(PHYS_REST_MASS_MEV[NEUTRON_TYPE] + neutron_KE);
                    }
                    particles.orbital_parent[ni] = -1;
                    neutrons_ejected++;
                }

                // Recoil on fragment 1
                float recoil_speed = ke_to_speed(frag_KE / std::max(1, Z1 + N1), PROTON_TYPE);
                for (uint32_t pi : nuc.proton_indices)
                    readback_velocities_[pi] += -dir * recoil_speed;
                for (uint32_t ni : nuc.neutron_indices)
                    readback_velocities_[ni] += -dir * recoil_speed;

                nuc.Z = Z1; nuc.N = N1;
                any_decayed = true;
                nuclear_decay_count_++;
                achievements.total_nuclear_decays++;
                {
                    char msg[128];
                    int A_parent = Z1 + N1 + Z2 + N2 + free_neutrons;
                    snprintf(msg, sizeof(msg), "Fission: %s-%d \xe2\x86\x92 %s-%d + %s-%d + %dn",
                             element_symbol(Z1 + Z2), A_parent,
                             element_symbol(Z1), Z1 + N1,
                             element_symbol(Z2), Z2 + N2,
                             free_neutrons);
                    iface.push_notification(msg, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    char spfiss_detail[512];
                    snprintf(spfiss_detail, sizeof(spfiss_detail),
                        "Nucleus: rep #%u, A=%d (Z=%d+Z=%d)\nFragment1: Z=%d N=%d\nFragment2: Z=%d N=%d\nFree neutrons: %d\nTotal KE: %.0f MeV (frags) + %.1f MeV (neutrons)",
                        nuc.rep, A_parent, Z1, Z2,
                        Z1, N1, Z2, N2,
                        free_neutrons,
                        fission_KE * 0.85f, fission_KE * 0.15f);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_NUCLEAR_DECAY, ImVec4(1.0f, 0.4f, 0.4f, 1.0f), std::string(spfiss_detail));
                }
                break;
            }

            default:
                break;
        }
    }

    if (any_decayed) {
        cpu_particles_dirty_ = true;
    }
}

// ── Photoelectric effect & Compton scattering ────────────────────────────────
// High-energy photons interact with bound electrons:
//   - Photoelectric: photon absorbed, electron ionized (ejected from orbit)
//   - Compton: photon partially transfers energy, electron kicked to higher shell or out
// Also handles general photon-electron energy transfer for free electrons.

void PhysicsSimulation::check_photoelectric() {
    if (!cfg.compton_enabled) return;
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float INTERACTION_RADIUS = cfg.compton_radius;
    const float INTERACTION_RADIUS_SQ = INTERACTION_RADIUS * INTERACTION_RADIUS;

    // Binding energy per shell (game units): how much photon energy needed to ionize
    // Shell 1 (1s) is tightest, shell 4 (4s4p4d4f) loosest
    const float BINDING_ENERGY[] = {0.50f, 0.30f, 0.15f, 0.08f};  // indexed by shell

    const int MAX_INTERACTIONS_PER_FRAME = cfg.max_compton_per_frame;
    int interaction_count = 0;
    bool any_changed = false;

    std::mt19937 rng(frame_counter_ * 314159265u + 1);
    std::vector<bool> used(n, false);

    auto rand_dir = [&]() -> glm::vec2 {
        float angle = angle_dist_(rng);
        return glm::vec2(std::cos(angle), std::sin(angle));
    };

    // Scan all photons for interactions with bound electrons
    for (uint32_t i = 0; i < n && interaction_count < MAX_INTERACTIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (particles.types[i] != PHOTON_TYPE_PHYS) continue;
        float ph_energy = readback_energies_[i];
        if (ph_energy < 0.15f) continue;  // too low energy for meaningful interaction

        float ph_speed = glm::length(readback_velocities_[i]);
        if (ph_speed < 10.0f) continue;

        // Find nearest bound electron/positron within interaction radius
        float best_dist_sq = INTERACTION_RADIUS_SQ;
        uint32_t best_e = UINT32_MAX;
        auto pe_search = [&](uint32_t j) {
            if (j == i || used[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            uint32_t t = particles.types[j];
            if (t != ELECTRON_TYPE_PHYS && t != POSITRON_TYPE_PHYS) return;
            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_e = j;
            }
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, INTERACTION_RADIUS, pe_search);
        else
            for (uint32_t j = 0; j < n; ++j) pe_search(j);
        if (best_e == UINT32_MAX) continue;

        // Determine if the electron is bound (has orbital parent)
        int32_t orbital_parent = -1;
        if (best_e < particles.orbital_parent.size())
            orbital_parent = particles.orbital_parent[best_e];

        bool is_bound = (orbital_parent >= 0);

        if (is_bound) {
            // ── Bound electron: photoelectric effect / Compton ──

            // Use persistent shell assignment from update_orbitals()
            int shell = (best_e < particles.orbital_shell.size())
                ? static_cast<int>(particles.orbital_shell[best_e]) : -1;
            if (shell < 0 || shell > 3) shell = 0;  // fallback

            int Z_nucleus = 0;
            for (const auto& nuc : detected_nuclei_) {
                if (static_cast<int32_t>(nuc.rep) == orbital_parent) {
                    Z_nucleus = nuc.Z;
                    break;
                }
            }

            // Scale binding energy by Z (heavier atoms bind tighter)
            float Z_factor = std::max(1.0f, static_cast<float>(Z_nucleus));
            float binding = BINDING_ENERGY[shell] * std::sqrt(Z_factor);

            if (ph_energy >= binding * 1.5f) {
                // ── PHOTOELECTRIC EFFECT: photon fully absorbed, electron ionized ──
                // Electron gets ejected with kinetic energy = photon_energy - binding_energy
                float kick_energy = ph_energy - binding;

                // Absorb photon (kill it)
                used[i] = true;
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);

                // Eject electron: direction = away from nucleus
                glm::vec2 eject_dir = rand_dir();
                if (orbital_parent >= 0 && static_cast<uint32_t>(orbital_parent) < n) {
                    glm::vec2 to_electron = (readback_positions_[best_e] - readback_positions_[orbital_parent]);
                    float len = glm::length(to_electron);
                    if (len > 0.1f) eject_dir = to_electron / len;
                }

                float eject_speed = std::min(std::sqrt(kick_energy) * 80.0f, 250.0f);
                readback_velocities_[best_e] = eject_dir * eject_speed;
                readback_energies_[best_e] = std::min(readback_energies_[best_e] + kick_energy, 1.0f);
                particles.orbital_parent[best_e] = -1;  // ionized — no longer bound
                particles.orbital_shell[best_e] = -1;
                particles.excitation_timer[best_e] = 0;
                particles.genomes[best_e * GENOME_SIZE + 2] = 0.0f;  // clear orbital L

                used[best_e] = true;
                any_changed = true;
                interaction_count++;
                try_unlock(ACH_FIRST_PHOTOELECTRIC);
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Photon #%u E: %.4f\nTarget e- #%u (shell %d, Z_nuc=%d)\nBinding E: %.4f\nKick E: %.4f\nEject speed: %.2f",
                        i, ph_energy, best_e, shell, Z_nucleus,
                        binding, kick_energy, glm::length(readback_velocities_[best_e]));
                    iface.push_notification("Photoelectric: \xce\xb3 absorbed, e\xe2\x81\xbb ionized", ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
                    iface.push_decay_event("Photoelectric: \xce\xb3 absorbed, e\xe2\x81\xbb ionized", PhysicsInterface::DEVT_PHOTOELECTRIC, ImVec4(0.3f, 0.7f, 1.0f, 1.0f), std::string(detail));
                }

            } else if (ph_energy >= binding * 0.6f) {
                // ── COMPTON SCATTERING: partial energy transfer ──
                // Photon deflected with reduced energy, electron boosted
                float transfer = ph_energy * 0.4f;  // 40% of photon energy transferred

                // Reduce photon energy and deflect
                readback_energies_[i] = std::max(ph_energy - transfer, 0.05f);
                glm::vec2 deflect = rand_dir();
                readback_velocities_[i] = deflect * C_SIM;

                // Boost electron: if transfer > binding, ionize; otherwise promote to higher shell
                if (transfer >= binding) {
                    // Ionize
                    glm::vec2 kick_dir = glm::normalize((readback_positions_[best_e] - readback_positions_[i]) + glm::vec2(0.001f));
                    float eject_speed = std::min(std::sqrt(transfer - binding) * 60.0f, 200.0f);
                    readback_velocities_[best_e] = kick_dir * eject_speed;
                    readback_energies_[best_e] = std::min(readback_energies_[best_e] + transfer, 1.0f);
                    particles.orbital_parent[best_e] = -1;
                    particles.orbital_shell[best_e] = -1;
                    particles.excitation_timer[best_e] = 0;
                    particles.genomes[best_e * GENOME_SIZE + 2] = 0.0f;
                } else {
                    // Excite: promote electron to next shell
                    int next_shell = shell + 1;
                    if (next_shell < 4 && best_e < particles.orbital_shell.size()) {
                        particles.orbital_shell[best_e] = static_cast<int8_t>(next_shell);
                        particles.excitation_timer[best_e] = 1;
                        // L_ground will be recomputed by update_orbitals() next frame
                    }
                    readback_energies_[best_e] = std::min(readback_energies_[best_e] + transfer * 0.5f, 1.0f);
                }

                used[i] = true;
                used[best_e] = true;
                any_changed = true;
                interaction_count++;
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Photon #%u E: %.4f -> %.4f\nTarget e- #%u (shell %d, Z_nuc=%d)\nEnergy transfer: %.4f\nBinding E: %.4f\nScattered photon speed: %.2f",
                        i, ph_energy, readback_energies_[i],
                        best_e, shell, Z_nucleus,
                        ph_energy * 0.4f, binding,
                        glm::length(readback_velocities_[i]));
                    iface.push_notification("Compton: \xce\xb3 scattered off bound e\xe2\x81\xbb", ImVec4(0.4f, 0.7f, 0.9f, 1.0f));
                    iface.push_decay_event("Compton: \xce\xb3 scattered off bound e\xe2\x81\xbb", PhysicsInterface::DEVT_PHOTOELECTRIC, ImVec4(0.4f, 0.7f, 0.9f, 1.0f), std::string(detail));
                }
            }
            // else: photon too weak, passes through (handled by shader deflection)

        } else {
            // ── Free electron: Compton scattering / energy transfer ──
            // High-energy photon transfers momentum to free electron
            if (ph_energy >= 0.3f) {
                float transfer = ph_energy * 0.3f;

                // Photon loses energy and deflects
                glm::vec2 orig_ph_dir = glm::normalize(readback_velocities_[i] + glm::vec2(0.001f));
                readback_energies_[i] = std::max(ph_energy - transfer, 0.05f);
                readback_velocities_[i] = rand_dir() * C_SIM;

                // Electron gains momentum in photon's original direction
                readback_velocities_[best_e] += orig_ph_dir * transfer * 100.0f;
                readback_energies_[best_e] = std::min(readback_energies_[best_e] + transfer, 1.0f);

                used[i] = true;
                used[best_e] = true;
                any_changed = true;
                interaction_count++;
                {
                    char detail[512];
                    snprintf(detail, sizeof(detail),
                        "Photon #%u E: %.4f -> %.4f\nFree e- #%u (unbound)\nEnergy transfer: %.4f\ne- energy after: %.4f",
                        i, ph_energy, readback_energies_[i],
                        best_e, transfer, readback_energies_[best_e]);
                    iface.push_notification("Free e\xe2\x81\xbb Compton scatter", ImVec4(0.5f, 0.6f, 0.8f, 1.0f));
                    iface.push_decay_event("Free e\xe2\x81\xbb Compton scatter", PhysicsInterface::DEVT_PHOTOELECTRIC, ImVec4(0.5f, 0.6f, 0.8f, 1.0f), std::string(detail));
                }
            }
        }
    }

    // ── Nuclear Compton scattering: photon + free nucleon ──
    // Photon scatters off a free proton or neutron, transferring momentum.
    // Lower threshold than photodisintegration — just elastic scattering.
    for (uint32_t i = 0; i < n && interaction_count < MAX_INTERACTIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (particles.types[i] != PHOTON_TYPE_PHYS) continue;
        float ph_energy = readback_energies_[i];
        if (ph_energy < 0.25f) continue;

        float ph_speed = glm::length(readback_velocities_[i]);
        if (ph_speed < 10.0f) continue;
        glm::vec2 ph_dir = readback_velocities_[i] / ph_speed;

        // Find nearest free nucleon (not part of a nucleus)
        float best_dist_sq = INTERACTION_RADIUS_SQ;
        uint32_t best_nuc = UINT32_MAX;
        auto nuc_compton_search = [&](uint32_t j) {
            if (j == i || used[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            uint32_t t = particles.types[j];
            if (t != PROTON_TYPE && t != NEUTRON_TYPE && t != ANTIPROTON_TYPE_PHYS) return;
            if (j < particles.orbital_parent.size() && particles.orbital_parent[j] >= 0 &&
                particles.orbital_parent[j] != static_cast<int32_t>(j)) return;
            glm::vec2 delta = (readback_positions_[j] - readback_positions_[i]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_nuc = j;
            }
        };
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, INTERACTION_RADIUS, nuc_compton_search);
        else
            for (uint32_t j = 0; j < n; ++j) nuc_compton_search(j);
        if (best_nuc == UINT32_MAX) continue;

        // Nuclear Compton: photon deflects, nucleon gets momentum kick
        // Much less energy transfer than electron Compton (nucleon is ~2000x heavier)
        float transfer = ph_energy * 0.08f;  // small fraction due to heavy target

        readback_energies_[i] = std::max(ph_energy - transfer, 0.05f);
        readback_velocities_[i] = rand_dir() * C_SIM;

        // Nucleon recoil in photon's original direction
        readback_velocities_[best_nuc] += ph_dir * transfer * 15.0f;
        readback_energies_[best_nuc] = std::min(readback_energies_[best_nuc] + transfer * 0.3f, 1.0f);

        used[i] = true;
        used[best_nuc] = true;
        any_changed = true;
        interaction_count++;
        {
            char detail[512];
            snprintf(detail, sizeof(detail),
                "Photon #%u E: %.4f -> %.4f\nNucleon #%u type=%u\nEnergy transfer: %.4f (8%% of photon)\nNucleon recoil energy: %.4f",
                i, ph_energy, readback_energies_[i],
                best_nuc, particles.types[best_nuc],
                transfer, readback_energies_[best_nuc]);
            iface.push_notification("Nuclear Compton: \xce\xb3 + N scatter", ImVec4(0.6f, 0.5f, 0.9f, 1.0f));
            iface.push_decay_event("Nuclear Compton: \xce\xb3 + N scatter", PhysicsInterface::DEVT_PHOTOELECTRIC, ImVec4(0.6f, 0.5f, 0.9f, 1.0f), std::string(detail));
        }
    }

    if (any_changed) {
        cpu_particles_dirty_ = true;
    }
}

// ── Pion decay ─────────────────────────────────────────────────────────────
// Detects quark-antiquark meson pairs linked via entangled_partner and
// decays them:  π⁺(ud̄) → μ⁺ + νμ,  π⁻(ūd) → μ⁻ + νμ,  π⁰(uū/dd̄) → γγ
// Completes the spallation cascade: spallation → pion → muon → electron

void PhysicsSimulation::check_pion_decay() {
    if (readback_positions_.empty()) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0) return;

    std::mt19937 rng(frame_counter_ * 1737350767u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    constexpr float PION_CHARGED_MASS = 139.6f;   // MeV
    constexpr float PION_NEUTRAL_MASS = 135.0f;    // MeV
    constexpr float MUON_MASS         = 105.658f;  // MeV
    constexpr float CONFINEMENT_RADIUS = 25.0f;    // px — max distance for bound pair
    constexpr float CONFINEMENT_R2     = CONFINEMENT_RADIUS * CONFINEMENT_RADIUS;
    constexpr uint32_t MIN_AGE_FRAMES  = 8;        // let QCD bind them first
    constexpr float CHARGED_DECAY_PROB = 0.03f;    // ~30 frame mean lifetime
    constexpr float NEUTRAL_DECAY_PROB = 0.15f;    // ~6 frame mean lifetime

    // Cascade tag meanings:
    // 0 = no cascade, 1 = photopion origin, 2 = VMD origin, 3 = pion decay product
    auto cascade_origin_label = [&](uint32_t slot) -> const char* {
        if (slot >= particles.cascade_tag.size()) return "";
        switch (particles.cascade_tag[slot]) {
            case 1: return "photopion";
            case 2: return "VMD";
            default: return "";
        }
    };

    int decays = 0;

    for (uint32_t i = 0; i < n && decays < 4; ++i) {
        if (i >= particles.entangled_partner.size()) break;
        uint32_t j = particles.entangled_partner[i];
        if (j == UINT32_MAX || j >= n) continue;
        if (i > j) continue;  // process each pair once (lower index only)

        // Validate reciprocal link
        if (j >= particles.entangled_partner.size()) continue;
        if (particles.entangled_partner[j] != i) continue;

        // Both must be alive
        if (readback_energies_[i] < 0.01f || readback_energies_[j] < 0.01f) continue;

        uint32_t ti = particles.types[i];
        uint32_t tj = particles.types[j];

        // Identify pion flavor from quark content
        // π⁺ = u(13) + d̄(20),  π⁻ = ū(19) + d(14)
        // π⁰ = u(13) + ū(19)  or  d(14) + d̄(20)
        enum PionType { PION_NONE, PION_PLUS, PION_MINUS, PION_ZERO };
        PionType pion = PION_NONE;

        if ((ti == UP_QUARK_TYPE && tj == ANTI_DOWN_TYPE) ||
            (tj == UP_QUARK_TYPE && ti == ANTI_DOWN_TYPE))
            pion = PION_PLUS;
        else if ((ti == ANTI_UP_TYPE && tj == DOWN_QUARK_TYPE) ||
                 (tj == ANTI_UP_TYPE && ti == DOWN_QUARK_TYPE))
            pion = PION_MINUS;
        else if ((ti == UP_QUARK_TYPE && tj == ANTI_UP_TYPE) ||
                 (tj == UP_QUARK_TYPE && ti == ANTI_UP_TYPE) ||
                 (ti == DOWN_QUARK_TYPE && tj == ANTI_DOWN_TYPE) ||
                 (tj == DOWN_QUARK_TYPE && ti == ANTI_DOWN_TYPE))
            pion = PION_ZERO;

        if (pion == PION_NONE) continue;

        // Check proximity (must be within confinement radius)
        glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
        float d2 = delta.x * delta.x + delta.y * delta.y;
        if (d2 > CONFINEMENT_R2) continue;

        // Check minimum age
        if (i < particles.birth_frames.size() &&
            (frame_counter_ - particles.birth_frames[i]) < MIN_AGE_FRAMES) continue;
        if (j < particles.birth_frames.size() &&
            (frame_counter_ - particles.birth_frames[j]) < MIN_AGE_FRAMES) continue;

        // Stochastic decay probability
        float prob = (pion == PION_ZERO) ? NEUTRAL_DECAY_PROB : CHARGED_DECAY_PROB;
        if (unit(rng) > prob) continue;

        // ── Decay! ──
        glm::vec2 cm_pos = (readback_positions_[i] + readback_positions_[j]) * 0.5f;

        // Random decay direction
        float angle = unit(rng) * 6.2831853f;
        glm::vec2 decay_dir(std::cos(angle), std::sin(angle));

        // Cascade chain context from quark origin
        const char* origin = cascade_origin_label(i);
        bool has_origin = (origin[0] != '\0');

        if (pion == PION_PLUS) {
            // π⁺ → μ⁺ + νμ
            float p_dec = two_body_decay_momentum(PION_CHARGED_MASS, MUON_MASS, 0.0f);
            float KE_mu = std::sqrt(p_dec * p_dec + MUON_MASS * MUON_MASS) - MUON_MASS;
            float KE_nu = p_dec;  // massless neutrino: E = pc

            write_spawn_genome(particles, i, ANTIMUON_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[i] = cm_pos;
            readback_energies_[i] = mev_to_ebuf(MUON_MASS + KE_mu);
            readback_velocities_[i] = decay_dir * ke_to_speed(KE_mu, ANTIMUON_TYPE_PHYS);

            write_spawn_genome(particles, j, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[j] = cm_pos;
            readback_energies_[j] = mev_to_ebuf(KE_nu);
            readback_velocities_[j] = -decay_dir * C_SIM * 0.9999f;

            // Propagate cascade tag to muon product (step 2: pion decay)
            if (i < particles.cascade_tag.size() && particles.cascade_tag[i] != 0)
                particles.cascade_tag[i] = 3;  // pion decay product
            if (j < particles.cascade_tag.size())
                particles.cascade_tag[j] = 0;  // neutrino — end of tracking

            iface.push_notification("Decay: \xCF\x80\xE2\x81\xBA \xE2\x86\x92 \xCE\xBC\xE2\x81\xBA + \xCE\xBD\xCE\xBC",
                                    ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
            {
                char detail[512];
                int len = snprintf(detail, sizeof(detail),
                    "Pion+ (#%u,#%u) M=%.1f MeV\np_dec=%.1f MeV/c\n\xCE\xBC\xE2\x81\xBA #%u KE: %.1f MeV\n\xCE\xBD\xCE\xBC #%u E: %.1f MeV",
                    i, j, PION_CHARGED_MASS, p_dec, i, KE_mu, j, KE_nu);
                if (has_origin)
                    snprintf(detail + len, sizeof(detail) - len,
                        "\n\xE2\x94\x80 Cascade [%s]: \xCE\xB3+N \xE2\x86\x92 \xCF\x80\xE2\x81\xBA(ud\xCC\x84) \xE2\x86\x92 \xCE\xBC\xE2\x81\xBA+\xCE\xBD\xCE\xBC \xE2\x86\x92 e\xE2\x81\xBA+\xCE\xBD+\xCE\xBD\xCC\x84",
                        origin);
                iface.push_decay_event("\xCF\x80\xE2\x81\xBA \xE2\x86\x92 \xCE\xBC\xE2\x81\xBA + \xCE\xBD\xCE\xBC",
                    PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.6f, 0.3f, 1.0f), std::string(detail));
            }

        } else if (pion == PION_MINUS) {
            // π⁻ → μ⁻ + ν̄μ
            float p_dec = two_body_decay_momentum(PION_CHARGED_MASS, MUON_MASS, 0.0f);
            float KE_mu = std::sqrt(p_dec * p_dec + MUON_MASS * MUON_MASS) - MUON_MASS;
            float KE_nu = p_dec;

            write_spawn_genome(particles, i, MUON_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[i] = cm_pos;
            readback_energies_[i] = mev_to_ebuf(MUON_MASS + KE_mu);
            readback_velocities_[i] = decay_dir * ke_to_speed(KE_mu, MUON_TYPE_PHYS);

            write_spawn_genome(particles, j, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[j] = cm_pos;
            readback_energies_[j] = mev_to_ebuf(KE_nu);
            readback_velocities_[j] = -decay_dir * C_SIM * 0.9999f;

            // Propagate cascade tag to muon product
            if (i < particles.cascade_tag.size() && particles.cascade_tag[i] != 0)
                particles.cascade_tag[i] = 3;
            if (j < particles.cascade_tag.size())
                particles.cascade_tag[j] = 0;

            iface.push_notification("Decay: \xCF\x80\xE2\x81\xBB \xE2\x86\x92 \xCE\xBC\xE2\x81\xBB + \xCE\xBD\xCC\x84\xCE\xBC",
                                    ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
            {
                char detail[512];
                int len = snprintf(detail, sizeof(detail),
                    "Pion- (#%u,#%u) M=%.1f MeV\np_dec=%.1f MeV/c\n\xCE\xBC\xE2\x81\xBB #%u KE: %.1f MeV\n\xCE\xBD\xCC\x84\xCE\xBC #%u E: %.1f MeV",
                    i, j, PION_CHARGED_MASS, p_dec, i, KE_mu, j, KE_nu);
                if (has_origin)
                    snprintf(detail + len, sizeof(detail) - len,
                        "\n\xE2\x94\x80 Cascade [%s]: \xCE\xB3+N \xE2\x86\x92 \xCF\x80\xE2\x81\xBB(\xC5\xAB" "d) \xE2\x86\x92 \xCE\xBC\xE2\x81\xBB+\xCE\xBD\xCC\x84\xCE\xBC \xE2\x86\x92 e\xE2\x81\xBB+\xCE\xBD+\xCE\xBD\xCC\x84",
                        origin);
                iface.push_decay_event("\xCF\x80\xE2\x81\xBB \xE2\x86\x92 \xCE\xBC\xE2\x81\xBB + \xCE\xBD\xCC\x84\xCE\xBC",
                    PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.6f, 0.3f, 1.0f), std::string(detail));
            }

        } else {
            // π⁰ → γ + γ  (back-to-back photons, each ~67.5 MeV)
            float E_gamma = PION_NEUTRAL_MASS * 0.5f;

            write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[i] = cm_pos;
            readback_energies_[i] = mev_to_ebuf(E_gamma);
            readback_velocities_[i] = decay_dir * C_SIM;

            write_spawn_genome(particles, j, PHOTON_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[j] = cm_pos;
            readback_energies_[j] = mev_to_ebuf(E_gamma);
            readback_velocities_[j] = -decay_dir * C_SIM;

            // Clear cascade tags — photons are terminal
            if (i < particles.cascade_tag.size()) particles.cascade_tag[i] = 0;
            if (j < particles.cascade_tag.size()) particles.cascade_tag[j] = 0;

            iface.push_notification("Decay: \xCF\x80\xE2\x81\xB0 \xE2\x86\x92 \xCE\xB3 + \xCE\xB3",
                                    ImVec4(1.0f, 0.9f, 0.3f, 1.0f));
            try_unlock(ACH_PHOTON_EMISSION);
            {
                char detail[512];
                int len = snprintf(detail, sizeof(detail),
                    "Pion0 (#%u,#%u) M=%.1f MeV\n\xCE\xB3 #%u E: %.1f MeV\n\xCE\xB3 #%u E: %.1f MeV",
                    i, j, PION_NEUTRAL_MASS, i, E_gamma, j, E_gamma);
                if (has_origin)
                    snprintf(detail + len, sizeof(detail) - len,
                        "\n\xE2\x94\x80 Cascade [%s]: \xCE\xB3+N \xE2\x86\x92 \xCF\x80\xE2\x81\xB0(q\xC4\x81) \xE2\x86\x92 \xCE\xB3+\xCE\xB3",
                        origin);
                iface.push_decay_event("\xCF\x80\xE2\x81\xB0 \xE2\x86\x92 \xCE\xB3 + \xCE\xB3",
                    PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(1.0f, 0.9f, 0.3f, 1.0f), std::string(detail));
            }
        }

        // Clear meson link — products are independent particles now
        particles.entangled_partner[i] = UINT32_MAX;
        particles.entangled_partner[j] = UINT32_MAX;
        cpu_particles_dirty_ = true;
        ++decays;
        try_unlock(ACH_FIRST_PION_DECAY);
    }
}

// ── Electron shell transitions ──────────────────────────────────────────────
// Handles energy-based promotion (thermal excitation), spontaneous de-excitation
// (with photon emission), and ionization of excited electrons.

void PhysicsSimulation::check_shell_transitions() {
    if (readback_positions_.empty() || detected_nuclei_.empty()) return;

    const uint32_t n = cfg.particle_count;
    std::mt19937 rng(frame_counter_ * 2654435761u + 7);
    static constexpr float BINDING_ENERGY[4] = {0.50f, 0.30f, 0.15f, 0.08f};

    int transition_count = 0;

    // Build per-nucleus shell occupancy from current orbital_shell data
    // Map nucleus rep → index in detected_nuclei_
    std::unordered_map<int32_t, size_t> rep_to_nuc;
    for (size_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        if (!detected_nuclei_[ni].is_anti)
            rep_to_nuc[static_cast<int32_t>(detected_nuclei_[ni].rep)] = ni;
    }

    // Count current shell occupancy per nucleus
    // shell_occ[ni * NUM_SHELLS_O + s] = current electron count in shell s of nucleus ni
    std::vector<int> shell_occ(detected_nuclei_.size() * NUM_SHELLS_O, 0);
    for (uint32_t i = 0; i < n; ++i) {
        if (particles.orbital_shell.size() <= i) break;
        int8_t sh = particles.orbital_shell[i];
        if (sh < 0 || sh >= NUM_SHELLS_O) continue;
        int32_t parent = particles.orbital_parent[i];
        if (parent < 0) continue;
        auto it = rep_to_nuc.find(parent);
        if (it == rep_to_nuc.end()) continue;
        shell_occ[it->second * NUM_SHELLS_O + sh]++;
    }

    // Helper: find a dormant slot for photon emission
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t i = start; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) return i;
        }
        return UINT32_MAX;
    };

    // Helper: recompute L_ground for electron in a given shell of a nucleus
    auto recompute_L = [&](uint32_t idx, int Z, int shell, size_t ni) {
        int inner = 0;
        for (int s = 0; s < shell; ++s)
            inner += shell_occ[ni * NUM_SHELLS_O + s];
        float Z_eff = std::max(1.0f, static_cast<float>(Z - inner));
        float n_shell = static_cast<float>(shell + 1);
        float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
        particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
        particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
    };

    for (uint32_t i = 0; i < n && transition_count < cfg.max_transitions_per_frame; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];
        if (t != ELECTRON_TYPE_PHYS && t != POSITRON_TYPE_PHYS) continue;
        if (i >= particles.orbital_shell.size()) continue;

        int8_t shell = particles.orbital_shell[i];
        if (shell < 0) continue;  // free electron, not bound

        int32_t parent = particles.orbital_parent[i];
        if (parent < 0) continue;

        auto it = rep_to_nuc.find(parent);
        if (it == rep_to_nuc.end()) continue;
        size_t ni = it->second;
        int Z = detected_nuclei_[ni].Z;
        float Z_factor = std::sqrt(std::max(1.0f, static_cast<float>(Z)));

        // ── Spontaneous de-excitation ──
        if (particles.excitation_timer[i] > 0) {
            particles.excitation_timer[i]++;

            if (particles.excitation_timer[i] >= static_cast<uint16_t>(cfg.deexcitation_lifetime)) {
                // Find lowest shell with vacancy
                int target_shell = -1;
                for (int s = 0; s < shell; ++s) {
                    if (shell_occ[ni * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                        target_shell = s;
                        break;
                    }
                }

                if (target_shell >= 0) {
                    // De-excite: move electron down, emit photon
                    float energy_released = (BINDING_ENERGY[shell] - BINDING_ENERGY[target_shell]) * Z_factor;
                    // Note: BINDING_ENERGY is ordered inner=highest, so released = old_shell - new_shell
                    // But since old_shell > target_shell numerically, and binding energy is lower for outer shells:
                    // energy_released = BINDING_ENERGY[target_shell] - BINDING_ENERGY[shell]
                    energy_released = (BINDING_ENERGY[target_shell] - BINDING_ENERGY[shell]) * Z_factor;

                    // Update shell occupancy
                    shell_occ[ni * NUM_SHELLS_O + shell]--;
                    shell_occ[ni * NUM_SHELLS_O + target_shell]++;

                    particles.orbital_shell[i] = static_cast<int8_t>(target_shell);
                    particles.excitation_timer[i] = 0;
                    recompute_L(i, Z, target_shell, ni);

                    // Spawn photon
                    if (energy_released > 0.01f) {
                        uint32_t slot = find_dormant(0);
                        if (slot != UINT32_MAX) {
                            write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                            readback_positions_[slot] = readback_positions_[i];
                            // Tangent direction for momentum conservation
                            glm::vec2 to_nuc = detected_nuclei_[ni].center - readback_positions_[i];
                            float len = glm::length(to_nuc);
                            glm::vec2 tangent = (len > 0.1f)
                                ? glm::vec2(-to_nuc.y / len, to_nuc.x / len)
                                : glm::vec2(1.0f, 0.0f);
                            readback_velocities_[slot] = tangent * C_SIM;
                            readback_energies_[slot] = std::min(energy_released, 1.0f);
                            particles.orbital_shell[slot] = -1;
                            particles.excitation_timer[slot] = 0;
                            cpu_particles_dirty_ = true;
                        }
                    }

                    transition_count++;
                    iface.push_decay_event(
                        "De-excitation: e\xe2\x81\xbb \xe2\x86\x92 lower shell + \xce\xb3",
                        PhysicsInterface::DEVT_PHOTOELECTRIC,
                        ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "");
                    continue;
                }

                // No lower shell available: if at outermost occupied shell and timer >> lifetime, ionize
                if (shell >= 2 && particles.excitation_timer[i] >= static_cast<uint16_t>(cfg.deexcitation_lifetime * 2.0f)) {
                    // Ionize
                    particles.orbital_parent[i] = -1;
                    particles.orbital_shell[i] = -1;
                    particles.excitation_timer[i] = 0;
                    particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
                    particles.genomes[i * GENOME_SIZE + 3] = 0.0f;
                    shell_occ[ni * NUM_SHELLS_O + shell]--;

                    // Radial kick outward
                    glm::vec2 outward = readback_positions_[i] - detected_nuclei_[ni].center;
                    float len = glm::length(outward);
                    if (len > 0.1f) outward /= len;
                    else outward = glm::vec2(1.0f, 0.0f);
                    readback_velocities_[i] += outward * 40.0f;
                    cpu_particles_dirty_ = true;
                    transition_count++;
                    iface.push_decay_event(
                        "Ionization: excited e\xe2\x81\xbb escaped atom",
                        PhysicsInterface::DEVT_PHOTOELECTRIC,
                        ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "");
                    continue;
                }
            }
            continue;  // excited electrons don't also get thermal promotion
        }

        // ── Thermal promotion (ground state → higher shell) ──
        if (shell >= NUM_SHELLS_O - 1) continue;  // already at outermost shell

        // Compute kinetic energy of this electron
        float v_sq = glm::dot(readback_velocities_[i], readback_velocities_[i]);
        // electron mass_inv ≈ 1.0 (lightest), so KE ≈ 0.5 * v²
        float KE = 0.5f * v_sq / (C_SIM * C_SIM);  // normalize to sim energy scale

        int next_shell = shell + 1;
        float delta_E = (BINDING_ENERGY[shell] - BINDING_ENERGY[next_shell]) * Z_factor;

        // Need KE > 1.5× the energy gap to promote (thermal threshold)
        if (KE > delta_E * 1.5f) {
            // Check if next shell has room
            if (shell_occ[ni * NUM_SHELLS_O + next_shell] < SHELL_CAP_O[next_shell]) {
                // Promote
                shell_occ[ni * NUM_SHELLS_O + shell]--;
                shell_occ[ni * NUM_SHELLS_O + next_shell]++;
                particles.orbital_shell[i] = static_cast<int8_t>(next_shell);
                particles.excitation_timer[i] = 1;  // mark as excited
                recompute_L(i, Z, next_shell, ni);

                // Reduce electron speed (it spent energy climbing)
                float speed = std::sqrt(v_sq);
                if (speed > 1.0f) {
                    float new_speed = speed * 0.7f;  // lose ~30% of speed
                    readback_velocities_[i] *= (new_speed / speed);
                }
                cpu_particles_dirty_ = true;
                transition_count++;
                iface.push_decay_event(
                    "Excitation: e\xe2\x81\xbb promoted to higher shell",
                    PhysicsInterface::DEVT_PHOTOELECTRIC,
                    ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "");
            }
        }
    }
}

// ── Nuclear spallation ──────────────────────────────────────────────────────
// When a high-energy particle (any massive particle) strikes a nucleus with
// sufficient kinetic energy, the nucleus shatters into individual nucleon
// fragments. This is different from fission (which requires a fast neutron
// and a minimum cluster size). Spallation works with any projectile type
// and can break apart nuclei of any size.

void PhysicsSimulation::check_spallation() {
    if (!cfg.spallation_enabled) return;
    if (readback_positions_.empty() || detected_nuclei_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float HIT_RADIUS = 10.0f;
    const float HIT_RADIUS_SQ = HIT_RADIUS * HIT_RADIUS;
    const float MIN_PROJECTILE_SPEED = cfg.spallation_min_speed;
    const float MIN_PROJECTILE_ENERGY = cfg.spallation_min_energy;
    const int MIN_NUCLEUS_SIZE = 2;                  // at least deuteron
    const float FRAGMENT_SPEED = 100.0f;
    const float FRAGMENT_ENERGY = 0.6f;
    const int MAX_SPALLATIONS_PER_FRAME = cfg.max_spallations_per_frame;

    int spallation_count = 0;
    bool any_spallated = false;
    std::vector<bool> used(n, false);
    std::mt19937 rng(frame_counter_ * 1618033989u);

    auto rand_dir = [&]() -> glm::vec2 {
        float angle = angle_dist_(rng);
        return glm::vec2(std::cos(angle), std::sin(angle));
    };

    // Identify projectiles: any fast-moving massive particle (not photon/neutrino/gluon)
    for (uint32_t i = 0; i < n && spallation_count < MAX_SPALLATIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < MIN_PROJECTILE_ENERGY) continue;

        uint32_t ptype = particles.types[i];
        // Skip massless particles (they don't cause spallation directly —
        // photonuclear is handled in check_photoelectric via a separate path)
        if (ptype >= PHYS_PARTICLE_TYPES) continue;
        uint32_t bhv = particles.behavior_flags[ptype];
        if (bhv & (BEHAVIOR_PHOTON | BEHAVIOR_GLUON)) continue;
        if (bhv & BEHAVIOR_NEUTRINO) continue;

        float speed = glm::length(readback_velocities_[i]);
        if (speed < MIN_PROJECTILE_SPEED) continue;

        glm::vec2 proj_pos = readback_positions_[i];

        // Check against each detected nucleus
        for (auto& nuc : detected_nuclei_) {
            if (spallation_count >= MAX_SPALLATIONS_PER_FRAME) break;

            int total_nucleons = nuc.Z + static_cast<int>(nuc.neutron_indices.size());
            if (total_nucleons < MIN_NUCLEUS_SIZE) continue;

            // Check if projectile is inside the nucleus
            glm::vec2 delta = (proj_pos - nuc.center);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > HIT_RADIUS_SQ) continue;

            // Don't spall if the projectile IS part of this nucleus
            bool is_constituent = false;
            for (uint32_t pi : nuc.proton_indices) {
                if (pi == i) { is_constituent = true; break; }
            }
            if (!is_constituent) {
                for (uint32_t ni : nuc.neutron_indices) {
                    if (ni == i) { is_constituent = true; break; }
                }
            }
            if (is_constituent) continue;

            // ── SPALLATION: shatter the nucleus ──
            // Compute projectile relativistic kinetic energy (MeV)
            float beta_proj = std::min(speed / C_SIM, 0.9999f);
            float gamma_proj = 1.0f / std::sqrt(1.0f - beta_proj * beta_proj);
            float proj_mass_MeV = (ptype < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[ptype] : 938.0f;
            float proj_KE_MeV = (gamma_proj - 1.0f) * proj_mass_MeV;

            // Minimum KE required: must exceed nuclear binding energy per nucleon
            // Average binding energy ~8 MeV/nucleon (empirical semi-empirical mass formula)
            constexpr float BINDING_PER_NUCLEON_MEV = 8.0f;
            if (proj_KE_MeV < BINDING_PER_NUCLEON_MEV) continue;  // not enough to eject even 1 nucleon

            // Scale damage by projectile kinetic energy
            // Higher energy → more nucleons ejected
            float damage_frac = std::min(proj_KE_MeV / (BINDING_PER_NUCLEON_MEV * total_nucleons), 1.0f);

            // Number of nucleons ejected: proportional to damage and nucleus size,
            // capped by what the projectile can energetically afford
            int max_eject = total_nucleons;
            int num_eject = std::max(1, static_cast<int>(damage_frac * max_eject));
            int max_affordable = std::max(1, static_cast<int>(proj_KE_MeV / BINDING_PER_NUCLEON_MEV));
            num_eject = std::min(num_eject, std::min(max_eject, max_affordable));

            // If we're ejecting less than half, it's partial spallation (knock-out)
            // If we're ejecting all, it's total disintegration
            bool total_disintegration = (num_eject >= total_nucleons - 1);

            glm::vec2 proj_dir = glm::normalize(readback_velocities_[i] + glm::vec2(0.001f));
            int ejected = 0;

            // Eject from proton indices
            auto eject_nucleon = [&](uint32_t idx) {
                if (used[idx]) return;
                used[idx] = true;

                // Give fragment velocity: combination of projectile direction + random scatter
                glm::vec2 scatter = rand_dir() * 0.5f + proj_dir * 0.5f;
                scatter = glm::normalize(scatter);
                float frag_speed = FRAGMENT_SPEED + speed * 0.2f * damage_frac;
                readback_velocities_[idx] = scatter * frag_speed;
                readback_energies_[idx] = FRAGMENT_ENERGY;
                particles.orbital_parent[idx] = -1;  // unbind from nucleus
            };

            // Eject protons first (from the back to avoid index invalidation)
            while (ejected < num_eject && !nuc.proton_indices.empty()) {
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();
                eject_nucleon(pi);
                nuc.Z--;
                ejected++;
            }

            // Then eject neutrons
            while (ejected < num_eject && !nuc.neutron_indices.empty()) {
                uint32_t ni = nuc.neutron_indices.back();
                nuc.neutron_indices.pop_back();
                eject_nucleon(ni);
                nuc.N--;
                ejected++;
            }

            // Projectile loses energy and slows down
            readback_energies_[i] = std::max(readback_energies_[i] * 0.3f, 0.1f);
            readback_velocities_[i] *= 0.3f;
            used[i] = true;

            // Recoil on remaining nucleus
            if (!total_disintegration) {
                glm::vec2 recoil = -proj_dir * speed * 0.15f;
                for (uint32_t pi : nuc.proton_indices) {
                    if (!used[pi]) readback_velocities_[pi] += recoil;
                }
                for (uint32_t ni : nuc.neutron_indices) {
                    if (!used[ni]) readback_velocities_[ni] += recoil;
                }
            }

            // Also free any bound electrons/positrons (they scatter)
            uint32_t lepton_type = nuc.is_anti ? POSITRON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
            for (uint32_t k = 0; k < n; ++k) {
                if (used[k]) continue;
                if (particles.types[k] != lepton_type) continue;
                if (k >= particles.orbital_parent.size()) continue;
                if (particles.orbital_parent[k] != static_cast<int32_t>(nuc.rep)) continue;

                // Scatter the electron/positron
                if (total_disintegration || (ejected > total_nucleons / 2)) {
                    readback_velocities_[k] += rand_dir() * 40.0f;
                    particles.orbital_parent[k] = -1;
                    particles.orbital_shell[k] = -1;
                    particles.excitation_timer[k] = 0;
                    particles.genomes[k * GENOME_SIZE + 2] = 0.0f;
                }
            }

            any_spallated = true;
            spallation_count++;
            try_unlock(ACH_FIRST_SPALLATION);

            {
                char msg[128];
                if (total_disintegration)
                    snprintf(msg, sizeof(msg), "Spallation: nucleus (Z=%d) disintegrated!",
                             nuc.Z + ejected);
                else
                    snprintf(msg, sizeof(msg), "Spallation: %d nucleons ejected from Z=%d nucleus",
                             ejected, nuc.Z + ejected);
                iface.push_notification(msg, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                char spall_detail[512];
                snprintf(spall_detail, sizeof(spall_detail),
                    "Projectile #%u type=%u KE: %.2f MeV\nTarget nucleus: rep #%u Z=%d N=%d\nDamage fraction: %.3f\nNucleons ejected: %d / %d\nTotal disintegration: %s",
                    i, ptype, proj_KE_MeV,
                    nuc.rep, nuc.Z + ejected, total_nucleons - nuc.Z,
                    damage_frac, ejected, total_nucleons,
                    total_disintegration ? "yes" : "no");
                iface.push_decay_event(msg, PhysicsInterface::DEVT_SPALLATION, ImVec4(1.0f, 0.5f, 0.3f, 1.0f), std::string(spall_detail));
            }

            break;  // one spallation per projectile
        }
    }

    // ── High-energy photon–nucleus interactions ────────────────────────────
    // Processes ordered by energy threshold (ascending):
    //   0.50+ : Photodisintegration  — γ + A → (A-1) + nucleon
    //   0.60+ : Pair production      — γ → e⁺ + e⁻ (needs nuclear field)
    //   0.80+ : Pion production      — γ + N → N' + π (quark-antiquark pair)
    //   0.85+ : Vector meson dom.    — γ → ρ/ω meson → hadronic shower
    //
    // A given photon triggers AT MOST one of these per frame.

    auto find_dormant_sp = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f && !used[k]) return k;
        return UINT32_MAX;
    };

    for (uint32_t i = 0; i < n && spallation_count < MAX_SPALLATIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (particles.types[i] != PHOTON_TYPE_PHYS) continue;
        float ph_energy = readback_energies_[i];
        if (ph_energy < 0.50f) continue;

        float ph_speed = glm::length(readback_velocities_[i]);
        if (ph_speed < 80.0f) continue;
        glm::vec2 ph_dir = readback_velocities_[i] / ph_speed;

        glm::vec2 ph_pos = readback_positions_[i];

        for (auto& nuc : detected_nuclei_) {
            if (spallation_count >= MAX_SPALLATIONS_PER_FRAME) break;

            int total_nucleons = nuc.Z + static_cast<int>(nuc.neutron_indices.size());
            if (total_nucleons < 2) continue;

            glm::vec2 delta = (ph_pos - nuc.center);
            float d2 = delta.x * delta.x + delta.y * delta.y;

            // Use a larger interaction radius for pair production (virtual photon
            // couples to nuclear Coulomb field at longer range)
            float radius_sq = (ph_energy >= 0.60f) ? 225.0f : HIT_RADIUS_SQ;  // 15² or 10²
            if (d2 > radius_sq) continue;

            // ── Probabilistic process selection based on energy ──
            // Higher energy unlocks more processes; we pick the highest available
            // with some randomness (the distribution favors dominant channels).
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            float roll = unit(rng);

            if (ph_energy >= 0.85f && roll < 0.25f) {
                // ═══ VECTOR MESON DOMINANCE ═══
                // γ fluctuates into a virtual ρ⁰ meson (uū–dd̄ superposition)
                // which interacts hadronically → multiple nucleon ejections +
                // quark-antiquark debris (hadronic shower).
                // Physical threshold: E_γ ≥ m_ρ = 775 MeV (ρ⁰ rest mass)
                // Sim-scaled: 0.85 (85% of max energy buffer)

                used[i] = true;
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);

                // Eject multiple nucleons (scaled by energy and nucleus size)
                int num_eject = std::min(std::max(2, static_cast<int>(ph_energy * 4.0f)),
                                         total_nucleons - 1);
                int ejected = 0;

                while (ejected < num_eject && !nuc.neutron_indices.empty()) {
                    uint32_t ni = nuc.neutron_indices.back();
                    nuc.neutron_indices.pop_back();
                    readback_velocities_[ni] = rand_dir() * (FRAGMENT_SPEED + ph_energy * 50.0f);
                    readback_energies_[ni] = FRAGMENT_ENERGY;
                    particles.orbital_parent[ni] = -1;
                    used[ni] = true;
                    nuc.N--;
                    ejected++;
                }
                while (ejected < num_eject && !nuc.proton_indices.empty()) {
                    uint32_t pi = nuc.proton_indices.back();
                    nuc.proton_indices.pop_back();
                    readback_velocities_[pi] = rand_dir() * (FRAGMENT_SPEED + ph_energy * 50.0f);
                    readback_energies_[pi] = FRAGMENT_ENERGY;
                    particles.orbital_parent[pi] = -1;
                    used[pi] = true;
                    nuc.Z--;
                    ejected++;
                }

                // Spawn quark-antiquark debris (the ρ meson decay products)
                // ρ⁰ → u + ū  (or d + d̄) — spawn as quark pair
                uint32_t q_slot = find_dormant_sp(0);
                uint32_t qbar_slot = (q_slot != UINT32_MAX) ? find_dormant_sp(q_slot + 1) : UINT32_MAX;
                if (q_slot != UINT32_MAX && qbar_slot != UINT32_MAX) {
                    glm::vec2 q_dir = rand_dir();
                    bool pick_up = (unit(rng) < 0.5f);
                    uint32_t q_type = pick_up ? UP_QUARK_TYPE : DOWN_QUARK_TYPE;
                    uint32_t qbar_type = pick_up ? ANTI_UP_TYPE : ANTI_DOWN_TYPE;

                    write_spawn_genome(particles, q_slot, q_type, rng, frame_counter_);
                    readback_positions_[q_slot] = nuc.center + q_dir * 5.0f;
                    readback_velocities_[q_slot] = q_dir * 180.0f;
                    readback_energies_[q_slot] = ph_energy * 0.3f;
                    used[q_slot] = true;

                    write_spawn_genome(particles, qbar_slot, qbar_type, rng, frame_counter_);
                    readback_positions_[qbar_slot] = nuc.center - q_dir * 5.0f;
                    readback_velocities_[qbar_slot] = -q_dir * 180.0f;
                    readback_energies_[qbar_slot] = ph_energy * 0.3f;
                    used[qbar_slot] = true;

                    // Link quark pair as meson for decay tracking
                    particles.entangled_partner[q_slot] = qbar_slot;
                    particles.entangled_partner[qbar_slot] = q_slot;
                    // Tag as VMD cascade origin
                    if (q_slot < particles.cascade_tag.size()) particles.cascade_tag[q_slot] = 2;
                    if (qbar_slot < particles.cascade_tag.size()) particles.cascade_tag[qbar_slot] = 2;
                }

                // Scatter bound leptons
                uint32_t lepton_type = nuc.is_anti ? POSITRON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
                for (uint32_t k = 0; k < n; ++k) {
                    if (used[k] || particles.types[k] != lepton_type) continue;
                    if (k >= particles.orbital_parent.size()) continue;
                    if (particles.orbital_parent[k] != static_cast<int32_t>(nuc.rep)) continue;
                    readback_velocities_[k] += rand_dir() * 50.0f;
                    particles.orbital_parent[k] = -1;
                    particles.orbital_shell[k] = -1;
                    particles.excitation_timer[k] = 0;
                    particles.genomes[k * GENOME_SIZE + 2] = 0.0f;
                }

                any_spallated = true;
                spallation_count++;

                char msg[128];
                snprintf(msg, sizeof(msg),
                    "VMD: \xCF\x81\xE2\x81\xB0 meson shower — %d nucleons + q\xC4\x81 pair from Z=%d",
                    ejected, nuc.Z + ejected);
                iface.push_notification(msg, ImVec4(0.9f, 0.3f, 0.9f, 1.0f));
                {
                    char vmd_detail[512];
                    snprintf(vmd_detail, sizeof(vmd_detail),
                        "Photon #%u E: %.4f (>=0.85 threshold)\nTarget nucleus: rep #%u Z=%d N=%d\nNucleons ejected: %d\nq\xC4\x81 pair: q=#%u q\xCC\x84=#%u"
                        "\n\xE2\x94\x80 CASCADE START: \xCE\xB3 \xE2\x86\x92 \xCF\x81\xE2\x81\xB0 \xE2\x86\x92 q\xC4\x81 \xE2\x86\x92 \xCF\x80 \xE2\x86\x92 \xCE\xBC+\xCE\xBD \xE2\x86\x92 e+\xCE\xBD+\xCE\xBD\xCC\x84",
                        i, ph_energy,
                        nuc.rep, nuc.Z + ejected, total_nucleons - ejected,
                        ejected, q_slot, qbar_slot);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_VMD, ImVec4(0.9f, 0.3f, 0.9f, 1.0f), std::string(vmd_detail));
                }
                break;

            } else if (ph_energy >= 0.80f && roll < 0.50f) {
                // ═══ PHOTOPION PRODUCTION (Δ resonance) ═══
                // γ + p → Δ⁺ → n + π⁺  (or γ + n → Δ⁰ → p + π⁻)
                // Physical threshold: E_γ ≥ m_π + m_π²/(2m_N) ≈ 145 MeV
                // Sim-scaled: 0.80 (80% of max energy buffer)
                // The pion is a quark-antiquark bound state. In our sim we
                // represent it as a u + d̄ (π⁺) or ū + d (π⁻) pair.

                // Pick a target nucleon from the nucleus
                bool target_proton = !nuc.proton_indices.empty();
                bool target_neutron = !nuc.neutron_indices.empty();
                if (!target_proton && !target_neutron) continue;

                // Prefer proton targets (γ + p → n + π⁺ has higher cross-section)
                uint32_t target_idx = UINT32_MAX;
                bool used_proton = false;
                if (target_proton && (roll < 0.35f || !target_neutron)) {
                    target_idx = nuc.proton_indices.back();
                    nuc.proton_indices.pop_back();
                    nuc.Z--;
                    used_proton = true;
                } else if (target_neutron) {
                    target_idx = nuc.neutron_indices.back();
                    nuc.neutron_indices.pop_back();
                    nuc.N--;
                }
                if (target_idx == UINT32_MAX) continue;

                // Absorb photon
                used[i] = true;
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);

                // Transmute nucleon: p → n (or n → p) via isospin flip
                uint32_t new_nucleon_type = used_proton ? NEUTRON_TYPE : PROTON_TYPE;
                write_spawn_genome(particles, target_idx, new_nucleon_type, rng, frame_counter_);
                glm::vec2 eject_dir = rand_dir();
                readback_velocities_[target_idx] = eject_dir * 120.0f;
                readback_energies_[target_idx] = 0.6f;
                particles.orbital_parent[target_idx] = -1;
                used[target_idx] = true;

                // Spawn pion as quark-antiquark pair
                // π⁺ = u + d̄;  π⁻ = ū + d
                uint32_t q_slot = find_dormant_sp(0);
                uint32_t qbar_slot = (q_slot != UINT32_MAX) ? find_dormant_sp(q_slot + 1) : UINT32_MAX;

                if (q_slot != UINT32_MAX && qbar_slot != UINT32_MAX) {
                    glm::vec2 pion_dir = ph_dir;  // pion roughly follows photon direction
                    uint32_t q_type, qbar_type;
                    if (used_proton) {
                        // γ + p → n + π⁺ (u + d̄)
                        q_type = UP_QUARK_TYPE;
                        qbar_type = ANTI_DOWN_TYPE;
                    } else {
                        // γ + n → p + π⁻ (ū + d)
                        q_type = DOWN_QUARK_TYPE;
                        qbar_type = ANTI_UP_TYPE;
                    }

                    glm::vec2 pion_pos = nuc.center + pion_dir * 6.0f;
                    write_spawn_genome(particles, q_slot, q_type, rng, frame_counter_);
                    readback_positions_[q_slot] = pion_pos + glm::vec2(1.5f, 0.0f);
                    readback_velocities_[q_slot] = pion_dir * 200.0f + rand_dir() * 20.0f;
                    readback_energies_[q_slot] = ph_energy * 0.35f;
                    used[q_slot] = true;

                    write_spawn_genome(particles, qbar_slot, qbar_type, rng, frame_counter_);
                    readback_positions_[qbar_slot] = pion_pos - glm::vec2(1.5f, 0.0f);
                    readback_velocities_[qbar_slot] = pion_dir * 200.0f + rand_dir() * 20.0f;
                    readback_energies_[qbar_slot] = ph_energy * 0.35f;
                    used[qbar_slot] = true;

                    // Link quark pair as pion meson for decay tracking
                    particles.entangled_partner[q_slot] = qbar_slot;
                    particles.entangled_partner[qbar_slot] = q_slot;
                    // Tag as photopion cascade origin
                    if (q_slot < particles.cascade_tag.size()) particles.cascade_tag[q_slot] = 1;
                    if (qbar_slot < particles.cascade_tag.size()) particles.cascade_tag[qbar_slot] = 1;
                }

                // Recoil on remaining nucleus
                glm::vec2 recoil = -ph_dir * 20.0f;
                for (uint32_t pi : nuc.proton_indices)
                    if (!used[pi]) readback_velocities_[pi] += recoil;
                for (uint32_t ni : nuc.neutron_indices)
                    if (!used[ni]) readback_velocities_[ni] += recoil;

                any_spallated = true;
                spallation_count++;

                char msg[128];
                const char* pion_sym = used_proton ? "\xCF\x80\xE2\x81\xBA" : "\xCF\x80\xE2\x81\xBB";
                snprintf(msg, sizeof(msg), "Photopion: \xCE\xB3 + %s \xE2\x86\x92 %s + %s",
                         used_proton ? "p" : "n",
                         used_proton ? "n" : "p",
                         pion_sym);
                iface.push_notification(msg, ImVec4(0.4f, 0.9f, 0.6f, 1.0f));
                {
                    char pion_detail[512];
                    snprintf(pion_detail, sizeof(pion_detail),
                        "Photon #%u E: %.4f (>=0.80 threshold)\nTarget nucleus: rep #%u Z=%d\nTarget nucleon #%u (%s)\nIsospin flip: %s->%s\nPion (%s): q=#%u q\xCC\x84=#%u"
                        "\n\xE2\x94\x80 CASCADE START: \xCE\xB3+%s \xE2\x86\x92 %s+%s \xE2\x86\x92 \xCE\xBC+\xCE\xBD \xE2\x86\x92 e+\xCE\xBD+\xCE\xBD\xCC\x84",
                        i, ph_energy,
                        nuc.rep, nuc.Z,
                        target_idx, used_proton ? "proton" : "neutron",
                        used_proton ? "p" : "n", used_proton ? "n" : "p",
                        used_proton ? "\xCF\x80\xE2\x81\xBA" : "\xCF\x80\xE2\x81\xBB", q_slot, qbar_slot,
                        used_proton ? "p" : "n",
                        used_proton ? "n" : "p",
                        used_proton ? "\xCF\x80\xE2\x81\xBA" : "\xCF\x80\xE2\x81\xBB");
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_PION_PRODUCTION, ImVec4(0.4f, 0.9f, 0.6f, 1.0f), std::string(pion_detail));
                }
                break;

            } else if (ph_energy >= 0.75f && roll < 0.65f && d2 < 225.0f) {
                // ═══ PAIR PRODUCTION ═══
                // γ → e⁺ + e⁻  (requires nearby nucleus for momentum conservation)
                // Physical threshold: E_γ ≥ 2 × m_e c² = 1.022 MeV
                // Sim-scaled: 0.75 (energy buffer max = 1.0 MeV, so 75% of max)
                // Requires high-energy photon in nuclear Coulomb field.

                used[i] = true;
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);

                // Create electron-positron pair
                uint32_t e_slot = find_dormant_sp(0);
                uint32_t p_slot = (e_slot != UINT32_MAX) ? find_dormant_sp(e_slot + 1) : UINT32_MAX;

                if (e_slot != UINT32_MAX && p_slot != UINT32_MAX) {
                    // Pair opens in directions roughly perpendicular to photon path
                    // with slight forward boost (lab frame)
                    glm::vec2 perp(-ph_dir.y, ph_dir.x);
                    float pair_speed = std::min(ph_energy * 80.0f, 200.0f);
                    float pair_energy = std::min(ph_energy * 0.45f, 0.8f);

                    write_spawn_genome(particles, e_slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[e_slot] = ph_pos + perp * 3.0f;
                    readback_velocities_[e_slot] = (ph_dir * 0.4f + perp * 0.6f) * pair_speed;
                    readback_energies_[e_slot] = pair_energy;
                    used[e_slot] = true;

                    write_spawn_genome(particles, p_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[p_slot] = ph_pos - perp * 3.0f;
                    readback_velocities_[p_slot] = (ph_dir * 0.4f - perp * 0.6f) * pair_speed;
                    readback_energies_[p_slot] = pair_energy;
                    used[p_slot] = true;

                    // Small nuclear recoil (momentum conservation)
                    glm::vec2 recoil = -ph_dir * 5.0f;
                    for (uint32_t pi : nuc.proton_indices)
                        if (!used[pi]) readback_velocities_[pi] += recoil;
                    for (uint32_t ni : nuc.neutron_indices)
                        if (!used[ni]) readback_velocities_[ni] += recoil;
                }

                any_spallated = true;
                spallation_count++;

                iface.push_notification(
                    "Pair production: \xCE\xB3 \xE2\x86\x92 e\xE2\x81\xBA + e\xE2\x81\xBB",
                    ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
                {
                    float pp_energy = std::min(ph_energy * 0.45f, 0.8f);
                    float pp_speed  = std::min(ph_energy * 80.0f, 200.0f);
                    char pp_detail[512];
                    snprintf(pp_detail, sizeof(pp_detail),
                        "Photon #%u E: %.4f (>=0.75, in nuclear field)\nTarget nucleus: rep #%u Z=%d\ne- #%u E: %.4f\ne+ #%u E: %.4f\nPair speed: %.2f",
                        i, ph_energy,
                        nuc.rep, nuc.Z,
                        e_slot, pp_energy, p_slot, pp_energy, pp_speed);
                    iface.push_decay_event("\xCE\xB3 \xE2\x86\x92 e\xE2\x81\xBA + e\xE2\x81\xBB", PhysicsInterface::DEVT_PAIR_PRODUCTION, ImVec4(0.3f, 0.7f, 1.0f, 1.0f), std::string(pp_detail));
                }
                try_unlock(ACH_FIRST_PAIR_PRODUCTION);
                break;

            } else if (ph_energy >= 0.50f) {
                // ═══ PHOTODISINTEGRATION ═══
                // γ + A → (A-1) + nucleon
                // Physical threshold: E_γ ≥ S_n ≈ 6-8 MeV (neutron separation energy)
                // Sim-scaled: 0.50 (50% of max energy buffer)
                // Giant dipole resonance: photon absorbed by nucleus, ejects nucleon.
                // Prefer neutron (lower Coulomb barrier).

                used[i] = true;
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);

                glm::vec2 eject_dir = rand_dir();
                float eject_speed = std::min(ph_energy * 150.0f, 250.0f);

                // At higher energies, can eject 2 nucleons (giant resonance breakup)
                int num_eject = (ph_energy >= 0.75f && total_nucleons >= 4) ? 2 : 1;

                for (int ne = 0; ne < num_eject; ++ne) {
                    glm::vec2 dir = (ne == 0) ? eject_dir : rand_dir();
                    if (!nuc.neutron_indices.empty()) {
                        uint32_t ni = nuc.neutron_indices.back();
                        nuc.neutron_indices.pop_back();
                        readback_velocities_[ni] = dir * eject_speed;
                        readback_energies_[ni] = std::min(ph_energy * 0.7f, 0.9f);
                        particles.orbital_parent[ni] = -1;
                        used[ni] = true;
                        nuc.N--;
                    } else if (!nuc.proton_indices.empty()) {
                        uint32_t pi = nuc.proton_indices.back();
                        nuc.proton_indices.pop_back();
                        readback_velocities_[pi] = dir * eject_speed;
                        readback_energies_[pi] = std::min(ph_energy * 0.7f, 0.9f);
                        particles.orbital_parent[pi] = -1;
                        used[pi] = true;
                        nuc.Z--;
                    }
                }

                // Recoil on remaining nucleus
                glm::vec2 recoil = -eject_dir * eject_speed * 0.1f;
                for (uint32_t pi : nuc.proton_indices)
                    if (!used[pi]) readback_velocities_[pi] += recoil;
                for (uint32_t ni : nuc.neutron_indices)
                    if (!used[ni]) readback_velocities_[ni] += recoil;

                any_spallated = true;
                spallation_count++;

                char msg[96];
                snprintf(msg, sizeof(msg),
                    "Photodisintegration: \xCE\xB3 ejected %d nucleon%s from Z=%d",
                    num_eject, num_eject > 1 ? "s" : "",
                    nuc.Z + (num_eject > 1 ? 1 : 0));
                iface.push_notification(msg, ImVec4(0.8f, 0.6f, 1.0f, 1.0f));
                {
                    char pdis_detail[512];
                    snprintf(pdis_detail, sizeof(pdis_detail),
                        "Photon #%u E: %.4f (>=0.50 threshold)\nTarget nucleus: rep #%u Z=%d N=%d\nNucleons ejected: %d\nEject speed: %.2f\nGDR: giant dipole resonance absorption",
                        i, ph_energy,
                        nuc.rep, nuc.Z, total_nucleons - nuc.Z,
                        num_eject, eject_speed);
                    iface.push_decay_event(msg, PhysicsInterface::DEVT_PHOTODISINTEGRATION, ImVec4(0.8f, 0.6f, 1.0f, 1.0f), std::string(pdis_detail));
                }
                break;
            }
        }
    }

    if (any_spallated) {
        cpu_particles_dirty_ = true;
    }
}

// ── Virtual particle pair creation ───────────────────────────────────────────

void PhysicsSimulation::check_virtual_pairs() {
    if (readback_positions_.empty()) return;
    if (cfg.vacuum_energy < 0.001f) return;

    const uint32_t n = cfg.particle_count;
    const uint32_t max_pairs = cfg.virtual_pair_max_per_tick;
    const uint32_t max_attempts = max_pairs * 3;
    const float casimir_r = cfg.casimir_radius;
    const float scatter = cfg.virtual_pair_scatter;

    std::mt19937 rng(frame_counter_ * 1664525u + 1013904223u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Spawn virtual pairs within the camera's visible area (not a fixed rectangle)
    float half_vw = static_cast<float>(REGION_W) / (2.0f * cfg.current_camera_zoom);
    float half_vh = static_cast<float>(REGION_H) / (2.0f * cfg.current_camera_zoom);
    std::uniform_real_distribution<float> x_dist(cfg.camera_origin.x - half_vw,
                                                  cfg.camera_origin.x + half_vw);
    std::uniform_real_distribution<float> y_dist(cfg.camera_origin.y - half_vh,
                                                  cfg.camera_origin.y + half_vh);

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    bool any_spawned = false;
    uint32_t pairs_created = 0;
    uint32_t dormant_hint = 0;

    for (uint32_t attempt = 0; attempt < max_attempts && pairs_created < max_pairs; ++attempt) {
        // ── 1. Random vacuum point ──────────────────────────────────────
        glm::vec2 P(x_dist(rng), y_dist(rng));

        // ── 2. Select pair type (weighted random, independent probabilities) ──
        // Rates ∝ coupling² × phase space.  Each type has an independent
        // weight so none can shadow another (fixes graviton crowding bug).
        //
        // γγ:    QED vacuum — always present, massless, baseline rate
        // gg:    QCD vacuum — always present (massless!), α_s >> α_em
        // e⁺e⁻: QED pair — needs ~1 MeV vacuum energy (2 × 0.511 MeV)
        // W⁺W⁻: Weak pair — needs ~161 GeV (2 × 80.4 GeV), extremely rare
        // GG:    Quantum gravity — needs gravity enabled, very weak coupling
        uint32_t vtype_a, vtype_b;
        float ve = cfg.vacuum_energy;

        float w_photon   = 1.0f;                                               // QED: baseline
        float w_gluon    = 2.0f;                                               // QCD: α_s² >> α² (massless, always)
        float w_epair    = (ve > 0.3f)  ? 0.4f * (ve - 0.3f) / 0.7f : 0.0f;  // ramps 0→0.4 over ve 0.3→1.0
        float w_wpair    = (ve > 0.9f)  ? 0.08f * cfg.weak_coupling  : 0.0f;  // extremely rare, needs weak on
        float w_graviton = (cfg.gravity_strength > 0.001f) ? 0.15f : 0.0f;     // quantum gravity, needs gravity on

        float w_total = w_photon + w_gluon + w_epair + w_wpair + w_graviton;
        float roll = unit(rng) * w_total;

        if (roll < w_photon) {
            vtype_a = PHOTON_TYPE_PHYS;
            vtype_b = PHOTON_TYPE_PHYS;
        } else if ((roll -= w_photon) < w_gluon) {
            vtype_a = GLUON_TYPE_PHYS;
            vtype_b = GLUON_TYPE_PHYS;
        } else if ((roll -= w_gluon) < w_epair) {
            vtype_a = ELECTRON_TYPE_PHYS;
            vtype_b = POSITRON_TYPE_PHYS;
        } else if ((roll -= w_epair) < w_wpair) {
            vtype_a = W_PLUS_TYPE_PHYS;
            vtype_b = W_MINUS_TYPE_PHYS;
        } else {
            vtype_a = GRAVITON_TYPE_PHYS;
            vtype_b = GRAVITON_TYPE_PHYS;
        }

        // ── 3. Rest mass check — vacuum must supply 2mc² ────────────────
        float m_a = (vtype_a < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[vtype_a] : 0.0f;
        float m_b = (vtype_b < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[vtype_b] : 0.0f;
        float pair_mass = m_a + m_b;
        if (pair_mass > 0.01f) {
            float min_ve = std::min(pair_mass / E_SCALE_MEV, 1.9f);
            if (ve < min_ve) continue;  // vacuum too cold for massive pair
        }

        // ── 4. Casimir mode exclusion ───────────────────────────────────
        // Between closely-spaced real particles, certain vacuum modes are
        // excluded (boundary conditions). This suppression of virtual pair
        // creation IS the Casimir effect — the missing radiation pressure
        // creates a net attractive force between nearby surfaces.
        if (cfg.casimir_strength > 0.001f && iface.prefs.spatial_grid) {
            int nearby_count = 0;
            // Collect nearby real (non-virtual, non-dormant) particles
            struct NearbyInfo { uint32_t idx; float dist; glm::vec2 delta; };
            NearbyInfo nearby[64];

            grid_.query(P.x, P.y, casimir_r, [&](uint32_t j) {
                if (nearby_count >= 64) return;
                if (readback_energies_[j] < 0.01f) return;
                if (particles.behavior_flags[particles.types[j]] & BEHAVIOR_VIRTUAL) return;
                glm::vec2 d = P - readback_positions_[j];
                float dist = std::sqrt(d.x * d.x + d.y * d.y);
                if (dist < 1.0f) return;
                nearby[nearby_count++] = { j, dist, d };
            });

            if (nearby_count >= 2) {
                // Suppression probability increases with local particle density
                float suppression = 1.0f - 1.0f / (1.0f + nearby_count * 0.5f);
                if (unit(rng) < suppression) {
                    // Mode excluded — apply Casimir attractive impulse
                    // Missing radiation pressure → net inward force ∝ 1/d³ (2D)
                    float cs = cfg.casimir_strength * ve;
                    for (int k = 0; k < nearby_count; ++k) {
                        float d = nearby[k].dist;
                        float impulse_mag = cs / (d * d * d);
                        glm::vec2 impulse = (nearby[k].delta / d) * impulse_mag;
                        readback_velocities_[nearby[k].idx] += impulse;
                    }
                    any_spawned = true;  // velocities changed
                    continue;  // pair not spawned (suppressed mode)
                }
            }
        }

        // ── 5. Spawn the virtual pair ───────────────────────────────────
        uint32_t slot_a = find_dormant(dormant_hint);
        if (slot_a == UINT32_MAX) break;
        uint32_t slot_b = find_dormant(slot_a + 1);
        if (slot_b == UINT32_MAX) break;
        dormant_hint = slot_b + 1;

        float angle = angle_dist_(rng);
        glm::vec2 dir(std::cos(angle), std::sin(angle));

        // Position: spawn at vacuum point with pair scatter (toroidal wrap)
        readback_positions_[slot_a] = (P + dir * scatter);
        readback_positions_[slot_b] = (P - dir * scatter);

        // Velocity: massless at c, massive at relativistic speed
        float pair_speed = (pair_mass < 0.01f) ? C_SIM : ke_to_speed(pair_mass * 0.5f, vtype_a);
        readback_velocities_[slot_a] =  dir * pair_speed;
        readback_velocities_[slot_b] = -dir * pair_speed;

        // Energy
        float pair_energy = mev_to_ebuf(std::max(pair_mass * 0.5f, 0.1f));
        readback_energies_[slot_a] = pair_energy;
        readback_energies_[slot_b] = pair_energy;

        // Set types and genome
        write_spawn_genome(particles, slot_a, vtype_a, rng, frame_counter_);
        write_spawn_genome(particles, slot_b, vtype_b, rng, frame_counter_);

        // Heisenberg uncertainty: ΔE·Δt ≥ ℏ/2
        // Heavier pairs live shorter: decay_rate ∝ pair_mass
        // γγ (0 MeV) → 0.01 (~120 frames), e⁺e⁻ (1 MeV) → 0.08 (~15 frames)
        // W⁺W⁻ (161 GeV) → 0.5 (~2 frames, nearly instant)
        float virt_decay = (pair_mass < 0.01f) ? 0.01f : std::min(pair_mass * 0.08f, 0.5f);
        // Negative sign marks virtual particles (shader uses abs for drain, sign for classification)
        particles.genomes[slot_a * GENOME_SIZE + 3] = -virt_decay;
        particles.genomes[slot_b * GENOME_SIZE + 3] = -virt_decay;

        // Entangle the pair
        if (cfg.entanglement_enabled) {
            particles.entangled_partner[slot_a] = slot_b;
            particles.entangled_partner[slot_b] = slot_a;
            float spin_a = particles.genomes[slot_a * GENOME_SIZE + 1];
            if (std::abs(spin_a) > 0.01f)
                particles.genomes[slot_b * GENOME_SIZE + 1] = -spin_a;
        }

        pairs_created++;
        any_spawned = true;
    }

    if (any_spawned) {
        cpu_particles_dirty_ = true;
        try_unlock(ACH_FIRST_VIRTUAL_PAIR);
    }
}

// ── Hadronization / Color confinement ────────────────────────────────────────

void PhysicsSimulation::check_hadronization() {
    if (readback_positions_.empty()) return;
    if (!cfg.hadronization_enabled) return;

    // QGP deconfinement — quarks are free above this temperature
    constexpr float QGP_TEMP = 2.0e12f;
    if (cfg.temperature_kelvin >= QGP_TEMP) return;

    const uint32_t n = cfg.particle_count;
    constexpr float CONFINEMENT_RADIUS = 45.0f;   // beyond string breaking (40px shader)
    constexpr float HADRONIZE_RADIUS   = 50.0f;   // search radius for meson partners
    constexpr float STRING_BREAK_DIST  = 55.0f;   // create new pairs beyond this
    constexpr float MESON_ENERGY       = 0.4f;
    constexpr uint32_t MAX_HADRONIZE   = 24;       // aggressive meson binding (confinement)
    constexpr uint32_t MAX_STRING_BREAK = 2;

    // ── Helpers ──────────────────────────────────────────────────────────────
    auto is_quark = [](uint32_t t) { return t >= UP_QUARK_TYPE && t <= BOTTOM_QUARK_TYPE; };
    auto is_antiquark = [](uint32_t t) { return t >= ANTI_UP_TYPE && t <= ANTI_BOTTOM_TYPE; };
    auto is_any_quark = [](uint32_t t) {
        return (t >= UP_QUARK_TYPE && t <= BOTTOM_QUARK_TYPE) ||
               (t >= ANTI_UP_TYPE  && t <= ANTI_BOTTOM_TYPE);
    };
    auto anti_flavor = [](uint32_t t) -> uint32_t {
        if (t >= UP_QUARK_TYPE && t <= BOTTOM_QUARK_TYPE)
            return t + (ANTI_UP_TYPE - UP_QUARK_TYPE);   // +6
        if (t >= ANTI_UP_TYPE && t <= ANTI_BOTTOM_TYPE)
            return t - (ANTI_UP_TYPE - UP_QUARK_TYPE);   // -6
        return t;
    };

    // ── Phase 1: identify free quarks (no nearby quark partner) ──────────────
    std::vector<uint32_t> free_quarks;
    free_quarks.reserve(64);

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t type = particles.types[i];
        if (!is_any_quark(type)) continue;

        bool has_partner = false;
        auto partner_check = [&](uint32_t j) {
            if (has_partner || j == i) return;
            if (readback_energies_[j] < 0.01f) return;
            if (!is_any_quark(particles.types[j])) return;
            glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
            if (d.x * d.x + d.y * d.y < CONFINEMENT_RADIUS * CONFINEMENT_RADIUS)
                has_partner = true;
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                        CONFINEMENT_RADIUS, partner_check);
        else
            for (uint32_t j = 0; j < n; ++j) partner_check(j);

        if (!has_partner)
            free_quarks.push_back(i);
    }

    if (free_quarks.empty()) return;

    // Suppress pair creation if too many free quarks already — confinement pressure
    constexpr uint32_t MAX_FREE_QUARKS = 24;
    bool suppress_creation = (free_quarks.size() > MAX_FREE_QUARKS);

    std::mt19937 rng(frame_counter_ * 3141592653u);
    std::vector<bool> consumed(n, false);
    uint32_t events = 0;
    bool any_changed = false;

    // ── Phase 2: meson formation — pair free q + free q̄ ─────────────────────
    for (size_t fi = 0; fi < free_quarks.size() && events < MAX_HADRONIZE; ++fi) {
        uint32_t i = free_quarks[fi];
        if (consumed[i]) continue;
        uint32_t ti = particles.types[i];

        float color_i = particles.genomes[i * GENOME_SIZE + 2];
        float best_d2 = HADRONIZE_RADIUS * HADRONIZE_RADIUS;
        uint32_t best_j = UINT32_MAX;

        auto meson_search = [&](uint32_t j) {
            if (j == i || consumed[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            uint32_t tj = particles.types[j];
            if (!is_any_quark(tj)) return;
            // Need opposite matter/antimatter
            if (is_quark(ti) == is_quark(tj)) return;
            // Color compatibility: q(+c) + q̄(-c), same |c|
            float color_j = particles.genomes[j * GENOME_SIZE + 2];
            if (std::abs(std::abs(color_i) - std::abs(color_j)) > 0.1f) return;
            if (color_i * color_j >= 0.0f) return;
            glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) { best_d2 = d2; best_j = j; }
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                        HADRONIZE_RADIUS, meson_search);
        else
            for (uint32_t j = 0; j < n; ++j) meson_search(j);

        if (best_j != UINT32_MAX) {
            // Velocity impulse toward each other (binding)
            glm::vec2 delta = (readback_positions_[best_j] - readback_positions_[i]);
            float dist = std::sqrt(best_d2);
            if (dist > 0.1f) {
                glm::vec2 dir = delta / dist;
                readback_velocities_[i]      += dir * 30.0f;
                readback_velocities_[best_j] -= dir * 30.0f;
            }
            // Link quark pair as meson for decay tracking
            particles.entangled_partner[i] = best_j;
            particles.entangled_partner[best_j] = i;

            consumed[i] = true;
            consumed[best_j] = true;
            any_changed = true;
            ++events;
        }
    }

    // ── Phase 2b: baryon condensation — 3 quarks with RGB → proton/neutron ──
    // Hagedorn temperature ~150 MeV ≈ 1.7×10¹² K — below this, baryons form
    constexpr float HAGEDORN_TEMP    = 1.7e12f;
    constexpr float BARYON_RADIUS    = 15.0f;   // quarks must be very close
    constexpr float BARYON_RADIUS_SQ = BARYON_RADIUS * BARYON_RADIUS;
    constexpr uint32_t MAX_BARYON    = 16;

    // Map quark flavor to up-like(0) or down-like(1) for baryon determination
    // +2/3 charge quarks (u,c,t) → up-like → eventually decay to u
    // -1/3 charge quarks (d,s,b) → down-like → d stable, s/b decay to u but
    //   in the baryon context the quark content determines the hadron
    auto is_down_type = [](uint32_t t) -> bool {
        return t == DOWN_QUARK_TYPE  || t == STRANGE_QUARK_TYPE  || t == BOTTOM_QUARK_TYPE ||
               t == ANTI_DOWN_TYPE   || t == ANTI_STRANGE_TYPE   || t == ANTI_BOTTOM_TYPE;
    };

    if (cfg.temperature_kelvin < HAGEDORN_TEMP) {
        uint32_t baryons_formed = 0;

        for (uint32_t i = 0; i < n && baryons_formed < MAX_BARYON; ++i) {
            if (readback_energies_[i] < 0.01f) continue;
            if (consumed[i]) continue;
            uint32_t ti = particles.types[i];
            bool matter_i = is_quark(ti);
            bool anti_i   = is_antiquark(ti);
            if (!matter_i && !anti_i) continue;

            float color_i = particles.genomes[i * GENOME_SIZE + 2];
            int ci = static_cast<int>(std::round(color_i));
            if (ci == 0) continue;

            // Find the 2 missing colors (same sign as ci)
            int sign = (ci > 0) ? 1 : -1;
            int abs_ci = std::abs(ci);
            int need[2];
            int ni = 0;
            for (int c = 1; c <= 3; ++c)
                if (c != abs_ci) need[ni++] = c * sign;

            // Search for nearest quarks with each missing color
            uint32_t best_j = UINT32_MAX, best_k = UINT32_MAX;
            float best_j_d2 = BARYON_RADIUS_SQ, best_k_d2 = BARYON_RADIUS_SQ;

            auto triplet_search = [&](uint32_t j) {
                if (j == i || consumed[j]) return;
                if (readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (matter_i ? !is_quark(tj) : !is_antiquark(tj)) return;
                float color_j = particles.genomes[j * GENOME_SIZE + 2];
                int cj = static_cast<int>(std::round(color_j));
                glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
                float d2 = d.x * d.x + d.y * d.y;
                if (cj == need[0] && d2 < best_j_d2) { best_j_d2 = d2; best_j = j; }
                if (cj == need[1] && d2 < best_k_d2) { best_k_d2 = d2; best_k = j; }
            };

            if (iface.prefs.spatial_grid)
                grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                            BARYON_RADIUS, triplet_search);
            else
                for (uint32_t j = 0; j < n; ++j) triplet_search(j);

            if (best_j == UINT32_MAX || best_k == UINT32_MAX) continue;

            // Found RGB triplet! Determine baryon type from quark flavors
            int down_count = (is_down_type(ti) ? 1 : 0)
                           + (is_down_type(particles.types[best_j]) ? 1 : 0)
                           + (is_down_type(particles.types[best_k]) ? 1 : 0);

            uint32_t baryon_type;
            const char* baryon_name;
            if (matter_i) {
                // Real baryon content: uud→proton, udd→neutron
                // 0 down-type (uuu)→Δ++, but sim only has p/n, map to proton
                // 1 down-type (uud)→proton
                // 2 down-type (udd/usd/...)→neutron
                // 3 down-type (ddd/dds/...)→Δ⁻, map to neutron
                // Note: strange quarks count as down-type but will later decay to up
                baryon_type = (down_count <= 1) ? PROTON_TYPE : NEUTRON_TYPE;
                baryon_name = (down_count <= 1) ? "proton" : "neutron";
            } else {
                baryon_type = ANTIPROTON_TYPE_PHYS;
                baryon_name = "antiproton";
            }

            // Condense: centroid position, average velocity, combined energy
            glm::vec2 centroid = (readback_positions_[i] + readback_positions_[best_j]
                                + readback_positions_[best_k]) / 3.0f;
            glm::vec2 avg_vel  = (readback_velocities_[i] + readback_velocities_[best_j]
                                + readback_velocities_[best_k]) / 3.0f;
            float total_energy = readback_energies_[i] + readback_energies_[best_j]
                               + readback_energies_[best_k];

            // Convert slot i → baryon, kill j and k
            write_spawn_genome(particles, i, baryon_type, rng, frame_counter_);
            readback_positions_[i]  = centroid;
            readback_velocities_[i] = avg_vel;
            readback_energies_[i]   = std::min(total_energy, 1.0f);

            readback_energies_[best_j] = 0.0f;
            readback_energies_[best_k] = 0.0f;

            consumed[i] = true;
            consumed[best_j] = true;
            consumed[best_k] = true;
            any_changed = true;
            ++baryons_formed;

            char msg[64];
            snprintf(msg, sizeof(msg), "Hadronization \xe2\x86\x92 %s", baryon_name);
            iface.push_notification(msg, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
            {
                char hadr_detail[512];
                snprintf(hadr_detail, sizeof(hadr_detail),
                    "Quarks: #%u (type=%u) + #%u (type=%u) + #%u (type=%u)\nDown-type count: %d\nBaryon: %s (type=%u)\nCombined E: %.4f\nCentroid: (%.1f, %.1f)",
                    i, ti, best_j, particles.types[best_j], best_k, particles.types[best_k],
                    down_count, baryon_name, baryon_type,
                    total_energy, centroid.x, centroid.y);
                iface.push_decay_event(msg, PhysicsInterface::DEVT_FUSION,
                                       ImVec4(0.2f, 0.8f, 1.0f, 1.0f), std::string(hadr_detail));
            }
        }
    }

    // ── Phase 3: vacuum instability — spawn confinement partner for free quarks ─
    // In real QCD, a free quark's color field creates a q-qbar pair that immediately
    // forms a meson with the original quark. The new pair is BOUND, not free.
    uint32_t search_from = 0;
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    uint32_t vacuum_events = 0;
    if (!suppress_creation) {
    for (size_t fi = 0; fi < free_quarks.size() && vacuum_events < MAX_STRING_BREAK; ++fi) {
        uint32_t i = free_quarks[fi];
        if (consumed[i]) continue;
        if (readback_energies_[i] < 0.3f) continue;  // need energy for pair production

        uint32_t ti = particles.types[i];
        uint32_t spawn_type;
        if (is_quark(ti)) {
            spawn_type = (ti <= DOWN_QUARK_TYPE) ? anti_flavor(ti)
                       : ((rng() % 2 == 0) ? ANTI_UP_TYPE : ANTI_DOWN_TYPE);
        } else {
            spawn_type = (ti <= ANTI_DOWN_TYPE) ? anti_flavor(ti)
                       : ((rng() % 2 == 0) ? UP_QUARK_TYPE : DOWN_QUARK_TYPE);
        }

        uint32_t slot = find_dormant(search_from);
        if (slot == UINT32_MAX) break;
        search_from = slot + 1;

        glm::vec2 pos_i = readback_positions_[i];
        float a = angle_dist_(rng);
        glm::vec2 off(std::cos(a) * 2.0f, std::sin(a) * 2.0f);

        float color_i = particles.genomes[i * GENOME_SIZE + 2];
        write_spawn_genome(particles, slot, spawn_type, rng, frame_counter_);
        particles.genomes[slot * GENOME_SIZE + 2] = -color_i;  // complementary color

        // Spawn close to parent and drifting toward it (bound meson, not escaping)
        readback_positions_[slot]  = pos_i + off;
        readback_velocities_[slot] = readback_velocities_[i] - off * 5.0f;
        readback_energies_[slot]   = MESON_ENERGY * 0.5f;

        // Immediately bind as meson (confinement)
        particles.entangled_partner[i]    = slot;
        particles.entangled_partner[slot] = i;

        // Stronger energy drain from parent
        readback_energies_[i] -= 0.3f;
        if (readback_energies_[i] < 0.05f) readback_energies_[i] = 0.05f;

        consumed[i] = true;
        any_changed = true;
        ++vacuum_events;

        iface.push_notification("Confinement: q\xc4\x81 meson", ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
        {
            char vac_detail[512];
            snprintf(vac_detail, sizeof(vac_detail),
                "Source quark #%u type=%u E=%.4f\nBound antiquark type=%u slot %u\nColor: %.3f -> complement %.3f\nQuark E after: %.4f",
                i, ti, readback_energies_[i],
                spawn_type, slot,
                particles.genomes[i * GENOME_SIZE + 2],
                particles.genomes[slot * GENOME_SIZE + 2],
                readback_energies_[i]);
            iface.push_decay_event("Confinement pair", PhysicsInterface::DEVT_PAIR_PRODUCTION,
                                   ImVec4(0.3f, 0.9f, 0.4f, 1.0f), std::string(vac_detail));
        }
    }
    } // suppress_creation

    // ── Phase 4: string breaking — stretched pair → 2 bound mesons ─────────
    // In real QCD, when a color string breaks, the new q-qbar pair immediately
    // binds with the original quarks: [q_orig + qbar_new] and [q_new + qbar_orig].
    uint32_t breaks = 0;
    if (!suppress_creation) {
    for (uint32_t i = 0; i < n && breaks < MAX_STRING_BREAK; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t ti = particles.types[i];
        if (!is_quark(ti)) continue;  // matter quarks only (avoid double-counting)
        if (consumed[i]) continue;

        float color_i = particles.genomes[i * GENOME_SIZE + 2];
        uint32_t best_j = UINT32_MAX;
        float best_d2 = 0.0f;

        auto string_search = [&](uint32_t j) {
            if (j == i || consumed[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            if (!is_antiquark(particles.types[j])) return;
            float color_j = particles.genomes[j * GENOME_SIZE + 2];
            if (color_i * color_j >= 0.0f) return;
            if (std::abs(std::abs(color_i) - std::abs(color_j)) > 0.1f) return;
            glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 > STRING_BREAK_DIST * STRING_BREAK_DIST && d2 > best_d2) {
                best_d2 = d2; best_j = j;
            }
        };

        float search_r = STRING_BREAK_DIST * 2.0f;
        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                        search_r, string_search);
        else
            for (uint32_t j = 0; j < n; ++j) string_search(j);

        if (best_j == UINT32_MAX) continue;

        uint32_t slot_q = find_dormant(search_from);
        if (slot_q == UINT32_MAX) break;
        search_from = slot_q + 1;
        uint32_t slot_qbar = find_dormant(search_from);
        if (slot_qbar == UINT32_MAX) break;
        search_from = slot_qbar + 1;

        glm::vec2 axis = (readback_positions_[best_j] - readback_positions_[i]);
        float dist = std::sqrt(best_d2);
        glm::vec2 dir = (dist > 0.1f) ? axis / dist : glm::vec2(1.0f, 0.0f);
        glm::vec2 perp(-dir.y, dir.x);

        uint32_t new_q_type    = (rng() % 2 == 0) ? UP_QUARK_TYPE : DOWN_QUARK_TYPE;
        uint32_t new_qbar_type = anti_flavor(new_q_type);

        write_spawn_genome(particles, slot_q, new_q_type, rng, frame_counter_);
        particles.genomes[slot_q * GENOME_SIZE + 2] = -particles.genomes[best_j * GENOME_SIZE + 2];

        write_spawn_genome(particles, slot_qbar, new_qbar_type, rng, frame_counter_);
        particles.genomes[slot_qbar * GENOME_SIZE + 2] = -color_i;

        // Place new quarks near their binding partners (not at midpoint)
        readback_positions_[slot_q]    = readback_positions_[best_j] + perp * 2.0f;
        readback_positions_[slot_qbar] = readback_positions_[i] - perp * 2.0f;
        // Low relative velocity — bound state, not escaping
        readback_velocities_[slot_q]    = readback_velocities_[best_j] + perp * 3.0f;
        readback_velocities_[slot_qbar] = readback_velocities_[i] - perp * 3.0f;
        readback_energies_[slot_q]    = 0.2f;
        readback_energies_[slot_qbar] = 0.2f;

        // Immediately entangle as 2 mesons: [q_orig + qbar_new], [q_new + qbar_orig]
        particles.entangled_partner[i]        = slot_qbar;
        particles.entangled_partner[slot_qbar] = i;
        particles.entangled_partner[best_j]   = slot_q;
        particles.entangled_partner[slot_q]   = best_j;

        // Stronger energy drain on originals (string energy consumed)
        readback_energies_[i]      = std::max(0.05f, readback_energies_[i] - 0.25f);
        readback_energies_[best_j] = std::max(0.05f, readback_energies_[best_j] - 0.25f);

        consumed[i] = true;
        consumed[best_j] = true;
        any_changed = true;
        ++breaks;

        iface.push_notification("String break \xe2\x86\x92 2 mesons", ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
        {
            char str_detail[512];
            snprintf(str_detail, sizeof(str_detail),
                "Quark #%u (type=%u, color=%.3f) - antiquark #%u (type=%u)\nString length: %.2f px (threshold %.2f)\nNew q slot %u (type=%u) + qbar slot %u (type=%u)\nMeson E: %.4f each",
                i, ti, color_i, best_j, particles.types[best_j],
                dist, STRING_BREAK_DIST,
                slot_q, new_q_type, slot_qbar, new_qbar_type,
                readback_energies_[slot_q]);
            iface.push_decay_event("String break", PhysicsInterface::DEVT_PAIR_PRODUCTION,
                                   ImVec4(0.3f, 0.9f, 0.4f, 1.0f), std::string(str_detail));
        }
    }
    } // suppress_creation

    // ── Phase 5: gluon interactions ─────────────────────────────────────────
    // Gluons carry color charge and are confined like quarks.
    // Near quarks → absorbed (q-g vertex, transfers energy/momentum)
    // Near other gluons → merge (g+g → g, trilinear self-coupling)
    // Isolated → split into q+q̄ pair (color confinement)
    constexpr float GLUON_ABSORB_RADIUS    = 12.0f;
    constexpr float GLUON_ABSORB_RADIUS_SQ = GLUON_ABSORB_RADIUS * GLUON_ABSORB_RADIUS;
    constexpr float GLUON_SPLIT_ENERGY     = 0.2f;
    constexpr uint32_t MAX_GLUON_EVENTS    = 4;

    uint32_t gluon_events = 0;
    for (uint32_t i = 0; i < n && gluon_events < MAX_GLUON_EVENTS; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        if (consumed[i]) continue;
        if (particles.types[i] != GLUON_TYPE_PHYS) continue;

        // ── Try quark absorption first (color-aware vertex) ────────────
        // Gluon absorbed only if anticolor matches quark's color (QCD vertex)
        uint32_t nearest_quark = UINT32_MAX;
        float nearest_q_d2 = GLUON_ABSORB_RADIUS_SQ;
        float g_color_raw = particles.genomes[i * GENOME_SIZE + 2];
        float g_carried   = std::floor(std::abs(g_color_raw));           // 1,2,3
        float g_anti      = std::round(std::fmod(std::abs(g_color_raw), 1.0f) * 10.0f); // 1,2,3

        auto quark_absorb = [&](uint32_t j) {
            if (j == i || consumed[j]) return;
            if (readback_energies_[j] < 0.01f) return;
            if (!is_any_quark(particles.types[j])) return;
            // Color vertex check: gluon anticolor must match quark color
            float q_abs_color = std::abs(particles.genomes[j * GENOME_SIZE + 2]);
            bool vertex_ok = (std::abs(g_anti - q_abs_color) < 0.1f) ||
                             (std::abs(g_carried - q_abs_color) < 0.1f);
            if (!vertex_ok) return;
            glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < nearest_q_d2) { nearest_q_d2 = d2; nearest_quark = j; }
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                        GLUON_ABSORB_RADIUS, quark_absorb);
        else
            for (uint32_t j = 0; j < n; ++j) quark_absorb(j);

        if (nearest_quark != UINT32_MAX) {
            // Gluon absorbed by quark — color rotation + energy transfer
            // Quark's color rotates to the gluon's carried color
            bool is_antiquark_target = (particles.behavior_flags[particles.types[nearest_quark]] & BEHAVIOR_ANTIQUARK) != 0;
            float new_q_color = is_antiquark_target ? -g_carried : g_carried;
            particles.genomes[nearest_quark * GENOME_SIZE + 2] = new_q_color;
            readback_energies_[nearest_quark] += readback_energies_[i] * 0.5f;
            readback_energies_[nearest_quark] = std::min(readback_energies_[nearest_quark], 1.5f);
            readback_velocities_[nearest_quark] += readback_velocities_[i] * 0.3f;
            readback_energies_[i] = 0.0f;
            consumed[i] = true;
            any_changed = true;
            ++gluon_events;
            continue;
        }

        // ── Try gluon-gluon merge (trilinear self-coupling: g+g → g) ───
        uint32_t nearest_gluon = UINT32_MAX;
        float nearest_g_d2 = GLUON_ABSORB_RADIUS_SQ;

        auto gluon_merge = [&](uint32_t j) {
            if (j <= i || consumed[j]) return;  // j > i avoids double processing
            if (readback_energies_[j] < 0.01f) return;
            if (particles.types[j] != GLUON_TYPE_PHYS) return;
            glm::vec2 d = (readback_positions_[j] - readback_positions_[i]);
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < nearest_g_d2) { nearest_g_d2 = d2; nearest_gluon = j; }
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y,
                        GLUON_ABSORB_RADIUS, gluon_merge);
        else
            for (uint32_t j = 0; j < n; ++j) gluon_merge(j);

        if (nearest_gluon != UINT32_MAX) {
            // Trilinear merge: g(a,b̄) + g(b,c̄) → g(a,c̄)
            // Result: carried color from gluon i, anticolor from gluon j
            float c2_raw = particles.genomes[nearest_gluon * GENOME_SIZE + 2];
            float new_anticolor = std::round(std::fmod(std::abs(c2_raw), 1.0f) * 10.0f);
            if (new_anticolor < 0.5f) new_anticolor = g_carried; // fallback: diagonal state
            particles.genomes[i * GENOME_SIZE + 2] = g_carried + new_anticolor * 0.1f;
            readback_energies_[i] += readback_energies_[nearest_gluon] * 0.7f;
            readback_energies_[i] = std::min(readback_energies_[i], 1.5f);
            readback_velocities_[i] = (readback_velocities_[i] + readback_velocities_[nearest_gluon]) * 0.5f;
            readback_energies_[nearest_gluon] = 0.0f;
            consumed[nearest_gluon] = true;
            any_changed = true;
            ++gluon_events;
            continue;
        }

        // ── Isolated gluon — split into bound q+q̄ meson (confinement) ───
        // Quark gets gluon's carried color, antiquark gets gluon's anticolor.
        // The pair is immediately entangled as a meson (no free quarks produced).
        if (!suppress_creation && readback_energies_[i] >= GLUON_SPLIT_ENERGY) {
            uint32_t slot = find_dormant(search_from);
            if (slot == UINT32_MAX) continue;
            search_from = slot + 1;

            float q_color  = g_carried;                // quark gets carried color (positive)
            float qb_color = -(g_anti > 0.5f ? g_anti : g_carried); // antiquark gets anticolor (negative)

            // Convert gluon slot → quark
            uint32_t q_type = (rng() % 2 == 0) ? UP_QUARK_TYPE : DOWN_QUARK_TYPE;
            uint32_t qbar_type = anti_flavor(q_type);
            float half_energy = readback_energies_[i] * 0.5f;

            write_spawn_genome(particles, i, q_type, rng, frame_counter_);
            particles.genomes[i * GENOME_SIZE + 2] = q_color;
            readback_energies_[i] = half_energy;

            // Spawn antiquark very close with shared velocity (bound meson)
            float a = angle_dist_(rng);
            write_spawn_genome(particles, slot, qbar_type, rng, frame_counter_);
            particles.genomes[slot * GENOME_SIZE + 2] = qb_color;
            readback_positions_[slot]  = readback_positions_[i] + glm::vec2(std::cos(a) * 1.0f, std::sin(a) * 1.0f);
            readback_velocities_[slot] = readback_velocities_[i] + glm::vec2(std::cos(a), std::sin(a)) * 3.0f;
            readback_energies_[slot]   = half_energy;

            // Immediately entangle as meson
            particles.entangled_partner[i]    = slot;
            particles.entangled_partner[slot] = i;

            consumed[i] = true;
            any_changed = true;
            ++gluon_events;

            iface.push_notification("g \xe2\x86\x92 qq\xcc\x84 split", ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            {
                char gluon_detail[512];
                snprintf(gluon_detail, sizeof(gluon_detail),
                    "Gluon #%u E=%.4f color=%.3f\nSplit: q type=%u color=%.3f slot %u\nAnti-q type=%u color=%.3f slot %u\nHalf energy: %.4f each",
                    i, readback_energies_[i] + half_energy, g_color_raw,
                    q_type, q_color, i,
                    qbar_type, qb_color, slot,
                    half_energy);
                iface.push_decay_event("Gluon split", PhysicsInterface::DEVT_PAIR_PRODUCTION,
                                       ImVec4(0.3f, 0.9f, 0.3f, 1.0f), std::string(gluon_detail));
            }
        }
    }

    if (any_changed)
        cpu_particles_dirty_ = true;
}

// ── Bremsstrahlung: accelerated charged particles emit photons ────────────────

void PhysicsSimulation::check_bremsstrahlung() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 31337u + 271828u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    uint32_t photons_spawned = 0;
    const uint32_t MAX_BREMS = 3;

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    for (uint32_t i = 0; i < n && photons_spawned < MAX_BREMS; ++i) {
        if (readback_energies_[i] < 0.1f) continue;
        uint32_t type = particles.types[i];
        if (type >= PHYS_PARTICLE_TYPES) continue;
        float q = PHYS_CHARGE[type];
        if (std::abs(q) < 0.01f) continue;

        float gamma = compute_gamma(readback_velocities_[i]);
        float P = 0.005f * q * q * gamma * gamma;
        if (prob(rng) >= P) continue;

        uint32_t slot = find_dormant(i + 1);
        if (slot == UINT32_MAX) continue;

        // Photon carries ~10% of parent energy, perpendicular to velocity
        float E_parent = readback_energies_[i];
        float E_photon = E_parent * 0.1f;
        readback_energies_[i] -= E_photon;

        glm::vec2 vel = readback_velocities_[i];
        glm::vec2 perp = glm::normalize(glm::vec2(-vel.y, vel.x));

        write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
        readback_positions_[slot] = readback_positions_[i];
        readback_velocities_[slot] = perp * C_SIM;
        readback_energies_[slot] = mev_to_ebuf(E_photon);

        ++photons_spawned;

        const char* name = (type < PHYS_PARTICLE_TYPES) ? SM_LABELS[type] : "?";
        char msg[128];
        snprintf(msg, sizeof(msg), "Bremsstrahlung: %s #%u \xe2\x86\x92 \xce\xb3", name, i);
        iface.push_notification(msg, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));
        {
            char detail[256];
            snprintf(detail, sizeof(detail), "Source: %s #%u  q=%.0f  \xce\xb3=%.2f\nPhoton E: %.4f MeV  slot #%u",
                     name, i, PHYS_CHARGE[type], compute_gamma(readback_velocities_[i]), E_photon, slot);
            iface.push_decay_event(msg, PhysicsInterface::DEVT_BREMSSTRAHLUNG, ImVec4(0.8f, 0.8f, 1.0f, 1.0f), std::string(detail));
        }
    }

    if (photons_spawned > 0)
        cpu_particles_dirty_ = true;
}

// ── Neutrino scattering (NC + CC inverse beta decay) ─────────────────────────

void PhysicsSimulation::check_neutrino_scattering() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 48271u + 314159u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t type = particles.types[i];
        if ((particles.behavior_flags[type] & BEHAVIOR_NEUTRINO) == 0) continue;

        // NC scattering: rare random deflection
        if (prob(rng) < cfg.weak_coupling * 0.001f) {
            float theta = angle_dist(rng);
            glm::vec2 vel = readback_velocities_[i];
            float speed = glm::length(vel);
            readback_velocities_[i] = glm::vec2(std::cos(theta), std::sin(theta)) * speed;
            const char* nu_name = (type < PHYS_PARTICLE_TYPES) ? SM_LABELS[type] : "\xce\xbd";
            char nc_msg[128];
            snprintf(nc_msg, sizeof(nc_msg), "NC scatter: %s #%u deflected", nu_name, i);
            iface.push_notification(nc_msg, ImVec4(0.6f, 0.9f, 1.0f, 1.0f));
            iface.push_decay_event(nc_msg, PhysicsInterface::DEVT_NEUTRINO, ImVec4(0.6f, 0.9f, 1.0f, 1.0f));
            cpu_particles_dirty_ = true;
            continue;
        }

        // CC: νₑ + n → e⁻ + p (inverse beta decay)
        if (type != NEUTRINO_TYPE_PHYS) continue;
        if (prob(rng) >= cfg.weak_coupling * 0.002f) continue;

        // Find nearest neutron within 5px
        uint32_t best_n = UINT32_MAX;
        float best_d2 = 25.0f;
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || readback_energies_[j] < 0.01f) continue;
            if (particles.types[j] != NEUTRON_TYPE) continue;
            glm::vec2 d = readback_positions_[j] - readback_positions_[i];
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) { best_d2 = d2; best_n = j; }
        }
        if (best_n == UINT32_MAX) continue;

        // Transform: νₑ → e⁻, n → p
        glm::vec2 pos_mid = (readback_positions_[i] + readback_positions_[best_n]) * 0.5f;
        float E_avail = readback_energies_[i] + readback_energies_[best_n];
        glm::vec2 dir = glm::normalize(readback_velocities_[i]);

        write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
        readback_positions_[i] = pos_mid;
        readback_velocities_[i] = dir * ke_to_speed(E_avail * 0.4f, ELECTRON_TYPE_PHYS);
        readback_energies_[i] = mev_to_ebuf(PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS] + E_avail * 0.4f);

        write_spawn_genome(particles, best_n, PROTON_TYPE, rng, frame_counter_);
        readback_positions_[best_n] = pos_mid;
        readback_velocities_[best_n] = -dir * ke_to_speed(E_avail * 0.4f, PROTON_TYPE);
        readback_energies_[best_n] = mev_to_ebuf(PHYS_REST_MASS_MEV[PROTON_TYPE] + E_avail * 0.4f);

        iface.push_notification("CC: \xce\xbd" "e + n \xe2\x86\x92 e\xe2\x81\xbb + p", ImVec4(0.6f, 0.9f, 1.0f, 1.0f));
        {
            char detail[256];
            snprintf(detail, sizeof(detail),
                "\xce\xbd" "e #%u + n #%u \xe2\x86\x92 e- + p\nAvailable E: %.2f",
                i, best_n, E_avail);
            iface.push_decay_event("CC: \xce\xbd" "e + n \xe2\x86\x92 e\xe2\x81\xbb + p",
                PhysicsInterface::DEVT_PARTICLE_DECAY, ImVec4(0.6f, 0.9f, 1.0f, 1.0f), std::string(detail));
        }
        cpu_particles_dirty_ = true;
    }
}

// ── Neutrino flavor oscillations (PMNS matrix) ──────────────────────────────

void PhysicsSimulation::check_neutrino_oscillations() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 65537u + 161803u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    // Neutrino type triplets: νₑ(6), νμ(11), ντ(12)
    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t type = particles.types[i];
        if (type != NEUTRINO_TYPE_PHYS && type != MU_NEUTRINO_TYPE_PHYS &&
            type != TAU_NEUTRINO_TYPE_PHYS) continue;

        // Distance traveled (proxy: frames since birth × C_SIM)
        uint32_t age = frame_counter_ - ((i < particles.birth_frames.size()) ? particles.birth_frames[i] : 0);
        float L = static_cast<float>(age) * C_SIM;
        float E = std::max(readback_energies_[i], 0.001f);

        // Check oscillation channels
        float sin2_arg_21 = PMNS_DM2_21 * L * SIM_OSC_SCALE / (4.0f * E);
        float sin2_arg_32 = PMNS_DM2_32 * L * SIM_OSC_SCALE / (4.0f * E);

        uint32_t new_type = type;
        const char* osc_label = nullptr;

        if (type == NEUTRINO_TYPE_PHYS) {
            // νₑ → νμ (solar, θ₁₂)
            float P_em = PMNS_SIN2_2T12 * std::sin(sin2_arg_21) * std::sin(sin2_arg_21);
            // νₑ → ντ (reactor, θ₁₃)
            float P_et = PMNS_SIN2_2T13 * std::sin(sin2_arg_32) * std::sin(sin2_arg_32);
            float roll = prob(rng);
            if (roll < P_em) { new_type = MU_NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd" "e \xe2\x86\x92 \xce\xbd\xce\xbc"; }
            else if (roll < P_em + P_et) { new_type = TAU_NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd" "e \xe2\x86\x92 \xce\xbd\xcf\x84"; }
        } else if (type == MU_NEUTRINO_TYPE_PHYS) {
            // νμ → ντ (atmospheric, θ₂₃)
            float P_mt = PMNS_SIN2_2T23 * std::sin(sin2_arg_32) * std::sin(sin2_arg_32);
            // νμ → νₑ (solar reverse, θ₁₂)
            float P_me = PMNS_SIN2_2T12 * std::sin(sin2_arg_21) * std::sin(sin2_arg_21);
            float roll = prob(rng);
            if (roll < P_mt) { new_type = TAU_NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd\xce\xbc \xe2\x86\x92 \xce\xbd\xcf\x84"; }
            else if (roll < P_mt + P_me) { new_type = NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd\xce\xbc \xe2\x86\x92 \xce\xbd" "e"; }
        } else { // TAU_NEUTRINO
            // ντ → νμ (atmospheric reverse, θ₂₃)
            float P_tm = PMNS_SIN2_2T23 * std::sin(sin2_arg_32) * std::sin(sin2_arg_32);
            // ντ → νₑ (reactor reverse, θ₁₃)
            float P_te = PMNS_SIN2_2T13 * std::sin(sin2_arg_32) * std::sin(sin2_arg_32);
            float roll = prob(rng);
            if (roll < P_tm) { new_type = MU_NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd\xcf\x84 \xe2\x86\x92 \xce\xbd\xce\xbc"; }
            else if (roll < P_tm + P_te) { new_type = NEUTRINO_TYPE_PHYS; osc_label = "\xce\xbd\xcf\x84 \xe2\x86\x92 \xce\xbd" "e"; }
        }

        if (new_type != type) {
            // Change flavor, preserve position/velocity
            write_spawn_genome(particles, i, new_type, rng, frame_counter_);
            char osc_msg[128];
            snprintf(osc_msg, sizeof(osc_msg), "Oscillation: %s (#%u)", osc_label, i);
            iface.push_notification(osc_msg, ImVec4(0.5f, 1.0f, 0.8f, 1.0f));
            {
                char detail[256];
                snprintf(detail, sizeof(detail), "Particle #%u  L=%.1f  E=%.4f\nAge: %u frames",
                         i, static_cast<float>(age) * C_SIM, E, age);
                iface.push_decay_event(osc_msg, PhysicsInterface::DEVT_NEUTRINO, ImVec4(0.5f, 1.0f, 0.8f, 1.0f), std::string(detail));
            }
            cpu_particles_dirty_ = true;
        }
    }
}

// ── Weak flavor-changing quark scattering (W-mediated) ──────────────────────

void PhysicsSimulation::check_weak_flavor_change() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 77773u + 99991u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    auto is_quark = [](uint32_t t) { return t >= UP_QUARK_TYPE && t <= BOTTOM_QUARK_TYPE; };
    auto is_antiquark = [](uint32_t t) { return t >= ANTI_UP_TYPE && t <= ANTI_BOTTOM_TYPE; };
    // up-type index: u=0, c=1, t=2; down-type index: d=0, s=1, b=2
    auto up_idx = [](uint32_t t) -> int {
        if (t == UP_QUARK_TYPE || t == ANTI_UP_TYPE) return 0;
        if (t == CHARM_QUARK_TYPE || t == ANTI_CHARM_TYPE) return 1;
        if (t == TOP_QUARK_TYPE || t == ANTI_TOP_TYPE) return 2;
        return -1;
    };
    auto down_idx = [](uint32_t t) -> int {
        if (t == DOWN_QUARK_TYPE || t == ANTI_DOWN_TYPE) return 0;
        if (t == STRANGE_QUARK_TYPE || t == ANTI_STRANGE_TYPE) return 1;
        if (t == BOTTOM_QUARK_TYPE || t == ANTI_BOTTOM_TYPE) return 2;
        return -1;
    };
    auto is_up_type = [](uint32_t t) {
        return t == UP_QUARK_TYPE || t == CHARM_QUARK_TYPE || t == TOP_QUARK_TYPE
            || t == ANTI_UP_TYPE || t == ANTI_CHARM_TYPE || t == ANTI_TOP_TYPE;
    };

    static const uint32_t UP_TYPES[3] = { UP_QUARK_TYPE, CHARM_QUARK_TYPE, TOP_QUARK_TYPE };
    static const uint32_t DOWN_TYPES[3] = { DOWN_QUARK_TYPE, STRANGE_QUARK_TYPE, BOTTOM_QUARK_TYPE };
    static const uint32_t ANTI_UP_TYPES[3] = { ANTI_UP_TYPE, ANTI_CHARM_TYPE, ANTI_TOP_TYPE };
    static const uint32_t ANTI_DOWN_TYPES[3] = { ANTI_DOWN_TYPE, ANTI_STRANGE_TYPE, ANTI_BOTTOM_TYPE };

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.1f) continue;
        uint32_t ti = particles.types[i];
        if (!is_quark(ti) && !is_antiquark(ti)) continue;
        if (prob(rng) >= cfg.weak_coupling * 0.0005f) continue;

        // Find nearest quark within 3px
        uint32_t best_j = UINT32_MAX;
        float best_d2 = 9.0f;
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || readback_energies_[j] < 0.1f) continue;
            uint32_t tj = particles.types[j];
            if (!is_quark(tj) && !is_antiquark(tj)) continue;
            glm::vec2 d = readback_positions_[j] - readback_positions_[i];
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) { best_d2 = d2; best_j = j; }
        }
        if (best_j == UINT32_MAX) continue;

        // CKM-weighted flavor transition
        bool is_matter_i = is_quark(ti);
        int u_i = up_idx(ti), d_i = down_idx(ti);
        bool up_i = is_up_type(ti);

        // Roll CKM for new flavor
        float roll = prob(rng);
        uint32_t new_type;
        if (up_i) {
            // up-type → down-type
            int row = (u_i >= 0) ? u_i : 0;
            if (roll < CKM_BR[row][0]) new_type = is_matter_i ? DOWN_TYPES[0] : ANTI_DOWN_TYPES[0];
            else if (roll < CKM_BR[row][0] + CKM_BR[row][1]) new_type = is_matter_i ? DOWN_TYPES[1] : ANTI_DOWN_TYPES[1];
            else new_type = is_matter_i ? DOWN_TYPES[2] : ANTI_DOWN_TYPES[2];
        } else {
            // down-type → up-type (use column)
            int col = (d_i >= 0) ? d_i : 0;
            float norm = CKM_BR[0][col] + CKM_BR[1][col]; // exclude top
            if (roll < CKM_BR[0][col] / norm) new_type = is_matter_i ? UP_TYPES[0] : ANTI_UP_TYPES[0];
            else new_type = is_matter_i ? UP_TYPES[1] : ANTI_UP_TYPES[1];
        }

        if (new_type == ti) continue; // no change

        write_spawn_genome(particles, i, new_type, rng, frame_counter_);

        const char* old_name = (ti < PHYS_PARTICLE_TYPES) ? SM_LABELS[ti] : "?";
        const char* new_name = (new_type < PHYS_PARTICLE_TYPES) ? SM_LABELS[new_type] : "?";
        char msg[128];
        snprintf(msg, sizeof(msg), "Weak CC: %s \xe2\x86\x92 %s", old_name, new_name);
        iface.push_notification(msg, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
        {
            char detail[256];
            snprintf(detail, sizeof(detail), "Quark #%u: %s \xe2\x86\x92 %s\nNear quark #%u  CKM roll: %.4f",
                     i, old_name, new_name, best_j, roll);
            iface.push_decay_event(msg, PhysicsInterface::DEVT_WEAK_SCATTER, ImVec4(0.7f, 0.8f, 1.0f, 1.0f), std::string(detail));
        }
        cpu_particles_dirty_ = true;
    }
}

// ── Quantum entanglement update ──────────────────────────────────────────────

void PhysicsSimulation::update_entanglement() {
    if (!cfg.entanglement_enabled) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 7919u + 104729u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    uint32_t active_count = 0;
    bool any_changed = false;

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t p = particles.entangled_partner[i];
        if (p == 0xFFFFFFFFu || p >= n) continue;
        if (i > p) continue;  // process each pair once (lower index only)

        // Check both alive
        if (readback_energies_[i] < 0.01f || readback_energies_[p] < 0.01f) {
            particles.entangled_partner[i] = 0xFFFFFFFFu;
            particles.entangled_partner[p] = 0xFFFFFFFFu;
            continue;
        }

        // Decoherence check
        if (unit(rng) < cfg.entanglement_decoherence) {
            particles.entangled_partner[i] = 0xFFFFFFFFu;
            particles.entangled_partner[p] = 0xFFFFFFFFu;
            continue;
        }

        active_count++;

        // Velocity coupling — fraction of velocity difference applied mutually
        glm::vec2 dv = readback_velocities_[p] - readback_velocities_[i];
        float c = cfg.entanglement_coupling;
        readback_velocities_[i] += dv * c * 0.5f;
        readback_velocities_[p] -= dv * c * 0.5f;
        any_changed = true;

        // Spin anti-correlation maintenance
        float spin_i = particles.genomes[i * GENOME_SIZE + 1];
        float spin_p = particles.genomes[p * GENOME_SIZE + 1];
        if (std::abs(spin_i) > 0.01f && spin_i * spin_p > 0.0f) {
            particles.genomes[p * GENOME_SIZE + 1] = -spin_i;
        }
    }

    if (any_changed) {
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
    }
    entangled_pair_count_ = active_count;
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
        readback_positions_.resize(cfg.particle_count);
        readback_velocities_.resize(cfg.particle_count);
        readback_energies_.resize(cfg.particle_count);
        compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

        // Physics quality and skip settings
        int quality = iface.prefs.physics_quality;
        int skip    = iface.prefs.physics_skip;
        bool run_physics = (skip == 0) || (frame_counter_ % static_cast<uint32_t>(skip + 1) == 0);

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
            iface.field_gravity, iface.field_higgs,
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
            iface.field_higgs     = r.field_higgs;
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
