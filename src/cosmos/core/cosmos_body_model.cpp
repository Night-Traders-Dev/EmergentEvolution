#include "cosmos/cosmos_app_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

// ── Star / BH classification helpers ────────────────────────────────────────

uint32_t classify_star_spectral(float temperature, float mass) {
    // Spectral class is primarily temperature-driven; mass is used to
    // distinguish extreme hot-massive WR/O stars.
    if (temperature >= 45000.0f && mass >= 22.0f) return CTYPE_STAR_WR;
    if (temperature >= 30000.0f)                  return CTYPE_STAR_O;
    if (temperature >= 10000.0f)                  return CTYPE_STAR_B;
    if (temperature >= 7500.0f)                   return CTYPE_STAR_A;
    if (temperature >= 6000.0f)                   return CTYPE_STAR_F;
    if (temperature >= 5200.0f)                   return CTYPE_STAR_G;
    if (temperature >= 3700.0f)                   return CTYPE_STAR_K;
    if (temperature >= 2400.0f)                   return CTYPE_STAR_M;
    if (temperature >= 1300.0f)                   return CTYPE_STAR_L;
    if (temperature >= 700.0f)                    return CTYPE_STAR_T;
    return CTYPE_STAR_Y;
}

void clear_ring_system(CelestialBody& body) {
    body.ring_inner_radius = 0.0f;
    body.ring_outer_radius = 0.0f;
    body.ring_density = 0.0f;
    body.ring_ice_fraction = 0.0f;
    body.ring_tilt = 0.0f;
}

void clear_impact_signature(CelestialBody& body) {
    body.impact_normal = glm::vec3(0.0f, 1.0f, 0.0f);
    body.impact_crater_strength = 0.0f;
    body.impact_heat = 0.0f;
    body.impact_radius = 0.0f;
    body.impact_ejecta = 0.0f;
}

bool body_can_host_rings(const CelestialBody& body) {
    if (is_star_type(body.type) || is_black_hole_type(body.type))
        return false;
    return body.type == CTYPE_PLANET || body.type == CTYPE_MOON || body.type == CTYPE_NEBULA;
}

bool fragment_like_body(const CelestialBody& body) {
    return (int)body.frag_generation > 0 &&
        !is_star_type(body.type) && !is_black_hole_type(body.type);
}

bool roche_secondary_fluid_like(const CelestialBody& body) {
    if (body.type == CTYPE_NEBULA || body.type == CTYPE_COMET)
        return true;
    if (body.material_phase == PHASE_GAS || body.material_phase == PHASE_LIQUID ||
        body.material_phase == PHASE_PLASMA || body.material_phase == PHASE_COLLAPSING)
        return true;
    if ((body.type == CTYPE_PLANET || body.type == CTYPE_MOON) &&
        body.props_valid && body.cached_props.surface == SURF_GAS)
        return true;
    return false;
}

float roche_distance_for_mode(const CelestialBody& primary,
                                     const CelestialBody& secondary,
                                     bool fluid_mode) {
    float rho_primary = std::max(body_density(primary), 1.0e-6f);
    float rho_secondary = std::max(body_density(secondary), 1.0e-6f);
    float ratio = std::max(rho_primary / rho_secondary, 1.0e-4f);
    if (fluid_mode) {
        // Fluid Roche limit: d ≈ 2.423 R_M (rho_M / rho_m)^(1/3)
        return 2.423f * std::max(primary.radius, 1.0e-4f) * std::cbrt(ratio);
    }
    // Rigid Roche limit: d = R_M (2 rho_M / rho_m)^(1/3)
    return std::max(primary.radius, 1.0e-4f) * std::cbrt(2.0f * ratio);
}

void set_ring_system(CelestialBody& body, float inner_radius, float outer_radius,
                            float density, float ice_fraction, float tilt) {
    body.ring_inner_radius = std::max(inner_radius, body.radius * 1.15f);
    body.ring_outer_radius = std::max(outer_radius, body.ring_inner_radius + body.radius * 0.25f);
    body.ring_density = std::clamp(density, 0.0f, 1.0f);
    body.ring_ice_fraction = std::clamp(ice_fraction, 0.0f, 1.0f);
    body.ring_tilt = std::clamp(tilt, 0.0f, 1.30f);
}

uint32_t classify_black_hole(float mass) {
    if (mass < 3.0f)        return CTYPE_BH_PRIMORDIAL;
    if (mass <= 20.0f)      return CTYPE_BH_STELLAR;
    if (mass <= 100000.0f)  return CTYPE_BH_INTERMEDIATE;
    return CTYPE_BH_SUPERMASSIVE;
}

void star_spectral_bounds(uint32_t type, float& min_mass, float& max_mass,
                                 float& min_temp, float& max_temp) {
    switch (type) {
    case CTYPE_STAR_O:  min_mass = 16.0f;  max_mass = 150.0f; min_temp = 30000.0f; max_temp = 52000.0f; break;
    case CTYPE_STAR_B:  min_mass = 2.1f;   max_mass = 16.0f;  min_temp = 10000.0f; max_temp = 30000.0f; break;
    case CTYPE_STAR_A:  min_mass = 1.4f;   max_mass = 2.5f;   min_temp = 7500.0f;  max_temp = 10000.0f; break;
    case CTYPE_STAR_F:  min_mass = 1.04f;  max_mass = 1.4f;   min_temp = 6000.0f;  max_temp = 7500.0f; break;
    case CTYPE_STAR_G:  min_mass = 0.8f;   max_mass = 1.04f;  min_temp = 5200.0f;  max_temp = 6000.0f; break;
    case CTYPE_STAR_K:  min_mass = 0.45f;  max_mass = 0.8f;   min_temp = 3700.0f;  max_temp = 5200.0f; break;
    case CTYPE_STAR_M:  min_mass = 0.08f;  max_mass = 0.45f;  min_temp = 2400.0f;  max_temp = 3700.0f; break;
    case CTYPE_STAR_L:  min_mass = 0.013f; max_mass = 0.08f;  min_temp = 1300.0f;  max_temp = 2400.0f; break;
    case CTYPE_STAR_T:  min_mass = 0.007f; max_mass = 0.05f;  min_temp = 700.0f;   max_temp = 1300.0f; break;
    case CTYPE_STAR_Y:  min_mass = 0.003f; max_mass = 0.03f;  min_temp = 250.0f;   max_temp = 700.0f; break;
    case CTYPE_STAR_WR: min_mass = 20.0f;  max_mass = 150.0f; min_temp = 45000.0f; max_temp = 120000.0f; break;
    default:            min_mass = 0.075f; max_mass = 120.0f; min_temp = 2200.0f;  max_temp = 45000.0f; break;
    }
}

float expected_main_sequence_temperature(float mass) {
    if (mass < 0.013f) return 350.0f;
    if (mass < 0.08f) return 1400.0f + (mass - 0.013f) / 0.067f * 1000.0f;
    if (mass < 0.45f) return 2400.0f + (mass - 0.08f) / 0.37f * 1300.0f;
    if (mass < 0.8f)  return 3700.0f + (mass - 0.45f) / 0.35f * 1500.0f;
    if (mass < 1.04f) return 5200.0f + (mass - 0.8f) / 0.24f * 600.0f;
    if (mass < 1.4f)  return 5800.0f + (mass - 1.04f) / 0.36f * 1700.0f;
    if (mass < 2.5f)  return 7500.0f + (mass - 1.4f) / 1.1f * 2500.0f;
    if (mass < 16.0f) return 10000.0f + (mass - 2.5f) / 13.5f * 20000.0f;
    return 30000.0f + std::clamp((mass - 16.0f) / 134.0f, 0.0f, 1.0f) * 22000.0f;
}

