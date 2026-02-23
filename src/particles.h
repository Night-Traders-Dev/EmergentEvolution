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

    float trait_scales[MAX_PARTICLE_TYPES];
    float structure_integrity[MAX_PARTICLE_TYPES]; // Added this
    uint32_t behavior_flags[MAX_PARTICLE_TYPES];

    std::vector<float> angles;
    std::vector<float> angular_velocities;

    Particles();
    void gen_data(const SimConfig& cfg);

    // Archetype presets: set behavior_flags AND seed the force-matrix row for `type`.
    // Safe to call at any time; changes are picked up by upload_dynamic_data next frame.
    void apply_preset_default(uint32_t type);
    void apply_preset_repeller(uint32_t type);
    void apply_preset_polar(uint32_t type, uint32_t active_types);
    void apply_preset_heavy(uint32_t type);
    void apply_preset_catalyst(uint32_t type);
    void apply_preset_membrane(uint32_t type);
    void apply_preset_viral(uint32_t type, uint32_t active_types);

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
    int rand_range_i(int lo, int hi);
    // uniform float in [lo, hi)
    float rand_range_f(float lo, float hi);
};
