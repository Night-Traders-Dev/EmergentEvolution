#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

float spin_fragmentation_critical_omega(const CelestialBody& body, float G) {
    if (!std::isfinite(body.mass) || !std::isfinite(body.radius) ||
        body.mass <= 0.0f || body.radius <= 0.0f) {
        return 0.0f;
    }

    float radius = std::max(body.radius, body.type == CTYPE_DUST ? 0.04f : 0.1f);
    float omega = std::sqrt(std::max(G * body.mass / std::max(radius * radius * radius, 1.0e-8f), 0.0f));
    if (!std::isfinite(omega) || omega <= 0.0f)
        return 0.0f;

    MaterialComposition materials = derive_materials(body);
    float material_factor = 1.0f;
    if (gas_dominated_body(body, materials)) {
        material_factor *= 0.72f;
    } else if (roche_secondary_fluid_like(body)) {
        material_factor *= 0.82f;
    }
    if (body.type == CTYPE_COMET) material_factor *= 0.80f;
    if (body.type == CTYPE_DUST) material_factor *= 0.72f;
    if (body.type == CTYPE_ASTEROID) material_factor *= 0.92f;
    if (materials.water > 0.45f) material_factor *= 0.93f;
    if (materials.iron > 0.35f) material_factor *= 1.08f;

    switch (body.material_phase) {
    case PHASE_LIQUID:
    case PHASE_MOLTEN:
        material_factor *= 0.82f;
        break;
    case PHASE_GAS:
    case PHASE_PLASMA:
    case PHASE_COLLAPSING:
        material_factor *= 0.68f;
        break;
    default:
        break;
    }

    material_factor = std::clamp(material_factor, 0.45f, 1.20f);
    return omega * material_factor;
}

float spin_fragmentation_ratio(const CelestialBody& body, float G) {
    float critical = spin_fragmentation_critical_omega(body, G);
    if (critical <= 0.0f)
        return 0.0f;
    // Physical condition is (ω/ω_crit)² — centrifugal vs gravitational energy
    float ratio = std::abs(body.angular_vel) / critical;
    return ratio * ratio;
}

glm::vec3 spin_fragmentation_axis(const CelestialBody& body) {
    if (glm::dot(body.vel, body.vel) > 1.0e-10f)
        return glm::normalize(body.vel);

    uint32_t h0 = hash_combine(body.seed, 0x5311u);
    uint32_t h1 = hash_combine(body.seed, 0x9F02u);
    uint32_t h2 = hash_combine(body.seed, 0xC733u);
    glm::vec3 axis(hash_float(h0) * 2.0f - 1.0f,
                   hash_float(h1) * 2.0f - 1.0f,
                   hash_float(h2) * 2.0f - 1.0f);
    if (glm::dot(axis, axis) < 1.0e-10f)
        axis = glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(axis);
}


