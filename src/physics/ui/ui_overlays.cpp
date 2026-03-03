#include "physics/ui/interface.h"
#include "physics/core/phys_particles.h"
#include "physics/ui/ui_data.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ── Visualization overlays ───────────────────────────────────────────────────
// Split from interface.cpp: measurement overlays, energy heatmap,
// velocity field, magnetic field, gravity map, and other visual overlays.

void PhysicsInterface::draw_measurement_overlays(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // ── Thermometer probes ───────────────────────────────────────────────
    for (auto& probe : thermo_probes) {
        ImVec2 center = w2s(probe.world_pos);
        float screen_r = probe.radius * cfg.current_camera_zoom * scx;
        if (center.x < -screen_r || center.x > ww + screen_r ||
            center.y < -screen_r || center.y > wh + screen_r) continue;

        ImU32 ring_col = IM_COL32(255, 150, 50, 120);
        fg->AddCircle(center, screen_r, ring_col, 48, 1.5f);

        // Label
        char buf[64];
        snprintf(buf, sizeof(buf), "T: %.0f K (%u)", probe.local_temp, probe.local_count);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float lx = center.x - ts.x * 0.5f, ly = center.y - screen_r - 20.0f;
        fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(lx, ly), IM_COL32(255, 180, 80, 255), buf);
    }

    // ── Velocity meters ──────────────────────────────────────────────────
    if (readback_positions_ptr && readback_velocities && readback_energies_ptr) {
        for (auto& vm : velocity_meters) {
            if (!vm.active || vm.particle_idx < 0 ||
                static_cast<uint32_t>(vm.particle_idx) >= readback_count) continue;
            uint32_t pi = static_cast<uint32_t>(vm.particle_idx);
            if (readback_energies_ptr[pi] <= 0.0f) { vm.active = false; continue; }

            glm::vec2 pos = readback_positions_ptr[pi];
            glm::vec2 vel = readback_velocities[pi];
            ImVec2 scr = w2s(pos);

            float speed = glm::length(vel);

            // Arrow
            if (speed > 0.1f) {
                glm::vec2 dir = vel / speed;
                float arrow_len = std::min(speed * 0.5f, 80.0f) * cfg.current_camera_zoom * scx;
                ImVec2 tip = ImVec2(scr.x + dir.x * arrow_len, scr.y + dir.y * arrow_len);
                fg->AddLine(scr, tip, IM_COL32(80, 255, 130, 200), 2.0f);
                // Arrowhead
                glm::vec2 perp(-dir.y, dir.x);
                float ah = 6.0f;
                fg->AddTriangleFilled(
                    tip,
                    ImVec2(tip.x - dir.x * ah + perp.x * ah * 0.5f,
                           tip.y - dir.y * ah + perp.y * ah * 0.5f),
                    ImVec2(tip.x - dir.x * ah - perp.x * ah * 0.5f,
                           tip.y - dir.y * ah - perp.y * ah * 0.5f),
                    IM_COL32(80, 255, 130, 200));
            }

            // Label — real-world speed
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), speed);
            char buf[64];
            snprintf(buf, sizeof(buf), "v=%s", spd_buf);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            float lx = scr.x + 10, ly = scr.y - 20;
            fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                              IM_COL32(0, 0, 0, 180), 3.0f);
            fg->AddText(ImVec2(lx, ly), IM_COL32(80, 255, 130, 255), buf);
        }
    }

    // ── Distance rulers ──────────────────────────────────────────────────
    for (auto& ruler : distance_rulers) {
        ImVec2 a = w2s(ruler.point_a);
        ImVec2 b = w2s(ruler.point_b);

        fg->AddLine(a, b, IM_COL32(230, 230, 80, 200), 1.5f);

        // Tick marks at endpoints
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
            float nx = -dy / len * 6.0f, ny = dx / len * 6.0f;
            fg->AddLine(ImVec2(a.x + nx, a.y + ny), ImVec2(a.x - nx, a.y - ny),
                        IM_COL32(230, 230, 80, 200), 1.5f);
            fg->AddLine(ImVec2(b.x + nx, b.y + ny), ImVec2(b.x - nx, b.y - ny),
                        IM_COL32(230, 230, 80, 200), 1.5f);
        }

        // Midpoint label (display in nanometers)
        float nm = ruler.distance * WORLD_TO_NM;
        char buf[32];
        if (nm < 0.01f)
            snprintf(buf, sizeof(buf), "%.2f pm", nm * 1000.0f);
        else if (nm < 1.0f)
            snprintf(buf, sizeof(buf), "%.3f nm", nm);
        else
            snprintf(buf, sizeof(buf), "%.2f nm", nm);
        ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        fg->AddRectFilled(ImVec2(mid.x - ts.x * 0.5f - 4, mid.y - ts.y * 0.5f - 2),
                          ImVec2(mid.x + ts.x * 0.5f + 4, mid.y + ts.y * 0.5f + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f),
                    IM_COL32(230, 230, 80, 255), buf);
    }

    // ── Density counters ─────────────────────────────────────────────────
    for (auto& dc : density_counters) {
        ImVec2 center = w2s(dc.world_pos);
        float screen_r = dc.radius * cfg.current_camera_zoom * scx;
        if (center.x < -screen_r || center.x > ww + screen_r ||
            center.y < -screen_r || center.y > wh + screen_r) continue;

        // Dashed circle
        ImU32 ring_col = IM_COL32(150, 80, 255, 120);
        int segs = 48;
        for (int s = 0; s < segs; s += 2) {
            float a0 = (float)s / segs * 6.2831853f;
            float a1 = (float)(s + 1) / segs * 6.2831853f;
            fg->AddLine(ImVec2(center.x + std::cos(a0) * screen_r,
                               center.y + std::sin(a0) * screen_r),
                        ImVec2(center.x + std::cos(a1) * screen_r,
                               center.y + std::sin(a1) * screen_r),
                        ring_col, 1.5f);
        }

        // Label
        char buf[64];
        snprintf(buf, sizeof(buf), "n=%u  \xcf\x81=%.4f", dc.count, dc.density);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float lx = center.x - ts.x * 0.5f, ly = center.y - screen_r - 20.0f;
        fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                          IM_COL32(0, 0, 0, 180), 3.0f);
        fg->AddText(ImVec2(lx, ly), IM_COL32(180, 130, 255, 255), buf);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Energy Heatmap ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_energy_heatmap(const SimConfig& cfg) {
    if (!readback_positions_ptr || !readback_velocities || readback_count == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);
    float zoom = cfg.current_camera_zoom;

    // Use atom grid cells (H_ATOM_DIAMETER spacing) for the heatmap
    float cell = H_ATOM_DIAMETER;
    float cell_px = cell * zoom * scx;

    // Don't draw if cells are too small to see
    if (cell_px < 2.0f) return;

    // Camera: top-left of screen in world coords
    glm::vec2 screen_tl = cfg.camera_origin - glm::vec2(ww, wh) * 0.5f / (zoom * glm::vec2(scx, scy));
    glm::vec2 screen_br = cfg.camera_origin + glm::vec2(ww, wh) * 0.5f / (zoom * glm::vec2(scx, scy));

    // Grid range visible on screen
    int gx_min = static_cast<int>(std::floor(screen_tl.x / cell)) - 1;
    int gy_min = static_cast<int>(std::floor(screen_tl.y / cell)) - 1;
    int gx_max = static_cast<int>(std::ceil(screen_br.x / cell)) + 1;
    int gy_max = static_cast<int>(std::ceil(screen_br.y / cell)) + 1;

    // Clamp to world bounds
    int world_gx_max = static_cast<int>(WORLD_W / cell);
    int world_gy_max = static_cast<int>(WORLD_H / cell);
    gx_min = std::max(gx_min, 0);
    gy_min = std::max(gy_min, 0);
    gx_max = std::min(gx_max, world_gx_max);
    gy_max = std::min(gy_max, world_gy_max);

    int grid_w = gx_max - gx_min;
    int grid_h = gy_max - gy_min;
    if (grid_w <= 0 || grid_h <= 0) return;

    // Cap grid resolution for performance
    constexpr int MAX_CELLS = 8000;
    if (grid_w * grid_h > MAX_CELLS) return;

    // Accumulate kinetic energy per cell
    std::vector<float> cell_energy(grid_w * grid_h, 0.0f);
    std::vector<uint32_t> cell_count(grid_w * grid_h, 0);

    for (uint32_t i = 0; i < readback_count; ++i) {
        if (readback_energies_ptr && readback_energies_ptr[i] <= 0.0f) continue;
        glm::vec2 pos = readback_positions_ptr[i];
        int gx = static_cast<int>(pos.x / cell) - gx_min;
        int gy = static_cast<int>(pos.y / cell) - gy_min;
        if (gx < 0 || gx >= grid_w || gy < 0 || gy >= grid_h) continue;

        glm::vec2 v = readback_velocities[i];
        float ke = 0.5f * glm::dot(v, v);
        int idx = gy * grid_w + gx;
        cell_energy[idx] += ke;
        cell_count[idx]++;
    }

    // Find max average energy for normalization
    float max_avg = 0.0f;
    for (int i = 0; i < grid_w * grid_h; ++i) {
        if (cell_count[i] > 0) {
            float avg = cell_energy[i] / static_cast<float>(cell_count[i]);
            if (avg > max_avg) max_avg = avg;
        }
    }
    if (max_avg < 0.001f) return;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    uint8_t alpha = static_cast<uint8_t>(heatmap_opacity * 255);

    for (int gy = 0; gy < grid_h; ++gy) {
        for (int gx = 0; gx < grid_w; ++gx) {
            int idx = gy * grid_w + gx;
            if (cell_count[idx] == 0) continue;

            float t = (cell_energy[idx] / static_cast<float>(cell_count[idx])) / max_avg;
            t = std::clamp(t, 0.0f, 1.0f);
            if (t < 0.02f) continue;

            // Blue → Cyan → Green → Yellow → Red
            uint8_t r, g, b;
            if (t < 0.25f) {
                float s = t / 0.25f;
                r = 0; g = static_cast<uint8_t>(s * 255); b = 255;
            } else if (t < 0.5f) {
                float s = (t - 0.25f) / 0.25f;
                r = 0; g = 255; b = static_cast<uint8_t>((1.0f - s) * 255);
            } else if (t < 0.75f) {
                float s = (t - 0.5f) / 0.25f;
                r = static_cast<uint8_t>(s * 255); g = 255; b = 0;
            } else {
                float s = (t - 0.75f) / 0.25f;
                r = 255; g = static_cast<uint8_t>((1.0f - s) * 255); b = 0;
            }

            uint8_t cell_alpha = static_cast<uint8_t>(alpha * std::min(t + 0.3f, 1.0f));

            float wx = static_cast<float>(gx + gx_min) * cell;
            float wy = static_cast<float>(gy + gy_min) * cell;
            ImVec2 tl = w2s(glm::vec2(wx, wy));
            ImVec2 br = w2s(glm::vec2(wx + cell, wy + cell));

            fg->AddRectFilled(tl, br, IM_COL32(r, g, b, cell_alpha));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Velocity Field ──────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_velocity_field(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    float cell_w = static_cast<float>(WORLD_W) / VIS_GRID_W;
    float cell_h = static_cast<float>(WORLD_H) / VIS_GRID_H;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // Find max avg speed for normalization
    float max_speed = 0.0f;
    for (uint32_t i = 0; i < VIS_GRID_CELLS; ++i) {
        if (vis_grid.count[i] > 0) {
            float vx = vis_grid.vel_x[i] / vis_grid.count[i];
            float vy = vis_grid.vel_y[i] / vis_grid.count[i];
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd > max_speed) max_speed = spd;
        }
    }
    if (max_speed < 0.01f) return;

    for (uint32_t gy = 0; gy < VIS_GRID_H; ++gy) {
        for (uint32_t gx = 0; gx < VIS_GRID_W; ++gx) {
            uint32_t idx = gy * VIS_GRID_W + gx;
            if (vis_grid.count[idx] < 2) continue;  // need at least 2 particles

            float vx = vis_grid.vel_x[idx] / vis_grid.count[idx];
            float vy = vis_grid.vel_y[idx] / vis_grid.count[idx];
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd < 0.01f) continue;

            // Cell center in world space
            glm::vec2 cell_center((gx + 0.5f) * cell_w, (gy + 0.5f) * cell_h);
            ImVec2 origin = w2s(cell_center);

            // Arrow length proportional to speed, scaled by zoom and user scale
            float norm_spd = spd / max_speed;
            float arrow_len = norm_spd * cell_w * 0.4f * cfg.current_camera_zoom * scx * velocity_field_scale;
            arrow_len = std::clamp(arrow_len, 3.0f, 40.0f);

            float dx = vx / spd, dy = vy / spd;
            ImVec2 tip(origin.x + dx * arrow_len, origin.y + dy * arrow_len);

            uint8_t a = static_cast<uint8_t>(100 + norm_spd * 155);
            fg->AddLine(origin, tip, IM_COL32(200, 200, 255, a), 1.2f);

            // Small arrowhead
            float ah = 3.0f;
            float px = -dy, py = dx;
            fg->AddTriangleFilled(
                tip,
                ImVec2(tip.x - dx * ah + px * ah * 0.4f, tip.y - dy * ah + py * ah * 0.4f),
                ImVec2(tip.x - dx * ah - px * ah * 0.4f, tip.y - dy * ah - py * ah * 0.4f),
                IM_COL32(200, 200, 255, a));
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Magnetic Field Visualization ─────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_magnetic_field(const SimConfig& cfg) {
    if (magnetic_sources.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // ── Per-particle dipole field lines (Earth-like loops, multi-source) ──
    // Field lines emerge from the north magnetic pole and loop back to the south pole.
    // We seed near the north pole and trace along B; the dipole field naturally curves
    // the lines around to the south pole, forming closed loops.
    // Adapt detail based on source count: fewer lines per particle when many visible
    int src_count = static_cast<int>(magnetic_sources.size());
    int lines_per_pole = (src_count <= 10) ? 6 :
                         (src_count <= 30) ? 4 :
                         (src_count <= 80) ? 3 : 2;
    int line_steps     = (src_count <= 30) ? 64 :
                         (src_count <= 80) ? 40 : 24;
    constexpr int MAX_LINE_STEPS = 64;  // fixed-size arrays
    constexpr float STEP_SIZE    = 6.0f;
    constexpr float SEED_R       = 5.0f;  // small ring near pole
    // Match the shader's interaction_radius — field lines shouldn't extend
    // beyond where the particle's magnetic force actually reaches
    const float MAX_RANGE = cfg.interaction_radius;

    // Helper: compute total B-field at a world position from all sources.
    // Two physically distinct contributions:
    //   1. Intrinsic dipole (spin): B ∝ μ/r³  (Earth-like field lines)
    //   2. Biot-Savart (moving charge / momentum): B ∝ q·(v×r̂)/r²  (azimuthal)
    // These superpose to give realistic stacking for elements/compounds.
    auto compute_B = [&](glm::vec2 pos) -> glm::vec2 {
        glm::vec2 B_total(0.0f);
        for (size_t sj = 0; sj < magnetic_sources.size(); ++sj) {
            const auto& s = magnetic_sources[sj];
            glm::vec2 dr = pos - s.pos;
            float r = glm::length(dr);
            if (r < 1.0f || r > MAX_RANGE) continue;

            float inv_r = 1.0f / r;
            glm::vec2 r_hat = dr * inv_r;
            glm::vec2 t_hat(-r_hat.y, r_hat.x);  // azimuthal unit vector

            // ── 1. Intrinsic spin dipole (1/r³ falloff) ──
            // Dipole axis: perpendicular to velocity (magnetic moment from spin),
            // or vertical if stationary
            if (std::abs(s.intrinsic_moment) > 0.001f) {
                float s_spd = glm::length(s.vel);
                glm::vec2 s_axis;
                if (s_spd > 1.0f)
                    s_axis = glm::vec2(-s.vel.y, s.vel.x) / s_spd;
                else
                    s_axis = glm::vec2(0.0f, 1.0f);

                float cos_a = glm::dot(r_hat, s_axis);
                float sin_a = r_hat.x * s_axis.y - r_hat.y * s_axis.x;

                float r3_inv = inv_r * inv_r * inv_r;
                float B_r = 2.0f * s.intrinsic_moment * cos_a * r3_inv;
                float B_t = s.intrinsic_moment * sin_a * r3_inv;

                B_total += r_hat * B_r + t_hat * B_t;
            }

            // ── 2. Biot-Savart from moving charge (1/r² falloff) ──
            // B = (K/r²) · q · (v × r̂)   — azimuthal field from current
            // Momentum p = m·v determines field strength, so faster & heavier = stronger
            // The cross product v × r̂ gives direction-dependent magnitude
            if (std::abs(s.charge) > 0.01f) {
                float spd = glm::length(s.vel);
                if (spd > 0.5f) {
                    // 2D cross: v × r̂ = vx·ry - vy·rx  (scalar, out-of-plane component)
                    float cross_vr = s.vel.x * r_hat.y - s.vel.y * r_hat.x;
                    // Scale by momentum: p = m·v, but we use charge·v with mass-dependent
                    // scaling. Heavier particles carry more current at same speed.
                    float momentum_scale = std::sqrt(s.mass_mev / 0.511f);  // relative to electron
                    float B_bs = 0.5f * s.charge * cross_vr * momentum_scale
                               * inv_r * inv_r;
                    // Biot-Savart B is azimuthal (tangential to r̂)
                    B_total += t_hat * B_bs;
                }
            }
        }
        return B_total;
    };

    // Helper: trace a field line from a seed point in a given direction
    // dir_sign = +1 traces along B, -1 traces against B
    // max_range limits how far from origin the line extends
    auto trace_line = [&](glm::vec2 seed, glm::vec2 origin, float dir_sign,
                          float max_range, ImVec2* pts, int& out_steps) {
        pts[0] = w2s(seed);
        glm::vec2 cur = seed;
        out_steps = 0;

        for (int step = 0; step < line_steps; ++step) {
            glm::vec2 B = compute_B(cur);
            float B_mag = glm::length(B);
            if (B_mag < 1e-7f) break;

            cur += (B / B_mag) * (STEP_SIZE * dir_sign);
            out_steps++;
            pts[step + 1] = w2s(cur);

            // Stop if returned very close to origin (loop completed)
            float d = glm::length(cur - origin);
            if (d < SEED_R * 0.8f && step > 4) break;

            // Stop if too far away
            if (d > max_range) break;
        }
    };

    for (size_t si = 0; si < magnetic_sources.size(); ++si) {
        const auto& src = magnetic_sources[si];
        float abs_mu = std::abs(src.intrinsic_moment);
        float spd = glm::length(src.vel);
        // Effective field strength: intrinsic dipole + Biot-Savart from momentum
        float bs_strength = std::abs(src.charge) * spd * std::sqrt(src.mass_mev / 0.511f) * 0.5f;
        float effective_strength = abs_mu + bs_strength * 0.1f;  // BS is 1/r² so weaker close-in

        // Per-particle effective range: stronger fields reach farther
        // Dipole ∝ μ/r³ → range ∝ cbrt(μ), BS ∝ q·v/r² → range ∝ sqrt(q·v)
        float dipole_range = std::cbrt(std::max(abs_mu, 0.01f) * 1e4f) * 12.0f;
        float bs_range = std::sqrt(std::max(bs_strength, 0.01f)) * 30.0f;
        float src_range = std::min(MAX_RANGE, std::max(20.0f, std::max(dipole_range, bs_range)));

        // Dipole axis (perpendicular to velocity, or vertical if stationary)
        glm::vec2 axis;
        if (spd > 1.0f)
            axis = glm::vec2(-src.vel.y, src.vel.x) / spd;
        else
            axis = glm::vec2(0.0f, 1.0f);
        glm::vec2 perp(-axis.y, axis.x);  // perpendicular to axis

        uint8_t line_alpha = static_cast<uint8_t>(
            magnetic_field_opacity * 200.0f * std::min(effective_strength * 0.5f, 1.0f));
        if (line_alpha < 8) continue;

        // North pole color (red/warm), south pole color (blue/cool)
        ImU32 col_north = IM_COL32(220, 80, 60, line_alpha);
        ImU32 col_south = IM_COL32(60, 100, 220, line_alpha);

        // Seed lines near the north pole: small offsets perpendicular to the axis
        // Use intrinsic moment sign if present, else charge sign for Biot-Savart
        float dominant_sign = (abs_mu > 0.01f) ? src.intrinsic_moment : src.charge;
        float pole_sign = (dominant_sign > 0) ? 1.0f : -1.0f;
        glm::vec2 north_pole = src.pos + axis * (SEED_R * pole_sign);

        for (int li = 0; li < lines_per_pole; ++li) {
            // Spread seeds in a small fan around the north pole
            float frac = (static_cast<float>(li) / static_cast<float>(lines_per_pole - 1)) - 0.5f;
            float spread = frac * SEED_R * 1.6f;  // lateral spread
            glm::vec2 seed = north_pole + perp * spread;

            // Trace forward (along B): lines arc outward from north pole to south pole
            ImVec2 pts_fwd[MAX_LINE_STEPS + 1];
            int steps_fwd = 0;
            trace_line(seed, src.pos, 1.0f, src_range, pts_fwd, steps_fwd);

            if (steps_fwd >= 3) {
                fg->AddPolyline(pts_fwd, steps_fwd + 1, col_north,
                                ImDrawFlags_None, 1.3f);
            }

            // Trace backward (against B): from same seed back toward the source
            // This completes the other half of the loop
            ImVec2 pts_bwd[MAX_LINE_STEPS + 1];
            int steps_bwd = 0;
            trace_line(seed, src.pos, -1.0f, src_range, pts_bwd, steps_bwd);

            if (steps_bwd >= 3) {
                fg->AddPolyline(pts_bwd, steps_bwd + 1, col_south,
                                ImDrawFlags_None, 1.3f);
            }
        }
    }

}

// ══════════════════════════════════════════════════════════════════════════════
// ── Gravity Map ──────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_gravity_map(const SimConfig& cfg) {
    if (!readback_positions_ptr || !readback_types_ptr || readback_count == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    constexpr float MARGIN = 60.0f;
    int drawn = 0;
    constexpr int MAX_SPLATS = 500;

    for (uint32_t i = 0; i < readback_count && drawn < MAX_SPLATS; ++i) {
        if (readback_energies_ptr && readback_energies_ptr[i] <= 0.0f) continue;

        uint32_t pt = readback_types_ptr[i];
        if (pt >= PHYS_PARTICLE_TYPES) continue;

        float mass = PHYS_REST_MASS_MEV[pt];
        if (mass < 0.01f) continue;  // skip massless (photons, gluons, gravitons)

        // When mass-energy equivalence is on, use relativistic mass: m × γ
        if (gr_mass_energy && readback_velocities) {
            constexpr float C_SIM = 300.0f;
            float v = glm::length(readback_velocities[i]);
            float beta = std::min(v / C_SIM, 0.999f);
            float gamma = 1.0f / std::sqrt(std::max(1.0f - beta * beta, 0.0001f));
            mass *= gamma;
        }

        glm::vec2 pos = readback_positions_ptr[i];
        ImVec2 sp = w2s(pos);

        if (sp.x < -MARGIN || sp.x > ww + MARGIN ||
            sp.y < -MARGIN || sp.y > wh + MARGIN) continue;

        // Splat radius: heavier particles → larger wells
        float log_mass = std::log(1.0f + mass / 100.0f);
        float radius = 8.0f + log_mass * 12.0f;
        radius = std::clamp(radius, 4.0f, 50.0f);

        // Color: deep purple → blue → cyan gradient based on mass
        // mass ranges: electron ~0.511, proton ~938, W/Z ~80-91k, Higgs ~125k, top ~173k
        float t = std::clamp(log_mass / 4.0f, 0.0f, 1.0f);  // log(1+938/100)≈2.3, log(1+173000/100)≈7.5

        uint8_t r, g, b;
        if (t < 0.33f) {
            // Deep purple → blue
            float s = t / 0.33f;
            r = static_cast<uint8_t>(80 * (1.0f - s));
            g = 0;
            b = static_cast<uint8_t>(120 + s * 135);
        } else if (t < 0.66f) {
            // Blue → cyan
            float s = (t - 0.33f) / 0.33f;
            r = 0;
            g = static_cast<uint8_t>(s * 220);
            b = 255;
        } else {
            // Cyan → white-cyan (heaviest)
            float s = (t - 0.66f) / 0.34f;
            r = static_cast<uint8_t>(s * 120);
            g = static_cast<uint8_t>(220 + s * 35);
            b = 255;
        }

        uint8_t a = static_cast<uint8_t>(38 + t * 30);  // ~0.15-0.27 alpha

        fg->AddCircleFilled(sp, radius, IM_COL32(r, g, b, a), 16);
        drawn++;
    }

    // Legend — bottom-left corner
    ImVec2 legend_pos(10, wh - 50);
    fg->AddCircleFilled(ImVec2(legend_pos.x + 6, legend_pos.y + 6), 4, IM_COL32(80, 0, 120, 180));
    fg->AddText(ImVec2(legend_pos.x + 14, legend_pos.y), IM_COL32(180, 140, 255, 200), "Light");
    fg->AddCircleFilled(ImVec2(legend_pos.x + 66, legend_pos.y + 6), 6, IM_COL32(0, 220, 255, 180));
    fg->AddText(ImVec2(legend_pos.x + 76, legend_pos.y), IM_COL32(100, 230, 255, 200), "Heavy");
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Strong Field Visualization ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
// Yukawa-range field lines around quarks, gluons, and nucleons.
// Color-coded by QCD color charge: red/green/blue.
// Lines trace through the combined strong potential using Euler integration.

void PhysicsInterface::draw_strong_field(const SimConfig& cfg) {
    if (strong_sources.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    int src_count = static_cast<int>(strong_sources.size());
    int lines_per = (src_count <= 10) ? 8 :
                    (src_count <= 40) ? 5 :
                    (src_count <= 120) ? 3 : 2;
    int line_steps = (src_count <= 40) ? 32 :
                     (src_count <= 120) ? 20 : 12;
    constexpr int MAX_STEPS = 32;
    constexpr float STEP_SIZE = 4.0f;
    // Yukawa range: pion Compton wavelength ~1.4fm, mapped to sim units
    const float YUKAWA_RANGE = std::min(60.0f, cfg.interaction_radius * 0.3f);
    const float YUKAWA_LAMBDA = 15.0f;  // screening length in sim pixels

    // Compute strong field at a point: Yukawa potential from all sources
    auto compute_F = [&](glm::vec2 pos) -> glm::vec2 {
        glm::vec2 F_total(0.0f);
        for (const auto& s : strong_sources) {
            glm::vec2 dr = s.pos - pos;  // toward source (attractive)
            float r = glm::length(dr);
            if (r < 1.0f || r > YUKAWA_RANGE) continue;
            float inv_r = 1.0f / r;
            glm::vec2 r_hat = dr * inv_r;
            // Yukawa: F = g² * exp(-r/λ) / r² * r̂  (attractive, pointing toward source)
            float strength = s.mass_mev * 0.001f;  // scale by mass
            float yukawa = strength * std::exp(-r / YUKAWA_LAMBDA) * inv_r * inv_r;
            // Linear confinement term (Cornell): σ·r (pulls harder at distance)
            float confinement = strength * 0.01f * r * inv_r;
            F_total += r_hat * (yukawa + confinement);
        }
        return F_total;
    };

    for (size_t si = 0; si < strong_sources.size(); ++si) {
        const auto& src = strong_sources[si];

        // QCD color charge → RGB color
        float cc = src.color_charge;
        uint8_t cr, cg, cb;
        if (cc < 1.5f) {
            cr = 220; cg = 60; cb = 60;     // red
        } else if (cc < 2.5f) {
            cr = 60; cg = 200; cb = 80;     // green
        } else {
            cr = 60; cg = 100; cb = 220;    // blue
        }

        uint8_t alpha = static_cast<uint8_t>(field_intensity * 140.0f);
        if (alpha < 5) continue;
        ImU32 col = IM_COL32(cr, cg, cb, alpha);

        for (int li = 0; li < lines_per; ++li) {
            float angle = static_cast<float>(li) * 6.2831853f / static_cast<float>(lines_per);
            glm::vec2 dir(std::cos(angle), std::sin(angle));
            glm::vec2 cur = src.pos + dir * 4.0f;

            ImVec2 pts[MAX_STEPS + 1];
            pts[0] = w2s(cur);
            int steps = 0;

            for (int step = 0; step < line_steps; ++step) {
                glm::vec2 F = compute_F(cur);
                float F_mag = glm::length(F);
                // Mix outward direction with field direction for radial emanation
                glm::vec2 advance = dir * 0.3f;
                if (F_mag > 1e-6f) advance += (F / F_mag) * 0.7f;
                float adv_mag = glm::length(advance);
                if (adv_mag < 1e-6f) break;
                cur += (advance / adv_mag) * STEP_SIZE;
                steps++;
                pts[step + 1] = w2s(cur);

                float d = glm::length(cur - src.pos);
                if (d > YUKAWA_RANGE) break;
            }

            if (steps >= 2) {
                fg->AddPolyline(pts, steps + 1, col, ImDrawFlags_None, 1.5f);
            }
        }

        // Gluon sources get a small glow ring
        if (src.is_gluon) {
            ImVec2 center = w2s(src.pos);
            fg->AddCircle(center, 6.0f * cfg.current_camera_zoom * scx,
                          IM_COL32(cr, cg, cb, alpha / 2), 12, 1.0f);
        }
    }

    // Draw "flux tubes" between nearby quark-antiquark or quark-quark pairs
    constexpr float TUBE_MAX_DIST = 50.0f;
    constexpr int MAX_TUBES = 200;
    int tubes = 0;
    for (size_t i = 0; i < strong_sources.size() && tubes < MAX_TUBES; ++i) {
        for (size_t j = i + 1; j < strong_sources.size() && tubes < MAX_TUBES; ++j) {
            float d = glm::length(strong_sources[i].pos - strong_sources[j].pos);
            if (d < 2.0f || d > TUBE_MAX_DIST) continue;

            ImVec2 a = w2s(strong_sources[i].pos);
            ImVec2 b = w2s(strong_sources[j].pos);

            uint8_t tube_alpha = static_cast<uint8_t>(
                field_intensity * 80.0f * (1.0f - d / TUBE_MAX_DIST));
            if (tube_alpha < 3) continue;

            fg->AddLine(a, b, IM_COL32(120, 220, 80, tube_alpha), 2.0f);
            tubes++;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Weak Field Visualization ────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
// Very short-range exponential halos around W/Z bosons and Higgs.
// Purple/violet color scheme with concentric fading rings.

void PhysicsInterface::draw_weak_field(const SimConfig& cfg) {
    if (weak_sources.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);
    float zoom = cfg.current_camera_zoom;

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    // Weak force range is extremely short (mediator mass ~80-91 GeV)
    // In sim units: ~15-25px
    constexpr float WEAK_RANGE_BASE = 20.0f;
    constexpr int RADIAL_LINES = 8;

    for (const auto& src : weak_sources) {
        ImVec2 center = w2s(src.pos);

        // Skip if offscreen
        float screen_range = WEAK_RANGE_BASE * zoom * scx;
        if (center.x < -screen_range || center.x > ww + screen_range ||
            center.y < -screen_range || center.y > wh + screen_range) continue;

        float base_alpha = field_intensity * 160.0f * std::min(src.coupling, 1.0f);
        if (base_alpha < 3.0f) continue;

        // Color: W bosons = violet, Z boson = indigo, Higgs = magenta, others = purple
        uint8_t cr, cg, cb;
        if (src.type == W_PLUS_TYPE_PHYS || src.type == W_MINUS_TYPE_PHYS) {
            cr = 160; cg = 60; cb = 220;    // violet
        } else if (src.type == Z_BOSON_TYPE_PHYS) {
            cr = 80; cg = 60; cb = 200;     // indigo
        } else if (src.type == HIGGS_TYPE_PHYS) {
            cr = 200; cg = 60; cb = 180;    // magenta
        } else {
            cr = 140; cg = 80; cb = 200;    // purple
        }

        // Concentric fading rings (3 rings)
        for (int ring = 1; ring <= 3; ++ring) {
            float frac = static_cast<float>(ring) / 3.0f;
            float r = screen_range * frac;
            float decay = std::exp(-frac * 3.0f);  // exponential falloff
            uint8_t ring_alpha = static_cast<uint8_t>(base_alpha * decay);
            if (ring_alpha < 2) continue;
            fg->AddCircle(center, r, IM_COL32(cr, cg, cb, ring_alpha), 24, 1.2f);
        }

        // Short radial lines
        for (int li = 0; li < RADIAL_LINES; ++li) {
            float angle = static_cast<float>(li) * 6.2831853f / static_cast<float>(RADIAL_LINES);
            float inner_r = 3.0f * zoom * scx;
            float outer_r = screen_range * 0.8f;

            ImVec2 inner(center.x + std::cos(angle) * inner_r,
                         center.y + std::sin(angle) * inner_r);
            ImVec2 outer(center.x + std::cos(angle) * outer_r,
                         center.y + std::sin(angle) * outer_r);

            uint8_t line_alpha = static_cast<uint8_t>(base_alpha * 0.6f);
            fg->AddLine(inner, outer, IM_COL32(cr, cg, cb, line_alpha), 1.0f);
        }

        // Central glow dot
        uint8_t center_alpha = static_cast<uint8_t>(std::min(base_alpha * 1.2f, 255.0f));
        fg->AddCircleFilled(center, 3.0f * zoom * scx,
                            IM_COL32(cr, cg, cb, center_alpha), 12);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Gravity Field Visualization ─────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
// Radial field lines converging on massive particles.
// Traces through the combined gravitational field using Euler integration.
// Blue-gray color scheme, line length proportional to mass.

void PhysicsInterface::draw_gravity_field(const SimConfig& cfg) {
    if (gravity_field_sources.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    int src_count = static_cast<int>(gravity_field_sources.size());
    int lines_per = (src_count <= 10) ? 8 :
                    (src_count <= 40) ? 5 :
                    (src_count <= 150) ? 3 : 2;
    int line_steps = (src_count <= 40) ? 48 :
                     (src_count <= 150) ? 28 : 16;
    constexpr int MAX_STEPS = 48;
    constexpr float STEP_SIZE = 5.0f;
    const float MAX_RANGE = cfg.interaction_radius;

    // Compute gravitational field at a point: sum of G*M/r² toward each source
    auto compute_G = [&](glm::vec2 pos) -> glm::vec2 {
        glm::vec2 G_total(0.0f);
        for (const auto& s : gravity_field_sources) {
            glm::vec2 dr = s.pos - pos;  // toward source
            float r = glm::length(dr);
            if (r < 2.0f || r > MAX_RANGE) continue;
            float inv_r = 1.0f / r;
            glm::vec2 r_hat = dr * inv_r;
            // Newton: g = G*M/r² toward mass
            float grav = s.mass_mev * 0.0001f * inv_r * inv_r;
            G_total += r_hat * grav;
        }
        return G_total;
    };

    for (size_t si = 0; si < gravity_field_sources.size(); ++si) {
        const auto& src = gravity_field_sources[si];

        // Effective range scales with mass (heavier → farther reach)
        float log_mass = std::log(1.0f + src.mass_mev / 100.0f);
        float src_range = std::min(MAX_RANGE, 30.0f + log_mass * 40.0f);

        // Color intensity scales with mass
        float mass_frac = std::clamp(log_mass / 5.0f, 0.0f, 1.0f);

        // Blue-gray gradient: light → deeper blue for heavier
        uint8_t cr = static_cast<uint8_t>(100 - mass_frac * 60);
        uint8_t cg = static_cast<uint8_t>(130 + mass_frac * 40);
        uint8_t cb = static_cast<uint8_t>(180 + mass_frac * 75);
        uint8_t alpha = static_cast<uint8_t>(field_intensity * 130.0f * std::min(mass_frac + 0.3f, 1.0f));
        if (alpha < 5) continue;
        ImU32 col = IM_COL32(cr, cg, cb, alpha);

        // Seed field lines on a ring OUTSIDE the source, trace INWARD
        float seed_r = src_range * 0.9f;

        for (int li = 0; li < lines_per; ++li) {
            float angle = static_cast<float>(li) * 6.2831853f / static_cast<float>(lines_per);
            glm::vec2 seed = src.pos + glm::vec2(std::cos(angle), std::sin(angle)) * seed_r;

            ImVec2 pts[MAX_STEPS + 1];
            pts[0] = w2s(seed);
            glm::vec2 cur = seed;
            int steps = 0;

            for (int step = 0; step < line_steps; ++step) {
                glm::vec2 G = compute_G(cur);
                float G_mag = glm::length(G);
                if (G_mag < 1e-8f) break;

                cur += (G / G_mag) * STEP_SIZE;
                steps++;
                pts[step + 1] = w2s(cur);

                // Stop if converged near a source
                float d_to_src = glm::length(cur - src.pos);
                if (d_to_src < 5.0f) break;
            }

            if (steps >= 2) {
                fg->AddPolyline(pts, steps + 1, col, ImDrawFlags_None, 1.0f);
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Gravitational Wave Ripples ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_grav_waves(const SimConfig& cfg) {
    if (gw_rings.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    for (const auto& ring : gw_rings) {
        ImVec2 center = w2s(ring.origin);
        float screen_r = ring.radius * cfg.current_camera_zoom * scx;

        // Skip tiny or huge rings
        if (screen_r < 1.0f || screen_r > ww * 3.0f) continue;

        // GW amplitude falls off as 1/r (strain h ∝ 1/r in 3D, ∝ 1/√r in 2D)
        float effective_amp = ring.amplitude / std::max(1.0f, ring.radius * 0.005f);
        float alpha_f = std::clamp(effective_amp * 100.0f, 0.0f, 180.0f);
        if (alpha_f < 3.0f) continue;

        uint8_t alpha = static_cast<uint8_t>(alpha_f);

        // Color: gold near source → violet at distance (absolute radius, not life fraction)
        float color_frac = std::clamp(ring.radius / 1500.0f, 0.0f, 1.0f);
        uint8_t r, g, b;
        if (color_frac < 0.4f) {
            // Gold/amber (fresh wave)
            r = 255; g = static_cast<uint8_t>(180 - color_frac * 200); b = 40;
        } else if (color_frac < 0.7f) {
            // Transition to violet
            float s = (color_frac - 0.4f) / 0.3f;
            r = static_cast<uint8_t>(255 * (1.0f - s) + 140 * s);
            g = static_cast<uint8_t>(100 * (1.0f - s) + 80 * s);
            b = static_cast<uint8_t>(40 * (1.0f - s) + 220 * s);
        } else {
            // Deep violet (distant wave)
            float s = (color_frac - 0.7f) / 0.3f;
            r = static_cast<uint8_t>(140 * (1.0f - s) + 60 * s);
            g = static_cast<uint8_t>(80 * (1.0f - s) + 40 * s);
            b = static_cast<uint8_t>(220 + s * 35);
        }

        // Draw ring — thickness fades with distance
        float thickness = std::max(1.0f, 2.5f / std::max(1.0f, ring.radius * 0.002f));
        fg->AddCircle(center, screen_r, IM_COL32(r, g, b, alpha), 48, thickness);

        // Second fainter ring slightly inside (wavefront has width)
        if (screen_r > 4.0f) {
            uint8_t a2 = static_cast<uint8_t>(alpha_f * 0.3f);
            fg->AddCircle(center, screen_r * 0.92f, IM_COL32(r, g, b, a2), 48, 1.0f);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Trajectory Traces ───────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_trajectory_traces(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(ww, wh) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    for (uint32_t pi = 0; pi < static_cast<uint32_t>(trajectory_history.size()); ++pi) {
        auto& hist = trajectory_history[pi];
        if (hist.size() < 2) continue;

        // Get particle type color
        ImVec4 col(0.7f, 0.7f, 0.7f, 1.0f);
        if (pi < readback_count && readback_positions_ptr) {
            // Use type color if we have it via type_counts_display context
            // We need the type — check if readback_energies_ptr is valid
            // For simplicity, use a generic fading white with slight type-based tint
        }

        int n = static_cast<int>(hist.size());
        for (int i = 1; i < n; ++i) {
            float alpha_frac = static_cast<float>(i) / static_cast<float>(n);
            uint8_t a = static_cast<uint8_t>(alpha_frac * 160);

            ImVec2 p0 = w2s(hist[i - 1]);
            ImVec2 p1 = w2s(hist[i]);

            // Skip if both off screen
            if ((p0.x < -20 || p0.x > ww + 20 || p0.y < -20 || p0.y > wh + 20) &&
                (p1.x < -20 || p1.x > ww + 20 || p1.y < -20 || p1.y > wh + 20))
                continue;

            // Skip if the segment is too long (toroidal wrap artifact)
            float sdx = p1.x - p0.x, sdy = p1.y - p0.y;
            if (sdx * sdx + sdy * sdy > (ww * 0.3f) * (ww * 0.3f)) continue;

            fg->AddLine(p0, p1, IM_COL32(180, 200, 255, a), 1.0f);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Force Vectors Overlay ───────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_force_vectors_overlay(const SimConfig& cfg) {
    if (selected_particle_idx < 0 || force_contributions.empty()) return;
    if (!readback_positions_ptr || static_cast<uint32_t>(selected_particle_idx) >= readback_count) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);

    glm::vec2 pos = readback_positions_ptr[selected_particle_idx];
    glm::vec2 scr_v = glm::vec2(ww, wh) * 0.5f + (pos - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
    ImVec2 origin(scr_v.x, scr_v.y);

    // Find max magnitude for scaling
    float max_mag = 0.0f;
    for (auto& fc : force_contributions)
        if (fc.magnitude > max_mag) max_mag = fc.magnitude;
    if (max_mag < 0.0001f) return;

    for (auto& fc : force_contributions) {
        float norm = std::log(1.0f + fc.magnitude) / std::log(1.0f + max_mag);
        float arrow_len = norm * 80.0f;
        if (arrow_len < 5.0f) continue;

        ImVec2 tip(origin.x + fc.direction.x * arrow_len,
                   origin.y + fc.direction.y * arrow_len);

        ImU32 col = ImGui::ColorConvertFloat4ToU32(fc.color);
        fg->AddLine(origin, tip, col, 2.5f);

        // Arrowhead
        float dx = fc.direction.x, dy = fc.direction.y;
        float ah = 7.0f;
        float px = -dy, py = dx;
        fg->AddTriangleFilled(
            tip,
            ImVec2(tip.x - dx * ah + px * ah * 0.4f, tip.y - dy * ah + py * ah * 0.4f),
            ImVec2(tip.x - dx * ah - px * ah * 0.4f, tip.y - dy * ah - py * ah * 0.4f),
            col);

        // Label at tip
        ImVec2 ts = ImGui::CalcTextSize(fc.name);
        fg->AddRectFilled(ImVec2(tip.x + 4, tip.y - ts.y * 0.5f - 1),
                          ImVec2(tip.x + 8 + ts.x, tip.y + ts.y * 0.5f + 1),
                          IM_COL32(0, 0, 0, 160), 2.0f);
        fg->AddText(ImVec2(tip.x + 6, tip.y - ts.y * 0.5f), col, fc.name);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Atom Grid ───────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_atom_grid(const SimConfig& cfg) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
    float scx = ww / static_cast<float>(REGION_W);
    float scy = wh / static_cast<float>(REGION_H);
    float zoom = cfg.current_camera_zoom;

    // Cell size in screen pixels
    float cell_px = H_ATOM_DIAMETER * zoom * scx;

    // Don't draw if cells are too small to see
    if (cell_px < 4.0f) return;

    uint8_t alpha = static_cast<uint8_t>(std::clamp(atom_grid_opacity * 255.0f, 0.0f, 255.0f));
    if (alpha == 0) return;

    // Brighter lines every 10 cells for readability
    uint8_t alpha_major = static_cast<uint8_t>(std::min(255, static_cast<int>(alpha) * 2));
    ImU32 col_minor = IM_COL32(100, 180, 255, alpha);
    ImU32 col_major = IM_COL32(100, 180, 255, alpha_major);

    // Camera offset: top-left of screen in world coords
    glm::vec2 screen_tl_world = cfg.camera_origin - glm::vec2(ww, wh) * 0.5f / (zoom * glm::vec2(scx, scy));

    // First grid line positions (snap to grid)
    float x_start = std::floor(screen_tl_world.x / H_ATOM_DIAMETER) * H_ATOM_DIAMETER;
    float y_start = std::floor(screen_tl_world.y / H_ATOM_DIAMETER) * H_ATOM_DIAMETER;

    // Grid index offset for major-line calculation
    int ix_off = static_cast<int>(std::floor(screen_tl_world.x / H_ATOM_DIAMETER));
    int iy_off = static_cast<int>(std::floor(screen_tl_world.y / H_ATOM_DIAMETER));

    auto w2s_x = [&](float wx) -> float {
        return ww * 0.5f + (wx - cfg.camera_origin.x) * zoom * scx;
    };
    auto w2s_y = [&](float wy) -> float {
        return wh * 0.5f + (wy - cfg.camera_origin.y) * zoom * scy;
    };

    // Vertical lines
    int line_idx = 0;
    for (float wx = x_start; ; wx += H_ATOM_DIAMETER, ++line_idx) {
        float sx = w2s_x(wx);
        if (sx < -1.0f) continue;
        if (sx > ww + 1.0f) break;
        int grid_ix = ix_off + line_idx;
        ImU32 col = (grid_ix % 10 == 0) ? col_major : col_minor;
        float thick = (grid_ix % 10 == 0) ? 1.0f : 0.5f;
        fg->AddLine(ImVec2(sx, 0), ImVec2(sx, wh), col, thick);
    }

    // Horizontal lines
    line_idx = 0;
    for (float wy = y_start; ; wy += H_ATOM_DIAMETER, ++line_idx) {
        float sy = w2s_y(wy);
        if (sy < -1.0f) continue;
        if (sy > wh + 1.0f) break;
        int grid_iy = iy_off + line_idx;
        ImU32 col = (grid_iy % 10 == 0) ? col_major : col_minor;
        float thick = (grid_iy % 10 == 0) ? 1.0f : 0.5f;
        fg->AddLine(ImVec2(0, sy), ImVec2(ww, sy), col, thick);
    }

    // Scale label at bottom-left
    float nm_per_cell = H_ATOM_DIAMETER * WORLD_TO_NM;
    char label[64];
    snprintf(label, sizeof(label), "1 cell = 1 H atom (%.3f nm)", nm_per_cell);
    ImVec2 ts = ImGui::CalcTextSize(label);
    float lx = 12.0f, ly = wh - 40.0f;
    fg->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + ts.x + 4, ly + ts.y + 2),
                      IM_COL32(0, 0, 0, 180), 3.0f);
    fg->AddText(ImVec2(lx, ly), IM_COL32(100, 180, 255, 220), label);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Orbit Paths Overlay ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_orbit_paths(const SimConfig& cfg) {
    if (orbit_paths.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x, win_h = io.DisplaySize.y;
    float scx = win_w / static_cast<float>(REGION_W);
    float scy = win_h / static_cast<float>(REGION_H);

    auto w2s = [&](glm::vec2 w) -> ImVec2 {
        glm::vec2 s = glm::vec2(win_w, win_h) * 0.5f
                    + (w - cfg.camera_origin) * cfg.current_camera_zoom * glm::vec2(scx, scy);
        return ImVec2(s.x, s.y);
    };

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // Shell colors (same palette as electron cloud)
    const ImVec4 SHELL_COLORS_M[] = {
        ImVec4(0.3f, 0.6f, 1.0f, 1.0f),   // shell 0: blue
        ImVec4(0.3f, 0.9f, 0.5f, 1.0f),   // shell 1: green
        ImVec4(0.9f, 0.7f, 0.2f, 1.0f),   // shell 2: gold
        ImVec4(0.95f, 0.35f, 0.3f, 1.0f),  // shell 3: red
    };
    const ImVec4 SHELL_COLORS_A[] = {
        ImVec4(0.0f, 0.85f, 0.95f, 1.0f),
        ImVec4(0.6f, 0.3f, 0.95f, 1.0f),
        ImVec4(0.95f, 0.4f, 0.6f, 1.0f),
        ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
    };

    constexpr int SEGMENTS = 48;  // points per ellipse

    for (const auto& orb : orbit_paths) {
        // Screen-space center and radii
        ImVec2 center_s = w2s(orb.center);

        float a_px = orb.semi_major * cfg.current_camera_zoom * scx;
        float b_px = orb.semi_minor * cfg.current_camera_zoom * scy;

        // Cull: skip tiny or offscreen orbits
        float max_r = std::max(a_px, b_px);
        if (max_r < 3.0f) continue;
        if (center_s.x < -max_r || center_s.x > win_w + max_r ||
            center_s.y < -max_r || center_s.y > win_h + max_r) continue;

        // Color from shell
        int si = std::clamp(orb.shell, 0, 3);
        const ImVec4& sc = orb.is_anti ? SHELL_COLORS_A[si] : SHELL_COLORS_M[si];
        float alpha = 0.35f;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(sc.x, sc.y, sc.z, alpha));

        // Draw rotated ellipse as polyline
        float cos_o = std::cos(orb.orientation);
        float sin_o = std::sin(orb.orientation);

        ImVec2 pts[SEGMENTS + 1];
        for (int k = 0; k <= SEGMENTS; ++k) {
            float theta = static_cast<float>(k) * 2.0f * 3.14159265f / static_cast<float>(SEGMENTS);
            float ex = a_px * std::cos(theta);
            float ey = b_px * std::sin(theta);
            // Rotate by orientation
            float rx = ex * cos_o - ey * sin_o;
            float ry = ex * sin_o + ey * cos_o;
            pts[k] = ImVec2(center_s.x + rx, center_s.y + ry);
        }
        fg->AddPolyline(pts, SEGMENTS + 1, col, ImDrawFlags_None, 1.0f);
    }
}
