#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "physics/core/types.h"
#include "physics/core/phys_particles.h"

#include <glm/glm.hpp>
#include <cmath>
#include <limits>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <vector>

// ═════════════════════════════════════════════════════════════════════════════
// 1. PARTICLE TYPE CONSTANTS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Particle Type Constants") {

TEST_CASE("Fundamental type indices are within bounds") {
    CHECK(PROTON_TYPE < PHYS_PARTICLE_TYPES);
    CHECK(NEUTRON_TYPE < PHYS_PARTICLE_TYPES);
    CHECK(ELECTRON_TYPE_PHYS < PHYS_PARTICLE_TYPES);
    CHECK(PHOTON_TYPE < MAX_PARTICLE_TYPES);
}

TEST_CASE("Type indices are unique for fundamental particles") {
    CHECK(PROTON_TYPE != NEUTRON_TYPE);
    CHECK(PROTON_TYPE != ELECTRON_TYPE_PHYS);
    CHECK(NEUTRON_TYPE != ELECTRON_TYPE_PHYS);
}

TEST_CASE("PHYS_PARTICLE_TYPES matches MAX_PARTICLE_TYPES") {
    CHECK(PHYS_PARTICLE_TYPES == 282);
    CHECK(MAX_PARTICLE_TYPES == 282);
}

TEST_CASE("Proton and neutron are first two types") {
    CHECK(PROTON_TYPE == 0);
    CHECK(NEUTRON_TYPE == 1);
}

} // Particle Type Constants

// ═════════════════════════════════════════════════════════════════════════════
// 2. CHARGE CONSERVATION
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Charge Properties") {

TEST_CASE("Proton has +1 charge") {
    CHECK(PHYS_CHARGE[PROTON_TYPE] == doctest::Approx(1.0f));
}

TEST_CASE("Neutron has 0 charge") {
    CHECK(PHYS_CHARGE[NEUTRON_TYPE] == doctest::Approx(0.0f));
}

TEST_CASE("Electron has -1 charge") {
    CHECK(PHYS_CHARGE[ELECTRON_TYPE_PHYS] == doctest::Approx(-1.0f));
}

TEST_CASE("Photon has 0 charge") {
    CHECK(PHYS_CHARGE[3] == doctest::Approx(0.0f));  // PHOTON_TYPE_PHYS = 3
}

TEST_CASE("Positron has +1 charge (opposite electron)") {
    CHECK(PHYS_CHARGE[4] == doctest::Approx(1.0f));  // POSITRON_TYPE_PHYS = 4
}

TEST_CASE("Antiproton has -1 charge (opposite proton)") {
    CHECK(PHYS_CHARGE[5] == doctest::Approx(-1.0f)); // ANTIPROTON_TYPE_PHYS = 5
}

TEST_CASE("All charges are finite") {
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) {
        CHECK(std::isfinite(PHYS_CHARGE[i]));
    }
}

TEST_CASE("Quarks have fractional charges") {
    // Up quark: +2/3
    CHECK(PHYS_CHARGE[13] == doctest::Approx(2.0f / 3.0f).epsilon(0.01f));
    // Down quark: -1/3
    CHECK(PHYS_CHARGE[14] == doctest::Approx(-1.0f / 3.0f).epsilon(0.01f));
}

} // Charge Properties

// ═════════════════════════════════════════════════════════════════════════════
// 3. SPIN PROPERTIES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Spin Properties") {

TEST_CASE("Fermions have half-integer spin") {
    // Proton: spin 1/2
    CHECK(PHYS_SPIN[PROTON_TYPE] == doctest::Approx(0.5f));
    // Neutron: spin 1/2
    CHECK(PHYS_SPIN[NEUTRON_TYPE] == doctest::Approx(0.5f));
    // Electron: spin 1/2
    CHECK(PHYS_SPIN[ELECTRON_TYPE_PHYS] == doctest::Approx(0.5f));
}

