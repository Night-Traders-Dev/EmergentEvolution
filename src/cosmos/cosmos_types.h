#pragma once
// ── Cosmic Sandbox — Data Types ─────────────────────────────────────────────

#include "common/orbit_camera.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <string>

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
    CTYPE_DUST            = 22,  // Ring/disk dust grain aggregate

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

enum PlanetClass : uint8_t {
    PCLASS_DWARF = 0,
    PCLASS_TERRESTRIAL = 1,
    PCLASS_OCEAN = 2,
    PCLASS_SUPER_EARTH = 3,
    PCLASS_ICE_GIANT = 4,
    PCLASS_GAS_GIANT = 5,
};

enum MaterialPhase : uint8_t {
    PHASE_SOLID = 0,
    PHASE_LIQUID = 1,
    PHASE_ICE = 2,
    PHASE_GAS = 3,
    PHASE_MOLTEN = 4,
    PHASE_PLASMA = 5,
    PHASE_COLLAPSING = 6,
};

enum BodyRenderClass : uint8_t {
    RENDER_STAR = 0,
    RENDER_PLANET = 1,
    RENDER_MOON = 2,
    RENDER_ASTEROID = 3,
    RENDER_COMET = 4,
    RENDER_BLACK_HOLE = 5,
    RENDER_NEBULA = 6,
};

enum SmallBodyClass : uint8_t {
    SMALLBODY_C = 0,
    SMALLBODY_S = 1,
    SMALLBODY_M = 2,
    SMALLBODY_ICY = 3,
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
    PlanetClass        planet_class = PCLASS_TERRESTRIAL;

    Atmosphere atmosphere;

    OceanType ocean_type     = OCEAN_NONE;
    float     ocean_coverage = 0.0f;   // 0-100 %
    float     ocean_depth    = 0.0f;   // km

    bool  has_mountains   = false;
    bool  has_valleys     = false;
    bool  has_continents  = false;
    bool  has_islands     = false;
    bool  has_rivers      = false;
    bool  has_ice_sheets  = false;
    bool  has_iron_core   = false;
    float mountain_height = 0.0f;      // km
    float valley_depth    = 0.0f;      // km
    int   continent_count = 0;
    float continent_coverage = 0.0f;   // 0-100 %
    float island_coverage    = 0.0f;   // 0-100 %
    float river_density      = 0.0f;   // 0-1
    float ice_sheet_coverage = 0.0f;   // 0-100 %

    bool        has_weather  = false;
    WeatherType weather_type = WEATHER_NONE;
    float       cloud_coverage     = 0.0f;  // 0-100 %
    float       vegetation_coverage = 0.0f; // 0-100 %
};

struct BodyVisualProperties {
    BodyRenderClass render_class = RENDER_PLANET;
    uint8_t subtype = 0;

    float roughness = 0.8f;
    float metallic = 0.0f;
    float specular = 0.04f;
    float normal_strength = 1.0f;

    float terrain_amp = 0.0f;
    float terrain_freq = 1.0f;
    float ridge_amp = 0.0f;
    float crater_density = 0.0f;

    float rock_frac = 0.0f;
    float ice_frac = 0.0f;
    float metal_frac = 0.0f;
    float dust_frac = 0.0f;

    float haze_density = 0.0f;
    float rayleigh_strength = 0.0f;
    float mie_strength = 0.0f;
    float cloud_detail = 0.0f;

