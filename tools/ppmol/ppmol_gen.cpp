// ppmol_gen.cpp — Standalone PPMOL molecule generator, loader, and renderer
//
// Compatible with EmergentEvolution save_load.cpp (GENOME_SIZE=4, atom-based types).
//
// Commands:
//   gen <out.ppmol> <formula>   Generate .ppmol from chemical formula (e.g. "H2O", "C6H12O6")
//   dump <file.ppmol>           Print molecule info
//   validate <file.ppmol>       Check file integrity
//   copy <in.ppmol> <out.ppmol> Round-trip copy
//   render <in.ppmol> <out.png> Render to PNG
//
// Build:
//   g++ -O2 -std=c++17 -I../  -o ../../build/ppmol_gen ppmol_gen.cpp -lm

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../src/third_party/stb_image_write.h"

// ── Format constants (must match save_load.cpp) ─────────────────────────────

static constexpr uint32_t PPMOL_MAGIC   = 0x4D4F4C50;  // "PLOM" little-endian
static constexpr uint32_t PPMOL_VERSION = 3;  // v3 adds chirality fields
static constexpr size_t   GENOME_SIZE   = 4;  // charge, spin, color_charge, decay_rate

// ── Particle type indices (must match types.h / phys_particles.h) ───────────

// Atom types 0-17
static constexpr uint32_t TYPE_H  = 0;
static constexpr uint32_t TYPE_C  = 1;
static constexpr uint32_t TYPE_N  = 2;
static constexpr uint32_t TYPE_O  = 3;
static constexpr uint32_t TYPE_P  = 4;
static constexpr uint32_t TYPE_S  = 5;
static constexpr uint32_t TYPE_NA = 6;
static constexpr uint32_t TYPE_CL = 7;
static constexpr uint32_t TYPE_FE = 8;
static constexpr uint32_t TYPE_NI = 9;
static constexpr uint32_t TYPE_SI = 10;
static constexpr uint32_t TYPE_CA = 11;
static constexpr uint32_t TYPE_TI = 12;
static constexpr uint32_t TYPE_SR = 13;
static constexpr uint32_t TYPE_AU = 14;
static constexpr uint32_t TYPE_PB = 15;
static constexpr uint32_t TYPE_EU = 16;
static constexpr uint32_t TYPE_U  = 17;

// Special particle types
static constexpr uint32_t TYPE_PHOTON   = 18;
static constexpr uint32_t TYPE_ALPHA    = 19;
static constexpr uint32_t TYPE_ELECTRON = 20;  // free electron
static constexpr uint32_t TYPE_POSITRON = 21;
static constexpr uint32_t TYPE_NEUTRINO = 22;

// Physics sub-atomic types (from phys_particles.h)
static constexpr uint32_t PROTON_TYPE         = 0;  // type 0 = H = proton in physics sim
static constexpr uint32_t NEUTRON_TYPE        = 1;  // type 1 = C slot doubles as neutron
static constexpr uint32_t ELECTRON_TYPE_PHYS  = 2;  // type 2 = N slot doubles as electron

// ── Atomic number → atom type index mapping ─────────────────────────────────
// The simulation's type system uses atom indices 0-17 for specific elements.

struct ElementEntry {
    uint32_t Z;           // atomic number
    uint32_t type_index;  // simulation particle type
    const char* symbol;
    uint32_t default_N;   // most common isotope neutron count
};

static const ElementEntry ELEMENT_TABLE[] = {
    { 1, TYPE_H,   "H",  0 },
    { 6, TYPE_C,   "C",  6 },
    { 7, TYPE_N,   "N",  7 },
    { 8, TYPE_O,   "O",  8 },
    {15, TYPE_P,   "P",  16},
    {16, TYPE_S,   "S",  16},
    {11, TYPE_NA,  "Na", 12},
    {17, TYPE_CL,  "Cl", 18},
    {26, TYPE_FE,  "Fe", 30},
    {28, TYPE_NI,  "Ni", 30},
    {14, TYPE_SI,  "Si", 14},
    {20, TYPE_CA,  "Ca", 20},
    {22, TYPE_TI,  "Ti", 26},
    {38, TYPE_SR,  "Sr", 50},
    {79, TYPE_AU,  "Au", 118},
    {82, TYPE_PB,  "Pb", 126},
    {63, TYPE_EU,  "Eu", 89},
    {92, TYPE_U,   "U",  146},
    // Additional common elements mapped to nearest type
    { 9, TYPE_CL,  "F",  10},  // Fluorine → Cl slot (halogen)
    {35, TYPE_CL,  "Br", 44},  // Bromine → Cl slot (halogen)
    {53, TYPE_CL,  "I",  74},  // Iodine → Cl slot (halogen)
    {19, TYPE_NA,  "K",  20},  // Potassium → Na slot (alkali)
    {12, TYPE_CA,  "Mg", 12},  // Magnesium → Ca slot (alkaline earth)
    { 5, TYPE_C,   "B",  6 },  // Boron → C slot
    { 3, TYPE_NA,  "Li", 4 },  // Lithium → Na slot
    {30, TYPE_NI,  "Zn", 34},  // Zinc → Ni slot
    {29, TYPE_FE,  "Cu", 34},  // Copper → Fe slot
    {33, TYPE_P,   "As", 42},  // Arsenic → P slot
    {34, TYPE_S,   "Se", 45},  // Selenium → S slot
    { 2, TYPE_H,   "He", 2 },  // Helium → H slot
    {10, TYPE_H,   "Ne", 10},  // Neon → H slot (noble gas)
    {18, TYPE_CL,  "Ar", 22},  // Argon → Cl slot
    // Extended elements for materials/superconductors
    {13, TYPE_SI,  "Al", 14},  // Aluminium → Si slot
    {31, TYPE_SI,  "Ga", 38},  // Gallium → Si slot
    {32, TYPE_SI,  "Ge", 41},  // Germanium → Si slot
    {41, TYPE_FE,  "Nb", 52},  // Niobium → Fe slot (transition metal)
    {42, TYPE_FE,  "Mo", 54},  // Molybdenum → Fe slot
    {23, TYPE_TI,  "V",  28},  // Vanadium → Ti slot
    {24, TYPE_FE,  "Cr", 28},  // Chromium → Fe slot
    {25, TYPE_FE,  "Mn", 30},  // Manganese → Fe slot
    {74, TYPE_FE,  "W",  110}, // Tungsten → Fe slot
    {40, TYPE_TI,  "Zr", 51},  // Zirconium → Ti slot
    {50, TYPE_PB,  "Sn", 69},  // Tin → Pb slot
    {56, TYPE_CA,  "Ba", 81},  // Barium → Ca slot (alkaline earth)
    {47, TYPE_AU,  "Ag", 61},  // Silver → Au slot
    {48, TYPE_NI,  "Cd", 64},  // Cadmium → Ni slot
    {49, TYPE_SI,  "In", 66},  // Indium → Si slot
    {39, TYPE_TI,  "Y",  50},  // Yttrium → Ti slot
};
static constexpr int ELEMENT_TABLE_SIZE = sizeof(ELEMENT_TABLE) / sizeof(ELEMENT_TABLE[0]);

static const ElementEntry* lookup_element_by_Z(uint32_t Z) {
    for (int i = 0; i < ELEMENT_TABLE_SIZE; ++i)
        if (ELEMENT_TABLE[i].Z == Z) return &ELEMENT_TABLE[i];
    return nullptr;
}

static const ElementEntry* lookup_element_by_symbol(const std::string& sym) {
    for (int i = 0; i < ELEMENT_TABLE_SIZE; ++i)
        if (sym == ELEMENT_TABLE[i].symbol) return &ELEMENT_TABLE[i];
    return nullptr;
}