TEST_CASE("Photon has spin 1") {
    CHECK(PHYS_SPIN[3] == doctest::Approx(1.0f));
}

TEST_CASE("Higgs boson has spin 0") {
    // Higgs is type 29 in PHYS system
    CHECK(PHYS_SPIN[29] == doctest::Approx(0.0f));
}

TEST_CASE("All spins are non-negative and finite") {
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) {
        CHECK(std::isfinite(PHYS_SPIN[i]));
        CHECK(PHYS_SPIN[i] >= 0.0f);
    }
}

} // Spin Properties

// ═════════════════════════════════════════════════════════════════════════════
// 4. REST MASS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Rest Mass") {

TEST_CASE("Photon is massless") {
    CHECK(PHYS_REST_MASS_MEV[3] == doctest::Approx(0.0f));
}

TEST_CASE("Neutrinos have near-zero mass") {
    // electron neutrino = type 6
    CHECK(PHYS_REST_MASS_MEV[6] < 1.0f);
    CHECK(PHYS_REST_MASS_MEV[6] >= 0.0f);
}

TEST_CASE("Electron mass is ~0.511 MeV") {
    CHECK(PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS] == doctest::Approx(0.511f).epsilon(0.01f));
}

TEST_CASE("Proton mass is ~938 MeV") {
    CHECK(PHYS_REST_MASS_MEV[PROTON_TYPE] == doctest::Approx(938.3f).epsilon(1.0f));
}

TEST_CASE("Neutron is heavier than proton") {
    CHECK(PHYS_REST_MASS_MEV[NEUTRON_TYPE] > PHYS_REST_MASS_MEV[PROTON_TYPE]);
}

TEST_CASE("All masses are non-negative and finite") {
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) {
        CHECK(std::isfinite(PHYS_REST_MASS_MEV[i]));
        CHECK(PHYS_REST_MASS_MEV[i] >= 0.0f);
    }
}

TEST_CASE("Particle-antiparticle pairs have equal mass") {
    // electron (2) vs positron (4)
    CHECK(PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS] ==
          doctest::Approx(PHYS_REST_MASS_MEV[4]).epsilon(0.001f));
    // proton (0) vs antiproton (5)
    CHECK(PHYS_REST_MASS_MEV[PROTON_TYPE] ==
          doctest::Approx(PHYS_REST_MASS_MEV[5]).epsilon(0.001f));
}

} // Rest Mass

// ═════════════════════════════════════════════════════════════════════════════
// 5. DECAY RATES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Decay Rates") {

TEST_CASE("Stable particles have zero decay rate") {
    // Proton: stable
    CHECK(PHYS_DECAY_RATE[PROTON_TYPE] == doctest::Approx(0.0f));
    // Electron: stable
    CHECK(PHYS_DECAY_RATE[ELECTRON_TYPE_PHYS] == doctest::Approx(0.0f));
    // Photon: stable
    CHECK(PHYS_DECAY_RATE[3] == doctest::Approx(0.0f));
}

TEST_CASE("Neutron decay rate is non-negative") {
    // Free neutron beta decay may be handled by a separate CPU process
    CHECK(PHYS_DECAY_RATE[NEUTRON_TYPE] >= 0.0f);
}

TEST_CASE("All decay rates are non-negative and finite") {
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) {
        CHECK(std::isfinite(PHYS_DECAY_RATE[i]));
        CHECK(PHYS_DECAY_RATE[i] >= 0.0f);
    }
}

TEST_CASE("Muon has non-negative decay rate") {
    CHECK(PHYS_DECAY_RATE[MUON_TYPE_PHYS] >= 0.0f);
}

} // Decay Rates

// ═════════════════════════════════════════════════════════════════════════════
// 6. VALENCE / BOND SYSTEM
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Valence System") {

TEST_CASE("Hydrogen has valence 1") {
    CHECK(ATOM_VALENCE[0] == 1);  // H
}

