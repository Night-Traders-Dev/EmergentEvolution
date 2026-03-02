#include "physics/interface.h"
#include "physics/paths.h"
#include "physics/tutorial.h"
#include "physics/scenarios.h"
#include "physics/audio.h"
#include "physics/molecules.h"
#include "physics/phys_particles.h"
#include "physics/meson_data.h"
#include "vulkan_context.h"
#include "physics/ui_data.h"
#include "stb_image.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cmath>
#include <random>
#include <cstdio>
#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>
#include <chrono>
#ifdef HAS_OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;

// scan_theme_directory declared in interface.h

void PhysicsInterface::init() {
    std::random_device rd;
    seed_value = static_cast<int>(rd() % 100000);
    load_prefs();
    load_keybindings();
    load_molecule_bestiary();
    scan_theme_directory();
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i)
        particle_type_filter[i] = true;
}

// ── Settings persistence ────────────────────────────────────────────────────

static constexpr uint32_t PPCFG_MAGIC   = 0x47464350;  // "PCFG" little-endian
static constexpr uint32_t PPCFG_VERSION = 6;

// Historical struct sizes (for backward-compat migration)
static constexpr size_t PPCFG_V2_SIZE = 52;   // temp_unit .. event_log_save
static constexpr size_t PPCFG_V3_SIZE = 72;   // + autosave_interval .. tutorial_done
static constexpr size_t PPCFG_V4_SIZE = 104;  // + vsync .. sfx_muted
static constexpr size_t PPCFG_V5_SIZE = 108;  // + hide_bond_visuals .. particle_count_slider

void PhysicsInterface::save_prefs() {
    const std::string& data_dir = get_data_dir();
    std::ofstream f((data_dir + "settings.ppcfg").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    f.write(reinterpret_cast<const char*>(&PPCFG_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPCFG_VERSION), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&prefs), sizeof(UserPrefs));
}

void PhysicsInterface::load_prefs() {
    // One-time migration: copy old saves/ to new data dir if needed
    #ifndef PORTABLE_PATHS
    {
        std::error_code ec;
        const std::string& new_dir = get_data_dir();
        fs::path old_file = "saves/settings.ppcfg";
        fs::path new_file = fs::path(new_dir) / "settings.ppcfg";
        if (fs::exists(old_file, ec) && !fs::exists(new_file, ec)) {
            // Old dir exists but new dir has no settings — migrate
            fs::path old_dir = "saves";
            for (auto& entry : fs::directory_iterator(old_dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    fs::copy_file(entry.path(),
                                  fs::path(new_dir) / entry.path().filename(),
                                  fs::copy_options::skip_existing, ec);
                }
            }
        }
    }
    #endif

    std::ifstream f((get_data_dir() + "settings.ppcfg").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPCFG_MAGIC) return;
    if (version < 2 || version > PPCFG_VERSION) return;

    // Zero-init loaded struct so new fields get defaults
    UserPrefs loaded{};
    size_t read_size = sizeof(UserPrefs);
    if (version == 2)      read_size = PPCFG_V2_SIZE;
    else if (version == 3) read_size = PPCFG_V3_SIZE;
    else if (version == 4) read_size = PPCFG_V4_SIZE;
    else if (version == 5) read_size = PPCFG_V5_SIZE;

    f.read(reinterpret_cast<char*>(&loaded), static_cast<std::streamsize>(read_size));
    if (!f.good()) return;

    // Copy loaded fields, then restore defaults for fields added after this version
    prefs = loaded;
    UserPrefs defaults{};
    if (version <= 2) {
        prefs.autosave_interval = defaults.autosave_interval;
        prefs.window_mode = defaults.window_mode;
        prefs.window_w = defaults.window_w;
        prefs.window_h = defaults.window_h;
        prefs.bloom_enabled = defaults.bloom_enabled;
        prefs.tutorial_done = defaults.tutorial_done;
    }
    if (version <= 3) {
        prefs.vsync = defaults.vsync;
        prefs.preferred_gpu = defaults.preferred_gpu;
        prefs.preferred_monitor = defaults.preferred_monitor;
        prefs.mouse_sensitivity = defaults.mouse_sensitivity;
        prefs.colorblind_mode = defaults.colorblind_mode;
        prefs.high_contrast = defaults.high_contrast;
        prefs.reduced_motion = defaults.reduced_motion;
        prefs.quality_preset = defaults.quality_preset;
        prefs.sfx_volume = defaults.sfx_volume;
        prefs.sfx_muted = defaults.sfx_muted;
    }
    if (version <= 4) {
        prefs.hide_bond_visuals = defaults.hide_bond_visuals;
        prefs.hide_virtual_trails = defaults.hide_virtual_trails;
        prefs.hide_entanglement_lines = defaults.hide_entanglement_lines;
        prefs.particle_count_slider = defaults.particle_count_slider;
    }
    if (version <= 5) {
        prefs.voice_volume = defaults.voice_volume;
        prefs.voice_muted = defaults.voice_muted;
    }
}

// ── Molecule bestiary persistence ─────────────────────────────────────────────

static constexpr uint32_t PPBST_MAGIC   = 0x42535450;  // "PBST" little-endian
static constexpr uint32_t PPBST_VERSION = 2;

