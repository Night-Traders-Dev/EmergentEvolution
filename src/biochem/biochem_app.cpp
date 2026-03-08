#include "biochem/biochem_app.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>

// ── Entity type colors / names ──────────────────────────────────────────────

static const char* const BIO_TYPE_NAMES[] = {
    "Cell", "Bacterium", "Virus", "Nutrient",
    "Toxin", "Antibody", "Red Blood Cell", "White Blood Cell", "Phagocyte"
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
    IM_COL32(120, 220, 180, 255),  // Phagocyte - seafoam
};

static const char* const BIO_ENVIRONMENT_NAMES[] = {
    "Human Lung",
    "Pond Water",
    "Petri Dish",
    "Cat Brain",
    "Gut Microbiome",
    "Blood Stream",
    "Soil Rhizosphere",
    "Wound Site"
};

enum BioEventType : uint32_t {
    BIO_EVENT_SYSTEM = 0,
    BIO_EVENT_DIVISION,
    BIO_EVENT_INFECTION,
    BIO_EVENT_IMMUNE,
    BIO_EVENT_LIFECYCLE,
    BIO_EVENT_USER
};

static const char* bio_event_type_name(uint32_t type) {
    switch (type) {
    case BIO_EVENT_DIVISION:  return "Division";
    case BIO_EVENT_INFECTION: return "Infection";
    case BIO_EVENT_IMMUNE:    return "Immune";
    case BIO_EVENT_LIFECYCLE: return "Lifecycle";
    case BIO_EVENT_USER:      return "User";
    default:                  return "System";
    }
}

static ImVec4 bio_event_type_color(uint32_t type) {
    switch (type) {
    case BIO_EVENT_DIVISION:  return {0.90f, 0.62f, 0.98f, 1.0f};
    case BIO_EVENT_INFECTION: return {1.00f, 0.44f, 0.44f, 1.0f};
    case BIO_EVENT_IMMUNE:    return {0.98f, 0.94f, 0.46f, 1.0f};
    case BIO_EVENT_LIFECYCLE: return {0.62f, 0.92f, 0.70f, 1.0f};
    case BIO_EVENT_USER:      return {0.60f, 0.88f, 1.0f, 1.0f};
    default:                  return {0.64f, 0.96f, 0.72f, 1.0f};
    }
}

static std::string bio_entity_label(const BioEntity& e) {
    const char* base = BIO_TYPE_NAMES[e.type % BIO_TYPE_COUNT];
    if (e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_VIRUS)
        base = bio_entity_variant_name(e.type, e.morphology);
    char label[160];
    std::snprintf(label, sizeof(label), "%s #%u", base, e.entity_id);
    return std::string(label);
}

static uint32_t type_variant_count(uint32_t type);
static bool type_uses_telomeres(uint32_t type);

static const char* bio_lifecycle_stage_name(const BioEntity& e) {
    if (e.corpse) return "Dead Husk";
    if ((type_uses_telomeres(e.type) && e.telomere_state <= 0.08f) || e.organelle_health <= 0.20f) return "Senescent";
    if (e.organelle_health <= 0.55f || (type_uses_telomeres(e.type) && e.telomere_state <= 0.42f)) return "Aging";
    if (e.age < 18.0f) return "Young";
    return "Mature";
}

enum BioPathogenEntryMode : uint32_t {
    BIO_ENTRY_NONE = 0,
    BIO_ENTRY_RECEPTOR_ENDOCYTOSIS,
    BIO_ENTRY_MEMBRANE_FUSION,
    BIO_ENTRY_SIALIC_ENDOCYTOSIS,
    BIO_ENTRY_GENOME_INJECTION,
    BIO_ENTRY_FIMBRIAL_ADHESION,
    BIO_ENTRY_BIOFILM_COLONIZATION,
    BIO_ENTRY_TOXIN_MEDIATED
};

struct CellSubtypeTraits {
    const char* lineage;
    uint32_t exchange_group;
    float infection_susceptibility;
};

struct BacteriaSubtypeTraits {
    const char* lineage;
    const char* infection_mode;
    BioPathogenEntryMode entry_mode;
    uint32_t exchange_group;
    float colonization_rate;
    float antibiotic_bonus;
    uint32_t preferred_host_subtype;
    float host_tropism_bonus;
};

struct VirusSubtypeTraits {
    const char* lineage;
    const char* receptor;
    BioPathogenEntryMode entry_mode;
    uint32_t exchange_group;
    uint32_t preferred_host_type;
    uint32_t preferred_host_subtype;
    float infection_multiplier;
    float host_tropism_bonus;
};

static const CellSubtypeTraits& cell_traits(uint32_t morphology) {
    static const CellSubtypeTraits traits[BIO_CELL_VARIANT_COUNT] = {
        {"Animal lineage", 1u, 1.00f},
        {"Alveolar epithelium", 2u, 1.22f},
        {"Airway epithelium", 2u, 1.28f},
        {"Absorptive epithelium", 2u, 0.92f},
        {"Neural lineage", 3u, 0.72f},
        {"Glial lineage", 3u, 0.88f},
        {"Connective tissue lineage", 4u, 0.86f},
        {"Amoeboid lineage", 5u, 0.82f},
    };
    return traits[morphology % BIO_CELL_VARIANT_COUNT];
}

static const BacteriaSubtypeTraits& bacteria_traits(uint32_t morphology) {
    static const BacteriaSubtypeTraits traits[BIO_BACTERIA_VARIANT_COUNT] = {
        {"Gram-positive cocci", "adhesin-driven biofilm colonization", BIO_ENTRY_BIOFILM_COLONIZATION, 10u, 0.82f, 1.10f, BIO_CELL_GENERIC_ANIMAL, 0.16f},
        {"Encapsulated cocci", "capsular adhesion and mucosal invasion", BIO_ENTRY_FIMBRIAL_ADHESION, 10u, 0.92f, 0.96f, BIO_CELL_CILIATED_EPITHELIAL, 0.28f},
        {"Enteric bacilli", "fimbrial adhesion", BIO_ENTRY_FIMBRIAL_ADHESION, 11u, 0.94f, 0.90f, BIO_CELL_ENTEROCYTE, 0.26f},
        {"Opportunistic bacilli", "biofilm colonization", BIO_ENTRY_BIOFILM_COLONIZATION, 11u, 0.88f, 1.18f, BIO_CELL_TYPE_II_PNEUMOCYTE, 0.22f},
        {"Spore-forming bacilli", "spore persistence", BIO_ENTRY_NONE, 12u, 0.58f, 0.82f, BIO_CELL_GENERIC_ANIMAL, 0.08f},
        {"Curved vibrios", "toxin-mediated colonization", BIO_ENTRY_TOXIN_MEDIATED, 13u, 1.06f, 0.72f, BIO_CELL_ENTEROCYTE, 0.32f},
    };
    return traits[morphology % BIO_BACTERIA_VARIANT_COUNT];
}

static const VirusSubtypeTraits& virus_traits(uint32_t morphology) {
    static const VirusSubtypeTraits traits[BIO_VIRUS_VARIANT_COUNT] = {
        {"Mastadenovirus", "CAR/integrin-mediated endocytosis", BIO_ENTRY_RECEPTOR_ENDOCYTOSIS,
         20u, BIO_CELL, BIO_CELL_CILIATED_EPITHELIAL, 0.86f, 0.18f},
        {"Betacoronavirus", "ACE2-mediated fusion/endocytosis", BIO_ENTRY_MEMBRANE_FUSION,
         21u, BIO_CELL, BIO_CELL_TYPE_II_PNEUMOCYTE, 1.18f, 0.34f},
        {"Orthomyxovirus", "sialic-acid endocytosis", BIO_ENTRY_SIALIC_ENDOCYTOSIS,
         22u, BIO_CELL, BIO_CELL_CILIATED_EPITHELIAL, 1.02f, 0.24f},
        {"Orthomyxovirus", "sialic-acid endocytosis", BIO_ENTRY_SIALIC_ENDOCYTOSIS,
         22u, BIO_CELL, BIO_CELL_CILIATED_EPITHELIAL, 0.94f, 0.18f},
        {"Tequatrovirus", "tail-fiber adsorption and genome injection", BIO_ENTRY_GENOME_INJECTION,
         23u, BIO_BACTERIUM, BIO_BACTERIA_ESCHERICHIA_COLI, 1.12f, 0.42f},
    };
    return traits[morphology % BIO_VIRUS_VARIANT_COUNT];
}

static uint64_t type_subtype_key(uint32_t type, uint32_t morphology) {
    return (static_cast<uint64_t>(type) << 32) | static_cast<uint64_t>(morphology);
}

static uint32_t base_species_key(uint32_t type, uint32_t morphology) {
    return 1u + type * 256u + (morphology % std::max(1u, type_variant_count(type)));
}

static uint32_t subtype_exchange_group(uint32_t type, uint32_t morphology) {
    switch (type) {
    case BIO_CELL:      return cell_traits(morphology).exchange_group;
    case BIO_BACTERIUM: return bacteria_traits(morphology).exchange_group;
    case BIO_VIRUS:     return virus_traits(morphology).exchange_group;
    default:            return 0u;
    }
}

static bool type_supports_gene_exchange(uint32_t type) {
    return type == BIO_BACTERIUM;
}

static void blend_gene_block(BioGenes& dst, const BioGenes& src, float weight) {
    dst.seek += (src.seek - dst.seek) * weight;
    dst.flee += (src.flee - dst.flee) * weight;
    dst.spacing += (src.spacing - dst.spacing) * weight;
    dst.brownian += (src.brownian - dst.brownian) * weight;
    dst.energy += (src.energy - dst.energy) * weight;
    dst.telomere += (src.telomere - dst.telomere) * weight;
    dst.mitotic_clock += (src.mitotic_clock - dst.mitotic_clock) * weight;
    dst.metabolism_efficiency += (src.metabolism_efficiency - dst.metabolism_efficiency) * weight;
    dst.nutrient_affinity += (src.nutrient_affinity - dst.nutrient_affinity) * weight;
    dst.stress_tolerance += (src.stress_tolerance - dst.stress_tolerance) * weight;
    dst.defense += (src.defense - dst.defense) * weight;
    dst.sensing += (src.sensing - dst.sensing) * weight;
    dst.mutation_stability += (src.mutation_stability - dst.mutation_stability) * weight;
    dst.antibiotic_type += (src.antibiotic_type - dst.antibiotic_type) * weight;
    dst.antibiotic_yield += (src.antibiotic_yield - dst.antibiotic_yield) * weight;
    dst.antibiotic_diversity += (src.antibiotic_diversity - dst.antibiotic_diversity) * weight;
    dst.resistance += (src.resistance - dst.resistance) * weight;
    dst.quorum_threshold += (src.quorum_threshold - dst.quorum_threshold) * weight;
}

static bool virus_targets_host(const BioEntity& virus, const BioEntity& host) {
    const auto& traits = virus_traits(virus.morphology);
    if (host.type != traits.preferred_host_type)
        return false;
    if (traits.preferred_host_type == BIO_BACTERIUM)
        return host.morphology % BIO_BACTERIA_VARIANT_COUNT == traits.preferred_host_subtype;
    return true;
}

static float virus_host_tropism(const BioEntity& virus, const BioEntity& host) {
    const auto& traits = virus_traits(virus.morphology);
    float tropism = traits.infection_multiplier;
    if (!virus_targets_host(virus, host))
        return 0.0f;
    if (host.type == BIO_CELL) {
        tropism *= cell_traits(host.morphology).infection_susceptibility;
        if (host.morphology % BIO_CELL_VARIANT_COUNT == traits.preferred_host_subtype)
            tropism *= 1.0f + traits.host_tropism_bonus;
    } else if (host.type == BIO_BACTERIUM) {
        if (host.morphology % BIO_BACTERIA_VARIANT_COUNT == traits.preferred_host_subtype)
            tropism *= 1.0f + traits.host_tropism_bonus;
        tropism *= bacteria_traits(host.morphology).colonization_rate;
    }
    return tropism;
}

static bool entity_has_active_infection(const BioEntity& e) {
    return e.viral_infection.progress > 0.0f || e.bacterial_infection.progress > 0.0f;
}

static bool entity_has_viral_infection(const BioEntity& e) {
    return e.viral_infection.progress > 0.0f;
}

static bool entity_has_bacterial_infection(const BioEntity& e) {
    return e.bacterial_infection.progress > 0.0f;
}

static float dominant_infection_progress(const BioEntity& e) {
    return std::max(e.viral_infection.progress, e.bacterial_infection.progress);
}

static float combined_infection_load(const BioEntity& e) {
    return e.viral_infection.load + e.bacterial_infection.load;
}

static const BioPathogenState* dominant_infection_state(const BioEntity& e) {
    bool viral = entity_has_viral_infection(e);
    bool bacterial = entity_has_bacterial_infection(e);
    if (!viral && !bacterial)
        return nullptr;
    if (viral && !bacterial)
        return &e.viral_infection;
    if (!viral && bacterial)
        return &e.bacterial_infection;
    return (e.viral_infection.progress >= e.bacterial_infection.progress)
        ? &e.viral_infection
        : &e.bacterial_infection;
}

static std::string infection_variant_name(const BioEntity& e) {
    bool viral = entity_has_viral_infection(e);
    bool bacterial = entity_has_bacterial_infection(e);
    if (viral && bacterial) {
        return std::string(bio_entity_variant_name(BIO_VIRUS, e.viral_infection.morphology)) +
               " + " +
               bio_entity_variant_name(BIO_BACTERIUM, e.bacterial_infection.morphology);
    }
    if (viral)
        return bio_entity_variant_name(BIO_VIRUS, e.viral_infection.morphology);
    if (bacterial)
        return bio_entity_variant_name(BIO_BACTERIUM, e.bacterial_infection.morphology);
    return "Unknown pathogen";
}

static std::string infection_mechanism_name(const BioEntity& e) {
    bool viral = entity_has_viral_infection(e);
    bool bacterial = entity_has_bacterial_infection(e);
    if (viral && bacterial) {
        return std::string(virus_traits(e.viral_infection.morphology).receptor) +
               " + " +
               bacteria_traits(e.bacterial_infection.morphology).infection_mode;
    }
    if (viral)
        return virus_traits(e.viral_infection.morphology).receptor;
    if (bacterial)
        return bacteria_traits(e.bacterial_infection.morphology).infection_mode;
    return "unspecified infection pathway";
}

static float pathogen_entry_bias(BioPathogenEntryMode mode) {
    switch (mode) {
    case BIO_ENTRY_MEMBRANE_FUSION:       return 1.16f;
    case BIO_ENTRY_SIALIC_ENDOCYTOSIS:    return 1.08f;
    case BIO_ENTRY_GENOME_INJECTION:      return 1.18f;
    case BIO_ENTRY_FIMBRIAL_ADHESION:     return 1.06f;
    case BIO_ENTRY_BIOFILM_COLONIZATION:  return 0.96f;
    case BIO_ENTRY_TOXIN_MEDIATED:        return 1.12f;
    case BIO_ENTRY_RECEPTOR_ENDOCYTOSIS:  return 1.00f;
    case BIO_ENTRY_NONE:
    default:                              return 0.78f;
    }
}

static float bacteria_host_tropism(const BioEntity& bacterium, const BioEntity& host) {
    if (host.type != BIO_CELL)
        return 0.0f;
    const auto& traits = bacteria_traits(bacterium.morphology);
    float tropism = traits.colonization_rate * pathogen_entry_bias(traits.entry_mode);
    tropism *= cell_traits(host.morphology).infection_susceptibility;
    if (host.morphology % BIO_CELL_VARIANT_COUNT == traits.preferred_host_subtype)
        tropism *= 1.0f + traits.host_tropism_bonus;
    return std::clamp(tropism, 0.0f, 2.75f);
}

static std::string format_generation_set(const std::set<uint32_t>& generations) {
    if (generations.empty())
        return "none";
    std::ostringstream out;
    auto it = generations.begin();
    while (it != generations.end()) {
        uint32_t start = *it;
        uint32_t end = start;
        auto next = std::next(it);
        while (next != generations.end() && *next == end + 1) {
            end = *next;
            ++next;
        }
        if (out.tellp() > 0)
            out << ", ";
        if (start == end)
            out << start;
        else
            out << start << "-" << end;
        it = next;
    }
    return out.str();
}

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

static std::mt19937& global_rng() {
    static std::mt19937 rng(0xC0FFEE11u);
    return rng;
}

static uint32_t rand_u32() {
    return global_rng()();
}

static float randf_range(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(global_rng());
}

static float type_default_radius(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return 12.0f;
    case BIO_BACTERIUM:   return 6.0f;
    case BIO_VIRUS:       return 2.6f;
    case BIO_NUTRIENT:    return 3.0f;
    case BIO_TOXIN:       return 4.0f;
    case BIO_ANTIBODY:    return 5.0f;
    case BIO_RED_BLOOD:   return 5.0f;
    case BIO_WHITE_BLOOD: return 12.0f;
    case BIO_JANITOR:     return 11.0f;
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
    case BIO_JANITOR:     return 160.0f;
    default:              return 100.0f;
    }
}

static BioGenes type_gene_baseline(uint32_t type) {
    switch (type) {
    case BIO_CELL:        return {1.00f, 1.00f, 1.05f, 0.80f, 1.00f, 1.00f, 1.00f, 0.96f, 0.94f, 0.96f, 1.02f, 0.92f, 1.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    case BIO_BACTERIUM:   return {1.15f, 0.80f, 0.85f, 1.30f, 0.90f, 0.00f, 1.30f, 1.18f, 1.22f, 1.08f, 0.82f, 1.10f, 0.88f, 0.50f, 0.90f, 0.50f, 0.15f, 0.50f};
    case BIO_VIRUS:       return {1.20f, 0.30f, 0.55f, 0.55f, 0.75f, 0.00f, 0.40f, 0.92f, 0.70f, 0.92f, 1.10f, 1.05f, 0.76f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    case BIO_ANTIBODY:    return {1.10f, 0.40f, 0.90f, 0.45f, 0.95f, 0.00f, 0.65f, 1.02f, 0.72f, 1.08f, 1.16f, 1.08f, 1.06f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    case BIO_RED_BLOOD:   return {0.10f, 0.05f, 0.60f, 1.10f, 1.05f, 0.00f, 0.55f, 1.12f, 0.88f, 1.04f, 0.98f, 0.68f, 1.04f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    case BIO_WHITE_BLOOD: return {1.30f, 0.75f, 1.10f, 0.70f, 1.20f, 0.82f, 0.78f, 0.98f, 0.90f, 1.18f, 1.22f, 1.18f, 1.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    case BIO_JANITOR:     return {1.18f, 0.28f, 0.95f, 0.55f, 1.10f, 0.00f, 0.72f, 1.08f, 0.98f, 1.14f, 1.12f, 1.06f, 1.04f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
    default:              return {};
    }
}

static BioGenes zero_genes() {
    BioGenes genes;
    genes.seek = 0.0f;
    genes.flee = 0.0f;
    genes.spacing = 0.0f;
    genes.brownian = 0.0f;
    genes.energy = 0.0f;
    genes.telomere = 0.0f;
    genes.mitotic_clock = 0.0f;
    genes.metabolism_efficiency = 0.0f;
    genes.nutrient_affinity = 0.0f;
    genes.stress_tolerance = 0.0f;
    genes.defense = 0.0f;
    genes.sensing = 0.0f;
    genes.mutation_stability = 0.0f;
    genes.antibiotic_type = 0.0f;
    genes.antibiotic_yield = 0.0f;
    genes.antibiotic_diversity = 0.0f;
    genes.resistance = 0.0f;
    genes.quorum_threshold = 0.0f;
    return genes;
}

struct BioGeneAggregate {
    BioGenes sum = zero_genes();
    uint32_t count = 0;
};

static bool type_has_genomic_panel(uint32_t type) {
    return type != BIO_NUTRIENT && type != BIO_TOXIN;
}

static void accumulate_gene_aggregate(BioGeneAggregate& agg, const BioGenes& genes) {
    agg.sum.seek += genes.seek;
    agg.sum.flee += genes.flee;
    agg.sum.spacing += genes.spacing;
    agg.sum.brownian += genes.brownian;
    agg.sum.energy += genes.energy;
    agg.sum.telomere += genes.telomere;
    agg.sum.mitotic_clock += genes.mitotic_clock;
    agg.sum.metabolism_efficiency += genes.metabolism_efficiency;
    agg.sum.nutrient_affinity += genes.nutrient_affinity;
    agg.sum.stress_tolerance += genes.stress_tolerance;
    agg.sum.defense += genes.defense;
    agg.sum.sensing += genes.sensing;
    agg.sum.mutation_stability += genes.mutation_stability;
    agg.sum.antibiotic_type += genes.antibiotic_type;
    agg.sum.antibiotic_yield += genes.antibiotic_yield;
    agg.sum.antibiotic_diversity += genes.antibiotic_diversity;
    agg.sum.resistance += genes.resistance;
    agg.sum.quorum_threshold += genes.quorum_threshold;
    agg.count += 1;
}

static BioGenes average_genes(const BioGeneAggregate& agg) {
    if (agg.count == 0)
        return zero_genes();
    float inv = 1.0f / static_cast<float>(agg.count);
    BioGenes avg = agg.sum;
    avg.seek *= inv;
    avg.flee *= inv;
    avg.spacing *= inv;
    avg.brownian *= inv;
    avg.energy *= inv;
    avg.telomere *= inv;
    avg.mitotic_clock *= inv;
    avg.metabolism_efficiency *= inv;
    avg.nutrient_affinity *= inv;
    avg.stress_tolerance *= inv;
    avg.defense *= inv;
    avg.sensing *= inv;
    avg.mutation_stability *= inv;
    avg.antibiotic_type *= inv;
    avg.antibiotic_yield *= inv;
    avg.antibiotic_diversity *= inv;
    avg.resistance *= inv;
    avg.quorum_threshold *= inv;
    return avg;
}

static const char* gene_trait_name(int idx) {
    static const char* names[] = {
        "Seek", "Flee", "Spacing", "Brownian", "Energy",
        "Telomere", "Mitotic Clock", "Metabolism", "Nutrient Affinity",
        "Stress Tolerance", "Defense", "Sensing", "Mutation Stability",
        "Antibiotic Type", "Antibiotic Yield", "Antibiotic Diversity",
        "Resistance", "Quorum Threshold"
    };
    return names[idx];
}

static float gene_trait_value(const BioGenes& genes, int idx) {
    switch (idx) {
    case 0: return genes.seek;
    case 1: return genes.flee;
    case 2: return genes.spacing;
    case 3: return genes.brownian;
    case 4: return genes.energy;
    case 5: return genes.telomere;
    case 6: return genes.mitotic_clock;
    case 7: return genes.metabolism_efficiency;
    case 8: return genes.nutrient_affinity;
    case 9: return genes.stress_tolerance;
    case 10: return genes.defense;
    case 11: return genes.sensing;
    case 12: return genes.mutation_stability;
    case 13: return genes.antibiotic_type;
    case 14: return genes.antibiotic_yield;
    case 15: return genes.antibiotic_diversity;
    case 16: return genes.resistance;
    case 17: return genes.quorum_threshold;
    default: return 0.0f;
    }
}

static bool gene_trait_applicable(uint32_t type, int idx) {
    if (idx >= 13)
        return type == BIO_BACTERIUM;
    if (idx == 5)
        return type == BIO_CELL || type == BIO_WHITE_BLOOD;
    if (idx == 7 || idx == 8)
        return type == BIO_CELL || type == BIO_BACTERIUM || type == BIO_RED_BLOOD ||
               type == BIO_WHITE_BLOOD || type == BIO_ANTIBODY || type == BIO_JANITOR;
    if (idx == 12)
        return type == BIO_CELL || type == BIO_BACTERIUM || type == BIO_VIRUS ||
               type == BIO_WHITE_BLOOD || type == BIO_JANITOR;
    return true;
}

static std::string dominant_traits_summary(uint32_t type, const BioGeneAggregate& agg, int limit = 3) {
    if (agg.count == 0)
        return "none";

    struct RankedTrait {
        const char* name = "";
        float score = 0.0f;
        float value = 0.0f;
        float baseline = 0.0f;
    };

    BioGenes avg = average_genes(agg);
    BioGenes baseline = type_gene_baseline(type);
    std::array<RankedTrait, BIO_GENE_TRAIT_COUNT> ranked{};
    size_t used = 0;
    for (int idx = 0; idx < BIO_GENE_TRAIT_COUNT; ++idx) {
        if (!gene_trait_applicable(type, idx))
            continue;
        float value = gene_trait_value(avg, idx);
        float base = gene_trait_value(baseline, idx);
        float score = (base > 0.001f) ? std::abs(value - base) / base : std::abs(value - base);
        if (score < (base > 0.001f ? 0.06f : 0.10f))
            continue;
        ranked[used++] = {gene_trait_name(idx), score, value, base};
    }
    if (used == 0)
        return "baseline-like";

    std::sort(ranked.begin(), ranked.begin() + used,
              [](const RankedTrait& a, const RankedTrait& b) { return a.score > b.score; });

    std::ostringstream out;
    size_t shown = std::min<size_t>(static_cast<size_t>(limit), used);
    for (size_t i = 0; i < shown; ++i) {
        if (i > 0)
            out << ", ";
        if (ranked[i].baseline > 0.001f) {
            out << ranked[i].name << ' ' << (ranked[i].value >= ranked[i].baseline ? "high" : "low")
                << " x" << std::fixed << std::setprecision(2)
                << (ranked[i].value / ranked[i].baseline);
        } else {
            out << ranked[i].name << ' ' << std::fixed << std::setprecision(2) << ranked[i].value;
        }
    }
    return out.str();
}

static void clamp_genes(BioGenes& genes) {
    genes.seek = std::clamp(genes.seek, 0.05f, 2.50f);
    genes.flee = std::clamp(genes.flee, 0.05f, 2.50f);
    genes.spacing = std::clamp(genes.spacing, 0.05f, 2.50f);
    genes.brownian = std::clamp(genes.brownian, 0.00f, 2.50f);
    genes.energy = std::clamp(genes.energy, 0.35f, 2.25f);
    genes.telomere = std::clamp(genes.telomere, 0.00f, 2.25f);
    genes.mitotic_clock = std::clamp(genes.mitotic_clock, 0.25f, 2.50f);
    genes.metabolism_efficiency = std::clamp(genes.metabolism_efficiency, 0.35f, 2.25f);
    genes.nutrient_affinity = std::clamp(genes.nutrient_affinity, 0.25f, 2.50f);
    genes.stress_tolerance = std::clamp(genes.stress_tolerance, 0.35f, 2.50f);
    genes.defense = std::clamp(genes.defense, 0.35f, 2.50f);
    genes.sensing = std::clamp(genes.sensing, 0.20f, 2.50f);
    genes.mutation_stability = std::clamp(genes.mutation_stability, 0.25f, 2.50f);
    genes.antibiotic_type = std::clamp(genes.antibiotic_type, 0.00f, 1.00f);
    genes.antibiotic_yield = std::clamp(genes.antibiotic_yield, 0.00f, 2.50f);
    genes.antibiotic_diversity = std::clamp(genes.antibiotic_diversity, 0.00f, 1.00f);
    genes.resistance = std::clamp(genes.resistance, 0.00f, 2.50f);
    genes.quorum_threshold = std::clamp(genes.quorum_threshold, 0.05f, 1.00f);
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
    e.genes.mitotic_clock *= jitter(0.26f);
    e.genes.metabolism_efficiency *= jitter(0.24f);
    e.genes.nutrient_affinity *= jitter(0.26f);
    e.genes.stress_tolerance *= jitter(0.24f);
    e.genes.defense *= jitter(0.26f);
    e.genes.sensing *= jitter(0.28f);
    e.genes.mutation_stability *= jitter(0.24f);
    if (type_uses_telomeres(e.type))
        e.genes.telomere *= jitter(0.20f);
    else
        e.genes.telomere = 0.0f;

    switch (e.type) {
    case BIO_CELL:
        if (bio_cell_visual_family(e.morphology) == BIO_CELL_FAMILY_EPITHELIAL) {
            e.genes.spacing *= 1.20f;
            e.genes.brownian *= 0.72f;
            e.genes.defense *= 1.08f;
            e.genes.metabolism_efficiency *= 1.04f;
        } else if (bio_cell_visual_family(e.morphology) == BIO_CELL_FAMILY_AMOEBOID) {
            e.genes.seek *= 1.18f;
            e.genes.brownian *= 1.28f;
            e.genes.flee *= 0.92f;
            e.genes.sensing *= 1.08f;
        }
        if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_NEURON) {
            e.genes.seek *= 0.72f;
            e.genes.energy *= 1.08f;
            e.genes.sensing *= 1.28f;
            e.genes.metabolism_efficiency *= 0.90f;
            e.genes.mutation_stability *= 1.08f;
        } else if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_TYPE_II_PNEUMOCYTE) {
            e.genes.spacing *= 1.08f;
            e.genes.energy *= 1.04f;
            e.genes.defense *= 1.14f;
            e.genes.stress_tolerance *= 1.10f;
        } else if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_ASTROCYTE) {
            e.genes.stress_tolerance *= 1.12f;
            e.genes.defense *= 1.06f;
            e.genes.sensing *= 1.10f;
        }
        break;
    case BIO_BACTERIUM:
        e.genes.antibiotic_type = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
        e.genes.antibiotic_yield *= jitter(0.30f);
        e.genes.antibiotic_diversity *= jitter(0.45f);
        e.genes.resistance = std::uniform_real_distribution<float>(0.0f, 0.35f)(rng);
        e.genes.quorum_threshold = std::uniform_real_distribution<float>(0.25f, 0.75f)(rng);
        e.genes.metabolism_efficiency *= 1.08f;
        e.genes.nutrient_affinity *= 1.14f;
        e.genes.defense *= 0.92f;
        e.genes.mutation_stability *= 0.94f;
        if (bio_bacteria_visual_family(e.morphology) == BIO_BACTERIA_FAMILY_BACILLI) {
            e.genes.seek *= 1.12f;
            e.genes.spacing *= 1.08f;
            e.genes.antibiotic_yield *= 1.10f;
            e.genes.metabolism_efficiency *= 1.05f;
            e.genes.resistance *= 1.15f;
        } else if (bio_bacteria_visual_family(e.morphology) == BIO_BACTERIA_FAMILY_SPIRAL) {
            e.genes.brownian *= 1.20f;
            e.genes.flee *= 1.10f;
            e.genes.antibiotic_diversity *= 1.12f;
            e.genes.sensing *= 1.16f;
            e.genes.stress_tolerance *= 0.94f;
        }
        e.genes.antibiotic_yield *= bacteria_traits(e.morphology).antibiotic_bonus;
        break;
    case BIO_VIRUS:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        e.genes.nutrient_affinity = 0.70f;
        if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_SARS_COV_2) {
            e.genes.seek *= 1.18f;
            e.genes.energy *= 1.06f;
            e.genes.defense *= 1.10f;
        } else if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_BACTERIOPHAGE_T4) {
            e.genes.spacing *= 0.82f;
            e.genes.seek *= 1.10f;
            e.genes.sensing *= 1.12f;
        } else if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_INFLUENZA_A_H1N1 ||
                   e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_INFLUENZA_A_H3N2) {
            e.genes.seek *= 1.08f;
            e.genes.spacing *= 0.90f;
            e.genes.mutation_stability *= 0.82f;
            e.genes.sensing *= 1.08f;
        }
        break;
    case BIO_WHITE_BLOOD:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        e.genes.resistance = 0.0f;
        e.genes.quorum_threshold = 0.5f;
        e.genes.telomere *= 0.92f;
        e.genes.mitotic_clock *= 0.82f;
        e.genes.defense *= 1.18f;
        e.genes.stress_tolerance *= 1.16f;
        e.genes.sensing *= 1.22f;
        // Assign immune subtype: 40% T cell, 30% B cell, 30% generic (neutrophil)
        {
            float roll = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            if (roll < 0.40f) {
                e.immune_subtype = BIO_IMMUNE_T_CELL;
                e.genes.seek *= 1.25f;     // T cells are aggressive hunters
                e.genes.defense *= 1.12f;
                e.genes.sensing *= 1.18f;
            } else if (roll < 0.70f) {
                e.immune_subtype = BIO_IMMUNE_B_CELL;
                e.genes.seek *= 0.75f;     // B cells are less mobile
                e.genes.defense *= 1.08f;
                e.genes.sensing *= 1.30f;  // but better at detection
            } else {
                e.immune_subtype = BIO_IMMUNE_GENERIC;
            }
        }
        break;
    case BIO_JANITOR:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        e.genes.seek *= 1.08f;
        e.genes.flee *= 0.55f;
        e.genes.metabolism_efficiency *= 1.05f;
        e.genes.stress_tolerance *= 1.14f;
        e.genes.sensing *= 1.12f;
        break;
    default:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        break;
    }

    clamp_genes(e.genes);
}

