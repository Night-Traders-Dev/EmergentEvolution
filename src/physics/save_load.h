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
    std::vector<float> forces;           // MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES
    std::vector<glm::vec4> colors;       // MAX_PARTICLE_TYPES
    std::vector<uint32_t> behavior_flags;// MAX_PARTICLE_TYPES
    std::vector<ForceObject> force_objects; // MAX_FORCE_OBJECTS
    uint32_t force_object_count = 0;
    // UI state
    bool field_em, field_strong, field_weak, field_gravity, field_higgs;
    float field_intensity, log_temperature;
};

LoadResult load_simulation(const std::string& filepath);

// ── Element export/import (.ppel files) ──────────────────────────────────────

struct ElementExportData {
    float dx, dy;               // position offset from nucleus center
    float vx, vy;               // velocity
    float energy;
    uint32_t type;              // particle type index
    float genome[GENOME_SIZE];  // per-particle genome
};

SaveResult export_element(
    const std::string& filepath,
    int Z, int N, int electrons,
    const std::vector<ElementExportData>& particles
);

struct ImportElementResult {
    bool success = false;
    std::string message;
    int Z = 0, N = 0, electrons = 0;
    std::vector<ElementExportData> particles;
};

ImportElementResult import_element(const std::string& filepath);

// ── Molecule export/import (.ppmol files) ──────────────────────────────────

struct MoleculeAtomData {
    int32_t Z, N, electrons;
    float cx_offset, cy_offset;                 // center offset from molecule COM
    std::vector<ElementExportData> particles;   // nucleons + electrons
};

struct MoleculeBondData {
    int32_t atom_a, atom_b;                     // indices into atom array
};

SaveResult export_molecule(
    const std::string& filepath,
    const std::string& formula,
    const std::vector<MoleculeAtomData>& atoms,
    const std::vector<MoleculeBondData>& bonds
);

struct ImportMoleculeResult {
    bool success = false;
    std::string message;
    std::string formula;
    std::vector<MoleculeAtomData> atoms;
    std::vector<MoleculeBondData> bonds;
};

ImportMoleculeResult import_molecule(const std::string& filepath);
