// ppel_gen.cpp — Standalone PPEL element file generator
// Generates .ppel files for individual elements with proper nuclear structure and electron shells.
//
// Usage:
//   ppel_gen <out_dir> [Z_min] [Z_max]     Generate .ppel for elements Z_min..Z_max (default 1..118)
//   ppel_gen <out.ppel> <Z>                 Generate single element
//
// Build:
//   g++ -O2 -std=c++17 -o ../../build/ppel_gen ppel_gen.cpp -lm

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>
#include <vector>
#include <sys/stat.h>

// ── Format constants (must match save_load.cpp) ─────────────────────────────

static constexpr uint32_t PPEL_MAGIC   = 0x4C455050;  // "PPEL" little-endian
static constexpr uint32_t PPEL_VERSION = 1;
static constexpr size_t   GENOME_SIZE  = 4;

// ── Sub-atomic types ────────────────────────────────────────────────────────

static constexpr uint32_t PROTON_TYPE        = 0;
static constexpr uint32_t NEUTRON_TYPE       = 1;
static constexpr uint32_t ELECTRON_TYPE_PHYS = 2;

// ── Element data ────────────────────────────────────────────────────────────

static const char* ELEMENT_SYMBOLS[] = {
    "n",  // Z=0 placeholder
    "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne",
    "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",  "Ca",
    "Sc", "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn",
    "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y",  "Zr",
    "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn",
    "Sb", "Te", "I",  "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
    "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb",
    "Lu", "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au", "Hg",
    "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th",
    "Pa", "U",  "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
    "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds",
    "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"
};

// Most common isotope neutron counts
static const int MOST_STABLE_N[] = {
    0,    // Z=0
    0,    // H-1
    2,    // He-4
    4,    // Li-7
    5,    // Be-9
    6,    // B-11
    6,    // C-12
    7,    // N-14
    8,    // O-16
    10,   // F-19
    10,   // Ne-20
    12,   // Na-23
    12,   // Mg-24
    14,   // Al-27
    14,   // Si-28
    16,   // P-31
    16,   // S-32
    18,   // Cl-35
    22,   // Ar-40
    20,   // K-39
    20,   // Ca-40
    24,   // Sc-45
    26,   // Ti-48
    28,   // V-51
    28,   // Cr-52
    30,   // Mn-55
    30,   // Fe-56
    32,   // Co-59
    30,   // Ni-58
    34,   // Cu-63
    34,   // Zn-64
    38,   // Ga-69
    41,   // Ge-73
    42,   // As-75
    45,   // Se-80
    44,   // Br-79
    48,   // Kr-84
    48,   // Rb-85
    50,   // Sr-88
    50,   // Y-89
    51,   // Zr-90
    52,   // Nb-93
    54,   // Mo-96
    55,   // Tc-98
    57,   // Ru-101
    58,   // Rh-103
    60,   // Pd-106
    61,   // Ag-108
    64,   // Cd-112
    66,   // In-115
    69,   // Sn-119
    70,   // Sb-121
    76,   // Te-128
    74,   // I-127
    78,   // Xe-132
    78,   // Cs-133
    81,   // Ba-137
    82,   // La-139
    82,   // Ce-140
    82,   // Pr-141
    84,   // Nd-144
    84,   // Pm-145
    88,   // Sm-150
    89,   // Eu-153
    93,   // Gd-157
    94,   // Tb-159
    96,   // Dy-164
    98,   // Ho-165
    99,   // Er-167
    100,  // Tm-169
    103,  // Yb-173
    104,  // Lu-175
    106,  // Hf-178
    108,  // Ta-181
    110,  // W-184
    111,  // Re-185
    114,  // Os-190
    115,  // Ir-193
    117,  // Pt-195
    118,  // Au-197
    121,  // Hg-201
    123,  // Tl-204
    126,  // Pb-208
    126,  // Bi-209
    125,  // Po-209
    125,  // At-210
    136,  // Rn-222
    136,  // Fr-223
    138,  // Ra-226
    138,  // Ac-227
    142,  // Th-232
    140,  // Pa-231
    146,  // U-238
    144,  // Np-237
    150,  // Pu-244
    148,  // Am-243
    151,  // Cm-247
    150,  // Bk-247
    153,  // Cf-251
    153,  // Es-252
    157,  // Fm-257
    157,  // Md-258
    157,  // No-259
    159,  // Lr-262
    163,  // Rf-267
    163,  // Db-268
    163,  // Sg-269
    163,  // Bh-270
    169,  // Hs-277
    169,  // Mt-278
    171,  // Ds-281
    171,  // Rg-282
    173,  // Cn-285
    173,  // Nh-286
    175,  // Fl-289
    175,  // Mc-290
    177,  // Lv-293
    177,  // Ts-294
    176,  // Og-294
};