static void mutate_entity_genes(BioEntity& e, std::mt19937& rng, float mutation_rate) {
    float effective_rate = std::clamp(mutation_rate, 0.0f, 0.95f);
    auto mutate_trait = [&](float& trait, float min_v, float max_v) {
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < std::min(0.85f, effective_rate * 8.0f + 0.06f)) {
            float delta = std::normal_distribution<float>(0.0f, 0.10f + effective_rate * 1.75f)(rng);
            trait = std::clamp(trait * (1.0f + delta), min_v, max_v);
        }
    };

    mutate_trait(e.genes.seek, 0.05f, 2.50f);
    mutate_trait(e.genes.flee, 0.05f, 2.50f);
    mutate_trait(e.genes.spacing, 0.05f, 2.50f);
    mutate_trait(e.genes.brownian, 0.00f, 2.50f);
    mutate_trait(e.genes.energy, 0.35f, 2.25f);
    mutate_trait(e.genes.telomere, 0.00f, 2.25f);
    mutate_trait(e.genes.mitotic_clock, 0.25f, 2.50f);
    mutate_trait(e.genes.metabolism_efficiency, 0.35f, 2.25f);
    mutate_trait(e.genes.nutrient_affinity, 0.25f, 2.50f);
    mutate_trait(e.genes.stress_tolerance, 0.35f, 2.50f);
    mutate_trait(e.genes.defense, 0.35f, 2.50f);
    mutate_trait(e.genes.sensing, 0.20f, 2.50f);
    mutate_trait(e.genes.mutation_stability, 0.25f, 2.50f);
    mutate_trait(e.genes.antibiotic_type, 0.00f, 1.00f);
    mutate_trait(e.genes.antibiotic_yield, 0.00f, 2.50f);
    mutate_trait(e.genes.antibiotic_diversity, 0.00f, 1.00f);
    mutate_trait(e.genes.resistance, 0.00f, 2.50f);
    mutate_trait(e.genes.quorum_threshold, 0.05f, 1.00f);

    float major_mutation_chance = std::min(0.24f, effective_rate * 0.80f + 0.03f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < major_mutation_chance) {
        auto shock_trait = [&](float& trait, float min_v, float max_v) {
            float delta = std::normal_distribution<float>(0.0f, 0.22f + effective_rate * 1.10f)(rng);
            trait = std::clamp(trait * (1.0f + delta), min_v, max_v);
        };
        int shock_count = std::uniform_int_distribution<int>(2, 4)(rng);
        for (int shock = 0; shock < shock_count; ++shock) {
            switch (std::uniform_int_distribution<int>(0, 8)(rng)) {
            case 0: shock_trait(e.genes.seek, 0.05f, 2.50f); break;
            case 1: shock_trait(e.genes.flee, 0.05f, 2.50f); break;
            case 2: shock_trait(e.genes.energy, 0.35f, 2.25f); break;
            case 3: shock_trait(e.genes.metabolism_efficiency, 0.35f, 2.25f); break;
            case 4: shock_trait(e.genes.nutrient_affinity, 0.25f, 2.50f); break;
            case 5: shock_trait(e.genes.stress_tolerance, 0.35f, 2.50f); break;
            case 6: shock_trait(e.genes.defense, 0.35f, 2.50f); break;
            case 7: shock_trait(e.genes.sensing, 0.20f, 2.50f); break;
            default: shock_trait(e.genes.mutation_stability, 0.25f, 2.50f); break;
            }
        }
    }
    clamp_genes(e.genes);
}

static float mutation_instability_scale(const BioGenes& genes);

static bool apply_genomic_mutation(BioEntity& e, std::mt19937& rng, float mutation_rate,
                                   bool allow_morphology_shift = true) {
    float effective_rate = std::clamp(mutation_rate * mutation_instability_scale(e.genes), 0.0f, 0.95f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) > effective_rate)
        return false;

    int flip_count = 1;
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < effective_rate * 0.55f)
        flip_count += 1;
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < effective_rate * 0.20f)
        flip_count += 1;
    for (int flip = 0; flip < flip_count; ++flip) {
        int bit = std::uniform_int_distribution<int>(0, 31)(rng);
        e.genome ^= (1u << bit);
    }

    mutate_entity_genes(e, rng, effective_rate);
    uint32_t variant_count = type_variant_count(e.type);
    if (allow_morphology_shift && variant_count > 1 &&
        std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < std::min(0.28f, effective_rate * 1.4f)) {
        e.morphology = (e.morphology + 1u +
                        static_cast<uint32_t>(std::uniform_int_distribution<int>(0, static_cast<int>(variant_count) - 2)(rng)))
            % variant_count;
    }
    return true;
}

static bool type_feeds_on_nutrients(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_BACTERIUM:
    case BIO_RED_BLOOD:
    case BIO_WHITE_BLOOD:
    case BIO_ANTIBODY:
    case BIO_JANITOR:
        return true;
    default:
        return false;
    }
}

static bool type_leaves_corpse(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_BACTERIUM:
    case BIO_RED_BLOOD:
    case BIO_WHITE_BLOOD:
    case BIO_ANTIBODY:
    case BIO_JANITOR:
        return true;
    default:
        return false;
    }
}

static bool type_uses_telomeres(uint32_t type) {
    return type == BIO_CELL || type == BIO_WHITE_BLOOD;
}

static float type_cycle_cooldown_base(uint32_t type) {
    // Post-division refractory period before next cycle can begin
    // Scaled to match longer division durations
    switch (type) {
    case BIO_CELL:        return 25.0f;
    case BIO_WHITE_BLOOD: return 35.0f;
    case BIO_BACTERIUM:   return 10.0f;
    default:              return 0.0f;
    }
}

static float telomere_division_capacity(const BioEntity& e) {
    if (!type_uses_telomeres(e.type))
        return 0.0f;
    float base = e.type == BIO_WHITE_BLOOD ? 7.0f : 10.0f;
    return std::max(3.0f, base + e.genes.telomere * 8.0f);
}

static float telomere_fraction_remaining(const BioEntity& e) {
    if (!type_uses_telomeres(e.type))
        return 1.0f;
    return std::clamp(e.telomere_state, 0.0f, 1.0f);
}

static void reset_infection_state(BioEntity& e) {
    e.viral_infection = {};
    e.bacterial_infection = {};
}

static int antibiotic_type_count(const BioEntity& e) {
    if (e.type != BIO_BACTERIUM)
        return 0;
    return 1 + static_cast<int>(std::floor(std::clamp(e.genes.antibiotic_diversity, 0.0f, 0.999f) * 4.0f));
}

static float wrap_unit(float v) {
    v = std::fmod(v, 1.0f);
    return (v < 0.0f) ? (v + 1.0f) : v;
}

static float circular_distance01(float a, float b) {
    float d = std::fabs(a - b);
    return std::min(d, 1.0f - d);
}

static float antibiotic_spectrum_overlap(const BioEntity& producer, const BioEntity& target) {
    int band_count = antibiotic_type_count(producer);
    if (producer.type != BIO_BACTERIUM || target.type != BIO_BACTERIUM || band_count <= 0)
        return 0.0f;

    float best = 0.0f;
    float spread = 0.07f + producer.genes.antibiotic_diversity * 0.08f;
    float band_width = 0.09f + producer.genes.antibiotic_diversity * 0.10f + target.genes.antibiotic_diversity * 0.05f;
    for (int band = 0; band < band_count; ++band) {
        float centered = static_cast<float>(band) - static_cast<float>(band_count - 1) * 0.5f;
        float signature = wrap_unit(producer.genes.antibiotic_type + centered * spread);
        float d = circular_distance01(signature, target.genes.antibiotic_type);
        best = std::max(best, 1.0f - d / std::max(0.02f, band_width));
    }
    return std::clamp(best, 0.0f, 1.0f);
}

static float antibiotic_range(const BioEntity& e) {
    return e.radius * (2.4f + e.genes.antibiotic_yield * 0.9f + e.genes.antibiotic_diversity * 0.45f);
}

static float antibiotic_film_target(const BioEntity& e, float local_pressure) {
    if (e.type != BIO_BACTERIUM)
        return 0.0f;
    float yield = std::clamp(e.genes.antibiotic_yield, 0.0f, 2.5f);
    float pressure = std::clamp(local_pressure, 0.0f, 1.0f);
    return std::clamp((0.16f + yield * 0.30f) * (0.25f + pressure * 0.95f), 0.0f, 1.0f);
}

static float viral_burst_capacity(const BioEntity& host) {
    float radius_ratio = host.radius / std::max(type_default_radius(BIO_VIRUS), 0.1f);
    float capacity = 4.5f + radius_ratio * (2.35f + std::clamp(host.viral_infection.genes.energy, 0.35f, 2.25f) * 0.65f);
    switch (host.viral_infection.morphology % BIO_VIRUS_VARIANT_COUNT) {
    case BIO_VIRUS_SARS_COV_2:
        capacity *= 1.12f;
        break;
    case BIO_VIRUS_BACTERIOPHAGE_T4:
        capacity *= 0.88f;
        break;
    default:
        break;
    }
    return std::clamp(capacity, 6.0f, 18.0f);
}

static void initialize_entity_lifecycle(BioEntity& e, std::mt19937& rng) {
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    e.corpse = false;
    e.corpse_age = 0.0f;
    e.starvation = 0.0f;
    e.nutrient_reserve = type_feeds_on_nutrients(e.type) ? randf(0.60f, 1.0f) : 0.0f;
    e.organelle_health = 1.0f;
    e.telomere_state = type_uses_telomeres(e.type) ? std::clamp(e.genes.telomere * randf(0.88f, 1.08f), 0.28f, 1.25f) : 1.0f;
    e.division_cooldown = std::max(0.0f, type_cycle_cooldown_base(e.type) / std::max(e.genes.mitotic_clock, 0.25f) * randf(0.15f, 0.65f));
    e.division_count = 0;
    e.antibiotic_film = 0.0f;
    e.atp = (e.type == BIO_CELL) ? 80.0f : (e.type == BIO_BACTERIUM ? 50.0f : 60.0f);
    e.quorum_signal = 0.0f;
    e.complement_tag = 0.0f;
    e.resistance_level = 0.0f;
    e.immune_subtype = 0;
    e.species_key = base_species_key(e.type, e.morphology);
    e.ever_infected = false;
    reset_infection_state(e);
}

static void feed_entity(BioEntity& e, float reserve_gain, float energy_gain) {
    e.nutrient_reserve = std::clamp(e.nutrient_reserve + reserve_gain, 0.0f, 1.35f);
    e.starvation = std::max(0.0f, e.starvation - reserve_gain * 0.9f);
    e.energy += energy_gain;
}

static float metabolic_gene_scale(const BioEntity& e) {
    float energy_term = 1.22f - (e.genes.energy - 1.0f) * 0.40f;
    float metabolism_term = 1.18f - (e.genes.metabolism_efficiency - 1.0f) * 0.42f;
    return std::clamp(energy_term * metabolism_term, 0.50f, 1.45f);
}

static float energy_gain_gene_scale(const BioEntity& e) {
    return std::clamp((0.75f + e.genes.energy * 0.35f) *
                      (0.68f + e.genes.nutrient_affinity * 0.38f), 0.35f, 1.95f);
}

static float nutrient_affinity_scale(const BioEntity& e) {
    return std::clamp(0.55f + e.genes.nutrient_affinity * 0.45f, 0.25f, 1.85f);
}

static float stress_tolerance_scale(const BioEntity& e) {
    return std::clamp(1.20f - (e.genes.stress_tolerance - 1.0f) * 0.38f, 0.45f, 1.35f);
}

static float defense_gene_scale(const BioEntity& e) {
    return std::clamp(0.50f + e.genes.defense * 0.50f, 0.25f, 1.85f);
}

static float sensing_gene_scale(const BioEntity& e) {
    return std::clamp(0.55f + e.genes.sensing * 0.50f, 0.30f, 1.90f);
}

static float mutation_instability_scale(const BioGenes& genes) {
    return std::clamp(1.0f + (1.0f - genes.mutation_stability) * 0.55f, 0.35f, 1.95f);
}

static float compute_environment_stress(const BiochemConfig& cfg,
                                        const BiochemEnvironment& environment,
                                        const BioEntity& e);

static float mutation_pressure_for(const BiochemConfig& cfg,
                                   const BiochemEnvironment& environment,
                                   const BioEntity& e) {
    float stress = compute_environment_stress(cfg, environment, e);
    float pressure = 1.0f + e.starvation * 0.38f + stress * 0.42f + cfg.toxicity * 0.55f;
    pressure += dominant_infection_progress(e) * 0.30f;
    if (e.type == BIO_BACTERIUM || e.type == BIO_VIRUS)
        pressure += 0.10f;
    return std::clamp(pressure, 0.35f, 2.75f);
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
        float weights[BIO_CELL_VARIANT_COUNT] = {0.18f, 0.12f, 0.12f, 0.10f, 0.14f, 0.12f, 0.12f, 0.10f};
        if (env == BIO_ENV_HUMAN_LUNG) {
            float lung[BIO_CELL_VARIANT_COUNT] = {0.08f, 0.30f, 0.32f, 0.04f, 0.04f, 0.04f, 0.10f, 0.08f};
            std::copy(lung, lung + BIO_CELL_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_POND_WATER) {
            float pond[BIO_CELL_VARIANT_COUNT] = {0.10f, 0.04f, 0.02f, 0.02f, 0.06f, 0.12f, 0.08f, 0.56f};
            std::copy(pond, pond + BIO_CELL_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_CAT_BRAIN) {
            float brain[BIO_CELL_VARIANT_COUNT] = {0.06f, 0.02f, 0.02f, 0.02f, 0.40f, 0.28f, 0.14f, 0.06f};
            std::copy(brain, brain + BIO_CELL_VARIANT_COUNT, weights);
        }
        return weighted_pick(weights, BIO_CELL_VARIANT_COUNT);
    }
    case BIO_BACTERIUM: {
        float weights[BIO_BACTERIA_VARIANT_COUNT] = {0.18f, 0.16f, 0.20f, 0.16f, 0.12f, 0.18f};
        if (env == BIO_ENV_POND_WATER) {
            float pond[BIO_BACTERIA_VARIANT_COUNT] = {0.06f, 0.06f, 0.22f, 0.16f, 0.10f, 0.40f};
            std::copy(pond, pond + BIO_BACTERIA_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_HUMAN_LUNG) {
            float lung[BIO_BACTERIA_VARIANT_COUNT] = {0.26f, 0.28f, 0.12f, 0.18f, 0.06f, 0.10f};
            std::copy(lung, lung + BIO_BACTERIA_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_PETRI_DISH) {
            float dish[BIO_BACTERIA_VARIANT_COUNT] = {0.12f, 0.10f, 0.30f, 0.18f, 0.18f, 0.12f};
            std::copy(dish, dish + BIO_BACTERIA_VARIANT_COUNT, weights);
        }
        return weighted_pick(weights, BIO_BACTERIA_VARIANT_COUNT);
    }
    case BIO_VIRUS: {
        float weights[BIO_VIRUS_VARIANT_COUNT] = {0.20f, 0.22f, 0.20f, 0.18f, 0.20f};
        if (env == BIO_ENV_HUMAN_LUNG) {
            float lung[BIO_VIRUS_VARIANT_COUNT] = {0.10f, 0.42f, 0.28f, 0.16f, 0.04f};
            std::copy(lung, lung + BIO_VIRUS_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_POND_WATER) {
            float pond[BIO_VIRUS_VARIANT_COUNT] = {0.12f, 0.08f, 0.08f, 0.08f, 0.64f};
            std::copy(pond, pond + BIO_VIRUS_VARIANT_COUNT, weights);
        } else if (env == BIO_ENV_CAT_BRAIN) {
            float brain[BIO_VIRUS_VARIANT_COUNT] = {0.24f, 0.26f, 0.18f, 0.22f, 0.10f};
            std::copy(brain, brain + BIO_VIRUS_VARIANT_COUNT, weights);
        }
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
        switch (bio_cell_visual_family(e.morphology)) {
        case BIO_CELL_FAMILY_ANIMAL:
            e.radius *= randf(0.95f, 1.10f);
            e.shape_aspect = randf(0.95f, 1.10f);
            e.shape_noise = randf(0.16f, 0.24f);
            break;
        case BIO_CELL_FAMILY_EPITHELIAL:
            e.radius *= randf(1.05f, 1.20f);
            e.shape_aspect = randf(0.58f, 0.78f);
            e.shape_noise = randf(0.08f, 0.15f);
            e.axis = normalized_or(glm::mix(e.axis, glm::vec3(0.0f, 1.0f, 0.0f), 0.55f), glm::vec3(0.0f, 1.0f, 0.0f));
            break;
        case BIO_CELL_FAMILY_AMOEBOID:
        default:
            e.radius *= randf(0.95f, 1.15f);
            e.shape_aspect = randf(0.90f, 1.12f);
            e.shape_noise = randf(0.26f, 0.42f);
            break;
        }
        if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_NEURON) {
            e.radius *= 0.92f;
            e.shape_aspect *= 1.05f;
        } else if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_FIBROBLAST) {
            e.shape_aspect *= 1.10f;
        }
        break;
    case BIO_BACTERIUM:
        switch (bio_bacteria_visual_family(e.morphology)) {
        case BIO_BACTERIA_FAMILY_COCCI:
            e.radius *= randf(0.90f, 1.02f);
            e.shape_aspect = randf(0.95f, 1.08f);
            e.shape_noise = randf(0.05f, 0.10f);
            break;
        case BIO_BACTERIA_FAMILY_BACILLI:
            e.radius *= randf(1.12f, 1.28f);
            e.shape_aspect = randf(1.70f, 2.25f);
            e.shape_noise = randf(0.03f, 0.08f);
            break;
        case BIO_BACTERIA_FAMILY_SPIRAL:
        default:
            e.radius *= randf(1.18f, 1.35f);
            e.shape_aspect = randf(2.10f, 2.90f);
            e.shape_noise = randf(0.08f, 0.16f);
            break;
        }
        break;
    case BIO_VIRUS:
        switch (bio_virus_visual_family(e.morphology)) {
        case BIO_VIRUS_FAMILY_CAPSID:
            e.radius *= randf(0.92f, 1.02f);
            e.shape_aspect = randf(0.94f, 1.04f);
            e.shape_noise = randf(0.05f, 0.10f);
            break;
        case BIO_VIRUS_FAMILY_CORONA:
            e.radius *= randf(1.00f, 1.10f);
            e.shape_aspect = randf(0.95f, 1.05f);
            e.shape_noise = randf(0.09f, 0.16f);
            break;
        case BIO_VIRUS_FAMILY_PHAGE:
            e.radius *= randf(1.12f, 1.26f);
            e.shape_aspect = randf(2.55f, 3.25f);
            e.shape_noise = randf(0.02f, 0.05f);
            break;
        case BIO_VIRUS_FAMILY_INFLUENZA:
        default:
            e.radius *= randf(0.96f, 1.08f);
            e.shape_aspect = randf(0.95f, 1.10f);
            e.shape_noise = randf(0.06f, 0.12f);
            break;
        }
        break;
    case BIO_WHITE_BLOOD:
        e.shape_noise = randf(0.22f, 0.34f);
        break;
    case BIO_JANITOR:
        e.radius *= randf(0.92f, 1.08f);
        e.shape_aspect = randf(0.92f, 1.08f);
        e.shape_noise = randf(0.18f, 0.28f);
        break;
    default:
        break;
    }
}

static float type_temperature_preference(uint32_t type) {
    switch (type) {
    case BIO_CELL:
    case BIO_WHITE_BLOOD:
    case BIO_JANITOR:
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
    case BIO_JANITOR:
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
    case BIO_JANITOR:     return 0.64f;
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
    case BIO_JANITOR:
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

static float free_virus_lifetime(const BiochemConfig& cfg, const BioEntity& virus) {
    float base = 26.0f;
    switch (virus.morphology % BIO_VIRUS_VARIANT_COUNT) {
    case BIO_VIRUS_SARS_COV_2:
        base = 18.0f;
        break;
    case BIO_VIRUS_INFLUENZA_A_H1N1:
    case BIO_VIRUS_INFLUENZA_A_H3N2:
        base = 14.0f;
        break;
    case BIO_VIRUS_BACTERIOPHAGE_T4:
        base = 34.0f;
        break;
    case BIO_VIRUS_ADENOVIRUS_C5:
    default:
        base = 30.0f;
        break;
    }
    float temperature_penalty = std::abs(cfg.temperature_c - type_temperature_preference(BIO_VIRUS)) * 0.55f;
    float ph_penalty = std::abs(cfg.acidity_ph - type_ph_preference(BIO_VIRUS)) * 2.8f;
    float toxicity_penalty = cfg.toxicity * 16.0f;
    float stability_gene = std::clamp(virus.genes.energy, 0.6f, 1.6f);
    return std::max(4.0f, (base - temperature_penalty - ph_penalty - toxicity_penalty) * stability_gene);
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
    stress *= stress_tolerance_scale(e);
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
    } else if (e.type == BIO_WHITE_BLOOD) {
        float immune_drive = std::max(0.0f, cfg.immune_pressure - 0.45f) * 0.10f;
        float toxicity_penalty = cfg.toxicity * 0.22f;
        float oxygen_penalty = std::max(0.0f, 0.72f - cfg.oxygen_level) * 0.28f;
        threshold *= 1.38f - immune_drive + toxicity_penalty + oxygen_penalty;
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

// ── CPU-side SDF primitives (matching shader exactly) ───────────────────────

static float sdf_sphere(glm::vec3 p, float r) {
    return glm::length(p) - r;
}

static float sdf_capsule(glm::vec3 p, glm::vec3 a, glm::vec3 b, float r) {
    glm::vec3 pa = p - a;
    glm::vec3 ba = b - a;
    float h = glm::clamp(glm::dot(pa, ba) / std::max(glm::dot(ba, ba), 1e-5f), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - r;
}

static float sdf_ellipsoid(glm::vec3 p, glm::vec3 r) {
    glm::vec3 pr = p / r;
    glm::vec3 pr2 = p / (r * r);
    float k0 = glm::length(pr);
    float k1 = glm::length(pr2);
    return k0 * (k0 - 1.0f) / std::max(k1, 1e-5f);
}

static float sdf_torus(glm::vec3 p, float R, float r) {
    float qx = std::sqrt(p.x * p.x + p.z * p.z) - R;
    float qy = p.y;
    return std::sqrt(qx * qx + qy * qy) - r;
}

static glm::mat3 basis_from_axis_cpu(glm::vec3 axis) {
    glm::vec3 up = glm::length(axis) > 1e-5f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 tangent = std::abs(up.y) < 0.95f
        ? glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), up))
        : glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), up));
    glm::vec3 bitangent = glm::cross(up, tangent);
    return glm::mat3(tangent, bitangent, up);
}

