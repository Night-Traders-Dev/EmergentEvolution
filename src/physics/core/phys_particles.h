#pragma once

#include "types.h"
#include "particles.h"
#include "meson_data.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

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

// ── Dark Matter Candidates ──────────────────────────────────────────────────
static constexpr uint32_t AXINO_TYPE_PHYS              = 33;  // SUSY partner of axion
static constexpr uint32_t WIMPZILLA_TYPE_PHYS          = 34;  // super-heavy ~10^12 GeV DM
static constexpr uint32_t SIMP_TYPE_PHYS               = 35;  // strongly interacting massive particle
static constexpr uint32_t STERILE_NEUTRINO_TYPE_PHYS   = 36;  // right-handed neutrino
static constexpr uint32_t DARK_PHOTON_TYPE_PHYS        = 37;  // dark electromagnetism carrier A'
static constexpr uint32_t QBALL_TYPE_PHYS              = 38;  // stable field-energy soliton

// ── Supersymmetric Sparticles ───────────────────────────────────────────────
static constexpr uint32_t SELECTRON_TYPE_PHYS          = 39;  // scalar partner of electron
static constexpr uint32_t SMUON_TYPE_PHYS              = 40;  // scalar partner of muon
static constexpr uint32_t STAU_TYPE_PHYS               = 41;  // scalar partner of tau
static constexpr uint32_t SQUARK_TYPE_PHYS             = 42;  // scalar partner of quark
static constexpr uint32_t GLUINO_TYPE_PHYS             = 43;  // fermionic partner of gluon
static constexpr uint32_t PHOTINO_TYPE_PHYS            = 44;  // fermionic partner of photon
static constexpr uint32_t WINO_TYPE_PHYS               = 45;  // fermionic partner of W
static constexpr uint32_t ZINO_TYPE_PHYS               = 46;  // fermionic partner of Z
static constexpr uint32_t HIGGSINO_TYPE_PHYS           = 47;  // fermionic partner of Higgs
static constexpr uint32_t NEUTRALINO_TYPE_PHYS         = 48;  // lightest SUSY particle (LSP)
static constexpr uint32_t SNEUTRINO_TYPE_PHYS          = 49;  // scalar partner of neutrino

// ── Force Carriers & Gravity ────────────────────────────────────────────────
static constexpr uint32_t GRAVITINO_TYPE_PHYS          = 50;  // SUSY partner of graviton
static constexpr uint32_t X_BOSON_TYPE_PHYS            = 51;  // GUT leptoquark (proton decay)
static constexpr uint32_t Y_BOSON_TYPE_PHYS            = 52;  // GUT leptoquark (proton decay)
static constexpr uint32_t MONOPOLE_TYPE_PHYS           = 53;  // magnetic monopole
static constexpr uint32_t RADION_TYPE_PHYS             = 54;  // extra-dimension size modulus
static constexpr uint32_t DILATON_TYPE_PHYS            = 55;  // string theory scale field

// ── Theoretical Extremes ────────────────────────────────────────────────────
static constexpr uint32_t TACHYON_TYPE_PHYS            = 56;  // imaginary-mass superluminal
static constexpr uint32_t PREON_TYPE_PHYS              = 57;  // sub-quark constituent
static constexpr uint32_t INFLATON_TYPE_PHYS           = 58;  // cosmic inflation driver
static constexpr uint32_t MAJORON_TYPE_PHYS            = 59;  // neutrino mass Goldstone
static constexpr uint32_t ODDERON_TYPE_PHYS            = 60;  // odd-gluon bound state
static constexpr uint32_t GLUEBALL_TYPE_PHYS           = 61;  // pure-glue bound state
static constexpr uint32_t SKYRMION_TYPE_PHYS           = 62;  // topological soliton baryon
static constexpr uint32_t X17_TYPE_PHYS                = 63;  // fifth-force light boson
static constexpr uint32_t CHAMELEON_TYPE_PHYS          = 64;  // environment-dependent mass scalar

