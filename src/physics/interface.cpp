#include "physics/interface.h"
#include "physics/audio.h"
#include "physics/molecules.h"
#include "physics/phys_particles.h"
#include <imgui.h>
#include <cmath>
#include <random>
#include <cstdio>
#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>
#ifdef HAS_OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;

// ── World-unit ↔ real-unit scale ────────────────────────────────────────────
// Bohr radius = 15 world units (R_BOHR_PE in simulation.cpp)
// Real Bohr radius = 0.0529 nm → 1 world unit ≈ 0.00353 nm
static constexpr float R_BOHR_WORLD    = 15.0f;             // Bohr radius in world units
static constexpr float BOHR_RADIUS_NM  = 0.0529f;           // Bohr radius in nanometers
static constexpr float WORLD_TO_NM     = BOHR_RADIUS_NM / R_BOHR_WORLD;  // ~0.00353 nm/wu
static constexpr float H_ATOM_DIAMETER = 2.0f * R_BOHR_WORLD;            // 30 world units

// ── Physics constants for real-world display ─────────────────────────────
// C_SIM defined in phys_particles.h (300.0f)
static constexpr float C_REAL = 299792458.0f;      // speed of light (m/s)

// PDG rest masses — use shared table from phys_particles.h
static inline float rest_mass_MeV(uint32_t t) {
    return (t < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[t] : 0.0f;
}

// Format speed in real-world units: m/s, km/s, or fraction of c
static void fmt_speed(char* buf, size_t sz, float sim_speed) {
    float beta = std::min(sim_speed / C_SIM, 0.9999f);
    float real_speed = beta * C_REAL;
    if (beta >= 0.01f)
        snprintf(buf, sz, "%.4fc", beta);
    else if (real_speed >= 1000.0f)
        snprintf(buf, sz, "%.1f km/s", real_speed / 1000.0f);
    else
        snprintf(buf, sz, "%.0f m/s", real_speed);
}

// Format momentum in MeV/c (relativistic p = γm₀βc)
static void fmt_momentum(char* buf, size_t sz, float sim_speed, uint32_t ptype) {
    float m0 = rest_mass_MeV(ptype);
    float beta = std::min(sim_speed / C_SIM, 0.9999f);
    float p_MeV;
    if (m0 < 0.001f) {
        // Massless: p = E/c; use beta as proxy
        p_MeV = beta * 1.0f;
    } else {
        float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
        p_MeV = gamma * m0 * beta;
    }
    if (p_MeV < 0.001f)
        snprintf(buf, sz, "~0");
    else if (p_MeV < 1.0f)
        snprintf(buf, sz, "%.2f keV/c", p_MeV * 1e3f);
    else if (p_MeV < 1e3f)
        snprintf(buf, sz, "%.2f MeV/c", p_MeV);
    else if (p_MeV < 1e6f)
        snprintf(buf, sz, "%.2f GeV/c", p_MeV / 1e3f);
    else
        snprintf(buf, sz, "%.2f TeV/c", p_MeV / 1e6f);
}

// Format energy in eV auto-scaling
static void fmt_energy_ev(char* buf, size_t sz, float MeV) {
    float eV = MeV * 1e6f;
    if (eV < 0.01f)
        snprintf(buf, sz, "~0 eV");
    else if (eV < 1e3f)
        snprintf(buf, sz, "%.1f eV", eV);
    else if (eV < 1e6f)
        snprintf(buf, sz, "%.2f keV", eV / 1e3f);
    else if (eV < 1e9f)
        snprintf(buf, sz, "%.2f MeV", eV / 1e6f);
    else if (eV < 1e12f)
        snprintf(buf, sz, "%.2f GeV", eV / 1e9f);
    else
        snprintf(buf, sz, "%.2f TeV", eV / 1e12f);
}

// Forward declarations for helpers defined later in file
static std::string build_molecular_formula(const std::vector<PhysicsInterface::ElementSummary>& elems,
                                            const std::vector<uint32_t>& atom_indices);

void PhysicsInterface::init() {
    std::random_device rd;
    seed_value = static_cast<int>(rd() % 100000);
    load_prefs();
}

// ── Settings persistence ────────────────────────────────────────────────────

static constexpr uint32_t PPCFG_MAGIC   = 0x47464350;  // "PCFG" little-endian
static constexpr uint32_t PPCFG_VERSION = 1;

void PhysicsInterface::save_prefs() {
    std::error_code ec;
    fs::create_directories("saves", ec);
    std::ofstream f("saves/settings.ppcfg", std::ios::binary);
    if (!f.is_open()) return;
    f.write(reinterpret_cast<const char*>(&PPCFG_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPCFG_VERSION), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&prefs), sizeof(UserPrefs));
}

void PhysicsInterface::load_prefs() {
    std::ifstream f("saves/settings.ppcfg", std::ios::binary);
    if (!f.is_open()) return;
    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPCFG_MAGIC || version != PPCFG_VERSION) return;
    UserPrefs loaded{};
    f.read(reinterpret_cast<char*>(&loaded), sizeof(UserPrefs));
    if (f.good()) prefs = loaded;
}

// ── Nucleus / atom group templates ───────────────────────────────────────────

// Hydrogen atom: 1 proton + 1 electron
// Shell 0: Z_eff=1, R_bohr = 15px
static const SubAtomicSpec H_ATOM[] = {
    { 0, 0, PROTON_TYPE },
    { 15, 0, ELECTRON_TYPE_PHYS },
};

// Deuterium: 1 proton + 1 neutron + 1 electron
// Nucleon spacing ~3.8px; Shell 0: Z_eff=1, R_bohr = 15px
static const SubAtomicSpec DEUTERIUM[] = {
    { -1.9f, 0, PROTON_TYPE },
    {  1.9f, 0, NEUTRON_TYPE },
    { 15, 0, ELECTRON_TYPE_PHYS },
};

// Helium-4 atom: 2p + 2n + 2e
// Nucleons in 2x2 at 3.8px spacing, interleaved p/n
// Shell 0: Z_eff=2, R_bohr = 7.5 -> floor 8px
static const SubAtomicSpec HE4_ATOM[] = {
    { -1.9f, -1.9f, PROTON_TYPE },
    {  1.9f, -1.9f, NEUTRON_TYPE },
    { -1.9f,  1.9f, NEUTRON_TYPE },
    {  1.9f,  1.9f, PROTON_TYPE },
    { -8, 0, ELECTRON_TYPE_PHYS },
    {  8, 0, ELECTRON_TYPE_PHYS },
};

// Lithium-7: 3p + 4n + 3e
// Center + hex ring at 3.8px, interleaved p/n
// Shell 0: Z_eff=3, R=5 -> 8px (2e); Shell 1: Z_eff=1, R=60px (1e)
static const SubAtomicSpec LI7_ATOM[] = {
    {  0,    0,     PROTON_TYPE },
    {  3.8f, 0,     NEUTRON_TYPE },
    { -3.8f, 0,     PROTON_TYPE },
    {  1.9f, 3.29f, NEUTRON_TYPE },
    { -1.9f, 3.29f, NEUTRON_TYPE },
    {  1.9f,-3.29f, PROTON_TYPE },
    { -1.9f,-3.29f, NEUTRON_TYPE },
    { -8, 0, ELECTRON_TYPE_PHYS },
    {  8, 0, ELECTRON_TYPE_PHYS },
    {  0, 60, ELECTRON_TYPE_PHYS },
};

// Carbon-12 nucleus: 6p + 6n + 6e
// Center + ring of 6 + partial ring of 5, interleaved p/n
// Shell 0: Z_eff=6, R=2.5 -> 8px (2e); Shell 1: Z_eff=4, R=15px (4e)
static const SubAtomicSpec C12_ATOM[] = {
    {  0,    0,     PROTON_TYPE },
    {  3.8f, 0,     NEUTRON_TYPE },
    { -3.8f, 0,     PROTON_TYPE },
    {  1.9f, 3.29f, NEUTRON_TYPE },
    { -1.9f, 3.29f, PROTON_TYPE },
    {  1.9f,-3.29f, NEUTRON_TYPE },
    { -1.9f,-3.29f, PROTON_TYPE },
    {  7.6f, 0,     NEUTRON_TYPE },
    { -7.6f, 0,     PROTON_TYPE },
    {  3.8f, 6.58f, NEUTRON_TYPE },
    { -3.8f, 6.58f, NEUTRON_TYPE },
    {  3.8f,-6.58f, PROTON_TYPE },
    { -8, 0, ELECTRON_TYPE_PHYS }, { 8, 0, ELECTRON_TYPE_PHYS },
    { 0, -15, ELECTRON_TYPE_PHYS }, { 0, 15, ELECTRON_TYPE_PHYS },
    { -10.6f, -10.6f, ELECTRON_TYPE_PHYS }, { 10.6f, 10.6f, ELECTRON_TYPE_PHYS },
};

// Positronium: 1 electron + 1 positron (bound e-e+ pair)
static const SubAtomicSpec POSITRONIUM[] = {
    { -10, 0, ELECTRON_TYPE_PHYS },
    {  10, 0, POSITRON_TYPE_PHYS },
};

// Anti-hydrogen: 1 antiproton + 1 positron
static const SubAtomicSpec ANTI_H_ATOM[] = {
    { 0, 0, ANTIPROTON_TYPE_PHYS },
    { 20, 0, POSITRON_TYPE_PHYS },
};

// Anti-helium-4: 2 antiproton + 2 neutron + 2 positron
static const SubAtomicSpec ANTI_HE4_ATOM[] = {
    { -1.9f, -1.9f, ANTIPROTON_TYPE_PHYS },
    {  1.9f, -1.9f, NEUTRON_TYPE },
    { -1.9f,  1.9f, NEUTRON_TYPE },
    {  1.9f,  1.9f, ANTIPROTON_TYPE_PHYS },
    { -8, 0, POSITRON_TYPE_PHYS },
    {  8, 0, POSITRON_TYPE_PHYS },
};

// Oxygen-16: 8p + 8n + 8e
// Hex rings: center(1) + ring1(6) + ring2(9), interleaved p/n
// Shell 0: Z_eff=8, R=1.875 -> 8px (2e); Shell 1: Z_eff=6, R=10px (6e)
static const SubAtomicSpec O16_ATOM[] = {
    {  0,    0,     PROTON_TYPE },
    {  3.8f, 0,     NEUTRON_TYPE },
    { -3.8f, 0,     PROTON_TYPE },
    {  1.9f, 3.29f, NEUTRON_TYPE },
    { -1.9f, 3.29f, PROTON_TYPE },
    {  1.9f,-3.29f, NEUTRON_TYPE },
    { -1.9f,-3.29f, PROTON_TYPE },
    {  7.6f, 0,     NEUTRON_TYPE },
    { -7.6f, 0,     PROTON_TYPE },
    {  3.8f, 6.58f, NEUTRON_TYPE },
    { -3.8f, 6.58f, PROTON_TYPE },
    {  3.8f,-6.58f, NEUTRON_TYPE },
    { -3.8f,-6.58f, PROTON_TYPE },
    {  7.6f, 4.39f, NEUTRON_TYPE },
    { -7.6f, 4.39f, NEUTRON_TYPE },
    {  0,    8.78f, PROTON_TYPE },
    { -8, 0, ELECTRON_TYPE_PHYS }, { 8, 0, ELECTRON_TYPE_PHYS },
    { 10, 0, ELECTRON_TYPE_PHYS }, { -10, 0, ELECTRON_TYPE_PHYS },
    { 5, 8.66f, ELECTRON_TYPE_PHYS }, { -5, 8.66f, ELECTRON_TYPE_PHYS },
    { 5, -8.66f, ELECTRON_TYPE_PHYS }, { -5, -8.66f, ELECTRON_TYPE_PHYS },
};

// ── Quark-level baryons (3 quarks in triangle formation) ────────────────────

// Proton as quarks: uud
static const SubAtomicSpec PROTON_QUARKS[] = {
    {  0, -2, UP_QUARK_TYPE },
    { -2,  2, UP_QUARK_TYPE },
    {  2,  2, DOWN_QUARK_TYPE },
};

// Neutron as quarks: udd
static const SubAtomicSpec NEUTRON_QUARKS[] = {
    {  0, -2, UP_QUARK_TYPE },
    { -2,  2, DOWN_QUARK_TYPE },
    {  2,  2, DOWN_QUARK_TYPE },
};

// Antiproton as quarks: u-bar u-bar d-bar
static const SubAtomicSpec ANTIPROTON_QUARKS[] = {
    {  0, -2, ANTI_UP_TYPE },
    { -2,  2, ANTI_UP_TYPE },
    {  2,  2, ANTI_DOWN_TYPE },
};

// Antineutron as quarks: u-bar d-bar d-bar
static const SubAtomicSpec ANTINEUTRON_QUARKS[] = {
    {  0, -2, ANTI_UP_TYPE },
    { -2,  2, ANTI_DOWN_TYPE },
    {  2,  2, ANTI_DOWN_TYPE },
};

// Delta++ (uuu) — doubly charged baryon
static const SubAtomicSpec DELTA_PP_QUARKS[] = {
    {  0, -2, UP_QUARK_TYPE },
    { -2,  2, UP_QUARK_TYPE },
    {  2,  2, UP_QUARK_TYPE },
};

// Lambda0 (uds) — all different light quarks
static const SubAtomicSpec LAMBDA_QUARKS[] = {
    {  0, -2, UP_QUARK_TYPE },
    { -2,  2, DOWN_QUARK_TYPE },
    {  2,  2, STRANGE_QUARK_TYPE },
};

// Omega- (sss) — triple strange baryon
static const SubAtomicSpec OMEGA_QUARKS[] = {
    {  0, -2, STRANGE_QUARK_TYPE },
    { -2,  2, STRANGE_QUARK_TYPE },
    {  2,  2, STRANGE_QUARK_TYPE },
};

// ── Quark-level mesons (quark-antiquark pairs) ──────────────────────────────

// Pion (pi+): u + d-bar
static const SubAtomicSpec PION_PLUS[] = {
    { -3, 0, UP_QUARK_TYPE },
    {  3, 0, ANTI_DOWN_TYPE },
};

// Pion (pi-): d + u-bar
static const SubAtomicSpec PION_MINUS[] = {
    { -3, 0, DOWN_QUARK_TYPE },
    {  3, 0, ANTI_UP_TYPE },
};

// Kaon (K+): u + s-bar
static const SubAtomicSpec KAON_PLUS[] = {
    { -3, 0, UP_QUARK_TYPE },
    {  3, 0, ANTI_STRANGE_TYPE },
};

// J/psi: c + c-bar (charmonium)
static const SubAtomicSpec JPSI[] = {
    { -3, 0, CHARM_QUARK_TYPE },
    {  3, 0, ANTI_CHARM_TYPE },
};

const GroupTemplate GROUP_TEMPLATES[] = {
    { "H atom",       "H",    H_ATOM,        2 },
    { "Deuterium",    "D",    DEUTERIUM,      3 },
    { "He-4 atom",    "He",   HE4_ATOM,       6 },
    { "Li-7 atom",    "Li",   LI7_ATOM,      10 },
    { "C-12 atom",    "C",    C12_ATOM,      18 },
    { "O-16 atom",    "O",    O16_ATOM,      24 },
    { "Positronium",  "Ps",   POSITRONIUM,    2 },
    { "Anti-H",       "H-",   ANTI_H_ATOM,    2 },
    { "Anti-He-4",    "He-",  ANTI_HE4_ATOM,  6 },
    { "Pion+",        "pi+",  PION_PLUS,      2 },
    { "Pion-",        "pi-",  PION_MINUS,     2 },
    { "Kaon+",        "K+",   KAON_PLUS,      2 },
};
extern const int GROUP_TEMPLATE_COUNT_VAL = sizeof(GROUP_TEMPLATES) / sizeof(GROUP_TEMPLATES[0]);

// ── Hadron templates (quark-level composites) ────────────────────────────────
const GroupTemplate HADRON_TEMPLATES[] = {
    // Baryons (3 quarks)
    { "Proton (uud)",       "p",    PROTON_QUARKS,      3 },
    { "Neutron (udd)",      "n",    NEUTRON_QUARKS,     3 },
    { "Antiproton",         "p-",   ANTIPROTON_QUARKS,  3 },
    { "Antineutron",        "n-",   ANTINEUTRON_QUARKS, 3 },
    { "Delta++ (uuu)",      "D++",  DELTA_PP_QUARKS,    3 },
    { "Lambda0 (uds)",      "L0",   LAMBDA_QUARKS,      3 },
    { "Omega- (sss)",       "O-",   OMEGA_QUARKS,       3 },
    // Mesons (quark-antiquark)
    { "Pion+ (ud~)",        "pi+",  PION_PLUS,          2 },
    { "Pion- (du~)",        "pi-",  PION_MINUS,         2 },
    { "Kaon+ (us~)",        "K+",   KAON_PLUS,          2 },
    { "J/psi (cc~)",        "J/p",  JPSI,               2 },
};
extern const int HADRON_TEMPLATE_COUNT_VAL = sizeof(HADRON_TEMPLATES) / sizeof(HADRON_TEMPLATES[0]);

// ── Periodic table element data ──────────────────────────────────────────────

struct ElementData {
    int Z, N;
    const char* symbol;
    const char* name;
};

static const ElementData ELEMENTS[] = {
    { 1,  0, "H",  "Hydrogen"},
    { 2,  2, "He", "Helium"},
    { 3,  4, "Li", "Lithium"},
    { 4,  5, "Be", "Beryllium"},
    { 5,  6, "B",  "Boron"},
    { 6,  6, "C",  "Carbon"},
    { 7,  7, "N",  "Nitrogen"},
    { 8,  8, "O",  "Oxygen"},
    { 9, 10, "F",  "Fluorine"},
    {10, 10, "Ne", "Neon"},
    {11, 12, "Na", "Sodium"},
    {12, 12, "Mg", "Magnesium"},
    {13, 14, "Al", "Aluminum"},
    {14, 14, "Si", "Silicon"},
    {15, 16, "P",  "Phosphorus"},
    {16, 16, "S",  "Sulfur"},
    {17, 18, "Cl", "Chlorine"},
    {18, 22, "Ar", "Argon"},
    {19, 20, "K",  "Potassium"},
    {20, 20, "Ca", "Calcium"},
    {26, 30, "Fe", "Iron"},
};
static const int ELEMENT_COUNT = sizeof(ELEMENTS) / sizeof(ELEMENTS[0]);

// Periodic table layout grid: element index into ELEMENTS[], -1 = empty cell
// Compressed to 10 columns: s-block (0-1) + gap (2-3) + p-block (4-9)
static const int PT_LAYOUT[4][10] = {
    {  0, -1, -1, -1, -1, -1, -1, -1, -1,  1},  // H ... He
    {  2,  3, -1, -1,  4,  5,  6,  7,  8,  9},  // Li Be . . B C N O F Ne
    { 10, 11, -1, -1, 12, 13, 14, 15, 16, 17},  // Na Mg . . Al Si P S Cl Ar
    { 18, 19, -1, -1, -1, -1, -1, -1, -1, 20},  // K Ca ... Fe
};

// ── Element names by atomic number (Z=1..118) ────────────────────────────────
static const char* const ELEMENT_NAMES[] = {
    "?",        // 0 (unused)
    "Hydrogen",  "Helium",     "Lithium",    "Beryllium",  "Boron",
    "Carbon",    "Nitrogen",   "Oxygen",     "Fluorine",   "Neon",
    "Sodium",    "Magnesium",  "Aluminium",  "Silicon",    "Phosphorus",
    "Sulfur",    "Chlorine",   "Argon",      "Potassium",  "Calcium",
    "Scandium",  "Titanium",   "Vanadium",   "Chromium",   "Manganese",
    "Iron",      "Cobalt",     "Nickel",     "Copper",     "Zinc",
    "Gallium",   "Germanium",  "Arsenic",    "Selenium",   "Bromine",
    "Krypton",   "Rubidium",   "Strontium",  "Yttrium",    "Zirconium",
    "Niobium",   "Molybdenum", "Technetium", "Ruthenium",  "Rhodium",
    "Palladium", "Silver",     "Cadmium",    "Indium",     "Tin",
    "Antimony",  "Tellurium",  "Iodine",     "Xenon",      "Cesium",
    "Barium",    "Lanthanum",  "Cerium",     "Praseodymium","Neodymium",
    "Promethium","Samarium",   "Europium",   "Gadolinium", "Terbium",
    "Dysprosium","Holmium",    "Erbium",     "Thulium",    "Ytterbium",
    "Lutetium",  "Hafnium",    "Tantalum",   "Tungsten",   "Rhenium",
    "Osmium",    "Iridium",    "Platinum",   "Gold",       "Mercury",
    "Thallium",  "Lead",       "Bismuth",    "Polonium",   "Astatine",
    "Radon",     "Francium",   "Radium",     "Actinium",   "Thorium",
    "Protactinium","Uranium",  "Neptunium",  "Plutonium",  "Americium",
    "Curium",    "Berkelium",  "Californium","Einsteinium", "Fermium",
    "Mendelevium","Nobelium",  "Lawrencium", "Rutherfordium","Dubnium",
    "Seaborgium","Bohrium",    "Hassium",    "Meitnerium", "Darmstadtium",
    "Roentgenium","Copernicium","Nihonium",  "Flerovium",  "Moscovium",
    "Livermorium","Tennessine","Oganesson",
};
static const char* const ELEMENT_SYMBOLS[] = {
    "?",
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
    "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og",
};
static constexpr int FULL_ELEMENT_COUNT = 118;

// ── Particle name/color tables for all 30 types ─────────────────────────────

static const char* const PHYS_TYPE_NAMES[PHYS_PARTICLE_TYPES] = {
    "Proton", "Neutron", "Electron", "Photon", "Positron", "Antiproton",
    "Neutrino_e",
    "Muon", "Anti-muon", "Tau", "Anti-tau", "Neutrino_mu", "Neutrino_tau",
    "Up", "Down", "Strange", "Charm", "Top", "Bottom",
    "Anti-up", "Anti-down", "Anti-strange", "Anti-charm", "Anti-top", "Anti-bottom",
    "Gluon", "W+", "W-", "Z0", "Higgs",
    "Graviton", "Dark Matter", "Dark Energy",
    // 33-38: DM candidates
    "Axino", "WIMPzilla", "SIMP", "Sterile Neutrino", "Dark Photon", "Q-Ball",
    // 39-49: SUSY
    "Selectron", "Smuon", "Stau", "Squark", "Gluino", "Photino",
    "Wino", "Zino", "Higgsino", "Neutralino", "Sneutrino",
    // 50-55: Force carriers
    "Gravitino", "X Boson", "Y Boson", "Monopole", "Radion", "Dilaton",
    // 56-64: Theoretical extremes
    "Tachyon", "Preon", "Inflaton", "Majoron", "Odderon",
    "Glueball", "Skyrmion", "X17", "Chameleon",
    // 65-66: New class
    "Paraparticle", "Dyn. Axion QP",
};

static const char* const PHYS_TYPE_LABELS[PHYS_PARTICLE_TYPES] = {
    "p", "n", "e-", "y", "e+", "p-",
    "ve",
    "mu-", "mu+", "tau-", "tau+", "vmu", "vtau",
    "u", "d", "s", "c", "t", "b",
    "u~", "d~", "s~", "c~", "t~", "b~",
    "g", "W+", "W-", "Z0", "H0",
    "G", "DM", "DE",
    // 33-38
    "Ax", "WZ", "SI", "Ns", "A'", "QB",
    // 39-49
    "e~", "mu~", "ta~", "q~", "g~", "y~",
    "W~", "Z~", "H~", "N1", "v~",
    // 50-55
    "G~", "X", "Y", "MM", "Ra", "Di",
    // 56-64
    "Ta", "Pr", "In", "Mj", "Od",
    "Gb", "Sk", "X17", "Ch",
    // 65-66
    "Pp", "Dq",
};

static const ImVec4 PHYS_TYPE_UI_COLORS[PHYS_PARTICLE_TYPES] = {
    // 0-6: composites + gen-1 leptons + photon
    ImVec4(0.9f, 0.2f, 0.2f, 1.0f),   // proton — red
    ImVec4(0.7f, 0.7f, 0.7f, 1.0f),   // neutron — grey
    ImVec4(0.2f, 0.5f, 1.0f, 1.0f),   // electron — blue
    ImVec4(1.0f, 1.0f, 0.6f, 1.0f),   // photon — yellow
    ImVec4(1.0f, 0.3f, 0.8f, 1.0f),   // positron — magenta
    ImVec4(0.2f, 0.85f, 0.7f, 1.0f),  // antiproton — teal
    ImVec4(0.6f, 0.9f, 0.6f, 1.0f),   // neutrino_e — green

    // 7-12: gen-2+3 leptons
    ImVec4(0.6f, 0.3f, 0.9f, 1.0f),   // muon — purple
    ImVec4(0.8f, 0.5f, 1.0f, 1.0f),   // anti-muon — light purple
    ImVec4(0.4f, 0.2f, 0.7f, 1.0f),   // tau — dark violet
    ImVec4(0.6f, 0.4f, 0.9f, 1.0f),   // anti-tau — light violet
    ImVec4(0.5f, 0.8f, 0.5f, 1.0f),   // neutrino_mu — green
    ImVec4(0.4f, 0.7f, 0.4f, 1.0f),   // neutrino_tau — dark green

    // 13-18: quarks
    ImVec4(0.9f, 0.5f, 0.2f, 1.0f),   // up — orange
    ImVec4(0.4f, 0.7f, 0.2f, 1.0f),   // down — olive
    ImVec4(0.2f, 0.8f, 0.6f, 1.0f),   // strange — teal-green
    ImVec4(0.9f, 0.8f, 0.2f, 1.0f),   // charm — gold
    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),   // top — bright red
    ImVec4(0.5f, 0.3f, 0.8f, 1.0f),   // bottom — blue-purple

    // 19-24: antiquarks
    ImVec4(1.0f, 0.7f, 0.5f, 1.0f),   // anti-up — peach
    ImVec4(0.7f, 0.9f, 0.5f, 1.0f),   // anti-down — light olive
    ImVec4(0.5f, 1.0f, 0.8f, 1.0f),   // anti-strange — mint
    ImVec4(1.0f, 0.9f, 0.5f, 1.0f),   // anti-charm — light gold
    ImVec4(1.0f, 0.6f, 0.6f, 1.0f),   // anti-top — pink
    ImVec4(0.7f, 0.6f, 1.0f, 1.0f),   // anti-bottom — lavender

    // 25-29: bosons
    ImVec4(0.3f, 0.9f, 0.3f, 1.0f),   // gluon — green
    ImVec4(0.9f, 0.9f, 1.0f, 1.0f),   // W+ — bright white-blue
    ImVec4(0.7f, 0.7f, 1.0f, 1.0f),   // W- — light blue
    ImVec4(0.8f, 0.8f, 0.9f, 1.0f),   // Z0 — silver-blue
    ImVec4(1.0f, 0.85f, 0.3f, 1.0f),  // Higgs — golden

    // 30-32: hypothetical
    ImVec4(0.7f, 0.8f, 1.0f, 1.0f),   // graviton — faint blue-white
    ImVec4(0.3f, 0.1f, 0.5f, 1.0f),   // dark matter — deep purple
    ImVec4(0.6f, 0.1f, 0.2f, 1.0f),   // dark energy — faint crimson
    // 33-38: DM candidates
    ImVec4(0.4f, 0.2f, 0.6f, 1.0f),   // axino — dark purple
    ImVec4(0.2f, 0.05f, 0.35f, 1.0f), // WIMPzilla — near-black violet
    ImVec4(0.35f, 0.25f, 0.55f, 1.0f),// SIMP — muted purple
    ImVec4(0.45f, 0.55f, 0.45f, 1.0f),// sterile neutrino — ghostly grey-green
    ImVec4(0.5f, 0.2f, 0.7f, 1.0f),   // dark photon — dark violet
    ImVec4(0.55f, 0.3f, 0.75f, 1.0f), // Q-Ball — bright purple
    // 39-49: SUSY
    ImVec4(0.5f, 0.8f, 1.0f, 1.0f),   // selectron — ice blue
    ImVec4(0.7f, 0.6f, 1.0f, 1.0f),   // smuon — periwinkle
    ImVec4(0.6f, 0.5f, 0.9f, 1.0f),   // stau — soft violet
    ImVec4(1.0f, 0.7f, 0.4f, 1.0f),   // squark — neon orange
    ImVec4(0.4f, 1.0f, 0.5f, 1.0f),   // gluino — neon green
    ImVec4(1.0f, 1.0f, 0.8f, 1.0f),   // photino — pale gold
    ImVec4(0.9f, 0.9f, 1.0f, 1.0f),   // wino — platinum
    ImVec4(0.8f, 0.8f, 0.95f, 1.0f),  // zino — cool silver
    ImVec4(1.0f, 0.9f, 0.6f, 1.0f),   // higgsino — warm gold
    ImVec4(0.4f, 0.2f, 0.65f, 1.0f),  // neutralino — dark indigo
    ImVec4(0.55f, 0.85f, 0.55f, 1.0f),// sneutrino — sage green
    // 50-55: Force carriers
    ImVec4(0.6f, 0.7f, 1.0f, 1.0f),   // gravitino — pale blue
    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),   // X boson — bright red
    ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // Y boson — bright orange-red
    ImVec4(0.95f, 0.95f, 0.95f, 1.0f),// monopole — white
    ImVec4(0.7f, 0.6f, 0.4f, 1.0f),   // radion — bronze
    ImVec4(0.6f, 0.55f, 0.45f, 1.0f), // dilaton — warm grey
    // 56-64: Theoretical extremes
    ImVec4(0.0f, 1.0f, 1.0f, 1.0f),   // tachyon — electric cyan
    ImVec4(1.0f, 0.0f, 0.5f, 1.0f),   // preon — hot pink
    ImVec4(1.0f, 0.7f, 0.1f, 1.0f),   // inflaton — amber glow
    ImVec4(0.55f, 0.6f, 0.55f, 1.0f), // majoron — faint grey-green
    ImVec4(0.7f, 0.3f, 1.0f, 1.0f),   // odderon — electric purple
    ImVec4(0.5f, 1.0f, 0.3f, 1.0f),   // glueball — lime-yellow
    ImVec4(0.9f, 0.4f, 0.2f, 1.0f),   // skyrmion — burnt orange
    ImVec4(0.2f, 0.9f, 0.8f, 1.0f),   // X17 — turquoise
    ImVec4(0.7f, 0.5f, 0.3f, 1.0f),   // chameleon — fading brown
    // 65-66: New class
    ImVec4(0.9f, 0.2f, 1.0f, 1.0f),   // paraparticle — vivid magenta
    ImVec4(0.4f, 0.7f, 0.95f, 1.0f),  // dyn axion QP — soft sky blue
};

// ── Temperature formatting ───────────────────────────────────────────────────

static void format_temperature(float kelvin, char* buf, int buf_size, int unit = 0) {
    float val = kelvin;
    const char* suffix = "K";
    if (unit == 1) { val = kelvin - 273.15f; suffix = "\xC2\xB0""C"; }       // °C
    else if (unit == 2) { val = kelvin * 1.8f - 459.67f; suffix = "\xC2\xB0""F"; } // °F

    float abs_val = std::abs(val);
    if (abs_val < 1000.0f) {
        snprintf(buf, buf_size, "%.0f %s", val, suffix);
    } else {
        const char* prefix = "";
        float display = val;
        if (abs_val >= 1e12f)      { display = val / 1e12f; prefix = "T"; }
        else if (abs_val >= 1e9f)  { display = val / 1e9f;  prefix = "G"; }
        else if (abs_val >= 1e6f)  { display = val / 1e6f;  prefix = "M"; }
        else if (abs_val >= 1e3f)  { display = val / 1e3f;  prefix = "k"; }
        if (unit == 0)
            snprintf(buf, buf_size, "%.1f %s%s", display, prefix, suffix);
        else
            snprintf(buf, buf_size, "%.1f %s%s", display, prefix, suffix);
    }
}

