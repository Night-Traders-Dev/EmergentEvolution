#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr uint32_t WORLD_W            = REGION_W * 4;  // 10240 — simulation world bounds
static constexpr uint32_t WORLD_H            = REGION_H * 4;  // 5760
static constexpr uint32_t MAX_PARTICLE_TYPES = 282;
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
// Atoms 0–17, photon 18, SM particles 19–29, hypothetical 30–66, quasi 67–73, mesons 74–281
static constexpr uint32_t ATOM_VALENCE[MAX_PARTICLE_TYPES] = {
    1, 4, 3, 2, 5, 2, 1, 1,  // H C N O P S Na Cl (0-7)
    3, 2, 4, 2, 4, 2, 1, 4, 3, 6,  // Fe Ni Si Ca Ti Sr Au Pb Eu U (8-17)
    0,                   // photon (18)
    0, 0, 0, 0, 0,       // α e- e+ νe μ (19-23)
    0, 0, 0, 0, 0, 0,    // τ νμ ντ W Z H (24-29)
    0, 0, 0,             // graviton, dark matter, dark energy (30-32)
    0, 0, 0, 0, 0, 0,   // 33-38: DM candidates (axino..Q-ball)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 39-49: SUSY sparticles
    0, 0, 0, 0, 0, 0,   // 50-55: force carriers (gravitino..dilaton)
    0, 0, 0, 0, 0, 0, 0, 0, 0,  // 56-64: theoretical extremes
    0, 0,                // 65-66: new class (paraparticle, dyn axion QP)
    0,                   // 67: electron hole
    0, 0, 0, 0, 0, 0,   // 68-73: quasiparticles (plasmon..roton)
    // 74-281: mesons (208 slots, all valence 0)
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, // 74-113
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, // 114-153
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, // 154-193
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, // 194-233
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, // 234-273
    0,0,0,0,0,0,0,0,     // 274-281
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
    // Hypothetical particle category flags
    BEHAVIOR_SUSY        = 1u << 30, // supersymmetric partner particle
    BEHAVIOR_EXOTIC      = 1u << 31, // theoretical extreme / exotic statistics
};

// ── Force Objects (spawnable stationary force emitters) ──────────────────────

static constexpr uint32_t MAX_FORCE_OBJECTS = 8;

enum ForceObjectType : uint32_t {
    FORCE_OBJ_EM_FIELD     = 0,   // EM field: Coulomb radial + Lorentz deflection on charged particles
    FORCE_OBJ_STRONG       = 1,   // Yukawa short-range attraction on baryons
    FORCE_OBJ_WEAK         = 2,   // Short-range weak force boost
    FORCE_OBJ_GRAVITY_WELL = 3,   // Gravitational attraction on massive particles
    FORCE_OBJ_HEAT_SOURCE  = 4,   // Local thermal noise / temperature boost
    FORCE_OBJ_MIRROR       = 5,   // Reflective line segment (endpoints in x,y and _pad0,_pad1)
    FORCE_OBJ_COULOMB      = 6,   // Pure electrostatic point charge (Coulomb only, no magnetic)
    FORCE_OBJ_VORTEX       = 7,   // Tangential force creating circular flow (cyclotron)
    FORCE_OBJ_POTENTIAL_WELL = 8, // Harmonic trap: F = -k*r restoring force toward center
    FORCE_OBJ_COUNT        = 9
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

    float radius             = 1.0f;
    float dampening          = 0.990f;
    float repulsion_radius   = 1.0f;
    float interaction_radius = 200.0f;
    float density_limit      = 60.0f;

    // Soft-Body Parameters
    float viscosity_strength  = 0.15f;
    float pressure_resistance = 100.0f;
    float local_density_cap   = 1.0f;

    // Bond parameters
    bool     bonds_enabled            = true;
    float    bond_spring_k            = 500.0f;
    float    bond_rest_length         = 36.0f;  // pixels — must exceed largest nuclear pair extent
    float    bond_break_factor        = 2.2f;   // breaks when dist > rest * factor
    float    bond_form_radius         = 44.0f;
    uint32_t bond_update_interval     = 2;      // update bonds every N frames
    float    bond_activation_energy   = 0.02f;  // min relative KE required to form a bond

    // Thermal noise
    float    temperature              = 0.30f;  // Brownian motion strength (0 = off)

