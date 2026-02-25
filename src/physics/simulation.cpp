#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include "physics/save_load.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <random>

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

// ── Init / Destroy ───────────────────────────────────────────────────────────

void PhysicsSimulation::init(GLFWwindow* window) {
    // Defaults — all forces active, 1K temperature, all fields visualized
    cfg.particle_count     = 5000;
    cfg.particle_types     = PHYS_PARTICLE_TYPES;
    cfg.start_empty        = true;
    cfg.environment_mode   = 0;  // Lab Mode
    cfg.pool_size          = 5000;
    cfg.temperature_kelvin = 1.0f;
    cfg.temperature        = 0.30f;
    cfg.thermo_coupling    = 1.0f;
    cfg.dampening          = 0.990f;
    cfg.repulsion_radius   = 1.0f;
    cfg.interaction_radius = 200.0f;
    cfg.pressure_resistance = 100.0f;
    cfg.gravity_strength   = 0.0f;
    cfg.lorentz_strength   = 0.0f;
    cfg.magnetic_coupling  = 1.0f;
    cfg.radius             = 2.0f;
    cfg.density_limit      = 0.0f;
    cfg.local_density_cap  = 0.5f;
    cfg.viscosity_strength = 0.0f;
    cfg.string_tension     = 100.0f;
    cfg.weak_coupling      = 1.0f;
    cfg.higgs_vev          = 246.0f;
    cfg.virtual_pair_threshold    = 2.0f;
    cfg.virtual_pair_max_per_tick = 2;
    cfg.time_scale         = 1.0f;
    cfg.field_flags        = 0;  // assembled from iface bools each frame

    iface.init();
    cfg.generation_seed = static_cast<uint32_t>(iface.seed_value);

    vk.init(window);
    compute.init(vk, COMPUTE_SPV);
    renderer.init(vk, window, compute);

    reset();
}

