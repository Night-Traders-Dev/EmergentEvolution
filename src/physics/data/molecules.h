#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

// ── Molecule template data structures ────────────────────────────────────────

struct MoleculeAtom {
    float dx, dy;   // offset in pixels from molecule center
    int Z;          // atomic number (1=H, 6=C, 8=O, etc.)
};

struct MoleculeTemplate {
    const char* formula;
    const char* name;
    const MoleculeAtom* atoms;
    uint32_t atom_count;
};

// ── Helpers ──────────────────────────────────────────────────────────────────

// Reverse lookup: element symbol string (length len) → atomic number (0 if not found)
inline int symbol_to_Z(const char* sym, int len) {
    static const struct { const char* s; int z; } TABLE[] = {
        {"H",1},{"He",2},{"Li",3},{"Be",4},{"B",5},{"C",6},{"N",7},{"O",8},
        {"F",9},{"Ne",10},{"Na",11},{"Mg",12},{"Al",13},{"Si",14},{"P",15},
        {"S",16},{"Cl",17},{"Ar",18},{"K",19},{"Ca",20},{"Ti",22},{"Fe",26},
        {"Ni",28},{"Cu",29},{"Zn",30},{"Br",35},{"Sr",38},{"Au",79},{"Pb",82},
        {"U",92},
    };
    for (auto& e : TABLE) {
        if (static_cast<int>(strlen(e.s)) == len && strncmp(e.s, sym, len) == 0)
            return e.z;
    }
    return 0;
}

// Default stable-isotope neutron count for common elements
inline int default_neutron_count(int Z) {
    static const int N_TABLE[] = {
    // Z: 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
        0, 2, 4, 5, 6, 6, 7, 8,10,10,12,12,14,14,16,16,18,22,20,20
    };
    if (Z >= 1 && Z <= 20) return N_TABLE[Z - 1];
    if (Z == 22) return 26;  // Ti-48
    if (Z == 26) return 30;  // Fe-56
    if (Z == 28) return 30;  // Ni-58
    if (Z == 29) return 34;  // Cu-63
    if (Z == 30) return 35;  // Zn-65
    if (Z == 35) return 45;  // Br-80
    if (Z == 38) return 50;  // Sr-88
    if (Z == 79) return 118; // Au-197
    if (Z == 82) return 125; // Pb-207
    if (Z == 92) return 146; // U-238
    return Z; // fallback
}

// Parse a molecular formula string into {Z, count} pairs.
// Returns empty vector on parse error.
// Examples: "H2O" → [{1,2},{8,1}], "C6H12O6" → [{6,6},{1,12},{8,6}]
inline std::vector<std::pair<int,int>> parse_molecular_formula(const char* str) {
    std::vector<std::pair<int,int>> result;
    int i = 0;
    int len = static_cast<int>(strlen(str));

    while (i < len) {
        // Expect uppercase letter
        if (str[i] < 'A' || str[i] > 'Z') { return {}; }

        int sym_start = i;
        i++;
        // Optional lowercase letter(s)
        while (i < len && str[i] >= 'a' && str[i] <= 'z') i++;

        int sym_len = i - sym_start;
        int Z = symbol_to_Z(str + sym_start, sym_len);
        if (Z == 0) return {}; // unknown element

        // Parse optional count digits
        int count = 0;
        while (i < len && str[i] >= '0' && str[i] <= '9') {
            count = count * 10 + (str[i] - '0');
            i++;
        }
        if (count == 0) count = 1;

        // Merge with existing entry for same Z
        bool found = false;
        for (auto& p : result) {
            if (p.first == Z) { p.second += count; found = true; break; }
        }
        if (!found) result.push_back({Z, count});
    }
    return result;
}

// Find a molecule template by exact formula match. Returns index or -1.
inline int find_molecule_template(const char* formula);

// ── Molecule template database ───────────────────────────────────────────────
// All positions in pixels relative to molecule center.
// Bond rest length = 22px. Atoms within 28px auto-bond via update_bonds().