// Evaluate structure SDF in local space [-1,1] — matches shader sd_environment_structure_local
// Simplified: skips tiny detail textures (rings, mucosa, pits) that don't affect collision
static float structure_sdf_local(glm::vec3 p, uint32_t shape) {
    if (shape == BIO_ENV_STRUCTURE_LUNG_BRANCH) {
        float trunk_r = glm::mix(0.18f, 0.12f, glm::smoothstep(-0.88f, 0.10f, p.y));
        float trunk = sdf_capsule(p, {0, -0.88f, 0}, {0, 0.10f, 0}, trunk_r);
        float arm_a_r = glm::mix(0.13f, 0.08f, glm::smoothstep(0.0f, 0.74f, glm::length(p - glm::vec3(0.56f, 0.74f, 0))));
        float arm_a = sdf_capsule(p, {0, 0.02f, 0}, {0.56f, 0.74f, 0.08f}, arm_a_r);
        float arm_b = sdf_capsule(p, {0, 0.02f, 0}, {-0.56f, 0.74f, -0.08f}, arm_a_r);
        float twig_a = sdf_capsule(p, {0.42f, 0.54f, 0.06f}, {0.72f, 0.92f, 0.18f}, 0.055f);
        float twig_b = sdf_capsule(p, {-0.38f, 0.50f, -0.06f}, {-0.68f, 0.88f, -0.22f}, 0.050f);
        return std::min(trunk, std::min(std::min(arm_a, arm_b), std::min(twig_a, twig_b)));
    }
    if (shape == BIO_ENV_STRUCTURE_ALVEOLAR_CLUSTER) {
        float duct = sdf_capsule(p, {0, -0.38f, 0}, {0, 0.10f, 0}, 0.08f);
        float cluster = 1e5f;
        for (int i = 0; i < 8; ++i) {
            float fi = (float)i;
            float theta = fi * 2.39996f;
            float z = 1.0f - 2.0f * (fi + 0.5f) / 8.0f;
            float r_xy = std::sqrt(std::max(1.0f - z * z, 0.0f));
            glm::vec3 c = glm::vec3(std::cos(theta) * r_xy, z, std::sin(theta) * r_xy) * 0.38f;
            float alv_r = 0.22f + std::sin(fi * 1.7f) * 0.04f;
            float outer = sdf_sphere(p - c, alv_r);
            float inner = sdf_sphere(p - c, alv_r - 0.04f);
            cluster = std::min(cluster, std::max(outer, -inner));
        }
        return std::min(duct, cluster);
    }
    if (shape == BIO_ENV_STRUCTURE_POND_REED) {
        return sdf_capsule(p, {0, -0.95f, 0}, {0, 0.95f, 0}, 0.22f);
    }
    if (shape == BIO_ENV_STRUCTURE_POND_ROCK) {
        glm::vec3 q = p;
        q.y *= 1.15f;
        float rock = sdf_ellipsoid(q, {0.98f, 0.55f, 0.92f});
        float sediment = sdf_ellipsoid(q + glm::vec3(0, -0.18f, 0), {0.88f, 0.14f, 0.82f});
        return std::min(rock, sediment);
    }
    if (shape == BIO_ENV_STRUCTURE_PETRI_RIM) {
        glm::vec3 q = p;
        q.y *= 1.20f;
        float rim = sdf_torus(q, 1.02f, 0.16f);
        float base_outer = sdf_sphere(q + glm::vec3(0, 0.20f, 0), 1.12f);
        float base_inner = sdf_sphere(q + glm::vec3(0, 0.20f, 0), 1.06f);
        float base_disk = std::max(std::max(base_outer, -base_inner),
                                   std::max(q.y + 0.12f, -(q.y + 0.35f)));
        float lid = sdf_torus(q + glm::vec3(0, -0.18f, 0), 1.06f, 0.10f);
        return std::min(std::min(rim, lid), base_disk);
    }
    if (shape == BIO_ENV_STRUCTURE_PETRI_AGAR) {
        glm::vec3 q = p;
        q.y += 0.35f;
        return sdf_ellipsoid(q, {1.02f, 0.18f, 1.02f});
    }
    if (shape == BIO_ENV_STRUCTURE_BRAIN_FOLD) {
        glm::vec3 q = p;
        q.y += std::sin(p.z * 2.5f) * 0.14f + std::sin(p.z * 4.5f) * 0.05f;
        q.x += std::sin(p.z * 2.0f) * 0.08f + std::cos(p.z * 3.5f) * 0.03f;
        float fold = sdf_ellipsoid(q, {0.85f, 0.70f, 0.90f});
        float sulcus = sdf_capsule(q, {0, -0.20f, -0.85f}, {0, 0.30f, 0.85f}, 0.14f);
        float sulcus2 = sdf_capsule(q, {-0.65f, 0.10f, 0}, {0.65f, 0.15f, 0}, 0.09f);
        return std::max(fold, -std::min(sulcus, sulcus2));
    }
    if (shape == BIO_ENV_STRUCTURE_BRAIN_VESSEL) {
        float trunk = sdf_capsule(p, {0, -0.76f, -0.20f}, {0, 0.78f, 0.20f}, 0.09f);
        float br_a = sdf_capsule(p, {0, 0.18f, 0.04f}, {0.44f, 0.62f, 0.36f}, 0.065f);
        float br_b = sdf_capsule(p, {0, 0.18f, 0.04f}, {-0.32f, 0.54f, -0.28f}, 0.058f);
        float cap_a = sdf_capsule(p, {0.34f, 0.50f, 0.28f}, {0.58f, 0.78f, 0.52f}, 0.035f);
        float cap_b = sdf_capsule(p, {-0.24f, 0.42f, -0.20f}, {-0.50f, 0.68f, -0.44f}, 0.032f);
        return std::min(trunk, std::min(std::min(br_a, br_b), std::min(cap_a, cap_b)));
    }
    if (shape == BIO_ENV_STRUCTURE_GUT_VILLUS) {
        // Intestinal villus — finger-like projection ~0.5-1.6mm tall, ~0.1mm wide
        float stem = sdf_capsule(p, {0, -0.80f, 0}, {0, 0.72f, 0}, 0.16f);
        // Rounded tip (bulbous villus apex with brush border)
        float tip = sdf_sphere(p - glm::vec3(0, 0.72f, 0), 0.20f);
        // Lateral micro-ridges (epithelial cell outlines)
        return std::min(stem, tip);
    }
    if (shape == BIO_ENV_STRUCTURE_GUT_CRYPT) {
        // Intestinal crypt of Lieberkuhn — tubular gland between villi
        // Invagination into lamina propria
        float outer = sdf_capsule(p, {0, -0.85f, 0}, {0, 0.10f, 0}, 0.20f);
        float inner = sdf_capsule(p, {0, -0.75f, 0}, {0, 0.15f, 0}, 0.13f);
        return std::max(outer, -inner); // hollow tube
    }
    if (shape == BIO_ENV_STRUCTURE_BLOOD_WALL) {
        // Blood vessel endothelium — curved wall section
        glm::vec3 q = p;
        q.y *= 1.10f;
        float wall = sdf_ellipsoid(q, {0.95f, 0.85f, 0.95f});
        // Hollow interior where blood flows
        float lumen = sdf_ellipsoid(q, {0.82f, 0.72f, 0.82f});
        return std::max(wall, -lumen);
    }
    if (shape == BIO_ENV_STRUCTURE_BLOOD_VALVE) {
        // Venous valve — two leaflet cusps
        float cusp_a = sdf_ellipsoid(p - glm::vec3(0.15f, 0, 0), {0.06f, 0.35f, 0.28f});
        float cusp_b = sdf_ellipsoid(p + glm::vec3(0.15f, 0, 0), {0.06f, 0.35f, 0.28f});
        return std::min(cusp_a, cusp_b);
    }
    if (shape == BIO_ENV_STRUCTURE_SOIL_GRAIN) {
        // Soil mineral grain — irregular rounded boulder
        glm::vec3 q = p;
        q.x += std::sin(p.y * 3.0f) * 0.08f;
        q.z += std::cos(p.y * 2.5f) * 0.06f;
        float grain = sdf_ellipsoid(q, {0.82f, 0.65f, 0.76f});
        return grain;
    }
    if (shape == BIO_ENV_STRUCTURE_SOIL_ROOT) {
        // Plant root — branching tubular structure
        float main = sdf_capsule(p, {0, -0.90f, 0}, {0, 0.85f, 0}, 0.12f);
        float lat_a = sdf_capsule(p, {0, -0.20f, 0}, {0.50f, 0.30f, 0.25f}, 0.06f);
        float lat_b = sdf_capsule(p, {0, 0.15f, 0}, {-0.40f, 0.55f, -0.30f}, 0.05f);
        float tip_a = sdf_capsule(p, {0.40f, 0.22f, 0.20f}, {0.65f, 0.42f, 0.38f}, 0.035f);
        return std::min(main, std::min(std::min(lat_a, lat_b), tip_a));
    }
    if (shape == BIO_ENV_STRUCTURE_WOUND_FIBRIN) {
        // Fibrin mesh — criss-crossing strands forming clot scaffold
        float strand_a = sdf_capsule(p, {-0.70f, -0.30f, -0.20f}, {0.65f, 0.40f, 0.30f}, 0.04f);
        float strand_b = sdf_capsule(p, {0.20f, -0.50f, -0.60f}, {-0.30f, 0.55f, 0.50f}, 0.035f);
        float strand_c = sdf_capsule(p, {-0.50f, 0.10f, -0.45f}, {0.55f, -0.15f, 0.55f}, 0.038f);
        float strand_d = sdf_capsule(p, {-0.10f, -0.60f, 0.30f}, {0.25f, 0.65f, -0.20f}, 0.032f);
        float strand_e = sdf_capsule(p, {0.50f, -0.40f, 0.10f}, {-0.45f, 0.50f, -0.15f}, 0.036f);
        return std::min(strand_a, std::min(std::min(strand_b, strand_c), std::min(strand_d, strand_e)));
    }
    if (shape == BIO_ENV_STRUCTURE_WOUND_TISSUE) {
        // Wound edge — ragged tissue boundary
        glm::vec3 q = p;
        q.y += std::sin(p.x * 3.5f) * 0.12f + std::sin(p.z * 2.8f) * 0.08f;
        float tissue = sdf_ellipsoid(q, {0.90f, 0.60f, 0.85f});
        // Torn edge — irregular cavity
        float cavity = sdf_ellipsoid(q + glm::vec3(0.15f, -0.10f, 0), {0.55f, 0.40f, 0.50f});
        return std::max(tissue, -cavity);
    }
    return sdf_sphere(p, 0.80f);
}

static glm::vec3 structure_sdf_gradient(glm::vec3 p, uint32_t shape) {
    constexpr float eps = 0.005f;
    return glm::normalize(glm::vec3(
        structure_sdf_local(p + glm::vec3(eps, 0, 0), shape) - structure_sdf_local(p - glm::vec3(eps, 0, 0), shape),
        structure_sdf_local(p + glm::vec3(0, eps, 0), shape) - structure_sdf_local(p - glm::vec3(0, eps, 0), shape),
        structure_sdf_local(p + glm::vec3(0, 0, eps), shape) - structure_sdf_local(p - glm::vec3(0, 0, eps), shape)
    ));
}

struct StructCollider {
    glm::vec3 pos;
    float radius;
    uint32_t shape;
    glm::mat3 basis;
    glm::mat3 basis_inv;
    float bound_scale;
};

static float structure_bound_scale_cpu(uint32_t shape) {
    if (shape == BIO_ENV_STRUCTURE_PETRI_RIM) return 1.50f;
    if (shape == BIO_ENV_STRUCTURE_LUNG_BRANCH) return 1.40f;
    if (shape == BIO_ENV_STRUCTURE_ALVEOLAR_CLUSTER) return 1.35f;
    if (shape == BIO_ENV_STRUCTURE_POND_ROCK || shape == BIO_ENV_STRUCTURE_POND_REED) return 1.35f;
    if (shape == BIO_ENV_STRUCTURE_BRAIN_FOLD) return 1.35f;
    if (shape == BIO_ENV_STRUCTURE_PETRI_AGAR) return 1.30f;
    if (shape == BIO_ENV_STRUCTURE_BRAIN_VESSEL) return 1.30f;
    if (shape == BIO_ENV_STRUCTURE_BLOOD_WALL) return 1.40f;
    if (shape == BIO_ENV_STRUCTURE_GUT_VILLUS || shape == BIO_ENV_STRUCTURE_GUT_CRYPT) return 1.30f;
    if (shape == BIO_ENV_STRUCTURE_SOIL_GRAIN) return 1.30f;
    if (shape == BIO_ENV_STRUCTURE_SOIL_ROOT) return 1.35f;
    if (shape == BIO_ENV_STRUCTURE_WOUND_FIBRIN) return 1.25f;
    if (shape == BIO_ENV_STRUCTURE_WOUND_TISSUE) return 1.35f;
    if (shape == BIO_ENV_STRUCTURE_BLOOD_VALVE) return 1.25f;
    return 1.20f;
}

static std::vector<StructCollider> build_colliders(const BiochemEnvironment& env) {
    std::vector<StructCollider> colliders;
    for (const auto& f : env.features) {
        if (f.type != BIO_ENV_FEATURE_STRUCTURE) continue;
        StructCollider sc;
        sc.pos = f.pos;
        sc.radius = f.radius;
        sc.shape = static_cast<uint32_t>(f.shape + 0.5f);
        sc.basis = basis_from_axis_cpu(f.axis);
        sc.basis_inv = glm::transpose(sc.basis);
        sc.bound_scale = structure_bound_scale_cpu(sc.shape);
        colliders.push_back(sc);
    }
    return colliders;
}

static bool resolve_structure_collision(const StructCollider& sc, glm::vec3& pos, glm::vec3& vel, float entity_radius) {
    float bound_r = sc.radius * sc.bound_scale;
    float dist_to_center = glm::length(pos - sc.pos);
    if (dist_to_center > bound_r + entity_radius)
        return false;

    glm::vec3 local = sc.basis_inv * ((pos - sc.pos) / sc.radius);
    float local_sdf = structure_sdf_local(local, sc.shape);
    float world_sdf = local_sdf * sc.radius;

    float penetration = entity_radius - world_sdf;
    if (penetration <= 0.0f)
        return false;

    glm::vec3 local_normal = structure_sdf_gradient(local, sc.shape);
    glm::vec3 world_normal = glm::normalize(sc.basis * local_normal);

    pos += world_normal * (penetration + 0.5f);

    float vn = glm::dot(vel, world_normal);
    if (vn < 0.0f)
        vel -= world_normal * vn * 1.5f;

    return true;
}

static bool position_inside_any_structure(const std::vector<StructCollider>& colliders,
                                           glm::vec3 pos, float entity_radius) {
    for (const auto& sc : colliders) {
        float bound_r = sc.radius * sc.bound_scale;
        if (glm::length(pos - sc.pos) > bound_r + entity_radius)
            continue;
        glm::vec3 local = sc.basis_inv * ((pos - sc.pos) / sc.radius);
        float world_sdf = structure_sdf_local(local, sc.shape) * sc.radius;
        if (world_sdf < entity_radius + 0.5f)
            return true;
    }
    return false;
}

static glm::vec3 push_out_of_structures(const std::vector<StructCollider>& colliders,
                                         glm::vec3 pos, float entity_radius) {
    glm::vec3 dummy_vel(0.0f);
    for (int iter = 0; iter < 8; ++iter) {
        bool any_hit = false;
        for (const auto& sc : colliders) {
            if (resolve_structure_collision(sc, pos, dummy_vel, entity_radius))
                any_hit = true;
        }
        if (!any_hit) break;
    }
    return pos;
}

// ── Population seeding ──────────────────────────────────────────────────────

static void seed_default_population(BiochemState& state,
                                    const BiochemConfig& cfg,
                                    const BiochemEnvironment& environment,
                                    uint32_t& next_entity_id) {
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

    auto colliders = build_colliders(environment);

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
            configure_entity_shape(e, rng);

            // Retry position if inside a structure (up to 6 attempts, then push out)
            for (int attempt = 0; attempt < 6; ++attempt) {
                if (!position_inside_any_structure(colliders, e.pos, e.radius))
                    break;
                e.pos = clamp_world(center + sample_local_offset(rng, local_spread * 1.2f));
            }
            e.pos = push_out_of_structures(colliders, e.pos, e.radius);

            e.vel = sample_local_offset(rng, speed);
            e.genome = (uint32_t)rng();
            randomize_entity_genes(e, rng);
            e.energy = type_default_energy(type) * e.genes.energy * energy_scale * randf(0.85f, 1.15f);
            initialize_entity_lifecycle(e, rng);
            e.entity_id = next_entity_id++;
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

void BiochemApp::assign_entity_identity(BioEntity& e, uint32_t generation, uint32_t parent_id) {
    e.entity_id = next_entity_id_++;
    e.parent_id = parent_id;
    e.generation = generation;
}

void BiochemApp::push_event(uint32_t type, const std::string& text) {
    event_log_.push_back({sim_time_, type, text});
    while (event_log_.size() > 160)
        event_log_.pop_front();
    event_log_dirty_ = true;
}

void BiochemApp::mark_entity_corpse(BioEntity& e, uint32_t event_type, const std::string& reason) {
    if (!type_leaves_corpse(e.type)) {
        e.alive = false;
        e.corpse = false;
    } else {
        e.alive = false;
        e.corpse = true;
        e.corpse_age = 0.0f;
        e.mitosis_progress = 0.0f;
        e.energy = 0.0f;
        e.nutrient_reserve = 0.0f;
        e.starvation = 1.25f;
        e.organelle_health = 0.0f;
        e.antibiotic_film = 0.0f;
        e.atp = 0.0f;
        e.quorum_signal = 0.0f;
        reset_infection_state(e);
        e.vel *= 0.35f;
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg), "%s %s.", bio_entity_label(e).c_str(), reason.c_str());
    push_event(event_type, msg);
}

static int32_t spatial_coord(float value, float cell_size) {
    return static_cast<int32_t>(std::floor(value / cell_size));
}

static int64_t spatial_key(int32_t x, int32_t y, int32_t z) {
    constexpr int64_t BIAS = 1 << 20;
    constexpr int64_t MASK = (1 << 21) - 1;
    return (((static_cast<int64_t>(x) + BIAS) & MASK) << 42) |
           (((static_cast<int64_t>(y) + BIAS) & MASK) << 21) |
           ((static_cast<int64_t>(z) + BIAS) & MASK);
}

void BiochemApp::rebuild_spatial_index() {
    spatial_buckets_.clear();
    spatial_max_radius_ = 0.0f;
    for (const auto& e : state.entities) {
        if (!e.alive && !e.corpse)
            continue;
        spatial_max_radius_ = std::max(spatial_max_radius_, e.radius);
    }
    spatial_cell_size_ = std::max(18.0f, spatial_max_radius_ * 2.5f);
    if (spatial_cell_size_ <= 0.0f)
        spatial_cell_size_ = 18.0f;

    for (size_t i = 0; i < state.entities.size(); ++i) {
        const auto& e = state.entities[i];
        if (!e.alive && !e.corpse)
            continue;
        int32_t cx = spatial_coord(e.pos.x, spatial_cell_size_);
        int32_t cy = spatial_coord(e.pos.y, spatial_cell_size_);
        int32_t cz = spatial_coord(e.pos.z, spatial_cell_size_);
        spatial_buckets_[spatial_key(cx, cy, cz)].push_back(static_cast<int>(i));
    }
}

