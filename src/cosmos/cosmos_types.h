#pragma once
// ── Cosmic Sandbox — Data Types ─────────────────────────────────────────────

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <cmath>
#include <vector>
#include <deque>

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
    glm::vec3   pos{0.0f};
    glm::vec3   vel{0.0f};
    float       mass       = 1.0f;      // solar masses
    float       radius     = 10.0f;     // world-space radius
    float       temperature = 5778.0f;  // Kelvin (for star color)
    uint32_t    type       = CTYPE_PLANET;
    int32_t     parent     = -1;        // orbital parent index (-1 = none)
};

// ── Orbit camera ───────────────────────────────────────────────────────────

struct OrbitCamera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance  = 600.0f;
    float azimuth   = 0.0f;     // radians, horizontal rotation
    float elevation = 0.5f;     // radians, vertical angle
    float fov       = 45.0f;    // degrees
    float near_clip = 0.1f;
    float far_clip  = 10000.0f;

    glm::vec3 eye_position() const {
        float cos_el = std::cos(elevation);
        return target + glm::vec3(
            distance * cos_el * std::sin(azimuth),
            distance * std::sin(elevation),
            distance * cos_el * std::cos(azimuth)
        );
    }

    glm::mat4 view_matrix() const {
        return glm::lookAt(eye_position(), target, glm::vec3(0, 1, 0));
    }

    glm::mat4 proj_matrix(float aspect) const {
        return glm::perspective(glm::radians(fov), aspect, near_clip, far_clip);
    }
};

// ── Simulation config ───────────────────────────────────────────────────────

struct CosmosConfig {
    uint32_t body_count     = 100;
    float    G              = 1.0f;     // gravitational constant scale
    float    dt_scale       = 10.0f;    // time step multiplier
    float    softening      = 5.0f;     // gravity softening length
    float    damping        = 1.0f;     // velocity damping (1 = none)
    bool     collisions     = true;
    bool     tidal_forces   = false;
    bool     show_orbits    = true;
    bool     show_trails    = true;
    uint32_t trail_length   = 120;      // max trail points per body

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
