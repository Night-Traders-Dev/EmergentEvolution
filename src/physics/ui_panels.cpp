#include "physics/interface.h"
#include "physics/phys_particles.h"
#include "physics/ui_data.h"
#include "physics/molecules.h"
#include "physics/audio.h"
#include "physics/repository.h"
#include "physics/paths.h"
#include <filesystem>
#include "particle_textures.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ── Main UI panels ───────────────────────────────────────────────────────────
// Split from interface.cpp: top bar, bottom bar, settings panel, spawn menu.

static bool spawn_button(int type_idx, const char* label, ImVec4 color,
                          int current_spawn_type, int current_spawn_group,
                          const char* tooltip, ImVec2 size = ImVec2(45, 32))
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.35f, color.y * 0.35f, color.z * 0.35f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
        ImVec4(color.x * 0.55f, color.y * 0.55f, color.z * 0.55f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
        ImVec4(color.x * 0.7f, color.y * 0.7f, color.z * 0.7f, 1.0f));

    bool selected = (current_spawn_type == type_idx && current_spawn_group == -1);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }

    bool clicked = ImGui::Button(label, size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);

    if (selected) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor(3);

    return clicked;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Main UI entry point ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════


// ── Top bar ──────────────────────────────────────────────────────────────────

void PhysicsInterface::draw_top_bar(SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 42.0f;
    float display_w = io.DisplaySize.x;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.039f, 0.051f, 0.090f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));

    if (ImGui::Begin("##TopBar", nullptr, flags)) {
        // ── Sim state ──
        if (sim_running)
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), ">> RUNNING");
        else
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "|| PAUSED");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Simulation state\nSpace = pause/resume");

        // ── Timestep ──
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        ImGui::SetNextItemWidth(80);
        ImGui::SliderFloat("##TimeScale", &cfg.time_scale, 0.0f, 16.0f, "%.2fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Simulation speed  [ = slower, ] = faster\nSpace = pause/resume");
        {
            static const float presets[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
            static const char* labels[]  = { ".25", ".5", "1x", "2x", "4x" };
            ImGui::SameLine(0, 6);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
            for (int i = 0; i < 5; i++) {
                if (i > 0) ImGui::SameLine(0, 2);
                bool active = (std::abs(cfg.time_scale - presets[i]) < 0.01f);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.25f, 0.5f, 0.9f));
                else        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.10f, 0.18f, 0.7f));
                char btn_id[16]; snprintf(btn_id, sizeof(btn_id), "%s###TS%d", labels[i], i);
                if (ImGui::SmallButton(btn_id)) cfg.time_scale = presets[i];
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set time scale to %.2fx", presets[i]);
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
        }

        // ── Temperature ──
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        if (cfg.thermo_feedback_enabled && emergent_temp_display > 0.0f) {
            char etemp_buf[64];
            format_temperature(emergent_temp_display, etemp_buf, sizeof(etemp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "T: %s", etemp_buf);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Emergent temperature (from particle kinetic energy)\nOrange = measured from simulation");
        } else {
            char temp_buf[64];
            format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "T: %s", temp_buf);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Configured temperature\nBlue = thermostat setpoint");
        }

        // ── B-field ──
        if (cfg.magnetic_feedback_enabled && emergent_bfield_display > 0.001f) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "B: %.3f T", emergent_bfield_display);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Emergent magnetic field strength (Tesla)\nFrom charged particle currents");
        }

        // ── FPS ──
        if (prefs.show_fps) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            ImGui::Text("%.0f fps", fps_display);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frames per second\nToggle in Settings > Display");
        }

        // ── Energy + Entropy ──
        ImGui::SameLine(0, 20);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
        ImGui::SameLine(0, 10);
        {
            char ebuf[32];
            float total_MeV = total_energy_display * E_SCALE_MEV;
            fmt_energy_ev(ebuf, sizeof(ebuf), total_MeV);
            ImGui::Text("E: %s", ebuf);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Total system energy (kinetic + potential)");
        }
        {
            ImGui::SameLine(0, 8);
            const char* s_arrow = (entropy_trend_display > 0) ? "S^" :
                                   (entropy_trend_display < 0) ? "Sv" : "S=";
            ImVec4 s_color = (entropy_trend_display >= 0)
                ? ImVec4(0.5f, 0.8f, 0.5f, 1.0f)
                : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(s_color, "%s", s_arrow);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Entropy trend\nS^ = increasing (2nd law)\nS= = equilibrium\nSv = decreasing (external work)");
        }

        // ── Events button ──
        if (nuclear_decay_count_display > 0 || !decay_log.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char decay_btn[64];
            snprintf(decay_btn, sizeof(decay_btn), "Events: %u###DecayBtn",
                     static_cast<unsigned>(decay_log.size()));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.2f, 0.1f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.15f, 0.05f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            if (ImGui::SmallButton(decay_btn))
                show_decay_log = !show_decay_log;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to open decay/interaction event log");
        }

        // ── Particle count button ──
        {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char part_btn[48];
            snprintf(part_btn, sizeof(part_btn), "Particles: %u###PartBtn", active_particle_display);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.2f, 0.4f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.15f, 0.3f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.75f, 1.0f, 1.0f));
            if (ImGui::SmallButton(part_btn))
                show_particle_list = !show_particle_list;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to show particle list");
        }

        // ── Element/Molecule count button ──
        if (!element_list.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.80f), "|");
            ImGui::SameLine(0, 10);
            char elem_btn[64];
            int nmol = 0;
            for (auto& m : molecule_list) if (m.atom_indices.size() > 1) nmol++;
            if (nmol > 0)
                snprintf(elem_btn, sizeof(elem_btn), "Atoms: %d  Mol: %d",
                         static_cast<int>(element_list.size()), nmol);
            else
                snprintf(elem_btn, sizeof(elem_btn), "Elements: %d", static_cast<int>(element_list.size()));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.5f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.2f, 0.4f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.6f, 1.0f));
            if (ImGui::SmallButton(elem_btn))
                show_element_list = !show_element_list;
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to show element list");
        }

        // ── Tool status indicators ──
        if (select_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.9f, 1.0f), "[SELECT]");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select mode (F4)\nClick particles to inspect");
        }
        if (thermo_probe_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[THERMO]");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thermometer probe\nClick to place temperature sensor");
        }
        if (velocity_meter_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "[VEL]");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Velocity meter\nClick a particle to track speed");
        }
        if (ruler_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "[RULER]");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Distance ruler\nClick two points to measure");
        }
        if (density_counter_placement_mode) {
            ImGui::SameLine(0, 12);
            ImGui::TextColored(ImVec4(0.6f, 0.3f, 1.0f, 1.0f), "[DENSITY]");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Density counter\nClick to place particle density sensor");
        }

    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Bottom Bar ──────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════


// ── Bottom bar ───────────────────────────────────────────────────────────────