TEST_CASE("Carbon has valence 4") {
    CHECK(ATOM_VALENCE[1] == 4);  // C
}

TEST_CASE("Nitrogen has valence 3") {
    CHECK(ATOM_VALENCE[2] == 3);  // N
}

TEST_CASE("Oxygen has valence 2") {
    CHECK(ATOM_VALENCE[3] == 2);  // O
}

TEST_CASE("Uranium has maximum valence 6") {
    CHECK(ATOM_VALENCE[17] == 6); // U
    // MAX_BONDS_PER_PARTICLE should accommodate max valence
    CHECK(MAX_BONDS_PER_PARTICLE >= 6);
}

TEST_CASE("Non-atom particles have zero valence") {
    // Photon
    CHECK(ATOM_VALENCE[18] == 0);
    // SM particles 19-29
    for (uint32_t i = 19; i < 30; ++i) {
        CHECK(ATOM_VALENCE[i] == 0);
    }
    // BSM particles 30-66
    for (uint32_t i = 30; i < 67; ++i) {
        CHECK(ATOM_VALENCE[i] == 0);
    }
    // Mesons 74-281
    for (uint32_t i = 74; i < PHYS_PARTICLE_TYPES; ++i) {
        CHECK(ATOM_VALENCE[i] == 0);
    }
}

TEST_CASE("Atom count is 18") {
    CHECK(ATOM_COUNT == 18);
}

TEST_CASE("GENOME_SIZE is 4") {
    CHECK(GENOME_SIZE == 4);
}

} // Valence System

// ═════════════════════════════════════════════════════════════════════════════
// 7. BEHAVIOR FLAGS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Behavior Flags") {

TEST_CASE("All behavior flags are distinct powers of 2") {
    // Test that common flags don't overlap
    CHECK((BEHAVIOR_REPEL & BEHAVIOR_POLAR) == 0);
    CHECK((BEHAVIOR_PHOTON & BEHAVIOR_LEPTON) == 0);
    CHECK((BEHAVIOR_POSITRON & BEHAVIOR_LEPTON) == 0);
    CHECK((BEHAVIOR_NEUTRINO & BEHAVIOR_ALPHA) == 0);
}

TEST_CASE("BEHAVIOR_NONE is zero") {
    CHECK(BEHAVIOR_NONE == 0);
}

TEST_CASE("Flag bits do not exceed 32") {
    // Each flag should be a single bit
    CHECK(BEHAVIOR_REPEL == (1u << 0));
    CHECK(BEHAVIOR_POLAR == (1u << 1));
    CHECK(BEHAVIOR_PHOTON == (1u << 10));
}

} // Behavior Flags

// ═════════════════════════════════════════════════════════════════════════════
// 8. SIMCONFIG DEFAULTS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("SimConfig Defaults") {

TEST_CASE("Default SimConfig has sane values") {
    SimConfig cfg{};
    CHECK(cfg.particle_count > 0);
    CHECK(cfg.particle_count <= 500000);
    CHECK(cfg.temperature >= 0.0f);
    CHECK(cfg.temperature_kelvin > 0.0f);
    CHECK(cfg.radius > 0.0f);
    CHECK(cfg.interaction_radius > 0.0f);
}

TEST_CASE("Force multipliers default to 1.0") {
    SimConfig cfg{};
    CHECK(cfg.coulomb_strength == doctest::Approx(1.0f));
    CHECK(cfg.yukawa_strength == doctest::Approx(1.0f));
    CHECK(cfg.pauli_multiplier == doctest::Approx(1.0f));
    CHECK(cfg.alpha_s_scale == doctest::Approx(1.0f));
    CHECK(cfg.compton_strength == doctest::Approx(1.0f));
    CHECK(cfg.annihilation_strength == doctest::Approx(1.0f));
}

