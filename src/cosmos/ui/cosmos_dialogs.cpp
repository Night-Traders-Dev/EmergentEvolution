#include "cosmos/cosmos_app_internal.h"
#include "common/paths.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

void CosmosApp::draw_file_dialog() {
    bool any_dialog = show_save_dialog_ || show_load_dialog_ ||
                      show_export_dialog_ || show_import_dialog_;
    if (!any_dialog) return;

    const char* title = show_save_dialog_   ? "Save Simulation" :
                        show_load_dialog_   ? "Load Simulation" :
                        show_export_dialog_ ? "Export Body" :
                                              "Import Body";

    const char* extension = (show_save_dialog_ || show_load_dialog_) ? ".cssim" : ".csbody";

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 200, io.DisplaySize.y * 0.5f - 100),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("File path (%s):", extension);
        ImGui::InputText("##FilePath", file_path_buf_, sizeof(file_path_buf_));

        if (file_path_buf_[0] == '\0') {
            std::string def = get_data_dir();
            if (show_save_dialog_ || show_load_dialog_)
                def += "cosmos_save" + std::string(extension);
            else if (show_export_dialog_ && selected_body >= 0 &&
                     selected_body < (int)state.bodies.size())
                def += state.bodies[selected_body].name + extension;
            else
                def += "body" + std::string(extension);
            strncpy(file_path_buf_, def.c_str(), sizeof(file_path_buf_) - 1);
        }

        if (show_load_dialog_ || show_import_dialog_) {
            ImGui::Separator();
            ImGui::Text("Existing files:");
            std::error_code ec;
            std::string dir = get_data_dir();
            if (std::filesystem::exists(dir, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    std::string fn = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    if (ext == extension) {
                        if (ImGui::Selectable(fn.c_str())) {
                            strncpy(file_path_buf_, entry.path().string().c_str(),
                                    sizeof(file_path_buf_) - 1);
                        }
                    }
                }
            }
        }

        ImGui::Separator();

        const char* action_label = (show_save_dialog_ || show_export_dialog_) ? "Save" : "Load";
        if (ImGui::Button(action_label, ImVec2(120, 30))) {
            bool ok = false;
            if (show_save_dialog_) {
                ok = save_simulation(file_path_buf_);
                last_save_status_ = ok ? "Saved successfully" : "Save failed";
            } else if (show_load_dialog_) {
                ok = load_simulation(file_path_buf_);
                last_save_status_ = ok ? "Loaded successfully" : "Load failed";
            } else if (show_export_dialog_) {
                ok = export_body(selected_body, file_path_buf_);
                last_save_status_ = ok ? "Exported successfully" : "Export failed";
            } else if (show_import_dialog_) {
                ok = import_body(file_path_buf_);
                last_save_status_ = ok ? "Imported successfully" : "Import failed";
            }
            save_status_timer_ = 3.0f;
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
    }
    ImGui::End();

    if (!open) {
        show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
        file_path_buf_[0] = '\0';
    }
}

