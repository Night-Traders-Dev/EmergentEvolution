#include "physics/phys_particles.h"
#include <random>
#include <cmath>
#include <algorithm>

// ── Sub-atomic particle colors (30 types) ────────────────────────────────────
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
        // Gluon: bi-colored, encode as R(1) for simplicity
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

    // ── Behavior flags for all 30 types ──────────────────────────────────────
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
    p.behavior_flags[GLUON_TYPE_PHYS]      = BEHAVIOR_GLUON | BEHAVIOR_PHOTON;  // ballistic like photon
    p.behavior_flags[W_PLUS_TYPE_PHYS]     = BEHAVIOR_WEAK_BOSON | BEHAVIOR_IONIC_POS;
    p.behavior_flags[W_MINUS_TYPE_PHYS]    = BEHAVIOR_WEAK_BOSON | BEHAVIOR_IONIC_NEG;
    p.behavior_flags[Z_BOSON_TYPE_PHYS]    = BEHAVIOR_WEAK_BOSON;
    p.behavior_flags[HIGGS_TYPE_PHYS]      = BEHAVIOR_HIGGS;

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

        std::uniform_real_distribution<float> dx(0.0f, static_cast<float>(REGION_W));
        std::uniform_real_distribution<float> dy(0.0f, static_cast<float>(REGION_H));

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
    std::uniform_real_distribution<float> dx(0.0f, static_cast<float>(REGION_W));
    std::uniform_real_distribution<float> dy(0.0f, static_cast<float>(REGION_H));
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    const float rw = static_cast<float>(REGION_W);
    const float rh = static_cast<float>(REGION_H);

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
        } else {
            // Meson Factory: quark-antiquark pairs
            float r = unit(rng);
            if (r < 0.20f) type = UP_QUARK_TYPE;
            else if (r < 0.40f) type = ANTI_DOWN_TYPE;
            else if (r < 0.50f) type = DOWN_QUARK_TYPE;
            else if (r < 0.60f) type = ANTI_UP_TYPE;
            else if (r < 0.70f) type = STRANGE_QUARK_TYPE;
            else if (r < 0.80f) type = ANTI_STRANGE_TYPE;
            else if (r < 0.90f) type = GLUON_TYPE_PHYS;
            else type = ELECTRON_TYPE_PHYS;
        }

        // Spatial distribution
        glm::vec2 pos;
        switch (env) {
            case 2: {
                if (unit(rng) < 0.80f) {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(rw, rh) * 0.12f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                } else {
                    pos = glm::vec2(dx(rng), dy(rng));
                }
                break;
            }
            case 3: {
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sigma = std::min(rw, rh) * 0.25f;
                pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                break;
            }
            case 5: {
                if (i % 4 == 0) {
                    pos = glm::vec2(dx(rng), dy(rng));
                } else {
                    glm::vec2 center = p.positions[i - (i % 4)];
                    pos = center + glm::vec2(gauss(rng) * 4.0f, gauss(rng) * 4.0f);
                }
                break;
            }
            case 6: {
                if (type == ELECTRON_TYPE_PHYS) {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(rw, rh) * 0.18f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                } else {
                    float cx = rw * 0.5f, cy = rh * 0.5f;
                    float sigma = std::min(rw, rh) * 0.04f;
                    pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                }
                break;
            }
            case 7: case 8: case 9: {
                // QGP, Electroweak, Meson Factory — central Gaussian
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sigma = std::min(rw, rh) * 0.20f;
                pos = glm::vec2(cx + gauss(rng) * sigma, cy + gauss(rng) * sigma);
                break;
            }
            default:
                pos = glm::vec2(dx(rng), dy(rng));
                break;
        }

        // Wrap toroidal
        pos.x = std::fmod(pos.x + rw, rw);
        pos.y = std::fmod(pos.y + rh, rh);

        // Random initial velocity (scaled by mass)
        glm::vec2 vel = glm::vec2(gauss(rng) * 2.0f, gauss(rng) * 2.0f);
        bool is_lepton = (type == ELECTRON_TYPE_PHYS || type == POSITRON_TYPE_PHYS ||
                          type == MUON_TYPE_PHYS || type == ANTIMUON_TYPE_PHYS);
        if (is_lepton) vel *= 5.0f;
        if (type >= UP_QUARK_TYPE && type <= ANTI_BOTTOM_TYPE) vel *= 3.0f;

        p.positions.push_back(pos);
        p.velocities.push_back(vel);
        p.types.push_back(type);
        p.energies.push_back(0.5f);
        p.angles.push_back(0.0f);
        p.angular_velocities.push_back(0.0f);

        // Full genome: [charge, spin, color_charge, decay_rate]
        write_genome(p, type, rng);
    }
}
