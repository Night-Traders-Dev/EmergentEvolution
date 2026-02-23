#include "organism.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cmath>
#include <unordered_map>

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline int64_t cell_key(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
}

// Shortest-path squared distance on a toroidal world
static inline float toroidal_dist2(const glm::vec2& a, const glm::vec2& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    if (dx >  static_cast<float>(REGION_W) * 0.5f) dx -= static_cast<float>(REGION_W);
    if (dx < -static_cast<float>(REGION_W) * 0.5f) dx += static_cast<float>(REGION_W);
    if (dy >  static_cast<float>(REGION_H) * 0.5f) dy -= static_cast<float>(REGION_H);
    if (dy < -static_cast<float>(REGION_H) * 0.5f) dy += static_cast<float>(REGION_H);
    return dx * dx + dy * dy;
}

// ── OrganismManager::reset ────────────────────────────────────────────────────

void OrganismManager::reset() {
    organisms.clear();
    prev_organisms_.clear();
    next_id_           = 1;
    last_births        = 0;
    last_deaths        = 0;
    dust_count         = 0;
    alive_count        = 0;
    pop_history_idx    = 0;
    pop_history_count  = 0;
    std::memset(pop_history,       0, sizeof(pop_history));
    std::memset(type_populations,  0, sizeof(type_populations));
}

// ── DBSCAN Clustering (spatial hash accelerated) ──────────────────────────────

std::vector<int> OrganismManager::build_clusters(
    const std::vector<glm::vec2>& positions)
{
    uint32_t n = static_cast<uint32_t>(positions.size());
    if (n == 0) return {};

    float eps  = cluster_radius * eps_scale;
    float eps2 = eps * eps;

    // Grid dimensions — used for toroidal cell wrapping
    int grid_w = std::max(1, static_cast<int>(std::ceil(static_cast<float>(REGION_W) / eps)));
    int grid_h = std::max(1, static_cast<int>(std::ceil(static_cast<float>(REGION_H) / eps)));

    // Cell key with toroidal modular cell coordinates
    auto tkey = [&](int cx, int cy) -> int64_t {
        cx = ((cx % grid_w) + grid_w) % grid_w;
        cy = ((cy % grid_h) + grid_h) % grid_h;
        return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
    };

    // Spatial hash
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);

    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / eps);
        int cy = static_cast<int>(positions[i].y / eps);
        grid[tkey(cx, cy)].push_back(i);
    }

    // Neighbor lookup — toroidal cell search + shortest-path distance
    auto neighbors = [&](uint32_t i, std::vector<uint32_t>& out) {
        out.clear();
        int cx = static_cast<int>(positions[i].x / eps);
        int cy = static_cast<int>(positions[i].y / eps);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.find(tkey(cx + dx, cy + dy));
                if (it == grid.end()) continue;

                for (uint32_t j : it->second) {
                    if (toroidal_dist2(positions[i], positions[j]) <= eps2)
                        out.push_back(j);
                }
            }
        }
    };

    enum State { UNVISITED, NOISE, CLUSTERED };
    std::vector<State> state(n, UNVISITED);
    std::vector<int>   cluster_id(n, -1);

    int current_cluster = 0;
    std::vector<uint32_t> neigh, neigh2;

    // DBSCAN main loop
    for (uint32_t i = 0; i < n; ++i) {
        if (state[i] != UNVISITED) continue;

        neighbors(i, neigh);

        if (neigh.size() < min_pts) {
            state[i] = NOISE;
            continue;
        }

        int cid = current_cluster++;
        cluster_id[i] = cid;
        state[i]      = CLUSTERED;

        std::vector<uint32_t> stack(neigh.begin(), neigh.end());

        while (!stack.empty()) {
            uint32_t p = stack.back();
            stack.pop_back();

            if (state[p] == NOISE) {
                state[p]      = CLUSTERED;
                cluster_id[p] = cid;
            }

            if (state[p] != UNVISITED) continue;

            state[p]      = CLUSTERED;
            cluster_id[p] = cid;

            neighbors(p, neigh2);
            if (neigh2.size() >= min_pts)
                stack.insert(stack.end(), neigh2.begin(), neigh2.end());
        }
    }

    return cluster_id;
}

// ── Viral infection ───────────────────────────────────────────────────────────