// ── New Class (2025-2026) ───────────────────────────────────────────────────
static constexpr uint32_t PARAPARTICLE_TYPE_PHYS       = 65;  // neither boson nor fermion
static constexpr uint32_t DYN_AXION_QP_TYPE_PHYS       = 66;  // dynamical axion quasiparticle

// ── Quasiparticles ────────────────────────────────────────────────────────
static constexpr uint32_t ELECTRON_HOLE_TYPE_PHYS      = 67;  // electron hole quasiparticle
static constexpr uint32_t PLASMON_TYPE_PHYS            = 68;  // collective electron density oscillation
static constexpr uint32_t PHONON_TYPE_PHYS             = 69;  // quantized lattice vibration
static constexpr uint32_t MAGNON_TYPE_PHYS             = 70;  // quantized spin wave
static constexpr uint32_t POLARON_TYPE_PHYS            = 71;  // electron + lattice polarization cloud
static constexpr uint32_t COOPER_PAIR_TYPE_PHYS        = 72;  // paired neutrons (superfluid)
static constexpr uint32_t ROTON_TYPE_PHYS              = 73;  // quantized vortex excitation

static constexpr uint32_t PHYS_PARTICLE_TYPES  = 282;

// ── Environment presets ──────────────────────────────────────────────────────

