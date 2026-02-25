#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <cmath>
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
    // Lab Mode defaults — particle physics vacuum
    // No gravity, no external fields, near-zero temperature
    // Forces: Strong nuclear (Yukawa) >> Electromagnetic (Coulomb) at nuclear distances
    cfg.particle_count   = 5000;
    cfg.particle_types   = PHYS_PARTICLE_TYPES;
    cfg.start_empty      = true;
    cfg.environment_mode = 0;  // Lab Mode
    cfg.pool_size        = 5000;
    cfg.temperature      = 0.03f;   // computed from temperature_kelvin
    cfg.temperature_kelvin = 2.7f;  // CMB temperature — near-vacuum
    cfg.dampening        = 0.985f;  // minimal friction (vacuum); provides numerical stability
    cfg.repulsion_radius = 5.0f;    // nucleon hard-core radius
    cfg.interaction_radius = 120.0f; // EM range (electron binding)
    cfg.pressure_resistance = 60.0f; // core repulsion strength
    cfg.gravity_strength = 0.0f;    // negligible at particle scale
    cfg.lorentz_strength = 0.0f;    // no external B field
    cfg.radius           = 2.0f;
    cfg.density_limit    = 0.0f;    // legacy field viz off
    cfg.local_density_cap = 0.5f;
    cfg.viscosity_strength = 0.0f;  // vacuum — no medium
    cfg.string_tension   = 50.0f;   // quark confinement
    cfg.weak_coupling    = 0.0f;    // weak force negligible at lab energies
    cfg.higgs_vev        = 246.0f;  // standard Higgs VEV
    cfg.field_flags      = 0;

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

    physics_gen_data(particles, cfg);
    cfg.particle_count = static_cast<uint32_t>(particles.positions.size());

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

    // Click to spawn (when spawn menu pending)
    if (lmb && !lmb_down_ && iface.pending_spawn && !ImGui::GetIO().WantCaptureMouse) {
        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        glm::vec2 world_pos = cfg.camera_origin
            + (mouse_pos - glm::vec2(win_w * 0.5f, win_h * 0.5f)) / cfg.current_camera_zoom;
        do_spawn_at_world(world_pos);
    }
    lmb_down_ = lmb;
}

// ── Helper: write genome for a particle type ─────────────────────────────────

