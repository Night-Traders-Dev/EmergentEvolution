#include "cosmos/cosmos_app_internal.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <thread>

// ── Star / BH classification helpers ────────────────────────────────────────

uint32_t classify_star_spectral(float temperature, float mass) {
    if (temperature > 40000.0f && mass > 16.0f)  return CTYPE_STAR_WR;
    if (temperature > 30000.0f && mass > 16.0f)  return CTYPE_STAR_O;
    if (temperature > 10000.0f && mass > 2.1f)   return CTYPE_STAR_B;
    if (temperature > 7500.0f  && mass > 1.4f)   return CTYPE_STAR_A;
    if (temperature > 6000.0f  && mass > 1.04f)  return CTYPE_STAR_F;
    if (temperature > 5200.0f  && mass > 0.8f)   return CTYPE_STAR_G;
    if (temperature > 3700.0f  && mass > 0.45f)  return CTYPE_STAR_K;
    if (temperature > 2400.0f  && mass > 0.08f)  return CTYPE_STAR_M;
    if (temperature > 1300.0f)                    return CTYPE_STAR_L;
    if (temperature > 500.0f)                     return CTYPE_STAR_T;
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

static bool body_can_host_rings(const CelestialBody& body) {
    if (is_star_type(body.type) || is_black_hole_type(body.type))
        return false;
    return body.type == CTYPE_PLANET || body.type == CTYPE_MOON || body.type == CTYPE_NEBULA;
}

static bool fragment_like_body(const CelestialBody& body) {
    return (int)body.frag_generation > 0 &&
        !is_star_type(body.type) && !is_black_hole_type(body.type);
}

static bool roche_secondary_fluid_like(const CelestialBody& body) {
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

static float roche_distance_for_mode(const CelestialBody& primary,
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

static void set_ring_system(CelestialBody& body, float inner_radius, float outer_radius,
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

static void star_spectral_bounds(uint32_t type, float& min_mass, float& max_mass,
                                 float& min_temp, float& max_temp) {
    switch (type) {
    case CTYPE_STAR_O:  min_mass = 16.0f; max_mass = 80.0f;  min_temp = 30000.0f; max_temp = 52000.0f; break;
    case CTYPE_STAR_B:  min_mass = 2.1f;  max_mass = 16.0f;  min_temp = 10000.0f; max_temp = 30000.0f; break;
    case CTYPE_STAR_A:  min_mass = 1.4f;  max_mass = 2.1f;   min_temp = 7500.0f;  max_temp = 10000.0f; break;
    case CTYPE_STAR_F:  min_mass = 1.04f; max_mass = 1.4f;   min_temp = 6000.0f;  max_temp = 7500.0f; break;
    case CTYPE_STAR_G:  min_mass = 0.8f;  max_mass = 1.04f;  min_temp = 5200.0f;  max_temp = 6000.0f; break;
    case CTYPE_STAR_K:  min_mass = 0.45f; max_mass = 0.8f;   min_temp = 3700.0f;  max_temp = 5200.0f; break;
    case CTYPE_STAR_M:  min_mass = 0.08f; max_mass = 0.45f;  min_temp = 2400.0f;  max_temp = 3700.0f; break;
    case CTYPE_STAR_L:  min_mass = 0.04f; max_mass = 0.08f;  min_temp = 1300.0f;  max_temp = 2400.0f; break;
    case CTYPE_STAR_T:  min_mass = 0.02f; max_mass = 0.06f;  min_temp = 500.0f;   max_temp = 1300.0f; break;
    case CTYPE_STAR_Y:  min_mass = 0.01f; max_mass = 0.04f;  min_temp = 250.0f;   max_temp = 500.0f; break;
    case CTYPE_STAR_WR: min_mass = 20.0f; max_mass = 120.0f; min_temp = 35000.0f; max_temp = 60000.0f; break;
    default:            min_mass = 0.08f; max_mass = 60.0f;  min_temp = 2400.0f;  max_temp = 40000.0f; break;
    }
}

static float expected_main_sequence_temperature(float mass) {
    if (mass < 0.08f) return 1800.0f;
    if (mass < 0.45f) return 2500.0f + (mass - 0.08f) / 0.37f * 1200.0f;
    if (mass < 0.8f)  return 3700.0f + (mass - 0.45f) / 0.35f * 1500.0f;
    if (mass < 1.04f) return 5200.0f + (mass - 0.8f) / 0.24f * 600.0f;
    if (mass < 1.4f)  return 6000.0f + (mass - 1.04f) / 0.36f * 1500.0f;
    if (mass < 2.1f)  return 7500.0f + (mass - 1.4f) / 0.7f * 2500.0f;
    if (mass < 16.0f) return 10000.0f + (mass - 2.1f) / 13.9f * 20000.0f;
    return 30000.0f + std::clamp((mass - 16.0f) / 64.0f, 0.0f, 1.0f) * 22000.0f;
}

static float stellar_rotation_period_hours(float mass, uint32_t type, std::mt19937& rng) {
    if (type == CTYPE_STAR_WR || mass > 16.0f)
        return std::uniform_real_distribution<float>(8.0f, 60.0f)(rng);
    if (mass > 2.0f)
        return std::uniform_real_distribution<float>(12.0f, 180.0f)(rng);
    if (mass > 0.8f)
        return std::uniform_real_distribution<float>(120.0f, 720.0f)(rng);
    return std::uniform_real_distribution<float>(240.0f, 1800.0f)(rng);
}

float expected_star_radius(const CelestialBody& b);

static void randomize_small_body_properties(CelestialBody& body, std::mt19937& rng,
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

static void randomize_nebula_properties(CelestialBody& body, std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    body.type = CTYPE_NEBULA;
    body.temperature = std::uniform_real_distribution<float>(18.0f, 140.0f)(rng);
    body.radius = std::max(35.0f, std::cbrt(std::max(body.mass, 1.0e-5f)) * 120.0f);
    body.angular_vel = (u01(rng) * 2.0f - 1.0f) * 0.0008f;
    body.atmosphere_retention = 1.0f;
    body.material_phase = PHASE_GAS;
    body.phase_intensity = 0.65f + u01(rng) * 0.30f;
    body.collapse_progress = std::clamp(body.mass / HYDROGEN_BURNING_MASS_SOLAR, 0.0f, 0.55f) * 0.35f;
    clear_ring_system(body);
    clear_impact_signature(body);
}

static void randomize_star_properties(CelestialBody& body, std::mt19937& rng,
                                      uint32_t requested_type = CTYPE_STAR) {
    constexpr float PI = 3.14159265359f;
    float min_mass, max_mass, min_temp, max_temp;
    star_spectral_bounds(requested_type, min_mass, max_mass, min_temp, max_temp);

    if (requested_type != CTYPE_STAR)
        body.mass = std::max(body.mass, min_mass);
    body.mass = std::clamp(body.mass, 0.01f, MAX_MAIN_SEQUENCE_MASS_SOLAR);

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
    body.fuel = std::clamp(std::uniform_real_distribution<float>(0.55f, 1.0f)(rng) -
                           std::clamp((body.mass - 8.0f) / 80.0f, 0.0f, 0.18f),
                           0.25f, 1.0f);
    body.radius = expected_star_radius(body) *
                  std::uniform_real_distribution<float>(0.82f, 1.22f)(rng);
    float period_h = stellar_rotation_period_hours(body.mass, body.type, rng);
    body.angular_vel = (2.0f * PI) / (period_h * 3600.0f);
    if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.08f)
        body.angular_vel *= -1.0f;
    body.luminosity = std::pow(std::max(body.mass, 0.08f), 3.2f) * 0.1f *
                      std::uniform_real_distribution<float>(0.75f, 1.35f)(rng);
}

static float merged_star_fuel(const CelestialBody& a, const CelestialBody& b, float total_mass) {
    float weighted = (a.fuel * a.mass + b.fuel * b.mass) / std::max(total_mass, 1.0e-6f);
    float mixing_bonus = std::clamp(0.10f + 0.20f * std::min(a.mass, b.mass) / std::max(total_mass, 1.0e-6f),
                                    0.0f, 0.22f);
    return std::clamp(weighted * 0.82f + mixing_bonus, 0.05f, 1.0f);
}

enum StellarRemnantKind {
    REMNANT_NONE = 0,
    REMNANT_WHITE_DWARF,
    REMNANT_NEUTRON_STAR,
    REMNANT_BLACK_HOLE,
};

static StellarRemnantKind stellar_remnant_kind(float progenitor_mass, bool thermonuclear = false) {
    if (thermonuclear) return REMNANT_NONE;
    if (progenitor_mass < CORE_COLLAPSE_MIN_MASS_SOLAR) return REMNANT_WHITE_DWARF;
    if (progenitor_mass < BLACK_HOLE_MIN_REMNANT_MASS_SOLAR) return REMNANT_NEUTRON_STAR;
    return REMNANT_BLACK_HOLE;
}


static void randomize_planet_properties(CelestialBody& body, const CosmosState& state,
                                        std::mt19937& rng) {
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
        set_ring_system(body, body.radius * inner_mult, body.radius * outer_mult,
                        density, ice_fraction, tilt);
    }

    body.phase_intensity = 0.0f;
    body.collapse_progress = 0.0f;
    clear_impact_signature(body);
}

static void randomize_moon_properties(CelestialBody& body, const CosmosState& state,
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

static void enforce_body_physical_limits(CelestialBody& b);

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

static float body_gravitational_binding_energy(const CelestialBody& b, float G) {
    return 0.6f * G * b.mass * b.mass / std::max(b.radius, 0.1f);
}

float body_surface_gravity(const CelestialBody& b, float G) {
    return G * b.mass / std::max(b.radius * b.radius, 1.0e-6f);
}

float body_escape_velocity(const CelestialBody& b, float G) {
    return std::sqrt(std::max(2.0f * G * b.mass / std::max(b.radius, 1.0e-6f), 0.0f));
}

static float body_escape_speed(const CelestialBody& a, const CelestialBody& b, float G) {
    float sep = std::max(a.radius + b.radius, 1.0f);
    return std::sqrt(std::max(2.0f * G * (a.mass + b.mass) / sep, 0.0f));
}

static void register_mass_loss(CelestialBody& b, float amount, float dt) {
    if (amount <= 0.0f || dt <= 0.0f) return;
    b.mass_loss_rate += amount / dt;
    b.mass_loss_total += amount;
}

static void apply_impact_signature(CelestialBody& target, glm::vec3 impact_normal,
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

float expected_star_radius(const CelestialBody& b) {
    float mass = std::clamp(b.mass, 0.02f, MAX_MAIN_SEQUENCE_MASS_SOLAR);
    if (b.stellar_stage == SSTAGE_WHITE_DWARF)
        return std::clamp(1.6f + 1.8f / std::pow(std::max(mass, 0.2f), 0.33f), 1.5f, 4.0f);
    if (b.stellar_stage == SSTAGE_NEUTRON_STAR)
        return 2.5f;
    if (b.stellar_stage == SSTAGE_HYPERGIANT)
        return std::clamp(70.0f * std::pow(mass, 0.58f), 60.0f, 320.0f);
    if (b.stellar_stage == SSTAGE_SUPERGIANT)
        return std::clamp(48.0f * std::pow(mass, 0.54f), 40.0f, 260.0f);
    if (b.stellar_stage == SSTAGE_RED_GIANT)
        return std::clamp(35.0f * std::pow(mass, 0.6f), 30.0f, 220.0f);
    return std::clamp(10.0f + 20.0f * std::pow(mass, 0.8f), 6.0f, 120.0f);
}

static float stellar_luminosity_units(const CelestialBody& b);

MaterialComposition derive_materials(const CelestialBody& b) {
    MaterialComposition m{};
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

static bool gas_dominated_body(const CelestialBody& b, const MaterialComposition& m) {
    if (b.type == CTYPE_NEBULA) return true;
    if (is_star_type(b.type) || is_black_hole_type(b.type)) return false;
    if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON)
        return b.cached_props.surface == SURF_GAS || b.cached_props.planet_class == PCLASS_GAS_GIANT ||
               b.cached_props.planet_class == PCLASS_ICE_GIANT || m.hydrogen > 0.35f;
    return m.hydrogen > 0.40f;
}

static MaterialPhase infer_material_phase(const CelestialBody& b, const MaterialComposition& m) {
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

static float phase_intensity_for_body(const CelestialBody& b, MaterialPhase phase,
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

static void add_ring_material(CelestialBody& primary, const CelestialBody& source,
                              float deposited_mass, float orbital_radius_hint) {
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

    set_ring_system(primary, inner, outer, density_boost, ice_fraction, tilt);
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

static float magnetic_shielding_score(const CelestialBody& b, float G) {
    MagneticSignature sig = estimate_magnetic_signature(b, G);
    if (!sig.has_magnetosphere) return 0.0f;
    float escape = body_escape_velocity(b, G);
    float atmosphere = (b.type == CTYPE_PLANET || b.type == CTYPE_MOON)
        ? b.cached_props.atmosphere.pressure : 0.0f;
    return std::clamp(sig.magnetic_field * 0.10f + sig.particle_trapping * 0.32f +
                      escape * 0.16f + atmosphere * 0.008f, 0.0f, 1.25f);
}

static void enforce_body_physical_limits(CelestialBody& b) {
    b.mass = std::max(b.mass, 1.0e-12f);
    b.radius = std::max(b.radius, 0.1f);

    if (is_black_hole_type(b.type)) {
        b.radius = std::max(0.5f, 2.0f * b.mass);
        return;
    }

    if (is_star_type(b.type)) {
        b.mass = std::clamp(b.mass, 0.01f, MAX_MAIN_SEQUENCE_MASS_SOLAR);
        float target_r = expected_star_radius(b);
        b.radius = std::clamp(b.radius, target_r * 0.75f, target_r * 1.25f);
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE)
            b.temperature = std::clamp(b.temperature, 1800.0f, 55000.0f);
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
    } else if (b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET) {
        MaterialComposition mat = derive_materials(b);
        float density_factor = mat.iron * 5.0f + mat.silicate * 3.0f + mat.water * 1.4f + mat.hydrogen * 0.2f;
        density_factor = std::max(density_factor, 0.5f);
        float target_r = std::cbrt(b.mass / density_factor) * 18.0f;
        b.radius = std::clamp(b.radius, std::max(target_r * 0.7f, 0.3f), std::max(target_r * 1.3f, 0.6f));
    }

    if (b.ring_density > 0.0f) {
        b.ring_inner_radius = std::max(b.ring_inner_radius, b.radius * 1.15f);
        b.ring_outer_radius = std::max(b.ring_outer_radius, b.ring_inner_radius + b.radius * 0.20f);
    }
}

static float stellar_luminosity_units(const CelestialBody& b) {
    if (!is_star_type(b.type)) return 0.0f;
    float mass_lum = std::pow(std::max(b.mass, 0.05f), 3.5f);
    float thermal_lum = std::pow(std::max(b.temperature, 100.0f) / 5778.0f, 4.0f) *
                        std::max(b.radius * b.radius / (30.0f * 30.0f), 0.02f);
    return std::max(b.luminosity, 0.0f) + mass_lum * 0.1f + thermal_lum;
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

// ── Lifecycle ────────────────────────────────────────────────────────────────

static void seed_default_system(CosmosState& state, const CosmosConfig& cfg) {
    state.clear();

    // Sun at origin
    CelestialBody sun;
    sun.pos  = {0.0f, 0.0f, 0.0f};
    sun.vel  = {0.0f, 0.0f, 0.0f};
    sun.mass = 1.0f;
    sun.radius = 30.0f;
    sun.temperature = 5778.0f;
    sun.type = CTYPE_STAR;
    sun.seed = 42;
    sun.fuel = 0.72f;
    sun.angular_vel = (2.0f * 3.14159265359f) / (26.0f * 24.0f * 3600.0f);
    sun.luminosity = std::pow(std::max(sun.mass, 0.08f), 3.2f) * 0.1f;
    sun.type = classify_star_spectral(sun.temperature, sun.mass);
    sun.name = generate_body_name(sun.seed, sun.type);
    state.bodies.push_back(sun);

    // Planets in circular orbits in the XZ plane
    const float orbit_radii[] = {100.0f, 170.0f, 250.0f, 350.0f};
    // Solar-mass units: roughly Mercury, Earth, Mars, Jupiter class worlds.
    const float planet_mass[] = {1.66e-7f, 3.00e-6f, 3.22e-7f, 9.54e-4f};
    const float planet_temp[] = {700.0f, 300.0f, 200.0f, 120.0f};
    for (int i = 0; i < 4; i++) {
        CelestialBody p;
        float angle = static_cast<float>(i) * 1.57f;
        p.pos  = glm::vec3(std::cos(angle) * orbit_radii[i], 0.0f,
                            std::sin(angle) * orbit_radii[i]);
        float v = std::sqrt(cfg.G * sun.mass / orbit_radii[i]);
        p.vel  = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        p.mass = planet_mass[i];
        p.radius = 8.0f + planet_mass[i] * 3.0f;
        p.temperature = planet_temp[i];
        p.type = CTYPE_PLANET;
        p.parent = 0;
        p.seed = (uint32_t)(i * 7919 + 12345);
        p.name = generate_body_name(p.seed, p.type);
        state.bodies.push_back(p);
    }

    // A moon
    {
        CelestialBody moon;
        moon.pos = state.bodies[2].pos + glm::vec3(25.0f, 0.0f, 0.0f);
        float moon_v = std::sqrt(cfg.G * state.bodies[2].mass / 25.0f);
        moon.vel = state.bodies[2].vel + glm::vec3(0.0f, 0.0f, moon_v);
        moon.mass = 3.70e-8f;
        moon.radius = 4.0f;
        moon.temperature = 200.0f;
        moon.type = CTYPE_MOON;
        moon.parent = 2;
        moon.seed = 99999;
        moon.name = generate_body_name(moon.seed, moon.type);
        state.bodies.push_back(moon);
    }

    // Asteroids
    std::mt19937 rng(42);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 20; i++) {
        CelestialBody a;
        float r = randf(420.0f, 500.0f);
        float angle = randf(0.0f, 6.2832f);
        a.pos = glm::vec3(std::cos(angle) * r, randf(-10.0f, 10.0f),
                           std::sin(angle) * r);
        float v = std::sqrt(cfg.G * sun.mass / r) * randf(0.9f, 1.1f);
        a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        a.mass = randf(5.0e-11f, 3.0e-9f);
        a.radius = randf(0.4f, 1.6f);
        a.temperature = 100.0f;
        a.type = CTYPE_ASTEROID;
        a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a);
    }

    state.trails.resize(state.bodies.size());

    // Initialize cached planet properties for all bodies
    for (auto& b : state.bodies) refresh_body_render_state(b, &state);
}

void CosmosApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);
    raytracer_.init(vk, renderer.render_pass());

    state.clear();
    cfg.body_count = 0;

    camera.target = {0, 0, 0};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    camera.azimuth = 0.0f;
}

// ── Body picking (screen-space hit test) ─────────────────────────────────────

int CosmosApp::pick_body(float mx, float my, float W, float H) const {
    float aspect = W / H;
    glm::dmat4 vp = camera.proj_matrix_d(aspect) * camera.view_matrix_d();
    float fov_rad = glm::radians(camera.fov);

    int best = -1;
    float best_dist = 30.0f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        glm::dvec4 clip = vp * glm::dvec4(b.pos, 1.0);
        if (clip.w <= 0.0) continue;
        glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
        float sx = (float)((ndc.x * 0.5 + 0.5) * (double)W);
        float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)H);

        float dx = sx - mx;
        float dy = sy - my;
        float d = std::sqrt(dx * dx + dy * dy);
        float sr = (float)(((double)b.radius / clip.w) * ((double)H / (2.0 * std::tan((double)fov_rad * 0.5))));
        float pick_r = std::max(sr, 12.0f);
        if (d < pick_r && d < best_dist) {
            best_dist = d;
            best = (int)i;
        }
    }
    return best;
}

void CosmosApp::destroy() {
    vkDeviceWaitIdle(vk.device);
    raytracer_.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

void CosmosApp::reset_simulation() {
    seed_default_system(state, cfg);
    cfg.body_count = static_cast<uint32_t>(state.count());
    selected_body = -1;
    inspector_visible_ = false;
    sim_time_ = 0.0f;
    cfg.sim_time_accumulated = 0.0;
    camera = OrbitCamera{};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    paused = false;
}

void CosmosApp::spawn_at(glm::vec3 pos) {
    CelestialBody nb;
    nb.pos = pos;
    nb.vel = glm::vec3(0.0f);
    nb.mass = spawn_mass;
    nb.radius = std::max(3.0f, std::cbrt(spawn_mass) * 5.0f);
    nb.type = (uint32_t)spawn_type;
    uint32_t pos_seed = hash_combine(hash_combine(float_bits(pos.x), float_bits(pos.y)), float_bits(pos.z));
    nb.seed = hash_combine(hash_combine(pos_seed,
        (uint32_t)state.bodies.size() * 747796405u + 2891336453u),
        (uint32_t)(sim_time_ * 1000.0f) + 1181783497u);
    std::mt19937 rng(nb.seed ^ (uint32_t)(sim_time_ * 1000.0f));
    clear_ring_system(nb);
    nb.material_phase = PHASE_SOLID;
    nb.phase_intensity = 0.0f;
    nb.collapse_progress = 0.0f;
    clear_impact_signature(nb);

    if (is_star_type((uint32_t)spawn_type)) {
        randomize_star_properties(nb, rng, (uint32_t)spawn_type);
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else if (is_black_hole_type((uint32_t)spawn_type)) {
        nb.temperature = 0.0f;
        nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
        if (spawn_type == CTYPE_BLACK_HOLE)
            nb.type = classify_black_hole(nb.mass);
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else {
        nb.temperature = 300.0f;
        if (spawn_type == CTYPE_PLANET) {
            randomize_planet_properties(nb, state, rng);
        } else if (spawn_type == CTYPE_MOON) {
            randomize_moon_properties(nb, state, rng);
        } else if (spawn_type == CTYPE_ASTEROID) {
            randomize_small_body_properties(nb, rng, false);
        } else if (spawn_type == CTYPE_COMET) {
            randomize_small_body_properties(nb, rng, true);
        } else if (spawn_type == CTYPE_NEBULA) {
            randomize_nebula_properties(nb, rng);
        }
    }


    if (spawn_in_orbit_ && !state.bodies.empty()) {
        int nearest = -1;
        float nearest_dist = 1e9f;
        for (size_t i = 0; i < state.bodies.size(); i++) {
            float d = glm::length(state.bodies[i].pos - nb.pos);
            if (d > 0.1f && d < nearest_dist && state.bodies[i].mass > nb.mass) {
                nearest_dist = d;
                nearest = (int)i;
            }
        }
        if (nearest >= 0) {
            nb.parent = nearest;
            glm::vec3 diff = nb.pos - state.bodies[nearest].pos;
            float dist = glm::length(diff);
            if (dist > 0.1f) {
                float v = std::sqrt(cfg.G * state.bodies[nearest].mass / dist);
                glm::vec3 dir = glm::normalize(diff);
                glm::vec3 perp(-dir.z, 0.0f, dir.x);
                nb.vel = state.bodies[nearest].vel + perp * v;
            }
        }
    }

    nb.name = generate_body_name(nb.seed, nb.type);
    refresh_body_render_state(nb, &state);
    state.bodies.push_back(nb);
    state.trails.emplace_back();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    // WASD camera panning (breaks focus tracking)
    if (!show_splash && !show_pause_menu && !ImGui::GetIO().WantTextInput) {
        float move_speed = camera.distance * 0.5f * dt;
        glm::vec3 fwd = camera.forward_direction();
        glm::vec3 right = camera.right_direction();
        fwd.y = 0; fwd = glm::normalize(fwd);
        right.y = 0; right = glm::normalize(right);

        bool moved = false;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { camera.target += fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { camera.target -= fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { camera.target += right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { camera.target -= right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) { camera.target.y += move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) { camera.target.y -= move_speed; moved = true; }
        if (moved) camera.release_focus();
    }

    // Track focused body (update position each frame so camera follows)
    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        camera.track_body(state.bodies[camera.focus_body].pos);
    } else if (camera.focus_active && camera.focus_body >= 0) {
        // Body was removed
        camera.release_focus();
    }

    // Smooth camera animation (zoom + focus lerp)
    camera.update(dt);

    if (!paused && !show_splash) {
        step_physics(dt);
        sim_time_ += dt;
    }

    // GPU raytraced scene (draws within the active render pass)
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                io.DisplaySize.x, io.DisplaySize.y, sim_time_);

    // DrawList overlays (trails, selection, focus indicator)
    if (!show_splash && !show_pause_menu)
        render_overlay();

    // ImGui UI panels
    render_ui();

    renderer.end_frame(vk);
}

// ── Physics (CPU N-body, 3D) ────────────────────────────────────────────────

void CosmosApp::step_physics(float dt) {
    float frame_dt = std::max(dt, 1.0e-4f);
    float instant_fps = 1.0f / frame_dt;
    if (!std::isfinite(smoothed_fps_))
        smoothed_fps_ = instant_fps;
    smoothed_fps_ = smoothed_fps_ * 0.92f + instant_fps * 0.08f;

    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);
    float time_sign = reverse_time_ ? -1.0f : 1.0f;
    float scaled_dt = dt * cfg.dt_scale * time_sign;
    cfg.sim_time_accumulated += (double)scaled_dt;
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    if (n == 0) return;

    // Compute gravitational acceleration (Newtonian + GR corrections)
    std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
    float c2 = cfg.speed_of_light * cfg.speed_of_light;

    auto accumulate_gravity_pair = [&](size_t i, size_t j, std::vector<glm::vec3>& out_accel) {
            bool source_i = !bodies[i].non_attracting;
            bool source_j = !bodies[j].non_attracting;
            if (!source_i && !source_j) return;

            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist2 = glm::dot(diff, diff) + cfg.softening * cfg.softening;
            float dist  = std::sqrt(dist2);
            if (dist <= 1.0e-6f) return;
            glm::vec3 dir = diff / dist;

            auto apply_source_accel = [&](size_t target, size_t source, const glm::vec3& r_hat) {
                float GM = cfg.G * bodies[source].mass;
                glm::vec3 acc = r_hat * (GM / dist2);

                if (cfg.gr_enabled && c2 > 0.0f) {
                    if (cfg.gr_precession_scale > 0.0f) {
                        glm::vec3 v_t = bodies[target].vel;
                        float v2 = glm::dot(v_t, v_t);
                        float vr = glm::dot(v_t, r_hat);
                        float pn_scale = GM / (dist * c2) * cfg.gr_precession_scale;
                        acc += pn_scale * ((4.0f * GM / dist - v2) * r_hat + 4.0f * vr * v_t);
                    }

                    if (cfg.gr_time_dilation > 0.0f) {
                        float phi = -GM / dist;
                        float td = 1.0f + cfg.gr_time_dilation * phi / c2;
                        acc *= td;
                    }

                    if (cfg.gr_frame_dragging > 0.0f &&
                        std::abs(bodies[source].angular_vel) > 1e-6f) {
                        float J = bodies[source].mass * bodies[source].radius * bodies[source].radius *
                                  bodies[source].angular_vel;
                        glm::vec3 J_hat(0, 1, 0);
                        float coeff = cfg.gr_frame_dragging * cfg.G * J / (c2 * dist * dist * dist);
                        glm::vec3 v_t = bodies[target].vel;
                        glm::vec3 vxJ = glm::cross(v_t, J_hat);
                        float rdotJ = glm::dot(r_hat, J_hat);
                        glm::vec3 vxr = glm::cross(v_t, r_hat);
                        acc += coeff * (vxJ - 3.0f * rdotJ * vxr);
                    }
                }

                out_accel[target] += acc;
            };

            if (source_j) apply_source_accel(i, j, dir);
            if (source_i) apply_source_accel(j, i, -dir);
    };

    constexpr size_t kParallelGravityThreshold = 256;
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
                                ? static_cast<size_t>(std::thread::hardware_concurrency())
                                : 1;
    const bool use_parallel_gravity =
        cfg.parallel_gravity && n >= kParallelGravityThreshold && hw_threads > 1;

    if (use_parallel_gravity) {
        const size_t worker_count = std::min(hw_threads, n);
        std::vector<std::vector<glm::vec3>> local_accel(
            worker_count, std::vector<glm::vec3>(n, glm::vec3(0.0f)));
        std::vector<std::thread> workers;
        workers.reserve(worker_count);

        for (size_t t = 0; t < worker_count; ++t) {
            const size_t i_begin = (n * t) / worker_count;
            const size_t i_end = (n * (t + 1)) / worker_count;

            workers.emplace_back([&, t, i_begin, i_end]() {
                auto& thread_accel = local_accel[t];
                for (size_t i = i_begin; i < i_end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    for (size_t j = i + 1; j < n; ++j) {
                        if (bodies[j].marked_for_removal) continue;
                        accumulate_gravity_pair(i, j, thread_accel);
                    }
                }
            });
        }

        for (auto& worker : workers)
            worker.join();

        for (size_t t = 0; t < worker_count; ++t) {
            const auto& thread_accel = local_accel[t];
            for (size_t i = 0; i < n; ++i)
                accel[i] += thread_accel[i];
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (bodies[j].marked_for_removal) continue;
                accumulate_gravity_pair(i, j, accel);
            }
        }
    }

    // Integrate (symplectic Euler)
    for (size_t i = 0; i < n; i++) {
        if (bodies[i].marked_for_removal) continue;
        bodies[i].mass_loss_rate = 0.0f;
        bodies[i].vel += accel[i] * scaled_dt;
        bodies[i].vel *= cfg.damping;
        bodies[i].pos += bodies[i].vel * scaled_dt;
        bodies[i].age += scaled_dt;
    }

    // Physics subsystems
    if (cfg.roche_limit || cfg.tidal_forces) process_roche_limit(scaled_dt);
    if (cfg.collisions)         process_collisions(scaled_dt);
    if (cfg.temperature_system) process_temperature(scaled_dt);
    if (cfg.temperature_system || cfg.evaporation) process_space_weather(scaled_dt);
    if (cfg.material_phases)    process_material_phases(scaled_dt);
    if (cfg.evaporation)        process_evaporation(scaled_dt);
    if (cfg.stellar_evolution)  process_stellar_evolution(scaled_dt);
    cleanup_bodies();

    for (auto& b : state.bodies)
        enforce_body_physical_limits(b);

    // Refresh cached planet properties (only recomputes on temperature band changes)
    for (auto& b : state.bodies) {
        refresh_planet_props(b);
        refresh_body_visuals(b, &state);
    }

    update_body_tracking_cache();

    // Update trails
    n = bodies.size();
    while (state.trails.size() < n)
        state.trails.emplace_back();
    for (size_t i = 0; i < n; i++) {
        state.trails[i].push_back(bodies[i].pos);
        while (state.trails[i].size() > cfg.trail_length)
            state.trails[i].pop_front();
    }
}

int CosmosApp::dominant_primary_for(int body_index) const {
    if (body_index < 0 || body_index >= (int)state.bodies.size())
        return -1;
    const auto& body = state.bodies[(size_t)body_index];
    if (body.marked_for_removal) return -1;

    if (body.parent >= 0 && body.parent < (int)state.bodies.size() && body.parent != body_index) {
        const auto& parent = state.bodies[(size_t)body.parent];
        if (!parent.marked_for_removal && parent.mass > body.mass * 0.25f)
            return body.parent;
    }

    int best = -1;
    double best_score = 0.0;
    for (int j = 0; j < (int)state.bodies.size(); ++j) {
        if (j == body_index) continue;
        const auto& cand = state.bodies[(size_t)j];
        if (cand.marked_for_removal) continue;
        if (cand.non_attracting) continue;
        if (cand.mass <= body.mass * 1.01f && !is_star_type(cand.type) && !is_black_hole_type(cand.type))
            continue;
        glm::dvec3 d = glm::dvec3(cand.pos) - glm::dvec3(body.pos);
        double d2 = glm::dot(d, d);
        if (d2 <= 1.0e-8) continue;
        double score = (double)cand.mass / d2;
        if (score > best_score) {
            best_score = score;
            best = j;
        }
    }
    return best;
}

void CosmosApp::update_body_tracking_cache() {
    const size_t n = state.bodies.size();
    tracked_primary_.assign(n, -1);
    tracked_children_count_.assign(n, 0);
    tracked_eccentricity_.assign(n, -1.0f);

    for (size_t i = 0; i < n; ++i) {
        if (state.bodies[i].marked_for_removal) continue;
        int primary = dominant_primary_for((int)i);
        tracked_primary_[i] = primary;
        if (primary >= 0 && primary < (int)n)
            tracked_children_count_[(size_t)primary]++;
    }

    for (size_t i = 0; i < n; ++i) {
        int pidx = tracked_primary_[i];
        if (pidx < 0 || pidx >= (int)n) continue;
        const auto& b = state.bodies[i];
        const auto& p = state.bodies[(size_t)pidx];
        if (b.marked_for_removal || p.marked_for_removal) continue;

        glm::dvec3 r = glm::dvec3(b.pos) - glm::dvec3(p.pos);
        glm::dvec3 v = glm::dvec3(b.vel) - glm::dvec3(p.vel);
        double rmag = glm::length(r);
        if (rmag <= 1.0e-8) continue;
        double mu = (double)cfg.G * std::max((double)b.mass + (double)p.mass, 1.0e-8);
        if (mu <= 0.0) continue;
        glm::dvec3 h = glm::cross(r, v);
        glm::dvec3 evec = glm::cross(v, h) / mu - r / rmag;
        tracked_eccentricity_[i] = (float)glm::length(evec);
    }
}

glm::vec3 CosmosApp::verlet_auto_orbit_velocity(const CelestialBody& body, const CelestialBody& primary,
                                                float radial_scale, float tangential_scale) const {
    glm::vec3 rel = body.pos - primary.pos;
    float r0 = glm::length(rel);
    if (!std::isfinite(r0) || r0 < 1.0e-5f) return body.vel;
    glm::vec3 r_hat = rel / r0;

    glm::vec3 rel_v_now = body.vel - primary.vel;
    glm::vec3 tangent = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(rel_v_now, r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        return body.vel;
    tangent = glm::normalize(tangent);
    if (glm::dot(rel_v_now, tangent) < 0.0f) tangent *= -1.0f;

    float mu = cfg.G * std::max(primary.mass + body.mass, 1.0e-8f);
    float v_circ = std::sqrt(std::max(mu / std::max(r0, 1.0e-6f), 0.0f));
    float v_rad = glm::dot(rel_v_now, r_hat) * radial_scale;
    float vt_center = std::max(v_circ * tangential_scale, 1.0e-6f);

    float orbital_period = (2.0f * 3.14159265359f * r0) / std::max(v_circ, 1.0e-4f);
    int steps = std::clamp((int)std::round(orbital_period * 0.18f / 0.01f), 48, 180);
    float dt_sim = std::clamp(orbital_period * 0.18f / (float)steps, 0.001f, 0.025f);

    auto accel = [&](const glm::vec3& r) {
        float rmag = glm::length(r);
        if (rmag < 1.0e-6f) return glm::vec3(0.0f);
        return -r * (mu / (rmag * rmag * rmag));
    };

    auto score_candidate = [&](float v_tan) {
        glm::vec3 r = rel;
        glm::vec3 v = r_hat * v_rad + tangent * v_tan;
        float score = 0.0f;
        for (int s = 0; s < steps; ++s) {
            glm::vec3 a0 = accel(r);
            glm::vec3 r1 = r + v * dt_sim + 0.5f * a0 * dt_sim * dt_sim;
            glm::vec3 a1 = accel(r1);
            glm::vec3 v1 = v + 0.5f * (a0 + a1) * dt_sim;

            float rr = glm::length(r1);
            if (!std::isfinite(rr) || rr < 1.0e-6f) return 1.0e9f;
            float radial_v = glm::dot(v1, r1 / rr);
            score += std::abs(rr - r0) / std::max(r0, 1.0e-6f);
            score += std::abs(radial_v) / std::max(v_circ, 1.0e-4f) * 0.40f;

            r = r1;
            v = v1;
        }
        score /= (float)steps;
        score += std::abs(v_tan - vt_center) / std::max(v_circ, 1.0e-4f) * 0.10f;
        return score;
    };

    float best_vt = vt_center;
    float best_score = score_candidate(best_vt);
    float lo = vt_center * 0.70f;
    float hi = vt_center * 1.30f;
    const int scans = 16;
    for (int i = 0; i <= scans; ++i) {
        float t = (float)i / (float)scans;
        float vt = lo + (hi - lo) * t;
        float sc = score_candidate(vt);
        if (sc < best_score) {
            best_score = sc;
            best_vt = vt;
        }
    }

    return primary.vel + r_hat * v_rad + tangent * best_vt;
}

// ── Collision Processing ────────────────────────────────────────────────────

void CosmosApp::trigger_stellar_supernova(size_t index, float dt, bool thermonuclear,
                                          glm::vec3 impact_axis, float ejecta_speed) {
    if (index >= state.bodies.size()) return;
    CelestialBody& b = state.bodies[index];
    if (b.marked_for_removal) return;

    float progenitor_mass = std::max(b.mass, 0.01f);
    StellarRemnantKind remnant = stellar_remnant_kind(progenitor_mass, thermonuclear);
    float remnant_mass = 0.0f;

    switch (remnant) {
    case REMNANT_WHITE_DWARF:
        remnant_mass = std::clamp(0.48f + progenitor_mass * 0.10f, 0.45f, 1.30f);
        break;
    case REMNANT_NEUTRON_STAR:
        remnant_mass = std::clamp(1.25f + (progenitor_mass - CORE_COLLAPSE_MIN_MASS_SOLAR) * 0.045f,
                                  1.25f, 2.40f);
        break;
    case REMNANT_BLACK_HOLE:
        remnant_mass = std::clamp(progenitor_mass * 0.35f, 3.0f, progenitor_mass * 0.85f);
        break;
    case REMNANT_NONE:
    default:
        remnant_mass = 0.0f;
        break;
    }

    float ejecta_mass = std::max(progenitor_mass - remnant_mass, 0.0f);
    int burst_count = std::clamp(cfg.fragment_count * 2, cfg.fragment_count, 24);
    glm::vec3 axis = glm::length(impact_axis) > 1.0e-4f
        ? glm::normalize(impact_axis)
        : glm::normalize(glm::vec3(0.7f, 0.3f, 0.2f));
    float burst_speed = ejecta_speed > 0.0f
        ? ejecta_speed
        : (thermonuclear ? std::max(22.0f, progenitor_mass * 2.0f)
                         : std::max(28.0f, progenitor_mass * 1.4f));

    if (ejecta_mass > 1.0e-4f) {
        spawn_fragments(b.pos, b.vel, ejecta_mass, burst_count,
                        b.frag_generation, std::max(b.temperature, 6000.0f),
                        axis, burst_speed, &b, thermonuclear ? 1.6f : 1.2f);
        register_mass_loss(b, ejecta_mass, std::max(dt, 1.0e-4f));
    }

    if (thermonuclear || remnant == REMNANT_NONE) {
        b.marked_for_removal = true;
        return;
    }

    b.mass = remnant_mass;
    b.fuel = 0.0f;
    b.internal_energy *= 0.1f;
    clear_ring_system(b);
    clear_impact_signature(b);

    if (remnant == REMNANT_WHITE_DWARF) {
        b.stellar_stage = SSTAGE_WHITE_DWARF;
        b.temperature = std::clamp(22000.0f + progenitor_mass * 1800.0f, 9000.0f, 120000.0f);
        b.radius = std::max(1.6f, 1.2f + 1.7f / std::pow(std::max(b.mass, 0.25f), 0.35f));
        b.luminosity = 0.002f + progenitor_mass * 0.0004f;
        b.type = classify_star_spectral(std::max(b.temperature, 2200.0f), std::max(b.mass, 0.1f));
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else if (remnant == REMNANT_NEUTRON_STAR) {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 120000.0f;
        b.radius = 3.0f;
        b.luminosity = 0.02f;
        b.type = classify_star_spectral(std::max(b.temperature, 2200.0f), std::max(b.mass, 0.1f));
        float spin_sign = (b.angular_vel < 0.0f) ? -1.0f : 1.0f;
        b.angular_vel = spin_sign * std::clamp(std::abs(b.angular_vel) * 4.0f + 0.002f, 0.002f, 0.05f);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 0.0f;
        b.luminosity = 0.0f;
        b.type = classify_black_hole(b.mass);
        b.radius = std::max(0.5f, 2.0f * b.mass);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    }

    b.vel += axis * (burst_speed * 0.04f / std::max(b.mass, 0.1f));
    refresh_body_render_state(b, &state);
}

bool CosmosApp::handle_stellar_collision_supernova(size_t i, size_t j, float rel_speed,
                                                   float impact_energy, float escape_speed,
                                                   const glm::vec3& impact_axis, float dt) {
    if (!is_star_type(state.bodies[i].type) || !is_star_type(state.bodies[j].type))
        return false;

    CelestialBody& a = state.bodies[i];
    CelestialBody& b = state.bodies[j];
    float total_mass = a.mass + b.mass;
    bool white_dwarf_pair = (a.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_WHITE_DWARF);
    bool thermonuclear = white_dwarf_pair &&
        total_mass >= CHANDRASEKHAR_LIMIT_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 0.7f, escape_speed * 1.1f);
    bool core_collapse = total_mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 1.15f, escape_speed * 1.6f) &&
        impact_energy > total_mass * 25.0f;

    if (!thermonuclear && !core_collapse)
        return false;

    size_t big = (a.mass >= b.mass) ? i : j;
    size_t small = (big == i) ? j : i;
    CelestialBody merged = state.bodies[big];

    merged.pos = (a.pos * a.mass + b.pos * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.vel = (a.vel * a.mass + b.vel * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.mass = total_mass;
    merged.radius = std::cbrt(std::max(a.radius * a.radius * a.radius +
                                       b.radius * b.radius * b.radius, 1.0f));
    merged.temperature = std::max((a.temperature * a.mass + b.temperature * b.mass) /
                                  std::max(total_mass, 1.0e-6f),
                                  thermonuclear ? 140000.0f : 90000.0f);
    merged.fuel = thermonuclear ? 0.0f : merged_star_fuel(a, b, total_mass) * 0.7f;
    merged.internal_energy = a.internal_energy + b.internal_energy + impact_energy * 0.15f;
    merged.angular_vel = (a.angular_vel * a.mass + b.angular_vel * b.mass) /
                         std::max(total_mass, 1.0e-6f);
    merged.type = classify_star_spectral(std::max(merged.temperature, 2200.0f), std::max(merged.mass, 0.1f));
    merged.stellar_stage = thermonuclear ? SSTAGE_WHITE_DWARF :
        ((a.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_RED_GIANT)
            ? SSTAGE_RED_GIANT : SSTAGE_MAIN_SEQUENCE);

    state.bodies[big] = merged;
    state.bodies[small].marked_for_removal = true;
    trigger_stellar_supernova(big, dt, thermonuclear, impact_axis, std::max(rel_speed * 0.6f, 24.0f));
    return true;
}

void CosmosApp::process_collisions(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = i + 1; j < n; ++j) {
            if (bodies[j].marked_for_removal) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;

            glm::vec3 prev_diff = diff - rel_vel * dt;
            glm::vec3 seg = diff - prev_diff;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_diff, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_diff + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);

            bool overlap_now = (dist < touch);
            bool swept_hit = (sweep_dist < touch);
            if (!overlap_now && !swept_hit) continue;

            float contact_dist = overlap_now ? dist : sweep_dist;
            glm::vec3 normal = overlap_now ? (diff / std::max(dist, 1.0e-6f))
                                           : (closest_rel / std::max(sweep_dist, 1.0e-6f));
            if (glm::length(normal) < 1.0e-5f) {
                if (glm::length(rel_vel) > 1.0e-5f) {
                    normal = glm::normalize(rel_vel);
                } else {
                    // Deterministic fallback axis for near-identical overlap states.
                    uint32_t h = (uint32_t)(i * 73856093u) ^ (uint32_t)(j * 19349663u);
                    float x = ((h & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float y = (((h >> 10) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float z = (((h >> 20) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    normal = glm::normalize(glm::vec3(x, y, z));
                    if (glm::length(normal) < 1.0e-5f)
                        normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }

            float overlap = std::max(touch - contact_dist, 0.0f);
            float overlap_fraction = overlap / std::max(touch, 1.0e-6f);
            float total_mass = bodies[i].mass + bodies[j].mass;
            if (total_mass <= 1.0e-8f) continue;

            // Immediate depenetration to avoid sticky overlap accumulation.
            if (overlap_now && overlap > 0.0f) {
                bodies[i].pos -= normal * overlap * (bodies[j].mass / std::max(total_mass, 1.0e-6f));
                bodies[j].pos += normal * overlap * (bodies[i].mass / std::max(total_mass, 1.0e-6f));
            }
            float rel_speed = glm::length(rel_vel);
            float vel_along = glm::dot(rel_vel, normal);
            float closing_speed = std::max(-vel_along, 0.0f);
            float escape_speed = body_escape_speed(bodies[i], bodies[j], cfg.G);

            bool soft_body_pair =
                !is_star_type(bodies[i].type) && !is_star_type(bodies[j].type) &&
                !is_black_hole_type(bodies[i].type) && !is_black_hole_type(bodies[j].type) &&
                bodies[i].type != CTYPE_NEBULA && bodies[j].type != CTYPE_NEBULA;
            bool small_soft_pair = soft_body_pair &&
                std::max(bodies[i].radius, bodies[j].radius) < (EARTH_RADIUS_SIM_UNITS * 0.95f);
            bool compact_i = is_star_type(bodies[i].type) || is_black_hole_type(bodies[i].type);
            bool compact_j = is_star_type(bodies[j].type) || is_black_hole_type(bodies[j].type);
            bool compact_vs_soft = (compact_i != compact_j);

            float infall_speed = soft_body_pair ? std::max(closing_speed, escape_speed * 0.45f) : closing_speed;
            float compression_speed = overlap_fraction * escape_speed * (soft_body_pair ? 2.8f : 1.6f);
            float impact_speed = std::max(rel_speed, std::max(infall_speed, compression_speed));
            float reduced_mass = (bodies[i].mass * bodies[j].mass) / std::max(total_mass, 1.0e-6f);
            float impact_energy = 0.5f * reduced_mass * impact_speed * impact_speed;

            float binding_i = body_gravitational_binding_energy(bodies[i], cfg.G);
            float binding_j = body_gravitational_binding_energy(bodies[j], cfg.G);
            float combined_binding = binding_i + binding_j;
            float disruption_i = impact_energy / std::max(binding_i, 1.0e-6f);
            float disruption_j = impact_energy / std::max(binding_j, 1.0e-6f);
            float combined_disruption = impact_energy / std::max(combined_binding, 1.0e-6f);
            glm::vec3 impact_axis = (rel_speed > 1.0e-5f) ? (rel_vel / rel_speed) : normal;

            // Collision heating (kinetic -> thermal/internal).
            if (cfg.temperature_system) {
                float heat = impact_energy * cfg.collision_heating * (soft_body_pair ? 1.35f : 0.85f);
                bodies[i].internal_energy += heat * 0.5f;
                bodies[j].internal_energy += heat * 0.5f;
                bodies[i].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[i].mass, 1.0e-6f), 18000.0f);
                bodies[j].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[j].mass, 1.0e-6f), 18000.0f);
            }

            if (handle_stellar_collision_supernova(i, j, rel_speed, impact_energy,
                                                   escape_speed, impact_axis, dt)) {
                continue;
            }

            float merge_speed_limit = 0.0f;
            float fragment_speed_limit = 0.0f;
            if (soft_body_pair) {
                if (small_soft_pair) {
                    merge_speed_limit = std::max(escape_speed * 0.20f, 1.0e-4f);
                    fragment_speed_limit = std::max(escape_speed * 0.35f, 2.5e-4f);
                } else {
                    merge_speed_limit = std::max(std::max(escape_speed * 0.55f, cfg.merge_speed_threshold * 0.0005f), 0.0015f);
                    fragment_speed_limit = std::max(std::max(escape_speed * 2.20f, cfg.fragment_speed_threshold * 0.0010f),
                                                    merge_speed_limit * 2.0f);
                }
            } else {
                merge_speed_limit = std::max(cfg.merge_speed_threshold, escape_speed * 0.95f);
                fragment_speed_limit = std::max(cfg.fragment_speed_threshold, escape_speed * 1.15f);
            }

            bool catastrophic_fragment =
                (impact_speed >= (soft_body_pair ? fragment_speed_limit * 1.15f : fragment_speed_limit) ||
                 combined_disruption > (soft_body_pair ? 0.35f : 0.72f) ||
                 disruption_i > (soft_body_pair ? 0.38f : 0.92f) ||
                 disruption_j > (soft_body_pair ? 0.38f : 0.92f) ||
                 (compact_vs_soft && (swept_hit || overlap_fraction > 0.005f)));

            bool optional_fragment = cfg.collision_fragmentation &&
                (impact_speed >= fragment_speed_limit * (soft_body_pair ? 0.90f : 0.90f) ||
                 combined_disruption > (soft_body_pair ? 0.22f : 0.60f) ||
                 disruption_i > (soft_body_pair ? 0.26f : 0.75f) ||
                 disruption_j > (soft_body_pair ? 0.26f : 0.75f));

            bool fragment_candidate = catastrophic_fragment || optional_fragment ||
                (soft_body_pair && swept_hit && impact_speed > fragment_speed_limit * 0.75f);
            bool ultra_gentle_soft = soft_body_pair &&
                impact_speed < merge_speed_limit * 0.95f &&
                combined_disruption < 0.030f &&
                overlap_fraction < 0.12f;
            if (small_soft_pair) {
                ultra_gentle_soft = impact_speed < merge_speed_limit * 0.60f &&
                    combined_disruption < 0.005f &&
                    overlap_fraction < 0.01f;
            }
            if (ultra_gentle_soft)
                fragment_candidate = false;
            if (small_soft_pair && cfg.collision_fragmentation &&
                (overlap_now || swept_hit) && !ultra_gentle_soft)
                fragment_candidate = true;

            bool sticky_soft_merge = soft_body_pair && !cfg.collision_fragmentation &&
                overlap_now && !fragment_candidate &&
                impact_speed < fragment_speed_limit * 0.95f &&
                combined_disruption < 0.20f;

            bool merge_candidate = cfg.collision_merging &&
                !swept_hit &&
                ((overlap_now &&
                  rel_speed <= merge_speed_limit &&
                  closing_speed <= merge_speed_limit &&
                  impact_speed <= fragment_speed_limit * (soft_body_pair ? 0.70f : 0.90f) &&
                  overlap_fraction >= (soft_body_pair ? 0.03f : 0.006f) &&
                  combined_disruption < (soft_body_pair ? 0.06f : 0.55f)) ||
                 sticky_soft_merge) &&
                !fragment_candidate;
            if (soft_body_pair && cfg.collision_fragmentation)
                merge_candidate = merge_candidate && ultra_gentle_soft;
            if (small_soft_pair && cfg.collision_fragmentation)
                merge_candidate = false;

            if (merge_candidate) {
                size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small = (big == i) ? j : i;
                CelestialBody pre_big = bodies[big];
                CelestialBody pre_small = bodies[small];

                glm::vec3 com_pos = (pre_big.pos * pre_big.mass + pre_small.pos * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                glm::vec3 com_vel = (pre_big.vel * pre_big.mass + pre_small.vel * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                float merged_temp = (pre_big.temperature * pre_big.mass + pre_small.temperature * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);

                bodies[big].pos = com_pos;
                bodies[big].vel = com_vel;
                bodies[big].mass = total_mass;
                bodies[big].radius = std::cbrt(std::max(pre_big.radius * pre_big.radius * pre_big.radius +
                                                        pre_small.radius * pre_small.radius * pre_small.radius,
                                                        1.0e-6f));
                bodies[big].temperature = merged_temp +
                    std::min(impact_energy * (soft_body_pair ? 28.0f : 10.0f) / std::max(total_mass, 1.0e-6f), 4500.0f);
                bodies[big].internal_energy = pre_big.internal_energy + pre_small.internal_energy + impact_energy * 0.06f;
                bodies[big].angular_vel = (pre_big.angular_vel * pre_big.mass + pre_small.angular_vel * pre_small.mass) /
                                          std::max(total_mass, 1.0e-6f);

                if (is_star_type(pre_big.type) && is_star_type(pre_small.type)) {
                    bodies[big].fuel = merged_star_fuel(pre_big, pre_small, total_mass);
                    bodies[big].luminosity = std::pow(std::max(bodies[big].mass, 0.08f), 3.2f) * 0.1f;
                    bodies[big].type = classify_star_spectral(std::max(bodies[big].temperature, 2200.0f),
                                                              std::max(bodies[big].mass, 0.1f));
                    if (bodies[big].mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
                        (pre_big.stellar_stage == SSTAGE_RED_GIANT ||
                         pre_small.stellar_stage == SSTAGE_RED_GIANT ||
                         bodies[big].fuel < 0.18f)) {
                        trigger_stellar_supernova(big, dt, false, impact_axis, std::max(rel_speed * 0.45f, 18.0f));
                    }
                    if ((pre_big.stellar_stage == SSTAGE_WHITE_DWARF || pre_small.stellar_stage == SSTAGE_WHITE_DWARF) &&
                        bodies[big].mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
                        trigger_stellar_supernova(big, dt, true, impact_axis, std::max(rel_speed * 0.50f, 22.0f));
                    }
                } else {
                    bodies[big].fuel = std::max(pre_big.fuel, pre_small.fuel);
                }

                // Gentle post-merge ejecta only for high-disruption mergers.
                if (!is_star_type(pre_big.type) && !is_black_hole_type(pre_big.type) &&
                    !is_star_type(pre_small.type) && !is_black_hole_type(pre_small.type) &&
                    cfg.collision_fragmentation) {
                    float ejecta_fraction = std::clamp((combined_disruption - (soft_body_pair ? 0.10f : 0.22f)) *
                                                       (soft_body_pair ? 0.35f : 0.20f),
                                                       0.0f, soft_body_pair ? 0.18f : 0.10f);
                    float ejecta_mass = total_mass * ejecta_fraction;
                    if (ejecta_mass > 1.0e-8f) {
                        int ejecta_count = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 8);
                        float merger_ejecta_speed = soft_body_pair
                            ? std::clamp(escape_speed * 0.55f + closing_speed * 0.20f, 0.004f, 0.045f)
                            : std::max(impact_speed * 0.35f, 2.0f);
                        spawn_fragments((pre_big.pos + pre_small.pos) * 0.5f, com_vel, ejecta_mass,
                                        ejecta_count, std::max(pre_big.frag_generation, pre_small.frag_generation),
                                        std::max(pre_big.temperature, pre_small.temperature),
                                        (big == i) ? normal : -normal,
                                        merger_ejecta_speed,
                                        &pre_small, combined_disruption);
                        register_mass_loss(bodies[big], ejecta_mass, std::max(dt, 1.0e-4f));
                        float remaining_mass = std::max(total_mass - ejecta_mass, 1.0e-8f);
                        float mass_scale = std::cbrt(remaining_mass / std::max(total_mass, 1.0e-8f));
                        bodies[big].mass = remaining_mass;
                        bodies[big].radius = std::max(bodies[big].radius * mass_scale, 0.1f);
                    }

                    apply_impact_signature(
                        bodies[big], (big == i) ? normal : -normal,
                        std::clamp(combined_disruption, 0.0f, 1.2f),
                        pre_small.mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(total_mass, 1.0e-6f), 0.0f, 1.5f),
                        std::clamp(pre_small.radius / std::max(bodies[big].radius, 0.1f), 0.08f, 0.95f));
                }

                bodies[big].props_valid = false;
                bodies[big].visuals_valid = false;
                bodies[small].marked_for_removal = true;
                continue;
            }

            bool closing_contact = vel_along < 0.0f || (soft_body_pair && dist < touch * 0.99f) || swept_hit;
            if (!closing_contact) continue;

            bool fragmenting = fragment_candidate;
            float restitution = fragmenting ? 0.0f : (soft_body_pair ? 0.0f : 0.28f);
            float inv_mass_sum = (1.0f / std::max(bodies[i].mass, 1.0e-6f)) +
                                 (1.0f / std::max(bodies[j].mass, 1.0e-6f));
            float impulse_speed = (vel_along < -1.0e-5f) ? vel_along : (soft_body_pair ? 0.0f : -impact_speed * 0.35f);
            float j_impulse = -(1.0f + restitution) * impulse_speed / std::max(inv_mass_sum, 1.0e-6f);
            glm::vec3 impulse = normal * j_impulse;
            bodies[i].vel -= impulse / std::max(bodies[i].mass, 1.0e-6f);
            bodies[j].vel += impulse / std::max(bodies[j].mass, 1.0e-6f);

            // Tangential damping keeps non-fragment impacts from behaving like pinballs.
            glm::vec3 post_rel = bodies[j].vel - bodies[i].vel;
            glm::vec3 tangential = post_rel - normal * glm::dot(post_rel, normal);
            float tangential_len = glm::length(tangential);
            if (tangential_len > 1.0e-6f) {
                glm::vec3 tangent_dir = tangential / tangential_len;
                float tangential_reduce = tangential_len * (fragmenting ? 0.80f : (soft_body_pair ? 0.90f : 0.35f));
                float jt = tangential_reduce / std::max(inv_mass_sum, 1.0e-6f);
                glm::vec3 tangent_impulse = tangent_dir * jt;
                bodies[i].vel += tangent_impulse / std::max(bodies[i].mass, 1.0e-6f);
                bodies[j].vel -= tangent_impulse / std::max(bodies[j].mass, 1.0e-6f);
            }

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            auto can_fragment = [&](const CelestialBody& body) {
                return body.mass >= effective_min_frag_mass &&
                       (int)body.frag_generation < cfg.max_frag_generation &&
                       !is_star_type(body.type) &&
                       !is_black_hole_type(body.type);
            };

            if (fragmenting) {
                bool fragment_i = can_fragment(bodies[i]) &&
                    (disruption_i > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[i].mass <= bodies[j].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));
                bool fragment_j = can_fragment(bodies[j]) &&
                    (disruption_j > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[j].mass <= bodies[i].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));

                if (combined_disruption > (soft_body_pair ? 0.30f : 0.90f) &&
                    can_fragment(bodies[i]) && can_fragment(bodies[j])) {
                    fragment_i = true;
                    fragment_j = true;
                }
                if (!fragment_i && !fragment_j) {
                    if (bodies[i].mass <= bodies[j].mass && (can_fragment(bodies[i]) || bodies[i].mass > 2.0e-9f))
                        fragment_i = true;
                    else if (can_fragment(bodies[j]) || bodies[j].mass > 2.0e-9f)
                        fragment_j = true;
                }

                int shock_fragments = std::clamp(
                    cfg.fragment_count + (int)std::floor(combined_disruption * (soft_body_pair ? 4.0f : 2.0f)),
                    cfg.fragment_count, 12);
                if (soft_body_pair && !compact_vs_soft)
                    shock_fragments = std::clamp(shock_fragments, 2, 6);
                if (compact_vs_soft)
                    shock_fragments = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 6);
                float ejecta_speed = 0.0f;
                if (soft_body_pair && !compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.40f + combined_disruption * 0.60f) + closing_speed * 0.18f,
                        0.004f, 0.060f);
                } else if (compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.65f + combined_disruption * 0.70f) + closing_speed * 0.28f,
                        0.010f, 0.20f);
                } else {
                    ejecta_speed = std::max(2.0f, impact_speed * (0.32f + combined_disruption * 0.18f));
                }

                if (fragment_i) {
                    bool keep_remnant = false;
                    float removed = bodies[i].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[i].type == CTYPE_PLANET || bodies[i].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.32f + disruption_i * 0.24f +
                            std::min(overlap_fraction, 0.6f) * 0.30f, 0.10f, 0.70f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.45f : 0.28f);
                        float remnant_mass = bodies[i].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[i].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[i].mass, 1.0e-12f));
                            bodies[i].mass = remnant_mass;
                            bodies[i].radius = std::max(bodies[i].radius * mass_scale, 0.1f);
                            bodies[i].frag_generation = std::min<uint32_t>(bodies[i].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            bodies[i].props_valid = false;
                            bodies[i].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[i].pos, bodies[i].vel, removed,
                                        shock_fragments, bodies[i].frag_generation,
                                        bodies[i].temperature, -impact_axis, ejecta_speed,
                                        &bodies[i], std::max(disruption_i, combined_disruption));
                        register_mass_loss(bodies[i], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[i].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[i], normal,
                            std::clamp(disruption_i + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[i].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[i], normal,
                        std::clamp(disruption_i + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[j].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.90f));
                }

                if (fragment_j) {
                    bool keep_remnant = false;
                    float removed = bodies[j].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.32f + disruption_j * 0.24f +
                            std::min(overlap_fraction, 0.6f) * 0.30f, 0.10f, 0.70f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.45f : 0.28f);
                        float remnant_mass = bodies[j].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[j].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[j].mass, 1.0e-12f));
                            bodies[j].mass = remnant_mass;
                            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
                            bodies[j].frag_generation = std::min<uint32_t>(bodies[j].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            bodies[j].props_valid = false;
                            bodies[j].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[j].pos, bodies[j].vel, removed,
                                        shock_fragments, bodies[j].frag_generation,
                                        bodies[j].temperature, impact_axis, ejecta_speed,
                                        &bodies[j], std::max(disruption_j, combined_disruption));
                        register_mass_loss(bodies[j], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[j].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[j], -normal,
                            std::clamp(disruption_j + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[j].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[j], -normal,
                        std::clamp(disruption_j + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[i].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.90f));
                }
            } else if (!is_star_type(bodies[i].type) && !is_black_hole_type(bodies[i].type) &&
                       !is_star_type(bodies[j].type) && !is_black_hole_type(bodies[j].type)) {
                apply_impact_signature(
                    bodies[i], normal,
                    std::clamp(disruption_i * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[j].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.80f));
                apply_impact_signature(
                    bodies[j], -normal,
                    std::clamp(disruption_j * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[i].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.80f));
            }
        }
    }
}

