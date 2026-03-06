#include "biochem/biochem_app.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>

// ── Entity type colors / names ──────────────────────────────────────────────

static const char* const BIO_TYPE_NAMES[] = {
    "Cell", "Bacterium", "Virus", "Nutrient",
    "Toxin", "Antibody", "Red Blood Cell", "White Blood Cell"
};

static const ImU32 TYPE_COLORS[] = {
    IM_COL32(70, 160, 255, 255),   // Cell - blue
    IM_COL32(230, 150, 50, 255),   // Bacterium - orange
    IM_COL32(220, 50, 50, 255),    // Virus - red
    IM_COL32(80, 220, 80, 255),    // Nutrient - green
    IM_COL32(200, 50, 200, 255),   // Toxin - purple
    IM_COL32(255, 255, 70, 255),   // Antibody - yellow
    IM_COL32(220, 70, 70, 255),    // Red blood - red
    IM_COL32(240, 240, 255, 255),  // White blood - white
};

static const char* const BIO_ENVIRONMENT_NAMES[] = {
    "Human Lung",
    "Pond Water",
    "Petri Dish",
    "Cat Brain"
};

static glm::vec3 normalized_or(glm::vec3 v, glm::vec3 fallback);
static glm::vec3 sample_local_offset(std::mt19937& rng, float radius);

// ── Draw helpers ────────────────────────────────────────────────────────────

static void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                              ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 16;
    for (int s = STEPS; s >= 0; --s) {
        float t = (float)s / STEPS;
        float r = radius * t;
        if (r < 1.0f) continue;
        float blend = 1.0f - t;
        int a_c = (center_col >> IM_COL32_A_SHIFT) & 0xFF;
        int a_e = (edge_col   >> IM_COL32_A_SHIFT) & 0xFF;
        int a = a_c + (int)((a_e - a_c) * blend);
        int r_c = (center_col >> IM_COL32_R_SHIFT) & 0xFF, r_e = (edge_col >> IM_COL32_R_SHIFT) & 0xFF;
        int g_c = (center_col >> IM_COL32_G_SHIFT) & 0xFF, g_e = (edge_col >> IM_COL32_G_SHIFT) & 0xFF;
        int b_c = (center_col >> IM_COL32_B_SHIFT) & 0xFF, b_e = (edge_col >> IM_COL32_B_SHIFT) & 0xFF;
        int rr = r_c + (int)((r_e - r_c) * blend);
        int gg = g_c + (int)((g_e - g_c) * blend);
        int bb = b_c + (int)((b_e - b_c) * blend);
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 32);
    }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

static float randf_range(float lo, float hi) {
    return lo + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (hi - lo);
}

static float type_default_radius(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return 12.0f;
    case BIO_BACTERIUM:   return 6.0f;
    case BIO_VIRUS:       return 4.0f;
    case BIO_NUTRIENT:    return 3.0f;
    case BIO_TOXIN:       return 4.0f;
    case BIO_ANTIBODY:    return 5.0f;
    case BIO_RED_BLOOD:   return 5.0f;
    case BIO_WHITE_BLOOD: return 12.0f;
    default:              return 8.0f;
    }
}

static uint32_t type_variant_count(uint32_t type) {
    switch (type) {
    case BIO_CELL:      return BIO_CELL_VARIANT_COUNT;
    case BIO_BACTERIUM: return BIO_BACTERIA_VARIANT_COUNT;
    case BIO_VIRUS:     return BIO_VIRUS_VARIANT_COUNT;
    default:            return 1;
    }
}

static float type_default_energy(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return 100.0f;
    case BIO_BACTERIUM:   return 60.0f;
    case BIO_VIRUS:       return 30.0f;
    case BIO_NUTRIENT:    return 25.0f;
    case BIO_TOXIN:       return 45.0f;
    case BIO_ANTIBODY:    return 80.0f;
    case BIO_RED_BLOOD:   return 90.0f;
    case BIO_WHITE_BLOOD: return 200.0f;
    default:              return 100.0f;
    }
}

static BioGenes type_gene_baseline(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return {1.00f, 1.00f, 1.05f, 0.80f, 1.00f};
    case BIO_BACTERIUM:   return {1.15f, 0.80f, 0.85f, 1.30f, 0.90f};
    case BIO_VIRUS:       return {1.20f, 0.30f, 0.55f, 0.55f, 0.75f};
    case BIO_ANTIBODY:    return {1.10f, 0.40f, 0.90f, 0.45f, 0.95f};
    case BIO_RED_BLOOD:   return {0.10f, 0.05f, 0.60f, 1.10f, 1.05f};
    case BIO_WHITE_BLOOD: return {1.30f, 0.75f, 1.10f, 0.70f, 1.20f};
    default:              return {};
    }
}

static void clamp_genes(BioGenes& genes) {
    genes.seek = std::clamp(genes.seek, 0.05f, 2.50f);
    genes.flee = std::clamp(genes.flee, 0.05f, 2.50f);
    genes.spacing = std::clamp(genes.spacing, 0.05f, 2.50f);
    genes.brownian = std::clamp(genes.brownian, 0.00f, 2.50f);
    genes.energy = std::clamp(genes.energy, 0.35f, 2.25f);
}

static void randomize_entity_genes(BioEntity& e, std::mt19937& rng) {
    auto jitter = [&](float amount) {
        return 1.0f + std::uniform_real_distribution<float>(-amount, amount)(rng);
    };

    e.genes = type_gene_baseline(e.type);
    e.genes.seek *= jitter(0.30f);
    e.genes.flee *= jitter(0.28f);
    e.genes.spacing *= jitter(0.24f);
    e.genes.brownian *= jitter(0.35f);
    e.genes.energy *= jitter(0.22f);

    switch (e.type) {
    case BIO_CELL:
        if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_EPITHELIAL) {
            e.genes.spacing *= 1.20f;
            e.genes.brownian *= 0.72f;
        } else if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_AMOEBOID) {
            e.genes.seek *= 1.18f;
            e.genes.brownian *= 1.28f;
            e.genes.flee *= 0.92f;
        }
        break;
    case BIO_BACTERIUM:
        if (e.morphology % BIO_BACTERIA_VARIANT_COUNT == BIO_BACTERIA_BACILLI) {
            e.genes.seek *= 1.12f;
            e.genes.spacing *= 1.08f;
        } else if (e.morphology % BIO_BACTERIA_VARIANT_COUNT == BIO_BACTERIA_SPIRAL) {
            e.genes.brownian *= 1.20f;
            e.genes.flee *= 1.10f;
        }
        break;
    case BIO_VIRUS:
        if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_CORONA) {
            e.genes.seek *= 1.18f;
            e.genes.energy *= 1.06f;
        } else if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_PHAGE) {
            e.genes.spacing *= 0.82f;
            e.genes.seek *= 1.10f;
        }
        break;
    default:
        break;
    }

    clamp_genes(e.genes);
}

static void mutate_entity_genes(BioEntity& e, std::mt19937& rng, float mutation_rate) {
    auto mutate_trait = [&](float& trait, float min_v, float max_v) {
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < std::min(0.85f, mutation_rate * 8.0f + 0.06f)) {
            float delta = std::normal_distribution<float>(0.0f, 0.10f + mutation_rate * 1.75f)(rng);
            trait = std::clamp(trait * (1.0f + delta), min_v, max_v);
        }
    };

    mutate_trait(e.genes.seek, 0.05f, 2.50f);
    mutate_trait(e.genes.flee, 0.05f, 2.50f);
    mutate_trait(e.genes.spacing, 0.05f, 2.50f);
    mutate_trait(e.genes.brownian, 0.00f, 2.50f);
    mutate_trait(e.genes.energy, 0.35f, 2.25f);
    clamp_genes(e.genes);
}

static float metabolic_gene_scale(const BioEntity& e) {
    return std::clamp(1.22f - (e.genes.energy - 1.0f) * 0.40f, 0.65f, 1.45f);
}

static float energy_gain_gene_scale(const BioEntity& e) {
    return std::clamp(0.75f + e.genes.energy * 0.35f, 0.40f, 1.65f);
}

