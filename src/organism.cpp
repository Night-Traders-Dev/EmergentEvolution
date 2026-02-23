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

// ── Clustering: spatial hash + union-find ──────────────────────────────────────

std::vector<int> OrganismManager::build_clusters(
    const std::vector<glm::vec2>& positions)
{
    uint32_t n = static_cast<uint32_t>(positions.size());

    // DSU with full path compression
    std::vector<int> parent(n);
    std::vector<int> rank_arr(n, 0);
    std::iota(parent.begin(), parent.end(), 0);

    auto find = [&](int x) {
        // Iterative with full compression (two-pass)
        int root = x;
        while (parent[root] != root) root = parent[root];
        while (parent[x] != root) {
            int next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    };

    auto unite = [&](int a, int b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank_arr[a] < rank_arr[b]) std::swap(a, b);
        parent[b] = a;
        if (rank_arr[a] == rank_arr[b]) rank_arr[a]++;
    };

    // Build spatial hash
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);

    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / cluster_radius);
        int cy = static_cast<int>(positions[i].y / cluster_radius);
        grid[cell_key(cx, cy)].push_back(i);
    }

    float r2 = cluster_radius * cluster_radius;

    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(positions[i].x / cluster_radius);
        int cy = static_cast<int>(positions[i].y / cluster_radius);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.find(cell_key(cx + dx, cy + dy));
                if (it == grid.end()) continue;
                for (uint32_t j : it->second) {
                    if (j <= i) continue;
                    glm::vec2 d = positions[j] - positions[i];
                    if (glm::dot(d, d) < r2)
                        unite(static_cast<int>(i), static_cast<int>(j));
                }
            }
        }
    }

    // Final pass: ensure all parents point directly to root
    for (uint32_t i = 0; i < n; ++i)
        parent[i] = find(i);

    return parent;
}

// ── Trait feedback ────────────────────────────────────────────────────────────

void OrganismManager::apply_trait_feedback(Particles& particles) {
    for (float& s : particles.trait_scales) s = 1.0f;

    for (const auto& org : organisms) {
        uint32_t dt = org.traits.dominant_type;
        if (dt >= MAX_PARTICLE_TYPES) continue;
        float kill_bonus = 0.1f * std::min(static_cast<float>(org.traits.kills),     5.0f);
        float div_bonus  = 0.03f * std::min(static_cast<float>(org.traits.divisions), 10.0f);
        float scale = 1.0f + kill_bonus + div_bonus;
        particles.trait_scales[dt] = std::max(particles.trait_scales[dt], scale);
    }

    for (float& s : particles.trait_scales)
        s = std::min(s, 1.8f);
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

    // ── 1. Cluster particles ──────────────────────────────────────────────────
    auto parent = build_clusters(positions);

    std::unordered_map<int, std::vector<uint32_t>> root_map;
    root_map.reserve(n / 8 + 1);
    for (uint32_t i = 0; i < n; ++i)
        root_map[parent[i]].push_back(i);

    // ── 2. Build new Organism structs ─────────────────────────────────────────
    std::vector<Organism> new_orgs;
    new_orgs.reserve(root_map.size());

    for (auto& [root, members] : root_map) {
        if (members.size() < ORGANISM_MIN_SIZE) continue;

        Organism org{};
        org.id = next_id_++;
        org.particle_indices = members;
        org.traits.size = static_cast<uint32_t>(members.size());

        glm::vec2 sum_pos(0.0f), sum_vel(0.0f);
        for (uint32_t idx : members) {
            sum_pos += positions[idx];
            sum_vel += velocities[idx];
            uint32_t t = types[idx];
            if (t < MAX_PARTICLE_TYPES)
                org.traits.type_counts[t]++;
        }

        float inv = 1.0f / static_cast<float>(members.size());
        org.centroid        = sum_pos * inv;
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

    // ── 3. Match new orgs to previous (greedy nearest-centroid) ───────────────
    std::vector<bool> prev_matched(prev_organisms_.size(), false);
    std::vector<bool> new_matched(new_orgs.size(), false);

    // Record previous sizes for consumption detection
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

            float ratio = static_cast<float>(new_orgs[ni].traits.size)
                        / static_cast<float>(prev_organisms_[pi].traits.size);
            if (ratio < 0.3f || ratio > 3.5f) continue;

            glm::vec2 d = new_orgs[ni].centroid - prev_organisms_[pi].centroid;
            float d2 = glm::dot(d, d);
            if (d2 < best_d2) { best_d2 = d2; best_pi = static_cast<int>(pi); }
        }

        if (best_pi >= 0) {
            prev_matched[best_pi] = true;
            new_matched[ni]       = true;
            const auto& prev = prev_organisms_[best_pi];
            new_orgs[ni].id                 = prev.id;
            new_orgs[ni].traits.kills       = prev.traits.kills;
            new_orgs[ni].traits.divisions   = prev.traits.divisions;
            new_orgs[ni].traits.generation  = prev.traits.generation;
            new_orgs[ni].traits.parent_id   = prev.traits.parent_id;
        }
    }

    // ── 4. Division detection ─────────────────────────────────────────────────
    float div_r2 = (cluster_radius * 4.0f) * (cluster_radius * 4.0f);

    for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
        if (prev_matched[pi]) continue;
        const auto& prev = prev_organisms_[pi];
        if (prev.traits.size < ORGANISM_MIN_SIZE * 2) continue;

        std::vector<size_t> nearby_new;
        uint32_t nearby_total = 0;

        for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
            glm::vec2 d = new_orgs[ni].centroid - prev.centroid;
            if (glm::dot(d, d) < div_r2) {
                nearby_new.push_back(ni);
                nearby_total += new_orgs[ni].traits.size;
            }
        }

        if (nearby_new.size() >= 2) {
            int diff = static_cast<int>(nearby_total) - static_cast<int>(prev.traits.size);
            if (std::abs(diff) < static_cast<int>(prev.traits.size) / 2) {
                for (size_t ni : nearby_new) {
                    if (!new_matched[ni]) {
                        new_orgs[ni].traits.kills     = prev.traits.kills;
                        new_orgs[ni].traits.divisions = prev.traits.divisions + 1;
                        new_orgs[ni].traits.generation= prev.traits.generation + 1;
                        new_orgs[ni].traits.parent_id = prev.id;
                        new_matched[ni] = true;
                    } else {
                        new_orgs[ni].traits.divisions++;
                    }
                }
            }
        }
    }

    // ── 5. Consumption detection ──────────────────────────────────────────────
    float kill_r2 = (cluster_radius * 3.0f) * (cluster_radius * 3.0f);

    for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
        if (!new_matched[ni]) continue;

        auto it = prev_sizes.find(new_orgs[ni].id);
        if (it == prev_sizes.end()) continue;
        if (new_orgs[ni].traits.size < it->second * 1.2f) continue;

        for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
            if (prev_matched[pi]) continue;
            glm::vec2 d = new_orgs[ni].centroid - prev_organisms_[pi].centroid;
            if (glm::dot(d, d) < kill_r2) {
                new_orgs[ni].traits.kills++;
                break;
            }
        }
    }

    // ── 6. Commit & feedback ──────────────────────────────────────────────────
    organisms      = std::move(new_orgs);
    prev_organisms_ = organisms;

    apply_trait_feedback(particles);
}
