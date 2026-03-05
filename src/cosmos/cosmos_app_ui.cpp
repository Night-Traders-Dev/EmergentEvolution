#include "cosmos/cosmos_app_internal.h"
#include "common/paths.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace {

ImVec4 star_tint_ui(const CelestialBody& b) {
    float t = std::clamp((b.temperature - 1200.0f) / 32000.0f, 0.0f, 1.0f);
    glm::vec3 tint(
        std::clamp(1.25f - t * 0.95f, 0.0f, 1.0f),
        std::clamp(0.45f + t * 0.6f, 0.0f, 1.0f),
        std::clamp(-0.1f + t * 1.25f, 0.0f, 1.0f));
    float giant = (b.stellar_stage == SSTAGE_RED_GIANT) ? 1.0f
        : std::clamp((b.radius - 40.0f) / 120.0f, 0.0f, 1.0f);
    float white_dwarf = (b.stellar_stage == SSTAGE_WHITE_DWARF) ? 1.0f : 0.0f;
    float neutron_star = (b.stellar_stage == SSTAGE_NEUTRON_STAR) ? 1.0f : 0.0f;
    float massive_hot = std::clamp((b.mass - 8.0f) / 32.0f, 0.0f, 1.0f) *
        std::clamp((b.temperature - 9000.0f) / 26000.0f, 0.0f, 1.0f);
    float cool = std::clamp((6000.0f - b.temperature) / 3600.0f, 0.0f, 1.0f);
    tint = glm::mix(tint, glm::vec3(1.00f, 0.62f, 0.34f), giant * std::max(cool, 0.35f) * 0.75f);
    tint = glm::mix(tint, glm::vec3(0.76f, 0.86f, 1.00f), massive_hot * 0.70f);
    tint = glm::mix(tint, glm::vec3(0.92f, 0.96f, 1.00f), white_dwarf * 0.90f);
    tint = glm::mix(tint, glm::vec3(0.72f, 0.84f, 1.00f), neutron_star * 0.96f);
    tint = glm::clamp(tint, glm::vec3(0.0f), glm::vec3(1.0f));
    return ImVec4(tint.r, tint.g, tint.b, 1.0f);
}

ImU32 body_color(const CelestialBody& b) {
    if (is_star_type(b.type)) {
        ImVec4 tint = star_tint_ui(b);
        return IM_COL32((int)(tint.x * 255.0f), (int)(tint.y * 255.0f),
                        (int)(tint.z * 255.0f), 255);
    }
    if (is_black_hole_type(b.type))
        return IM_COL32(18, 18, 22, 255);
    switch (b.type) {
    case CTYPE_PLANET:     return IM_COL32(60, 140, 220, 255);
    case CTYPE_MOON:       return IM_COL32(180, 180, 190, 255);
    case CTYPE_ASTEROID:   return IM_COL32(140, 130, 110, 255);
    case CTYPE_COMET:      return IM_COL32(160, 220, 255, 255);
    case CTYPE_NEBULA:     return IM_COL32(120, 60, 180, 255);
    case CTYPE_DUST:       return IM_COL32(205, 185, 160, 255);
    default:               return IM_COL32(200, 200, 200, 255);
    }
}

const char* const CTYPE_NAMES[] = {
    "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula",
    "O Star", "B Star", "A Star", "F Star", "G Star", "K Star", "M Star",
    "L Dwarf", "T Dwarf", "Y Dwarf", "Wolf-Rayet",
    "Stellar BH", "Intermediate BH", "Supermassive BH", "Primordial BH",
    "Dust",
};

const char* const PLANET_CLASS_NAMES[] = {
    "Dwarf Planet", "Terrestrial", "Ocean World", "Super-Earth", "Ice Giant", "Gas Giant",
};

const char* const MATERIAL_PHASE_NAMES[] = {
    "Solid", "Liquid", "Ice", "Gas", "Molten", "Plasma", "Collapsing Cloud",
};

const ImU32 CTYPE_COLORS[] = {
    IM_COL32(255, 200, 60, 255),   // Star - gold
    IM_COL32(60, 140, 220, 255),   // Planet - blue
    IM_COL32(180, 180, 190, 255),  // Moon - silver
    IM_COL32(140, 130, 110, 255),  // Asteroid - brown
    IM_COL32(160, 220, 255, 255),  // Comet - ice blue
    IM_COL32(18, 18, 22, 255),     // Black Hole - black
    IM_COL32(120, 60, 180, 255),   // Nebula - violet
    IM_COL32(120, 140, 255, 255),  // O - deep blue
    IM_COL32(160, 180, 255, 255),  // B - blue-white
    IM_COL32(220, 220, 255, 255),  // A - white
    IM_COL32(255, 255, 200, 255),  // F - yellow-white
    IM_COL32(255, 240, 100, 255),  // G - yellow (Sun)
    IM_COL32(255, 180, 60, 255),   // K - orange
    IM_COL32(255, 100, 60, 255),   // M - red
    IM_COL32(180, 60, 40, 255),    // L - dark red-brown
    IM_COL32(140, 40, 60, 255),    // T - magenta-brown
    IM_COL32(100, 30, 50, 255),    // Y - very dark
    IM_COL32(100, 180, 255, 255),  // WR - hot blue
    IM_COL32(24, 24, 30, 255),     // Stellar BH
    IM_COL32(18, 18, 24, 255),     // Intermediate BH
    IM_COL32(12, 12, 18, 255),     // Supermassive BH
    IM_COL32(32, 32, 40, 255),     // Primordial BH
    IM_COL32(205, 185, 160, 255),  // Dust
};

const char* format_sim_time(double seconds, char* buf, size_t buf_size) {
    double abs_s = std::abs(seconds);
    if (abs_s < 1e-6)
        snprintf(buf, buf_size, "%.1f ns", seconds * 1e9);
    else if (abs_s < 1e-3)
        snprintf(buf, buf_size, "%.1f us", seconds * 1e6);
    else if (abs_s < 1.0)
        snprintf(buf, buf_size, "%.1f ms", seconds * 1e3);
    else if (abs_s < 60.0)
        snprintf(buf, buf_size, "%.1f s", seconds);
    else if (abs_s < 3600.0)
        snprintf(buf, buf_size, "%.1f min", seconds / 60.0);
    else if (abs_s < 86400.0)
        snprintf(buf, buf_size, "%.1f hr", seconds / 3600.0);
    else if (abs_s < 3.156e7)
        snprintf(buf, buf_size, "%.1f day", seconds / 86400.0);
    else if (abs_s < 3.156e10)
        snprintf(buf, buf_size, "%.2f yr", seconds / 3.156e7);
    else if (abs_s < 3.156e13)
        snprintf(buf, buf_size, "%.2f kyr", seconds / 3.156e10);
    else if (abs_s < 3.156e16)
        snprintf(buf, buf_size, "%.2f Myr", seconds / 3.156e13);
    else if (abs_s < 3.156e19)
        snprintf(buf, buf_size, "%.2f Gyr", seconds / 3.156e16);
    else
        snprintf(buf, buf_size, "%.2f Tyr", seconds / 3.156e19);
    return buf;
}

void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                      ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 16;
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
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 32);
    }
}

} // namespace

