// decay.cpp — Particle decay & hadronization (split from simulation.cpp)
//
// Implements particle decay, hadronization, bremsstrahlung,
// and weak flavor changes.

#include "physics/simulation.h"
#include "physics/sim_helpers.h"
#include "physics/meson_data.h"
#include <algorithm>
#include <cmath>
#include <random>

// ── Quark pair → meson type mapping ──────────────────────────────────────────
// Maps quark + antiquark flavors to the appropriate meson type.
// High-energy pairs produce vector mesons; low-energy produce pseudoscalars.

static uint32_t quark_pair_to_meson(uint32_t q_type, uint32_t qbar_type, float energy) {
    // Map quark type to flavor index: u=0, d=1, s=2, c=3, t=4, b=5
    auto flavor = [](uint32_t t) -> int {
        if (t >= UP_QUARK_TYPE && t <= BOTTOM_QUARK_TYPE) return t - UP_QUARK_TYPE;
        if (t >= ANTI_UP_TYPE && t <= ANTI_BOTTOM_TYPE) return t - ANTI_UP_TYPE;
        return -1;
    };

    int fq  = flavor(q_type);
    int fqb = flavor(qbar_type);
    if (fq < 0 || fqb < 0) return PION_ZERO_MESON;

    bool high_e = energy > 800.0f;

    // u + d̄ → π⁺ / ρ⁺
    if (fq == 0 && fqb == 1) return high_e ? RHO_770_PLUS : PION_PLUS_MESON;
    // d + ū → π⁻ / ρ⁻
    if (fq == 1 && fqb == 0) return high_e ? RHO_770_MINUS : PION_MINUS_MESON;
    // u + ū or d + d̄ → π⁰ / ρ⁰
    if ((fq == 0 && fqb == 0) || (fq == 1 && fqb == 1))
        return high_e ? RHO_770_ZERO : PION_ZERO_MESON;
    // s + s̄ → η / φ
    if (fq == 2 && fqb == 2) return high_e ? PHI_1020_MESON : ETA_MESON;
    // u + s̄ → K⁺ / K*⁺
    if (fq == 0 && fqb == 2) return high_e ? KSTAR_892_PLUS : KAON_PLUS_MESON;
    // d + s̄ → K⁰ / K*⁰
    if (fq == 1 && fqb == 2) return high_e ? KSTAR_892_ZERO : KAON_ZERO_MESON;
    // s + ū → K⁻ / K*⁻
    if (fq == 2 && fqb == 0) return high_e ? KSTAR_892_MINUS : KAON_MINUS_MESON;
    // s + d̄ → K̄⁰ / K̄*⁰
    if (fq == 2 && fqb == 1) return high_e ? KSTAR_892_ZERO_BAR : KAON_ZERO_BAR_MESON;
    // c + c̄ → ηc / J/ψ
    if (fq == 3 && fqb == 3) return high_e ? JPSI_MESON : ETA_C_1S;
    // b + b̄ → ηb / Υ
    if (fq == 5 && fqb == 5) return high_e ? UPSILON_1S : ETA_B_1S;
    // c + d̄ → D⁺
    if (fq == 3 && fqb == 1) return D_PLUS_MESON;
    // c + ū → D⁰
    if (fq == 3 && fqb == 0) return D_ZERO_MESON;
    // c + s̄ → Ds⁺
    if (fq == 3 && fqb == 2) return DS_PLUS_MESON;
    // d + c̄ → D⁻
    if (fq == 1 && fqb == 3) return D_MINUS_MESON;
    // u + c̄ → D̄⁰
    if (fq == 0 && fqb == 3) return D_ZERO_BAR_MESON;
    // s + c̄ → Ds⁻
    if (fq == 2 && fqb == 3) return DS_MINUS_MESON;
    // u + b̄ → B⁺
    if (fq == 0 && fqb == 5) return B_PLUS_MESON;
    // d + b̄ → B⁰
    if (fq == 1 && fqb == 5) return B_ZERO_MESON;
    // s + b̄ → Bs⁰
    if (fq == 2 && fqb == 5) return BS_ZERO_MESON;
    // b + ū → B⁻
    if (fq == 5 && fqb == 0) return B_MINUS_MESON;
    // b + d̄ → B̄⁰
    if (fq == 5 && fqb == 1) return B_ZERO_BAR_MESON;
    // b + s̄ → B̄s⁰
    if (fq == 5 && fqb == 2) return BS_ZERO_BAR_MESON;
    // c + b̄ → Bc⁺
    if (fq == 3 && fqb == 5) return BC_PLUS_MESON;
    // b + c̄ → Bc⁻
    if (fq == 5 && fqb == 3) return BC_MINUS_MESON;

    return PION_ZERO_MESON;
}