// ── Data structures (matches save_load.h) ───────────────────────────────────

struct SaveResult {
    bool success = false;
    std::string message;
};

struct ParticleData {
    float    dx       = 0.0f;   // offset from atom center
    float    dy       = 0.0f;
    float    vx       = 0.0f;
    float    vy       = 0.0f;
    float    energy   = 0.0f;
    uint32_t type     = 0;
    float    genome[GENOME_SIZE] = {};  // charge, spin, color_charge, decay_rate
};

struct AtomData {
    int32_t Z          = 0;
    int32_t N          = 0;
    int32_t electrons  = 0;
    float   cx_offset  = 0.0f;   // offset from molecule center
    float   cy_offset  = 0.0f;
    std::vector<ParticleData> particles;
};

struct BondData {
    int32_t atom_a = 0;
    int32_t atom_b = 0;
};

struct ImportResult {
    bool success = false;
    std::string message;
    std::string formula;
    std::string name;       // common name (v2+), empty for v1 files
    bool is_chiral = false;         // v3+: molecule has chiral centers
    uint32_t chiral_centers = 0;    // v3+: number of chiral center atoms
    std::vector<AtomData> atoms;
    std::vector<BondData> bonds;
};

// ── Binary I/O helpers ──────────────────────────────────────────────────────

static inline bool host_is_little_endian() {
    const uint16_t x = 0x0102;
    unsigned char b[2];
    std::memcpy(b, &x, 2);
    return (b[0] == 0x02);
}

template <typename T>
static T byteswap_trivial(T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::array<unsigned char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    std::reverse(bytes.begin(), bytes.end());
    T result{};
    std::memcpy(&result, bytes.data(), sizeof(T));
    return result;
}

template <typename T>
static void write_val(std::ostream& os, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    T tmp = value;
    if (!host_is_little_endian() && sizeof(T) > 1)
        tmp = byteswap_trivial(tmp);
    os.write(reinterpret_cast<const char*>(&tmp), sizeof(T));
}

template <typename T>
static bool read_val(std::istream& is, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    T tmp{};
    is.read(reinterpret_cast<char*>(&tmp), sizeof(T));
    if (!is.good()) return false;
    if (!host_is_little_endian() && sizeof(T) > 1)
        tmp = byteswap_trivial(tmp);
    out = tmp;
    return true;
}

// ── Export / Import ─────────────────────────────────────────────────────────

static SaveResult export_molecule(
    const std::string& filepath,
    const std::string& formula,
    const std::vector<AtomData>& atoms,
    const std::vector<BondData>& bonds,
    const std::string& name = "",
    bool is_chiral = false,
    uint32_t chiral_centers = 0)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open())
        return { false, "Cannot open file for writing" };

    write_val(f, PPMOL_MAGIC);
    write_val(f, PPMOL_VERSION);

    uint32_t flen = static_cast<uint32_t>(formula.size());
    write_val(f, flen);
    f.write(formula.data(), flen);

    // Name (v2+)
    uint32_t nlen = static_cast<uint32_t>(name.size());
    write_val(f, nlen);
    if (nlen > 0) f.write(name.data(), nlen);

    // Chirality (v3+)
    uint8_t chiral_flag = is_chiral ? 1 : 0;
    write_val(f, chiral_flag);
    write_val(f, chiral_centers);

    uint32_t atom_count = static_cast<uint32_t>(atoms.size());
    uint32_t bond_count = static_cast<uint32_t>(bonds.size());
    write_val(f, atom_count);
    write_val(f, bond_count);

    for (const auto& a : atoms) {
        write_val(f, a.Z);
        write_val(f, a.N);
        write_val(f, a.electrons);
        write_val(f, a.cx_offset);
        write_val(f, a.cy_offset);

        uint32_t pcount = static_cast<uint32_t>(a.particles.size());
        write_val(f, pcount);

        for (const auto& p : a.particles) {
            write_val(f, p.dx);
            write_val(f, p.dy);
            write_val(f, p.vx);
            write_val(f, p.vy);
            write_val(f, p.energy);
            write_val(f, p.type);
            f.write(reinterpret_cast<const char*>(p.genome), GENOME_SIZE * sizeof(float));
        }
    }

    for (const auto& b : bonds) {
        write_val(f, b.atom_a);
        write_val(f, b.atom_b);
    }

    if (!f.good())
        return { false, "Write error" };

    return { true, "OK" };
}

static ImportResult import_molecule(const std::string& filepath) {
    ImportResult r;
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) { r.message = "Cannot open file"; return r; }

    uint32_t magic = 0, version = 0;
    if (!read_val(f, magic) || !read_val(f, version)) {
        r.message = "Truncated file (header)"; return r;
    }
    if (magic != PPMOL_MAGIC) { r.message = "Not a valid .ppmol file"; return r; }
    if (version < 1 || version > 3) { r.message = "Unsupported .ppmol version"; return r; }

    uint32_t flen = 0;
    if (!read_val(f, flen)) { r.message = "Truncated file (formula len)"; return r; }
    if (flen > 256) { r.message = "Formula too long"; return r; }
    r.formula.resize(flen);
    f.read(r.formula.data(), flen);
    if (!f.good()) { r.message = "Truncated file (formula)"; return r; }

    // Name (v2+)
    if (version >= 2) {
        uint32_t nlen = 0;
        if (!read_val(f, nlen)) { r.message = "Truncated file (name len)"; return r; }
        if (nlen > 256) { r.message = "Name too long"; return r; }
        if (nlen > 0) {
            r.name.resize(nlen);
            f.read(r.name.data(), nlen);
            if (!f.good()) { r.message = "Truncated file (name)"; return r; }
        }
    }

    // Chirality (v3+)
    if (version >= 3) {
        uint8_t chiral_flag = 0;
        if (!read_val(f, chiral_flag)) { r.message = "Truncated file (chirality)"; return r; }
        r.is_chiral = (chiral_flag != 0);
        if (!read_val(f, r.chiral_centers)) { r.message = "Truncated file (chiral_centers)"; return r; }
    }

    uint32_t atom_count = 0, bond_count = 0;
    if (!read_val(f, atom_count) || !read_val(f, bond_count)) {
        r.message = "Truncated file (counts)"; return r;
    }
    if (atom_count > 1000) { r.message = "Too many atoms"; return r; }
    if (bond_count > 10000) { r.message = "Too many bonds"; return r; }

    r.atoms.resize(atom_count);
    for (uint32_t ai = 0; ai < atom_count; ++ai) {
        auto& a = r.atoms[ai];
        if (!read_val(f, a.Z) || !read_val(f, a.N) || !read_val(f, a.electrons) ||
            !read_val(f, a.cx_offset) || !read_val(f, a.cy_offset)) {
            r.message = "Truncated file (atom header)"; return r;
        }

        uint32_t pcount = 0;
        if (!read_val(f, pcount)) { r.message = "Truncated file (pcount)"; return r; }
        if (pcount > 10000) { r.message = "Atom too large"; return r; }

        a.particles.resize(pcount);
        for (uint32_t pi = 0; pi < pcount; ++pi) {
            auto& p = a.particles[pi];
            if (!read_val(f, p.dx) || !read_val(f, p.dy) ||
                !read_val(f, p.vx) || !read_val(f, p.vy) ||
                !read_val(f, p.energy) || !read_val(f, p.type)) {
                r.message = "Truncated file (particle)"; return r;
            }
            f.read(reinterpret_cast<char*>(p.genome), GENOME_SIZE * sizeof(float));
            if (!f.good()) { r.message = "Truncated file (genome)"; return r; }
        }
    }

    r.bonds.resize(bond_count);
    for (uint32_t bi = 0; bi < bond_count; ++bi) {
        if (!read_val(f, r.bonds[bi].atom_a) || !read_val(f, r.bonds[bi].atom_b)) {
            r.message = "Truncated file (bonds)"; return r;
        }
    }

    r.success = true;
    r.message = "OK";
    return r;
}

