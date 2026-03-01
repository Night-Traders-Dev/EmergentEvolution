// ppel_gen.cpp — Standalone PPEL element generator and inspector
//
// Generates .ppel files for any element on the periodic table (Z=1–118).
// Compatible with EmergentEvolution save_load.cpp (GENOME_SIZE=4).
//
// Commands:
//   gen <out.ppel> <symbol|Z> [--neutrons N] [--no-electrons] [--seed N]
//   gen-all <outdir>           Generate all 118 elements (most common isotope)
//   dump <file.ppel>           Print element info
//   validate <file.ppel>       Check file integrity
//   copy <in.ppel> <out.ppel>  Round-trip copy
//   list                       Print all elements
//
// Build:
//   g++ -O2 -std=c++17 -Isrc -o build/ppel_gen tools/ppel/ppel_gen.cpp -lm

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

// ── Format constants (must match save_load.cpp) ─────────────────────────────

static constexpr uint32_t PPEL_MAGIC   = 0x4C455050;  // "PPEL" little-endian
static constexpr uint32_t PPEL_VERSION = 1;
static constexpr size_t   GENOME_SIZE  = 4;

// Physics sub-atomic types (from phys_particles.h)
static constexpr uint32_t PROTON_TYPE   = 0;
static constexpr uint32_t NEUTRON_TYPE  = 1;
static constexpr uint32_t ELECTRON_TYPE = 2;

// ── Full periodic table ─────────────────────────────────────────────────────

struct Element {
    uint32_t Z;             // atomic number
    const char* symbol;
    const char* name;
    uint32_t mass_number;   // most common isotope A = Z + N
};

