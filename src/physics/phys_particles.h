#pragma once

#include "types.h"
#include "particles.h"
#include <glm/glm.hpp>
#include <cstdint>

// ── Sub-atomic particle type indices (within the shared MAX_PARTICLE_TYPES=36 space) ─

// Composites (existing)
static constexpr uint32_t PROTON_TYPE          = 0;
static constexpr uint32_t NEUTRON_TYPE         = 1;

// Leptons — generation 1
static constexpr uint32_t ELECTRON_TYPE_PHYS   = 2;
static constexpr uint32_t POSITRON_TYPE_PHYS   = 4;
static constexpr uint32_t NEUTRINO_TYPE_PHYS   = 6;   // νe

// Composites (antimatter)
static constexpr uint32_t ANTIPROTON_TYPE_PHYS = 5;

// Gauge bosons
static constexpr uint32_t PHOTON_TYPE_PHYS     = 3;

// Leptons — generation 2
static constexpr uint32_t MUON_TYPE_PHYS       = 7;   // μ⁻
static constexpr uint32_t ANTIMUON_TYPE_PHYS   = 8;   // μ⁺
static constexpr uint32_t MU_NEUTRINO_TYPE_PHYS = 11;  // νμ

// Leptons — generation 3
static constexpr uint32_t TAU_TYPE_PHYS        = 9;   // τ⁻
static constexpr uint32_t ANTITAU_TYPE_PHYS    = 10;  // τ⁺
static constexpr uint32_t TAU_NEUTRINO_TYPE_PHYS = 12; // ντ

// Quarks — matter
static constexpr uint32_t UP_QUARK_TYPE        = 13;
static constexpr uint32_t DOWN_QUARK_TYPE      = 14;
static constexpr uint32_t STRANGE_QUARK_TYPE   = 15;
static constexpr uint32_t CHARM_QUARK_TYPE     = 16;
static constexpr uint32_t TOP_QUARK_TYPE       = 17;
static constexpr uint32_t BOTTOM_QUARK_TYPE    = 18;

// Quarks — antimatter
static constexpr uint32_t ANTI_UP_TYPE         = 19;
static constexpr uint32_t ANTI_DOWN_TYPE       = 20;
static constexpr uint32_t ANTI_STRANGE_TYPE    = 21;
static constexpr uint32_t ANTI_CHARM_TYPE      = 22;
static constexpr uint32_t ANTI_TOP_TYPE        = 23;
static constexpr uint32_t ANTI_BOTTOM_TYPE     = 24;

// Gauge bosons — strong + weak + scalar
static constexpr uint32_t GLUON_TYPE_PHYS      = 25;
static constexpr uint32_t W_PLUS_TYPE_PHYS     = 26;
static constexpr uint32_t W_MINUS_TYPE_PHYS    = 27;
static constexpr uint32_t Z_BOSON_TYPE_PHYS    = 28;
static constexpr uint32_t HIGGS_TYPE_PHYS      = 29;

// Beyond Standard Model — hypothetical
static constexpr uint32_t GRAVITON_TYPE_PHYS     = 30;  // spin-2 massless gravitational mediator
static constexpr uint32_t DARK_MATTER_TYPE_PHYS  = 31;  // WIMP — gravity + weak only
static constexpr uint32_t DARK_ENERGY_TYPE_PHYS  = 32;  // quintessence — repulsive field quantum

static constexpr uint32_t PHYS_PARTICLE_TYPES  = 33;

// ── Environment presets ──────────────────────────────────────────────────────

static constexpr int PHYS_ENV_COUNT = 12;
static const char* const PHYS_ENV_NAMES[PHYS_ENV_COUNT] = {
    "Lab Mode",
    "Hydrogen Plasma",
    "Neutron Star Surface",
    "Solar Core",
    "Particle Soup",
    "Alpha Emitter",
    "Heavy Nucleus",
    "Quark-Gluon Plasma",
    "Electroweak Era",
    "Meson Factory",
    "Particle Accelerator",
    "Dark Sector"
};

// ── Per-type physics data ────────────────────────────────────────────────────

// Electric charge
static constexpr float PHYS_CHARGE[PHYS_PARTICLE_TYPES] = {
    // 0:p    1:n    2:e-   3:γ    4:e+   5:p̄    6:νe
     1.0f,  0.0f, -1.0f,  0.0f,  1.0f, -1.0f,  0.0f,
    // 7:μ-   8:μ+   9:τ-  10:τ+  11:νμ  12:ντ
    -1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f,
    // 13:u  14:d   15:s   16:c   17:t   18:b
     0.667f, -0.333f, -0.333f,  0.667f,  0.667f, -0.333f,
    // 19:ū  20:d̄  21:s̄  22:c̄  23:t̄  24:b̄
    -0.667f,  0.333f,  0.333f, -0.667f, -0.667f,  0.333f,
    // 25:g  26:W+  27:W-  28:Z   29:H
     0.0f,  1.0f, -1.0f,  0.0f,  0.0f,
    // 30:graviton  31:DM  32:DE
     0.0f,  0.0f,  0.0f,
};

// Spin quantum number
static constexpr float PHYS_SPIN[PHYS_PARTICLE_TYPES] = {
    // 0-6: composites + gen1 leptons + photon (all fermions ±0.5, photon ±1)
     0.5f,  0.5f,  0.5f,  1.0f,  0.5f,  0.5f,  0.5f,
    // 7-12: gen2+3 leptons
     0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
    // 13-18: quarks
     0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
    // 19-24: antiquarks
     0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
    // 25-29: bosons (gluon=1, W=1, Z=1, Higgs=0)
     1.0f,  1.0f,  1.0f,  1.0f,  0.0f,
    // 30:graviton(spin-2)  31:DM(WIMP fermion)  32:DE(scalar)
     2.0f,  0.5f,  0.0f,
};

// Decay rate (energy drain per dt, 0 = stable)
static constexpr float PHYS_DECAY_RATE[PHYS_PARTICLE_TYPES] = {
    // 0-6: stable particles
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    // 7:μ   8:μ+   9:τ   10:τ+  11:νμ  12:ντ
    0.01f, 0.01f, 0.20f, 0.20f, 0.0f, 0.0f,
    // 13:u  14:d   15:s   16:c   17:t   18:b  (u,d stable; others decay)
    0.0f, 0.0f, 0.02f, 0.12f, 0.50f, 0.10f,
    // 19:ū  20:d̄  21:s̄  22:c̄  23:t̄  24:b̄
    0.0f, 0.0f, 0.02f, 0.12f, 0.50f, 0.10f,
    // 25:g  26:W+  27:W-  28:Z   29:H
    0.0f, 0.50f, 0.50f, 0.50f, 0.40f,
    // 30:graviton  31:DM  32:DE  (all stable)
    0.0f, 0.0f, 0.0f,
};

// Populate a Particles object for the sub-atomic physics simulation.
void physics_gen_data(Particles& p, const SimConfig& cfg);