    float weather_strength = 0.0f;
    float volcanic_activity = 0.0f;
    float aurora_strength = 0.0f;
    float flare_activity = 0.0f;
    float corona_strength = 0.0f;
    float coma_strength = 0.0f;
    float tail_strength = 0.0f;
    float accretion_strength = 0.0f;
    float jet_strength = 0.0f;
    float lensing_strength = 0.0f;
    float spin_visual = 0.0f;
    float star_spot_coverage = 0.0f;
    float star_flare_frequency = 1.0f;
    float star_pulsation = 0.0f;
    float star_differential_rotation = 0.0f;
    float continent_coverage = 0.0f;
    float island_coverage = 0.0f;
    float river_density = 0.0f;
    float ice_sheet_coverage = 0.0f;
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

// ── Temperature band for hysteresis (prevents per-frame property flashing) ──

inline int temp_band(float t) {
    if (t <  60.0f) return 0;
    if (t <  90.0f) return 1;   // methane ocean zone
    if (t < 130.0f) return 2;
    if (t < 180.0f) return 3;   // ammonia zone
    if (t < 230.0f) return 4;   // frozen
    if (t < 285.0f) return 5;   // cold temperate
    if (t < 350.0f) return 6;   // liquid water habitable
    if (t < 420.0f) return 7;   // hot
    if (t < 600.0f) return 8;   // very hot rocky
    if (t < 800.0f) return 9;   // scorching
    if (t < 1100.0f) return 10; // lava
    return 11;                   // molten
}

// ── Generate planet properties deterministically ────────────────────────────

inline PlanetProperties generate_planet_properties(uint32_t seed, float mass, float temperature,
                                                   bool is_moon = false) {
    PlanetProperties pp;
    constexpr float EARTH_MASS_SOLAR = 3.003e-6f;
    float mass_earth = std::max(mass / EARTH_MASS_SOLAR, 0.01f);

    // Hash only seed + mass (stable inputs). Temperature drives threshold
    // decisions but is NOT hashed — prevents per-frame flashing.
    uint32_t h = hash_combine(seed, float_bits(mass));
    // Secondary hashes for independent random streams
    float h0 = hash_float(hash_combine(h, 0));
    float h1 = hash_float(hash_combine(h, 1));
    float h2 = hash_float(hash_combine(h, 2));

    // ── Surface composition (broader ranges, more variety) ──
    if (is_moon) {
        if (temperature < 170.0f) {
            pp.surface = (h0 < 0.18f && mass_earth > 0.015f) ? SURF_MIXED : SURF_FROZEN;
        } else if (temperature >= 250.0f && temperature <= 320.0f && mass_earth > 0.01f && h0 < 0.10f) {
            pp.surface = SURF_MIXED;
        } else {
            pp.surface = (h0 > 0.30f) ? SURF_ROCKY : SURF_MIXED;
        }
    } else if (mass_earth > 80.0f) {
        pp.surface = SURF_GAS;
    } else if (mass_earth > 8.0f) {
        // Mini-neptune zone: 70% gas, 30% mixed
        pp.surface = (h0 > 0.3f) ? SURF_GAS : SURF_MIXED;
    } else if (temperature < 180.0f) {
        // Cold: mostly frozen, small chance mixed/rocky
        if (h0 < 0.15f && mass_earth > 0.3f)
            pp.surface = SURF_MIXED;  // subsurface ocean world
        else if (h0 < 0.25f)
            pp.surface = SURF_ROCKY;  // barren cold rock
        else
            pp.surface = SURF_FROZEN;
    } else if (temperature >= 235.0f && temperature <= 395.0f && mass_earth >= 0.25f && mass_earth <= 8.0f) {
        // Habitable zone: diverse
        if (h0 < 0.22f)
            pp.surface = SURF_LIQUID;  // water world
        else if (h0 < 0.72f)
            pp.surface = SURF_MIXED;   // earth-like continents
        else if (h0 < 0.92f)
            pp.surface = SURF_ROCKY;   // arid/desert
        else
            pp.surface = SURF_FROZEN;  // tidally locked cold side
    } else if (temperature > 700.0f) {
        // Very hot: rocky/mixed
        pp.surface = (h0 > 0.4f) ? SURF_ROCKY : SURF_MIXED;
    } else {
        // Default: rocky or mixed based on seed
        pp.surface = (h0 > 0.5f) ? SURF_ROCKY : SURF_MIXED;
    }

    if (is_moon) {
        if (pp.surface == SURF_LIQUID)
            pp.planet_class = PCLASS_OCEAN;
        else if (mass_earth < 0.03f)
            pp.planet_class = PCLASS_DWARF;
        else
            pp.planet_class = PCLASS_TERRESTRIAL;
    } else if (pp.surface == SURF_GAS) {
        pp.planet_class = (mass_earth >= 45.0f) ? PCLASS_GAS_GIANT : PCLASS_ICE_GIANT;
    } else if (mass_earth < 0.18f) {
        pp.planet_class = PCLASS_DWARF;
    } else if (pp.surface == SURF_LIQUID) {
        pp.planet_class = PCLASS_OCEAN;
    } else if (mass_earth > 2.4f) {
        pp.planet_class = PCLASS_SUPER_EARTH;
    } else {
        pp.planet_class = PCLASS_TERRESTRIAL;
    }

    // ── Atmosphere ──
    uint32_t ha = hash_combine(h, 100);
    float ha0 = hash_float(hash_combine(ha, 0));
    if (pp.surface == SURF_GAS) {
        pp.atmosphere.h2_frac  = 0.70f + hash_float(hash_combine(ha, 1)) * 0.20f;
        pp.atmosphere.he_frac  = 1.0f - pp.atmosphere.h2_frac - 0.03f;
        pp.atmosphere.ch4_frac = hash_float(hash_combine(ha, 2)) * 0.03f;
        pp.atmosphere.pressure = 10.0f + hash_float(hash_combine(ha, 3)) * 200.0f;
    } else if (mass_earth > (is_moon ? 0.01f : 0.08f) && temperature > 120.0f) {
        // Terrestrial atmosphere — wide variety
        float atm_chance = std::min(1.0f, (is_moon ? 0.04f : 0.16f) + mass_earth * (is_moon ? 0.12f : 0.22f));
        if (ha0 < atm_chance) {
            pp.atmosphere.n2_frac  = 0.1f + hash_float(hash_combine(ha, 4)) * 0.85f;
            pp.atmosphere.co2_frac = hash_float(hash_combine(ha, 5)) * 0.60f;
            if (temperature >= 220.0f && temperature <= 380.0f && h1 > 0.28f) {
                pp.atmosphere.o2_frac = hash_float(hash_combine(ha, 6)) * 0.30f;
            }
            // Some worlds: thick CO2 (Venus-like)
            if (temperature > 400.0f && hash_float(hash_combine(ha, 10)) > 0.5f) {
                pp.atmosphere.co2_frac = 0.90f + hash_float(hash_combine(ha, 11)) * 0.08f;
                pp.atmosphere.n2_frac  = 0.02f;
                pp.atmosphere.o2_frac  = 0.0f;
            }
            // Methane atmosphere for cold worlds
            if (temperature < 200.0f) {
                pp.atmosphere.ch4_frac = hash_float(hash_combine(ha, 12)) * 0.15f;
                pp.atmosphere.nh3_frac = hash_float(hash_combine(ha, 13)) * 0.05f;
            }
            float total = pp.atmosphere.n2_frac + pp.atmosphere.o2_frac +
                          pp.atmosphere.co2_frac + pp.atmosphere.ch4_frac + pp.atmosphere.nh3_frac;
            if (total > 0.01f) {
                pp.atmosphere.n2_frac  /= total;
                pp.atmosphere.o2_frac  /= total;
                pp.atmosphere.co2_frac /= total;
                pp.atmosphere.ch4_frac /= total;
                pp.atmosphere.nh3_frac /= total;
            }
            // Pressure: thin to thick based on mass and seed
            float base_p = 0.02f + std::pow(std::min(mass_earth, 30.0f), 0.45f) * (is_moon ? 0.14f : 0.55f);
            pp.atmosphere.pressure = base_p + hash_float(hash_combine(ha, 7)) * base_p * 4.0f;
            if (temperature > 400.0f && pp.atmosphere.co2_frac > 0.5f)
                pp.atmosphere.pressure = 20.0f + hash_float(hash_combine(ha, 14)) * 80.0f; // Venus-like
        }
        // else: airless (Mercury/Moon-like) — pressure stays 0
    }

    if ((pp.planet_class == PCLASS_DWARF || is_moon) && pp.surface != SURF_GAS) {
        pp.atmosphere.pressure *= 0.05f + ha0 * 0.25f;
    }
    if (pp.planet_class == PCLASS_GAS_GIANT || pp.planet_class == PCLASS_ICE_GIANT) {
        pp.atmosphere.has_clouds = true;
        pp.atmosphere.pressure = std::max(pp.atmosphere.pressure, 18.0f + ha0 * 120.0f);
    }

    pp.atmosphere.has_clouds = ((pp.atmosphere.pressure > 0.3f &&
                                 hash_float(hash_combine(ha, 8)) > 0.25f) ||
                                pp.planet_class == PCLASS_GAS_GIANT ||
                                pp.planet_class == PCLASS_ICE_GIANT);
    pp.atmosphere.weather_intensity = pp.atmosphere.has_clouds
        ? 0.28f + hash_float(hash_combine(ha, 9)) * 0.72f : 0.0f;

    // ── Oceans (wider temperature ranges, more types) ──
    uint32_t ho = hash_combine(h, 200);
    float ho0 = hash_float(hash_combine(ho, 0));
    if (pp.surface == SURF_GAS) {
        pp.ocean_type = OCEAN_NONE;
    } else if (pp.surface == SURF_LIQUID) {
        // Water world — very high coverage
        pp.ocean_type     = OCEAN_WATER;
        pp.ocean_coverage = 80.0f + ho0 * 20.0f;
        pp.ocean_depth    = 5.0f + hash_float(hash_combine(ho, 1)) * 50.0f;
    } else if (temperature >= 235.0f && temperature <= 390.0f && mass_earth >= 0.25f && ho0 > 0.18f) {
        pp.ocean_type     = OCEAN_WATER;
        pp.ocean_coverage = 22.0f + hash_float(hash_combine(ho, 1)) * 68.0f;
        pp.ocean_depth    = 0.8f + hash_float(hash_combine(ho, 2)) * 14.0f;
    } else if (temperature < 115.0f && temperature > 65.0f && ho0 > 0.4f) {
        pp.ocean_type     = OCEAN_METHANE;
        pp.ocean_coverage = 5.0f + hash_float(hash_combine(ho, 3)) * 50.0f;
        pp.ocean_depth    = 0.2f + hash_float(hash_combine(ho, 4)) * 5.0f;
    } else if (temperature > 600.0f && (pp.surface == SURF_ROCKY || pp.surface == SURF_MIXED) && ho0 > 0.35f) {
        pp.ocean_type     = OCEAN_LAVA;
        pp.ocean_coverage = 5.0f + hash_float(hash_combine(ho, 5)) * 50.0f;
        pp.ocean_depth    = 0.1f + hash_float(hash_combine(ho, 6)) * 2.0f;
    } else if (temperature < 210.0f && temperature > 140.0f && ho0 > 0.5f) {
        pp.ocean_type     = OCEAN_AMMONIA;
        pp.ocean_coverage = 5.0f + hash_float(hash_combine(ho, 7)) * 35.0f;
        pp.ocean_depth    = 0.2f + hash_float(hash_combine(ho, 8)) * 3.0f;
    }

    if (pp.planet_class == PCLASS_OCEAN && pp.ocean_type == OCEAN_WATER) {
        pp.ocean_coverage = std::max(pp.ocean_coverage, 85.0f + ho0 * 12.0f);
        pp.ocean_depth = std::max(pp.ocean_depth, 10.0f + hash_float(hash_combine(ho, 9)) * 60.0f);
    }
    if ((pp.planet_class == PCLASS_DWARF || is_moon) && pp.ocean_type != OCEAN_NONE) {
        pp.ocean_coverage *= 0.65f;
        pp.ocean_depth *= 0.55f;
    }

    // ── Terrain (not gas giants) ──
    uint32_t ht = hash_combine(h, 300);
    if (pp.surface != SURF_GAS) {
        pp.has_mountains   = hash_float(hash_combine(ht, 1)) > 0.25f;
        pp.has_valleys     = hash_float(hash_combine(ht, 2)) > 0.30f;
        pp.has_continents  = (pp.ocean_coverage > 15.0f && hash_float(hash_combine(ht, 3)) > 0.2f);
        pp.mountain_height = pp.has_mountains ? (0.5f + hash_float(hash_combine(ht, 4)) * 25.0f) : 0.0f;
        pp.valley_depth    = pp.has_valleys   ? (0.2f + hash_float(hash_combine(ht, 5)) * 12.0f) : 0.0f;
        pp.continent_count = pp.has_continents ? (1 + (int)(hash_float(hash_combine(ht, 6)) * 8.0f)) : 0;
        float ocean_frac = pp.ocean_coverage / 100.0f;
        pp.continent_coverage = pp.has_continents
            ? std::clamp((1.0f - ocean_frac) * (45.0f + hash_float(hash_combine(ht, 7)) * 45.0f), 5.0f, 92.0f)
            : 0.0f;
        pp.has_islands = (pp.ocean_coverage > 8.0f && pp.ocean_coverage < 92.0f &&
                          hash_float(hash_combine(ht, 8)) > 0.25f);
        pp.island_coverage = pp.has_islands
            ? std::clamp(2.0f + ocean_frac * 25.0f + hash_float(hash_combine(ht, 9)) * 20.0f, 2.0f, 38.0f)
            : 0.0f;
        pp.has_ice_sheets = (temperature < 285.0f || pp.surface == SURF_FROZEN);
        pp.ice_sheet_coverage = pp.has_ice_sheets
            ? std::clamp((285.0f - temperature) * 0.45f + hash_float(hash_combine(ht, 10)) * 20.0f +
                         (pp.surface == SURF_FROZEN ? 25.0f : 0.0f), 0.0f, 92.0f)
            : 0.0f;
        pp.has_iron_core = (mass_earth > (is_moon ? 0.025f : 0.12f) && hash_float(hash_combine(ht, 11)) > 0.18f) ||
                           pp.planet_class == PCLASS_SUPER_EARTH;
        pp.has_rivers = (pp.ocean_type == OCEAN_WATER && pp.atmosphere.pressure > 0.12f &&
                         temperature >= 235.0f && temperature <= 340.0f &&
                         (pp.has_continents || pp.has_islands));
        pp.river_density = pp.has_rivers
            ? std::clamp(pp.atmosphere.weather_intensity * 0.55f +
                         (pp.continent_coverage / 100.0f) * 0.45f +
                         hash_float(hash_combine(ht, 12)) * 0.35f, 0.08f, 1.0f)
            : 0.0f;
        if (pp.planet_class == PCLASS_DWARF || is_moon) {
            pp.continent_count = std::min(pp.continent_count, 3);
            pp.continent_coverage *= 0.65f;
            pp.mountain_height *= 0.55f;
            pp.valley_depth *= 0.70f;
            pp.river_density *= 0.35f;
            pp.island_coverage *= 0.70f;
        }
        if (is_moon) {
            pp.atmosphere.pressure *= 0.45f;
            pp.cloud_coverage *= 0.35f;
            pp.continent_count = std::min(pp.continent_count, 2);
            pp.continent_coverage *= 0.55f;
            pp.has_weather = pp.atmosphere.pressure > 0.08f;
            pp.has_rivers = pp.has_rivers && pp.atmosphere.pressure > 0.12f;
            pp.river_density *= 0.30f;
            pp.ocean_coverage = std::min(pp.ocean_coverage, 38.0f);
            pp.ocean_depth *= 0.65f;
        } else if (pp.planet_class == PCLASS_SUPER_EARTH) {
            pp.mountain_height *= 1.25f;
            pp.valley_depth *= 1.15f;
        } else if (pp.planet_class == PCLASS_OCEAN) {
            pp.continent_coverage *= 0.55f;
            pp.island_coverage = std::max(pp.island_coverage, 8.0f + hash_float(hash_combine(ht, 13)) * 22.0f);
        }
    } else {
        pp.has_weather = true;
        pp.weather_type = WEATHER_STORMS;
        pp.cloud_coverage = 68.0f + hash_float(hash_combine(ht, 14)) * 30.0f;
        pp.atmosphere.has_clouds = true;
    }

    // ── Climate ──
    uint32_t hc = hash_combine(h, 400);
    pp.has_weather = pp.atmosphere.pressure > 0.2f;
        if (pp.has_weather) {
            float wh = hash_float(hash_combine(hc, 1));
            if (temperature < 180.0f)               pp.weather_type = WEATHER_SNOW;
            else if (temperature > 500.0f)           pp.weather_type = WEATHER_DUST;
            else if (pp.ocean_coverage > 40.0f)      pp.weather_type = (wh > 0.4f) ? WEATHER_STORMS : WEATHER_RAIN;
            else if (pp.ocean_coverage > 10.0f)      pp.weather_type = (wh > 0.6f) ? WEATHER_RAIN : WEATHER_DUST;
            else                                     pp.weather_type = (wh > 0.7f) ? WEATHER_STORMS : WEATHER_DUST;
        pp.cloud_coverage = 8.0f + hash_float(hash_combine(hc, 2)) * 85.0f;
        // Dry worlds: less clouds
        if (pp.ocean_coverage < 10.0f) pp.cloud_coverage *= 0.3f;
    }
    if (pp.ocean_type == OCEAN_WATER && pp.atmosphere.pressure > 0.18f) {
        pp.atmosphere.has_clouds = true;
        pp.has_weather = true;
        pp.cloud_coverage = std::max(pp.cloud_coverage, 32.0f + hash_float(hash_combine(hc, 15)) * 52.0f);
        pp.atmosphere.weather_intensity = std::max(pp.atmosphere.weather_intensity,
            0.40f + hash_float(hash_combine(hc, 16)) * 0.55f);
        if (pp.weather_type == WEATHER_NONE)
            pp.weather_type = (pp.cloud_coverage > 62.0f) ? WEATHER_STORMS : WEATHER_RAIN;
    }
    if (pp.planet_class == PCLASS_GAS_GIANT || pp.planet_class == PCLASS_ICE_GIANT) {
        pp.has_weather = true;
        pp.weather_type = WEATHER_STORMS;
        pp.cloud_coverage = std::max(pp.cloud_coverage, 70.0f + hash_float(hash_combine(hc, 4)) * 25.0f);
        pp.atmosphere.has_clouds = true;
    }
    if (pp.has_rivers) {
        pp.river_density = std::clamp(pp.river_density +
                                      (pp.cloud_coverage / 100.0f) * 0.50f +
                                      pp.atmosphere.weather_intensity * 0.25f, 0.08f, 1.0f);
    }

    // ── Vegetation (requires water, O2, right temperature) ──
    if (pp.ocean_type == OCEAN_WATER && pp.atmosphere.o2_frac > 0.03f &&
        temperature >= 240.0f && temperature <= 340.0f) {
        float veg_potential = std::min(pp.atmosphere.o2_frac * 4.0f, 1.0f) *
                              std::min(pp.ocean_coverage / 50.0f, 1.0f);
        pp.vegetation_coverage = hash_float(hash_combine(hc, 3)) * 70.0f * veg_potential;
    }

    return pp;
}

// ── Procedural name generation ──────────────────────────────────────────────

inline std::string generate_body_name(uint32_t seed, uint32_t type) {
    // Syllable pools for planet/moon/asteroid names
    static const char* const ONSETS[]  = {
        "", "b", "c", "d", "f", "g", "h", "j", "k", "l", "m", "n",
        "p", "r", "s", "t", "v", "z", "th", "sh", "ch", "kr", "tr",
        "pr", "br", "gr", "dr", "st", "fr", "gl", "pl", "cl", "qu",
    };
    static const char* const NUCLEI[] = {
        "a", "e", "i", "o", "u", "ae", "ei", "ou", "ai", "au",
        "ia", "io", "ea", "eo", "ua",
    };
    static const char* const CODAS[]  = {
        "", "n", "s", "r", "l", "th", "x", "m", "d", "k", "p",
        "nd", "ns", "rs", "nt", "rn",
    };

    // Star prefix letters (like real catalog designations)
    static const char* const STAR_PREFIXES[] = {
        "HD", "HR", "HIP", "TYC", "Gliese", "Ross", "Wolf", "Luyten",
        "BD", "CD", "Kepler", "TRAPPIST", "Proxima", "Barnard",
    };
    static const char* const GREEK[]  = {
        "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta",
        "Eta", "Theta", "Iota", "Kappa", "Lambda", "Mu",
        "Nu", "Xi", "Omicron", "Pi", "Rho", "Sigma",
        "Tau", "Upsilon", "Phi", "Chi", "Psi", "Omega",
    };
    static const char* const CONSTELLATIONS[] = {
        "Centauri", "Cygni", "Orionis", "Lyrae", "Eridani", "Draconis",
        "Pegasi", "Aquilae", "Ursae", "Leonis", "Tauri", "Scorpii",
        "Sagittarii", "Hydrae", "Andromedae", "Cassiopeiae", "Virginis",
        "Crucis", "Carinae", "Velorum", "Puppis", "Pavonis",
    };

    uint32_t h = seed;
    auto next = [&]() -> uint32_t {
        h = hash_combine(h, 0x12345678);
        return h;
    };

    std::string name;

    if (is_star_type(type)) {
        // 50% chance catalog style, 50% Greek+Constellation
        if ((next() & 1) == 0) {
            const char* prefix = STAR_PREFIXES[next() % 14];
            uint32_t num = (next() % 9000) + 1000;
            name = std::string(prefix) + " " + std::to_string(num);
        } else {
            const char* greek = GREEK[next() % 24];
            const char* constellation = CONSTELLATIONS[next() % 22];
            name = std::string(greek) + " " + constellation;
        }
    } else if (is_black_hole_type(type)) {
        // Black holes get catalog-like names
        static const char* const BH_PREFIXES[] = {
            "Sgr", "Cyg", "M", "NGC", "TON", "PG", "GRS", "XTE",
        };
        const char* prefix = BH_PREFIXES[next() % 8];
        uint32_t num = (next() % 9000) + 100;
        name = std::string(prefix) + " " + std::to_string(num);
    } else {
        // Planets, moons, asteroids, comets, nebulae — syllable-based
        int syllable_count = 2 + (int)(next() % 2); // 2-3 syllables
        for (int s = 0; s < syllable_count; s++) {
            name += ONSETS[next() % 33];
            name += NUCLEI[next() % 15];
            if (s < syllable_count - 1)
                name += CODAS[next() % 16];
            else
                name += CODAS[next() % 11]; // shorter ending
        }
        // Capitalize first letter
        if (!name.empty() && name[0] >= 'a' && name[0] <= 'z')
            name[0] -= 32;

        // Moons get parent reference suffix
        if (type == CTYPE_MOON) {
            int moon_idx = (int)(next() % 20) + 1;
            char suffix[8];
            snprintf(suffix, sizeof(suffix), " %c", 'a' + (char)(moon_idx % 26));
            name += suffix;
        }
    }

    return name;
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
    float       mass_loss_rate = 0.0f;      // solar masses / simulated second
    float       mass_loss_total = 0.0f;     // cumulative solar masses lost
    float       atmosphere_retention = 1.0f; // 0-1 atmospheric reservoir for erosion / shielding
    float       phase_intensity = 0.0f;     // 0-1 current strength of dominant material phase
    float       collapse_progress = 0.0f;   // 0-1 protostellar / cloud collapse state
    float       ring_inner_radius = 0.0f;   // world-space inner ring radius
    float       ring_outer_radius = 0.0f;   // world-space outer ring radius
    float       ring_density = 0.0f;        // 0-1 visual density / optical depth
    float       ring_ice_fraction = 0.0f;   // 0-1 icy vs dusty ring composition
    float       ring_tilt = 0.0f;           // radians from "up" for procedural ring axis
    glm::vec3   impact_normal{0.0f, 1.0f, 0.0f}; // dominant recent impact location on the surface
    float       impact_crater_strength = 0.0f;   // 0-1 crater/scar visibility
    float       impact_heat = 0.0f;              // 0-1 molten/glow strength
    float       impact_radius = 0.0f;            // 0-1 angular size of dominant impact basin
    float       impact_ejecta = 0.0f;            // 0-1 dust/ejecta blanket strength
    float       angular_vel    = 0.0f;
    uint32_t    stellar_stage  = 0;         // StellarStage enum
    uint32_t    material_phase = PHASE_SOLID;
    bool        marked_for_removal = false;
    bool        non_attracting = false;     // affected by gravity but does not source gravity
    uint32_t    seed           = 0;         // procedural generation seed
    uint32_t    frag_generation = 0;        // how many times this body has been fragmented (0 = original)
    int32_t     forced_surface = -1;        // -1 auto, 0 rocky, 1 water, 2 ice, 3 earth-like, 4 gas giant
    bool        custom_material = false;    // use manually specified composition fractions
    float       custom_iron = 0.0f;
    float       custom_silicate = 0.0f;
    float       custom_water = 0.0f;
    float       custom_hydrogen = 0.0f;
    std::string name;                       // procedural or user-given name

    // Cached planet properties (computed once, refreshed only on major temp band changes)
    PlanetProperties cached_props;
    bool             props_valid = false;
    int              cached_temp_band = -1;  // transient, not serialized

    BodyVisualProperties cached_visuals;
    bool                 visuals_valid = false;
    int                  cached_visual_temp_band = -1;  // transient, not serialized
};

// ── Refresh cached planet properties (call after physics, not in renderer) ──

inline void refresh_planet_props(CelestialBody& b) {
    if (b.type != CTYPE_PLANET && b.type != CTYPE_MOON) return;
    int band = temp_band(b.temperature);
    if (b.props_valid && band == b.cached_temp_band) return; // same band, no change
    b.cached_props = generate_planet_properties(b.seed, b.mass, b.temperature, b.type == CTYPE_MOON);
    float retention = std::clamp(b.atmosphere_retention, 0.0f, 1.0f);
    float retention_scale = (b.cached_props.surface == SURF_GAS)
        ? (0.60f + retention * 0.40f)
        : (0.05f + retention * 0.95f);
    b.cached_props.atmosphere.pressure *= retention_scale;
    b.cached_props.cloud_coverage *= (b.cached_props.surface == SURF_GAS)
        ? (0.70f + retention * 0.30f)
        : retention;
    b.cached_props.atmosphere.weather_intensity *= 0.25f + retention * 0.75f;
    b.cached_props.river_density *= retention;
    if (b.cached_props.surface != SURF_GAS && retention < 0.15f) {
        b.cached_props.atmosphere.has_clouds = false;
        b.cached_props.cloud_coverage = 0.0f;
        b.cached_props.has_weather = false;
        b.cached_props.weather_type = WEATHER_NONE;
    } else {
        b.cached_props.atmosphere.has_clouds =
            (b.cached_props.atmosphere.pressure > 0.05f && b.cached_props.cloud_coverage > 2.0f);
    }

    if (b.forced_surface >= 0) {
        float mass_earth = b.mass / 3.003e-6f;
        switch (b.forced_surface) {
        case 0: // rocky
            b.cached_props.surface = SURF_ROCKY;
            b.cached_props.planet_class = (mass_earth > 2.4f) ? PCLASS_SUPER_EARTH : PCLASS_TERRESTRIAL;
            b.cached_props.ocean_type = OCEAN_NONE;
            b.cached_props.ocean_coverage = 0.0f;
            b.cached_props.cloud_coverage = std::min(b.cached_props.cloud_coverage, 25.0f);
            b.cached_props.vegetation_coverage = std::min(b.cached_props.vegetation_coverage, 12.0f);
            break;
        case 1: // water
            b.cached_props.surface = SURF_LIQUID;
            b.cached_props.planet_class = PCLASS_OCEAN;
            b.cached_props.ocean_type = OCEAN_WATER;
            b.cached_props.ocean_coverage = std::max(b.cached_props.ocean_coverage, 82.0f);
            b.cached_props.cloud_coverage = std::max(b.cached_props.cloud_coverage, 35.0f);
            b.cached_props.vegetation_coverage = std::min(b.cached_props.vegetation_coverage, 8.0f);
            break;
        case 2: // ice
            b.cached_props.surface = SURF_FROZEN;
            b.cached_props.planet_class = (mass_earth < 0.18f) ? PCLASS_DWARF : PCLASS_TERRESTRIAL;
            b.cached_props.ocean_type = OCEAN_NONE;
            b.cached_props.ocean_coverage = std::min(b.cached_props.ocean_coverage, 15.0f);
            b.cached_props.ice_sheet_coverage = std::max(b.cached_props.ice_sheet_coverage, 70.0f);
            b.cached_props.cloud_coverage = std::min(b.cached_props.cloud_coverage, 35.0f);
            b.cached_props.vegetation_coverage = 0.0f;
            break;
        case 3: // earth-like
            b.cached_props.surface = SURF_MIXED;
            b.cached_props.planet_class = PCLASS_TERRESTRIAL;
            b.cached_props.ocean_type = OCEAN_WATER;
            b.cached_props.ocean_coverage = std::clamp(b.cached_props.ocean_coverage, 40.0f, 75.0f);
            b.cached_props.atmosphere.n2_frac = 0.78f;
            b.cached_props.atmosphere.o2_frac = 0.21f;
            b.cached_props.atmosphere.co2_frac = std::clamp(b.cached_props.atmosphere.co2_frac, 0.0001f, 0.02f);
            b.cached_props.atmosphere.pressure = std::clamp(b.cached_props.atmosphere.pressure, 0.6f, 2.2f);
            b.cached_props.cloud_coverage = std::clamp(b.cached_props.cloud_coverage, 20.0f, 80.0f);
            b.cached_props.vegetation_coverage = std::max(b.cached_props.vegetation_coverage, 28.0f);
            b.cached_props.has_continents = true;
            b.cached_props.has_weather = true;
            b.cached_props.weather_type = (b.cached_props.cloud_coverage > 62.0f) ? WEATHER_STORMS : WEATHER_RAIN;
            break;
        case 4: // gas giant
            b.cached_props.surface = SURF_GAS;
            b.cached_props.planet_class = (mass_earth > 45.0f) ? PCLASS_GAS_GIANT : PCLASS_ICE_GIANT;
            b.cached_props.ocean_type = OCEAN_NONE;
            b.cached_props.ocean_coverage = 0.0f;
            b.cached_props.vegetation_coverage = 0.0f;
            b.cached_props.ice_sheet_coverage = (b.cached_props.planet_class == PCLASS_ICE_GIANT)
                ? std::clamp(b.cached_props.ice_sheet_coverage, 18.0f, 65.0f)
                : std::clamp(b.cached_props.ice_sheet_coverage, 0.0f, 28.0f);
            b.cached_props.atmosphere.pressure = std::max(b.cached_props.atmosphere.pressure, 15.0f);
            b.cached_props.cloud_coverage = std::max(b.cached_props.cloud_coverage, 58.0f);
            b.cached_props.atmosphere.h2_frac = std::max(b.cached_props.atmosphere.h2_frac, 0.68f);
            b.cached_props.atmosphere.he_frac = std::max(b.cached_props.atmosphere.he_frac, 0.18f);
            b.cached_props.has_weather = true;
            b.cached_props.weather_type = WEATHER_STORMS;
            break;
        default:
            break;
        }
    }

    b.cached_temp_band = band;
    b.props_valid = true;
}

// ── Simulation config ───────────────────────────────────────────────────────

struct CosmosConfig {
    uint32_t body_count     = 100;
    float    G              = 1.0f;     // gravitational constant scale
    float    dt_scale       = 10.0f;    // derived from time_exponent
    float    softening      = 5.0f;     // gravity softening length
    float    damping        = 1.0f;     // velocity damping (1 = none)
    bool     collisions     = true;
    bool     tidal_forces   = true;
    bool     show_orbits    = true;
    bool     show_trails    = true;
    uint32_t trail_length   = 120;      // max trail points per body