static const Element PERIODIC_TABLE[] = {
    {  1, "H",  "Hydrogen",       1},
    {  2, "He", "Helium",         4},
    {  3, "Li", "Lithium",        7},
    {  4, "Be", "Beryllium",      9},
    {  5, "B",  "Boron",         11},
    {  6, "C",  "Carbon",        12},
    {  7, "N",  "Nitrogen",      14},
    {  8, "O",  "Oxygen",        16},
    {  9, "F",  "Fluorine",      19},
    { 10, "Ne", "Neon",          20},
    { 11, "Na", "Sodium",        23},
    { 12, "Mg", "Magnesium",     24},
    { 13, "Al", "Aluminium",     27},
    { 14, "Si", "Silicon",       28},
    { 15, "P",  "Phosphorus",    31},
    { 16, "S",  "Sulfur",        32},
    { 17, "Cl", "Chlorine",      35},
    { 18, "Ar", "Argon",         40},
    { 19, "K",  "Potassium",     39},
    { 20, "Ca", "Calcium",       40},
    { 21, "Sc", "Scandium",      45},
    { 22, "Ti", "Titanium",      48},
    { 23, "V",  "Vanadium",      51},
    { 24, "Cr", "Chromium",      52},
    { 25, "Mn", "Manganese",     55},
    { 26, "Fe", "Iron",          56},
    { 27, "Co", "Cobalt",        59},
    { 28, "Ni", "Nickel",        58},
    { 29, "Cu", "Copper",        63},
    { 30, "Zn", "Zinc",          65},
    { 31, "Ga", "Gallium",       69},
    { 32, "Ge", "Germanium",     73},
    { 33, "As", "Arsenic",       75},
    { 34, "Se", "Selenium",      79},
    { 35, "Br", "Bromine",       80},
    { 36, "Kr", "Krypton",       84},
    { 37, "Rb", "Rubidium",      85},
    { 38, "Sr", "Strontium",     88},
    { 39, "Y",  "Yttrium",       89},
    { 40, "Zr", "Zirconium",     91},
    { 41, "Nb", "Niobium",       93},
    { 42, "Mo", "Molybdenum",    96},
    { 43, "Tc", "Technetium",    98},
    { 44, "Ru", "Ruthenium",    101},
    { 45, "Rh", "Rhodium",     103},
    { 46, "Pd", "Palladium",   106},
    { 47, "Ag", "Silver",      108},
    { 48, "Cd", "Cadmium",     112},
    { 49, "In", "Indium",      115},
    { 50, "Sn", "Tin",         119},
    { 51, "Sb", "Antimony",    122},
    { 52, "Te", "Tellurium",   128},
    { 53, "I",  "Iodine",      127},
    { 54, "Xe", "Xenon",       131},
    { 55, "Cs", "Caesium",     133},
    { 56, "Ba", "Barium",      137},
    { 57, "La", "Lanthanum",   139},
    { 58, "Ce", "Cerium",      140},
    { 59, "Pr", "Praseodymium",141},
    { 60, "Nd", "Neodymium",   144},
    { 61, "Pm", "Promethium",  145},
    { 62, "Sm", "Samarium",    150},
    { 63, "Eu", "Europium",    152},
    { 64, "Gd", "Gadolinium",  157},
    { 65, "Tb", "Terbium",     159},
    { 66, "Dy", "Dysprosium",  163},
    { 67, "Ho", "Holmium",     165},
    { 68, "Er", "Erbium",      167},
    { 69, "Tm", "Thulium",     169},
    { 70, "Yb", "Ytterbium",   173},
    { 71, "Lu", "Lutetium",    175},
    { 72, "Hf", "Hafnium",     178},
    { 73, "Ta", "Tantalum",    181},
    { 74, "W",  "Tungsten",    184},
    { 75, "Re", "Rhenium",     186},
    { 76, "Os", "Osmium",      190},
    { 77, "Ir", "Iridium",     192},
    { 78, "Pt", "Platinum",    195},
    { 79, "Au", "Gold",        197},
    { 80, "Hg", "Mercury",     201},
    { 81, "Tl", "Thallium",    204},
    { 82, "Pb", "Lead",        207},
    { 83, "Bi", "Bismuth",     209},
    { 84, "Po", "Polonium",    209},
    { 85, "At", "Astatine",    210},
    { 86, "Rn", "Radon",       222},
    { 87, "Fr", "Francium",    223},
    { 88, "Ra", "Radium",      226},
    { 89, "Ac", "Actinium",    227},
    { 90, "Th", "Thorium",     232},
    { 91, "Pa", "Protactinium",231},
    { 92, "U",  "Uranium",     238},
    { 93, "Np", "Neptunium",   237},
    { 94, "Pu", "Plutonium",   244},
    { 95, "Am", "Americium",   243},
    { 96, "Cm", "Curium",      247},
    { 97, "Bk", "Berkelium",   247},
    { 98, "Cf", "Californium", 251},
    { 99, "Es", "Einsteinium", 252},
    {100, "Fm", "Fermium",     257},
    {101, "Md", "Mendelevium", 258},
    {102, "No", "Nobelium",    259},
    {103, "Lr", "Lawrencium",  266},
    {104, "Rf", "Rutherfordium",267},
    {105, "Db", "Dubnium",     268},
    {106, "Sg", "Seaborgium",  269},
    {107, "Bh", "Bohrium",     270},
    {108, "Hs", "Hassium",     277},
    {109, "Mt", "Meitnerium",  278},
    {110, "Ds", "Darmstadtium",281},
    {111, "Rg", "Roentgenium", 282},
    {112, "Cn", "Copernicium", 285},
    {113, "Nh", "Nihonium",    286},
    {114, "Fl", "Flerovium",   289},
    {115, "Mc", "Moscovium",   290},
    {116, "Lv", "Livermorium", 293},
    {117, "Ts", "Tennessine",  294},
    {118, "Og", "Oganesson",   294},
};
static constexpr int NUM_ELEMENTS = sizeof(PERIODIC_TABLE) / sizeof(PERIODIC_TABLE[0]);

static const Element* lookup_by_Z(uint32_t Z) {
    for (int i = 0; i < NUM_ELEMENTS; ++i)
        if (PERIODIC_TABLE[i].Z == Z) return &PERIODIC_TABLE[i];
    return nullptr;
}