void BiochemApp::query_spatial_neighbors(const glm::vec3& pos, float radius, std::vector<int>& out,
                                         bool include_corpses) const {
    out.clear();
    if (spatial_buckets_.empty())
        return;

    float reach = std::max(radius, spatial_max_radius_) + spatial_max_radius_;
    int32_t min_x = spatial_coord(pos.x - reach, spatial_cell_size_);
    int32_t max_x = spatial_coord(pos.x + reach, spatial_cell_size_);
    int32_t min_y = spatial_coord(pos.y - reach, spatial_cell_size_);
    int32_t max_y = spatial_coord(pos.y + reach, spatial_cell_size_);
    int32_t min_z = spatial_coord(pos.z - reach, spatial_cell_size_);
    int32_t max_z = spatial_coord(pos.z + reach, spatial_cell_size_);
    float max_dist2 = reach * reach;

    for (int32_t x = min_x; x <= max_x; ++x) {
        for (int32_t y = min_y; y <= max_y; ++y) {
            for (int32_t z = min_z; z <= max_z; ++z) {
                auto it = spatial_buckets_.find(spatial_key(x, y, z));
                if (it == spatial_buckets_.end())
                    continue;
                for (int idx : it->second) {
                    const auto& e = state.entities[idx];
                    if (!include_corpses) {
                        if (!e.alive)
                            continue;
                    } else if (!e.alive && !e.corpse) {
                        continue;
                    }
                    if (glm::length2(e.pos - pos) <= max_dist2)
                        out.push_back(idx);
                }
            }
        }
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
                           float falloff, float noise,
                           float shape = 0.0f, float opacity = 1.0f) {
        BioEnvironmentFeature feature;
        feature.type = type;
        feature.pos = clamp_world(pos);
        feature.radius = radius;
        feature.axis = normalized_or(axis, glm::vec3(1.0f, 0.0f, 0.0f));
        feature.strength = strength;
        feature.tint = tint;
        feature.falloff = falloff;
        feature.noise = noise;
        feature.shape = shape;
        feature.opacity = opacity;
        environment_.features.push_back(feature);
    };
    auto add_structure = [&](BioEnvironmentStructureShape shape, glm::vec3 pos, float radius,
                             glm::vec3 axis, float strength, glm::vec3 tint,
                             float falloff, float noise, float opacity) {
        add_feature(BIO_ENV_FEATURE_STRUCTURE, pos, radius, axis, strength, tint,
                    falloff, noise, static_cast<float>(shape), opacity);
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
        // Scale: 1 unit ≈ 0.625 μm. Cell radius ~8 units (10 μm diameter).
        // Terminal bronchiole: ~500 μm diameter → radius ~400 units (2× world_radius!)
        // Alveolus: 200-300 μm diameter → radius ~160-240 units (world-scale caves)
        // Alveolar wall: 0.2-0.5 μm → <1 unit (tissue boundary)
        // Capillary: 5-8 μm → radius 4-6 units (cell-scale)
        // Scene: interior of alveolar duct/sac region at end of terminal bronchiole.
        // One massive bronchiolar tunnel running through the scene
        {
            glm::vec3 tunnel_pos = up_axis * (wr * 0.70f); // above the action
            add_structure(BIO_ENV_STRUCTURE_LUNG_BRANCH, tunnel_pos, wr * 0.90f,
                          flow_axis,
                          1.0f, preset.tint * 0.65f + glm::vec3(0.24f, 0.18f, 0.16f),
                          0.55f, randf(0.0f, 1.0f), 0.78f);
        }
        // 3-4 alveolar sacs — world-scale hollow cavities that entities inhabit
        for (int i = 0; i < 4; ++i) {
            float angle = (float)i / 4.0f * 6.2831853f + randf(0.0f, 0.8f);
            float dist = wr * randf(0.45f, 0.65f);
            glm::vec3 sac_pos(std::cos(angle) * dist,
                              randf(-wr * 0.30f, wr * 0.10f),
                              std::sin(angle) * dist);
            // Alveolus radius ~160-200 units — these are massive cave-like enclosures
            float alv_r = wr * randf(0.60f, 0.85f);
            glm::vec3 outward = normalized_or(sac_pos, up_axis);
            add_structure(BIO_ENV_STRUCTURE_ALVEOLAR_CLUSTER, sac_pos, alv_r,
                          outward,
                          randf(0.82f, 1.05f), preset.tint * 0.78f + glm::vec3(0.28f, 0.18f, 0.16f),
                          0.48f, randf(0.0f, 1.0f), 0.68f);
        }
        // Alveolar membrane surfaces (type I pneumocyte epithelium)
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.60f);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.18f, wr * 0.30f),
                        rand_dir(),
                        randf(0.7f, 1.0f), preset.tint * 0.9f + glm::vec3(0.08f, 0.12f, 0.10f),
                        randf(0.4f, 0.7f), randf(0.0f, 1.0f));
        }
        // Oxygen-rich nutrient zones (gas exchange surfaces)
        for (int i = 0; i < 5; ++i) {
            add_feature(BIO_ENV_FEATURE_NUTRIENT,
                        sample_local_offset(rng, wr * 0.50f),
                        randf(wr * 0.14f, wr * 0.22f), up_axis,
                        randf(0.50f, 0.80f), glm::vec3(0.20f, 0.48f, 0.30f), 0.35f, randf(0.0f, 1.0f));
        }
        // Tidal airflow currents (rhythmic breathing)
        for (int i = 0; i < 5; ++i) {
            float t = -0.35f + 0.18f * (float)i;
            add_feature(BIO_ENV_FEATURE_CURRENT,
                        flow_axis * (wr * t) + sample_local_offset(rng, wr * 0.10f),
                        randf(wr * 0.20f, wr * 0.30f), flow_axis + rand_dir() * 0.20f,
                        randf(0.65f, 0.95f), glm::vec3(0.24f, 0.42f, 0.36f), 0.30f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_POND_WATER:
        // Scale: 1 unit ≈ 0.625 μm. Cells ~8 unit radius.
        // Rocks: mm-scale → thousands of units → world-boundary terrain.
        // Reeds/cattails: cm-scale → massively bigger than world → towering columns.
        // Biofilm: few μm → cell-scale patches.
        // Scene: microscopic view at pond bottom, on/between rock surfaces.
        // 2-3 massive rocks forming the ground/walls (world-boundary scale)
        for (int i = 0; i < 3; ++i) {
            float angle = (float)i / 3.0f * 6.2831853f + randf(0.0f, 1.0f);
            float dist = wr * randf(0.40f, 0.70f);
            glm::vec3 pos(std::cos(angle) * dist,
                          -wr * 0.50f + randf(-wr * 0.05f, wr * 0.05f),
                          std::sin(angle) * dist);
            // Rocks are mm-scale → radius ≈ world_radius or larger
            add_structure(BIO_ENV_STRUCTURE_POND_ROCK, pos, wr * randf(0.70f, 0.95f),
                          normalized_or(up_axis * 0.5f + rand_dir() * 0.5f, up_axis),
                          randf(0.75f, 0.98f), glm::vec3(0.28f, 0.24f, 0.18f),
                          0.72f, randf(0.0f, 1.0f), 0.84f);
        }
        // 2-3 massive reed stems — towering pillars (cm-scale, far bigger than world)
        for (int i = 0; i < 3; ++i) {
            glm::vec3 pos(randf(-wr * 0.55f, wr * 0.55f),
                          0.0f,
                          randf(-wr * 0.55f, wr * 0.55f));
            // Reed stem is cm-scale → fills a large portion of scene as a column
            add_structure(BIO_ENV_STRUCTURE_POND_REED, pos, wr * randf(0.70f, 0.95f),
                          normalized_or(up_axis * 0.96f + rand_dir() * 0.08f, up_axis),
                          randf(0.72f, 0.95f), glm::vec3(0.22f, 0.34f, 0.12f),
                          0.65f, randf(0.0f, 1.0f), 0.72f);
        }
        // Organic detritus / biofilm membranes (μm-scale, appropriate size)
        for (int i = 0; i < 10; ++i) {
            glm::vec3 pos(randf(-wr * 0.60f, wr * 0.60f),
                          randf(-wr * 0.42f, wr * 0.12f),
                          randf(-wr * 0.60f, wr * 0.60f));
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.10f, wr * 0.20f), rand_dir(),
                        randf(0.45f, 0.72f), glm::vec3(0.18f, 0.28f, 0.18f), 0.65f, randf(0.0f, 1.0f));
        }
        // Dissolved nutrient plumes
        for (int i = 0; i < 7; ++i) {
            glm::vec3 pos(randf(-wr * 0.65f, wr * 0.65f),
                          randf(-wr * 0.25f, wr * 0.30f),
                          randf(-wr * 0.65f, wr * 0.65f));
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.12f, wr * 0.22f),
                        rand_dir(), randf(0.55f, 0.90f), glm::vec3(0.22f, 0.50f, 0.24f),
                        0.45f, randf(0.0f, 1.0f));
        }
        // Toxin zones (anaerobic pockets near substrate)
        for (int i = 0; i < 5; ++i) {
            glm::vec3 pos(randf(-wr * 0.70f, wr * 0.70f),
                          randf(-wr * 0.50f, -wr * 0.10f),
                          randf(-wr * 0.70f, wr * 0.70f));
            add_feature(BIO_ENV_FEATURE_TOXIN, pos, randf(wr * 0.12f, wr * 0.20f),
                        rand_dir(), randf(0.55f, 0.95f), glm::vec3(0.36f, 0.18f, 0.26f),
                        0.55f, randf(0.0f, 1.0f));
        }
        // Slow convective currents (thermal stratification)
        for (int i = 0; i < 4; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.50f),
                        randf(wr * 0.20f, wr * 0.30f), normalized_or(flow_axis + rand_dir() * 0.7f, flow_axis),
                        randf(0.40f, 0.75f), glm::vec3(0.16f, 0.32f, 0.30f), 0.30f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_PETRI_DISH: {
        // Scale: 1 unit ≈ 0.625 μm. Bacteria ~1 μm → radius ~1-2 units.
        // Petri dish: 90mm diameter → 72,000 units radius → effectively infinite.
        // Agar: 3mm thick → 4800 units → massive floor plane.
        // Agar pores: 0.1-2.7 μm → 0.2-4 units → sub-cell scale.
        // Scene: tiny patch of agar surface with bacteria colonies.
        // The "rim" is the world boundary itself — impossibly far away at this scale.
        // Glass dish rim — world-boundary scale (bacteria can never reach the edge)
        add_structure(BIO_ENV_STRUCTURE_PETRI_RIM, glm::vec3(0.0f), wr * 0.92f,
                      up_axis, 0.92f, glm::vec3(0.48f, 0.46f, 0.38f),
                      0.90f, randf(0.0f, 1.0f), 0.86f);
        // Agar gel — massive flat floor filling entire bottom half of world
        // The agar is effectively an infinite plane at bacterial scale
        add_structure(BIO_ENV_STRUCTURE_PETRI_AGAR, glm::vec3(0.0f, -wr * 0.30f, 0.0f),
                      wr * 0.92f, up_axis, 0.95f, glm::vec3(0.56f, 0.52f, 0.28f),
                      0.38f, randf(0.0f, 1.0f), 0.58f);
        // Streak plate inoculation zones (nutrient-rich lines on agar surface)
        for (int i = 0; i < 3; ++i) {
            float a = (float)i / 3.0f * 6.2831853f + randf(0.0f, 0.5f);
            glm::vec3 pos(std::cos(a) * wr * 0.30f, -wr * 0.22f, std::sin(a) * wr * 0.30f);
            add_structure(BIO_ENV_STRUCTURE_PETRI_AGAR, pos, randf(wr * 0.30f, wr * 0.45f),
                          up_axis, randf(0.78f, 1.02f), glm::vec3(0.54f, 0.52f, 0.26f),
                          0.42f, randf(0.0f, 1.0f), 0.62f);
        }
        // Condensation membrane around edges
        for (int i = 0; i < 8; ++i) {
            float a = (float)i / 8.0f * 6.2831853f;
            float edge_r = wr * 0.80f;
            glm::vec3 ring_pos(std::cos(a) * edge_r, randf(-wr * 0.06f, wr * 0.06f), std::sin(a) * edge_r);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, ring_pos, randf(wr * 0.12f, wr * 0.20f),
                        glm::vec3(-std::sin(a), 0.0f, std::cos(a)), randf(0.65f, 0.88f),
                        glm::vec3(0.34f, 0.34f, 0.18f), 0.75f, randf(0.0f, 1.0f));
        }
        // Nutrient-rich agar zones
        for (int i = 0; i < 6; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.45f);
            pos.y = pos.y * 0.15f - wr * 0.15f;
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.14f, wr * 0.22f),
                        up_axis, randf(0.80f, 1.15f), glm::vec3(0.34f, 0.54f, 0.24f),
                        0.30f, randf(0.0f, 1.0f));
        }
        // Minimal currents (still medium, diffusion-driven)
        for (int i = 0; i < 2; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.30f),
                        randf(wr * 0.16f, wr * 0.24f), rand_dir(),
                        randf(0.15f, 0.35f), glm::vec3(0.26f, 0.30f, 0.18f), 0.25f, randf(0.0f, 1.0f));
        }
        break;
    }

    case BIO_ENV_CAT_BRAIN:
        // Scale: 1 unit ≈ 0.625 μm. Neurons ~20 μm (radius ~16 units).
        // Cortical gyri: 2-3mm thick → 3200-4800 units → far beyond world (terrain walls).
        // Capillaries: 5-8 μm diameter → radius 4-6 units (CELL-SCALE, not terrain).
        // Axons: ~1 μm diameter → ~1.6 unit radius (tiny fibers).
        // Scene: microscopic view within cortical grey matter.
        // Massive cortical fold walls forming the scene boundary (gyri/sulci)
        // These are enormous tissue walls — entities live in the crevices between them
        for (int side = -1; side <= 1; side += 2) {
            // 2-3 massive gyri per side forming canyon walls
            for (int fold = 0; fold < 3; ++fold) {
                float t = -0.30f + fold * 0.30f;
                glm::vec3 pos = cross_axis * (float)side * (wr * 0.60f + randf(0.0f, wr * 0.10f)) +
                                flow_axis * (wr * t) +
                                up_axis * randf(-wr * 0.05f, wr * 0.10f);
                // Gyri are mm-scale → massive walls (radius ≈ 0.6-0.9 × world_radius)
                add_structure(BIO_ENV_STRUCTURE_BRAIN_FOLD, pos, wr * randf(0.55f, 0.80f),
                              normalized_or(up_axis * 0.70f + flow_axis * 0.24f + cross_axis * (float)side * 0.22f, up_axis),
                              randf(0.86f, 1.06f), glm::vec3(0.36f, 0.30f, 0.42f),
                              0.58f, randf(0.0f, 1.0f), 0.74f);
            }
        }
        // Cerebral capillaries — CELL-SCALE (5-8 μm = radius 4-6 units)
        // These are the same size as neurons, threading between cells
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = flow_axis * randf(-wr * 0.50f, wr * 0.50f) +
                            up_axis * randf(-wr * 0.20f, wr * 0.30f) +
                            cross_axis * randf(-wr * 0.35f, wr * 0.35f);
            // Capillary radius 4-8 units (cell-scale!) — NOT terrain-scale
            add_structure(BIO_ENV_STRUCTURE_BRAIN_VESSEL, pos, randf(5.0f, 10.0f),
                          normalized_or(flow_axis * 0.60f + up_axis * 0.45f + rand_dir() * 0.12f, flow_axis),
                          randf(0.72f, 0.94f), glm::vec3(0.58f, 0.18f, 0.18f),
                          0.48f, randf(0.0f, 1.0f), 0.74f);
        }
        // Neuropil membranes (dense synaptic regions)
        for (int side = -1; side <= 1; side += 2) {
            for (int lobe = 0; lobe < 6; ++lobe) {
                float u = lobe / 5.0f;
                glm::vec3 pos = cross_axis * (side * (wr * 0.20f + randf(0.0f, wr * 0.15f)))
                    + flow_axis * randf(-wr * 0.40f, wr * 0.40f)
                    + up_axis * (-wr * 0.10f + u * wr * 0.40f)
                    + sample_local_offset(rng, wr * 0.05f);
                add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.12f, wr * 0.22f),
                            up_axis + cross_axis * (float)side * 0.4f, randf(0.75f, 1.00f),
                            glm::vec3(0.28f, 0.26f, 0.38f), 0.55f, randf(0.0f, 1.0f));
            }
        }
        // Glucose/oxygen-rich nutrient zones
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = flow_axis * randf(-wr * 0.40f, wr * 0.40f)
                + up_axis * randf(-wr * 0.12f, wr * 0.35f)
                + cross_axis * randf(-wr * 0.30f, wr * 0.30f);
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.10f, wr * 0.16f),
                        flow_axis + up_axis * 0.3f, randf(0.58f, 0.82f),
                        glm::vec3(0.30f, 0.22f, 0.18f), 0.35f, randf(0.0f, 1.0f));
        }
        // Cerebrospinal fluid currents
        for (int i = 0; i < 4; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.40f),
                        randf(wr * 0.18f, wr * 0.28f), normalized_or(up_axis + rand_dir() * 0.35f, up_axis),
                        randf(0.38f, 0.68f), glm::vec3(0.26f, 0.28f, 0.40f), 0.28f, randf(0.0f, 1.0f));
        }
        // Toxin zones (metabolic waste)
        for (int i = 0; i < 2; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.20f),
                        randf(wr * 0.08f, wr * 0.14f), rand_dir(), randf(0.15f, 0.35f),
                        glm::vec3(0.34f, 0.18f, 0.22f), 0.55f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_GUT:
        // Scale: 1 unit ≈ 0.625 μm. Villus: ~0.5-1.6mm tall → 800-2560 units (terrain-scale).
        // Crypt: ~0.3-0.5mm deep → 480-800 units. Epithelial cells: ~20 μm → radius ~16 units.
        // Scene: microscopic view in the intestinal lumen between villi.
        // Intestinal villi — massive finger-like projections forming terrain walls
        for (int i = 0; i < 5; ++i) {
            float angle = (float)i / 5.0f * 6.2831853f + randf(0.0f, 0.6f);
            float dist = wr * randf(0.40f, 0.70f);
            glm::vec3 pos(std::cos(angle) * dist,
                          -wr * 0.15f + randf(-wr * 0.10f, wr * 0.10f),
                          std::sin(angle) * dist);
            add_structure(BIO_ENV_STRUCTURE_GUT_VILLUS, pos, wr * randf(0.55f, 0.80f),
                          normalized_or(up_axis * 0.92f + rand_dir() * 0.12f, up_axis),
                          randf(0.85f, 1.05f), preset.tint * 0.80f + glm::vec3(0.30f, 0.18f, 0.14f),
                          0.50f, randf(0.0f, 1.0f), 0.72f);
        }
        // Crypts of Lieberkuhn — invaginations between villi bases
        for (int i = 0; i < 3; ++i) {
            float angle = (float)i / 3.0f * 6.2831853f + randf(0.3f, 0.9f);
            float dist = wr * randf(0.25f, 0.45f);
            glm::vec3 pos(std::cos(angle) * dist,
                          -wr * 0.45f + randf(-wr * 0.05f, wr * 0.05f),
                          std::sin(angle) * dist);
            add_structure(BIO_ENV_STRUCTURE_GUT_CRYPT, pos, wr * randf(0.30f, 0.45f),
                          normalized_or(up_axis * 0.95f + rand_dir() * 0.08f, up_axis),
                          randf(0.78f, 0.95f), preset.tint * 0.65f + glm::vec3(0.22f, 0.14f, 0.10f),
                          0.45f, randf(0.0f, 1.0f), 0.65f);
        }
        // Mucus layer membranes (protective glycocalyx covering epithelium)
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.55f);
            pos.y = pos.y * 0.5f - wr * 0.10f;
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.14f, wr * 0.24f),
                        rand_dir(), randf(0.60f, 0.90f),
                        glm::vec3(0.28f, 0.22f, 0.16f), 0.55f, randf(0.0f, 1.0f));
        }
        // Dense nutrient zones (chyme — digested food particles)
        for (int i = 0; i < 7; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.50f);
            pos.y = std::abs(pos.y) * 0.6f; // nutrients concentrate in lumen
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.12f, wr * 0.20f),
                        rand_dir(), randf(0.70f, 1.10f),
                        glm::vec3(0.38f, 0.32f, 0.16f), 0.40f, randf(0.0f, 1.0f));
        }
        // Bile acid toxin zones
        for (int i = 0; i < 4; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.45f),
                        randf(wr * 0.10f, wr * 0.16f), rand_dir(),
                        randf(0.40f, 0.70f), glm::vec3(0.40f, 0.30f, 0.12f), 0.50f, randf(0.0f, 1.0f));
        }
        // Peristaltic currents (slow rhythmic push along gut axis)
        for (int i = 0; i < 4; ++i) {
            float t = -0.30f + 0.20f * (float)i;
            add_feature(BIO_ENV_FEATURE_CURRENT,
                        flow_axis * (wr * t) + sample_local_offset(rng, wr * 0.08f),
                        randf(wr * 0.18f, wr * 0.28f), flow_axis + rand_dir() * 0.15f,
                        randf(0.50f, 0.80f), glm::vec3(0.24f, 0.20f, 0.14f), 0.30f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_BLOOD:
        // Scale: 1 unit ≈ 0.625 μm. RBC: ~8 μm diameter → radius ~6 units.
        // Arteriole: 10-100 μm diameter → radius 8-80 units (cell-to-terrain scale).
        // Scene: inside a small arteriole/capillary, blood flowing through.
        // Vessel wall — curved endothelial tube forming the scene boundary
        {
            add_structure(BIO_ENV_STRUCTURE_BLOOD_WALL, glm::vec3(0.0f), wr * 0.88f,
                          flow_axis,
                          1.0f, glm::vec3(0.40f, 0.14f, 0.14f),
                          0.55f, randf(0.0f, 1.0f), 0.80f);
        }
        // Venous valve leaflets (if present in this section)
        for (int i = 0; i < 2; ++i) {
            float offset = (i == 0) ? 0.35f : -0.25f;
            glm::vec3 pos = flow_axis * (wr * offset);
            add_structure(BIO_ENV_STRUCTURE_BLOOD_VALVE, pos, wr * randf(0.20f, 0.35f),
                          normalized_or(cross_axis * ((i == 0) ? 1.0f : -1.0f) + flow_axis * 0.3f, cross_axis),
                          randf(0.80f, 1.00f), glm::vec3(0.52f, 0.20f, 0.20f),
                          0.40f, randf(0.0f, 1.0f), 0.70f);
        }
        // Endothelial membranes (glycocalyx lining)
        for (int i = 0; i < 6; ++i) {
            float angle = (float)i / 6.0f * 6.2831853f;
            float edge_r = wr * 0.72f;
            glm::vec3 pos = cross_axis * std::cos(angle) * edge_r +
                            up_axis * std::sin(angle) * edge_r +
                            flow_axis * randf(-wr * 0.35f, wr * 0.35f);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.12f, wr * 0.20f),
                        normalized_or(pos, up_axis), randf(0.70f, 0.95f),
                        glm::vec3(0.36f, 0.14f, 0.14f), 0.60f, randf(0.0f, 1.0f));
        }
        // Oxygen/glucose nutrient zones (dissolved in plasma)
        for (int i = 0; i < 6; ++i) {
            add_feature(BIO_ENV_FEATURE_NUTRIENT, sample_local_offset(rng, wr * 0.50f),
                        randf(wr * 0.14f, wr * 0.22f), flow_axis,
                        randf(0.60f, 0.90f), glm::vec3(0.50f, 0.20f, 0.20f), 0.35f, randf(0.0f, 1.0f));
        }
        // Strong laminar flow currents (blood flow along vessel axis)
        for (int i = 0; i < 6; ++i) {
            float t = -0.40f + 0.16f * (float)i;
            add_feature(BIO_ENV_FEATURE_CURRENT,
                        flow_axis * (wr * t) + sample_local_offset(rng, wr * 0.06f),
                        randf(wr * 0.16f, wr * 0.24f), flow_axis,
                        randf(0.80f, 1.20f), glm::vec3(0.42f, 0.12f, 0.12f), 0.25f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_SOIL:
        // Scale: 1 unit ≈ 0.625 μm. Bacteria ~1 μm → radius ~1-2 units.
        // Sand grains: 0.05-2mm → 80-3200 units → terrain-scale boulders.
        // Root hairs: ~10 μm diameter → radius ~8 units (cell-scale).
        // Scene: microscopic view in soil pore spaces between mineral grains.
        // Massive mineral grains forming ground/walls
        for (int i = 0; i < 4; ++i) {
            float angle = (float)i / 4.0f * 6.2831853f + randf(0.0f, 0.8f);
            float dist = wr * randf(0.35f, 0.65f);
            glm::vec3 pos(std::cos(angle) * dist,
                          -wr * 0.30f + randf(-wr * 0.15f, wr * 0.15f),
                          std::sin(angle) * dist);
            add_structure(BIO_ENV_STRUCTURE_SOIL_GRAIN, pos, wr * randf(0.55f, 0.85f),
                          normalized_or(up_axis * 0.6f + rand_dir() * 0.5f, up_axis),
                          randf(0.80f, 1.00f), glm::vec3(0.32f, 0.26f, 0.18f),
                          0.65f, randf(0.0f, 1.0f), 0.80f);
        }
        // Plant root structures threading through soil
        for (int i = 0; i < 2; ++i) {
            glm::vec3 pos(randf(-wr * 0.40f, wr * 0.40f),
                          randf(-wr * 0.10f, wr * 0.30f),
                          randf(-wr * 0.40f, wr * 0.40f));
            add_structure(BIO_ENV_STRUCTURE_SOIL_ROOT, pos, wr * randf(0.45f, 0.70f),
                          normalized_or(up_axis * 0.85f + rand_dir() * 0.20f, up_axis),
                          randf(0.75f, 0.95f), glm::vec3(0.24f, 0.30f, 0.14f),
                          0.55f, randf(0.0f, 1.0f), 0.70f);
        }
        // Organic matter membranes (decomposing leaf litter, humus)
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.55f);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.10f, wr * 0.18f),
                        rand_dir(), randf(0.50f, 0.80f),
                        glm::vec3(0.20f, 0.18f, 0.10f), 0.60f, randf(0.0f, 1.0f));
        }
        // Nutrient zones (root exudates, decomposing organics)
        for (int i = 0; i < 6; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.50f);
            add_feature(BIO_ENV_FEATURE_NUTRIENT, pos, randf(wr * 0.10f, wr * 0.18f),
                        rand_dir(), randf(0.50f, 0.85f),
                        glm::vec3(0.22f, 0.28f, 0.12f), 0.40f, randf(0.0f, 1.0f));
        }
        // Heavy metal toxin pockets
        for (int i = 0; i < 3; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.40f),
                        randf(wr * 0.08f, wr * 0.14f), rand_dir(),
                        randf(0.35f, 0.65f), glm::vec3(0.30f, 0.22f, 0.16f), 0.55f, randf(0.0f, 1.0f));
        }
        // Very slow percolation currents
        for (int i = 0; i < 2; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.35f),
                        randf(wr * 0.14f, wr * 0.22f), normalized_or(up_axis * -0.8f + rand_dir() * 0.3f, up_axis),
                        randf(0.15f, 0.35f), glm::vec3(0.18f, 0.16f, 0.10f), 0.25f, randf(0.0f, 1.0f));
        }
        break;

    case BIO_ENV_WOUND:
        // Scale: 1 unit ≈ 0.625 μm. Neutrophils ~12 μm → radius ~10 units.
        // Fibrin strands: 50-100 nm thick but μm-long → cell-scale mesh.
        // Wound edge: mm-scale → terrain wall.
        // Scene: microscopic view at wound bed, tissue edge visible as terrain.
        // Wound edge tissue — massive ragged wall on one side
        for (int i = 0; i < 3; ++i) {
            float angle = (float)i / 3.0f * 3.14159f - 0.5f; // half-circle arrangement
            float dist = wr * randf(0.50f, 0.70f);
            glm::vec3 pos(std::cos(angle) * dist,
                          randf(-wr * 0.10f, wr * 0.10f),
                          std::sin(angle) * dist);
            add_structure(BIO_ENV_STRUCTURE_WOUND_TISSUE, pos, wr * randf(0.50f, 0.75f),
                          normalized_or(glm::vec3(-std::cos(angle), 0.2f, -std::sin(angle)), up_axis),
                          randf(0.85f, 1.05f), glm::vec3(0.38f, 0.16f, 0.14f),
                          0.55f, randf(0.0f, 1.0f), 0.76f);
        }
        // Fibrin mesh — criss-crossing strands forming clot scaffold
        for (int i = 0; i < 4; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.40f);
            pos.y = pos.y * 0.5f; // concentrate near wound surface
            add_structure(BIO_ENV_STRUCTURE_WOUND_FIBRIN, pos, wr * randf(0.25f, 0.45f),
                          rand_dir(),
                          randf(0.70f, 0.92f), glm::vec3(0.60f, 0.52f, 0.38f),
                          0.35f, randf(0.0f, 1.0f), 0.55f);
        }
        // Inflammatory membranes (edema fluid boundaries)
        for (int i = 0; i < 6; ++i) {
            glm::vec3 pos = sample_local_offset(rng, wr * 0.50f);
            add_feature(BIO_ENV_FEATURE_MEMBRANE, pos, randf(wr * 0.12f, wr * 0.20f),
                        rand_dir(), randf(0.65f, 0.90f),
                        glm::vec3(0.32f, 0.14f, 0.12f), 0.50f, randf(0.0f, 1.0f));
        }
        // Serum nutrient zones (leaked plasma proteins, glucose)
        for (int i = 0; i < 5; ++i) {
            add_feature(BIO_ENV_FEATURE_NUTRIENT, sample_local_offset(rng, wr * 0.45f),
                        randf(wr * 0.12f, wr * 0.18f), rand_dir(),
                        randf(0.65f, 0.95f), glm::vec3(0.40f, 0.22f, 0.18f), 0.35f, randf(0.0f, 1.0f));
        }
        // Toxin zones (ROS, inflammatory cytokines, bacterial toxins)
        for (int i = 0; i < 5; ++i) {
            add_feature(BIO_ENV_FEATURE_TOXIN, sample_local_offset(rng, wr * 0.40f),
                        randf(wr * 0.10f, wr * 0.16f), rand_dir(),
                        randf(0.50f, 0.85f), glm::vec3(0.44f, 0.18f, 0.24f), 0.45f, randf(0.0f, 1.0f));
        }
        // Mild edema currents (fluid seepage from damaged capillaries)
        for (int i = 0; i < 3; ++i) {
            add_feature(BIO_ENV_FEATURE_CURRENT, sample_local_offset(rng, wr * 0.35f),
                        randf(wr * 0.14f, wr * 0.22f),
                        normalized_or(up_axis * -0.5f + rand_dir() * 0.6f, up_axis),
                        randf(0.25f, 0.50f), glm::vec3(0.30f, 0.14f, 0.12f), 0.30f, randf(0.0f, 1.0f));
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

    next_entity_id_ = 1;
    reset_population_metrics();
    event_log_.clear();
    event_log_dirty_ = false;
    seed_default_population(state, cfg, environment_, next_entity_id_);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
    autospawn_timer_ = 0.0f;
    push_event(BIO_EVENT_SYSTEM, "Simulation initialized.");

    // Load shared app settings (display, audio, accessibility, controls)
    load_app_settings(app_settings_, "biochem_settings.ppcfg");
}

void BiochemApp::reset_population_metrics() {
    next_species_key_ = 1024;
    ever_infected_total_ = 0;
    ever_infected_by_type_.fill(0);
    ever_infected_by_subtype_.clear();
    ever_pathogen_infections_by_type_.fill(0);
    ever_pathogen_infections_by_subtype_.clear();
}

void BiochemApp::destroy() {
    raytracer_.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

void BiochemApp::reset_simulation() {
    regenerate_environment(true);
    state.clear();
    next_entity_id_ = 1;
    reset_population_metrics();
    event_log_.clear();
    event_log_dirty_ = false;
    seed_default_population(state, cfg, environment_, next_entity_id_);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
    selected_entity = -1;
    nutrient_timer_ = 0.0f;
    autospawn_timer_ = 0.0f;
    sim_time_ = 0.0f;
    paused = false;

    char msg[192];
    std::snprintf(msg, sizeof(msg), "Simulation seeded in %s with %zu living entities.",
                  BIO_ENVIRONMENT_NAMES[cfg.environment % BIO_ENV_COUNT], state.count_alive());
    push_event(BIO_EVENT_SYSTEM, msg);
}

void BiochemApp::spawn_at(glm::vec3 pos) {
    if (state.entities.size() >= cfg.max_entities)
        return;
    int t = spawn_bio_type_ % BIO_TYPE_COUNT;
    BioEntity e;
    e.pos = pos;
    e.vel = {0, 0, 0};
    e.energy = spawn_energy_;
    e.type = (uint32_t)t;
    e.morphology = (uint32_t)(spawn_variant_ % (int)type_variant_count((uint32_t)t));
    e.genome = rand_u32();
    std::mt19937 rng(e.genome ^ (uint32_t)t * 747796405u);
    randomize_entity_genes(e, rng);
    initialize_entity_lifecycle(e, rng);
    configure_entity_shape(e, rng);
    // Push out of structures
    {
        auto colliders = build_colliders(environment_);
        e.pos = push_out_of_structures(colliders, e.pos, e.radius);
    }
    assign_entity_identity(e);

    state.entities.push_back(e);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());

    char msg[224];
    const char* variant = (e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_VIRUS)
        ? bio_entity_variant_name(e.type, e.morphology) : "Default";
    std::snprintf(msg, sizeof(msg),
                  "Spawned %s at generation %u (%s) at (%.0f, %.0f, %.0f).",
                  bio_entity_label(e).c_str(), e.generation, variant, e.pos.x, e.pos.y, e.pos.z);
    push_event(BIO_EVENT_USER, msg);
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
    if (state.entities.size() >= cfg.max_entities)
        return;
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
    e.radius = type_default_radius(BIO_NUTRIENT);
    e.type   = BIO_NUTRIENT;
    // Push out of structures
    {
        auto colliders = build_colliders(environment_);
        e.pos = push_out_of_structures(colliders, e.pos, e.radius);
    }
    e.vel = axis * (cfg.flow_strength * 0.12f) +
            glm::vec3(randf_range(-2.0f, 2.0f), randf_range(-2.0f, 2.0f), randf_range(-2.0f, 2.0f));
    e.genome = rand_u32();
    std::mt19937 rng(e.genome ^ 0xA341316Cu);
    randomize_entity_genes(e, rng);
    e.energy = type_default_energy(BIO_NUTRIENT) * e.genes.energy * (0.75f + cfg.nutrient_density * 0.35f);
    initialize_entity_lifecycle(e, rng);
    configure_entity_shape(e, rng);
    assign_entity_identity(e);
    state.entities.push_back(e);
}

size_t BiochemApp::count_alive_matching_spawn_selection() const {
    uint32_t type = static_cast<uint32_t>(spawn_bio_type_ % BIO_TYPE_COUNT);
    uint32_t variant_count = type_variant_count(type);
    uint32_t selected_variant = (variant_count > 0)
        ? static_cast<uint32_t>(spawn_variant_ % static_cast<int>(variant_count))
        : 0u;

    size_t count = 0;
    for (const auto& e : state.entities) {
        if (!e.alive || e.type != type)
            continue;
        if (variant_count > 1 && (e.morphology % variant_count) != selected_variant)
            continue;
        count += 1;
    }
    return count;
}

