#include "particles.h"
#include <random>
#include <cstring>
#include <algorithm>
#include <cmath>

Particles::Particles() {
    // Pre-allocate force / color arrays at max size
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
    gen_default_colors();
}

void Particles::gen_data(const SimConfig& cfg) {
    rng_.seed(cfg.generation_seed);
    for (float& s : trait_scales) s = 1.0f;
    for (auto& f : behavior_flags) f = BEHAVIOR_NONE;

    if (cfg.reset_forces)
        gen_random_force_matrix();

    if (cfg.reset_colors)
        gen_default_colors();

    // Always overlay the default ecosystem so births/evolution can begin immediately.
    // Users can override individual types via the preset UI afterwards.
    apply_default_ecosystem(cfg.particle_types);

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
        // Init orientation arrays for 2-particle case
        angles.assign(2, 0.0f);
        angular_velocities.assign(2, 0.0f);
        energies.assign(2, 1.0f);
        return;
    }

    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        glm::vec2 pos(rand_range_f(0.0f, rw),
                      rand_range_f(0.0f, rh));
        uint32_t t = static_cast<uint32_t>(rand_range_i(0, (int)cfg.particle_types - 1));
        add_particle(pos, glm::vec2(0.0f), t);
    }

    // Random initial orientations for all particles (used by POLAR types)
    angles.resize(cfg.particle_count);
    angular_velocities.assign(cfg.particle_count, 0.0f);
    for (uint32_t i = 0; i < cfg.particle_count; ++i)
        angles[i] = rand_range_f(0.0f, 6.28318f);

    energies.assign(cfg.particle_count, 1.0f);
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

// ── Archetype presets ─────────────────────────────────────────────────────────
// Each preset clears then sets behavior_flags for `type` and seeds the
// corresponding row of the force matrix.  The UI can still hand-edit forces.

static void set_row(std::vector<float>& forces, uint32_t type, float self_val, float cross_val) {
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        uint32_t fi = type + b * MAX_PARTICLE_TYPES;
        forces[fi]  = (b == type) ? self_val : cross_val;
    }
}

void Particles::apply_preset_default(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;
    // Leave force matrix as-is (user-controlled)
}

void Particles::apply_preset_repeller(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_REPEL;
    set_row(forces, type, -0.8f, -0.8f);
}

void Particles::apply_preset_polar(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_POLAR;
    // Strong self-attraction to form chains; random-ish cross forces
    set_row(forces, type, 0.4f, 0.0f);
    // Give slight positive force toward all active types to mix into the soup
    for (uint32_t b = 0; b < active_types; ++b) {
        if (b == type) continue;
        forces[type + b * MAX_PARTICLE_TYPES] = 0.15f;
    }
}

void Particles::apply_preset_heavy(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_HEAVY;
    set_row(forces, type, -0.2f, -0.2f);
}

void Particles::apply_preset_catalyst(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CATALYST;
    set_row(forces, type, 0.1f, 0.2f);
}

void Particles::apply_preset_membrane(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;  // pure force-matrix behaviour
    set_row(forces, type, 0.7f, -0.4f);
}

void Particles::apply_preset_viral(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_VIRAL;
    // Strong attraction toward all types to get close enough to infect
    set_row(forces, type, 0.6f, 0.6f);
    (void)active_types;  // unused, kept for API symmetry
}

// ── New archetype presets ─────────────────────────────────────────────────────

void Particles::apply_preset_adhesive(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_ADHESIVE;
    // Strong self-cohesion, mild attraction to others
    set_row(forces, type, 0.8f, 0.2f);
}

void Particles::apply_preset_secretor(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_SECRETOR;
    // Mild self-attraction, neutral cross; main effect is shader "halo" force
    set_row(forces, type, 0.2f, 0.0f);
}

void Particles::apply_preset_photosynth(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_PHOTOSYNTH;
    // Gentle cohesion; main effect is shader "light" drift in low density
    set_row(forces, type, 0.3f, 0.1f);
}

void Particles::apply_preset_predator(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_PREDATOR;
    // Repel self, attract others strongly
    set_row(forces, type, -0.2f, 0.6f);
    (void)active_types;
}

void Particles::apply_preset_reproductive(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_REPRODUCTIVE;
    // Neutral-ish forces; reproduction is signaled via velocity in the shader
    set_row(forces, type, 0.1f, 0.1f);
}

// ── Default ecosystem ─────────────────────────────────────────────────────────
// Assigns viable archetypes + force rows so the ecological loop fires immediately.
// Overwrites behavior flags and seeds the most critical force-matrix entries;
// the rest of the matrix (from gen_random_force_matrix) is left intact.
//
// Food web:
//   Type 0  Dust          — passive resource (energy recovers in shader)
//   Type 1  Photosynthesizer — primary producer; thrives in low-density space
//   Type 2  Colonizer     — adhesive colony builder; reproduces into dust
//   Type 3  Predator      — hunts types 1 & 2 for energy
//   Type 4  Catalyst      — boosts metabolism of nearby particles
//
void Particles::apply_default_ecosystem(uint32_t active_types) {
    if (active_types < 2) return;

    // Type 0: Dust — no special flags; shader gives it energy recovery already
    behavior_flags[0] = BEHAVIOR_NONE;

    // Type 1: Photosynthesizer
    if (active_types > 1) {
        behavior_flags[1] = BEHAVIOR_PHOTOSYNTH;
        set_row(forces, 1, 0.35f, 0.15f);
        forces[1 + 0 * MAX_PARTICLE_TYPES] = 0.5f;   // attracted to dust/food
        if (active_types > 3)
            forces[1 + 3 * MAX_PARTICLE_TYPES] = -0.5f; // flee predators
    }

    // Type 2: Colonizer — adhesive colony + reproduces into nearby recovered dust
    if (active_types > 2) {
        behavior_flags[2] = BEHAVIOR_ADHESIVE | BEHAVIOR_REPRODUCTIVE;
        set_row(forces, 2, 0.75f, 0.25f);
        forces[2 + 0 * MAX_PARTICLE_TYPES] = 0.65f;  // strongly toward dust (birth target)
        if (active_types > 1)
            forces[2 + 1 * MAX_PARTICLE_TYPES] = 0.45f; // symbiosis with photosynth
        if (active_types > 3)
            forces[2 + 3 * MAX_PARTICLE_TYPES] = -0.45f; // flee predators
    }

    // Type 3: Predator — hunts non-self types, avoids its own kind
    if (active_types > 3) {
        behavior_flags[3] = BEHAVIOR_PREDATOR;
        set_row(forces, 3, -0.35f, 0.65f);
        forces[3 + 0 * MAX_PARTICLE_TYPES] = 0.0f;   // ignore dust
        forces[3 + 3 * MAX_PARTICLE_TYPES] = -0.4f;  // territorial — avoid own kind
    }

    // Type 4: Catalyst — gentle all-around cohesion, boosts everyone's metabolism
    if (active_types > 4) {
        behavior_flags[4] = BEHAVIOR_CATALYST;
        set_row(forces, 4, 0.2f, 0.2f);
    }
}