static constexpr int PHYS_ENV_COUNT = 14;
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
    "Dark Sector",
    "SUSY Sector",
    "Big Bang"
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
    // 33:axino 34:WIMPzilla 35:SIMP 36:sterile_nu 37:dark_photon 38:Q-ball
     0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  1.0f,
    // 39:selectron 40:smuon 41:stau 42:squark 43:gluino
    -1.0f, -1.0f, -1.0f,  0.667f,  0.0f,
    // 44:photino 45:wino 46:zino 47:higgsino 48:neutralino 49:sneutrino
     0.0f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    // 50:gravitino 51:X_boson 52:Y_boson 53:monopole 54:radion 55:dilaton
     0.0f,  1.333f,  0.333f,  0.0f,  0.0f,  0.0f,
    // 56:tachyon 57:preon 58:inflaton 59:majoron 60:odderon 61:glueball
     0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    // 62:skyrmion 63:X17 64:chameleon
     1.0f,  0.0f,  0.0f,
    // 65:paraparticle 66:dyn_axion_qp
     0.0f,  0.0f,
    // 67:electron_hole
     1.0f,
    // 68-73: quasiparticles
     0.0f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    // ── Mesons (74-261) ─────────────────────────────────────────────────────
    // Light pseudoscalar (74-86)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f, -1.0f,
    // Light vector (87-100)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,
    // Light scalar (101-111)
     0.0f,  1.0f,  0.0f, -1.0f,  0.0f,
     0.0f,  1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    // Light axial-vector (112-124)
     1.0f,  0.0f, -1.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,
    // Light tensor (125-134)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    // Light pseudotensor (135-139)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    // Light higher spin (140-148)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,
    // Strange (149-174)
     1.0f,  0.0f, -1.0f,  0.0f,
     1.0f,  0.0f, -1.0f,  0.0f,
     1.0f,  0.0f,
     1.0f,  0.0f,  1.0f,  0.0f,
     1.0f,  0.0f,  1.0f,  0.0f,
     1.0f,  0.0f,  1.0f,  0.0f,
     1.0f,  0.0f,  1.0f,  0.0f,
    // Charmed (175-196)
     1.0f,  0.0f, -1.0f,  0.0f,  1.0f, -1.0f,
     0.0f,  1.0f, -1.0f,  0.0f,  1.0f, -1.0f,
     0.0f,  1.0f,  1.0f,
     0.0f,  1.0f,  1.0f,  1.0f,
     0.0f,  1.0f,  1.0f,
    // Bottom (197-216)
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f, -1.0f,
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
     0.0f,  1.0f,  0.0f,  1.0f,  0.0f,  0.0f,
    // Charmonium (217-234)
     0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    // Bottomonium (235-252)
     0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,
     0.0f,  0.0f,  0.0f,
    // Exotic (253-261)
     1.0f, -1.0f,  1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  1.0f, -1.0f,
    // ── Reserved (262-281) ──────────────────────────────────────────────────
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
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
    // 33:axino(1/2) 34:WIMPzilla(1/2) 35:SIMP(1/2) 36:sterile_nu(1/2) 37:dark_photon(1) 38:Q-ball(0)
     0.5f,  0.5f,  0.5f,  0.5f,  1.0f,  0.0f,
    // 39:selectron(0) 40:smuon(0) 41:stau(0) 42:squark(0) 43:gluino(1/2)
     0.0f,  0.0f,  0.0f,  0.0f,  0.5f,
    // 44:photino(1/2) 45:wino(1/2) 46:zino(1/2) 47:higgsino(1/2) 48:neutralino(1/2) 49:sneutrino(0)
     0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.0f,
    // 50:gravitino(3/2) 51:X(1) 52:Y(1) 53:monopole(0) 54:radion(0) 55:dilaton(0)
     1.5f,  1.0f,  1.0f,  0.0f,  0.0f,  0.0f,
    // 56:tachyon(0) 57:preon(1/2) 58:inflaton(0) 59:majoron(0) 60:odderon(3) 61:glueball(0)
     0.0f,  0.5f,  0.0f,  0.0f,  3.0f,  0.0f,
    // 62:skyrmion(1/2) 63:X17(1) 64:chameleon(0)
     0.5f,  1.0f,  0.0f,
    // 65:paraparticle(1/3) 66:dyn_axion_qp(0)
     0.33f,  0.0f,
    // 67:electron_hole(1/2)
     0.5f,
    // 68:plasmon(1) 69:phonon(0) 70:magnon(1) 71:polaron(1/2) 72:cooper_pair(0) 73:roton(0)
     1.0f,  0.0f,  1.0f,  0.5f,  0.0f,  0.0f,
    // ── Mesons (74-261) ─────────────────────────────────────────────────────
    // Light pseudoscalar J=0 (74-86)
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,
    // Light vector J=1 (87-100)
    1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,
    // Light scalar J=0 (101-111)
    0,0,0,0,0, 0,0,0,0,0, 0,
    // Light axial-vector J=1 (112-124)
    1,1,1,1,1, 1,1,1,1,1, 1,1,1,
    // Light tensor J=2 (125-134)
    2,2,2,2,2, 2,2,2,2,2,
    // Light pseudotensor J=2 (135-139)
    2,2,2,2,2,
    // Light higher spin J=3,4 (140-148)
    3,3,3,3,3,4,4,4,4,
    // Strange mesons (149-174)
    0,0,0,0, 1,1,1,1, 0,0,
    1,1,1,1, 1,1,0,0,
    2,2,1,1, 3,3,4,4,
    // Charmed (175-196)
    0,0,0,0,0,0, 1,1,1,1,1,1,
    0,0,0, 1,1,1,1, 2,2,2,
    // Bottom (197-216)
    0,0,0,0,0,0,0,0, 1,1,1,1,1,1,
    1,1,2,2,1,2,
    // Charmonium (217-234)
    0,1, 0,1,1,2, 0,1,1, 1,0,2, 1,1,1,1,1,1,
    // Bottomonium (235-252)
    0,1, 0,1,1,2, 0,1, 0,1,1,2, 1,1,2, 1,1,1,
    // Exotic (253-261)
    1,1,1,1,1,1,1,1,1,
    // ── Reserved (262-281) ──────────────────────────────────────────────────
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
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
    // 33:axino 34:WIMPzilla 35:SIMP 36:sterile_nu 37:dark_photon 38:Q-ball
    0.05f, 0.0f, 0.0f, 0.0f, 0.03f, 0.0f,
    // 39:selectron 40:smuon 41:stau 42:squark 43:gluino
    0.15f, 0.15f, 0.08f, 0.20f, 0.20f,
    // 44:photino 45:wino 46:zino 47:higgsino 48:neutralino 49:sneutrino
    0.10f, 0.15f, 0.15f, 0.12f, 0.0f, 0.12f,
    // 50:gravitino 51:X_boson 52:Y_boson 53:monopole 54:radion 55:dilaton
    0.0f, 0.50f, 0.50f, 0.0f, 0.10f, 0.03f,
    // 56:tachyon 57:preon 58:inflaton 59:majoron 60:odderon 61:glueball
    0.30f, 0.0f, 0.05f, 0.0f, 0.08f, 0.06f,
    // 62:skyrmion 63:X17 64:chameleon
    0.0f, 0.20f, 0.0f,
    // 65:paraparticle 66:dyn_axion_qp
    0.10f, 0.0f,
    // 67:electron_hole
    0.05f,
    // 68:plasmon 69:phonon 70:magnon 71:polaron 72:cooper_pair 73:roton
    0.08f, 0.04f, 0.03f, 0.02f, 0.005f, 0.01f,
    // ── Mesons (74-261) ─────────────────────────────────────────────────────
    // Light pseudoscalar (74-86)
    0.001f, 0.05f, 0.001f, 0.03f, 0.04f,
    0.20f, 0.20f, 0.20f, 0.15f, 0.15f, 0.25f, 0.25f, 0.25f,
    // Light vector (87-100)
    0.25f, 0.25f, 0.25f, 0.02f, 0.02f,
    0.20f, 0.20f, 0.20f, 0.15f, 0.15f,
    0.20f, 0.20f, 0.20f, 0.15f,
    // Light scalar (101-111)
    0.40f, 0.10f, 0.10f, 0.10f, 0.08f,
    0.20f, 0.15f, 0.15f, 0.15f, 0.10f, 0.10f,
    // Light axial-vector (112-124)
    0.10f, 0.10f, 0.10f, 0.20f,
    0.30f, 0.30f, 0.30f, 0.03f, 0.04f, 0.10f,
    0.20f, 0.20f, 0.20f,
    // Light tensor (125-134)
    0.08f, 0.08f, 0.08f, 0.08f, 0.04f,
    0.15f, 0.15f, 0.15f, 0.20f, 0.20f,
    // Light pseudotensor (135-139)
    0.10f, 0.10f, 0.10f, 0.10f, 0.10f,
    // Light higher spin (140-148)
    0.10f, 0.10f, 0.10f, 0.08f, 0.06f,
    0.10f, 0.10f, 0.10f, 0.08f,
    // Strange (149-174)
    0.001f, 0.005f, 0.001f, 0.005f,
    0.15f, 0.15f, 0.15f, 0.15f,
    0.35f, 0.35f,
    0.08f, 0.08f, 0.12f, 0.12f,
    0.15f, 0.15f, 0.18f, 0.18f,
    0.08f, 0.08f, 0.15f, 0.15f,
    0.10f, 0.10f, 0.10f, 0.10f,
    // Charmed (175-196)
    0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f,
    0.05f, 0.05f, 0.05f, 0.05f, 0.04f, 0.04f,
    0.20f, 0.20f, 0.05f,
    0.10f, 0.10f, 0.08f, 0.02f,
    0.08f, 0.08f, 0.03f,
    // Bottom (197-216)
    0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.001f, 0.002f, 0.002f,
    0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f,
    0.08f, 0.08f, 0.06f, 0.06f, 0.04f, 0.04f,
    // Charmonium (217-234)
    0.05f, 0.01f,
    0.05f, 0.03f, 0.04f, 0.04f,
    0.06f, 0.01f, 0.08f,
    0.005f, 0.08f, 0.06f,
    0.10f, 0.08f, 0.08f, 0.10f, 0.08f, 0.10f,
    // Bottomonium (235-252)
    0.05f, 0.01f,
    0.04f, 0.03f, 0.04f, 0.04f,
    0.05f, 0.01f,
    0.04f, 0.03f, 0.04f, 0.04f,
    0.01f, 0.04f, 0.04f,
    0.15f, 0.12f, 0.12f,
    // Exotic (253-261)
    0.10f, 0.10f, 0.08f, 0.08f, 0.15f, 0.15f, 0.06f, 0.08f, 0.08f,
    // ── Reserved (262-281) ──────────────────────────────────────────────────
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
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
    0.0f, 80369.2f, 80369.2f, 91187.6f, 125100.0f,
    // 30:graviton  31:DM(~100GeV WIMP)  32:DE(quintessence)
    0.0f, 100000.0f, 0.001f,
    // 33:axino(~100keV)  34:WIMPzilla(~10^12GeV)  35:SIMP(500MeV)
    0.1f, 1.0e12f, 500.0f,
    // 36:sterile_nu(~1keV) 37:dark_photon(17MeV) 38:Q-ball(~1TeV)
    0.001f, 17.0f, 1.0e6f,
    // 39:selectron(200GeV) 40:smuon(300GeV) 41:stau(150GeV) 42:squark(1.5TeV) 43:gluino(2TeV)
    200000.0f, 300000.0f, 150000.0f, 1.5e6f, 2.0e6f,
    // 44:photino(100GeV) 45:wino(300GeV) 46:zino(300GeV) 47:higgsino(200GeV)
    100000.0f, 300000.0f, 300000.0f, 200000.0f,
    // 48:neutralino(100GeV) 49:sneutrino(200GeV)
    100000.0f, 200000.0f,
    // 50:gravitino(~1keV) 51:X(~10^15GeV) 52:Y(~10^15GeV) 53:monopole(~10^16GeV)
    0.001f, 1.0e15f, 1.0e15f, 1.0e16f,
    // 54:radion(~1TeV) 55:dilaton(10GeV)
    1.0e6f, 10000.0f,
    // 56:tachyon(~1MeV imaginary) 57:preon(~10^17GeV) 58:inflaton(~10^13GeV) 59:majoron(~0.1eV)
    1.0f, 1.0e17f, 1.0e13f, 0.0001f,
    // 60:odderon(2.5GeV) 61:glueball(1.7GeV) 62:skyrmion(939MeV) 63:X17(17MeV) 64:chameleon(~0.01eV)
    2500.0f, 1700.0f, 939.0f, 17.0f, 0.00001f,
    // 65:paraparticle(500GeV) 66:dyn_axion_qp(~1meV)
    500000.0f, 0.001f,
    // 67:electron_hole(effective mass ~ electron)
    0.511f,
    // 68:plasmon 69:phonon 70:magnon 71:polaron 72:cooper_pair(2×neutron) 73:roton
    0.01f, 0.001f, 0.01f, 1.0f, 1879.13f, 0.1f,
    // ── Mesons (74-261) — PDG 2024 masses in MeV/c² ────────────────────────
    // Light pseudoscalar (74-86)
    139.570f, 134.977f, 139.570f, 547.862f, 957.780f,
    1300.0f, 1300.0f, 1300.0f, 1294.0f, 1476.0f, 1812.0f, 1812.0f, 1812.0f,
    // Light vector (87-100)
    775.26f, 775.26f, 775.26f, 782.66f, 1019.46f,
    1465.0f, 1465.0f, 1465.0f, 1410.0f, 1680.0f,
    1720.0f, 1720.0f, 1720.0f, 1670.0f,
    // Light scalar (101-111)
    475.0f, 980.0f, 980.0f, 980.0f, 990.0f,
    1350.0f, 1474.0f, 1474.0f, 1474.0f, 1506.0f, 1704.0f,
    // Light axial-vector (112-124)
    1229.5f, 1229.5f, 1229.5f, 1166.0f,
    1230.0f, 1230.0f, 1230.0f, 1281.9f, 1426.3f, 1416.0f,
    1655.0f, 1655.0f, 1655.0f,
    // Light tensor (125-134)
    1318.2f, 1318.2f, 1318.2f, 1275.5f, 1525.0f,
    1732.0f, 1732.0f, 1732.0f, 1936.0f, 2011.0f,
    // Light pseudotensor (135-139)
    1670.6f, 1670.6f, 1670.6f, 1617.0f, 1842.0f,
    // Light higher spin (140-148)
    1688.8f, 1688.8f, 1688.8f, 1667.0f, 1854.0f,
    1996.0f, 1996.0f, 1996.0f, 2018.0f,
    // Strange (149-174)
    493.677f, 497.611f, 493.677f, 497.611f,
    891.67f, 895.55f, 891.67f, 895.55f,
    845.0f, 845.0f,
    1253.0f, 1253.0f, 1403.0f, 1403.0f,
    1414.0f, 1414.0f, 1425.0f, 1425.0f,
    1425.6f, 1432.4f, 1718.0f, 1718.0f,
    1776.0f, 1776.0f, 2045.0f, 2045.0f,
    // Charmed (175-196)
    1869.66f, 1864.84f, 1869.66f, 1864.84f, 1968.35f, 1968.35f,
    2006.85f, 2010.26f, 2010.26f, 2006.85f, 2112.2f, 2112.2f,
    2343.0f, 2349.0f, 2317.8f,
    2420.8f, 2423.2f, 2459.5f, 2535.1f,
    2461.1f, 2465.4f, 2569.1f,
    // Bottom (197-216)
    5279.34f, 5279.66f, 5279.34f, 5279.66f, 5366.88f, 5366.88f, 6274.9f, 6274.9f,
    5324.70f, 5324.65f, 5324.70f, 5324.65f, 5415.4f, 5415.4f,
    5726.0f, 5725.9f, 5739.5f, 5737.2f, 5828.7f, 5839.85f,
    // Charmonium (217-234)
    2983.9f, 3096.9f,
    3414.71f, 3510.67f, 3525.38f, 3556.17f,
    3637.5f, 3686.10f, 3773.7f,
    3871.65f, 3862.0f, 3922.5f,
    4039.0f, 4191.0f, 4222.5f, 4374.0f, 4421.0f, 4630.0f,
    // Bottomonium (235-252)
    9399.0f, 9460.30f,
    9859.44f, 9892.78f, 9899.3f, 9912.21f,
    9999.0f, 10023.26f,
    10232.5f, 10255.46f, 10259.8f, 10268.65f,
    10355.2f, 10513.42f, 10524.02f,
    10579.4f, 10885.2f, 11000.0f,
    // Exotic (253-261)
    3887.1f, 3887.1f, 4024.1f, 4024.1f, 4478.0f, 4478.0f, 4146.5f, 10607.2f, 10607.2f,
    // ── Reserved (262-281) ──────────────────────────────────────────────────
    0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
};

