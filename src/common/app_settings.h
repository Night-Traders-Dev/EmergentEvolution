#pragma once
// ── Shared application settings (AAA-quality) ──────────────────────────────
// Common display, audio, accessibility, and performance settings shared
// across all EmergentEvolution simulators.

#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

struct AppSettings {
    // ── Display ──────────────────────────────────────────────────────────
    int   window_mode       = 0;     // 0=Borderless fullscreen, 1=Windowed
    int   window_w          = 0;     // Windowed mode width (0=auto)
    int   window_h          = 0;     // Windowed mode height (0=auto)
    bool  vsync             = true;  // VSync enabled
    int   fps_cap           = 0;     // FPS limit (0=uncapped, 30/60/120/144/240)
    int   render_scale      = 1;     // 1=native, 2/3/4 = supersampled
    bool  show_fps          = false; // Show FPS counter
    float ui_scale          = 1.0f;  // UI scale multiplier (0.8-1.5)
    int   preferred_monitor = 0;     // Monitor index for fullscreen
    int   preferred_gpu     = -1;    // GPU index (-1=auto)

    // ── Graphics Quality ────────────────────────────────────────────────
    int   quality_preset    = 2;     // 0=Low, 1=Medium, 2=High, 3=Ultra, 4=Custom
    bool  bloom_enabled     = true;  // Post-processing bloom
    int   shadow_quality    = 2;     // 0=Off, 1=Low, 2=Medium, 3=High
    int   antialiasing      = 1;     // 0=Off, 1=FXAA, 2=MSAA 2x, 3=MSAA 4x
    bool  motion_blur       = false; // Motion blur effect
    float gamma             = 1.0f;  // Display gamma (0.5-2.0)
    float brightness        = 1.0f;  // Display brightness (0.5-1.5)

    // ── Audio ────────────────────────────────────────────────────────────
    float master_volume     = 0.8f;  // Master volume (0-1)
    bool  master_muted      = false;
    float music_volume      = 0.5f;  // Background music (0-1)
    bool  music_muted       = false;
    float sfx_volume        = 0.7f;  // Sound effects (0-1)
    bool  sfx_muted         = false;
    float ambient_volume    = 0.6f;  // Ambient/environmental (0-1)
    bool  ambient_muted     = false;

    // ── Accessibility ───────────────────────────────────────────────────
    int   colorblind_mode   = 0;     // 0=Off, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia
    bool  high_contrast     = false; // High contrast UI
    bool  reduced_motion    = false; // Disable animations
    float mouse_sensitivity = 1.0f;  // Mouse speed multiplier (0.1-3.0)
    bool  screen_shake      = true;  // Screen shake on impacts
    float subtitle_size     = 1.0f;  // Subtitle/tooltip text size (0.8-1.5)

    // ── Performance ─────────────────────────────────────────────────────
    int   max_threads       = 0;     // Thread cap (0=auto)
    int   physics_quality   = 2;     // Physics detail (0=Low, 1=Medium, 2=High)
    int   autosave_interval = 5;     // Minutes between auto-saves (0=disabled)

    // ── Controls ────────────────────────────────────────────────────────
    bool  invert_y          = false; // Invert Y axis for camera
    float scroll_sensitivity = 1.0f; // Scroll wheel zoom speed (0.2-3.0)
    float orbit_sensitivity  = 1.0f; // Camera orbit speed (0.2-3.0)
    float pan_sensitivity    = 1.0f; // Camera pan speed (0.2-3.0)
};

// Persistence helpers
inline constexpr uint32_t APP_SETTINGS_MAGIC   = 0x47544541; // "AETG"
inline constexpr uint32_t APP_SETTINGS_VERSION  = 1;

inline void save_app_settings(const AppSettings& s, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    uint32_t magic = APP_SETTINGS_MAGIC;
    uint32_t ver   = APP_SETTINGS_VERSION;
    uint32_t sz    = (uint32_t)sizeof(AppSettings);
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&ver), 4);
    f.write(reinterpret_cast<const char*>(&sz), 4);
    f.write(reinterpret_cast<const char*>(&s), sizeof(s));
}