// ── Roche Limit ─────────────────────────────────────────────────────────────

void CosmosApp::process_roche_limit(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    bool allow_disruption = cfg.roche_limit && (cfg.roche_limit_fluid || cfg.roche_limit_rigid);

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || bodies[j].marked_for_removal) continue;
            if (is_black_hole_type(bodies[j].type)) continue;
            if (bodies[i].mass <= bodies[j].mass * 2.5f) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 delta = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(delta);
            bool fluid_like_secondary = roche_secondary_fluid_like(bodies[j]);
            float roche_fluid = roche_distance_for_mode(bodies[i], bodies[j], true);
            float roche_rigid = roche_distance_for_mode(bodies[i], bodies[j], false);
            float heating_limit = std::max(roche_fluid, roche_rigid);

            float disruption_limit = 0.0f;
            bool using_fluid_limit = false;
            if (allow_disruption) {
                if (fluid_like_secondary && cfg.roche_limit_fluid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (!fluid_like_secondary && cfg.roche_limit_rigid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else if (cfg.roche_limit_fluid && !cfg.roche_limit_rigid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (cfg.roche_limit_rigid && !cfg.roche_limit_fluid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else {
                    disruption_limit = fluid_like_secondary ? roche_fluid : roche_rigid;
                    using_fluid_limit = fluid_like_secondary;
                }
            }

            glm::vec3 prev_delta = delta - rel_vel * dt;
            glm::vec3 seg = delta - prev_delta;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_delta, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_delta + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);
            float eval_dist = std::min(dist, sweep_dist);

            float interaction_limit = 0.0f;
            if (cfg.tidal_forces) interaction_limit = heating_limit;
            if (allow_disruption) interaction_limit = std::max(interaction_limit, disruption_limit);
            if (interaction_limit <= 0.0f || eval_dist >= interaction_limit) continue;
            if (eval_dist <= std::max(bodies[i].radius * 1.01f, 1.0e-4f)) continue;

            float heating_overflow = std::clamp((heating_limit - eval_dist) /
                std::max(heating_limit, 1.0e-4f), 0.0f, 1.0f);
            float disruption_overflow = (allow_disruption && disruption_limit > 0.0f)
                ? std::clamp((disruption_limit - eval_dist) / std::max(disruption_limit, 1.0e-4f), 0.0f, 1.0f)
                : 0.0f;
            glm::vec3 tidal_axis = (sweep_dist < dist && sweep_dist > 1.0e-5f)
                ? (closest_rel / sweep_dist)
                : glm::normalize(delta);
            if (glm::length(tidal_axis) < 1.0e-5f)
                tidal_axis = glm::vec3(0.0f, 1.0f, 0.0f);

            // Baseline tidal work/heat in Roche zone.
            float tide_strain = cfg.G * bodies[i].mass * bodies[j].radius /
                std::max(eval_dist * eval_dist * eval_dist, 1.0e-6f);
            float tidal_work = tide_strain * (0.45f + heating_overflow * 2.0f) * dt * 1400.0f;
            if (tidal_work > 0.0f) {
                bodies[j].internal_energy += tidal_work;
                bodies[j].temperature += std::min(tidal_work / std::max(bodies[j].mass * 0.08f, 1.0e-7f), 4000.0f);
                if (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON) {
                    bodies[j].impact_heat = std::max(
                        bodies[j].impact_heat,
                        std::clamp(heating_overflow * 0.75f, 0.0f, 0.95f));
                    bodies[j].visuals_valid = false;
                }
            }
            if (!allow_disruption || eval_dist >= disruption_limit)
                continue;

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            if (bodies[j].mass < effective_min_frag_mass ||
                (int)bodies[j].frag_generation >= cfg.max_frag_generation) {
                continue;
            }

            // Partial Roche stripping (instead of instant full deletion).
            float secondary_mass = bodies[j].mass;
            float local_orbital_speed = std::sqrt(std::max(cfg.G * bodies[i].mass /
                std::max(eval_dist, 1.0e-6f), 0.0f));
            float speed_ratio = glm::length(rel_vel) / std::max(local_orbital_speed, 1.0e-4f);
            float dynamic_overflow = disruption_overflow +
                std::clamp((speed_ratio - 0.8f) * 0.28f, 0.0f, 0.45f);
            float strip_fraction = using_fluid_limit
                ? std::clamp(0.10f + dynamic_overflow * 0.92f, 0.04f, 0.98f)
                : std::clamp(0.06f + dynamic_overflow * 0.66f, 0.02f, 0.86f);
            if (eval_dist < disruption_limit * (using_fluid_limit ? 0.62f : 0.52f) ||
                disruption_overflow > (using_fluid_limit ? 0.72f : 0.82f))
                strip_fraction = std::max(strip_fraction, using_fluid_limit ? 0.93f : 0.78f);
            float stripped_mass = secondary_mass * strip_fraction;
            if (stripped_mass <= 1.0e-9f) continue;

            float ring_fraction = (cfg.planetary_rings && body_can_host_rings(bodies[i]))
                ? std::clamp((using_fluid_limit ? 0.18f : 0.08f) + disruption_overflow * 0.32f, 0.0f, 0.75f)
                : 0.0f;
            float ring_mass = stripped_mass * ring_fraction;
            if (ring_mass > 0.0f) {
                bodies[i].mass += ring_mass;
                add_ring_material(bodies[i], bodies[j], ring_mass, disruption_limit);
                bodies[i].props_valid = false;
                bodies[i].visuals_valid = false;
            }

            float fragment_mass = std::max(stripped_mass - ring_mass, 0.0f);
            if (fragment_mass > 1.0e-9f) {
                int fragment_count = std::clamp(
                    std::max(1, cfg.fragment_count / 2 + (int)std::floor(disruption_overflow * (using_fluid_limit ? 3.8f : 2.4f))),
                                                1, 10);
                float speed_scale = using_fluid_limit ? 0.08f : 0.055f;
                float roche_ejecta_speed = std::clamp(local_orbital_speed * (speed_scale + dynamic_overflow * 0.13f),
                                                      0.0003f, using_fluid_limit ? 0.012f : 0.009f);
                spawn_fragments(bodies[j].pos, bodies[j].vel, fragment_mass, fragment_count,
                                bodies[j].frag_generation, bodies[j].temperature,
                                tidal_axis, roche_ejecta_speed,
                                &bodies[j], 0.45f + disruption_overflow * 0.90f);
            }

            register_mass_loss(bodies[j], stripped_mass, std::max(dt, 1.0e-4f));
            float remaining_mass = std::max(secondary_mass - stripped_mass, 0.0f);
            if (remaining_mass <= effective_min_frag_mass) {
                bodies[j].marked_for_removal = true;
                continue;
            }

            float mass_scale = std::cbrt(remaining_mass / std::max(secondary_mass, 1.0e-8f));
            bodies[j].mass = remaining_mass;
            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
            bodies[j].props_valid = false;
            bodies[j].visuals_valid = false;
            apply_impact_signature(
                bodies[j], -tidal_axis,
                std::clamp(disruption_overflow * 0.8f, 0.0f, 1.2f),
                stripped_mass / std::max(secondary_mass, 1.0e-8f),
                std::clamp(tidal_work / std::max(bodies[j].mass, 1.0e-8f), 0.0f, 1.0f),
                std::clamp(0.18f + disruption_overflow * 0.45f, 0.08f, 0.92f));
        }
    }
}