void PhysicsInterface::save_molecule_bestiary() {
    const std::string& data_dir = get_data_dir();
    std::ofstream f((data_dir + "bestiary_molecules.ppbst").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    f.write(reinterpret_cast<const char*>(&PPBST_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPBST_VERSION), sizeof(uint32_t));
    uint32_t count = static_cast<uint32_t>(molecule_bestiary.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
    for (const auto& e : molecule_bestiary) {
        uint32_t flen = static_cast<uint32_t>(e.formula.size());
        uint32_t nlen = static_cast<uint32_t>(e.name.size());
        f.write(reinterpret_cast<const char*>(&flen), sizeof(uint32_t));
        f.write(e.formula.data(), flen);
        f.write(reinterpret_cast<const char*>(&nlen), sizeof(uint32_t));
        f.write(e.name.data(), nlen);
        f.write(reinterpret_cast<const char*>(&e.times_seen), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(&e.atom_count), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(&e.first_seen_session), sizeof(uint32_t));
        f.write(reinterpret_cast<const char*>(&e.first_seen_time), sizeof(int64_t));
    }
}

void PhysicsInterface::load_molecule_bestiary() {
    std::ifstream f((get_data_dir() + "bestiary_molecules.ppbst").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPBST_MAGIC) return;
    if (version != 1 && version != 2) return;
    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    if (count > 10000) return;
    molecule_bestiary.clear();
    molecule_bestiary.reserve(count);
    for (uint32_t i = 0; i < count && f.good(); ++i) {
        MoleculeBestiaryEntry e;
        uint32_t flen = 0, nlen = 0;
        f.read(reinterpret_cast<char*>(&flen), sizeof(uint32_t));
        if (flen > 256) break;
        e.formula.resize(flen);
        f.read(e.formula.data(), flen);
        f.read(reinterpret_cast<char*>(&nlen), sizeof(uint32_t));
        if (nlen > 256) break;
        e.name.resize(nlen);
        f.read(e.name.data(), nlen);
        f.read(reinterpret_cast<char*>(&e.times_seen), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&e.atom_count), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&e.first_seen_session), sizeof(uint32_t));
        if (version >= 2)
            f.read(reinterpret_cast<char*>(&e.first_seen_time), sizeof(int64_t));
        if (f.good()) molecule_bestiary.push_back(std::move(e));
    }
}

// ── Keybinding system ─────────────────────────────────────────────────────────

const char* const KEY_ACTION_NAMES[KACT_COUNT] = {
    "Toggle Spawn Menu",       // KACT_TOGGLE_SPAWN_MENU
    "Toggle Select Mode",      // KACT_TOGGLE_SELECT_MODE
    "Toggle Settings Panel",   // KACT_TOGGLE_SETTINGS_PANEL
    "Reset Simulation",        // KACT_RESET
    "Play / Pause",            // KACT_PLAY_PAUSE
    "Pause Menu",              // KACT_PAUSE_MENU
    "Save",                    // KACT_SAVE
    "Load",                    // KACT_LOAD
    "Undo",                    // KACT_UNDO
    "Redo",                    // KACT_REDO
    "Time Slower",             // KACT_TIME_SLOWER
    "Time Faster",             // KACT_TIME_FASTER
    "Camera Up",               // KACT_CAMERA_UP
    "Camera Down",             // KACT_CAMERA_DOWN
    "Camera Left",             // KACT_CAMERA_LEFT
    "Camera Right",            // KACT_CAMERA_RIGHT
    "Toggle Fullscreen",       // KACT_FULLSCREEN_TOGGLE
};

void KeyBindings::set_defaults() {
    bindings[KACT_TOGGLE_SPAWN_MENU]     = { ImGuiKey_F3,           false, false, false };
    bindings[KACT_TOGGLE_SELECT_MODE]    = { ImGuiKey_F4,           false, false, false };
    bindings[KACT_TOGGLE_SETTINGS_PANEL] = { ImGuiKey_F1,           false, false, false };
    bindings[KACT_RESET]                 = { ImGuiKey_F2,           false, false, false };
    bindings[KACT_PLAY_PAUSE]            = { ImGuiKey_Space,        false, false, false };
    bindings[KACT_PAUSE_MENU]            = { ImGuiKey_Escape,       false, false, false };
    bindings[KACT_SAVE]                  = { ImGuiKey_S,            true,  false, false };
    bindings[KACT_LOAD]                  = { ImGuiKey_L,            true,  false, false };
    bindings[KACT_UNDO]                  = { ImGuiKey_Z,            true,  false, false };
    bindings[KACT_REDO]                  = { ImGuiKey_Z,            true,  true,  false };
    bindings[KACT_TIME_SLOWER]           = { ImGuiKey_LeftBracket,  false, false, false };
    bindings[KACT_TIME_FASTER]           = { ImGuiKey_RightBracket, false, false, false };
    bindings[KACT_CAMERA_UP]             = { ImGuiKey_W,            false, false, false };
    bindings[KACT_CAMERA_DOWN]           = { ImGuiKey_S,            false, false, false };
    bindings[KACT_CAMERA_LEFT]           = { ImGuiKey_A,            false, false, false };
    bindings[KACT_CAMERA_RIGHT]          = { ImGuiKey_D,            false, false, false };
    bindings[KACT_FULLSCREEN_TOGGLE]     = { ImGuiKey_Enter,        false, false, true  };
}

bool KeyBindings::is_pressed(KeyAction action) const {
    const auto& b = bindings[action];
    if (b.key == ImGuiKey_None) return false;
    if (!ImGui::IsKeyPressed(b.key, false)) return false;
    const ImGuiIO& io = ImGui::GetIO();
    if (b.ctrl  != io.KeyCtrl)  return false;
    if (b.shift != io.KeyShift) return false;
    if (b.alt   != io.KeyAlt)   return false;
    return true;
}

