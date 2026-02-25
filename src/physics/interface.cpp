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

// Pion (π+): u + d-bar
static const SubAtomicSpec PION_PLUS[] = {
    { -3, 0, UP_QUARK_TYPE },
    {  3, 0, ANTI_DOWN_TYPE },
};

// Pion (π-): d + u-bar
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
    float celsius = kelvin - 273.15f;
    if (kelvin < 1000.0f) {
        snprintf(buf, buf_size, "%.0f K  (%.0f C)", kelvin, celsius);
    } else if (kelvin < 1e6f) {
        snprintf(buf, buf_size, "%.1e K  (%.0f C)", kelvin, celsius);
    } else {
        // Use engineering notation
        const char* prefix = "";
        float display = kelvin;
        if (kelvin >= 1e12f)      { display = kelvin / 1e12f; prefix = "T"; }
        else if (kelvin >= 1e9f)  { display = kelvin / 1e9f;  prefix = "G"; }
        else if (kelvin >= 1e6f)  { display = kelvin / 1e6f;  prefix = "M"; }
        snprintf(buf, buf_size, "%.1f %sK", display, prefix);
    }
}

// ── Helper: draw a particle spawn button ─────────────────────────────────────

static bool spawn_button(int type_idx, const char* label, ImVec4 color,
                          int current_spawn_type, int current_spawn_group,
                          const char* tooltip, ImVec2 size = ImVec2(45, 32))
{
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, 1.0f));

    bool selected = (current_spawn_type == type_idx && current_spawn_group == -1);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }

    bool clicked = ImGui::Button(label, size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);

    if (selected) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor(2);

    return clicked;
}

// ── Main UI ──────────────────────────────────────────────────────────────────

