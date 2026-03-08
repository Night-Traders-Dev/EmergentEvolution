#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "biochem/biochem_types.h"

#include <glm/glm.hpp>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>

// ═════════════════════════════════════════════════════════════════════════════
// 1. ENTITY TYPES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Entity Types") {

TEST_CASE("BIO_TYPE_COUNT is 9") {
    CHECK(BIO_TYPE_COUNT == 9);
}

TEST_CASE("Entity type enum values are sequential") {
    CHECK(BIO_CELL == 0);
    CHECK(BIO_BACTERIUM == 1);
    CHECK(BIO_VIRUS == 2);
    CHECK(BIO_NUTRIENT == 3);
    CHECK(BIO_TOXIN == 4);
    CHECK(BIO_ANTIBODY == 5);
    CHECK(BIO_RED_BLOOD == 6);
    CHECK(BIO_WHITE_BLOOD == 7);
    CHECK(BIO_JANITOR == 8);
}

} // Entity Types

// ═════════════════════════════════════════════════════════════════════════════
// 2. ENVIRONMENT PRESETS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Environment Presets") {

TEST_CASE("All 8 environments have valid presets") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(p.name != nullptr);
        CHECK(p.summary != nullptr);
        CHECK(std::strlen(p.name) > 0);
    }
}

TEST_CASE("BIO_ENV_COUNT is 8") {
    CHECK(BIO_ENV_COUNT == 8);
}

TEST_CASE("Human lung has body temperature") {
    const auto& p = bio_environment_preset(BIO_ENV_HUMAN_LUNG);
    CHECK(p.temperature_c == doctest::Approx(37.0f));
}

TEST_CASE("Pond water is cooler than body temperature") {
    const auto& p = bio_environment_preset(BIO_ENV_POND_WATER);
    CHECK(p.temperature_c < 37.0f);
    CHECK(p.temperature_c > 0.0f);
}

TEST_CASE("Blood stream has highest flow strength") {
    const auto& blood = bio_environment_preset(BIO_ENV_BLOOD);
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        if (i == BIO_ENV_BLOOD) continue;
        const auto& other = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(blood.flow_strength >= other.flow_strength);
    }
}

TEST_CASE("Wound site has highest immune pressure") {
    const auto& wound = bio_environment_preset(BIO_ENV_WOUND);
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        if (i == BIO_ENV_WOUND) continue;
        const auto& other = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(wound.immune_pressure >= other.immune_pressure);
    }
}

TEST_CASE("All environments have valid pH (0-14)") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(p.acidity_ph >= 0.0f);
        CHECK(p.acidity_ph <= 14.0f);
    }
}

TEST_CASE("All environments have oxygen level in [0, 1]") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(p.oxygen_level >= 0.0f);
        CHECK(p.oxygen_level <= 1.0f);
    }
}

TEST_CASE("Out-of-bounds environment index falls back to Human Lung") {
    const auto& fallback = bio_environment_preset(static_cast<BioEnvironmentType>(999));
    const auto& lung = bio_environment_preset(BIO_ENV_HUMAN_LUNG);
    CHECK(fallback.temperature_c == lung.temperature_c);
    CHECK(fallback.acidity_ph == lung.acidity_ph);
}

TEST_CASE("Gut has lowest oxygen (anaerobic)") {
    const auto& gut = bio_environment_preset(BIO_ENV_GUT);
    CHECK(gut.oxygen_level < 0.15f);
}

TEST_CASE("Immune system flag set correctly per environment") {
    CHECK(bio_environment_preset(BIO_ENV_HUMAN_LUNG).immune_system == true);
    CHECK(bio_environment_preset(BIO_ENV_BLOOD).immune_system == true);
    CHECK(bio_environment_preset(BIO_ENV_WOUND).immune_system == true);
    CHECK(bio_environment_preset(BIO_ENV_POND_WATER).immune_system == false);
    CHECK(bio_environment_preset(BIO_ENV_PETRI_DISH).immune_system == false);
    CHECK(bio_environment_preset(BIO_ENV_SOIL).immune_system == false);
}

TEST_CASE("All environments have positive fluid damping < 1") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(p.fluid_damping > 0.0f);
        CHECK(p.fluid_damping < 1.0f);
    }
}

} // Environment Presets

