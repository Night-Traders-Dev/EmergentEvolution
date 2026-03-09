#include "cosmos/data/cosmos_astro_data.h"
#include "cosmos/cosmos_app_internal.h"

// SSCore headers
#include "SSTime.hpp"
#include "SSOrbit.hpp"
#include "SSPlanet.hpp"
#include "SSObject.hpp"
#include "SSCoordinates.hpp"
#include "SSVector.hpp"
#include "SSUtilities.hpp"

#include <cmath>
#include <algorithm>
#include <map>

// ── Module state ───────────────────────────────────────────────────────────

namespace {

bool g_initialized = false;
SSObjectVec g_planets;       // loaded planet objects
SSObjectVec g_moons;         // loaded moon objects
std::string g_data_path;

// Planet temperature estimates (Kelvin, equilibrium)
const std::map<int, double> kPlanetTemperatures = {
    {1, 440.0},    // Mercury
    {2, 737.0},    // Venus (greenhouse)
    {3, 288.0},    // Earth
    {4, 210.0},    // Mars
    {5, 165.0},    // Jupiter (internal heat)
    {6, 134.0},    // Saturn
    {7, 76.0},     // Uranus
    {8, 72.0},     // Neptune
    {9, 44.0},     // Pluto
};

// Map SSCore planet IDs to cosmos types
uint32_t planet_id_to_cosmos_type(int id) {
    if (id == 0) return CTYPE_STAR;     // Sun
    if (id >= 1 && id <= 9) return CTYPE_PLANET;
    if (id >= 301 && id <= 999) return CTYPE_MOON;
    return CTYPE_ASTEROID;
}

// SSVector to glm::dvec3
glm::dvec3 to_glm(const SSVector& v) {
    return {v.x, v.y, v.z};
}

} // anonymous namespace

// ── Init / Shutdown ────────────────────────────────────────────────────────

