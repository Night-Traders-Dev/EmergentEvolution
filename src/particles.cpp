#include "particles.h"
#include <random>
#include <cstring>
#include <algorithm>
#include <cmath>

Particles::Particles() {
    // Pre-allocate force / color arrays at max size
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
    gen_default_colors();
}

void Particles::gen_data(const SimConfig& cfg) {
    rng_.seed(cfg.generation_seed);
    for (float& s : trait_scales) s = 1.0f;
    for (auto& f : structure_integrity) f = 1.0f;
    for (auto& f : behavior_flags) f = BEHAVIOR_NONE;

    if (cfg.reset_forces)
        gen_random_force_matrix();

    if (cfg.reset_colors)
        gen_default_colors();

    // Set atom types, CPK colors, and chemistry force matrix
    apply_atom_defaults(cfg.particle_types, cfg.force_randomness);

    gen_particles(cfg);
}

// ── CPK colors & chemistry force matrix ──────────────────────────────────────

// CPK atom colors (standard chemistry palette), indexed 0-17
// H C N O P S Na Cl  + Fe Ni Si Ca Ti Sr Au Pb Eu U
static const glm::vec4 CPK_COLORS[ATOM_COUNT] = {
    { 0.95f, 0.95f, 0.95f, 1.0f }, // H  — near-white
    { 0.33f, 0.33f, 0.33f, 1.0f }, // C  — dark grey
    { 0.18f, 0.31f, 0.97f, 1.0f }, // N  — blue
    { 1.00f, 0.05f, 0.05f, 1.0f }, // O  — red
    { 1.00f, 0.50f, 0.00f, 1.0f }, // P  — orange
    { 1.00f, 1.00f, 0.19f, 1.0f }, // S  — yellow
    { 0.67f, 0.36f, 0.95f, 1.0f }, // Na — violet
    { 0.12f, 0.94f, 0.12f, 1.0f }, // Cl — green
    // ── R-process / supernova elements ────────────────────────────────────────
    { 0.88f, 0.40f, 0.20f, 1.0f }, // Fe — rust orange-brown
    { 0.31f, 0.82f, 0.31f, 1.0f }, // Ni — pale green
    { 0.94f, 0.78f, 0.63f, 1.0f }, // Si — sandy tan
    { 0.24f, 1.00f, 0.00f, 1.0f }, // Ca — bright lime
    { 0.75f, 0.76f, 0.78f, 1.0f }, // Ti — silver
    { 0.00f, 0.78f, 0.39f, 1.0f }, // Sr — teal green
    { 1.00f, 0.82f, 0.14f, 1.0f }, // Au — gold
    { 0.34f, 0.35f, 0.38f, 1.0f }, // Pb — dark slate
    { 0.38f, 1.00f, 0.78f, 1.0f }, // Eu — aqua
    { 0.00f, 0.56f, 1.00f, 1.0f }, // U  — steel blue
};