static glm::vec3 random_unit_vector(std::mt19937& rng) {
    return normalized_or(sample_local_offset(rng, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

static uint32_t random_morphology_for(uint32_t type, BioEnvironmentType env, std::mt19937& rng) {
    auto weighted_pick = [&](const float* weights, uint32_t count) -> uint32_t {
        float total = 0.0f;
        for (uint32_t i = 0; i < count; ++i)
            total += weights[i];
        float roll = std::uniform_real_distribution<float>(0.0f, total)(rng);
        for (uint32_t i = 0; i < count; ++i) {
            if (roll <= weights[i])
                return i;
            roll -= weights[i];
        }
        return count > 0 ? count - 1 : 0;
    };

    switch (type) {
    case BIO_CELL: {
        float weights[BIO_CELL_VARIANT_COUNT] = {0.55f, 0.25f, 0.20f};
        if (env == BIO_ENV_HUMAN_LUNG) { weights[0] = 0.35f; weights[1] = 0.50f; weights[2] = 0.15f; }
        else if (env == BIO_ENV_POND_WATER) { weights[0] = 0.20f; weights[1] = 0.10f; weights[2] = 0.70f; }
        else if (env == BIO_ENV_CAT_BRAIN) { weights[0] = 0.65f; weights[1] = 0.20f; weights[2] = 0.15f; }
        return weighted_pick(weights, BIO_CELL_VARIANT_COUNT);
    }
    case BIO_BACTERIUM: {
        float weights[BIO_BACTERIA_VARIANT_COUNT] = {0.30f, 0.50f, 0.20f};
        if (env == BIO_ENV_POND_WATER) { weights[0] = 0.20f; weights[1] = 0.30f; weights[2] = 0.50f; }
        else if (env == BIO_ENV_HUMAN_LUNG) { weights[0] = 0.45f; weights[1] = 0.45f; weights[2] = 0.10f; }
        return weighted_pick(weights, BIO_BACTERIA_VARIANT_COUNT);
    }
    case BIO_VIRUS: {
        float weights[BIO_VIRUS_VARIANT_COUNT] = {0.40f, 0.35f, 0.25f};
        if (env == BIO_ENV_HUMAN_LUNG) { weights[0] = 0.15f; weights[1] = 0.70f; weights[2] = 0.15f; }
        else if (env == BIO_ENV_POND_WATER) { weights[0] = 0.30f; weights[1] = 0.15f; weights[2] = 0.55f; }
        else if (env == BIO_ENV_CAT_BRAIN) { weights[0] = 0.45f; weights[1] = 0.40f; weights[2] = 0.15f; }
        return weighted_pick(weights, BIO_VIRUS_VARIANT_COUNT);
    }
    default:
        return 0;
    }
}

static void configure_entity_shape(BioEntity& e, std::mt19937& rng) {
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    e.axis = random_unit_vector(rng);
    e.shape_phase = randf(0.0f, 6.2831853f);
    e.shape_aspect = 1.0f;
    e.shape_noise = 0.15f;
    e.radius = type_default_radius(e.type);

    switch (e.type) {
    case BIO_CELL:
        switch (e.morphology % BIO_CELL_VARIANT_COUNT) {
        case BIO_CELL_ANIMAL:
            e.radius *= randf(0.95f, 1.10f);
            e.shape_aspect = randf(0.95f, 1.10f);
            e.shape_noise = randf(0.16f, 0.24f);
            break;
        case BIO_CELL_EPITHELIAL:
            e.radius *= randf(1.05f, 1.20f);
            e.shape_aspect = randf(0.58f, 0.78f);
            e.shape_noise = randf(0.08f, 0.15f);
            e.axis = normalized_or(glm::mix(e.axis, glm::vec3(0.0f, 1.0f, 0.0f), 0.55f), glm::vec3(0.0f, 1.0f, 0.0f));
            break;
        case BIO_CELL_AMOEBOID:
        default:
            e.radius *= randf(0.95f, 1.15f);
            e.shape_aspect = randf(0.90f, 1.12f);
            e.shape_noise = randf(0.26f, 0.42f);
            break;
        }
        break;
    case BIO_BACTERIUM:
        switch (e.morphology % BIO_BACTERIA_VARIANT_COUNT) {
        case BIO_BACTERIA_COCCI:
            e.radius *= randf(0.90f, 1.02f);
            e.shape_aspect = randf(0.95f, 1.08f);
            e.shape_noise = randf(0.05f, 0.10f);
            break;
        case BIO_BACTERIA_BACILLI:
            e.radius *= randf(1.12f, 1.28f);
            e.shape_aspect = randf(1.70f, 2.25f);
            e.shape_noise = randf(0.03f, 0.08f);
            break;
        case BIO_BACTERIA_SPIRAL:
        default:
            e.radius *= randf(1.18f, 1.35f);
            e.shape_aspect = randf(2.10f, 2.90f);
            e.shape_noise = randf(0.08f, 0.16f);
            break;
        }
        break;
    case BIO_VIRUS:
        switch (e.morphology % BIO_VIRUS_VARIANT_COUNT) {
        case BIO_VIRUS_CLASSICAL:
            e.radius *= randf(1.00f, 1.15f);
            e.shape_aspect = randf(0.95f, 1.08f);
            e.shape_noise = randf(0.04f, 0.10f);
            break;
        case BIO_VIRUS_CORONA:
            e.radius *= randf(1.08f, 1.22f);
            e.shape_aspect = randf(0.95f, 1.06f);
            e.shape_noise = randf(0.10f, 0.18f);
            break;
        case BIO_VIRUS_PHAGE:
        default:
            e.radius *= randf(1.35f, 1.55f);
            e.shape_aspect = randf(2.50f, 3.20f);
            e.shape_noise = randf(0.02f, 0.06f);
            break;
        }
        break;
    case BIO_WHITE_BLOOD:
        e.shape_noise = randf(0.22f, 0.34f);
        break;
    default:
        break;
    }
}

static float type_temperature_preference(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_WHITE_BLOOD:
    case BIO_RED_BLOOD:
    case BIO_ANTIBODY:
        return 37.0f;
    case BIO_BACTERIUM:
        return 29.0f;
    case BIO_VIRUS:
        return 34.0f;
    case BIO_NUTRIENT:
    case BIO_TOXIN:
    default:
        return 25.0f;
    }
}

static float type_ph_preference(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_WHITE_BLOOD:
    case BIO_RED_BLOOD:
    case BIO_ANTIBODY:
        return 7.25f;
    case BIO_BACTERIUM:
        return 6.85f;
    case BIO_VIRUS:
        return 7.00f;
    case BIO_NUTRIENT:
    case BIO_TOXIN:
    default:
        return 7.00f;
    }
}

static float type_oxygen_need(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return 0.72f;
    case BIO_WHITE_BLOOD: return 0.68f;
    case BIO_RED_BLOOD:   return 0.65f;
    case BIO_ANTIBODY:    return 0.55f;
    case BIO_BACTERIUM:   return 0.35f;
    case BIO_VIRUS:       return 0.05f;
    default:              return 0.0f;
    }
}

static float type_toxicity_sensitivity(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_WHITE_BLOOD:
    case BIO_RED_BLOOD:
        return 1.0f;
    case BIO_ANTIBODY:
        return 0.8f;
    case BIO_BACTERIUM:
        return 0.55f;
    case BIO_VIRUS:
        return 0.15f;
    default:
        return 0.0f;
    }
}

static glm::vec3 normalized_or(glm::vec3 v, glm::vec3 fallback) {
    float len = glm::length(v);
    if (len > 1e-4f)
        return v / len;
    return fallback;
}

static glm::vec3 sample_local_offset(std::mt19937& rng, float radius) {
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    while (true) {
        glm::vec3 p(randf(-radius, radius), randf(-radius, radius), randf(-radius, radius));
        if (glm::dot(p, p) <= radius * radius)
            return p;
    }
}

static float feature_influence(const BioEnvironmentFeature& feature, const glm::vec3& pos) {
    float dist = glm::length(pos - feature.pos);
    if (dist >= feature.radius || feature.radius <= 0.0f)
        return 0.0f;

    float t = 1.0f - dist / feature.radius;
    float exponent = 1.2f + feature.falloff * 2.0f;
    return std::pow(std::max(t, 0.0f), exponent) * feature.strength;
}

static float sample_feature_density(const BiochemEnvironment& environment,
                                    BioEnvironmentFeatureType type,
                                    const glm::vec3& pos) {
    float density = 0.0f;
    for (const auto& feature : environment.features) {
        if (feature.type != type) continue;
        density += feature_influence(feature, pos);
    }
    return density;
}

static const BioEnvironmentFeature* pick_random_feature(std::mt19937& rng,
                                                        const BiochemEnvironment& environment,
                                                        BioEnvironmentFeatureType type) {
    int matches = 0;
    for (const auto& feature : environment.features)
        if (feature.type == type) matches++;
    if (matches == 0)
        return nullptr;

    int choice = std::uniform_int_distribution<int>(0, matches - 1)(rng);
    for (const auto& feature : environment.features) {
        if (feature.type != type) continue;
        if (choice-- == 0)
            return &feature;
    }
    return nullptr;
}

static float compute_environment_stress(const BiochemConfig& cfg,
                                        const BiochemEnvironment& environment,
                                        const BioEntity& e) {
    float thermal_delta = std::abs(cfg.temperature_c - type_temperature_preference(e.type));
    float thermal_stress = std::max(0.0f, thermal_delta - 4.0f) * 0.035f;

    float ph_delta = std::abs(cfg.acidity_ph - type_ph_preference(e.type));
    float ph_stress = std::max(0.0f, ph_delta - 0.2f) * 0.7f;

    float oxygen_stress = std::max(0.0f, type_oxygen_need(e.type) - cfg.oxygen_level) * 1.1f;
    float toxicity_stress = cfg.toxicity * type_toxicity_sensitivity(e.type);
    float local_toxin_stress = sample_feature_density(environment, BIO_ENV_FEATURE_TOXIN, e.pos) * 0.55f;
    float local_nutrient_relief = sample_feature_density(environment, BIO_ENV_FEATURE_NUTRIENT, e.pos) * 0.18f;
    float membrane_shelter = sample_feature_density(environment, BIO_ENV_FEATURE_MEMBRANE, e.pos) * 0.08f;

    float stress = thermal_stress + ph_stress + oxygen_stress + toxicity_stress +
                   local_toxin_stress - local_nutrient_relief - membrane_shelter;
    return std::max(0.0f, stress);
}

static float division_threshold_for(const BiochemConfig& cfg, const BioEntity& e) {
    float threshold = cfg.division_energy;

    if (e.type == BIO_BACTERIUM) {
        float nutrient_bonus = std::min(cfg.nutrient_density, 1.8f) * 0.18f;
        float low_oxygen_bonus = std::max(0.0f, 0.7f - cfg.oxygen_level) * 0.25f;
        float toxicity_penalty = cfg.toxicity * 0.35f;
        threshold *= 0.86f - nutrient_bonus - low_oxygen_bonus + toxicity_penalty;
    } else if (e.type == BIO_CELL) {
        float oxygen_penalty = std::max(0.0f, 0.8f - cfg.oxygen_level) * 0.45f;
        float acidity_penalty = std::max(0.0f, std::abs(cfg.acidity_ph - 7.25f) - 0.15f) * 0.22f;
        threshold *= 1.0f + oxygen_penalty + acidity_penalty + cfg.toxicity * 0.25f;
    }

    threshold *= std::clamp(0.85f + e.genes.energy * 0.22f, 0.70f, 1.30f);
    return std::clamp(threshold, 45.0f, 340.0f);
}