// ── Chemical formula parser ─────────────────────────────────────────────────
// Parses formulas like "H2O", "C6H12O6", "C10H14BrNO2", "Ca(OH)2", "Fe2O3"

struct FormulaEntry {
    std::string symbol;
    int count;
};

static bool parse_formula(const std::string& formula, std::vector<FormulaEntry>& out) {
    out.clear();
    size_t i = 0;
    size_t n = formula.size();

    // Stack for parenthesized groups
    std::vector<std::vector<FormulaEntry>> stack;
    stack.push_back({});

    while (i < n) {
        if (formula[i] == '(') {
            stack.push_back({});
            i++;
        } else if (formula[i] == ')') {
            i++;
            int count = 0;
            while (i < n && formula[i] >= '0' && formula[i] <= '9') {
                count = count * 10 + (formula[i] - '0');
                i++;
            }
            if (count == 0) count = 1;

            if (stack.size() < 2) return false;
            auto group = std::move(stack.back());
            stack.pop_back();
            for (auto& e : group) {
                e.count *= count;
                stack.back().push_back(e);
            }
        } else if (formula[i] >= 'A' && formula[i] <= 'Z') {
            std::string sym(1, formula[i]);
            i++;
            while (i < n && formula[i] >= 'a' && formula[i] <= 'z') {
                sym += formula[i];
                i++;
            }
            int count = 0;
            while (i < n && formula[i] >= '0' && formula[i] <= '9') {
                count = count * 10 + (formula[i] - '0');
                i++;
            }
            if (count == 0) count = 1;
            stack.back().push_back({sym, count});
        } else {
            return false;  // unexpected character
        }
    }

    if (stack.size() != 1) return false;  // unmatched parentheses
    out = std::move(stack[0]);
    return true;
}

// ── Nucleus layout: ring-based proton/neutron placement ─────────────────────

static constexpr float NUC_SPACING = 3.8f;  // must match simulation NUC_SPACING

static void layout_nucleus(int Z, int N, std::vector<std::pair<float,float>>& positions,
                           std::vector<uint32_t>& types)
{
    positions.clear();
    types.clear();

    int total = Z + N;
    if (total == 0) return;

    // Interleave protons and neutrons
    std::vector<uint32_t> nuc_types;
    nuc_types.reserve(total);
    int p = 0, n = 0;
    for (int i = 0; i < total; ++i) {
        if (p < Z && (n >= N || i % 2 == 0)) {
            nuc_types.push_back(PROTON_TYPE);
            p++;
        } else if (n < N) {
            nuc_types.push_back(NEUTRON_TYPE);
            n++;
        } else {
            nuc_types.push_back(PROTON_TYPE);
            p++;
        }
    }

    // Place nucleons in concentric rings
    if (total == 1) {
        positions.push_back({0.0f, 0.0f});
        types.push_back(nuc_types[0]);
        return;
    }

    int placed = 0;
    // Center nucleon
    positions.push_back({0.0f, 0.0f});
    types.push_back(nuc_types[placed++]);

    int ring = 1;
    while (placed < total) {
        float r = ring * NUC_SPACING;
        int capacity = static_cast<int>(std::round(2.0 * M_PI * ring)); // ~6*ring
        if (capacity < 6) capacity = 6;
        int to_place = std::min(capacity, total - placed);
        for (int k = 0; k < to_place; ++k) {
            float angle = 2.0f * M_PI * k / capacity;
            positions.push_back({r * std::cos(angle), r * std::sin(angle)});
            types.push_back(nuc_types[placed++]);
        }
        ring++;
    }
}

// ── Electron shell layout ───────────────────────────────────────────────────

static void layout_electrons(int Z, int num_electrons,
                             std::vector<std::pair<float,float>>& positions)
{
    positions.clear();
    if (num_electrons <= 0) return;

    // Shell capacities: 1s(2), 2s2p(8), 3s3p(8), 3d4s(18)...
    static const int SHELL_CAP[] = {2, 8, 8, 18, 18, 32, 32};
    static const float SHELL_RADIUS[] = {12.0f, 24.0f, 38.0f, 52.0f, 68.0f, 84.0f, 100.0f};
    int num_shells = sizeof(SHELL_CAP) / sizeof(SHELL_CAP[0]);

    // Nuclear extent for offset
    float nuc_extent = 0.0f;
    if (Z > 1) {
        int rings = 1;
        int count = 1;
        while (count < Z) {
            int cap = static_cast<int>(std::round(2.0 * M_PI * rings));
            if (cap < 6) cap = 6;
            count += std::min(cap, Z - count);
            rings++;
        }
        nuc_extent = (rings - 1) * NUC_SPACING + 1.9f;
    }

    int remaining = num_electrons;
    int shell = 0;
    while (remaining > 0 && shell < num_shells) {
        int in_shell = std::min(remaining, SHELL_CAP[shell]);
        float r = nuc_extent + SHELL_RADIUS[shell];
        for (int k = 0; k < in_shell; ++k) {
            float angle = 2.0f * M_PI * k / in_shell + shell * 2.399f; // golden angle offset
            positions.push_back({r * std::cos(angle), r * std::sin(angle)});
        }
        remaining -= in_shell;
        shell++;
    }
    // Overflow electrons go in last valid shell
    float r_last = nuc_extent + SHELL_RADIUS[std::min(shell, num_shells - 1)];
    int overflow_start = num_electrons - remaining;
    for (int k = 0; k < remaining; ++k) {
        float angle = 2.0f * M_PI * k / (remaining + 1);
        positions.push_back({r_last * std::cos(angle), r_last * std::sin(angle)});
    }
}

// ── Build genome for a particle type ────────────────────────────────────────

static void make_genome(float genome[GENOME_SIZE], uint32_t type, std::mt19937& rng) {
    // genome[0] = charge, [1] = spin, [2] = color_charge, [3] = decay_rate
    float charge = 0.0f, spin = 0.5f, color = 0.0f, decay = 0.0f;

    // Simple defaults for the types we generate
    switch (type) {
        case PROTON_TYPE:         charge =  1.0f; spin = 0.5f; break;
        case NEUTRON_TYPE:        charge =  0.0f; spin = 0.5f; break;
        case ELECTRON_TYPE_PHYS:  charge = -1.0f; spin = 0.5f; break;
    }

    // Randomize spin sign for fermions
    if (std::abs(spin) == 0.5f) {
        std::uniform_int_distribution<int> coin(0, 1);
        spin = coin(rng) ? 0.5f : -0.5f;
    }

    genome[0] = charge;
    genome[1] = spin;
    genome[2] = color;
    genome[3] = decay;
}

// ── Generate atom particles ─────────────────────────────────────────────────