// ── Temperature ─────────────────────────────────────────────────────────────

void CosmosApp::process_temperature(float dt) {
    auto& bodies = state.bodies;
    const float background = 2.7f;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) && b.fuel > 0.05f) {
            b.luminosity = std::pow(std::max(b.mass, 0.05f), 3.5f) * 0.1f;
            continue;
        }
        b.temperature -= cfg.radiative_cooling * (b.temperature - background) * dt;
        if (b.temperature < background) b.temperature = background;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || is_star_type(b.type)) continue;

        float eq_t4_sum = std::pow(background, 4.0f);
        for (size_t j = 0; j < bodies.size(); ++j) {
            if (i == j || bodies[j].marked_for_removal || !is_star_type(bodies[j].type)) continue;
            float eq_t = equilibrium_temperature_from_star(b, bodies[j]);
            eq_t4_sum += std::pow(eq_t, 4.0f);
        }

        float target_temp = std::pow(std::max(eq_t4_sum, std::pow(background, 4.0f)), 0.25f);
        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float greenhouse = 1.0f + std::min(b.cached_props.atmosphere.pressure * 0.015f, 0.35f);
            target_temp *= greenhouse;
        }

        float thermal_response = 0.35f / (0.6f + std::cbrt(std::max(b.mass, 1.0e-5f)) * 0.8f);
        b.temperature += (target_temp - b.temperature) * std::min(thermal_response * dt, 1.0f);
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            if (bodies[j].marked_for_removal) continue;

            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;
            if (dist < touch * 1.15f) {
                float contact = 1.0f - dist / std::max(touch * 1.15f, 1.0e-3f);
                float delta = bodies[j].temperature - bodies[i].temperature;
                float exchange = delta * contact * std::min(0.25f * dt, 0.5f);
                float inv_heat_i = 1.0f / std::max(bodies[i].mass, 0.01f);
                float inv_heat_j = 1.0f / std::max(bodies[j].mass, 0.01f);
                bodies[i].temperature += exchange * inv_heat_i;
                bodies[j].temperature -= exchange * inv_heat_j;
            }

            if (cfg.tidal_forces) {
                size_t big_idx = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small_idx = (big_idx == i) ? j : i;
                CelestialBody& big = bodies[big_idx];
                CelestialBody& small = bodies[small_idx];

                if (dist > std::max(big.radius * 1.05f, 1.0f) && big.mass > small.mass * 2.0f) {
                    bool fluid_like_small = roche_secondary_fluid_like(small);
                    float roche_fluid = roche_distance_for_mode(big, small, true);
                    float roche_rigid = roche_distance_for_mode(big, small, false);
                    float roche_dist = 0.0f;
                    if (cfg.roche_limit_fluid && cfg.roche_limit_rigid)
                        roche_dist = fluid_like_small ? roche_fluid : roche_rigid;
                    else if (cfg.roche_limit_fluid)
                        roche_dist = roche_fluid;
                    else if (cfg.roche_limit_rigid)
                        roche_dist = roche_rigid;
                    else
                        roche_dist = std::max(roche_fluid, roche_rigid);
                    float tidal_reach = std::max(roche_dist * 6.0f, big.radius * 3.0f);
                    if (dist < tidal_reach) {
                        float proximity = 1.0f - std::clamp((dist - big.radius) /
                            std::max(tidal_reach - big.radius, 1.0e-3f), 0.0f, 1.0f);
                        float orbital_rate = std::sqrt(std::max(cfg.G * (big.mass + small.mass) /
                            std::max(dist * dist * dist, 1.0e-6f), 0.0f));
                        float spin_mismatch = std::abs(std::abs(small.angular_vel) - orbital_rate);
                        float shear_speed = glm::length(small.vel - big.vel);
                        float strain = cfg.G * big.mass * small.radius / std::max(dist * dist * dist, 1.0e-6f);
                        float dissipation = strain * strain *
                            (0.20f + spin_mismatch * 3600.0f + shear_speed * 0.12f) *
                            proximity * proximity * dt * 18000.0f;
                        if (dissipation > 0.0f) {
                            small.internal_energy += dissipation;
                            small.temperature += std::min(dissipation / std::max(small.mass * 0.08f, 1.0e-7f), 12000.0f);
                            if (small.type == CTYPE_PLANET || small.type == CTYPE_MOON) {
                                small.impact_heat = std::max(small.impact_heat, std::clamp(proximity * 0.75f, 0.0f, 0.95f));
                                small.visuals_valid = false;
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (b.internal_energy > 0.0f) {
            float transfer_rate = 15.0f;
            if (b.impact_heat > 0.01f || b.material_phase == PHASE_MOLTEN || b.material_phase == PHASE_PLASMA)
                transfer_rate = 95.0f;
            float transfer = std::min(b.internal_energy, transfer_rate * dt);
            b.temperature += transfer / std::max(b.mass, 0.01f);
            b.internal_energy -= transfer;
        }

        if (b.impact_heat > 0.0f || b.impact_ejecta > 0.0f || b.impact_crater_strength > 0.0f) {
            float mass_scale = std::clamp(std::cbrt(std::max(b.mass, 1.0e-6f)) + 0.35f, 0.35f, 6.0f);
            b.impact_heat = std::max(0.0f, b.impact_heat - dt * (0.000028f + cfg.radiative_cooling * 0.020f) / mass_scale);
            b.impact_ejecta = std::max(0.0f, b.impact_ejecta - dt * 0.000020f *
                (0.55f + std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 0.65f));
            b.impact_crater_strength = std::max(0.0f, b.impact_crater_strength - dt * 0.0000018f / mass_scale);
            b.impact_radius = std::max(0.0f, b.impact_radius - dt * 0.0000012f / mass_scale);
        }
    }
}

void CosmosApp::process_material_phases(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;

        MaterialComposition materials = derive_materials(b);
        MaterialPhase prev_phase = static_cast<MaterialPhase>(b.material_phase);
        MaterialPhase next_phase = infer_material_phase(b, materials);
        float next_intensity = phase_intensity_for_body(b, next_phase, materials);

        if (cfg.planetary_rings && b.ring_density > 0.001f) {
            if (!body_can_host_rings(b) || is_star_type(b.type) || is_black_hole_type(b.type)) {
                clear_ring_system(b);
            } else {
                float hot_sublimation = std::clamp((b.temperature - 260.0f) / 1600.0f, 0.0f, 1.0f);
                float spread = std::min(0.06f, dt * (0.0008f + b.ring_density * 0.0024f +
                                                     std::abs(b.angular_vel) * 8.0f));
                b.ring_inner_radius = std::max(b.radius * 1.15f, b.ring_inner_radius - b.radius * spread * 0.08f);
                b.ring_outer_radius = std::max(b.ring_inner_radius + b.radius * 0.20f,
                                               b.ring_outer_radius + b.radius * spread * 0.45f);
                b.ring_density = std::max(0.0f, b.ring_density -
                    dt * (0.00008f + hot_sublimation * 0.0014f));
                b.ring_ice_fraction = std::clamp(b.ring_ice_fraction -
                    hot_sublimation * dt * 0.0016f, 0.0f, 1.0f);
                if (b.ring_density < 0.015f || b.ring_outer_radius <= b.ring_inner_radius + b.radius * 0.06f)
                    clear_ring_system(b);
            }
        }

        if (!is_star_type(b.type) && !is_black_hole_type(b.type)) {
            if ((next_phase == PHASE_MOLTEN || next_phase == PHASE_PLASMA) &&
                (b.type == CTYPE_PLANET || b.type == CTYPE_MOON ||
                 b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET)) {
                float boiloff = std::clamp((b.temperature - 900.0f) / 1800.0f, 0.0f, 1.0f);
                b.internal_energy += boiloff * dt * 1.8f;
                if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
                    float prev_retention = b.atmosphere_retention;
                    b.atmosphere_retention = std::clamp(b.atmosphere_retention - boiloff * dt * 0.0012f, 0.0f, 1.0f);
                    if (std::abs(prev_retention - b.atmosphere_retention) > 1.0e-6f)
                        b.props_valid = false;
                }
            }

            bool gas_dominated = gas_dominated_body(b, materials);
            if ((b.type == CTYPE_NEBULA || gas_dominated) &&
                b.mass >= HYDROGEN_BURNING_MASS_SOLAR * 0.65f) {
                float gravity_drive = std::clamp(cfg.G * b.mass /
                    std::max(b.radius * b.radius, 1.0e-6f), 0.0f, 2.0f);
                float mass_drive = std::clamp((b.mass / HYDROGEN_BURNING_MASS_SOLAR) - 0.65f, 0.0f, 2.5f);
                float temp_drive = std::clamp((b.temperature - 250.0f) / 2800.0f, 0.0f, 1.2f);
                float energy_drive = std::clamp(b.internal_energy / std::max(b.mass * 30.0f, 0.04f), 0.0f, 1.4f);
                float collapse_drive = mass_drive * 0.70f + gravity_drive * 0.30f +
                                       temp_drive * 0.35f + energy_drive * 0.25f +
                                       (b.type == CTYPE_NEBULA ? 0.22f : 0.0f);
                if (collapse_drive > 0.05f) {
                    float advance = std::min(0.10f, dt * (0.0014f + collapse_drive * 0.0036f));
                    b.collapse_progress = std::clamp(b.collapse_progress + advance, 0.0f, 1.25f);
                    next_phase = PHASE_COLLAPSING;
                    next_intensity = std::max(next_intensity, std::clamp(b.collapse_progress, 0.15f, 1.0f));

                    float proto_temp = std::max(2200.0f,
                        expected_main_sequence_temperature(std::max(b.mass, 0.08f)) * 0.88f);
                    b.temperature += (proto_temp - b.temperature) * std::min(0.18f * dt, 0.30f);

                    CelestialBody proto = b;
                    proto.type = classify_star_spectral(proto_temp, std::max(proto.mass, 0.08f));
                    proto.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                    float target_radius = expected_star_radius(proto) * 1.35f;
                    b.radius = glm::mix(b.radius, target_radius, std::min(0.10f * dt, 0.22f));
                    b.internal_energy += collapse_drive * dt * 2.8f;

                    if (b.collapse_progress >= 1.0f || b.temperature >= 2200.0f) {
                        b.type = classify_star_spectral(std::max(b.temperature, proto_temp), std::max(b.mass, 0.08f));
                        b.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                        b.fuel = std::max(b.fuel, 0.92f);
                        b.temperature = std::clamp(std::max(b.temperature, proto_temp), 2200.0f, 55000.0f);
                        b.radius = expected_star_radius(b);
                        b.luminosity = std::pow(std::max(b.mass, 0.08f), 3.2f) * 0.1f;
                        b.material_phase = PHASE_PLASMA;
                        b.phase_intensity = 1.0f;
                        b.collapse_progress = 1.0f;
                        clear_ring_system(b);
                        b.props_valid = false;
                        b.visuals_valid = false;
                        continue;
                    }
                } else {
                    b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
                }
            } else {
                b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
            }
        } else {
            if (b.ring_density > 0.0f)
                clear_ring_system(b);
            b.material_phase = PHASE_PLASMA;
            b.phase_intensity = 1.0f;
            continue;
        }

        if (next_phase != prev_phase ||
            std::abs(next_intensity - b.phase_intensity) > 0.03f) {
            b.props_valid = false;
            b.visuals_valid = false;
        }
        b.material_phase = next_phase;
        b.phase_intensity = next_intensity;
    }
}

void CosmosApp::process_space_weather(float dt) {
    auto& bodies = state.bodies;

    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& target = bodies[i];
        if (target.marked_for_removal) continue;
        if (is_star_type(target.type) || is_black_hole_type(target.type)) continue;

        float stellar_flux = 0.0f;
        float quasar_flux = 0.0f;
        float particle_flux = estimate_space_weather_flux(target, &state, cfg.G, &stellar_flux, &quasar_flux);
        if (particle_flux <= 0.0f) continue;

        float shielding = magnetic_shielding_score(target, cfg.G);
        float unshielded_flux = particle_flux * (1.0f - std::min(shielding, 1.0f) * 0.72f);
        float heating_flux = stellar_flux * (0.22f + (1.0f - std::min(shielding, 1.0f)) * 0.55f) +
                             quasar_flux * (0.95f + (1.0f - std::min(shielding, 1.0f)) * 0.85f);

        if (cfg.temperature_system) {
            float heat_gain = heating_flux * dt * std::max(target.radius, 0.5f) * 0.55f;
            target.internal_energy += heat_gain;
            target.temperature += heat_gain / std::max(target.mass * 8.0f, 0.02f);
        }

        if (cfg.evaporation &&
            (target.type == CTYPE_PLANET || target.type == CTYPE_MOON) &&
            target.cached_props.atmosphere.pressure > 0.001f) {
            float escape = body_escape_velocity(target, cfg.G);
            float retention_resist = 0.20f + escape * 0.30f + shielding * 0.90f;
            float erosion = unshielded_flux * (0.0004f + cfg.evaporation_rate * 0.0022f) * dt /
                            std::max(retention_resist, 0.08f);
            if (target.cached_props.planet_class == PCLASS_GAS_GIANT ||
                target.cached_props.planet_class == PCLASS_ICE_GIANT) {
                erosion *= 0.10f;
            } else if (target.cached_props.planet_class == PCLASS_DWARF) {
                erosion *= 1.45f;
            }

            float prev_retention = target.atmosphere_retention;
            target.atmosphere_retention = std::clamp(target.atmosphere_retention - erosion, 0.0f, 1.0f);
            if (std::abs(target.atmosphere_retention - prev_retention) > 1.0e-6f) {
                target.props_valid = false;
                target.visuals_valid = false;
                float proxy_loss = std::min(target.mass * erosion * 0.01f, target.mass * 0.0015f);
                register_mass_loss(target, proxy_loss, dt);
            }
        }
    }
}

