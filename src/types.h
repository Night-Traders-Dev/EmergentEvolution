#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr uint32_t MAX_PARTICLE_TYPES = 36;
static constexpr uint32_t GROUP_DENSITY      = 256;
static constexpr uint32_t GENOME_SIZE        = 4;   // floats per particle: charge, electronegativity, reactivity, bond_strength

// ── Atom system ───────────────────────────────────────────────────────────────
// H C N O P S Na Cl + Fe Ni Si Ca Ti Sr Au Pb Eu U  (18 atoms total)
static constexpr uint32_t ATOM_COUNT             = 18;
static constexpr uint32_t MAX_BONDS_PER_PARTICLE = 6;   // U has valence 6
static constexpr uint32_t PHOTON_TYPE            = 18u; // particle type index for photons

// Standard Model free particle type indices (beyond atoms + photon)
static constexpr uint32_t ALPHA_TYPE         = 19u;  // He-4 nucleus (α)
static constexpr uint32_t ELECTRON_TYPE      = 20u;  // free electron (e⁻)
static constexpr uint32_t POSITRON_TYPE      = 21u;  // positron (e⁺)
static constexpr uint32_t NEUTRINO_TYPE      = 22u;  // electron neutrino (νe)
static constexpr uint32_t MUON_TYPE          = 23u;  // muon (μ⁻)
static constexpr uint32_t TAU_TYPE           = 24u;  // tau lepton (τ⁻)
static constexpr uint32_t MU_NEUTRINO_TYPE   = 25u;  // muon neutrino (νμ)
static constexpr uint32_t TAU_NEUTRINO_TYPE  = 26u;  // tau neutrino (ντ)
static constexpr uint32_t W_BOSON_TYPE       = 27u;  // W± gauge boson (weak force)
static constexpr uint32_t Z_BOSON_TYPE       = 28u;  // Z⁰ gauge boson (weak force)
static constexpr uint32_t HIGGS_TYPE         = 29u;  // Higgs boson (H⁰)

// Max covalent bonds per particle type (indexed 0–MAX_PARTICLE_TYPES-1)
// Atoms 0–17, photon 18, SM particles 19–29, hypothetical 30–32, reserved 33–35
//                                H  C  N  O  P  S Na Cl Fe Ni Si Ca Ti Sr Au Pb Eu  U  γ  α  e- e+ νe μ  τ  νμ ντ W  Z  H  G DM DE  r  r  r
static constexpr uint32_t ATOM_VALENCE[MAX_PARTICLE_TYPES] = {
    1, 4, 3, 2, 5, 2, 1, 1,  // H C N O P S Na Cl (0-7)
    3, 2, 4, 2, 4, 2, 1, 4, 3, 6,  // Fe Ni Si Ca Ti Sr Au Pb Eu U (8-17)
    0,                   // photon (18)
    0, 0, 0, 0, 0,       // α e- e+ νe μ (19-23)
    0, 0, 0, 0, 0, 0,    // τ νμ ντ W Z H (24-29)
    0, 0, 0,             // graviton, dark matter, dark energy (30-32)
    0, 0, 0              // reserved (33-35)
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
    // Per-atom mass tiers — encoded as 1/√(relative_mass) fractions for force scaling
    BEHAVIOR_MASS_MEDIUM = 1u << 16, // C N O  (12-16 amu)  mass_inv=0.27
    BEHAVIOR_MASS_HEAVY  = 1u << 17, // P S Na Cl (23-35 amu)  mass_inv=0.18
    BEHAVIOR_MASS_DENSE  = 1u << 18, // Fe Ni Si Ca Ti (28-59 amu)  mass_inv=0.13
    BEHAVIOR_MASS_ULTRA  = 1u << 19, // Sr Au Pb Eu U (88-238 amu) mass_inv=0.08
    // Sub-atomic / Standard Model particle flags
    BEHAVIOR_QUARK       = 1u << 20, // participates in color confinement (strong force)
    BEHAVIOR_ANTIQUARK   = 1u << 21, // antiquark (opposite color charge)
    BEHAVIOR_WEAK_BOSON  = 1u << 22, // W or Z boson — weak force mediator
    BEHAVIOR_HIGGS       = 1u << 23, // Higgs boson — scalar field coupling
    BEHAVIOR_GLUON       = 1u << 24, // gluon — strong force mediator (massless, colored)
    BEHAVIOR_TAU         = 1u << 25, // tau lepton family (heavy, short-lived)
    // Beyond Standard Model / hypothetical
    BEHAVIOR_GRAVITON    = 1u << 26, // spin-2 massless boson — mediates gravity
    BEHAVIOR_DARK_MATTER = 1u << 27, // WIMP — only gravity + weak, no EM/strong
    BEHAVIOR_DARK_ENERGY = 1u << 28, // quintessence — repulsive field quantum
    BEHAVIOR_VIRTUAL     = 1u << 29, // short-TTL virtual particle (fast energy drain)
};

// ── Force Objects (spawnable stationary force emitters) ──────────────────────

static constexpr uint32_t MAX_FORCE_OBJECTS = 8;

enum ForceObjectType : uint32_t {
    FORCE_OBJ_EM_FIELD     = 0,   // Coulomb-like radial force on charged particles
    FORCE_OBJ_STRONG       = 1,   // Yukawa short-range attraction on baryons
    FORCE_OBJ_WEAK         = 2,   // Short-range weak force boost
    FORCE_OBJ_GRAVITY_WELL = 3,   // Gravitational attraction on massive particles
    FORCE_OBJ_HEAT_SOURCE  = 4,   // Local thermal noise / temperature boost
    FORCE_OBJ_COUNT        = 5
};

