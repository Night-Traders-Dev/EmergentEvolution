#include "physics/interface.h"
#include "physics/phys_particles.h"
#include <imgui.h>
#include <cmath>
#include <random>
#include <cstdio>

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

// ── Particle name/color tables for all 30 types ─────────────────────────────

static const char* const PHYS_TYPE_NAMES[PHYS_PARTICLE_TYPES] = {
    "Proton", "Neutron", "Electron", "Photon", "Positron", "Antiproton",
    "Neutrino_e",
    "Muon", "Anti-muon", "Tau", "Anti-tau", "Neutrino_mu", "Neutrino_tau",
    "Up", "Down", "Strange", "Charm", "Top", "Bottom",
    "Anti-up", "Anti-down", "Anti-strange", "Anti-charm", "Anti-top", "Anti-bottom",
    "Gluon", "W+", "W-", "Z0", "Higgs",
};

static const char* const PHYS_TYPE_LABELS[PHYS_PARTICLE_TYPES] = {
    "p", "n", "e-", "y", "e+", "p-",
    "ve",
    "mu-", "mu+", "tau-", "tau+", "vmu", "vtau",
    "u", "d", "s", "c", "t", "b",
    "u~", "d~", "s~", "c~", "t~", "b~",
    "g", "W+", "W-", "Z0", "H0",
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
};

// ── Temperature formatting ───────────────────────────────────────────────────

static void format_temperature(float kelvin, char* buf, int buf_size) {
    if (kelvin < 1000.0f) {
        snprintf(buf, buf_size, "%.0f K", kelvin);
    } else {
        const char* prefix = "";
        float display = kelvin;
        if (kelvin >= 1e12f)      { display = kelvin / 1e12f; prefix = "T"; }
        else if (kelvin >= 1e9f)  { display = kelvin / 1e9f;  prefix = "G"; }
        else if (kelvin >= 1e6f)  { display = kelvin / 1e6f;  prefix = "M"; }
        else if (kelvin >= 1e3f)  { display = kelvin / 1e3f;  prefix = "k"; }
        snprintf(buf, buf_size, "%.1f %sK", display, prefix);
    }
}

// ── Theme: dark navy + cyan accents ──────────────────────────────────────────

static constexpr int THEME_COLOR_COUNT = 33;
static constexpr int THEME_VAR_COUNT   = 10;

