#include "cosmos/cosmos_app_internal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

// ── Spawn preview body construction ─────────────────────────────────────────

uint32_t CosmosApp::draft_settings_hash() const {
    uint32_t h = 0x811C9DC5u;
    auto mix = [&](uint32_t v) { h ^= v; h *= 0x01000193u; };
    mix((uint32_t)spawn_draft_.planet_look);
    mix((uint32_t)spawn_draft_.star_stage_hint);
    mix(spawn_draft_.override_temperature ? 1u : 0u);
    mix(float_bits(spawn_draft_.temperature));
    mix(spawn_draft_.override_radius ? 1u : 0u);
    mix(float_bits(spawn_draft_.radius));
    mix(spawn_draft_.override_material ? 1u : 0u);
    mix(float_bits(spawn_draft_.material_iron));
    mix(float_bits(spawn_draft_.material_silicate));
    mix(float_bits(spawn_draft_.material_ice));
    mix(float_bits(spawn_draft_.material_hydrogen));
    mix(spawn_draft_.spawn_rings ? 1u : 0u);
    return h;
}

int CosmosApp::spawn_at(glm::vec3 pos) {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return -1;
    const bool is_small_body = (spawn_type == CTYPE_ASTEROID || spawn_type == CTYPE_COMET || spawn_type == CTYPE_DUST);
    const int requested_count = std::clamp(spawn_draft_.small_body_spawn_count, 1, 1000);
    const bool use_batch = is_small_body && requested_count > 1;
    const int spawn_count = use_batch ? requested_count : 1;

    uint32_t base_seed = hash_combine(hash_combine(float_bits(pos.x), float_bits(pos.y)), float_bits(pos.z));
    base_seed = hash_combine(base_seed, (uint32_t)spawn_type);
    base_seed = hash_combine(base_seed, (uint32_t)(sim_time_ * 1000.0f) + 2166136261u);
    uint64_t wall_ticks = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device rd;
    base_seed = hash_combine(base_seed, (uint32_t)wall_ticks);
    base_seed = hash_combine(base_seed, (uint32_t)(wall_ticks >> 32));
    base_seed = hash_combine(base_seed, rd());
    std::mt19937 layout_rng(base_seed ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
    std::uniform_real_distribution<float> jitter01(-1.0f, 1.0f);

    float nominal_radius = spawn_draft_.override_radius
        ? std::max(0.04f, spawn_draft_.radius)
        : std::max(0.06f, std::cbrt(std::max(spawn_mass, 1.0e-13f)) * 5.0f);
    float cluster_radius = std::max(6.0f, std::cbrt((float)spawn_count) * std::max(1.2f, nominal_radius) * 4.0f);

    auto layout_offset = [&](int index) -> glm::vec3 {
        if (!use_batch) return glm::vec3(0.0f);
        const float two_pi = 6.28318530718f;
        const int layout = std::clamp(spawn_draft_.small_body_layout, 0, 3);
        switch (layout) {
        case 1: { // Sphere
            float u = ((float)index + 0.5f) / (float)spawn_count;
            float y = 1.0f - 2.0f * u;
            float rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float theta = 2.39996323f * (float)index;
            glm::vec3 dir(std::cos(theta) * rr, y, std::sin(theta) * rr);
            float radial = cluster_radius * (0.42f + 0.58f * unit01(layout_rng));
            return dir * radial;
        }
        case 2: { // Cube
            int side = std::max(1, (int)std::ceil(std::cbrt((float)spawn_count)));
            int ix = index % side;
            int iy = (index / side) % side;
            int iz = index / (side * side);
            float denom = (side > 1) ? (float)(side - 1) : 1.0f;
            float spacing = (cluster_radius * 2.0f) / denom;
            return glm::vec3(
                ((float)ix - 0.5f * (float)(side - 1)) * spacing,
                ((float)iy - 0.5f * (float)(side - 1)) * spacing,
                ((float)iz - 0.5f * (float)(side - 1)) * spacing);
        }
        case 3: { // Torus
            float t = (float)index / (float)spawn_count;
            float u = t * two_pi;
            float frac = std::fmod((float)index * 0.61803398875f + unit01(layout_rng) * 0.2f, 1.0f);
            if (frac < 0.0f) frac += 1.0f;
            float v = frac * two_pi;
            float major = cluster_radius;
            float minor = cluster_radius * 0.30f;
            float ring = major + minor * std::cos(v);
            return glm::vec3(ring * std::cos(u), minor * std::sin(v), ring * std::sin(u));
        }
        default: { // Random
            return glm::vec3(
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius);
        }
        }
    };

    auto spawn_single = [&](const glm::vec3& spawn_pos, int ordinal) -> int {
        CelestialBody nb;
        nb.pos = spawn_pos;
        nb.vel = glm::vec3(0.0f);
        nb.mass = spawn_mass;
        nb.radius = std::max(3.0f, std::cbrt(spawn_mass) * 5.0f);
        nb.type = (uint32_t)spawn_type;
        uint32_t pos_seed = hash_combine(hash_combine(float_bits(spawn_pos.x), float_bits(spawn_pos.y)), float_bits(spawn_pos.z));
        nb.seed = hash_combine(hash_combine(pos_seed,
            (uint32_t)state.bodies.size() * 747796405u + 2891336453u),
            (uint32_t)(sim_time_ * 1000.0f) + 1181783497u + (uint32_t)ordinal * 2654435761u);
        std::mt19937 rng(nb.seed ^ (uint32_t)(sim_time_ * 1000.0f) ^ ((uint32_t)ordinal * 1013904223u));

        // Nebulae spawn as a cloud of many small gas particles, not a single body.
        if (spawn_type == CTYPE_NEBULA) {
            float cloud_r = std::max(35.0f, std::cbrt(spawn_mass) * 120.0f);
            if (spawn_draft_.override_radius)
                cloud_r = std::max(10.0f, spawn_draft_.radius);
            glm::vec3 vel(0.0f);
            if (spawn_draft_.override_velocity)
                vel = spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
            return spawn_nebula_cloud(spawn_pos, vel, spawn_mass, cloud_r, nb.seed);
        }

        clear_ring_system(nb);
        nb.material_phase = PHASE_SOLID;
        nb.phase_intensity = 0.0f;
        nb.collapse_progress = 0.0f;
        clear_impact_signature(nb);
        nb.forced_surface = -1;
        nb.custom_material = false;
        nb.props_valid = false;
        nb.visuals_valid = false;
        nb.cached_temp_band = -1;
        nb.cached_visual_temp_band = -1;

        if (is_star_type((uint32_t)spawn_type)) {
            randomize_star_properties(nb, rng, (uint32_t)spawn_type);
            if (spawn_draft_.star_stage_hint >= 0 &&
                spawn_draft_.star_stage_hint < (int)SSTAGE_COUNT) {
                nb.stellar_stage = (uint32_t)spawn_draft_.star_stage_hint;
                if (nb.stellar_stage == SSTAGE_WHITE_DWARF) {
                    nb.mass = std::clamp(nb.mass, 0.17f, 1.44f);
                    nb.fuel = 0.0f;
                    nb.temperature = std::clamp(nb.temperature, 9000.0f, 140000.0f);
                } else if (nb.stellar_stage == SSTAGE_NEUTRON_STAR) {
                    nb.mass = std::clamp(nb.mass, 1.10f, 2.50f);
                    nb.fuel = 0.0f;
                    nb.temperature = std::clamp(std::max(nb.temperature, 70000.0f), 70000.0f, 250000.0f);
                } else if (nb.stellar_stage == SSTAGE_RED_GIANT || nb.stellar_stage == SSTAGE_AGB) {
                    nb.mass = std::max(nb.mass, 0.8f);
                    nb.temperature = std::clamp(nb.temperature * 0.62f, 2800.0f, 5600.0f);
                } else if (nb.stellar_stage == SSTAGE_SUPERGIANT || nb.stellar_stage == SSTAGE_HYPERGIANT) {
                    nb.mass = std::max(nb.mass, CORE_COLLAPSE_MIN_MASS_SOLAR);
                    nb.temperature = std::clamp(nb.temperature * 0.72f, 3200.0f, 52000.0f);
                }
                nb.radius = expected_star_radius(nb);
                nb.type = classify_star_spectral(std::max(nb.temperature, 250.0f), std::max(nb.mass, 0.003f));
                nb.luminosity = expected_stellar_luminosity(nb.mass, nb.temperature, nb.radius,
                                                            nb.stellar_stage, nb.fuel);
            }
            nb.material_phase = PHASE_PLASMA;
            nb.phase_intensity = 1.0f;
        } else if (is_black_hole_type((uint32_t)spawn_type)) {
            nb.temperature = 0.0f;
            nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
            if (spawn_type == CTYPE_BLACK_HOLE)
                nb.type = classify_black_hole(nb.mass);
            nb.material_phase = PHASE_PLASMA;
            nb.phase_intensity = 1.0f;
        } else {
            nb.temperature = 300.0f;
            if (spawn_type == CTYPE_PLANET) {
                randomize_planet_properties(nb, state, cfg, rng);
            } else if (spawn_type == CTYPE_MOON) {
                randomize_moon_properties(nb, state, rng);
            } else if (spawn_type == CTYPE_ASTEROID) {
                randomize_small_body_properties(nb, rng, false);
            } else if (spawn_type == CTYPE_COMET) {
                randomize_small_body_properties(nb, rng, true);
            } else if (spawn_type == CTYPE_DUST) {
                randomize_dust_properties(nb, rng);
            } else if (spawn_type == CTYPE_NEBULA) {
                randomize_nebula_properties(nb, rng);
            }
        }

        if (spawn_draft_.override_temperature)
            nb.temperature = std::clamp(spawn_draft_.temperature, 2.7f, 120000.0f);
        if (spawn_draft_.override_radius)
            nb.radius = std::max(0.04f, spawn_draft_.radius);
        if (spawn_draft_.override_rotation) {
            float hours = std::max(spawn_draft_.rotation_hours, 0.1f);
            float sign = (nb.angular_vel < 0.0f) ? -1.0f : 1.0f;
            nb.angular_vel = sign * (2.0f * 3.14159265359f) / (hours * 3600.0f);
        }
        if (spawn_draft_.override_material) {
            nb.custom_material = true;
            nb.custom_iron = std::max(spawn_draft_.material_iron, 0.0f);
            nb.custom_silicate = std::max(spawn_draft_.material_silicate, 0.0f);
            nb.custom_water = std::max(spawn_draft_.material_ice, 0.0f);
            nb.custom_hydrogen = std::max(spawn_draft_.material_hydrogen, 0.0f);
        }
        if (nb.type == CTYPE_NEBULA) {
            // Nebulae are hydrogen-dominated gas/dust clouds, not gas-giant planets.
            nb.material_phase = PHASE_GAS;
            nb.phase_intensity = std::max(nb.phase_intensity, 0.70f);
            nb.custom_material = true;
            nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.85f);
            nb.custom_silicate = std::clamp(nb.custom_silicate, 0.02f, 0.12f);
            nb.custom_water = std::clamp(nb.custom_water, 0.0f, 0.08f);
            nb.custom_iron = std::clamp(nb.custom_iron, 0.0f, 0.04f);
            nb.forced_surface = -1;
            clear_ring_system(nb);
            nb.props_valid = false;
            nb.visuals_valid = false;
        }
        if ((nb.type == CTYPE_PLANET || nb.type == CTYPE_MOON) && spawn_draft_.planet_look > 0) {
            float mass_earth = nb.mass / std::max(EARTH_MASS_SOLAR, 1.0e-12f);
            bool force_gas = (spawn_draft_.planet_look == 5 && nb.type == CTYPE_PLANET);
            bool likely_gas = mass_earth > 10.0f;
            if (force_gas) {
                nb.forced_surface = 4; // gas giant
                nb.custom_material = true;
                nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.70f);
                nb.custom_silicate = std::min(nb.custom_silicate, 0.20f);
                nb.custom_iron = std::min(nb.custom_iron, 0.08f);
                nb.custom_water = std::max(nb.custom_water, 0.02f);
                nb.props_valid = false;
                nb.visuals_valid = false;
            } else if (!likely_gas) {
                switch (spawn_draft_.planet_look) {
                case 1: nb.forced_surface = 0; break; // rocky
                case 2: nb.forced_surface = 1; break; // water
                case 3: nb.forced_surface = 2; break; // ice
                case 4: nb.forced_surface = 3; break; // earth-like
                default: nb.forced_surface = -1; break;
                }
                nb.props_valid = false;
                nb.visuals_valid = false;
            }
        }
        if (body_can_host_rings(nb)) {
            if (spawn_draft_.spawn_rings) {
                int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
                if (spawn_draft_.override_ring_layout) {
                    float ring_tilt = std::clamp(std::abs(nb.angular_vel) * 150.0f, 0.02f, 0.45f);
                    switch (ring_style) {
                    case 0: ring_tilt = 0.14f; break; // Saturn-like
                    case 1: ring_tilt = 1.24f; break; // Uranus-like high obliquity
                    case 2: ring_tilt = 0.52f; break; // Neptune-like
                    case 3: ring_tilt = 0.32f; break; // Torus
                    case 4: ring_tilt = 0.18f; break; // Realistic disk
                    case 5: ring_tilt = 0.90f; break; // Unrealistic geometry
                    case 6: ring_tilt = 0.22f; break; // Resonance gaps
                    default: break;
                    }
                    set_ring_system(nb,
                        nb.radius * std::max(spawn_draft_.ring_inner_mult, 1.15f),
                        nb.radius * std::max(spawn_draft_.ring_outer_mult, spawn_draft_.ring_inner_mult + 0.25f),
                        std::clamp(spawn_draft_.ring_density, 0.01f, 1.0f),
                        std::clamp(spawn_draft_.ring_ice_fraction, 0.0f, 1.0f),
                        std::clamp(ring_tilt, 0.02f, 1.30f));
                } else if (nb.ring_density <= 0.001f) {
                    set_ring_system(nb, nb.radius * 1.5f, nb.radius * 3.0f, 0.32f, 0.55f, 0.12f);
                }
            } else {
                clear_ring_system(nb);
            }
        } else {
            clear_ring_system(nb);
        }

        if (spawn_in_orbit_ && !state.bodies.empty()) {
            int nearest = -1;
            float nearest_dist = 1e9f;
            for (size_t i = 0; i < state.bodies.size(); i++) {
                float d = glm::length(state.bodies[i].pos - nb.pos);
                if (d > 0.1f && d < nearest_dist && state.bodies[i].mass > nb.mass) {
                    nearest_dist = d;
                    nearest = (int)i;
                }
            }
            if (nearest >= 0) {
                nb.parent = nearest;
                glm::vec3 diff = nb.pos - state.bodies[nearest].pos;
                float dist = glm::length(diff);
                if (dist > 0.1f) {
                    float v = std::sqrt(cfg.G * state.bodies[nearest].mass / dist);
                    glm::vec3 dir = glm::normalize(diff);
                    glm::vec3 perp(-dir.z, 0.0f, dir.x);
                    nb.vel = state.bodies[nearest].vel + perp * v;
                }
            }
        }
        if (spawn_draft_.override_velocity) {
            nb.vel += spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
        }

        nb.name = generate_body_name(nb.seed, nb.type);
        nb.non_attracting = (nb.type == CTYPE_DUST) ? cfg.dust_debug_non_attracting : nb.non_attracting;
        refresh_body_render_state(nb, &state);
        state.bodies.push_back(nb);
        state.trails.emplace_back();

        int host_idx = (int)state.bodies.size() - 1;
        const auto spawned = state.bodies[(size_t)host_idx];
        int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
        if (cfg.planetary_rings && spawned.ring_density > 0.001f &&
            body_can_host_rings(spawned)) {
            float annulus = std::max(spawned.ring_outer_radius * spawned.ring_outer_radius -
                                     spawned.ring_inner_radius * spawned.ring_inner_radius, 1.0f);
            float mass_hint = std::max(spawned.mass * spawned.ring_density * 0.00012f *
                                       (annulus / std::max(spawned.radius * spawned.radius, 1.0e-5f)),
                                       std::max(cfg.min_fragment_mass, 1.0e-12f) * 48.0f);
            spawn_dust_ring(host_idx, mass_hint, spawned.ring_inner_radius, spawned.ring_outer_radius,
                            spawned.ring_density, spawned.ring_ice_fraction, spawned.seed ^ 0xD05751EDu,
                            ring_style);
            // Keep ring visual data on host — shader renders the translucent ring overlay,
            // particles provide the physical simulation. Don't clear_ring_system here.
        }

        if (spawn_draft_.spawn_moons && host_idx >= 0 && host_idx < (int)state.bodies.size()) {
            spawn_moons_for_host(host_idx,
                                 std::clamp(spawn_draft_.moon_count, 1, 100),
                                 std::clamp(spawn_draft_.moon_orbit_layout, 0, 4),
                                 std::clamp(spawn_draft_.moon_inclination_deg, 0.0f, 85.0f),
                                 std::clamp(spawn_draft_.moon_spacing_scale, 0.35f, 4.0f));
        }
        return host_idx;
    };

    int first_idx = (int)state.bodies.size();
    for (int i = 0; i < spawn_count; ++i) {
        glm::vec3 offset = layout_offset(i);
        spawn_single(pos + offset, i);
    }

    if (diagnostics_enabled_)
        validate_body_state("spawn/manual", true);

    return (first_idx < (int)state.bodies.size()) ? first_idx : -1;
}

