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
    decay_manager.reset();
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
    // Disabled in lab mode (start_empty) — the user manually places everything via F3.
    if (cfg.spawn_enabled && is_active && !cfg.start_empty) {
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

    // ── LOD coupling: nuclear stability → macro reactivity ────────────────────
    if (sub_atomic_sim.active && iface.hover_particle_idx >= 0) {
        float instability = 1.f - sub_atomic_sim.nuclear_stability;
        auto idx = static_cast<uint32_t>(iface.hover_particle_idx);
        if (idx * GENOME_SIZE + 2 < static_cast<uint32_t>(particles.genomes.size())) {
            float& reactivity = particles.genomes[idx * GENOME_SIZE + 2];
            reactivity = std::clamp(reactivity + instability * 0.005f, 0.2f, 2.0f);
        }
    }

    // ── System stats for debug panel ─────────────────────────────────────────
    {
        // Rolling FPS (averaged over 30 frames)
        fps_acc_ += dt;
        if (++fps_frame_cnt_ >= 30) {
            iface.fps_display = 30.0f / static_cast<float>(fps_acc_);
            fps_acc_          = 0.0;
            fps_frame_cnt_    = 0;
        }

        // Active / dormant / energy / photon / SM particle counts
        uint32_t active = 0, dormant = 0, photons = 0;
        uint32_t sm[5]  = {};
        double   e_sum = 0.0, en_sum = 0.0, re_sum = 0.0;
        uint32_t genome_n = 0;
        const uint32_t np = static_cast<uint32_t>(readback_energies_.size());
        for (uint32_t pi = 0; pi < np; ++pi) {
            float    e = readback_energies_[pi];
            uint32_t t = particles.types[pi];
            if (e < 0.01f) { ++dormant; continue; }
            ++active;
            e_sum += e;
            if (t == PHOTON_TYPE)                        { ++photons; continue; }
            if (t >= ALPHA_TYPE && t <= MUON_TYPE)       { ++sm[t - ALPHA_TYPE]; continue; }
            // Genome averages (normal atoms only)
            if (pi * GENOME_SIZE + 2 < particles.genomes.size()) {
                en_sum += particles.genomes[pi * GENOME_SIZE + 1];
                re_sum += particles.genomes[pi * GENOME_SIZE + 2];
                ++genome_n;
            }
        }
        iface.active_particle_display  = active;
        iface.dormant_particle_display = dormant;
        iface.total_energy_display     = static_cast<float>(e_sum);
        iface.avg_energy_display       = active ? static_cast<float>(e_sum / active) : 0.f;
        iface.photon_count_display     = photons;
        std::copy(sm, sm + 5, iface.sm_counts_display);
        if (genome_n > 0) {
            iface.avg_electroneg_display = static_cast<float>(en_sum / genome_n);
            iface.avg_reactivity_display = static_cast<float>(re_sum / genome_n);
        }
        // Bond count (each bond stored twice — once per endpoint)
        uint32_t bond_sum = 0;
        for (uint32_t bc : bond_manager.bond_counts) bond_sum += bc;
        iface.total_bonds_display = bond_sum / 2;
    }

    // ── ImGui ──────────────────────────────────────────────────────────────────
    iface.decay_total_display   = decay_manager.total_decays;
    iface.vacuum_total_display  = vacuum_total_injections_;
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

                // ── Radioactive decay processing ──────────────────────────────
                {
                    float real_dt = static_cast<float>(dt) * static_cast<float>(cfg.bond_update_interval);
                    auto decay_events = decay_manager.update(
                        particles.types, readback_positions_,
                        readback_energies_, real_dt, cfg.particle_count);

                    if (!decay_events.empty()) {
                        // Build a sorted list of low-energy non-photon candidates for
                        // product particle injection. Cap at 64 to avoid O(n) stall.
                        static const uint32_t DECAY_CAND_MAX = 64u;
                        std::vector<uint32_t> cands;
                        cands.reserve(DECAY_CAND_MAX);
                        // Sort indices by energy ascending (reuse readback_energies_)
                        std::vector<uint32_t> order(cfg.particle_count);
                        std::iota(order.begin(), order.end(), 0u);
                        std::partial_sort(order.begin(),
                            order.begin() + static_cast<int>(std::min(
                                static_cast<uint32_t>(order.size()), DECAY_CAND_MAX * 4)),
                            order.end(),
                            [&](uint32_t a, uint32_t b){
                                return readback_energies_[a] < readback_energies_[b];
                            });
                        for (uint32_t idx : order) {
                            if (particles.types[idx] == PHOTON_TYPE) continue;
                            if (spawn_protect_ids_.count(idx)) continue;
                            cands.push_back(idx);
                            if (cands.size() >= DECAY_CAND_MAX) break;
                        }

                        // Read current GPU positions/velocities for in-place edits
                        std::vector<glm::vec2> dpos(cfg.particle_count);
                        std::vector<glm::vec2> dvel(cfg.particle_count);
                        std::vector<float>     dnrg(cfg.particle_count);
                        compute.read_current_state(vk, dpos, dvel, dnrg);

                        uint32_t cand_idx = 0;
                        std::vector<PhotonEvent> gamma_photons;

                        for (const auto& ev : decay_events) {
                            if (ev.parent_idx >= cfg.particle_count) continue;

                            // Transform parent particle in-place
                            particles.types[ev.parent_idx] = ev.daughter_type;
                            bond_manager.clear_particle_bonds(ev.parent_idx);
                            dnrg[ev.parent_idx] = std::clamp(dnrg[ev.parent_idx] - ev.q_energy * 0.5f, 0.f, 1.f);
                            // Zero partial charge on daughter (it's been transmuted)
                            particles.genomes[ev.parent_idx * GENOME_SIZE + 0] = 0.f;

                            // Inject product particle into a recycled low-energy slot
                            if (ev.product_type != 0xFFFFFFFFu && cand_idx < cands.size()) {
                                uint32_t pidx = cands[cand_idx++];
                                particles.types[pidx] = ev.product_type;
                                bond_manager.clear_particle_bonds(pidx);
                                dpos[pidx] = ev.parent_pos + glm::vec2{3.f, 3.f};
                                dvel[pidx] = ev.product_vel;
                                dnrg[pidx] = ev.q_energy;
                                // Genome: set charge for SM particle types
                                if (ev.product_type == ALPHA_TYPE)    particles.genomes[pidx*GENOME_SIZE] =  0.8f;
                                else if (ev.product_type == ELECTRON_TYPE) particles.genomes[pidx*GENOME_SIZE] = -1.0f;
                                else if (ev.product_type == POSITRON_TYPE) particles.genomes[pidx*GENOME_SIZE] =  1.0f;
                                else if (ev.product_type == NEUTRINO_TYPE) particles.genomes[pidx*GENOME_SIZE] =  0.0f;
                                else if (ev.product_type == MUON_TYPE)     particles.genomes[pidx*GENOME_SIZE] = -1.0f;
                                spawn_protect_ids_.insert(pidx);
                            }

                            // Emit gamma photon if flagged
                            if (ev.emits_gamma) {
                                float angle = float(ev.parent_idx) * 1.618f;
                                glm::vec2 dir = { std::cos(angle), std::sin(angle) };
                                gamma_photons.push_back({ ev.parent_pos, dir, 0.6f });
                            }
                        }

                        if (!decay_events.empty()) {
                            spawn_protect_ttl_ = std::max(spawn_protect_ttl_, 60);
                        }

                        // Write modified state back to GPU
                        compute.write_particle_state(vk, dpos, dvel, dnrg);
                        compute.upload_dynamic_data(vk, particles);

                        if (!gamma_photons.empty())
                            inject_photons(gamma_photons);
                    }
                }
            }

            if (need_organism) {
                organism_manager.update(readback_positions_, readback_velocities_,
                                        readback_energies_, particles.types, particles,
                                        bond_manager);
            }

            // ── Vacuum fluctuations (QFT virtual pairs + ZPE) ─────────────────
            if (need_bond)
                inject_vacuum_fluctuations();
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

    // ── WASD: camera pan ──────────────────────────────────────────────────────
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        const float PAN_SPEED = 600.0f; // world-pixels per second at zoom=1
        float pan = PAN_SPEED * static_cast<float>(dt) / cfg.current_camera_zoom;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cfg.camera_origin.y -= pan;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cfg.camera_origin.y += pan;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cfg.camera_origin.x -= pan;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cfg.camera_origin.x += pan;
    }

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

    // Choose target indices — sort by energy ascending, skip protected/photon
    std::vector<uint32_t> sorted(n);
    std::iota(sorted.begin(), sorted.end(), 0u);
    std::sort(sorted.begin(), sorted.end(),
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

    uint32_t placed = 0;
    for (uint32_t s = 0; s < n && placed < spawn_count; ++s) {
        uint32_t idx = sorted[s];
        if (particles.types[idx] == PHOTON_TYPE)   continue;
        if (spawn_protect_ids_.count(idx))          continue;  // respect F3 placements

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
        ++placed;
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
        uint32_t t = std::min(type, static_cast<uint32_t>(MAX_PARTICLE_TYPES - 1u));
        cur_pos[idx]                = pos;
        cur_vel[idx]                = { 0.0f, 0.0f };
        cur_nrg[idx]                = spawn_energy;
        particles.types[idx]        = t;
        // Set genome from atom table for elements (0-17); use SM defaults for others
        if (t < ATOM_COUNT) {
            particles.genomes[idx*4+0]  = BASE_CHARGE[t];
            particles.genomes[idx*4+1]  = BASE_ELECTRONEG[t];
            particles.genomes[idx*4+2]  = BASE_REACTIVITY[t];
            particles.genomes[idx*4+3]  = BASE_BOND_STR[t];
        } else {
            // SM particle defaults
            float sm_charge = (t == ELECTRON_TYPE || t == MUON_TYPE) ? -1.0f :
                              (t == POSITRON_TYPE  || t == ALPHA_TYPE) ?  1.0f : 0.0f;
            particles.genomes[idx*4+0] = sm_charge;
            particles.genomes[idx*4+1] = 0.5f;  // electronegativity
            particles.genomes[idx*4+2] = 1.0f;  // reactivity
            particles.genomes[idx*4+3] = 0.0f;  // bond_str
        }
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

    // ── Bio-molecule templates (Organics tab) ────────────────────────────────
    // Types: H=0 C=1 N=2 O=3 P=4 S=5
    static const MolSpec ORGANICS[8] = {
        // 0: Glycine — NH2-CH2-COOH (8 atoms)
        { {{-24,0,2},{0,0,1},{22,0,1},{32,-14,3},{32,14,3},{-34,-12,0},{-34,12,0},{0,-16,0}},
          {{0,1},{1,2},{2,3},{2,4},{0,5},{0,6},{1,7}} },
        // 1: Alanine — NH2-CH(CH3)-COOH (10 atoms)
        { {{-24,0,2},{0,0,1},{0,-22,1},{22,0,1},{32,-14,3},{32,14,3},{-34,-12,0},{-34,12,0},{-10,-32,0},{10,-32,0}},
          {{0,1},{1,2},{1,3},{3,4},{3,5},{0,6},{0,7},{2,8},{2,9}} },
        // 2: Glucose — hexose ring (11 atoms: 5C + ring-O + 5 OH)
        { {{22,0,1},{11,19,1},{-11,19,1},{-22,0,1},{-11,-19,1},{11,-19,3},{34,-10,3},{18,30,3},{-18,30,3},{-34,-10,3},{-20,-28,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{1,7},{2,8},{3,9},{4,10}} },
        // 3: Ribose — pentose ring (9 atoms: 4C + ring-O + 4 OH)
        { {{20,0,1},{12,17,1},{-12,17,1},{-20,0,1},{0,-20,3},{32,-6,3},{18,28,3},{-18,28,3},{-32,-6,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,0},{0,5},{1,6},{2,7},{3,8}} },
        // 4: Butyric acid — CH3-CH2-CH2-COOH, short fatty acid (12 atoms)
        { {{-30,0,1},{-10,0,1},{10,0,1},{30,0,1},{44,-14,3},{44,14,3},{-40,14,0},{-40,-14,0},{-10,16,0},{-10,-16,0},{10,16,0},{10,-16,0}},
          {{0,1},{1,2},{2,3},{3,4},{3,5},{0,6},{0,7},{1,8},{1,9},{2,10},{2,11}} },
        // 5: Glycerophosphate — lipid head (P + 4O + 3C + N, 10 atoms)
        { {{0,0,4},{-20,12,3},{-20,-12,3},{20,0,3},{0,-22,3},{36,0,1},{50,14,1},{50,-14,1},{66,0,2},{48,28,3}},
          {{0,1},{0,2},{0,3},{0,4},{3,5},{5,6},{5,7},{6,8},{5,9}} },
        // 6: Adenine — purine base (10 atoms: 5C + 5N in fused 6-5 ring)
        { {{0,22,2},{18,10,1},{22,-10,2},{8,-24,1},{-14,-20,1},{-20,6,1},{-30,-8,2},{-28,12,1},{-10,28,2},{-32,18,2}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{3,8},{8,7},{7,6},{6,4},{5,9}} },
        // 7: Cytosine — pyrimidine base (8 atoms: 4C + 3N + 1O in 6-ring + NH2)
        { {{-22,0,2},{0,18,1},{22,0,2},{22,-20,1},{0,-24,1},{-22,-14,1},{0,30,3},{34,-26,2}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7}} },
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
        spawn_protect_ttl_ = cfg.start_empty ? 600 : 90;
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
        spawn_protect_ttl_ = cfg.start_empty ? 600 : 90;
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
            spawn_protect_ttl_ = cfg.start_empty ? 600 : 90;
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
        spawn_protect_ttl_ = cfg.start_empty ? 600 : 90;
        for (const auto& b : tmpl_bonds) {
            if (b.ai < static_cast<int>(placed.size()) &&
                b.bi < static_cast<int>(placed.size()))
                bond_manager.force_bond(placed[b.ai], placed[b.bi]);
        }

        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
        return;
    }

    // ── Case: Organics (bio-molecules) ─────────────────────────────────────────
    if (iface.spawn_tab == 3) {
        int oi = std::clamp(iface.spawn_organic_idx, 0, 7);
        const MolSpec& mol = ORGANICS[oi];
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
        spawn_protect_ttl_ = cfg.start_empty ? 600 : 90;
        for (const auto& b : mol.bonds) {
            if (b.ai < static_cast<int>(placed.size()) &&
                b.bi < static_cast<int>(placed.size()))
                bond_manager.force_bond(placed[b.ai], placed[b.bi]);
        }
        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
        return;
    }
}

// ── Vacuum fluctuations ───────────────────────────────────────────────────────
// Injects virtual particle pairs: photon pairs (ZPE photons) and occasional
// e⁺/e⁻ pairs (fermion vacuum fluctuations). Called each bond-update cycle.

void Simulation::inject_vacuum_fluctuations() {
    if (cfg.vacuum_energy <= 0.001f) return;
    if (!compute.is_ready()) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0) return;

    static std::mt19937 vac_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> uni01(0.0f, 1.0f);
    std::uniform_real_distribution<float> pos_x(0.0f, float(REGION_W));
    std::uniform_real_distribution<float> pos_y(0.0f, float(REGION_H));
    std::uniform_real_distribution<float> ang_d(0.0f, 6.28318f);

    // Expected virtual photon pairs per bond-update cycle (avg)
    float expected_ph = cfg.vacuum_energy * 0.8f;
    int n_photon_pairs = static_cast<int>(expected_ph);
    if (uni01(vac_rng) < (expected_ph - static_cast<float>(n_photon_pairs))) ++n_photon_pairs;

    // Expected fermion pairs (rarer, ~18% of photon rate)
    float expected_fm = cfg.vacuum_energy * 0.15f;
    int n_fermion_pairs = static_cast<int>(expected_fm);
    if (uni01(vac_rng) < (expected_fm - static_cast<float>(n_fermion_pairs))) ++n_fermion_pairs;

    if (n_photon_pairs <= 0 && n_fermion_pairs <= 0) return;

    // ── Virtual photon pairs ──────────────────────────────────────────────────
    // Two low-energy photons emitted from the same point in opposite directions.
    // They drain via PHOTON_DRAIN (0.07/s) and are absorbed by nearby atoms.
    if (n_photon_pairs > 0) {
        std::vector<PhotonEvent> virt_ph;
        virt_ph.reserve(static_cast<size_t>(n_photon_pairs) * 2);
        for (int p = 0; p < n_photon_pairs; ++p) {
            glm::vec2 vpos  = { pos_x(vac_rng), pos_y(vac_rng) };
            float     a     = ang_d(vac_rng);
            glm::vec2 dir   = { std::cos(a), std::sin(a) };
            float     nrg   = 0.10f + cfg.vacuum_energy * 0.10f;  // 0.10–0.20
            virt_ph.push_back({ vpos,  dir, nrg });
            virt_ph.push_back({ vpos, -dir, nrg });
        }
        inject_photons(virt_ph);
        vacuum_total_injections_ += static_cast<uint32_t>(n_photon_pairs);
    }

    // ── Virtual fermion pairs (e⁺/e⁻) ────────────────────────────────────────
    // Particle–antiparticle pair created from vacuum; they quickly annihilate
    // (detected by DecayManager on the next bond-update cycle).
    if (n_fermion_pairs > 0) {
        // Need a fresh state read since inject_photons may have written back
        std::vector<glm::vec2> fpos(n), fvel(n);
        std::vector<float>     fnrg(n);
        compute.read_current_state(vk, fpos, fvel, fnrg);

        // Collect low-energy candidates to recycle
        const uint32_t NEED = static_cast<uint32_t>(n_fermion_pairs) * 2u + 2u;
        std::vector<uint32_t> order(n);
        std::iota(order.begin(), order.end(), 0u);
        uint32_t sort_n = std::min(NEED * 4u, n);
        std::partial_sort(order.begin(), order.begin() + static_cast<int>(sort_n), order.end(),
            [&](uint32_t a, uint32_t b){ return fnrg[a] < fnrg[b]; });

        std::vector<uint32_t> cands;
        cands.reserve(NEED);
        for (uint32_t k = 0; k < sort_n && cands.size() < NEED; ++k) {
            uint32_t idx = order[k];
            if (particles.types[idx] == PHOTON_TYPE) continue;
            if (spawn_protect_ids_.count(idx)) continue;
            cands.push_back(idx);
        }

        float vm_spd = 55.0f + cfg.vacuum_energy * 30.0f;
        uint32_t ci = 0;
        bool modified = false;

        for (int p = 0; p < n_fermion_pairs && ci + 1 < cands.size(); ++p) {
            glm::vec2 vpos = { pos_x(vac_rng), pos_y(vac_rng) };
            float     a    = ang_d(vac_rng);
            glm::vec2 dir  = { std::cos(a), std::sin(a) };

            uint32_t ip = cands[ci++];  // positron
            particles.types[ip]           = POSITRON_TYPE;
            fpos[ip]                       = vpos + glm::vec2{3.f, 0.f};
            fvel[ip]                       =  dir * vm_spd;
            fnrg[ip]                       = 0.18f;
            particles.genomes[ip*4+0]      =  1.0f;  // +charge
            particles.genomes[ip*4+1]      =  0.5f;
            particles.genomes[ip*4+2]      =  1.0f;
            particles.genomes[ip*4+3]      =  0.0f;
            bond_manager.clear_particle_bonds(ip);
            spawn_protect_ids_.insert(ip);

            uint32_t ie = cands[ci++];  // electron
            particles.types[ie]           = ELECTRON_TYPE;
            fpos[ie]                       = vpos - glm::vec2{3.f, 0.f};
            fvel[ie]                       = -dir * vm_spd;
            fnrg[ie]                       = 0.18f;
            particles.genomes[ie*4+0]      = -1.0f;  // -charge
            particles.genomes[ie*4+1]      =  0.5f;
            particles.genomes[ie*4+2]      =  1.0f;
            particles.genomes[ie*4+3]      =  0.0f;
            bond_manager.clear_particle_bonds(ie);
            spawn_protect_ids_.insert(ie);

            modified = true;
        }

        if (modified) {
            spawn_protect_ttl_ = std::max(spawn_protect_ttl_, 30);
            compute.write_particle_state(vk, fpos, fvel, fnrg);
            compute.upload_dynamic_data(vk, particles);
        }
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
