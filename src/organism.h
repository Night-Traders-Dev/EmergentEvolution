#pragma once

#include "types.h"
#include "particles.h"
#include "bond_manager.h"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

static constexpr uint32_t ORGANISM_UPDATE_INTERVAL = 5;
static constexpr uint32_t ORGANISM_MIN_SIZE        = 3;

// Molecular aggregate class inferred from atom composition
enum class MoleculeClass : uint8_t {
    INORGANIC  = 0,
    WATER      = 1,
    LIPID      = 2,
    AMINO_ACID = 3,
    NUCLEOTIDE = 4,
    RADICAL    = 5,
    POLYMER    = 6,
};

struct OrganismTraits {
    uint32_t size          = 0;
    float    avg_speed     = 0.0f;
    uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
    uint32_t dominant_type = 0;

    // Organism-level energy (averaged across members from GPU readback)
    float energy = 1.0f;

    // Averaged genome traits: [0]=charge [1]=electroneg [2]=reactivity [3]=bond_str
    float avg_charge        = 0.0f;
    float avg_electroneg    = 1.0f;
    float avg_reactivity    = 1.0f;
    float avg_bond_strength = 0.0f;

    MoleculeClass mol_class  = MoleculeClass::INORGANIC;
    uint32_t      bond_count = 0;  // total bonds among member particles

    // Lineage
    uint32_t generation = 0;
    uint64_t parent_id  = 0;
    uint32_t kills      = 0;
    uint32_t divisions  = 0;
};

struct Organism {
    uint64_t id = 0;
    OrganismTraits traits = {};
    glm::vec2 centroid = {};
    float spread = 0.0f;
    std::vector<uint32_t> particle_indices;
};

static constexpr uint32_t POP_HISTORY_LEN = 300;

class OrganismManager {
public:
    std::vector<Organism> organisms;
    float cluster_radius = 40.0f;

    // DBSCAN parameters
    uint32_t min_pts   = 4;     // minimum neighbors to be a core point
    float    eps_scale = 1.0f;  // eps = eps_scale * cluster_radius

    // Population statistics (updated each organism tick)
    uint32_t last_births  = 0;
    uint32_t last_deaths  = 0;
    uint32_t dust_count   = 0;
    uint32_t alive_count  = 0;

    // Per-type particle counts (updated each organism tick)
    uint32_t type_populations[MAX_PARTICLE_TYPES] = {};

    // Rolling population history for plotting (ring buffer)
    float    pop_history[POP_HISTORY_LEN] = {};
    uint32_t pop_history_idx   = 0;
    uint32_t pop_history_count = 0;

    void reset();

    void update(const std::vector<glm::vec2>& positions,
                const std::vector<glm::vec2>& velocities,
                const std::vector<float>&     energies,
                const std::vector<uint32_t>&  types,
                Particles& particles,
                BondManager& bond_manager);

private:
    std::vector<Organism> prev_organisms_;
    uint64_t next_id_ = 1;

    std::vector<int> build_clusters(const std::vector<glm::vec2>& positions);

    void apply_trait_feedback(Particles& particles);
};
