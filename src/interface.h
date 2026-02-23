#pragma once

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"
#include "types.h"
#include "particles.h"
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

    // Initialise with a random seed
    void init();

    // Draw all ImGui windows and return updated config.
    // Call once per frame BEFORE ImGui::Render().
    // `request_reset` is set to true if the user clicks the Reset button.
    void render_imgui(SimConfig& cfg,
                      Particles& particles,
                      bool&      request_reset);

private:
    // Force grid state – one float per MAX_PARTICLE_TYPES² entry
    // We write directly into particles.forces; this just tracks colors.
    void draw_particle_grid(SimConfig& cfg, Particles& particles);

    // Convert force value to an ImVec4 colour
    static ImVec4 force_to_color(float f);
};
