#pragma once
// ── Biochemical Simulator — Data Types ──────────────────────────────────────

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// ── Entity types ────────────────────────────────────────────────────────────

enum BioEntityType : uint32_t {
    BIO_CELL        = 0,    // generic eukaryotic cell
    BIO_BACTERIUM   = 1,    // prokaryote
    BIO_VIRUS       = 2,    // viral particle
    BIO_NUTRIENT    = 3,    // food / glucose molecule
    BIO_TOXIN       = 4,    // harmful chemical
    BIO_ANTIBODY    = 5,    // immune system agent
    BIO_RED_BLOOD   = 6,    // red blood cell
    BIO_WHITE_BLOOD = 7,    // white blood cell (immune)
    BIO_TYPE_COUNT
};

// ── Single biological entity ────────────────────────────────────────────────

struct BioEntity {
    glm::vec2   pos{0.0f};
    glm::vec2   vel{0.0f};
    float       radius    = 8.0f;
    float       energy    = 100.0f;     // health / metabolic energy
    float       age       = 0.0f;       // seconds alive
    uint32_t    type      = BIO_CELL;
    uint32_t    genome    = 0;          // simple genome tag for mutations
    bool        alive     = true;
};

// ── Simulation config ───────────────────────────────────────────────────────

struct BiochemConfig {
    uint32_t entity_count     = 200;
    float    nutrient_rate    = 2.0f;     // nutrients spawned per second
    float    metabolism_rate  = 1.0f;     // energy consumption rate
    float    division_energy  = 150.0f;   // energy threshold for cell division
    float    mutation_rate    = 0.01f;    // per-division mutation probability
    float    infection_radius = 20.0f;    // virus infection range
    float    infection_rate   = 0.5f;     // infection probability per contact
    float    immune_strength  = 1.0f;     // antibody effectiveness
    float    viscosity        = 0.98f;    // fluid damping
    float    dt_scale         = 1.0f;
    bool     immune_system    = true;
    bool     show_energy_bars = true;
};

// ── Entity collection ───────────────────────────────────────────────────────

struct BiochemState {
    std::vector<BioEntity> entities;

    void clear() { entities.clear(); }
    size_t count() const { return entities.size(); }
    size_t count_alive() const {
        size_t n = 0;
        for (const auto& e : entities)
            if (e.alive) n++;
        return n;
    }
    size_t count_type(BioEntityType t) const {
        size_t n = 0;
        for (const auto& e : entities)
            if (e.alive && e.type == t) n++;
        return n;
    }
};
