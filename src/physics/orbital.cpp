#include "physics/simulation.h"
#include "physics/sim_helpers.h"
#include <algorithm>
#include <random>
#include <cmath>
#include <unordered_map>

// ── Orbital mechanics & bonds ────────────────────────────────────────────────
// Split from simulation.cpp: orbital assignment, nucleus repulsion,
// covalent bonds, and shell transitions.

// ── CPU-side orbital assignment ──────────────────────────────────────────────
// Clusters nucleons into nuclei, assigns electrons to orbital shells,
// and writes the ground-state angular momentum (L_ground) to genome[2].
// The shader reads this to apply the correct centrifugal barrier per electron.

void PhysicsSimulation::update_orbitals() {
    if (readback_positions_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float NUCLEAR_CLUSTER_RADIUS = 16.0f;    // matches shader YUKAWA_RANGE
    const float NUCLEAR_CLUSTER_RADIUS_SQ = NUCLEAR_CLUSTER_RADIUS * NUCLEAR_CLUSTER_RADIUS;
    // Tighter clustering radius for nucleons from different atoms —
    // electron clouds keep distinct atoms apart; only explicit fusion
    // (high energy collision) should merge nucleons from different atoms.
    const float CROSS_ATOM_CLUSTER_RADIUS = 5.0f;
    const float CROSS_ATOM_CLUSTER_RADIUS_SQ = CROSS_ATOM_CLUSTER_RADIUS * CROSS_ATOM_CLUSTER_RADIUS;
    const float BINDING_RADIUS = cfg.orbital_binding_radius;
    static const int MAX_ELECTRONS = 2 + 8 + 18 + 32;  // 60

    // Save previous orbital_parent before clearing (used to detect cross-atom clustering)
    std::vector<int32_t> prev_orbital_parent(particles.orbital_parent.begin(),
                                              particles.orbital_parent.end());
    prev_orbital_parent.resize(n, -1);

    // Clear orbital parent mapping for all particles
    particles.orbital_parent.resize(n, -1);
    std::fill(particles.orbital_parent.begin(), particles.orbital_parent.end(), -1);

    // Ensure orbital_shell and excitation_timer are sized (preserve previous values)
    particles.orbital_shell.resize(n, -1);
    particles.excitation_timer.resize(n, 0);

    // Save previous shell assignments before clearing
    std::vector<int8_t> prev_orbital_shell(particles.orbital_shell.begin(),
                                            particles.orbital_shell.end());

    // Helper: get nucleus representative for a nucleon from previous frame
    auto prev_nuc_rep = [&](uint32_t idx) -> int32_t {
        if (idx >= prev_orbital_parent.size()) return -1;
        int32_t p = prev_orbital_parent[idx];
        return (p >= 0) ? p : static_cast<int32_t>(idx);  // self if not in a nucleus
    };

    // ── Step 1: Cluster nucleons into nuclei ────────────────────────────
    struct Nucleus {
        glm::vec2 center;
        int Z;          // proton count
        int total;      // total nucleon count
        uint32_t rep;   // representative proton index (for orbital_parent tracking)
    };
    std::vector<Nucleus> nuclei;
    std::vector<bool> clustered(n, false);
    detected_nuclei_.clear();

    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];
        if (t != PROTON_TYPE && t != NEUTRON_TYPE) continue;

        // BFS: gather all nearby nucleons into one nucleus
        std::vector<uint32_t> members;
        members.reserve(64);
        members.push_back(i);
        clustered[i] = true;

        for (size_t front = 0; front < members.size(); ++front) {
            uint32_t mi = members[front];
            auto bfs_search = [&](uint32_t j) {
                if (clustered[j]) return;
                if (readback_energies_[j] < 0.01f) return;
                uint32_t tj = particles.types[j];
                if (tj != PROTON_TYPE && tj != NEUTRON_TYPE) return;
                glm::vec2 d = (readback_positions_[j] - readback_positions_[mi]);
                float d2 = glm::dot(d, d);

                // Use tighter radius only if BOTH nucleons were in established,
                // *different* atoms last frame. New/free particles (orbital_parent == -1)
                // use the normal radius so freshly spawned atoms can cluster properly.
                int32_t prev_mi = (mi < prev_orbital_parent.size()) ? prev_orbital_parent[mi] : -1;
                int32_t prev_j  = (j  < prev_orbital_parent.size()) ? prev_orbital_parent[j]  : -1;
                bool cross_atom = (prev_mi >= 0 && prev_j >= 0
                                   && prev_nuc_rep(mi) != prev_nuc_rep(j));
                float threshold = cross_atom ? CROSS_ATOM_CLUSTER_RADIUS_SQ
                                             : NUCLEAR_CLUSTER_RADIUS_SQ;
                if (d2 < threshold) {
                    members.push_back(j);
                    clustered[j] = true;
                }
            };
            if (iface.prefs.spatial_grid)
                grid_.query(readback_positions_[mi].x, readback_positions_[mi].y, NUCLEAR_CLUSTER_RADIUS, bfs_search);
            else
                for (uint32_t j = 0; j < n; ++j) bfs_search(j);
        }

        // Compute nucleus centroid and proton count
        Nucleus nuc{};
        nuc.rep = UINT32_MAX;
        for (uint32_t mi : members) {
            nuc.center += readback_positions_[mi];
            nuc.total++;
            if (particles.types[mi] == PROTON_TYPE) {
                nuc.Z++;
                if (nuc.rep == UINT32_MAX) nuc.rep = mi;  // first proton = representative
            }
        }
        nuc.center /= static_cast<float>(nuc.total);

        // Mark nucleon members as belonging to this nucleus
        if (nuc.Z > 0) {
            for (uint32_t mi : members)
                particles.orbital_parent[mi] = static_cast<int32_t>(nuc.rep);
            nuclei.push_back(nuc);

            // Store detailed nucleus info for nuclear decay
            NucleusInfo ni;
            ni.center = nuc.center;
            ni.Z = nuc.Z;
            ni.N = nuc.total - nuc.Z;
            ni.rep = nuc.rep;
            for (uint32_t mi : members) {
                if (particles.types[mi] == PROTON_TYPE) ni.proton_indices.push_back(mi);
                else ni.neutron_indices.push_back(mi);
            }
            detected_nuclei_.push_back(std::move(ni));
        }
    }

    // Also track free neutrons (Z=0 clusters or unclustered single neutrons) for decay
    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        if (particles.types[i] != NEUTRON_TYPE) continue;
        // Free neutron — add as Z=0, N=1 nucleus entry for decay tracking
        NucleusInfo fn;
        fn.center = readback_positions_[i];
        fn.Z = 0; fn.N = 1;
        fn.rep = i;
        fn.neutron_indices.push_back(i);
        detected_nuclei_.push_back(std::move(fn));
    }

    // ── Step 1b: Cluster antiprotons into antinuclei ────────────────────
    // Antinuclei are made of antiprotons (no antineutron type exists yet,
    // so Z = antiproton count, N = 0 for now).
    size_t anti_start_in_detected = detected_nuclei_.size();
    std::vector<Nucleus> antinuclei;
    for (uint32_t i = 0; i < n; ++i) {
        if (clustered[i]) continue;
        if (readback_energies_[i] < 0.01f) continue;
        if (particles.types[i] != ANTIPROTON_TYPE_PHYS) continue;

        // BFS: gather all nearby antiprotons
        std::vector<uint32_t> members;
        members.reserve(32);
        members.push_back(i);
        clustered[i] = true;

        for (size_t front = 0; front < members.size(); ++front) {
            uint32_t mi = members[front];
            auto anti_bfs_search = [&](uint32_t j) {
                if (clustered[j]) return;
                if (readback_energies_[j] < 0.01f) return;
                if (particles.types[j] != ANTIPROTON_TYPE_PHYS) return;
                glm::vec2 d = (readback_positions_[j] - readback_positions_[mi]);
                if (glm::dot(d, d) < NUCLEAR_CLUSTER_RADIUS_SQ) {
                    members.push_back(j);
                    clustered[j] = true;
                }
            };
            if (iface.prefs.spatial_grid)
                grid_.query(readback_positions_[mi].x, readback_positions_[mi].y, NUCLEAR_CLUSTER_RADIUS, anti_bfs_search);
            else
                for (uint32_t j = 0; j < n; ++j) anti_bfs_search(j);
        }

        Nucleus anuc{};
        anuc.rep = members[0];
        anuc.Z = static_cast<int>(members.size());
        anuc.total = anuc.Z;
        for (uint32_t mi : members) {
            anuc.center += readback_positions_[mi];
            particles.orbital_parent[mi] = static_cast<int32_t>(anuc.rep);
        }
        anuc.center /= static_cast<float>(anuc.total);
        antinuclei.push_back(anuc);

        NucleusInfo ani;
        ani.center = anuc.center;
        ani.Z = anuc.Z;
        ani.N = 0;
        ani.rep = anuc.rep;
        ani.is_anti = true;
        for (uint32_t mi : members)
            ani.proton_indices.push_back(mi);
        detected_nuclei_.push_back(std::move(ani));
    }

    // ── Step 2: Find electrons and assign to nearest nucleus ────────────
    //            Also find positrons and assign to nearest antinucleus.
    struct ElectronBind {
        uint32_t idx;
        int nuc_idx;
        float dist;
        bool assigned = false;
    };
    std::vector<ElectronBind> bindings;
    std::vector<ElectronBind> anti_bindings;  // positrons → antinuclei

    for (uint32_t i = 0; i < n; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];

        // Positrons orbit antinuclei only (antimatter counterpart of electrons→protons)
        if (t == POSITRON_TYPE_PHYS) {
            if (!antinuclei.empty()) {
                float best_d_sq = BINDING_RADIUS * BINDING_RADIUS;
                int best_anuc = -1;
                for (int ai = 0; ai < static_cast<int>(antinuclei.size()); ++ai) {
                    glm::vec2 d = readback_positions_[i] - antinuclei[ai].center;
                    float d_sq = glm::dot(d, d);
                    if (d_sq < best_d_sq) {
                        best_d_sq = d_sq;
                        best_anuc = ai;
                    }
                }
                if (best_anuc >= 0) {
                    anti_bindings.push_back({i, best_anuc, std::sqrt(best_d_sq)});
                    particles.orbital_parent[i] = static_cast<int32_t>(antinuclei[best_anuc].rep);
                    continue;
                }
            }
            // Free positron (no antinucleus nearby) — clear orbital data
            particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
            particles.orbital_shell[i] = -1;
            particles.excitation_timer[i] = 0;
            continue;
        }

        // Only electrons orbit proton-nuclei (no e-e orbiting)
        if (t != ELECTRON_TYPE_PHYS) {
            // Clear orbital data for non-electrons that might have stale genome[2]
            if (t == MUON_TYPE_PHYS || t == ANTIMUON_TYPE_PHYS ||
                t == TAU_TYPE_PHYS || t == ANTITAU_TYPE_PHYS) {
                particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
            }
            continue;
        }

        float best_d_sq = BINDING_RADIUS * BINDING_RADIUS;
        int best_nuc = -1;
        for (int ni = 0; ni < static_cast<int>(nuclei.size()); ++ni) {
            glm::vec2 d = readback_positions_[i] - nuclei[ni].center;
            float d_sq = glm::dot(d, d);
            if (d_sq < best_d_sq) {
                best_d_sq = d_sq;
                best_nuc = ni;
            }
        }

        if (best_nuc >= 0) {
            bindings.push_back({i, best_nuc, std::sqrt(best_d_sq)});
            particles.orbital_parent[i] = static_cast<int32_t>(nuclei[best_nuc].rep);
        } else {
            particles.genomes[i * GENOME_SIZE + 2] = 0.0f;  // free electron
            particles.orbital_shell[i] = -1;
            particles.excitation_timer[i] = 0;
        }
    }

    // Sort by distance (closest electrons get inner shells first)
    std::sort(bindings.begin(), bindings.end(), [](const ElectronBind& a, const ElectronBind& b) {
        if (a.nuc_idx != b.nuc_idx) return a.nuc_idx < b.nuc_idx;
        return a.dist < b.dist;
    });

    // ── Step 3: Assign orbital shells and compute L_ground ──────────────
    // Two-pass approach: first retain electrons in their previous shells,
    // then assign newly-captured electrons to available slots.
    // shell_fill[nuc_idx * NUM_SHELLS_O + shell] = count of electrons in that shell
    std::vector<int> shell_fill(nuclei.size() * NUM_SHELLS_O, 0);

    // Helper: compute and write L_ground + boost for an electron in a given shell
    auto write_orbital_params = [&](uint32_t idx, int nuc_idx, int shell) {
        int Z = nuclei[nuc_idx].Z;
        int inner_electrons = 0;
        for (int s = 0; s < shell; ++s)
            inner_electrons += shell_fill[nuc_idx * NUM_SHELLS_O + s];
        float Z_eff = std::max(1.0f, static_cast<float>(Z - inner_electrons));
        float n_shell = static_cast<float>(shell + 1);
        float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
        particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
        particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
        particles.orbital_shell[idx] = static_cast<int8_t>(shell);
    };

    // Pass 1: retain electrons that were already bound to the same nucleus
    for (auto& b : bindings) {
        int Z = nuclei[b.nuc_idx].Z;
        int8_t prev_shell = (b.idx < prev_orbital_shell.size()) ? prev_orbital_shell[b.idx] : -1;
        int32_t prev_parent = (b.idx < prev_orbital_parent.size()) ? prev_orbital_parent[b.idx] : -1;
        bool was_bound_here = (prev_parent >= 0 &&
                               prev_parent == static_cast<int32_t>(nuclei[b.nuc_idx].rep) &&
                               prev_shell >= 0 && prev_shell < NUM_SHELLS_O);
        if (!was_bound_here) continue;

        // Check capacity: total assigned to this nucleus vs max allowed
        int total_assigned = 0;
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            total_assigned += shell_fill[b.nuc_idx * NUM_SHELLS_O + s];
        if (total_assigned >= std::min(Z, MAX_ELECTRONS)) continue;

        // Try to keep in same shell
        int offset = b.nuc_idx * NUM_SHELLS_O + prev_shell;
        if (shell_fill[offset] < SHELL_CAP_O[prev_shell]) {
            shell_fill[offset]++;
            write_orbital_params(b.idx, b.nuc_idx, prev_shell);
            // Preserve excitation_timer — don't reset it
            b.assigned = true;
        }
    }

    // Pass 2: assign newly-captured or displaced electrons to lowest available shell
    for (auto& b : bindings) {
        if (b.assigned) continue;
        int Z = nuclei[b.nuc_idx].Z;

        int total_assigned = 0;
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            total_assigned += shell_fill[b.nuc_idx * NUM_SHELLS_O + s];

        if (total_assigned >= std::min(Z, MAX_ELECTRONS)) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;  // excess, free
            particles.orbital_shell[b.idx] = -1;
            continue;
        }

        // Find first shell with room
        int shell = -1;
        for (int s = 0; s < NUM_SHELLS_O; ++s) {
            if (shell_fill[b.nuc_idx * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                shell = s;
                break;
            }
        }
        if (shell < 0) {
            particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
            particles.orbital_shell[b.idx] = -1;
            continue;
        }

        shell_fill[b.nuc_idx * NUM_SHELLS_O + shell]++;
        write_orbital_params(b.idx, b.nuc_idx, shell);
        particles.excitation_timer[b.idx] = 0;  // newly captured = ground state
    }

    // Copy shell_fill into NucleusInfo for matter nuclei
    for (size_t ni = 0; ni < nuclei.size() && ni < detected_nuclei_.size(); ++ni) {
        for (int s = 0; s < NUM_SHELLS_O; ++s)
            detected_nuclei_[ni].shell_fill[s] = shell_fill[ni * NUM_SHELLS_O + s];
    }

    // ── Step 3b: Assign positron orbital shells around antinuclei ─────────
    if (!anti_bindings.empty()) {
        std::sort(anti_bindings.begin(), anti_bindings.end(),
                  [](const ElectronBind& a, const ElectronBind& b) {
                      if (a.nuc_idx != b.nuc_idx) return a.nuc_idx < b.nuc_idx;
                      return a.dist < b.dist;
                  });
        std::vector<int> anti_shell_fill(antinuclei.size() * NUM_SHELLS_O, 0);

        // Helper for antinuclei orbital params
        auto write_anti_orbital_params = [&](uint32_t idx, int anuc_idx, int shell) {
            int Z = antinuclei[anuc_idx].Z;
            int inner = 0;
            for (int s = 0; s < shell; ++s)
                inner += anti_shell_fill[anuc_idx * NUM_SHELLS_O + s];
            float Z_eff = std::max(1.0f, static_cast<float>(Z - inner));
            float n_shell = static_cast<float>(shell + 1);
            float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_O;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
            particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
            particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
            particles.orbital_shell[idx] = static_cast<int8_t>(shell);
        };

        // Pass 1: retain positrons already bound to same antinucleus
        for (auto& b : anti_bindings) {
            int Z = antinuclei[b.nuc_idx].Z;
            int8_t prev_shell = (b.idx < prev_orbital_shell.size()) ? prev_orbital_shell[b.idx] : -1;
            int32_t prev_parent = (b.idx < prev_orbital_parent.size()) ? prev_orbital_parent[b.idx] : -1;
            bool was_bound_here = (prev_parent >= 0 &&
                                   prev_parent == static_cast<int32_t>(antinuclei[b.nuc_idx].rep) &&
                                   prev_shell >= 0 && prev_shell < NUM_SHELLS_O);
            if (!was_bound_here) continue;

            int total_assigned = 0;
            for (int s = 0; s < NUM_SHELLS_O; ++s)
                total_assigned += anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s];
            if (total_assigned >= std::min(Z, MAX_ELECTRONS)) continue;

            int offset = b.nuc_idx * NUM_SHELLS_O + prev_shell;
            if (anti_shell_fill[offset] < SHELL_CAP_O[prev_shell]) {
                anti_shell_fill[offset]++;
                write_anti_orbital_params(b.idx, b.nuc_idx, prev_shell);
                b.assigned = true;
            }
        }

        // Pass 2: assign newly-captured positrons
        for (auto& b : anti_bindings) {
            if (b.assigned) continue;
            int Z = antinuclei[b.nuc_idx].Z;

            int total_assigned = 0;
            for (int s = 0; s < NUM_SHELLS_O; ++s)
                total_assigned += anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s];

            if (total_assigned >= std::min(Z, MAX_ELECTRONS)) {
                particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
                particles.orbital_shell[b.idx] = -1;
                continue;
            }

            int shell = -1;
            for (int s = 0; s < NUM_SHELLS_O; ++s) {
                if (anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                    shell = s;
                    break;
                }
            }
            if (shell < 0) {
                particles.genomes[b.idx * GENOME_SIZE + 2] = 0.0f;
                particles.orbital_shell[b.idx] = -1;
                continue;
            }

            anti_shell_fill[b.nuc_idx * NUM_SHELLS_O + shell]++;
            write_anti_orbital_params(b.idx, b.nuc_idx, shell);
            particles.excitation_timer[b.idx] = 0;
        }

        // Copy anti_shell_fill into NucleusInfo for antinuclei
        for (size_t ai = 0; ai < antinuclei.size(); ++ai) {
            size_t di = anti_start_in_detected + ai;
            if (di < detected_nuclei_.size()) {
                for (int s = 0; s < NUM_SHELLS_O; ++s)
                    detected_nuclei_[di].shell_fill[s] = anti_shell_fill[ai * NUM_SHELLS_O + s];
            }
        }
    }
}

