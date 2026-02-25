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

class PhysicsInterface {
public:
    bool settings_visible = true;
    bool spawn_menu_visible = false;
    bool pending_spawn = false;

    // Spawn settings
    int   spawn_type    = 0;     // phys_particles.h type index
    int   spawn_count   = 1;
    float spawn_energy  = 0.7f;
    float spawn_scatter = 20.0f;
    int   spawn_group   = -1;    // -1=single particle, 0+=group template

    // Hover inspection
    int32_t hover_particle_idx = -1;

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
    void render_imgui(SimConfig& cfg, Particles& particles, bool& request_reset);

private:
    void draw_spawn_menu(const SimConfig& cfg);
    void draw_hover_tooltip(const Particles& particles);
};
