#include "cosmos/cosmos_app_internal.h"
#include "cosmos/ui/cosmos_ui_data.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// ── UI Orchestrator ──────────────────────────────────────────────────────────
// Individual panels are in their own files:
//   cosmos_overlay.cpp      — render_overlay, draw_menu_background, draw_splash_screen, draw_pause_menu
//   cosmos_spawn_menu.cpp   — draw_spawn_menu
//   cosmos_inspector.cpp    — draw_inspector
//   cosmos_bottom_bar.cpp   — draw_bottom_bar
//   cosmos_dialogs.cpp      — draw_file_dialog, draw_debug_window, draw_shortcuts_overlay, duplicate_selected_body

void CosmosApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();
    bool any_overlay = show_splash || show_pause_menu;

    if (any_overlay)
        draw_menu_background();

    if (show_splash) {
        draw_splash_screen();
        return;
    }

    if (show_pause_menu) {
        draw_pause_menu();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.10f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.16f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.16f, 0.26f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.20f, 0.34f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.60f, 0.20f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.75f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.18f, 0.32f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.24f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.34f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.24f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.20f, 0.35f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.25f, 0.40f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.88f, 0.94f, 1.0f));

    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        inspector_visible_ = true;
    }

    // Keep relationship tracking fresh while paused or during direct menu edits.
    update_body_tracking_cache();

    draw_inspector();
    draw_spawn_menu();
    draw_file_dialog();
    draw_debug_window();
    draw_shortcuts_overlay();

    if (save_status_timer_ > 0.0f) {
        save_status_timer_ -= io.DeltaTime;
        float alpha = std::min(save_status_timer_, 1.0f);
        ImVec2 text_size = ImGui::CalcTextSize(last_save_status_.c_str());
        float tx = io.DisplaySize.x * 0.5f - text_size.x * 0.5f - 12;
        float ty = io.DisplaySize.y * 0.5f - 20;
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(tx - 8, ty - 4), ImVec2(tx + text_size.x + 20, ty + text_size.y + 8),
            IM_COL32(20, 20, 30, (int)(200 * alpha)), 6.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(tx, ty),
                                                IM_COL32(255, 220, 80, (int)(255 * alpha)),
                                                last_save_status_.c_str());
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !show_save_dialog_) {
        show_save_dialog_ = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L) && !show_load_dialog_) {
        show_load_dialog_ = true;
    }

    if (body_list_visible_) {
        ImGui::SetNextWindowPos({io.DisplaySize.x - 620.0f, 320}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({610, 430}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Bodies", &body_list_visible_)) {
            static std::vector<uint8_t> expanded_rows;
            if (expanded_rows.size() != state.count())
                expanded_rows.assign(state.count(), 0u);

            ImGui::Text("Objects: %zu", state.count());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160);
            ImGui::InputTextWithHint("##body_search", "Search...", body_search_buf_, sizeof(body_search_buf_));
            ImGui::Separator();

            // Build lowercase search term once
            std::string search_lower;
            if (body_search_buf_[0]) {
                search_lower = body_search_buf_;
                for (auto& c : search_lower) c = (char)std::tolower((unsigned char)c);
            }

            ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("##BodiesTable", 5, table_flags, ImVec2(0, -4))) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.9f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Mass", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Temp", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 105.0f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < state.count(); i++) {
                    const auto& b = state.bodies[i];
                    const char* tn = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "?";
                    std::string display_name = b.name.empty() ? std::string(tn) : b.name;

                    // Filter by search term
                    if (!search_lower.empty()) {
                        std::string name_lower = display_name;
                        for (auto& c : name_lower) c = (char)std::tolower((unsigned char)c);
                        std::string type_lower = tn;
                        for (auto& c : type_lower) c = (char)std::tolower((unsigned char)c);
                        if (name_lower.find(search_lower) == std::string::npos &&
                            type_lower.find(search_lower) == std::string::npos)
                            continue;
                    }
                    bool is_sel = ((int)i == selected_body);
                    char age_buf[64];
                    format_sim_time((double)b.age, age_buf, sizeof(age_buf));

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    char row_label[256];
                    snprintf(row_label, sizeof(row_label), "%s %s##body_row_%zu",
                             expanded_rows[i] ? "v" : ">",
                             display_name.c_str(), i);
                    if (ImGui::Selectable(row_label, is_sel, ImGuiSelectableFlags_SpanAllColumns)) {
                        selected_body = (int)i;
                        inspector_visible_ = true;
                        expanded_rows[i] = expanded_rows[i] ? 0u : 1u;
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(tn);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3e", b.mass);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.0f K", b.temperature);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%s", age_buf);

                    if (expanded_rows[i]) {
                        constexpr float KMH_TO_MPH = 0.6213712f;
                        float speed_kmh = glm::length(b.vel) * SIM_UNIT_TO_KM * 3600.0f;
                        float speed_mph = speed_kmh * KMH_TO_MPH;

                        int pidx = (i < tracked_primary_.size()) ? tracked_primary_[i] : -1;
                        std::string parent_name = "None";
                        if (pidx >= 0 && pidx < (int)state.bodies.size()) {
                            const auto& p = state.bodies[(size_t)pidx];
                            const char* p_type = (p.type < CTYPE_COUNT) ? CTYPE_NAMES[p.type] : "?";
                            parent_name = p.name.empty() ? std::string(p_type) : p.name;
                        }

                        std::string children = "None";
                        int child_count = 0;
                        for (size_t j = 0; j < state.bodies.size(); ++j) {
                            const auto& c = state.bodies[j];
                            if (j >= tracked_primary_.size() || tracked_primary_[j] != (int)i) continue;
                            const char* c_type = (c.type < CTYPE_COUNT) ? CTYPE_NAMES[c.type] : "?";
                            const char* c_name = c.name.empty() ? c_type : c.name.c_str();
                            if (child_count == 0) children = c_name;
                            else if (child_count < 5) {
                                children += ", ";
                                children += c_name;
                            }
                            child_count++;
                        }
                        if (child_count > 5) {
                            children += ", +";
                            children += std::to_string(child_count - 5);
                            children += " more";
                        }

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextWrapped("Radius: %.2f km | Velocity: %.1f km/h (%.1f mph)", b.radius, speed_kmh, speed_mph);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextWrapped("Orbits: %s", parent_name.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextWrapped("Orbited by: %s", children.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text(" ");
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text(" ");
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    draw_bottom_bar();

    ImGui::PopStyleColor(18);
    ImGui::PopStyleVar(6);
}