// ── Evaporation ─────────────────────────────────────────────────────────────

void CosmosApp::process_evaporation(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;

        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float vapor_threshold = 650.0f;
            if (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                b.cached_props.planet_class == PCLASS_ICE_GIANT) {
                vapor_threshold = 900.0f;
            }

            if (b.temperature > vapor_threshold) {
                float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
                float escape = body_escape_velocity(b, cfg.G);
                float escape_resist = 0.18f + escape * 0.42f;
                float class_scale = (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                                     b.cached_props.planet_class == PCLASS_ICE_GIANT) ? 0.45f : 1.0f;
                float loss = cfg.evaporation_rate * heat_factor * dt * 0.060f * class_scale /
                             std::max(escape_resist, 0.12f);
                loss = std::min(loss, b.mass * 0.030f);
                if (loss > 0.0f) {
                    b.mass -= loss;
                    b.radius = std::max(b.radius * 0.99985f, 0.08f);
                    b.atmosphere_retention = std::max(0.0f, b.atmosphere_retention - loss / std::max(b.mass + loss, 1.0e-12f));
                    b.props_valid = false;
                    b.visuals_valid = false;
                    register_mass_loss(b, loss, dt);
                }
            }
            continue;
        }

        if (b.type != CTYPE_ASTEROID && b.type != CTYPE_COMET && b.type != CTYPE_NEBULA) continue;

        float vapor_threshold = (b.type == CTYPE_COMET) ? 220.0f : 700.0f;
        if (b.temperature <= vapor_threshold) continue;

        float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
        float loss = cfg.evaporation_rate * heat_factor * dt *
            (b.type == CTYPE_NEBULA ? 0.12f : (b.type == CTYPE_COMET ? 0.07f : 0.05f));
        loss = std::min(loss, b.mass * 0.08f);
        if (loss <= 0.0f) continue;

        b.mass -= loss;
        b.radius = std::max(b.radius * 0.9996f, 0.05f);
        register_mass_loss(b, loss, dt);

        if (b.mass < 5.0e-11f) {
            b.marked_for_removal = true;
        }
    }
}

// ── Stellar Evolution ───────────────────────────────────────────────────────

void CosmosApp::process_stellar_evolution(float dt) {
    auto& bodies = state.bodies;

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal) continue;
        if (!is_star_type(b.type)) continue;

        if (b.stellar_stage == SSTAGE_WHITE_DWARF && b.mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
            trigger_stellar_supernova(i, dt, true);
            continue;
        }

        bool evolved_star =
            b.stellar_stage == SSTAGE_RED_GIANT ||
            b.stellar_stage == SSTAGE_SUPERGIANT ||
            b.stellar_stage == SSTAGE_HYPERGIANT;
        bool compact_star =
            b.stellar_stage == SSTAGE_WHITE_DWARF ||
            b.stellar_stage == SSTAGE_NEUTRON_STAR;

        // Main-sequence lifetime roughly scales as M^-2.5, with post-main-sequence
        // phases burning the remaining fuel much faster.
        float stellar_mass = std::clamp(b.mass, 0.08f, 120.0f);
        float lifetime_scale = cfg.stellar_timescale * 250000.0f;
        float main_sequence_lifetime = lifetime_scale / std::pow(stellar_mass, 2.5f);
        float burn_multiplier = compact_star ? 0.0f : (evolved_star ? 7.5f : 1.0f);
        float burn_rate = (burn_multiplier > 0.0f)
            ? dt * burn_multiplier / std::max(main_sequence_lifetime, cfg.stellar_timescale * 1200.0f)
            : 0.0f;
        b.fuel -= burn_rate;
        if (b.fuel < 0.0f) b.fuel = 0.0f;

        float wind_factor = compact_star ? 0.0f : (evolved_star ? 2.6f : 1.0f);
        float wind_loss = std::min(b.mass * wind_factor * (0.00002f + b.luminosity * 0.000002f) * dt,
                                   b.mass * (compact_star ? 0.0f : 0.005f));
        if (wind_loss > 0.0f) {
            b.mass -= wind_loss;
            register_mass_loss(b, wind_loss, dt);
        }

        // Reclassify spectral type as temperature changes
        b.type = classify_star_spectral(b.temperature, b.mass);

        // Main sequence → evolved giant / supergiant
        float giant_threshold = (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.45f : 0.30f;
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE && b.fuel < giant_threshold) {
            if (b.mass >= 20.0f)
                b.stellar_stage = SSTAGE_HYPERGIANT;
            else if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR)
                b.stellar_stage = SSTAGE_SUPERGIANT;
            else
                b.stellar_stage = SSTAGE_RED_GIANT;
            b.radius *= (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 6.0f : 5.0f;
            b.temperature *= (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.62f : 0.5f;
            b.luminosity = std::max(b.luminosity * (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR ? 180.0f : 100.0f),
                                    std::pow(std::max(b.mass, 0.1f), 3.5f) * 0.25f);
        }

        if (evolved_star && b.fuel < 0.05f) {
            if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) {
                trigger_stellar_supernova(i, dt, false);
                continue;
            }

            // Lower-mass stars end as white dwarfs after envelope loss.
            b.stellar_stage = SSTAGE_WHITE_DWARF;
            float lost = std::max(b.mass - std::clamp(0.48f + b.mass * 0.10f, 0.45f, 1.30f), 0.0f);
            if (lost > 0.0f) {
                b.mass -= lost;
                register_mass_loss(b, lost, dt);
            }
            b.radius = std::max(1.6f, 1.2f + 1.7f / std::pow(std::max(b.mass, 0.25f), 0.35f));
            b.temperature = std::clamp(22000.0f + b.mass * 4000.0f, 9000.0f, 120000.0f);
            b.luminosity = 0.002f + b.mass * 0.0004f;
            b.type = classify_star_spectral(std::max(b.temperature, 2200.0f), std::max(b.mass, 0.1f));
        }
    }
}

