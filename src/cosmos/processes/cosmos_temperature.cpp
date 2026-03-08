#include "cosmos/cosmos_app_internal.h"
#include "cosmos/core/cosmos_parallel.h"
#include <cmath>
#include <thread>

// ── Temperature ─────────────────────────────────────────────────────────────

void CosmosApp::process_temperature(float dt) {
    auto& bodies = state.bodies;
    const float background = 2.7f;

    // Pre-collect star indices to avoid O(N²) inner scan
    std::vector<size_t> star_indices;
    star_indices.reserve(32);
    for (size_t j = 0; j < bodies.size(); ++j) {
        if (!bodies[j].marked_for_removal && is_star_type(bodies[j].type))
            star_indices.push_back(j);
    }

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) && b.fuel > 0.05f) {
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
            continue;
        }
        // Stefan-Boltzmann: dT/dt ∝ (T⁴ - T_bg⁴) / thermal_mass
        // Thermal inertia scales with mass — massive bodies cool slower
        float T4_body = b.temperature * b.temperature * b.temperature * b.temperature;
        float T4_bg = background * background * background * background;
        float T4_diff = T4_body - T4_bg;
        if (T4_diff > 0.0f) {
            float thermal_inertia = std::max(std::cbrt(b.mass) * 10.0f, 0.1f);
            float cool_rate = cfg.radiative_cooling * T4_diff / std::max(T4_body, 1.0e-6f) * dt
                              / thermal_inertia;
            b.temperature -= b.temperature * std::min(cool_rate, 0.5f);
        }
        if (b.temperature < background) b.temperature = background;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || is_star_type(b.type)) continue;

        float eq_t4_sum = std::pow(background, 4.0f);
        for (size_t j = 0; j < star_indices.size(); ++j) {
            size_t si = star_indices[j];
            if (si == i || bodies[si].marked_for_removal) continue;
            float eq_t = equilibrium_temperature_from_star(b, bodies[si]);
            eq_t4_sum += std::pow(eq_t, 4.0f);
        }

        float target_temp = std::pow(std::max(eq_t4_sum, std::pow(background, 4.0f)), 0.25f);
        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float greenhouse = 1.0f + std::min(b.cached_props.atmosphere.pressure * 0.015f, 0.35f);
            target_temp *= greenhouse;
        }

        // Thermal response ∝ M^(-2/3): surface-to-volume ratio governs heat exchange
        float m23 = std::pow(std::max(b.mass, 1.0e-5f), 2.0f / 3.0f);
        float thermal_response = 0.35f / (0.6f + m23 * 0.8f);
        b.temperature += (target_temp - b.temperature) * std::min(thermal_response * dt, 1.0f);
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            if (bodies[j].marked_for_removal) continue;

            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;
            if (dist < touch * 1.15f) {
                float contact = 1.0f - dist / std::max(touch * 1.15f, 1.0e-3f);
                float delta = bodies[j].temperature - bodies[i].temperature;
                float exchange = delta * contact * std::min(0.25f * dt, 0.5f);
                float inv_heat_i = 1.0f / std::max(bodies[i].mass, 0.01f);
                float inv_heat_j = 1.0f / std::max(bodies[j].mass, 0.01f);
                bodies[i].temperature += exchange * inv_heat_i;
                bodies[j].temperature -= exchange * inv_heat_j;
            }

            if (cfg.tidal_forces) {
                size_t big_idx = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small_idx = (big_idx == i) ? j : i;
                CelestialBody& big = bodies[big_idx];
                CelestialBody& small = bodies[small_idx];

                if (dist > std::max(big.radius * 1.05f, 1.0f) && big.mass > small.mass * 2.0f) {
                    bool fluid_like_small = roche_secondary_fluid_like(small);
                    float roche_fluid = roche_distance_for_mode(big, small, true) *
                        std::clamp(cfg.roche_fluid_scale, 0.25f, 4.0f);
                    float roche_rigid = roche_distance_for_mode(big, small, false) *
                        std::clamp(cfg.roche_rigid_scale, 0.25f, 4.0f);
                    float roche_dist = 0.0f;
                    if (cfg.roche_limit_fluid && cfg.roche_limit_rigid)
                        roche_dist = fluid_like_small ? roche_fluid : roche_rigid;
                    else if (cfg.roche_limit_fluid)
                        roche_dist = roche_fluid;
                    else if (cfg.roche_limit_rigid)
                        roche_dist = roche_rigid;
                    else
                        roche_dist = std::max(roche_fluid, roche_rigid);
                    float tidal_reach = std::max(roche_dist * 6.0f, big.radius * 3.0f);
                    if (dist < tidal_reach) {
                        float proximity = 1.0f - std::clamp((dist - big.radius) /
                            std::max(tidal_reach - big.radius, 1.0e-3f), 0.0f, 1.0f);
                        float orbital_rate = std::sqrt(std::max(cfg.G * (big.mass + small.mass) /
                            std::max(dist * dist * dist, 1.0e-6f), 0.0f));
                        float spin_mismatch = std::abs(std::abs(small.angular_vel) - orbital_rate);
                        float shear_speed = glm::length(small.vel - big.vel);
                        float strain = cfg.G * big.mass * small.radius / std::max(dist * dist * dist, 1.0e-6f);
                        // Tidal heating ∝ e² — circular orbits produce no tidal flexing
                        float ecc = (tracked_eccentricity_.size() > small_idx)
                            ? std::max(tracked_eccentricity_[small_idx], 0.0f) : 0.0f;
                        float ecc_factor = std::max(ecc * ecc, 0.01f); // floor to allow contact heating
                        float dissipation = strain * strain *
                            (0.20f + spin_mismatch * 3600.0f + shear_speed * 0.12f) *
                            proximity * proximity * ecc_factor * dt * 18000.0f *
                            std::clamp(cfg.tidal_heating_scale, 0.0f, 4.0f);
                        if (dissipation > 0.0f) {
                            small.internal_energy += dissipation;
                            small.temperature += std::min(dissipation / std::max(small.mass * 0.08f, 1.0e-7f), 12000.0f);
                            if (small.type == CTYPE_PLANET || small.type == CTYPE_MOON) {
                                small.impact_heat = std::max(small.impact_heat, std::clamp(proximity * 0.75f, 0.0f, 0.95f));
                                small.visuals_valid = false;
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (b.internal_energy > 0.0f) {
            float transfer_rate = 15.0f;
            if (b.impact_heat > 0.01f || b.material_phase == PHASE_MOLTEN || b.material_phase == PHASE_PLASMA)
                transfer_rate = 95.0f;
            float transfer = std::min(b.internal_energy, transfer_rate * dt);
            b.temperature += transfer / std::max(b.mass, 0.01f);
            b.internal_energy -= transfer;
        }

        if (b.impact_heat > 0.0f || b.impact_ejecta > 0.0f || b.impact_crater_strength > 0.0f) {
            float mass_scale = std::clamp(std::cbrt(std::max(b.mass, 1.0e-6f)) + 0.35f, 0.35f, 6.0f);
            b.impact_heat = std::max(0.0f, b.impact_heat - dt * (0.000028f + cfg.radiative_cooling * 0.020f) / mass_scale);
            b.impact_ejecta = std::max(0.0f, b.impact_ejecta - dt * 0.000020f *
                (0.55f + std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 0.65f));
            b.impact_crater_strength = std::max(0.0f, b.impact_crater_strength - dt * 0.0000018f / mass_scale);
            b.impact_radius = std::max(0.0f, b.impact_radius - dt * 0.0000012f / mass_scale);
        }
    }
}
