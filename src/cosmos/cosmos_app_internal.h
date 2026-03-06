#pragma once

#include "cosmos/cosmos_app.h"
#include <random>

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

bool body_can_host_rings(const CelestialBody& body);
bool fragment_like_body(const CelestialBody& body);
bool roche_secondary_fluid_like(const CelestialBody& body);
float roche_distance_for_mode(const CelestialBody& primary, const CelestialBody& secondary, bool fluid_mode);
void set_ring_system(CelestialBody& body, float inner_radius, float outer_radius,
                     float density, float ice_fraction, float tilt);

void star_spectral_bounds(uint32_t type, float& min_mass, float& max_mass,
                          float& min_temp, float& max_temp);
float expected_main_sequence_temperature(float mass);
float stellar_rotation_period_hours(float mass, uint32_t type, uint32_t stage, std::mt19937& rng);
float expected_stellar_luminosity(float mass, float temperature, float radius, uint32_t stage, float fuel);
float solar_radius_sim_units();
float expected_main_sequence_radius_solar(float mass);

void randomize_small_body_properties(CelestialBody& body, std::mt19937& rng, bool is_comet);
void randomize_dust_properties(CelestialBody& body, std::mt19937& rng);
void randomize_nebula_properties(CelestialBody& body, std::mt19937& rng);
void randomize_star_properties(CelestialBody& body, std::mt19937& rng, uint32_t requested_type = CTYPE_STAR);
void randomize_planet_properties(CelestialBody& body, const CosmosState& state, const CosmosConfig& cfg, std::mt19937& rng);
void randomize_moon_properties(CelestialBody& body, const CosmosState& state, std::mt19937& rng);

float merged_star_fuel(const CelestialBody& a, const CelestialBody& b, float total_mass);
enum StellarRemnantKind {
    REMNANT_NONE = 0,
    REMNANT_WHITE_DWARF,
    REMNANT_NEUTRON_STAR,
    REMNANT_BLACK_HOLE,
};
StellarRemnantKind stellar_remnant_kind(float progenitor_mass, bool thermonuclear = false);

float body_gravitational_binding_energy(const CelestialBody& b, float G);
float body_albedo_for_type(const CelestialBody& b);
float body_escape_speed(const CelestialBody& a, const CelestialBody& b, float G);
void register_mass_loss(CelestialBody& b, float amount, float dt);
void apply_impact_signature(CelestialBody& target, glm::vec3 impact_normal,
                            float crater_strength, float ejecta, float heat, float radius);

bool gas_dominated_body(const CelestialBody& b, const MaterialComposition& m);
bool body_is_cloud_feedstock(const CelestialBody& b, const MaterialComposition& m);
float cloud_capture_radius(const CelestialBody& cloud);
float stellar_capture_radius(const CelestialBody& star);
MaterialPhase infer_material_phase(const CelestialBody& b, const MaterialComposition& m);
float phase_intensity_for_body(const CelestialBody& b, MaterialPhase phase, const MaterialComposition& m);
void add_ring_material(CelestialBody& primary, const CelestialBody& source,
                       float deposited_mass, float disruption_factor, const CosmosConfig& cfg);
float magnetic_shielding_score(const CelestialBody& b, float G);
void enforce_body_physical_limits(CelestialBody& b);
float stellar_luminosity_units(const CelestialBody& b);
