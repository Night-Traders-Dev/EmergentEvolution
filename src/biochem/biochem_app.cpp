#include "biochem/biochem_app.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
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
    "Cat Brain"
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

static const char* bio_lifecycle_stage_name(const BioEntity& e) {
    if (e.corpse) return "Dead Husk";
    if (e.telomere_state <= 0.08f || e.organelle_health <= 0.20f) return "Senescent";
    if (e.organelle_health <= 0.55f || e.telomere_state <= 0.42f) return "Aging";
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
    return type == BIO_CELL || type == BIO_BACTERIUM || type == BIO_VIRUS;
}

static void blend_gene_block(BioGenes& dst, const BioGenes& src, float weight) {
    dst.seek += (src.seek - dst.seek) * weight;
    dst.flee += (src.flee - dst.flee) * weight;
    dst.spacing += (src.spacing - dst.spacing) * weight;
    dst.brownian += (src.brownian - dst.brownian) * weight;
    dst.energy += (src.energy - dst.energy) * weight;
    dst.telomere += (src.telomere - dst.telomere) * weight;
    dst.mitotic_clock += (src.mitotic_clock - dst.mitotic_clock) * weight;
    dst.antibiotic_type += (src.antibiotic_type - dst.antibiotic_type) * weight;
    dst.antibiotic_yield += (src.antibiotic_yield - dst.antibiotic_yield) * weight;
    dst.antibiotic_diversity += (src.antibiotic_diversity - dst.antibiotic_diversity) * weight;
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
    return e.infection_progress > 0.0f;
}

static bool entity_has_viral_infection(const BioEntity& e) {
    return e.infection_progress > 0.0f && e.infection_source_type == BIO_VIRUS;
}

static bool entity_has_bacterial_infection(const BioEntity& e) {
    return e.infection_progress > 0.0f && e.infection_source_type == BIO_BACTERIUM;
}

static const char* infection_variant_name(const BioEntity& e) {
    if (e.infection_source_type == BIO_VIRUS)
        return bio_entity_variant_name(BIO_VIRUS, e.infection_morphology);
    if (e.infection_source_type == BIO_BACTERIUM)
        return bio_entity_variant_name(BIO_BACTERIUM, e.infection_morphology);
    return "Unknown pathogen";
}

static const char* infection_mechanism_name(const BioEntity& e) {
    if (e.infection_source_type == BIO_VIRUS)
        return virus_traits(e.infection_morphology).receptor;
    if (e.infection_source_type == BIO_BACTERIUM)
        return bacteria_traits(e.infection_morphology).infection_mode;
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

static float randf_range(float lo, float hi) {
    return lo + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (hi - lo);
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
    case BIO_CELL:        return {1.00f, 1.00f, 1.05f, 0.80f, 1.00f, 1.00f, 1.00f, 0.0f, 0.0f, 0.0f};
    case BIO_BACTERIUM:   return {1.15f, 0.80f, 0.85f, 1.30f, 0.90f, 0.00f, 1.30f, 0.50f, 0.90f, 0.50f};
    case BIO_VIRUS:       return {1.20f, 0.30f, 0.55f, 0.55f, 0.75f, 0.00f, 0.40f, 0.0f, 0.0f, 0.0f};
    case BIO_ANTIBODY:    return {1.10f, 0.40f, 0.90f, 0.45f, 0.95f, 0.00f, 0.65f, 0.0f, 0.0f, 0.0f};
    case BIO_RED_BLOOD:   return {0.10f, 0.05f, 0.60f, 1.10f, 1.05f, 0.00f, 0.55f, 0.0f, 0.0f, 0.0f};
    case BIO_WHITE_BLOOD: return {1.30f, 0.75f, 1.10f, 0.70f, 1.20f, 0.82f, 0.78f, 0.0f, 0.0f, 0.0f};
    case BIO_JANITOR:     return {1.18f, 0.28f, 0.95f, 0.55f, 1.10f, 0.72f, 0.72f, 0.0f, 0.0f, 0.0f};
    default:              return {};
    }
}

struct BioGeneAggregate {
    BioGenes sum{};
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
    agg.sum.antibiotic_type += genes.antibiotic_type;
    agg.sum.antibiotic_yield += genes.antibiotic_yield;
    agg.sum.antibiotic_diversity += genes.antibiotic_diversity;
    agg.count += 1;
}

static BioGenes average_genes(const BioGeneAggregate& agg) {
    if (agg.count == 0)
        return {};
    float inv = 1.0f / static_cast<float>(agg.count);
    BioGenes avg = agg.sum;
    avg.seek *= inv;
    avg.flee *= inv;
    avg.spacing *= inv;
    avg.brownian *= inv;
    avg.energy *= inv;
    avg.telomere *= inv;
    avg.mitotic_clock *= inv;
    avg.antibiotic_type *= inv;
    avg.antibiotic_yield *= inv;
    avg.antibiotic_diversity *= inv;
    return avg;
}

static const char* gene_trait_name(int idx) {
    static const char* names[] = {
        "Seek", "Flee", "Spacing", "Brownian", "Energy",
        "Telomere", "Mitotic Clock", "Antibiotic Type",
        "Antibiotic Yield", "Antibiotic Diversity"
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
    case 7: return genes.antibiotic_type;
    case 8: return genes.antibiotic_yield;
    case 9: return genes.antibiotic_diversity;
    default: return 0.0f;
    }
}

