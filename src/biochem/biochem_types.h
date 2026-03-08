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
    BIO_JANITOR     = 8,    // phagocytic cleanup cell
    BIO_TYPE_COUNT
};

enum BioCellMorphology : uint32_t {
    BIO_CELL_GENERIC_ANIMAL      = 0,
    BIO_CELL_TYPE_II_PNEUMOCYTE  = 1,
    BIO_CELL_CILIATED_EPITHELIAL = 2,
    BIO_CELL_ENTEROCYTE          = 3,
    BIO_CELL_NEURON              = 4,
    BIO_CELL_ASTROCYTE           = 5,
    BIO_CELL_FIBROBLAST          = 6,
    BIO_CELL_AMOEBOID            = 7,
    BIO_CELL_VARIANT_COUNT
};

enum BioBacteriaMorphology : uint32_t {
    BIO_BACTERIA_STAPHYLOCOCCUS_AUREUS   = 0,
    BIO_BACTERIA_STREPTOCOCCUS_PNEUMONIAE = 1,
    BIO_BACTERIA_ESCHERICHIA_COLI        = 2,
    BIO_BACTERIA_PSEUDOMONAS_AERUGINOSA  = 3,
    BIO_BACTERIA_BACILLUS_SUBTILIS       = 4,
    BIO_BACTERIA_VIBRIO_CHOLERAE         = 5,
    BIO_BACTERIA_VARIANT_COUNT
};

enum BioVirusMorphology : uint32_t {
    BIO_VIRUS_ADENOVIRUS_C5        = 0,
    BIO_VIRUS_SARS_COV_2           = 1,
    BIO_VIRUS_INFLUENZA_A_H1N1     = 2,
    BIO_VIRUS_INFLUENZA_A_H3N2     = 3,
    BIO_VIRUS_BACTERIOPHAGE_T4     = 4,
    BIO_VIRUS_VARIANT_COUNT
};

enum BioCellVisualFamily : uint32_t {
    BIO_CELL_FAMILY_ANIMAL = 0,
    BIO_CELL_FAMILY_EPITHELIAL = 1,
    BIO_CELL_FAMILY_AMOEBOID = 2
};

enum BioBacteriaVisualFamily : uint32_t {
    BIO_BACTERIA_FAMILY_COCCI = 0,
    BIO_BACTERIA_FAMILY_BACILLI = 1,
    BIO_BACTERIA_FAMILY_SPIRAL = 2
};

enum BioVirusVisualFamily : uint32_t {
    BIO_VIRUS_FAMILY_CAPSID = 0,
    BIO_VIRUS_FAMILY_CORONA = 1,
    BIO_VIRUS_FAMILY_PHAGE = 2,
    BIO_VIRUS_FAMILY_INFLUENZA = 3
};

enum BioEnvironmentType : uint32_t {
    BIO_ENV_HUMAN_LUNG = 0,
    BIO_ENV_POND_WATER = 1,
    BIO_ENV_PETRI_DISH = 2,
    BIO_ENV_CAT_BRAIN  = 3,
    BIO_ENV_GUT        = 4,
    BIO_ENV_BLOOD      = 5,
    BIO_ENV_SOIL       = 6,
    BIO_ENV_WOUND      = 7,
    BIO_ENV_COUNT
};

enum BioAutoSpawnMode : uint32_t {
    BIO_AUTOSPAWN_STATIC = 0,
    BIO_AUTOSPAWN_DYNAMIC = 1,
    BIO_AUTOSPAWN_MODE_COUNT
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
    BIO_ENV_FEATURE_STRUCTURE = 4,
    BIO_ENV_FEATURE_COUNT
};