float stellar_rotation_period_hours(float mass, uint32_t type, uint32_t stage,
                                           std::mt19937& rng) {
    if (stage == SSTAGE_NEUTRON_STAR)
        return std::uniform_real_distribution<float>(0.0004f, 0.08f)(rng);
    if (stage == SSTAGE_WHITE_DWARF)
        return std::uniform_real_distribution<float>(0.12f, 48.0f)(rng);
    if (stage == SSTAGE_RED_GIANT || stage == SSTAGE_AGB)
        return std::uniform_real_distribution<float>(1200.0f, 16000.0f)(rng);
    if (stage == SSTAGE_SUPERGIANT || stage == SSTAGE_HYPERGIANT)
        return std::uniform_real_distribution<float>(400.0f, 7000.0f)(rng);
    if (type == CTYPE_STAR_WR || mass > 16.0f)
        return std::uniform_real_distribution<float>(6.0f, 52.0f)(rng);
    if (mass > 2.0f)
        return std::uniform_real_distribution<float>(10.0f, 140.0f)(rng);
    if (mass > 0.8f)
        return std::uniform_real_distribution<float>(90.0f, 650.0f)(rng);
    return std::uniform_real_distribution<float>(180.0f, 2200.0f)(rng);
}

float expected_star_radius(const CelestialBody& b);
float expected_stellar_luminosity(float mass, float temperature, float radius,
                                         uint32_t stage, float fuel);

void randomize_small_body_properties(CelestialBody& body, std::mt19937& rng,
                                            bool is_comet) {
    // Radii are in simulation units (Earth radius ~= 8 units).
    // Asteroids: mostly rocky, Comets: icy and lower density.
    float radius_km;
    float density_kg_m3;

    if (is_comet) {
        radius_km = std::uniform_real_distribution<float>(1.0f, 35.0f)(rng);
        density_kg_m3 = std::uniform_real_distribution<float>(300.0f, 900.0f)(rng);
        body.temperature = std::uniform_real_distribution<float>(40.0f, 260.0f)(rng);
    } else {
        radius_km = std::uniform_real_distribution<float>(2.0f, 500.0f)(rng);
        density_kg_m3 = std::uniform_real_distribution<float>(1200.0f, 3500.0f)(rng);
        body.temperature = std::uniform_real_distribution<float>(120.0f, 420.0f)(rng);
    }

    // Convert radius to simulation units.
    body.radius = radius_km / SIM_UNIT_TO_KM;

    // Physical mass estimate from volume + density, then convert kg -> solar masses.
    constexpr double SOLAR_MASS_KG = 1.98847e30;
    double radius_m = (double)radius_km * 1000.0;
    double volume_m3 = (4.0 / 3.0) * 3.141592653589793 * radius_m * radius_m * radius_m;
    double mass_kg = volume_m3 * (double)density_kg_m3;
    body.mass = (float)(mass_kg / SOLAR_MASS_KG);

    // Spin periods: small bodies can spin rapidly but avoid breakup extremes.
    float period_h = is_comet
        ? std::uniform_real_distribution<float>(4.0f, 80.0f)(rng)
        : std::uniform_real_distribution<float>(2.5f, 30.0f)(rng);
    body.angular_vel = (2.0f * 3.14159265359f) / (period_h * 3600.0f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.2f)
        body.angular_vel *= -1.0f;
}

void randomize_dust_properties(CelestialBody& body, std::mt19937& rng) {
    float radius_km = std::uniform_real_distribution<float>(0.08f, 2.0f)(rng);
    float density_kg_m3 = std::uniform_real_distribution<float>(900.0f, 2600.0f)(rng);
    body.temperature = std::uniform_real_distribution<float>(35.0f, 260.0f)(rng);

    body.radius = std::max(0.05f, radius_km / SIM_UNIT_TO_KM);

    constexpr double SOLAR_MASS_KG = 1.98847e30;
    double radius_m = (double)radius_km * 1000.0;
    double volume_m3 = (4.0 / 3.0) * 3.141592653589793 * radius_m * radius_m * radius_m;
    double mass_kg = volume_m3 * (double)density_kg_m3;
    body.mass = std::max((float)(mass_kg / SOLAR_MASS_KG), 1.0e-13f);

    float period_h = std::uniform_real_distribution<float>(1.0f, 18.0f)(rng);
    body.angular_vel = (2.0f * 3.14159265359f) / (period_h * 3600.0f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.35f)
        body.angular_vel *= -1.0f;
    body.atmosphere_retention = 0.0f;
    body.material_phase = (body.temperature < 170.0f) ? PHASE_ICE : PHASE_SOLID;
    body.phase_intensity = std::clamp(0.25f + std::uniform_real_distribution<float>(0.0f, 0.45f)(rng), 0.1f, 1.0f);
}

void randomize_nebula_properties(CelestialBody& body, std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    body.type = CTYPE_NEBULA;
    body.temperature = std::uniform_real_distribution<float>(18.0f, 140.0f)(rng);
    body.radius = std::max(35.0f, std::cbrt(std::max(body.mass, 1.0e-5f)) * 120.0f);
    body.angular_vel = (u01(rng) * 2.0f - 1.0f) * 0.0008f;
    body.atmosphere_retention = 1.0f;
    body.material_phase = PHASE_GAS;
    body.phase_intensity = 0.65f + u01(rng) * 0.30f;
    body.collapse_progress = std::clamp(body.mass / HYDROGEN_BURNING_MASS_SOLAR, 0.0f, 0.55f) * 0.35f;
    body.custom_material = true;
    body.custom_hydrogen = 0.90f;
    body.custom_silicate = 0.06f;
    body.custom_water = 0.03f;
    body.custom_iron = 0.01f;
    body.forced_surface = -1;
    clear_ring_system(body);
    clear_impact_signature(body);
}

