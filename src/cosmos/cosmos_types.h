#pragma once
// ── Cosmic Sandbox — Data Types ─────────────────────────────────────────────

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// ── Celestial body types ────────────────────────────────────────────────────

enum CelestialType : uint32_t {
    CTYPE_STAR      = 0,
    CTYPE_PLANET    = 1,
    CTYPE_MOON      = 2,
    CTYPE_ASTEROID  = 3,
    CTYPE_COMET     = 4,
    CTYPE_BLACK_HOLE = 5,
    CTYPE_NEBULA    = 6,
    CTYPE_COUNT
};

// ── Single celestial body ───────────────────────────────────────────────────

struct CelestialBody {
    glm::vec2   pos{0.0f};
    glm::vec2   vel{0.0f};
    float       mass       = 1.0f;      // solar masses
    float       radius     = 10.0f;     // display radius (pixels)
    float       temperature = 5778.0f;  // Kelvin (for star color)
    uint32_t    type       = CTYPE_PLANET;
    int32_t     parent     = -1;        // orbital parent index (-1 = none)
};

// ── Simulation config ───────────────────────────────────────────────────────

struct CosmosConfig {
    uint32_t body_count     = 100;
    float    G              = 1.0f;     // gravitational constant scale
    float    dt_scale       = 1.0f;     // time step multiplier
    float    softening      = 5.0f;     // gravity softening length
    float    damping        = 1.0f;     // velocity damping (1 = none)
    bool     collisions     = true;
    bool     tidal_forces   = false;
    bool     show_orbits    = true;
    bool     show_trails    = true;
};

// ── Body collection ─────────────────────────────────────────────────────────

struct CosmosState {
    std::vector<CelestialBody> bodies;

    void clear() { bodies.clear(); }
    size_t count() const { return bodies.size(); }
};