enum BioEnvironmentStructureShape : uint32_t {
    BIO_ENV_STRUCTURE_LUNG_BRANCH = 0,
    BIO_ENV_STRUCTURE_ALVEOLAR_CLUSTER = 1,
    BIO_ENV_STRUCTURE_POND_REED = 2,
    BIO_ENV_STRUCTURE_POND_ROCK = 3,
    BIO_ENV_STRUCTURE_PETRI_RIM = 4,
    BIO_ENV_STRUCTURE_PETRI_AGAR = 5,
    BIO_ENV_STRUCTURE_BRAIN_FOLD = 6,
    BIO_ENV_STRUCTURE_BRAIN_VESSEL = 7,
    BIO_ENV_STRUCTURE_GUT_VILLUS = 8,
    BIO_ENV_STRUCTURE_GUT_CRYPT = 9,
    BIO_ENV_STRUCTURE_BLOOD_WALL = 10,
    BIO_ENV_STRUCTURE_BLOOD_VALVE = 11,
    BIO_ENV_STRUCTURE_SOIL_GRAIN = 12,
    BIO_ENV_STRUCTURE_SOIL_ROOT = 13,
    BIO_ENV_STRUCTURE_WOUND_FIBRIN = 14,
    BIO_ENV_STRUCTURE_WOUND_TISSUE = 15,
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
        {
            "Gut Microbiome",
            "Warm, anaerobic intestinal lumen with dense microbial communities, villi, and active immune surveillance.",
            {0.22f, 0.16f, 0.12f},
            {0.6f, 0.1f, 0.8f},
            37.0f,
            6.80f,
            0.08f,
            1.85f,
            18.0f,
            0.22f,
            0.95f,
            0.970f,
            true,
        },
        {
            "Blood Stream",
            "Fast-flowing arterial blood with high oxygen, tight pH buffering, and intense immune surveillance.",
            {0.28f, 0.08f, 0.08f},
            {1.0f, 0.0f, 0.0f},
            37.0f,
            7.40f,
            0.95f,
            1.20f,
            65.0f,
            0.01f,
            1.80f,
            0.955f,
            true,
        },
        {
            "Soil Rhizosphere",
            "Cool, variable-oxygen ground with organic matter, root exudates, and diverse microbial competition.",
            {0.14f, 0.12f, 0.08f},
            {0.1f, 0.9f, 0.2f},
            18.0f,
            6.20f,
            0.32f,
            1.10f,
            3.0f,
            0.18f,
            0.05f,
            0.988f,
            false,
        },
        {
            "Wound Site",
            "Inflamed, hypoxic tissue with fibrin deposits, high immune infiltration, and bacterial colonization.",
            {0.24f, 0.10f, 0.10f},
            {0.3f, 0.7f, 0.4f},
            37.5f,
            6.40f,
            0.25f,
            1.40f,
            8.0f,
            0.35f,
            2.50f,
            0.968f,
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
            "Generic Animal Cell",
            "Type II Pneumocyte",
            "Ciliated Epithelial Cell",
            "Enterocyte",
            "Neuron",
            "Astrocyte",
            "Fibroblast",
            "Amoeboid Cell"
        };
        return names[morphology % BIO_CELL_VARIANT_COUNT];
    }
    case BIO_BACTERIUM: {
        static const char* names[BIO_BACTERIA_VARIANT_COUNT] = {
            "Staphylococcus aureus",
            "Streptococcus pneumoniae",
            "Escherichia coli",
            "Pseudomonas aeruginosa",
            "Bacillus subtilis",
            "Vibrio cholerae"
        };
        return names[morphology % BIO_BACTERIA_VARIANT_COUNT];
    }
    case BIO_VIRUS: {
        static const char* names[BIO_VIRUS_VARIANT_COUNT] = {
            "Human adenovirus C5",
            "SARS-CoV-2",
            "Influenza A virus subtype H1N1 (swine flu)",
            "Influenza A virus subtype H3N2",
            "Enterobacteria phage T4"
        };
        return names[morphology % BIO_VIRUS_VARIANT_COUNT];
    }
    default:
        return "Default";
    }
}