// ═════════════════════════════════════════════════════════════════════════════
// 3. CELL MORPHOLOGY
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Cell Morphology") {

TEST_CASE("Cell variant count is 8") {
    CHECK(BIO_CELL_VARIANT_COUNT == 8);
}

TEST_CASE("Bacteria variant count is 6") {
    CHECK(BIO_BACTERIA_VARIANT_COUNT == 6);
}

TEST_CASE("Virus variant count is 5") {
    CHECK(BIO_VIRUS_VARIANT_COUNT == 5);
}

TEST_CASE("All cell variant names are non-null") {
    for (uint32_t i = 0; i < BIO_CELL_VARIANT_COUNT; ++i) {
        const char* name = bio_entity_variant_name(BIO_CELL, i);
        CHECK(name != nullptr);
        CHECK(std::strlen(name) > 0);
    }
}

TEST_CASE("All bacteria variant names are non-null") {
    for (uint32_t i = 0; i < BIO_BACTERIA_VARIANT_COUNT; ++i) {
        const char* name = bio_entity_variant_name(BIO_BACTERIUM, i);
        CHECK(name != nullptr);
        CHECK(std::strlen(name) > 0);
    }
}

TEST_CASE("All virus variant names are non-null") {
    for (uint32_t i = 0; i < BIO_VIRUS_VARIANT_COUNT; ++i) {
        const char* name = bio_entity_variant_name(BIO_VIRUS, i);
        CHECK(name != nullptr);
        CHECK(std::strlen(name) > 0);
    }
}

TEST_CASE("Morphology index wraps safely") {
    // Passing out-of-range morphology should wrap via modulo
    const char* name = bio_entity_variant_name(BIO_CELL, 1000);
    CHECK(name != nullptr);
}

TEST_CASE("Default entity type returns Default name") {
    const char* name = bio_entity_variant_name(BIO_NUTRIENT, 0);
    CHECK(std::strcmp(name, "Default") == 0);
}

} // Cell Morphology

// ═════════════════════════════════════════════════════════════════════════════
// 4. VISUAL FAMILIES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Visual Families") {

TEST_CASE("Cell visual families are classified correctly") {
    CHECK(bio_cell_visual_family(BIO_CELL_GENERIC_ANIMAL) == BIO_CELL_FAMILY_ANIMAL);
    CHECK(bio_cell_visual_family(BIO_CELL_TYPE_II_PNEUMOCYTE) == BIO_CELL_FAMILY_EPITHELIAL);
    CHECK(bio_cell_visual_family(BIO_CELL_CILIATED_EPITHELIAL) == BIO_CELL_FAMILY_EPITHELIAL);
    CHECK(bio_cell_visual_family(BIO_CELL_ENTEROCYTE) == BIO_CELL_FAMILY_EPITHELIAL);
    CHECK(bio_cell_visual_family(BIO_CELL_NEURON) == BIO_CELL_FAMILY_ANIMAL);
    CHECK(bio_cell_visual_family(BIO_CELL_ASTROCYTE) == BIO_CELL_FAMILY_AMOEBOID);
    CHECK(bio_cell_visual_family(BIO_CELL_FIBROBLAST) == BIO_CELL_FAMILY_ANIMAL);
    CHECK(bio_cell_visual_family(BIO_CELL_AMOEBOID) == BIO_CELL_FAMILY_AMOEBOID);
}

TEST_CASE("Bacteria visual families are classified correctly") {
    CHECK(bio_bacteria_visual_family(BIO_BACTERIA_STAPHYLOCOCCUS_AUREUS) == BIO_BACTERIA_FAMILY_COCCI);
    CHECK(bio_bacteria_visual_family(BIO_BACTERIA_STREPTOCOCCUS_PNEUMONIAE) == BIO_BACTERIA_FAMILY_COCCI);
    CHECK(bio_bacteria_visual_family(BIO_BACTERIA_ESCHERICHIA_COLI) == BIO_BACTERIA_FAMILY_BACILLI);
    CHECK(bio_bacteria_visual_family(BIO_BACTERIA_VIBRIO_CHOLERAE) == BIO_BACTERIA_FAMILY_SPIRAL);
}