// ── Cleanup ─────────────────────────────────────────────────────────────────

void CosmosApp::cleanup_bodies() {
    auto& bodies = state.bodies;

    auto finite_vec3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    for (auto& b : bodies) {
        bool invalid = !finite_vec3(b.pos) || !finite_vec3(b.vel) ||
            !std::isfinite(b.mass) || !std::isfinite(b.radius) ||
            !std::isfinite(b.temperature) || !std::isfinite(b.internal_energy) ||
            b.mass <= 0.0f || b.radius <= 0.0f;
        if (invalid)
            b.marked_for_removal = true;
        if (is_star_type(b.type) || is_black_hole_type(b.type) ||
            b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            b.non_attracting = false;
        }
    }

    if (cfg.dynamic_budget_enabled) {
        int max_fragments = std::max(cfg.dynamic_max_fragments, 0);
        int max_non_attracting = std::max(cfg.dynamic_max_non_attracting, 0);
        float reduction = std::clamp(cfg.dynamic_reduction_percent, 0.01f, 1.0f);
        float target_fps = std::max(cfg.dynamic_target_fps, 1.0f);

        std::vector<size_t> attracting_fragments;
        std::vector<size_t> non_attracting_fragments;
        attracting_fragments.reserve(bodies.size());
        non_attracting_fragments.reserve(bodies.size());

        for (size_t i = 0; i < bodies.size(); ++i) {
            const auto& b = bodies[i];
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (b.non_attracting) non_attracting_fragments.push_back(i);
            else attracting_fragments.push_back(i);
        }

        if ((int)attracting_fragments.size() > max_fragments) {
            int to_convert = (int)attracting_fragments.size() - max_fragments;
            std::sort(attracting_fragments.begin(), attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_convert && k < (int)attracting_fragments.size(); ++k) {
                size_t idx = attracting_fragments[(size_t)k];
                bodies[idx].non_attracting = true;
                non_attracting_fragments.push_back(idx);
            }
        }

        int desired_non_attracting = max_non_attracting;
        if (smoothed_fps_ < target_fps) {
            float deficit = std::clamp((target_fps - smoothed_fps_) / target_fps, 0.0f, 1.0f);
            int fps_cut = (int)std::ceil((float)non_attracting_fragments.size() *
                                         reduction * (0.50f + deficit * 1.50f));
            desired_non_attracting = std::max(0, desired_non_attracting - fps_cut);
        }

        if ((int)non_attracting_fragments.size() > desired_non_attracting) {
            int overflow = (int)non_attracting_fragments.size() - desired_non_attracting;
            int chunk = (int)std::ceil((float)non_attracting_fragments.size() * reduction);
            int to_remove = std::max(overflow, chunk);
            std::sort(non_attracting_fragments.begin(), non_attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_remove && k < (int)non_attracting_fragments.size(); ++k) {
                bodies[non_attracting_fragments[(size_t)k]].marked_for_removal = true;
            }
        }
    }

    bool any_removed = false;
    for (const auto& b : bodies) {
        if (b.marked_for_removal) { any_removed = true; break; }
    }
    if (!any_removed) return;

    // Build index mapping (old → new)
    std::vector<int> remap(bodies.size(), -1);
    int new_idx = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal)
            remap[i] = new_idx++;
    }

    // Fix selected body
    if (selected_body >= 0 && selected_body < (int)bodies.size()) {
        selected_body = remap[selected_body];
    }

    // Fix parent indices
    for (auto& b : bodies) {
        if (b.parent >= 0 && b.parent < (int)remap.size())
            b.parent = remap[b.parent];
    }

    // Remove bodies and trails
    size_t write = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal) {
            if (write != i) {
                bodies[write] = bodies[i];
                if (i < state.trails.size())
                    state.trails[write] = std::move(state.trails[i]);
            }
            write++;
        }
    }
    bodies.resize(write);
    state.trails.resize(write);
}

// ── Fragment Spawning ───────────────────────────────────────────────────────

void CosmosApp::spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count,
                                uint32_t parent_generation, float source_temperature,
                                glm::vec3 impact_axis, float ejecta_speed,
                                const CelestialBody* source_body, float shock_ratio) {
    if (count < 1 || total_mass <= 0.0f) return;
    if (!std::isfinite(total_mass) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        !std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z)) return;
    if ((int)parent_generation >= cfg.max_frag_generation) return;

    size_t body_count = state.bodies.size();
    if (cfg.dynamic_budget_enabled) {
        int attract_cap = std::max(cfg.dynamic_max_fragments, 0);
        int non_attract_cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int total_budget_cap = std::max(attract_cap + non_attract_cap + 256, 1);
        if ((int)body_count >= total_budget_cap) return;
        int capacity = total_budget_cap - (int)body_count;
        count = std::min(count, capacity);

        int per_event_cap = std::max(
            1, (int)std::ceil((float)non_attract_cap *
                std::clamp(cfg.dynamic_explosion_density, 0.01f, 1.0f)));
        count = std::min(count, per_event_cap);
    }
    if (count < 1) return;

    float kMinFragmentMass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
    int max_count = std::max(1, (int)std::floor(total_mass / kMinFragmentMass));
    count = std::min(count, max_count);
    if (count < 1) count = 1;

    uint32_t seed = (uint32_t)(std::hash<float>{}(pos.x) ^ std::hash<float>{}(pos.y) ^
                               std::hash<float>{}(pos.z) ^ std::hash<float>{}(total_mass) ^
                               (parent_generation * 2654435761u));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    std::vector<float> weights((size_t)count, 0.0f);
    float weight_sum = 0.0f;
    for (int i = 0; i < count; ++i) {
        weights[(size_t)i] = 0.25f + u01(rng);
        weight_sum += weights[(size_t)i];
    }

    std::vector<float> masses((size_t)count, kMinFragmentMass);
    float remaining_mass = total_mass - kMinFragmentMass * (float)count;
    if (remaining_mass < 0.0f) remaining_mass = 0.0f;
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] += remaining_mass * (weights[(size_t)i] / std::max(weight_sum, 1.0e-6f));
    masses.back() += total_mass - std::accumulate(masses.begin(), masses.end(), 0.0f);

    auto safe_normalize = [](const glm::vec3& v, const glm::vec3& fallback) {
        float l2 = glm::dot(v, v);
        if (!std::isfinite(l2) || l2 < 1.0e-12f) return fallback;
        return v / std::sqrt(l2);
    };
    glm::vec3 axis = safe_normalize(impact_axis, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(axis, axis) < 1.0e-8f) {
        axis = safe_normalize(glm::vec3(u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    }
    MaterialComposition source_materials{};
    if (source_body != nullptr) {
        source_materials = derive_materials(*source_body);
    } else {
        source_materials.silicate = 0.75f;
        source_materials.iron = 0.25f;
    }
    bool energetic_source = source_body != nullptr &&
        (is_star_type(source_body->type) || is_black_hole_type(source_body->type) ||
         source_body->type == CTYPE_NEBULA);
    float min_ejecta = energetic_source ? 4.0f : 0.004f;
    float max_ejecta = energetic_source ? 120.0f : 0.080f;
    float base_ejecta = std::clamp(std::max(ejecta_speed, min_ejecta), min_ejecta, max_ejecta);
    if (!std::isfinite(base_ejecta)) base_ejecta = min_ejecta;
    bool gas_source = (source_body != nullptr) && gas_dominated_body(*source_body, source_materials);
    float normalized_shock = std::clamp(shock_ratio, 0.0f, 2.0f);
    float gas_frag_chance = gas_source
        ? std::clamp(source_materials.hydrogen * (0.28f + normalized_shock * 0.22f), 0.0f, 0.78f)
        : 0.0f;
    float icy_frag_chance = std::clamp(source_materials.water * (0.22f + std::max(0.0f, 0.70f - normalized_shock) * 0.30f),
                                       0.0f, 0.62f);
    float molten_bias = std::clamp((source_temperature - 900.0f) / 1500.0f + normalized_shock * 0.55f +
                                   source_materials.silicate * 0.16f + source_materials.iron * 0.20f,
                                   0.0f, 1.35f);

    // Planetary breakups should produce a few dominant chunks instead of equal tiny shards.
    if (!energetic_source && count <= 6 && remaining_mass > 0.0f) {
        int dom = (int)(u01(rng) * (float)count);
        if (dom >= count) dom = count - 1;
        float target_dom = std::clamp(total_mass * (0.35f + u01(rng) * 0.20f),
                                      total_mass * 0.25f, total_mass * 0.60f);
        float current_dom = masses[(size_t)dom];
        float extra = std::max(target_dom - current_dom, 0.0f);
        if (extra > 0.0f) {
            float donor_total = 0.0f;
            for (int i = 0; i < count; ++i) {
                if (i == dom) continue;
                donor_total += std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
            }
            if (donor_total > 1.0e-12f) {
                for (int i = 0; i < count; ++i) {
                    if (i == dom) continue;
                    float avail = std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
                    float take = extra * (avail / donor_total);
                    masses[(size_t)i] -= take;
                    masses[(size_t)dom] += take;
                }
            }
        }
    }

    std::vector<glm::vec3> rel_vels((size_t)count, glm::vec3(0.0f));
    glm::vec3 momentum_bias(0.0f);
    for (int i = 0; i < count; ++i) {
        float theta = u01(rng) * 6.28318530718f;
        float z = u01(rng) * 2.0f - 1.0f;
        float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        glm::vec3 rand_dir(r * std::cos(theta), z, r * std::sin(theta));
        glm::vec3 dir;
        float speed = 0.0f;
        if (energetic_source) {
            dir = safe_normalize(glm::mix(rand_dir, axis, 0.45f + 0.25f * u01(rng)), axis);
            speed = base_ejecta * (0.55f + u01(rng) * 0.90f);
        } else {
            glm::vec3 planar = rand_dir - axis * glm::dot(rand_dir, axis);
            if (glm::length(planar) < 1.0e-5f) {
                planar = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(planar) < 1.0e-5f)
                    planar = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            if (glm::length(planar) < 1.0e-5f)
                planar = glm::vec3(1.0f, 0.0f, 0.0f);
            dir = safe_normalize(glm::mix(safe_normalize(planar, glm::vec3(1.0f, 0.0f, 0.0f)), axis,
                                          0.12f + 0.08f * u01(rng)), axis);
            speed = base_ejecta * (0.65f + u01(rng) * 0.45f);
        }
        rel_vels[(size_t)i] = dir * speed;
        momentum_bias += rel_vels[(size_t)i] * masses[(size_t)i];
    }
    momentum_bias /= std::max(total_mass, 1.0e-6f);
    for (auto& rv : rel_vels)
        rv -= momentum_bias;

    int attracting_fragments_now = 0;
    if (cfg.dynamic_budget_enabled) {
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (!b.non_attracting) ++attracting_fragments_now;
        }
    }
    int max_attracting_fragments = std::max(cfg.dynamic_max_fragments, 0);

    for (int i = 0; i < count; i++) {
        float frag_mass = masses[(size_t)i];
        float frag_radius = std::max(1.2f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 3.0f);

        CelestialBody frag;
        glm::vec3 dir = safe_normalize(rel_vels[(size_t)i] + axis * 0.1f, axis);
        frag.pos = pos + dir * (frag_radius * 1.5f + 1.0f);
        frag.vel = vel + rel_vels[(size_t)i];
        if (!std::isfinite(frag.vel.x) || !std::isfinite(frag.vel.y) || !std::isfinite(frag.vel.z) ||
            !std::isfinite(frag.pos.x) || !std::isfinite(frag.pos.y) || !std::isfinite(frag.pos.z))
            continue;
        frag.mass = frag_mass;
        frag.radius = frag_radius;
        frag.temperature = std::clamp(source_temperature +
                                      base_ejecta * (5.0f + normalized_shock * 6.5f) *
                                      (0.55f + u01(rng) * 0.95f),
                                      40.0f, 30000.0f);
        float type_roll = u01(rng);
        if (gas_source && type_roll < gas_frag_chance) {
            frag.type = CTYPE_NEBULA;
            frag.radius = std::max(frag_radius * 1.35f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 24.0f);
            frag.temperature = std::max(60.0f, frag.temperature * 0.55f);
            frag.atmosphere_retention = 1.0f;
            frag.material_phase = (normalized_shock > 0.95f) ? PHASE_COLLAPSING : PHASE_GAS;
            frag.phase_intensity = std::clamp(0.40f + source_materials.hydrogen * 0.35f + normalized_shock * 0.18f,
                                              0.25f, 1.0f);
            frag.collapse_progress = std::clamp(source_materials.hydrogen * 0.12f + normalized_shock * 0.08f,
                                                0.0f, 0.28f);
        } else if (type_roll < gas_frag_chance + icy_frag_chance && frag.temperature < 520.0f) {
            frag.type = CTYPE_COMET;
            frag.atmosphere_retention = 0.18f;
            frag.material_phase = (frag.temperature < 170.0f) ? PHASE_ICE : PHASE_LIQUID;
            frag.phase_intensity = std::clamp(0.28f + source_materials.water * 0.60f, 0.20f, 1.0f);
        } else {
            frag.type = CTYPE_ASTEROID;
            frag.atmosphere_retention = 0.02f;
            if (molten_bias > 0.28f || frag.temperature > 950.0f) {
                frag.material_phase = (frag.temperature > 2400.0f) ? PHASE_PLASMA : PHASE_MOLTEN;
                frag.phase_intensity = std::clamp(0.24f + molten_bias * 0.65f, 0.18f, 1.0f);
            } else if (source_materials.water > 0.28f && frag.temperature < 170.0f) {
                frag.material_phase = PHASE_ICE;
                frag.phase_intensity = std::clamp(0.22f + source_materials.water * 0.55f, 0.18f, 1.0f);
            } else {
                frag.material_phase = PHASE_SOLID;
                frag.phase_intensity = std::clamp(0.18f + source_materials.silicate * 0.35f +
                                                  source_materials.iron * 0.20f, 0.12f, 0.85f);
            }
        }
        frag.fuel = 0.0f;
        frag.internal_energy = normalized_shock * frag_mass * (12.0f + base_ejecta * 0.9f);
        frag.seed = rng();
        frag.frag_generation = parent_generation + 1;
        frag.angular_vel = (u01(rng) * 2.0f - 1.0f) * 0.01f;
        if (cfg.dynamic_budget_enabled) {
            if (attracting_fragments_now >= max_attracting_fragments) {
                frag.non_attracting = true;
            } else {
                frag.non_attracting = false;
                ++attracting_fragments_now;
            }
        } else {
            frag.non_attracting = false;
        }
        frag.name = generate_body_name(frag.seed, frag.type);
        clear_ring_system(frag);
        clear_impact_signature(frag);
        refresh_body_render_state(frag, &state);

        state.bodies.push_back(frag);
        state.trails.emplace_back();
    }
}

// ── 3D Projection (for overlay) ─────────────────────────────────────────────

CosmosApp::Projected CosmosApp::project(const glm::vec3& world_pos,
                                         const glm::dmat4& vp,
                                         float screen_w, float screen_h) const {
    glm::dvec4 clip = vp * glm::dvec4(world_pos, 1.0);
    if (clip.w <= 0.0)
        return {0, 0, 0, false};

    glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    float sx = (float)((ndc.x * 0.5 + 0.5) * (double)screen_w);
    float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)screen_h);

    bool visible = (ndc.x >= -1.2f && ndc.x <= 1.2f &&
                    ndc.y >= -1.2f && ndc.y <= 1.2f &&
                    ndc.z >= 0.0f && ndc.z <= 1.0f);
    return {sx, sy, (float)clip.w, visible};
}

float CosmosApp::screen_radius(float world_radius, float depth,
                                float fov_rad, float screen_h) const {
    if (depth <= 0.0f) return 0.0f;
    return (world_radius / depth) * (screen_h / (2.0f * std::tan(fov_rad * 0.5f)));
}

#if 0

// ── Overlay rendering (trails + selection on DrawList) ──────────────────────

void CosmosApp::render_overlay() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float aspect = W / H;
    float fov_rad = glm::radians(camera.fov);

    glm::dmat4 view = camera.view_matrix_d();
    glm::dmat4 proj = camera.proj_matrix_d(aspect);
    glm::dmat4 vp = proj * view;

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // Draw trails
    if (cfg.show_trails) {
        for (size_t i = 0; i < state.trails.size() && i < state.bodies.size(); i++) {
            auto& trail = state.trails[i];
            if (trail.size() < 2) continue;

            ImU32 c = body_color(state.bodies[i]);
            int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
            int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
            int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;

            for (size_t j = 1; j < trail.size(); j++) {
                auto p0 = project(trail[j - 1], vp, W, H);
                auto p1 = project(trail[j], vp, W, H);
                if (!p0.visible || !p1.visible) continue;

                float frac = (float)j / (float)trail.size();
                int alpha = (int)(frac * 80.0f);
                float width = 1.0f + frac * 1.5f;
                fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy),
                            IM_COL32(cr, cg, cb, alpha), width);
            }
        }
    }

    // Selection highlight — animated ring
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        const auto& b = state.bodies[selected_body];
        auto p = project(b.pos, vp, W, H);
        if (p.visible) {
            float sr = screen_radius(b.radius, p.depth, fov_rad, H);
            sr = std::max(sr, 6.0f);

            // Animated selection ring
            float pulse = 0.85f + 0.15f * std::sin(sim_time_ * 4.0f);
            float ring_r = (sr + 6.0f) * pulse;

            // Outer glow
            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r + 2.0f,
                IM_COL32(255, 200, 60, 40), 48, 3.0f);
            // Main ring
            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r,
                IM_COL32(255, 220, 100, 200), 48, 2.0f);

            // Corner brackets (like a targeting reticle)
            float bk = ring_r + 8.0f;
            float bl = 8.0f;
            ImU32 bk_col = IM_COL32(255, 255, 255, 120);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk + bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk - bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk + bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk, p.sy + bk - bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk - bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk, p.sy + bk - bl), bk_col, 1.5f);

            // Body name label above
            const char* name = b.name.empty() ? CTYPE_NAMES[std::min(b.type, (uint32_t)CTYPE_COUNT - 1)] : b.name.c_str();
            ImVec2 name_size = ImGui::CalcTextSize(name);
            float label_x = p.sx - name_size.x * 0.5f;
            float label_y = p.sy - ring_r - 20.0f;
            fg->AddRectFilled(ImVec2(label_x - 4, label_y - 2),
                              ImVec2(label_x + name_size.x + 4, label_y + name_size.y + 2),
                              IM_COL32(10, 10, 20, 180), 3.0f);
            fg->AddText(ImVec2(label_x, label_y), IM_COL32(255, 220, 100, 240), name);
        }
    }

    // Focus indicator (when camera is tracking a body)
    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        // Small "TRACKING" label in top-right
        const char* track_label = "TRACKING";
        ImVec2 tl_size = ImGui::CalcTextSize(track_label);
        float tx = W - tl_size.x - 16.0f;
        float ty = 44.0f;
        fg->AddRectFilled(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                          IM_COL32(255, 180, 40, 30), 3.0f);
        fg->AddRect(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                    IM_COL32(255, 180, 40, 120), 3.0f, 0, 1.0f);
        float alpha = 160.0f + 60.0f * std::sin(sim_time_ * 3.0f);
        fg->AddText(ImVec2(tx, ty), IM_COL32(255, 200, 80, (int)alpha), track_label);
    }

    if (cfg.cosmos_space_fabric) {
        char fabric_label[96];
        snprintf(fabric_label, sizeof(fabric_label), "Space fabric: %.1f units per square",
                 cfg.cosmos_space_fabric_grid_size);
        ImVec2 label_size = ImGui::CalcTextSize(fabric_label);
        float px = 16.0f;
        float py = H - label_size.y - 18.0f;
        fg->AddRectFilled(ImVec2(px - 8.0f, py - 4.0f),
                          ImVec2(px + label_size.x + 8.0f, py + label_size.y + 4.0f),
                          IM_COL32(10, 14, 24, 180), 4.0f);
        fg->AddRect(ImVec2(px - 8.0f, py - 4.0f),
                    ImVec2(px + label_size.x + 8.0f, py + label_size.y + 4.0f),
                    IM_COL32(80, 150, 230, 110), 4.0f, 0, 1.0f);
        fg->AddText(ImVec2(px, py), IM_COL32(180, 220, 255, 235), fabric_label);
    }
}

// ── Menu background (animated cosmic particles) ─────────────────────────────