// ── Binary I/O ──────────────────────────────────────────────────────────────

template <typename T>
static void write_val(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

// ── Particle data ───────────────────────────────────────────────────────────

struct ParticleData {
    float    dx = 0, dy = 0;
    float    vx = 0, vy = 0;
    float    energy = 0;
    uint32_t type = 0;
    float    genome[GENOME_SIZE] = {};
};

// ── Nucleus layout ──────────────────────────────────────────────────────────

static constexpr float NUC_SPACING = 3.8f;

static void layout_nucleus(int Z, int N, std::vector<ParticleData>& out, std::mt19937& rng) {
    int total = Z + N;
    if (total == 0) return;

    // Interleave protons and neutrons
    std::vector<uint32_t> types;
    int p = 0, n = 0;
    for (int i = 0; i < total; ++i) {
        if (p < Z && (n >= N || i % 2 == 0)) { types.push_back(PROTON_TYPE); p++; }
        else if (n < N) { types.push_back(NEUTRON_TYPE); n++; }
        else { types.push_back(PROTON_TYPE); p++; }
    }

    // Place in concentric rings
    std::vector<std::pair<float,float>> pos;
    if (total == 1) {
        pos.push_back({0, 0});
    } else {
        pos.push_back({0, 0}); // center
        int placed = 1, ring = 1;
        while (placed < total) {
            float r = ring * NUC_SPACING;
            int cap = std::max(6, static_cast<int>(std::round(2.0 * M_PI * ring)));
            int to_place = std::min(cap, total - placed);
            for (int k = 0; k < to_place; ++k) {
                float angle = 2.0f * M_PI * k / cap;
                pos.push_back({r * cosf(angle), r * sinf(angle)});
                placed++;
            }
            ring++;
        }
    }

    std::uniform_int_distribution<int> coin(0, 1);
    for (int i = 0; i < total; ++i) {
        ParticleData pd;
        pd.dx = pos[i].first;
        pd.dy = pos[i].second;
        pd.type = types[i];
        pd.energy = 0.5f;
        pd.genome[0] = (types[i] == PROTON_TYPE) ? 1.0f : 0.0f;
        pd.genome[1] = coin(rng) ? 0.5f : -0.5f;
        out.push_back(pd);
    }
}

// ── Electron shell layout ───────────────────────────────────────────────────

static void layout_electrons(int Z, int num_e, std::vector<ParticleData>& out, std::mt19937& rng) {
    if (num_e <= 0) return;

    static const int SHELL_CAP[] = {2, 8, 8, 18, 18, 32, 32};
    static const float SHELL_R[] = {12, 24, 38, 52, 68, 84, 100};
    int num_shells = 7;

    // Nuclear extent
    float nuc_ext = 0;
    if (Z > 1) {
        int rings = 1, count = 1;
        while (count < Z) {
            int cap = std::max(6, static_cast<int>(std::round(2.0 * M_PI * rings)));
            count += std::min(cap, Z - count);
            rings++;
        }
        nuc_ext = (rings - 1) * NUC_SPACING + 1.9f;
    }

    std::uniform_int_distribution<int> coin(0, 1);
    int remaining = num_e;
    int shell = 0;
    while (remaining > 0 && shell < num_shells) {
        int in_shell = std::min(remaining, SHELL_CAP[shell]);
        float r = nuc_ext + SHELL_R[shell];
        for (int k = 0; k < in_shell; ++k) {
            float angle = 2.0f * M_PI * k / in_shell + shell * 2.399f;
            ParticleData pd;
            pd.dx = r * cosf(angle);
            pd.dy = r * sinf(angle);
            pd.type = ELECTRON_TYPE_PHYS;
            pd.energy = 0.2f;
            pd.genome[0] = -1.0f;
            pd.genome[1] = coin(rng) ? 0.5f : -0.5f;
            out.push_back(pd);
        }
        remaining -= in_shell;
        shell++;
    }
}

// ── Export .ppel ─────────────────────────────────────────────────────────────

static bool export_ppel(const std::string& path, int Z, int N, int electrons,
                         const std::vector<ParticleData>& particles) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    write_val(f, PPEL_MAGIC);
    write_val(f, PPEL_VERSION);
    write_val(f, static_cast<int32_t>(Z));
    write_val(f, static_cast<int32_t>(N));
    write_val(f, static_cast<int32_t>(electrons));

    uint32_t count = static_cast<uint32_t>(particles.size());
    write_val(f, count);
    for (const auto& p : particles) {
        write_val(f, p.dx);
        write_val(f, p.dy);
        write_val(f, p.vx);
        write_val(f, p.vy);
        write_val(f, p.energy);
        write_val(f, p.type);
        f.write(reinterpret_cast<const char*>(p.genome), GENOME_SIZE * sizeof(float));
    }

    return f.good();
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  ppel_gen <out_dir> [Z_min] [Z_max]   Generate range of elements\n");
        fprintf(stderr, "  ppel_gen <out.ppel> <Z>               Generate single element\n");
        return 1;
    }

    std::string arg1 = argv[1];

    // Single element mode: ppel_gen <out.ppel> <Z>
    if (arg1.size() > 5 && arg1.substr(arg1.size()-5) == ".ppel") {
        if (argc < 3) { fprintf(stderr, "Need Z argument\n"); return 1; }
        int Z = std::atoi(argv[2]);
        if (Z < 1 || Z > 118) { fprintf(stderr, "Z must be 1..118\n"); return 1; }
        int N = MOST_STABLE_N[Z];

        std::mt19937 rng(Z * 1000 + N);
        std::vector<ParticleData> particles;
        layout_nucleus(Z, N, particles, rng);
        layout_electrons(Z, Z, particles, rng);

        if (!export_ppel(arg1, Z, N, Z, particles)) {
            fprintf(stderr, "Failed to write %s\n", arg1.c_str());
            return 1;
        }
        printf("Generated: %s (%s, Z=%d, N=%d, %zu particles)\n",
               arg1.c_str(), ELEMENT_SYMBOLS[Z], Z, N, particles.size());
        return 0;
    }

    // Directory mode: ppel_gen <out_dir> [Z_min] [Z_max]
    std::string dir = arg1;
    if (dir.back() != '/') dir += '/';

    int z_min = 1, z_max = 118;
    if (argc >= 3) z_min = std::atoi(argv[2]);
    if (argc >= 4) z_max = std::atoi(argv[3]);
    if (z_min < 1) z_min = 1;
    if (z_max > 118) z_max = 118;

    // Create directory if needed
    mkdir(dir.c_str(), 0755);

    int generated = 0;
    for (int Z = z_min; Z <= z_max; ++Z) {
        int N = MOST_STABLE_N[Z];

        std::mt19937 rng(Z * 1000 + N);
        std::vector<ParticleData> particles;
        layout_nucleus(Z, N, particles, rng);
        layout_electrons(Z, Z, particles, rng);

        // Filename: Symbol-MassNumber.ppel  (e.g., H-1.ppel, He-4.ppel, U-238.ppel)
        char fname[128];
        snprintf(fname, sizeof(fname), "%s%s-%d.ppel", dir.c_str(),
                 ELEMENT_SYMBOLS[Z], Z + N);

        if (!export_ppel(fname, Z, N, Z, particles)) {
            fprintf(stderr, "Failed: %s\n", fname);
            continue;
        }
        generated++;
    }

    printf("Generated %d element files in %s\n", generated, dir.c_str());
    return 0;
}