static glm::vec3 environment_flow(const BiochemConfig& cfg,
                                  const BiochemEnvironment& environment,
                                  const BioEntity& e,
                                  float time) {
    glm::vec3 axis = normalized_or(cfg.flow_axis, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 swirl(
        std::sin(e.pos.y * 0.018f + time * 0.8f),
        std::cos(e.pos.z * 0.015f - time * 0.6f),
        std::sin(e.pos.x * 0.012f + time * 0.5f));
    glm::vec3 flow = (axis * 0.65f + swirl * 0.35f) * cfg.flow_strength;

    for (const auto& feature : environment.features) {
        if (feature.type != BIO_ENV_FEATURE_CURRENT) continue;
        float influence = feature_influence(feature, e.pos);
        flow += normalized_or(feature.axis, axis) * (cfg.flow_strength * influence * 0.9f);
    }

    return flow;
}

static void seed_default_population(BiochemState& state,
                                    const BiochemConfig& cfg,
                                    const BiochemEnvironment& environment) {
    state.clear();

    std::mt19937 rng(42);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    float world_radius = cfg.world_radius;
    auto clamp_world = [&](glm::vec3 p) -> glm::vec3 {
        float max_radius = world_radius * 0.82f;
        float len = glm::length(p);
        if (len > max_radius && len > 0.0f)
            p *= max_radius / len;
        return p;
    };
    auto spawn_group = [&](BioEntityType type, int count, float speed, float spread,
                           float energy_scale, int anchor_type) {
        for (int i = 0; i < count; ++i) {
            glm::vec3 center(0.0f);
            float local_spread = spread;
            if (anchor_type >= 0) {
                const BioEnvironmentFeature* feature = pick_random_feature(
                    rng, environment, static_cast<BioEnvironmentFeatureType>(anchor_type));
                if (feature) {
                    center = feature->pos;
                    local_spread = std::min(spread, feature->radius * 0.55f + 8.0f);
                }
            }

            BioEntity e;
            e.type = type;
            e.morphology = random_morphology_for(type, static_cast<BioEnvironmentType>(cfg.environment), rng);
            e.pos = clamp_world(center + sample_local_offset(rng, local_spread));
            e.vel = sample_local_offset(rng, speed);
            e.genome = (uint32_t)rng();
            randomize_entity_genes(e, rng);
            e.energy = type_default_energy(type) * e.genes.energy * energy_scale * randf(0.85f, 1.15f);
            configure_entity_shape(e, rng);
            state.entities.push_back(e);
        }
    };
    float core = world_radius * 0.18f;
    float mid  = world_radius * 0.35f;
    float wide = world_radius * 0.62f;

    switch (cfg.environment) {
    case BIO_ENV_POND_WATER:
        spawn_group(BIO_CELL, 8, 18.0f, core * 1.2f, 0.95f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM, 44, 38.0f, wide, 1.10f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS, 10, 52.0f, mid, 1.0f, BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_NUTRIENT, 64, 8.0f, wide, 1.15f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN, 12, 10.0f, wide * 0.8f, 1.0f, BIO_ENV_FEATURE_TOXIN);
        break;
    case BIO_ENV_PETRI_DISH:
        spawn_group(BIO_CELL, 16, 16.0f, core, 1.0f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM, 34, 34.0f, mid, 1.15f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS, 4, 40.0f, core * 1.1f, 0.9f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_NUTRIENT, 72, 4.0f, wide * 0.75f, 1.25f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN, 4, 5.0f, core * 0.8f, 0.9f, BIO_ENV_FEATURE_TOXIN);
        break;
    case BIO_ENV_CAT_BRAIN:
        spawn_group(BIO_CELL, 48, 22.0f, mid, 1.05f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM, 6, 30.0f, core, 0.9f, BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_VIRUS, 3, 46.0f, core * 0.9f, 0.9f, BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_NUTRIENT, 24, 6.0f, mid, 1.1f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_RED_BLOOD, 18, 20.0f, wide * 0.9f, 1.0f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_WHITE_BLOOD, 4, 18.0f, mid, 1.0f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_ANTIBODY, 4, 26.0f, mid, 1.0f, BIO_ENV_FEATURE_NUTRIENT);
        break;
    case BIO_ENV_HUMAN_LUNG:
    default:
        spawn_group(BIO_CELL, 34, 22.0f, mid, 1.0f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM, 12, 34.0f, mid, 1.0f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS, 6, 54.0f, core * 1.2f, 1.0f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_NUTRIENT, 30, 10.0f, wide * 0.85f, 1.0f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_RED_BLOOD, 12, 18.0f, wide * 0.9f, 1.0f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_WHITE_BLOOD, 5, 16.0f, mid * 1.1f, 1.0f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_ANTIBODY, 4, 22.0f, mid, 1.0f, BIO_ENV_FEATURE_NUTRIENT);
        break;
    }
}

void BiochemApp::regenerate_environment(bool advance_seed) {
    if (advance_seed)
        cfg.environment_seed = cfg.environment_seed * 1664525u + 1013904223u;

    environment_.clear();
    environment_.seed = cfg.environment_seed;

    std::mt19937 rng(cfg.environment_seed);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    auto rand_dir = [&]() -> glm::vec3 {
        glm::vec3 dir = sample_local_offset(rng, 1.0f);
        return normalized_or(dir, glm::vec3(1.0f, 0.0f, 0.0f));
    };
    auto clamp_world = [&](glm::vec3 p) -> glm::vec3 {
        float max_radius = cfg.world_radius * 0.84f;
        float len = glm::length(p);
        if (len > max_radius && len > 0.0f)
            p *= max_radius / len;
        return p;
    };
    auto add_feature = [&](BioEnvironmentFeatureType type, glm::vec3 pos, float radius,
                           glm::vec3 axis, float strength, glm::vec3 tint,
                           float falloff, float noise) {
        BioEnvironmentFeature feature;
        feature.type = type;
        feature.pos = clamp_world(pos);
        feature.radius = radius;
        feature.axis = normalized_or(axis, glm::vec3(1.0f, 0.0f, 0.0f));
        feature.strength = strength;
        feature.tint = tint;
        feature.falloff = falloff;
        feature.noise = noise;
        environment_.features.push_back(feature);
    };

    const BioEnvironmentPreset& preset = bio_environment_preset(static_cast<BioEnvironmentType>(cfg.environment));
    glm::vec3 flow_axis = normalized_or(preset.flow_axis, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 cross_axis = normalized_or(glm::cross(flow_axis, glm::vec3(0.0f, 1.0f, 0.0f)),
                                         glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 up_axis = normalized_or(glm::cross(cross_axis, flow_axis), glm::vec3(0.0f, 1.0f, 0.0f));
    float wr = cfg.world_radius;

    switch (cfg.environment) {
    case BIO_ENV_HUMAN_LUNG:
    default:
        for (int branch = 0; branch < 3; ++branch) {
            float branch_t = branch / 2.0f;
            glm::vec3 branch_center = flow_axis * (-wr * 0.30f + branch_t * wr * 0.34f);
            for (int side = -1; side <= 1; side += 2) {
                for (int sac = 0; sac < 5; ++sac) {
                    glm::vec3 pos = branch_center
                        + cross_axis * (side * (wr * 0.12f + randf(0.0f, wr * 0.08f)))
                        + up_axis * randf(-wr * 0.18f, wr * 0.18f)
                        + sample_local_offset(rng, wr * 0.03f);
                    add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.10f, wr * 0.15f),
                                cross_axis * (float)side + up_axis * randf(-0.4f, 0.4f),
                                randf(0.7f, 1.0f), preset.tint * 0.9f + glm::vec3(0.08f, 0.12f, 0.10f),
                                randf(0.4f, 0.7f), randf(0.0f, 1.0f));
                }
            }
            add_feature(BIO_ENV_FEATURE_NUTRIENT,
                        branch_center + up_axis * randf(-wr * 0.10f, wr * 0.10f),
                        randf(wr * 0.08f, wr * 0.12f), up_axis,
                        randf(0.45f, 0.75f), glm::vec3(0.20f, 0.48f, 0.30f), 0.35f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 5; ++i) {
            float t = -0.35f + 0.18f * (float)i;
            add_feature(BIO_ENV_FEATURE_CURRENT,
                        flow_axis * (wr * t) + sample_local_offset(rng, wr * 0.05f),
                        randf(wr * 0.12f, wr * 0.18f), flow_axis + rand_dir() * 0.25f,
                        randf(0.65f, 0.95f), glm::vec3(0.24f, 0.42f, 0.36f), 0.30f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_POND_WATER:
        for (int i = 0; i < 12; ++i) {
            glm::vec3 pos(randf(-wr * 0.65f, wr * 0.65f),
                          randf(-wr * 0.45f, wr * 0.15f),
                          randf(-wr * 0.65f, wr * 0.65f));
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.06f, wr * 0.11f), rand_dir(),
                        randf(0.45f, 0.70f), glm::vec3(0.18f, 0.28f, 0.18f), 0.65f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos(randf(-wr * 0.70f, wr * 0.70f),
                          randf(-wr * 0.20f, wr * 0.35f),
                          randf(-wr * 0.70f, wr * 0.70f));
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.07f, wr * 0.13f),
                        rand_dir(), randf(0.55f, 0.95f), glm::vec3(0.22f, 0.50f, 0.24f),
                        0.45f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 6; ++i) {
            glm::vec3 pos(randf(-wr * 0.75f, wr * 0.75f),
                          randf(-wr * 0.55f, wr * 0.10f),
                          randf(-wr * 0.75f, wr * 0.75f));
            add_feature(BIO_ENV_FEATURE_TOXIN, pos, randf(wr * 0.07f, wr * 0.15f),
                        rand_dir(), randf(0.65f, 1.05f), glm::vec3(0.36f, 0.18f, 0.26f),
                        0.55f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 4; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.55f),
                        randf(wr * 0.14f, wr * 0.22f), normalized_or(flow_axis + rand_dir() * 0.8f, flow_axis),
                        randf(0.45f, 0.80f), glm::vec3(0.16f, 0.32f, 0.30f), 0.30f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_PETRI_DISH: {
        float ring_r = wr * 0.58f;
        for (int i = 0; i < 14; ++i) {
            float a = (float)i / 14.0f * 6.2831853f;
            glm::vec3 ring_pos(std::cos(a) * ring_r, randf(-wr * 0.05f, wr * 0.05f), std::sin(a) * ring_r);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, ring_pos, randf(wr * 0.07f, wr * 0.10f),
                        glm::vec3(-std::sin(a), 0.0f, std::cos(a)), randf(0.65f, 0.90f),
                        glm::vec3(0.34f, 0.34f, 0.18f), 0.75f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 5; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.20f);
            pos.y *= 0.2f;
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.08f, wr * 0.14f),
                        up_axis, randf(0.75f, 1.10f), glm::vec3(0.32f, 0.52f, 0.22f),
                        0.32f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 3; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.28f),
                        randf(wr * 0.10f, wr * 0.16f), rand_dir(),
                        randf(0.25f, 0.45f), glm::vec3(0.26f, 0.30f, 0.18f), 0.25f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 2; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.25f),
                        randf(wr * 0.05f, wr * 0.08f), rand_dir(),
                        randf(0.25f, 0.40f), glm::vec3(0.40f, 0.20f, 0.28f), 0.50f, randf(0.0f, 1.0f));
        }
        break;
    }

    case BIO_ENV_CAT_BRAIN:
        for (int side = -1; side <= 1; side += 2) {
            for (int lobe = 0; lobe < 7; ++lobe) {
                float u = lobe / 6.0f;
                glm::vec3 pos = cross_axis * (side * (wr * 0.16f + randf(0.0f, wr * 0.10f)))
                    + flow_axis * randf(-wr * 0.24f, wr * 0.24f)
                    + up_axis * (-wr * 0.08f + u * wr * 0.30f)
                    + sample_local_offset(rng, wr * 0.04f);
                add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.09f, wr * 0.14f),
                            up_axis + cross_axis * (float)side * 0.4f, randf(0.75f, 1.00f),
                            glm::vec3(0.26f, 0.24f, 0.36f), 0.55f, randf(0.0f, 1.0f));
            }
        }
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = flow_axis * randf(-wr * 0.25f, wr * 0.25f)
                + up_axis * randf(-wr * 0.10f, wr * 0.30f)
                + cross_axis * randf(-wr * 0.20f, wr * 0.20f);
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.06f, wr * 0.10f),
                        flow_axis + up_axis * 0.3f, randf(0.55f, 0.80f),
                        glm::vec3(0.30f, 0.22f, 0.18f), 0.35f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 4; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.35f),
                        randf(wr * 0.12f, wr * 0.18f), normalized_or(up_axis + rand_dir() * 0.4f, up_axis),
                        randf(0.40f, 0.70f), glm::vec3(0.26f, 0.28f, 0.40f), 0.28f, randf(0.0f, 1.0f));
        }
        for (int i = 0; i < 2; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.18f),
                        randf(wr * 0.05f, wr * 0.09f), rand_dir(), randf(0.15f, 0.35f),
                        glm::vec3(0.34f, 0.18f, 0.22f), 0.55f, randf(0.0f, 1.0f));
        }
        break;
    }
}