// ── CKM matrix |V_ij|² row-normalized for branching (PDG 2024) ──────────────
// Rows: u/c/t   Cols: d/s/b
static constexpr float CKM_BR[3][3] = {
    { 0.9490f, 0.0509f, 0.00002f },   // u: Vud²=0.9490 Vus²=0.0509 Vub²=0.00002
    { 0.0503f, 0.9481f, 0.0016f },    // c: Vcd²=0.0503 Vcs²=0.9481 Vcb²=0.0016
    { 0.00008f, 0.00163f, 0.99829f }, // t: Vtd²=0.00008 Vts²=0.00163 Vtb²=0.99829
};

// ── PMNS neutrino mixing parameters ─────────────────────────────────────────
static constexpr float PMNS_SIN2_2T12 = 0.846f;    // solar angle
static constexpr float PMNS_SIN2_2T23 = 0.999f;    // atmospheric angle
static constexpr float PMNS_SIN2_2T13 = 0.0856f;   // reactor angle
static constexpr float PMNS_DM2_21    = 7.53e-5f;   // solar Δm² (eV²)
static constexpr float PMNS_DM2_32    = 2.453e-3f;  // atmospheric Δm²
static constexpr float SIM_OSC_SCALE  = 0.01f;      // tunable oscillation scale