void randomize_star_properties(CelestialBody& body, std::mt19937& rng,
                                      uint32_t requested_type) {
    constexpr float PI = 3.14159265359f;
    float min_mass, max_mass, min_temp, max_temp;
    star_spectral_bounds(requested_type, min_mass, max_mass, min_temp, max_temp);

    if (requested_type != CTYPE_STAR)
        body.mass = std::max(body.mass, min_mass);
    body.mass = std::clamp(body.mass, 0.003f, MAX_MAIN_SEQUENCE_MASS_SOLAR);

    if (requested_type != CTYPE_STAR) {
        body.temperature = std::clamp(
            std::uniform_real_distribution<float>(min_temp, max_temp)(rng) *
            std::uniform_real_distribution<float>(0.96f, 1.04f)(rng),
            min_temp, max_temp * 1.03f);
    } else {
        float base_t = expected_main_sequence_temperature(body.mass);
        body.temperature = std::clamp(
            base_t * std::uniform_real_distribution<float>(0.92f, 1.10f)(rng),
            1800.0f, 55000.0f);
    }

    body.type = classify_star_spectral(body.temperature, body.mass);
    body.stellar_stage = SSTAGE_MAIN_SEQUENCE;
    if (requested_type == CTYPE_STAR) {
        float v = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
        if (body.mass < 8.0f) {
            if (v < 0.10f) body.stellar_stage = SSTAGE_SUBGIANT;
            else if (v < 0.16f) body.stellar_stage = SSTAGE_RED_GIANT;
            else if (v < 0.18f) body.stellar_stage = SSTAGE_AGB;
            else if (v < 0.20f) body.stellar_stage = SSTAGE_WHITE_DWARF;
        } else {
            if (v < 0.08f) body.stellar_stage = SSTAGE_SUPERGIANT;
            else if (v < 0.12f) body.stellar_stage = SSTAGE_HYPERGIANT;
            else if (v < 0.14f) body.stellar_stage = SSTAGE_NEUTRON_STAR;
        }
    }

    body.fuel = std::clamp(std::uniform_real_distribution<float>(0.55f, 1.0f)(rng) -
                           std::clamp((body.mass - 8.0f) / 80.0f, 0.0f, 0.18f),
                           0.25f, 1.0f);
    if (body.stellar_stage == SSTAGE_WHITE_DWARF || body.stellar_stage == SSTAGE_NEUTRON_STAR)
        body.fuel = 0.0f;

    if (body.stellar_stage == SSTAGE_WHITE_DWARF) {
        body.temperature = std::uniform_real_distribution<float>(9000.0f, 110000.0f)(rng);
    } else if (body.stellar_stage == SSTAGE_NEUTRON_STAR) {
        body.temperature = std::uniform_real_distribution<float>(70000.0f, 220000.0f)(rng);
    } else if (body.stellar_stage == SSTAGE_RED_GIANT || body.stellar_stage == SSTAGE_AGB) {
        body.temperature = std::uniform_real_distribution<float>(2800.0f, 5200.0f)(rng);
    } else if (body.stellar_stage == SSTAGE_SUBGIANT) {
        body.temperature = std::uniform_real_distribution<float>(4200.0f, 7600.0f)(rng);
    } else if (body.stellar_stage == SSTAGE_SUPERGIANT || body.stellar_stage == SSTAGE_HYPERGIANT) {
        float base_t = expected_main_sequence_temperature(std::max(body.mass, 8.0f));
        body.temperature = std::clamp(base_t * std::uniform_real_distribution<float>(0.52f, 1.20f)(rng),
                                      3200.0f, 52000.0f);
    }
    body.type = classify_star_spectral(std::max(body.temperature, 250.0f), std::max(body.mass, 0.003f));

    float radius_jitter = (body.stellar_stage == SSTAGE_WHITE_DWARF || body.stellar_stage == SSTAGE_NEUTRON_STAR)
        ? std::uniform_real_distribution<float>(0.92f, 1.10f)(rng)
        : std::uniform_real_distribution<float>(0.88f, 1.15f)(rng);
    body.radius = expected_star_radius(body) * radius_jitter;
    float period_h = stellar_rotation_period_hours(body.mass, body.type, body.stellar_stage, rng);
    body.angular_vel = (2.0f * PI) / (period_h * 3600.0f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.08f)
        body.angular_vel *= -1.0f;
    body.luminosity = expected_stellar_luminosity(body.mass, body.temperature, body.radius,
                                                  body.stellar_stage, body.fuel) *
                      std::uniform_real_distribution<float>(0.88f, 1.12f)(rng);
}

float merged_star_fuel(const CelestialBody& a, const CelestialBody& b, float total_mass) {
    float weighted = (a.fuel * a.mass + b.fuel * b.mass) / std::max(total_mass, 1.0e-6f);
    float mixing_bonus = std::clamp(0.10f + 0.20f * std::min(a.mass, b.mass) / std::max(total_mass, 1.0e-6f),
                                    0.0f, 0.22f);
    return std::clamp(weighted * 0.82f + mixing_bonus, 0.05f, 1.0f);
}

StellarRemnantKind stellar_remnant_kind(float progenitor_mass, bool thermonuclear) {
    if (thermonuclear) return REMNANT_NONE;
    if (progenitor_mass < CORE_COLLAPSE_MIN_MASS_SOLAR) return REMNANT_WHITE_DWARF;
    if (progenitor_mass < BLACK_HOLE_MIN_REMNANT_MASS_SOLAR) return REMNANT_NEUTRON_STAR;
    return REMNANT_BLACK_HOLE;
}


