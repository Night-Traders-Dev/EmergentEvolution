#include "cosmos/cosmos_app_internal.h"
#include <algorithm>
#include <cmath>
#include <random>

// ── Stellar Evolution ───────────────────────────────────────────────────────

void CosmosApp::process_stellar_evolution(float dt) {
    auto& bodies = state.bodies;
    const float dt_step = std::abs(dt);
    if (dt_step <= 0.0f)
        return;

    struct PendingSinkStar {
        CelestialBody body;
    };
    std::vector<PendingSinkStar> pending_sink_stars;
    pending_sink_stars.reserve(16);

    // Dense nebula sink creation: convert converging cloud cores into attracting
    // protostar/star particles and consume gas mass from the host cloud.
    if (cfg.nebula_sink_formation) for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& host = bodies[i];
        if (host.marked_for_removal) continue;
        if (is_star_type(host.type) || is_black_hole_type(host.type)) continue;
        if (host.type != CTYPE_NEBULA) continue;
        if (host.non_attracting) continue;
        if (host.mass < std::max(cfg.nebula_sink_min_mass * 1.5f, 1.0e-7f)) continue;

        float sink_radius = std::max(host.radius * 1.1f, 20.0f);
        float converging = 0.0f;
        float local_feed = 0.0f;
        glm::vec3 mean_flow(0.0f);

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            const CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;
            MaterialComposition dm = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, dm)) continue;

            glm::vec3 delta = donor.pos - host.pos;
            float dist = glm::length(delta);
            if (dist > sink_radius + donor.radius * 2.0f) continue;
            glm::vec3 rel_v = donor.vel - host.vel;
            float weight = 1.0f - std::clamp(dist / std::max(sink_radius, 1.0e-5f), 0.0f, 1.0f);
            if (weight <= 0.0f) continue;
            local_feed += donor.mass * weight;
            mean_flow += rel_v * weight;
            if (dist > 1.0e-5f) {
                float inward = -glm::dot(rel_v, delta) / std::max(dist, 1.0e-5f);
                if (inward > 0.0f) converging += inward * weight;
            }
        }

        float density = body_density(host);
        float gravity_drive = cfg.G * host.mass / std::max(host.radius * host.radius, 1.0e-6f);
        float collapse_metric =
            density * 2.0e6f +
            converging * 0.20f +
            gravity_drive * 0.50f +
            host.collapse_progress * 1.50f +
            host.phase_intensity * 0.15f;
        if (collapse_metric < cfg.nebula_sink_threshold) continue;

        float spawn_frac = std::clamp(cfg.nebula_sink_spawn_fraction, 0.001f, 0.50f);
        float spawn_mass = std::max(host.mass * spawn_frac, local_feed * 0.22f);
        spawn_mass = std::clamp(spawn_mass, std::max(cfg.nebula_sink_min_mass, 1.0e-7f), host.mass * 0.28f);
        if (spawn_mass < std::max(cfg.nebula_sink_min_mass, 1.0e-7f)) continue;

        CelestialBody sink = host;
        sink.mass = spawn_mass;
        float seed_phase = hash_float(hash_combine(host.seed, (uint32_t)(i * 2654435761u + 17u)));
        float seed_phi = hash_float(hash_combine(host.seed, (uint32_t)(i * 2246822519u + 29u))) * 6.28318530718f;
        float z = seed_phase * 2.0f - 1.0f;
        float rxy = std::sqrt(std::max(0.0f, 1.0f - z * z));
        glm::vec3 dir(std::cos(seed_phi) * rxy, z, std::sin(seed_phi) * rxy);
        sink.pos = host.pos + dir * std::max(host.radius * 0.22f, 8.0f);
        if (glm::length(mean_flow) > 1.0e-6f)
            sink.vel = host.vel + glm::normalize(mean_flow) * std::min(0.35f * glm::length(mean_flow), 2.5f);
        else
            sink.vel = host.vel;

        sink.stellar_stage = SSTAGE_MAIN_SEQUENCE;
        float proto_temp = std::max(220.0f, expected_main_sequence_temperature(std::max(sink.mass, 0.003f)) * 0.54f);
        sink.temperature = std::clamp(std::max(host.temperature, proto_temp), 120.0f, 26000.0f);
        sink.type = classify_star_spectral(std::max(sink.temperature, 250.0f), std::max(sink.mass, 0.003f));
        sink.radius = std::max(expected_star_radius(sink) * 1.75f, std::cbrt(std::max(sink.mass, 1.0e-8f)) * 44.0f);
        sink.fuel = std::clamp(std::max(host.fuel, 0.65f), 0.25f, 1.0f);
        sink.luminosity = expected_stellar_luminosity(sink.mass, sink.temperature, sink.radius,
                                                      sink.stellar_stage, sink.fuel);
        sink.material_phase = PHASE_COLLAPSING;
        sink.phase_intensity = std::max(host.phase_intensity, 0.55f);
        sink.collapse_progress = std::clamp(host.collapse_progress + 0.18f, 0.20f, 0.98f);
        sink.non_attracting = false;
        sink.props_valid = false;
        sink.visuals_valid = false;
        clear_ring_system(sink);
        clear_impact_signature(sink);
        sink.name = generate_body_name(hash_combine(host.seed, (uint32_t)(i * 747796405u + 0x53544B52u)), sink.type);

        float consumed = spawn_mass * std::clamp(cfg.nebula_sink_consume_fraction, 0.05f, 1.0f);
        host.mass = std::max(host.mass - consumed, 1.0e-8f);
        host.collapse_progress = std::clamp(host.collapse_progress + 0.04f, 0.0f, 1.35f);
        host.internal_energy += consumed * 2.5f;
        host.temperature = std::clamp(host.temperature + collapse_metric * 6.0f, 15.0f, 60000.0f);
        host.props_valid = false;
        host.visuals_valid = false;
        register_mass_loss(host, consumed, std::max(dt_step, 1.0e-4f));

        pending_sink_stars.push_back(PendingSinkStar{sink});
    }

    std::vector<float> recent_star_accretion(bodies.size(), 0.0f);

    // Cloud concentration: dense gas/dust environments collapse toward protostars.
    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& host = bodies[i];
        if (host.marked_for_removal) continue;
        if (is_star_type(host.type) || is_black_hole_type(host.type)) continue;

        bool cloud_host = (host.type == CTYPE_NEBULA) ||
            ((host.type == CTYPE_DUST || host.type == CTYPE_COMET) &&
             host.mass > 2.0e-4f && !host.non_attracting);
        if (!cloud_host) continue;

        float host_mass_before = std::max(host.mass, 1.0e-12f);
        float capture = cloud_capture_radius(host);
        float mass_gain = 0.0f;
        float hydrogen_gain = 0.0f;
        float concentration_gain = 0.0f;

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;

            MaterialComposition donor_m = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, donor_m)) continue;

            glm::vec3 delta = donor.pos - host.pos;
            float dist = glm::length(delta);
            if (dist > capture + donor.radius * 1.8f) continue;

            glm::vec3 rel_v = donor.vel - host.vel;
            float rel_speed = glm::length(rel_v);
            float esc = std::sqrt(std::max(2.0f * cfg.G * (host.mass + donor.mass) /
                                           std::max(dist, 1.0e-5f), 0.0f));
            if (rel_speed > esc * 1.85f && dist > host.radius * 1.15f) continue;

            float proximity = 1.0f - std::clamp(dist / std::max(capture + donor.radius, 1.0e-5f), 0.0f, 1.0f);
            float absorb = (0.06f + proximity * 0.36f +
                            (donor.non_attracting ? 0.28f : 0.0f) +
                            std::clamp((esc - rel_speed) / std::max(esc, 1.0e-5f), -0.12f, 0.25f)) * dt_step;
            if (dist < host.radius * 0.95f)
                absorb = std::max(absorb, 0.92f);
            absorb = std::clamp(absorb, 0.0f, 1.0f);
            if (absorb <= 1.0e-5f) continue;

            float gained = donor.mass * absorb;
            if (gained <= 1.0e-12f) continue;

            float donor_before = donor.mass;
            donor.mass = std::max(donor.mass - gained, 0.0f);
            register_mass_loss(donor, gained, std::max(dt_step, 1.0e-4f));
            concentration_gain += gained * (0.45f + proximity * 0.55f);
            mass_gain += gained;
            hydrogen_gain += gained * donor_m.hydrogen;

            if (donor.mass <= std::max(1.0e-12f, cfg.min_fragment_mass * 0.10f)) {
                donor.marked_for_removal = true;
            } else {
                float mass_scale = std::cbrt(donor.mass / std::max(donor_before, 1.0e-12f));
                donor.radius = std::max(donor.radius * mass_scale, donor.type == CTYPE_DUST ? 0.06f : 0.1f);
                donor.props_valid = false;
                donor.visuals_valid = false;
            }
        }

        if (mass_gain <= 0.0f) continue;

        host.mass += mass_gain;
        float gain_ratio = mass_gain / std::max(host_mass_before, 1.0e-8f);
        float concentration_ratio = concentration_gain / std::max(host_mass_before, 1.0e-8f);
        host.temperature = std::clamp(host.temperature +
            std::min(200.0f * gain_ratio + concentration_ratio * 60.0f, 1500.0f),
            15.0f, 60000.0f);
        host.internal_energy += mass_gain * (4.0f + concentration_ratio * 3.0f);
        host.collapse_progress = std::clamp(
            host.collapse_progress + gain_ratio * 0.12f + concentration_ratio * 0.06f + dt_step * 0.00008f,
            0.0f, 1.35f);

        // Only promote dust/comet to nebula — never convert planets/moons.
        if (host.type != CTYPE_NEBULA && host.mass > 8.0e-4f &&
            (host.type == CTYPE_DUST || host.type == CTYPE_COMET)) {
            host.type = CTYPE_NEBULA;
            host.material_phase = PHASE_GAS;
            host.phase_intensity = std::max(host.phase_intensity, 0.45f);
        }

        host.fuel = std::clamp((host.fuel * host_mass_before + hydrogen_gain * 0.90f) /
                               std::max(host.mass, 1.0e-8f), 0.0f, 1.0f);

        float proto_threshold = HYDROGEN_BURNING_MASS_SOLAR * 0.92f;
        bool ignite = host.mass >= proto_threshold &&
            host.collapse_progress > 0.85f &&
            host.temperature > 3500.0f;
        if (ignite) {
            float proto_temp = std::max(expected_main_sequence_temperature(std::max(host.mass, 0.003f)) * 0.96f, 250.0f);
            host.type = classify_star_spectral(std::max(host.temperature, proto_temp), std::max(host.mass, 0.003f));
            host.stellar_stage = SSTAGE_MAIN_SEQUENCE;
            host.fuel = std::clamp(std::max(host.fuel, 0.72f), 0.12f, 1.0f);
            host.temperature = std::clamp(std::max(host.temperature, proto_temp), 250.0f, 60000.0f);
            host.radius = expected_star_radius(host);
            host.luminosity = expected_stellar_luminosity(host.mass, host.temperature, host.radius,
                                                          host.stellar_stage, host.fuel);
            host.material_phase = PHASE_PLASMA;
            host.phase_intensity = 1.0f;
            host.collapse_progress = 1.0f;
            clear_ring_system(host);
        } else {
            float target_radius = std::max(18.0f, std::cbrt(std::max(host.mass, 1.0e-8f)) * 105.0f);
            host.radius = glm::mix(host.radius, target_radius, std::clamp(0.02f + gain_ratio * 0.22f, 0.02f, 0.30f));
            host.material_phase = PHASE_COLLAPSING;
            host.phase_intensity = std::max(host.phase_intensity, std::clamp(host.collapse_progress, 0.2f, 1.0f));
        }

        host.props_valid = false;
        host.visuals_valid = false;
    }

    for (const auto& p : pending_sink_stars) {
        CelestialBody sink = p.body;
        refresh_body_render_state(sink, &state);
        state.bodies.push_back(sink);
        state.trails.emplace_back();
    }
    recent_star_accretion.resize(bodies.size(), 0.0f);

    // Stellar accretion: stars gain mass and hydrogen fuel from nearby feedstock.
    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& star = bodies[i];
        if (star.marked_for_removal) continue;
        if (!is_star_type(star.type)) continue;

        float mass_before = std::max(star.mass, 1.0e-8f);
        float capture = stellar_capture_radius(star);
        float mass_gain = 0.0f;
        float hydrogen_gain = 0.0f;
        float kinetic_gain = 0.0f;

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;

            MaterialComposition donor_m = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, donor_m)) continue;

            float donor_reach = capture + donor.radius * (donor.type == CTYPE_DUST ? 6.0f : 2.0f);
            glm::vec3 delta = donor.pos - star.pos;
            float dist = glm::length(delta);
            if (dist > donor_reach) continue;

            glm::vec3 rel_v = donor.vel - star.vel;
            float rel_speed = glm::length(rel_v);
            float esc = std::sqrt(std::max(2.0f * cfg.G * star.mass / std::max(dist, 1.0e-5f), 0.0f));
            if (rel_speed > esc * 2.4f && dist > star.radius * 1.1f) continue;

            float proximity = 1.0f - std::clamp(dist / std::max(donor_reach, 1.0e-5f), 0.0f, 1.0f);
            float absorb = (0.09f + proximity * 0.78f +
                            (donor.non_attracting ? 0.25f : 0.0f) +
                            std::clamp((esc - rel_speed) / std::max(esc, 1.0e-5f), -0.12f, 0.42f)) * dt_step;
            if (dist <= star.radius * 1.05f)
                absorb = std::max(absorb, 0.98f);
            absorb = std::clamp(absorb, 0.0f, 1.0f);
            if (absorb <= 1.0e-5f) continue;

            float gained = donor.mass * absorb;
            if (gained <= 1.0e-12f) continue;

            float donor_before = donor.mass;
            donor.mass = std::max(donor.mass - gained, 0.0f);
            register_mass_loss(donor, gained, std::max(dt_step, 1.0e-4f));
            mass_gain += gained;
            hydrogen_gain += gained * donor_m.hydrogen;
            kinetic_gain += 0.5f * gained * rel_speed * rel_speed;

            if (donor.mass <= std::max(1.0e-12f, cfg.min_fragment_mass * 0.10f)) {
                donor.marked_for_removal = true;
            } else {
                float mass_scale = std::cbrt(donor.mass / std::max(donor_before, 1.0e-12f));
                donor.radius = std::max(donor.radius * mass_scale, donor.type == CTYPE_DUST ? 0.06f : 0.1f);
                donor.props_valid = false;
                donor.visuals_valid = false;
            }
        }

        if (mass_gain <= 0.0f) continue;

        star.mass += mass_gain;
        float acc_ratio = mass_gain / std::max(mass_before, 1.0e-8f);
        float fuel_mass = star.fuel * mass_before + hydrogen_gain * 0.92f + mass_gain * 0.03f;
        star.fuel = std::clamp(fuel_mass / std::max(star.mass, 1.0e-8f) +
                               std::clamp(acc_ratio * 0.06f, 0.0f, 0.10f), 0.0f, 1.0f);
        float heating = kinetic_gain / std::max(star.mass, 1.0e-8f) * 0.65f + acc_ratio * 1800.0f;
        star.temperature = std::clamp(star.temperature + std::min(heating, 22000.0f), 1800.0f, 140000.0f);
        star.internal_energy += kinetic_gain * 0.18f + mass_gain * 10.0f;
        star.type = classify_star_spectral(std::max(star.temperature, 250.0f), std::max(star.mass, 0.003f));
        star.radius = glm::mix(star.radius, expected_star_radius(star),
                               std::clamp(0.04f + acc_ratio * 0.55f, 0.04f, 0.32f));
        star.luminosity = expected_stellar_luminosity(star.mass, star.temperature, star.radius,
                                                      star.stellar_stage, star.fuel);
        star.props_valid = false;
        star.visuals_valid = false;
        recent_star_accretion[i] = mass_gain;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal) continue;
        if (!is_star_type(b.type)) continue;

        if (b.stellar_stage == SSTAGE_WHITE_DWARF && b.mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
            trigger_stellar_supernova(i, dt_step, true);
            continue;
        }

        float accreted_mass = recent_star_accretion[i];
        if (accreted_mass > 0.0f) {
            float pre_mass = std::max(b.mass - accreted_mass, 1.0e-8f);
            float acc_ratio = accreted_mass / pre_mass;
            bool runaway_supernova =
                b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
                ((acc_ratio > 0.18f && b.fuel < 0.45f) ||
                 (b.mass > 40.0f && acc_ratio > 0.08f && b.fuel < 0.30f));
            bool collapse_to_bh =
                (b.mass > 85.0f) ||
                (b.mass > 60.0f && acc_ratio > 0.22f && b.fuel < 0.25f);
            if (runaway_supernova || collapse_to_bh) {
                float escape = body_escape_velocity(b, cfg.G);
                float ejecta_speed = std::max(escape * (collapse_to_bh ? 0.65f : 0.85f), 24.0f);
                trigger_stellar_supernova(i, dt_step, false, glm::vec3(0.0f, 1.0f, 0.0f), ejecta_speed);
                continue;
            }
        }

        bool evolved_star =
            b.stellar_stage == SSTAGE_RED_GIANT ||
            b.stellar_stage == SSTAGE_SUPERGIANT ||
            b.stellar_stage == SSTAGE_HYPERGIANT;
        bool compact_star =
            b.stellar_stage == SSTAGE_WHITE_DWARF ||
            b.stellar_stage == SSTAGE_NEUTRON_STAR;

        // Main-sequence lifetime roughly scales as M^-2.5, with post-main-sequence
        // phases burning the remaining fuel much faster.
        float stellar_mass = std::clamp(b.mass, 0.08f, 120.0f);
        float lifetime_scale = cfg.stellar_timescale * 250000.0f;
        float main_sequence_lifetime = lifetime_scale / std::pow(stellar_mass, 2.5f);
        float burn_multiplier = compact_star ? 0.0f : (evolved_star ? 7.5f : 1.0f);
        float burn_rate = (burn_multiplier > 0.0f)
            ? dt_step * burn_multiplier / std::max(main_sequence_lifetime, cfg.stellar_timescale * 1200.0f)
            : 0.0f;
        // Cap fuel burn per step to prevent catastrophic aging at high time scales
        constexpr float kMaxFuelBurnPerStep = 0.0005f;
        burn_rate = std::min(burn_rate, kMaxFuelBurnPerStep);
        b.fuel -= burn_rate;
        if (b.fuel < 0.0f) b.fuel = 0.0f;

        float wind_factor = compact_star ? 0.0f : (evolved_star ? 2.6f : 1.0f);
        float wind_loss = std::min(b.mass * wind_factor * (0.00002f + b.luminosity * 0.000002f) * dt_step,
                                   b.mass * (compact_star ? 0.0f : 0.00005f));
        if (wind_loss > 0.0f) {
            b.mass -= wind_loss;
            register_mass_loss(b, wind_loss, dt_step);
        }

        // Reclassify spectral type as temperature changes
        b.type = classify_star_spectral(b.temperature, b.mass);

        // Main sequence → evolved giant / supergiant
        float giant_threshold = (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.45f : 0.30f;
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE && b.fuel < giant_threshold) {
            if (b.mass >= 20.0f)
                b.stellar_stage = SSTAGE_HYPERGIANT;
            else if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR)
                b.stellar_stage = SSTAGE_SUPERGIANT;
            else
                b.stellar_stage = SSTAGE_RED_GIANT;
            b.radius = expected_star_radius(b);
            b.temperature = std::clamp(b.temperature *
                ((b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.66f : 0.54f), 2800.0f, 32000.0f);
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
        }

        if (evolved_star && b.fuel < 0.05f) {
            if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) {
                trigger_stellar_supernova(i, dt_step, false);
                continue;
            }

            // Lower-mass stars end as white dwarfs after envelope loss.
            b.stellar_stage = SSTAGE_WHITE_DWARF;
            float lost = std::max(b.mass - std::clamp(0.48f + b.mass * 0.10f, 0.45f, 1.30f), 0.0f);
            if (lost > 0.0f) {
                b.mass -= lost;
                register_mass_loss(b, lost, dt_step);
            }
            b.temperature = std::clamp(22000.0f + b.mass * 4000.0f, 9000.0f, 120000.0f);
            b.radius = expected_star_radius(b);
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
            b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        }

        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
    }
}
