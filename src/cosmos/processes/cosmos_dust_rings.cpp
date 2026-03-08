#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

bool CosmosApp::spawn_dust_ring(int host_index, float total_mass, float inner_radius, float outer_radius,
                                float density, float ice_fraction, uint32_t seed_hint, int ring_style) {
    if (!cfg.planetary_rings || host_index < 0 || host_index >= (int)state.bodies.size())
        return false;
    if (total_mass <= 0.0f || !std::isfinite(total_mass))
        return false;

    const CelestialBody host = state.bodies[(size_t)host_index];
    if (host.marked_for_removal || is_star_type(host.type) || is_black_hole_type(host.type))
        return false;

    float ring_mass = total_mass * std::clamp(cfg.ring_mass_scale, 0.1f, 5.0f);
    float density_scaled = density * std::clamp(cfg.ring_density_scale, 0.2f, 3.0f);
    float inner = std::max(inner_radius * std::clamp(cfg.ring_inner_scale, 0.6f, 3.0f), host.radius * 1.15f);
    float outer = std::max(outer_radius * std::clamp(cfg.ring_outer_scale, 0.6f, 3.0f), inner + host.radius * 0.18f);
    int style = std::clamp(ring_style, 0, 6);
    float width = outer - inner;
    if (!std::isfinite(inner) || !std::isfinite(outer) || width <= 1.0e-4f)
        return false;

    float safe_min_mass = std::max(1.0e-13f, std::min(cfg.min_fragment_mass * 0.08f, 1.0e-10f));
    float annulus_span = std::sqrt(std::max(width / std::max(host.radius, 0.1f), 0.1f));
    int count = std::clamp((int)std::round((24.0f + density_scaled * 82.0f + annulus_span * 26.0f +
                                           std::sqrt(ring_mass / std::max(safe_min_mass, 1.0e-13f)) * 0.18f) *
                                           std::clamp(cfg.ring_particle_scale, 0.2f, 5.0f)),
                           24, 420);

    if (cfg.dynamic_budget_enabled) {
        int attract_cap = std::max(cfg.dynamic_max_fragments, 0);
        int non_attract_cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int total_budget_cap = std::max(attract_cap + non_attract_cap + 256, 1);
        count = std::min(count, std::max(0, total_budget_cap - (int)state.bodies.size()));

        // Ring particles are always non_attracting, so check against that cap.
        int cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int current = 0;
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            if (b.non_attracting) ++current;
        }
        count = std::min(count, std::max(0, cap - current));
    }
    // Rings need at least 8 particles to look like a ring, not a single asteroid
    if (count < 8)
        return false;

    count = std::min(count, std::max(8, (int)std::floor(ring_mass / safe_min_mass)));
    if (count < 8)
        return false;

    uint32_t seed = hash_combine(host.seed, seed_hint == 0u ? 0xD057CAFEu : seed_hint);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    float tilt = std::clamp(std::max(host.ring_tilt, 0.02f), 0.02f, 1.30f);
    float yaw = u01(rng) * 6.28318530718f;
    glm::vec3 axis(std::sin(tilt) * std::cos(yaw), std::cos(tilt), std::sin(tilt) * std::sin(yaw));
    if (glm::dot(axis, axis) < 1.0e-8f) axis = glm::vec3(0.0f, 1.0f, 0.0f);
    axis = glm::normalize(axis);
    glm::vec3 basis_u = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(basis_u, basis_u) < 1.0e-8f)
        basis_u = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
    basis_u = glm::normalize(basis_u);
    glm::vec3 basis_v = glm::normalize(glm::cross(axis, basis_u));

    std::vector<float> weights((size_t)count, 0.0f);
    float wsum = 0.0f;
    for (int i = 0; i < count; ++i) {
        weights[(size_t)i] = 0.55f + u01(rng);
        wsum += weights[(size_t)i];
    }

    std::vector<float> masses((size_t)count, safe_min_mass);
    float rem_mass = std::max(0.0f, ring_mass - safe_min_mass * (float)count);
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] += rem_mass * (weights[(size_t)i] / std::max(wsum, 1.0e-6f));
    masses.back() += ring_mass - std::accumulate(masses.begin(), masses.end(), 0.0f);

    float style_thickness = 1.0f;
    if (style == 3) style_thickness = 2.1f;      // torus
    if (style == 5) style_thickness = 1.8f;      // exaggerated geometry
    if (style == 6) style_thickness = 0.9f;      // resonance gaps stay relatively thin
    float max_vertical = std::max(host.radius * (0.006f + density_scaled * 0.02f) *
                                  std::clamp(cfg.ring_thickness_scale, 0.3f, 4.0f) * style_thickness, 0.01f);
    float base_temp = std::clamp(host.temperature * (0.14f + (1.0f - ice_fraction) * 0.12f),
                                 30.0f, 900.0f);
    bool spawned_any = false;

    for (int i = 0; i < count; ++i) {
        float theta = u01(rng) * 6.28318530718f;
        float radial_t = std::pow(u01(rng), 0.75f);
        if (style == 0) { // Saturn
            radial_t = std::pow(u01(rng), 0.60f);
        } else if (style == 1) { // Uranus
            radial_t = std::pow(u01(rng), 1.35f);
        } else if (style == 2) { // Neptune
            radial_t = std::pow(u01(rng), 1.85f);
        } else if (style == 3) { // Torus
            radial_t = std::clamp(0.50f + (u01(rng) * 2.0f - 1.0f) * 0.18f, 0.0f, 1.0f);
        } else if (style == 5) { // Unrealistic geometries
            float lobes = 0.5f + 0.5f * std::sin(theta * 3.0f + u01(rng) * 6.28318530718f);
            radial_t = std::clamp(0.20f + lobes * 0.70f + (u01(rng) * 2.0f - 1.0f) * 0.18f, 0.0f, 1.0f);
        } else if (style == 6) { // Resonance gaps
            for (int iter = 0; iter < 8; ++iter) {
                float candidate = std::pow(u01(rng), 0.78f);
                float g0 = std::abs(candidate - 0.22f);
                float g1 = std::abs(candidate - 0.48f);
                float g2 = std::abs(candidate - 0.74f);
                bool near_gap = (g0 < 0.04f) || (g1 < 0.05f) || (g2 < 0.05f);
                if (!near_gap || u01(rng) < 0.10f) {
                    radial_t = candidate;
                    break;
                }
            }
        }
        float radial = inner + width * radial_t;
        float vertical = (u01(rng) * 2.0f - 1.0f) * max_vertical;
        glm::vec3 ring_dir = basis_u * std::cos(theta) + basis_v * std::sin(theta);
        glm::vec3 rel = ring_dir * radial + axis * vertical;

        CelestialBody dust;
        // Ring particles are a mixture of dust, asteroids, and comets.
        float type_roll = u01(rng);
        if (type_roll < 0.50f)
            dust.type = CTYPE_DUST;
        else if (type_roll < 0.80f)
            dust.type = CTYPE_ASTEROID;
        else
            dust.type = CTYPE_COMET;
        dust.parent = host_index;
        dust.mass = std::max(masses[(size_t)i], safe_min_mass);
        float density_factor = 1.3f + (1.0f - ice_fraction) * 1.2f;
        dust.radius = std::max(dust.type == CTYPE_DUST ? 0.07f : 0.15f,
                               std::cbrt(dust.mass / std::max(density_factor, 0.1f)) *
                               (dust.type == CTYPE_DUST ? 11.0f : 6.0f));
        dust.pos = host.pos + rel;

        // Skip ring particles that would immediately overlap with existing bodies.
        bool overlaps_existing = false;
        for (size_t bi = 0; bi < state.bodies.size(); ++bi) {
            if ((int)bi == host_index || state.bodies[bi].marked_for_removal) continue;
            float d = glm::length(dust.pos - state.bodies[bi].pos);
            float touch = dust.radius + state.bodies[bi].radius;
            if (d < touch * 1.05f) {
                overlaps_existing = true;
                break;
            }
        }
        if (overlaps_existing) continue;

        dust.vel = host.vel;
        dust.temperature = std::clamp(base_temp * (0.88f + u01(rng) * 0.24f), 25.0f, 5000.0f);
        dust.atmosphere_retention = 0.0f;
        dust.material_phase = (dust.temperature < 170.0f) ? PHASE_ICE : PHASE_SOLID;
        dust.phase_intensity = std::clamp(0.25f + ice_fraction * 0.50f + u01(rng) * 0.15f, 0.10f, 1.0f);
        dust.seed = hash_combine(seed, (uint32_t)(i * 2654435761u + 9719u));
        dust.frag_generation = std::max<uint32_t>(1u, host.frag_generation + 1u);
        dust.angular_vel = (u01(rng) * 2.0f - 1.0f) * (dust.type == CTYPE_DUST ? 0.03f : 0.01f);
        dust.non_attracting = true; // all ring debris is non-attracting
        dust.name = generate_body_name(dust.seed, dust.type);
        clear_ring_system(dust);
        clear_impact_signature(dust);

        // Initialize ring dynamics with a velocity-Verlet tuned orbital velocity.
        dust.vel = verlet_auto_orbit_velocity(dust, host, 0.0f, 1.0f + (u01(rng) * 2.0f - 1.0f) * 0.04f);
        dust.vel += axis * ((u01(rng) * 2.0f - 1.0f) * 0.00018f);
        if (!std::isfinite(dust.vel.x) || !std::isfinite(dust.vel.y) || !std::isfinite(dust.vel.z))
            continue;

        refresh_body_render_state(dust, &state);
        state.bodies.push_back(dust);
        state.trails.emplace_back();
        spawned_any = true;
    }

    return spawned_any;
}