static const Element* lookup_by_symbol(const std::string& sym) {
    for (int i = 0; i < NUM_ELEMENTS; ++i)
        if (sym == PERIODIC_TABLE[i].symbol) return &PERIODIC_TABLE[i];
    return nullptr;
}

static const Element* lookup_by_name(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (int i = 0; i < NUM_ELEMENTS; ++i) {
        std::string ename = PERIODIC_TABLE[i].name;
        std::transform(ename.begin(), ename.end(), ename.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower == ename) return &PERIODIC_TABLE[i];
    }
    return nullptr;
}

// Try symbol first, then name, then atomic number
static const Element* resolve_element(const std::string& input) {
    // Try symbol (case-sensitive first, then case-insensitive)
    auto* e = lookup_by_symbol(input);
    if (e) return e;

    // Try name
    e = lookup_by_name(input);
    if (e) return e;

    // Try atomic number
    try {
        uint32_t z = static_cast<uint32_t>(std::stoul(input));
        return lookup_by_Z(z);
    } catch (...) {}

    return nullptr;
}

// ── Data structures ─────────────────────────────────────────────────────────

struct ParticleData {
    float    dx       = 0.0f;
    float    dy       = 0.0f;
    float    vx       = 0.0f;
    float    vy       = 0.0f;
    float    energy   = 0.0f;
    uint32_t type     = 0;
    float    genome[GENOME_SIZE] = {};
};

struct ElementData {
    bool success = false;
    std::string message;
    int32_t Z = 0, N = 0, electrons = 0;
    std::vector<ParticleData> particles;
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

static bool export_element(const std::string& filepath,
                           int32_t Z, int32_t N, int32_t electrons,
                           const std::vector<ParticleData>& particles)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    write_val(f, PPEL_MAGIC);
    write_val(f, PPEL_VERSION);
    write_val(f, Z);
    write_val(f, N);
    write_val(f, electrons);

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

static ElementData import_element(const std::string& filepath) {
    ElementData r;
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) { r.message = "Cannot open file"; return r; }

    uint32_t magic = 0, version = 0;
    if (!read_val(f, magic) || !read_val(f, version)) {
        r.message = "Truncated file (header)"; return r;
    }
    if (magic != PPEL_MAGIC) { r.message = "Not a valid .ppel file"; return r; }
    if (version != PPEL_VERSION) { r.message = "Unsupported .ppel version"; return r; }

    if (!read_val(f, r.Z) || !read_val(f, r.N) || !read_val(f, r.electrons)) {
        r.message = "Truncated file (element header)"; return r;
    }

    uint32_t count = 0;
    if (!read_val(f, count)) { r.message = "Truncated file (count)"; return r; }
    if (count > 10000) { r.message = "Element too large"; return r; }

    r.particles.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto& p = r.particles[i];
        if (!read_val(f, p.dx) || !read_val(f, p.dy) ||
            !read_val(f, p.vx) || !read_val(f, p.vy) ||
            !read_val(f, p.energy) || !read_val(f, p.type)) {
            r.message = "Truncated file (particle)"; return r;
        }
        f.read(reinterpret_cast<char*>(p.genome), GENOME_SIZE * sizeof(float));
        if (!f.good()) { r.message = "Truncated file (genome)"; return r; }
    }

    r.success = true;
    r.message = "OK";
    return r;
}

// ── Nucleus layout ──────────────────────────────────────────────────────────

static constexpr float NUC_SPACING = 3.8f;

