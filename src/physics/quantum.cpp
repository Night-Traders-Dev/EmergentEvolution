#include "physics/simulation.h"
#include "physics/sim_helpers.h"
#include <algorithm>
#include <random>
#include <cmath>

// ── Quantum effects ──────────────────────────────────────────────────────────
// Split from simulation.cpp: virtual pairs, neutrino scattering/oscillations,
// and entanglement.

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
    // Spawn virtual pairs across the full simulation world (viewport-independent)
    std::uniform_real_distribution<float> x_dist(0.0f, static_cast<float>(WORLD_W));
    std::uniform_real_distribution<float> y_dist(0.0f, static_cast<float>(WORLD_H));

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
        achievements.total_virtual_pairs++;
        any_spawned = true;
    }

    if (any_spawned) {
        cpu_particles_dirty_ = true;
        try_unlock(ACH_FIRST_VIRTUAL_PAIR);
    }
}

void PhysicsSimulation::check_neutrino_scattering() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 48271u + 314159u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);

    uint32_t _rs0 = random_start(n, frame_counter_, 0u);
    for (uint32_t _it = 0; _it < n; ++_it) {
        uint32_t i = (_rs0 + _it) % n;
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
        auto nu_cc_search = [&](uint32_t j) {
            if (j == i || readback_energies_[j] < 0.01f) return;
            if (particles.types[j] != NEUTRON_TYPE) return;
            glm::vec2 d = readback_positions_[j] - readback_positions_[i];
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) { best_d2 = d2; best_n = j; }
        };
        if (iface.prefs.spatial_grid) {
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 5.0f, nu_cc_search);
        } else {
            for (uint32_t j = 0; j < n; ++j) nu_cc_search(j);
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
    uint32_t _rs1 = random_start(n, frame_counter_, 1u);
    for (uint32_t _it = 0; _it < n; ++_it) {
        uint32_t i = (_rs1 + _it) % n;
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
            achievements.seen_neutrino_oscillation = true;
            achievements.total_neutrino_oscillations++;
            cpu_particles_dirty_ = true;
        }
    }
}

void PhysicsSimulation::update_entanglement() {
    if (!cfg.entanglement_enabled) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 7919u + 104729u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    uint32_t active_count = 0;
    bool any_changed = false;

    uint32_t _rs2 = random_start(n, frame_counter_, 2u);
    for (uint32_t _it = 0; _it < n; ++_it) {
        uint32_t i = (_rs2 + _it) % n;
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
        cpu_particles_dirty_ = true;
    }
    entangled_pair_count_ = active_count;
}
