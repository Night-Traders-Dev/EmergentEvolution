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

// ── Simulation constants ─────────────────────────────────────────────────────
static constexpr float C_SIM       = 300.0f;   // speed of light in sim units (px/frame)
static constexpr float E_SCALE_MEV = 1.0f;     // 1.0 energy buffer = 1 MeV

// PDG rest masses in MeV/c² (2024 Review of Particle Physics)
static constexpr float PHYS_REST_MASS_MEV[PHYS_PARTICLE_TYPES] = {
    // 0-6: stable particles
    938.272f, 939.565f, 0.511f, 0.0f, 0.511f, 938.272f, 0.0f,
    // 7:μ   8:μ+    9:τ      10:τ+     11:νμ  12:ντ
    105.658f, 105.658f, 1776.86f, 1776.86f, 0.0f, 0.0f,
    // 13:u    14:d    15:s    16:c      17:t        18:b
    2.16f, 4.67f, 93.4f, 1270.0f, 172760.0f, 4180.0f,
    // 19:ū   20:d̄   21:s̄   22:c̄     23:t̄       24:b̄
    2.16f, 4.67f, 93.4f, 1270.0f, 172760.0f, 4180.0f,
    // 25:g   26:W+      27:W-      28:Z       29:H
    0.0f, 80379.0f, 80379.0f, 91188.0f, 125100.0f,
    // 30:graviton  31:DM(~100GeV WIMP)  32:DE(quintessence)
    0.0f, 100000.0f, 0.001f,
};

// ── Nuclear isotope decay ────────────────────────────────────────────────────

enum NuclearDecayMode : uint8_t {
    NDECAY_NONE = 0,
    NDECAY_ALPHA,               // emit He-4 (2p + 2n)
    NDECAY_BETA_MINUS,          // n → p + e⁻ + ν̄e
    NDECAY_BETA_PLUS,           // p → n + e⁺ + νe
    NDECAY_NEUTRON_EMISSION,    // eject 1 neutron
    NDECAY_PROTON_EMISSION,     // eject 1 proton
    NDECAY_GAMMA,               // excited nucleus → ground state + γ
    NDECAY_ELECTRON_CAPTURE,    // p + e⁻ → n + νe (orbital electron captured)
    NDECAY_SPONTANEOUS_FISSION, // heavy nucleus → 2 fragments + neutrons
};

struct IsotopeDecayEntry {
    uint8_t Z;               // proton count
    uint8_t N;               // neutron count
    NuclearDecayMode mode;
    float half_life_frames;  // at 60 FPS
};