static bool gene_trait_applicable(uint32_t type, int idx) {
    if (idx >= 7)
        return type == BIO_BACTERIUM;
    if (idx == 5)
        return type == BIO_CELL || type == BIO_WHITE_BLOOD || type == BIO_JANITOR || type == BIO_BACTERIUM;
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
    std::array<RankedTrait, 10> ranked{};
    size_t used = 0;
    for (int idx = 0; idx < 10; ++idx) {
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
    genes.antibiotic_type = std::clamp(genes.antibiotic_type, 0.00f, 1.00f);
    genes.antibiotic_yield = std::clamp(genes.antibiotic_yield, 0.00f, 2.50f);
    genes.antibiotic_diversity = std::clamp(genes.antibiotic_diversity, 0.00f, 1.00f);
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
    e.genes.telomere *= jitter(0.20f);
    e.genes.mitotic_clock *= jitter(0.26f);

    switch (e.type) {
    case BIO_CELL:
        if (bio_cell_visual_family(e.morphology) == BIO_CELL_FAMILY_EPITHELIAL) {
            e.genes.spacing *= 1.20f;
            e.genes.brownian *= 0.72f;
        } else if (bio_cell_visual_family(e.morphology) == BIO_CELL_FAMILY_AMOEBOID) {
            e.genes.seek *= 1.18f;
            e.genes.brownian *= 1.28f;
            e.genes.flee *= 0.92f;
        }
        if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_NEURON) {
            e.genes.seek *= 0.72f;
            e.genes.energy *= 1.08f;
        } else if (e.morphology % BIO_CELL_VARIANT_COUNT == BIO_CELL_TYPE_II_PNEUMOCYTE) {
            e.genes.spacing *= 1.08f;
            e.genes.energy *= 1.04f;
        }
        break;
    case BIO_BACTERIUM:
        e.genes.antibiotic_type = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
        e.genes.antibiotic_yield *= jitter(0.30f);
        e.genes.antibiotic_diversity *= jitter(0.45f);
        if (bio_bacteria_visual_family(e.morphology) == BIO_BACTERIA_FAMILY_BACILLI) {
            e.genes.seek *= 1.12f;
            e.genes.spacing *= 1.08f;
            e.genes.antibiotic_yield *= 1.10f;
        } else if (bio_bacteria_visual_family(e.morphology) == BIO_BACTERIA_FAMILY_SPIRAL) {
            e.genes.brownian *= 1.20f;
            e.genes.flee *= 1.10f;
            e.genes.antibiotic_diversity *= 1.12f;
        }
        e.genes.antibiotic_yield *= bacteria_traits(e.morphology).antibiotic_bonus;
        break;
    case BIO_VIRUS:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_SARS_COV_2) {
            e.genes.seek *= 1.18f;
            e.genes.energy *= 1.06f;
        } else if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_BACTERIOPHAGE_T4) {
            e.genes.spacing *= 0.82f;
            e.genes.seek *= 1.10f;
        } else if (e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_INFLUENZA_A_H1N1 ||
                   e.morphology % BIO_VIRUS_VARIANT_COUNT == BIO_VIRUS_INFLUENZA_A_H3N2) {
            e.genes.seek *= 1.08f;
            e.genes.spacing *= 0.90f;
        }
        break;
    case BIO_WHITE_BLOOD:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        e.genes.telomere *= 0.92f;
        e.genes.mitotic_clock *= 0.82f;
        break;
    case BIO_JANITOR:
        e.genes.antibiotic_type = 0.0f;
        e.genes.antibiotic_yield = 0.0f;
        e.genes.antibiotic_diversity = 0.0f;
        e.genes.seek *= 1.08f;
        e.genes.flee *= 0.55f;
        e.genes.telomere *= 0.88f;
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
    mutate_trait(e.genes.telomere, 0.00f, 2.25f);
    mutate_trait(e.genes.mitotic_clock, 0.25f, 2.50f);
    mutate_trait(e.genes.antibiotic_type, 0.00f, 1.00f);
    mutate_trait(e.genes.antibiotic_yield, 0.00f, 2.50f);
    mutate_trait(e.genes.antibiotic_diversity, 0.00f, 1.00f);
    clamp_genes(e.genes);
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
    switch (type) {
    case BIO_CELL:        return 9.0f;
    case BIO_WHITE_BLOOD: return 15.0f;
    case BIO_BACTERIUM:   return 4.0f;
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
    e.infection_progress = 0.0f;
    e.infection_load = 0.0f;
    e.infection_axis = glm::vec3(1.0f, 0.0f, 0.0f);
    e.infection_morphology = 0;
    e.infection_source_id = 0;
    e.infection_source_type = BIO_TYPE_COUNT;
    e.infection_species_key = 0;
    e.infection_generation = 0;
    e.infection_genome = 0;
    e.infection_genes = {};
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
    float capacity = 4.5f + radius_ratio * (2.35f + std::clamp(host.infection_genes.energy, 0.35f, 2.25f) * 0.65f);
    switch (host.infection_morphology % BIO_VIRUS_VARIANT_COUNT) {
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
            initialize_entity_lifecycle(e, rng);
            configure_entity_shape(e, rng);
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
        reset_infection_state(e);
        e.vel *= 0.35f;
    }

    char msg[256];
    std::snprintf(msg, sizeof(msg), "%s %s.", bio_entity_label(e).c_str(), reason.c_str());
    push_event(event_type, msg);
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

    next_entity_id_ = 1;
    reset_population_metrics();
    event_log_.clear();
    event_log_dirty_ = false;
    seed_default_population(state, cfg, environment_, next_entity_id_);
    cfg.entity_count = static_cast<uint32_t>(state.count_alive());
    push_event(BIO_EVENT_SYSTEM, "Simulation initialized.");
}