// ── Diatomic ─────────────────────────────────────────────────────────────────
static const MoleculeAtom H2_ATOMS[] = {
    {-11.0f, 0.0f, 1}, {11.0f, 0.0f, 1}
};
static const MoleculeAtom O2_ATOMS[] = {
    {-11.0f, 0.0f, 8}, {11.0f, 0.0f, 8}
};
static const MoleculeAtom N2_ATOMS[] = {
    {-11.0f, 0.0f, 7}, {11.0f, 0.0f, 7}
};
static const MoleculeAtom F2_ATOMS[] = {
    {-11.0f, 0.0f, 9}, {11.0f, 0.0f, 9}
};
static const MoleculeAtom Cl2_ATOMS[] = {
    {-11.0f, 0.0f, 17}, {11.0f, 0.0f, 17}
};
static const MoleculeAtom HF_ATOMS[] = {
    {-11.0f, 0.0f, 1}, {11.0f, 0.0f, 9}
};
static const MoleculeAtom HCl_ATOMS[] = {
    {-11.0f, 0.0f, 1}, {11.0f, 0.0f, 17}
};
static const MoleculeAtom CO_ATOMS[] = {
    {-11.0f, 0.0f, 6}, {11.0f, 0.0f, 8}
};
static const MoleculeAtom NO_ATOMS[] = {
    {-11.0f, 0.0f, 7}, {11.0f, 0.0f, 8}
};
static const MoleculeAtom NaCl_ATOMS[] = {
    {-11.0f, 0.0f, 11}, {11.0f, 0.0f, 17}
};
static const MoleculeAtom MgO_ATOMS[] = {
    {-11.0f, 0.0f, 12}, {11.0f, 0.0f, 8}
};
static const MoleculeAtom CaO_ATOMS[] = {
    {-11.0f, 0.0f, 20}, {11.0f, 0.0f, 8}
};

// ── Triatomic ────────────────────────────────────────────────────────────────
// H2O: O at center, H's at 104.5° (half-angle 52.25°)
// dx = 22*sin(52.25°) = 17.4, dy = 22*cos(52.25°) = 13.4
static const MoleculeAtom H2O_ATOMS[] = {
    { 0.0f,  0.0f, 8},
    {-17.4f, 13.4f, 1},
    { 17.4f, 13.4f, 1},
};
// CO2: linear O=C=O
static const MoleculeAtom CO2_ATOMS[] = {
    { 0.0f, 0.0f, 6},
    {-22.0f, 0.0f, 8},
    { 22.0f, 0.0f, 8},
};
// NO2: bent ~134°, half-angle 67°
static const MoleculeAtom NO2_ATOMS[] = {
    { 0.0f,  0.0f, 7},
    {-20.3f, 8.6f, 8},
    { 20.3f, 8.6f, 8},
};
// H2S: bent ~92°, half-angle 46°
static const MoleculeAtom H2S_ATOMS[] = {
    { 0.0f,  0.0f, 16},
    {-15.8f, 15.3f, 1},
    { 15.8f, 15.3f, 1},
};
// O3: bent ~116.8°, half-angle 58.4°
static const MoleculeAtom O3_ATOMS[] = {
    { 0.0f,  0.0f, 8},
    {-18.7f, 11.5f, 8},
    { 18.7f, 11.5f, 8},
};
// SO2: bent ~119°
static const MoleculeAtom SO2_ATOMS[] = {
    { 0.0f,  0.0f, 16},
    {-19.1f, 11.0f, 8},
    { 19.1f, 11.0f, 8},
};
// CS2: linear S=C=S
static const MoleculeAtom CS2_ATOMS[] = {
    { 0.0f, 0.0f, 6},
    {-22.0f, 0.0f, 16},
    { 22.0f, 0.0f, 16},
};
// N2O: linear N-N-O
static const MoleculeAtom N2O_ATOMS[] = {
    {-22.0f, 0.0f, 7},
    { 0.0f,  0.0f, 7},
    { 22.0f, 0.0f, 8},
};
// HCN: linear H-C≡N
static const MoleculeAtom HCN_ATOMS[] = {
    {-22.0f, 0.0f, 1},
    { 0.0f,  0.0f, 6},
    { 22.0f, 0.0f, 7},
};
// SiO2: linear O-Si-O
static const MoleculeAtom SiO2_ATOMS[] = {
    { 0.0f, 0.0f, 14},
    {-22.0f, 0.0f, 8},
    { 22.0f, 0.0f, 8},
};

