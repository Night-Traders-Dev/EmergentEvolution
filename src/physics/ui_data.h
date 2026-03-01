#pragma once

// Shared UI data tables used across multiple interface module files.
// Include this header in any ui_*.cpp that needs element/particle data.

#include "physics/phys_particles.h"
#include <imgui.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

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

// ── Particle name/color tables for all 67 types ─────────────────────────────

inline const char* const PHYS_TYPE_NAMES[PHYS_PARTICLE_TYPES] = {
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
};

inline const char* const PHYS_TYPE_LABELS[PHYS_PARTICLE_TYPES] = {
    "p", "n", "e-", "y", "e+", "p-",
    "ve",
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
    "Pp", "Dq",
};

inline const ImVec4 PHYS_TYPE_UI_COLORS[PHYS_PARTICLE_TYPES] = {
    ImVec4(0.9f, 0.2f, 0.2f, 1.0f),   // proton
    ImVec4(0.7f, 0.7f, 0.7f, 1.0f),   // neutron
    ImVec4(0.2f, 0.5f, 1.0f, 1.0f),   // electron
    ImVec4(1.0f, 1.0f, 0.6f, 1.0f),   // photon
    ImVec4(1.0f, 0.3f, 0.8f, 1.0f),   // positron
    ImVec4(0.2f, 0.85f, 0.7f, 1.0f),  // antiproton
    ImVec4(0.6f, 0.9f, 0.6f, 1.0f),   // neutrino_e
    ImVec4(0.6f, 0.3f, 0.9f, 1.0f),   // muon
    ImVec4(0.8f, 0.5f, 1.0f, 1.0f),   // anti-muon
    ImVec4(0.4f, 0.2f, 0.7f, 1.0f),   // tau
    ImVec4(0.6f, 0.4f, 0.9f, 1.0f),   // anti-tau
    ImVec4(0.5f, 0.8f, 0.5f, 1.0f),   // neutrino_mu
    ImVec4(0.4f, 0.7f, 0.4f, 1.0f),   // neutrino_tau
    ImVec4(0.9f, 0.5f, 0.2f, 1.0f),   // up
    ImVec4(0.4f, 0.7f, 0.2f, 1.0f),   // down
    ImVec4(0.2f, 0.8f, 0.6f, 1.0f),   // strange
    ImVec4(0.9f, 0.8f, 0.2f, 1.0f),   // charm
    ImVec4(1.0f, 0.3f, 0.3f, 1.0f),   // top
    ImVec4(0.5f, 0.3f, 0.8f, 1.0f),   // bottom
    ImVec4(1.0f, 0.7f, 0.5f, 1.0f),   // anti-up
    ImVec4(0.7f, 0.9f, 0.5f, 1.0f),   // anti-down
    ImVec4(0.5f, 1.0f, 0.8f, 1.0f),   // anti-strange
    ImVec4(1.0f, 0.9f, 0.5f, 1.0f),   // anti-charm
    ImVec4(1.0f, 0.6f, 0.6f, 1.0f),   // anti-top
    ImVec4(0.7f, 0.6f, 1.0f, 1.0f),   // anti-bottom
    ImVec4(0.3f, 0.9f, 0.3f, 1.0f),   // gluon
    ImVec4(0.9f, 0.9f, 1.0f, 1.0f),   // W+
    ImVec4(0.7f, 0.7f, 1.0f, 1.0f),   // W-
    ImVec4(0.8f, 0.8f, 0.9f, 1.0f),   // Z0
    ImVec4(1.0f, 0.85f, 0.3f, 1.0f),  // Higgs
    ImVec4(0.7f, 0.8f, 1.0f, 1.0f),   // graviton
    ImVec4(0.3f, 0.1f, 0.5f, 1.0f),   // dark matter
    ImVec4(0.6f, 0.1f, 0.2f, 1.0f),   // dark energy
    ImVec4(0.4f, 0.2f, 0.6f, 1.0f),   // axino
    ImVec4(0.2f, 0.05f, 0.35f, 1.0f), // WIMPzilla
    ImVec4(0.35f, 0.25f, 0.55f, 1.0f),// SIMP
    ImVec4(0.45f, 0.55f, 0.45f, 1.0f),// sterile neutrino
    ImVec4(0.5f, 0.2f, 0.7f, 1.0f),   // dark photon
    ImVec4(0.55f, 0.3f, 0.75f, 1.0f), // Q-Ball
    ImVec4(0.5f, 0.8f, 1.0f, 1.0f),   // selectron
    ImVec4(0.7f, 0.6f, 1.0f, 1.0f),   // smuon
    ImVec4(0.6f, 0.5f, 0.9f, 1.0f),   // stau
    ImVec4(1.0f, 0.7f, 0.4f, 1.0f),   // squark
    ImVec4(0.4f, 1.0f, 0.5f, 1.0f),   // gluino
    ImVec4(1.0f, 1.0f, 0.8f, 1.0f),   // photino
    ImVec4(0.9f, 0.9f, 1.0f, 1.0f),   // wino
    ImVec4(0.8f, 0.8f, 0.95f, 1.0f),  // zino
    ImVec4(1.0f, 0.9f, 0.6f, 1.0f),   // higgsino
    ImVec4(0.4f, 0.2f, 0.65f, 1.0f),  // neutralino
    ImVec4(0.55f, 0.85f, 0.55f, 1.0f),// sneutrino
    ImVec4(0.6f, 0.7f, 1.0f, 1.0f),   // gravitino
    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),   // X boson
    ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // Y boson
    ImVec4(0.95f, 0.95f, 0.95f, 1.0f),// monopole
    ImVec4(0.7f, 0.6f, 0.4f, 1.0f),   // radion
    ImVec4(0.6f, 0.55f, 0.45f, 1.0f), // dilaton
    ImVec4(0.0f, 1.0f, 1.0f, 1.0f),   // tachyon
    ImVec4(1.0f, 0.0f, 0.5f, 1.0f),   // preon
    ImVec4(1.0f, 0.7f, 0.1f, 1.0f),   // inflaton
    ImVec4(0.55f, 0.6f, 0.55f, 1.0f), // majoron
    ImVec4(0.7f, 0.3f, 1.0f, 1.0f),   // odderon
    ImVec4(0.5f, 1.0f, 0.3f, 1.0f),   // glueball
    ImVec4(0.9f, 0.4f, 0.2f, 1.0f),   // skyrmion
    ImVec4(0.2f, 0.9f, 0.8f, 1.0f),   // X17
    ImVec4(0.7f, 0.5f, 0.3f, 1.0f),   // chameleon
    ImVec4(0.9f, 0.2f, 1.0f, 1.0f),   // paraparticle
    ImVec4(0.4f, 0.7f, 0.95f, 1.0f),  // dyn axion QP
};