void CosmosApp::draw_menu_background() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // Dark space fill
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 4, 12, 255));

    // Init particles once
    if (!menu_bg_inited_) {
        menu_bg_inited_ = true;
        menu_particles_.resize(60);
        std::mt19937 rng(42);
        auto randf = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        for (auto& p : menu_particles_) {
            p.x = randf(0, W);
            p.y = randf(0, H);
            p.vx = randf(-12, 12);
            p.vy = randf(-12, 12);
            p.radius = randf(1.5f, 4.0f);
            // Cosmic colors: golds, oranges, blues, purples
            int variant = rng() % 4;
            if (variant == 0) { p.r = 1.0f; p.g = 0.7f; p.b = 0.2f; }       // gold
            else if (variant == 1) { p.r = 0.3f; p.g = 0.5f; p.b = 1.0f; }   // blue
            else if (variant == 2) { p.r = 0.6f; p.g = 0.3f; p.b = 0.9f; }   // purple
            else { p.r = 0.2f; p.g = 0.8f; p.b = 0.6f; }                     // teal
            p.alpha = randf(0.4f, 0.8f);
            for (int t = 0; t < 12; t++) { p.trail_x[t] = p.x; p.trail_y[t] = p.y; }
        }
    }

    menu_bg_time_ += io.DeltaTime;

    // Animated nebula glows
    for (int i = 0; i < 3; i++) {
        float phase = menu_bg_time_ * 0.15f + (float)i * 2.1f;
        float cx = W * (0.3f + 0.4f * sinf(phase));
        float cy = H * (0.3f + 0.4f * cosf(phase * 0.7f + 1.0f));
        float glow_r = 200.0f + 50.0f * sinf(phase * 1.3f);
        ImU32 center, edge;
        if (i == 0) { center = IM_COL32(255, 160, 40, 18); edge = IM_COL32(255, 80, 0, 0); }
        else if (i == 1) { center = IM_COL32(60, 100, 200, 14); edge = IM_COL32(30, 50, 180, 0); }
        else { center = IM_COL32(120, 50, 180, 12); edge = IM_COL32(80, 20, 140, 0); }
        draw_radial_glow(bg, cx, cy, glow_r, center, edge);
    }

    // Update + draw particles
    float dt = io.DeltaTime;
    for (auto& p : menu_particles_) {
        // Shift trail
        for (int t = 11; t > 0; t--) { p.trail_x[t] = p.trail_x[t-1]; p.trail_y[t] = p.trail_y[t-1]; }
        p.trail_x[0] = p.x;
        p.trail_y[0] = p.y;

        p.x += p.vx * dt;
        p.y += p.vy * dt;

        // Wrap
        if (p.x < -20) p.x += W + 40;
        if (p.x > W + 20) p.x -= W + 40;
        if (p.y < -20) p.y += H + 40;
        if (p.y > H + 20) p.y -= H + 40;

        // Draw trail
        for (int t = 1; t < 12; t++) {
            float frac = 1.0f - (float)t / 12.0f;
            int alpha = (int)(p.alpha * frac * 40.0f);
            bg->AddLine(ImVec2(p.trail_x[t-1], p.trail_y[t-1]),
                        ImVec2(p.trail_x[t], p.trail_y[t]),
                        IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha),
                        p.radius * frac * 0.6f);
        }

        // Draw particle
        int alpha = (int)(p.alpha * 255.0f);
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha), 12);
    }

    // Force lines between nearby particles
    for (size_t i = 0; i < menu_particles_.size(); i++) {
        for (size_t j = i + 1; j < menu_particles_.size(); j++) {
            float dx = menu_particles_[j].x - menu_particles_[i].x;
            float dy = menu_particles_[j].y - menu_particles_[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 100.0f) {
                float a = (1.0f - d / 100.0f) * 20.0f;
                bg->AddLine(ImVec2(menu_particles_[i].x, menu_particles_[i].y),
                            ImVec2(menu_particles_[j].x, menu_particles_[j].y),
                            IM_COL32(255, 180, 60, (int)a), 0.5f);
            }
        }
    }

    // Vignette
    float vig = std::min(W, H) * 0.6f;
    draw_radial_glow(bg, W * 0.5f, H * 0.5f, std::max(W, H) * 0.8f,
                     IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));

    // Subtle scanlines
    for (float y = 0; y < H; y += 3.0f) {
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(0, 0, 0, 8));
    }
}

// ── Splash screen ───────────────────────────────────────────────────────────

void CosmosApp::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    splash_time_ += io.DeltaTime;

    // Dismiss after 0.3s on any input
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

    // Text overlay window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##CosmosSplash", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Central star glow
        float pulse = 0.7f + 0.3f * sinf(splash_time_ * 2.0f);
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 160.0f * pulse,
                         IM_COL32(255, 200, 60, 40), IM_COL32(255, 120, 0, 0));
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 80.0f * pulse,
                         IM_COL32(255, 240, 180, 60), IM_COL32(255, 200, 60, 0));

        // Orbiting dots
        for (int i = 0; i < 4; i++) {
            float orbit_r = 60.0f + (float)i * 35.0f;
            float speed = 0.8f - (float)i * 0.15f;
            float angle = splash_time_ * speed + (float)i * 1.57f;
            float px = W * 0.5f + cosf(angle) * orbit_r;
            float py = H * 0.4f + sinf(angle) * orbit_r * 0.4f;
            float dot_r = 3.0f + (float)i * 0.5f;
            dl->AddCircleFilled(ImVec2(px, py), dot_r,
                IM_COL32(60 + i * 40, 140 + i * 20, 220, 200), 12);
        }

        // Title — bottom-left
        float title_scale = 2.4f;
        ImGui::SetWindowFontScale(title_scale);

        const char* title1 = "Cosmic ";
        const char* title2 = "Sandbox";
        ImVec2 t1_size = ImGui::CalcTextSize(title1);
        ImVec2 t2_size = ImGui::CalcTextSize(title2);
        float title_x = 60.0f;
        float title_y = H - 120.0f;

        // Glow layers behind title
        for (int layer = 3; layer >= 0; layer--) {
            float offset = (float)layer * 1.5f;
            int alpha = 12 + layer * 6;
            dl->AddText(ImVec2(title_x - offset, title_y - offset),
                IM_COL32(255, 180, 40, alpha), title1);
            dl->AddText(ImVec2(title_x + t1_size.x - offset, title_y - offset),
                IM_COL32(255, 130, 0, alpha), title2);
        }

        // Solid title
        dl->AddText(ImVec2(title_x, title_y), IM_COL32(255, 220, 120, 255), title1);
        dl->AddText(ImVec2(title_x + t1_size.x, title_y), IM_COL32(255, 160, 40, 255), title2);

        // Badge — top right
        ImGui::SetWindowFontScale(1.0f);
        const char* badge = "CELESTIAL SIMULATION";
        ImVec2 badge_size = ImGui::CalcTextSize(badge);
        float badge_x = W - badge_size.x - 40.0f;
        float badge_y = 30.0f;
        float pad = 8.0f;
        dl->AddRect(ImVec2(badge_x - pad, badge_y - pad * 0.5f),
                    ImVec2(badge_x + badge_size.x + pad, badge_y + badge_size.y + pad * 0.5f),
                    IM_COL32(255, 160, 40, 200), 4.0f, 0, 1.5f);
        dl->AddText(ImVec2(badge_x, badge_y), IM_COL32(255, 180, 60, 230), badge);

        // Hint — bottom center, pulsing
        ImGui::SetWindowFontScale(1.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = 120.0f + 80.0f * sinf(splash_time_ * 3.0f);
        dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 40.0f),
                    IM_COL32(200, 200, 210, (int)hint_alpha), hint);

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ── Pause menu ──────────────────────────────────────────────────────────────

void CosmosApp::draw_pause_menu() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;

    // Semi-transparent overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##CosmosPause", nullptr, flags)) {
        float cx = W * 0.5f, cy = H * 0.5f;

        // Title
        ImGui::SetWindowFontScale(2.0f);
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_y = cy - 160.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Glow behind title
        for (int layer = 2; layer >= 0; layer--) {
            float off = (float)layer * 2.0f;
            dl->AddText(ImVec2(cx - title_size.x * 0.5f - off, title_y - off),
                IM_COL32(255, 180, 40, 15 + layer * 10), title);
        }
        dl->AddText(ImVec2(cx - title_size.x * 0.5f, title_y),
            IM_COL32(255, 200, 80, 255), title);
        ImGui::SetWindowFontScale(1.0f);

        // Buttons
        float btn_w = 200.0f, btn_h = 40.0f, btn_spacing = 52.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.18f, 0.22f, 0.35f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.25f, 0.30f, 0.45f, 1.00f));

        // Resume
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            paused = false;
        }

        // New Simulation
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            reset_simulation();
            show_pause_menu = false;
        }

        // Save
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Save Simulation", ImVec2(btn_w, btn_h))) {
            show_save_dialog_ = true;
            show_pause_menu = false;
        }

        // Load
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Load Simulation", ImVec2(btn_w, btn_h))) {
            show_load_dialog_ = true;
            show_pause_menu = false;
        }

        // Empty Simulation
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Empty Simulation", ImVec2(btn_w, btn_h))) {
            state.clear();
            cfg.body_count = 0;
            selected_body = -1;
            sim_time_ = 0.0f;
            show_pause_menu = false;
            paused = false;
        }

        // Return to Launcher
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Return to Launcher", ImVec2(btn_w, btn_h))) {
            request_launcher = true;
            request_quit = true;
        }

        // Quit — red tinted
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(); // FrameRounding

        // Hint
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cx - hint_size.x * 0.5f, H - 60.0f),
            IM_COL32(160, 160, 170, 100), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Spawn menu ──────────────────────────────────────────────────────────────

void CosmosApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGui::SetNextWindowPos({10, 500}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280, 360}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(320, 600));

    if (!ImGui::Begin("Spawn Bodies", &spawn_menu_visible_)) {
        ImGui::End();
        return;
    }

    // Type selection helper lambda
    auto type_button = [&](int t, float btn_w, float default_mass) {
        ImU32 col = CTYPE_COLORS[t];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b_c = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        bool sel = (spawn_type == t);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r*0.5f, g*0.5f, b_c*0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r*0.6f, g*0.6f, b_c*0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r*0.7f, g*0.7f, b_c*0.7f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.9f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r*0.25f, g*0.25f, b_c*0.25f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r*0.4f, g*0.4f, b_c*0.4f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r*0.5f, g*0.5f, b_c*0.5f, 1.0f));
        }
        if (ImGui::Button(CTYPE_NAMES[t], ImVec2(btn_w, 24))) {
            spawn_type = t;
            spawn_mass = default_mass;
        }
        if (sel) { ImGui::PopStyleColor(4); ImGui::PopStyleVar(); }
        else     { ImGui::PopStyleColor(3); }
    };

    if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen)) {
        float bw = 76.0f;
        type_button(CTYPE_PLANET, bw, 1.0f);    ImGui::SameLine();
        type_button(CTYPE_MOON, bw, 0.05f);     ImGui::SameLine();
        type_button(CTYPE_ASTEROID, bw, 0.02f);
        type_button(CTYPE_COMET, bw, 0.01f);    ImGui::SameLine();
        type_button(CTYPE_NEBULA, bw, 0.10f);
    }

    if (ImGui::CollapsingHeader("Stars")) {
        float bw = 76.0f;
        type_button(CTYPE_STAR, bw, 1.0f);
        ImGui::SameLine(); type_button(CTYPE_STAR_O, bw, 30.0f);
        ImGui::SameLine(); type_button(CTYPE_STAR_B, bw, 5.0f);
        type_button(CTYPE_STAR_A, bw, 1.8f);
        ImGui::SameLine(); type_button(CTYPE_STAR_F, bw, 1.2f);
        ImGui::SameLine(); type_button(CTYPE_STAR_G, bw, 1.0f);
        type_button(CTYPE_STAR_K, bw, 0.6f);
        ImGui::SameLine(); type_button(CTYPE_STAR_M, bw, 0.2f);
        ImGui::SameLine(); type_button(CTYPE_STAR_L, bw, 0.06f);
        type_button(CTYPE_STAR_T, bw, 0.04f);
        ImGui::SameLine(); type_button(CTYPE_STAR_Y, bw, 0.02f);
        ImGui::SameLine(); type_button(CTYPE_STAR_WR, bw, 20.0f);
    }

    if (ImGui::CollapsingHeader("Black Holes")) {
        float bw = 120.0f;
        type_button(CTYPE_BLACK_HOLE, bw, 200.0f);
        ImGui::SameLine(); type_button(CTYPE_BH_STELLAR, bw, 10.0f);
        type_button(CTYPE_BH_INTERMEDIATE, bw, 1000.0f);
        ImGui::SameLine(); type_button(CTYPE_BH_SUPERMASSIVE, bw, 1000000.0f);
        type_button(CTYPE_BH_PRIMORDIAL, bw, 0.5f);
    }

    ImGui::Separator();

    // Properties
    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Mass", &spawn_mass, 0.001f, 500.0f, "%.3f",
                            ImGuiSliderFlags_Logarithmic);

        ImGui::Checkbox("Orbital velocity", &spawn_in_orbit_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawn with circular orbital velocity\naround the nearest massive body");
    }

    ImGui::Separator();

    // Spawn button
    {
        ImU32 col = CTYPE_COLORS[spawn_type % CTYPE_COUNT];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.4f, g * 0.4f, b * 0.4f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Middle-click in viewport to place");

        char label[64];
        snprintf(label, sizeof(label), "Spawn %s at Origin", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
        if (ImGui::Button(label, ImVec2(-1, 36))) {
            spawn_at(camera.target);
        }
        ImGui::PopStyleColor(3);
    }

    // Quick spawn presets
    if (ImGui::CollapsingHeader("Quick Presets")) {
        if (ImGui::Button("Add Solar System", ImVec2(-1, 0))) {
            glm::vec3 offset = camera.target;
            CelestialBody s;
            s.pos = offset; s.mass = 100.0f; s.radius = 30.0f;
            s.temperature = 5778.0f; s.type = classify_star_spectral(5778.0f, 100.0f);
            s.seed = 42;
            s.fuel = 0.72f;
            s.angular_vel = (2.0f * 3.14159265359f) / (26.0f * 24.0f * 3600.0f);
            s.luminosity = std::pow(std::max(s.mass, 0.08f), 3.2f) * 0.1f;
            s.name = generate_body_name(s.seed, s.type);
            int star_idx = (int)state.bodies.size();
            state.bodies.push_back(s); state.trails.emplace_back();
            refresh_body_render_state(state.bodies.back(), &state);

            float radii[] = {80, 140, 210, 300};
            float masses[] = {0.3f, 0.8f, 0.5f, 1.5f};
            float temps[] = {600.0f, 300.0f, 180.0f, 90.0f};
            for (int i = 0; i < 4; i++) {
                CelestialBody p;
                float angle = (float)i * 1.57f;
                p.pos = offset + glm::vec3(cosf(angle) * radii[i], 0, sinf(angle) * radii[i]);
                float v = std::sqrt(cfg.G * s.mass / radii[i]);
                p.vel = s.vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                p.mass = masses[i]; p.radius = 6 + masses[i] * 3;
                p.temperature = temps[i];
                p.type = CTYPE_PLANET; p.parent = star_idx;
                p.seed = (uint32_t)(i * 31337 + 54321);
                p.name = generate_body_name(p.seed, p.type);
                refresh_body_render_state(p, &state);
                state.bodies.push_back(p); state.trails.emplace_back();
            }
        }

        if (ImGui::Button("Add Binary Stars", ImVec2(-1, 0))) {
            glm::vec3 center = camera.target;
            float sep = 60.0f;
            float v = std::sqrt(cfg.G * 50.0f / sep);

            CelestialBody s1;
            s1.pos = center + glm::vec3(sep * 0.5f, 0, 0);
            s1.vel = glm::vec3(0, 0, v * 0.5f);
            s1.mass = 50.0f; s1.radius = 22.0f; s1.temperature = 8000.0f;
            s1.type = classify_star_spectral(8000.0f, 50.0f); s1.seed = 111;
            s1.fuel = 0.68f;
            s1.angular_vel = (2.0f * 3.14159265359f) / (38.0f * 3600.0f);
            s1.luminosity = std::pow(std::max(s1.mass, 0.08f), 3.2f) * 0.1f;
            s1.name = generate_body_name(s1.seed, s1.type);
            state.bodies.push_back(s1); state.trails.emplace_back();
            refresh_body_render_state(state.bodies.back(), &state);

            CelestialBody s2;
            s2.pos = center - glm::vec3(sep * 0.5f, 0, 0);
            s2.vel = glm::vec3(0, 0, -v * 0.5f);
            s2.mass = 50.0f; s2.radius = 22.0f; s2.temperature = 3500.0f;
            s2.type = classify_star_spectral(3500.0f, 50.0f); s2.seed = 222;
            s2.fuel = 0.62f;
            s2.angular_vel = (2.0f * 3.14159265359f) / (84.0f * 3600.0f);
            s2.luminosity = std::pow(std::max(s2.mass, 0.08f), 3.2f) * 0.1f;
            s2.name = generate_body_name(s2.seed, s2.type);
            state.bodies.push_back(s2); state.trails.emplace_back();
            refresh_body_render_state(state.bodies.back(), &state);
        }

        if (ImGui::Button("Add Asteroid Belt", ImVec2(-1, 0))) {
            std::mt19937 rng((unsigned)sim_time_);
            auto randf = [&](float lo, float hi) {
                return std::uniform_real_distribution<float>(lo, hi)(rng);
            };
            // Find nearest star for orbital reference
            float nearest_mass = 100.0f;
            glm::vec3 nearest_pos = camera.target;
            glm::vec3 nearest_vel(0);
            for (auto& b : state.bodies) {
                if (is_star_type(b.type)) {
                    float d = glm::length(b.pos - camera.target);
                    if (d < 800.0f) {
                        nearest_mass = b.mass;
                        nearest_pos = b.pos;
                        nearest_vel = b.vel;
                    }
                }
            }
            for (int i = 0; i < 30; i++) {
                CelestialBody a;
                float r = randf(400.0f, 500.0f);
                float angle = randf(0, 6.2832f);
                a.pos = nearest_pos + glm::vec3(cosf(angle) * r, randf(-10, 10), sinf(angle) * r);
                float v = std::sqrt(cfg.G * nearest_mass / r) * randf(0.9f, 1.1f);
                a.vel = nearest_vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                a.mass = randf(0.01f, 0.05f); a.radius = randf(2, 4);
                a.type = CTYPE_ASTEROID;
                a.seed = (uint32_t)(rng());
                a.name = generate_body_name(a.seed, a.type);
                state.bodies.push_back(a); state.trails.emplace_back();
            }
        }
    }

    ImGui::End();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void CosmosApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();

    bool any_overlay = show_splash || show_pause_menu;

    // Animated background for fullscreen overlays
    if (any_overlay)
        draw_menu_background();

    // Splash screen blocks all other UI
    if (show_splash) {
        draw_splash_screen();
        return;
    }

    // Pause menu blocks all other UI
    if (show_pause_menu) {
        draw_pause_menu();
        return;
    }

    // ── Global modern styling ────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.10f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.16f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.16f, 0.26f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.20f, 0.34f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.60f, 0.20f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.75f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.18f, 0.32f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.24f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.34f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.24f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.20f, 0.35f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.25f, 0.40f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.88f, 0.94f, 1.0f));

    // ── Normal UI ────────────────────────────────────────────────────────────

    // Show inspector when a body is selected
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        inspector_visible_ = true;
    }

    // Settings panel
    if (settings_visible_) {
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 760}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Cosmos Settings", &settings_visible_);
    ImGui::SliderFloat("G",          &cfg.G,        0.1f, 10.0f);
    ImGui::SliderFloat("Softening",  &cfg.softening, 1.0f, 50.0f);
    ImGui::Checkbox("Collisions",    &cfg.collisions);
    ImGui::Checkbox("Tidal Forces",  &cfg.tidal_forces);
    ImGui::Checkbox("Show Trails",   &cfg.show_trails);

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 90.0f);
    float log_dist = std::log10(camera.distance);
    if (ImGui::SliderFloat("Distance", &log_dist, 1.0f, 3.7f, "10^%.1f")) {
        camera.distance = std::pow(10.0f, log_dist);
    }
    int trail_len = (int)cfg.trail_length;
    if (ImGui::SliderInt("Trail Length", &trail_len, 0, 500))
        cfg.trail_length = (uint32_t)trail_len;
    if (ImGui::Button("Reset Camera"))
        camera = OrbitCamera{};

    ImGui::Separator();
    ImGui::Text("Collision Physics");
    ImGui::Checkbox("Merging",        &cfg.collision_merging);
    ImGui::Checkbox("Fragmentation",  &cfg.collision_fragmentation);
    ImGui::SliderFloat("Merge Speed",    &cfg.merge_speed_threshold,    1.0f, 20.0f);
    ImGui::SliderFloat("Fragment Speed", &cfg.fragment_speed_threshold, 10.0f, 50.0f);
    ImGui::SliderInt("Fragment Count",   &cfg.fragment_count,           1, 12);
    ImGui::SliderFloat("Min Frag Mass",  &cfg.min_fragment_mass,        0.01f, 1.0f, "%.2f");
    ImGui::SliderInt("Max Frag Depth",   &cfg.max_frag_generation,      0, 5);

    ImGui::Separator();
    ImGui::Text("Thermal");
    ImGui::Checkbox("Temperature", &cfg.temperature_system);
    ImGui::Checkbox("Evaporation", &cfg.evaporation);
    ImGui::Checkbox("Roche Limit", &cfg.roche_limit);
    ImGui::Checkbox("Material Phases", &cfg.material_phases);
    ImGui::Checkbox("Planetary Rings", &cfg.planetary_rings);
    if (cfg.temperature_system)
        ImGui::SliderFloat("Cooling", &cfg.radiative_cooling, 0.0f, 0.01f, "%.4f");

    ImGui::Separator();
    ImGui::Text("Stellar");
    ImGui::Checkbox("Stellar Evolution", &cfg.stellar_evolution);
    if (cfg.stellar_evolution)
        ImGui::SliderFloat("Star Timescale", &cfg.stellar_timescale, 10.0f, 500.0f);

    ImGui::Separator();
    ImGui::Text("Lighting");
    ImGui::Checkbox("Star Lighting",    &cfg.star_lighting);
    ImGui::Checkbox("Uniform Lighting", &cfg.uniform_lighting);
    if (cfg.star_lighting) {
        ImGui::Checkbox("Fast Star Lighting", &cfg.fast_star_lighting);
        ImGui::SliderFloat("Ambient", &cfg.ambient_strength, 0.0f, 0.5f);
    }

    ImGui::Separator();
    ImGui::Text("Cosmos Rendering");
    ImGui::Checkbox("HQ Shading", &cfg.cosmos_hq_shading);
    ImGui::Checkbox("Background Starfield", &cfg.cosmos_background_starfield);
    ImGui::Checkbox("Star Corona", &cfg.cosmos_star_corona);
    ImGui::Checkbox("Comet Tails", &cfg.cosmos_comet_tails);
    ImGui::Checkbox("Black Hole Lensing", &cfg.cosmos_blackhole_lensing);
    ImGui::Checkbox("Space Fabric Grid", &cfg.cosmos_space_fabric);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw a reference plane through the camera focus and warp it by body mass.");
    if (cfg.cosmos_space_fabric) {
        ImGui::SliderFloat("Fabric Square Size", &cfg.cosmos_space_fabric_grid_size,
                           5.0f, 200.0f, "%.1f u", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Fabric Curvature", &cfg.cosmos_space_fabric_strength,
                           0.1f, 3.0f, "%.2f");
        if (ImGui::Button("Snap Fabric View Isometric")) {
            camera.azimuth = glm::radians(45.0f);
            camera.elevation = glm::radians(35.2643897f);
            camera.target_distance = camera.distance;
        }
        ImGui::Text("Each square spans %.1f simulation units.", cfg.cosmos_space_fabric_grid_size);
    }
    ImGui::SliderInt("Cosmos Quality", &cfg.cosmos_quality, 0, 2,
                     cfg.cosmos_quality == 0 ? "Low" :
                     (cfg.cosmos_quality == 1 ? "Balanced" : "High"));

    ImGui::Separator();
    ImGui::Text("Time Control");
    {
        float exp_f = (float)cfg.time_exponent;
        if (ImGui::SliderFloat("Time Rate", &exp_f, -9.0f, 21.0f, "")) {
            cfg.time_exponent = (double)exp_f;
        }
        char rate_buf[64], time_buf[64];
        double rate = std::pow(10.0, cfg.time_exponent);
        format_sim_time(rate, rate_buf, sizeof(rate_buf));
        ImGui::SameLine();
        ImGui::Text("%s/s", rate_buf);

        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        ImGui::Text("Sim Time: %s", time_buf);

        struct Preset { const char* label; double exp; };
        static const Preset presets[] = {
            {"1 s/s",   0.0},   {"1 min/s", 1.778},
            {"1 hr/s",  3.556}, {"1 day/s", 4.937},
            {"1 yr/s",  7.499}, {"1 Myr/s", 13.499},
            {"1 Gyr/s", 16.499},
        };
        for (int i = 0; i < 7; i++) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::SmallButton(presets[i].label))
                cfg.time_exponent = presets[i].exp;
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "General Relativity");
    ImGui::Checkbox("GR Corrections", &cfg.gr_enabled);
    ImGui::Checkbox("Parallel Gravity", &cfg.parallel_gravity);
    if (cfg.gr_enabled) {
        ImGui::SliderFloat("Precession", &cfg.gr_precession_scale, 0.0f, 10.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Perihelion precession (1PN correction)\n1.0 = physical value");
        ImGui::SliderFloat("Time Dilation", &cfg.gr_time_dilation, 0.0f, 5.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gravitational time dilation effect");
        ImGui::SliderFloat("Frame Drag", &cfg.gr_frame_dragging, 0.0f, 5.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lense-Thirring frame dragging\nfrom spinning bodies");
        ImGui::SliderFloat("Speed of Light", &cfg.speed_of_light, 50.0f, 1000.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("c in simulation units\n(lower = stronger GR effects)");
    }

    ImGui::End();
    } // settings_visible_

    // Body Inspector
    draw_inspector();

    // Spawn menu
    draw_spawn_menu();

    // File dialog
    draw_file_dialog();

    // Save status toast
    if (save_status_timer_ > 0.0f) {
        save_status_timer_ -= io.DeltaTime;
        float alpha = std::min(save_status_timer_, 1.0f);
        ImVec2 text_size = ImGui::CalcTextSize(last_save_status_.c_str());
        float tx = io.DisplaySize.x * 0.5f - text_size.x * 0.5f - 12;
        float ty = io.DisplaySize.y * 0.5f - 20;
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(tx - 8, ty - 4), ImVec2(tx + text_size.x + 20, ty + text_size.y + 8),
            IM_COL32(20, 20, 30, (int)(200 * alpha)), 6.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(tx, ty),
            IM_COL32(255, 220, 80, (int)(255 * alpha)), last_save_status_.c_str());
    }

    // Ctrl+S / Ctrl+L hotkeys
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !show_save_dialog_) {
        show_save_dialog_ = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L) && !show_load_dialog_) {
        show_load_dialog_ = true;
    }

    // Body list
    if (body_list_visible_) {
    ImGui::SetNextWindowPos({io.DisplaySize.x - 280.0f, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bodies", &body_list_visible_);
    for (size_t i = 0; i < state.count(); i++) {
        const auto& b = state.bodies[i];
        const char* tn = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "?";
        bool is_sel = ((int)i == selected_body);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.3f, 1));

        char buf[128];
        const char* display_name = b.name.empty() ? tn : b.name.c_str();
        snprintf(buf, sizeof(buf), "%zu. %s (%.1f M)", i, display_name, b.mass);
        if (ImGui::Selectable(buf, is_sel))
            selected_body = (int)i;

        if (is_sel) ImGui::PopStyleColor();
    }
    ImGui::End();
    } // body_list_visible_

    // Bottom bar
    draw_bottom_bar();

    ImGui::PopStyleColor(18);
    ImGui::PopStyleVar(6);
}