static AtomData build_atom(int Z, int N, int electrons, float cx, float cy, std::mt19937& rng) {
    AtomData atom;
    atom.Z = Z;
    atom.N = N;
    atom.electrons = electrons;
    atom.cx_offset = cx;
    atom.cy_offset = cy;

    // Layout nucleus
    std::vector<std::pair<float,float>> nuc_pos;
    std::vector<uint32_t> nuc_types;
    layout_nucleus(Z, N, nuc_pos, nuc_types);

    for (size_t i = 0; i < nuc_pos.size(); ++i) {
        ParticleData p;
        p.dx = nuc_pos[i].first;
        p.dy = nuc_pos[i].second;
        p.type = nuc_types[i];
        p.energy = 0.5f;  // rest mass in energy buffer units
        make_genome(p.genome, p.type, rng);
        atom.particles.push_back(p);
    }

    // Layout electrons
    std::vector<std::pair<float,float>> e_pos;
    layout_electrons(Z, electrons, e_pos);

    for (auto& ep : e_pos) {
        ParticleData p;
        p.dx = ep.first;
        p.dy = ep.second;
        p.type = ELECTRON_TYPE_PHYS;
        p.energy = 0.2f;
        make_genome(p.genome, p.type, rng);
        atom.particles.push_back(p);
    }

    return atom;
}

// ── Rendering ───────────────────────────────────────────────────────────────

struct RGB { uint8_t r, g, b; };

static RGB get_atom_color(int Z) {
    // CPK coloring for common elements
    switch (Z) {
        case 1:  return {255, 255, 255}; // H - white
        case 6:  return {144, 144, 144}; // C - grey
        case 7:  return { 48,  80, 248}; // N - blue
        case 8:  return {255,  13,  13}; // O - red
        case 9:  return {144, 224,  80}; // F - green
        case 15: return {255, 128,   0}; // P - orange
        case 16: return {255, 255,  48}; // S - yellow
        case 11: return {171,  92, 242}; // Na - violet
        case 17: return { 31, 240,  31}; // Cl - green
        case 26: return {224, 102,  51}; // Fe - rust
        case 35: return {166,  41,  41}; // Br - dark red
        case 53: return {148,   0, 148}; // I - violet
        case 14: return {240, 200, 160}; // Si - beige
        case 20: return { 61, 255,   0}; // Ca - lime
        case 79: return {255, 209,  35}; // Au - gold
        case 82: return { 87,  89,  97}; // Pb - dark grey
        case 92: return {  0, 143,   0}; // U - green
        default: return {200, 200, 200};
    }
}

static void draw_circle(uint8_t* img, int w, int h, int cx, int cy, int radius, RGB color) {
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx*dx + dy*dy <= r2) {
                int px = cx + dx, py = cy + dy;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    int idx = (py * w + px) * 3;
                    img[idx+0] = color.r; img[idx+1] = color.g; img[idx+2] = color.b;
                }
            }
        }
    }
}

