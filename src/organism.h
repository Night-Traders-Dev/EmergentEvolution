#pragma once

#include "types.h"
#include "particles.h"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t ORGANISM_UPDATE_INTERVAL = 5;  // frames between updates
static constexpr uint32_t ORGANISM_MIN_SIZE         = 3;  // min particles to form an organism

// ── Traits ───────────────────────────────────────────────────────────────────

struct OrganismTraits {
    uint32_t size           = 0;
    float    avg_speed      = 0.0f;
    uint32_t type_counts[MAX_PARTICLE_TYPES] = {};
    uint32_t dominant_type  = 0;

    // Lineage
    uint32_t generation     = 0;   // how many division/absorption events in ancestry
    uint64_t parent_id      = 0;   // 0 = primordial
    uint32_t kills          = 0;   // organisms this lineage has consumed
    uint32_t divisions      = 0;   // times this lineage has divided
};

// ── Organism ──────────────────────────────────────────────────────────────────

struct Organism {
    uint64_t       id       = 0;
    OrganismTraits traits   = {};
    glm::vec2      centroid = {};
    float          spread   = 0.0f;  // RMS distance of particles from centroid
    std::vector<uint32_t> particle_indices;
};

// ── OrganismManager ───────────────────────────────────────────────────────────

class OrganismManager {
public:
    std::vector<Organism> organisms;    // current detected organisms
    float cluster_radius = 40.0f;       // proximity threshold for same-organism

    // Clear all state (call on simulation reset)
    void reset();

    // Detect organisms from current particle state and update traits.
    // Modifies particles.trait_scales to feed traits back into physics.
    void update(const std::vector<glm::vec2>& positions,
                const std::vector<glm::vec2>& velocities,
                const std::vector<uint32_t>&  types,
                Particles& particles);

private:
    std::vector<Organism> prev_organisms_;
    uint64_t              next_id_ = 1;

    // Cluster particles using spatial hash + union-find.
    // Returns parent[i] = root cluster index for particle i.
    std::vector<int> build_clusters(const std::vector<glm::vec2>& positions);

    // Write trait_scales into particles based on current organisms.
    void apply_trait_feedback(Particles& particles);
};