bool KeyBindings::is_down(KeyAction action) const {
    const auto& b = bindings[action];
    if (b.key == ImGuiKey_None) return false;
    if (!ImGui::IsKeyDown(b.key)) return false;
    // For held keys (WASD), don't require exact modifier match if no modifiers set
    if (!b.ctrl && !b.shift && !b.alt) return true;
    const ImGuiIO& io = ImGui::GetIO();
    if (b.ctrl  != io.KeyCtrl)  return false;
    if (b.shift != io.KeyShift) return false;
    if (b.alt   != io.KeyAlt)   return false;
    return true;
}

void format_keybinding(const KeyBinding& b, char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    if (b.key == ImGuiKey_None) { snprintf(buf, buf_size, "(none)"); return; }
    char tmp[128] = {};
    if (b.ctrl)  strncat(tmp, "Ctrl+", sizeof(tmp) - strlen(tmp) - 1);
    if (b.shift) strncat(tmp, "Shift+", sizeof(tmp) - strlen(tmp) - 1);
    if (b.alt)   strncat(tmp, "Alt+", sizeof(tmp) - strlen(tmp) - 1);
    strncat(tmp, ImGui::GetKeyName(b.key), sizeof(tmp) - strlen(tmp) - 1);
    snprintf(buf, buf_size, "%s", tmp);
}

// Keybinding serialization — separate file to avoid breaking UserPrefs v4
static constexpr uint32_t PPKEYS_MAGIC   = 0x5359454B;  // "KEYS"
static constexpr uint32_t PPKEYS_VERSION = 1;

void PhysicsInterface::save_keybindings() {
    const std::string& data_dir = get_data_dir();
    std::ofstream f((data_dir + "keybindings.ppkeys").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    f.write(reinterpret_cast<const char*>(&PPKEYS_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPKEYS_VERSION), sizeof(uint32_t));
    uint32_t count = KACT_COUNT;
    f.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(keybindings.bindings), sizeof(KeyBinding) * KACT_COUNT);
}

void PhysicsInterface::load_keybindings() {
    keybindings.set_defaults();
    std::ifstream f((get_data_dir() + "keybindings.ppkeys").c_str(), std::ios::binary);
    if (!f.is_open()) return;
    uint32_t magic = 0, version = 0, count = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPKEYS_MAGIC || version != PPKEYS_VERSION) return;
    f.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    if (count > KACT_COUNT) count = KACT_COUNT;
    f.read(reinterpret_cast<char*>(keybindings.bindings), sizeof(KeyBinding) * count);
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

