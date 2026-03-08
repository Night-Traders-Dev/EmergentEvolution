#include "cosmos/ui/cosmos_ui_data.h"
#include "cosmos/cosmos_app_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

ImVec4 star_tint_ui(const CelestialBody& b) {
    float t = std::clamp((b.temperature - 1200.0f) / 32000.0f, 0.0f, 1.0f);
    glm::vec3 tint(
        std::clamp(1.25f - t * 0.95f, 0.0f, 1.0f),
        std::clamp(0.45f + t * 0.6f, 0.0f, 1.0f),
        std::clamp(-0.1f + t * 1.25f, 0.0f, 1.0f));
    float radius_solar = b.radius / (EARTH_RADIUS_SIM_UNITS * 109.1f);
    float giant_stage = (b.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_AGB ||
                         b.stellar_stage == SSTAGE_SUPERGIANT || b.stellar_stage == SSTAGE_HYPERGIANT)
        ? 1.0f : 0.0f;
    float giant = std::max(giant_stage, std::clamp((radius_solar - 6.0f) / 24.0f, 0.0f, 1.0f));
    float white_dwarf = (b.stellar_stage == SSTAGE_WHITE_DWARF) ? 1.0f : 0.0f;
    float neutron_star = (b.stellar_stage == SSTAGE_NEUTRON_STAR) ? 1.0f : 0.0f;
    float massive_hot = std::clamp((b.mass - 8.0f) / 32.0f, 0.0f, 1.0f) *
        std::clamp((b.temperature - 9000.0f) / 26000.0f, 0.0f, 1.0f);
    float cool = std::clamp((6000.0f - b.temperature) / 3600.0f, 0.0f, 1.0f);
    tint = glm::mix(tint, glm::vec3(1.00f, 0.62f, 0.34f), giant * std::max(cool, 0.35f) * 0.75f);
    tint = glm::mix(tint, glm::vec3(0.76f, 0.86f, 1.00f), massive_hot * 0.70f);
    tint = glm::mix(tint, glm::vec3(0.92f, 0.96f, 1.00f), white_dwarf * 0.90f);
    tint = glm::mix(tint, glm::vec3(0.72f, 0.84f, 1.00f), neutron_star * 0.96f);
    tint = glm::clamp(tint, glm::vec3(0.0f), glm::vec3(1.0f));
    return ImVec4(tint.r, tint.g, tint.b, 1.0f);
}

ImU32 body_color(const CelestialBody& b) {
    if (is_star_type(b.type)) {
        ImVec4 tint = star_tint_ui(b);
        return IM_COL32((int)(tint.x * 255.0f), (int)(tint.y * 255.0f),
                        (int)(tint.z * 255.0f), 255);
    }
    if (is_black_hole_type(b.type))
        return IM_COL32(18, 18, 22, 255);
    switch (b.type) {
    case CTYPE_PLANET:     return IM_COL32(60, 140, 220, 255);
    case CTYPE_MOON:       return IM_COL32(180, 180, 190, 255);
    case CTYPE_ASTEROID:   return IM_COL32(140, 130, 110, 255);
    case CTYPE_COMET:      return IM_COL32(160, 220, 255, 255);
    case CTYPE_NEBULA:     return IM_COL32(120, 60, 180, 255);
    case CTYPE_DUST:       return IM_COL32(205, 185, 160, 255);
    default:               return IM_COL32(200, 200, 200, 255);
    }
}

const char* const CTYPE_NAMES[] = {
    "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula",
    "O Star", "B Star", "A Star", "F Star", "G Star", "K Star", "M Star",
    "L Dwarf", "T Dwarf", "Y Dwarf", "Wolf-Rayet",
    "Stellar BH", "Intermediate BH", "Supermassive BH", "Primordial BH",
    "Dust",
};

const char* const PLANET_CLASS_NAMES[] = {
    "Dwarf Planet", "Terrestrial", "Ocean World", "Super-Earth", "Ice Giant", "Gas Giant",
};

const char* const MATERIAL_PHASE_NAMES[] = {
    "Solid", "Liquid", "Ice", "Gas", "Molten", "Plasma", "Collapsing Cloud",
};