void PhysicsInterface::draw_bottom_bar(SimConfig& cfg, bool& request_reset) {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 36.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;
    float dt = io.DeltaTime;

    // ── Auto-hide animation ─────────────────────────────────────────────────
    bool mouse_near_bottom = (io.MousePos.y > display_h - 8.0f);
    float current_bar_y = display_h - bar_h + bottom_bar_offset * (bar_h + 4.0f);
    bool mouse_over_bar = (io.MousePos.y > current_bar_y && bottom_bar_offset < 0.5f);
    bool keep_visible = show_tools_popup || show_pause_menu || show_settings_menu
                     || show_save_dialog || show_load_dialog;
    float target = (mouse_near_bottom || mouse_over_bar || keep_visible) ? 0.0f : 1.0f;
    bottom_bar_offset += (target - bottom_bar_offset) * std::min(1.0f, 8.0f * dt);
    if (bottom_bar_offset < 0.005f) bottom_bar_offset = 0.0f;
    if (bottom_bar_offset > 0.995f) bottom_bar_offset = 1.0f;

    float bar_y = display_h - bar_h + bottom_bar_offset * (bar_h + 4.0f);

    ImGui::SetNextWindowPos(ImVec2(0, bar_y));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.039f, 0.051f, 0.090f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    if (ImGui::Begin("##BottomBar", nullptr, bar_flags)) {
        // ── Left: Menu button ──
        float menu_btn_w = 70.0f;
        if (ImGui::Button("Menu", ImVec2(menu_btn_w, 24))) {
            show_tools_popup = !show_tools_popup;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tools & settings menu (Escape for pause)");

        // ── Separator ──
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.180f, 0.220f, 0.349f, 0.50f), "|");
        ImGui::SameLine(0, 8);

        // ── Taskbar: open windows as dock entries ──
        struct TBEntry { const char* label; TaskbarWindow tw; bool* close_flag; };
        TBEntry entries[TW_COUNT];
        int entry_count = 0;
        auto add_entry = [&](bool condition, const char* label, TaskbarWindow tw, bool* close_flag) {
            if (condition && entry_count < (int)TW_COUNT)
                entries[entry_count++] = { label, tw, close_flag };
        };
        add_entry(spawn_menu_visible, "Spawn", TW_SPAWN_MENU, &spawn_menu_visible);
        add_entry(settings_visible, "Settings", TW_SETTINGS, &settings_visible);
        add_entry(show_element_list, "Elements", TW_ELEMENT_LIST, &show_element_list);
        add_entry(show_particle_list, "Particles", TW_PARTICLE_LIST, &show_particle_list);
        add_entry(show_particle_bestiary, "Bestiary", TW_PARTICLE_BESTIARY, &show_particle_bestiary);
        add_entry(show_element_bestiary, "Elem Disc", TW_ELEMENT_BESTIARY, &show_element_bestiary);
        add_entry(show_molecule_bestiary, "Mol Disc", TW_MOLECULE_BESTIARY, &show_molecule_bestiary);
        add_entry(show_decay_log, "Events", TW_DECAY_LOG, &show_decay_log);
        add_entry(show_nuclear_debug, "Nuclear", TW_NUCLEAR_DEBUG, &show_nuclear_debug);
        add_entry(show_texture_panel, "Textures", TW_TEXTURE_PANEL, &show_texture_panel);
        add_entry(show_save_dialog, "Save", TW_SAVE_DIALOG, &show_save_dialog);
        add_entry(show_load_dialog, "Load", TW_LOAD_DIALOG, &show_load_dialog);
        add_entry(show_repository, "Repository", TW_REPOSITORY, &show_repository);
        add_entry(accel_mode, "Accelerator", TW_ACCELERATOR, nullptr);  // accel_mode needs special close

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        for (int i = 0; i < entry_count; i++) {
            auto& e = entries[i];
            bool visible = !is_minimized(e.tw);

            if (visible) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.16f, 0.28f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.38f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.18f, 0.32f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.95f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.08f, 0.14f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.14f, 0.24f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.12f, 0.20f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.55f, 0.65f, 0.8f));
            }

            char btn_id[64];
            snprintf(btn_id, sizeof(btn_id), "%s###TB_%d", e.label, static_cast<int>(e.tw));
            if (ImGui::Button(btn_id, ImVec2(0, 22))) {
                toggle_minimized(e.tw);
            }

            // Blue underline for visible (non-minimized) windows
            if (visible) {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(rmin.x + 2, rmax.y - 2), ImVec2(rmax.x - 2, rmax.y),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.302f, 0.749f, 0.953f, 0.8f)));
            }

            // Right-click context menu: close window
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (e.close_flag) {
                    *e.close_flag = false;
                } else if (e.tw == TW_ACCELERATOR) {
                    accel_mode = false;
                    accel_phase = 0;
                    accel_source_idx = -1;
                    accel_stream_timer = 0;
                    accel_free_origin_set = false;
                }
                set_minimized(e.tw, false);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                ; // consumed above
            else if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nClick: minimize/restore\nRight-click: close", e.label);

            ImGui::PopStyleColor(4);
            ImGui::SameLine(0, 4);
        }
        ImGui::PopStyleVar(2);

        // ── Menu popup (anchored above left-side Menu button) ──
        if (show_tools_popup) {
            float popup_w = 230.0f;
            float popup_h = std::min(540.0f, display_h - 40.0f);
            float popup_x = 12.0f;
            float popup_y = std::max(10.0f, bar_y - popup_h - 4.0f);
            ImGui::SetNextWindowPos(ImVec2(popup_x, popup_y));
            ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h));
            ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.059f, 0.071f, 0.130f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.14f, 0.22f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.20f, 0.32f, 1.0f));
            if (ImGui::Begin("##MenuPopup", &show_tools_popup, popup_flags)) {

                // Helper: draw a toggle menu item with a colored ON/OFF indicator
                auto toggle_item = [&](const char* label, bool& flag, const char* tooltip = nullptr) -> bool {
                    ImVec4 dot_col = flag
                        ? ImVec4(0.3f, 0.85f, 0.5f, 1.0f)   // green dot when ON
                        : ImVec4(0.35f, 0.35f, 0.4f, 0.6f);  // dim gray when OFF
                    if (flag) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.8f, 1.0f));
                    }
                    // Draw colored dot before the label
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float y_center = p.y + ImGui::GetTextLineHeight() * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        ImVec2(p.x + 5.0f, y_center), 3.5f, ImGui::ColorConvertFloat4ToU32(dot_col));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
                    bool clicked = ImGui::MenuItem(label, nullptr, flag);
                    if (flag) ImGui::PopStyleColor();
                    if (clicked) {
                        flag = !flag;
                        show_tools_popup = false;
                    }
                    if (tooltip && ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tooltip);
                    return clicked;
                };

                // ── Simulation ──
                if (ImGui::TreeNodeEx("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::MenuItem(sim_running ? "Pause (Space)" : "Resume (Space)")) {
                        sim_running = !sim_running;
                        show_tools_popup = false;
                    }
                    toggle_item("Spawn (F3)", spawn_menu_visible);
                    {
                        bool sel_tmp = select_mode;
                        if (toggle_item("Select (F4)", sel_tmp)) {
                            select_mode = sel_tmp;
                            if (select_mode) { pending_spawn = false; force_obj_placement_mode = false; }
                        }
                    }
                    toggle_item("Carrier Mode", cfg.carrier_mode_enabled,
                                "Visible force carrier exchange between particles\n"
                                "Photons between charges, gluons between quarks,\n"
                                "W/Z between weak-interacting particles");
                    toggle_item("Quasi Mode", cfg.quasi_mode_enabled,
                                "Quasiparticle spawning and physics effects\n"
                                "Plasmons, phonons, magnons, polarons,\n"
                                "Cooper pairs, rotons in appropriate environments");
                    ImGui::Separator();
                    toggle_item("Settings", show_settings_menu);
                    if (ImGui::MenuItem("Reset (F2)")) {
                        request_reset = true;
                        show_tools_popup = false;
                    }
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── File ──
                if (ImGui::TreeNodeEx("File")) {
                    if (ImGui::MenuItem("Save (Ctrl+S)")) {
                        show_save_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    if (ImGui::MenuItem("Load (Ctrl+L)")) {
                        show_load_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Undo (Ctrl+Z)")) {
                        request_undo = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Redo (Ctrl+Shift+Z)")) {
                        request_redo = true;
                        show_tools_popup = false;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import Element (.ppel)")) {
                        show_import_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    if (ImGui::MenuItem("Import Molecule (.ppmol)")) {
                        show_molecule_import_dialog = true;
                        show_tools_popup = false;
                        browse_needs_refresh = true;
                    }
                    ImGui::Separator();
                    if (ParticleRepository::is_available()) {
                        if (ImGui::MenuItem("Online Repository")) {
                            show_repository = true;
                            show_tools_popup = false;
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::MenuItem("Online Repository (needs libcurl)");
                        ImGui::EndDisabled();
                    }
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── View ─────────────────────────────────────────────────────
                if (ImGui::TreeNodeEx("View", ImGuiTreeNodeFlags_DefaultOpen)) {
                    toggle_item("Event Log", show_decay_log);
                    toggle_item("Particle List", show_particle_list);
                    toggle_item("Element List", show_element_list);
                    toggle_item("Particle Bestiary", show_particle_bestiary);
                    toggle_item("Element Bestiary", show_element_bestiary);
                    toggle_item("Molecule Bestiary", show_molecule_bestiary);
                    toggle_item("Achievements", show_achievements_panel);
                    toggle_item("Particle Textures", show_texture_panel);
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── Visualization ───────────────────────────────────────────
                if (ImGui::TreeNodeEx("Visualization")) {
                    toggle_item("Show Trails", cfg.show_trails);
                    toggle_item("Electron Cloud", show_electron_cloud);
                    toggle_item("Orbit Paths", show_orbit_paths,
                                "Show predicted Keplerian orbits\nfor bound electrons around nuclei");
                    toggle_item("Trajectory Tracer", show_trajectory_tracer);
                    toggle_item("Energy Heatmap", show_energy_heatmap);
                    toggle_item("Velocity Field", show_velocity_field);
                    toggle_item("Magnetic Field", show_magnetic_field);
                    toggle_item("Strong Field", show_strong_field,
                                "Show strong nuclear force field lines\nYukawa potential around quarks and nucleons\nColor = QCD charge (red/green/blue)");
                    toggle_item("Weak Field", show_weak_field,
                                "Show weak force field around W/Z bosons\nVery short range (exponential decay)\nViolet = W, Indigo = Z, Magenta = Higgs");
                    toggle_item("Gravity Field", show_gravity_field,
                                "Show gravitational field lines\nConverge on massive particles\nBlue intensity = mass");
                    toggle_item("Gravity Map", show_gravity_map);
                    ImGui::Separator();
                    toggle_item("Mass-Energy Gravity (E=mc\xc2\xb2)", gr_mass_energy);
                    toggle_item("Frame Dragging (Spin)", gr_frame_dragging);
                    toggle_item("Gravitational Waves", gr_grav_waves);
                    toggle_item("GW Ripples", show_grav_waves);
                    ImGui::Separator();
                    toggle_item("Orbital Drive", orbital_drive,
                                "Orbital tangential drive + velocity boost.\nON = electrons actively driven to orbital speed.\nOFF = natural Coulomb orbits with quantum barrier only.");
                    ImGui::Separator();
                    toggle_item("Force Vectors", show_force_vectors);
                    toggle_item("Wave Mode", wave_mode);
                    toggle_item("Atom Grid", show_atom_grid);
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── Measurement ──────────────────────────────────────────────
                if (ImGui::TreeNodeEx("Measurement")) {
                    auto enter_meas_mode = [&]() {
                        pending_spawn = false;
                        select_mode = false;
                        force_obj_placement_mode = false;
                        accel_mode = false;
                        mirror_placement_mode = false;
                        thermo_probe_placement_mode = false;
                        velocity_meter_mode = false;
                        ruler_placement_mode = false;
                        density_counter_placement_mode = false;
                    };

                    if (ImGui::MenuItem("Thermometer", nullptr, thermo_probe_placement_mode)) {
                        bool was = thermo_probe_placement_mode;
                        enter_meas_mode();
                        thermo_probe_placement_mode = !was;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Velocity Meter", nullptr, velocity_meter_mode)) {
                        bool was = velocity_meter_mode;
                        enter_meas_mode();
                        velocity_meter_mode = !was;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Distance Ruler", nullptr, ruler_placement_mode)) {
                        bool was = ruler_placement_mode;
                        enter_meas_mode();
                        ruler_placement_mode = !was;
                        ruler_placement_phase = 0;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Density Counter", nullptr, density_counter_placement_mode)) {
                        bool was = density_counter_placement_mode;
                        enter_meas_mode();
                        density_counter_placement_mode = !was;
                        show_tools_popup = false;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear All")) {
                        thermo_probes.clear();
                        velocity_meters.clear();
                        distance_rulers.clear();
                        density_counters.clear();
                        show_tools_popup = false;
                    }
                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // ── Tools ───────────────────────────────────────────────────
                if (ImGui::TreeNodeEx("Tools")) {
                    if (ImGui::MenuItem("Accelerator", nullptr, accel_mode)) {
                        accel_mode = !accel_mode;
                        accel_phase = 0;
                        accel_source_idx = -1;
                        accel_stream_timer = 0;
                        if (accel_mode) {
                            pending_spawn = false;
                            select_mode = false;
                            force_obj_placement_mode = false;
                            thermo_probe_placement_mode = false;
                            velocity_meter_mode = false;
                            ruler_placement_mode = false;
                            density_counter_placement_mode = false;
                        }
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Mirror", nullptr, mirror_placement_mode)) {
                        mirror_placement_mode = !mirror_placement_mode;
                        if (mirror_placement_mode) {
                            mirror_placement_phase = 0;
                            pending_spawn = false;
                            select_mode = false;
                            force_obj_placement_mode = false;
                            accel_mode = false;
                            thermo_probe_placement_mode = false;
                            velocity_meter_mode = false;
                            ruler_placement_mode = false;
                            density_counter_placement_mode = false;
                        }
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Nuclear Debug", nullptr, show_nuclear_debug)) {
                        show_nuclear_debug = !show_nuclear_debug;
                        show_tools_popup = false;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Halt Velocities")) {
                        request_halt_velocities = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Remove Massless")) {
                        request_remove_massless = true;
                        show_tools_popup = false;
                    }
                    if (ImGui::MenuItem("Remove Massive")) {
                        request_remove_massive = true;
                        show_tools_popup = false;
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Settings Panel (Left Sidebar) ───────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════


// ── Settings panel ───────────────────────────────────────────────────────────

void PhysicsInterface::draw_settings_panel(SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;
    ImVec2 def_size(300, std::min(700.0f, max_h));

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_SETTINGS]);
        ImGui::SetNextWindowSize(tile_size_[TW_SETTINGS]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(def_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(def_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(350, max_h));

    bool open = ImGui::Begin("Settings", &settings_visible);
    record_window_rect(TW_SETTINGS);
    if (!open) {
        wobble_window(1.0f);
        ImGui::End();
        return;
    }
    draw_minimize_button(TW_SETTINGS);

    // ── Environment ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        int env = static_cast<int>(cfg.environment_mode);
        if (ImGui::Combo("Preset", &env, PHYS_ENV_NAMES, PHYS_ENV_COUNT)) {
            cfg.environment_mode = static_cast<uint32_t>(env);
            switch (env) {
                case 0:  // Lab Mode
                    cfg.start_empty = true;
                    cfg.temperature_kelvin = 1.0f;
                    cfg.dampening = 0.990f;
                    cfg.repulsion_radius = 1.0f;
                    cfg.pressure_resistance = 100.0f;
                    cfg.interaction_radius = 200.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.lorentz_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 100.0f;
                    cfg.hadronization_enabled = true;
                    cfg.viscosity_strength = 0.0f;
                    cfg.time_scale = 1.0f;
                    break;
                case 1:  // Hydrogen Plasma
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 100.0f;
                    break;
                case 2:  // Neutron Star Surface
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e9f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 120.0f;
                    break;
                case 3:  // Solar Core
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1.5e7f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 110.0f;
                    break;
                case 4:  // Particle Soup
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5000.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 80.0f;
                    break;
                case 5:  // Alpha Emitter
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 300.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 60.0f;
                    break;
                case 6:  // Heavy Nucleus
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 100.0f;
                    cfg.dampening = 0.985f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 80.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 1.0f;
                    prefs.particle_count_slider = 50.0f;
                    break;
                case 7:  // Quark-Gluon Plasma
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2e12f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.string_tension = 10.0f;
                    cfg.hadronization_enabled = false;  // quarks deconfined in QGP
                    cfg.weak_coupling = 0.5f;
                    prefs.particle_count_slider = 100.0f;
                    break;
                case 8:  // Electroweak Era
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 1e15f;
                    cfg.dampening = 0.97f;
                    cfg.repulsion_radius = 3.0f;
                    cfg.pressure_resistance = 40.0f;
                    cfg.interaction_radius = 80.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 50.0f;
                    prefs.particle_count_slider = 80.0f;
                    break;
                case 9:  // Meson Factory
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 5e11f;
                    cfg.dampening = 0.98f;
                    cfg.repulsion_radius = 4.0f;
                    cfg.pressure_resistance = 50.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.string_tension = 60.0f;
                    cfg.weak_coupling = 0.2f;
                    prefs.particle_count_slider = 90.0f;
                    break;
                case 10:  // Particle Accelerator
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2.7f;
                    cfg.dampening = 0.995f;
                    cfg.repulsion_radius = 5.0f;
                    cfg.pressure_resistance = 60.0f;
                    cfg.interaction_radius = 120.0f;
                    cfg.gravity_strength = 1.0f;
                    cfg.lorentz_strength = 1.5f;
                    cfg.weak_coupling = 0.0f;
                    cfg.string_tension = 50.0f;
                    cfg.time_scale = 3.0f;
                    prefs.particle_count_slider = 60.0f;
                    break;
                case 13:  // Big Bang
                    cfg.start_empty = false;
                    cfg.temperature_kelvin = 2e15f;
                    cfg.dampening = 0.96f;
                    cfg.repulsion_radius = 2.0f;
                    cfg.pressure_resistance = 30.0f;
                    cfg.interaction_radius = 100.0f;
                    cfg.gravity_strength = 2.0f;
                    cfg.lorentz_strength = 1.0f;
                    cfg.weak_coupling = 1.0f;
                    cfg.string_tension = 10.0f;
                    cfg.hadronization_enabled = false;
                    cfg.viscosity_strength = 0.1f;
                    cfg.time_scale = 1.0f;
                    prefs.particle_count_slider = 150.0f;
                    break;
            }
            log_temperature = std::log10(std::max(1.0f, cfg.temperature_kelvin));
        }

        if (!cfg.start_empty) {
            ImGui::SliderFloat("Count", &prefs.particle_count_slider, 1.0f, 317.0f, "%.0f");
            int pc = static_cast<int>(std::max(2.0f, std::pow(prefs.particle_count_slider, 2.0f)));
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Particles: %d  (applied on Reset)", pc);
        } else {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Particles: %u  (Lab Mode)", cfg.particle_count);
        }

        ImGui::SliderInt("Seed", &seed_value, 0, 99999);
        cfg.generation_seed = static_cast<uint32_t>(seed_value);

        // Quick experiment presets (adjust parameters without resetting)
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Quick Presets:");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Apply parameter tweaks without resetting the simulation");
        float bw = (ImGui::GetContentRegionAvail().x - 8.0f) / 2.0f;
        if (ImGui::Button("Cold Lab", ImVec2(bw, 0))) {
            cfg.temperature_kelvin = 10.0f;
            log_temperature = 1.0f;
            cfg.dampening = 0.995f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("10 K, high damping — slow and stable");
        ImGui::SameLine();
        if (ImGui::Button("Hot Plasma", ImVec2(bw, 0))) {
            cfg.temperature_kelvin = 1e7f;
            log_temperature = 7.0f;
            cfg.dampening = 0.98f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("10 MK — hot enough for fusion");
        if (ImGui::Button("Nuclear Fuel", ImVec2(bw, 0))) {
            cfg.temperature_kelvin = 1e8f;
            log_temperature = 8.0f;
            cfg.dampening = 0.985f;
            cfg.pressure_resistance = 40.0f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("100 MK, low pressure — fusion/fission conditions");
        ImGui::SameLine();
        if (ImGui::Button("Antimatter", ImVec2(bw, 0))) {
            cfg.temperature_kelvin = 5000.0f;
            log_temperature = std::log10(5000.0f);
            cfg.virtual_pairs_enabled = true;
            cfg.virtual_pair_threshold = 1.5f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable virtual pairs, lower threshold — more antimatter");
        if (ImGui::Button("Dark Universe", ImVec2(bw, 0))) {
            cfg.temperature_kelvin = 2.7f;
            log_temperature = std::log10(2.7f);
            cfg.gravity_strength = 3.0f;
            cfg.dampening = 0.997f;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("2.7 K, strong gravity — dark matter clustering");
    }

    // ── Temperature ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Temperature", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("##TempSlider", &log_temperature, 0.0f, 13.0f, "");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Logarithmic temperature scale\n1 = 10 K, 3 = 1000 K, 7 = 10 MK");
        cfg.temperature_kelvin = std::pow(10.0f, log_temperature);

        char temp_buf[64];
        format_temperature(cfg.temperature_kelvin, temp_buf, sizeof(temp_buf), prefs.temp_unit);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", temp_buf);

        // Context label
        const char* temp_context = "Deep space (CMB)";
        if (cfg.temperature_kelvin > 100.0f) temp_context = "Room temperature";
        if (cfg.temperature_kelvin > 5000.0f) temp_context = "Surface of star";
        if (cfg.temperature_kelvin > 1e6f) temp_context = "Stellar core";
        if (cfg.temperature_kelvin > 1e9f) temp_context = "Neutron star";
        if (cfg.temperature_kelvin > 1e11f) temp_context = "Quark-gluon plasma";
        if (cfg.temperature_kelvin > 1e15f) temp_context = "Electroweak epoch";
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "  %s", temp_context);

        // Emergent temperature readout + feedback controls
        if (cfg.thermo_feedback_enabled) {
            char etemp_buf[64];
            format_temperature(emergent_temp_display, etemp_buf, sizeof(etemp_buf), prefs.temp_unit);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "  Measured: %s", etemp_buf);
        }
        ImGui::Checkbox("Thermodynamic Feedback", &cfg.thermo_feedback_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Temperature emerges from particle kinetic energies\n(statistical mechanics: T ~ <1/2 mv^2>)");
        if (cfg.thermo_feedback_enabled) {
            ImGui::SliderFloat("Coupling##thermo", &cfg.thermo_coupling, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = slider only\n1 = fully emergent\n0.5 = blended");
        }
    }

    // ── Thermodynamics ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Thermodynamics")) {
        // First Law: Energy conservation
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "1st Law: Energy Conservation");

        char ebuf_ke[32], ebuf_pe[32], ebuf_tot[32];
        fmt_energy_ev(ebuf_ke, sizeof(ebuf_ke), energy_kinetic_display * E_SCALE_MEV);
        fmt_energy_ev(ebuf_pe, sizeof(ebuf_pe), energy_potential_display * E_SCALE_MEV);
        float total_disp = (energy_kinetic_display + energy_potential_display) * E_SCALE_MEV;
        fmt_energy_ev(ebuf_tot, sizeof(ebuf_tot), total_disp);

        ImGui::Text("  KE: %s   PE: %s", ebuf_ke, ebuf_pe);
        ImGui::Text("  Total: %s", ebuf_tot);

        float ratio = energy_conservation_ratio_display;
        ImVec4 ratio_color;
        if (ratio > 0.95f && ratio < 1.05f)
            ratio_color = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
        else if (ratio > 0.80f && ratio < 1.20f)
            ratio_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        else
            ratio_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(ratio_color, "  Conservation: %.1f%%", ratio * 100.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("E_now / E_initial\n100%% = perfect conservation\n"
                "Sources: thermal noise, vacuum energy\n"
                "Sinks: damping, photon/neutrino drain, synchrotron");

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "  In: %.2f/f  Out: %.2f/f  Drift: %+.3f/f",
            energy_injected_rate_display, energy_dissipated_rate_display,
            energy_drift_rate_display);

        ImGui::Spacing();

        // Second Law: Entropy
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "2nd Law: Entropy");
        const char* trend_icon = (entropy_trend_display > 0) ? "^" :
                                  (entropy_trend_display < 0) ? "v" : "=";
        ImVec4 trend_color = (entropy_trend_display >= 0)
            ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(trend_color, "  S = %.1f  %s", system_entropy_display, trend_icon);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Boltzmann entropy from velocity distribution\n"
                "S = sum[ N * (1 + ln(A/N) + ln(T)) ]\n"
                "^ = increasing (normal)\n"
                "= = equilibrium\n"
                "v = decreasing (external work/cooling)");

        ImGui::Spacing();

        // Zeroth + Third Law notes
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "0th Law: Thermal conduction (30px range)");
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
            "3rd Law: Zero-point energy per particle type");
    }

    // ── Fundamental Forces ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fundamental Forces", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Electromagnetic
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "Electromagnetic");
        ImGui::SliderFloat("B Field", &cfg.lorentz_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Magnetic force (Biot-Savart + Lorentz)\n1.0 = F = q(v x B), B = q(v x r)/r²\n0 = off\n>1 = enhanced");
        if (cfg.magnetic_feedback_enabled)
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "  Measured: %.3f T", emergent_bfield_display);
        ImGui::Checkbox("Emergent B Field", &cfg.magnetic_feedback_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Magnetic field emerges from charged particle currents\n(moving charges generate B fields)");
        if (cfg.magnetic_feedback_enabled) {
            ImGui::SliderFloat("Coupling##mag", &cfg.magnetic_coupling, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = slider only\n1 = fully emergent\n0.5 = blended");
        }

        ImGui::Spacing();

        // Strong Nuclear
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Strong Nuclear");
        ImGui::SliderFloat("Confinement", &cfg.string_tension, 0.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quark confinement (string tension)\nHigher = quarks held more tightly\n0 = deconfinement (quark-gluon plasma)");
        ImGui::Checkbox("Hadronization", &cfg.hadronization_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color confinement enforcement\n"
                "Free quarks form mesons (q+qbar) or spawn vacuum pairs\n"
                "String breaking creates new pairs when quarks separate\n"
                "Disabled above QGP temperature (2 trillion K)");

        ImGui::Spacing();

        // Weak Nuclear
        ImGui::TextColored(ImVec4(0.7f, 0.3f, 0.9f, 1.0f), "Weak Nuclear");
        ImGui::SliderFloat("Weak Force", &cfg.weak_coupling, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Weak force coupling strength\nMediates beta decay and flavor changes\nVery short range (~3 px)");

        ImGui::SliderFloat("Higgs VEV", &cfg.higgs_vev, 0.0f, 500.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higgs vacuum expectation value\nSets the mass scale for W/Z bosons\nStandard Model: 246");

        ImGui::Spacing();

        // Gravity
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.3f, 1.0f), "Gravity");
        ImGui::SliderFloat("Strength##grav", &cfg.gravity_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gravitational attraction (Newton's law)\n1.0 = F = G·m1·m2/r²\n0 = off\n>1 = enhanced gravity");
    }

    // ── Force Multipliers ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Force Multipliers")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Scale individual force strengths (1.0 = SM)");
        ImGui::SliderFloat("Coulomb (EM)", &cfg.coulomb_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Electromagnetic Coulomb force multiplier\n0 = no charge interaction\n1 = standard model\n>1 = enhanced EM");
        ImGui::SliderFloat("Yukawa (Nuclear)", &cfg.yukawa_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Strong nuclear binding (Yukawa potential)\n0 = no nuclear binding\n1 = standard model\n>1 = enhanced binding");
        ImGui::SliderFloat("Pauli Exclusion", &cfg.pauli_multiplier, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pauli exclusion repulsion strength\n0 = bosonic (particles overlap)\n1 = standard fermionic repulsion");
        ImGui::SliderFloat("QCD Color", &cfg.alpha_s_scale, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("QCD strong force (Cornell potential)\n0 = deconfined quarks\n1 = standard model\n>1 = enhanced confinement");
        ImGui::SliderFloat("Compton", &cfg.compton_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compton scattering (photon-matter)\n0 = photons don't push matter\n1 = standard model");
        ImGui::SliderFloat("Annihilation", &cfg.annihilation_strength, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matter-antimatter annihilation rate\n0 = no annihilation\n1 = standard model\n>1 = faster annihilation");

        if (ImGui::Button("Reset to SM##forces")) {
            cfg.coulomb_strength = 1.0f;
            cfg.yukawa_strength = 1.0f;
            cfg.pauli_multiplier = 1.0f;
            cfg.alpha_s_scale = 1.0f;
            cfg.compton_strength = 1.0f;
            cfg.annihilation_strength = 1.0f;
        }
    }

    // ── Electron Orbitals ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Electron Orbitals")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Per-shell orbital speed and binding");
        ImGui::SliderFloat("1s Boost", &cfg.orbit_boost[0], 0.5f, 12.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shell 1 (1s) speed multiplier\n2 electrons, closest to nucleus");
        ImGui::SliderFloat("2sp Boost", &cfg.orbit_boost[1], 0.5f, 12.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shell 2 (2s, 2p) speed multiplier\n8 electrons");
        ImGui::SliderFloat("3spd Boost", &cfg.orbit_boost[2], 0.5f, 12.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shell 3 (3s, 3p, 3d) speed multiplier\n18 electrons");
        ImGui::SliderFloat("4spdf Boost", &cfg.orbit_boost[3], 0.5f, 12.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shell 4 (4s, 4p, 4d, 4f) speed multiplier\n32 electrons, outermost");
        ImGui::SliderFloat("Binding Radius", &cfg.orbital_binding_radius, 30.0f, 300.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum distance an electron can bind to a nucleus\nLarger = electrons captured from further away");
        ImGui::SliderFloat("Shell Offset", &cfg.orbital_shell_offset, 0.0f, 6.28f, "%.2f rad");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Angular offset between shells at spawn\n2.40 = golden angle (natural distribution)\n0 = all shells aligned");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Shell Transitions");
        ImGui::Checkbox("Enable Transitions", &cfg.shell_transitions_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Allow electrons to jump between shells\nPromotion (thermal), de-excitation (photon emission), ionization");
        if (cfg.shell_transitions_enabled) {
            ImGui::SliderFloat("De-excitation Time", &cfg.deexcitation_lifetime, 10.0f, 300.0f, "%.0f frames");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How long an excited electron stays in a higher shell\nbefore dropping back down and emitting a photon");
            ImGui::SliderInt("Max Trans/Frame", &cfg.max_transitions_per_frame, 1, 32);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum shell transitions per simulation frame\nHigher = more responsive, slightly more CPU");
        }

        ImGui::Separator();
        if (ImGui::Button("Reset##orbitals")) {
            cfg.orbit_boost[0] = 1.5f;
            cfg.orbit_boost[1] = 3.5f;
            cfg.orbit_boost[2] = 6.0f;
            cfg.orbit_boost[3] = 9.0f;
            cfg.orbital_binding_radius = 130.0f;
            cfg.orbital_shell_offset = 2.399f;
            cfg.shell_transitions_enabled = true;
            cfg.deexcitation_lifetime = 60.0f;
            cfg.max_transitions_per_frame = 8;
        }
    }

    // ── World Settings ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("World Settings")) {
        ImGui::SliderFloat("Friction", &cfg.dampening, 0.50f, 0.99f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Velocity dampening per frame\n0.99 = near-vacuum (very low friction)\n0.90 = dense medium\nLower = more energy loss");

        ImGui::SliderFloat("Hard Core", &cfg.repulsion_radius, 1.0f, 40.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum distance before particles repel\nModels nucleon hard-core radius");

        ImGui::SliderFloat("Core Force", &cfg.pressure_resistance, 5.0f, 100.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How strongly particles repel at close range\nHigher = harder collisions");

        ImGui::SliderFloat("Max Range", &cfg.interaction_radius, 20.0f, 200.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum distance for force calculations\nHigher = longer-range EM interactions\nLower = faster simulation");

        ImGui::SliderFloat("Display Size", &cfg.radius, 0.5f, 8.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Visual size of particles on screen\nDoes not affect physics");
    }

    // ── Field Visualization ──────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Field Visualization")) {
        ImGui::Checkbox("EM##field", &field_em);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show electric field lines\nRed = positive, Blue = negative");
        ImGui::SameLine();
        ImGui::Checkbox("Strong##field", &field_strong);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show strong force field\nGreen glow around nucleons");

        ImGui::Checkbox("Weak##field", &field_weak);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show weak field around W/Z bosons");
        ImGui::SameLine();
        ImGui::Checkbox("Gravity##field", &field_gravity);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show gravitational potential wells");

        ImGui::Checkbox("Higgs##field", &field_higgs);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show Higgs field coupling to massive particles");
        ImGui::SameLine();
        ImGui::Checkbox("Dark Energy##field", &field_dark_energy);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show dark energy repulsive field\nCrimson glow growing with distance\nInflaton = 20x stronger, Chameleon = screened");

        ImGui::Checkbox("Collision##field", &show_collision_radii);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show collision detection radii\nGreen = hard-sphere, Yellow = Pauli core, Red = Yukawa range");

        if (field_em || field_strong || field_weak || field_gravity || field_higgs || field_dark_energy) {
            ImGui::SliderFloat("Brightness", &field_intensity, 0.05f, 2.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Field visualization brightness");
        }
    }

    // ── Virtual Particles ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Virtual Particles")) {
        ImGui::Checkbox("Enable Virtual Pairs", &cfg.virtual_pairs_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spontaneous particle-antiparticle pairs\nfrom quantum vacuum fluctuations\n(Casimir effect source)");

        ImGui::Checkbox("Hide Virtual Trails", &prefs.hide_virtual_trails);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide virtual particle rendering\nwhile keeping the physics active");

        if (cfg.virtual_pairs_enabled) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Tune parameters in Nuclear Debug");
        }
    }

    // ── Entanglement ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Entanglement")) {
        ImGui::Checkbox("Enable Entanglement", &cfg.entanglement_enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Quantum entanglement between particle pairs\n"
                             "Created during virtual pair production\n"
                             "Correlated spins + velocity coupling");

        if (cfg.entanglement_enabled) {
            ImGui::SliderFloat("Coupling", &cfg.entanglement_coupling, 0.0f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Velocity coupling fraction between entangled pairs\n"
                                 "0 = no coupling, higher = stronger 'spooky action'");

            ImGui::SliderFloat("Decoherence", &cfg.entanglement_decoherence, 0.0f, 0.05f, "%.4f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Probability per tick of entanglement breaking\n"
                                 "0 = permanent, higher = faster decay");

            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f),
                "Active pairs: %u", entangled_pair_count_display);
        }
    }

    // ── Carrier Mode (parameters only — toggle is in bottom bar menu) ────
    if (cfg.carrier_mode_enabled) {
        if (ImGui::CollapsingHeader("Carrier Mode")) {
            ImGui::SliderInt("Max Carriers/Tick", &cfg.carrier_max_per_tick, 1, 10);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum force carriers spawned per frame\n"
                                 "Higher = more visible exchanges, more particles");

            ImGui::SliderFloat("Exchange Radius", &cfg.carrier_exchange_radius, 20.0f, 100.0f, "%.0f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum distance for carrier exchange\n"
                                 "Carriers spawn between interacting particle pairs\n"
                                 "within this radius");
        }
    }

    wobble_window(1.0f);
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Spawn Menu (Consolidated with collapsing headers) ───────────────────────
// ══════════════════════════════════════════════════════════════════════════════


// ── Spawn menu ───────────────────────────────────────────────────────────────

void PhysicsInterface::draw_spawn_menu(const SimConfig& /*cfg*/) {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;
    ImVec2 def_size(380, std::min(720.0f, max_h));

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_SPAWN_MENU]);
        ImGui::SetNextWindowSize(tile_size_[TW_SPAWN_MENU]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(def_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(def_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(340, 200), ImVec2(420, max_h));

    bool open = ImGui::Begin("Spawn Particles", &spawn_menu_visible);
    record_window_rect(TW_SPAWN_MENU);
    if (!open) {
        wobble_window(2.0f);
        ImGui::End();
        return;
    }
    draw_minimize_button(TW_SPAWN_MENU);

    // ── Periodic Table ──────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Periodic Table", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImVec2 btn_size(32, 26);
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 10; ++col) {
                if (col > 0) ImGui::SameLine();
                int idx = PT_LAYOUT[row][col];
                if (idx < 0) {
                    ImGui::Dummy(btn_size);
                    continue;
                }
                const auto& e = ELEMENTS[idx];
                bool selected = (spawn_atom_Z == e.Z);

                // Color by category
                ImVec4 color(0.15f, 0.30f, 0.20f, 0.80f);  // default
                if (e.Z == 2 || e.Z == 10 || e.Z == 18)
                    color = ImVec4(0.25f, 0.15f, 0.40f, 0.80f);  // noble gas
                else if (e.Z == 1 || e.Z == 6 || e.Z == 7 || e.Z == 8 ||
                         e.Z == 9 || e.Z == 15 || e.Z == 16 || e.Z == 17)
                    color = ImVec4(0.12f, 0.25f, 0.45f, 0.80f);  // nonmetal
                else if (e.Z == 3 || e.Z == 11 || e.Z == 19)
                    color = ImVec4(0.45f, 0.15f, 0.12f, 0.80f);  // alkali
                else if (e.Z == 4 || e.Z == 12 || e.Z == 20)
                    color = ImVec4(0.40f, 0.28f, 0.10f, 0.80f);  // alkaline earth
                else if (e.Z == 5 || e.Z == 14)
                    color = ImVec4(0.30f, 0.28f, 0.15f, 0.80f);  // metalloid
                else if (e.Z >= 21)
                    color = ImVec4(0.25f, 0.25f, 0.30f, 0.80f);  // transition metal

                if (selected)
                    color = ImVec4(0.15f, 0.45f, 0.60f, 0.90f);

                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(color.x * 1.5f, color.y * 1.5f, color.z * 1.5f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(0.302f, 0.749f, 0.953f, 0.80f));
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                }

                char btn_id[32];
                snprintf(btn_id, sizeof(btn_id), "%s##pt%d", e.symbol, e.Z);
                if (ImGui::Button(btn_id, btn_size)) {
                    spawn_atom_Z = e.Z;
                    spawn_atom_N = e.N;
                    spawn_group = -1;
                    pending_spawn = true;
                }
                if (ImGui::IsItemHovered()) {
                    int total = e.Z * 2 + e.N;
                    ImGui::SetTooltip("%s (Z=%d)\n%dp + %dn + %de = %d particles",
                        e.name, e.Z, e.Z, e.N, e.Z, total);
                }

                if (selected) {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor(3);
            }
        }

        // Composites
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Composites:");
        for (int g = 0; g < GROUP_TEMPLATE_COUNT_VAL; ++g) {
            const char* lbl = GROUP_TEMPLATES[g].label;
            if (strcmp(lbl, "H") == 0 || strcmp(lbl, "He") == 0 ||
                strcmp(lbl, "Li") == 0 || strcmp(lbl, "C") == 0 ||
                strcmp(lbl, "O") == 0) continue;

            bool sel = (spawn_group == g && spawn_atom_Z < 0);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char gid[32];
            snprintf(gid, sizeof(gid), "%s##gt%d", GROUP_TEMPLATES[g].label, g);
            if (ImGui::Button(gid, ImVec2(40, 24))) {
                spawn_group = g;
                spawn_atom_Z = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u particles)", GROUP_TEMPLATES[g].name, GROUP_TEMPLATES[g].count);

            if (sel) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    // ── Molecules ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Molecules")) {
        // ── Scan cached .ppmol files from repository ────────────────────────
        struct CachedMol {
            std::string formula;       // parsed from filename (e.g. "H2O")
            std::string name;          // looked up from MOLECULE_COMMON_NAMES
            std::string path;          // full filesystem path
        };
        static std::vector<CachedMol> cached_molecules;
        static float last_cache_scan = -10.0f;

        // Rescan every 5 seconds (lightweight directory read)
        float now = static_cast<float>(ImGui::GetTime());
        if (now - last_cache_scan > 5.0f) {
            last_cache_scan = now;
            cached_molecules.clear();
            std::string mol_cache = get_data_dir() + "repository/molecules/";
            std::error_code ec;
            if (std::filesystem::is_directory(mol_cache, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(mol_cache, ec)) {
                    if (!entry.is_regular_file()) continue;
                    std::string fname = entry.path().filename().string();
                    if (fname.size() < 6 || fname.substr(fname.size() - 6) != ".ppmol") continue;
                    std::string formula = fname.substr(0, fname.size() - 6);

                    // Skip if this formula already exists in MOLECULE_TEMPLATES
                    if (find_molecule_template(formula.c_str()) >= 0) continue;

                    CachedMol cm;
                    cm.formula = formula;
                    const char* cname = lookup_molecule_common_name(formula.c_str());
                    cm.name = cname ? cname : "";
                    cm.path = entry.path().string();
                    cached_molecules.push_back(std::move(cm));
                }
            }
        }

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Type a formula:");

        bool enter_pressed = ImGui::InputText("##mol_formula", molecule_formula_buf, 64,
            ImGuiInputTextFlags_EnterReturnsTrue);

        // Live search: find exact and prefix matches
        molecule_match_idx = -1;
        int cached_match_idx = -1;   // index into cached_molecules for exact match
        spawn_molecule_file.clear();
        int match_count = 0;
        int match_indices[16] = {};
        bool match_is_cached[16] = {};

        if (molecule_formula_buf[0] != '\0') {
            size_t buf_len = strlen(molecule_formula_buf);

            // Exact match first (templates)
            molecule_match_idx = find_molecule_template(molecule_formula_buf);

            // If no template exact match, check cached files
            if (molecule_match_idx < 0) {
                for (int ci = 0; ci < static_cast<int>(cached_molecules.size()); ++ci) {
                    if (cached_molecules[ci].formula == molecule_formula_buf) {
                        cached_match_idx = ci;
                        spawn_molecule_file = cached_molecules[ci].path;
                        break;
                    }
                }
            }

            // Prefix/substring matches from templates
            for (int mi = 0; mi < MOLECULE_TEMPLATE_COUNT && match_count < 12; ++mi) {
                if (strncmp(MOLECULE_TEMPLATES[mi].formula, molecule_formula_buf, buf_len) == 0) {
                    match_indices[match_count] = mi;
                    match_is_cached[match_count] = false;
                    match_count++;
                    continue;
                }
                // Name substring (case-insensitive)
                const char* name = MOLECULE_TEMPLATES[mi].name;
                bool name_match = false;
                for (size_t ni = 0; name[ni] && !name_match; ++ni) {
                    bool ok = true;
                    for (size_t bi = 0; bi < buf_len && ok; ++bi) {
                        char nc = name[ni + bi];
                        char bc = molecule_formula_buf[bi];
                        if (nc >= 'A' && nc <= 'Z') nc += 32;
                        if (bc >= 'A' && bc <= 'Z') bc += 32;
                        if (nc != bc) ok = false;
                    }
                    if (ok) name_match = true;
                }
                if (name_match) {
                    match_indices[match_count] = mi;
                    match_is_cached[match_count] = false;
                    match_count++;
                }
            }

            // Prefix/substring matches from cached files
            for (int ci = 0; ci < static_cast<int>(cached_molecules.size()) && match_count < 16; ++ci) {
                const auto& cm = cached_molecules[ci];
                // Formula prefix
                if (strncmp(cm.formula.c_str(), molecule_formula_buf, buf_len) == 0) {
                    match_indices[match_count] = ci;
                    match_is_cached[match_count] = true;
                    match_count++;
                    continue;
                }
                // Name substring (case-insensitive)
                if (!cm.name.empty()) {
                    bool name_match = false;
                    for (size_t ni = 0; ni < cm.name.size() && !name_match; ++ni) {
                        bool ok = true;
                        for (size_t bi = 0; bi < buf_len && ok; ++bi) {
                            if (ni + bi >= cm.name.size()) { ok = false; break; }
                            char nc = cm.name[ni + bi];
                            char bc = molecule_formula_buf[bi];
                            if (nc >= 'A' && nc <= 'Z') nc += 32;
                            if (bc >= 'A' && bc <= 'Z') bc += 32;
                            if (nc != bc) ok = false;
                        }
                        if (ok) name_match = true;
                    }
                    if (name_match) {
                        match_indices[match_count] = ci;
                        match_is_cached[match_count] = true;
                        match_count++;
                    }
                }
            }
        }

        // Show match status
        ImGui::SameLine();
        if (molecule_match_idx >= 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%s",
                MOLECULE_TEMPLATES[molecule_match_idx].name);
        } else if (cached_match_idx >= 0) {
            // Cached file exact match
            const char* display = cached_molecules[cached_match_idx].name.empty()
                ? cached_molecules[cached_match_idx].formula.c_str()
                : cached_molecules[cached_match_idx].name.c_str();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.9f, 1.0f), "%s", display);
        } else if (molecule_formula_buf[0] != '\0') {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "?");
        }

        // Spawn button (or Enter) — template molecule
        if (molecule_match_idx >= 0) {
            if (enter_pressed || ImGui::Button("Spawn##mol_spawn")) {
                spawn_molecule_idx = molecule_match_idx;
                spawn_molecule_file.clear();
                spawn_atom_Z = -1;
                spawn_group = -1;
                pending_spawn = true;
            }
        }
        // Spawn button — cached .ppmol file (import at camera center)
        else if (cached_match_idx >= 0) {
            if (enter_pressed || ImGui::Button("Spawn##mol_spawn")) {
                snprintf(save_filename, sizeof(save_filename), "%s",
                         cached_molecules[cached_match_idx].path.c_str());
                request_molecule_import = true;
            }
        }

        // Autocomplete list
        if (match_count > 0 && molecule_formula_buf[0] != '\0') {
            for (int mi = 0; mi < match_count; ++mi) {
                int idx = match_indices[mi];
                char label[192];

                if (!match_is_cached[mi]) {
                    // Template molecule
                    snprintf(label, sizeof(label), "%s  (%s)##mol_%d",
                        MOLECULE_TEMPLATES[idx].formula,
                        MOLECULE_TEMPLATES[idx].name, idx);
                    if (ImGui::Selectable(label)) {
                        snprintf(molecule_formula_buf, 64, "%s", MOLECULE_TEMPLATES[idx].formula);
                        spawn_molecule_idx = idx;
                        spawn_molecule_file.clear();
                        spawn_atom_Z = -1;
                        spawn_group = -1;
                        pending_spawn = true;
                    }
                } else {
                    // Cached .ppmol file
                    const auto& cm = cached_molecules[idx];
                    if (!cm.name.empty()) {
                        snprintf(label, sizeof(label), "%s  (%s) [repo]##cmol_%d",
                            cm.formula.c_str(), cm.name.c_str(), idx);
                    } else {
                        snprintf(label, sizeof(label), "%s  [repo]##cmol_%d",
                            cm.formula.c_str(), idx);
                    }
                    if (ImGui::Selectable(label)) {
                        snprintf(molecule_formula_buf, 64, "%s", cm.formula.c_str());
                        snprintf(save_filename, sizeof(save_filename), "%s", cm.path.c_str());
                        request_molecule_import = true;
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Quick:");

        struct QuickMol { const char* formula; const char* label; };
        static const QuickMol quick[] = {
            {"H2O",   "H\xe2\x82\x82O"},
            {"CO2",   "CO\xe2\x82\x82"},
            {"NH3",   "NH\xe2\x82\x83"},
            {"CH4",   "CH\xe2\x82\x84"},
            {"NaCl",  "NaCl"},
            {"C2H5OH","EtOH"},
            {"C6H6",  "C\xe2\x82\x86H\xe2\x82\x86"},
            {"C6H12O6","Glucose"},
        };

        for (int qi = 0; qi < 8; ++qi) {
            int idx = find_molecule_template(quick[qi].formula);
            if (idx < 0) continue;
            if (qi > 0) ImGui::SameLine();

            bool sel = (spawn_molecule_idx == idx && pending_spawn);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char qid[32];
            snprintf(qid, sizeof(qid), "%s##qm%d", quick[qi].label, qi);
            if (ImGui::Button(qid, ImVec2(0, 24))) {
                snprintf(molecule_formula_buf, 64, "%s", quick[qi].formula);
                spawn_molecule_idx = idx;
                molecule_match_idx = idx;
                spawn_atom_Z = -1;
                spawn_group = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%s, %u atoms)", MOLECULE_TEMPLATES[idx].name,
                    MOLECULE_TEMPLATES[idx].formula, MOLECULE_TEMPLATES[idx].atom_count);

            if (sel) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
        }
    }

    // ── Leptons ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Leptons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 1:");
        if (spawn_button(ELECTRON_TYPE_PHYS, "e-", PHYS_TYPE_UI_COLORS[ELECTRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron"))
            { spawn_type = ELECTRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(POSITRON_TYPE_PHYS, "e+", PHYS_TYPE_UI_COLORS[POSITRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Positron"))
            { spawn_type = POSITRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRINO_TYPE_PHYS, "ve", PHYS_TYPE_UI_COLORS[NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Electron neutrino"))
            { spawn_type = NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 2:");
        if (spawn_button(MUON_TYPE_PHYS, "mu-", PHYS_TYPE_UI_COLORS[MUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon (decays ~100 frames)"))
            { spawn_type = MUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIMUON_TYPE_PHYS, "mu+", PHYS_TYPE_UI_COLORS[ANTIMUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-muon"))
            { spawn_type = ANTIMUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(MU_NEUTRINO_TYPE_PHYS, "vmu", PHYS_TYPE_UI_COLORS[MU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Muon neutrino"))
            { spawn_type = MU_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Generation 3:");
        if (spawn_button(TAU_TYPE_PHYS, "tau-", PHYS_TYPE_UI_COLORS[TAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau (decays ~5 frames)"))
            { spawn_type = TAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTITAU_TYPE_PHYS, "tau+", PHYS_TYPE_UI_COLORS[ANTITAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Anti-tau"))
            { spawn_type = ANTITAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TAU_NEUTRINO_TYPE_PHYS, "vtau", PHYS_TYPE_UI_COLORS[TAU_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Tau neutrino"))
            { spawn_type = TAU_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Composites:");
        if (spawn_button(PROTON_TYPE, "p", PHYS_TYPE_UI_COLORS[PROTON_TYPE],
                          spawn_type, spawn_group, "Proton"))
            { spawn_type = PROTON_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRON_TYPE, "n", PHYS_TYPE_UI_COLORS[NEUTRON_TYPE],
                          spawn_type, spawn_group, "Neutron"))
            { spawn_type = NEUTRON_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTIPROTON_TYPE_PHYS, "p-", PHYS_TYPE_UI_COLORS[ANTIPROTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Antiproton"))
            { spawn_type = ANTIPROTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Quarks ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Quarks")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Matter:");
        if (spawn_button(UP_QUARK_TYPE, "u", PHYS_TYPE_UI_COLORS[UP_QUARK_TYPE],
                          spawn_type, spawn_group, "Up quark (+2/3)"))
            { spawn_type = UP_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DOWN_QUARK_TYPE, "d", PHYS_TYPE_UI_COLORS[DOWN_QUARK_TYPE],
                          spawn_type, spawn_group, "Down quark (-1/3)"))
            { spawn_type = DOWN_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(STRANGE_QUARK_TYPE, "s", PHYS_TYPE_UI_COLORS[STRANGE_QUARK_TYPE],
                          spawn_type, spawn_group, "Strange quark (-1/3, decays)"))
            { spawn_type = STRANGE_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(CHARM_QUARK_TYPE, "c", PHYS_TYPE_UI_COLORS[CHARM_QUARK_TYPE],
                          spawn_type, spawn_group, "Charm quark (+2/3, decays fast)"))
            { spawn_type = CHARM_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(TOP_QUARK_TYPE, "t", PHYS_TYPE_UI_COLORS[TOP_QUARK_TYPE],
                          spawn_type, spawn_group, "Top quark (+2/3, instant decay)"))
            { spawn_type = TOP_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(BOTTOM_QUARK_TYPE, "b", PHYS_TYPE_UI_COLORS[BOTTOM_QUARK_TYPE],
                          spawn_type, spawn_group, "Bottom quark (-1/3, decays)"))
            { spawn_type = BOTTOM_QUARK_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Antimatter:");
        if (spawn_button(ANTI_UP_TYPE, "u~", PHYS_TYPE_UI_COLORS[ANTI_UP_TYPE],
                          spawn_type, spawn_group, "Anti-up (-2/3)"))
            { spawn_type = ANTI_UP_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_DOWN_TYPE, "d~", PHYS_TYPE_UI_COLORS[ANTI_DOWN_TYPE],
                          spawn_type, spawn_group, "Anti-down (+1/3)"))
            { spawn_type = ANTI_DOWN_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_STRANGE_TYPE, "s~", PHYS_TYPE_UI_COLORS[ANTI_STRANGE_TYPE],
                          spawn_type, spawn_group, "Anti-strange (+1/3)"))
            { spawn_type = ANTI_STRANGE_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(ANTI_CHARM_TYPE, "c~", PHYS_TYPE_UI_COLORS[ANTI_CHARM_TYPE],
                          spawn_type, spawn_group, "Anti-charm (-2/3)"))
            { spawn_type = ANTI_CHARM_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_TOP_TYPE, "t~", PHYS_TYPE_UI_COLORS[ANTI_TOP_TYPE],
                          spawn_type, spawn_group, "Anti-top (-2/3)"))
            { spawn_type = ANTI_TOP_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ANTI_BOTTOM_TYPE, "b~", PHYS_TYPE_UI_COLORS[ANTI_BOTTOM_TYPE],
                          spawn_type, spawn_group, "Anti-bottom (+1/3)"))
            { spawn_type = ANTI_BOTTOM_TYPE; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Bosons ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Bosons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Gauge:");
        if (spawn_button(PHOTON_TYPE_PHYS, "y", PHYS_TYPE_UI_COLORS[PHOTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Photon (massless, stable)"))
            { spawn_type = PHOTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(GLUON_TYPE_PHYS, "g", PHYS_TYPE_UI_COLORS[GLUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Gluon (strong force mediator)"))
            { spawn_type = GLUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Weak / Scalar:");
        if (spawn_button(W_PLUS_TYPE_PHYS, "W+", PHYS_TYPE_UI_COLORS[W_PLUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W+ boson (instant decay)"))
            { spawn_type = W_PLUS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(W_MINUS_TYPE_PHYS, "W-", PHYS_TYPE_UI_COLORS[W_MINUS_TYPE_PHYS],
                          spawn_type, spawn_group, "W- boson (instant decay)"))
            { spawn_type = W_MINUS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(Z_BOSON_TYPE_PHYS, "Z0", PHYS_TYPE_UI_COLORS[Z_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "Z0 boson (instant decay)"))
            { spawn_type = Z_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(HIGGS_TYPE_PHYS, "H0", PHYS_TYPE_UI_COLORS[HIGGS_TYPE_PHYS],
                          spawn_type, spawn_group, "Higgs boson (instant decay)"))
            { spawn_type = HIGGS_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Hypothetical ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hypothetical")) {
        if (spawn_button(GRAVITON_TYPE_PHYS, "G", PHYS_TYPE_UI_COLORS[GRAVITON_TYPE_PHYS],
                          spawn_type, spawn_group, "Graviton (spin-2, massless)"))
            { spawn_type = GRAVITON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_MATTER_TYPE_PHYS, "DM", PHYS_TYPE_UI_COLORS[DARK_MATTER_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Matter (WIMP, gravity-only)"))
            { spawn_type = DARK_MATTER_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_ENERGY_TYPE_PHYS, "DE", PHYS_TYPE_UI_COLORS[DARK_ENERGY_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Energy (repulsive field)"))
            { spawn_type = DARK_ENERGY_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Dark Matter Candidates ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Dark Matter Candidates:");
        if (spawn_button(AXINO_TYPE_PHYS, "Ax", PHYS_TYPE_UI_COLORS[AXINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Axino (SUSY partner of axion)"))
            { spawn_type = AXINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(WIMPZILLA_TYPE_PHYS, "WZ", PHYS_TYPE_UI_COLORS[WIMPZILLA_TYPE_PHYS],
                          spawn_type, spawn_group, "WIMPzilla (super-heavy ~10^12 GeV)"))
            { spawn_type = WIMPZILLA_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SIMP_TYPE_PHYS, "SI", PHYS_TYPE_UI_COLORS[SIMP_TYPE_PHYS],
                          spawn_type, spawn_group, "SIMP (strongly self-interacting)"))
            { spawn_type = SIMP_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(STERILE_NEUTRINO_TYPE_PHYS, "Ns", PHYS_TYPE_UI_COLORS[STERILE_NEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Sterile Neutrino (no weak force)"))
            { spawn_type = STERILE_NEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DARK_PHOTON_TYPE_PHYS, "A'", PHYS_TYPE_UI_COLORS[DARK_PHOTON_TYPE_PHYS],
                          spawn_type, spawn_group, "Dark Photon (dark EM carrier)"))
            { spawn_type = DARK_PHOTON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(QBALL_TYPE_PHYS, "QB", PHYS_TYPE_UI_COLORS[QBALL_TYPE_PHYS],
                          spawn_type, spawn_group, "Q-Ball (stable field soliton, charged)"))
            { spawn_type = QBALL_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Supersymmetric Sparticles ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Supersymmetric (Sparticles):");
        if (spawn_button(SELECTRON_TYPE_PHYS, "e~", PHYS_TYPE_UI_COLORS[SELECTRON_TYPE_PHYS],
                          spawn_type, spawn_group, "Selectron (scalar e-, 200 GeV)"))
            { spawn_type = SELECTRON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SMUON_TYPE_PHYS, "mu~", PHYS_TYPE_UI_COLORS[SMUON_TYPE_PHYS],
                          spawn_type, spawn_group, "Smuon (scalar muon, 300 GeV)"))
            { spawn_type = SMUON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(STAU_TYPE_PHYS, "ta~", PHYS_TYPE_UI_COLORS[STAU_TYPE_PHYS],
                          spawn_type, spawn_group, "Stau (scalar tau, 150 GeV, long-lived)"))
            { spawn_type = STAU_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SQUARK_TYPE_PHYS, "q~", PHYS_TYPE_UI_COLORS[SQUARK_TYPE_PHYS],
                          spawn_type, spawn_group, "Squark (scalar quark, 1.5 TeV, color-charged)"))
            { spawn_type = SQUARK_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(GLUINO_TYPE_PHYS, "g~", PHYS_TYPE_UI_COLORS[GLUINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Gluino (fermionic gluon, 2 TeV)"))
            { spawn_type = GLUINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(PHOTINO_TYPE_PHYS, "y~", PHYS_TYPE_UI_COLORS[PHOTINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Photino (fermionic photon, 100 GeV)"))
            { spawn_type = PHOTINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(WINO_TYPE_PHYS, "W~", PHYS_TYPE_UI_COLORS[WINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Wino (fermionic W, 300 GeV)"))
            { spawn_type = WINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(ZINO_TYPE_PHYS, "Z~", PHYS_TYPE_UI_COLORS[ZINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Zino (fermionic Z, 300 GeV)"))
            { spawn_type = ZINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(HIGGSINO_TYPE_PHYS, "H~", PHYS_TYPE_UI_COLORS[HIGGSINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Higgsino (fermionic Higgs, 200 GeV)"))
            { spawn_type = HIGGSINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(NEUTRALINO_TYPE_PHYS, "N1", PHYS_TYPE_UI_COLORS[NEUTRALINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Neutralino (stable LSP, DM candidate)"))
            { spawn_type = NEUTRALINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SNEUTRINO_TYPE_PHYS, "v~", PHYS_TYPE_UI_COLORS[SNEUTRINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Sneutrino (scalar neutrino, 200 GeV)"))
            { spawn_type = SNEUTRINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Force Carriers & Exotic ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Force Carriers & Exotic:");
        if (spawn_button(GRAVITINO_TYPE_PHYS, "G~", PHYS_TYPE_UI_COLORS[GRAVITINO_TYPE_PHYS],
                          spawn_type, spawn_group, "Gravitino (SUSY graviton, spin-3/2)"))
            { spawn_type = GRAVITINO_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(X_BOSON_TYPE_PHYS, "X", PHYS_TYPE_UI_COLORS[X_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "X Boson (GUT, proton decay, 10^15 GeV)"))
            { spawn_type = X_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(Y_BOSON_TYPE_PHYS, "Y", PHYS_TYPE_UI_COLORS[Y_BOSON_TYPE_PHYS],
                          spawn_type, spawn_group, "Y Boson (GUT, proton decay, 10^15 GeV)"))
            { spawn_type = Y_BOSON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(MONOPOLE_TYPE_PHYS, "MM", PHYS_TYPE_UI_COLORS[MONOPOLE_TYPE_PHYS],
                          spawn_type, spawn_group, "Magnetic Monopole (radial B-field, 10^16 GeV)"))
            { spawn_type = MONOPOLE_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(RADION_TYPE_PHYS, "Ra", PHYS_TYPE_UI_COLORS[RADION_TYPE_PHYS],
                          spawn_type, spawn_group, "Radion (extra-dimension size, 1 TeV)"))
            { spawn_type = RADION_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DILATON_TYPE_PHYS, "Di", PHYS_TYPE_UI_COLORS[DILATON_TYPE_PHYS],
                          spawn_type, spawn_group, "Dilaton (string theory scale, 10 GeV)"))
            { spawn_type = DILATON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── Theoretical Extremes ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Theoretical Extremes:");
        if (spawn_button(TACHYON_TYPE_PHYS, "Ta", PHYS_TYPE_UI_COLORS[TACHYON_TYPE_PHYS],
                          spawn_type, spawn_group, "Tachyon (superluminal, imaginary mass)"))
            { spawn_type = TACHYON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(PREON_TYPE_PHYS, "Pr", PHYS_TYPE_UI_COLORS[PREON_TYPE_PHYS],
                          spawn_type, spawn_group, "Preon (sub-quark constituent, confined)"))
            { spawn_type = PREON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(INFLATON_TYPE_PHYS, "In", PHYS_TYPE_UI_COLORS[INFLATON_TYPE_PHYS],
                          spawn_type, spawn_group, "Inflaton (cosmic inflation driver)"))
            { spawn_type = INFLATON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(MAJORON_TYPE_PHYS, "Mj", PHYS_TYPE_UI_COLORS[MAJORON_TYPE_PHYS],
                          spawn_type, spawn_group, "Majoron (neutrino mass, nearly invisible)"))
            { spawn_type = MAJORON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(ODDERON_TYPE_PHYS, "Od", PHYS_TYPE_UI_COLORS[ODDERON_TYPE_PHYS],
                          spawn_type, spawn_group, "Odderon (3-gluon bound state)"))
            { spawn_type = ODDERON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(GLUEBALL_TYPE_PHYS, "Gb", PHYS_TYPE_UI_COLORS[GLUEBALL_TYPE_PHYS],
                          spawn_type, spawn_group, "Glueball (pure-glue bound state)"))
            { spawn_type = GLUEBALL_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(SKYRMION_TYPE_PHYS, "Sk", PHYS_TYPE_UI_COLORS[SKYRMION_TYPE_PHYS],
                          spawn_type, spawn_group, "Skyrmion (topological soliton, baryon-like)"))
            { spawn_type = SKYRMION_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(X17_TYPE_PHYS, "X17", PHYS_TYPE_UI_COLORS[X17_TYPE_PHYS],
                          spawn_type, spawn_group, "X17 (fifth-force boson, 17 MeV)"))
            { spawn_type = X17_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        if (spawn_button(CHAMELEON_TYPE_PHYS, "Ch", PHYS_TYPE_UI_COLORS[CHAMELEON_TYPE_PHYS],
                          spawn_type, spawn_group, "Chameleon (environment-dependent mass)"))
            { spawn_type = CHAMELEON_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }

        // ── New Class (2025-2026) ──
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "New Class (2025-2026):");
        if (spawn_button(PARAPARTICLE_TYPE_PHYS, "Pp", PHYS_TYPE_UI_COLORS[PARAPARTICLE_TYPE_PHYS],
                          spawn_type, spawn_group, "Paraparticle (exotic statistics)"))
            { spawn_type = PARAPARTICLE_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
        ImGui::SameLine();
        if (spawn_button(DYN_AXION_QP_TYPE_PHYS, "Dq", PHYS_TYPE_UI_COLORS[DYN_AXION_QP_TYPE_PHYS],
                          spawn_type, spawn_group, "Dyn. Axion Quasiparticle (topological EM)"))
            { spawn_type = DYN_AXION_QP_TYPE_PHYS; spawn_group = -1; spawn_atom_Z = -1; pending_spawn = true; }
    }

    // ── Hadrons ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Hadrons")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Baryons (3 quarks):");
        for (int h = 0; h < HADRON_TEMPLATE_COUNT_VAL; ++h) {
            if (h == 7) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mesons (quark + antiquark):");
            }

            int group_id = GROUP_TEMPLATE_COUNT_VAL + h;
            bool selected = (spawn_group == group_id);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.60f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.302f, 0.749f, 0.953f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            if (ImGui::Button(HADRON_TEMPLATES[h].label, ImVec2(50, 30))) {
                spawn_group = group_id;
                spawn_atom_Z = -1;
                pending_spawn = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s (%u quarks)", HADRON_TEMPLATES[h].name, HADRON_TEMPLATES[h].count);

            if (selected) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }

            int row_idx = (h < 7) ? h : (h - 7);
            int row_count = (h < 7) ? 7 : (HADRON_TEMPLATE_COUNT_VAL - 7);
            if ((row_idx + 1) % 4 != 0 && row_idx < row_count - 1) ImGui::SameLine();
        }
    }

    // ── Force Objects ─────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Force Objects")) {
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Click to place in world:");

        static const char* fo_labels[] = {
            "EM", "Strong", "Weak", "Gravity", "Heat",
            nullptr, // 5 = mirror (handled separately)
            "Coulomb", "Vortex", "Well"
        };
        static const char* fo_tips[] = {
            "Electromagnetic field\nElectric (radial) + Magnetic (deflection)\non charged particles",
            "Strong nuclear force\nYukawa-like on baryons",
            "Weak nuclear force\nShort-range on all particles",
            "Gravity well\nAttracts all massive particles",
            "Heat source\nAdds thermal energy to nearby particles",
            nullptr, // mirror
            "Coulomb source\nPure electrostatic point charge\nRepels same-sign, attracts opposite",
            "Vortex field\nTangential force creating circular flow\nLike a cyclotron or whirlpool",
            "Potential well\nHarmonic restoring force toward center\nTraps particles in oscillation"
        };
        static const ImVec4 fo_colors[] = {
            ImVec4(0.3f, 0.5f, 1.0f, 1.0f),   // EM — blue
            ImVec4(0.3f, 0.9f, 0.4f, 1.0f),   // Strong — green
            ImVec4(0.7f, 0.3f, 0.9f, 1.0f),   // Weak — purple
            ImVec4(0.9f, 0.7f, 0.2f, 1.0f),   // Gravity — amber
            ImVec4(1.0f, 0.4f, 0.2f, 1.0f),   // Heat — orange-red
            ImVec4(0.7f, 0.7f, 0.8f, 1.0f),   // Mirror — grey (unused here)
            ImVec4(0.9f, 0.3f, 0.3f, 1.0f),   // Coulomb — red
            ImVec4(0.2f, 0.8f, 0.9f, 1.0f),   // Vortex — cyan
            ImVec4(0.9f, 0.9f, 0.2f, 1.0f),   // Well — yellow
        };

        // First row: fundamental forces (0-4)
        for (int fi = 0; fi < 5; ++fi) {
            if (fi > 0) ImGui::SameLine();

            bool active = (force_obj_placement_mode && force_obj_placement_type == fi);
            ImVec4 c = fo_colors[fi];

            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(c.x * 0.3f, c.y * 0.3f, c.z * 0.3f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(c.x * 0.5f, c.y * 0.5f, c.z * 0.5f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(c.x * 0.7f, c.y * 0.7f, c.z * 0.7f, 1.0f));

            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char fid[32];
            snprintf(fid, sizeof(fid), "%s##fo%d", fo_labels[fi], fi);
            if (ImGui::Button(fid, ImVec2(52, 30))) {
                force_obj_placement_mode = !active;
                force_obj_placement_type = fi;
                pending_spawn = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", fo_tips[fi]);

            if (active) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
        }

        // Second row: utility force objects (6-8) + Mirror
        static const int fo_row2[] = { 6, 7, 8 };
        for (int ri = 0; ri < 3; ++ri) {
            int fi = fo_row2[ri];
            if (ri > 0) ImGui::SameLine();

            bool active = (force_obj_placement_mode && force_obj_placement_type == fi);
            ImVec4 c = fo_colors[fi];

            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(c.x * 0.3f, c.y * 0.3f, c.z * 0.3f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(c.x * 0.5f, c.y * 0.5f, c.z * 0.5f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(c.x * 0.7f, c.y * 0.7f, c.z * 0.7f, 1.0f));

            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(c.x, c.y, c.z, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }

            char fid[32];
            snprintf(fid, sizeof(fid), "%s##fo%d", fo_labels[fi], fi);
            if (ImGui::Button(fid, ImVec2(52, 30))) {
                force_obj_placement_mode = !active;
                force_obj_placement_type = fi;
                pending_spawn = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", fo_tips[fi]);

            if (active) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
        }
        ImGui::SameLine();

        // Mirror button (separate — uses two-click placement)
        {
            ImVec4 mc(0.7f, 0.7f, 0.8f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(mc.x * 0.3f, mc.y * 0.3f, mc.z * 0.3f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(mc.x * 0.5f, mc.y * 0.5f, mc.z * 0.5f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                ImVec4(mc.x * 0.7f, mc.y * 0.7f, mc.z * 0.7f, 1.0f));
            if (mirror_placement_mode) {
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(mc.x, mc.y, mc.z, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            }
            if (ImGui::Button("Mirror##fo5", ImVec2(70, 30))) {
                mirror_placement_mode = !mirror_placement_mode;
                if (mirror_placement_mode) {
                    mirror_placement_phase = 0;
                    force_obj_placement_mode = false;
                    pending_spawn = false;
                    select_mode = false;
                    accel_mode = false;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reflective mirror\nTwo clicks define a line segment");
            if (mirror_placement_mode) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor(3);
        }

        if (force_obj_placement_mode) {
            ImGui::TextColored(fo_colors[force_obj_placement_type],
                "Click in world to place %s object", fo_labels[force_obj_placement_type]);
        }
    }

    // ── Spawn Settings (shared, outside headers) ─────────────────────────────
    ImGui::Separator();
    ImGui::SliderInt("Count", &spawn_count, 1, 5000);
    {
        // Energy slider: 0 eV to 9999 TeV (stored in MeV)
        // 9999 TeV = 9.999e9 MeV
        static constexpr float MAX_ENERGY_MEV = 9.999e9f;
        char e_label[32];
        fmt_energy_ev(e_label, sizeof(e_label), spawn_energy_mev);
        ImGui::SliderFloat("Energy", &spawn_energy_mev, 0.0f, MAX_ENERGY_MEV, e_label,
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    }
    ImGui::SliderFloat("Scatter", &spawn_scatter, 1.0f, 5000.0f, "%.0f");

    // Status text
    if (pending_spawn) {
        ImGui::Spacing();
        if (spawn_molecule_idx >= 0 && spawn_molecule_idx < MOLECULE_TEMPLATE_COUNT) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s (%s)", MOLECULE_TEMPLATES[spawn_molecule_idx].name,
                MOLECULE_TEMPLATES[spawn_molecule_idx].formula);
        } else if (spawn_atom_Z > 0) {
            const char* elem_name = "?";
            for (int ei = 0; ei < ELEMENT_COUNT; ++ei) {
                if (ELEMENTS[ei].Z == spawn_atom_Z) { elem_name = ELEMENTS[ei].name; break; }
            }
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s", elem_name);
        } else if (spawn_group >= GROUP_TEMPLATE_COUNT_VAL) {
            int h_idx = spawn_group - GROUP_TEMPLATE_COUNT_VAL;
            if (h_idx < HADRON_TEMPLATE_COUNT_VAL) {
                ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                    "Click to place %s", HADRON_TEMPLATES[h_idx].name);
            }
        } else if (spawn_group >= 0 && spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click to place %s", GROUP_TEMPLATES[spawn_group].name);
        } else if (spawn_group == -1) {
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f),
                "Click in world to place");
        }
    }

    wobble_window(2.0f);
    ImGui::End();
}

// ── Custom Particle Texture Panel ────────────────────────────────────────────

void PhysicsInterface::draw_texture_panel() {
    if (!show_texture_panel || !texture_mgr) return;

    ImGuiIO& io = ImGui::GetIO();
    float panel_w = 380.0f;
    float panel_h = io.DisplaySize.y * 0.7f;

    ImVec2 tex_size(panel_w, panel_h);
    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_TEXTURE_PANEL]);
        ImGui::SetNextWindowSize(tile_size_[TW_TEXTURE_PANEL]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(tex_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(tex_size, ImGuiCond_Appearing);
    }

    bool open = ImGui::Begin("Particle Textures", &show_texture_panel);
    record_window_rect(TW_TEXTURE_PANEL);
    if (!open) {
        ImGui::End();
        return;
    }
    draw_minimize_button(TW_TEXTURE_PANEL);

    // Convenience buttons
    if (ImGui::Button("All Procedural")) {
        for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i)
            texture_mgr->render_modes[i] = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("All Textured")) {
        for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i)
            if (texture_mgr->has_texture[i])
                texture_mgr->render_modes[i] = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("All Blended")) {
        for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i)
            if (texture_mgr->has_texture[i])
                texture_mgr->render_modes[i] = 2;
    }

    ImGui::Separator();

    // Type list + per-type controls
    float avail = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("TypeList", ImVec2(0, avail), true);

    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        ImGui::PushID(static_cast<int>(t));

        ImVec4 col = PHYS_TYPE_UI_COLORS[t];
        bool has_tex = texture_mgr->has_texture[t];

        // Color swatch
        ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();

        // Expandable header
        bool open = ImGui::TreeNodeEx(PHYS_TYPE_NAMES[t], 0);

        // Show status on same line
        if (!open) {
            ImGui::SameLine();
            uint32_t mode = texture_mgr->render_modes[t];
            const char* mode_str = (mode == 0) ? "[Procedural]" :
                                   (mode == 1) ? "[Textured]" : "[Blended]";
            ImVec4 status_col = has_tex ? ImVec4(0.5f, 1.0f, 0.5f, 0.7f)
                                        : ImVec4(0.6f, 0.6f, 0.6f, 0.5f);
            ImGui::TextColored(status_col, "%s", mode_str);
        }

        if (open) {
            int mode = static_cast<int>(texture_mgr->render_modes[t]);
            ImGui::RadioButton("Procedural", &mode, 0);
            ImGui::SameLine();
            if (!has_tex) ImGui::BeginDisabled();
            ImGui::RadioButton("Textured", &mode, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Blended", &mode, 2);
            if (!has_tex) ImGui::EndDisabled();
            texture_mgr->render_modes[t] = static_cast<uint32_t>(mode);

            if (mode == 2) {
                float bf = texture_mgr->blend_factors[t];
                if (ImGui::SliderFloat("Blend", &bf, 0.0f, 1.0f, "%.2f"))
                    texture_mgr->blend_factors[t] = bf;
            }

            if (has_tex)
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Texture loaded");
            else
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 0.8f), "No texture (assets/particles/%s)",
                    ParticleTextureManager::type_to_filename(PHYS_TYPE_NAMES[t]).c_str());

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::End();
}