// ── Direct meson spawns (single-particle meson types) ────────────────────────
static const SubAtomicSpec MESON_PI_PLUS[]   = {{ 0, 0, PION_PLUS_MESON }};
static const SubAtomicSpec MESON_PI_ZERO[]   = {{ 0, 0, PION_ZERO_MESON }};
static const SubAtomicSpec MESON_PI_MINUS[]  = {{ 0, 0, PION_MINUS_MESON }};
static const SubAtomicSpec MESON_ETA[]       = {{ 0, 0, ETA_MESON }};
static const SubAtomicSpec MESON_RHO_PLUS[]  = {{ 0, 0, RHO_770_PLUS }};
static const SubAtomicSpec MESON_RHO_ZERO[]  = {{ 0, 0, RHO_770_ZERO }};
static const SubAtomicSpec MESON_RHO_MINUS[] = {{ 0, 0, RHO_770_MINUS }};
static const SubAtomicSpec MESON_OMEGA[]     = {{ 0, 0, OMEGA_782_MESON }};
static const SubAtomicSpec MESON_PHI[]       = {{ 0, 0, PHI_1020_MESON }};
static const SubAtomicSpec MESON_K_PLUS[]    = {{ 0, 0, KAON_PLUS_MESON }};
static const SubAtomicSpec MESON_K_ZERO[]    = {{ 0, 0, KAON_ZERO_MESON }};
static const SubAtomicSpec MESON_K_MINUS[]   = {{ 0, 0, KAON_MINUS_MESON }};
static const SubAtomicSpec MESON_KSTAR_P[]   = {{ 0, 0, KSTAR_892_PLUS }};
static const SubAtomicSpec MESON_KSTAR_Z[]   = {{ 0, 0, KSTAR_892_ZERO }};
static const SubAtomicSpec MESON_D_PLUS[]    = {{ 0, 0, D_PLUS_MESON }};
static const SubAtomicSpec MESON_D_ZERO[]    = {{ 0, 0, D_ZERO_MESON }};
static const SubAtomicSpec MESON_DS_PLUS[]   = {{ 0, 0, DS_PLUS_MESON }};
static const SubAtomicSpec MESON_B_PLUS[]    = {{ 0, 0, B_PLUS_MESON }};
static const SubAtomicSpec MESON_B_ZERO[]    = {{ 0, 0, B_ZERO_MESON }};
static const SubAtomicSpec MESON_BS_ZERO[]   = {{ 0, 0, BS_ZERO_MESON }};
static const SubAtomicSpec MESON_BC_PLUS[]   = {{ 0, 0, BC_PLUS_MESON }};
static const SubAtomicSpec MESON_ETA_C[]     = {{ 0, 0, ETA_C_1S }};
static const SubAtomicSpec MESON_JPSI_M[]    = {{ 0, 0, JPSI_MESON }};
static const SubAtomicSpec MESON_UPSILON[]   = {{ 0, 0, UPSILON_1S }};

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
    // Mesons as quark pairs (for quark-level spawning)
    { "Pion+ (ud~)",        "pi+",  PION_PLUS,          2 },
    { "Pion- (du~)",        "pi-",  PION_MINUS,         2 },
    { "Kaon+ (us~)",        "K+",   KAON_PLUS,          2 },
    { "J/psi (cc~)",        "J/p",  JPSI,               2 },
    // Direct meson spawns (single-particle)
    { "\xcf\x80\xe2\x81\xba",         "pi+",  MESON_PI_PLUS,   1 },
    { "\xcf\x80\xe2\x81\xb0",         "pi0",  MESON_PI_ZERO,   1 },
    { "\xcf\x80\xe2\x81\xbb",         "pi-",  MESON_PI_MINUS,  1 },
    { "\xce\xb7",                      "eta",  MESON_ETA,       1 },
    { "\xcf\x81\xe2\x81\xba",         "rho+", MESON_RHO_PLUS,  1 },
    { "\xcf\x81\xe2\x81\xb0",         "rho0", MESON_RHO_ZERO,  1 },
    { "\xcf\x81\xe2\x81\xbb",         "rho-", MESON_RHO_MINUS, 1 },
    { "\xcf\x89(782)",                 "w",    MESON_OMEGA,     1 },
    { "\xcf\x86(1020)",                "phi",  MESON_PHI,       1 },
    { "K\xe2\x81\xba",                "K+",   MESON_K_PLUS,    1 },
    { "K\xe2\x81\xb0",                "K0",   MESON_K_ZERO,    1 },
    { "K\xe2\x81\xbb",                "K-",   MESON_K_MINUS,   1 },
    { "K*(892)\xe2\x81\xba",          "K*+",  MESON_KSTAR_P,   1 },
    { "K*(892)\xe2\x81\xb0",          "K*0",  MESON_KSTAR_Z,   1 },
    { "D\xe2\x81\xba",                "D+",   MESON_D_PLUS,    1 },
    { "D\xe2\x81\xb0",                "D0",   MESON_D_ZERO,    1 },
    { "Ds\xe2\x81\xba",               "Ds+",  MESON_DS_PLUS,   1 },
    { "B\xe2\x81\xba",                "B+",   MESON_B_PLUS,    1 },
    { "B\xe2\x81\xb0",                "B0",   MESON_B_ZERO,    1 },
    { "Bs\xe2\x81\xb0",               "Bs0",  MESON_BS_ZERO,   1 },
    { "Bc\xe2\x81\xba",               "Bc+",  MESON_BC_PLUS,   1 },
    { "\xce\xb7" "c(1S)",             "etac", MESON_ETA_C,     1 },
    { "J/\xcf\x88",                    "J/p",  MESON_JPSI_M,    1 },
    { "\xce\xa5(1S)",                  "Y1S",  MESON_UPSILON,   1 },
};
extern const int HADRON_TEMPLATE_COUNT_VAL = sizeof(HADRON_TEMPLATES) / sizeof(HADRON_TEMPLATES[0]);


// ── Themes ───────────────────────────────────────────────────────────────────

static constexpr int THEME_COLOR_COUNT = 33;
static constexpr int THEME_VAR_COUNT   = 10;

// Theme color palettes: { bg, bg_dim, accent, accent_bright, border, frame, frame_hover, text, text_dim }
// (ThemeColors struct is declared in interface.h)

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
    // 12: Universe Sandbox — deep space black with teal-blue accents
    { {0.025f,0.028f,0.050f,0.80f}, {0.015f,0.018f,0.035f,0.85f},
      {0.180f,0.580f,0.880f,1.0f},  {0.280f,0.680f,1.000f,1.0f},
      {0.080f,0.120f,0.220f,0.50f}, {0.040f,0.055f,0.100f,0.70f}, {0.065f,0.090f,0.160f,0.75f},
      {0.820f,0.860f,0.930f,1.0f},  {0.380f,0.420f,0.520f,1.0f} },
    // 13: Ubuntu Yaru — aubergine background with orange accents
    { {0.090f,0.020f,0.065f,0.80f}, {0.065f,0.015f,0.048f,0.85f},
      {0.910f,0.330f,0.125f,1.0f},  {1.000f,0.450f,0.220f,1.0f},
      {0.240f,0.080f,0.170f,0.50f}, {0.130f,0.035f,0.090f,0.70f}, {0.180f,0.055f,0.130f,0.75f},
      {0.920f,0.880f,0.900f,1.0f},  {0.530f,0.430f,0.470f,1.0f} },
};
// BUILTIN_THEME_COUNT is defined in interface.h
static const char* BUILTIN_THEME_NAMES[] = {
    "Dark Navy", "Midnight", "Slate", "Ember",
    "Synthwave", "Forest", "Arctic", "Solar",
    "Crimson", "Ocean", "Neon", "Lavender",
    "Universe Sandbox", "Ubuntu"
};

// ── Custom (imported) themes ─────────────────────────────────────────────────
struct CustomTheme {
    std::string name;
    ThemeColors colors;
};
static std::vector<CustomTheme> custom_themes;

int total_theme_count() { return BUILTIN_THEME_COUNT + static_cast<int>(custom_themes.size()); }
int custom_theme_count() { return static_cast<int>(custom_themes.size()); }