void CosmosApp::render_overlay() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float aspect = W / H;
    float fov_rad = glm::radians(camera.fov);

    glm::dmat4 view = camera.view_matrix_d();
    glm::dmat4 proj = camera.proj_matrix_d(aspect);
    glm::dmat4 vp = proj * view;

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    if (cfg.show_orbits) {
        auto resolve_display_parent = [&](int body_idx) -> int {
            if (body_idx < 0 || body_idx >= (int)state.bodies.size())
                return -1;
            const auto& body = state.bodies[body_idx];
            if (body.marked_for_removal) return -1;

            if (body.parent >= 0 && body.parent < (int)state.bodies.size() && body.parent != body_idx) {
                const auto& parent = state.bodies[body.parent];
                if (!parent.marked_for_removal && parent.mass > body.mass * 0.25f)
                    return body.parent;
            }

            int best = -1;
            double best_pull = 0.0;
            for (int k = 0; k < (int)state.bodies.size(); ++k) {
                if (k == body_idx) continue;
                const auto& cand = state.bodies[k];
                if (cand.marked_for_removal) continue;
                if (cand.mass <= body.mass * 1.05f && !is_star_type(cand.type) && !is_black_hole_type(cand.type))
                    continue;
                glm::dvec3 d = glm::dvec3(cand.pos) - glm::dvec3(body.pos);
                double dist2 = glm::dot(d, d);
                if (dist2 < 1.0e-8) continue;
                double pull = (double)cand.mass / dist2;
                if (pull > best_pull) {
                    best_pull = pull;
                    best = k;
                }
            }
            return best;
        };

        constexpr double PI = 3.141592653589793;
        int rendered = 0;
        const int orbit_cap = std::min(300, (int)state.bodies.size());
        for (int i = 0; i < (int)state.bodies.size() && rendered < orbit_cap; ++i) {
            const auto& body = state.bodies[i];
            if (body.marked_for_removal) continue;
            if (is_star_type(body.type) || is_black_hole_type(body.type)) continue;

            int parent_idx = resolve_display_parent(i);
            if (parent_idx < 0 || parent_idx == i) continue;
            const auto& parent = state.bodies[parent_idx];
            if (parent.marked_for_removal) continue;

            glm::dvec3 r = glm::dvec3(body.pos) - glm::dvec3(parent.pos);
            glm::dvec3 v = glm::dvec3(body.vel) - glm::dvec3(parent.vel);
            double rmag = glm::length(r);
            if (rmag < 1.0e-6) continue;

            double mu = (double)cfg.G * std::max((double)parent.mass + (double)body.mass, 1.0e-8);
            if (mu <= 0.0) continue;

            glm::dvec3 h = glm::cross(r, v);
            double hmag = glm::length(h);
            if (hmag < 1.0e-10) continue;
            glm::dvec3 h_hat = h / hmag;

            glm::dvec3 evec = glm::cross(v, h) / mu - (r / rmag);
            double ecc = glm::length(evec);
            double energy = 0.5 * glm::dot(v, v) - mu / rmag;
            if (!std::isfinite(ecc) || !std::isfinite(energy)) continue;

            bool bound = (energy < -1.0e-8 && ecc < 0.9995);
            bool hyperbolic = (energy > 1.0e-8 && ecc > 1.0005);
            if (!bound && !hyperbolic) continue;

            glm::dvec3 p_hat = (ecc > 1.0e-5) ? (evec / ecc) : (r / rmag);
            glm::dvec3 q_hat = glm::cross(h_hat, p_hat);
            double qmag = glm::length(q_hat);
            if (qmag < 1.0e-10) continue;
            q_hat /= qmag;

            ImU32 c = body_color(body);
            int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
            int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
            int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;
            int alpha = (i == selected_body) ? 190 : 72;
            float width = (i == selected_body) ? 1.9f : 1.0f;
            ImU32 orbit_col = IM_COL32(cr, cg, cb, alpha);

            glm::vec3 prev_world(0.0f);
            bool have_prev = false;

            if (bound) {
                double a = -mu / (2.0 * energy);
                if (!std::isfinite(a) || a <= 0.0) continue;
                double p = a * (1.0 - ecc * ecc);
                if (!std::isfinite(p) || p <= 1.0e-8) continue;

                const int segs = 120;
                for (int s = 0; s <= segs; ++s) {
                    double theta = (2.0 * PI * (double)s) / (double)segs;
                    double denom = 1.0 + ecc * std::cos(theta);
                    if (std::abs(denom) < 1.0e-8) continue;
                    double radius = p / denom;
                    if (!std::isfinite(radius) || radius <= 0.0) continue;

                    glm::dvec3 world_d = glm::dvec3(parent.pos) +
                        p_hat * (radius * std::cos(theta)) +
                        q_hat * (radius * std::sin(theta));
                    glm::vec3 world = glm::vec3(world_d);

                    if (have_prev) {
                        auto p0 = project(prev_world, vp, W, H);
                        auto p1 = project(world, vp, W, H);
                        if (p0.visible && p1.visible)
                            fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy), orbit_col, width);
                    }
                    prev_world = world;
                    have_prev = true;
                }
            } else {
                double p = (hmag * hmag) / mu;
                if (!std::isfinite(p) || p <= 1.0e-8) continue;
                double theta_max = std::acos(std::clamp(-1.0 / ecc, -1.0, 1.0)) - 0.04;
                if (!std::isfinite(theta_max) || theta_max <= 0.05) continue;

                const int segs = 72;
                for (int s = 0; s <= segs; ++s) {
                    double t = (double)s / (double)segs;
                    double theta = -theta_max + (2.0 * theta_max) * t;
                    double denom = 1.0 + ecc * std::cos(theta);
                    if (std::abs(denom) < 1.0e-8) continue;
                    double radius = p / denom;
                    if (!std::isfinite(radius) || radius <= 0.0 || radius > rmag * 14.0) continue;

                    glm::dvec3 world_d = glm::dvec3(parent.pos) +
                        p_hat * (radius * std::cos(theta)) +
                        q_hat * (radius * std::sin(theta));
                    glm::vec3 world = glm::vec3(world_d);

                    if (have_prev) {
                        auto p0 = project(prev_world, vp, W, H);
                        auto p1 = project(world, vp, W, H);
                        if (p0.visible && p1.visible)
                            fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy), orbit_col, width);
                    }
                    prev_world = world;
                    have_prev = true;
                }
            }

            rendered++;
        }
    }

    if (cfg.show_trails) {
        for (size_t i = 0; i < state.trails.size() && i < state.bodies.size(); i++) {
            auto& trail = state.trails[i];
            if (trail.size() < 2) continue;

            ImU32 c = body_color(state.bodies[i]);
            int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
            int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
            int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;

            for (size_t j = 1; j < trail.size(); j++) {
                auto p0 = project(trail[j - 1], vp, W, H);
                auto p1 = project(trail[j], vp, W, H);
                if (!p0.visible || !p1.visible) continue;

                float frac = (float)j / (float)trail.size();
                int alpha = (int)(frac * 80.0f);
                float width = 1.0f + frac * 1.5f;
                fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy),
                            IM_COL32(cr, cg, cb, alpha), width);
            }
        }
    }

    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        const auto& b = state.bodies[selected_body];
        auto p = project(b.pos, vp, W, H);
        if (p.visible) {
            float sr = screen_radius(b.radius, p.depth, fov_rad, H);
            sr = std::max(sr, 6.0f);

            float pulse = 0.85f + 0.15f * std::sin(sim_time_ * 4.0f);
            float ring_r = (sr + 6.0f) * pulse;

            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r + 2.0f,
                          IM_COL32(255, 200, 60, 40), 48, 3.0f);
            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r,
                          IM_COL32(255, 220, 100, 200), 48, 2.0f);

            float bk = ring_r + 8.0f;
            float bl = 8.0f;
            ImU32 bk_col = IM_COL32(255, 255, 255, 120);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk + bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk - bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk + bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk, p.sy + bk - bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk - bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk, p.sy + bk - bl), bk_col, 1.5f);

            const char* name = b.name.empty() ? CTYPE_NAMES[std::min(b.type, (uint32_t)CTYPE_COUNT - 1)] : b.name.c_str();
            ImVec2 name_size = ImGui::CalcTextSize(name);
            float label_x = p.sx - name_size.x * 0.5f;
            float label_y = p.sy - ring_r - 20.0f;
            fg->AddRectFilled(ImVec2(label_x - 4, label_y - 2),
                              ImVec2(label_x + name_size.x + 4, label_y + name_size.y + 2),
                              IM_COL32(10, 10, 20, 180), 3.0f);
            fg->AddText(ImVec2(label_x, label_y), IM_COL32(255, 220, 100, 240), name);
        }
    }

    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        const char* track_label = "TRACKING";
        ImVec2 tl_size = ImGui::CalcTextSize(track_label);
        float tx = W - tl_size.x - 16.0f;
        float ty = 44.0f;
        fg->AddRectFilled(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                          IM_COL32(255, 180, 40, 30), 3.0f);
        fg->AddRect(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                    IM_COL32(255, 180, 40, 120), 3.0f, 0, 1.0f);
        float alpha = 160.0f + 60.0f * std::sin(sim_time_ * 3.0f);
        fg->AddText(ImVec2(tx, ty), IM_COL32(255, 200, 80, (int)alpha), track_label);
    }

    if (cfg.cosmos_space_fabric) {
        char fabric_label[96];
        snprintf(fabric_label, sizeof(fabric_label), "Space fabric: %.1f units per square",
                 cfg.cosmos_space_fabric_grid_size);
        ImVec2 label_size = ImGui::CalcTextSize(fabric_label);
        float px = 16.0f;
        float py = H - label_size.y - 18.0f;
        fg->AddRectFilled(ImVec2(px - 8.0f, py - 4.0f),
                          ImVec2(px + label_size.x + 8.0f, py + label_size.y + 4.0f),
                          IM_COL32(10, 14, 24, 180), 4.0f);
        fg->AddRect(ImVec2(px - 8.0f, py - 4.0f),
                    ImVec2(px + label_size.x + 8.0f, py + label_size.y + 4.0f),
                    IM_COL32(80, 150, 230, 110), 4.0f, 0, 1.0f);
        fg->AddText(ImVec2(px, py), IM_COL32(180, 220, 255, 235), fabric_label);
    }
}