void CosmosApp::process_spin_fragmentation(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
    float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
    float dt_limited = std::clamp(dt, 1.0e-4f, 2.0f);

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        if (is_star_type(bodies[i].type) || is_black_hole_type(bodies[i].type) ||
            bodies[i].type == CTYPE_NEBULA) {
            continue;
        }

        float critical = spin_fragmentation_critical_omega(bodies[i], cfg.G);
        if (critical <= 0.0f) continue;

        float spin_ratio = spin_fragmentation_ratio(bodies[i], cfg.G);
        if (!std::isfinite(spin_ratio) || spin_ratio <= spin_threshold)
            continue;

        float spin_cap = critical * std::max(spin_threshold * 0.94f, 0.25f);
        if (bodies[i].mass < effective_min_frag_mass * 2.0f ||
            (int)bodies[i].frag_generation >= cfg.max_frag_generation) {
            bodies[i].angular_vel = std::copysign(std::min(std::abs(bodies[i].angular_vel), spin_cap),
                                                  bodies[i].angular_vel);
            continue;
        }

        float overflow = spin_ratio - spin_threshold;
        float max_strip = (bodies[i].type == CTYPE_ASTEROID || bodies[i].type == CTYPE_COMET || bodies[i].type == CTYPE_DUST)
            ? 0.38f : 0.30f;
        float strip_fraction = std::clamp(0.06f + overflow * 0.24f + dt_limited * 0.02f, 0.04f, max_strip);
        if (spin_ratio > spin_threshold * 1.35f)
            strip_fraction = std::max(strip_fraction, max_strip * 0.72f);

        float original_mass = bodies[i].mass;
        float stripped_mass = std::min(original_mass * strip_fraction,
                                       std::max(original_mass - effective_min_frag_mass * 1.25f, 0.0f));
        if (stripped_mass <= effective_min_frag_mass * 0.25f) {
            bodies[i].angular_vel = std::copysign(std::min(std::abs(bodies[i].angular_vel), spin_cap),
                                                  bodies[i].angular_vel);
            continue;
        }

        MaterialComposition materials = derive_materials(bodies[i]);
        bool gas_dominated = gas_dominated_body(bodies[i], materials);
        bool fluid_like = roche_secondary_fluid_like(bodies[i]);
        float radius = std::max(bodies[i].radius, bodies[i].type == CTYPE_DUST ? 0.04f : 0.1f);
        float surface_speed = std::abs(bodies[i].angular_vel) * radius;
        float excess_surface_speed = std::max(surface_speed - critical * radius * spin_threshold, 0.0f);
        float ejecta_speed = std::clamp(
            excess_surface_speed * (gas_dominated ? 0.12f : 0.18f) +
            critical * radius * (fluid_like ? 0.014f : 0.020f),
            0.00025f,
            gas_dominated ? 0.0045f : (bodies[i].type == CTYPE_DUST ? 0.006f : 0.015f));
        int fragment_count = std::clamp(
            std::max(2, cfg.fragment_count / 2 + (int)std::floor(overflow * 5.0f)),
            2, 8);
        glm::vec3 fragment_axis = spin_fragmentation_axis(bodies[i]);
        uint32_t parent_generation = bodies[i].frag_generation;
        float temperature = bodies[i].temperature;

        spawn_fragments(bodies[i].pos, bodies[i].vel, stripped_mass, fragment_count,
                        parent_generation, temperature, fragment_axis, ejecta_speed,
                        &bodies[i], 0.30f + overflow * 0.75f);
        register_mass_loss(bodies[i], stripped_mass, std::max(dt_limited, 1.0e-4f));

        float remaining_mass = std::max(original_mass - stripped_mass, 0.0f);
        if (remaining_mass <= effective_min_frag_mass) {
            bodies[i].marked_for_removal = true;
            continue;
        }

        float mass_scale = std::cbrt(remaining_mass / std::max(original_mass, 1.0e-12f));
        bodies[i].mass = remaining_mass;
        bodies[i].radius = std::max(bodies[i].radius * mass_scale, bodies[i].type == CTYPE_DUST ? 0.06f : 0.1f);
        bodies[i].frag_generation = std::min<uint32_t>(parent_generation + 1u, (uint32_t)cfg.max_frag_generation);
        float post_spin_cap = spin_fragmentation_critical_omega(bodies[i], cfg.G) *
            std::max(spin_threshold * 0.94f, 0.25f);
        if (post_spin_cap > 0.0f) {
            float damped_spin = std::abs(bodies[i].angular_vel) * std::max(0.52f, mass_scale * 0.82f);
            bodies[i].angular_vel = std::copysign(std::min(damped_spin, post_spin_cap), bodies[i].angular_vel);
        }
        if (cfg.temperature_system) {
            float spin_heat = stripped_mass * surface_speed * surface_speed * 0.04f;
            bodies[i].internal_energy += spin_heat;
            bodies[i].temperature += std::min(spin_heat / std::max(bodies[i].mass * 0.08f, 1.0e-7f), 2200.0f);
        }
        bodies[i].props_valid = false;
        bodies[i].visuals_valid = false;
        apply_impact_signature(
            bodies[i], fragment_axis,
            std::clamp(0.24f + overflow * 0.55f, 0.0f, 1.2f),
            stripped_mass / std::max(original_mass, 1.0e-8f),
            std::clamp(surface_speed * 0.08f, 0.0f, 1.0f),
            std::clamp(0.18f + overflow * 0.25f, 0.10f, 0.85f));
    }
}