// ── Neutral meson mixing parameters (PDG 2024, simulation-scaled) ───────────
// Real Δm values span orders of magnitude; pre-scaled for visible oscillation
// periods (~100-500 frames), analogous to PMNS_DM2 * SIM_OSC_SCALE.
static constexpr float KAON_DM_SIM    = 0.03f;    // K⁰ oscillation rate (Δm_K scaled)
static constexpr float KAON_DG_RATIO  = 2.0f;     // KS vs KL lifetime ratio (ΔΓ/Γ_avg)
static constexpr float BD_DM_SIM      = 0.05f;    // B⁰ oscillation rate (Δm_d scaled)
static constexpr float BD_DG_RATIO    = 0.001f;   // negligible width difference
static constexpr float BS_DM_SIM      = 0.10f;    // Bs⁰ oscillation rate (Δm_s scaled, fastest)
static constexpr float BS_DG_RATIO    = 0.13f;    // modest width difference

// ── CP violation parameters ─────────────────────────────────────────────────
// Exaggerated for simulation visibility (real values in comments)
static constexpr float CP_EPSILON       = 2.228e-3f; // |ε| indirect CP violation (K system)
static constexpr float CP_EPSILON_PRIME = 0.05f;     // direct CP asymmetry (real: 1.66e-3)
static constexpr float CP_SIN2BETA      = 0.699f;    // sin(2β) — B⁰ → J/ψ K (PDG)
static constexpr float CP_SIN2BETAS     = 0.036f;    // sin(2βs) — Bs⁰ → J/ψ φ (PDG)

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