// Isotope decay table — half-lives compressed to sim-friendly timescales.
// Covers known unstable isotopes reachable in the simulation.
// Nuclei not found here fall through to general stability rules in check_nuclear_decay().
static constexpr IsotopeDecayEntry ISOTOPE_DECAY_TABLE[] = {
    // ── Free neutron ─────────────────────────────────────────────────────
    {0, 1, NDECAY_BETA_MINUS,       600.0f},   // n → p + e⁻ + ν̄e  (real: 10 min)

    // ── Hydrogen isotopes ────────────────────────────────────────────────
    {1, 2, NDECAY_BETA_MINUS,      3600.0f},   // H-3 tritium (real: 12.3 yr)

    // ── Helium isotopes ──────────────────────────────────────────────────
    {2, 1, NDECAY_PROTON_EMISSION,    6.0f},   // He-3 can p-emit if very neutron-poor — treat as unstable in sim
    {2, 3, NDECAY_NEUTRON_EMISSION,   6.0f},   // He-5 — instantly ejects neutron
    {2, 4, NDECAY_BETA_MINUS,        30.0f},   // He-6 (real: 807 ms)
    {2, 6, NDECAY_BETA_MINUS,        12.0f},   // He-8 (real: 119 ms)

    // ── Lithium isotopes ─────────────────────────────────────────────────
    {3, 2, NDECAY_PROTON_EMISSION,    6.0f},   // Li-5 — unbound, instant
    {3, 5, NDECAY_BETA_MINUS,        30.0f},   // Li-8 (real: 839 ms)
    {3, 6, NDECAY_NEUTRON_EMISSION,   6.0f},   // Li-9 (real: 178 ms)

    // ── Beryllium isotopes ───────────────────────────────────────────────
    {4, 3, NDECAY_ELECTRON_CAPTURE, 7200.0f},   // Be-7 electron capture (real: 53 d)
    {4, 4, NDECAY_ALPHA,              6.0f},    // Be-8 → 2 He-4 (instant)
    {4, 6, NDECAY_BETA_MINUS,        60.0f},   // Be-10 (real: 1.4 Myr → long-lived in sim)

    // ── Boron isotopes ───────────────────────────────────────────────────
    {5, 3, NDECAY_BETA_PLUS,         30.0f},   // B-8 (real: 770 ms)
    {5, 7, NDECAY_BETA_MINUS,        60.0f},   // B-12 (real: 20 ms — fast in reality)

    // ── Carbon isotopes ──────────────────────────────────────────────────
    {6, 4, NDECAY_BETA_PLUS,         30.0f},   // C-10 (real: 19 s)
    {6, 5, NDECAY_BETA_PLUS,        120.0f},   // C-11 (real: 20 min)
    {6, 8, NDECAY_BETA_MINUS,     18000.0f},   // C-14 carbon dating (real: 5730 yr)
    {6, 9, NDECAY_BETA_MINUS,        12.0f},   // C-15 (real: 2.4 s)

    // ── Nitrogen isotopes ────────────────────────────────────────────────
    {7, 6, NDECAY_BETA_PLUS,        600.0f},   // N-13 (real: 10 min)
    {7, 9, NDECAY_BETA_MINUS,        30.0f},   // N-16 (real: 7.1 s)

    // ── Oxygen isotopes ──────────────────────────────────────────────────
    {8, 6, NDECAY_BETA_PLUS,         30.0f},   // O-14 (real: 71 s)
    {8, 7, NDECAY_BETA_PLUS,        120.0f},   // O-15 (real: 122 s) PET isotope

    // ── Fluorine ─────────────────────────────────────────────────────────
    {9, 8, NDECAY_BETA_PLUS,        300.0f},   // F-17 (real: 64 s)
    {9, 9, NDECAY_BETA_PLUS,       1800.0f},   // F-18 (real: 110 min) PET isotope

    // ── Neon ─────────────────────────────────────────────────────────────
    {10, 9, NDECAY_BETA_PLUS,       600.0f},   // Ne-19 (real: 17 s)

    // ── Sodium ───────────────────────────────────────────────────────────
    {11, 11, NDECAY_BETA_MINUS,    3600.0f},   // Na-22 (real: 2.6 yr)
    {11, 13, NDECAY_BETA_MINUS,     300.0f},   // Na-24 (real: 15 hr)

    // ── Magnesium ────────────────────────────────────────────────────────
    {12, 15, NDECAY_BETA_MINUS,     300.0f},   // Mg-27 (real: 9.5 min)

    // ── Aluminium ────────────────────────────────────────────────────────
    {13, 13, NDECAY_BETA_PLUS,     3600.0f},   // Al-26 (real: 717 kyr)

    // ── Silicon ──────────────────────────────────────────────────────────
    {14, 18, NDECAY_BETA_MINUS,    7200.0f},   // Si-32 (real: 153 yr)

    // ── Phosphorus ───────────────────────────────────────────────────────
    {15, 17, NDECAY_BETA_MINUS,    1800.0f},   // P-32 (real: 14.3 d)
    {15, 18, NDECAY_BETA_MINUS,    3600.0f},   // P-33 (real: 25.3 d)

    // ── Sulfur ───────────────────────────────────────────────────────────
    {16, 19, NDECAY_BETA_MINUS,    7200.0f},   // S-35 (real: 87 d)

    // ── Chlorine ─────────────────────────────────────────────────────────
    {17, 19, NDECAY_BETA_PLUS,    18000.0f},   // Cl-36 (real: 301 kyr)

    // ── Potassium ────────────────────────────────────────────────────────
    {19, 21, NDECAY_BETA_MINUS,   18000.0f},   // K-40 (real: 1.25 Gyr)

    // ── Calcium ──────────────────────────────────────────────────────────
    {20, 25, NDECAY_BETA_MINUS,    1200.0f},   // Ca-45 (real: 163 d)

    // ── Iron ─────────────────────────────────────────────────────────────
    {26, 29, NDECAY_ELECTRON_CAPTURE, 3600.0f}, // Fe-55 EC (real: 2.7 yr)
    {26, 33, NDECAY_BETA_MINUS,    1800.0f},   // Fe-59 (real: 44.5 d)

    // ── Cobalt ───────────────────────────────────────────────────────────
    {27, 33, NDECAY_BETA_MINUS,    7200.0f},   // Co-60 (real: 5.3 yr)

    // ── Strontium ────────────────────────────────────────────────────────
    {38, 52, NDECAY_BETA_MINUS,    7200.0f},   // Sr-90 (real: 28.8 yr)

    // ── Iodine ───────────────────────────────────────────────────────────
    {53, 78, NDECAY_BETA_MINUS,    1200.0f},   // I-131 (real: 8 d)

    // ── Cesium ───────────────────────────────────────────────────────────
    {55, 82, NDECAY_BETA_MINUS,   18000.0f},   // Cs-137 (real: 30.2 yr)

    // ── Radon ────────────────────────────────────────────────────────────
    {86, 136, NDECAY_ALPHA,         300.0f},    // Rn-222 (real: 3.8 d)

    // ── Radium ───────────────────────────────────────────────────────────
    {88, 138, NDECAY_ALPHA,        7200.0f},    // Ra-226 (real: 1600 yr)

    // ── Thorium ──────────────────────────────────────────────────────────
    {90, 142, NDECAY_ALPHA,       18000.0f},    // Th-232 (real: 14 Gyr)

    // ── Uranium ──────────────────────────────────────────────────────────
    {92, 143, NDECAY_ALPHA,       12000.0f},    // U-235 (real: 704 Myr)
    {92, 146, NDECAY_ALPHA,       18000.0f},    // U-238 (real: 4.5 Gyr)

    // ── Plutonium ────────────────────────────────────────────────────────
    {94, 145, NDECAY_ALPHA,        7200.0f},    // Pu-239 (real: 24.1 kyr)
    {94, 146, NDECAY_SPONTANEOUS_FISSION, 18000.0f}, // Pu-240 SF (real: 6500 yr for SF branch)

    // ── Californium ─────────────────────────────────────────────────────
    {98, 154, NDECAY_SPONTANEOUS_FISSION, 3600.0f},  // Cf-252 (real: 2.6 yr, 3.1% SF branch)

    // ── Gamma isomeric transitions ──────────────────────────────────────
    // These represent metastable nuclear excited states that de-excite via γ emission.
    // In the sim, nuclei that just underwent beta decay can match these entries.
    {56, 81, NDECAY_GAMMA,          180.0f},    // Ba-137m (from Cs-137 β⁻, real: 2.55 min)
    {43, 56, NDECAY_GAMMA,         3600.0f},    // Tc-99m (real: 6.01 hr) — medical imaging
};
static constexpr int ISOTOPE_DECAY_TABLE_SIZE =
    static_cast<int>(sizeof(ISOTOPE_DECAY_TABLE) / sizeof(ISOTOPE_DECAY_TABLE[0]));

