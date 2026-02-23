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

// ── OrganismManager::reset ────────────────────────────────────────────────────

void OrganismManager::reset() {
    organisms.clear();
    prev_organisms_.clear();
    next_id_ = 1;
}

// ── DBSCAN Clustering (spatial hash accelerated) ──────────────────────────────

std::vector<int> OrganismManager::build_clusters(
    const std::vector<glm::vec2>& positions)
{
    uint32_t n = static_cast<uint32_t>(positions.size());
    if (n == 0) return {};

    float eps  = cluster_radius * eps_scale;
    float eps2 = eps * eps;

    // Spatial hash
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);

    auto key = [&](float x, float y) {
        int cx = static_cast<int>(x / eps);
        int cy = static_cast<int>(y / eps);
        return (static_cast<int64_t>(cx) << 32) | uint32_t(cy);
    };

    for (uint32_t i = 0; i < n; ++i)
        grid[key(positions[i].x, positions[i].y)].push_back(i);

    // Neighbor lookup
    auto neighbors = [&](uint32_t i, std::vector<uint32_t>& out) {
        out.clear();
        float x = positions[i].x;
        float y = positions[i].y;
        int cx  = static_cast<int>(x / eps);
        int cy  = static_cast<int>(y / eps);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int64_t k = (static_cast<int64_t>(cx + dx) << 32) | uint32_t(cy + dy);
                auto it = grid.find(k);
                if (it == grid.end()) continue;

                for (uint32_t j : it->second) {
                    glm::vec2 d = positions[j] - positions[i];
                    if (glm::dot(d, d) <= eps2)
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
                    glm::vec2 d = positions[j] - positions[i];
                    if (glm::dot(d, d) < vr2)
                        particles.types[j] = ti;
                }
            }
        }
    }
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

            glm::vec2 d  = new_orgs[ni].centroid - prev_organisms_[pi].centroid;
            float     d2 = glm::dot(d, d);
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
            new_orgs[ni].traits.energy        = prev.traits.energy; // carry energy forward
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
            glm::vec2 d = new_orgs[ni].centroid - prev.centroid;
            if (glm::dot(d, d) < div_r2) {
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
            glm::vec2 d = new_orgs[ni].centroid - prev_organisms_[pi].centroid;
            if (glm::dot(d, d) < kill_r2) {
                new_orgs[ni].traits.kills++;
                break;
            }
        }
    }

    // 5.5 Metabolic update (organism-level energy)
    for (auto& org : new_orgs) {
        // Movement cost (based on average speed)
        float move_cost = org.traits.avg_speed * 0.002f;

        // Density cost (based on how tightly packed the organism is)
        float ideal_spread = cluster_radius * 0.6f;
        float density_cost = 0.0f;
        if (org.spread < ideal_spread)
            density_cost = (ideal_spread - org.spread) * 0.0015f;

        // Base metabolism (always drains)
        float base_metabolism = 0.001f;

        // Feeding gain – for now, tie it simply to kills (predation-like)
        float feeding_gain = org.traits.kills * 0.02f;

        // Update energy
        org.traits.energy = glm::clamp(
            org.traits.energy + feeding_gain - move_cost - density_cost - base_metabolism,
            0.0f, 1.0f
        );

        // Death: organism dissolves into dust
        if (org.traits.energy <= 0.0f) {
            for (uint32_t idx : org.particle_indices)
                particles.types[idx] = 0; // dust
            org.traits.size = 0; // mark as effectively dead
        }
    }

    // Remove dead (size==0) organisms from the list
    new_orgs.erase(
        std::remove_if(new_orgs.begin(), new_orgs.end(),
                       [](const Organism& o) { return o.traits.size == 0; }),
        new_orgs.end()
    );

    // 6. Commit & feedback
    organisms       = std::move(new_orgs);
    prev_organisms_ = organisms;

    apply_viral_infections(positions, particles);
    apply_trait_feedback(particles);
}
