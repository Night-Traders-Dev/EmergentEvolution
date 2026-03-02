#pragma once

#include "imgui.h"
#include "types.h"
#include "particles.h"
#include "physics/achievements.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <ctime>
#include <vector>
#include <string>
#include <glm/glm.hpp>

class VulkanContext;  // forward declare
class ParticleRepository;  // forward declare

// ── User preferences (settings menu) ─────────────────────────────────────────
struct UserPrefs {
    int   temp_unit       = 0;     // 0=Kelvin, 1=Celsius, 2=Fahrenheit
    int   theme           = 0;     // 0=Dark Navy, 1=Midnight, 2=Slate, 3=Ember
    int   max_threads     = 0;     // OpenMP thread cap (0 = use system max)
    int   fps_cap         = 0;     // 0=uncapped, 30/60/120/144/240
    bool  show_fps        = true;
    float ui_scale        = 1.0f;  // 0.8 - 1.5
    int   physics_quality = 2;     // 0=Low, 1=Medium, 2=High
    int   physics_skip    = 0;     // run CPU physics every (skip+1) frames
    bool  spatial_grid    = true;  // use spatial grid for neighbor queries
    float music_volume    = 0.5f;  // 0.0-1.0 background music volume
    bool  music_muted     = false; // mute toggle
    int   render_scale    = 1;     // 1=native, 2=2x, 3=3x, 4=4x supersampling
    bool  event_log_limit = true;  // true=cap at 10k entries, false=unlimited
    bool  event_log_save  = false; // true=append events to saves/event_log.txt
    int   autosave_interval = 5;   // minutes between auto-saves (0=disabled)
    int   window_mode     = 0;     // 0=borderless fullscreen, 1=windowed
    int   window_w        = 0;     // windowed width (0=monitor default)
    int   window_h        = 0;     // windowed height (0=monitor default)
    bool  bloom_enabled   = false; // post-processing bloom
    bool  wobbly_windows  = true;  // subtle floating UI panel animations
    bool  tutorial_done   = false; // true after tutorial completed or skipped

    // v4 fields — append only, keep defaults for backward compat
    bool  vsync            = false;  // VSync (FIFO present mode)
    int   preferred_gpu    = -1;     // -1=auto, 0+=index from GPU enumeration
    int   preferred_monitor = 0;     // monitor index for fullscreen
    float mouse_sensitivity = 1.0f;  // mouse speed multiplier (0.1-3.0)
    int   colorblind_mode  = 0;      // 0=off, 1=protanopia, 2=deuteranopia, 3=tritanopia
    bool  high_contrast    = false;  // high contrast UI
    bool  reduced_motion   = false;  // disable wobbly windows + splash particles + GW ripples
    int   quality_preset   = 2;      // 0=Low, 1=Medium, 2=High, 3=Ultra, 4=Custom
    float sfx_volume       = 0.7f;   // 0.0-1.0 sound effect volume
    bool  sfx_muted        = false;  // mute SFX toggle

    // v5 fields — visual overlays + particle count
    bool  hide_bond_visuals       = false;  // hide bond lines (spring physics still active)
    bool  hide_virtual_trails     = false;  // hide virtual particle rendering
    bool  hide_entanglement_lines = false;  // hide entanglement dashed lines
    float particle_count_slider   = 70.0f;  // sqrt-scale particle count (70^2 = ~5000)
};

// ── Keybinding system ─────────────────────────────────────────────────────────
enum KeyAction : uint8_t {
    KACT_TOGGLE_SPAWN_MENU = 0,
    KACT_TOGGLE_SELECT_MODE,
    KACT_TOGGLE_SETTINGS_PANEL,
    KACT_RESET,
    KACT_PLAY_PAUSE,
    KACT_PAUSE_MENU,
    KACT_SAVE,
    KACT_LOAD,
    KACT_UNDO,
    KACT_REDO,
    KACT_TIME_SLOWER,
    KACT_TIME_FASTER,
    KACT_CAMERA_UP,
    KACT_CAMERA_DOWN,
    KACT_CAMERA_LEFT,
    KACT_CAMERA_RIGHT,
    KACT_FULLSCREEN_TOGGLE,
    KACT_COUNT
};