static void layout_nucleus(int Z, int N,
                           std::vector<std::pair<float,float>>& positions,
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
            nuc_types.push_back(PROTON_TYPE); p++;
        } else if (n < N) {
            nuc_types.push_back(NEUTRON_TYPE); n++;
        } else {
            nuc_types.push_back(PROTON_TYPE); p++;
        }
    }

    // Center nucleon
    if (total == 1) {
        positions.push_back({0.0f, 0.0f});
        types.push_back(nuc_types[0]);
        return;
    }

    int placed = 0;
    positions.push_back({0.0f, 0.0f});
    types.push_back(nuc_types[placed++]);

    int ring = 1;
    while (placed < total) {
        float r = ring * NUC_SPACING;
        int capacity = static_cast<int>(std::round(2.0 * M_PI * ring));
        if (capacity < 6) capacity = 6;
        int to_place = std::min(capacity, total - placed);
        for (int k = 0; k < to_place; ++k) {
            float angle = 2.0f * static_cast<float>(M_PI) * k / capacity;
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

    static const int SHELL_CAP[] = {2, 8, 8, 18, 18, 32, 32};
    static const float SHELL_RADIUS[] = {12.0f, 24.0f, 38.0f, 52.0f, 68.0f, 84.0f, 100.0f};
    int num_shells = sizeof(SHELL_CAP) / sizeof(SHELL_CAP[0]);

    // Nuclear extent
    float nuc_extent = 0.0f;
    if (Z > 1) {
        int A = Z + static_cast<int>(Z <= 2 ? Z : Z + std::round(Z * 0.31));
        int rings = 0, count = 1;
        while (count < A) {
            ++rings;
            int cap = static_cast<int>(std::round(2.0 * M_PI * rings));
            if (cap < 6) cap = 6;
            count += std::min(cap, A - count);
        }
        nuc_extent = rings * NUC_SPACING + 1.9f;
    }

    int remaining = num_electrons;
    int shell = 0;
    while (remaining > 0 && shell < num_shells) {
        int in_shell = std::min(remaining, SHELL_CAP[shell]);
        float r = nuc_extent + SHELL_RADIUS[shell];
        for (int k = 0; k < in_shell; ++k) {
            float angle = 2.0f * static_cast<float>(M_PI) * k / in_shell
                          + shell * 2.399f;
            positions.push_back({r * std::cos(angle), r * std::sin(angle)});
        }
        remaining -= in_shell;
        shell++;
    }

    // Overflow
    if (remaining > 0) {
        float r_last = nuc_extent + SHELL_RADIUS[std::min(shell, num_shells - 1)];
        for (int k = 0; k < remaining; ++k) {
            float angle = 2.0f * static_cast<float>(M_PI) * k / (remaining + 1);
            positions.push_back({r_last * std::cos(angle), r_last * std::sin(angle)});
        }
    }
}

// ── Genome builder ──────────────────────────────────────────────────────────

static void make_genome(float genome[GENOME_SIZE], uint32_t type, std::mt19937& rng) {
    float charge = 0.0f, spin = 0.5f;
    switch (type) {
        case PROTON_TYPE:   charge =  1.0f; break;
        case NEUTRON_TYPE:  charge =  0.0f; break;
        case ELECTRON_TYPE: charge = -1.0f; break;
    }
    std::uniform_int_distribution<int> coin(0, 1);
    spin = coin(rng) ? 0.5f : -0.5f;

    genome[0] = charge;
    genome[1] = spin;
    genome[2] = 0.0f;  // color_charge
    genome[3] = 0.0f;  // decay_rate
}

// ── Build element ───────────────────────────────────────────────────────────

static std::vector<ParticleData> build_element(int Z, int N, int electrons,
                                                std::mt19937& rng)
{
    std::vector<ParticleData> particles;

    // Nucleus
    std::vector<std::pair<float,float>> nuc_pos;
    std::vector<uint32_t> nuc_types;
    layout_nucleus(Z, N, nuc_pos, nuc_types);

    for (size_t i = 0; i < nuc_pos.size(); ++i) {
        ParticleData p;
        p.dx = nuc_pos[i].first;
        p.dy = nuc_pos[i].second;
        p.type = nuc_types[i];
        p.energy = 0.5f;
        make_genome(p.genome, p.type, rng);
        particles.push_back(p);
    }

    // Electrons
    std::vector<std::pair<float,float>> e_pos;
    layout_electrons(Z, electrons, e_pos);

    for (auto& ep : e_pos) {
        ParticleData p;
        p.dx = ep.first;
        p.dy = ep.second;
        p.type = ELECTRON_TYPE;
        p.energy = 0.2f;
        make_genome(p.genome, p.type, rng);
        particles.push_back(p);
    }

    return particles;
}

// ── Help ────────────────────────────────────────────────────────────────────

static void print_help() {
    std::cout << "ppel_gen — element file tool (EmergentEvolution)\n\n";
    std::cout << "Commands:\n";
    std::cout << "  gen <out.ppel> <symbol|Z|name> [--neutrons N] [--no-electrons] [--seed N]\n";
    std::cout << "      Generate .ppel for a single element.\n";
    std::cout << "      Element can be symbol (Fe), atomic number (26), or name (Iron).\n";
    std::cout << "      --neutrons N   : override neutron count (default: most common isotope)\n";
    std::cout << "      --no-electrons : bare nucleus only\n\n";
    std::cout << "  gen-all <outdir> [--no-electrons] [--seed N]\n";
    std::cout << "      Generate all 118 elements as {Symbol}-{A}.ppel files.\n\n";
    std::cout << "  dump <file.ppel>\n";
    std::cout << "      Print element info.\n\n";
    std::cout << "  validate <file.ppel>\n";
    std::cout << "      Check file integrity.\n\n";
    std::cout << "  copy <in.ppel> <out.ppel>\n";
    std::cout << "      Round-trip copy.\n\n";
    std::cout << "  list\n";
    std::cout << "      Print the full periodic table.\n";
}

// ── Commands ────────────────────────────────────────────────────────────────

static int cmd_gen(int argc, char** argv) {
    if (argc < 4) { print_help(); return 2; }

    std::string out_path = argv[2];
    std::string input    = argv[3];
    int override_N = -1;
    bool gen_electrons = true;
    uint64_t seed = 42;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--neutrons" && i+1 < argc) { override_N = std::stoi(argv[++i]); }
        else if (arg == "--no-electrons") { gen_electrons = false; }
        else if (arg == "--seed" && i+1 < argc) { seed = std::stoull(argv[++i]); }
    }

    const Element* elem = resolve_element(input);
    if (!elem) {
        std::cerr << "Error: unknown element '" << input << "'\n";
        return 1;
    }

    int Z = static_cast<int>(elem->Z);
    int N = (override_N >= 0) ? override_N : static_cast<int>(elem->mass_number - elem->Z);
    int electrons = gen_electrons ? Z : 0;

    std::mt19937 rng(seed);
    auto particles = build_element(Z, N, electrons, rng);

    if (!export_element(out_path, Z, N, electrons, particles)) {
        std::cerr << "Error: failed to write " << out_path << "\n";
        return 1;
    }

    int A = Z + N;
    printf("Generated: %s\n", out_path.c_str());
    printf("  Element:   %s (%s)\n", elem->name, elem->symbol);
    printf("  Z=%d  N=%d  A=%d  e=%d\n", Z, N, A, electrons);
    printf("  Particles: %zu (p=%d n=%d e=%d)\n",
           particles.size(), Z, N, electrons);
    return 0;
}

