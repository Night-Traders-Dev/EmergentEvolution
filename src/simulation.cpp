#include "simulation.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <unordered_map>

// ── Init / Destroy ────────────────────────────────────────────────────────────

void Simulation::init(GLFWwindow* window) {
    // Seed the interface (random starting seed)
    iface.init();
    cfg.generation_seed = static_cast<uint32_t>(iface.seed_value);

    // Vulkan setup
    vk.init(window);
    compute.init(vk, COMPUTE_SPV);
    renderer.init(vk, window, compute);

    // Generate first particle set
    reset();
}

void Simulation::destroy() {
    vkDeviceWaitIdle(vk.device);
    compute.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void Simulation::reset() {
    vkDeviceWaitIdle(vk.device);
    particles.gen_data(cfg);
    // When start_empty is on, gen_particles() emits pool_size particles instead of
    // particle_count — sync the count so GPU buffers and push constants agree.
    cfg.particle_count = static_cast<uint32_t>(particles.positions.size());
    compute.clear_buffers(vk);
    compute.create_buffers(vk, particles);
    organism_manager.reset();
    sub_atomic_sim.reset();
    organism_tick_counter_ = 0;

    bond_manager.reset(cfg.particle_count);
    particles.bond_partners_ptr   = bond_manager.bond_partners.data();
    particles.bond_partners_count = static_cast<uint32_t>(bond_manager.bond_partners.size());
    bond_tick_counter_ = 0;
}

// ── Per-frame tick ────────────────────────────────────────────────────────────

void Simulation::tick(GLFWwindow* window, double dt) {
    // ── Spawn protection TTL ───────────────────────────────────────────────────
    if (spawn_protect_ttl_ > 0 && --spawn_protect_ttl_ == 0)
        spawn_protect_ids_.clear();

    // ── Input ──────────────────────────────────────────────────────────────────
    handle_input(window, dt);

    // ── Periodic particle spawn ────────────────────────────────────────────────
    if (cfg.spawn_enabled && is_active) {
        spawn_timer_ += dt;
        if (spawn_timer_ >= cfg.spawn_interval) {
            spawn_timer_ = 0.0;
            do_particle_spawn();
        }
    }

    // ── Upload dynamic GPU data ────────────────────────────────────────────────
    if (is_active)
        compute.upload_dynamic_data(vk, particles);

    // ── Hover detection (world-space nearest particle to mouse) ───────────────
    {
        iface.hover_particle_idx = -1;
        iface.hover_energies_ptr = &readback_energies_;
        if (!iface.mouse_within && !readback_positions_.empty()) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            int win_w, win_h;
            glfwGetWindowSize(window, &win_w, &win_h);
            glm::vec2 mw = cfg.camera_origin
                         + (glm::vec2(float(mx), float(my)) - glm::vec2(win_w * 0.5f, win_h * 0.5f))
                         / cfg.current_camera_zoom;

            // 14 screen-pixel snap radius, converted to world units
            float snap_r  = 14.0f / cfg.current_camera_zoom;
            float min_d2  = snap_r * snap_r;
            for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                glm::vec2 d = readback_positions_[pi] - mw;
                float     d2 = d.x*d.x + d.y*d.y;
                if (d2 < min_d2) { min_d2 = d2; iface.hover_particle_idx = static_cast<int32_t>(pi); }
            }
        }
    }

    // ── Sub-atomic LOD update ─────────────────────────────────────────────────
    {
        float zoom_now = cfg.current_camera_zoom;
        int new_zoom   = (zoom_now > 150.f) ? 2 : (zoom_now > 20.f) ? 1 : 0;
        int  hov_type  = -1;
        float hov_nrg  = 0.f;
        if (iface.hover_particle_idx >= 0) {
            auto pidx = static_cast<uint32_t>(iface.hover_particle_idx);
            if (pidx < static_cast<uint32_t>(particles.types.size())) {
                hov_type = static_cast<int>(particles.types[pidx]);
                if (pidx < static_cast<uint32_t>(readback_energies_.size()))
                    hov_nrg = readback_energies_[pidx];
            }
        }
        sub_atomic_sim.update(dt, hov_type, hov_nrg, new_zoom);
    }

    // ── ImGui ──────────────────────────────────────────────────────────────────
    bool request_reset = false;
    iface.render_imgui(cfg, particles, organism_manager, bond_manager, request_reset);

    if (request_reset)
        reset();

    // Sub-atomic panel — draws after main UI, still within ImGui frame
    sub_atomic_sim.render_panel(iface.hover_particle_idx, particles);

    // ── Record compute command buffer ─────────────────────────────────────────
    // We encode the compute work into a separate one-shot command buffer
    // that we submit before the render frame so the image is ready.
    if (is_active && compute.is_ready()) {
        // Use a temporary one-time command buffer for the compute pass
        VkCommandBuffer compute_cmd = vk.begin_single_command();

        float scaled_dt = static_cast<float>(dt) * 5.0f;
        compute.record(compute_cmd, cfg, scaled_dt);

        vk.end_single_command(compute_cmd);

        // Shared readback for bond + organism updates
        organism_tick_counter_++;
        bond_tick_counter_++;

        bool need_bond     = (bond_tick_counter_     % static_cast<int>(cfg.bond_update_interval) == 0);
        bool need_organism = (organism_tick_counter_  % ORGANISM_UPDATE_INTERVAL == 0);

        if (need_bond || need_organism) {
            readback_positions_.resize(cfg.particle_count);
            readback_velocities_.resize(cfg.particle_count);
            readback_energies_.resize(cfg.particle_count);
            compute.read_current_state(vk, readback_positions_, readback_velocities_,
                                       readback_energies_);

            if (need_bond) {
                bond_manager.update(readback_positions_, readback_velocities_,
                                    particles.types,
                                    cfg.bond_form_radius,
                                    cfg.bond_rest_length,
                                    cfg.bond_break_factor,
                                    cfg.bond_activation_energy);
                // Pointer is stable (vector data doesn't move without resize)
                particles.bond_partners_ptr   = bond_manager.bond_partners.data();
                particles.bond_partners_count = static_cast<uint32_t>(bond_manager.bond_partners.size());

                // Inject photons emitted by bond events (capped at 20 per tick)
                if (!bond_manager.pending_photons.empty())
                    inject_photons(bond_manager.pending_photons);
            }

            if (need_organism) {
                organism_manager.update(readback_positions_, readback_velocities_,
                                        readback_energies_, particles.types, particles,
                                        bond_manager);
            }
        }
    }

    // ── Draw frame (fullscreen quad + ImGui) ──────────────────────────────────
    if (renderer.swapchain_dirty)
        renderer.on_resize(vk, window, compute);

    renderer.draw_frame(vk, window, compute, is_active);
}