void PhysicsSimulation::destroy() {
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

    physics_gen_data(particles, cfg);
    cfg.particle_count = static_cast<uint32_t>(particles.positions.size());

    // Particle Accelerator: auto-place EM force objects as bending magnets
    if (cfg.environment_mode == 10) {
        float cx = static_cast<float>(REGION_W) * 0.5f;
        float cy = static_cast<float>(REGION_H) * 0.5f;
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

    // No bonds in physics sim
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

    // Escape to toggle pause menu
    static bool esc_was = false;
    bool esc_now = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (esc_now && !esc_was) {
        iface.show_pause_menu = !iface.show_pause_menu;
        if (iface.show_pause_menu) is_active = false;  // pause on open
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
        glfwGetWindowSize(window, &win_w, &win_h);
        glm::vec2 world_pos = cfg.camera_origin
            + (mouse_pos - glm::vec2(win_w * 0.5f, win_h * 0.5f)) / cfg.current_camera_zoom;

        if (iface.force_obj_move_mode && iface.selected_force_obj_idx >= 0) {
            // 1. Reposition selected force object
            int idx = iface.selected_force_obj_idx;
            force_objects_[idx].x = world_pos.x;
            force_objects_[idx].y = world_pos.y;
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
                float rw = static_cast<float>(REGION_W);
                float rh = static_cast<float>(REGION_H);
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

// ── Spawn at world position ──────────────────────────────────────────────────

void PhysicsSimulation::do_spawn_at_world(glm::vec2 world_pos) {
    if (!compute.is_ready()) return;

    uint32_t n = cfg.particle_count;
    readback_positions_.resize(n);
    readback_velocities_.resize(n);
    readback_energies_.resize(n);
    compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

    uint32_t type = static_cast<uint32_t>(iface.spawn_type);

    int count = iface.spawn_count;
    float scatter = iface.spawn_scatter;
    std::mt19937 rng(static_cast<uint32_t>(world_pos.x * 1000.0f + world_pos.y));
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // ── Helper: find a dormant particle slot ────────────────────────────────
    auto find_dormant = [&](uint32_t start_from) -> uint32_t {
        for (uint32_t i = start_from; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) return i;
        }
        return UINT32_MAX;
    };

    // ── Helper: wrap position into toroidal world ─────────────────────────
    float rw = static_cast<float>(REGION_W);
    float rh = static_cast<float>(REGION_H);
    auto wrap_pos = [&](glm::vec2 p) -> glm::vec2 {
        p.x = std::fmod(p.x + rw, rw);
        p.y = std::fmod(p.y + rh, rh);
        return p;
    };

    // ── Orbital constants (must match shader + update_orbitals) ────────────
    const float R_BOHR_SPAWN = 15.0f;
    const float K_COULOMB_SPAWN = 1200.0f;
    const float SOFTEN_SQ_SPAWN = 64.0f;
    const int SHELL_CAP_SPAWN[] = {2, 8, 18};

    // ── Dynamic atom spawn (periodic table) ───────────────────────────────
    if (iface.spawn_atom_Z > 0) {
        int Z = iface.spawn_atom_Z;
        int N = iface.spawn_atom_N;
        if (N < 0) N = Z;  // fallback: equal protons and neutrons
        int A = Z + N;

        // Generate nucleon positions in compact cluster
        float nuc_spacing = 5.0f;
        int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(A)))));
        int rows_n = (A + cols - 1) / cols;
        float cx = (cols - 1) * 0.5f * nuc_spacing;
        float cy = (rows_n - 1) * 0.5f * nuc_spacing;

        // Place nucleons: alternate proton/neutron
        int protons_placed = 0, neutrons_placed = 0;
        uint32_t search_start = 0;
        std::vector<uint32_t> spawned_slots;

        for (int k = 0; k < A; ++k) {
            uint32_t slot = find_dormant(search_start);
            if (slot == UINT32_MAX) break;
            search_start = slot + 1;

            float x = (k % cols) * nuc_spacing - cx;
            float y = (k / cols) * nuc_spacing - cy;
            readback_positions_[slot] = wrap_pos(world_pos + glm::vec2(x, y));
            readback_velocities_[slot] = glm::vec2(0.0f);
            readback_energies_[slot] = iface.spawn_energy;

            // Alternate: fill protons first, then neutrons
            uint32_t ptype;
            if (protons_placed < Z) {
                ptype = PROTON_TYPE;
                protons_placed++;
            } else {
                ptype = NEUTRON_TYPE;
                neutrons_placed++;
            }
            write_spawn_genome(particles, slot, ptype, rng, frame_counter_);
            spawned_slots.push_back(slot);
        }

        // Place electrons in proper orbital shells outside the nucleus
        // Compute nucleus physical extent (half-width of the nucleon grid)
        float nuc_extent = std::max(cx, cy) + nuc_spacing * 0.5f;
        // Shell radii: start outside nucleus, each shell separated by SHELL_GAP
        const float SHELL_GAP = 15.0f;
        const float MIN_INNER_SHELL = nuc_extent + 12.0f;  // clear of nucleus surface

        int electrons_left = Z;
        int inner_electrons = 0;
        for (int shell = 0; shell < 3 && electrons_left > 0; ++shell) {
            int cap = std::min(SHELL_CAP_SPAWN[shell], electrons_left);
            float n_shell = static_cast<float>(shell + 1);

            // Slater screening: inner shells screen more effectively
            // s=0.30 for 1s peers, s=0.85 for inner shells, s=1.0 for deeper
            float screening = 0.0f;
            if (shell == 0) screening = 0.30f * std::max(0, cap - 1);  // 1s: only peer screens
            else if (shell == 1) screening = static_cast<float>(SHELL_CAP_SPAWN[0]) * 0.85f
                                           + 0.35f * std::max(0, cap - 1);
            else screening = static_cast<float>(SHELL_CAP_SPAWN[0]) * 1.0f
                           + static_cast<float>(SHELL_CAP_SPAWN[1]) * 0.85f
                           + 0.35f * std::max(0, cap - 1);
            float Z_eff = std::max(1.0f, static_cast<float>(Z) - screening);

            // Bohr-like radius with floor to stay outside nucleus
            float R_bohr = n_shell * n_shell * R_BOHR_SPAWN / Z_eff;
            float R_target = std::max(R_bohr, MIN_INNER_SHELL + shell * SHELL_GAP);

            // Compute L_ground for this shell (equilibrium: Coulomb = centrifugal)
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_SPAWN;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_SPAWN * R3 / R2_soft);
            float v_orbital = L_ground / R_target;

            for (int e = 0; e < cap; ++e) {
                uint32_t slot = find_dormant(search_start);
                if (slot == UINT32_MAX) break;
                search_start = slot + 1;

                float angle = 2.0f * 3.14159265f * static_cast<float>(e) / static_cast<float>(cap);
                glm::vec2 offset(R_target * std::cos(angle), R_target * std::sin(angle));
                glm::vec2 tangent(-std::sin(angle), std::cos(angle));

                readback_positions_[slot] = wrap_pos(world_pos + offset);
                readback_velocities_[slot] = tangent * v_orbital;
                readback_energies_[slot] = iface.spawn_energy;

                write_spawn_genome(particles, slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                // Write L_ground to genome[2] so shader uses correct orbital immediately
                particles.genomes[slot * GENOME_SIZE + 2] = L_ground;
                spawned_slots.push_back(slot);
            }

            inner_electrons += cap;
            electrons_left -= cap;
        }
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
        int shell_fill[3] = {0, 0, 0};
        struct ShellInfo { float L_ground; int shell; };
        std::vector<ShellInfo> electron_shells;
        for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
            int shell = -1;
            for (int s = 0; s < 3; ++s) {
                if (shell_fill[s] < SHELL_CAP_SPAWN[s]) { shell = s; break; }
            }
            if (shell < 0) {
                electron_shells.push_back({120.0f, 0});
                continue;
            }
            shell_fill[shell]++;
            int inner = 0;
            for (int s = 0; s < shell; ++s) inner += shell_fill[s];
            float Z_eff = std::max(1.0f, static_cast<float>(template_Z - inner));
            float n_shell = static_cast<float>(shell + 1);
            float R_target = n_shell * n_shell * R_BOHR_SPAWN / Z_eff;
            R_target = std::max(R_target, 8.0f);
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_SPAWN;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_SPAWN * R3 / R2_soft);
            electron_shells.push_back({L_ground, shell});
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
            readback_energies_[slot] = iface.spawn_energy;

            uint32_t t = tmpl.atoms[a].type;
            bool is_electron = (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS);
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
                } else {
                    readback_velocities_[slot] = glm::vec2(0.0f);
                }
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            write_spawn_genome(particles, slot, t, rng, frame_counter_);

            // Write L_ground to genome[2] for electrons
            if (is_electron && nucleon_count > 0) {
                for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
                    if (std::abs(electron_entries[ei].dx - tmpl.atoms[a].dx) < 0.1f &&
                        std::abs(electron_entries[ei].dy - tmpl.atoms[a].dy) < 0.1f) {
                        particles.genomes[slot * GENOME_SIZE + 2] = electron_shells[ei].L_ground;
                        break;
                    }
                }
            }
        }
    } else {
        // Single particle spawn
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
            readback_velocities_[slot] = glm::vec2(0.0f);
            readback_energies_[slot] = iface.spawn_energy;

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
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float CONTACT_RADIUS = 5.0f;
    const float CONTACT_RADIUS_SQ = CONTACT_RADIUS * CONTACT_RADIUS;
    const float PHOTON_SPEED = 300.0f;
    const float NEUTRINO_SPEED = 280.0f;
    const float PRODUCT_ENERGY = 0.9f;

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

        // Find nearest counterpart
        float best_dist_sq = CONTACT_RADIUS_SQ;
        uint32_t best_j = UINT32_MAX;
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || consumed[j]) continue;
            if (readback_energies_[j] < 0.01f) continue;
            if (particles.types[j] != target_type) continue;

            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_j = j;
            }
        }

        if (best_j == UINT32_MAX) continue;

        consumed[i] = true;
        consumed[best_j] = true;
        any_annihilated = true;

        glm::vec2 mid = (readback_positions_[i] + readback_positions_[best_j]) * 0.5f;
        float angle = static_cast<float>(i * 1664525u + best_j * 1013904223u) * (6.2831853f / 4294967296.0f);
        glm::vec2 dir1(std::cos(angle), std::sin(angle));
        glm::vec2 dir2 = -dir1;

        // All annihilation → 2 photons (+ optional neutrino for baryon pairs)
        particles.types[i] = PHOTON_TYPE_PHYS;
        particles.genomes[i * GENOME_SIZE + 0] = 0.0f;
        particles.genomes[i * GENOME_SIZE + 1] = 1.0f;  // photon spin
        particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
        particles.genomes[i * GENOME_SIZE + 3] = 0.0f;
        readback_positions_[i] = mid;
        readback_velocities_[i] = dir1 * PHOTON_SPEED;
        readback_energies_[i] = PRODUCT_ENERGY;

        particles.types[best_j] = PHOTON_TYPE_PHYS;
        particles.genomes[best_j * GENOME_SIZE + 0] = 0.0f;
        particles.genomes[best_j * GENOME_SIZE + 1] = 1.0f;
        particles.genomes[best_j * GENOME_SIZE + 2] = 0.0f;
        particles.genomes[best_j * GENOME_SIZE + 3] = 0.0f;
        readback_positions_[best_j] = mid;
        readback_velocities_[best_j] = dir2 * PHOTON_SPEED;
        readback_energies_[best_j] = PRODUCT_ENERGY;

        // Baryon annihilation produces extra neutrino
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
                readback_velocities_[nu_slot] = dir3 * NEUTRINO_SPEED;
                readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.5f;
            }
        }
    }

    if (any_annihilated) {
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
    }
}

