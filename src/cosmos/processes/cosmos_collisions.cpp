#include "cosmos/cosmos_app_internal.h"
#include "cosmos/core/cosmos_parallel.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <thread>

// ── Collision Processing ────────────────────────────────────────────────────

void CosmosApp::trigger_stellar_supernova(size_t index, float dt, bool thermonuclear,
                                          glm::vec3 impact_axis, float ejecta_speed) {
    if (index >= state.bodies.size()) return;
    CelestialBody& b = state.bodies[index];
    if (b.marked_for_removal) return;

    float progenitor_mass = std::max(b.mass, 0.01f);
    StellarRemnantKind remnant = stellar_remnant_kind(progenitor_mass, thermonuclear);
    float remnant_mass = 0.0f;

    switch (remnant) {
    case REMNANT_WHITE_DWARF:
        remnant_mass = std::clamp(0.48f + progenitor_mass * 0.10f, 0.45f, 1.30f);
        break;
    case REMNANT_NEUTRON_STAR:
        remnant_mass = std::clamp(1.25f + (progenitor_mass - CORE_COLLAPSE_MIN_MASS_SOLAR) * 0.045f,
                                  1.25f, 2.40f);
        break;
    case REMNANT_BLACK_HOLE:
        remnant_mass = std::clamp(progenitor_mass * 0.35f, 3.0f, progenitor_mass * 0.85f);
        break;
    case REMNANT_NONE:
    default:
        remnant_mass = 0.0f;
        break;
    }

    float ejecta_mass = std::max(progenitor_mass - remnant_mass, 0.0f);
    int burst_count = std::clamp(cfg.fragment_count * 2, cfg.fragment_count, 24);
    glm::vec3 axis = glm::length(impact_axis) > 1.0e-4f
        ? glm::normalize(impact_axis)
        : glm::normalize(glm::vec3(0.7f, 0.3f, 0.2f));
    float burst_speed = ejecta_speed > 0.0f
        ? ejecta_speed
        : (thermonuclear ? std::max(22.0f, progenitor_mass * 2.0f)
                         : std::max(28.0f, progenitor_mass * 1.4f));

    if (ejecta_mass > 1.0e-4f) {
        spawn_fragments(b.pos, b.vel, ejecta_mass, burst_count,
                        b.frag_generation, std::max(b.temperature, 6000.0f),
                        axis, burst_speed, &b, thermonuclear ? 1.6f : 1.2f);
        register_mass_loss(b, ejecta_mass, std::max(dt, 1.0e-4f));
    }

    if (thermonuclear || remnant == REMNANT_NONE) {
        b.marked_for_removal = true;
        return;
    }

    b.mass = remnant_mass;
    b.fuel = 0.0f;
    b.internal_energy *= 0.1f;
    clear_ring_system(b);
    clear_impact_signature(b);

    if (remnant == REMNANT_WHITE_DWARF) {
        b.stellar_stage = SSTAGE_WHITE_DWARF;
        b.temperature = std::clamp(22000.0f + progenitor_mass * 1800.0f, 9000.0f, 120000.0f);
        b.radius = expected_star_radius(b);
        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
        b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else if (remnant == REMNANT_NEUTRON_STAR) {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 120000.0f;
        b.radius = expected_star_radius(b);
        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
        b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        float spin_sign = (b.angular_vel < 0.0f) ? -1.0f : 1.0f;
        b.angular_vel = spin_sign * std::clamp(std::abs(b.angular_vel) * 4.0f + 0.002f, 0.002f, 0.05f);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 0.0f;
        b.luminosity = 0.0f;
        b.type = classify_black_hole(b.mass);
        b.radius = std::max(0.5f, 2.0f * b.mass);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    }

    b.vel += axis * (burst_speed * 0.04f / std::max(b.mass, 0.1f));
    refresh_body_render_state(b, &state);
}

bool CosmosApp::handle_stellar_collision_supernova(size_t i, size_t j, float rel_speed,
                                                   float impact_energy, float escape_speed,
                                                   const glm::vec3& impact_axis, float dt) {
    if (!is_star_type(state.bodies[i].type) || !is_star_type(state.bodies[j].type))
        return false;

    CelestialBody& a = state.bodies[i];
    CelestialBody& b = state.bodies[j];
    float total_mass = a.mass + b.mass;
    bool white_dwarf_pair = (a.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_WHITE_DWARF);
    bool thermonuclear = white_dwarf_pair &&
        total_mass >= CHANDRASEKHAR_LIMIT_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 0.7f, escape_speed * 1.1f);
    bool core_collapse = total_mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 1.15f, escape_speed * 1.6f) &&
        impact_energy > total_mass * 25.0f;

    if (!thermonuclear && !core_collapse)
        return false;

    size_t big = (a.mass >= b.mass) ? i : j;
    size_t small = (big == i) ? j : i;
    CelestialBody merged = state.bodies[big];

    merged.pos = (a.pos * a.mass + b.pos * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.vel = (a.vel * a.mass + b.vel * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.mass = total_mass;
    merged.radius = std::cbrt(std::max(a.radius * a.radius * a.radius +
                                       b.radius * b.radius * b.radius, 1.0f));
    merged.temperature = std::max((a.temperature * a.mass + b.temperature * b.mass) /
                                  std::max(total_mass, 1.0e-6f),
                                  thermonuclear ? 140000.0f : 90000.0f);
    merged.fuel = thermonuclear ? 0.0f : merged_star_fuel(a, b, total_mass) * 0.7f;
    merged.internal_energy = a.internal_energy + b.internal_energy + impact_energy * 0.15f;
    merged.angular_vel = (a.angular_vel * a.mass + b.angular_vel * b.mass) /
                         std::max(total_mass, 1.0e-6f);
    if (!std::isfinite(merged.angular_vel)) merged.angular_vel = 0.0f;
    merged.type = classify_star_spectral(std::max(merged.temperature, 250.0f), std::max(merged.mass, 0.003f));
    merged.stellar_stage = thermonuclear ? SSTAGE_WHITE_DWARF :
        ((a.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_RED_GIANT)
            ? SSTAGE_RED_GIANT : SSTAGE_MAIN_SEQUENCE);

    state.bodies[big] = merged;
    state.bodies[small].marked_for_removal = true;
    trigger_stellar_supernova(big, dt, thermonuclear, impact_axis, std::max(rel_speed * 0.6f, 24.0f));
    return true;
}

void CosmosApp::process_collisions(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    // Pre-reserve capacity so fragment spawning during collision processing
    // doesn't reallocate the vector and invalidate pointers/references.
    bodies.reserve(n + std::max<size_t>(64, n / 2));

    // Build candidate pair list -- spatial hash grid for n >= 64, brute force otherwise.
    std::vector<std::pair<size_t, size_t>> pairs;

    if (n < 64 || !cfg.spatial_hash_collisions) {
        // Brute-force: O(n^2) is cheaper than grid overhead for tiny counts.
        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (bodies[j].marked_for_removal) continue;
                pairs.emplace_back(i, j);
            }
        }
    } else {
        // Find max radius to set cell size.
        float max_radius = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (!bodies[i].marked_for_removal)
                max_radius = std::max(max_radius, bodies[i].radius);
        }
        float cell_size = std::max(2.0f * max_radius, 1.0e-6f);
        float inv_cell = 1.0f / cell_size;

        // Spatial hash: cell coord -> list of body indices.
        auto cell_hash = [](int cx, int cy, int cz) -> int64_t {
            // Combine three 21-bit signed cell coords into a single int64.
            return (int64_t(cx) * int64_t(73856093)) ^
                   (int64_t(cy) * int64_t(19349663)) ^
                   (int64_t(cz) * int64_t(83492791));
        };

        std::unordered_map<int64_t, std::vector<size_t>> grid;
        grid.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            int cx = (int)std::floor(bodies[i].pos.x * inv_cell);
            int cy = (int)std::floor(bodies[i].pos.y * inv_cell);
            int cz = (int)std::floor(bodies[i].pos.z * inv_cell);
            grid[cell_hash(cx, cy, cz)].push_back(i);
        }

        // Collect unique candidate pairs from 27-cell neighborhoods.
        // To avoid duplicates: for each cell, check the 13 "forward" neighbors
        // plus the cell itself (self-pairs within the cell).
        const int offsets[14][3] = {
            { 0, 0, 0},  // self
            { 1, 0, 0},  { 0, 1, 0},  { 0, 0, 1},
            { 1, 1, 0},  { 1,-1, 0},  { 1, 0, 1},  { 1, 0,-1},
            { 0, 1, 1},  { 0, 1,-1},  { 1, 1, 1},  { 1, 1,-1},
            { 1,-1, 1},  { 1,-1,-1}
        };

        for (auto& [key, cell] : grid) {
            if (cell.empty()) continue;
            // Recover cell coords from first body in the cell.
            size_t rep = cell[0];
            int cx = (int)std::floor(bodies[rep].pos.x * inv_cell);
            int cy = (int)std::floor(bodies[rep].pos.y * inv_cell);
            int cz = (int)std::floor(bodies[rep].pos.z * inv_cell);

            // Self-pairs within this cell.
            for (size_t a = 0; a < cell.size(); ++a) {
                for (size_t b = a + 1; b < cell.size(); ++b) {
                    size_t ii = cell[a], jj = cell[b];
                    if (ii > jj) std::swap(ii, jj);
                    pairs.emplace_back(ii, jj);
                }
            }

            // Cross-pairs with 13 forward-neighbor cells.
            for (int d = 1; d < 14; ++d) {
                int64_t nkey = cell_hash(cx + offsets[d][0],
                                         cy + offsets[d][1],
                                         cz + offsets[d][2]);
                auto it = grid.find(nkey);
                if (it == grid.end()) continue;
                const auto& ncell = it->second;
                for (size_t ai : cell) {
                    for (size_t bi : ncell) {
                        size_t ii = ai, jj = bi;
                        if (ii == jj) continue;
                        if (ii > jj) std::swap(ii, jj);
                        pairs.emplace_back(ii, jj);
                    }
                }
            }
        }

        // Deduplicate pairs (hash collisions can produce the same cell key for
        // different actual grid cells, yielding duplicate pairs).
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    }

    // Process all candidate pairs.
    for (auto& [i, j] : pairs) {
        if (bodies[i].marked_for_removal || bodies[j].marked_for_removal) continue;
        // Locked bodies don't participate in collisions
        if (bodies[i].locked || bodies[j].locked) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;

            glm::vec3 prev_diff = diff - rel_vel * dt;
            glm::vec3 seg = diff - prev_diff;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_diff, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_diff + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);

            bool overlap_now = (dist < touch);
            bool swept_hit = (sweep_dist < touch);
            if (!overlap_now && !swept_hit) continue;

            bool nebula_pair = (bodies[i].type == CTYPE_NEBULA || bodies[j].type == CTYPE_NEBULA);
            if (nebula_pair) {
                float contact_dist = overlap_now ? dist : sweep_dist;
                float overlap = std::max(touch - contact_dist, 0.0f);
                float overlap_fraction = overlap / std::max(touch, 1.0e-6f);

                int nebula_i = (bodies[i].type == CTYPE_NEBULA) ? 1 : 0;
                int nebula_j = (bodies[j].type == CTYPE_NEBULA) ? 1 : 0;

                // Nebulae are diffuse volumes: no rigid-body bounce/depenetration.
                // We only apply soft drag so bodies are entrained by cloud flow.
                if (nebula_i && !nebula_j) {
                    float drag = std::clamp((0.12f + overlap_fraction * 0.70f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.94f);
                    bodies[j].vel = glm::mix(bodies[j].vel, bodies[i].vel, drag);
                } else if (nebula_j && !nebula_i) {
                    float drag = std::clamp((0.12f + overlap_fraction * 0.70f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.94f);
                    bodies[i].vel = glm::mix(bodies[i].vel, bodies[j].vel, drag);
                } else {
                    float mutual = std::clamp((0.06f + overlap_fraction * 0.35f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.65f);
                    glm::vec3 vcm = (bodies[i].vel * bodies[i].mass + bodies[j].vel * bodies[j].mass) /
                                    std::max(bodies[i].mass + bodies[j].mass, 1.0e-6f);
                    bodies[i].vel = glm::mix(bodies[i].vel, vcm, mutual);
                    bodies[j].vel = glm::mix(bodies[j].vel, vcm, mutual);
                }

                if (cfg.temperature_system) {
                    float thermal_kick = std::min(glm::length(rel_vel) * overlap_fraction * 18.0f, 1200.0f);
                    if (nebula_i) {
                        bodies[i].temperature += thermal_kick * 0.10f;
                        bodies[i].internal_energy += thermal_kick * 0.002f;
                    }
                    if (nebula_j) {
                        bodies[j].temperature += thermal_kick * 0.10f;
                        bodies[j].internal_energy += thermal_kick * 0.002f;
                    }
                    if (nebula_i && !nebula_j) {
                        bodies[j].temperature += thermal_kick * 0.25f;
                        bodies[j].internal_energy += thermal_kick * 0.004f;
                    } else if (nebula_j && !nebula_i) {
                        bodies[i].temperature += thermal_kick * 0.25f;
                        bodies[i].internal_energy += thermal_kick * 0.004f;
                    }
                }
                continue;
            }

            float contact_dist = overlap_now ? dist : sweep_dist;
            glm::vec3 normal = overlap_now ? (diff / std::max(dist, 1.0e-6f))
                                           : (closest_rel / std::max(sweep_dist, 1.0e-6f));
            if (glm::length(normal) < 1.0e-5f) {
                if (glm::length(rel_vel) > 1.0e-5f) {
                    normal = glm::normalize(rel_vel);
                } else {
                    // Deterministic fallback axis for near-identical overlap states.
                    uint32_t h = (uint32_t)(i * 73856093u) ^ (uint32_t)(j * 19349663u);
                    float x = ((h & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float y = (((h >> 10) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float z = (((h >> 20) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    normal = glm::normalize(glm::vec3(x, y, z));
                    if (glm::length(normal) < 1.0e-5f)
                        normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }

            float overlap = std::max(touch - contact_dist, 0.0f);
            float overlap_fraction = overlap / std::max(touch, 1.0e-6f);
            float total_mass = bodies[i].mass + bodies[j].mass;
            if (total_mass <= 1.0e-8f) continue;
            bool soft_body_pair =
                !is_star_type(bodies[i].type) && !is_star_type(bodies[j].type) &&
                !is_black_hole_type(bodies[i].type) && !is_black_hole_type(bodies[j].type) &&
                bodies[i].type != CTYPE_NEBULA && bodies[j].type != CTYPE_NEBULA;
            bool use_rigid_response = cfg.collision_rigid_body_dynamics || !soft_body_pair;

            // Immediate depenetration to avoid sticky overlap accumulation.
            if (use_rigid_response && overlap_now && overlap > 0.0f) {
                float separate_scale = std::clamp(cfg.rigid_collision_separation, 0.0f, 3.0f);
                bodies[i].pos -= normal * overlap * separate_scale * (bodies[j].mass / std::max(total_mass, 1.0e-6f));
                bodies[j].pos += normal * overlap * separate_scale * (bodies[i].mass / std::max(total_mass, 1.0e-6f));
            }
            float rel_speed = glm::length(rel_vel);
            float vel_along = glm::dot(rel_vel, normal);
            float closing_speed = std::max(-vel_along, 0.0f);
            float escape_speed = body_escape_speed(bodies[i], bodies[j], cfg.G);

            bool small_soft_pair = soft_body_pair &&
                std::max(bodies[i].radius, bodies[j].radius) < (EARTH_RADIUS_SIM_UNITS * 0.95f);
            bool compact_i = is_star_type(bodies[i].type) || is_black_hole_type(bodies[i].type);
            bool compact_j = is_star_type(bodies[j].type) || is_black_hole_type(bodies[j].type);
            bool compact_vs_soft = (compact_i != compact_j);

            // For soft bodies, use actual measured speed; for hard bodies, use at
            // least a fraction of escape velocity as an infall floor.
            float infall_speed = soft_body_pair ? closing_speed
                                                : std::max(closing_speed, escape_speed * 0.15f);
            float compression_speed = overlap_fraction * escape_speed * (soft_body_pair ? 1.4f : 1.6f);
            float impact_speed = std::max(rel_speed, std::max(infall_speed, compression_speed));
            float reduced_mass = (bodies[i].mass * bodies[j].mass) / std::max(total_mass, 1.0e-6f);
            float impact_energy = 0.5f * reduced_mass * impact_speed * impact_speed;

            float binding_i = body_gravitational_binding_energy(bodies[i], cfg.G);
            float binding_j = body_gravitational_binding_energy(bodies[j], cfg.G);
            float combined_binding = binding_i + binding_j;
            float disruption_i = impact_energy / std::max(binding_i, 1.0e-6f);
            float disruption_j = impact_energy / std::max(binding_j, 1.0e-6f);
            float combined_disruption = impact_energy / std::max(combined_binding, 1.0e-6f);
            glm::vec3 impact_axis = (rel_speed > 1.0e-5f) ? (rel_vel / rel_speed) : normal;
            float spin_ratio_i = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[i], cfg.G) : 0.0f;
            float spin_ratio_j = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[j], cfg.G) : 0.0f;
            float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
            float spin_over_i = std::max(spin_ratio_i - spin_threshold, 0.0f);
            float spin_over_j = std::max(spin_ratio_j - spin_threshold, 0.0f);
            if (cfg.spin_fragmentation) {
                float spin_disruption_i = spin_over_i * (soft_body_pair ? 0.34f : 0.12f);
                float spin_disruption_j = spin_over_j * (soft_body_pair ? 0.34f : 0.12f);
                disruption_i += spin_disruption_i;
                disruption_j += spin_disruption_j;
                combined_disruption += (spin_disruption_i + spin_disruption_j) *
                    (soft_body_pair ? 0.55f : 0.28f);
            }

            if (soft_body_pair && cfg.collision_sph) {
                float h = touch * 1.25f;
                float q = std::clamp(1.0f - contact_dist / std::max(h, 1.0e-6f), 0.0f, 1.0f);
                if (q > 0.0f) {
                    float inv_total_mass = 1.0f / std::max(total_mass, 1.0e-6f);
                    float pressure_term = q * q * (0.08f + overlap_fraction * 0.35f) *
                        std::clamp(cfg.collision_sph_pressure, 0.0f, 4.0f);
                    glm::vec3 pressure_push = normal * pressure_term;
                    bodies[i].vel -= pressure_push * (bodies[j].mass * inv_total_mass);
                    bodies[j].vel += pressure_push * (bodies[i].mass * inv_total_mass);

                    glm::vec3 post_rel = bodies[j].vel - bodies[i].vel;
                    float viscosity_term = (0.10f + 0.65f * q) * std::max(dt, 1.0e-4f) *
                        std::clamp(cfg.collision_sph_viscosity, 0.0f, 4.0f);
                    glm::vec3 viscosity = post_rel * viscosity_term;
                    bodies[i].vel += viscosity * (bodies[j].mass * inv_total_mass);
                    bodies[j].vel -= viscosity * (bodies[i].mass * inv_total_mass);

                    if (cfg.temperature_system) {
                        float sph_heat = impact_energy * q * (0.05f + overlap_fraction * 0.10f) *
                            std::clamp(cfg.collision_sph_heat, 0.0f, 4.0f);
                        bodies[i].internal_energy += sph_heat * 0.5f;
                        bodies[j].internal_energy += sph_heat * 0.5f;
                    }
                }
            }

            // Collision heating (kinetic -> thermal/internal).
            if (cfg.temperature_system) {
                float heat = impact_energy * cfg.collision_heating * (soft_body_pair ? 1.35f : 0.85f);
                bodies[i].internal_energy += heat * 0.5f;
                bodies[j].internal_energy += heat * 0.5f;
                bodies[i].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[i].mass, 1.0e-6f), 18000.0f);
                bodies[j].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[j].mass, 1.0e-6f), 18000.0f);
            }

            if (handle_stellar_collision_supernova(i, j, rel_speed, impact_energy,
                                                   escape_speed, impact_axis, dt)) {
                continue;
            }

            float merge_speed_limit = 0.0f;
            float fragment_speed_limit = 0.0f;
            if (soft_body_pair) {
                if (small_soft_pair) {
                    merge_speed_limit = std::max(escape_speed * 0.40f, 1.0e-4f);
                    fragment_speed_limit = std::max(escape_speed * 0.70f, 2.5e-4f);
                } else {
                    // Allow merging up to ~escape velocity — planets that gravitationally
                    // fall into each other should merge, not bounce like billiard balls.
                    merge_speed_limit = std::max(std::max(escape_speed * 0.95f, cfg.merge_speed_threshold * 0.0005f), 0.0015f);
                    fragment_speed_limit = std::max(std::max(escape_speed * 2.20f, cfg.fragment_speed_threshold * 0.0010f),
                                                    merge_speed_limit * 2.0f);
                }
            } else {
                merge_speed_limit = std::max(cfg.merge_speed_threshold, escape_speed * 0.95f);
                fragment_speed_limit = std::max(cfg.fragment_speed_threshold, escape_speed * 1.15f);
            }

            bool catastrophic_fragment =
                (impact_speed >= (soft_body_pair ? fragment_speed_limit * 1.15f : fragment_speed_limit) ||
                 combined_disruption > (soft_body_pair ? 0.35f : 0.72f) ||
                 disruption_i > (soft_body_pair ? 0.38f : 0.92f) ||
                 disruption_j > (soft_body_pair ? 0.38f : 0.92f) ||
                 (compact_vs_soft && (swept_hit || overlap_fraction > 0.005f)));

            bool optional_fragment = cfg.collision_fragmentation &&
                (impact_speed >= fragment_speed_limit * (soft_body_pair ? 0.90f : 0.90f) ||
                 combined_disruption > (soft_body_pair ? 0.22f : 0.60f) ||
                 disruption_i > (soft_body_pair ? 0.26f : 0.75f) ||
                 disruption_j > (soft_body_pair ? 0.26f : 0.75f));
            bool spin_fragment_contact = cfg.spin_fragmentation &&
                (spin_over_i > 0.0f || spin_over_j > 0.0f) &&
                (overlap_now || swept_hit || overlap_fraction > (soft_body_pair ? 0.01f : 0.002f)) &&
                impact_speed > merge_speed_limit * (soft_body_pair ? 0.45f : 0.70f);

            bool fragment_candidate = catastrophic_fragment || optional_fragment || spin_fragment_contact ||
                (soft_body_pair && swept_hit && impact_speed > fragment_speed_limit * 0.75f);
            bool ultra_gentle_soft = soft_body_pair &&
                impact_speed < merge_speed_limit * 1.05f &&
                combined_disruption < 0.20f &&
                overlap_fraction < 0.30f;
            if (small_soft_pair) {
                // Relaxed from 0.04/0.06 — previous values made virtually ALL
                // small-body collisions classify as non-gentle → always fragment.
                ultra_gentle_soft = impact_speed < merge_speed_limit * 0.90f &&
                    combined_disruption < 0.12f &&
                    overlap_fraction < 0.15f;
            }
            if (ultra_gentle_soft)
                fragment_candidate = false;
            // Small soft body forced fragmentation only for genuinely energetic impacts,
            // not every single overlap.
            if (small_soft_pair && cfg.collision_fragmentation &&
                (overlap_now || swept_hit) && !ultra_gentle_soft &&
                impact_speed > fragment_speed_limit * 0.60f)
                fragment_candidate = true;

            bool sticky_soft_merge = soft_body_pair && !cfg.collision_fragmentation &&
                overlap_now && !fragment_candidate &&
                impact_speed < fragment_speed_limit * 0.95f &&
                combined_disruption < 0.20f;

            // CCD-detected collisions can also merge if the approach is gentle
            // (prevents fast-approach bodies from getting stuck in dead zone).
            bool swept_gentle = swept_hit && !overlap_now &&
                rel_speed <= merge_speed_limit &&
                closing_speed <= merge_speed_limit * 1.10f &&
                combined_disruption < (soft_body_pair ? 0.25f : 0.40f);
            bool merge_candidate = cfg.collision_merging &&
                ((!swept_hit || swept_gentle) &&
                ((overlap_now &&
                  rel_speed <= merge_speed_limit &&
                  closing_speed <= merge_speed_limit &&
                  impact_speed <= fragment_speed_limit * (soft_body_pair ? 0.85f : 0.90f) &&
                  overlap_fraction >= (soft_body_pair ? 0.005f : 0.006f) &&
                  combined_disruption < (soft_body_pair ? 0.20f : 0.55f)) ||
                 swept_gentle ||
                 sticky_soft_merge)) &&
                !fragment_candidate;
            if (soft_body_pair && cfg.collision_fragmentation)
                merge_candidate = merge_candidate && (ultra_gentle_soft || swept_gentle);

            if (merge_candidate) {
                size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small = (big == i) ? j : i;
                CelestialBody pre_big = bodies[big];
                CelestialBody pre_small = bodies[small];

                glm::vec3 com_pos = (pre_big.pos * pre_big.mass + pre_small.pos * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                glm::vec3 com_vel = (pre_big.vel * pre_big.mass + pre_small.vel * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                float merged_temp = (pre_big.temperature * pre_big.mass + pre_small.temperature * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);

                bodies[big].pos = com_pos;
                bodies[big].vel = com_vel;
                bodies[big].mass = total_mass;
                bodies[big].radius = std::cbrt(std::max(pre_big.radius * pre_big.radius * pre_big.radius +
                                                        pre_small.radius * pre_small.radius * pre_small.radius,
                                                        1.0e-6f));
                bodies[big].temperature = merged_temp +
                    std::min(impact_energy * (soft_body_pair ? 28.0f : 10.0f) / std::max(total_mass, 1.0e-6f), 4500.0f);
                bodies[big].internal_energy = pre_big.internal_energy + pre_small.internal_energy + impact_energy * 0.06f;
                // Conserve angular momentum: L = I*ω, I ∝ M*R² for solid sphere
                float I_big = pre_big.mass * pre_big.radius * pre_big.radius;
                float I_small = pre_small.mass * pre_small.radius * pre_small.radius;
                // Orbital angular momentum contribution from merger
                glm::vec3 r_rel = pre_small.pos - com_pos;
                glm::vec3 v_rel = pre_small.vel - com_vel;
                float L_orbital = (r_rel.x * v_rel.y - r_rel.y * v_rel.x) * pre_small.mass; // z-component
                float I_merged = total_mass * bodies[big].radius * bodies[big].radius;
                bodies[big].angular_vel = (I_big * pre_big.angular_vel + I_small * pre_small.angular_vel + L_orbital) /
                                          std::max(I_merged, 1.0e-6f);
                if (!std::isfinite(bodies[big].angular_vel)) bodies[big].angular_vel = 0.0f;

                if (is_star_type(pre_big.type) && is_star_type(pre_small.type)) {
                    bodies[big].fuel = merged_star_fuel(pre_big, pre_small, total_mass);
                    bodies[big].luminosity = expected_stellar_luminosity(
                        bodies[big].mass, bodies[big].temperature, bodies[big].radius,
                        bodies[big].stellar_stage, bodies[big].fuel);
                    bodies[big].type = classify_star_spectral(std::max(bodies[big].temperature, 250.0f),
                                                              std::max(bodies[big].mass, 0.003f));
                    if (bodies[big].mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
                        (pre_big.stellar_stage == SSTAGE_RED_GIANT ||
                         pre_small.stellar_stage == SSTAGE_RED_GIANT ||
                         bodies[big].fuel < 0.18f)) {
                        trigger_stellar_supernova(big, dt, false, impact_axis, std::max(rel_speed * 0.45f, 18.0f));
                    }
                    if ((pre_big.stellar_stage == SSTAGE_WHITE_DWARF || pre_small.stellar_stage == SSTAGE_WHITE_DWARF) &&
                        bodies[big].mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
                        trigger_stellar_supernova(big, dt, true, impact_axis, std::max(rel_speed * 0.50f, 22.0f));
                    }
                } else {
                    bodies[big].fuel = std::max(pre_big.fuel, pre_small.fuel);
                }

                // Gentle post-merge ejecta only for high-disruption mergers.
                if (!is_star_type(pre_big.type) && !is_black_hole_type(pre_big.type) &&
                    !is_star_type(pre_small.type) && !is_black_hole_type(pre_small.type) &&
                    cfg.collision_fragmentation) {
                    float ejecta_fraction = std::clamp((combined_disruption - (soft_body_pair ? 0.10f : 0.22f)) *
                                                       (soft_body_pair ? 0.35f : 0.20f),
                                                       0.0f, soft_body_pair ? 0.18f : 0.10f);
                    float ejecta_mass = total_mass * ejecta_fraction;
                    if (ejecta_mass > 1.0e-8f) {
                        int ejecta_count = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 8);
                        float merger_ejecta_speed = soft_body_pair
                            ? std::clamp(escape_speed * 0.55f + closing_speed * 0.20f, 0.004f, 0.045f)
                            : std::max(impact_speed * 0.35f, 2.0f);
                        spawn_fragments((pre_big.pos + pre_small.pos) * 0.5f, com_vel, ejecta_mass,
                                        ejecta_count, std::max(pre_big.frag_generation, pre_small.frag_generation),
                                        std::max(pre_big.temperature, pre_small.temperature),
                                        (big == i) ? normal : -normal,
                                        merger_ejecta_speed,
                                        &pre_small, combined_disruption);
                        register_mass_loss(bodies[big], ejecta_mass, std::max(dt, 1.0e-4f));
                        float remaining_mass = std::max(total_mass - ejecta_mass, 1.0e-8f);
                        float mass_scale = std::cbrt(remaining_mass / std::max(total_mass, 1.0e-8f));
                        bodies[big].mass = remaining_mass;
                        bodies[big].radius = std::max(bodies[big].radius * mass_scale, 0.1f);
                    }

                    apply_impact_signature(
                        bodies[big], (big == i) ? normal : -normal,
                        std::clamp(combined_disruption, 0.0f, 1.2f),
                        pre_small.mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(total_mass, 1.0e-6f), 0.0f, 1.5f),
                        std::clamp(pre_small.radius / std::max(bodies[big].radius, 0.1f), 0.08f, 0.95f));
                }

                bodies[big].props_valid = false;
                bodies[big].visuals_valid = false;
                bodies[small].marked_for_removal = true;
                continue;
            }

            bool closing_contact = vel_along < 0.0f || (soft_body_pair && dist < touch * 0.99f) || swept_hit;
            if (!closing_contact) continue;

            bool fragmenting = fragment_candidate;
            if (use_rigid_response) {
                // Soft bodies get reduced restitution (not zero) so they bounce
                // instead of getting stuck in a jittering dead zone.
                float restitution = fragmenting ? 0.0f
                    : std::clamp(cfg.rigid_collision_restitution * (soft_body_pair ? 0.45f : 1.0f), 0.0f, 1.2f);
                float inv_mass_sum = (1.0f / std::max(bodies[i].mass, 1.0e-6f)) +
                                     (1.0f / std::max(bodies[j].mass, 1.0e-6f));
                float impulse_speed = (vel_along < -1.0e-5f) ? vel_along
                    : (soft_body_pair ? -closing_speed * 0.25f : -impact_speed * 0.35f);
                float j_impulse = -(1.0f + restitution) * impulse_speed / std::max(inv_mass_sum, 1.0e-6f);
                glm::vec3 impulse = normal * j_impulse;
                bodies[i].vel -= impulse / std::max(bodies[i].mass, 1.0e-6f);
                bodies[j].vel += impulse / std::max(bodies[j].mass, 1.0e-6f);

                // Tangential damping keeps non-fragment impacts from behaving like pinballs.
                glm::vec3 post_rel = bodies[j].vel - bodies[i].vel;
                glm::vec3 tangential = post_rel - normal * glm::dot(post_rel, normal);
                float tangential_len = glm::length(tangential);
                if (tangential_len > 1.0e-6f) {
                    glm::vec3 tangent_dir = tangential / tangential_len;
                    float tangential_reduce = tangential_len * (fragmenting ? 0.80f : (soft_body_pair ? 0.90f : 0.35f));
                    float jt = tangential_reduce / std::max(inv_mass_sum, 1.0e-6f);
                    glm::vec3 tangent_impulse = tangent_dir * jt;
                    bodies[i].vel += tangent_impulse / std::max(bodies[i].mass, 1.0e-6f);
                    bodies[j].vel -= tangent_impulse / std::max(bodies[j].mass, 1.0e-6f);
                }
            }

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            auto can_fragment = [&](const CelestialBody& body) {
                return body.mass >= effective_min_frag_mass &&
                       (int)body.frag_generation < cfg.max_frag_generation &&
                       !is_star_type(body.type) &&
                       !is_black_hole_type(body.type);
            };

            if (fragmenting) {
                bool fragment_i = can_fragment(bodies[i]) &&
                    (disruption_i > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[i].mass <= bodies[j].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));
                bool fragment_j = can_fragment(bodies[j]) &&
                    (disruption_j > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[j].mass <= bodies[i].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));
                if (cfg.spin_fragmentation && spin_over_i > 0.0f && can_fragment(bodies[i]))
                    fragment_i = true;
                if (cfg.spin_fragmentation && spin_over_j > 0.0f && can_fragment(bodies[j]))
                    fragment_j = true;

                if (combined_disruption > (soft_body_pair ? 0.30f : 0.90f) &&
                    can_fragment(bodies[i]) && can_fragment(bodies[j])) {
                    fragment_i = true;
                    fragment_j = true;
                }

                // Similar-mass bodies: if fragment_candidate triggered but neither
                // body would actually be disrupted, promote to MERGE instead of the
                // useless bounce-with-token-fragment outcome.
                if (!fragment_i && !fragment_j && soft_body_pair && cfg.collision_merging) {
                    bool low_disruption = combined_disruption < 0.30f &&
                        disruption_i < 0.35f && disruption_j < 0.35f;
                    if (low_disruption && !catastrophic_fragment) {
                        // Demote: merge the bodies instead of bouncing with a token fragment
                        size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                        size_t sml = (big == i) ? j : i;
                        CelestialBody pre_big = bodies[big];
                        CelestialBody pre_small = bodies[sml];
                        glm::vec3 com_pos = (pre_big.pos * pre_big.mass + pre_small.pos * pre_small.mass) /
                                            std::max(total_mass, 1.0e-6f);
                        glm::vec3 com_vel = (pre_big.vel * pre_big.mass + pre_small.vel * pre_small.mass) /
                                            std::max(total_mass, 1.0e-6f);
                        float merged_temp = (pre_big.temperature * pre_big.mass + pre_small.temperature * pre_small.mass) /
                                            std::max(total_mass, 1.0e-6f);
                        bodies[big].pos = com_pos;
                        bodies[big].vel = com_vel;
                        bodies[big].mass = total_mass;
                        bodies[big].radius = std::cbrt(std::max(pre_big.radius * pre_big.radius * pre_big.radius +
                                                                pre_small.radius * pre_small.radius * pre_small.radius,
                                                                1.0e-6f));
                        bodies[big].temperature = merged_temp +
                            std::min(impact_energy * 28.0f / std::max(total_mass, 1.0e-6f), 4500.0f);
                        bodies[big].internal_energy = pre_big.internal_energy + pre_small.internal_energy + impact_energy * 0.06f;
                        float I_big = pre_big.mass * pre_big.radius * pre_big.radius;
                        float I_sml = pre_small.mass * pre_small.radius * pre_small.radius;
                        glm::vec3 r_rel = pre_small.pos - com_pos;
                        glm::vec3 v_rel = pre_small.vel - com_vel;
                        float L_orbital = (r_rel.x * v_rel.y - r_rel.y * v_rel.x) * pre_small.mass;
                        float I_merged = total_mass * bodies[big].radius * bodies[big].radius;
                        bodies[big].angular_vel = (I_big * pre_big.angular_vel + I_sml * pre_small.angular_vel + L_orbital) /
                                                  std::max(I_merged, 1.0e-6f);
                        if (!std::isfinite(bodies[big].angular_vel)) bodies[big].angular_vel = 0.0f;
                        bodies[big].fuel = std::max(pre_big.fuel, pre_small.fuel);
                        // Post-merge ejecta
                        if (cfg.collision_fragmentation) {
                            float ejecta_frac = std::clamp((combined_disruption - 0.08f) * 0.30f, 0.0f, 0.15f);
                            float ejecta_mass = total_mass * ejecta_frac;
                            if (ejecta_mass > 1.0e-8f) {
                                int ejecta_count = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 6);
                                float merger_ejecta_speed = std::clamp(
                                    escape_speed * 0.55f + closing_speed * 0.20f, 0.004f, 0.045f);
                                spawn_fragments((pre_big.pos + pre_small.pos) * 0.5f, com_vel, ejecta_mass,
                                                ejecta_count, std::max(pre_big.frag_generation, pre_small.frag_generation),
                                                std::max(pre_big.temperature, pre_small.temperature),
                                                (big == i) ? normal : -normal,
                                                merger_ejecta_speed,
                                                &pre_small, combined_disruption);
                                register_mass_loss(bodies[big], ejecta_mass, std::max(dt, 1.0e-4f));
                                float remaining_mass = std::max(total_mass - ejecta_mass, 1.0e-8f);
                                float ms = std::cbrt(remaining_mass / std::max(total_mass, 1.0e-8f));
                                bodies[big].mass = remaining_mass;
                                bodies[big].radius = std::max(bodies[big].radius * ms, 0.1f);
                            }
                        }
                        apply_impact_signature(
                            bodies[big], (big == i) ? normal : -normal,
                            std::clamp(combined_disruption, 0.0f, 1.2f),
                            pre_small.mass / std::max(total_mass, 1.0e-6f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(total_mass, 1.0e-6f), 0.0f, 1.5f),
                            std::clamp(pre_small.radius / std::max(bodies[big].radius, 0.1f), 0.08f, 0.95f));
                        bodies[big].props_valid = false;
                        bodies[big].visuals_valid = false;
                        bodies[sml].marked_for_removal = true;
                        continue;
                    }
                }

                if (!fragment_i && !fragment_j) {
                    if (bodies[i].mass <= bodies[j].mass && (can_fragment(bodies[i]) || bodies[i].mass > 2.0e-9f))
                        fragment_i = true;
                    else if (can_fragment(bodies[j]) || bodies[j].mass > 2.0e-9f)
                        fragment_j = true;
                }

                int shock_fragments = std::clamp(
                    cfg.fragment_count + (int)std::floor(combined_disruption * (soft_body_pair ? 4.0f : 2.0f)),
                    cfg.fragment_count, 12);
                if (soft_body_pair && !compact_vs_soft)
                    shock_fragments = std::clamp(shock_fragments, 2, 6);
                if (compact_vs_soft)
                    shock_fragments = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 6);
                float ejecta_speed = 0.0f;
                if (soft_body_pair && !compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.40f + combined_disruption * 0.60f) + closing_speed * 0.18f,
                        0.004f, 0.060f);
                } else if (compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.65f + combined_disruption * 0.70f) + closing_speed * 0.28f,
                        0.010f, 0.20f);
                } else {
                    ejecta_speed = std::max(2.0f, impact_speed * (0.32f + combined_disruption * 0.18f));
                }

                if (fragment_i) {
                    bool keep_remnant = false;
                    float removed = bodies[i].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[i].type == CTYPE_PLANET || bodies[i].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.42f + disruption_i * 0.32f +
                            std::min(overlap_fraction, 0.6f) * 0.30f +
                            spin_over_i * 0.16f, 0.10f, 0.85f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.25f : 0.12f);
                        float remnant_mass = bodies[i].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[i].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[i].mass, 1.0e-12f));
                            bodies[i].mass = remnant_mass;
                            bodies[i].radius = std::max(bodies[i].radius * mass_scale, 0.1f);
                            bodies[i].frag_generation = std::min<uint32_t>(bodies[i].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            if (cfg.spin_fragmentation) {
                                float post_spin_cap = spin_fragmentation_critical_omega(bodies[i], cfg.G) *
                                    std::max(spin_threshold * 0.92f, 0.35f);
                                if (post_spin_cap > 0.0f) {
                                    bodies[i].angular_vel = std::copysign(
                                        std::min(std::abs(bodies[i].angular_vel), post_spin_cap),
                                        bodies[i].angular_vel);
                                }
                            }
                            bodies[i].props_valid = false;
                            bodies[i].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[i].pos, bodies[i].vel, removed,
                                        shock_fragments, bodies[i].frag_generation,
                                        bodies[i].temperature, -impact_axis, ejecta_speed,
                                        &bodies[i], std::max(disruption_i, combined_disruption));
                        register_mass_loss(bodies[i], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[i].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[i], normal,
                            std::clamp(disruption_i + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[i].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[i], normal,
                        std::clamp(disruption_i + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[j].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.90f));
                }

                if (fragment_j) {
                    bool keep_remnant = false;
                    float removed = bodies[j].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.42f + disruption_j * 0.32f +
                            std::min(overlap_fraction, 0.6f) * 0.30f +
                            spin_over_j * 0.16f, 0.10f, 0.85f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.25f : 0.12f);
                        float remnant_mass = bodies[j].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[j].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[j].mass, 1.0e-12f));
                            bodies[j].mass = remnant_mass;
                            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
                            bodies[j].frag_generation = std::min<uint32_t>(bodies[j].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            if (cfg.spin_fragmentation) {
                                float post_spin_cap = spin_fragmentation_critical_omega(bodies[j], cfg.G) *
                                    std::max(spin_threshold * 0.92f, 0.35f);
                                if (post_spin_cap > 0.0f) {
                                    bodies[j].angular_vel = std::copysign(
                                        std::min(std::abs(bodies[j].angular_vel), post_spin_cap),
                                        bodies[j].angular_vel);
                                }
                            }
                            bodies[j].props_valid = false;
                            bodies[j].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[j].pos, bodies[j].vel, removed,
                                        shock_fragments, bodies[j].frag_generation,
                                        bodies[j].temperature, impact_axis, ejecta_speed,
                                        &bodies[j], std::max(disruption_j, combined_disruption));
                        register_mass_loss(bodies[j], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[j].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[j], -normal,
                            std::clamp(disruption_j + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[j].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[j], -normal,
                        std::clamp(disruption_j + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[i].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.90f));
                }
            } else if (!is_star_type(bodies[i].type) && !is_black_hole_type(bodies[i].type) &&
                       !is_star_type(bodies[j].type) && !is_black_hole_type(bodies[j].type)) {
                apply_impact_signature(
                    bodies[i], normal,
                    std::clamp(disruption_i * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[j].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.80f));
                apply_impact_signature(
                    bodies[j], -normal,
                    std::clamp(disruption_j * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[i].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.80f));
            }
    }
}
