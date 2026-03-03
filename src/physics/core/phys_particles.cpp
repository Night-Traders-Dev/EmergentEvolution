#include "physics/core/phys_particles.h"
#include <random>
#include <cmath>
#include <algorithm>

// ── Sub-atomic particle colors (67 types) ────────────────────────────────────
static const glm::vec4 PHYS_COLORS[PHYS_PARTICLE_TYPES] = {
    // 0: Composites + gen1
    { 0.9f, 0.2f, 0.2f, 1.0f },   // 0  Proton      — red
    { 0.7f, 0.7f, 0.7f, 1.0f },   // 1  Neutron     — grey
    { 0.2f, 0.5f, 1.0f, 1.0f },   // 2  Electron    — blue
    { 1.0f, 1.0f, 0.6f, 1.0f },   // 3  Photon      — yellow
    { 1.0f, 0.3f, 0.8f, 1.0f },   // 4  Positron    — magenta
    { 0.2f, 0.85f, 0.7f, 1.0f },  // 5  Antiproton  — teal
    { 0.6f, 0.9f, 0.6f, 1.0f },   // 6  Neutrino_e  — faint green
    // 7-12: Gen2+3 leptons
    { 0.45f, 0.2f, 0.85f, 1.0f }, // 7  Muon        — indigo
    { 0.75f, 0.5f, 1.0f, 1.0f },  // 8  Anti-muon   — lilac
    { 0.6f, 0.1f, 0.7f, 1.0f },   // 9  Tau         — violet
    { 0.85f, 0.5f, 0.9f, 1.0f },  // 10 Anti-tau    — pink-violet
    { 0.5f, 0.85f, 0.5f, 1.0f },  // 11 Neutrino_μ  — soft green
    { 0.4f, 0.8f, 0.4f, 1.0f },   // 12 Neutrino_τ  — pale green
    // 13-18: Quarks
    { 1.0f, 0.6f, 0.2f, 1.0f },   // 13 Up          — orange
    { 0.3f, 0.75f, 0.3f, 1.0f },  // 14 Down        — green
    { 0.8f, 0.85f, 0.2f, 1.0f },  // 15 Strange     — yellow-green
    { 1.0f, 0.8f, 0.1f, 1.0f },   // 16 Charm       — gold
    { 0.95f, 0.15f, 0.15f, 1.0f },// 17 Top         — crimson
    { 0.55f, 0.35f, 0.2f, 1.0f }, // 18 Bottom      — brown
    // 19-24: Antiquarks (pastel versions)
    { 1.0f, 0.8f, 0.6f, 1.0f },   // 19 Anti-up     — peach
    { 0.6f, 0.9f, 0.6f, 1.0f },   // 20 Anti-down   — mint
    { 0.9f, 0.95f, 0.5f, 1.0f },  // 21 Anti-strange— pale yellow
    { 1.0f, 0.9f, 0.5f, 1.0f },   // 22 Anti-charm  — light gold
    { 1.0f, 0.5f, 0.5f, 1.0f },   // 23 Anti-top    — salmon
    { 0.75f, 0.6f, 0.5f, 1.0f },  // 24 Anti-bottom — tan
    // 25-29: Bosons
    { 0.3f, 1.0f, 0.3f, 1.0f },   // 25 Gluon       — lime green
    { 0.4f, 0.8f, 1.0f, 1.0f },   // 26 W+          — cyan
    { 0.2f, 0.6f, 0.85f, 1.0f },  // 27 W-          — dark cyan
    { 0.85f, 0.85f, 0.9f, 1.0f }, // 28 Z0          — silver
    { 1.0f, 0.95f, 0.7f, 1.0f },  // 29 Higgs       — white-gold
    // 30-32: Beyond Standard Model — hypothetical
    { 0.7f, 0.8f, 1.0f, 0.5f },   // 30 Graviton    — faint blue-white
    { 0.15f, 0.05f, 0.25f, 0.6f },// 31 Dark Matter — deep purple-black
    { 0.4f, 0.05f, 0.1f, 0.4f },  // 32 Dark Energy — faint crimson glow
    // 33-38: Dark Matter Candidates — deep purples & blues
    { 0.25f, 0.1f, 0.4f, 0.7f },  // 33 Axino           — dark purple
    { 0.1f, 0.0f, 0.2f, 0.5f },   // 34 WIMPzilla       — near-black violet
    { 0.2f, 0.15f, 0.35f, 0.8f }, // 35 SIMP            — muted purple
    { 0.3f, 0.4f, 0.3f, 0.5f },   // 36 Sterile Neutrino— ghostly grey-green
    { 0.35f, 0.1f, 0.5f, 0.7f },  // 37 Dark Photon     — dark violet
    { 0.4f, 0.2f, 0.6f, 0.9f },   // 38 Q-Ball          — bright purple
    // 39-49: SUSY Sparticles — metallic/neon pastels
    { 0.5f, 0.8f, 1.0f, 1.0f },   // 39 Selectron       — ice blue
    { 0.7f, 0.6f, 1.0f, 1.0f },   // 40 Smuon           — periwinkle
    { 0.6f, 0.5f, 0.9f, 1.0f },   // 41 Stau            — soft violet
    { 1.0f, 0.7f, 0.4f, 1.0f },   // 42 Squark          — neon orange
    { 0.4f, 1.0f, 0.5f, 1.0f },   // 43 Gluino          — neon green
    { 1.0f, 1.0f, 0.8f, 0.8f },   // 44 Photino         — pale gold
    { 0.9f, 0.9f, 1.0f, 1.0f },   // 45 Wino            — platinum
    { 0.8f, 0.8f, 0.95f, 1.0f },  // 46 Zino            — cool silver
    { 1.0f, 0.9f, 0.6f, 1.0f },   // 47 Higgsino        — warm gold
    { 0.3f, 0.15f, 0.5f, 1.0f },  // 48 Neutralino      — dark indigo
    { 0.55f, 0.85f, 0.55f, 0.8f },// 49 Sneutrino       — sage green
    // 50-55: Force Carriers & Gravity — bright cyans and whites
    { 0.6f, 0.7f, 1.0f, 0.6f },   // 50 Gravitino       — pale blue
    { 1.0f, 0.4f, 0.4f, 1.0f },   // 51 X Boson         — bright red
    { 1.0f, 0.5f, 0.3f, 1.0f },   // 52 Y Boson         — bright orange-red
    { 0.9f, 0.9f, 0.9f, 1.0f },   // 53 Monopole        — white
    { 0.6f, 0.5f, 0.3f, 0.8f },   // 54 Radion          — bronze
    { 0.5f, 0.45f, 0.35f, 0.8f }, // 55 Dilaton         — warm grey
    // 56-64: Theoretical Extremes — vivid/unusual
    { 0.0f, 1.0f, 1.0f, 0.7f },   // 56 Tachyon         — electric cyan
    { 1.0f, 0.0f, 0.5f, 1.0f },   // 57 Preon           — hot pink
    { 1.0f, 0.6f, 0.0f, 0.6f },   // 58 Inflaton        — amber glow
    { 0.45f, 0.5f, 0.45f, 0.4f }, // 59 Majoron         — faint grey-green
    { 0.7f, 0.3f, 1.0f, 1.0f },   // 60 Odderon         — electric purple
    { 0.5f, 1.0f, 0.3f, 1.0f },   // 61 Glueball        — lime-yellow
    { 0.9f, 0.4f, 0.2f, 1.0f },   // 62 Skyrmion        — burnt orange
    { 0.2f, 0.9f, 0.8f, 1.0f },   // 63 X17             — turquoise
    { 0.6f, 0.4f, 0.2f, 0.5f },   // 64 Chameleon       — fading brown
    // 65-66: New Class — distinctive
    { 0.9f, 0.2f, 1.0f, 1.0f },   // 65 Paraparticle    — vivid magenta
    { 0.3f, 0.6f, 0.9f, 0.6f },   // 66 Dyn. Axion QP   — soft sky blue
    // 67-73: Quasiparticles
    { 1.0f, 0.75f, 0.25f, 1.0f }, // 67 Electron Hole   — warm amber
    { 0.3f, 1.0f, 0.95f, 1.0f },  // 68 Plasmon          — cyan
    { 0.95f, 0.95f, 0.4f, 1.0f }, // 69 Phonon           — pale yellow
    { 1.0f, 0.45f, 0.15f, 1.0f }, // 70 Magnon           — orange-red
    { 0.7f, 0.35f, 0.9f, 1.0f },  // 71 Polaron          — purple
    { 0.6f, 0.85f, 1.0f, 1.0f },  // 72 Cooper Pair      — ice blue
    { 0.2f, 0.9f, 0.7f, 1.0f },   // 73 Roton            — teal-green
    // ── Mesons (74-261) ─────────────────────────────────────────────────────
    // Light pseudoscalar (74-86): warm yellow/orange
    {1.0f,0.85f,0.2f,1}, {1.0f,0.9f,0.3f,1}, {1.0f,0.85f,0.2f,1}, {0.95f,0.8f,0.15f,1}, {0.9f,0.75f,0.1f,1},
    {0.85f,0.7f,0.15f,1}, {0.85f,0.7f,0.15f,1}, {0.85f,0.7f,0.15f,1}, {0.9f,0.75f,0.1f,1}, {0.9f,0.75f,0.1f,1},
    {0.8f,0.65f,0.1f,1}, {0.8f,0.65f,0.1f,1}, {0.8f,0.65f,0.1f,1},
    // Light vector (87-100): red/crimson
    {0.95f,0.2f,0.15f,1}, {0.95f,0.25f,0.2f,1}, {0.95f,0.2f,0.15f,1}, {0.85f,0.15f,0.1f,1}, {0.9f,0.1f,0.3f,1},
    {0.8f,0.15f,0.12f,1}, {0.8f,0.15f,0.12f,1}, {0.8f,0.15f,0.12f,1}, {0.75f,0.12f,0.08f,1}, {0.85f,0.08f,0.25f,1},
    {0.7f,0.12f,0.1f,1}, {0.7f,0.12f,0.1f,1}, {0.7f,0.12f,0.1f,1}, {0.65f,0.1f,0.07f,1},
    // Light scalar (101-111): pink
    {1.0f,0.5f,0.6f,1}, {1.0f,0.45f,0.55f,1}, {1.0f,0.5f,0.6f,1}, {1.0f,0.45f,0.55f,1}, {0.95f,0.5f,0.6f,1},
    {0.9f,0.4f,0.5f,1}, {0.9f,0.4f,0.5f,1}, {0.9f,0.45f,0.55f,1}, {0.9f,0.4f,0.5f,1}, {0.85f,0.38f,0.48f,1}, {0.85f,0.35f,0.45f,1},
    // Light axial-vector (112-124): purple
    {0.7f,0.3f,0.9f,1}, {0.7f,0.35f,0.9f,1}, {0.7f,0.3f,0.9f,1}, {0.65f,0.25f,0.85f,1},
    {0.75f,0.3f,0.95f,1}, {0.75f,0.35f,0.95f,1}, {0.75f,0.3f,0.95f,1}, {0.6f,0.25f,0.85f,1}, {0.6f,0.2f,0.8f,1}, {0.55f,0.2f,0.8f,1},
    {0.65f,0.25f,0.85f,1}, {0.65f,0.3f,0.85f,1}, {0.65f,0.25f,0.85f,1},
    // Light tensor (125-134): deeper purple
    {0.55f,0.2f,0.8f,1}, {0.55f,0.25f,0.8f,1}, {0.55f,0.2f,0.8f,1}, {0.5f,0.18f,0.75f,1}, {0.5f,0.15f,0.7f,1},
    {0.48f,0.16f,0.72f,1}, {0.48f,0.18f,0.72f,1}, {0.48f,0.16f,0.72f,1}, {0.45f,0.14f,0.68f,1}, {0.45f,0.14f,0.68f,1},
    // Light pseudotensor (135-139)
    {0.6f,0.15f,0.7f,1}, {0.6f,0.18f,0.7f,1}, {0.6f,0.15f,0.7f,1}, {0.55f,0.12f,0.65f,1}, {0.55f,0.12f,0.65f,1},
    // Light higher spin (140-148)
    {0.5f,0.1f,0.65f,1}, {0.5f,0.12f,0.65f,1}, {0.5f,0.1f,0.65f,1}, {0.45f,0.08f,0.6f,1}, {0.45f,0.08f,0.6f,1},
    {0.4f,0.06f,0.55f,1}, {0.4f,0.08f,0.55f,1}, {0.4f,0.06f,0.55f,1}, {0.38f,0.06f,0.52f,1},
    // Strange (149-174): green
    {0.2f,0.9f,0.3f,1}, {0.25f,0.85f,0.3f,1}, {0.2f,0.9f,0.3f,1}, {0.25f,0.85f,0.3f,1},
    {0.15f,0.8f,0.25f,1}, {0.18f,0.78f,0.25f,1}, {0.15f,0.8f,0.25f,1}, {0.18f,0.78f,0.25f,1},
    {0.25f,0.75f,0.2f,1}, {0.25f,0.75f,0.2f,1},
    {0.2f,0.7f,0.18f,1}, {0.2f,0.7f,0.18f,1}, {0.18f,0.65f,0.16f,1}, {0.18f,0.65f,0.16f,1},
    {0.15f,0.7f,0.15f,1}, {0.15f,0.7f,0.15f,1}, {0.2f,0.68f,0.18f,1}, {0.2f,0.68f,0.18f,1},
    {0.12f,0.65f,0.14f,1}, {0.12f,0.65f,0.14f,1}, {0.15f,0.62f,0.12f,1}, {0.15f,0.62f,0.12f,1},
    {0.1f,0.6f,0.1f,1}, {0.1f,0.6f,0.1f,1}, {0.08f,0.55f,0.08f,1}, {0.08f,0.55f,0.08f,1},
    // Charmed (175-196): cyan/teal
    {0.1f,0.85f,0.85f,1}, {0.15f,0.8f,0.8f,1}, {0.1f,0.85f,0.85f,1}, {0.15f,0.8f,0.8f,1}, {0.1f,0.9f,0.8f,1}, {0.1f,0.9f,0.8f,1},
    {0.12f,0.75f,0.75f,1}, {0.08f,0.78f,0.78f,1}, {0.08f,0.78f,0.78f,1}, {0.12f,0.75f,0.75f,1}, {0.08f,0.82f,0.72f,1}, {0.08f,0.82f,0.72f,1},
    {0.15f,0.7f,0.7f,1}, {0.15f,0.7f,0.7f,1}, {0.12f,0.72f,0.65f,1},
    {0.1f,0.68f,0.68f,1}, {0.1f,0.68f,0.68f,1}, {0.08f,0.7f,0.62f,1}, {0.08f,0.7f,0.62f,1},
    {0.12f,0.65f,0.65f,1}, {0.12f,0.65f,0.65f,1}, {0.1f,0.67f,0.6f,1},
    // Bottom (197-216): blue
    {0.2f,0.3f,0.95f,1}, {0.25f,0.35f,0.9f,1}, {0.2f,0.3f,0.95f,1}, {0.25f,0.35f,0.9f,1}, {0.15f,0.25f,0.88f,1}, {0.15f,0.25f,0.88f,1},
    {0.18f,0.28f,0.92f,1}, {0.18f,0.28f,0.92f,1},
    {0.15f,0.25f,0.85f,1}, {0.18f,0.28f,0.82f,1}, {0.15f,0.25f,0.85f,1}, {0.18f,0.28f,0.82f,1}, {0.12f,0.2f,0.8f,1}, {0.12f,0.2f,0.8f,1},
    {0.1f,0.2f,0.78f,1}, {0.1f,0.2f,0.78f,1}, {0.08f,0.18f,0.75f,1}, {0.08f,0.18f,0.75f,1},
    {0.08f,0.15f,0.72f,1}, {0.08f,0.15f,0.72f,1},
    // Charmonium (217-234): gold
    {0.95f,0.8f,0.2f,1}, {1.0f,0.85f,0.0f,1},
    {0.9f,0.75f,0.15f,1}, {0.92f,0.78f,0.1f,1}, {0.88f,0.72f,0.12f,1}, {0.9f,0.75f,0.1f,1},
    {0.85f,0.7f,0.18f,1}, {0.95f,0.8f,0.0f,1}, {0.88f,0.72f,0.0f,1},
    {1.0f,0.9f,0.3f,1}, {0.82f,0.68f,0.15f,1}, {0.85f,0.7f,0.1f,1},
    {0.8f,0.65f,0.0f,1}, {0.78f,0.63f,0.0f,1}, {0.75f,0.6f,0.0f,1}, {0.72f,0.58f,0.0f,1}, {0.7f,0.55f,0.0f,1}, {0.68f,0.52f,0.0f,1},
    // Bottomonium (235-252): silver/steel
    {0.75f,0.75f,0.82f,1}, {0.8f,0.8f,0.9f,1},
    {0.7f,0.7f,0.78f,1}, {0.72f,0.72f,0.8f,1}, {0.68f,0.68f,0.76f,1}, {0.7f,0.7f,0.78f,1},
    {0.72f,0.72f,0.8f,1}, {0.78f,0.78f,0.88f,1},
    {0.65f,0.65f,0.74f,1}, {0.67f,0.67f,0.76f,1}, {0.63f,0.63f,0.72f,1}, {0.65f,0.65f,0.74f,1},
    {0.75f,0.75f,0.85f,1}, {0.62f,0.62f,0.72f,1}, {0.6f,0.6f,0.7f,1},
    {0.72f,0.72f,0.82f,1}, {0.68f,0.68f,0.78f,1}, {0.65f,0.65f,0.75f,1},
    // Exotic (253-261): magenta
    {0.95f,0.15f,0.7f,1}, {0.95f,0.15f,0.7f,1}, {0.9f,0.1f,0.65f,1}, {0.9f,0.1f,0.65f,1},
    {0.85f,0.08f,0.6f,1}, {0.85f,0.08f,0.6f,1}, {0.92f,0.12f,0.68f,1},
    {0.8f,0.05f,0.55f,1}, {0.8f,0.05f,0.55f,1},
    // Reserved (262-281): dark grey placeholder
    {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1},
    {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1},
    {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1},
    {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1}, {0.3f,0.3f,0.3f,1},
};

