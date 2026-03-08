#include "cosmos/cosmos_app_internal.h"
#include "cosmos/core/cosmos_parallel.h"
#include <cmath>
#include <thread>

int CosmosApp::dominant_primary_for(int body_index) const {
    if (body_index < 0 || body_index >= (int)state.bodies.size())
        return -1;
    const auto& body = state.bodies[(size_t)body_index];
    if (body.marked_for_removal) return -1;

    if (body.parent >= 0 && body.parent < (int)state.bodies.size() && body.parent != body_index) {
        const auto& parent = state.bodies[(size_t)body.parent];
        if (!parent.marked_for_removal && parent.mass > body.mass * 0.25f)
            return body.parent;
    }

    int best = -1;
    double best_score = 0.0;
    for (int j = 0; j < (int)state.bodies.size(); ++j) {
        if (j == body_index) continue;
        const auto& cand = state.bodies[(size_t)j];
        if (cand.marked_for_removal) continue;
        if (cand.non_attracting) continue;
        if (cand.mass <= body.mass * 1.01f && !is_star_type(cand.type) && !is_black_hole_type(cand.type))
            continue;
        glm::dvec3 d = glm::dvec3(cand.pos) - glm::dvec3(body.pos);
        double d2 = glm::dot(d, d);
        if (d2 <= 1.0e-8) continue;
        double score = (double)cand.mass / d2;
        if (score > best_score) {
            best_score = score;
            best = j;
        }
    }
    return best;
}

void CosmosApp::update_body_tracking_cache() {
    const size_t n = state.bodies.size();
    tracked_primary_.assign(n, -1);
    tracked_children_count_.assign(n, 0);
    tracked_eccentricity_.assign(n, -1.0f);
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
        ? static_cast<size_t>(std::thread::hardware_concurrency())
        : 1;
    const bool can_parallel = cfg.parallel_gravity && hw_threads > 1 && n >= 256;

    if (can_parallel) {
        run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (state.bodies[i].marked_for_removal) continue;
                tracked_primary_[i] = dominant_primary_for((int)i);
            }
        });
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (state.bodies[i].marked_for_removal) continue;
            tracked_primary_[i] = dominant_primary_for((int)i);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        int primary = tracked_primary_[i];
        if (primary >= 0 && primary < (int)n)
            tracked_children_count_[(size_t)primary]++;
    }

    auto calc_ecc_range = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            int pidx = tracked_primary_[i];
            if (pidx < 0 || pidx >= (int)n) continue;
            const auto& b = state.bodies[i];
            const auto& p = state.bodies[(size_t)pidx];
            if (b.marked_for_removal || p.marked_for_removal) continue;

            glm::dvec3 r = glm::dvec3(b.pos) - glm::dvec3(p.pos);
            glm::dvec3 v = glm::dvec3(b.vel) - glm::dvec3(p.vel);
            double rmag = glm::length(r);
            if (rmag <= 1.0e-8) continue;
            double mu = (double)cfg.G * std::max((double)b.mass + (double)p.mass, 1.0e-8);
            if (mu <= 0.0) continue;
            glm::dvec3 h = glm::cross(r, v);
            glm::dvec3 evec = glm::cross(v, h) / mu - r / rmag;
            tracked_eccentricity_[i] = (float)glm::length(evec);
        }
    };
    if (can_parallel)
        run_parallel_chunks(n, std::min(hw_threads, n), calc_ecc_range);
    else
        calc_ecc_range(0, n);
}