void BiochemApp::apply_environment_preset(BioEnvironmentType env, bool reseed_population) {
    const BioEnvironmentPreset& preset = bio_environment_preset(env);
    cfg.environment = env;
    cfg.temperature_c = preset.temperature_c;
    cfg.acidity_ph = preset.acidity_ph;
    cfg.oxygen_level = preset.oxygen_level;
    cfg.nutrient_density = preset.nutrient_density;
    cfg.flow_strength = preset.flow_strength;
    cfg.toxicity = preset.toxicity;
    cfg.immune_pressure = preset.immune_pressure;
    cfg.fluid_damping = preset.fluid_damping;
    cfg.environment_tint = preset.tint;
    cfg.flow_axis = preset.flow_axis;
    cfg.immune_system = preset.immune_system;

    if (reseed_population)
        reset_simulation();
    else
        regenerate_environment(false);
}

void BiochemApp::reset_camera_pose() {
    camera = OrbitCamera{};
    camera.distance = 500.0f;
    camera.elevation = 0.4f;
    camera.fov = 50.0f;
    camera.near_clip = 0.5f;
    camera.far_clip = 5000.0f;
}

void BiochemApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);
    raytracer_.init(vk, renderer.render_pass());

    apply_environment_preset(BIO_ENV_HUMAN_LUNG, false);
    reset_camera_pose();

    seed_default_population(state, cfg, environment_);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
}

void BiochemApp::destroy() {
    raytracer_.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

void BiochemApp::reset_simulation() {
    regenerate_environment(true);
    seed_default_population(state, cfg, environment_);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
    selected_entity = -1;
    nutrient_timer_ = 0.0f;
    sim_time_ = 0.0f;
    paused = false;
}

void BiochemApp::spawn_at(glm::vec3 pos) {
    int t = spawn_bio_type_ % BIO_TYPE_COUNT;
    BioEntity e;
    e.pos = pos;
    e.vel = {0, 0, 0};
    e.energy = spawn_energy_;
    e.type = (uint32_t)t;
    e.morphology = (uint32_t)(spawn_variant_ % (int)type_variant_count((uint32_t)t));
    e.genome = (uint32_t)rand();
    std::mt19937 rng(e.genome ^ (uint32_t)t * 747796405u);
    randomize_entity_genes(e, rng);
    configure_entity_shape(e, rng);

    state.entities.push_back(e);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void BiochemApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    // WASD camera panning
    if (!paused && !show_splash && !show_pause_menu && !ImGui::GetIO().WantTextInput) {
        float move_speed = camera.distance * 0.5f * dt;
        glm::vec3 fwd = camera.forward_direction();
        glm::vec3 right = camera.right_direction();
        fwd.y = 0; fwd = glm::normalize(fwd);
        right.y = 0; right = glm::normalize(right);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.target += fwd * move_speed;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.target -= fwd * move_speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.target += right * move_speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.target -= right * move_speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) camera.target.y += move_speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) camera.target.y -= move_speed;
    }

    if (!paused && !show_splash) {
        step_simulation(dt);
        sim_time_ += dt;
    }

    ImGuiIO& io = ImGui::GetIO();

    // GPU raytraced entity rendering (before UI overlays)
    if (!show_splash && !show_pause_menu) {
        raytracer_.update_and_draw(vk, renderer.current_cmd(), state, environment_, camera, cfg,
                                    io.DisplaySize.x, io.DisplaySize.y, sim_time_);
    }

    render_overlay();
    render_ui();

    renderer.end_frame(vk);
}

// ── Simulation ──────────────────────────────────────────────────────────────

void BiochemApp::spawn_nutrient() {
    float r = cfg.world_radius * 0.8f;
    glm::vec3 axis = normalized_or(cfg.flow_axis, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 spawn_pos = {
        randf_range(-r, r) + axis.x * randf_range(-r * 0.2f, r * 0.2f),
        randf_range(-r, r) + axis.y * randf_range(-r * 0.2f, r * 0.2f),
        randf_range(-r, r) + axis.z * randf_range(-r * 0.2f, r * 0.2f)
    };

    float best_weight = 0.0f;
    for (const auto& feature : environment_.features) {
        if (feature.type != BIO_ENV_FEATURE_NUTRIENT) continue;
        float candidate = randf_range(0.0f, 1.0f) * feature.strength;
        if (candidate > best_weight) {
            best_weight = candidate;
            spawn_pos = feature.pos + feature.axis * randf_range(-feature.radius * 0.25f, feature.radius * 0.25f) +
                        glm::vec3(randf_range(-feature.radius * 0.35f, feature.radius * 0.35f),
                                  randf_range(-feature.radius * 0.35f, feature.radius * 0.35f),
                                  randf_range(-feature.radius * 0.35f, feature.radius * 0.35f));
        }
    }

    BioEntity e;
    float pos_len = glm::length(spawn_pos);
    if (pos_len > cfg.world_radius && pos_len > 0.0f)
        spawn_pos *= cfg.world_radius / pos_len;
    e.pos = spawn_pos;
    e.vel = axis * (cfg.flow_strength * 0.12f) +
            glm::vec3(randf_range(-2.0f, 2.0f), randf_range(-2.0f, 2.0f), randf_range(-2.0f, 2.0f));
    e.radius = type_default_radius(BIO_NUTRIENT);
    e.type   = BIO_NUTRIENT;
    e.genome = (uint32_t)rand();
    std::mt19937 rng(e.genome ^ 0xA341316Cu);
    randomize_entity_genes(e, rng);
    e.energy = type_default_energy(BIO_NUTRIENT) * e.genes.energy * (0.75f + cfg.nutrient_density * 0.35f);
    configure_entity_shape(e, rng);
    state.entities.push_back(e);
}

void BiochemApp::step_simulation(float dt) {
    float scaled_dt = dt * cfg.dt_scale;
    auto& ents = state.entities;
    float wr = cfg.world_radius;

    // Spawn nutrients over time
    nutrient_timer_ += scaled_dt;
    float nutrient_rate = std::max(cfg.nutrient_rate * cfg.nutrient_density, 0.1f);
    float interval = 1.0f / nutrient_rate;
    while (nutrient_timer_ >= interval) {
        nutrient_timer_ -= interval;
        spawn_nutrient();
    }

    // Update each entity
    for (auto& e : ents) {
        if (!e.alive) continue;

        // Environment flow + movement
        e.vel += environment_flow(cfg, environment_, e, sim_time_) * scaled_dt * 0.08f;
        e.pos += e.vel * scaled_dt;
        float damping = std::clamp(cfg.viscosity * cfg.fluid_damping, 0.90f, 0.9995f);
        e.vel *= damping;
        e.age += scaled_dt;

        // Bounce off spherical world boundary
        float dist_from_center = glm::length(e.pos);
        if (dist_from_center > wr) {
            glm::vec3 norm = e.pos / dist_from_center;
            e.pos = norm * wr;
            e.vel -= 2.0f * glm::dot(e.vel, norm) * norm;
            e.vel *= 0.8f;
        }

        // Metabolism
        if (e.type == BIO_CELL || e.type == BIO_BACTERIUM ||
            e.type == BIO_WHITE_BLOOD || e.type == BIO_RED_BLOOD ||
            e.type == BIO_ANTIBODY) {
            float base_metabolism = cfg.metabolism_rate;
            if (e.type == BIO_BACTERIUM) base_metabolism *= 0.75f;
            if (e.type == BIO_WHITE_BLOOD || e.type == BIO_ANTIBODY) base_metabolism *= 1.20f;
            if (e.type == BIO_RED_BLOOD) base_metabolism *= 0.55f;
            base_metabolism *= metabolic_gene_scale(e);

            float stress = compute_environment_stress(cfg, environment_, e);
            e.energy -= base_metabolism * scaled_dt * (1.0f + stress);

            if (e.type == BIO_BACTERIUM) {
                float scavenging = std::max(0.0f, cfg.nutrient_density - 0.8f) * 0.55f;
                scavenging += std::max(0.0f, 0.65f - cfg.oxygen_level) * 0.25f;
                scavenging += sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, e.pos) * 1.5f;
                scavenging -= sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, e.pos) * 0.9f;
                e.energy += scavenging * scaled_dt * 8.0f * energy_gain_gene_scale(e);
            } else if (e.type == BIO_CELL) {
                float gain_scale = energy_gain_gene_scale(e);
                e.energy += std::max(0.0f, cfg.oxygen_level - 0.65f) * scaled_dt * 2.0f * gain_scale;
                e.energy += sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, e.pos) * scaled_dt * 2.0f * gain_scale;
            }

            if (e.energy <= 0) {
                e.alive = false;
                continue;
            }
        }

        // Viruses die after a while
        if (e.type == BIO_VIRUS && e.age > (20.0f + cfg.nutrient_density * 10.0f) * std::clamp(e.genes.energy, 0.6f, 1.8f))
            e.alive = false;
        if (e.type == BIO_NUTRIENT && e.age > 80.0f + cfg.nutrient_density * 40.0f)
            e.alive = false;
        if (e.type == BIO_TOXIN && e.age > 60.0f)
            e.alive = false;
    }

    // AI steering behaviors
    if (cfg.ai_movement)
        process_ai_movement(scaled_dt);

    // Eating nutrients
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (ents[i].type != BIO_CELL && ents[i].type != BIO_BACTERIUM) continue;

        for (size_t j = 0; j < ents.size(); j++) {
            if (i == j || !ents[j].alive) continue;

            glm::vec3 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (ents[j].type == BIO_NUTRIENT && dist < touch) {
                float uptake = 0.75f + cfg.nutrient_density * 0.25f;
                ents[i].energy += ents[j].energy * uptake * energy_gain_gene_scale(ents[i]);
                ents[j].alive = false;
            } else if (ents[j].type == BIO_TOXIN && dist < touch) {
                ents[i].energy -= ents[j].energy * (0.10f + cfg.toxicity * 0.08f);
            }
        }
    }

    // Subsystems
    process_repulsion();
    process_cell_division();
    process_virus_infection(scaled_dt);
    if (cfg.immune_system)
        process_antibody_response(scaled_dt);

    // Remove dead entities periodically
    size_t dead = 0;
    for (const auto& e : ents)
        if (!e.alive) dead++;
    if (dead > 50) {
        if (selected_entity >= 0) {
            int new_idx = 0;
            for (int i = 0; i < selected_entity && i < (int)ents.size(); i++) {
                if (ents[i].alive) new_idx++;
            }
            if (selected_entity < (int)ents.size() && ents[selected_entity].alive)
                selected_entity = new_idx;
            else
                selected_entity = -1;
        }
        ents.erase(std::remove_if(ents.begin(), ents.end(),
            [](const BioEntity& e) { return !e.alive; }), ents.end());
    }

    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
}

