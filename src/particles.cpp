#include "particles.h"
#include <random>
#include <cstring>
#include <algorithm>

Particles::Particles() {
    // Pre-allocate force / color arrays at max size
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
    gen_default_colors();
}

void Particles::gen_data(const SimConfig& cfg) {
    rng_.seed(cfg.generation_seed);

    if (cfg.reset_forces)
        gen_random_force_matrix();

    if (cfg.reset_colors)
        gen_default_colors();

    gen_particles(cfg);
}

void Particles::gen_particles(const SimConfig& cfg) {
    positions.clear();
    velocities.clear();
    types.clear();

    const float rw = static_cast<float>(REGION_W);
    const float rh = static_cast<float>(REGION_H);

    if (cfg.particle_count == 2) {
        add_particle(glm::vec2(rw / 2.0f - 30.0f, rh / 2.0f),
                     glm::vec2(0.0f),
                     rand_range_i(0, (int)cfg.particle_types - 1));
        add_particle(glm::vec2(rw / 2.0f + 30.0f, rh / 2.0f),
                     glm::vec2(0.0f),
                     rand_range_i(0, (int)cfg.particle_types - 1));
        return;
    }

    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        glm::vec2 pos(rand_range_f(0.0f, rw),
                      rand_range_f(0.0f, rh));
        uint32_t t = static_cast<uint32_t>(rand_range_i(0, (int)cfg.particle_types - 1));
        add_particle(pos, glm::vec2(0.0f), t);
    }
}

void Particles::add_particle(glm::vec2 pos, glm::vec2 vel, uint32_t type) {
    positions.push_back(pos);
    velocities.push_back(vel);
    types.push_back(type);
}

void Particles::gen_random_force_matrix() {
    forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES);
    for (auto& f : forces)
        f = rand_range_f(-1.0f, 1.0f);
}

void Particles::gen_empty_force_matrix() {
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
}

void Particles::gen_default_colors() {
    colors = {
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),  //  1 cyan
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),  //  2 red
        glm::vec4(0.0f, 1.0f, 0.0f, 1.0f),  //  3 green
        glm::vec4(1.0f, 0.0f, 1.0f, 1.0f),  //  4 magenta
        glm::vec4(1.0f, 1.0f, 0.0f, 1.0f),  //  5 yellow
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),  //  6 blue
        glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),  //  7 orange
        glm::vec4(0.5f, 0.0f, 1.0f, 1.0f),  //  8 violet
        glm::vec4(0.0f, 1.0f, 0.5f, 1.0f),  //  9 spring green
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),  // 10 white
    };
    colors.resize(MAX_PARTICLE_TYPES, glm::vec4(1.0f));
}

int Particles::rand_range_i(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng_);
}

float Particles::rand_range_f(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}