    // Fundamental forces
    float    gravity_strength         = 1.0f;   // Newton's law at 1.0
    float    lorentz_strength         = 1.0f;   // scaled Lorentz / magnetic deflection
    float    vacuum_energy            = 0.5f;   // vacuum fluctuation rate + ZPE floor (0=off)

    // Standard Model forces
    uint32_t field_flags              = 0;      // bitfield for field visualization toggles
    float    weak_coupling            = 1.0f;   // weak force strength
    float    string_tension           = 100.0f; // quark confinement linear potential coefficient
    float    higgs_vev                = 246.0f; // Higgs vacuum expectation value
    bool     hadronization_enabled    = true;   // CPU-side color confinement enforcement

    // Temperature in Kelvin (for physics sim)
    float    temperature_kelvin       = 1.0f;    // actual temperature; converted to noise amplitude

    // Periodic particle spawn
    bool     spawn_enabled   = false;
    float    spawn_interval  = 5.0f;   // seconds between spawn events
    uint32_t spawn_min       = 100;
    uint32_t spawn_max       = 500;

    // Lab mode — start with a quiet dormant reservoir; use F3 to place structures
    bool     start_empty     = true;
    uint32_t pool_size       = 100000; // how many dormant slots to pre-allocate

    // Environment presets
    uint32_t environment_mode = 0;  // 0=Lab 1=Tide Pool 2=Vent 3=Primordial
                                    // 4=Pond 5=Space 6=Nebula 7=Asteroid 8=Comet

    // Nuclear reaction toggles
    bool     fusion_enabled            = true;
    bool     fission_enabled           = true;
    bool     spallation_enabled        = true;
    bool     compton_enabled           = true;
    bool     annihilation_enabled      = true;
    bool     decay_enabled             = true;
    bool     nuclear_decay_enabled     = true;

    // Fusion parameters
    float    fusion_threshold_keV      = 550.0f;  // Coulomb barrier for p+p (keV), Gamow peak
    float    fusion_radius             = 8.0f;    // collision distance (px)
    int      max_fusions_per_frame     = 5;
    // Fission parameters
    float    fission_neutron_threshold = 0.6f;    // min neutron energy to trigger fission
    int      min_fission_cluster       = 20;      // min nucleons for fissile nucleus
    int      max_fissions_per_frame    = 2;

    // Particle decay
    float    decay_threshold           = 0.08f;   // energy below which particle decays

    // Nuclear decay
    float    alpha_ke_mev              = 5.0f;    // alpha particle KE (MeV)
    float    nucleon_emit_ke_mev       = 2.0f;    // emitted nucleon KE (MeV)
    float    decay_rate_multiplier     = 1.0f;    // half-life scaler (0.5 = 2x faster, 2.0 = 2x slower)

    // Compton / photoelectric
    float    compton_radius            = 25.0f;   // interaction distance (px)
    int      max_compton_per_frame     = 8;

    // Spallation
    float    spallation_min_speed      = 120.0f;  // min projectile speed (px/frame)
    float    spallation_min_energy     = 0.5f;    // min projectile energy
    int      max_spallations_per_frame = 3;

    // Annihilation
    float    annihilation_radius       = 1.0f;    // contact distance (px)

    // Virtual particle pair creation
    bool     virtual_pairs_enabled     = true;
    float    virtual_pair_threshold    = 2.1f;   // min combined energy for pair creation
    uint32_t virtual_pair_max_per_tick = 2;      // max pairs spawned per frame

    // Quantum Entanglement
    bool     entanglement_enabled       = true;
    float    entanglement_coupling      = 0.15f;  // velocity coupling fraction (0=none, 1=full mirror)
    float    entanglement_decoherence   = 0.005f; // probability per tick of breaking (0=permanent)

    // Carrier Mode — visible force carrier exchange between particles
    bool     carrier_mode_enabled       = true;
    int      carrier_max_per_tick       = 5;
    float    carrier_exchange_radius    = 50.0f;

    // Quasi Mode — quasiparticle spawning and physics effects
    bool     quasi_mode_enabled         = true;

    // CP violation — neutral meson oscillation + asymmetric decays
    bool     cp_violation_enabled       = true;

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