static int cmd_gen_all(int argc, char** argv) {
    if (argc < 3) { print_help(); return 2; }

    std::string outdir = argv[2];
    bool gen_electrons = true;
    uint64_t seed = 42;

    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-electrons") gen_electrons = false;
        else if (arg == "--seed" && i+1 < argc) { seed = std::stoull(argv[++i]); }
    }

    std::filesystem::create_directories(outdir);

    int generated = 0;
    for (int i = 0; i < NUM_ELEMENTS; ++i) {
        const auto& elem = PERIODIC_TABLE[i];
        int Z = static_cast<int>(elem.Z);
        int N = static_cast<int>(elem.mass_number - elem.Z);
        int electrons = gen_electrons ? Z : 0;
        int A = Z + N;

        std::mt19937 rng(seed + elem.Z);
        auto particles = build_element(Z, N, electrons, rng);

        // Filename: {Symbol}-{A}.ppel
        std::string filename = std::string(elem.symbol) + "-" + std::to_string(A) + ".ppel";
        std::string filepath = outdir + "/" + filename;

        if (!export_element(filepath, Z, N, electrons, particles)) {
            std::cerr << "Error: failed to write " << filepath << "\n";
            continue;
        }

        printf("[%3d/118] %s  %-14s  Z=%-3d N=%-3d A=%-3d  %3zu particles\n",
               i + 1, filename.c_str(), elem.name, Z, N, A, particles.size());
        generated++;
    }

    printf("\nGenerated %d element files in %s\n", generated, outdir.c_str());
    return 0;
}

