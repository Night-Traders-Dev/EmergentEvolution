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

            // Snap radius: at least particle-radius + 2 world units, but never smaller
            // than 6 screen pixels worth of world space (so it still works when zoomed out).
            float world_min = 6.0f / cfg.current_camera_zoom;
            float snap_r    = std::max(cfg.radius + 2.0f, world_min);
            float min_d2    = snap_r * snap_r;
            const float RW  = static_cast<float>(REGION_W);
            const float RH  = static_cast<float>(REGION_H);
            for (uint32_t pi = 0; pi < static_cast<uint32_t>(readback_positions_.size()); ++pi) {
                glm::vec2 d = readback_positions_[pi] - mw;
                // Shortest-path toroidal distance
                if (d.x >  RW * 0.5f) d.x -= RW;
                else if (d.x < -RW * 0.5f) d.x += RW;
                if (d.y >  RH * 0.5f) d.y -= RH;
                else if (d.y < -RH * 0.5f) d.y += RH;
                float d2 = d.x*d.x + d.y*d.y;
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
        uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
        double   e_sum = 0.0, en_sum = 0.0, re_sum = 0.0;
        uint32_t genome_n = 0;
        const uint32_t np = static_cast<uint32_t>(readback_energies_.size());
        for (uint32_t pi = 0; pi < np; ++pi) {
            float    e = readback_energies_[pi];
            uint32_t t = particles.types[pi];
            if (e < 0.01f) { ++dormant; continue; }
            ++active;
            e_sum += e;
            if (t < MAX_PARTICLE_TYPES) ++type_counts[t];
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
        std::copy(type_counts, type_counts + MAX_PARTICLE_TYPES, iface.type_counts_display);
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

    if (request_reset) {
        // Apply pending particle count from slider before reset
        if (!cfg.start_empty) {
            int pc = static_cast<int>(std::max(2.0f,
                std::pow(iface.particle_count_slider, 2.0f)));
            cfg.particle_count = static_cast<uint32_t>(pc);
        } else {
            cfg.particle_count = 10000;
        }
        cfg.particle_types = static_cast<uint32_t>(iface.particle_types_slider);
        reset();
    }

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
                                if      (ev.product_type == ALPHA_TYPE)        particles.genomes[pidx*GENOME_SIZE] =  0.8f;
                                else if (ev.product_type == ELECTRON_TYPE)    particles.genomes[pidx*GENOME_SIZE] = -1.0f;
                                else if (ev.product_type == POSITRON_TYPE)    particles.genomes[pidx*GENOME_SIZE] =  1.0f;
                                else if (ev.product_type == NEUTRINO_TYPE)    particles.genomes[pidx*GENOME_SIZE] =  0.0f;
                                else if (ev.product_type == MUON_TYPE)        particles.genomes[pidx*GENOME_SIZE] = -1.0f;
                                else if (ev.product_type == TAU_TYPE)         particles.genomes[pidx*GENOME_SIZE] = -1.0f;
                                else if (ev.product_type == MU_NEUTRINO_TYPE) particles.genomes[pidx*GENOME_SIZE] =  0.0f;
                                else if (ev.product_type == TAU_NEUTRINO_TYPE)particles.genomes[pidx*GENOME_SIZE] =  0.0f;
                                else if (ev.product_type == W_BOSON_TYPE)     particles.genomes[pidx*GENOME_SIZE] =  1.0f;
                                else if (ev.product_type == Z_BOSON_TYPE)     particles.genomes[pidx*GENOME_SIZE] =  0.0f;
                                else if (ev.product_type == HIGGS_TYPE)       particles.genomes[pidx*GENOME_SIZE] =  0.0f;
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

            // ── Cosmic ray injection (environment mode 5: Deep Space) ─────────
            if (need_bond && cfg.environment_mode == 5)
                inject_cosmic_rays();
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
    if (f2_cur && !f2_prev) {
        if (!cfg.start_empty) {
            int pc = static_cast<int>(std::max(2.0f,
                std::pow(iface.particle_count_slider, 2.0f)));
            cfg.particle_count = static_cast<uint32_t>(pc);
        } else {
            cfg.particle_count = 10000;
        }
        cfg.particle_types = static_cast<uint32_t>(iface.particle_types_slider);
        reset();
    }
    f2_prev = f2_cur;

    // F3: spawn picker
    static bool f3_prev = false;
    bool f3_cur = (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS);
    if (f3_cur && !f3_prev) {
        iface.spawn_menu_visible = !iface.spawn_menu_visible;
        if (!iface.spawn_menu_visible) iface.pending_spawn = false;
    }
    f3_prev = f3_cur;

    // F4: quantum field display
    static bool f4_prev = false;
    bool f4_cur = (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS);
    if (f4_cur && !f4_prev)
        iface.quantum_field_visible = !iface.quantum_field_visible;
    f4_prev = f4_cur;

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

    static const MolSpec MOLECULES[49] = {
        // ── Common molecules (original 14) ───────────────────────────────────
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

        // ── Atmospheric gases ─────────────────────────────────────────────────
        // 14: O2 — dioxygen (O=O)
        { {{-11,0,3},{11,0,3}},
          {{0,1}} },
        // 15: N2 — dinitrogen (N≡N)
        { {{-11,0,2},{11,0,2}},
          {{0,1}} },
        // 16: O3 — ozone (bent ~117°, 3 O)
        { {{0,0,3},{-20,11,3},{20,11,3}},
          {{0,1},{0,2}} },
        // 17: CO — carbon monoxide (C≡O)
        { {{-11,0,1},{11,0,3}},
          {{0,1}} },
        // 18: NO — nitric oxide (N=O)
        { {{-11,0,2},{11,0,3}},
          {{0,1}} },
        // 19: NO2 — nitrogen dioxide (bent ~134°)
        { {{0,0,2},{-22,8,3},{22,8,3}},
          {{0,1},{0,2}} },
        // 20: SO2 — sulfur dioxide (bent ~119°)
        { {{0,0,5},{-22,10,3},{22,10,3}},
          {{0,1},{0,2}} },
        // 21: H2S — hydrogen sulfide (bent ~92°)
        { {{0,0,5},{-14,10,0},{14,10,0}},
          {{0,1},{0,2}} },

        // ── Acids, bases, salts ───────────────────────────────────────────────
        // 22: HCl — hydrochloric acid (H-Cl)
        { {{-11,0,0},{11,0,7}},
          {{0,1}} },
        // 23: NaOH — sodium hydroxide (Na-O-H)
        { {{-24,0,6},{0,0,3},{16,0,0}},
          {{0,1},{1,2}} },
        // 24: H2O2 — hydrogen peroxide (H-O-O-H)
        { {{-34,0,0},{-11,0,3},{11,0,3},{34,0,0}},
          {{0,1},{1,2},{2,3}} },
        // 25: H2SO4 — sulfuric acid (S + 4O + 2H, simplified)
        { {{0,0,5},{0,-22,3},{-22,0,3},{22,0,3},{0,22,3},{-36,0,0},{36,0,0}},
          {{0,1},{0,2},{0,3},{0,4},{2,5},{3,6}} },
        // 26: HNO3 — nitric acid (H-O-N(=O)2)
        { {{0,0,2},{-22,0,3},{16,-16,3},{16,16,3},{-36,0,0}},
          {{0,1},{0,2},{0,3},{1,4}} },
        // 27: H3PO4 — phosphoric acid (P + 4O + 3H)
        { {{0,0,4},{0,-22,3},{-22,0,3},{22,0,3},{0,22,3},{-36,0,0},{36,0,0},{0,36,0}},
          {{0,1},{0,2},{0,3},{0,4},{2,5},{3,6},{4,7}} },

        // ── Organic basics ────────────────────────────────────────────────────
        // 28: CH2O — formaldehyde (H2C=O)
        { {{0,0,1},{0,-22,3},{-14,12,0},{14,12,0}},
          {{0,1},{0,2},{0,3}} },
        // 29: HCN — hydrogen cyanide (H-C≡N)
        { {{-24,0,0},{0,0,1},{24,0,2}},
          {{0,1},{1,2}} },
        // 30: C2H6 — ethane (H3C-CH3)
        { {{-11,0,1},{11,0,1},
           {-25,14,0},{-25,-14,0},{-25,0,0},
           {25,14,0},{25,-14,0},{25,0,0}},
          {{0,1},{0,2},{0,3},{0,4},{1,5},{1,6},{1,7}} },
        // 31: C2H4 — ethylene (H2C=CH2)
        { {{-11,0,1},{11,0,1},
           {-25,14,0},{-25,-14,0},
           {25,14,0},{25,-14,0}},
          {{0,1},{0,2},{0,3},{1,4},{1,5}} },
        // 32: C2H2 — acetylene (HC≡CH)
        { {{-11,0,1},{11,0,1},{-26,0,0},{26,0,0}},
          {{0,1},{0,2},{1,3}} },
        // 33: CH3OH — methanol (H3C-OH)
        { {{-11,0,1},{11,0,3},
           {-25,14,0},{-25,-14,0},{-25,0,0},{26,0,0}},
          {{0,1},{0,2},{0,3},{0,4},{1,5}} },
        // 34: CH3COOH — acetic acid (CH3-C(=O)-OH)
        { {{-22,0,1},{0,0,1},{14,-14,3},{14,14,3},
           {-36,14,0},{-36,-14,0},{-36,0,0},{28,22,0}},
          {{0,1},{1,2},{1,3},{3,7},{0,4},{0,5},{0,6}} },
        // 35: Urea — (NH2)2CO
        { {{0,0,1},{0,-22,3},{-22,0,2},{22,0,2},
           {-34,14,0},{-34,-14,0},{34,14,0},{34,-14,0}},
          {{0,1},{0,2},{0,3},{2,4},{2,5},{3,6},{3,7}} },
        // 36: Acetone — (CH3)2C=O
        { {{0,0,1},{0,-22,3},{-22,0,1},{22,0,1},
           {-36,14,0},{-36,-14,0},{-36,0,0},
           {36,14,0},{36,-14,0},{36,0,0}},
          {{0,1},{0,2},{0,3},{2,4},{2,5},{2,6},{3,7},{3,8},{3,9}} },
        // 37: C3H8 — propane (CH3-CH2-CH3)
        { {{-22,0,1},{0,0,1},{22,0,1},
           {-36,14,0},{-36,-14,0},{-36,0,0},
           {0,22,0},{0,-22,0},
           {36,14,0},{36,-14,0},{36,0,0}},
          {{0,1},{1,2},{0,3},{0,4},{0,5},{1,6},{1,7},{2,8},{2,9},{2,10}} },
        // 38: CH3NH2 — methylamine (CH3-NH2)
        { {{-11,0,1},{11,0,2},
           {-25,14,0},{-25,-14,0},{-25,0,0},{25,14,0},{25,-14,0}},
          {{0,1},{0,2},{0,3},{0,4},{1,5},{1,6}} },

        // ── Minerals ──────────────────────────────────────────────────────────
        // 39: SiO2 — quartz (O-Si-O, linear)
        { {{0,0,10},{-24,0,3},{24,0,3}},
          {{0,1},{0,2}} },
        // 40: TiO2 — rutile (O-Ti-O, linear)
        { {{0,0,12},{-24,0,3},{24,0,3}},
          {{0,1},{0,2}} },
        // 41: NiO — nickel oxide
        { {{-13,0,9},{13,0,3}},
          {{0,1}} },
        // 42: CaO — calcium oxide
        { {{-13,0,11},{13,0,3}},
          {{0,1}} },
        // 43: FeO — iron(II) oxide (wüstite)
        { {{-13,0,8},{13,0,3}},
          {{0,1}} },
        // 44: NiS — nickel sulfide (millerite)
        { {{-13,0,9},{13,0,5}},
          {{0,1}} },

        // ── More small molecules ──────────────────────────────────────────────
        // 45: SO3 — sulfur trioxide (planar trigonal)
        { {{0,0,5},{0,-24,3},{-20,12,3},{20,12,3}},
          {{0,1},{0,2},{0,3}} },
        // 46: ClNO — nitrosyl chloride (Cl-N=O)
        { {{0,0,2},{0,-22,3},{22,8,7}},
          {{0,1},{0,2}} },
        // 47: CS2 — carbon disulfide (S=C=S)
        { {{0,0,1},{-24,0,5},{24,0,5}},
          {{0,1},{0,2}} },
        // 48: PH3 — phosphine (trigonal pyramidal)
        { {{0,0,4},{0,-22,0},{-18,12,0},{18,12,0}},
          {{0,1},{0,2},{0,3}} },
    };

    // ── Bio-molecule templates (Organics tab) ────────────────────────────────
    // Types: H=0 C=1 N=2 O=3 P=4 S=5
    static const MolSpec ORGANICS[] = {
        // ── Amino Acids ─────────────────────────────────────────────────────────
        // Backbone: N-Cα-C'(=O)(-OH), side chains off Cα downward
        // 0: Glycine — NH2-CH2-COOH (8 atoms)
        { {{-24,0,2},{0,0,1},{22,0,1},{32,-14,3},{32,14,3},{-34,-12,0},{-34,12,0},{0,-16,0}},
          {{0,1},{1,2},{2,3},{2,4},{0,5},{0,6},{1,7}} },
        // 1: Alanine — NH2-CH(CH3)-COOH (10 atoms)
        { {{-24,0,2},{0,0,1},{0,-22,1},{22,0,1},{32,-14,3},{32,14,3},{-34,-12,0},{-34,12,0},{-10,-32,0},{10,-32,0}},
          {{0,1},{1,2},{1,3},{3,4},{3,5},{0,6},{0,7},{2,8},{2,9}} },
        // 2: Serine — NH2-CH(CH2OH)-COOH (7 atoms, no explicit H)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{0,-40,3}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6}} },
        // 3: Cysteine — NH2-CH(CH2SH)-COOH (7 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{0,-40,5}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6}} },
        // 4: Valine — NH2-CH(CH(CH3)2)-COOH (8 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{-14,-36,1},{14,-36,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{5,7}} },
        // 5: Threonine — NH2-CH(CH(OH)CH3)-COOH (8 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{-14,-36,3},{14,-36,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{5,7}} },
        // 6: Leucine — NH2-CH(CH2CH(CH3)2)-COOH (9 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{0,-44,1},{-14,-58,1},{14,-58,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{6,8}} },
        // 7: Proline — cyclic: N-Cα-Cβ-Cγ-Cδ-N ring + C'(=O)(-OH) (8 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{-16,-34,1},{-26,-16,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,0}} },
        // 8: Phenylalanine — backbone + -CH2-C6H5 benzene ring (12 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},
           {0,-22,1},{0,-44,1},{14,-52,1},{14,-68,1},{0,-76,1},{-14,-68,1},{-14,-52,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,8},{8,9},{9,10},{10,11},{11,6}} },
        // 9: Lysine — backbone + -(CH2)4-NH2 (10 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},
           {0,-22,1},{14,-34,1},{0,-46,1},{14,-58,1},{0,-72,2}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,8},{8,9}} },
        // 10: Aspartate — backbone + -CH2-COO⁻ (9 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},{0,-22,1},{0,-44,1},{-12,-56,3},{12,-56,3}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{6,8}} },
        // 11: Glutamate — backbone + -CH2-CH2-COO⁻ (10 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},
           {0,-22,1},{0,-44,1},{0,-66,1},{-12,-78,3},{12,-78,3}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,8},{7,9}} },
        // 12: Histidine — backbone + -CH2-imidazole ring (11 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},
           {0,-22,1},{0,-44,1},{-14,-54,2},{-10,-68,1},{6,-70,2},{14,-56,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,8},{8,9},{9,10},{10,6}} },
        // 13: Tryptophan — backbone + -CH2-indole (fused 5+6 ring) (14 atoms)
        { {{-22,0,2},{0,0,1},{22,0,1},{30,-14,3},{30,14,3},
           {0,-22,1},{0,-44,1},{-12,-56,2},{-22,-44,1},{-22,-28,1},{-10,-66,1},
           {-22,-72,1},{-32,-60,1},{-32,-44,1}},
          {{0,1},{1,2},{2,3},{2,4},{1,5},{5,6},{6,7},{7,8},{8,9},{9,6},
           {7,10},{10,11},{11,12},{12,13},{13,9}} },

        // ── Peptides ─────────────────────────────────────────────────────────────
        // 14: Gly-Ala dipeptide — H2N-CH2-CO-NH-CH(CH3)-COOH (10 atoms)
        //  N(0)-Cα1(1)-C'1(2)=O(3)-N2(4)-Cα2(5)-Cβ(6)-C'2(7)=O(8)-OH(9)
        { {{-44,0,2},{-22,0,1},{0,0,1},{8,-14,3},{22,0,2},{44,0,1},{44,-22,1},{66,0,1},{74,-14,3},{74,14,3}},
          {{0,1},{1,2},{2,3},{2,4},{4,5},{5,6},{5,7},{7,8},{7,9}} },
        // 15: Gly-Gly-Gly tripeptide — H2N-CH2-CO-NH-CH2-CO-NH-CH2-COOH (13 atoms)
        //  N(0)-Cα1(1)-C'1(2)=O(3)-N2(4)-Cα2(5)-C'2(6)=O(7)-N3(8)-Cα3(9)-C'3(10)=O(11)-OH(12)
        { {{-66,0,2},{-44,0,1},{-22,0,1},{-14,-14,3},{0,0,2},{22,0,1},{44,0,1},{52,-14,3},
           {66,0,2},{88,0,1},{110,0,1},{118,-14,3},{118,14,3}},
          {{0,1},{1,2},{2,3},{2,4},{4,5},{5,6},{6,7},{6,8},{8,9},{9,10},{10,11},{10,12}} },

        // ── Carbohydrates ───────────────────────────────────────────────────────
        // 16: Glucose — hexose ring (11 atoms: 5C + ring-O + 5 OH)
        { {{22,0,1},{11,19,1},{-11,19,1},{-22,0,1},{-11,-19,1},{11,-19,3},{34,-10,3},{18,30,3},{-18,30,3},{-34,-10,3},{-20,-28,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{1,7},{2,8},{3,9},{4,10}} },
        // 17: Fructose — ketohexose ring (11 atoms: 5C + ring-O + CH2OH + 4 OH)
        { {{22,0,1},{11,19,1},{-11,19,1},{-22,0,1},{-11,-19,1},{11,-19,3},
           {34,-10,1},{48,-4,3},{18,30,3},{-18,30,3},{-34,-10,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{6,7},{1,8},{2,9},{3,10}} },
        // 18: Galactose — hexose ring, C4 epimer of glucose (11 atoms)
        { {{22,0,1},{11,19,1},{-11,19,1},{-22,0,1},{-11,-19,1},{11,-19,3},{34,-10,3},{18,30,3},{-18,30,3},{-34,10,3},{-20,-28,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{1,7},{2,8},{3,9},{4,10}} },
        // 19: Ribose — pentose ring (9 atoms: 4C + ring-O + 4 OH)
        { {{20,0,1},{12,17,1},{-12,17,1},{-20,0,1},{0,-20,3},{32,-6,3},{18,28,3},{-18,28,3},{-32,-6,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,0},{0,5},{1,6},{2,7},{3,8}} },
        // 20: Deoxyribose — pentose ring minus 2'-OH (8 atoms: 4C + ring-O + 3 OH)
        { {{20,0,1},{12,17,1},{-12,17,1},{-20,0,1},{0,-20,3},{32,-6,3},{-18,28,3},{-32,-6,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,0},{0,5},{2,6},{3,7}} },
        // 21: Sucrose — disaccharide: glucose + fructose rings linked by O-bridge (18 atoms)
        //  Glc ring (0-4,O5), O-bridge(6), Fru ring (7-11,O12), + 5 OH groups
        { {{-32,0,1},{-43,19,1},{-21,19,1},{-10,0,1},{-21,-19,1},{-43,-19,3},
           {6,0,3},
           {22,0,1},{33,19,1},{55,19,1},{55,0,1},{33,-19,1},{22,-19,3},
           {-54,0,3},{-50,32,3},{66,0,3},{40,32,3},{40,-30,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{3,6},{6,7},{7,8},{8,9},{9,10},{10,11},{11,12},{12,7},
           {0,13},{1,14},{10,15},{8,16},{11,17}} },

        // ── Lipids ──────────────────────────────────────────────────────────────
        // 22: Butyric acid — CH3-CH2-CH2-COOH, short fatty acid (12 atoms)
        { {{-30,0,1},{-10,0,1},{10,0,1},{30,0,1},{44,-14,3},{44,14,3},{-40,14,0},{-40,-14,0},{-10,16,0},{-10,-16,0},{10,16,0},{10,-16,0}},
          {{0,1},{1,2},{2,3},{3,4},{3,5},{0,6},{0,7},{1,8},{1,9},{2,10},{2,11}} },
        // 23: Glycerophosphate — lipid head (P + 4O + 3C + N, 10 atoms)
        { {{0,0,4},{-20,12,3},{-20,-12,3},{20,0,3},{0,-22,3},{36,0,1},{50,14,1},{50,-14,1},{66,0,2},{48,28,3}},
          {{0,1},{0,2},{0,3},{0,4},{3,5},{5,6},{5,7},{6,8},{5,9}} },
        // 24: Palmitic acid — C16 saturated fatty acid chain (no H, 10 C + COOH, 13 atoms)
        { {{-60,0,1},{-40,0,1},{-20,0,1},{0,0,1},{20,0,1},{40,0,1},{60,0,1},{80,0,1},
           {100,0,1},{120,0,1},{134,-14,3},{134,14,3},{-80,0,1}},
          {{12,0},{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,8},{8,9},{9,10},{9,11}} },
        // 25: Oleic acid — C18:1 unsaturated fatty acid (simplified 10 C chain + COOH, 13 atoms)
        //  Kink at C9=C10 double bond represented by y-offset
        { {{-60,0,1},{-40,0,1},{-20,0,1},{0,0,1},{20,10,1},{40,10,1},{60,0,1},{80,0,1},
           {100,0,1},{120,0,1},{134,-14,3},{134,14,3},{-80,0,1}},
          {{12,0},{0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{6,7},{7,8},{8,9},{9,10},{9,11}} },
        // 26: Phosphatidylcholine head — PC lipid head group (12 atoms)
        //  N(CH3)3-CH2-CH2-O-PO4-O-glycerol
        { {{-60,0,2},{-60,-16,1},{-60,16,1},{-38,0,1},{-16,0,1},{4,0,3},{26,0,4},
           {26,-18,3},{26,18,3},{48,0,3},{66,0,1},{66,-20,3}},
          {{0,1},{0,2},{0,3},{3,4},{4,5},{5,6},{6,7},{6,8},{6,9},{9,10},{10,11}} },
        // 27: Cholesterol — steroid 4-ring skeleton (simplified 14 atoms)
        //  A(6)-B(6)-C(6)-D(5) fused ring system + OH + tail
        { {{-30,16,1},{-16,24,1},{0,16,1},{0,-2,1},{-16,-10,1},{-30,-2,1},
           {16,24,1},{32,16,1},{32,-2,1},{16,-10,1},
           {48,-2,1},{48,-20,1},{36,-28,1},
           {-42,24,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{2,6},{6,7},{7,8},{8,9},{9,3},
           {8,10},{10,11},{11,12},{0,13}} },

        // ── Nucleobases ─────────────────────────────────────────────────────────
        // 28: Adenine — purine base (10 atoms: 5C + 5N in fused 6-5 ring)
        { {{0,22,2},{18,10,1},{22,-10,2},{8,-24,1},{-14,-20,1},{-20,6,1},{-30,-8,2},{-28,12,1},{-10,28,2},{-32,18,2}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{3,8},{8,7},{7,6},{6,4},{5,9}} },
        // 29: Guanine — purine base + =O + NH2 (11 atoms)
        //  6-ring: N1(0)-C2(1)-N3(2)-C4(3)-C5(4)-C6(5); 5-ring: C4-C5-N7(6)-C8(7)-N9(8)
        { {{-16,0,2},{-8,-14,1},{8,-14,2},{16,0,1},{8,14,1},{-8,14,1},
           {22,22,2},{34,12,1},{28,-6,2},{-20,26,3},{-14,-28,2}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{4,6},{6,7},{7,8},{8,3},{5,9},{1,10}} },
        // 30: Cytosine — pyrimidine base (8 atoms: 4C + 3N + 1O in 6-ring + NH2)
        { {{-22,0,2},{0,18,1},{22,0,2},{22,-20,1},{0,-24,1},{-22,-14,1},{0,30,3},{34,-26,2}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7}} },
        // 31: Thymine — pyrimidine base + CH3 + 2x =O (9 atoms)
        //  6-ring: N1(0)-C2(1)-N3(2)-C4(3)-C5(4)-C6(5); O at C2(6), O at C4(7), CH3 at C5(8)
        { {{0,18,2},{-16,9,1},{-16,-9,2},{0,-18,1},{16,-9,1},{16,9,1},
           {-30,14,3},{0,-34,3},{30,-17,1}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7},{4,8}} },
        // 32: Uracil — pyrimidine base + 2x =O (8 atoms)
        //  Like Thymine but no CH3 at C5
        { {{0,18,2},{-16,9,1},{-16,-9,2},{0,-18,1},{16,-9,1},{16,9,1},
           {-30,14,3},{0,-34,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7}} },

        // ── Nucleosides (RNA: base + ribose) ────────────────────────────────────
        // Layout: ribose ring on right, base on left, N-glycosidic bond connects them
        // Ribose: C1'(r0) C2'(r1) C3'(r2) C4'(r3) O4'(r4) + O2'(r5) O3'(r6) O5'(r7)
        // 33: Adenosine — Adenine + Ribose (18 atoms)
        { {{0,22,2},{18,10,1},{22,-10,2},{8,-24,1},{-14,-20,1},{-20,6,1},{-30,-8,2},{-28,12,1},{-10,28,2},{-32,18,2},
           {36,18,1},{48,30,1},{62,22,1},{62,6,1},{42,6,3},{74,36,3},{76,18,3},{62,-12,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{3,8},{8,7},{7,6},{6,4},{5,9},
           {1,10},{10,11},{11,12},{12,13},{13,14},{14,10},{11,15},{12,16},{13,17}} },
        // 34: Guanosine — Guanine + Ribose (19 atoms)
        { {{-16,0,2},{-8,-14,1},{8,-14,2},{16,0,1},{8,14,1},{-8,14,1},
           {22,22,2},{34,12,1},{28,-6,2},{-20,26,3},{-14,-28,2},
           {46,0,1},{58,12,1},{72,4,1},{72,-12,1},{52,-12,3},{84,18,3},{86,0,3},{72,-30,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{4,6},{6,7},{7,8},{8,3},{5,9},{1,10},
           {8,11},{11,12},{12,13},{13,14},{14,15},{15,11},{12,16},{13,17},{14,18}} },
        // 35: Cytidine — Cytosine + Ribose (16 atoms)
        { {{-22,0,2},{0,18,1},{22,0,2},{22,-20,1},{0,-24,1},{-22,-14,1},{0,30,3},{34,-26,2},
           {44,0,1},{56,12,1},{70,4,1},{70,-12,1},{50,-12,3},{82,18,3},{84,0,3},{70,-30,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7},
           {2,8},{8,9},{9,10},{10,11},{11,12},{12,8},{9,13},{10,14},{11,15}} },
        // 36: Uridine — Uracil + Ribose (16 atoms)
        { {{0,18,2},{-16,9,1},{-16,-9,2},{0,-18,1},{16,-9,1},{16,9,1},{-30,14,3},{0,-34,3},
           {34,9,1},{46,21,1},{60,13,1},{60,-3,1},{40,-3,3},{72,27,3},{74,9,3},{60,-21,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7},
           {5,8},{8,9},{9,10},{10,11},{11,12},{12,8},{9,13},{10,14},{11,15}} },
        // 37: Thymidine — Thymine + Deoxyribose (no 2'-OH) (15 atoms)
        { {{0,18,2},{-16,9,1},{-16,-9,2},{0,-18,1},{16,-9,1},{16,9,1},{-30,14,3},{0,-34,3},{30,-17,1},
           {34,9,1},{46,21,1},{60,13,1},{60,-3,1},{40,-3,3},{60,-21,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7},{4,8},
           {5,9},{9,10},{10,11},{11,12},{12,13},{13,9},{12,14}} },

        // ── Nucleosides (DNA: base + deoxyribose) ──────────────────────────────
        // Same layout but deoxyribose (no 2'-OH → one fewer atom)
        // 38: Deoxyadenosine — Adenine + dRib (17 atoms)
        { {{0,22,2},{18,10,1},{22,-10,2},{8,-24,1},{-14,-20,1},{-20,6,1},{-30,-8,2},{-28,12,1},{-10,28,2},{-32,18,2},
           {36,18,1},{48,30,1},{62,22,1},{62,6,1},{42,6,3},{76,18,3},{62,-12,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{3,8},{8,7},{7,6},{6,4},{5,9},
           {1,10},{10,11},{11,12},{12,13},{13,14},{14,10},{12,15},{13,16}} },
        // 39: Deoxyguanosine — Guanine + dRib (18 atoms)
        { {{-16,0,2},{-8,-14,1},{8,-14,2},{16,0,1},{8,14,1},{-8,14,1},
           {22,22,2},{34,12,1},{28,-6,2},{-20,26,3},{-14,-28,2},
           {46,0,1},{58,12,1},{72,4,1},{72,-12,1},{52,-12,3},{86,0,3},{72,-30,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{4,6},{6,7},{7,8},{8,3},{5,9},{1,10},
           {8,11},{11,12},{12,13},{13,14},{14,15},{15,11},{13,16},{14,17}} },
        // 40: Deoxycytidine — Cytosine + dRib (15 atoms)
        { {{-22,0,2},{0,18,1},{22,0,2},{22,-20,1},{0,-24,1},{-22,-14,1},{0,30,3},{34,-26,2},
           {44,0,1},{56,12,1},{70,4,1},{70,-12,1},{50,-12,3},{84,0,3},{70,-30,3}},
          {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{1,6},{3,7},
           {2,8},{8,9},{9,10},{10,11},{11,12},{12,8},{10,13},{11,14}} },
    };
    static constexpr int ORGANICS_COUNT = sizeof(ORGANICS) / sizeof(ORGANICS[0]);

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
        int gi = std::clamp(iface.spawn_group_idx, 0, 48);
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
        } else if (oi == -13) {
            // Small Proto-Cell: 16-C lipid ring (R=57, spacing=22px=bond_rest) + glycine core
            // Ring: C*3=48>16 → LIPID; ring_factor≈1.0 → VESICLE
            // Core (5 atoms: N=1 C=2 O=2): (N+O)*2=6>5, C>0 → AMINO_ACID → PROTO_CELL
            constexpr float  RING_R  = 57.0f;   // 22/(2*sin(π/16)) = 56.6 ≈ 57px
            constexpr int    RING_N  = 16;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            // Consecutive ring bonds (membrane topology)
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // Glycine-like amino acid core — within 10px of center, 47px+ from ring atoms
            tmpl_atoms.push_back({   0.f,   0.f, 2u }); // N  (16) — amino group
            tmpl_atoms.push_back({  -8.f,   0.f, 1u }); // C  (17) — alpha carbon
            tmpl_atoms.push_back({   8.f,   0.f, 1u }); // C  (18) — carbonyl carbon
            tmpl_atoms.push_back({   8.f,  -9.f, 3u }); // O  (19) — carbonyl O (=O)
            tmpl_atoms.push_back({   0.f,   9.f, 3u }); // O  (20) — hydroxyl (−OH)
            tmpl_bonds.push_back({ 16, 17 }); // N-Cα
            tmpl_bonds.push_back({ 16, 18 }); // N-C(=O)
            tmpl_bonds.push_back({ 17, 19 }); // Cα-O
            tmpl_bonds.push_back({ 18, 20 }); // C-OH
        } else if (oi == -14) {
            // Cell: 16-C lipid ring (R=57) + DNA/ATP/ribosome organelle core
            // Ring: LIPID + ring_factor≈1.0 → VESICLE
            // Core (8 atoms: P=5 N=2 C=1): P*3=15>8 → NUCLEOTIDE → CELL
            // Organelles represented: ATP (P-P-P), DNA backbone (P-N-C), ribosome (N-N-C)
            constexpr float  RING_R  = 57.0f;
            constexpr int    RING_N  = 16;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // Organelle core — all within 14px of center, 43px+ from ring atoms
            tmpl_atoms.push_back({   0.f,   0.f, 4u }); // P  (16) — ATP γ-phosphate
            tmpl_atoms.push_back({ -12.f,   0.f, 4u }); // P  (17) — ATP β-phosphate
            tmpl_atoms.push_back({  12.f,   0.f, 4u }); // P  (18) — ATP α-phosphate
            tmpl_atoms.push_back({   0.f, -12.f, 4u }); // P  (19) — DNA backbone P
            tmpl_atoms.push_back({  -8.f,  10.f, 4u }); // P  (20) — RNA phosphate
            tmpl_atoms.push_back({   8.f,  10.f, 2u }); // N  (21) — purine base
            tmpl_atoms.push_back({   0.f,  14.f, 2u }); // N  (22) — pyrimidine base
            tmpl_atoms.push_back({   6.f, -10.f, 1u }); // C  (23) — ribose carbon
            tmpl_bonds.push_back({ 16, 17 }); // P-P  ATP γ-β
            tmpl_bonds.push_back({ 16, 18 }); // P-P  ATP γ-α
            tmpl_bonds.push_back({ 16, 19 }); // P-P  ATP-DNA bridge
            tmpl_bonds.push_back({ 17, 20 }); // P-P  DNA-RNA
            tmpl_bonds.push_back({ 18, 21 }); // P-N  base attachment
            tmpl_bonds.push_back({ 21, 22 }); // N-N  base pair
            tmpl_bonds.push_back({ 22, 23 }); // N-C  glycosidic bond
            tmpl_bonds.push_back({ 19, 23 }); // P-C  ribose-phosphate
        } else if (oi == -15) {
            // Large Proto-Cell: 20-C ring (R=70, spacing=22px=bond_rest) + ribosome-like core
            // Ring: C*3=60>20 → LIPID; ring_factor≈1.0 → VESICLE
            // Core (8 atoms: N=3 C=3 O=2): (N+O)*2=10>8, C>0 → AMINO_ACID → PROTO_CELL
            constexpr float  RING_R  = 70.0f;   // 22/(2*sin(π/20)) = 70.3 ≈ 70px
            constexpr int    RING_N  = 20;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // Ribosome-like amino acid core — within 15px of center, 55px+ from ring atoms
            tmpl_atoms.push_back({   0.f,   0.f, 2u }); // N  (20) — central amine
            tmpl_atoms.push_back({ -10.f,   0.f, 1u }); // C  (21) — alpha carbon
            tmpl_atoms.push_back({  10.f,   0.f, 1u }); // C  (22) — beta carbon
            tmpl_atoms.push_back({ -10.f,  10.f, 2u }); // N  (23) — amino group
            tmpl_atoms.push_back({  10.f,  10.f, 2u }); // N  (24) — amino group
            tmpl_atoms.push_back({   0.f,  14.f, 1u }); // C  (25) — carbonyl carbon
            tmpl_atoms.push_back({  -8.f, -12.f, 3u }); // O  (26) — carboxyl O
            tmpl_atoms.push_back({   8.f, -12.f, 3u }); // O  (27) — carboxyl O
            tmpl_bonds.push_back({ 20, 21 }); // N-Cα
            tmpl_bonds.push_back({ 20, 22 }); // N-Cβ
            tmpl_bonds.push_back({ 21, 23 }); // Cα-N amine
            tmpl_bonds.push_back({ 22, 24 }); // Cβ-N amine
            tmpl_bonds.push_back({ 20, 25 }); // N-C carbonyl
            tmpl_bonds.push_back({ 21, 26 }); // Cα-O carboxyl
            tmpl_bonds.push_back({ 22, 27 }); // Cβ-O carboxyl
        } else if (oi == -16) {
            // Large Cell: 20-C ring (R=70) + full organelle core
            // Ring: LIPID + ring_factor≈1.0 → VESICLE
            // Core (12 atoms: P=5 N=2 C=2 O=1 S=2): P*3=15>12 → NUCLEOTIDE → CELL
            // Organelles: DNA (P-N-C backbone), ATP (P-P-P), mitochondrion Fe-S (S-S), ribosome (N-C)
            constexpr float  RING_R  = 70.0f;
            constexpr int    RING_N  = 20;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // Full organelle core — within 17px of center, 53px+ from ring atoms
            tmpl_atoms.push_back({   0.f,   0.f, 4u }); // P  (20) — ATP central phosphate
            tmpl_atoms.push_back({ -12.f,  -8.f, 4u }); // P  (21) — DNA backbone
            tmpl_atoms.push_back({  12.f,  -8.f, 4u }); // P  (22) — DNA backbone
            tmpl_atoms.push_back({   0.f, -15.f, 4u }); // P  (23) — ATP terminal
            tmpl_atoms.push_back({  16.f,   0.f, 4u }); // P  (24) — phosphate chain
            tmpl_atoms.push_back({ -16.f,   0.f, 2u }); // N  (25) — purine base
            tmpl_atoms.push_back({ -10.f,  12.f, 2u }); // N  (26) — pyrimidine base
            tmpl_atoms.push_back({   0.f,  12.f, 1u }); // C  (27) — ribose carbon
            tmpl_atoms.push_back({  -5.f, -10.f, 1u }); // C  (28) — backbone carbon
            tmpl_atoms.push_back({  10.f,  10.f, 3u }); // O  (29) — oxygen bridge
            tmpl_atoms.push_back({   0.f,  16.f, 5u }); // S  (30) — Fe-S cluster (ETC)
            tmpl_atoms.push_back({   8.f,  14.f, 5u }); // S  (31) — Fe-S pair
            tmpl_bonds.push_back({ 20, 21 }); // P-P  ATP-DNA
            tmpl_bonds.push_back({ 20, 22 }); // P-P  ATP-DNA
            tmpl_bonds.push_back({ 20, 23 }); // P-P  ATP chain
            tmpl_bonds.push_back({ 20, 24 }); // P-P  phosphate
            tmpl_bonds.push_back({ 21, 25 }); // P-N  base attachment
            tmpl_bonds.push_back({ 22, 25 }); // P-N  base pair
            tmpl_bonds.push_back({ 25, 26 }); // N-N  base stacking
            tmpl_bonds.push_back({ 26, 27 }); // N-C  glycosidic
            tmpl_bonds.push_back({ 27, 28 }); // C-C  backbone
            tmpl_bonds.push_back({ 28, 29 }); // C-O  ester
            tmpl_bonds.push_back({ 30, 31 }); // S-S  iron-sulfur cluster
        } else if (oi == -17) {
            // Alt Proto-Cell: 16-C ring (R=57) + N-rich amino acid core
            // Core (5 atoms: N=2 C=1 O=2): (N+O)*2=8>5, C>0 → AMINO_ACID → PROTO_CELL
            constexpr float  RING_R  = 57.0f;
            constexpr int    RING_N  = 16;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // N-rich core — within 8px of center, 49px+ from ring atoms
            tmpl_atoms.push_back({  -8.f,   0.f, 2u }); // N  (16) — amine
            tmpl_atoms.push_back({   8.f,   0.f, 2u }); // N  (17) — amine
            tmpl_atoms.push_back({   0.f,   0.f, 1u }); // C  (18) — alpha carbon
            tmpl_atoms.push_back({   0.f,  -8.f, 3u }); // O  (19) — carbonyl O
            tmpl_atoms.push_back({   0.f,   8.f, 3u }); // O  (20) — hydroxyl O
            tmpl_bonds.push_back({ 16, 18 }); // N-C
            tmpl_bonds.push_back({ 17, 18 }); // N-C
            tmpl_bonds.push_back({ 18, 19 }); // C=O
            tmpl_bonds.push_back({ 18, 20 }); // C-OH
        } else if (oi == -18) {
            // Alt Cell: 16-C ring (R=57) + P-rich nucleotide core
            // Core (5 atoms: P=3 N=1 C=1): P*3=9>5 → NUCLEOTIDE → CELL
            constexpr float  RING_R  = 57.0f;
            constexpr int    RING_N  = 16;
            constexpr float  RING_DA = 6.28318530f / RING_N;
            for (int i = 0; i < RING_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * RING_DA) * RING_R,
                                       std::sin(i * RING_DA) * RING_R, 1u }); // C
            for (int i = 0; i < RING_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % RING_N });
            // P-rich core — within 8px of center, 49px+ from ring atoms
            tmpl_atoms.push_back({  -8.f,   0.f, 4u }); // P  (16) — phosphate
            tmpl_atoms.push_back({   8.f,   0.f, 4u }); // P  (17) — phosphate
            tmpl_atoms.push_back({   0.f,  -8.f, 4u }); // P  (18) — phosphate
            tmpl_atoms.push_back({   0.f,   8.f, 2u }); // N  (19) — base N
            tmpl_atoms.push_back({   0.f,   0.f, 1u }); // C  (20) — ribose C
            tmpl_bonds.push_back({ 16, 17 }); // P-P
            tmpl_bonds.push_back({ 16, 18 }); // P-P
            tmpl_bonds.push_back({ 17, 19 }); // P-N base
            tmpl_bonds.push_back({ 18, 20 }); // P-C ribose
        } else if (oi == -19) {
            // ── Plant Cell ──────────────────────────────────────────────────
            // Outer ring: 40-atom cellulose cell wall (C/O alternating)
            // Inner ring: 32-atom phospholipid membrane (C/P alternating)
            // Interior: nucleus, 4 chloroplasts, 2 mitochondria, ER, Golgi,
            //           vacuole water, ribosomes  (125 atoms total)

            // ── Cell Wall: 40 atoms, R=140px ──
            constexpr int   WALL_N  = 40;
            constexpr float WALL_R  = 140.0f;
            constexpr float WALL_DA = 6.28318530f / WALL_N;
            for (int i = 0; i < WALL_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * WALL_DA) * WALL_R,
                                       std::sin(i * WALL_DA) * WALL_R,
                                       (i % 2 == 0) ? 1u : 3u }); // C/O cellulose
            for (int i = 0; i < WALL_N; ++i)
                tmpl_bonds.push_back({ i, (i + 1) % WALL_N });

            // ── Cell Membrane: 32 atoms, R=112px  (indices 40-71) ──
            constexpr int   MEM_N   = 32;
            constexpr float MEM_R   = 112.0f;
            constexpr float MEM_DA  = 6.28318530f / MEM_N;
            for (int i = 0; i < MEM_N; ++i)
                tmpl_atoms.push_back({ std::cos(i * MEM_DA) * MEM_R,
                                       std::sin(i * MEM_DA) * MEM_R,
                                       (i % 2 == 0) ? 1u : 4u }); // C/P phospholipid
            for (int i = 0; i < MEM_N; ++i)
                tmpl_bonds.push_back({ WALL_N + i, WALL_N + (i + 1) % MEM_N });

            // ── Radial tethers: wall↔membrane at 8 compass points ──
            // Wall[0°,45°,...] → Membrane[0°,45°,...] (28px gap ≈ turgor pressure)
            static const int TETHER[][2] = {
                {0,40},{5,44},{10,48},{15,52},{20,56},{25,60},{30,64},{35,68}
            };
            for (const auto& t : TETHER)
                tmpl_bonds.push_back({ t[0], t[1] });

            // ── Nucleus: 8-ring at (-55,0), R=29px  (indices 72-79) ──
            constexpr int   NUC_N  = 8;
            constexpr float NUC_R  = 29.0f;
            constexpr float NUC_DA = 6.28318530f / NUC_N;
            constexpr float NUC_CX = -55.0f, NUC_CY = 0.0f;
            static const uint32_t NUC_TYPES[8] = { 2,4,1,3, 2,4,1,3 }; // N,P,C,O
            for (int i = 0; i < NUC_N; ++i)
                tmpl_atoms.push_back({ NUC_CX + std::cos(i * NUC_DA) * NUC_R,
                                       NUC_CY + std::sin(i * NUC_DA) * NUC_R,
                                       NUC_TYPES[i] });
            for (int i = 0; i < NUC_N; ++i)
                tmpl_bonds.push_back({ 72 + i, 72 + (i + 1) % NUC_N });
            // Nucleolus: 2 atoms inside nucleus (indices 80-81)
            tmpl_atoms.push_back({ NUC_CX, NUC_CY + 5.0f, 2u }); // N  rRNA
            tmpl_atoms.push_back({ NUC_CX, NUC_CY - 5.0f, 1u }); // C  ribosomal
            tmpl_bonds.push_back({ 80, 81 });
            tmpl_bonds.push_back({ 80, 72 }); // link to ring
            tmpl_bonds.push_back({ 81, 76 }); // link to opposite ring atom

            // ── 4 Chloroplasts: Ca(Mg) center + 4 N arms (indices 82-101) ──
            static const float CHLORO_POS[4][2] = {
                { 70.f,  0.f}, {-70.f,  0.f}, { 0.f,  70.f}, { 0.f, -70.f}
            };
            for (int c = 0; c < 4; ++c) {
                float cx = CHLORO_POS[c][0], cy = CHLORO_POS[c][1];
                int base = 82 + c * 5;
                tmpl_atoms.push_back({ cx, cy, 11u });          // Ca=Mg center
                tmpl_atoms.push_back({ cx - 18.f, cy, 2u });    // N porphyrin
                tmpl_atoms.push_back({ cx + 18.f, cy, 2u });    // N porphyrin
                tmpl_atoms.push_back({ cx, cy + 18.f, 2u });    // N porphyrin
                tmpl_atoms.push_back({ cx, cy - 18.f, 2u });    // N porphyrin
                tmpl_bonds.push_back({ base, base + 1 });
                tmpl_bonds.push_back({ base, base + 2 });
                tmpl_bonds.push_back({ base, base + 3 });
                tmpl_bonds.push_back({ base, base + 4 });
            }

            // ── 2 Mitochondria: Fe + C + S (indices 102-107) ──
            static const float MITO_POS[2][2] = {
                { 50.f, 50.f}, {-50.f, -50.f}
            };
            for (int m = 0; m < 2; ++m) {
                float mx = MITO_POS[m][0], my = MITO_POS[m][1];
                int base = 102 + m * 3;
                tmpl_atoms.push_back({ mx, my, 8u });            // Fe heme
                tmpl_atoms.push_back({ mx - 12.f, my, 1u });     // C
                tmpl_atoms.push_back({ mx, my - 12.f, 5u });     // S
                tmpl_bonds.push_back({ base, base + 1 });
                tmpl_bonds.push_back({ base, base + 2 });
            }

            // ── ER: 4-atom chain near nucleus (indices 108-111) ──
            tmpl_atoms.push_back({ -10.f, 40.f, 1u }); // C
            tmpl_atoms.push_back({   8.f, 40.f, 2u }); // N
            tmpl_atoms.push_back({  26.f, 40.f, 1u }); // C
            tmpl_atoms.push_back({  44.f, 40.f, 3u }); // O
            tmpl_bonds.push_back({ 108, 109 });
            tmpl_bonds.push_back({ 109, 110 });
            tmpl_bonds.push_back({ 110, 111 });

            // ── Golgi: 3-atom stack (indices 112-114) ──
            tmpl_atoms.push_back({  10.f, 55.f, 1u }); // C
            tmpl_atoms.push_back({  28.f, 55.f, 4u }); // P
            tmpl_atoms.push_back({  46.f, 55.f, 1u }); // C
            tmpl_bonds.push_back({ 112, 113 });
            tmpl_bonds.push_back({ 113, 114 });

            // ── Central vacuole water: 6 sparse H/O (indices 115-120) ──
            tmpl_atoms.push_back({   0.f,   0.f, 0u }); // H
            tmpl_atoms.push_back({  15.f, -10.f, 3u }); // O
            tmpl_atoms.push_back({ -18.f, -15.f, 0u }); // H
            tmpl_atoms.push_back({  22.f,  18.f, 3u }); // O
            tmpl_atoms.push_back({ -10.f,  22.f, 0u }); // H
            tmpl_atoms.push_back({   8.f, -22.f, 3u }); // O

            // ── Ribosomes: 4 scattered N atoms (indices 121-124) ──
            tmpl_atoms.push_back({ -25.f, -35.f, 2u }); // N rRNA
            tmpl_atoms.push_back({  35.f, -25.f, 2u }); // N
            tmpl_atoms.push_back({ -30.f,  50.f, 2u }); // N
            tmpl_atoms.push_back({  45.f,  35.f, 2u }); // N
        } else {
            return;
        }

        // ── Pre-clear: push all soup particles out of the ring neighbourhood ──────
        // Without this, ~150-200 soup particles within eps=40 of ring atoms merge
        // into the ring's DBSCAN cluster, diluting C fraction below the LIPID threshold.
        if (oi <= -13) {
            // clear_r covers ring_R + eps(40) + 20px drift buffer
            const float CLEAR_R  = (oi == -19) ? 185.0f
                                   : (oi == -15 || oi == -16) ? 130.0f : 118.0f;
            const float CLEAR_R2 = CLEAR_R * CLEAR_R;
            const float OUT_R    = CLEAR_R + 30.0f;  // scatter to just outside clear zone
            const float RW = static_cast<float>(REGION_W);
            const float RH = static_cast<float>(REGION_H);
            for (uint32_t k = 0; k < n; ++k) {
                if (particles.types[k] == PHOTON_TYPE) continue;
                if (spawn_protect_ids_.count(k)) continue;
                glm::vec2 d = cur_pos[k] - world_pos;
                // Toroidal shortest path
                if (d.x >  RW * 0.5f) d.x -= RW;
                else if (d.x < -RW * 0.5f) d.x += RW;
                if (d.y >  RH * 0.5f) d.y -= RH;
                else if (d.y < -RH * 0.5f) d.y += RH;
                float d2 = glm::dot(d, d);
                if (d2 > CLEAR_R2) continue;
                // Eject radially outward; stagger scatter distances to avoid pile-up
                float ang = (d2 > 1e-6f) ? std::atan2(d.y, d.x)
                                          : static_cast<float>(k % 628) * 0.01f;
                float rad = OUT_R + static_cast<float>(k % 80) * 1.5f;
                glm::vec2 np = world_pos + glm::vec2(std::cos(ang) * rad,
                                                      std::sin(ang) * rad);
                np.x = std::fmod(np.x + RW * 2.0f, RW);
                np.y = std::fmod(np.y + RH * 2.0f, RH);
                cur_pos[k]  = np;
                cur_vel[k]  = { 0.0f, 0.0f };
                bond_manager.clear_particle_bonds(k);
            }
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

        // Boost bond stiffness for ring atoms — eff_k = 80*(bond_str+0.5) → 120 (vs 80 default)
        if (oi <= -13) {
            int rn = (oi == -19) ? 72
                   : (oi == -15 || oi == -16) ? 20 : 16;
            for (int i = 0; i < rn && i < static_cast<int>(placed.size()); ++i)
                particles.genomes[placed[i] * 4 + 3] = 1.0f;
        }

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
        int oi = std::clamp(iface.spawn_organic_idx, 0, ORGANICS_COUNT - 1);
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

// ── Cosmic ray injection ──────────────────────────────────────────────────────
// Injects 1-3 high-energy particles per bond cycle from world edges.
// Active only in environment_mode == 5 (Deep Space).

void Simulation::inject_cosmic_rays() {
    if (!compute.is_ready()) return;
    auto& cur_pos = readback_positions_;
    auto& cur_vel = readback_velocities_;
    auto& cur_nrg = readback_energies_;
    uint32_t n = static_cast<uint32_t>(particles.types.size());
    if (n == 0) return;

    static std::mt19937 cr_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    int inject_count = 1 + static_cast<int>(uni(cr_rng) * 3.0f);
    bool modified = false;

    for (int j = 0; j < inject_count; ++j) {
        // Find a low-energy recyclable particle
        uint32_t idx = 0xFFFFFFFF;
        for (int tries = 0; tries < 50; ++tries) {
            uint32_t cand = static_cast<uint32_t>(uni(cr_rng) * n);
            if (cand >= n) continue;
            if (particles.types[cand] == PHOTON_TYPE) continue;
            if (spawn_protect_ids_.count(cand)) continue;
            if (cur_nrg[cand] < 0.05f) { idx = cand; break; }
        }
        if (idx == 0xFFFFFFFF) continue;

        // Random light atom type (H, C, N, O)
        uint32_t cosmic_type = static_cast<uint32_t>(uni(cr_rng) * 4.0f);
        float angle = uni(cr_rng) * 6.28318f;
        float speed = 150.0f + uni(cr_rng) * 150.0f;

        // Spawn at random world edge
        float edge = uni(cr_rng);
        glm::vec2 pos(0.0f);
        if (edge < 0.25f)
            pos = glm::vec2(0.0f, uni(cr_rng) * REGION_H);
        else if (edge < 0.5f)
            pos = glm::vec2(static_cast<float>(REGION_W), uni(cr_rng) * REGION_H);
        else if (edge < 0.75f)
            pos = glm::vec2(uni(cr_rng) * REGION_W, 0.0f);
        else
            pos = glm::vec2(uni(cr_rng) * REGION_W, static_cast<float>(REGION_H));

        cur_pos[idx] = pos;
        cur_vel[idx] = glm::vec2(std::cos(angle), std::sin(angle)) * speed;
        cur_nrg[idx] = 0.8f + uni(cr_rng) * 0.2f;
        particles.types[idx] = cosmic_type;
        bond_manager.clear_particle_bonds(idx);
        modified = true;
    }

    if (modified) {
        compute.write_particle_state(vk, cur_pos, cur_vel, cur_nrg);
        compute.upload_dynamic_data(vk, particles);
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