void CosmosApp::build_preview_body() {
    CelestialBody& nb = preview_body_;
    nb = CelestialBody{};  // reset
    nb.mass = spawn_mass;
    nb.radius = std::max(3.0f, std::cbrt(std::max(spawn_mass, 1.0e-13f)) * 5.0f);
    nb.type = (uint32_t)spawn_type;
    nb.seed = hash_combine(preview_reroll_counter_ * 747796405u + 2891336453u,
                            (uint32_t)(spawn_type) * 2654435761u);
    std::mt19937 rng(nb.seed);

    clear_ring_system(nb);
    nb.material_phase = PHASE_SOLID;
    nb.phase_intensity = 0.0f;
    nb.collapse_progress = 0.0f;
    clear_impact_signature(nb);
    nb.forced_surface = -1;
    nb.custom_material = false;
    nb.props_valid = false;
    nb.visuals_valid = false;
    nb.cached_temp_band = -1;
    nb.cached_visual_temp_band = -1;

    if (is_star_type((uint32_t)spawn_type)) {
        randomize_star_properties(nb, rng, (uint32_t)spawn_type);
        if (spawn_draft_.star_stage_hint >= 0 &&
            spawn_draft_.star_stage_hint < (int)SSTAGE_COUNT) {
            nb.stellar_stage = (uint32_t)spawn_draft_.star_stage_hint;
            nb.radius = expected_star_radius(nb);
            nb.type = classify_star_spectral(std::max(nb.temperature, 250.0f),
                                              std::max(nb.mass, 0.003f));
            nb.luminosity = expected_stellar_luminosity(nb.mass, nb.temperature, nb.radius,
                                                         nb.stellar_stage, nb.fuel);
        }
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else if (is_black_hole_type((uint32_t)spawn_type)) {
        nb.temperature = 0.0f;
        nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
        if (spawn_type == CTYPE_BLACK_HOLE)
            nb.type = classify_black_hole(nb.mass);
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else {
        nb.temperature = 300.0f;
        if (spawn_type == CTYPE_PLANET) {
            randomize_planet_properties(nb, state, cfg, rng);
        } else if (spawn_type == CTYPE_MOON) {
            randomize_moon_properties(nb, state, rng);
        } else if (spawn_type == CTYPE_ASTEROID) {
            randomize_small_body_properties(nb, rng, false);
        } else if (spawn_type == CTYPE_COMET) {
            randomize_small_body_properties(nb, rng, true);
        } else if (spawn_type == CTYPE_DUST) {
            randomize_dust_properties(nb, rng);
        } else if (spawn_type == CTYPE_NEBULA) {
            randomize_nebula_properties(nb, rng);
        }
    }

    if (spawn_draft_.override_temperature)
        nb.temperature = std::clamp(spawn_draft_.temperature, 2.7f, 120000.0f);
    if (spawn_draft_.override_radius)
        nb.radius = std::max(0.04f, spawn_draft_.radius);
    if (spawn_draft_.override_material) {
        nb.custom_material = true;
        nb.custom_iron = std::max(spawn_draft_.material_iron, 0.0f);
        nb.custom_silicate = std::max(spawn_draft_.material_silicate, 0.0f);
        nb.custom_water = std::max(spawn_draft_.material_ice, 0.0f);
        nb.custom_hydrogen = std::max(spawn_draft_.material_hydrogen, 0.0f);
    }
    if (nb.type == CTYPE_NEBULA) {
        nb.material_phase = PHASE_GAS;
        nb.phase_intensity = std::max(nb.phase_intensity, 0.70f);
        nb.custom_material = true;
        nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.85f);
    }
    if ((nb.type == CTYPE_PLANET || nb.type == CTYPE_MOON) && spawn_draft_.planet_look > 0) {
        float mass_earth = nb.mass / std::max(EARTH_MASS_SOLAR, 1.0e-12f);
        bool force_gas = (spawn_draft_.planet_look == 5 && nb.type == CTYPE_PLANET);
        bool likely_gas = mass_earth > 10.0f;
        if (force_gas) {
            nb.forced_surface = 4;
            nb.custom_material = true;
            nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.70f);
        } else if (!likely_gas) {
            switch (spawn_draft_.planet_look) {
            case 1: nb.forced_surface = 0; break;
            case 2: nb.forced_surface = 1; break;
            case 3: nb.forced_surface = 2; break;
            case 4: nb.forced_surface = 3; break;
            default: nb.forced_surface = -1; break;
            }
        }
        nb.props_valid = false;
        nb.visuals_valid = false;
    }
    if (body_can_host_rings(nb) && spawn_draft_.spawn_rings && nb.ring_density <= 0.001f) {
        set_ring_system(nb, nb.radius * 1.5f, nb.radius * 3.0f, 0.32f, 0.55f, 0.12f);
    } else if (!body_can_host_rings(nb)) {
        clear_ring_system(nb);
    }

    refresh_body_render_state(nb, &state);
    preview_body_valid_ = true;
    preview_last_type_ = spawn_type;
    preview_last_mass_ = spawn_mass;
    preview_last_draft_hash_ = draft_settings_hash();
}