// Electrochemistry force matrix seed:
// forces[a][b] = how strongly type a is attracted to type b (+attraction, -repulsion)
// Indexed: 0=H 1=C 2=N 3=O 4=P 5=S 6=Na 7=Cl 8=Fe 9=Ni 10=Si 11=Ca 12=Ti 13=Sr 14=Au 15=Pb 16=Eu 17=U
static const float CHEM_FORCE[ATOM_COUNT][ATOM_COUNT] = {
//    H      C      N      O      P      S     Na     Cl     Fe     Ni     Si     Ca     Ti     Sr     Au     Pb     Eu     U
{ 0.10f, 0.20f, 0.40f, 0.60f, 0.20f, 0.20f, 0.00f, 0.30f, 0.10f, 0.10f, 0.10f, 0.10f, 0.00f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f }, // H
{ 0.20f, 0.50f, 0.20f, 0.10f, 0.30f, 0.30f,-0.20f,-0.20f, 0.10f, 0.20f, 0.30f, 0.00f, 0.10f,-0.10f, 0.10f, 0.00f, 0.00f, 0.00f }, // C
{ 0.40f, 0.20f, 0.30f, 0.30f, 0.10f, 0.10f,-0.20f,-0.10f, 0.20f, 0.10f, 0.10f,-0.10f, 0.00f,-0.10f, 0.00f, 0.00f, 0.00f, 0.10f }, // N
{ 0.60f, 0.10f, 0.30f, 0.20f,-0.10f, 0.00f,-0.30f,-0.10f, 0.50f, 0.30f, 0.60f, 0.50f, 0.40f, 0.20f, 0.10f, 0.30f, 0.10f, 0.50f }, // O
{ 0.20f, 0.30f, 0.10f,-0.10f, 0.20f, 0.40f,-0.10f,-0.10f, 0.20f, 0.10f, 0.20f, 0.10f, 0.10f, 0.00f, 0.10f, 0.00f, 0.00f, 0.10f }, // P
{ 0.20f, 0.30f, 0.10f, 0.00f, 0.40f, 0.40f,-0.10f,-0.10f, 0.30f, 0.30f, 0.10f, 0.00f, 0.10f, 0.00f, 0.40f, 0.10f, 0.00f, 0.10f }, // S
{ 0.00f,-0.20f,-0.20f,-0.30f,-0.10f,-0.10f,-0.50f, 0.80f,-0.10f,-0.10f,-0.10f,-0.30f,-0.10f,-0.30f, 0.00f,-0.10f, 0.00f, 0.00f }, // Na
{ 0.30f,-0.20f,-0.10f,-0.10f,-0.10f,-0.10f, 0.80f,-0.50f, 0.20f, 0.20f, 0.30f,-0.10f, 0.20f,-0.10f, 0.10f, 0.20f, 0.00f, 0.10f }, // Cl
{ 0.10f, 0.10f, 0.20f, 0.50f, 0.20f, 0.30f,-0.10f, 0.20f, 0.20f, 0.20f, 0.10f, 0.10f, 0.10f, 0.00f,-0.10f, 0.00f, 0.00f, 0.00f }, // Fe
{ 0.10f, 0.20f, 0.10f, 0.30f, 0.10f, 0.30f,-0.10f, 0.20f, 0.20f, 0.20f, 0.10f, 0.00f, 0.20f, 0.00f,-0.10f, 0.00f, 0.00f, 0.00f }, // Ni
{ 0.10f, 0.10f, 0.10f, 0.60f, 0.10f, 0.00f, 0.10f, 0.30f, 0.10f, 0.10f, 0.30f, 0.20f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f }, // Si
{ 0.10f, 0.00f,-0.10f, 0.50f, 0.10f, 0.00f,-0.30f,-0.10f, 0.10f, 0.00f, 0.20f, 0.10f, 0.10f, 0.10f, 0.00f, 0.10f, 0.00f, 0.00f }, // Ca
{ 0.00f, 0.10f, 0.00f, 0.40f, 0.10f, 0.10f,-0.10f, 0.20f, 0.10f, 0.20f, 0.10f, 0.10f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f }, // Ti
{ 0.10f, 0.00f,-0.10f, 0.20f, 0.00f, 0.00f,-0.30f,-0.10f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.10f, 0.00f, 0.10f, 0.00f, 0.00f }, // Sr
{ 0.10f, 0.10f, 0.00f, 0.10f, 0.10f, 0.40f, 0.00f, 0.10f,-0.10f,-0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 0.00f }, // Au
{ 0.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.10f,-0.10f, 0.20f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.10f, 0.00f, 0.10f, 0.00f, 0.00f }, // Pb
{ 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.20f, 0.00f }, // Eu
{ 0.00f, 0.00f, 0.10f, 0.50f, 0.10f, 0.10f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.20f }, // U
};

void Particles::gen_default_colors() {
    colors.resize(MAX_PARTICLE_TYPES, glm::vec4(1.0f));
    for (uint32_t i = 0; i < MAX_PARTICLE_TYPES; ++i) {
        if (i < ATOM_COUNT)
            colors[i] = CPK_COLORS[i];
    }
}

static void set_row(std::vector<float>& forces, uint32_t type, float self_val, float cross_val) {
    for (uint32_t b = 0; b < MAX_PARTICLE_TYPES; ++b) {
        forces[type + b * MAX_PARTICLE_TYPES] = (b == type) ? self_val : cross_val;
    }
}

// ── apply_atom_defaults ───────────────────────────────────────────────────────
// Sets CPK colors, chemistry behavior flags, and electrochemistry force matrix
// for all active atom types. Called by gen_data() and the reset UI.

