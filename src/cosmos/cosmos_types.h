#pragma once
// ── Cosmic Sandbox — Data Types ─────────────────────────────────────────────

#include "common/orbit_camera.h"
#include <glm/glm.hpp>
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

enum BodyRenderClass : uint8_t {
    RENDER_STAR = 0,
    RENDER_PLANET = 1,
    RENDER_MOON = 2,
    RENDER_ASTEROID = 3,
    RENDER_COMET = 4,
    RENDER_BLACK_HOLE = 5,
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

inline PlanetProperties generate_planet_properties(uint32_t seed, float mass, float temperature) {
    PlanetProperties pp;

    // Hash only seed + mass (stable inputs). Temperature drives threshold
    // decisions but is NOT hashed — prevents per-frame flashing.
    uint32_t h = hash_combine(seed, float_bits(mass));
    // Secondary hashes for independent random streams
    float h0 = hash_float(hash_combine(h, 0));
    float h1 = hash_float(hash_combine(h, 1));
    float h2 = hash_float(hash_combine(h, 2));

    // ── Surface composition (broader ranges, more variety) ──
    if (mass > 8.0f) {
        pp.surface = SURF_GAS;
    } else if (mass > 5.0f) {
        // Mini-neptune zone: 70% gas, 30% mixed
        pp.surface = (h0 > 0.3f) ? SURF_GAS : SURF_MIXED;
    } else if (temperature < 180.0f) {
        // Cold: mostly frozen, small chance mixed/rocky
        if (h0 < 0.15f && mass > 0.3f)
            pp.surface = SURF_MIXED;  // subsurface ocean world
        else if (h0 < 0.25f)
            pp.surface = SURF_ROCKY;  // barren cold rock
        else
            pp.surface = SURF_FROZEN;
    } else if (temperature >= 250.0f && temperature <= 380.0f && mass >= 0.3f && mass <= 5.0f) {
        // Habitable zone: diverse
        if (h0 < 0.15f)
            pp.surface = SURF_LIQUID;  // water world
        else if (h0 < 0.55f)
            pp.surface = SURF_MIXED;   // earth-like continents
        else if (h0 < 0.80f)
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

    // ── Atmosphere ──
    uint32_t ha = hash_combine(h, 100);
    float ha0 = hash_float(hash_combine(ha, 0));
    if (pp.surface == SURF_GAS) {
        pp.atmosphere.h2_frac  = 0.70f + hash_float(hash_combine(ha, 1)) * 0.20f;
        pp.atmosphere.he_frac  = 1.0f - pp.atmosphere.h2_frac - 0.03f;
        pp.atmosphere.ch4_frac = hash_float(hash_combine(ha, 2)) * 0.03f;
        pp.atmosphere.pressure = 10.0f + hash_float(hash_combine(ha, 3)) * 200.0f;
    } else if (mass > 0.2f && temperature > 120.0f) {
        // Terrestrial atmosphere — wide variety
        float atm_chance = std::min(1.0f, mass * 0.8f); // bigger = more likely
        if (ha0 < atm_chance) {
            pp.atmosphere.n2_frac  = 0.1f + hash_float(hash_combine(ha, 4)) * 0.85f;
            pp.atmosphere.co2_frac = hash_float(hash_combine(ha, 5)) * 0.60f;
            if (temperature >= 230.0f && temperature <= 370.0f && h1 > 0.4f) {
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
            float base_p = 0.01f + mass * 0.5f;
            pp.atmosphere.pressure = base_p + hash_float(hash_combine(ha, 7)) * base_p * 4.0f;
            if (temperature > 400.0f && pp.atmosphere.co2_frac > 0.5f)
                pp.atmosphere.pressure = 20.0f + hash_float(hash_combine(ha, 14)) * 80.0f; // Venus-like
        }
        // else: airless (Mercury/Moon-like) — pressure stays 0
    }

    pp.atmosphere.has_clouds = (pp.atmosphere.pressure > 0.3f &&
                                hash_float(hash_combine(ha, 8)) > 0.25f);
    pp.atmosphere.weather_intensity = pp.atmosphere.has_clouds
        ? 0.2f + hash_float(hash_combine(ha, 9)) * 0.8f : 0.0f;

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
    } else if (temperature >= 250.0f && temperature <= 380.0f && mass >= 0.3f && ho0 > 0.3f) {
        pp.ocean_type     = OCEAN_WATER;
        pp.ocean_coverage = 15.0f + hash_float(hash_combine(ho, 1)) * 75.0f;
        pp.ocean_depth    = 0.5f + hash_float(hash_combine(ho, 2)) * 12.0f;
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

    // ── Terrain (not gas giants) ──
    uint32_t ht = hash_combine(h, 300);
    if (pp.surface != SURF_GAS) {
        pp.has_mountains   = hash_float(hash_combine(ht, 1)) > 0.25f;
        pp.has_valleys     = hash_float(hash_combine(ht, 2)) > 0.30f;
        pp.has_continents  = (pp.ocean_coverage > 15.0f && hash_float(hash_combine(ht, 3)) > 0.2f);
        pp.mountain_height = pp.has_mountains ? (0.5f + hash_float(hash_combine(ht, 4)) * 25.0f) : 0.0f;
        pp.valley_depth    = pp.has_valleys   ? (0.2f + hash_float(hash_combine(ht, 5)) * 12.0f) : 0.0f;
        pp.continent_count = pp.has_continents ? (1 + (int)(hash_float(hash_combine(ht, 6)) * 8.0f)) : 0;
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
        pp.cloud_coverage = 5.0f + hash_float(hash_combine(hc, 2)) * 85.0f;
        // Dry worlds: less clouds
        if (pp.ocean_coverage < 10.0f) pp.cloud_coverage *= 0.3f;
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
    float       angular_vel    = 0.0f;
    uint32_t    stellar_stage  = 0;         // StellarStage enum
    bool        marked_for_removal = false;
    uint32_t    seed           = 0;         // procedural generation seed
    uint32_t    frag_generation = 0;        // how many times this body has been fragmented (0 = original)
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
    b.cached_props = generate_planet_properties(b.seed, b.mass, b.temperature);
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
    int      fragment_count          = 4;        // number of fragments from collision/tidal breakup (1-12)
    float    min_fragment_mass       = 0.05f;    // bodies below this mass cannot fragment (just bounce)
    int      max_frag_generation     = 2;        // max times a body can be re-fragmented (0=originals only)

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
    bool     fast_star_lighting = true; // use strongest-star direct lighting path
    float    ambient_strength = 0.08f;  // ambient light level in star mode
    bool     cosmos_hq_shading = true;
    bool     cosmos_background_starfield = true;
    bool     cosmos_star_corona = true;
    bool     cosmos_comet_tails = true;
    bool     cosmos_blackhole_lensing = true;
    int      cosmos_quality = 1;        // 0=low, 1=balanced, 2=high

    // General Relativity corrections
    bool     gr_enabled           = true;    // enable GR corrections
    float    gr_precession_scale  = 1.0f;    // perihelion precession strength (1 = physical)
    float    gr_time_dilation     = 1.0f;    // gravitational time dilation factor
    float    gr_frame_dragging    = 1.0f;    // Lense-Thirring frame dragging
    float    speed_of_light       = 300.0f;  // c in simulation units (orbital speeds ~1-30)

    // Physics parallelization
    bool     parallel_gravity     = true;    // parallelize pairwise gravity accumulation
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
        vp.roughness = 0.92f;
        vp.specular = 0.0f;
        vp.normal_strength = 0.35f + h0 * 0.4f;
        vp.terrain_amp = 0.12f + (1.0f - temp_n) * 0.12f;
        vp.terrain_freq = 6.0f + h1 * 18.0f;
        vp.ridge_amp = (b.type == CTYPE_STAR_G || b.type == CTYPE_STAR_K || b.type == CTYPE_STAR_M)
            ? (0.25f + h2 * 0.55f) : (0.03f + h2 * 0.12f);
        vp.rock_frac = 0.02f + h0 * 0.18f;
        vp.flare_activity = std::clamp(0.15f + b.mass * 0.03f + spin_mag * 0.45f, 0.0f, 1.0f);
        vp.corona_strength = std::clamp(0.25f + temp_n * 0.85f +
            (b.stellar_stage == SSTAGE_RED_GIANT ? 0.15f : 0.0f) +
            (b.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_NEUTRON_STAR ? 0.25f : 0.0f),
            0.0f, 1.2f);
        vp.spin_visual = spin_mag;
        return vp;
    }

    if (is_black_hole_type(b.type)) {
        vp.render_class = RENDER_BLACK_HOLE;
        vp.subtype = (uint8_t)std::min<int>(b.type, 255);
        vp.roughness = 0.02f;
        vp.specular = 0.0f;
        vp.normal_strength = 0.0f;
        vp.accretion_strength = std::clamp(0.25f + h0 * 0.45f +
            (b.type == CTYPE_BH_INTERMEDIATE ? 0.15f : 0.0f) +
            (b.type == CTYPE_BH_SUPERMASSIVE ? 0.30f : 0.0f), 0.0f, 1.2f);
        vp.jet_strength = std::clamp(spin_mag * (0.4f + h1 * 0.6f), 0.0f, 1.0f);
        vp.lensing_strength = std::clamp(0.4f + h2 * 0.35f +
            (b.type == CTYPE_BH_SUPERMASSIVE ? 0.25f : 0.0f), 0.0f, 1.2f);
        vp.spin_visual = spin_mag;
        vp.metal_frac = 0.75f;
        vp.dust_frac = 0.25f;
        return vp;
    }

    if (b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET) {
        bool is_comet = b.type == CTYPE_COMET;
        vp.render_class = is_comet ? RENDER_COMET : RENDER_ASTEROID;
        vp.subtype = is_comet ? (uint8_t)SMALLBODY_ICY : (uint8_t)(hash_combine(h, 9) % 4u);
        vp.terrain_amp = is_comet ? 0.32f + h0 * 0.18f : 0.22f + h0 * 0.22f;
        vp.terrain_freq = 2.5f + h1 * 5.5f;
        vp.ridge_amp = 0.2f + h2 * 0.45f;
        vp.crater_density = is_comet ? 0.25f + h1 * 0.25f : 0.55f + h1 * 0.35f;
        vp.normal_strength = is_comet ? 1.2f : 1.0f;
        vp.roughness = 0.78f;
        vp.specular = 0.05f;
        vp.rock_frac = 0.4f;
        vp.ice_frac = 0.0f;
        vp.metal_frac = 0.05f;
        vp.dust_frac = 0.55f;

        if (!is_comet) {
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
        return vp;
    }

    if (b.type == CTYPE_MOON || b.type == CTYPE_PLANET) {
        const PlanetProperties& pp = b.cached_props;
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
        vp.aurora_strength = (pp.atmosphere.pressure > 0.15f && b.temperature > 120.0f && b.temperature < 320.0f)
            ? (0.08f + h2 * 0.25f) : 0.0f;
        vp.volcanic_activity = (pp.ocean_type == OCEAN_LAVA || (b.temperature > 700.0f && pp.surface == SURF_ROCKY))
            ? std::clamp(0.35f + h0 * 0.55f, 0.0f, 1.0f) : 0.0f;
        vp.spin_visual = spin_mag;

        switch (pp.surface) {
        case SURF_ROCKY:
            vp.rock_frac = 0.75f; vp.dust_frac = 0.18f; vp.roughness = 0.88f; break;
        case SURF_LIQUID:
            vp.rock_frac = 0.20f; vp.dust_frac = 0.08f; vp.specular = 0.12f; vp.roughness = 0.22f; break;
        case SURF_FROZEN:
            vp.rock_frac = 0.25f; vp.ice_frac = 0.65f; vp.roughness = 0.45f; vp.specular = 0.10f; break;
        case SURF_GAS:
            vp.rock_frac = 0.0f; vp.ice_frac = 0.0f; vp.dust_frac = 0.55f; vp.roughness = 1.0f; vp.normal_strength = 0.3f; break;
        case SURF_MIXED:
            vp.rock_frac = 0.55f; vp.dust_frac = 0.15f; vp.roughness = 0.75f; break;
        }

        switch (pp.ocean_type) {
        case OCEAN_WATER:   vp.ice_frac += 0.10f; vp.specular = std::max(vp.specular, 0.14f); break;
        case OCEAN_METHANE: vp.dust_frac += 0.06f; vp.specular = std::max(vp.specular, 0.11f); break;
        case OCEAN_AMMONIA: vp.ice_frac += 0.18f; break;
        case OCEAN_LAVA:    vp.metal_frac += 0.12f; vp.volcanic_activity = std::max(vp.volcanic_activity, 0.55f); break;
        default: break;
        }

        if (b.type == CTYPE_MOON) {
            vp.crater_density = std::clamp(vp.crater_density + 0.35f, 0.0f, 1.0f);
            vp.haze_density *= 0.45f;
            vp.cloud_detail *= 0.35f;
            vp.weather_strength *= 0.25f;
            vp.specular *= (pp.surface == SURF_FROZEN) ? 1.15f : 0.65f;
            vp.roughness = std::clamp(vp.roughness + (pp.surface == SURF_FROZEN ? -0.10f : 0.06f), 0.15f, 1.0f);
        }
        return vp;
    }

    vp.render_class = RENDER_PLANET;
    return vp;
}

inline void refresh_body_visuals(CelestialBody& b, const CosmosState* state = nullptr) {
    int band = temp_band(b.temperature);
    if (b.visuals_valid && band == b.cached_visual_temp_band) return;
    b.cached_visuals = generate_body_visual_properties(b, state);
    b.cached_visual_temp_band = band;
    b.visuals_valid = true;
}