// ── Inter-nucleus Coulomb repulsion ──────────────────────────────────────────
// Prevents distinct UNBONDED atoms from drifting close enough for their
// nucleons to overlap and be merged by the BFS clustering in update_orbitals().
// Applies both position correction and velocity impulse proportional to Z₁·Z₂.
// Bonded atom pairs are skipped — they're held at bond length by the spring.

void PhysicsSimulation::repel_distinct_nuclei() {
    if (detected_nuclei_.size() < 2) return;

    const float REPEL_RADIUS    = 40.0f;   // start repelling when centers this close
    const float REPEL_RADIUS_SQ = REPEL_RADIUS * REPEL_RADIUS;
    const float DANGER_DIST     = 16.0f;   // below this, hard position push (> cluster radius 10)
    const float MIN_DIST        = 3.0f;    // softening floor
    const float VEL_K           = 80.0f;   // velocity impulse strength (was 1.5; needs to overcome drift)
    const float POS_K           = 1.2f;    // position correction strength (for close nuclei)
    const uint32_t n = cfg.particle_count;
    bool any_modified = false;

    // Helper: check if two nuclei are covalently bonded
    auto nuclei_bonded = [&](uint32_t rep_a, uint32_t rep_b) -> bool {
        if (bond_data_.empty()) return false;
        size_t required = static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE;
        if (bond_data_.size() < required) return false;
        uint32_t base = rep_a * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (base + s >= bond_data_.size()) break;
            if (bond_data_[base + s] == rep_b) return true;
        }
        return false;
    };

    for (size_t a = 0; a < detected_nuclei_.size(); ++a) {
        auto& na = detected_nuclei_[a];
        if (na.Z <= 0) continue;

        for (size_t b = a + 1; b < detected_nuclei_.size(); ++b) {
            auto& nb = detected_nuclei_[b];
            if (nb.Z <= 0) continue;

            // Skip bonded atom pairs — spring force handles them
            if (nuclei_bonded(na.rep, nb.rep) || nuclei_bonded(nb.rep, na.rep))
                continue;

            glm::vec2 delta = nb.center - na.center;
            float d2 = glm::dot(delta, delta);
            if (d2 > REPEL_RADIUS_SQ || d2 < 0.001f) continue;

            float dist = std::sqrt(d2);
            float eff_dist = std::max(dist, MIN_DIST);
            glm::vec2 dir = delta / dist;

            // Mass-weighted fractions (lighter nucleus moves more)
            float mass_a = static_cast<float>(na.Z + static_cast<int>(na.neutron_indices.size()));
            float mass_b = static_cast<float>(nb.Z + static_cast<int>(nb.neutron_indices.size()));
            float total_mass = mass_a + mass_b;
            float frac_a = mass_b / total_mass;
            float frac_b = mass_a / total_mass;

            // ── Velocity impulse: Z₁·Z₂ / r², smooth fade at edge ──────
            float t = dist / REPEL_RADIUS;  // 0..1
            float fade = (1.0f - t) * (1.0f - t);  // quadratic fade to zero at edge
            float zz = static_cast<float>(na.Z * nb.Z);
            float impulse = VEL_K * zz * fade / (eff_dist * eff_dist);
            impulse = std::min(impulse, 4.0f);

            glm::vec2 vel_a = -dir * impulse * frac_a;
            glm::vec2 vel_b =  dir * impulse * frac_b;

            // ── Position correction: hard push when dangerously close ───
            glm::vec2 pos_a(0.0f), pos_b(0.0f);
            if (dist < DANGER_DIST) {
                float penetration = DANGER_DIST - dist;
                float pos_push = POS_K * penetration * std::min(zz, 10.0f);
                pos_push = std::min(pos_push, 3.0f);  // cap per-frame correction
                pos_a = -dir * pos_push * frac_a;
                pos_b =  dir * pos_push * frac_b;
            }

            // Apply to all nucleons in each nucleus
            for (uint32_t idx : na.proton_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_a;
                    readback_positions_[idx] += pos_a;
                }
            }
            for (uint32_t idx : na.neutron_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_a;
                    readback_positions_[idx] += pos_a;
                }
            }
            for (uint32_t idx : nb.proton_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_b;
                    readback_positions_[idx] += pos_b;
                }
            }
            for (uint32_t idx : nb.neutron_indices) {
                if (idx < n) {
                    readback_velocities_[idx] += vel_b;
                    readback_positions_[idx] += pos_b;
                }
            }
            any_modified = true;
        }
    }

    if (any_modified) cpu_particles_dirty_ = true;
}