inline uint32_t bio_cell_visual_family(uint32_t morphology) {
    switch (morphology % BIO_CELL_VARIANT_COUNT) {
    case BIO_CELL_TYPE_II_PNEUMOCYTE:
    case BIO_CELL_CILIATED_EPITHELIAL:
    case BIO_CELL_ENTEROCYTE:
        return BIO_CELL_FAMILY_EPITHELIAL;
    case BIO_CELL_ASTROCYTE:
    case BIO_CELL_AMOEBOID:
        return BIO_CELL_FAMILY_AMOEBOID;
    case BIO_CELL_GENERIC_ANIMAL:
    case BIO_CELL_NEURON:
    case BIO_CELL_FIBROBLAST:
    default:
        return BIO_CELL_FAMILY_ANIMAL;
    }
}

inline uint32_t bio_bacteria_visual_family(uint32_t morphology) {
    switch (morphology % BIO_BACTERIA_VARIANT_COUNT) {
    case BIO_BACTERIA_STAPHYLOCOCCUS_AUREUS:
    case BIO_BACTERIA_STREPTOCOCCUS_PNEUMONIAE:
        return BIO_BACTERIA_FAMILY_COCCI;
    case BIO_BACTERIA_VIBRIO_CHOLERAE:
        return BIO_BACTERIA_FAMILY_SPIRAL;
    case BIO_BACTERIA_ESCHERICHIA_COLI:
    case BIO_BACTERIA_PSEUDOMONAS_AERUGINOSA:
    case BIO_BACTERIA_BACILLUS_SUBTILIS:
    default:
        return BIO_BACTERIA_FAMILY_BACILLI;
    }
}

inline uint32_t bio_virus_visual_family(uint32_t morphology) {
    switch (morphology % BIO_VIRUS_VARIANT_COUNT) {
    case BIO_VIRUS_SARS_COV_2:
        return BIO_VIRUS_FAMILY_CORONA;
    case BIO_VIRUS_BACTERIOPHAGE_T4:
        return BIO_VIRUS_FAMILY_PHAGE;
    case BIO_VIRUS_INFLUENZA_A_H1N1:
    case BIO_VIRUS_INFLUENZA_A_H3N2:
        return BIO_VIRUS_FAMILY_INFLUENZA;
    case BIO_VIRUS_ADENOVIRUS_C5:
    default:
        return BIO_VIRUS_FAMILY_CAPSID;
    }
}

inline const char* bio_mitosis_stage_name(float progress) {
    if (progress <= 0.02f) return "Interphase";
    if (progress < 0.28f) return "Prophase";
    if (progress < 0.55f) return "Metaphase";
    if (progress < 0.82f) return "Anaphase";
    if (progress < 0.995f) return "Telophase";
    return "Cytokinesis";
}

inline const char* bio_binary_fission_stage_name(float progress) {
    if (progress <= 0.02f) return "Resting";
    if (progress < 0.28f) return "DNA Replication";
    if (progress < 0.58f) return "Elongation";
    if (progress < 0.84f) return "Septation";
    if (progress < 0.995f) return "Constriction";
    return "Separation";
}

// ── Single biological entity ────────────────────────────────────────────────

struct BioGenes {
    float seek     = 1.0f;
    float flee     = 1.0f;
    float spacing  = 1.0f;
    float brownian = 1.0f;
    float energy   = 1.0f;
    float telomere = 1.0f;
    float mitotic_clock = 1.0f;
    float metabolism_efficiency = 1.0f;
    float nutrient_affinity = 1.0f;
    float stress_tolerance = 1.0f;
    float defense = 1.0f;
    float sensing = 1.0f;
    float mutation_stability = 1.0f;
    float antibiotic_type = 0.0f;
    float antibiotic_yield = 0.0f;
    float antibiotic_diversity = 0.0f;
    float resistance = 0.0f;           // antibiotic resistance (bacteria, 0-2.5)
    float quorum_threshold = 0.5f;     // quorum sensing threshold (bacteria, 0-1)
};