// ── Input handling ────────────────────────────────────────────────────────────

void Simulation::handle_input(GLFWwindow* window, double dt) {
    // ── Keyboard ──────────────────────────────────────────────────────────────

    // ESC: quit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    // Space: pause / unpause
    static bool space_prev = false;
    bool space_cur = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    if (space_cur && !space_prev)
        is_active = !is_active;
    space_prev = space_cur;

    // F1: toggle settings panel
    static bool f1_prev = false;
    bool f1_cur = (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS);
    if (f1_cur && !f1_prev)
        iface.settings_visible = !iface.settings_visible;
    f1_prev = f1_cur;

    // F2: reset simulation
    static bool f2_prev = false;
    bool f2_cur = (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS);
    if (f2_cur && !f2_prev)
        reset();
    f2_prev = f2_cur;

    // F3: spawn picker
    static bool f3_prev = false;
    bool f3_cur = (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS);
    if (f3_cur && !f3_prev) {
        iface.spawn_menu_visible = !iface.spawn_menu_visible;
        if (!iface.spawn_menu_visible) iface.pending_spawn = false;
    }
    f3_prev = f3_cur;

    // F11: toggle fullscreen
    static bool f11_prev = false;
    bool f11_cur = (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS);
    if (f11_cur && !f11_prev) {
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
        static bool        is_fs   = false;
        if (!is_fs) {
            glfwSetWindowMonitor(window, monitor, 0, 0,
                                 mode->width, mode->height, mode->refreshRate);
            is_fs = true;
        } else {
            glfwSetWindowMonitor(window, nullptr, 100, 100,
                                 REGION_W / 2, REGION_H / 2, 0);
            is_fs = false;
        }
    }
    f11_prev = f11_cur;

    // ── Mouse: camera pan ─────────────────────────────────────────────────────
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    glm::vec2 mouse_pos = { static_cast<float>(mx), static_cast<float>(my) };
    glm::vec2 raw_change = mouse_pos - last_mouse_pos_;
    last_mouse_pos_ = mouse_pos;

    bool lmb = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

    // ── Spawn picker: intercept LMB click before camera pan ───────────────────
    bool lmb_clicked = lmb && !lmb_down_;
    if (iface.pending_spawn && lmb_clicked && !iface.mouse_within) {
        int win_w = 0, win_h = 0;
        glfwGetWindowSize(window, &win_w, &win_h);
        glm::vec2 screen_center = { win_w * 0.5f, win_h * 0.5f };
        glm::vec2 world_pos = cfg.camera_origin
                            + (mouse_pos - screen_center) / cfg.current_camera_zoom;
        do_spawn_at_world(world_pos);
        iface.pending_spawn = false;
    }

    if ((!iface.mouse_within || !iface.settings_visible) && !iface.pending_spawn) {
        if (lmb) {
            smooth_mouse_change_ += raw_change * static_cast<float>(dt);
            cfg.camera_origin    -= smooth_mouse_change_ / cfg.current_camera_zoom;
        }
    }

    if (!lmb) {
        raw_change = {};
        cfg.camera_origin    -= smooth_mouse_change_ / cfg.current_camera_zoom;
        smooth_mouse_change_  = glm::mix(smooth_mouse_change_, glm::vec2(0.0f),
                                          static_cast<float>(dt) * 4.0f);
    }

    // ── Mouse: zoom (scroll handled by GLFW callback set in main.cpp) ─────────
    // Smoothly interpolate current_camera_zoom toward the target stored in camera_zoom.
    cfg.current_camera_zoom = glm::mix(cfg.current_camera_zoom,
                                       cfg.camera_zoom,
                                       static_cast<float>(dt) * 8.0f);

    lmb_down_ = lmb;
}