void randomize_planet_properties(CelestialBody& body, const CosmosState& state,
                                        const CosmosConfig& cfg, std::mt19937& rng) {
    // Sample from broad observed exoplanet-like ranges.
    // Internal mass unit is solar masses; 1 Earth mass ~= 3.003e-6 solar masses.
    constexpr float EARTH_MASS_TO_SOLAR = 3.003e-6f;
    constexpr float PI = 3.14159265359f;

    enum PlanetSpawnArchetype {
        SPAWN_DWARF_ICE = 0,
        SPAWN_TERRESTRIAL,
        SPAWN_OCEAN,
        SPAWN_DESERT,
        SPAWN_LAVA,
        SPAWN_ICE_GIANT,
        SPAWN_GAS_GIANT,
    };

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // Use broad weighted categories so dwarf planets, terrestrial worlds,
    // ice giants, and gas giants all appear regularly.
    float bucket = u01(rng);
    PlanetSpawnArchetype archetype = SPAWN_TERRESTRIAL;
    if (bucket < 0.12f) archetype = SPAWN_DWARF_ICE;
    else if (bucket < 0.38f) archetype = SPAWN_TERRESTRIAL;
    else if (bucket < 0.56f) archetype = SPAWN_OCEAN;
    else if (bucket < 0.70f) archetype = SPAWN_DESERT;
    else if (bucket < 0.78f) archetype = SPAWN_LAVA;
    else if (bucket < 0.90f) archetype = SPAWN_ICE_GIANT;
    else archetype = SPAWN_GAS_GIANT;

    float mass_earth = 1.0f;
    switch (archetype) {
    case SPAWN_DWARF_ICE:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.02f), std::log10(0.20f))(rng));
        break;
    case SPAWN_TERRESTRIAL:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.35f), std::log10(2.5f))(rng));
        break;
    case SPAWN_OCEAN:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.6f), std::log10(5.0f))(rng));
        break;
    case SPAWN_DESERT:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.4f), std::log10(3.5f))(rng));
        break;
    case SPAWN_LAVA:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.5f), std::log10(4.5f))(rng));
        break;
    case SPAWN_ICE_GIANT:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(9.0f), std::log10(40.0f))(rng));
        break;
    case SPAWN_GAS_GIANT:
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(45.0f), std::log10(1000.0f))(rng));
        break;
    }
    float mass_hint_earth = body.mass / EARTH_MASS_TO_SOLAR;
    if (mass_hint_earth >= 0.02f && mass_hint_earth <= 2000.0f) {
        float hinted = mass_hint_earth * std::pow(10.0f, std::uniform_real_distribution<float>(-0.22f, 0.22f)(rng));
        mass_earth = glm::mix(mass_earth, hinted, 0.88f);
    }
    mass_earth *= std::uniform_real_distribution<float>(0.72f, 1.38f)(rng);
    mass_earth = std::clamp(mass_earth, 0.02f, 1200.0f);
    body.mass = std::clamp(mass_earth * EARTH_MASS_TO_SOLAR, 1.0e-7f, 0.01f);

    float density_earth = 1.0f;
    switch (archetype) {
    case SPAWN_DWARF_ICE: density_earth = std::uniform_real_distribution<float>(0.85f, 1.85f)(rng); break;
    case SPAWN_TERRESTRIAL: density_earth = std::uniform_real_distribution<float>(0.80f, 1.45f)(rng); break;
    case SPAWN_OCEAN: density_earth = std::uniform_real_distribution<float>(0.52f, 1.05f)(rng); break;
    case SPAWN_DESERT: density_earth = std::uniform_real_distribution<float>(0.92f, 1.60f)(rng); break;
    case SPAWN_LAVA: density_earth = std::uniform_real_distribution<float>(1.00f, 1.85f)(rng); break;
    case SPAWN_ICE_GIANT: density_earth = std::uniform_real_distribution<float>(0.18f, 0.42f)(rng); break;
    case SPAWN_GAS_GIANT: density_earth = std::uniform_real_distribution<float>(0.08f, 0.32f)(rng); break;
    }
    float radius_earth = std::cbrt(std::max(mass_earth / std::max(density_earth, 0.05f), 1.0e-4f)) *
                         std::uniform_real_distribution<float>(0.90f, 1.12f)(rng);
    if (archetype == SPAWN_GAS_GIANT)
        radius_earth = std::min(radius_earth, 13.6f);
    else if (archetype == SPAWN_ICE_GIANT)
        radius_earth = std::min(radius_earth, 6.8f);
    radius_earth = std::clamp(radius_earth, 0.28f, 13.6f);
    body.radius = radius_earth * EARTH_RADIUS_SIM_UNITS;

    // Estimate equilibrium temperature from nearest star if available.
    // Using relative stellar scaling: T_eq ~ T_star * sqrt(R_star/(2d)) * (1-A)^(1/4)
    int nearest_star = -1;
    float nearest_dist = 1e30f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        if (!is_star_type(b.type)) continue;
        float d = glm::length(b.pos - body.pos);
        if (d > 1e-3f && d < nearest_dist) {
            nearest_dist = d;
            nearest_star = (int)i;
        }
    }

    float albedo = std::uniform_real_distribution<float>(0.08f, 0.75f)(rng);
    if (nearest_star >= 0) {
        const auto& s = state.bodies[(size_t)nearest_star];
        float ratio = std::sqrt(std::max(s.radius, 1.0f) / (2.0f * std::max(nearest_dist, 1.0f)));
        float eq_t = s.temperature * ratio * std::pow(std::max(1.0f - albedo, 0.05f), 0.25f);
        float green_min = 0.9f;
        float green_max = 1.25f;
        switch (archetype) {
        case SPAWN_DWARF_ICE: green_min = 0.80f; green_max = 1.00f; break;
        case SPAWN_TERRESTRIAL: green_min = 0.92f; green_max = 1.18f; break;
        case SPAWN_OCEAN: green_min = 0.96f; green_max = 1.10f; break;
        case SPAWN_DESERT: green_min = 1.02f; green_max = 1.24f; break;
        case SPAWN_LAVA: green_min = 1.08f; green_max = 1.28f; break;
        case SPAWN_ICE_GIANT: green_min = 0.84f; green_max = 1.02f; break;
        case SPAWN_GAS_GIANT: green_min = 0.88f; green_max = 1.12f; break;
        }
        float greenhouse = std::uniform_real_distribution<float>(green_min, green_max)(rng);
        body.temperature = std::clamp(eq_t * greenhouse, 60.0f, 2500.0f);
    } else {
        switch (archetype) {
        case SPAWN_DWARF_ICE:
            body.temperature = std::uniform_real_distribution<float>(45.0f, 180.0f)(rng);
            break;
        case SPAWN_TERRESTRIAL:
            body.temperature = std::uniform_real_distribution<float>(190.0f, 360.0f)(rng);
            break;
        case SPAWN_OCEAN:
            body.temperature = std::uniform_real_distribution<float>(255.0f, 325.0f)(rng);
            break;
        case SPAWN_DESERT:
            body.temperature = std::uniform_real_distribution<float>(290.0f, 620.0f)(rng);
            break;
        case SPAWN_LAVA:
            body.temperature = std::uniform_real_distribution<float>(650.0f, 1400.0f)(rng);
            break;
        case SPAWN_ICE_GIANT:
            body.temperature = std::uniform_real_distribution<float>(65.0f, 170.0f)(rng);
            break;
        case SPAWN_GAS_GIANT:
            body.temperature = std::uniform_real_distribution<float>(90.0f, 900.0f)(rng);
            break;
        }
    }

    // Rotation period in hours: small rocky planets trend faster than giants.
    float period_hours;
    if (mass_earth < 2.0f) {
        period_hours = std::uniform_real_distribution<float>(10.0f, 120.0f)(rng);
    } else if (mass_earth < 130.0f) {
        period_hours = std::uniform_real_distribution<float>(8.0f, 50.0f)(rng);
    } else {
        period_hours = std::uniform_real_distribution<float>(6.0f, 20.0f)(rng);
    }
    body.angular_vel = (2.0f * PI) / (period_hours * 3600.0f);
    if (u01(rng) < 0.15f) body.angular_vel *= -1.0f; // occasional retrograde spin
    switch (archetype) {
    case SPAWN_DWARF_ICE: body.atmosphere_retention = std::uniform_real_distribution<float>(0.10f, 0.45f)(rng); break;
    case SPAWN_TERRESTRIAL: body.atmosphere_retention = std::uniform_real_distribution<float>(0.55f, 0.95f)(rng); break;
    case SPAWN_OCEAN: body.atmosphere_retention = std::uniform_real_distribution<float>(0.80f, 1.00f)(rng); break;
    case SPAWN_DESERT: body.atmosphere_retention = std::uniform_real_distribution<float>(0.35f, 0.85f)(rng); break;
    case SPAWN_LAVA: body.atmosphere_retention = std::uniform_real_distribution<float>(0.20f, 0.70f)(rng); break;
    case SPAWN_ICE_GIANT: body.atmosphere_retention = std::uniform_real_distribution<float>(0.92f, 1.00f)(rng); break;
    case SPAWN_GAS_GIANT: body.atmosphere_retention = 1.0f; break;
    }

    clear_ring_system(body);
    clear_impact_signature(body);
    float ring_roll = u01(rng);
    bool giant = (archetype == SPAWN_ICE_GIANT || archetype == SPAWN_GAS_GIANT);
    bool dwarf_ring = (archetype == SPAWN_DWARF_ICE && ring_roll > 0.82f);
    bool rocky_ring = ((archetype == SPAWN_TERRESTRIAL || archetype == SPAWN_OCEAN || archetype == SPAWN_DESERT) &&
                       ring_roll > 0.95f);
    if (giant || dwarf_ring || rocky_ring) {
        float inner_mult = giant ? std::uniform_real_distribution<float>(1.45f, 1.95f)(rng)
                                 : std::uniform_real_distribution<float>(1.25f, 1.70f)(rng);
        float outer_mult = giant ? std::uniform_real_distribution<float>(2.6f, 4.8f)(rng)
                                 : std::uniform_real_distribution<float>(2.0f, 3.4f)(rng);
        float density = giant ? std::uniform_real_distribution<float>(0.18f, 0.62f)(rng)
                              : std::uniform_real_distribution<float>(0.10f, 0.36f)(rng);
        float ice_fraction = (archetype == SPAWN_DWARF_ICE || archetype == SPAWN_ICE_GIANT)
            ? std::uniform_real_distribution<float>(0.55f, 0.95f)(rng)
            : (giant ? std::uniform_real_distribution<float>(0.22f, 0.68f)(rng)
                     : std::uniform_real_distribution<float>(0.05f, 0.32f)(rng));
        float tilt = giant ? std::uniform_real_distribution<float>(0.04f, 0.25f)(rng)
                           : std::uniform_real_distribution<float>(0.06f, 0.45f)(rng);
        set_ring_system(body,
                        body.radius * inner_mult * std::clamp(cfg.ring_inner_scale, 0.6f, 3.0f),
                        body.radius * outer_mult * std::clamp(cfg.ring_outer_scale, 0.6f, 3.0f),
                        density * std::clamp(cfg.ring_density_scale, 0.2f, 3.0f),
                        ice_fraction, tilt);
    }

    body.phase_intensity = 0.0f;
    body.collapse_progress = 0.0f;
    clear_impact_signature(body);
}

