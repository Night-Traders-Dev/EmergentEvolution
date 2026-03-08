#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

// ── Nebula Cloud ─────────────────────────────────────────────────────────────
// Spawns a nebula as a cloud of many small gas/dust particles instead of a
// single volumetric body. Particles attract each other via gravity and will
// naturally collapse over time. Returns the index of the central seed particle,
// or -1 on failure.
int CosmosApp::spawn_nebula_cloud(glm::vec3 center, glm::vec3 base_vel,
                                  float total_mass, float cloud_radius,
                                  uint32_t seed) {
    if (total_mass <= 0.0f || cloud_radius <= 0.0f)
        return -1;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // Particle count scales with mass and radius; budget-aware.
    int count = std::clamp((int)std::round(60.0f + std::sqrt(total_mass / 0.001f) * 12.0f +
                                           cloud_radius * 0.4f), 40, 300);

    if (cfg.dynamic_budget_enabled) {
        int cap = std::max(cfg.dynamic_max_fragments, 0);
        int current = 0;
        for (const auto& b : state.bodies) {
            if (!b.marked_for_removal && !b.non_attracting) ++current;
        }
        count = std::min(count, std::max(0, cap - current));
    }
    if (count < 8) return -1;

    // Distribute mass with some variance — a few heavier clumps, many lighter wisps.
    std::vector<float> masses((size_t)count);
    float wsum = 0.0f;
    for (int i = 0; i < count; ++i) {
        // Power-law weights: most particles are small, a few are large clumps.
        float w = std::pow(u01(rng), 0.6f) + 0.1f;
        masses[(size_t)i] = w;
        wsum += w;
    }
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] = total_mass * (masses[(size_t)i] / std::max(wsum, 1.0e-6f));

    // Cloud shape: ellipsoidal with random axis ratios for asymmetry.
    glm::vec3 axis_scale(
        0.6f + u01(rng) * 1.0f,
        0.4f + u01(rng) * 0.7f,
        0.5f + u01(rng) * 1.1f);
    float max_axis = std::max({axis_scale.x, axis_scale.y, axis_scale.z});
    axis_scale /= max_axis; // normalize so largest axis = 1.0

    // Random rotation basis for the cloud orientation.
    float yaw = u01(rng) * 6.28318530718f;
    float pitch = (u01(rng) - 0.5f) * 3.14159265f;
    glm::vec3 fwd(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                  std::cos(pitch) * std::sin(yaw));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    if (glm::dot(right, right) < 1e-6f)
        right = glm::normalize(glm::cross(fwd, glm::vec3(1, 0, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    int seed_idx = -1;
    float base_temp = 20.0f + u01(rng) * 80.0f; // cold gas cloud (20-100K)

    for (int i = 0; i < count; ++i) {
        CelestialBody p;
        p.type = CTYPE_DUST;
        p.mass = std::max(masses[(size_t)i], 1.0e-12f);

        // Position: Gaussian distribution with ellipsoidal shaping.
        // Sigma = cloud_radius/3 so ~99% of particles within cloud_radius.
        float sigma = cloud_radius / 3.0f;
        glm::vec3 local(gauss(rng) * sigma * axis_scale.x,
                        gauss(rng) * sigma * axis_scale.y,
                        gauss(rng) * sigma * axis_scale.z);
        // Rotate into cloud orientation.
        glm::vec3 world_offset = right * local.x + up * local.y + fwd * local.z;
        p.pos = center + world_offset;

        // Radius: tiny gas clumps — visual size from mass.
        float density_factor = 0.8f + u01(rng) * 0.8f;
        p.radius = std::max(0.8f, std::cbrt(p.mass / density_factor) * 35.0f);

        // Velocity: base velocity + slow turbulent motion + slight bulk rotation.
        glm::vec3 offset_from_center = p.pos - glm::vec3(center);
        float dist = glm::length(offset_from_center);
        glm::vec3 turbulent(gauss(rng) * 0.0003f, gauss(rng) * 0.0003f, gauss(rng) * 0.0003f);
        // Slight bulk rotation around cloud axis.
        glm::vec3 rot_vel(0.0f);
        if (dist > 0.1f) {
            glm::vec3 dir = offset_from_center / dist;
            glm::vec3 tangent = glm::normalize(glm::cross(fwd, dir));
            if (glm::dot(tangent, tangent) > 1e-6f)
                rot_vel = tangent * (0.0001f + u01(rng) * 0.0002f);
        }
        p.vel = base_vel + turbulent + rot_vel;

        p.temperature = std::clamp(base_temp * (0.8f + u01(rng) * 0.4f), 10.0f, 200.0f);
        p.material_phase = PHASE_GAS;
        p.phase_intensity = std::clamp(0.5f + u01(rng) * 0.4f, 0.3f, 0.95f);
        p.atmosphere_retention = 0.8f + u01(rng) * 0.2f;
        p.seed = hash_combine(seed, (uint32_t)(i * 2654435761u + 7919u));
        p.custom_material = true;
        p.custom_hydrogen = 0.88f + u01(rng) * 0.08f;
        p.custom_silicate = 0.02f + u01(rng) * 0.04f;
        p.custom_water   = 0.01f + u01(rng) * 0.03f;
        p.custom_iron    = 0.005f + u01(rng) * 0.01f;
        p.angular_vel = (u01(rng) - 0.5f) * 0.001f;
        p.non_attracting = false; // must attract to collapse under gravity
        p.name = generate_body_name(p.seed, p.type);
        clear_ring_system(p);
        clear_impact_signature(p);
        p.props_valid = false;
        p.visuals_valid = false;

        refresh_body_render_state(p, &state);
        state.bodies.push_back(p);
        state.trails.emplace_back();

        // Track the heaviest particle as the "seed" (central clump).
        if (seed_idx < 0 || p.mass > state.bodies[(size_t)seed_idx].mass)
            seed_idx = (int)state.bodies.size() - 1;
    }

    return seed_idx;
}