// ── Periodic particle spawn ───────────────────────────────────────────────────

void Simulation::do_particle_spawn() {
    if (!compute.is_ready()) return;

    const uint32_t n = cfg.particle_count;
    if (n == 0) return;

    // Read current state so we can find the lowest-energy candidates
    std::vector<glm::vec2> cur_pos(n), cur_vel(n);
    std::vector<float>     cur_nrg(n);
    compute.read_current_state(vk, cur_pos, cur_vel, cur_nrg);

    // How many particles to spawn this event
    static std::mt19937 spawn_rng{ std::random_device{}() };
    uint32_t spawn_count = std::uniform_int_distribution<uint32_t>(
        cfg.spawn_min, std::min(cfg.spawn_max, n))(spawn_rng);

    // Choose target indices — sort by energy ascending, pick the lowest
    std::vector<uint32_t> sorted(n);
    std::iota(sorted.begin(), sorted.end(), 0u);
    std::partial_sort(sorted.begin(), sorted.begin() + spawn_count, sorted.end(),
        [&](uint32_t a, uint32_t b){ return cur_nrg[a] < cur_nrg[b]; });

    // Pick a random world position for the spawn cluster
    float view_w = static_cast<float>(REGION_W) / cfg.current_camera_zoom;
    float view_h = static_cast<float>(REGION_H) / cfg.current_camera_zoom;
    float cx = cfg.camera_origin.x + std::uniform_real_distribution<float>(-view_w * 0.4f, view_w * 0.4f)(spawn_rng);
    float cy = cfg.camera_origin.y + std::uniform_real_distribution<float>(-view_h * 0.4f, view_h * 0.4f)(spawn_rng);
    float scatter = cfg.interaction_radius * 3.0f;

    // Atom abundance weights for type selection
    static const float RAW_ABUNDANCE[ATOM_COUNT] = {
        0.400f, 0.250f, 0.100f, 0.150f, 0.020f, 0.020f, 0.030f, 0.030f, // H C N O P S Na Cl
        0.005f, 0.002f, 0.005f, 0.003f, 0.001f, 0.001f, 0.001f, 0.001f, // Fe Ni Si Ca Ti Sr Au Pb
        0.001f, 0.001f                                                    // Eu U
    };
    uint32_t n_active = std::min(cfg.particle_types, static_cast<uint32_t>(ATOM_COUNT));
    float cum[ATOM_COUNT + 1]; cum[0] = 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < n_active; ++i) total += RAW_ABUNDANCE[i];
    for (uint32_t i = 0; i < n_active; ++i) cum[i+1] = cum[i] + RAW_ABUNDANCE[i] / total;
    cum[n_active] = 1.0f;

    // Genome defaults per atom type
    static const float BASE_CHARGE[ATOM_COUNT] = {
         0.3f, 0.0f,-0.1f,-0.4f,-0.1f,-0.2f, 0.8f,-0.8f,
         0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f,
         0.0f, 0.0f
    };
    static const float BASE_ELECTRONEG[ATOM_COUNT] = {
         0.6f, 0.6f, 0.9f, 1.6f, 0.8f, 0.9f, 0.3f, 1.1f,
         0.7f, 0.7f, 0.8f, 0.5f, 0.7f, 0.5f, 0.6f, 0.6f,
         0.6f, 0.6f
    };
    static const float BASE_REACTIVITY[ATOM_COUNT] = {
         1.0f, 0.8f, 1.2f, 1.4f, 1.0f, 1.1f, 0.6f, 0.8f,
         0.8f, 0.7f, 0.6f, 0.5f, 0.7f, 0.5f, 0.5f, 0.4f,
         1.0f, 1.2f
    };
    static const float BASE_BOND_STR[ATOM_COUNT] = {
         0.3f, 0.5f, 0.4f, 0.4f, 0.6f, 0.5f, 0.2f, 0.2f,
         0.4f, 0.3f, 0.4f, 0.2f, 0.3f, 0.2f, 0.2f, 0.3f,
         0.3f, 0.4f
    };

    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);
    std::uniform_real_distribution<float> scat(-scatter, scatter);
    std::uniform_real_distribution<float> uni01(0.0f, 1.0f);

    for (uint32_t s = 0; s < spawn_count; ++s) {
        uint32_t idx = sorted[s];

        // Position: cluster near cx,cy with scatter
        cur_pos[idx] = { cx + scat(spawn_rng), cy + scat(spawn_rng) };
        cur_vel[idx] = { 0.0f, 0.0f };
        cur_nrg[idx] = 0.8f;

        // Sample atom type from abundance
        float r = uni01(spawn_rng);
        uint32_t t = 0;
        for (uint32_t k = 0; k < n_active; ++k)
            if (r >= cum[k] && r < cum[k+1]) { t = k; break; }

        particles.types[idx] = t;

        uint32_t tc = std::min(t, static_cast<uint32_t>(ATOM_COUNT - 1));
        particles.genomes[idx*4+0] = std::clamp(BASE_CHARGE[tc]    + jitter(spawn_rng), -1.0f, 1.0f);
        particles.genomes[idx*4+1] = std::clamp(BASE_ELECTRONEG[tc] + jitter(spawn_rng),  0.2f, 2.0f);
        particles.genomes[idx*4+2] = std::clamp(BASE_REACTIVITY[tc] + jitter(spawn_rng),  0.2f, 2.0f);
        particles.genomes[idx*4+3] = std::clamp(BASE_BOND_STR[tc]   + jitter(spawn_rng), -0.5f, 0.5f);

        // Clear any existing bonds for the recycled particle
        bond_manager.clear_particle_bonds(idx);
    }

    // Push the new positions/velocities/energies to both ping-pong GPU buffers
    compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
    // Push updated types + genomes (handled by upload_dynamic_data on next frame)
    // But we call it now to avoid a one-frame flicker of old type colors
    compute.upload_dynamic_data(vk, particles);
}

