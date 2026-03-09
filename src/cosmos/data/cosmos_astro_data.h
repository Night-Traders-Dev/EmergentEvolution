#pragma once
// ── Cosmos Astronomical Data Provider ──────────────────────────────────────
// Wraps SSCore to provide real astronomical data for the cosmos simulator.
// Loads planet/moon catalogs, computes orbital elements, and converts
// SSCore data into CelestialBody format for use in presets and spawning.

#include "cosmos/cosmos_types.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

// ── Astronomical body record ───────────────────────────────────────────────
// Intermediate format between SSCore and CelestialBody.

struct AstroBodyRecord {
    std::string name;
    int         sscore_id = 0;      // SSPlanetID enum value
    int         parent_id = -1;     // parent SSPlanetID (-1 = Sun)

    // Physical properties
    double mass_kg        = 0.0;    // kilograms
    double mass_solar     = 0.0;    // solar masses
    double radius_km      = 0.0;    // equatorial radius in km
    double rotation_days  = 0.0;    // sidereal rotation period in days
    double albedo         = 0.0;    // geometric albedo
    double temperature_K  = 0.0;    // estimated equilibrium temperature

    // Orbital elements (J2000 ecliptic)
    double semi_major_au  = 0.0;    // semi-major axis in AU
    double eccentricity   = 0.0;
    double inclination_rad = 0.0;
    double arg_periapse_rad = 0.0;
    double ascending_node_rad = 0.0;
    double mean_anomaly_rad = 0.0;
    double mean_motion_rad_per_day = 0.0;
    double period_days    = 0.0;

    // Position/velocity at epoch (heliocentric, AU and AU/day)
    glm::dvec3 position_au{0.0};
    glm::dvec3 velocity_au_day{0.0};

    // Classification
    uint32_t cosmos_type = CTYPE_PLANET; // mapped CelestialType
};

// ── Orbit record for direct use ────────────────────────────────────────────

struct AstroOrbitRecord {
    double q;       // periapse distance (AU)
    double e;       // eccentricity
    double i;       // inclination (radians)
    double w;       // argument of periapse (radians)
    double n;       // longitude of ascending node (radians)
    double m;       // mean anomaly (radians)
    double mm;      // mean motion (radians/day)
    double period;  // orbital period (days)
    double a;       // semi-major axis (AU)
};

// ── Scale conversion constants ─────────────────────────────────────────────

namespace AstroScale {
    // SSCore uses AU internally; our sim uses "sim units" where Earth radius = 8 units
    constexpr double KM_PER_AU          = 149597870.7;
    constexpr double KM_PER_EARTH_RADIUS = 6371.0;
    constexpr double SIM_UNITS_PER_KM   = 8.0 / KM_PER_EARTH_RADIUS; // ~1.256e-3
    constexpr double SIM_UNITS_PER_AU   = KM_PER_AU * SIM_UNITS_PER_KM;

    // Mass: SSCore uses Earth masses; sim uses solar masses
    constexpr double EARTH_MASS_KG      = 5.97237e24;
    constexpr double SOLAR_MASS_KG      = 1.98847e30;
    constexpr double EARTH_MASS_SOLAR   = EARTH_MASS_KG / SOLAR_MASS_KG;

    // Radius conversion
    constexpr double SOLAR_RADIUS_KM    = 695508.0;
    constexpr double SOLAR_RADIUS_SIM   = SOLAR_RADIUS_KM * SIM_UNITS_PER_KM;

    // Velocity: AU/day to sim_units/sim_second
    // (sim time scale is configurable, so we provide AU/day → sim_units/day)
    constexpr double SIM_VEL_PER_AU_DAY = SIM_UNITS_PER_AU;

    inline double km_to_sim(double km) { return km * SIM_UNITS_PER_KM; }
    inline double au_to_sim(double au) { return au * SIM_UNITS_PER_AU; }
    inline double au_to_km(double au)  { return au * KM_PER_AU; }
    inline double kg_to_solar(double kg) { return kg / SOLAR_MASS_KG; }
    inline double earth_mass_to_solar(double em) { return em * EARTH_MASS_SOLAR; }
}

// ── Main API ───────────────────────────────────────────────────────────────

namespace AstroData {

// Initialize SSCore (load catalogs from SSData directory).
// Returns true on success. `data_path` should point to the SSData/ directory.
bool init(const std::string& data_path = "external/SSCore/SSData");

// Check if SSCore data has been loaded
bool is_initialized();

// Shut down SSCore and free catalog data
void shutdown();

// ── Planet data ────────────────────────────────────────────────────────

// Get all 9 planets (Mercury-Pluto) with orbital elements and physical data.
// Positions computed for the given Julian Date (default: J2000.0).
std::vector<AstroBodyRecord> get_planets(double jde = 2451545.0);

// Get a single planet by SSPlanetID
AstroBodyRecord get_planet(int planet_id, double jde = 2451545.0);

// ── Moon data ──────────────────────────────────────────────────────────

// Get all moons for a given planet (by SSPlanetID, e.g., 5 = Jupiter)
std::vector<AstroBodyRecord> get_moons(int parent_planet_id, double jde = 2451545.0);

// Get Earth's Moon
AstroBodyRecord get_luna(double jde = 2451545.0);

// ── Orbit computation ──────────────────────────────────────────────────

// Get current orbital elements for a planet
AstroOrbitRecord get_planet_orbit(int planet_id, double jde = 2451545.0);

// Compute position and velocity from orbital elements at a given time
void orbit_to_position_velocity(const AstroOrbitRecord& orbit, double jde,
                                 glm::dvec3& out_pos_au, glm::dvec3& out_vel_au_day);

// ── Conversion to CelestialBody ────────────────────────────────────────

// Convert an AstroBodyRecord directly into a CelestialBody for the sim.
// Handles all unit conversions (AU→sim, kg→solar, km→sim).
// `orbit_scale` controls compression of orbital distances for visual clarity.
// 1.0 = real scale, <1.0 = compressed (e.g., 0.01 for solar system preset).
CelestialBody to_celestial_body(const AstroBodyRecord& record,
                                 float orbit_scale = 1.0f,
                                 const glm::vec3& parent_pos = glm::vec3(0.0f),
                                 const glm::vec3& parent_vel = glm::vec3(0.0f));

// ── Bulk preset helpers ────────────────────────────────────────────────

// Build a complete solar system preset using real SSCore data.
// Returns bodies ready to push into CosmosState.
// `orbit_compression` controls how much to compress orbits (1.0 = real AU,
// smaller values bring planets closer for visual sim).
std::vector<CelestialBody> build_solar_system(
    float orbit_compression = 0.01f,
    float G = 0.5f,
    bool include_moons = true,
    bool include_pluto = true);

// Estimate equilibrium temperature at distance from a star
double estimate_temperature(double luminosity_solar, double distance_au, double albedo);

// Get the number of loaded objects (for diagnostics)
int get_loaded_planet_count();
int get_loaded_moon_count();

} // namespace AstroData
