#pragma once

// Shared UI data tables used across multiple interface module files.
// Include this header in any ui_*.cpp that needs element/particle data.

#include "physics/core/phys_particles.h"
#include <imgui.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

// ── Screen-edge clamping for window placement ───────────────────────────────
// Ensures a window of the given size stays within the display area.
inline ImVec2 clamp_window_pos(ImVec2 pos, ImVec2 size, float margin = 10.0f) {
    auto& io = ImGui::GetIO();
    float max_x = io.DisplaySize.x - size.x - margin;
    float max_y = io.DisplaySize.y - size.y - margin;
    pos.x = std::max(margin, std::min(pos.x, max_x));
    pos.y = std::max(margin, std::min(pos.y, max_y));
    return pos;
}

// ── World-unit ↔ real-unit scale ────────────────────────────────────────────
inline constexpr float R_BOHR_WORLD    = 15.0f;
inline constexpr float BOHR_RADIUS_NM  = 0.0529f;
inline constexpr float WORLD_TO_NM     = BOHR_RADIUS_NM / R_BOHR_WORLD;
inline constexpr float H_ATOM_DIAMETER = 2.0f * R_BOHR_WORLD;

// ── Physics constants for real-world display ─────────────────────────────
inline constexpr float C_REAL = 299792458.0f;