TEST_CASE("Virus visual families are classified correctly") {
    CHECK(bio_virus_visual_family(BIO_VIRUS_ADENOVIRUS_C5) == BIO_VIRUS_FAMILY_CAPSID);
    CHECK(bio_virus_visual_family(BIO_VIRUS_SARS_COV_2) == BIO_VIRUS_FAMILY_CORONA);
    CHECK(bio_virus_visual_family(BIO_VIRUS_BACTERIOPHAGE_T4) == BIO_VIRUS_FAMILY_PHAGE);
    CHECK(bio_virus_visual_family(BIO_VIRUS_INFLUENZA_A_H1N1) == BIO_VIRUS_FAMILY_INFLUENZA);
    CHECK(bio_virus_visual_family(BIO_VIRUS_INFLUENZA_A_H3N2) == BIO_VIRUS_FAMILY_INFLUENZA);
}

TEST_CASE("Visual family wraps for out-of-range morphology") {
    // Should not crash — wraps via modulo
    uint32_t f = bio_cell_visual_family(999);
    CHECK(f <= 2);
    f = bio_bacteria_visual_family(999);
    CHECK(f <= 2);
    f = bio_virus_visual_family(999);
    CHECK(f <= 3);
}

} // Visual Families

// ═════════════════════════════════════════════════════════════════════════════
// 5. MITOSIS / FISSION STAGES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Division Stages") {

TEST_CASE("Mitosis stages progress correctly") {
    CHECK(std::strcmp(bio_mitosis_stage_name(0.0f), "Interphase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(0.01f), "Interphase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(0.15f), "Prophase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(0.40f), "Metaphase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(0.70f), "Anaphase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(0.90f), "Telophase") == 0);
    CHECK(std::strcmp(bio_mitosis_stage_name(1.0f), "Cytokinesis") == 0);
}

TEST_CASE("Binary fission stages progress correctly") {
    CHECK(std::strcmp(bio_binary_fission_stage_name(0.0f), "Resting") == 0);
    CHECK(std::strcmp(bio_binary_fission_stage_name(0.15f), "DNA Replication") == 0);
    CHECK(std::strcmp(bio_binary_fission_stage_name(0.40f), "Elongation") == 0);
    CHECK(std::strcmp(bio_binary_fission_stage_name(0.70f), "Septation") == 0);
    CHECK(std::strcmp(bio_binary_fission_stage_name(0.90f), "Constriction") == 0);
    CHECK(std::strcmp(bio_binary_fission_stage_name(1.0f), "Separation") == 0);
}

TEST_CASE("Stage names are non-null at all boundaries") {
    for (float p = 0.0f; p <= 1.01f; p += 0.05f) {
        CHECK(bio_mitosis_stage_name(p) != nullptr);
        CHECK(bio_binary_fission_stage_name(p) != nullptr);
    }
}

} // Division Stages

// ═════════════════════════════════════════════════════════════════════════════
// 6. BIOGENES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("BioGenes") {

TEST_CASE("BIO_GENE_TRAIT_COUNT is 18") {
    CHECK(BIO_GENE_TRAIT_COUNT == 18);
}

TEST_CASE("Default BioGenes have positive values") {
    BioGenes g{};
    CHECK(g.seek > 0.0f);
    CHECK(g.flee > 0.0f);
    CHECK(g.energy > 0.0f);
    CHECK(g.telomere > 0.0f);
    CHECK(g.metabolism_efficiency > 0.0f);
}

TEST_CASE("Default resistance is zero") {
    BioGenes g{};
    CHECK(g.resistance == doctest::Approx(0.0f));
}

TEST_CASE("Default quorum threshold is 0.5") {
    BioGenes g{};
    CHECK(g.quorum_threshold == doctest::Approx(0.5f));
}

TEST_CASE("BioGenes struct has exactly 18 float fields") {
    // Verify by checking sizeof — 18 floats = 72 bytes
    CHECK(sizeof(BioGenes) == 18 * sizeof(float));
}

} // BioGenes