void PhysicsInterface::render_imgui(SimConfig& cfg, Particles& particles, bool& request_reset) {
    // F1 toggle settings panel
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        settings_visible = !settings_visible;

    if (!settings_visible) return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 750), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Particle Physics", &settings_visible)) {
        ImGui::End();
        return;
    }

    // ── Statistics ───────────────────────────────────────────────────────────
    ImGui::Text("FPS: %.0f", fps_display);
    ImGui::Text("Active: %u  Dormant: %u", active_particle_display, dormant_particle_display);
    ImGui::Text("Energy: %.1f avg  (%.0f total)", avg_energy_display, total_energy_display);
    ImGui::Separator();

    // Type counts grouped by family
    // Nucleons
    bool any_nucleon = type_counts_display[PROTON_TYPE] || type_counts_display[NEUTRON_TYPE]
                     || type_counts_display[ANTIPROTON_TYPE_PHYS];
    if (any_nucleon) {
        if (type_counts_display[PROTON_TYPE])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[PROTON_TYPE], "p:%u", type_counts_display[PROTON_TYPE]); ImGui::SameLine(); }
        if (type_counts_display[NEUTRON_TYPE])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[NEUTRON_TYPE], "n:%u", type_counts_display[NEUTRON_TYPE]); ImGui::SameLine(); }
        if (type_counts_display[ANTIPROTON_TYPE_PHYS])
            { ImGui::TextColored(PHYS_TYPE_UI_COLORS[ANTIPROTON_TYPE_PHYS], "p-:%u", type_counts_display[ANTIPROTON_TYPE_PHYS]); ImGui::SameLine(); }
    }

    // Leptons
    static const uint32_t LEPTON_INDICES[] = {
        ELECTRON_TYPE_PHYS, POSITRON_TYPE_PHYS,
        MUON_TYPE_PHYS, ANTIMUON_TYPE_PHYS,
        TAU_TYPE_PHYS, ANTITAU_TYPE_PHYS,
        NEUTRINO_TYPE_PHYS, MU_NEUTRINO_TYPE_PHYS, TAU_NEUTRINO_TYPE_PHYS
    };
    for (uint32_t t : LEPTON_INDICES) {
        if (type_counts_display[t]) {
            ImGui::TextColored(PHYS_TYPE_UI_COLORS[t], "%s:%u",
                PHYS_TYPE_LABELS[t], type_counts_display[t]);
            ImGui::SameLine();
        }
    }

    // Quarks
    for (uint32_t t = UP_QUARK_TYPE; t <= ANTI_BOTTOM_TYPE; ++t) {
        if (type_counts_display[t]) {
            ImGui::TextColored(PHYS_TYPE_UI_COLORS[t], "%s:%u",
                PHYS_TYPE_LABELS[t], type_counts_display[t]);
            ImGui::SameLine();
        }
    }

    // Bosons
    static const uint32_t BOSON_INDICES[] = {
        PHOTON_TYPE_PHYS, GLUON_TYPE_PHYS,
        W_PLUS_TYPE_PHYS, W_MINUS_TYPE_PHYS,
        Z_BOSON_TYPE_PHYS, HIGGS_TYPE_PHYS
    };
    for (uint32_t t : BOSON_INDICES) {
        if (type_counts_display[t]) {
            ImGui::TextColored(PHYS_TYPE_UI_COLORS[t], "%s:%u",
                PHYS_TYPE_LABELS[t], type_counts_display[t]);
            ImGui::SameLine();
        }
    }

    ImGui::NewLine();
    ImGui::Separator();

    // ── Environment ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        int env = static_cast<int>(cfg.environment_mode);
        if (ImGui::Combo("Preset", &env, PHYS_ENV_NAMES, PHYS_ENV_COUNT)) {
            cfg.environment_mode = static_cast<uint32_t>(env);
            switch (env) {
                case 0:  // Lab Mode — particle physics vacuum
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
                case 1:  // Hydrogen Plasma — 15 million K, ionized H
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 100.0f;
                    break;
                case 2:  // Neutron Star Surface — 1 billion K, extreme density
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e9f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 0.5f;
                    particle_count_slider = 120.0f;
                    break;
                case 3:  // Solar Core — 15 million K, gravitational compression
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.2f;
                    particle_count_slider = 110.0f;
                    break;
                case 4:  // Particle Soup — warm, mixed particles
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5000.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 80.0f;
                    break;
                case 5:  // Alpha Emitter — low temp, alpha decay
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 300.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 0.0f;
                    particle_count_slider = 60.0f;
                    break;
                case 6:  // Heavy Nucleus — cold, dense nuclear matter
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 100.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 0.3f;
                    particle_count_slider = 50.0f;
                    break;
                case 7:  // Quark-Gluon Plasma — 2 trillion K, deconfinement
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2e12f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 0.0f;
                    cfg.string_tension = 10.0f;  // weakened — deconfinement
                    cfg.weak_coupling = 0.5f;
                    particle_count_slider = 100.0f;
                    break;
                case 8:  // Electroweak Era — 10^15 K, unified EM+weak
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
                case 9:  // Meson Factory — quark-antiquark pairs
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
            // Sync log slider to new kelvin
            log_temperature = std::log10(std::max(1.0f, cfg.temperature_kelvin));
        }

        if (!cfg.start_empty) {
            ImGui::SliderFloat("Count", &particle_count_slider, 1.0f, 317.0f, "%.0f");
            int pc = static_cast<int>(std::max(2.0f, std::pow(particle_count_slider, 2.0f)));
            ImGui::Text("Particles: %d  (applied on Reset)", pc);
        } else {
            ImGui::Text("Particles: %u  (Lab Mode)", cfg.particle_count);
        }

        ImGui::SliderInt("Seed", &seed_value, 0, 99999);
        cfg.generation_seed = static_cast<uint32_t>(seed_value);
    }

    // ── Physics Sliders ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Temperature: logarithmic Kelvin slider
        ImGui::SliderFloat("Temp (log)", &log_temperature, 0.0f, 13.0f, "%.2f");
        cfg.temperature_kelvin = std::pow(10.0f, log_temperature);

        char temp_buf[64];
        format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf));
        ImGui::Text("  %s", temp_buf);

        ImGui::SliderFloat("Dampening", &cfg.dampening, 0.50f, 0.99f, "%.3f");
        ImGui::SliderFloat("Gravity", &cfg.gravity_strength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Magnetism", &cfg.lorentz_strength, 0.0f, 2.0f, "%.2f");
        ImGui::Separator();
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

    // ── Quantum Fields (5 checkboxes) ────────────────────────────────────────
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

    // ── Controls ─────────────────────────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::Button("Reset (F2)")) {
        request_reset = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Spawn (F3)")) {
        spawn_menu_visible = !spawn_menu_visible;
    }

    ImGui::End();

    // ── Spawn Menu ───────────────────────────────────────────────────────────
    if (spawn_menu_visible)
        draw_spawn_menu(cfg);

    // ── Hover tooltip ────────────────────────────────────────────────────────
    draw_hover_tooltip(particles);
}

