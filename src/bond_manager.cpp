#include "bond_manager.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// ── Helpers ───────────────────────────────────────────────────────────────────

int64_t BondManager::cell_key(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
}

glm::vec2 BondManager::random_unit_vec() {
    static uint32_t lcg = 12345u;
    lcg = lcg * 1664525u + 1013904223u;
    float angle = static_cast<float>(lcg & 0xFFFFu) / 65535.0f * 6.28318f;
    return { std::cos(angle), std::sin(angle) };
}

bool BondManager::can_bond(uint32_t ta, uint32_t tb) const {
    if (ta >= ATOM_COUNT || tb >= ATOM_COUNT) return false;
    return (BOND_COMPAT[ta] & (1u << tb)) != 0u;
}

bool BondManager::are_bonded(uint32_t i, uint32_t j) const {
    if (i >= n_particles_) return false;
    uint32_t base = i * MAX_BONDS_PER_PARTICLE;
    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s)
        if (bond_partners[base + s] == j) return true;
    return false;
}

bool BondManager::add_bond(uint32_t i, uint32_t j) {
    // Find empty slot in i's bond list
    uint32_t base_i = i * MAX_BONDS_PER_PARTICLE;
    uint32_t slot_i = MAX_BONDS_PER_PARTICLE;
    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
        if (bond_partners[base_i + s] == 0xFFFFFFFFu) { slot_i = s; break; }
    }
    if (slot_i == MAX_BONDS_PER_PARTICLE) return false;

    // Find empty slot in j's bond list
    uint32_t base_j = j * MAX_BONDS_PER_PARTICLE;
    uint32_t slot_j = MAX_BONDS_PER_PARTICLE;
    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
        if (bond_partners[base_j + s] == 0xFFFFFFFFu) { slot_j = s; break; }
    }
    if (slot_j == MAX_BONDS_PER_PARTICLE) return false;

    bond_partners[base_i + slot_i] = j;
    bond_partners[base_j + slot_j] = i;
    bond_counts[i]++;
    bond_counts[j]++;
    return true;
}

void BondManager::remove_bond(uint32_t i, uint32_t j) {
    auto clear_slot = [&](uint32_t from, uint32_t to) {
        uint32_t base = from * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_partners[base + s] == to) {
                bond_partners[base + s] = 0xFFFFFFFFu;
                if (bond_counts[from] > 0) bond_counts[from]--;
                return;
            }
        }
    };
    clear_slot(i, j);
    clear_slot(j, i);
    // Set cooldown so these atoms don't immediately re-bond
    if (i < bond_cooldown.size()) bond_cooldown[i] = BOND_COOLDOWN_TICKS;
    if (j < bond_cooldown.size()) bond_cooldown[j] = BOND_COOLDOWN_TICKS;
}

void BondManager::clear_particle_bonds(uint32_t idx) {
    if (idx >= n_particles_) return;
    uint32_t base = idx * MAX_BONDS_PER_PARTICLE;
    for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
        uint32_t partner = bond_partners[base + s];
        if (partner != 0xFFFFFFFFu && partner < n_particles_)
            remove_bond(idx, partner);
    }
}

// ── reset ─────────────────────────────────────────────────────────────────────

void BondManager::reset(uint32_t n) {
    n_particles_ = n;
    bond_partners.assign(static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE, 0xFFFFFFFFu);
    bond_counts.assign(n, 0u);
    bond_cooldown.assign(n, 0u);
}

// ── break_stretched_bonds ─────────────────────────────────────────────────────

void BondManager::break_stretched_bonds(
    const std::vector<glm::vec2>& positions,
    float rest_length,
    float break_factor)
{
    float break_dist_sq = (rest_length * break_factor) * (rest_length * break_factor);

    // Phase 1: Parallel detection — collect pairs to break
    struct BrokenPair { uint32_t i, j; glm::vec2 midpoint; };
    std::vector<BrokenPair> broken_pairs;

    #pragma omp parallel if(n_particles_ > 2000)
    {
        std::vector<BrokenPair> local_broken;

        #pragma omp for nowait
        for (uint32_t i = 0; i < n_particles_; ++i) {
            uint32_t base = i * MAX_BONDS_PER_PARTICLE;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                uint32_t j = bond_partners[base + s];
                if (j == 0xFFFFFFFFu || j <= i) continue;

                glm::vec2 d = positions[j] - positions[i];
                if (glm::dot(d, d) > break_dist_sq) {
                    local_broken.push_back({i, j,
                        (positions[i] + positions[j]) * 0.5f});
                }
            }
        }

        #pragma omp critical
        broken_pairs.insert(broken_pairs.end(),
                           local_broken.begin(), local_broken.end());
    }

    // Phase 2: Sequential removal (modifies shared bond arrays)
    for (const auto& bp : broken_pairs) {
        remove_bond(bp.i, bp.j);
        pending_photons.push_back({bp.midpoint, random_unit_vec(), 0.60f});
    }
}