void Particles::apply_atom_defaults(uint32_t active_types, float force_randomness) {
    uint32_t n = std::min(active_types, static_cast<uint32_t>(ATOM_COUNT));

    // CPK colors
    for (uint32_t t = 0; t < n; ++t)
        colors[t] = CPK_COLORS[t];

    // Chemistry behavior flags
    //   H:  POLAR (participates in hydrogen bonds)
    //   C:  NONE (neutral, nonpolar)
    //   N:  DONOR (electron lone pairs)
    //   O:  POLAR | ACCEPTOR (very electronegative)
    //   P:  HEAVY | CATALYST (enzymatic backbone)
    //   S:  HEAVY (disulfide bridges)
    //   Na: HEAVY | IONIC_POS | ADHESIVE
    //   Cl: HEAVY | IONIC_NEG | ADHESIVE
    //   Fe: HEAVY | POLAR (redox-active transition metal)
    //   Ni: HEAVY | CATALYST (nickel catalyst)
    //   Si: HEAVY (semiconductor, silicate network former)
    //   Ca: HEAVY | IONIC_POS (alkaline earth, biological signalling)
    //   Ti: HEAVY (refractory transition metal)
    //   Sr: HEAVY | IONIC_POS (alkaline earth, r-process product)
    //   Au: HEAVY | ADHESIVE (noble metal, inert but surface-reactive)
    //   Pb: HEAVY (dense post-transition metal, r-process end product)
    //   Eu: HEAVY | RADICAL (lanthanide, neutron star merger product)
    //   U:  HEAVY | RADICAL | CATALYST (actinide, fission-relevant)
    static const uint32_t ATOM_FLAGS[ATOM_COUNT] = {
        BEHAVIOR_POLAR,
        BEHAVIOR_NONE,
        BEHAVIOR_DONOR,
        BEHAVIOR_POLAR | BEHAVIOR_ACCEPTOR,
        BEHAVIOR_HEAVY | BEHAVIOR_CATALYST,
        BEHAVIOR_HEAVY,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS | BEHAVIOR_ADHESIVE,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_NEG | BEHAVIOR_ADHESIVE,
        // R-process / supernova elements
        BEHAVIOR_HEAVY | BEHAVIOR_POLAR,                       // Fe
        BEHAVIOR_HEAVY | BEHAVIOR_CATALYST,                    // Ni
        BEHAVIOR_HEAVY,                                        // Si
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS,                   // Ca
        BEHAVIOR_HEAVY,                                        // Ti
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS,                   // Sr
        BEHAVIOR_HEAVY | BEHAVIOR_ADHESIVE,                    // Au
        BEHAVIOR_HEAVY,                                        // Pb
        BEHAVIOR_HEAVY | BEHAVIOR_RADICAL,                     // Eu
        BEHAVIOR_HEAVY | BEHAVIOR_RADICAL | BEHAVIOR_CATALYST, // U
    };
    for (uint32_t t = 0; t < n; ++t)
        behavior_flags[t] = ATOM_FLAGS[t];

    // Per-atom mass tiers (OR'd in after base flags)
    static const uint32_t ATOM_MASS_FLAGS[ATOM_COUNT] = {
        0u,                   // H   (1 amu)   — lightest
        BEHAVIOR_MASS_MEDIUM, // C   (12 amu)
        BEHAVIOR_MASS_MEDIUM, // N   (14 amu)
        BEHAVIOR_MASS_MEDIUM, // O   (16 amu)
        BEHAVIOR_MASS_HEAVY,  // P   (31 amu)
        BEHAVIOR_MASS_HEAVY,  // S   (32 amu)
        BEHAVIOR_MASS_HEAVY,  // Na  (23 amu)
        BEHAVIOR_MASS_HEAVY,  // Cl  (35 amu)
        BEHAVIOR_MASS_DENSE,  // Fe  (56 amu)
        BEHAVIOR_MASS_DENSE,  // Ni  (59 amu)
        BEHAVIOR_MASS_DENSE,  // Si  (28 amu)
        BEHAVIOR_MASS_DENSE,  // Ca  (40 amu)
        BEHAVIOR_MASS_DENSE,  // Ti  (48 amu)
        BEHAVIOR_MASS_ULTRA,  // Sr  (88 amu)
        BEHAVIOR_MASS_ULTRA,  // Au  (197 amu)
        BEHAVIOR_MASS_ULTRA,  // Pb  (207 amu)
        BEHAVIOR_MASS_ULTRA,  // Eu  (152 amu)
        BEHAVIOR_MASS_ULTRA,  // U   (238 amu)
    };
    for (uint32_t t = 0; t < n; ++t)
        behavior_flags[t] |= ATOM_MASS_FLAGS[t];

    // Electrochemistry force matrix — blended with random noise for emergent variation
    for (uint32_t a = 0; a < n; ++a)
        for (uint32_t b = 0; b < n; ++b) {
            float chem = CHEM_FORCE[a][b];
            float rnd  = rand_range_f(-1.0f, 1.0f);
            forces[a + b * MAX_PARTICLE_TYPES] = chem + (rnd - chem) * force_randomness;
        }

    // Always set up photon type (type index 18) — zero forces, BEHAVIOR_PHOTON flag
    setup_photon_type();
    // Set up Standard Model free particle types (19-23)
    setup_sm_types();
}

