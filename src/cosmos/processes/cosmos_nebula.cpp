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

// ── Galaxy Cloud ─────────────────────────────────────────────────────────────
// Spawns a galaxy as a collection of star particles distributed according to
// morphological type (spiral arms, bulge, disk, halo). Each particle is a small
// star rendered normally. Returns the index of the central supermassive body.
int CosmosApp::spawn_galaxy_cloud(glm::vec3 center, glm::vec3 base_vel,
                                  float total_mass, float galaxy_radius,
                                  uint32_t galaxy_type, uint32_t seed) {
    if (total_mass <= 0.0f || galaxy_radius <= 0.0f)
        return -1;

    constexpr float TWO_PI = 6.28318530718f;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // Particle count: 120-500, budget-aware
    int count = std::clamp((int)std::round(120.0f + std::log10(std::max(total_mass, 1.0f)) * 30.0f), 120, 500);

    if (cfg.dynamic_budget_enabled) {
        int cap = std::max(cfg.dynamic_max_fragments, 0);
        int current = 0;
        for (const auto& b : state.bodies) {
            if (!b.marked_for_removal && !b.non_attracting) ++current;
        }
        count = std::min(count, std::max(0, cap - current));
    }
    // Also respect MAX_SPHERES (2048) hard limit
    int available = 2048 - (int)state.bodies.size();
    count = std::min(count, std::max(0, available - 10)); // leave headroom
    if (count < 20) return -1;

    // Galaxy morphology parameters from type + seed
    float arm_count_f = 2.0f;
    float arm_tightness = 0.4f;
    float bar_strength = 0.0f;
    float bulge_ratio = 0.3f;     // fraction of stars in bulge
    float disk_thickness = 0.06f; // as fraction of radius
    float halo_frac = 0.05f;     // fraction of stars in halo
    float arm_width = 0.12f;     // angular width of arms

    float h0 = u01(rng), h1 = u01(rng), h2 = u01(rng);

    switch (galaxy_type) {
    case CTYPE_GALAXY_SPIRAL:
        arm_count_f = 2.0f + h0 * 4.0f;
        arm_tightness = 0.3f + h1 * 0.4f;
        bar_strength = h2 * 0.3f;
        bulge_ratio = 0.15f + h0 * 0.1f;
        disk_thickness = 0.04f + h1 * 0.04f;
        halo_frac = 0.03f + h2 * 0.04f;
        arm_width = 0.10f + h0 * 0.06f;
        break;
    case CTYPE_GALAXY_ELLIPTICAL:
        arm_count_f = 0.0f;
        bulge_ratio = 0.85f + h0 * 0.15f; // almost all bulge
        disk_thickness = 0.4f + h1 * 0.4f; // thick/spheroidal
        halo_frac = 0.10f + h2 * 0.1f;
        break;
    case CTYPE_GALAXY_LENTICULAR:
        arm_count_f = 0.0f;
        bulge_ratio = 0.45f + h0 * 0.2f;
        disk_thickness = 0.08f + h1 * 0.08f;
        halo_frac = 0.05f + h2 * 0.05f;
        break;
    case CTYPE_GALAXY_IRREGULAR:
        arm_count_f = 1.0f + h0 * 2.0f;
        arm_tightness = 0.05f + h1 * 0.15f;
        bulge_ratio = 0.08f + h0 * 0.12f;
        disk_thickness = 0.08f + h1 * 0.10f;
        halo_frac = 0.02f + h2 * 0.03f;
        arm_width = 0.20f + h0 * 0.15f; // wider, messier arms
        break;
    case CTYPE_GALAXY_DWARF:
        arm_count_f = h0 * 1.5f;
        arm_tightness = 0.1f + h1 * 0.1f;
        bulge_ratio = 0.25f + h0 * 0.2f;
        disk_thickness = 0.10f + h1 * 0.08f;
        halo_frac = 0.04f + h2 * 0.04f;
        break;
    default: break;
    }

    int n_arms = (int)(arm_count_f + 0.5f);
    int n_bulge = (int)(count * bulge_ratio);
    int n_halo = (int)(count * halo_frac);
    int n_disk = count - n_bulge - n_halo;
    if (n_disk < 0) { n_disk = 0; n_bulge = count - n_halo; }

    // Random orientation for the galaxy disk
    float tilt = 0.35f + u01(rng) * 0.55f;  // 20-51 degrees from face-on
    float roll = u01(rng) * TWO_PI;
    // Disk normal (tilted from Y axis)
    glm::vec3 disk_normal(std::sin(tilt) * std::cos(roll), std::cos(tilt),
                          std::sin(tilt) * std::sin(roll));
    disk_normal = glm::normalize(disk_normal);
    // Build orthonormal basis for disk plane
    glm::vec3 disk_u = glm::normalize(glm::cross(disk_normal, glm::vec3(0, 0, 1)));
    if (glm::dot(disk_u, disk_u) < 1e-6f)
        disk_u = glm::normalize(glm::cross(disk_normal, glm::vec3(1, 0, 0)));
    glm::vec3 disk_v = glm::normalize(glm::cross(disk_normal, disk_u));

    // Mass distribution: IMF-like power law
    std::vector<float> masses((size_t)count);
    float wsum = 0.0f;
    for (int i = 0; i < count; ++i) {
        float w = std::pow(u01(rng), 1.5f) + 0.05f; // steeper power law than nebula
        masses[(size_t)i] = w;
        wsum += w;
    }
    // Reserve 5% for central SMBH
    float smbh_mass = total_mass * 0.05f;
    float star_total = total_mass * 0.95f;
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] = star_total * (masses[(size_t)i] / std::max(wsum, 1.0e-6f));

    // Helper: position in disk plane
    auto disk_pos = [&](float r, float theta, float z_offset) -> glm::vec3 {
        return center + disk_u * (r * std::cos(theta)) + disk_v * (r * std::sin(theta))
               + disk_normal * z_offset;
    };

    // Helper: circular orbital velocity at radius r
    auto orbital_vel = [&](float r) -> float {
        if (r < 1.0e-3f) return 0.0f;
        return std::sqrt(cfg.G * total_mass * 0.6f / r); // ~60% enclosed mass approximation
    };

    int center_idx = -1;

    // Spawn central supermassive body first (black hole or massive star as anchor)
    {
        CelestialBody c;
        c.type = CTYPE_STAR_O;
        c.mass = smbh_mass;
        c.radius = std::max(8.0f, std::cbrt(smbh_mass) * 2.0f);
        c.pos = center;
        c.vel = base_vel;
        c.temperature = 35000.0f;
        c.stellar_stage = SSTAGE_MAIN_SEQUENCE;
        c.luminosity = expected_stellar_luminosity(c.mass, c.temperature, c.radius,
                                                    c.stellar_stage, 1.0f);
        c.material_phase = PHASE_PLASMA;
        c.phase_intensity = 1.0f;
        c.seed = hash_combine(seed, 0xCE47E5u);
        c.non_attracting = false;
        c.name = "Galaxy Core";
        clear_ring_system(c);
        clear_impact_signature(c);
        c.props_valid = false;
        c.visuals_valid = false;
        refresh_body_render_state(c, &state);
        state.bodies.push_back(c);
        state.trails.emplace_back();
        center_idx = (int)state.bodies.size() - 1;
    }

    int star_i = 0;

    // ── Bulge stars ──
    for (int i = 0; i < n_bulge; ++i, ++star_i) {
        CelestialBody s;
        float r = galaxy_radius * bulge_ratio * 0.5f * std::pow(u01(rng), 0.5f);
        // Spheroidal distribution
        float theta = u01(rng) * TWO_PI;
        float phi = std::acos(1.0f - 2.0f * u01(rng));
        float x = r * std::sin(phi) * std::cos(theta);
        float y = r * std::sin(phi) * std::sin(theta);
        float z = r * std::cos(phi) * (0.6f + disk_thickness * 3.0f); // slightly flattened
        s.pos = center + disk_u * x + disk_v * y + disk_normal * z;

        s.mass = std::max(masses[(size_t)star_i], 1.0e-8f);
        s.radius = std::max(1.5f, std::cbrt(s.mass) * 3.0f);
        // Bulge stars are older/redder
        s.temperature = 3200.0f + u01(rng) * 3000.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.stellar_stage = (u01(rng) < 0.3f) ? SSTAGE_RED_GIANT : SSTAGE_MAIN_SEQUENCE;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius,
                                                    s.stellar_stage, 0.3f + u01(rng) * 0.5f);
        s.material_phase = PHASE_PLASMA;
        s.phase_intensity = 1.0f;
        s.seed = hash_combine(seed, (uint32_t)(star_i * 2654435761u + 1013904223u));
        s.non_attracting = true; // only center attracts to avoid O(N²) blowup
        s.name = generate_body_name(s.seed, s.type);
        clear_ring_system(s);
        clear_impact_signature(s);
        s.props_valid = false;
        s.visuals_valid = false;

        // Orbital velocity
        float dist = glm::length(s.pos - center);
        if (dist > 0.5f) {
            glm::vec3 radial = glm::normalize(s.pos - center);
            glm::vec3 tangent = glm::normalize(glm::cross(disk_normal, radial));
            float v_orb = orbital_vel(dist) * (0.5f + u01(rng) * 0.6f); // dispersion-dominated
            s.vel = base_vel + tangent * v_orb + glm::vec3(gauss(rng), gauss(rng), gauss(rng)) * v_orb * 0.3f;
        } else {
            s.vel = base_vel;
        }

        refresh_body_render_state(s, &state);
        state.bodies.push_back(s);
        state.trails.emplace_back();
    }

    // ── Disk stars (with spiral arm structure) ──
    for (int i = 0; i < n_disk; ++i, ++star_i) {
        CelestialBody s;
        // Exponential disk profile: most stars near center
        float r_frac = std::pow(u01(rng), 0.7f); // bias toward center
        float r = galaxy_radius * (0.1f + r_frac * 0.85f);

        float base_theta = u01(rng) * TWO_PI;
        float theta = base_theta;

        // Spiral arm enhancement
        if (n_arms > 0) {
            // Logarithmic spiral: theta_arm = tightness * ln(r) + offset
            float nearest_arm_dist = 1e9f;
            float nearest_arm_theta = base_theta;
            for (int a = 0; a < n_arms; ++a) {
                float arm_offset = (float)a * TWO_PI / (float)n_arms;
                float arm_theta = arm_tightness * std::log(std::max(r / (galaxy_radius * 0.1f), 0.01f))
                                  + arm_offset;
                // Bar perturbation near center
                if (bar_strength > 0.01f && r < galaxy_radius * 0.25f) {
                    float bar_angle = u01(rng) * 0.3f; // bar is roughly aligned
                    arm_theta += bar_strength * std::sin(2.0f * arm_theta) * (1.0f - r / (galaxy_radius * 0.25f));
                    (void)bar_angle;
                }
                float diff = std::fmod(base_theta - arm_theta + 3.0f * TWO_PI, TWO_PI);
                if (diff > 3.14159f) diff = TWO_PI - diff;
                if (diff < nearest_arm_dist) {
                    nearest_arm_dist = diff;
                    nearest_arm_theta = arm_theta;
                }
            }
            // 70% of disk stars are pulled into arms, 30% inter-arm
            if (u01(rng) < 0.70f) {
                theta = nearest_arm_theta + gauss(rng) * arm_width;
            }
        }

        float z = gauss(rng) * galaxy_radius * disk_thickness;
        s.pos = disk_pos(r, theta, z);

        s.mass = std::max(masses[(size_t)star_i], 1.0e-8f);
        s.radius = std::max(1.2f, std::cbrt(s.mass) * 3.0f);
        // Disk stars: mix of young (blue) and old (yellow/red)
        float age_roll = u01(rng);
        if (age_roll < 0.15f) {
            s.temperature = 12000.0f + u01(rng) * 25000.0f; // young hot O/B
        } else if (age_roll < 0.35f) {
            s.temperature = 6000.0f + u01(rng) * 6000.0f;   // intermediate A/F
        } else {
            s.temperature = 2800.0f + u01(rng) * 3500.0f;   // older K/M
        }
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.stellar_stage = SSTAGE_MAIN_SEQUENCE;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius,
                                                    s.stellar_stage, 0.5f + u01(rng) * 0.4f);
        s.material_phase = PHASE_PLASMA;
        s.phase_intensity = 1.0f;
        s.seed = hash_combine(seed, (uint32_t)(star_i * 2654435761u + 1013904223u));
        s.non_attracting = true;
        s.name = generate_body_name(s.seed, s.type);
        clear_ring_system(s);
        clear_impact_signature(s);
        s.props_valid = false;
        s.visuals_valid = false;

        // Circular orbital velocity in disk plane
        float dist = glm::length(s.pos - center);
        if (dist > 0.5f) {
            glm::vec3 radial = glm::normalize(s.pos - center);
            glm::vec3 tangent = glm::normalize(glm::cross(disk_normal, radial));
            float v_orb = orbital_vel(dist) * (0.85f + u01(rng) * 0.3f);
            s.vel = base_vel + tangent * v_orb + glm::vec3(gauss(rng), gauss(rng), gauss(rng)) * v_orb * 0.05f;
        } else {
            s.vel = base_vel;
        }

        refresh_body_render_state(s, &state);
        state.bodies.push_back(s);
        state.trails.emplace_back();
    }

    // ── Halo stars (globular cluster-like) ──
    for (int i = 0; i < n_halo; ++i, ++star_i) {
        CelestialBody s;
        // Spherical distribution extending beyond disk
        float r = galaxy_radius * (0.3f + u01(rng) * 1.5f);
        float theta = u01(rng) * TWO_PI;
        float phi = std::acos(1.0f - 2.0f * u01(rng));
        s.pos = center + glm::vec3(r * std::sin(phi) * std::cos(theta),
                                    r * std::cos(phi),
                                    r * std::sin(phi) * std::sin(theta));

        s.mass = std::max(masses[(size_t)std::min(star_i, count - 1)], 1.0e-8f);
        s.radius = std::max(1.0f, std::cbrt(s.mass) * 2.5f);
        // Halo stars are old, red/yellow
        s.temperature = 3000.0f + u01(rng) * 3000.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.stellar_stage = (u01(rng) < 0.4f) ? SSTAGE_RED_GIANT : SSTAGE_MAIN_SEQUENCE;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius,
                                                    s.stellar_stage, 0.1f + u01(rng) * 0.3f);
        s.material_phase = PHASE_PLASMA;
        s.phase_intensity = 1.0f;
        s.seed = hash_combine(seed, (uint32_t)(star_i * 2654435761u + 1013904223u));
        s.non_attracting = true;
        s.name = generate_body_name(s.seed, s.type);
        clear_ring_system(s);
        clear_impact_signature(s);
        s.props_valid = false;
        s.visuals_valid = false;

        // Random orbits (halo has high velocity dispersion)
        float dist = glm::length(s.pos - center);
        if (dist > 0.5f) {
            glm::vec3 radial = glm::normalize(s.pos - center);
            // Random tangent direction for halo
            glm::vec3 rand_dir = glm::normalize(glm::vec3(gauss(rng), gauss(rng), gauss(rng)));
            glm::vec3 tangent = glm::normalize(rand_dir - radial * glm::dot(rand_dir, radial));
            if (glm::dot(tangent, tangent) < 1e-6f)
                tangent = glm::normalize(glm::cross(radial, glm::vec3(0, 1, 0)));
            float v_orb = orbital_vel(dist) * (0.6f + u01(rng) * 0.8f);
            s.vel = base_vel + tangent * v_orb;
        } else {
            s.vel = base_vel;
        }

        refresh_body_render_state(s, &state);
        state.bodies.push_back(s);
        state.trails.emplace_back();
    }

    return center_idx;
}
