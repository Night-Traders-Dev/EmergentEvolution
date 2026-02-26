#include "physics/interface.h"
#include "physics/phys_particles.h"
#include <imgui.h>
#include <cmath>
#include <random>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <omp.h>

namespace fs = std::filesystem;

void PhysicsInterface::init() {
    std::random_device rd;
    seed_value = static_cast<int>(rd() % 100000);
}

// ── Nucleus / atom group templates ───────────────────────────────────────────

// Hydrogen atom: 1 proton + 1 electron
static const SubAtomicSpec H_ATOM[] = {
    { 0, 0, PROTON_TYPE },
    { 20, 0, ELECTRON_TYPE_PHYS },
};

// Deuterium: 1 proton + 1 neutron + 1 electron
static const SubAtomicSpec DEUTERIUM[] = {
    { 0, 0, PROTON_TYPE },
    { 5, 0, NEUTRON_TYPE },
    { 25, 0, ELECTRON_TYPE_PHYS },
};

// Helium-4 atom: 2p + 2n + 2e
static const SubAtomicSpec HE4_ATOM[] = {
    { -2.5f, -2.5f, PROTON_TYPE },
    {  2.5f, -2.5f, NEUTRON_TYPE },
    { -2.5f,  2.5f, NEUTRON_TYPE },
    {  2.5f,  2.5f, PROTON_TYPE },
    { -25, 0, ELECTRON_TYPE_PHYS },
    {  25, 0, ELECTRON_TYPE_PHYS },
};

// Lithium-7: 3p + 4n + 3e
static const SubAtomicSpec LI7_ATOM[] = {
    { -3, -3, PROTON_TYPE },
    {  3, -3, NEUTRON_TYPE },
    { -3,  3, NEUTRON_TYPE },
    {  3,  3, PROTON_TYPE },
    {  0,  0, NEUTRON_TYPE },
    {  0, -6, NEUTRON_TYPE },
    {  0,  6, PROTON_TYPE },
    { -30, 0, ELECTRON_TYPE_PHYS },
    {  30, 0, ELECTRON_TYPE_PHYS },
    {  0, 35, ELECTRON_TYPE_PHYS },
};

// Carbon-12 nucleus: 6p + 6n + 6e
static const SubAtomicSpec C12_ATOM[] = {
    { -5, -5, PROTON_TYPE }, {  0, -5, NEUTRON_TYPE }, {  5, -5, PROTON_TYPE },
    { -5,  0, NEUTRON_TYPE }, {  0,  0, PROTON_TYPE }, {  5,  0, NEUTRON_TYPE },
    { -5,  5, PROTON_TYPE }, {  0,  5, NEUTRON_TYPE }, {  5,  5, PROTON_TYPE },
    { -2.5f, -2.5f, NEUTRON_TYPE }, {  2.5f, 2.5f, NEUTRON_TYPE }, { -2.5f, 2.5f, PROTON_TYPE },
    { -35, 0, ELECTRON_TYPE_PHYS }, { 35, 0, ELECTRON_TYPE_PHYS },
    { 0, -40, ELECTRON_TYPE_PHYS }, { 0, 40, ELECTRON_TYPE_PHYS },
    { -28, -28, ELECTRON_TYPE_PHYS }, { 28, 28, ELECTRON_TYPE_PHYS },
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
    { -2.5f, -2.5f, ANTIPROTON_TYPE_PHYS },
    {  2.5f, -2.5f, NEUTRON_TYPE },
    { -2.5f,  2.5f, NEUTRON_TYPE },
    {  2.5f,  2.5f, ANTIPROTON_TYPE_PHYS },
    { -25, 0, POSITRON_TYPE_PHYS },
    {  25, 0, POSITRON_TYPE_PHYS },
};