namespace AstroData {

bool init(const std::string& data_path) {
    if (g_initialized) return true;

    g_data_path = data_path;

    // Load planet catalog
    std::string planet_csv = data_path + "/SolarSystem/Planets.csv";
    int n_planets = SSImportObjectsFromCSV(planet_csv, g_planets);

    // Load moon catalog
    std::string moon_csv = data_path + "/SolarSystem/Moons.csv";
    int n_moons = SSImportObjectsFromCSV(moon_csv, g_moons);

    g_initialized = (n_planets > 0);
    return g_initialized;
}

bool is_initialized() { return g_initialized; }

void shutdown() {
    g_planets.erase();
    g_moons.erase();
    g_initialized = false;
}

// ── Internal helpers ───────────────────────────────────────────────────────

static AstroBodyRecord make_record_from_planet(SSPlanet* planet, int id, double jde) {
    AstroBodyRecord rec;
    rec.name = planet->getName(0);
    rec.sscore_id = id;
    rec.cosmos_type = planet_id_to_cosmos_type(id);

    // Physical properties
    rec.mass_kg = planet->getMass() * AstroScale::EARTH_MASS_KG;
    rec.mass_solar = AstroScale::kg_to_solar(rec.mass_kg);
    rec.radius_km = planet->getRadius();
    rec.rotation_days = planet->getRotationPeriod();
    rec.albedo = planet->getAlbedo();

    // Temperature
    auto it = kPlanetTemperatures.find(id % 100); // strip century prefix for moons
    if (it != kPlanetTemperatures.end()) {
        rec.temperature_K = it->second;
    } else {
        // Estimate from parent distance
        rec.temperature_K = 200.0;
    }

    // Orbital elements
    SSOrbit orbit = planet->getOrbit();
    rec.semi_major_au = orbit.semiMajorAxis();
    rec.eccentricity = orbit.e;
    rec.inclination_rad = orbit.i;
    rec.arg_periapse_rad = orbit.w;
    rec.ascending_node_rad = orbit.n;
    rec.mean_anomaly_rad = orbit.m;
    rec.mean_motion_rad_per_day = orbit.mm;
    rec.period_days = orbit.period();

    // Compute position/velocity at epoch
    SSVector pos, vel;
    orbit.toPositionVelocity(jde, pos, vel);
    rec.position_au = to_glm(pos);
    rec.velocity_au_day = to_glm(vel);

    // Parent assignment (planets orbit Sun, moons orbit their planet)
    if (id >= 100) {
        rec.parent_id = id / 100; // e.g., 301 → 3 (Earth), 501 → 5 (Jupiter)
    } else {
        rec.parent_id = 0; // Sun
    }

    return rec;
}

// ── Planet queries ─────────────────────────────────────────────────────────

std::vector<AstroBodyRecord> get_planets(double jde) {
    std::vector<AstroBodyRecord> results;
    if (!g_initialized) return results;

    for (int i = 0; i < g_planets.size(); ++i) {
        SSPlanet* p = SSGetPlanetPtr(g_planets[i]);
        if (!p) continue;

        SSIdentifier ident = p->getIdentifier(0);
        int id = static_cast<int>(ident.identifier());

        // Skip the Sun (id=0), we handle it separately
        if (id == 0) continue;

        results.push_back(make_record_from_planet(p, id, jde));
    }

    return results;
}

AstroBodyRecord get_planet(int planet_id, double jde) {
    if (!g_initialized) return {};

    for (int i = 0; i < g_planets.size(); ++i) {
        SSPlanet* p = SSGetPlanetPtr(g_planets[i]);
        if (!p) continue;

        SSIdentifier ident = p->getIdentifier(0);
        if (static_cast<int>(ident.identifier()) == planet_id) {
            return make_record_from_planet(p, planet_id, jde);
        }
    }
    return {};
}

// ── Moon queries ───────────────────────────────────────────────────────────

std::vector<AstroBodyRecord> get_moons(int parent_planet_id, double jde) {
    std::vector<AstroBodyRecord> results;
    if (!g_initialized) return results;

    int parent_century = parent_planet_id * 100; // e.g., 5 → 500

    for (int i = 0; i < g_moons.size(); ++i) {
        SSPlanet* m = SSGetPlanetPtr(g_moons[i]);
        if (!m) continue;

        SSIdentifier ident = m->getIdentifier(0);
        int id = static_cast<int>(ident.identifier());

        // Check if this moon belongs to the requested planet
        if (id / 100 == parent_planet_id) {
            results.push_back(make_record_from_planet(m, id, jde));
        }
    }

    return results;
}

AstroBodyRecord get_luna(double jde) {
    auto moons = get_moons(3, jde); // Earth moons
    for (auto& m : moons) {
        if (m.sscore_id == 301) return m;
    }
    return {};
}

// ── Orbit queries ──────────────────────────────────────────────────────────

AstroOrbitRecord get_planet_orbit(int planet_id, double jde) {
    AstroOrbitRecord rec{};

    // Use SSOrbit's built-in orbit generators for high accuracy
    SSOrbit orbit;
    switch (planet_id) {
    case 1: orbit = SSOrbit::getMercuryOrbit(jde); break;
    case 2: orbit = SSOrbit::getVenusOrbit(jde); break;
    case 3: orbit = SSOrbit::getEarthOrbit(jde); break;
    case 4: orbit = SSOrbit::getMarsOrbit(jde); break;
    case 5: orbit = SSOrbit::getJupiterOrbit(jde); break;
    case 6: orbit = SSOrbit::getSaturnOrbit(jde); break;
    case 7: orbit = SSOrbit::getUranusOrbit(jde); break;
    case 8: orbit = SSOrbit::getNeptuneOrbit(jde); break;
    case 9: orbit = SSOrbit::getPlutoOrbit(jde); break;
    default: return rec;
    }

    rec.q = orbit.q;
    rec.e = orbit.e;
    rec.i = orbit.i;
    rec.w = orbit.w;
    rec.n = orbit.n;
    rec.m = orbit.m;
    rec.mm = orbit.mm;
    rec.period = orbit.period();
    rec.a = orbit.semiMajorAxis();

    return rec;
}

void orbit_to_position_velocity(const AstroOrbitRecord& orbit, double jde,
                                 glm::dvec3& out_pos_au, glm::dvec3& out_vel_au_day) {
    SSOrbit ss_orbit(jde, orbit.q, orbit.e, orbit.i, orbit.w, orbit.n, orbit.m, orbit.mm);
    SSVector pos, vel;
    ss_orbit.toPositionVelocity(jde, pos, vel);
    out_pos_au = to_glm(pos);
    out_vel_au_day = to_glm(vel);
}

// ── Conversion to CelestialBody ────────────────────────────────────────────

CelestialBody to_celestial_body(const AstroBodyRecord& record,
                                 float orbit_scale,
                                 const glm::vec3& parent_pos,
                                 const glm::vec3& parent_vel) {
    CelestialBody body;

    // Position: convert AU to sim units, apply orbit compression
    glm::dvec3 pos_sim = record.position_au * AstroScale::SIM_UNITS_PER_AU * (double)orbit_scale;
    body.pos = parent_pos + glm::vec3(pos_sim);

    // Velocity: convert AU/day to sim units/sim_second
    // The sim time step is in "sim seconds", and we need orbital velocity to
    // produce stable circular orbits under the sim's gravity constant G.
    // Rather than directly converting AU/day, we compute circular orbit velocity
    // from the sim's G and the compressed distance, preserving orbital stability.
    if (record.parent_id >= 0) {
        float dist = glm::length(body.pos - parent_pos);
        if (dist > 0.1f) {
            // Use the parent's mass to compute circular velocity
            // (caller must set correct G for the sim)
            float parent_mass = (record.parent_id == 0) ? 1.0f
                : static_cast<float>(record.mass_solar * 100.0); // approximate
            float v_circ = std::sqrt(0.5f * parent_mass / dist); // default G=0.5
            // Direction: perpendicular to radial, in the orbital plane
            glm::vec3 radial = glm::normalize(body.pos - parent_pos);
            // Cross with "up" to get tangent (approximate)
            glm::vec3 up(0, 1, 0);
            if (std::abs(glm::dot(radial, up)) > 0.99f) up = glm::vec3(1, 0, 0);
            glm::vec3 tangent = glm::normalize(glm::cross(up, radial));

            // Apply inclination from orbital elements
            float cos_i = std::cos(static_cast<float>(record.inclination_rad));
            float sin_i = std::sin(static_cast<float>(record.inclination_rad));
            tangent = tangent * cos_i + glm::cross(radial, tangent) * sin_i;
            tangent = glm::normalize(tangent);

            body.vel = parent_vel + tangent * v_circ;
        }
    }

    body.mass = static_cast<float>(record.mass_solar);
    body.radius = static_cast<float>(AstroScale::km_to_sim(record.radius_km));
    body.temperature = static_cast<float>(record.temperature_K);
    body.type = record.cosmos_type;
    body.seed = static_cast<uint32_t>(std::hash<std::string>{}(record.name));
    body.name = record.name;

    // Rotation period (days → angular velocity in rad/sim_sec)
    if (std::abs(record.rotation_days) > 0.001) {
        body.angular_vel = static_cast<float>(2.0 * M_PI / (record.rotation_days * 86400.0));
    }

    // Orbital elements cache
    body.orbital_period = static_cast<float>(record.period_days * 86400.0); // sim seconds
    body.orbital_eccentricity = static_cast<float>(record.eccentricity);
    body.orbital_semi_major = static_cast<float>(AstroScale::au_to_sim(record.semi_major_au) * orbit_scale);

    return body;
}

// ── Bulk preset: Solar System ──────────────────────────────────────────────

std::vector<CelestialBody> build_solar_system(
    float orbit_compression, float G,
    bool include_moons, bool include_pluto) {

    std::vector<CelestialBody> bodies;
    if (!g_initialized) return bodies;

    const double jde = 2451545.0; // J2000.0

    // ── Sun ────────────────────────────────────────────────────────────
    CelestialBody sun;
    sun.pos = {0, 0, 0};
    sun.vel = {0, 0, 0};
    sun.mass = 1.0f;
    sun.radius = static_cast<float>(AstroScale::SOLAR_RADIUS_SIM);
    sun.temperature = 5778.0f;
    sun.type = CTYPE_STAR;
    sun.seed = 42;
    sun.fuel = 0.72f;
    sun.angular_vel = static_cast<float>(2.0 * M_PI / (25.38 * 86400.0));
    sun.luminosity = expected_stellar_luminosity(sun.mass, sun.temperature, sun.radius,
                                                  SSTAGE_MAIN_SEQUENCE, sun.fuel);
    sun.type = classify_star_spectral(sun.temperature, sun.mass);
    sun.name = "Sun";
    bodies.push_back(sun);

    // ── Planets ────────────────────────────────────────────────────────
    auto planets = get_planets(jde);

    // Sort by distance from Sun
    std::sort(planets.begin(), planets.end(), [](const AstroBodyRecord& a, const AstroBodyRecord& b) {
        return a.semi_major_au < b.semi_major_au;
    });

    std::map<int, int> planet_id_to_index; // SSPlanetID → index in bodies vector

    for (auto& p : planets) {
        if (!include_pluto && p.sscore_id == 9) continue;

        CelestialBody cb = to_celestial_body(p, orbit_compression,
                                              sun.pos, sun.vel);
        // Recompute velocity for stability with our G
        float dist = glm::length(cb.pos - sun.pos);
        if (dist > 0.1f) {
            float v_circ = std::sqrt(G * sun.mass / dist);
            glm::vec3 radial = glm::normalize(cb.pos - sun.pos);
            glm::vec3 up(0, 1, 0);
            if (std::abs(glm::dot(radial, up)) > 0.99f) up = glm::vec3(1, 0, 0);
            glm::vec3 tangent = glm::normalize(glm::cross(up, radial));
            cb.vel = sun.vel + tangent * v_circ;
        }
        cb.parent = 0; // parent = Sun

        // Ensure minimum visible radius
        cb.radius = std::max(cb.radius, 2.0f);

        planet_id_to_index[p.sscore_id] = static_cast<int>(bodies.size());
        bodies.push_back(cb);
    }

    // ── Moons ──────────────────────────────────────────────────────────
    if (include_moons) {
        for (auto& [planet_id, body_idx] : planet_id_to_index) {
            auto moons = get_moons(planet_id, jde);
            const auto& parent = bodies[body_idx];

            for (auto& m : moons) {
                CelestialBody moon_body = to_celestial_body(
                    m, orbit_compression * 20.0f, // moons need less compression
                    parent.pos, parent.vel);

                // Find parent index
                moon_body.parent = body_idx;
                moon_body.type = CTYPE_MOON;

                // Ensure minimum radius
                moon_body.radius = std::max(moon_body.radius, 1.0f);

                // Recompute velocity for stable orbit
                float dist = glm::length(moon_body.pos - parent.pos);
                if (dist > 0.1f) {
                    float v_circ = std::sqrt(G * parent.mass / dist);
                    glm::vec3 radial = glm::normalize(moon_body.pos - parent.pos);
                    glm::vec3 up(0, 1, 0);
                    if (std::abs(glm::dot(radial, up)) > 0.99f) up = glm::vec3(1, 0, 0);
                    glm::vec3 tangent = glm::normalize(glm::cross(up, radial));
                    moon_body.vel = parent.vel + tangent * v_circ;
                }

                bodies.push_back(moon_body);
            }
        }
    }

    return bodies;
}

// ── Utility ────────────────────────────────────────────────────────────────

double estimate_temperature(double luminosity_solar, double distance_au, double albedo) {
    // Stefan-Boltzmann equilibrium: T = T_sun * sqrt(R_sun / (2 * d)) * (1-a)^0.25
    constexpr double T_SUN = 5778.0;
    constexpr double R_SUN_AU = 0.00465047; // solar radius in AU
    double flux_ratio = luminosity_solar * R_SUN_AU * R_SUN_AU / (distance_au * distance_au);
    return T_SUN * std::pow(flux_ratio * (1.0 - albedo) / 4.0, 0.25);
}

int get_loaded_planet_count() { return g_planets.size(); }
int get_loaded_moon_count() { return g_moons.size(); }

} // namespace AstroData