// ── Inspector panel ─────────────────────────────────────────────────────────

void CosmosApp::draw_inspector() {
    if (!inspector_visible_) return;
    if (selected_body < 0 || selected_body >= (int)state.bodies.size()) {
        inspector_visible_ = false;
        return;
    }

    auto& b = state.bodies[selected_body];
    const auto& vp = b.cached_visuals;
    MaterialComposition materials = derive_materials(b);
    ComparisonMetrics comparisons = derive_comparisons(b);
    MagneticMetrics magnetic = derive_magnetic_metrics(b, cfg.G);
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 320.0f, 46.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(400, 800));

    if (!ImGui::Begin("Inspector", &inspector_visible_)) {
        ImGui::End();
        return;
    }

    // ── Header: name + type ──
    const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Unknown";
    const char* display_name = b.name.empty() ? type_name : b.name.c_str();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::TextWrapped("%s", display_name);
    ImGui::PopStyleColor();

    if (!b.name.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(%s)", type_name);
    }

    // Focus / Track button
    ImGui::SameLine(ImGui::GetWindowWidth() - 72);
    bool is_tracked = camera.focus_active && camera.focus_body == selected_body;
    if (is_tracked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.9f));
        if (ImGui::SmallButton("Untrack")) camera.release_focus();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::SmallButton("Track")) {
            camera.focus_on(b.pos, selected_body);
            camera.target_distance = b.radius * 8.0f;
        }
    }

    ImGui::Separator();

    // ── Core properties ──
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Properties");

    ImGui::Columns(2, "##props", false);
    ImGui::SetColumnWidth(0, 110);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass");
    ImGui::NextColumn();
    constexpr double SOLAR_MASS_KG = 1.98847e30;
    constexpr double KG_TO_LBS = 2.20462262185;
    double mass_kg = (double)b.mass * SOLAR_MASS_KG;
    double mass_lbs = mass_kg * KG_TO_LBS;
    ImGui::Text("%.3e kg / %.3e lbs", mass_kg, mass_lbs);

    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Radius");
    ImGui::NextColumn();
    constexpr float KM_TO_MILES = 0.6213712f;
    float radius_km = b.radius;
    float radius_miles = radius_km * KM_TO_MILES;
    ImGui::Text("%.1f km / %.1f mi", radius_km, radius_miles);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Temperature");
    ImGui::NextColumn();
    float temp_c = b.temperature - 273.15f;
    float temp_f = temp_c * 9.0f / 5.0f + 32.0f;
    ImGui::Text("%.0f K (%.1f C / %.1f F)", b.temperature, temp_c, temp_f);

    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Material Phase");
    ImGui::NextColumn();
    const char* phase_name = (b.material_phase <= PHASE_COLLAPSING)
        ? MATERIAL_PHASE_NAMES[b.material_phase] : "?";
    if (b.collapse_progress > 0.01f && b.material_phase == PHASE_COLLAPSING)
        ImGui::Text("%s %.0f%%", phase_name, b.collapse_progress * 100.0f);
    else
        ImGui::Text("%s", phase_name);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Speed");
    ImGui::NextColumn();
    constexpr float KMH_TO_MPH = 0.6213712f;
    float speed_kmh = glm::length(b.vel) * SIM_UNIT_TO_KM * 3600.0f;
    float speed_mph = speed_kmh * KMH_TO_MPH;
    ImGui::Text("%.1f km/h / %.1f mph", speed_kmh, speed_mph);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Position");
    ImGui::NextColumn();
    ImGui::Text("%.0f, %.0f, %.0f", b.pos.x, b.pos.y, b.pos.z);
    ImGui::NextColumn();

    if (std::abs(b.angular_vel) > 1e-6f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Spin");
        ImGui::NextColumn();
        ImGui::Text("%.3f rad/s", b.angular_vel);
        ImGui::NextColumn();
    }

    char age_buf[64];
    format_sim_time((double)b.age, age_buf, sizeof(age_buf));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Age");
    ImGui::NextColumn();
    ImGui::Text("%s", age_buf);
    ImGui::NextColumn();

    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Cumulative Properties");
    ImGui::Columns(2, "##cumulative", false);
    ImGui::SetColumnWidth(0, 140);

    float density = body_density(b);
    float volume = body_volume(b);
    float calc_radius = is_star_type(b.type) ? expected_star_radius(b) :
                        ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) ? expected_planet_radius(std::min(b.mass, 0.02f)) : b.radius);
    float surface_g = body_surface_gravity(b, cfg.G);
    float escape_v = body_escape_velocity(b, cfg.G);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Density");
    ImGui::NextColumn();
    ImGui::Text("%.4g M/u^3", density);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Volume");
    ImGui::NextColumn();
    ImGui::Text("%.4g u^3", volume);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Calculated Radius");
    ImGui::NextColumn();
    ImGui::Text("%.2f", calc_radius);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Surface Gravity");
    ImGui::NextColumn();
    ImGui::Text("%.4f", surface_g);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Escape Velocity");
    ImGui::NextColumn();
    ImGui::Text("%.4f", escape_v);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass Loss Rate");
    ImGui::NextColumn();
    ImGui::Text("%.4e M/s", b.mass_loss_rate);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass Loss Total");
    ImGui::NextColumn();
    ImGui::Text("%.4e M", b.mass_loss_total);
    ImGui::NextColumn();

    ImGui::Columns(1);
    if (ImGui::Button("Reset Mass Loss Total", ImVec2(-1, 0)))
        b.mass_loss_total = 0.0f;

    if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Comparisons");
        ImGui::Columns(2, "##comparisons", false);
        ImGui::SetColumnWidth(0, 140);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Earth Similarity");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", comparisons.earth_similarity * 100.0f);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Life Likelihood");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", comparisons.life_likelihood * 100.0f);
        ImGui::NextColumn();

        ImGui::Columns(1);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Composition");
    ImGui::Columns(2, "##materials", false);
    ImGui::SetColumnWidth(0, 140);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Iron");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.iron * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Silicate");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.silicate * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Water");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.water * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Hydrogen");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.hydrogen * 100.0f);
    ImGui::NextColumn();

    ImGui::Columns(1);

    if (magnetic.show_magnetosphere || magnetic.show_magnetic_axis || magnetic.particle_jets ||
        is_star_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Magnetic Fields");
        ImGui::Columns(2, "##magnetic", false);
        ImGui::SetColumnWidth(0, 150);

        if (magnetic.show_magnetosphere) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Show Magnetosphere");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.show_magnetosphere ? "Yes" : "No");
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetosphere Size");
            ImGui::NextColumn();
            ImGui::Text("%.2f", magnetic.magnetosphere_size);
            ImGui::NextColumn();
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetic Field");
        ImGui::NextColumn();
        ImGui::Text("%.3f", magnetic.magnetic_field);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Show Magnetic Axis");
        ImGui::NextColumn();
        ImGui::Text("%s", magnetic.show_magnetic_axis ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetic Pole Angle");
        ImGui::NextColumn();
        ImGui::Text("%.1f deg", magnetic.magnetic_pole_angle);
        ImGui::NextColumn();

        if (magnetic.particle_jets || magnetic.make_pulsar) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Particle Jets");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.particle_jets ? "Yes" : "No");
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Make Pulsar");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.make_pulsar ? "Yes" : "No");
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Orbital info ──
    if (b.parent >= 0 && b.parent < (int)state.bodies.size()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Orbit");

        const auto& par = state.bodies[b.parent];
        const char* par_name = par.name.empty()
            ? CTYPE_NAMES[std::min(par.type, (uint32_t)CTYPE_COUNT - 1)]
            : par.name.c_str();

        ImGui::Columns(2, "##orbit", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Parent");
        ImGui::NextColumn();
        if (ImGui::SmallButton(par_name)) {
            selected_body = b.parent;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to select parent");
        ImGui::NextColumn();

        float orb_dist = glm::length(b.pos - par.pos);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Distance");
        ImGui::NextColumn();
        ImGui::Text("%.1f", orb_dist);
        ImGui::NextColumn();

        if (orb_dist > 0.1f) {
            float orb_v = std::sqrt(cfg.G * par.mass / orb_dist);
            float period = 2.0f * 3.14159f * orb_dist / std::max(orb_v, 0.01f);
            char period_buf[64];
            format_sim_time((double)period, period_buf, sizeof(period_buf));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Period");
            ImGui::NextColumn();
            ImGui::Text("%s", period_buf);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Star info ──
    if (is_star_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "Stellar");

        static const char* STAGE_NAMES[] = {
            "Main Sequence", "Subgiant", "Red Giant", "Horizontal Branch",
            "AGB", "Supergiant", "Hypergiant", "White Dwarf", "Neutron Star"
        };

        ImGui::Columns(2, "##star", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Stage");
        ImGui::NextColumn();
        const char* stage = (b.stellar_stage < SSTAGE_COUNT) ? STAGE_NAMES[b.stellar_stage] : "?";
        ImGui::Text("%s", stage);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Fuel");
        ImGui::NextColumn();
        ImGui::ProgressBar(b.fuel, ImVec2(-1, 14));
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Luminosity");
        ImGui::NextColumn();
        ImGui::Text("%.2f L", b.luminosity);
        ImGui::NextColumn();

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Corona");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.corona_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Flares");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.flare_activity);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Granulation");
            ImGui::NextColumn();
            ImGui::Text("%.2f @ %.1f", vp.terrain_amp, vp.terrain_freq);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Black hole info ──
    if (is_black_hole_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Black Hole");

        float rs = 2.0f * cfg.G * b.mass / (cfg.speed_of_light * cfg.speed_of_light);

        ImGui::Columns(2, "##bh", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Schwarzschild r");
        ImGui::NextColumn();
        ImGui::Text("%.4f", rs);
        ImGui::NextColumn();

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Lensing");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.lensing_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Accretion");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.accretion_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Jet Strength");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.jet_strength);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Planet / Moon properties ──
    if ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) && b.props_valid) {
        const auto& pp = b.cached_props;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Surface & Atmosphere");

        static const char* SURF_NAMES[] = {"Rocky", "Liquid", "Frozen", "Gas Giant", "Mixed"};
        static const char* OCEAN_NAMES[] = {"None", "Water", "Methane", "Ammonia", "Lava"};
        static const char* WEATHER_NAMES[] = {"None", "Storms", "Rain", "Snow", "Dust"};

        ImGui::Columns(2, "##planet", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Surface");
        ImGui::NextColumn();
        ImGui::Text("%s", SURF_NAMES[pp.surface]);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Planet Class");
        ImGui::NextColumn();
        ImGui::Text("%s", PLANET_CLASS_NAMES[pp.planet_class]);
        ImGui::NextColumn();

        // Atmosphere
        if (pp.atmosphere.pressure > 0.01f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Atm Pressure");
            ImGui::NextColumn();
            ImGui::Text("%.2f atm", pp.atmosphere.pressure);
            ImGui::NextColumn();

            // Show dominant gas
            float max_frac = 0;
            const char* dom_gas = "N2";
            struct GasEntry { float frac; const char* name; };
            GasEntry gases[] = {
                {pp.atmosphere.n2_frac, "N2"},   {pp.atmosphere.o2_frac, "O2"},
                {pp.atmosphere.co2_frac, "CO2"}, {pp.atmosphere.h2_frac, "H2"},
                {pp.atmosphere.he_frac, "He"},   {pp.atmosphere.ch4_frac, "CH4"},
                {pp.atmosphere.nh3_frac, "NH3"},
            };
            for (auto& g : gases) {
                if (g.frac > max_frac) { max_frac = g.frac; dom_gas = g.name; }
            }

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Composition");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", dom_gas, max_frac * 100.0f);
            // Show secondary gas if significant
            float second_max = 0;
            const char* second_gas = "";
            for (auto& g : gases) {
                if (g.frac > second_max && g.name != dom_gas) {
                    second_max = g.frac; second_gas = g.name;
                }
            }
            if (second_max > 0.05f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), "%s %.0f%%",
                                   second_gas, second_max * 100.0f);
            }
            ImGui::NextColumn();

            if (pp.atmosphere.has_clouds) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Clouds");
                ImGui::NextColumn();
                ImGui::Text("%.0f%%", pp.cloud_coverage);
                ImGui::NextColumn();
            }
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Atmosphere Health");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 100.0f);
        ImGui::NextColumn();

        if (b.ring_density > 0.01f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Rings");
            ImGui::NextColumn();
            ImGui::Text("%.2f - %.2f / %.0f%%", b.ring_inner_radius, b.ring_outer_radius, b.ring_density * 100.0f);
            ImGui::NextColumn();
        }

        // Ocean
        if (pp.ocean_type != OCEAN_NONE) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", OCEAN_NAMES[pp.ocean_type], pp.ocean_coverage);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean Depth");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.ocean_depth);
            ImGui::NextColumn();
        }

        // Terrain
        if (pp.has_mountains) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mountains");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.mountain_height);
            ImGui::NextColumn();
        }
        if (pp.has_continents) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Continents");
            ImGui::NextColumn();
            ImGui::Text("%d / %.0f%%", pp.continent_count, pp.continent_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_islands) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Islands");
            ImGui::NextColumn();
            ImGui::Text("%.0f%%", pp.island_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_rivers) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Rivers");
            ImGui::NextColumn();
            ImGui::Text("%.0f%% density", pp.river_density * 100.0f);
            ImGui::NextColumn();
        }
        if (pp.has_ice_sheets) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ice Sheets");
            ImGui::NextColumn();
            ImGui::Text("%.0f%%", pp.ice_sheet_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_iron_core) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Iron Core");
            ImGui::NextColumn();
            ImGui::Text("Yes");
            ImGui::NextColumn();
        }

        // Weather
        if (pp.has_weather) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Weather");
            ImGui::NextColumn();
            ImGui::Text("%s", WEATHER_NAMES[pp.weather_type]);
            ImGui::NextColumn();
        }

        // Vegetation
        if (pp.vegetation_coverage > 1.0f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Vegetation");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%.0f%%",
                               pp.vegetation_coverage);
            ImGui::NextColumn();
        }

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Roughness");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.roughness);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Haze");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.haze_density);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Cratering");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.crater_density);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Weather FX");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.weather_strength);
            ImGui::NextColumn();

            if (vp.volcanic_activity > 0.01f) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Volcanism");
                ImGui::NextColumn();
                ImGui::Text("%.2f", vp.volcanic_activity);
                ImGui::NextColumn();
            }
        }

        ImGui::Columns(1);
    }

    if ((b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET) && b.visuals_valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.80f, 0.72f, 1.0f), "Small Body Visuals");

        ImGui::Columns(2, "##smallbody", false);
        ImGui::SetColumnWidth(0, 110);

        const char* small_body_class = "Icy";
        if (b.type == CTYPE_ASTEROID) {
            switch ((SmallBodyClass)vp.subtype) {
            case SMALLBODY_C: small_body_class = "Carbonaceous"; break;
            case SMALLBODY_S: small_body_class = "Silicate"; break;
            case SMALLBODY_M: small_body_class = "Metallic"; break;
            case SMALLBODY_ICY: small_body_class = "Icy"; break;
            }
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Class");
        ImGui::NextColumn();
        ImGui::Text("%s", b.type == CTYPE_COMET ? "Cometary Ice" : small_body_class);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ice / Metal");
        ImGui::NextColumn();
        ImGui::Text("%.0f%% / %.0f%%", vp.ice_frac * 100.0f, vp.metal_frac * 100.0f);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Cratering");
        ImGui::NextColumn();
        ImGui::Text("%.2f", vp.crater_density);
        ImGui::NextColumn();

        if (b.type == CTYPE_COMET) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Coma / Tail");
            ImGui::NextColumn();
            ImGui::Text("%.2f / %.2f", vp.coma_strength, vp.tail_strength);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Actions ──
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Body", ImVec2(-1, 0))) {
        b.marked_for_removal = true;
        if (camera.focus_body == selected_body) camera.release_focus();
        selected_body = -1;
        inspector_visible_ = false;
    }

    ImGui::End();
}

// ── Bottom bar ──────────────────────────────────────────────────────────────