// Look up an isotope in the table. Returns nullptr if not found.
inline const IsotopeDecayEntry* lookup_isotope_decay(int Z, int N) {
    for (int i = 0; i < ISOTOPE_DECAY_TABLE_SIZE; ++i) {
        if (ISOTOPE_DECAY_TABLE[i].Z == Z && ISOTOPE_DECAY_TABLE[i].N == N)
            return &ISOTOPE_DECAY_TABLE[i];
    }
    return nullptr;
}

// General stability rules for nuclei not in the explicit table.
// Returns decay mode and estimated half-life. NDECAY_NONE = stable.
inline NuclearDecayMode general_stability_rule(int Z, int N, float& half_life_out) {
    int A = Z + N;
    if (Z == 0 && N == 0) return NDECAY_NONE;          // nothing
    if (Z == 0 && N == 1) { half_life_out = 600.0f; return NDECAY_BETA_MINUS; } // free neutron
    if (A == 1 && Z == 1) return NDECAY_NONE;           // lone proton = stable
    if (Z == 1 && N == 0) return NDECAY_NONE;           // hydrogen

    // Nuclear gaps: A=5 and A=8 are unbound
    if (A == 5) { half_life_out = 6.0f; return (N > Z) ? NDECAY_NEUTRON_EMISSION : NDECAY_PROTON_EMISSION; }
    if (A == 8 && Z == 4) { half_life_out = 6.0f; return NDECAY_ALPHA; }

    // Super-heavy elements (Z>95, A>240): spontaneous fission dominates
    if (Z > 95 && A > 240) { half_life_out = 600.0f + Z * 30.0f; return NDECAY_SPONTANEOUS_FISSION; }

    // All elements above bismuth (Z>83) alpha-decay
    if (Z > 83) { half_life_out = 1200.0f + Z * 60.0f; return NDECAY_ALPHA; }

    // Neutron-rich: N/Z > 1.5 (for Z>=2) → beta-minus
    if (Z >= 2 && N > 0) {
        float ratio = static_cast<float>(N) / static_cast<float>(Z);
        if (ratio > 1.5f) {
            half_life_out = 60.0f + ratio * 120.0f;
            return NDECAY_BETA_MINUS;
        }
        // Proton-rich: N/Z < 0.7 (for Z>=3) → beta-plus
        if (Z >= 3 && ratio < 0.7f) {
            half_life_out = 60.0f + (1.0f / ratio) * 120.0f;
            return NDECAY_BETA_PLUS;
        }
    }

    // Lone neutrons in a cluster without protons
    if (Z == 0 && N > 1) { half_life_out = 30.0f; return NDECAY_BETA_MINUS; }

    return NDECAY_NONE;  // stable
}

// Compact element symbol lookup for notifications (Z=0..118)
inline const char* element_symbol(int Z) {
    static const char* const SYM[] = {
        "n","H","He","Li","Be","B","C","N","O","F","Ne",
        "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
        "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
        "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
        "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
        "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd",
        "Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb",
        "Lu","Hf","Ta","W","Re","Os","Ir","Pt","Au","Pb",
        "Tl","Bi","Po","At","Rn","Fr","Ra","Ac","Th","Pa",
        "U","Np","Pu",
    };
    if (Z < 0 || Z >= static_cast<int>(sizeof(SYM)/sizeof(SYM[0]))) return "?";
    return SYM[Z];
}

// Populate a Particles object for the sub-atomic physics simulation.
void physics_gen_data(Particles& p, const SimConfig& cfg);