float BiochemApp::compute_autospawn_rate(size_t matching_alive) const {
    if (!cfg.autospawn_enabled)
        return 0.0f;

    float base_rate = std::max(0.0f, cfg.autospawn_static_rate);
    if (cfg.autospawn_mode == BIO_AUTOSPAWN_STATIC)
        return base_rate;

    uint32_t type = static_cast<uint32_t>(spawn_bio_type_ % BIO_TYPE_COUNT);
    float target = static_cast<float>(std::max(1u, cfg.autospawn_target_alive));
    if (static_cast<float>(matching_alive) >= target)
        return 0.0f;

    float deficit_ratio = (target - static_cast<float>(matching_alive)) / target;
    float env_scale = 1.0f;
    switch (type) {
    case BIO_NUTRIENT: {
        float starvation_sum = 0.0f;
        float feeders = 0.0f;
        for (const auto& e : state.entities) {
            if (!e.alive || !type_feeds_on_nutrients(e.type))
                continue;
            starvation_sum += e.starvation;
            feeders += 1.0f;
        }
        float avg_starvation = feeders > 0.0f ? starvation_sum / feeders : 0.0f;
        env_scale = 0.55f + avg_starvation * 1.10f + std::max(0.0f, 1.15f - cfg.nutrient_density) * 0.40f;
        break;
    }
    case BIO_VIRUS: {
        size_t susceptible_hosts = 0;
        for (const auto& e : state.entities) {
            if (!e.alive || (e.type != BIO_CELL && e.type != BIO_BACTERIUM))
                continue;
            if (entity_has_viral_infection(e))
                continue;
            susceptible_hosts += 1;
        }
        env_scale = 0.40f + std::min(1.80f, static_cast<float>(susceptible_hosts) / target);
        break;
    }
    case BIO_BACTERIUM: {
        size_t susceptible_hosts = 0;
        for (const auto& e : state.entities) {
            if (!e.alive || e.type != BIO_CELL || entity_has_bacterial_infection(e))
                continue;
            susceptible_hosts += 1;
        }
        env_scale = 0.50f + cfg.nutrient_density * 0.35f +
                    std::min(1.00f, static_cast<float>(susceptible_hosts) / target) * 0.65f;
        break;
    }
    case BIO_WHITE_BLOOD:
    case BIO_ANTIBODY:
        env_scale = 0.45f + cfg.immune_pressure * 0.55f;
        break;
    case BIO_TOXIN:
        env_scale = 0.40f + cfg.toxicity * 1.10f;
        break;
    default:
        env_scale = 0.60f + cfg.nutrient_density * 0.30f;
        break;
    }

    float capacity_scale = 1.0f;
    if (cfg.max_entities > 0) {
        float fill = static_cast<float>(state.entities.size()) / static_cast<float>(cfg.max_entities);
        capacity_scale = std::clamp(1.10f - fill, 0.05f, 1.0f);
    }

    return std::clamp(base_rate + cfg.autospawn_dynamic_rate * deficit_ratio * env_scale * capacity_scale,
                      0.0f, 24.0f);
}

bool BiochemApp::spawn_autospawn_entity() {
    if (state.entities.size() >= cfg.max_entities)
        return false;

    uint32_t type = static_cast<uint32_t>(spawn_bio_type_ % BIO_TYPE_COUNT);
    if (type == BIO_NUTRIENT) {
        spawn_nutrient();
        return true;
    }

    std::mt19937 rng(rand_u32() ^ static_cast<uint32_t>(sim_time_ * 1000.0f) ^ next_entity_id_);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    auto clamp_world = [&](glm::vec3 p) -> glm::vec3 {
        float max_radius = cfg.world_radius * 0.82f;
        float len = glm::length(p);
        if (len > max_radius && len > 0.0f)
            p *= max_radius / len;
        return p;
    };

    BioEnvironmentFeatureType anchor_type = BIO_ENV_FEATURE_MEMBRANE;
    switch (type) {
    case BIO_BACTERIUM:
    case BIO_ANTIBODY:
        anchor_type = BIO_ENV_FEATURE_NUTRIENT;
        break;
    case BIO_VIRUS:
    case BIO_RED_BLOOD:
        anchor_type = BIO_ENV_FEATURE_CURRENT;
        break;
    case BIO_TOXIN:
        anchor_type = BIO_ENV_FEATURE_TOXIN;
        break;
    case BIO_WHITE_BLOOD:
    case BIO_JANITOR:
    case BIO_CELL:
    default:
        anchor_type = BIO_ENV_FEATURE_MEMBRANE;
        break;
    }

    glm::vec3 pos = sample_local_offset(rng, cfg.world_radius * 0.60f);
    if (const BioEnvironmentFeature* feature = pick_random_feature(rng, environment_, anchor_type)) {
        float spread = std::min(cfg.world_radius * 0.24f, feature->radius * 0.55f + 8.0f);
        pos = feature->pos + sample_local_offset(rng, spread);
    }

    BioEntity e;
    e.type = type;
    e.morphology = (type_variant_count(type) > 0)
        ? static_cast<uint32_t>(spawn_variant_ % static_cast<int>(type_variant_count(type)))
        : 0u;
    e.pos = clamp_world(pos);
    e.vel = sample_local_offset(rng, type == BIO_VIRUS ? 28.0f : 12.0f) +
            normalized_or(cfg.flow_axis, glm::vec3(1.0f, 0.0f, 0.0f)) *
            randf(0.0f, std::max(1.0f, cfg.flow_strength * 0.30f));
    e.genome = rand_u32();
    randomize_entity_genes(e, rng);
    e.energy = std::max(1.0f, spawn_energy_) * randf(0.90f, 1.10f);
    initialize_entity_lifecycle(e, rng);
    configure_entity_shape(e, rng);
    // Push out of structures
    {
        auto colliders = build_colliders(environment_);
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (!position_inside_any_structure(colliders, e.pos, e.radius))
                break;
            e.pos = clamp_world(sample_local_offset(rng, cfg.world_radius * 0.60f));
        }
        e.pos = push_out_of_structures(colliders, e.pos, e.radius);
    }
    assign_entity_identity(e);
    state.entities.push_back(e);
    return true;
}

void BiochemApp::process_autospawn(float dt) {
    if (!cfg.autospawn_enabled) {
        autospawn_timer_ = 0.0f;
        return;
    }
    if (state.entities.size() >= cfg.max_entities)
        return;

    size_t matching_alive = count_alive_matching_spawn_selection();
    float rate = compute_autospawn_rate(matching_alive);
    if (rate <= 0.0f) {
        autospawn_timer_ = 0.0f;
        return;
    }

    autospawn_timer_ += dt;
    int safety = 0;
    while (rate > 0.0f && safety < 16 && state.entities.size() < cfg.max_entities) {
        float interval = 1.0f / std::max(rate, 0.001f);
        if (autospawn_timer_ < interval)
            break;
        autospawn_timer_ -= interval;
        if (!spawn_autospawn_entity())
            break;
        matching_alive += 1;
        rate = compute_autospawn_rate(matching_alive);
        safety += 1;
    }
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
    process_autospawn(scaled_dt);

    // Update each entity
    for (auto& e : ents) {
        if (!e.alive) {
            if (!e.corpse)
                continue;

            e.age += scaled_dt;
            e.corpse_age += scaled_dt;
            e.vel += environment_flow(cfg, environment_, e, sim_time_) * scaled_dt * 0.018f;
            e.pos += e.vel * scaled_dt;
            e.vel *= 0.992f;

            float dist_from_center = glm::length(e.pos);
            if (dist_from_center > wr) {
                glm::vec3 norm = e.pos / dist_from_center;
                e.pos = norm * wr;
                e.vel -= 2.0f * glm::dot(e.vel, norm) * norm;
                e.vel *= 0.75f;
            }
            continue;
        }

        e.division_cooldown = std::max(0.0f, e.division_cooldown - scaled_dt);

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

        // Metabolism + ATP + starvation + organelle aging
        if (e.type == BIO_CELL || e.type == BIO_BACTERIUM ||
            e.type == BIO_WHITE_BLOOD || e.type == BIO_RED_BLOOD ||
            e.type == BIO_ANTIBODY || e.type == BIO_JANITOR) {
            float base_metabolism = cfg.metabolism_rate;
            if (e.type == BIO_BACTERIUM) base_metabolism *= 0.75f;
            if (e.type == BIO_WHITE_BLOOD || e.type == BIO_ANTIBODY || e.type == BIO_JANITOR) base_metabolism *= 1.15f;
            if (e.type == BIO_RED_BLOOD) base_metabolism *= 0.55f;
            base_metabolism *= metabolic_gene_scale(e);

            float nutrient_field = sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, e.pos);
            float toxin_field = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, e.pos);
            float nutrient_affinity = nutrient_affinity_scale(e);
            float reserve_gain = 0.0f;
            float energy_gain = 0.0f;

            if (e.type == BIO_BACTERIUM) {
                float scavenging = std::max(0.0f, cfg.nutrient_density - 0.8f) * 0.35f;
                scavenging += nutrient_field * 0.85f;
                scavenging -= toxin_field * 0.22f;
                reserve_gain = std::max(0.0f, scavenging) * scaled_dt * 0.18f * nutrient_affinity;
                energy_gain = reserve_gain * 5.5f * energy_gain_gene_scale(e);
            } else if (type_feeds_on_nutrients(e.type)) {
                float uptake = nutrient_field * scaled_dt;
                if (e.type == BIO_CELL) uptake *= 0.11f;
                else if (e.type == BIO_WHITE_BLOOD) uptake *= 0.08f;
                else if (e.type == BIO_JANITOR) uptake *= 0.10f;
                else if (e.type == BIO_RED_BLOOD) uptake *= 0.05f;
                else if (e.type == BIO_ANTIBODY) uptake *= 0.03f;
                uptake *= nutrient_affinity;
                reserve_gain = uptake;
                energy_gain = uptake * 3.8f * energy_gain_gene_scale(e);
            }
            if (reserve_gain > 0.0f || energy_gain > 0.0f)
                feed_entity(e, reserve_gain, energy_gain);

            // ATP production from nutrient reserve (glycolysis + oxidative phosphorylation)
            // Aerobic: ~36 ATP/glucose; anaerobic: ~2 ATP/glucose
            float atp_efficiency = (e.type == BIO_BACTERIUM)
                ? (0.60f + cfg.oxygen_level * 0.40f)   // bacteria: partial anaerobic
                : (0.25f + cfg.oxygen_level * 0.75f);   // eukaryotes: strongly O2-dependent
            atp_efficiency *= e.genes.metabolism_efficiency;
            float atp_production = e.nutrient_reserve * atp_efficiency * scaled_dt * 12.0f;
            float atp_max = (e.type == BIO_CELL) ? 100.0f : (e.type == BIO_BACTERIUM ? 60.0f : 80.0f);
            e.atp = std::min(atp_max, e.atp + atp_production);

            float reserve_drain = base_metabolism * scaled_dt * 0.055f;
            e.nutrient_reserve = std::max(0.0f, e.nutrient_reserve - reserve_drain);

            if (e.nutrient_reserve < 0.18f)
                e.starvation = std::min(1.6f, e.starvation + scaled_dt * (0.16f + (0.18f - e.nutrient_reserve) * 1.8f));
            else
                e.starvation = std::max(0.0f, e.starvation - scaled_dt * (0.20f + e.nutrient_reserve * 0.30f));

            float stress = compute_environment_stress(cfg, environment_, e);
            float telomere_frac = telomere_fraction_remaining(e);
            float infection_burden = dominant_infection_progress(e);
            float replicative_age = 0.0f;
            if (type_uses_telomeres(e.type)) {
                float division_capacity = telomere_division_capacity(e);
                if (division_capacity > 0.0f)
                    replicative_age = std::clamp(static_cast<float>(e.division_count) / division_capacity, 0.0f, 1.25f);
            }
            float division_wear = glm::smoothstep(0.28f, 1.0f, replicative_age);
            float senescence = (type_uses_telomeres(e.type) && telomere_frac <= 0.12f)
                ? (0.18f + (0.12f - telomere_frac) * 3.2f) : 0.0f;
            senescence += division_wear * 0.30f;
            float defense_drag = 1.08f - std::clamp(e.genes.defense, 0.35f, 2.50f) * 0.10f;

            // ATP consumption drives energy drain — low ATP accelerates metabolic failure
            float atp_factor = std::clamp(e.atp / atp_max, 0.0f, 1.0f);
            float metabolic_drain = base_metabolism * scaled_dt *
                        (1.0f + stress + e.starvation * 1.45f + senescence + infection_burden * 0.28f) *
                        std::clamp(defense_drag, 0.72f, 1.08f);
            e.energy -= metabolic_drain;
            e.atp -= metabolic_drain * 2.5f; // ATP consumed proportional to metabolic demand
            e.atp = std::max(0.0f, e.atp);

            // Low ATP accelerates organelle damage
            float atp_stress = (atp_factor < 0.25f) ? (0.25f - atp_factor) * 1.2f : 0.0f;

            float target_health = 0.20f + std::clamp(e.nutrient_reserve, 0.0f, 1.0f) * 0.42f;
            target_health += std::clamp(telomere_frac, 0.0f, 1.0f) * 0.33f;
            target_health -= e.starvation * 0.34f + senescence * 0.22f + division_wear * 0.24f + atp_stress * 0.18f;
            target_health = std::clamp(target_health, 0.0f, 1.0f);
            e.organelle_health += (target_health - e.organelle_health) * std::min(1.0f, scaled_dt * 1.8f);

            if (e.energy <= 0.0f) {
                if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_viral_infection(e)) {
                    e.energy = 0.0f;
                    continue;
                }
                std::string reason = (e.atp <= 1.0f)
                    ? "suffered ATP depletion and collapsed into a dead husk"
                    : (e.starvation > 0.65f || e.nutrient_reserve <= 0.01f)
                    ? "starved and collapsed into a dead husk"
                    : "metabolically failed and collapsed into a dead husk";
                mark_entity_corpse(e, BIO_EVENT_LIFECYCLE, reason);
                continue;
            }

            if (type_uses_telomeres(e.type) && e.telomere_state <= 0.05f && e.organelle_health <= 0.08f) {
                mark_entity_corpse(e, BIO_EVENT_LIFECYCLE, "reached telomere exhaustion and senesced into a dead husk");
                continue;
            }
        }

        // Viruses die after a while
        if (e.type == BIO_VIRUS && e.age > free_virus_lifetime(cfg, e))
            e.alive = false;
        if (e.type == BIO_NUTRIENT && e.age > 80.0f + cfg.nutrient_density * 40.0f)
            e.alive = false;
        if (e.type == BIO_TOXIN && e.age > 60.0f)
            e.alive = false;
    }

    // Position-modifying subsystems first, then single spatial index rebuild
    process_repulsion();
    process_structure_collision();
    rebuild_spatial_index();

    // AI steering behaviors
    if (cfg.ai_movement)
        process_ai_movement(scaled_dt);

    // Eating nutrients
    std::vector<int> nearby;
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (!type_feeds_on_nutrients(ents[i].type)) continue;

        query_spatial_neighbors(ents[i].pos, ents[i].radius + 18.0f, nearby);
        for (int j : nearby) {
            if (static_cast<size_t>(j) == i || !ents[j].alive) continue;

            glm::vec3 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (ents[j].type == BIO_NUTRIENT && dist < touch) {
                float uptake = 0.35f + cfg.nutrient_density * 0.18f;
                uptake *= nutrient_affinity_scale(ents[i]);
                float reserve_gain = ents[j].energy * uptake * 0.020f;
                float energy_gain = ents[j].energy * uptake * 0.24f * energy_gain_gene_scale(ents[i]);
                feed_entity(ents[i], reserve_gain, energy_gain);
                ents[j].alive = false;
            } else if (ents[j].type == BIO_TOXIN && dist < touch) {
                ents[i].energy -= ents[j].energy * (0.10f + cfg.toxicity * 0.08f) /
                                  std::max(0.35f, defense_gene_scale(ents[i]));
            }
        }
    }

    // Subsystems (spatial index already current)
    process_bacteria_antibiotics(scaled_dt);
    process_gene_exchange(scaled_dt);
    process_cell_division(scaled_dt);
    process_virus_infection(scaled_dt);
    process_bacterial_colonization(scaled_dt);
    if (cfg.immune_system) {
        process_complement_cascade(scaled_dt);
        process_antibody_response(scaled_dt);
    }
    process_phagocyte_cleanup(scaled_dt);

    // Remove dead entities periodically
    size_t dead = 0;
    for (const auto& e : ents)
        if (!e.alive && !e.corpse) dead++;
    if (dead > 50) {
        if (selected_entity >= 0) {
            int new_idx = 0;
            for (int i = 0; i < selected_entity && i < (int)ents.size(); i++) {
                if (ents[i].alive || ents[i].corpse) new_idx++;
            }
            if (selected_entity < (int)ents.size() && (ents[selected_entity].alive || ents[selected_entity].corpse))
                selected_entity = new_idx;
            else
                selected_entity = -1;
        }
        ents.erase(std::remove_if(ents.begin(), ents.end(),
            [](const BioEntity& e) { return !e.alive && !e.corpse; }), ents.end());
    }

    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
}

// ── Cell Division ───────────────────────────────────────────────────────────

void BiochemApp::process_cell_division(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;
        if (e.type != BIO_CELL && e.type != BIO_BACTERIUM && e.type != BIO_WHITE_BLOOD) continue;
        if (entity_has_active_infection(e))
            continue;
        if (type_uses_telomeres(e.type) && e.telomere_state <= 0.08f)
            continue;
        // Division requires sufficient ATP (energy currency)
        float atp_min_for_division = (e.type == BIO_BACTERIUM) ? 20.0f : 35.0f;
        if (e.atp < atp_min_for_division)
            continue;
        float threshold = division_threshold_for(cfg, e);
        float previous_mitosis = e.mitosis_progress;
        bool uses_mitosis = (e.type == BIO_CELL || e.type == BIO_WHITE_BLOOD);
        bool uses_binary_fission = (e.type == BIO_BACTERIUM);
        bool uses_staged_division = uses_mitosis || uses_binary_fission;

        if (uses_staged_division) {
            // Realistic-ish timescales (still compressed ~100x for gameplay):
            // Real: cell cycle ~20h, bacteria ~30min, WBC ~36h
            // Sim: cell ~18s, bacteria ~8s, WBC ~22s
            float duration_base = 18.0f;
            if (e.type == BIO_WHITE_BLOOD)
                duration_base = 22.0f;
            else if (e.type == BIO_BACTERIUM)
                duration_base = 8.0f;
            float division_duration = duration_base /
                std::clamp(e.genes.energy * e.genes.mitotic_clock * e.genes.metabolism_efficiency, 0.55f, 2.40f);
            bool cycle_ready = (e.division_cooldown <= 0.0f);
            if (e.mitosis_progress > 0.0f || (cycle_ready && e.energy >= threshold)) {
                if (cycle_ready && e.energy >= threshold * 0.92f)
                    e.mitosis_progress = std::min(1.0f, e.mitosis_progress + dt / division_duration);
                else
                    e.mitosis_progress = std::max(0.0f, e.mitosis_progress - dt / (division_duration * 0.7f));
            }

            if (previous_mitosis <= 0.02f && e.mitosis_progress > 0.02f) {
                char msg[192];
                if (uses_binary_fission) {
                    std::snprintf(msg, sizeof(msg), "%s entered binary fission at generation %u.",
                                  bio_entity_label(e).c_str(), e.generation);
                } else {
                    std::snprintf(msg, sizeof(msg), "%s entered mitosis at generation %u.",
                                  bio_entity_label(e).c_str(), e.generation);
                }
                push_event(BIO_EVENT_DIVISION, msg);
            }

            if (e.mitosis_progress < 1.0f)
                continue;
        } else if (e.energy < threshold || e.division_cooldown > 0.0f) {
            continue;
        }
        if (ents.size() >= cfg.max_entities)
            continue;

        BioEntity child;
        child.type = e.type;
        child.morphology = e.morphology;
        child.genes = e.genes;
        child.energy = e.energy * 0.5f;
        e.energy *= 0.5f;

        std::mt19937 rng(e.genome ^ e.entity_id ^ static_cast<uint32_t>((e.division_count + 1u) * 2654435761u));
        std::uniform_real_distribution<float> theta_dist(0.0f, 6.2832f);
        std::uniform_real_distribution<float> phi_dist(-1.5708f, 1.5708f);
        float theta = theta_dist(rng);
        float phi = phi_dist(rng);
        glm::vec3 dir(cosf(phi) * cosf(theta), sinf(phi), cosf(phi) * sinf(theta));
        glm::vec3 offset = dir * e.radius;

        child.pos = e.pos + offset;
        e.pos -= offset;
        child.vel = e.vel + dir * 5.0f;
        child.genome = e.genome;

        std::mt19937 mutation_rng(child.genome ^ static_cast<uint32_t>(i * 2654435761u));
        float effective_mutation = cfg.mutation_rate * mutation_pressure_for(cfg, environment_, e);
        bool mutated = apply_genomic_mutation(child, mutation_rng, effective_mutation);

        child.alive = true;
        child.age = 0.0f;
        child.mitosis_progress = 0.0f;
        e.mitosis_progress = 0.0f;
        initialize_entity_lifecycle(child, mutation_rng);
        child.immune_subtype = e.immune_subtype; // WBC: inherit T/B/neutrophil subtype
        child.species_key = e.species_key;
        if (type_uses_telomeres(e.type)) {
            float attrition = 1.0f / telomere_division_capacity(e);
            float next_telomere = std::max(0.0f, std::min(e.telomere_state, child.telomere_state) - attrition);
            e.telomere_state = next_telomere;
            child.telomere_state = next_telomere;
            e.division_count += 1;
            child.division_count = e.division_count;
        } else {
            child.division_count = e.division_count + 1;
            e.division_count = child.division_count;
        }
        float cooldown = type_cycle_cooldown_base(e.type) / std::max(e.genes.mitotic_clock, 0.25f);
        e.division_cooldown = cooldown;
        child.division_cooldown = cooldown;
        if (mutated || child.morphology != e.morphology || child.genome != e.genome)
            child.species_key = next_species_key_++;
        assign_entity_identity(child, e.generation + 1, e.entity_id);
        configure_entity_shape(child, mutation_rng);

        std::string parent_label = bio_entity_label(e);
        std::string child_label = bio_entity_label(child);
        char msg[256];
        if (uses_binary_fission) {
            std::snprintf(msg, sizeof(msg), "%s completed binary fission and produced %s at generation %u.",
                          parent_label.c_str(), child_label.c_str(), child.generation);
        } else if (uses_mitosis) {
            std::snprintf(msg, sizeof(msg), "%s completed mitosis and produced %s at generation %u.",
                          parent_label.c_str(), child_label.c_str(), child.generation);
        } else {
            std::snprintf(msg, sizeof(msg), "%s divided and produced %s at generation %u.",
                          parent_label.c_str(), child_label.c_str(), child.generation);
        }
        ents.push_back(child);
        push_event(BIO_EVENT_DIVISION, msg);

        if (type_uses_telomeres(e.type) && e.telomere_state <= 0.14f) {
            char telomere_msg[224];
            std::snprintf(telomere_msg, sizeof(telomere_msg),
                          "%s and %s entered late-life senescence as telomeres shortened to %.0f%%.",
                          parent_label.c_str(), child_label.c_str(), e.telomere_state * 100.0f);
            push_event(BIO_EVENT_LIFECYCLE, telomere_msg);
        }
    }
}

void BiochemApp::process_bacteria_antibiotics(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();
    if (n == 0)
        return;

    std::vector<float> film_targets(n, 0.0f);
    std::vector<float> damage_accum(n, 0.0f);
    std::vector<float> strongest_hit(n, 0.0f);
    std::vector<int> damage_source(n, -1);
    std::vector<int> nearby;

    // Quorum sensing: compute local same-species density for each bacterium
    // Autoinducer molecules accumulate when kin density exceeds quorum threshold
    for (size_t i = 0; i < n; ++i) {
        auto& bact = ents[i];
        if (!bact.alive || bact.type != BIO_BACTERIUM)
            continue;

        float qs_range = 45.0f + bact.genes.sensing * 15.0f;
        int kin_count = 0;
        int rival_count = 0;
        query_spatial_neighbors(bact.pos, qs_range, nearby);
        for (int j : nearby) {
            if (j == static_cast<int>(i) || !ents[j].alive) continue;
            if (ents[j].type != BIO_BACTERIUM) continue;
            float overlap = antibiotic_spectrum_overlap(bact, ents[j]);
            if (overlap > 0.5f) kin_count++;
            else rival_count++;
        }
        // Autoinducer signal: rises with kin density, falls without neighbors
        float kin_density = static_cast<float>(kin_count) / std::max(1.0f, qs_range * 0.1f);
        float signal_target = std::clamp(kin_density / std::max(0.1f, bact.genes.quorum_threshold * 3.0f), 0.0f, 1.0f);
        bact.quorum_signal += (signal_target - bact.quorum_signal) * std::min(1.0f, dt * 2.0f);

        // Quorum-activated behaviors:
        // 1. Upregulated antibiotic production when quorum is met and rivals are near
        float quorum_boost = (bact.quorum_signal > bact.genes.quorum_threshold && rival_count > 0)
            ? (1.0f + bact.quorum_signal * 0.8f) : 1.0f;
        // 2. Reduced metabolism (biofilm-like metabolic slowdown at high quorum)
        if (bact.quorum_signal > 0.7f)
            bact.atp += dt * bact.quorum_signal * 1.5f; // biofilm protection conserves ATP

        float local_pressure = 0.0f;
        float range = antibiotic_range(bact);
        query_spatial_neighbors(bact.pos, range, nearby);
        for (int j_idx : nearby) {
            if (static_cast<int>(i) == j_idx)
                continue;
            const auto& target = ents[j_idx];
            if (!target.alive || target.type != BIO_BACTERIUM)
                continue;

            float dist = glm::length(target.pos - bact.pos);
            if (dist > range)
                continue;

            float overlap = antibiotic_spectrum_overlap(bact, target);
            float genomic_mismatch = std::min(1.0f,
                static_cast<float>(__builtin_popcount(static_cast<unsigned int>(bact.genome ^ target.genome))) / 10.0f);
            float hostility = std::clamp((1.0f - overlap) * (0.25f + genomic_mismatch * 0.75f), 0.0f, 1.0f);
            if (hostility <= 0.04f)
                continue;

            float falloff = 1.0f - dist / std::max(range, 1.0f);
            float pressure = hostility * falloff;
            local_pressure = std::max(local_pressure, pressure);

            float potency = (1.8f + bact.genes.antibiotic_yield * 3.8f +
                             bact.genes.antibiotic_diversity * 1.8f) * pressure * dt;
            potency *= 0.75f + bact.genes.sensing * 0.20f;
            potency *= quorum_boost; // quorum-upregulated potency
            // Antibiotic resistance: target's resistance gene reduces incoming damage
            float resistance_factor = std::max(0.15f, 1.0f - target.genes.resistance * 0.35f);
            potency *= resistance_factor;
            potency /= std::max(0.35f, defense_gene_scale(target));
            damage_accum[j_idx] += potency;
            if (potency > strongest_hit[j_idx]) {
                strongest_hit[j_idx] = potency;
                damage_source[j_idx] = static_cast<int>(i);
            }
        }

        film_targets[i] = antibiotic_film_target(bact, local_pressure);
    }

    for (size_t i = 0; i < n; ++i) {
        auto& bacterium = ents[i];
        if (!bacterium.alive || bacterium.type != BIO_BACTERIUM)
            continue;

        bacterium.antibiotic_film += (film_targets[i] - bacterium.antibiotic_film) * std::min(1.0f, dt * 4.0f);
        if (film_targets[i] > 0.0f) {
            bacterium.energy -= film_targets[i] * dt * (0.45f + bacterium.genes.antibiotic_diversity * 0.35f);
            bacterium.nutrient_reserve = std::max(0.0f, bacterium.nutrient_reserve - film_targets[i] * dt * 0.035f);
        }

        if (damage_accum[i] <= 0.0f)
            continue;

        // Adaptive resistance: surviving antibiotic exposure slightly increases resistance
        if (bacterium.energy > 10.0f && damage_accum[i] > 0.5f) {
            bacterium.resistance_level = std::min(1.0f, bacterium.resistance_level + damage_accum[i] * 0.003f * dt);
            // Epigenetic resistance boost feeds into effective resistance
            bacterium.genes.resistance = std::min(2.50f,
                bacterium.genes.resistance + bacterium.resistance_level * 0.001f * dt);
        }

        bacterium.energy -= damage_accum[i];
        bacterium.starvation = std::min(1.6f, bacterium.starvation + damage_accum[i] * 0.015f);
        bacterium.organelle_health = std::max(0.0f, bacterium.organelle_health - damage_accum[i] * 0.016f);
        if (bacterium.energy <= 0.0f && bacterium.alive) {
            std::string reason = "succumbed to an antibiotic film";
            if (damage_source[i] >= 0 && damage_source[i] < static_cast<int>(ents.size()))
                reason = "succumbed to the antibiotic film from " + bio_entity_label(ents[damage_source[i]]);
            mark_entity_corpse(bacterium, BIO_EVENT_LIFECYCLE, reason);
        }
    }
}