// ── setup_photon_type ─────────────────────────────────────────────────────────

void Particles::setup_photon_type() {
    // Photons: bright yellow-white glow
    if (colors.size() > PHOTON_TYPE)
        colors[PHOTON_TYPE] = { 1.0f, 1.0f, 0.55f, 1.0f };
    // Zero mass (very light), no bonding, travels ballistic
    behavior_flags[PHOTON_TYPE] = static_cast<uint32_t>(BEHAVIOR_PHOTON);
    // Zero force row/column (photons don't interact via force matrix)
    for (uint32_t k = 0; k < MAX_PARTICLE_TYPES; ++k) {
        forces[PHOTON_TYPE + k * MAX_PARTICLE_TYPES] = 0.0f;
        forces[k + PHOTON_TYPE * MAX_PARTICLE_TYPES] = 0.0f;
    }
}

// ── setup_sm_types ────────────────────────────────────────────────────────────
// Standard Model free particle types 19-29.
// Colors, behavior flags, and zero force-matrix rows. Not spawned by default.

void Particles::setup_sm_types() {
    // Zero force rows/columns for all SM types so they don't accidentally
    // attract/repel through the chemistry force matrix.
    static constexpr uint32_t SM_TYPES[] = {
        ALPHA_TYPE, ELECTRON_TYPE, POSITRON_TYPE, NEUTRINO_TYPE, MUON_TYPE,
        TAU_TYPE, MU_NEUTRINO_TYPE, TAU_NEUTRINO_TYPE, W_BOSON_TYPE, Z_BOSON_TYPE, HIGGS_TYPE
    };
    for (uint32_t t : SM_TYPES) {
        if (t >= MAX_PARTICLE_TYPES) continue;
        for (uint32_t k = 0; k < MAX_PARTICLE_TYPES; ++k) {
            forces[t + k * MAX_PARTICLE_TYPES] = 0.0f;
            forces[k + t * MAX_PARTICLE_TYPES] = 0.0f;
        }
    }

    // Alpha particle (He-4 nucleus): yellow-green, HEAVY + IONIC_POS
    if (colors.size() > ALPHA_TYPE) {
        colors[ALPHA_TYPE]         = { 0.75f, 0.95f, 0.3f, 1.0f };
        behavior_flags[ALPHA_TYPE] = BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS | BEHAVIOR_ALPHA;
    }

    // Free electron: bright blue, light, negative charge
    if (colors.size() > ELECTRON_TYPE) {
        colors[ELECTRON_TYPE]         = { 0.2f, 0.6f, 1.0f, 1.0f };
        behavior_flags[ELECTRON_TYPE] = BEHAVIOR_LEPTON | BEHAVIOR_IONIC_NEG;
    }

    // Positron (e+): hot orange, positive charge, annihilates with electron
    if (colors.size() > POSITRON_TYPE) {
        colors[POSITRON_TYPE]         = { 1.0f, 0.4f, 0.1f, 1.0f };
        behavior_flags[POSITRON_TYPE] = BEHAVIOR_POSITRON | BEHAVIOR_IONIC_POS;
    }

    // Electron neutrino (νe): near-invisible dim grey; no force interactions
    if (colors.size() > NEUTRINO_TYPE) {
        colors[NEUTRINO_TYPE]         = { 0.4f, 0.4f, 0.5f, 0.5f };
        behavior_flags[NEUTRINO_TYPE] = BEHAVIOR_NEUTRINO;
    }

    // Muon (μ-): vivid purple heavy lepton; decays to e- + νe + νμ (CPU TTL)
    if (colors.size() > MUON_TYPE) {
        colors[MUON_TYPE]         = { 0.7f, 0.2f, 0.9f, 1.0f };
        behavior_flags[MUON_TYPE] = BEHAVIOR_MUON | BEHAVIOR_LEPTON | BEHAVIOR_IONIC_NEG;
    }

    // Tau lepton (τ-): bright crimson; heavier than muon, decays similarly
    if (colors.size() > TAU_TYPE) {
        colors[TAU_TYPE]         = { 0.95f, 0.1f, 0.3f, 1.0f };
        behavior_flags[TAU_TYPE] = BEHAVIOR_MUON | BEHAVIOR_LEPTON | BEHAVIOR_IONIC_NEG;
    }

    // Muon neutrino (νμ): faint amber-grey; ballistic, near-zero interaction
    if (colors.size() > MU_NEUTRINO_TYPE) {
        colors[MU_NEUTRINO_TYPE]         = { 0.5f, 0.45f, 0.3f, 0.45f };
        behavior_flags[MU_NEUTRINO_TYPE] = BEHAVIOR_NEUTRINO;
    }

    // Tau neutrino (ντ): faint teal-grey; ballistic, near-zero interaction
    if (colors.size() > TAU_NEUTRINO_TYPE) {
        colors[TAU_NEUTRINO_TYPE]         = { 0.3f, 0.5f, 0.45f, 0.45f };
        behavior_flags[TAU_NEUTRINO_TYPE] = BEHAVIOR_NEUTRINO;
    }

    // W boson (W±): pulsing orange-gold; charged weak-force carrier, very short-lived
    if (colors.size() > W_BOSON_TYPE) {
        colors[W_BOSON_TYPE]         = { 1.0f, 0.65f, 0.0f, 1.0f };
        behavior_flags[W_BOSON_TYPE] = BEHAVIOR_PHOTON;  // ballistic, fast energy drain
    }

    // Z boson (Z0): silver-white; neutral weak-force carrier, very short-lived
    if (colors.size() > Z_BOSON_TYPE) {
        colors[Z_BOSON_TYPE]         = { 0.85f, 0.85f, 0.9f, 1.0f };
        behavior_flags[Z_BOSON_TYPE] = BEHAVIOR_PHOTON;  // ballistic, fast energy drain
    }

    // Higgs boson (H0): golden shimmer; scalar field quantum, very short-lived
    if (colors.size() > HIGGS_TYPE) {
        colors[HIGGS_TYPE]         = { 1.0f, 0.88f, 0.2f, 1.0f };
        behavior_flags[HIGGS_TYPE] = BEHAVIOR_HEAVY | BEHAVIOR_CATALYST;
    }
}