// ── Photon injection ──────────────────────────────────────────────────────────

void Simulation::inject_photons(const std::vector<PhotonEvent>& events) {
    if (!compute.is_ready() || events.empty()) return;

    const uint32_t n = cfg.particle_count;
    if (n == 0) return;

    // Use the already-fresh readback buffers (called from within the bond-update block)
    auto& cur_pos = readback_positions_;
    auto& cur_vel = readback_velocities_;
    auto& cur_nrg = readback_energies_;
    if (cur_pos.size() != n) return;

    // Sort indices by energy ascending (candidates for recycling)
    std::vector<uint32_t> sorted(n);
    std::iota(sorted.begin(), sorted.end(), 0u);
    std::partial_sort(sorted.begin(), sorted.begin() + std::min<uint32_t>(20, n),
        sorted.end(), [&](uint32_t a, uint32_t b){ return cur_nrg[a] < cur_nrg[b]; });

    // Emit up to 20 photons per tick, skipping already-photon particles
    uint32_t injected = 0;
    // 200 world-px/s — slow enough to be visible across several frames,
    // fast enough to look like light relative to atom motion.
    static constexpr float PHOTON_SPEED = 200.0f;
    for (const auto& ev : events) {
        if (injected >= 20) break;

        // Find a suitable recycling candidate (non-photon, lowest-energy)
        // Threshold 0.6 so we can recycle active particles — in a busy sim
        // nearly dead (<0.1) particles rarely exist.
        uint32_t idx = n; // sentinel
        for (uint32_t s = injected; s < std::min<uint32_t>(40, n); ++s) {
            uint32_t cand = sorted[s];
            if (particles.types[cand] == PHOTON_TYPE) continue; // already a photon
            if (cur_nrg[cand] > 0.6f) break;  // sorted ascending; everything above is busier
            idx = cand;
            break;
        }
        if (idx == n) break;  // no suitable candidate found

        cur_pos[idx] = ev.position;
        cur_vel[idx] = ev.direction * PHOTON_SPEED;
        cur_nrg[idx] = ev.energy;

        particles.types[idx]           = PHOTON_TYPE;
        particles.genomes[idx*4+0]     = 0.0f;
        particles.genomes[idx*4+1]     = 0.0f;
        particles.genomes[idx*4+2]     = 0.0f;
        particles.genomes[idx*4+3]     = 0.0f;

        // Clear any bonds on the recycled particle
        bond_manager.clear_particle_bonds(idx);

        ++injected;
    }

    if (injected > 0) {
        particles.setup_photon_type();  // ensure color/flags are set
        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
    }
}