// Full element name lookup (Z=0..118)
inline const char* element_name(int Z) {
    static const char* const NAMES[] = {
        "?",
        "Hydrogen","Helium","Lithium","Beryllium","Boron",
        "Carbon","Nitrogen","Oxygen","Fluorine","Neon",
        "Sodium","Magnesium","Aluminium","Silicon","Phosphorus",
        "Sulfur","Chlorine","Argon","Potassium","Calcium",
        "Scandium","Titanium","Vanadium","Chromium","Manganese",
        "Iron","Cobalt","Nickel","Copper","Zinc",
        "Gallium","Germanium","Arsenic","Selenium","Bromine",
        "Krypton","Rubidium","Strontium","Yttrium","Zirconium",
        "Niobium","Molybdenum","Technetium","Ruthenium","Rhodium",
        "Palladium","Silver","Cadmium","Indium","Tin",
        "Antimony","Tellurium","Iodine","Xenon","Cesium",
        "Barium","Lanthanum","Cerium","Praseodymium","Neodymium",
        "Promethium","Samarium","Europium","Gadolinium","Terbium",
        "Dysprosium","Holmium","Erbium","Thulium","Ytterbium",
        "Lutetium","Hafnium","Tantalum","Tungsten","Rhenium",
        "Osmium","Iridium","Platinum","Gold","Mercury",
        "Thallium","Lead","Bismuth","Polonium","Astatine",
        "Radon","Francium","Radium","Actinium","Thorium",
        "Protactinium","Uranium","Neptunium","Plutonium","Americium",
        "Curium","Berkelium","Californium","Einsteinium","Fermium",
        "Mendelevium","Nobelium","Lawrencium","Rutherfordium","Dubnium",
        "Seaborgium","Bohrium","Hassium","Meitnerium","Darmstadtium",
        "Roentgenium","Copernicium","Nihonium","Flerovium","Moscovium",
        "Livermorium","Tennessine","Oganesson",
    };
    if (Z < 0 || Z >= static_cast<int>(sizeof(NAMES)/sizeof(NAMES[0]))) return "?";
    return NAMES[Z];
}