inline constexpr int BIO_GENE_TRAIT_COUNT = 18;

// Immune cell subtypes for WBC
inline constexpr uint32_t BIO_IMMUNE_GENERIC    = 0; // generic WBC (neutrophil-like)
inline constexpr uint32_t BIO_IMMUNE_T_CELL     = 1; // cytotoxic T cell — kills infected cells
inline constexpr uint32_t BIO_IMMUNE_B_CELL     = 2; // B cell — produces antibodies

struct BioPathogenState {
    glm::vec3   axis{1.0f, 0.0f, 0.0f};
    float       progress = 0.0f;
    float       load = 0.0f;
    BioGenes    genes{};
    uint32_t    morphology = 0;
    uint32_t    source_id = 0;
    uint32_t    source_type = BIO_TYPE_COUNT;
    uint32_t    species_key = 0;
    uint32_t    generation = 0;
    uint32_t    genome = 0;
};

struct BioEntity {
    glm::vec3   pos{0.0f};
    glm::vec3   vel{0.0f};
    glm::vec3   axis{0.0f, 1.0f, 0.0f};
    float       radius    = 8.0f;
    float       energy    = 100.0f;     // health / metabolic energy
    float       age       = 0.0f;       // seconds alive
    float       nutrient_reserve = 1.0f;
    float       starvation = 0.0f;
    float       organelle_health = 1.0f;
    float       telomere_state = 1.0f;
    float       corpse_age = 0.0f;
    float       division_cooldown = 0.0f;
    float       antibiotic_film = 0.0f;
    float       shape_aspect = 1.0f;
    float       shape_noise  = 0.2f;
    float       shape_phase  = 0.0f;
    float       mitosis_progress = 0.0f;
    float       atp        = 80.0f;     // ATP pool (cells ~80, bacteria ~50)
    float       quorum_signal = 0.0f;   // autoinducer concentration (bacteria)
    float       complement_tag = 0.0f;  // complement opsonization level (0-1)
    float       resistance_level = 0.0f; // accumulated antibiotic resistance (bacteria)
    BioGenes     genes{};
    BioPathogenState viral_infection{};
    BioPathogenState bacterial_infection{};
    uint32_t    type      = BIO_CELL;
    uint32_t    morphology = 0;
    uint32_t    entity_id = 0;
    uint32_t    parent_id = 0;
    uint32_t    generation = 0;
    uint32_t    division_count = 0;
    uint32_t    species_key = 0;
    uint32_t    genome    = 0;          // simple genome tag for mutations
    uint32_t    immune_subtype = 0;     // 0=generic WBC, 1=T cell (killer), 2=B cell (antibody producer)
    bool        alive     = true;
    bool        corpse    = false;
    bool        ever_infected = false;
};

// ── Simulation config ───────────────────────────────────────────────────────

struct BiochemConfig {
    uint32_t environment      = BIO_ENV_HUMAN_LUNG;
    uint32_t environment_seed = 1337u;
    uint32_t entity_count     = 200;
    uint32_t max_entities     = 5000;
    float    nutrient_rate    = 2.0f;     // nutrients spawned per second
    bool     autospawn_enabled = false;
    uint32_t autospawn_mode    = BIO_AUTOSPAWN_STATIC;
    float    autospawn_static_rate = 0.6f;
    float    autospawn_dynamic_rate = 2.0f;
    uint32_t autospawn_target_alive = 36;
    float    antibiotic_visibility = 1.30f;
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
    float     shape = 0.0f;
    float     opacity = 1.0f;
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
    size_t count_corpses() const {
        size_t n = 0;
        for (const auto& e : entities)
            if (e.corpse) n++;
        return n;
    }
    size_t count_type(BioEntityType t) const {
        size_t n = 0;
        for (const auto& e : entities)
            if (e.alive && e.type == t) n++;
        return n;
    }
};
