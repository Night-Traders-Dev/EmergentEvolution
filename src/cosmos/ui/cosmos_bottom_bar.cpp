#include "cosmos/cosmos_app_internal.h"
#include "cosmos/ui/cosmos_ui_data.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

void CosmosApp::draw_bottom_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 36.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;
    float dt = io.DeltaTime;
    auto draw_time_presets = [&]() {
        struct Preset { const char* label; double exp; };
        static const Preset presets[] = {
            {"1 s/s", 0.0}, {"1 min/s", 1.778},
            {"1 hr/s", 3.556}, {"1 day/s", 4.937},
            {"1 yr/s", 7.499}, {"1 Myr/s", 13.499},
            {"1 Gyr/s", 16.499},
        };
        for (int i = 0; i < 7; i++) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::SmallButton(presets[i].label))
                cfg.time_exponent = presets[i].exp;
            show_bottom_bar_tooltip(presets[i].label);
        }
    };

    bool mouse_near_bottom = (io.MousePos.y > display_h - 8.0f);
    float current_bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);
    bool mouse_over_bar = (io.MousePos.y > current_bar_y && bottom_bar_offset_ < 0.5f);
    bool keep_visible = show_menu_popup_ || show_pause_menu || !bottom_bar_autohide_;
    float target = bottom_bar_autohide_
        ? ((mouse_near_bottom || mouse_over_bar || keep_visible) ? 0.0f : 1.0f)
        : 0.0f;
    bottom_bar_offset_ += (target - bottom_bar_offset_) * std::min(1.0f, 8.0f * dt);
    if (bottom_bar_offset_ < 0.005f) bottom_bar_offset_ = 0.0f;
    if (bottom_bar_offset_ > 0.995f) bottom_bar_offset_ = 1.0f;

    float bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);

    ImGui::SetNextWindowPos(ImVec2(0, bar_y));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.04f, 0.02f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    if (ImGui::Begin("##CosmosBottomBar", nullptr, bar_flags)) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.15f, 0.05f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.22f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.30f, 0.10f, 1.0f));
        if (ImGui::Button("Menu", ImVec2(70, 24))) {
            show_menu_popup_ = !show_menu_popup_;
        }
        show_bottom_bar_tooltip("Menu");
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.4f, 0.35f, 0.2f, 0.5f), "|");
        ImGui::SameLine(0, 8);

        struct TBEntry { const char* label; bool* visible; };
        TBEntry entries[] = {
            {"Spawn",     &spawn_menu_visible_},
            {"Bodies",    &body_list_visible_},
            {"Inspector", &inspector_visible_},
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        for (int i = 0; i < 3; i++) {
            bool vis = *entries[i].visible;
            if (vis) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.14f, 0.05f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.20f, 0.08f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.16f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.06f, 0.02f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.10f, 0.04f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.08f, 0.03f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.5f, 0.3f, 0.8f));
            }

            char btn_id[64];
            snprintf(btn_id, sizeof(btn_id), "%s###CTB_%d", entries[i].label, i);
            if (ImGui::Button(btn_id, ImVec2(0, 22))) {
                *entries[i].visible = !(*entries[i].visible);
            }
            show_bottom_bar_tooltip(entries[i].label);

            if (vis) {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(rmin.x + 2, rmax.y - 2), ImVec2(rmax.x - 2, rmax.y),
                    IM_COL32(255, 200, 60, 200));
            }

            ImGui::PopStyleColor(4);
            ImGui::SameLine(0, 4);
        }
        ImGui::PopStyleVar(2);

        char time_buf[64], rate_buf[64], nominal_rate_buf[64];
        double displayed_rate = paused ? 0.0 : displayed_time_rate_;
        double nominal_rate = std::pow(10.0, cfg.time_exponent) * (reverse_time_ ? -1.0 : 1.0);
        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        format_sim_time(std::abs(displayed_rate), rate_buf, sizeof(rate_buf));
        format_sim_time(std::abs(nominal_rate), nominal_rate_buf, sizeof(nominal_rate_buf));

        char time_label[96];
        snprintf(time_label, sizeof(time_label), "T: %s###BottomTimeBtn", time_buf);
        char time_visible[64];
        snprintf(time_visible, sizeof(time_visible), "T: %s", time_buf);
        char rate_text[96];
        snprintf(rate_text, sizeof(rate_text), "%s%s/s", displayed_rate < -1.0e-12 ? "-" : "", rate_buf);
        char bodies_text[64];
        snprintf(bodies_text, sizeof(bodies_text), "%zu bodies", state.count());
        int attracting_fragments = 0;
        int non_attracting_fragments = 0;
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            bool is_fragment = ((int)b.frag_generation > 0) &&
                               !is_star_type(b.type) &&
                               !is_black_hole_type(b.type);
            if (!is_fragment) continue;
            if (b.non_attracting) ++non_attracting_fragments;
            else ++attracting_fragments;
        }
        char budget_text[160];
        snprintf(budget_text, sizeof(budget_text), "Object Budget  FPS %.1f  A %d  N %d",
                 smoothed_fps_, attracting_fragments, non_attracting_fragments);

        float time_btn_w = ImGui::CalcTextSize(time_visible).x + 14.0f;
        float rate_w = ImGui::CalcTextSize(rate_text).x;
        float bodies_w = ImGui::CalcTextSize(bodies_text).x;
        float budget_w = ImGui::CalcTextSize(budget_text).x;
        float sep_w = ImGui::CalcTextSize("|").x;
        float total_w = time_btn_w + 8.0f + sep_w + 8.0f + rate_w +
                        8.0f + sep_w + 8.0f + bodies_w +
                        8.0f + sep_w + 8.0f + budget_w;

        ImGui::SameLine(std::max(8.0f, display_w - total_w - 16.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.11f, 0.05f, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.18f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.22f, 0.10f, 1.0f));
        if (ImGui::Button(time_label, ImVec2(time_btn_w, 22.0f)))
            ImGui::OpenPopup("##BottomTimeStepPopup");
        show_hover_tooltip("Open compact time controls, time presets, and the live rate readout.");
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.45f, 0.40f, 0.26f, 0.85f), "|");
        ImGui::SameLine(0, 8);
        ImVec4 live_rate_color = adaptive_substeps_saturated_
            ? ImVec4(1.0f, 0.35f, 0.25f, 0.95f)
            : ImVec4(0.8f, 0.7f, 0.3f, 0.9f);
        ImGui::TextColored(live_rate_color, "%s", rate_text);
        show_hover_tooltip("Actual realized simulation rate after pause state and adaptive timestep clamping.");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.45f, 0.40f, 0.26f, 0.85f), "|");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "%s", bodies_text);
        show_hover_tooltip("Current active body count in the simulation.");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.45f, 0.40f, 0.26f, 0.85f), "|");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.74f, 0.78f, 0.82f, 0.95f), "%s", budget_text);
        show_hover_tooltip("Live object-budget monitor showing FPS plus attracting and non-attracting fragment counts.");

        if (ImGui::BeginPopup("##BottomTimeStepPopup")) {
            ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.56f, 1.0f), "Time Control");
            float exp_f = (float)cfg.time_exponent;
            if (ImGui::SliderFloat("Rate##Bottom", &exp_f, -9.0f, 21.0f, ""))
                cfg.time_exponent = (double)exp_f;
            show_bottom_bar_tooltip("Rate");
            ImGui::Checkbox("Adaptive Time-Step##Bottom", &cfg.adaptive_time_step);
            show_bottom_bar_tooltip("Adaptive Time-Step");
            ImGui::Checkbox("Adaptive Substepping##Bottom", &cfg.adaptive_substepping);
            show_bottom_bar_tooltip("Adaptive Substepping");
            char popup_rate[64], popup_nominal_rate[64], popup_time[64];
            format_sim_time(std::abs(displayed_rate), popup_rate, sizeof(popup_rate));
            format_sim_time(std::abs(nominal_rate), popup_nominal_rate, sizeof(popup_nominal_rate));
            format_sim_time(cfg.sim_time_accumulated, popup_time, sizeof(popup_time));
            ImVec4 popup_rate_color = adaptive_substeps_saturated_
                ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            ImGui::TextColored(popup_rate_color, "Rate: %s%s/s", displayed_rate < -1.0e-12 ? "-" : "", popup_rate);
            show_hover_tooltip("Actual realized simulation rate after pause state and adaptive timestep clamping.");
            ImGui::Text("Nominal Rate: %s%s/s", nominal_rate < -1.0e-12 ? "-" : "", popup_nominal_rate);
            show_bottom_bar_tooltip("Nominal Rate");
            ImGui::Text("Sim Time: %s", popup_time);
            show_bottom_bar_tooltip("Sim Time");
            if (cfg.adaptive_substepping) {
                ImGui::Text("Substeps Used: %d", adaptive_substeps_last_);
                show_bottom_bar_tooltip("Substeps Used");
                ImGui::Text("Required Substeps: %d", adaptive_substeps_required_);
                show_bottom_bar_tooltip("Required Substeps");
            }
            draw_time_presets();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (show_menu_popup_) {
        float popup_w = 440.0f;
        float popup_h = std::min(760.0f, display_h - bar_h - 18.0f);
        float popup_x = 12.0f;
        float popup_y = std::max(10.0f, bar_y - popup_h - 4.0f);
        ImGui::SetNextWindowPos(ImVec2(popup_x, popup_y));
        ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h));

        ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.06f, 0.02f, 0.95f));

        if (ImGui::Begin("##CosmosMenuPopup", &show_menu_popup_, popup_flags)) {
            auto for_each_body = [&](const auto& fn) {
                for (auto& b : state.bodies) {
                    if (b.marked_for_removal) continue;
                    fn(b);
                }
            };

            auto system_com = [&]() -> glm::vec3 {
                double total_m = 0.0;
                glm::dvec3 weighted_pos(0.0);
                for (const auto& b : state.bodies) {
                    if (b.marked_for_removal) continue;
                    double m = std::max((double)b.mass, 0.0);
                    weighted_pos += glm::dvec3(b.pos) * m;
                    total_m += m;
                }
                if (total_m <= 1.0e-12) return glm::vec3(0.0f);
                return glm::vec3(weighted_pos / total_m);
            };

            auto set_auto_orbit = [&](float eccentricity_scale_radial, float eccentricity_scale_tangent) {
                for (int i = 0; i < (int)state.bodies.size(); ++i) {
                    auto& b = state.bodies[(size_t)i];
                    if (b.marked_for_removal || b.non_attracting) continue;
                    if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;
                    int pidx = dominant_primary_for(i);
                    if (pidx < 0) continue;
                    const auto& p = state.bodies[(size_t)pidx];
                    b.vel = verlet_auto_orbit_velocity(b, p,
                                                       eccentricity_scale_radial,
                                                       eccentricity_scale_tangent);
                    b.parent = pidx;
                }
                update_body_tracking_cache();
            };

            auto scale_system = [&](float factor) {
                glm::vec3 center = system_com();
                float v_scale = std::sqrt(1.0f / std::max(factor, 1.0e-6f));
                for_each_body([&](CelestialBody& b) {
                    b.pos = center + (b.pos - center) * factor;
                    b.vel *= v_scale;
                });
            };

            auto adjust_eccentricity = [&](float radial_scale, float tangential_scale) {
                for (int i = 0; i < (int)state.bodies.size(); ++i) {
                    auto& b = state.bodies[(size_t)i];
                    if (b.marked_for_removal || b.non_attracting) continue;
                    int pidx = dominant_primary_for(i);
                    if (pidx < 0) continue;
                    const auto& p = state.bodies[(size_t)pidx];
                    glm::vec3 rel = b.pos - p.pos;
                    float r = glm::length(rel);
                    if (r < 1.0e-5f) continue;
                    glm::vec3 r_hat = rel / r;
                    glm::vec3 rel_v = b.vel - p.vel;
                    float v_rad = glm::dot(rel_v, r_hat);
                    glm::vec3 v_tan = rel_v - r_hat * v_rad;
                    b.vel = p.vel + r_hat * (v_rad * radial_scale) + v_tan * tangential_scale;
                }
                update_body_tracking_cache();
            };

            auto tree_node_tt = [&](const char* label) {
                bool open = ImGui::TreeNodeEx(label);
                show_bottom_bar_tooltip(label);
                return open;
            };
            auto collapsing_header_tt = [&](const char* label) {
                bool open = ImGui::CollapsingHeader(label);
                show_bottom_bar_tooltip(label);
                return open;
            };
            auto menu_item_tt = [&](const char* label) {
                bool activated = ImGui::MenuItem(label);
                show_bottom_bar_tooltip(label);
                return activated;
            };
            auto menu_item_selected_tt = [&](const char* label, bool selected) {
                bool activated = ImGui::MenuItem(label, nullptr, selected);
                show_bottom_bar_tooltip(label);
                return activated;
            };
            auto menu_item_toggle_tt = [&](const char* label, bool* selected) {
                bool activated = ImGui::MenuItem(label, nullptr, selected);
                show_bottom_bar_tooltip(label);
                return activated;
            };
            auto checkbox_tt = [&](const char* label, bool* value) {
                bool changed = ImGui::Checkbox(label, value);
                show_bottom_bar_tooltip(label);
                return changed;
            };
            auto slider_float_tt = [&](const char* label, float* value, float min_v, float max_v,
                                       const char* fmt = "%.3f", ImGuiSliderFlags flags = 0) {
                bool changed = ImGui::SliderFloat(label, value, min_v, max_v, fmt, flags);
                show_bottom_bar_tooltip(label);
                return changed;
            };
            auto slider_int_tt = [&](const char* label, int* value, int min_v, int max_v,
                                     const char* fmt = "%d", ImGuiSliderFlags flags = 0) {
                bool changed = ImGui::SliderInt(label, value, min_v, max_v, fmt, flags);
                show_bottom_bar_tooltip(label);
                return changed;
            };
            auto combo_tt = [&](const char* label, int* current_item, const char* const items[], int items_count) {
                bool changed = ImGui::Combo(label, current_item, items, items_count);
                show_bottom_bar_tooltip(label);
                return changed;
            };
            auto button_tt = [&](const char* label, ImVec2 size = ImVec2(0, 0)) {
                bool pressed = ImGui::Button(label, size);
                show_bottom_bar_tooltip(label);
                return pressed;
            };

            if (button_tt("Reset All Menu Parameters to Default##Menu", ImVec2(-1, 0))) {
                reset_bottom_bar_menu_defaults();
            }
            ImGui::Separator();

            if (tree_node_tt("Simulation")) {
                if (menu_item_tt(paused ? "Resume (Space)" : "Pause (Space)")) {
                    paused = !paused; show_menu_popup_ = false;
                }
                if (menu_item_tt("New Simulation")) {
                    reset_simulation(); show_menu_popup_ = false;
                }
                if (ImGui::BeginMenu("Presets")) {
                    for (int i = 0; i < COSMOS_PRESET_COUNT; i++) {
                        if (ImGui::MenuItem(COSMOS_PRESETS[i].name)) {
                            load_preset(i);
                            show_menu_popup_ = false;
                        }
                        if (ImGui::IsItemHovered() && COSMOS_PRESETS[i].description[0])
                            ImGui::SetTooltip("%s", COSMOS_PRESETS[i].description);
                    }
                    ImGui::EndMenu();
                }
                if (menu_item_tt("Empty Universe")) {
                    state.clear(); cfg.body_count = 0;
                    selected_body = -1; sim_time_ = 0.0f;
                    cfg.sim_time_accumulated = 0.0;
                    displayed_time_rate_ = 0.0;
                    show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (menu_item_tt("Save (Ctrl+S)")) {
                    show_save_dialog_ = true; show_menu_popup_ = false;
                }
                if (menu_item_tt("Load (Ctrl+L)")) {
                    show_load_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (menu_item_tt("Import Body...")) {
                    show_import_dialog_ = true; show_menu_popup_ = false;
                }
                if (selected_body >= 0 && menu_item_tt("Export Selected Body...")) {
                    show_export_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::TreePop();
            }
            if (tree_node_tt("Panels")) {
                bool tmp;
                tmp = spawn_menu_visible_;
                if (menu_item_selected_tt("Spawn Menu", tmp)) { spawn_menu_visible_ = !spawn_menu_visible_; }
                tmp = body_list_visible_;
                if (menu_item_selected_tt("Body List", tmp)) { body_list_visible_ = !body_list_visible_; }
                tmp = inspector_visible_;
                if (menu_item_selected_tt("Inspector", tmp)) { inspector_visible_ = !inspector_visible_; }
                tmp = debug_window_visible_;
                if (menu_item_selected_tt("Debug", tmp)) { debug_window_visible_ = !debug_window_visible_; }
                ImGui::Separator();
                menu_item_toggle_tt("Show Orbits", &cfg.show_orbits);
                menu_item_toggle_tt("Show Trails", &cfg.show_trails);
                menu_item_toggle_tt("Show Object Labels", &cfg.show_body_labels);
                menu_item_toggle_tt("Auto-hide Bottom Bar", &bottom_bar_autohide_);
                ImGui::TreePop();
            }
            if (tree_node_tt("System Management")) {
                if (menu_item_tt("Balance System Momentum")) {
                    glm::dvec3 momentum(0.0);
                    double total_m = 0.0;
                    for (const auto& b : state.bodies) {
                        if (b.marked_for_removal) continue;
                        double m = std::max((double)b.mass, 0.0);
                        momentum += glm::dvec3(b.vel) * m;
                        total_m += m;
                    }
                    if (total_m > 1.0e-12) {
                        glm::vec3 com_vel = glm::vec3(momentum / total_m);
                        for_each_body([&](CelestialBody& b) { b.vel -= com_vel; });
                    }
                }
                if (menu_item_tt("Auto Orbit")) {
                    set_auto_orbit(0.0f, 1.0f);
                }
                if (menu_item_tt("Expand System")) {
                    scale_system(1.05f);
                }
                if (menu_item_tt("Shrink System")) {
                    scale_system(0.95f);
                }
                if (menu_item_tt("Increase Eccentricity")) {
                    adjust_eccentricity(1.20f, 0.93f);
                }
                if (menu_item_tt("Decrease Eccentricity")) {
                    adjust_eccentricity(0.80f, 1.05f);
                }
                if (menu_item_tt("Make 2D - Zero All Height Values")) {
                    for_each_body([&](CelestialBody& b) {
                        b.pos.y = 0.0f;
                        b.vel.y = 0.0f;
                    });
                    camera.target.y = 0.0f;
                }
                ImGui::TreePop();
            }
            if (tree_node_tt("Motion")) {
                if (menu_item_selected_tt("Reverse Time", reverse_time_)) {
                    reverse_time_ = !reverse_time_;
                }
                if (menu_item_tt("Reverse All Velocities")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= -1.0f; });
                }
                if (menu_item_tt("Halt All Velocities")) {
                    for_each_body([&](CelestialBody& b) { b.vel = glm::vec3(0.0f); });
                }
                if (menu_item_tt("Halt All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel = 0.0f; });
                }
                if (menu_item_tt("+2% All Speeds")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= 1.02f; });
                }
                if (menu_item_tt("-2% All Speeds")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= 0.98f; });
                }
                if (menu_item_tt("+2% All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel *= 1.02f; });
                }
                if (menu_item_tt("-2% All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel *= 0.98f; });
                }
                float dv_10kms = 10.0f / SIM_UNIT_TO_KM;
                if (menu_item_tt("Add Velocity of 10 km/s on X")) {
                    for_each_body([&](CelestialBody& b) { b.vel.x += dv_10kms; });
                }
                if (menu_item_tt("Add Velocity of -10 km/s on X")) {
                    for_each_body([&](CelestialBody& b) { b.vel.x -= dv_10kms; });
                }
                if (menu_item_tt("Add Velocity of 10 km/s on Y")) {
                    for_each_body([&](CelestialBody& b) { b.vel.y += dv_10kms; });
                }
                if (menu_item_tt("Add Velocity of -10 km/s on Y")) {
                    for_each_body([&](CelestialBody& b) { b.vel.y -= dv_10kms; });
                }
                if (menu_item_tt("Add Velocity of 10 km/s on Z")) {
                    for_each_body([&](CelestialBody& b) { b.vel.z += dv_10kms; });
                }
                if (menu_item_tt("Add Velocity of -10 km/s on Z")) {
                    for_each_body([&](CelestialBody& b) { b.vel.z -= dv_10kms; });
                }
                ImGui::TreePop();
            }
            if (tree_node_tt("Performance Management")) {
                if (menu_item_tt("Delete All Particles/Dust")) {
                    for (auto& b : state.bodies) {
                        if (b.marked_for_removal) continue;
                        bool dust_like = b.non_attracting ||
                            (b.type == CTYPE_DUST) ||
                            ((b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_NEBULA) &&
                             (int)b.frag_generation > 0);
                        if (dust_like) b.marked_for_removal = true;
                    }
                    cleanup_bodies();
                }
                if (menu_item_tt("Delete All Fragments")) {
                    for (auto& b : state.bodies) {
                        if (b.marked_for_removal) continue;
                        if ((int)b.frag_generation > 0) b.marked_for_removal = true;
                    }
                    cleanup_bodies();
                }
                if (menu_item_tt("Delete All Escaping Bodies")) {
                    int primary = -1;
                    float primary_mass = 0.0f;
                    for (int i = 0; i < (int)state.bodies.size(); ++i) {
                        const auto& b = state.bodies[(size_t)i];
                        if (b.marked_for_removal || b.non_attracting) continue;
                        if (b.mass > primary_mass) {
                            primary_mass = b.mass;
                            primary = i;
                        }
                    }
                    if (primary >= 0) {
                        const auto primary_pos = state.bodies[(size_t)primary].pos;
                        const auto primary_vel = state.bodies[(size_t)primary].vel;
                        const float primary_r = state.bodies[(size_t)primary].radius;
                        const float primary_m = std::max(state.bodies[(size_t)primary].mass, 1.0e-8f);
                        for (int i = 0; i < (int)state.bodies.size(); ++i) {
                            if (i == primary) continue;
                            auto& b = state.bodies[(size_t)i];
                            if (b.marked_for_removal) continue;
                            glm::vec3 rel = b.pos - primary_pos;
                            float dist = glm::length(rel);
                            if (dist < std::max(primary_r * 8.0f, 1.0f)) continue;
                            glm::vec3 rel_v = b.vel - primary_vel;
                            if (dist < 1.0e-5f) continue;
                            float radial_speed = glm::dot(rel_v, rel / dist);
                            if (radial_speed <= 0.0f) continue;
                            float escape_v = std::sqrt(std::max(2.0f * cfg.G * primary_m / std::max(dist, 1.0e-6f), 0.0f));
                            if (glm::length(rel_v) > escape_v * 1.05f)
                                b.marked_for_removal = true;
                        }
                        cleanup_bodies();
                    }
                }
                ImGui::TreePop();
            }
            if (tree_node_tt("Cosmos Settings")) {
                if (collapsing_header_tt("General Physics")) {
                    slider_float_tt("G##Menu", &cfg.G, 0.1f, 10.0f, "%.2f");
                    slider_float_tt("Softening##Menu", &cfg.softening, 1.0f, 50.0f, "%.1f");
                    slider_float_tt("Damping##Menu", &cfg.damping, 0.80f, 1.00f, "%.3f");
                    checkbox_tt("Collisions##Menu", &cfg.collisions);
                    checkbox_tt("Tidal Forces##Menu", &cfg.tidal_forces);
                    slider_float_tt("Tidal Heating Scale##Menu", &cfg.tidal_heating_scale, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Tidal Locking##Menu", &cfg.tidal_locking);
                    slider_float_tt("Tidal Lock Rate##Menu", &cfg.tidal_locking_rate, 0.0f, 0.1f, "%.3f");
                    checkbox_tt("Hawking Radiation##Menu", &cfg.hawking_radiation);
                    slider_float_tt("Hawking Scale##Menu", &cfg.hawking_radiation_scale, 0.0f, 10.0f, "%.2f");
                    checkbox_tt("Orbital Resonances##Menu", &cfg.orbital_resonance_detection);
                    checkbox_tt("Habitable Zones##Menu", &cfg.show_habitable_zones);
                    checkbox_tt("Spatial Hash Collisions##Menu", &cfg.spatial_hash_collisions);
                    static const char* INTEGRATOR_NAMES[] = {
                        "Velocity Verlet",
                        "Euler Explicit",
                        "Euler Semi-Implicit",
                        "RK2",
                        "Forest-Ruth",
                        "PEFRL"
                    };
                    combo_tt("Integrator##Menu", &cfg.integrator_type,
                             INTEGRATOR_NAMES, IM_ARRAYSIZE(INTEGRATOR_NAMES));
                    cfg.integrator_type = std::clamp(cfg.integrator_type, 0, (int)INTEGRATOR_PEFRL);
                    cfg.velocity_verlet = (cfg.integrator_type == INTEGRATOR_VELOCITY_VERLET);
                    slider_int_tt("Physics Substeps##Menu", &cfg.physics_substeps, 1, 16);
                    checkbox_tt("Barnes-Hut Gravity##Menu", &cfg.barnes_hut);
                    checkbox_tt("GPU BH Compute##Menu", &cfg.gpu_barnes_hut);
                    slider_float_tt("BH Theta##Menu", &cfg.barnes_hut_theta, 0.2f, 1.6f, "%.2f");
                    slider_int_tt("BH Min Bodies##Menu", &cfg.barnes_hut_min_bodies, 16, 2000);
                    slider_int_tt("Parallel Min Batch##Menu", &cfg.parallel_min_batch, 32, 4096);
                    checkbox_tt("Show Orbits##Menu", &cfg.show_orbits);
                    slider_float_tt("Orbit Opacity##Menu", &cfg.orbit_line_alpha, 0.0f, 1.0f, "%.2f");
                    slider_float_tt("Orbit Width##Menu", &cfg.orbit_line_width, 0.5f, 4.0f, "%.2f");
                    checkbox_tt("Show Trails##Menu", &cfg.show_trails);
                    {
                        int trail_len = (int)cfg.trail_length;
                        if (slider_int_tt("Trail Length##Menu", &trail_len, 0, 500))
                            cfg.trail_length = (uint32_t)trail_len;
                    }
                    slider_float_tt("Trail Opacity##Menu", &cfg.trail_alpha_scale, 0.0f, 4.0f, "%.2f");
                    slider_float_tt("Trail Width##Menu", &cfg.trail_width_scale, 0.1f, 4.0f, "%.2f");
                    checkbox_tt("Show Object Labels##Menu", &cfg.show_body_labels);
                    float label_min_log = std::log10(std::max(cfg.body_label_min_distance, 1.0e-3f));
                    if (slider_float_tt("Label Min Dist##Menu", &label_min_log, -3.0f, 8.0f, "10^%.2f")) {
                        cfg.body_label_min_distance = (label_min_log <= -2.95f)
                            ? 0.0f
                            : std::pow(10.0f, label_min_log);
                    }
                    float label_max_log = std::log10(std::max(cfg.body_label_max_distance, 1.0e-3f));
                    if (slider_float_tt("Label Max Dist##Menu", &label_max_log, -2.0f, 8.0f, "10^%.2f"))
                        cfg.body_label_max_distance = std::pow(10.0f, label_max_log);
                    slider_float_tt("Label Opacity##Menu", &cfg.body_label_opacity, 0.05f, 1.0f, "%.2f");
                    slider_int_tt("Label Max Count##Menu", &cfg.body_label_max_count, 1, 256);
                    cfg.body_label_min_distance = std::clamp(cfg.body_label_min_distance, 0.0f, 1.0e8f);
                    cfg.body_label_max_distance = std::clamp(cfg.body_label_max_distance,
                                                             std::max(cfg.body_label_min_distance, 1.0e-3f),
                                                             1.0e8f);
                    cfg.trail_length = (uint32_t)std::clamp((int)cfg.trail_length, 0, 500);
                }

                if (collapsing_header_tt("Camera")) {
                    slider_float_tt("FOV##Menu", &camera.fov, 20.0f, 90.0f, "%.1f");
                    float log_dist = std::log10(std::max(camera.distance, 0.01f));
                    if (slider_float_tt("Distance##Menu", &log_dist, 1.0f, 3.7f, "10^%.1f"))
                        camera.distance = std::pow(10.0f, log_dist);
                    if (button_tt("Reset Camera##Menu")) camera = OrbitCamera{};
                }

                if (collapsing_header_tt("Collision & Fragmentation")) {
                    checkbox_tt("Smoothed Particle Hydrodynamics##Menu", &cfg.collision_sph);
                    checkbox_tt("Rigid Body Dynamics##Menu", &cfg.collision_rigid_body_dynamics);
                    slider_float_tt("SPH Pressure##Menu", &cfg.collision_sph_pressure, 0.0f, 4.0f, "%.2f");
                    slider_float_tt("SPH Viscosity##Menu", &cfg.collision_sph_viscosity, 0.0f, 4.0f, "%.2f");
                    slider_float_tt("SPH Heat##Menu", &cfg.collision_sph_heat, 0.0f, 4.0f, "%.2f");
                    slider_float_tt("Rigid Restitution##Menu", &cfg.rigid_collision_restitution, 0.0f, 1.2f, "%.2f");
                    slider_float_tt("Rigid Separation##Menu", &cfg.rigid_collision_separation, 0.0f, 3.0f, "%.2f");
                    if (!cfg.collision_sph && !cfg.collision_rigid_body_dynamics) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                           "Enable SPH and/or Rigid Body Dynamics.");
                    }
                    checkbox_tt("Merging##Menu", &cfg.collision_merging);
                    checkbox_tt("Fragmentation##Menu", &cfg.collision_fragmentation);
                    checkbox_tt("Spin Fragmentation##Menu", &cfg.spin_fragmentation);
                    slider_float_tt("Merge Speed##Menu", &cfg.merge_speed_threshold, 1.0f, 20.0f, "%.2f");
                    slider_float_tt("Fragment Speed##Menu", &cfg.fragment_speed_threshold, 10.0f, 50.0f, "%.2f");
                    slider_float_tt("Collision Heating##Menu", &cfg.collision_heating, 0.0f, 2.0f, "%.2f");
                    slider_float_tt("Spin Frag Threshold##Menu", &cfg.spin_fragmentation_threshold, 0.70f, 1.50f, "%.2f x");
                    slider_int_tt("Fragment Count##Menu", &cfg.fragment_count, 1, 12);
                    slider_float_tt("Min Frag Mass##Menu", &cfg.min_fragment_mass, 1.0e-9f, 1.0f, "%.6f",
                                    ImGuiSliderFlags_Logarithmic);
                    slider_int_tt("Max Frag Depth##Menu", &cfg.max_frag_generation, 0, 5);
                }

                if (collapsing_header_tt("Thermal & Roche")) {
                    checkbox_tt("Temperature##Menu", &cfg.temperature_system);
                    slider_float_tt("Cooling##Menu", &cfg.radiative_cooling, 0.0f, 0.01f, "%.4f");
                    checkbox_tt("Evaporation##Menu", &cfg.evaporation);
                    slider_float_tt("Evaporation Rate##Menu", &cfg.evaporation_rate, 0.0f, 0.10f, "%.3f");
                    checkbox_tt("Roche Limit##Menu", &cfg.roche_limit);
                    checkbox_tt("Fluid Roche Limit##Menu", &cfg.roche_limit_fluid);
                    slider_float_tt("Fluid Roche Scale##Menu", &cfg.roche_fluid_scale, 0.25f, 4.0f, "%.2f");
                    checkbox_tt("Rigid Roche Limit##Menu", &cfg.roche_limit_rigid);
                    slider_float_tt("Rigid Roche Scale##Menu", &cfg.roche_rigid_scale, 0.25f, 4.0f, "%.2f");
                    if (!cfg.roche_limit_fluid && !cfg.roche_limit_rigid) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                           "Enable at least one Roche mode.");
                    }
                    checkbox_tt("Material Phases##Menu", &cfg.material_phases);
                    slider_float_tt("Material Phase Rate##Menu", &cfg.material_phase_rate, 0.1f, 4.0f, "%.2f");
                    checkbox_tt("Planetary Rings##Menu", &cfg.planetary_rings);
                    slider_float_tt("Ring Inner Scale##Menu", &cfg.ring_inner_scale, 0.6f, 3.0f, "%.2f");
                    slider_float_tt("Ring Outer Scale##Menu", &cfg.ring_outer_scale, 0.6f, 3.0f, "%.2f");
                    slider_float_tt("Ring Density Scale##Menu", &cfg.ring_density_scale, 0.2f, 3.0f, "%.2f");
                    slider_float_tt("Ring Thickness##Menu", &cfg.ring_thickness_scale, 0.3f, 4.0f, "%.2f");
                    slider_float_tt("Ring Particle Scale##Menu", &cfg.ring_particle_scale, 0.2f, 5.0f, "%.2f");
                    slider_float_tt("Ring Mass Scale##Menu", &cfg.ring_mass_scale, 0.1f, 5.0f, "%.2f");
                }

                if (collapsing_header_tt("Stellar")) {
                    checkbox_tt("Stellar Evolution##Menu", &cfg.stellar_evolution);
                    slider_float_tt("Star Timescale##Menu", &cfg.stellar_timescale, 10.0f, 500.0f, "%.1f");
                    checkbox_tt("Stellar Wind Pressure##Menu", &cfg.stellar_wind_pressure);
                    slider_float_tt("Wind Pressure Scale##Menu", &cfg.stellar_wind_pressure_scale, 0.0f, 4.0f, "%.2f");
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Nebula Gravity Coupling");
                    slider_float_tt("Nebula Grav Advect##Menu", &cfg.nebula_gravity_advection_scale,
                                    0.0f, 0.25f, "%.3f");
                    slider_float_tt("Nebula Grav Collapse##Menu", &cfg.nebula_gravity_collapse_scale,
                                    0.0f, 0.30f, "%.3f");
                    slider_float_tt("Nebula Grav Compress##Menu", &cfg.nebula_gravity_compress_scale,
                                    0.0f, 1.50f, "%.3f");
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Nebula Sink Formation");
                    checkbox_tt("Enable Sink Formation##Menu", &cfg.nebula_sink_formation);
                    slider_float_tt("Sink Threshold##Menu", &cfg.nebula_sink_threshold,
                                    0.05f, 8.0f, "%.2f");
                    slider_float_tt("Sink Min Mass##Menu", &cfg.nebula_sink_min_mass,
                                    1.0e-7f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);
                    slider_float_tt("Sink Spawn Fraction##Menu", &cfg.nebula_sink_spawn_fraction,
                                    0.001f, 0.50f, "%.3f");
                    slider_float_tt("Sink Consume Fraction##Menu", &cfg.nebula_sink_consume_fraction,
                                    0.05f, 1.0f, "%.2f");
                }

                if (collapsing_header_tt("Rendering & Lighting")) {
                    checkbox_tt("Star Lighting##Menu", &cfg.star_lighting);
                    slider_float_tt("Star Light Strength##Menu", &cfg.star_light_strength, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Uniform Lighting##Menu", &cfg.uniform_lighting);
                    slider_float_tt("Uniform Light Strength##Menu", &cfg.uniform_light_strength, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Fast Star Lighting##Menu", &cfg.fast_star_lighting);
                    slider_float_tt("Ambient##Menu", &cfg.ambient_strength, 0.0f, 0.5f, "%.2f");

                    ImGui::Separator();
                    checkbox_tt("HQ Shading##Menu", &cfg.cosmos_hq_shading);
                    checkbox_tt("Background Starfield##Menu", &cfg.cosmos_background_starfield);
                    static const char* BG_PRESETS[] = {
                        "Realistic",
                        "Deep Black",
                        "Nebula",
                        "Warm Dust",
                        "Blue Haze",
                        "Aurora Veil",
                        "Crimson Rift",
                        "Galactic Core",
                        "Monochrome",
                        "Emerald Sea",
                        "Infrared Dust",
                        "Deep Field",
                        "Milky Way Panorama",
                        "Orion Nebula",
                        "Carina Nebula",
                        "Cosmic Microwave Background",
                        "Void",
                        "Eagle Nebula",
                        "Supernova Remnant",
                        "Stellar Nursery"
                    };
                    combo_tt("Background Preset##Menu", &cfg.cosmos_background_preset,
                             BG_PRESETS, IM_ARRAYSIZE(BG_PRESETS));
                    slider_float_tt("Background Strength##Menu", &cfg.background_starfield_intensity, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Star Corona##Menu", &cfg.cosmos_star_corona);
                    slider_float_tt("Corona Strength##Menu", &cfg.corona_strength_scale, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Comet Tails##Menu", &cfg.cosmos_comet_tails);
                    slider_float_tt("Comet Tail Strength##Menu", &cfg.comet_tail_strength_scale, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Black Hole Lensing##Menu", &cfg.cosmos_blackhole_lensing);
                    slider_float_tt("Lensing Strength##Menu", &cfg.blackhole_lensing_strength, 0.0f, 4.0f, "%.2f");
                    checkbox_tt("Space Fabric Grid##Menu", &cfg.cosmos_space_fabric);
                    slider_float_tt("Fabric Square Size##Menu", &cfg.cosmos_space_fabric_grid_size,
                                    5.0f, 200.0f, "%.1f u", ImGuiSliderFlags_Logarithmic);
                    slider_float_tt("Fabric Curvature##Menu", &cfg.cosmos_space_fabric_strength,
                                    0.1f, 3.0f, "%.2f");
                    if (button_tt("Snap Fabric View Isometric##Menu")) {
                        camera.azimuth = glm::radians(45.0f);
                        camera.elevation = glm::radians(35.2643897f);
                        camera.target_distance = camera.distance;
                    }
                    slider_int_tt("Cosmos Quality##Menu", &cfg.cosmos_quality, 0, 3,
                                  cfg.cosmos_quality == 0 ? "Low" :
                                  (cfg.cosmos_quality == 1 ? "Balanced" :
                                   (cfg.cosmos_quality == 2 ? "High" : "Ultra")));
                }

                if (collapsing_header_tt("Time Control")) {
                    float exp_f = (float)cfg.time_exponent;
                    if (slider_float_tt("Time Rate##Menu", &exp_f, -9.0f, 21.0f, ""))
                        cfg.time_exponent = (double)exp_f;
                    checkbox_tt("Adaptive Time-Stepping##Menu", &cfg.adaptive_time_step);
                    slider_float_tt("Adaptive Safety##Menu", &cfg.adaptive_step_safety, 0.01f, 1.0f, "%.2f");
                    slider_float_tt("Adaptive Min dt##Menu", &cfg.adaptive_step_min,
                                    1.0e-6f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);
                    slider_float_tt("Adaptive Max dt##Menu", &cfg.adaptive_step_max,
                                    0.01f, 1.0e7f, "%.2f", ImGuiSliderFlags_Logarithmic);
                    checkbox_tt("Adaptive Substepping##Menu", &cfg.adaptive_substepping);
                    float substep_tol_log = std::log10(std::max(cfg.adaptive_substep_tolerance, 1.0e-6f));
                    if (slider_float_tt("Adaptive Tolerance##Menu", &substep_tol_log, -6.0f, 3.0f, "10^%.2f"))
                        cfg.adaptive_substep_tolerance = std::pow(10.0f, substep_tol_log);
                    slider_int_tt("Adaptive Max Substeps##Menu", &cfg.adaptive_substep_max, 1, 256);
                    char rate_buf[64], nominal_rate_buf[64], time_buf[64];
                    double displayed_rate = paused ? 0.0 : displayed_time_rate_;
                    double nominal_rate = std::pow(10.0, cfg.time_exponent) * (reverse_time_ ? -1.0 : 1.0);
                    format_sim_time(std::abs(displayed_rate), rate_buf, sizeof(rate_buf));
                    format_sim_time(std::abs(nominal_rate), nominal_rate_buf, sizeof(nominal_rate_buf));
                    format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
                    ImVec4 rate_color = adaptive_substeps_saturated_
                        ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    ImGui::TextColored(rate_color, "Rate: %s%s/s", displayed_rate < -1.0e-12 ? "-" : "", rate_buf);
                    ImGui::Text("Nominal Rate: %s%s/s", nominal_rate < -1.0e-12 ? "-" : "", nominal_rate_buf);
                    ImGui::Text("Sim Time: %s", time_buf);
                    if (cfg.adaptive_substepping) {
                        ImGui::Text("Substeps Used: %d", adaptive_substeps_last_);
                        show_bottom_bar_tooltip("Substeps Used");
                        ImGui::Text("Required Substeps: %d", adaptive_substeps_required_);
                        show_bottom_bar_tooltip("Required Substeps");
                        if (adaptive_substeps_saturated_) {
                            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                                               "Accuracy limited by substep cap; sim is slowing down.");
                        }
                    }
                    draw_time_presets();
                }

                if (collapsing_header_tt("General Relativity")) {
                    // Physics mode selector
                    ImGui::Text("Physics Mode:");
                    ImGui::SameLine();
                    bool is_newtonian = (cfg.physics_mode == PHYSICS_MODE_NEWTONIAN);
                    bool is_gr = (cfg.physics_mode == PHYSICS_MODE_GR);
                    if (is_newtonian) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
                    if (ImGui::SmallButton("Newtonian##Mode")) {
                        apply_physics_mode(cfg, PHYSICS_MODE_NEWTONIAN);
                    }
                    if (is_newtonian) ImGui::PopStyleColor();
                    show_hover_tooltip("Classical Newtonian gravity. No relativistic corrections, no J2 oblateness, no Hawking radiation. Faster and simpler.");
                    ImGui::SameLine();
                    if (is_gr) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
                    if (ImGui::SmallButton("General Relativity##Mode")) {
                        apply_physics_mode(cfg, PHYSICS_MODE_GR);
                    }
                    if (is_gr) ImGui::PopStyleColor();
                    show_hover_tooltip("1PN post-Newtonian corrections: perihelion precession, gravitational time dilation, Lense-Thirring frame dragging, J2 oblateness, Hawking radiation.");
                    ImGui::Separator();

                    checkbox_tt("GR Corrections##Menu", &cfg.gr_enabled);
                    checkbox_tt("J2 Oblateness##Menu", &cfg.j2_perturbation);
                    checkbox_tt("Parallel Gravity##Menu", &cfg.parallel_gravity);
                    slider_float_tt("Precession##Menu", &cfg.gr_precession_scale, 0.0f, 10.0f, "%.2f");
                    slider_float_tt("Time Dilation##Menu", &cfg.gr_time_dilation, 0.0f, 5.0f, "%.2f");
                    slider_float_tt("Frame Drag##Menu", &cfg.gr_frame_dragging, 0.0f, 5.0f, "%.2f");
                    slider_float_tt("Speed of Light##Menu", &cfg.speed_of_light, 50.0f, 1000.0f, "%.1f");
                }

                ImGui::TreePop();
            }
            if (tree_node_tt("Performance")) {
                if (tree_node_tt("Dynamic Budget")) {
                    checkbox_tt("Object Budget##Perf", &cfg.dynamic_budget_enabled);
                    ImGui::Text("Current FPS: %.1f", smoothed_fps_);
                    show_hover_tooltip("Smoothed render performance used by the dynamic budgeter.");
                    slider_float_tt("Target FPS##Perf", &cfg.dynamic_target_fps, 20.0f, 240.0f, "%.0f");
                    slider_int_tt("Max Fragments##Perf", &cfg.dynamic_max_fragments, 0, 3000);
                    slider_int_tt("Max Non-Attracting##Perf", &cfg.dynamic_max_non_attracting, 0, 10000);
                    float explosion_density_pct = cfg.dynamic_explosion_density * 100.0f;
                    if (slider_float_tt("Explosion Density##Perf", &explosion_density_pct, 1.0f, 100.0f, "%.0f%%"))
                        cfg.dynamic_explosion_density = std::clamp(explosion_density_pct / 100.0f, 0.01f, 1.0f);
                    float reduction_pct = cfg.dynamic_reduction_percent * 100.0f;
                    if (slider_float_tt("Reduction Percentage##Perf", &reduction_pct, 1.0f, 100.0f, "%.0f%%"))
                        cfg.dynamic_reduction_percent = std::clamp(reduction_pct / 100.0f, 0.01f, 1.0f);
                    int dust_mode = cfg.dust_debug_non_attracting ? 1 : 0;
                    const char* dust_modes[] = {"Standard", "Non-attracting"};
                    if (combo_tt("Dust Mode##Perf", &dust_mode, dust_modes, IM_ARRAYSIZE(dust_modes))) {
                        cfg.dust_debug_non_attracting = (dust_mode == 1);
                        apply_dust_debug_mode();
                    }
                    if (!cfg.dynamic_budget_enabled) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                           "Budget disabled: fragment growth is uncapped.");
                    }
                    ImGui::TreePop();
                }
                if (tree_node_tt("Diagnostics")) {
                    checkbox_tt("Enable Runtime Diagnostics##PerfDiag", &diagnostics_enabled_);
                    checkbox_tt("Pause on Invalid State##PerfDiag", &diagnostics_pause_on_invalid_);
                    ImGui::Text("Step Counter: %llu", (unsigned long long)diagnostics_step_counter_);
                    show_hover_tooltip("Monotonic counter of physics steps processed since launch.");
                    ImGui::TextWrapped("Logs: cosmos_debug.log and /tmp/cosmos_debug.log");
                    show_hover_tooltip("Primary runtime diagnostics log files.");
                    ImGui::TextWrapped("Crash dump: /tmp/cosmos_crash.log");
                    show_hover_tooltip("Crash-handler output path used after fatal signals.");
                    if (button_tt("Validate Now##PerfDiag", ImVec2(-1, 0))) {
                        validate_body_state("ui/manual_validate", true);
                    }
                    if (button_tt("Dump Snapshot##PerfDiag", ImVec2(-1, 0))) {
                        int attracting = 0;
                        int non_attracting = 0;
                        for (const auto& b : state.bodies) {
                            if (b.marked_for_removal) continue;
                            if (b.non_attracting) ++non_attracting;
                            else ++attracting;
                        }
                        debug_logf("snapshot bodies=%zu attracting=%d non_attracting=%d fps=%.2f sim_time=%.6g",
                                   state.bodies.size(), attracting, non_attracting, smoothed_fps_, sim_time_);
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            if (tree_node_tt("Navigation")) {
                if (menu_item_tt("Return to Launcher")) { request_launcher = true; request_quit = true; }
                if (menu_item_tt("Quit")) { request_quit = true; }
                ImGui::TreePop();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}