void CosmosApp::spawn_moons_for_host(int host_index, int moon_count,
                                     int orbit_layout, float inclination_deg,
                                     float spacing_scale) {
    if (host_index < 0 || host_index >= (int)state.bodies.size())
        return;
    const CelestialBody host_snapshot = state.bodies[(size_t)host_index];
    if (host_snapshot.marked_for_removal || host_snapshot.type != CTYPE_PLANET ||
        is_star_type(host_snapshot.type) || is_black_hole_type(host_snapshot.type))
        return;

    int count = std::clamp(moon_count, 1, 100);
    int layout = std::clamp(orbit_layout, 0, 4);
    float inc_deg = std::clamp(inclination_deg, 0.0f, 85.0f);
    float spacing = std::clamp(spacing_scale, 0.35f, 4.0f);
    uint32_t base_seed = host_snapshot.seed ^ (uint32_t)(std::abs((int)(sim_time_ * 1000.0f))) ^ 0xA11CE55u;
    std::mt19937 moon_rng(base_seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float two_pi = 6.28318530718f;
    const float inc_rad_base = glm::radians(inc_deg);
    const float compact_scale = (layout == 1) ? 0.74f : (layout == 2 ? 1.35f : 1.0f);
    const float spacing_step = 0.28f + 0.92f * spacing * compact_scale;

    auto random_unit = [&]() -> glm::vec3 {
        float z = u01(moon_rng) * 2.0f - 1.0f;
        float t = u01(moon_rng) * two_pi;
        float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        return glm::vec3(std::cos(t) * r, z, std::sin(t) * r);
    };

    float base_phase = u01(moon_rng) * two_pi;
    for (int mi = 0; mi < count; ++mi) {
        CelestialBody moon;
        moon.type = CTYPE_MOON;
        float mass_scale = 1.0f / std::sqrt(std::max((float)count, 1.0f));
        moon.mass = std::clamp(host_snapshot.mass * (0.00008f + u01(moon_rng) * 0.0045f) * mass_scale,
                               1.0e-9f, host_snapshot.mass * 0.15f);
        moon.seed = hash_combine(base_seed, (uint32_t)(mi * 7919 + 17));
        randomize_moon_properties(moon, state, moon_rng);
        moon.parent = host_index;
        float orbit_r = host_snapshot.radius * (2.2f + spacing_step * (float)mi + u01(moon_rng) * (1.2f + 0.9f * spacing));
        if (layout == 3) { // resonant chain
            orbit_r = host_snapshot.radius * (2.3f * std::pow(1.58f, (float)mi));
        }
        float theta = base_phase + (layout == 3 ? (float)mi * 2.0943951f : (float)mi * 0.43f) + u01(moon_rng) * 0.35f;
        float inc_use = inc_rad_base;
        if (layout == 4) inc_use = glm::radians(70.0f);
        if (layout == 1) inc_use *= 0.45f;
        if (layout == 2) inc_use *= 1.35f;
        float inc_jitter = (u01(moon_rng) * 2.0f - 1.0f) * inc_use;

        glm::vec3 rel(0.0f);
        if (layout == 4) { // isotropic cloud
            rel = random_unit() * orbit_r;
        } else {
            rel = glm::vec3(
                std::cos(theta) * orbit_r,
                std::sin(inc_jitter) * orbit_r * (0.15f + 0.85f * u01(moon_rng)),
                std::sin(theta) * orbit_r);
        }
        moon.pos = host_snapshot.pos + rel;
        glm::vec3 r_hat = glm::normalize(rel);
        glm::vec3 orbit_normal = (layout == 4) ? random_unit() : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 tangent = glm::cross(orbit_normal, r_hat);
        if (glm::dot(tangent, tangent) < 1.0e-8f)
            tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
        if (glm::dot(tangent, tangent) < 1.0e-8f)
            tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), r_hat);
        tangent = glm::normalize(tangent);
        float v = std::sqrt(std::max(cfg.G * host_snapshot.mass / std::max(glm::length(rel), 1.0e-4f), 0.0f));
        moon.vel = host_snapshot.vel + tangent * v;
        moon.name = generate_body_name(moon.seed, moon.type);
        refresh_body_render_state(moon, &state);
        state.bodies.push_back(moon);
        state.trails.emplace_back();
    }
}

