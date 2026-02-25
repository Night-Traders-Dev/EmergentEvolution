#pragma once

#include "types.h"
#include "particles.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct SaveResult {
    bool success = false;
    std::string message;
};

// Save full simulation state to binary .ppsg file
SaveResult save_simulation(
    const std::string& filepath,
    const SimConfig& cfg,
    const Particles& particles,
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<float>& energies,
    const ForceObject* force_objects,
    uint32_t force_object_count,
    // UI state
    bool field_em, bool field_strong, bool field_weak,
    bool field_gravity, bool field_higgs,
    float field_intensity, float log_temperature
);

// Load simulation state from binary .ppsg file
struct LoadResult {
    bool success = false;
    std::string message;
    SimConfig cfg;
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;
    std::vector<float> energies;
    std::vector<uint32_t> types;
    std::vector<float> angles;
    std::vector<float> angular_velocities;
    std::vector<float> genomes;
    float forces[MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES];
    glm::vec4 colors[MAX_PARTICLE_TYPES];
    uint32_t behavior_flags[MAX_PARTICLE_TYPES];
    ForceObject force_objects[MAX_FORCE_OBJECTS];
    uint32_t force_object_count;
    // UI state
    bool field_em, field_strong, field_weak, field_gravity, field_higgs;
    float field_intensity, log_temperature;
};

LoadResult load_simulation(const std::string& filepath);