void BiochemApp::reset_population_metrics() {
    next_species_key_ = 1024;
    ever_infected_total_ = 0;
    ever_infected_by_type_.fill(0);
    ever_infected_by_subtype_.clear();
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
    sim_time_ = 0.0f;
    paused = false;

    char msg[192];
    std::snprintf(msg, sizeof(msg), "Simulation seeded in %s with %zu living entities.",
                  BIO_ENVIRONMENT_NAMES[cfg.environment % BIO_ENV_COUNT], state.count_alive());
    push_event(BIO_EVENT_SYSTEM, msg);
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
    initialize_entity_lifecycle(e, rng);
    configure_entity_shape(e, rng);
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
    initialize_entity_lifecycle(e, rng);
    configure_entity_shape(e, rng);
    assign_entity_identity(e);
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

        // Metabolism + starvation + organelle aging
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
            float reserve_gain = 0.0f;
            float energy_gain = 0.0f;

            if (e.type == BIO_BACTERIUM) {
                float scavenging = std::max(0.0f, cfg.nutrient_density - 0.8f) * 0.35f;
                scavenging += nutrient_field * 0.85f;
                scavenging -= toxin_field * 0.22f;
                reserve_gain = std::max(0.0f, scavenging) * scaled_dt * 0.18f;
                energy_gain = reserve_gain * 5.5f * energy_gain_gene_scale(e);
            } else if (type_feeds_on_nutrients(e.type)) {
                float uptake = nutrient_field * scaled_dt;
                if (e.type == BIO_CELL) uptake *= 0.11f;
                else if (e.type == BIO_WHITE_BLOOD) uptake *= 0.08f;
                else if (e.type == BIO_JANITOR) uptake *= 0.10f;
                else if (e.type == BIO_RED_BLOOD) uptake *= 0.05f;
                else if (e.type == BIO_ANTIBODY) uptake *= 0.03f;
                reserve_gain = uptake;
                energy_gain = uptake * 3.8f * energy_gain_gene_scale(e);
            }
            if (reserve_gain > 0.0f || energy_gain > 0.0f)
                feed_entity(e, reserve_gain, energy_gain);

            float reserve_drain = base_metabolism * scaled_dt * 0.055f;
            e.nutrient_reserve = std::max(0.0f, e.nutrient_reserve - reserve_drain);

            if (e.nutrient_reserve < 0.18f)
                e.starvation = std::min(1.6f, e.starvation + scaled_dt * (0.16f + (0.18f - e.nutrient_reserve) * 1.8f));
            else
                e.starvation = std::max(0.0f, e.starvation - scaled_dt * (0.20f + e.nutrient_reserve * 0.30f));

            float stress = compute_environment_stress(cfg, environment_, e);
            float telomere_frac = telomere_fraction_remaining(e);
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
            e.energy -= base_metabolism * scaled_dt * (1.0f + stress + e.starvation * 1.45f + senescence);

            float target_health = 0.20f + std::clamp(e.nutrient_reserve, 0.0f, 1.0f) * 0.42f;
            target_health += std::clamp(telomere_frac, 0.0f, 1.0f) * 0.33f;
            target_health -= e.starvation * 0.34f + senescence * 0.22f + division_wear * 0.24f;
            target_health = std::clamp(target_health, 0.0f, 1.0f);
            e.organelle_health += (target_health - e.organelle_health) * std::min(1.0f, scaled_dt * 1.8f);

            if (e.energy <= 0.0f) {
                if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_viral_infection(e)) {
                    e.energy = 0.0f;
                    continue;
                }
                std::string reason = (e.starvation > 0.65f || e.nutrient_reserve <= 0.01f)
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
        if (!type_feeds_on_nutrients(ents[i].type)) continue;

        for (size_t j = 0; j < ents.size(); j++) {
            if (i == j || !ents[j].alive) continue;

            glm::vec3 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (ents[j].type == BIO_NUTRIENT && dist < touch) {
                float uptake = 0.35f + cfg.nutrient_density * 0.18f;
                float reserve_gain = ents[j].energy * uptake * 0.020f;
                float energy_gain = ents[j].energy * uptake * 0.24f * energy_gain_gene_scale(ents[i]);
                feed_entity(ents[i], reserve_gain, energy_gain);
                ents[j].alive = false;
            } else if (ents[j].type == BIO_TOXIN && dist < touch) {
                ents[i].energy -= ents[j].energy * (0.10f + cfg.toxicity * 0.08f);
            }
        }
    }

    // Subsystems
    process_bacteria_antibiotics(scaled_dt);
    process_gene_exchange(scaled_dt);
    process_repulsion();
    process_cell_division(scaled_dt);
    process_virus_infection(scaled_dt);
    process_bacterial_colonization(scaled_dt);
    if (cfg.immune_system)
        process_antibody_response(scaled_dt);
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
    if (n > 1000) return;

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;
        if (e.type != BIO_CELL && e.type != BIO_BACTERIUM && e.type != BIO_WHITE_BLOOD) continue;
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && e.infection_progress > 0.0f)
            continue;
        if (type_uses_telomeres(e.type) && e.telomere_state <= 0.08f)
            continue;
        float threshold = division_threshold_for(cfg, e);
        float previous_mitosis = e.mitosis_progress;
        bool uses_mitosis = (e.type == BIO_CELL || e.type == BIO_WHITE_BLOOD);
        bool uses_binary_fission = (e.type == BIO_BACTERIUM);
        bool uses_staged_division = uses_mitosis || uses_binary_fission;

        if (uses_staged_division) {
            float duration_base = 3.6f;
            if (e.type == BIO_WHITE_BLOOD)
                duration_base = 4.9f;
            else if (e.type == BIO_BACTERIUM)
                duration_base = 2.45f;
            float division_duration = duration_base /
                std::clamp(e.genes.energy * e.genes.mitotic_clock, 0.55f, 2.20f);
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
        child.mitosis_progress = 0.0f;
        e.mitosis_progress = 0.0f;
        initialize_entity_lifecycle(child, rng);
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
        if (child.morphology != e.morphology || child.genome != e.genome)
            child.species_key = next_species_key_++;
        assign_entity_identity(child, e.generation + 1, e.entity_id);
        configure_entity_shape(child, rng);

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

    for (size_t i = 0; i < n; ++i) {
        const auto& producer = ents[i];
        if (!producer.alive || producer.type != BIO_BACTERIUM)
            continue;

        float local_pressure = 0.0f;
        float range = antibiotic_range(producer);
        for (size_t j = 0; j < n; ++j) {
            if (i == j)
                continue;
            const auto& target = ents[j];
            if (!target.alive || target.type != BIO_BACTERIUM)
                continue;

            float dist = glm::length(target.pos - producer.pos);
            if (dist > range)
                continue;

            float overlap = antibiotic_spectrum_overlap(producer, target);
            float genomic_mismatch = std::min(1.0f,
                static_cast<float>(__builtin_popcount(static_cast<unsigned int>(producer.genome ^ target.genome))) / 10.0f);
            float hostility = std::clamp((1.0f - overlap) * (0.25f + genomic_mismatch * 0.75f), 0.0f, 1.0f);
            if (hostility <= 0.04f)
                continue;

            float falloff = 1.0f - dist / std::max(range, 1.0f);
            float pressure = hostility * falloff;
            local_pressure = std::max(local_pressure, pressure);

            float potency = (1.8f + producer.genes.antibiotic_yield * 3.8f +
                             producer.genes.antibiotic_diversity * 1.8f) * pressure * dt;
            damage_accum[j] += potency;
            if (potency > strongest_hit[j]) {
                strongest_hit[j] = potency;
                damage_source[j] = static_cast<int>(i);
            }
        }

        film_targets[i] = antibiotic_film_target(producer, local_pressure);
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

    for (size_t i = 0; i < n; ++i) {
        auto& a = ents[i];
        if (!a.alive || !type_supports_gene_exchange(a.type))
            continue;

        for (size_t j = i + 1; j < n; ++j) {
            auto& b = ents[j];
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
            float chance = dt * (0.010f + subtype_mix * 0.028f) * (0.55f + genome_similarity * 0.45f);
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
            clamp_genes(recipient->genes);

            bool speciation = subtype_shift || randf_range(0.0f, 1.0f) < 0.14f;
            if (speciation) {
                recipient->species_key = next_species_key_++;
                char msg[256];
                std::snprintf(msg, sizeof(msg),
                              "%s exchanged genes with %s and diverged into species cluster %u.",
                              bio_entity_label(*recipient).c_str(), bio_entity_label(*donor).c_str(),
                              recipient->species_key);
                push_event(BIO_EVENT_DIVISION, msg);
            }

            std::mt19937 rng(recipient->genome ^ recipient->species_key ^ static_cast<uint32_t>(i * 977u + j * 131u));
            configure_entity_shape(*recipient, rng);
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

    for (size_t i = 0; i < n; ++i) {
        auto& host = ents[i];
        if (!host.alive || (host.type != BIO_CELL && host.type != BIO_BACTERIUM) || !entity_has_viral_infection(host))
            continue;

        float local_shelter = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, host.pos);
        float local_toxin = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, host.pos);
        float local_factor = std::clamp(1.0f + local_toxin * 0.30f - local_shelter * 0.16f, 0.65f, 1.65f);
        float replication_rate = effective_rate * local_factor *
            (0.65f + std::clamp(host.infection_genes.mitotic_clock, 0.25f, 2.50f) * 0.40f) *
            (0.70f + std::clamp(host.infection_genes.energy, 0.35f, 2.25f) * 0.30f);
        float burst_capacity = viral_burst_capacity(host);
        float ingress_gate = glm::smoothstep(0.04f, 0.22f, host.infection_progress);
        float replication_gate = glm::smoothstep(0.14f, 0.82f, host.infection_progress);
        float burst_bias = 0.70f + std::clamp(host.infection_genes.seek, 0.05f, 2.50f) * 0.12f +
                           std::clamp(host.infection_genes.energy, 0.35f, 2.25f) * 0.20f;

        host.infection_progress = std::min(1.20f, host.infection_progress + replication_rate * dt * 0.38f);
        float target_load = 1.0f + replication_gate * burst_capacity * burst_bias;
        host.infection_load += (target_load - host.infection_load) * std::min(1.0f, dt * (1.9f + ingress_gate * 2.5f));
        host.energy -= dt * (4.4f + replication_gate * 2.2f + host.infection_load * 1.35f);
        host.starvation = std::min(1.6f, host.starvation + dt * host.infection_progress * 0.18f);
        host.organelle_health = std::max(0.0f, host.organelle_health - dt * (0.012f + replication_gate * 0.09f));
        host.vel *= 0.992f;

        bool overcrowded = host.infection_load >= burst_capacity * 0.96f;
        bool ready_to_lyse = overcrowded || host.infection_progress >= 1.08f ||
                             (host.energy <= 10.0f && host.infection_progress >= 0.82f) ||
                             (host.organelle_health <= 0.05f && host.infection_progress >= 0.76f);
        if (!ready_to_lyse)
            continue;

        std::string host_label = bio_entity_label(host);
        uint32_t source_generation = host.infection_generation;
        uint32_t source_id = host.infection_source_id;
        uint32_t source_species = host.infection_species_key;
        uint32_t source_genome = host.infection_genome;
        uint32_t source_morphology = host.infection_morphology % BIO_VIRUS_VARIANT_COUNT;
        BioGenes source_genes = host.infection_genes;
        glm::vec3 burst_origin = host.pos;
        glm::vec3 burst_velocity = host.vel;
        int burst_count = std::clamp(
            static_cast<int>(std::round(host.infection_load * (0.88f + source_genes.energy * 0.10f) +
                                        (source_morphology == BIO_VIRUS_BACTERIOPHAGE_T4 ? 1.0f : 0.0f))),
            6, 22);

        mark_entity_corpse(host, BIO_EVENT_INFECTION, "was lysed into a dead husk");

        int spawned = 0;
        for (int k = 0; k < burst_count && ents.size() < 1200; ++k) {
            BioEntity v;
            float a = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
            float p = static_cast<float>(rand()) / RAND_MAX * 3.1416f - 1.5708f;
            glm::vec3 d(cosf(p) * cosf(a), sinf(p), cosf(p) * sinf(a));
            v.pos = burst_origin + d * (type_default_radius(BIO_VIRUS) * 2.4f + 0.18f * spawned);
            v.vel = burst_velocity * 0.22f + d * randf_range(42.0f, 84.0f);
            v.type = BIO_VIRUS;
            v.morphology = source_morphology;
            v.genes = source_genes;
            v.genome = source_genome ? source_genome : static_cast<uint32_t>(rand());
            std::mt19937 vrng(v.genome ^ (uint32_t)(k * 977u + i * 131u));
            if (randf_range(0.0f, 1.0f) < cfg.mutation_rate * 1.5f)
                mutate_entity_genes(v, vrng, cfg.mutation_rate * 1.2f);
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
        for (size_t j = 0; j < n; ++j) {
            if (i == j)
                continue;
            auto& host = ents[j];
            if (!host.alive || (host.type != BIO_CELL && host.type != BIO_BACTERIUM))
                continue;
            if (host.infection_progress > 0.0f)
                continue;
            if (!virus_targets_host(virus, host))
                continue;

            float dist = glm::length(host.pos - virus.pos);
            float contact_radius = std::min(cfg.infection_radius, virus.radius + host.radius * 1.02f + 0.8f);
            if (dist < contact_radius && dist < best_contact) {
                best_contact = dist;
                best_host = static_cast<int>(j);
            }
        }

        if (best_host < 0)
            continue;

        auto& host = ents[best_host];
        float local_shelter = sample_feature_density(environment_, BIO_ENV_FEATURE_MEMBRANE, host.pos);
        float tropism = virus_host_tropism(virus, host);
        if (tropism <= 0.0f)
            continue;
        float local_factor = std::clamp(1.0f - local_shelter * 0.18f, 0.45f, 1.0f) * tropism;
        float contact_radius = std::min(cfg.infection_radius, virus.radius + host.radius * 1.02f + 0.8f);
        float chance = std::clamp(effective_rate * local_factor * (1.0f - best_contact / std::max(contact_radius, 1.0f)) * dt * 3.5f,
                                  0.0f, 0.95f);
        if (randf_range(0.0f, 1.0f) > chance)
            continue;

        if (host.infection_progress <= 0.0f) {
            host.infection_progress = 0.01f;
            host.infection_load = 1.0f;
            host.infection_axis = normalized_or(virus.pos - host.pos, host.axis);
            host.infection_morphology = virus.morphology;
            host.infection_source_id = virus.entity_id;
            host.infection_source_type = BIO_VIRUS;
            host.infection_species_key = virus.species_key;
            host.infection_generation = virus.generation;
            host.infection_genome = virus.genome;
            host.infection_genes = virus.genes;
            if (!host.ever_infected) {
                host.ever_infected = true;
                ever_infected_total_ += 1;
                ever_infected_by_type_[host.type % BIO_TYPE_COUNT] += 1;
                ever_infected_by_subtype_[type_subtype_key(host.type, host.morphology)] += 1;
            }

            char msg[224];
            std::snprintf(msg, sizeof(msg), "%s entered %s and began replicating.",
                          bio_entity_label(virus).c_str(), bio_entity_label(host).c_str());
            push_event(BIO_EVENT_INFECTION, msg);
        } else {
            host.infection_load += 0.45f + std::clamp(virus.genes.energy, 0.35f, 2.25f) * 0.25f;
            host.infection_progress = std::max(host.infection_progress, 0.06f);
            if (virus.genes.energy > host.infection_genes.energy) {
                host.infection_axis = normalized_or(glm::mix(host.infection_axis, virus.pos - host.pos, 0.65f), host.axis);
                host.infection_morphology = virus.morphology;
                host.infection_source_id = virus.entity_id;
                host.infection_source_type = BIO_VIRUS;
                host.infection_species_key = virus.species_key;
                host.infection_generation = virus.generation;
                host.infection_genome = virus.genome;
                host.infection_genes = virus.genes;
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

    for (size_t i = 0; i < n; ++i) {
        auto& host = ents[i];
        if (!host.alive || host.type != BIO_CELL || !entity_has_bacterial_infection(host))
            continue;

        const auto& traits = bacteria_traits(host.infection_morphology);
        float nutrient_field = sample_feature_density(environment_, BIO_ENV_FEATURE_NUTRIENT, host.pos);
        float toxin_field = sample_feature_density(environment_, BIO_ENV_FEATURE_TOXIN, host.pos);
        float entry_bias = pathogen_entry_bias(traits.entry_mode);
        float growth_rate = effective_rate * traits.colonization_rate * entry_bias *
                            (0.68f + nutrient_field * 0.42f) *
                            (0.70f + std::clamp(host.infection_genes.energy, 0.35f, 2.25f) * 0.22f);
        float host_drag = 0.78f + cell_traits(host.morphology).infection_susceptibility * 0.22f;

        host.infection_progress = std::min(1.18f, host.infection_progress + growth_rate * dt * 0.26f);
        host.infection_load += dt * (0.55f + growth_rate * 1.8f + toxin_field * 0.18f);
        host.energy -= dt * (2.1f + host.infection_load * 0.62f + toxin_field * 0.85f) * host_drag;
        host.starvation = std::min(1.6f, host.starvation + dt * host.infection_progress * 0.12f);
        host.organelle_health = std::max(0.0f, host.organelle_health -
            dt * (0.010f + host.infection_progress * 0.048f + toxin_field * 0.020f));
        host.vel *= 0.994f;

        bool overwhelmed = host.infection_load >= (4.4f + traits.colonization_rate * 3.2f);
        bool host_failed = overwhelmed || host.infection_progress >= 1.04f ||
                           (host.energy <= 9.0f && host.infection_progress >= 0.68f) ||
                           (host.organelle_health <= 0.08f && host.infection_progress >= 0.62f);
        if (!host_failed)
            continue;

        std::string host_label = bio_entity_label(host);
        uint32_t source_generation = host.infection_generation;
        uint32_t source_id = host.infection_source_id;
        uint32_t source_species = host.infection_species_key;
        uint32_t source_genome = host.infection_genome;
        uint32_t source_morphology = host.infection_morphology % BIO_BACTERIA_VARIANT_COUNT;
        BioGenes source_genes = host.infection_genes;
        glm::vec3 release_origin = host.pos;
        glm::vec3 release_velocity = host.vel;

        mark_entity_corpse(host, BIO_EVENT_INFECTION, "succumbed to bacterial colonization and collapsed into a dead husk");

        int spawned = std::clamp(
            static_cast<int>(std::round(1.0f + host.infection_load * 0.40f +
                                        std::clamp(source_genes.antibiotic_yield, 0.0f, 2.5f) * 0.35f)),
            1, 4);
        int released = 0;
        for (int k = 0; k < spawned && ents.size() < 1200; ++k) {
            BioEntity bacterium;
            float a = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
            float p = static_cast<float>(rand()) / RAND_MAX * 3.1416f - 1.5708f;
            glm::vec3 d(cosf(p) * cosf(a), sinf(p), cosf(p) * sinf(a));
            bacterium.pos = release_origin + d * (type_default_radius(BIO_BACTERIUM) * 1.8f + 0.14f * released);
            bacterium.vel = release_velocity * 0.28f + d * randf_range(12.0f, 28.0f);
            bacterium.type = BIO_BACTERIUM;
            bacterium.morphology = source_morphology;
            bacterium.genes = source_genes;
            bacterium.genome = source_genome ? source_genome : static_cast<uint32_t>(rand());
            std::mt19937 brng(bacterium.genome ^ (uint32_t)(k * 733u + i * 193u));
            if (randf_range(0.0f, 1.0f) < cfg.mutation_rate)
                mutate_entity_genes(bacterium, brng, cfg.mutation_rate);
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
        for (size_t j = 0; j < n; ++j) {
            if (i == j)
                continue;
            auto& host = ents[j];
            if (!host.alive || host.type != BIO_CELL)
                continue;
            if (entity_has_active_infection(host))
                continue;

            float dist = glm::length(host.pos - bacterium.pos);
            float contact_radius = std::min(cfg.infection_radius * 0.92f, bacterium.radius + host.radius * 1.02f + 1.4f);
            if (dist < contact_radius && dist < best_contact) {
                best_contact = dist;
                best_host = static_cast<int>(j);
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
        float contact_radius = std::min(cfg.infection_radius * 0.92f, bacterium.radius + host.radius * 1.02f + 1.4f);
        float local_factor = std::clamp((0.74f + nutrient_field * 0.30f) * (1.0f - membrane_cover * 0.12f) * tropism, 0.25f, 2.25f);
        float chance = std::clamp(effective_rate * local_factor *
                                  (1.0f - best_contact / std::max(contact_radius, 1.0f)) * dt * 2.4f,
                                  0.0f, 0.88f);
        if (randf_range(0.0f, 1.0f) > chance)
            continue;

        host.infection_progress = 0.01f;
        host.infection_load = 1.0f;
        host.infection_axis = normalized_or(bacterium.pos - host.pos, host.axis);
        host.infection_morphology = bacterium.morphology;
        host.infection_source_id = bacterium.entity_id;
        host.infection_source_type = BIO_BACTERIUM;
        host.infection_species_key = bacterium.species_key;
        host.infection_generation = bacterium.generation;
        host.infection_genome = bacterium.genome;
        host.infection_genes = bacterium.genes;
        if (!host.ever_infected) {
            host.ever_infected = true;
            ever_infected_total_ += 1;
            ever_infected_by_type_[host.type % BIO_TYPE_COUNT] += 1;
            ever_infected_by_subtype_[type_subtype_key(host.type, host.morphology)] += 1;
        }

        char msg[320];
        std::snprintf(msg, sizeof(msg), "%s colonized %s via %s.",
                      bio_entity_label(bacterium).c_str(), bio_entity_label(host).c_str(),
                      bacteria_traits(bacterium.morphology).infection_mode);
        push_event(BIO_EVENT_INFECTION, msg);
    }
}

// ── Antibody / Immune Response ──────────────────────────────────────────────

void BiochemApp::process_antibody_response(float dt) {
    auto& ents = state.entities;
    float wr = cfg.world_radius;
    float effective_immune = cfg.immune_strength * (0.45f + cfg.immune_pressure);

    size_t virus_count = 0, bacteria_count = 0, infected_host_count = 0, wbc_count = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.type == BIO_VIRUS) virus_count++;
        if (e.type == BIO_BACTERIUM) bacteria_count++;
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e)) infected_host_count++;
        if (e.type == BIO_WHITE_BLOOD) wbc_count++;
    }

    size_t immune_threat_count = virus_count + bacteria_count / 2 + infected_host_count;
    if (immune_threat_count > wbc_count && ents.size() < 1200 &&
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
        initialize_entity_lifecycle(wbc, wrng);
        assign_entity_identity(wbc);
        configure_entity_shape(wbc, wrng);
        char msg[192];
        std::snprintf(msg, sizeof(msg), "Immune system deployed %s.", bio_entity_label(wbc).c_str());
        ents.push_back(wbc);
        push_event(BIO_EVENT_IMMUNE, msg);
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
        initialize_entity_lifecycle(antibody, arng);
        assign_entity_identity(antibody);
        configure_entity_shape(antibody, arng);
        char msg[192];
        std::snprintf(msg, sizeof(msg), "Immune system released %s.", bio_entity_label(antibody).c_str());
        ents.push_back(antibody);
        push_event(BIO_EVENT_IMMUNE, msg);
    }

    for (auto& wbc : ents) {
        if (!wbc.alive || wbc.type != BIO_WHITE_BLOOD) continue;

        float best_dist = 999999.0f;
        int best_idx = -1;
        for (size_t j = 0; j < ents.size(); j++) {
            if (!ents[j].alive) continue;
            bool is_target = ents[j].type == BIO_VIRUS || ents[j].type == BIO_TOXIN || ents[j].type == BIO_BACTERIUM;
            if (!is_target && (ents[j].type == BIO_CELL || ents[j].type == BIO_BACTERIUM))
                is_target = entity_has_active_infection(ents[j]);
            if (!is_target) continue;
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
                float mitosis_slow = 1.0f - 0.65f * wbc.mitosis_progress;
                float chase_speed = 60.0f * effective_immune * wbc.genes.seek * mitosis_slow;
                wbc.vel += dir * chase_speed * dt;
                float spd = glm::length(wbc.vel);
                if (spd > 80.0f) wbc.vel *= 80.0f / spd;
            }

            float touch = wbc.radius + ents[best_idx].radius;
            if (dist < touch) {
                std::string target_label = bio_entity_label(ents[best_idx]);
                if (ents[best_idx].type == BIO_CELL || ents[best_idx].type == BIO_BACTERIUM)
                    mark_entity_corpse(ents[best_idx], BIO_EVENT_IMMUNE, "was cleared by a white blood cell");
                else
                    ents[best_idx].alive = false;
                wbc.energy += 10.0f;
                char msg[224];
                std::snprintf(msg, sizeof(msg), "%s neutralized %s.",
                              bio_entity_label(wbc).c_str(), target_label.c_str());
                push_event(BIO_EVENT_IMMUNE, msg);
            }
        }

        wbc.energy -= (0.3f + cfg.toxicity * 0.5f) * dt;
        if (wbc.energy <= 0.0f)
            mark_entity_corpse(wbc, BIO_EVENT_LIFECYCLE, "failed and became a dead husk");
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
            std::string target_label = bio_entity_label(ents[best_idx]);
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

    size_t corpse_count = 0;
    size_t phagocyte_count = 0;
    for (const auto& e : ents) {
        if (e.corpse) corpse_count++;
        if (e.alive && e.type == BIO_JANITOR) phagocyte_count++;
    }

    if (corpse_count > phagocyte_count * 2 && ents.size() < 1200 &&
        randf_range(0.0f, 1.0f) < std::min(0.55f, 0.04f + corpse_count * 0.025f) * dt * 60.0f) {
        BioEntity phagocyte;
        phagocyte.pos = {randf_range(-wr * 0.75f, wr * 0.75f),
                         randf_range(-wr * 0.75f, wr * 0.75f),
                         randf_range(-wr * 0.75f, wr * 0.75f)};
        phagocyte.vel = {0, 0, 0};
        phagocyte.type = BIO_JANITOR;
        phagocyte.genome = (uint32_t)rand();
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
        for (size_t j = 0; j < ents.size(); ++j) {
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

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;

        float max_speed = 0.0f;
        float seek_force = cfg.seek_strength * e.genes.seek;
        float flee_force = cfg.flee_strength * e.genes.flee;
        float spacing_force = cfg.spacing_strength * e.genes.spacing;
        float brownian_force = cfg.brownian_strength * e.genes.brownian;
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
        if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && e.infection_progress > 0.0f) {
            float infection_slow = 1.0f - std::min(0.72f, e.infection_progress * 0.72f);
            seek_force *= infection_slow;
            flee_force *= 0.85f * infection_slow;
            spacing_force *= 0.70f + 0.30f * infection_slow;
            brownian_force *= 0.55f + 0.45f * infection_slow;
        }

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

            if (e.type == BIO_BACTERIUM) {
                float best_host_score = 0.0f;
                int best_host = -1;
                for (size_t j = 0; j < n; j++) {
                    if (!ents[j].alive || ents[j].type != BIO_CELL) continue;
                    if (entity_has_active_infection(ents[j])) continue;
                    float tropism = bacteria_host_tropism(e, ents[j]);
                    if (tropism <= 0.0f) continue;
                    float d = glm::length(ents[j].pos - e.pos);
                    if (d <= 1.0f || d > 180.0f) continue;
                    float score = tropism * (1.0f - d / 180.0f);
                    if (score > best_host_score) {
                        best_host_score = score;
                        best_host = (int)j;
                    }
                }
                if (best_host >= 0) {
                    glm::vec3 dir = ents[best_host].pos - e.pos;
                    float d = glm::length(dir);
                    if (d > 1.0f) {
                        float colonize_bias = 0.14f + (1.0f - std::min(1.0f, e.nutrient_reserve)) * 0.26f;
                        e.vel += glm::normalize(dir) * seek_force * colonize_bias * dt;
                    }
                }
            }

            // Flee nearest virus/toxin
            float best_threat_dist = 100.0f;
            int best_threat = -1;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive) continue;
                bool is_threat = (ents[j].type == BIO_VIRUS || ents[j].type == BIO_TOXIN);
                if (!is_threat && e.type == BIO_CELL && ents[j].type == BIO_BACTERIUM)
                    is_threat = bacteria_host_tropism(ents[j], e) > 0.85f;
                if (!is_threat) continue;
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
            int best_infected = -1;
            float best_infected_dist = 80.0f;
            for (size_t j = 0; j < n; j++) {
                if (!ents[j].alive || (ents[j].type != BIO_CELL && ents[j].type != BIO_BACTERIUM)) continue;
                if (!virus_targets_host(e, ents[j])) continue;
                float d = glm::length(ents[j].pos - e.pos);
                if (ents[j].infection_progress > 0.0f) {
                    if (d < best_infected_dist) {
                        best_infected_dist = d;
                        best_infected = (int)j;
                    }
                    continue;
                }
                if (d < best_dist) { best_dist = d; best = (int)j; }
            }
            if (best >= 0) {
                glm::vec3 dir = ents[best].pos - e.pos;
                float d = glm::length(dir);
                if (d > 1.0f)
                    e.vel += glm::normalize(dir) * seek_force * 0.72f * dt;
            } else if (best_infected >= 0) {
                glm::vec3 away = e.pos - ents[best_infected].pos;
                float d = glm::length(away);
                if (d > 1.0f)
                    e.vel += glm::normalize(away) * flee_force * 0.40f * dt;
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
        else if (e.type == BIO_JANITOR) {
            max_speed = 68.0f;
            float rx = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float ry = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            float rz = (float)rand() / RAND_MAX * 2.0f - 1.0f;
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
            sim_time_ = 0.0f;
            next_entity_id_ = 1;
            event_log_.clear();
            event_log_dirty_ = false;
            push_event(BIO_EVENT_SYSTEM, "Simulation cleared.");
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
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
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
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
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
                initialize_entity_lifecycle(e, rng);
                assign_entity_identity(e);
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
    ImGui::TextColored({0.62f, 0.54f, 0.48f, 1.0f}, "  Husks: %zu", state.count_corpses());
    ImGui::SameLine();
    ImGui::TextColored({0.45f, 0.60f, 0.70f, 1.0f}, "  Features: %zu", environment_.count());
    ImGui::SameLine();
    if (ImGui::SmallButton(paused ? "Resume" : "Pause"))
        paused = !paused;
    ImGui::SameLine();
    ImGui::TextColored({0.4f, 0.4f, 0.5f, 1.0f}, "  %.0f FPS", io.Framerate);
    ImGui::SameLine();
    ImGui::TextColored({0.3f, 0.3f, 0.4f, 1.0f},
        "  |  WASD: pan  Click: inspect  Drag: orbit  Scroll: zoom  Middle: spawn  Esc: menu");
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
    std::array<GenSet, BIO_TYPE_COUNT> generation_sets;
    std::array<std::vector<size_t>, BIO_TYPE_COUNT> subtype_alive;
    std::array<std::vector<size_t>, BIO_TYPE_COUNT> subtype_infected;
    std::array<std::vector<GenSet>, BIO_TYPE_COUNT> subtype_generations;
    for (int t = 0; t < BIO_TYPE_COUNT; ++t) {
        uint32_t count = type_variant_count(static_cast<uint32_t>(t));
        subtype_alive[t].assign(count, 0);
        subtype_infected[t].assign(count, 0);
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
            if (e.infection_progress > 0.0f) {
                subtype_infected[type][subtype] += 1;
            }
        }
        if (e.infection_progress > 0.0f) {
            infected_counts[type] += 1;
            current_infected_total += 1;
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
            ImGui::Text("Current infected: %zu", infected_counts[t]);
            ImGui::Text("Ever infected: %u", ever_infected_by_type_[t]);
            ImGui::TextWrapped("Alive generations: %s", format_generation_set(generation_sets[t]).c_str());
            uint32_t subtype_count = type_variant_count(static_cast<uint32_t>(t));
            for (uint32_t subtype = 0; subtype < subtype_count; ++subtype) {
                uint32_t ever = 0;
                auto it = ever_infected_by_subtype_.find(type_subtype_key(static_cast<uint32_t>(t), subtype));
                if (it != ever_infected_by_subtype_.end())
                    ever = it->second;
                if (subtype_alive[t][subtype] == 0 && ever == 0)
                    continue;
                ImGui::BulletText("%s | alive %zu | infected %zu | ever %u | gens %s",
                    bio_entity_variant_name(static_cast<uint32_t>(t), subtype),
                    subtype_alive[t][subtype],
                    subtype_infected[t][subtype],
                    ever,
                    format_generation_set(subtype_generations[t][subtype]).c_str());
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
            ImGui::Text("Energy %.2f  Tel %.2f  Cycle %.2f",
                        avg.energy, avg.telomere, avg.mitotic_clock);
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
            ImGui::Text("Starve:  %.2f", e.starvation);
            ImGui::Text("Telomere %.0f%%", telomere_fraction_remaining(e) * 100.0f);
            ImGui::Text("Organs:  %.0f%%", e.organelle_health * 100.0f);
            ImGui::Text("Stage:   %s", bio_lifecycle_stage_name(e));
            ImGui::Text("Cycle CD %.1f s", e.division_cooldown);
            ImGui::Text("Divides: %u", e.division_count);
            if ((e.type == BIO_CELL || e.type == BIO_BACTERIUM) && entity_has_active_infection(e)) {
                ImGui::TextColored(ImVec4(0.96f, 0.48f, 0.44f, 1.0f), "Infection: %s (%.0f%%)",
                    infection_variant_name(e), e.infection_progress * 100.0f);
                ImGui::Text("%s Load: %.1f", e.infection_source_type == BIO_VIRUS ? "Virion" : "Bacterial", e.infection_load);
                ImGui::TextWrapped("Mechanism: %s", infection_mechanism_name(e));
                if (e.infection_source_id != 0)
                    ImGui::Text("Infected By: %u", e.infection_source_id);
            }
            if (e.type == BIO_BACTERIUM) {
                ImGui::Text("Antibiotic Film: %.0f%%", e.antibiotic_film * 100.0f);
                ImGui::Text("Antibiotic Types: %d", antibiotic_type_count(e));
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
            ImGui::Text("Telomr:  %.2f", e.genes.telomere);
            ImGui::Text("Cycle:   %.2f", e.genes.mitotic_clock);
            if (e.type == BIO_BACTERIUM) {
                ImGui::Text("AntiType: %.2f", e.genes.antibiotic_type);
                ImGui::Text("AntiOut:  %.2f", e.genes.antibiotic_yield);
                ImGui::Text("AntiMix:  %.2f", e.genes.antibiotic_diversity);
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