const ImU32 CTYPE_COLORS[] = {
    IM_COL32(255, 200, 60, 255),   // Star - gold
    IM_COL32(60, 140, 220, 255),   // Planet - blue
    IM_COL32(180, 180, 190, 255),  // Moon - silver
    IM_COL32(140, 130, 110, 255),  // Asteroid - brown
    IM_COL32(160, 220, 255, 255),  // Comet - ice blue
    IM_COL32(18, 18, 22, 255),     // Black Hole - black
    IM_COL32(120, 60, 180, 255),   // Nebula - violet
    IM_COL32(120, 140, 255, 255),  // O - deep blue
    IM_COL32(160, 180, 255, 255),  // B - blue-white
    IM_COL32(220, 220, 255, 255),  // A - white
    IM_COL32(255, 255, 200, 255),  // F - yellow-white
    IM_COL32(255, 240, 100, 255),  // G - yellow (Sun)
    IM_COL32(255, 180, 60, 255),   // K - orange
    IM_COL32(255, 100, 60, 255),   // M - red
    IM_COL32(180, 60, 40, 255),    // L - dark red-brown
    IM_COL32(140, 40, 60, 255),    // T - magenta-brown
    IM_COL32(100, 30, 50, 255),    // Y - very dark
    IM_COL32(100, 180, 255, 255),  // WR - hot blue
    IM_COL32(24, 24, 30, 255),     // Stellar BH
    IM_COL32(18, 18, 24, 255),     // Intermediate BH
    IM_COL32(12, 12, 18, 255),     // Supermassive BH
    IM_COL32(32, 32, 40, 255),     // Primordial BH
    IM_COL32(205, 185, 160, 255),  // Dust
};

std::string_view imgui_label_key(const char* label) {
    if (!label) return {};
    const char* marker = std::strstr(label, "##");
    size_t len = marker ? static_cast<size_t>(marker - label) : std::strlen(label);
    return std::string_view(label, len);
}

