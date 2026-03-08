#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

// ── Fragment Spawning ───────────────────────────────────────────────────────

void CosmosApp::spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count,
                                uint32_t parent_generation, float source_temperature,
                                glm::vec3 impact_axis, float ejecta_speed,
                                const CelestialBody* source_body, float shock_ratio) {
    if (count < 1 || total_mass <= 0.0f) return;
    if (!std::isfinite(total_mass) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        !std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z)) return;
    if ((int)parent_generation >= cfg.max_frag_generation) return;

    size_t body_count = state.bodies.size();
    if (cfg.dynamic_budget_enabled) {
        int attract_cap = std::max(cfg.dynamic_max_fragments, 0);
        int non_attract_cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int total_budget_cap = std::max(attract_cap + non_attract_cap + 256, 1);
        if ((int)body_count >= total_budget_cap) return;
        int capacity = total_budget_cap - (int)body_count;
        count = std::min(count, capacity);

        int per_event_cap = std::max(
            1, (int)std::ceil((float)non_attract_cap *
                std::clamp(cfg.dynamic_explosion_density, 0.01f, 1.0f)));
        count = std::min(count, per_event_cap);
    }
    if (count < 1) return;

    float kMinFragmentMass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
    int max_count = std::max(1, (int)std::floor(total_mass / kMinFragmentMass));
    count = std::min(count, max_count);
    if (count < 1) count = 1;

    uint32_t seed = (uint32_t)(std::hash<float>{}(pos.x) ^ std::hash<float>{}(pos.y) ^
                               std::hash<float>{}(pos.z) ^ std::hash<float>{}(total_mass) ^
                               (parent_generation * 2654435761u));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    std::vector<float> weights((size_t)count, 0.0f);
    float weight_sum = 0.0f;
    for (int i = 0; i < count; ++i) {
        weights[(size_t)i] = 0.25f + u01(rng);
        weight_sum += weights[(size_t)i];
    }

    std::vector<float> masses((size_t)count, kMinFragmentMass);
    float remaining_mass = total_mass - kMinFragmentMass * (float)count;
    if (remaining_mass < 0.0f) remaining_mass = 0.0f;
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] += remaining_mass * (weights[(size_t)i] / std::max(weight_sum, 1.0e-6f));
    masses.back() += total_mass - std::accumulate(masses.begin(), masses.end(), 0.0f);

    auto safe_normalize = [](const glm::vec3& v, const glm::vec3& fallback) {
        float l2 = glm::dot(v, v);
        if (!std::isfinite(l2) || l2 < 1.0e-12f) return fallback;
        return v / std::sqrt(l2);
    };
    glm::vec3 axis = safe_normalize(impact_axis, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(axis, axis) < 1.0e-8f) {
        axis = safe_normalize(glm::vec3(u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    }
    MaterialComposition source_materials{};
    if (source_body != nullptr) {
        source_materials = derive_materials(*source_body);
    } else {
        source_materials.silicate = 0.75f;
        source_materials.iron = 0.25f;
    }
    bool energetic_source = source_body != nullptr &&
        (is_star_type(source_body->type) || is_black_hole_type(source_body->type) ||
         source_body->type == CTYPE_NEBULA);
    float min_ejecta = energetic_source ? 4.0f : 0.004f;
    float max_ejecta = energetic_source ? 120.0f : 0.080f;
    float base_ejecta = std::clamp(std::max(ejecta_speed, min_ejecta), min_ejecta, max_ejecta);
    if (!std::isfinite(base_ejecta)) base_ejecta = min_ejecta;
    bool gas_source = (source_body != nullptr) && gas_dominated_body(*source_body, source_materials);
    float normalized_shock = std::clamp(shock_ratio, 0.0f, 2.0f);
    float gas_frag_chance = gas_source
        ? std::clamp(source_materials.hydrogen * (0.28f + normalized_shock * 0.22f), 0.0f, 0.78f)
        : 0.0f;
    float icy_frag_chance = std::clamp(source_materials.water * (0.22f + std::max(0.0f, 0.70f - normalized_shock) * 0.30f),
                                       0.0f, 0.62f);
    float molten_bias = std::clamp((source_temperature - 900.0f) / 1500.0f + normalized_shock * 0.55f +
                                   source_materials.silicate * 0.16f + source_materials.iron * 0.20f,
                                   0.0f, 1.35f);

    // Planetary breakups should produce a few dominant chunks instead of equal tiny shards.
    if (!energetic_source && count <= 6 && remaining_mass > 0.0f) {
        int dom = (int)(u01(rng) * (float)count);
        if (dom >= count) dom = count - 1;
        float target_dom = std::clamp(total_mass * (0.35f + u01(rng) * 0.20f),
                                      total_mass * 0.25f, total_mass * 0.60f);
        float current_dom = masses[(size_t)dom];
        float extra = std::max(target_dom - current_dom, 0.0f);
        if (extra > 0.0f) {
            float donor_total = 0.0f;
            for (int i = 0; i < count; ++i) {
                if (i == dom) continue;
                donor_total += std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
            }
            if (donor_total > 1.0e-12f) {
                for (int i = 0; i < count; ++i) {
                    if (i == dom) continue;
                    float avail = std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
                    float take = extra * (avail / donor_total);
                    masses[(size_t)i] -= take;
                    masses[(size_t)dom] += take;
                }
            }
        }
    }

    std::vector<glm::vec3> rel_vels((size_t)count, glm::vec3(0.0f));
    glm::vec3 momentum_bias(0.0f);
    for (int i = 0; i < count; ++i) {
        float theta = u01(rng) * 6.28318530718f;
        float z = u01(rng) * 2.0f - 1.0f;
        float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        glm::vec3 rand_dir(r * std::cos(theta), z, r * std::sin(theta));
        glm::vec3 dir;
        float speed = 0.0f;
        if (energetic_source) {
            dir = safe_normalize(glm::mix(rand_dir, axis, 0.45f + 0.25f * u01(rng)), axis);
            speed = base_ejecta * (0.55f + u01(rng) * 0.90f);
        } else {
            glm::vec3 planar = rand_dir - axis * glm::dot(rand_dir, axis);
            if (glm::length(planar) < 1.0e-5f) {
                planar = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(planar) < 1.0e-5f)
                    planar = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            if (glm::length(planar) < 1.0e-5f)
                planar = glm::vec3(1.0f, 0.0f, 0.0f);
            dir = safe_normalize(glm::mix(safe_normalize(planar, glm::vec3(1.0f, 0.0f, 0.0f)), axis,
                                          0.12f + 0.08f * u01(rng)), axis);
            speed = base_ejecta * (0.65f + u01(rng) * 0.45f);
        }
        rel_vels[(size_t)i] = dir * speed;
        momentum_bias += rel_vels[(size_t)i] * masses[(size_t)i];
    }
    momentum_bias /= std::max(total_mass, 1.0e-6f);
    for (auto& rv : rel_vels)
        rv -= momentum_bias;

    int attracting_fragments_now = 0;
    if (cfg.dynamic_budget_enabled) {
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (!b.non_attracting) ++attracting_fragments_now;
        }
    }
    int max_attracting_fragments = std::max(cfg.dynamic_max_fragments, 0);

    for (int i = 0; i < count; i++) {
        float frag_mass = masses[(size_t)i];
        float frag_radius = std::max(1.2f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 3.0f);

        CelestialBody frag;
        glm::vec3 dir = safe_normalize(rel_vels[(size_t)i] + axis * 0.1f, axis);
        frag.pos = pos + dir * (frag_radius * 1.5f + 1.0f);
        frag.vel = vel + rel_vels[(size_t)i];
        if (!std::isfinite(frag.vel.x) || !std::isfinite(frag.vel.y) || !std::isfinite(frag.vel.z) ||
            !std::isfinite(frag.pos.x) || !std::isfinite(frag.pos.y) || !std::isfinite(frag.pos.z))
            continue;
        frag.mass = frag_mass;
        frag.radius = frag_radius;
        frag.temperature = std::clamp(source_temperature +
                                      base_ejecta * (5.0f + normalized_shock * 6.5f) *
                                      (0.55f + u01(rng) * 0.95f),
                                      40.0f, 30000.0f);
        float type_roll = u01(rng);
        if (gas_source && type_roll < gas_frag_chance) {
            frag.type = CTYPE_NEBULA;
            frag.radius = std::max(frag_radius * 1.35f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 24.0f);
            frag.temperature = std::max(60.0f, frag.temperature * 0.55f);
            frag.atmosphere_retention = 1.0f;
            frag.material_phase = (normalized_shock > 0.95f) ? PHASE_COLLAPSING : PHASE_GAS;
            frag.phase_intensity = std::clamp(0.40f + source_materials.hydrogen * 0.35f + normalized_shock * 0.18f,
                                              0.25f, 1.0f);
            frag.collapse_progress = std::clamp(source_materials.hydrogen * 0.12f + normalized_shock * 0.08f,
                                                0.0f, 0.28f);
        } else if (type_roll < gas_frag_chance + icy_frag_chance && frag.temperature < 520.0f) {
            frag.type = CTYPE_COMET;
            frag.atmosphere_retention = 0.18f;
            frag.material_phase = (frag.temperature < 170.0f) ? PHASE_ICE : PHASE_LIQUID;
            frag.phase_intensity = std::clamp(0.28f + source_materials.water * 0.60f, 0.20f, 1.0f);
        } else {
            frag.type = CTYPE_ASTEROID;
            frag.atmosphere_retention = 0.02f;
            if (molten_bias > 0.28f || frag.temperature > 950.0f) {
                frag.material_phase = (frag.temperature > 2400.0f) ? PHASE_PLASMA : PHASE_MOLTEN;
                frag.phase_intensity = std::clamp(0.24f + molten_bias * 0.65f, 0.18f, 1.0f);
            } else if (source_materials.water > 0.28f && frag.temperature < 170.0f) {
                frag.material_phase = PHASE_ICE;
                frag.phase_intensity = std::clamp(0.22f + source_materials.water * 0.55f, 0.18f, 1.0f);
            } else {
                frag.material_phase = PHASE_SOLID;
                frag.phase_intensity = std::clamp(0.18f + source_materials.silicate * 0.35f +
                                                  source_materials.iron * 0.20f, 0.12f, 0.85f);
            }
        }
        frag.fuel = 0.0f;
        frag.internal_energy = normalized_shock * frag_mass * (12.0f + base_ejecta * 0.9f);
        frag.seed = rng();
        frag.frag_generation = parent_generation + 1;
        frag.angular_vel = (u01(rng) * 2.0f - 1.0f) * 0.01f;
        if (cfg.dynamic_budget_enabled) {
            if (attracting_fragments_now >= max_attracting_fragments) {
                frag.non_attracting = true;
            } else {
                frag.non_attracting = false;
                ++attracting_fragments_now;
            }
        } else {
            frag.non_attracting = false;
        }
        frag.name = generate_body_name(frag.seed, frag.type);
        clear_ring_system(frag);
        clear_impact_signature(frag);
        refresh_body_render_state(frag, &state);

        state.bodies.push_back(frag);
        state.trails.emplace_back();
    }
}
