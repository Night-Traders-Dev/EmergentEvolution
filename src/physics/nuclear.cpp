#include "physics/simulation.h"
#include "physics/sim_helpers.h"
#include "physics/ui_data.h"
#include <imgui.h>
#include <algorithm>
#include <random>
#include <cmath>

// ── Nuclear reactions ────────────────────────────────────────────────────────
// Split from simulation.cpp: annihilation, fusion, fission, nuclear decay,
// photoelectric, pion decay, and spallation processes.

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
        audio.play(AudioPlayer::SFX_ANNIHILATION, frame_counter_);
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
        audio.play(AudioPlayer::SFX_FUSION, frame_counter_);
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
        audio.play(AudioPlayer::SFX_FISSION, frame_counter_);
    }
}

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

                // Spawn electron hole at nucleus position (vacancy left behind)
                if (orbital_parent >= 0 && static_cast<uint32_t>(orbital_parent) < n) {
                    for (uint32_t h = 0; h < cfg.particle_count; ++h) {
                        if (readback_energies_[h] < 0.01f) {
                            readback_positions_[h] = readback_positions_[orbital_parent];
                            readback_velocities_[h] = rand_dir() * 5.0f;
                            readback_energies_[h] = 1.0f;
                            write_spawn_genome(particles, h, ELECTRON_HOLE_TYPE_PHYS, rng, frame_counter_);
                            particles.orbital_parent[h] = -1;
                            particles.orbital_shell[h] = -1;
                            cpu_particles_dirty_ = true;
                            iface.push_decay_event("Electron hole spawned (photoelectric)",
                                PhysicsInterface::DEVT_ELECTRON_HOLE, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "");
                            break;
                        }
                    }
                }

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

                    // Spawn electron hole at nucleus position
                    if (orbital_parent >= 0 && static_cast<uint32_t>(orbital_parent) < n) {
                        for (uint32_t h = 0; h < cfg.particle_count; ++h) {
                            if (readback_energies_[h] < 0.01f) {
                                readback_positions_[h] = readback_positions_[orbital_parent];
                                readback_velocities_[h] = rand_dir() * 5.0f;
                                readback_energies_[h] = 1.0f;
                                write_spawn_genome(particles, h, ELECTRON_HOLE_TYPE_PHYS, rng, frame_counter_);
                                particles.orbital_parent[h] = -1;
                                particles.orbital_shell[h] = -1;
                                cpu_particles_dirty_ = true;
                                iface.push_decay_event("Electron hole spawned (Compton)",
                                    PhysicsInterface::DEVT_ELECTRON_HOLE, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "");
                                break;
                            }
                        }
                    }
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
        audio.play(AudioPlayer::SFX_PHOTON, frame_counter_);
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

// ── Electron-hole recombination ──────────────────────────────────────────────

void PhysicsSimulation::check_recombination() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float RECOMB_RADIUS = 40.0f;
    const float RECOMB_RADIUS_SQ = RECOMB_RADIUS * RECOMB_RADIUS;
    const int MAX_RECOMB_PER_FRAME = 50;

    int recomb_count = 0;
    bool any_changed = false;
    std::mt19937 rng(frame_counter_ * 777777777u + 67);

    auto rand_dir = [&]() -> glm::vec2 {
        std::uniform_real_distribution<float> angle_d(0.0f, 6.28318530718f);
        float a = angle_d(rng);
        return glm::vec2(std::cos(a), std::sin(a));
    };

    // Find dormant slot for photon spawn
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t i = start; i < n; ++i)
            if (readback_energies_[i] < 0.01f) return i;
        return UINT32_MAX;
    };

    // Scan for electron holes
    for (uint32_t i = 0; i < n && recomb_count < MAX_RECOMB_PER_FRAME; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        if (particles.types[i] != ELECTRON_HOLE_TYPE_PHYS) continue;

        // Find nearest free electron
        float best_dist_sq = RECOMB_RADIUS_SQ;
        uint32_t best_e = UINT32_MAX;
        auto search = [&](uint32_t j) {
            if (j == i) return;
            if (readback_energies_[j] < 0.01f) return;
            if (particles.types[j] != ELECTRON_TYPE_PHYS) return;
            // Only free electrons (not bound to atoms)
            if (j < particles.orbital_parent.size() && particles.orbital_parent[j] >= 0) return;
            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 < best_dist_sq) {
                best_dist_sq = d2;
                best_e = j;
            }
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, RECOMB_RADIUS, search);
        else
            for (uint32_t j = 0; j < n; ++j) search(j);

        if (best_e == UINT32_MAX) continue;

        // Recombination: hole + free electron → photon
        glm::vec2 midpoint = (readback_positions_[i] + readback_positions_[best_e]) * 0.5f;

        // Deactivate hole
        readback_energies_[i] = 0.0f;
        readback_velocities_[i] = glm::vec2(0.0f);

        // Deactivate electron
        readback_energies_[best_e] = 0.0f;
        readback_velocities_[best_e] = glm::vec2(0.0f);

        // Spawn recombination photon
        uint32_t slot = find_dormant(0);
        if (slot != UINT32_MAX) {
            readback_positions_[slot] = midpoint;
            readback_velocities_[slot] = rand_dir() * C_SIM;
            readback_energies_[slot] = 0.5f;
            write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
            particles.orbital_parent[slot] = -1;
            particles.orbital_shell[slot] = -1;
        }

        any_changed = true;
        recomb_count++;
        iface.push_decay_event("Electron hole recombined: h\xe2\x81\xba + e\xe2\x81\xbb \xe2\x86\x92 \xce\xb3",
            PhysicsInterface::DEVT_ELECTRON_HOLE, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "");
    }

    if (any_changed) {
        cpu_particles_dirty_ = true;
    }
}

