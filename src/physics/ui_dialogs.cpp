#include "physics/interface.h"
#include "physics/paths.h"
#include "physics/phys_particles.h"
#include "physics/tutorial.h"
#include "physics/scenarios.h"
#include "physics/ui_data.h"
#include "physics/audio.h"
#include "physics/repository.h"
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
#include <random>
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

    // Pick a random variant on first call
    if (splash_variant_ < 0) {
        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        splash_variant_ = std::uniform_int_distribution<int>(0, 3)(rng);
    }

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

    // Dispatch to the selected variant
    if (splash_variant_ == 1) {
        if (!splash_inited_ && !prefs.reduced_motion) { init_splash_blue_orb(); splash_inited_ = true; }
        draw_splash_blue_orb();
        return;
    }
    if (splash_variant_ == 2) {
        if (!splash_inited_ && !prefs.reduced_motion) { init_splash_nebula(); splash_inited_ = true; }
        draw_splash_nebula();
        return;
    }
    if (splash_variant_ == 3) {
        if (!splash_inited_ && !prefs.reduced_motion) { init_splash_collider(); splash_inited_ = true; }
        draw_splash_collider();
        return;
    }

    // Variant 0: Original atom splash
    // Init particles on first call (or re-init on About)
    // Skip splash animation in reduced-motion mode
    if (!splash_inited_ && !prefs.reduced_motion) { init_splash_particles(); splash_inited_ = true; }

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
        // Use pre-rasterized 48px title font for crisp rendering (no bitmap scaling blur)
        bool has_title_font = (title_font != nullptr);
        if (has_title_font) {
            ImGui::PushFont(title_font);
        } else {
            // Fallback: runtime scale (blurry but functional)
            float old_scale_val = ImGui::GetFont()->Scale;
            float title_font_scale = 2.2f * scale;
            if (title_font_scale < 1.5f) title_font_scale = 1.5f;
            ImGui::GetFont()->Scale = title_font_scale;
            ImGui::PushFont(ImGui::GetFont());
            (void)old_scale_val; // used in PopFont below
        }

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

        if (!has_title_font) {
            ImGui::GetFont()->Scale = 1.0f;  // restore default scale
        }
        ImGui::PopFont();

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
        // Subtitle
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.0f, 0.78f, 1.0f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types  |  v" APP_VERSION);

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
// ── Blue Orb Splash (variant 1) ─────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::init_splash_blue_orb() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);

    splash_particles_.clear();
    splash_trails_.clear();
    splash_time_ = 0.0f;

    std::mt19937 rng(77);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto randf_range = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    float cx = W * 0.50f, cy = H * 0.42f;

    // Ring particles orbiting the central orb — 24 fast, tight orbit
    for (int i = 0; i < 24; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (55.0f + randf() * 20.0f) * scale;
        p.orbit_speed = 0.010f + randf() * 0.008f;
        if (i % 2 == 0) p.orbit_speed = -p.orbit_speed; // counter-rotating
        p.phase = (6.2831853f * i / 24.0f) + randf() * 0.3f;
        p.cx = cx;
        p.cy = cy;
        p.tilt_x = 0.85f + randf() * 0.15f;
        p.tilt_y = 0.35f + randf() * 0.30f;
        p.r = (1.5f + randf() * 1.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 0;
        // Cyan/white shades
        int bright = 180 + (int)(randf() * 75);
        p.color = IM_COL32(bright / 2, bright, 255, 220);
        p.glow_color = IM_COL32(0, 150, 255, 80);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // Second ring — wider, slower, more tilted
    for (int i = 0; i < 18; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (90.0f + randf() * 30.0f) * scale;
        p.orbit_speed = 0.005f + randf() * 0.005f;
        if (i % 3 == 0) p.orbit_speed = -p.orbit_speed;
        p.phase = (6.2831853f * i / 18.0f) + randf() * 0.5f;
        p.cx = cx;
        p.cy = cy;
        p.tilt_x = 0.55f + randf() * 0.20f;
        p.tilt_y = 0.80f + randf() * 0.20f;
        p.r = (1.0f + randf() * 2.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 1;
        p.color = IM_COL32(100, 200, 255, 180);
        p.glow_color = IM_COL32(50, 120, 255, 60);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // Distant ambient particles — slow drift, various blues and purples
    ImU32 ambient_colors[] = {
        IM_COL32(80, 160, 255, 200),   // blue
        IM_COL32(120, 100, 255, 180),  // indigo
        IM_COL32(60, 200, 255, 190),   // cyan
        IM_COL32(150, 80, 255, 170),   // purple
        IM_COL32(40, 220, 200, 160),   // teal
        IM_COL32(200, 200, 255, 140),  // pale white-blue
    };
    ImU32 ambient_glows[] = {
        IM_COL32(40, 80, 255, 50),
        IM_COL32(80, 50, 200, 40),
        IM_COL32(30, 120, 200, 50),
        IM_COL32(100, 40, 200, 40),
        IM_COL32(20, 150, 150, 40),
        IM_COL32(120, 120, 200, 30),
    };
    for (int i = 0; i < 120; ++i) {
        SplashParticle p{};
        p.orbit = false;
        p.x = randf() * W;
        p.y = randf() * H;
        p.vx = randf_range(-0.3f, 0.3f) * 0.5f;
        p.vy = randf_range(-0.3f, 0.3f) * 0.5f;
        p.r = (1.0f + randf() * 2.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        int ci = i % 6;
        p.type_idx = ci + 2;
        p.color = ambient_colors[ci];
        p.glow_color = ambient_glows[ci];
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    splash_trails_.resize(splash_particles_.size());
}

void PhysicsInterface::draw_splash_blue_orb() {
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    splash_time_ += dt;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float cx = W * 0.50f, cy = H * 0.42f;

    // 1. Deep dark background
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(1, 4, 12, 255));

    // 2. Distant nebula hints — subtle blue/purple patches
    draw_radial_glow(bg, W * 0.2f, H * 0.7f, 280.0f * scale,
                     IM_COL32(15, 10, 40, 40), IM_COL32(15, 10, 40, 0));
    draw_radial_glow(bg, W * 0.75f, H * 0.25f, 200.0f * scale,
                     IM_COL32(8, 15, 50, 35), IM_COL32(8, 15, 50, 0));

    // 3. The giant blue orb — layered radial glows
    float pulse = sinf(splash_time_ * 1.2f);
    float breathe = sinf(splash_time_ * 0.6f);
    float orb_r = (60.0f + breathe * 4.0f) * scale;

    // Outermost corona — huge, faint
    draw_radial_glow(bg, cx, cy, orb_r * 6.0f,
                     IM_COL32(0, 60, 180, (int)(12 + pulse * 4)), IM_COL32(0, 20, 80, 0));

    // Outer halo — medium, blue
    draw_radial_glow(bg, cx, cy, orb_r * 3.5f,
                     IM_COL32(0, 100, 255, (int)(30 + pulse * 8)), IM_COL32(0, 40, 140, 0));

    // Mid glow — brighter blue-cyan
    draw_radial_glow(bg, cx, cy, orb_r * 2.2f,
                     IM_COL32(20, 140, 255, (int)(50 + pulse * 10)), IM_COL32(10, 60, 180, 0));

    // Inner glow — bright cyan
    draw_radial_glow(bg, cx, cy, orb_r * 1.4f,
                     IM_COL32(60, 180, 255, (int)(80 + pulse * 15)), IM_COL32(20, 100, 220, 0));

    // Core orb — solid, bright
    bg->AddCircleFilled(ImVec2(cx, cy), orb_r,
                        IM_COL32(30, 120, 220, 200), 96);
    // Core inner bright spot
    bg->AddCircleFilled(ImVec2(cx, cy), orb_r * 0.7f,
                        IM_COL32(80, 170, 255, (int)(180 + pulse * 30)), 96);
    // Hot center
    bg->AddCircleFilled(ImVec2(cx, cy), orb_r * 0.35f,
                        IM_COL32(160, 220, 255, (int)(200 + pulse * 40)), 64);
    // Specular highlight (slightly off-center top-left)
    bg->AddCircleFilled(ImVec2(cx - orb_r * 0.25f, cy - orb_r * 0.25f),
                        orb_r * 0.18f, IM_COL32(220, 240, 255, 120), 32);

    // 4. Update particles
    for (auto& p : splash_particles_) {
        p.r = p.base_r * (1.0f + 0.2f * sinf(splash_time_ * 2.0f + p.pulse_phase));
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

    // 5. Update trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        splash_trails_[i].push_back(ImVec2(splash_particles_[i].x, splash_particles_[i].y));
        if (splash_trails_[i].size() > 12) splash_trails_[i].erase(splash_trails_[i].begin());
    }

    // 6. Draw trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& trail = splash_trails_[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float t = (float)j / (float)trail.size();
            float alpha = t * 0.35f;
            float width = splash_particles_[i].r * (0.2f + 0.5f * t);
            ImU32 col = (splash_particles_[i].color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j-1], trail[j], col, width);
        }
    }

    // 7. Force lines between nearby orbital particles
    float force_dist = 70.0f * scale;
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        if (!splash_particles_[i].orbit) continue;
        for (size_t j = i + 1; j < splash_particles_.size(); ++j) {
            if (!splash_particles_[j].orbit) continue;
            float dx = splash_particles_[j].x - splash_particles_[i].x;
            float dy = splash_particles_[j].y - splash_particles_[i].y;
            float dist2 = dx * dx + dy * dy;
            if (dist2 < force_dist * force_dist && dist2 > 25.0f) {
                float dist = sqrtf(dist2);
                float alpha = (1.0f - dist / force_dist) * 0.12f;
                ImVec2 a(splash_particles_[i].x, splash_particles_[i].y);
                ImVec2 b(splash_particles_[j].x, splash_particles_[j].y);
                bg->AddLine(a, b, IM_COL32(0, 140, 255, (int)(alpha * 200)), 3.0f);
                bg->AddLine(a, b, IM_COL32(100, 200, 255, (int)(alpha * 255)), 1.0f);
            }
        }
    }

    // 8. Draw particles (glow + core)
    for (auto& p : splash_particles_) {
        draw_radial_glow(bg, p.x, p.y, p.r * 3.5f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
        bg->AddCircleFilled(ImVec2(p.x - p.r * 0.15f, p.y - p.r * 0.15f),
                            p.r * 0.45f, IM_COL32(220, 240, 255, 70));
    }

    // 9. Vignette
    draw_vignette(bg, W, H);

    // 10. Scanlines
    float scanline_gap = 4.0f * scale;
    if (scanline_gap < 2.0f) scanline_gap = 2.0f;
    for (float y = 0; y < H; y += scanline_gap)
        bg->AddRectFilled(ImVec2(0, y + scanline_gap * 0.5f), ImVec2(W, y + scanline_gap),
                          IM_COL32(0, 0, 0, 6));

    // 11. Text overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##SplashOrb", nullptr, flags)) {
        float title_y = H - 80.0f * scale;
        float left_margin = 30.0f * scale;

        // Accent line
        bg->AddLine(ImVec2(left_margin, title_y - 8.0f * scale),
                    ImVec2(left_margin + 180.0f * scale, title_y - 8.0f * scale),
                    IM_COL32(0, 160, 255, 130), 1.0f);

        // Title — use pre-rasterized font if available
        bool has_title_font = (title_font != nullptr);
        if (has_title_font) {
            ImGui::PushFont(title_font);
        } else {
            float title_font_scale = 2.2f * scale;
            if (title_font_scale < 1.5f) title_font_scale = 1.5f;
            ImGui::GetFont()->Scale = title_font_scale;
            ImGui::PushFont(ImGui::GetFont());
        }

        // Glow shadow layers
        for (int g = 3; g >= 1; --g) {
            float ga = 0.10f / (float)g;
            float off = (float)g * 1.5f;
            ImGui::SetCursorPos(ImVec2(left_margin + off, title_y + off));
            ImGui::TextColored(ImVec4(0.0f, 0.3f, 0.9f, ga), "Particle ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImVec4(0.0f, 0.3f, 0.9f, ga), "Playground");
        }

        // "Particle " in white
        ImGui::SetCursorPos(ImVec2(left_margin, title_y));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Particle ");
        ImGui::SameLine(0, 0);
        // "Playground" in bright blue
        ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "Playground");

        if (!has_title_font) {
            ImGui::GetFont()->Scale = 1.0f;
        }
        ImGui::PopFont();

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
        // Subtitle
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.3f, 0.65f, 1.0f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types  |  v" APP_VERSION);

        // Top-right badge
        {
            const char* badge = "QUANTUM PHYSICS SANDBOX";
            ImVec2 badge_sz = ImGui::CalcTextSize(badge);
            float badge_x = W - badge_sz.x - 18.0f * scale;
            float badge_y = 14.0f * scale;
            bg->AddRectFilled(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                              ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                              IM_COL32(20, 60, 180, 20));
            bg->AddRect(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                        ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                        IM_COL32(60, 140, 255, 60), 2.0f);
            ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
            ImGui::TextColored(ImVec4(0.3f, 0.55f, 1.0f, 0.9f), "%s", badge);
        }

        // Bottom-center dismiss hint
        float hint_pulse = 0.3f + 0.2f * sinf(splash_time_ * 3.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 30.0f * scale));
        ImGui::TextColored(ImVec4(0.35f, 0.50f, 0.70f, hint_pulse), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Nebula Splash (variant 2) ───────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::init_splash_nebula() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);

    splash_particles_.clear();
    splash_trails_.clear();
    splash_time_ = 0.0f;

    std::mt19937 rng(314);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto randf_range = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    float cx = W * 0.45f, cy = H * 0.45f;

    // Star field — many tiny white/blue dots slowly drifting
    ImU32 star_colors[] = {
        IM_COL32(255, 255, 255, 200),
        IM_COL32(200, 220, 255, 180),
        IM_COL32(180, 200, 255, 160),
        IM_COL32(255, 240, 200, 170),
        IM_COL32(255, 200, 180, 150),
    };
    for (int i = 0; i < 80; ++i) {
        SplashParticle p{};
        p.orbit = false;
        p.x = randf() * W;
        p.y = randf() * H;
        p.vx = randf_range(-0.05f, 0.05f);
        p.vy = randf_range(-0.05f, 0.05f);
        p.r = (0.5f + randf() * 1.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 0;
        p.color = star_colors[i % 5];
        p.glow_color = IM_COL32(100, 150, 255, 30);
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // Nebula cloud particles — larger, colorful, slow spiral outward
    ImU32 nebula_colors[] = {
        IM_COL32(180, 60, 120, 120),   // magenta
        IM_COL32(80, 40, 160, 100),    // deep purple
        IM_COL32(40, 100, 180, 110),   // blue
        IM_COL32(60, 160, 140, 90),    // teal
        IM_COL32(200, 80, 60, 100),    // red-orange
        IM_COL32(140, 60, 180, 95),    // violet
        IM_COL32(60, 80, 200, 105),    // royal blue
        IM_COL32(180, 120, 60, 85),    // gold dust
    };
    ImU32 nebula_glows[] = {
        IM_COL32(180, 60, 120, 40),
        IM_COL32(80, 40, 160, 35),
        IM_COL32(40, 100, 180, 40),
        IM_COL32(60, 160, 140, 30),
        IM_COL32(200, 80, 60, 35),
        IM_COL32(140, 60, 180, 30),
        IM_COL32(60, 80, 200, 35),
        IM_COL32(180, 120, 60, 25),
    };
    for (int i = 0; i < 60; ++i) {
        SplashParticle p{};
        p.orbit = true;
        float angle = randf() * 6.2831853f;
        float arm_offset = static_cast<float>(i % 3) * 2.0944f; // 3 spiral arms
        p.orbit_r = (30.0f + randf() * 150.0f) * scale;
        p.orbit_speed = 0.002f + (200.0f * scale / (p.orbit_r + 50.0f)) * 0.003f; // faster near center
        if (i % 2 == 0) p.orbit_speed = -p.orbit_speed;
        p.phase = angle + arm_offset;
        p.cx = cx;
        p.cy = cy;
        p.tilt_x = 0.90f + randf() * 0.10f;
        p.tilt_y = 0.50f + randf() * 0.25f;
        p.r = (3.0f + randf() * 6.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        int ci = i % 8;
        p.type_idx = ci + 1;
        p.color = nebula_colors[ci];
        p.glow_color = nebula_glows[ci];
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    // Bright core cluster — a few hot bright particles near center
    for (int i = 0; i < 12; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (8.0f + randf() * 25.0f) * scale;
        p.orbit_speed = 0.008f + randf() * 0.008f;
        p.phase = randf() * 6.2831853f;
        p.cx = cx;
        p.cy = cy;
        p.tilt_x = 0.7f + randf() * 0.3f;
        p.tilt_y = 0.7f + randf() * 0.3f;
        p.r = (2.0f + randf() * 2.5f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 9;
        p.color = IM_COL32(255, 230, 200, 230);
        p.glow_color = IM_COL32(255, 180, 100, 80);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    splash_trails_.resize(splash_particles_.size());
}

void PhysicsInterface::draw_splash_nebula() {
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    splash_time_ += dt;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float cx = W * 0.45f, cy = H * 0.45f;

    // 1. Very dark background
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 3, 8, 255));

    // 2. Multi-layer nebula glow clouds — rotating hue patches
    float t = splash_time_;
    // Warm magenta cloud
    draw_radial_glow(bg, cx + sinf(t * 0.3f) * 40.0f * scale, cy + cosf(t * 0.2f) * 30.0f * scale,
                     280.0f * scale,
                     IM_COL32(60, 15, 40, 45), IM_COL32(60, 15, 40, 0));
    // Cool blue cloud
    draw_radial_glow(bg, cx - cosf(t * 0.25f) * 50.0f * scale, cy - sinf(t * 0.35f) * 35.0f * scale,
                     240.0f * scale,
                     IM_COL32(15, 25, 70, 40), IM_COL32(15, 25, 70, 0));
    // Teal accent
    draw_radial_glow(bg, cx + 100.0f * scale, cy + 60.0f * scale, 180.0f * scale,
                     IM_COL32(10, 50, 50, 30), IM_COL32(10, 50, 50, 0));
    // Purple haze
    draw_radial_glow(bg, cx - 80.0f * scale, cy - 40.0f * scale, 200.0f * scale,
                     IM_COL32(40, 10, 60, 35), IM_COL32(40, 10, 60, 0));

    // 3. Central galactic core glow
    float pulse = sinf(t * 0.8f);
    draw_radial_glow(bg, cx, cy, (100.0f + pulse * 8.0f) * scale,
                     IM_COL32(255, 200, 130, (int)(35 + pulse * 10)), IM_COL32(200, 120, 60, 0));
    draw_radial_glow(bg, cx, cy, (50.0f + pulse * 4.0f) * scale,
                     IM_COL32(255, 240, 200, (int)(50 + pulse * 15)), IM_COL32(255, 180, 100, 0));

    // 4. Update particles
    for (auto& p : splash_particles_) {
        p.r = p.base_r * (1.0f + 0.25f * sinf(t * 1.5f + p.pulse_phase));
        if (p.orbit) {
            p.phase += p.orbit_speed * dt * 60.0f;
            p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
            p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        } else {
            // Stars: very subtle twinkle drift
            p.x += p.vx * dt * 60.0f;
            p.y += p.vy * dt * 60.0f;
            if (p.x < -10) p.x = W + 10;
            if (p.x > W + 10) p.x = -10;
            if (p.y < -10) p.y = H + 10;
            if (p.y > H + 10) p.y = -10;
        }
    }

    // 5. Update trails (short for nebula — ghostly wisps)
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        splash_trails_[i].push_back(ImVec2(splash_particles_[i].x, splash_particles_[i].y));
        if (splash_trails_[i].size() > 10) splash_trails_[i].erase(splash_trails_[i].begin());
    }

    // 6. Draw trails (nebula particles only — not stars)
    for (size_t i = 80; i < splash_particles_.size(); ++i) {
        auto& trail = splash_trails_[i];
        for (size_t j = 1; j < trail.size(); ++j) {
            float tf = (float)j / (float)trail.size();
            float alpha = tf * 0.3f;
            float width = splash_particles_[i].r * (0.3f + 0.4f * tf);
            ImU32 col = (splash_particles_[i].color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j-1], trail[j], col, width);
        }
    }

    // 7. Draw particles — stars first (small, sharp), then nebula (big, glowy)
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& p = splash_particles_[i];
        if (i < 80) {
            // Stars — tiny, sharp, twinkle
            float twinkle = 0.6f + 0.4f * sinf(t * 4.0f + p.pulse_phase);
            ImU32 col = (p.color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(twinkle * ((p.color >> IM_COL32_A_SHIFT) & 0xFF)) << IM_COL32_A_SHIFT);
            bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, col);
        } else {
            // Nebula — big soft glow
            draw_radial_glow(bg, p.x, p.y, p.r * 5.0f, p.glow_color, IM_COL32(0, 0, 0, 0));
            bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
        }
    }

    // 8. Vignette
    draw_vignette(bg, W, H);

    // 9. Scanlines (very subtle)
    float scanline_gap = 4.0f * scale;
    if (scanline_gap < 2.0f) scanline_gap = 2.0f;
    for (float y = 0; y < H; y += scanline_gap)
        bg->AddRectFilled(ImVec2(0, y + scanline_gap * 0.5f), ImVec2(W, y + scanline_gap),
                          IM_COL32(0, 0, 0, 5));

    // 10. Text overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##SplashNebula", nullptr, flags)) {
        float title_y = H - 80.0f * scale;
        float left_margin = 30.0f * scale;

        // Accent line — warm gradient
        bg->AddLine(ImVec2(left_margin, title_y - 8.0f * scale),
                    ImVec2(left_margin + 200.0f * scale, title_y - 8.0f * scale),
                    IM_COL32(200, 100, 180, 120), 1.0f);

        bool has_title_font = (title_font != nullptr);
        if (has_title_font) {
            ImGui::PushFont(title_font);
        } else {
            float title_font_scale = 2.2f * scale;
            if (title_font_scale < 1.5f) title_font_scale = 1.5f;
            ImGui::GetFont()->Scale = title_font_scale;
            ImGui::PushFont(ImGui::GetFont());
        }

        // Glow shadow layers
        for (int g = 3; g >= 1; --g) {
            float ga = 0.10f / (float)g;
            float off = (float)g * 1.5f;
            ImGui::SetCursorPos(ImVec2(left_margin + off, title_y + off));
            ImGui::TextColored(ImVec4(0.5f, 0.15f, 0.4f, ga), "Particle ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImVec4(0.5f, 0.15f, 0.4f, ga), "Playground");
        }

        // "Particle " in white
        ImGui::SetCursorPos(ImVec2(left_margin, title_y));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Particle ");
        ImGui::SameLine(0, 0);
        // "Playground" in warm rose/magenta
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.7f, 1.0f), "Playground");

        if (!has_title_font) {
            ImGui::GetFont()->Scale = 1.0f;
        }
        ImGui::PopFont();

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.7f, 0.4f, 0.6f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types  |  v" APP_VERSION);

        // Top-right badge
        {
            const char* badge = "QUANTUM PHYSICS SANDBOX";
            ImVec2 badge_sz = ImGui::CalcTextSize(badge);
            float badge_x = W - badge_sz.x - 18.0f * scale;
            float badge_y = 14.0f * scale;
            bg->AddRectFilled(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                              ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                              IM_COL32(120, 40, 80, 20));
            bg->AddRect(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                        ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                        IM_COL32(180, 80, 140, 60), 2.0f);
            ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
            ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.6f, 0.9f), "%s", badge);
        }

        float hint_pulse = 0.3f + 0.2f * sinf(splash_time_ * 3.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 30.0f * scale));
        ImGui::TextColored(ImVec4(0.50f, 0.40f, 0.50f, hint_pulse), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Collider Splash (variant 3) ─────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::init_splash_collider() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);

    splash_particles_.clear();
    splash_trails_.clear();
    splash_time_ = 0.0f;

    std::mt19937 rng(256);
    auto randf = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto randf_range = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };

    float cx = W * 0.50f, cy = H * 0.42f;

    // Two beam streams converging on center — left beam and right beam
    // Left beam particles (moving right)
    for (int i = 0; i < 20; ++i) {
        SplashParticle p{};
        p.orbit = false;
        p.x = randf_range(-50.0f, W * 0.35f);
        p.y = cy + randf_range(-8.0f, 8.0f) * scale;
        p.vx = 1.5f + randf() * 1.5f;
        p.vy = randf_range(-0.1f, 0.1f);
        p.r = (2.0f + randf() * 2.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 0; // beam left
        p.color = IM_COL32(255, 100, 50, 220);
        p.glow_color = IM_COL32(255, 60, 20, 60);
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // Right beam particles (moving left)
    for (int i = 0; i < 20; ++i) {
        SplashParticle p{};
        p.orbit = false;
        p.x = randf_range(W * 0.65f, W + 50.0f);
        p.y = cy + randf_range(-8.0f, 8.0f) * scale;
        p.vx = -(1.5f + randf() * 1.5f);
        p.vy = randf_range(-0.1f, 0.1f);
        p.r = (2.0f + randf() * 2.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 1; // beam right
        p.color = IM_COL32(50, 150, 255, 220);
        p.glow_color = IM_COL32(20, 100, 255, 60);
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // Collision debris — particles exploding outward from center
    ImU32 debris_colors[] = {
        IM_COL32(255, 220, 80, 200),   // yellow
        IM_COL32(80, 255, 160, 190),   // green
        IM_COL32(255, 80, 200, 180),   // pink
        IM_COL32(100, 200, 255, 190),  // light blue
        IM_COL32(255, 160, 60, 185),   // orange
        IM_COL32(200, 100, 255, 175),  // purple
        IM_COL32(255, 255, 255, 200),  // white
        IM_COL32(80, 255, 255, 185),   // cyan
    };
    ImU32 debris_glows[] = {
        IM_COL32(255, 180, 40, 50),
        IM_COL32(40, 200, 120, 45),
        IM_COL32(200, 40, 150, 40),
        IM_COL32(60, 150, 220, 45),
        IM_COL32(220, 120, 30, 40),
        IM_COL32(150, 60, 220, 38),
        IM_COL32(200, 200, 200, 50),
        IM_COL32(40, 220, 220, 42),
    };
    for (int i = 0; i < 80; ++i) {
        SplashParticle p{};
        p.orbit = false;
        float angle = randf() * 6.2831853f;
        float speed = 0.5f + randf() * 2.5f;
        // Spawn from center with initial radial offset
        float off = randf() * 30.0f * scale;
        p.x = cx + cosf(angle) * off;
        p.y = cy + sinf(angle) * off;
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed;
        p.r = (1.0f + randf() * 3.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        int ci = i % 8;
        p.type_idx = ci + 2;
        p.color = debris_colors[ci];
        p.glow_color = debris_glows[ci];
        p.phase = randf() * 6.2831853f;
        splash_particles_.push_back(p);
    }

    // Detector ring particles — orbiting at fixed radius like a detector barrel
    for (int i = 0; i < 32; ++i) {
        SplashParticle p{};
        p.orbit = true;
        p.orbit_r = (140.0f + randf() * 15.0f) * scale;
        p.orbit_speed = 0.003f + randf() * 0.002f;
        if (i % 2 == 0) p.orbit_speed = -p.orbit_speed;
        p.phase = (6.2831853f * i / 32.0f);
        p.cx = cx;
        p.cy = cy;
        p.tilt_x = 1.0f;
        p.tilt_y = 0.35f + randf() * 0.10f;
        p.r = (1.5f + randf() * 1.0f) * scale;
        p.base_r = p.r;
        p.pulse_phase = randf() * 6.2831853f;
        p.type_idx = 10;
        p.color = IM_COL32(60, 80, 100, 140);
        p.glow_color = IM_COL32(30, 50, 80, 30);
        p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
        p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        splash_particles_.push_back(p);
    }

    splash_trails_.resize(splash_particles_.size());
}

void PhysicsInterface::draw_splash_collider() {
    ImGuiIO& io = ImGui::GetIO();
    float dt = io.DeltaTime;
    splash_time_ += dt;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float scale = std::min(W / 616.0f, H / 353.0f);
    float cx = W * 0.50f, cy = H * 0.42f;
    float t = splash_time_;

    // 1. Dark background
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(3, 5, 10, 255));

    // 2. Detector ring — faint structural circle
    float det_r = 145.0f * scale;
    bg->AddCircle(ImVec2(cx, cy), det_r, IM_COL32(30, 45, 60, 60), 96, 1.5f);
    bg->AddCircle(ImVec2(cx, cy), det_r + 5.0f * scale, IM_COL32(20, 30, 45, 30), 96, 1.0f);

    // 3. Beam line — horizontal glow
    float beam_alpha = 0.4f + 0.15f * sinf(t * 3.0f);
    bg->AddRectFilled(ImVec2(0, cy - 3.0f * scale), ImVec2(W, cy + 3.0f * scale),
                      IM_COL32(80, 100, 140, (int)(beam_alpha * 25)));
    bg->AddLine(ImVec2(0, cy), ImVec2(cx - 20.0f * scale, cy),
                IM_COL32(255, 120, 60, (int)(beam_alpha * 100)), 2.0f);
    bg->AddLine(ImVec2(cx + 20.0f * scale, cy), ImVec2(W, cy),
                IM_COL32(60, 160, 255, (int)(beam_alpha * 100)), 2.0f);

    // 4. Collision flash at center — pulsing
    float flash = 0.5f + 0.5f * sinf(t * 2.0f);
    draw_radial_glow(bg, cx, cy, (40.0f + flash * 15.0f) * scale,
                     IM_COL32(255, 230, 180, (int)(40 + flash * 25)),
                     IM_COL32(255, 150, 80, 0));
    draw_radial_glow(bg, cx, cy, (20.0f + flash * 5.0f) * scale,
                     IM_COL32(255, 255, 240, (int)(60 + flash * 40)),
                     IM_COL32(255, 200, 120, 0));

    // 5. Update particles
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& p = splash_particles_[i];
        p.r = p.base_r * (1.0f + 0.15f * sinf(t * 2.5f + p.pulse_phase));

        if (p.orbit) {
            p.phase += p.orbit_speed * dt * 60.0f;
            p.x = p.cx + cosf(p.phase) * p.orbit_r * p.tilt_x;
            p.y = p.cy + sinf(p.phase) * p.orbit_r * p.tilt_y;
        } else {
            p.x += p.vx * dt * 60.0f;
            p.y += p.vy * dt * 60.0f;

            // Beam particles: wrap horizontally
            if (p.type_idx == 0) { // left beam
                if (p.x > cx - 15.0f * scale) {
                    p.x = -20.0f;
                    p.y = cy + (p.y - cy) * 0.3f; // re-center
                }
            } else if (p.type_idx == 1) { // right beam
                if (p.x < cx + 15.0f * scale) {
                    p.x = W + 20.0f;
                    p.y = cy + (p.y - cy) * 0.3f;
                }
            } else {
                // Debris: wrap at edges
                if (p.x < -20) p.x = W + 20;
                if (p.x > W + 20) p.x = -20;
                if (p.y < -20) p.y = H + 20;
                if (p.y > H + 20) p.y = -20;
            }
        }
    }

    // 6. Trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        splash_trails_[i].push_back(ImVec2(splash_particles_[i].x, splash_particles_[i].y));
        if (splash_trails_[i].size() > 14) splash_trails_[i].erase(splash_trails_[i].begin());
    }

    // 7. Draw trails
    for (size_t i = 0; i < splash_particles_.size(); ++i) {
        auto& trail = splash_trails_[i];
        auto& p = splash_particles_[i];
        if (p.type_idx >= 10) continue; // skip detector ring trails
        for (size_t j = 1; j < trail.size(); ++j) {
            float tf = (float)j / (float)trail.size();
            float alpha = tf * 0.4f;
            float width = p.r * (0.2f + 0.5f * tf);
            ImU32 col = (p.color & ~IM_COL32_A_MASK) |
                        ((uint32_t)(alpha * 255) << IM_COL32_A_SHIFT);
            bg->AddLine(trail[j-1], trail[j], col, width);
        }
    }

    // 8. Draw particles
    for (auto& p : splash_particles_) {
        draw_radial_glow(bg, p.x, p.y, p.r * 3.5f, p.glow_color, IM_COL32(0, 0, 0, 0));
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.r, p.color);
        bg->AddCircleFilled(ImVec2(p.x - p.r * 0.15f, p.y - p.r * 0.15f),
                            p.r * 0.4f, IM_COL32(255, 255, 255, 60));
    }

    // 9. Vignette
    draw_vignette(bg, W, H);

    // 10. Scanlines
    float scanline_gap = 4.0f * scale;
    if (scanline_gap < 2.0f) scanline_gap = 2.0f;
    for (float y = 0; y < H; y += scanline_gap)
        bg->AddRectFilled(ImVec2(0, y + scanline_gap * 0.5f), ImVec2(W, y + scanline_gap),
                          IM_COL32(0, 0, 0, 6));

    // 11. Text overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##SplashCollider", nullptr, flags)) {
        float title_y = H - 80.0f * scale;
        float left_margin = 30.0f * scale;

        // Accent line — warm orange
        bg->AddLine(ImVec2(left_margin, title_y - 8.0f * scale),
                    ImVec2(left_margin + 180.0f * scale, title_y - 8.0f * scale),
                    IM_COL32(255, 160, 60, 130), 1.0f);

        bool has_title_font = (title_font != nullptr);
        if (has_title_font) {
            ImGui::PushFont(title_font);
        } else {
            float title_font_scale = 2.2f * scale;
            if (title_font_scale < 1.5f) title_font_scale = 1.5f;
            ImGui::GetFont()->Scale = title_font_scale;
            ImGui::PushFont(ImGui::GetFont());
        }

        // Glow shadow
        for (int g = 3; g >= 1; --g) {
            float ga = 0.10f / (float)g;
            float off = (float)g * 1.5f;
            ImGui::SetCursorPos(ImVec2(left_margin + off, title_y + off));
            ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.1f, ga), "Particle ");
            ImGui::SameLine(0, 0);
            ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.1f, ga), "Playground");
        }

        // Title
        ImGui::SetCursorPos(ImVec2(left_margin, title_y));
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Particle ");
        ImGui::SameLine(0, 0);
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Playground");

        if (!has_title_font) {
            ImGui::GetFont()->Scale = 1.0f;
        }
        ImGui::PopFont();

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
        ImGui::SetCursorPos(ImVec2(left_margin, title_y + 40.0f * scale));
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 0.7f),
            "Standard Model  |  Fusion  |  Fission  |  67 Particle Types  |  v" APP_VERSION);

        // Top-right badge
        {
            const char* badge = "QUANTUM PHYSICS SANDBOX";
            ImVec2 badge_sz = ImGui::CalcTextSize(badge);
            float badge_x = W - badge_sz.x - 18.0f * scale;
            float badge_y = 14.0f * scale;
            bg->AddRectFilled(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                              ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                              IM_COL32(200, 100, 30, 15));
            bg->AddRect(ImVec2(badge_x - 10.0f * scale, badge_y - 3.0f * scale),
                        ImVec2(badge_x + badge_sz.x + 10.0f * scale, badge_y + badge_sz.y + 3.0f * scale),
                        IM_COL32(255, 160, 60, 50), 2.0f);
            ImGui::SetCursorPos(ImVec2(badge_x, badge_y));
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 0.9f), "%s", badge);
        }

        float hint_pulse = 0.3f + 0.2f * sinf(splash_time_ * 3.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 30.0f * scale));
        ImGui::TextColored(ImVec4(0.55f, 0.45f, 0.35f, hint_pulse), "%s", hint);
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
        // Compute total menu height and center vertically
        float btn_h = 40.0f;
        float btn_spacing = 48.0f;
        float title_h = title_size.y + 20.0f;  // title + gap to first button
        float menu_h = title_h + btn_spacing * 10 + btn_h + 30.0f; // 11 buttons + hint
        float menu_top = cy - menu_h * 0.5f;

        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, menu_top));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Menu buttons (centered column)
        float btn_w = 200.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = menu_top + title_h;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));

        // Click SFX helper
        auto menu_click = [&]() {
            if (audio_ptr) audio_ptr->play(AudioPlayer::SFX_CLICK);
        };

        // Resume
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_pause_menu = false;
            sim_running = true;
        }

        // New
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_pause_menu = false;
            request_reset = true;
        }

        // Scenarios
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Scenarios", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_scenario_menu = true;
            show_pause_menu = false;
        }

        // Save
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Save", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_save_dialog = true;
            show_load_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // Load
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Load", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_load_dialog = true;
            show_save_dialog = false;
            show_pause_menu = false;
            browse_needs_refresh = true;
        }

        // How To Play
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("How To Play", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_howto = true;
            show_pause_menu = false;
        }

        // Repository
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
        if (ParticleRepository::is_available()) {
            if (ImGui::Button("Repository", ImVec2(btn_w, btn_h))) {
                menu_click();
                show_repository = true;
                show_pause_menu = false;
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("Repository", ImVec2(btn_w, btn_h));
            ImGui::EndDisabled();
        }

        // Achievements
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 7));
        if (ImGui::Button("Achievements", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_achievements_panel = true;
            show_pause_menu = false;
        }

        // Credits
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 8));
        if (ImGui::Button("Credits", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_credits_ = true;
            show_pause_menu = false;
        }

        // Settings
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 9));
        if (ImGui::Button("Settings", ImVec2(btn_w, btn_h))) {
            menu_click();
            show_settings_menu = true;
            show_pause_menu = false;
        }

        // Quit
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 10));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 0.95f));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            menu_click();
            request_quit = true;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // Hint text
        float hint_y = btn_y + btn_spacing * 11 + 10.0f;
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

        static const char* TAB_LABELS[] = { "Display", "Perf.", "Theme", "Access.", "Audio", "Controls" };
        static constexpr int TAB_COUNT = 6;
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

            // Quality presets
            {
                const char* preset_labels[] = { "Low", "Medium", "High", "Ultra", "Custom" };
                int old_preset = prefs.quality_preset;
                if (ImGui::Combo("Quality Preset", &prefs.quality_preset, preset_labels, 5)) {
                    if (prefs.quality_preset != 4 && prefs.quality_preset != old_preset) {
                        // Apply preset values
                        switch (prefs.quality_preset) {
                            case 0: // Low
                                prefs.render_scale = 1; prefs.bloom_enabled = false;
                                prefs.physics_quality = 0; prefs.physics_skip = 2;
                                break;
                            case 1: // Medium
                                prefs.render_scale = 1; prefs.bloom_enabled = false;
                                prefs.physics_quality = 1; prefs.physics_skip = 1;
                                break;
                            case 2: // High
                                prefs.render_scale = 1; prefs.bloom_enabled = true;
                                prefs.physics_quality = 2; prefs.physics_skip = 0;
                                break;
                            case 3: // Ultra
                                prefs.render_scale = 2; prefs.bloom_enabled = true;
                                prefs.physics_quality = 2; prefs.physics_skip = 0;
                                break;
                        }
                    }
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Quick quality settings\nLow: best performance\nUltra: highest quality\nCustom: manual control");
            }

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::SeparatorText("Display");

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
            if (ImGui::Combo("Render Quality", &render_idx, render_labels, 4)) {
                prefs.render_scale = render_idx + 1;
                prefs.quality_preset = 4; // Custom
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Native: render at base resolution\n2x/3x/4x: higher resolution, downsampled\nHigher quality but uses more GPU memory");

            ImGui::Dummy(ImVec2(0, 8));
            const char* window_labels[] = { "Borderless Fullscreen", "Windowed" };
            ImGui::Combo("Window Mode", &prefs.window_mode, window_labels, 2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Alt+Enter to toggle at any time\nChange takes effect on next launch");

            // Monitor picker
            {
                int mon_count = 0;
                GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
                if (monitors && mon_count > 1) {
                    ImGui::Dummy(ImVec2(0, 8));
                    static std::vector<std::string> mon_labels;
                    static std::vector<const char*> mon_ptrs;
                    if (static_cast<int>(mon_labels.size()) != mon_count) {
                        mon_labels.clear();
                        mon_ptrs.clear();
                        for (int m = 0; m < mon_count; m++) {
                            const GLFWvidmode* vm = glfwGetVideoMode(monitors[m]);
                            const char* name = glfwGetMonitorName(monitors[m]);
                            char buf[256];
                            snprintf(buf, sizeof(buf), "%d: %s (%dx%d)", m + 1,
                                     name ? name : "Unknown", vm ? vm->width : 0, vm ? vm->height : 0);
                            mon_labels.push_back(buf);
                        }
                        mon_ptrs.resize(mon_count);
                        for (int m = 0; m < mon_count; m++)
                            mon_ptrs[m] = mon_labels[m].c_str();
                    }
                    prefs.preferred_monitor = std::clamp(prefs.preferred_monitor, 0, mon_count - 1);
                    ImGui::Combo("Monitor", &prefs.preferred_monitor, mon_ptrs.data(), mon_count);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Select display for fullscreen mode\nTakes effect on next fullscreen toggle or restart");
                }
            }

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Checkbox("VSync", &prefs.vsync);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Synchronize with display refresh rate\nReduces tearing but may cap FPS\nTakes effect immediately");

            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Checkbox("Bloom Glow", &prefs.bloom_enabled))
                prefs.quality_preset = 4;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Post-processing bloom effect\nAdds a soft glow around bright particles");
            ImGui::Checkbox("Wobbly Windows", &prefs.wobbly_windows);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Subtle floating animation for UI panels");

            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Visual Overlays");
            {
                bool show_bonds = !prefs.hide_bond_visuals;
                if (ImGui::Checkbox("Show Bond Lines", &show_bonds)) {
                    prefs.hide_bond_visuals = !show_bonds;
                    save_prefs();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle covalent bond line rendering\nPhysics (spring forces) still active when hidden");

                bool show_virtual = !prefs.hide_virtual_trails;
                if (ImGui::Checkbox("Show Virtual Particles", &show_virtual)) {
                    prefs.hide_virtual_trails = !show_virtual;
                    save_prefs();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle virtual particle rendering\nCasimir forces still active when hidden");

                bool show_entangle = !prefs.hide_entanglement_lines;
                if (ImGui::Checkbox("Show Entanglement Lines", &show_entangle)) {
                    prefs.hide_entanglement_lines = !show_entangle;
                    save_prefs();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle entanglement dashed line overlay\nVelocity coupling still active when hidden");
            }
        }

        // ── Tab 1: Performance ───────────────────────────────────────────
        else if (settings_tab == 1) {
            ImGui::Dummy(ImVec2(0, 6));

            // GPU selection + VRAM display
            if (vk_ctx_ && !vk_ctx_->gpu_list.empty()) {
                // Build GPU label list
                static std::vector<std::string> gpu_labels;
                static std::vector<const char*> gpu_ptrs;
                if (gpu_labels.size() != vk_ctx_->gpu_list.size() + 1) {
                    gpu_labels.clear();
                    gpu_ptrs.clear();
                    gpu_labels.push_back("Auto (recommended)");
                    for (const auto& g : vk_ctx_->gpu_list) {
                        char buf[256];
                        float vram_mb = static_cast<float>(g.vram_bytes) / (1024.0f * 1024.0f);
                        snprintf(buf, sizeof(buf), "%s (%.0f MB)", g.name.c_str(), vram_mb);
                        gpu_labels.push_back(buf);
                    }
                    gpu_ptrs.resize(gpu_labels.size());
                    for (size_t i = 0; i < gpu_labels.size(); i++)
                        gpu_ptrs[i] = gpu_labels[i].c_str();
                }
                int gpu_combo_idx = prefs.preferred_gpu + 1;  // -1=auto → 0, 0 → 1, etc.
                if (gpu_combo_idx < 0 || gpu_combo_idx >= static_cast<int>(gpu_ptrs.size()))
                    gpu_combo_idx = 0;
                if (ImGui::Combo("GPU", &gpu_combo_idx, gpu_ptrs.data(), static_cast<int>(gpu_ptrs.size()))) {
                    prefs.preferred_gpu = gpu_combo_idx - 1;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Select GPU device (requires restart to take effect)");

                // Show current GPU + VRAM
                int cur = vk_ctx_->selected_gpu_index;
                if (cur >= 0 && cur < static_cast<int>(vk_ctx_->gpu_list.size())) {
                    float vram_gb = static_cast<float>(vk_ctx_->gpu_list[cur].vram_bytes) / (1024.0f * 1024.0f * 1024.0f);
                    ImGui::TextColored(tc.text_dim, "Active: %s  |  VRAM: %.1f GB",
                        vk_ctx_->gpu_list[cur].name.c_str(), vram_gb);
                }
                ImGui::Dummy(ImVec2(0, 8));
            }

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
            if (ImGui::Combo("Physics Quality", &prefs.physics_quality, quality_labels, 3))
                prefs.quality_preset = 4;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Low: essential physics, skip expensive checks\nMedium: most interactions at reduced rate\nHigh: full simulation fidelity every frame");

            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::SliderInt("Physics Skip", &prefs.physics_skip, 0, 4))
                prefs.quality_preset = 4;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Run CPU physics every (N+1) frames\n0 = every frame (best quality)\nHigher = better FPS, less accurate");

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Checkbox("Spatial Grid", &prefs.spatial_grid);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Use spatial acceleration grid for neighbor searches\nGreatly improves CPU physics performance\nDisable only for debugging");

            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Particles");
            {
                int pc = static_cast<int>(std::round(prefs.particle_count_slider * prefs.particle_count_slider));
                char fmt[64];
                snprintf(fmt, sizeof(fmt), "%%.0f  (%d particles)", pc);
                if (ImGui::SliderFloat("Max Particles", &prefs.particle_count_slider, 10.0f, 316.0f, fmt))
                    save_prefs();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Drag to set max particle count (sqrt scale)\n10\xc2\xb2=100 up to 316\xc2\xb2=~100,000");
                ImGui::TextColored(tc.text_dim, "Active: %u  |  Dormant: %u",
                    active_particle_display, dormant_particle_display);
            }

            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Auto-Save");
            {
                const char* autosave_labels[] = { "Disabled", "2 min", "5 min", "10 min" };
                const int   autosave_values[] = { 0, 2, 5, 10 };
                int autosave_idx = 0;
                for (int i = 0; i < 4; i++) {
                    if (autosave_values[i] == prefs.autosave_interval) { autosave_idx = i; break; }
                }
                if (ImGui::Combo("Auto-Save Interval", &autosave_idx, autosave_labels, 4))
                    prefs.autosave_interval = autosave_values[autosave_idx];
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Automatically save at the chosen interval");
            }
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

        // ── Tab 3: Accessibility ─────────────────────────────────────────
        else if (settings_tab == 3) {
            ImGui::Dummy(ImVec2(0, 6));

            ImGui::TextColored(tc.accent, "Vision");
            ImGui::Separator();

            const char* cb_labels[] = { "Off", "Protanopia (red-weak)", "Deuteranopia (green-weak)", "Tritanopia (blue-weak)" };
            ImGui::Combo("Colorblind Mode", &prefs.colorblind_mode, cb_labels, 4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Adjust particle colors for color vision deficiency\nShifts problematic hue pairs to distinguishable alternatives");

            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Checkbox("High Contrast", &prefs.high_contrast);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Increase UI contrast\nThicker borders, brighter text, bolder accents");

            ImGui::Dummy(ImVec2(0, 14));
            ImGui::TextColored(tc.accent, "Motion");
            ImGui::Separator();

            if (ImGui::Checkbox("Reduced Motion", &prefs.reduced_motion)) {
                if (prefs.reduced_motion) prefs.wobbly_windows = false;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Disable UI animations and visual effects\nTurns off wobbly windows, splash particles,\nand gravitational wave ripple visualization");

            ImGui::Dummy(ImVec2(0, 14));
            ImGui::TextColored(tc.accent, "Input");
            ImGui::Separator();

            ImGui::SliderFloat("Mouse Sensitivity", &prefs.mouse_sensitivity, 0.1f, 3.0f, "%.1fx");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Adjust camera pan speed when dragging\n1.0x = default speed");
        }

        // ── Tab 4: Audio & Log ───────────────────────────────────────────
        else if (settings_tab == 4) {
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

            ImGui::TextColored(tc.accent, "Sound Effects");
            ImGui::Separator();

            if (ImGui::Checkbox("Mute SFX", &prefs.sfx_muted)) {
                if (audio_ptr) audio_ptr->sfx_muted = prefs.sfx_muted;
            }

            if (!prefs.sfx_muted) {
                float sfx_pct = prefs.sfx_volume * 100.0f;
                if (ImGui::SliderFloat("SFX Volume", &sfx_pct, 0.0f, 100.0f, "%.0f%%")) {
                    prefs.sfx_volume = sfx_pct / 100.0f;
                    if (audio_ptr) audio_ptr->set_sfx_volume(prefs.sfx_volume);
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
                ImGui::SetTooltip("Append all physics events to event_log.txt\nas they occur (timestamped, one per line).");
        }

        // ── Tab 5: Controls ─────────────────────────────────────────────
        else if (settings_tab == 5) {
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextColored(tc.accent, "Keyboard Shortcuts");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));

            if (ImGui::BeginTable("##KeyBindings", 2,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < KACT_COUNT; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", KEY_ACTION_NAMES[i]);
                    ImGui::TableSetColumnIndex(1);

                    // Fullscreen toggle is non-rebindable (GLFW callback)
                    if (i == KACT_FULLSCREEN_TOGGLE) {
                        char label[64];
                        format_keybinding(keybindings.bindings[i], label, sizeof(label));
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 0.8f), "%s", label);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("This shortcut cannot be reassigned.");
                        continue;
                    }

                    if (rebinding_action == i) {
                        // "Press a key..." mode
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press a key...");

                        // Cancel with Escape
                        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                            rebinding_action = -1;
                        } else {
                            // Scan for any key press
                            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                                auto ik = static_cast<ImGuiKey>(k);
                                // Skip mouse buttons and modifier-only keys
                                if (ik >= ImGuiKey_MouseLeft && ik <= ImGuiKey_MouseMiddle) continue;
                                if (ik == ImGuiKey_LeftCtrl || ik == ImGuiKey_RightCtrl) continue;
                                if (ik == ImGuiKey_LeftShift || ik == ImGuiKey_RightShift) continue;
                                if (ik == ImGuiKey_LeftAlt || ik == ImGuiKey_RightAlt) continue;
                                if (ik == ImGuiKey_LeftSuper || ik == ImGuiKey_RightSuper) continue;

                                if (ImGui::IsKeyPressed(ik, false)) {
                                    KeyBinding nb;
                                    nb.key   = ik;
                                    nb.ctrl  = ImGui::GetIO().KeyCtrl;
                                    nb.shift = ImGui::GetIO().KeyShift;
                                    nb.alt   = ImGui::GetIO().KeyAlt;

                                    // Swap-on-conflict
                                    for (int j = 0; j < KACT_COUNT; ++j) {
                                        if (j == i || j == KACT_FULLSCREEN_TOGGLE) continue;
                                        auto& ob = keybindings.bindings[j];
                                        if (ob.key == nb.key && ob.ctrl == nb.ctrl
                                            && ob.shift == nb.shift && ob.alt == nb.alt) {
                                            ob = keybindings.bindings[i];  // swap
                                            break;
                                        }
                                    }
                                    keybindings.bindings[i] = nb;
                                    rebinding_action = -1;
                                    save_keybindings();
                                    break;
                                }
                            }
                        }
                    } else {
                        // Show current binding as a clickable button
                        char label[64];
                        format_keybinding(keybindings.bindings[i], label, sizeof(label));
                        char btn_id[80];
                        snprintf(btn_id, sizeof(btn_id), "%s##kb%d", label, i);
                        if (ImGui::Button(btn_id, ImVec2(170, 0))) {
                            rebinding_action = i;
                        }
                    }
                }
                ImGui::EndTable();
            }

            ImGui::Dummy(ImVec2(0, 12));
            if (ImGui::Button("Reset to Defaults", ImVec2(panel_w - 20.0f, 0))) {
                keybindings.set_defaults();
                save_keybindings();
            }
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

        // Version + hint
        const char* ver = "v" APP_VERSION;
        ImVec2 ver_size = ImGui::CalcTextSize(ver);
        ImGui::SetCursorPos(ImVec2(cx - ver_size.x * 0.5f, panel_bottom + 56.0f));
        ImGui::TextColored(ImVec4(0.4f, 0.43f, 0.5f, 0.5f), "%s", ver);

        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(cx - hint_size.x * 0.5f, panel_bottom + 74.0f));
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 0.6f), "%s", hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Loading Overlay ─────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_loading_overlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.92f));
    if (ImGui::Begin("##LoadingOverlay", nullptr, flags)) {
        float cx = io.DisplaySize.x * 0.5f;
        float cy = io.DisplaySize.y * 0.5f;

        // Animated spinner
        float t = static_cast<float>(ImGui::GetTime());
        const char* spinner_frames[] = { "|", "/", "-", "\\" };
        int frame = static_cast<int>(t * 8.0f) % 4;

        ImGui::SetCursorPos(ImVec2(cx - 60.0f, cy - 20.0f));
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s  %s",
                           spinner_frames[frame], loading_message);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Tutorial Overlay ────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_tutorial_overlay() {
    if (!tutorial_ptr || !tutorial_ptr->active) return;

    ImGuiIO& io = ImGui::GetIO();
    const auto& step = tutorial_ptr->current();
    const auto& tc = get_theme(std::clamp(prefs.theme, 0, total_theme_count() - 1));

    float panel_w = std::min(500.0f, io.DisplaySize.x - 20.0f);
    float panel_h = 160.0f;
    float panel_x = std::max(10.0f, (io.DisplaySize.x - panel_w) * 0.5f);
    float panel_y = std::max(10.0f, io.DisplaySize.y - panel_h - 80.0f);

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tc.accent.x, tc.accent.y, tc.accent.z, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    if (ImGui::Begin("##Tutorial", nullptr, flags)) {
        // Step counter
        ImGui::TextColored(ImVec4(tc.accent.x, tc.accent.y, tc.accent.z, 0.7f),
                           "Step %d of %d", tutorial_ptr->current_step + 1, tutorial_ptr->step_count());

        // Title
        ImGui::TextColored(tc.accent, "%s", step.title);
        ImGui::Separator();

        // Description
        ImGui::TextWrapped("%s", step.text);

        // Buttons
        ImGui::SetCursorPosY(panel_h - 35.0f);
        if (step.check_complete == nullptr) {
            if (ImGui::Button("Next", ImVec2(80, 24))) {
                tutorial_ptr->advance();
                if (!tutorial_ptr->active) {
                    prefs.tutorial_done = true;
                    save_prefs();
                }
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(Complete the action to continue)");
        }

        ImGui::SameLine(panel_w - 120.0f);
        if (ImGui::Button("Skip Tutorial", ImVec2(100, 24))) {
            tutorial_ptr->skip();
            prefs.tutorial_done = true;
            save_prefs();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
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

    // Link the most recent notification to this event (if unlinked)
    if (!notifications.empty() && notifications.back().event_idx == -1)
        notifications.back().event_idx = static_cast<int32_t>(decay_log.size()) - 1;
}

void PhysicsInterface::save_event_to_disk(const char* desc, DecayEventType type) {
    namespace fs = std::filesystem;
    static const char* TYPE_TAGS[] = {
        "DECAY", "NUCLEAR", "FUSION", "FISSION", "ANNIHILATION",
        "PHOTOELECTRIC", "SPALLATION", "PAIR_PROD", "PION_PROD",
        "VMD", "PHOTODISINT", "BOND_FORM", "BOND_BREAK",
        "BREMSSTRAHLUNG", "NEUTRINO", "WEAK_SCATTER", "ELECTRON_HOLE",
        "CARRIER_EM", "CARRIER_QCD", "CARRIER_WEAK",
        "CARRIER_GRAVITY", "CARRIER_HIGGS", "CARRIER_NUCLEAR",
        "QUASIPARTICLE"
    };
    const std::string& data_dir = get_data_dir();
    std::ofstream f((data_dir + "event_log.txt").c_str(), std::ios::app);
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
    float start_x = std::max(10.0f, io.DisplaySize.x - card_w - 10.0f);
    float start_y = 50.0f;

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

            // Click notification → open event log at linked event
            if (ImGui::IsWindowHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                show_decay_log = true;
                if (n.event_idx >= 0 && n.event_idx < static_cast<int32_t>(decay_log.size())) {
                    scroll_to_event_idx = n.event_idx;
                    expanded_event_idx = n.event_idx;
                }
                notifications.erase(notifications.begin() + i);
                ImGui::End();
                ImGui::PopStyleVar(3);
                ImGui::PopStyleColor(2);
                break;  // iterator invalidated, redraw next frame
            }

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
        std::string data_dir = get_data_dir();
        if (fs::is_directory(data_dir, ec))
            browse_current_dir = data_dir;
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

    TaskbarWindow sl_tw = show_save_dialog ? TW_SAVE_DIALOG : TW_LOAD_DIALOG;
    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[sl_tw]);
        ImGui::SetNextWindowSize(tile_size_[sl_tw]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    }

    const char* title = show_molecule_import_dialog ? "Import Molecule###SaveLoad"
                      : show_import_dialog ? "Import Element###SaveLoad"
                      : show_save_dialog  ? "Save Simulation###SaveLoad"
                                          : "Load Simulation###SaveLoad";
    bool open = true;

    bool sl_vis = ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse);
    record_window_rect(sl_tw);
    if (sl_vis) {
        draw_minimize_button(sl_tw);
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

// ══════════════════════════════════════════════════════════════════════════════
// ── Scenario Menu ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_scenario_menu() {
    if (!scenarios_ptr) { show_scenario_menu = false; return; }

    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen overlay background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##ScenarioOverlay", nullptr, overlay_flags)) {
        float cx = io.DisplaySize.x * 0.5f;

        // Title
        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale = 1.8f;
        ImGui::PushFont(ImGui::GetFont());
        const char* title = "SCENARIOS";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, 30.0f));
        ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 1.0f), "%s", title);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();

        // Category tabs
        static const char* CATEGORIES[] = { "All", "Nuclear", "Chemistry", "Cosmology", "Sandbox" };
        static int selected_category = 0;

        float tab_y = 75.0f;
        float tab_w = 100.0f;
        float total_tab_w = 5 * tab_w + 4 * 8.0f;
        float tab_start_x = cx - total_tab_w * 0.5f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (int c = 0; c < 5; c++) {
            ImGui::SetCursorPos(ImVec2(tab_start_x + c * (tab_w + 8.0f), tab_y));
            bool active = (selected_category == c);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.60f, 0.95f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.12f, 0.20f, 0.80f));
            }
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.30f, 0.55f, 0.95f));
            if (ImGui::Button(CATEGORIES[c], ImVec2(tab_w, 28.0f))) {
                selected_category = c;
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::PopStyleVar();

        // Scenario cards grid
        int count = ScenarioManager::scenario_count();
        float card_w = 340.0f;
        float card_h = 155.0f;
        float card_spacing = 16.0f;
        int cols = std::max(1, static_cast<int>((io.DisplaySize.x - 80.0f) / (card_w + card_spacing)));
        float grid_w = cols * card_w + (cols - 1) * card_spacing;
        float grid_start_x = cx - grid_w * 0.5f;
        float grid_start_y = 120.0f;

        // Category colors
        auto cat_color = [](const char* cat) -> ImVec4 {
            if (std::strcmp(cat, "Nuclear") == 0)    return ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
            if (std::strcmp(cat, "Chemistry") == 0)  return ImVec4(0.3f, 0.9f, 0.5f, 1.0f);
            if (std::strcmp(cat, "Cosmology") == 0)  return ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
            if (std::strcmp(cat, "Sandbox") == 0)    return ImVec4(0.9f, 0.8f, 0.3f, 1.0f);
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        };

        int visible_idx = 0;
        for (int i = 0; i < count; i++) {
            const Scenario& s = ScenarioManager::get(i);

            // Category filter
            if (selected_category > 0) {
                if (std::strcmp(s.category, CATEGORIES[selected_category]) != 0)
                    continue;
            }

            int col = visible_idx % cols;
            int row = visible_idx / cols;
            float x = grid_start_x + col * (card_w + card_spacing);
            float y = grid_start_y + row * (card_h + card_spacing);
            visible_idx++;

            // Card background
            ImVec2 card_min(x, y);
            ImVec2 card_max(x + card_w, y + card_h);
            ImDrawList* dl = ImGui::GetWindowDrawList();

            bool hovered = ImGui::IsMouseHoveringRect(card_min, card_max);
            ImU32 bg_col = hovered
                ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.16f, 0.28f, 0.95f))
                : ImGui::ColorConvertFloat4ToU32(ImVec4(0.08f, 0.10f, 0.18f, 0.90f));
            dl->AddRectFilled(card_min, card_max, bg_col, 8.0f);

            // Border
            ImVec4 cc = cat_color(s.category);
            ImU32 border = hovered
                ? ImGui::ColorConvertFloat4ToU32(ImVec4(cc.x, cc.y, cc.z, 0.8f))
                : ImGui::ColorConvertFloat4ToU32(ImVec4(cc.x, cc.y, cc.z, 0.3f));
            dl->AddRect(card_min, card_max, border, 8.0f, 0, 1.5f);

            // Category badge
            ImVec2 badge_pos(x + 10.0f, y + 8.0f);
            dl->AddText(badge_pos,
                ImGui::ColorConvertFloat4ToU32(ImVec4(cc.x, cc.y, cc.z, 0.8f)),
                s.category);

            // Task count badge (right side)
            if (s.task_count > 0) {
                char task_badge[24];
                std::snprintf(task_badge, sizeof(task_badge), "%d Tasks", s.task_count);
                ImVec2 tb_size = ImGui::CalcTextSize(task_badge);
                dl->AddText(ImVec2(x + card_w - tb_size.x - 10.0f, y + 8.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.6f, 0.75f, 0.7f)),
                    task_badge);
            }

            // Completion checkmark (top-right corner)
            bool completed = achievements_ptr && i < 20
                && achievements_ptr->scenarios_completed[i];
            if (completed) {
                dl->AddText(ImVec2(x + card_w - 22.0f, y + 22.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.95f, 0.4f, 0.9f)), "OK");
            }

            // Scenario name
            ImGui::SetCursorPos(ImVec2(x + 10.0f, y + 26.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 1.0f, 1.0f));
            float s2 = ImGui::GetFont()->Scale;
            ImGui::GetFont()->Scale = 1.2f;
            ImGui::PushFont(ImGui::GetFont());
            ImGui::Text("%s", s.name);
            ImGui::GetFont()->Scale = s2;
            ImGui::PopFont();
            ImGui::PopStyleColor();

            // Storyline subtitle (if available)
            if (s.storyline) {
                ImGui::SetCursorPos(ImVec2(x + 10.0f, y + 46.0f));
                ImGui::TextColored(ImVec4(0.55f, 0.6f, 0.75f, 0.7f), "\"%s\"", s.storyline);
            }

            // Description (wrapped)
            float desc_y = s.storyline ? y + 62.0f : y + 50.0f;
            ImGui::SetCursorPos(ImVec2(x + 10.0f, desc_y));
            ImGui::PushTextWrapPos(x + card_w - 10.0f);
            ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.78f, 1.0f), "%s", s.description);
            ImGui::PopTextWrapPos();

            // Start button (or Replay if completed)
            ImGui::SetCursorPos(ImVec2(x + card_w - 75.0f, y + card_h - 36.0f));
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.55f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.45f, 0.70f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.30f, 0.50f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            const char* btn_label = completed ? "Replay" : "Start";
            if (ImGui::Button(btn_label, ImVec2(60.0f, 28.0f))) {
                show_scenario_menu = false;
                request_scenario_start = i;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }

        // Back button (bottom center)
        float back_y = io.DisplaySize.y - 60.0f;
        ImGui::SetCursorPos(ImVec2(cx - 60.0f, back_y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.40f, 0.95f));
        if (ImGui::Button("Back", ImVec2(120.0f, 36.0f))) {
            show_scenario_menu = false;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

        // ESC to close
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_scenario_menu = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Scenario Goal HUD ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_scenario_goal_hud() {
    if (!scenarios_ptr || !scenarios_ptr->active) return;

    const Scenario& s = scenarios_ptr->current();
    const ScenarioTask* task = scenarios_ptr->current_task_ptr();
    int total = scenarios_ptr->total_tasks();
    int cur = scenarios_ptr->current_task;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();

    float cx = io.DisplaySize.x * 0.5f;
    float hud_y = 48.0f;  // below 42px top bar

    // Build HUD text
    char buf[256];
    if (scenarios_ptr->goal_complete) {
        std::snprintf(buf, sizeof(buf), "%s  --  SCENARIO COMPLETE!", s.name);
    } else if (scenarios_ptr->task_just_completed && cur < total - 1) {
        std::snprintf(buf, sizeof(buf), "%s  |  Task %d/%d  --  Task Complete!", s.name, cur + 1, total);
    } else if (total > 0 && task && task->goal_text) {
        std::snprintf(buf, sizeof(buf), "%s  |  Task %d/%d  |  %s", s.name, cur + 1, total, task->goal_text);
    } else {
        std::snprintf(buf, sizeof(buf), "%s  |  Sandbox", s.name);
    }

    ImVec2 text_size = ImGui::CalcTextSize(buf);
    float pad_x = 16.0f, pad_y = 6.0f;
    float bg_w = text_size.x + pad_x * 2;
    float bg_h = text_size.y + pad_y * 2;

    ImVec2 bg_min(cx - bg_w * 0.5f, hud_y);
    ImVec2 bg_max(cx + bg_w * 0.5f, hud_y + bg_h);

    // Background
    ImU32 bg_col;
    if (scenarios_ptr->goal_complete) {
        bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.20f, 0.08f, 0.85f));
    } else if (scenarios_ptr->task_just_completed) {
        bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.04f, 0.12f, 0.18f, 0.85f));
    } else {
        bg_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.04f, 0.06f, 0.12f, 0.85f));
    }
    fg->AddRectFilled(bg_min, bg_max, bg_col, 6.0f);

    // Border
    ImU32 border_col;
    if (scenarios_ptr->goal_complete) {
        border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.9f, 0.3f, 0.7f));
    } else if (scenarios_ptr->task_just_completed) {
        border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.8f, 1.0f, 0.7f));
    } else {
        border_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f));
    }
    fg->AddRect(bg_min, bg_max, border_col, 6.0f, 0, 1.0f);

    // Task progress dots (below border, inside HUD)
    if (total > 1) {
        float dot_y = bg_max.y - 4.0f;
        float dot_spacing = 10.0f;
        float dots_w = (total - 1) * dot_spacing;
        float dot_x = cx - dots_w * 0.5f;
        for (int i = 0; i < total; i++) {
            ImU32 dot_col;
            if (i < cur || (i == cur && scenarios_ptr->task_just_completed)) {
                dot_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.9f, 0.4f, 0.9f));  // completed
            } else if (i == cur) {
                dot_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.7f, 1.0f, 0.9f));  // current
            } else {
                dot_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.35f, 0.45f, 0.6f)); // pending
            }
            fg->AddCircleFilled(ImVec2(dot_x + i * dot_spacing, dot_y), 2.5f, dot_col);
        }
    }

    // Text
    ImU32 text_col;
    if (scenarios_ptr->goal_complete) {
        text_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
    } else if (scenarios_ptr->task_just_completed) {
        text_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.9f, 1.0f, 1.0f));
    } else {
        text_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.85f, 0.95f, 1.0f));
    }
    fg->AddText(ImVec2(cx - text_size.x * 0.5f, hud_y + pad_y), text_col, buf);

    // Narrative text (fades over 5 seconds, shown below HUD)
    float narrative_alpha = 0.0f;
    const char* narrative_text = nullptr;
    if (scenarios_ptr->narrative_timer < 5.0f) {
        // Show current task narrative or intro storyline
        if (task && task->narrative) {
            narrative_text = task->narrative;
        } else if (cur == 0 && s.storyline) {
            narrative_text = s.storyline;
        }
        if (narrative_text) {
            // Fade in over 0.5s, hold, fade out over last 1.5s
            if (scenarios_ptr->narrative_timer < 0.5f) {
                narrative_alpha = scenarios_ptr->narrative_timer / 0.5f;
            } else if (scenarios_ptr->narrative_timer < 3.5f) {
                narrative_alpha = 1.0f;
            } else {
                narrative_alpha = 1.0f - (scenarios_ptr->narrative_timer - 3.5f) / 1.5f;
            }
        }
    }
    float below_y = bg_max.y + 6.0f;

    if (narrative_text && narrative_alpha > 0.01f) {
        ImVec2 nar_size = ImGui::CalcTextSize(narrative_text);
        // Wrap long narratives
        float max_w = io.DisplaySize.x * 0.6f;
        if (nar_size.x > max_w) {
            nar_size = ImGui::CalcTextSize(narrative_text, nullptr, false, max_w);
        }
        float nar_y = below_y;
        ImVec2 nar_bg_min(cx - nar_size.x * 0.5f - 12.0f, nar_y);
        ImVec2 nar_bg_max(cx + nar_size.x * 0.5f + 12.0f, nar_y + nar_size.y + 10.0f);
        fg->AddRectFilled(nar_bg_min, nar_bg_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.02f, 0.03f, 0.06f, 0.7f * narrative_alpha)), 4.0f);
        // Italic-style color for narrative
        fg->AddText(nullptr, 0.0f, ImVec2(cx - nar_size.x * 0.5f, nar_y + 5.0f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.75f, 0.85f, 0.9f * narrative_alpha)),
            narrative_text, nullptr, max_w);
        below_y = nar_bg_max.y + 4.0f;
    }

    // Hint (shown below narrative/goal when not yet complete)
    if (!scenarios_ptr->goal_complete && !scenarios_ptr->task_just_completed
        && task && task->hint_text) {
        ImVec2 hint_size = ImGui::CalcTextSize(task->hint_text);
        float hint_y = below_y;
        ImVec2 hint_bg_min(cx - hint_size.x * 0.5f - 8.0f, hint_y);
        ImVec2 hint_bg_max(cx + hint_size.x * 0.5f + 8.0f, hint_y + hint_size.y + 6.0f);
        fg->AddRectFilled(hint_bg_min, hint_bg_max,
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.03f, 0.04f, 0.08f, 0.75f)), 4.0f);
        fg->AddText(ImVec2(cx - hint_size.x * 0.5f, hint_y + 3.0f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.60f, 0.70f, 0.8f)), task->hint_text);
    }

    // End scenario button (small, right side of HUD)
    float end_btn_x = bg_max.x + 8.0f;
    float end_btn_y = hud_y;
    ImGui::SetNextWindowPos(ImVec2(end_btn_x, end_btn_y));
    ImGui::SetNextWindowSize(ImVec2(24.0f, bg_h));
    ImGuiWindowFlags btn_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##ScenarioEndBtn", nullptr, btn_flags)) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.4f, 0.4f, 0.8f));
        if (ImGui::Button("X", ImVec2(20.0f, bg_h - 4.0f))) {
            scenarios_ptr->end();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("End scenario");
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Credits Screen ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_credits() {
    ImGuiIO& io = ImGui::GetIO();
    const auto& tc = get_theme(std::clamp(prefs.theme, 0, total_theme_count() - 1));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.04f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##Credits", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    {
        float cx = io.DisplaySize.x * 0.5f;

        // Title
        ImGui::PushFont(title_font ? title_font : ImGui::GetFont());
        const char* title = "Credits";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        ImGui::SetCursorPos(ImVec2(cx - title_size.x * 0.5f, 40.0f));
        ImGui::TextColored(tc.accent, "%s", title);
        ImGui::PopFont();

        // Scrollable credits area
        float panel_w = 500.0f;
        float panel_top = 100.0f;
        float panel_bottom = io.DisplaySize.y - 100.0f;
        ImGui::SetCursorPos(ImVec2(cx - panel_w * 0.5f, panel_top));
        ImGui::BeginChild("##CreditsScroll", ImVec2(panel_w, panel_bottom - panel_top), false);

        // Embedded credits text (works without file access)
        static const char* CREDITS_TEXT[] = {
            "ENGINE & LIBRARIES",
            "",
            "Vulkan SDK  -  Graphics & Compute API",
            "  Apache License 2.0",
            "",
            "GLFW  -  Window, Input & Context",
            "  zlib/libpng License",
            "",
            "GLM  -  OpenGL Mathematics",
            "  MIT License",
            "",
            "Dear ImGui  -  Immediate Mode GUI",
            "  MIT License",
            "",
            "miniaudio  -  Audio Playback Engine",
            "  MIT-0 / Public Domain",
            "",
            "stb_image / stb_image_write  -  Image I/O",
            "  MIT License / Public Domain",
            "",
            "cJSON  -  JSON Parser (Repository API)",
            "  MIT License",
            "",
            "libcurl  -  HTTP Client (Repository)",
            "  curl License (MIT-style)",
            "",
            "BUILD TOOLS",
            "",
            "CMake  -  Build System Generator",
            "  BSD 3-Clause License",
            "",
            "glslc  -  SPIR-V Shader Compiler",
            "  Apache License 2.0",
            "",
        };

        for (const char* line : CREDITS_TEXT) {
            if (line[0] == '\0') {
                ImGui::Dummy(ImVec2(0, 4));
            } else if (line[0] != ' ') {
                // Section header or library name
                bool is_header = (line == CREDITS_TEXT[0] || std::string(line) == "BUILD TOOLS");
                if (is_header) {
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::TextColored(tc.accent, "%s", line);
                    ImGui::Separator();
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.95f), "%s", line);
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 0.8f), "%s", line);
            }
        }

        ImGui::EndChild();

        // Back button
        float btn_w = 160.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, panel_bottom + 15.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("Back", ImVec2(btn_w, 36.0f))) {
            show_credits_ = false;
            show_pause_menu = true;
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── How-To / About Guide ────────────────────────────────────────────────────
void PhysicsInterface::draw_howto() {
    const auto& tc = get_theme(prefs.theme);
    auto& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.06f, 0.97f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##howto", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    {
        float cx = io.DisplaySize.x * 0.5f;
        float panel_w = 640.0f;
        float panel_top = 50.0f;
        float panel_bottom = io.DisplaySize.y - 80.0f;
        float panel_h = panel_bottom - panel_top;

        // Title
        if (title_font) ImGui::PushFont(title_font);
        const char* title = "How To Play";
        float tw = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPos(ImVec2(cx - tw * 0.5f, 12.0f));
        ImGui::TextColored(tc.accent_bright, "%s", title);
        if (title_font) ImGui::PopFont();

        // Scrollable content
        ImGui::SetCursorPos(ImVec2(cx - panel_w * 0.5f, panel_top));
        ImGui::BeginChild("##howto_scroll", ImVec2(panel_w, panel_h), false);
        ImGui::PushTextWrapPos(panel_w - 20.0f);

        auto section_header = [&](const char* text) {
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::TextColored(tc.accent, "%s", text);
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));
        };

        auto body = [&](const char* text) {
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.88f, 1.0f), "%s", text);
        };

        auto hint = [&](const char* text) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 0.9f), "  %s", text);
        };

        // ── Section 1: Getting Started ──
        section_header("Getting Started");
        body("Particle Playground is an interactive particle physics simulator. "
             "You can spawn fundamental particles — quarks, leptons, and bosons — "
             "and watch them interact through the fundamental forces of nature.");
        ImGui::Dummy(ImVec2(0, 4));
        body("Particles bind into nuclei through the strong force, capture electrons "
             "to form atoms, and atoms bond into molecules through covalent bonds. "
             "All interactions emerge from the underlying physics.");

        // ── Section 2: Controls ──
        section_header("Controls");
        body("Keyboard shortcuts can be customized in Settings > Controls.");
        ImGui::Dummy(ImVec2(0, 6));

        if (ImGui::BeginTable("##howto_keys", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            char buf[64];
            for (int i = 0; i < KACT_COUNT; i++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(KEY_ACTION_NAMES[i]);
                ImGui::TableSetColumnIndex(1);
                format_keybinding(keybindings.bindings[i], buf, sizeof(buf));
                ImGui::TextUnformatted(buf);
            }
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, 4));
        hint("Mouse: Left-click to spawn, Right-drag to pan, Scroll to zoom");
        hint("Gamepad: Left stick = pan, Triggers = zoom, Bumpers = switch tabs");

        // ── Section 3: Spawning Particles ──
        section_header("Spawning Particles");
        body("Click anywhere in the world to spawn particles of the currently "
             "selected type. Open the Spawn Menu to choose from:");
        ImGui::Dummy(ImVec2(0, 4));
        hint("Standard Model particles: quarks, leptons, gauge bosons, Higgs");
        hint("Composite particles: protons, neutrons, pions, kaons");
        hint("Periodic Table: click any element to spawn a complete atom");
        hint("Groups: pre-built structures (alpha particles, deuterium, etc.)");
        hint("Molecules: import .ppmol files for complex molecular structures");

        // ── Section 4: Nuclear Physics ──
        section_header("Nuclear Physics");
        body("Protons and neutrons attract via the Yukawa (strong nuclear) force "
             "and bind into nuclei when close enough. The simulation supports:");
        ImGui::Dummy(ImVec2(0, 4));
        hint("Fusion: light nuclei merge when pushed together with enough energy");
        hint("Fission: heavy nuclei (Z > 83) can spontaneously split");
        hint("Decay: unstable particles decay (muons, taus, heavy quarks, W/Z bosons)");
        hint("Annihilation: particle-antiparticle pairs annihilate into photons");
        hint("Virtual Pairs: high-energy regions can spontaneously create particle pairs");

        // ── Section 5: Molecules & Bonds ──
        section_header("Molecules & Bonds");
        body("Atoms can form covalent bonds when their electron clouds overlap. "
             "Each element has a valence determining how many bonds it can form.");
        ImGui::Dummy(ImVec2(0, 4));
        hint("Bonds form automatically when atoms are within bonding radius");
        hint("Bond strength and rest length can be tuned in simulation settings");
        hint("Molecules are tracked and displayed in the bottom status bar");

        // ── Section 6: Molecule Tools ──
        section_header("Molecule Tools (ppmol_gen)");
        body("The ppmol_gen tool generates .ppmol molecule files that can be "
             "imported into the simulation. Two versions are available:");
        ImGui::Dummy(ImVec2(0, 4));
        hint("C++ version:  tools/ppmol/ppmol_gen");
        hint("Python version: tools/ppmol/ppmol_gen.py");
        ImGui::Dummy(ImVec2(0, 4));
        body("Usage examples:");
        hint("ppmol_gen gen water.ppmol H2O        — generate from formula");
        hint("ppmol_gen dl caffeine.ppmol caffeine  — download from PubChem");
        hint("ppmol_gen info molecule.ppmol         — inspect a .ppmol file");
        ImGui::Dummy(ImVec2(0, 4));
        body("Import molecules via Spawn Menu > Molecules section, or "
             "File > Import Molecule from the pause menu.");

        // ── Section 7: Force Objects ──
        section_header("Force Objects");
        body("Place persistent force-generating objects using the Tools popup "
             "(wrench icon in the bottom bar):");
        ImGui::Dummy(ImVec2(0, 4));
        hint("Gravity Well: attracts all massive particles");
        hint("EM Field: uniform electric + magnetic field region");
        hint("Particle Accelerator: high-energy linear beam");
        hint("Magnetic Bottle: confines charged particles");
        hint("Heat Source / Cold Sink: local temperature control");
        hint("Black Hole: extreme gravity with event horizon");
        hint("Gravitational Wave: periodic spacetime ripple");
        hint("Dark Energy Void: localized expansion effect");
        hint("Force Wall: impenetrable barrier");

        // ── Section 8: Environments ──
        section_header("Environments");
        body("The Settings menu (F2) offers environment presets that configure "
             "the simulation for different physical scenarios:");
        ImGui::Dummy(ImVec2(0, 4));
        hint("Lab (default), Deep Space, Stellar Core, Neutron Star");
        hint("Early Universe, Quark Epoch, Electroweak, Cosmic Void");
        hint("Magnetar, Accretion Disk, Bose-Einstein Condensate");
        hint("Dark Sector, SUSY Sector");

        // ── Section 9: Save & Load ──
        section_header("Save & Load");
        body("Save and load simulation states in .ppsg format:");
        ImGui::Dummy(ImVec2(0, 4));
        char save_buf[64], load_buf[64];
        format_keybinding(keybindings.bindings[KACT_SAVE], save_buf, sizeof(save_buf));
        format_keybinding(keybindings.bindings[KACT_LOAD], load_buf, sizeof(load_buf));
        ImGui::Text("  Quick Save: %s    Quick Load: %s", save_buf, load_buf);
        hint("Also available from the pause menu and bottom bar");
        hint("Auto-save interval configurable in Settings > Performance");

        // ── Section 10: About ──
        section_header("About");
        body("Particle Playground");
        hint("A real-time particle physics simulator");
        ImGui::Dummy(ImVec2(0, 4));
        body("All particle interactions emerge from simulated fundamental forces: "
             "electromagnetism, strong nuclear (Yukawa + QCD), weak nuclear, "
             "and gravity. No predefined behaviors — just physics.");
        ImGui::Dummy(ImVec2(0, 8));
        hint("See Credits from the pause menu for full attribution.");

        ImGui::PopTextWrapPos();
        ImGui::EndChild();

        // Back button
        float btn_w = 160.0f;
        ImGui::SetCursorPos(ImVec2(cx - btn_w * 0.5f, panel_bottom + 15.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button("Back##howto", ImVec2(btn_w, 36.0f))) {
            show_howto = false;
            show_pause_menu = true;
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