// ── gen_particles ─────────────────────────────────────────────────────────────

void Particles::gen_particles(const SimConfig& cfg) {
    positions.clear();
    velocities.clear();
    types.clear();

    const float rw = static_cast<float>(REGION_W);
    const float rh = static_cast<float>(REGION_H);

    // ── Empty-world reservoir ──────────────────────────────────────────────────
    // Fills the full particle_count slots with dormant H atoms at energy=0.
    // The F3 spawner recycles these slots; the world starts visually empty.
    // Lab Mode (env 0) always forces empty start.
    if (cfg.environment_mode == 0 || cfg.start_empty) {
        uint32_t pool = std::max(10u, cfg.particle_count);
        for (uint32_t i = 0; i < pool; ++i)
            add_particle({ rand_range_f(0.0f, rw), rand_range_f(0.0f, rh) },
                         glm::vec2(0.0f), 0u); // H, no velocity
        angles.assign(pool, 0.0f);
        angular_velocities.assign(pool, 0.0f);
        energies.assign(pool, 0.0f);          // dark / near-invisible
        birth_frames.assign(pool, 0u);
        orbital_parent.assign(pool, -1);
        genomes.assign(pool * GENOME_SIZE, 0.0f);
        // Stamp H atom genome defaults
        for (uint32_t i = 0; i < pool; ++i) {
            genomes[i*4+0] = 0.3f;  // charge
            genomes[i*4+1] = 0.6f;  // electronegativity
            genomes[i*4+2] = 1.0f;  // reactivity
            genomes[i*4+3] = 0.3f;  // bond_strength
        }
        return;
    }

    if (cfg.particle_count == 2) {
        add_particle(glm::vec2(rw / 2.0f - 30.0f, rh / 2.0f),
                     glm::vec2(0.0f), 0u);
        add_particle(glm::vec2(rw / 2.0f + 30.0f, rh / 2.0f),
                     glm::vec2(0.0f), 1u);
        angles.assign(2, 0.0f);
        angular_velocities.assign(2, 0.0f);
        energies.assign(2, 1.0f);
        birth_frames.assign(2, 0u);
        orbital_parent.assign(2, -1);
        genomes.assign(2 * GENOME_SIZE, 0.0f);
        // H defaults
        genomes[0] =  0.3f; genomes[1] = 0.6f; genomes[2] = 1.0f; genomes[3] = 0.3f;
        // C defaults
        genomes[4] =  0.0f; genomes[5] = 0.6f; genomes[6] = 0.8f; genomes[7] = 0.5f;
        return;
    }

    // ── Environment abundance tables ────────────────────────────────────────
    // Index: H  C  N  O  P  S  Na Cl Fe Ni Si Ca Ti Sr Au Pb Eu U
    static const float ENV_ABUNDANCE[9][ATOM_COUNT] = {
        // 0: Lab Mode — not used (start_empty path)
        { 0.0f },
        // 1: Tide Pool — salt water + organics
        { 0.60f, 0.04f, 0.03f, 0.16f, 0.01f, 0.01f, 0.05f, 0.05f,
          0.01f, 0.0f, 0.0f, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        // 2: Hydrothermal Vent — hot, sulfur/iron rich
        { 0.35f, 0.05f, 0.05f, 0.15f, 0.02f, 0.12f, 0.0f, 0.0f,
          0.10f, 0.02f, 0.06f, 0.05f, 0.01f, 0.0f, 0.01f, 0.0f, 0.0f, 0.01f },
        // 3: Primordial Soup — early Earth organics
        { 0.30f, 0.20f, 0.15f, 0.20f, 0.03f, 0.03f, 0.01f, 0.01f,
          0.03f, 0.01f, 0.01f, 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f },
        // 4: Freshwater Pond — mostly water + trace minerals
        { 0.72f, 0.02f, 0.01f, 0.19f, 0.0f, 0.0f, 0.01f, 0.01f,
          0.01f, 0.0f, 0.0f, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        // 5: Deep Space — sparse hydrogen
        { 0.88f, 0.03f, 0.02f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f,
          0.01f, 0.0f, 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        // 6: Nebula — dense H cloud with trace metals
        { 0.82f, 0.05f, 0.03f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f,
          0.02f, 0.01f, 0.01f, 0.0f, 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        // 7: Asteroid Surface — heavy/rocky elements
        { 0.02f, 0.05f, 0.0f, 0.18f, 0.0f, 0.05f, 0.0f, 0.0f,
          0.22f, 0.10f, 0.18f, 0.08f, 0.05f, 0.02f, 0.02f, 0.02f, 0.01f, 0.0f },
        // 8: Comet — ice + dust + organics
        { 0.40f, 0.12f, 0.05f, 0.25f, 0.02f, 0.02f, 0.0f, 0.0f,
          0.04f, 0.01f, 0.04f, 0.01f, 0.01f, 0.0f, 0.0f, 0.01f, 0.0f, 0.02f },
    };
    const float* abundance = ENV_ABUNDANCE[std::min(cfg.environment_mode, 8u)];

    uint32_t n_active = std::min(cfg.particle_types, static_cast<uint32_t>(ATOM_COUNT));
    float cum[ATOM_COUNT + 1];
    cum[0] = 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < n_active; ++i) total += abundance[i];
    for (uint32_t i = 0; i < n_active; ++i) cum[i+1] = cum[i] + abundance[i] / total;
    cum[n_active] = 1.0f; // clamp

    // ── Per-environment spatial distribution ────────────────────────────────
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        glm::vec2 pos;

        switch (cfg.environment_mode) {
            case 2: { // Hydrothermal Vent — plume rising from bottom-center
                float cx = rw * 0.5f;
                float plume_sx = rw * 0.06f;
                if (rand_range_f(0.0f, 1.0f) < 0.70f) {
                    // Plume column: narrow x, biased toward bottom
                    float py = rh - std::abs(gauss(rng_)) * rh * 0.4f;
                    py = std::clamp(py, 0.0f, rh - 1.0f);
                    pos = glm::vec2(
                        std::clamp(cx + gauss(rng_) * plume_sx, 0.0f, rw - 1.0f),
                        py);
                } else {
                    // Sparse warm-water background
                    pos = glm::vec2(rand_range_f(0.0f, rw), rand_range_f(0.0f, rh));
                }
                break;
            }
            case 6: { // Nebula — dense Gaussian cloud at center
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float sx = rw * 0.25f, sy = rh * 0.25f;
                pos = glm::vec2(
                    std::clamp(cx + gauss(rng_) * sx, 0.0f, rw - 1.0f),
                    std::clamp(cy + gauss(rng_) * sy, 0.0f, rh - 1.0f));
                break;
            }
            case 7: { // Asteroid — dense rocky core + scattered debris
                float cx = rw * 0.5f, cy = rh * 0.5f;
                float core_s = rw * 0.08f;
                if (rand_range_f(0.0f, 1.0f) < 0.80f) {
                    pos = glm::vec2(
                        std::clamp(cx + gauss(rng_) * core_s, 0.0f, rw - 1.0f),
                        std::clamp(cy + gauss(rng_) * core_s, 0.0f, rh - 1.0f));
                } else {
                    // Scattered debris
                    pos = glm::vec2(rand_range_f(0.0f, rw), rand_range_f(0.0f, rh));
                }
                break;
            }
            case 8: { // Comet — dense nucleus + elongated tail
                float nx = rw * 0.25f, ny = rh * 0.5f;
                float nuc_s = rw * 0.05f;
                if (rand_range_f(0.0f, 1.0f) < 0.55f) {
                    // Dense icy nucleus
                    pos = glm::vec2(
                        std::clamp(nx + gauss(rng_) * nuc_s, 0.0f, rw - 1.0f),
                        std::clamp(ny + gauss(rng_) * nuc_s, 0.0f, rh - 1.0f));
                } else {
                    // Tail streaming rightward, widening with distance
                    float tx = nx + std::abs(gauss(rng_)) * rw * 0.35f;
                    float spread = (tx - nx) / rw * 0.3f;
                    float ty = ny + gauss(rng_) * rh * (0.03f + spread);
                    pos = glm::vec2(
                        std::clamp(tx, 0.0f, rw - 1.0f),
                        std::clamp(ty, 0.0f, rh - 1.0f));
                }
                break;
            }
            default: // Uniform fill (Tide Pool, Primordial Soup, Freshwater Pond, Deep Space)
                pos = glm::vec2(rand_range_f(0.0f, rw), rand_range_f(0.0f, rh));
                break;
        }

        // Sample type from abundance distribution
        float r = rand_range_f(0.0f, 1.0f);
        uint32_t t = 0;
        for (uint32_t k = 0; k < n_active; ++k)
            if (r >= cum[k] && r < cum[k+1]) { t = k; break; }
        add_particle(pos, glm::vec2(0.0f), t);
    }

    angles.resize(cfg.particle_count);
    angular_velocities.assign(cfg.particle_count, 0.0f);
    for (uint32_t i = 0; i < cfg.particle_count; ++i)
        angles[i] = rand_range_f(0.0f, 6.28318f);

    energies.assign(cfg.particle_count, 1.0f);
    birth_frames.assign(cfg.particle_count, 0u);
    orbital_parent.assign(cfg.particle_count, -1);

    // ── Chemistry genome defaults ─────────────────────────────────────────────
    // Per atom: [0]=charge [1]=electronegativity [2]=reactivity [3]=bond_strength
    static const float BASE_CHARGE[ATOM_COUNT] = {
         0.3f, 0.0f,-0.1f,-0.4f,-0.1f,-0.2f, 0.8f,-0.8f,  // H C N O P S Na Cl
         0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f,  // Fe Ni Si Ca Ti Sr Au Pb
         0.0f, 0.0f                                         // Eu U
    };
    static const float BASE_ELECTRONEG[ATOM_COUNT] = {
         0.6f, 0.6f, 0.9f, 1.6f, 0.8f, 0.9f, 0.3f, 1.1f,
         0.7f, 0.7f, 0.8f, 0.5f, 0.7f, 0.5f, 0.6f, 0.6f,
         0.6f, 0.6f
    };
    static const float BASE_REACTIVITY[ATOM_COUNT] = {
         1.0f, 0.8f, 1.2f, 1.4f, 1.0f, 1.1f, 0.6f, 0.8f,
         0.8f, 0.7f, 0.6f, 0.5f, 0.7f, 0.5f, 0.5f, 0.4f,
         1.0f, 1.2f
    };
    static const float BASE_BOND_STR[ATOM_COUNT] = {
         0.3f, 0.5f, 0.4f, 0.4f, 0.6f, 0.5f, 0.2f, 0.2f,
         0.4f, 0.3f, 0.4f, 0.2f, 0.3f, 0.2f, 0.2f, 0.3f,
         0.3f, 0.4f
    };

    genomes.resize(cfg.particle_count * GENOME_SIZE);
    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        uint32_t t = std::min(types[i], static_cast<uint32_t>(ATOM_COUNT - 1));
        genomes[i*4+0] = std::clamp(BASE_CHARGE[t]    + rand_range_f(-0.05f, 0.05f), -1.0f, 1.0f);
        genomes[i*4+1] = std::clamp(BASE_ELECTRONEG[t] + rand_range_f(-0.1f,  0.1f),  0.1f, 2.0f);
        genomes[i*4+2] = std::clamp(BASE_REACTIVITY[t] + rand_range_f(-0.1f,  0.1f),  0.1f, 2.0f);
        genomes[i*4+3] = std::clamp(BASE_BOND_STR[t]   + rand_range_f(-0.05f, 0.05f), -0.5f, 0.5f);
    }
}

void Particles::add_particle(glm::vec2 pos, glm::vec2 vel, uint32_t type) {
    positions.push_back(pos);
    velocities.push_back(vel);
    types.push_back(type);
}

void Particles::gen_random_force_matrix() {
    forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES);
    for (auto& f : forces)
        f = rand_range_f(-1.0f, 1.0f);
}

