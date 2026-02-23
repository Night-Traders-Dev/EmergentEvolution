#include "bond_manager.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

// ── Helpers ───────────────────────────────────────────────────────────────────

int64_t BondManager::cell_key(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
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
}

// ── reset ─────────────────────────────────────────────────────────────────────

void BondManager::reset(uint32_t n) {
    n_particles_ = n;
    bond_partners.assign(static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE, 0xFFFFFFFFu);
    bond_counts.assign(n, 0u);
}

// ── break_stretched_bonds ─────────────────────────────────────────────────────

void BondManager::break_stretched_bonds(
    const std::vector<glm::vec2>& positions,
    float rest_length,
    float break_factor)
{
    float break_dist_sq = (rest_length * break_factor) * (rest_length * break_factor);

    for (uint32_t i = 0; i < n_particles_; ++i) {
        uint32_t base = i * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            uint32_t j = bond_partners[base + s];
            if (j == 0xFFFFFFFFu || j <= i) continue; // process each pair once

            glm::vec2 d = positions[j] - positions[i];
            float d2 = glm::dot(d, d);
            if (d2 > break_dist_sq)
                remove_bond(i, j);
        }
    }
}

// ── form_new_bonds ────────────────────────────────────────────────────────────

void BondManager::form_new_bonds(
    const std::vector<glm::vec2>& positions,
    const std::vector<uint32_t>&  types,
    float form_radius)
{
    float form_r2   = form_radius * form_radius;
    float cell_size = form_radius;

    // Build spatial hash of particles that still have free valence slots
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

    for (uint32_t i = 0; i < n_particles_; ++i) {
        uint32_t ti  = (i < types.size()) ? types[i] : 0;
        uint32_t cap = (ti < ATOM_COUNT) ? ATOM_VALENCE[ti] : 1u;
        if (bond_counts[i] >= cap) continue;

        int cx = static_cast<int>(std::floor(positions[i].x / cell_size));
        int cy = static_cast<int>(std::floor(positions[i].y / cell_size));

        bool saturated = false;
        for (int dy = -1; dy <= 1 && !saturated; ++dy) {
            for (int dx = -1; dx <= 1 && !saturated; ++dx) {
                auto it = grid.find(cell_key(cx + dx, cy + dy));
                if (it == grid.end()) continue;

                for (uint32_t j : it->second) {
                    if (j <= i) continue; // each pair once; also skips j==i

                    // Re-check i's cap (may have changed in this loop)
                    if (bond_counts[i] >= cap) { saturated = true; break; }

                    uint32_t tj  = (j < types.size()) ? types[j] : 0;
                    uint32_t capj = (tj < ATOM_COUNT) ? ATOM_VALENCE[tj] : 1u;
                    if (bond_counts[j] >= capj) continue;

                    if (are_bonded(i, j)) continue;
                    if (!can_bond(ti, tj))  continue;

                    glm::vec2 d = positions[j] - positions[i];
                    if (glm::dot(d, d) < form_r2)
                        add_bond(i, j);
                }
            }
        }
    }
}

// ── update ────────────────────────────────────────────────────────────────────

void BondManager::update(
    const std::vector<glm::vec2>& positions,
    const std::vector<uint32_t>&  types,
    float bond_form_radius,
    float bond_rest_length,
    float bond_break_factor)
{
    if (n_particles_ == 0) return;
    // Resize if particle count changed (e.g. after reset)
    if (static_cast<uint32_t>(bond_counts.size()) != n_particles_) {
        reset(static_cast<uint32_t>(positions.size()));
    }

    break_stretched_bonds(positions, bond_rest_length, bond_break_factor);
    form_new_bonds(positions, types, bond_form_radius);
}