    // Timestep system
    double   time_exponent        = 1.0;   // log10(sim seconds per real second), range [-9, 21]
    double   sim_time_accumulated = 0.0;   // total simulation time in seconds
    bool     adaptive_time_step   = false; // dynamically clamp simulation dt for stability
    float    adaptive_step_safety = 0.22f; // lower = safer, slower
    float    adaptive_step_min    = 1.0e-4f; // absolute sim-seconds lower bound
    float    adaptive_step_max    = 500000.0f; // absolute sim-seconds upper bound

    // Collision physics
    bool     collision_merging       = true;
    bool     collision_fragmentation = true;
    bool     collision_sph           = true;     // apply SPH-like pressure/viscosity for soft-body impacts
    bool     collision_rigid_body_dynamics = true; // apply rigid-body impulse/depenetration response
    float    merge_speed_threshold   = 5.0f;     // relative speed below which bodies merge
    float    fragment_speed_threshold = 20.0f;   // relative speed above which bodies fragment
    int      fragment_count          = 4;        // number of fragments from collision/tidal breakup (1-12)
    float    min_fragment_mass       = 1.0e-8f;  // bodies below this mass cannot fragment (just bounce)
    int      max_frag_generation     = 2;        // max times a body can be re-fragmented (0=originals only)

    // Roche limit
    bool     roche_limit        = true;
    bool     roche_limit_fluid  = true;
    bool     roche_limit_rigid  = true;
    bool     material_phases    = true;
    bool     planetary_rings    = true;

