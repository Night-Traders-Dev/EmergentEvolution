#pragma once
// ── Biochemical Simulator — Data Types ──────────────────────────────────────

#include "common/orbit_camera.h"
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

enum BioCellMorphology : uint32_t {
    BIO_CELL_ANIMAL     = 0,
    BIO_CELL_EPITHELIAL = 1,
    BIO_CELL_AMOEBOID   = 2,
    BIO_CELL_VARIANT_COUNT
};

enum BioBacteriaMorphology : uint32_t {
    BIO_BACTERIA_COCCI   = 0,
    BIO_BACTERIA_BACILLI = 1,
    BIO_BACTERIA_SPIRAL  = 2,
    BIO_BACTERIA_VARIANT_COUNT
};

enum BioVirusMorphology : uint32_t {
    BIO_VIRUS_CLASSICAL = 0,
    BIO_VIRUS_CORONA    = 1,
    BIO_VIRUS_PHAGE     = 2,
    BIO_VIRUS_VARIANT_COUNT
};

enum BioEnvironmentType : uint32_t {
    BIO_ENV_HUMAN_LUNG = 0,
    BIO_ENV_POND_WATER = 1,
    BIO_ENV_PETRI_DISH = 2,
    BIO_ENV_CAT_BRAIN  = 3,
    BIO_ENV_COUNT
};

struct BioEnvironmentPreset {
    const char* name;
    const char* summary;
    glm::vec3   tint;
    glm::vec3   flow_axis;
    float       temperature_c;
    float       acidity_ph;
    float       oxygen_level;
    float       nutrient_density;
    float       flow_strength;
    float       toxicity;
    float       immune_pressure;
    float       fluid_damping;
    bool        immune_system;
};

enum BioEnvironmentFeatureType : uint32_t {
    BIO_ENV_FEATURE_MEMBRANE = 0,
    BIO_ENV_FEATURE_NUTRIENT = 1,
    BIO_ENV_FEATURE_TOXIN    = 2,
    BIO_ENV_FEATURE_CURRENT  = 3,
    BIO_ENV_FEATURE_COUNT
};

inline const BioEnvironmentPreset& bio_environment_preset(BioEnvironmentType env) {
    static const BioEnvironmentPreset presets[BIO_ENV_COUNT] = {
        {
            "Human Lung",
            "Warm, oxygen-rich tissue with active immune surveillance and rhythmic airflow.",
            {0.16f, 0.32f, 0.28f},
            {0.9f, 0.15f, 0.25f},
            37.0f,
            7.25f,
            0.98f,
            0.95f,
            24.0f,
            0.04f,
            1.10f,
            0.965f,
            true,
        },
        {
            "Pond Water",
            "Cool, nutrient-rich water with suspended toxins, weak immunity, and slow currents.",
            {0.10f, 0.24f, 0.18f},
            {0.45f, 0.05f, 0.9f},
            18.0f,
            6.70f,
            0.58f,
            1.45f,
            12.0f,
            0.16f,
            0.18f,
            0.985f,
            false,
        },
        {
            "Petri Dish",
            "Engineered culture media with high nutrient availability, low flow, and weak immunity.",
            {0.20f, 0.22f, 0.14f},
            {0.2f, 0.0f, 1.0f},
            30.0f,
            7.05f,
            0.76f,
            1.65f,
            4.0f,
            0.02f,
            0.08f,
            0.975f,
            false,
        },
        {
            "Cat Brain",
            "Warm, protected neural tissue with high metabolic demand, moderate perfusion, and selective immunity.",
            {0.18f, 0.20f, 0.30f},
            {0.25f, 0.8f, 0.35f},
            38.2f,
            7.32f,
            0.88f,
            1.08f,
            16.0f,
            0.03f,
            0.70f,
            0.972f,
            true,
        },
    };

    uint32_t idx = static_cast<uint32_t>(env);
    if (idx >= BIO_ENV_COUNT)
        idx = BIO_ENV_HUMAN_LUNG;
    return presets[idx];
}