static void draw_line(uint8_t* img, int w, int h, int x0, int y0, int x1, int y1,
                     RGB color, int thickness = 2)
{
    int dx = std::abs(x1-x0), dy = std::abs(y1-y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        for (int ty = -thickness/2; ty <= thickness/2; ++ty)
            for (int tx = -thickness/2; tx <= thickness/2; ++tx) {
                int px = x0+tx, py = y0+ty;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    int idx = (py*w+px)*3;
                    img[idx+0] = color.r; img[idx+1] = color.g; img[idx+2] = color.b;
                }
            }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static bool render_molecule(const std::string& png_path, const ImportResult& mol,
                            int img_w = 1024, int img_h = 1024, float scale = 50.0f,
                            bool show_particles = true)
{
    std::vector<uint8_t> img(img_w * img_h * 3, 32);  // dark background

    int cx = img_w / 2, cy = img_h / 2;

    // Draw bonds
    RGB bond_color = {100, 100, 100};
    for (const auto& b : mol.bonds) {
        if (b.atom_a < 0 || b.atom_b < 0) continue;
        size_t a = static_cast<size_t>(b.atom_a);
        size_t bx = static_cast<size_t>(b.atom_b);
        if (a >= mol.atoms.size() || bx >= mol.atoms.size()) continue;
        int x1 = cx + static_cast<int>(mol.atoms[a].cx_offset * scale);
        int y1 = cy + static_cast<int>(mol.atoms[a].cy_offset * scale);
        int x2 = cx + static_cast<int>(mol.atoms[bx].cx_offset * scale);
        int y2 = cy + static_cast<int>(mol.atoms[bx].cy_offset * scale);
        draw_line(img.data(), img_w, img_h, x1, y1, x2, y2, bond_color, 3);
    }

    // Draw atoms
    for (const auto& atom : mol.atoms) {
        int ax = cx + static_cast<int>(atom.cx_offset * scale);
        int ay = cy + static_cast<int>(atom.cy_offset * scale);

        // Sub-particle dots
        if (show_particles) {
            for (const auto& p : atom.particles) {
                int px = ax + static_cast<int>(p.dx * scale * 0.15f);
                int py = ay + static_cast<int>(p.dy * scale * 0.15f);
                RGB pc;
                switch (p.type) {
                    case PROTON_TYPE:         pc = {255, 100, 100}; break;  // red
                    case NEUTRON_TYPE:        pc = {100, 100, 255}; break;  // blue
                    case ELECTRON_TYPE_PHYS:  pc = {255, 255, 100}; break;  // yellow
                    default:                  pc = {180, 180, 180}; break;
                }
                draw_circle(img.data(), img_w, img_h, px, py, 2, pc);
            }
        }

        // Atom sphere
        RGB ac = get_atom_color(atom.Z);
        int radius = std::max(8, std::min(25, static_cast<int>(std::sqrt(atom.Z) * 4)));
        draw_circle(img.data(), img_w, img_h, ax, ay, radius, ac);

        // Outline
        RGB outline = {200, 200, 200};
        for (int ang = 0; ang < 360; ang += 5) {
            float rad = ang * M_PI / 180.0f;
            int ox = ax + static_cast<int>(radius * std::cos(rad));
            int oy = ay + static_cast<int>(radius * std::sin(rad));
            draw_circle(img.data(), img_w, img_h, ox, oy, 1, outline);
        }
    }

    return stbi_write_png(png_path.c_str(), img_w, img_h, 3, img.data(), img_w * 3) != 0;
}

// ── CLI argument helpers ────────────────────────────────────────────────────

static bool parse_u32(const std::string& s, uint32_t& out) {
    try {
        unsigned long long v = std::stoull(s, nullptr, 0);
        if (v > std::numeric_limits<uint32_t>::max()) return false;
        out = static_cast<uint32_t>(v);
        return true;
    } catch (...) { return false; }
}

static float parse_float_or(const std::string& s, float def) {
    try { return std::stof(s); } catch (...) { return def; }
}

// ── Commands ────────────────────────────────────────────────────────────────

// ── Common molecule name lookup ──────────────────────────────────────────────

struct MoleculeNameEntry { const char* formula; const char* name; };
static const MoleculeNameEntry MOLECULE_NAMES[] = {
    // Diatomic
    {"H2",      "Hydrogen"},
    {"O2",      "Oxygen"},
    {"N2",      "Nitrogen"},
    {"F2",      "Fluorine"},
    {"Cl2",     "Chlorine"},
    {"HF",      "Hydrogen Fluoride"},
    {"HCl",     "Hydrogen Chloride"},
    {"HBr",     "Hydrogen Bromide"},
    {"HI",      "Hydrogen Iodide"},
    {"CO",      "Carbon Monoxide"},
    {"NO",      "Nitric Oxide"},
    {"NaCl",    "Sodium Chloride"},
    {"NaF",     "Sodium Fluoride"},
    {"KCl",     "Potassium Chloride"},
    {"MgO",     "Magnesium Oxide"},
    {"CaO",     "Calcium Oxide"},
    {"LiH",     "Lithium Hydride"},
    {"NaH",     "Sodium Hydride"},
    // Triatomic
    {"H2O",     "Water"},
    {"CO2",     "Carbon Dioxide"},
    {"NO2",     "Nitrogen Dioxide"},
    {"H2S",     "Hydrogen Sulfide"},
    {"O3",      "Ozone"},
    {"SO2",     "Sulfur Dioxide"},
    {"CS2",     "Carbon Disulfide"},
    {"N2O",     "Nitrous Oxide"},
    {"HCN",     "Hydrogen Cyanide"},
    {"SiO2",    "Silicon Dioxide"},
    // 4-atom
    {"NH3",     "Ammonia"},
    {"PH3",     "Phosphine"},
    {"BF3",     "Boron Trifluoride"},
    {"SO3",     "Sulfur Trioxide"},
    {"NaOH",    "Sodium Hydroxide"},
    {"H2O2",    "Hydrogen Peroxide"},
    {"COCl2",   "Phosgene"},
    // 5-atom
    {"CH4",     "Methane"},
    {"SiH4",    "Silane"},
    {"CF4",     "Carbon Tetrafluoride"},
    {"CCl4",    "Carbon Tetrachloride"},
    {"CHCl3",   "Chloroform"},
    {"CH2Cl2",  "Dichloromethane"},
    {"CH3Cl",   "Chloromethane"},
    {"HNO3",    "Nitric Acid"},
    {"HCOOH",   "Formic Acid"},
    // 6+ atom
    {"CH2O",    "Formaldehyde"},
    {"CH3OH",   "Methanol"},
    {"C2H2",    "Acetylene"},
    {"C2H4",    "Ethylene"},
    {"C2H6",    "Ethane"},
    {"C2H5OH",  "Ethanol"},
    {"CH3COOH", "Acetic Acid"},
    {"CH3NH2",  "Methylamine"},
    {"NaHCO3",  "Sodium Bicarbonate"},
    {"H2SO4",   "Sulfuric Acid"},
    {"H3PO4",   "Phosphoric Acid"},
    // Larger organic
    {"C3H8",    "Propane"},
    {"C3H6O",   "Acetone"},
    {"C6H6",    "Benzene"},
    {"C6H12O6", "Glucose"},
    {"C8H10N4O2","Caffeine"},
    {"Fe2O3",   "Iron(III) Oxide"},
    {"CaCO3",   "Calcium Carbonate"},
    {"Ca(OH)2", "Calcium Hydroxide"},
    {"Al2O3",   "Aluminium Oxide"},
    {"CH3OCH3", "Dimethyl Ether"},
    // Gas molecules
    {"C4H10",   "Butane"},
    {"C3H6",    "Propene"},
    {"C2H4O",   "Ethylene Oxide"},
    {"SF6",     "Sulfur Hexafluoride"},
    {"SiF4",    "Silicon Tetrafluoride"},
    {"AsH3",    "Arsine"},
    {"COCl2",   "Phosgene"},
    {"Br2",     "Bromine"},
    {"I2",      "Iodine"},
    {"SO3",     "Sulfur Trioxide"},
    {"ClF3",    "Chlorine Trifluoride"},
    {"NF3",     "Nitrogen Trifluoride"},
    {"BH3",     "Borane"},
    {"B2H6",    "Diborane"},
    {"GeH4",    "Germane"},
    {"H2Se",    "Hydrogen Selenide"},
    {"C5H12",   "Pentane"},
    {"C6H14",   "Hexane"},
    {"C2H3Cl",  "Vinyl Chloride"},
    {"COF2",    "Carbonyl Fluoride"},
    // Pharmaceuticals
    {"C9H8O4",      "Aspirin"},
    {"C13H18O2",    "Ibuprofen"},
    {"C8H9NO2",     "Acetaminophen"},
    {"C10H14N2",    "Nicotine"},
    {"C8H11NO2",    "Dopamine"},
    {"C10H12N2O",   "Serotonin"},
    {"C9H13NO3",    "Epinephrine"},
    {"C13H16N2O2",  "Melatonin"},
    {"C4H11N5",     "Metformin"},
    {"C17H19NO3",   "Morphine"},
    {"C18H21NO3",   "Codeine"},
    {"C6H8O6",      "Ascorbic Acid"},
    {"C16H18N2O4S", "Penicillin G"},
    {"C16H19N3O5S", "Amoxicillin"},
    {"C21H30O2",    "THC"},
    {"C18H27NO3",   "Capsaicin"},
    {"C7H8N4O2",    "Theobromine"},
    {"C14H18N2O5",  "Aspartame"},
    {"C12H22O11",   "Sucrose"},
    {"C9H11NO2",    "Phenylalanine"},
    {"C3H7NO2",     "Alanine"},
    {"C2H5NO2",     "Glycine"},
    {"C5H9NO4",     "Glutamic Acid"},
    {"C9H11NO3",    "Tyrosine"},
    {"C5H11NO2",    "Valine"},
    {"C6H13NO2",    "Leucine"},
    {"C11H12N2O2",  "Tryptophan"},
    // Acids
    {"H2CO3",   "Carbonic Acid"},
    {"C6H8O7",  "Citric Acid"},
    {"C3H6O3",  "Lactic Acid"},
    {"C2H2O4",  "Oxalic Acid"},
    {"C4H6O6",  "Tartaric Acid"},
    {"C4H6O5",  "Malic Acid"},
    {"C7H6O2",  "Benzoic Acid"},
    {"C7H6O3",  "Salicylic Acid"},
    {"H3BO3",   "Boric Acid"},
    {"HClO4",   "Perchloric Acid"},
    {"HBr",     "Hydrobromic Acid"},
    {"HPO3",    "Metaphosphoric Acid"},
    {"C4H4O4",  "Fumaric Acid"},
    {"C3H4O3",  "Pyruvic Acid"},
    {"C4H6O4",  "Succinic Acid"},
    // Bases
    {"KOH",         "Potassium Hydroxide"},
    {"LiOH",        "Lithium Hydroxide"},
    {"Ba(OH)2",     "Barium Hydroxide"},
    {"Mg(OH)2",     "Magnesium Hydroxide"},
    {"C5H5N",       "Pyridine"},
    {"C6H7N",       "Aniline"},
    {"C6H15N",      "Triethylamine"},
    {"C4H9N",       "Pyrrolidine"},
    {"C4H10N2",     "Piperazine"},
    {"C5H11N",      "Piperidine"},
    // DNA/RNA building blocks — nucleobases
    {"C5H5N5",      "Adenine"},
    {"C5H5N5O",     "Guanine"},
    {"C4H5N3O",     "Cytosine"},
    {"C5H6N2O2",    "Thymine"},
    {"C4H4N2O2",    "Uracil"},
    // DNA/RNA — sugars
    {"C5H10O4",     "Deoxyribose"},
    {"C5H10O5",     "Ribose"},
    // DNA/RNA — nucleosides
    {"C10H13N5O3",  "Deoxyadenosine"},
    {"C10H13N5O4",  "Adenosine"},
    {"C10H13N5O5",  "Guanosine"},
    {"C9H13N3O4",   "Deoxycytidine"},
    {"C9H13N3O5",   "Cytidine"},
    {"C10H14N2O5",  "Thymidine"},
    {"C9H12N2O6",   "Uridine"},
    // Energy molecules
    {"C10H16N5O13P3","ATP"},
    {"C10H15N5O10P2","ADP"},
    {"C10H14N5O7P",  "AMP"},
    // Psychoactive / controlled substances
    {"C9H13N",       "Amphetamine"},
    {"C10H15N",      "Methamphetamine"},
    {"C17H21NO4",    "Cocaine"},
    {"C21H23NO5",    "Heroin"},
    {"C22H28N2O",    "Fentanyl"},
    {"C20H25N3O",    "LSD"},
    {"C11H15NO2",    "MDMA"},
    {"C12H17N2O4P",  "Psilocybin"},
    {"C12H16N2",     "DMT"},
    {"C13H16ClNO",   "Ketamine"},
    {"C11H17NO3",    "Mescaline"},
    {"C4H8O3",       "GHB"},
    {"C17H25N",      "PCP"},
    {"C15H21NO2",    "Psilocin"},
    // More pharmaceuticals
    {"C17H18F3NO",   "Fluoxetine"},
    {"C17H17Cl2N",   "Sertraline"},
    {"C16H13ClN2O",  "Diazepam"},
    {"C17H19N3O3S",  "Omeprazole"},
    {"C15H25NO3",    "Metoprolol"},
    {"C21H26O5",     "Prednisone"},
    {"C22H29FO5",    "Dexamethasone"},
    {"C19H16O4",     "Warfarin"},
    {"C5H9NO3S",     "Acetylcysteine"},
    {"C22H23ClN2O2", "Loratadine"},
    {"C22H30N6O4S",  "Sildenafil"},
    {"C14H14O3",     "Naproxen"},
    {"C9H17NO2",     "Gabapentin"},
    {"C18H26ClN3O",  "Hydroxychloroquine"},
    {"C17H18FN3O3",  "Ciprofloxacin"},
    {"C10H13N5O4",   "Acyclovir"},
    {"C8H9NO2",      "Acetaminophen"},
    {"C14H10O4",     "Dantrolene"},
    {"C21H28O2",     "Testosterone"},
    {"C19H24O2",     "Estradiol"},
    {"C26H29NO",     "Tamoxifen"},
    {"C15H12N2O",    "Carbamazepine"},
    {"C8H11NO3",     "Norepinephrine"},
    // Semiconductors and advanced materials
    {"GaAs",     "Gallium Arsenide"},
    {"SiC",      "Silicon Carbide"},
    {"BN",       "Boron Nitride"},
    {"AlN",      "Aluminium Nitride"},
    {"GaN",      "Gallium Nitride"},
    {"ZnO",      "Zinc Oxide"},
    {"CdSe",     "Cadmium Selenide"},
    {"InP",      "Indium Phosphide"},
    {"ZnS",      "Zinc Sulfide"},
    {"TiO2",     "Titanium Dioxide"},
    {"WC",       "Tungsten Carbide"},
    {"ZrO2",     "Zirconium Dioxide"},
    {"SnO2",     "Tin Dioxide"},
    {"BaTiO3",   "Barium Titanate"},
    // Superconducting materials
    {"MgB2",     "Magnesium Diboride"},
    {"NbN",      "Niobium Nitride"},
    {"Nb3Sn",    "Niobium-Tin"},
    {"NbTi",     "Niobium-Titanium"},
    {"V3Si",     "Vanadium-Silicon"},
    // Fullerenes and carbon allotropes
    {"C60",      "Buckminsterfullerene"},
    {"C24",      "Coronene Carbon"},
    {"C20",      "Dodecahedrane Carbon"},
    // Industrial / common chemicals
    {"CaF2",     "Calcium Fluoride"},
    {"Na2CO3",   "Sodium Carbonate"},
    {"KNO3",     "Potassium Nitrate"},
    {"AgNO3",    "Silver Nitrate"},
    {"CuSO4",    "Copper Sulfate"},
    {"FeCl3",    "Iron(III) Chloride"},
    {"AlCl3",    "Aluminium Chloride"},
    {"ZnCl2",    "Zinc Chloride"},
    {"PbO2",     "Lead Dioxide"},
    {"MnO2",     "Manganese Dioxide"},
    {"Cr2O3",    "Chromium(III) Oxide"},
    {"V2O5",     "Vanadium Pentoxide"},
    {"WO3",      "Tungsten Trioxide"},
    {"MoS2",     "Molybdenum Disulfide"},
    {"CaCl2",    "Calcium Chloride"},
    {"NaNO3",    "Sodium Nitrate"},
    {"NH4NO3",   "Ammonium Nitrate"},
    {"NH4Cl",    "Ammonium Chloride"},
    {"Ca3(PO4)2","Tricalcium Phosphate"},
};
static constexpr int MOLECULE_NAMES_SIZE = sizeof(MOLECULE_NAMES) / sizeof(MOLECULE_NAMES[0]);

static const char* lookup_molecule_name(const std::string& formula) {
    for (int i = 0; i < MOLECULE_NAMES_SIZE; ++i)
        if (formula == MOLECULE_NAMES[i].formula) return MOLECULE_NAMES[i].name;
    return nullptr;
}

static void print_help() {
    std::cout << "ppmol_gen — molecule file tool (EmergentEvolution)\n\n";
    std::cout << "Commands:\n";
    std::cout << "  gen <out.ppmol> <formula> [--name NAME] [--seed N] [--scale F] [--no-particles]\n";
    std::cout << "      Generate .ppmol from chemical formula.\n";
    std::cout << "      --name NAME : common name (e.g. \"Water\"). Auto-detected for known molecules.\n";
    std::cout << "      --scale F   : inter-atom spacing in pixels (default: 36 = bond_rest_length)\n";
    std::cout << "      Examples: H2O, C6H12O6, NaCl, Ca(OH)2, C10H14BrNO2\n\n";
    std::cout << "  dump <file.ppmol>\n";
    std::cout << "      Print molecule info.\n\n";
    std::cout << "  validate <file.ppmol>\n";
    std::cout << "      Check file integrity.\n\n";
    std::cout << "  copy <in.ppmol> <out.ppmol>\n";
    std::cout << "      Round-trip copy.\n\n";
    std::cout << "  render <in.ppmol> <out.png> [--width N] [--height N] [--scale F] [--no-particles]\n";
    std::cout << "      Render molecule to PNG.\n";
}

// ── Nuclear extent helper (matches spawning.cpp) ────────────────────────────

static float nuclear_extent(int Z) {
    // Estimate nucleus radius from hex-packed ring layout
    if (Z <= 1) return 1.9f;  // single nucleon
    // Default neutron count approximation
    int N = (Z <= 2) ? Z : Z + static_cast<int>(std::round(Z * 0.31f));
    int A = Z + N;
    int ring = 0, placed = 1;
    while (placed < A) { ++ring; placed += 6 * ring; }
    return ring * NUC_SPACING + 1.9f;
}

// Default inter-atom spacing: must exceed nuclear extents and match bond_rest_length
static constexpr float DEFAULT_ATOM_SPACING = 36.0f;  // matches SimConfig::bond_rest_length
static constexpr float SAFE_MARGIN = 10.0f;            // keep nucleons outside Yukawa range

// ── Chirality detection ─────────────────────────────────────────────────────
// An atom is a chiral center if it can bond to 3+ different element types (Z).
// This matches the simulation's chirality detection in simulation.cpp.

static uint32_t compute_chirality(const std::vector<AtomData>& atoms,
                                   const std::vector<BondData>& bonds)
{
    // Build adjacency: for each atom, collect Z values of bonded neighbors
    uint32_t chiral_count = 0;
    for (size_t i = 0; i < atoms.size(); ++i) {
        int Z = atoms[i].Z;
        // Only C, Si, N, P, S can be chiral centers
        if (Z != 6 && Z != 14 && Z != 7 && Z != 15 && Z != 16) continue;

        std::vector<int> bonded_z;
        for (const auto& b : bonds) {
            if (static_cast<size_t>(b.atom_a) == i)
                bonded_z.push_back(atoms[static_cast<size_t>(b.atom_b)].Z);
            else if (static_cast<size_t>(b.atom_b) == i)
                bonded_z.push_back(atoms[static_cast<size_t>(b.atom_a)].Z);
        }

        if (bonded_z.size() >= 3) {
            std::sort(bonded_z.begin(), bonded_z.end());
            bonded_z.erase(std::unique(bonded_z.begin(), bonded_z.end()), bonded_z.end());
            if (bonded_z.size() >= 3) chiral_count++;
        }
    }
    return chiral_count;
}

static int cmd_gen(int argc, char** argv) {
    if (argc < 4) { print_help(); return 2; }

    std::string out_path = argv[2];
    std::string formula  = argv[3];
    std::string mol_name;
    uint64_t seed = 42;
    float atom_spacing = DEFAULT_ATOM_SPACING;
    bool gen_particles = true;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i+1 < argc) { seed = std::stoull(argv[++i]); }
        else if (arg == "--name" && i+1 < argc) { mol_name = argv[++i]; }
        else if (arg == "--scale" && i+1 < argc) { atom_spacing = parse_float_or(argv[++i], DEFAULT_ATOM_SPACING); }
        else if (arg == "--no-particles") { gen_particles = false; }
    }

    // Auto-detect name if not explicitly provided
    if (mol_name.empty()) {
        const char* known = lookup_molecule_name(formula);
        if (known) mol_name = known;
    }

    // Parse formula
    std::vector<FormulaEntry> entries;
    if (!parse_formula(formula, entries) || entries.empty()) {
        std::cerr << "Error: failed to parse formula '" << formula << "'\n";
        return 1;
    }

    // Resolve elements
    struct AtomSpec {
        const ElementEntry* elem;
        int count;
    };
    std::vector<AtomSpec> specs;
    int total_atoms = 0;
    for (auto& e : entries) {
        auto* elem = lookup_element_by_symbol(e.symbol);
        if (!elem) {
            std::cerr << "Error: unknown element '" << e.symbol << "'\n";
            return 1;
        }
        specs.push_back({elem, e.count});
        total_atoms += e.count;
    }

    if (total_atoms > 1000) {
        std::cerr << "Error: too many atoms (" << total_atoms << " > 1000)\n";
        return 1;
    }

    std::mt19937 rng(seed);

    // 2D layout: arrange atoms in a grid-like pattern, then relax
    std::vector<AtomData> atoms;
    atoms.reserve(total_atoms);

    // Flatten atom list — heavy atoms first, hydrogen last (better bonding)
    std::vector<const ElementEntry*> atom_elems;
    for (auto& s : specs) {
        if (s.elem->Z != 1) {
            for (int i = 0; i < s.count; ++i)
                atom_elems.push_back(s.elem);
        }
    }
    for (auto& s : specs) {
        if (s.elem->Z == 1) {
            for (int i = 0; i < s.count; ++i)
                atom_elems.push_back(s.elem);
        }
    }

    // Layout atoms in concentric rings with simulation-appropriate spacing.
    // Ring radius = ring * atom_spacing so bonded atoms are ~atom_spacing apart.
    if (total_atoms == 1) {
        auto* elem = atom_elems[0];
        atoms.push_back(build_atom(elem->Z, elem->default_N, elem->Z,
                                   0.0f, 0.0f, rng));
    } else {
        int placed = 0;
        int ring = 0;
        while (placed < total_atoms) {
            if (ring == 0) {
                auto* elem = atom_elems[placed];
                if (gen_particles)
                    atoms.push_back(build_atom(elem->Z, elem->default_N, elem->Z,
                                               0.0f, 0.0f, rng));
                else {
                    AtomData a;
                    a.Z = elem->Z; a.N = elem->default_N; a.electrons = elem->Z;
                    atoms.push_back(a);
                }
                placed++;
            } else {
                float r = ring * atom_spacing;
                // Limit atoms per ring so adjacent atoms stay ~atom_spacing apart
                int cap = std::max(3, static_cast<int>(2.0f * M_PI * r / atom_spacing));
                int to_place = std::min(cap, total_atoms - placed);
                for (int k = 0; k < to_place; ++k) {
                    float angle = 2.0f * M_PI * k / cap;
                    float cx = r * std::cos(angle);
                    float cy = r * std::sin(angle);
                    auto* elem = atom_elems[placed];
                    if (gen_particles)
                        atoms.push_back(build_atom(elem->Z, elem->default_N, elem->Z,
                                                   cx, cy, rng));
                    else {
                        AtomData a;
                        a.Z = elem->Z; a.N = elem->default_N; a.electrons = elem->Z;
                        a.cx_offset = cx; a.cy_offset = cy;
                        atoms.push_back(a);
                    }
                    placed++;
                }
            }
            ring++;
        }
    }

    // Safety check: scale up if any pair of atoms is too close for their nuclear extents
    if (atoms.size() >= 2) {
        float max_needed_scale = 1.0f;
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i + 1; j < atoms.size(); ++j) {
                float dx = atoms[i].cx_offset - atoms[j].cx_offset;
                float dy = atoms[i].cy_offset - atoms[j].cy_offset;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 1.0f) continue;
                float safe = nuclear_extent(atoms[i].Z) + nuclear_extent(atoms[j].Z) + SAFE_MARGIN;
                if (safe > dist)
                    max_needed_scale = std::max(max_needed_scale, safe / dist);
            }
        }
        if (max_needed_scale > 1.001f) {
            for (auto& a : atoms) {
                a.cx_offset *= max_needed_scale;
                a.cy_offset *= max_needed_scale;
                // Also scale sub-particle positions relative to the OLD atom center
                // (they're already placed relative to 0,0 within the atom, so only atom offsets need scaling)
            }
        }
    }

    // Generate bonds between adjacent atoms (simple nearest-neighbor heuristic)
    std::vector<BondData> bonds;
    if (atoms.size() >= 2) {
        // For each atom, bond to nearest unmatched neighbors up to valence
        auto valence_of = [](int Z) -> int {
            switch (Z) {
                case 1:  return 1;   // H
                case 6:  return 4;   // C
                case 7:  return 3;   // N
                case 8:  return 2;   // O
                case 15: return 3;   // P (can be 5)
                case 16: return 2;   // S
                case 11: return 1;   // Na
                case 17: return 1;   // Cl
                case 9:  return 1;   // F
                case 35: return 1;   // Br
                case 53: return 1;   // I
                case 14: return 4;   // Si
                default: return 2;
            }
        };

        std::vector<int> bond_count(atoms.size(), 0);
        std::vector<int> max_bonds(atoms.size());
        for (size_t i = 0; i < atoms.size(); ++i)
            max_bonds[i] = valence_of(atoms[i].Z);

        // Build pair list sorted by priority: prefer different-element bonds,
        // penalize H-H bonds (hydrogen almost always bonds to heavier atoms)
        struct Pair { size_t a, b; float score; };
        std::vector<Pair> pairs;
        for (size_t i = 0; i < atoms.size(); ++i) {
            for (size_t j = i+1; j < atoms.size(); ++j) {
                float dx = atoms[i].cx_offset - atoms[j].cx_offset;
                float dy = atoms[i].cy_offset - atoms[j].cy_offset;
                float dist = std::sqrt(dx*dx + dy*dy);
                // Penalize same-element bonds (especially H-H)
                float penalty = 0.0f;
                if (atoms[i].Z == atoms[j].Z) penalty += 100.0f;
                if (atoms[i].Z == 1 && atoms[j].Z == 1) penalty += 500.0f;
                pairs.push_back({i, j, dist + penalty});
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](auto& a, auto& b) { return a.score < b.score; });

        for (auto& p : pairs) {
            if (bond_count[p.a] < max_bonds[p.a] && bond_count[p.b] < max_bonds[p.b]) {
                bonds.push_back({static_cast<int32_t>(p.a), static_cast<int32_t>(p.b)});
                bond_count[p.a]++;
                bond_count[p.b]++;
            }
        }
    }

    // Compute chirality
    uint32_t chiral_n = compute_chirality(atoms, bonds);
    bool is_chiral = (chiral_n > 0);

    auto res = export_molecule(out_path, formula, atoms, bonds, mol_name, is_chiral, chiral_n);

    if (res.success) {
        // Count particles
        int total_particles = 0;
        for (auto& a : atoms) total_particles += static_cast<int>(a.particles.size());

        std::cout << "Generated: " << out_path << "\n";
        std::cout << "  Formula:   " << formula << "\n";
        if (!mol_name.empty())
            std::cout << "  Name:      " << mol_name << "\n";
        std::cout << "  Atoms:     " << atoms.size() << "\n";
        std::cout << "  Bonds:     " << bonds.size() << "\n";
        std::cout << "  Particles: " << total_particles << "\n";
        std::cout << "  Chirality: " << (is_chiral ? "chiral" : "achiral");
        if (chiral_n > 0) std::cout << " (" << chiral_n << " center" << (chiral_n > 1 ? "s" : "") << ")";
        std::cout << "\n";
    } else {
        std::cerr << "Error: " << res.message << "\n";
    }

    return res.success ? 0 : 1;
}