const char* bottom_bar_menu_tooltip(std::string_view key) {
    struct Entry { const char* key; const char* tip; };
    static const Entry kEntries[] = {
        {"Menu", "Open the bottom-bar master menu with simulation, settings, diagnostics, and system tools."},
        {"Simulation", "Simulation-wide session actions such as pause, reset, save, and load."},
        {"Resume", "Continue simulation time from the current paused state."},
        {"Pause (Space)", "Pause physics integration and rendering updates tied to simulation time."},
        {"Resume (Space)", "Resume physics integration and simulation time after pausing."},
        {"New Simulation", "Reset to a fresh default system and clear accumulated simulation time."},
        {"Reset All Menu Parameters to Default", "Restore all tunable bottom-bar menu parameters to their startup defaults without clearing the current simulation."},
        {"Empty Universe", "Remove all bodies and start from an empty simulation state."},
        {"Save (Ctrl+S)", "Write the current simulation state to a .cssim save file."},
        {"Load (Ctrl+L)", "Load a saved simulation from disk and replace the current state."},
        {"Import Body...", "Import a single body definition into the current simulation."},
        {"Export Selected Body...", "Export the currently selected body to a standalone body file."},
        {"Panels", "Show or hide the major bottom-bar and side-panel UI windows."},
        {"Spawn", "Toggle the spawn studio window."},
        {"Bodies", "Toggle the bodies list window."},
        {"Spawn Menu", "Toggle the spawn studio window."},
        {"Body List", "Toggle the bodies list window."},
        {"Inspector", "Toggle the selected-body inspector window."},
        {"Show Orbits", "Draw derived orbit guides relative to each body's dominant primary."},
        {"Show Trails", "Draw recent motion trails for visible bodies."},
        {"Show Object Labels", "Draw body names next to visible objects in the viewport."},
        {"Auto-hide Bottom Bar", "Hide the bottom bar when the cursor is away from the screen edge."},
        {"System Management", "Apply system-wide orbital and structural transforms to all active bodies."},
        {"Balance System Momentum", "Remove net linear momentum so the system center of mass stops drifting."},
        {"Auto Orbit", "Recompute orbital velocities around each body's dominant primary using the auto-orbit solver."},
        {"Expand System", "Scale the system outward and reduce orbital speeds to preserve rough stability."},
        {"Shrink System", "Scale the system inward and increase orbital speeds accordingly."},
        {"Increase Eccentricity", "Increase radial motion relative to tangential motion to make orbits less circular."},
        {"Decrease Eccentricity", "Reduce radial motion and emphasize tangential motion to circularize orbits."},
        {"Make 2D - Zero All Height Values", "Flatten all bodies into the reference plane by clearing Y position and velocity."},
        {"Motion", "Apply bulk velocity, rotation, and time-direction edits to the active system."},
        {"Reverse Time", "Run the simulation backward by negating the effective time direction."},
        {"Reverse All Velocities", "Invert all linear velocities instantly."},
        {"Halt All Velocities", "Zero every body's linear velocity."},
        {"Halt All Rotations", "Zero every body's angular velocity."},
        {"+2% All Speeds", "Increase every body's linear speed by 2%."},
        {"-2% All Speeds", "Reduce every body's linear speed by 2%."},
        {"+2% All Rotations", "Increase every body's spin rate by 2%."},
        {"-2% All Rotations", "Reduce every body's spin rate by 2%."},
        {"Add Velocity of 10 km/s on X", "Add +10 km/s to every body's X velocity."},
        {"Add Velocity of -10 km/s on X", "Add -10 km/s to every body's X velocity."},
        {"Add Velocity of 10 km/s on Y", "Add +10 km/s to every body's Y velocity."},
        {"Add Velocity of -10 km/s on Y", "Add -10 km/s to every body's Y velocity."},
        {"Add Velocity of 10 km/s on Z", "Add +10 km/s to every body's Z velocity."},
        {"Add Velocity of -10 km/s on Z", "Add -10 km/s to every body's Z velocity."},
        {"Performance Management", "Remove transient debris and runaway bodies to recover performance."},
        {"Delete All Particles/Dust", "Delete non-attracting dust and dust-like transient debris."},
        {"Delete All Fragments", "Delete all generated fragments while keeping original intact bodies."},
        {"Delete All Escaping Bodies", "Cull bodies that are clearly unbound and leaving the dominant primary."},
        {"Cosmos Settings", "Simulation, rendering, thermal, and gravity settings for the cosmos sandbox."},
        {"General Physics", "Core gravity, integration, orbit-guide, trail, and label settings."},
        {"G", "Global gravitational constant scale used by the simulation."},
        {"Softening", "Minimum gravity smoothing distance used to avoid singular accelerations."},
        {"Damping", "Velocity damping multiplier applied after integration. 1.0 means no damping."},
        {"Collisions", "Enable collision detection and collision-response processing."},
        {"Tidal Forces", "Enable tidal heating and strain calculations from close gravitational encounters."},
        {"Tidal Heating Scale", "Scale the amount of tidal work converted into internal energy and temperature."},
        {"Integrator", "Choose the active orbital integrator used for gravity-driven motion during the simulation."},
        {"Velocity Verlet", "Use Velocity Verlet integration instead of the simpler fallback integrator."},
        {"Physics Substeps", "Split each rendered frame into multiple smaller physics steps for stability."},
        {"Barnes-Hut Gravity", "Approximate distant gravity sources with Barnes-Hut instead of direct N-body summation."},
        {"GPU BH Compute", "Evaluate Barnes-Hut gravity using the Vulkan compute path when available."},
        {"BH Theta", "Barnes-Hut opening angle. Lower values are slower and more accurate."},
        {"BH Min Bodies", "Minimum active body count before Barnes-Hut engages."},
        {"Parallel Min Batch", "Minimum work size before CPU-side parallel processing is used."},
        {"Orbit Opacity", "Opacity scale for orbit-guide lines."},
        {"Orbit Width", "Width scale for orbit-guide lines."},
        {"Trail Length", "Maximum stored trail samples per body."},
        {"Trail Opacity", "Opacity scale for motion trails."},
        {"Trail Width", "Width scale for motion trails."},
        {"Label Min Dist", "Closest camera distance at which object labels are allowed to appear."},
        {"Label Max Dist", "Farthest camera distance at which object labels are still drawn."},
        {"Label Opacity", "Opacity scale for viewport object labels."},
        {"Label Max Count", "Maximum number of object labels drawn at once."},
        {"Camera", "Viewport camera controls for the orbit camera."},
        {"FOV", "Camera field of view in degrees."},
        {"Distance", "Orbit-camera distance from its target point."},
        {"Reset Camera", "Restore the orbit camera to its default state."},
        {"Collision & Fragmentation", "Collision solver, fragmentation, and impact-response tuning."},
        {"Smoothed Particle Hydrodynamics", "Enable SPH-like soft-body pressure and viscosity during impacts."},
        {"Rigid Body Dynamics", "Enable rigid-body impulse and depenetration response during collisions."},
        {"SPH Pressure", "Scale the SPH pressure push applied to soft-body collision pairs."},
        {"SPH Viscosity", "Scale the SPH viscosity damping applied after soft-body contact."},
        {"SPH Heat", "Scale the amount of collision energy converted into SPH heating."},
        {"Rigid Restitution", "Bounce strength for rigid collisions that do not fragment."},
        {"Rigid Separation", "How aggressively overlapping rigid bodies are pushed apart."},
        {"Merging", "Allow sufficiently gentle impacts to merge bodies."},
        {"Fragmentation", "Allow sufficiently energetic impacts to break bodies into fragments."},
        {"Spin Fragmentation", "Allow excessive spin to shed mass or break bodies apart."},
        {"Merge Speed", "Approximate impact-speed threshold below which collisions merge."},
        {"Fragment Speed", "Approximate impact-speed threshold above which collisions fragment."},
        {"Collision Heating", "Fraction of collision energy converted into thermal/internal energy."},
        {"Spin Frag Threshold", "Fraction of critical breakup spin at which spin fragmentation starts."},
        {"Fragment Count", "Target fragment count for breakup events."},
        {"Min Frag Mass", "Minimum body mass that is still allowed to fragment."},
        {"Max Frag Depth", "Maximum fragment-generation depth before further breakup is suppressed."},
        {"Thermal & Roche", "Temperature, evaporation, Roche-limit, and ring-generation controls."},
        {"Temperature", "Enable thermal balance, heating, and cooling updates."},
        {"Cooling", "Radiative cooling rate that drives bodies back toward background temperature."},
        {"Evaporation", "Allow hot bodies near strong heating sources to lose mass."},
        {"Evaporation Rate", "Scale the mass-loss rate from thermal evaporation."},
        {"Roche Limit", "Enable Roche-limit breakup logic during close tidal encounters."},
        {"Fluid Roche Limit", "Use the fluid-body Roche formulation for weakly bound or fluid-like bodies."},
        {"Fluid Roche Scale", "Scale the fluid Roche distance used for disruption checks."},
        {"Rigid Roche Limit", "Use the rigid-body Roche formulation for stronger cohesive bodies."},
        {"Rigid Roche Scale", "Scale the rigid Roche distance used for disruption checks."},
        {"Material Phases", "Enable material phase transitions such as molten, gas, and collapsing states."},
        {"Material Phase Rate", "Scale how quickly thermal state changes drive material phase effects."},
        {"Planetary Rings", "Allow ring-system generation and maintenance from disrupted material."},
        {"Ring Inner Scale", "Global multiplier for ring inner radius."},
        {"Ring Outer Scale", "Global multiplier for ring outer radius."},
        {"Ring Density Scale", "Global multiplier for ring density."},
        {"Ring Thickness", "Global multiplier for ring thickness."},
        {"Ring Particle Scale", "Global multiplier for spawned ring dust count."},
        {"Ring Mass Scale", "Global multiplier for ring mass converted into dust."},
        {"Stellar", "Stellar evolution and nebula-to-star formation controls."},
        {"Stellar Evolution", "Enable long-timescale stellar evolution and fuel consumption."},
        {"Star Timescale", "Scale the rate of stellar evolution."},
        {"Stellar Wind Pressure", "Apply outward stellar wind and radiation pressure using luminosity over distance squared."},
        {"Wind Pressure Scale", "Scale the acceleration applied by stellar wind pressure to bodies."},
        {"Nebula Grav Advect", "How strongly gravitational acceleration steers nebula flow advection."},
        {"Nebula Grav Collapse", "Extra collapse pressure applied to nebulae from gravity coupling."},
        {"Nebula Grav Compress", "How strongly nebula density compresses under gravity coupling."},
        {"Enable Sink Formation", "Allow dense converging nebula regions to spawn protostellar sink bodies."},
        {"Sink Threshold", "Threshold for nebula density/collapse needed to form a sink."},
        {"Sink Min Mass", "Minimum mass assigned to a newly formed sink body."},
        {"Sink Spawn Fraction", "Fraction of host nebula mass allocated to each sink spawn."},
        {"Sink Consume Fraction", "Fraction of sink mass consumed from the host nebula field."},
        {"Rendering & Lighting", "Lighting, background, corona, comet-tail, and fabric-grid controls."},
        {"Star Lighting", "Enable point-light illumination from stars."},
        {"Star Light Strength", "Scale direct illumination from stellar light sources."},
        {"Uniform Lighting", "Use a non-physical uniform hemispheric light model for readability."},
        {"Uniform Light Strength", "Scale the uniform-light contribution."},
        {"Fast Star Lighting", "Use the fast strongest-star lighting path instead of summing many stars."},
        {"Ambient", "Ambient light floor added under star-light rendering."},
        {"HQ Shading", "Enable higher-detail procedural shading paths."},
        {"Background Starfield", "Render the background sky and starfield presets."},
        {"Background Preset", "Choose the sky/background style used behind the simulation."},
        {"Background Strength", "Scale the intensity of the background starfield and sky."},
        {"Star Corona", "Render stellar coronae, flares, prominence loops, and storm glow around stars."},
        {"Corona Strength", "Scale visible stellar corona and flare intensity."},
        {"Comet Tails", "Render illuminated comet tails."},
        {"Comet Tail Strength", "Scale comet-tail brightness and contribution."},
        {"Black Hole Lensing", "Render black-hole lensing distortion."},
        {"Lensing Strength", "Scale the strength of black-hole lensing."},
        {"Space Fabric Grid", "Render the gravity-warped space-fabric guide grid."},
        {"Fabric Square Size", "Grid spacing for the space-fabric overlay."},
        {"Fabric Curvature", "Curvature strength of the space-fabric warp field."},
        {"Snap Fabric View Isometric", "Snap the camera to an isometric angle that reads well with the fabric grid."},
        {"Cosmos Quality", "Overall rendering quality level for the cosmos shader path."},
        {"Time Control", "Target time-rate and adaptive timestep controls."},
        {"Time Rate", "Nominal simulation seconds advanced per real second, expressed on a log scale."},
        {"Rate", "Nominal simulation seconds advanced per real second, expressed on a log scale."},
        {"Adaptive Time-Stepping", "Clamp the actual step size dynamically to maintain stability."},
        {"Adaptive Time-Step", "Clamp the actual step size dynamically to maintain stability."},
        {"Adaptive Safety", "Safety factor used when adaptive timestep chooses a smaller step."},
        {"Adaptive Min dt", "Minimum simulation step allowed by adaptive stepping."},
        {"Adaptive Max dt", "Maximum simulation step allowed by adaptive stepping."},
        {"Adaptive Substepping", "Reject large physics steps and split them until the estimated position error fits the configured tolerance."},
        {"Adaptive Tolerance", "Maximum allowed world-space position error for one accepted step."},
        {"Adaptive Max Substeps", "Maximum number of accepted substeps before the sim slows down to preserve accuracy."},
        {"Substeps Used", "Number of substeps actually executed for the last requested physics step."},
        {"Required Substeps", "Number of substeps demanded by the error estimator for the last requested physics step."},
        {"Nominal Rate", "Requested simulation rate before adaptive timestep reduces or clamps it."},
        {"Sim Time", "Current accumulated simulated time in the active universe."},
        {"1 s/s", "Set the nominal simulation rate to 1 simulated second per real second."},
        {"1 min/s", "Set the nominal simulation rate to 1 simulated minute per real second."},
        {"1 hr/s", "Set the nominal simulation rate to 1 simulated hour per real second."},
        {"1 day/s", "Set the nominal simulation rate to 1 simulated day per real second."},
        {"1 yr/s", "Set the nominal simulation rate to 1 simulated year per real second."},
        {"1 Myr/s", "Set the nominal simulation rate to 1 simulated million years per real second."},
        {"1 Gyr/s", "Set the nominal simulation rate to 1 simulated billion years per real second."},
        {"General Relativity", "Relativistic correction controls for gravity and timing."},
        {"GR Corrections", "Enable relativistic orbit and timing corrections."},
        {"J2 Oblateness", "Enable J2 oblateness gravity correction for oblate rotating bodies."},
        {"Parallel Gravity", "Allow multithreaded CPU-side gravity work and related packing tasks."},
        {"Precession", "Scale periapsis precession strength."},
        {"Time Dilation", "Scale gravitational time dilation strength."},
        {"Frame Drag", "Scale frame-dragging strength."},
        {"Speed of Light", "Simulation-space speed of light used by relativistic corrections."},
        {"Performance", "Dynamic object budgeting and diagnostics."},
        {"Dynamic Budget", "Automatically manage fragment and debris counts to hold performance targets."},
        {"Object Budget", "Master toggle for dynamic fragment/debris budgeting."},
        {"Target FPS", "Preferred framerate the dynamic budgeter tries to preserve."},
        {"Max Fragments", "Maximum attracting fragments before new debris gets downgraded."},
        {"Max Non-Attracting", "Maximum non-attracting debris pieces allowed at once."},
        {"Explosion Density", "Maximum share of the debris budget that one event may consume."},
        {"Reduction Percentage", "How aggressively debris is culled when the budget is exceeded."},
        {"Dust Mode", "Choose whether dust bodies contribute gravity or remain non-attracting."},
        {"Diagnostics", "Runtime validation and debug logging controls."},
        {"Enable Runtime Diagnostics", "Enable periodic state validation and runtime diagnostics logging."},
        {"Pause on Invalid State", "Pause simulation automatically when diagnostics detect invalid body state."},
        {"Validate Now", "Run a manual state-validation pass immediately."},
        {"Dump Snapshot", "Write a diagnostic snapshot of current body counts and timing to the debug log."},
        {"Navigation", "Exit or return to the launcher."},
        {"Return to Launcher", "Close the current app session and return to the launcher."},
        {"Quit", "Exit the application."},
    };
    for (const auto& entry : kEntries) {
        if (key == entry.key)
            return entry.tip;
    }
    return nullptr;
}