// ── Carrier Mode: visible force carrier exchange ─────────────────────────────

void PhysicsSimulation::check_carrier_exchange() {
    if (!cfg.carrier_mode_enabled) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 123456789u + 42u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    int spawned = 0;
    const int max_spawn = cfg.carrier_max_per_tick;
    const float radius = cfg.carrier_exchange_radius;
    const float radius_sq = radius * radius;

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    // ── Classification helpers ──
    auto is_neutrino_flags = [](uint32_t flags) {
        return (flags & BEHAVIOR_NEUTRINO) != 0;
    };
    auto is_baryon = [](uint32_t t) {
        return t == PROTON_TYPE || t == NEUTRON_TYPE || t == ANTIPROTON_TYPE_PHYS;
    };
    auto is_cc_lepton = [](uint32_t t) {
        return t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS ||
               t == MUON_TYPE_PHYS || t == ANTIMUON_TYPE_PHYS ||
               t == TAU_TYPE_PHYS || t == ANTITAU_TYPE_PHYS;
    };
    auto is_massive_fermion = [](uint32_t t, uint32_t flags) {
        if (t >= PHYS_PARTICLE_TYPES) return false;
        if (flags & (BEHAVIOR_PHOTON | BEHAVIOR_GLUON | BEHAVIOR_GRAVITON |
                     BEHAVIOR_DARK_ENERGY)) return false;
        return PHYS_REST_MASS_MEV[t] > 0.1f;
    };

    // Carrier candidate result
    struct CarrierMatch {
        uint32_t carrier_type;
        float    probability;
        PhysicsInterface::DecayEventType evt;
    };

    // Shuffle start index to avoid bias
    std::uniform_int_distribution<uint32_t> start_dist(0, n > 1 ? n - 1 : 0);
    uint32_t start_idx = start_dist(rng);

    for (uint32_t iter = 0; iter < n && spawned < max_spawn; ++iter) {
        uint32_t i = (start_idx + iter) % n;
        if (readback_energies_[i] < 0.05f) continue;

        uint32_t type_i = particles.types[i];
        if (type_i >= PHYS_PARTICLE_TYPES) continue;
        uint32_t flags_i = particles.behavior_flags[type_i];

        // Skip existing force carriers, virtual particles, dark energy quanta
        if (flags_i & (BEHAVIOR_PHOTON | BEHAVIOR_VIRTUAL | BEHAVIOR_GLUON |
                       BEHAVIOR_WEAK_BOSON | BEHAVIOR_HIGGS | BEHAVIOR_GRAVITON |
                       BEHAVIOR_DARK_ENERGY)) continue;

        float charge_i = PHYS_CHARGE[type_i];
        float mass_i = PHYS_REST_MASS_MEV[type_i];
        bool is_quark_i = (flags_i & BEHAVIOR_QUARK) != 0;
        bool is_charged_i = std::abs(charge_i) > 0.01f;
        bool is_neutrino_i = is_neutrino_flags(flags_i);
        bool is_baryon_i = is_baryon(type_i);

        // Every matter particle can participate in at least gravity
        // so we don't filter here — carrier selection happens per-pair

        // Find nearest suitable partner
        float best_dist_sq = radius_sq;
        uint32_t best_j = UINT32_MAX;
        CarrierMatch best_match{UINT32_MAX, 0.0f, PhysicsInterface::DEVT_CARRIER_EM};

        auto search = [&](uint32_t j) {
            if (j == i) return;
            if (readback_energies_[j] < 0.01f) return;
            uint32_t type_j = particles.types[j];
            if (type_j >= PHYS_PARTICLE_TYPES) return;
            uint32_t flags_j = particles.behavior_flags[type_j];
            if (flags_j & (BEHAVIOR_PHOTON | BEHAVIOR_VIRTUAL | BEHAVIOR_GLUON |
                           BEHAVIOR_WEAK_BOSON | BEHAVIOR_HIGGS | BEHAVIOR_GRAVITON |
                           BEHAVIOR_DARK_ENERGY)) return;

            glm::vec2 delta = readback_positions_[j] - readback_positions_[i];
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 >= best_dist_sq) return;

            float charge_j = PHYS_CHARGE[type_j];
            bool is_quark_j = (flags_j & BEHAVIOR_QUARK) != 0;
            bool is_charged_j = std::abs(charge_j) > 0.01f;
            bool is_neutrino_j = is_neutrino_flags(flags_j);
            bool is_baryon_j = is_baryon(type_j);

            // ── Determine carrier by interaction type (priority order) ──
            CarrierMatch m{UINT32_MAX, 0.0f, PhysicsInterface::DEVT_CARRIER_EM};

            // 1) QCD color: quark ↔ quark → gluon
            if (is_quark_i && is_quark_j) {
                m = {GLUON_TYPE_PHYS, 0.005f * cfg.alpha_s_scale,
                     PhysicsInterface::DEVT_CARRIER_QCD};
            }
            // 2) Yukawa nuclear: baryon ↔ baryon → gluon (nuclear force mediator)
            else if (is_baryon_i && is_baryon_j) {
                m = {GLUON_TYPE_PHYS, 0.003f * cfg.yukawa_strength,
                     PhysicsInterface::DEVT_CARRIER_NUCLEAR};
            }
            // 3) EM Coulomb: charged ↔ charged → photon
            else if (is_charged_i && is_charged_j) {
                float q_prod = std::abs(charge_i * charge_j);
                m = {PHOTON_TYPE_PHYS, 0.003f * q_prod,
                     PhysicsInterface::DEVT_CARRIER_EM};
            }
            // 4) Weak CC: lepton ↔ neutrino → W±
            else if ((is_cc_lepton(type_i) && is_neutrino_j) ||
                     (is_neutrino_i && is_cc_lepton(type_j))) {
                float q_sum = charge_i + charge_j;
                uint32_t w_type = (q_sum < 0.0f) ? W_MINUS_TYPE_PHYS : W_PLUS_TYPE_PHYS;
                m = {w_type, 0.0003f, PhysicsInterface::DEVT_CARRIER_WEAK};
            }
            // 5) Weak CC: quark ↔ lepton/neutrino → W±
            else if ((is_quark_i && !is_quark_j) || (!is_quark_i && is_quark_j)) {
                float q_sum = charge_i + charge_j;
                uint32_t w_type = (q_sum < 0.0f) ? W_MINUS_TYPE_PHYS : W_PLUS_TYPE_PHYS;
                m = {w_type, 0.0004f, PhysicsInterface::DEVT_CARRIER_WEAK};
            }
            // 6) Weak NC: neutrino ↔ neutrino → Z⁰
            else if (is_neutrino_i && is_neutrino_j) {
                m = {Z_BOSON_TYPE_PHYS, 0.0002f, PhysicsInterface::DEVT_CARRIER_WEAK};
            }
            // 7) Neutral ↔ neutral (DM, neutron↔neutrino, etc.) → Z⁰ (weak NC)
            else if (!is_charged_i && !is_charged_j && !is_quark_i && !is_quark_j) {
                m = {Z_BOSON_TYPE_PHYS, 0.0002f, PhysicsInterface::DEVT_CARRIER_WEAK};
            }

            if (m.carrier_type != UINT32_MAX) {
                best_dist_sq = d2;
                best_j = j;
                best_match = m;
            }
        };

        if (iface.prefs.spatial_grid)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, radius, search);
        else
            for (uint32_t j = 0; j < n; ++j) search(j);

        if (best_j == UINT32_MAX) continue;

        // ── Rare carrier overrides: graviton & Higgs ──
        // These can override the primary carrier via separate probability rolls
        uint32_t final_carrier = best_match.carrier_type;
        float final_P = best_match.probability;
        auto final_evt = best_match.evt;

        uint32_t type_j = particles.types[best_j];
        float mass_j = (type_j < PHYS_PARTICLE_TYPES)
            ? PHYS_REST_MASS_MEV[type_j] : 0.0f;
        uint32_t flags_j = (type_j < PHYS_PARTICLE_TYPES)
            ? particles.behavior_flags[type_j] : 0u;

        // Graviton: any two massive particles (very rare, scales with mass product)
        if (mass_i > 0.1f && mass_j > 0.1f && cfg.gravity_strength > 0.0f) {
            float P_grav = 0.0002f * std::min(1.0f, (mass_i * mass_j) / (938.0f * 938.0f));
            if (prob(rng) < P_grav) {
                final_carrier = GRAVITON_TYPE_PHYS;
                final_P = 1.0f;  // already passed probability gate
                final_evt = PhysicsInterface::DEVT_CARRIER_GRAVITY;
            }
        }

        // Higgs: massive fermion ↔ massive fermion (scales with heavier mass)
        if (final_carrier != GRAVITON_TYPE_PHYS &&
            is_massive_fermion(type_i, flags_i) &&
            is_massive_fermion(particles.types[best_j], flags_j)) {
            float heavier = std::max(mass_i, mass_j);
            float P_higgs = 0.0001f * std::min(1.0f, heavier / 125100.0f);
            // Top quark has strongest Yukawa coupling to Higgs
            if (type_i == TOP_QUARK_TYPE || type_i == ANTI_TOP_TYPE ||
                particles.types[best_j] == TOP_QUARK_TYPE ||
                particles.types[best_j] == ANTI_TOP_TYPE)
                P_higgs *= 50.0f;
            if (prob(rng) < P_higgs) {
                final_carrier = HIGGS_TYPE_PHYS;
                final_P = 1.0f;
                final_evt = PhysicsInterface::DEVT_CARRIER_HIGGS;
            }
        }

        // Primary probability gate (graviton/Higgs already gated above)
        if (final_P < 1.0f && prob(rng) >= final_P) continue;

        // ── Spawn carrier ──
        uint32_t slot = find_dormant(i + 1);
        if (slot == UINT32_MAX) continue;

        glm::vec2 dir = readback_positions_[best_j] - readback_positions_[i];
        float dist = std::sqrt(best_dist_sq);
        if (dist > 0.1f) dir /= dist;
        else dir = glm::vec2(1.0f, 0.0f);

        // Energy: small fraction of source particle, capped
        float E_carrier = readback_energies_[i] * 0.08f;
        E_carrier = std::min(E_carrier, 0.3f);
        readback_energies_[i] -= E_carrier;

        write_spawn_genome(particles, slot, final_carrier, rng, frame_counter_);

        // Spawn at source particle, offset slightly toward partner
        readback_positions_[slot] = readback_positions_[i] + dir * 4.0f;

        // Massless carriers at C_SIM, massive (W/Z/Higgs) slower
        bool massless_carrier = (final_carrier == PHOTON_TYPE_PHYS ||
                                 final_carrier == GLUON_TYPE_PHYS ||
                                 final_carrier == GRAVITON_TYPE_PHYS);
        float speed = massless_carrier ? C_SIM : C_SIM * 0.5f;
        readback_velocities_[slot] = dir * speed;
        readback_energies_[slot] = E_carrier;

        // Mark as virtual (short lifetime)
        particles.genomes[slot * GENOME_SIZE + 3] = 0.5f;

        particles.orbital_parent[slot] = -1;
        particles.orbital_shell[slot] = -1;

        // ── Event log — distinct type per carrier ──
        {
            const char* carrier_name = "?";
            ImVec4 col(0.6f, 0.4f, 1.0f, 1.0f);
            if (final_carrier == PHOTON_TYPE_PHYS) {
                carrier_name = "\xce\xb3"; col = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
            } else if (final_carrier == GLUON_TYPE_PHYS && final_evt == PhysicsInterface::DEVT_CARRIER_NUCLEAR) {
                carrier_name = "g(nuc)"; col = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
            } else if (final_carrier == GLUON_TYPE_PHYS) {
                carrier_name = "g"; col = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            } else if (final_carrier == W_PLUS_TYPE_PHYS) {
                carrier_name = "W\xe2\x81\xba"; col = ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
            } else if (final_carrier == W_MINUS_TYPE_PHYS) {
                carrier_name = "W\xe2\x81\xbb"; col = ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
            } else if (final_carrier == Z_BOSON_TYPE_PHYS) {
                carrier_name = "Z\xc2\xb0"; col = ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
            } else if (final_carrier == GRAVITON_TYPE_PHYS) {
                carrier_name = "G"; col = ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
            } else if (final_carrier == HIGGS_TYPE_PHYS) {
                carrier_name = "H"; col = ImVec4(1.0f, 0.5f, 0.8f, 1.0f);
            }
            char msg[128], detail[256];
            const char* src_name = (type_i < PHYS_PARTICLE_TYPES)
                ? PHYS_TYPE_NAMES[type_i] : "?";
            const char* dst_name = (type_j < PHYS_PARTICLE_TYPES)
                ? PHYS_TYPE_NAMES[type_j] : "?";
            snprintf(msg, sizeof(msg), "%s \xe2\x86\x94 %s via %s",
                     src_name, dst_name, carrier_name);
            snprintf(detail, sizeof(detail),
                     "Carrier exchange: %s emitted from #%u (%s) toward #%u (%s)\n"
                     "Energy: %.3f MeV, distance: %.1f px",
                     carrier_name, i, src_name, best_j, dst_name,
                     E_carrier, dist);
            iface.push_decay_event(msg, final_evt, col, std::string(detail));
        }

        ++spawned;
    }

    if (spawned > 0)
        cpu_particles_dirty_ = true;
}

