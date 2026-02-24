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

// Plain squared distance (infinite world — no wrapping)
static inline float dist2(const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 d = b - a;
    return glm::dot(d, d);
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

    // Spatial hash (infinite world — no cell-coordinate wrapping)
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n / 4 + 1);

    for (uint32_t i = 0; i < n; ++i) {
        int cx = static_cast<int>(std::floor(positions[i].x / eps));
        int cy = static_cast<int>(std::floor(positions[i].y / eps));
        grid[cell_key(cx, cy)].push_back(i);
    }

    // Neighbor lookup — plain squared distance
    auto neighbors = [&](uint32_t i, std::vector<uint32_t>& out) {
        out.clear();
        int cx = static_cast<int>(std::floor(positions[i].x / eps));
        int cy = static_cast<int>(std::floor(positions[i].y / eps));

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto it = grid.find(cell_key(cx + dx, cy + dy));
                if (it == grid.end()) continue;

                for (uint32_t j : it->second) {
                    if (dist2(positions[i], positions[j]) <= eps2)
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

// ── Classify molecular aggregate from atom composition ────────────────────────
// Atom type indices: H=0 C=1 N=2 O=3 P=4 S=5 Na=6 Cl=7

static MoleculeClass classify_molecule(const uint32_t* type_counts,
                                       uint32_t size,
                                       bool has_radical)
{
    if (size == 0) return MoleculeClass::INORGANIC;
    if (has_radical) return MoleculeClass::RADICAL;

    uint32_t H  = type_counts[0];
    uint32_t C  = type_counts[1];
    uint32_t N  = type_counts[2];
    uint32_t O  = type_counts[3];
    uint32_t P  = type_counts[4];

    // NUCLEOTIDE: phosphorus-rich (P * 3 > total size)
    if (P * 3u > size)
        return MoleculeClass::NUCLEOTIDE;

    // AMINO_ACID: nitrogen + oxygen dominant with carbon backbone
    if ((N + O) * 2u > size && C > 0)
        return MoleculeClass::AMINO_ACID;

    // WATER: mostly hydrogen with some oxygen
    if (H * 4u > size * 3u && O > 1u)
        return MoleculeClass::WATER;

    // POLYMER: carbon-heavy, large aggregate
    if (C * 2u > size && size > 20u)
        return MoleculeClass::POLYMER;

    // LIPID: carbon + hydrogen make up bulk
    if ((C + H) * 3u > size * 2u)
        return MoleculeClass::LIPID;

    return MoleculeClass::INORGANIC;
}

// ── Trait feedback — chemistry-appropriate genome nudging ─────────────────────

void OrganismManager::apply_trait_feedback(Particles& particles) {
    // Reset all scales so extinct types don't retain stale boosts
    for (float& s : particles.trait_scales) s = 1.0f;

    constexpr float nudge = 0.002f;

    for (auto& org : organisms) {
        uint32_t type = org.traits.dominant_type;

        // Force-row scale: bonds + size, capped at 1.8×
        float bond_bonus = std::min(org.traits.bond_count * 0.005f, 0.4f);
        float size_bonus = std::min(org.traits.size       * 0.001f, 0.3f);
        float new_scale  = std::clamp(1.0f + bond_bonus + size_bonus, 1.0f, 1.8f);
        particles.trait_scales[type] = std::max(particles.trait_scales[type], new_scale);

        particles.structure_integrity[type] =
            1.0f + (org.traits.generation * 0.05f);

        // Per-particle genome nudging toward molecular role
        for (uint32_t idx : org.particle_indices) {
            uint32_t base = idx * GENOME_SIZE;
            if (base + 3 >= static_cast<uint32_t>(particles.genomes.size())) continue;

            float& charge   = particles.genomes[base + 0];
            float& electroneg = particles.genomes[base + 1];
            float& reactivity = particles.genomes[base + 2];
            float& bond_str   = particles.genomes[base + 3];

            switch (org.traits.mol_class) {
            case MoleculeClass::WATER:
                // Water: pull toward high electronegativity, polar charge
                electroneg = std::min(electroneg + nudge,        2.0f);
                charge     += (0.0f - charge) * nudge * 0.5f;
                break;

            case MoleculeClass::LIPID:
                // Lipids: stronger bonds, low charge (non-polar)
                bond_str   = std::min(bond_str   + nudge * 0.5f, 0.5f);
                charge     += (0.0f - charge) * nudge;
                break;

            case MoleculeClass::AMINO_ACID:
                // Amino acids: reactive + electronegative
                reactivity = std::min(reactivity + nudge,        2.0f);
                electroneg = std::min(electroneg + nudge * 0.5f, 2.0f);
                break;

            case MoleculeClass::NUCLEOTIDE:
                // Nucleotides: strong stable bonds + high reactivity
                bond_str   = std::min(bond_str   + nudge * 0.5f, 0.5f);
                reactivity = std::min(reactivity + nudge,        2.0f);
                break;

            case MoleculeClass::RADICAL:
                // Radicals: high reactivity, destabilised bonds
                reactivity = std::min(reactivity + nudge * 1.5f, 2.0f);
                bond_str   = std::max(bond_str   - nudge,       -0.5f);
                break;

            case MoleculeClass::POLYMER:
                // Polymers: maximise bond strength
                bond_str   = std::min(bond_str   + nudge,        0.5f);
                reactivity += (1.0f - reactivity) * nudge * 0.3f;
                break;

            default:
                // INORGANIC: gentle regression toward neutral
                charge    += (0.0f - charge)    * nudge * 0.5f;
                electroneg += (1.0f - electroneg) * nudge * 0.5f;
                reactivity += (1.0f - reactivity) * nudge * 0.5f;
                bond_str   += (0.0f - bond_str)   * nudge * 0.5f;
                break;
            }
        }
    }
}

// ── Main update ───────────────────────────────────────────────────────────────

void OrganismManager::update(
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<float>&     energies,
    const std::vector<uint32_t>&  types,
    Particles& particles,
    BondManager& bond_manager)
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
        bool      has_radical = false;
        for (uint32_t idx : members) {
            sum_pos += positions[idx];
            sum_vel += velocities[idx];
            uint32_t t = types[idx];
            if (t < MAX_PARTICLE_TYPES) {
                org.traits.type_counts[t]++;
                if (particles.behavior_flags[t] & BEHAVIOR_RADICAL)
                    has_radical = true;
            }
        }

        float inv = 1.0f / static_cast<float>(members.size());
        org.centroid         = sum_pos * inv;
        org.traits.avg_speed = glm::length(sum_vel * inv);

        // Average GPU-readback energy across all member particles
        float total_energy = 0.0f;
        for (uint32_t idx : members)
            total_energy += (idx < energies.size() ? energies[idx] : 1.0f);
        org.traits.energy = total_energy * inv;

        // Average genome traits: charge, electroneg, reactivity, bond_str
        float sum_charge = 0.0f, sum_electroneg = 0.0f,
              sum_reactivity = 0.0f, sum_bond_str = 0.0f;
        for (uint32_t idx : members) {
            uint32_t base = idx * GENOME_SIZE;
            if (base + 3 < static_cast<uint32_t>(particles.genomes.size())) {
                sum_charge      += particles.genomes[base + 0];
                sum_electroneg  += particles.genomes[base + 1];
                sum_reactivity  += particles.genomes[base + 2];
                sum_bond_str    += particles.genomes[base + 3];
            } else {
                sum_electroneg += 1.0f;
                sum_reactivity += 1.0f;
            }
        }
        org.traits.avg_charge        = sum_charge        * inv;
        org.traits.avg_electroneg    = sum_electroneg    * inv;
        org.traits.avg_reactivity    = sum_reactivity    * inv;
        org.traits.avg_bond_strength = sum_bond_str      * inv;

        // Bond count: sum bond_counts of all member particles
        uint32_t total_bonds = 0;
        for (uint32_t idx : members) {
            if (idx < static_cast<uint32_t>(bond_manager.bond_counts.size()))
                total_bonds += bond_manager.bond_counts[idx];
        }
        org.traits.bond_count = total_bonds;

        // Dominant type
        org.traits.dominant_type = 0;
        uint32_t max_count = 0;
        for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
            if (org.traits.type_counts[t] > max_count) {
                max_count = org.traits.type_counts[t];
                org.traits.dominant_type = t;
            }
        }

        // Molecular class from atom composition
        org.traits.mol_class = classify_molecule(org.traits.type_counts,
                                                  org.traits.size,
                                                  has_radical);

        float sum_d2 = 0.0f;
        for (uint32_t idx : members) {
            glm::vec2 d = positions[idx] - org.centroid;
            sum_d2 += glm::dot(d, d);
        }
        org.spread = std::sqrt(sum_d2 * inv);

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

            float d2 = dist2(new_orgs[ni].centroid, prev_organisms_[pi].centroid);
            if (d2 < best_d2) { best_d2 = d2; best_pi = static_cast<int>(pi); }
        }

        if (best_pi >= 0) {
            prev_matched[best_pi] = true;
            new_matched[ni]       = true;
            const auto& prev      = prev_organisms_[best_pi];
            new_orgs[ni].id                = prev.id;
            new_orgs[ni].traits.kills      = prev.traits.kills;
            new_orgs[ni].traits.divisions  = prev.traits.divisions;
            new_orgs[ni].traits.generation = prev.traits.generation;
            new_orgs[ni].traits.parent_id  = prev.traits.parent_id;
        }
    }

    // 4. Division detection
    float div_r2 = (cluster_radius * 5.0f) * (cluster_radius * 5.0f);

    for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
        if (prev_matched[pi]) continue;
        const auto& prev = prev_organisms_[pi];
        if (prev.traits.size < ORGANISM_MIN_SIZE * 2) continue;

        std::vector<size_t> nearby;
        uint32_t total = 0;

        for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
            if (dist2(new_orgs[ni].centroid, prev.centroid) < div_r2) {
                nearby.push_back(ni);
                total += new_orgs[ni].traits.size;
            }
        }

        if (nearby.size() < 2) continue;
        if (total < prev.traits.size * 0.7f || total > prev.traits.size * 1.4f) continue;

        for (size_t ni : nearby) {
            new_orgs[ni].traits.generation = prev.traits.generation + 1;
            new_orgs[ni].traits.divisions  = prev.traits.divisions + 1;
            new_orgs[ni].traits.parent_id  = prev.id;

            // Genome mutation — ±3% on electronegativity + reactivity per division.
            // This is the engine of Darwinian evolution: heritable variation under
            // selection pressure (energy efficiency determines survival).
            constexpr float MUT = 0.03f;
            for (uint32_t idx : new_orgs[ni].particle_indices) {
                uint32_t base = idx * GENOME_SIZE;
                if (base + 3 >= static_cast<uint32_t>(particles.genomes.size())) continue;
                float m = mutation_dist_(mutation_rng_) * MUT;
                particles.genomes[base + 1] = std::clamp(
                    particles.genomes[base + 1] + m, 0.1f, 2.0f);
                particles.genomes[base + 2] = std::clamp(
                    particles.genomes[base + 2] + m, 0.1f, 2.0f);
            }
        }
    }

    // 5. Consumption detection
    float kill_r2 = (cluster_radius * 4.0f) * (cluster_radius * 4.0f);

    for (size_t ni = 0; ni < new_orgs.size(); ++ni) {
        if (!new_matched[ni]) continue;

        auto it = prev_sizes.find(new_orgs[ni].id);
        if (it == prev_sizes.end()) continue;
        if (new_orgs[ni].traits.size < it->second * 1.25f) continue;

        for (size_t pi = 0; pi < prev_organisms_.size(); ++pi) {
            if (prev_matched[pi]) continue;
            if (dist2(new_orgs[ni].centroid, prev_organisms_[pi].centroid) < kill_r2) {
                new_orgs[ni].traits.kills++;
                break;
            }
        }
    }

    // 5.5 Per-particle reset from zero energy (atom returns to lowest energy: H)
    uint32_t deaths_this_tick = 0;
    for (uint32_t i = 0; i < static_cast<uint32_t>(energies.size()); ++i) {
        if (energies[i] <= 0.0f && particles.types[i] != 0) {
            particles.types[i] = 0;
            ++deaths_this_tick;
        }
    }

    // 5.6 Population statistics
    {
        uint32_t dc = 0, ac = 0;
        std::memset(type_populations, 0, sizeof(type_populations));
        for (uint32_t i = 0; i < static_cast<uint32_t>(particles.types.size()); ++i) {
            uint32_t t = particles.types[i];
            if (t == 0) ++dc; else ++ac;
            if (t < MAX_PARTICLE_TYPES)
                ++type_populations[t];
        }
        dust_count  = dc;
        alive_count = ac;
        last_deaths = deaths_this_tick;
        last_births = 0;

        pop_history[pop_history_idx] = static_cast<float>(ac);
        pop_history_idx = (pop_history_idx + 1) % POP_HISTORY_LEN;
        if (pop_history_count < POP_HISTORY_LEN) ++pop_history_count;
    }

    // 6. Commit & feedback
    organisms       = std::move(new_orgs);
    prev_organisms_ = organisms;

    apply_trait_feedback(particles);
}
