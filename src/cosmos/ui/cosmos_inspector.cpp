#include "cosmos/cosmos_app_internal.h"
#include "cosmos/ui/cosmos_ui_data.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ── Helpers for Universe-Sandbox-2-style layout ─────────────────────────────

namespace {

// Right-aligned value text on the same line as a label
void inspector_row(const char* label, const char* fmt, ...) {
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.62f, 1.0f), "%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);

    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    ImGui::TextUnformatted(buf);
}

// Progress bar row: label on left, bar on right
void inspector_bar(const char* label, float fraction, const char* overlay = nullptr) {
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.62f, 1.0f), "%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::PushItemWidth(-1);
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-1, 14), overlay);
    ImGui::PopItemWidth();
}

// Thin separator line with spacing
void thin_separator() {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f, 0.30f, 0.35f, 0.5f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Body type indicator dot color
ImVec4 type_dot_color(uint32_t type) {
    if (is_star_type(type))        return ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
    if (is_black_hole_type(type))  return ImVec4(0.6f, 0.3f, 1.0f, 1.0f);
    if (is_galaxy_type(type))      return ImVec4(0.9f, 0.6f, 1.0f, 1.0f);
    if (type == CTYPE_NEBULA)      return ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
    if (type == CTYPE_COMET)       return ImVec4(0.5f, 0.9f, 0.9f, 1.0f);
    if (type == CTYPE_ASTEROID)    return ImVec4(0.7f, 0.65f, 0.55f, 1.0f);
    if (type == CTYPE_DUST)        return ImVec4(0.6f, 0.55f, 0.45f, 1.0f);
    if (type == CTYPE_MOON)        return ImVec4(0.7f, 0.7f, 0.75f, 1.0f);
    return ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // planet default
}

const char* galaxy_morphology_name(uint32_t type) {
    switch (type) {
        case CTYPE_GALAXY_SPIRAL:     return "Spiral";
        case CTYPE_GALAXY_ELLIPTICAL: return "Elliptical";
        case CTYPE_GALAXY_IRREGULAR:  return "Irregular";
        case CTYPE_GALAXY_LENTICULAR: return "Lenticular";
        case CTYPE_GALAXY_DWARF:      return "Dwarf";
        default:                      return "Unknown";
    }
}

} // anon namespace

// ── Main inspector draw ─────────────────────────────────────────────────────