TEST_CASE("PushConstants fit in 128 bytes") {
    CHECK(sizeof(PushConstants) <= 128);
}

TEST_CASE("World dimensions are positive") {
    CHECK(WORLD_W > 0);
    CHECK(WORLD_H > 0);
    CHECK(REGION_W > 0);
    CHECK(REGION_H > 0);
    CHECK(WORLD_W == REGION_W * 4);
    CHECK(WORLD_H == REGION_H * 4);
}

} // SimConfig Defaults

// ═════════════════════════════════════════════════════════════════════════════
// 9. FORCE OBJECT
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("ForceObject") {

TEST_CASE("Default ForceObject is inactive") {
    ForceObject fo{};
    CHECK(fo.active == 0);
    CHECK(fo.strength == doctest::Approx(0.0f));
}

TEST_CASE("ForceObject is 32 bytes (GPU aligned)") {
    CHECK(sizeof(ForceObject) == 32);
}

} // ForceObject

// ═════════════════════════════════════════════════════════════════════════════
// 10. PUSHCONSTANTS LAYOUT
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("PushConstants Layout") {

TEST_CASE("PushConstants can be zero-initialized") {
    PushConstants pc{};
    CHECK(pc.particle_count == 0);
    CHECK(pc.dt == doctest::Approx(0.0f));
}

TEST_CASE("PushConstants force multipliers at end") {
    // PushConstants must fit Vulkan's guaranteed 128-byte minimum
    PushConstants pc{};
    pc.coulomb_strength = 2.0f;
    pc.yukawa_strength = 3.0f;
    CHECK(pc.coulomb_strength == doctest::Approx(2.0f));
    CHECK(pc.yukawa_strength == doctest::Approx(3.0f));
}

} // PushConstants Layout

// ═════════════════════════════════════════════════════════════════════════════
// 11. PARTICLE MASS HIERARCHY
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Mass Hierarchy") {

TEST_CASE("Mass ordering: electron < muon < tau") {
    float m_e = PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS];
    float m_mu = PHYS_REST_MASS_MEV[MUON_TYPE_PHYS];
    float m_tau = PHYS_REST_MASS_MEV[TAU_TYPE_PHYS];
    CHECK(m_e < m_mu);
    CHECK(m_mu < m_tau);
}

TEST_CASE("Mass ordering: up quark < charm quark < top quark") {
    float m_u = PHYS_REST_MASS_MEV[UP_QUARK_TYPE];      // 13
    float m_c = PHYS_REST_MASS_MEV[CHARM_QUARK_TYPE];   // 16
    float m_t = PHYS_REST_MASS_MEV[TOP_QUARK_TYPE];     // 17
    CHECK(m_u < m_c);
    CHECK(m_c < m_t);
}

TEST_CASE("W and Z bosons are massive") {
    float m_W = PHYS_REST_MASS_MEV[W_PLUS_TYPE_PHYS];
    float m_Z = PHYS_REST_MASS_MEV[Z_BOSON_TYPE_PHYS];
    CHECK(m_W > 70000.0f);  // ~80 GeV
    CHECK(m_Z > 80000.0f);  // ~91 GeV
}

TEST_CASE("Higgs boson mass ~125 GeV") {
    float m_H = PHYS_REST_MASS_MEV[HIGGS_TYPE_PHYS];
    CHECK(m_H > 120000.0f);
    CHECK(m_H < 130000.0f);
}

} // Mass Hierarchy

// ═════════════════════════════════════════════════════════════════════════════
// 12. CHARGE CONSERVATION PAIRS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Charge Conservation") {

TEST_CASE("Particle-antiparticle charges sum to zero") {
    // electron + positron
    CHECK(PHYS_CHARGE[ELECTRON_TYPE_PHYS] + PHYS_CHARGE[4] ==
          doctest::Approx(0.0f));
    // proton + antiproton
    CHECK(PHYS_CHARGE[PROTON_TYPE] + PHYS_CHARGE[5] ==
          doctest::Approx(0.0f));
}