    // Dynamic performance budget
    bool     dynamic_budget_enabled      = true;
    int      dynamic_max_fragments       = 300;   // attracting fragment cap
    int      dynamic_max_non_attracting  = 900;   // debris cap
    float    dynamic_explosion_density   = 0.25f; // per-event share of non-attracting budget (0-1)
    float    dynamic_reduction_percent   = 0.20f; // cull percent when reducing (0-1)
    float    dynamic_target_fps          = 60.0f; // target framerate
    bool     dust_debug_non_attracting   = true;  // debug: dust sources gravity when false
    float    ring_inner_scale            = 1.0f;  // global ring inner radius multiplier
    float    ring_outer_scale            = 1.0f;  // global ring outer radius multiplier
    float    ring_density_scale          = 1.0f;  // global ring density multiplier
    float    ring_thickness_scale        = 1.0f;  // global vertical thickness multiplier
    float    ring_particle_scale         = 1.0f;  // global spawned dust-count multiplier
    float    ring_mass_scale             = 1.0f;  // global ring mass-to-dust multiplier

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
    bool     fast_star_lighting = true; // use strongest-star direct lighting path
    float    ambient_strength = 0.08f;  // ambient light level in star mode
    bool     cosmos_hq_shading = true;
    bool     cosmos_background_starfield = true;
    bool     cosmos_star_corona = true;
    bool     cosmos_comet_tails = true;
    bool     cosmos_blackhole_lensing = true;
    bool     cosmos_space_fabric = false;
    int      cosmos_background_preset = 0; // 0=realistic, 1=deep black, 2=nebula, 3=warm dust, 4=blue haze,
                                            // 5=aurora veil, 6=crimson rift, 7=galactic core, 8=monochrome,
                                            // 9=emerald sea, 10=infrared dust, 11=deep field
    int      cosmos_quality = 3;        // 0=low, 1=balanced, 2=high, 3=ultra
    float    cosmos_space_fabric_grid_size = 40.0f;
    float    cosmos_space_fabric_strength = 1.0f;

