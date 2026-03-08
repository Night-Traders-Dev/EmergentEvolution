#include "cosmos/cosmos_app_internal.h"
#include "cosmos/core/cosmos_parallel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <thread>
#include <unordered_map>

void CosmosApp::step_physics(float dt) {
    ++diagnostics_step_counter_;
    if (diagnostics_enabled_ && !validate_body_state("step_physics/pre", true))
        return;
    if (diagnostics_enabled_ && (diagnostics_step_counter_ % 600ull) == 0ull) {
        debug_logf("heartbeat bodies=%zu fps=%.2f sim_time=%.6g",
                   state.bodies.size(), smoothed_fps_, sim_time_);
    }

    float frame_dt = std::max(dt, 1.0e-4f);
    float instant_fps = 1.0f / frame_dt;
    if (!std::isfinite(smoothed_fps_))
        smoothed_fps_ = instant_fps;
    smoothed_fps_ = smoothed_fps_ * 0.92f + instant_fps * 0.08f;

    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);
    float time_sign = reverse_time_ ? -1.0f : 1.0f;
    float scaled_dt_nominal = dt * cfg.dt_scale * time_sign;
    if (std::abs(dt) > 1.0e-9f)
        displayed_time_rate_ = (double)scaled_dt_nominal / (double)dt;
    else
        displayed_time_rate_ = (double)cfg.dt_scale * (double)time_sign;
    if (!std::isfinite(displayed_time_rate_))
        displayed_time_rate_ = 0.0;
    cfg.integrator_type = std::clamp(cfg.integrator_type,
                                     (int)INTEGRATOR_VELOCITY_VERLET,
                                     (int)INTEGRATOR_PEFRL);
    cfg.velocity_verlet = (cfg.integrator_type == INTEGRATOR_VELOCITY_VERLET);
    if (!adaptive_substep_refining_) {
        adaptive_substeps_last_ = 1;
        adaptive_substeps_required_ = 1;
        adaptive_substeps_saturated_ = false;
    }
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
        ? static_cast<size_t>(std::thread::hardware_concurrency())
        : 1;
    const bool can_parallel = cfg.parallel_gravity && hw_threads > 1;
    const size_t parallel_min_batch = static_cast<size_t>(std::clamp(cfg.parallel_min_batch, 32, 100000));
    auto parallel_for = [&](size_t count, size_t min_parallel, auto&& fn) {
        if (!can_parallel || count < std::max(min_parallel, parallel_min_batch)) {
            fn(0, count);
            return;
        }
        run_parallel_chunks(count, std::min(hw_threads, count), fn);
    };
    if (n == 0) return;
    apply_dust_debug_mode();

    // Use persistent scratch buffers to avoid per-frame heap allocations.
    // Only resize when body count changes (amortized O(1)).
    auto resize_scratch = [&](size_t sz) {
        if (scratch_pos0_.size() < sz) {
            scratch_pos0_.resize(sz);    scratch_vel0_.resize(sz);
            scratch_source_active_.resize(sz);
            scratch_source_mass_.resize(sz);  scratch_source_spin_y_.resize(sz);
            scratch_source_radius_.resize(sz); scratch_source_angular_vel_.resize(sz);
            scratch_source_spin_axis_.resize(sz);
            scratch_accel0_.resize(sz);
            scratch_int_pos_.resize(sz);  scratch_int_vel_.resize(sz);
            scratch_int_accel_.resize(sz); scratch_int_accel2_.resize(sz);
            scratch_int_pos2_.resize(sz); scratch_int_vel2_.resize(sz);
        }
    };
    resize_scratch(n);
    auto& pos0 = scratch_pos0_;
    auto& vel0 = scratch_vel0_;
    auto& source_active = scratch_source_active_;
    auto& source_mass = scratch_source_mass_;
    auto& source_spin_y = scratch_source_spin_y_;
    auto& source_radius = scratch_source_radius_;
    auto& source_angular_vel = scratch_source_angular_vel_;
    auto& source_spin_axis = scratch_source_spin_axis_;

    // Clear and fill (parallel memset + fill is faster than allocate+construct)
    std::memset(source_active.data(), 0, n * sizeof(uint8_t));
    parallel_for(n, 512, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const auto& b = bodies[i];
            pos0[i] = b.pos;
            vel0[i] = b.vel;
            source_mass[i] = 0.0f;
            source_spin_y[i] = 0.0f;
            source_radius[i] = 0.0f;
            source_angular_vel[i] = 0.0f;
            source_spin_axis[i] = glm::vec3(0.0f, 1.0f, 0.0f);
            if (!b.marked_for_removal && !b.non_attracting && b.mass > 0.0f) {
                source_active[i] = 1u;
                source_mass[i] = b.mass;
                source_radius[i] = b.radius;
                source_angular_vel[i] = b.angular_vel;
                float r = std::max(b.radius, 1.0e-4f);
                source_spin_y[i] = b.mass * r * r * b.angular_vel;
                float tilt = b.axial_tilt;
                source_spin_axis[i] = glm::vec3(std::sin(tilt), std::cos(tilt), 0.0f);
            }
        }
    });
    scratch_source_indices_.clear();
    scratch_source_indices_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (source_active[i] && source_mass[i] > 0.0f)
            scratch_source_indices_.push_back(i);
    }
    auto& source_indices = scratch_source_indices_;

    float c2 = cfg.speed_of_light * cfg.speed_of_light;
    float soft2 = cfg.softening * cfg.softening;
    constexpr float kMinDist = 1.0e-6f;

    auto apply_source_accel = [&](size_t target,
                                  float src_mass,
                                  float src_spin_y,
                                  float src_radius,
                                  float src_angular_vel,
                                  const glm::vec3& src_spin_axis,
                                  const glm::vec3& src_pos,
                                  const glm::vec3& tgt_pos,
                                  const glm::vec3& tgt_vel,
                                  glm::vec3& out_accel) {
        if (src_mass <= 0.0f) return;
        glm::vec3 diff = src_pos - tgt_pos;
        float dist2 = glm::dot(diff, diff) + soft2;
        float dist = std::sqrt(dist2);
        if (dist <= kMinDist) return;
        glm::vec3 r_hat = diff / dist;
        float GM = cfg.G * src_mass;
        glm::vec3 acc = r_hat * (GM / dist2);

        // J2 oblateness perturbation: oblate bodies have stronger equatorial gravity
        if (cfg.j2_perturbation && src_radius > 0.0f && dist > src_radius * 1.01f) {
            // J2 ≈ (ω²R³)/(3GM) for fluid body; estimate from spin rate
            float omega2 = src_angular_vel * src_angular_vel;
            float R3 = src_radius * src_radius * src_radius;
            float J2 = std::clamp(omega2 * R3 / std::max(3.0f * GM, 1.0e-8f), 0.0f, 0.1f);
            if (J2 > 1.0e-8f) {
                float R2_r2 = (src_radius * src_radius) / (dist * dist);
                // sin(lat) = projection of r_hat onto the body's spin axis
                float sin_lat = glm::dot(r_hat, src_spin_axis);
                float sin2_lat = sin_lat * sin_lat;
                float j2_radial = -1.5f * J2 * R2_r2 * (3.0f * sin2_lat - 1.0f);
                float cos_lat = std::sqrt(std::max(1.0f - sin2_lat, 0.0f));
                float j2_lat = -3.0f * J2 * R2_r2 * sin_lat * cos_lat;
                acc += (GM / dist2) * (j2_radial * r_hat + j2_lat * src_spin_axis);
            }
        }

        if (cfg.gr_enabled && c2 > 0.0f) {
            // 1PN post-Newtonian correction (EIH equations, Schwarzschild limit)
            if (cfg.gr_precession_scale > 0.0f) {
                float v2 = glm::dot(tgt_vel, tgt_vel);
                float vr = glm::dot(tgt_vel, r_hat);
                float pn_scale = GM / (dist * c2) * cfg.gr_precession_scale;
                // Correct 1PN: {4GM/r - v² + 3(v·r̂)²} r̂ + 4(v·r̂) v
                acc += pn_scale * ((4.0f * GM / dist - v2 + 3.0f * vr * vr) * r_hat
                                    + 4.0f * vr * tgt_vel);
            }
            // Gravitational time dilation: gravity strengthens in deep potential wells
            if (cfg.gr_time_dilation > 0.0f) {
                float phi_over_c2 = GM / (dist * c2);
                float td = 1.0f + cfg.gr_time_dilation * phi_over_c2;
                acc *= td;
            }
            // Lense-Thirring frame-dragging (factor of 2 from GR)
            if (cfg.gr_frame_dragging > 0.0f && std::abs(src_spin_y) > 1.0e-9f) {
                float coeff = 2.0f * cfg.gr_frame_dragging * cfg.G * src_spin_y /
                              (c2 * dist * dist * dist);
                glm::vec3 vxJ = glm::cross(tgt_vel, src_spin_axis);
                float rdotJ = glm::dot(r_hat, src_spin_axis);
                glm::vec3 vxr = glm::cross(tgt_vel, r_hat);
                acc += coeff * (vxJ - 3.0f * rdotJ * vxr);
            }
        }

        // Clamp acceleration to prevent overflow from extreme close encounters
        float acc_mag2 = glm::dot(acc, acc);
        constexpr float kMaxAccel = 1.0e8f;
        constexpr float kMaxAccel2 = kMaxAccel * kMaxAccel;
        if (!std::isfinite(acc_mag2) || acc_mag2 > kMaxAccel2) {
            if (std::isfinite(acc_mag2) && acc_mag2 > 0.0f) {
                acc *= kMaxAccel / std::sqrt(acc_mag2);
            } else {
                return; // NaN/Inf — discard this contribution
            }
        }
        out_accel += acc;
    };

    auto compute_accel_direct = [&](const std::vector<glm::vec3>& pos,
                                    const std::vector<glm::vec3>& vel,
                                    std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        if (source_indices.empty())
            return;
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (bodies[i].marked_for_removal)
                    continue;
                glm::vec3 ai(0.0f);
                for (size_t src : source_indices) {
                    if (src == i) continue;
                    apply_source_accel(i, source_mass[src], source_spin_y[src],
                                       source_radius[src], source_angular_vel[src],
                                       source_spin_axis[src],
                                       pos[src], pos[i], vel[i], ai);
                }
                out_accel[i] = ai;
            }
        });
    };

    struct BHNode {
        glm::vec3 center{0.0f};
        float half = 0.0f;
        std::array<int, 8> child{{-1, -1, -1, -1, -1, -1, -1, -1}};
        std::array<size_t, 4> leaf{};
        int leaf_count = 0;
        bool is_leaf = true;
        float mass = 0.0f;
        glm::vec3 com{0.0f};
        float spin_y = 0.0f;
    };

    auto build_barnes_hut_tree = [&](const std::vector<glm::vec3>& pos,
                                     std::vector<BHNode>& nodes,
                                     int& root_out) -> bool {
        if (source_indices.empty()) {
            root_out = -1;
            return false;
        }

        glm::vec3 bmin(std::numeric_limits<float>::max());
        glm::vec3 bmax(std::numeric_limits<float>::lowest());
        for (size_t idx : source_indices) {
            bmin = glm::min(bmin, pos[idx]);
            bmax = glm::max(bmax, pos[idx]);
        }
        glm::vec3 span = bmax - bmin;
        float half = std::max(std::max(span.x, span.y), span.z) * 0.5f;
        half = std::max(half + std::max(cfg.softening * 2.0f, 1.0f), 1.0e-3f);
        glm::vec3 root_center = (bmin + bmax) * 0.5f;

        nodes.clear();
        nodes.reserve(source_indices.size() * 2 + 8);
        auto make_node = [&](const glm::vec3& center, float node_half) -> int {
            nodes.push_back(BHNode{});
            BHNode& node = nodes.back();
            node.center = center;
            node.half = node_half;
            return (int)nodes.size() - 1;
        };
        auto child_index_for = [](const glm::vec3& p, const glm::vec3& c) {
            int idx = 0;
            if (p.x >= c.x) idx |= 1;
            if (p.y >= c.y) idx |= 2;
            if (p.z >= c.z) idx |= 4;
            return idx;
        };
        auto child_center_for = [](const glm::vec3& c, float parent_half, int oct) {
            float h = parent_half * 0.5f;
            return glm::vec3(
                c.x + ((oct & 1) ? h : -h),
                c.y + ((oct & 2) ? h : -h),
                c.z + ((oct & 4) ? h : -h));
        };
        auto ensure_child = [&](int parent_idx, int oct) -> int {
            int child_idx = nodes[(size_t)parent_idx].child[(size_t)oct];
            if (child_idx >= 0)
                return child_idx;
            glm::vec3 cc = child_center_for(nodes[(size_t)parent_idx].center,
                                            nodes[(size_t)parent_idx].half, oct);
            child_idx = make_node(cc, nodes[(size_t)parent_idx].half * 0.5f);
            nodes[(size_t)parent_idx].child[(size_t)oct] = child_idx;
            return child_idx;
        };

        constexpr int kLeafCap = 4;
        constexpr int kMaxDepth = 20;
        constexpr float kMinHalf = 1.0e-4f;
        root_out = make_node(root_center, half);
        auto insert_body = [&](auto&& self, int node_idx, size_t body_idx, int depth) -> void {
            if (node_idx < 0) return;
            BHNode& node = nodes[(size_t)node_idx];
            if (node.is_leaf) {
                if (node.leaf_count < kLeafCap || depth >= kMaxDepth || node.half <= kMinHalf) {
                    node.leaf[(size_t)node.leaf_count++] = body_idx;
                    return;
                }
                std::array<size_t, kLeafCap> reinserts{};
                int rein_count = node.leaf_count;
                for (int i = 0; i < rein_count; ++i)
                    reinserts[(size_t)i] = node.leaf[(size_t)i];
                node.leaf_count = 0;
                node.is_leaf = false;
                for (int i = 0; i < rein_count; ++i) {
                    int oct = child_index_for(pos[reinserts[(size_t)i]], nodes[(size_t)node_idx].center);
                    int child_idx = ensure_child(node_idx, oct);
                    self(self, child_idx, reinserts[(size_t)i], depth + 1);
                }
            }
            int oct = child_index_for(pos[body_idx], nodes[(size_t)node_idx].center);
            int child_idx = ensure_child(node_idx, oct);
            self(self, child_idx, body_idx, depth + 1);
        };
        for (size_t idx : source_indices)
            insert_body(insert_body, root_out, idx, 0);

        auto finalize_mass = [&](auto&& self, int node_idx) -> void {
            BHNode& node = nodes[(size_t)node_idx];
            node.mass = 0.0f;
            node.com = glm::vec3(0.0f);
            node.spin_y = 0.0f;
            if (node.is_leaf) {
                for (int i = 0; i < node.leaf_count; ++i) {
                    size_t bidx = node.leaf[(size_t)i];
                    float m = std::max(source_mass[bidx], 0.0f);
                    if (m <= 0.0f) continue;
                    node.mass += m;
                    node.com += pos[bidx] * m;
                    node.spin_y += source_spin_y[bidx];
                }
            } else {
                for (int c = 0; c < 8; ++c) {
                    int child_idx = node.child[(size_t)c];
                    if (child_idx < 0) continue;
                    self(self, child_idx);
                    const BHNode& child = nodes[(size_t)child_idx];
                    if (child.mass <= 0.0f) continue;
                    node.mass += child.mass;
                    node.com += child.com * child.mass;
                    node.spin_y += child.spin_y;
                }
            }
            if (node.mass > 0.0f)
                node.com /= node.mass;
            else
                node.com = node.center;
        };
        finalize_mass(finalize_mass, root_out);
        return true;
    };

    auto compute_accel_barnes_hut = [&](const std::vector<glm::vec3>& pos,
                                        const std::vector<glm::vec3>& vel,
                                        std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        std::vector<BHNode> nodes;
        int root = -1;
        if (!build_barnes_hut_tree(pos, nodes, root))
            return;

        float theta = std::clamp(cfg.barnes_hut_theta, 0.2f, 1.6f);
        auto traverse = [&](auto&& self, size_t target, int node_idx) -> void {
            const BHNode& node = nodes[(size_t)node_idx];
            if (node.mass <= 0.0f) return;
            if (node.is_leaf) {
                for (int i = 0; i < node.leaf_count; ++i) {
                    size_t src = node.leaf[(size_t)i];
                    if (src == target) continue;
                    apply_source_accel(target, source_mass[src], source_spin_y[src],
                                       source_radius[src], source_angular_vel[src],
                                       source_spin_axis[src],
                                       pos[src], pos[target], vel[target], out_accel[target]);
                }
                return;
            }

            bool target_inside =
                std::abs(pos[target].x - node.center.x) <= node.half &&
                std::abs(pos[target].y - node.center.y) <= node.half &&
                std::abs(pos[target].z - node.center.z) <= node.half;
            glm::vec3 delta = node.com - pos[target];
            float dist2 = glm::dot(delta, delta) + soft2;
            float dist = std::sqrt(dist2);
            float size = node.half * 2.0f;
            if (!target_inside && dist > kMinDist && (size / dist) < theta) {
                apply_source_accel(target, node.mass, node.spin_y,
                                   0.0f, 0.0f, // no J2 for aggregate BH nodes
                                   glm::vec3(0.0f, 1.0f, 0.0f), // approximate axis for aggregate
                                   node.com, pos[target], vel[target], out_accel[target]);
                return;
            }
            for (int c = 0; c < 8; ++c) {
                int child_idx = node.child[(size_t)c];
                if (child_idx >= 0)
                    self(self, target, child_idx);
            }
        };

        constexpr size_t kParallelBhTraverseThreshold = 256;
        if (can_parallel && n >= kParallelBhTraverseThreshold) {
            run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    traverse(traverse, i, root);
                }
            });
        } else {
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                traverse(traverse, i, root);
            }
        }
    };

    auto compute_accel_barnes_hut_gpu = [&](const std::vector<glm::vec3>& pos,
                                            const std::vector<glm::vec3>& vel,
                                            std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        std::vector<BHNode> nodes;
        int root = -1;
        if (!build_barnes_hut_tree(pos, nodes, root))
            return;
        if (root != 0 || nodes.empty()) {
            compute_accel_barnes_hut(pos, vel, out_accel);
            return;
        }

        std::vector<CosmosBhGpuNode> gpu_nodes(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            const BHNode& node = nodes[i];
            CosmosBhGpuNode gn{};
            gn.center_half = glm::vec4(node.center, node.half);
            gn.com_mass = glm::vec4(node.com, node.mass);
            gn.spin_leaf = glm::vec4(node.spin_y, (float)node.leaf_count, node.is_leaf ? 1.0f : 0.0f, 0.0f);
            gn.child0 = glm::ivec4(node.child[0], node.child[1], node.child[2], node.child[3]);
            gn.child1 = glm::ivec4(node.child[4], node.child[5], node.child[6], node.child[7]);
            gn.leaf = glm::ivec4(-1);
            for (int li = 0; li < node.leaf_count && li < 4; ++li)
                gn.leaf[li] = (int)node.leaf[(size_t)li];
            gpu_nodes[i] = gn;
        }

        std::vector<glm::vec4> gpu_sources(n, glm::vec4(0.0f));
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                gpu_sources[i] = glm::vec4(
                    source_mass[i],
                    source_spin_y[i],
                    source_active[i] ? 1.0f : 0.0f,
                    bodies[i].marked_for_removal ? 1.0f : 0.0f);
            }
        });

        float theta = std::clamp(cfg.barnes_hut_theta, 0.2f, 1.6f);
        if (!gravity_compute_.compute_barnes_hut(vk, pos, vel, gpu_sources, gpu_nodes, theta, cfg, out_accel))
            compute_accel_barnes_hut(pos, vel, out_accel);
    };

    auto compute_accel = [&](const std::vector<glm::vec3>& pos,
                             const std::vector<glm::vec3>& vel,
                             std::vector<glm::vec3>& out_accel) {
        bool use_bh = cfg.barnes_hut && (int)n >= std::max(cfg.barnes_hut_min_bodies, 16);
        if (use_bh && cfg.gpu_barnes_hut)
            compute_accel_barnes_hut_gpu(pos, vel, out_accel);
        else if (use_bh)
            compute_accel_barnes_hut(pos, vel, out_accel);
        else
            compute_accel_direct(pos, vel, out_accel);
    };

    auto integrate_kinematics = [&](float step_scaled_dt,
                                    const std::vector<glm::vec3>& in_pos,
                                    const std::vector<glm::vec3>& in_vel,
                                    const std::vector<glm::vec3>* initial_accel,
                                    std::vector<glm::vec3>& out_pos,
                                    std::vector<glm::vec3>& out_vel) {
        // Reuse scratch buffers instead of allocating new vectors each call
        out_pos.resize(n);
        out_vel.resize(n);
        if (n == 0) return;

        auto& accel_a = scratch_int_accel_;
        auto& accel_b = scratch_int_accel2_;
        auto& tmp_pos = scratch_int_pos2_;
        auto& tmp_vel = scratch_int_vel2_;

        auto ensure_accel = [&](std::vector<glm::vec3>& accel) {
            if (initial_accel) {
                std::memcpy(accel.data(), initial_accel->data(), n * sizeof(glm::vec3));
            } else {
                compute_accel(in_pos, in_vel, accel);
            }
        };

        switch (cfg.integrator_type) {
        case INTEGRATOR_EULER_EXPLICIT: {
            std::fill_n(accel_a.data(), n, glm::vec3(0.0f));
            ensure_accel(accel_a);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        out_pos[i] = in_pos[i]; out_vel[i] = in_vel[i]; continue;
                    }
                    out_pos[i] = in_pos[i] + in_vel[i] * step_scaled_dt;
                    out_vel[i] = in_vel[i] + accel_a[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_EULER_SEMI_IMPLICIT: {
            std::fill_n(accel_a.data(), n, glm::vec3(0.0f));
            ensure_accel(accel_a);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        out_pos[i] = in_pos[i]; out_vel[i] = in_vel[i]; continue;
                    }
                    out_vel[i] = in_vel[i] + accel_a[i] * step_scaled_dt;
                    out_pos[i] = in_pos[i] + out_vel[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_RK2: {
            std::fill_n(accel_a.data(), n, glm::vec3(0.0f));
            ensure_accel(accel_a);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        tmp_pos[i] = in_pos[i]; tmp_vel[i] = in_vel[i]; continue;
                    }
                    tmp_pos[i] = in_pos[i] + in_vel[i] * (step_scaled_dt * 0.5f);
                    tmp_vel[i] = in_vel[i] + accel_a[i] * (step_scaled_dt * 0.5f);
                }
            });
            std::fill_n(accel_b.data(), n, glm::vec3(0.0f));
            compute_accel(tmp_pos, tmp_vel, accel_b);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        out_pos[i] = in_pos[i]; out_vel[i] = in_vel[i]; continue;
                    }
                    out_pos[i] = in_pos[i] + tmp_vel[i] * step_scaled_dt;
                    out_vel[i] = in_vel[i] + accel_b[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_FOREST_RUTH: {
            std::memcpy(tmp_pos.data(), in_pos.data(), n * sizeof(glm::vec3));
            std::memcpy(tmp_vel.data(), in_vel.data(), n * sizeof(glm::vec3));
            constexpr float theta = 1.0f / (2.0f - 1.2599210498948732f);
            constexpr float c1 = theta * 0.5f;
            constexpr float c2 = (1.0f - theta) * 0.5f;
            constexpr float d1 = theta;
            constexpr float d2 = 1.0f - 2.0f * theta;
            constexpr float d3 = theta;
            auto drift = [&](float coeff) {
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                        tmp_pos[i] += tmp_vel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            auto kick = [&](float coeff) {
                compute_accel(tmp_pos, tmp_vel, accel_a);
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                        tmp_vel[i] += accel_a[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            drift(c1); kick(d1);
            drift(c2); kick(d2);
            drift(c2); kick(d3);
            drift(c1);
            std::memcpy(out_pos.data(), tmp_pos.data(), n * sizeof(glm::vec3));
            std::memcpy(out_vel.data(), tmp_vel.data(), n * sizeof(glm::vec3));
            break;
        }
        case INTEGRATOR_PEFRL: {
            std::memcpy(tmp_pos.data(), in_pos.data(), n * sizeof(glm::vec3));
            std::memcpy(tmp_vel.data(), in_vel.data(), n * sizeof(glm::vec3));
            constexpr float xi = 0.1786178958448091f;
            constexpr float lambda = -0.2123418310626054f;
            constexpr float chi = -0.06626458266981849f;
            constexpr float c_half = (1.0f - 2.0f * lambda) * 0.5f;
            constexpr float drift_mid = 1.0f - 2.0f * (chi + xi);
            auto drift = [&](float coeff) {
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                        tmp_pos[i] += tmp_vel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            auto kick = [&](float coeff) {
                compute_accel(tmp_pos, tmp_vel, accel_a);
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                        tmp_vel[i] += accel_a[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            drift(xi);        kick(c_half);
            drift(chi);       kick(lambda);
            drift(drift_mid); kick(lambda);
            drift(chi);       kick(c_half);
            drift(xi);
            std::memcpy(out_pos.data(), tmp_pos.data(), n * sizeof(glm::vec3));
            std::memcpy(out_vel.data(), tmp_vel.data(), n * sizeof(glm::vec3));
            break;
        }
        case INTEGRATOR_VELOCITY_VERLET:
        default: {
            std::fill_n(accel_a.data(), n, glm::vec3(0.0f));
            ensure_accel(accel_a);
            float dt2 = step_scaled_dt * step_scaled_dt;
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        tmp_pos[i] = in_pos[i]; tmp_vel[i] = in_vel[i]; continue;
                    }
                    tmp_pos[i] = in_pos[i] + in_vel[i] * step_scaled_dt + 0.5f * accel_a[i] * dt2;
                    tmp_vel[i] = in_vel[i] + 0.5f * accel_a[i] * step_scaled_dt;
                }
            });
            std::fill_n(accel_b.data(), n, glm::vec3(0.0f));
            compute_accel(tmp_pos, tmp_vel, accel_b);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal || bodies[i].locked) {
                        out_pos[i] = in_pos[i]; out_vel[i] = in_vel[i]; continue;
                    }
                    out_pos[i] = tmp_pos[i];
                    out_vel[i] = in_vel[i] + 0.5f * (accel_a[i] + accel_b[i]) * step_scaled_dt;
                }
            });
            break;
        }
        }

        parallel_for(n, 384, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                out_vel[i] *= cfg.damping;
            }
        });

        // Sanitize: clamp extreme velocities and fix NaN/Inf
        constexpr float kMaxVel = 1.0e6f;
        constexpr float kMaxVel2 = kMaxVel * kMaxVel;
        parallel_for(n, 384, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (bodies[i].marked_for_removal || bodies[i].locked) continue;
                auto finite_v3 = [](const glm::vec3& v) {
                    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
                };
                if (!finite_v3(out_vel[i])) {
                    out_vel[i] = in_vel[i]; // revert to previous velocity
                }
                if (!finite_v3(out_pos[i])) {
                    out_pos[i] = in_pos[i]; // revert to previous position
                    out_vel[i] = glm::vec3(0.0f);
                }
                float v2 = glm::dot(out_vel[i], out_vel[i]);
                if (v2 > kMaxVel2) {
                    out_vel[i] *= kMaxVel / std::sqrt(v2);
                }
            }
        });
    };

    auto& accel0 = scratch_accel0_;
    std::fill_n(accel0.data(), n, glm::vec3(0.0f));
    compute_accel(pos0, vel0, accel0);

    float scaled_dt = scaled_dt_nominal;
    if (cfg.adaptive_time_step) {
        float max_speed = 0.0f;
        float max_acc = 0.0f;
        if (can_parallel && n >= 512) {
            std::mutex reduce_mutex;
            run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
                float local_max_speed = 0.0f;
                float local_max_acc = 0.0f;
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    local_max_speed = std::max(local_max_speed, glm::length(vel0[i]));
                    local_max_acc = std::max(local_max_acc, glm::length(accel0[i]));
                }
                std::lock_guard<std::mutex> lock(reduce_mutex);
                max_speed = std::max(max_speed, local_max_speed);
                max_acc = std::max(max_acc, local_max_acc);
            });
        } else {
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                max_speed = std::max(max_speed, glm::length(vel0[i]));
                max_acc = std::max(max_acc, glm::length(accel0[i]));
            }
        }

        float abs_nominal = std::abs(scaled_dt_nominal);
        float safety = std::clamp(cfg.adaptive_step_safety, 0.01f, 1.0f);
        float char_len = std::max(cfg.softening, 0.25f);
        float dt_vel = (max_speed > 1.0e-6f) ? (safety * char_len / max_speed) : abs_nominal;
        float dt_acc = (max_acc > 1.0e-9f) ? (safety * std::sqrt(char_len / max_acc)) : abs_nominal;
        float dt_min = std::max(cfg.adaptive_step_min, 1.0e-6f);
        float dt_max = std::max(cfg.adaptive_step_max, dt_min);
        float dt_limit = std::clamp(std::min(dt_vel, dt_acc), dt_min, dt_max);
        scaled_dt = time_sign * std::min(abs_nominal, dt_limit);
    }
    if (!std::isfinite(scaled_dt))
        scaled_dt = 0.0f;

    // Orbital-period-aware substepping: ensure enough steps per shortest orbit
    // to maintain stable integration at high time scales (e.g. TRAPPIST-1 at 1yr/s).
    if (!adaptive_substep_refining_ && n > 1 && std::abs(scaled_dt) > 1.0e-9f) {
        constexpr int kMinStepsPerOrbit = 32;
        constexpr int kOrbitSubstepCap = 128;
        constexpr float kTwoPi = 6.283185307f;

        // Cache shortest period — only recompute every 30 frames
        int current_frame = (int)(sim_time_ * 60.0f);
        if (current_frame != cached_shortest_period_frame_) {
            cached_shortest_period_frame_ = current_frame;
            float shortest = std::numeric_limits<float>::max();
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal || bodies[i].non_attracting) continue;
                if (bodies[i].orbital_period > 1.0e-3f) {
                    shortest = std::min(shortest, bodies[i].orbital_period);
                    continue;
                }
                int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
                if (primary < 0 || primary >= (int)n) continue;
                const auto& host = bodies[(size_t)primary];
                if (host.marked_for_removal) continue;
                float r = glm::length(bodies[i].pos - host.pos);
                float mu = cfg.G * (host.mass + bodies[i].mass);
                if (r > 1.0e-3f && mu > 1.0e-12f) {
                    float T = kTwoPi * std::sqrt(r * r * r / mu);
                    if (std::isfinite(T) && T > 1.0e-3f)
                        shortest = std::min(shortest, T);
                }
            }
            cached_shortest_period_ = shortest;
        }
        float shortest_period = cached_shortest_period_;
        if (shortest_period < std::numeric_limits<float>::max() * 0.5f) {
            float abs_dt = std::abs(scaled_dt);
            int orbit_substeps = (int)std::ceil(abs_dt * (float)kMinStepsPerOrbit / shortest_period);
            if (orbit_substeps > 1) {
                // When substep cap would be exceeded, clamp dt so orbits
                // always get at least kMinStepsPerOrbit steps per revolution.
                // This prevents polygonal "snake-like" trajectories at high
                // time scales by sacrificing simulation speed for accuracy.
                if (orbit_substeps > kOrbitSubstepCap) {
                    float max_scaled_dt = shortest_period * (float)kOrbitSubstepCap /
                                          (float)kMinStepsPerOrbit;
                    scaled_dt = time_sign * max_scaled_dt;
                    abs_dt = max_scaled_dt;
                    orbit_substeps = kOrbitSubstepCap;
                }
                float sub_scaled_dt = scaled_dt / (float)orbit_substeps;
                float denom = cfg.dt_scale * time_sign;
                float sub_real_dt = (std::abs(denom) > 1.0e-9f)
                    ? (sub_scaled_dt / denom)
                    : (dt / (float)orbit_substeps);
                float applied_total = sub_scaled_dt * (float)orbit_substeps;
                adaptive_substeps_required_ = orbit_substeps;
                adaptive_substeps_last_ = orbit_substeps;
                adaptive_substeps_saturated_ = (orbit_substeps >= kOrbitSubstepCap);
                adaptive_substep_refining_ = true;
                for (int substep = 0; substep < orbit_substeps; ++substep)
                    step_physics(sub_real_dt);
                adaptive_substep_refining_ = false;
                displayed_time_rate_ = (std::abs(dt) > 1.0e-9f)
                    ? (double)applied_total / (double)dt : 0.0;
                if (!std::isfinite(displayed_time_rate_))
                    displayed_time_rate_ = 0.0;
                return;
            }
        }
    }

    if (cfg.adaptive_substepping && !adaptive_substep_refining_ &&
        n > 0 && std::abs(scaled_dt) > 1.0e-9f) {
        // Use persistent scratch buffers for adaptive error estimation
        auto resize_adapt = [&](size_t sz) {
            if (scratch_adapt_full_pos_.size() < sz) {
                scratch_adapt_full_pos_.resize(sz); scratch_adapt_full_vel_.resize(sz);
                scratch_adapt_half_pos_.resize(sz); scratch_adapt_half_vel_.resize(sz);
                scratch_adapt_half2_pos_.resize(sz); scratch_adapt_half2_vel_.resize(sz);
            }
        };
        resize_adapt(n);
        auto& full_pos = scratch_adapt_full_pos_;  auto& full_vel = scratch_adapt_full_vel_;
        auto& half_pos = scratch_adapt_half_pos_;   auto& half_vel = scratch_adapt_half_vel_;
        auto& half2_pos = scratch_adapt_half2_pos_; auto& half2_vel = scratch_adapt_half2_vel_;

        auto estimate_segment_error = [&](float segment_scaled_dt) {
            if (std::abs(segment_scaled_dt) <= 1.0e-9f)
                return 0.0f;
            integrate_kinematics(segment_scaled_dt, pos0, vel0, &accel0, full_pos, full_vel);
            integrate_kinematics(segment_scaled_dt * 0.5f, pos0, vel0, &accel0, half_pos, half_vel);
            integrate_kinematics(segment_scaled_dt * 0.5f, half_pos, half_vel, nullptr, half2_pos, half2_vel);
            float max_error = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                float err = glm::length(full_pos[i] - half2_pos[i]);
                if (std::isfinite(err))
                    max_error = std::max(max_error, err);
            }
            return max_error;
        };

        float tolerance = std::clamp(cfg.adaptive_substep_tolerance, 1.0e-6f, 1.0e6f);
        int required_substeps = 1;
        // Cap search to 4 iterations (max 16 substeps from error estimation).
        // Each iteration does 3 full gravity evaluations, so limit cost.
        // Orbital-period refinement (above) handles the high-substep case.
        constexpr int kSearchCap = 16;
        float segment_error = estimate_segment_error(scaled_dt);
        int search_iters = 0;
        while (segment_error > tolerance && required_substeps < kSearchCap && search_iters < 4) {
            required_substeps *= 2;
            segment_error = estimate_segment_error(scaled_dt / (float)required_substeps);
            ++search_iters;
        }

        adaptive_substeps_required_ = std::max(required_substeps, 1);
        adaptive_substeps_last_ = std::min(adaptive_substeps_required_,
                                           std::max(cfg.adaptive_substep_max, 1));
        adaptive_substeps_saturated_ = adaptive_substeps_last_ < adaptive_substeps_required_;

        if (adaptive_substeps_required_ > 1 || adaptive_substeps_saturated_) {
            float refined_scaled_dt = scaled_dt / (float)adaptive_substeps_required_;
            float denom = cfg.dt_scale * time_sign;
            float refined_real_dt = (std::abs(denom) > 1.0e-9f)
                ? (refined_scaled_dt / denom)
                : (dt / (float)adaptive_substeps_required_);
            float applied_scaled_total = refined_scaled_dt * (float)adaptive_substeps_last_;
            adaptive_substep_refining_ = true;
            for (int substep = 0; substep < adaptive_substeps_last_; ++substep)
                step_physics(refined_real_dt);
            adaptive_substep_refining_ = false;
            displayed_time_rate_ = (std::abs(dt) > 1.0e-9f)
                ? (double)applied_scaled_total / (double)dt
                : 0.0;
            if (!std::isfinite(displayed_time_rate_))
                displayed_time_rate_ = 0.0;
            return;
        }
    }
    if (std::abs(dt) > 1.0e-9f)
        displayed_time_rate_ = (double)scaled_dt / (double)dt;
    if (!std::isfinite(displayed_time_rate_))
        displayed_time_rate_ = 0.0;
    cfg.sim_time_accumulated += (double)scaled_dt;

    auto& pos1 = scratch_int_pos_;
    auto& vel1 = scratch_int_vel_;
    integrate_kinematics(scaled_dt, pos0, vel0, &accel0, pos1, vel1);
    parallel_for(n, 384, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (bodies[i].marked_for_removal) continue;
            bodies[i].mass_loss_rate = 0.0f;
            if (!bodies[i].locked) {
                bodies[i].vel = vel1[i];
                bodies[i].pos = pos1[i];
            }
            bodies[i].age += scaled_dt;
        }
    });

    // Physics subsystems — during recursive substeps (orbit/adaptive refinement),
    // only run essential subsystems (gravity integration above is always done).
    // Heavy per-body processes are deferred to the outermost frame to avoid
    // O(substeps × N²) cost that causes the simulation to crawl.
    if (!adaptive_substep_refining_) {
        // Full physics — only on the outermost call
        if (cfg.roche_limit || cfg.tidal_forces) process_roche_limit(scaled_dt);
        if (cfg.collisions)         process_collisions(scaled_dt);
        if (cfg.spin_fragmentation) process_spin_fragmentation(scaled_dt);
        if (cfg.temperature_system) process_temperature(scaled_dt);
        if (cfg.temperature_system || cfg.evaporation) process_space_weather(scaled_dt);
        if (cfg.material_phases)    process_material_phases(scaled_dt);
        if (cfg.evaporation)        process_evaporation(scaled_dt);
        if (cfg.stellar_evolution)  process_stellar_evolution(scaled_dt);
        if (cfg.tidal_locking)      process_tidal_locking(scaled_dt);
        if (cfg.hawking_radiation)  process_hawking_radiation(scaled_dt);
        if (cfg.yarkovsky_effect)   process_yarkovsky(scaled_dt);
        process_orbital_elements();
    } else {
        // Substep: only collisions (critical for correctness) and cleanup
        if (cfg.collisions) process_collisions(scaled_dt);
    }
    cleanup_bodies();

    n = bodies.size();
    parallel_for(n, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            enforce_body_physical_limits(bodies[i]);
    });

    // Skip expensive per-body property + visual refresh during recursive substeps.
    // These only affect rendering and don't influence physics integration.
    if (!adaptive_substep_refining_) {
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i)
                refresh_planet_props(bodies[i]);
        });
        for (auto& b : state.bodies)
            refresh_body_visuals(b, &state);
        update_body_tracking_cache();
    }

    // Update trails (skip during recursive substeps — only render the outermost frame)
    if (!adaptive_substep_refining_) {
        n = bodies.size();
        while (state.trails.size() < n)
            state.trails.emplace_back();
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; i++) {
                state.trails[i].push_back(bodies[i].pos);
                while (state.trails[i].size() > cfg.trail_length)
                    state.trails[i].pop_front();
            }
        });
    }

    if (diagnostics_enabled_ && !adaptive_substep_refining_)
        validate_body_state("step_physics/post", true);
}