// ── 4-atom ───────────────────────────────────────────────────────────────────
// NH3: trigonal pyramid → 2D: 120° fan
static const MoleculeAtom NH3_ATOMS[] = {
    { 0.0f,  0.0f, 7},
    { 0.0f, -22.0f, 1},
    {-19.1f, 11.0f, 1},
    { 19.1f, 11.0f, 1},
};
// PH3: phosphine
static const MoleculeAtom PH3_ATOMS[] = {
    { 0.0f,  0.0f, 15},
    { 0.0f, -22.0f, 1},
    {-19.1f, 11.0f, 1},
    { 19.1f, 11.0f, 1},
};
// BF3: trigonal planar
static const MoleculeAtom BF3_ATOMS[] = {
    { 0.0f,  0.0f, 5},
    { 0.0f, -22.0f, 9},
    {-19.1f, 11.0f, 9},
    { 19.1f, 11.0f, 9},
};
// SO3: trigonal planar
static const MoleculeAtom SO3_ATOMS[] = {
    { 0.0f,  0.0f, 16},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 8},
    { 19.1f, 11.0f, 8},
};
// H2O2: H-O-O-H
static const MoleculeAtom H2O2_ATOMS[] = {
    {-11.0f,  0.0f, 8},
    { 11.0f,  0.0f, 8},
    {-22.0f, -17.4f, 1},
    { 22.0f,  17.4f, 1},
};
// NaOH: Na-O-H linear
static const MoleculeAtom NaOH_ATOMS[] = {
    {-22.0f, 0.0f, 11},
    { 0.0f,  0.0f, 8},
    { 22.0f, 0.0f, 1},
};
// COCl2: phosgene Cl2C=O
static const MoleculeAtom COCl2_ATOMS[] = {
    { 0.0f,   0.0f, 6},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 17},
    { 19.1f, 11.0f, 17},
};

// ── 5-atom ───────────────────────────────────────────────────────────────────
// CH4: tetrahedral → 2D cross
static const MoleculeAtom CH4_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 1},
    { 0.0f,  22.0f, 1},
    {-22.0f, 0.0f, 1},
    { 22.0f, 0.0f, 1},
};
// SiH4: silane
static const MoleculeAtom SiH4_ATOMS[] = {
    { 0.0f,  0.0f, 14},
    { 0.0f, -22.0f, 1},
    { 0.0f,  22.0f, 1},
    {-22.0f, 0.0f, 1},
    { 22.0f, 0.0f, 1},
};
// CF4: carbon tetrafluoride
static const MoleculeAtom CF4_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 9},
    { 0.0f,  22.0f, 9},
    {-22.0f, 0.0f, 9},
    { 22.0f, 0.0f, 9},
};
// CCl4: carbon tetrachloride
static const MoleculeAtom CCl4_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 17},
    { 0.0f,  22.0f, 17},
    {-22.0f, 0.0f, 17},
    { 22.0f, 0.0f, 17},
};
// CH3Cl: chloromethane
static const MoleculeAtom CH3Cl_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 17},
    {-19.1f, 11.0f, 1},
    { 19.1f, 11.0f, 1},
    { 0.0f,  22.0f, 1},
};
// CH2Cl2: dichloromethane
static const MoleculeAtom CH2Cl2_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 17},
    { 0.0f,  22.0f, 17},
    {-22.0f, 0.0f, 1},
    { 22.0f, 0.0f, 1},
};
// CHCl3: chloroform
static const MoleculeAtom CHCl3_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 17},
    {-19.1f, 11.0f, 17},
    { 19.1f, 11.0f, 17},
    { 0.0f,  22.0f, 1},
};
// HNO3: nitric acid
static const MoleculeAtom HNO3_ATOMS[] = {
    { 0.0f,   0.0f, 7},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 8},
    { 19.1f, 11.0f, 8},
    { 30.1f, 28.4f, 1},
};

