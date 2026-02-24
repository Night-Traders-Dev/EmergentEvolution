#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr uint32_t MAX_PARTICLE_TYPES = 26;
static constexpr uint32_t GROUP_DENSITY      = 256;
static constexpr uint32_t GENOME_SIZE        = 4;   // floats per particle: charge, electronegativity, reactivity, bond_strength

// ── Atom system ───────────────────────────────────────────────────────────────
// H C N O P S Na Cl + Fe Ni Si Ca Ti Sr Au Pb Eu U  (18 atoms total)
static constexpr uint32_t ATOM_COUNT             = 18;
static constexpr uint32_t MAX_BONDS_PER_PARTICLE = 6;   // U has valence 6
static constexpr uint32_t PHOTON_TYPE            = 18u; // particle type index for photons

// Standard Model free particle type indices (beyond atoms + photon)
static constexpr uint32_t ALPHA_TYPE    = 19u;  // He-4 nucleus
static constexpr uint32_t ELECTRON_TYPE = 20u;  // free electron (e-)
static constexpr uint32_t POSITRON_TYPE = 21u;  // positron (e+)
static constexpr uint32_t NEUTRINO_TYPE = 22u;  // electron neutrino
static constexpr uint32_t MUON_TYPE     = 23u;  // muon (μ-)

// Max covalent bonds per particle type (indexed 0–MAX_PARTICLE_TYPES-1)
// Atoms 0–17, photon 18, SM particles 19–23, reserved 24–25
//                                H  C  N  O  P  S Na Cl Fe Ni Si Ca Ti Sr Au Pb Eu  U  γ  α  e- e+ ν  μ  r  r
static constexpr uint32_t ATOM_VALENCE[MAX_PARTICLE_TYPES] = {
    1, 4, 3, 2, 5, 2, 1, 1,  // original 8: H C N O P S Na Cl
    3, 2, 4, 2, 4, 2, 1, 4, 3, 6,  // new 10: Fe Ni Si Ca Ti Sr Au Pb Eu U
    0,            // photon (18)
    0, 0, 0, 0, 0, // alpha e- e+ ν μ (19–23)
    0, 0           // reserved (24–25)
};

enum ParticleBehavior : uint32_t {
    BEHAVIOR_NONE      = 0,
    BEHAVIOR_REPEL     = 1u << 0,  // same-charge ionic repulsion
    BEHAVIOR_POLAR     = 1u << 1,  // dipole rotation (H, O)
    BEHAVIOR_HEAVY     = 1u << 2,  // high-mass atom (P, S, Na, Cl)
    BEHAVIOR_CATALYST  = 1u << 3,  // enzymatic / lowers activation energy (P)
    BEHAVIOR_RADICAL   = 1u << 4,  // unpaired electron — aggressive bonding (was VIRAL)
    BEHAVIOR_ADHESIVE  = 1u << 5,  // ionic pair adhesion (Na-Cl)
    BEHAVIOR_DONOR     = 1u << 6,  // electron donor (was SECRETOR)
    BEHAVIOR_ACCEPTOR  = 1u << 7,  // electron acceptor (was PHOTOSYNTH)
    BEHAVIOR_IONIC_POS = 1u << 8,  // positive ion (Na) (was PREDATOR)
    BEHAVIOR_IONIC_NEG = 1u << 9,  // negative ion (Cl) (was REPRODUCTIVE)
    BEHAVIOR_PHOTON    = 1u << 10, // massless energy carrier; skips normal physics
    // Standard Model / decay product flags
    BEHAVIOR_ALPHA     = 1u << 11, // He-4 nucleus emitted from alpha decay
    BEHAVIOR_LEPTON    = 1u << 12, // free lepton (e-, muon)
    BEHAVIOR_NEUTRINO  = 1u << 13, // near-zero interaction; ballistic like photon
    BEHAVIOR_POSITRON  = 1u << 14, // positron (e+); annihilates with LEPTON
    BEHAVIOR_MUON      = 1u << 15, // heavy lepton; decays to e- on half-life TTL
};


