#include "cosmos/cosmos_app_internal.h"
#include "cosmos/ui/cosmos_ui_data.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>

void CosmosApp::render_overlay() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float aspect = W / H;
    float fov_rad = glm::radians(camera.fov);
    glm::dvec3 eye = camera.eye_position_d();

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
            float orbit_alpha_scale = std::clamp(cfg.orbit_line_alpha, 0.0f, 1.0f);
            int alpha = (i == selected_body)
                ? (int)std::clamp(255.0f * std::max(orbit_alpha_scale * 2.4f, 0.18f), 0.0f, 255.0f)
                : (int)std::clamp(255.0f * orbit_alpha_scale, 0.0f, 255.0f);
            float width = (i == selected_body)
                ? std::max(0.5f, cfg.orbit_line_width * 1.9f)
                : std::max(0.5f, cfg.orbit_line_width);
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

            ImU32 base_c = body_color(state.bodies[i]);
            int base_r = (base_c >> IM_COL32_R_SHIFT) & 0xFF;
            int base_g = (base_c >> IM_COL32_G_SHIFT) & 0xFF;
            int base_b = (base_c >> IM_COL32_B_SHIFT) & 0xFF;

            for (size_t j = 1; j < trail.size(); j++) {
                auto p0 = project(trail[j - 1], vp, W, H);
                auto p1 = project(trail[j], vp, W, H);
                if (!p0.visible || !p1.visible) continue;

                float frac = (float)j / (float)trail.size();
                int alpha = (int)std::clamp(frac * 80.0f * std::clamp(cfg.trail_alpha_scale, 0.0f, 4.0f),
                                            0.0f, 255.0f);
                float width = (1.0f + frac * 1.5f) * std::clamp(cfg.trail_width_scale, 0.1f, 4.0f);

                // Velocity-based color: compute speed from trail segment displacement
                float seg_speed = glm::length(trail[j] - trail[j - 1]);
                // Map speed to 0..1 (log scale for better contrast)
                float speed_t = std::clamp(std::log2(seg_speed * 20.0f + 1.0f) * 0.25f, 0.0f, 1.0f);
                // Blend: body color at low speed → white at mid → warm highlight at high
                int tr, tg, tb;
                if (speed_t < 0.5f) {
                    float t2 = speed_t * 2.0f;
                    tr = (int)std::clamp(base_r + (255 - base_r) * t2 * 0.5f, 0.0f, 255.0f);
                    tg = (int)std::clamp(base_g + (255 - base_g) * t2 * 0.5f, 0.0f, 255.0f);
                    tb = (int)std::clamp(base_b + (255 - base_b) * t2 * 0.4f, 0.0f, 255.0f);
                } else {
                    float t2 = (speed_t - 0.5f) * 2.0f;
                    tr = (int)std::clamp(base_r * 0.5f + 255.0f * 0.5f + t2 * (255.0f - base_r * 0.5f - 255.0f * 0.5f + 60.0f), 0.0f, 255.0f);
                    tg = (int)std::clamp(base_g * 0.5f + 255.0f * 0.5f - t2 * 80.0f, 0.0f, 255.0f);
                    tb = (int)std::clamp(base_b * 0.5f + 255.0f * 0.5f - t2 * 140.0f, 0.0f, 255.0f);
                }

                fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy),
                            IM_COL32(tr, tg, tb, alpha), width);
            }
        }
    }

    // Velocity arrows
    if (show_velocity_arrows_) {
        for (size_t i = 0; i < state.bodies.size(); ++i) {
            const auto& b = state.bodies[i];
            if (b.marked_for_removal) continue;
            float speed = glm::length(b.vel);
            if (speed < 1.0e-6f) continue;

            auto p0 = project(b.pos, vp, W, H);
            if (!p0.visible || p0.depth < 0.0f) continue;

            // Arrow length: proportional to velocity, clamped to screen space
            float sr = screen_radius(b.radius, p0.depth, fov_rad, H);
            float arrow_len = std::clamp(sr * speed * 300.0f, sr * 1.5f, 120.0f);

            glm::vec3 vel_dir = b.vel / speed;
            glm::vec3 arrow_end_world = b.pos + vel_dir * (b.radius * 2.0f + speed * 50.0f);
            auto p1 = project(arrow_end_world, vp, W, H);
            if (!p1.visible) continue;

            // Compute screen direction and normalize to arrow_len
            float dx = p1.sx - p0.sx;
            float dy = p1.sy - p0.sy;
            float screen_dist = std::sqrt(dx * dx + dy * dy);
            if (screen_dist < 2.0f) continue;
            float scale = arrow_len / screen_dist;
            float ex = p0.sx + dx * scale;
            float ey = p0.sy + dy * scale;

            // Color based on speed
            float t = std::clamp(speed * 2000.0f, 0.0f, 1.0f);
            ImU32 arrow_col = IM_COL32(
                (int)(100 + 155 * t), (int)(200 - 60 * t), (int)(255 - 200 * t), 180);

            fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(ex, ey), arrow_col, 1.8f);
            // Arrowhead
            float nx = -(ey - p0.sy) / arrow_len * 5.0f;
            float ny =  (ex - p0.sx) / arrow_len * 5.0f;
            fg->AddTriangleFilled(
                ImVec2(ex, ey),
                ImVec2(ex - dx * scale * 0.15f + nx, ey - dy * scale * 0.15f + ny),
                ImVec2(ex - dx * scale * 0.15f - nx, ey - dy * scale * 0.15f - ny),
                arrow_col);
        }
    }

    if (cfg.show_body_labels) {
        float label_min_dist = std::clamp(cfg.body_label_min_distance, 0.0f, 1.0e8f);
        float label_max_dist = std::clamp(cfg.body_label_max_distance,
                                          std::max(label_min_dist, 1.0e-3f), 1.0e8f);
        struct LabelCandidate {
            int index = -1;
            Projected projected{};
            float screen_r = 0.0f;
            float priority = 0.0f;
        };

        std::vector<LabelCandidate> labels;
        labels.reserve(std::min<size_t>(state.bodies.size(), 128));
        for (int i = 0; i < (int)state.bodies.size(); ++i) {
            if (i == selected_body) continue;
            const auto& b = state.bodies[(size_t)i];
            if (b.marked_for_removal || b.non_attracting || b.type == CTYPE_DUST) continue;
            if (fragment_like_body(b) && !is_star_type(b.type) && !is_black_hole_type(b.type))
                continue;

            double cam_dist = glm::length(glm::dvec3(b.pos) - eye);
            if (!std::isfinite(cam_dist) ||
                cam_dist < (double)label_min_dist ||
                cam_dist > (double)label_max_dist) {
                continue;
            }

            auto p = project(b.pos, vp, W, H);
            if (!p.visible) continue;

            const char* name = b.name.empty()
                ? CTYPE_NAMES[std::min(b.type, (uint32_t)CTYPE_COUNT - 1)]
                : b.name.c_str();
            if (!name || name[0] == '\0') continue;

            float sr = screen_radius(b.radius, p.depth, fov_rad, H);
            if (sr < 1.0f && !is_star_type(b.type) && !is_black_hole_type(b.type) && b.mass < 1.0e-5f)
                continue;

            LabelCandidate candidate;
            candidate.index = i;
            candidate.projected = p;
            candidate.screen_r = sr;
            candidate.priority = sr + (is_star_type(b.type) ? 8.0f : 0.0f) +
                (is_black_hole_type(b.type) ? 6.0f : 0.0f) +
                std::clamp((label_max_dist - (float)cam_dist) / std::max(label_max_dist, 1.0f), 0.0f, 1.0f) * 4.0f;
            labels.push_back(candidate);
        }

        std::sort(labels.begin(), labels.end(), [](const LabelCandidate& a, const LabelCandidate& b) {
            return a.priority > b.priority;
        });

        std::vector<ImVec4> occupied;
        occupied.reserve(labels.size());
        auto overlaps = [](const ImVec4& a, const ImVec4& b) {
            return a.x < b.z && a.z > b.x && a.y < b.w && a.w > b.y;
        };

        int drawn = 0;
        int label_cap = std::min<int>((int)labels.size(), std::clamp(cfg.body_label_max_count, 1, 512));
        float label_alpha_scale = std::clamp(cfg.body_label_opacity, 0.05f, 1.0f);
        for (const auto& candidate : labels) {
            if (drawn >= label_cap) break;
            const auto& b = state.bodies[(size_t)candidate.index];
            const char* name = b.name.empty()
                ? CTYPE_NAMES[std::min(b.type, (uint32_t)CTYPE_COUNT - 1)]
                : b.name.c_str();
            ImVec2 name_size = ImGui::CalcTextSize(name);
            float label_x = std::clamp(candidate.projected.sx + std::max(candidate.screen_r + 8.0f, 10.0f),
                                       6.0f, W - name_size.x - 6.0f);
            float label_y = std::clamp(candidate.projected.sy - name_size.y * 0.5f,
                                       6.0f, H - name_size.y - 6.0f);
            ImVec4 rect(label_x - 4.0f, label_y - 2.0f,
                        label_x + name_size.x + 4.0f, label_y + name_size.y + 2.0f);

            bool blocked = false;
            for (const auto& other : occupied) {
                ImVec4 expanded(other.x - 6.0f, other.y - 4.0f, other.z + 6.0f, other.w + 4.0f);
                if (overlaps(rect, expanded)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;

            ImU32 c = body_color(b);
            int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
            int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
            int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;
            ImU32 text_col = IM_COL32(std::min(cr + 48, 255),
                                      std::min(cg + 48, 255),
                                      std::min(cb + 48, 255),
                                      (int)std::clamp(235.0f * label_alpha_scale, 0.0f, 255.0f));
            fg->AddRectFilled(ImVec2(rect.x, rect.y), ImVec2(rect.z, rect.w),
                              IM_COL32(8, 10, 20,
                                       (int)std::clamp(140.0f * label_alpha_scale, 0.0f, 255.0f)), 3.0f);
            fg->AddText(ImVec2(label_x + 1.0f, label_y + 1.0f),
                        IM_COL32(0, 0, 0, (int)std::clamp(150.0f * label_alpha_scale, 0.0f, 255.0f)), name);
            fg->AddText(ImVec2(label_x, label_y), text_col, name);
            occupied.push_back(rect);
            drawn++;
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

    // Lock indicators on locked bodies
    for (size_t i = 0; i < state.bodies.size(); ++i) {
        const auto& lb = state.bodies[i];
        if (!lb.locked || lb.marked_for_removal) continue;
        auto lp = project(lb.pos, vp, W, H);
        if (!lp.visible) continue;
        float lsr = std::max(screen_radius(lb.radius, lp.depth, fov_rad, H), 6.0f);
        // Small "LOCKED" badge below the body
        const char* lock_text = "LOCKED";
        ImVec2 lt_size = ImGui::CalcTextSize(lock_text);
        float ltx = lp.sx - lt_size.x * 0.5f;
        float lty = lp.sy + lsr + 4.0f;
        fg->AddRectFilled(ImVec2(ltx - 3, lty - 1),
                          ImVec2(ltx + lt_size.x + 3, lty + lt_size.y + 1),
                          IM_COL32(200, 80, 30, 140), 2.0f);
        fg->AddText(ImVec2(ltx, lty), IM_COL32(255, 200, 100, 220), lock_text);
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

    // Spawn drag height indicator — vertical line from orbital plane to spawn position
    if (spawn_dragging_ && std::abs(spawn_drag_y_offset_) > 0.1f) {
        glm::vec3 base_pos = spawn_drag_base_pos_;
        glm::vec3 drag_pos = base_pos;
        drag_pos.y += spawn_drag_y_offset_;

        // Project both points to screen
        Projected p_base = project(base_pos, vp, W, H);
        Projected p_drag = project(drag_pos, vp, W, H);

        if (p_base.visible && p_drag.visible) {
            // Dashed vertical line
            ImU32 line_col = IM_COL32(100, 200, 255, 180);
            float dash_len = 6.0f, gap_len = 4.0f;
            float lx = p_base.sx;
            float y0 = p_base.sy, y1 = p_drag.sy;
            float dir = (y1 < y0) ? -1.0f : 1.0f;
            float total = std::abs(y1 - y0);
            float drawn = 0.0f;
            while (drawn < total) {
                float seg = std::min(dash_len, total - drawn);
                fg->AddLine(ImVec2(lx, y0 + dir * drawn),
                            ImVec2(lx, y0 + dir * (drawn + seg)), line_col, 1.5f);
                drawn += seg + gap_len;
            }

            // Small horizontal tick at base (orbital plane marker)
            fg->AddLine(ImVec2(lx - 8, y0), ImVec2(lx + 8, y0), line_col, 1.5f);

            // Height label next to the drag point
            float height_km = spawn_drag_y_offset_ * SIM_UNIT_TO_KM;
            char height_label[64];
            if (std::abs(height_km) < 10000.0f)
                snprintf(height_label, sizeof(height_label), "%+.0f km", height_km);
            else
                snprintf(height_label, sizeof(height_label), "%+.1f Earth R",
                         height_km / EARTH_RADIUS_KM_REAL);
            ImVec2 hl_size = ImGui::CalcTextSize(height_label);
            float lbl_x = p_drag.sx + 12.0f;
            float lbl_y = p_drag.sy - hl_size.y * 0.5f;
            fg->AddRectFilled(ImVec2(lbl_x - 4, lbl_y - 2),
                              ImVec2(lbl_x + hl_size.x + 4, lbl_y + hl_size.y + 2),
                              IM_COL32(10, 14, 24, 200), 3.0f);
            fg->AddText(ImVec2(lbl_x, lbl_y), IM_COL32(100, 200, 255, 240), height_label);
        }
    }

    if (cfg.cosmos_space_fabric) {
        // Convert grid square size from sim units to human-readable distance
        float grid_km = cfg.cosmos_space_fabric_grid_size * SIM_UNIT_TO_KM;
        constexpr float AU_KM = 149597870.7f;
        constexpr float LY_KM = 9.461e12f;
        char fabric_label[128];
        if (grid_km < 1.0f) {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.0f m per square", grid_km * 1000.0f);
        } else if (grid_km < 10000.0f) {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.0f km per square", grid_km);
        } else if (grid_km < EARTH_RADIUS_KM_REAL * 4.0f) {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.1f Earth radii per square",
                     grid_km / EARTH_RADIUS_KM_REAL);
        } else if (grid_km < AU_KM * 0.01f) {
            if (grid_km >= 1e6f)
                snprintf(fabric_label, sizeof(fabric_label), "Grid: %.2f million km per square", grid_km / 1e6f);
            else
                snprintf(fabric_label, sizeof(fabric_label), "Grid: %.0f km per square", grid_km);
        } else if (grid_km < AU_KM * 1000.0f) {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.2f AU per square", grid_km / AU_KM);
        } else if (grid_km < LY_KM) {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.0f AU per square", grid_km / AU_KM);
        } else {
            snprintf(fabric_label, sizeof(fabric_label), "Grid: %.2f ly per square", grid_km / LY_KM);
        }

        // Position: centered horizontally, just above the bottom bar + spawn menu
        // (like Universe Sandbox's grid label placement)
        float bar_h = 36.0f;
        float spawn_h = spawn_menu_visible_ ? 210.0f : 0.0f;
        ImVec2 label_size = ImGui::CalcTextSize(fabric_label);
        float px = W * 0.5f - label_size.x * 0.5f;
        float py = H - bar_h - spawn_h - label_size.y - 12.0f;
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
        // Only Escape/Space/Enter dismiss to default — card clicks handled below
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Space) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter)) {
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

        // Subtitle
        ImGui::SetWindowFontScale(0.9f);
        const char* subtitle = "Choose a scenario to begin";
        ImVec2 sub_size = ImGui::CalcTextSize(subtitle);
        float sub_alpha = std::min(splash_time_ * 2.0f, 1.0f) * 180.0f;
        dl->AddText(ImVec2(title_x, title_y + 36.0f),
                    IM_COL32(200, 200, 220, (int)sub_alpha), subtitle);
        ImGui::SetWindowFontScale(1.0f);

        // Preset gallery grid — right side of screen
        float grid_left = W * 0.32f;
        float grid_top = 55.0f;
        float grid_right = W - 30.0f;
        float grid_bottom = H - 50.0f;
        int cols = (W > 1600.0f) ? 4 : 3;
        int rows = (COSMOS_PRESET_COUNT + cols - 1) / cols;
        float gap = 5.0f;
        float card_w = (grid_right - grid_left - gap * (float)(cols - 1)) / (float)cols;
        float card_h = std::min((grid_bottom - grid_top - gap * (float)(rows - 1)) / (float)rows, 52.0f);

        // Icon colors per preset (grouped by theme)
        const ImU32 preset_colors[] = {
            IM_COL32(255, 200, 60, 255),  // Solar System - yellow
            IM_COL32(255, 160, 80, 255),  // Binary Stars - orange
            IM_COL32(220, 80, 60, 255),   // TRAPPIST-1 - red dwarf
            IM_COL32(255, 120, 40, 255),  // Hot Jupiter - hot orange
            IM_COL32(200, 100, 60, 255),  // Giant Impact - brown
            IM_COL32(140, 120, 200, 255), // Stellar Graveyard - purple
            IM_COL32(180, 160, 120, 255), // Protoplanetary - dust
            IM_COL32(160, 200, 255, 255), // Ringed Worlds - ice blue
            IM_COL32(200, 200, 255, 255), // Star Cluster - white blue
            IM_COL32(120, 200, 255, 255), // Comet Shower - cyan
            IM_COL32(80, 140, 200, 255),  // Rogue Planet - dark blue
            IM_COL32(180, 60, 200, 255),  // Supermassive BH - magenta
            IM_COL32(100, 200, 120, 255), // Habitable Zone - green
            IM_COL32(255, 220, 180, 255), // Stellar Evolution - warm white
            IM_COL32(255, 255, 160, 255), // Figure Eight - bright yellow
            IM_COL32(160, 140, 120, 255), // Asteroid Belt - grey
            IM_COL32(100, 140, 255, 255), // Wolf-Rayet - blue
            IM_COL32(255, 100, 100, 255), // Collision Course - red
            IM_COL32(160, 100, 200, 255), // Nebula Collapse - purple
            IM_COL32(80, 220, 255, 255),  // Pulsar Binary - bright cyan
            IM_COL32(180, 170, 140, 255), // Trojan Asteroids - tan
            IM_COL32(200, 140, 80, 255),  // Exomoon System - amber
            IM_COL32(255, 200, 120, 255), // Hierarchical Triple - gold
            IM_COL32(255, 170, 100, 255), // Tatooine - warm sunset
            IM_COL32(100, 60, 160, 255),  // Black Hole Accretion - dark purple
        };

        for (int i = 0; i < COSMOS_PRESET_COUNT; i++) {
            int col = i % cols;
            int row = i / cols;
            float cx = grid_left + col * (card_w + gap);
            float cy = grid_top + row * (card_h + gap);

            // Stagger animation
            float anim_delay = 0.4f + i * 0.04f;
            float anim_t = std::clamp((splash_time_ - anim_delay) * 3.0f, 0.0f, 1.0f);
            float ease = anim_t * anim_t * (3.0f - 2.0f * anim_t); // smoothstep
            float slide = (1.0f - ease) * 30.0f;
            cy += slide;
            int card_alpha = (int)(ease * 255.0f);
            if (card_alpha < 5) continue;

            ImVec2 p0(cx, cy), p1(cx + card_w, cy + card_h);
            ImVec2 mouse = io.MousePos;
            bool hovered = mouse.x >= p0.x && mouse.x <= p1.x &&
                           mouse.y >= p0.y && mouse.y <= p1.y;

            // Card background
            ImU32 bg_col = hovered
                ? IM_COL32(40, 50, 70, std::min(card_alpha, 220))
                : IM_COL32(18, 22, 35, std::min(card_alpha, 180));
            dl->AddRectFilled(p0, p1, bg_col, 6.0f);

            // Accent bar on left
            ImU32 accent = i < (int)(sizeof(preset_colors)/sizeof(preset_colors[0]))
                ? preset_colors[i] : IM_COL32(200, 200, 200, 255);
            int ar = (accent >> IM_COL32_R_SHIFT) & 0xFF;
            int ag = (accent >> IM_COL32_G_SHIFT) & 0xFF;
            int ab = (accent >> IM_COL32_B_SHIFT) & 0xFF;
            dl->AddRectFilled(ImVec2(cx, cy + 2), ImVec2(cx + 3, cy + card_h - 2),
                              IM_COL32(ar, ag, ab, std::min(card_alpha, 200)), 2.0f);

            // Border on hover
            if (hovered) {
                dl->AddRect(p0, p1, IM_COL32(ar, ag, ab, 160), 6.0f, 0, 1.5f);
            }

            // Name
            ImGui::SetWindowFontScale(0.85f);
            dl->AddText(ImVec2(cx + 10, cy + 4),
                        IM_COL32(240, 240, 255, card_alpha), COSMOS_PRESETS[i].name);
            ImGui::SetWindowFontScale(1.0f);

            // Description (truncated)
            ImGui::SetWindowFontScale(0.65f);
            dl->AddText(nullptr, 0.0f, ImVec2(cx + 10, cy + 22),
                        IM_COL32(160, 170, 190, std::min(card_alpha, 180)),
                        COSMOS_PRESETS[i].description,
                        COSMOS_PRESETS[i].description + std::min((int)strlen(COSMOS_PRESETS[i].description), 60));
            ImGui::SetWindowFontScale(1.0f);

            // Click to launch
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && splash_time_ > 0.5f) {
                load_preset(i);
                show_splash = false;
            }
        }

        // Hint at bottom
        const char* hint = "Click a scenario to begin  |  Press Escape to start with default";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = std::min(splash_time_ - 0.3f, 1.0f) * (120.0f + 80.0f * sinf(splash_time_ * 3.0f));
        if (hint_alpha > 0.0f) {
            dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 35.0f),
                        IM_COL32(200, 200, 210, (int)std::max(hint_alpha, 0.0f)), hint);
        }
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
        if (ImGui::Button("Presets...", ImVec2(btn_w, btn_h))) {
            ImGui::OpenPopup("##PresetPicker");
        }
        if (ImGui::BeginPopup("##PresetPicker")) {
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Scenarios");
            ImGui::Separator();
            for (int i = 0; i < COSMOS_PRESET_COUNT; i++) {
                if (ImGui::MenuItem(COSMOS_PRESETS[i].name)) {
                    load_preset(i);
                    show_pause_menu = false;
                }
                if (ImGui::IsItemHovered() && COSMOS_PRESETS[i].description[0]) {
                    ImGui::SetTooltip("%s", COSMOS_PRESETS[i].description);
                }
            }
            ImGui::EndPopup();
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
            cfg.sim_time_accumulated = 0.0;
            displayed_time_rate_ = 0.0;
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
