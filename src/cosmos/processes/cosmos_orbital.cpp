#include "cosmos/cosmos_app_internal.h"
#include <cmath>

// ── Tidal locking ────────────────────────────────────────────────────────────

void CosmosApp::process_tidal_locking(float dt) {
    auto& bodies = state.bodies;
    const size_t n = bodies.size();
    const float rate = std::max(cfg.tidal_locking_rate, 0.0f);
    if (rate <= 0.0f) return;

    for (size_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || b.non_attracting) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type) || b.type == CTYPE_NEBULA) continue;

        int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
        if (primary < 0 || primary >= (int)n) continue;
        const auto& host = bodies[(size_t)primary];
        if (host.marked_for_removal) continue;

        glm::vec3 diff = host.pos - b.pos;
        float dist = glm::length(diff);
        if (dist < 1.0e-3f) continue;

        // Tidal torque ∝ (M_host * R_body²) / dist³
        float tidal_strength = (host.mass * b.radius * b.radius) /
                               (dist * dist * dist);
        tidal_strength *= rate * 50.0f;

        // Target: synchronous rotation = orbital angular velocity
        float orbital_speed = glm::length(b.vel - host.vel);
        if (!std::isfinite(orbital_speed)) continue;
        float target_angular_vel = orbital_speed / std::max(dist, 0.01f);

        float delta = target_angular_vel - std::abs(b.angular_vel);
        if (!std::isfinite(delta)) continue;
        float adjustment = std::clamp(tidal_strength * std::abs(dt), 0.0f, 1.0f);
        float sign = (b.angular_vel >= 0.0f) ? 1.0f : -1.0f;

        b.angular_vel += sign * delta * adjustment;
        if (!std::isfinite(b.angular_vel)) b.angular_vel = 0.0f;
        b.tidal_lock_progress = std::clamp(
            1.0f - std::abs(delta) / std::max(target_angular_vel, 1.0e-6f),
            0.0f, 1.0f);
    }
}

// ── Orbital elements computation ─────────────────────────────────────────────

void CosmosApp::process_orbital_elements() {
    auto& bodies = state.bodies;
    const size_t n = bodies.size();

    for (size_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal) continue;

        int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
        if (primary < 0 || primary >= (int)n) {
            b.orbital_period = 0.0f;
            b.orbital_eccentricity = 0.0f;
            b.orbital_semi_major = 0.0f;
            continue;
        }
        const auto& host = bodies[(size_t)primary];
        if (host.marked_for_removal) continue;

        glm::vec3 r_vec = b.pos - host.pos;
        glm::vec3 v_vec = b.vel - host.vel;
        float r = glm::length(r_vec);
        float v = glm::length(v_vec);
        if (r < 1.0e-6f) continue;

        float mu = cfg.G * (host.mass + b.mass);
        if (mu < 1.0e-12f) continue;

        float energy = 0.5f * v * v - mu / r;

        if (energy < -1.0e-9f)
            b.orbital_semi_major = -mu / (2.0f * energy);
        else
            b.orbital_semi_major = r;

        glm::vec3 h = glm::cross(r_vec, v_vec);
        glm::vec3 e_vec = glm::cross(v_vec, h) / mu - r_vec / r;
        b.orbital_eccentricity = std::clamp(glm::length(e_vec), 0.0f, 10.0f);

        if (b.orbital_semi_major > 0.0f && energy < -1.0e-9f) {
            float a3 = b.orbital_semi_major * b.orbital_semi_major * b.orbital_semi_major;
            b.orbital_period = 6.283185f * std::sqrt(a3 / mu);
        } else {
            b.orbital_period = 0.0f;
        }

        if (b.orbital_period > 1.0e-3f) {
            float dt_fraction = sim_time_ / b.orbital_period;
            b.season_phase = std::fmod(dt_fraction * 6.283185f, 6.283185f);
        }

        if (is_star_type(b.type) && b.luminosity > 0.0f) {
            b.habitable_zone_inner = std::sqrt(b.luminosity / 1.1f);
            b.habitable_zone_outer = std::sqrt(b.luminosity / 0.53f);
        }

        if (is_black_hole_type(b.type) && b.mass > 0.0f) {
            // T_H = ℏc³/(8πGMk_B) — in solar mass units: T ≈ 6.17e-8 K / (M/M_sun)
            // But our mass is in solar masses, so this is correct dimensionally
            b.hawking_temperature = 6.17e-8f / b.mass;
        }
    }
}
