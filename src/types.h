#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <glm/glm.hpp>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr uint32_t REGION_W           = 2560;
static constexpr uint32_t REGION_H           = 1440;
static constexpr uint32_t MAX_PARTICLE_TYPES = 10;
static constexpr uint32_t GROUP_DENSITY      = 256;

// ── GPU push-constant block (must match GLSL layout exactly) ─────────────────

struct PushConstants {
    glm::vec2 region_size;        //  0 – 7
    glm::vec2 camera_origin;      //  8 – 15
    uint32_t  particle_count;     // 16 – 19
    uint32_t  particle_types;     // 20 – 23
    uint32_t  step;               // 24 – 27
    float     dt;                 // 28 – 31
    float     camera_zoom;        // 32 – 35
    float     radius;             // 36 – 39
    float     dampening;          // 40 – 43
    float     repulsion_radius;   // 44 – 47
    float     interaction_radius; // 48 – 51
    float     density_limit;      // 52 – 55
};
static_assert(sizeof(PushConstants) == 56, "PushConstants layout mismatch");

// ── Simulation configuration (mirrors interface slider defaults from .tscn) ──

struct SimConfig {
    // Generation settings
    uint32_t particle_count     = 22500; // pow(150,2)
    uint32_t particle_types     = 5;
    bool     reset_colors       = false;
    bool     reset_forces       = true;
    uint32_t generation_seed    = 0;

    // Physics / rendering (real-time sliders)
    float radius             = 2.0f;
    float dampening          = 0.85f;
    float repulsion_radius   = 20.0f;
    float interaction_radius = 60.0f;
    float density_limit      = 60.0f;

    // Camera state (managed by simulation)
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
