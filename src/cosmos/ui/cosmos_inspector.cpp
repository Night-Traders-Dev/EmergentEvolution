#include "cosmos/cosmos_app_internal.h"
#include "cosmos/ui/cosmos_ui_data.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 320.0f, 46.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(400, 800));

    if (!ImGui::Begin("Inspector", &inspector_visible_)) {
        ImGui::End();
        return;
    }

    const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Unknown";
    const char* display_name = b.name.empty() ? type_name : b.name.c_str();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::TextWrapped("%s", display_name);
    ImGui::PopStyleColor();

    if (!b.name.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(%s)", type_name);
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    // Lock/pin toggle
    if (b.locked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.1f, 0.9f));
        if (ImGui::SmallButton("Unlock")) b.locked = false;
        ImGui::PopStyleColor();
    } else {
        if (ImGui::SmallButton("Lock")) {
            b.locked = true;
            b.vel = glm::vec3(0.0f);
        }
    }
    ImGui::SameLine();
    bool is_tracked = camera.focus_active && camera.focus_body == selected_body;
    if (is_tracked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.9f));
        if (ImGui::SmallButton("Untrack")) camera.release_focus();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::SmallButton("Track")) {
            camera.focus_on(b.pos, selected_body, b.radius);
            camera.target_distance = std::max(b.radius * 8.0f, 30.0f);
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Properties");

    ImGui::Columns(2, "##props", false);
    ImGui::SetColumnWidth(0, 110);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass");
    ImGui::NextColumn();
    constexpr double SOLAR_MASS_KG = 1.98847e30;
    constexpr double KG_TO_LBS = 2.20462262185;
    double mass_kg = (double)b.mass * SOLAR_MASS_KG;
    double mass_lbs = mass_kg * KG_TO_LBS;
    ImGui::Text("%.3e kg / %.3e lbs", mass_kg, mass_lbs);

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Radius");
    ImGui::NextColumn();
    constexpr float KM_TO_MILES = 0.6213712f;
    float radius_km = b.radius;
    float radius_miles = radius_km * KM_TO_MILES;
    ImGui::Text("%.1f km / %.1f mi", radius_km, radius_miles);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Temperature");
    ImGui::NextColumn();
    float temp_c = b.temperature - 273.15f;
    float temp_f = temp_c * 9.0f / 5.0f + 32.0f;
    ImGui::Text("%.0f K (%.1f C / %.1f F)", b.temperature, temp_c, temp_f);

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Material Phase");
    ImGui::NextColumn();
    const char* phase_name = (b.material_phase <= PHASE_COLLAPSING)
        ? MATERIAL_PHASE_NAMES[b.material_phase] : "?";
    if (b.collapse_progress > 0.01f && b.material_phase == PHASE_COLLAPSING)
        ImGui::Text("%s %.0f%%", phase_name, b.collapse_progress * 100.0f);
    else
        ImGui::Text("%s", phase_name);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Speed");
    ImGui::NextColumn();
    constexpr float KMH_TO_MPH = 0.6213712f;
    float speed_kmh = glm::length(b.vel) * SIM_UNIT_TO_KM * 3600.0f;
    float speed_mph = speed_kmh * KMH_TO_MPH;
    ImGui::Text("%.1f km/h / %.1f mph", speed_kmh, speed_mph);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Position");
    ImGui::NextColumn();
    ImGui::Text("%.0f, %.0f, %.0f", b.pos.x, b.pos.y, b.pos.z);
    ImGui::NextColumn();

    if (std::abs(b.angular_vel) > 1e-6f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Spin");
        ImGui::NextColumn();
        ImGui::Text("%.3f rad/s", b.angular_vel);
        ImGui::NextColumn();
    }

    if (b.tidal_lock_progress > 0.01f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Tidal Lock");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", b.tidal_lock_progress * 100.0f);
        ImGui::NextColumn();
    }

    if (b.orbital_period > 1.0e-3f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Orbital Period");
        ImGui::NextColumn();
        char period_buf[64];
        format_sim_time((double)b.orbital_period, period_buf, sizeof(period_buf));
        ImGui::Text("%s", period_buf);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Eccentricity");
        ImGui::NextColumn();
        ImGui::Text("%.4f", b.orbital_eccentricity);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Semi-Major Axis");
        ImGui::NextColumn();
        ImGui::Text("%.1f", b.orbital_semi_major);
        ImGui::NextColumn();
    }

    if (std::abs(b.axial_tilt) > 0.01f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Axial Tilt");
        ImGui::NextColumn();
        ImGui::Text("%.1f deg", b.axial_tilt * 57.2957795f);
        ImGui::NextColumn();
    }

    if (is_star_type(b.type) && b.habitable_zone_outer > 0.01f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Habitable Zone");
        ImGui::NextColumn();
        ImGui::Text("%.1f - %.1f", b.habitable_zone_inner, b.habitable_zone_outer);
        ImGui::NextColumn();
    }

    if (is_black_hole_type(b.type) && b.hawking_temperature > 0.0f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Hawking Temp");
        ImGui::NextColumn();
        ImGui::Text("%.3e K", b.hawking_temperature);
        ImGui::NextColumn();
    }

    char age_buf[64];
    format_sim_time((double)b.age, age_buf, sizeof(age_buf));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Age");
    ImGui::NextColumn();
    ImGui::Text("%s", age_buf);
    ImGui::NextColumn();

    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Cumulative Properties");
    ImGui::Columns(2, "##cumulative", false);
    ImGui::SetColumnWidth(0, 140);

    float density = body_density(b);
    float volume = body_volume(b);
    float calc_radius = is_star_type(b.type) ? expected_star_radius(b) :
                        ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) ? expected_planet_radius(std::min(b.mass, 0.02f)) : b.radius);
    float surface_g = body_surface_gravity(b, cfg.G);
    float escape_v = body_escape_velocity(b, cfg.G);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Density");
    ImGui::NextColumn();
    ImGui::Text("%.4g M/u^3", density);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Volume");
    ImGui::NextColumn();
    ImGui::Text("%.4g u^3", volume);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Calculated Radius");
    ImGui::NextColumn();
    ImGui::Text("%.2f", calc_radius);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Surface Gravity");
    ImGui::NextColumn();
    ImGui::Text("%.4f", surface_g);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Escape Velocity");
    ImGui::NextColumn();
    ImGui::Text("%.4f", escape_v);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass Loss Rate");
    ImGui::NextColumn();
    ImGui::Text("%.4e M/s", b.mass_loss_rate);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass Loss Total");
    ImGui::NextColumn();
    ImGui::Text("%.4e M", b.mass_loss_total);
    ImGui::NextColumn();

    ImGui::Columns(1);
    if (ImGui::Button("Reset Mass Loss Total", ImVec2(-1, 0)))
        b.mass_loss_total = 0.0f;

    if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Comparisons");
        ImGui::Columns(2, "##comparisons", false);
        ImGui::SetColumnWidth(0, 140);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Earth Similarity");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", comparisons.earth_similarity * 100.0f);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Life Likelihood");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", comparisons.life_likelihood * 100.0f);
        ImGui::NextColumn();

        ImGui::Columns(1);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Composition");
    ImGui::Columns(2, "##materials", false);
    ImGui::SetColumnWidth(0, 140);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Iron");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.iron * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Silicate");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.silicate * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Water");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.water * 100.0f);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Hydrogen");
    ImGui::NextColumn();
    ImGui::Text("%.0f%%", materials.hydrogen * 100.0f);
    ImGui::NextColumn();

    ImGui::Columns(1);

    if (magnetic.show_magnetosphere || magnetic.show_magnetic_axis || magnetic.particle_jets ||
        is_star_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Magnetic Fields");
        ImGui::Columns(2, "##magnetic", false);
        ImGui::SetColumnWidth(0, 150);

        if (magnetic.show_magnetosphere) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Show Magnetosphere");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.show_magnetosphere ? "Yes" : "No");
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetosphere Size");
            ImGui::NextColumn();
            ImGui::Text("%.2f", magnetic.magnetosphere_size);
            ImGui::NextColumn();
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetic Field");
        ImGui::NextColumn();
        ImGui::Text("%.3f", magnetic.magnetic_field);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Show Magnetic Axis");
        ImGui::NextColumn();
        ImGui::Text("%s", magnetic.show_magnetic_axis ? "Yes" : "No");
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Magnetic Pole Angle");
        ImGui::NextColumn();
        ImGui::Text("%.1f deg", magnetic.magnetic_pole_angle);
        ImGui::NextColumn();

        if (magnetic.particle_jets || magnetic.make_pulsar) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Particle Jets");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.particle_jets ? "Yes" : "No");
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Make Pulsar");
            ImGui::NextColumn();
            ImGui::Text("%s", magnetic.make_pulsar ? "Yes" : "No");
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    if (b.parent >= 0 && b.parent < (int)state.bodies.size()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Orbit");

        const auto& par = state.bodies[b.parent];
        const char* par_name = par.name.empty()
            ? CTYPE_NAMES[std::min(par.type, (uint32_t)CTYPE_COUNT - 1)]
            : par.name.c_str();

        ImGui::Columns(2, "##orbit", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Parent");
        ImGui::NextColumn();
        if (ImGui::SmallButton(par_name)) {
            selected_body = b.parent;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to select parent");
        ImGui::NextColumn();

        float orb_dist = glm::length(b.pos - par.pos);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Distance");
        ImGui::NextColumn();
        ImGui::Text("%.1f", orb_dist);
        ImGui::NextColumn();

        if (orb_dist > 0.1f) {
            float orb_v = std::sqrt(cfg.G * par.mass / orb_dist);
            float period = 2.0f * 3.14159f * orb_dist / std::max(orb_v, 0.01f);
            char period_buf[64];
            format_sim_time((double)period, period_buf, sizeof(period_buf));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Period");
            ImGui::NextColumn();
            ImGui::Text("%s", period_buf);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    if (is_star_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "Stellar");

        static const char* STAGE_NAMES[] = {
            "Main Sequence", "Subgiant", "Red Giant", "Horizontal Branch",
            "AGB", "Supergiant", "Hypergiant", "White Dwarf", "Neutron Star"
        };

        ImGui::Columns(2, "##star", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Stage");
        ImGui::NextColumn();
        const char* stage = (b.stellar_stage < SSTAGE_COUNT) ? STAGE_NAMES[b.stellar_stage] : "?";
        ImGui::Text("%s", stage);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Fuel");
        ImGui::NextColumn();
        ImGui::ProgressBar(b.fuel, ImVec2(-1, 14));
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Luminosity");
        ImGui::NextColumn();
        ImGui::Text("%.2f L", b.luminosity);
        ImGui::NextColumn();

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Corona");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.corona_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Flares");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.flare_activity);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Granulation");
            ImGui::NextColumn();
            ImGui::Text("%.2f @ %.1f", vp.terrain_amp, vp.terrain_freq);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    if (is_black_hole_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Black Hole");

        float rs = 2.0f * cfg.G * b.mass / (cfg.speed_of_light * cfg.speed_of_light);

        ImGui::Columns(2, "##bh", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Schwarzschild r");
        ImGui::NextColumn();
        ImGui::Text("%.4f", rs);
        ImGui::NextColumn();

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Lensing");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.lensing_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Accretion");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.accretion_strength);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Jet Strength");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.jet_strength);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    if ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) && b.props_valid) {
        const auto& pp = b.cached_props;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Surface & Atmosphere");

        static const char* SURF_NAMES[] = {"Rocky", "Liquid", "Frozen", "Gas Giant", "Mixed"};
        static const char* OCEAN_NAMES[] = {"None", "Water", "Methane", "Ammonia", "Lava"};
        static const char* WEATHER_NAMES[] = {"None", "Storms", "Rain", "Snow", "Dust"};

        ImGui::Columns(2, "##planet", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Surface");
        ImGui::NextColumn();
        ImGui::Text("%s", SURF_NAMES[pp.surface]);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Planet Class");
        ImGui::NextColumn();
        ImGui::Text("%s", PLANET_CLASS_NAMES[pp.planet_class]);
        ImGui::NextColumn();

        if (pp.atmosphere.pressure > 0.01f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Atm Pressure");
            ImGui::NextColumn();
            ImGui::Text("%.2f atm", pp.atmosphere.pressure);
            ImGui::NextColumn();

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

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Composition");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", dom_gas, max_frac * 100.0f);
            float second_max = 0;
            const char* second_gas = "";
            for (auto& g : gases) {
                if (g.frac > second_max && g.name != dom_gas) {
                    second_max = g.frac; second_gas = g.name;
                }
            }
            if (second_max > 0.05f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), "%s %.0f%%",
                                   second_gas, second_max * 100.0f);
            }
            ImGui::NextColumn();

            if (pp.atmosphere.has_clouds) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Clouds");
                ImGui::NextColumn();
                ImGui::Text("%.0f%%", pp.cloud_coverage);
                ImGui::NextColumn();
            }
        }

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Atmosphere Health");
        ImGui::NextColumn();
        ImGui::Text("%.0f%%", std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 100.0f);
        ImGui::NextColumn();

        if (b.ring_density > 0.01f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Rings");
            ImGui::NextColumn();
            ImGui::Text("%.2f - %.2f / %.0f%%", b.ring_inner_radius, b.ring_outer_radius, b.ring_density * 100.0f);
            ImGui::NextColumn();
        }

        if (pp.ocean_type != OCEAN_NONE) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", OCEAN_NAMES[pp.ocean_type], pp.ocean_coverage);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean Depth");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.ocean_depth);
            ImGui::NextColumn();
        }

        if (pp.has_mountains) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mountains");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.mountain_height);
            ImGui::NextColumn();
        }
        if (pp.has_continents) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Continents");
            ImGui::NextColumn();
            ImGui::Text("%d / %.0f%%", pp.continent_count, pp.continent_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_islands) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Islands");
            ImGui::NextColumn();
            ImGui::Text("%.0f%%", pp.island_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_rivers) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Rivers");
            ImGui::NextColumn();
            ImGui::Text("%.0f%% density", pp.river_density * 100.0f);
            ImGui::NextColumn();
        }
        if (pp.has_ice_sheets) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ice Sheets");
            ImGui::NextColumn();
            ImGui::Text("%.0f%%", pp.ice_sheet_coverage);
            ImGui::NextColumn();
        }
        if (pp.has_iron_core) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Iron Core");
            ImGui::NextColumn();
            ImGui::Text("Yes");
            ImGui::NextColumn();
        }

        if (pp.has_weather) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Weather");
            ImGui::NextColumn();
            ImGui::Text("%s", WEATHER_NAMES[pp.weather_type]);
            ImGui::NextColumn();
        }

        if (pp.vegetation_coverage > 1.0f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Vegetation");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%.0f%%",
                               pp.vegetation_coverage);
            ImGui::NextColumn();
        }

        if (b.visuals_valid) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Roughness");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.roughness);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Haze");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.haze_density);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Cratering");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.crater_density);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Weather FX");
            ImGui::NextColumn();
            ImGui::Text("%.2f", vp.weather_strength);
            ImGui::NextColumn();

            if (vp.volcanic_activity > 0.01f) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Volcanism");
                ImGui::NextColumn();
                ImGui::Text("%.2f", vp.volcanic_activity);
                ImGui::NextColumn();
            }
        }

        ImGui::Columns(1);
    }

    if ((b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST) && b.visuals_valid) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.80f, 0.72f, 1.0f), "Small Body Visuals");

        ImGui::Columns(2, "##smallbody", false);
        ImGui::SetColumnWidth(0, 110);

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

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Class");
        ImGui::NextColumn();
        ImGui::Text("%s", b.type == CTYPE_COMET ? "Cometary Ice" : small_body_class);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ice / Metal");
        ImGui::NextColumn();
        ImGui::Text("%.0f%% / %.0f%%", vp.ice_frac * 100.0f, vp.metal_frac * 100.0f);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Cratering");
        ImGui::NextColumn();
        ImGui::Text("%.2f", vp.crater_density);
        ImGui::NextColumn();

        if (b.type == CTYPE_COMET) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Coma / Tail");
            ImGui::NextColumn();
            ImGui::Text("%.2f / %.2f", vp.coma_strength, vp.tail_strength);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Duplicate + Delete side by side
    float btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Duplicate", ImVec2(btn_w, 0))) {
        duplicate_selected_body();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("Delete", ImVec2(btn_w, 0))) {
        b.marked_for_removal = true;
        if (camera.focus_body == selected_body) camera.release_focus();
        selected_body = -1;
        inspector_visible_ = false;
    }
    ImGui::PopStyleColor();

    ImGui::End();
}