// ── Particle decay ───────────────────────────────────────────────────────────

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

            // ── Electron hole: quasiparticle disappears (vacancy filled thermally) ──
            case ELECTRON_HOLE_TYPE_PHYS: {
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);
                iface.push_decay_event("Electron hole decayed",
                    PhysicsInterface::DEVT_ELECTRON_HOLE, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "");
                break;
            }

            case PLASMON_TYPE_PHYS: {
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);
                iface.push_decay_event("Plasmon damped (Landau)",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.3f, 1.0f, 0.95f, 1.0f), "");
                break;
            }
            case PHONON_TYPE_PHYS: {
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);
                iface.push_decay_event("Phonon scattered (thermal)",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.95f, 0.95f, 0.4f, 1.0f), "");
                break;
            }
            case MAGNON_TYPE_PHYS: {
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);
                iface.push_decay_event("Magnon damped (magnetic)",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(1.0f, 0.45f, 0.15f, 1.0f), "");
                break;
            }
            case POLARON_TYPE_PHYS: {
                // Polaron decays back into a free electron
                particles.types[i] = ELECTRON_TYPE_PHYS;
                write_spawn_genome(particles, i, ELECTRON_TYPE_PHYS, rng, frame_counter_);
                readback_energies_[i] *= 0.8f;  // some energy lost to lattice
                iface.push_decay_event("Polaron \xe2\x86\x92 e\xe2\x81\xbb (cloud dispersed)",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.7f, 0.35f, 0.9f, 1.0f), "");
                break;
            }
            case COOPER_PAIR_TYPE_PHYS: {
                // Cooper pair breaks into two neutrons
                readback_energies_[i] *= 0.5f;
                particles.types[i] = NEUTRON_TYPE;
                write_spawn_genome(particles, i, NEUTRON_TYPE, rng, frame_counter_);
                // Try to spawn second neutron
                for (uint32_t k = 0; k < n; ++k) {
                    if (readback_energies_[k] < 0.01f) {
                        write_spawn_genome(particles, k, NEUTRON_TYPE, rng, frame_counter_);
                        readback_positions_[k] = readback_positions_[i] + glm::vec2(4.0f, 0.0f);
                        readback_velocities_[k] = -readback_velocities_[i] * 0.5f;
                        readback_energies_[k] = readback_energies_[i];
                        particles.orbital_parent[k] = -1;
                        particles.orbital_shell[k] = -1;
                        break;
                    }
                }
                iface.push_decay_event("Cooper pair broken \xe2\x86\x92 n + n",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "");
                break;
            }
            case ROTON_TYPE_PHYS: {
                readback_energies_[i] = 0.0f;
                readback_velocities_[i] = glm::vec2(0.0f);
                iface.push_decay_event("Roton dissipated (viscous)",
                    PhysicsInterface::DEVT_QUASIPARTICLE, ImVec4(0.2f, 0.9f, 0.7f, 1.0f), "");
                break;
            }

            default:
                any_decayed = false;  // unknown type, skip
                break;
        }
    }

    if (any_decayed) {
        cpu_particles_dirty_ = true;
        audio.play(AudioPlayer::SFX_DECAY, frame_counter_);
    }
}

// ── Nuclear isotope decay ────────────────────────────────────────────────────


// ── Hadronization ────────────────────────────────────────────────────────────

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
            uint32_t tj = particles.types[best_j];

            // Determine which is quark and which is antiquark
            uint32_t q_type, qbar_type, q_idx, qbar_idx;
            if (is_quark(ti)) {
                q_type = ti; q_idx = i;
                qbar_type = tj; qbar_idx = best_j;
            } else {
                qbar_type = ti; qbar_idx = i;
                q_type = tj; q_idx = best_j;
            }

            float pair_energy = readback_energies_[i] + readback_energies_[best_j];
            uint32_t meson_type = quark_pair_to_meson(q_type, qbar_type, pair_energy);

            // Convert quark slot → meson particle at center of pair
            glm::vec2 center = (readback_positions_[i] + readback_positions_[best_j]) * 0.5f;
            particles.types[q_idx] = meson_type;
            readback_positions_[q_idx] = center;
            readback_velocities_[q_idx] = (readback_velocities_[i] + readback_velocities_[best_j]) * 0.5f;
            readback_energies_[q_idx] = pair_energy;

            // Set genome for new meson
            if (q_idx * GENOME_SIZE + 3 < particles.genomes.size()) {
                particles.genomes[q_idx * GENOME_SIZE + 0] = PHYS_CHARGE[meson_type];
                particles.genomes[q_idx * GENOME_SIZE + 1] = PHYS_SPIN[meson_type];
                particles.genomes[q_idx * GENOME_SIZE + 2] = 0.0f;
                particles.genomes[q_idx * GENOME_SIZE + 3] = PHYS_DECAY_RATE[meson_type];
            }
            if (q_idx < particles.birth_frames.size())
                particles.birth_frames[q_idx] = frame_counter_;

            // Deactivate the other quark slot
            readback_energies_[qbar_idx] = 0.0f;

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
            achievements.seen_hadronization = true;

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


// ── Bremsstrahlung ───────────────────────────────────────────────────────────

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
        achievements.seen_bremsstrahlung = true;
        achievements.total_bremsstrahlung++;
    }

    if (photons_spawned > 0)
        cpu_particles_dirty_ = true;
}

// ── Neutrino scattering (NC + CC inverse beta decay) ─────────────────────────


// ── Weak flavor change ───────────────────────────────────────────────────────

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
        auto wfc_search = [&](uint32_t j) {
            if (j == i || readback_energies_[j] < 0.1f) return;
            uint32_t tj = particles.types[j];
            if (!is_quark(tj) && !is_antiquark(tj)) return;
            glm::vec2 d = readback_positions_[j] - readback_positions_[i];
            float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best_d2) { best_d2 = d2; best_j = j; }
        };
        if (iface.prefs.spatial_grid) {
            grid_.query(readback_positions_[i].x, readback_positions_[i].y, 3.0f, wfc_search);
        } else {
            for (uint32_t j = 0; j < n; ++j) wfc_search(j);
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
        achievements.seen_weak_decay = true;
        cpu_particles_dirty_ = true;
    }
}

// ── Quantum entanglement update ──────────────────────────────────────────────