void CosmosApp::draw_debug_window() {
    if (!debug_window_visible_) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 440, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    if (!ImGui::Begin("Debug##CosmosDebug", &debug_window_visible_)) {
        ImGui::End();
        return;
    }

    // -- Performance --
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f (%.2f ms)", smoothed_fps_, 1000.0f / std::max(smoothed_fps_, 0.001f));
        ImGui::Text("Bodies: %d / %d", (int)state.bodies.size(), cfg.body_count);
        ImGui::Text("Sim Time: %.3f s", sim_time_);
        ImGui::Text("Time Rate: %.4f", displayed_time_rate_);
        ImGui::Text("Substeps: %d (required: %d)%s",
                     adaptive_substeps_last_, adaptive_substeps_required_,
                     adaptive_substeps_saturated_ ? " [SATURATED]" : "");
        ImGui::Text("Diagnostics Step: %llu", (unsigned long long)diagnostics_step_counter_);
    }

    // -- Physics Config --
    if (ImGui::CollapsingHeader("Physics Config")) {
        ImGui::Text("G: %.6e", cfg.G);
        ImGui::Text("DT Scale: %.4f", cfg.dt_scale);
        ImGui::Text("Substeps: %d (adaptive max: %d)", cfg.physics_substeps, cfg.adaptive_substep_max);
        ImGui::Text("Softening: %.4f", cfg.softening);
        ImGui::Text("Collision Restitution: %.3f", cfg.rigid_collision_restitution);
        ImGui::Separator();
        ImGui::Text("Collisions: %s", cfg.collisions ? "ON" : "OFF");
        ImGui::Text("Roche Limit: %s", cfg.roche_limit ? "ON" : "OFF");
        ImGui::Text("Tidal Heating Scale: %.2f", cfg.tidal_heating_scale);
        ImGui::Text("Tidal Locking: %s", cfg.tidal_locking ? "ON" : "OFF");
        ImGui::Text("Stellar Evolution: %s", cfg.stellar_evolution ? "ON" : "OFF");
        ImGui::Text("Hawking Radiation: %s", cfg.hawking_radiation ? "ON" : "OFF");
        ImGui::Text("Stellar Wind: %s", cfg.stellar_wind_pressure ? "ON" : "OFF");
        ImGui::Text("Spatial Hash: %s", cfg.spatial_hash_collisions ? "ON" : "OFF");
    }

    // -- Conservation --
    if (ImGui::CollapsingHeader("Conservation / Bookkeeping")) {
        double total_mass = 0.0, total_ke = 0.0;
        glm::dvec3 total_momentum(0.0);
        glm::dvec3 com(0.0);
        int active_count = 0;
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            double m = std::max((double)b.mass, 0.0);
            total_mass += m;
            glm::dvec3 v(b.vel);
            total_momentum += v * m;
            total_ke += 0.5 * m * glm::dot(v, v);
            com += glm::dvec3(b.pos) * m;
            active_count++;
        }
        if (total_mass > 1.0e-15) com /= total_mass;
        ImGui::Text("Active Bodies: %d", active_count);
        ImGui::Text("Total Mass: %.6e", total_mass);
        ImGui::Text("Escaped Mass: %.6e", escaped_mass_total_);
        ImGui::Text("Kinetic Energy: %.6e", total_ke);
        ImGui::Text("Radiated Energy: %.6e", radiated_energy_total_);
        ImGui::Text("Escaped Energy: %.6e", escaped_energy_total_);
        ImGui::Text("Momentum: (%.3e, %.3e, %.3e)",
                     total_momentum.x, total_momentum.y, total_momentum.z);
        ImGui::Text("|P|: %.6e", glm::length(total_momentum));
        ImGui::Text("Escaped |P|: %.6e", glm::length(escaped_momentum_total_));
        ImGui::Text("CoM: (%.2f, %.2f, %.2f)", com.x, com.y, com.z);
    }

    // -- Camera --
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::Text("Target: (%.2f, %.2f, %.2f)", camera.target.x, camera.target.y, camera.target.z);
        ImGui::Text("Distance: %.2f", camera.distance);
        ImGui::Text("Azimuth: %.1f  Elevation: %.1f", glm::degrees(camera.azimuth), glm::degrees(camera.elevation));
        ImGui::Text("Selected Body: %d", selected_body);
        if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
            const auto& b = state.bodies[selected_body];
            ImGui::Text("  Name: %s", b.name.c_str());
            ImGui::Text("  Pos: (%.2f, %.2f, %.2f)", b.pos.x, b.pos.y, b.pos.z);
            ImGui::Text("  Vel: (%.2f, %.2f, %.2f) |v|=%.4f", b.vel.x, b.vel.y, b.vel.z, glm::length(b.vel));
        }
    }

    // -- Body Type Census --
    if (ImGui::CollapsingHeader("Body Type Census")) {
        int type_counts[16] = {};
        const char* type_names[] = {
            "Star", "Planet", "Moon", "Asteroid", "Comet",
            "Dwarf Planet", "Nebula", "Black Hole", "White Dwarf",
            "Neutron Star", "Dust", "Gas Giant", "Ice Giant",
            "Brown Dwarf", "Protostar", "Ring Particle"
        };
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            int t = b.type;
            if (t >= 0 && t < 16) type_counts[t]++;
        }
        for (int i = 0; i < 16; i++) {
            if (type_counts[i] > 0) {
                ImGui::Text("  %s: %d", (i < 16 ? type_names[i] : "Unknown"), type_counts[i]);
            }
        }
    }

    // -- Render --
    if (ImGui::CollapsingHeader("Render")) {
        ImGui::Text("Swapchain: %dx%d", vk.swapchain_extent.width, vk.swapchain_extent.height);
        ImGui::Text("Display: %.0fx%.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("Paused: %s", paused ? "YES" : "NO");
        ImGui::Text("Reverse Time: %s", reverse_time_ ? "YES" : "NO");
    }

    // -- Diagnostics --
    if (ImGui::CollapsingHeader("Diagnostics")) {
        ImGui::Checkbox("Enable Runtime Diagnostics", &diagnostics_enabled_);
        ImGui::Checkbox("Pause on Invalid State", &diagnostics_pause_on_invalid_);
        if (ImGui::Button("Validate State Now")) {
            validate_body_state("manual_debug_check", diagnostics_pause_on_invalid_);
        }
        if (ImGui::Button("Log All Bodies")) {
            for (size_t i = 0; i < state.bodies.size(); i++) {
                const auto& b = state.bodies[i];
                if (b.marked_for_removal) continue;
                debug_logf("[%zu] %s type=%d mass=%.4e r=%.2f T=%.1f pos=(%.2f,%.2f,%.2f)",
                           i, b.name.c_str(), b.type, b.mass, b.radius, b.temperature,
                           b.pos.x, b.pos.y, b.pos.z);
            }
        }
    }

    ImGui::End();
}

