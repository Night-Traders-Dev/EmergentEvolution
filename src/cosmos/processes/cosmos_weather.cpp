#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

void CosmosApp::process_space_weather(float dt) {
    auto& bodies = state.bodies;
    constexpr float kPi = 3.14159265359f;

    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& target = bodies[i];
        if (target.marked_for_removal) continue;
        if (is_star_type(target.type) || is_black_hole_type(target.type)) continue;

        float stellar_flux = 0.0f;
        float quasar_flux = 0.0f;
        float particle_flux = estimate_space_weather_flux(target, &state, cfg.G, &stellar_flux, &quasar_flux);
        if (particle_flux <= 0.0f) continue;

        float shielding = magnetic_shielding_score(target, cfg.G);
        float unshielded_flux = particle_flux * (1.0f - std::min(shielding, 1.0f) * 0.72f);
        float heating_flux = stellar_flux * (0.22f + (1.0f - std::min(shielding, 1.0f)) * 0.55f) +
                             quasar_flux * (0.95f + (1.0f - std::min(shielding, 1.0f)) * 0.85f);

        if (cfg.stellar_wind_pressure) {
            glm::vec3 wind_accel(0.0f);
            float cross_section = kPi * std::max(target.radius * target.radius, 1.0e-4f);
            float mass = std::max(target.mass, 1.0e-9f);
            float shield_scale = std::clamp(1.0f - std::min(shielding, 1.25f) * 0.45f, 0.15f, 1.0f);
            float type_scale = (target.type == CTYPE_DUST) ? 2.8f :
                               (target.type == CTYPE_COMET ? 2.1f :
                               (target.type == CTYPE_ASTEROID ? 1.3f :
                               ((target.cached_props.surface == SURF_GAS) ? 0.45f : 0.85f)));
            for (size_t j = 0; j < bodies.size(); ++j) {
                if (i == j) continue;
                const CelestialBody& source = bodies[j];
                if (source.marked_for_removal || !is_star_type(source.type))
                    continue;
                glm::vec3 delta = target.pos - source.pos;
                float dist2 = std::max(glm::dot(delta, delta), source.radius * source.radius * 4.0f);
                float dist = std::sqrt(dist2);
                if (dist <= 1.0e-5f)
                    continue;
                float luminosity = std::max(estimate_stellar_luminosity_units(source), 0.0f);
                // Scale factor calibrated so radiation pressure β≈1 for micron-dust,
                // negligible for planets, moderate for comets (physically correct).
                constexpr float kWindSimScale = 5.0e-7f;
                float pressure = kWindSimScale * cfg.stellar_wind_pressure_scale * luminosity /
                                 std::max(4.0f * kPi * dist2 * std::max(cfg.speed_of_light, 1.0f), 1.0e-6f);
                float accel_mag = pressure * cross_section * shield_scale * type_scale / mass;
                wind_accel += (delta / dist) * accel_mag;
            }
            if (glm::dot(wind_accel, wind_accel) > 0.0f) {
                // Physical β check: skip wind when radiation/gravity ratio is negligible.
                // For planets β ≈ 10⁻²⁰ (radiation pressure can't move planets).
                // Only dust, comets, and small asteroids have significant β.
                float wind_mag = glm::length(wind_accel);
                float grav_accel = 0.0f;
                for (size_t j = 0; j < bodies.size(); ++j) {
                    if (i == j || bodies[j].marked_for_removal) continue;
                    if (!is_star_type(bodies[j].type)) continue;
                    glm::vec3 d = bodies[j].pos - target.pos;
                    float r2 = glm::dot(d, d);
                    if (r2 > 1.0e-6f)
                        grav_accel += cfg.G * bodies[j].mass / r2;
                }
                float beta = (grav_accel > 1.0e-15f) ? (wind_mag / grav_accel) : 0.0f;
                if (beta > 0.001f) // only apply when β > 0.1% (dust/comets)
                    target.vel += wind_accel * dt;
            }
        }

        if (cfg.temperature_system) {
            float heat_gain = heating_flux * dt * std::max(target.radius, 0.5f) * 0.55f;
            target.internal_energy += heat_gain;
            // Limit temperature change per step to prevent thermal runaway at high time scales.
            // Use exponential approach: T → T_eq rather than unbounded linear accumulation.
            float thermal_inertia = std::max(target.mass * 8.0f, 0.02f);
            float dT = heat_gain / thermal_inertia;
            // Cap temperature increase to prevent unphysical spikes at large dt
            target.temperature += std::min(dT, std::max(target.temperature * 0.02f, 5.0f));
        }

        if (cfg.evaporation &&
            (target.type == CTYPE_PLANET || target.type == CTYPE_MOON) &&
            target.cached_props.atmosphere.pressure > 0.001f) {
            float escape = body_escape_velocity(target, cfg.G);
            float retention_resist = 0.20f + escape * 0.30f + shielding * 0.90f;
            float erosion = unshielded_flux * (0.0004f + cfg.evaporation_rate * 0.0022f) * dt /
                            std::max(retention_resist, 0.08f);
            if (target.cached_props.planet_class == PCLASS_GAS_GIANT ||
                target.cached_props.planet_class == PCLASS_ICE_GIANT) {
                erosion *= 0.10f;
            } else if (target.cached_props.planet_class == PCLASS_DWARF) {
                erosion *= 1.45f;
            }

            float prev_retention = target.atmosphere_retention;
            target.atmosphere_retention = std::clamp(target.atmosphere_retention - erosion, 0.0f, 1.0f);
            if (std::abs(target.atmosphere_retention - prev_retention) > 1.0e-6f) {
                target.props_valid = false;
                target.visuals_valid = false;
                float proxy_loss = std::min(target.mass * erosion * 0.01f, target.mass * 0.0015f);
                register_mass_loss(target, proxy_loss, dt);
            }
        }
    }
}