// ── CPU-side covalent bond formation / breaking ─────────────────────────────
// Detects atoms (from detected_nuclei_) with unfilled valence shells and forms
// bonds between nearby atoms.  Bond data is uploaded to GPU for spring forces.

void PhysicsSimulation::update_bonds() {
    if (readback_positions_.empty() || detected_nuclei_.empty()) return;

    const uint32_t n = cfg.particle_count;
    const float FORM_RADIUS    = cfg.bond_form_radius;
    const float BREAK_DIST     = cfg.bond_rest_length * cfg.bond_break_factor;
    const float BREAK_DIST_SQ  = BREAK_DIST * BREAK_DIST;
    const float ACTIVATION_E   = cfg.bond_activation_energy;
    const int   MAX_NEW_BONDS  = 10;

    // Ensure bond_data_ is sized correctly
    size_t required = static_cast<size_t>(n) * MAX_BONDS_PER_PARTICLE;
    if (bond_data_.size() != required) {
        bond_data_.assign(required, 0xFFFFFFFFu);
    }

    // Build rep → nucleus index map for quick lookup
    std::unordered_map<uint32_t, uint32_t> rep_to_nuc;
    rep_to_nuc.reserve(detected_nuclei_.size());
    for (uint32_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        rep_to_nuc[detected_nuclei_[ni].rep] = ni;
    }

    // Helper: count current bonds for a particle
    auto bond_count = [&](uint32_t rep) -> int {
        int cnt = 0;
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] != 0xFFFFFFFFu) ++cnt;
        }
        return cnt;
    };

    // Helper: check if a already bonded to b
    auto has_bond = [&](uint32_t rep_a, uint32_t rep_b) -> bool {
        uint32_t base = rep_a * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == rep_b) return true;
        }
        return false;
    };

    // Helper: add bond slot (returns true if added)
    auto add_bond_slot = [&](uint32_t rep, uint32_t partner) -> bool {
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == 0xFFFFFFFFu) {
                bond_data_[base + s] = partner;
                return true;
            }
        }
        return false;
    };

    // Helper: remove bond slot
    auto remove_bond_slot = [&](uint32_t rep, uint32_t partner) {
        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            if (bond_data_[base + s] == partner) {
                bond_data_[base + s] = 0xFFFFFFFFu;
                return;
            }
        }
    };

    bool any_changed = false;

    // Element symbols for event logging
    static const char* BSYM[] = {
        "n","H","He","Li","Be","B","C","N","O","F","Ne",
        "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
        "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
        "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
        "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
        "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd"
    };
    static const int BSYM_COUNT = static_cast<int>(sizeof(BSYM) / sizeof(BSYM[0]));
    auto bsym = [&](int Z) -> const char* {
        return (Z >= 0 && Z < BSYM_COUNT) ? BSYM[Z] : "?";
    };

    // ── Break pass: remove bonds where atoms are too far apart ───────────
    for (uint32_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        const auto& nuc = detected_nuclei_[ni];
        if (nuc.Z == 0) continue;
        uint32_t rep = nuc.rep;
        if (rep >= n) continue;

        uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
        for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
            uint32_t partner = bond_data_[base + s];
            if (partner == 0xFFFFFFFFu || partner >= n) continue;

            // Check if partner is still a valid nucleus rep
            if (rep_to_nuc.find(partner) == rep_to_nuc.end()) {
                // Partner nucleus no longer exists — break bond
                bond_data_[base + s] = 0xFFFFFFFFu;
                remove_bond_slot(partner, rep);
                any_changed = true;
                {
                    char desc[128], detail[256];
                    int A_a = nuc.Z + nuc.N;
                    snprintf(desc, sizeof(desc), "Bond broken: %s-%d — ? (partner lost)",
                             bsym(nuc.Z), A_a);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Partner rep #%u no longer exists\n"
                             "Cause: partner nucleus destroyed",
                             bsym(nuc.Z), A_a, rep, nuc.Z, nuc.N, partner);
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_BROKEN,
                        ImVec4(0.9f, 0.45f, 0.3f, 1.0f), std::string(detail));
                }
                continue;
            }

            glm::vec2 delta = (readback_positions_[partner] - readback_positions_[rep]);
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > BREAK_DIST_SQ) {
                bond_data_[base + s] = 0xFFFFFFFFu;
                remove_bond_slot(partner, rep);
                any_changed = true;
                {
                    auto& nuc_b = detected_nuclei_[rep_to_nuc[partner]];
                    char desc[128], detail[256];
                    int A_a = nuc.Z + nuc.N, A_b = nuc_b.Z + nuc_b.N;
                    float dist = std::sqrt(d2);
                    snprintf(desc, sizeof(desc), "Bond broken: %s-%d — %s-%d",
                             bsym(nuc.Z), A_a, bsym(nuc_b.Z), A_b);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Atom B: %s-%d (rep #%u, Z=%d N=%d)\n"
                             "Separation: %.1f px (threshold: %.1f px)",
                             bsym(nuc.Z), A_a, rep, nuc.Z, nuc.N,
                             bsym(nuc_b.Z), A_b, partner, nuc_b.Z, nuc_b.N,
                             dist, std::sqrt(BREAK_DIST_SQ));
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_BROKEN,
                        ImVec4(0.9f, 0.45f, 0.3f, 1.0f), std::string(detail));
                }
            }
        }
    }

    // ── Form pass: create new bonds between nearby atoms with unfilled valence ─
    int new_bonds = 0;
    for (uint32_t ni = 0; ni < detected_nuclei_.size() && new_bonds < MAX_NEW_BONDS; ++ni) {
        const auto& nuc = detected_nuclei_[ni];
        if (nuc.Z == 0) continue;

        int valence = valence_from_Z(nuc.Z);
        if (valence <= 0) continue;

        uint32_t rep_a = nuc.rep;
        if (rep_a >= n) continue;

        int current_bonds_a = bond_count(rep_a);
        if (current_bonds_a >= valence) continue;

        // Search for nearby atoms
        glm::vec2 center_a = nuc.center;

        auto bond_search = [&](uint32_t j) {
            if (new_bonds >= MAX_NEW_BONDS) return;
            if (current_bonds_a >= valence) return;

            // j must be a nucleus rep
            auto it = rep_to_nuc.find(j);
            if (it == rep_to_nuc.end()) return;

            uint32_t nj = it->second;
            const auto& nuc_b = detected_nuclei_[nj];
            if (nuc_b.Z == 0) return;
            if (nuc_b.is_anti != nuc.is_anti) return;  // no matter-antimatter bonds
            if (nuc_b.rep == rep_a) return;  // same nucleus

            // Already bonded?
            if (has_bond(rep_a, nuc_b.rep)) return;

            // Partner has room?
            int valence_b = valence_from_Z(nuc_b.Z);
            if (valence_b <= 0) return;
            if (bond_count(nuc_b.rep) >= valence_b) return;

            // Distance check (use nucleus centers)
            glm::vec2 delta = nuc_b.center - center_a;
            float d2 = delta.x * delta.x + delta.y * delta.y;
            if (d2 > FORM_RADIUS * FORM_RADIUS) return;

            // Activation energy: relative KE between nucleus centers
            glm::vec2 vel_a = readback_velocities_[rep_a];
            glm::vec2 vel_b = readback_velocities_[nuc_b.rep];
            glm::vec2 rel_v = vel_b - vel_a;
            float v_rel_sq = glm::dot(rel_v, rel_v);
            if (v_rel_sq > ACTIVATION_E * 100.0f) return;  // too fast = scattering, not bonding
            // (no minimum check — thermal capture is fine)

            // Form the bond
            if (add_bond_slot(rep_a, nuc_b.rep) && add_bond_slot(nuc_b.rep, rep_a)) {
                current_bonds_a++;
                new_bonds++;
                any_changed = true;
                try_unlock(ACH_FIRST_BOND);
                {
                    char desc[128], detail[384];
                    int A_a = nuc.Z + nuc.N, A_b = nuc_b.Z + nuc_b.N;
                    float dist = std::sqrt(d2);
                    snprintf(desc, sizeof(desc), "Bond formed: %s-%d — %s-%d",
                             bsym(nuc.Z), A_a, bsym(nuc_b.Z), A_b);
                    snprintf(detail, sizeof(detail),
                             "Atom A: %s-%d (rep #%u, Z=%d N=%d, valence %d, bonds %d)\n"
                             "Atom B: %s-%d (rep #%u, Z=%d N=%d, valence %d, bonds %d)\n"
                             "Distance: %.1f px",
                             bsym(nuc.Z), A_a, rep_a, nuc.Z, nuc.N,
                             valence, current_bonds_a,
                             bsym(nuc_b.Z), A_b, nuc_b.rep, nuc_b.Z, nuc_b.N,
                             valence_b, bond_count(nuc_b.rep),
                             dist);
                    iface.push_decay_event(desc, PhysicsInterface::DEVT_BOND_FORMED,
                        ImVec4(0.3f, 0.85f, 0.5f, 1.0f), std::string(detail));
                }
            }
        };

        grid_.query(center_a.x, center_a.y, FORM_RADIUS, bond_search);
    }

    // Update particle bond pointers for GPU upload
    particles.bond_partners_ptr = bond_data_.data();
    particles.bond_partners_count = static_cast<uint32_t>(bond_data_.size());

    if (any_changed) {
        cpu_particles_dirty_ = true;
        if (new_bonds > 0) {
            audio.play(AudioPlayer::SFX_BOND, frame_counter_);
        }
    }
}