// ── CPU-side nuclear fusion ──────────────────────────────────────────────────
// Detects nucleon pairs close enough and energetic enough to fuse.
// Implements: p+p chain, deuteron formation (p+n), and He-4 formation.

void PhysicsSimulation::check_fusion() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float FUSION_RADIUS = 8.0f;
    const float FUSION_RADIUS_SQ = FUSION_RADIUS * FUSION_RADIUS;
    const int MAX_FUSIONS_PER_FRAME = 5;

    int fusion_count = 0;
    bool any_fused = false;
    std::vector<bool> used(n, false);
    std::mt19937 rng(frame_counter_ * 3141592653u);

    auto rand_dir = [&]() -> glm::vec2 {
        std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
        float a = angle_dist(rng);
        return glm::vec2(std::cos(a), std::sin(a));
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

    // ── Pass 1: Proton-proton chain (p + p → p + n + e⁺ + νe) ───────────
    // Requires high energy AND high relative velocity (Coulomb barrier tunneling)
    for (uint32_t i = 0; i < n && fusion_count < MAX_FUSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < 0.8f) continue;
        if (particles.types[i] != PROTON_TYPE) continue;

        for (uint32_t j = i + 1; j < n; ++j) {
            if (used[j]) continue;
            if (readback_energies_[j] < 0.8f) continue;
            if (particles.types[j] != PROTON_TYPE) continue;

            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FUSION_RADIUS_SQ) continue;

            // Require high relative velocity (Coulomb barrier tunneling)
            glm::vec2 rel_vel = readback_velocities_[j] - readback_velocities_[i];
            float rel_speed_sq = glm::dot(rel_vel, rel_vel);
            if (rel_speed_sq < 60.0f * 60.0f) continue;

            // Convert one proton to neutron
            used[i] = true;
            used[j] = true;
            any_fused = true;
            fusion_count++;

            write_spawn_genome(particles, j, NEUTRON_TYPE, rng, frame_counter_);
            readback_energies_[i] += 0.2f;
            readback_energies_[j] += 0.2f;

            // Spawn positron
            uint32_t e_slot = find_dormant(j + 1);
            if (e_slot != UINT32_MAX) {
                used[e_slot] = true;
                glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
                glm::vec2 dir = rand_dir();
                write_spawn_genome(particles, e_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_positions_[e_slot] = mid;
                readback_velocities_[e_slot] = dir * 200.0f;
                readback_energies_[e_slot] = 0.6f;
            }

            // Spawn neutrino
            uint32_t nu_slot = find_dormant((e_slot != UINT32_MAX) ? e_slot + 1 : j + 1);
            if (nu_slot != UINT32_MAX) {
                used[nu_slot] = true;
                glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
                glm::vec2 dir = rand_dir();
                write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                readback_positions_[nu_slot] = mid;
                readback_velocities_[nu_slot] = dir * 280.0f;
                readback_energies_[nu_slot] = 0.4f;
            }
            iface.push_notification("Fusion: p + p \xe2\x86\x92 d + e\xe2\x81\xba + \xce\xbd",
                                    ImVec4(0.4f, 0.9f, 1.0f, 1.0f));
            break;
        }
    }

    // ── Pass 2: Deuteron formation (p + n → bound pair) ──────────────────
    // Requires moderate energy AND relative approach velocity
    for (uint32_t i = 0; i < n && fusion_count < MAX_FUSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < 0.6f) continue;
        if (particles.types[i] != PROTON_TYPE) continue;

        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || used[j]) continue;
            if (readback_energies_[j] < 0.6f) continue;
            if (particles.types[j] != NEUTRON_TYPE) continue;

            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FUSION_RADIUS_SQ) continue;

            // Require moderate relative velocity (energetic collision)
            glm::vec2 rel_vel = readback_velocities_[j] - readback_velocities_[i];
            float rel_speed_sq = glm::dot(rel_vel, rel_vel);
            if (rel_speed_sq < 30.0f * 30.0f) continue;

            // Bind them: move close together and match velocities
            used[i] = true;
            used[j] = true;
            any_fused = true;
            fusion_count++;

            glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
            glm::vec2 avg_vel = (readback_velocities_[i] + readback_velocities_[j]) * 0.5f;

            // Place within Yukawa binding range
            glm::vec2 sep = glm::normalize(delta + glm::vec2(0.001f, 0.0f)) * 3.0f;
            readback_positions_[i] = mid - sep * 0.5f;
            readback_positions_[j] = mid + sep * 0.5f;
            readback_velocities_[i] = avg_vel;
            readback_velocities_[j] = avg_vel;
            readback_energies_[i] += 0.15f;
            readback_energies_[j] += 0.15f;
            iface.push_notification("Fusion: p + n \xe2\x86\x92 deuteron",
                                    ImVec4(0.4f, 0.9f, 1.0f, 1.0f));
            break;
        }
    }

    if (any_fused) {
        // Clamp energies
        for (uint32_t i = 0; i < n; ++i) {
            readback_energies_[i] = std::clamp(readback_energies_[i], 0.0f, 1.0f);
        }
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
    }
}

// ── CPU-side nuclear fission ─────────────────────────────────────────────────
// Fast neutrons hitting heavy nuclei (6+ nucleons) trigger splitting.