// ── F3 Spawn Menu ────────────────────────────────────────────────────────────

void PhysicsInterface::draw_spawn_menu(const SimConfig& /*cfg*/) {
    ImGui::SetNextWindowPos(ImVec2(380, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Spawn Particles", &spawn_menu_visible)) {
        ImGui::End();
        return;
    }

    ImGui::BeginTabBar("SpawnTabs");

    // ── Leptons tab ──────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Leptons")) {
        ImGui::Text("Generation 1:");
        // e-, e+, ve
        if (spawn_button(ELECTRON_TYPE_PHYS, "e-", PHYS_TYPE_UI_COLORS[ELECTRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron"))
            { spawn_type = ELECTRON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(POSITRON_TYPE_PHYS, "e+", PHYS_TYPE_UI_COLORS[POSITRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Positron"))
            { spawn_type = POSITRON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRINO_TYPE_PHYS, "ve", PHYS_TYPE_UI_COLORS[NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron neutrino"))
            { spawn_type = NEUTRINO_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Text("Generation 2:");
        if (spawn_button(MUON_TYPE_PHYS, "mu-", PHYS_TYPE_UI_COLORS[MUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon (decays ~100 frames)"))
            { spawn_type = MUON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIMUON_TYPE_PHYS, "mu+", PHYS_TYPE_UI_COLORS[ANTIMUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-muon"))
            { spawn_type = ANTIMUON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(MU_NEUTRINO_TYPE_PHYS, "vmu", PHYS_TYPE_UI_COLORS[MU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon neutrino"))
            { spawn_type = MU_NEUTRINO_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Text("Generation 3:");
        if (spawn_button(TAU_TYPE_PHYS, "tau-", PHYS_TYPE_UI_COLORS[TAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau (decays ~5 frames)"))
            { spawn_type = TAU_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTITAU_TYPE_PHYS, "tau+", PHYS_TYPE_UI_COLORS[ANTITAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-tau"))
            { spawn_type = ANTITAU_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TAU_NEUTRINO_TYPE_PHYS, "vtau", PHYS_TYPE_UI_COLORS[TAU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau neutrino"))
            { spawn_type = TAU_NEUTRINO_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::Text("Composites:");
        if (spawn_button(PROTON_TYPE, "p", PHYS_TYPE_UI_COLORS[PROTON_TYPE],
                          spawn_type, spawn_group, "Proton"))
            { spawn_type = PROTON_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRON_TYPE, "n", PHYS_TYPE_UI_COLORS[NEUTRON_TYPE],
                          spawn_type, spawn_group, "Neutron"))
            { spawn_type = NEUTRON_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIPROTON_TYPE_PHYS, "p-", PHYS_TYPE_UI_COLORS[ANTIPROTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Antiproton"))
            { spawn_type = ANTIPROTON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::SliderInt("Count", &spawn_count, 1, 100);
        ImGui::SliderFloat("Energy", &spawn_energy, 0.1f, 1.0f, "%.2f");
        ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 100.0f, "%.0f");

        if (pending_spawn && spawn_group == -1)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Click in world to place");

        ImGui::EndTabItem();
    }

    // ── Quarks tab ───────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Quarks")) {
        ImGui::Text("Quarks:");
        if (spawn_button(UP_QUARK_TYPE, "u", PHYS_TYPE_UI_COLORS[UP_QUARK_TYPE],
                          spawn_type, spawn_group, "Up quark (+2/3)"))
            { spawn_type = UP_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DOWN_QUARK_TYPE, "d", PHYS_TYPE_UI_COLORS[DOWN_QUARK_TYPE],
                          spawn_type, spawn_group, "Down quark (-1/3)"))
            { spawn_type = DOWN_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(STRANGE_QUARK_TYPE, "s", PHYS_TYPE_UI_COLORS[STRANGE_QUARK_TYPE],
                          spawn_type, spawn_group, "Strange quark (-1/3, decays)"))
            { spawn_type = STRANGE_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }

        if (spawn_button(CHARM_QUARK_TYPE, "c", PHYS_TYPE_UI_COLORS[CHARM_QUARK_TYPE],
                          spawn_type, spawn_group, "Charm quark (+2/3, decays fast)"))
            { spawn_type = CHARM_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TOP_QUARK_TYPE, "t", PHYS_TYPE_UI_COLORS[TOP_QUARK_TYPE],
                          spawn_type, spawn_group, "Top quark (+2/3, instant decay)"))
            { spawn_type = TOP_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(BOTTOM_QUARK_TYPE, "b", PHYS_TYPE_UI_COLORS[BOTTOM_QUARK_TYPE],
                          spawn_type, spawn_group, "Bottom quark (-1/3, decays)"))
            { spawn_type = BOTTOM_QUARK_TYPE; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::Text("Antiquarks:");
        if (spawn_button(ANTI_UP_TYPE, "u~", PHYS_TYPE_UI_COLORS[ANTI_UP_TYPE],
                          spawn_type, spawn_group, "Anti-up (-2/3)"))
            { spawn_type = ANTI_UP_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_DOWN_TYPE, "d~", PHYS_TYPE_UI_COLORS[ANTI_DOWN_TYPE],
                          spawn_type, spawn_group, "Anti-down (+1/3)"))
            { spawn_type = ANTI_DOWN_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_STRANGE_TYPE, "s~", PHYS_TYPE_UI_COLORS[ANTI_STRANGE_TYPE],
                          spawn_type, spawn_group, "Anti-strange (+1/3)"))
            { spawn_type = ANTI_STRANGE_TYPE; spawn_group = -1; pending_spawn = true; }

        if (spawn_button(ANTI_CHARM_TYPE, "c~", PHYS_TYPE_UI_COLORS[ANTI_CHARM_TYPE],
                          spawn_type, spawn_group, "Anti-charm (-2/3)"))
            { spawn_type = ANTI_CHARM_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_TOP_TYPE, "t~", PHYS_TYPE_UI_COLORS[ANTI_TOP_TYPE],
                          spawn_type, spawn_group, "Anti-top (-2/3)"))
            { spawn_type = ANTI_TOP_TYPE; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_BOTTOM_TYPE, "b~", PHYS_TYPE_UI_COLORS[ANTI_BOTTOM_TYPE],
                          spawn_type, spawn_group, "Anti-bottom (+1/3)"))
            { spawn_type = ANTI_BOTTOM_TYPE; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::SliderInt("Count", &spawn_count, 1, 100);
        ImGui::SliderFloat("Energy", &spawn_energy, 0.1f, 1.0f, "%.2f");
        ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 100.0f, "%.0f");

        if (pending_spawn && spawn_group == -1)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Click in world to place");

        ImGui::EndTabItem();
    }

    // ── Bosons tab ───────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Bosons")) {
        if (spawn_button(PHOTON_TYPE_PHYS, "y", PHYS_TYPE_UI_COLORS[PHOTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Photon (massless, stable)"))
            { spawn_type = PHOTON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(GLUON_TYPE_PHYS, "g", PHYS_TYPE_UI_COLORS[GLUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Gluon (strong force mediator)"))
            { spawn_type = GLUON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::Text("Weak / Scalar:");
        if (spawn_button(W_PLUS_TYPE_PHYS, "W+", PHYS_TYPE_UI_COLORS[W_PLUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W+ boson (instant decay)"))
            { spawn_type = W_PLUS_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(W_MINUS_TYPE_PHYS, "W-", PHYS_TYPE_UI_COLORS[W_MINUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W- boson (instant decay)"))
            { spawn_type = W_MINUS_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(Z_BOSON_TYPE_PHYS, "Z0", PHYS_TYPE_UI_COLORS[Z_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "Z0 boson (instant decay)"))
            { spawn_type = Z_BOSON_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        if (spawn_button(HIGGS_TYPE_PHYS, "H0", PHYS_TYPE_UI_COLORS[HIGGS_TYPE_PHYS],
                          spawn_type, spawn_group, "Higgs boson (instant decay)"))
            { spawn_type = HIGGS_TYPE_PHYS; spawn_group = -1; pending_spawn = true; }

        ImGui::Separator();
        ImGui::SliderInt("Count", &spawn_count, 1, 100);
        ImGui::SliderFloat("Energy", &spawn_energy, 0.1f, 1.0f, "%.2f");
        ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 100.0f, "%.0f");

        if (pending_spawn && spawn_group == -1)
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Click in world to place");

        ImGui::EndTabItem();
    }

    // ── Atoms tab (group templates) ──────────────────────────────────────────
    if (ImGui::BeginTabItem("Atoms")) {
        for (int g = 0; g < GROUP_TEMPLATE_COUNT_VAL; ++g) {
            bool selected = (spawn_group == g);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            }

            if (ImGui::Button(GROUP_TEMPLATES[g].label, ImVec2(50, 30))) {
                spawn_group = g;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u particles)", GROUP_TEMPLATES[g].name, GROUP_TEMPLATES[g].count);

            if (selected) ImGui::PopStyleColor();
            if ((g + 1) % 4 != 0 && g < GROUP_TEMPLATE_COUNT_VAL - 1) ImGui::SameLine();
        }

        if (pending_spawn && spawn_group >= 0 && spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f),
                "Click to place %s", GROUP_TEMPLATES[spawn_group].name);
        }

        ImGui::EndTabItem();
    }

    // ── Hadrons tab (quark-level composites) ─────────────────────────────────
    if (ImGui::BeginTabItem("Hadrons")) {
        ImGui::Text("Baryons (3 quarks):");
        for (int h = 0; h < HADRON_TEMPLATE_COUNT_VAL; ++h) {
            // Separator between baryons and mesons
            if (h == 7) {
                ImGui::Separator();
                ImGui::Text("Mesons (quark + antiquark):");
            }

            int group_id = GROUP_TEMPLATE_COUNT_VAL + h;
            bool selected = (spawn_group == group_id);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            }

            if (ImGui::Button(HADRON_TEMPLATES[h].label, ImVec2(50, 30))) {
                spawn_group = group_id;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u quarks)", HADRON_TEMPLATES[h].name, HADRON_TEMPLATES[h].count);

            if (selected) ImGui::PopStyleColor();

            // Layout: 4 per row for baryons, 4 per row for mesons
            int row_idx = (h < 7) ? h : (h - 7);
            int row_count = (h < 7) ? 7 : (HADRON_TEMPLATE_COUNT_VAL - 7);
            if ((row_idx + 1) % 4 != 0 && row_idx < row_count - 1) ImGui::SameLine();
        }

        if (pending_spawn && spawn_group >= GROUP_TEMPLATE_COUNT_VAL) {
            int h_idx = spawn_group - GROUP_TEMPLATE_COUNT_VAL;
            if (h_idx < HADRON_TEMPLATE_COUNT_VAL) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f),
                    "Click to place %s", HADRON_TEMPLATES[h_idx].name);
            }
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

// ── Hover Tooltip ────────────────────────────────────────────────────────────

void PhysicsInterface::draw_hover_tooltip(const Particles& particles) {
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

    ImGui::BeginTooltip();
    ImGui::Text("%s (#%u)", name, idx);
    ImGui::Text("Charge: %+.2f  Spin: %+.1f", charge, spin);

    // Color charge for quarks
    if (ptype >= UP_QUARK_TYPE && ptype <= ANTI_BOTTOM_TYPE) {
        int cc = static_cast<int>(color_charge);
        const char* color_name = "?";
        if (cc == 1 || cc == -1)  color_name = (cc > 0) ? "Red" : "Anti-Red";
        if (cc == 2 || cc == -2)  color_name = (cc > 0) ? "Green" : "Anti-Green";
        if (cc == 3 || cc == -3)  color_name = (cc > 0) ? "Blue" : "Anti-Blue";
        ImGui::Text("Color: %s", color_name);
    }

    if (decay_rate > 0.001f) {
        ImGui::Text("Decay rate: %.3f", decay_rate);
    }

    // Mass tier
    if (ptype < PHYS_PARTICLE_TYPES) {
        const char* mass_name = "light";
        uint32_t bhv = (ptype < MAX_PARTICLE_TYPES) ? particles.behavior_flags[ptype] : 0;
        if (bhv & BEHAVIOR_PHOTON)     mass_name = "massless";
        if (bhv & BEHAVIOR_NEUTRINO)   mass_name = "~massless";
        if (bhv & BEHAVIOR_MASS_MEDIUM) mass_name = "medium";
        if (bhv & BEHAVIOR_MASS_HEAVY)  mass_name = "heavy";
        if (bhv & BEHAVIOR_MASS_DENSE)  mass_name = "dense";
        if (bhv & BEHAVIOR_MASS_ULTRA)  mass_name = "ultra-heavy";
        ImGui::Text("Mass: %s", mass_name);
    }

    ImGui::EndTooltip();
}
