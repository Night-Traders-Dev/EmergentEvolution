#pragma once

// Shared inline helpers used across multiple simulation .cpp modules.
// Include this header in any module that calls these utility functions.

#include "physics/phys_particles.h"
#include "particles.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <random>

// ── Orbital constants (single source of truth — must match shader K_COULOMB, SOFTEN_MIN²) ──
inline constexpr float R_BOHR      = 15.0f;   // base Bohr radius (hydrogen ground state in px)
inline constexpr float K_COULOMB_O = 1200.0f;  // Coulomb constant (px-based units)
inline constexpr float SOFTEN_SQ_O = 4.0f;     // softening = SOFTEN_MIN² = 2² (matches shader)
inline constexpr int   SHELL_CAP_O[] = {2, 8, 18, 32};
inline constexpr int   NUM_SHELLS_O  = 4;

// ── SM particle labels (for notifications & event log) ──────────────────────
inline const char* const SM_LABELS[] = {
    "p","n","e\xe2\x81\xbb","\xce\xb3","e\xe2\x81\xba","p\xcc\x84",
    "\xce\xbd" "e","\xce\xbc\xe2\x81\xbb","\xce\xbc\xe2\x81\xba",
    "\xcf\x84\xe2\x81\xbb","\xcf\x84\xe2\x81\xba","\xce\xbd\xce\xbc","\xce\xbd\xcf\x84",
    "u","d","s","c","t","b",
    "u\xcc\x84","d\xcc\x84","s\xcc\x84","c\xcc\x84","t\xcc\x84","b\xcc\x84",
    "g","W\xe2\x81\xba","W\xe2\x81\xbb","Z\xe2\x81\xb0","H",
    "G","DM","DE"
};

// ── Lorentz gamma factor ────────────────────────────────────────────────────
inline float compute_gamma(const glm::vec2& vel) {
    float beta = std::min(glm::length(vel) / C_SIM, 0.9999f);
    return 1.0f / std::sqrt(1.0f - beta * beta);
}

// Random start index for unbiased particle iteration.
// Prevents systematic bias toward low-index particles when loops have early exits.
// Usage: for (uint32_t _it = 0; _it < n; ++_it) { uint32_t i = (start + _it) % n; ... }
inline uint32_t random_start(uint32_t n, uint32_t frame, uint32_t salt) {
    if (n == 0) return 0;
    uint32_t h = frame * 2654435761u ^ salt;
    h ^= (h >> 16); h *= 0x45d9f3bu; h ^= (h >> 16);
    return h % n;
}

// Total relativistic energy E = γm₀c² (in MeV)
inline float total_rel_energy(uint32_t type, const glm::vec2& vel) {
    float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
    if (m0 < 0.001f) return glm::length(vel) / C_SIM * 1.0f; // massless: E ∝ β
    return compute_gamma(vel) * m0;
}

// Convert kinetic energy (MeV) to sim speed for a given particle type
inline float ke_to_speed(float KE_MeV, uint32_t type) {
    float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
    if (m0 < 0.001f) return C_SIM;  // massless always at c
    float gamma = 1.0f + KE_MeV / m0;
    float beta = std::sqrt(std::max(0.0f, 1.0f - 1.0f / (gamma * gamma)));
    return beta * C_SIM;
}

// Convert MeV to energy buffer [0, 1]
inline float mev_to_ebuf(float MeV) {
    return std::clamp(MeV / E_SCALE_MEV, 0.0f, 1.0f);
}

// 2-body decay kinematics: returns child momentum magnitude in parent rest frame (MeV/c)
inline float two_body_decay_momentum(float M, float m1, float m2) {
    float sum = m1 + m2, diff = m1 - m2;
    float arg = (M * M - sum * sum) * (M * M - diff * diff);
    return (arg > 0.0f) ? std::sqrt(arg) / (2.0f * M) : 0.0f;
}

// ── Helper: write genome for a particle type ─────────────────────────────────

inline void write_spawn_genome(Particles& particles, uint32_t slot, uint32_t type,
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