// ── Environment abundance tables ─────────────────────────────────────────────
// For envs 0-6: [env][type] — fraction for proton(0), neutron(1), electron(2)
static const float PHYS_ABUNDANCE[7][3] = {
    { 0.0f,  0.0f,  0.0f  },  // 0: Lab Mode — empty
    { 0.50f, 0.0f,  0.50f },  // 1: Hydrogen Plasma
    { 0.10f, 0.80f, 0.10f },  // 2: Neutron Star Surface
    { 0.45f, 0.10f, 0.45f },  // 3: Solar Core
    { 0.33f, 0.34f, 0.33f },  // 4: Particle Soup
    { 0.40f, 0.40f, 0.20f },  // 5: Alpha Emitter
    { 0.42f, 0.50f, 0.08f },  // 6: Heavy Nucleus
};

// Helper: write genome for a particle
static void write_genome(Particles& p, uint32_t type, std::mt19937& rng) {
    // [0] charge
    p.genomes.push_back(PHYS_CHARGE[type]);
    // [1] spin — random ± for fermions
    float spin = PHYS_SPIN[type];
    if (spin == 0.5f) {
        std::uniform_int_distribution<int> coin(0, 1);
        spin = coin(rng) ? 0.5f : -0.5f;
    }
    p.genomes.push_back(spin);
    // [2] color charge — only quarks/antiquarks/gluons
    float color = 0.0f;
    if (type >= UP_QUARK_TYPE && type <= BOTTOM_QUARK_TYPE) {
        // Quarks: random R(1), G(2), B(3)
        std::uniform_int_distribution<int> c(1, 3);
        color = static_cast<float>(c(rng));
    } else if (type >= ANTI_UP_TYPE && type <= ANTI_BOTTOM_TYPE) {
        // Antiquarks: anti-R(-1), anti-G(-2), anti-B(-3)
        std::uniform_int_distribution<int> c(1, 3);
        color = -static_cast<float>(c(rng));
    } else if (type == GLUON_TYPE_PHYS) {
        // Gluon: color-anticolor pair from SU(3) octet
        // Encoded as X.Y: floor(val)=carried color (R=1,G=2,B=3),
        //                  round(fract(val)*10)=anticolor index
        static const float GLUON_COLORS[8] = {
            1.2f, 1.3f,   // r-ḡ, r-b̄
            2.1f, 2.3f,   // g-r̄, g-b̄
            3.1f, 3.2f,   // b-r̄, b-ḡ
            1.1f, 2.2f    // diagonal (rr̄ mix, gḡ mix)
        };
        std::uniform_int_distribution<int> s(0, 7);
        color = GLUON_COLORS[s(rng)];
    } else if (type == SQUARK_TYPE_PHYS || type == GLUINO_TYPE_PHYS || type == PREON_TYPE_PHYS) {
        // Color-charged SUSY/exotic: random R/G/B like quarks
        std::uniform_int_distribution<int> c(1, 3);
        color = static_cast<float>(c(rng));
    }
    p.genomes.push_back(color);
    // [3] decay rate
    p.genomes.push_back(PHYS_DECAY_RATE[type]);
}