    // General Relativity corrections
    bool     gr_enabled           = true;    // enable GR corrections
    float    gr_precession_scale  = 1.0f;    // perihelion precession strength (1 = physical)
    float    gr_time_dilation     = 1.0f;    // gravitational time dilation factor
    float    gr_frame_dragging    = 1.0f;    // Lense-Thirring frame dragging
    float    speed_of_light       = 300.0f;  // c in simulation units (orbital speeds ~1-30)

    // Physics parallelization
    bool     parallel_gravity     = true;    // parallelize pairwise gravity accumulation
    bool     velocity_verlet      = true;    // use Velocity Verlet integration
    bool     barnes_hut           = true;    // use Barnes-Hut gravity approximation at scale
    bool     gpu_barnes_hut       = false;   // evaluate Barnes-Hut acceleration with Vulkan compute
    int      nebula_render_mode   = 1;       // 0=raymarch, 1=raymarch+compute, 2=advanced particles
    float    nebula_gravity_advection_scale = 0.020f; // coupling of gravity acceleration into nebula advection
    float    nebula_gravity_collapse_scale  = 0.045f; // extra collapse amplification from gravity field
    float    nebula_gravity_compress_scale  = 0.220f; // density compression response to |gravity|
    bool     nebula_sink_formation = true;   // allow dense converging nebula cores to spawn protostars
    float    nebula_sink_threshold = 1.05f;  // collapse metric threshold for sink spawning
    float    nebula_sink_min_mass  = 2.0e-4f; // minimum sink/star spawn mass
    float    nebula_sink_spawn_fraction = 0.018f; // nominal host-mass fraction converted to each sink
    float    nebula_sink_consume_fraction = 0.95f; // fraction of sink mass consumed from host cloud
    float    barnes_hut_theta     = 0.72f;   // opening angle (smaller = more accurate)
    int      barnes_hut_min_bodies = 128;    // body-count threshold before BH engages
};

// ── Body collection ─────────────────────────────────────────────────────────

struct CosmosState {
    std::vector<CelestialBody> bodies;
    std::vector<std::deque<glm::vec3>> trails;

