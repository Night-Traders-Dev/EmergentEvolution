#include "cosmos/cosmos_app_internal.h"
#include <cmath>

void CosmosApp::process_material_phases(float dt) {
    auto& bodies = state.bodies;
    float phase_rate = std::clamp(cfg.material_phase_rate, 0.1f, 4.0f);
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

    for (size_t idx = 0; idx < bodies.size(); ++idx) {
        auto& b = bodies[idx];
        if (b.marked_for_removal) continue;

        MaterialComposition materials = derive_materials(b);
        MaterialPhase prev_phase = static_cast<MaterialPhase>(b.material_phase);
        MaterialPhase next_phase = infer_material_phase(b, materials);
        float next_intensity = phase_intensity_for_body(b, next_phase, materials);

        if (cfg.planetary_rings && b.ring_density > 0.001f) {
            if (!body_can_host_rings(b) || is_star_type(b.type) || is_black_hole_type(b.type)) {
                clear_ring_system(b);
            } else {
                float annulus = std::max(b.ring_outer_radius * b.ring_outer_radius -
                                         b.ring_inner_radius * b.ring_inner_radius, 1.0f);
                float mass_hint = std::max(b.mass * b.ring_density * 0.0000020f *
                                           (annulus / std::max(b.radius * b.radius, 1.0e-5f)),
                                           std::max(cfg.min_fragment_mass, 1.0e-12f) * 2.0f);
                pending_rings.push_back(PendingDustRing{
                    (int)idx,
                    mass_hint,
                    b.ring_inner_radius,
                    b.ring_outer_radius,
                    b.ring_density,
                    b.ring_ice_fraction,
                    hash_combine(b.seed, (uint32_t)(idx + 0xD057u))
                });
                clear_ring_system(b);
            }
        }

        if (!is_star_type(b.type) && !is_black_hole_type(b.type)) {
            if ((next_phase == PHASE_MOLTEN || next_phase == PHASE_PLASMA) &&
                (b.type == CTYPE_PLANET || b.type == CTYPE_MOON ||
                 b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST)) {
                float boiloff = std::clamp((b.temperature - 900.0f) / 1800.0f, 0.0f, 1.0f);
                b.internal_energy += boiloff * dt * 1.8f * phase_rate;
                if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
                    float prev_retention = b.atmosphere_retention;
                    b.atmosphere_retention = std::clamp(
                        b.atmosphere_retention - boiloff * dt * 0.0012f * phase_rate, 0.0f, 1.0f);
                    if (std::abs(prev_retention - b.atmosphere_retention) > 1.0e-6f)
                        b.props_valid = false;
                }
            }

            bool gas_dominated = gas_dominated_body(b, materials);
            // Only nebulae collapse toward stars — gas giant planets should NOT ignite.
            if (b.type == CTYPE_NEBULA &&
                b.mass >= HYDROGEN_BURNING_MASS_SOLAR * 0.65f) {
                float gravity_drive = std::clamp(cfg.G * b.mass /
                    std::max(b.radius * b.radius, 1.0e-6f), 0.0f, 2.0f);
                float mass_drive = std::clamp((b.mass / HYDROGEN_BURNING_MASS_SOLAR) - 0.65f, 0.0f, 2.5f);
                float temp_drive = std::clamp((b.temperature - 250.0f) / 2800.0f, 0.0f, 1.2f);
                float energy_drive = std::clamp(b.internal_energy / std::max(b.mass * 30.0f, 0.04f), 0.0f, 1.4f);
                float collapse_drive = mass_drive * 0.70f + gravity_drive * 0.30f +
                                       temp_drive * 0.35f + energy_drive * 0.25f +
                                       (b.type == CTYPE_NEBULA ? 0.08f : 0.0f);
                if (collapse_drive > 0.15f) {
                    // Slow collapse — nebulae should persist as clouds for a long time.
                    float advance = std::min(0.04f, dt * (0.00035f + collapse_drive * 0.0012f) * phase_rate);
                    b.collapse_progress = std::clamp(b.collapse_progress + advance, 0.0f, 1.25f);
                    next_phase = PHASE_COLLAPSING;
                    next_intensity = std::max(next_intensity, std::clamp(b.collapse_progress, 0.15f, 1.0f));

                    float proto_temp = std::max(250.0f,
                        expected_main_sequence_temperature(std::max(b.mass, 0.003f)) * 0.88f);
                    // Very gradual heating — cloud should stay cool for extended periods.
                    b.temperature += (proto_temp - b.temperature) * std::min(0.012f * dt, 0.06f);

                    CelestialBody proto = b;
                    proto.type = classify_star_spectral(proto_temp, std::max(proto.mass, 0.003f));
                    proto.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                    float target_radius = expected_star_radius(proto) * 1.35f;
                    b.radius = glm::mix(b.radius, target_radius, std::min(0.10f * dt, 0.22f));
                    b.internal_energy += collapse_drive * dt * 2.8f;

                    if (b.collapse_progress >= 1.0f && b.temperature >= 4500.0f) {
                        b.type = classify_star_spectral(std::max(b.temperature, proto_temp), std::max(b.mass, 0.003f));
                        b.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                        b.fuel = std::max(b.fuel, 0.92f);
                        b.temperature = std::clamp(std::max(b.temperature, proto_temp), 250.0f, 60000.0f);
                        b.radius = expected_star_radius(b);
                        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                                   b.stellar_stage, b.fuel);
                        b.material_phase = PHASE_PLASMA;
                        b.phase_intensity = 1.0f;
                        b.collapse_progress = 1.0f;
                        clear_ring_system(b);
                        b.props_valid = false;
                        b.visuals_valid = false;
                        continue;
                    }
                } else {
                    b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
                }
            } else {
                b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
            }
        } else {
            if (b.ring_density > 0.0f)
                clear_ring_system(b);
            b.material_phase = PHASE_PLASMA;
            b.phase_intensity = 1.0f;
            continue;
        }

        if (next_phase != prev_phase ||
            std::abs(next_intensity - b.phase_intensity) > 0.03f) {
            b.props_valid = false;
            b.visuals_valid = false;
        }
        b.material_phase = next_phase;
        b.phase_intensity = next_intensity;
    }

    for (const auto& ring : pending_rings) {
        spawn_dust_ring(ring.host_index, ring.total_mass, ring.inner, ring.outer,
                        ring.density, ring.ice_fraction, ring.seed_hint);
    }
}