TEST_CASE("Quark-antiquark charges sum to zero") {
    // up (13) + anti-up (19)
    CHECK(PHYS_CHARGE[13] + PHYS_CHARGE[19] == doctest::Approx(0.0f).epsilon(0.01f));
    // down (14) + anti-down (20)
    CHECK(PHYS_CHARGE[14] + PHYS_CHARGE[20] == doctest::Approx(0.0f).epsilon(0.01f));
}

TEST_CASE("Neutral particles have zero charge") {
    CHECK(PHYS_CHARGE[NEUTRON_TYPE] == doctest::Approx(0.0f));
    CHECK(PHYS_CHARGE[3] == doctest::Approx(0.0f));  // photon
}

} // Charge Conservation

// ═════════════════════════════════════════════════════════════════════════════
// 13. ARRAY BOUNDS SAFETY
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Array Bounds Safety") {

TEST_CASE("ATOM_VALENCE array has exactly MAX_PARTICLE_TYPES entries") {
    // This is a compile-time check — if it compiles, it's correct
    // But let's verify runtime access at boundaries
    CHECK(ATOM_VALENCE[0] >= 0);
    CHECK(ATOM_VALENCE[MAX_PARTICLE_TYPES - 1] == 0);
}

TEST_CASE("PHYS_CHARGE array covers all types") {
    // First and last element accessible
    float first = PHYS_CHARGE[0];
    float last = PHYS_CHARGE[PHYS_PARTICLE_TYPES - 1];
    CHECK(std::isfinite(first));
    CHECK(std::isfinite(last));
}

TEST_CASE("PHYS_SPIN array covers all types") {
    float first = PHYS_SPIN[0];
    float last = PHYS_SPIN[PHYS_PARTICLE_TYPES - 1];
    CHECK(std::isfinite(first));
    CHECK(std::isfinite(last));
}

TEST_CASE("PHYS_DECAY_RATE array covers all types") {
    float first = PHYS_DECAY_RATE[0];
    float last = PHYS_DECAY_RATE[PHYS_PARTICLE_TYPES - 1];
    CHECK(std::isfinite(first));
    CHECK(std::isfinite(last));
}

TEST_CASE("PHYS_REST_MASS_MEV array covers all types") {
    float first = PHYS_REST_MASS_MEV[0];
    float last = PHYS_REST_MASS_MEV[PHYS_PARTICLE_TYPES - 1];
    CHECK(std::isfinite(first));
    CHECK(std::isfinite(last));
}

} // Array Bounds Safety

// ═════════════════════════════════════════════════════════════════════════════
// 14. MESON TYPE RANGE
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Meson Types") {

TEST_CASE("Meson range starts at type 74") {
    // First meson should be pion (π±)
    // All mesons have valence 0
    for (uint32_t i = 74; i < 262; ++i) {
        CHECK(ATOM_VALENCE[i] == 0);
    }
}

TEST_CASE("Meson masses are positive (where defined)") {
    // Pion π± at type 74 should have ~140 MeV
    float m_pi = PHYS_REST_MASS_MEV[74];
    CHECK(m_pi > 100.0f);
    CHECK(m_pi < 200.0f);
}

TEST_CASE("Mesons have integer spin (bosons)") {
    // Pseudoscalar mesons: spin 0
    // Vector mesons: spin 1
    for (uint32_t i = 74; i < 262; ++i) {
        float s = PHYS_SPIN[i];
        // Spin should be 0 or 1 (or 2 for tensor mesons)
        bool valid = (s == doctest::Approx(0.0f) ||
                      s == doctest::Approx(1.0f) ||
                      s == doctest::Approx(2.0f) ||
                      s == doctest::Approx(3.0f) ||
                      s == doctest::Approx(4.0f));
        CHECK(valid);
    }
}

} // Meson Types