// ── form_new_bonds ────────────────────────────────────────────────────────────

void BondManager::form_new_bonds(
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<uint32_t>&  types,
    float form_radius,
    float activation_energy)
{
    float form_r2   = form_radius * form_radius;
    float cell_size = form_radius;

    // Phase 1: Build spatial hash (sequential — fast O(N))
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    grid.reserve(n_particles_ / 4 + 1);

    for (uint32_t i = 0; i < n_particles_; ++i) {
        uint32_t t = (i < types.size()) ? types[i] : 0;
        uint32_t cap = (t < ATOM_COUNT) ? ATOM_VALENCE[t] : 1u;
        if (bond_counts[i] >= cap) continue;

        int cx = static_cast<int>(std::floor(positions[i].x / cell_size));
        int cy = static_cast<int>(std::floor(positions[i].y / cell_size));
        grid[cell_key(cx, cy)].push_back(i);
    }

    // Phase 2: Parallel candidate detection (read-only grid, snapshot bond_counts)
    struct BondCandidate { uint32_t i, j; glm::vec2 midpoint; };
    std::vector<BondCandidate> candidates;
    std::vector<uint32_t> snap_counts(bond_counts);

    #pragma omp parallel if(n_particles_ > 2000)
    {
        std::vector<BondCandidate> local_cands;

        #pragma omp for nowait schedule(dynamic, 64)
        for (uint32_t i = 0; i < n_particles_; ++i) {
            uint32_t ti  = (i < types.size()) ? types[i] : 0;
            uint32_t cap = (ti < ATOM_COUNT) ? ATOM_VALENCE[ti] : 1u;
            if (snap_counts[i] >= cap) continue;

            int cx = static_cast<int>(std::floor(positions[i].x / cell_size));
            int cy = static_cast<int>(std::floor(positions[i].y / cell_size));

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    auto it = grid.find(cell_key(cx + dx, cy + dy));
                    if (it == grid.end()) continue;

                    for (uint32_t j : it->second) {
                        if (j <= i) continue;

                        uint32_t tj  = (j < types.size()) ? types[j] : 0;
                        uint32_t capj = (tj < ATOM_COUNT) ? ATOM_VALENCE[tj] : 1u;
                        if (snap_counts[j] >= capj) continue;

                        if (!can_bond(ti, tj))  continue;
                        if (bond_cooldown[i] > 0 || bond_cooldown[j] > 0) continue;

                        glm::vec2 d = positions[j] - positions[i];
                        if (glm::dot(d, d) >= form_r2) continue;

                        if (activation_energy > 0.0f && i < velocities.size() && j < velocities.size()) {
                            glm::vec2 dv  = velocities[i] - velocities[j];
                            if (0.5f * glm::dot(dv, dv) < activation_energy) continue;
                        }

                        local_cands.push_back({i, j,
                            (positions[i] + positions[j]) * 0.5f});
                    }
                }
            }
        }

        #pragma omp critical
        candidates.insert(candidates.end(),
                         local_cands.begin(), local_cands.end());
    }

    // Phase 3: Sequential bond formation (re-check live bond_counts)
    for (const auto& c : candidates) {
        uint32_t ti  = (c.i < types.size()) ? types[c.i] : 0;
        uint32_t capi = (ti < ATOM_COUNT) ? ATOM_VALENCE[ti] : 1u;
        if (bond_counts[c.i] >= capi) continue;

        uint32_t tj  = (c.j < types.size()) ? types[c.j] : 0;
        uint32_t capj = (tj < ATOM_COUNT) ? ATOM_VALENCE[tj] : 1u;
        if (bond_counts[c.j] >= capj) continue;

        if (are_bonded(c.i, c.j)) continue;

        if (add_bond(c.i, c.j)) {
            pending_photons.push_back({c.midpoint, random_unit_vec(), 0.30f});
        }
    }
}

// ── update ────────────────────────────────────────────────────────────────────

void BondManager::update(
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<uint32_t>&  types,
    float bond_form_radius,
    float bond_rest_length,
    float bond_break_factor,
    float bond_activation_energy)
{
    if (n_particles_ == 0) return;
    // Resize if particle count changed (e.g. after reset)
    if (static_cast<uint32_t>(bond_counts.size()) != n_particles_) {
        reset(static_cast<uint32_t>(positions.size()));
    }

    // Clear stale photon events from previous tick
    pending_photons.clear();

    // Tick down cooldowns
    for (auto& cd : bond_cooldown)
        if (cd > 0) --cd;

    break_stretched_bonds(positions, bond_rest_length, bond_break_factor);
    form_new_bonds(positions, velocities, types, bond_form_radius, bond_activation_energy);
}