void randomize_moon_properties(CelestialBody& body, const CosmosState& state,
                                      std::mt19937& rng) {
    constexpr float EARTH_MASS_TO_SOLAR = 3.003e-6f;
    constexpr float PI = 3.14159265359f;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    float bucket = u01(rng);
    float mass_earth;
    if (bucket < 0.70f) {
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.0005f), std::log10(0.03f))(rng));
    } else if (bucket < 0.95f) {
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.03f), std::log10(0.12f))(rng));
    } else {
        mass_earth = std::pow(10.0f, std::uniform_real_distribution<float>(
            std::log10(0.12f), std::log10(0.22f))(rng));
    }
    float moon_mass_hint_earth = body.mass / EARTH_MASS_TO_SOLAR;
    if (moon_mass_hint_earth >= 0.0002f && moon_mass_hint_earth <= 0.40f) {
        float hinted = moon_mass_hint_earth * std::pow(10.0f, std::uniform_real_distribution<float>(-0.18f, 0.18f)(rng));
        mass_earth = glm::mix(mass_earth, hinted, 0.92f);
    }
    mass_earth *= std::uniform_real_distribution<float>(0.68f, 1.52f)(rng);
    mass_earth = std::clamp(mass_earth, 0.0002f, 0.35f);
    body.mass = std::clamp(mass_earth * EARTH_MASS_TO_SOLAR, 1.0e-8f, 8.0e-7f);

    float density_earth = std::uniform_real_distribution<float>(0.70f, 1.80f)(rng);
    if (bucket < 0.30f) density_earth = std::uniform_real_distribution<float>(0.55f, 1.10f)(rng);
    if (bucket > 0.88f) density_earth = std::uniform_real_distribution<float>(1.10f, 2.10f)(rng);
    float radius_earth = std::clamp(std::cbrt(std::max(mass_earth / std::max(density_earth, 0.08f), 1.0e-5f)) *
                                        std::uniform_real_distribution<float>(0.88f, 1.15f)(rng),
                                    0.04f, 0.82f);
    body.radius = radius_earth * EARTH_RADIUS_SIM_UNITS;

    int nearest_star = -1;
    float nearest_dist = 1e30f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        if (!is_star_type(b.type)) continue;
        float d = glm::length(b.pos - body.pos);
        if (d > 1e-3f && d < nearest_dist) {
            nearest_dist = d;
            nearest_star = (int)i;
        }
    }

    float albedo = std::uniform_real_distribution<float>(0.05f, 0.65f)(rng);
    if (nearest_star >= 0) {
        const auto& s = state.bodies[(size_t)nearest_star];
        float ratio = std::sqrt(std::max(s.radius, 1.0f) / (2.0f * std::max(nearest_dist, 1.0f)));
        float eq_t = s.temperature * ratio * std::pow(std::max(1.0f - albedo, 0.05f), 0.25f);
        float greenhouse = std::uniform_real_distribution<float>(0.85f, 1.12f)(rng);
        body.temperature = std::clamp(eq_t * greenhouse, 40.0f, 900.0f);
    } else {
        body.temperature = std::uniform_real_distribution<float>(60.0f, 420.0f)(rng);
    }

    float period_hours = std::uniform_real_distribution<float>(60.0f, 900.0f)(rng);
    if (u01(rng) < 0.35f)
        period_hours = std::uniform_real_distribution<float>(180.0f, 1600.0f)(rng);
    body.angular_vel = (2.0f * PI) / (period_hours * 3600.0f);
    if (u01(rng) < 0.05f) body.angular_vel *= -1.0f;

    body.atmosphere_retention = std::clamp(0.08f + mass_earth * 2.4f +
                                           (body.temperature < 180.0f ? 0.10f : 0.0f),
                                           0.05f, 0.82f);
    clear_ring_system(body);
    body.phase_intensity = 0.0f;
    body.collapse_progress = 0.0f;
    clear_impact_signature(body);
}

void enforce_body_physical_limits(CelestialBody& b);

void refresh_body_render_state(CelestialBody& body, const CosmosState* state) {
    enforce_body_physical_limits(body);
    refresh_planet_props(body);
    refresh_body_visuals(body, state);
}

float body_density(const CelestialBody& b) {
    float volume = (4.0f / 3.0f) * 3.14159265359f * b.radius * b.radius * b.radius;
    return b.mass / std::max(volume, 1.0e-5f);
}

float body_volume(const CelestialBody& b) {
    return (4.0f / 3.0f) * 3.14159265359f * b.radius * b.radius * b.radius;
}

float body_gravitational_binding_energy(const CelestialBody& b, float G) {
    return 0.6f * G * b.mass * b.mass / std::max(b.radius, 0.1f);
}

float body_surface_gravity(const CelestialBody& b, float G) {
    return G * b.mass / std::max(b.radius * b.radius, 1.0e-6f);
}

float body_escape_velocity(const CelestialBody& b, float G) {
    return std::sqrt(std::max(2.0f * G * b.mass / std::max(b.radius, 1.0e-6f), 0.0f));
}

float body_escape_speed(const CelestialBody& a, const CelestialBody& b, float G) {
    float sep = std::max(a.radius + b.radius, 1.0f);
    return std::sqrt(std::max(2.0f * G * (a.mass + b.mass) / sep, 0.0f));
}

void register_mass_loss(CelestialBody& b, float amount, float dt) {
    if (amount <= 0.0f || dt <= 0.0f) return;
    b.mass_loss_rate += amount / dt;
    b.mass_loss_total += amount;
}

void apply_impact_signature(CelestialBody& target, glm::vec3 impact_normal,
                                   float energy_ratio, float mass_fraction,
                                   float thermal_ratio, float crater_radius) {
    if (target.marked_for_removal) return;
    if (is_star_type(target.type) || is_black_hole_type(target.type) || target.type == CTYPE_NEBULA) return;

    glm::vec3 normal = glm::length(impact_normal) > 1.0e-4f
        ? glm::normalize(impact_normal)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    float scar = std::clamp(0.10f + energy_ratio * 0.55f + mass_fraction * 0.40f, 0.0f, 1.0f);
    float glow = std::clamp(0.06f + thermal_ratio * 0.70f + energy_ratio * 0.22f, 0.0f, 1.0f);
    float radius = std::clamp(0.10f + crater_radius * 0.35f + mass_fraction * 0.30f, 0.08f, 0.95f);
    float ejecta = std::clamp(0.08f + energy_ratio * 0.38f + thermal_ratio * 0.28f, 0.0f, 1.0f);

    target.impact_normal = glm::normalize(glm::mix(target.impact_normal, normal, scar > 0.45f ? 0.85f : 0.55f));
    target.impact_crater_strength = std::max(target.impact_crater_strength * 0.78f, scar);
    target.impact_heat = std::max(target.impact_heat * 0.82f, glow);
    target.impact_radius = std::max(target.impact_radius * 0.82f, radius);
    target.impact_ejecta = std::max(target.impact_ejecta * 0.76f, ejecta);
    target.props_valid = false;
    target.visuals_valid = false;
}

float expected_planet_radius(float mass_solar) {
    float mass_earth = std::max(mass_solar / EARTH_MASS_SOLAR, 0.01f);
    float radius_earth;
    if (mass_earth < 2.0f) {
        radius_earth = std::pow(mass_earth, 0.28f);
    } else if (mass_earth < 130.0f) {
        radius_earth = 1.5f * std::pow(mass_earth, 0.30f);
    } else {
        radius_earth = 11.0f * std::pow(mass_earth / 318.0f, 0.04f);
    }
    radius_earth = std::clamp(radius_earth, 0.2f, 13.0f);
    return radius_earth * EARTH_RADIUS_SIM_UNITS;
}