void PhysicsInterface::push_theme() {
    // Colors
    ImGui::PushStyleColor(ImGuiCol_WindowBg,        ImVec4(0.059f, 0.071f, 0.110f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,          ImVec4(0.059f, 0.071f, 0.110f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,          ImVec4(0.059f, 0.071f, 0.110f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,           ImVec4(0.180f, 0.220f, 0.349f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow,     ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,          ImVec4(0.098f, 0.118f, 0.180f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,   ImVec4(0.137f, 0.165f, 0.259f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,    ImVec4(0.180f, 0.220f, 0.349f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,          ImVec4(0.039f, 0.051f, 0.090f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    ImVec4(0.059f, 0.071f, 0.130f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.039f, 0.051f, 0.090f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg,        ImVec4(0.059f, 0.071f, 0.110f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,      ImVec4(0.039f, 0.051f, 0.090f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,    ImVec4(0.180f, 0.220f, 0.349f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.302f, 0.749f, 0.953f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  ImVec4(0.302f, 0.749f, 0.953f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,        ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.302f, 0.749f, 0.953f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.400f, 0.820f, 1.000f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button,           ImVec4(0.118f, 0.161f, 0.259f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,    ImVec4(0.180f, 0.240f, 0.380f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,     ImVec4(0.302f, 0.749f, 0.953f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_Header,           ImVec4(0.118f, 0.161f, 0.259f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,    ImVec4(0.180f, 0.240f, 0.380f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,     ImVec4(0.302f, 0.749f, 0.953f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Separator,        ImVec4(0.180f, 0.220f, 0.349f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, ImVec4(0.302f, 0.749f, 0.953f, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorActive,  ImVec4(0.302f, 0.749f, 0.953f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Tab,              ImVec4(0.098f, 0.118f, 0.180f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,       ImVec4(0.180f, 0.240f, 0.380f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,      ImVec4(0.302f, 0.749f, 0.953f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Text,             ImVec4(0.820f, 0.851f, 0.922f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,     ImVec4(0.451f, 0.478f, 0.580f, 1.0f));

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

void PhysicsInterface::render_imgui(SimConfig& cfg, Particles& particles, bool& request_reset) {
    // F1 toggle settings panel
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        settings_visible = !settings_visible;

    push_theme();

    // Draw bottom bar (always visible)
    draw_bottom_bar(cfg, request_reset);

    // Draw settings panel
    if (settings_visible)
        draw_settings_panel(cfg);

    // Draw spawn menu
    if (spawn_menu_visible)
        draw_spawn_menu(cfg);

    // Draw info card (hover)
    draw_info_card(particles);

    pop_theme();
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.039f, 0.051f, 0.090f, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

    if (ImGui::Begin("##BottomBar", nullptr, bar_flags)) {
        // Sim state indicator
        if (sim_running) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), ">> RUNNING");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "|| PAUSED");
        }

        // Temperature
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        char temp_buf[64];
        format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", temp_buf);

        // FPS
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        ImGui::Text("%.0f fps", fps_display);

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

        // Right-aligned buttons
        float btn_width = 80.0f;
        float x_right = display_w - 12.0f - btn_width * 2 - 8.0f;
        ImGui::SameLine(x_right);

        if (ImGui::Button("Reset (F2)", ImVec2(btn_width, 26))) {
            request_reset = true;
        }
        ImGui::SameLine(0, 8);
        if (ImGui::Button("Spawn (F3)", ImVec2(btn_width, 26))) {
            spawn_menu_visible = !spawn_menu_visible;
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
                    cfg.temperature_kelvin = 2.7f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    cfg.lorentz_strength = 0.0f;
                    cfg.weak_coupling = 0.0f;
                    cfg.string_tension = 50.0f;
                    cfg.viscosity_strength = 0.0f;
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

    // ── Physics ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Temp (log)", &log_temperature, 0.0f, 13.0f, "%.2f");
        cfg.temperature_kelvin = std::pow(10.0f, log_temperature);

        char temp_buf[64];
        format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf));
        float celsius = cfg.temperature_kelvin - 273.15f;
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "  %s  (%.0f C)", temp_buf, celsius);

        ImGui::SliderFloat("Dampening", &cfg.dampening, 0.50f, 0.99f, "%.3f");
        ImGui::SliderFloat("Gravity", &cfg.gravity_strength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Magnetism", &cfg.lorentz_strength, 0.0f, 2.0f, "%.2f");
        ImGui::Spacing();
        ImGui::SliderFloat("Repulsion R", &cfg.repulsion_radius, 1.0f, 40.0f, "%.1f");
        ImGui::SliderFloat("Interact R", &cfg.interaction_radius, 20.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Pressure", &cfg.pressure_resistance, 5.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("Radius", &cfg.radius, 0.5f, 8.0f, "%.1f");
    }

    // ── QCD / Weak ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("QCD / Weak")) {
        ImGui::SliderFloat("String Tension", &cfg.string_tension, 0.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quark confinement strength (Cornell potential linear term)");

        ImGui::SliderFloat("Weak Coupling", &cfg.weak_coupling, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Weak nuclear force strength (very short range)");

        ImGui::SliderFloat("Higgs VEV", &cfg.higgs_vev, 0.0f, 500.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higgs vacuum expectation value (mass coupling scale)");
    }

    // ── Field Visualization ──────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Field Visualization")) {
        ImGui::Checkbox("Electromagnetic", &field_em);
        ImGui::SameLine();
        ImGui::Checkbox("Strong", &field_strong);

        ImGui::Checkbox("Weak", &field_weak);
        ImGui::SameLine();
        ImGui::Checkbox("Gravity", &field_gravity);

        ImGui::Checkbox("Higgs", &field_higgs);

        if (field_em || field_strong || field_weak || field_gravity || field_higgs)
            ImGui::SliderFloat("Intensity", &field_intensity, 0.05f, 2.0f, "%.2f");
    }

    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Spawn Menu (Consolidated with collapsing headers) ───────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_spawn_menu(const SimConfig& /*cfg*/) {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
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
    if (hover_particle_idx < 0) return;
    uint32_t idx = static_cast<uint32_t>(hover_particle_idx);
    if (idx >= particles.types.size()) return;

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
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, 10));

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
    }
    ImGui::End();
}
