#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr uint32_t MAX_PARTICLE_TYPES = 10;
static constexpr uint32_t GROUP_DENSITY      = 256;

enum ParticleBehavior : uint32_t {
    BEHAVIOR_NONE     = 0,
    BEHAVIOR_REPEL    = 1u << 0,
    BEHAVIOR_POLAR    = 1u << 1,
    BEHAVIOR_HEAVY    = 1u << 2,
    BEHAVIOR_CATALYST = 1u << 3,
    BEHAVIOR_VIRAL    = 1u << 4
};

struct PushConstants {
    glm::vec2 region_size;        // 0
    glm::vec2 camera_origin;      // 8
    uint32_t  particle_count;     // 16
    uint32_t  particle_types;     // 20
    uint32_t  step;               // 24
    float     dt;                 // 28
    float     camera_zoom;        // 32
    float     radius;             // 36
    float     dampening;          // 40
    float     repulsion_radius;   // 44
    float     interaction_radius; // 48
    float     density_limit;      // 52
    float     viscosity_strength; // 56
    float     pressure_resistance;// 60
    float     local_density_cap;  // 64
};
// Size is 68 bytes
static_assert(sizeof(PushConstants) == 68, "PushConstants layout mismatch");

struct SimConfig {
    uint32_t particle_count     = 22500;
    uint32_t particle_types     = 5;
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    uint32_t generation_seed    = 0;

    float radius             = 2.0f;
    float dampening          = 0.85f;
    float repulsion_radius   = 20.0f;
    float interaction_radius = 60.0f;
    float density_limit      = 60.0f;

    // New Soft-Body Parameters
    float viscosity_strength  = 0.15f;
    float pressure_resistance = 25.0f;
    float local_density_cap   = 1.0f;

    glm::vec2 camera_origin      = { REGION_W / 2.0f, REGION_H / 2.0f };
    float     camera_zoom        = 1.0f;
    float     current_camera_zoom = 1.0f;
};

// ── Colour helpers ────────────────────────────────────────────────────────────

inline glm::vec4 color_from_hsv(float h, float s, float v, float a = 1.0f) {
    if (s <= 0.0f) return { v, v, v, a };
    float hh = h * 6.0f;
    int   i  = (int)hh;
    float ff = hh - (float)i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * ff);
    float t  = v * (1.0f - s * (1.0f - ff));
    switch (i % 6) {
        case 0: return { v, t, p, a };
        case 1: return { q, v, p, a };
        case 2: return { p, v, t, a };
        case 3: return { p, q, v, a };
        case 4: return { t, p, v, a };
        default: return { v, p, q, a };
    }
}

inline glm::vec4 calc_force_button_color(float force) {
    float abs_f = std::abs(force);
    if (force < 0.0f)
        return color_from_hsv(0.0f,   abs_f, abs_f); // red spectrum
    else
        return color_from_hsv(0.333f, abs_f, abs_f); // green spectrum
}