// ── Duplicate Selected Body ─────────────────────────────────────────────────
void CosmosApp::duplicate_selected_body() {
    if (selected_body < 0 || selected_body >= (int)state.bodies.size()) return;
    const auto& src = state.bodies[selected_body];
    CelestialBody dup = src;
    // Offset position slightly so the duplicate doesn't overlap
    dup.pos += glm::vec3(src.radius * 2.5f, 0.0f, 0.0f);
    dup.marked_for_removal = false;
    dup.seed = src.seed + 7919u;
    dup.props_valid = false;
    dup.visuals_valid = false;
    state.bodies.push_back(dup);
    state.trails.push_back({});
    selected_body = (int)state.bodies.size() - 1;
    inspector_visible_ = true;
}

// ── Keyboard Shortcuts Overlay ──────────────────────────────────────────────
void CosmosApp::draw_shortcuts_overlay() {
    if (!shortcuts_visible_) return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));

    if (ImGui::Begin("Keyboard Shortcuts", &shortcuts_visible_,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoMove)) {
        auto row = [](const char* key, const char* desc) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%-16s", key);
            ImGui::SameLine(160);
            ImGui::Text("%s", desc);
        };
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Camera");
        ImGui::Separator();
        row("Left Drag", "Orbit camera");
        row("Right Drag", "Pan camera");
        row("Middle Drag", "Pan camera");
        row("Scroll", "Zoom (toward cursor)");
        row("R", "Reset camera");
        row("F", "Focus on selected body");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Simulation");
        ImGui::Separator();
        row("Space", "Pause / Resume");
        row("Escape", "Pause menu");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Bodies");
        ImGui::Separator();
        row("Click", "Select body");
        row("Double-Click", "Focus on body");
        row("Delete", "Delete selected body");
        row("Ctrl+D", "Duplicate selected body");
        row("L", "Lock/unlock selected body");
        row("V", "Toggle velocity arrows");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "UI");
        ImGui::Separator();
        row("F1", "Toggle this overlay");
        row("F12", "Take screenshot");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
