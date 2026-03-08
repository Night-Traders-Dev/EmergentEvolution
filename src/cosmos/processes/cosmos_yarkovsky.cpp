#include "cosmos/cosmos_app_internal.h"
#include <cmath>

// ── Yarkovsky / YORP effect ──────────────────────────────────────────────────

void CosmosApp::process_yarkovsky(float dt) {
    // Yarkovsky effect: asymmetric thermal re-emission creates a net thrust
    // on small rotating bodies, causing orbital drift. Magnitude ∝ 1/R for
    // bodies small enough that thermal inertia matters (asteroids, small moons).
    // YORP: torque variant that changes spin rate.
    auto& bodies = state.bodies;
    const size_t n = bodies.size();

    for (size_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || b.non_attracting) continue;
        // Only affects small bodies: asteroids, comets, small moons
        if (b.type != CTYPE_ASTEROID && b.type != CTYPE_COMET &&
            !(b.type == CTYPE_MOON && b.radius < 2.0f)) continue;

        int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
        if (primary < 0 || primary >= (int)n) continue;
        const auto& host = bodies[(size_t)primary];
        if (!is_star_type(host.type)) continue;

        glm::vec3 to_star = host.pos - b.pos;
        float star_dist = glm::length(to_star);
        if (star_dist < 1.0e-3f) continue;

        // Yarkovsky acceleration ∝ L_star / (R_body * ρ * dist²)
        // Simplified: use luminosity and body size
        float L = std::max(host.luminosity, 0.01f);
        float R = std::max(b.radius, 0.01f);
        float accel_mag = 1.0e-8f * L / (R * std::max(b.mass, 1.0e-6f) * star_dist * star_dist);
        accel_mag = std::min(accel_mag, 1.0e-4f); // cap to prevent instability

        // Direction: perpendicular to sunlight in orbital plane (prograde/retrograde based on spin)
        glm::vec3 sun_dir = to_star / star_dist;
        glm::vec3 orbital_vel = b.vel - host.vel;
        float v_mag = glm::length(orbital_vel);
        if (v_mag < 1.0e-6f) continue;
        glm::vec3 v_hat = orbital_vel / v_mag;
        // Prograde spin → outward drift; retrograde → inward drift
        float spin_sign = (b.angular_vel >= 0.0f) ? 1.0f : -1.0f;
        {
            // Yarkovsky only matters for small bodies — skip when accel/gravity ratio is negligible
            float grav = cfg.G * host.mass / (star_dist * star_dist);
            float beta_y = (grav > 1.0e-15f) ? (accel_mag / grav) : 0.0f;
            if (beta_y > 0.001f) // only apply for small bodies where Yarkovsky matters
                b.vel += v_hat * (accel_mag * spin_sign * dt);
        }

        // YORP torque: slowly changes spin rate
        float yorp_torque = 5.0e-9f * L / (R * R * std::max(b.mass, 1.0e-6f) * star_dist * star_dist);
        yorp_torque = std::min(yorp_torque, 1.0e-4f);
        if (std::isfinite(yorp_torque))
            b.angular_vel += yorp_torque * spin_sign * dt;
    }
}