// ── Themes ───────────────────────────────────────────────────────────────────

static constexpr int THEME_COLOR_COUNT = 33;
static constexpr int THEME_VAR_COUNT   = 10;

// Theme color palettes: { bg, bg_dim, accent, accent_bright, border, frame, frame_hover, text, text_dim }
struct ThemeColors {
    ImVec4 bg, bg_dim, accent, accent_bright, border, frame, frame_hover, text, text_dim;
};

static const ThemeColors THEMES[] = {
    // 0: Dark Navy + Cyan
    { {0.059f,0.071f,0.110f,0.75f}, {0.039f,0.051f,0.090f,0.80f},
      {0.302f,0.749f,0.953f,1.0f},  {0.400f,0.820f,1.000f,1.0f},
      {0.180f,0.220f,0.349f,0.50f}, {0.098f,0.118f,0.180f,0.65f}, {0.137f,0.165f,0.259f,0.70f},
      {0.820f,0.851f,0.922f,1.0f},  {0.451f,0.478f,0.580f,1.0f} },
    // 1: Midnight + Violet
    { {0.050f,0.040f,0.100f,0.75f}, {0.030f,0.025f,0.070f,0.80f},
      {0.600f,0.400f,0.950f,1.0f},  {0.720f,0.520f,1.000f,1.0f},
      {0.200f,0.150f,0.350f,0.50f}, {0.080f,0.065f,0.160f,0.65f}, {0.120f,0.100f,0.240f,0.70f},
      {0.850f,0.830f,0.920f,1.0f},  {0.480f,0.440f,0.580f,1.0f} },
    // 2: Slate + Teal
    { {0.090f,0.100f,0.110f,0.75f}, {0.060f,0.070f,0.080f,0.80f},
      {0.200f,0.800f,0.700f,1.0f},  {0.300f,0.900f,0.800f,1.0f},
      {0.180f,0.200f,0.220f,0.50f}, {0.120f,0.135f,0.150f,0.65f}, {0.160f,0.180f,0.200f,0.70f},
      {0.850f,0.870f,0.890f,1.0f},  {0.500f,0.520f,0.550f,1.0f} },
    // 3: Ember + Orange
    { {0.080f,0.060f,0.050f,0.75f}, {0.055f,0.040f,0.035f,0.80f},
      {1.000f,0.550f,0.200f,1.0f},  {1.000f,0.700f,0.350f,1.0f},
      {0.280f,0.180f,0.120f,0.50f}, {0.130f,0.095f,0.075f,0.65f}, {0.180f,0.130f,0.100f,0.70f},
      {0.920f,0.880f,0.840f,1.0f},  {0.550f,0.480f,0.420f,1.0f} },
    // 4: Synthwave + Hot Pink
    { {0.080f,0.030f,0.090f,0.75f}, {0.055f,0.020f,0.065f,0.80f},
      {1.000f,0.200f,0.600f,1.0f},  {1.000f,0.400f,0.750f,1.0f},
      {0.250f,0.100f,0.280f,0.50f}, {0.120f,0.050f,0.130f,0.65f}, {0.170f,0.080f,0.190f,0.70f},
      {0.920f,0.850f,0.930f,1.0f},  {0.520f,0.400f,0.550f,1.0f} },
    // 5: Forest + Lime
    { {0.040f,0.080f,0.050f,0.75f}, {0.025f,0.055f,0.035f,0.80f},
      {0.400f,0.900f,0.300f,1.0f},  {0.550f,1.000f,0.450f,1.0f},
      {0.120f,0.220f,0.130f,0.50f}, {0.065f,0.120f,0.075f,0.65f}, {0.095f,0.170f,0.105f,0.70f},
      {0.860f,0.920f,0.860f,1.0f},  {0.450f,0.550f,0.460f,1.0f} },
    // 6: Arctic + Ice Blue
    { {0.070f,0.085f,0.105f,0.75f}, {0.050f,0.060f,0.080f,0.80f},
      {0.750f,0.900f,1.000f,1.0f},  {0.850f,0.950f,1.000f,1.0f},
      {0.160f,0.200f,0.260f,0.50f}, {0.100f,0.120f,0.155f,0.65f}, {0.140f,0.165f,0.210f,0.70f},
      {0.880f,0.910f,0.950f,1.0f},  {0.500f,0.540f,0.600f,1.0f} },
    // 7: Solar + Gold
    { {0.070f,0.055f,0.030f,0.75f}, {0.050f,0.038f,0.020f,0.80f},
      {1.000f,0.820f,0.200f,1.0f},  {1.000f,0.900f,0.400f,1.0f},
      {0.220f,0.180f,0.100f,0.50f}, {0.110f,0.090f,0.055f,0.65f}, {0.160f,0.130f,0.080f,0.70f},
      {0.930f,0.910f,0.860f,1.0f},  {0.560f,0.520f,0.430f,1.0f} },
    // 8: Crimson + Blood Red
    { {0.090f,0.040f,0.040f,0.75f}, {0.065f,0.025f,0.025f,0.80f},
      {0.900f,0.200f,0.200f,1.0f},  {1.000f,0.350f,0.350f,1.0f},
      {0.280f,0.120f,0.120f,0.50f}, {0.135f,0.060f,0.060f,0.65f}, {0.190f,0.090f,0.090f,0.70f},
      {0.930f,0.870f,0.870f,1.0f},  {0.550f,0.430f,0.430f,1.0f} },
    // 9: Ocean + Aquamarine
    { {0.030f,0.065f,0.090f,0.75f}, {0.020f,0.045f,0.065f,0.80f},
      {0.200f,0.900f,0.800f,1.0f},  {0.350f,1.000f,0.900f,1.0f},
      {0.100f,0.200f,0.260f,0.50f}, {0.055f,0.100f,0.135f,0.65f}, {0.080f,0.145f,0.190f,0.70f},
      {0.850f,0.920f,0.940f,1.0f},  {0.430f,0.520f,0.560f,1.0f} },
    // 10: Neon + Electric Green
    { {0.035f,0.035f,0.040f,0.75f}, {0.020f,0.020f,0.025f,0.80f},
      {0.200f,1.000f,0.300f,1.0f},  {0.400f,1.000f,0.500f,1.0f},
      {0.100f,0.150f,0.110f,0.50f}, {0.055f,0.065f,0.058f,0.65f}, {0.080f,0.100f,0.085f,0.70f},
      {0.880f,0.950f,0.890f,1.0f},  {0.450f,0.530f,0.460f,1.0f} },
    // 11: Lavender + Lilac
    { {0.075f,0.065f,0.095f,0.75f}, {0.052f,0.045f,0.070f,0.80f},
      {0.720f,0.560f,0.900f,1.0f},  {0.820f,0.680f,1.000f,1.0f},
      {0.200f,0.170f,0.260f,0.50f}, {0.105f,0.090f,0.140f,0.65f}, {0.148f,0.130f,0.200f,0.70f},
      {0.910f,0.880f,0.940f,1.0f},  {0.500f,0.470f,0.560f,1.0f} },
};
static constexpr int THEME_COUNT = 12;
static const char* THEME_NAMES[] = {
    "Dark Navy", "Midnight", "Slate", "Ember",
    "Synthwave", "Forest", "Arctic", "Solar",
    "Crimson", "Ocean", "Neon", "Lavender"
};

void PhysicsInterface::push_theme() {
    int t = std::clamp(prefs.theme, 0, THEME_COUNT - 1);
    const auto& c = THEMES[t];

    // Direct style assignment — avoids per-frame push/pop stack overhead
    ImGuiStyle& s = ImGui::GetStyle();
    auto* col = s.Colors;

    // Derived colors
    ImVec4 bg_child  = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.45f);
    ImVec4 bg_popup  = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.80f);
    ImVec4 bg_menu   = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.95f);
    ImVec4 frame_act = ImVec4(c.border.x, c.border.y, c.border.z, 0.70f);
    ImVec4 title_col = ImVec4(c.bg_dim.x, c.bg_dim.y, c.bg_dim.z, 0.55f);
    ImVec4 btn       = ImVec4(c.frame.x * 1.2f, c.frame.y * 1.2f, c.frame.z * 1.2f, 0.80f);
    ImVec4 btn_hov   = ImVec4(c.frame_hover.x * 1.2f, c.frame_hover.y * 1.2f, c.frame_hover.z * 1.2f, 0.90f);
    ImVec4 accent_lo = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.40f);
    ImVec4 accent_md = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.60f);
    ImVec4 accent_hi = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.80f);
    ImVec4 sep       = ImVec4(c.border.x, c.border.y, c.border.z, 0.60f);

    col[ImGuiCol_WindowBg]             = c.bg;
    col[ImGuiCol_ChildBg]              = bg_child;
    col[ImGuiCol_PopupBg]              = bg_popup;
    col[ImGuiCol_Border]               = c.border;
    col[ImGuiCol_BorderShadow]         = ImVec4(0,0,0,0);
    col[ImGuiCol_FrameBg]              = c.frame;
    col[ImGuiCol_FrameBgHovered]       = c.frame_hover;
    col[ImGuiCol_FrameBgActive]        = frame_act;
    col[ImGuiCol_TitleBg]              = c.bg_dim;
    col[ImGuiCol_TitleBgActive]        = ImVec4(c.bg_dim.x*1.3f, c.bg_dim.y*1.3f, c.bg_dim.z*1.3f, 0.80f);
    col[ImGuiCol_TitleBgCollapsed]     = title_col;
    col[ImGuiCol_MenuBarBg]            = bg_menu;
    col[ImGuiCol_ScrollbarBg]          = ImVec4(c.bg_dim.x, c.bg_dim.y, c.bg_dim.z, 0.60f);
    col[ImGuiCol_ScrollbarGrab]        = ImVec4(c.border.x, c.border.y, c.border.z, 0.80f);
    col[ImGuiCol_ScrollbarGrabHovered] = accent_md;
    col[ImGuiCol_ScrollbarGrabActive]  = accent_hi;
    col[ImGuiCol_CheckMark]            = c.accent;
    col[ImGuiCol_SliderGrab]           = accent_hi;
    col[ImGuiCol_SliderGrabActive]     = c.accent_bright;
    col[ImGuiCol_Button]               = btn;
    col[ImGuiCol_ButtonHovered]        = btn_hov;
    col[ImGuiCol_ButtonActive]         = accent_md;
    col[ImGuiCol_Header]               = btn;
    col[ImGuiCol_HeaderHovered]        = btn_hov;
    col[ImGuiCol_HeaderActive]         = accent_lo;
    col[ImGuiCol_Separator]            = sep;
    col[ImGuiCol_SeparatorHovered]     = accent_md;
    col[ImGuiCol_SeparatorActive]      = accent_hi;
    col[ImGuiCol_Tab]                  = c.frame;
    col[ImGuiCol_TabHovered]           = btn_hov;
    col[ImGuiCol_TabSelected]          = accent_lo;
    col[ImGuiCol_Text]                 = c.text;
    col[ImGuiCol_TextDisabled]         = c.text_dim;

    // Style vars
    s.WindowRounding    = 10.0f;
    s.FrameRounding     =  6.0f;
    s.GrabRounding      =  6.0f;
    s.FramePadding      = ImVec2(8.0f, 5.0f);
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ScrollbarRounding =  8.0f;
    s.WindowBorderSize  =  1.0f;
    s.FrameBorderSize   =  0.0f;
    s.ScrollbarSize     = 12.0f;
    s.WindowPadding     = ImVec2(12.0f, 10.0f);
}

void PhysicsInterface::pop_theme() {
    // No-op: theme is applied via direct style assignment, not push/pop stack
}

// ── Helper: draw a particle spawn button ─────────────────────────────────────

static bool spawn_button(int type_idx, const char* label, ImVec4 color,
                          int current_spawn_type, int current_spawn_group,
                          const char* tooltip, ImVec2 size = ImVec2(45, 32))
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(color.x * 0.55f, color.y * 0.55f, color.z * 0.55f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(color.x * 0.7f, color.y * 0.7f, color.z * 0.7f, 1.0f));

    bool selected = (current_spawn_type == type_idx && current_spawn_group == -1);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }

    bool clicked = ImGui::Button(label, size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);

    if (selected) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor(3);

    return clicked;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Main UI entry point ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::render_imgui(SimConfig& cfg, Particles& particles, ForceObject* force_objects, bool& request_reset) {
    // Ctrl+S / Ctrl+L hotkeys
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        show_save_dialog = true;
        browse_needs_refresh = true;
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        show_load_dialog = true;
        browse_needs_refresh = true;
    }

    // F1 toggle settings panel
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        settings_visible = !settings_visible;

    // Auto-disable select_mode when other modes activate
    if (pending_spawn || force_obj_placement_mode || accel_mode || mirror_placement_mode
        || thermo_probe_placement_mode || velocity_meter_mode
        || ruler_placement_mode || density_counter_placement_mode)
        select_mode = false;

    push_theme();

    // Splash screen (blocks all other UI until dismissed)
    if (show_splash) {
        draw_splash_screen();
        pop_theme();
        return;
    }

    // Pause menu (blocks other UI when visible)
    if (show_pause_menu) {
        draw_pause_menu(cfg, request_reset);
        pop_theme();
        return;
    }

    // Settings menu (blocks other UI when visible)
    if (show_settings_menu) {
        draw_settings_menu();
        pop_theme();
        return;
    }

    // Achievements panel (blocks other UI when visible)
    if (show_achievements_panel) {
        draw_achievements_panel();
        pop_theme();
        return;
    }

    // Draw bottom bar (always visible)
    draw_bottom_bar(cfg, request_reset);

    // Draw settings panel
    if (settings_visible)
        draw_settings_panel(cfg);

    // Draw spawn menu
    if (spawn_menu_visible)
        draw_spawn_menu(cfg);

    // Draw force object panel (if selected)
    if (selected_force_obj_idx >= 0)
        draw_force_object_panel(force_objects);

    // Draw accelerator panel
    if (accel_mode)
        draw_accelerator_panel();

    // Save/Load/Import dialog (drawn before cards so cards render on top)
    if (show_save_dialog || show_load_dialog || show_import_dialog || show_molecule_import_dialog)
        draw_save_load_dialog();

    // Draw element list window (center, drawn before cards so cards overlay)
    draw_element_list();

    // Draw particle list window
    draw_particle_list(particles);

    // Draw decay log window
    draw_decay_log();

    // Draw nuclear reactions debug window
    draw_nuclear_debug(cfg);

    // Draw particle info card (bottom-right, always on top)
    draw_info_card(particles);

    // Draw element detail card (bottom-right, always on top)
    if (element_card_nucleus_rep >= 0)
        draw_element_card(particles);

    // Draw molecule detail card
    if (molecule_card_atom_rep >= 0)
        draw_molecule_card(particles);

    // Fade save/load status message
    if (save_load_msg_timer > 0.0f)
        save_load_msg_timer -= ImGui::GetIO().DeltaTime;

    // ── Accelerator aim visualization overlay ──
    if (accel_mode && accel_phase == 1 && accel_source_idx >= 0) {
        ImGuiIO& io = ImGui::GetIO();
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;
        float scx = win_w / static_cast<float>(REGION_W);
        float scy = win_h / static_cast<float>(REGION_H);

        // World-to-screen
        auto w2s = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };

        ImVec2 tgt_scr = w2s(accel_source_world_pos);
        ImVec2 mouse = io.MousePos;

        // Direction from mouse toward target
        float dx = tgt_scr.x - mouse.x;
        float dy = tgt_scr.y - mouse.y;
        float len = std::sqrt(dx * dx + dy * dy);

        if (len > 1.0f) {
            float nx = dx / len;
            float ny = dy / len;

            ImVec4 tc = PHYS_TYPE_UI_COLORS[accel_fire_type];
            ImU32 line_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.6f));
            ImU32 dot_col  = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 1.0f));

            ImDrawList* fg = ImGui::GetForegroundDrawList();

            // Target indicator circle (crosshair)
            fg->AddCircle(tgt_scr, 14.0f, dot_col, 16, 2.0f);
            fg->AddCircle(tgt_scr, 6.0f, dot_col, 12, 1.5f);

            // Dashed aim line from mouse toward target
            float line_len = len;
            float dash = 8.0f, gap = 4.0f, t = 0.0f;
            bool draw_seg = true;
            while (t < line_len) {
                float seg = draw_seg ? dash : gap;
                seg = std::min(seg, line_len - t);
                if (draw_seg) {
                    ImVec2 a(mouse.x + nx * t, mouse.y + ny * t);
                    ImVec2 b(mouse.x + nx * (t + seg), mouse.y + ny * (t + seg));
                    fg->AddLine(a, b, line_col, 2.0f);
                }
                t += seg;
                draw_seg = !draw_seg;
            }

            // Arrowhead pointing at target (near target end)
            float as = 10.0f;
            ImVec2 tip = tgt_scr;
            ImVec2 left(tip.x - nx*as + ny*as*0.5f, tip.y - ny*as - nx*as*0.5f);
            ImVec2 right(tip.x - nx*as - ny*as*0.5f, tip.y - ny*as + nx*as*0.5f);
            fg->AddTriangleFilled(tip, left, right, dot_col);

            // Triple shot spread lines (from mouse toward target)
            if (accel_fire_mode == 1) {
                float spread = 5.0f * 3.14159265f / 180.0f;
                float base_a = std::atan2(ny, nx);
                ImU32 spread_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.25f));
                for (int s = -1; s <= 1; s += 2) {
                    float a = base_a + s * spread;
                    ImVec2 end(mouse.x + std::cos(a) * line_len,
                              mouse.y + std::sin(a) * line_len);
                    fg->AddLine(mouse, end, spread_col, 1.0f);
                }
            }

            // Stream mode: pulsing indicator at mouse (fire origin)
            if (accel_fire_mode == 2) {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetFrameCount()) * 0.15f);
                ImU32 pulse_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, pulse * 0.4f));
                fg->AddCircleFilled(mouse, 8.0f, pulse_col);
            }
        }
    }

    // ── Mirror placement preview overlay ──
    if (mirror_placement_mode && mirror_placement_phase == 1) {
        ImGuiIO& mio = ImGui::GetIO();
        float win_w = mio.DisplaySize.x;
        float win_h = mio.DisplaySize.y;
        float scx = win_w / static_cast<float>(REGION_W);
        float scy = win_h / static_cast<float>(REGION_H);
        auto w2s = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };
        ImVec2 p1 = w2s(mirror_endpoint1);
        ImVec2 p2 = mio.MousePos;
        ImU32 line_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.8f, 0.6f));
        ImU32 dot_col  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 1.0f, 1.0f));
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddLine(p1, p2, line_col, 2.5f);
        fg->AddCircleFilled(p1, 4.0f, dot_col);
        fg->AddCircleFilled(p2, 4.0f, dot_col);
    }
    if (mirror_placement_mode) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImGuiIO& mio = ImGui::GetIO();
        const char* txt = (mirror_placement_phase == 0)
            ? "Click to set mirror start point"
            : "Click to set mirror end point";
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImVec2 pos(mio.DisplaySize.x * 0.5f - ts.x * 0.5f, 30.0f);
        fg->AddText(pos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.8f, 1.0f)), txt);
    }

    // ── Electron cloud (orbital shell rings) overlay ──
    if (show_electron_cloud && !nucleus_clouds.empty()) {
        ImGuiIO& cio = ImGui::GetIO();
        float win_w = cio.DisplaySize.x, win_h = cio.DisplaySize.y;
        float scx = win_w / static_cast<float>(REGION_W);
        float scy = win_h / static_cast<float>(REGION_H);
        auto w2s_cloud = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };

        ImDrawList* fg = ImGui::GetForegroundDrawList();

        // Shell colors: inner → outer, more saturated when filled
        const ImVec4 SHELL_COLORS_MATTER[] = {
            ImVec4(0.3f, 0.6f, 1.0f, 1.0f),   // shell 1: blue
            ImVec4(0.3f, 0.9f, 0.5f, 1.0f),   // shell 2: green
            ImVec4(0.9f, 0.7f, 0.2f, 1.0f),   // shell 3: gold
        };
        const ImVec4 SHELL_COLORS_ANTI[] = {
            ImVec4(0.0f, 0.85f, 0.95f, 1.0f),  // shell 1: cyan
            ImVec4(0.6f, 0.3f, 0.95f, 1.0f),   // shell 2: purple
            ImVec4(0.95f, 0.4f, 0.6f, 1.0f),   // shell 3: pink
        };

        for (const auto& cloud : nucleus_clouds) {
            ImVec2 center = w2s_cloud(cloud.center);

            // Cull offscreen nuclei (generous margin for large shells)
            float max_r = cloud.shell_radii[2] * cfg.current_camera_zoom * scx;
            if (center.x < -max_r || center.x > win_w + max_r ||
                center.y < -max_r || center.y > win_h + max_r) continue;

            // Skip if rings would be too tiny to see
            float min_r_px = cloud.shell_radii[0] * cfg.current_camera_zoom * scx;
            if (min_r_px < 2.0f) continue;

            const ImVec4* shell_colors = cloud.is_anti ? SHELL_COLORS_ANTI : SHELL_COLORS_MATTER;

            for (int s = 0; s < 3; ++s) {
                if (cloud.shell_cap[s] == 0) continue;  // shouldn't happen

                float r_px = cloud.shell_radii[s] * cfg.current_camera_zoom * scx;
                if (r_px < 1.5f) continue;

                float fill_frac = static_cast<float>(cloud.shell_fill[s])
                                / static_cast<float>(cloud.shell_cap[s]);
                bool is_full = (cloud.shell_fill[s] >= cloud.shell_cap[s]);
                bool is_empty = (cloud.shell_fill[s] == 0);

                ImVec4 sc = shell_colors[s];

                if (is_empty) {
                    // Empty shell: dim dashed ring
                    float alpha = 0.15f;
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(sc.x, sc.y, sc.z, alpha));

                    // Draw dashed circle
                    float circumference = 2.0f * 3.14159265f * r_px;
                    int segments = std::max(24, static_cast<int>(circumference / 6.0f));
                    float dash_angle = 3.14159265f * 2.0f / segments;
                    for (int seg = 0; seg < segments; seg += 2) {
                        float a1 = seg * dash_angle;
                        float a2 = (seg + 1) * dash_angle;
                        ImVec2 p1(center.x + std::cos(a1) * r_px,
                                  center.y + std::sin(a1) * r_px);
                        ImVec2 p2(center.x + std::cos(a2) * r_px,
                                  center.y + std::sin(a2) * r_px);
                        fg->AddLine(p1, p2, col, 1.0f);
                    }
                } else if (is_full) {
                    // Full shell: solid bright ring
                    float alpha = 0.5f;
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(sc.x, sc.y, sc.z, alpha));
                    fg->AddCircle(center, r_px, col, 0, 1.5f);
                } else {
                    // Partial fill: draw filled arc + dashed remainder
                    float filled_angle = fill_frac * 2.0f * 3.14159265f;
                    float alpha_filled = 0.4f;
                    float alpha_empty = 0.12f;

                    ImU32 col_filled = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(sc.x, sc.y, sc.z, alpha_filled));
                    ImU32 col_empty = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(sc.x, sc.y, sc.z, alpha_empty));

                    // Filled portion: solid arc
                    float circumference = 2.0f * 3.14159265f * r_px;
                    int total_segs = std::max(32, static_cast<int>(circumference / 4.0f));
                    float seg_angle = 2.0f * 3.14159265f / total_segs;

                    for (int seg = 0; seg < total_segs; ++seg) {
                        float a1 = seg * seg_angle;
                        float a2 = (seg + 1) * seg_angle;
                        ImVec2 p1(center.x + std::cos(a1) * r_px,
                                  center.y + std::sin(a1) * r_px);
                        ImVec2 p2(center.x + std::cos(a2) * r_px,
                                  center.y + std::sin(a2) * r_px);

                        if (a1 < filled_angle) {
                            fg->AddLine(p1, p2, col_filled, 1.5f);
                        } else if (seg % 2 == 0) {
                            fg->AddLine(p1, p2, col_empty, 1.0f);
                        }
                    }
                }

                // Label: "N/M" fill count on the ring (if big enough to read)
                if (r_px > 20.0f) {
                    char label[16];
                    snprintf(label, sizeof(label), "%d/%d", cloud.shell_fill[s], cloud.shell_cap[s]);
                    ImVec2 ts = ImGui::CalcTextSize(label);
                    // Position label at top of ring
                    ImVec2 label_pos(center.x - ts.x * 0.5f,
                                     center.y - r_px - ts.y - 1.0f);
                    ImU32 text_col = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(sc.x, sc.y, sc.z, 0.7f));
                    fg->AddText(label_pos, text_col, label);
                }
            }

            // Element symbol at nucleus center (if zoomed in enough)
            float inner_r_px = cloud.shell_radii[0] * cfg.current_camera_zoom * scx;
            if (inner_r_px > 10.0f && cloud.Z >= 1 && cloud.Z <= FULL_ELEMENT_COUNT) {
                const char* sym = ELEMENT_SYMBOLS[cloud.Z];
                char label[16];
                if (cloud.is_anti)
                    snprintf(label, sizeof(label), "%s", sym);
                else
                    snprintf(label, sizeof(label), "%s", sym);
                ImVec2 ts = ImGui::CalcTextSize(label);
                ImVec2 lp(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
                ImVec4 lc = cloud.is_anti ? ImVec4(0.0f, 0.9f, 1.0f, 0.6f)
                                          : ImVec4(0.9f, 0.85f, 0.6f, 0.6f);
                fg->AddText(lp, ImGui::ColorConvertFloat4ToU32(lc), label);
            }
        }
    }

    // ── Entanglement visualization overlay ──
    if (cfg.entanglement_enabled && readback_positions_ptr && entangled_partners_ptr
        && readback_count > 0) {
        ImGuiIO& eio = ImGui::GetIO();
        float win_w = eio.DisplaySize.x, win_h = eio.DisplaySize.y;
        float scx = win_w / static_cast<float>(REGION_W);
        float scy = win_h / static_cast<float>(REGION_H);
        auto w2s_ent = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImU32 ent_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.7f, 1.0f, 0.35f));

        for (uint32_t ei = 0; ei < readback_count; ++ei) {
            uint32_t ep = entangled_partners_ptr[ei];
            if (ep == 0xFFFFFFFFu || ep >= readback_count || ei > ep) continue;

            ImVec2 a = w2s_ent(readback_positions_ptr[ei]);
            ImVec2 b = w2s_ent(readback_positions_ptr[ep]);

            // Skip if both off screen
            if (a.x < -50.0f || a.x > win_w + 50.0f ||
                a.y < -50.0f || a.y > win_h + 50.0f) continue;
            if (b.x < -50.0f || b.x > win_w + 50.0f ||
                b.y < -50.0f || b.y > win_h + 50.0f) continue;

            float edx = b.x - a.x, edy = b.y - a.y;
            float elen = std::sqrt(edx * edx + edy * edy);
            if (elen < 1.0f || elen > 800.0f) continue;

            float enx = edx / elen, eny = edy / elen;
            float dash = 6.0f, gap = 4.0f, et = 0.0f;
            bool draw_seg = true;
            while (et < elen) {
                float seg = draw_seg ? dash : gap;
                seg = std::min(seg, elen - et);
                if (draw_seg) {
                    fg->AddLine(ImVec2(a.x + enx * et, a.y + eny * et),
                               ImVec2(a.x + enx * (et + seg), a.y + eny * (et + seg)),
                               ent_col, 1.0f);
                }
                et += seg;
                draw_seg = !draw_seg;
            }
        }
    }

    // ── Covalent bond overlay ──────────────────────────────────────────────
    if (cfg.bonds_enabled && bond_data_ptr && readback_positions_ptr && readback_count > 0) {
        ImGuiIO& bio = ImGui::GetIO();
        float bwin_w = bio.DisplaySize.x, bwin_h = bio.DisplaySize.y;
        float scx = bwin_w / static_cast<float>(REGION_W);
        float scy = bwin_h / static_cast<float>(REGION_H);
        auto w2s_bond = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(bwin_w, bwin_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };

        ImDrawList* bfg = ImGui::GetForegroundDrawList();
        ImU32 bond_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.65f, 1.0f, 0.5f));
        uint32_t max_particles = bond_data_count / MAX_BONDS_PER_PARTICLE;

        for (uint32_t bi = 0; bi < max_particles && bi < readback_count; ++bi) {
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                uint32_t partner = bond_data_ptr[bi * MAX_BONDS_PER_PARTICLE + s];
                if (partner == 0xFFFFFFFFu || partner >= readback_count) continue;
                if (bi > partner) continue;  // draw once per pair

                ImVec2 a = w2s_bond(readback_positions_ptr[bi]);
                ImVec2 b = w2s_bond(readback_positions_ptr[partner]);

                // Skip off-screen
                if (a.x < -50.0f || a.x > bwin_w + 50.0f ||
                    a.y < -50.0f || a.y > bwin_h + 50.0f) continue;
                if (b.x < -50.0f || b.x > bwin_w + 50.0f ||
                    b.y < -50.0f || b.y > bwin_h + 50.0f) continue;

                float dx = b.x - a.x, dy = b.y - a.y;
                float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1.0f || len > 600.0f) continue;

                bfg->AddLine(a, b, bond_col, 1.5f);
            }
        }
    }

    // ── Visualization overlays ───────────────────────────────────────────────
    if (show_energy_heatmap)    draw_energy_heatmap(cfg);
    if (show_velocity_field)    draw_velocity_field(cfg);
    if (show_magnetic_field)    draw_magnetic_field(cfg);
    if (show_trajectory_tracer) draw_trajectory_traces(cfg);
    if (show_force_vectors)     draw_force_vectors_overlay(cfg);
    if (show_atom_grid)         draw_atom_grid(cfg);

    // ── Measurement overlays ─────────────────────────────────────────────────
    if (!thermo_probes.empty() || !velocity_meters.empty() ||
        !distance_rulers.empty() || !density_counters.empty())
        draw_measurement_overlays(cfg);

    if (!thermo_probes.empty() || !velocity_meters.empty() ||
        !distance_rulers.empty() || !density_counters.empty())
        draw_measurement_panel();

    // ── Placement mode hints ─────────────────────────────────────────────────
    if (thermo_probe_placement_mode || density_counter_placement_mode ||
        ruler_placement_mode || velocity_meter_mode) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const char* txt = thermo_probe_placement_mode ? "Click to place thermometer probe" :
                          density_counter_placement_mode ? "Click to place density counter" :
                          ruler_placement_mode ? (ruler_placement_phase == 0 ?
                              "Click to set ruler start point" : "Click to set ruler end point") :
                          "Click a particle to attach velocity meter";
        ImVec2 ts = ImGui::CalcTextSize(txt);
        ImGuiIO& io2 = ImGui::GetIO();
        fg->AddRectFilled(ImVec2(io2.DisplaySize.x * 0.5f - ts.x * 0.5f - 8, 26),
                          ImVec2(io2.DisplaySize.x * 0.5f + ts.x * 0.5f + 8, 50),
                          IM_COL32(0, 0, 0, 180), 4.0f);
        fg->AddText(ImVec2(io2.DisplaySize.x * 0.5f - ts.x * 0.5f, 30.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.9f, 1.0f)), txt);
    }

    // Draw notifications last so they render on top of all windows
    draw_notifications();

    pop_theme();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Splash Screen ───────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// ── Splash animation helpers ─────────────────────────────────────────────────

static void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                              ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 12;
    for (int s = STEPS; s >= 0; --s) {
        float t = (float)s / STEPS;
        float r = radius * t;
        if (r < 1.0f) continue;
        float blend = 1.0f - t;
        int a_c = (center_col >> IM_COL32_A_SHIFT) & 0xFF;
        int a_e = (edge_col   >> IM_COL32_A_SHIFT) & 0xFF;
        int a = a_c + (int)((a_e - a_c) * blend);
        int r_c = (center_col >> IM_COL32_R_SHIFT) & 0xFF, r_e = (edge_col >> IM_COL32_R_SHIFT) & 0xFF;
        int g_c = (center_col >> IM_COL32_G_SHIFT) & 0xFF, g_e = (edge_col >> IM_COL32_G_SHIFT) & 0xFF;
        int b_c = (center_col >> IM_COL32_B_SHIFT) & 0xFF, b_e = (edge_col >> IM_COL32_B_SHIFT) & 0xFF;
        int rr = r_c + (int)((r_e - r_c) * blend);
        int gg = g_c + (int)((g_e - g_c) * blend);
        int bb = b_c + (int)((b_e - b_c) * blend);
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 32);
    }
}