void physics_gen_data(Particles& p, const SimConfig& cfg) {
    // Reset all arrays
    p.positions.clear();
    p.velocities.clear();
    p.types.clear();
    p.energies.clear();
    p.angles.clear();
    p.angular_velocities.clear();
    p.genomes.clear();
    p.forces.clear();
    p.colors.clear();

    for (uint32_t i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        p.trait_scales[i] = 1.0f;
        p.behavior_flags[i] = BEHAVIOR_NONE;
    }

    // Set colors for all physics types
    p.colors.resize(MAX_PARTICLE_TYPES, glm::vec4(0.0f));
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i)
        p.colors[i] = PHYS_COLORS[i];

    // ── Behavior flags for all 33 types ──────────────────────────────────────
    // Composites
    p.behavior_flags[PROTON_TYPE]          = BEHAVIOR_MASS_HEAVY | BEHAVIOR_IONIC_POS;
    p.behavior_flags[NEUTRON_TYPE]         = BEHAVIOR_MASS_HEAVY;
    p.behavior_flags[ANTIPROTON_TYPE_PHYS] = BEHAVIOR_MASS_HEAVY | BEHAVIOR_IONIC_NEG;

    // Gen1 leptons
    p.behavior_flags[ELECTRON_TYPE_PHYS]   = BEHAVIOR_LEPTON | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[POSITRON_TYPE_PHYS]   = BEHAVIOR_LEPTON | BEHAVIOR_POSITRON | BEHAVIOR_IONIC_POS;
    p.behavior_flags[NEUTRINO_TYPE_PHYS]   = BEHAVIOR_NEUTRINO;

    // Gen2 leptons
    p.behavior_flags[MUON_TYPE_PHYS]       = BEHAVIOR_LEPTON | BEHAVIOR_MUON | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[ANTIMUON_TYPE_PHYS]   = BEHAVIOR_LEPTON | BEHAVIOR_POSITRON | BEHAVIOR_MUON | BEHAVIOR_IONIC_POS;
    p.behavior_flags[MU_NEUTRINO_TYPE_PHYS] = BEHAVIOR_NEUTRINO;

    // Gen3 leptons
    p.behavior_flags[TAU_TYPE_PHYS]        = BEHAVIOR_LEPTON | BEHAVIOR_TAU | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[ANTITAU_TYPE_PHYS]    = BEHAVIOR_LEPTON | BEHAVIOR_POSITRON | BEHAVIOR_TAU | BEHAVIOR_IONIC_POS;
    p.behavior_flags[TAU_NEUTRINO_TYPE_PHYS] = BEHAVIOR_NEUTRINO;

    // Quarks (all 6 flavors)
    p.behavior_flags[UP_QUARK_TYPE]        = BEHAVIOR_QUARK;
    p.behavior_flags[DOWN_QUARK_TYPE]      = BEHAVIOR_QUARK;
    p.behavior_flags[STRANGE_QUARK_TYPE]   = BEHAVIOR_QUARK;
    p.behavior_flags[CHARM_QUARK_TYPE]     = BEHAVIOR_QUARK;
    p.behavior_flags[TOP_QUARK_TYPE]       = BEHAVIOR_QUARK;
    p.behavior_flags[BOTTOM_QUARK_TYPE]    = BEHAVIOR_QUARK;

    // Antiquarks
    p.behavior_flags[ANTI_UP_TYPE]         = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;
    p.behavior_flags[ANTI_DOWN_TYPE]       = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;
    p.behavior_flags[ANTI_STRANGE_TYPE]    = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;
    p.behavior_flags[ANTI_CHARM_TYPE]      = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;
    p.behavior_flags[ANTI_TOP_TYPE]        = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;
    p.behavior_flags[ANTI_BOTTOM_TYPE]     = BEHAVIOR_QUARK | BEHAVIOR_ANTIQUARK;

    // Bosons
    p.behavior_flags[PHOTON_TYPE_PHYS]     = BEHAVIOR_PHOTON;
    p.behavior_flags[GLUON_TYPE_PHYS]      = BEHAVIOR_GLUON;  // colored boson, own QCD fast-path
    p.behavior_flags[W_PLUS_TYPE_PHYS]     = BEHAVIOR_WEAK_BOSON | BEHAVIOR_IONIC_POS;
    p.behavior_flags[W_MINUS_TYPE_PHYS]    = BEHAVIOR_WEAK_BOSON | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[Z_BOSON_TYPE_PHYS]    = BEHAVIOR_WEAK_BOSON;
    p.behavior_flags[HIGGS_TYPE_PHYS]      = BEHAVIOR_HIGGS;

    // Beyond Standard Model — hypothetical
    p.behavior_flags[GRAVITON_TYPE_PHYS]     = BEHAVIOR_GRAVITON | BEHAVIOR_PHOTON;  // ballistic massless
    p.behavior_flags[DARK_MATTER_TYPE_PHYS]  = BEHAVIOR_DARK_MATTER | BEHAVIOR_MASS_ULTRA;  // heavy, gravity-only
    p.behavior_flags[DARK_ENERGY_TYPE_PHYS]  = BEHAVIOR_DARK_ENERGY;  // repulsive field

    // ── Dark Matter Candidates ──────────────────────────────────────────────
    p.behavior_flags[AXINO_TYPE_PHYS]            = BEHAVIOR_DARK_MATTER | BEHAVIOR_EXOTIC;
    p.behavior_flags[WIMPZILLA_TYPE_PHYS]        = BEHAVIOR_DARK_MATTER | BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;
    p.behavior_flags[SIMP_TYPE_PHYS]             = BEHAVIOR_DARK_MATTER | BEHAVIOR_EXOTIC;  // custom self-force in shader
    p.behavior_flags[STERILE_NEUTRINO_TYPE_PHYS] = BEHAVIOR_NEUTRINO | BEHAVIOR_EXOTIC;
    p.behavior_flags[DARK_PHOTON_TYPE_PHYS]      = BEHAVIOR_PHOTON | BEHAVIOR_EXOTIC;
    p.behavior_flags[QBALL_TYPE_PHYS]            = BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_POS | BEHAVIOR_EXOTIC;

    // ── Supersymmetric Sparticles ───────────────────────────────────────────
    p.behavior_flags[SELECTRON_TYPE_PHYS]   = BEHAVIOR_SUSY | BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[SMUON_TYPE_PHYS]       = BEHAVIOR_SUSY | BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[STAU_TYPE_PHYS]        = BEHAVIOR_SUSY | BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[SQUARK_TYPE_PHYS]      = BEHAVIOR_SUSY | BEHAVIOR_QUARK | BEHAVIOR_MASS_ULTRA;
    p.behavior_flags[GLUINO_TYPE_PHYS]      = BEHAVIOR_SUSY | BEHAVIOR_QUARK | BEHAVIOR_MASS_ULTRA;
    p.behavior_flags[PHOTINO_TYPE_PHYS]     = BEHAVIOR_SUSY | BEHAVIOR_WEAK_BOSON;
    p.behavior_flags[WINO_TYPE_PHYS]        = BEHAVIOR_SUSY | BEHAVIOR_WEAK_BOSON | BEHAVIOR_IONIC_POS;
    p.behavior_flags[ZINO_TYPE_PHYS]        = BEHAVIOR_SUSY | BEHAVIOR_WEAK_BOSON;
    p.behavior_flags[HIGGSINO_TYPE_PHYS]    = BEHAVIOR_SUSY | BEHAVIOR_HIGGS;
    p.behavior_flags[NEUTRALINO_TYPE_PHYS]  = BEHAVIOR_DARK_MATTER | BEHAVIOR_SUSY;  // stable LSP
    p.behavior_flags[SNEUTRINO_TYPE_PHYS]   = BEHAVIOR_SUSY | BEHAVIOR_NEUTRINO;

    // ── Force Carriers & Gravity ────────────────────────────────────────────
    p.behavior_flags[GRAVITINO_TYPE_PHYS]   = BEHAVIOR_GRAVITON | BEHAVIOR_PHOTON | BEHAVIOR_SUSY;
    p.behavior_flags[X_BOSON_TYPE_PHYS]     = BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_POS | BEHAVIOR_QUARK | BEHAVIOR_EXOTIC;
    p.behavior_flags[Y_BOSON_TYPE_PHYS]     = BEHAVIOR_MASS_ULTRA | BEHAVIOR_IONIC_POS | BEHAVIOR_QUARK | BEHAVIOR_EXOTIC;
    p.behavior_flags[MONOPOLE_TYPE_PHYS]    = BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;  // custom B-field in shader
    p.behavior_flags[RADION_TYPE_PHYS]      = BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;
    p.behavior_flags[DILATON_TYPE_PHYS]     = BEHAVIOR_MASS_HEAVY | BEHAVIOR_EXOTIC;

    // ── Theoretical Extremes ────────────────────────────────────────────────
    p.behavior_flags[TACHYON_TYPE_PHYS]     = BEHAVIOR_PHOTON | BEHAVIOR_EXOTIC;  // superluminal in shader
    p.behavior_flags[PREON_TYPE_PHYS]       = BEHAVIOR_QUARK | BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;
    p.behavior_flags[INFLATON_TYPE_PHYS]    = BEHAVIOR_DARK_ENERGY | BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;
    p.behavior_flags[MAJORON_TYPE_PHYS]     = BEHAVIOR_NEUTRINO | BEHAVIOR_EXOTIC;
    p.behavior_flags[ODDERON_TYPE_PHYS]     = BEHAVIOR_QUARK | BEHAVIOR_EXOTIC;
    p.behavior_flags[GLUEBALL_TYPE_PHYS]    = BEHAVIOR_QUARK | BEHAVIOR_EXOTIC;
    p.behavior_flags[SKYRMION_TYPE_PHYS]    = BEHAVIOR_MASS_HEAVY | BEHAVIOR_IONIC_POS | BEHAVIOR_EXOTIC;
    p.behavior_flags[X17_TYPE_PHYS]         = BEHAVIOR_PHOTON | BEHAVIOR_EXOTIC;
    p.behavior_flags[CHAMELEON_TYPE_PHYS]   = BEHAVIOR_DARK_ENERGY | BEHAVIOR_EXOTIC;

    // ── New Class (2025-2026) ───────────────────────────────────────────────
    p.behavior_flags[PARAPARTICLE_TYPE_PHYS]     = BEHAVIOR_WEAK_BOSON | BEHAVIOR_EXOTIC;
    p.behavior_flags[DYN_AXION_QP_TYPE_PHYS]     = BEHAVIOR_NEUTRINO | BEHAVIOR_EXOTIC;

    // ── Quasiparticles ────────────────────────────────────────────────────────
    p.behavior_flags[ELECTRON_HOLE_TYPE_PHYS]  = BEHAVIOR_IONIC_POS | BEHAVIOR_EXOTIC;
    p.behavior_flags[PLASMON_TYPE_PHYS]        = BEHAVIOR_EXOTIC;
    p.behavior_flags[PHONON_TYPE_PHYS]         = BEHAVIOR_EXOTIC;
    p.behavior_flags[MAGNON_TYPE_PHYS]         = BEHAVIOR_EXOTIC;
    p.behavior_flags[POLARON_TYPE_PHYS]        = BEHAVIOR_LEPTON | BEHAVIOR_EXOTIC;
    p.behavior_flags[COOPER_PAIR_TYPE_PHYS]    = BEHAVIOR_MASS_ULTRA | BEHAVIOR_EXOTIC;
    p.behavior_flags[ROTON_TYPE_PHYS]          = BEHAVIOR_EXOTIC;

    // ── Mesons (types 74-261) ─────────────────────────────────────────────────
    // Mesons are color-neutral QCD bound states. Behavior flags encode mass tier + charge.
    for (uint32_t t = MESON_TYPE_FIRST; t <= MESON_TYPE_LAST; ++t) {
        uint32_t idx = t - MESON_TYPE_FIRST;
        float charge = MESON_CHARGE[idx];
        float mass   = MESON_MASS_MEV[idx];
        uint32_t flags = BEHAVIOR_NONE;

        // Mass tier based on PDG mass
        if (mass > 3000.0f)       flags |= BEHAVIOR_MASS_ULTRA;   // charmed, bottom, quarkonium
        else if (mass > 800.0f)   flags |= BEHAVIOR_MASS_HEAVY;   // strange, excited light
        else                      flags |= BEHAVIOR_MASS_MEDIUM;  // pions, light ground states

        // Charge for EM interaction
        if (charge > 0.5f)        flags |= BEHAVIOR_IONIC_POS;
        else if (charge < -0.5f)  flags |= BEHAVIOR_IONIC_NEG;

        p.behavior_flags[t] = flags;
    }

    // Force matrix: zeroed (physics is computed in shader, not from matrix)
    p.forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);

    std::mt19937 rng(cfg.generation_seed);

    // ── Lab Mode — empty world ───────────────────────────────────────────────
    if (cfg.start_empty) {
        uint32_t pool = cfg.pool_size;
        p.positions.resize(pool);
        p.velocities.resize(pool, glm::vec2(0.0f));
        p.types.resize(pool, PROTON_TYPE);
        p.energies.resize(pool, 0.0f);  // dormant
        p.angles.resize(pool, 0.0f);
        p.angular_velocities.resize(pool, 0.0f);
        p.genomes.resize(pool * GENOME_SIZE, 0.0f);

        std::uniform_real_distribution<float> dx(0.0f, static_cast<float>(WORLD_W));
        std::uniform_real_distribution<float> dy(0.0f, static_cast<float>(WORLD_H));

        p.birth_frames.assign(pool, 0u);
        p.orbital_parent.assign(pool, -1);
        p.orbital_shell.assign(pool, -1);
        p.excitation_timer.assign(pool, 0);
        p.entangled_partner.assign(pool, 0xFFFFFFFFu);

        for (uint32_t i = 0; i < pool; ++i) {
            p.positions[i] = glm::vec2(dx(rng), dy(rng));
            // Dormant: all genome slots zero (no charge, no spin, no color, no decay)
        }
        return;
    }

    // ── Environment spawn ────────────────────────────────────────────────────
    uint32_t env = std::min(cfg.environment_mode, static_cast<uint32_t>(PHYS_ENV_COUNT - 1));
    uint32_t count = cfg.particle_count;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> dx(0.0f, static_cast<float>(WORLD_W));
    std::uniform_real_distribution<float> dy(0.0f, static_cast<float>(WORLD_H));
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    const float rw = static_cast<float>(WORLD_W);   // world bounds
    const float rh = static_cast<float>(WORLD_H);
    const float sw = static_cast<float>(REGION_W);  // spawn distribution scale
    const float sh = static_cast<float>(REGION_H);

    p.positions.reserve(count);
    p.velocities.reserve(count);
    p.types.reserve(count);
    p.energies.reserve(count);
    p.angles.reserve(count);
    p.angular_velocities.reserve(count);
    p.genomes.reserve(count * GENOME_SIZE);

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t type;

        // Type selection based on environment
        if (env <= 6) {
            // Standard p/n/e environments
            float cum[3];
            cum[0] = PHYS_ABUNDANCE[env][0];
            cum[1] = cum[0] + PHYS_ABUNDANCE[env][1];
            cum[2] = cum[1] + PHYS_ABUNDANCE[env][2];
            float r = unit(rng);
            type = (r < cum[0]) ? PROTON_TYPE :
                   (r < cum[1]) ? NEUTRON_TYPE : ELECTRON_TYPE_PHYS;
        } else if (env == 7) {
            // Quark-Gluon Plasma: 40% quarks (u/d), 30% antiquarks, 20% gluons, 10% electrons
            float r = unit(rng);
            if (r < 0.20f) type = UP_QUARK_TYPE;
            else if (r < 0.40f) type = DOWN_QUARK_TYPE;
            else if (r < 0.55f) type = ANTI_UP_TYPE;
            else if (r < 0.70f) type = ANTI_DOWN_TYPE;
            else if (r < 0.90f) type = GLUON_TYPE_PHYS;
            else type = ELECTRON_TYPE_PHYS;
        } else if (env == 8) {
            // Electroweak Era: W/Z/Higgs + leptons
            float r = unit(rng);
            if (r < 0.15f) type = W_PLUS_TYPE_PHYS;
            else if (r < 0.30f) type = W_MINUS_TYPE_PHYS;
            else if (r < 0.40f) type = Z_BOSON_TYPE_PHYS;
            else if (r < 0.45f) type = HIGGS_TYPE_PHYS;
            else if (r < 0.60f) type = ELECTRON_TYPE_PHYS;
            else if (r < 0.70f) type = POSITRON_TYPE_PHYS;
            else if (r < 0.80f) type = NEUTRINO_TYPE_PHYS;
            else if (r < 0.90f) type = MUON_TYPE_PHYS;
            else type = ANTIMUON_TYPE_PHYS;
        } else if (env == 10) {
            // Particle Accelerator: proton beam + some electrons for diagnostics
            float r = unit(rng);
            if (r < 0.80f) type = PROTON_TYPE;
            else if (r < 0.95f) type = ELECTRON_TYPE_PHYS;
            else type = PHOTON_TYPE_PHYS;  // synchrotron radiation
        } else if (env == 11) {
            // Dark Sector: 40% DM, 30% protons, 15% electrons, 10% DE, 5% gravitons
            float r = unit(rng);
            if (r < 0.40f) type = DARK_MATTER_TYPE_PHYS;
            else if (r < 0.70f) type = PROTON_TYPE;
            else if (r < 0.85f) type = ELECTRON_TYPE_PHYS;
            else if (r < 0.95f) type = DARK_ENERGY_TYPE_PHYS;
            else type = GRAVITON_TYPE_PHYS;
        } else if (env == 12) {
            // SUSY Sector: 30% neutralino, 20% selectron, 15% smuon, 10% squark, 10% gluino, 10% photon, 5% electron
            float r = unit(rng);
            if (r < 0.30f) type = NEUTRALINO_TYPE_PHYS;
            else if (r < 0.50f) type = SELECTRON_TYPE_PHYS;
            else if (r < 0.65f) type = SMUON_TYPE_PHYS;
            else if (r < 0.75f) type = SQUARK_TYPE_PHYS;
            else if (r < 0.85f) type = GLUINO_TYPE_PHYS;
            else if (r < 0.95f) type = PHOTON_TYPE_PHYS;
            else type = ELECTRON_TYPE_PHYS;
        } else if (env == 13) {
            // Big Bang: quark-epoch plasma — all SM particles, matter-antimatter symmetric
            float r = unit(rng);
            if      (r < 0.10f) type = UP_QUARK_TYPE;        // 10% u
            else if (r < 0.20f) type = DOWN_QUARK_TYPE;      // 10% d
            else if (r < 0.30f) type = ANTI_UP_TYPE;          // 10% ū
            else if (r < 0.40f) type = ANTI_DOWN_TYPE;        // 10% d̄
            else if (r < 0.55f) type = GLUON_TYPE_PHYS;      // 15% gluon
            else if (r < 0.65f) type = PHOTON_TYPE_PHYS;     // 10% photon
            else if (r < 0.69f) type = ELECTRON_TYPE_PHYS;   //  4% e⁻
            else if (r < 0.73f) type = POSITRON_TYPE_PHYS;   //  4% e⁺
            else if (r < 0.76f) type = NEUTRINO_TYPE_PHYS;   //  3% νe
            else if (r < 0.78f) type = MU_NEUTRINO_TYPE_PHYS;//  2% νμ
            else if (r < 0.80f) type = TAU_NEUTRINO_TYPE_PHYS;//  2% ντ
            else if (r < 0.83f) type = W_PLUS_TYPE_PHYS;     //  3% W⁺
            else if (r < 0.86f) type = W_MINUS_TYPE_PHYS;    //  3% W⁻
            else if (r < 0.88f) type = Z_BOSON_TYPE_PHYS;    //  2% Z⁰
            else if (r < 0.90f) type = HIGGS_TYPE_PHYS;      //  2% Higgs
            else if (r < 0.93f) type = GRAVITON_TYPE_PHYS;   //  3% graviton
            else if (r < 0.97f) type = DARK_MATTER_TYPE_PHYS;//  4% dark matter
            else                type = DARK_ENERGY_TYPE_PHYS; //  3% dark energy
        } else {
            // Meson Factory: diverse meson states + some quarks/gluons
            float r = unit(rng);
            if      (r < 0.10f) type = PION_PLUS_MESON;       // π⁺
            else if (r < 0.17f) type = PION_ZERO_MESON;       // π⁰
            else if (r < 0.24f) type = PION_MINUS_MESON;      // π⁻
            else if (r < 0.30f) type = KAON_PLUS_MESON;       // K⁺
            else if (r < 0.34f) type = KAON_MINUS_MESON;      // K⁻
            else if (r < 0.38f) type = KAON_ZERO_MESON;       // K⁰
            else if (r < 0.42f) type = ETA_MESON;             // η
            else if (r < 0.48f) type = RHO_770_PLUS;          // ρ⁺
            else if (r < 0.52f) type = RHO_770_ZERO;          // ρ⁰
            else if (r < 0.55f) type = OMEGA_782_MESON;       // ω
            else if (r < 0.58f) type = PHI_1020_MESON;        // φ
            else if (r < 0.62f) type = D_PLUS_MESON;          // D⁺
            else if (r < 0.65f) type = D_ZERO_MESON;          // D⁰
            else if (r < 0.68f) type = DS_PLUS_MESON;         // Ds⁺
            else if (r < 0.71f) type = JPSI_MESON;            // J/ψ
            else if (r < 0.74f) type = B_PLUS_MESON;          // B⁺
            else if (r < 0.76f) type = BS_ZERO_MESON;         // Bs⁰
            else if (r < 0.78f) type = UPSILON_1S;            // Υ(1S)
            else if (r < 0.80f) type = ETA_PRIME_MESON;       // η'
            else if (r < 0.85f) type = GLUON_TYPE_PHYS;       // gluon
            else if (r < 0.90f) type = UP_QUARK_TYPE;         // u
            else if (r < 0.95f) type = ANTI_DOWN_TYPE;        // d̄
            else                type = ELECTRON_TYPE_PHYS;     // e⁻
        }

        // Spatial distribution
        glm::vec2 pos;
        switch (env) {
            case 2: {
                if (unit(rng) < 0.80f) {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(sw, sh) * 0.12f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                } else {
                    pos = glm::vec2(
                        rw * 0.5f + (dx(rng) - rw * 0.5f) * (sw / rw),
                        rh * 0.5f + (dy(rng) - rh * 0.5f) * (sh / rh));
                }
                break;
            }
            case 3: {
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sigma = std::min(sw, sh) * 0.25f;
                pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                break;
            }
            case 5: {
                if (i % 4 == 0) {
                    pos = glm::vec2(
                        rw * 0.5f + (dx(rng) - rw * 0.5f) * (sw / rw),
                        rh * 0.5f + (dy(rng) - rh * 0.5f) * (sh / rh));
                } else {
                    glm::vec2 center = p.positions[i - (i % 4)];
                    pos = center + glm::vec2(gauss(rng) * 4.0f, gauss(rng) * 4.0f);
                }
                break;
            }
            case 6: {
                if (type == ELECTRON_TYPE_PHYS) {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(sw, sh) * 0.18f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                } else {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(sw, sh) * 0.04f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                }
                break;
            }
            case 7: case 8: case 9: case 11: case 12: {
                // QGP, Electroweak, Meson Factory, Dark Sector, SUSY Sector — central Gaussian
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sigma = std::min(sw, sh) * 0.20f;
                pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                break;
            }
            case 13: {
                // Big Bang: tight central singularity (3% of screen)
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sigma = std::min(sw, sh) * 0.03f;
                pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                break;
            }
            case 10: {
                // Particle Accelerator — beam ring (elliptical)
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float rx = sw * 0.35f;  // ellipse semi-major
                float ry = sh * 0.35f;  // ellipse semi-minor
                float beam_width = 12.0f;
                std::uniform_real_distribution<float> angle_dist(0.0f, 6.2831853f);
                float theta = angle_dist(rng);
                pos = glm::vec2(cx + std::cos(theta) * rx + gauss(rng) * beam_width,
                                cy + std::sin(theta) * ry + gauss(rng) * beam_width);
                break;
            }
            default:
                pos = glm::vec2(
                    rw * 0.5f + (dx(rng) - rw * 0.5f) * (sw / rw),
                    rh * 0.5f + (dy(rng) - rh * 0.5f) * (sh / rh));
                break;
        }

        // Wrap toroidal
        pos.x = std::fmod(pos.x + rw, rw);
        pos.y = std::fmod(pos.y + rh, rh);

        // Random initial velocity (scaled by mass)
        glm::vec2 vel;
        if (env == 13) {
            // Big Bang: radial outward expansion from center (Hubble-like v ∝ r)
            float cx = rw * 0.5f, cy = rh * 0.5f;
            glm::vec2 delta = pos - glm::vec2(cx, cy);
            float r = glm::length(delta);
            float expansion_speed = 15.0f;
            if (r > 0.1f) {
                vel = glm::normalize(delta) * expansion_speed;
            } else {
                vel = glm::vec2(gauss(rng), gauss(rng)) * expansion_speed;
            }
            // Thermal jitter on top of expansion
            vel += glm::vec2(gauss(rng) * 3.0f, gauss(rng) * 3.0f);
            // Massless particles at c
            if (type == PHOTON_TYPE_PHYS || type == GRAVITON_TYPE_PHYS || type == GLUON_TYPE_PHYS) {
                vel = glm::normalize(vel + glm::vec2(0.001f)) * C_SIM;
            }
        } else if (env == 10) {
            // Accelerator: tangential velocity along the beam ring
            float cx = rw * 0.5f, cy = rh * 0.5f;
            float dx_beam = pos.x - cx, dy_beam = pos.y - cy;
            float dist = std::sqrt(dx_beam * dx_beam + dy_beam * dy_beam) + 1e-6f;
            float nx = dx_beam / dist, ny = dy_beam / dist;
            // Tangent = (-ny, nx) for counter-clockwise
            vel = glm::vec2(-ny * 8.0f, nx * 8.0f);
        } else {
            vel = glm::vec2(gauss(rng) * 2.0f, gauss(rng) * 2.0f);
            bool is_lepton = (type == ELECTRON_TYPE_PHYS || type == POSITRON_TYPE_PHYS ||
                              type == MUON_TYPE_PHYS || type == ANTIMUON_TYPE_PHYS);
            if (is_lepton) vel *= 5.0f;
            if (type >= UP_QUARK_TYPE && type <= ANTI_BOTTOM_TYPE) vel *= 3.0f;
            if (type == GRAVITON_TYPE_PHYS || type == GRAVITINO_TYPE_PHYS) vel = glm::normalize(vel + glm::vec2(0.001f)) * 200.0f;
            if (type == DARK_MATTER_TYPE_PHYS || type == NEUTRALINO_TYPE_PHYS || type == WIMPZILLA_TYPE_PHYS) vel *= 0.3f;
            if (type == DARK_ENERGY_TYPE_PHYS || type == INFLATON_TYPE_PHYS || type == CHAMELEON_TYPE_PHYS) vel *= 0.1f;
            if (type == DARK_PHOTON_TYPE_PHYS || type == X17_TYPE_PHYS) vel = glm::normalize(vel + glm::vec2(0.001f)) * C_SIM;
            if (type == TACHYON_TYPE_PHYS) vel = glm::normalize(vel + glm::vec2(0.001f)) * 400.0f;  // superluminal
            if (type == SIMP_TYPE_PHYS) vel *= 0.5f;
            if (type == AXINO_TYPE_PHYS || type == STERILE_NEUTRINO_TYPE_PHYS) vel *= 0.4f;
            // SUSY sparticles: moderate speed, heavy
            if (type >= SELECTRON_TYPE_PHYS && type <= SNEUTRINO_TYPE_PHYS && type != NEUTRALINO_TYPE_PHYS) vel *= 0.8f;
        }

        p.positions.push_back(pos);
        p.velocities.push_back(vel);
        p.types.push_back(type);
        p.energies.push_back(0.5f);
        p.angles.push_back(0.0f);
        p.angular_velocities.push_back(0.0f);

        // Full genome: [charge, spin, color_charge, decay_rate]
        write_genome(p, type, rng);
    }

    // Auxiliary vectors (not populated in the per-particle loop above)
    p.birth_frames.assign(count, 0u);
    p.orbital_parent.assign(count, -1);
    p.orbital_shell.assign(count, -1);
    p.excitation_timer.assign(count, 0);
    p.entangled_partner.assign(count, 0xFFFFFFFFu);
}