// ── Spawn at world position (F3 picker) ──────────────────────────────────────

void Simulation::do_spawn_at_world(glm::vec2 world_pos) {
    if (!compute.is_ready()) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0) return;

    // Genome defaults shared with do_particle_spawn
    static const float BASE_CHARGE[ATOM_COUNT] = {
         0.3f, 0.0f,-0.1f,-0.4f,-0.1f,-0.2f, 0.8f,-0.8f,
         0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f,
         0.0f, 0.0f
    };
    static const float BASE_ELECTRONEG[ATOM_COUNT] = {
         0.6f, 0.6f, 0.9f, 1.6f, 0.8f, 0.9f, 0.3f, 1.1f,
         0.7f, 0.7f, 0.8f, 0.5f, 0.7f, 0.5f, 0.6f, 0.6f,
         0.6f, 0.6f
    };
    static const float BASE_REACTIVITY[ATOM_COUNT] = {
         1.0f, 0.8f, 1.2f, 1.4f, 1.0f, 1.1f, 0.6f, 0.8f,
         0.8f, 0.7f, 0.6f, 0.5f, 0.7f, 0.5f, 0.5f, 0.4f,
         1.0f, 1.2f
    };
    static const float BASE_BOND_STR[ATOM_COUNT] = {
         0.3f, 0.5f, 0.4f, 0.4f, 0.6f, 0.5f, 0.2f, 0.2f,
         0.4f, 0.3f, 0.4f, 0.2f, 0.3f, 0.2f, 0.2f, 0.3f,
         0.3f, 0.4f
    };

    // Read current GPU state
    std::vector<glm::vec2> cur_pos(n), cur_vel(n);
    std::vector<float>     cur_nrg(n);
    compute.read_current_state(vk, cur_pos, cur_vel, cur_nrg);

    // Pull UI placement settings
    const float  spawn_energy  = iface.spawn_energy;
    const int    spawn_count   = std::max(1, iface.spawn_count);
    const float  spawn_scatter = iface.spawn_scatter;

    // Helper: overwrite one particle slot with energy from UI
    auto set_atom = [&](uint32_t idx, uint32_t type, glm::vec2 pos) {
        uint32_t t = std::min(type, static_cast<uint32_t>(ATOM_COUNT - 1u));
        cur_pos[idx]                = pos;
        cur_vel[idx]                = { 0.0f, 0.0f };
        cur_nrg[idx]                = spawn_energy;
        particles.types[idx]        = t;
        particles.genomes[idx*4+0]  = BASE_CHARGE[t];
        particles.genomes[idx*4+1]  = BASE_ELECTRONEG[t];
        particles.genomes[idx*4+2]  = BASE_REACTIVITY[t];
        particles.genomes[idx*4+3]  = BASE_BOND_STR[t];
        bond_manager.clear_particle_bonds(idx);
    };

    // Find low-energy non-photon candidates — need enough for spawn_count + molecule size
    const uint32_t max_cands = static_cast<uint32_t>(std::max(256, spawn_count * 2 + 32));
    std::vector<uint32_t> sorted(n);
    std::iota(sorted.begin(), sorted.end(), 0u);
    std::sort(sorted.begin(), sorted.end(),
              [&](uint32_t a, uint32_t b){ return cur_nrg[a] < cur_nrg[b]; });

    std::vector<uint32_t> candidates;
    candidates.reserve(max_cands);
    for (uint32_t idx : sorted) {
        if (particles.types[idx] == PHOTON_TYPE) continue;
        if (spawn_protect_ids_.count(idx)) continue;  // skip recently placed
        candidates.push_back(idx);
        if (static_cast<uint32_t>(candidates.size()) >= max_cands) break;
    }

    // RNG for scatter offsets
    static std::mt19937 place_rng{ std::random_device{}() };
    auto scatter_offset = [&]() -> glm::vec2 {
        if (spawn_scatter < 0.5f) return { 0.0f, 0.0f };
        std::uniform_real_distribution<float> ang_d(0.0f, 6.28318f);
        std::uniform_real_distribution<float> rad_d(0.0f, spawn_scatter);
        float a = ang_d(place_rng);
        float r = rad_d(place_rng);
        return { std::cos(a) * r, std::sin(a) * r };
    };

    // ── Molecule template definitions (Groups tab) ────────────────────────────
    // Types: H=0 C=1 N=2 O=3 P=4 S=5 Na=6 Cl=7 Fe=8 Ni=9 Si=10 Ca=11 Ti=12 Sr=13 Au=14 Pb=15 Eu=16 U=17
    struct AtomSpec { float rx, ry; uint32_t type; };
    struct BondSpec { int ai, bi; };
    struct MolSpec  { std::vector<AtomSpec> atoms; std::vector<BondSpec> bonds; };

    static const MolSpec MOLECULES[14] = {
        // 0: H2O — O + 2H, bent ~105°
        { {{0,0,3},{-14,12,0},{14,12,0}},
          {{0,1},{0,2}} },
        // 1: CH4 — C + 4H, tetrahedral
        { {{0,0,1},{0,-20,0},{20,0,0},{0,20,0},{-20,0,0}},
          {{0,1},{0,2},{0,3},{0,4}} },
        // 2: NaCl — Na + Cl, ionic pair
        { {{-13,0,6},{13,0,7}},
          {{0,1}} },
        // 3: NH3 — N + 3H, trigonal pyramidal
        { {{0,0,2},{0,-18,0},{15,10,0},{-15,10,0}},
          {{0,1},{0,2},{0,3}} },
        // 4: CO2 — C + 2O, linear
        { {{0,0,1},{-22,0,3},{22,0,3}},
          {{0,1},{0,2}} },
        // 5: Gly — simplified glycine backbone (8 atoms)
        { {{-28,0,2},{-8,0,1},{12,0,1},{26,10,3},{26,-10,3},{-36,12,0},{-36,-12,0},{-8,-16,0}},
          {{0,1},{1,2},{2,3},{2,4},{0,5},{0,6},{1,7}} },
        // 6: C6H6 — benzene ring (12 atoms: 6 C + 6 H)
        { {{22,0,1},{11,19,1},{-11,19,1},{-22,0,1},{-11,-19,1},{11,-19,1},
           {34,0,0},{17,29,0},{-17,29,0},{-34,0,0},{-17,-29,0},{17,-29,0}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},
           {0,6},{1,7},{2,8},{3,9},{4,10},{5,11}} },
        // 7: SiO4 — silicate tetrahedron (Si + 4 O in cross pattern)
        { {{0,0,10},{0,-24,3},{0,24,3},{-24,0,3},{24,0,3}},
          {{0,1},{0,2},{0,3},{0,4}} },
        // 8: Fe2O3 — hematite (2 Fe + 3 O)
        { {{-18,0,8},{18,0,8},{0,-20,3},{-26,16,3},{26,16,3}},
          {{0,2},{0,3},{1,2},{1,4},{0,1}} },
        // 9: C2H5OH — ethanol (2C + 6H + 1O = 9 atoms)
        { {{-22,0,1},{0,0,1},{18,0,3},
           {-32,-12,0},{-32,0,0},{-32,12,0},
           {0,-15,0},{0,15,0},{26,12,0}},
          {{0,1},{1,2},{0,3},{0,4},{0,5},{1,6},{1,7},{2,8}} },
        // 10: CaCO3 — calcite (Ca + C + 3 O, trigonal)
        { {{-24,0,11},{0,0,1},{18,-14,3},{18,14,3},{22,0,3}},
          {{0,1},{1,2},{1,3},{1,4}} },
        // 11: Au3 — gold trimer cluster (triangle)
        { {{0,-22,14},{-19,11,14},{19,11,14}},
          {{0,1},{1,2},{2,0}} },
        // 12: UO2 — uranium dioxide (U + 2O, linear)
        { {{0,0,17},{-26,0,3},{26,0,3}},
          {{0,1},{0,2}} },
        // 13: FeS2 — pyrite / fool's gold (Fe + 2S)
        { {{0,0,8},{-20,0,5},{20,0,5}},
          {{0,1},{0,2}} },
    };

    // ── Case: Atom(s) ─────────────────────────────────────────────────────────
    if (iface.spawn_tab == 0) {
        uint32_t place = std::min(static_cast<uint32_t>(spawn_count),
                                  static_cast<uint32_t>(candidates.size()));
        if (place == 0) return;
        uint32_t atom_type = static_cast<uint32_t>(iface.spawn_atom_type);
        for (uint32_t k = 0; k < place; ++k) {
            set_atom(candidates[k], atom_type, world_pos + scatter_offset());
            spawn_protect_ids_.insert(candidates[k]);
        }
        spawn_protect_ttl_ = 90;
        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
        return;
    }

    // ── Case: Molecule template ───────────────────────────────────────────────
    if (iface.spawn_tab == 1) {
        int gi = std::clamp(iface.spawn_group_idx, 0, 13);
        const MolSpec& mol = MOLECULES[gi];
        uint32_t need = static_cast<uint32_t>(mol.atoms.size());
        if (candidates.size() < need) return;

        std::vector<uint32_t> placed;
        placed.reserve(need);
        for (uint32_t i = 0; i < need; ++i) {
            uint32_t idx = candidates[i];
            set_atom(idx, mol.atoms[i].type,
                     world_pos + glm::vec2(mol.atoms[i].rx, mol.atoms[i].ry));
            placed.push_back(idx);
            spawn_protect_ids_.insert(idx);
        }
        spawn_protect_ttl_ = 90;
        for (const auto& b : mol.bonds) {
            if (b.ai < static_cast<int>(placed.size()) &&
                b.bi < static_cast<int>(placed.size()))
                bond_manager.force_bond(placed[b.ai], placed[b.bi]);
        }

        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
        return;
    }

    // ── Case: Organism ────────────────────────────────────────────────────────
    if (iface.spawn_tab == 2) {
        int oi = iface.spawn_organism_idx;

        if (oi >= 0) {
            // Clone a live organism
            const auto& orgs = organism_manager.organisms;
            if (oi >= static_cast<int>(orgs.size())) return;
            const Organism& org = orgs[static_cast<uint32_t>(oi)];
            const auto& members = org.particle_indices;
            uint32_t m = static_cast<uint32_t>(members.size());
            if (m == 0 || candidates.size() < m) return;

            // Compute centroid
            glm::vec2 centroid = {};
            for (uint32_t mi : members)
                centroid += (mi < readback_positions_.size()) ? readback_positions_[mi] : glm::vec2{};
            centroid /= static_cast<float>(m);
            glm::vec2 offset = world_pos - centroid;

            std::unordered_map<uint32_t,uint32_t> old_to_new;
            old_to_new.reserve(m);

            for (uint32_t s = 0; s < m; ++s) {
                uint32_t old_idx = members[s];
                uint32_t new_idx = candidates[s];
                old_to_new[old_idx] = new_idx;
                glm::vec2 orig = (old_idx < readback_positions_.size())
                                  ? readback_positions_[old_idx] : centroid;
                set_atom(new_idx, particles.types[old_idx], orig + offset);
                cur_nrg[new_idx] = (old_idx < readback_energies_.size())
                                    ? readback_energies_[old_idx] : 0.7f;
            }
            // Re-create internal bonds
            for (uint32_t s = 0; s < m; ++s) {
                uint32_t old_i = members[s];
                uint32_t new_i = old_to_new.at(old_i);
                uint32_t base  = old_i * MAX_BONDS_PER_PARTICLE;
                for (uint32_t slot = 0; slot < MAX_BONDS_PER_PARTICLE; ++slot) {
                    uint32_t old_j = bond_manager.bond_partners[base + slot];
                    if (old_j == 0xFFFFFFFFu) continue;
                    auto it = old_to_new.find(old_j);
                    if (it == old_to_new.end()) continue;
                    uint32_t new_j = it->second;
                    if (new_i < new_j)
                        bond_manager.force_bond(new_i, new_j);
                }
            }

            for (uint32_t s = 0; s < m; ++s)
                spawn_protect_ids_.insert(candidates[s]);
            spawn_protect_ttl_ = 90;
            compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
            compute.upload_dynamic_data(vk, particles);
            return;
        }

        // Predefined organism templates
        struct OrgAtomSpec { float rx, ry; uint32_t type; };
        std::vector<OrgAtomSpec> tmpl_atoms;
        std::vector<BondSpec>    tmpl_bonds;

        if (oi == -10) {
            // 5× H2O in a pentagon ring, radius 50
            for (int w = 0; w < 5; ++w) {
                float ang = w * 1.25664f;  // 2π/5
                float px  = std::cos(ang) * 50.0f;
                float py  = std::sin(ang) * 50.0f;
                int base  = static_cast<int>(tmpl_atoms.size());
                tmpl_atoms.push_back({px,        py,        3u}); // O
                tmpl_atoms.push_back({px - 13.f, py + 10.f, 0u}); // H
                tmpl_atoms.push_back({px + 13.f, py + 10.f, 0u}); // H
                tmpl_bonds.push_back({base, base+1});
                tmpl_bonds.push_back({base, base+2});
            }
        } else if (oi == -11) {
            // 4× NaCl in a 2×2 ionic grid
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    float px   = (col - 0.5f) * 32.0f;
                    float py   = (row - 0.5f) * 32.0f;
                    int   base = static_cast<int>(tmpl_atoms.size());
                    uint32_t ta = ((row + col) % 2 == 0) ? 6u : 7u;
                    uint32_t tb = (ta == 6u) ? 7u : 6u;
                    tmpl_atoms.push_back({px - 13.f, py, ta});
                    tmpl_atoms.push_back({px + 13.f, py, tb});
                    tmpl_bonds.push_back({base, base+1});
                }
            }
        } else if (oi == -12) {
            // C6H12O2: 6-carbon fatty acid chain stub
            for (int c = 0; c < 6; ++c) {
                float px  = (c - 2.5f) * 24.0f;
                int   ci  = static_cast<int>(tmpl_atoms.size());
                if (c < 5) {
                    tmpl_atoms.push_back({px,    0.f, 1u}); // C
                    tmpl_atoms.push_back({px, -18.f, 0u}); // H
                    tmpl_atoms.push_back({px,  18.f, 0u}); // H
                    tmpl_bonds.push_back({ci, ci+1});
                    tmpl_bonds.push_back({ci, ci+2});
                    if (c > 0) tmpl_bonds.push_back({ci - 3, ci}); // C-C chain
                } else {
                    tmpl_atoms.push_back({px,      0.f, 1u}); // C (carboxyl)
                    tmpl_atoms.push_back({px,    -18.f, 3u}); // =O
                    tmpl_atoms.push_back({px+16.f, 12.f, 3u}); // -OH
                    tmpl_bonds.push_back({ci, ci+1});
                    tmpl_bonds.push_back({ci, ci+2});
                    tmpl_bonds.push_back({ci - 3, ci}); // C-C chain
                }
            }
        } else {
            return;
        }

        uint32_t need = static_cast<uint32_t>(tmpl_atoms.size());
        if (candidates.size() < need) return;

        std::vector<uint32_t> placed;
        placed.reserve(need);
        for (uint32_t i = 0; i < need; ++i) {
            uint32_t idx = candidates[i];
            set_atom(idx, tmpl_atoms[i].type,
                     world_pos + glm::vec2(tmpl_atoms[i].rx, tmpl_atoms[i].ry));
            placed.push_back(idx);
            spawn_protect_ids_.insert(idx);
        }
        spawn_protect_ttl_ = 90;
        for (const auto& b : tmpl_bonds) {
            if (b.ai < static_cast<int>(placed.size()) &&
                b.bi < static_cast<int>(placed.size()))
                bond_manager.force_bond(placed[b.ai], placed[b.bi]);
        }

        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
        return;
    }
}

// ── Scroll callback (called from main.cpp) ────────────────────────────────────

// Accessed via a global pointer so the GLFW callback can reach it.
static Simulation* g_sim = nullptr;

static void scroll_callback(GLFWwindow*, double, double y_offset) {
    if (!g_sim) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;  // ImGui consumed this

    float& zoom = g_sim->cfg.camera_zoom;
    if (y_offset > 0)
        zoom *= 1.25f;
    else if (y_offset < 0)
        zoom *= 0.8f;
    zoom = std::clamp(zoom, 0.02f, 500.0f);
}

void Simulation_RegisterScrollCallback(GLFWwindow* window, Simulation* sim) {
    g_sim = sim;
    glfwSetScrollCallback(window, scroll_callback);
}
