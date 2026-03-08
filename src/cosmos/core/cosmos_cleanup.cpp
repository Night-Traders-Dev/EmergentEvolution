#include "cosmos/cosmos_app_internal.h"
#include <algorithm>
#include <cmath>

void CosmosApp::account_escaped_mass(const CelestialBody& source, float amount,
                                     float thermal_energy) {
    if (amount <= 0.0f || !std::isfinite(amount))
        return;
    double m = (double)amount;
    glm::dvec3 v = glm::dvec3(source.vel);
    escaped_mass_total_ += m;
    escaped_momentum_total_ += v * m;
    escaped_energy_total_ += 0.5 * m * (double)glm::dot(v, v) +
                             std::max(0.0, (double)thermal_energy);
}

void CosmosApp::cleanup_bodies() {
    auto& bodies = state.bodies;

    auto finite_vec3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    for (auto& b : bodies) {
        bool invalid = !finite_vec3(b.pos) || !finite_vec3(b.vel) ||
            !std::isfinite(b.mass) || !std::isfinite(b.radius) ||
            !std::isfinite(b.temperature) || !std::isfinite(b.internal_energy) ||
            b.mass <= 0.0f || b.radius <= 0.0f;
        if (invalid)
            b.marked_for_removal = true;
        if (is_star_type(b.type) || is_black_hole_type(b.type) ||
            b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            b.non_attracting = false;
        }
    }

    if (cfg.dynamic_budget_enabled) {
        int max_fragments = std::max(cfg.dynamic_max_fragments, 0);
        int max_non_attracting = std::max(cfg.dynamic_max_non_attracting, 0);
        float reduction = std::clamp(cfg.dynamic_reduction_percent, 0.01f, 1.0f);
        float target_fps = std::max(cfg.dynamic_target_fps, 1.0f);

        std::vector<size_t> attracting_fragments;
        std::vector<size_t> non_attracting_fragments;
        attracting_fragments.reserve(bodies.size());
        non_attracting_fragments.reserve(bodies.size());

        for (size_t i = 0; i < bodies.size(); ++i) {
            const auto& b = bodies[i];
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (b.non_attracting) non_attracting_fragments.push_back(i);
            else attracting_fragments.push_back(i);
        }

        if ((int)attracting_fragments.size() > max_fragments) {
            int to_convert = (int)attracting_fragments.size() - max_fragments;
            std::sort(attracting_fragments.begin(), attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_convert && k < (int)attracting_fragments.size(); ++k) {
                size_t idx = attracting_fragments[(size_t)k];
                bodies[idx].non_attracting = true;
                non_attracting_fragments.push_back(idx);
            }
        }

        int desired_non_attracting = max_non_attracting;
        if (smoothed_fps_ < target_fps) {
            float deficit = std::clamp((target_fps - smoothed_fps_) / target_fps, 0.0f, 1.0f);
            int fps_cut = (int)std::ceil((float)non_attracting_fragments.size() *
                                         reduction * (0.50f + deficit * 1.50f));
            desired_non_attracting = std::max(0, desired_non_attracting - fps_cut);
        }

        if ((int)non_attracting_fragments.size() > desired_non_attracting) {
            int overflow = (int)non_attracting_fragments.size() - desired_non_attracting;
            int chunk = (int)std::ceil((float)non_attracting_fragments.size() * reduction);
            int to_remove = std::max(overflow, chunk);
            std::sort(non_attracting_fragments.begin(), non_attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_remove && k < (int)non_attracting_fragments.size(); ++k) {
                bodies[non_attracting_fragments[(size_t)k]].marked_for_removal = true;
            }
        }
    }

    bool any_removed = false;
    for (const auto& b : bodies) {
        if (b.marked_for_removal) { any_removed = true; break; }
    }
    if (!any_removed) return;

    // Build index mapping (old → new)
    std::vector<int> remap(bodies.size(), -1);
    int new_idx = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal)
            remap[i] = new_idx++;
    }

    // Fix selected body
    if (selected_body >= 0 && selected_body < (int)bodies.size()) {
        selected_body = remap[selected_body];
    }

    // Fix camera focus body
    if (camera.focus_body >= 0 && camera.focus_body < (int)bodies.size()) {
        int new_focus = remap[camera.focus_body];
        if (new_focus < 0) {
            camera.release_focus();
        } else {
            camera.focus_body = new_focus;
        }
    } else if (camera.focus_body >= (int)bodies.size()) {
        camera.release_focus();
    }

    // Fix parent indices
    for (auto& b : bodies) {
        if (b.parent >= 0 && b.parent < (int)remap.size())
            b.parent = remap[b.parent];
    }

    // Remove bodies and trails
    size_t write = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal) {
            if (write != i) {
                bodies[write] = bodies[i];
                if (i < state.trails.size())
                    state.trails[write] = std::move(state.trails[i]);
            }
            write++;
        }
    }
    bodies.resize(write);
    state.trails.resize(write);
}