void OrganismManager::apply_viral_infections(
    const std::vector<glm::vec2>& positions,
    Particles& particles)
{
    uint32_t n        = static_cast<uint32_t>(positions.size());
    float    viral_radius = cluster_radius * 0.5f;
    float    vr2          = viral_radius * viral_radius;
    float    cell_sz      = viral_radius;

    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);

    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / cell_sz);
        int cy = static_cast<int>(positions[i].y / cell_sz);
        grid[cell_key(cx, cy)].push_back(i);
    }

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t ti = particles.types[i];
        if (!(particles.behavior_flags[ti] & BEHAVIOR_VIRAL)) continue;

        int cx = static_cast<int>(positions[i].x / cell_sz);
        int cy = static_cast<int>(positions[i].y / cell_sz);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.find(cell_key(cx + dx, cy + dy));
                if (it == grid.end()) continue;
                for (uint32_t j : it->second) {
                    if (j == i) continue;
                    uint32_t tj = particles.types[j];
                    if (particles.behavior_flags[tj] & BEHAVIOR_VIRAL) continue;
                    if (toroidal_dist2(positions[i], positions[j]) < vr2)
                        particles.types[j] = ti;
                }
            }
        }
    }
}

// ── Reproductive birth ────────────────────────────────────────────────────────
// REPRODUCTIVE particles with high energy convert nearby recovered dust (type 0)
// into copies of themselves, closing the birth-from-death cycle.

uint32_t OrganismManager::apply_reproductive_birth(
    const std::vector<glm::vec2>& positions,
    const std::vector<float>&     energies,
    Particles& particles)
{
    constexpr float energy_threshold = 0.75f;  // parent must be well-fed
    constexpr float dust_min_energy  = 0.15f;  // dust must have recovered some energy
    constexpr int   max_births       = 20;     // cap births per update cycle

    uint32_t n       = static_cast<uint32_t>(positions.size());
    float    cell_sz = cluster_radius;
    float    birth_r2 = (cluster_radius * 2.0f) * (cluster_radius * 2.0f);

    // Spatial hash of eligible dust particles
    std::unordered_map<int64_t, std::vector<uint32_t>> dust_grid;
    dust_grid.reserve(n / 8 + 1);
    for (uint32_t i = 0; i < n; ++i) {
        if (particles.types[i] == 0 && energies[i] >= dust_min_energy) {
            int cx = static_cast<int>(positions[i].x / cell_sz);
            int cy = static_cast<int>(positions[i].y / cell_sz);
            dust_grid[cell_key(cx, cy)].push_back(i);
        }
    }
    if (dust_grid.empty()) return 0;

    uint32_t births = 0;
    for (uint32_t i = 0; i < n && static_cast<int>(births) < max_births; ++i) {
        uint32_t ti = particles.types[i];
        if (!(particles.behavior_flags[ti] & BEHAVIOR_REPRODUCTIVE)) continue;
        if (energies[i] < energy_threshold) continue;

        // Find nearest eligible dust particle within birth radius
        int cx = static_cast<int>(positions[i].x / cell_sz);
        int cy = static_cast<int>(positions[i].y / cell_sz);

        uint32_t best_j = UINT32_MAX;
        float    best_d2 = birth_r2;

        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                auto it = dust_grid.find(cell_key(cx + dx, cy + dy));
                if (it == dust_grid.end()) continue;
                for (uint32_t j : it->second) {
                    float d2 = toroidal_dist2(positions[i], positions[j]);
                    if (d2 < best_d2) { best_d2 = d2; best_j = j; }
                }
            }
        }

        if (best_j != UINT32_MAX) {
            // 8% mutation chance: offspring gets a random non-dust type
            uint32_t new_type = ti;
            uint32_t hash = (best_j * 2654435761u) ^
                            static_cast<uint32_t>(positions[best_j].x * 100.0f) ^
                            static_cast<uint32_t>(positions[best_j].y * 100.0f);
            bool mutated = (MAX_PARTICLE_TYPES > 1 && (hash % 100u) < 8u);
            if (mutated) {
                new_type = 1u + (hash / 100u) % (MAX_PARTICLE_TYPES - 1u);

                // Force row mutation: drift new_type's interaction strengths slightly
                // Each birth-via-mutation nudges the type's force row by ±0.05
                uint32_t h = hash;
                for (uint32_t k = 0; k < MAX_PARTICLE_TYPES; ++k) {
                    h = h * 1664525u + 1013904223u;  // LCG step
                    float delta = (static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f) * 0.05f;
                    uint32_t fidx = new_type + k * MAX_PARTICLE_TYPES;
                    if (fidx < static_cast<uint32_t>(particles.forces.size()))
                        particles.forces[fidx] = std::clamp(particles.forces[fidx] + delta, -1.0f, 1.0f);
                }
            }
            particles.types[best_j] = new_type;

            // Remove from grid so it isn't double-converted
            int jcx = static_cast<int>(positions[best_j].x / cell_sz);
            int jcy = static_cast<int>(positions[best_j].y / cell_sz);
            auto it = dust_grid.find(cell_key(jcx, jcy));
            if (it != dust_grid.end()) {
                auto& vec = it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), best_j), vec.end());
            }
            ++births;
        }
    }
    return births;
}