// ── 6-9 atom ─────────────────────────────────────────────────────────────────
// CH2O: formaldehyde
static const MoleculeAtom CH2O_ATOMS[] = {
    { 0.0f,  0.0f, 6},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 1},
    { 19.1f, 11.0f, 1},
};
// CH3OH: methanol C-O chain
static const MoleculeAtom CH3OH_ATOMS[] = {
    { 0.0f,   0.0f, 6},
    { 22.0f,  0.0f, 8},
    {-11.0f, -19.1f, 1},
    {-11.0f,  19.1f, 1},
    {-22.0f,  0.0f, 1},
    { 33.0f, -17.4f, 1},
};
// C2H2: acetylene linear H-C≡C-H
static const MoleculeAtom C2H2_ATOMS[] = {
    {-11.0f, 0.0f, 6},
    { 11.0f, 0.0f, 6},
    {-33.0f, 0.0f, 1},
    { 33.0f, 0.0f, 1},
};
// C2H4: ethylene
static const MoleculeAtom C2H4_ATOMS[] = {
    {-11.0f,  0.0f, 6},
    { 11.0f,  0.0f, 6},
    {-28.1f, -15.6f, 1},
    {-28.1f,  15.6f, 1},
    { 28.1f, -15.6f, 1},
    { 28.1f,  15.6f, 1},
};
// HCOOH: formic acid H-C(=O)-O-H
static const MoleculeAtom HCOOH_ATOMS[] = {
    { 0.0f,   0.0f, 6},
    { 0.0f, -22.0f, 8},
    { 22.0f,  0.0f, 8},
    {-22.0f,  0.0f, 1},
    { 33.0f,  17.4f, 1},
};
// CH3NH2: methylamine
static const MoleculeAtom CH3NH2_ATOMS[] = {
    {-11.0f,  0.0f, 6},
    { 11.0f,  0.0f, 7},
    {-22.0f, -19.1f, 1},
    {-22.0f,  19.1f, 1},
    {-33.0f,  0.0f, 1},
    { 22.0f, -17.4f, 1},
    { 22.0f,  17.4f, 1},
};
// NaHCO3: baking soda
static const MoleculeAtom NaHCO3_ATOMS[] = {
    { 0.0f,   0.0f, 6},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 8},
    { 19.1f, 11.0f, 8},
    {-38.2f, 0.0f, 11},
    { 30.1f, 28.4f, 1},
};
// H2SO4: sulfuric acid
static const MoleculeAtom H2SO4_ATOMS[] = {
    { 0.0f,   0.0f, 16},
    { 0.0f, -22.0f, 8},
    { 0.0f,  22.0f, 8},
    {-22.0f,  0.0f, 8},
    { 22.0f,  0.0f, 8},
    {-33.0f, -17.4f, 1},
    { 33.0f, -17.4f, 1},
};
// H3PO4: phosphoric acid
static const MoleculeAtom H3PO4_ATOMS[] = {
    { 0.0f,   0.0f, 15},
    { 0.0f, -22.0f, 8},
    {-19.1f, 11.0f, 8},
    { 19.1f, 11.0f, 8},
    { 0.0f,  22.0f, 8},
    {-30.1f, 28.4f, 1},
    { 30.1f, 28.4f, 1},
    { 0.0f,  44.0f, 1},
};
// C2H6: ethane
static const MoleculeAtom C2H6_ATOMS[] = {
    {-11.0f,  0.0f, 6},
    { 11.0f,  0.0f, 6},
    {-22.0f, -19.1f, 1},
    {-22.0f,  19.1f, 1},
    {-33.0f,  0.0f, 1},
    { 22.0f, -19.1f, 1},
    { 22.0f,  19.1f, 1},
    { 33.0f,  0.0f, 1},
};
// C2H5OH: ethanol
static const MoleculeAtom C2H5OH_ATOMS[] = {
    {-11.0f,  0.0f, 6},
    { 11.0f,  0.0f, 6},
    { 33.0f,  0.0f, 8},
    { 44.0f, 17.4f, 1},
    {-22.0f, -19.1f, 1},
    {-22.0f,  19.1f, 1},
    {-33.0f,  0.0f, 1},
    { 11.0f, -22.0f, 1},
    { 11.0f,  22.0f, 1},
};
// CH3COOH: acetic acid
static const MoleculeAtom CH3COOH_ATOMS[] = {
    {-11.0f,  0.0f, 6},
    { 11.0f,  0.0f, 6},
    { 11.0f, -22.0f, 8},
    { 33.0f,  0.0f, 8},
    {-22.0f, -19.1f, 1},
    {-22.0f,  19.1f, 1},
    {-33.0f,  0.0f, 1},
    { 44.0f,  17.4f, 1},
};