void PhysicsSimulation::check_fission() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float CLUSTER_RADIUS = 12.0f;
    const float CLUSTER_RADIUS_SQ = CLUSTER_RADIUS * CLUSTER_RADIUS;
    const float NEUTRON_ENERGY_THRESHOLD = 0.6f;
    const int MIN_CLUSTER_SIZE = 6;
    const int MAX_FISSIONS_PER_FRAME = 2;

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
        std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
        float a = angle_dist(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    for (uint32_t i = 0; i < n && fission_count < MAX_FISSIONS_PER_FRAME; ++i) {
        if (used[i]) continue;
        if (readback_energies_[i] < NEUTRON_ENERGY_THRESHOLD) continue;
        if (particles.types[i] != NEUTRON_TYPE) continue;

        // Require fast-moving neutron (not just high stored energy)
        float neutron_speed = glm::length(readback_velocities_[i]);
        if (neutron_speed < 50.0f) continue;

        // Count nucleons near this fast neutron
        std::vector<uint32_t> cluster;
        cluster.push_back(i);
        for (uint32_t j = 0; j < n; ++j) {
            if (j == i || used[j]) continue;
            if (readback_energies_[j] < 0.01f) continue;
            uint32_t t = particles.types[j];
            if (t != PROTON_TYPE && t != NEUTRON_TYPE) continue;

            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < CLUSTER_RADIUS_SQ) {
                cluster.push_back(j);
            }
        }

        if (static_cast<int>(cluster.size()) < MIN_CLUSTER_SIZE) continue;

        // Fission! Split cluster in half with separation impulse
        any_fissioned = true;
        fission_count++;
        for (uint32_t idx : cluster) used[idx] = true;

        glm::vec2 dir = rand_dir();
        uint32_t half = static_cast<uint32_t>(cluster.size()) / 2;

        for (uint32_t c = 0; c < static_cast<uint32_t>(cluster.size()); ++c) {
            uint32_t idx = cluster[c];
            float kick = 80.0f;
            if (c < half) {
                readback_velocities_[idx] += dir * kick;
            } else {
                readback_velocities_[idx] -= dir * kick;
            }
            readback_energies_[idx] = std::min(readback_energies_[idx] + 0.4f, 1.0f);
        }

        // Spawn 2-3 free neutrons (chain reaction fuel)
        std::uniform_int_distribution<int> neutron_dist(2, 3);
        int free_neutrons = neutron_dist(rng);
        glm::vec2 center = readback_positions_[cluster[0]];
        for (int f = 0; f < free_neutrons; ++f) {
            uint32_t slot = find_dormant(0);
            if (slot == UINT32_MAX) break;
            used[slot] = true;
            write_spawn_genome(particles, slot, NEUTRON_TYPE, rng, frame_counter_);
            readback_positions_[slot] = center + rand_dir() * 5.0f;
            readback_velocities_[slot] = rand_dir() * 150.0f;
            readback_energies_[slot] = 0.7f;
        }

        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Fission: %d-nucleon cluster split + %dn",
                     static_cast<int>(cluster.size()), free_neutrons);
            iface.push_notification(msg, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        }
    }

    if (any_fissioned) {
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
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
    const float BINDING_RADIUS = 60.0f;
    const float R_BOHR = 15.0f;        // base Bohr radius (hydrogen ground state in px)
    const float K_COULOMB_F = 1200.0f;  // must match shader K_COULOMB
    const float SOFTEN_SQ = 64.0f;      // must match shader SOFTEN_MIN² (8²)

    // Shell capacities: 1s=2, 2s2p=8, 3s3p3d=18
    static const int SHELL_CAP[] = {2, 8, 18};
    static const int NUM_SHELLS = 3;
    static const int MAX_ELECTRONS = 2 + 8 + 18;  // 28

    // Clear orbital parent mapping for all particles
    particles.orbital_parent.resize(n, -1);
    std::fill(particles.orbital_parent.begin(), particles.orbital_parent.end(), -1);

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
        members.push_back(i);
        clustered[i] = true;

        for (size_t front = 0; front < members.size(); ++front) {
            uint32_t mi = members[front];
            for (uint32_t j = 0; j < n; ++j) {
                if (clustered[j]) continue;
                if (readback_energies_[j] < 0.01f) continue;
                uint32_t tj = particles.types[j];
                if (tj != PROTON_TYPE && tj != NEUTRON_TYPE) continue;

                glm::vec2 d = readback_positions_[j] - readback_positions_[mi];
                if (glm::dot(d, d) < NUCLEAR_CLUSTER_RADIUS_SQ) {
                    members.push_back(j);
                    clustered[j] = true;
                }
            }
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

    // ── Step 2: Find electrons and assign to nearest nucleus ────────────
    struct ElectronBind {
        uint32_t idx;
        int nuc_idx;
        float dist;
    };
    std::vector<ElectronBind> bindings;

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];

        // Only bind electrons to proton-nuclei (not positrons, not to anti-nuclei)
        if (t != ELECTRON_TYPE_PHYS) {
            // Clear orbital data for non-electrons that might have stale genome[2]
            if (t == POSITRON_TYPE_PHYS || t == MUON_TYPE_PHYS || t == ANTIMUON_TYPE_PHYS ||
                t == TAU_TYPE_PHYS || t == ANTITAU_TYPE_PHYS) {
                particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
            }
            continue;
        }

        float best_d = BINDING_RADIUS;
        int best_nuc = -1;
        for (int ni = 0; ni < static_cast<int>(nuclei.size()); ++ni) {
            glm::vec2 d = readback_positions_[i] - nuclei[ni].center;
            float dist = glm::length(d);
            if (dist < best_d) {
                best_d = dist;
                best_nuc = ni;
            }
        }

        if (best_nuc >= 0) {
            bindings.push_back({i, best_nuc, best_d});
            particles.orbital_parent[i] = static_cast<int32_t>(nuclei[best_nuc].rep);
        } else {
            particles.genomes[i * GENOME_SIZE + 2] = 0.0f;  // free electron
        }
    }

    // Sort by distance (closest electrons get inner shells first)
    std::sort(bindings.begin(), bindings.end(), [](const ElectronBind& a, const ElectronBind& b) {
        if (a.nuc_idx != b.nuc_idx) return a.nuc_idx < b.nuc_idx;
        return a.dist < b.dist;
    });

    // ── Step 3: Assign orbital shells and compute L_ground ──────────────
    // shell_fill[nuc_idx * NUM_SHELLS + shell] = count of electrons in that shell
    std::vector<int> shell_fill(nuclei.size() * NUM_SHELLS, 0);

    for (auto& b : bindings) {
        int Z = nuclei[b.nuc_idx].Z;

        // Count already assigned to this nucleus
        int total_assigned = 0;
        for (int s = 0; s < NUM_SHELLS; ++s)
            total_assigned += shell_fill[b.nuc_idx * NUM_SHELLS + s];

        if (total_assigned >= std::min(Z, MAX_ELECTRONS)) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;  // excess, free
            continue;
        }

        // Find first shell with room
        int shell = -1;
        for (int s = 0; s < NUM_SHELLS; ++s) {
            if (shell_fill[b.nuc_idx * NUM_SHELLS + s] < SHELL_CAP[s]) {
                shell = s;
                break;
            }
        }
        if (shell < 0) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
            continue;
        }

        shell_fill[b.nuc_idx * NUM_SHELLS + shell]++;

        // Compute target orbital radius using Bohr model with screening
        // Inner electrons screen nuclear charge
        int inner_electrons = 0;
        for (int s = 0; s < shell; ++s)
            inner_electrons += shell_fill[b.nuc_idx * NUM_SHELLS + s];

        float Z_eff = std::max(1.0f, static_cast<float>(Z - inner_electrons));
        float n_shell = static_cast<float>(shell + 1);
        float R_target = n_shell * n_shell * R_BOHR / Z_eff;
        R_target = std::max(R_target, 8.0f);  // don't go inside nucleon cluster

        // Compute L_ground: at equilibrium, F_coulomb = F_centrifugal
        // Z_eff * K / (R² + soften²) = L² / R³
        // L² = Z_eff * K * R³ / (R² + soften²)
        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_F * R3 / R2_soft);

        particles.genomes[b.idx * GENOME_SIZE + 2] = L_ground;
    }
}