// ── Cell Division ───────────────────────────────────────────────────────────

void BiochemApp::process_cell_division() {
    auto& ents = state.entities;
    size_t n = ents.size();
    if (n > 1000) return;

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;
        if (e.type != BIO_CELL && e.type != BIO_BACTERIUM) continue;
        if (e.energy < division_threshold_for(cfg, e)) continue;

        BioEntity child;
        child.type = e.type;
        child.morphology = e.morphology;
        child.genes = e.genes;
        child.energy = e.energy * 0.5f;
        e.energy *= 0.5f;

        float theta = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
        float phi = static_cast<float>(rand()) / RAND_MAX * 3.1416f - 1.5708f;
        glm::vec3 dir(cosf(phi) * cosf(theta), sinf(phi), cosf(phi) * sinf(theta));
        glm::vec3 offset = dir * e.radius;

        child.pos = e.pos + offset;
        e.pos -= offset;
        child.vel = e.vel + dir * 5.0f;
        child.genome = e.genome;

        float roll = static_cast<float>(rand()) / RAND_MAX;
        std::mt19937 rng(child.genome ^ (uint32_t)(i * 2654435761u));
        if (roll < cfg.mutation_rate) {
            int bit = rand() % 32;
            child.genome ^= (1u << bit);
            if ((rand() % 100) < 25)
                child.morphology = (child.morphology + 1 + rand() % (int)type_variant_count(child.type))
                    % type_variant_count(child.type);
            mutate_entity_genes(child, rng, cfg.mutation_rate);
        }

        child.alive = true;
        child.age = 0.0f;
        configure_entity_shape(child, rng);
        ents.push_back(child);
    }
}

// ── Virus Infection ─────────────────────────────────────────────────────────

void BiochemApp::process_virus_infection(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();
    float thermal_alignment = 1.0f - std::min(std::abs(cfg.temperature_c - 34.0f), 18.0f) / 18.0f * 0.45f;
    float immune_drag = 1.0f / (1.0f + cfg.immune_pressure * 0.55f);
    float oxygen_bonus = 1.0f + std::max(0.0f, 0.7f - cfg.oxygen_level) * 0.45f;
    float effective_rate = cfg.infection_rate * thermal_alignment * immune_drag * oxygen_bonus;

    for (size_t i = 0; i < n; i++) {
        if (!ents[i].alive || ents[i].type != BIO_VIRUS) continue;

        for (size_t j = 0; j < n; j++) {
            if (i == j || !ents[j].alive) continue;
            if (ents[j].type != BIO_CELL) continue;

            glm::vec3 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);

            if (dist < cfg.infection_radius) {
                float local_shelter = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, ents[j].pos);
                float local_toxin = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, ents[j].pos);
                float local_factor = std::clamp(1.0f + local_toxin * 0.35f - local_shelter * 0.20f, 0.55f, 1.75f);
                float damage = effective_rate * local_factor * dt * (1.0f - dist / cfg.infection_radius) *
                               20.0f * std::clamp(ents[i].genes.energy, 0.65f, 1.8f);
                ents[j].energy -= damage;

                if (dist > 1.0f) {
                    glm::vec3 dir = diff / dist;
                    ents[i].vel += dir * 30.0f * dt;
                }

                if (ents[j].energy < 30.0f && ents[j].alive) {
                    ents[j].alive = false;
                    int spawn_count = 2 + rand() % 2;
                    for (int k = 0; k < spawn_count && ents.size() < 1200; k++) {
                        BioEntity v;
                        float a = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
                        float p = static_cast<float>(rand()) / RAND_MAX * 3.1416f - 1.5708f;
                        glm::vec3 d(cosf(p) * cosf(a), sinf(p), cosf(p) * sinf(a));
                        v.pos = ents[j].pos + d * 8.0f;
                        v.vel = d * 40.0f;
                        v.radius = 4.0f;
                        v.energy = 30.0f;
                        v.type = BIO_VIRUS;
                        v.morphology = ents[i].morphology;
                        v.genes = ents[i].genes;
                        v.genome = ents[i].genome;
                        std::mt19937 vrng(v.genome ^ (uint32_t)(k * 977u + i));
                        if (randf_range(0.0f, 1.0f) < cfg.mutation_rate * 1.5f)
                            mutate_entity_genes(v, vrng, cfg.mutation_rate * 1.2f);
                        v.energy *= std::clamp(v.genes.energy, 0.45f, 1.8f);
                        configure_entity_shape(v, vrng);
                        ents.push_back(v);
                    }
                }
            }
        }
    }
}

// ── Antibody / Immune Response ──────────────────────────────────────────────

void BiochemApp::process_antibody_response(float dt) {
    auto& ents = state.entities;
    float wr = cfg.world_radius;
    float effective_immune = cfg.immune_strength * (0.45f + cfg.immune_pressure);

    size_t virus_count = 0, wbc_count = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.type == BIO_VIRUS) virus_count++;
        if (e.type == BIO_WHITE_BLOOD) wbc_count++;
    }

    if (virus_count > wbc_count && ents.size() < 1200 &&
        randf_range(0.0f, 1.0f) < std::min(0.65f, 0.10f + cfg.immune_pressure * 0.12f)) {
        BioEntity wbc;
        wbc.pos = {randf_range(-wr * 0.8f, wr * 0.8f),
                   randf_range(-wr * 0.8f, wr * 0.8f),
                   randf_range(-wr * 0.8f, wr * 0.8f)};
        wbc.vel = {0, 0, 0};
        wbc.type = BIO_WHITE_BLOOD;
        wbc.genome = (uint32_t)rand();
        std::mt19937 wrng(wbc.genome ^ 0xC2B2AE35u);
        randomize_entity_genes(wbc, wrng);
        wbc.energy = type_default_energy(BIO_WHITE_BLOOD) * wbc.genes.energy;
        configure_entity_shape(wbc, wrng);
        ents.push_back(wbc);
    }

    if (virus_count > 0 && cfg.immune_pressure > 0.4f && ents.size() < 1200 &&
        randf_range(0.0f, 1.0f) < std::min(0.45f, cfg.immune_pressure * 0.06f * dt * 60.0f)) {
        BioEntity antibody;
        antibody.pos = {randf_range(-wr * 0.6f, wr * 0.6f),
                        randf_range(-wr * 0.6f, wr * 0.6f),
                        randf_range(-wr * 0.6f, wr * 0.6f)};
        antibody.vel = {0, 0, 0};
        antibody.type = BIO_ANTIBODY;
        antibody.genome = (uint32_t)rand();
        std::mt19937 arng(antibody.genome ^ 0x27D4EB2Du);
        randomize_entity_genes(antibody, arng);
        antibody.energy = type_default_energy(BIO_ANTIBODY) * antibody.genes.energy;
        configure_entity_shape(antibody, arng);
        ents.push_back(antibody);
    }

    for (auto& wbc : ents) {
        if (!wbc.alive || wbc.type != BIO_WHITE_BLOOD) continue;

        float best_dist = 999999.0f;
        int best_idx = -1;
        for (size_t j = 0; j < ents.size(); j++) {
            if (!ents[j].alive) continue;
            if (ents[j].type != BIO_VIRUS && ents[j].type != BIO_TOXIN) continue;
            float d = glm::length(ents[j].pos - wbc.pos);
            if (d < best_dist) {
                best_dist = d;
                best_idx = (int)j;
            }
        }

        if (best_idx >= 0) {
            glm::vec3 dir = ents[best_idx].pos - wbc.pos;
            float dist = glm::length(dir);
            if (dist > 1.0f) {
                dir /= dist;
                float chase_speed = 60.0f * effective_immune * wbc.genes.seek;
                wbc.vel += dir * chase_speed * dt;
                float spd = glm::length(wbc.vel);
                if (spd > 80.0f) wbc.vel *= 80.0f / spd;
            }

            float touch = wbc.radius + ents[best_idx].radius;
            if (dist < touch) {
                ents[best_idx].alive = false;
                wbc.energy += 10.0f;
            }
        }

        wbc.energy -= (0.3f + cfg.toxicity * 0.5f) * dt;
        if (wbc.energy <= 0) wbc.alive = false;
    }

    for (auto& antibody : ents) {
        if (!antibody.alive || antibody.type != BIO_ANTIBODY) continue;

        float best_dist = cfg.infection_radius;
        int best_idx = -1;
        for (size_t j = 0; j < ents.size(); ++j) {
            if (!ents[j].alive || ents[j].type != BIO_VIRUS) continue;
            float d = glm::length(ents[j].pos - antibody.pos);
            if (d < best_dist) {
                best_dist = d;
                best_idx = (int)j;
            }
        }

        if (best_idx >= 0 && best_dist < antibody.radius + ents[best_idx].radius + 3.0f) {
            ents[best_idx].alive = false;
            antibody.energy -= 10.0f;
        }

        antibody.energy -= 0.45f * dt;
        if (antibody.energy <= 0.0f)
            antibody.alive = false;
    }
}

// ── AI Movement ─────────────────────────────────────────────────────────────