    // Electron orbital parameters — per-shell speed multipliers (steep gradient)
    float    orbit_boost[4]           = {1.5f, 3.5f, 6.0f, 9.0f};  // 1s, 2s2p, 3s3p3d, 4s4p4d4f
    float    orbital_binding_radius   = 130.0f; // max electron-nucleus binding distance (px)
    float    orbital_shell_offset     = 2.399f; // golden angle offset between shells (radians)

    // Shell transition parameters
    bool     shell_transitions_enabled  = true;   // enable promotion/demotion/ionization
    float    deexcitation_lifetime      = 60.0f;  // frames before excited electron de-excites
    int      max_transitions_per_frame  = 8;      // cap transitions per tick

    // Visual
    bool     show_trails        = false;  // particle trail rendering (fade instead of clear)
    bool     bloom_enabled      = false;  // post-processing bloom glow

    // Camera shake (triggered by nuclear events)
    float    shake_intensity    = 0.0f;   // current magnitude (pixels)
    float    shake_decay        = 0.92f;  // per-frame exponential decay

    // Timestep multiplier (UI-adjustable)
    float    time_scale         = 1.0f;

    // Force objects (transient — set each frame before compute dispatch)
    uint32_t force_object_count = 0;

    glm::vec2 camera_origin          = { WORLD_W / 2.0f, WORLD_H / 2.0f };
    glm::vec2 current_camera_origin  = { WORLD_W / 2.0f, WORLD_H / 2.0f };
    float     camera_zoom        = 1.0f;
    float     current_camera_zoom = 1.0f;

    // ── Extended fusion parameters (v3+) ──────────────────────────────────
    float    fusion_min_energy         = 0.1f;    // min particle energy to participate
    float    fusion_pn_min_ke_keV      = 1.0f;    // min CM KE for p+n deuteron formation (keV)
    float    fusion_binding_mev        = 2.22f;   // deuteron binding energy Q-value (MeV)
    float    fusion_leptonic_q_mev     = 0.42f;   // p+p chain leptonic Q-value (MeV)
    float    fusion_product_separation = 3.0f;    // separation distance for bound pair (px)

    // ── Extended fission parameters (v3+) ─────────────────────────────────
    float    fission_cluster_radius    = 12.0f;   // nucleon clustering search radius (px)
    float    fission_barrier_mev       = 5.0f;    // min neutron KE to trigger fission (MeV)
    int      fission_min_protons       = 8;       // min protons for Coulomb instability
    float    fission_energy_per_nucleon = 1.0f;   // MeV released per nucleon in fragments
    int      fission_free_neutrons_min = 2;       // min chain-reaction neutrons spawned
    int      fission_free_neutrons_max = 3;       // max chain-reaction neutrons spawned
    float    fission_neutron_ke_mev    = 2.0f;    // KE of ejected chain-reaction neutrons (MeV)
    float    fission_spawn_radius      = 5.0f;    // spawn distance from cluster center (px)
    float    fission_fragment_energy_mev = 1.0f;  // energy boost per fragment (MeV)

    // ── Vacuum / Casimir parameters (v4+) ─────────────────────────────────
    float    casimir_radius       = 30.0f;   // detection range for Casimir force (px)
    float    casimir_strength     = 0.5f;    // Casimir attractive force scaling
    float    virtual_pair_scatter = 3.0f;    // spawn separation of particle pair (px)

    // ── Fission physics (v5+) ───────────────────────────────────────────
    float    fission_fissility_threshold = 35.0f;  // Bohr-Wheeler Z²/A threshold for fission
};

// ── Valence electrons from atomic number (periods 1-3) ───────────────────────
inline int valence_from_Z(int Z) {
    if (Z <= 0 || Z == 2 || Z == 10 || Z == 18) return 0; // noble gases + invalid
    if (Z == 1 || Z == 3 || Z == 11) return 1;   // H, Li, Na
    if (Z == 4 || Z == 12) return 2;               // Be, Mg
    if (Z == 5 || Z == 13) return 3;               // B, Al
    if (Z == 6 || Z == 14) return 4;               // C, Si
    if (Z == 7 || Z == 15) return 3;               // N, P
    if (Z == 8 || Z == 16) return 2;               // O, S
    if (Z == 9 || Z == 17) return 1;               // F, Cl
    return 0;
}

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