// ── CPU-side particle decay ──────────────────────────────────────────────────
// Detects particles whose energy has been drained below threshold by the shader
// (via genome[3] decay_rate) and converts them to appropriate decay products.

void PhysicsSimulation::check_decay() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float DECAY_THRESHOLD = 0.08f;  // energy below which particle "decays"
    const float PRODUCT_ENERGY  = 0.6f;
    const float FAST_SPEED      = 200.0f;

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
        std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
        float a = angle_dist(rng);
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
                write_spawn_genome(particles, i, BOTTOM_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.5f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: t \xe2\x86\x92 b + W\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTI_TOP_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_BOTTOM_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.5f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: \xc4\xab \xe2\x86\x92 b\xcc\x84 + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── W+ → positron + neutrino ──
            case W_PLUS_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.5f;
                }
                iface.push_notification("Decay: W\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── W- → electron + neutrino ──
            case W_MINUS_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.5f;
                }
                iface.push_notification("Decay: W\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcc\x84", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Z0 → electron + positron ──
            case Z_BOSON_TYPE_PHYS: {
                uint32_t e_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (e_slot != UINT32_MAX) {
                    write_spawn_genome(particles, e_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[e_slot] = pos;
                    readback_velocities_[e_slot] = -dir * FAST_SPEED;
                    readback_energies_[e_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: Z\xe2\x81\xb0 \xe2\x86\x92 e\xe2\x81\xbb + e\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Higgs → 2 photons ──
            case HIGGS_TYPE_PHYS: {
                uint32_t g_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * 300.0f;
                if (g_slot != UINT32_MAX) {
                    write_spawn_genome(particles, g_slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[g_slot] = pos;
                    readback_velocities_[g_slot] = -dir * 300.0f;
                    readback_energies_[g_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: H \xe2\x86\x92 \xce\xb3 + \xce\xb3", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Tau → electron + neutrino_tau ──
            case TAU_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, TAU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.4f;
                }
                iface.push_notification("Decay: \xcf\x84 \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xcf\x84", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTITAU_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, TAU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.4f;
                }
                iface.push_notification("Decay: \xcf\x84\xcc\x84 \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xcf\x84", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Bottom → charm + W ──
            case BOTTOM_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, CHARM_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: b \xe2\x86\x92 c + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTI_BOTTOM_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_CHARM_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: b\xcc\x84 \xe2\x86\x92 c\xcc\x84 + W\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Charm → strange + W ──
            case CHARM_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, STRANGE_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: c \xe2\x86\x92 s + W\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTI_CHARM_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_STRANGE_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: c\xcc\x84 \xe2\x86\x92 s\xcc\x84 + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Strange → up + W ──
            case STRANGE_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, UP_QUARK_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.2f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: s \xe2\x86\x92 u + W\xe2\x81\xbb", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTI_STRANGE_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_UP_TYPE, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.2f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                iface.push_notification("Decay: s\xcc\x84 \xe2\x86\x92 u\xcc\x84 + W\xe2\x81\xba", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            // ── Muon → electron + neutrino_mu + neutrino_e ──
            case MUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * FAST_SPEED;
                    readback_energies_[nu1] = PRODUCT_ENERGY * 0.3f;
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * FAST_SPEED;
                    readback_energies_[nu2] = PRODUCT_ENERGY * 0.3f;
                }
                iface.push_notification("Decay: \xce\xbc\xe2\x81\xbb \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd\xce\xbc + \xce\xbd\xcc\x84" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }
            case ANTIMUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * FAST_SPEED;
                    readback_energies_[nu1] = PRODUCT_ENERGY * 0.3f;
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * FAST_SPEED;
                    readback_energies_[nu2] = PRODUCT_ENERGY * 0.3f;
                }
                iface.push_notification("Decay: \xce\xbc\xe2\x81\xba \xe2\x86\x92 e\xe2\x81\xba + \xce\xbd\xce\xbc + \xce\xbd" "e", ImVec4(1.0f, 0.8f, 0.5f, 1.0f));
                break;
            }

            default:
                any_decayed = false;  // unknown type, skip
                break;
        }
    }

    if (any_decayed) {
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
    }
}

// ── Nuclear isotope decay ────────────────────────────────────────────────────

void PhysicsSimulation::check_nuclear_decay() {
    if (detected_nuclei_.empty() || readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float EJECT_SPEED = 150.0f;
    const float PRODUCT_ENERGY = 0.6f;

    std::mt19937 rng(frame_counter_ * 1337u + 7919u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);

    auto rand_dir = [&]() -> glm::vec2 {
        float a = angle_dist(rng);
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

                // Give alpha particles velocity away from nucleus
                for (int k = 0; k < 2; ++k) {
                    readback_velocities_[alpha_p[k]] = dir * EJECT_SPEED;
                    readback_energies_[alpha_p[k]] = PRODUCT_ENERGY;
                    particles.orbital_parent[alpha_p[k]] = -1;
                    readback_velocities_[alpha_n[k]] = dir * EJECT_SPEED;
                    readback_energies_[alpha_n[k]] = PRODUCT_ENERGY;
                    particles.orbital_parent[alpha_n[k]] = -1;
                }
                // Recoil on remaining nucleus
                if (!nuc.proton_indices.empty()) {
                    for (uint32_t pi : nuc.proton_indices)
                        readback_velocities_[pi] += -dir * (EJECT_SPEED * 0.1f);
                    for (uint32_t ni : nuc.neutron_indices)
                        readback_velocities_[ni] += -dir * (EJECT_SPEED * 0.1f);
                }
                nuc.Z -= 2; nuc.N -= 2;
                any_decayed = true;
                nuclear_decay_count_++;
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 4;
                    snprintf(msg, sizeof(msg), "\xce\xb1 Decay: %s-%d \xe2\x86\x92 %s-%d + He-4",
                             element_symbol(nuc.Z + 2), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                }
                break;
            }

            case NDECAY_BETA_MINUS: {
                // Convert neutron → proton, emit electron + antineutrino
                if (nuc.neutron_indices.empty()) break;
                uint32_t ni = nuc.neutron_indices.back();
                nuc.neutron_indices.pop_back();

                // Transmute neutron to proton
                write_spawn_genome(particles, ni, PROTON_TYPE, rng, frame_counter_);
                readback_energies_[ni] = PRODUCT_ENERGY;
                nuc.proton_indices.push_back(ni);
                nuc.Z++; nuc.N--;

                // Spawn electron
                uint32_t e_slot = find_dormant(ni + 1);
                if (e_slot != UINT32_MAX) {
                    write_spawn_genome(particles, e_slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[e_slot] = readback_positions_[ni];
                    readback_velocities_[e_slot] = dir * EJECT_SPEED;
                    readback_energies_[e_slot] = PRODUCT_ENERGY * 0.5f;
                }
                // Spawn antineutrino
                uint32_t nu_slot = find_dormant(e_slot != UINT32_MAX ? e_slot + 1 : ni + 1);
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = readback_positions_[ni];
                    readback_velocities_[nu_slot] = -dir * EJECT_SPEED * 1.5f;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.3f;
                }
                any_decayed = true;
                nuclear_decay_count_++;
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "\xce\xb2\xe2\x81\xbb Decay: %s-%d \xe2\x86\x92 %s-%d + e\xe2\x81\xbb",
                             element_symbol(nuc.Z - 1), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                }
                break;
            }

            case NDECAY_BETA_PLUS: {
                // Convert proton → neutron, emit positron + neutrino
                if (nuc.proton_indices.empty()) break;
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();

                // Transmute proton to neutron
                write_spawn_genome(particles, pi, NEUTRON_TYPE, rng, frame_counter_);
                readback_energies_[pi] = PRODUCT_ENERGY;
                nuc.neutron_indices.push_back(pi);
                nuc.Z--; nuc.N++;

                // Spawn positron
                uint32_t pos_slot = find_dormant(pi + 1);
                if (pos_slot != UINT32_MAX) {
                    write_spawn_genome(particles, pos_slot, POSITRON_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[pos_slot] = readback_positions_[pi];
                    readback_velocities_[pos_slot] = dir * EJECT_SPEED;
                    readback_energies_[pos_slot] = PRODUCT_ENERGY * 0.5f;
                }
                // Spawn neutrino
                uint32_t nu_slot = find_dormant(pos_slot != UINT32_MAX ? pos_slot + 1 : pi + 1);
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng, frame_counter_);
                    readback_positions_[nu_slot] = readback_positions_[pi];
                    readback_velocities_[nu_slot] = -dir * EJECT_SPEED * 1.5f;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.3f;
                }
                any_decayed = true;
                nuclear_decay_count_++;
                {
                    char msg[128];
                    int A = nuc.Z + nuc.N;
                    snprintf(msg, sizeof(msg), "\xce\xb2\xe2\x81\xba Decay: %s-%d \xe2\x86\x92 %s-%d + e\xe2\x81\xba",
                             element_symbol(nuc.Z + 1), A,
                             element_symbol(nuc.Z), A);
                    iface.push_notification(msg, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                }
                break;
            }

            case NDECAY_NEUTRON_EMISSION: {
                // Eject one neutron from nucleus
                if (nuc.neutron_indices.empty()) break;
                uint32_t ni = nuc.neutron_indices.back();
                nuc.neutron_indices.pop_back();
                readback_velocities_[ni] = dir * EJECT_SPEED;
                readback_energies_[ni] = PRODUCT_ENERGY;
                particles.orbital_parent[ni] = -1;
                nuc.N--;
                any_decayed = true;
                nuclear_decay_count_++;
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 1;
                    snprintf(msg, sizeof(msg), "n Emission: %s-%d \xe2\x86\x92 %s-%d + n",
                             element_symbol(nuc.Z), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
                }
                break;
            }

            case NDECAY_PROTON_EMISSION: {
                // Eject one proton from nucleus
                if (nuc.proton_indices.empty()) break;
                uint32_t pi = nuc.proton_indices.back();
                nuc.proton_indices.pop_back();
                readback_velocities_[pi] = dir * EJECT_SPEED;
                readback_energies_[pi] = PRODUCT_ENERGY;
                particles.orbital_parent[pi] = -1;
                nuc.Z--;
                any_decayed = true;
                nuclear_decay_count_++;
                {
                    char msg[128];
                    int A_parent = nuc.Z + nuc.N + 1;
                    snprintf(msg, sizeof(msg), "p Emission: %s-%d \xe2\x86\x92 %s-%d + p",
                             element_symbol(nuc.Z + 1), A_parent,
                             element_symbol(nuc.Z), nuc.Z + nuc.N);
                    iface.push_notification(msg, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
                }
                break;
            }

            default:
                break;
        }
    }

    if (any_decayed) {
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
    }
}

// ── Virtual particle pair creation ───────────────────────────────────────────

void PhysicsSimulation::check_virtual_pairs() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float PAIR_RADIUS = 15.0f;
    const float PAIR_RADIUS_SQ = PAIR_RADIUS * PAIR_RADIUS;
    const float PAIR_ENERGY = 0.12f;
    const float PAIR_SPEED = 80.0f;

    uint32_t pairs_created = 0;
    uint32_t max_pairs = cfg.virtual_pair_max_per_tick;
    float threshold = cfg.virtual_pair_threshold;
    float min_energy = threshold * 0.5f;  // each particle needs at least half threshold

    std::mt19937 rng(frame_counter_ * 1664525u + 1013904223u);
    std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Helper to find a dormant slot
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    bool any_spawned = false;

    // Scan for high-energy close encounters (sparse sampling for performance)
    uint32_t step = std::max(1u, n / 500u);  // check ~500 particles per frame
    for (uint32_t i = 0; i < n && pairs_created < max_pairs; i += step) {
        float e_i = readback_energies_[i];
        if (e_i < min_energy) continue;
        if (particles.behavior_flags[particles.types[i]] & BEHAVIOR_VIRTUAL) continue;

        uint32_t type_i = particles.types[i];
        uint32_t flags_i = particles.behavior_flags[type_i];

        // Find nearest high-energy neighbor
        float best_d2 = PAIR_RADIUS_SQ;
        uint32_t best_j = UINT32_MAX;
        float e_j_best = 0.0f;
        uint32_t flags_j_best = 0;

        for (uint32_t j = 0; j < n; ++j) {
            if (j == i) continue;
            float e_j = readback_energies_[j];
            if (e_j < min_energy) continue;
            if (particles.behavior_flags[particles.types[j]] & BEHAVIOR_VIRTUAL) continue;

            glm::vec2 d = readback_positions_[j] - readback_positions_[i];
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_j = j;
                e_j_best = e_j;
                flags_j_best = particles.behavior_flags[particles.types[j]];
            }
        }

        if (best_j == UINT32_MAX) continue;
        float combined_e = e_i + e_j_best;
        if (combined_e < threshold) continue;

        // Determine virtual pair type based on interaction
        uint32_t vtype_a, vtype_b;
        bool both_quarks = (flags_i & BEHAVIOR_QUARK) && (flags_j_best & BEHAVIOR_QUARK);
        bool has_charge = (std::abs(PHYS_CHARGE[type_i]) > 0.01f) ||
                         (best_j < n && std::abs(PHYS_CHARGE[particles.types[best_j]]) > 0.01f);
        bool has_weak = (flags_i & BEHAVIOR_WEAK_BOSON) || (flags_j_best & BEHAVIOR_WEAK_BOSON);

        if (combined_e > 1.5f && unit(rng) < 0.3f) {
            // Schwinger effect: virtual e+/e- pair
            vtype_a = ELECTRON_TYPE_PHYS;
            vtype_b = POSITRON_TYPE_PHYS;
        } else if (both_quarks && unit(rng) < 0.5f) {
            // QCD: virtual gluon pair
            vtype_a = GLUON_TYPE_PHYS;
            vtype_b = GLUON_TYPE_PHYS;
        } else if (has_weak && unit(rng) < 0.3f) {
            // Weak: virtual W+/W- pair
            vtype_a = W_PLUS_TYPE_PHYS;
            vtype_b = W_MINUS_TYPE_PHYS;
        } else if (cfg.gravity_strength > 0.001f && unit(rng) < 0.2f) {
            // Gravitational: virtual graviton pair
            vtype_a = GRAVITON_TYPE_PHYS;
            vtype_b = GRAVITON_TYPE_PHYS;
        } else if (has_charge) {
            // QED: virtual photon pair (most common)
            vtype_a = PHOTON_TYPE_PHYS;
            vtype_b = PHOTON_TYPE_PHYS;
        } else {
            continue;  // no suitable virtual pair for this interaction
        }

        // Find two dormant slots
        uint32_t slot_a = find_dormant(i + 1);
        if (slot_a == UINT32_MAX) break;
        uint32_t slot_b = find_dormant(slot_a + 1);
        if (slot_b == UINT32_MAX) break;

        // Spawn at midpoint with opposite velocities
        glm::vec2 mid = (readback_positions_[i] + readback_positions_[best_j]) * 0.5f;
        float angle = angle_dist(rng);
        glm::vec2 dir(std::cos(angle), std::sin(angle));

        readback_positions_[slot_a] = mid + dir * 3.0f;
        readback_positions_[slot_b] = mid - dir * 3.0f;
        readback_velocities_[slot_a] = dir * PAIR_SPEED;
        readback_velocities_[slot_b] = -dir * PAIR_SPEED;
        readback_energies_[slot_a] = PAIR_ENERGY;
        readback_energies_[slot_b] = PAIR_ENERGY;

        // Set types and override genome decay for virtual lifetime
        write_spawn_genome(particles, slot_a, vtype_a, rng, frame_counter_);
        write_spawn_genome(particles, slot_b, vtype_b, rng, frame_counter_);
        // High per-particle decay rate → shader drains energy fast (~15 frame lifetime)
        particles.genomes[slot_a * GENOME_SIZE + 3] = 0.08f;
        particles.genomes[slot_b * GENOME_SIZE + 3] = 0.08f;

        pairs_created++;
        any_spawned = true;
    }

    if (any_spawned) {
        vkDeviceWaitIdle(vk.device);
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
    }

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
            return;
        }
    }
}

