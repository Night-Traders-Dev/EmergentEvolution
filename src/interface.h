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

    static ImVec4 force_to_color(float f);
};