static void write_spawn_genome(Particles& particles, uint32_t slot, uint32_t type, std::mt19937& rng) {
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

    // Group template spawn
    const GroupTemplate* resolved_tmpl = nullptr;
    if (iface.spawn_group >= 0 && iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &GROUP_TEMPLATES[iface.spawn_group];
    } else if (iface.spawn_group >= GROUP_TEMPLATE_COUNT_VAL &&
               iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL + HADRON_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &HADRON_TEMPLATES[iface.spawn_group - GROUP_TEMPLATE_COUNT_VAL];
    }

    if (resolved_tmpl) {
        const auto& tmpl = *resolved_tmpl;

        // Find nucleus center (average position of nucleons in template)
        glm::vec2 nucleus_center(0.0f);
        int nucleon_count = 0;
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t t = tmpl.atoms[a].type;
            if (t == PROTON_TYPE || t == NEUTRON_TYPE || t == ANTIPROTON_TYPE_PHYS) {
                nucleus_center += glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy);
                nucleon_count++;
            }
        }
        if (nucleon_count > 0) nucleus_center /= static_cast<float>(nucleon_count);

        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) {
                if (readback_energies_[i] < 0.01f) { slot = i; break; }
            }
            if (slot == UINT32_MAX) break;

            glm::vec2 pos = world_pos + glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy);
            float rw = static_cast<float>(REGION_W);
            float rh = static_cast<float>(REGION_H);
            pos.x = std::fmod(pos.x + rw, rw);
            pos.y = std::fmod(pos.y + rh, rh);

            readback_positions_[slot] = pos;
            readback_energies_[slot] = iface.spawn_energy;

            uint32_t t = tmpl.atoms[a].type;
            bool is_electron = (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS);
            if (is_electron && nucleon_count > 0) {
                // Give electron initial orbital velocity (tangent to nucleus)
                glm::vec2 to_electron = glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy)
                                      - nucleus_center;
                float r = glm::length(to_electron);
                if (r > 1.0f) {
                    // Orbital velocity: v = L_MIN / r (centrifugal barrier ground state)
                    float v_orbital = 120.0f / std::max(r, 3.0f);
                    glm::vec2 radial = to_electron / r;
                    glm::vec2 tangent(-radial.y, radial.x);  // perpendicular
                    readback_velocities_[slot] = tangent * v_orbital;
                } else {
                    readback_velocities_[slot] = glm::vec2(0.0f);
                }
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            write_spawn_genome(particles, slot, t, rng);
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

            glm::vec2 pos = world_pos + offset;
            float rw = static_cast<float>(REGION_W);
            float rh = static_cast<float>(REGION_H);
            pos.x = std::fmod(pos.x + rw, rw);
            pos.y = std::fmod(pos.y + rh, rh);

            readback_positions_[slot] = pos;
            readback_velocities_[slot] = glm::vec2(0.0f);
            readback_energies_[slot] = iface.spawn_energy;

            write_spawn_genome(particles, slot, type, rng);
        }
    }

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
                write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng);
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

            write_spawn_genome(particles, j, NEUTRON_TYPE, rng);
            readback_energies_[i] += 0.2f;
            readback_energies_[j] += 0.2f;

            // Spawn positron
            uint32_t e_slot = find_dormant(j + 1);
            if (e_slot != UINT32_MAX) {
                used[e_slot] = true;
                glm::vec2 mid = (readback_positions_[i] + readback_positions_[j]) * 0.5f;
                glm::vec2 dir = rand_dir();
                write_spawn_genome(particles, e_slot, POSITRON_TYPE_PHYS, rng);
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
                write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng);
                readback_positions_[nu_slot] = mid;
                readback_velocities_[nu_slot] = dir * 280.0f;
                readback_energies_[nu_slot] = 0.4f;
            }
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
            write_spawn_genome(particles, slot, NEUTRON_TYPE, rng);
            readback_positions_[slot] = center + rand_dir() * 5.0f;
            readback_velocities_[slot] = rand_dir() * 150.0f;
            readback_energies_[slot] = 0.7f;
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

    // ── Step 1: Cluster nucleons into nuclei ────────────────────────────
    struct Nucleus {
        glm::vec2 center;
        int Z;      // proton count
        int total;  // total nucleon count
    };
    std::vector<Nucleus> nuclei;
    std::vector<bool> clustered(n, false);

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
        for (uint32_t mi : members) {
            nuc.center += readback_positions_[mi];
            nuc.total++;
            if (particles.types[mi] == PROTON_TYPE) nuc.Z++;
        }
        nuc.center /= static_cast<float>(nuc.total);

        if (nuc.Z > 0) nuclei.push_back(nuc);
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
                write_spawn_genome(particles, i, BOTTOM_QUARK_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.5f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }
            case ANTI_TOP_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_BOTTOM_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.5f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── W+ → positron + neutrino ──
            case W_PLUS_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.5f;
                }
                break;
            }

            // ── W- → electron + neutrino ──
            case W_MINUS_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.5f;
                }
                break;
            }

            // ── Z0 → electron + positron ──
            case Z_BOSON_TYPE_PHYS: {
                uint32_t e_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (e_slot != UINT32_MAX) {
                    write_spawn_genome(particles, e_slot, POSITRON_TYPE_PHYS, rng);
                    readback_positions_[e_slot] = pos;
                    readback_velocities_[e_slot] = -dir * FAST_SPEED;
                    readback_energies_[e_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── Higgs → 2 photons ──
            case HIGGS_TYPE_PHYS: {
                uint32_t g_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, PHOTON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * 300.0f;
                if (g_slot != UINT32_MAX) {
                    write_spawn_genome(particles, g_slot, PHOTON_TYPE_PHYS, rng);
                    readback_positions_[g_slot] = pos;
                    readback_velocities_[g_slot] = -dir * 300.0f;
                    readback_energies_[g_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── Tau → electron + neutrino_tau ──
            case TAU_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, TAU_NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.4f;
                }
                break;
            }
            case ANTITAU_TYPE_PHYS: {
                uint32_t nu_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu_slot != UINT32_MAX) {
                    write_spawn_genome(particles, nu_slot, TAU_NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu_slot] = pos;
                    readback_velocities_[nu_slot] = -dir * FAST_SPEED;
                    readback_energies_[nu_slot] = PRODUCT_ENERGY * 0.4f;
                }
                break;
            }

            // ── Bottom → charm + W ──
            case BOTTOM_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, CHARM_QUARK_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }
            case ANTI_BOTTOM_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_CHARM_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── Charm → strange + W ──
            case CHARM_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, STRANGE_QUARK_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }
            case ANTI_CHARM_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_STRANGE_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.3f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── Strange → up + W ──
            case STRANGE_QUARK_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, UP_QUARK_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.2f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_MINUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }
            case ANTI_STRANGE_TYPE: {
                uint32_t w_slot = find_dormant(i + 1);
                write_spawn_genome(particles, i, ANTI_UP_TYPE, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED * 0.2f;
                if (w_slot != UINT32_MAX) {
                    write_spawn_genome(particles, w_slot, W_PLUS_TYPE_PHYS, rng);
                    readback_positions_[w_slot] = pos;
                    readback_velocities_[w_slot] = -dir * FAST_SPEED;
                    readback_energies_[w_slot] = PRODUCT_ENERGY;
                }
                break;
            }

            // ── Muon → electron + neutrino_mu + neutrino_e ──
            case MUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * FAST_SPEED;
                    readback_energies_[nu1] = PRODUCT_ENERGY * 0.3f;
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * FAST_SPEED;
                    readback_energies_[nu2] = PRODUCT_ENERGY * 0.3f;
                }
                break;
            }
            case ANTIMUON_TYPE_PHYS: {
                uint32_t nu1 = find_dormant(i + 1);
                uint32_t nu2 = (nu1 != UINT32_MAX) ? find_dormant(nu1 + 1) : UINT32_MAX;
                write_spawn_genome(particles, i, POSITRON_TYPE_PHYS, rng);
                readback_energies_[i] = PRODUCT_ENERGY;
                readback_velocities_[i] = dir * FAST_SPEED;
                if (nu1 != UINT32_MAX) {
                    write_spawn_genome(particles, nu1, MU_NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu1] = pos;
                    readback_velocities_[nu1] = glm::vec2(-dir.y, dir.x) * FAST_SPEED;
                    readback_energies_[nu1] = PRODUCT_ENERGY * 0.3f;
                }
                if (nu2 != UINT32_MAX) {
                    write_spawn_genome(particles, nu2, NEUTRINO_TYPE_PHYS, rng);
                    readback_positions_[nu2] = pos;
                    readback_velocities_[nu2] = -dir * FAST_SPEED;
                    readback_energies_[nu2] = PRODUCT_ENERGY * 0.3f;
                }
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

// ── Per-frame tick ───────────────────────────────────────────────────────────

void PhysicsSimulation::tick(GLFWwindow* window, double dt) {
    handle_input(window, dt);
    frame_counter_++;

    // ── Temperature kelvin → noise amplitude ─────────────────────────────────
    // 300K → 0.10, 10^7K → 0.76, 10^8K → 2.0 (capped)
    cfg.temperature = std::min(2.0f,
        0.10f * std::pow(cfg.temperature_kelvin / 300.0f, 0.25f));

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
    if (is_active)
        compute.upload_dynamic_data(vk, particles);

    // Dispatch compute shader
    if (is_active && compute.is_ready()) {
        VkCommandBuffer compute_cmd = vk.begin_single_command();
        float scaled_dt = static_cast<float>(dt) * 5.0f;
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

            float snap_r = std::max(cfg.radius + 2.0f, 6.0f / cfg.current_camera_zoom);
            float min_d2 = snap_r * snap_r;
            for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
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
        update_orbitals();

        // Count active particles and type populations
        uint32_t active = 0;
        float total_energy = 0.0f;
        uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
        for (uint32_t i = 0; i < cfg.particle_count; ++i) {
            if (readback_energies_[i] > 0.01f) {
                active++;
                total_energy += readback_energies_[i];
                uint32_t t = particles.types[i];
                if (t < MAX_PARTICLE_TYPES) type_counts[t]++;
            }
        }
        iface.active_particle_display = active;
        iface.dormant_particle_display = cfg.particle_count - active;
        iface.total_energy_display = total_energy;
        iface.avg_energy_display = (active > 0) ? total_energy / active : 0.0f;
        for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t)
            iface.type_counts_display[t] = type_counts[t];
    }

    // ImGui render
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool request_reset = false;
    iface.render_imgui(cfg, particles, request_reset);

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
