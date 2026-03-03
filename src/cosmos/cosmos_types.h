#pragma once
// ── Cosmic Sandbox — Data Types ─────────────────────────────────────────────

#include "common/orbit_camera.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>

// ── Celestial body types ────────────────────────────────────────────────────

enum CelestialType : uint32_t {
    CTYPE_STAR       = 0,
    CTYPE_PLANET     = 1,
    CTYPE_MOON       = 2,
    CTYPE_ASTEROID   = 3,
    CTYPE_COMET      = 4,
    CTYPE_BLACK_HOLE = 5,
    CTYPE_NEBULA     = 6,

    // Star spectral subtypes
    CTYPE_STAR_O     = 7,   // Blue supergiant, >30000K
    CTYPE_STAR_B     = 8,   // Blue-white, 10000-30000K
    CTYPE_STAR_A     = 9,   // White, 7500-10000K
    CTYPE_STAR_F     = 10,  // Yellow-white, 6000-7500K
    CTYPE_STAR_G     = 11,  // Yellow (Sun-like), 5200-6000K
    CTYPE_STAR_K     = 12,  // Orange, 3700-5200K
    CTYPE_STAR_M     = 13,  // Red dwarf, 2400-3700K
    CTYPE_STAR_L     = 14,  // Brown dwarf, 1300-2400K
    CTYPE_STAR_T     = 15,  // Cool brown dwarf, 500-1300K
    CTYPE_STAR_Y     = 16,  // Ultra-cool brown dwarf, <500K
    CTYPE_STAR_WR    = 17,  // Wolf-Rayet

    // Black hole subtypes
    CTYPE_BH_STELLAR      = 18,  // 3-20 solar masses
    CTYPE_BH_INTERMEDIATE = 19,  // 100-100000 solar masses
    CTYPE_BH_SUPERMASSIVE = 20,  // 10^6-10^10 solar masses
    CTYPE_BH_PRIMORDIAL   = 21,  // Sub-stellar mass, tiny

    CTYPE_COUNT
};

// ── Type classification helpers ─────────────────────────────────────────────

inline bool is_star_type(uint32_t t) {
    return t == CTYPE_STAR || (t >= CTYPE_STAR_O && t <= CTYPE_STAR_WR);
}

inline bool is_black_hole_type(uint32_t t) {
    return t == CTYPE_BLACK_HOLE || (t >= CTYPE_BH_STELLAR && t <= CTYPE_BH_PRIMORDIAL);
}

// ── Stellar evolutionary stages ─────────────────────────────────────────────

enum StellarStage : uint32_t {
    SSTAGE_MAIN_SEQUENCE = 0,
    SSTAGE_SUBGIANT      = 1,
    SSTAGE_RED_GIANT     = 2,
    SSTAGE_HORIZONTAL    = 3,  // Horizontal Branch
    SSTAGE_AGB           = 4,  // Asymptotic Giant Branch
    SSTAGE_SUPERGIANT    = 5,
    SSTAGE_HYPERGIANT    = 6,
    SSTAGE_WHITE_DWARF   = 7,
    SSTAGE_NEUTRON_STAR  = 8,
    SSTAGE_COUNT
};

// ── Planet property enums ───────────────────────────────────────────────────

enum SurfaceComposition : uint8_t {
    SURF_ROCKY  = 0,
    SURF_LIQUID = 1,
    SURF_FROZEN = 2,
    SURF_GAS    = 3,
    SURF_MIXED  = 4,
};

enum OceanType : uint8_t {
    OCEAN_NONE    = 0,
    OCEAN_WATER   = 1,
    OCEAN_METHANE = 2,
    OCEAN_AMMONIA = 3,
    OCEAN_LAVA    = 4,
};

enum WeatherType : uint8_t {
    WEATHER_NONE   = 0,
    WEATHER_STORMS = 1,
    WEATHER_RAIN   = 2,
    WEATHER_SNOW   = 3,
    WEATHER_DUST   = 4,
};

// ── Procedurally generated planet properties ────────────────────────────────

struct Atmosphere {
    float n2_frac     = 0.0f;
    float o2_frac     = 0.0f;
    float co2_frac    = 0.0f;
    float h2_frac     = 0.0f;
    float he_frac     = 0.0f;
    float ch4_frac    = 0.0f;
    float nh3_frac    = 0.0f;
    float pressure    = 0.0f;   // atmospheres
    bool  has_clouds  = false;
    float weather_intensity = 0.0f;
};

struct PlanetProperties {
    SurfaceComposition surface = SURF_ROCKY;

    Atmosphere atmosphere;

    OceanType ocean_type     = OCEAN_NONE;
    float     ocean_coverage = 0.0f;   // 0-100 %
    float     ocean_depth    = 0.0f;   // km

    bool  has_mountains   = false;
    bool  has_valleys     = false;
    bool  has_continents  = false;
    float mountain_height = 0.0f;      // km
    float valley_depth    = 0.0f;      // km
    int   continent_count = 0;