struct PushConstants {
    glm::vec2 region_size;        // 0
    glm::vec2 camera_origin;      // 8
    uint32_t  particle_count;     // 16
    uint32_t  particle_types;     // 20
    uint32_t  step;               // 24
    float     dt;                 // 28
    float     camera_zoom;        // 32
    float     radius;             // 36
    float     dampening;          // 40
    float     repulsion_radius;   // 44
    float     interaction_radius; // 48
    float     density_limit;      // 52
    float     viscosity_strength; // 56
    float     pressure_resistance;// 60
    float     local_density_cap;  // 64
    float     temperature;        // 68  — thermal noise strength
    float     gravity_strength;   // 72  — scaled gravity (0 = off)
    float     lorentz_strength;   // 76  — scaled Lorentz / magnetic force (0 = off)
    float     vacuum_energy;      // 80  — zero-point energy / vacuum fluctuation strength
};
// Size is 84 bytes
static_assert(sizeof(PushConstants) == 84, "PushConstants layout mismatch");

struct SimConfig {
    uint32_t particle_count     = 22500;
    uint32_t particle_types     = 5;
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    uint32_t generation_seed    = 0;

    float radius             = 2.0f;
    float dampening          = 0.85f;
    float repulsion_radius   = 20.0f;
    float interaction_radius = 60.0f;
    float density_limit      = 60.0f;

    // Soft-Body Parameters
    float viscosity_strength  = 0.15f;
    float pressure_resistance = 25.0f;
    float local_density_cap   = 1.0f;

    // Bond parameters
    float    bond_spring_k            = 80.0f;
    float    bond_rest_length         = 22.0f;  // pixels, approx repulsion_radius
    float    bond_break_factor        = 2.2f;   // breaks when dist > rest * factor
    float    bond_form_radius         = 28.0f;
    uint32_t bond_update_interval     = 2;      // update bonds every N frames
    float    bond_activation_energy   = 0.02f;  // min relative KE required to form a bond

    // Thermal noise
    float    temperature              = 0.05f;  // Brownian motion strength (0 = off)

    // Fundamental forces (off by default)
    float    gravity_strength         = 0.0f;   // scaled gravitational attraction
    float    lorentz_strength         = 0.0f;   // scaled Lorentz / magnetic deflection
    float    vacuum_energy            = 0.0f;   // vacuum fluctuation rate + ZPE floor (0=off)

    // Periodic particle spawn
    bool     spawn_enabled   = true;
    float    spawn_interval  = 5.0f;   // seconds between spawn events
    uint32_t spawn_min       = 100;
    uint32_t spawn_max       = 500;

    // Empty-world mode — start with a quiet dormant reservoir instead of a live soup
    bool     start_empty     = false;
    uint32_t pool_size       = 500;    // how many dormant H atoms to pre-allocate

    glm::vec2 camera_origin      = { REGION_W / 2.0f, REGION_H / 2.0f };
    float     camera_zoom        = 1.0f;
    float     current_camera_zoom = 1.0f;
};

// ── Colour helpers ────────────────────────────────────────────────────────────

inline glm::vec4 color_from_hsv(float h, float s, float v, float a = 1.0f) {
    if (s <= 0.0f) return { v, v, v, a };
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float ff = hh - (float)i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * ff);
    float t  = v * (1.0f - s * (1.0f - ff));
    switch (i % 6) {
        case 0: return { v, t, p, a };
        case 1: return { q, v, p, a };
        case 2: return { p, v, t, a };
        case 3: return { p, q, v, a };
        case 4: return { t, p, v, a };
        default: return { v, p, q, a };
    }
}

inline glm::vec4 calc_force_button_color(float force) {
    float abs_f = std::abs(force);
    if (force < 0.0f)
        return color_from_hsv(0.0f,   abs_f, abs_f); // red spectrum
    else
        return color_from_hsv(0.333f, abs_f, abs_f); // green spectrum
}
