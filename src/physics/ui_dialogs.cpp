#include "physics/interface.h"
#include "physics/phys_particles.h"
#include "physics/ui_data.h"
#include "physics/audio.h"
#include "vulkan_context.h"
#include "stb_image.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <chrono>
#ifdef HAS_OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;

// ── Menus & dialogs ──────────────────────────────────────────────────────────
// Split from interface.cpp: splash screen, pause menu, settings menu,
// achievements, save/load dialog, notifications, and thumbnail management.

static void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                              ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 28;
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
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 64);
    }
}

static void draw_vignette(ImDrawList* dl, float W, float H) {
    // Smooth edge vignette — darkens edges/corners, no hard boundary
    constexpr int BANDS = 32;
    float band_h = H / static_cast<float>(BANDS);
    for (int i = 0; i < BANDS; ++i) {
        float y_center = (static_cast<float>(i) + 0.5f) / static_cast<float>(BANDS);
        // Distance from vertical center (0 at middle, 1 at edges)
        float edge_dist = std::abs(y_center - 0.5f) * 2.0f;
        // Smooth cubic falloff: only darken near edges
        float vignette = edge_dist * edge_dist * edge_dist;
        // Stronger at top (0.7) and bottom (0.9)
        float strength = (y_center < 0.5f) ? 0.7f : 0.9f;
        int a = static_cast<int>(vignette * strength * 255.0f);
        if (a < 1) continue;
        dl->AddRectFilled(ImVec2(0, i * band_h), ImVec2(W, (i + 1) * band_h),
                          IM_COL32(2, 8, 16, std::min(a, 255)));
    }
}

// ── Splash particle initialization ───────────────────────────────────────────