// ── Quasiparticle spawning ───────────────────────────────────────────────────
// Plasmon, Phonon, Magnon, Polaron, Cooper Pair, Roton — environment-dependent

void PhysicsSimulation::check_quasiparticles() {
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 7919u + 104729u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle(0.0f, 6.2832f);
    int spawned = 0;
    const int MAX_QP_PER_TICK = 3;
    uint32_t env = cfg.environment_mode;

    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t k = start; k < n; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        for (uint32_t k = 0; k < start; ++k)
            if (readback_energies_[k] < 0.01f) return k;
        return UINT32_MAX;
    };

    auto spawn_qp = [&](uint32_t slot, uint32_t qp_type, glm::vec2 pos,
                         glm::vec2 vel, float energy, const char* msg, ImVec4 col,
                         const std::string& detail = "") {
        write_spawn_genome(particles, slot, qp_type, rng, frame_counter_);
        readback_positions_[slot] = pos;
        readback_velocities_[slot] = vel;
        readback_energies_[slot] = energy;
        particles.orbital_parent[slot] = -1;
        particles.orbital_shell[slot] = -1;
        iface.push_decay_event(msg, PhysicsInterface::DEVT_QUASIPARTICLE, col, detail);
        ++spawned;
    };

    // Shuffle start to avoid bias
    std::uniform_int_distribution<uint32_t> start_dist(0, n > 1 ? n - 1 : 0);
    uint32_t start_idx = start_dist(rng);

    // ── Plasmon: collective electron oscillation in plasma ──
    // Envs: Hydrogen Plasma(1), Solar Core(3), QGP(7), Electroweak(8), Big Bang(13)
    bool plasma_env = (env == 1 || env == 3 || env == 7 || env == 8 || env == 13);
    if ((plasma_env || emergent_temperature_ > 500.0f) && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter) % n;
            if (readback_energies_[i] < 0.05f) continue;
            uint32_t ti = particles.types[i];
            if (ti != ELECTRON_TYPE_PHYS) continue;

            // Count nearby electrons (density check)
            int electron_count = 0;
            glm::vec2 centroid(0.0f);
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 40.0f, [&](uint32_t j) {
                if (j == i) return;
                if (readback_energies_[j] < 0.01f) return;
                if (particles.types[j] == ELECTRON_TYPE_PHYS && electron_count < 8) {
                    centroid += readback_positions_[j];
                    ++electron_count;
                }
            });

            if (electron_count < 3) continue;
            if (prob(rng) >= 0.002f) continue;

            uint32_t slot = find_dormant(i + 1);
            if (slot == UINT32_MAX) break;

            centroid = (centroid + readback_positions_[i]) / float(electron_count + 1);
            float a = angle(rng);
            float speed = 20.0f + emergent_temperature_ * 0.05f;
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "Collective oscillation of %d electrons within 40px\n"
                     "Seed electron #%u  E: %.3f MeV\n"
                     "Emergent temp: %.1f  |  Env: %u\n"
                     "Spawned at centroid → slot #%u  speed: %.1f",
                     electron_count + 1, i, readback_energies_[i],
                     emergent_temperature_, env, slot, speed);
            spawn_qp(slot, PLASMON_TYPE_PHYS, centroid,
                      glm::vec2(std::cos(a), std::sin(a)) * speed,
                      0.05f, "Plasmon (electron density oscillation)",
                      ImVec4(0.3f, 1.0f, 0.95f, 1.0f), std::string(detail));
            break;  // one plasmon attempt per tick
        }
    }

    // ── Phonon: lattice vibration in dense nucleon environments ──
    bool neutron_star = (env == 2);
    if (neutron_star && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter * 7) % n;
            if (readback_energies_[i] < 0.05f) continue;
            uint32_t ti = particles.types[i];
            if (ti != NEUTRON_TYPE && ti != PROTON_TYPE) continue;

            // Count nearby nucleons (lattice density)
            int nucleon_count = 0;
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 20.0f, [&](uint32_t j) {
                if (j == i) return;
                if (readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if ((tj == NEUTRON_TYPE || tj == PROTON_TYPE) && nucleon_count < 4)
                    ++nucleon_count;
            });

            if (nucleon_count < 2) continue;
            if (prob(rng) >= 0.003f) continue;

            uint32_t slot = find_dormant(i + 1);
            if (slot == UINT32_MAX) break;

            float a = angle(rng);
            float speed = 30.0f;
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "Lattice vibration from %d nucleons within 20px\n"
                     "Source %s #%u  E: %.3f MeV\n"
                     "Neutron star env  |  Spawned → slot #%u",
                     nucleon_count + 1, (ti == NEUTRON_TYPE ? "Neutron" : "Proton"),
                     i, readback_energies_[i], slot);
            spawn_qp(slot, PHONON_TYPE_PHYS, readback_positions_[i] + glm::vec2(3.0f, 0.0f),
                      glm::vec2(std::cos(a), std::sin(a)) * speed,
                      0.03f, "Phonon (lattice vibration)",
                      ImVec4(0.95f, 0.95f, 0.4f, 1.0f), std::string(detail));
            break;
        }
    }

    // ── Magnon: spin wave in strong magnetic field ──
    if (emergent_bfield_ > 0.3f && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter * 11) % n;
            if (readback_energies_[i] < 0.1f) continue;
            uint32_t ti = particles.types[i];
            if (ti >= PHYS_PARTICLE_TYPES) continue;
            float qi = PHYS_CHARGE[ti];
            if (std::abs(qi) < 0.01f) continue;  // only from moving charges

            float v_mag = std::sqrt(readback_velocities_[i].x * readback_velocities_[i].x +
                                    readback_velocities_[i].y * readback_velocities_[i].y);
            if (v_mag < 0.3f * C_SIM) continue;

            float P = 0.001f * emergent_bfield_;
            if (prob(rng) >= P) continue;

            uint32_t slot = find_dormant(i + 1);
            if (slot == UINT32_MAX) break;

            // Perpendicular to velocity
            glm::vec2 perp(-readback_velocities_[i].y, readback_velocities_[i].x);
            if (v_mag > 0.1f) perp /= v_mag;
            float speed = 40.0f;
            const char* src_name = (ti < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_NAMES[ti] : "?";
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "Spin wave from fast charged particle\n"
                     "%s #%u  q=%.1f  |v|=%.2fc  E: %.3f MeV\n"
                     "Emergent B-field: %.3f  |  P(spawn): %.4f\n"
                     "Launched perpendicular → slot #%u",
                     src_name, i, qi, v_mag / C_SIM, readback_energies_[i],
                     emergent_bfield_, P, slot);
            spawn_qp(slot, MAGNON_TYPE_PHYS, readback_positions_[i] + perp * 3.0f,
                      perp * speed,
                      0.04f, "Magnon (spin wave excitation)",
                      ImVec4(1.0f, 0.45f, 0.15f, 1.0f), std::string(detail));
            break;
        }
    }

    // ── Polaron: electron dressed by ion cloud in dense plasma ──
    bool dense_env = (env == 2 || env == 3 || env == 7);
    if (dense_env && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter * 13) % n;
            if (readback_energies_[i] < 0.05f) continue;
            if (particles.types[i] != ELECTRON_TYPE_PHYS) continue;

            // Count nearby ions
            int ion_count = 0;
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 30.0f, [&](uint32_t j) {
                if (j == i) return;
                if (readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj < PHYS_PARTICLE_TYPES && PHYS_CHARGE[tj] > 0.5f && ion_count < 4)
                    ++ion_count;
            });

            if (ion_count < 2) continue;
            if (prob(rng) >= 0.001f) continue;

            // Transform the electron into a polaron in-place
            float E_old = readback_energies_[i];
            float v_old = std::sqrt(readback_velocities_[i].x * readback_velocities_[i].x +
                                    readback_velocities_[i].y * readback_velocities_[i].y);
            write_spawn_genome(particles, i, POLARON_TYPE_PHYS, rng, frame_counter_);
            readback_energies_[i] = E_old;
            // Slow down (heavier effective mass)
            readback_velocities_[i] *= 0.5f;
            char pol_detail[256];
            snprintf(pol_detail, sizeof(pol_detail),
                     "Electron #%u dressed by %d nearby ions\n"
                     "E: %.3f MeV  |v|: %.2fc → %.2fc (mass drag)\n"
                     "Env: %u  |  In-place type conversion",
                     i, ion_count, E_old, v_old / C_SIM, v_old * 0.5f / C_SIM, env);
            iface.push_decay_event("e\xe2\x81\xbb \xe2\x86\x92 Polaron (ion cloud dressing)",
                PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.7f, 0.35f, 0.9f, 1.0f),
                std::string(pol_detail));
            ++spawned;
            break;
        }
    }

    // ── Cooper Pair: paired neutrons in cold superfluid ──
    if (neutron_star && emergent_temperature_ < 200.0f && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter * 17) % n;
            if (readback_energies_[i] < 0.05f) continue;
            if (particles.types[i] != NEUTRON_TYPE) continue;

            float vi_mag = std::sqrt(readback_velocities_[i].x * readback_velocities_[i].x +
                                     readback_velocities_[i].y * readback_velocities_[i].y);
            if (vi_mag > 0.1f * C_SIM) continue;  // must be slow

            // Find a nearby slow neutron partner
            uint32_t partner = UINT32_MAX;
            float best_d2 = 15.0f * 15.0f;
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 15.0f, [&](uint32_t j) {
                if (j == i || j == partner) return;
                if (readback_energies_[j] < 0.01f) return;
                if (particles.types[j] != NEUTRON_TYPE) return;
                float vj = std::sqrt(readback_velocities_[j].x * readback_velocities_[j].x +
                                     readback_velocities_[j].y * readback_velocities_[j].y);
                if (vj > 0.1f * C_SIM) return;
                glm::vec2 d = readback_positions_[j] - readback_positions_[i];
                float d2 = d.x * d.x + d.y * d.y;
                if (d2 < best_d2) { best_d2 = d2; partner = j; }
            });

            if (partner == UINT32_MAX) continue;
            if (prob(rng) >= 0.002f) continue;

            // Merge two neutrons into a Cooper pair
            glm::vec2 midpoint = (readback_positions_[i] + readback_positions_[partner]) * 0.5f;
            glm::vec2 total_p = readback_velocities_[i] + readback_velocities_[partner];
            float E_i = readback_energies_[i], E_p = readback_energies_[partner];
            float total_E = E_i + E_p;
            float vp_mag = std::sqrt(readback_velocities_[partner].x * readback_velocities_[partner].x +
                                     readback_velocities_[partner].y * readback_velocities_[partner].y);

            // Build detail before we zero the sources
            char cp_detail[256];
            snprintf(cp_detail, sizeof(cp_detail),
                     "Neutron pairing in cold superfluid\n"
                     "n #%u  E: %.3f MeV  |v|: %.2fc\n"
                     "n #%u  E: %.3f MeV  |v|: %.2fc\n"
                     "Emergent temp: %.1f (< 200 threshold)\n"
                     "Merged \xe2\x86\x92 slot #%u  total E: %.3f MeV",
                     i, E_i, vi_mag / C_SIM,
                     partner, E_p, vp_mag / C_SIM,
                     emergent_temperature_,
                     i, total_E);

            // Deactivate both neutrons
            readback_energies_[i] = 0.0f;
            readback_velocities_[i] = glm::vec2(0.0f);
            readback_energies_[partner] = 0.0f;
            readback_velocities_[partner] = glm::vec2(0.0f);

            // Spawn Cooper pair in one of the slots
            write_spawn_genome(particles, i, COOPER_PAIR_TYPE_PHYS, rng, frame_counter_);
            readback_positions_[i] = midpoint;
            readback_velocities_[i] = total_p * 0.5f;  // conserve momentum
            readback_energies_[i] = total_E;
            particles.orbital_parent[i] = -1;
            particles.orbital_shell[i] = -1;
            iface.push_decay_event("n + n \xe2\x86\x92 Cooper Pair (superfluid condensate)",
                PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                std::string(cp_detail));
            ++spawned;
            cpu_particles_dirty_ = true;
            break;
        }
    }

    // ── Roton: vortex excitation from energetic Cooper Pairs ──
    if (neutron_star && spawned < MAX_QP_PER_TICK) {
        for (uint32_t iter = 0; iter < n && spawned < MAX_QP_PER_TICK; ++iter) {
            uint32_t i = (start_idx + iter * 19) % n;
            if (readback_energies_[i] < 0.2f) continue;
            if (particles.types[i] != COOPER_PAIR_TYPE_PHYS) continue;

            if (prob(rng) >= 0.001f) continue;

            uint32_t slot = find_dormant(i + 1);
            if (slot == UINT32_MAX) break;

            // Spawn perpendicular to Cooper pair's velocity
            glm::vec2 v = readback_velocities_[i];
            float vmag = std::sqrt(v.x * v.x + v.y * v.y);
            glm::vec2 perp = (vmag > 0.1f)
                ? glm::vec2(-v.y, v.x) / vmag
                : glm::vec2(std::cos(angle(rng)), std::sin(angle(rng)));

            float E_roton = readback_energies_[i] * 0.1f;
            readback_energies_[i] -= E_roton;

            char rt_detail[256];
            snprintf(rt_detail, sizeof(rt_detail),
                     "Vortex excitation from energetic Cooper Pair\n"
                     "Cooper Pair #%u  E: %.3f MeV  |v|: %.2fc\n"
                     "Energy transferred: %.3f MeV (10%%)\n"
                     "Launched perpendicular → slot #%u",
                     i, readback_energies_[i] + E_roton, vmag / C_SIM,
                     E_roton, slot);
            spawn_qp(slot, ROTON_TYPE_PHYS, readback_positions_[i] + perp * 4.0f,
                      perp * 25.0f,
                      E_roton, "Roton (superfluid vortex excitation)",
                      ImVec4(0.2f, 0.9f, 0.7f, 1.0f), std::string(rt_detail));
            break;
        }
    }

    if (spawned > 0)
        cpu_particles_dirty_ = true;

    // ── Quasiparticle physics effects on nearby particles ──────────────────
    // Each living quasiparticle exerts real forces on surrounding matter
    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t ti = particles.types[i];

        if (ti == PLASMON_TYPE_PHYS) {
            // Plasmon: oscillating electric field pushes/pulls nearby electrons
            // Phase oscillates with frame counter — creates wave-like sloshing
            float phase = std::sin(float(frame_counter_ + i * 37) * 0.5f);
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 35.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj >= PHYS_PARTICLE_TYPES) return;
                float qj = PHYS_CHARGE[tj];
                if (std::abs(qj) < 0.01f) return;
                glm::vec2 d = readback_positions_[j] - readback_positions_[i];
                float r2 = d.x * d.x + d.y * d.y + 1.0f;
                float r = std::sqrt(r2);
                // Oscillating radial push/pull scaled by charge
                float strength = phase * qj * 8.0f / r2;
                readback_velocities_[j] += (d / r) * strength;
            });

        } else if (ti == PHONON_TYPE_PHYS) {
            // Phonon: vibrates nearby nucleons, transferring kinetic energy
            glm::vec2 vph = readback_velocities_[i];
            float vph_mag = std::sqrt(vph.x * vph.x + vph.y * vph.y);
            if (vph_mag < 0.1f) continue;
            glm::vec2 vph_dir = vph / vph_mag;
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 20.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj != NEUTRON_TYPE && tj != PROTON_TYPE) return;
                // Kick nucleon along phonon's travel direction
                readback_velocities_[j] += vph_dir * 3.0f;
                readback_energies_[j] += 0.005f;
                // Phonon loses energy as it transfers
                readback_energies_[i] = std::max(0.0f, readback_energies_[i] - 0.002f);
            });

        } else if (ti == MAGNON_TYPE_PHYS) {
            // Magnon: deflects charged particles — magnetic perturbation
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 30.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj >= PHYS_PARTICLE_TYPES) return;
                float qj = PHYS_CHARGE[tj];
                if (std::abs(qj) < 0.01f) return;
                // Lorentz-like deflection: F perpendicular to v
                glm::vec2 vj = readback_velocities_[j];
                float deflect = qj * 4.0f * emergent_bfield_;
                readback_velocities_[j] += glm::vec2(-vj.y, vj.x) * deflect * 0.01f;
            });

        } else if (ti == POLARON_TYPE_PHYS) {
            // Polaron: drags nearby ions slightly — mutual coupling
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 25.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj >= PHYS_PARTICLE_TYPES) return;
                if (PHYS_CHARGE[tj] < 0.5f) return;  // only positive ions
                glm::vec2 d = readback_positions_[i] - readback_positions_[j];
                float r2 = d.x * d.x + d.y * d.y + 1.0f;
                // Attract ions toward polaron (phonon cloud polarization)
                readback_velocities_[j] += d * (2.0f / r2);
            });

        } else if (ti == COOPER_PAIR_TYPE_PHYS) {
            // Cooper Pair: superfluid effect — reduces scattering of nearby particles
            // Nearby particles lose less energy (friction damping reduced)
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 30.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj > NEUTRON_TYPE && tj != PROTON_TYPE) return;  // only nucleons
                // Slight energy boost — superfluidity prevents dissipation
                float v_mag = std::sqrt(readback_velocities_[j].x * readback_velocities_[j].x +
                                        readback_velocities_[j].y * readback_velocities_[j].y);
                if (v_mag > 0.01f && v_mag < 0.2f * C_SIM) {
                    // Gentle push to maintain flow (reduce friction)
                    readback_velocities_[j] *= 1.005f;
                }
            });

        } else if (ti == ROTON_TYPE_PHYS) {
            // Roton: vortex — creates rotational velocity field
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 25.0f, [&](uint32_t j) {
                if (j == i || readback_energies_[j] < 0.01f) return;
                glm::vec2 d = readback_positions_[j] - readback_positions_[i];
                float r2 = d.x * d.x + d.y * d.y + 1.0f;
                float r = std::sqrt(r2);
                // Tangential velocity — swirl around roton center
                glm::vec2 tangent(-d.y, d.x);
                float vortex_strength = 5.0f / r2;
                readback_velocities_[j] += tangent * (vortex_strength / r);
            });
        }
    }
}

// ── Virtual particle pair creation ───────────────────────────────────────────