float solar_radius_sim_units() {
    // 1 R_sun ~= 109.1 R_earth
    return EARTH_RADIUS_SIM_UNITS * 109.1f;
}

float expected_main_sequence_radius_solar(float mass) {
    float m = std::max(mass, 0.003f);
    if (m < 0.08f) {
        // Brown dwarfs stay near Jupiter-size (roughly flat radius).
        return 0.095f + 0.020f * std::exp(-std::pow((m - 0.04f) / 0.03f, 2.0f));
    }
    if (m < 1.0f)
        return std::pow(m, 0.80f);
    if (m < 1.7f)
        return std::pow(m, 0.57f);
    if (m < 20.0f)
        return 1.25f * std::pow(m, 0.50f);
    return 1.85f * std::pow(m, 0.44f);
}

float expected_stellar_luminosity(float mass, float temperature, float radius,
                                         uint32_t stage, float fuel) {
    float r_solar = std::max(radius / std::max(solar_radius_sim_units(), 1.0f), 1.0e-4f);
    float t_term = std::pow(std::max(temperature, 100.0f) / 5778.0f, 4.0f);
    float stefan = r_solar * r_solar * t_term;
    float mass_law;
    if (mass < 0.43f) mass_law = 0.23f * std::pow(std::max(mass, 0.01f), 2.3f);
    else if (mass < 2.0f) mass_law = std::pow(mass, 4.0f);
    else if (mass < 20.0f) mass_law = 1.5f * std::pow(mass, 3.5f);
    else mass_law = 3200.0f * std::pow(mass / 20.0f, 2.2f);

    float stage_boost = 1.0f;
    if (stage == SSTAGE_SUBGIANT) stage_boost = 1.6f;
    else if (stage == SSTAGE_RED_GIANT || stage == SSTAGE_AGB) stage_boost = 60.0f;
    else if (stage == SSTAGE_SUPERGIANT) stage_boost = 350.0f;
    else if (stage == SSTAGE_HYPERGIANT) stage_boost = 900.0f;
    else if (stage == SSTAGE_WHITE_DWARF) stage_boost = 0.004f;
    else if (stage == SSTAGE_NEUTRON_STAR) stage_boost = 0.02f;

    float fuel_term = (stage == SSTAGE_WHITE_DWARF || stage == SSTAGE_NEUTRON_STAR)
        ? 1.0f
        : std::clamp(0.35f + std::max(fuel, 0.0f) * 0.90f, 0.25f, 1.35f);
    return std::max(stefan, mass_law) * stage_boost * fuel_term;
}

float expected_star_radius(const CelestialBody& b) {
    float mass = std::clamp(b.mass, 0.003f, MAX_MAIN_SEQUENCE_MASS_SOLAR);
    float ms_r_solar = expected_main_sequence_radius_solar(mass);
    float r_solar = ms_r_solar;

    if (b.stellar_stage == SSTAGE_WHITE_DWARF) {
        float inv = std::pow(std::max(mass, 0.2f), -0.35f);
        r_solar = std::clamp(0.010f * inv, 0.006f, 0.022f);
    } else if (b.stellar_stage == SSTAGE_NEUTRON_STAR) {
        r_solar = std::clamp(12.0f / 696340.0f, 8.0e-6f, 2.4e-5f);
    } else if (b.stellar_stage == SSTAGE_SUBGIANT) {
        r_solar = std::clamp(ms_r_solar * (1.8f + std::pow(std::clamp(mass, 0.6f, 5.0f), 0.25f)),
                             1.7f, 8.0f);
    } else if (b.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_AGB) {
        float giant = 20.0f + 85.0f * std::pow(std::clamp((mass - 0.8f) / 7.2f, 0.0f, 1.0f), 0.65f);
        r_solar = std::clamp(giant, 12.0f, 180.0f);
    } else if (b.stellar_stage == SSTAGE_SUPERGIANT) {
        float sg = 80.0f + 320.0f * std::pow(std::clamp((mass - 8.0f) / 42.0f, 0.0f, 1.0f), 0.60f);
        r_solar = std::clamp(sg, 70.0f, 420.0f);
    } else if (b.stellar_stage == SSTAGE_HYPERGIANT) {
        float hg = 180.0f + 620.0f * std::pow(std::clamp((mass - 20.0f) / 80.0f, 0.0f, 1.0f), 0.55f);
        r_solar = std::clamp(hg, 160.0f, 900.0f);
    }
    return std::max(r_solar * solar_radius_sim_units(), 0.006f);
}

float stellar_luminosity_units(const CelestialBody& b);

MaterialComposition derive_materials(const CelestialBody& b) {
    MaterialComposition m{};
    if (b.custom_material) {
        m.iron = std::max(b.custom_iron, 0.0f);
        m.silicate = std::max(b.custom_silicate, 0.0f);
        m.water = std::max(b.custom_water, 0.0f);
        m.hydrogen = std::max(b.custom_hydrogen, 0.0f);
        float custom_total = m.iron + m.silicate + m.water + m.hydrogen;
        if (custom_total > 1.0e-6f) {
            m.iron /= custom_total;
            m.silicate /= custom_total;
            m.water /= custom_total;
            m.hydrogen /= custom_total;
        } else {
            m.silicate = 1.0f;
        }
        return m;
    }
    if (is_star_type(b.type)) {
        m.hydrogen = 1.0f;
        return m;
    }
    if (is_black_hole_type(b.type)) {
        m.iron = 0.5f;
        m.silicate = 0.5f;
        return m;
    }
    if (b.type == CTYPE_NEBULA) {
        m.hydrogen = 0.88f;
        m.water = 0.05f;
        m.silicate = 0.05f;
        m.iron = 0.02f;
        return m;
    }
    if (b.type == CTYPE_ASTEROID) {
        m.iron = std::clamp(b.cached_visuals.metal_frac, 0.0f, 1.0f);
        m.silicate = std::clamp(1.0f - m.iron - b.cached_visuals.ice_frac, 0.0f, 1.0f);
        m.water = std::clamp(b.cached_visuals.ice_frac, 0.0f, 1.0f);
    } else if (b.type == CTYPE_DUST) {
        float cold = std::clamp((220.0f - b.temperature) / 220.0f, 0.0f, 1.0f);
        m.water = 0.22f + 0.28f * cold;
        m.iron = 0.18f;
        m.silicate = std::clamp(1.0f - m.water - m.iron, 0.2f, 0.7f);
    } else if (b.type == CTYPE_COMET) {
        m.water = 0.65f;
        m.silicate = 0.25f;
        m.iron = 0.10f;
    } else if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
        const PlanetProperties& pp = b.cached_props;
        if (pp.surface == SURF_GAS) {
            m.hydrogen = std::clamp(pp.atmosphere.h2_frac + pp.atmosphere.he_frac * 0.5f + 0.55f, 0.55f, 0.95f);
            m.water = std::clamp(pp.ocean_coverage / 100.0f * 0.2f + b.cached_visuals.ice_frac * 0.2f, 0.0f, 0.25f);
            m.silicate = std::clamp(1.0f - m.hydrogen - m.water - 0.03f, 0.0f, 0.30f);
            m.iron = std::clamp(1.0f - m.hydrogen - m.water - m.silicate, 0.0f, 0.12f);
        } else {
            float core_bonus = pp.has_iron_core ? 0.12f : 0.0f;
            m.iron = std::clamp(0.15f + core_bonus + b.cached_visuals.metal_frac * 0.55f, 0.05f, 0.60f);
            m.water = std::clamp((pp.ocean_coverage / 100.0f) * 0.7f + b.cached_visuals.ice_frac * 0.35f, 0.0f, 0.80f);
            m.hydrogen = std::clamp(pp.atmosphere.h2_frac * 0.4f, 0.0f, 0.25f);
            m.silicate = std::clamp(1.0f - m.iron - m.water - m.hydrogen, 0.0f, 0.90f);
        }
    } else {
        m.iron = 0.2f;
        m.silicate = 0.8f;
    }

    float total = m.iron + m.silicate + m.water + m.hydrogen;
    if (total > 1.0e-6f) {
        m.iron /= total;
        m.silicate /= total;
        m.water /= total;
        m.hydrogen /= total;
    }
    return m;
}