    void clear() { bodies.clear(); trails.clear(); }
    size_t count() const { return bodies.size(); }
};

inline float nearest_star_distance(const CelestialBody& b, const CosmosState* state) {
    if (!state) return 1.0e9f;
    float best = 1.0e9f;
    for (const auto& other : state->bodies) {
        if (&other == &b || !is_star_type(other.type)) continue;
        float d = glm::length(other.pos - b.pos);
        if (d > 1.0e-3f && d < best) best = d;
    }
    return best;
}

struct MagneticSignature {
    bool  has_magnetosphere = false;
    float magnetic_field = 0.0f;
    float magnetosphere_size = 0.0f;
    bool  show_axis = false;
    float axis_angle = 0.0f;
    float particle_trapping = 0.0f;
    bool  particle_jets = false;
    bool  pulsar = false;
};

inline float body_density_estimate(const CelestialBody& b) {
    float volume = (4.0f / 3.0f) * 3.14159265359f * b.radius * b.radius * b.radius;
    return b.mass / std::max(volume, 1.0e-5f);
}

inline float estimate_black_hole_feeding(const CelestialBody& b, const CosmosState* state) {
    if (!state || !is_black_hole_type(b.type)) return 0.0f;
    float feed = 0.0f;
    float capture_radius = std::max(b.radius * 42.0f, 20.0f);
    for (const auto& other : state->bodies) {
        if (&other == &b || other.marked_for_removal || is_black_hole_type(other.type)) continue;
        float dist = glm::length(other.pos - b.pos);
        if (dist <= b.radius || dist > capture_radius) continue;
        float donor = 0.18f;
        if (is_star_type(other.type)) donor = 0.95f;
        else if (other.type == CTYPE_COMET) donor = 0.60f;
        else if ((other.type == CTYPE_PLANET || other.type == CTYPE_MOON) && other.cached_props.surface == SURF_GAS)
            donor = 0.90f;
        else if (other.type == CTYPE_PLANET || other.type == CTYPE_MOON)
            donor = 0.35f;
        float normalized = 1.0f - dist / capture_radius;
        feed += donor * std::sqrt(std::max(other.mass, 1.0e-6f)) * normalized * normalized;
    }
    return std::clamp(feed * 1.9f, 0.0f, 2.5f);
}

inline MagneticSignature estimate_magnetic_signature(const CelestialBody& b, float G = 1.0f) {
    MagneticSignature ms{};
    float density = body_density_estimate(b);
    float spin = std::abs(b.angular_vel);
    ms.axis_angle = hash_float(hash_combine(b.seed, 700u)) * 1.57079632679f;

    if (is_star_type(b.type)) {
        constexpr float TAU = 6.28318530718f;
        constexpr float SOLAR_SPIN = TAU / (26.0f * 24.0f * 3600.0f);
        constexpr float SOLAR_RADIUS_SIM = 872.8f; // 109.1 Earth radii at 8 units / R_earth
        float solar_volume = (4.0f / 3.0f) * 3.14159265359f *
                             SOLAR_RADIUS_SIM * SOLAR_RADIUS_SIM * SOLAR_RADIUS_SIM;
        float solar_density = 1.0f / std::max(solar_volume, 1.0e-6f);
        float density_rel = density / std::max(solar_density, 1.0e-12f);
        float spin_norm = std::clamp(spin / std::max(SOLAR_SPIN, 1.0e-8f), 0.0f, 2000.0f);
        float convective = std::clamp((6500.0f - b.temperature) / 4300.0f, 0.0f, 1.0f);
        float hot_wind = std::clamp((b.temperature - 9000.0f) / 26000.0f, 0.0f, 1.0f);
        float luminosity_term = std::clamp(std::log2(std::max(b.luminosity + 1.0f, 1.0f)) / 9.0f, 0.0f, 2.0f);
        float giant = (b.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_AGB ||
                       b.stellar_stage == SSTAGE_SUPERGIANT || b.stellar_stage == SSTAGE_HYPERGIANT) ? 1.0f : 0.0f;
        float compact = (b.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_NEUTRON_STAR) ? 1.0f : 0.0f;
        float compact_boost = (b.stellar_stage == SSTAGE_NEUTRON_STAR) ? 46.0f :
                              (b.stellar_stage == SSTAGE_WHITE_DWARF ? 8.0f : 1.0f);
        float dyn = (0.08f + convective * 0.95f + hot_wind * 0.35f) *
                    std::pow(std::max(spin_norm, 0.08f), 0.44f) *
                    std::pow(std::max(density_rel, 0.02f), 0.22f);
        dyn *= compact_boost;
        dyn *= (giant > 0.5f) ? 0.45f : 1.0f;
        dyn += luminosity_term * 0.25f + (1.0f - std::clamp(b.fuel, 0.0f, 1.0f)) * 0.20f;

        ms.magnetic_field = std::clamp(dyn, 0.0f, compact > 0.5f ? 120.0f : 24.0f);
        float extent_mult = 4.5f + std::pow(std::max(ms.magnetic_field, 0.01f), 0.52f) *
                            (compact > 0.5f ? 1.45f : 1.08f);
        float max_mult = compact > 0.5f ? 180.0f : 28.0f;
        ms.magnetosphere_size = b.radius * std::clamp(extent_mult, 3.5f, max_mult);
        ms.has_magnetosphere = true;
        ms.show_axis = true;
        ms.particle_trapping = std::clamp(ms.magnetic_field * (compact > 0.5f ? 0.045f : 0.09f),
                                          0.0f, compact > 0.5f ? 2.2f : 1.6f);
        if (b.stellar_stage == SSTAGE_NEUTRON_STAR || b.stellar_stage == SSTAGE_WHITE_DWARF) {
            ms.particle_jets = ms.magnetic_field > (b.stellar_stage == SSTAGE_NEUTRON_STAR ? 15.0f : 6.0f);
            ms.pulsar = (b.stellar_stage == SSTAGE_NEUTRON_STAR) && ms.particle_jets && spin > 0.01f;
        }
        return ms;
    }

    if (b.type != CTYPE_PLANET && b.type != CTYPE_MOON) return ms;

    const PlanetProperties& pp = b.cached_props;
    bool gas_dynamo = pp.planet_class == PCLASS_GAS_GIANT || pp.planet_class == PCLASS_ICE_GIANT;
    bool iron_core = pp.has_iron_core && (pp.surface == SURF_ROCKY || pp.surface == SURF_MIXED || pp.surface == SURF_FROZEN);
    if (!gas_dynamo && !iron_core) return ms;

    float mass_term = std::sqrt(std::max(b.mass, 0.0f));
    float radius_term = std::pow(std::max(b.radius, 0.1f), 0.55f);
    float field_driver = gas_dynamo ? 1.10f : 0.72f;
    field_driver += iron_core ? 0.35f : 0.0f;
    field_driver += std::clamp(pp.atmosphere.pressure * 0.005f, 0.0f, 0.4f);
    ms.magnetic_field = std::clamp(field_driver * mass_term * radius_term *
                                   (0.16f + spin * 520.0f) * (0.55f + density * 2.8f), 0.0f, 9.5f);
    float gravity = G * b.mass / std::max(b.radius * b.radius, 1.0e-6f);
    float extent = std::pow(std::max(ms.magnetic_field, 0.01f), 0.33f) *
                   (1.3f + gravity * 2.1f + std::clamp(pp.atmosphere.pressure * 0.015f, 0.0f, 0.8f));
    float max_mult = gas_dynamo ? 22.0f : (b.type == CTYPE_MOON ? 8.0f : 12.0f);
    ms.magnetosphere_size = b.radius * std::clamp(1.65f + extent, 1.8f, max_mult);
    ms.has_magnetosphere = ms.magnetic_field > 0.03f;
    ms.show_axis = ms.has_magnetosphere;
    ms.particle_trapping = std::clamp(ms.magnetic_field * 0.16f + (gas_dynamo ? 0.35f : 0.0f), 0.0f, 1.5f);
    return ms;
}

inline float estimate_stellar_luminosity_units(const CelestialBody& b) {
    if (!is_star_type(b.type)) return 0.0f;
    constexpr float SOLAR_RADIUS_SIM = 872.8f;
    float r_solar = std::max(b.radius / SOLAR_RADIUS_SIM, 1.0e-4f);
    float stefan = r_solar * r_solar * std::pow(std::max(b.temperature, 100.0f) / 5778.0f, 4.0f);
    float mass_lum;
    if (b.mass < 0.43f) mass_lum = 0.23f * std::pow(std::max(b.mass, 0.01f), 2.3f);
    else if (b.mass < 2.0f) mass_lum = std::pow(b.mass, 4.0f);
    else if (b.mass < 20.0f) mass_lum = 1.5f * std::pow(b.mass, 3.5f);
    else mass_lum = 3200.0f * std::pow(std::max(b.mass, 20.0f) / 20.0f, 2.2f);
    return std::max(std::max(stefan, mass_lum), std::max(b.luminosity, 0.0f));
}

inline glm::vec3 estimate_magnetic_axis_dir(uint32_t seed) {
    float tilt = hash_float(hash_combine(seed, 700u)) * 1.57079632679f;
    float phase = hash_float(hash_combine(seed, 701u)) * 6.28318530718f;
    glm::vec3 axis(std::cos(phase) * std::sin(tilt),
                   std::cos(tilt),
                   std::sin(phase) * std::sin(tilt));
    if (glm::dot(axis, axis) < 1.0e-6f) axis = glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(axis);
}

inline float estimate_stellar_wind_flux(const CelestialBody& source, const CelestialBody& target,
                                        float G = 1.0f) {
    float dist = glm::length(source.pos - target.pos);
    if (dist <= 1.0e-3f) return 0.0f;
    MagneticSignature sig = estimate_magnetic_signature(source, G);
    float flare = std::max(source.cached_visuals.flare_activity, 0.0f);
    float corona = std::max(source.cached_visuals.corona_strength, 0.0f);
    float activity = 0.55f + flare * 0.85f + corona * 0.35f + sig.magnetic_field * 0.04f;
    float luminosity = std::sqrt(std::max(estimate_stellar_luminosity_units(source), 0.0f) + 1.0f);
    return activity * luminosity / std::max(dist * dist, source.radius * source.radius * 4.0f);
}

inline float estimate_black_hole_radiation_flux(const CelestialBody& source, const CelestialBody& target,
                                                const CosmosState* state) {
    float dist = glm::length(source.pos - target.pos);
    if (dist <= 1.0e-3f) return 0.0f;
    float feeding = estimate_black_hole_feeding(source, state);
    if (feeding <= 0.01f) return 0.0f;
    glm::vec3 axis = estimate_magnetic_axis_dir(source.seed);
    glm::vec3 dir = glm::normalize(target.pos - source.pos);
    float alignment = 0.22f + 0.78f * std::pow(std::abs(glm::dot(dir, axis)), 3.0f);
    float base = 0.35f + feeding * 1.15f + std::max(source.cached_visuals.jet_strength, 0.0f) * 0.65f;
    return base * alignment / std::max(dist * dist, source.radius * source.radius * 9.0f);
}

inline float estimate_space_weather_flux(const CelestialBody& target, const CosmosState* state,
                                         float G = 1.0f, float* stellar_out = nullptr,
                                         float* bh_out = nullptr) {
    float stellar_flux = 0.0f;
    float bh_flux = 0.0f;
    if (state) {
        for (const auto& source : state->bodies) {
            if (&source == &target || source.marked_for_removal) continue;
            if (is_star_type(source.type))
                stellar_flux += estimate_stellar_wind_flux(source, target, G);
            else if (is_black_hole_type(source.type))
                bh_flux += estimate_black_hole_radiation_flux(source, target, state);
        }
    }
    if (stellar_out) *stellar_out = stellar_flux;
    if (bh_out) *bh_out = bh_flux;
    return stellar_flux + bh_flux * 1.35f;
}

inline BodyVisualProperties generate_body_visual_properties(const CelestialBody& b,
                                                            const CosmosState* state = nullptr) {
    BodyVisualProperties vp;
    uint32_t h = hash_combine(b.seed, float_bits(b.mass));
    float h0 = hash_float(hash_combine(h, 0));
    float h1 = hash_float(hash_combine(h, 1));
    float h2 = hash_float(hash_combine(h, 2));
    float spin_mag = std::min(std::abs(b.angular_vel) * 50000.0f, 1.0f);
    float temp_n = std::clamp((b.temperature - 60.0f) / 6000.0f, 0.0f, 1.0f);

    if (is_star_type(b.type)) {
        vp.render_class = RENDER_STAR;
        vp.subtype = (uint8_t)std::min<int>(b.type, 255);
        float cool_factor = std::clamp((6500.0f - b.temperature) / 4300.0f, 0.0f, 1.0f);
        float evolved_factor = (b.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_AGB)
            ? 1.0f : 0.0f;
        float compact_factor = (b.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_NEUTRON_STAR)
            ? 1.0f : 0.0f;
        float giant_factor = (b.stellar_stage == SSTAGE_SUPERGIANT || b.stellar_stage == SSTAGE_HYPERGIANT)
            ? 1.0f : 0.0f;
        float spent_fuel = 1.0f - std::clamp(b.fuel, 0.0f, 1.0f);
        float hot_factor = std::clamp((b.temperature - 9000.0f) / 26000.0f, 0.0f, 1.0f);
        float subtype_wr = (b.type == CTYPE_STAR_WR) ? 1.0f : 0.0f;
        float subtype_cool = (b.type == CTYPE_STAR_M || b.type == CTYPE_STAR_K || b.type == CTYPE_STAR_G)
            ? 1.0f : 0.0f;
        float subtype_hot = (b.type == CTYPE_STAR_O || b.type == CTYPE_STAR_B || b.type == CTYPE_STAR_WR)
            ? 1.0f : 0.0f;
        vp.roughness = 0.92f;
        vp.specular = 0.0f;
        vp.normal_strength = 0.22f + h0 * 0.46f + evolved_factor * 0.18f + giant_factor * 0.10f;
        vp.terrain_amp = 0.06f + (1.0f - temp_n) * 0.16f + evolved_factor * 0.12f + giant_factor * 0.14f;
        vp.terrain_freq = 4.0f + h1 * 22.0f + compact_factor * 12.0f + subtype_wr * 7.0f;
        vp.ridge_amp = subtype_cool > 0.5f
            ? (0.24f + h2 * 0.58f)
            : (0.03f + h2 * 0.16f + subtype_wr * 0.22f);
        vp.rock_frac = 0.02f + h0 * 0.18f;
        vp.star_spot_coverage = std::clamp(0.06f + cool_factor * 0.34f + spin_mag * 0.15f +
            spent_fuel * 0.10f + h1 * 0.12f - subtype_hot * 0.08f, 0.01f, 0.66f);
        vp.star_flare_frequency = 0.60f + spin_mag * 1.9f + h2 * 0.9f + compact_factor * 2.6f +
                                  subtype_wr * 1.3f + subtype_hot * 0.45f;
        vp.star_pulsation = std::clamp(evolved_factor * (0.24f + h0 * 0.36f) +
            giant_factor * (0.20f + h2 * 0.20f) +
            compact_factor * (0.24f + h1 * 0.28f) +
            subtype_wr * 0.22f + spent_fuel * 0.08f, 0.0f, 1.0f);
        vp.star_differential_rotation = std::clamp(0.14f + h2 * 0.42f + spin_mag * 0.32f +
            subtype_hot * 0.12f - giant_factor * 0.10f, 0.05f, 1.0f);
        vp.flare_activity = std::clamp(0.20f + b.mass * 0.022f + spin_mag * 0.62f +
            hot_factor * 0.35f + subtype_wr * 0.45f + spent_fuel * 0.18f +
            compact_factor * 0.22f + h0 * 0.14f, 0.08f, 2.0f);
        vp.corona_strength = std::clamp(0.30f + temp_n * 0.95f + subtype_hot * 0.28f +
            evolved_factor * 0.28f + giant_factor * 0.22f + compact_factor * 0.35f +
            spent_fuel * 0.10f, 0.12f, 2.1f);
        vp.spin_visual = spin_mag;
        return vp;
    }

    if (is_black_hole_type(b.type)) {
        vp.render_class = RENDER_BLACK_HOLE;
        vp.subtype = (uint8_t)std::min<int>(b.type, 255);
        vp.roughness = 0.02f;
        vp.specular = 0.0f;
        vp.normal_strength = 0.0f;
        float feeding = estimate_black_hole_feeding(b, state);
        vp.accretion_strength = std::clamp(0.12f + h0 * 0.28f + feeding * 0.85f +
            (b.type == CTYPE_BH_INTERMEDIATE ? 0.15f : 0.0f) +
            (b.type == CTYPE_BH_SUPERMASSIVE ? 0.30f : 0.0f), 0.0f, 1.8f);
        vp.jet_strength = std::clamp(spin_mag * (0.4f + h1 * 0.6f) + feeding * 0.35f, 0.0f, 1.3f);
        vp.lensing_strength = std::clamp(0.4f + h2 * 0.35f +
            (b.type == CTYPE_BH_SUPERMASSIVE ? 0.25f : 0.0f), 0.0f, 1.2f);
        vp.spin_visual = spin_mag;
        vp.metal_frac = 0.75f;
        vp.dust_frac = 0.25f;
        return vp;
    }

    if (b.type == CTYPE_NEBULA) {
        vp.render_class = RENDER_NEBULA;
        vp.subtype = (uint8_t)(hash_combine(b.seed, 0x4E454255u) & 7u); // random palette index
        vp.roughness = 1.0f;
        vp.specular = 0.0f;
        vp.normal_strength = 0.05f;
        vp.terrain_amp = 0.02f + h0 * 0.08f;
        vp.terrain_freq = 0.8f + h1 * 1.6f;
        vp.ridge_amp = 0.01f + h2 * 0.04f;
        vp.rock_frac = 0.01f;
        vp.ice_frac = (b.temperature < 120.0f) ? (0.08f + h0 * 0.12f) : (0.01f + h0 * 0.03f);
        vp.metal_frac = 0.005f + h1 * 0.025f + std::clamp(b.collapse_progress, 0.0f, 1.0f) * 0.06f;
        vp.dust_frac = 0.55f + h2 * 0.30f;
        vp.haze_density = 1.10f;
        vp.rayleigh_strength = 0.22f;
        vp.mie_strength = 1.30f;
        vp.cloud_detail = 0.95f;
        vp.weather_strength = 0.86f + std::clamp(b.collapse_progress, 0.0f, 1.0f) * 0.20f;
        vp.spin_visual = spin_mag;
        return vp;
    }

    if (b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST) {
        bool is_comet = b.type == CTYPE_COMET;
        bool is_dust = b.type == CTYPE_DUST;
        float impact_damage = std::clamp(b.impact_crater_strength, 0.0f, 1.0f);
        float impact_heat = std::clamp(b.impact_heat, 0.0f, 1.0f);
        vp.render_class = is_comet ? RENDER_COMET : RENDER_ASTEROID;
        vp.subtype = is_comet ? (uint8_t)SMALLBODY_ICY : (uint8_t)(hash_combine(h, 9) % 4u);
        vp.terrain_amp = is_dust ? 0.05f + h0 * 0.08f : (is_comet ? 0.24f + h0 * 0.14f : 0.34f + h0 * 0.26f);
        vp.terrain_freq = is_dust ? (8.0f + h1 * 9.0f) : (is_comet ? 2.4f + h1 * 3.6f : 3.8f + h1 * 6.8f);
        vp.ridge_amp = is_dust ? (0.03f + h2 * 0.08f) : (is_comet ? 0.16f + h2 * 0.24f : 0.34f + h2 * 0.46f);
        vp.crater_density = is_dust ? (0.15f + h1 * 0.20f) : (is_comet ? 0.25f + h1 * 0.25f : 0.55f + h1 * 0.35f);
        vp.normal_strength = is_dust ? 0.65f : (is_comet ? 1.2f : 1.0f);
        vp.roughness = is_dust ? 0.95f : 0.78f;
        vp.specular = is_dust ? 0.02f : 0.05f;
        vp.rock_frac = is_dust ? 0.45f : 0.4f;
        vp.ice_frac = is_dust ? 0.35f : 0.0f;
        vp.metal_frac = is_dust ? 0.20f : 0.05f;
        vp.dust_frac = is_dust ? 0.85f : 0.55f;

        if (!is_comet && !is_dust) {
            switch ((SmallBodyClass)vp.subtype) {
            case SMALLBODY_C:
                vp.rock_frac = 0.35f; vp.metal_frac = 0.05f; vp.dust_frac = 0.60f; vp.roughness = 0.92f; break;
            case SMALLBODY_S:
                vp.rock_frac = 0.70f; vp.metal_frac = 0.10f; vp.dust_frac = 0.20f; vp.roughness = 0.82f; break;
            case SMALLBODY_M:
                vp.rock_frac = 0.20f; vp.metal_frac = 0.70f; vp.dust_frac = 0.10f; vp.roughness = 0.45f; vp.specular = 0.18f; break;
            case SMALLBODY_ICY:
                vp.rock_frac = 0.20f; vp.ice_frac = 0.65f; vp.metal_frac = 0.05f; vp.dust_frac = 0.10f; vp.roughness = 0.62f; break;
            }
        } else {
            float star_dist = nearest_star_distance(b, state);
            float heat = std::clamp((320.0f - star_dist) / 320.0f, 0.0f, 1.0f) * 0.7f +
                         std::clamp((b.temperature - 120.0f) / 500.0f, 0.0f, 1.0f) * 0.6f;
            vp.rock_frac = 0.15f;
            vp.ice_frac = 0.70f;
            vp.metal_frac = 0.02f;
            vp.dust_frac = 0.55f;
            vp.coma_strength = std::clamp(heat * (0.55f + h1 * 0.45f), 0.0f, 1.0f);
            vp.tail_strength = std::clamp(heat * (0.65f + h2 * 0.5f), 0.0f, 1.2f);
        }
        vp.crater_density = std::clamp(vp.crater_density + impact_damage * 0.40f, 0.0f, 1.0f);
        vp.volcanic_activity = std::max(vp.volcanic_activity, impact_heat * 0.70f);
        return vp;
    }

    if (b.type == CTYPE_MOON || b.type == CTYPE_PLANET) {
        const PlanetProperties& pp = b.cached_props;
        float impact_damage = std::clamp(b.impact_crater_strength, 0.0f, 1.0f);
        float impact_heat = std::clamp(b.impact_heat, 0.0f, 1.0f);
        vp.render_class = (b.type == CTYPE_MOON) ? RENDER_MOON : RENDER_PLANET;
        vp.subtype = (uint8_t)pp.surface;
        vp.terrain_amp = std::clamp(pp.mountain_height / 18.0f + pp.valley_depth / 24.0f, 0.08f, 1.0f);
        vp.terrain_freq = 2.0f + pp.continent_count * 0.7f + h0 * 3.0f;
        vp.ridge_amp = pp.has_mountains ? (0.25f + h1 * 0.45f) : (0.08f + h1 * 0.15f);
        vp.crater_density = (pp.atmosphere.pressure > 0.05f) ? 0.08f : 0.35f;
        vp.roughness = 0.82f;
        vp.specular = 0.05f;
        vp.normal_strength = 1.0f;
        vp.rock_frac = 0.65f;
        vp.ice_frac = 0.0f;
        vp.metal_frac = 0.05f;
        vp.dust_frac = 0.18f;
        vp.haze_density = std::clamp(pp.atmosphere.pressure * 0.08f, 0.0f, 1.0f);
        vp.rayleigh_strength = std::clamp(
            pp.atmosphere.n2_frac * 0.8f + pp.atmosphere.o2_frac * 1.0f + pp.atmosphere.h2_frac * 0.25f, 0.0f, 1.0f);
        vp.mie_strength = std::clamp(
            pp.atmosphere.co2_frac * 0.7f + pp.atmosphere.ch4_frac * 0.8f + pp.atmosphere.nh3_frac * 0.65f +
            pp.atmosphere.pressure * 0.02f, 0.0f, 1.2f);
        vp.cloud_detail = std::clamp(pp.cloud_coverage / 100.0f, 0.0f, 1.0f);
        vp.weather_strength = std::clamp(pp.atmosphere.weather_intensity, 0.0f, 1.0f);
        MagneticSignature magnetic = estimate_magnetic_signature(b);
        vp.aurora_strength = (pp.atmosphere.pressure > 0.08f && magnetic.has_magnetosphere)
            ? std::clamp(0.10f + magnetic.magnetic_field * 0.08f + h2 * 0.15f, 0.0f, 1.1f) : 0.0f;
        vp.volcanic_activity = (pp.ocean_type == OCEAN_LAVA || (b.temperature > 700.0f && pp.surface == SURF_ROCKY))
            ? std::clamp(0.35f + h0 * 0.55f, 0.0f, 1.0f) : 0.0f;
        vp.spin_visual = spin_mag;
        vp.continent_coverage = std::clamp(pp.continent_coverage / 100.0f, 0.0f, 1.0f);
        vp.island_coverage = std::clamp(pp.island_coverage / 100.0f, 0.0f, 1.0f);
        vp.river_density = std::clamp(pp.river_density, 0.0f, 1.0f);
        vp.ice_sheet_coverage = std::clamp(pp.ice_sheet_coverage / 100.0f, 0.0f, 1.0f);

        switch (pp.surface) {
        case SURF_ROCKY:
            vp.rock_frac = 0.75f;
            vp.dust_frac = 0.18f;
            vp.roughness = 0.88f;
            vp.terrain_amp = std::clamp(vp.terrain_amp + 0.10f, 0.16f, 1.0f);
            vp.terrain_freq = std::clamp(vp.terrain_freq + 1.2f, 2.5f, 12.0f);
            vp.ridge_amp = std::clamp(vp.ridge_amp + 0.10f, 0.15f, 1.0f);
            break;
        case SURF_LIQUID:
            vp.rock_frac = 0.20f;
            vp.dust_frac = 0.08f;
            vp.specular = 0.12f;
            vp.roughness = 0.22f;
            vp.terrain_amp = std::clamp(vp.terrain_amp * 0.75f, 0.08f, 0.70f);
            vp.ridge_amp = std::clamp(vp.ridge_amp * 0.75f, 0.04f, 0.85f);
            break;
        case SURF_FROZEN:
            vp.rock_frac = 0.25f;
            vp.ice_frac = 0.65f;
            vp.roughness = 0.45f;
            vp.specular = 0.10f;
            vp.terrain_freq = std::clamp(vp.terrain_freq + 2.2f, 3.0f, 16.0f);
            vp.ridge_amp = std::clamp(vp.ridge_amp + 0.08f, 0.12f, 1.0f);
            break;
        case SURF_GAS:
            vp.rock_frac = 0.0f;
            vp.ice_frac = 0.0f;
            vp.dust_frac = 0.55f;
            vp.roughness = 1.0f;
            vp.normal_strength = 0.3f;
            vp.terrain_amp = std::clamp(0.06f + h0 * 0.14f, 0.05f, 0.26f);
            vp.terrain_freq = std::clamp(8.0f + h1 * 10.0f, 6.0f, 20.0f);
            vp.ridge_amp = std::clamp(0.04f + h2 * 0.18f, 0.02f, 0.34f);
            break;
        case SURF_MIXED:
            vp.rock_frac = 0.55f;
            vp.dust_frac = 0.15f;
            vp.roughness = 0.75f;
            vp.terrain_amp = std::clamp(vp.terrain_amp + 0.06f, 0.12f, 1.0f);
            vp.terrain_freq = std::clamp(vp.terrain_freq + 0.8f, 2.2f, 12.0f);
            break;
        }

        if (pp.surface == SURF_MIXED &&
            b.temperature >= 240.0f && b.temperature <= 330.0f &&
            pp.ocean_coverage >= 20.0f && pp.ocean_coverage <= 80.0f) {
            vp.terrain_amp = std::clamp(vp.terrain_amp + 0.08f, 0.16f, 1.0f);
            vp.ridge_amp = std::clamp(vp.ridge_amp + 0.10f, 0.12f, 1.0f);
            vp.cloud_detail = std::max(vp.cloud_detail, 0.45f);
            vp.weather_strength = std::max(vp.weather_strength, 0.38f);
        }

        switch (pp.ocean_type) {
        case OCEAN_WATER:   vp.ice_frac += 0.10f; vp.specular = std::max(vp.specular, 0.14f); break;
        case OCEAN_METHANE: vp.dust_frac += 0.06f; vp.specular = std::max(vp.specular, 0.11f); break;
        case OCEAN_AMMONIA: vp.ice_frac += 0.18f; break;
        case OCEAN_LAVA:    vp.metal_frac += 0.12f; vp.volcanic_activity = std::max(vp.volcanic_activity, 0.55f); break;
        default: break;
        }

        switch (pp.planet_class) {
        case PCLASS_DWARF:
            vp.crater_density = std::clamp(vp.crater_density + 0.28f, 0.0f, 1.0f);
            vp.terrain_amp *= 0.72f;
            vp.roughness = std::min(1.0f, vp.roughness + 0.06f);
            break;
        case PCLASS_OCEAN:
            vp.specular = std::max(vp.specular, 0.16f);
            vp.roughness = std::max(0.16f, vp.roughness - 0.14f);
            vp.cloud_detail = std::max(vp.cloud_detail, 0.45f);
            break;
        case PCLASS_SUPER_EARTH:
            vp.terrain_amp = std::clamp(vp.terrain_amp + 0.08f, 0.10f, 1.0f);
            vp.ridge_amp = std::clamp(vp.ridge_amp + 0.10f, 0.0f, 1.0f);
            break;
        case PCLASS_ICE_GIANT:
            vp.dust_frac = 0.38f;
            vp.roughness = 0.94f;
            vp.normal_strength = 0.24f;
            vp.cloud_detail = std::max(vp.cloud_detail, 0.70f);
            vp.weather_strength = std::max(vp.weather_strength, 0.58f);
            break;
        case PCLASS_GAS_GIANT:
            vp.dust_frac = 0.58f;
            vp.roughness = 0.98f;
            vp.normal_strength = 0.22f;
            vp.cloud_detail = std::max(vp.cloud_detail, 0.78f);
            vp.weather_strength = std::max(vp.weather_strength, 0.72f);
            break;
        default:
            break;
        }

        if (pp.surface != SURF_GAS) {
            float ocean_frac = std::clamp(pp.ocean_coverage / 100.0f, 0.0f, 1.0f);
            float cloud_frac = std::clamp(pp.cloud_coverage / 100.0f, 0.0f, 1.0f);
            float vegetation_frac = std::clamp(pp.vegetation_coverage / 100.0f, 0.0f, 1.0f);
            float pressure_frac = std::clamp(pp.atmosphere.pressure / 12.0f, 0.0f, 1.0f);
            float cold_factor = std::clamp((240.0f - b.temperature) / 220.0f, 0.0f, 1.0f);
            float hot_factor = std::clamp((b.temperature - 420.0f) / 900.0f, 0.0f, 1.0f);
            float arid_factor = std::clamp(1.0f - ocean_frac * 0.95f - cloud_frac * 0.30f -
                                           vegetation_frac * 0.45f, 0.0f, 1.0f);
            float oxidized_factor = std::clamp(0.10f + h0 * 0.40f + hot_factor * 0.30f +
                                               arid_factor * 0.22f - ocean_frac * 0.12f,
                                               0.0f, 1.0f);

            vp.rock_frac = std::clamp(0.18f + h1 * 0.58f + oxidized_factor * 0.18f -
                                      vegetation_frac * 0.10f, 0.0f, 1.0f);
            vp.ice_frac = std::clamp(vp.ice_frac + cold_factor * (0.35f + h0 * 0.32f),
                                     0.0f, 1.0f);
            vp.metal_frac = std::clamp(vp.metal_frac + (pp.has_iron_core ? 0.18f : 0.03f) +
                                       h2 * 0.24f + hot_factor * 0.14f, 0.0f, 1.0f);
            vp.dust_frac = std::clamp(vp.dust_frac + 0.04f + arid_factor * 0.42f +
                                      hot_factor * 0.14f - ocean_frac * 0.20f,
                                      0.0f, 1.0f);
            vp.metallic = std::clamp(vp.metallic + (pp.has_iron_core ? 0.05f + h2 * 0.06f : 0.0f),
                                     0.0f, 0.25f);
            vp.specular = std::clamp(vp.specular + ocean_frac * 0.04f + pressure_frac * 0.02f,
                                     0.02f, 0.24f);
            vp.roughness = std::clamp(vp.roughness + arid_factor * 0.06f - ocean_frac * 0.10f -
                                      vegetation_frac * 0.04f, 0.15f, 1.0f);
        }

        if (b.type == CTYPE_MOON) {
            vp.crater_density = std::clamp(vp.crater_density + 0.35f, 0.0f, 1.0f);
            vp.haze_density *= 0.45f;
            vp.cloud_detail *= 0.35f;
            vp.weather_strength *= 0.25f;
            vp.specular *= (pp.surface == SURF_FROZEN) ? 1.15f : 0.65f;
            vp.roughness = std::clamp(vp.roughness + (pp.surface == SURF_FROZEN ? -0.10f : 0.06f), 0.15f, 1.0f);
            if (pp.surface == SURF_FROZEN) {
                vp.terrain_freq = std::clamp(vp.terrain_freq + 2.0f, 3.5f, 18.0f);
                vp.ridge_amp = std::clamp(vp.ridge_amp + 0.08f, 0.14f, 1.0f);
            }
        }
        vp.crater_density = std::clamp(vp.crater_density + impact_damage * 0.45f, 0.0f, 1.0f);
        vp.volcanic_activity = std::max(vp.volcanic_activity, impact_heat * 0.85f);
        vp.haze_density = std::clamp(vp.haze_density + b.impact_ejecta * 0.10f, 0.0f, 1.0f);
        return vp;
    }

    vp.render_class = RENDER_PLANET;
    return vp;
}

inline void refresh_body_visuals(CelestialBody& b, const CosmosState* state = nullptr) {
    if (state != nullptr) {
        b.cached_visuals = generate_body_visual_properties(b, state);
        b.cached_visual_temp_band = temp_band(b.temperature);
        b.visuals_valid = true;
        return;
    }
    int band = temp_band(b.temperature);
    if (b.visuals_valid && band == b.cached_visual_temp_band) return;
    b.cached_visuals = generate_body_visual_properties(b, state);
    b.cached_visual_temp_band = band;
    b.visuals_valid = true;
}
