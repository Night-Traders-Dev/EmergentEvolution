#pragma once

#include "imgui.h"
#include "types.h"
#include "particles.h"
#include <cstdint>

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
    bool settings_visible = true;
    bool spawn_menu_visible = false;
    bool pending_spawn = false;
    bool sim_running = true;

    // Spawn settings
    int   spawn_type    = 0;     // phys_particles.h type index
    int   spawn_count   = 1;
    float spawn_energy  = 0.7f;
    float spawn_scatter = 20.0f;
    int   spawn_group   = -1;    // -1=single particle, 0+=group template
    int   spawn_atom_Z  = -1;    // -1=not spawning atom, >0=atomic number
    int   spawn_atom_N  = -1;    // neutron count for spawn_atom_Z

    // Hover inspection
    int32_t hover_particle_idx = -1;

    // Force object interaction
    bool force_obj_placement_mode = false;
    int  force_obj_placement_type = 0;
    int  selected_force_obj_idx   = -1;
    bool force_obj_move_mode      = false;

    // Stats display
    float    fps_display = 0.0f;
    uint32_t active_particle_display  = 0;
    uint32_t dormant_particle_display = 0;
    float    total_energy_display     = 0.0f;
    float    avg_energy_display       = 0.0f;
    uint32_t type_counts_display[MAX_PARTICLE_TYPES] = {};

    // Field visualization (5 independent toggles)
    bool  field_em      = false;
    bool  field_strong  = false;
    bool  field_weak    = false;
    bool  field_gravity = false;
    bool  field_higgs   = false;
    float field_intensity = 0.5f;

    // Temperature display
    float log_temperature = 0.431f;  // log10(2.7) ≈ 0.431 → 2.7 K (vacuum/CMB)

    // Sliders
    float particle_count_slider = 70.0f;   // sqrt(5000) ~ 70
    int   seed_value = 0;

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
};
