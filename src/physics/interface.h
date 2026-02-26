#pragma once

#include "imgui.h"
#include "types.h"
#include "particles.h"
#include <cstdint>
#include <vector>
#include <string>
#include <glm/glm.hpp>

// ── User preferences (settings menu) ─────────────────────────────────────────
struct UserPrefs {
    int   temp_unit    = 0;     // 0=Kelvin, 1=Celsius, 2=Fahrenheit
    int   theme        = 0;     // 0=Dark Navy, 1=Midnight, 2=Slate, 3=Ember
    int   max_threads  = 8;     // OpenMP thread cap (1..system max)
    int   fps_cap      = 0;     // 0=uncapped, 30/60/120/144/240
    bool  show_fps     = true;
    float ui_scale     = 1.0f;  // 0.8 - 1.5
};

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

class PhysicsInterface {
public:
    bool show_splash = true;
    bool show_pause_menu = false;
    bool show_settings_menu = false;
    bool request_quit = false;
    UserPrefs prefs;
    bool settings_visible = true;
    bool spawn_menu_visible = true;
    bool pending_spawn = false;
    bool sim_running = true;

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
    char save_load_message[256] = {};
    float save_load_msg_timer = 0.0f;

    // Spawn settings
    int   spawn_type    = 0;     // phys_particles.h type index
    int   spawn_count   = 1;
    float spawn_energy  = 0.7f;
    float spawn_scatter = 20.0f;
    int   spawn_group   = -1;    // -1=single particle, 0+=group template
    int   spawn_atom_Z  = -1;    // -1=not spawning atom, >0=atomic number
    int   spawn_atom_N  = -1;    // neutron count for spawn_atom_Z

    // Hover / selected particle inspection
    int32_t hover_particle_idx    = -1;
    int32_t selected_particle_idx = -1;  // pinned particle for info card (-1=hover mode)
    bool    particle_move_mode    = false;
    bool    request_delete_particle = false;  // set by UI, consumed by simulation

    // Element detail card
    int32_t element_card_nucleus_rep = -1;  // nucleus rep index for element card (-1=hidden)
    bool request_element_delete    = false;
    bool request_element_duplicate = false;
    bool element_move_mode         = false;

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

    // Stats display
    float    fps_display = 0.0f;
    uint32_t active_particle_display  = 0;
    uint32_t dormant_particle_display = 0;
    float    total_energy_display     = 0.0f;
    float    avg_energy_display       = 0.0f;
    uint32_t type_counts_display[MAX_PARTICLE_TYPES] = {};

    // Field visualization (5 independent toggles)
    bool  field_em      = true;
    bool  field_strong  = true;
    bool  field_weak    = true;
    bool  field_gravity = true;
    bool  field_higgs   = true;
    float field_intensity = 0.5f;

    // Emergent feedback display
    float emergent_temp_display   = 0.0f;
    float emergent_bfield_display = 0.0f;
    uint32_t nuclear_decay_count_display = 0;
    uint32_t entangled_pair_count_display = 0;

    // Readback data pointers for entanglement visualization
    const glm::vec2*  readback_positions_ptr = nullptr;
    const uint32_t*   entangled_partners_ptr = nullptr;

    // Element list (populated by simulation from detected_nuclei_)
    struct ElementSummary {
        int Z, N;           // proton/neutron count
        int electrons;      // bound electron count
        uint32_t rep;       // nucleus representative index
    };
    std::vector<ElementSummary> element_list;
    bool show_element_list = false;

    // Camera navigation (set by info card click, consumed by simulation)
    int32_t navigate_to_particle = -1;

    // Readback data pointers (set by simulation each tick for info card display)
    uint32_t frame_counter_display = 0;
    const glm::vec2* readback_velocities = nullptr;
    uint32_t readback_count = 0;

    // Temperature display
    float log_temperature = 0.0f;  // log10(1) = 0.0 → 1 K

    // Sliders
    float particle_count_slider = 70.0f;   // sqrt(5000) ~ 70
    int   seed_value = 0;

    // Event notifications (top-right toast stack)
    struct Notification {
        std::string text;
        ImVec4 color;
        float timer;       // seconds remaining
    };
    static constexpr float NOTIFY_DURATION = 5.0f;
    static constexpr int   NOTIFY_MAX = 8;
    std::vector<Notification> notifications;

    void push_notification(const char* text, ImVec4 color = ImVec4(1,1,1,1));

    void init();
    void render_imgui(SimConfig& cfg, Particles& particles, ForceObject* force_objects, bool& request_reset);

private:
    void push_theme();
    void pop_theme();
    void draw_bottom_bar(SimConfig& cfg, bool& request_reset);
    void draw_settings_panel(SimConfig& cfg);
    void draw_spawn_menu(const SimConfig& cfg);
    void draw_info_card(const Particles& particles);
    void draw_force_object_panel(ForceObject* objects);
    void draw_splash_screen();
    void draw_pause_menu(SimConfig& cfg, bool& request_reset);
    void draw_save_load_dialog();
    void draw_element_card(const Particles& particles);
    void draw_element_list();
    void draw_accelerator_panel();
    void draw_notifications();
    void draw_settings_menu();
};