void CosmosApp::draw_menu_background() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 4, 12, 255));

    if (!menu_bg_inited_) {
        menu_bg_inited_ = true;
        menu_particles_.resize(60);
        std::mt19937 rng(42);
        auto randf = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        for (auto& p : menu_particles_) {
            p.x = randf(0, W);
            p.y = randf(0, H);
            p.vx = randf(-12, 12);
            p.vy = randf(-12, 12);
            p.radius = randf(1.5f, 4.0f);
            int variant = rng() % 4;
            if (variant == 0) { p.r = 1.0f; p.g = 0.7f; p.b = 0.2f; }
            else if (variant == 1) { p.r = 0.3f; p.g = 0.5f; p.b = 1.0f; }
            else if (variant == 2) { p.r = 0.6f; p.g = 0.3f; p.b = 0.9f; }
            else { p.r = 0.2f; p.g = 0.8f; p.b = 0.6f; }
            p.alpha = randf(0.4f, 0.8f);
            for (int t = 0; t < 12; t++) { p.trail_x[t] = p.x; p.trail_y[t] = p.y; }
        }
    }

    menu_bg_time_ += io.DeltaTime;

    for (int i = 0; i < 3; i++) {
        float phase = menu_bg_time_ * 0.15f + (float)i * 2.1f;
        float cx = W * (0.3f + 0.4f * sinf(phase));
        float cy = H * (0.3f + 0.4f * cosf(phase * 0.7f + 1.0f));
        float glow_r = 200.0f + 50.0f * sinf(phase * 1.3f);
        ImU32 center, edge;
        if (i == 0) { center = IM_COL32(255, 160, 40, 18); edge = IM_COL32(255, 80, 0, 0); }
        else if (i == 1) { center = IM_COL32(60, 100, 200, 14); edge = IM_COL32(30, 50, 180, 0); }
        else { center = IM_COL32(120, 50, 180, 12); edge = IM_COL32(80, 20, 140, 0); }
        draw_radial_glow(bg, cx, cy, glow_r, center, edge);
    }

    float dt = io.DeltaTime;
    for (auto& p : menu_particles_) {
        for (int t = 11; t > 0; t--) { p.trail_x[t] = p.trail_x[t - 1]; p.trail_y[t] = p.trail_y[t - 1]; }
        p.trail_x[0] = p.x;
        p.trail_y[0] = p.y;

        p.x += p.vx * dt;
        p.y += p.vy * dt;

        if (p.x < -20) p.x += W + 40;
        if (p.x > W + 20) p.x -= W + 40;
        if (p.y < -20) p.y += H + 40;
        if (p.y > H + 20) p.y -= H + 40;

        for (int t = 1; t < 12; t++) {
            float frac = 1.0f - (float)t / 12.0f;
            int alpha = (int)(p.alpha * frac * 40.0f);
            bg->AddLine(ImVec2(p.trail_x[t - 1], p.trail_y[t - 1]),
                        ImVec2(p.trail_x[t], p.trail_y[t]),
                        IM_COL32((int)(p.r * 255), (int)(p.g * 255), (int)(p.b * 255), alpha),
                        p.radius * frac * 0.6f);
        }

        int alpha = (int)(p.alpha * 255.0f);
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.radius,
                            IM_COL32((int)(p.r * 255), (int)(p.g * 255), (int)(p.b * 255), alpha), 12);
    }

    for (size_t i = 0; i < menu_particles_.size(); i++) {
        for (size_t j = i + 1; j < menu_particles_.size(); j++) {
            float dx = menu_particles_[j].x - menu_particles_[i].x;
            float dy = menu_particles_[j].y - menu_particles_[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 100.0f) {
                float a = (1.0f - d / 100.0f) * 20.0f;
                bg->AddLine(ImVec2(menu_particles_[i].x, menu_particles_[i].y),
                            ImVec2(menu_particles_[j].x, menu_particles_[j].y),
                            IM_COL32(255, 180, 60, (int)a), 0.5f);
            }
        }
    }

    draw_radial_glow(bg, W * 0.5f, H * 0.5f, std::max(W, H) * 0.8f,
                     IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));

    for (float y = 0; y < H; y += 3.0f)
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(0, 0, 0, 8));
}