void PhysicsSimulation::check_shell_transitions() {
    if (readback_positions_.empty() || detected_nuclei_.empty()) return;

    const uint32_t n = cfg.particle_count;
    std::mt19937 rng(frame_counter_ * 2654435761u + 7);
    static constexpr float BINDING_ENERGY[4] = {0.50f, 0.30f, 0.15f, 0.08f};

    int transition_count = 0;

    // Build per-nucleus shell occupancy from current orbital_shell data
    // Map nucleus rep → index in detected_nuclei_
    std::unordered_map<int32_t, size_t> rep_to_nuc;
    for (size_t ni = 0; ni < detected_nuclei_.size(); ++ni) {
        if (!detected_nuclei_[ni].is_anti)
            rep_to_nuc[static_cast<int32_t>(detected_nuclei_[ni].rep)] = ni;
    }

    // Count current shell occupancy per nucleus
    // shell_occ[ni * NUM_SHELLS_O + s] = current electron count in shell s of nucleus ni
    std::vector<int> shell_occ(detected_nuclei_.size() * NUM_SHELLS_O, 0);
    for (uint32_t i = 0; i < n; ++i) {
        if (particles.orbital_shell.size() <= i) break;
        int8_t sh = particles.orbital_shell[i];
        if (sh < 0 || sh >= NUM_SHELLS_O) continue;
        int32_t parent = particles.orbital_parent[i];
        if (parent < 0) continue;
        auto it = rep_to_nuc.find(parent);
        if (it == rep_to_nuc.end()) continue;
        shell_occ[it->second * NUM_SHELLS_O + sh]++;
    }

    // Helper: find a dormant slot for photon emission
    auto find_dormant = [&](uint32_t start) -> uint32_t {
        for (uint32_t i = start; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) return i;
        }
        return UINT32_MAX;
    };

    // Helper: recompute L_ground for electron in a given shell of a nucleus
    auto recompute_L = [&](uint32_t idx, int Z, int shell, size_t ni) {
        int inner = 0;
        for (int s = 0; s < shell; ++s)
            inner += shell_occ[ni * NUM_SHELLS_O + s];
        float Z_eff = std::max(1.0f, static_cast<float>(Z - inner));
        float n_shell = static_cast<float>(shell + 1);
        float R_target = std::max(n_shell * n_shell * R_BOHR / Z_eff, 8.0f);
        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
        particles.genomes[idx * GENOME_SIZE + 2] = L_ground;
        particles.genomes[idx * GENOME_SIZE + 3] = cfg.orbit_boost[shell];
    };

    for (uint32_t i = 0; i < n && transition_count < cfg.max_transitions_per_frame; ++i) {
        if (readback_energies_[i] < 0.01f) continue;
        uint32_t t = particles.types[i];
        if (t != ELECTRON_TYPE_PHYS && t != POSITRON_TYPE_PHYS) continue;
        if (i >= particles.orbital_shell.size()) continue;

        int8_t shell = particles.orbital_shell[i];
        if (shell < 0) continue;  // free electron, not bound

        int32_t parent = particles.orbital_parent[i];
        if (parent < 0) continue;

        auto it = rep_to_nuc.find(parent);
        if (it == rep_to_nuc.end()) continue;
        size_t ni = it->second;
        int Z = detected_nuclei_[ni].Z;
        float Z_factor = std::sqrt(std::max(1.0f, static_cast<float>(Z)));

        // ── Spontaneous de-excitation ──
        if (particles.excitation_timer[i] > 0) {
            particles.excitation_timer[i]++;

            if (particles.excitation_timer[i] >= static_cast<uint16_t>(cfg.deexcitation_lifetime)) {
                // Find lowest shell with vacancy
                int target_shell = -1;
                for (int s = 0; s < shell; ++s) {
                    if (shell_occ[ni * NUM_SHELLS_O + s] < SHELL_CAP_O[s]) {
                        target_shell = s;
                        break;
                    }
                }

                if (target_shell >= 0) {
                    // De-excite: move electron down, emit photon
                    float energy_released = (BINDING_ENERGY[shell] - BINDING_ENERGY[target_shell]) * Z_factor;
                    // Note: BINDING_ENERGY is ordered inner=highest, so released = old_shell - new_shell
                    // But since old_shell > target_shell numerically, and binding energy is lower for outer shells:
                    // energy_released = BINDING_ENERGY[target_shell] - BINDING_ENERGY[shell]
                    energy_released = (BINDING_ENERGY[target_shell] - BINDING_ENERGY[shell]) * Z_factor;

                    // Update shell occupancy
                    shell_occ[ni * NUM_SHELLS_O + shell]--;
                    shell_occ[ni * NUM_SHELLS_O + target_shell]++;

                    particles.orbital_shell[i] = static_cast<int8_t>(target_shell);
                    particles.excitation_timer[i] = 0;
                    recompute_L(i, Z, target_shell, ni);

                    // Spawn photon
                    if (energy_released > 0.01f) {
                        uint32_t slot = find_dormant(0);
                        if (slot != UINT32_MAX) {
                            write_spawn_genome(particles, slot, PHOTON_TYPE_PHYS, rng, frame_counter_);
                            readback_positions_[slot] = readback_positions_[i];
                            // Tangent direction for momentum conservation
                            glm::vec2 to_nuc = detected_nuclei_[ni].center - readback_positions_[i];
                            float len = glm::length(to_nuc);
                            glm::vec2 tangent = (len > 0.1f)
                                ? glm::vec2(-to_nuc.y / len, to_nuc.x / len)
                                : glm::vec2(1.0f, 0.0f);
                            readback_velocities_[slot] = tangent * C_SIM;
                            readback_energies_[slot] = std::min(energy_released, 1.0f);
                            particles.orbital_shell[slot] = -1;
                            particles.excitation_timer[slot] = 0;
                            cpu_particles_dirty_ = true;
                        }
                    }

                    transition_count++;
                    iface.push_decay_event(
                        "De-excitation: e\xe2\x81\xbb \xe2\x86\x92 lower shell + \xce\xb3",
                        PhysicsInterface::DEVT_PHOTOELECTRIC,
                        ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "");
                    continue;
                }

                // No lower shell available: if at outermost occupied shell and timer >> lifetime, ionize
                if (shell >= 2 && particles.excitation_timer[i] >= static_cast<uint16_t>(cfg.deexcitation_lifetime * 2.0f)) {
                    // Ionize
                    particles.orbital_parent[i] = -1;
                    particles.orbital_shell[i] = -1;
                    particles.excitation_timer[i] = 0;
                    particles.genomes[i * GENOME_SIZE + 2] = 0.0f;
                    particles.genomes[i * GENOME_SIZE + 3] = 0.0f;
                    shell_occ[ni * NUM_SHELLS_O + shell]--;

                    // Radial kick outward
                    glm::vec2 outward = readback_positions_[i] - detected_nuclei_[ni].center;
                    float len = glm::length(outward);
                    if (len > 0.1f) outward /= len;
                    else outward = glm::vec2(1.0f, 0.0f);
                    readback_velocities_[i] += outward * 40.0f;
                    cpu_particles_dirty_ = true;

                    // Spawn electron hole at nucleus position
                    {
                        uint32_t slot = find_dormant(0);
                        if (slot != UINT32_MAX) {
                            readback_positions_[slot] = detected_nuclei_[ni].center;
                            readback_velocities_[slot] = glm::vec2(0.0f);
                            readback_energies_[slot] = 1.0f;
                            write_spawn_genome(particles, slot, ELECTRON_HOLE_TYPE_PHYS, rng, frame_counter_);
                            particles.orbital_parent[slot] = -1;
                            particles.orbital_shell[slot] = -1;
                            iface.push_decay_event("Electron hole spawned (thermal ionization)",
                                PhysicsInterface::DEVT_ELECTRON_HOLE, ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "");
                        }
                    }

                    transition_count++;
                    iface.push_decay_event(
                        "Ionization: excited e\xe2\x81\xbb escaped atom",
                        PhysicsInterface::DEVT_PHOTOELECTRIC,
                        ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "");
                    continue;
                }
            }
            continue;  // excited electrons don't also get thermal promotion
        }

        // ── Thermal promotion (ground state → higher shell) ──
        if (shell >= NUM_SHELLS_O - 1) continue;  // already at outermost shell

        // Compute kinetic energy of this electron
        float v_sq = glm::dot(readback_velocities_[i], readback_velocities_[i]);
        // electron mass_inv ≈ 1.0 (lightest), so KE ≈ 0.5 * v²
        float KE = 0.5f * v_sq / (C_SIM * C_SIM);  // normalize to sim energy scale

        int next_shell = shell + 1;
        float delta_E = (BINDING_ENERGY[shell] - BINDING_ENERGY[next_shell]) * Z_factor;

        // Need KE > 1.5× the energy gap to promote (thermal threshold)
        if (KE > delta_E * 1.5f) {
            // Check if next shell has room
            if (shell_occ[ni * NUM_SHELLS_O + next_shell] < SHELL_CAP_O[next_shell]) {
                // Promote
                shell_occ[ni * NUM_SHELLS_O + shell]--;
                shell_occ[ni * NUM_SHELLS_O + next_shell]++;
                particles.orbital_shell[i] = static_cast<int8_t>(next_shell);
                particles.excitation_timer[i] = 1;  // mark as excited
                recompute_L(i, Z, next_shell, ni);

                // Reduce electron speed (it spent energy climbing)
                float speed = std::sqrt(v_sq);
                if (speed > 1.0f) {
                    float new_speed = speed * 0.7f;  // lose ~30% of speed
                    readback_velocities_[i] *= (new_speed / speed);
                }
                cpu_particles_dirty_ = true;
                transition_count++;
                iface.push_decay_event(
                    "Excitation: e\xe2\x81\xbb promoted to higher shell",
                    PhysicsInterface::DEVT_PHOTOELECTRIC,
                    ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "");
            }
        }
    }
}