static int cmd_dump(int argc, char** argv) {
    if (argc < 3) { print_help(); return 2; }
    auto r = import_molecule(argv[2]);
    if (!r.success) { std::cerr << "Error: " << r.message << "\n"; return 1; }

    std::cout << "Formula:    " << r.formula << "\n";
    if (!r.name.empty())
        std::cout << "Name:       " << r.name << "\n";
    std::cout << "Chirality:  " << (r.is_chiral ? "chiral" : "achiral");
    if (r.chiral_centers > 0)
        std::cout << " (" << r.chiral_centers << " center" << (r.chiral_centers > 1 ? "s" : "") << ")";
    std::cout << "\n";
    std::cout << "Atoms:      " << r.atoms.size() << "\n";
    std::cout << "Bonds:      " << r.bonds.size() << "\n";

    int total_p = 0;
    for (auto& a : r.atoms) {
        total_p += static_cast<int>(a.particles.size());
    }
    std::cout << "Particles:  " << total_p << "\n\n";

    for (size_t i = 0; i < r.atoms.size(); ++i) {
        auto& a = r.atoms[i];
        const char* sym = "?";
        auto* elem = lookup_element_by_Z(a.Z);
        if (elem) sym = elem->symbol;
        printf("  Atom %zu: %s (Z=%d, N=%d, e=%d) at (%.1f, %.1f) — %zu particles\n",
               i, sym, a.Z, a.N, a.electrons, a.cx_offset, a.cy_offset, a.particles.size());
    }

    // Show inter-atom distances
    if (r.atoms.size() >= 2) {
        std::cout << "\n  Inter-atom distances (px):\n";
        for (size_t i = 0; i < r.atoms.size(); ++i) {
            for (size_t j = i + 1; j < r.atoms.size(); ++j) {
                float dx = r.atoms[i].cx_offset - r.atoms[j].cx_offset;
                float dy = r.atoms[i].cy_offset - r.atoms[j].cy_offset;
                float dist = std::sqrt(dx*dx + dy*dy);
                const char* si = "?"; const char* sj = "?";
                auto* ei = lookup_element_by_Z(r.atoms[i].Z);
                auto* ej = lookup_element_by_Z(r.atoms[j].Z);
                if (ei) si = ei->symbol;
                if (ej) sj = ej->symbol;
                printf("    %s[%zu] — %s[%zu]: %.1f px\n", si, i, sj, j, dist);
            }
        }
    }

    if (!r.bonds.empty()) {
        std::cout << "\n  Bonds:\n";
        for (size_t i = 0; i < r.bonds.size(); ++i) {
            printf("    %zu: atom %d — atom %d\n", i, r.bonds[i].atom_a, r.bonds[i].atom_b);
        }
    }

    return 0;
}