bool gas_dominated_body(const CelestialBody& b, const MaterialComposition& m) {
    if (b.type == CTYPE_NEBULA) return true;
    if (is_star_type(b.type) || is_black_hole_type(b.type)) return false;
    if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON)
        return b.cached_props.surface == SURF_GAS || b.cached_props.planet_class == PCLASS_GAS_GIANT ||
               b.cached_props.planet_class == PCLASS_ICE_GIANT || m.hydrogen > 0.35f;
    return m.hydrogen > 0.40f;
}

bool body_is_cloud_feedstock(const CelestialBody& b, const MaterialComposition& m) {
    if (is_star_type(b.type) || is_black_hole_type(b.type))
        return false;
    if (b.type == CTYPE_NEBULA || b.type == CTYPE_DUST || b.type == CTYPE_COMET)
        return true;
    if (b.material_phase == PHASE_GAS || b.material_phase == PHASE_COLLAPSING)
        return true;
    if ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) &&
        b.props_valid &&
        (b.cached_props.surface == SURF_GAS || b.cached_props.planet_class == PCLASS_GAS_GIANT ||
         b.cached_props.planet_class == PCLASS_ICE_GIANT))
        return true;
    return m.hydrogen > 0.22f;
}

float cloud_capture_radius(const CelestialBody& cloud) {
    float grav_span = std::cbrt(std::max(cloud.mass, 1.0e-10f)) * 320.0f;
    float pressure_span = std::max(cloud.radius * 2.6f, 20.0f);
    return std::max(pressure_span, cloud.radius * 1.5f + grav_span);
}

float stellar_capture_radius(const CelestialBody& star) {
    float mass_span = std::sqrt(std::max(star.mass, 0.01f));
    float base = star.radius * (1.45f + std::min(mass_span * 0.9f, 6.0f));
    return std::max(base, star.radius * 1.7f);
}

MaterialPhase infer_material_phase(const CelestialBody& b, const MaterialComposition& m) {
    if (is_star_type(b.type))
        return PHASE_PLASMA;
    if (is_black_hole_type(b.type))
        return PHASE_PLASMA;
    if (b.type == CTYPE_NEBULA)
        return (b.collapse_progress > 0.08f || b.mass >= HYDROGEN_BURNING_MASS_SOLAR * 0.65f)
            ? PHASE_COLLAPSING : PHASE_GAS;

    bool gas_dominated = gas_dominated_body(b, m);
    if (gas_dominated && b.mass >= HYDROGEN_BURNING_MASS_SOLAR * 0.70f &&
        (b.temperature > 700.0f || b.internal_energy > 2.0f || b.collapse_progress > 0.05f))
        return PHASE_COLLAPSING;
    if (b.temperature > 2400.0f)
        return PHASE_PLASMA;
    if (b.temperature > 900.0f)
        return PHASE_MOLTEN;
    if (gas_dominated)
        return PHASE_GAS;
    if (m.water > 0.10f && b.temperature < 220.0f)
        return PHASE_ICE;
    if (m.water > 0.08f &&
        ((b.temperature >= 235.0f && b.temperature <= 390.0f) ||
         (b.temperature >= 95.0f && b.temperature <= 145.0f && b.cached_props.ocean_type == OCEAN_METHANE) ||
         (b.temperature >= 150.0f && b.temperature <= 235.0f && b.cached_props.ocean_type == OCEAN_AMMONIA) ||
         (b.temperature >= 650.0f && b.cached_props.ocean_type == OCEAN_LAVA)))
        return PHASE_LIQUID;
    if (b.temperature < 170.0f)
        return PHASE_ICE;
    return PHASE_SOLID;
}

float phase_intensity_for_body(const CelestialBody& b, MaterialPhase phase,
                                      const MaterialComposition& m) {
    switch (phase) {
    case PHASE_LIQUID:
        return std::clamp(m.water * 1.4f + (b.type == CTYPE_PLANET || b.type == CTYPE_MOON
            ? b.cached_props.ocean_coverage / 100.0f * 0.4f : 0.0f), 0.15f, 1.0f);
    case PHASE_ICE:
        return std::clamp((220.0f - b.temperature) / 180.0f + m.water * 0.7f, 0.15f, 1.0f);
    case PHASE_GAS:
        return std::clamp(0.35f + m.hydrogen * 0.65f, 0.25f, 1.0f);
    case PHASE_MOLTEN:
        return std::clamp((b.temperature - 850.0f) / 1400.0f, 0.12f, 1.0f);
    case PHASE_PLASMA:
        return std::clamp((b.temperature - 1800.0f) / 14000.0f + 0.45f, 0.35f, 1.0f);
    case PHASE_COLLAPSING:
        return std::clamp(std::max(b.collapse_progress, b.mass / HYDROGEN_BURNING_MASS_SOLAR * 0.18f),
                          0.12f, 1.0f);
    case PHASE_SOLID:
    default:
        return std::clamp(0.20f + m.silicate * 0.35f + m.iron * 0.25f, 0.10f, 0.85f);
    }
}

void add_ring_material(CelestialBody& primary, const CelestialBody& source,
                              float deposited_mass, float orbital_radius_hint,
                              const CosmosConfig& cfg) {
    if (!body_can_host_rings(primary) || deposited_mass <= 0.0f)
        return;

    MaterialComposition mats = derive_materials(source);
    float ice_fraction = std::clamp(mats.water * 0.85f + mats.hydrogen * 0.25f, 0.0f, 1.0f);
    float mass_ratio = deposited_mass / std::max(primary.mass + deposited_mass, 1.0e-6f);
    float density_boost = std::clamp(0.08f + std::sqrt(std::max(mass_ratio, 0.0f)) * 0.85f, 0.05f, 0.80f);
    float inner = std::max(primary.radius * 1.22f, orbital_radius_hint * 0.65f);
    float outer = std::max(inner + primary.radius * 0.40f,
                           orbital_radius_hint * (1.05f + mass_ratio * 1.8f));
    float tilt = std::max(primary.ring_tilt, 0.04f + hash_float(hash_combine(primary.seed, source.seed)) * 0.35f);

    if (primary.ring_density > 0.01f) {
        inner = std::min(primary.ring_inner_radius, inner);
        outer = std::max(primary.ring_outer_radius, outer);
        density_boost = std::clamp(primary.ring_density + density_boost * 0.55f, 0.0f, 0.95f);
        ice_fraction = glm::mix(primary.ring_ice_fraction, ice_fraction, 0.35f);
    }

    set_ring_system(primary,
                    inner * std::clamp(cfg.ring_inner_scale, 0.6f, 3.0f),
                    outer * std::clamp(cfg.ring_outer_scale, 0.6f, 3.0f),
                    density_boost * std::clamp(cfg.ring_density_scale, 0.2f, 3.0f),
                    ice_fraction, tilt);
}