void BiochemApp::process_ai_movement(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;

        float max_speed = 0.0f;
        float seek_force = cfg.seek_strength * e.genes.seek;
        float flee_force = cfg.flee_strength * e.genes.flee;
        float spacing_force = cfg.spacing_strength * e.genes.spacing;
        float brownian_force = cfg.brownian_strength * e.genes.brownian;

        if (e.type == BIO_CELL || e.type == BIO_BACTERIUM) {
            max_speed = (e.type == BIO_CELL) ? 60.0f : 80.0f;

            // Seek nearest nutrient
            float best_food_dist = 150.0f;
            int best_food = -1;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive || ents[j].type != BIO_NUTRIENT) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (d < best_food_dist) { best_food_dist = d; best_food = (int)j; }
            }
            if (best_food >= 0) {
                glm::vec3 dir = ents[best_food].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * dt;
            }

            // Flee nearest virus/toxin
            float best_threat_dist = 100.0f;
            int best_threat = -1;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive) continue;
                if (ents[j].type != BIO_VIRUS && ents[j].type != BIO_TOXIN) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (d < best_threat_dist) { best_threat_dist = d; best_threat = (int)j; }
            }
            if (best_threat >= 0) {
                glm::vec3 away = e.pos - ents[best_threat].pos;
                float d = glm::length(away);
                if (d > 1.0f)
                    e.vel += glm::normalize(away) * flee_force * dt;
            }

            // Spacing from same type
            for (size_t j = 0; j < n; j++) {
                if (i == j || !ents[j].alive || ents[j].type != e.type) continue;
                glm::vec3 diff = e.pos - ents[j].pos;
                float d = glm::length(diff);
                float min_dist = (e.radius + ents[j].radius) * (1.35f + 0.65f * e.genes.spacing);
                if (d < min_dist && d > 0.1f)
                    e.vel += glm::normalize(diff) * spacing_force * dt;
            }
        }
        else if (e.type == BIO_VIRUS) {
            max_speed = 100.0f;
            // Seek nearest cell
            float best_dist = 200.0f;
            int best = -1;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive || ents[j].type != BIO_CELL) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (d < best_dist) { best_dist = d; best = (int)j; }
            }
            if (best >= 0) {
                glm::vec3 dir = ents[best].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * 0.5f * dt;
            }
        }
        else if (e.type == BIO_RED_BLOOD) {
            max_speed = 40.0f;
            // Brownian motion
            float rx = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float ry = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float rz = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * dt;
        }
        else if (e.type == BIO_NUTRIENT) {
            max_speed = 15.0f;
            // Gentle drift
            float rx = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float ry = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float rz = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * 0.3f * dt;
        }
        else if (e.type == BIO_ANTIBODY) {
            max_speed = 70.0f;
            // Seek nearest virus
            float best_dist = 120.0f;
            int best = -1;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive || ents[j].type != BIO_VIRUS) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (d < best_dist) { best_dist = d; best = (int)j; }
            }
            if (best >= 0) {
                glm::vec3 dir = ents[best].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * 0.7f * dt;
            }
        }

        // Clamp speed
        if (max_speed > 0.0f) {
            float spd = glm::length(e.vel);
            if (spd > max_speed)
                e.vel *= max_speed / spd;
        }
    }
}

// ── Repulsion ───────────────────────────────────────────────────────────────

void BiochemApp::process_repulsion() {
    auto& ents = state.entities;
    size_t n = ents.size();

    for (size_t i = 0; i < n; i++) {
        if (!ents[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!ents[j].alive) continue;

            glm::vec3 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (dist < touch && dist > 0.1f) {
                float overlap = touch - dist;
                glm::vec3 dir = diff / dist;
                float push = overlap * 0.5f;
                ents[i].pos -= dir * push;
                ents[j].pos += dir * push;
                ents[i].vel -= dir * push * 2.0f;
                ents[j].vel += dir * push * 2.0f;
            }
        }
    }
}

// ── Overlay rendering (selection highlight via DrawList) ────────────────────

void BiochemApp::render_overlay() {
    if (show_splash || show_pause_menu) return;
    if (selected_entity < 0 || selected_entity >= (int)state.entities.size()) return;

    const auto& e = state.entities[selected_entity];
    if (!e.alive) { selected_entity = -1; return; }

    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float aspect = W / H;

    glm::mat4 vp = camera.proj_matrix(aspect) * camera.view_matrix();
    glm::vec4 clip = vp * glm::vec4(e.pos, 1.0f);
    if (clip.w <= 0.0f) return;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * W;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * H;

    float fov_rad = glm::radians(camera.fov);
    float sr = (e.radius / clip.w) * (H / (2.0f * std::tan(fov_rad * 0.5f)));
    float pick_r = std::max(sr, 8.0f);

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddCircle(ImVec2(sx, sy), pick_r + 4.0f,
        IM_COL32(255, 255, 100, 200), 24, 2.0f);
}

// ── Menu background (animated bio particles) ────────────────────────────────

void BiochemApp::draw_menu_background() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(3, 10, 16, 255));

    if (!menu_bg_inited_) {
        menu_bg_inited_ = true;
        menu_particles_.resize(50);
        std::mt19937 rng(99);
        auto randf = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        for (auto& p : menu_particles_) {
            p.x = randf(0, W);
            p.y = randf(0, H);
            p.vx = randf(-10, 10);
            p.vy = randf(-10, 10);
            p.radius = randf(2.0f, 6.0f);
            int variant = rng() % 4;
            if (variant == 0) { p.r = 0.2f; p.g = 0.7f; p.b = 0.3f; }
            else if (variant == 1) { p.r = 0.3f; p.g = 0.5f; p.b = 0.9f; }
            else if (variant == 2) { p.r = 0.2f; p.g = 0.7f; p.b = 0.7f; }
            else { p.r = 0.6f; p.g = 0.3f; p.b = 0.7f; }
            p.alpha = randf(0.3f, 0.7f);
            for (int t = 0; t < 12; t++) { p.trail_x[t] = p.x; p.trail_y[t] = p.y; }
        }
    }

    menu_bg_time_ += io.DeltaTime;

    for (int i = 0; i < 3; i++) {
        float phase = menu_bg_time_ * 0.12f + (float)i * 2.1f;
        float cx = W * (0.3f + 0.4f * sinf(phase));
        float cy = H * (0.3f + 0.4f * cosf(phase * 0.7f + 1.0f));
        float glow_r = 180.0f + 40.0f * sinf(phase * 1.3f);
        ImU32 center, edge;
        if (i == 0) { center = IM_COL32(30, 120, 60, 16); edge = IM_COL32(15, 60, 30, 0); }
        else if (i == 1) { center = IM_COL32(40, 80, 160, 14); edge = IM_COL32(20, 40, 100, 0); }
        else { center = IM_COL32(80, 40, 120, 12); edge = IM_COL32(40, 20, 80, 0); }
        draw_radial_glow(bg, cx, cy, glow_r, center, edge);
    }

    float dt = io.DeltaTime;
    for (auto& p : menu_particles_) {
        for (int t = 11; t > 0; t--) { p.trail_x[t] = p.trail_x[t-1]; p.trail_y[t] = p.trail_y[t-1]; }
        p.trail_x[0] = p.x;
        p.trail_y[0] = p.y;

        p.x += p.vx * dt;
        p.y += p.vy * dt;

        if (p.x < -20) p.x += W + 40;
        if (p.x > W + 20) p.x -= W + 40;
        if (p.y < -20) p.y += H + 40;
        if (p.y > H + 20) p.y -= H + 40;

        for (int t = 1; t < 12; t++) {
            float frac = 1.0f - (float)t / 12.0f;
            int alpha = (int)(p.alpha * frac * 35.0f);
            bg->AddLine(ImVec2(p.trail_x[t-1], p.trail_y[t-1]),
                        ImVec2(p.trail_x[t], p.trail_y[t]),
                        IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha),
                        p.radius * frac * 0.5f);
        }

        int alpha = (int)(p.alpha * 255.0f);
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha), 16);
        bg->AddCircle(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*180), (int)(p.g*180), (int)(p.b*180), alpha / 2), 16, 0.8f);
    }

    for (size_t i = 0; i < menu_particles_.size(); i++) {
        for (size_t j = i + 1; j < menu_particles_.size(); j++) {
            float dx = menu_particles_[j].x - menu_particles_[i].x;
            float dy = menu_particles_[j].y - menu_particles_[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 90.0f) {
                float a = (1.0f - d / 90.0f) * 18.0f;
                bg->AddLine(ImVec2(menu_particles_[i].x, menu_particles_[i].y),
                            ImVec2(menu_particles_[j].x, menu_particles_[j].y),
                            IM_COL32(60, 180, 100, (int)a), 0.5f);
            }
        }
    }

    draw_radial_glow(bg, W * 0.5f, H * 0.5f, std::max(W, H) * 0.8f,
                     IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));

    for (float y = 0; y < H; y += 3.0f)
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(0, 0, 0, 6));
}

// ── Splash screen ───────────────────────────────────────────────────────────

void BiochemApp::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    splash_time_ += io.DeltaTime;

    if (splash_time_ > 0.3f) {
        bool dismiss = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                       ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!dismiss) {
            const ImGuiKey keys[] = {
                ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Escape,
                ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E,
                ImGuiKey_F, ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J,
                ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N, ImGuiKey_O,
                ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T,
                ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y,
                ImGuiKey_Z, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
            };
            for (auto k : keys) {
                if (ImGui::IsKeyPressed(k)) { dismiss = true; break; }
            }
        }
        if (dismiss) {
            show_splash = false;
            return;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##BiochemSplash", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        float pulse = 0.7f + 0.3f * sinf(splash_time_ * 1.8f);
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 140.0f * pulse,
                         IM_COL32(40, 180, 80, 35), IM_COL32(20, 100, 40, 0));
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 70.0f * pulse,
                         IM_COL32(80, 220, 120, 50), IM_COL32(40, 160, 60, 0));

        float cell_r = 50.0f * pulse;
        dl->AddCircle(ImVec2(W * 0.5f, H * 0.4f), cell_r,
            IM_COL32(60, 200, 100, 120), 32, 2.0f);
        dl->AddCircleFilled(ImVec2(W * 0.5f, H * 0.4f), cell_r * 0.3f,
            IM_COL32(40, 120, 180, 80), 16);

        for (int i = 0; i < 5; i++) {
            float orbit_r = 25.0f + (float)i * 12.0f;
            float speed = 0.6f - (float)i * 0.08f;
            float angle = splash_time_ * speed + (float)i * 1.256f;
            float px = W * 0.5f + cosf(angle) * orbit_r;
            float py = H * 0.4f + sinf(angle) * orbit_r;
            ImU32 orga_col;
            if (i == 0) orga_col = IM_COL32(70, 160, 255, 180);
            else if (i == 1) orga_col = IM_COL32(230, 150, 50, 180);
            else if (i == 2) orga_col = IM_COL32(80, 220, 80, 180);
            else if (i == 3) orga_col = IM_COL32(220, 50, 50, 150);
            else orga_col = IM_COL32(200, 50, 200, 150);
            dl->AddCircleFilled(ImVec2(px, py), 3.0f + (float)i * 0.3f, orga_col, 8);
        }

        float title_scale = 2.4f;
        ImGui::SetWindowFontScale(title_scale);

        const char* title1 = "Biochemical ";
        const char* title2 = "Simulator";
        ImVec2 t1_size = ImGui::CalcTextSize(title1);
        float title_x = 60.0f;
        float title_y = H - 120.0f;

        for (int layer = 3; layer >= 0; layer--) {
            float offset = (float)layer * 1.5f;
            int lpha = 12 + layer * 6;
            dl->AddText(ImVec2(title_x - offset, title_y - offset),
                IM_COL32(40, 200, 80, lpha), title1);
            dl->AddText(ImVec2(title_x + t1_size.x - offset, title_y - offset),
                IM_COL32(20, 160, 60, lpha), title2);
        }

        dl->AddText(ImVec2(title_x, title_y), IM_COL32(100, 240, 140, 255), title1);
        dl->AddText(ImVec2(title_x + t1_size.x, title_y), IM_COL32(60, 200, 100, 255), title2);

        ImGui::SetWindowFontScale(1.0f);
        const char* badge = "CELLULAR BIOLOGY SANDBOX";
        ImVec2 badge_size = ImGui::CalcTextSize(badge);
        float badge_x = W - badge_size.x - 40.0f;
        float badge_y = 30.0f;
        float pad = 8.0f;
        dl->AddRect(ImVec2(badge_x - pad, badge_y - pad * 0.5f),
                    ImVec2(badge_x + badge_size.x + pad, badge_y + badge_size.y + pad * 0.5f),
                    IM_COL32(60, 200, 100, 200), 4.0f, 0, 1.5f);
        dl->AddText(ImVec2(badge_x, badge_y), IM_COL32(80, 220, 120, 230), badge);

        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = 120.0f + 80.0f * sinf(splash_time_ * 3.0f);
        dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 40.0f),
                    IM_COL32(180, 220, 190, (int)hint_alpha), hint);

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ── Pause menu ──────────────────────────────────────────────────────────────