inline bool load_app_settings(AppSettings& s, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, ver = 0, sz = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&ver), 4);
    f.read(reinterpret_cast<char*>(&sz), 4);
    if (!f.good() || magic != APP_SETTINGS_MAGIC || ver > APP_SETTINGS_VERSION)
        return false;
    if (sz == 0 || sz > (1u << 20)) return false;
    size_t copy = std::min<size_t>(sz, sizeof(AppSettings));
    f.read(reinterpret_cast<char*>(&s), (std::streamsize)copy);
    return f.good();
}

// ── Shared settings menu rendering ──────────────────────────────────────────
// Call this inside a fullscreen ImGui overlay. Returns true if "Back" was pressed.
inline bool draw_app_settings_menu(AppSettings& settings, int& tab, float display_w, float display_h) {
    float cx = display_w * 0.5f;
    float cy = display_h * 0.5f;

    // Title
    float old_scale = ImGui::GetFont()->Scale;
    ImGui::GetFont()->Scale = 2.0f;
    ImGui::PushFont(ImGui::GetFont());
    const char* title = "SETTINGS";
    ImVec2 title_size = ImGui::CalcTextSize(title);
    ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 310.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "%s", title);
    ImGui::GetFont()->Scale = old_scale;
    ImGui::PopFont();

    // Tab bar
    float panel_w = 500.0f;
    float panel_x = cx - panel_w * 0.5f;
    float tab_top = cy - 260.0f;
    float panel_top = tab_top + 36.0f;
    float panel_bottom = cy + 260.0f;
    float panel_h = panel_bottom - panel_top;

    static const char* TAB_LABELS[] = {"Display", "Graphics", "Audio", "Access.", "Controls"};
    static constexpr int TAB_COUNT = 5;
    float tab_w = panel_w / TAB_COUNT;

    ImGui::SetCursorPos(ImVec2(panel_x, tab_top));

    ImVec4 accent(1.0f, 0.80f, 0.30f, 1.0f);

    for (int t = 0; t < TAB_COUNT; t++) {
        if (t > 0) ImGui::SameLine(0, 0);
        bool active = (tab == t);

        ImVec4 btn_bg  = active ? ImVec4(accent.x * 0.15f, accent.y * 0.15f, accent.z * 0.15f, 0.9f)
                                : ImVec4(0.06f, 0.07f, 0.10f, 0.7f);
        ImVec4 btn_hov = ImVec4(accent.x * 0.10f, accent.y * 0.10f, accent.z * 0.10f, 0.8f);

        ImGui::PushStyleColor(ImGuiCol_Button, btn_bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btn_hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btn_bg);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

        char label[32];
        snprintf(label, sizeof(label), "%s##stab%d", TAB_LABELS[t], t);
        if (ImGui::Button(label, ImVec2(tab_w, 30.0f)))
            tab = t;

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        if (active) {
            ImVec2 p = ImGui::GetItemRectMin();
            ImVec2 q = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, q.y - 2.0f), ImVec2(q.x, q.y),
                ImGui::ColorConvertFloat4ToU32(accent));
        }
    }

    // Content area
    ImGui::SetCursorPos(ImVec2(panel_x, panel_top));
    ImGui::BeginChild("##SettingsContent", ImVec2(panel_w, panel_h), false, ImGuiWindowFlags_NoBackground);
    ImGui::PushItemWidth(panel_w - 40.0f);

    tab = std::clamp(tab, 0, TAB_COUNT - 1);

    // ── Tab 0: Display ──────────────────────────────────────────────────
    if (tab == 0) {
        ImGui::Dummy(ImVec2(0, 6));

        const char* window_labels[] = {"Borderless Fullscreen", "Windowed"};
        ImGui::Combo("Window Mode", &settings.window_mode, window_labels, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Change takes effect on next launch\nAlt+Enter to toggle at any time");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("VSync", &settings.vsync);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Synchronize with display refresh rate\nReduces tearing but may limit FPS");

        ImGui::Dummy(ImVec2(0, 8));
        {
            const char* fps_labels[] = {"Uncapped", "30", "60", "120", "144", "240"};
            int fps_values[] = {0, 30, 60, 120, 144, 240};
            int fps_idx = 0;
            for (int i = 0; i < 6; i++)
                if (settings.fps_cap == fps_values[i]) { fps_idx = i; break; }
            if (ImGui::Combo("FPS Limit", &fps_idx, fps_labels, 6))
                settings.fps_cap = fps_values[fps_idx];
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cap frame rate to reduce GPU usage\n0 = uncapped (limited by VSync if on)");
        }

        ImGui::Dummy(ImVec2(0, 8));
        const char* render_labels[] = {"Native (1x)", "2x Supersampled", "3x Supersampled", "4x Supersampled"};
        int render_idx = std::clamp(settings.render_scale - 1, 0, 3);
        if (ImGui::Combo("Render Scale", &render_idx, render_labels, 4)) {
            settings.render_scale = render_idx + 1;
            settings.quality_preset = 4;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higher values = sharper image, more GPU usage");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("Show FPS Counter", &settings.show_fps);

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("UI Scale", &settings.ui_scale, 0.8f, 1.5f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale all UI elements (requires restart)");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("Brightness", &settings.brightness, 0.5f, 1.5f, "%.2f");
        ImGui::SliderFloat("Gamma", &settings.gamma, 0.5f, 2.0f, "%.2f");
    }

    // ── Tab 1: Graphics ─────────────────────────────────────────────────
    if (tab == 1) {
        ImGui::Dummy(ImVec2(0, 6));

        {
            const char* preset_labels[] = {"Low", "Medium", "High", "Ultra", "Custom"};
            int old_preset = settings.quality_preset;
            if (ImGui::Combo("Quality Preset", &settings.quality_preset, preset_labels, 5)) {
                if (settings.quality_preset != 4 && settings.quality_preset != old_preset) {
                    switch (settings.quality_preset) {
                        case 0: // Low
                            settings.render_scale = 1; settings.bloom_enabled = false;
                            settings.shadow_quality = 0; settings.antialiasing = 0;
                            settings.motion_blur = false; settings.physics_quality = 0;
                            break;
                        case 1: // Medium
                            settings.render_scale = 1; settings.bloom_enabled = false;
                            settings.shadow_quality = 1; settings.antialiasing = 1;
                            settings.motion_blur = false; settings.physics_quality = 1;
                            break;
                        case 2: // High
                            settings.render_scale = 1; settings.bloom_enabled = true;
                            settings.shadow_quality = 2; settings.antialiasing = 1;
                            settings.motion_blur = false; settings.physics_quality = 2;
                            break;
                        case 3: // Ultra
                            settings.render_scale = 2; settings.bloom_enabled = true;
                            settings.shadow_quality = 3; settings.antialiasing = 3;
                            settings.motion_blur = true; settings.physics_quality = 2;
                            break;
                    }
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SeparatorText("Effects");

        if (ImGui::Checkbox("Bloom Glow", &settings.bloom_enabled))
            settings.quality_preset = 4;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Post-processing glow around bright objects");

        {
            const char* shadow_labels[] = {"Off", "Low", "Medium", "High"};
            if (ImGui::Combo("Shadow Quality", &settings.shadow_quality, shadow_labels, 4))
                settings.quality_preset = 4;
        }

        {
            const char* aa_labels[] = {"Off", "FXAA", "MSAA 2x", "MSAA 4x"};
            if (ImGui::Combo("Anti-Aliasing", &settings.antialiasing, aa_labels, 4))
                settings.quality_preset = 4;
        }

        if (ImGui::Checkbox("Motion Blur", &settings.motion_blur))
            settings.quality_preset = 4;

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SeparatorText("Performance");

        {
            const char* phys_labels[] = {"Low", "Medium", "High"};
            if (ImGui::Combo("Physics Quality", &settings.physics_quality, phys_labels, 3))
                settings.quality_preset = 4;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Low: fewer calculations, best performance\nHigh: full physics fidelity");
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderInt("Thread Count", &settings.max_threads, 0, 32, settings.max_threads == 0 ? "Auto" : "%d");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = automatic (use all available cores)");

        ImGui::Dummy(ImVec2(0, 8));
        {
            const char* save_labels[] = {"Off", "1 min", "5 min", "10 min", "15 min", "30 min"};
            int save_values[] = {0, 1, 5, 10, 15, 30};
            int save_idx = 2;
            for (int i = 0; i < 6; i++)
                if (settings.autosave_interval == save_values[i]) { save_idx = i; break; }
            if (ImGui::Combo("Autosave", &save_idx, save_labels, 6))
                settings.autosave_interval = save_values[save_idx];
        }
    }

    // ── Tab 2: Audio ────────────────────────────────────────────────────
    if (tab == 2) {
        ImGui::Dummy(ImVec2(0, 6));

        auto volume_slider = [](const char* label, float* volume, bool* muted) {
            int pct = (int)(*volume * 100.0f + 0.5f);
            if (ImGui::SliderInt(label, &pct, 0, 100, "%d%%"))
                *volume = std::clamp(pct / 100.0f, 0.0f, 1.0f);
            ImGui::SameLine();
            char mute_id[64];
            snprintf(mute_id, sizeof(mute_id), "%s##mute_%s", *muted ? "X" : "O", label);
            if (ImGui::SmallButton(mute_id))
                *muted = !*muted;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(*muted ? "Unmute" : "Mute");
        };

        volume_slider("Master Volume",  &settings.master_volume,  &settings.master_muted);
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SeparatorText("Channels");
        volume_slider("Music",          &settings.music_volume,   &settings.music_muted);
        ImGui::Dummy(ImVec2(0, 4));
        volume_slider("Sound Effects",  &settings.sfx_volume,     &settings.sfx_muted);
        ImGui::Dummy(ImVec2(0, 4));
        volume_slider("Ambient",        &settings.ambient_volume, &settings.ambient_muted);
    }

    // ── Tab 3: Accessibility ────────────────────────────────────────────
    if (tab == 3) {
        ImGui::Dummy(ImVec2(0, 6));

        {
            const char* cb_labels[] = {"Off", "Protanopia (Red-weak)", "Deuteranopia (Green-weak)", "Tritanopia (Blue-weak)"};
            ImGui::Combo("Colorblind Mode", &settings.colorblind_mode, cb_labels, 4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Adjust colors for color vision deficiency");
        }

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("High Contrast UI", &settings.high_contrast);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Increase contrast for UI elements\nMakes text and borders more visible");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("Reduced Motion", &settings.reduced_motion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Disable UI animations and screen effects\nReduces visual stimulation");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("Screen Shake", &settings.screen_shake);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera shake on collisions and impacts\nDisable if this causes discomfort");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("Subtitle Size", &settings.subtitle_size, 0.8f, 1.5f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale tooltip and label text");
    }

    // ── Tab 4: Controls ─────────────────────────────────────────────────
    if (tab == 4) {
        ImGui::Dummy(ImVec2(0, 6));

        ImGui::SliderFloat("Mouse Sensitivity", &settings.mouse_sensitivity, 0.1f, 3.0f, "%.1f");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("Scroll Speed", &settings.scroll_sensitivity, 0.2f, 3.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Zoom speed when using scroll wheel");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("Orbit Speed", &settings.orbit_sensitivity, 0.2f, 3.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera rotation speed when dragging");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SliderFloat("Pan Speed", &settings.pan_sensitivity, 0.2f, 3.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera pan speed when middle-dragging");

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Checkbox("Invert Y Axis", &settings.invert_y);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Invert vertical camera rotation");

        ImGui::Dummy(ImVec2(0, 16));
        ImGui::SeparatorText("Controls Reference");
        ImGui::Dummy(ImVec2(0, 4));

        if (ImGui::BeginTable("##ControlsRef", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableHeadersRow();

            auto row = [](const char* action, const char* key) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(action);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(key);
            };

            row("Orbit Camera",    "Left Drag");
            row("Pan Camera",      "Middle Drag / Right Drag");
            row("Zoom",            "Scroll Wheel");
            row("Select Body",     "Left Click");
            row("Focus Body",      "Double Click");
            row("Spawn Body",      "Left Click (Spawn Mode)");
            row("Set Height",      "Hold + Drag Up/Down");
            row("Pause/Resume",    "Space");
            row("Toggle Pause Menu", "Escape");
            row("Delete Selected", "Delete");
            row("Focus Selected",  "F");
            row("Reset Camera",    "R");
            row("Screenshot",      "F12");
            row("Save",            "Ctrl+S");
            row("Load",            "Ctrl+L");

            ImGui::EndTable();
        }
    }

    ImGui::PopItemWidth();
    ImGui::EndChild();

    // Back button
    float back_y = panel_bottom + 20.0f;
    float back_w = 120.0f;
    ImGui::SetCursorPos(ImVec2(cx - back_w * 0.5f, back_y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.35f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.28f, 0.42f, 1.00f));
    bool back = ImGui::Button("Back", ImVec2(back_w, 36.0f));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    return back;
}