void show_bottom_bar_tooltip(const char* label) {
    const char* tip = bottom_bar_menu_tooltip(imgui_label_key(label));
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tip);
}

void show_hover_tooltip(const char* tip) {
    if (tip && tip[0] &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tip);
    }
}

const char* format_sim_time(double seconds, char* buf, size_t buf_size) {
    double abs_s = std::abs(seconds);
    if (abs_s < 1e-6)
        snprintf(buf, buf_size, "%.1f ns", seconds * 1e9);
    else if (abs_s < 1e-3)
        snprintf(buf, buf_size, "%.1f us", seconds * 1e6);
    else if (abs_s < 1.0)
        snprintf(buf, buf_size, "%.1f ms", seconds * 1e3);
    else if (abs_s < 60.0)
        snprintf(buf, buf_size, "%.1f s", seconds);
    else if (abs_s < 3600.0)
        snprintf(buf, buf_size, "%.1f min", seconds / 60.0);
    else if (abs_s < 86400.0)
        snprintf(buf, buf_size, "%.1f hr", seconds / 3600.0);
    else if (abs_s < 3.156e7)
        snprintf(buf, buf_size, "%.1f day", seconds / 86400.0);
    else if (abs_s < 3.156e10)
        snprintf(buf, buf_size, "%.2f yr", seconds / 3.156e7);
    else if (abs_s < 3.156e13)
        snprintf(buf, buf_size, "%.2f kyr", seconds / 3.156e10);
    else if (abs_s < 3.156e16)
        snprintf(buf, buf_size, "%.2f Myr", seconds / 3.156e13);
    else if (abs_s < 3.156e19)
        snprintf(buf, buf_size, "%.2f Gyr", seconds / 3.156e16);
    else
        snprintf(buf, buf_size, "%.2f Tyr", seconds / 3.156e19);
    return buf;
}

void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                      ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 16;
    for (int s = STEPS; s >= 0; --s) {
        float t = (float)s / STEPS;
        float r = radius * t;
        if (r < 1.0f) continue;
        float blend = 1.0f - t;
        int a_c = (center_col >> IM_COL32_A_SHIFT) & 0xFF;
        int a_e = (edge_col   >> IM_COL32_A_SHIFT) & 0xFF;
        int a = a_c + (int)((a_e - a_c) * blend);
        int r_c = (center_col >> IM_COL32_R_SHIFT) & 0xFF, r_e = (edge_col >> IM_COL32_R_SHIFT) & 0xFF;
        int g_c = (center_col >> IM_COL32_G_SHIFT) & 0xFF, g_e = (edge_col >> IM_COL32_G_SHIFT) & 0xFF;
        int b_c = (center_col >> IM_COL32_B_SHIFT) & 0xFF, b_e = (edge_col >> IM_COL32_B_SHIFT) & 0xFF;
        int rr = r_c + (int)((r_e - r_c) * blend);
        int gg = g_c + (int)((g_e - g_c) * blend);
        int bb = b_c + (int)((b_e - b_c) * blend);
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 32);
    }
}