const ThemeColors& get_theme(int idx) {
    if (idx < BUILTIN_THEME_COUNT) return THEMES[idx];
    int ci = idx - BUILTIN_THEME_COUNT;
    if (ci >= 0 && ci < static_cast<int>(custom_themes.size())) return custom_themes[ci].colors;
    return THEMES[0];
}

const char* get_theme_name(int idx) {
    if (idx < BUILTIN_THEME_COUNT) return BUILTIN_THEME_NAMES[idx];
    int ci = idx - BUILTIN_THEME_COUNT;
    if (ci >= 0 && ci < static_cast<int>(custom_themes.size())) return custom_themes[ci].name.c_str();
    return "Unknown";
}

// Parse a .pptheme file (INI-like: name=..., bg=r,g,b,a, accent=r,g,b,a, ...)
static bool load_theme_file(const std::string& path, CustomTheme& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    // Start with Dark Navy as base so missing fields have sane defaults
    out.colors = THEMES[0];
    out.name = "Imported";

    auto parse_vec4 = [](const std::string& s, ImVec4& v) {
        float r, g, b, a;
        if (std::sscanf(s.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a) == 4)
            v = ImVec4(r, g, b, a);
    };

    std::string line;
    while (std::getline(f, line)) {
        // Strip leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line[start] == '#') continue;
        line = line.substr(start);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim key/val
        while (!key.empty() && key.back() == ' ') key.pop_back();
        size_t vs = val.find_first_not_of(" \t");
        if (vs != std::string::npos) val = val.substr(vs);

        if      (key == "name")         out.name = val;
        else if (key == "bg")           parse_vec4(val, out.colors.bg);
        else if (key == "bg_dim")       parse_vec4(val, out.colors.bg_dim);
        else if (key == "accent")       parse_vec4(val, out.colors.accent);
        else if (key == "accent_bright") parse_vec4(val, out.colors.accent_bright);
        else if (key == "border")       parse_vec4(val, out.colors.border);
        else if (key == "frame")        parse_vec4(val, out.colors.frame);
        else if (key == "frame_hover")  parse_vec4(val, out.colors.frame_hover);
        else if (key == "text")         parse_vec4(val, out.colors.text);
        else if (key == "text_dim")     parse_vec4(val, out.colors.text_dim);
    }
    return true;
}

void scan_theme_directory() {
    namespace fs = std::filesystem;
    custom_themes.clear();
    std::error_code ec;
    if (!fs::is_directory("themes", ec)) return;
    for (auto& entry : fs::directory_iterator("themes", ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".pptheme") continue;
        CustomTheme ct;
        if (load_theme_file(entry.path().string(), ct))
            custom_themes.push_back(std::move(ct));
    }
    // Sort by name for stable ordering
    std::sort(custom_themes.begin(), custom_themes.end(),
              [](const CustomTheme& a, const CustomTheme& b) { return a.name < b.name; });
}

void PhysicsInterface::push_theme() {
    int t = std::clamp(prefs.theme, 0, total_theme_count() - 1);
    const auto& c = get_theme(t);

    // Direct style assignment — avoids per-frame push/pop stack overhead
    ImGuiStyle& s = ImGui::GetStyle();
    auto* col = s.Colors;

    // High contrast mode: boost text brightness, border alpha, accent saturation
    float hc = prefs.high_contrast ? 1.0f : 0.0f;

    // Derived colors
    ImVec4 bg_child  = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.45f + hc * 0.3f);
    ImVec4 bg_popup  = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.80f + hc * 0.15f);
    ImVec4 bg_menu   = ImVec4(c.bg.x, c.bg.y, c.bg.z, 0.95f);
    ImVec4 frame_act = ImVec4(c.border.x, c.border.y, c.border.z, 0.70f);
    ImVec4 title_col = ImVec4(c.bg_dim.x, c.bg_dim.y, c.bg_dim.z, 0.55f);
    ImVec4 btn       = ImVec4(c.frame.x * 1.2f, c.frame.y * 1.2f, c.frame.z * 1.2f, 0.80f);
    ImVec4 btn_hov   = ImVec4(c.frame_hover.x * 1.2f, c.frame_hover.y * 1.2f, c.frame_hover.z * 1.2f, 0.90f);
    ImVec4 accent_lo = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.40f);
    ImVec4 accent_md = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.60f);
    ImVec4 accent_hi = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.80f);
    ImVec4 sep       = ImVec4(c.border.x, c.border.y, c.border.z, 0.60f + hc * 0.3f);

    // High contrast: brighter text
    ImVec4 text_hc   = prefs.high_contrast ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : c.text;
    ImVec4 border_hc = prefs.high_contrast
        ? ImVec4(c.border.x * 1.5f, c.border.y * 1.5f, c.border.z * 1.5f, 1.0f)
        : c.border;

    col[ImGuiCol_WindowBg]             = c.bg;
    col[ImGuiCol_ChildBg]              = bg_child;
    col[ImGuiCol_PopupBg]              = bg_popup;
    col[ImGuiCol_Border]               = border_hc;
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
    col[ImGuiCol_Text]                 = text_hc;
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