    bool        has_weather  = false;
    WeatherType weather_type = WEATHER_NONE;
    float       cloud_coverage     = 0.0f;  // 0-100 %
    float       vegetation_coverage = 0.0f; // 0-100 %
};

// ── Hash helpers for procedural generation ──────────────────────────────────

inline uint32_t hash_combine(uint32_t seed, uint32_t val) {
    seed ^= val + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

inline float hash_float(uint32_t h) {
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

inline uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// ── Generate planet properties deterministically ────────────────────────────

inline PlanetProperties generate_planet_properties(uint32_t seed, float mass, float temperature) {
    PlanetProperties pp;

    uint32_t h = hash_combine(seed, float_bits(mass));
    h = hash_combine(h, float_bits(temperature));

    // ── Surface composition ──
    if (mass > 10.0f) {
        pp.surface = SURF_GAS;
    } else if (temperature < 200.0f) {
        pp.surface = SURF_FROZEN;
    } else if (temperature >= 273.0f && temperature <= 373.0f && mass >= 0.5f && mass <= 3.0f) {
        pp.surface = (hash_float(hash_combine(h, 1)) > 0.3f) ? SURF_MIXED : SURF_LIQUID;
    } else if (temperature >= 200.0f && temperature <= 700.0f && mass >= 0.1f && mass <= 5.0f) {
        pp.surface = SURF_ROCKY;
    } else {
        pp.surface = SURF_MIXED;
    }

    // ── Atmosphere ──
    uint32_t ha = hash_combine(h, 100);
    if (mass > 10.0f) {
        pp.atmosphere.h2_frac  = 0.80f + hash_float(hash_combine(ha, 1)) * 0.10f;
        pp.atmosphere.he_frac  = 1.0f - pp.atmosphere.h2_frac - 0.02f;
        pp.atmosphere.ch4_frac = hash_float(hash_combine(ha, 2)) * 0.02f;
        pp.atmosphere.pressure = 10.0f + hash_float(hash_combine(ha, 3)) * 90.0f;
    } else if (mass > 0.3f && temperature > 150.0f) {
        pp.atmosphere.n2_frac  = hash_float(hash_combine(ha, 4)) * 0.80f;
        pp.atmosphere.co2_frac = hash_float(hash_combine(ha, 5)) * 0.50f;
        if (temperature >= 250.0f && temperature <= 350.0f) {
            pp.atmosphere.o2_frac = hash_float(hash_combine(ha, 6)) * 0.25f;
        }
        float total = pp.atmosphere.n2_frac + pp.atmosphere.o2_frac + pp.atmosphere.co2_frac;
        if (total > 0.01f) {
            pp.atmosphere.n2_frac  /= total;
            pp.atmosphere.o2_frac  /= total;
            pp.atmosphere.co2_frac /= total;
        }
        pp.atmosphere.pressure = 0.1f + hash_float(hash_combine(ha, 7)) * 5.0f;
    }

    pp.atmosphere.has_clouds = (pp.atmosphere.pressure > 0.5f &&
                                hash_float(hash_combine(ha, 8)) > 0.3f);
    pp.atmosphere.weather_intensity = pp.atmosphere.has_clouds
        ? hash_float(hash_combine(ha, 9)) : 0.0f;

    // ── Oceans ──
    uint32_t ho = hash_combine(h, 200);
    if (pp.surface == SURF_GAS) {
        pp.ocean_type = OCEAN_NONE;
    } else if (temperature >= 273.0f && temperature <= 373.0f && mass >= 0.5f) {
        pp.ocean_type     = OCEAN_WATER;
        pp.ocean_coverage = 20.0f + hash_float(hash_combine(ho, 1)) * 80.0f;
        pp.ocean_depth    = 1.0f + hash_float(hash_combine(ho, 2)) * 10.0f;
    } else if (temperature < 110.0f && temperature > 70.0f) {
        pp.ocean_type     = OCEAN_METHANE;
        pp.ocean_coverage = hash_float(hash_combine(ho, 3)) * 40.0f;
        pp.ocean_depth    = 0.5f + hash_float(hash_combine(ho, 4)) * 3.0f;
    } else if (temperature > 700.0f && pp.surface == SURF_ROCKY) {
        pp.ocean_type     = OCEAN_LAVA;
        pp.ocean_coverage = hash_float(hash_combine(ho, 5)) * 30.0f;
        pp.ocean_depth    = 0.1f + hash_float(hash_combine(ho, 6)) * 1.0f;
    } else if (temperature < 200.0f && temperature > 150.0f) {
        pp.ocean_type     = OCEAN_AMMONIA;
        pp.ocean_coverage = hash_float(hash_combine(ho, 7)) * 25.0f;
        pp.ocean_depth    = 0.3f + hash_float(hash_combine(ho, 8)) * 2.0f;
    }

    // ── Terrain (not gas giants) ──
    uint32_t ht = hash_combine(h, 300);
    if (pp.surface != SURF_GAS) {
        pp.has_mountains   = hash_float(hash_combine(ht, 1)) > 0.3f;
        pp.has_valleys     = hash_float(hash_combine(ht, 2)) > 0.4f;
        pp.has_continents  = (pp.ocean_coverage > 20.0f && hash_float(hash_combine(ht, 3)) > 0.3f);
        pp.mountain_height = pp.has_mountains ? (1.0f + hash_float(hash_combine(ht, 4)) * 20.0f) : 0.0f;
        pp.valley_depth    = pp.has_valleys   ? (0.5f + hash_float(hash_combine(ht, 5)) * 10.0f) : 0.0f;
        pp.continent_count = pp.has_continents ? (1 + (int)(hash_float(hash_combine(ht, 6)) * 7.0f)) : 0;
    }

    // ── Climate ──
    uint32_t hc = hash_combine(h, 400);
    pp.has_weather = pp.atmosphere.pressure > 0.3f;
    if (pp.has_weather) {
        float wh = hash_float(hash_combine(hc, 1));
        if (temperature < 200.0f)               pp.weather_type = WEATHER_SNOW;
        else if (temperature > 500.0f)           pp.weather_type = WEATHER_DUST;
        else if (pp.ocean_coverage > 30.0f)      pp.weather_type = (wh > 0.5f) ? WEATHER_STORMS : WEATHER_RAIN;
        else                                     pp.weather_type = (wh > 0.7f) ? WEATHER_STORMS : WEATHER_DUST;
        pp.cloud_coverage = 10.0f + hash_float(hash_combine(hc, 2)) * 80.0f;
    }

    // ── Vegetation (requires water, O2, right temperature) ──
    if (pp.ocean_type == OCEAN_WATER && pp.atmosphere.o2_frac > 0.05f &&
        temperature >= 260.0f && temperature <= 320.0f) {
        pp.vegetation_coverage = hash_float(hash_combine(hc, 3)) * 60.0f;
    }

    return pp;
}

// ── Single celestial body ───────────────────────────────────────────────────

struct CelestialBody {
    glm::vec3   pos{0.0f};
    glm::vec3   vel{0.0f};
    float       mass       = 1.0f;      // solar masses
    float       radius     = 10.0f;     // world-space radius
    float       temperature = 5778.0f;  // Kelvin (for star color)
    uint32_t    type       = CTYPE_PLANET;
    int32_t     parent     = -1;        // orbital parent index (-1 = none)
    float       age            = 0.0f;
    float       internal_energy = 0.0f;
    float       luminosity     = 0.0f;
    float       fuel           = 1.0f;      // 0-1 hydrogen fuel fraction (stars)
    float       angular_vel    = 0.0f;
    uint32_t    stellar_stage  = 0;         // StellarStage enum
    bool        marked_for_removal = false;
    uint32_t    seed           = 0;         // procedural generation seed
};

// ── Simulation config ───────────────────────────────────────────────────────

struct CosmosConfig {
    uint32_t body_count     = 100;
    float    G              = 1.0f;     // gravitational constant scale
    float    dt_scale       = 10.0f;    // derived from time_exponent
    float    softening      = 5.0f;     // gravity softening length
    float    damping        = 1.0f;     // velocity damping (1 = none)
    bool     collisions     = true;
    bool     tidal_forces   = false;
    bool     show_orbits    = true;
    bool     show_trails    = true;
    uint32_t trail_length   = 120;      // max trail points per body

    // Timestep system
    double   time_exponent        = 1.0;   // log10(sim seconds per real second), range [-9, 21]
    double   sim_time_accumulated = 0.0;   // total simulation time in seconds

    // Collision physics
    bool     collision_merging       = true;
    bool     collision_fragmentation = true;
    float    merge_speed_threshold   = 5.0f;     // relative speed below which bodies merge
    float    fragment_speed_threshold = 20.0f;   // relative speed above which bodies fragment

    // Roche limit
    bool     roche_limit        = true;

    // Temperature system
    bool     temperature_system = true;
    float    radiative_cooling  = 0.001f;   // cooling rate per second
    float    collision_heating  = 0.5f;     // fraction of KE → heat

    // Evaporation
    bool     evaporation        = true;
    float    evaporation_rate   = 0.01f;

    // Stellar evolution
    bool     stellar_evolution  = true;
    float    stellar_timescale  = 100.0f;   // seconds for main-sequence lifetime (scaled)

    // Lighting
    bool     star_lighting    = true;   // stars act as point light sources
    bool     uniform_lighting = false;  // everything uniformly illuminated
    float    ambient_strength = 0.08f;  // ambient light level in star mode
};

// ── Body collection ─────────────────────────────────────────────────────────

struct CosmosState {
    std::vector<CelestialBody> bodies;
    std::vector<std::deque<glm::vec3>> trails;

    void clear() { bodies.clear(); trails.clear(); }
    size_t count() const { return bodies.size(); }
};