struct KeyBinding {
    ImGuiKey key   = ImGuiKey_None;
    bool     ctrl  = false;
    bool     shift = false;
    bool     alt   = false;
};

struct KeyBindings {
    KeyBinding bindings[KACT_COUNT] = {};
    void set_defaults();
    bool is_pressed(KeyAction action) const;  // single-fire (edge-triggered)
    bool is_down(KeyAction action) const;     // continuous hold (for WASD camera)
};

// Human-readable action names (indexed by KeyAction)
extern const char* const KEY_ACTION_NAMES[KACT_COUNT];

// Format a KeyBinding into a string like "Ctrl+Shift+F3"
void format_keybinding(const KeyBinding& b, char* buf, size_t buf_size);

// ── Group template for spawning composite structures ─────────────────────────
struct SubAtomicSpec {
    float dx, dy;
    uint32_t type;
};

struct GroupTemplate {
    const char* name;
    const char* label;
    const SubAtomicSpec* atoms;
    uint32_t count;
};

// Defined in interface.cpp
extern const GroupTemplate GROUP_TEMPLATES[];
extern const int GROUP_TEMPLATE_COUNT_VAL;
extern const GroupTemplate HADRON_TEMPLATES[];
extern const int HADRON_TEMPLATE_COUNT_VAL;

// ── Measurement tool data ────────────────────────────────────────────────────

struct ThermometerProbe {
    glm::vec2 world_pos;
    float radius = 80.0f;
    float local_temp = 0.0f;
    uint32_t local_count = 0;
};

struct VelocityMeterTarget {
    int32_t particle_idx = -1;
    bool active = true;
};

struct DistanceRuler {
    glm::vec2 point_a, point_b;
    float distance = 0.0f;
};

struct DensityCounter {
    glm::vec2 world_pos;
    float radius = 100.0f;
    uint32_t count = 0;
    float density = 0.0f;
};

// ── Visualization grid ───────────────────────────────────────────────────────

static constexpr uint32_t VIS_GRID_W     = 32;
static constexpr uint32_t VIS_GRID_H     = 18;
static constexpr uint32_t VIS_GRID_CELLS = VIS_GRID_W * VIS_GRID_H;

struct VisGrid {
    float    energy[VIS_GRID_CELLS] = {};
    float    vel_x[VIS_GRID_CELLS]  = {};
    float    vel_y[VIS_GRID_CELLS]  = {};
    float    bfield_z[VIS_GRID_CELLS] = {};  // magnetic field Bz at cell center
    uint32_t count[VIS_GRID_CELLS]  = {};
};

// Theme color palette — shared across UI modules
struct ThemeColors {
    ImVec4 bg, bg_dim, accent, accent_bright, border, frame, frame_hover, text, text_dim;
};

// Theme accessors (defined in interface.cpp)
inline constexpr int BUILTIN_THEME_COUNT = 14;
int total_theme_count();
const ThemeColors& get_theme(int idx);
const char* get_theme_name(int idx);
int custom_theme_count();
void scan_theme_directory();

class PhysicsInterface {
public:
    ImFont* title_font = nullptr;  // 48px font for crisp splash title (set by simulation init)

    bool show_splash = true;
    bool show_pause_menu = false;
    bool show_settings_menu = false;
    bool show_credits_ = false;
    bool show_howto = false;
    bool show_repository = false;
    int  settings_tab = 0;  // 0=Display, 1=Perf, 2=Theme, 3=Access, 4=Audio, 5=Controls
    bool request_quit = false;
    UserPrefs prefs;
    KeyBindings keybindings;
    int rebinding_action = -1;  // -1=not rebinding, else KeyAction index
    bool settings_visible = false;
    bool spawn_menu_visible = true;
    bool pending_spawn = false;
    bool sim_running = true;

