#pragma once

#include "cosmos/cosmos_app.h"

inline constexpr float EARTH_RADIUS_KM_REAL = 6371.0f;
inline constexpr float EARTH_RADIUS_SIM_UNITS = 8.0f;
inline constexpr float SIM_UNIT_TO_KM = EARTH_RADIUS_KM_REAL / EARTH_RADIUS_SIM_UNITS;
inline constexpr float EARTH_MASS_SOLAR = 3.003e-6f;
inline constexpr float JUPITER_MASS_SOLAR = 9.5458e-4f;
inline constexpr float HYDROGEN_BURNING_MASS_SOLAR = 0.075f;
inline constexpr float MAX_MAIN_SEQUENCE_MASS_SOLAR = 150.0f;
inline constexpr float CORE_COLLAPSE_MIN_MASS_SOLAR = 8.0f;
inline constexpr float BLACK_HOLE_MIN_REMNANT_MASS_SOLAR = 20.0f;
inline constexpr float CHANDRASEKHAR_LIMIT_SOLAR = 1.38f;

struct MaterialComposition {
    float iron = 0.0f;
    float silicate = 0.0f;
    float water = 0.0f;
    float hydrogen = 0.0f;
};

struct ComparisonMetrics {
    float earth_similarity = 0.0f;
    float life_likelihood = 0.0f;
};

struct MagneticMetrics {
    bool  show_magnetosphere = false;
    float magnetosphere_size = 0.0f;
    float magnetic_field = 0.0f;
    bool  show_magnetic_axis = false;
    float magnetic_pole_angle = 0.0f;
    bool  particle_jets = false;
    bool  make_pulsar = false;
};

uint32_t classify_star_spectral(float temperature, float mass);
uint32_t classify_black_hole(float mass);
void clear_ring_system(CelestialBody& body);
void clear_impact_signature(CelestialBody& body);
void refresh_body_render_state(CelestialBody& body, const CosmosState* state = nullptr);
float body_density(const CelestialBody& b);
float body_volume(const CelestialBody& b);
float body_surface_gravity(const CelestialBody& b, float G);
float body_escape_velocity(const CelestialBody& b, float G);
float expected_planet_radius(float mass_solar);
float expected_star_radius(const CelestialBody& b);
MaterialComposition derive_materials(const CelestialBody& b);
ComparisonMetrics derive_comparisons(const CelestialBody& b);
MagneticMetrics derive_magnetic_metrics(const CelestialBody& b, float G);
float equilibrium_temperature_from_star(const CelestialBody& body, const CelestialBody& star);
