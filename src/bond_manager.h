#pragma once

#include "types.h"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

// Forward declaration to avoid circular include
class Particles;

// ── Bond compatibility matrix ─────────────────────────────────────────────────
// BOND_COMPAT[type_a] has bit type_b set if type_a may bond with type_b.
// Indexed 0-7: H C N O P S Na Cl
static constexpr uint32_t BOND_COMPAT[ATOM_COUNT] = {
    0b11111111u, // H  — bonds everything
    0b11111111u, // C  — bonds everything
    0b00011111u, // N  — H C N O P  (bits 0,1,2,3,4)
    0b00001111u, // O  — H C N O    (bits 0,1,2,3)
    0b00111111u, // P  — H C N O P S (bits 0-5)
    0b00110101u, // S  — H C P S    (bits 0,2,4,5)
    0b10000000u, // Na — Cl only    (bit 7)
    0b01000001u, // Cl — Na H       (bits 0,6)
};

class BondManager {
public:
    // Flat bond array: bond_partners[i * MAX_BONDS_PER_PARTICLE + slot] = partner index
    // 0xFFFFFFFF means empty slot.
    std::vector<uint32_t> bond_partners;

    // Current bond count per particle (derived; not stored on GPU).
    std::vector<uint32_t> bond_counts;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void reset(uint32_t particle_count);

    // Form new bonds and break stretched bonds.
    // Call every bond_update_interval frames from Simulation::tick() after readback.
    void update(const std::vector<glm::vec2>& positions,
                const std::vector<uint32_t>&  types,
                float bond_form_radius,
                float bond_rest_length,
                float bond_break_factor);

    bool are_bonded(uint32_t i, uint32_t j) const;

private:
    uint32_t n_particles_ = 0;

    static int64_t cell_key(int cx, int cy);

    void break_stretched_bonds(const std::vector<glm::vec2>& positions,
                               float rest_length, float break_factor);

    void form_new_bonds(const std::vector<glm::vec2>& positions,
                        const std::vector<uint32_t>&  types,
                        float form_radius);

    bool can_bond(uint32_t ta, uint32_t tb) const;
    bool add_bond(uint32_t i, uint32_t j);
    void remove_bond(uint32_t i, uint32_t j);
};