// ═════════════════════════════════════════════════════════════════════════════
// 7. BIOENTITY
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("BioEntity") {

TEST_CASE("Default BioEntity is alive cell") {
    BioEntity e{};
    CHECK(e.alive == true);
    CHECK(e.corpse == false);
    CHECK(e.type == BIO_CELL);
}

TEST_CASE("Default BioEntity has positive energy and ATP") {
    BioEntity e{};
    CHECK(e.energy > 0.0f);
    CHECK(e.atp > 0.0f);
}

TEST_CASE("Default BioEntity position is origin") {
    BioEntity e{};
    CHECK(e.pos.x == doctest::Approx(0.0f));
    CHECK(e.pos.y == doctest::Approx(0.0f));
    CHECK(e.pos.z == doctest::Approx(0.0f));
}

TEST_CASE("Default BioEntity has zero age") {
    BioEntity e{};
    CHECK(e.age == doctest::Approx(0.0f));
}

TEST_CASE("Default complement tag is zero") {
    BioEntity e{};
    CHECK(e.complement_tag == doctest::Approx(0.0f));
}

TEST_CASE("Default resistance level is zero") {
    BioEntity e{};
    CHECK(e.resistance_level == doctest::Approx(0.0f));
}

TEST_CASE("Default radius is positive") {
    BioEntity e{};
    CHECK(e.radius > 0.0f);
}

} // BioEntity

// ═════════════════════════════════════════════════════════════════════════════
// 8. BIOCHEMCONFIG
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("BiochemConfig") {

TEST_CASE("Default config has sane values") {
    BiochemConfig cfg{};
    CHECK(cfg.entity_count > 0);
    CHECK(cfg.max_entities > cfg.entity_count);
    CHECK(cfg.metabolism_rate > 0.0f);
    CHECK(cfg.division_energy > 0.0f);
    CHECK(cfg.infection_radius > 0.0f);
    CHECK(cfg.viscosity > 0.0f);
    CHECK(cfg.viscosity <= 1.0f);
    CHECK(cfg.dt_scale > 0.0f);
}

TEST_CASE("Default environment is Human Lung") {
    BiochemConfig cfg{};
    CHECK(cfg.environment == BIO_ENV_HUMAN_LUNG);
}

TEST_CASE("Default AI movement is enabled") {
    BiochemConfig cfg{};
    CHECK(cfg.ai_movement == true);
}

TEST_CASE("Default immune system is enabled") {
    BiochemConfig cfg{};
    CHECK(cfg.immune_system == true);
}

TEST_CASE("AI strengths are positive") {
    BiochemConfig cfg{};
    CHECK(cfg.seek_strength > 0.0f);
    CHECK(cfg.flee_strength > 0.0f);
    CHECK(cfg.spacing_strength > 0.0f);
    CHECK(cfg.brownian_strength > 0.0f);
}

TEST_CASE("Mutation rate is small but positive") {
    BiochemConfig cfg{};
    CHECK(cfg.mutation_rate > 0.0f);
    CHECK(cfg.mutation_rate < 1.0f);
}

TEST_CASE("World radius is positive") {
    BiochemConfig cfg{};
    CHECK(cfg.world_radius > 0.0f);
}

TEST_CASE("Default pH is near-neutral") {
    BiochemConfig cfg{};
    CHECK(cfg.acidity_ph > 6.0f);
    CHECK(cfg.acidity_ph < 8.0f);
}

} // BiochemConfig

// ═════════════════════════════════════════════════════════════════════════════
// 9. IMMUNE SUBTYPES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Immune Subtypes") {

TEST_CASE("Immune subtype constants are distinct") {
    CHECK(BIO_IMMUNE_GENERIC != BIO_IMMUNE_T_CELL);
    CHECK(BIO_IMMUNE_GENERIC != BIO_IMMUNE_B_CELL);
    CHECK(BIO_IMMUNE_T_CELL != BIO_IMMUNE_B_CELL);
}

TEST_CASE("Immune subtype values are sequential") {
    CHECK(BIO_IMMUNE_GENERIC == 0);
    CHECK(BIO_IMMUNE_T_CELL == 1);
    CHECK(BIO_IMMUNE_B_CELL == 2);
}

} // Immune Subtypes

// ═════════════════════════════════════════════════════════════════════════════
// 10. PATHOGEN STATE
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Pathogen State") {