void PhysicsInterface::render_imgui(SimConfig& cfg, Particles& particles, ForceObject* force_objects, bool& request_reset) {
    // Deferred thumbnail cleanup — must happen before any ImGui::Image() calls
    // to avoid destroying descriptor sets referenced by the current frame's draw list
    if (pending_free_thumbnails_) {
        free_thumbnails();
        pending_free_thumbnails_ = false;
    }

    // Keybinding-based hotkeys (skip when rebinding)
    if (rebinding_action < 0) {
        if (keybindings.is_pressed(KACT_SAVE)) {
            show_save_dialog = true;
            browse_needs_refresh = true;
        }
        if (keybindings.is_pressed(KACT_LOAD)) {
            show_load_dialog = true;
            browse_needs_refresh = true;
        }
        // Check Redo before Undo (Ctrl+Shift+Z vs Ctrl+Z)
        if (keybindings.is_pressed(KACT_REDO))
            request_redo = true;
        else if (keybindings.is_pressed(KACT_UNDO))
            request_undo = true;
        if (keybindings.is_pressed(KACT_TOGGLE_SETTINGS_PANEL))
            settings_visible = !settings_visible;
    }

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

    // Cutscene playback (blocks all other UI)
    if (cutscene_state_ != CS_INACTIVE) {
        draw_cutscene();
        pop_theme();
        return;
    }

    // Cutscene gallery (blocks other UI when visible)
    if (show_cutscene_gallery) {
        draw_cutscene_gallery();
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

    // Credits panel (blocks other UI when visible)
    if (show_credits_) {
        draw_credits();
        pop_theme();
        return;
    }

    // How-To guide (blocks other UI when visible)
    if (show_howto) {
        draw_howto();
        pop_theme();
        return;
    }

    // Achievements panel (blocks other UI when visible)
    if (show_achievements_panel) {
        draw_achievements_panel();
        pop_theme();
        return;
    }

    // Scenario selection menu (blocks other UI when visible)
    if (show_scenario_menu) {
        draw_scenario_menu();
        pop_theme();
        return;
    }

    // Draw bottom bar (taskbar dock) and top stats bar
    draw_bottom_bar(cfg, request_reset);
    draw_top_bar(cfg);

    // Clear minimized bits and window rects for closed windows
    auto clear_closed = [&](bool flag, TaskbarWindow tw) {
        if (!flag) { set_minimized(tw, false); window_rect_valid_[tw] = false; }
    };
    clear_closed(spawn_menu_visible, TW_SPAWN_MENU);
    clear_closed(settings_visible, TW_SETTINGS);
    clear_closed(show_element_list, TW_ELEMENT_LIST);
    clear_closed(show_particle_list, TW_PARTICLE_LIST);
    clear_closed(show_particle_bestiary, TW_PARTICLE_BESTIARY);
    clear_closed(show_element_bestiary, TW_ELEMENT_BESTIARY);
    clear_closed(show_molecule_bestiary, TW_MOLECULE_BESTIARY);
    clear_closed(show_decay_log, TW_DECAY_LOG);
    clear_closed(show_nuclear_debug, TW_NUCLEAR_DEBUG);
    clear_closed(show_texture_panel, TW_TEXTURE_PANEL);
    clear_closed(show_save_dialog, TW_SAVE_DIALOG);
    clear_closed(show_load_dialog, TW_LOAD_DIALOG);
    clear_closed(show_repository, TW_REPOSITORY);
    clear_closed(accel_mode, TW_ACCELERATOR);

    // ── Auto-tile when window count reaches 3+ ──
    {
        TaskbarWindow open_wnd[TW_COUNT];
        int open_count = 0;
        for (int i = 0; i < (int)TW_COUNT; i++) {
            auto tw = static_cast<TaskbarWindow>(i);
            if (is_window_open(tw) && !is_minimized(tw))
                open_wnd[open_count++] = tw;
        }
        if (open_count >= 3 && open_count > prev_open_count_) {
            auto& io = ImGui::GetIO();
            float top = 48.0f, bottom = 40.0f, gap = 6.0f;
            float avail_w = io.DisplaySize.x - 2 * gap;
            float avail_h = io.DisplaySize.y - top - bottom - 2 * gap;
            int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(open_count))));
            int rows = (open_count + cols - 1) / cols;
            float tw = avail_w / cols;
            float th = avail_h / rows;
            for (int i = 0; i < open_count; i++) {
                int c = i % cols;
                int r = i / cols;
                tile_pos_[open_wnd[i]]  = ImVec2(gap + c * tw, top + gap + r * th);
                tile_size_[open_wnd[i]] = ImVec2(tw - gap, th - gap);
            }
            retile_windows_ = true;
        }
        prev_open_count_ = open_count;
    }

    // Draw settings panel
    if (settings_visible && !is_minimized(TW_SETTINGS))
        draw_settings_panel(cfg);

    // Draw spawn menu
    if (spawn_menu_visible && !is_minimized(TW_SPAWN_MENU))
        draw_spawn_menu(cfg);

    // Draw force object panel (if selected — not taskbar-managed)
    if (selected_force_obj_idx >= 0)
        draw_force_object_panel(force_objects);

    // Draw accelerator panel
    if (accel_mode && !is_minimized(TW_ACCELERATOR))
        draw_accelerator_panel();

    // Save/Load/Import dialog (import dialogs not taskbar-managed)
    if ((show_save_dialog && !is_minimized(TW_SAVE_DIALOG)) ||
        (show_load_dialog && !is_minimized(TW_LOAD_DIALOG)) ||
        show_import_dialog || show_molecule_import_dialog)
        draw_save_load_dialog();

    // Repository dialog
    if (show_repository && !is_minimized(TW_REPOSITORY))
        draw_repository();

    // Draw element list window (center, drawn before cards so cards overlay)
    if (!is_minimized(TW_ELEMENT_LIST))
        draw_element_list();

    // Draw particle list window
    if (!is_minimized(TW_PARTICLE_LIST))
        draw_particle_list(particles);

    // Draw bestiary windows
    if (!is_minimized(TW_PARTICLE_BESTIARY))
        draw_particle_bestiary();
    if (!is_minimized(TW_ELEMENT_BESTIARY))
        draw_element_bestiary();
    if (!is_minimized(TW_MOLECULE_BESTIARY))
        draw_molecule_bestiary();

    // Draw decay log window
    if (!is_minimized(TW_DECAY_LOG))
        draw_decay_log();

    // Draw nuclear reactions debug window
    if (!is_minimized(TW_NUCLEAR_DEBUG))
        draw_nuclear_debug(cfg);

    // Draw custom particle texture panel
    if (!is_minimized(TW_TEXTURE_PANEL))
        draw_texture_panel();

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
    if (accel_mode && accel_phase == 1 && accel_source_idx >= 0
        && accel_fire_type >= 0 && static_cast<uint32_t>(accel_fire_type) < PHYS_PARTICLE_TYPES) {
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

    // ── Accelerator free-fire aim visualization ──
    if (accel_mode && accel_phase == 1 && accel_source_idx < 0 && accel_free_origin_set
        && accel_fire_type >= 0 && static_cast<uint32_t>(accel_fire_type) < PHYS_PARTICLE_TYPES) {
        ImGuiIO& io = ImGui::GetIO();
        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;
        float scx = win_w / static_cast<float>(REGION_W);
        float scy = win_h / static_cast<float>(REGION_H);

        auto w2s = [&](glm::vec2 w) -> ImVec2 {
            glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                        + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
            return ImVec2(s.x, s.y);
        };

        ImVec2 origin_scr = w2s(accel_free_origin);
        ImVec2 mouse = io.MousePos;

        // Direction from origin toward mouse (fire direction)
        float dx = mouse.x - origin_scr.x;
        float dy = mouse.y - origin_scr.y;
        float len = std::sqrt(dx * dx + dy * dy);

        if (len > 1.0f) {
            float nx = dx / len;
            float ny = dy / len;

            ImVec4 tc = PHYS_TYPE_UI_COLORS[accel_fire_type];
            ImU32 line_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.6f));
            ImU32 dot_col  = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 1.0f));
            ImU32 origin_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.8f));

            ImDrawList* fg = ImGui::GetForegroundDrawList();

            // Origin indicator (diamond shape)
            float os = 8.0f;
            fg->AddQuadFilled(
                ImVec2(origin_scr.x, origin_scr.y - os),
                ImVec2(origin_scr.x + os, origin_scr.y),
                ImVec2(origin_scr.x, origin_scr.y + os),
                ImVec2(origin_scr.x - os, origin_scr.y),
                origin_col);
            fg->AddCircle(origin_scr, 12.0f, dot_col, 16, 1.5f);

            // Dashed aim line from origin toward mouse
            float dash = 8.0f, gap = 4.0f, t = 0.0f;
            bool draw_seg = true;
            while (t < len) {
                float seg = draw_seg ? dash : gap;
                seg = std::min(seg, len - t);
                if (draw_seg) {
                    ImVec2 a(origin_scr.x + nx * t, origin_scr.y + ny * t);
                    ImVec2 b(origin_scr.x + nx * (t + seg), origin_scr.y + ny * (t + seg));
                    fg->AddLine(a, b, line_col, 2.0f);
                }
                t += seg;
                draw_seg = !draw_seg;
            }

            // Arrowhead at mouse end (aim direction)
            float as = 10.0f;
            ImVec2 tip = mouse;
            ImVec2 left(tip.x - nx*as + ny*as*0.5f, tip.y - ny*as - nx*as*0.5f);
            ImVec2 right(tip.x - nx*as - ny*as*0.5f, tip.y - ny*as + nx*as*0.5f);
            fg->AddTriangleFilled(tip, left, right, dot_col);

            // Triple shot spread lines
            if (accel_fire_mode == 1) {
                float spread = 5.0f * 3.14159265f / 180.0f;
                float base_a = std::atan2(ny, nx);
                ImU32 spread_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, 0.25f));
                for (int s = -1; s <= 1; s += 2) {
                    float a = base_a + s * spread;
                    ImVec2 end(origin_scr.x + std::cos(a) * len,
                              origin_scr.y + std::sin(a) * len);
                    fg->AddLine(origin_scr, end, spread_col, 1.0f);
                }
            }

            // Stream mode: pulsing indicator at origin
            if (accel_fire_mode == 2) {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetFrameCount()) * 0.15f);
                ImU32 pulse_col = ImGui::ColorConvertFloat4ToU32(ImVec4(tc.x, tc.y, tc.z, pulse * 0.4f));
                fg->AddCircleFilled(origin_scr, 8.0f, pulse_col);
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
            ImVec4(0.95f, 0.35f, 0.3f, 1.0f),  // shell 4: red
        };
        const ImVec4 SHELL_COLORS_ANTI[] = {
            ImVec4(0.0f, 0.85f, 0.95f, 1.0f),  // shell 1: cyan
            ImVec4(0.6f, 0.3f, 0.95f, 1.0f),   // shell 2: purple
            ImVec4(0.95f, 0.4f, 0.6f, 1.0f),   // shell 3: pink
            ImVec4(1.0f, 0.7f, 0.3f, 1.0f),    // shell 4: orange
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

            for (int s = 0; s < 4; ++s) {
                if (cloud.shell_cap[s] == 0) continue;

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
    if (cfg.entanglement_enabled && !prefs.hide_entanglement_lines
        && readback_positions_ptr && entangled_partners_ptr
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
    if (cfg.bonds_enabled && !prefs.hide_bond_visuals && bond_data_ptr && readback_positions_ptr && readback_count > 0) {
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
    if (show_strong_field)      draw_strong_field(cfg);
    if (show_weak_field)        draw_weak_field(cfg);
    if (show_gravity_field)     draw_gravity_field(cfg);
    if (show_gravity_map)       draw_gravity_map(cfg);
    if (show_grav_waves)        draw_grav_waves(cfg);
    if (show_trajectory_tracer) draw_trajectory_traces(cfg);
    if (show_force_vectors)     draw_force_vectors_overlay(cfg);
    if (show_atom_grid)         draw_atom_grid(cfg);
    if (show_orbit_paths)       draw_orbit_paths(cfg);

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

    // Scenario goal HUD (top-center, during active scenario)
    if (scenarios_ptr && scenarios_ptr->active) draw_scenario_goal_hud();

    // Tutorial overlay
    if (tutorial_ptr && tutorial_ptr->active) draw_tutorial_overlay();

    // Encyclopedia popup
    if (show_encyclopedia) draw_encyclopedia_popup();

    // Loading overlay (highest priority, covers everything)
    if (show_loading_overlay) draw_loading_overlay();

    // Draw notifications last so they render on top of all windows
    draw_notifications();

    retile_windows_ = false;
    pop_theme();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Window Management: intelligent placement & auto-tiling ──────────────────
// ══════════════════════════════════════════════════════════════════════════════

bool PhysicsInterface::is_window_open(TaskbarWindow tw) const {
    switch (tw) {
        case TW_SPAWN_MENU:        return spawn_menu_visible;
        case TW_SETTINGS:          return settings_visible;
        case TW_ELEMENT_LIST:      return show_element_list;
        case TW_PARTICLE_LIST:     return show_particle_list;
        case TW_PARTICLE_BESTIARY: return show_particle_bestiary;
        case TW_ELEMENT_BESTIARY:  return show_element_bestiary;
        case TW_MOLECULE_BESTIARY: return show_molecule_bestiary;
        case TW_DECAY_LOG:         return show_decay_log;
        case TW_NUCLEAR_DEBUG:     return show_nuclear_debug;
        case TW_TEXTURE_PANEL:     return show_texture_panel;
        case TW_SAVE_DIALOG:       return show_save_dialog;
        case TW_LOAD_DIALOG:       return show_load_dialog;
        case TW_REPOSITORY:        return show_repository;
        case TW_ACCELERATOR:       return accel_mode;
        default: return false;
    }
}

void PhysicsInterface::record_window_rect(TaskbarWindow tw) {
    window_rects_[tw] = { ImGui::GetWindowPos(), ImGui::GetWindowSize() };
    window_rect_valid_[tw] = true;
}

ImVec2 PhysicsInterface::find_free_window_pos(ImVec2 win_size) {
    auto& io = ImGui::GetIO();
    float top = 48.0f;

    // Collect occupied rects from tracked windows
    ImVec2 occ_pos[TW_COUNT], occ_size[TW_COUNT];
    int occ = 0;
    for (int i = 0; i < (int)TW_COUNT; i++) {
        if (window_rect_valid_[i]) {
            occ_pos[occ] = window_rects_[i].pos;
            occ_size[occ] = window_rects_[i].size;
            occ++;
        }
    }

    // Try cascade positions (diagonal offset), avoiding overlap
    for (int slot = 0; slot < 16; slot++) {
        float x = 60.0f + slot * 36.0f;
        float y = top + 20.0f + slot * 36.0f;
        ImVec2 cand = clamp_window_pos(ImVec2(x, y), win_size);

        bool overlaps = false;
        for (int j = 0; j < occ; j++) {
            if (cand.x < occ_pos[j].x + occ_size[j].x && cand.x + win_size.x > occ_pos[j].x &&
                cand.y < occ_pos[j].y + occ_size[j].y && cand.y + win_size.y > occ_pos[j].y) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) return cand;
    }

    // Fallback: center screen
    return clamp_window_pos(
        ImVec2(io.DisplaySize.x * 0.5f - win_size.x * 0.5f,
               io.DisplaySize.y * 0.5f - win_size.y * 0.5f), win_size);
}

void PhysicsInterface::draw_minimize_button(TaskbarWindow tw) {
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();
    float fsz = ImGui::GetFontSize();
    float px = ImGui::GetStyle().FramePadding.x;
    float py = ImGui::GetStyle().FramePadding.y;

    // Position left of the close button (close is at MaxX - px - fsz)
    float x = wp.x + ws.x - px - fsz * 2 - 4.0f;
    float y = wp.y + py;
    ImVec2 mn(x, y), mx(x + fsz, y + fsz);

    bool in_rect = ImGui::IsMouseHoveringRect(mn, mx, false);
    bool win_hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    bool hov = in_rect && win_hov;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) {
        dl->AddRectFilled(mn, mx, IM_COL32(70, 70, 110, 180), 2.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            set_minimized(tw, true);
        ImGui::SetTooltip("Minimize to taskbar");
    }

    // Draw "_" minimize icon (horizontal line near bottom of button)
    float ly = mx.y - 4.0f;
    ImU32 col = hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 200, 180);
    dl->AddLine(ImVec2(mn.x + 3, ly), ImVec2(mx.x - 3, ly), col, 1.5f);
}