// ── Colorblind correction ────────────────────────────────────────────────────
// Simplified Daltonize: shift problematic hue channels to distinguishable ones.
// mode: 0=off, 1=protanopia (red-weak), 2=deuteranopia (green-weak), 3=tritanopia (blue-weak)
void apply_colorblind_correction(Particles& p, int mode) {
    if (mode <= 0 || mode > 3) return;

    for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(p.colors.size()),
                                        static_cast<uint32_t>(PHYS_PARTICLE_TYPES)); ++i) {
        // Start from original colors
        glm::vec4 c = PHYS_COLORS[i];
        float r = c.r, g = c.g, b = c.b;

        switch (mode) {
            case 1: // Protanopia: red-weak — shift red toward blue
                r = c.r * 0.2f + c.b * 0.8f;
                g = c.g * 0.9f + c.r * 0.1f;
                b = c.b * 0.7f + c.r * 0.3f;
                break;
            case 2: // Deuteranopia: green-weak — shift green toward blue
                r = c.r * 0.8f + c.g * 0.2f;
                g = c.g * 0.2f + c.b * 0.8f;
                b = c.b * 0.7f + c.g * 0.3f;
                break;
            case 3: // Tritanopia: blue-weak — shift blue toward red
                r = c.r * 0.7f + c.b * 0.3f;
                g = c.g * 0.9f + c.b * 0.1f;
                b = c.b * 0.2f + c.r * 0.8f;
                break;
        }

        p.colors[i] = glm::vec4(
            std::clamp(r, 0.0f, 1.0f),
            std::clamp(g, 0.0f, 1.0f),
            std::clamp(b, 0.0f, 1.0f),
            c.a);
    }
}