void CosmosApp::spawn_ring_for_host(int host_index, float inner_mult, float outer_mult,
                                    float density, float ice_fraction, int ring_style) {
    if (host_index < 0 || host_index >= (int)state.bodies.size())
        return;
    CelestialBody& host = state.bodies[(size_t)host_index];
    if (host.marked_for_removal || is_star_type(host.type) || is_black_hole_type(host.type))
        return;
    if (!(host.type == CTYPE_PLANET || host.type == CTYPE_MOON || host.type == CTYPE_NEBULA))
        return;

    float inner = std::max(host.radius * std::max(inner_mult, 1.15f), host.radius * 1.15f);
    float outer = std::max(host.radius * std::max(outer_mult, inner_mult + 0.2f), inner + host.radius * 0.20f);
    host.ring_inner_radius = inner;
    host.ring_outer_radius = outer;
    host.ring_density = std::clamp(density, 0.01f, 1.0f);
    host.ring_ice_fraction = std::clamp(ice_fraction, 0.0f, 1.0f);
    host.ring_tilt = std::clamp(std::max(std::abs(host.angular_vel) * 150.0f, 0.02f), 0.02f, 1.30f);

    float annulus = std::max(host.ring_outer_radius * host.ring_outer_radius -
                             host.ring_inner_radius * host.ring_inner_radius, 1.0f);
    float mass_hint = std::max(host.mass * host.ring_density * 0.00012f *
                               (annulus / std::max(host.radius * host.radius, 1.0e-5f)),
                               std::max(cfg.min_fragment_mass, 1.0e-12f) * 48.0f);
    spawn_dust_ring(host_index, mass_hint, host.ring_inner_radius, host.ring_outer_radius,
                    host.ring_density, host.ring_ice_fraction,
                    hash_combine(host.seed, 0xA77A11u),
                    ring_style);
    // Keep ring visual data — shader renders translucent ring overlay
    host.props_valid = false;
    host.visuals_valid = false;
}

void CosmosApp::apply_dust_debug_mode() {
    for (auto& b : state.bodies) {
        if (b.marked_for_removal || b.type != CTYPE_DUST)
            continue;
        b.non_attracting = cfg.dust_debug_non_attracting;
    }
}