// ── 10+ atom ─────────────────────────────────────────────────────────────────
// C3H8: propane
static const MoleculeAtom C3H8_ATOMS[] = {
    {-22.0f,  0.0f, 6},
    {  0.0f,  0.0f, 6},
    { 22.0f,  0.0f, 6},
    {-33.0f, -19.1f, 1},
    {-33.0f,  19.1f, 1},
    {-44.0f,  0.0f, 1},
    {  0.0f, -22.0f, 1},
    {  0.0f,  22.0f, 1},
    { 33.0f, -19.1f, 1},
    { 33.0f,  19.1f, 1},
    { 44.0f,  0.0f, 1},
};
// C3H6O: acetone CH3-CO-CH3
static const MoleculeAtom C3H6O_ATOMS[] = {
    { 0.0f,   0.0f, 6},
    { 0.0f, -22.0f, 8},
    {-22.0f,  0.0f, 6},
    { 22.0f,  0.0f, 6},
    {-33.0f, -19.1f, 1},
    {-33.0f,  19.1f, 1},
    {-44.0f,  0.0f, 1},
    { 33.0f, -19.1f, 1},
    { 33.0f,  19.1f, 1},
    { 44.0f,  0.0f, 1},
};
// CH3OCH3: dimethyl ether C-O-C
static const MoleculeAtom CH3OCH3_ATOMS[] = {
    { 0.0f,   0.0f, 8},
    {-19.1f, -11.0f, 6},
    { 19.1f, -11.0f, 6},
    {-30.1f, -28.4f, 1},
    {-30.1f,   6.4f, 1},
    {-41.1f, -11.0f, 1},
    { 30.1f, -28.4f, 1},
    { 30.1f,   6.4f, 1},
    { 41.1f, -11.0f, 1},
};
// C6H6: benzene — hexagonal ring
static const MoleculeAtom C6H6_ATOMS[] = {
    { 22.0f,   0.0f, 6},
    { 11.0f,  19.1f, 6},
    {-11.0f,  19.1f, 6},
    {-22.0f,   0.0f, 6},
    {-11.0f, -19.1f, 6},
    { 11.0f, -19.1f, 6},
    { 44.0f,   0.0f, 1},
    { 22.0f,  38.1f, 1},
    {-22.0f,  38.1f, 1},
    {-44.0f,   0.0f, 1},
    {-22.0f, -38.1f, 1},
    { 22.0f, -38.1f, 1},
};
// C6H12O6: glucose (pyranose ring, simplified 2D)
static const MoleculeAtom C6H12O6_ATOMS[] = {
    // Ring: C1-C2-C3-C4-C5-O
    { 22.0f,   0.0f, 6},
    { 11.0f,  19.1f, 6},
    {-11.0f,  19.1f, 6},
    {-22.0f,   0.0f, 6},
    {-11.0f, -19.1f, 6},
    { 11.0f, -19.1f, 8},   // ring O
    // Exocyclic C6 on C5
    {-33.0f, -19.1f, 6},
    // OH groups on C1,C2,C3,C4,C6
    { 44.0f,   0.0f, 8},
    { 22.0f,  38.1f, 8},
    {-22.0f,  38.1f, 8},
    {-44.0f,   0.0f, 8},
    {-44.0f, -38.1f, 8},
    // H on ring carbons + C6 + OH groups (12 total)
    { 22.0f, -22.0f, 1},
    {  0.0f,  30.1f, 1},
    {-22.0f,  30.1f, 1},
    {-33.0f, -11.0f, 1},
    {  0.0f, -30.1f, 1},
    {-33.0f, -41.1f, 1},
    { 55.0f,  11.0f, 1},
    { 33.0f,  49.1f, 1},
    {-33.0f,  49.1f, 1},
    {-55.0f,  11.0f, 1},
    {-55.0f, -49.1f, 1},
    {-44.0f, -22.0f, 1},
};

// ── Master template table ────────────────────────────────────────────────────