static int cmd_dump(int argc, char** argv) {
    if (argc < 3) { print_help(); return 2; }
    auto r = import_element(argv[2]);
    if (!r.success) { std::cerr << "Error: " << r.message << "\n"; return 1; }

    const Element* elem = lookup_by_Z(r.Z);
    const char* sym  = elem ? elem->symbol : "?";
    const char* name = elem ? elem->name   : "Unknown";
    int A = r.Z + r.N;

    printf("Element:    %s (%s)\n", name, sym);
    printf("Z:          %d (protons)\n", r.Z);
    printf("N:          %d (neutrons)\n", r.N);
    printf("A:          %d (mass number)\n", A);
    printf("Electrons:  %d\n", r.electrons);
    printf("Particles:  %zu\n\n", r.particles.size());

    // Count by type
    int protons = 0, neutrons = 0, electrons_count = 0, other = 0;
    for (auto& p : r.particles) {
        switch (p.type) {
            case PROTON_TYPE:   protons++; break;
            case NEUTRON_TYPE:  neutrons++; break;
            case ELECTRON_TYPE: electrons_count++; break;
            default: other++; break;
        }
    }
    printf("  Protons:   %d\n", protons);
    printf("  Neutrons:  %d\n", neutrons);
    printf("  Electrons: %d\n", electrons_count);
    if (other > 0) printf("  Other:     %d\n", other);

    return 0;
}

static int cmd_validate(int argc, char** argv) {
    if (argc < 3) { print_help(); return 2; }
    auto r = import_element(argv[2]);
    if (!r.success) { std::cerr << "INVALID: " << r.message << "\n"; return 1; }

    const Element* elem = lookup_by_Z(r.Z);
    const char* sym  = elem ? elem->symbol : "?";
    const char* name = elem ? elem->name   : "Unknown";
    int A = r.Z + r.N;

    printf("VALID — %s-%d (%s): Z=%d N=%d e=%d, %zu particles\n",
           sym, A, name, r.Z, r.N, r.electrons, r.particles.size());
    return 0;
}

static int cmd_copy(int argc, char** argv) {
    if (argc < 4) { print_help(); return 2; }
    auto r = import_element(argv[2]);
    if (!r.success) { std::cerr << "Error: " << r.message << "\n"; return 1; }
    bool ok = export_element(argv[3], r.Z, r.N, r.electrons, r.particles);
    std::cout << (ok ? "Copied\n" : "Error\n");
    return ok ? 0 : 1;
}

static int cmd_list([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    printf("  Z  Sym  Name                A\n");
    printf("───  ───  ──────────────────  ───\n");
    for (int i = 0; i < NUM_ELEMENTS; ++i) {
        const auto& e = PERIODIC_TABLE[i];
        printf("%3d  %-3s  %-18s  %d\n", e.Z, e.symbol, e.name, e.mass_number);
    }
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 2; }

    std::string cmd = argv[1];
    if (cmd == "gen")      return cmd_gen(argc, argv);
    if (cmd == "gen-all")  return cmd_gen_all(argc, argv);
    if (cmd == "dump")     return cmd_dump(argc, argv);
    if (cmd == "validate") return cmd_validate(argc, argv);
    if (cmd == "copy")     return cmd_copy(argc, argv);
    if (cmd == "list")     return cmd_list(argc, argv);

    print_help();
    return 2;
}