inline const char* bio_entity_variant_name(uint32_t type, uint32_t morphology) {
    switch (type) {
    case BIO_CELL: {
        static const char* names[BIO_CELL_VARIANT_COUNT] = {
            "Animal Cell", "Epithelial Cell", "Amoeboid Cell"
        };
        return names[morphology % BIO_CELL_VARIANT_COUNT];
    }
    case BIO_BACTERIUM: {
        static const char* names[BIO_BACTERIA_VARIANT_COUNT] = {
            "Cocci", "Bacilli", "Spiral"
        };
        return names[morphology % BIO_BACTERIA_VARIANT_COUNT];
    }
    case BIO_VIRUS: {
        static const char* names[BIO_VIRUS_VARIANT_COUNT] = {
            "Classical Capsid", "Coronavirus", "Bacteriophage"
        };
        return names[morphology % BIO_VIRUS_VARIANT_COUNT];
    }
    default:
        return "Default";
    }
}

// ── Single biological entity ────────────────────────────────────────────────

struct BioGenes {
    float seek     = 1.0f;
    float flee     = 1.0f;
    float spacing  = 1.0f;
    float brownian = 1.0f;
    float energy   = 1.0f;
};

struct BioEntity {
    glm::vec3   pos{0.0f};
    glm::vec3   vel{0.0f};
    glm::vec3   axis{0.0f, 1.0f, 0.0f};
    float       radius    = 8.0f;
    float       energy    = 100.0f;     // health / metabolic energy
    float       age       = 0.0f;       // seconds alive
    float       shape_aspect = 1.0f;
    float       shape_noise  = 0.2f;
    float       shape_phase  = 0.0f;
    BioGenes     genes{};
    uint32_t    type      = BIO_CELL;
    uint32_t    morphology = 0;
    uint32_t    genome    = 0;          // simple genome tag for mutations
    bool        alive     = true;
};

// ── Simulation config ───────────────────────────────────────────────────────

struct BiochemConfig {
    uint32_t environment      = BIO_ENV_HUMAN_LUNG;
    uint32_t environment_seed = 1337u;
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

    // 3D world bounds (entities wrap within this volume)
    float    world_radius     = 200.0f;

    // Lighting
    float    ambient_strength = 0.12f;

    // AI movement
    bool     ai_movement       = true;
    float    seek_strength      = 40.0f;
    float    flee_strength      = 60.0f;
    float    spacing_strength   = 20.0f;
    float    brownian_strength  = 15.0f;

    // Environment
    float    temperature_c      = 37.0f;
    float    acidity_ph         = 7.25f;
    float    oxygen_level       = 0.98f;
    float    nutrient_density   = 0.95f;
    float    flow_strength      = 24.0f;
    float    toxicity           = 0.04f;
    float    immune_pressure    = 1.10f;
    float    fluid_damping      = 0.965f;
    glm::vec3 environment_tint  = {0.16f, 0.32f, 0.28f};
    glm::vec3 flow_axis         = {0.9f, 0.15f, 0.25f};
};

struct BioEnvironmentFeature {
    glm::vec3 pos{0.0f};
    float     radius = 20.0f;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float     strength = 1.0f;
    glm::vec3 tint{0.2f, 0.35f, 0.3f};
    uint32_t  type = BIO_ENV_FEATURE_MEMBRANE;
    float     falloff = 0.5f;
    float     noise = 0.0f;
};

struct BiochemEnvironment {
    uint32_t seed = 1337u;
    std::vector<BioEnvironmentFeature> features;

    void clear() { features.clear(); }
    size_t count() const { return features.size(); }
    size_t count_type(BioEnvironmentFeatureType t) const {
        size_t n = 0;
        for (const auto& feature : features)
            if (feature.type == t) n++;
        return n;
    }
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