void BiochemApp::process_gene_exchange(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();
    if (n < 2)
        return;
    std::vector<int> nearby;

    for (size_t i = 0; i < n; ++i) {
        auto& a = ents[i];
        if (!a.alive || !type_supports_gene_exchange(a.type))
            continue;

        query_spatial_neighbors(a.pos, (a.radius + spatial_max_radius_) * 2.0f, nearby);
        for (int j_idx : nearby) {
            if (j_idx <= static_cast<int>(i))
                continue;
            auto& b = ents[j_idx];
            if (!b.alive || b.type != a.type || !type_supports_gene_exchange(b.type))
                continue;
            if (subtype_exchange_group(a.type, a.morphology) != subtype_exchange_group(b.type, b.morphology))
                continue;

            float dist = glm::length(a.pos - b.pos);
            float range = (a.radius + b.radius) * 1.9f;
            if (dist > range)
                continue;

            float genome_similarity = 1.0f - std::min(1.0f,
                static_cast<float>(__builtin_popcount(static_cast<unsigned int>(a.genome ^ b.genome))) / 18.0f);
            float subtype_mix = (a.morphology == b.morphology) ? 0.20f : 0.42f;
            float instability = 0.5f * (mutation_instability_scale(a.genes) + mutation_instability_scale(b.genes));
            float chance = dt * (0.010f + subtype_mix * 0.028f) * (0.55f + genome_similarity * 0.45f) *
                           std::clamp(0.80f + instability * 0.20f, 0.55f, 1.25f);
            if (randf_range(0.0f, 1.0f) > chance)
                continue;

            BioEntity* recipient = (randf_range(0.0f, 1.0f) < 0.5f) ? &a : &b;
            const BioEntity* donor = (recipient == &a) ? &b : &a;
            blend_gene_block(recipient->genes, donor->genes, 0.28f);
            recipient->genome ^= (donor->genome & 0x00FF00FFu);

            bool subtype_shift = (recipient->morphology != donor->morphology &&
                                  randf_range(0.0f, 1.0f) < 0.18f);
            if (subtype_shift)
                recipient->morphology = donor->morphology;
            std::mt19937 exchange_rng(recipient->genome ^ donor->genome ^
                                      static_cast<uint32_t>(i * 977u + static_cast<size_t>(j_idx) * 131u));
            bool recombined = apply_genomic_mutation(*recipient, exchange_rng, cfg.mutation_rate * 0.45f, false);
            clamp_genes(recipient->genes);

            bool speciation = subtype_shift || recombined || randf_range(0.0f, 1.0f) < 0.14f;
            if (speciation) {
                recipient->species_key = next_species_key_++;
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                              "%s exchanged genes with %s and diverged into species cluster %u.",
                              bio_entity_label(*recipient).c_str(), bio_entity_label(*donor).c_str(),
                              recipient->species_key);
                push_event(BIO_EVENT_DIVISION, msg);
            }

            std::mt19937 shape_rng(recipient->genome ^ recipient->species_key ^
                                   static_cast<uint32_t>(i * 977u + static_cast<size_t>(j_idx) * 131u));
            configure_entity_shape(*recipient, shape_rng);
        }
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
    std::vector<int> nearby;

    for (size_t i = 0; i < n; ++i) {
        auto& host = ents[i];
        if (!host.alive || (host.type != BIO_CELL && host.type != BIO_BACTERIUM) || !entity_has_viral_infection(host))
            continue;

        auto& infection = host.viral_infection;
        float local_shelter = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, host.pos);
        float local_toxin = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, host.pos);
        float host_defense = defense_gene_scale(host);
        float local_factor = std::clamp(1.0f + local_toxin * 0.30f - local_shelter * 0.16f, 0.65f, 1.65f);
        float replication_rate = effective_rate * local_factor *
            (0.65f + std::clamp(infection.genes.mitotic_clock, 0.25f, 2.50f) * 0.40f) *
            (0.70f + std::clamp(infection.genes.energy, 0.35f, 2.25f) * 0.30f) *
            (0.72f + infection.genes.sensing * 0.18f) / host_defense;
        float burst_capacity = viral_burst_capacity(host);
        float ingress_gate = glm::smoothstep(0.04f, 0.22f, infection.progress);
        float replication_gate = glm::smoothstep(0.14f, 0.82f, infection.progress);
        float burst_bias = 0.70f + std::clamp(infection.genes.seek, 0.05f, 2.50f) * 0.12f +
                           std::clamp(infection.genes.energy, 0.35f, 2.25f) * 0.20f;

        infection.progress = std::min(1.20f, infection.progress + replication_rate * dt * 0.38f);
        float target_load = 1.0f + replication_gate * burst_capacity * burst_bias;
        infection.load += (target_load - infection.load) * std::min(1.0f, dt * (1.9f + ingress_gate * 2.5f));
        host.energy -= dt * (4.4f + replication_gate * 2.2f + infection.load * 1.35f) /
                       host_defense;
        host.starvation = std::min(1.6f, host.starvation + dt * infection.progress * 0.18f);
        host.organelle_health = std::max(0.0f, host.organelle_health - dt * (0.012f + replication_gate * 0.09f));
        host.vel *= 0.992f;

        bool overcrowded = infection.load >= burst_capacity * 0.96f;
        bool ready_to_lyse = overcrowded || infection.progress >= 1.08f ||
                             (host.energy <= 10.0f && infection.progress >= 0.82f) ||
                             (host.organelle_health <= 0.05f && infection.progress >= 0.76f);
        if (!ready_to_lyse)
            continue;

        std::string host_label = bio_entity_label(host);
        uint32_t source_generation = infection.generation;
        uint32_t source_id = infection.source_id;
        uint32_t source_species = infection.species_key;
        uint32_t source_genome = infection.genome;
        uint32_t source_morphology = infection.morphology % BIO_VIRUS_VARIANT_COUNT;
        BioGenes source_genes = infection.genes;
        float host_mutation_pressure = mutation_pressure_for(cfg, environment_, host);
        glm::vec3 burst_origin = host.pos;
        glm::vec3 burst_velocity = host.vel;
        // Real burst sizes: adenovirus ~100, influenza ~1000, T4 phage ~200
        // Capped for sim performance but biologically more accurate
        int burst_count = std::clamp(
            static_cast<int>(std::round(infection.load * (6.0f + source_genes.energy * 2.5f) +
                                        (source_morphology == BIO_VIRUS_BACTERIOPHAGE_T4 ? 30.0f : 0.0f))),
            30, 120);

        mark_entity_corpse(host, BIO_EVENT_INFECTION, "was lysed into a dead husk");

        int spawned = 0;
        for (int k = 0; k < burst_count && ents.size() < cfg.max_entities; ++k) {
            BioEntity v;
            std::mt19937 release_rng(source_genome ^ static_cast<uint32_t>(k * 977u + i * 131u));
            float a = std::uniform_real_distribution<float>(0.0f, 6.2832f)(release_rng);
            float p = std::uniform_real_distribution<float>(-1.5708f, 1.5708f)(release_rng);
            glm::vec3 d(cosf(p) * cosf(a), sinf(p), cosf(p) * sinf(a));
            v.pos = burst_origin + d * (type_default_radius(BIO_VIRUS) * 2.4f + 0.18f * spawned);
            v.vel = burst_velocity * 0.22f + d * randf_range(42.0f, 84.0f);
            v.type = BIO_VIRUS;
            v.morphology = source_morphology;
            v.genes = source_genes;
            v.genome = source_genome ? source_genome : release_rng();
            std::mt19937 vrng(v.genome ^ static_cast<uint32_t>(k * 977u + i * 131u));
            apply_genomic_mutation(v, vrng, cfg.mutation_rate * 1.5f * host_mutation_pressure);
            v.energy = type_default_energy(BIO_VIRUS) * std::clamp(v.genes.energy, 0.45f, 1.8f);
            initialize_entity_lifecycle(v, vrng);
            v.species_key = (v.genome != source_genome) ? next_species_key_++ :
                (source_species != 0 ? source_species : base_species_key(BIO_VIRUS, v.morphology));
            assign_entity_identity(v, source_generation + 1, source_id);
            configure_entity_shape(v, vrng);
            ents.push_back(v);
            spawned++;
        }

        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "%s burst after viral replication and released %d virions at generation %u.",
                      host_label.c_str(), spawned, source_generation + 1);
        push_event(BIO_EVENT_INFECTION, msg);
    }

    n = ents.size();
    for (size_t i = 0; i < n; ++i) {
        auto& virus = ents[i];
        if (!virus.alive || virus.type != BIO_VIRUS)
            continue;

        int best_host = -1;
        float best_contact = cfg.infection_radius;
        query_spatial_neighbors(virus.pos, cfg.infection_radius + 18.0f, nearby);
        for (int j_idx : nearby) {
            if (static_cast<int>(i) == j_idx)
                continue;
            auto& host = ents[j_idx];
            if (!host.alive || (host.type != BIO_CELL && host.type != BIO_BACTERIUM))
                continue;
            if (entity_has_viral_infection(host))
                continue;
            if (!virus_targets_host(virus, host))
                continue;

            float dist = glm::length(host.pos - virus.pos);
            float contact_radius = std::min(cfg.infection_radius, virus.radius + host.radius * 1.02f + 0.8f);
            if (dist < contact_radius && dist < best_contact) {
                best_contact = dist;
                best_host = j_idx;
            }
        }

        if (best_host < 0)
            continue;

        auto& host = ents[best_host];
        float local_shelter = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, host.pos);
        float tropism = virus_host_tropism(virus, host);
        if (tropism <= 0.0f)
            continue;
        float host_defense = defense_gene_scale(host);
        float local_factor = std::clamp(1.0f - local_shelter * 0.18f, 0.45f, 1.0f) * tropism;
        local_factor *= (0.72f + virus.genes.defense * 0.18f) / host_defense;
        float contact_radius = std::min(cfg.infection_radius, virus.radius + host.radius * 1.02f + 0.8f);
        float chance = std::clamp(effective_rate * local_factor * (1.0f - best_contact / std::max(contact_radius, 1.0f)) * dt * 3.5f,
                                  0.0f, 0.95f);
        if (randf_range(0.0f, 1.0f) > chance)
            continue;

        auto& infection = host.viral_infection;
        if (infection.progress <= 0.0f) {
            infection.progress = 0.01f;
            infection.load = 1.0f;
            infection.axis = normalized_or(virus.pos - host.pos, host.axis);
            infection.morphology = virus.morphology;
            infection.source_id = virus.entity_id;
            infection.source_type = BIO_VIRUS;
            infection.species_key = virus.species_key;
            infection.generation = virus.generation;
            infection.genome = virus.genome;
            infection.genes = virus.genes;
            if (!host.ever_infected) {
                host.ever_infected = true;
                ever_infected_total_ += 1;
                ever_infected_by_type_[host.type % BIO_TYPE_COUNT] += 1;
                ever_infected_by_subtype_[type_subtype_key(host.type, host.morphology)] += 1;
            }
            ever_pathogen_infections_by_type_[BIO_VIRUS] += 1;
            ever_pathogen_infections_by_subtype_[type_subtype_key(BIO_VIRUS, virus.morphology)] += 1;

            char msg[224];
            std::snprintf(msg, sizeof(msg), "%s entered %s and began replicating.",
                          bio_entity_label(virus).c_str(), bio_entity_label(host).c_str());
            push_event(BIO_EVENT_INFECTION, msg);
        } else {
            infection.load += 0.45f + std::clamp(virus.genes.energy, 0.35f, 2.25f) * 0.25f;
            infection.progress = std::max(infection.progress, 0.06f);
            if (virus.genes.energy > infection.genes.energy) {
                infection.axis = normalized_or(glm::mix(infection.axis, virus.pos - host.pos, 0.65f), host.axis);
                infection.morphology = virus.morphology;
                infection.source_id = virus.entity_id;
                infection.source_type = BIO_VIRUS;
                infection.species_key = virus.species_key;
                infection.generation = virus.generation;
                infection.genome = virus.genome;
                infection.genes = virus.genes;
            }
        }

        virus.alive = false;
    }
}

void BiochemApp::process_bacterial_colonization(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();
    if (n == 0)
        return;

    float acidity_bias = 1.0f - std::min(std::abs(cfg.acidity_ph - 7.0f), 2.1f) / 2.1f * 0.28f;
    float nutrient_bias = 0.72f + cfg.nutrient_density * 0.34f;
    float oxygen_bias = 0.85f + std::max(0.0f, 0.85f - cfg.oxygen_level) * 0.20f;
    float effective_rate = cfg.infection_rate * acidity_bias * nutrient_bias * oxygen_bias * 0.58f;
    std::vector<int> nearby;

    for (size_t i = 0; i < n; ++i) {
        auto& host = ents[i];
        if (!host.alive || host.type != BIO_CELL || !entity_has_bacterial_infection(host))
            continue;

        auto& infection = host.bacterial_infection;
        const auto& traits = bacteria_traits(infection.morphology);
        float nutrient_field = sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, host.pos);
        float toxin_field = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, host.pos);
        float entry_bias = pathogen_entry_bias(traits.entry_mode);
        float host_defense = defense_gene_scale(host);
        float growth_rate = effective_rate * traits.colonization_rate * entry_bias *
                            (0.68f + nutrient_field * 0.42f) *
                            (0.70f + std::clamp(infection.genes.energy, 0.35f, 2.25f) * 0.22f) *
                            (0.72f + infection.genes.sensing * 0.18f) / host_defense;
        float host_drag = (0.78f + cell_traits(host.morphology).infection_susceptibility * 0.22f) / host_defense;

        infection.progress = std::min(1.18f, infection.progress + growth_rate * dt * 0.26f);
        infection.load += dt * (0.55f + growth_rate * 1.8f + toxin_field * 0.18f);
        host.energy -= dt * (2.1f + infection.load * 0.62f + toxin_field * 0.85f) * host_drag;
        host.starvation = std::min(1.6f, host.starvation + dt * infection.progress * 0.12f);
        host.organelle_health = std::max(0.0f, host.organelle_health -
            dt * (0.010f + infection.progress * 0.048f + toxin_field * 0.020f));
        host.vel *= 0.994f;

        bool overwhelmed = infection.load >= (4.4f + traits.colonization_rate * 3.2f);
        bool host_failed = overwhelmed || infection.progress >= 1.04f ||
                           (host.energy <= 9.0f && infection.progress >= 0.68f) ||
                           (host.organelle_health <= 0.08f && infection.progress >= 0.62f);
        if (!host_failed)
            continue;

        std::string host_label = bio_entity_label(host);
        uint32_t source_generation = infection.generation;
        uint32_t source_id = infection.source_id;
        uint32_t source_species = infection.species_key;
        uint32_t source_genome = infection.genome;
        uint32_t source_morphology = infection.morphology % BIO_BACTERIA_VARIANT_COUNT;
        BioGenes source_genes = infection.genes;
        float host_mutation_pressure = mutation_pressure_for(cfg, environment_, host);
        glm::vec3 release_origin = host.pos;
        glm::vec3 release_velocity = host.vel;

        mark_entity_corpse(host, BIO_EVENT_INFECTION, "succumbed to bacterial colonization and collapsed into a dead husk");

        int spawned = std::clamp(
            static_cast<int>(std::round(1.0f + infection.load * 0.40f +
                                        std::clamp(source_genes.antibiotic_yield, 0.0f, 2.5f) * 0.35f)),
            1, 4);
        int released = 0;
        for (int k = 0; k < spawned && ents.size() < cfg.max_entities; ++k) {
            BioEntity bacterium;
            std::mt19937 release_rng(source_genome ^ static_cast<uint32_t>(k * 733u + i * 193u));
            float a = std::uniform_real_distribution<float>(0.0f, 6.2832f)(release_rng);
            float p = std::uniform_real_distribution<float>(-1.5708f, 1.5708f)(release_rng);
            glm::vec3 d(cosf(p) * cosf(a), sinf(p), cosf(p) * sinf(a));
            bacterium.pos = release_origin + d * (type_default_radius(BIO_BACTERIUM) * 1.8f + 0.14f * released);
            bacterium.vel = release_velocity * 0.28f + d * randf_range(12.0f, 28.0f);
            bacterium.type = BIO_BACTERIUM;
            bacterium.morphology = source_morphology;
            bacterium.genes = source_genes;
            bacterium.genome = source_genome ? source_genome : release_rng();
            std::mt19937 brng(bacterium.genome ^ static_cast<uint32_t>(k * 733u + i * 193u));
            apply_genomic_mutation(bacterium, brng, cfg.mutation_rate * host_mutation_pressure);
            bacterium.energy = type_default_energy(BIO_BACTERIUM) * std::clamp(bacterium.genes.energy, 0.45f, 1.9f);
            initialize_entity_lifecycle(bacterium, brng);
            bacterium.species_key = (bacterium.genome != source_genome) ? next_species_key_++ :
                (source_species != 0 ? source_species : base_species_key(BIO_BACTERIUM, bacterium.morphology));
            assign_entity_identity(bacterium, source_generation + 1, source_id);
            configure_entity_shape(bacterium, brng);
            ents.push_back(bacterium);
            released++;
        }

        char msg[320];
        std::snprintf(msg, sizeof(msg),
                      "%s was overrun by %s and shed %d daughter bacteria at generation %u.",
                      host_label.c_str(), bio_entity_variant_name(BIO_BACTERIUM, source_morphology), released,
                      source_generation + 1);
        push_event(BIO_EVENT_INFECTION, msg);
    }

    n = ents.size();
    for (size_t i = 0; i < n; ++i) {
        auto& bacterium = ents[i];
        if (!bacterium.alive || bacterium.type != BIO_BACTERIUM)
            continue;

        int best_host = -1;
        float best_contact = cfg.infection_radius * 0.92f;
        query_spatial_neighbors(bacterium.pos, cfg.infection_radius + 18.0f, nearby);
        for (int j_idx : nearby) {
            if (static_cast<int>(i) == j_idx)
                continue;
            auto& host = ents[j_idx];
            if (!host.alive || host.type != BIO_CELL)
                continue;
            if (entity_has_bacterial_infection(host))
                continue;

            float dist = glm::length(host.pos - bacterium.pos);
            float contact_radius = std::min(cfg.infection_radius * 0.92f, bacterium.radius + host.radius * 1.02f + 1.4f);
            if (dist < contact_radius && dist < best_contact) {
                best_contact = dist;
                best_host = j_idx;
            }
        }

        if (best_host < 0)
            continue;

        auto& host = ents[best_host];
        float tropism = bacteria_host_tropism(bacterium, host);
        if (tropism <= 0.0f)
            continue;
        float membrane_cover = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, host.pos);
        float nutrient_field = sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, host.pos);
        float host_defense = defense_gene_scale(host);
        float contact_radius = std::min(cfg.infection_radius * 0.92f, bacterium.radius + host.radius * 1.02f + 1.4f);
        float local_factor = std::clamp((0.74f + nutrient_field * 0.30f) * (1.0f - membrane_cover * 0.12f) * tropism,
                                        0.25f, 2.25f);
        local_factor *= (0.72f + bacterium.genes.sensing * 0.18f) / host_defense;
        float chance = std::clamp(effective_rate * local_factor *
                                  (1.0f - best_contact / std::max(contact_radius, 1.0f)) * dt * 2.4f,
                                  0.0f, 0.88f);
        if (randf_range(0.0f, 1.0f) > chance)
            continue;

        auto& infection = host.bacterial_infection;
        infection.progress = 0.01f;
        infection.load = 1.0f;
        infection.axis = normalized_or(bacterium.pos - host.pos, host.axis);
        infection.morphology = bacterium.morphology;
        infection.source_id = bacterium.entity_id;
        infection.source_type = BIO_BACTERIUM;
        infection.species_key = bacterium.species_key;
        infection.generation = bacterium.generation;
        infection.genome = bacterium.genome;
        infection.genes = bacterium.genes;
        if (!host.ever_infected) {
            host.ever_infected = true;
            ever_infected_total_ += 1;
            ever_infected_by_type_[host.type % BIO_TYPE_COUNT] += 1;
            ever_infected_by_subtype_[type_subtype_key(host.type, host.morphology)] += 1;
        }
        ever_pathogen_infections_by_type_[BIO_BACTERIUM] += 1;
        ever_pathogen_infections_by_subtype_[type_subtype_key(BIO_BACTERIUM, bacterium.morphology)] += 1;

        char msg[320];
        std::snprintf(msg, sizeof(msg), "%s colonized %s via %s.",
                      bio_entity_label(bacterium).c_str(), bio_entity_label(host).c_str(),
                      bacteria_traits(bacterium.morphology).infection_mode);
        push_event(BIO_EVENT_INFECTION, msg);
    }
}

// ── Complement Cascade ─────────────────────────────────────────────────────
// Innate immune system: complement proteins opsonize (tag) pathogens, making
// them easier for WBC/phagocytes to find and destroy. Also causes direct
// membrane attack on bacteria (MAC — membrane attack complex).

void BiochemApp::process_complement_cascade(float dt) {
    auto& ents = state.entities;
    float effective_immune = cfg.immune_strength * (0.45f + cfg.immune_pressure);
    std::vector<int> nearby;

    for (auto& e : ents) {
        if (!e.alive) continue;
        bool is_pathogen = (e.type == BIO_VIRUS) ||
                           (e.type == BIO_BACTERIUM && e.antibiotic_film < 0.3f);
        bool is_infected = (e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e);

        if (is_pathogen || is_infected) {
            // Complement activation — tag increases based on immune pressure and pathogen exposure
            // Classical pathway: antibodies near pathogen trigger faster complement activation
            float antibody_boost = 0.0f;
            query_spatial_neighbors(e.pos, 40.0f, nearby);
            for (int j : nearby) {
                if (ents[j].alive && ents[j].type == BIO_ANTIBODY)
                    antibody_boost += 0.15f;
            }
            float activation_rate = (0.02f + effective_immune * 0.03f + antibody_boost) * dt;
            // Lectin pathway: bacteria with certain surface markers activate faster
            if (e.type == BIO_BACTERIUM) activation_rate *= 1.3f;
            e.complement_tag = std::min(1.0f, e.complement_tag + activation_rate);

            // Membrane Attack Complex (MAC) — direct bacterial damage at high complement
            if (e.type == BIO_BACTERIUM && e.complement_tag > 0.7f) {
                float mac_damage = (e.complement_tag - 0.7f) * effective_immune * 2.5f * dt;
                // Resistance gene partially blocks MAC pore formation
                mac_damage *= std::max(0.2f, 1.0f - e.genes.resistance * 0.25f);
                e.energy -= mac_damage;
                e.organelle_health = std::max(0.0f, e.organelle_health - mac_damage * 0.008f);
                if (e.energy <= 0.0f && e.alive) {
                    mark_entity_corpse(e, BIO_EVENT_IMMUNE,
                        "was lysed by complement membrane attack complex (MAC)");
                }
            }
        } else {
            // Non-pathogen: complement tag decays
            e.complement_tag = std::max(0.0f, e.complement_tag - dt * 0.08f);
        }
    }
}

// ── Antibody / Immune Response ──────────────────────────────────────────────