void PhysicsInterface::init_splash_particles() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float ncx = W * 0.48f, ncy = H * 0.38f;

    splash_particles_.clear();
    splash_trails_.clear();
    splash_time_ = 0.0f;

    std::mt19937 rng(42);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto randf_range = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    // Particle type colors (matching banner.html)
    struct SplashType { ImU32 color; ImU32 glow; float r_min, r_max; };
    SplashType types[] = {
        { IM_COL32(0xff, 0x44, 0x33, 255), IM_COL32(0xff, 0x22, 0x00, 0x66), 4, 8 },   // proton
        { IM_COL32(0x44, 0x88, 0xcc, 255), IM_COL32(0x33, 0x66, 0xaa, 0x66), 4, 7 },   // neutron
        { IM_COL32(0x00, 0xdd, 0xff, 255), IM_COL32(0x00, 0xaa, 0xff, 0x44), 2, 4 },   // electron
        { IM_COL32(0xff, 0xcc, 0x00, 255), IM_COL32(0xff, 0xaa, 0x00, 0x33), 1.5f, 3 },// photon
        { IM_COL32(0x00, 0xff, 0xaa, 255), IM_COL32(0x00, 0xdd, 0x88, 0x44), 2, 4 },   // positron
        { IM_COL32(0xcc, 0x55, 0xff, 255), IM_COL32(0xaa, 0x33, 0xff, 0x44), 2.5f, 5 },// quark
        { IM_COL32(0xff, 0x66, 0x88, 255), IM_COL32(0xff, 0x44, 0x66, 0x44), 1.5f, 3 },// muon
        { IM_COL32(0x88, 0xff, 0x44, 255), IM_COL32(0x66, 0xdd, 0x22, 0x44), 2, 3 },   // gluon
    };

    // Nucleus cluster: 12 protons + 12 neutrons
    for (int i = 0; i < 24; ++i) {
        float angle = randf() * 6.2831853f;
        float dist = randf() * 22.0f * scale;
        auto& t = types[i < 12 ? 0 : 1];
        SplashParticle p{};
        p.x = ncx + cosf(angle) * dist;
        p.y = ncy + sinf(angle) * dist;
        p.vx = randf_range(-0.5f, 0.5f) * 0.15f;
        p.vy = randf_range(-0.5f, 0.5f) * 0.15f;
        p.r = randf_range(t.r_min, t.r_max) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = i < 12 ? 0 : 1;
        p.color = t.color;
        p.glow_color = t.glow;
        p.orbit = false;
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // 12 orbiting electrons
    for (int i = 0; i < 12; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (35.0f + i * 14.0f + randf() * 10.0f) * scale;
        p.orbit_speed = 0.006f + randf() * 0.008f;
        p.phase = (6.2831853f * i / 12.0f) + randf() * 0.5f;
        p.cx = ncx;
        p.cy = ncy;
        p.tilt_x = 0.4f + randf() * 0.6f;
        p.tilt_y = 0.6f + randf() * 0.5f;
        p.r = (2.5f + randf() * 1.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 2;
        p.color = IM_COL32(0x00, 0xdd, 0xff, 255);
        p.glow_color = IM_COL32(0x00, 0xaa, 0xff, 0x66);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // 15 inner corona particles (small, fast, warm-colored)
    for (int i = 0; i < 15; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (12.0f + randf() * 20.0f) * scale;
        p.orbit_speed = 0.015f + randf() * 0.012f;
        p.phase = randf() * 6.2831853f;
        p.cx = ncx;
        p.cy = ncy;
        p.tilt_x = 0.6f + randf() * 0.4f;
        p.tilt_y = 0.6f + randf() * 0.4f;
        p.r = (1.0f + randf() * 1.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 3;  // photon-like
        p.color = IM_COL32(255, 200, 100, 220);
        p.glow_color = IM_COL32(255, 150, 50, 100);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // 160 scattered ambient particles
    for (int i = 0; i < 160; ++i) {
        int ti = (int)(randf() * 8.0f);
        if (ti > 7) ti = 7;
        auto& t = types[ti];
        SplashParticle p{};
        p.x = randf() * W;
        p.y = randf() * H;
        p.vx = randf_range(-0.5f, 0.5f) * 0.8f;
        p.vy = randf_range(-0.5f, 0.5f) * 0.8f;
        p.r = randf_range(t.r_min, t.r_max) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = ti;
        p.color = t.color;
        p.glow_color = t.glow;
        p.orbit = false;
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    splash_trails_.resize(splash_particles_.size());
}

// ── Animated Splash Screen ───────────────────────────────────────────────────

void PhysicsInterface::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();

    // Check for dismiss: any mouse button or key (skip first 0.3s to avoid accidental dismiss)
    if (splash_time_ > 0.3f) {
        bool dismiss = ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                    || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!dismiss) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
                    dismiss = true;
                    break;
                }
            }
        }
        if (dismiss) { show_splash = false; return; }
    }

    // Init particles on first call (or re-init on About)
    if (!splash_inited_) { init_splash_particles(); splash_inited_ = true; }

    float dt = io.DeltaTime;
    splash_time_ += dt;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float ncx = W * 0.48f, ncy = H * 0.38f;

    // 1. Background fill
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 8, 16, 255));

    // 2. Multi-layer nebula glow
    draw_radial_glow(bg, W * 0.3f, H * 0.3f, 350.0f * scale,
                     IM_COL32(10, 20, 60, 80), IM_COL32(10, 20, 60, 0));
    draw_radial_glow(bg, W * 0.65f, H * 0.55f, 250.0f * scale,
                     IM_COL32(30, 10, 50, 50), IM_COL32(30, 10, 50, 0));
    draw_radial_glow(bg, W * 0.5f, H * 0.2f, 200.0f * scale,
                     IM_COL32(10, 40, 50, 40), IM_COL32(10, 40, 50, 0));

    // 3. Energy core glow around nucleus
    float core_r = (35.0f + sinf(splash_time_ * 2.0f) * 5.0f) * scale;
    draw_radial_glow(bg, ncx, ncy, core_r * 3.0f,
                     IM_COL32(255, 120, 60, 38), IM_COL32(255, 60, 30, 0));
    draw_radial_glow(bg, ncx, ncy, 160.0f * scale,
                     IM_COL32(0, 100, 255, 10), IM_COL32(0, 100, 255, 0));

    // 4. Update particles (with radius pulsing)
    for (auto& p : splash_particles_) {
        p.r = p.base_r * (1.0f + 0.15f * sinf(splash_time_ * 2.5f + p.pulse_phase));
        if (p.orbit) {
            p.phase += p.orbit_speed * dt * 60.0f;
            p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
            p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        } else {
            p.x += p.vx * dt * 60.0f;
            p.y += p.vy * dt * 60.0f;
            if (p.x < -10) p.x = W + 10;
            if (p.x > W + 10) p.x = -10;
            if (p.y < -10) p.y = H + 10;
            if (p.y > H + 10) p.y = -10;
        }
    }

    // 5. Update trails (longer for HD)
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        splash_trails_[i].push_back(ImVec2(splash_particles_[i].x, splash_particles_[i].y));
        if (splash_trails_[i].size() > 16) splash_trails_[i].erase(splash_trails_[i].begin());
    }

    // 6. Draw trails with tapering width
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& trail = splash_trails_[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float t = (float)j / (float)trail.size();
            float alpha = t * 0.45f;
            float width = splash_particles_[i].r * (0.15f + 0.55f * t);
            ImU32 col = (splash_particles_[i].color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j-1], trail[j], col, width);
        }
    }

    // 7. Draw force lines between nearby particles (wider + glow)
    float force_dist = 80.0f * scale;
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        for (size_t j = i + 1; j < splash_particles_.size(); ++j) {
            float dx = splash_particles_[j].x - splash_particles_[i].x;
            float dy = splash_particles_[j].y - splash_particles_[i].y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < force_dist * force_dist && dist2 > 25.0f) {
                float dist = sqrtf(dist2);
                float alpha = (1.0f - dist / force_dist) * 0.15f;
                ImVec2 a(splash_particles_[i].x, splash_particles_[i].y);
                ImVec2 b(splash_particles_[j].x, splash_particles_[j].y);
                // Outer glow (wider, dimmer)
                bg->AddLine(a, b, IM_COL32(0, 120, 255, (int)(alpha * 80)), 4.0f);
                // Core line
                bg->AddLine(a, b, IM_COL32(0, 180, 255, (int)(alpha * 255)), 1.5f);
            }
        }
    }

    // 8. Draw particles (glow + core + highlight)
    for (auto& p : splash_particles_) {
        draw_radial_glow(bg, p.x, p.y, p.r * 4.0f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
        bg->AddCircleFilled(ImVec2(p.x - p.r * 0.2f, p.y - p.r * 0.2f),
                            p.r * 0.5f, IM_COL32(255, 255, 255, 80));
    }

    // 9. Vignette overlay
    draw_vignette(bg, W, H);

    // 10. Scanlines
    float scanline_gap = 4.0f * scale;
    if (scanline_gap < 2.0f) scanline_gap = 2.0f;
    for (float y = 0; y < H; y += scanline_gap)
        bg->AddRectFilled(ImVec2(0, y + scanline_gap * 0.5f), ImVec2(W, y + scanline_gap),
                          IM_COL32(0, 0, 0, 8));

    // 11. Text overlay — transparent fullscreen ImGui window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##Splash", nullptr, flags)) {
        float title_y = H - 80.0f * scale;
        float left_margin = 30.0f * scale;

        // Accent line
        bg->AddLine(ImVec2(left_margin, title_y - 8.0f * scale),
                    ImVec2(left_margin + 180.0f * scale, title_y - 8.0f * scale),
                    IM_COL32(0, 200, 255, 153), 1.0f);

        // Title "Particle Playground" — large text with glow shadow
        float old_scale = ImGui::GetFont()->Scale;
        float title_font_scale = 2.2f * scale;
        if (title_font_scale < 1.5f) title_font_scale = 1.5f;
        ImGui::GetFont()->Scale = title_font_scale;
        ImGui::PushFont(ImGui::GetFont());

        // Glow shadow layers (drawn behind, progressively offset + dimmer)
        for (int g = 3; g >= 1; --g) {
            float ga = 0.12f / (float)g;
            float off = (float)g * 1.5f;
            ImGui::SetCursorPos(ImVec2(left_margin + off, title_y + off));
            ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.8f, ga), "Particle ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImVec4(0.0f, 0.4f, 0.8f, ga), "Playground");
        }

        // "Particle " in white
        ImGui::SetCursorPos(ImVec2(left_margin, title_y));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Particle ");
        ImGui::SameLine(0, 0);
        // "Playground" in cyan
        ImGui::TextColored(ImVec4(0.0f, 0.83f, 1.0f, 1.0f), "Playground");

        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Subtitle
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.0f, 0.78f, 1.0f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types");

        // Top-right badge "QUANTUM PHYSICS SANDBOX"
        {
            const char* badge = "QUANTUM PHYSICS SANDBOX";
            ImVec2 badge_sz = ImGui::CalcTextSize(badge);
            float badge_x = W - badge_sz.x - 18.0f * scale;
            float badge_y = 14.0f * scale;
            // Badge background
            bg->AddRectFilled(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                              ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                              IM_COL32(255, 60, 30, 20));
            bg->AddRect(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                        ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                        IM_COL32(255, 100, 60, 77), 2.0f);
            ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
            ImGui::TextColored(ImVec4(1.0f, 0.39f, 0.24f, 0.9f), "%s", badge);
        }

        // Bottom-center dismiss hint (pulsing alpha)
        float pulse = 0.3f + 0.2f * sinf(splash_time_ * 3.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 30.0f * scale));
        ImGui::TextColored(ImVec4(0.45f, 0.48f, 0.58f, pulse), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Pause Menu (Escape key) ─────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_pause_menu(SimConfig& /*cfg*/, bool& request_reset) {
    ImGuiIO& io = ImGui::GetIO();

    // Semi-transparent fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##PauseOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 140.0f));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Menu buttons (centered column)
        float btn_w = 200.0f;
        float btn_h = 40.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;
        float btn_spacing = 52.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));

        // Resume
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            sim_running = true;
        }

        // New
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            request_reset = true;
        }

        // Save
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Save", ImVec2(btn_w, btn_h))) {
            show_save_dialog = true;
            show_load_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // Load
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Load", ImVec2(btn_w, btn_h))) {
            show_load_dialog = true;
            show_save_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // About
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("About", ImVec2(btn_w, btn_h))) {
            show_splash = true;
            splash_inited_ = false;  // re-init animation
            show_pause_menu = false;
        }

        // Achievements
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Achievements", ImVec2(btn_w, btn_h))) {
            show_achievements_panel = true;
            show_pause_menu = false;
        }

        // Settings
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
        if (ImGui::Button("Settings", ImVec2(btn_w, btn_h))) {
            show_settings_menu = true;
            show_pause_menu = false;
        }

        // Quit
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 7));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 0.95f));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // Hint text
        float hint_y = btn_y + btn_spacing * 8 + 10.0f;
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, hint_y));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Settings Menu ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_settings_menu() {
    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.88f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##SettingsOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;
        const auto& tc = get_theme(std::clamp(prefs.theme, 0, total_theme_count() - 1));

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "SETTINGS";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, cy - 310.0f));
        ImGui::TextColored(tc.accent, "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // ── Tab bar ──────────────────────────────────────────────────────
        float panel_w = 460.0f;
        float panel_x = cx - panel_w * 0.5f;
        float tab_top = cy - 260.0f;
        float panel_top = tab_top + 36.0f;
        float panel_bottom = cy + 230.0f;
        float panel_h = panel_bottom - panel_top;

        static const char* TAB_LABELS[] = { "Display", "Performance", "Theme", "Audio & Log" };
        static constexpr int TAB_COUNT = 4;
        float tab_w = panel_w / TAB_COUNT;

        ImGui::SetCursorPos(ImVec2(panel_x, tab_top));

        // Draw tab buttons
        for (int t = 0; t < TAB_COUNT; t++) {
            if (t > 0) ImGui::SameLine(0, 0);
            bool active = (settings_tab == t);

            ImVec4 btn_bg    = active ? ImVec4(tc.accent.x * 0.15f, tc.accent.y * 0.15f, tc.accent.z * 0.15f, 0.9f)
                                      : ImVec4(0.06f, 0.07f, 0.10f, 0.7f);
            ImVec4 btn_hover = ImVec4(tc.accent.x * 0.10f, tc.accent.y * 0.10f, tc.accent.z * 0.10f, 0.8f);

            ImGui::PushStyleColor(ImGuiCol_Button, btn_bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btn_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, btn_bg);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

            if (ImGui::Button(TAB_LABELS[t], ImVec2(tab_w, 30.0f)))
                settings_tab = t;

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            // Active tab underline
            if (active) {
                ImVec2 p = ImGui::GetItemRectMin();
                ImVec2 q = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(p.x, q.y - 2.0f), ImVec2(q.x, q.y),
                    ImGui::ColorConvertFloat4ToU32(tc.accent));
            }
        }

        // ── Tab content area ─────────────────────────────────────────────
        ImGui::SetCursorPos(ImVec2(panel_x, panel_top));
        ImGui::BeginChild("##SettingsContent", ImVec2(panel_w, panel_h), false, ImGuiWindowFlags_NoBackground);
        ImGui::PushItemWidth(panel_w - 40.0f);

        settings_tab = std::clamp(settings_tab, 0, TAB_COUNT - 1);

        // ── Tab 0: Display ───────────────────────────────────────────────
        if (settings_tab == 0) {
            ImGui::Dummy(ImVec2(0, 6));

            ImGui::Text("Temperature Units");
            ImGui::RadioButton("Kelvin",     &prefs.temp_unit, 0); ImGui::SameLine();
            ImGui::RadioButton("Celsius",    &prefs.temp_unit, 1); ImGui::SameLine();
            ImGui::RadioButton("Fahrenheit", &prefs.temp_unit, 2);

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Checkbox("Show FPS", &prefs.show_fps);

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::SliderFloat("UI Scale", &prefs.ui_scale, 0.8f, 1.5f, "%.1fx");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scale all UI elements (requires restart)");

            ImGui::Dummy(ImVec2(0, 8));
            const char* render_labels[] = { "Native (1x)", "Supersampled (2x)", "Supersampled (3x)", "Supersampled (4x)" };
            int render_idx = std::clamp(prefs.render_scale - 1, 0, 3);
            if (ImGui::Combo("Render Quality", &render_idx, render_labels, 4))
                prefs.render_scale = render_idx + 1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Native: render at base resolution\n2x/3x/4x: higher resolution, downsampled\nHigher quality but uses more GPU memory");

            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Visual Overlays");
            {
                bool show_bonds = !hide_bond_visuals;
                if (ImGui::Checkbox("Show Bond Lines", &show_bonds))
                    hide_bond_visuals = !show_bonds;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle covalent bond line rendering\nPhysics (spring forces) still active when hidden");

                bool show_virtual = !hide_virtual_trails;
                if (ImGui::Checkbox("Show Virtual Particles", &show_virtual))
                    hide_virtual_trails = !show_virtual;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle virtual particle rendering\nCasimir forces still active when hidden");

                bool show_entangle = !hide_entanglement_lines;
                if (ImGui::Checkbox("Show Entanglement Lines", &show_entangle))
                    hide_entanglement_lines = !show_entangle;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle entanglement dashed line overlay\nVelocity coupling still active when hidden");
            }
        }

        // ── Tab 1: Performance ───────────────────────────────────────────
        else if (settings_tab == 1) {
            ImGui::Dummy(ImVec2(0, 6));

#ifdef HAS_OPENMP
            int sys_max = omp_get_max_threads();
            if (prefs.max_threads <= 0) prefs.max_threads = sys_max;
            prefs.max_threads = std::clamp(prefs.max_threads, 1, sys_max);
            ImGui::SliderInt("CPU Threads", &prefs.max_threads, 1, sys_max);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("OpenMP thread count for physics\nSystem max: %d", sys_max);
#else
            ImGui::TextDisabled("CPU Threads: single-threaded (no OpenMP)");
#endif

            ImGui::Dummy(ImVec2(0, 8));
            const char* fps_labels[] = { "Uncapped", "30", "60", "120", "144", "240" };
            const int   fps_values[] = { 0, 30, 60, 120, 144, 240 };
            int fps_idx = 0;
            for (int i = 0; i < 6; i++) {
                if (fps_values[i] == prefs.fps_cap) { fps_idx = i; break; }
            }
            if (ImGui::Combo("FPS Cap", &fps_idx, fps_labels, 6))
                prefs.fps_cap = fps_values[fps_idx];

            ImGui::Dummy(ImVec2(0, 8));
            const char* quality_labels[] = { "Low", "Medium", "High" };
            ImGui::Combo("Physics Quality", &prefs.physics_quality, quality_labels, 3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Low: essential physics, skip expensive checks\nMedium: most interactions at reduced rate\nHigh: full simulation fidelity every frame");

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::SliderInt("Physics Skip", &prefs.physics_skip, 0, 4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Run CPU physics every (N+1) frames\n0 = every frame (best quality)\nHigher = better FPS, less accurate");

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Checkbox("Spatial Grid", &prefs.spatial_grid);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use spatial acceleration grid for neighbor searches\nGreatly improves CPU physics performance\nDisable only for debugging");
        }

        // ── Tab 2: Theme ─────────────────────────────────────────────────
        else if (settings_tab == 2) {
            ImGui::Dummy(ImVec2(0, 6));

            int count = total_theme_count();
            float swatch_sz = 14.0f;
            float row_h = 26.0f;
            float swatch_total = swatch_sz * 3 + 8.0f;

            for (int i = 0; i < count; i++) {
                const auto& th = get_theme(i);
                const char* name = get_theme_name(i);
                bool selected = (prefs.theme == i);

                ImGui::PushID(i);

                ImVec2 row_pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();

                if (ImGui::Selectable("##t", selected, 0, ImVec2(panel_w - 20.0f, row_h)))
                    prefs.theme = i;

                float sx = row_pos.x + 6.0f;
                float sy = row_pos.y + (row_h - swatch_sz) * 0.5f;
                dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + swatch_sz, sy + swatch_sz),
                    ImGui::ColorConvertFloat4ToU32(th.bg));
                dl->AddRectFilled(ImVec2(sx + swatch_sz + 2, sy), ImVec2(sx + swatch_sz * 2 + 2, sy + swatch_sz),
                    ImGui::ColorConvertFloat4ToU32(th.accent));
                dl->AddRectFilled(ImVec2(sx + swatch_sz * 2 + 4, sy), ImVec2(sx + swatch_sz * 3 + 4, sy + swatch_sz),
                    ImGui::ColorConvertFloat4ToU32(th.frame));

                ImVec4 label_col = selected ? th.accent : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                float tx = row_pos.x + swatch_total + 12.0f;
                float ty = row_pos.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddText(ImVec2(tx, ty), ImGui::ColorConvertFloat4ToU32(label_col), name);

                if (i >= BUILTIN_THEME_COUNT) {
                    float name_w = ImGui::CalcTextSize(name).x;
                    dl->AddText(ImVec2(tx + name_w + 6.0f, ty),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 0.7f)), "(imported)");
                }

                ImGui::PopID();
            }

            ImGui::Spacing();

            if (ImGui::Button("Import Theme (.pptheme)", ImVec2(panel_w - 20.0f, 0))) {
                scan_theme_directory();
                char msg[128];
                snprintf(msg, sizeof(msg), "Scanned themes/ — found %d custom theme(s)", custom_theme_count());
                push_notification(msg, tc.accent);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Place .pptheme files in the themes/ directory.\nClick to scan and load them.\n\nFormat (one field per line):\n  name=My Theme\n  bg=0.05,0.05,0.08,0.75\n  accent=0.3,0.7,0.9,1.0\n  ...");
        }

        // ── Tab 3: Audio & Log ───────────────────────────────────────────
        else if (settings_tab == 3) {
            ImGui::Dummy(ImVec2(0, 6));

            ImGui::TextColored(tc.accent, "Music");
            ImGui::Separator();

            if (ImGui::Checkbox("Mute Music", &prefs.music_muted)) {
                if (audio_ptr) {
                    if (prefs.music_muted) audio_ptr->pause();
                    else                   audio_ptr->resume();
                }
            }

            if (!prefs.music_muted) {
                float vol_pct = prefs.music_volume * 100.0f;
                if (ImGui::SliderFloat("Music Volume", &vol_pct, 0.0f, 100.0f, "%.0f%%")) {
                    prefs.music_volume = vol_pct / 100.0f;
                    if (audio_ptr) audio_ptr->set_volume(prefs.music_volume);
                }
            }

            ImGui::Dummy(ImVec2(0, 14));

            ImGui::TextColored(tc.accent, "Event Log");
            ImGui::Separator();

            ImGui::Checkbox("Limit to 10,000 entries", &prefs.event_log_limit);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("When enabled, the event log keeps the most recent 10,000 entries.\nDisable for unlimited history (uses more memory).");

            ImGui::Dummy(ImVec2(0, 4));
            ImGui::Checkbox("Save events to disk", &prefs.event_log_save);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Append all physics events to saves/event_log.txt\nas they occur (timestamped, one per line).");
        }

        ImGui::PopItemWidth();
        ImGui::EndChild();

        // ── Back button ──────────────────────────────────────────────────
        float btn_w = 160.0f;
        float btn_h = 36.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, panel_bottom + 15.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("Back", ImVec2(btn_w, btn_h))) {
            show_settings_menu = false;
            show_pause_menu = true;
            save_prefs();
        }
        ImGui::PopStyleVar();

        // Hint
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, panel_bottom + 60.0f));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Achievements Panel ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_achievements_panel() {
    if (!achievements_ptr) return;

    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##AchievementsOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 2.0f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "ACHIEVEMENTS";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, 30.0f));
        ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Progress summary
        int unlocked = achievements_ptr->unlocked_count();
        char progress_buf[64];
        snprintf(progress_buf, sizeof(progress_buf), "%d / %d Unlocked", unlocked, ACH_COUNT);
        ImVec2 prog_size = ImGui::CalcTextSize(progress_buf);
        ImGui::SetCursorPos(ImVec2(cx - prog_size.x * 0.5f, 72.0f));
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "%s", progress_buf);

        // Progress bar
        float bar_w = 400.0f;
        ImGui::SetCursorPos(ImVec2(cx - bar_w * 0.5f, 95.0f));
        float fraction = (ACH_COUNT > 0) ? static_cast<float>(unlocked) / static_cast<float>(ACH_COUNT) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.843f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
        ImGui::PushItemWidth(bar_w);
        ImGui::ProgressBar(fraction, ImVec2(bar_w, 16.0f), "");
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);

        // Scrollable content area with category tabs
        float content_top = 125.0f;
        float content_bottom = io.DisplaySize.y - 70.0f;
        float panel_w = std::min(700.0f, io.DisplaySize.x - 80.0f);

        ImGui::SetCursorPos(ImVec2(cx - panel_w * 0.5f, content_top));
        ImGui::BeginChild("##AchContent", ImVec2(panel_w, content_bottom - content_top), ImGuiChildFlags_Border);

        for (int cat = 0; cat < ACAT_COUNT; cat++) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.18f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.24f, 0.35f, 1.0f));

            // Count unlocked in this category
            int cat_total = 0, cat_unlocked = 0;
            for (uint32_t i = 0; i < ACH_COUNT; i++) {
                if (ACHIEVEMENT_DEFS[i].category == cat) {
                    cat_total++;
                    if (achievements_ptr->is_unlocked(static_cast<AchievementID>(i)))
                        cat_unlocked++;
                }
            }

            char cat_label[128];
            snprintf(cat_label, sizeof(cat_label), "%s  (%d/%d)",
                     ACHIEVEMENT_CATEGORY_NAMES[cat], cat_unlocked, cat_total);

            bool open = ImGui::CollapsingHeader(cat_label, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(2);

            if (open) {
                for (uint32_t i = 0; i < ACH_COUNT; i++) {
                    const auto& def = ACHIEVEMENT_DEFS[i];
                    if (def.category != cat) continue;

                    bool done = achievements_ptr->is_unlocked(def.id);

                    // Achievement row
                    ImGui::PushID(i);
                    float row_h = 44.0f;
                    ImVec2 cursor = ImGui::GetCursorPos();

                    // Background highlight for unlocked
                    if (done) {
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImVec2 p1 = ImVec2(p0.x + panel_w - 20.0f, p0.y + row_h);
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            p0, p1, IM_COL32(40, 60, 30, 100), 4.0f);
                    }

                    // Icon
                    ImGui::SetCursorPos(ImVec2(cursor.x + 8.0f, cursor.y + 4.0f));
                    if (done) {
                        ImGui::TextColored(ImVec4(1.0f, 0.843f, 0.0f, 1.0f), "%s", def.icon);
                    } else {
                        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "[?]");
                    }

                    // Name
                    ImGui::SameLine(60.0f);
                    ImGui::SetCursorPosY(cursor.y + 4.0f);
                    if (done) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", def.name);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%s", def.name);
                    }

                    // Description
                    ImGui::SetCursorPos(ImVec2(cursor.x + 60.0f, cursor.y + 22.0f));
                    if (done) {
                        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.6f, 1.0f), "%s", def.description);
                    } else {
                        ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.4f, 1.0f), "%s", def.description);
                    }

                    ImGui::SetCursorPosY(cursor.y + row_h);
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }

        ImGui::EndChild();

        // Back button
        float btn_w = 160.0f;
        float btn_h = 36.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, content_bottom + 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));
        if (ImGui::Button("Back", ImVec2(btn_w, btn_h))) {
            show_achievements_panel = false;
            show_pause_menu = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Notifications ───────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::push_notification(const char* text, ImVec4 color) {
    if (static_cast<int>(notifications.size()) >= NOTIFY_MAX)
        notifications.erase(notifications.begin());  // drop oldest
    notifications.push_back({std::string(text), color, NOTIFY_DURATION});
}

void PhysicsInterface::push_decay_event(const char* desc, DecayEventType type, ImVec4 color) {
    push_decay_event(desc, type, color, std::string());
}

void PhysicsInterface::push_decay_event(const char* desc, DecayEventType type, ImVec4 color, const std::string& details) {
    if (prefs.event_log_limit && static_cast<int>(decay_log.size()) >= DECAY_LOG_MAX) {
        decay_log.erase(decay_log.begin());
        if (expanded_event_idx > 0) expanded_event_idx--;
        else if (expanded_event_idx == 0) expanded_event_idx = -1;
    }
    decay_log.push_back({std::string(desc), details, type, color, frame_counter_display, std::time(nullptr)});
    if (prefs.event_log_save) save_event_to_disk(desc, type);
}

void PhysicsInterface::save_event_to_disk(const char* desc, DecayEventType type) {
    namespace fs = std::filesystem;
    static const char* TYPE_TAGS[] = {
        "DECAY", "NUCLEAR", "FUSION", "FISSION", "ANNIHILATION",
        "PHOTOELECTRIC", "SPALLATION", "PAIR_PROD", "PION_PROD",
        "VMD", "PHOTODISINT", "BOND_FORM", "BOND_BREAK"
    };
    std::error_code ec;
    fs::create_directories("saves", ec);
    std::ofstream f("saves/event_log.txt", std::ios::app);
    if (!f.is_open()) return;
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    const char* tag = (static_cast<int>(type) < DEVT_COUNT) ? TYPE_TAGS[static_cast<int>(type)] : "UNKNOWN";
    f << "[" << ts << "] [" << tag << "] " << desc << "\n";
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Notification Toast Drawing ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_notifications() {
    if (notifications.empty()) return;
    // Don't draw toasts when the event log is already visible
    if (show_decay_log) {
        notifications.clear();
        return;
    }

    float dt = ImGui::GetIO().DeltaTime;
    ImGuiIO& io = ImGui::GetIO();

    // Tick timers and remove expired
    for (auto& n : notifications) n.timer -= dt;
    notifications.erase(
        std::remove_if(notifications.begin(), notifications.end(),
                        [](const Notification& n) { return n.timer <= 0.0f; }),
        notifications.end());

    if (notifications.empty()) return;

    // Draw stacked cards from top-right, growing downward
    float card_w = 260.0f;
    float card_pad = 4.0f;
    float start_x = io.DisplaySize.x - card_w - 10.0f;
    float start_y = 10.0f;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    for (int i = 0; i < static_cast<int>(notifications.size()); ++i) {
        auto& n = notifications[i];
        float alpha = (n.timer < 1.0f) ? n.timer : 1.0f;  // fade out in last second

        ImGui::SetNextWindowPos(ImVec2(start_x, start_y));
        ImGui::SetNextWindowSize(ImVec2(card_w, 0));
        ImGui::SetNextWindowBgAlpha(0.85f * alpha);

        char wid[32];
        snprintf(wid, sizeof(wid), "##notify%d", i);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.12f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(n.color.x * 0.5f, n.color.y * 0.5f, n.color.z * 0.5f, alpha * 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        if (ImGui::Begin(wid, nullptr, flags)) {
            ImVec4 tc = n.color;
            tc.w = alpha;
            ImGui::TextColored(tc, "%s", n.text.c_str());
            start_y += ImGui::GetWindowHeight() + card_pad;
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Thumbnail Management ────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::free_thumbnails() {
    if (!vk_ctx_) return;
    vkDeviceWaitIdle(vk_ctx_->device);
    for (auto& te : thumbnail_entries_) {
        if (te.imgui_tex) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(te.imgui_tex));
            te.imgui_tex = nullptr;
        }
        if (te.thumb_view != VK_NULL_HANDLE) {
            vkDestroyImageView(vk_ctx_->device, te.thumb_view, nullptr);
            te.thumb_view = VK_NULL_HANDLE;
        }
        if (te.thumb_image != VK_NULL_HANDLE) {
            vkDestroyImage(vk_ctx_->device, te.thumb_image, nullptr);
            te.thumb_image = VK_NULL_HANDLE;
        }
        if (te.thumb_memory != VK_NULL_HANDLE) {
            vkFreeMemory(vk_ctx_->device, te.thumb_memory, nullptr);
            te.thumb_memory = VK_NULL_HANDLE;
        }
    }
    thumbnail_entries_.clear();
    thumbnails_dirty_ = true;
}

void PhysicsInterface::load_thumbnails() {
    free_thumbnails();

    if (!vk_ctx_ || browse_current_dir.empty()) return;

    // Create sampler if not yet created
    if (thumb_sampler_ == VK_NULL_HANDLE) {
        thumb_sampler_ = vk_ctx_->create_sampler_nearest();
    }

    std::error_code ec;
    for (auto& entry : fs::directory_iterator(browse_current_dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".ppsg") continue;

        ThumbnailEntry te;
        te.filepath = entry.path().string();
        te.name = entry.path().stem().string();
        te.size = entry.file_size(ec);

        // Get modification time as string
        {
            auto ftime = entry.last_write_time(ec);
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
            auto sctp = std::chrono::file_clock::to_sys(ftime);
            auto tt = std::chrono::system_clock::to_time_t(sctp);
#else
            // Fallback: use file_time_type epoch offset
            auto dur = ftime.time_since_epoch();
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(dur).count();
            // file_time_type epoch differs from system clock; approximate
            std::time_t tt = static_cast<std::time_t>(sec);
            // Adjust for NTFS/ext4 epoch difference if needed
            // Most Linux implementations use the same epoch
#endif
            char timebuf[64];
            struct tm* tm_info = std::localtime(&tt);
            if (tm_info)
                std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M", tm_info);
            else
                snprintf(timebuf, sizeof(timebuf), "Unknown");
            te.date_str = timebuf;
        }

        // Try to load thumbnail PNG
        std::string thumb_path = te.filepath + ".thumb.png";
        int tw, th, channels;
        unsigned char* pixels = stbi_load(thumb_path.c_str(), &tw, &th, &channels, 4);
        if (pixels && tw > 0 && th > 0) {
            VkDeviceSize img_size = static_cast<VkDeviceSize>(tw) * th * 4;

            // Create staging buffer
            Buffer staging = vk_ctx_->create_buffer(img_size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vk_ctx_->update_buffer(staging, pixels, img_size);

            // Create device image
            Image img = vk_ctx_->create_image(
                static_cast<uint32_t>(tw), static_cast<uint32_t>(th),
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            img.view = vk_ctx_->create_image_view(img.handle,
                VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

            // Transition to TRANSFER_DST, copy, transition to SHADER_READ
            vk_ctx_->transition_image_layout(img.handle,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkCommandBuffer cmd = vk_ctx_->begin_single_command();
            VkBufferImageCopy region{};
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1 };
            vkCmdCopyBufferToImage(cmd, staging.handle, img.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            vk_ctx_->end_single_command(cmd);

            vk_ctx_->transition_image_layout(img.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            vk_ctx_->destroy_buffer(staging);
            stbi_image_free(pixels);

            // Register with ImGui
            te.thumb_image = img.handle;
            te.thumb_memory = img.memory;
            te.thumb_view = img.view;
            te.imgui_tex = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
                thumb_sampler_, img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            te.has_thumbnail = true;
        } else if (pixels) {
            stbi_image_free(pixels);
        }

        thumbnail_entries_.push_back(std::move(te));
    }

    // Sort by date (newest first)
    std::sort(thumbnail_entries_.begin(), thumbnail_entries_.end(),
        [](const ThumbnailEntry& a, const ThumbnailEntry& b) {
            return a.date_str > b.date_str;
        });

    thumbnails_dirty_ = false;
}

// ── File browser helpers ─────────────────────────────────────────────────────

static std::string format_file_size(uintmax_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

void PhysicsInterface::refresh_browse_entries() {
    browse_entries.clear();
    browse_selected_idx = -1;

    std::error_code ec;
    if (!fs::is_directory(browse_current_dir, ec)) {
        browse_current_dir = fs::current_path(ec).string();
    }

    // Collect directories and matching files
    std::string filter_ext = show_molecule_import_dialog ? ".ppmol"
                           : show_import_dialog ? ".ppel" : ".ppsg";
    std::vector<BrowseEntry> dirs, files;
    for (auto& entry : fs::directory_iterator(browse_current_dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // skip hidden

        if (entry.is_directory(ec)) {
            dirs.push_back({ name, true, 0 });
        } else if (entry.is_regular_file(ec)) {
            std::string ext = entry.path().extension().string();
            if (ext == filter_ext) {
                uintmax_t sz = entry.file_size(ec);
                files.push_back({ name, false, sz });
            }
        }
    }

    // Sort alphabetically
    auto cmp = [](const BrowseEntry& a, const BrowseEntry& b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Dirs first, then files
    browse_entries.insert(browse_entries.end(), dirs.begin(), dirs.end());
    browse_entries.insert(browse_entries.end(), files.begin(), files.end());

    browse_needs_refresh = false;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Save / Load Dialog ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_save_load_dialog() {
    // Initialize browse directory on first open
    if (browse_current_dir.empty()) {
        std::error_code ec;
        fs::path saves_dir = fs::current_path(ec) / "saves";
        if (fs::is_directory(saves_dir, ec))
            browse_current_dir = saves_dir.string();
        else
            browse_current_dir = fs::current_path(ec).string();
    }

    bool use_thumbnails = !show_import_dialog && !show_molecule_import_dialog;

    if (browse_needs_refresh) {
        refresh_browse_entries();
        if (use_thumbnails) thumbnails_dirty_ = true;
    }
    if (use_thumbnails && thumbnails_dirty_)
        load_thumbnails();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImVec2 size = use_thumbnails ? ImVec2(720, 520) : ImVec2(520, 480);

    ImGui::SetNextWindowPos(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    const char* title = show_molecule_import_dialog ? "Import Molecule###SaveLoad"
                      : show_import_dialog ? "Import Element###SaveLoad"
                      : show_save_dialog  ? "Save Simulation###SaveLoad"
                                          : "Load Simulation###SaveLoad";
    bool open = true;

    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        // ── Path bar ─────────────────────────────────────────────────────
        ImGui::Text("Path:");
        ImGui::SameLine();

        // Sync path buffer
        snprintf(browse_path_buf, sizeof(browse_path_buf), "%s", browse_current_dir.c_str());
        ImGui::SetNextItemWidth(-60);
        if (ImGui::InputText("##path", browse_path_buf, sizeof(browse_path_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec;
            if (fs::is_directory(browse_path_buf, ec)) {
                browse_current_dir = browse_path_buf;
                browse_needs_refresh = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            fs::path parent = fs::path(browse_current_dir).parent_path();
            if (!parent.empty() && parent != browse_current_dir) {
                browse_current_dir = parent.string();
                browse_needs_refresh = true;
                refresh_browse_entries();
                if (use_thumbnails) { thumbnails_dirty_ = true; load_thumbnails(); }
            }
        }

        ImGui::Separator();

        float list_height = ImGui::GetContentRegionAvail().y - 90.0f;

        if (use_thumbnails) {
            // ── Thumbnail card grid for .ppsg save/load ──────────────────
            if (ImGui::BeginChild("##ThumbnailGrid", ImVec2(0, list_height), ImGuiChildFlags_Border)) {
                // Show directories first (as simple selectables)
                for (int i = 0; i < (int)browse_entries.size(); i++) {
                    if (!browse_entries[i].is_dir) continue;
                    ImGui::PushID(i + 10000);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
                    char label[320];
                    snprintf(label, sizeof(label), "[DIR]  %s/", browse_entries[i].name.c_str());
                    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        fs::path new_dir = fs::path(browse_current_dir) / browse_entries[i].name;
                        browse_current_dir = new_dir.string();
                        browse_needs_refresh = true;
                        refresh_browse_entries();
                        thumbnails_dirty_ = true;
                        load_thumbnails();
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }

                if (!thumbnail_entries_.empty()) {
                    if (browse_entries.size() > thumbnail_entries_.size())
                        ImGui::Separator();

                    // Grid layout
                    float card_w = 192.0f;
                    float card_h = 155.0f;
                    float thumb_img_h = 108.0f;
                    float padding = 10.0f;
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    int cols = std::max(1, (int)((avail_w + padding) / (card_w + padding)));

                    for (int i = 0; i < (int)thumbnail_entries_.size(); ++i) {
                        auto& te = thumbnail_entries_[i];
                        int col = i % cols;
                        if (col > 0) ImGui::SameLine(0, padding);

                        ImGui::PushID(i);

                        ImVec2 cursor = ImGui::GetCursorScreenPos();
                        bool selected = (browse_selected_idx == i);
                        ImU32 bg_col = selected ? IM_COL32(40, 80, 140, 200) : IM_COL32(20, 25, 35, 200);
                        ImU32 border_col = selected ? IM_COL32(80, 160, 255, 255) : IM_COL32(50, 60, 80, 200);

                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddRectFilled(cursor, ImVec2(cursor.x + card_w, cursor.y + card_h), bg_col, 6.0f);
                        dl->AddRect(cursor, ImVec2(cursor.x + card_w, cursor.y + card_h), border_col, 6.0f);

                        // Thumbnail image or placeholder
                        float img_x = cursor.x + 4;
                        float img_y = cursor.y + 4;
                        float img_w = card_w - 8;
                        if (te.has_thumbnail && te.imgui_tex) {
                            ImGui::SetCursorScreenPos(ImVec2(img_x, img_y));
                            ImGui::Image(te.imgui_tex, ImVec2(img_w, thumb_img_h));
                        } else {
                            dl->AddRectFilled(ImVec2(img_x, img_y),
                                ImVec2(img_x + img_w, img_y + thumb_img_h),
                                IM_COL32(10, 12, 20, 255), 4.0f);
                            ImVec2 text_sz = ImGui::CalcTextSize("No Preview");
                            dl->AddText(ImVec2(img_x + (img_w - text_sz.x) * 0.5f,
                                img_y + (thumb_img_h - text_sz.y) * 0.5f),
                                IM_COL32(80, 90, 110, 200), "No Preview");
                        }

                        // Save name (clipped to card width)
                        float text_y = cursor.y + thumb_img_h + 10;
                        dl->PushClipRect(ImVec2(cursor.x + 6, text_y),
                            ImVec2(cursor.x + card_w - 6, text_y + 16), true);
                        dl->AddText(ImVec2(cursor.x + 6, text_y),
                            IM_COL32(230, 230, 240, 255), te.name.c_str());
                        dl->PopClipRect();

                        // Date + size
                        float info_y = text_y + 16;
                        std::string info = te.date_str + "  " + format_file_size(te.size);
                        dl->PushClipRect(ImVec2(cursor.x + 6, info_y),
                            ImVec2(cursor.x + card_w - 6, info_y + 14), true);
                        dl->AddText(ImVec2(cursor.x + 6, info_y),
                            IM_COL32(120, 130, 155, 200), info.c_str());
                        dl->PopClipRect();

                        // Invisible button for click handling
                        ImGui::SetCursorScreenPos(cursor);
                        if (ImGui::InvisibleButton("##card", ImVec2(card_w, card_h))) {
                            browse_selected_idx = i;
                            snprintf(browse_filename, sizeof(browse_filename), "%s.ppsg", te.name.c_str());
                        }

                        // Double-click to load
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            if (!show_save_dialog) {
                                snprintf(save_filename, sizeof(save_filename), "%s", te.filepath.c_str());
                                request_load = true;
                                show_load_dialog = false;
                                pending_free_thumbnails_ = true;
                            }
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n%s  %s", te.name.c_str(),
                                te.date_str.c_str(), format_file_size(te.size).c_str());
                        }

                        ImGui::PopID();

                        // Advance to next row if needed
                        if (col == cols - 1 || i == (int)thumbnail_entries_.size() - 1) {
                            ImGui::Dummy(ImVec2(0, card_h + padding - ImGui::GetTextLineHeight()));
                        }
                    }
                } else if (browse_entries.empty()) {
                    ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.7f),
                        "  No .ppsg files in this directory");
                }
            }
            ImGui::EndChild();
        } else {
            // ── File list (for element/molecule imports) ─────────────────
            if (ImGui::BeginChild("##FileList", ImVec2(0, list_height), ImGuiChildFlags_Border)) {
                for (int i = 0; i < (int)browse_entries.size(); i++) {
                    const auto& entry = browse_entries[i];
                    bool selected = (browse_selected_idx == i);

                    char label[320];
                    if (entry.is_dir) {
                        snprintf(label, sizeof(label), "[DIR]  %s/", entry.name.c_str());
                    } else {
                        snprintf(label, sizeof(label), "  %s", entry.name.c_str());
                    }

                    ImGui::PushID(i);
                    if (entry.is_dir) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 1.0f, 1.0f));
                        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            fs::path new_dir = fs::path(browse_current_dir) / entry.name;
                            browse_current_dir = new_dir.string();
                            browse_needs_refresh = true;
                            refresh_browse_entries();
                        }
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            browse_selected_idx = i;
                            snprintf(browse_filename, sizeof(browse_filename), "%s", entry.name.c_str());

                            if (!show_save_dialog && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                fs::path full = fs::path(browse_current_dir) / entry.name;
                                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                                if (show_molecule_import_dialog) {
                                    request_molecule_import = true;
                                    show_molecule_import_dialog = false;
                                } else if (show_import_dialog) {
                                    request_import = true;
                                    show_import_dialog = false;
                                }
                            }
                        }

                        std::string sz = format_file_size(entry.size);
                        float text_w = ImGui::CalcTextSize(sz.c_str()).x;
                        float avail = ImGui::GetContentRegionAvail().x;
                        ImGui::SameLine(avail - text_w + ImGui::GetCursorPosX() - 8);
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", sz.c_str());
                    }
                    ImGui::PopID();
                }

                if (browse_entries.empty()) {
                    const char* ext_hint = show_molecule_import_dialog ? ".ppmol" : ".ppel";
                    ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.7f),
                        "  No %s files in this directory", ext_hint);
                }
            }
            ImGui::EndChild();
        }

        // ── Filename input + action buttons ──────────────────────────────
        ImGui::Separator();

        if (show_save_dialog) {
            ImGui::Text("File:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-180);
            ImGui::InputText("##savename", browse_filename, sizeof(browse_filename));
        }

        // Status message
        if (save_load_msg_timer > 0.0f) {
            ImVec4 color = (save_load_message[0] == 'S' || save_load_message[0] == 'L')
                ? ImVec4(0.2f, 0.9f, 0.4f, std::min(1.0f, save_load_msg_timer))
                : ImVec4(0.9f, 0.3f, 0.3f, std::min(1.0f, save_load_msg_timer));
            if (show_save_dialog) ImGui::SameLine();
            ImGui::TextColored(color, "%s", save_load_message);
        }

        ImGui::Spacing();

        float btn_w = 80.0f;
        if (show_save_dialog) {
            if (ImGui::Button("Save", ImVec2(btn_w, 28))) {
                std::string fname = browse_filename;
                if (fname.size() < 5 || fname.substr(fname.size() - 5) != ".ppsg")
                    fname += ".ppsg";
                std::error_code ec;
                fs::create_directories(browse_current_dir, ec);
                fs::path full = fs::path(browse_current_dir) / fname;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_save = true;
                show_save_dialog = false;
                browse_needs_refresh = true;
                pending_free_thumbnails_ = true;
            }
        } else if (show_molecule_import_dialog) {
            bool can_import = (browse_selected_idx >= 0 &&
                               browse_selected_idx < (int)browse_entries.size() &&
                               !browse_entries[browse_selected_idx].is_dir);
            if (!can_import) ImGui::BeginDisabled();
            if (ImGui::Button("Import", ImVec2(btn_w, 28))) {
                fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_molecule_import = true;
                show_molecule_import_dialog = false;
                browse_needs_refresh = true;
            }
            if (!can_import) ImGui::EndDisabled();
        } else if (show_import_dialog) {
            bool can_import = (browse_selected_idx >= 0 &&
                               browse_selected_idx < (int)browse_entries.size() &&
                               !browse_entries[browse_selected_idx].is_dir);
            if (!can_import) ImGui::BeginDisabled();
            if (ImGui::Button("Import", ImVec2(btn_w, 28))) {
                fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                request_import = true;
                show_import_dialog = false;
                browse_needs_refresh = true;
            }
            if (!can_import) ImGui::EndDisabled();
        } else {
            // Load button (thumbnail mode uses thumbnail_entries_ index)
            bool can_load = false;
            if (use_thumbnails) {
                can_load = (browse_selected_idx >= 0 &&
                            browse_selected_idx < (int)thumbnail_entries_.size());
            } else {
                can_load = (browse_selected_idx >= 0 &&
                            browse_selected_idx < (int)browse_entries.size() &&
                            !browse_entries[browse_selected_idx].is_dir);
            }
            if (!can_load) ImGui::BeginDisabled();
            if (ImGui::Button("Load", ImVec2(btn_w, 28))) {
                if (use_thumbnails && browse_selected_idx < (int)thumbnail_entries_.size()) {
                    snprintf(save_filename, sizeof(save_filename), "%s",
                        thumbnail_entries_[browse_selected_idx].filepath.c_str());
                } else {
                    fs::path full = fs::path(browse_current_dir) / browse_entries[browse_selected_idx].name;
                    snprintf(save_filename, sizeof(save_filename), "%s", full.string().c_str());
                }
                request_load = true;
                show_load_dialog = false;
                browse_needs_refresh = true;
                pending_free_thumbnails_ = true;
            }
            if (!can_load) ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btn_w, 28))) {
            show_save_dialog = false;
            show_load_dialog = false;
            show_import_dialog = false;
            show_molecule_import_dialog = false;
            pending_free_thumbnails_ = true;
        }

        // Escape to close
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_save_dialog = false;
            show_load_dialog = false;
            show_import_dialog = false;
            show_molecule_import_dialog = false;
            pending_free_thumbnails_ = true;
        }
    }

    if (!open) {
        show_save_dialog = false;
        show_load_dialog = false;
        show_import_dialog = false;
        show_molecule_import_dialog = false;
        pending_free_thumbnails_ = true;
    }

    ImGui::End();
}