// GPU-aligned struct (must match GLSL std430 layout). 32 bytes per object.
struct ForceObject {
    float    x, y;          // world position
    float    strength;      // force multiplier (slider-adjustable, 0.1 to 10.0)
    float    radius;        // effective radius in world units
    uint32_t force_type;    // ForceObjectType enum
    uint32_t active;        // 1=active, 0=inactive
    float    _pad0, _pad1;  // padding to 32 bytes
};
static_assert(sizeof(ForceObject) == 32, "ForceObject must be 32 bytes for std430");

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
    uint32_t  field_flags;        // 84  — bitfield: bit0=EM, 1=strong, 2=weak, 3=gravity, 4=Higgs
    float     weak_coupling;      // 88  — weak force strength
    float     string_tension;     // 92  — quark confinement constant (Cornell potential)
    float     higgs_vev;          // 96  — Higgs vacuum expectation value
    uint32_t  force_object_count; // 100 — number of active force objects (0-8)
    // Per-force multipliers (all default 1.0 = standard model strengths)
    float     coulomb_strength;       // 104 — EM Coulomb force multiplier
    float     yukawa_strength;        // 108 — Strong nuclear binding multiplier
    float     pauli_multiplier;       // 112 — Pauli exclusion strength multiplier
    float     alpha_s_scale;          // 116 — QCD color force multiplier
    float     compton_strength;       // 120 — Compton scattering multiplier
    float     annihilation_strength;  // 124 — Matter-antimatter interaction multiplier
};
// Size is 128 bytes (Vulkan guaranteed minimum maxPushConstantsSize)
static_assert(sizeof(PushConstants) == 128, "PushConstants layout mismatch");

struct SimConfig {
    uint32_t particle_count     = 22500;
    uint32_t particle_types     = 8;       // H C N O P S Na Cl — all essential bioatoms
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    float    force_randomness   = 0.0f;    // 0=pure chemistry  1=pure random
    uint32_t generation_seed    = 0;

    float radius             = 2.0f;
    float dampening          = 0.990f;
    float repulsion_radius   = 1.0f;
    float interaction_radius = 200.0f;
    float density_limit      = 60.0f;

    // Soft-Body Parameters
    float viscosity_strength  = 0.15f;
    float pressure_resistance = 100.0f;
    float local_density_cap   = 1.0f;

    // Bond parameters
    float    bond_spring_k            = 80.0f;
    float    bond_rest_length         = 22.0f;  // pixels, approx repulsion_radius
    float    bond_break_factor        = 2.2f;   // breaks when dist > rest * factor
    float    bond_form_radius         = 28.0f;
    uint32_t bond_update_interval     = 2;      // update bonds every N frames
    float    bond_activation_energy   = 0.02f;  // min relative KE required to form a bond

    // Thermal noise
    float    temperature              = 0.30f;  // Brownian motion strength (0 = off)

    // Fundamental forces
    float    gravity_strength         = 0.0f;   // scaled gravitational attraction
    float    lorentz_strength         = 1.0f;   // scaled Lorentz / magnetic deflection
    float    vacuum_energy            = 0.0f;   // vacuum fluctuation rate + ZPE floor (0=off)

    // Standard Model forces
    uint32_t field_flags              = 0;      // bitfield for field visualization toggles
    float    weak_coupling            = 1.0f;   // weak force strength
    float    string_tension           = 100.0f; // quark confinement linear potential coefficient
    float    higgs_vev                = 246.0f; // Higgs vacuum expectation value

    // Temperature in Kelvin (for physics sim)
    float    temperature_kelvin       = 1.0f;    // actual temperature; converted to noise amplitude

    // Periodic particle spawn
    bool     spawn_enabled   = false;
    float    spawn_interval  = 5.0f;   // seconds between spawn events
    uint32_t spawn_min       = 100;
    uint32_t spawn_max       = 500;

    // Lab mode — start with a quiet dormant reservoir; use F3 to place structures
    bool     start_empty     = true;
    uint32_t pool_size       = 5000;   // how many dormant H atoms to pre-allocate

    // Environment presets
    uint32_t environment_mode = 0;  // 0=Lab 1=Tide Pool 2=Vent 3=Primordial
                                    // 4=Pond 5=Space 6=Nebula 7=Asteroid 8=Comet

    // Virtual particle pair creation
    bool     virtual_pairs_enabled     = true;
    float    virtual_pair_threshold    = 2.0f;   // min combined energy for pair creation
    uint32_t virtual_pair_max_per_tick = 2;      // max pairs spawned per frame

    // Thermodynamic feedback
    bool  thermo_feedback_enabled   = true;
    float thermo_coupling           = 1.0f;   // 0=slider only, 1=fully emergent
    bool  magnetic_feedback_enabled = true;
    float magnetic_coupling         = 1.0f;
    float effective_lorentz         = 1.0f;   // computed each tick, used in push constants

    // Per-force strength multipliers (1.0 = standard model, 0.0 = disabled)
    float    coulomb_strength         = 1.0f;   // EM Coulomb force
    float    yukawa_strength          = 1.0f;   // Strong nuclear binding (Yukawa)
    float    pauli_multiplier         = 1.0f;   // Pauli exclusion repulsion
    float    alpha_s_scale            = 1.0f;   // QCD color force (Cornell)
    float    compton_strength         = 1.0f;   // Compton scattering (photon-matter)
    float    annihilation_strength    = 1.0f;   // Matter-antimatter annihilation

    // Visual
    bool     show_trails        = false;  // particle trail rendering (fade instead of clear)

    // Timestep multiplier (UI-adjustable)
    float    time_scale         = 1.0f;

    // Force objects (transient — set each frame before compute dispatch)
    uint32_t force_object_count = 0;

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