void BiochemApp::process_antibody_response(float dt) {
    auto& ents = state.entities;
    float wr = cfg.world_radius;
    float effective_immune = cfg.immune_strength * (0.45f + cfg.immune_pressure);
    std::vector<int> nearby;

    size_t virus_count = 0, bacteria_count = 0, infected_host_count = 0, wbc_count = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.type == BIO_VIRUS) virus_count++;
        if (e.type == BIO_BACTERIUM) bacteria_count++;
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e)) infected_host_count++;
        if (e.type == BIO_WHITE_BLOOD) wbc_count++;
    }

    size_t t_cell_count = 0, b_cell_count = 0;
    for (auto& e : ents) {
        if (!e.alive || e.type != BIO_WHITE_BLOOD) continue;
        if (e.immune_subtype == BIO_IMMUNE_T_CELL) t_cell_count++;
        else if (e.immune_subtype == BIO_IMMUNE_B_CELL) b_cell_count++;
    }

    size_t immune_threat_count = virus_count + bacteria_count / 2 + infected_host_count;
    if (immune_threat_count > wbc_count && ents.size() < cfg.max_entities &&
        randf_range(0.0f, 1.0f) < std::min(0.65f, 0.10f + cfg.immune_pressure * 0.12f)) {
        BioEntity wbc;
        wbc.pos = {randf_range(-wr * 0.8f, wr * 0.8f),
                   randf_range(-wr * 0.8f, wr * 0.8f),
                   randf_range(-wr * 0.8f, wr * 0.8f)};
        wbc.vel = {0, 0, 0};
        wbc.type = BIO_WHITE_BLOOD;
        wbc.genome = rand_u32();
        std::mt19937 wrng(wbc.genome ^ 0xC2B2AE35u);
        randomize_entity_genes(wbc, wrng);
        wbc.energy = type_default_energy(BIO_WHITE_BLOOD) * wbc.genes.energy;
        initialize_entity_lifecycle(wbc, wrng);
        assign_entity_identity(wbc);
        configure_entity_shape(wbc, wrng);
        const char* subtype_name = (wbc.immune_subtype == BIO_IMMUNE_T_CELL) ? "T cell"
            : (wbc.immune_subtype == BIO_IMMUNE_B_CELL) ? "B cell" : "neutrophil";
        char msg[192];
        std::snprintf(msg, sizeof(msg), "Immune system deployed %s (%s).",
                      bio_entity_label(wbc).c_str(), subtype_name);
        ents.push_back(wbc);
        push_event(BIO_EVENT_IMMUNE, msg);
    }

    // B cells produce antibodies — replaces direct antibody spawning
    // Antibodies are now spawned near B cells rather than at random positions
    bool b_cell_antibody_spawned = false;
    if (virus_count > 0 && cfg.immune_pressure > 0.4f && ents.size() < cfg.max_entities &&
        randf_range(0.0f, 1.0f) < std::min(0.45f, cfg.immune_pressure * 0.06f * dt * 60.0f)) {
        // Find a B cell to produce the antibody from
        glm::vec3 spawn_pos = {randf_range(-wr * 0.6f, wr * 0.6f),
                                randf_range(-wr * 0.6f, wr * 0.6f),
                                randf_range(-wr * 0.6f, wr * 0.6f)};
        for (auto& e : ents) {
            if (!e.alive || e.type != BIO_WHITE_BLOOD || e.immune_subtype != BIO_IMMUNE_B_CELL) continue;
            if (e.energy > 30.0f) {
                spawn_pos = e.pos + glm::vec3(randf_range(-12.0f, 12.0f),
                                               randf_range(-12.0f, 12.0f),
                                               randf_range(-12.0f, 12.0f));
                e.energy -= 8.0f;  // B cell energy cost for antibody production
                e.atp -= 5.0f;
                b_cell_antibody_spawned = true;
                break;
            }
        }
        BioEntity antibody;
        antibody.pos = spawn_pos;
        antibody.vel = {0, 0, 0};
        antibody.type = BIO_ANTIBODY;
        antibody.genome = rand_u32();
        std::mt19937 arng(antibody.genome ^ 0x27D4EB2Du);
        randomize_entity_genes(antibody, arng);
        antibody.energy = type_default_energy(BIO_ANTIBODY) * antibody.genes.energy;
        initialize_entity_lifecycle(antibody, arng);
        assign_entity_identity(antibody);
        configure_entity_shape(antibody, arng);
        char msg[192];
        std::snprintf(msg, sizeof(msg), "%s %s.",
                      b_cell_antibody_spawned ? "B cell produced" : "Immune system released",
                      bio_entity_label(antibody).c_str());
        ents.push_back(antibody);
        push_event(BIO_EVENT_IMMUNE, msg);
    }

    for (auto& wbc : ents) {
        if (!wbc.alive || wbc.type != BIO_WHITE_BLOOD) continue;

        float best_dist = 999999.0f;
        int best_idx = -1;
        query_spatial_neighbors(wbc.pos, 260.0f * sensing_gene_scale(wbc), nearby);
        for (int j : nearby) {
            if (!ents[j].alive) continue;
            bool is_target = false;
            bool is_infected_host = (ents[j].type == BIO_CELL || ents[j].type == BIO_BACTERIUM) &&
                                     entity_has_active_infection(ents[j]);
            // T cells preferentially target infected host cells (adaptive immunity)
            if (wbc.immune_subtype == BIO_IMMUNE_T_CELL) {
                is_target = is_infected_host;
                // Also target complement-tagged entities (opsonization)
                if (!is_target && ents[j].complement_tag > 0.5f &&
                    (ents[j].type == BIO_VIRUS || ents[j].type == BIO_BACTERIUM))
                    is_target = true;
            } else if (wbc.immune_subtype == BIO_IMMUNE_B_CELL) {
                // B cells are less aggressive, mainly detect and signal
                is_target = ents[j].type == BIO_VIRUS;
            } else {
                // Generic neutrophils: attack anything hostile
                is_target = ents[j].type == BIO_VIRUS || ents[j].type == BIO_TOXIN || ents[j].type == BIO_BACTERIUM;
                if (!is_target) is_target = is_infected_host;
            }
            if (!is_target) continue;
            float d = glm::length(ents[j].pos - wbc.pos);
            // Complement-tagged targets get priority (smaller effective distance)
            if (ents[j].complement_tag > 0.3f) d *= (1.0f - ents[j].complement_tag * 0.4f);
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
                float mitosis_slow = 1.0f - 0.65f * wbc.mitosis_progress;
                float chase_speed = 60.0f * effective_immune * wbc.genes.seek * mitosis_slow;
                // T cells are faster hunters
                if (wbc.immune_subtype == BIO_IMMUNE_T_CELL) chase_speed *= 1.20f;
                wbc.vel += dir * chase_speed * dt;
                float spd = glm::length(wbc.vel);
                if (spd > 80.0f) wbc.vel *= 80.0f / spd;
            }

            float touch = wbc.radius + ents[best_idx].radius;
            if (dist < touch) {
                std::string target_label = bio_entity_label(ents[best_idx]);
                float defense = defense_gene_scale(ents[best_idx]);
                float clear_chance = std::clamp((0.48f + effective_immune * 0.10f) / defense, 0.18f, 0.98f);
                // T cells are more effective at clearing infected cells
                if (wbc.immune_subtype == BIO_IMMUNE_T_CELL && entity_has_active_infection(ents[best_idx]))
                    clear_chance = std::min(0.98f, clear_chance * 1.35f);
                // Complement tagging boosts clearance
                if (ents[best_idx].complement_tag > 0.3f)
                    clear_chance = std::min(0.98f, clear_chance + ents[best_idx].complement_tag * 0.15f);
                if (randf_range(0.0f, 1.0f) > clear_chance)
                    continue;
                if (ents[best_idx].type == BIO_CELL || ents[best_idx].type == BIO_BACTERIUM)
                    mark_entity_corpse(ents[best_idx], BIO_EVENT_IMMUNE,
                        wbc.immune_subtype == BIO_IMMUNE_T_CELL
                            ? "was killed by a cytotoxic T cell"
                            : "was cleared by a white blood cell");
                else
                    ents[best_idx].alive = false;
                wbc.energy += 10.0f;
                const char* wbc_type = (wbc.immune_subtype == BIO_IMMUNE_T_CELL) ? "T cell"
                    : (wbc.immune_subtype == BIO_IMMUNE_B_CELL) ? "B cell" : "Neutrophil";
                char msg[224];
                std::snprintf(msg, sizeof(msg), "%s (%s) neutralized %s.",
                              bio_entity_label(wbc).c_str(), wbc_type, target_label.c_str());
                push_event(BIO_EVENT_IMMUNE, msg);
            }
        }

        wbc.energy -= (0.3f + cfg.toxicity * 0.5f) * dt;
        if (wbc.energy <= 0.0f)
            mark_entity_corpse(wbc, BIO_EVENT_LIFECYCLE, "failed and became a dead husk");
    }

    for (auto& antibody : ents) {
        if (!antibody.alive || antibody.type != BIO_ANTIBODY) continue;

        float best_dist = cfg.infection_radius * sensing_gene_scale(antibody);
        int best_idx = -1;
        query_spatial_neighbors(antibody.pos, cfg.infection_radius * sensing_gene_scale(antibody) + 8.0f, nearby);
        for (int j : nearby) {
            if (!ents[j].alive || ents[j].type != BIO_VIRUS) continue;
            float d = glm::length(ents[j].pos - antibody.pos);
            if (d < best_dist) {
                best_dist = d;
                best_idx = (int)j;
            }
        }

        if (best_idx >= 0 && best_dist < antibody.radius + ents[best_idx].radius + 3.0f) {
            std::string target_label = bio_entity_label(ents[best_idx]);
            float neutralize_chance = std::clamp((0.62f + effective_immune * 0.08f) /
                                                 std::max(0.35f, defense_gene_scale(ents[best_idx])), 0.18f, 0.98f);
            if (randf_range(0.0f, 1.0f) > neutralize_chance) {
                antibody.energy -= 5.0f;
                continue;
            }
            ents[best_idx].alive = false;
            antibody.energy -= 10.0f;
            char msg[224];
            std::snprintf(msg, sizeof(msg), "%s bound and neutralized %s.",
                          bio_entity_label(antibody).c_str(), target_label.c_str());
            push_event(BIO_EVENT_IMMUNE, msg);
        }

        antibody.energy -= 0.45f * dt;
        if (antibody.energy <= 0.0f)
            mark_entity_corpse(antibody, BIO_EVENT_LIFECYCLE, "failed and became a dead husk");
    }
}

void BiochemApp::process_phagocyte_cleanup(float dt) {
    auto& ents = state.entities;
    float wr = cfg.world_radius;
    std::vector<int> nearby;

    size_t corpse_count = 0;
    size_t phagocyte_count = 0;
    for (const auto& e : ents) {
        if (e.corpse) corpse_count++;
        if (e.alive && e.type == BIO_JANITOR) phagocyte_count++;
    }

    if (corpse_count > phagocyte_count * 2 && ents.size() < cfg.max_entities &&
        randf_range(0.0f, 1.0f) < std::min(0.55f, 0.04f + corpse_count * 0.025f) * dt * 60.0f) {
        BioEntity phagocyte;
        phagocyte.pos = {randf_range(-wr * 0.75f, wr * 0.75f),
                         randf_range(-wr * 0.75f, wr * 0.75f),
                         randf_range(-wr * 0.75f, wr * 0.75f)};
        phagocyte.vel = {0, 0, 0};
        phagocyte.type = BIO_JANITOR;
        phagocyte.genome = rand_u32();
        std::mt19937 jrng(phagocyte.genome ^ 0x9E3779B9u);
        randomize_entity_genes(phagocyte, jrng);
        phagocyte.energy = type_default_energy(BIO_JANITOR) * phagocyte.genes.energy;
        initialize_entity_lifecycle(phagocyte, jrng);
        assign_entity_identity(phagocyte);
        configure_entity_shape(phagocyte, jrng);
        ents.push_back(phagocyte);

        char msg[192];
        std::snprintf(msg, sizeof(msg), "Cleanup system recruited %s to scavenge dead husks.",
                      bio_entity_label(phagocyte).c_str());
        push_event(BIO_EVENT_LIFECYCLE, msg);
    }

    for (auto& phagocyte : ents) {
        if (!phagocyte.alive || phagocyte.type != BIO_JANITOR) continue;

        float best_dist = 999999.0f;
        int best_idx = -1;
        query_spatial_neighbors(phagocyte.pos, 260.0f * sensing_gene_scale(phagocyte), nearby, true);
        for (int j : nearby) {
            if (!ents[j].corpse) continue;
            float d = glm::length(ents[j].pos - phagocyte.pos);
            if (d < best_dist) {
                best_dist = d;
                best_idx = (int)j;
            }
        }

        if (best_idx >= 0) {
            glm::vec3 dir = ents[best_idx].pos - phagocyte.pos;
            float dist = glm::length(dir);
            if (dist > 1.0f) {
                dir /= dist;
                phagocyte.vel += dir * (52.0f * phagocyte.genes.seek) * dt;
            }

            float touch = phagocyte.radius + ents[best_idx].radius * 0.8f;
            if (dist < touch) {
                std::string husk_label = bio_entity_label(ents[best_idx]);
                ents[best_idx].corpse = false;
                ents[best_idx].alive = false;
                phagocyte.energy += 12.0f;
                feed_entity(phagocyte, 0.30f, 5.0f);

                char msg[224];
                std::snprintf(msg, sizeof(msg), "%s removed the dead husk of %s.",
                              bio_entity_label(phagocyte).c_str(), husk_label.c_str());
                push_event(BIO_EVENT_LIFECYCLE, msg);
            }
        } else {
            phagocyte.vel += glm::vec3(randf_range(-1.0f, 1.0f),
                                       randf_range(-1.0f, 1.0f),
                                       randf_range(-1.0f, 1.0f)) * dt * 6.0f;
        }
    }
}

// ── AI Movement ─────────────────────────────────────────────────────────────