void Particles::gen_empty_force_matrix() {
    forces.assign(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
}

int Particles::rand_range_i(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng_);
}

float Particles::rand_range_f(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

// ── apply_preset_atom ─────────────────────────────────────────────────────────
// Sets the flags and force row for one type to match its atom chemistry.

void Particles::apply_preset_atom(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES || type >= ATOM_COUNT) return;
    colors[type] = CPK_COLORS[type];
    static const uint32_t ATOM_FLAGS[ATOM_COUNT] = {
        BEHAVIOR_POLAR,
        BEHAVIOR_NONE,
        BEHAVIOR_DONOR,
        BEHAVIOR_POLAR | BEHAVIOR_ACCEPTOR,
        BEHAVIOR_HEAVY | BEHAVIOR_CATALYST,
        BEHAVIOR_HEAVY,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS | BEHAVIOR_ADHESIVE,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_NEG | BEHAVIOR_ADHESIVE,
        BEHAVIOR_HEAVY | BEHAVIOR_POLAR,
        BEHAVIOR_HEAVY | BEHAVIOR_CATALYST,
        BEHAVIOR_HEAVY,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS,
        BEHAVIOR_HEAVY,
        BEHAVIOR_HEAVY | BEHAVIOR_IONIC_POS,
        BEHAVIOR_HEAVY | BEHAVIOR_ADHESIVE,
        BEHAVIOR_HEAVY,
        BEHAVIOR_HEAVY | BEHAVIOR_RADICAL,
        BEHAVIOR_HEAVY | BEHAVIOR_RADICAL | BEHAVIOR_CATALYST,
    };
    behavior_flags[type] = ATOM_FLAGS[type];
    uint32_t n = std::min(active_types, static_cast<uint32_t>(ATOM_COUNT));
    for (uint32_t b = 0; b < n; ++b)
        forces[type + b * MAX_PARTICLE_TYPES] = CHEM_FORCE[type][b];
    (void)active_types;
}