static const MoleculeTemplate MOLECULE_TEMPLATES[] = {
    // Diatomic
    {"H2",      "Hydrogen gas",           H2_ATOMS,       2},
    {"O2",      "Oxygen gas",             O2_ATOMS,       2},
    {"N2",      "Nitrogen gas",           N2_ATOMS,       2},
    {"F2",      "Fluorine gas",           F2_ATOMS,       2},
    {"Cl2",     "Chlorine gas",           Cl2_ATOMS,      2},
    {"HF",      "Hydrogen fluoride",      HF_ATOMS,       2},
    {"HCl",     "Hydrochloric acid",      HCl_ATOMS,      2},
    {"CO",      "Carbon monoxide",        CO_ATOMS,       2},
    {"NO",      "Nitric oxide",           NO_ATOMS,       2},
    {"NaCl",    "Sodium chloride",        NaCl_ATOMS,     2},
    {"MgO",     "Magnesium oxide",        MgO_ATOMS,      2},
    {"CaO",     "Calcium oxide",          CaO_ATOMS,      2},
    // Triatomic
    {"H2O",     "Water",                  H2O_ATOMS,      3},
    {"CO2",     "Carbon dioxide",         CO2_ATOMS,      3},
    {"NO2",     "Nitrogen dioxide",       NO2_ATOMS,      3},
    {"H2S",     "Hydrogen sulfide",       H2S_ATOMS,      3},
    {"O3",      "Ozone",                  O3_ATOMS,       3},
    {"SO2",     "Sulfur dioxide",         SO2_ATOMS,      3},
    {"CS2",     "Carbon disulfide",       CS2_ATOMS,      3},
    {"N2O",     "Nitrous oxide",          N2O_ATOMS,      3},
    {"HCN",     "Hydrogen cyanide",       HCN_ATOMS,      3},
    {"SiO2",    "Silicon dioxide",        SiO2_ATOMS,     3},
    // 4-atom
    {"NH3",     "Ammonia",                NH3_ATOMS,      4},
    {"PH3",     "Phosphine",              PH3_ATOMS,      4},
    {"BF3",     "Boron trifluoride",      BF3_ATOMS,      4},
    {"SO3",     "Sulfur trioxide",        SO3_ATOMS,      4},
    {"H2O2",    "Hydrogen peroxide",      H2O2_ATOMS,     4},
    {"NaOH",    "Sodium hydroxide",       NaOH_ATOMS,     3},
    {"COCl2",   "Phosgene",              COCl2_ATOMS,    4},
    // 5-atom
    {"CH4",     "Methane",                CH4_ATOMS,      5},
    {"SiH4",    "Silane",                 SiH4_ATOMS,     5},
    {"CF4",     "Carbon tetrafluoride",   CF4_ATOMS,      5},
    {"CCl4",    "Carbon tetrachloride",   CCl4_ATOMS,     5},
    {"CH3Cl",   "Chloromethane",          CH3Cl_ATOMS,    5},
    {"CH2Cl2",  "Dichloromethane",        CH2Cl2_ATOMS,   5},
    {"CHCl3",   "Chloroform",             CHCl3_ATOMS,    5},
    {"HNO3",    "Nitric acid",            HNO3_ATOMS,     5},
    // 6-9 atom
    {"CH2O",    "Formaldehyde",           CH2O_ATOMS,     4},
    {"CH3OH",   "Methanol",               CH3OH_ATOMS,    6},
    {"C2H2",    "Acetylene",              C2H2_ATOMS,     4},
    {"C2H4",    "Ethylene",               C2H4_ATOMS,     6},
    {"HCOOH",   "Formic acid",            HCOOH_ATOMS,    5},
    {"CH3NH2",  "Methylamine",            CH3NH2_ATOMS,   7},
    {"NaHCO3",  "Baking soda",            NaHCO3_ATOMS,   6},
    {"H2SO4",   "Sulfuric acid",          H2SO4_ATOMS,    7},
    {"H3PO4",   "Phosphoric acid",        H3PO4_ATOMS,    8},
    {"C2H6",    "Ethane",                 C2H6_ATOMS,     8},
    {"C2H5OH",  "Ethanol",                C2H5OH_ATOMS,   9},
    {"CH3COOH", "Acetic acid",            CH3COOH_ATOMS,  8},
    // 10+ atom
    {"C3H8",    "Propane",                C3H8_ATOMS,     11},
    {"C3H6O",   "Acetone",                C3H6O_ATOMS,    10},
    {"CH3OCH3", "Dimethyl ether",         CH3OCH3_ATOMS,  9},
    {"C6H6",    "Benzene",                C6H6_ATOMS,     12},
    {"C6H12O6", "Glucose",                C6H12O6_ATOMS,  24},
};
static constexpr int MOLECULE_TEMPLATE_COUNT =
    static_cast<int>(sizeof(MOLECULE_TEMPLATES) / sizeof(MOLECULE_TEMPLATES[0]));

// Implementation of find_molecule_template (declared above, after MOLECULE_TEMPLATES is defined)
inline int find_molecule_template(const char* formula) {
    for (int i = 0; i < MOLECULE_TEMPLATE_COUNT; ++i) {
        if (strcmp(MOLECULE_TEMPLATES[i].formula, formula) == 0) return i;
    }
    return -1;
}