void BiochemApp::process_ai_movement(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();
    std::vector<int> nearby;

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;

        float max_speed = 0.0f;
        float seek_force = cfg.seek_strength * e.genes.seek;
        float flee_force = cfg.flee_strength * e.genes.flee;
        float spacing_force = cfg.spacing_strength * e.genes.spacing;
        float brownian_force = cfg.brownian_strength * e.genes.brownian;
        float sensing = sensing_gene_scale(e);
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_WHITE_BLOOD) &&
            e.mitosis_progress > 0.0f) {
            float slowdown = (e.type == BIO_BACTERIUM)
                ? (1.0f - 0.52f * e.mitosis_progress)
                : (1.0f - 0.65f * e.mitosis_progress);
            seek_force *= slowdown;
            flee_force *= slowdown;
            spacing_force *= 0.65f + 0.35f * slowdown;
            brownian_force *= 0.45f + 0.55f * slowdown;
        }
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e)) {
            float infection_slow = 1.0f - std::min(0.72f, dominant_infection_progress(e) * 0.72f);
            seek_force *= infection_slow;
            flee_force *= 0.85f * infection_slow;
            spacing_force *= 0.70f + 0.30f * infection_slow;
            brownian_force *= 0.55f + 0.45f * infection_slow;
        }

        if (e.type == BIO_CELL || e.type == BIO_BACTERIUM) {
            max_speed = (e.type == BIO_CELL) ? 60.0f : 80.0f;

            // Single spatial query covers all behaviors (food, host, threat, spacing)
            float max_query_radius = std::max({150.0f * sensing, 180.0f * sensing,
                                                100.0f * sensing, std::max(48.0f, e.radius * 4.0f)});
            query_spatial_neighbors(e.pos, max_query_radius, nearby);

            float best_food_dist = 150.0f * sensing;
            int best_food = -1;
            float best_host_score = 0.0f;
            int best_host = -1;
            float best_threat_dist = 100.0f * sensing;
            int best_threat = -1;
            float spacing_radius = std::max(48.0f, e.radius * 4.0f);

            for (int j : nearby) {
                if (!ents[j].alive) continue;
                float d = glm::length(ents[j].pos - e.pos);

                // Food check
                if (ents[j].type == BIO_NUTRIENT && d < best_food_dist) {
                    best_food_dist = d; best_food = j;
                }
                // Threat check
                if (d < 100.0f * sensing) {
                    bool is_threat = (ents[j].type == BIO_VIRUS || ents[j].type == BIO_TOXIN);
                    if (!is_threat && e.type == BIO_CELL && ents[j].type == BIO_BACTERIUM)
                        is_threat = bacteria_host_tropism(ents[j], e) > 0.85f;
                    if (is_threat && d < best_threat_dist) {
                        best_threat_dist = d; best_threat = j;
                    }
                }
                // Host check (bacteria only)
                if (e.type == BIO_BACTERIUM && ents[j].type == BIO_CELL && d <= 180.0f * sensing && d > 1.0f) {
                    if (!entity_has_bacterial_infection(ents[j])) {
                        float tropism = bacteria_host_tropism(e, ents[j]);
                        if (tropism > 0.0f) {
                            float score = tropism * (1.0f - d / (180.0f * sensing));
                            if (score > best_host_score) {
                                best_host_score = score; best_host = j;
                            }
                        }
                    }
                }
                // Spacing check
                if (d < spacing_radius && static_cast<size_t>(j) != i && ents[j].type == e.type) {
                    float min_dist = (e.radius + ents[j].radius) * (1.35f + 0.65f * e.genes.spacing);
                    if (d < min_dist && d > 0.1f)
                        e.vel += glm::normalize(e.pos - ents[j].pos) * spacing_force * dt;
                }
            }

            if (best_food >= 0) {
                glm::vec3 dir = ents[best_food].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * dt;
            }
            if (best_host >= 0) {
                glm::vec3 dir = ents[best_host].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f) {
                    float colonize_bias = 0.14f + (1.0f - std::min(1.0f, e.nutrient_reserve)) * 0.26f;
                    e.vel += glm::normalize(dir) * seek_force * colonize_bias * dt;
                }
            }
            if (best_threat >= 0) {
                glm::vec3 away = e.pos - ents[best_threat].pos;
                float d = glm::length(away);
                if (d > 1.0f)
                    e.vel += glm::normalize(away) * flee_force * dt;
            }
        }
        else if (e.type == BIO_VIRUS) {
            // Viruses are passive particles — they don't actively seek hosts.
            // Movement is primarily Brownian diffusion with weak drift toward
            // nearby cells (modeling receptor-mediated adhesion at close range).
            max_speed = 35.0f;
            // Brownian diffusion (dominant movement mode for viruses)
            float rx = randf_range(-1.0f, 1.0f);
            float ry = randf_range(-1.0f, 1.0f);
            float rz = randf_range(-1.0f, 1.0f);
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * 1.2f * dt;
            // Very weak short-range drift toward nearby host cells
            // (models receptor binding probability increasing at close proximity)
            float detect_range = 25.0f * sensing;
            int best = -1;
            float best_dist = detect_range;
            query_spatial_neighbors(e.pos, detect_range, nearby);
            for (int j : nearby) {
                if (!ents[j].alive || (ents[j].type != BIO_CELL && ents[j].type != BIO_BACTERIUM)) continue;
                if (!virus_targets_host(e, ents[j])) continue;
                if (entity_has_viral_infection(ents[j])) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (d < best_dist) { best_dist = d; best = (int)j; }
            }
            if (best >= 0) {
                glm::vec3 dir = ents[best].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * 0.15f * dt;
            }
        }
        else if (e.type == BIO_RED_BLOOD) {
            max_speed = 40.0f;
            // Brownian motion
            float rx = randf_range(-1.0f, 1.0f);
            float ry = randf_range(-1.0f, 1.0f);
            float rz = randf_range(-1.0f, 1.0f);
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * dt;
        }
        else if (e.type == BIO_NUTRIENT) {
            max_speed = 15.0f;
            // Gentle drift
            float rx = randf_range(-1.0f, 1.0f);
            float ry = randf_range(-1.0f, 1.0f);
            float rz = randf_range(-1.0f, 1.0f);
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * 0.3f * dt;
        }
        else if (e.type == BIO_ANTIBODY) {
            max_speed = 70.0f;
            // Seek nearest virus
            float best_dist = 120.0f * sensing;
            int best = -1;
            query_spatial_neighbors(e.pos, best_dist, nearby);
            for (int j : nearby) {
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
        else if (e.type == BIO_JANITOR) {
            max_speed = 68.0f;
            float rx = randf_range(-1.0f, 1.0f);
            float ry = randf_range(-1.0f, 1.0f);
            float rz = randf_range(-1.0f, 1.0f);
            e.vel += glm::vec3(rx, ry, rz) * brownian_force * 0.25f * dt;
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
    std::vector<int> nearby;

    for (size_t i = 0; i < n; i++) {
        if (!ents[i].alive) continue;
        query_spatial_neighbors(ents[i].pos, ents[i].radius + spatial_max_radius_ + 2.0f, nearby);
        for (int j_idx : nearby) {
            if (j_idx <= static_cast<int>(i) || !ents[j_idx].alive) continue;

            glm::vec3 diff = ents[j_idx].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j_idx].radius;

            if (dist < touch && dist > 0.1f) {
                float overlap = touch - dist;
                glm::vec3 dir = diff / dist;
                float push = overlap * 0.5f;
                ents[i].pos -= dir * push;
                ents[j_idx].pos += dir * push;
                ents[i].vel -= dir * push * 2.0f;
                ents[j_idx].vel += dir * push * 2.0f;
            }
        }
    }
}

void BiochemApp::process_structure_collision() {
    auto& ents = state.entities;
    auto colliders = build_colliders(environment_);
    if (colliders.empty()) return;

    for (auto& e : ents) {
        if (!e.alive && !e.corpse) continue;
        // Run up to 3 iterations to resolve multi-structure penetration
        for (int iter = 0; iter < 3; ++iter) {
            bool any_hit = false;
            for (const auto& sc : colliders)
                if (resolve_structure_collision(sc, e.pos, e.vel, e.radius))
                    any_hit = true;
            if (!any_hit) break;
        }
    }
}

// ── Overlay rendering (selection highlight via DrawList) ────────────────────

void BiochemApp::render_overlay() {
    if (show_splash || show_pause_menu) return;
    if (selected_entity < 0 || selected_entity >= (int)state.entities.size()) return;

    const auto& e = state.entities[selected_entity];
    if (!e.alive && !e.corpse) { selected_entity = -1; return; }

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
            autospawn_timer_ = 0.0f;
            sim_time_ = 0.0f;
            next_entity_id_ = 1;
            reset_population_metrics();
            event_log_.clear();
            event_log_dirty_ = false;
            push_event(BIO_EVENT_SYSTEM, "Simulation cleared.");
            show_pause_menu = false;
            paused = false;
        }

        // Settings
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Settings", ImVec2(btn_w, btn_h))) {
            show_settings_menu_ = true;
        }

        // Return to Launcher
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Return to Launcher", ImVec2(btn_w, btn_h))) {
            request_launcher = true;
            request_quit = true;
        }

        // Quit — red tinted
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if (!show_settings_menu_) {
            const char* hint = "Press Escape to resume";
            ImVec2 hint_size = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(cx - hint_size.x * 0.5f, H - 60.0f),
                IM_COL32(140, 170, 150, 100), hint);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Settings overlay (separate fullscreen window on top of pause menu)
    if (show_settings_menu_) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(W, H));
        ImGuiWindowFlags sflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.04f, 0.06f, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##BiochemSettings", nullptr, sflags)) {
            if (draw_app_settings_menu(app_settings_, settings_tab_, W, H)) {
                show_settings_menu_ = false;
                save_app_settings(app_settings_, "biochem_settings.ppcfg");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

// ── Spawn menu ──────────────────────────────────────────────────────────────

void BiochemApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGui::SetNextWindowPos({10, 400}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280, 430}, ImGuiCond_FirstUseEver);
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
                spawn_energy_ = type_default_energy((uint32_t)t);
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

    if (ImGui::CollapsingHeader("Autospawn", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* autospawn_modes[] = {"Static", "Dynamic"};
        size_t matching_alive = count_alive_matching_spawn_selection();
        float effective_rate = compute_autospawn_rate(matching_alive);

        ImGui::Checkbox("Enable Autospawn", &cfg.autospawn_enabled);
        int autospawn_mode = static_cast<int>(cfg.autospawn_mode);
        if (ImGui::Combo("Mode", &autospawn_mode, autospawn_modes, BIO_AUTOSPAWN_MODE_COUNT))
            cfg.autospawn_mode = static_cast<uint32_t>(autospawn_mode);
        ImGui::SliderFloat("Static Rate", &cfg.autospawn_static_rate, 0.0f, 12.0f, "%.2f/sec");
        if (cfg.autospawn_mode == BIO_AUTOSPAWN_DYNAMIC) {
            ImGui::SliderFloat("Dynamic Response", &cfg.autospawn_dynamic_rate, 0.0f, 12.0f, "%.2fx");
            int target_alive = static_cast<int>(cfg.autospawn_target_alive);
            if (ImGui::SliderInt("Target Alive", &target_alive, 1, 250))
                cfg.autospawn_target_alive = static_cast<uint32_t>(std::max(1, target_alive));
        }
        ImGui::Text("Selection alive: %zu", matching_alive);
        ImGui::Text("Effective rate: %.2f/sec", effective_rate);
        ImGui::TextDisabled("Autospawn uses the currently selected type and variant.");
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

    if (ImGui::CollapsingHeader("Quick Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        float wr = cfg.world_radius * 0.5f;
        auto randf = [](float lo, float hi) {
            return randf_range(lo, hi);
        };

        if (ImGui::Button("Add Cell Colony (10)", ImVec2(-1, 0))) {
            auto colliders = build_colliders(environment_);
            for (int i = 0; i < 10; i++) {
                if (state.entities.size() >= cfg.max_entities)
                    break;
                BioEntity e;
                float angle = (float)i / 10.0f * 6.2832f;
                e.pos = {cosf(angle) * 40.0f, sinf(angle) * 40.0f, randf(-20, 20)};
                e.vel = {cosf(angle) * 5.0f, sinf(angle) * 5.0f, 0};
                e.type = BIO_CELL;
                e.morphology = i % BIO_CELL_VARIANT_COUNT;
                e.genome = rand_u32();
                std::mt19937 rng(e.genome ^ (uint32_t)i);
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_CELL) * e.genes.energy;
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
                configure_entity_shape(e, rng);
                e.pos = push_out_of_structures(colliders, e.pos, e.radius);
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Virus Outbreak (8)", ImVec2(-1, 0))) {
            auto colliders = build_colliders(environment_);
            for (int i = 0; i < 8; i++) {
                if (state.entities.size() >= cfg.max_entities)
                    break;
                BioEntity e;
                float angle = (float)i / 8.0f * 6.2832f;
                float phi = randf(-0.5f, 0.5f);
                e.pos = {cosf(angle) * 60.0f, sinf(angle) * 60.0f, sinf(phi) * 40.0f};
                e.vel = {cosf(angle) * 30.0f, sinf(angle) * 30.0f, 0};
                e.type = BIO_VIRUS;
                e.morphology = i % BIO_VIRUS_VARIANT_COUNT;
                e.genome = rand_u32();
                std::mt19937 rng(e.genome ^ (uint32_t)(i * 13));
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_VIRUS) * e.genes.energy;
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
                configure_entity_shape(e, rng);
                e.pos = push_out_of_structures(colliders, e.pos, e.radius);
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Nutrient Burst (20)", ImVec2(-1, 0))) {
            for (int i = 0; i < 20; i++)
                spawn_nutrient();
        }

        if (ImGui::Button("Immune Response (5 WBC)", ImVec2(-1, 0))) {
            auto colliders = build_colliders(environment_);
            for (int i = 0; i < 5; i++) {
                if (state.entities.size() >= cfg.max_entities)
                    break;
                BioEntity e;
                e.pos = {randf(-wr, wr), randf(-wr, wr), randf(-wr, wr)};
                e.type = BIO_WHITE_BLOOD;
                e.genome = rand_u32();
                std::mt19937 rng(e.genome ^ (uint32_t)(i * 31));
                randomize_entity_genes(e, rng);
                e.energy = type_default_energy(BIO_WHITE_BLOOD) * e.genes.energy;
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
                configure_entity_shape(e, rng);
                e.pos = push_out_of_structures(colliders, e.pos, e.radius);
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
    ImGui::SetNextWindowSize({io.DisplaySize.x, 32});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.06f, 0.08f, 0.92f));
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextColored({0.35f, 0.90f, 0.45f, 1.0f}, "Biochem Sim");
    ImGui::SameLine(0, 6);
    ImGui::TextColored({0.40f, 0.78f, 0.88f, 1.0f}, "%s", BIO_ENVIRONMENT_NAMES[cfg.environment % BIO_ENV_COUNT]);
    ImGui::SameLine(0, 12);
    ImGui::TextColored({0.60f, 0.60f, 0.70f, 1.0f}, "%zu alive", state.count_alive());
    ImGui::SameLine(0, 8);
    ImGui::TextColored({0.55f, 0.48f, 0.42f, 1.0f}, "%zu dead", state.count_corpses());
    if (cfg.autospawn_enabled) {
        ImGui::SameLine(0, 8);
        ImGui::TextColored({0.55f, 0.82f, 0.42f, 1.0f}, "Auto %.1f/s",
            compute_autospawn_rate(count_alive_matching_spawn_selection()));
    }
    ImGui::SameLine(0, 12);
    if (ImGui::SmallButton(paused ? "> Resume" : "|| Pause"))
        paused = !paused;
    ImGui::SameLine(0, 8);
    ImGui::TextColored({0.45f, 0.45f, 0.55f, 1.0f}, "%.0f fps", io.Framerate);
    // Right-aligned help text
    float help_width = ImGui::CalcTextSize("WASD:move  Scroll:zoom  Click:inspect  Esc:menu").x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > help_width + 20.0f) {
        ImGui::SameLine(io.DisplaySize.x - help_width - 16.0f);
        ImGui::TextColored({0.35f, 0.35f, 0.45f, 1.0f}, "WASD:move  Scroll:zoom  Click:inspect  Esc:menu");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Settings panel
    if (settings_visible_) {
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({320, 620}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings");
    const BioEnvironmentPreset& env = bio_environment_preset(static_cast<BioEnvironmentType>(cfg.environment));

    // Helper for tooltips on hover
    auto tooltip = [](const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(280.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // ── Environment Selection ──
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        int environment = static_cast<int>(cfg.environment);
        if (ImGui::Combo("##Biome", &environment, BIO_ENVIRONMENT_NAMES, BIO_ENV_COUNT))
            apply_environment_preset(static_cast<BioEnvironmentType>(environment), false);
        tooltip("Select the biological environment to simulate");
        ImGui::TextWrapped("%s", env.summary);
        ImGui::TextColored({0.5f, 0.5f, 0.6f, 1.0f}, "Seed: %u", cfg.environment_seed);
        if (ImGui::Button("Apply Preset + Reseed", ImVec2(-1, 0)))
            apply_environment_preset(static_cast<BioEnvironmentType>(cfg.environment), true);
        tooltip("Reset all parameters to preset defaults and regenerate population");
        if (ImGui::Button("Regenerate Terrain", ImVec2(-1, 0)))
            regenerate_environment(true);
        tooltip("Rebuild structures and features with a new random seed");
        ImGui::TextColored({0.45f, 0.60f, 0.70f, 1.0f},
            "Features: %zu  (M:%zu N:%zu T:%zu C:%zu S:%zu)",
            environment_.count(),
            environment_.count_type(BIO_ENV_FEATURE_MEMBRANE),
            environment_.count_type(BIO_ENV_FEATURE_NUTRIENT),
            environment_.count_type(BIO_ENV_FEATURE_TOXIN),
            environment_.count_type(BIO_ENV_FEATURE_CURRENT),
            environment_.count_type(BIO_ENV_FEATURE_STRUCTURE));
    }

    // ── Environmental Conditions ──
    if (ImGui::CollapsingHeader("Conditions")) {
        ImGui::SliderFloat("Temperature", &cfg.temperature_c, 4.0f, 42.0f, "%.1f C");
        tooltip("Environmental temperature in Celsius. Affects metabolic rates and organism viability.");
        ImGui::SliderFloat("pH", &cfg.acidity_ph, 5.0f, 8.4f, "%.2f");
        tooltip("Acidity/alkalinity. 7.0 = neutral, <7 = acidic, >7 = alkaline.");
        ImGui::SliderFloat("Oxygen", &cfg.oxygen_level, 0.0f, 1.2f, "%.2f");
        tooltip("Dissolved oxygen level. 0 = anaerobic, 1.0 = fully oxygenated.");
        ImGui::SliderFloat("Nutrients", &cfg.nutrient_density, 0.2f, 2.0f, "%.2f");
        tooltip("Background nutrient concentration in the environment.");
        ImGui::SliderFloat("Toxicity", &cfg.toxicity, 0.0f, 0.5f, "%.2f");
        tooltip("Level of harmful chemicals or metabolic waste products.");
        ImGui::SliderFloat("Flow", &cfg.flow_strength, 0.0f, 80.0f, "%.1f");
        tooltip("Fluid flow strength (airflow, blood flow, currents).");
        ImGui::SliderFloat("Immune Pressure", &cfg.immune_pressure, 0.0f, 3.0f, "%.2f");
        tooltip("Strength of immune system activity against pathogens.");
        ImGui::SliderFloat("Damping", &cfg.fluid_damping, 0.92f, 0.995f, "%.3f");
        tooltip("Fluid viscosity damping. Lower = thicker fluid, slower movement.");
    }

    // ── Biology Settings ──
    if (ImGui::CollapsingHeader("Biology")) {
        ImGui::SliderFloat("Nutrient Rate", &cfg.nutrient_rate, 0.1f, 10.0f, "%.1f/s");
        tooltip("Rate at which new nutrient particles spawn per second.");
        ImGui::SliderFloat("Metabolism", &cfg.metabolism_rate, 0.1f, 5.0f, "%.2f");
        tooltip("Global metabolic rate multiplier. Higher = faster energy consumption.");
        ImGui::SliderFloat("Division Energy", &cfg.division_energy, 50.0f, 300.0f, "%.0f");
        tooltip("Minimum energy a cell needs before it can divide.");
        ImGui::SliderFloat("Mutation Rate", &cfg.mutation_rate, 0.0f, 0.1f, "%.3f");
        tooltip("Probability of genetic mutation per cell division event.");
        ImGui::Separator();
        ImGui::SliderFloat("Infection Radius", &cfg.infection_radius, 5.0f, 50.0f, "%.1f");
        tooltip("Maximum distance at which a virus can infect a host cell.");
        ImGui::SliderFloat("Infection Rate", &cfg.infection_rate, 0.1f, 2.0f, "%.2f");
        tooltip("Probability multiplier for successful infection per contact.");
        ImGui::SliderFloat("Immune Strength", &cfg.immune_strength, 0.1f, 5.0f, "%.2f");
        tooltip("Effectiveness of antibodies and white blood cells.");
        ImGui::SliderFloat("Antibiotic Vis.", &cfg.antibiotic_visibility, 0.4f, 3.0f, "%.2f");
        tooltip("Visual size of antibiotic secretion halos around bacteria.");
        ImGui::Separator();
        ImGui::Checkbox("Immune System", &cfg.immune_system);
        tooltip("Enable/disable immune cell spawning and antibody production.");
        ImGui::Checkbox("Energy Bars", &cfg.show_energy_bars);
        tooltip("Show health/energy bars above entities.");
    }

    // ── AI & Movement ──
    if (ImGui::CollapsingHeader("Movement")) {
        ImGui::Checkbox("Enable AI", &cfg.ai_movement);
        tooltip("Toggle intelligent organism movement (chemotaxis, flee, seek).");
        if (cfg.ai_movement) {
            ImGui::SliderFloat("Seek", &cfg.seek_strength, 0.0f, 100.0f, "%.0f");
            tooltip("Strength of nutrient-seeking behavior.");
            ImGui::SliderFloat("Flee", &cfg.flee_strength, 0.0f, 100.0f, "%.0f");
            tooltip("Strength of threat-avoidance behavior.");
            ImGui::SliderFloat("Spacing", &cfg.spacing_strength, 0.0f, 50.0f, "%.0f");
            tooltip("Repulsion force between same-type organisms.");
            ImGui::SliderFloat("Brownian", &cfg.brownian_strength, 0.0f, 50.0f, "%.0f");
            tooltip("Random thermal motion intensity.");
        }
    }

    // ── Simulation ──
    if (ImGui::CollapsingHeader("Simulation")) {
        int max_entities = static_cast<int>(cfg.max_entities);
        if (ImGui::SliderInt("Max Entities", &max_entities, 500, 20000))
            cfg.max_entities = static_cast<uint32_t>(std::max(500, max_entities));
        tooltip("Maximum number of entities allowed in the simulation.");
        ImGui::SliderFloat("Viscosity", &cfg.viscosity, 0.90f, 1.0f, "%.3f");
        tooltip("Global velocity damping. 1.0 = no damping, lower = more friction.");
        ImGui::SliderFloat("Time Scale", &cfg.dt_scale, 0.1f, 5.0f, "%.1fx");
        tooltip("Simulation speed multiplier. 1.0 = real-time.");
        ImGui::SliderFloat("World Radius", &cfg.world_radius, 50.0f, 500.0f, "%.0f");
        tooltip("Size of the simulation volume. Entities wrap at the boundary.");
        ImGui::SliderFloat("Ambient Light", &cfg.ambient_strength, 0.0f, 0.5f, "%.2f");
        tooltip("Minimum lighting level for all surfaces.");
        if (ImGui::Button("Reset Camera", ImVec2(-1, 0)))
            reset_camera_pose();
    }

    ImGui::End();
    } // settings_visible_

    // Shared arrays for population + inspector
    static const char* pop_type_names[] = {
        "Cells", "Bacteria", "Viruses", "Nutrients",
        "Toxins", "Antibodies", "Red Blood", "White Blood", "Phagocytes"
    };
    static const ImVec4 type_colors_v[] = {
        {0.3f,0.7f,1.0f,1}, {0.9f,0.6f,0.2f,1}, {0.9f,0.2f,0.2f,1}, {0.3f,0.9f,0.3f,1},
        {0.8f,0.2f,0.8f,1}, {1.0f,1.0f,0.3f,1}, {0.9f,0.3f,0.3f,1}, {1.0f,1.0f,1.0f,1},
        {0.47f,0.86f,0.70f,1}
    };

    // Population stats
    if (population_visible_) {
    ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 440}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Population");
    using GenSet = std::set<uint32_t>;
    std::array<size_t, BIO_TYPE_COUNT> alive_counts{};
    std::array<size_t, BIO_TYPE_COUNT> infected_counts{};
    std::array<size_t, BIO_TYPE_COUNT> pathogen_active_counts{};
    std::array<GenSet, BIO_TYPE_COUNT> generation_sets;
    std::array<std::vector<size_t>, BIO_TYPE_COUNT> subtype_alive;
    std::array<std::vector<size_t>, BIO_TYPE_COUNT> subtype_infected;
    std::array<std::vector<size_t>, BIO_TYPE_COUNT> subtype_pathogen_active;
    std::array<std::vector<GenSet>, BIO_TYPE_COUNT> subtype_generations;
    for (int t = 0; t < BIO_TYPE_COUNT; ++t) {
        uint32_t count = type_variant_count(static_cast<uint32_t>(t));
        subtype_alive[t].assign(count, 0);
        subtype_infected[t].assign(count, 0);
        subtype_pathogen_active[t].assign(count, 0);
        subtype_generations[t].assign(count, {});
    }

    size_t total_alive = 0;
    size_t current_infected_total = 0;
    for (const auto& e : state.entities) {
        if (!e.alive)
            continue;
        int type = static_cast<int>(e.type % BIO_TYPE_COUNT);
        alive_counts[type] += 1;
        total_alive += 1;
        generation_sets[type].insert(e.generation);
        uint32_t subtype_count = type_variant_count(e.type);
        if (subtype_count > 0) {
            uint32_t subtype = e.morphology % subtype_count;
            subtype_alive[type][subtype] += 1;
            subtype_generations[type][subtype].insert(e.generation);
            if (entity_has_active_infection(e)) {
                subtype_infected[type][subtype] += 1;
            }
        }
        if (entity_has_active_infection(e)) {
            infected_counts[type] += 1;
            current_infected_total += 1;
        }
        if (entity_has_viral_infection(e)) {
            pathogen_active_counts[BIO_VIRUS] += 1;
            subtype_pathogen_active[BIO_VIRUS][e.viral_infection.morphology % BIO_VIRUS_VARIANT_COUNT] += 1;
        }
        if (entity_has_bacterial_infection(e)) {
            pathogen_active_counts[BIO_BACTERIUM] += 1;
            subtype_pathogen_active[BIO_BACTERIUM][e.bacterial_infection.morphology % BIO_BACTERIA_VARIANT_COUNT] += 1;
        }
    }
    ImGui::Separator();
    ImGui::Text("Alive: %zu", total_alive);
    ImGui::Text("Current infected: %zu", current_infected_total);
    ImGui::Text("Ever infected: %u", ever_infected_total_);
    ImGui::Text("Husks: %zu", state.count_corpses());
    ImGui::Separator();
    ImGui::BeginChild("##PopulationRollup", ImVec2(0, 0), true);
    for (int t = 0; t < BIO_TYPE_COUNT; ++t) {
        if (alive_counts[t] == 0 && ever_infected_by_type_[t] == 0)
            continue;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        bool open = ImGui::TreeNodeEx((void*)(intptr_t)t, flags, "%s", pop_type_names[t]);
        ImGui::SameLine();
        ImGui::TextColored(type_colors_v[t], "%zu alive", alive_counts[t]);
        if (open) {
            ImGui::Text("Current infected hosts: %zu", infected_counts[t]);
            ImGui::Text("Ever infected hosts: %u", ever_infected_by_type_[t]);
            if (t == BIO_VIRUS || t == BIO_BACTERIUM) {
                ImGui::Text("Current host infections caused: %zu", pathogen_active_counts[t]);
                ImGui::Text("Ever host infections caused: %u", ever_pathogen_infections_by_type_[t]);
            }
            ImGui::TextWrapped("Alive generations: %s", format_generation_set(generation_sets[t]).c_str());
            uint32_t subtype_count = type_variant_count(static_cast<uint32_t>(t));
            for (uint32_t subtype = 0; subtype < subtype_count; ++subtype) {
                uint32_t ever_host = 0;
                auto host_it = ever_infected_by_subtype_.find(type_subtype_key(static_cast<uint32_t>(t), subtype));
                if (host_it != ever_infected_by_subtype_.end())
                    ever_host = host_it->second;
                uint32_t ever_pathogen = 0;
                auto pathogen_it = ever_pathogen_infections_by_subtype_.find(type_subtype_key(static_cast<uint32_t>(t), subtype));
                if (pathogen_it != ever_pathogen_infections_by_subtype_.end())
                    ever_pathogen = pathogen_it->second;
                if (subtype_alive[t][subtype] == 0 && ever_host == 0 && ever_pathogen == 0)
                    continue;
                if (t == BIO_VIRUS || t == BIO_BACTERIUM) {
                    ImGui::BulletText("%s | alive %zu | infected-hosts %zu | caused-now %zu | caused-ever %u | gens %s",
                        bio_entity_variant_name(static_cast<uint32_t>(t), subtype),
                        subtype_alive[t][subtype],
                        subtype_infected[t][subtype],
                        subtype_pathogen_active[t][subtype],
                        ever_pathogen,
                        format_generation_set(subtype_generations[t][subtype]).c_str());
                } else {
                    ImGui::BulletText("%s | alive %zu | infected %zu | ever %u | gens %s",
                        bio_entity_variant_name(static_cast<uint32_t>(t), subtype),
                        subtype_alive[t][subtype],
                        subtype_infected[t][subtype],
                        ever_host,
                        format_generation_set(subtype_generations[t][subtype]).c_str());
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::End();
    } // population_visible_

    if (genomics_visible_) {
    ImGui::SetNextWindowPos({std::max(330.0f, io.DisplaySize.x - 590.0f), 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 440}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Genomic Stats", &genomics_visible_)) {
        std::array<BioGeneAggregate, BIO_TYPE_COUNT> type_gene_stats{};
        std::array<std::vector<BioGeneAggregate>, BIO_TYPE_COUNT> subtype_gene_stats;
        size_t sampled_alive = 0;
        for (int t = 0; t < BIO_TYPE_COUNT; ++t)
            subtype_gene_stats[t].assign(type_variant_count(static_cast<uint32_t>(t)), {});

        for (const auto& e : state.entities) {
            if (!e.alive || !type_has_genomic_panel(e.type))
                continue;
            sampled_alive += 1;
            uint32_t type = e.type % BIO_TYPE_COUNT;
            accumulate_gene_aggregate(type_gene_stats[type], e.genes);
            uint32_t subtype_count = type_variant_count(type);
            if (subtype_count > 0)
                accumulate_gene_aggregate(subtype_gene_stats[type][e.morphology % subtype_count], e.genes);
        }

        auto draw_avg_lines = [&](const BioGeneAggregate& agg, uint32_t type) {
            BioGenes avg = average_genes(agg);
            ImGui::Text("Seek %.2f  Flee %.2f  Space %.2f  Brown %.2f",
                        avg.seek, avg.flee, avg.spacing, avg.brownian);
            if (type_feeds_on_nutrients(type))
                ImGui::Text("Energy %.2f  Metab %.2f  Nutr %.2f  Sense %.2f",
                            avg.energy, avg.metabolism_efficiency, avg.nutrient_affinity, avg.sensing);
            else
                ImGui::Text("Energy %.2f  Stress %.2f  Def %.2f  Sense %.2f",
                            avg.energy, avg.stress_tolerance, avg.defense, avg.sensing);
            if (type_feeds_on_nutrients(type) && gene_trait_applicable(type, 12))
                ImGui::Text("Stress %.2f  Def %.2f  MutStab %.2f",
                            avg.stress_tolerance, avg.defense, avg.mutation_stability);
            else if (type_feeds_on_nutrients(type))
                ImGui::Text("Stress %.2f  Def %.2f",
                            avg.stress_tolerance, avg.defense);
            else if (gene_trait_applicable(type, 12))
                ImGui::Text("Metab %.2f  MutStab %.2f",
                            avg.metabolism_efficiency, avg.mutation_stability);
            else
                ImGui::Text("Metab %.2f", avg.metabolism_efficiency);
            if (type_uses_telomeres(type))
                ImGui::Text("Tel %.2f  Cycle %.2f", avg.telomere, avg.mitotic_clock);
            else
                ImGui::Text("Cycle %.2f", avg.mitotic_clock);
            if (type == BIO_BACTERIUM) {
                ImGui::Text("AntiType %.2f  AntiYield %.2f  AntiDiv %.2f",
                            avg.antibiotic_type, avg.antibiotic_yield, avg.antibiotic_diversity);
            }
        };

        ImGui::Text("Alive genomes sampled: %zu", sampled_alive);
        ImGui::TextDisabled("Averages and dominant shifts across living organisms.");
        ImGui::Separator();
        ImGui::BeginChild("##GenomicRollup", ImVec2(0, 0), true);
        for (int t = 0; t < BIO_TYPE_COUNT; ++t) {
            if (type_gene_stats[t].count == 0)
                continue;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
            bool open = ImGui::TreeNodeEx((void*)(intptr_t)(1000 + t), flags, "%s", pop_type_names[t]);
            ImGui::SameLine();
            ImGui::TextColored(type_colors_v[t], "%u sampled", type_gene_stats[t].count);
            if (open) {
                ImGui::TextWrapped("Dominant: %s",
                                   dominant_traits_summary(static_cast<uint32_t>(t), type_gene_stats[t]).c_str());
                draw_avg_lines(type_gene_stats[t], static_cast<uint32_t>(t));
                uint32_t subtype_count = type_variant_count(static_cast<uint32_t>(t));
                if (subtype_count > 1) {
                    ImGui::Separator();
                    for (uint32_t subtype = 0; subtype < subtype_count; ++subtype) {
                        const auto& agg = subtype_gene_stats[t][subtype];
                        if (agg.count == 0)
                            continue;
                        ImGuiTreeNodeFlags subflags = ImGuiTreeNodeFlags_DefaultOpen;
                        int sub_id = 2000 + t * 64 + static_cast<int>(subtype);
                        bool subopen = ImGui::TreeNodeEx((void*)(intptr_t)sub_id, subflags, "%s",
                                                         bio_entity_variant_name(static_cast<uint32_t>(t), subtype));
                        ImGui::SameLine();
                        ImGui::Text("%u sampled", agg.count);
                        if (subopen) {
                            ImGui::TextWrapped("Dominant: %s",
                                               dominant_traits_summary(static_cast<uint32_t>(t), agg).c_str());
                            draw_avg_lines(agg, static_cast<uint32_t>(t));
                            ImGui::TreePop();
                        }
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    }

    if (event_log_visible_) {
    ImGui::SetNextWindowPos({10.0f, std::max(330.0f, io.DisplaySize.y - 300.0f)}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({460, 250}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Event Log", &event_log_visible_)) {
        ImGui::Text("Recent lifecycle events");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", event_log_.size());
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &event_log_auto_scroll_);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            event_log_.clear();
            event_log_dirty_ = false;
        }
        ImGui::Separator();
        ImGui::BeginChild("##EventLogEntries", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& entry : event_log_) {
            ImGui::PushStyleColor(ImGuiCol_Text, bio_event_type_color(entry.type));
            ImGui::TextWrapped("[%.1fs] [%s] %s", entry.time, bio_event_type_name(entry.type), entry.text.c_str());
            ImGui::PopStyleColor();
        }
        if (event_log_dirty_ && event_log_auto_scroll_)
            ImGui::SetScrollHereY(1.0f);
        event_log_dirty_ = false;
        ImGui::EndChild();
    }
    ImGui::End();
    }

    // Entity inspector
    if (selected_entity >= 0 && selected_entity < (int)state.entities.size()) {
        const auto& e = state.entities[selected_entity];
        if (e.alive || e.corpse) {
            ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 280}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({300, 580}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Entity Inspector");
		
            ImGui::TextColored(type_colors_v[e.type % BIO_TYPE_COUNT],
                "%s #%u", BIO_TYPE_NAMES[e.type % BIO_TYPE_COUNT], e.entity_id);
            if (e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_VIRUS)
                ImGui::Text("%s", bio_entity_variant_name(e.type, e.morphology));
            if (e.type == BIO_CELL)
                ImGui::TextDisabled("%s", cell_traits(e.morphology).lineage);
            else if (e.type == BIO_BACTERIUM) {
                ImGui::TextDisabled("%s", bacteria_traits(e.morphology).lineage);
                ImGui::TextWrapped("%s", bacteria_traits(e.morphology).infection_mode);
            } else if (e.type == BIO_VIRUS) {
                ImGui::TextDisabled("%s", virus_traits(e.morphology).lineage);
                ImGui::TextWrapped("%s", virus_traits(e.morphology).receptor);
            }
            if (e.corpse)
                ImGui::TextColored(ImVec4(0.82f, 0.66f, 0.52f, 1.0f), "Dead Husk");
            ImGui::Separator();
            ImGui::Text("Energy:  %.1f", e.energy);
            ImGui::Text("Age:     %.1f s", e.age);
            ImGui::Text("ID:      %u", e.entity_id);
            ImGui::Text("Gen:     %u", e.generation);
            ImGui::Text("Species: %u", e.species_key);
            if (e.parent_id != 0)
                ImGui::Text("Parent:  %u", e.parent_id);
            else
                ImGui::Text("Parent:  Founder");
            ImGui::Text("Radius:  %.1f", e.radius);
            ImGui::Text("Aspect:  %.2f", e.shape_aspect);
            ImGui::Text("Speed:   %.1f", glm::length(e.vel));
            ImGui::Text("Stress:  %.2f", compute_environment_stress(cfg, environment_, e));
            ImGui::Text("Reserve: %.2f", e.nutrient_reserve);
            ImGui::Text("ATP:     %.1f", e.atp);
            ImGui::Text("Starve:  %.2f", e.starvation);
            if (type_uses_telomeres(e.type))
                ImGui::Text("Telomere %.0f%%", telomere_fraction_remaining(e) * 100.0f);
            ImGui::Text("Organs:  %.0f%%", e.organelle_health * 100.0f);
            if (e.complement_tag > 0.01f)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Complement: %.0f%%", e.complement_tag * 100.0f);
            ImGui::Text("Stage:   %s", bio_lifecycle_stage_name(e));
            ImGui::Text("Cycle CD %.1f s", e.division_cooldown);
            ImGui::Text("Divides: %u", e.division_count);
            if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e)) {
                const BioPathogenState* dominant = dominant_infection_state(e);
                ImGui::TextColored(ImVec4(0.96f, 0.48f, 0.44f, 1.0f), "Infection: %s (%.0f%%)",
                    infection_variant_name(e).c_str(), dominant_infection_progress(e) * 100.0f);
                ImGui::Text("Total Load: %.1f", combined_infection_load(e));
                ImGui::TextWrapped("Mechanism: %s", infection_mechanism_name(e).c_str());
                if (entity_has_viral_infection(e))
                    ImGui::Text("Viral Load: %.1f (%s)", e.viral_infection.load,
                                bio_entity_variant_name(BIO_VIRUS, e.viral_infection.morphology));
                if (entity_has_bacterial_infection(e))
                    ImGui::Text("Bacterial Load: %.1f (%s)", e.bacterial_infection.load,
                                bio_entity_variant_name(BIO_BACTERIUM, e.bacterial_infection.morphology));
                if (dominant && dominant->source_id != 0)
                    ImGui::Text("Primary Source: %u", dominant->source_id);
            }
            if (e.type == BIO_WHITE_BLOOD) {
                const char* subtype = (e.immune_subtype == BIO_IMMUNE_T_CELL) ? "Cytotoxic T Cell"
                    : (e.immune_subtype == BIO_IMMUNE_B_CELL) ? "B Cell (antibody producer)"
                    : "Neutrophil";
                ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Subtype: %s", subtype);
            }
            if (e.type == BIO_BACTERIUM) {
                ImGui::Text("Antibiotic Film: %.0f%%", e.antibiotic_film * 100.0f);
                ImGui::Text("Antibiotic Types: %d", antibiotic_type_count(e));
                ImGui::Text("Quorum Signal: %.0f%%", e.quorum_signal * 100.0f);
                if (e.resistance_level > 0.01f)
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Resistance: %.0f%%", e.resistance_level * 100.0f);
            }
            if (e.corpse)
                ImGui::Text("Corpse:  %.1f s", e.corpse_age);
            ImGui::Text("Pos:     (%.0f, %.0f, %.0f)", e.pos.x, e.pos.y, e.pos.z);
            ImGui::Text("Genome:  %08X", e.genome);
            if ((e.type == BIO_CELL || e.type == BIO_WHITE_BLOOD) && e.mitosis_progress > 0.0f)
                ImGui::Text("Mitosis: %s (%.0f%%)", bio_mitosis_stage_name(e.mitosis_progress), e.mitosis_progress * 100.0f);
            if (e.type == BIO_BACTERIUM && e.mitosis_progress > 0.0f)
                ImGui::Text("Fission: %s (%.0f%%)", bio_binary_fission_stage_name(e.mitosis_progress), e.mitosis_progress * 100.0f);
            ImGui::Separator();
            ImGui::Text("Genes");
            ImGui::Text("Seek:    %.2f", e.genes.seek);
            ImGui::Text("Flee:    %.2f", e.genes.flee);
            ImGui::Text("Spacing: %.2f", e.genes.spacing);
            ImGui::Text("Brown:   %.2f", e.genes.brownian);
            ImGui::Text("Energy:  %.2f", e.genes.energy);
            ImGui::Text("Metab:   %.2f", e.genes.metabolism_efficiency);
            if (type_feeds_on_nutrients(e.type))
                ImGui::Text("NutrAff: %.2f", e.genes.nutrient_affinity);
            ImGui::Text("Stress:  %.2f", e.genes.stress_tolerance);
            ImGui::Text("Defense: %.2f", e.genes.defense);
            ImGui::Text("Sense:   %.2f", e.genes.sensing);
            if (gene_trait_applicable(e.type, 12))
                ImGui::Text("MutStab: %.2f", e.genes.mutation_stability);
            if (type_uses_telomeres(e.type))
                ImGui::Text("Telomr:  %.2f", e.genes.telomere);
            ImGui::Text("Cycle:   %.2f", e.genes.mitotic_clock);
            if (e.type == BIO_BACTERIUM) {
                ImGui::Text("AntiType: %.2f", e.genes.antibiotic_type);
                ImGui::Text("AntiOut:  %.2f", e.genes.antibiotic_yield);
                ImGui::Text("AntiMix:  %.2f", e.genes.antibiotic_diversity);
                ImGui::Text("Resist:   %.2f", e.genes.resistance);
                ImGui::Text("Quorum:   %.2f", e.genes.quorum_threshold);
            }
            ImGui::Spacing();
            if (!e.corpse) {
                if (ImGui::Button("Kill To Husk")) {
                    mark_entity_corpse(state.entities[selected_entity], BIO_EVENT_USER, "was manually collapsed into a dead husk");
                    selected_entity = -1;
                }
            } else {
                if (ImGui::Button("Cull Husk")) {
                    char msg[192];
                    std::snprintf(msg, sizeof(msg), "User culled the dead husk of %s.",
                                  bio_entity_label(e).c_str());
                    push_event(BIO_EVENT_USER, msg);
                    state.entities[selected_entity].corpse = false;
                    state.entities[selected_entity].alive = false;
                    selected_entity = -1;
                }
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
            ImGui::SetNextWindowSize(ImVec2(220, 220));
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
                    autospawn_timer_ = 0.0f;
                    sim_time_ = 0.0f;
                    next_entity_id_ = 1;
                    reset_population_metrics();
                    event_log_.clear();
                    event_log_dirty_ = false;
                    push_event(BIO_EVENT_SYSTEM, "Simulation cleared.");
                    show_menu_popup_ = false;
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.45f, 0.7f), "VIEW");
                ImGui::Separator();
                ImGui::MenuItem("Settings Panel", nullptr, &settings_visible_);
                ImGui::MenuItem("Population Panel", nullptr, &population_visible_);
                ImGui::MenuItem("Genomic Stats", nullptr, &genomics_visible_);
                ImGui::MenuItem("Event Log", nullptr, &event_log_visible_);
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

        struct TaskBtn { const char* label; bool* vis; };
        TaskBtn btns[] = {
            {"Settings",   &settings_visible_},
            {"Spawn",      &spawn_menu_visible_},
            {"Population", &population_visible_},
            {"Genomics",   &genomics_visible_},
            {"Events",     &event_log_visible_},
        };

        // ── Center: Taskbar buttons ──
        float total_btn_width = static_cast<float>(sizeof(btns) / sizeof(btns[0])) * 94.0f;
        float center_start = std::max(120.0f, W * 0.5f - total_btn_width * 0.5f);
        ImGui::SameLine(center_start);

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