// ── Legacy/convenience presets (map to nearest chemistry analog) ─────────────

void Particles::apply_preset_default(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_NONE;
}

void Particles::apply_preset_repeller(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_REPEL;
    set_row(forces, type, -0.5f, -0.5f);
}

void Particles::apply_preset_polar(uint32_t type, uint32_t active_types) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_POLAR;
    set_row(forces, type, 0.4f, 0.15f);
    (void)active_types;
}

void Particles::apply_preset_heavy(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_HEAVY;
    set_row(forces, type, -0.2f, -0.2f);
}

void Particles::apply_preset_catalyst(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_CATALYST;
    set_row(forces, type, 0.1f, 0.2f);
}

void Particles::apply_preset_adhesive(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_ADHESIVE;
    set_row(forces, type, 0.8f, 0.2f);
}

void Particles::apply_preset_radical(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_RADICAL;
    set_row(forces, type, 0.6f, 0.6f);
}

void Particles::apply_preset_donor(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_DONOR;
    set_row(forces, type, 0.2f, 0.0f);
}

void Particles::apply_preset_acceptor(uint32_t type) {
    if (type >= MAX_PARTICLE_TYPES) return;
    behavior_flags[type] = BEHAVIOR_ACCEPTOR;
    set_row(forces, type, 0.3f, 0.1f);
}
