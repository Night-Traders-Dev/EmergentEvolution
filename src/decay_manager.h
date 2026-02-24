#pragma once

#include "types.h"
#include <vector>
#include <random>
#include <glm/glm.hpp>

// ── Decay event ────────────────────────────────────────────────────────────────
// Returned by DecayManager::update(); processed by Simulation::tick().

struct DecayEvent {
    uint32_t  parent_idx;     // index of the particle being transformed
    uint32_t  daughter_type;  // type it becomes after decay (e.g. U → Pb)
    uint32_t  product_type;   // emitted particle type (alpha, e-, e+, ν, muon)
                              // 0xFFFFFFFFu = no product particle
    glm::vec2 parent_pos;     // world position at time of decay
    glm::vec2 product_vel;    // initial velocity for the emitted product
    float     q_energy;       // Q-value: kinetic energy released (0–1 normalised)
    bool      emits_gamma;    // whether a gamma photon is also emitted
};

// ── Per-element decay parameters ───────────────────────────────────────────────

struct DecayParams {
    bool     unstable      = false;
    float    half_life_sec = 9999.f;  // in simulation-seconds (scaled for visibility)
    uint32_t daughter_type = 0;
    uint32_t product_type  = 0xFFFFFFFFu;
    float    q_energy      = 0.3f;
    bool     emits_gamma   = true;
};

// ── DecayManager ───────────────────────────────────────────────────────────────

class DecayManager {
public:
    void reset();

    // Probabilistic decay + annihilation check. Call once per bond-update cadence
    // from Simulation::tick() after the CPU readback. Returns events to process.
    std::vector<DecayEvent> update(
        const std::vector<uint32_t>&  types,
        const std::vector<glm::vec2>& positions,
        const std::vector<float>&     energies,
        float dt,
        uint32_t count);

    uint32_t total_decays = 0;

    // Indexed by particle type 0–(MAX_PARTICLE_TYPES-1).
    // Stable elements have unstable==false (default-constructed).
    static const DecayParams DECAY_TABLE[MAX_PARTICLE_TYPES];

private:
    std::mt19937                          rng_{ std::random_device{}() };
    std::uniform_real_distribution<float> uniform_{ 0.f, 1.f };

    // TTL timers for muon-type particles (indexed by particle index)
    std::vector<float> muon_ttl_;

    glm::vec2 random_dir();
};