void BiochemApp::draw_pause_menu() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.04f, 0.06f, 0.78f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##BiochemPause", nullptr, flags)) {
        float cx = W * 0.5f, cy = H * 0.5f;

        ImGui::SetWindowFontScale(2.0f);
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_y = cy - 160.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int layer = 2; layer >= 0; layer--) {
            float off = (float)layer * 2.0f;
            dl->AddText(ImVec2(cx - title_size.x * 0.5f - off, title_y - off),
                IM_COL32(60, 200, 100, 15 + layer * 10), title);
        }
        dl->AddText(ImVec2(cx - title_size.x * 0.5f, title_y),
            IM_COL32(80, 230, 120, 255), title);
        ImGui::SetWindowFontScale(1.0f);

        float btn_w = 200.0f, btn_h = 40.0f, btn_spacing = 52.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.16f, 0.14f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.14f, 0.24f, 0.20f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.20f, 0.32f, 0.28f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            paused = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            reset_simulation();
            show_pause_menu = false;
        }

        // Empty Simulation
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Empty Simulation", ImVec2(btn_w, btn_h))) {
            state.clear();
            cfg.entity_count = 0;
            selected_entity = -1;
            nutrient_timer_ = 0.0f;
            sim_time_ = 0.0f;
            show_pause_menu = false;
            paused = false;
        }

        // Return to Launcher
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Return to Launcher", ImVec2(btn_w, btn_h))) {
            request_launcher = true;
            request_quit = true;
        }

        // Quit — red tinted
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cx - hint_size.x * 0.5f, H - 60.0f),
            IM_COL32(140, 170, 150, 100), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Spawn menu ──────────────────────────────────────────────────────────────

void BiochemApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGui::SetNextWindowPos({10, 400}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 340}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 180), ImVec2(300, 500));

    if (!ImGui::Begin("Spawn Entities", &spawn_menu_visible_)) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Entity Type", ImGuiTreeNodeFlags_DefaultOpen)) {
        float btn_w = 110.0f;
        for (int t = 0; t < BIO_TYPE_COUNT; t++) {
            ImU32 col = TYPE_COLORS[t];
            float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

            bool selected = (spawn_bio_type_ == t);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.9f, 0.5f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.2f, g * 0.2f, b * 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.45f, g * 0.45f, b * 0.45f, 1.0f));
            }

            if (ImGui::Button(BIO_TYPE_NAMES[t], ImVec2(btn_w, 26))) {
                spawn_bio_type_ = t;
                spawn_variant_ = 0;
                if (t == BIO_CELL) spawn_energy_ = 100.0f;
                else if (t == BIO_BACTERIUM) spawn_energy_ = 60.0f;
                else if (t == BIO_VIRUS) spawn_energy_ = 30.0f;
                else if (t == BIO_NUTRIENT) spawn_energy_ = 25.0f;
                else if (t == BIO_TOXIN) spawn_energy_ = 50.0f;
                else if (t == BIO_ANTIBODY) spawn_energy_ = 80.0f;
                else if (t == BIO_RED_BLOOD) spawn_energy_ = 100.0f;
                else if (t == BIO_WHITE_BLOOD) spawn_energy_ = 200.0f;
            }

            if (selected) {
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
            } else {
                ImGui::PopStyleColor(3);
            }

            if (t % 2 == 0 && t < BIO_TYPE_COUNT - 1) ImGui::SameLine();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        uint32_t variant_count = type_variant_count((uint32_t)spawn_bio_type_);
        if (variant_count > 1) {
            int variant = spawn_variant_ % (int)variant_count;
            if (ImGui::BeginCombo("Variant", bio_entity_variant_name((uint32_t)spawn_bio_type_, (uint32_t)variant))) {
                for (uint32_t i = 0; i < variant_count; ++i) {
                    bool selected = (variant == (int)i);
                    if (ImGui::Selectable(bio_entity_variant_name((uint32_t)spawn_bio_type_, i), selected))
                        spawn_variant_ = (int)i;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SliderFloat("Energy", &spawn_energy_, 5.0f, 500.0f, "%.0f",
                            ImGuiSliderFlags_Logarithmic);
    }

    ImGui::Separator();

    {
        int t = spawn_bio_type_ % BIO_TYPE_COUNT;
        ImU32 col = TYPE_COLORS[t];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.65f, g * 0.65f, b * 0.65f, 1.0f));

        ImGui::TextColored(ImVec4(0.4f, 0.5f, 0.4f, 1.0f), "Middle-click in viewport to place");

        char label[64];
        snprintf(label, sizeof(label), "Spawn %s at Center", BIO_TYPE_NAMES[t]);
        if (ImGui::Button(label, ImVec2(-1, 34))) {
            spawn_at(camera.target);
        }
        ImGui::PopStyleColor(3);
    }

    if (ImGui::CollapsingHeader("Quick Presets")) {
        float wr = cfg.world_radius * 0.5f;
        auto randf = [](float lo, float hi) {
            return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
        };

        if (ImGui::Button("Add Cell Colony (10)", ImVec2(-1, 0))) {
            for (int i = 0; i < 10; i++) {
                BioEntity e;
                float angle = (float)i / 10.0f * 6.2832f;
                e.pos = {cosf(angle) * 40.0f, sinf(angle) * 40.0f, randf(-20, 20)};
                e.vel = {cosf(angle) * 5.0f, sinf(angle) * 5.0f, 0};
                e.type = BIO_CELL;
                e.morphology = i % BIO_CELL_VARIANT_COUNT;
                e.genome = (uint32_t)rand();
                std::mt19937 rng(e.genome ^ (uint32_t)i);
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_CELL) * e.genes.energy;
                configure_entity_shape(e, rng);
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Virus Outbreak (8)", ImVec2(-1, 0))) {
            for (int i = 0; i < 8; i++) {
                BioEntity e;
                float angle = (float)i / 8.0f * 6.2832f;
                float phi = randf(-0.5f, 0.5f);
                e.pos = {cosf(angle) * 60.0f, sinf(angle) * 60.0f, sinf(phi) * 40.0f};
                e.vel = {cosf(angle) * 30.0f, sinf(angle) * 30.0f, 0};
                e.type = BIO_VIRUS;
                e.morphology = i % BIO_VIRUS_VARIANT_COUNT;
                e.genome = (uint32_t)rand();
                std::mt19937 rng(e.genome ^ (uint32_t)(i * 13));
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_VIRUS) * e.genes.energy;
                configure_entity_shape(e, rng);
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Nutrient Burst (20)", ImVec2(-1, 0))) {
            for (int i = 0; i < 20; i++)
                spawn_nutrient();
        }

        if (ImGui::Button("Immune Response (5 WBC)", ImVec2(-1, 0))) {
            for (int i = 0; i < 5; i++) {
                BioEntity e;
                e.pos = {randf(-wr, wr), randf(-wr, wr), randf(-wr, wr)};
                e.type = BIO_WHITE_BLOOD;
                e.genome = (uint32_t)rand();
                std::mt19937 rng(e.genome ^ (uint32_t)(i * 31));
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_WHITE_BLOOD) * e.genes.energy;
                configure_entity_shape(e, rng);
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Reseed Current Environment", ImVec2(-1, 0))) {
            reset_simulation();
        }
    }

    ImGui::End();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void BiochemApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();

    bool any_overlay = show_splash || show_pause_menu;

    if (any_overlay)
        draw_menu_background();

    if (show_splash) {
        draw_splash_screen();
        return;
    }

    if (show_pause_menu) {
        draw_pause_menu();
        return;
    }

    // ── Normal UI ────────────────────────────────────────────────────────────

    // Top bar
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({io.DisplaySize.x, 36});
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Biochemical Simulator");
    ImGui::SameLine();
    ImGui::TextColored({0.35f, 0.75f, 0.85f, 1.0f}, "[%s]", BIO_ENVIRONMENT_NAMES[cfg.environment % BIO_ENV_COUNT]);
    ImGui::SameLine();
    ImGui::TextColored({0.5f, 0.5f, 0.6f, 1.0f},
        "  |  Entities: %zu alive", state.count_alive());
    ImGui::SameLine();
    ImGui::TextColored({0.45f, 0.60f, 0.70f, 1.0f}, "  Features: %zu", environment_.count());
    ImGui::SameLine();
    if (ImGui::SmallButton(paused ? "Resume" : "Pause"))
        paused = !paused;
    ImGui::SameLine();
    ImGui::TextColored({0.4f, 0.4f, 0.5f, 1.0f}, "  %.0f FPS", io.Framerate);
    ImGui::SameLine();
    ImGui::TextColored({0.3f, 0.3f, 0.4f, 1.0f},
        "  |  WASD: pan  Drag: orbit  Scroll: zoom  Middle: spawn  Right: select  Esc: menu");
    ImGui::End();

    // Settings panel
    if (settings_visible_) {
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300, 560}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Biochem Settings");
    const BioEnvironmentPreset& env = bio_environment_preset(static_cast<BioEnvironmentType>(cfg.environment));
    ImGui::Text("Environment");
    int environment = static_cast<int>(cfg.environment);
    if (ImGui::Combo("Biome", &environment, BIO_ENVIRONMENT_NAMES, BIO_ENV_COUNT))
        apply_environment_preset(static_cast<BioEnvironmentType>(environment), false);
    ImGui::TextWrapped("%s", env.summary);
    ImGui::Text("Seed: %u", cfg.environment_seed);
    if (ImGui::Button("Apply Preset + Reseed", ImVec2(-1, 0)))
        apply_environment_preset(static_cast<BioEnvironmentType>(cfg.environment), true);
    if (ImGui::Button("Regenerate Environment", ImVec2(-1, 0)))
        regenerate_environment(true);
    ImGui::Text("Generated: %zu features", environment_.count());
    ImGui::Text("Membranes %zu  Nutrients %zu  Toxins %zu  Currents %zu",
        environment_.count_type(BIO_ENV_FEATURE_MEMBRANE),
        environment_.count_type(BIO_ENV_FEATURE_NUTRIENT),
        environment_.count_type(BIO_ENV_FEATURE_TOXIN),
        environment_.count_type(BIO_ENV_FEATURE_CURRENT));
    ImGui::Separator();
    ImGui::Text("Environment Parameters");
    ImGui::SliderFloat("Temperature C",  &cfg.temperature_c,    4.0f, 42.0f, "%.1f");
    ImGui::SliderFloat("Acidity pH",     &cfg.acidity_ph,       5.0f, 8.4f, "%.2f");
    ImGui::SliderFloat("Oxygen Level",   &cfg.oxygen_level,     0.0f, 1.2f, "%.2f");
    ImGui::SliderFloat("Nutrient Density", &cfg.nutrient_density, 0.2f, 2.0f, "%.2f");
    ImGui::SliderFloat("Flow Strength",  &cfg.flow_strength,    0.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("Toxicity",       &cfg.toxicity,         0.0f, 0.5f, "%.2f");
    ImGui::SliderFloat("Immune Pressure", &cfg.immune_pressure, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Fluid Damping",  &cfg.fluid_damping,    0.92f, 0.995f, "%.3f");
    ImGui::SliderFloat("Nutrient Rate",   &cfg.nutrient_rate,   0.1f, 10.0f);
    ImGui::SliderFloat("Metabolism",       &cfg.metabolism_rate,  0.1f, 5.0f);
    ImGui::SliderFloat("Division Energy",  &cfg.division_energy,  50.0f, 300.0f);
    ImGui::SliderFloat("Mutation Rate",    &cfg.mutation_rate,    0.0f, 0.1f, "%.3f");
    ImGui::SliderFloat("Infection Radius", &cfg.infection_radius, 5.0f, 50.0f);
    ImGui::SliderFloat("Infection Rate",   &cfg.infection_rate,   0.1f, 2.0f);
    ImGui::SliderFloat("Immune Strength",  &cfg.immune_strength,  0.1f, 5.0f);
    ImGui::SliderFloat("Viscosity",        &cfg.viscosity,        0.90f, 1.0f, "%.3f");
    ImGui::SliderFloat("Time Scale",       &cfg.dt_scale,         0.1f, 5.0f);
    ImGui::Checkbox("Immune System",       &cfg.immune_system);
    ImGui::Checkbox("Energy Bars",         &cfg.show_energy_bars);
    ImGui::Separator();
    ImGui::Text("AI Movement");
    ImGui::Checkbox("Enable AI", &cfg.ai_movement);
    if (cfg.ai_movement) {
        ImGui::SliderFloat("Seek",     &cfg.seek_strength,     0.0f, 100.0f);
        ImGui::SliderFloat("Flee",     &cfg.flee_strength,     0.0f, 100.0f);
        ImGui::SliderFloat("Spacing",  &cfg.spacing_strength,  0.0f, 50.0f);
        ImGui::SliderFloat("Brownian", &cfg.brownian_strength, 0.0f, 50.0f);
    }
    ImGui::Separator();
    ImGui::Text("World");
    ImGui::SliderFloat("World Radius",     &cfg.world_radius,     50.0f, 500.0f);
    ImGui::SliderFloat("Ambient",          &cfg.ambient_strength,  0.0f, 0.5f);
    if (ImGui::Button("Reset Camera")) {
        reset_camera_pose();
    }
    ImGui::End();
    } // settings_visible_

    // Shared arrays for population + inspector
    static const char* pop_type_names[] = {
        "Cells", "Bacteria", "Viruses", "Nutrients",
        "Toxins", "Antibodies", "Red Blood", "White Blood"
    };
    static const ImVec4 type_colors_v[] = {
        {0.3f,0.7f,1.0f,1}, {0.9f,0.6f,0.2f,1}, {0.9f,0.2f,0.2f,1}, {0.3f,0.9f,0.3f,1},
        {0.8f,0.2f,0.8f,1}, {1.0f,1.0f,0.3f,1}, {0.9f,0.3f,0.3f,1}, {1.0f,1.0f,1.0f,1}
    };

    // Population stats
    if (population_visible_) {
    ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210, 220}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Population");
    size_t total_alive = 0;
    for (int t = 0; t < BIO_TYPE_COUNT; t++) {
        size_t n = state.count_type(static_cast<BioEntityType>(t));
        total_alive += n;
        if (n > 0)
            ImGui::TextColored(type_colors_v[t], "%s: %zu", pop_type_names[t], n);
    }
    ImGui::Separator();
    ImGui::Text("Total: %zu", total_alive);
    ImGui::End();
    } // population_visible_

    // Entity inspector
    if (selected_entity >= 0 && selected_entity < (int)state.entities.size()) {
        const auto& e = state.entities[selected_entity];
        if (e.alive) {
            ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 280}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({240, 340}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Entity Inspector");

            ImGui::TextColored(type_colors_v[e.type % BIO_TYPE_COUNT],
                "%s #%d", pop_type_names[e.type % BIO_TYPE_COUNT], selected_entity);
            if (e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_VIRUS)
                ImGui::Text("%s", bio_entity_variant_name(e.type, e.morphology));
            ImGui::Separator();
            ImGui::Text("Energy:  %.1f", e.energy);
            ImGui::Text("Age:     %.1f s", e.age);
            ImGui::Text("Radius:  %.1f", e.radius);
            ImGui::Text("Aspect:  %.2f", e.shape_aspect);
            ImGui::Text("Speed:   %.1f", glm::length(e.vel));
            ImGui::Text("Stress:  %.2f", compute_environment_stress(cfg, environment_, e));
            ImGui::Text("Pos:     (%.0f, %.0f, %.0f)", e.pos.x, e.pos.y, e.pos.z);
            ImGui::Text("Genome:  %08X", e.genome);
            ImGui::Separator();
            ImGui::Text("Genes");
            ImGui::Text("Seek:    %.2f", e.genes.seek);
            ImGui::Text("Flee:    %.2f", e.genes.flee);
            ImGui::Text("Spacing: %.2f", e.genes.spacing);
            ImGui::Text("Brown:   %.2f", e.genes.brownian);
            ImGui::Text("Energy:  %.2f", e.genes.energy);
            ImGui::Spacing();
            if (ImGui::Button("Kill")) {
                state.entities[selected_entity].alive = false;
                selected_entity = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect"))
                selected_entity = -1;

            ImGui::End();
        } else {
            selected_entity = -1;
        }
    }

    if (spawn_menu_visible_)
        draw_spawn_menu();

    draw_bottom_bar();
}

