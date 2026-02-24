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
    apply_atom_defaults(cfg.particle_types);

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

void Particles::apply_atom_defaults(uint32_t active_types) {
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

    // Electrochemistry force matrix
    for (uint32_t a = 0; a < n; ++a)
        for (uint32_t b = 0; b < n; ++b)
            forces[a + b * MAX_PARTICLE_TYPES] = CHEM_FORCE[a][b];

    // Always set up photon type (type index 8) — zero forces, BEHAVIOR_PHOTON flag
    setup_photon_type();
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

// ── gen_particles ─────────────────────────────────────────────────────────────

void Particles::gen_particles(const SimConfig& cfg) {
    positions.clear();
    velocities.clear();
    types.clear();

    const float rw = static_cast<float>(REGION_W);
    const float rh = static_cast<float>(REGION_H);

    // ── Empty-world reservoir ──────────────────────────────────────────────────
    // Generates a quiet pool of dormant H atoms at energy=0 that the F3 spawner
    // can recycle. They render at 12% brightness (nearly invisible dark dots).
    if (cfg.start_empty) {
        uint32_t pool = std::max(10u, cfg.pool_size);
        for (uint32_t i = 0; i < pool; ++i)
            add_particle({ rand_range_f(0.0f, rw), rand_range_f(0.0f, rh) },
                         glm::vec2(0.0f), 0u); // H, no velocity
        angles.assign(pool, 0.0f);
        angular_velocities.assign(pool, 0.0f);
        energies.assign(pool, 0.0f);          // dark / near-invisible
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
        genomes.assign(2 * GENOME_SIZE, 0.0f);
        // H defaults
        genomes[0] =  0.3f; genomes[1] = 0.6f; genomes[2] = 1.0f; genomes[3] = 0.3f;
        // C defaults
        genomes[4] =  0.0f; genomes[5] = 0.6f; genomes[6] = 0.8f; genomes[7] = 0.5f;
        return;
    }

    // ── Atom abundance ratios (cumulative) ───────────────────────────────────
    // H=40%, C=25%, O=15%, N=10%, P=2%, S=2%, Na=3%, Cl=3%
    // R-process / supernova elements are very rare (< 0.5% each)
    // Renormalise to the number of active types so the user's type-count slider
    // still determines which atoms appear.
    static const float RAW_ABUNDANCE[ATOM_COUNT] = {
        0.400f, // H
        0.250f, // C
        0.100f, // N
        0.150f, // O
        0.020f, // P
        0.020f, // S
        0.030f, // Na
        0.030f, // Cl
        // ── R-process / supernova elements ────────────────────────────────────
        0.005f, // Fe  (most common heavy metal in cosmos after alpha process)
        0.002f, // Ni
        0.005f, // Si  (major silicate planet component)
        0.003f, // Ca
        0.001f, // Ti
        0.001f, // Sr  (first confirmed kilonova r-process product)
        0.001f, // Au  (gold, neutron star merger)
        0.001f, // Pb  (r-process end point)
        0.001f, // Eu  (lanthanide, neutron star diagnostic)
        0.001f, // U   (actinide, r-process endpoint)
    };
    uint32_t n_active = std::min(cfg.particle_types, static_cast<uint32_t>(ATOM_COUNT));
    float cum[ATOM_COUNT + 1];
    cum[0] = 0.0f;
    float total = 0.0f;
    for (uint32_t i = 0; i < n_active; ++i) total += RAW_ABUNDANCE[i];
    for (uint32_t i = 0; i < n_active; ++i) cum[i+1] = cum[i] + RAW_ABUNDANCE[i] / total;
    cum[n_active] = 1.0f; // clamp

    // ── Non-uniform clustered spawn ───────────────────────────────────────────
    // Generate 4-8 random cluster centres so matter starts as separated blobs.
    int n_clusters = 4 + static_cast<int>(rng_() % 5);
    std::vector<glm::vec2> cluster_centers(n_clusters);
    for (auto& c : cluster_centers)
        c = { rand_range_f(rw * 0.08f, rw * 0.92f),
              rand_range_f(rh * 0.08f, rh * 0.92f) };

    // Each cluster has its own sigma (spread) for variety
    std::vector<float> cluster_sigma(n_clusters);
    for (auto& s : cluster_sigma)
        s = rand_range_f(rh * 0.06f, rh * 0.22f);

    std::normal_distribution<float> gauss(0.0f, 1.0f);

    for (uint32_t i = 0; i < cfg.particle_count; ++i) {
        // Assign to a random cluster
        int ci = static_cast<int>(rng_() % n_clusters);
        glm::vec2 pos;
        // Rejection-sample to keep within world bounds
        int tries = 0;
        do {
            float sig = cluster_sigma[ci];
            pos = { cluster_centers[ci].x + gauss(rng_) * sig,
                    cluster_centers[ci].y + gauss(rng_) * sig };
            ++tries;
        } while ((pos.x < 0.0f || pos.x >= rw || pos.y < 0.0f || pos.y >= rh) && tries < 20);
        pos.x = std::clamp(pos.x, 0.0f, rw - 1.0f);
        pos.y = std::clamp(pos.y, 0.0f, rh - 1.0f);

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