void CosmosApp::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    splash_time_ += io.DeltaTime;

    if (splash_time_ > 0.3f) {
        bool dismiss = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                       ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!dismiss) {
            const ImGuiKey keys[] = {
                ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Escape,
                ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E,
                ImGuiKey_F, ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J,
                ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N, ImGuiKey_O,
                ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T,
                ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y,
                ImGuiKey_Z, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
            };
            for (auto k : keys) {
                if (ImGui::IsKeyPressed(k)) { dismiss = true; break; }
            }
        }
        if (dismiss) {
            show_splash = false;
            return;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##CosmosSplash", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        float pulse = 0.7f + 0.3f * sinf(splash_time_ * 2.0f);
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 160.0f * pulse,
                         IM_COL32(255, 200, 60, 40), IM_COL32(255, 120, 0, 0));
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 80.0f * pulse,
                         IM_COL32(255, 240, 180, 60), IM_COL32(255, 200, 60, 0));

        for (int i = 0; i < 4; i++) {
            float orbit_r = 60.0f + (float)i * 35.0f;
            float speed = 0.8f - (float)i * 0.15f;
            float angle = splash_time_ * speed + (float)i * 1.57f;
            float px = W * 0.5f + cosf(angle) * orbit_r;
            float py = H * 0.4f + sinf(angle) * orbit_r * 0.4f;
            float dot_r = 3.0f + (float)i * 0.5f;
            dl->AddCircleFilled(ImVec2(px, py), dot_r,
                                IM_COL32(60 + i * 40, 140 + i * 20, 220, 200), 12);
        }

        float title_scale = 2.4f;
        ImGui::SetWindowFontScale(title_scale);

        const char* title1 = "Cosmic ";
        const char* title2 = "Sandbox";
        ImVec2 t1_size = ImGui::CalcTextSize(title1);
        float title_x = 60.0f;
        float title_y = H - 120.0f;

        for (int layer = 3; layer >= 0; layer--) {
            float offset = (float)layer * 1.5f;
            int alpha = 12 + layer * 6;
            dl->AddText(ImVec2(title_x - offset, title_y - offset),
                        IM_COL32(255, 180, 40, alpha), title1);
            dl->AddText(ImVec2(title_x + t1_size.x - offset, title_y - offset),
                        IM_COL32(255, 130, 0, alpha), title2);
        }

        dl->AddText(ImVec2(title_x, title_y), IM_COL32(255, 220, 120, 255), title1);
        dl->AddText(ImVec2(title_x + t1_size.x, title_y), IM_COL32(255, 160, 40, 255), title2);

        ImGui::SetWindowFontScale(1.0f);
        const char* badge = "CELESTIAL SIMULATION";
        ImVec2 badge_size = ImGui::CalcTextSize(badge);
        float badge_x = W - badge_size.x - 40.0f;
        float badge_y = 30.0f;
        float pad = 8.0f;
        dl->AddRect(ImVec2(badge_x - pad, badge_y - pad * 0.5f),
                    ImVec2(badge_x + badge_size.x + pad, badge_y + badge_size.y + pad * 0.5f),
                    IM_COL32(255, 160, 40, 200), 4.0f, 0, 1.5f);
        dl->AddText(ImVec2(badge_x, badge_y), IM_COL32(255, 180, 60, 230), badge);

        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = 120.0f + 80.0f * sinf(splash_time_ * 3.0f);
        dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 40.0f),
                    IM_COL32(200, 200, 210, (int)hint_alpha), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void CosmosApp::draw_pause_menu() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##CosmosPause", nullptr, flags)) {
        float cx = W * 0.5f, cy = H * 0.5f;

        ImGui::SetWindowFontScale(2.0f);
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_y = cy - 160.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int layer = 2; layer >= 0; layer--) {
            float off = (float)layer * 2.0f;
            dl->AddText(ImVec2(cx - title_size.x * 0.5f - off, title_y - off),
                        IM_COL32(255, 180, 40, 15 + layer * 10), title);
        }
        dl->AddText(ImVec2(cx - title_size.x * 0.5f, title_y),
                    IM_COL32(255, 200, 80, 255), title);
        ImGui::SetWindowFontScale(1.0f);

        float btn_w = 200.0f, btn_h = 40.0f, btn_spacing = 52.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.22f, 0.35f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f, 0.30f, 0.45f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            paused = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            reset_simulation();
            show_pause_menu = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Save Simulation", ImVec2(btn_w, btn_h))) {
            show_save_dialog_ = true;
            show_pause_menu = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Load Simulation", ImVec2(btn_w, btn_h))) {
            show_load_dialog_ = true;
            show_pause_menu = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Empty Simulation", ImVec2(btn_w, btn_h))) {
            state.clear();
            cfg.body_count = 0;
            selected_body = -1;
            sim_time_ = 0.0f;
            show_pause_menu = false;
            paused = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Return to Launcher", ImVec2(btn_w, btn_h))) {
            request_launcher = true;
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cx - hint_size.x * 0.5f, H - 60.0f),
                    IM_COL32(160, 160, 170, 100), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void CosmosApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({30, io.DisplaySize.y - 430.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({980, 400}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(780, 330), ImVec2(1320, 620));

    if (!ImGui::Begin("Spawn Bodies", &spawn_menu_visible_)) {
        ImGui::End();
        return;
    }

    struct TypeEntry { int type; float mass; };
    static const TypeEntry BASIC[] = {
        {CTYPE_PLANET, 3.003e-6f}, {CTYPE_MOON, 3.70e-8f}, {CTYPE_ASTEROID, 2.0e-10f},
        {CTYPE_COMET, 8.0e-11f}, {CTYPE_DUST, 5.0e-12f}, {CTYPE_NEBULA, 0.02f},
    };
    static const TypeEntry STARS[] = {
        {CTYPE_STAR, 1.0f}, {CTYPE_STAR_O, 30.0f}, {CTYPE_STAR_B, 5.0f}, {CTYPE_STAR_A, 1.8f},
        {CTYPE_STAR_F, 1.2f}, {CTYPE_STAR_G, 1.0f}, {CTYPE_STAR_K, 0.6f}, {CTYPE_STAR_M, 0.2f},
        {CTYPE_STAR_L, 0.06f}, {CTYPE_STAR_T, 0.04f}, {CTYPE_STAR_Y, 0.02f}, {CTYPE_STAR_WR, 20.0f},
    };
    static const TypeEntry BHS[] = {
        {CTYPE_BLACK_HOLE, 200.0f}, {CTYPE_BH_STELLAR, 10.0f}, {CTYPE_BH_INTERMEDIATE, 1000.0f},
        {CTYPE_BH_SUPERMASSIVE, 1000000.0f}, {CTYPE_BH_PRIMORDIAL, 0.5f},
    };
    static int catalog_tab = 0;
    const char* tab_names[] = {"Basic", "Stars", "Black Holes", "Existing Objects"};
    const TypeEntry* active_list = BASIC;
    int active_count = (int)IM_ARRAYSIZE(BASIC);
    if (catalog_tab == 1) { active_list = STARS; active_count = (int)IM_ARRAYSIZE(STARS); }
    if (catalog_tab == 2) { active_list = BHS; active_count = (int)IM_ARRAYSIZE(BHS); }

    ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.66f, 1.0f), "Spawn Studio");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.62f, 0.60f, 0.56f, 1.0f),
                       "Select a body card, tune properties, then spawn at camera target.");
    for (int t = 0; t < 4; ++t) {
        if (t > 0) ImGui::SameLine();
        if (catalog_tab == t) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.34f, 0.26f, 0.10f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.30f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.46f, 0.34f, 0.15f, 1.0f));
        }
        if (ImGui::Button(tab_names[t], ImVec2(120, 24))) catalog_tab = t;
        if (catalog_tab == t) ImGui::PopStyleColor(3);
    }

    if (catalog_tab == 3) {
        static int attach_host = -1;
        static int attach_moon_count = 2;
        static float attach_ring_inner = 1.6f;
        static float attach_ring_outer = 3.2f;
        static float attach_ring_density = 0.35f;
        static float attach_ring_ice = 0.55f;

        std::vector<int> host_indices;
        std::vector<std::string> host_labels;
        host_indices.reserve(state.bodies.size());
        host_labels.reserve(state.bodies.size());
        for (int i = 0; i < (int)state.bodies.size(); ++i) {
            const auto& b = state.bodies[(size_t)i];
            if (b.marked_for_removal) continue;
            if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;
            host_indices.push_back(i);
            const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Body";
            std::string name = b.name.empty() ? std::string(type_name) : b.name;
            host_labels.push_back(name + " (" + type_name + ")");
        }

        if (!host_indices.empty()) {
            bool found = false;
            for (int idx : host_indices) {
                if (idx == attach_host) { found = true; break; }
            }
            if (!found) attach_host = host_indices.front();
        } else {
            attach_host = -1;
        }

        ImGui::Separator();
        if (ImGui::BeginChild("##attach_tools", ImVec2(0, 0), true)) {
            ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Spawn On Existing Objects");
            ImGui::TextColored(ImVec4(0.62f, 0.60f, 0.56f, 1.0f),
                               "Pick a host body, then add moons or a dust ring.");
            if (host_indices.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                   "No eligible host bodies found.");
            } else {
                int current_host_choice = 0;
                for (int k = 0; k < (int)host_indices.size(); ++k) {
                    if (host_indices[(size_t)k] == attach_host) { current_host_choice = k; break; }
                }
                const char* preview = host_labels[(size_t)current_host_choice].c_str();
                if (ImGui::BeginCombo("Host Body", preview)) {
                    for (int k = 0; k < (int)host_indices.size(); ++k) {
                        bool selected = (host_indices[(size_t)k] == attach_host);
                        if (ImGui::Selectable(host_labels[(size_t)k].c_str(), selected))
                            attach_host = host_indices[(size_t)k];
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const CelestialBody* host = (attach_host >= 0 && attach_host < (int)state.bodies.size())
                    ? &state.bodies[(size_t)attach_host] : nullptr;
                bool can_add_moons = (host != nullptr && host->type == CTYPE_PLANET);
                bool can_add_ring = (host != nullptr &&
                    (host->type == CTYPE_PLANET || host->type == CTYPE_MOON || host->type == CTYPE_NEBULA));

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Moons");
                ImGui::SliderInt("Moon Count", &attach_moon_count, 1, 12);
                if (!can_add_moons) ImGui::BeginDisabled();
                if (ImGui::Button("Spawn Moons on Host", ImVec2(-1, 28))) {
                    spawn_moons_for_host(attach_host, attach_moon_count);
                }
                if (!can_add_moons) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                       "Moons can be attached to planets only.");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Rings");
                ImGui::SliderFloat("Ring Inner xR", &attach_ring_inner, 1.15f, 4.0f, "%.2f");
                ImGui::SliderFloat("Ring Outer xR", &attach_ring_outer, 1.5f, 8.0f, "%.2f");
                ImGui::SliderFloat("Ring Density", &attach_ring_density, 0.01f, 1.0f, "%.2f");
                ImGui::SliderFloat("Ring Ice Fraction", &attach_ring_ice, 0.0f, 1.0f, "%.2f");
                if (!can_add_ring) ImGui::BeginDisabled();
                if (ImGui::Button("Spawn Ring on Host", ImVec2(-1, 28))) {
                    spawn_ring_for_host(attach_host, attach_ring_inner, attach_ring_outer,
                                        attach_ring_density, attach_ring_ice);
                }
                if (!can_add_ring) {
                    ImGui::EndDisabled();
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                       "Rings can be attached to planets, moons, or nebulae.");
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    if (ImGui::BeginChild("##spawn_type_strip", ImVec2(0, 96), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (int i = 0; i < active_count; ++i) {
            const int t = active_list[i].type;
            const float default_mass = active_list[i].mass;
            ImGui::PushID(t);
            ImU32 col = CTYPE_COLORS[t];
            float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
            bool sel = (spawn_type == t);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * (sel ? 0.60f : 0.26f), g * (sel ? 0.60f : 0.26f), b * (sel ? 0.60f : 0.26f), sel ? 1.0f : 0.86f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.75f, g * 0.75f, b * 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.88f, g * 0.88f, b * 0.88f, 1.0f));
            if (ImGui::Button(CTYPE_NAMES[t], ImVec2(130, 44))) {
                spawn_type = t;
                spawn_mass = default_mass;
            }
            ImGui::PopStyleColor(3);
            ImGui::TextColored(ImVec4(0.65f, 0.62f, 0.58f, 1.0f), "m %.2e", default_mass);
            ImGui::PopID();
            if (i + 1 < active_count) ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    ImGui::Columns(2, "##spawn_cols", false);
    ImGui::SetColumnWidth(0, 340.0f);

    if (ImGui::BeginChild("##spawn_dynamics", ImVec2(0, 0), true)) {
        ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Dynamics");
        ImGui::SliderFloat("Mass##Spawn", &spawn_mass, 1.0e-13f, 500.0f, "%.3e", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("Orbital Velocity", &spawn_in_orbit_);
        ImGui::Checkbox("Override Temperature", &spawn_draft_.override_temperature);
        if (spawn_draft_.override_temperature)
            ImGui::SliderFloat("Temperature K", &spawn_draft_.temperature, 2.7f, 8000.0f, "%.1f");
        ImGui::Checkbox("Override Radius", &spawn_draft_.override_radius);
        if (spawn_draft_.override_radius)
            ImGui::SliderFloat("Radius", &spawn_draft_.radius, 0.04f, 120.0f, "%.2f");
        ImGui::Checkbox("Override Rotation", &spawn_draft_.override_rotation);
        if (spawn_draft_.override_rotation)
            ImGui::SliderFloat("Rotation Hours", &spawn_draft_.rotation_hours, 0.1f, 2000.0f, "%.2f");
        ImGui::Checkbox("Override Velocity", &spawn_draft_.override_velocity);
        if (spawn_draft_.override_velocity) {
            ImGui::SliderFloat("Vx (km/s)", &spawn_draft_.velocity_kms.x, -200.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Vy (km/s)", &spawn_draft_.velocity_kms.y, -200.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Vz (km/s)", &spawn_draft_.velocity_kms.z, -200.0f, 200.0f, "%.1f");
        }
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    if (ImGui::BeginChild("##spawn_visuals", ImVec2(0, 0), true)) {
        ImGui::TextColored(ImVec4(0.88f, 0.84f, 0.75f, 1.0f), "Appearance & System");
        if (spawn_type == CTYPE_PLANET || spawn_type == CTYPE_MOON) {
            const char* looks[] = {"Auto", "Rocky", "Water", "Ice", "Earth-like"};
            ImGui::Combo("Planet Look", &spawn_draft_.planet_look, looks, IM_ARRAYSIZE(looks));
            ImGui::Checkbox("Add Moons", &spawn_draft_.spawn_moons);
            if (spawn_draft_.spawn_moons)
                ImGui::SliderInt("Moon Count", &spawn_draft_.moon_count, 1, 8);
            ImGui::Checkbox("Add Rings", &spawn_draft_.spawn_rings);
            if (spawn_draft_.spawn_rings) {
                ImGui::Checkbox("Override Ring Layout", &spawn_draft_.override_ring_layout);
                if (spawn_draft_.override_ring_layout) {
                    ImGui::SliderFloat("Ring Inner xR", &spawn_draft_.ring_inner_mult, 1.15f, 4.0f, "%.2f");
                    ImGui::SliderFloat("Ring Outer xR", &spawn_draft_.ring_outer_mult, 1.5f, 8.0f, "%.2f");
                    ImGui::SliderFloat("Ring Density", &spawn_draft_.ring_density, 0.01f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Ring Ice", &spawn_draft_.ring_ice_fraction, 0.0f, 1.0f, "%.2f");
                }
            }
        } else {
            spawn_draft_.planet_look = 0;
            spawn_draft_.spawn_moons = false;
            spawn_draft_.spawn_rings = false;
        }

        ImGui::Separator();
        ImGui::Checkbox("Override Material Composition", &spawn_draft_.override_material);
        if (spawn_draft_.override_material) {
            ImGui::SliderFloat("Iron", &spawn_draft_.material_iron, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Silicate", &spawn_draft_.material_silicate, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Ice/Water", &spawn_draft_.material_ice, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Hydrogen", &spawn_draft_.material_hydrogen, 0.0f, 1.0f, "%.2f");
            float total = spawn_draft_.material_iron + spawn_draft_.material_silicate +
                          spawn_draft_.material_ice + spawn_draft_.material_hydrogen;
            if (total > 1.0e-6f)
                ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f), "Current Sum: %.2f", total);
            if (ImGui::SmallButton("Normalize Composition")) {
                float s = std::max(total, 1.0e-6f);
                spawn_draft_.material_iron /= s;
                spawn_draft_.material_silicate /= s;
                spawn_draft_.material_ice /= s;
                spawn_draft_.material_hydrogen /= s;
            }
        }

        if (ImGui::CollapsingHeader("Quick Presets")) {
            if (ImGui::Button("Add Solar System", ImVec2(-1, 0))) {
                glm::vec3 offset = camera.target;
                CelestialBody s;
                s.pos = offset; s.mass = 1.0f; s.radius = 30.0f;
                s.temperature = 5778.0f; s.type = classify_star_spectral(5778.0f, 1.0f);
                s.seed = 42;
                s.fuel = 0.72f;
                s.angular_vel = (2.0f * 3.14159265359f) / (26.0f * 24.0f * 3600.0f);
                s.luminosity = std::pow(std::max(s.mass, 0.08f), 3.2f) * 0.1f;
                s.name = generate_body_name(s.seed, s.type);
                int star_idx = (int)state.bodies.size();
                state.bodies.push_back(s); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);

                float radii[] = {80, 140, 210, 300};
                float masses[] = {1.66e-7f, 3.00e-6f, 3.22e-7f, 9.54e-4f};
                float temps[] = {600.0f, 300.0f, 180.0f, 90.0f};
                for (int i = 0; i < 4; i++) {
                    CelestialBody p;
                    float angle = (float)i * 1.57f;
                    p.pos = offset + glm::vec3(cosf(angle) * radii[i], 0, sinf(angle) * radii[i]);
                    float v = std::sqrt(cfg.G * s.mass / radii[i]);
                    p.vel = s.vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                    p.mass = masses[i]; p.radius = 6 + masses[i] * 3;
                    p.temperature = temps[i];
                    p.type = CTYPE_PLANET; p.parent = star_idx;
                    p.seed = (uint32_t)(i * 31337 + 54321);
                    p.name = generate_body_name(p.seed, p.type);
                    refresh_body_render_state(p, &state);
                    state.bodies.push_back(p); state.trails.emplace_back();
                }
            }

            if (ImGui::Button("Add Binary Stars", ImVec2(-1, 0))) {
                glm::vec3 center = camera.target;
                float sep = 60.0f;
                float v = std::sqrt(cfg.G * 50.0f / sep);

                CelestialBody s1;
                s1.pos = center + glm::vec3(sep * 0.5f, 0, 0);
                s1.vel = glm::vec3(0, 0, v * 0.5f);
                s1.mass = 50.0f; s1.radius = 22.0f; s1.temperature = 8000.0f;
                s1.type = classify_star_spectral(8000.0f, 50.0f); s1.seed = 111;
                s1.fuel = 0.68f;
                s1.angular_vel = (2.0f * 3.14159265359f) / (38.0f * 3600.0f);
                s1.luminosity = std::pow(std::max(s1.mass, 0.08f), 3.2f) * 0.1f;
                s1.name = generate_body_name(s1.seed, s1.type);
                state.bodies.push_back(s1); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);

                CelestialBody s2;
                s2.pos = center - glm::vec3(sep * 0.5f, 0, 0);
                s2.vel = glm::vec3(0, 0, -v * 0.5f);
                s2.mass = 50.0f; s2.radius = 22.0f; s2.temperature = 3500.0f;
                s2.type = classify_star_spectral(3500.0f, 50.0f); s2.seed = 222;
                s2.fuel = 0.62f;
                s2.angular_vel = (2.0f * 3.14159265359f) / (84.0f * 3600.0f);
                s2.luminosity = std::pow(std::max(s2.mass, 0.08f), 3.2f) * 0.1f;
                s2.name = generate_body_name(s2.seed, s2.type);
                state.bodies.push_back(s2); state.trails.emplace_back();
                refresh_body_render_state(state.bodies.back(), &state);
            }

            if (ImGui::Button("Add Asteroid Belt", ImVec2(-1, 0))) {
                std::mt19937 rng((unsigned)sim_time_);
                auto randf = [&](float lo, float hi) {
                    return std::uniform_real_distribution<float>(lo, hi)(rng);
                };
                float nearest_mass = 1.0f;
                glm::vec3 nearest_pos = camera.target;
                glm::vec3 nearest_vel(0);
                for (auto& b : state.bodies) {
                    if (is_star_type(b.type)) {
                        float d = glm::length(b.pos - camera.target);
                        if (d < 800.0f) {
                            nearest_mass = b.mass;
                            nearest_pos = b.pos;
                            nearest_vel = b.vel;
                        }
                    }
                }
                for (int i = 0; i < 30; i++) {
                    CelestialBody a;
                    float r = randf(400.0f, 500.0f);
                    float angle = randf(0, 6.2832f);
                    a.pos = nearest_pos + glm::vec3(cosf(angle) * r, randf(-10, 10), sinf(angle) * r);
                    float v = std::sqrt(cfg.G * nearest_mass / r) * randf(0.9f, 1.1f);
                    a.vel = nearest_vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                    a.mass = randf(5.0e-11f, 3.0e-9f); a.radius = randf(0.4f, 1.6f);
                    a.type = CTYPE_ASTEROID;
                    a.seed = (uint32_t)(rng());
                    a.name = generate_body_name(a.seed, a.type);
                    state.bodies.push_back(a); state.trails.emplace_back();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.58f, 0.60f, 0.66f, 1.0f), "Left-click empty space in viewport to place");
    ImU32 col = CTYPE_COLORS[spawn_type % CTYPE_COUNT];
    float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.42f, g * 0.42f, b * 0.42f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.60f, g * 0.60f, b * 0.60f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.75f, g * 0.75f, b * 0.75f, 1.0f));
    char label[64];
    snprintf(label, sizeof(label), "Spawn %s at Origin", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
    if (ImGui::Button(label, ImVec2(-1, 34))) {
        spawn_at(camera.target);
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

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
            ImGui::Separator();

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

    ImGui::SameLine(ImGui::GetWindowWidth() - 72);
    bool is_tracked = camera.focus_active && camera.focus_body == selected_body;
    if (is_tracked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.9f));
        if (ImGui::SmallButton("Untrack")) camera.release_focus();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::SmallButton("Track")) {
            camera.focus_on(b.pos, selected_body);
            camera.target_distance = b.radius * 8.0f;
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

    if (ImGui::Button("Delete Body", ImVec2(-1, 0))) {
        b.marked_for_removal = true;
        if (camera.focus_body == selected_body) camera.release_focus();
        selected_body = -1;
        inspector_visible_ = false;
    }

    ImGui::End();
}

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

        char time_buf[64], rate_buf[64];
        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        format_sim_time(std::pow(10.0, cfg.time_exponent), rate_buf, sizeof(rate_buf));

        char time_label[96];
        snprintf(time_label, sizeof(time_label), "T: %s###BottomTimeBtn", time_buf);
        char time_visible[64];
        snprintf(time_visible, sizeof(time_visible), "T: %s", time_buf);
        char rate_text[96];
        snprintf(rate_text, sizeof(rate_text), "%s%s/s", reverse_time_ ? "-" : "", rate_buf);
        char bodies_text[64];
        snprintf(bodies_text, sizeof(bodies_text), "%zu bodies", state.count());

        float time_btn_w = ImGui::CalcTextSize(time_visible).x + 14.0f;
        float rate_w = ImGui::CalcTextSize(rate_text).x;
        float bodies_w = ImGui::CalcTextSize(bodies_text).x;
        float sep_w = ImGui::CalcTextSize("|").x;
        float total_w = time_btn_w + 8.0f + sep_w + 8.0f + rate_w + 8.0f + sep_w + 8.0f + bodies_w;

        ImGui::SameLine(std::max(8.0f, display_w - total_w - 16.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.11f, 0.05f, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.18f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.22f, 0.10f, 1.0f));
        if (ImGui::Button(time_label, ImVec2(time_btn_w, 22.0f)))
            ImGui::OpenPopup("##BottomTimeStepPopup");
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.45f, 0.40f, 0.26f, 0.85f), "|");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "%s", rate_text);
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.45f, 0.40f, 0.26f, 0.85f), "|");
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "%s", bodies_text);

        if (ImGui::BeginPopup("##BottomTimeStepPopup")) {
            ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.56f, 1.0f), "Time Control");
            float exp_f = (float)cfg.time_exponent;
            if (ImGui::SliderFloat("Rate##Bottom", &exp_f, -9.0f, 21.0f, ""))
                cfg.time_exponent = (double)exp_f;
            ImGui::Checkbox("Adaptive Time-Step##Bottom", &cfg.adaptive_time_step);
            char popup_rate[64], popup_time[64];
            format_sim_time(std::pow(10.0, cfg.time_exponent), popup_rate, sizeof(popup_rate));
            format_sim_time(cfg.sim_time_accumulated, popup_time, sizeof(popup_time));
            ImGui::Text("Rate: %s/s", popup_rate);
            ImGui::Text("Sim Time: %s", popup_time);
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

            if (ImGui::TreeNodeEx("Simulation")) {
                if (ImGui::MenuItem(paused ? "Resume (Space)" : "Pause (Space)")) {
                    paused = !paused; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("New Simulation")) {
                    reset_simulation(); show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Empty Universe")) {
                    state.clear(); cfg.body_count = 0;
                    selected_body = -1; sim_time_ = 0.0f;
                    cfg.sim_time_accumulated = 0.0;
                    show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save (Ctrl+S)")) {
                    show_save_dialog_ = true; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Load (Ctrl+L)")) {
                    show_load_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Body...")) {
                    show_import_dialog_ = true; show_menu_popup_ = false;
                }
                if (selected_body >= 0 && ImGui::MenuItem("Export Selected Body...")) {
                    show_export_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Panels")) {
                bool tmp;
                tmp = spawn_menu_visible_;
                if (ImGui::MenuItem("Spawn Menu", nullptr, tmp)) { spawn_menu_visible_ = !spawn_menu_visible_; }
                tmp = body_list_visible_;
                if (ImGui::MenuItem("Body List", nullptr, tmp)) { body_list_visible_ = !body_list_visible_; }
                tmp = inspector_visible_;
                if (ImGui::MenuItem("Inspector", nullptr, tmp)) { inspector_visible_ = !inspector_visible_; }
                ImGui::Separator();
                ImGui::MenuItem("Show Orbits", nullptr, &cfg.show_orbits);
                ImGui::MenuItem("Show Trails", nullptr, &cfg.show_trails);
                ImGui::MenuItem("Auto-hide Bottom Bar", nullptr, &bottom_bar_autohide_);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("System Management")) {
                if (ImGui::MenuItem("Balance System Momentum")) {
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
                if (ImGui::MenuItem("Auto Orbit")) {
                    set_auto_orbit(0.0f, 1.0f);
                }
                if (ImGui::MenuItem("Expand System")) {
                    scale_system(1.05f);
                }
                if (ImGui::MenuItem("Shrink System")) {
                    scale_system(0.95f);
                }
                if (ImGui::MenuItem("Increase Eccentricity")) {
                    adjust_eccentricity(1.20f, 0.93f);
                }
                if (ImGui::MenuItem("Decrease Eccentricity")) {
                    adjust_eccentricity(0.80f, 1.05f);
                }
                if (ImGui::MenuItem("Make 2D - Zero All Height Values")) {
                    for_each_body([&](CelestialBody& b) {
                        b.pos.y = 0.0f;
                        b.vel.y = 0.0f;
                    });
                    camera.target.y = 0.0f;
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Motion")) {
                if (ImGui::MenuItem("Reverse Time", nullptr, reverse_time_)) {
                    reverse_time_ = !reverse_time_;
                }
                if (ImGui::MenuItem("Reverse All Velocities")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= -1.0f; });
                }
                if (ImGui::MenuItem("Halt All Velocities")) {
                    for_each_body([&](CelestialBody& b) { b.vel = glm::vec3(0.0f); });
                }
                if (ImGui::MenuItem("Halt All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel = 0.0f; });
                }
                if (ImGui::MenuItem("+2% All Speeds")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= 1.02f; });
                }
                if (ImGui::MenuItem("-2% All Speeds")) {
                    for_each_body([&](CelestialBody& b) { b.vel *= 0.98f; });
                }
                if (ImGui::MenuItem("+2% All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel *= 1.02f; });
                }
                if (ImGui::MenuItem("-2% All Rotations")) {
                    for_each_body([&](CelestialBody& b) { b.angular_vel *= 0.98f; });
                }
                float dv_10kms = 10.0f / SIM_UNIT_TO_KM;
                if (ImGui::MenuItem("Add Velocity of 10 km/s on X")) {
                    for_each_body([&](CelestialBody& b) { b.vel.x += dv_10kms; });
                }
                if (ImGui::MenuItem("Add Velocity of -10 km/s on X")) {
                    for_each_body([&](CelestialBody& b) { b.vel.x -= dv_10kms; });
                }
                if (ImGui::MenuItem("Add Velocity of 10 km/s on Y")) {
                    for_each_body([&](CelestialBody& b) { b.vel.y += dv_10kms; });
                }
                if (ImGui::MenuItem("Add Velocity of -10 km/s on Y")) {
                    for_each_body([&](CelestialBody& b) { b.vel.y -= dv_10kms; });
                }
                if (ImGui::MenuItem("Add Velocity of 10 km/s on Z")) {
                    for_each_body([&](CelestialBody& b) { b.vel.z += dv_10kms; });
                }
                if (ImGui::MenuItem("Add Velocity of -10 km/s on Z")) {
                    for_each_body([&](CelestialBody& b) { b.vel.z -= dv_10kms; });
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Performance Management")) {
                if (ImGui::MenuItem("Delete All Particles/Dust")) {
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
                if (ImGui::MenuItem("Delete All Fragments")) {
                    for (auto& b : state.bodies) {
                        if (b.marked_for_removal) continue;
                        if ((int)b.frag_generation > 0) b.marked_for_removal = true;
                    }
                    cleanup_bodies();
                }
                if (ImGui::MenuItem("Delete All Escaping Bodies")) {
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
            if (ImGui::TreeNodeEx("Cosmos Settings")) {
                if (ImGui::CollapsingHeader("General Physics")) {
                    ImGui::SliderFloat("G##Menu", &cfg.G, 0.1f, 10.0f);
                    ImGui::SliderFloat("Softening##Menu", &cfg.softening, 1.0f, 50.0f);
                    ImGui::Checkbox("Collisions##Menu", &cfg.collisions);
                    ImGui::Checkbox("Tidal Forces##Menu", &cfg.tidal_forces);
                    ImGui::Checkbox("Velocity Verlet##Menu", &cfg.velocity_verlet);
                    ImGui::Checkbox("Barnes-Hut Gravity##Menu", &cfg.barnes_hut);
                    if (cfg.barnes_hut) {
                        ImGui::SliderFloat("BH Theta##Menu", &cfg.barnes_hut_theta, 0.2f, 1.6f, "%.2f");
                        ImGui::SliderInt("BH Min Bodies##Menu", &cfg.barnes_hut_min_bodies, 16, 2000);
                    }
                    ImGui::Checkbox("Show Orbits##Menu", &cfg.show_orbits);
                    ImGui::Checkbox("Show Trails##Menu", &cfg.show_trails);
                    int trail_len = (int)cfg.trail_length;
                    if (ImGui::SliderInt("Trail Length##Menu", &trail_len, 0, 500))
                        cfg.trail_length = (uint32_t)trail_len;
                }

                if (ImGui::CollapsingHeader("Camera")) {
                    ImGui::SliderFloat("FOV##Menu", &camera.fov, 20.0f, 90.0f);
                    float log_dist = std::log10(std::max(camera.distance, 0.01f));
                    if (ImGui::SliderFloat("Distance##Menu", &log_dist, 1.0f, 3.7f, "10^%.1f"))
                        camera.distance = std::pow(10.0f, log_dist);
                    if (ImGui::Button("Reset Camera##Menu")) camera = OrbitCamera{};
                }

                if (ImGui::CollapsingHeader("Collision & Fragmentation")) {
                    ImGui::Checkbox("Merging##Menu", &cfg.collision_merging);
                    ImGui::Checkbox("Fragmentation##Menu", &cfg.collision_fragmentation);
                    ImGui::SliderFloat("Merge Speed##Menu", &cfg.merge_speed_threshold, 1.0f, 20.0f);
                    ImGui::SliderFloat("Fragment Speed##Menu", &cfg.fragment_speed_threshold, 10.0f, 50.0f);
                    ImGui::SliderInt("Fragment Count##Menu", &cfg.fragment_count, 1, 12);
                    ImGui::SliderFloat("Min Frag Mass##Menu", &cfg.min_fragment_mass, 1.0e-9f, 1.0f, "%.6f",
                                       ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderInt("Max Frag Depth##Menu", &cfg.max_frag_generation, 0, 5);
                }

                if (ImGui::CollapsingHeader("Thermal & Roche")) {
                    ImGui::Checkbox("Temperature##Menu", &cfg.temperature_system);
                    ImGui::Checkbox("Evaporation##Menu", &cfg.evaporation);
                    ImGui::Checkbox("Roche Limit##Menu", &cfg.roche_limit);
                    if (cfg.roche_limit) {
                        ImGui::Checkbox("Fluid Roche Limit##Menu", &cfg.roche_limit_fluid);
                        ImGui::Checkbox("Rigid Roche Limit##Menu", &cfg.roche_limit_rigid);
                        if (!cfg.roche_limit_fluid && !cfg.roche_limit_rigid) {
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                               "Enable at least one Roche mode.");
                        }
                    }
                    ImGui::Checkbox("Material Phases##Menu", &cfg.material_phases);
                    ImGui::Checkbox("Planetary Rings##Menu", &cfg.planetary_rings);
                    if (cfg.planetary_rings) {
                        ImGui::SliderFloat("Ring Inner Scale##Menu", &cfg.ring_inner_scale, 0.6f, 3.0f, "%.2f");
                        ImGui::SliderFloat("Ring Outer Scale##Menu", &cfg.ring_outer_scale, 0.6f, 3.0f, "%.2f");
                        ImGui::SliderFloat("Ring Density Scale##Menu", &cfg.ring_density_scale, 0.2f, 3.0f, "%.2f");
                        ImGui::SliderFloat("Ring Thickness##Menu", &cfg.ring_thickness_scale, 0.3f, 4.0f, "%.2f");
                        ImGui::SliderFloat("Ring Particle Scale##Menu", &cfg.ring_particle_scale, 0.2f, 5.0f, "%.2f");
                        ImGui::SliderFloat("Ring Mass Scale##Menu", &cfg.ring_mass_scale, 0.1f, 5.0f, "%.2f");
                    }
                    if (cfg.temperature_system)
                        ImGui::SliderFloat("Cooling##Menu", &cfg.radiative_cooling, 0.0f, 0.01f, "%.4f");
                }

                if (ImGui::CollapsingHeader("Stellar")) {
                    ImGui::Checkbox("Stellar Evolution##Menu", &cfg.stellar_evolution);
                    if (cfg.stellar_evolution)
                        ImGui::SliderFloat("Star Timescale##Menu", &cfg.stellar_timescale, 10.0f, 500.0f);
                }

                if (ImGui::CollapsingHeader("Rendering & Lighting")) {
                    ImGui::Checkbox("Star Lighting##Menu", &cfg.star_lighting);
                    ImGui::Checkbox("Uniform Lighting##Menu", &cfg.uniform_lighting);
                    if (cfg.star_lighting) {
                        ImGui::Checkbox("Fast Star Lighting##Menu", &cfg.fast_star_lighting);
                        ImGui::SliderFloat("Ambient##Menu", &cfg.ambient_strength, 0.0f, 0.5f);
                    }

                    ImGui::Separator();
                    ImGui::Checkbox("HQ Shading##Menu", &cfg.cosmos_hq_shading);
                    ImGui::Checkbox("Background Starfield##Menu", &cfg.cosmos_background_starfield);
                    static const char* BG_PRESETS[] = {
                        "Realistic",
                        "Deep Black",
                        "Nebula",
                        "Warm Dust",
                        "Blue Haze"
                    };
                    ImGui::Combo("Background Preset##Menu", &cfg.cosmos_background_preset,
                                 BG_PRESETS, IM_ARRAYSIZE(BG_PRESETS));
                    ImGui::Checkbox("Star Corona##Menu", &cfg.cosmos_star_corona);
                    ImGui::Checkbox("Comet Tails##Menu", &cfg.cosmos_comet_tails);
                    ImGui::Checkbox("Black Hole Lensing##Menu", &cfg.cosmos_blackhole_lensing);
                    ImGui::Checkbox("Space Fabric Grid##Menu", &cfg.cosmos_space_fabric);
                    if (cfg.cosmos_space_fabric) {
                        ImGui::SliderFloat("Fabric Square Size##Menu", &cfg.cosmos_space_fabric_grid_size,
                                           5.0f, 200.0f, "%.1f u", ImGuiSliderFlags_Logarithmic);
                        ImGui::SliderFloat("Fabric Curvature##Menu", &cfg.cosmos_space_fabric_strength,
                                           0.1f, 3.0f, "%.2f");
                        if (ImGui::Button("Snap Fabric View Isometric##Menu")) {
                            camera.azimuth = glm::radians(45.0f);
                            camera.elevation = glm::radians(35.2643897f);
                            camera.target_distance = camera.distance;
                        }
                    }
                    ImGui::SliderInt("Cosmos Quality##Menu", &cfg.cosmos_quality, 0, 2,
                                     cfg.cosmos_quality == 0 ? "Low" :
                                     (cfg.cosmos_quality == 1 ? "Balanced" : "High"));
                }

                if (ImGui::CollapsingHeader("Time Control")) {
                    float exp_f = (float)cfg.time_exponent;
                    if (ImGui::SliderFloat("Time Rate##Menu", &exp_f, -9.0f, 21.0f, ""))
                        cfg.time_exponent = (double)exp_f;
                    ImGui::Checkbox("Adaptive Time-Stepping##Menu", &cfg.adaptive_time_step);
                    if (cfg.adaptive_time_step) {
                        ImGui::SliderFloat("Adaptive Safety##Menu", &cfg.adaptive_step_safety, 0.01f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Adaptive Min dt##Menu", &cfg.adaptive_step_min,
                                           1.0e-6f, 1.0f, "%.6f", ImGuiSliderFlags_Logarithmic);
                        ImGui::SliderFloat("Adaptive Max dt##Menu", &cfg.adaptive_step_max,
                                           0.01f, 1.0e7f, "%.2f", ImGuiSliderFlags_Logarithmic);
                    }
                    char rate_buf[64], time_buf[64];
                    format_sim_time(std::pow(10.0, cfg.time_exponent), rate_buf, sizeof(rate_buf));
                    format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
                    ImGui::Text("Rate: %s/s", rate_buf);
                    ImGui::Text("Sim Time: %s", time_buf);
                    draw_time_presets();
                }

                if (ImGui::CollapsingHeader("General Relativity")) {
                    ImGui::Checkbox("GR Corrections##Menu", &cfg.gr_enabled);
                    ImGui::Checkbox("Parallel Gravity##Menu", &cfg.parallel_gravity);
                    if (cfg.gr_enabled) {
                        ImGui::SliderFloat("Precession##Menu", &cfg.gr_precession_scale, 0.0f, 10.0f);
                        ImGui::SliderFloat("Time Dilation##Menu", &cfg.gr_time_dilation, 0.0f, 5.0f);
                        ImGui::SliderFloat("Frame Drag##Menu", &cfg.gr_frame_dragging, 0.0f, 5.0f);
                        ImGui::SliderFloat("Speed of Light##Menu", &cfg.speed_of_light, 50.0f, 1000.0f);
                    }
                }

                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Performance")) {
                if (ImGui::TreeNodeEx("Dynamic Budget")) {
                    ImGui::Checkbox("Object Budget##Perf", &cfg.dynamic_budget_enabled);
                    ImGui::Text("Current FPS: %.1f", smoothed_fps_);
                    ImGui::SliderFloat("Target FPS##Perf", &cfg.dynamic_target_fps, 20.0f, 240.0f, "%.0f");
                    ImGui::SliderInt("Max Fragments##Perf", &cfg.dynamic_max_fragments, 0, 3000);
                    ImGui::SliderInt("Max Non-Attracting##Perf", &cfg.dynamic_max_non_attracting, 0, 10000);
                    float explosion_density_pct = cfg.dynamic_explosion_density * 100.0f;
                    if (ImGui::SliderFloat("Explosion Density##Perf", &explosion_density_pct, 1.0f, 100.0f, "%.0f%%"))
                        cfg.dynamic_explosion_density = std::clamp(explosion_density_pct / 100.0f, 0.01f, 1.0f);
                    float reduction_pct = cfg.dynamic_reduction_percent * 100.0f;
                    if (ImGui::SliderFloat("Reduction Percentage##Perf", &reduction_pct, 1.0f, 100.0f, "%.0f%%"))
                        cfg.dynamic_reduction_percent = std::clamp(reduction_pct / 100.0f, 0.01f, 1.0f);
                    int dust_mode = cfg.dust_debug_non_attracting ? 1 : 0;
                    const char* dust_modes[] = {"Standard", "Non-attracting"};
                    if (ImGui::Combo("Dust Mode##Perf", &dust_mode, dust_modes, IM_ARRAYSIZE(dust_modes))) {
                        cfg.dust_debug_non_attracting = (dust_mode == 1);
                        apply_dust_debug_mode();
                    }
                    if (!cfg.dynamic_budget_enabled) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                           "Budget disabled: fragment growth is uncapped.");
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Navigation")) {
                if (ImGui::MenuItem("Return to Launcher")) { request_launcher = true; request_quit = true; }
                if (ImGui::MenuItem("Quit")) { request_quit = true; }
                ImGui::TreePop();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

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