int PhysicsSimulation::hit_test_force_objects(glm::vec2 world_pos, float snap_radius) {
    float best_d2 = snap_radius * snap_radius;
    int best = -1;
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i) {
        if (!force_objects_[i].active) continue;
        glm::vec2 d(force_objects_[i].x - world_pos.x, force_objects_[i].y - world_pos.y);
        float d2 = d.x * d.x + d.y * d.y;
        if (d2 < best_d2) { best_d2 = d2; best = static_cast<int>(i); }
    }
    return best;
}

void PhysicsSimulation::recount_force_objects() {
    force_object_count_ = 0;
    for (uint32_t i = 0; i < MAX_FORCE_OBJECTS; ++i)
        if (force_objects_[i].active) force_object_count_++;
}

// ── Per-frame tick ───────────────────────────────────────────────────────────

void PhysicsSimulation::tick(GLFWwindow* window, double dt) {
    handle_input(window, dt);
    frame_counter_++;

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

    // Hover detection
    {
        iface.hover_particle_idx = -1;
        if (!readback_positions_.empty() && !ImGui::GetIO().WantCaptureMouse) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            int win_w, win_h;
            glfwGetWindowSize(window, &win_w, &win_h);
            glm::vec2 mw = cfg.camera_origin
                + (glm::vec2(float(mx), float(my)) - glm::vec2(win_w * 0.5f, win_h * 0.5f))
                / cfg.current_camera_zoom;

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

        check_annihilation();
        check_fusion();
        check_fission();
        check_decay();
        if (cfg.virtual_pairs_enabled)
            check_virtual_pairs();
        update_orbitals();
        check_nuclear_decay();

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
                float rw = static_cast<float>(REGION_W);
                float rh = static_cast<float>(REGION_H);
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
                float nuc_spacing = 5.0f;
                int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(A_dup)))));
                float cx = (cols - 1) * 0.5f * nuc_spacing;
                float cy = ((A_dup + cols - 1) / cols - 1) * 0.5f * nuc_spacing;

                int protons_placed = 0;
                std::vector<uint32_t> dup_slots;
                for (int k = 0; k < A_dup; ++k) {
                    uint32_t slot = find_dormant(search);
                    if (slot == UINT32_MAX) break;
                    search = slot + 1;
                    float x = (k % cols) * nuc_spacing - cx;
                    float y = (k / cols) * nuc_spacing - cy;
                    readback_positions_[slot] = wrap(dup_origin + glm::vec2(x, y));
                    readback_velocities_[slot] = glm::vec2(0.0f);
                    readback_energies_[slot] = 0.7f;
                    uint32_t ptype = (protons_placed < Z_dup) ? PROTON_TYPE : NEUTRON_TYPE;
                    if (ptype == PROTON_TYPE) protons_placed++;
                    write_spawn_genome(particles, slot, ptype, rng, frame_counter_);
                    dup_slots.push_back(slot);
                }

                // Place electrons in orbital shells
                const float R_BOHR = 15.0f, K_COULOMB = 1200.0f, SOFTEN_SQ = 64.0f;
                const int SHELL_CAP[] = {2, 8, 18};
                float nuc_extent = std::max(cx, cy) + nuc_spacing * 0.5f;
                const float SHELL_GAP = 15.0f;
                const float MIN_INNER = nuc_extent + 12.0f;
                int e_left = Z_dup;
                int inner_e = 0;
                for (int sh = 0; sh < 3 && e_left > 0; ++sh) {
                    int cap = std::min(SHELL_CAP[sh], e_left);
                    float n_sh = static_cast<float>(sh + 1);
                    float screening = 0.0f;
                    if (sh == 0) screening = 0.30f * std::max(0, cap - 1);
                    else if (sh == 1) screening = static_cast<float>(SHELL_CAP[0]) * 0.85f
                                                + 0.35f * std::max(0, cap - 1);
                    else screening = static_cast<float>(SHELL_CAP[0]) * 1.0f
                                   + static_cast<float>(SHELL_CAP[1]) * 0.85f
                                   + 0.35f * std::max(0, cap - 1);
                    float Z_eff = std::max(1.0f, static_cast<float>(Z_dup) - screening);
                    float R_bohr = n_sh * n_sh * R_BOHR / Z_eff;
                    float R_target = std::max(R_bohr, MIN_INNER + sh * SHELL_GAP);
                    float R3 = R_target * R_target * R_target;
                    float R2_soft = R_target * R_target + SOFTEN_SQ;
                    float L_ground = std::sqrt(Z_eff * K_COULOMB * R3 / R2_soft);
                    float v_orbital = L_ground / R_target;

                    for (int e = 0; e < cap; ++e) {
                        uint32_t slot = find_dormant(search);
                        if (slot == UINT32_MAX) break;
                        search = slot + 1;
                        float angle = 2.0f * 3.14159265f * static_cast<float>(e) / static_cast<float>(cap);
                        glm::vec2 offset(R_target * std::cos(angle), R_target * std::sin(angle));
                        glm::vec2 tangent(-std::sin(angle), std::cos(angle));
                        readback_positions_[slot] = wrap(dup_origin + offset);
                        readback_velocities_[slot] = tangent * v_orbital;
                        readback_energies_[slot] = 0.7f;
                        write_spawn_genome(particles, slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                        particles.genomes[slot * GENOME_SIZE + 2] = L_ground;
                        dup_slots.push_back(slot);
                    }
                    inner_e += cap;
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
        }
    }

    // Thread readback data to interface for info card display
    iface.frame_counter_display = frame_counter_;
    iface.readback_velocities = readback_velocities_.data();
    iface.readback_count = cfg.particle_count;
    iface.nuclear_decay_count_display = nuclear_decay_count_;

    // Populate element list for UI (skip free neutrons / Z==0)
    iface.element_list.clear();
    for (auto& nuc : detected_nuclei_) {
        if (nuc.Z <= 0) continue;
        int electrons = 0;
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            if (particles.types[i] == ELECTRON_TYPE_PHYS &&
                particles.orbital_parent[i] == static_cast<int32_t>(nuc.rep))
                electrons++;
        }
        iface.element_list.push_back({nuc.Z, nuc.N, electrons, nuc.rep});
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
    }

    // ── Load request ─────────────────────────────────────────────────────────
    if (iface.request_load) {
        iface.request_load = false;
        auto r = load_simulation(iface.save_filename);
        if (r.success) {
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
            std::memcpy(particles.forces.data(), r.forces,
                        MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
            std::memcpy(particles.colors.data(), r.colors,
                        MAX_PARTICLE_TYPES * sizeof(glm::vec4));
            std::memcpy(particles.behavior_flags, r.behavior_flags,
                        MAX_PARTICLE_TYPES * sizeof(uint32_t));
            // Restore force objects
            force_object_count_ = r.force_object_count;
            std::memcpy(force_objects_, r.force_objects,
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

    ImGui::Render();

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
