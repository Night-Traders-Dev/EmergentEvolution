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
    // CPU arrays – uploaded to GPU after gen_data()
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;
    std::vector<uint32_t>  types;
    std::vector<float>     forces;   // MAX_PARTICLE_TYPES² elements
    std::vector<glm::vec4> colors;   // MAX_PARTICLE_TYPES elements

    Particles();

    // Called once at startup and on every reset (F2).
    void gen_data(const SimConfig& cfg);

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