void CosmosApp::reroll_spawn_preview() {
    preview_reroll_counter_++;
    preview_body_valid_ = false;  // triggers rebuild next frame
}

int CosmosApp::spawn_preview_body(glm::vec3 pos) {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return -1;

    if (!preview_body_valid_)
        build_preview_body();

    // Nebulae still need to spawn as a cloud
    if (spawn_type == CTYPE_NEBULA) {
        float cloud_r = std::max(35.0f, std::cbrt(spawn_mass) * 120.0f);
        if (spawn_draft_.override_radius)
            cloud_r = std::max(10.0f, spawn_draft_.radius);
        glm::vec3 vel(0.0f);
        if (spawn_draft_.override_velocity)
            vel = spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
        int idx = spawn_nebula_cloud(pos, vel, spawn_mass, cloud_r, preview_body_.seed);
        reroll_spawn_preview();
        return idx;
    }

    // Batch spawning for small bodies (asteroids, comets, dust)
    const bool is_small_body = (spawn_type == CTYPE_ASTEROID || spawn_type == CTYPE_COMET || spawn_type == CTYPE_DUST);
    const int requested_count = std::clamp(spawn_draft_.small_body_spawn_count, 1, 1000);
    const bool use_batch = is_small_body && requested_count > 1;
    const int spawn_count = use_batch ? requested_count : 1;

    float nominal_radius = preview_body_.radius;
    float cluster_radius = std::max(6.0f, std::cbrt((float)spawn_count) * std::max(1.2f, nominal_radius) * 4.0f);

    uint32_t layout_seed = hash_combine(hash_combine(float_bits(pos.x), float_bits(pos.y)), float_bits(pos.z));
    layout_seed = hash_combine(layout_seed, (uint32_t)(sim_time_ * 1000.0f));
    std::mt19937 layout_rng(layout_seed ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
    std::uniform_real_distribution<float> jitter01(-1.0f, 1.0f);

    auto layout_offset = [&](int index) -> glm::vec3 {
        if (!use_batch) return glm::vec3(0.0f);
        const float two_pi = 6.28318530718f;
        const int layout = std::clamp(spawn_draft_.small_body_layout, 0, 3);
        switch (layout) {
        case 1: { // Sphere
            float u = ((float)index + 0.5f) / (float)spawn_count;
            float y = 1.0f - 2.0f * u;
            float rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float theta = 2.39996323f * (float)index;
            glm::vec3 dir(std::cos(theta) * rr, y, std::sin(theta) * rr);
            float radial = cluster_radius * (0.42f + 0.58f * unit01(layout_rng));
            return dir * radial;
        }
        case 2: { // Cube
            int side = std::max(1, (int)std::ceil(std::cbrt((float)spawn_count)));
            int ix = index % side;
            int iy = (index / side) % side;
            int iz = index / (side * side);
            float denom = (side > 1) ? (float)(side - 1) : 1.0f;
            float spacing = (cluster_radius * 2.0f) / denom;
            return glm::vec3(
                ((float)ix - 0.5f * (float)(side - 1)) * spacing,
                ((float)iy - 0.5f * (float)(side - 1)) * spacing,
                ((float)iz - 0.5f * (float)(side - 1)) * spacing);
        }
        case 3: { // Torus
            float t = (float)index / (float)spawn_count;
            float u = t * two_pi;
            float frac = std::fmod((float)index * 0.61803398875f + unit01(layout_rng) * 0.2f, 1.0f);
            if (frac < 0.0f) frac += 1.0f;
            float v = frac * two_pi;
            float major = cluster_radius;
            float minor = cluster_radius * 0.30f;
            float ring = major + minor * std::cos(v);
            return glm::vec3(ring * std::cos(u), minor * std::sin(v), ring * std::sin(u));
        }
        default: { // Random
            return glm::vec3(
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius);
        }
        }
    };

    int first_idx = (int)state.bodies.size();
    for (int batch_i = 0; batch_i < spawn_count; ++batch_i) {
        glm::vec3 spawn_pos = pos + layout_offset(batch_i);

        CelestialBody nb = preview_body_;
        nb.pos = spawn_pos;
        nb.vel = glm::vec3(0.0f);

        // Re-seed each batch body uniquely
        if (use_batch) {
            nb.seed = hash_combine(preview_body_.seed, (uint32_t)(batch_i * 2654435761u + 9719u));
            std::mt19937 body_rng(nb.seed);
            if (spawn_type == CTYPE_ASTEROID)
                randomize_small_body_properties(nb, body_rng, false);
            else if (spawn_type == CTYPE_COMET)
                randomize_small_body_properties(nb, body_rng, true);
            else if (spawn_type == CTYPE_DUST)
                randomize_dust_properties(nb, body_rng);
            if (spawn_draft_.override_radius)
                nb.radius = std::max(0.04f, spawn_draft_.radius);
            if (spawn_draft_.override_temperature)
                nb.temperature = std::clamp(spawn_draft_.temperature, 2.7f, 120000.0f);
        }

        // Orbit insertion
        if (spawn_in_orbit_ && !state.bodies.empty()) {
            int nearest = -1;
            float nearest_dist = 1e9f;
            for (size_t i = 0; i < state.bodies.size(); i++) {
                float d = glm::length(state.bodies[i].pos - nb.pos);
                if (d > 0.1f && d < nearest_dist && state.bodies[i].mass > nb.mass) {
                    nearest_dist = d;
                    nearest = (int)i;
                }
            }
            if (nearest >= 0) {
                nb.parent = nearest;
                glm::vec3 diff = nb.pos - state.bodies[nearest].pos;
                float dist = glm::length(diff);
                if (dist > 0.1f) {
                    float v = std::sqrt(cfg.G * state.bodies[nearest].mass / dist);
                    glm::vec3 dir = glm::normalize(diff);
                    glm::vec3 perp(-dir.z, 0.0f, dir.x);
                    nb.vel = state.bodies[nearest].vel + perp * v;
                }
            }
        }
        if (spawn_draft_.override_velocity)
            nb.vel += spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;

        nb.name = generate_body_name(nb.seed, nb.type);
        nb.non_attracting = (nb.type == CTYPE_DUST) ? cfg.dust_debug_non_attracting : nb.non_attracting;
        refresh_body_render_state(nb, &state);
        state.bodies.push_back(nb);
        state.trails.emplace_back();

        int host_idx = (int)state.bodies.size() - 1;
        const auto spawned = state.bodies[(size_t)host_idx];
        int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
        if (cfg.planetary_rings && spawned.ring_density > 0.001f &&
            body_can_host_rings(spawned)) {
            float annulus = std::max(spawned.ring_outer_radius * spawned.ring_outer_radius -
                                     spawned.ring_inner_radius * spawned.ring_inner_radius, 1.0f);
            float mass_hint = std::max(spawned.mass * spawned.ring_density * 0.00012f *
                                       (annulus / std::max(spawned.radius * spawned.radius, 1.0e-5f)),
                                       std::max(cfg.min_fragment_mass, 1.0e-12f) * 48.0f);
            spawn_dust_ring(host_idx, mass_hint, spawned.ring_inner_radius, spawned.ring_outer_radius,
                            spawned.ring_density, spawned.ring_ice_fraction, spawned.seed ^ 0xD05751EDu,
                            ring_style);
        }
        if (spawn_draft_.spawn_moons && host_idx >= 0 && host_idx < (int)state.bodies.size()) {
            spawn_moons_for_host(host_idx,
                                 std::clamp(spawn_draft_.moon_count, 1, 100),
                                 std::clamp(spawn_draft_.moon_orbit_layout, 0, 4),
                                 std::clamp(spawn_draft_.moon_inclination_deg, 0.0f, 85.0f),
                                 std::clamp(spawn_draft_.moon_spacing_scale, 0.35f, 4.0f));
        }
    }

    if (diagnostics_enabled_)
        validate_body_state("spawn/preview", true);

    // Re-roll preview for next spawn
    reroll_spawn_preview();

    return (first_idx < (int)state.bodies.size()) ? first_idx : -1;
}

glm::vec3 CosmosApp::verlet_auto_orbit_velocity(const CelestialBody& body, const CelestialBody& primary,
                                                float radial_scale, float tangential_scale) const {
    glm::vec3 rel = body.pos - primary.pos;
    float r0 = glm::length(rel);
    if (!std::isfinite(r0) || r0 < 1.0e-5f) return body.vel;
    glm::vec3 r_hat = rel / r0;

    glm::vec3 rel_v_now = body.vel - primary.vel;
    glm::vec3 tangent = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(rel_v_now, r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        return body.vel;
    tangent = glm::normalize(tangent);
    if (glm::dot(rel_v_now, tangent) < 0.0f) tangent *= -1.0f;

    float mu = cfg.G * std::max(primary.mass + body.mass, 1.0e-8f);
    float v_circ = std::sqrt(std::max(mu / std::max(r0, 1.0e-6f), 0.0f));
    float v_rad = glm::dot(rel_v_now, r_hat) * radial_scale;
    float vt_center = std::max(v_circ * tangential_scale, 1.0e-6f);

    float orbital_period = (2.0f * 3.14159265359f * r0) / std::max(v_circ, 1.0e-4f);
    // Guard large-radius edge cases (e.g. Oort cloud distances) against float->int overflow.
    double raw_steps = (double)orbital_period * 0.18 / 0.01;
    if (!std::isfinite(raw_steps))
        raw_steps = 180.0;
    raw_steps = std::clamp(raw_steps, 48.0, 180.0);
    int steps = (int)std::lround(raw_steps);
    float dt_sim = std::clamp(orbital_period * 0.18f / (float)steps, 0.001f, 0.025f);

    auto accel = [&](const glm::vec3& r) {
        float rmag = glm::length(r);
        if (rmag < 1.0e-6f) return glm::vec3(0.0f);
        return -r * (mu / (rmag * rmag * rmag));
    };

    auto score_candidate = [&](float v_tan) {
        glm::vec3 r = rel;
        glm::vec3 v = r_hat * v_rad + tangent * v_tan;
        float score = 0.0f;
        for (int s = 0; s < steps; ++s) {
            glm::vec3 a0 = accel(r);
            glm::vec3 r1 = r + v * dt_sim + 0.5f * a0 * dt_sim * dt_sim;
            glm::vec3 a1 = accel(r1);
            glm::vec3 v1 = v + 0.5f * (a0 + a1) * dt_sim;

            float rr = glm::length(r1);
            if (!std::isfinite(rr) || rr < 1.0e-6f) return 1.0e9f;
            float radial_v = glm::dot(v1, r1 / rr);
            score += std::abs(rr - r0) / std::max(r0, 1.0e-6f);
            score += std::abs(radial_v) / std::max(v_circ, 1.0e-4f) * 0.40f;

            r = r1;
            v = v1;
        }
        score /= (float)steps;
        score += std::abs(v_tan - vt_center) / std::max(v_circ, 1.0e-4f) * 0.10f;
        return score;
    };

    float best_vt = vt_center;
    float best_score = score_candidate(best_vt);
    float lo = vt_center * 0.70f;
    float hi = vt_center * 1.30f;
    const int scans = 16;
    for (int i = 0; i <= scans; ++i) {
        float t = (float)i / (float)scans;
        float vt = lo + (hi - lo) * t;
        float sc = score_candidate(vt);
        if (sc < best_score) {
            best_score = sc;
            best_vt = vt;
        }
    }

    return primary.vel + r_hat * v_rad + tangent * best_vt;
}