    // Wobbly windows — continuous floating animation for UI panels.
    // Works by shifting rendered vertices, not positions — dragging works normally.
    float wobble_time = 0.0f;

    ImVec2 wobble_offset(float phase) const {
        if (!prefs.wobbly_windows) return ImVec2(0, 0);
        float wx = std::sin(wobble_time * 1.2f + phase) * 3.0f;
        float wy = std::cos(wobble_time * 0.9f + phase * 1.7f) * 2.5f;
        return ImVec2(wx, wy);
    }

    // Call between Begin() and End() to wobble the current window.
    // Shifts all rendered vertices + clip rects in the window's draw list.
    void wobble_window(float phase) {
        if (!prefs.wobbly_windows) return;
        ImVec2 w = wobble_offset(phase);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < dl->VtxBuffer.Size; i++) {
            dl->VtxBuffer[i].pos.x += w.x;
            dl->VtxBuffer[i].pos.y += w.y;
        }
        for (int i = 0; i < dl->CmdBuffer.Size; i++) {
            dl->CmdBuffer[i].ClipRect.x += w.x;
            dl->CmdBuffer[i].ClipRect.y += w.y;
            dl->CmdBuffer[i].ClipRect.z += w.x;
            dl->CmdBuffer[i].ClipRect.w += w.y;
        }
    }

    // Loading overlay
    bool show_loading_overlay = false;
    const char* loading_message = "";

    // Selection tool
    bool select_mode = false;

    // Tools popup
    bool show_tools_popup = false;
    bool request_halt_velocities = false;
    bool request_remove_massless = false;
    bool request_remove_massive  = false;

    // Save/Load
    char save_filename[256] = "save.ppsg";
    bool show_save_dialog = false;
    bool show_load_dialog = false;
    bool request_save = false;
    bool request_load = false;
    bool request_undo = false;
    bool request_redo = false;
    char save_load_message[256] = {};
    float save_load_msg_timer = 0.0f;

    // File browser state
    std::string browse_current_dir;
    struct BrowseEntry {
        std::string name;
        bool is_dir;
        uintmax_t size;  // file size in bytes (0 for dirs)
    };
    std::vector<BrowseEntry> browse_entries;
    int browse_selected_idx = -1;
    bool browse_needs_refresh = true;
    char browse_filename[256] = "save.ppsg";   // filename input for save
    char browse_path_buf[512] = {};             // editable path bar

    // Spawn settings
    int   spawn_type    = 0;     // phys_particles.h type index
    int   spawn_count   = 1;
    float spawn_energy_mev = 0.0f;  // kinetic energy in MeV (0 = stationary)
    float spawn_scatter = 20.0f;
    int   spawn_group   = -1;    // -1=single particle, 0+=group template
    int   spawn_atom_Z  = -1;    // -1=not spawning atom, >0=atomic number
    int   spawn_atom_N  = -1;    // neutron count for spawn_atom_Z

    // Molecule spawn
    int   spawn_molecule_idx = -1;        // index into MOLECULE_TEMPLATES (-1 = not spawning molecule)
    char  molecule_formula_buf[64] = {};   // text input buffer
    int   molecule_match_idx = -1;         // current exact match (-1 = no match)
    std::string spawn_molecule_file;      // cached .ppmol file path (empty = use template)

    // Hover / selected particle inspection
    int32_t hover_particle_idx    = -1;
    int32_t selected_particle_idx = -1;  // pinned particle for info card (-1=hover mode)
    bool    particle_move_mode    = false;
    bool    request_delete_particle = false;  // set by UI, consumed by simulation

    // Element detail card
    int32_t element_card_nucleus_rep = -1;  // nucleus rep index for element card (-1=hidden)
    bool request_element_delete    = false;
    bool request_element_duplicate = false;
    bool request_element_export    = false;
    int32_t export_element_rep     = -1;    // nucleus rep to export
    bool element_move_mode         = false;

    // Molecule detail card
    int32_t molecule_card_atom_rep = -1;    // rep of any atom in molecule (-1=hidden)

    // Molecule export/import
    bool request_molecule_export       = false;
    int32_t export_molecule_atom_rep   = -1;    // rep of atom in molecule to export
    bool show_molecule_import_dialog   = false;
    bool request_molecule_import       = false;

    // Element import
    bool show_import_dialog = false;
    bool request_import     = false;

    // Repository
    ParticleRepository* repository_ptr = nullptr;
    bool repo_auto_import = false;   // auto-import after download completes
    int  repo_import_category = 0;   // 0=element, 1=molecule
    int  repo_current_tab = 0;       // 0=elements, 1=molecules
    char repo_upload_path[512] = {};
    char repo_token_buf[256] = {};   // token input buffer
    char repo_search_buf[64] = {};   // search filter text
    int  repo_filter_mode = 0;       // 0=All, 1=Cached, 2=Not Downloaded

    // Force object interaction
    bool force_obj_placement_mode = false;
    int  force_obj_placement_type = 0;
    int  selected_force_obj_idx   = -1;
    bool force_obj_move_mode      = false;

    // Mirror placement (two-click: endpoint 1 → endpoint 2)
    bool      mirror_placement_mode = false;
    int       mirror_placement_phase = 0;    // 0=click endpoint 1, 1=click endpoint 2
    glm::vec2 mirror_endpoint1 = {};

    // Particle Accelerator tool
    bool     accel_mode = false;            // tool active
    int      accel_phase = 0;               // 0=select source, 1=aim/fire
    int32_t  accel_source_idx = -1;         // source particle index
    int      accel_fire_type = 0;           // projectile type index
    float    accel_speed = 300.0f;          // projectile speed (max 300 = "c")
    int      accel_fire_mode = 0;           // 0=single, 1=triple, 2=stream
    uint32_t accel_stream_timer = 0;        // frame counter for stream rate
    uint32_t accel_stream_interval = 3;     // fire every N frames in stream
    glm::vec2 accel_source_world_pos = {};  // updated each tick for aim rendering
    bool     accel_free_origin_set = false; // free-fire: origin has been placed
    glm::vec2 accel_free_origin = {};       // free-fire: spawn/fire origin point

    // ── Measurement Tools ────────────────────────────────────────────────────
    std::vector<ThermometerProbe> thermo_probes;
    bool thermo_probe_placement_mode = false;

    std::vector<VelocityMeterTarget> velocity_meters;
    bool velocity_meter_mode = false;

    std::vector<DistanceRuler> distance_rulers;
    bool ruler_placement_mode = false;
    int  ruler_placement_phase = 0;
    glm::vec2 ruler_point_a = {};

    std::vector<DensityCounter> density_counters;
    bool density_counter_placement_mode = false;

    // ── Visualization Toggles ────────────────────────────────────────────────
    bool  show_trajectory_tracer = false;
    int   trajectory_max_points  = 120;

    bool  show_energy_heatmap = false;
    float heatmap_opacity      = 0.3f;

    bool  show_velocity_field  = false;
    float velocity_field_scale = 1.0f;

    bool  show_force_vectors   = false;

    bool  show_atom_grid       = false;
    float atom_grid_opacity    = 0.25f;

    bool  show_magnetic_field  = false;
    float magnetic_field_opacity = 0.35f;
    bool  bfield_cache_valid     = false;  // B-field vis grid cache
    float bfield_cache_mean_speed = 0.0f;  // mean |v| when cache was last built

    bool  show_gravity_map     = false;
    bool  show_grav_waves      = false;  // GW ripple visualization

    // General Relativity extensions (GPU gravity)
    bool  gr_mass_energy    = true;   // E=mc² gravitational mass (bit 9)
    bool  gr_frame_dragging = true;   // spin-gravity coupling (bit 10)
    bool  gr_grav_waves     = true;   // finite-speed gravity / GW (bit 11)

    // Orbital mechanics model
    bool  orbital_drive       = false;  // artificial orbital confinement (bit 13)
                                        // ON: centrifugal barrier + tangential drive + radial damping
                                        // OFF: pure Coulomb + magnetic (classical, electrons spiral in)

    bool  show_orbit_paths    = false;

    // Orbit path data (populated by simulation each tick)
    struct OrbitPath {
        glm::vec2 center;       // nucleus center (world space)
        float semi_major;       // semi-major axis a
        float semi_minor;       // semi-minor axis b
        float orientation;      // angle of periapsis (radians)
        float eccentricity;     // orbit eccentricity
        int   shell;            // shell index for coloring
        bool  is_anti;          // antimatter flag
    };
    std::vector<OrbitPath> orbit_paths;

    // ── Visualization Data (populated by simulation each tick) ───────────────
    VisGrid vis_grid;
    std::vector<std::vector<glm::vec2>> trajectory_history;

    // Magnetic field line sources (populated by simulation when show_magnetic_field)
    struct MagneticSource {
        glm::vec2 pos;
        glm::vec2 vel;
        float intrinsic_moment; // spin dipole μ (signed, nuclear magnetons / Bohr magnetons)
        float charge;           // particle charge (for Biot-Savart B ∝ q·v×r̂/r²)
        float mass_mev;         // rest mass in MeV (for momentum scaling)
    };
    std::vector<MagneticSource> magnetic_sources;

    // Gravitational wave ripples (expanding rings from accelerating masses)
    struct GWRing {
        glm::vec2 origin;       // emission center (world)
        float     radius;       // current ring radius (world units)
        float     amplitude;    // wave strength (∝ mass × acceleration)
        float     max_radius;   // fade out at this range
    };
    std::vector<GWRing> gw_rings;

    struct ForceContribution {
        glm::vec2 direction;
        float magnitude;
        const char* name;
        ImVec4 color;
    };
    std::vector<ForceContribution> force_contributions;

    const float* readback_energies_ptr = nullptr;
    const uint32_t* readback_types_ptr = nullptr;

    // Stats display
    float    fps_display = 0.0f;
    uint32_t active_particle_display  = 0;
    uint32_t dormant_particle_display = 0;
    float    total_energy_display     = 0.0f;
    float    avg_energy_display       = 0.0f;
    uint32_t type_counts_display[MAX_PARTICLE_TYPES] = {};

    // Per-type simulation statistics (accumulated across simulation lifetime)
    struct ParticleTypeStats {
        uint32_t total_spawned = 0;     // cumulative ever existed
        uint32_t peak_count = 0;        // highest simultaneous count
        double   lifetime_sum = 0.0;    // sum of lifetimes (frames) of dead particles
        uint32_t lifetime_count = 0;    // number that have died
        double   energy_sum = 0.0;      // sum of current energies (living)
        double   speed_sum = 0.0;       // sum of current speeds (living)
    };
    ParticleTypeStats type_stats[MAX_PARTICLE_TYPES] = {};

    // Per-element (Z=0..118) simulation stats — reset each simulation
    struct ElementBestiaryStats {
        uint32_t current_count = 0;
        uint32_t total_spawned = 0;
        uint32_t peak_count = 0;
        double   lifetime_sum = 0.0;
        uint32_t lifetime_count = 0;
        double   energy_sum = 0.0;
    };
    ElementBestiaryStats element_stats[119] = {};

    // Molecule bestiary — persists to disk across sessions
    struct MoleculeBestiaryEntry {
        std::string formula;
        std::string name;
        uint32_t times_seen = 0;
        uint32_t atom_count = 0;
        uint32_t first_seen_session = 0;
        int64_t  first_seen_time = 0;       // unix timestamp (time_t)
    };
    std::vector<MoleculeBestiaryEntry> molecule_bestiary;
    uint32_t molecule_bestiary_session = 0;

    // Wave-particle duality
    bool  wave_mode = false;

    // Field visualization (6 independent toggles)
    bool  field_em          = false;
    bool  field_strong      = false;
    bool  field_weak        = false;
    bool  field_gravity     = false;
    bool  field_higgs       = false;
    bool  field_dark_energy = false;
    float field_intensity = 0.5f;
    bool  show_collision_radii = false;

    // Bottom bar auto-hide
    float bottom_bar_offset = 0.0f;  // 0.0 = fully visible, 1.0 = fully hidden

    // Emergent feedback display
    float emergent_temp_display   = 0.0f;
    float emergent_bfield_display = 0.0f;
    uint32_t nuclear_decay_count_display = 0;
    uint32_t entangled_pair_count_display = 0;

    // Thermodynamics display (First Law: energy conservation)
    float energy_kinetic_display              = 0.0f;
    float energy_potential_display            = 0.0f;
    float energy_conservation_ratio_display   = 1.0f;
    float energy_injected_rate_display        = 0.0f;
    float energy_dissipated_rate_display      = 0.0f;
    float energy_drift_rate_display           = 0.0f;

    // Thermodynamics display (Second Law: entropy)
    float system_entropy_display              = 0.0f;
    int   entropy_trend_display               = 0;  // +1 up, 0 stable, -1 down

    // Readback data pointers for entanglement visualization
    const glm::vec2*  readback_positions_ptr = nullptr;
    const uint32_t*   entangled_partners_ptr = nullptr;

    // Bond data pointers for overlay visualization
    const uint32_t* bond_data_ptr = nullptr;
    uint32_t        bond_data_count = 0;

    // Element list (populated by simulation from detected_nuclei_)
    struct ElementSummary {
        int Z, N;           // proton/neutron count
        int electrons;      // bound electron count (or bound positrons for anti)
        uint32_t rep;       // nucleus representative index
        bool is_anti;       // antimatter element
        float energy_MeV;   // total relativistic energy (sum of γm₀c² for all constituents)
        uint32_t oldest_birth; // earliest birth frame among all constituents
    };
    std::vector<ElementSummary> element_list;
    bool show_element_list = false;

    // Molecule grouping (bonded atom clusters)
    struct MoleculeSummary {
        std::vector<uint32_t> atom_indices;  // indices into element_list
        float total_energy_MeV = 0.0f;
        uint32_t oldest_birth = UINT32_MAX;
        int total_charge = 0;       // net ionic charge of molecule
    };
    std::vector<MoleculeSummary> molecule_list;

    // Particle list (all active particles, populated each tick)
    bool show_particle_list = false;
    bool show_particle_bestiary = false;
    bool show_element_bestiary = false;
    bool show_molecule_bestiary = false;
    bool particle_type_filter[MAX_PARTICLE_TYPES] = {}; // all true by default (initialized in init())

    // Element list filter
    int element_filter_mode = 0; // 0=all, 1=stable, 2=unstable, 3=antimatter, 4=molecules

    // Electron cloud visualization
    bool show_electron_cloud = false;
    struct NucleusCloudInfo {
        glm::vec2 center;       // world-space nucleus center
        int Z;                  // proton/antiproton count
        int electrons;          // bound electron/positron count
        bool is_anti;
        float shell_radii[4];   // Bohr radii for shells 1-4 (world units)
        int   shell_fill[4];    // electrons in each shell
        int   shell_cap[4];     // max electrons per shell
    };
    std::vector<NucleusCloudInfo> nucleus_clouds;

    // Achievements
    bool show_achievements_panel = false;
    AchievementManager* achievements_ptr = nullptr;  // set by simulation
    struct AudioPlayer* audio_ptr = nullptr;         // set by simulation

    // Tutorial
    class TutorialManager* tutorial_ptr = nullptr;  // set by simulation

    // Encyclopedia popup
    bool show_encyclopedia = false;
    int  encyclopedia_type = 0;    // particle type index

    // Scenarios
    class ScenarioManager* scenarios_ptr = nullptr;  // set by simulation
    bool show_scenario_menu = false;
    int  request_scenario_start = -1;  // set by UI, consumed by simulation tick

    // Camera navigation (set by info card click, consumed by simulation)
    int32_t navigate_to_particle = -1;

    // Readback data pointers (set by simulation each tick for info card display)
    uint32_t frame_counter_display = 0;
    const glm::vec2* readback_velocities = nullptr;
    uint32_t readback_count = 0;

    // Temperature display
    float log_temperature = 0.0f;  // log10(1) = 0.0 → 1 K

    // Sliders
    int   seed_value = 0;

    // Event notifications (top-right toast stack)
    struct Notification {
        std::string text;
        ImVec4 color;
        float timer;       // seconds remaining
        int32_t event_idx = -1;  // linked decay_log index (-1 = no link)
    };
    static constexpr float NOTIFY_DURATION = 5.0f;
    static constexpr int   NOTIFY_MAX = 8;
    std::vector<Notification> notifications;
    int32_t scroll_to_event_idx = -1;   // event log scroll target (-1 = none)

    void push_notification(const char* text, ImVec4 color = ImVec4(1,1,1,1));

    // Decay / interaction event log
    enum DecayEventType : uint8_t {
        DEVT_PARTICLE_DECAY = 0,    // SM particle decay (t→b, W→eν, etc.)
        DEVT_NUCLEAR_DECAY,         // Nuclear decay (α, β, n/p emission)
        DEVT_FUSION,                // Nuclear fusion
        DEVT_FISSION,               // Nuclear fission
        DEVT_ANNIHILATION,          // Particle-antiparticle annihilation
        DEVT_PHOTOELECTRIC,         // Photoelectric effect / Compton
        DEVT_SPALLATION,            // Nuclear spallation
        DEVT_PAIR_PRODUCTION,       // γ → e⁺e⁻
        DEVT_PION_PRODUCTION,       // Photopion (Δ resonance)
        DEVT_VMD,                   // Vector meson dominance
        DEVT_PHOTODISINTEGRATION,   // γ + A → (A-1) + n
        DEVT_BOND_FORMED,           // Covalent bond created
        DEVT_BOND_BROKEN,           // Covalent bond broken
        DEVT_BREMSSTRAHLUNG,        // Bremsstrahlung photon emission
        DEVT_NEUTRINO,              // Neutrino scatter / oscillation
        DEVT_WEAK_SCATTER,          // Weak flavor-changing scattering
    };
    static constexpr int DEVT_COUNT = 16;
    struct DecayLogEntry {
        std::string description;
        std::string details;        // multi-line detail shown on click
        DecayEventType type;
        ImVec4 color;
        uint32_t frame;             // frame number when event occurred
        std::time_t timestamp;      // wall-clock time when event occurred
    };
    static constexpr int DECAY_LOG_MAX = 10000;
    std::vector<DecayLogEntry> decay_log;
    bool show_decay_log = false;
    void save_event_to_disk(const char* desc, DecayEventType type);
    bool show_nuclear_debug = false;

    // Custom particle texture panel
    bool show_texture_panel = false;
    int  texture_panel_type = 0;         // selected type index
    struct ParticleTextureManager* texture_mgr = nullptr;  // set by simulation

    int32_t expanded_event_idx = -1;  // which event is expanded in log
    bool event_filter[DEVT_COUNT] = {true,true,true,true,true,true,true,true,true,true,true,true,true,true,true,true};

    void push_decay_event(const char* desc, DecayEventType type, ImVec4 color = ImVec4(1,0.6f,0.2f,1));
    void push_decay_event(const char* desc, DecayEventType type, ImVec4 color, const std::string& details);

    void init();
    void render_imgui(SimConfig& cfg, Particles& particles, ForceObject* force_objects, bool& request_reset);
    void save_prefs();
    void load_prefs();
    void save_molecule_bestiary();
    void load_molecule_bestiary();
    void set_vk_ctx(VulkanContext* ctx) { vk_ctx_ = ctx; }