// Oxygen-16: 8p + 8n + 8e
static const SubAtomicSpec O16_ATOM[] = {
    { -5, -7, PROTON_TYPE }, { 0, -7, NEUTRON_TYPE }, { 5, -7, PROTON_TYPE },
    { -5, -2, NEUTRON_TYPE }, { 0, -2, PROTON_TYPE }, { 5, -2, NEUTRON_TYPE },
    { -5,  3, PROTON_TYPE }, { 0,  3, NEUTRON_TYPE }, { 5,  3, PROTON_TYPE },
    { -2, -4, NEUTRON_TYPE }, { 2, 0, NEUTRON_TYPE }, { -2, 5, PROTON_TYPE },
    { 2, -5, NEUTRON_TYPE }, { -3, 1, PROTON_TYPE }, { 3, 1, NEUTRON_TYPE }, { 0, 7, PROTON_TYPE },
    { -40, 0, ELECTRON_TYPE_PHYS }, { 40, 0, ELECTRON_TYPE_PHYS },
    { 0, -40, ELECTRON_TYPE_PHYS }, { 0, 40, ELECTRON_TYPE_PHYS },
    { -30, -30, ELECTRON_TYPE_PHYS }, { 30, 30, ELECTRON_TYPE_PHYS },
    { -30, 30, ELECTRON_TYPE_PHYS }, { 30, -30, ELECTRON_TYPE_PHYS },
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
};

static const char* const PHYS_TYPE_LABELS[PHYS_PARTICLE_TYPES] = {
    "p", "n", "e-", "y", "e+", "p-",
    "ve",
    "mu-", "mu+", "tau-", "tau+", "vmu", "vtau",
    "u", "d", "s", "c", "t", "b",
    "u~", "d~", "s~", "c~", "t~", "b~",
    "g", "W+", "W-", "Z0", "H0",
    "G", "DM", "DE",
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
};
static constexpr int THEME_COUNT = 4;
static const char* THEME_NAMES[] = { "Dark Navy", "Midnight", "Slate", "Ember" };

void PhysicsInterface::push_theme() {
    int t = std::clamp(prefs.theme, 0, THEME_COUNT - 1);
    const auto& c = THEMES[t];

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

    ImGui::PushStyleColor(ImGuiCol_WindowBg,             c.bg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,              bg_child);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,              bg_popup);
    ImGui::PushStyleColor(ImGuiCol_Border,               c.border);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow,         ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,              c.frame);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,       c.frame_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,        frame_act);
    ImGui::PushStyleColor(ImGuiCol_TitleBg,              c.bg_dim);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,        ImVec4(c.bg_dim.x*1.3f, c.bg_dim.y*1.3f, c.bg_dim.z*1.3f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,     title_col);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg,            bg_menu);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          ImVec4(c.bg_dim.x, c.bg_dim.y, c.bg_dim.z, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        ImVec4(c.border.x, c.border.y, c.border.z, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, accent_md);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  accent_hi);
    ImGui::PushStyleColor(ImGuiCol_CheckMark,            c.accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,           accent_hi);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,     c.accent_bright);
    ImGui::PushStyleColor(ImGuiCol_Button,               btn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,        btn_hov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,         accent_md);
    ImGui::PushStyleColor(ImGuiCol_Header,               btn);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,        btn_hov);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,         accent_lo);
    ImGui::PushStyleColor(ImGuiCol_Separator,            sep);
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,     accent_md);
    ImGui::PushStyleColor(ImGuiCol_SeparatorActive,      accent_hi);
    ImGui::PushStyleColor(ImGuiCol_Tab,                  c.frame);
    ImGui::PushStyleColor(ImGuiCol_TabHovered,           btn_hov);
    ImGui::PushStyleColor(ImGuiCol_TabSelected,          accent_lo);
    ImGui::PushStyleColor(ImGuiCol_Text,                 c.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,         c.text_dim);

    // Style vars
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,    10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,      6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,       6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,  8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,   1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,    0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
}