inline float rest_mass_MeV(uint32_t t) {
    return (t < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[t] : 0.0f;
}

inline void fmt_speed(char* buf, size_t sz, float sim_speed) {
    float beta = std::min(sim_speed / C_SIM, 0.9999f);
    float real_speed = beta * C_REAL;
    if (beta >= 0.01f)
        snprintf(buf, sz, "%.4fc", beta);
    else if (real_speed >= 1000.0f)
        snprintf(buf, sz, "%.1f km/s", real_speed / 1000.0f);
    else
        snprintf(buf, sz, "%.0f m/s", real_speed);
}

inline void fmt_momentum(char* buf, size_t sz, float sim_speed, uint32_t ptype) {
    float m0 = rest_mass_MeV(ptype);
    float beta = std::min(sim_speed / C_SIM, 0.9999f);
    float p_MeV;
    if (m0 < 0.001f) {
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

inline void fmt_energy_ev(char* buf, size_t sz, float MeV) {
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

// ── Temperature formatting ───────────────────────────────────────────────────

inline void format_temperature(float kelvin, char* buf, int buf_size, int unit = 0) {
    float val = kelvin;
    const char* suffix = "K";
    if (unit == 1) { val = kelvin - 273.15f; suffix = "\xC2\xB0""C"; }
    else if (unit == 2) { val = kelvin * 1.8f - 459.67f; suffix = "\xC2\xB0""F"; }

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

// ── Periodic table element data ──────────────────────────────────────────────

struct ElementData {
    int Z, N;
    const char* symbol;
    const char* name;
};

inline const ElementData ELEMENTS[] = {
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
inline const int ELEMENT_COUNT = sizeof(ELEMENTS) / sizeof(ELEMENTS[0]);

inline const int PT_LAYOUT[4][10] = {
    {  0, -1, -1, -1, -1, -1, -1, -1, -1,  1},
    {  2,  3, -1, -1,  4,  5,  6,  7,  8,  9},
    { 10, 11, -1, -1, 12, 13, 14, 15, 16, 17},
    { 18, 19, -1, -1, -1, -1, -1, -1, -1, 20},
};

// ── Element names by atomic number (Z=1..118) ────────────────────────────────
inline const char* const ELEMENT_NAMES[] = {
    "?",
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
inline const char* const ELEMENT_SYMBOLS[] = {
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
inline constexpr int FULL_ELEMENT_COUNT = 118;

// ── Molecule common name lookup ──────────────────────────────────────────────

struct MoleculeNameEntry { const char* formula; const char* name; };

inline const MoleculeNameEntry MOLECULE_COMMON_NAMES[] = {
    // Diatomic
    {"H2",      "Hydrogen"},
    {"O2",      "Oxygen"},
    {"N2",      "Nitrogen"},
    {"F2",      "Fluorine"},
    {"Cl2",     "Chlorine"},
    {"HF",      "Hydrogen Fluoride"},
    {"HCl",     "Hydrogen Chloride"},
    {"CO",      "Carbon Monoxide"},
    {"NO",      "Nitric Oxide"},
    {"NaCl",    "Sodium Chloride"},
    {"MgO",     "Magnesium Oxide"},
    {"CaO",     "Calcium Oxide"},
    {"Br2",     "Bromine"},
    {"I2",      "Iodine"},
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
    {"H2CO3",   "Carbonic Acid"},
    // 4-atom
    {"NH3",     "Ammonia"},
    {"PH3",     "Phosphine"},
    {"NaOH",    "Sodium Hydroxide"},
    {"KOH",     "Potassium Hydroxide"},
    {"LiOH",    "Lithium Hydroxide"},
    {"H2O2",    "Hydrogen Peroxide"},
    {"H3BO3",   "Boric Acid"},
    // 5-atom
    {"CH4",     "Methane"},
    {"SiH4",    "Silane"},
    {"CF4",     "Carbon Tetrafluoride"},
    {"CCl4",    "Carbon Tetrachloride"},
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
    {"C4H10",   "Butane"},
    {"C5H12",   "Pentane"},
    {"C6H14",   "Hexane"},
    {"C6H6",    "Benzene"},
    {"C6H12O6", "Glucose"},
    {"C12H22O11","Sucrose"},
    {"C8H10N4O2","Caffeine"},
    {"SF6",     "Sulfur Hexafluoride"},
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
    {"C7H8N4O2",    "Theobromine"},
    // Amino acids
    {"C2H5NO2",     "Glycine"},
    {"C3H7NO2",     "Alanine"},
    {"C5H11NO2",    "Valine"},
    {"C6H13NO2",    "Leucine"},
    {"C9H11NO2",    "Phenylalanine"},
    {"C11H12N2O2",  "Tryptophan"},
    {"C5H9NO4",     "Glutamic Acid"},
    {"C9H11NO3",    "Tyrosine"},
    // Acids
    {"C6H8O7",  "Citric Acid"},
    {"C3H6O3",  "Lactic Acid"},
    {"C2H2O4",  "Oxalic Acid"},
    {"C4H6O6",  "Tartaric Acid"},
    {"C4H6O5",  "Malic Acid"},
    {"C7H6O2",  "Benzoic Acid"},
    {"C7H6O3",  "Salicylic Acid"},
    {"C3H4O3",  "Pyruvic Acid"},
    {"C4H6O4",  "Succinic Acid"},
    // Bases
    {"C5H5N",       "Pyridine"},
    {"C6H7N",       "Aniline"},
    {"C5H11N",      "Piperidine"},
    // DNA/RNA nucleobases
    {"C5H5N5",      "Adenine"},
    {"C5H5N5O",     "Guanine"},
    {"C4H5N3O",     "Cytosine"},
    {"C5H6N2O2",    "Thymine"},
    {"C4H4N2O2",    "Uracil"},
    // DNA/RNA sugars
    {"C5H10O4",     "Deoxyribose"},
    {"C5H10O5",     "Ribose"},
    // DNA/RNA nucleosides
    {"C10H13N5O3",  "Deoxyadenosine"},
    {"C10H13N5O4",  "Adenosine"},
    {"C10H13N5O5",  "Guanosine"},
    // Energy molecules
    {"C10H16N5O13P3","ATP"},
    {"C10H15N5O10P2","ADP"},
    {"C10H14N5O7P",  "AMP"},
    // Metal oxides
    {"Fe2O3",   "Iron(III) Oxide"},
    {"CaCO3",   "Calcium Carbonate"},
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
    // Fullerenes
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
inline constexpr int MOLECULE_COMMON_NAMES_COUNT =
    sizeof(MOLECULE_COMMON_NAMES) / sizeof(MOLECULE_COMMON_NAMES[0]);

inline const char* lookup_molecule_common_name(const char* formula) {
    for (int i = 0; i < MOLECULE_COMMON_NAMES_COUNT; ++i)
        if (strcmp(formula, MOLECULE_COMMON_NAMES[i].formula) == 0)
            return MOLECULE_COMMON_NAMES[i].name;
    return nullptr;
}

// Helper: extract element name from a .ppel filename like "H-1.ppel" or "Fe-56.ppel"
inline const char* element_name_from_ppel_filename(const char* filename) {
    // Parse the symbol before the first '-'
    char sym[4] = {};
    int i = 0;
    while (filename[i] && filename[i] != '-' && i < 3) {
        sym[i] = filename[i];
        i++;
    }
    sym[i] = '\0';
    // Look up Z from symbol
    for (int z = 1; z <= FULL_ELEMENT_COUNT; ++z) {
        if (strcmp(ELEMENT_SYMBOLS[z], sym) == 0)
            return ELEMENT_NAMES[z];
    }
    return nullptr;
}

// ── Particle name/label/color access for all 282 types ──────────────────────
// Function-based for meson range; falls back to base arrays for types 0-73.

inline const char* phys_type_name(uint32_t t) {
    static const char* const BASE[] = {
        "Proton", "Neutron", "Electron", "Photon", "Positron", "Antiproton",
        "Neutrino_e",
        "Muon", "Anti-muon", "Tau", "Anti-tau", "Neutrino_mu", "Neutrino_tau",
        "Up", "Down", "Strange", "Charm", "Top", "Bottom",
        "Anti-up", "Anti-down", "Anti-strange", "Anti-charm", "Anti-top", "Anti-bottom",
        "Gluon", "W+", "W-", "Z0", "Higgs",
        "Graviton", "Dark Matter", "Dark Energy",
        "Axino", "WIMPzilla", "SIMP", "Sterile Neutrino", "Dark Photon", "Q-Ball",
        "Selectron", "Smuon", "Stau", "Squark", "Gluino", "Photino",
        "Wino", "Zino", "Higgsino", "Neutralino", "Sneutrino",
        "Gravitino", "X Boson", "Y Boson", "Monopole", "Radion", "Dilaton",
        "Tachyon", "Preon", "Inflaton", "Majoron", "Odderon",
        "Glueball", "Skyrmion", "X17", "Chameleon",
        "Paraparticle", "Dyn. Axion QP",
        "Electron Hole",
        "Plasmon", "Phonon", "Magnon", "Polaron", "Cooper Pair", "Roton",
    };
    if (t < 74) return BASE[t];
    if (t >= MESON_TYPE_FIRST && t <= MESON_TYPE_LAST) return MESON_NAMES[t - MESON_TYPE_FIRST];
    return "Reserved";
}

inline const char* phys_type_label(uint32_t t) {
    static const char* const BASE[] = {
        "p", "n", "e-", "y", "e+", "p-", "ve",
        "mu-", "mu+", "tau-", "tau+", "vmu", "vtau",
        "u", "d", "s", "c", "t", "b",
        "u~", "d~", "s~", "c~", "t~", "b~",
        "g", "W+", "W-", "Z0", "H0",
        "G", "DM", "DE",
        "Ax", "WZ", "SI", "Ns", "A'", "QB",
        "e~", "mu~", "ta~", "q~", "g~", "y~",
        "W~", "Z~", "H~", "N1", "v~",
        "G~", "X", "Y", "MM", "Ra", "Di",
        "Ta", "Pr", "In", "Mj", "Od",
        "Gb", "Sk", "X17", "Ch",
        "Pp", "Dq", "h+",
        "Pl", "Ph", "Mn", "Po", "CP", "Ro",
    };
    if (t < 74) return BASE[t];
    if (t >= MESON_TYPE_FIRST && t <= MESON_TYPE_LAST) return MESON_LABELS[t - MESON_TYPE_FIRST];
    return "??";
}

inline ImVec4 phys_type_ui_color(uint32_t t) {
    static const ImVec4 BASE[] = {
        {0.9f,0.2f,0.2f,1}, {0.7f,0.7f,0.7f,1}, {0.2f,0.5f,1.0f,1}, {1.0f,1.0f,0.6f,1},
        {1.0f,0.3f,0.8f,1}, {0.2f,0.85f,0.7f,1}, {0.6f,0.9f,0.6f,1},
        {0.6f,0.3f,0.9f,1}, {0.8f,0.5f,1.0f,1}, {0.4f,0.2f,0.7f,1}, {0.6f,0.4f,0.9f,1},
        {0.5f,0.8f,0.5f,1}, {0.4f,0.7f,0.4f,1},
        {0.9f,0.5f,0.2f,1}, {0.4f,0.7f,0.2f,1}, {0.2f,0.8f,0.6f,1}, {0.9f,0.8f,0.2f,1},
        {1.0f,0.3f,0.3f,1}, {0.5f,0.3f,0.8f,1},
        {1.0f,0.7f,0.5f,1}, {0.7f,0.9f,0.5f,1}, {0.5f,1.0f,0.8f,1}, {1.0f,0.9f,0.5f,1},
        {1.0f,0.6f,0.6f,1}, {0.7f,0.6f,1.0f,1},
        {0.3f,0.9f,0.3f,1}, {0.9f,0.9f,1.0f,1}, {0.7f,0.7f,1.0f,1}, {0.8f,0.8f,0.9f,1},
        {1.0f,0.85f,0.3f,1},
        {0.7f,0.8f,1.0f,1}, {0.3f,0.1f,0.5f,1}, {0.6f,0.1f,0.2f,1},
        {0.4f,0.2f,0.6f,1}, {0.2f,0.05f,0.35f,1}, {0.35f,0.25f,0.55f,1},
        {0.45f,0.55f,0.45f,1}, {0.5f,0.2f,0.7f,1}, {0.55f,0.3f,0.75f,1},
        {0.5f,0.8f,1.0f,1}, {0.7f,0.6f,1.0f,1}, {0.6f,0.5f,0.9f,1},
        {1.0f,0.7f,0.4f,1}, {0.4f,1.0f,0.5f,1}, {1.0f,1.0f,0.8f,1},
        {0.9f,0.9f,1.0f,1}, {0.8f,0.8f,0.95f,1}, {1.0f,0.9f,0.6f,1},
        {0.4f,0.2f,0.65f,1}, {0.55f,0.85f,0.55f,1},
        {0.6f,0.7f,1.0f,1}, {1.0f,0.4f,0.4f,1}, {1.0f,0.5f,0.3f,1},
        {0.95f,0.95f,0.95f,1}, {0.7f,0.6f,0.4f,1}, {0.6f,0.55f,0.45f,1},
        {0.0f,1.0f,1.0f,1}, {1.0f,0.0f,0.5f,1}, {1.0f,0.7f,0.1f,1},
        {0.55f,0.6f,0.55f,1}, {0.7f,0.3f,1.0f,1}, {0.5f,1.0f,0.3f,1},
        {0.9f,0.4f,0.2f,1}, {0.2f,0.9f,0.8f,1}, {0.7f,0.5f,0.3f,1},
        {0.9f,0.2f,1.0f,1}, {0.4f,0.7f,0.95f,1},
        {1.0f,0.75f,0.25f,1}, {0.3f,1.0f,0.95f,1}, {0.95f,0.95f,0.4f,1},
        {1.0f,0.45f,0.15f,1}, {0.7f,0.35f,0.9f,1}, {0.6f,0.85f,1.0f,1}, {0.2f,0.9f,0.7f,1},
    };
    if (t < 74) return BASE[t];
    if (t >= MESON_TYPE_FIRST && t <= MESON_TYPE_LAST) {
        const auto& c = MESON_RENDER_COLORS[t - MESON_TYPE_FIRST];
        return ImVec4(c.r, c.g, c.b, c.a);
    }
    return ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
}

// ── Lazy-init arrays — PHYS_TYPE_NAMES[t] / LABELS / UI_COLORS syntax ───────
// Delegates to phys_type_name/label/ui_color() for all 282 types incl. mesons.

inline const char* const* get_phys_type_names_() {
    static const char* arr[PHYS_PARTICLE_TYPES];
    static bool init = false;
    if (!init) {
        for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t)
            arr[t] = phys_type_name(t);
        init = true;
    }
    return arr;
}

inline const char* const* get_phys_type_labels_() {
    static const char* arr[PHYS_PARTICLE_TYPES];
    static bool init = false;
    if (!init) {
        for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t)
            arr[t] = phys_type_label(t);
        init = true;
    }
    return arr;
}

inline const ImVec4* get_phys_type_ui_colors_() {
    static ImVec4 arr[PHYS_PARTICLE_TYPES];
    static bool init = false;
    if (!init) {
        for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t)
            arr[t] = phys_type_ui_color(t);
        init = true;
    }
    return arr;
}

#define PHYS_TYPE_NAMES     (get_phys_type_names_())
#define PHYS_TYPE_LABELS    (get_phys_type_labels_())
#define PHYS_TYPE_UI_COLORS (get_phys_type_ui_colors_())