private:
    void push_theme();
    void pop_theme();
    void draw_bottom_bar(SimConfig& cfg, bool& request_reset);
    void draw_top_bar(const SimConfig& cfg);
    void draw_settings_panel(SimConfig& cfg);
    void draw_spawn_menu(const SimConfig& cfg);
    void draw_info_card(const Particles& particles);
    void draw_force_object_panel(ForceObject* objects);
    void draw_splash_screen();
    void init_splash_particles();
    void draw_pause_menu(SimConfig& cfg, bool& request_reset);
    void draw_save_load_dialog();
    void refresh_browse_entries();
    void draw_element_card(const Particles& particles);
    void draw_molecule_card(const Particles& particles);
    void draw_element_list();
    void draw_particle_list(const Particles& particles);
    void draw_accelerator_panel();
    void draw_notifications();
    void draw_settings_menu();
    void draw_loading_overlay();
    void draw_achievements_panel();
    void draw_decay_log();
    void draw_particle_bestiary();
    void draw_element_bestiary();
    void draw_molecule_bestiary();
    void draw_nuclear_debug(SimConfig& cfg);
    void draw_tutorial_overlay();
    void draw_encyclopedia_popup();
    void draw_scenario_menu();
    void draw_scenario_goal_hud();
    void draw_measurement_overlays(const SimConfig& cfg);
    void draw_energy_heatmap(const SimConfig& cfg);
    void draw_velocity_field(const SimConfig& cfg);
    void draw_magnetic_field(const SimConfig& cfg);
    void draw_trajectory_traces(const SimConfig& cfg);
    void draw_force_vectors_overlay(const SimConfig& cfg);
    void draw_atom_grid(const SimConfig& cfg);
    void draw_orbit_paths(const SimConfig& cfg);
    void draw_gravity_map(const SimConfig& cfg);
    void draw_grav_waves(const SimConfig& cfg);
    void draw_measurement_panel();
    void draw_credits();
    void draw_howto();
    void draw_repository();
    void draw_texture_panel();
    void save_keybindings();
    void load_keybindings();

    // Splash animation state
    int   splash_variant_ = -1;   // -1=not chosen, 0=atom, 1=blue orb, 2=nebula, 3=collider
    struct SplashParticle {
        float x, y, vx, vy, r;
        ImU32 color, glow_color;
        bool  orbit;
        float orbit_r, orbit_speed, phase, cx, cy, tilt_x, tilt_y;
        float base_r;         // original radius for pulsing
        float pulse_phase;    // per-particle pulse offset
        int   type_idx;       // index into type table
    };
    std::vector<SplashParticle> splash_particles_;
    std::vector<std::vector<ImVec2>> splash_trails_;
    float splash_time_ = 0.0f;
    bool  splash_inited_ = false;
    void init_splash_blue_orb();
    void draw_splash_blue_orb();
    void init_splash_nebula();
    void draw_splash_nebula();
    void init_splash_collider();
    void draw_splash_collider();

    // Thumbnail cache for save/load dialog
    struct ThumbnailEntry {
        std::string filepath;       // full .ppsg path
        std::string name;           // stem (no extension)
        std::string date_str;       // "2026-02-27 14:30"
        uintmax_t   size = 0;
        VkImage       thumb_image  = VK_NULL_HANDLE;
        VkDeviceMemory thumb_memory = VK_NULL_HANDLE;
        VkImageView   thumb_view   = VK_NULL_HANDLE;
        ImTextureID   imgui_tex    = nullptr;
        bool          has_thumbnail = false;
    };
    std::vector<ThumbnailEntry> thumbnail_entries_;
    bool thumbnails_dirty_ = true;
    bool pending_free_thumbnails_ = false;  // deferred cleanup (safe across ImGui frames)

    VulkanContext* vk_ctx_ = nullptr;    // set by simulation during init
    VkSampler thumb_sampler_ = VK_NULL_HANDLE;

    void load_thumbnails();
    void free_thumbnails();
};

// Molecular formula builder (defined in ui_lists.cpp, used by ui_cards.cpp)
std::string build_molecular_formula(const std::vector<PhysicsInterface::ElementSummary>& elems,
                                     const std::vector<uint32_t>& atom_indices);
