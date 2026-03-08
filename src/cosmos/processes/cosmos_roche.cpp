#include "cosmos/cosmos_app_internal.h"
#include <cmath>

// ── Roche Limit ─────────────────────────────────────────────────────────────

void CosmosApp::process_roche_limit(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    bool allow_disruption = cfg.roche_limit && (cfg.roche_limit_fluid || cfg.roche_limit_rigid);
    struct PendingDustRing {
        int host_index = -1;
        float total_mass = 0.0f;
        float inner = 0.0f;
        float outer = 0.0f;
        float density = 0.0f;
        float ice_fraction = 0.0f;
        uint32_t seed_hint = 0u;
    };
    std::vector<PendingDustRing> pending_rings;

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || bodies[j].marked_for_removal) continue;
            if (is_black_hole_type(bodies[j].type)) continue;
            if (bodies[i].mass <= bodies[j].mass * 2.5f) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 delta = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(delta);
            bool fluid_like_secondary = roche_secondary_fluid_like(bodies[j]);
            float roche_fluid = roche_distance_for_mode(bodies[i], bodies[j], true) *
                std::clamp(cfg.roche_fluid_scale, 0.25f, 4.0f);
            float roche_rigid = roche_distance_for_mode(bodies[i], bodies[j], false) *
                std::clamp(cfg.roche_rigid_scale, 0.25f, 4.0f);
            float heating_limit = std::max(roche_fluid, roche_rigid);

            float disruption_limit = 0.0f;
            bool using_fluid_limit = false;
            if (allow_disruption) {
                if (fluid_like_secondary && cfg.roche_limit_fluid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (!fluid_like_secondary && cfg.roche_limit_rigid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else if (cfg.roche_limit_fluid && !cfg.roche_limit_rigid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (cfg.roche_limit_rigid && !cfg.roche_limit_fluid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else {
                    disruption_limit = fluid_like_secondary ? roche_fluid : roche_rigid;
                    using_fluid_limit = fluid_like_secondary;
                }
            }

            glm::vec3 prev_delta = delta - rel_vel * dt;
            glm::vec3 seg = delta - prev_delta;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_delta, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_delta + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);
            float eval_dist = std::min(dist, sweep_dist);

            float interaction_limit = 0.0f;
            if (cfg.tidal_forces) interaction_limit = heating_limit;
            if (allow_disruption) interaction_limit = std::max(interaction_limit, disruption_limit);
            if (interaction_limit <= 0.0f || eval_dist >= interaction_limit) continue;
            if (eval_dist <= std::max(bodies[i].radius * 1.01f, 1.0e-4f)) continue;

            float heating_overflow = std::clamp((heating_limit - eval_dist) /
                std::max(heating_limit, 1.0e-4f), 0.0f, 1.0f);
            float disruption_overflow = (allow_disruption && disruption_limit > 0.0f)
                ? std::clamp((disruption_limit - eval_dist) / std::max(disruption_limit, 1.0e-4f), 0.0f, 1.0f)
                : 0.0f;
            float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
            float spin_ratio = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[j], cfg.G) : 0.0f;
            float spin_overflow = cfg.spin_fragmentation ? std::max(spin_ratio - spin_threshold, 0.0f) : 0.0f;
            bool spin_assisted_disruption = cfg.spin_fragmentation &&
                spin_overflow > 0.0f &&
                eval_dist < heating_limit * (using_fluid_limit ? 1.20f : 1.08f);
            glm::vec3 tidal_axis = (sweep_dist < dist && sweep_dist > 1.0e-5f)
                ? (closest_rel / sweep_dist)
                : glm::normalize(delta);
            if (glm::length(tidal_axis) < 1.0e-5f)
                tidal_axis = glm::vec3(0.0f, 1.0f, 0.0f);

            // Baseline tidal work/heat in Roche zone.
            float tide_strain = cfg.G * bodies[i].mass * bodies[j].radius /
                std::max(eval_dist * eval_dist * eval_dist, 1.0e-6f);
            float tidal_work = tide_strain * (0.45f + heating_overflow * 2.0f) * dt * 1400.0f *
                std::clamp(cfg.tidal_heating_scale, 0.0f, 4.0f);
            if (tidal_work > 0.0f) {
                bodies[j].internal_energy += tidal_work;
                bodies[j].temperature += std::min(tidal_work / std::max(bodies[j].mass * 0.08f, 1.0e-7f), 4000.0f);
                if (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON) {
                    bodies[j].impact_heat = std::max(
                        bodies[j].impact_heat,
                        std::clamp(heating_overflow * 0.75f, 0.0f, 0.95f));
                    bodies[j].visuals_valid = false;
                }
            }
            if ((!allow_disruption || eval_dist >= disruption_limit) && !spin_assisted_disruption)
                continue;

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            if (bodies[j].mass < effective_min_frag_mass ||
                (int)bodies[j].frag_generation >= cfg.max_frag_generation) {
                continue;
            }

            // Partial Roche stripping (instead of instant full deletion).
            float secondary_mass = bodies[j].mass;
            float local_orbital_speed = std::sqrt(std::max(cfg.G * bodies[i].mass /
                std::max(eval_dist, 1.0e-6f), 0.0f));
            float speed_ratio = glm::length(rel_vel) / std::max(local_orbital_speed, 1.0e-4f);
            float dynamic_overflow = disruption_overflow +
                std::clamp((speed_ratio - 0.8f) * 0.28f, 0.0f, 0.45f) +
                spin_overflow * (using_fluid_limit ? 0.58f : 0.40f);
            float strip_fraction = using_fluid_limit
                ? std::clamp(0.10f + dynamic_overflow * 0.92f, 0.04f, 0.98f)
                : std::clamp(0.06f + dynamic_overflow * 0.66f, 0.02f, 0.86f);
            if (spin_assisted_disruption && eval_dist >= disruption_limit) {
                float spin_only_floor = using_fluid_limit ? 0.10f : 0.06f;
                float spin_only_cap = using_fluid_limit ? 0.40f : 0.28f;
                strip_fraction = std::clamp(std::max(strip_fraction,
                                                     spin_only_floor + spin_overflow * (using_fluid_limit ? 0.18f : 0.12f)),
                                            spin_only_floor, spin_only_cap);
            }
            if (eval_dist < disruption_limit * (using_fluid_limit ? 0.62f : 0.52f) ||
                disruption_overflow > (using_fluid_limit ? 0.72f : 0.82f))
                strip_fraction = std::max(strip_fraction, using_fluid_limit ? 0.93f : 0.78f);
            float stripped_mass = secondary_mass * strip_fraction;
            if (stripped_mass <= 1.0e-9f) continue;

            float ring_fraction = (cfg.planetary_rings && body_can_host_rings(bodies[i]))
                ? std::clamp((using_fluid_limit ? 0.18f : 0.08f) + disruption_overflow * 0.32f, 0.0f, 0.75f)
                : 0.0f;
            float ring_mass = stripped_mass * ring_fraction;
            if (ring_mass > 0.0f) {
                add_ring_material(bodies[i], bodies[j], ring_mass, disruption_limit, cfg);
                pending_rings.push_back(PendingDustRing{
                    (int)i,
                    ring_mass,
                    bodies[i].ring_inner_radius,
                    bodies[i].ring_outer_radius,
                    bodies[i].ring_density,
                    bodies[i].ring_ice_fraction,
                    hash_combine(bodies[i].seed, bodies[j].seed)
                });
                clear_ring_system(bodies[i]);
            }

            float fragment_mass = std::max(stripped_mass - ring_mass, 0.0f);
            if (fragment_mass > 1.0e-9f) {
                int fragment_count = std::clamp(
                    std::max(1, cfg.fragment_count / 2 +
                                (int)std::floor((disruption_overflow + spin_overflow * 0.85f) *
                                                (using_fluid_limit ? 3.8f : 2.4f))),
                                                1, 10);
                float speed_scale = using_fluid_limit ? 0.08f : 0.055f;
                float spin_surface_speed = std::abs(bodies[j].angular_vel) *
                    std::max(bodies[j].radius, bodies[j].type == CTYPE_DUST ? 0.04f : 0.1f);
                float roche_ejecta_speed = std::clamp(
                    local_orbital_speed * (speed_scale + dynamic_overflow * 0.13f) +
                    spin_surface_speed * (using_fluid_limit ? 0.05f : 0.07f),
                                                      0.0003f, using_fluid_limit ? 0.012f : 0.009f);
                spawn_fragments(bodies[j].pos, bodies[j].vel, fragment_mass, fragment_count,
                                bodies[j].frag_generation, bodies[j].temperature,
                                tidal_axis, roche_ejecta_speed,
                                &bodies[j], 0.45f + disruption_overflow * 0.90f + spin_overflow * 0.50f);
            }

            register_mass_loss(bodies[j], stripped_mass, std::max(dt, 1.0e-4f));
            float remaining_mass = std::max(secondary_mass - stripped_mass, 0.0f);
            if (remaining_mass <= effective_min_frag_mass) {
                bodies[j].marked_for_removal = true;
                continue;
            }

            float mass_scale = std::cbrt(remaining_mass / std::max(secondary_mass, 1.0e-8f));
            bodies[j].mass = remaining_mass;
            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
            if (cfg.spin_fragmentation) {
                float post_spin_cap = spin_fragmentation_critical_omega(bodies[j], cfg.G) *
                    std::max(spin_threshold * 0.94f, 0.25f);
                if (post_spin_cap > 0.0f) {
                    float damped_spin = std::abs(bodies[j].angular_vel) * std::max(0.48f, mass_scale * 0.78f);
                    bodies[j].angular_vel = std::copysign(std::min(damped_spin, post_spin_cap), bodies[j].angular_vel);
                }
            }
            bodies[j].props_valid = false;
            bodies[j].visuals_valid = false;
            apply_impact_signature(
                bodies[j], -tidal_axis,
                std::clamp(disruption_overflow * 0.8f + spin_overflow * 0.35f, 0.0f, 1.2f),
                stripped_mass / std::max(secondary_mass, 1.0e-8f),
                std::clamp(tidal_work / std::max(bodies[j].mass, 1.0e-8f), 0.0f, 1.0f),
                std::clamp(0.18f + disruption_overflow * 0.45f + spin_overflow * 0.18f, 0.08f, 0.92f));
        }
    }

    for (const auto& ring : pending_rings) {
        spawn_dust_ring(ring.host_index, ring.total_mass, ring.inner, ring.outer,
                        ring.density, ring.ice_fraction, ring.seed_hint);
    }
}