void CosmosApp::draw_bottom_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 36.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;
    float dt = io.DeltaTime;

    // Auto-hide animation
    bool mouse_near_bottom = (io.MousePos.y > display_h - 8.0f);
    float current_bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);
    bool mouse_over_bar = (io.MousePos.y > current_bar_y && bottom_bar_offset_ < 0.5f);
    bool keep_visible = show_menu_popup_ || show_pause_menu;
    float target = (mouse_near_bottom || mouse_over_bar || keep_visible) ? 0.0f : 1.0f;
    bottom_bar_offset_ += (target - bottom_bar_offset_) * std::min(1.0f, 8.0f * dt);
    if (bottom_bar_offset_ < 0.005f) bottom_bar_offset_ = 0.0f;
    if (bottom_bar_offset_ > 0.995f) bottom_bar_offset_ = 1.0f;

    float bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);

    ImGui::SetNextWindowPos(ImVec2(0, bar_y));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.04f, 0.02f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    if (ImGui::Begin("##CosmosBottomBar", nullptr, bar_flags)) {
        // Left: Menu button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.15f, 0.05f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.22f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.30f, 0.10f, 1.0f));
        if (ImGui::Button("Menu", ImVec2(70, 24))) {
            show_menu_popup_ = !show_menu_popup_;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.4f, 0.35f, 0.2f, 0.5f), "|");
        ImGui::SameLine(0, 8);

        // Taskbar entries
        struct TBEntry { const char* label; bool* visible; };
        TBEntry entries[] = {
            {"Settings",  &settings_visible_},
            {"Spawn",     &spawn_menu_visible_},
            {"Bodies",    &body_list_visible_},
            {"Inspector", &inspector_visible_},
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        for (int i = 0; i < 4; i++) {
            bool vis = *entries[i].visible;
            if (vis) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.14f, 0.05f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.20f, 0.08f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.16f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.06f, 0.02f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.10f, 0.04f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.08f, 0.03f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.5f, 0.3f, 0.8f));
            }

            char btn_id[64];
            snprintf(btn_id, sizeof(btn_id), "%s###CTB_%d", entries[i].label, i);
            if (ImGui::Button(btn_id, ImVec2(0, 22))) {
                *entries[i].visible = !(*entries[i].visible);
            }

            if (vis) {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(rmin.x + 2, rmax.y - 2), ImVec2(rmax.x - 2, rmax.y),
                    IM_COL32(255, 200, 60, 200));
            }

            ImGui::PopStyleColor(4);
            ImGui::SameLine(0, 4);
        }
        ImGui::PopStyleVar(2);

        // Right: sim time + rate
        char time_buf[64], rate_buf[64];
        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        format_sim_time(std::pow(10.0, cfg.time_exponent), rate_buf, sizeof(rate_buf));

        char right_text[256];
        snprintf(right_text, sizeof(right_text), "T: %s  |  %s/s  |  %zu bodies",
                 time_buf, rate_buf, state.count());
        ImVec2 text_size = ImGui::CalcTextSize(right_text);
        ImGui::SameLine(display_w - text_size.x - 16.0f);
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "%s", right_text);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Menu popup
    if (show_menu_popup_) {
        float popup_w = 200.0f;
        float popup_h = 380.0f;
        float popup_x = 12.0f;
        float popup_y = std::max(10.0f, bar_y - popup_h - 4.0f);
        ImGui::SetNextWindowPos(ImVec2(popup_x, popup_y));
        ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h));

        ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.06f, 0.02f, 0.95f));

        if (ImGui::Begin("##CosmosMenuPopup", &show_menu_popup_, popup_flags)) {
            if (ImGui::TreeNodeEx("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::MenuItem(paused ? "Resume (Space)" : "Pause (Space)")) {
                    paused = !paused; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("New Simulation")) {
                    reset_simulation(); show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Empty Universe")) {
                    state.clear(); cfg.body_count = 0;
                    selected_body = -1; sim_time_ = 0.0f;
                    cfg.sim_time_accumulated = 0.0;
                    show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save (Ctrl+S)")) {
                    show_save_dialog_ = true; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Load (Ctrl+L)")) {
                    show_load_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Body...")) {
                    show_import_dialog_ = true; show_menu_popup_ = false;
                }
                if (selected_body >= 0 && ImGui::MenuItem("Export Selected Body...")) {
                    show_export_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("View", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool tmp;
                tmp = settings_visible_;
                if (ImGui::MenuItem("Settings", nullptr, tmp)) { settings_visible_ = !settings_visible_; show_menu_popup_ = false; }
                tmp = spawn_menu_visible_;
                if (ImGui::MenuItem("Spawn Menu", nullptr, tmp)) { spawn_menu_visible_ = !spawn_menu_visible_; show_menu_popup_ = false; }
                tmp = body_list_visible_;
                if (ImGui::MenuItem("Body List", nullptr, tmp)) { body_list_visible_ = !body_list_visible_; show_menu_popup_ = false; }
                tmp = inspector_visible_;
                if (ImGui::MenuItem("Inspector", nullptr, tmp)) { inspector_visible_ = !inspector_visible_; show_menu_popup_ = false; }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Navigation")) {
                if (ImGui::MenuItem("Return to Launcher")) { request_launcher = true; request_quit = true; }
                if (ImGui::MenuItem("Quit")) { request_quit = true; }
                ImGui::TreePop();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

// ── Save / Load ─────────────────────────────────────────────────────────────

static constexpr uint32_t COSMOS_MAGIC   = 0x534D4F43; // "COSM"
static constexpr uint32_t COSMOS_VERSION = 4;

// POD struct for binary serialization of one body (fixed-size fields only)
#pragma pack(push, 1)
struct BodyPODV1 {
    float pos[3];
    float vel[3];
    float mass;
    float radius;
    float temperature;
    uint32_t type;
    int32_t parent;
    float age;
    float internal_energy;
    float luminosity;
    float fuel;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len; // followed by name_len bytes of name
};

struct BodyPODV2 {
    float pos[3];
    float vel[3];
    float mass;
    float radius;
    float temperature;
    uint32_t type;
    int32_t parent;
    float age;
    float internal_energy;
    float luminosity;
    float fuel;
    float atmosphere_retention;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len; // followed by name_len bytes of name
};

struct BodyPODV3 {
    float pos[3];
    float vel[3];
    float mass;
    float radius;
    float temperature;
    uint32_t type;
    int32_t parent;
    float age;
    float internal_energy;
    float luminosity;
    float fuel;
    float atmosphere_retention;
    float phase_intensity;
    float collapse_progress;
    float ring_inner_radius;
    float ring_outer_radius;
    float ring_density;
    float ring_ice_fraction;
    float ring_tilt;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t material_phase;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len; // followed by name_len bytes of name
};

struct BodyPOD {
    float pos[3];
    float vel[3];
    float mass;
    float radius;
    float temperature;
    uint32_t type;
    int32_t parent;
    float age;
    float internal_energy;
    float luminosity;
    float fuel;
    float atmosphere_retention;
    float phase_intensity;
    float collapse_progress;
    float ring_inner_radius;
    float ring_outer_radius;
    float ring_density;
    float ring_ice_fraction;
    float ring_tilt;
    float impact_normal[3];
    float impact_crater_strength;
    float impact_heat;
    float impact_radius;
    float impact_ejecta;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t material_phase;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len; // followed by name_len bytes of name
};
#pragma pack(pop)

bool CosmosApp::save_simulation(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // Header
    f.write(reinterpret_cast<const char*>(&COSMOS_MAGIC), 4);
    f.write(reinterpret_cast<const char*>(&COSMOS_VERSION), 4);

    // Config (selected POD fields)
    f.write(reinterpret_cast<const char*>(&cfg.G), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.time_exponent), sizeof(double));
    f.write(reinterpret_cast<const char*>(&cfg.sim_time_accumulated), sizeof(double));
    f.write(reinterpret_cast<const char*>(&cfg.softening), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.damping), sizeof(float));

    uint32_t flags = 0;
    if (cfg.collisions) flags |= 1;
    if (cfg.tidal_forces) flags |= 2;
    if (cfg.collision_merging) flags |= 4;
    if (cfg.collision_fragmentation) flags |= 8;
    if (cfg.roche_limit) flags |= 16;
    if (cfg.temperature_system) flags |= 32;
    if (cfg.evaporation) flags |= 64;
    if (cfg.stellar_evolution) flags |= 128;
    if (cfg.star_lighting) flags |= 256;
    if (cfg.uniform_lighting) flags |= 512;
    if (cfg.parallel_gravity) flags |= 1024;
    if (cfg.material_phases) flags |= 2048;
    if (cfg.planetary_rings) flags |= 4096;
    f.write(reinterpret_cast<const char*>(&flags), sizeof(uint32_t));

    f.write(reinterpret_cast<const char*>(&cfg.merge_speed_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.fragment_speed_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.fragment_count), sizeof(int));
    f.write(reinterpret_cast<const char*>(&cfg.radiative_cooling), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.collision_heating), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.evaporation_rate), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.stellar_timescale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ambient_strength), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.min_fragment_mass), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.max_frag_generation), sizeof(int));

    // Body count
    uint32_t body_count = (uint32_t)state.bodies.size();
    f.write(reinterpret_cast<const char*>(&body_count), 4);

    // Bodies
    for (const auto& b : state.bodies) {
        BodyPOD pod{};
        pod.pos[0] = b.pos.x; pod.pos[1] = b.pos.y; pod.pos[2] = b.pos.z;
        pod.vel[0] = b.vel.x; pod.vel[1] = b.vel.y; pod.vel[2] = b.vel.z;
        pod.mass = b.mass;
        pod.radius = b.radius;
        pod.temperature = b.temperature;
        pod.type = b.type;
        pod.parent = b.parent;
        pod.age = b.age;
        pod.internal_energy = b.internal_energy;
        pod.luminosity = b.luminosity;
        pod.fuel = b.fuel;
        pod.atmosphere_retention = b.atmosphere_retention;
        pod.phase_intensity = b.phase_intensity;
        pod.collapse_progress = b.collapse_progress;
        pod.ring_inner_radius = b.ring_inner_radius;
        pod.ring_outer_radius = b.ring_outer_radius;
        pod.ring_density = b.ring_density;
        pod.ring_ice_fraction = b.ring_ice_fraction;
        pod.ring_tilt = b.ring_tilt;
        pod.impact_normal[0] = b.impact_normal.x;
        pod.impact_normal[1] = b.impact_normal.y;
        pod.impact_normal[2] = b.impact_normal.z;
        pod.impact_crater_strength = b.impact_crater_strength;
        pod.impact_heat = b.impact_heat;
        pod.impact_radius = b.impact_radius;
        pod.impact_ejecta = b.impact_ejecta;
        pod.angular_vel = b.angular_vel;
        pod.stellar_stage = b.stellar_stage;
        pod.material_phase = b.material_phase;
        pod.seed = b.seed;
        pod.frag_generation = b.frag_generation;
        pod.name_len = (uint32_t)b.name.size();
        f.write(reinterpret_cast<const char*>(&pod), sizeof(BodyPOD));
        if (pod.name_len > 0)
            f.write(b.name.data(), pod.name_len);
    }

    // Camera state
    f.write(reinterpret_cast<const char*>(&camera.azimuth), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.elevation), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.distance), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.fov), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.target), sizeof(glm::vec3));

    return f.good();
}

bool CosmosApp::load_simulation(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (magic != COSMOS_MAGIC || version > COSMOS_VERSION) return false;

    // Config
    f.read(reinterpret_cast<char*>(&cfg.G), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.time_exponent), sizeof(double));
    f.read(reinterpret_cast<char*>(&cfg.sim_time_accumulated), sizeof(double));
    f.read(reinterpret_cast<char*>(&cfg.softening), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.damping), sizeof(float));

    uint32_t flags = 0;
    f.read(reinterpret_cast<char*>(&flags), sizeof(uint32_t));
    cfg.collisions              = (flags & 1) != 0;
    cfg.tidal_forces            = (flags & 2) != 0;
    cfg.collision_merging       = (flags & 4) != 0;
    cfg.collision_fragmentation = (flags & 8) != 0;
    cfg.roche_limit             = (flags & 16) != 0;
    cfg.temperature_system      = (flags & 32) != 0;
    cfg.evaporation             = (flags & 64) != 0;
    cfg.stellar_evolution       = (flags & 128) != 0;
    cfg.star_lighting           = (flags & 256) != 0;
    cfg.uniform_lighting        = (flags & 512) != 0;
    cfg.parallel_gravity        = (flags & 1024) != 0;
    cfg.material_phases         = (flags & 2048) != 0;
    cfg.planetary_rings         = (flags & 4096) != 0;
    if (version < 3) {
        cfg.material_phases = true;
        cfg.planetary_rings = true;
    }

    f.read(reinterpret_cast<char*>(&cfg.merge_speed_threshold), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.fragment_speed_threshold), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.fragment_count), sizeof(int));
    f.read(reinterpret_cast<char*>(&cfg.radiative_cooling), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.collision_heating), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.evaporation_rate), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.stellar_timescale), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.ambient_strength), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.min_fragment_mass), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.max_frag_generation), sizeof(int));

    // Body count
    uint32_t body_count = 0;
    f.read(reinterpret_cast<char*>(&body_count), 4);
    if (body_count > 10000) return false; // sanity check

    state.clear();
    state.bodies.reserve(body_count);

    for (uint32_t i = 0; i < body_count; i++) {
        CelestialBody b;
        uint32_t name_len = 0;
        if (version >= 4) {
            BodyPOD pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPOD));
            b.pos = {pod.pos[0], pod.pos[1], pod.pos[2]};
            b.vel = {pod.vel[0], pod.vel[1], pod.vel[2]};
            b.mass = pod.mass;
            b.radius = pod.radius;
            b.temperature = pod.temperature;
            b.type = pod.type;
            b.parent = pod.parent;
            b.age = pod.age;
            b.internal_energy = pod.internal_energy;
            b.luminosity = pod.luminosity;
            b.fuel = pod.fuel;
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = pod.phase_intensity;
            b.collapse_progress = pod.collapse_progress;
            b.ring_inner_radius = pod.ring_inner_radius;
            b.ring_outer_radius = pod.ring_outer_radius;
            b.ring_density = pod.ring_density;
            b.ring_ice_fraction = pod.ring_ice_fraction;
            b.ring_tilt = pod.ring_tilt;
            b.impact_normal = {pod.impact_normal[0], pod.impact_normal[1], pod.impact_normal[2]};
            b.impact_crater_strength = pod.impact_crater_strength;
            b.impact_heat = pod.impact_heat;
            b.impact_radius = pod.impact_radius;
            b.impact_ejecta = pod.impact_ejecta;
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = pod.material_phase;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else if (version >= 3) {
            BodyPODV3 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV3));
            b.pos = {pod.pos[0], pod.pos[1], pod.pos[2]};
            b.vel = {pod.vel[0], pod.vel[1], pod.vel[2]};
            b.mass = pod.mass;
            b.radius = pod.radius;
            b.temperature = pod.temperature;
            b.type = pod.type;
            b.parent = pod.parent;
            b.age = pod.age;
            b.internal_energy = pod.internal_energy;
            b.luminosity = pod.luminosity;
            b.fuel = pod.fuel;
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = pod.phase_intensity;
            b.collapse_progress = pod.collapse_progress;
            b.ring_inner_radius = pod.ring_inner_radius;
            b.ring_outer_radius = pod.ring_outer_radius;
            b.ring_density = pod.ring_density;
            b.ring_ice_fraction = pod.ring_ice_fraction;
            b.ring_tilt = pod.ring_tilt;
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = pod.material_phase;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else if (version >= 2) {
            BodyPODV2 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV2));
            b.pos = {pod.pos[0], pod.pos[1], pod.pos[2]};
            b.vel = {pod.vel[0], pod.vel[1], pod.vel[2]};
            b.mass = pod.mass;
            b.radius = pod.radius;
            b.temperature = pod.temperature;
            b.type = pod.type;
            b.parent = pod.parent;
            b.age = pod.age;
            b.internal_energy = pod.internal_energy;
            b.luminosity = pod.luminosity;
            b.fuel = pod.fuel;
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = 0.0f;
            b.collapse_progress = 0.0f;
            clear_ring_system(b);
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = PHASE_SOLID;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else {
            BodyPODV1 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV1));
            b.pos = {pod.pos[0], pod.pos[1], pod.pos[2]};
            b.vel = {pod.vel[0], pod.vel[1], pod.vel[2]};
            b.mass = pod.mass;
            b.radius = pod.radius;
            b.temperature = pod.temperature;
            b.type = pod.type;
            b.parent = pod.parent;
            b.age = pod.age;
            b.internal_energy = pod.internal_energy;
            b.luminosity = pod.luminosity;
            b.fuel = pod.fuel;
            b.atmosphere_retention = 1.0f;
            b.phase_intensity = 0.0f;
            b.collapse_progress = 0.0f;
            clear_ring_system(b);
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = PHASE_SOLID;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        }
        if (name_len > 0 && name_len < 256) {
            b.name.resize(name_len);
            f.read(b.name.data(), name_len);
        }
        state.bodies.push_back(std::move(b));
        state.trails.emplace_back();
    }

    // Camera state
    f.read(reinterpret_cast<char*>(&camera.azimuth), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.elevation), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.distance), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.fov), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.target), sizeof(glm::vec3));

    cfg.body_count = (uint32_t)state.bodies.size();
    selected_body = -1;
    sim_time_ = 0.0f;

    // Refresh cached planet properties for all loaded bodies
    for (auto& b : state.bodies) refresh_body_render_state(b, &state);

    return f.good();
}

// ── Import / Export individual bodies ────────────────────────────────────────
// Uses a simple text format (.csbody) for portability:
//   name, type, mass, radius, temperature, seed, pos(x,y,z), vel(x,y,z), fuel, etc.

bool CosmosApp::export_body(int index, const std::string& path) {
    if (index < 0 || index >= (int)state.bodies.size()) return false;
    const auto& b = state.bodies[index];

    std::ofstream f(path);
    if (!f) return false;

    f << "CSBODY 3\n";
    f << "name " << (b.name.empty() ? "Unnamed" : b.name) << "\n";
    f << "type " << b.type << "\n";
    f << "mass " << b.mass << "\n";
    f << "radius " << b.radius << "\n";
    f << "temperature " << b.temperature << "\n";
    f << "seed " << b.seed << "\n";
    f << "pos " << b.pos.x << " " << b.pos.y << " " << b.pos.z << "\n";
    f << "vel " << b.vel.x << " " << b.vel.y << " " << b.vel.z << "\n";
    f << "fuel " << b.fuel << "\n";
    f << "age " << b.age << "\n";
    f << "luminosity " << b.luminosity << "\n";
    f << "internal_energy " << b.internal_energy << "\n";
    f << "atmosphere_retention " << b.atmosphere_retention << "\n";
    f << "material_phase " << b.material_phase << "\n";
    f << "phase_intensity " << b.phase_intensity << "\n";
    f << "collapse_progress " << b.collapse_progress << "\n";
    f << "ring " << b.ring_inner_radius << " " << b.ring_outer_radius << " "
      << b.ring_density << " " << b.ring_ice_fraction << " " << b.ring_tilt << "\n";
    f << "impact_normal " << b.impact_normal.x << " " << b.impact_normal.y << " " << b.impact_normal.z << "\n";
    f << "impact_state " << b.impact_crater_strength << " " << b.impact_heat << " "
      << b.impact_radius << " " << b.impact_ejecta << "\n";
    f << "angular_vel " << b.angular_vel << "\n";
    f << "stellar_stage " << b.stellar_stage << "\n";
    f << "parent " << b.parent << "\n";
    f << "frag_generation " << b.frag_generation << "\n";

    return f.good();
}

bool CosmosApp::import_body(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string header;
    int version = 0;
    f >> header >> version;
    if (header != "CSBODY" || version < 1) return false;

    CelestialBody b;
    std::string key;
    while (f >> key) {
        if (key == "name") {
            std::getline(f >> std::ws, b.name);
        } else if (key == "type") { f >> b.type; }
        else if (key == "mass") { f >> b.mass; }
        else if (key == "radius") { f >> b.radius; }
        else if (key == "temperature") { f >> b.temperature; }
        else if (key == "seed") { f >> b.seed; }
        else if (key == "pos") { f >> b.pos.x >> b.pos.y >> b.pos.z; }
        else if (key == "vel") { f >> b.vel.x >> b.vel.y >> b.vel.z; }
        else if (key == "fuel") { f >> b.fuel; }
        else if (key == "age") { f >> b.age; }
        else if (key == "luminosity") { f >> b.luminosity; }
        else if (key == "internal_energy") { f >> b.internal_energy; }
        else if (key == "atmosphere_retention") { f >> b.atmosphere_retention; }
        else if (key == "material_phase") { f >> b.material_phase; }
        else if (key == "phase_intensity") { f >> b.phase_intensity; }
        else if (key == "collapse_progress") { f >> b.collapse_progress; }
        else if (key == "ring") {
            f >> b.ring_inner_radius >> b.ring_outer_radius >> b.ring_density
              >> b.ring_ice_fraction >> b.ring_tilt;
        }
        else if (key == "impact_normal") { f >> b.impact_normal.x >> b.impact_normal.y >> b.impact_normal.z; }
        else if (key == "impact_state") {
            f >> b.impact_crater_strength >> b.impact_heat >> b.impact_radius >> b.impact_ejecta;
        }
        else if (key == "angular_vel") { f >> b.angular_vel; }
        else if (key == "stellar_stage") { f >> b.stellar_stage; }
        else if (key == "parent") { f >> b.parent; }
        else if (key == "frag_generation") { f >> b.frag_generation; }
    }

    if (b.name == "Unnamed") b.name.clear();
    refresh_body_render_state(b, &state);
    state.bodies.push_back(std::move(b));
    state.trails.emplace_back();
    return true;
}

// ── File dialog ─────────────────────────────────────────────────────────────

void CosmosApp::draw_file_dialog() {
    bool any_dialog = show_save_dialog_ || show_load_dialog_ ||
                      show_export_dialog_ || show_import_dialog_;
    if (!any_dialog) return;

    const char* title = show_save_dialog_   ? "Save Simulation" :
                        show_load_dialog_   ? "Load Simulation" :
                        show_export_dialog_ ? "Export Body" :
                                              "Import Body";

    const char* extension = (show_save_dialog_ || show_load_dialog_) ? ".cssim" : ".csbody";

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 200, io.DisplaySize.y * 0.5f - 100),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("File path (%s):", extension);
        ImGui::InputText("##FilePath", file_path_buf_, sizeof(file_path_buf_));

        // Default path suggestion
        if (file_path_buf_[0] == '\0') {
            std::string def = get_data_dir();
            if (show_save_dialog_ || show_load_dialog_)
                def += "cosmos_save" + std::string(extension);
            else if (show_export_dialog_ && selected_body >= 0 &&
                     selected_body < (int)state.bodies.size())
                def += state.bodies[selected_body].name + extension;
            else
                def += "body" + std::string(extension);
            strncpy(file_path_buf_, def.c_str(), sizeof(file_path_buf_) - 1);
        }

        // List existing files in data dir
        if (show_load_dialog_ || show_import_dialog_) {
            ImGui::Separator();
            ImGui::Text("Existing files:");
            std::error_code ec;
            std::string dir = get_data_dir();
            if (std::filesystem::exists(dir, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    std::string fn = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    if (ext == extension) {
                        if (ImGui::Selectable(fn.c_str())) {
                            strncpy(file_path_buf_, entry.path().string().c_str(),
                                    sizeof(file_path_buf_) - 1);
                        }
                    }
                }
            }
        }

        ImGui::Separator();

        const char* action_label = (show_save_dialog_ || show_export_dialog_) ? "Save" : "Load";
        if (ImGui::Button(action_label, ImVec2(120, 30))) {
            bool ok = false;
            if (show_save_dialog_) {
                ok = save_simulation(file_path_buf_);
                last_save_status_ = ok ? "Saved successfully" : "Save failed";
            } else if (show_load_dialog_) {
                ok = load_simulation(file_path_buf_);
                last_save_status_ = ok ? "Loaded successfully" : "Load failed";
            } else if (show_export_dialog_) {
                ok = export_body(selected_body, file_path_buf_);
                last_save_status_ = ok ? "Exported successfully" : "Export failed";
            } else if (show_import_dialog_) {
                ok = import_body(file_path_buf_);
                last_save_status_ = ok ? "Imported successfully" : "Import failed";
            }
            save_status_timer_ = 3.0f;
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
    }
    ImGui::End();

    if (!open) {
        show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
        file_path_buf_[0] = '\0';
    }
}

#endif