static int cmd_validate(int argc, char** argv) {
    if (argc < 3) { print_help(); return 2; }
    auto r = import_molecule(argv[2]);
    if (!r.success) { std::cerr << "INVALID: " << r.message << "\n"; return 1; }

    for (const auto& b : r.bonds) {
        if (b.atom_a < 0 || b.atom_b < 0 ||
            static_cast<size_t>(b.atom_a) >= r.atoms.size() ||
            static_cast<size_t>(b.atom_b) >= r.atoms.size()) {
            std::cerr << "INVALID: bond index out of range\n";
            return 1;
        }
    }

    // Verify genome size per particle
    int total_p = 0;
    for (auto& a : r.atoms) total_p += static_cast<int>(a.particles.size());

    const char* chiral_str = r.is_chiral ? "chiral" : "achiral";
    if (!r.name.empty())
        printf("VALID — %s (%s): %zu atoms, %zu bonds, %d particles, %s (v3, GENOME_SIZE=%zu)\n",
               r.formula.c_str(), r.name.c_str(), r.atoms.size(), r.bonds.size(), total_p, chiral_str, GENOME_SIZE);
    else
        printf("VALID — %s: %zu atoms, %zu bonds, %d particles, %s (GENOME_SIZE=%zu)\n",
               r.formula.c_str(), r.atoms.size(), r.bonds.size(), total_p, chiral_str, GENOME_SIZE);
    return 0;
}