ComparisonMetrics derive_comparisons(const CelestialBody& b) {
    ComparisonMetrics cm{};
    if (b.type != CTYPE_PLANET && b.type != CTYPE_MOON)
        return cm;

    float mass_earth = std::max(b.mass / EARTH_MASS_SOLAR, 1.0e-5f);
    float radius_earth = std::max(b.radius / EARTH_RADIUS_SIM_UNITS, 1.0e-5f);
    float temp_score = std::exp(-std::pow((b.temperature - 288.0f) / 95.0f, 2.0f));
    float mass_score = std::exp(-std::pow(std::log10(mass_earth), 2.0f) / 0.28f);
    float radius_score = std::exp(-std::pow(std::log10(radius_earth), 2.0f) / 0.25f);
    float atm_score = (b.cached_props.atmosphere.pressure > 0.02f)
        ? std::exp(-std::pow(std::log10(std::max(b.cached_props.atmosphere.pressure, 0.01f)), 2.0f) / 0.9f) : 0.2f;
    float water_score = std::clamp(b.cached_props.ocean_coverage / 60.0f, 0.0f, 1.0f);
    cm.earth_similarity = std::clamp(temp_score * 0.35f + mass_score * 0.25f +
                                     radius_score * 0.2f + atm_score * 0.1f + water_score * 0.1f,
                                     0.0f, 1.0f);
    float oxygen_bonus = std::clamp(b.cached_props.atmosphere.o2_frac * 2.5f, 0.0f, 0.35f);
    float vegetation_bonus = std::clamp(b.cached_props.vegetation_coverage / 100.0f, 0.0f, 0.35f);
    cm.life_likelihood = std::clamp(cm.earth_similarity * 0.6f + water_score * 0.2f +
                                    oxygen_bonus + vegetation_bonus, 0.0f, 1.0f);
    return cm;
}

MagneticMetrics derive_magnetic_metrics(const CelestialBody& b, float G) {
    MagneticMetrics mm{};
    MagneticSignature sig = estimate_magnetic_signature(b, G);
    mm.show_magnetosphere = sig.has_magnetosphere;
    mm.magnetosphere_size = sig.magnetosphere_size;
    mm.magnetic_field = sig.magnetic_field;
    mm.show_magnetic_axis = sig.show_axis;
    mm.magnetic_pole_angle = glm::degrees(sig.axis_angle);
    mm.particle_jets = sig.particle_jets;
    mm.make_pulsar = sig.pulsar;
    return mm;
}

float magnetic_shielding_score(const CelestialBody& b, float G) {
    MagneticSignature sig = estimate_magnetic_signature(b, G);
    if (!sig.has_magnetosphere) return 0.0f;
    float escape = body_escape_velocity(b, G);
    float atmosphere = (b.type == CTYPE_PLANET || b.type == CTYPE_MOON)
        ? b.cached_props.atmosphere.pressure : 0.0f;
    return std::clamp(sig.magnetic_field * 0.10f + sig.particle_trapping * 0.32f +
                      escape * 0.16f + atmosphere * 0.008f, 0.0f, 1.25f);
}

void enforce_body_physical_limits(CelestialBody& b) {
    b.mass = std::max(b.mass, 1.0e-12f);
    float min_radius = (b.type == CTYPE_DUST) ? 0.06f : 0.1f;
    b.radius = std::max(b.radius, min_radius);

    if (is_black_hole_type(b.type)) {
        b.radius = std::max(0.5f, 2.0f * b.mass);
        return;
    }

    if (is_star_type(b.type)) {
        b.mass = std::clamp(b.mass, 0.003f, MAX_MAIN_SEQUENCE_MASS_SOLAR);
        float target_r = expected_star_radius(b);
        float low = (b.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_NEUTRON_STAR)
            ? 0.85f : 0.70f;
        float high = (b.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_NEUTRON_STAR)
            ? 1.20f : 1.35f;
        b.radius = std::clamp(b.radius, target_r * low, target_r * high);
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE)
            b.temperature = std::clamp(b.temperature, 250.0f, 60000.0f);
        else if (b.stellar_stage == SSTAGE_WHITE_DWARF)
            b.temperature = std::clamp(b.temperature, 7000.0f, 140000.0f);
        else if (b.stellar_stage == SSTAGE_NEUTRON_STAR)
            b.temperature = std::clamp(b.temperature, 30000.0f, 250000.0f);
        return;
    }

    if (b.type == CTYPE_NEBULA) {
        float target_r = std::max(28.0f, std::cbrt(std::max(b.mass, 1.0e-6f)) * 110.0f);
        float collapsed_r = std::max(expected_planet_radius(std::min(b.mass, 0.02f)) * 2.2f, 18.0f);
        float mix_t = std::clamp(b.collapse_progress, 0.0f, 1.0f);
        float desired = std::max(glm::mix(target_r, collapsed_r, mix_t), b.radius * 0.35f);
        b.radius = std::clamp(b.radius, desired * 0.65f, desired * 1.35f);
        return;
    }

    if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
        float target_r = expected_planet_radius(std::min(b.mass, 0.02f));
        b.radius = std::clamp(b.radius, target_r * 0.65f, target_r * 1.20f);
        if (b.type == CTYPE_MOON)
            b.radius = std::min(b.radius, expected_planet_radius(std::max(b.mass, 1.0e-6f)) * 0.7f);
    } else if (b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST) {
        MaterialComposition mat = derive_materials(b);
        float density_factor = mat.iron * 5.0f + mat.silicate * 3.0f + mat.water * 1.4f + mat.hydrogen * 0.2f;
        density_factor = std::max(density_factor, 0.5f);
        float target_r = std::cbrt(b.mass / density_factor) * (b.type == CTYPE_DUST ? 10.5f : 18.0f);
        if (b.type == CTYPE_DUST) {
            b.radius = std::clamp(b.radius, std::max(target_r * 0.65f, 0.06f), std::max(target_r * 1.5f, 0.45f));
        } else {
            b.radius = std::clamp(b.radius, std::max(target_r * 0.7f, 0.3f), std::max(target_r * 1.3f, 0.6f));
        }
    }

    if (b.ring_density > 0.0f) {
        b.ring_inner_radius = std::max(b.ring_inner_radius, b.radius * 1.15f);
        b.ring_outer_radius = std::max(b.ring_outer_radius, b.ring_inner_radius + b.radius * 0.20f);
    }
}

float stellar_luminosity_units(const CelestialBody& b) {
    if (!is_star_type(b.type)) return 0.0f;
    float physical = expected_stellar_luminosity(std::max(b.mass, 0.003f), b.temperature, b.radius,
                                                 b.stellar_stage, b.fuel);
    return std::max(b.luminosity, physical);
}

float equilibrium_temperature_from_star(const CelestialBody& body,
                                        const CelestialBody& star) {
    float dist = glm::length(star.pos - body.pos);
    if (dist <= 1.0e-3f) return body.temperature;

    float albedo = 0.35f;
    float ratio = std::sqrt(std::max(star.radius, 1.0f) / (2.0f * std::max(dist, 1.0f)));
    float lum_boost = std::clamp(1.0f + 0.08f * std::log10(std::max(stellar_luminosity_units(star), 1.0f)), 1.0f, 1.6f);
    return std::max(star.temperature, 50.0f) * ratio *
           std::pow(std::max(1.0f - albedo, 0.05f), 0.25f) * lum_boost;
}