// ── Trait feedback ────────────────────────────────────────────────────────────

void OrganismManager::apply_trait_feedback(Particles& particles) {
    for (auto& org : organisms) {
        uint32_t type = org.traits.dominant_type;

        float kill_bonus = std::min(org.traits.kills * 0.1f, 0.5f);
        particles.trait_scales[type] = 1.0f + kill_bonus;

        particles.structure_integrity[type] =
            1.0f + (org.traits.generation * 0.05f);
    }
}

// ── Main update ───────────────────────────────────────────────────────────────

void OrganismManager::update(
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<float>&     energies,
    const std::vector<uint32_t>&  types,
    Particles& particles)
{
    uint32_t n = static_cast<uint32_t>(positions.size());
    if (n == 0) { organisms.clear(); return; }

    // 1. DBSCAN cluster
    auto parent = build_clusters(positions);

    std::unordered_map<int, std::vector<uint32_t>> root_map;
    root_map.reserve(n / 8 + 1);
    for (uint32_t i = 0; i < n; ++i)
        root_map[parent[i]].push_back(i);

    // 2. Build new organisms
    std::vector<Organism> new_orgs;
    new_orgs.reserve(root_map.size());

    for (auto& [root, members] : root_map) {
        if (members.size() < ORGANISM_MIN_SIZE) continue;

        Organism org{};
        org.id               = next_id_++;
        org.particle_indices = members;
        org.traits.size      = static_cast<uint32_t>(members.size());

        glm::vec2 sum_pos(0.0f), sum_vel(0.0f);
        for (uint32_t idx : members) {
            sum_pos += positions[idx];
            sum_vel += velocities[idx];
            uint32_t t = types[idx];
            if (t < MAX_PARTICLE_TYPES)
                org.traits.type_counts[t]++;
        }

        float inv = 1.0f / static_cast<float>(members.size());
        org.centroid         = sum_pos * inv;
        org.traits.avg_speed = glm::length(sum_vel * inv);

        // Average GPU-readback energy across all member particles
        float total_energy = 0.0f;
        for (uint32_t idx : members)
            total_energy += (idx < energies.size() ? energies[idx] : 1.0f);
        org.traits.energy = total_energy * inv;

        float sum_d2 = 0.0f;
        for (uint32_t idx : members) {
            glm::vec2 d = positions[idx] - org.centroid;
            sum_d2 += glm::dot(d, d);
        }
        org.spread = std::sqrt(sum_d2 * inv);

        org.traits.dominant_type = 0;
        uint32_t max_count = 0;
        for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
            if (org.traits.type_counts[t] > max_count) {
                max_count = org.traits.type_counts[t];
                org.traits.dominant_type = t;
            }
        }

        new_orgs.push_back(std::move(org));
    }

    // 3. Match new to previous
    std::vector<bool> prev_matched(prev_organisms_.size(), false);
    std::vector<bool> new_matched(new_orgs.size(), false);

    std::unordered_map<uint64_t, uint32_t> prev_sizes;
    prev_sizes.reserve(prev_organisms_.size());
    for (const auto& p : prev_organisms_)
        prev_sizes[p.id] = p.traits.size;

    float match_r2 = (cluster_radius * 3.0f) * (cluster_radius * 3.0f);

    for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
        float best_d2 = match_r2;
        int   best_pi = -1;

        for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
            if (prev_matched[pi]) continue;

            float ratio = static_cast<float>(new_orgs[ni].traits.size) /
                          static_cast<float>(prev_organisms_[pi].traits.size);
            if (ratio < 0.3f || ratio > 3.5f) continue;

            float d2 = toroidal_dist2(new_orgs[ni].centroid, prev_organisms_[pi].centroid);
            if (d2 < best_d2) { best_d2 = d2; best_pi = static_cast<int>(pi); }
        }

        if (best_pi >= 0) {
            prev_matched[best_pi] = true;
            new_matched[ni]       = true;
            const auto& prev      = prev_organisms_[best_pi];
            new_orgs[ni].id                   = prev.id;
            new_orgs[ni].traits.kills         = prev.traits.kills;
            new_orgs[ni].traits.divisions     = prev.traits.divisions;
            new_orgs[ni].traits.generation    = prev.traits.generation;
            new_orgs[ni].traits.parent_id     = prev.traits.parent_id;
            // energy is read from GPU each update — no carry-forward needed
        }
    }

    // 4. Division detection (DBSCAN-aware)
    float div_r2 = (cluster_radius * 5.0f) * (cluster_radius * 5.0f);

    for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
        if (prev_matched[pi]) continue;
        const auto& prev = prev_organisms_[pi];
        if (prev.traits.size < ORGANISM_MIN_SIZE * 2) continue;

        std::vector<size_t> nearby;
        uint32_t total = 0;

        for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
            if (toroidal_dist2(new_orgs[ni].centroid, prev.centroid) < div_r2) {
                nearby.push_back(ni);
                total += new_orgs[ni].traits.size;
            }
        }

        if (nearby.size() < 2) continue;

        if (total < prev.traits.size * 0.7f ||
            total > prev.traits.size * 1.4f)
            continue;

        for (size_t ni : nearby) {
            new_orgs[ni].traits.generation = prev.traits.generation + 1;
            new_orgs[ni].traits.divisions  = prev.traits.divisions + 1;
            new_orgs[ni].traits.parent_id  = prev.id;
        }
    }

    // 5. Consumption detection (DBSCAN-aware)
    float kill_r2 = (cluster_radius * 4.0f) * (cluster_radius * 4.0f);

    for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
        if (!new_matched[ni]) continue;

        auto it = prev_sizes.find(new_orgs[ni].id);
        if (it == prev_sizes.end()) continue;

        if (new_orgs[ni].traits.size < it->second * 1.25f) continue;

        for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
            if (prev_matched[pi]) continue;
            if (toroidal_dist2(new_orgs[ni].centroid, prev_organisms_[pi].centroid) < kill_r2) {
                new_orgs[ni].traits.kills++;
                break;
            }
        }
    }

    // 5.5 Per-particle death from GPU energy
    // Any particle with zero energy becomes dust (type 0); types re-upload next frame.
    uint32_t deaths_this_tick = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(energies.size()); ++i) {
        if (energies[i] <= 0.0f && particles.types[i] != 0) {
            particles.types[i] = 0;
            ++deaths_this_tick;
        }
    }

    // 5.6 Reproductive birth: high-energy REPRODUCTIVE particles convert nearby dust
    uint32_t births_this_tick = apply_reproductive_birth(positions, energies, particles);

    // 5.7 Population statistics
    {
        uint32_t dc = 0, ac = 0;
        std::memset(type_populations, 0, sizeof(type_populations));
        for (uint32_t i = 0; i < static_cast<uint32_t>(particles.types.size()); ++i) {
            uint32_t t = particles.types[i];
            if (t == 0) {
                ++dc;
            } else {
                ++ac;
            }
            if (t < MAX_PARTICLE_TYPES)
                ++type_populations[t];
        }
        dust_count  = dc;
        alive_count = ac;
        last_deaths = deaths_this_tick;
        last_births = births_this_tick;

        pop_history[pop_history_idx] = static_cast<float>(ac);
        pop_history_idx = (pop_history_idx + 1) % POP_HISTORY_LEN;
        if (pop_history_count < POP_HISTORY_LEN) ++pop_history_count;
    }

    // 6. Commit & feedback
    organisms       = std::move(new_orgs);
    prev_organisms_ = organisms;

    apply_viral_infections(positions, particles);
    apply_trait_feedback(particles);
}