TEST_CASE("Default BioPathogenState is inactive") {
    BioPathogenState ps{};
    CHECK(ps.progress == doctest::Approx(0.0f));
    CHECK(ps.load == doctest::Approx(0.0f));
    CHECK(ps.source_type == BIO_TYPE_COUNT);
}

TEST_CASE("Default pathogen morphology is 0") {
    BioPathogenState ps{};
    CHECK(ps.morphology == 0);
}

TEST_CASE("Pathogen genes default to BioGenes defaults") {
    BioPathogenState ps{};
    CHECK(ps.genes.seek == doctest::Approx(1.0f));
    CHECK(ps.genes.resistance == doctest::Approx(0.0f));
}

} // Pathogen State

// ═════════════════════════════════════════════════════════════════════════════
// 11. ENVIRONMENT FEATURES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Environment Features") {

TEST_CASE("BIO_ENV_FEATURE_COUNT is 5") {
    CHECK(BIO_ENV_FEATURE_COUNT == 5);
}

TEST_CASE("Feature types are sequential") {
    CHECK(BIO_ENV_FEATURE_MEMBRANE == 0);
    CHECK(BIO_ENV_FEATURE_NUTRIENT == 1);
    CHECK(BIO_ENV_FEATURE_TOXIN == 2);
    CHECK(BIO_ENV_FEATURE_CURRENT == 3);
    CHECK(BIO_ENV_FEATURE_STRUCTURE == 4);
}

TEST_CASE("Default BioEnvironmentFeature has positive radius") {
    BioEnvironmentFeature f{};
    CHECK(f.radius > 0.0f);
}

TEST_CASE("Default BioEnvironmentFeature strength is 1") {
    BioEnvironmentFeature f{};
    CHECK(f.strength == doctest::Approx(1.0f));
}

} // Environment Features

// ═════════════════════════════════════════════════════════════════════════════
// 12. STRUCTURE SHAPES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Structure Shapes") {

TEST_CASE("16 SDF structure shapes defined") {
    // Each environment has 2 shape variants = 8 env × 2 = 16
    CHECK(BIO_ENV_STRUCTURE_LUNG_BRANCH == 0);
    CHECK(BIO_ENV_STRUCTURE_WOUND_TISSUE == 15);
}

TEST_CASE("Structure shapes pair with environments") {
    // Lung shapes: 0, 1
    CHECK(BIO_ENV_STRUCTURE_LUNG_BRANCH == 0);
    CHECK(BIO_ENV_STRUCTURE_ALVEOLAR_CLUSTER == 1);
    // Pond shapes: 2, 3
    CHECK(BIO_ENV_STRUCTURE_POND_REED == 2);
    CHECK(BIO_ENV_STRUCTURE_POND_ROCK == 3);
    // Blood shapes: 10, 11
    CHECK(BIO_ENV_STRUCTURE_BLOOD_WALL == 10);
    CHECK(BIO_ENV_STRUCTURE_BLOOD_VALVE == 11);
}

} // Structure Shapes

// ═════════════════════════════════════════════════════════════════════════════
// 13. ENVIRONMENT TINT AND FLOW AXIS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Environment Visuals") {

TEST_CASE("All environment tints have valid RGB components") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(p.tint.x >= 0.0f);
        CHECK(p.tint.x <= 1.0f);
        CHECK(p.tint.y >= 0.0f);
        CHECK(p.tint.y <= 1.0f);
        CHECK(p.tint.z >= 0.0f);
        CHECK(p.tint.z <= 1.0f);
    }
}

TEST_CASE("All flow axes are finite") {
    for (uint32_t i = 0; i < BIO_ENV_COUNT; ++i) {
        const auto& p = bio_environment_preset(static_cast<BioEnvironmentType>(i));
        CHECK(std::isfinite(p.flow_axis.x));
        CHECK(std::isfinite(p.flow_axis.y));
        CHECK(std::isfinite(p.flow_axis.z));
    }
}

TEST_CASE("Blood stream flow is primarily along x-axis") {
    const auto& blood = bio_environment_preset(BIO_ENV_BLOOD);
    CHECK(blood.flow_axis.x > blood.flow_axis.y);
    CHECK(blood.flow_axis.x > blood.flow_axis.z);
}

} // Environment Visuals