static int cmd_copy(int argc, char** argv) {
    if (argc < 4) { print_help(); return 2; }
    auto r = import_molecule(argv[2]);
    if (!r.success) { std::cerr << "Error: " << r.message << "\n"; return 1; }
    auto w = export_molecule(argv[3], r.formula, r.atoms, r.bonds, r.name);
    std::cout << (w.success ? "Copied\n" : "Error\n");
    return w.success ? 0 : 1;
}

static int cmd_render(int argc, char** argv) {
    if (argc < 4) { print_help(); return 2; }

    std::string in_path = argv[2];
    std::string out_png = argv[3];
    int width = 1024, height = 1024;
    float scale = 50.0f;
    bool show_particles = true;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--width"  && i+1 < argc) { uint32_t w; parse_u32(argv[++i], w); width  = w; }
        if (arg == "--height" && i+1 < argc) { uint32_t h; parse_u32(argv[++i], h); height = h; }
        if (arg == "--scale"  && i+1 < argc) scale = parse_float_or(argv[++i], 50.0f);
        if (arg == "--no-particles") show_particles = false;
    }

    auto r = import_molecule(in_path);
    if (!r.success) { std::cerr << "Error: " << r.message << "\n"; return 1; }

    if (!render_molecule(out_png, r, width, height, scale, show_particles)) {
        std::cerr << "Error: PNG write failed\n";
        return 1;
    }

    std::cout << "Rendered: " << out_png << "\n";
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 2; }

    std::string cmd = argv[1];
    if (cmd == "gen")      return cmd_gen(argc, argv);
    if (cmd == "dump")     return cmd_dump(argc, argv);
    if (cmd == "validate") return cmd_validate(argc, argv);
    if (cmd == "copy")     return cmd_copy(argc, argv);
    if (cmd == "render")   return cmd_render(argc, argv);

    print_help();
    return 2;
}
