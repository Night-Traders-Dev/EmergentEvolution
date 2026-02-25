#pragma once

#include "types.h"
#include <cstdint>
#include <vector>
#include <random>
#include <glm/glm.hpp>

// Mirrors particles.gd – owns all CPU-side particle arrays and
// generates/regenerates them according to SimConfig.

class Particles {
public:
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;
    std::vector<uint32_t>  types;
    std::vector<float>     forces;
    std::vector<glm::vec4> colors;

    float    trait_scales[MAX_PARTICLE_TYPES];
    float    structure_integrity[MAX_PARTICLE_TYPES];
    uint32_t behavior_flags[MAX_PARTICLE_TYPES];

    std::vector<float> angles;
    std::vector<float> angular_velocities;
    std::vector<float> energies;   // per-particle, 0.0–1.0
    std::vector<float> genomes;    // GENOME_SIZE floats per particle: charge, electronegativity, reactivity, bond_strength

    std::vector<uint32_t> birth_frames;   // frame at which each particle was created
    std::vector<int32_t>  orbital_parent;  // index of parent nucleus proton, or -1 if unbound

    // Bond data: pointer into BondManager.bond_partners — set by Simulation after BondManager::update()
    const uint32_t* bond_partners_ptr   = nullptr;
    uint32_t        bond_partners_count = 0;

    Particles();
    void gen_data(const SimConfig& cfg);

    // Sets CPK colors, chemistry behavior flags, and electrochemistry force matrix
    // for all active atom types (H C N O P S Na Cl).
    // Called automatically from gen_data(); can also be called from the UI.
    void apply_atom_defaults(uint32_t active_types, float force_randomness = 0.0f);

    // Sets photon color and behavior flag for type index PHOTON_TYPE (8).
    // Called at the end of apply_atom_defaults() and before photon injection.
    void setup_photon_type();

    // Sets colors, behavior flags, and zero force rows for Standard Model particle
    // types 19-23 (alpha, e-, e+, neutrino, muon).
    // Called at the end of apply_atom_defaults().
    void setup_sm_types();

    // Individual atom preset: set flags + seed force row for one type index.
    void apply_preset_atom(uint32_t type, uint32_t active_types);

    // Legacy presets kept for UI compatibility (map to nearest atom behavior)
    void apply_preset_default(uint32_t type);
    void apply_preset_repeller(uint32_t type);
    void apply_preset_polar(uint32_t type, uint32_t active_types);
    void apply_preset_heavy(uint32_t type);
    void apply_preset_catalyst(uint32_t type);
    void apply_preset_adhesive(uint32_t type);
    void apply_preset_radical(uint32_t type);
    void apply_preset_donor(uint32_t type);
    void apply_preset_acceptor(uint32_t type);

private:
    std::mt19937 rng_;

    void gen_particles(const SimConfig& cfg);
    void gen_random_force_matrix();
    void gen_empty_force_matrix();
    void gen_default_colors();

    void add_particle(glm::vec2 pos,
                      glm::vec2 vel  = glm::vec2(0.0f),
                      uint32_t  type = 0);

    // uniform int in [lo, hi]
    int   rand_range_i(int lo, int hi);
    // uniform float in [lo, hi)
    float rand_range_f(float lo, float hi);
};