// Dynamic molecule naming for binary compounds
// components: vector of (Z, count) pairs
inline std::string name_molecule_dynamic(const std::vector<std::pair<int,int>>& components) {
    if (components.empty()) return "";

    auto is_metal = [](int Z) -> bool {
        if (Z==3||Z==11||Z==19||Z==37||Z==55||Z==87) return true;
        if (Z==4||Z==12||Z==20||Z==38||Z==56||Z==88) return true;
        if ((Z>=21&&Z<=30)||(Z>=39&&Z<=48)||(Z>=72&&Z<=80)||(Z>=104&&Z<=112)) return true;
        if (Z==13||Z==31||Z==49||Z==50||Z==81||Z==82||Z==83||Z==84) return true;
        if (Z>=57&&Z<=71) return true;
        if (Z>=89&&Z<=103) return true;
        return false;
    };

    auto ide_name = [](int Z) -> const char* {
        switch (Z) {
            case 1: return "Hydride";   case 5: return "Boride";
            case 6: return "Carbide";   case 7: return "Nitride";
            case 8: return "Oxide";     case 9: return "Fluoride";
            case 14:return "Silicide";  case 15:return "Phosphide";
            case 16:return "Sulfide";   case 17:return "Chloride";
            case 33:return "Arsenide";  case 34:return "Selenide";
            case 35:return "Bromide";   case 52:return "Telluride";
            case 53:return "Iodide";    case 85:return "Astatide";
            default: return nullptr;
        }
    };

    auto prefix = [](int n) -> const char* {
        switch (n) {
            case 1: return "Mono"; case 2: return "Di";    case 3: return "Tri";
            case 4: return "Tetra";case 5: return "Penta"; case 6: return "Hexa";
            case 7: return "Hepta";case 8: return "Octa";  case 9: return "Nona";
            case 10:return "Deca"; default: return nullptr;
        }
    };

    // Monatomic homonuclear
    if (components.size() == 1) {
        int Z = components[0].first, n = components[0].second;
        if (Z < 1 || Z > 118) return "";
        if (n == 1) return element_name(Z);
        const char* pfx = prefix(n);
        if (!pfx) return "";
        return std::string(pfx) + element_name(Z);
    }

    // Binary compounds
    if (components.size() == 2) {
        int Z1 = components[0].first, n1 = components[0].second;
        int Z2 = components[1].first, n2 = components[1].second;
        if (Z1 < 1 || Z1 > 118 || Z2 < 1 || Z2 > 118) return "";
        const char* ide = ide_name(Z2);
        if (!ide) return "";

        if (is_metal(Z1)) {
            std::string name = element_name(Z1);
            name += " ";
            if (n2 > 1) { const char* p = prefix(n2); if (p) name += p; }
            name += ide;
            return name;
        }

        std::string name;
        if (n1 > 1) { const char* p = prefix(n1); if (p) name += p; }
        name += element_name(Z1);
        name += " ";
        if (n2 > 1) { const char* p = prefix(n2); if (p) name += p; }
        else name += "Mono";
        name += ide;
        return name;
    }

    return "";
}

// Populate a Particles object for the sub-atomic physics simulation.
void physics_gen_data(Particles& p, const SimConfig& cfg);

// Apply colorblind-friendly color correction to particle colors.
// mode: 0=off, 1=protanopia, 2=deuteranopia, 3=tritanopia
void apply_colorblind_correction(Particles& p, int mode);
