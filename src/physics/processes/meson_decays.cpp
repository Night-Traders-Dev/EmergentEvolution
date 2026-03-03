// meson_decays.cpp — Meson decay system with PDG branching ratios
// Scans active mesons each tick, rolls decay probabilities, spawns products.

#include "physics/core/simulation.h"
#include "physics/ui/interface.h"
#include "physics/core/phys_particles.h"
#include "physics/data/meson_data.h"
#include "physics/core/sim_helpers.h"
#include "physics/ui/ui_data.h"
#include <random>
#include <cstring>

// ── Decay mode table ─────────────────────────────────────────────────────────

struct MesonDecayMode {
    float branching_ratio;        // 0–1
    uint32_t products[4];         // particle type IDs
    uint8_t  product_count;       // 2–4
};

struct MesonDecayEntry {
    uint32_t meson_type;
    MesonDecayMode modes[4];      // up to 4 channels per meson
    uint8_t mode_count;
};

// ── PDG-based decay channels ─────────────────────────────────────────────────
// Product types use phys_particles.h constants.

static const MesonDecayEntry MESON_DECAY_TABLE[] = {
    // ═══ Light Pseudoscalar ═══
    // π⁺ → μ⁺ + νμ (99.99%)
    { PION_PLUS_MESON, {
        { 0.9999f, { ANTIMUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },
    // π⁰ → γ + γ (98.8%)
    { PION_ZERO_MESON, {
        { 0.988f, { PHOTON_TYPE_PHYS, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },
    // π⁻ → μ⁻ + ν̄μ (99.99%)
    { PION_MINUS_MESON, {
        { 0.9999f, { MUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },
    // η → γγ (39.4%), π⁺π⁻π⁰ (22.7%), 3π⁰ (32.6%)
    { ETA_MESON, {
        { 0.394f, { PHOTON_TYPE_PHYS, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.227f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.326f, { PION_ZERO_MESON, PION_ZERO_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 3 },
    // η' → π⁺π⁻η (42.6%), π⁰π⁰η (29.5%)
    { ETA_PRIME_MESON, {
        { 0.426f, { PION_PLUS_MESON, PION_MINUS_MESON, ETA_MESON, 0 }, 3 },
        { 0.295f, { PION_ZERO_MESON, PION_ZERO_MESON, ETA_MESON, 0 }, 3 },
    }, 2 },

    // ═══ Light Vector ═══
    // ρ⁺ → π⁺ + π⁰ (~100%)
    { RHO_770_PLUS, {
        { 1.0f, { PION_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 1 },
    // ρ⁰ → π⁺ + π⁻ (~100%)
    { RHO_770_ZERO, {
        { 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // ρ⁻ → π⁻ + π⁰ (~100%)
    { RHO_770_MINUS, {
        { 1.0f, { PION_MINUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 1 },
    // ω(782) → π⁺π⁻π⁰ (89.2%), π⁰γ (8.4%)
    { OMEGA_782_MESON, {
        { 0.892f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.084f, { PION_ZERO_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 2 },
    // φ(1020) → K⁺K⁻ (49.2%), K⁰K̄⁰ (34.0%), ρπ+π⁺π⁻π⁰ (15.3%)
    { PHI_1020_MESON, {
        { 0.492f, { KAON_PLUS_MESON, KAON_MINUS_MESON, 0, 0 }, 2 },
        { 0.340f, { KAON_ZERO_MESON, KAON_ZERO_BAR_MESON, 0, 0 }, 2 },
        { 0.153f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 3 },

    // ═══ Light Scalar ═══
    // f₀(500)/σ → π⁺π⁻ (~100%)
    { F0_500_MESON, {
        { 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // a₀(980)⁺ → ηπ⁺ (dominant)
    { A0_980_PLUS, {
        { 1.0f, { ETA_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // a₀(980)⁰ → ηπ⁰ (dominant)
    { A0_980_ZERO, {
        { 1.0f, { ETA_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 1 },
    // a₀(980)⁻ → ηπ⁻ (dominant)
    { A0_980_MINUS, {
        { 1.0f, { ETA_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // f₀(980) → ππ (dominant)
    { F0_980_MESON, {
        { 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },

    // ═══ Light Tensor ═══
    // a₂(1320)⁺ → ρ⁰π⁺ (dominant)
    { A2_1320_PLUS, {
        { 0.7f, { RHO_770_ZERO, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.3f, { ETA_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
    }, 2 },
    // f₂(1270) → ππ (84.2%)
    { F2_1270_MESON, {
        { 0.842f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.046f, { PHOTON_TYPE_PHYS, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 2 },

    // ═══ Strange Mesons ═══
    // K⁺ → μ⁺νμ (63.6%), π⁺π⁰ (20.7%), π⁺π⁺π⁻ (5.6%)
    { KAON_PLUS_MESON, {
        { 0.636f, { ANTIMUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
        { 0.207f, { PION_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
        { 0.056f, { PION_PLUS_MESON, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
    }, 3 },
    // K⁰ → π⁺π⁻ (69.2%), π⁰π⁰ (30.7%)  (treating as KS)
    { KAON_ZERO_MESON, {
        { 0.692f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.307f, { PION_ZERO_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // K⁻ → μ⁻ν̄μ (63.6%), π⁻π⁰ (20.7%)
    { KAON_MINUS_MESON, {
        { 0.636f, { MUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
        { 0.207f, { PION_MINUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // K̄⁰ → π⁺π⁻ (69.2%), π⁰π⁰ (30.7%)
    { KAON_ZERO_BAR_MESON, {
        { 0.692f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.307f, { PION_ZERO_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // K*(892)⁺ → K⁰π⁺ (~67%), K⁺π⁰ (~33%)
    { KSTAR_892_PLUS, {
        { 0.67f, { KAON_ZERO_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.33f, { KAON_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // K*(892)⁰ → K⁺π⁻ (~67%), K⁰π⁰ (~33%)
    { KSTAR_892_ZERO, {
        { 0.67f, { KAON_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.33f, { KAON_ZERO_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // K*(892)⁻ → K⁰π⁻ (~67%), K⁻π⁰ (~33%)
    { KSTAR_892_MINUS, {
        { 0.67f, { KAON_ZERO_BAR_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.33f, { KAON_MINUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },

    // ═══ Charmed Mesons ═══
    // D⁺ → K⁻π⁺π⁺ (~9.2%), K̄⁰π⁺ (~2.8%), μ⁺νμ (~0.04%)
    { D_PLUS_MESON, {
        { 0.5f, { KAON_MINUS_MESON, PION_PLUS_MESON, PION_PLUS_MESON, 0 }, 3 },
        { 0.3f, { KAON_ZERO_BAR_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.2f, { ANTIMUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // D⁰ → K⁻π⁺ (~3.9%), K⁻π⁺π⁺π⁻ (~8.1%)
    { D_ZERO_MESON, {
        { 0.5f, { KAON_MINUS_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.5f, { KAON_MINUS_MESON, PION_PLUS_MESON, PION_PLUS_MESON, PION_MINUS_MESON }, 4 },
    }, 2 },
    // D⁻ → K⁺π⁻π⁻ (charge conjugate of D⁺)
    { D_MINUS_MESON, {
        { 0.5f, { KAON_PLUS_MESON, PION_MINUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.3f, { KAON_ZERO_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.2f, { MUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // D̄⁰ → K⁺π⁻ (charge conjugate of D⁰)
    { D_ZERO_BAR_MESON, {
        { 1.0f, { KAON_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Ds⁺ → K⁺K̄⁰ + various
    { DS_PLUS_MESON, {
        { 0.5f, { KAON_PLUS_MESON, KAON_ZERO_BAR_MESON, 0, 0 }, 2 },
        { 0.3f, { PHI_1020_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.2f, { ANTIMUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // Ds⁻ (charge conjugate of Ds⁺)
    { DS_MINUS_MESON, {
        { 0.5f, { KAON_MINUS_MESON, KAON_ZERO_MESON, 0, 0 }, 2 },
        { 0.3f, { PHI_1020_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.2f, { MUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // D*(2010)⁺ → D⁰π⁺ (67.7%), D⁺π⁰ (30.7%)
    { DSTAR_2010_PLUS, {
        { 0.677f, { D_ZERO_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.307f, { D_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // D*(2007)⁰ → D⁰π⁰ (61.9%), D⁰γ (38.1%)
    { DSTAR_2007_ZERO, {
        { 0.619f, { D_ZERO_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
        { 0.381f, { D_ZERO_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 2 },

    // ═══ Bottom Mesons ═══
    // B⁺ → J/ψ K⁺ (~0.1%), simplified: D̄⁰ π⁺ etc.
    { B_PLUS_MESON, {
        { 0.4f, { D_ZERO_BAR_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.3f, { JPSI_MESON, KAON_PLUS_MESON, 0, 0 }, 2 },
        { 0.3f, { D_ZERO_BAR_MESON, PION_PLUS_MESON, PION_PLUS_MESON, PION_MINUS_MESON }, 4 },
    }, 3 },
    // B⁰ → D⁻π⁺, J/ψ K⁰
    { B_ZERO_MESON, {
        { 0.5f, { D_MINUS_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.3f, { JPSI_MESON, KAON_ZERO_MESON, 0, 0 }, 2 },
        { 0.2f, { D_MINUS_MESON, PION_PLUS_MESON, PION_PLUS_MESON, PION_MINUS_MESON }, 4 },
    }, 3 },
    // B⁻ (charge conjugate of B⁺)
    { B_MINUS_MESON, {
        { 0.4f, { D_ZERO_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.3f, { JPSI_MESON, KAON_MINUS_MESON, 0, 0 }, 2 },
        { 0.3f, { D_ZERO_MESON, PION_MINUS_MESON, PION_MINUS_MESON, PION_PLUS_MESON }, 4 },
    }, 3 },
    // Bs⁰ → Ds⁻π⁺, J/ψ φ
    { BS_ZERO_MESON, {
        { 0.6f, { DS_MINUS_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.4f, { JPSI_MESON, PHI_1020_MESON, 0, 0 }, 2 },
    }, 2 },
    // B̄⁰ → D⁺π⁻, J/ψ K̄⁰, D⁺π⁻π⁻π⁺ (charge conjugate of B⁰)
    { B_ZERO_BAR_MESON, {
        { 0.5f, { D_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.3f, { JPSI_MESON, KAON_ZERO_BAR_MESON, 0, 0 }, 2 },
        { 0.2f, { D_PLUS_MESON, PION_MINUS_MESON, PION_MINUS_MESON, PION_PLUS_MESON }, 4 },
    }, 3 },
    // B̄s⁰ → Ds⁺π⁻, J/ψ φ (charge conjugate of Bs⁰)
    { BS_ZERO_BAR_MESON, {
        { 0.6f, { DS_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
        { 0.4f, { JPSI_MESON, PHI_1020_MESON, 0, 0 }, 2 },
    }, 2 },
    // Bc⁺ → J/ψ π⁺, J/ψ μ⁺νμ
    { BC_PLUS_MESON, {
        { 0.5f, { JPSI_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
        { 0.5f, { JPSI_MESON, ANTIMUON_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, 0 }, 3 },
    }, 2 },
    // B*⁺ → B⁺γ (~100%)
    { BSTAR_PLUS, {
        { 1.0f, { B_PLUS_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },
    // B*⁰ → B⁰γ (~100%)
    { BSTAR_ZERO, {
        { 1.0f, { B_ZERO_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },

    // ═══ Charmonium (cc̄) ═══
    // ηc(1S) → hadrons (dominant), γγ (rare) — simplified to KK̄π
    { ETA_C_1S, {
        { 0.5f, { KAON_PLUS_MESON, KAON_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.3f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.2f, { PHOTON_TYPE_PHYS, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // J/ψ → e⁺e⁻ (5.97%), μ⁺μ⁻ (5.96%), hadrons (88%)
    { JPSI_MESON, {
        { 0.0597f, { POSITRON_TYPE_PHYS, ELECTRON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.0596f, { ANTIMUON_TYPE_PHYS, MUON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.44f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.44f, { KAON_PLUS_MESON, KAON_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 4 },
    // χc0(1P) → π⁺π⁻ (dominant)
    { CHI_C0_1P, {
        { 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // χc1(1P) → J/ψ γ (33.9%)
    { CHI_C1_1P, {
        { 0.339f, { JPSI_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.661f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 2 },
    // χc2(1P) → J/ψ γ (19.0%)
    { CHI_C2_1P, {
        { 0.190f, { JPSI_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.810f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 2 },
    // ψ(2S) → J/ψ π⁺π⁻ (34.5%), J/ψ π⁰π⁰ (18.0%), e⁺e⁻ (0.79%)
    { PSI_2S, {
        { 0.345f, { JPSI_MESON, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.180f, { JPSI_MESON, PION_ZERO_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.0079f, { POSITRON_TYPE_PHYS, ELECTRON_TYPE_PHYS, 0, 0 }, 2 },
    }, 3 },
    // ψ(3770) → D⁺D⁻ (~52%), D⁰D̄⁰ (~48%)
    { PSI_3770, {
        { 0.52f, { D_PLUS_MESON, D_MINUS_MESON, 0, 0 }, 2 },
        { 0.48f, { D_ZERO_MESON, D_ZERO_BAR_MESON, 0, 0 }, 2 },
    }, 2 },
    // X(3872) → J/ψ π⁺π⁻ (dominant observed)
    { X_3872, {
        { 0.5f, { JPSI_MESON, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.5f, { D_ZERO_MESON, D_ZERO_BAR_MESON, 0, 0 }, 2 },
    }, 2 },

    // ═══ Bottomonium (bb̄) ═══
    // ηb(1S) → hadrons
    { ETA_B_1S, {
        { 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 1 },
    // Υ(1S) → e⁺e⁻ (2.38%), μ⁺μ⁻ (2.48%), τ⁺τ⁻ (2.6%), hadrons (~93%)
    { UPSILON_1S, {
        { 0.0238f, { POSITRON_TYPE_PHYS, ELECTRON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.0248f, { ANTIMUON_TYPE_PHYS, MUON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.46f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
        { 0.49f, { KAON_PLUS_MESON, KAON_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 4 },
    // Υ(2S) → Υ(1S) π⁺π⁻ (17.9%)
    { UPSILON_2S, {
        { 0.179f, { UPSILON_1S, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.821f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 2 },
    // Υ(3S) → Υ(1S) π⁺π⁻ (~4.4%), Υ(2S) π⁺π⁻ (~2.1%)
    { UPSILON_3S, {
        { 0.044f, { UPSILON_1S, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.021f, { UPSILON_2S, PION_PLUS_MESON, PION_MINUS_MESON, 0 }, 3 },
        { 0.935f, { PION_PLUS_MESON, PION_MINUS_MESON, PION_ZERO_MESON, 0 }, 3 },
    }, 3 },
    // Υ(4S) → B⁺B⁻ (~51.4%), B⁰B̄⁰ (~48.6%) — the B factory resonance
    { UPSILON_4S, {
        { 0.514f, { B_PLUS_MESON, B_MINUS_MESON, 0, 0 }, 2 },
        { 0.486f, { B_ZERO_MESON, B_ZERO_BAR_MESON, 0, 0 }, 2 },
    }, 2 },

    // ═══ Exotic candidates ═══
    // Zc(3900)⁺ → J/ψ π⁺
    { ZC_3900_PLUS, {
        { 1.0f, { JPSI_MESON, PION_PLUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Zc(3900)⁻ → J/ψ π⁻
    { ZC_3900_MINUS, {
        { 1.0f, { JPSI_MESON, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Z(4430)⁺ → ψ(2S) π⁺
    { Z_4430_PLUS, {
        { 1.0f, { PSI_2S, PION_PLUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Z(4430)⁻ → ψ(2S) π⁻
    { Z_4430_MINUS, {
        { 1.0f, { PSI_2S, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Zb(10610)⁺ → Υ(1S)π⁺
    { ZB_10610_PLUS, {
        { 1.0f, { UPSILON_1S, PION_PLUS_MESON, 0, 0 }, 2 },
    }, 1 },
    // Zb(10610)⁻ → Υ(1S)π⁻
    { ZB_10610_MINUS, {
        { 1.0f, { UPSILON_1S, PION_MINUS_MESON, 0, 0 }, 2 },
    }, 1 },

    // ═══ Resonances with generic decay to lighter mesons ═══
    // ρ(1450) → ππ
    { RHO_1450_PLUS, {{ 1.0f, { PION_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 }}, 1 },
    { RHO_1450_ZERO, {{ 1.0f, { PION_PLUS_MESON, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    { RHO_1450_MINUS, {{ 1.0f, { PION_MINUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 }}, 1 },
    // Excited pions → ρπ
    { PION_1300_PLUS, {{ 1.0f, { RHO_770_ZERO, PION_PLUS_MESON, 0, 0 }, 2 }}, 1 },
    { PION_1300_ZERO, {{ 1.0f, { RHO_770_PLUS, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    { PION_1300_MINUS, {{ 1.0f, { RHO_770_ZERO, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    // b₁(1235) → ωπ
    { B1_1235_PLUS, {{ 1.0f, { OMEGA_782_MESON, PION_PLUS_MESON, 0, 0 }, 2 }}, 1 },
    { B1_1235_ZERO, {{ 1.0f, { OMEGA_782_MESON, PION_ZERO_MESON, 0, 0 }, 2 }}, 1 },
    { B1_1235_MINUS, {{ 1.0f, { OMEGA_782_MESON, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    // a₁(1260) → ρπ (dominant)
    { A1_1260_PLUS, {{ 1.0f, { RHO_770_ZERO, PION_PLUS_MESON, 0, 0 }, 2 }}, 1 },
    { A1_1260_ZERO, {{ 1.0f, { RHO_770_PLUS, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    { A1_1260_MINUS, {{ 1.0f, { RHO_770_ZERO, PION_MINUS_MESON, 0, 0 }, 2 }}, 1 },
    // Ds*⁺ → Ds⁺ γ (~94%), Ds⁺ π⁰ (~6%)
    { DS_STAR_PLUS, {
        { 0.94f, { DS_PLUS_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
        { 0.06f, { DS_PLUS_MESON, PION_ZERO_MESON, 0, 0 }, 2 },
    }, 2 },
    // Ds*⁻ → Ds⁻ γ
    { DS_STAR_MINUS, {
        { 1.0f, { DS_MINUS_MESON, PHOTON_TYPE_PHYS, 0, 0 }, 2 },
    }, 1 },
};

static constexpr int MESON_DECAY_TABLE_SIZE =
    sizeof(MESON_DECAY_TABLE) / sizeof(MESON_DECAY_TABLE[0]);

// ── Build fast-lookup by type ────────────────────────────────────────────────

static const MesonDecayEntry* find_decay_entry(uint32_t meson_type) {
    // Linear scan — table is small (~60 entries). Could hash-map for perf.
    for (int i = 0; i < MESON_DECAY_TABLE_SIZE; ++i) {
        if (MESON_DECAY_TABLE[i].meson_type == meson_type)
            return &MESON_DECAY_TABLE[i];
    }
    return nullptr;
}

// ── check_meson_decays() ─────────────────────────────────────────────────────
// Called each tick from simulation.cpp.

void PhysicsSimulation::check_meson_decays() {
    uint32_t n = static_cast<uint32_t>(particles.types.size());
    if (n == 0) return;

    constexpr uint32_t MAX_DECAYS_PER_TICK = 8;
    uint32_t decay_count = 0;

    std::mt19937 rng(frame_counter_ * 2718281828u + 31415u);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    uint32_t _rs0 = random_start(n, frame_counter_, 0u);
    for (uint32_t _it = 0; _it < n && decay_count < MAX_DECAYS_PER_TICK; ++_it) {
        uint32_t i = (_rs0 + _it) % n;
        uint32_t t = particles.types[i];
        if (!is_meson_type(t)) continue;
        if (readback_energies_[i] < 0.01f) continue;

        // Get decay rate from genome[3]
        float rate = 0.0f;
        if (i * GENOME_SIZE + 3 < particles.genomes.size())
            rate = particles.genomes[i * GENOME_SIZE + 3];
        if (rate < 0.0001f) continue;

        // Roll for decay this tick
        if (dist01(rng) > rate) continue;

        // Find decay entry
        const MesonDecayEntry* entry = find_decay_entry(t);
        if (!entry) {
            // No explicit decay table — generic decay to π⁺π⁻ for unknown
            // Just deactivate (energy → 0)
            readback_energies_[i] = 0.0f;
            continue;
        }

        // CP asymmetry: shift branching ratio threshold for neutral mesons
        // Particle and antiparticle have slightly different decay rates,
        // producing a matter-antimatter asymmetry (direct CP violation).
        float cp_shift = 0.0f;
        if (cfg.cp_violation_enabled) {
            if (t == KAON_ZERO_MESON)     cp_shift = +CP_EPSILON_PRIME;
            if (t == KAON_ZERO_BAR_MESON) cp_shift = -CP_EPSILON_PRIME;
            if (t == B_ZERO_MESON)        cp_shift = +CP_SIN2BETA * 0.01f;
            if (t == B_ZERO_BAR_MESON)    cp_shift = -CP_SIN2BETA * 0.01f;
            if (t == BS_ZERO_MESON)       cp_shift = +CP_SIN2BETAS * 0.01f;
            if (t == BS_ZERO_BAR_MESON)   cp_shift = -CP_SIN2BETAS * 0.01f;
        }

        // Select decay mode by branching ratio (shifted by CP asymmetry)
        float roll = dist01(rng);
        float adjusted_roll = std::clamp(roll - cp_shift, 0.0f, 1.0f);
        float cumulative = 0.0f;
        const MesonDecayMode* selected = nullptr;
        for (uint8_t m = 0; m < entry->mode_count; ++m) {
            cumulative += entry->modes[m].branching_ratio;
            if (adjusted_roll < cumulative) {
                selected = &entry->modes[m];
                break;
            }
        }
        if (!selected) selected = &entry->modes[entry->mode_count - 1];

        // Parent position/velocity for product placement
        glm::vec2 parent_pos = readback_positions_[i];
        glm::vec2 parent_vel = readback_velocities_[i];
        float parent_energy = readback_energies_[i];

        // Deactivate parent
        readback_energies_[i] = 0.0f;

        // Spawn products
        float energy_per_product = parent_energy / std::max((float)selected->product_count, 1.0f);

        for (uint8_t p = 0; p < selected->product_count; ++p) {
            uint32_t product_type = selected->products[p];
            if (product_type == 0 && p > 0) break;  // unused slot

            // Spread products slightly
            float angle = (float)p * (6.28318f / selected->product_count) +
                          dist01(rng) * 0.5f;
            float spread = 3.0f + dist01(rng) * 2.0f;
            glm::vec2 offset(std::cos(angle) * spread, std::sin(angle) * spread);

            // Product velocity: inherit parent momentum + spread
            float speed_spread = 20.0f + dist01(rng) * 30.0f;
            glm::vec2 prod_vel = parent_vel * (0.5f / (float)selected->product_count) +
                                 glm::vec2(std::cos(angle), std::sin(angle)) * speed_spread;

            // Find an inactive slot to reuse
            uint32_t slot = UINT32_MAX;
            for (uint32_t s = 0; s < n; ++s) {
                if (readback_energies_[s] < 0.01f && s != i) {
                    slot = s;
                    break;
                }
            }
            if (slot == UINT32_MAX) break;  // no room

            // Set up the product particle
            particles.types[slot] = product_type;
            readback_positions_[slot] = parent_pos + offset;
            readback_velocities_[slot] = prod_vel;
            readback_energies_[slot] = energy_per_product;

            // Genome: charge, spin, color_charge, decay_rate
            if (slot * GENOME_SIZE + 3 < particles.genomes.size()) {
                particles.genomes[slot * GENOME_SIZE + 0] = PHYS_CHARGE[product_type];
                particles.genomes[slot * GENOME_SIZE + 1] = PHYS_SPIN[product_type];
                particles.genomes[slot * GENOME_SIZE + 2] = 0.0f;
                particles.genomes[slot * GENOME_SIZE + 3] = PHYS_DECAY_RATE[product_type];
            }

            // Reset birth frame
            if (slot < particles.birth_frames.size())
                particles.birth_frames[slot] = frame_counter_;
        }

        ++decay_count;

        // Log the event
        char msg[128];
        snprintf(msg, sizeof(msg), "%s decay", phys_type_name(t));
        char detail[256];
        snprintf(detail, sizeof(detail), "%s → ", phys_type_name(t));
        for (uint8_t p = 0; p < selected->product_count; ++p) {
            if (p > 0) strncat(detail, " + ", sizeof(detail) - strlen(detail) - 1);
            strncat(detail, phys_type_name(selected->products[p]),
                    sizeof(detail) - strlen(detail) - 1);
        }
        const auto& mc = MESON_RENDER_COLORS[t - MESON_TYPE_FIRST];
        ImVec4 col(mc.r, mc.g, mc.b, mc.a);
        iface.push_decay_event(msg, PhysicsInterface::DEVT_MESON_DECAY, col, std::string(detail));
        achievements.seen_meson_decay = true;
        achievements.total_meson_decays++;

        // CP violation tracking: count matter vs antimatter products
        if (cp_shift != 0.0f) {
            achievements.seen_cp_violation = true;
            achievements.total_cp_violations++;
            for (uint8_t p = 0; p < selected->product_count; ++p) {
                float q = PHYS_CHARGE[selected->products[p]];
                if (q > 0.01f)  achievements.matter_excess++;
                if (q < -0.01f) achievements.antimatter_excess++;
            }
            // Log CP violation event
            char cp_msg[128];
            snprintf(cp_msg, sizeof(cp_msg), "CP violation: %s (shift %+.4f)", phys_type_name(t), cp_shift);
            iface.push_decay_event(cp_msg, PhysicsInterface::DEVT_CP_VIOLATION,
                                   ImVec4(0.85f, 0.6f, 1.0f, 1.0f), std::string(detail));
        }
    }
}
