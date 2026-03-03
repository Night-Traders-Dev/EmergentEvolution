// cp_violation.cpp — Neutral meson oscillation (K⁰↔K̄⁰, B⁰↔B̄⁰, Bs⁰↔B̄s⁰)
//                    and CP violation tracking.
//
// Follows the same structure as check_neutrino_oscillations() in quantum.cpp.
// Called from simulation.cpp tick() right before check_meson_decays().

#include "physics/core/simulation.h"
#include "physics/ui/interface.h"
#include "physics/core/phys_particles.h"
#include "physics/data/meson_data.h"
#include "physics/core/sim_helpers.h"
#include <cmath>
#include <random>

// ── Neutral meson mixing pairs ──────────────────────────────────────────────

struct MesonMixingPair {
    uint32_t type_a;       // particle (e.g., K⁰)
    uint32_t type_b;       // antiparticle (e.g., K̄⁰)
    float    delta_m;      // simulation-scaled mass difference
    float    dg_ratio;     // ΔΓ / Γ_avg (width difference ratio)
    const char* label_ab;  // "K⁰ → K̄⁰"
    const char* label_ba;  // "K̄⁰ → K⁰"
};

static const MesonMixingPair MIXING_PAIRS[] = {
    { KAON_ZERO_MESON, KAON_ZERO_BAR_MESON,
      KAON_DM_SIM, KAON_DG_RATIO,
      "K\xc2\xb0 \xe2\x86\x92 K\xcc\x84\xc2\xb0",      // K⁰ → K̄⁰
      "K\xcc\x84\xc2\xb0 \xe2\x86\x92 K\xc2\xb0" },     // K̄⁰ → K⁰

    { B_ZERO_MESON, B_ZERO_BAR_MESON,
      BD_DM_SIM, BD_DG_RATIO,
      "B\xc2\xb0 \xe2\x86\x92 B\xcc\x84\xc2\xb0",      // B⁰ → B̄⁰
      "B\xcc\x84\xc2\xb0 \xe2\x86\x92 B\xc2\xb0" },     // B̄⁰ → B⁰

    { BS_ZERO_MESON, BS_ZERO_BAR_MESON,
      BS_DM_SIM, BS_DG_RATIO,
      "Bs\xc2\xb0 \xe2\x86\x92 B\xcc\x84s\xc2\xb0",    // Bs⁰ → B̄s⁰
      "B\xcc\x84s\xc2\xb0 \xe2\x86\x92 Bs\xc2\xb0" },   // B̄s⁰ → Bs⁰
};
static constexpr int NUM_MIXING_PAIRS = 3;

// ── Neutral meson oscillation check ─────────────────────────────────────────

void PhysicsSimulation::check_meson_oscillations() {
    if (!cfg.cp_violation_enabled) return;
    const uint32_t n = cfg.particle_count;
    if (n == 0 || readback_positions_.empty()) return;

    std::mt19937 rng(frame_counter_ * 314159u + 271828u);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);

    uint32_t _rs0 = random_start(n, frame_counter_, 0u);
    for (uint32_t _it = 0; _it < n; ++_it) {
        uint32_t i = (_rs0 + _it) % n;
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t type = particles.types[i];

        for (int mp = 0; mp < NUM_MIXING_PAIRS; ++mp) {
            const auto& pair = MIXING_PAIRS[mp];
            bool is_a = (type == pair.type_a);
            bool is_b = (type == pair.type_b);
            if (!is_a && !is_b) continue;

            // Age in frames since creation
            uint32_t birth = (i < particles.birth_frames.size()) ? particles.birth_frames[i] : 0;
            uint32_t age = frame_counter_ - birth;
            float t = static_cast<float>(age);

            // Oscillation probability:
            //   P(M → M̄) = ½ (1 - cos(Δm·t)) · exp(-ΔΓ·t/2)
            // Cosine term gives oscillatory mixing; exponential damps as
            // the short-lived mass eigenstate decays away.
            float dm_arg = pair.delta_m * t;
            float P_osc = 0.5f * (1.0f - std::cos(dm_arg))
                        * std::exp(-pair.dg_ratio * t * 0.5f);
            P_osc = std::min(P_osc, 0.5f);  // physical maximum is 50%

            if (prob(rng) >= P_osc) break;  // no oscillation this tick

            // Oscillate: flip to conjugate type
            uint32_t new_type = is_a ? pair.type_b : pair.type_a;

            // Preserve birth frame (age matters for coherent oscillation)
            uint32_t saved_birth = birth;
            write_spawn_genome(particles, i, new_type, rng, frame_counter_);
            if (i < particles.birth_frames.size())
                particles.birth_frames[i] = saved_birth;

            // Event logging
            const char* osc_label = is_a ? pair.label_ab : pair.label_ba;
            char msg[128];
            snprintf(msg, sizeof(msg), "Oscillation: %s (#%u)", osc_label, i);
            iface.push_notification(msg, ImVec4(0.85f, 0.6f, 1.0f, 1.0f));

            char detail[256];
            snprintf(detail, sizeof(detail),
                     "Particle #%u  \xce\x94m=%.3f  P=%.4f\n"
                     "Age: %u frames  E=%.4f",
                     i, pair.delta_m, P_osc, age, readback_energies_[i]);
            iface.push_decay_event(msg, PhysicsInterface::DEVT_CP_VIOLATION,
                                   ImVec4(0.85f, 0.6f, 1.0f, 1.0f), std::string(detail));

            achievements.seen_meson_oscillation = true;
            achievements.total_meson_oscillations++;
            cpu_particles_dirty_ = true;
            break;  // only match one pair per particle
        }
    }
}