static void draw_vignette(ImDrawList* dl, float W, float H) {
    // Top fade
    for (int i = 0; i < 20; ++i) {
        float t = (float)i / 20.0f;
        int a = (int)((1.0f - t) * 0.8f * 255);
        dl->AddRectFilled(ImVec2(0, i * (H / 80.0f)), ImVec2(W, (i+1) * (H / 80.0f)),
                          IM_COL32(2, 8, 16, a));
    }
    // Bottom fade
    for (int i = 0; i < 30; ++i) {
        float t = (float)i / 30.0f;
        float band = H / 60.0f;
        float y = H - (30 - i) * band;
        int a = (int)((1.0f - t) * 0.95f * 255);
        dl->AddRectFilled(ImVec2(0, y), ImVec2(W, y + band),
                          IM_COL32(2, 8, 16, a));
    }
}

// ── Splash particle initialization ───────────────────────────────────────────

void PhysicsInterface::init_splash_particles() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float ncx = W * 0.48f, ncy = H * 0.38f;

    splash_particles_.clear();
    splash_trails_.clear();
    splash_time_ = 0.0f;

    std::mt19937 rng(42);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto randf_range = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    // Particle type colors (matching banner.html)
    struct SplashType { ImU32 color; ImU32 glow; float r_min, r_max; };
    SplashType types[] = {
        { IM_COL32(0xff, 0x44, 0x33, 255), IM_COL32(0xff, 0x22, 0x00, 0x66), 4, 8 },   // proton
        { IM_COL32(0x44, 0x88, 0xcc, 255), IM_COL32(0x33, 0x66, 0xaa, 0x66), 4, 7 },   // neutron
        { IM_COL32(0x00, 0xdd, 0xff, 255), IM_COL32(0x00, 0xaa, 0xff, 0x44), 2, 4 },   // electron
        { IM_COL32(0xff, 0xcc, 0x00, 255), IM_COL32(0xff, 0xaa, 0x00, 0x33), 1.5f, 3 },// photon
        { IM_COL32(0x00, 0xff, 0xaa, 255), IM_COL32(0x00, 0xdd, 0x88, 0x44), 2, 4 },   // positron
        { IM_COL32(0xcc, 0x55, 0xff, 255), IM_COL32(0xaa, 0x33, 0xff, 0x44), 2.5f, 5 },// quark
        { IM_COL32(0xff, 0x66, 0x88, 255), IM_COL32(0xff, 0x44, 0x66, 0x44), 1.5f, 3 },// muon
        { IM_COL32(0x88, 0xff, 0x44, 255), IM_COL32(0x66, 0xdd, 0x22, 0x44), 2, 3 },   // gluon
    };

    // Nucleus cluster: 6 protons + 6 neutrons
    for (int i = 0; i < 12; ++i) {
        float angle = randf() * 6.2831853f;
        float dist = randf() * 18.0f * scale;
        auto& t = types[i < 6 ? 0 : 1];
        SplashParticle p{};
        p.x = ncx + cosf(angle) * dist;
        p.y = ncy + sinf(angle) * dist;
        p.vx = randf_range(-0.5f, 0.5f) * 0.15f;
        p.vy = randf_range(-0.5f, 0.5f) * 0.15f;
        p.r = randf_range(t.r_min, t.r_max) * scale;
        p.color = t.color;
        p.glow_color = t.glow;
        p.orbit = false;
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // 6 orbiting electrons
    for (int i = 0; i < 6; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (40.0f + i * 22.0f + randf() * 10.0f) * scale;
        p.orbit_speed = 0.008f + randf() * 0.006f;
        p.phase = (6.2831853f * i / 6.0f) + randf() * 0.5f;
        p.cx = ncx;
        p.cy = ncy;
        p.tilt_x = 0.5f + randf() * 0.5f;
        p.tilt_y = 0.7f + randf() * 0.4f;
        p.r = (2.5f + randf() * 1.5f) * scale;
        p.color = IM_COL32(0x00, 0xdd, 0xff, 255);
        p.glow_color = IM_COL32(0x00, 0xaa, 0xff, 0x66);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // 60 scattered ambient particles
    for (int i = 0; i < 60; ++i) {
        int ti = (int)(randf() * 8.0f);
        if (ti > 7) ti = 7;
        auto& t = types[ti];
        SplashParticle p{};
        p.x = randf() * W;
        p.y = randf() * H;
        p.vx = randf_range(-0.5f, 0.5f) * 0.8f;
        p.vy = randf_range(-0.5f, 0.5f) * 0.8f;
        p.r = randf_range(t.r_min, t.r_max) * scale;
        p.color = t.color;
        p.glow_color = t.glow;
        p.orbit = false;
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    splash_trails_.resize(splash_particles_.size());
}

// ── Animated Splash Screen ───────────────────────────────────────────────────

void PhysicsInterface::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();

    // Check for dismiss: any mouse button or key (skip first 0.3s to avoid accidental dismiss)
    if (splash_time_ > 0.3f) {
        bool dismiss = ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                    || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!dismiss) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
                    dismiss = true;
                    break;
                }
            }
        }
        if (dismiss) { show_splash = false; return; }
    }

    // Init particles on first call (or re-init on About)
    if (!splash_inited_) { init_splash_particles(); splash_inited_ = true; }

    float dt = io.DeltaTime;
    splash_time_ += dt;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float ncx = W * 0.48f, ncy = H * 0.38f;

    // 1. Background fill
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 8, 16, 255));

    // 2. Nebula glow
    draw_radial_glow(bg, W * 0.3f, H * 0.3f, 300.0f * scale,
                     IM_COL32(10, 20, 60, 80), IM_COL32(10, 20, 60, 0));

    // 3. Energy core glow around nucleus
    float core_r = (35.0f + sinf(splash_time_ * 2.0f) * 5.0f) * scale;
    draw_radial_glow(bg, ncx, ncy, core_r * 3.0f,
                     IM_COL32(255, 120, 60, 38), IM_COL32(255, 60, 30, 0));
    draw_radial_glow(bg, ncx, ncy, 160.0f * scale,
                     IM_COL32(0, 100, 255, 10), IM_COL32(0, 100, 255, 0));

    // 4. Update particles
    for (auto& p : splash_particles_) {
        if (p.orbit) {
            p.phase += p.orbit_speed * dt * 60.0f;
            p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
            p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        } else {
            p.x += p.vx * dt * 60.0f;
            p.y += p.vy * dt * 60.0f;
            if (p.x < -10) p.x = W + 10;
            if (p.x > W + 10) p.x = -10;
            if (p.y < -10) p.y = H + 10;
            if (p.y > H + 10) p.y = -10;
        }
    }

    // 5. Update trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        splash_trails_[i].push_back(ImVec2(splash_particles_[i].x, splash_particles_[i].y));
        if (splash_trails_[i].size() > 8) splash_trails_[i].erase(splash_trails_[i].begin());
    }

    // 6. Draw trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& trail = splash_trails_[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float alpha = (float)j / (float)trail.size() * 0.3f;
            ImU32 col = (splash_particles_[i].color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j-1], trail[j], col, splash_particles_[i].r * 0.6f);
        }
    }

    // 7. Draw force lines between nearby particles
    float force_dist = 50.0f * scale;
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        for (size_t j = i + 1; j < splash_particles_.size(); ++j) {
            float dx = splash_particles_[j].x - splash_particles_[i].x;
            float dy = splash_particles_[j].y - splash_particles_[i].y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < force_dist * force_dist && dist2 > 25.0f) {
                float dist = sqrtf(dist2);
                float alpha = (1.0f - dist / force_dist) * 0.12f;
                bg->AddLine(ImVec2(splash_particles_[i].x, splash_particles_[i].y),
                           ImVec2(splash_particles_[j].x, splash_particles_[j].y),
                           IM_COL32(0, 180, 255, (int)(alpha * 255)), 0.5f);
            }
        }
    }

    // 8. Draw particles (glow + core + highlight)
    for (auto& p : splash_particles_) {
        draw_radial_glow(bg, p.x, p.y, p.r * 4.0f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
        bg->AddCircleFilled(ImVec2(p.x - p.r * 0.2f, p.y - p.r * 0.2f),
                            p.r * 0.5f, IM_COL32(255, 255, 255, 80));
    }

    // 9. Vignette overlay
    draw_vignette(bg, W, H);

    // 10. Scanlines
    float scanline_gap = 4.0f * scale;
    if (scanline_gap < 2.0f) scanline_gap = 2.0f;
    for (float y = 0; y < H; y += scanline_gap)
        bg->AddRectFilled(ImVec2(0, y + scanline_gap * 0.5f), ImVec2(W, y + scanline_gap),
                          IM_COL32(0, 0, 0, 8));

    // 11. Text overlay — transparent fullscreen ImGui window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##Splash", nullptr, flags)) {
        float title_y = H - 80.0f * scale;
        float left_margin = 30.0f * scale;

        // Accent line
        bg->AddLine(ImVec2(left_margin, title_y - 8.0f * scale),
                    ImVec2(left_margin + 180.0f * scale, title_y - 8.0f * scale),
                    IM_COL32(0, 200, 255, 153), 1.0f);

        // Title "Particle Playground" — large text
        float old_scale = ImGui::GetFont()->Scale;
        float title_font_scale = 2.2f * scale;
        if (title_font_scale < 1.5f) title_font_scale = 1.5f;
        ImGui::GetFont()->Scale = title_font_scale;
        ImGui::PushFont(ImGui::GetFont());

        // "Particle " in white
        ImGui::SetCursorPos(ImVec2(left_margin, title_y));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Particle ");
        ImGui::SameLine(0, 0);
        // "Playground" in cyan
        ImGui::TextColored(ImVec4(0.0f, 0.83f, 1.0f, 1.0f), "Playground");

        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Subtitle
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.0f, 0.78f, 1.0f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types");

        // Top-right badge "QUANTUM PHYSICS SANDBOX"
        {
            const char* badge = "QUANTUM PHYSICS SANDBOX";
            ImVec2 badge_sz = ImGui::CalcTextSize(badge);
            float badge_x = W - badge_sz.x - 18.0f * scale;
            float badge_y = 14.0f * scale;
            // Badge background
            bg->AddRectFilled(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                              ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                              IM_COL32(255, 60, 30, 20));
            bg->AddRect(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                        ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                        IM_COL32(255, 100, 60, 77), 2.0f);
            ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
            ImGui::TextColored(ImVec4(1.0f, 0.39f, 0.24f, 0.9f), "%s", badge);
        }

        // Bottom-center dismiss hint (pulsing alpha)
        float pulse = 0.3f + 0.2f * sinf(splash_time_ * 3.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 30.0f * scale));
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.58f, pulse), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Pause Menu (Escape key) ─────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_pause_menu(SimConfig& /*cfg*/, bool& request_reset) {
    ImGuiIO& io = ImGui::GetIO();

    // Semi-transparent fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##PauseOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 140.0f));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Menu buttons (centered column)
        float btn_w = 200.0f;
        float btn_h = 40.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;
        float btn_spacing = 52.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));

        // Resume
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            sim_running = true;
        }

        // New
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            request_reset = true;
        }

        // Save
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Save", ImVec2(btn_w, btn_h))) {
            show_save_dialog = true;
            show_load_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // Load
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Load", ImVec2(btn_w, btn_h))) {
            show_load_dialog = true;
            show_save_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // About
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("About", ImVec2(btn_w, btn_h))) {
            show_splash = true;
            splash_inited_ = false;  // re-init animation
            show_pause_menu = false;
        }

        // Achievements
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Achievements", ImVec2(btn_w, btn_h))) {
            show_achievements_panel = true;
            show_pause_menu = false;
        }

        // Settings
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
        if (ImGui::Button("Settings", ImVec2(btn_w, btn_h))) {
            show_settings_menu = true;
            show_pause_menu = false;
        }

        // Quit
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 7));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 0.95f));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // Hint text
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, btn_y + btn_spacing * 8 + 20.0f));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Settings Menu ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_settings_menu() {
    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##SettingsOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "SETTINGS";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 290.0f));

        const auto& tc = THEMES[std::clamp(prefs.theme, 0, THEME_COUNT - 1)];
        ImGui::TextColored(tc.accent, "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Settings panel (centered, scrollable)
        float panel_w = 420.0f;
        float panel_x = cx - panel_w * 0.5f;
        float panel_top = cy - 240.0f;
        float panel_bottom = cy + 220.0f;
        float panel_h = panel_bottom - panel_top;

        ImGui::SetCursorPos(ImVec2(panel_x, panel_top));
        ImGui::BeginChild("##SettingsScroll", ImVec2(panel_w, panel_h), false, ImGuiWindowFlags_NoBackground);
        ImGui::PushItemWidth(panel_w - 40.0f);

        // ── Display ──────────────────────────────────────────────────────
        ImGui::TextColored(tc.accent, "Display");
        ImGui::Separator();

        ImGui::Text("Temperature Units");
        ImGui::RadioButton("Kelvin",     &prefs.temp_unit, 0); ImGui::SameLine();
        ImGui::RadioButton("Celsius",    &prefs.temp_unit, 1); ImGui::SameLine();
        ImGui::RadioButton("Fahrenheit", &prefs.temp_unit, 2);

        ImGui::Checkbox("Show FPS", &prefs.show_fps);

        ImGui::SliderFloat("UI Scale", &prefs.ui_scale, 0.8f, 1.5f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale all UI elements (requires restart)");

        ImGui::Dummy(ImVec2(0, 10));

        // ── Performance ──────────────────────────────────────────────────
        ImGui::TextColored(tc.accent, "Performance");
        ImGui::Separator();

#ifdef HAS_OPENMP
        int sys_max = omp_get_max_threads();
        if (prefs.max_threads <= 0) prefs.max_threads = sys_max;
        prefs.max_threads = std::clamp(prefs.max_threads, 1, sys_max);
        ImGui::SliderInt("CPU Threads", &prefs.max_threads, 1, sys_max);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("OpenMP thread count for physics\nSystem max: %d", sys_max);
#else
        ImGui::TextDisabled("CPU Threads: single-threaded (no OpenMP)");
#endif

        const char* fps_labels[] = { "Uncapped", "30", "60", "120", "144", "240" };
        const int   fps_values[] = { 0, 30, 60, 120, 144, 240 };
        int fps_idx = 0;
        for (int i = 0; i < 6; i++) {
            if (fps_values[i] == prefs.fps_cap) { fps_idx = i; break; }
        }
        if (ImGui::Combo("FPS Cap", &fps_idx, fps_labels, 6))
            prefs.fps_cap = fps_values[fps_idx];

        const char* quality_labels[] = { "Low", "Medium", "High" };
        ImGui::Combo("Physics Quality", &prefs.physics_quality, quality_labels, 3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Low: essential physics, skip expensive checks\nMedium: most interactions at reduced rate\nHigh: full simulation fidelity every frame");

        ImGui::SliderInt("Physics Skip", &prefs.physics_skip, 0, 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Run CPU physics every (N+1) frames\n0 = every frame (best quality)\nHigher = better FPS, less accurate");

        ImGui::Checkbox("Spatial Grid", &prefs.spatial_grid);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use spatial acceleration grid for neighbor searches\nGreatly improves CPU physics performance\nDisable only for debugging");

        ImGui::Dummy(ImVec2(0, 10));

        // ── Theme ────────────────────────────────────────────────────────
        ImGui::TextColored(tc.accent, "Theme");
        ImGui::Separator();

        for (int i = 0; i < THEME_COUNT; i++) {
            const auto& th = THEMES[i];
            bool selected = (prefs.theme == i);

            // Color swatch + name as selectable
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float swatch_sz = 20.0f;

            if (ImGui::Selectable(("##theme_" + std::to_string(i)).c_str(), selected, 0, ImVec2(panel_w - 20.0f, 28.0f))) {
                prefs.theme = i;
            }

            // Draw color swatches over the selectable
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float sx = pos.x + 4.0f;
            float sy = pos.y + 4.0f;
            dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + swatch_sz, sy + swatch_sz),
                ImGui::ColorConvertFloat4ToU32(th.bg));
            dl->AddRectFilled(ImVec2(sx + swatch_sz + 3, sy), ImVec2(sx + swatch_sz * 2 + 3, sy + swatch_sz),
                ImGui::ColorConvertFloat4ToU32(th.accent));
            dl->AddRectFilled(ImVec2(sx + swatch_sz * 2 + 6, sy), ImVec2(sx + swatch_sz * 3 + 6, sy + swatch_sz),
                ImGui::ColorConvertFloat4ToU32(th.frame));

            // Label
            ImGui::SameLine(swatch_sz * 3 + 20.0f);
            ImGui::SetCursorPosY(pos.y - ImGui::GetWindowPos().y + 4.0f);
            if (selected)
                ImGui::TextColored(th.accent, "%s", THEME_NAMES[i]);
            else
                ImGui::Text("%s", THEME_NAMES[i]);
        }

        ImGui::Dummy(ImVec2(0, 10));

        // ── Audio ───────────────────────────────────────────────────────
        ImGui::TextColored(tc.accent, "Audio");
        ImGui::Separator();

        if (ImGui::Checkbox("Mute Music", &prefs.music_muted)) {
            if (audio_ptr) {
                if (prefs.music_muted) audio_ptr->pause();
                else                   audio_ptr->resume();
            }
        }

        if (!prefs.music_muted) {
            float vol_pct = prefs.music_volume * 100.0f;
            if (ImGui::SliderFloat("Music Volume", &vol_pct, 0.0f, 100.0f, "%.0f%%")) {
                prefs.music_volume = vol_pct / 100.0f;
                if (audio_ptr) audio_ptr->set_volume(prefs.music_volume);
            }
        }

        ImGui::PopItemWidth();
        ImGui::EndChild();

        // ── Back button ──────────────────────────────────────────────────
        float btn_w = 160.0f;
        float btn_h = 36.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, panel_bottom + 15.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("Back", ImVec2(btn_w, btn_h))) {
            show_settings_menu = false;
            show_pause_menu = true;
            save_prefs();
        }
        ImGui::PopStyleVar();

        // Hint
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, panel_bottom + 60.0f));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Achievements Panel ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_achievements_panel() {
    if (!achievements_ptr) return;

    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##AchievementsOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "ACHIEVEMENTS";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, 30.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Progress summary
        int unlocked = achievements_ptr->unlocked_count();
        char progress_buf[64];
        snprintf(progress_buf, sizeof(progress_buf), "%d / %d Unlocked", unlocked, ACH_COUNT);
        ImVec2 prog_size = ImGui::CalcTextSize(progress_buf);
        ImGui::SetCursorPos(ImVec2(cx - prog_size.x * 0.5f, 72.0f));
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%s", progress_buf);

        // Progress bar
        float bar_w = 400.0f;
        ImGui::SetCursorPos(ImVec2(cx - bar_w * 0.5f, 95.0f));
        float fraction = (ACH_COUNT > 0) ? static_cast<float>(unlocked) / static_cast<float>(ACH_COUNT) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.843f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
        ImGui::PushItemWidth(bar_w);
        ImGui::ProgressBar(fraction, ImVec2(bar_w, 16.0f), "");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);

        // Scrollable content area with category tabs
        float content_top = 125.0f;
        float content_bottom = io.DisplaySize.y - 70.0f;
        float panel_w = std::min(700.0f, io.DisplaySize.x - 80.0f);

        ImGui::SetCursorPos(ImVec2(cx - panel_w * 0.5f, content_top));
        ImGui::BeginChild("##AchContent", ImVec2(panel_w, content_bottom - content_top), ImGuiChildFlags_Border);

        for (int cat = 0; cat < ACAT_COUNT; cat++) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.18f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.24f, 0.35f, 1.0f));

            // Count unlocked in this category
            int cat_total = 0, cat_unlocked = 0;
            for (uint32_t i = 0; i < ACH_COUNT; i++) {
                if (ACHIEVEMENT_DEFS[i].category == cat) {
                    cat_total++;
                    if (achievements_ptr->is_unlocked(static_cast<AchievementID>(i)))
                        cat_unlocked++;
                }
            }

            char cat_label[128];
            snprintf(cat_label, sizeof(cat_label), "%s  (%d/%d)",
                     ACHIEVEMENT_CATEGORY_NAMES[cat], cat_unlocked, cat_total);

            bool open = ImGui::CollapsingHeader(cat_label, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(2);

            if (open) {
                for (uint32_t i = 0; i < ACH_COUNT; i++) {
                    const auto& def = ACHIEVEMENT_DEFS[i];
                    if (def.category != cat) continue;

                    bool done = achievements_ptr->is_unlocked(def.id);

                    // Achievement row
                    ImGui::PushID(i);
                    float row_h = 44.0f;
                    ImVec2 cursor = ImGui::GetCursorPos();

                    // Background highlight for unlocked
                    if (done) {
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImVec2 p1 = ImVec2(p0.x + panel_w - 20.0f, p0.y + row_h);
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            p0, p1, IM_COL32(40, 60, 30, 100), 4.0f);
                    }

                    // Icon
                    ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f, cursor.y + 4.0f));
                    if (done) {
                        ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 1.0f), "%s", def.icon);
                    } else {
                        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "[?]");
                    }

                    // Name
                    ImGui::SameLine(60.0f);
                    ImGui::SetCursorPosY(cursor.y + 4.0f);
                    if (done) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", def.name);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%s", def.name);
                    }

                    // Description
                    ImGui::SetCursorPos(ImVec2(cursor.x + 60.0f, cursor.y + 22.0f));
                    if (done) {
                        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.6f, 1.0f), "%s", def.description);
                    } else {
                        ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.4f, 1.0f), "%s", def.description);
                    }

                    ImGui::SetCursorPosY(cursor.y + row_h);
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }

        ImGui::EndChild();

        // Back button
        float btn_w = 160.0f;
        float btn_h = 36.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, content_bottom + 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));
        if (ImGui::Button("Back", ImVec2(btn_w, btn_h))) {
            show_achievements_panel = false;
            show_pause_menu = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Bottom Bar ──────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_bottom_bar(SimConfig& cfg, bool& request_reset) {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 42.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, display_h - bar_h));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Darker background for bottom bar
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.039f, 0.051f, 0.090f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

    if (ImGui::Begin("##BottomBar", nullptr, bar_flags)) {
        // Sim state indicator
        if (sim_running) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), ">> RUNNING");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "|| PAUSED");
        }

        // Timestep
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        ImGui::SetNextItemWidth(80);
        ImGui::SliderFloat("##TimeScale", &cfg.time_scale, 0.0f, 20.0f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Simulation speed\n0 = frozen, 5 = default, 20 = fast");

        // Emergent temperature
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        if (cfg.thermo_feedback_enabled && emergent_temp_display > 0.0f) {
            char etemp_buf[64];
            format_temperature(emergent_temp_display, etemp_buf, sizeof(etemp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "T: %s", etemp_buf);
        } else {
            char temp_buf[64];
            format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "T: %s", temp_buf);
        }

        // Emergent B-field
        if (cfg.magnetic_feedback_enabled && emergent_bfield_display > 0.001f) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "B: %.3f T", emergent_bfield_display);
        }

        // FPS
        if (prefs.show_fps) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            ImGui::Text("%.0f fps", fps_display);
        }

        // Active / Dormant
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        ImGui::Text("Active: %u", active_particle_display);
        ImGui::SameLine(0, 10);
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Dormant: %u", dormant_particle_display);

        // Energy
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        {
            char ebuf[32];
            float total_MeV = total_energy_display * E_SCALE_MEV;
            fmt_energy_ev(ebuf, sizeof(ebuf), total_MeV);
            ImGui::Text("E: %s", ebuf);
        }

        // Entropy trend indicator
        {
            ImGui::SameLine(0, 8);
            const char* s_arrow = (entropy_trend_display > 0) ? "S^" :
                                   (entropy_trend_display < 0) ? "Sv" : "S=";
            ImVec4 s_color = (entropy_trend_display >= 0)
                ? ImVec4(0.5f, 0.8f, 0.5f, 1.0f)
                : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(s_color, "%s", s_arrow);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Entropy trend\nS^ = increasing (2nd law)\nS= = equilibrium\nSv = decreasing (external work)");
        }

        // Nuclear decays (clickable — opens decay log)
        if (nuclear_decay_count_display > 0 || !decay_log.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char decay_btn[64];
            snprintf(decay_btn, sizeof(decay_btn), "Events: %u###DecayBtn",
                     static_cast<unsigned>(decay_log.size()));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.2f, 0.1f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.15f, 0.05f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            if (ImGui::SmallButton(decay_btn))
                show_decay_log = !show_decay_log;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to open decay/interaction event log");
        }

        // Nuclear debug (clickable)
        {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.15f, 0.05f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.1f, 0.02f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.15f, 1.0f));
            if (ImGui::SmallButton("Nuclear###NucDbgBtn"))
                show_nuclear_debug = !show_nuclear_debug;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Nuclear reactions debug — tune thresholds and rates");
        }

        // Particle count (clickable)
        {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char part_btn[48];
            snprintf(part_btn, sizeof(part_btn), "Particles: %u###PartBtn", active_particle_display);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.2f, 0.4f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.15f, 0.3f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.75f, 1.0f, 1.0f));
            if (ImGui::SmallButton(part_btn))
                show_particle_list = !show_particle_list;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to show particle list");
        }

        // Element count (clickable)
        if (!element_list.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char elem_btn[64];
            int nmol = 0;
            for (auto& m : molecule_list) if (m.atom_indices.size() > 1) nmol++;
            if (nmol > 0)
                snprintf(elem_btn, sizeof(elem_btn), "Atoms: %d  Mol: %d",
                         static_cast<int>(element_list.size()), nmol);
            else
                snprintf(elem_btn, sizeof(elem_btn), "Elements: %d", static_cast<int>(element_list.size()));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.2f, 0.4f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.6f, 1.0f));
            if (ImGui::SmallButton(elem_btn))
                show_element_list = !show_element_list;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to show element list");
        }

        // Type counts (inline colored) — show only active ones
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);

        // Nucleons
        if (type_counts_display[PROTON_TYPE])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[PROTON_TYPE], "p:%u", type_counts_display[PROTON_TYPE]); ImGui::SameLine(0, 6); }
        if (type_counts_display[NEUTRON_TYPE])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[NEUTRON_TYPE], "n:%u", type_counts_display[NEUTRON_TYPE]); ImGui::SameLine(0, 6); }
        if (type_counts_display[ELECTRON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[ELECTRON_TYPE_PHYS], "e-:%u", type_counts_display[ELECTRON_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        if (type_counts_display[PHOTON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[PHOTON_TYPE_PHYS], "y:%u", type_counts_display[PHOTON_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        if (type_counts_display[POSITRON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[POSITRON_TYPE_PHYS], "e+:%u", type_counts_display[POSITRON_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        if (type_counts_display[ANTIPROTON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[ANTIPROTON_TYPE_PHYS], "p-:%u", type_counts_display[ANTIPROTON_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        // Neutrinos
        if (type_counts_display[NEUTRINO_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[NEUTRINO_TYPE_PHYS], "ve:%u", type_counts_display[NEUTRINO_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        // Quarks (just show total if any)
        uint32_t quark_total = 0;
        for (uint32_t t = UP_QUARK_TYPE; t <= ANTI_BOTTOM_TYPE; ++t)
            quark_total += type_counts_display[t];
        if (quark_total)
            { ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.2f, 1.0f), "q:%u", quark_total); ImGui::SameLine(0, 6); }
        // Bosons (excluding photon, already shown)
        uint32_t boson_total = type_counts_display[GLUON_TYPE_PHYS]
            + type_counts_display[W_PLUS_TYPE_PHYS] + type_counts_display[W_MINUS_TYPE_PHYS]
            + type_counts_display[Z_BOSON_TYPE_PHYS] + type_counts_display[HIGGS_TYPE_PHYS];
        if (boson_total)
            { ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "B:%u", boson_total); ImGui::SameLine(0, 6); }
        // Hypothetical
        if (type_counts_display[GRAVITON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[GRAVITON_TYPE_PHYS], "G:%u", type_counts_display[GRAVITON_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        if (type_counts_display[DARK_MATTER_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[DARK_MATTER_TYPE_PHYS], "DM:%u", type_counts_display[DARK_MATTER_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        if (type_counts_display[DARK_ENERGY_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[DARK_ENERGY_TYPE_PHYS], "DE:%u", type_counts_display[DARK_ENERGY_TYPE_PHYS]); ImGui::SameLine(0, 6); }
        { uint32_t hyp_count = 0;
          for (uint32_t t = AXINO_TYPE_PHYS; t <= DYN_AXION_QP_TYPE_PHYS; t++) hyp_count += type_counts_display[t];
          if (hyp_count) { ImGui::TextColored(ImVec4(0.7f,0.5f,1.0f,1.0f), "Hyp:%u", hyp_count); ImGui::SameLine(0, 6); } }

        // Status indicators
        if (select_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.9f, 1.0f), "[SELECT]");
        }
        if (thermo_probe_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[THERMOMETER]");
        }
        if (velocity_meter_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "[VEL METER]");
        }
        if (ruler_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "[RULER]");
        }
        if (density_counter_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.6f, 0.3f, 1.0f, 1.0f), "[DENSITY]");
        }

        // Right-aligned Menu button
        float menu_btn_w = 70.0f;
        float x_right = display_w - 12.0f - menu_btn_w;
        ImGui::SameLine(x_right);

        if (ImGui::Button("Menu", ImVec2(menu_btn_w, 26))) {
            show_tools_popup = !show_tools_popup;
        }

        // Menu popup (rendered above the button)
        if (show_tools_popup) {
            float popup_w = 220.0f;
            float popup_h = 500.0f;
            float popup_x = display_w - 12.0f - popup_w;
            float popup_y = display_h - bar_h - popup_h - 4.0f;
            ImGui::SetNextWindowPos(ImVec2(popup_x, popup_y));
            ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h));
            ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059f, 0.071f, 0.130f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.14f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.32f, 1.0f));
            if (ImGui::Begin("##MenuPopup", &show_tools_popup, popup_flags)) {

                // ── Simulation ──
                if (ImGui::TreeNodeEx("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::MenuItem("Spawn (F3)", nullptr, spawn_menu_visible)) {
                        spawn_menu_visible = !spawn_menu_visible;
                    }
                    if (ImGui::MenuItem("Select (F4)", nullptr, select_mode)) {
                        select_mode = !select_mode;
                        if (select_mode) { pending_spawn = false; force_obj_placement_mode = false; }
                    }
                    if (ImGui::MenuItem("Reset (F2)")) {
                        request_reset = true;
                        show_tools_popup = false;
                    }
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── File ──
                if (ImGui::TreeNodeEx("File", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::MenuItem("Save (Ctrl+S)")) {
                        show_save_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    if (ImGui::MenuItem("Load (Ctrl+L)")) {
                        show_load_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import Element (.ppel)")) {
                        show_import_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    if (ImGui::MenuItem("Import Molecule (.ppmol)")) {
                        show_molecule_import_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── Tools ──
                // ── Measurement ──────────────────────────────────────────────
                if (ImGui::TreeNodeEx("Measurement", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto enter_meas_mode = [&]() {
                        pending_spawn = false;
                        select_mode = false;
                        force_obj_placement_mode = false;
                        accel_mode = false;
                        mirror_placement_mode = false;
                        thermo_probe_placement_mode = false;
                        velocity_meter_mode = false;
                        ruler_placement_mode = false;
                        density_counter_placement_mode = false;
                    };

                    if (ImGui::MenuItem("Thermometer Probe", nullptr, thermo_probe_placement_mode)) {
                        bool was = thermo_probe_placement_mode;
                        enter_meas_mode();
                        thermo_probe_placement_mode = !was;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to place a temperature probe\nShows local avg kinetic energy within radius");

                    if (ImGui::MenuItem("Velocity Meter", nullptr, velocity_meter_mode)) {
                        bool was = velocity_meter_mode;
                        enter_meas_mode();
                        velocity_meter_mode = !was;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click a particle to track its velocity\nShows speed, direction arrow, and kinetic energy");

                    if (ImGui::MenuItem("Distance Ruler", nullptr, ruler_placement_mode)) {
                        bool was = ruler_placement_mode;
                        enter_meas_mode();
                        ruler_placement_mode = !was;
                        ruler_placement_phase = 0;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click two points to measure distance\nDisplays world-space distance between points");

                    if (ImGui::MenuItem("Density Counter", nullptr, density_counter_placement_mode)) {
                        bool was = density_counter_placement_mode;
                        enter_meas_mode();
                        density_counter_placement_mode = !was;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click to place a density counter\nShows particle count and density within radius");

                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear All Measurements")) {
                        thermo_probes.clear();
                        velocity_meters.clear();
                        distance_rulers.clear();
                        density_counters.clear();
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove all placed measurement tools");

                    ImGui::TreePop();
                }

                // ── Visualization ───────────────────────────────────────────
                if (ImGui::TreeNodeEx("Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::MenuItem("Show Trails", nullptr, &cfg.show_trails);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Draw particle paths (fade effect)");

                    ImGui::MenuItem("Electron Cloud", nullptr, &show_electron_cloud);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Show orbital shell rings around nuclei\nDisplays expected electron shells and occupancy");

                    ImGui::MenuItem("Trajectory Tracer", nullptr, &show_trajectory_tracer);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Record and draw recent particle paths\nFading polylines colored by particle type");

                    ImGui::MenuItem("Energy Heatmap", nullptr, &show_energy_heatmap);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Overlay grid showing kinetic energy density\nBlue = low, Red = high");

                    ImGui::MenuItem("Velocity Field", nullptr, &show_velocity_field);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Overlay arrow grid showing average\nvelocity direction and magnitude");

                    ImGui::MenuItem("Magnetic Field", nullptr, &show_magnetic_field);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Overlay grid showing magnetic field Bz\nBlue = B<0, Red = B>0\nIncludes Biot-Savart + nucleon dipoles");

                    ImGui::MenuItem("Force Vectors", nullptr, &show_force_vectors);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Show force breakdown on selected particle\nCoulomb (red), Yukawa (green), Gravity (blue)");

                    ImGui::MenuItem("Wave Mode", nullptr, &wave_mode);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Toggle wave-particle duality\nRender all particles as de Broglie wave packets\nWavelength inversely proportional to momentum");

                    ImGui::MenuItem("Atom Grid", nullptr, &show_atom_grid);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Overlay grid where each cell = hydrogen atom diameter\n(2 Bohr radii = 0.106 nm)");

                    ImGui::TreePop();
                }

                // ── Tools ───────────────────────────────────────────────────
                if (ImGui::TreeNodeEx("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::MenuItem("Accelerator", nullptr, accel_mode)) {
                        accel_mode = !accel_mode;
                        if (accel_mode) {
                            accel_phase = 0;
                            accel_source_idx = -1;
                            pending_spawn = false;
                            select_mode = false;
                            force_obj_placement_mode = false;
                            thermo_probe_placement_mode = false;
                            velocity_meter_mode = false;
                            ruler_placement_mode = false;
                            density_counter_placement_mode = false;
                        }
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Particle accelerator tool\nSelect a target and fire particles at it");

                    if (ImGui::MenuItem("Mirror", nullptr, mirror_placement_mode)) {
                        mirror_placement_mode = !mirror_placement_mode;
                        if (mirror_placement_mode) {
                            mirror_placement_phase = 0;
                            pending_spawn = false;
                            select_mode = false;
                            force_obj_placement_mode = false;
                            accel_mode = false;
                            thermo_probe_placement_mode = false;
                            velocity_meter_mode = false;
                            ruler_placement_mode = false;
                            density_counter_placement_mode = false;
                        }
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Reflective mirror tool\nTwo clicks define a line segment\nParticles bounce off reflectively");

                    ImGui::Separator();
                    if (ImGui::MenuItem("Halt Velocities")) {
                        request_halt_velocities = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Set all particle velocities to zero");

                    if (ImGui::MenuItem("Remove Massless")) {
                        request_remove_massless = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove all massless particles\n(photons, gluons, gravitons, neutrinos)");

                    if (ImGui::MenuItem("Remove Massive")) {
                        request_remove_massive = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove all massive particles\n(everything except photons, gluons, etc.)");

                    ImGui::TreePop();
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Settings Panel (Left Sidebar) ───────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_settings_panel(SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, std::min(680.0f, max_h)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(350, max_h));

    if (!ImGui::Begin("Settings", &settings_visible)) {
        ImGui::End();
        return;
    }

    // ── Environment ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        int env = static_cast<int>(cfg.environment_mode);
        if (ImGui::Combo("Preset", &env, PHYS_ENV_NAMES, PHYS_ENV_COUNT)) {
            cfg.environment_mode = static_cast<uint32_t>(env);
            switch (env) {
                case 0:  // Lab Mode
                    cfg.start_empty = true;
                    cfg.temperature_kelvin = 1.0f;
                    cfg.dampening = 0.990f;
                    cfg.repulsion_radius = 1.0f;
                    cfg.pressure_resistance = 100.0f;
                    cfg.interaction_radius = 200.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.lorentz_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 100.0f;
                    cfg.hadronization_enabled = true;
                    cfg.viscosity_strength = 0.0f;
                    cfg.time_scale = 1.0f;
                    break;
                case 1:  // Hydrogen Plasma
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 100.0f;
                    break;
                case 2:  // Neutron Star Surface
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e9f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 120.0f;
                    break;
                case 3:  // Solar Core
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 110.0f;
                    break;
                case 4:  // Particle Soup
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5000.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 80.0f;
                    break;
                case 5:  // Alpha Emitter
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 300.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 60.0f;
                    break;
                case 6:  // Heavy Nucleus
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 100.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 1.0f;
                    particle_count_slider = 50.0f;
                    break;
                case 7:  // Quark-Gluon Plasma
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2e12f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.string_tension = 10.0f;
                    cfg.hadronization_enabled = false;  // quarks deconfined in QGP
                    cfg.weak_coupling = 0.5f;
                    particle_count_slider = 100.0f;
                    break;
                case 8:  // Electroweak Era
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e15f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 50.0f;
                    particle_count_slider = 80.0f;
                    break;
                case 9:  // Meson Factory
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5e11f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 50.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.string_tension = 60.0f;
                    cfg.weak_coupling = 0.2f;
                    particle_count_slider = 90.0f;
                    break;
                case 10:  // Particle Accelerator
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2.7f;
                    cfg.dampening = 0.995f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.lorentz_strength = 1.5f;
                    cfg.weak_coupling = 0.0f;
                    cfg.string_tension = 50.0f;
                    cfg.time_scale = 3.0f;
                    particle_count_slider = 60.0f;
                    break;
            }
            log_temperature = std::log10(std::max(1.0f, cfg.temperature_kelvin));
        }

        if (!cfg.start_empty) {
            ImGui::SliderFloat("Count", &particle_count_slider, 1.0f, 317.0f, "%.0f");
            int pc = static_cast<int>(std::max(2.0f, std::pow(particle_count_slider, 2.0f)));
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Particles: %d  (applied on Reset)", pc);
        } else {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Particles: %u  (Lab Mode)", cfg.particle_count);
        }

        ImGui::SliderInt("Seed", &seed_value, 0, 99999);
        cfg.generation_seed = static_cast<uint32_t>(seed_value);
    }

    // ── Temperature ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Temperature", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("##TempSlider", &log_temperature, 0.0f, 13.0f, "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Logarithmic temperature scale\n1 = 10 K, 3 = 1000 K, 7 = 10 MK");
        cfg.temperature_kelvin = std::pow(10.0f, log_temperature);

        char temp_buf[64];
        format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf), prefs.temp_unit);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", temp_buf);

        // Context label
        const char* temp_context = "Deep space (CMB)";
        if (cfg.temperature_kelvin > 100.0f) temp_context = "Room temperature";
        if (cfg.temperature_kelvin > 5000.0f) temp_context = "Surface of star";
        if (cfg.temperature_kelvin > 1e6f) temp_context = "Stellar core";
        if (cfg.temperature_kelvin > 1e9f) temp_context = "Neutron star";
        if (cfg.temperature_kelvin > 1e11f) temp_context = "Quark-gluon plasma";
        if (cfg.temperature_kelvin > 1e15f) temp_context = "Electroweak epoch";
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "  %s", temp_context);

        // Emergent temperature readout + feedback controls
        if (cfg.thermo_feedback_enabled) {
            char etemp_buf[64];
            format_temperature(emergent_temp_display, etemp_buf, sizeof(etemp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "  Measured: %s", etemp_buf);
        }
        ImGui::Checkbox("Thermodynamic Feedback", &cfg.thermo_feedback_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Temperature emerges from particle kinetic energies\n(statistical mechanics: T ~ <1/2 mv^2>)");
        if (cfg.thermo_feedback_enabled) {
            ImGui::SliderFloat("Coupling##thermo", &cfg.thermo_coupling, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = slider only\n1 = fully emergent\n0.5 = blended");
        }
    }

    // ── Thermodynamics ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Thermodynamics")) {
        // First Law: Energy conservation
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "1st Law: Energy Conservation");

        char ebuf_ke[32], ebuf_pe[32], ebuf_tot[32];
        fmt_energy_ev(ebuf_ke, sizeof(ebuf_ke), energy_kinetic_display * E_SCALE_MEV);
        fmt_energy_ev(ebuf_pe, sizeof(ebuf_pe), energy_potential_display * E_SCALE_MEV);
        float total_disp = (energy_kinetic_display + energy_potential_display) * E_SCALE_MEV;
        fmt_energy_ev(ebuf_tot, sizeof(ebuf_tot), total_disp);

        ImGui::Text("  KE: %s   PE: %s", ebuf_ke, ebuf_pe);
        ImGui::Text("  Total: %s", ebuf_tot);

        float ratio = energy_conservation_ratio_display;
        ImVec4 ratio_color;
        if (ratio > 0.95f && ratio < 1.05f)
            ratio_color = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
        else if (ratio > 0.80f && ratio < 1.20f)
            ratio_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        else
            ratio_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(ratio_color, "  Conservation: %.1f%%", ratio * 100.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("E_now / E_initial\n100%% = perfect conservation\n"
                "Sources: thermal noise, vacuum energy\n"
                "Sinks: damping, photon/neutrino drain, synchrotron");

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "  In: %.2f/f  Out: %.2f/f  Drift: %+.3f/f",
            energy_injected_rate_display, energy_dissipated_rate_display,
            energy_drift_rate_display);

        ImGui::Spacing();

        // Second Law: Entropy
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "2nd Law: Entropy");
        const char* trend_icon = (entropy_trend_display > 0) ? "^" :
                                  (entropy_trend_display < 0) ? "v" : "=";
        ImVec4 trend_color = (entropy_trend_display >= 0)
            ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(trend_color, "  S = %.1f  %s", system_entropy_display, trend_icon);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Boltzmann entropy from velocity distribution\n"
                "S = sum[ N * (1 + ln(A/N) + ln(T)) ]\n"
                "^ = increasing (normal)\n"
                "= = equilibrium\n"
                "v = decreasing (external work/cooling)");

        ImGui::Spacing();

        // Zeroth + Third Law notes
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "0th Law: Thermal conduction (30px range)");
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "3rd Law: Zero-point energy per particle type");
    }

    // ── Fundamental Forces ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fundamental Forces", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Electromagnetic
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "Electromagnetic");
        ImGui::SliderFloat("B Field", &cfg.lorentz_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Magnetic force (Biot-Savart + Lorentz)\n1.0 = F = q(v x B), B = q(v x r)/r²\n0 = off\n>1 = enhanced");
        if (cfg.magnetic_feedback_enabled)
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "  Measured: %.3f T", emergent_bfield_display);
        ImGui::Checkbox("Emergent B Field", &cfg.magnetic_feedback_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Magnetic field emerges from charged particle currents\n(moving charges generate B fields)");
        if (cfg.magnetic_feedback_enabled) {
            ImGui::SliderFloat("Coupling##mag", &cfg.magnetic_coupling, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = slider only\n1 = fully emergent\n0.5 = blended");
        }

        ImGui::Spacing();

        // Strong Nuclear
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Strong Nuclear");
        ImGui::SliderFloat("Confinement", &cfg.string_tension, 0.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quark confinement (string tension)\nHigher = quarks held more tightly\n0 = deconfinement (quark-gluon plasma)");
        ImGui::Checkbox("Hadronization", &cfg.hadronization_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color confinement enforcement\n"
                "Free quarks form mesons (q+qbar) or spawn vacuum pairs\n"
                "String breaking creates new pairs when quarks separate\n"
                "Disabled above QGP temperature (2 trillion K)");

        ImGui::Spacing();

        // Weak Nuclear
        ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.9f, 1.0f), "Weak Nuclear");
        ImGui::SliderFloat("Weak Force", &cfg.weak_coupling, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Weak force coupling strength\nMediates beta decay and flavor changes\nVery short range (~3 px)");

        ImGui::SliderFloat("Higgs VEV", &cfg.higgs_vev, 0.0f, 500.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higgs vacuum expectation value\nSets the mass scale for W/Z bosons\nStandard Model: 246");

        ImGui::Spacing();

        // Gravity
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.3f, 1.0f), "Gravity");
        ImGui::SliderFloat("Strength##grav", &cfg.gravity_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gravitational attraction (Newton's law)\n1.0 = F = G·m1·m2/r²\n0 = off\n>1 = enhanced gravity");
    }

    // ── Force Multipliers ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Force Multipliers")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Scale individual force strengths (1.0 = SM)");
        ImGui::SliderFloat("Coulomb (EM)", &cfg.coulomb_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Electromagnetic Coulomb force multiplier\n0 = no charge interaction\n1 = standard model\n>1 = enhanced EM");
        ImGui::SliderFloat("Yukawa (Nuclear)", &cfg.yukawa_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Strong nuclear binding (Yukawa potential)\n0 = no nuclear binding\n1 = standard model\n>1 = enhanced binding");
        ImGui::SliderFloat("Pauli Exclusion", &cfg.pauli_multiplier, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pauli exclusion repulsion strength\n0 = bosonic (particles overlap)\n1 = standard fermionic repulsion");
        ImGui::SliderFloat("QCD Color", &cfg.alpha_s_scale, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("QCD strong force (Cornell potential)\n0 = deconfined quarks\n1 = standard model\n>1 = enhanced confinement");
        ImGui::SliderFloat("Compton", &cfg.compton_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compton scattering (photon-matter)\n0 = photons don't push matter\n1 = standard model");
        ImGui::SliderFloat("Annihilation", &cfg.annihilation_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matter-antimatter annihilation rate\n0 = no annihilation\n1 = standard model\n>1 = faster annihilation");

        if (ImGui::Button("Reset to SM##forces")) {
            cfg.coulomb_strength = 1.0f;
            cfg.yukawa_strength = 1.0f;
            cfg.pauli_multiplier = 1.0f;
            cfg.alpha_s_scale = 1.0f;
            cfg.compton_strength = 1.0f;
            cfg.annihilation_strength = 1.0f;
        }
    }

    // ── World Settings ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("World Settings")) {
        ImGui::SliderFloat("Friction", &cfg.dampening, 0.50f, 0.99f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Velocity dampening per frame\n0.99 = near-vacuum (very low friction)\n0.90 = dense medium\nLower = more energy loss");

        ImGui::SliderFloat("Hard Core", &cfg.repulsion_radius, 1.0f, 40.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum distance before particles repel\nModels nucleon hard-core radius");

        ImGui::SliderFloat("Core Force", &cfg.pressure_resistance, 5.0f, 100.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How strongly particles repel at close range\nHigher = harder collisions");

        ImGui::SliderFloat("Max Range", &cfg.interaction_radius, 20.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum distance for force calculations\nHigher = longer-range EM interactions\nLower = faster simulation");

        ImGui::SliderFloat("Display Size", &cfg.radius, 0.5f, 8.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Visual size of particles on screen\nDoes not affect physics");
    }

    // ── Field Visualization ──────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Field Visualization")) {
        ImGui::Checkbox("EM##field", &field_em);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show electric field lines\nRed = positive, Blue = negative");
        ImGui::SameLine();
        ImGui::Checkbox("Strong##field", &field_strong);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show strong force field\nGreen glow around nucleons");

        ImGui::Checkbox("Weak##field", &field_weak);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show weak field around W/Z bosons");
        ImGui::SameLine();
        ImGui::Checkbox("Gravity##field", &field_gravity);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show gravitational potential wells");

        ImGui::Checkbox("Higgs##field", &field_higgs);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show Higgs field coupling to massive particles");
        ImGui::SameLine();
        ImGui::Checkbox("Collision##field", &show_collision_radii);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show collision detection radii\nGreen = hard-sphere, Yellow = Pauli core, Red = Yukawa range");

        if (field_em || field_strong || field_weak || field_gravity || field_higgs) {
            ImGui::SliderFloat("Brightness", &field_intensity, 0.05f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Field visualization brightness");
        }
    }

    // ── Virtual Particles / Casimir ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Virtual Particles")) {
        ImGui::Checkbox("Enable Virtual Pairs", &cfg.virtual_pairs_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spontaneous particle-antiparticle pairs\nfrom quantum vacuum fluctuations\n(Casimir effect source)");

        ImGui::Checkbox("Hide Virtual Trails", &hide_virtual_trails);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide virtual particle rendering\nwhile keeping the physics active");

        if (cfg.virtual_pairs_enabled) {
            ImGui::SliderFloat("Vacuum Energy", &cfg.vacuum_energy, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Vacuum fluctuation rate.\nHigher = more spontaneous pairs.\n0 = vacuum off. Default: 0.5");

            int max_pairs = static_cast<int>(cfg.virtual_pair_max_per_tick);
            ImGui::SliderInt("Max Pairs/Tick", &max_pairs, 1, 16);
            cfg.virtual_pair_max_per_tick = static_cast<uint32_t>(max_pairs);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum virtual pairs spawned per frame\nHigher = more quantum foam activity");

            ImGui::SliderFloat("Casimir Radius (px)", &cfg.casimir_radius, 5.0f, 60.0f, "%.0f px");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Detection range for Casimir force.\nDefault: 30 px");

            ImGui::SliderFloat("Casimir Strength", &cfg.casimir_strength, 0.0f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Casimir attractive force scaling.\n0 = no Casimir effect.\nDefault: 0.5");

            ImGui::SliderFloat("Pair Scatter (px)", &cfg.virtual_pair_scatter, 1.0f, 10.0f, "%.1f px");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Spawn separation of virtual pair.\nDefault: 3.0 px");
        }
    }

    // ── Entanglement ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Entanglement")) {
        ImGui::Checkbox("Enable Entanglement", &cfg.entanglement_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quantum entanglement between particle pairs\n"
                             "Created during virtual pair production\n"
                             "Correlated spins + velocity coupling");

        if (cfg.entanglement_enabled) {
            ImGui::SliderFloat("Coupling", &cfg.entanglement_coupling, 0.0f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Velocity coupling fraction between entangled pairs\n"
                                 "0 = no coupling, higher = stronger 'spooky action'");

            ImGui::SliderFloat("Decoherence", &cfg.entanglement_decoherence, 0.0f, 0.05f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Probability per tick of entanglement breaking\n"
                                 "0 = permanent, higher = faster decay");

            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Active pairs: %u", entangled_pair_count_display);
        }
    }

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Spawn Menu (Consolidated with collapsing headers) ───────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_spawn_menu(const SimConfig& /*cfg*/) {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 330, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, std::min(620.0f, max_h)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 200), ImVec2(360, max_h));

    if (!ImGui::Begin("Spawn Particles", &spawn_menu_visible)) {
        ImGui::End();
        return;
    }

    // ── Periodic Table ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Periodic Table", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImVec2 btn_size(27, 22);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 10; ++col) {
                if (col > 0) ImGui::SameLine();
                int idx = PT_LAYOUT[row][col];
                if (idx < 0) {
                    ImGui::Dummy(btn_size);
                    continue;
                }
                const auto& e = ELEMENTS[idx];
                bool selected = (spawn_atom_Z == e.Z);

                // Color by category
                ImVec4 color(0.15f, 0.30f, 0.20f, 0.80f);  // default
                if (e.Z == 2 || e.Z == 10 || e.Z == 18)
                    color = ImVec4(0.25f, 0.15f, 0.40f, 0.80f);  // noble gas
                else if (e.Z == 1 || e.Z == 6 || e.Z == 7 || e.Z == 8 ||
                         e.Z == 9 || e.Z == 15 || e.Z == 16 || e.Z == 17)
                    color = ImVec4(0.12f, 0.25f, 0.45f, 0.80f);  // nonmetal
                else if (e.Z == 3 || e.Z == 11 || e.Z == 19)
                    color = ImVec4(0.45f, 0.15f, 0.12f, 0.80f);  // alkali
                else if (e.Z == 4 || e.Z == 12 || e.Z == 20)
                    color = ImVec4(0.40f, 0.28f, 0.10f, 0.80f);  // alkaline earth
                else if (e.Z == 5 || e.Z == 14)
                    color = ImVec4(0.30f, 0.28f, 0.15f, 0.80f);  // metalloid
                else if (e.Z >= 21)
                    color = ImVec4(0.25f, 0.25f, 0.30f, 0.80f);  // transition metal

                if (selected)
                    color = ImVec4(0.15f, 0.45f, 0.60f, 0.90f);

                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(color.x * 1.5f, color.y * 1.5f, color.z * 1.5f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(0.302f, 0.749f, 0.953f, 0.80f));
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }

                char btn_id[32];
                snprintf(btn_id, sizeof(btn_id), "%s##pt%d", e.symbol, e.Z);
                if (ImGui::Button(btn_id, btn_size)) {
                    spawn_atom_Z = e.Z;
                    spawn_atom_N = e.N;
                    spawn_group = -1;
                    pending_spawn = true;
                }
                if (ImGui::IsItemHovered()) {
                    int total = e.Z * 2 + e.N;
                    ImGui::SetTooltip("%s (Z=%d)\n%dp + %dn + %de = %d particles",
                        e.name, e.Z, e.Z, e.N, e.Z, total);
                }

                if (selected) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
            }
        }

        // Composites
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Composites:");
        for (int g = 0; g < GROUP_TEMPLATE_COUNT_VAL; ++g) {
            const char* lbl = GROUP_TEMPLATES[g].label;
            if (strcmp(lbl, "H") == 0 || strcmp(lbl, "He") == 0 ||
                strcmp(lbl, "Li") == 0 || strcmp(lbl, "C") == 0 ||
                strcmp(lbl, "O") == 0) continue;

            bool sel = (spawn_group == g && spawn_atom_Z < 0);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char gid[32];
            snprintf(gid, sizeof(gid), "%s##gt%d", GROUP_TEMPLATES[g].label, g);
            if (ImGui::Button(gid, ImVec2(40, 24))) {
                spawn_group = g;
                spawn_atom_Z = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u particles)", GROUP_TEMPLATES[g].name, GROUP_TEMPLATES[g].count);

            if (sel) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    // ── Molecules ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Molecules")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Type a formula:");

        bool enter_pressed = ImGui::InputText("##mol_formula", molecule_formula_buf, 64,
            ImGuiInputTextFlags_EnterReturnsTrue);

        // Live search: find exact and prefix matches
        molecule_match_idx = -1;
        int match_count = 0;
        int match_indices[8] = {-1,-1,-1,-1,-1,-1,-1,-1};

        if (molecule_formula_buf[0] != '\0') {
            // Exact match first
            molecule_match_idx = find_molecule_template(molecule_formula_buf);

            // Prefix/substring matches for autocomplete
            size_t buf_len = strlen(molecule_formula_buf);
            for (int mi = 0; mi < MOLECULE_TEMPLATE_COUNT && match_count < 8; ++mi) {
                // Check formula prefix
                if (strncmp(MOLECULE_TEMPLATES[mi].formula, molecule_formula_buf, buf_len) == 0) {
                    match_indices[match_count++] = mi;
                    continue;
                }
                // Check name substring (case-insensitive)
                const char* name = MOLECULE_TEMPLATES[mi].name;
                const char* buf = molecule_formula_buf;
                bool name_match = false;
                for (size_t ni = 0; name[ni] && !name_match; ++ni) {
                    bool ok = true;
                    for (size_t bi = 0; bi < buf_len && ok; ++bi) {
                        char nc = name[ni + bi];
                        char bc = buf[bi];
                        if (nc >= 'A' && nc <= 'Z') nc += 32;
                        if (bc >= 'A' && bc <= 'Z') bc += 32;
                        if (nc != bc) ok = false;
                    }
                    if (ok) name_match = true;
                }
                if (name_match) {
                    match_indices[match_count++] = mi;
                }
            }
        }

        // Show match status
        ImGui::SameLine();
        if (molecule_match_idx >= 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%s",
                MOLECULE_TEMPLATES[molecule_match_idx].name);
        } else if (molecule_formula_buf[0] != '\0') {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "?");
        }

        // Spawn button (or Enter)
        if (molecule_match_idx >= 0) {
            if (enter_pressed || ImGui::Button("Spawn##mol_spawn")) {
                spawn_molecule_idx = molecule_match_idx;
                spawn_atom_Z = -1;
                spawn_group = -1;
                pending_spawn = true;
            }
        }

        // Autocomplete list
        if (match_count > 0 && molecule_formula_buf[0] != '\0') {
            for (int mi = 0; mi < match_count; ++mi) {
                int idx = match_indices[mi];
                char label[128];
                snprintf(label, sizeof(label), "%s  (%s)##mol_%d",
                    MOLECULE_TEMPLATES[idx].formula,
                    MOLECULE_TEMPLATES[idx].name, idx);
                if (ImGui::Selectable(label)) {
                    snprintf(molecule_formula_buf, 64, "%s", MOLECULE_TEMPLATES[idx].formula);
                    spawn_molecule_idx = idx;
                    spawn_atom_Z = -1;
                    spawn_group = -1;
                    pending_spawn = true;
                }
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Quick:");

        struct QuickMol { const char* formula; const char* label; };
        static const QuickMol quick[] = {
            {"H2O",   "H\xe2\x82\x82O"},
            {"CO2",   "CO\xe2\x82\x82"},
            {"NH3",   "NH\xe2\x82\x83"},
            {"CH4",   "CH\xe2\x82\x84"},
            {"NaCl",  "NaCl"},
            {"C2H5OH","EtOH"},
            {"C6H6",  "C\xe2\x82\x86H\xe2\x82\x86"},
            {"C6H12O6","Glucose"},
        };

        for (int qi = 0; qi < 8; ++qi) {
            int idx = find_molecule_template(quick[qi].formula);
            if (idx < 0) continue;
            if (qi > 0) ImGui::SameLine();

            bool sel = (spawn_molecule_idx == idx && pending_spawn);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char qid[32];
            snprintf(qid, sizeof(qid), "%s##qm%d", quick[qi].label, qi);
            if (ImGui::Button(qid, ImVec2(0, 24))) {
                snprintf(molecule_formula_buf, 64, "%s", quick[qi].formula);
                spawn_molecule_idx = idx;
                molecule_match_idx = idx;
                spawn_atom_Z = -1;
                spawn_group = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%s, %u atoms)", MOLECULE_TEMPLATES[idx].name,
                    MOLECULE_TEMPLATES[idx].formula, MOLECULE_TEMPLATES[idx].atom_count);

            if (sel) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
        }
    }

    // ── Leptons ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Leptons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 1:");
        if (spawn_button(ELECTRON_TYPE_PHYS, "e-", PHYS_TYPE_UI_COLORS[ELECTRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron"))
            { spawn_type = ELECTRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(POSITRON_TYPE_PHYS, "e+", PHYS_TYPE_UI_COLORS[POSITRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Positron"))
            { spawn_type = POSITRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRINO_TYPE_PHYS, "ve", PHYS_TYPE_UI_COLORS[NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron neutrino"))
            { spawn_type = NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 2:");
        if (spawn_button(MUON_TYPE_PHYS, "mu-", PHYS_TYPE_UI_COLORS[MUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon (decays ~100 frames)"))
            { spawn_type = MUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIMUON_TYPE_PHYS, "mu+", PHYS_TYPE_UI_COLORS[ANTIMUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-muon"))
            { spawn_type = ANTIMUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(MU_NEUTRINO_TYPE_PHYS, "vmu", PHYS_TYPE_UI_COLORS[MU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon neutrino"))
            { spawn_type = MU_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 3:");
        if (spawn_button(TAU_TYPE_PHYS, "tau-", PHYS_TYPE_UI_COLORS[TAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau (decays ~5 frames)"))
            { spawn_type = TAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTITAU_TYPE_PHYS, "tau+", PHYS_TYPE_UI_COLORS[ANTITAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-tau"))
            { spawn_type = ANTITAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TAU_NEUTRINO_TYPE_PHYS, "vtau", PHYS_TYPE_UI_COLORS[TAU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau neutrino"))
            { spawn_type = TAU_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Composites:");
        if (spawn_button(PROTON_TYPE, "p", PHYS_TYPE_UI_COLORS[PROTON_TYPE],
                          spawn_type, spawn_group, "Proton"))
            { spawn_type = PROTON_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRON_TYPE, "n", PHYS_TYPE_UI_COLORS[NEUTRON_TYPE],
                          spawn_type, spawn_group, "Neutron"))
            { spawn_type = NEUTRON_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIPROTON_TYPE_PHYS, "p-", PHYS_TYPE_UI_COLORS[ANTIPROTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Antiproton"))
            { spawn_type = ANTIPROTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Quarks ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Quarks")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Matter:");
        if (spawn_button(UP_QUARK_TYPE, "u", PHYS_TYPE_UI_COLORS[UP_QUARK_TYPE],
                          spawn_type, spawn_group, "Up quark (+2/3)"))
            { spawn_type = UP_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DOWN_QUARK_TYPE, "d", PHYS_TYPE_UI_COLORS[DOWN_QUARK_TYPE],
                          spawn_type, spawn_group, "Down quark (-1/3)"))
            { spawn_type = DOWN_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(STRANGE_QUARK_TYPE, "s", PHYS_TYPE_UI_COLORS[STRANGE_QUARK_TYPE],
                          spawn_type, spawn_group, "Strange quark (-1/3, decays)"))
            { spawn_type = STRANGE_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(CHARM_QUARK_TYPE, "c", PHYS_TYPE_UI_COLORS[CHARM_QUARK_TYPE],
                          spawn_type, spawn_group, "Charm quark (+2/3, decays fast)"))
            { spawn_type = CHARM_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TOP_QUARK_TYPE, "t", PHYS_TYPE_UI_COLORS[TOP_QUARK_TYPE],
                          spawn_type, spawn_group, "Top quark (+2/3, instant decay)"))
            { spawn_type = TOP_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(BOTTOM_QUARK_TYPE, "b", PHYS_TYPE_UI_COLORS[BOTTOM_QUARK_TYPE],
                          spawn_type, spawn_group, "Bottom quark (-1/3, decays)"))
            { spawn_type = BOTTOM_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Antimatter:");
        if (spawn_button(ANTI_UP_TYPE, "u~", PHYS_TYPE_UI_COLORS[ANTI_UP_TYPE],
                          spawn_type, spawn_group, "Anti-up (-2/3)"))
            { spawn_type = ANTI_UP_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_DOWN_TYPE, "d~", PHYS_TYPE_UI_COLORS[ANTI_DOWN_TYPE],
                          spawn_type, spawn_group, "Anti-down (+1/3)"))
            { spawn_type = ANTI_DOWN_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_STRANGE_TYPE, "s~", PHYS_TYPE_UI_COLORS[ANTI_STRANGE_TYPE],
                          spawn_type, spawn_group, "Anti-strange (+1/3)"))
            { spawn_type = ANTI_STRANGE_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(ANTI_CHARM_TYPE, "c~", PHYS_TYPE_UI_COLORS[ANTI_CHARM_TYPE],
                          spawn_type, spawn_group, "Anti-charm (-2/3)"))
            { spawn_type = ANTI_CHARM_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_TOP_TYPE, "t~", PHYS_TYPE_UI_COLORS[ANTI_TOP_TYPE],
                          spawn_type, spawn_group, "Anti-top (-2/3)"))
            { spawn_type = ANTI_TOP_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_BOTTOM_TYPE, "b~", PHYS_TYPE_UI_COLORS[ANTI_BOTTOM_TYPE],
                          spawn_type, spawn_group, "Anti-bottom (+1/3)"))
            { spawn_type = ANTI_BOTTOM_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Bosons ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Bosons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Gauge:");
        if (spawn_button(PHOTON_TYPE_PHYS, "y", PHYS_TYPE_UI_COLORS[PHOTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Photon (massless, stable)"))
            { spawn_type = PHOTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(GLUON_TYPE_PHYS, "g", PHYS_TYPE_UI_COLORS[GLUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Gluon (strong force mediator)"))
            { spawn_type = GLUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Weak / Scalar:");
        if (spawn_button(W_PLUS_TYPE_PHYS, "W+", PHYS_TYPE_UI_COLORS[W_PLUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W+ boson (instant decay)"))
            { spawn_type = W_PLUS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(W_MINUS_TYPE_PHYS, "W-", PHYS_TYPE_UI_COLORS[W_MINUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W- boson (instant decay)"))
            { spawn_type = W_MINUS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(Z_BOSON_TYPE_PHYS, "Z0", PHYS_TYPE_UI_COLORS[Z_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "Z0 boson (instant decay)"))
            { spawn_type = Z_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(HIGGS_TYPE_PHYS, "H0", PHYS_TYPE_UI_COLORS[HIGGS_TYPE_PHYS],
                          spawn_type, spawn_group, "Higgs boson (instant decay)"))
            { spawn_type = HIGGS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Hypothetical ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hypothetical")) {
        if (spawn_button(GRAVITON_TYPE_PHYS, "G", PHYS_TYPE_UI_COLORS[GRAVITON_TYPE_PHYS],
                          spawn_type, spawn_group, "Graviton (spin-2, massless)"))
            { spawn_type = GRAVITON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_MATTER_TYPE_PHYS, "DM", PHYS_TYPE_UI_COLORS[DARK_MATTER_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Matter (WIMP, gravity-only)"))
            { spawn_type = DARK_MATTER_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_ENERGY_TYPE_PHYS, "DE", PHYS_TYPE_UI_COLORS[DARK_ENERGY_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Energy (repulsive field)"))
            { spawn_type = DARK_ENERGY_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Dark Matter Candidates ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Dark Matter Candidates:");
        if (spawn_button(AXINO_TYPE_PHYS, "Ax", PHYS_TYPE_UI_COLORS[AXINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Axino (SUSY partner of axion)"))
            { spawn_type = AXINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(WIMPZILLA_TYPE_PHYS, "WZ", PHYS_TYPE_UI_COLORS[WIMPZILLA_TYPE_PHYS],
                          spawn_type, spawn_group, "WIMPzilla (super-heavy ~10^12 GeV)"))
            { spawn_type = WIMPZILLA_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SIMP_TYPE_PHYS, "SI", PHYS_TYPE_UI_COLORS[SIMP_TYPE_PHYS],
                          spawn_type, spawn_group, "SIMP (strongly self-interacting)"))
            { spawn_type = SIMP_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(STERILE_NEUTRINO_TYPE_PHYS, "Ns", PHYS_TYPE_UI_COLORS[STERILE_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Sterile Neutrino (no weak force)"))
            { spawn_type = STERILE_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_PHOTON_TYPE_PHYS, "A'", PHYS_TYPE_UI_COLORS[DARK_PHOTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Photon (dark EM carrier)"))
            { spawn_type = DARK_PHOTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(QBALL_TYPE_PHYS, "QB", PHYS_TYPE_UI_COLORS[QBALL_TYPE_PHYS],
                          spawn_type, spawn_group, "Q-Ball (stable field soliton, charged)"))
            { spawn_type = QBALL_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Supersymmetric Sparticles ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Supersymmetric (Sparticles):");
        if (spawn_button(SELECTRON_TYPE_PHYS, "e~", PHYS_TYPE_UI_COLORS[SELECTRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Selectron (scalar e-, 200 GeV)"))
            { spawn_type = SELECTRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SMUON_TYPE_PHYS, "mu~", PHYS_TYPE_UI_COLORS[SMUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Smuon (scalar muon, 300 GeV)"))
            { spawn_type = SMUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(STAU_TYPE_PHYS, "ta~", PHYS_TYPE_UI_COLORS[STAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Stau (scalar tau, 150 GeV, long-lived)"))
            { spawn_type = STAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SQUARK_TYPE_PHYS, "q~", PHYS_TYPE_UI_COLORS[SQUARK_TYPE_PHYS],
                          spawn_type, spawn_group, "Squark (scalar quark, 1.5 TeV, color-charged)"))
            { spawn_type = SQUARK_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(GLUINO_TYPE_PHYS, "g~", PHYS_TYPE_UI_COLORS[GLUINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Gluino (fermionic gluon, 2 TeV)"))
            { spawn_type = GLUINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(PHOTINO_TYPE_PHYS, "y~", PHYS_TYPE_UI_COLORS[PHOTINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Photino (fermionic photon, 100 GeV)"))
            { spawn_type = PHOTINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(WINO_TYPE_PHYS, "W~", PHYS_TYPE_UI_COLORS[WINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Wino (fermionic W, 300 GeV)"))
            { spawn_type = WINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ZINO_TYPE_PHYS, "Z~", PHYS_TYPE_UI_COLORS[ZINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Zino (fermionic Z, 300 GeV)"))
            { spawn_type = ZINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(HIGGSINO_TYPE_PHYS, "H~", PHYS_TYPE_UI_COLORS[HIGGSINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Higgsino (fermionic Higgs, 200 GeV)"))
            { spawn_type = HIGGSINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRALINO_TYPE_PHYS, "N1", PHYS_TYPE_UI_COLORS[NEUTRALINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Neutralino (stable LSP, DM candidate)"))
            { spawn_type = NEUTRALINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SNEUTRINO_TYPE_PHYS, "v~", PHYS_TYPE_UI_COLORS[SNEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Sneutrino (scalar neutrino, 200 GeV)"))
            { spawn_type = SNEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Force Carriers & Exotic ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Force Carriers & Exotic:");
        if (spawn_button(GRAVITINO_TYPE_PHYS, "G~", PHYS_TYPE_UI_COLORS[GRAVITINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Gravitino (SUSY graviton, spin-3/2)"))
            { spawn_type = GRAVITINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(X_BOSON_TYPE_PHYS, "X", PHYS_TYPE_UI_COLORS[X_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "X Boson (GUT, proton decay, 10^15 GeV)"))
            { spawn_type = X_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(Y_BOSON_TYPE_PHYS, "Y", PHYS_TYPE_UI_COLORS[Y_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "Y Boson (GUT, proton decay, 10^15 GeV)"))
            { spawn_type = Y_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(MONOPOLE_TYPE_PHYS, "MM", PHYS_TYPE_UI_COLORS[MONOPOLE_TYPE_PHYS],
                          spawn_type, spawn_group, "Magnetic Monopole (radial B-field, 10^16 GeV)"))
            { spawn_type = MONOPOLE_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(RADION_TYPE_PHYS, "Ra", PHYS_TYPE_UI_COLORS[RADION_TYPE_PHYS],
                          spawn_type, spawn_group, "Radion (extra-dimension size, 1 TeV)"))
            { spawn_type = RADION_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DILATON_TYPE_PHYS, "Di", PHYS_TYPE_UI_COLORS[DILATON_TYPE_PHYS],
                          spawn_type, spawn_group, "Dilaton (string theory scale, 10 GeV)"))
            { spawn_type = DILATON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Theoretical Extremes ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Theoretical Extremes:");
        if (spawn_button(TACHYON_TYPE_PHYS, "Ta", PHYS_TYPE_UI_COLORS[TACHYON_TYPE_PHYS],
                          spawn_type, spawn_group, "Tachyon (superluminal, imaginary mass)"))
            { spawn_type = TACHYON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(PREON_TYPE_PHYS, "Pr", PHYS_TYPE_UI_COLORS[PREON_TYPE_PHYS],
                          spawn_type, spawn_group, "Preon (sub-quark constituent, confined)"))
            { spawn_type = PREON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(INFLATON_TYPE_PHYS, "In", PHYS_TYPE_UI_COLORS[INFLATON_TYPE_PHYS],
                          spawn_type, spawn_group, "Inflaton (cosmic inflation driver)"))
            { spawn_type = INFLATON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(MAJORON_TYPE_PHYS, "Mj", PHYS_TYPE_UI_COLORS[MAJORON_TYPE_PHYS],
                          spawn_type, spawn_group, "Majoron (neutrino mass, nearly invisible)"))
            { spawn_type = MAJORON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(ODDERON_TYPE_PHYS, "Od", PHYS_TYPE_UI_COLORS[ODDERON_TYPE_PHYS],
                          spawn_type, spawn_group, "Odderon (3-gluon bound state)"))
            { spawn_type = ODDERON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(GLUEBALL_TYPE_PHYS, "Gb", PHYS_TYPE_UI_COLORS[GLUEBALL_TYPE_PHYS],
                          spawn_type, spawn_group, "Glueball (pure-glue bound state)"))
            { spawn_type = GLUEBALL_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SKYRMION_TYPE_PHYS, "Sk", PHYS_TYPE_UI_COLORS[SKYRMION_TYPE_PHYS],
                          spawn_type, spawn_group, "Skyrmion (topological soliton, baryon-like)"))
            { spawn_type = SKYRMION_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(X17_TYPE_PHYS, "X17", PHYS_TYPE_UI_COLORS[X17_TYPE_PHYS],
                          spawn_type, spawn_group, "X17 (fifth-force boson, 17 MeV)"))
            { spawn_type = X17_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(CHAMELEON_TYPE_PHYS, "Ch", PHYS_TYPE_UI_COLORS[CHAMELEON_TYPE_PHYS],
                          spawn_type, spawn_group, "Chameleon (environment-dependent mass)"))
            { spawn_type = CHAMELEON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── New Class (2025-2026) ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "New Class (2025-2026):");
        if (spawn_button(PARAPARTICLE_TYPE_PHYS, "Pp", PHYS_TYPE_UI_COLORS[PARAPARTICLE_TYPE_PHYS],
                          spawn_type, spawn_group, "Paraparticle (exotic statistics)"))
            { spawn_type = PARAPARTICLE_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DYN_AXION_QP_TYPE_PHYS, "Dq", PHYS_TYPE_UI_COLORS[DYN_AXION_QP_TYPE_PHYS],
                          spawn_type, spawn_group, "Dyn. Axion Quasiparticle (topological EM)"))
            { spawn_type = DYN_AXION_QP_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Hadrons ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hadrons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Baryons (3 quarks):");
        for (int h = 0; h < HADRON_TEMPLATE_COUNT_VAL; ++h) {
            if (h == 7) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mesons (quark + antiquark):");
            }

            int group_id = GROUP_TEMPLATE_COUNT_VAL + h;
            bool selected = (spawn_group == group_id);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            if (ImGui::Button(HADRON_TEMPLATES[h].label, ImVec2(50, 30))) {
                spawn_group = group_id;
                spawn_atom_Z = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u quarks)", HADRON_TEMPLATES[h].name, HADRON_TEMPLATES[h].count);

            if (selected) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }

            int row_idx = (h < 7) ? h : (h - 7);
            int row_count = (h < 7) ? 7 : (HADRON_TEMPLATE_COUNT_VAL - 7);
            if ((row_idx + 1) % 4 != 0 && row_idx < row_count - 1) ImGui::SameLine();
        }
    }

    // ── Force Objects ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Force Objects")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Click to place in world:");

        static const char* fo_labels[] = { "EM", "Strong", "Weak", "Gravity", "Heat" };
        static const char* fo_tips[] = {
            "Electromagnetic field\nPushes/pulls charged particles",
            "Strong nuclear force\nYukawa-like on baryons",
            "Weak nuclear force\nShort-range on all particles",
            "Gravity well\nAttracts all massive particles",
            "Heat source\nAdds thermal energy to nearby particles"
        };
        static const ImVec4 fo_colors[] = {
            ImVec4(0.3f, 0.5f, 1.0f, 1.0f),   // EM — blue
            ImVec4(0.3f, 0.9f, 0.4f, 1.0f),   // Strong — green
            ImVec4(0.7f, 0.3f, 0.9f, 1.0f),   // Weak — purple
            ImVec4(0.9f, 0.7f, 0.2f, 1.0f),   // Gravity — amber
            ImVec4(1.0f, 0.4f, 0.2f, 1.0f),   // Heat — orange-red
        };

        for (int fi = 0; fi < 5; ++fi) {
            if (fi > 0) ImGui::SameLine();

            bool active = (force_obj_placement_mode && force_obj_placement_type == fi);
            ImVec4 c = fo_colors[fi];

            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(c.x * 0.3f, c.y * 0.3f, c.z * 0.3f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(c.x * 0.5f, c.y * 0.5f, c.z * 0.5f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(c.x * 0.7f, c.y * 0.7f, c.z * 0.7f, 1.0f));

            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char fid[32];
            snprintf(fid, sizeof(fid), "%s##fo%d", fo_labels[fi], fi);
            if (ImGui::Button(fid, ImVec2(52, 30))) {
                force_obj_placement_mode = !active;
                force_obj_placement_type = fi;
                pending_spawn = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", fo_tips[fi]);

            if (active) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
        }

        // Mirror button (separate — uses two-click placement)
        {
            ImVec4 mc(0.7f, 0.7f, 0.8f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(mc.x * 0.3f, mc.y * 0.3f, mc.z * 0.3f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(mc.x * 0.5f, mc.y * 0.5f, mc.z * 0.5f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(mc.x * 0.7f, mc.y * 0.7f, mc.z * 0.7f, 1.0f));
            if (mirror_placement_mode) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(mc.x, mc.y, mc.z, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }
            if (ImGui::Button("Mirror##fo5", ImVec2(70, 30))) {
                mirror_placement_mode = !mirror_placement_mode;
                if (mirror_placement_mode) {
                    mirror_placement_phase = 0;
                    force_obj_placement_mode = false;
                    pending_spawn = false;
                    select_mode = false;
                    accel_mode = false;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reflective mirror\nTwo clicks define a line segment");
            if (mirror_placement_mode) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
        }

        if (force_obj_placement_mode) {
            ImGui::TextColored(fo_colors[force_obj_placement_type],
                "Click in world to place %s object", fo_labels[force_obj_placement_type]);
        }
    }

    // ── Spawn Settings (shared, outside headers) ─────────────────────────────
    ImGui::Separator();
    ImGui::SliderInt("Count", &spawn_count, 1, 100);
    {
        // Energy slider: 0 eV to 9999 TeV (stored in MeV)
        // 9999 TeV = 9.999e9 MeV
        static constexpr float MAX_ENERGY_MEV = 9.999e9f;
        char e_label[32];
        fmt_energy_ev(e_label, sizeof(e_label), spawn_energy_mev);
        ImGui::SliderFloat("Energy", &spawn_energy_mev, 0.0f, MAX_ENERGY_MEV, e_label,
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    }
    ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 100.0f, "%.0f");

    // Status text
    if (pending_spawn) {
        ImGui::Spacing();
        if (spawn_molecule_idx >= 0 && spawn_molecule_idx < MOLECULE_TEMPLATE_COUNT) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s (%s)", MOLECULE_TEMPLATES[spawn_molecule_idx].name,
                MOLECULE_TEMPLATES[spawn_molecule_idx].formula);
        } else if (spawn_atom_Z > 0) {
            const char* elem_name = "?";
            for (int ei = 0; ei < ELEMENT_COUNT; ++ei) {
                if (ELEMENTS[ei].Z == spawn_atom_Z) { elem_name = ELEMENTS[ei].name; break; }
            }
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s", elem_name);
        } else if (spawn_group >= GROUP_TEMPLATE_COUNT_VAL) {
            int h_idx = spawn_group - GROUP_TEMPLATE_COUNT_VAL;
            if (h_idx < HADRON_TEMPLATE_COUNT_VAL) {
                ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                    "Click to place %s", HADRON_TEMPLATES[h_idx].name);
            }
        } else if (spawn_group >= 0 && spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s", GROUP_TEMPLATES[spawn_group].name);
        } else if (spawn_group == -1) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click in world to place");
        }
    }

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Info Card (Right Side, fixed position) ──────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_info_card(const Particles& particles) {
    // Show pinned selection, or hover preview
    bool pinned = (selected_particle_idx >= 0);
    int32_t show_idx = pinned ? selected_particle_idx : hover_particle_idx;
    if (show_idx < 0) return;
    uint32_t idx = static_cast<uint32_t>(show_idx);
    if (idx >= particles.types.size()) {
        if (pinned) selected_particle_idx = -1;
        return;
    }

    uint32_t ptype = particles.types[idx];
    const char* name = (ptype < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_NAMES[ptype] : "Unknown";

    float charge = 0.0f, spin = 0.0f, color_charge = 0.0f, decay_rate = 0.0f;
    if (idx * GENOME_SIZE + 3 < particles.genomes.size()) {
        charge       = particles.genomes[idx * GENOME_SIZE + 0];
        spin         = particles.genomes[idx * GENOME_SIZE + 1];
        color_charge = particles.genomes[idx * GENOME_SIZE + 2];
        decay_rate   = particles.genomes[idx * GENOME_SIZE + 3];
    }

    ImGuiIO& io = ImGui::GetIO();
    // Bottom-right, notification style — pivot (0,1) anchors bottom edge, grows upward
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, io.DisplaySize.y - 60),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));

    ImGuiWindowFlags card_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowSize(ImVec2(250, 0));

    if (ImGui::Begin("##InfoCard", nullptr, card_flags)) {
        // Particle name with color
        ImVec4 pcolor = (ptype < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_UI_COLORS[ptype] : ImVec4(1,1,1,1);
        ImGui::TextColored(pcolor, "%s", name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "#%u", idx);
        if (pinned) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 0.6f), "(selected)");
        }

        ImGui::Separator();

        // Two-column layout
        float col_w = 80.0f;

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Charge");
        ImGui::SameLine(col_w);
        ImGui::Text("%+.2f", charge);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Spin");
        ImGui::SameLine(col_w);
        ImGui::Text("%+.1f", spin);

        // Color charge for quarks
        if (ptype >= UP_QUARK_TYPE && ptype <= ANTI_BOTTOM_TYPE) {
            int cc = static_cast<int>(color_charge);
            const char* color_name = "?";
            if (cc == 1 || cc == -1)  color_name = (cc > 0) ? "Red" : "Anti-Red";
            if (cc == 2 || cc == -2)  color_name = (cc > 0) ? "Green" : "Anti-Green";
            if (cc == 3 || cc == -3)  color_name = (cc > 0) ? "Blue" : "Anti-Blue";
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Color");
            ImGui::SameLine(col_w);
            ImGui::Text("%s", color_name);
        }

        // Mass tier
        if (ptype < PHYS_PARTICLE_TYPES) {
            const char* mass_name = "light";
            uint32_t bhv = (ptype < MAX_PARTICLE_TYPES) ? particles.behavior_flags[ptype] : 0;
            if (bhv & BEHAVIOR_PHOTON)      mass_name = "massless";
            if (bhv & BEHAVIOR_NEUTRINO)    mass_name = "~massless";
            if (bhv & BEHAVIOR_MASS_MEDIUM) mass_name = "medium";
            if (bhv & BEHAVIOR_MASS_HEAVY)  mass_name = "heavy";
            if (bhv & BEHAVIOR_MASS_DENSE)  mass_name = "dense";
            if (bhv & BEHAVIOR_MASS_ULTRA)  mass_name = "ultra-heavy";
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mass");
            ImGui::SameLine(col_w);
            ImGui::Text("%s", mass_name);
        }

        if (decay_rate > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Decay");
            ImGui::SameLine(col_w);
            ImGui::Text("%.3f", decay_rate);
        }

        // ── Age ──────────────────────────────────────────────────────────
        if (idx < particles.birth_frames.size()) {
            uint32_t age_frames = frame_counter_display - particles.birth_frames[idx];
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f)
                ImGui::Text("%.1f s", age_sec);
            else
                ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // ── Speed, Momentum, Energy, Temperature, Magnetic Moment ──
        if (readback_velocities && idx < readback_count) {
            glm::vec2 vel = readback_velocities[idx];
            float speed = glm::length(vel);
            float beta = std::min(speed / C_SIM, 0.9999f);

            // Speed — real-world units
            {
                char spd_buf[32];
                fmt_speed(spd_buf, sizeof(spd_buf), speed);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", spd_buf);
            }

            // Momentum — relativistic p = γm₀βc (MeV/c)
            {
                char mom_buf[32];
                fmt_momentum(mom_buf, sizeof(mom_buf), speed, ptype);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", mom_buf);
            }

            // Kinetic energy — KE = (γ-1)m₀c² for massive, E = pc for massless
            {
                float m0 = rest_mass_MeV(ptype);
                float KE_MeV;
                if (m0 < 0.001f) {
                    KE_MeV = beta * 1.0f;  // massless: all energy is kinetic
                    if (KE_MeV < 0.001f) KE_MeV = 0.0f;
                } else {
                    float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                    KE_MeV = (gamma - 1.0f) * m0;
                }

                char e_buf[32];
                fmt_energy_ev(e_buf, sizeof(e_buf), KE_MeV);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", e_buf);
            }

            // Temperature — from kinetic energy
            {
                // Sim mass for KE-based temp (keep existing sim-unit mass for thermal calc)
                auto get_sim_mass = [](uint32_t t) -> float {
                    if (t <= 1 || t == 5) return 40.0f;
                    if (t == 2 || t == 4) return 1.0f;
                    if (t == 7 || t == 8) return 200.0f;
                    if (t == 9 || t == 10) return 3333.0f;
                    if (t == 6 || t == 11 || t == 12) return 0.01f;
                    if (t == 3 || t == 25) return 0.01f;
                    return 1.0f;
                };
                float sim_mass = get_sim_mass(ptype);
                float ke = 0.5f * sim_mass * speed * speed;
                float particle_temp = ke * 0.1f;
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Temp");
                ImGui::SameLine(col_w);
                if (particle_temp < 1000.0f)
                    ImGui::Text("%.1f K", particle_temp);
                else
                    ImGui::Text("%.1fk K", particle_temp / 1000.0f);
            }

            // ── Magnetic moment (intrinsic) ──────────────────────────────
            // Nucleons: anomalous moments from quark substructure
            // Leptons: Dirac g≈2 (accurate to 0.1%)
            {
                float mu_val = 0.0f;
                const char* mu_unit = "";
                if (ptype == PROTON_TYPE) {
                    mu_val = 2.793f * spin;
                    mu_unit = "\xCE\xBC\x4E";  // μN
                } else if (ptype == ANTIPROTON_TYPE_PHYS) {
                    mu_val = -2.793f * spin;
                    mu_unit = "\xCE\xBC\x4E";
                } else if (ptype == NEUTRON_TYPE) {
                    mu_val = -1.913f * spin;
                    mu_unit = "\xCE\xBC\x4E";
                } else if (std::abs(charge) > 0.01f && std::abs(spin) > 0.01f) {
                    // Dirac g=2: μ = q·S (in Bohr magnetons for leptons, μN for quarks)
                    bool is_lepton = (ptype == ELECTRON_TYPE_PHYS || ptype == POSITRON_TYPE_PHYS ||
                                      ptype == MUON_TYPE_PHYS || ptype == ANTIMUON_TYPE_PHYS ||
                                      ptype == TAU_TYPE_PHYS || ptype == ANTITAU_TYPE_PHYS);
                    if (is_lepton) {
                        mu_val = charge * spin;  // in Bohr magnetons
                        mu_unit = "\xCE\xBC\x42";  // μB
                    } else {
                        mu_val = charge * spin;  // in natural units
                        mu_unit = "\xCE\xBC";
                    }
                }
                if (std::abs(mu_val) > 0.001f) {
                    ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
                    ImGui::SameLine(col_w);
                    ImGui::Text("%+.3f %s", mu_val, mu_unit);
                }
            }
        }

        // ── Orbital relationship ─────────────────────────────────────────
        if (idx < particles.orbital_parent.size()) {
            int32_t parent = particles.orbital_parent[idx];
            if (parent >= 0 && parent != static_cast<int32_t>(idx) &&
                static_cast<uint32_t>(parent) < particles.types.size()) {
                uint32_t parent_type = particles.types[parent];
                const char* parent_name = (parent_type < PHYS_PARTICLE_TYPES)
                    ? PHYS_TYPE_NAMES[parent_type] : "Unknown";
                ImVec4 parent_color = (parent_type < PHYS_PARTICLE_TYPES)
                    ? PHYS_TYPE_UI_COLORS[parent_type] : ImVec4(1,1,1,1);

                // Determine relationship label
                bool is_nucleon = (ptype <= 1 || ptype == 5);
                const char* relation = is_nucleon ? "Bound to" : "Orbiting";

                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", relation);
                ImGui::SameLine(col_w);
                ImGui::PushStyleColor(ImGuiCol_Text, parent_color);
                char btn_label[64];
                snprintf(btn_label, sizeof(btn_label), "%s #%d", parent_name, parent);
                if (ImGui::SmallButton(btn_label)) {
                    navigate_to_particle = parent;
                }
                ImGui::PopStyleColor();
            }
        }

        // ── Bond partners ────────────────────────────────────────────────
        if (particles.bond_partners_ptr && idx < particles.bond_partners_count / MAX_BONDS_PER_PARTICLE) {
            bool has_bond = false;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                uint32_t partner = particles.bond_partners_ptr[idx * MAX_BONDS_PER_PARTICLE + s];
                if (partner != 0xFFFFFFFF && partner < particles.types.size()) {
                    if (!has_bond) {
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Bonds");
                        ImGui::SameLine(col_w);
                        has_bond = true;
                    } else {
                        ImGui::SameLine();
                    }
                    uint32_t bp_type = particles.types[partner];
                    ImVec4 bp_color = (bp_type < PHYS_PARTICLE_TYPES)
                        ? PHYS_TYPE_UI_COLORS[bp_type] : ImVec4(1,1,1,1);
                    const char* bp_label = (bp_type < PHYS_PARTICLE_TYPES)
                        ? PHYS_TYPE_LABELS[bp_type] : "?";
                    char bp_btn[32];
                    snprintf(bp_btn, sizeof(bp_btn), "%s##bp%u", bp_label, s);
                    ImGui::PushStyleColor(ImGuiCol_Text, bp_color);
                    if (ImGui::SmallButton(bp_btn)) {
                        navigate_to_particle = static_cast<int32_t>(partner);
                    }
                    ImGui::PopStyleColor();
                }
            }
        }

        // ── Element membership ─────────────────────────────────────────
        if (idx < particles.orbital_parent.size()) {
            // Find nucleus representative for this particle
            int32_t nuc_rep = -1;
            bool is_nucleon = (ptype == PROTON_TYPE || ptype == NEUTRON_TYPE || ptype == ANTIPROTON_TYPE_PHYS);
            bool is_lepton  = (ptype == ELECTRON_TYPE_PHYS || ptype == POSITRON_TYPE_PHYS);

            if (is_nucleon || is_lepton) {
                int32_t op = particles.orbital_parent[idx];
                if (op >= 0 && static_cast<uint32_t>(op) < particles.types.size()) {
                    nuc_rep = op;
                }
                // If this IS a nucleus rep (proton or antiproton with no parent)
                if (is_nucleon && nuc_rep < 0 && (ptype == PROTON_TYPE || ptype == ANTIPROTON_TYPE_PHYS)) {
                    nuc_rep = static_cast<int32_t>(idx);
                }
            }

            if (nuc_rep >= 0) {
                // Determine if this is an antimatter nucleus
                uint32_t rep_type = particles.types[static_cast<uint32_t>(nuc_rep)];
                bool is_anti = (rep_type == ANTIPROTON_TYPE_PHYS);

                // Count constituents for this nucleus
                int Z = 0, N_count = 0, lepton_count = 0;
                uint32_t n_total = static_cast<uint32_t>(particles.types.size());
                for (uint32_t pi = 0; pi < n_total; ++pi) {
                    if (particles.orbital_parent.size() <= pi) break;
                    int32_t their_parent = particles.orbital_parent[pi];
                    if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;
                    uint32_t pt = particles.types[pi];
                    if (is_anti) {
                        if (pt == ANTIPROTON_TYPE_PHYS) Z++;
                        else if (pt == NEUTRON_TYPE) N_count++;
                        else if (pt == POSITRON_TYPE_PHYS) lepton_count++;
                    } else {
                        if (pt == PROTON_TYPE) Z++;
                        else if (pt == NEUTRON_TYPE) N_count++;
                        else if (pt == ELECTRON_TYPE_PHYS) lepton_count++;
                    }
                }

                if (Z > 0 && Z <= FULL_ELEMENT_COUNT) {
                    int A = Z + N_count;
                    int net_charge = Z - lepton_count;

                    ImGui::Spacing();
                    ImGui::Separator();

                    // Element header with symbol
                    ImVec4 elem_header_color = is_anti
                        ? ImVec4(0.0f, 0.85f, 0.95f, 1.0f)   // cyan for antimatter
                        : ImVec4(0.302f, 0.749f, 0.953f, 1.0f);
                    ImGui::TextColored(elem_header_color, is_anti ? "Anti-Element" : "Element");
                    ImGui::SameLine(col_w);

                    // Clickable element button
                    char elem_label[96];
                    const char* prefix = is_anti ? "Anti-" : "";
                    if (net_charge == 0)
                        snprintf(elem_label, sizeof(elem_label), "%s%s-%d (%s%s)",
                                 prefix, ELEMENT_SYMBOLS[Z], A, prefix, ELEMENT_NAMES[Z]);
                    else
                        snprintf(elem_label, sizeof(elem_label), "%s%s-%d %s%d",
                                 prefix, ELEMENT_SYMBOLS[Z], A,
                                 net_charge > 0 ? "+" : "", net_charge);

                    ImVec4 elem_btn_color = is_anti
                        ? ImVec4(0.6f, 0.9f, 0.95f, 1.0f)   // cyan-tinted for antimatter
                        : ImVec4(0.9f, 0.85f, 0.6f, 1.0f);  // gold for matter
                    ImGui::PushStyleColor(ImGuiCol_Text, elem_btn_color);
                    if (ImGui::SmallButton(elem_label)) {
                        element_card_nucleus_rep = nuc_rep;
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click for %selement details", prefix);

                    // ── Molecule membership ───────────────────────────────
                    // Check if this atom's nucleus belongs to a molecule
                    for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
                        auto& mol = molecule_list[mi];
                        if (mol.atom_indices.size() < 2) continue;  // single atoms aren't molecules
                        bool found = false;
                        for (uint32_t ei : mol.atom_indices) {
                            if (ei < element_list.size() &&
                                static_cast<int32_t>(element_list[ei].rep) == nuc_rep) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) continue;

                        std::string mol_formula = build_molecular_formula(element_list, mol.atom_indices);
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Molecule");
                        ImGui::SameLine(col_w);

                        char mol_label[128];
                        snprintf(mol_label, sizeof(mol_label), "%s (%d atoms)",
                                 mol_formula.c_str(), static_cast<int>(mol.atom_indices.size()));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.65f, 1.0f, 1.0f));
                        if (ImGui::SmallButton(mol_label)) {
                            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
                            molecule_card_atom_rep = static_cast<int32_t>(first_rep);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Click for molecule details");
                        break;  // only one molecule per atom
                    }
                }
            }
        }

        // Action buttons (only when pinned/selected)
        if (pinned) {
            ImGui::Spacing();
            ImGui::Separator();

            if (particle_move_mode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
                if (ImGui::Button("Moving...", ImVec2(72, 26))) {
                    particle_move_mode = false;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
            } else {
                if (ImGui::Button("Move", ImVec2(72, 26))) {
                    particle_move_mode = true;
                }
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
                if (ImGui::Button("Delete", ImVec2(72, 26))) {
                    request_delete_particle = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(72, 26))) {
                    selected_particle_idx = -1;
                    particle_move_mode = false;
                }
            }
        }
    }
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Element Detail Card ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_element_card(const Particles& particles) {
    int32_t nuc_rep = element_card_nucleus_rep;
    if (nuc_rep < 0 || static_cast<uint32_t>(nuc_rep) >= particles.types.size()) {
        element_card_nucleus_rep = -1;
        return;
    }

    // Determine if this is an antimatter nucleus
    uint32_t rep_type = particles.types[static_cast<uint32_t>(nuc_rep)];
    bool is_anti = (rep_type == ANTIPROTON_TYPE_PHYS);

    // Gather all particles belonging to this nucleus
    uint32_t n_total = static_cast<uint32_t>(particles.types.size());
    int Z = 0, N_count = 0, lepton_count = 0;
    std::vector<uint32_t> nucleon_indices;
    std::vector<uint32_t> lepton_indices;
    glm::vec2 sum_pos(0.0f);
    float total_energy_MeV = 0.0f;
    float total_momentum_MeV = 0.0f;  // sum of γm₀β per constituent (MeV/c)
    glm::vec2 com_vel(0.0f);          // center-of-mass velocity (sim units)
    float total_sim_mass = 0.0f;
    uint32_t oldest_birth = UINT32_MAX;
    float total_mu_N = 0.0f;  // net magnetic moment in nuclear magnetons

    for (uint32_t pi = 0; pi < n_total; ++pi) {
        if (pi >= particles.orbital_parent.size()) break;
        int32_t their_parent = particles.orbital_parent[pi];
        if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;

        uint32_t pt = particles.types[pi];
        bool counted = false;
        if (is_anti) {
            if (pt == ANTIPROTON_TYPE_PHYS) { Z++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == NEUTRON_TYPE)    { N_count++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == POSITRON_TYPE_PHYS) { lepton_count++; lepton_indices.push_back(pi); counted = true; }
        } else {
            if (pt == PROTON_TYPE)          { Z++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == NEUTRON_TYPE)    { N_count++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == ELECTRON_TYPE_PHYS) { lepton_count++; lepton_indices.push_back(pi); counted = true; }
        }
        if (!counted) continue;

        // Accumulate magnetic moment (in nuclear magnetons)
        if (pi * GENOME_SIZE + 1 < particles.genomes.size()) {
            float sp = particles.genomes[pi * GENOME_SIZE + 1];
            float ch = particles.genomes[pi * GENOME_SIZE + 0];
            if (pt == PROTON_TYPE)              total_mu_N += 2.793f * sp;
            else if (pt == ANTIPROTON_TYPE_PHYS) total_mu_N += -2.793f * sp;
            else if (pt == NEUTRON_TYPE)        total_mu_N += -1.913f * sp;
            else if (pt == ELECTRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;  // μ_B → μ_N
            else if (pt == POSITRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;
        }

        sum_pos += particles.positions[pi];

        if (readback_velocities && pi < readback_count) {
            // Sim-mass weighted velocity for CoM
            float sim_mass = (pt <= 1 || pt == 5) ? 40.0f : 1.0f;
            com_vel += readback_velocities[pi] * sim_mass;
            total_sim_mass += sim_mass;

            // Kinetic energy and momentum per constituent
            float m0 = rest_mass_MeV(pt);
            float spd = glm::length(readback_velocities[pi]);
            float beta = std::min(spd / C_SIM, 0.9999f);
            float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
            total_energy_MeV += (gamma - 1.0f) * m0;  // KE = (γ-1)m₀c²
            total_momentum_MeV += gamma * m0 * beta;   // p = γm₀βc (MeV/c)
        }

        if (pi < particles.birth_frames.size()) {
            if (particles.birth_frames[pi] < oldest_birth)
                oldest_birth = particles.birth_frames[pi];
        }
    }

    if (Z == 0) { element_card_nucleus_rep = -1; return; }

    int A = Z + N_count;
    int net_charge = Z - lepton_count;
    float total_mass = Z * 40.0f + N_count * 40.0f + lepton_count * 1.0f;
    // Center-of-mass speed for speed display
    float com_speed_sim = (total_sim_mass > 0.0f) ? glm::length(com_vel) / total_sim_mass : 0.0f;
    // Shell configuration string
    const int SHELL_CAP[] = {2, 8, 18, 32, 32, 18, 8};
    char shell_str[64] = {};
    {
        int remaining = lepton_count;
        int pos = 0;
        for (int s = 0; s < 7 && remaining > 0; ++s) {
            int in_shell = std::min(remaining, SHELL_CAP[s]);
            if (s > 0) shell_str[pos++] = '/';
            pos += snprintf(shell_str + pos, sizeof(shell_str) - pos, "%d", in_shell);
            remaining -= in_shell;
        }
    }

    // Window — bottom-right, to the left of info card
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 590, io.DisplaySize.y - 60),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(320, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoNav;

    bool open = true;
    char title[96];
    const char* title_prefix = is_anti ? "Anti-" : "";
    snprintf(title, sizeof(title), "%s%s (%s%s-%d)###ElementCard",
             title_prefix,
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "?",
             title_prefix,
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?", A);

    // Antimatter: cyan-tinted title bar; matter: warm gold
    ImVec4 title_bg = is_anti ? ImVec4(0.02f, 0.10f, 0.12f, 0.95f) : ImVec4(0.12f, 0.10f, 0.05f, 0.95f);
    ImVec4 title_bg_active = is_anti ? ImVec4(0.04f, 0.16f, 0.20f, 0.95f) : ImVec4(0.20f, 0.16f, 0.06f, 0.95f);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, title_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, title_bg_active);

    if (ImGui::Begin(title, &open, flags)) {
        float col_w = 110.0f;

        // Big element symbol + name
        if (Z <= FULL_ELEMENT_COUNT) {
            ImVec4 sym_color = is_anti ? ImVec4(0.6f, 0.95f, 1.0f, 1.0f) : ImVec4(0.9f, 0.85f, 0.6f, 1.0f);
            ImVec4 name_color = is_anti ? ImVec4(0.4f, 0.85f, 0.9f, 1.0f) : ImVec4(0.8f, 0.75f, 0.5f, 1.0f);
            if (is_anti) {
                ImGui::TextColored(sym_color, "%s%s", title_prefix, ELEMENT_SYMBOLS[Z]);
                ImGui::SameLine();
                ImGui::TextColored(name_color, "Anti-%s", ELEMENT_NAMES[Z]);
            } else {
                ImGui::TextColored(sym_color, "%s", ELEMENT_SYMBOLS[Z]);
                ImGui::SameLine();
                ImGui::TextColored(name_color, "%s", ELEMENT_NAMES[Z]);
            }
        }
        if (is_anti) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 0.7f), "(antimatter)");
        }

        ImGui::Separator();

        // Composition
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), is_anti ? "Antiprotons" : "Atomic No.");
        ImGui::SameLine(col_w); ImGui::Text("Z = %d", Z);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Neutrons");
        ImGui::SameLine(col_w); ImGui::Text("N = %d", N_count);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mass No.");
        ImGui::SameLine(col_w); ImGui::Text("A = %d", A);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), is_anti ? "Positrons" : "Electrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", lepton_count);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Shells");
        ImGui::SameLine(col_w); ImGui::Text("%s", shell_str);

        ImGui::Spacing();
        ImGui::Separator();

        // Charge
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Net Charge");
        ImGui::SameLine(col_w);
        if (net_charge == 0)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "0 (neutral)");
        else if (net_charge > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%d (ion)", net_charge);
        else
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%d (ion)", net_charge);

        // Mass
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Mass");
        ImGui::SameLine(col_w); ImGui::Text("%.0f u", total_mass);

        // Particles
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Particles");
        ImGui::SameLine(col_w); ImGui::Text("%d", Z + N_count + lepton_count);

        // Speed — center-of-mass
        {
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), com_speed_sim);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
            ImGui::SameLine(col_w); ImGui::Text("%s", spd_buf);
        }

        // Momentum — sum of constituent relativistic momenta
        {
            char mom_buf[32];
            if (total_momentum_MeV < 0.001f)
                snprintf(mom_buf, sizeof(mom_buf), "~0");
            else if (total_momentum_MeV < 1.0f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f keV/c", total_momentum_MeV * 1e3f);
            else if (total_momentum_MeV < 1e3f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f MeV/c", total_momentum_MeV);
            else if (total_momentum_MeV < 1e6f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f GeV/c", total_momentum_MeV / 1e3f);
            else
                snprintf(mom_buf, sizeof(mom_buf), "%.2f TeV/c", total_momentum_MeV / 1e6f);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
            ImGui::SameLine(col_w); ImGui::Text("%s", mom_buf);
        }

        // Energy — relativistic total (sum of γm₀c² for all constituents)
        {
            char e_buf[32];
            fmt_energy_ev(e_buf, sizeof(e_buf), total_energy_MeV);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
            ImGui::SameLine(col_w); ImGui::Text("%s", e_buf);
        }

        // Age
        if (oldest_birth != UINT32_MAX) {
            uint32_t age_frames = frame_counter_display - oldest_birth;
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f) ImGui::Text("%.1f s", age_sec);
            else ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // Magnetic moment — net of all constituents (in nuclear magnetons)
        if (std::abs(total_mu_N) > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
            ImGui::SameLine(col_w);
            ImGui::Text("%+.2f \xCE\xBC\x4E", total_mu_N);
        }

        // Stability — look up isotope decay table
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Stability");
        ImGui::SameLine(col_w);
        {
            const IsotopeDecayEntry* iso = lookup_isotope_decay(Z, N_count);
            NuclearDecayMode dmode = NDECAY_NONE;
            float hl = 0.0f;
            if (iso) { dmode = iso->mode; hl = iso->half_life_frames; }
            else dmode = general_stability_rule(Z, N_count, hl);

            if (dmode == NDECAY_NONE) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Stable");
            } else {
                const char* mode_str = "?";
                switch (dmode) {
                    case NDECAY_ALPHA:            mode_str = "\xce\xb1"; break;
                    case NDECAY_BETA_MINUS:       mode_str = "\xce\xb2\xe2\x81\xbb"; break;
                    case NDECAY_BETA_PLUS:        mode_str = "\xce\xb2\xe2\x81\xba"; break;
                    case NDECAY_NEUTRON_EMISSION: mode_str = "n-emit"; break;
                    case NDECAY_PROTON_EMISSION:  mode_str = "p-emit"; break;
                    default: break;
                }
                float hl_sec = hl / 60.0f;
                ImVec4 color;
                if (hl < 12.0f) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);       // instant — red
                else if (hl < 300.0f) color = ImVec4(1.0f, 0.5f, 0.2f, 1.0f);  // short — orange
                else if (hl < 7200.0f) color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); // medium — yellow
                else color = ImVec4(0.8f, 0.8f, 0.4f, 1.0f);                   // long — pale yellow

                if (hl_sec < 60.0f)
                    ImGui::TextColored(color, "%s  t\xc2\xbd=%.1fs", mode_str, hl_sec);
                else
                    ImGui::TextColored(color, "%s  t\xc2\xbd=%.1fmin", mode_str, hl_sec / 60.0f);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Clickable constituent list
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Nucleons:");
        ImGui::SameLine(col_w);
        for (size_t ni = 0; ni < nucleon_indices.size() && ni < 12; ++ni) {
            uint32_t pi = nucleon_indices[ni];
            uint32_t pt = particles.types[pi];
            ImVec4 c = (pt < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_UI_COLORS[pt] : ImVec4(1,1,1,1);
            const char* lb = (pt < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_LABELS[pt] : "?";
            char btn[32];
            snprintf(btn, sizeof(btn), "%s##en%zu", lb, ni);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            if (ImGui::SmallButton(btn)) navigate_to_particle = static_cast<int32_t>(pi);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        if (nucleon_indices.size() > 12) ImGui::Text("+%zu", nucleon_indices.size() - 12);
        else ImGui::NewLine();

        if (!lepton_indices.empty()) {
            const char* lepton_label = is_anti ? "Positrons:" : "Electrons:";
            const char* lepton_btn_label = is_anti ? "e+" : "e-";
            uint32_t lepton_type = is_anti ? POSITRON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", lepton_label);
            ImGui::SameLine(col_w);
            for (size_t ei = 0; ei < lepton_indices.size() && ei < 12; ++ei) {
                uint32_t pi = lepton_indices[ei];
                char btn[32];
                snprintf(btn, sizeof(btn), "%s##ee%zu", lepton_btn_label, ei);
                ImGui::PushStyleColor(ImGuiCol_Text, PHYS_TYPE_UI_COLORS[lepton_type]);
                if (ImGui::SmallButton(btn)) navigate_to_particle = static_cast<int32_t>(pi);
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            if (lepton_indices.size() > 12) ImGui::Text("+%zu", lepton_indices.size() - 12);
            else ImGui::NewLine();
        }

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (element_move_mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
            if (ImGui::Button("Moving...", ImVec2(86, 26))) {
                element_move_mode = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
        } else {
            if (ImGui::Button("Move", ImVec2(72, 26))) {
                element_move_mode = true;
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
            if (ImGui::Button("Delete", ImVec2(72, 26))) {
                request_element_delete = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.15f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.55f, 0.2f, 0.90f));
            if (ImGui::Button("Duplicate", ImVec2(86, 26))) {
                request_element_duplicate = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.50f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.40f, 0.65f, 0.90f));
            if (ImGui::Button("Export", ImVec2(72, 26))) {
                request_element_export = true;
                export_element_rep = nuc_rep;
            }
            ImGui::PopStyleColor(2);
        }

        // Navigate + Close
        if (ImGui::Button("Navigate", ImVec2(100, 26))) {
            navigate_to_particle = nuc_rep;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 26))) {
            element_card_nucleus_rep = -1;
            element_move_mode = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!open) element_card_nucleus_rep = -1;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Molecule Detail Card ────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_molecule_card(const Particles& particles) {
    if (molecule_card_atom_rep < 0) return;

    // Resolve stable atom rep → current molecule_list index
    int32_t mol_idx = -1;
    for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
        for (uint32_t ei : molecule_list[mi].atom_indices) {
            if (ei < element_list.size() &&
                static_cast<int32_t>(element_list[ei].rep) == molecule_card_atom_rep) {
                mol_idx = static_cast<int32_t>(mi);
                break;
            }
        }
        if (mol_idx >= 0) break;
    }
    if (mol_idx < 0) { molecule_card_atom_rep = -1; return; }

    auto& mol = molecule_list[static_cast<size_t>(mol_idx)];
    if (mol.atom_indices.empty()) { molecule_card_atom_rep = -1; return; }

    // Build formula for title
    std::string formula = build_molecular_formula(element_list, mol.atom_indices);

    // Aggregate physics from all constituent particles
    int total_Z = 0, total_N = 0, total_leptons = 0;
    int total_particles = 0;
    float total_energy_MeV = 0.0f;
    float total_momentum_MeV = 0.0f;
    glm::vec2 com_vel(0.0f);
    float total_sim_mass = 0.0f;
    uint32_t oldest_birth = UINT32_MAX;
    float total_mu_N = 0.0f;  // net magnetic moment in nuclear magnetons
    uint32_t n_total = static_cast<uint32_t>(particles.types.size());

    // Collect per-atom data
    struct AtomInfo {
        int Z, N, electrons;
        uint32_t rep;
        const char* sym;
        const char* name;
    };
    std::vector<AtomInfo> atoms;

    for (uint32_t ei : mol.atom_indices) {
        if (ei >= element_list.size()) continue;
        auto& elem = element_list[ei];
        total_Z += elem.Z;
        total_N += elem.N;
        total_leptons += elem.electrons;

        const char* sym = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[elem.Z] : "?";
        const char* name = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[elem.Z] : "?";
        atoms.push_back({elem.Z, elem.N, elem.electrons, elem.rep, sym, name});

        // Scan all particles belonging to this atom for physics data
        for (uint32_t pi = 0; pi < n_total; ++pi) {
            if (pi >= particles.orbital_parent.size()) break;
            int32_t par = particles.orbital_parent[pi];
            if (par != static_cast<int32_t>(elem.rep) &&
                static_cast<int32_t>(pi) != static_cast<int32_t>(elem.rep)) continue;

            uint32_t pt = particles.types[pi];
            bool is_nucleon = (pt == PROTON_TYPE || pt == NEUTRON_TYPE);
            bool is_lepton = (pt == ELECTRON_TYPE_PHYS);
            if (!is_nucleon && !is_lepton) continue;
            total_particles++;

            // Accumulate magnetic moment
            if (pi * GENOME_SIZE + 1 < particles.genomes.size()) {
                float sp = particles.genomes[pi * GENOME_SIZE + 1];
                float ch = particles.genomes[pi * GENOME_SIZE + 0];
                if (pt == PROTON_TYPE)              total_mu_N += 2.793f * sp;
                else if (pt == NEUTRON_TYPE)        total_mu_N += -1.913f * sp;
                else if (pt == ELECTRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;
            }

            if (readback_velocities && pi < readback_count) {
                float sim_mass = (pt <= 1) ? 40.0f : 1.0f;
                com_vel += readback_velocities[pi] * sim_mass;
                total_sim_mass += sim_mass;

                float m0 = rest_mass_MeV(pt);
                float spd = glm::length(readback_velocities[pi]);
                float beta = std::min(spd / C_SIM, 0.9999f);
                float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                total_energy_MeV += (gamma - 1.0f) * m0;
                total_momentum_MeV += gamma * m0 * beta;
            }

            if (pi < particles.birth_frames.size() &&
                particles.birth_frames[pi] < oldest_birth)
                oldest_birth = particles.birth_frames[pi];
        }
    }

    int net_charge = total_Z - total_leptons;
    float com_speed_sim = (total_sim_mass > 0.0f) ? glm::length(com_vel) / total_sim_mass : 0.0f;

    // Count bonds within molecule
    int bond_count = 0;
    if (bond_data_ptr && bond_data_count > 0) {
        for (auto& atom : atoms) {
            uint32_t base = atom.rep * MAX_BONDS_PER_PARTICLE;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                if (base + s >= bond_data_count) break;
                uint32_t partner = bond_data_ptr[base + s];
                if (partner != 0xFFFFFFFFu && atom.rep < partner) bond_count++;
            }
        }
    }

    // Window position — next to element card position
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 590, io.DisplaySize.y - 60),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(340, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoNav;

    bool open = true;
    char title[128];
    snprintf(title, sizeof(title), "Molecule: %s###MoleculeCard", formula.c_str());

    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.04f, 0.07f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.06f, 0.12f, 0.24f, 0.95f));

    if (ImGui::Begin(title, &open, flags)) {
        float col_w = 120.0f;

        // Big formula display
        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "%s", formula.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.55f, 0.85f, 0.8f), "(%d atoms)", static_cast<int>(atoms.size()));
        ImGui::Separator();

        // Composition
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Protons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_Z);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Neutrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_N);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Electrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_leptons);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Bonds");
        ImGui::SameLine(col_w); ImGui::Text("%d", bond_count);

        ImGui::Spacing();
        ImGui::Separator();

        // Charge
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Net Charge");
        ImGui::SameLine(col_w);
        if (net_charge == 0)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "0 (neutral)");
        else if (net_charge > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%d (ion)", net_charge);
        else
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%d (ion)", net_charge);

        // Mass
        float total_mass = total_Z * 40.0f + total_N * 40.0f + total_leptons * 1.0f;
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Mass");
        ImGui::SameLine(col_w); ImGui::Text("%.0f u", total_mass);

        // Particles
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Particles");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_particles);

        // Speed
        {
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), com_speed_sim);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
            ImGui::SameLine(col_w); ImGui::Text("%s", spd_buf);
        }

        // Momentum
        {
            char mom_buf[32];
            if (total_momentum_MeV < 0.001f)
                snprintf(mom_buf, sizeof(mom_buf), "~0");
            else if (total_momentum_MeV < 1.0f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f keV/c", total_momentum_MeV * 1e3f);
            else if (total_momentum_MeV < 1e3f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f MeV/c", total_momentum_MeV);
            else if (total_momentum_MeV < 1e6f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f GeV/c", total_momentum_MeV / 1e3f);
            else
                snprintf(mom_buf, sizeof(mom_buf), "%.2f TeV/c", total_momentum_MeV / 1e6f);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
            ImGui::SameLine(col_w); ImGui::Text("%s", mom_buf);
        }

        // Energy
        {
            char e_buf[32];
            fmt_energy_ev(e_buf, sizeof(e_buf), total_energy_MeV);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
            ImGui::SameLine(col_w); ImGui::Text("%s", e_buf);
        }

        // Age
        if (oldest_birth != UINT32_MAX) {
            uint32_t age_frames = frame_counter_display - oldest_birth;
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f) ImGui::Text("%.1f s", age_sec);
            else ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // Magnetic moment — net of all constituent particles
        if (std::abs(total_mu_N) > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
            ImGui::SameLine(col_w);
            ImGui::Text("%+.2f \xCE\xBC\x4E", total_mu_N);
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Constituent atoms — clickable buttons that open element cards
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Atoms:");
        ImGui::SameLine(col_w);
        for (size_t ai = 0; ai < atoms.size() && ai < 12; ++ai) {
            auto& a = atoms[ai];
            int A = a.Z + a.N;
            char btn[32];
            snprintf(btn, sizeof(btn), "%s-%d##ma%zu", a.sym, A, ai);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.6f, 1.0f));
            if (ImGui::SmallButton(btn)) {
                element_card_nucleus_rep = static_cast<int32_t>(a.rep);
                navigate_to_particle = static_cast<int32_t>(a.rep);
            }
            ImGui::PopStyleColor();
            if (ai + 1 < atoms.size() && ai < 11) ImGui::SameLine();
        }
        if (atoms.size() > 12) ImGui::Text("+%zu more", atoms.size() - 12);

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Navigate", ImVec2(72, 26))) {
            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
            navigate_to_particle = static_cast<int32_t>(first_rep);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.50f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.40f, 0.65f, 0.90f));
        if (ImGui::Button("Export", ImVec2(72, 26))) {
            request_molecule_export = true;
            export_molecule_atom_rep = molecule_card_atom_rep;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(72, 26))) {
            molecule_card_atom_rep = -1;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!open) molecule_card_atom_rep = -1;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Event Notifications (Top-Right Toast Stack) ─────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════════
// ── Particle Accelerator Panel ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════════
// ── Decay / Interaction Event Log ────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_decay_log() {
    if (!show_decay_log) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 420.0f;
    float max_h = io.DisplaySize.y - 120.0f;
    float win_h = std::min(500.0f, max_h);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - win_w * 0.5f,
                                    io.DisplaySize.y * 0.5f - win_h * 0.5f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(340, 200), ImVec2(560, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.06f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.10f, 0.03f, 0.95f));

    char title[64];
    snprintf(title, sizeof(title), "Event Log (%d)###DecayLog",
             static_cast<int>(decay_log.size()));

    if (!ImGui::Begin(title, &show_decay_log)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Event type labels and colors
    static const char* TYPE_LABELS[] = {
        "Decay", "Nuclear", "Fusion", "Fission", "Annihil.",
        "Photo-e", "Spall.", "Pair", "Pion", "VMD", "Photodis.",
        "Bond+", "Bond-"
    };
    static const ImVec4 TYPE_COLORS[] = {
        ImVec4(1.0f, 0.8f, 0.5f, 1.0f),   // PARTICLE_DECAY — warm yellow
        ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // NUCLEAR_DECAY — orange
        ImVec4(0.4f, 0.9f, 0.6f, 1.0f),   // FUSION — green
        ImVec4(1.0f, 0.6f, 0.2f, 1.0f),   // FISSION — amber
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f),   // ANNIHILATION — red
        ImVec4(0.3f, 0.7f, 1.0f, 1.0f),   // PHOTOELECTRIC — blue
        ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // SPALLATION — orange
        ImVec4(0.3f, 0.7f, 1.0f, 1.0f),   // PAIR_PRODUCTION — blue
        ImVec4(0.4f, 0.9f, 0.6f, 1.0f),   // PION_PRODUCTION — green
        ImVec4(0.9f, 0.3f, 0.9f, 1.0f),   // VMD — magenta
        ImVec4(0.8f, 0.6f, 1.0f, 1.0f),   // PHOTODISINTEGRATION — purple
        ImVec4(0.3f, 0.85f, 0.5f, 1.0f),  // BOND_FORMED — teal-green
        ImVec4(0.9f, 0.45f, 0.3f, 1.0f),  // BOND_BROKEN — warm red
    };

    // Summary counts by type
    int type_counts[DEVT_COUNT] = {};
    for (const auto& e : decay_log) {
        if (e.type < DEVT_COUNT) type_counts[e.type]++;
    }

    // Filter toggles — click type label to show/hide that event type
    float wrap_x = ImGui::GetContentRegionAvail().x - 60.0f;
    float cur_x = 0.0f;
    for (int t = 0; t < DEVT_COUNT; ++t) {
        if (type_counts[t] == 0) continue;
        char tag_label[32];
        snprintf(tag_label, sizeof(tag_label), "%s:%d###filt%d", TYPE_LABELS[t], type_counts[t], t);
        float btn_w = ImGui::CalcTextSize(tag_label).x + 10.0f;
        if (cur_x + btn_w > wrap_x && cur_x > 0.0f) {
            cur_x = 0.0f;  // wrap to next line
        } else if (cur_x > 0.0f) {
            ImGui::SameLine(0, 4);
        }
        ImVec4 col = TYPE_COLORS[t];
        if (!event_filter[t]) col = ImVec4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.2f, col.y * 0.2f, col.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::SmallButton(tag_label)) {
            event_filter[t] = !event_filter[t];
            expanded_event_idx = -1;
        }
        ImGui::PopStyleColor(3);
        cur_x += btn_w + 4.0f;
    }
    if (decay_log.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "No events yet");
    }

    ImGui::Spacing();

    // Clear button
    if (!decay_log.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 0.5f));
        if (ImGui::SmallButton("Clear")) {
            decay_log.clear();
            expanded_event_idx = -1;
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Scrollable event list (newest at top)
    ImGui::BeginChild("##DecayLogScroll", ImVec2(0, 0), false);

    for (int idx = static_cast<int>(decay_log.size()) - 1; idx >= 0; --idx) {
        const auto& entry = decay_log[idx];
        if (entry.type < DEVT_COUNT && !event_filter[entry.type]) continue;
        bool is_expanded = (expanded_event_idx == idx);
        bool has_details = !entry.details.empty();

        ImGui::PushID(idx);

        // Clickable row
        char row_label[384];
        {
            struct tm tm_buf;
            localtime_r(&entry.timestamp, &tm_buf);
            const char* tag = (entry.type < DEVT_COUNT) ? TYPE_LABELS[entry.type] : "?";
            const char* arrow = has_details ? (is_expanded ? "v " : "> ") : "  ";
            snprintf(row_label, sizeof(row_label), "%s%02d:%02d:%02d  [%-8s]  %s",
                     arrow,
                     tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                     tag, entry.description.c_str());
        }

        ImVec4 tag_color = (entry.type < DEVT_COUNT) ? TYPE_COLORS[entry.type] : ImVec4(1,1,1,1);
        ImGui::PushStyleColor(ImGuiCol_Text, entry.color);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(tag_color.x * 0.15f, tag_color.y * 0.15f,
                                                       tag_color.z * 0.15f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(tag_color.x * 0.25f, tag_color.y * 0.25f,
                                                              tag_color.z * 0.25f, 0.6f));
        if (ImGui::Selectable(row_label, is_expanded, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
            if (has_details)
                expanded_event_idx = is_expanded ? -1 : idx;
        }
        ImGui::PopStyleColor(3);

        // Show detail text when expanded
        if (is_expanded && has_details) {
            ImGui::Indent(20.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 10.0f);
            ImGui::TextWrapped("%s", entry.details.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Unindent(20.0f);
            ImGui::Spacing();
        }

        ImGui::PopID();
    }

    // Auto-scroll to top (newest) when new events arrive
    if (ImGui::GetScrollY() < 10.0f)
        ImGui::SetScrollHereY(0.0f);

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Nuclear Reactions Debug Window ──────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_nuclear_debug(SimConfig& cfg) {
    if (!show_nuclear_debug) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 680, 60), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(320, 540), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 300), ImVec2(500, 800));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.14f, 0.08f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.22f, 0.12f, 0.03f, 0.95f));

    if (!ImGui::Begin("Nuclear Reactions###NuclearDebug", &show_nuclear_debug)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    float w = ImGui::GetContentRegionAvail().x;

    // ── Reaction Toggles ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Reaction Toggles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Fusion", &cfg.fusion_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Fission", &cfg.fission_enabled);

        ImGui::Checkbox("Spallation", &cfg.spallation_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Compton", &cfg.compton_enabled);

        ImGui::Checkbox("Annihilation", &cfg.annihilation_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Decay", &cfg.decay_enabled);

        ImGui::Checkbox("Nuclear Decay", &cfg.nuclear_decay_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Virtual Pairs", &cfg.virtual_pairs_enabled);
    }

    ImGui::Spacing();

    // ── Fusion ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fusion")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Coulomb Barrier (keV)##fus", &cfg.fusion_threshold_keV, 0.0f, 2000.0f, "%.0f keV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gamow peak barrier energy.\n550 keV = p+p (solar core).\nLower = easier fusion.");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Radius (px)##fus", &cfg.fusion_radius, 2.0f, 30.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Collision distance for fusion check.\nDefault: 8 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##fus", &cfg.max_fusions_per_frame, 1, 20);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum fusion events per tick.\nDefault: 5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Energy##fus", &cfg.fusion_min_energy, 0.01f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min particle energy to participate.\nDefault: 0.1");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("p+n Min KE (keV)##fus", &cfg.fusion_pn_min_ke_keV, 0.1f, 100.0f, "%.1f keV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min CM kinetic energy for p+n capture.\nDefault: 1.0 keV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Binding Energy (MeV)##fus", &cfg.fusion_binding_mev, 0.5f, 10.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deuteron binding Q-value.\nDefault: 2.22 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Leptonic Q (MeV)##fus", &cfg.fusion_leptonic_q_mev, 0.0f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("p+p chain e+ + v energy.\nDefault: 0.42 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Product Separation##fus", &cfg.fusion_product_separation, 1.0f, 10.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Nucleon pair separation distance.\nDefault: 3.0 px");
    }

    // ── Fission ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fission")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Neutron Threshold##fis", &cfg.fission_neutron_threshold, 0.1f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum neutron energy to trigger fission.\nDefault: 0.6");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Min Cluster Size##fis", &cfg.min_fission_cluster, 2, 100);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum nucleons in target nucleus.\nDefault: 20");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##fis", &cfg.max_fissions_per_frame, 1, 10);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum fission events per tick.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Cluster Radius (px)##fis", &cfg.fission_cluster_radius, 4.0f, 30.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Search radius for nucleons.\nDefault: 12.0 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Barrier (MeV)##fis", &cfg.fission_barrier_mev, 0.1f, 20.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min neutron KE to trigger fission.\nU-235 barrier: ~5.75 MeV\nDefault: 5.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Min Protons##fis", &cfg.fission_min_protons, 1, 40);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min protons for Coulomb instability.\nDefault: 8");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Fissility (Z\xC2\xB2/A)##fis", &cfg.fission_fissility_threshold, 1.0f, 50.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bohr-Wheeler fissility threshold.\nZ\xC2\xB2/A must exceed this for fission.\nU-235: 36.0, C-12: 3.0, Fe-56: 12.1\nDefault: 35.0 (real physics)");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Energy/Nucleon (MeV)##fis", &cfg.fission_energy_per_nucleon, 0.1f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("MeV released per nucleon.\nDefault: 1.0");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Fragment Energy (MeV)##fis", &cfg.fission_fragment_energy_mev, 0.1f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Energy boost per fragment.\nDefault: 1.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Free Neutrons Min##fis", &cfg.fission_free_neutrons_min, 0, 5);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min chain-reaction neutrons.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Free Neutrons Max##fis", &cfg.fission_free_neutrons_max, 1, 8);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max chain-reaction neutrons.\nDefault: 3");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Neutron KE (MeV)##fis", &cfg.fission_neutron_ke_mev, 0.5f, 10.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("KE of ejected neutrons.\nDefault: 2.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Spawn Radius (px)##fis", &cfg.fission_spawn_radius, 1.0f, 20.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Neutron spawn distance.\nDefault: 5.0 px");
    }

    // ── Particle Decay ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Particle Decay")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Energy Threshold##pdec", &cfg.decay_threshold, 0.01f, 0.50f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Particle decays when energy drops below this.\nDefault: 0.08\nLower = particles live longer before decaying.");
    }

    // ── Nuclear Decay ────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Nuclear Decay")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Rate Multiplier##ndec", &cfg.decay_rate_multiplier, 0.01f, 10.0f, "%.2fx",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scales all nuclear half-lives.\n< 1 = faster decay, > 1 = slower.\nDefault: 1.0");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Alpha KE (MeV)##ndec", &cfg.alpha_ke_mev, 0.5f, 20.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Kinetic energy of emitted alpha particles.\nDefault: 5.0 MeV (typical: 4-6 MeV)");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Nucleon KE (MeV)##ndec", &cfg.nucleon_emit_ke_mev, 0.1f, 10.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Kinetic energy of emitted neutrons/protons.\nDefault: 2.0 MeV (typical: 1-3 MeV)");
    }

    // ── Compton / Photoelectric ──────────────────────────────────────────
    if (ImGui::CollapsingHeader("Compton / Photoelectric")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Radius (px)##comp", &cfg.compton_radius, 5.0f, 60.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Photon-electron interaction distance.\nDefault: 25 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##comp", &cfg.max_compton_per_frame, 1, 20);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum Compton interactions per tick.\nDefault: 8");
    }

    // ── Spallation ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Spallation")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Speed (px/f)##spal", &cfg.spallation_min_speed, 20.0f, 300.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum projectile speed to trigger spallation.\nDefault: 120 px/frame");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Energy##spal", &cfg.spallation_min_energy, 0.1f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum projectile energy.\nDefault: 0.5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##spal", &cfg.max_spallations_per_frame, 1, 10);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum spallation events per tick.\nDefault: 3");
    }

    // ── Annihilation ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Annihilation")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Contact Radius (px)##annih", &cfg.annihilation_radius, 1.0f, 20.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matter-antimatter annihilation distance.\nDefault: 5.0 px");
    }

    // ── Virtual Pairs / Casimir ─────────────────────────────────────────
    if (ImGui::CollapsingHeader("Virtual Pairs")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Vacuum Energy##vpair", &cfg.vacuum_energy, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vacuum fluctuation rate.\nHigher = more spontaneous pairs.\n0 = off. Default: 0.5");

        ImGui::SetNextItemWidth(-1);
        int vp_max = static_cast<int>(cfg.virtual_pair_max_per_tick);
        if (ImGui::SliderInt("Max/frame##vpair", &vp_max, 1, 10))
            cfg.virtual_pair_max_per_tick = static_cast<uint32_t>(vp_max);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum virtual pairs spawned per tick.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Casimir Radius##vpair", &cfg.casimir_radius, 5.0f, 60.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Detection range for Casimir force.\nDefault: 30 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Casimir Strength##vpair", &cfg.casimir_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Casimir attractive force scaling.\n0 = no Casimir effect.\nDefault: 0.5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Pair Scatter##vpair", &cfg.virtual_pair_scatter, 1.0f, 10.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawn separation of virtual pair.\nDefault: 3.0 px");
    }

    // ── Covalent Bonds ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Covalent Bonds")) {
        ImGui::Checkbox("Bonds Enabled##cbond", &cfg.bonds_enabled);

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Spring K##cbond", &cfg.bond_spring_k, 10.0f, 2000.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bond spring constant (Hooke's law).\nHigher = stiffer bonds.\nDefault: 500");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Rest Length (px)##cbond", &cfg.bond_rest_length, 8.0f, 80.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Equilibrium bond length.\nMust exceed nuclear radii + 10px.\nDefault: 36 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Break Factor##cbond", &cfg.bond_break_factor, 1.5f, 5.0f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bond breaks at distance > rest * factor.\nDefault: 2.2x");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Form Radius (px)##cbond", &cfg.bond_form_radius, 15.0f, 100.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max distance for new bond formation.\nDefault: 44 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Activation Energy##cbond", &cfg.bond_activation_energy, 0.001f, 0.2f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max relative velocity for bonding (too fast = scattering).\nDefault: 0.02");

        // Count active bonds
        uint32_t active_bonds = 0;
        if (bond_data_ptr && bond_data_count > 0) {
            uint32_t max_p = bond_data_count / MAX_BONDS_PER_PARTICLE;
            for (uint32_t bi = 0; bi < max_p; ++bi) {
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                    uint32_t partner = bond_data_ptr[bi * MAX_BONDS_PER_PARTICLE + s];
                    if (partner != 0xFFFFFFFFu && bi < partner) active_bonds++;
                }
            }
        }
        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "Active bonds: %u", active_bonds);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Reset to Defaults ────────────────────────────────────────────────
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        cfg.fusion_threshold_keV      = 550.0f;
        cfg.fusion_radius             = 8.0f;
        cfg.max_fusions_per_frame     = 5;
        cfg.fission_neutron_threshold = 0.6f;
        cfg.min_fission_cluster       = 20;
        cfg.max_fissions_per_frame    = 2;
        cfg.fission_barrier_mev       = 5.0f;
        cfg.fission_min_protons       = 8;
        cfg.fission_fissility_threshold = 35.0f;
        cfg.decay_threshold           = 0.08f;
        cfg.alpha_ke_mev              = 5.0f;
        cfg.nucleon_emit_ke_mev       = 2.0f;
        cfg.decay_rate_multiplier     = 1.0f;
        cfg.compton_radius            = 25.0f;
        cfg.max_compton_per_frame     = 8;
        cfg.spallation_min_speed      = 120.0f;
        cfg.spallation_min_energy     = 0.5f;
        cfg.max_spallations_per_frame = 3;
        cfg.annihilation_radius       = 5.0f;
        cfg.virtual_pair_threshold    = 2.1f;
        cfg.virtual_pair_max_per_tick = 2;

        cfg.fusion_enabled            = true;
        cfg.fission_enabled           = true;
        cfg.spallation_enabled        = true;
        cfg.compton_enabled           = true;
        cfg.annihilation_enabled      = true;
        cfg.decay_enabled             = true;
        cfg.nuclear_decay_enabled     = true;
        cfg.virtual_pairs_enabled     = true;

        cfg.bonds_enabled             = true;
        cfg.bond_spring_k             = 500.0f;
        cfg.bond_rest_length          = 36.0f;
        cfg.bond_break_factor         = 2.2f;
        cfg.bond_form_radius          = 44.0f;
        cfg.bond_activation_energy    = 0.02f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset all nuclear reaction parameters to Standard Model defaults");

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Particle Accelerator Panel ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_accelerator_panel() {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 650, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, std::min(420.0f, max_h)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(340, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.04f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.18f, 0.06f, 0.03f, 0.95f));

    bool panel_open = accel_mode;
    if (!ImGui::Begin("Particle Accelerator", &panel_open)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        if (!panel_open) {
            accel_mode = false;
            accel_phase = 0;
            accel_source_idx = -1;
        }
        return;
    }
    if (!panel_open) {
        accel_mode = false;
        accel_phase = 0;
        accel_source_idx = -1;
    }

    // ── Status ──
    if (accel_phase == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Click a particle to set as target");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Click to fire at target!");
        ImGui::SameLine();
        if (ImGui::SmallButton("Change Target")) {
            accel_phase = 0;
            accel_source_idx = -1;
        }
    }

    ImGui::Separator();

    // ── Projectile Type ──
    if (ImGui::CollapsingHeader("Projectile", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Common types in rows
        static const int ROW1[] = { PROTON_TYPE, NEUTRON_TYPE, ELECTRON_TYPE_PHYS,
                                     PHOTON_TYPE_PHYS, POSITRON_TYPE_PHYS, NEUTRINO_TYPE_PHYS };
        static const int ROW2[] = { MUON_TYPE_PHYS, ANTIMUON_TYPE_PHYS,
                                     UP_QUARK_TYPE, DOWN_QUARK_TYPE,
                                     GLUON_TYPE_PHYS, HIGGS_TYPE_PHYS };

        auto draw_type_row = [&](const int* types, int count) {
            for (int r = 0; r < count; ++r) {
                int t = types[r];
                ImVec4 col = PHYS_TYPE_UI_COLORS[t];
                bool selected = (accel_fire_type == t);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x*0.6f, col.y*0.6f, col.z*0.6f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x*0.7f, col.y*0.7f, col.z*0.7f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, col);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x*0.25f, col.y*0.25f, col.z*0.25f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x*0.4f, col.y*0.4f, col.z*0.4f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                }
                char id[32];
                snprintf(id, sizeof(id), "%s##acc%d", PHYS_TYPE_LABELS[t], t);
                if (ImGui::Button(id, ImVec2(38, 24)))
                    accel_fire_type = t;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", PHYS_TYPE_NAMES[t]);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (r < count - 1) ImGui::SameLine(0, 4);
            }
        };

        draw_type_row(ROW1, 6);
        draw_type_row(ROW2, 6);

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Selected: %s",
                           PHYS_TYPE_NAMES[accel_fire_type]);
    }

    // ── Speed ──
    if (ImGui::CollapsingHeader("Speed", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Show slider as fraction of c (0.03c – 1.00c)
        float beta = accel_speed / C_SIM;
        char slider_label[32];
        snprintf(slider_label, sizeof(slider_label), "%.3fc", beta);
        ImGui::SliderFloat("##AccelSpeed", &accel_speed, 10.0f, C_SIM, slider_label);

        // Real-world speed annotation
        char spd_buf[32];
        fmt_speed(spd_buf, sizeof(spd_buf), accel_speed);
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.9f, 1.0f), "%s", spd_buf);

        // Massless particles always travel at c — note this
        float m0 = rest_mass_MeV(accel_fire_type);
        if (m0 < 0.001f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "(massless: always c)");
        }
    }

    // ── Fire Mode ──
    if (ImGui::CollapsingHeader("Fire Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::RadioButton("Single Shot",  &accel_fire_mode, 0);
        ImGui::RadioButton("Triple Shot",  &accel_fire_mode, 1);
        ImGui::RadioButton("Stream",       &accel_fire_mode, 2);
        if (accel_fire_mode == 2) {
            int interval = static_cast<int>(accel_stream_interval);
            ImGui::SliderInt("Rate##stream", &interval, 1, 10, "every %d frames");
            accel_stream_interval = static_cast<uint32_t>(interval);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Element List Window ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════


// Helper: format energy value as human-readable string
static void format_energy_str(char* buf, size_t buf_size, float energy_MeV) {
    float eV = energy_MeV * 1e6f;
    if (eV < 1e3f)
        snprintf(buf, buf_size, "%.0f eV", eV);
    else if (eV < 1e6f)
        snprintf(buf, buf_size, "%.1f keV", eV / 1e3f);
    else if (eV < 1e9f)
        snprintf(buf, buf_size, "%.1f MeV", eV / 1e6f);
    else if (eV < 1e12f)
        snprintf(buf, buf_size, "%.2f GeV", eV / 1e9f);
    else
        snprintf(buf, buf_size, "%.2f TeV", eV / 1e12f);
}

// Helper: format age from birth frame
static void format_age_str(char* buf, size_t buf_size, uint32_t oldest_birth, uint32_t current_frame) {
    if (oldest_birth != UINT32_MAX) {
        float age_sec = (current_frame - oldest_birth) / 60.0f;
        if (age_sec < 60.0f)
            snprintf(buf, buf_size, "%.0fs", age_sec);
        else
            snprintf(buf, buf_size, "%.1fm", age_sec / 60.0f);
    } else {
        snprintf(buf, buf_size, "-");
    }
}

// Helper: build molecular formula string from element Z counts (Hill system: C first, H second, rest alpha)
static std::string build_molecular_formula(const std::vector<PhysicsInterface::ElementSummary>& elems,
                                            const std::vector<uint32_t>& atom_indices) {
    struct FormulaEntry { const char* sym; int count; int Z; };
    std::map<int, int> z_counts;
    for (uint32_t ei : atom_indices) {
        z_counts[elems[ei].Z]++;
    }
    std::vector<FormulaEntry> entries;
    for (auto& [z, cnt] : z_counts) {
        const char* sym = (z >= 1 && z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[z] : "?";
        entries.push_back({sym, cnt, z});
    }
    std::sort(entries.begin(), entries.end(), [](const FormulaEntry& a, const FormulaEntry& b) {
        if (a.Z == 6 && b.Z != 6) return true;
        if (b.Z == 6 && a.Z != 6) return false;
        if (a.Z == 1 && b.Z != 1) return true;
        if (b.Z == 1 && a.Z != 1) return false;
        return a.sym < b.sym ? true : (a.sym > b.sym ? false : a.Z < b.Z);
    });
    std::string formula;
    for (auto& e : entries) {
        formula += e.sym;
        if (e.count > 1) formula += std::to_string(e.count);
    }
    return formula;
}

void PhysicsInterface::draw_element_list() {
    if (!show_element_list || element_list.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 360.0f;
    float max_h = io.DisplaySize.y - 120.0f;

    // Count display rows: molecules if available, else individual atoms
    size_t display_count = molecule_list.empty() ? element_list.size() : molecule_list.size();
    float win_h = std::min(40.0f + static_cast<float>(display_count) * 28.0f + 40.0f, max_h);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - win_w * 0.5f,
                                    io.DisplaySize.y * 0.5f - win_h * 0.5f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(340, 120), ImVec2(520, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.06f, 0.03f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.10f, 0.04f, 0.95f));

    // Count molecules vs free atoms
    int mol_count = 0, free_count = 0;
    for (auto& mol : molecule_list) {
        if (mol.atom_indices.size() > 1) mol_count++;
        else free_count++;
    }

    char title[64];
    if (mol_count > 0)
        snprintf(title, sizeof(title), "Elements (%d) + Molecules (%d)###ElementList",
                 static_cast<int>(element_list.size()), mol_count);
    else
        snprintf(title, sizeof(title), "Elements (%d)###ElementList",
                 static_cast<int>(element_list.size()));

    if (!ImGui::Begin(title, &show_element_list)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Use molecule_list if available (always populated when bonds_enabled)
    if (!molecule_list.empty()) {
        // Column headers
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "  %-14s %4s %10s %6s",
                           "Formula", "Q", "Energy", "Age");
        ImGui::Separator();

        ImGui::BeginChild("##ElemListScroll", ImVec2(0, 0), false);

        for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
            auto& mol = molecule_list[mi];
            bool is_molecule = mol.atom_indices.size() > 1;

            // Build formula
            std::string formula = build_molecular_formula(element_list, mol.atom_indices);

            // Charge string
            char charge_str[16];
            if (mol.total_charge == 0) snprintf(charge_str, sizeof(charge_str), "0");
            else if (mol.total_charge > 0) snprintf(charge_str, sizeof(charge_str), "+%d", mol.total_charge);
            else snprintf(charge_str, sizeof(charge_str), "%d", mol.total_charge);

            // Energy and age
            char energy_str[32], age_str[16];
            format_energy_str(energy_str, sizeof(energy_str), mol.total_energy_MeV);
            format_age_str(age_str, sizeof(age_str), mol.oldest_birth, frame_counter_display);

            ImGui::PushID(static_cast<int>(mi + 10000));

            // Color dot: blue for molecules, stability color for single atoms
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec4 dot_col;
            if (is_molecule) {
                dot_col = ImVec4(0.4f, 0.65f, 1.0f, 1.0f);  // bond blue
            } else {
                auto& elem = element_list[mol.atom_indices[0]];
                const IsotopeDecayEntry* decay = elem.is_anti ? nullptr : lookup_isotope_decay(elem.Z, elem.N);
                float hl = 0.0f;
                NuclearDecayMode dmode = NDECAY_NONE;
                if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
                else if (!elem.is_anti) { dmode = general_stability_rule(elem.Z, elem.N, hl); }

                if (elem.is_anti)             dot_col = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                else if (dmode == NDECAY_NONE) dot_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
                else if (hl > 7200.0f)        dot_col = ImVec4(0.9f, 0.9f, 0.5f, 1.0f);
                else if (hl > 60.0f)          dot_col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
                else                          dot_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            }
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 4.0f,
                ImGui::ColorConvertFloat4ToU32(dot_col));

            // Selectable row (no cursor shift — Selectable spans full width)
            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
            bool is_selected_mol = (molecule_card_atom_rep == static_cast<int32_t>(first_rep));
            bool is_selected_elem = !is_molecule && (element_card_nucleus_rep == static_cast<int32_t>(first_rep));
            char row_text[192];
            snprintf(row_text, sizeof(row_text), "  %-14s %4s %10s %6s",
                     formula.c_str(), charge_str, energy_str, age_str);

            if (ImGui::Selectable(row_text, is_selected_mol || is_selected_elem,
                                  ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                if (is_molecule) {
                    molecule_card_atom_rep = static_cast<int32_t>(first_rep);
                    element_card_nucleus_rep = -1;  // close atom card
                } else {
                    element_card_nucleus_rep = static_cast<int32_t>(first_rep);
                    molecule_card_atom_rep = -1;  // close molecule card
                }
                navigate_to_particle = static_cast<int32_t>(first_rep);
            }

            // Tooltip
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (is_molecule) {
                    ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "Molecule: %s", formula.c_str());
                    ImGui::Text("Atoms: %d  Charge: %s", static_cast<int>(mol.atom_indices.size()), charge_str);
                    ImGui::Separator();
                    for (uint32_t ei : mol.atom_indices) {
                        auto& elem = element_list[ei];
                        const char* sym = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[elem.Z] : "?";
                        ImGui::Text("  %s-%d (Z=%d N=%d)", sym, elem.Z + elem.N, elem.Z, elem.N);
                    }
                } else {
                    auto& elem = element_list[mol.atom_indices[0]];
                    int Z = elem.Z;
                    int A = Z + elem.N;
                    const char* sym = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?";
                    const char* name = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "Unknown";
                    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1.0f), "%s-%d %s", sym, A, name);
                    const char* lep = elem.is_anti ? "e+" : "e-";
                    ImGui::Text("Z=%d  N=%d  %s=%d", Z, elem.N, lep, elem.electrons);
                }
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    } else {
        // Fallback: original atom-by-atom display (no bonds)
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%-6s %-12s %3s %3s %10s %6s",
                           "Sym", "Name", "A", "Q", "Energy", "Age");
        ImGui::Separator();

        ImGui::BeginChild("##ElemListScroll", ImVec2(0, 0), false);

        for (size_t i = 0; i < element_list.size(); ++i) {
            auto& elem = element_list[i];
            int Z = elem.Z;
            int A = elem.Z + elem.N;
            int net_charge = Z - elem.electrons;

            const char* sym_base = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?";
            const char* name_base = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "Unknown";

            char sym[16], name[64];
            if (elem.is_anti) {
                snprintf(sym, sizeof(sym), "\xc4\x80%s", sym_base);
                snprintf(name, sizeof(name), "Anti-%s", name_base);
            } else {
                snprintf(sym, sizeof(sym), "%s", sym_base);
                snprintf(name, sizeof(name), "%s", name_base);
            }

            const IsotopeDecayEntry* decay = elem.is_anti ? nullptr : lookup_isotope_decay(Z, elem.N);
            float hl = 0.0f;
            NuclearDecayMode dmode = NDECAY_NONE;
            if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
            else if (!elem.is_anti) { dmode = general_stability_rule(Z, elem.N, hl); }

            ImVec4 stab_col;
            if (elem.is_anti)               stab_col = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
            else if (dmode == NDECAY_NONE)   stab_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
            else if (hl > 7200.0f)          stab_col = ImVec4(0.9f, 0.9f, 0.5f, 1.0f);
            else if (hl > 60.0f)            stab_col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
            else                            stab_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

            char charge_str[16];
            if (net_charge == 0) snprintf(charge_str, sizeof(charge_str), "0");
            else if (net_charge > 0) snprintf(charge_str, sizeof(charge_str), "+%d", net_charge);
            else snprintf(charge_str, sizeof(charge_str), "%d", net_charge);

            bool is_selected = (element_card_nucleus_rep == static_cast<int32_t>(elem.rep));
            ImGui::PushID(static_cast<int>(i));

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 4.0f,
                ImGui::ColorConvertFloat4ToU32(stab_col));

            char energy_str[32], age_str[16];
            format_energy_str(energy_str, sizeof(energy_str), elem.energy_MeV);
            format_age_str(age_str, sizeof(age_str), elem.oldest_birth, frame_counter_display);

            char row_text[192];
            snprintf(row_text, sizeof(row_text), "  %-4s %-4s-%-3d  %-10s %3s %10s %6s",
                     sym, sym, A, name, charge_str, energy_str, age_str);

            if (ImGui::Selectable(row_text, is_selected, ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                element_card_nucleus_rep = static_cast<int32_t>(elem.rep);
                navigate_to_particle = static_cast<int32_t>(elem.rep);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(elem.is_anti ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f) : ImVec4(0.9f, 0.85f, 0.6f, 1.0f),
                                   "%s-%d%s", sym, A, elem.is_anti ? " (antimatter)" : "");
                const char* lep = elem.is_anti ? "e+" : "e-";
                ImGui::Text("Z=%d  N=%d  %s=%d", Z, elem.N, lep, elem.electrons);
                if (elem.is_anti) {
                    ImGui::TextColored(stab_col, "Antimatter");
                } else if (dmode == NDECAY_NONE) {
                    ImGui::TextColored(stab_col, "Stable");
                } else {
                    const char* mode_str = "";
                    switch (dmode) {
                        case NDECAY_ALPHA: mode_str = "alpha"; break;
                        case NDECAY_BETA_MINUS: mode_str = "beta-"; break;
                        case NDECAY_BETA_PLUS: mode_str = "beta+"; break;
                        case NDECAY_NEUTRON_EMISSION: mode_str = "n-emit"; break;
                        case NDECAY_PROTON_EMISSION: mode_str = "p-emit"; break;
                        default: break;
                    }
                    ImGui::TextColored(stab_col, "Unstable (%s, t1/2=%.0f frames)", mode_str, hl);
                }
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Particle List Window ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_particle_list(const Particles& particles) {
    if (!show_particle_list || active_particle_display == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 460.0f;
    float max_h = io.DisplaySize.y - 120.0f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - win_w * 0.5f,
                                    io.DisplaySize.y * 0.5f - max_h * 0.35f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(win_w, max_h * 0.7f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 150), ImVec2(580, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.03f, 0.06f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.05f, 0.10f, 0.18f, 0.95f));

    char ptitle[64];
    snprintf(ptitle, sizeof(ptitle), "Particles (%u)###ParticleList", active_particle_display);

    if (!ImGui::Begin(ptitle, &show_particle_list)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Column headers
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), " %-4s %-12s %12s  %10s %6s",
                       "ID", "Type", "Speed", "Energy", "Age");
    ImGui::Separator();

    // Scrollable list — grouped by type
    ImGui::BeginChild("##PartListScroll", ImVec2(0, 0), false);

    uint32_t n = static_cast<uint32_t>(particles.types.size());

    // Iterate by type group for organization
    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        if (type_counts_display[t] == 0) continue;

        // Type group header
        const char* tname = (t < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_NAMES[t] : "Unknown";
        ImVec4 tcol = (t < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_UI_COLORS[t] : ImVec4(1,1,1,1);

        ImGui::PushID(static_cast<int>(t + 1000));
        bool header_open = ImGui::TreeNodeEx(tname,
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%u)", tname, type_counts_display[t]);

        if (header_open) {
            float m0 = rest_mass_MeV(t);
            bool massless = (m0 < 0.001f);

            for (uint32_t i = 0; i < n; ++i) {
                if (particles.types[i] != t) continue;
                if (readback_energies_ptr && readback_energies_ptr[i] < 0.01f) continue;

                // Compute kinetic energy
                float KE_MeV = 0.0f;
                float speed = 0.0f;
                if (readback_velocities && i < readback_count) {
                    speed = glm::length(readback_velocities[i]);
                    float beta = std::min(speed / C_SIM, 0.9999f);
                    if (massless) {
                        KE_MeV = beta * 1.0f;  // all kinetic for massless
                    } else {
                        float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                        KE_MeV = (gamma - 1.0f) * m0;
                    }
                }

                char energy_str[32];
                fmt_energy_ev(energy_str, sizeof(energy_str), KE_MeV);

                // Speed in real-world units
                char speed_str[32];
                fmt_speed(speed_str, sizeof(speed_str), speed);

                bool is_selected = (selected_particle_idx == static_cast<int32_t>(i));
                ImGui::PushID(static_cast<int>(i));

                // Color dot
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 3.0f,
                    ImGui::ColorConvertFloat4ToU32(tcol));

                // Format age
                char age_str[16];
                if (i < static_cast<uint32_t>(particles.birth_frames.size())) {
                    float age_sec = (frame_counter_display - particles.birth_frames[i]) / 60.0f;
                    if (age_sec < 60.0f)
                        snprintf(age_str, sizeof(age_str), "%.0fs", age_sec);
                    else
                        snprintf(age_str, sizeof(age_str), "%.1fm", age_sec / 60.0f);
                } else {
                    snprintf(age_str, sizeof(age_str), "-");
                }

                char row[200];
                snprintf(row, sizeof(row), "  #%-5u %-10s %12s  %10s %6s",
                         i, tname, speed_str, energy_str, age_str);

                if (ImGui::Selectable(row, is_selected, ImGuiSelectableFlags_None, ImVec2(0, 20))) {
                    selected_particle_idx = static_cast<int32_t>(i);
                    navigate_to_particle = static_cast<int32_t>(i);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(tcol, "%s #%u", tname, i);
                    ImGui::Text("Speed: %s  Energy: %s  Age: %s", speed_str, energy_str, age_str);
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect & navigate");
                    ImGui::EndTooltip();
                }

                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor(3);
}

void PhysicsInterface::push_notification(const char* text, ImVec4 color) {
    if (static_cast<int>(notifications.size()) >= NOTIFY_MAX)
        notifications.erase(notifications.begin());  // drop oldest
    notifications.push_back({std::string(text), color, NOTIFY_DURATION});
}

void PhysicsInterface::push_decay_event(const char* desc, DecayEventType type, ImVec4 color) {
    push_decay_event(desc, type, color, std::string());
}

void PhysicsInterface::push_decay_event(const char* desc, DecayEventType type, ImVec4 color, const std::string& details) {
    if (static_cast<int>(decay_log.size()) >= DECAY_LOG_MAX) {
        decay_log.erase(decay_log.begin());
        if (expanded_event_idx > 0) expanded_event_idx--;
        else if (expanded_event_idx == 0) expanded_event_idx = -1;
    }
    decay_log.push_back({std::string(desc), details, type, color, frame_counter_display, std::time(nullptr)});
}

void PhysicsInterface::draw_notifications() {
    if (notifications.empty()) return;

    float dt = ImGui::GetIO().DeltaTime;
    ImGuiIO& io = ImGui::GetIO();

    // Tick timers and remove expired
    for (auto& n : notifications) n.timer -= dt;
    notifications.erase(
        std::remove_if(notifications.begin(), notifications.end(),
                        [](const Notification& n) { return n.timer <= 0.0f; }),
        notifications.end());

    if (notifications.empty()) return;

    // Draw stacked cards from top-right, growing downward
    float card_w = 260.0f;
    float card_pad = 4.0f;
    float start_x = io.DisplaySize.x - card_w - 10.0f;
    float start_y = 10.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    for (int i = 0; i < static_cast<int>(notifications.size()); ++i) {
        auto& n = notifications[i];
        float alpha = (n.timer < 1.0f) ? n.timer : 1.0f;  // fade out in last second

        ImGui::SetNextWindowPos(ImVec2(start_x, start_y));
        ImGui::SetNextWindowSize(ImVec2(card_w, 0));
        ImGui::SetNextWindowBgAlpha(0.85f * alpha);

        char wid[32];
        snprintf(wid, sizeof(wid), "##notify%d", i);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(n.color.x * 0.5f, n.color.y * 0.5f, n.color.z * 0.5f, alpha * 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        if (ImGui::Begin(wid, nullptr, flags)) {
            ImVec4 tc = n.color;
            tc.w = alpha;
            ImGui::TextColored(tc, "%s", n.text.c_str());
            start_y += ImGui::GetWindowHeight() + card_pad;
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Force Object Inspection Panel (Right Side) ──────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_force_object_panel(ForceObject* objects) {
    if (selected_force_obj_idx < 0) return;
    int idx = selected_force_obj_idx;
    if (idx >= static_cast<int>(MAX_FORCE_OBJECTS) || !objects[idx].active) {
        selected_force_obj_idx = -1;
        return;
    }

    ForceObject& obj = objects[idx];

    static const char* fo_type_names[] = { "EM Field", "Strong Force", "Weak Force", "Gravity Well", "Heat Source", "Mirror" };
    static const ImVec4 fo_type_colors[] = {
        ImVec4(0.3f, 0.5f, 1.0f, 1.0f),
        ImVec4(0.3f, 0.9f, 0.4f, 1.0f),
        ImVec4(0.7f, 0.3f, 0.9f, 1.0f),
        ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
        ImVec4(1.0f, 0.4f, 0.2f, 1.0f),
        ImVec4(0.7f, 0.7f, 0.8f, 1.0f),
    };

    const char* type_name = (obj.force_type < FORCE_OBJ_COUNT) ? fo_type_names[obj.force_type] : "Unknown";
    ImVec4 type_color = (obj.force_type < FORCE_OBJ_COUNT) ? fo_type_colors[obj.force_type] : ImVec4(1,1,1,1);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, 10));
    ImGui::SetNextWindowSize(ImVec2(250, 0));

    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##ForceObjPanel", nullptr, panel_flags)) {
        // Header
        ImGui::TextColored(type_color, "%s", type_name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "#%d", idx);
        ImGui::Separator();

        float col_w = 90.0f;
        if (obj.force_type == FORCE_OBJ_MIRROR) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Endpoint 1");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj.x, obj.y);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Endpoint 2");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj._pad0, obj._pad1);

            ImGui::Spacing();
            ImGui::TextColored(type_color, "Elasticity");
            ImGui::SliderFloat("##fo_str", &obj.strength, 0.0f, 1.0f, "%.2f");

            ImGui::TextColored(type_color, "Thickness");
            ImGui::SliderFloat("##fo_rad", &obj.radius, 1.0f, 20.0f, "%.1f");
        } else {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Position");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj.x, obj.y);

            ImGui::Spacing();
            ImGui::TextColored(type_color, "Strength");
            ImGui::SliderFloat("##fo_str", &obj.strength, 0.1f, 10.0f, "%.2f");

            ImGui::TextColored(type_color, "Radius");
            ImGui::SliderFloat("##fo_rad", &obj.radius, 10.0f, 200.0f, "%.0f");
        }

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (force_obj_move_mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
            if (ImGui::Button("Moving...", ImVec2(72, 26))) {
                force_obj_move_mode = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
        } else {
            if (ImGui::Button("Move", ImVec2(72, 26))) {
                force_obj_move_mode = true;
            }
        }
        if (!force_obj_move_mode) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
            if (ImGui::Button("Delete", ImVec2(72, 26))) {
                obj.active = 0;
                selected_force_obj_idx = -1;
                force_obj_move_mode = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(72, 26))) {
                selected_force_obj_idx = -1;
                force_obj_move_mode = false;
            }
        }
    }
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Save / Load Dialog ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// ── File browser helpers ─────────────────────────────────────────────────────

static std::string format_file_size(uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

void PhysicsInterface::refresh_browse_entries() {
    browse_entries.clear();
    browse_selected_idx = -1;

    std::error_code ec;
    if (!fs::is_directory(browse_current_dir, ec)) {
        browse_current_dir = fs::current_path(ec).string();
    }

    // Collect directories and matching files
    std::string filter_ext = show_molecule_import_dialog ? ".ppmol"
                           : show_import_dialog ? ".ppel" : ".ppsg";
    std::vector<BrowseEntry> dirs, files;
    for (auto& entry : fs::directory_iterator(browse_current_dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // skip hidden

        if (entry.is_directory(ec)) {
            dirs.push_back({ name, true, 0 });
        } else if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            if (ext == filter_ext) {
                uintmax_t sz = entry.file_size(ec);
                files.push_back({ name, false, sz });
            }
        }
    }

    // Sort alphabetically
    auto cmp = [](const BrowseEntry& a, const BrowseEntry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Dirs first, then files
    browse_entries.insert(browse_entries.end(), dirs.begin(), dirs.end());
    browse_entries.insert(browse_entries.end(), files.begin(), files.end());

    browse_needs_refresh = false;
}

void PhysicsInterface::draw_save_load_dialog() {
    // Initialize browse directory on first open
    if (browse_current_dir.empty()) {
        std::error_code ec;
        fs::path saves_dir = fs::current_path(ec) / "saves";
        if (fs::is_directory(saves_dir, ec))
            browse_current_dir = saves_dir.string();
        else
            browse_current_dir = fs::current_path(ec).string();
    }

    if (browse_needs_refresh)
        refresh_browse_entries();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImVec2 size(520, 480);

    ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    const char* title = show_molecule_import_dialog ? "Import Molecule###SaveLoad"
                      : show_import_dialog ? "Import Element###SaveLoad"
                      : show_save_dialog  ? "Save Simulation###SaveLoad"
                                          : "Load Simulation###SaveLoad";
    bool open = true;

    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        // ── Path bar ─────────────────────────────────────────────────────
        ImGui::Text("Path:");
        ImGui::SameLine();

        // Sync path buffer
        snprintf(browse_path_buf, sizeof(browse_path_buf), "%s", browse_current_dir.c_str());
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("##path", browse_path_buf, sizeof(browse_path_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec;
            if (fs::is_directory(browse_path_buf, ec)) {
                browse_current_dir = browse_path_buf;
                browse_needs_refresh = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            fs::path parent = fs::path(browse_current_dir).parent_path();
            if (!parent.empty() && parent != browse_current_dir) {
                browse_current_dir = parent.string();
                browse_needs_refresh = true;
                refresh_browse_entries();
            }
        }

        ImGui::Separator();

        // ── File list ────────────────────────────────────────────────────
        float list_height = ImGui::GetContentRegionAvail().y - 90.0f;
        if (ImGui::BeginChild("##FileList", ImVec2(0, list_height), ImGuiChildFlags_Border)) {
            for (int i = 0; i < (int)browse_entries.size(); i++) {
                const auto& entry = browse_entries[i];
                bool selected = (browse_selected_idx == i);

                // Build display label
                char label[320];
                if (entry.is_dir) {
                    snprintf(label, sizeof(label), "[DIR]  %s/", entry.name.c_str());
                } else {
                    std::string sz = format_file_size(entry.size);
                    snprintf(label, sizeof(label), "  %s", entry.name.c_str());

                    // We'll draw the size right-aligned after the selectable
                }

                ImGui::PushID(i);
                if (entry.is_dir) {
                    // Directory — colored
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
                    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                        // Navigate into directory
                        fs::path new_dir = fs::path(browse_current_dir) / entry.name;
                        browse_current_dir = new_dir.string();
                        browse_needs_refresh = true;
                        refresh_browse_entries();
                    }
                    ImGui::PopStyleColor();
                } else {
                    // File
                    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                        browse_selected_idx = i;
                        snprintf(browse_filename, sizeof(browse_filename), "%s", entry.name.c_str());

                        // Double-click to load/import
                        if (!show_save_dialog && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            fs::path full = fs::path(browse_current_dir) / entry.name;
                            snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                            if (show_molecule_import_dialog) {
                                request_molecule_import = true;
                                show_molecule_import_dialog = false;
                            } else if (show_import_dialog) {
                                request_import = true;
                                show_import_dialog = false;
                            } else {
                                request_load = true;
                                show_load_dialog = false;
                            }
                            show_save_dialog = false;
                        }
                    }

                    // Right-align file size
                    std::string sz = format_file_size(entry.size);
                    float text_w = ImGui::CalcTextSize(sz.c_str()).x;
                    float avail = ImGui::GetContentRegionAvail().x;
                    ImGui::SameLine(avail - text_w + ImGui::GetCursorPosX() - 8);
                    ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", sz.c_str());
                }
                ImGui::PopID();
            }

            if (browse_entries.empty()) {
                const char* ext_hint = show_molecule_import_dialog ? ".ppmol"
                                     : show_import_dialog ? ".ppel" : ".ppsg";
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.7f), "  No %s files in this directory", ext_hint);
            }
        }
        ImGui::EndChild();

        // ── Filename input + action buttons ──────────────────────────────
        ImGui::Separator();

        if (show_save_dialog) {
            ImGui::Text("File:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-180);
            ImGui::InputText("##savename", browse_filename, sizeof(browse_filename));
        }

        // Status message
        if (save_load_msg_timer > 0.0f) {
            ImVec4 color = (save_load_message[0] == 'S' || save_load_message[0] == 'L')
                ? ImVec4(0.2f, 0.9f, 0.4f, std::min(1.0f, save_load_msg_timer))
                : ImVec4(0.9f, 0.3f, 0.3f, std::min(1.0f, save_load_msg_timer));
            if (show_save_dialog) ImGui::SameLine();
            ImGui::TextColored(color, "%s", save_load_message);
        }

        ImGui::Spacing();

        float btn_w = 80.0f;
        if (show_save_dialog) {
            if (ImGui::Button("Save", ImVec2(btn_w, 28))) {
                // Ensure .ppsg extension
                std::string fname = browse_filename;
                if (fname.size() < 5 || fname.substr(fname.size() - 5) != ".ppsg")
                    fname += ".ppsg";
                // Create saves dir if saving to it
                std::error_code ec;
                fs::create_directories(browse_current_dir, ec);
                fs::path full = fs::path(browse_current_dir) / fname;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_save = true;
                show_save_dialog = false;
                browse_needs_refresh = true;
            }
        } else if (show_molecule_import_dialog) {
            bool can_import = (browse_selected_idx >= 0 &&
                               browse_selected_idx < (int)browse_entries.size() &&
                               !browse_entries[browse_selected_idx].is_dir);
            if (!can_import) ImGui::BeginDisabled();
            if (ImGui::Button("Import", ImVec2(btn_w, 28))) {
                fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_molecule_import = true;
                show_molecule_import_dialog = false;
                browse_needs_refresh = true;
            }
            if (!can_import) ImGui::EndDisabled();
        } else if (show_import_dialog) {
            bool can_import = (browse_selected_idx >= 0 &&
                               browse_selected_idx < (int)browse_entries.size() &&
                               !browse_entries[browse_selected_idx].is_dir);
            if (!can_import) ImGui::BeginDisabled();
            if (ImGui::Button("Import", ImVec2(btn_w, 28))) {
                fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_import = true;
                show_import_dialog = false;
                browse_needs_refresh = true;
            }
            if (!can_import) ImGui::EndDisabled();
        } else {
            bool can_load = (browse_selected_idx >= 0 &&
                             browse_selected_idx < (int)browse_entries.size() &&
                             !browse_entries[browse_selected_idx].is_dir);
            if (!can_load) ImGui::BeginDisabled();
            if (ImGui::Button("Load", ImVec2(btn_w, 28))) {
                fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_load = true;
                show_load_dialog = false;
                browse_needs_refresh = true;
            }
            if (!can_load) ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btn_w, 28))) {
            show_save_dialog = false;
            show_load_dialog = false;
            show_import_dialog = false;
            show_molecule_import_dialog = false;
        }

        // Escape to close
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_save_dialog = false;
            show_load_dialog = false;
            show_import_dialog = false;
            show_molecule_import_dialog = false;
        }
    }

    if (!open) {
        show_save_dialog = false;
        show_load_dialog = false;
        show_import_dialog = false;
        show_molecule_import_dialog = false;
    }

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Measurement Overlays ────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_measurement_overlays(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // ── Thermometer probes ───────────────────────────────────────────────
    for (auto& probe : thermo_probes) {
        ImVec2 center = w2s(probe.world_pos);
        float screen_r = probe.radius * cfg.current_camera_zoom * scx;
        if (center.x < -screen_r || center.x > ww + screen_r ||
            center.y < -screen_r || center.y > wh + screen_r) continue;

        ImU32 ring_col = IM_COL32(255, 150, 50, 120);
        fg->AddCircle(center, screen_r, ring_col, 48, 1.5f);

        // Label
        char buf[64];
        snprintf(buf, sizeof(buf), "T: %.0f K (%u)", probe.local_temp, probe.local_count);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float lx = center.x - ts.x * 0.5f, ly = center.y - screen_r - 20.0f;
        fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(lx, ly), IM_COL32(255, 180, 80, 255), buf);
    }

    // ── Velocity meters ──────────────────────────────────────────────────
    if (readback_positions_ptr && readback_velocities && readback_energies_ptr) {
        for (auto& vm : velocity_meters) {
            if (!vm.active || vm.particle_idx < 0 ||
                static_cast<uint32_t>(vm.particle_idx) >= readback_count) continue;
            uint32_t pi = static_cast<uint32_t>(vm.particle_idx);
            if (readback_energies_ptr[pi] <= 0.0f) { vm.active = false; continue; }

            glm::vec2 pos = readback_positions_ptr[pi];
            glm::vec2 vel = readback_velocities[pi];
            ImVec2 scr = w2s(pos);

            float speed = glm::length(vel);

            // Arrow
            if (speed > 0.1f) {
                glm::vec2 dir = vel / speed;
                float arrow_len = std::min(speed * 0.5f, 80.0f) * cfg.current_camera_zoom * scx;
                ImVec2 tip = ImVec2(scr.x + dir.x * arrow_len, scr.y + dir.y * arrow_len);
                fg->AddLine(scr, tip, IM_COL32(80, 255, 130, 200), 2.0f);
                // Arrowhead
                glm::vec2 perp(-dir.y, dir.x);
                float ah = 6.0f;
                fg->AddTriangleFilled(
                    tip,
                    ImVec2(tip.x - dir.x * ah + perp.x * ah * 0.5f,
                           tip.y - dir.y * ah + perp.y * ah * 0.5f),
                    ImVec2(tip.x - dir.x * ah - perp.x * ah * 0.5f,
                           tip.y - dir.y * ah - perp.y * ah * 0.5f),
                    IM_COL32(80, 255, 130, 200));
            }

            // Label — real-world speed
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), speed);
            char buf[64];
            snprintf(buf, sizeof(buf), "v=%s", spd_buf);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            float lx = scr.x + 10, ly = scr.y - 20;
            fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                              IM_COL32(0, 0, 0, 180), 3.0f);
            fg->AddText(ImVec2(lx, ly), IM_COL32(80, 255, 130, 255), buf);
        }
    }

    // ── Distance rulers ──────────────────────────────────────────────────
    for (auto& ruler : distance_rulers) {
        ImVec2 a = w2s(ruler.point_a);
        ImVec2 b = w2s(ruler.point_b);

        fg->AddLine(a, b, IM_COL32(230, 230, 80, 200), 1.5f);

        // Tick marks at endpoints
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
            float nx = -dy / len * 6.0f, ny = dx / len * 6.0f;
            fg->AddLine(ImVec2(a.x + nx, a.y + ny), ImVec2(a.x - nx, a.y - ny),
                        IM_COL32(230, 230, 80, 200), 1.5f);
            fg->AddLine(ImVec2(b.x + nx, b.y + ny), ImVec2(b.x - nx, b.y - ny),
                        IM_COL32(230, 230, 80, 200), 1.5f);
        }

        // Midpoint label (display in nanometers)
        float nm = ruler.distance * WORLD_TO_NM;
        char buf[32];
        if (nm < 0.01f)
            snprintf(buf, sizeof(buf), "%.2f pm", nm * 1000.0f);
        else if (nm < 1.0f)
            snprintf(buf, sizeof(buf), "%.3f nm", nm);
        else
            snprintf(buf, sizeof(buf), "%.2f nm", nm);
        ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        fg->AddRectFilled(ImVec2(mid.x - ts.x * 0.5f - 4, mid.y - ts.y * 0.5f - 2),
                          ImVec2(mid.x + ts.x * 0.5f + 4, mid.y + ts.y * 0.5f + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f),
                    IM_COL32(230, 230, 80, 255), buf);
    }

    // ── Density counters ─────────────────────────────────────────────────
    for (auto& dc : density_counters) {
        ImVec2 center = w2s(dc.world_pos);
        float screen_r = dc.radius * cfg.current_camera_zoom * scx;
        if (center.x < -screen_r || center.x > ww + screen_r ||
            center.y < -screen_r || center.y > wh + screen_r) continue;

        // Dashed circle
        ImU32 ring_col = IM_COL32(150, 80, 255, 120);
        int segs = 48;
        for (int s = 0; s < segs; s += 2) {
            float a0 = (float)s / segs * 6.2831853f;
            float a1 = (float)(s + 1) / segs * 6.2831853f;
            fg->AddLine(ImVec2(center.x + std::cos(a0) * screen_r,
                               center.y + std::sin(a0) * screen_r),
                        ImVec2(center.x + std::cos(a1) * screen_r,
                               center.y + std::sin(a1) * screen_r),
                        ring_col, 1.5f);
        }

        // Label
        char buf[64];
        snprintf(buf, sizeof(buf), "n=%u  \xcf\x81=%.4f", dc.count, dc.density);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float lx = center.x - ts.x * 0.5f, ly = center.y - screen_r - 20.0f;
        fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(lx, ly), IM_COL32(180, 130, 255, 255), buf);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Energy Heatmap ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_energy_heatmap(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    float cell_w = static_cast<float>(REGION_W) / VIS_GRID_W;
    float cell_h = static_cast<float>(REGION_H) / VIS_GRID_H;

    // Find max avg energy for normalization
    float max_avg = 0.0f;
    for (uint32_t i = 0; i < VIS_GRID_CELLS; ++i) {
        if (vis_grid.count[i] > 0) {
            float avg = vis_grid.energy[i] / vis_grid.count[i];
            if (avg > max_avg) max_avg = avg;
        }
    }
    if (max_avg < 0.001f) return;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    uint8_t alpha = static_cast<uint8_t>(heatmap_opacity * 255);

    for (uint32_t gy = 0; gy < VIS_GRID_H; ++gy) {
        for (uint32_t gx = 0; gx < VIS_GRID_W; ++gx) {
            uint32_t idx = gy * VIS_GRID_W + gx;
            if (vis_grid.count[idx] == 0) continue;

            float t = (vis_grid.energy[idx] / vis_grid.count[idx]) / max_avg;
            t = std::clamp(t, 0.0f, 1.0f);

            // Blue → Cyan → Green → Yellow → Red
            uint8_t r, g, b;
            if (t < 0.25f) {
                float s = t / 0.25f;
                r = 0; g = static_cast<uint8_t>(s * 255); b = 255;
            } else if (t < 0.5f) {
                float s = (t - 0.25f) / 0.25f;
                r = 0; g = 255; b = static_cast<uint8_t>((1.0f - s) * 255);
            } else if (t < 0.75f) {
                float s = (t - 0.5f) / 0.25f;
                r = static_cast<uint8_t>(s * 255); g = 255; b = 0;
            } else {
                float s = (t - 0.75f) / 0.25f;
                r = 255; g = static_cast<uint8_t>((1.0f - s) * 255); b = 0;
            }

            glm::vec2 tl(gx * cell_w, gy * cell_h);
            glm::vec2 br((gx + 1) * cell_w, (gy + 1) * cell_h);
            ImVec2 stl = w2s(tl), sbr = w2s(br);

            fg->AddRectFilled(stl, sbr, IM_COL32(r, g, b, alpha));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Velocity Field ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_velocity_field(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    float cell_w = static_cast<float>(REGION_W) / VIS_GRID_W;
    float cell_h = static_cast<float>(REGION_H) / VIS_GRID_H;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // Find max avg speed for normalization
    float max_speed = 0.0f;
    for (uint32_t i = 0; i < VIS_GRID_CELLS; ++i) {
        if (vis_grid.count[i] > 0) {
            float vx = vis_grid.vel_x[i] / vis_grid.count[i];
            float vy = vis_grid.vel_y[i] / vis_grid.count[i];
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd > max_speed) max_speed = spd;
        }
    }
    if (max_speed < 0.01f) return;

    for (uint32_t gy = 0; gy < VIS_GRID_H; ++gy) {
        for (uint32_t gx = 0; gx < VIS_GRID_W; ++gx) {
            uint32_t idx = gy * VIS_GRID_W + gx;
            if (vis_grid.count[idx] < 2) continue;  // need at least 2 particles

            float vx = vis_grid.vel_x[idx] / vis_grid.count[idx];
            float vy = vis_grid.vel_y[idx] / vis_grid.count[idx];
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd < 0.01f) continue;

            // Cell center in world space
            glm::vec2 cell_center((gx + 0.5f) * cell_w, (gy + 0.5f) * cell_h);
            ImVec2 origin = w2s(cell_center);

            // Arrow length proportional to speed, scaled by zoom and user scale
            float norm_spd = spd / max_speed;
            float arrow_len = norm_spd * cell_w * 0.4f * cfg.current_camera_zoom * scx * velocity_field_scale;
            arrow_len = std::clamp(arrow_len, 3.0f, 40.0f);

            float dx = vx / spd, dy = vy / spd;
            ImVec2 tip(origin.x + dx * arrow_len, origin.y + dy * arrow_len);

            uint8_t a = static_cast<uint8_t>(100 + norm_spd * 155);
            fg->AddLine(origin, tip, IM_COL32(200, 200, 255, a), 1.2f);

            // Small arrowhead
            float ah = 3.0f;
            float px = -dy, py = dx;
            fg->AddTriangleFilled(
                tip,
                ImVec2(tip.x - dx * ah + px * ah * 0.4f, tip.y - dy * ah + py * ah * 0.4f),
                ImVec2(tip.x - dx * ah - px * ah * 0.4f, tip.y - dy * ah - py * ah * 0.4f),
                IM_COL32(200, 200, 255, a));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Magnetic Field Visualization ─────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_magnetic_field(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    float cell_w = static_cast<float>(REGION_W) / VIS_GRID_W;
    float cell_h = static_cast<float>(REGION_H) / VIS_GRID_H;

    // Find max |Bz| for normalization
    float max_bz = 0.0f;
    for (uint32_t i = 0; i < VIS_GRID_CELLS; ++i) {
        float abz = std::abs(vis_grid.bfield_z[i]);
        if (abz > max_bz) max_bz = abz;
    }
    if (max_bz < 0.0001f) return;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    uint8_t alpha = static_cast<uint8_t>(magnetic_field_opacity * 255);

    for (uint32_t gy = 0; gy < VIS_GRID_H; ++gy) {
        for (uint32_t gx = 0; gx < VIS_GRID_W; ++gx) {
            uint32_t idx = gy * VIS_GRID_W + gx;
            float bz = vis_grid.bfield_z[idx];
            if (std::abs(bz) < max_bz * 0.01f) continue;

            float t = bz / max_bz;  // -1 to +1
            t = std::clamp(t, -1.0f, 1.0f);

            // Diverging color map: Blue (B<0) → Black (B=0) → Red (B>0)
            uint8_t r, g, b;
            if (t > 0.0f) {
                // Positive Bz: black → red → yellow
                if (t < 0.5f) {
                    float s = t / 0.5f;
                    r = static_cast<uint8_t>(s * 255);
                    g = 0;
                    b = 0;
                } else {
                    float s = (t - 0.5f) / 0.5f;
                    r = 255;
                    g = static_cast<uint8_t>(s * 200);
                    b = 0;
                }
            } else {
                // Negative Bz: black → blue → cyan
                float at = -t;
                if (at < 0.5f) {
                    float s = at / 0.5f;
                    r = 0;
                    g = 0;
                    b = static_cast<uint8_t>(s * 255);
                } else {
                    float s = (at - 0.5f) / 0.5f;
                    r = 0;
                    g = static_cast<uint8_t>(s * 200);
                    b = 255;
                }
            }

            // Scale alpha by magnitude
            uint8_t cell_alpha = static_cast<uint8_t>(alpha * std::abs(t));
            if (cell_alpha < 4) continue;

            glm::vec2 tl(gx * cell_w, gy * cell_h);
            glm::vec2 br((gx + 1) * cell_w, (gy + 1) * cell_h);
            ImVec2 stl = w2s(tl), sbr = w2s(br);

            fg->AddRectFilled(stl, sbr, IM_COL32(r, g, b, cell_alpha));
        }
    }

    // Legend — bottom-left corner
    ImVec2 legend_pos(10, wh - 30);
    fg->AddText(legend_pos, IM_COL32(100, 100, 255, 200), "B<0");
    fg->AddText(ImVec2(legend_pos.x + 40, legend_pos.y), IM_COL32(180, 180, 180, 200), "|");
    fg->AddText(ImVec2(legend_pos.x + 52, legend_pos.y), IM_COL32(255, 100, 100, 200), "B>0");
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Trajectory Traces ───────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_trajectory_traces(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    for (uint32_t pi = 0; pi < static_cast<uint32_t>(trajectory_history.size()); ++pi) {
        auto& hist = trajectory_history[pi];
        if (hist.size() < 2) continue;

        // Get particle type color
        ImVec4 col(0.7f, 0.7f, 0.7f, 1.0f);
        if (pi < readback_count && readback_positions_ptr) {
            // Use type color if we have it via type_counts_display context
            // We need the type — check if readback_energies_ptr is valid
            // For simplicity, use a generic fading white with slight type-based tint
        }

        int n = static_cast<int>(hist.size());
        for (int i = 1; i < n; ++i) {
            float alpha_frac = static_cast<float>(i) / static_cast<float>(n);
            uint8_t a = static_cast<uint8_t>(alpha_frac * 160);

            ImVec2 p0 = w2s(hist[i - 1]);
            ImVec2 p1 = w2s(hist[i]);

            // Skip if both off screen
            if ((p0.x < -20 || p0.x > ww + 20 || p0.y < -20 || p0.y > wh + 20) &&
                (p1.x < -20 || p1.x > ww + 20 || p1.y < -20 || p1.y > wh + 20))
                continue;

            // Skip if the segment is too long (toroidal wrap artifact)
            float sdx = p1.x - p0.x, sdy = p1.y - p0.y;
            if (sdx * sdx + sdy * sdy > (ww * 0.3f) * (ww * 0.3f)) continue;

            fg->AddLine(p0, p1, IM_COL32(180, 200, 255, a), 1.0f);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Force Vectors Overlay ───────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_force_vectors_overlay(const SimConfig& cfg) {
    if (selected_particle_idx < 0 || force_contributions.empty()) return;
    if (!readback_positions_ptr || static_cast<uint32_t>(selected_particle_idx) >= readback_count) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    glm::vec2 pos = readback_positions_ptr[selected_particle_idx];
    glm::vec2 scr_v = glm::vec2(ww, wh) * 0.5f + (pos - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
    ImVec2 origin(scr_v.x, scr_v.y);

    // Find max magnitude for scaling
    float max_mag = 0.0f;
    for (auto& fc : force_contributions)
        if (fc.magnitude > max_mag) max_mag = fc.magnitude;
    if (max_mag < 0.0001f) return;

    for (auto& fc : force_contributions) {
        float norm = std::log(1.0f + fc.magnitude) / std::log(1.0f + max_mag);
        float arrow_len = norm * 80.0f;
        if (arrow_len < 5.0f) continue;

        ImVec2 tip(origin.x + fc.direction.x * arrow_len,
                   origin.y + fc.direction.y * arrow_len);

        ImU32 col = ImGui::ColorConvertFloat4ToU32(fc.color);
        fg->AddLine(origin, tip, col, 2.5f);

        // Arrowhead
        float dx = fc.direction.x, dy = fc.direction.y;
        float ah = 7.0f;
        float px = -dy, py = dx;
        fg->AddTriangleFilled(
            tip,
            ImVec2(tip.x - dx * ah + px * ah * 0.4f, tip.y - dy * ah + py * ah * 0.4f),
            ImVec2(tip.x - dx * ah - px * ah * 0.4f, tip.y - dy * ah - py * ah * 0.4f),
            col);

        // Label at tip
        ImVec2 ts = ImGui::CalcTextSize(fc.name);
        fg->AddRectFilled(ImVec2(tip.x + 4, tip.y - ts.y * 0.5f - 1),
                          ImVec2(tip.x + 8 + ts.x, tip.y + ts.y * 0.5f + 1),
                          IM_COL32(0, 0, 0, 160), 2.0f);
        fg->AddText(ImVec2(tip.x + 6, tip.y - ts.y * 0.5f), col, fc.name);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Measurement Panel (right side) ──────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_measurement_panel() {
    ImGuiIO& io = ImGui::GetIO();
    float panel_w = 280.0f;
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panel_w - 10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panel_w, 80), ImVec2(panel_w, max_h));
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Measurements", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // ── Thermometer probes ───────────────────────────────────────────────
    if (!thermo_probes.empty() && ImGui::CollapsingHeader("Thermometer Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(thermo_probes.size()); ++i) {
            auto& p = thermo_probes[i];
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Probe %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            ImGui::Text("  T: %.0f K  (%u particles)", p.local_temp, p.local_count);
            ImGui::SliderFloat("Radius", &p.radius, 20.0f, 300.0f, "%.0f");
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            thermo_probes.erase(thermo_probes.begin() + remove_idx);
    }

    // ── Velocity meters ──────────────────────────────────────────────────
    if (!velocity_meters.empty() && ImGui::CollapsingHeader("Velocity Meters", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(velocity_meters.size()); ++i) {
            auto& vm = velocity_meters[i];
            ImGui::PushID(1000 + i);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Particle #%d", vm.particle_idx);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            if (vm.active && readback_velocities && static_cast<uint32_t>(vm.particle_idx) < readback_count) {
                glm::vec2 v = readback_velocities[vm.particle_idx];
                float speed = glm::length(v);
                char spd_buf[32];
                fmt_speed(spd_buf, sizeof(spd_buf), speed);
                ImGui::Text("  Speed: %s", spd_buf);
            }
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            velocity_meters.erase(velocity_meters.begin() + remove_idx);
    }

    // ── Distance rulers ──────────────────────────────────────────────────
    if (!distance_rulers.empty() && ImGui::CollapsingHeader("Distance Rulers", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(distance_rulers.size()); ++i) {
            auto& r = distance_rulers[i];
            ImGui::PushID(2000 + i);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "Ruler %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            float nm = r.distance * WORLD_TO_NM;
            if (nm < 0.01f)
                ImGui::Text("  Distance: %.2f pm", nm * 1000.0f);
            else if (nm < 1.0f)
                ImGui::Text("  Distance: %.3f nm", nm);
            else
                ImGui::Text("  Distance: %.2f nm", nm);
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            distance_rulers.erase(distance_rulers.begin() + remove_idx);
    }

    // ── Density counters ─────────────────────────────────────────────────
    if (!density_counters.empty() && ImGui::CollapsingHeader("Density Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(density_counters.size()); ++i) {
            auto& dc = density_counters[i];
            ImGui::PushID(3000 + i);
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Counter %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            ImGui::Text("  Count: %u  Density: %.4f", dc.count, dc.density);
            ImGui::SliderFloat("Radius", &dc.radius, 20.0f, 300.0f, "%.0f");
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            density_counters.erase(density_counters.begin() + remove_idx);
    }

    // ── Clear All ────────────────────────────────────────────────────────
    ImGui::Spacing();
    if (ImGui::Button("Clear All", ImVec2(-1, 0))) {
        thermo_probes.clear();
        velocity_meters.clear();
        distance_rulers.clear();
        density_counters.clear();
    }

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Atom Grid Overlay ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_atom_grid(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);
    float zoom = cfg.current_camera_zoom;

    // Cell size in screen pixels
    float cell_px = H_ATOM_DIAMETER * zoom * scx;

    // Don't draw if cells are too small to see
    if (cell_px < 4.0f) return;

    uint8_t alpha = static_cast<uint8_t>(std::clamp(atom_grid_opacity * 255.0f, 0.0f, 255.0f));
    if (alpha == 0) return;

    // Brighter lines every 10 cells for readability
    uint8_t alpha_major = static_cast<uint8_t>(std::min(255, static_cast<int>(alpha) * 2));
    ImU32 col_minor = IM_COL32(100, 180, 255, alpha);
    ImU32 col_major = IM_COL32(100, 180, 255, alpha_major);

    // Camera offset: top-left of screen in world coords
    glm::vec2 screen_tl_world = cfg.camera_origin - glm::vec2(ww, wh) * 0.5f / (zoom * glm::vec2(scx, scy));

    // First grid line positions (snap to grid)
    float x_start = std::floor(screen_tl_world.x / H_ATOM_DIAMETER) * H_ATOM_DIAMETER;
    float y_start = std::floor(screen_tl_world.y / H_ATOM_DIAMETER) * H_ATOM_DIAMETER;

    // Grid index offset for major-line calculation
    int ix_off = static_cast<int>(std::floor(screen_tl_world.x / H_ATOM_DIAMETER));
    int iy_off = static_cast<int>(std::floor(screen_tl_world.y / H_ATOM_DIAMETER));

    auto w2s_x = [&](float wx) -> float {
        return ww * 0.5f + (wx - cfg.camera_origin.x) * zoom * scx;
    };
    auto w2s_y = [&](float wy) -> float {
        return wh * 0.5f + (wy - cfg.camera_origin.y) * zoom * scy;
    };

    // Vertical lines
    int line_idx = 0;
    for (float wx = x_start; ; wx += H_ATOM_DIAMETER, ++line_idx) {
        float sx = w2s_x(wx);
        if (sx < -1.0f) continue;
        if (sx > ww + 1.0f) break;
        int grid_ix = ix_off + line_idx;
        ImU32 col = (grid_ix % 10 == 0) ? col_major : col_minor;
        float thick = (grid_ix % 10 == 0) ? 1.0f : 0.5f;
        fg->AddLine(ImVec2(sx, 0), ImVec2(sx, wh), col, thick);
    }

    // Horizontal lines
    line_idx = 0;
    for (float wy = y_start; ; wy += H_ATOM_DIAMETER, ++line_idx) {
        float sy = w2s_y(wy);
        if (sy < -1.0f) continue;
        if (sy > wh + 1.0f) break;
        int grid_iy = iy_off + line_idx;
        ImU32 col = (grid_iy % 10 == 0) ? col_major : col_minor;
        float thick = (grid_iy % 10 == 0) ? 1.0f : 0.5f;
        fg->AddLine(ImVec2(0, sy), ImVec2(ww, sy), col, thick);
    }

    // Scale label at bottom-left
    float nm_per_cell = H_ATOM_DIAMETER * WORLD_TO_NM;
    char label[64];
    snprintf(label, sizeof(label), "1 cell = 1 H atom (%.3f nm)", nm_per_cell);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float lx = 12.0f, ly = wh - 40.0f;
    fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                      IM_COL32(0, 0, 0, 180), 3.0f);
    fg->AddText(ImVec2(lx, ly), IM_COL32(100, 180, 255, 220), label);
}