void CosmosApp::draw_inspector() {
    if (!inspector_visible_) return;
    if (selected_body < 0 || selected_body >= (int)state.bodies.size()) {
        inspector_visible_ = false;
        return;
    }

    auto& b = state.bodies[selected_body];
    const auto& vp = b.cached_visuals;
    MaterialComposition materials = derive_materials(b);
    ComparisonMetrics comparisons = derive_comparisons(b);
    MagneticMetrics magnetic = derive_magnetic_metrics(b, cfg.G);
    ImGuiIO& io = ImGui::GetIO();

    // ── Window styling (dark semi-transparent, Universe Sandbox 2 aesthetic) ──
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.10f, 0.13f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.12f, 0.16f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.14f, 0.14f, 0.18f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.24f, 0.24f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.20f, 0.20f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.30f, 0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 350.0f, 46.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 220), ImVec2(440, 860));

    if (!ImGui::Begin("Inspector", &inspector_visible_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(7);
        return;
    }

    // ── Header: colored dot + name + type + action buttons ──────────────────

    const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Unknown";
    const char* display_name = b.name.empty() ? type_name : b.name.c_str();

    // Type indicator dot
    ImVec4 dot_col = type_dot_color(b.type);
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float dot_y = cursor.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddCircleFilled(ImVec2(cursor.x + 5.0f, dot_y), 5.0f,
                        ImGui::ColorConvertFloat4ToU32(dot_col));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);

    // Body name
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.92f, 0.85f, 1.0f));
    ImGui::TextWrapped("%s", display_name);
    ImGui::PopStyleColor();

    if (!b.name.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.52f, 1.0f), "(%s)", type_name);
    }

    // ── Header action buttons (right side): Focus  Lock  X ──────────────────
    {
        float btn_x = ImGui::GetWindowWidth() - 150.0f;
        ImGui::SameLine(btn_x);

        // Focus / Track button
        bool is_tracked = camera.focus_active && camera.focus_body == selected_body;
        if (is_tracked) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.35f, 0.12f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.45f, 0.15f, 1.0f));
            if (ImGui::SmallButton("Untrack")) camera.release_focus();
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.50f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.40f, 0.60f, 1.0f));
            if (ImGui::SmallButton("Focus")) {
                camera.focus_on(b.pos, selected_body, b.radius);
                camera.target_distance = std::max(b.radius * 8.0f, 30.0f);
            }
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(is_tracked ? "Release camera focus" : "Focus camera on body");

        ImGui::SameLine();

        // Lock button
        if (b.locked) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.25f, 0.10f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.35f, 0.15f, 1.0f));
            if (ImGui::SmallButton("Unlock")) b.locked = false;
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.25f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.38f, 1.0f));
            if (ImGui::SmallButton("Lock")) {
                b.locked = true;
                b.vel = glm::vec3(0.0f);
            }
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(b.locked ? "Unlock body (allow forces)" : "Lock position (freeze)");

        ImGui::SameLine();

        // Prominent close button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.12f, 0.12f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.18f, 0.18f, 1.0f));
        if (ImGui::SmallButton("X")) {
            inspector_visible_ = false;
        }
        ImGui::PopStyleColor(2);
    }

    thin_separator();

    // ── Tabbed interface ────────────────────────────────────────────────────

    bool is_star = is_star_type(b.type);
    bool is_bh = is_black_hole_type(b.type);
    bool is_galaxy = is_galaxy_type(b.type);
    bool is_planet_moon = (b.type == CTYPE_PLANET || b.type == CTYPE_MOON);

    if (ImGui::BeginTabBar("##InspectorTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {

        // ════════════════════════════════════════════════════════════════════
        // OVERVIEW TAB
        // ════════════════════════════════════════════════════════════════════
        if (ImGui::BeginTabItem("Overview")) {

            ImGui::Spacing();

            // Mass
            constexpr double SOLAR_MASS_KG = 1.98847e30;
            constexpr double KG_TO_LBS = 2.20462262185;
            double mass_kg = (double)b.mass * SOLAR_MASS_KG;
            double mass_lbs = mass_kg * KG_TO_LBS;
            {
                char buf[128];
                snprintf(buf, sizeof(buf), "%.3e kg / %.3e lbs", mass_kg, mass_lbs);
                inspector_row("Mass", "%s", buf);
            }

            // Radius
            constexpr float KM_TO_MILES = 0.6213712f;
            float radius_km = b.radius;
            float radius_miles = radius_km * KM_TO_MILES;
            inspector_row("Radius", "%.1f km / %.1f mi", radius_km, radius_miles);

            // Temperature
            float temp_c = b.temperature - 273.15f;
            float temp_f = temp_c * 9.0f / 5.0f + 32.0f;
            inspector_row("Temperature", "%.0f K (%.1f C / %.1f F)", b.temperature, temp_c, temp_f);

            // Material phase
            const char* phase_name = (b.material_phase <= PHASE_COLLAPSING)
                ? MATERIAL_PHASE_NAMES[b.material_phase] : "?";
            if (b.collapse_progress > 0.01f && b.material_phase == PHASE_COLLAPSING) {
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "%s %.0f%%", phase_name, b.collapse_progress * 100.0f);
                inspector_row("Phase", "%s", pbuf);
            } else {
                inspector_row("Phase", "%s", phase_name);
            }

            // Speed
            constexpr float KMH_TO_MPH = 0.6213712f;
            float speed_kmh = glm::length(b.vel) * SIM_UNIT_TO_KM * 3600.0f;
            float speed_mph = speed_kmh * KMH_TO_MPH;
            inspector_row("Speed", "%.1f km/h / %.1f mph", speed_kmh, speed_mph);

            // Position
            inspector_row("Position", "%.0f, %.0f, %.0f", b.pos.x, b.pos.y, b.pos.z);

            // Spin
            if (std::abs(b.angular_vel) > 1e-6f) {
                inspector_row("Spin", "%.3f rad/s", b.angular_vel);
            }

            // Axial tilt
            if (std::abs(b.axial_tilt) > 0.01f) {
                inspector_row("Axial Tilt", "%.1f deg", b.axial_tilt * 57.2957795f);
            }

            // Age
            char age_buf[64];
            format_sim_time((double)b.age, age_buf, sizeof(age_buf));
            inspector_row("Age", "%s", age_buf);

            thin_separator();

            // ── Cumulative / derived properties ──
            float density = body_density(b);
            float volume = body_volume(b);
            float surface_g = body_surface_gravity(b, cfg.G);
            float escape_v = body_escape_velocity(b, cfg.G);

            inspector_row("Density", "%.4g M/u^3", density);
            inspector_row("Volume", "%.4g u^3", volume);
            inspector_row("Surface Gravity", "%.4f", surface_g);
            inspector_row("Escape Velocity", "%.4f", escape_v);

            float calc_radius = is_star ? expected_star_radius(b) :
                                (is_planet_moon ? expected_planet_radius(std::min(b.mass, 0.02f)) : b.radius);
            inspector_row("Calc. Radius", "%.2f", calc_radius);
            inspector_row("Mass Loss Rate", "%.4e M/s", b.mass_loss_rate);
            inspector_row("Mass Loss Total", "%.4e M", b.mass_loss_total);

            if (ImGui::Button("Reset Mass Loss Total", ImVec2(-1, 0)))
                b.mass_loss_total = 0.0f;

            // Comparisons (planets/moons)
            if (is_planet_moon) {
                thin_separator();
                inspector_bar("Earth Similarity", comparisons.earth_similarity);
                inspector_bar("Life Likelihood", comparisons.life_likelihood);
            }

            // Habitable zone (stars)
            if (is_star && b.habitable_zone_outer > 0.01f) {
                thin_separator();
                inspector_row("Habitable Zone", "%.1f - %.1f", b.habitable_zone_inner, b.habitable_zone_outer);
            }

            // Hawking temp (BH)
            if (is_bh && b.hawking_temperature > 0.0f) {
                thin_separator();
                inspector_row("Hawking Temp", "%.3e K", b.hawking_temperature);
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // SURFACE TAB (planets/moons)
        // ════════════════════════════════════════════════════════════════════
        if (is_planet_moon && ImGui::BeginTabItem("Surface")) {

            ImGui::Spacing();

            if (b.props_valid) {
                const auto& pp = b.cached_props;

                static const char* SURF_NAMES[] = {"Rocky", "Liquid", "Frozen", "Gas Giant", "Mixed"};
                static const char* OCEAN_NAMES[] = {"None", "Water", "Methane", "Ammonia", "Lava"};
                static const char* WEATHER_NAMES[] = {"None", "Storms", "Rain", "Snow", "Dust"};

                inspector_row("Surface Type", "%s", SURF_NAMES[pp.surface]);
                inspector_row("Planet Class", "%s", PLANET_CLASS_NAMES[pp.planet_class]);

                thin_separator();

                // ── Atmosphere ──
                if (pp.atmosphere.pressure > 0.01f) {
                    ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.90f, 1.0f), "Atmosphere");
                    ImGui::Spacing();

                    inspector_row("Pressure", "%.2f atm", pp.atmosphere.pressure);

                    // Dominant gas
                    float max_frac = 0;
                    const char* dom_gas = "N2";
                    struct GasEntry { float frac; const char* name; };
                    GasEntry gases[] = {
                        {pp.atmosphere.n2_frac, "N2"}, {pp.atmosphere.o2_frac, "O2"},
                        {pp.atmosphere.co2_frac, "CO2"}, {pp.atmosphere.h2_frac, "H2"},
                        {pp.atmosphere.he_frac, "He"}, {pp.atmosphere.ch4_frac, "CH4"},
                        {pp.atmosphere.nh3_frac, "NH3"},
                    };
                    for (auto& g : gases) {
                        if (g.frac > max_frac) { max_frac = g.frac; dom_gas = g.name; }
                    }
                    float second_max = 0;
                    const char* second_gas = "";
                    for (auto& g : gases) {
                        if (g.frac > second_max && g.name != dom_gas) {
                            second_max = g.frac; second_gas = g.name;
                        }
                    }
                    char comp_buf[128];
                    if (second_max > 0.05f)
                        snprintf(comp_buf, sizeof(comp_buf), "%s %.0f%%, %s %.0f%%",
                                 dom_gas, max_frac * 100.0f, second_gas, second_max * 100.0f);
                    else
                        snprintf(comp_buf, sizeof(comp_buf), "%s %.0f%%", dom_gas, max_frac * 100.0f);
                    inspector_row("Composition", "%s", comp_buf);

                    if (pp.atmosphere.has_clouds) {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pp.cloud_coverage);
                        inspector_bar("Cloud Cover", pp.cloud_coverage / 100.0f, overlay);
                    }
                }

                {
                    char overlay[32];
                    snprintf(overlay, sizeof(overlay), "%.0f%%", std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 100.0f);
                    inspector_bar("Atm Health", std::clamp(b.atmosphere_retention, 0.0f, 1.0f), overlay);
                }

                thin_separator();

                // ── Ocean ──
                if (pp.ocean_type != OCEAN_NONE) {
                    ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Ocean");
                    ImGui::Spacing();
                    inspector_row("Type", "%s", OCEAN_NAMES[pp.ocean_type]);
                    {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pp.ocean_coverage);
                        inspector_bar("Coverage", pp.ocean_coverage / 100.0f, overlay);
                    }
                    inspector_row("Depth", "%.1f km", pp.ocean_depth);
                    thin_separator();
                }

                // ── Terrain ──
                if (pp.has_mountains || pp.has_continents || pp.has_islands ||
                    pp.has_rivers || pp.has_ice_sheets || pp.has_iron_core) {

                    ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.60f, 1.0f), "Terrain");
                    ImGui::Spacing();

                    if (pp.has_mountains)
                        inspector_row("Mountains", "%.1f km", pp.mountain_height);
                    if (pp.has_continents)
                        inspector_row("Continents", "%d / %.0f%%", pp.continent_count, pp.continent_coverage);
                    if (pp.has_islands) {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pp.island_coverage);
                        inspector_bar("Islands", pp.island_coverage / 100.0f, overlay);
                    }
                    if (pp.has_rivers)
                        inspector_row("Rivers", "%.0f%% density", pp.river_density * 100.0f);
                    if (pp.has_ice_sheets) {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pp.ice_sheet_coverage);
                        inspector_bar("Ice Sheets", pp.ice_sheet_coverage / 100.0f, overlay);
                    }
                    if (pp.has_iron_core)
                        inspector_row("Iron Core", "Yes");

                    thin_separator();
                }

                // ── Weather ──
                if (pp.has_weather) {
                    inspector_row("Weather", "%s", WEATHER_NAMES[pp.weather_type]);
                }

                // ── Vegetation ──
                if (pp.vegetation_coverage > 1.0f) {
                    char overlay[32];
                    snprintf(overlay, sizeof(overlay), "%.0f%%", pp.vegetation_coverage);
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.62f, 1.0f), "Vegetation");
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
                    ImGui::PushItemWidth(-1);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.25f, 0.70f, 0.25f, 1.0f));
                    ImGui::ProgressBar(pp.vegetation_coverage / 100.0f, ImVec2(-1, 14), overlay);
                    ImGui::PopStyleColor();
                    ImGui::PopItemWidth();
                }

                // ── Rings ──
                if (b.ring_density > 0.01f) {
                    thin_separator();
                    inspector_row("Rings", "%.2f - %.2f", b.ring_inner_radius, b.ring_outer_radius);
                    {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", b.ring_density * 100.0f);
                        inspector_bar("Ring Density", b.ring_density, overlay);
                    }
                }

                // ── Visual details ──
                if (b.visuals_valid) {
                    thin_separator();
                    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.75f, 1.0f), "Visual Details");
                    ImGui::Spacing();
                    inspector_row("Roughness", "%.2f", vp.roughness);
                    inspector_row("Haze", "%.2f", vp.haze_density);
                    inspector_row("Cratering", "%.2f", vp.crater_density);
                    inspector_row("Weather FX", "%.2f", vp.weather_strength);
                    if (vp.volcanic_activity > 0.01f)
                        inspector_row("Volcanism", "%.2f", vp.volcanic_activity);
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Surface data not yet computed.");
            }

            ImGui::EndTabItem();
        }

        // Small bodies surface tab
        if ((b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST) &&
            ImGui::BeginTabItem("Surface")) {

            ImGui::Spacing();

            if (b.visuals_valid) {
                const char* small_body_class = "Icy";
                if (b.type == CTYPE_DUST) {
                    small_body_class = "Dust Aggregate";
                } else if (b.type == CTYPE_ASTEROID) {
                    switch ((SmallBodyClass)vp.subtype) {
                    case SMALLBODY_C: small_body_class = "Carbonaceous"; break;
                    case SMALLBODY_S: small_body_class = "Silicate"; break;
                    case SMALLBODY_M: small_body_class = "Metallic"; break;
                    case SMALLBODY_ICY: small_body_class = "Icy"; break;
                    }
                }

                inspector_row("Class", "%s", b.type == CTYPE_COMET ? "Cometary Ice" : small_body_class);

                {
                    char overlay_ice[32], overlay_metal[32];
                    snprintf(overlay_ice, sizeof(overlay_ice), "%.0f%%", vp.ice_frac * 100.0f);
                    snprintf(overlay_metal, sizeof(overlay_metal), "%.0f%%", vp.metal_frac * 100.0f);
                    inspector_bar("Ice", vp.ice_frac, overlay_ice);
                    inspector_bar("Metal", vp.metal_frac, overlay_metal);
                }

                inspector_row("Cratering", "%.2f", vp.crater_density);

                if (b.type == CTYPE_COMET) {
                    inspector_row("Coma", "%.2f", vp.coma_strength);
                    inspector_row("Tail", "%.2f", vp.tail_strength);
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Visual data not available.");
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // ORBIT TAB
        // ════════════════════════════════════════════════════════════════════
        if (ImGui::BeginTabItem("Orbit")) {

            ImGui::Spacing();

            if (b.parent >= 0 && b.parent < (int)state.bodies.size()) {
                const auto& par = state.bodies[b.parent];
                const char* par_name = par.name.empty()
                    ? CTYPE_NAMES[std::min(par.type, (uint32_t)CTYPE_COUNT - 1)]
                    : par.name.c_str();

                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.62f, 1.0f), "Parent");
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.40f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.55f, 1.0f));
                if (ImGui::SmallButton(par_name)) {
                    selected_body = b.parent;
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to select parent");

                float orb_dist = glm::length(b.pos - par.pos);
                inspector_row("Distance", "%.1f", orb_dist);

                if (orb_dist > 0.1f) {
                    float orb_v = std::sqrt(cfg.G * par.mass / orb_dist);
                    float period = 2.0f * 3.14159f * orb_dist / std::max(orb_v, 0.01f);
                    char period_buf[64];
                    format_sim_time((double)period, period_buf, sizeof(period_buf));
                    inspector_row("Period", "%s", period_buf);
                }

                thin_separator();
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No orbital parent.");
                thin_separator();
            }

            // Stored orbital elements (always shown if available)
            if (b.orbital_period > 1.0e-3f) {
                char period_buf[64];
                format_sim_time((double)b.orbital_period, period_buf, sizeof(period_buf));
                inspector_row("Orbital Period", "%s", period_buf);
                inspector_row("Eccentricity", "%.4f", b.orbital_eccentricity);
                inspector_row("Semi-Major Axis", "%.1f", b.orbital_semi_major);
            }

            if (b.tidal_lock_progress > 0.01f) {
                char overlay[32];
                snprintf(overlay, sizeof(overlay), "%.0f%%", b.tidal_lock_progress * 100.0f);
                inspector_bar("Tidal Lock", b.tidal_lock_progress, overlay);
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // COMPOSITION TAB
        // ════════════════════════════════════════════════════════════════════
        if (ImGui::BeginTabItem("Composition")) {

            ImGui::Spacing();

            // Material fractions with progress bars
            {
                char ov[32];
                snprintf(ov, sizeof(ov), "%.0f%%", materials.iron * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.60f, 0.40f, 0.25f, 1.0f));
                inspector_bar("Iron", materials.iron, ov);
                ImGui::PopStyleColor();
            }
            {
                char ov[32];
                snprintf(ov, sizeof(ov), "%.0f%%", materials.silicate * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.55f, 0.50f, 0.40f, 1.0f));
                inspector_bar("Silicate", materials.silicate, ov);
                ImGui::PopStyleColor();
            }
            {
                char ov[32];
                snprintf(ov, sizeof(ov), "%.0f%%", materials.water * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.50f, 0.80f, 1.0f));
                inspector_bar("Water", materials.water, ov);
                ImGui::PopStyleColor();
            }
            {
                char ov[32];
                snprintf(ov, sizeof(ov), "%.0f%%", materials.hydrogen * 100.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.65f, 0.65f, 0.80f, 1.0f));
                inspector_bar("Hydrogen", materials.hydrogen, ov);
                ImGui::PopStyleColor();
            }

            thin_separator();

            // ── Magnetic Fields ──
            if (magnetic.show_magnetosphere || magnetic.show_magnetic_axis || magnetic.particle_jets ||
                is_star) {

                ImGui::TextColored(ImVec4(0.60f, 0.70f, 0.90f, 1.0f), "Magnetic Fields");
                ImGui::Spacing();

                inspector_row("Field Strength", "%.3f", magnetic.magnetic_field);
                inspector_row("Pole Angle", "%.1f deg", magnetic.magnetic_pole_angle);

                if (magnetic.show_magnetosphere) {
                    inspector_row("Magnetosphere", "%.2f", magnetic.magnetosphere_size);
                }

                inspector_row("Mag. Axis Visible", "%s", magnetic.show_magnetic_axis ? "Yes" : "No");

                if (magnetic.particle_jets || magnetic.make_pulsar) {
                    inspector_row("Particle Jets", "%s", magnetic.particle_jets ? "Yes" : "No");
                    inspector_row("Pulsar", "%s", magnetic.make_pulsar ? "Yes" : "No");
                }
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // STELLAR TAB (stars only)
        // ════════════════════════════════════════════════════════════════════
        if (is_star && ImGui::BeginTabItem("Stellar")) {

            ImGui::Spacing();

            static const char* STAGE_NAMES[] = {
                "Main Sequence", "Subgiant", "Red Giant", "Horizontal Branch",
                "AGB", "Supergiant", "Hypergiant", "White Dwarf", "Neutron Star"
            };

            const char* stage = (b.stellar_stage < SSTAGE_COUNT) ? STAGE_NAMES[b.stellar_stage] : "?";
            inspector_row("Stage", "%s", stage);

            {
                char overlay[32];
                snprintf(overlay, sizeof(overlay), "%.1f%%", b.fuel * 100.0f);
                inspector_bar("Fuel", b.fuel, overlay);
            }

            inspector_row("Luminosity", "%.2f L", b.luminosity);

            if (b.visuals_valid) {
                thin_separator();

                ImGui::TextColored(ImVec4(0.75f, 0.70f, 0.55f, 1.0f), "Visual Properties");
                ImGui::Spacing();

                inspector_row("Corona", "%.2f", vp.corona_strength);
                inspector_row("Flare Activity", "%.2f", vp.flare_activity);
                inspector_row("Granulation", "%.2f @ %.1f", vp.terrain_amp, vp.terrain_freq);

                if (vp.star_spot_coverage > 0.01f)
                    inspector_row("Spot Coverage", "%.1f%%", vp.star_spot_coverage * 100.0f);
                if (vp.star_pulsation > 0.01f)
                    inspector_row("Pulsation", "%.2f", vp.star_pulsation);
                if (std::abs(vp.star_differential_rotation) > 0.01f)
                    inspector_row("Diff. Rotation", "%.2f", vp.star_differential_rotation);
            }

            if (b.habitable_zone_outer > 0.01f) {
                thin_separator();
                inspector_row("Habitable Zone", "%.1f - %.1f", b.habitable_zone_inner, b.habitable_zone_outer);
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // BLACK HOLE TAB
        // ════════════════════════════════════════════════════════════════════
        if (is_bh && ImGui::BeginTabItem("Black Hole")) {

            ImGui::Spacing();

            float rs = 2.0f * cfg.G * b.mass / (cfg.speed_of_light * cfg.speed_of_light);
            inspector_row("Schwarzschild r", "%.4f", rs);

            if (b.hawking_temperature > 0.0f)
                inspector_row("Hawking Temp", "%.3e K", b.hawking_temperature);

            if (b.visuals_valid) {
                thin_separator();

                ImGui::TextColored(ImVec4(0.60f, 0.45f, 0.85f, 1.0f), "Visual Properties");
                ImGui::Spacing();

                inspector_row("Lensing", "%.2f", vp.lensing_strength);
                inspector_row("Accretion", "%.2f", vp.accretion_strength);
                inspector_row("Jet Strength", "%.2f", vp.jet_strength);
            }

            ImGui::EndTabItem();
        }

        // ════════════════════════════════════════════════════════════════════
        // GALAXY TAB
        // ════════════════════════════════════════════════════════════════════
        if (is_galaxy && ImGui::BeginTabItem("Galaxy")) {

            ImGui::Spacing();

            inspector_row("Morphology", "%s", galaxy_morphology_name(b.type));

            if (b.visuals_valid) {
                thin_separator();

                ImGui::TextColored(ImVec4(0.80f, 0.70f, 0.90f, 1.0f), "Structure");
                ImGui::Spacing();

                inspector_row("Arm Count", "%.0f", vp.galaxy_arm_count);
                inspector_row("Arm Tightness", "%.2f", vp.galaxy_arm_tightness);

                {
                    char ov[32];
                    snprintf(ov, sizeof(ov), "%.0f%%", vp.galaxy_bar_strength * 100.0f);
                    inspector_bar("Bar Strength", vp.galaxy_bar_strength, ov);
                }
                {
                    char ov[32];
                    snprintf(ov, sizeof(ov), "%.0f%%", vp.galaxy_bulge_ratio * 100.0f);
                    inspector_bar("Bulge Ratio", vp.galaxy_bulge_ratio, ov);
                }

                inspector_row("Disk Thickness", "%.2f", vp.galaxy_disk_thickness);

                thin_separator();

                ImGui::TextColored(ImVec4(0.80f, 0.70f, 0.90f, 1.0f), "Density & Dust");
                ImGui::Spacing();

                {
                    char ov[32];
                    snprintf(ov, sizeof(ov), "%.0f%%", vp.galaxy_star_density * 100.0f);
                    inspector_bar("Star Density", vp.galaxy_star_density, ov);
                }
                {
                    char ov[32];
                    snprintf(ov, sizeof(ov), "%.0f%%", vp.galaxy_dust_lane * 100.0f);
                    inspector_bar("Dust Lanes", vp.galaxy_dust_lane, ov);
                }

                inspector_row("Halo Extent", "%.2f", vp.galaxy_halo_extent);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ── Bottom action buttons ───────────────────────────────────────────────
    thin_separator();

    float btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.30f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.32f, 0.42f, 1.0f));
    if (ImGui::Button("Duplicate", ImVec2(btn_w, 0))) {
        duplicate_selected_body();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.12f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(btn_w, 0))) {
        b.marked_for_removal = true;
        if (camera.focus_body == selected_body) camera.release_focus();
        selected_body = -1;
        inspector_visible_ = false;
    }
    ImGui::PopStyleColor(2);

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}
