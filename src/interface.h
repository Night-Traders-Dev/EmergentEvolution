#pragma once

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include "types.h"
#include "particles.h"
#include "organism.h"
#include "bond_manager.h"
#include <vector>
#include <array>
#include <glm/glm.hpp>

// Mirrors interface.gd.
// All state lives here; Simulation calls render_imgui() once per frame.

class Interface {
public:
    bool settings_visible = true;
    bool mouse_within     = false; // true when cursor is over the settings panel
    bool glow_enabled     = false;

    // ── F3 Spawn Menu ─────────────────────────────────────────────────────────
    bool  spawn_menu_visible = false;
    bool  pending_spawn      = false;  // waiting for user to left-click in world
    int   spawn_tab          = 0;      // 0=Atoms, 1=Groups, 2=Organisms, 3=Organics
    int   spawn_atom_type    = 1;      // 0=H..7=Cl (default C)
    int   spawn_group_idx    = 0;      // which molecule template
    int   spawn_organism_idx = -1;     // -1=predefined template, ≥0=live organism
    int   spawn_organic_idx  = 0;      // which bio-molecule template

    // ── Hover inspection (set by Simulation each tick) ────────────────────────
    int32_t                   hover_particle_idx = -1;
    const std::vector<float>* hover_energies_ptr = nullptr;

    // ── Decay + quantum stats (set by Simulation each tick) ───────────────────
    uint32_t decay_total_display  = 0;
    uint32_t vacuum_total_display = 0;

    // ── System stats (set by Simulation each tick) ────────────────────────────
    float    fps_display             = 0.0f;
    uint32_t active_particle_display = 0;   // energy > 0.01
    uint32_t dormant_particle_display= 0;   // energy ≤ 0.01 (pool)
    float    total_energy_display    = 0.0f;
    float    avg_energy_display      = 0.0f;
    uint32_t total_bonds_display     = 0;
    uint32_t photon_count_display    = 0;
    uint32_t sm_counts_display[5]    = {};  // [0]=α [1]=e⁻ [2]=e⁺ [3]=ν [4]=μ
    // Genome drift (rolling avg across all active particles)
    float    avg_electroneg_display  = 1.0f;
    float    avg_reactivity_display  = 1.0f;

    // Placement settings (apply to all spawn types)
    int   spawn_count        = 1;      // number of atoms to place per click (Atoms tab)
    float spawn_energy       = 0.7f;   // initial energy of placed particles
    float spawn_scatter      = 40.0f;  // scatter radius when count > 1 (pixels)

    // ── Sliders (raw slider values, converted to actual params by render_imgui) ─
    float particle_count_slider  = 150.0f;  // particle_count = pow(value, 2)
    float particle_types_slider  = 5.0f;
    float particle_radius_slider = 2.0f;
    float dampening_slider       = 0.85f;
    float repulsion_slider       = 20.0f;
    float interaction_slider     = 60.0f;
    float density_limit_slider   = 60.0f;
    int   seed_value             = 0;
    bool  reset_colors_check     = false;
    bool  reset_forces_check     = true;

    // Per-type archetype selection: 0=Default,1=Repeller,2=Polar,3=Heavy,4=Catalyst,5=Adhesive,6=Radical,7=Donor,8=Acceptor
    int archetype_selection[MAX_PARTICLE_TYPES] = {};

    // Initialise with a random seed
    void init();

    // Draw all ImGui windows and return updated config.
    // Call once per frame BEFORE ImGui::Render().
    // `request_reset` is set to true if the user clicks the Reset button.
    void render_imgui(SimConfig&       cfg,
                      Particles&       particles,
                      OrganismManager& org_manager,
                      BondManager&     bond_manager,
                      bool&            request_reset);

private:
    void draw_particle_grid(SimConfig& cfg, Particles& particles);
    void draw_organism_panel(OrganismManager& org_manager,
                             const BondManager& bond_manager,
                             const Particles& particles,
                             const SimConfig& cfg);

    void draw_archetype_panel(Particles& particles, const SimConfig& cfg);
    void draw_spawn_menu(const OrganismManager& org_manager,
                         const Particles& particles,
                         const SimConfig& cfg);
    void draw_hover_tooltip(const OrganismManager& org_manager,
                            const Particles& particles,
                            const BondManager& bond_manager);

    static ImVec4 force_to_color(float f);
};