// ── Bottom bar (auto-hiding, green/bio theme) ───────────────────────────────

void BiochemApp::draw_bottom_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float bar_h = 36.0f;

    // Auto-hide: show when mouse near bottom
    float mouse_y = io.MousePos.y;
    bool want_show = (mouse_y > H - 50.0f) && (mouse_y <= H);
    float target = want_show ? 0.0f : 1.0f;
    bottom_bar_offset_ += (target - bottom_bar_offset_) * std::min(1.0f, 8.0f * io.DeltaTime);
    if (bottom_bar_offset_ > 0.99f && !want_show) return;

    float bar_y = H - bar_h * (1.0f - bottom_bar_offset_);

    ImGui::SetNextWindowPos(ImVec2(0, bar_y));
    ImGui::SetNextWindowSize(ImVec2(W, bar_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.08f, 0.04f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.50f, 0.25f, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##BioBottomBar", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Subtle top border glow
        dl->AddLine(ImVec2(0, bar_y), ImVec2(W, bar_y),
                    IM_COL32(40, 180, 80, 100), 1.0f);

        // ── Left: Menu button ──
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.20f, 0.12f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.30f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.40f, 0.25f, 1.0f));

        if (ImGui::Button("Menu", ImVec2(60, 28)))
            show_menu_popup_ = !show_menu_popup_;

        ImGui::PopStyleColor(3);

        // Menu popup
        if (show_menu_popup_) {
            ImGui::SetNextWindowPos(ImVec2(4, bar_y - 200));
            ImGui::SetNextWindowSize(ImVec2(220, 196));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.12f, 0.06f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.55f, 0.30f, 0.7f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

            if (ImGui::Begin("##BioMenuPopup", &show_menu_popup_,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove)) {

                ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.45f, 0.7f), "SIMULATION");
                ImGui::Separator();
                if (ImGui::MenuItem(paused ? "Resume" : "Pause"))
                    paused = !paused;
                if (ImGui::MenuItem("New Simulation")) {
                    reset_simulation();
                    show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Empty Simulation")) {
                    state.clear();
                    cfg.entity_count = 0;
                    selected_entity = -1;
                    nutrient_timer_ = 0.0f;
                    sim_time_ = 0.0f;
                    show_menu_popup_ = false;
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.45f, 0.7f), "VIEW");
                ImGui::Separator();
                ImGui::MenuItem("Settings Panel", nullptr, &settings_visible_);
                ImGui::MenuItem("Population Panel", nullptr, &population_visible_);
                ImGui::MenuItem("Spawn Menu", nullptr, &spawn_menu_visible_);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.45f, 0.7f), "NAVIGATION");
                ImGui::Separator();
                if (ImGui::MenuItem("Return to Launcher")) {
                    request_launcher = true;
                    request_quit = true;
                }
                if (ImGui::MenuItem("Quit"))
                    request_quit = true;
            }
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        // ── Center: Taskbar buttons ──
        float center_start = W * 0.5f - 150.0f;
        ImGui::SameLine(center_start);

        struct TaskBtn { const char* label; bool* vis; };
        TaskBtn btns[] = {
            {"Settings",   &settings_visible_},
            {"Spawn",      &spawn_menu_visible_},
            {"Population", &population_visible_},
        };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.25f, 0.14f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.35f, 0.20f, 0.8f));

        for (auto& btn : btns) {
            ImVec2 before = ImGui::GetCursorScreenPos();
            if (ImGui::Button(btn.label, ImVec2(90, 28)))
                *btn.vis = !*btn.vis;

            // Green underline when active
            if (*btn.vis) {
                ImVec2 after = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(before.x + 4, before.y + 27),
                            ImVec2(before.x + 86, before.y + 27),
                            IM_COL32(60, 220, 100, 200), 2.0f);
            }
            ImGui::SameLine();
        }

        ImGui::PopStyleColor(3);

        // ── Right: Alive count ──
        size_t alive = state.count_alive();
        size_t total = state.entities.size();
        char info[64];
        snprintf(info, sizeof(info), "Alive: %zu / %zu", alive, total);
        ImVec2 info_size = ImGui::CalcTextSize(info);
        ImGui::SameLine(W - info_size.x - 16.0f);
        ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.50f, 0.9f), "%s", info);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}