void PhysicsInterface::pop_theme() {
    ImGui::PopStyleVar(THEME_VAR_COUNT);
    ImGui::PopStyleColor(THEME_COLOR_COUNT);
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
    if (pending_spawn || force_obj_placement_mode || accel_mode || mirror_placement_mode)
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

    // Draw event notifications (top-right toast stack)
    draw_notifications();

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

    // Save/Load dialog (drawn before cards so cards render on top)
    if (show_save_dialog || show_load_dialog)
        draw_save_load_dialog();

    // Draw element list window (center, drawn before cards so cards overlay)
    draw_element_list();

    // Draw particle info card (bottom-right, always on top)
    draw_info_card(particles);

    // Draw element detail card (bottom-right, always on top)
    if (element_card_nucleus_rep >= 0)
        draw_element_card(particles);

    // Fade save/load status message
    if (save_load_msg_timer > 0.0f)
        save_load_msg_timer -= ImGui::GetIO().DeltaTime;

    // ── Accelerator aim visualization overlay ──
    if (accel_mode && accel_phase == 1 && accel_source_idx >= 0) {
        ImGuiIO& io = ImGui::GetIO();
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;

        // World-to-screen
        auto w2s = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom;
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
        auto w2s = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom;
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

    // ── Entanglement visualization overlay ──
    if (cfg.entanglement_enabled && readback_positions_ptr && entangled_partners_ptr
        && readback_count > 0) {
        ImGuiIO& eio = ImGui::GetIO();
        float win_w = eio.DisplaySize.x, win_h = eio.DisplaySize.y;
        auto w2s_ent = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom;
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

    pop_theme();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Splash Screen ───────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();

    // Check for dismiss: any mouse button or key
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

    // Fullscreen dark overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##Splash", nullptr, flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.5f;
        ImGui::PushFont(ImGui::GetFont());

        const char* title = "Particle Playground";
        ImVec2 text_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - text_size.x * 0.5f, cy - 120.0f));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", title);

        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Tagline
        const char* tagline = "A GPU-accelerated quantum particle physics sandbox";
        ImVec2 tag_size = ImGui::CalcTextSize(tagline);
        ImGui::SetCursorPosX(cx - tag_size.x * 0.5f);
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 0.9f), "%s", tagline);

        ImGui::Dummy(ImVec2(0, 12));

        // Feature list
        const ImVec4 dim(0.451f, 0.478f, 0.580f, 0.80f);
        const ImVec4 accent(0.302f, 0.749f, 0.953f, 0.9f);
        float left = cx - 220.0f;

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "33 particle types");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Standard Model + Beyond SM");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "7 fundamental forces");
        ImGui::SameLine(); ImGui::TextColored(dim, " - EM, Strong, QCD, Weak, Gravity, Compton, Annihilation");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "Nuclear reactions");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Fusion, fission, isotope decay, chain reactions");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "Quantum mechanics");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Orbitals, virtual pairs, entanglement");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "Emergent physics");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Thermodynamics, magnetic fields");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "Interactive tools");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Accelerator, mirrors, force objects");

        ImGui::SetCursorPosX(left);
        ImGui::TextColored(accent, "22,500 particles");
        ImGui::SameLine(); ImGui::TextColored(dim, " - Real-time O(n^2) Vulkan compute");

        ImGui::Dummy(ImVec2(0, 16));

        // Credits
        const char* credits = "C++20 / Vulkan / Dear ImGui  -  Night-Traders-Dev 2026";
        ImVec2 cr_size = ImGui::CalcTextSize(credits);
        ImGui::SetCursorPosX(cx - cr_size.x * 0.5f);
        ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.48f, 0.7f), "%s", credits);

        ImGui::Dummy(ImVec2(0, 20));

        // Dismiss hint
        const char* subtitle = "Click or press any key to continue";
        ImVec2 sub_size = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorPosX(cx - sub_size.x * 0.5f);
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.5f), "%s", subtitle);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
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
            show_pause_menu = false;
        }

        // Settings
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Settings", ImVec2(btn_w, btn_h))) {
            show_settings_menu = true;
            show_pause_menu = false;
        }

        // Quit
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
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
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, btn_y + btn_spacing * 7 + 20.0f));
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
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 260.0f));

        const auto& tc = THEMES[std::clamp(prefs.theme, 0, THEME_COUNT - 1)];
        ImGui::TextColored(tc.accent, "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Settings panel (centered, fixed width)
        float panel_w = 420.0f;
        float panel_x = cx - panel_w * 0.5f;
        float panel_y = cy - 190.0f;

        ImGui::SetCursorPos(ImVec2(panel_x, panel_y));
        ImGui::BeginGroup();
        ImGui::PushItemWidth(panel_w - 20.0f);

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

        int sys_max = omp_get_max_threads();
        prefs.max_threads = std::clamp(prefs.max_threads, 1, sys_max);
        ImGui::SliderInt("CPU Threads", &prefs.max_threads, 1, sys_max);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("OpenMP thread count for physics\nSystem max: %d", sys_max);

        const char* fps_labels[] = { "Uncapped", "30", "60", "120", "144", "240" };
        const int   fps_values[] = { 0, 30, 60, 120, 144, 240 };
        int fps_idx = 0;
        for (int i = 0; i < 6; i++) {
            if (fps_values[i] == prefs.fps_cap) { fps_idx = i; break; }
        }
        if (ImGui::Combo("FPS Cap", &fps_idx, fps_labels, 6))
            prefs.fps_cap = fps_values[fps_idx];

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

        ImGui::PopItemWidth();
        ImGui::EndGroup();

        // ── Back button ──────────────────────────────────────────────────
        float btn_w = 160.0f;
        float btn_h = 36.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, cy + 200.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("Back", ImVec2(btn_w, btn_h))) {
            show_settings_menu = false;
            show_pause_menu = true;
        }
        ImGui::PopStyleVar();

        // Hint
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, cy + 250.0f));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
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
        ImGui::Text("E: %.2f avg", avg_energy_display);

        // Nuclear decays
        if (nuclear_decay_count_display > 0) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Decays: %u", nuclear_decay_count_display);
        }

        // Element count (clickable)
        if (!element_list.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char elem_btn[32];
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

        // Status indicators
        if (select_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.9f, 1.0f), "[SELECT]");
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
            float popup_h = 350.0f;
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
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── Tools ──
                if (ImGui::TreeNodeEx("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::MenuItem("Halt Velocities")) {
                        request_halt_velocities = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Set all particle velocities to zero");

                    ImGui::MenuItem("Show Trails", nullptr, &cfg.show_trails);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Draw particle paths (fade effect)");

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

                    ImGui::Separator();
                    if (ImGui::MenuItem("Accelerator", nullptr, accel_mode)) {
                        accel_mode = !accel_mode;
                        if (accel_mode) {
                            accel_phase = 0;
                            accel_source_idx = -1;
                            pending_spawn = false;
                            select_mode = false;
                            force_obj_placement_mode = false;
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
                        }
                        show_tools_popup = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Reflective mirror tool\nTwo clicks define a line segment\nParticles bounce off reflectively");

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
                    cfg.gravity_strength = 0.0f;
                    cfg.lorentz_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 100.0f;
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
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 100.0f;
                    break;
                case 2:  // Neutron Star Surface
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e9f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 0.5f;
                    particle_count_slider = 120.0f;
                    break;
                case 3:  // Solar Core
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.2f;
                    particle_count_slider = 110.0f;
                    break;
                case 4:  // Particle Soup
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5000.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 80.0f;
                    break;
                case 5:  // Alpha Emitter
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 300.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 60.0f;
                    break;
                case 6:  // Heavy Nucleus
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 100.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 0.3f;
                    particle_count_slider = 50.0f;
                    break;
                case 7:  // Quark-Gluon Plasma
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2e12f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 0.0f;
                    cfg.string_tension = 10.0f;
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
                    cfg.gravity_strength = 0.0f;
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
                    cfg.gravity_strength = 0.0f;
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
                    cfg.gravity_strength = 0.0f;
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

    // ── Fundamental Forces ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fundamental Forces", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Electromagnetic
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "Electromagnetic");
        ImGui::SliderFloat("B Field", &cfg.lorentz_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("External magnetic field strength\nAffects charged particle trajectories");
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
            ImGui::SetTooltip("Gravitational attraction between massive particles\n0 = off (negligible at particle scale)\nUseful for stellar/neutron star scenarios");
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

        if (field_em || field_strong || field_weak || field_gravity || field_higgs) {
            ImGui::SliderFloat("Brightness", &field_intensity, 0.05f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Field visualization brightness");
        }
    }

    // ── Virtual Particles ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Virtual Particles")) {
        ImGui::Checkbox("Enable Virtual Pairs", &cfg.virtual_pairs_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spontaneous particle-antiparticle pairs\nfrom high-energy close encounters\n(QFT vacuum fluctuations)");

        if (cfg.virtual_pairs_enabled) {
            ImGui::SliderFloat("Energy Threshold", &cfg.virtual_pair_threshold, 0.8f, 5.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Min combined energy for pair creation\nLower = more pairs, higher = rarer");

            int max_pairs = static_cast<int>(cfg.virtual_pair_max_per_tick);
            ImGui::SliderInt("Max Pairs/Tick", &max_pairs, 1, 16);
            cfg.virtual_pair_max_per_tick = static_cast<uint32_t>(max_pairs);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum virtual pairs spawned per frame\nHigher = more quantum foam activity");
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
    ImGui::SliderFloat("Energy", &spawn_energy, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 100.0f, "%.0f");

    // Status text
    if (pending_spawn) {
        ImGui::Spacing();
        if (spawn_atom_Z > 0) {
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

        // ── Momentum, Temperature, Magnetic Moment (need readback velocity) ──
        if (readback_velocities && idx < readback_count) {
            glm::vec2 vel = readback_velocities[idx];
            float speed = glm::length(vel);

            // Mass lookup (mirrors shader get_mass_inv)
            auto get_mass_display = [](uint32_t t) -> float {
                if (t <= 1 || t == 5) return 40.0f;   // proton/neutron/antiproton
                if (t == 2 || t == 4) return 1.0f;     // electron/positron
                if (t == 7 || t == 8) return 200.0f;    // muon/antimuon
                if (t == 9 || t == 10) return 3333.0f;  // tau/antitau
                if (t == 6 || t == 11 || t == 12) return 0.01f;  // neutrinos
                if (t == 3 || t == 25) return 0.01f;    // photon/gluon
                if (t == 13 || t == 19) return 5.0f;    // up/anti-up
                if (t == 14 || t == 20) return 6.7f;    // down/anti-down
                if (t == 15 || t == 21) return 200.0f;  // strange
                if (t == 16 || t == 22) return 2500.0f; // charm
                if (t == 17 || t == 23) return 333333.0f; // top
                if (t == 18 || t == 24) return 8333.0f; // bottom
                if (t == 26 || t == 27) return 16667.0f; // W
                if (t == 28) return 20000.0f;  // Z
                if (t == 29) return 25000.0f;  // Higgs
                return 1.0f;
            };

            float mass = get_mass_display(ptype);
            float momentum = mass * speed;
            float ke = 0.5f * mass * speed * speed;
            float particle_temp = ke * 0.1f;

            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
            ImGui::SameLine(col_w);
            ImGui::Text("%.1f", momentum);

            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Temp");
            ImGui::SameLine(col_w);
            if (particle_temp < 1000.0f)
                ImGui::Text("%.1f K", particle_temp);
            else
                ImGui::Text("%.1fk K", particle_temp / 1000.0f);

            // Magnetic moment: |charge| * speed * coupling
            float mag_moment = std::abs(charge) * speed * 0.5f;
            if (mag_moment > 0.001f) {
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "B Moment");
                ImGui::SameLine(col_w);
                ImGui::Text("%.3f", mag_moment);
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
            bool is_electron = (ptype == ELECTRON_TYPE_PHYS);

            if (is_nucleon || is_electron) {
                int32_t op = particles.orbital_parent[idx];
                if (op >= 0 && static_cast<uint32_t>(op) < particles.types.size()) {
                    // For nucleons, orbital_parent points to the nucleus rep proton
                    // For electrons, orbital_parent points to the nucleus rep proton
                    nuc_rep = op;
                }
                // If this IS a nucleus rep proton (orbital_parent == self or -1 for solo)
                if (is_nucleon && nuc_rep < 0 && ptype == PROTON_TYPE) {
                    // Check if any other particle references this one as parent
                    nuc_rep = static_cast<int32_t>(idx);
                }
            }

            if (nuc_rep >= 0) {
                // Count protons, neutrons, electrons for this nucleus
                int Z = 0, N_count = 0, e_count = 0;
                uint32_t n_total = static_cast<uint32_t>(particles.types.size());
                for (uint32_t pi = 0; pi < n_total; ++pi) {
                    if (particles.orbital_parent.size() <= pi) break;
                    int32_t their_parent = particles.orbital_parent[pi];
                    if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;
                    uint32_t pt = particles.types[pi];
                    if (pt == PROTON_TYPE) Z++;
                    else if (pt == NEUTRON_TYPE) N_count++;
                    else if (pt == ELECTRON_TYPE_PHYS) e_count++;
                }

                if (Z > 0 && Z <= FULL_ELEMENT_COUNT) {
                    int A = Z + N_count;
                    int net_charge = Z - e_count;

                    ImGui::Spacing();
                    ImGui::Separator();

                    // Element header with symbol
                    ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "Element");
                    ImGui::SameLine(col_w);

                    // Clickable element button
                    char elem_label[64];
                    if (net_charge == 0)
                        snprintf(elem_label, sizeof(elem_label), "%s-%d (%s)",
                                 ELEMENT_SYMBOLS[Z], A, ELEMENT_NAMES[Z]);
                    else
                        snprintf(elem_label, sizeof(elem_label), "%s-%d %s%d",
                                 ELEMENT_SYMBOLS[Z], A,
                                 net_charge > 0 ? "+" : "", net_charge);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.6f, 1.0f));
                    if (ImGui::SmallButton(elem_label)) {
                        element_card_nucleus_rep = nuc_rep;
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click for element details");
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

    // Gather all particles belonging to this nucleus
    uint32_t n_total = static_cast<uint32_t>(particles.types.size());
    int Z = 0, N_count = 0, e_count = 0;
    std::vector<uint32_t> nucleon_indices;
    std::vector<uint32_t> electron_indices;
    glm::vec2 sum_pos(0.0f), sum_mom(0.0f);
    uint32_t oldest_birth = UINT32_MAX;

    auto get_mass_elem = [](uint32_t t) -> float {
        if (t <= 1 || t == 5) return 40.0f;
        if (t == 2 || t == 4) return 1.0f;
        return 1.0f;
    };

    for (uint32_t pi = 0; pi < n_total; ++pi) {
        if (pi >= particles.orbital_parent.size()) break;
        int32_t their_parent = particles.orbital_parent[pi];
        if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;

        uint32_t pt = particles.types[pi];
        if (pt == PROTON_TYPE) {
            Z++;
            nucleon_indices.push_back(pi);
        } else if (pt == NEUTRON_TYPE) {
            N_count++;
            nucleon_indices.push_back(pi);
        } else if (pt == ELECTRON_TYPE_PHYS) {
            e_count++;
            electron_indices.push_back(pi);
        } else {
            continue;
        }

        sum_pos += particles.positions[pi];

        if (readback_velocities && pi < readback_count) {
            float mass = get_mass_elem(pt);
            sum_mom += readback_velocities[pi] * mass;
        }

        if (pi < particles.birth_frames.size()) {
            if (particles.birth_frames[pi] < oldest_birth)
                oldest_birth = particles.birth_frames[pi];
        }
    }

    if (Z == 0) { element_card_nucleus_rep = -1; return; }

    int A = Z + N_count;
    int net_charge = Z - e_count;
    float total_mass = Z * 40.0f + N_count * 40.0f + e_count * 1.0f;
    float momentum = glm::length(sum_mom);
    // Shell configuration string
    const int SHELL_CAP[] = {2, 8, 18, 32, 32, 18, 8};
    char shell_str[64] = {};
    {
        int remaining = e_count;
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
    char title[64];
    snprintf(title, sizeof(title), "%s (%s-%d)###ElementCard",
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "?",
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?", A);

    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.10f, 0.05f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.20f, 0.16f, 0.06f, 0.95f));

    if (ImGui::Begin(title, &open, flags)) {
        float col_w = 110.0f;

        // Big element symbol + name
        if (Z <= FULL_ELEMENT_COUNT) {
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1.0f), "%s", ELEMENT_SYMBOLS[Z]);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.75f, 0.5f, 1.0f), "%s", ELEMENT_NAMES[Z]);
        }

        ImGui::Separator();

        // Composition
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Atomic No.");
        ImGui::SameLine(col_w); ImGui::Text("Z = %d", Z);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Neutrons");
        ImGui::SameLine(col_w); ImGui::Text("N = %d", N_count);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mass No.");
        ImGui::SameLine(col_w); ImGui::Text("A = %d", A);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Electrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", e_count);

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
        ImGui::SameLine(col_w); ImGui::Text("%d", Z + N_count + e_count);

        // Momentum
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
        ImGui::SameLine(col_w); ImGui::Text("%.1f", momentum);

        // Age
        if (oldest_birth != UINT32_MAX) {
            uint32_t age_frames = frame_counter_display - oldest_birth;
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f) ImGui::Text("%.1f s", age_sec);
            else ImGui::Text("%.1f min", age_sec / 60.0f);
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

        if (!electron_indices.empty()) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Electrons:");
            ImGui::SameLine(col_w);
            for (size_t ei = 0; ei < electron_indices.size() && ei < 12; ++ei) {
                uint32_t pi = electron_indices[ei];
                char btn[32];
                snprintf(btn, sizeof(btn), "e-##ee%zu", ei);
                ImGui::PushStyleColor(ImGuiCol_Text, PHYS_TYPE_UI_COLORS[ELECTRON_TYPE_PHYS]);
                if (ImGui::SmallButton(btn)) navigate_to_particle = static_cast<int32_t>(pi);
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            if (electron_indices.size() > 12) ImGui::Text("+%zu", electron_indices.size() - 12);
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
// ── Event Notifications (Top-Right Toast Stack) ─────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

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
        ImGui::SliderFloat("##AccelSpeed", &accel_speed, 10.0f, 300.0f, "%.0f px/s");
        if (accel_speed >= 295.0f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "(c)");
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

void PhysicsInterface::draw_element_list() {
    if (!show_element_list || element_list.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 320.0f;
    float max_h = io.DisplaySize.y - 120.0f;
    float win_h = std::min(40.0f + static_cast<float>(element_list.size()) * 28.0f + 40.0f, max_h);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - win_w * 0.5f,
                                    io.DisplaySize.y * 0.5f - win_h * 0.5f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 120), ImVec2(400, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.06f, 0.03f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.10f, 0.04f, 0.95f));

    char title[64];
    snprintf(title, sizeof(title), "Elements (%d)###ElementList",
             static_cast<int>(element_list.size()));

    if (!ImGui::Begin(title, &show_element_list)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Column headers
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%-6s %-12s %4s %6s %3s",
                       "Sym", "Name", "A", "Charge", "e-");
    ImGui::Separator();

    // Scrollable list
    ImGui::BeginChild("##ElemListScroll", ImVec2(0, 0), false);

    for (size_t i = 0; i < element_list.size(); ++i) {
        auto& elem = element_list[i];
        int Z = elem.Z;
        int A = elem.Z + elem.N;
        int net_charge = Z - elem.electrons;

        const char* sym = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?";
        const char* name = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "Unknown";

        // Stability color indicator
        const IsotopeDecayEntry* decay = lookup_isotope_decay(Z, elem.N);
        float hl = 0.0f;
        NuclearDecayMode dmode = NDECAY_NONE;
        if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
        else { dmode = general_stability_rule(Z, elem.N, hl); }

        ImVec4 stab_col;
        if (dmode == NDECAY_NONE)       stab_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);  // stable green
        else if (hl > 7200.0f)          stab_col = ImVec4(0.9f, 0.9f, 0.5f, 1.0f);  // long-lived yellow
        else if (hl > 60.0f)            stab_col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f); // medium orange
        else                            stab_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // short red

        // Charge string
        char charge_str[16];
        if (net_charge == 0) snprintf(charge_str, sizeof(charge_str), "0");
        else if (net_charge > 0) snprintf(charge_str, sizeof(charge_str), "+%d", net_charge);
        else snprintf(charge_str, sizeof(charge_str), "%d", net_charge);

        // Build clickable button label
        char label[128];
        snprintf(label, sizeof(label), "##elem_%zu", i);

        // Stability dot + element row as a selectable
        bool is_selected = (element_card_nucleus_rep == static_cast<int32_t>(elem.rep));
        ImGui::PushID(static_cast<int>(i));

        // Stability dot
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 4.0f,
            ImGui::ColorConvertFloat4ToU32(stab_col));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);

        // Selectable row
        char row_text[128];
        snprintf(row_text, sizeof(row_text), "%-3s %-2s-%-3d  %-12s  %3s  %2de-",
                 sym, sym, A, name, charge_str, elem.electrons);

        if (ImGui::Selectable(row_text, is_selected, ImGuiSelectableFlags_None, ImVec2(0, 22))) {
            element_card_nucleus_rep = static_cast<int32_t>(elem.rep);
            navigate_to_particle = static_cast<int32_t>(elem.rep);
        }

        // Tooltip with details
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1.0f), "%s-%d", sym, A);
            ImGui::Text("Z=%d  N=%d  e-=%d", Z, elem.N, elem.electrons);
            if (dmode == NDECAY_NONE) {
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
    ImGui::End();
    ImGui::PopStyleColor(3);
}

void PhysicsInterface::push_notification(const char* text, ImVec4 color) {
    if (static_cast<int>(notifications.size()) >= NOTIFY_MAX)
        notifications.erase(notifications.begin());  // drop oldest
    notifications.push_back({std::string(text), color, NOTIFY_DURATION});
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

    // Collect directories and .ppsg files
    std::vector<BrowseEntry> dirs, files;
    for (auto& entry : fs::directory_iterator(browse_current_dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // skip hidden

        if (entry.is_directory(ec)) {
            dirs.push_back({ name, true, 0 });
        } else if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            if (ext == ".ppsg") {
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

    const char* title = show_save_dialog ? "Save Simulation###SaveLoad" : "Load Simulation###SaveLoad";
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

                        // Double-click to load (in load mode)
                        if (!show_save_dialog && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            fs::path full = fs::path(browse_current_dir) / entry.name;
                            snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                            request_load = true;
                            show_load_dialog = false;
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
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.7f), "  No .ppsg files in this directory");
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
        }

        // Escape to close
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_save_dialog = false;
            show_load_dialog = false;
        }
    }

    if (!open) {
        show_save_dialog = false;
        show_load_dialog = false;
    }

    ImGui::End();
}
