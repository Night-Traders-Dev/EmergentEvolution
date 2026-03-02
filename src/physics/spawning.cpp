#include "physics/simulation.h"
#include "physics/sim_helpers.h"
#include "physics/molecules.h"
#include <algorithm>
#include <random>
#include <cmath>

// ── Particle spawning ────────────────────────────────────────────────────────
// Split from simulation.cpp: accelerator fire, atom spawning, and
// general particle spawning at world coordinates.

void PhysicsSimulation::do_accelerator_fire(glm::vec2 aim_world_pos) {
    if (!compute.is_ready()) return;
    int32_t src = iface.accel_source_idx;

    uint32_t n = cfg.particle_count;
    if (readback_positions_.size() != n) {
        readback_positions_.resize(n);
        readback_velocities_.resize(n);
        readback_energies_.resize(n);
    }
    compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

    glm::vec2 dir;
    glm::vec2 spawn_origin;
    if (src >= 0 && src < static_cast<int32_t>(cfg.particle_count)) {
        // Targeted mode: fire toward selected target
        if (readback_energies_[src] < 0.01f) {
            iface.accel_phase = 0;
            iface.accel_source_idx = -1;
            iface.push_notification("Target particle died!", ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            return;
        }
        glm::vec2 target_pos = readback_positions_[src];
        dir = target_pos - aim_world_pos;
        float dist = glm::length(dir);
        if (dist < 1.0f) return;
        dir /= dist;
        spawn_origin = aim_world_pos;
    } else {
        // Free-fire mode: fire from stored origin toward click point
        dir = aim_world_pos - iface.accel_free_origin;
        float dist = glm::length(dir);
        if (dist < 1.0f) dir = glm::vec2(1.0f, 0.0f);
        else dir /= dist;
        spawn_origin = iface.accel_free_origin;
    }

    uint32_t fire_type = static_cast<uint32_t>(iface.accel_fire_type);
    float speed = iface.accel_speed;

    // Build shot directions
    std::vector<glm::vec2> shot_dirs;
    if (iface.accel_fire_mode == 1) {
        // Triple: -5°, 0°, +5°
        float spread = 5.0f * 3.14159265f / 180.0f;
        for (int s = -1; s <= 1; ++s) {
            float a = static_cast<float>(s) * spread;
            float cs = std::cos(a), sn = std::sin(a);
            shot_dirs.push_back({dir.x * cs - dir.y * sn,
                                 dir.x * sn + dir.y * cs});
        }
    } else {
        shot_dirs.push_back(dir);
    }

    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    float offset_dist = cfg.radius * 4.0f + 8.0f;

    std::mt19937 rng(static_cast<uint32_t>(aim_world_pos.x * 1000.0f + aim_world_pos.y + frame_counter_));
    uint32_t search_from = 0;
    bool any_spawned = false;

    for (auto& sd : shot_dirs) {
        // Find dormant slot
        uint32_t slot = UINT32_MAX;
        for (uint32_t i = search_from; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) { slot = i; break; }
        }
        if (slot == UINT32_MAX) break;
        search_from = slot + 1;

        // Spawn at origin (slightly offset in fire direction)
        glm::vec2 spawn_pos = spawn_origin + sd * offset_dist;
        spawn_pos.x = std::fmod(spawn_pos.x + rw, rw);
        spawn_pos.y = std::fmod(spawn_pos.y + rh, rh);

        readback_positions_[slot] = spawn_pos;

        // Massless particles always travel at c
        float m0 = (fire_type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[fire_type] : 0.0f;
        float actual_speed = (m0 < 0.001f) ? C_SIM : speed;
        readback_velocities_[slot] = sd * actual_speed;

        // Relativistic energy: E = γm₀c² for massive, E ∝ β for massless
        float beta = std::min(actual_speed / C_SIM, 0.9999f);
        float E_MeV;
        if (m0 < 0.001f) {
            E_MeV = beta;  // massless: energy proxy
        } else {
            float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
            E_MeV = gamma * m0;  // total relativistic energy
        }
        readback_energies_[slot] = mev_to_ebuf(E_MeV);

        write_spawn_genome(particles, slot, fire_type, rng, frame_counter_);
        any_spawned = true;
    }

    if (any_spawned) {
        compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
        compute.upload_dynamic_data(vk, particles);
        audio.play(AudioPlayer::SFX_COLLISION, frame_counter_);
        try_unlock(ACH_FIRST_ACCELERATOR);
        // Cosmic ray: fire at near-lightspeed
        if (speed >= 295.0f) try_unlock(ACH_COSMIC_RAY);
    }
}

// ── Spawn a single atom (nucleus + electrons) at world position ──────────────
uint32_t PhysicsSimulation::spawn_atom_at(glm::vec2 pos, int Z, int N,
                                          std::mt19937& rng, uint32_t& search_start) {
    uint32_t n = cfg.particle_count;

    auto find_dormant = [&](uint32_t start_from) -> uint32_t {
        for (uint32_t i = start_from; i < n; ++i) {
            if (readback_energies_[i] < 0.01f) return i;
        }
        return UINT32_MAX;
    };

    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    auto wrap_pos = [&](glm::vec2 p) -> glm::vec2 {
        p.x = std::fmod(p.x + rw, rw);
        p.y = std::fmod(p.y + rh, rh);
        return p;
    };

    if (N < 0) N = Z;
    int A = Z + N;

    const float NUC_SPACING = 3.8f;

    struct NucPos { float x, y; };
    std::vector<NucPos> nuc_positions;
    nuc_positions.reserve(A);

    uint32_t first_nucleon_slot = UINT32_MAX;

    nuc_positions.push_back({0.0f, 0.0f});
    int ring = 1;
    while (static_cast<int>(nuc_positions.size()) < A) {
        for (int side = 0; side < 6 && static_cast<int>(nuc_positions.size()) < A; ++side) {
            for (int step = 0; step < ring && static_cast<int>(nuc_positions.size()) < A; ++step) {
                float angle0 = 3.14159265f / 3.0f * side + 3.14159265f / 2.0f;
                float angle1 = 3.14159265f / 3.0f * (side + 1) + 3.14159265f / 2.0f;
                float t = static_cast<float>(step) / static_cast<float>(ring);
                float px = ring * NUC_SPACING * std::cos(angle0) * (1.0f - t)
                         + ring * NUC_SPACING * std::cos(angle1) * t;
                float py = ring * NUC_SPACING * std::sin(angle0) * (1.0f - t)
                         + ring * NUC_SPACING * std::sin(angle1) * t;
                nuc_positions.push_back({px, py});
            }
        }
        ++ring;
    }

    std::vector<uint32_t> nuc_types;
    nuc_types.reserve(A);
    int p_left = Z, n_left = N;
    for (int k = 0; k < A; ++k) {
        if (k % 2 == 0) {
            if (p_left > 0) { nuc_types.push_back(PROTON_TYPE); --p_left; }
            else            { nuc_types.push_back(NEUTRON_TYPE); --n_left; }
        } else {
            if (n_left > 0) { nuc_types.push_back(NEUTRON_TYPE); --n_left; }
            else            { nuc_types.push_back(PROTON_TYPE); --p_left; }
        }
    }

    // ── Force relaxation: find equilibrium nucleon positions ─────────────
    // Start from hex-packed positions and iteratively move each nucleon
    // along the net Yukawa + Pauli + Coulomb force until forces balance.
    // This matches the shader constants exactly so nucleons spawn at rest.
    if (A > 1) {
        constexpr float YK_RANGE     = 16.0f;
        constexpr float YK_COUPLING  = 2500.0f;
        constexpr float YK_INV_RANGE = 1.0f / YK_RANGE;
        constexpr float PL_CORE      = 6.0f;
        constexpr float PL_STRENGTH  = 12000.0f;
        constexpr float KC           = 1200.0f;
        constexpr float SOFTEN_SQ    = 4.0f;   // SOFTEN_MIN² = 2²
        constexpr int   MAX_ITERS    = 80;
        constexpr float STEP_INIT    = 0.001f;
        constexpr float MAX_MOVE     = 1.5f;   // px per iteration cap

        for (int iter = 0; iter < MAX_ITERS; ++iter) {
            float alpha = STEP_INIT * (1.0f - 0.6f * static_cast<float>(iter) / MAX_ITERS);

            std::vector<NucPos> forces(A, {0.0f, 0.0f});

            for (int i = 0; i < A; ++i) {
                for (int j = i + 1; j < A; ++j) {
                    float dx = nuc_positions[j].x - nuc_positions[i].x;
                    float dy = nuc_positions[j].y - nuc_positions[i].y;
                    float d2 = dx * dx + dy * dy;
                    float dist = std::sqrt(d2 + 0.001f);
                    float inv_r = 1.0f / std::max(dist, 0.5f);
                    float nx = dx / dist;
                    float ny = dy / dist;

                    // Force on i from j (positive component = toward j)
                    float fx = 0.0f, fy = 0.0f;

                    // Yukawa nuclear attraction (toward j)
                    if (dist < YK_RANGE) {
                        float yukawa = YK_COUPLING * std::exp(-dist * YK_INV_RANGE)
                                     * (inv_r + YK_INV_RANGE) * inv_r;
                        float t = std::clamp((dist - 12.0f) / 4.0f, 0.0f, 1.0f);
                        float window = 1.0f - t * t * (3.0f - 2.0f * t);
                        fx += nx * yukawa * window;
                        fy += ny * yukawa * window;

                        // Pauli exclusion repulsion (away from j)
                        if (dist < PL_CORE) {
                            float overlap = 1.0f - dist / PL_CORE;
                            float pauli = PL_STRENGTH * overlap * overlap / (d2 + 0.05f);
                            fx -= nx * pauli;
                            fy -= ny * pauli;
                        }
                    }

                    // Coulomb repulsion between protons (away from j)
                    if (nuc_types[i] == PROTON_TYPE && nuc_types[j] == PROTON_TYPE) {
                        float coulomb = KC / (d2 + SOFTEN_SQ);
                        fx -= nx * coulomb;
                        fy -= ny * coulomb;
                    }

                    // Newton's third law
                    forces[i].x += fx;  forces[i].y += fy;
                    forces[j].x -= fx;  forces[j].y -= fy;
                }
            }

            // Move nucleons along force direction
            float max_disp = 0.0f;
            for (int i = 0; i < A; ++i) {
                float mx = forces[i].x * alpha;
                float my = forces[i].y * alpha;
                float mag = std::sqrt(mx * mx + my * my);
                if (mag > MAX_MOVE) {
                    mx *= MAX_MOVE / mag;
                    my *= MAX_MOVE / mag;
                    mag = MAX_MOVE;
                }
                nuc_positions[i].x += mx;
                nuc_positions[i].y += my;
                max_disp = std::max(max_disp, mag);
            }

            // Re-center nucleus at origin
            float cx = 0.0f, cy = 0.0f;
            for (int i = 0; i < A; ++i) { cx += nuc_positions[i].x; cy += nuc_positions[i].y; }
            cx /= A; cy /= A;
            for (int i = 0; i < A; ++i) { nuc_positions[i].x -= cx; nuc_positions[i].y -= cy; }

            if (max_disp < 0.005f) break;  // converged
        }
    }

    float nuc_extent = 0.0f;
    for (int k = 0; k < A; ++k) {
        float r2 = nuc_positions[k].x * nuc_positions[k].x
                  + nuc_positions[k].y * nuc_positions[k].y;
        nuc_extent = std::max(nuc_extent, std::sqrt(r2));
    }
    nuc_extent += NUC_SPACING * 0.5f;

    for (int k = 0; k < A; ++k) {
        uint32_t slot = find_dormant(search_start);
        if (slot == UINT32_MAX) break;
        search_start = slot + 1;

        if (first_nucleon_slot == UINT32_MAX) first_nucleon_slot = slot;

        readback_positions_[slot] = wrap_pos(pos + glm::vec2(nuc_positions[k].x, nuc_positions[k].y));
        readback_velocities_[slot] = glm::vec2(0.0f);
        readback_energies_[slot] = mev_to_ebuf(PHYS_REST_MASS_MEV[nuc_types[k]]);

        write_spawn_genome(particles, slot, nuc_types[k], rng, frame_counter_);

        // Set orbital_parent to the first nucleon (nucleus rep) immediately so
        // the same-nucleus guard in check_fusion() works before update_orbitals() runs
        particles.orbital_parent[slot] = static_cast<int32_t>(first_nucleon_slot);
    }

    const float NUC_CLEAR = nuc_extent + 4.0f;

    int electrons_left = Z;
    int shell_fill_spawn[4] = {0, 0, 0, 0};
    for (int shell = 0; shell < 4 && electrons_left > 0; ++shell) {
        int cap = std::min(SHELL_CAP_O[shell], electrons_left);
        float n_shell = static_cast<float>(shell + 1);

        int inner_e = 0;
        for (int s = 0; s < shell; ++s)
            inner_e += shell_fill_spawn[s];

        float Z_eff = std::max(1.0f, static_cast<float>(Z) - static_cast<float>(inner_e));
        float R_bohr = n_shell * n_shell * R_BOHR / Z_eff;
        float R_target = std::max(R_bohr, 8.0f);
        R_target = std::max(R_target, NUC_CLEAR + shell * 8.0f);

        float R3 = R_target * R_target * R_target;
        float R2_soft = R_target * R_target + SOFTEN_SQ_O;
        float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);

        // The centrifugal barrier (L²/r³) is always active as the quantum ground state.
        // When orbital_drive ON: use boosted velocity (tangential drive + F_bind maintain it).
        // When orbital_drive OFF: barrier ≈ Coulomb at R_target, so equilibrium v ≈ 0.
        //   Give a small tangential velocity for visual rotation; the barrier prevents collapse.
        float v_keplerian = L_ground / R_target;  // sqrt(K·Z·R/(R²+s))
        float v_orbital;
        if (iface.orbital_drive) {
            v_orbital = v_keplerian * cfg.orbit_boost[shell];
        } else {
            // Barrier absorbs most of Coulomb — spawn at ~30% Keplerian for visual orbit.
            // The barrier will keep the electron from falling below R_target.
            v_orbital = v_keplerian * 0.3f;
        }
        float orbit_boost = iface.orbital_drive ? cfg.orbit_boost[shell] : 1.0f;

        // Stagger shells by configurable offset for cloud appearance
        float shell_offset = shell * cfg.orbital_shell_offset;

        for (int e = 0; e < cap; ++e) {
            uint32_t slot = find_dormant(search_start);
            if (slot == UINT32_MAX) break;
            search_start = slot + 1;

            float angle = shell_offset + 2.0f * 3.14159265f * static_cast<float>(e) / static_cast<float>(cap);
            glm::vec2 offset(R_target * std::cos(angle), R_target * std::sin(angle));
            glm::vec2 tangent(-std::sin(angle), std::cos(angle));

            readback_positions_[slot] = wrap_pos(pos + offset);
            readback_velocities_[slot] = tangent * v_orbital;

            float e_speed = v_orbital;
            float e_beta = std::min(e_speed / C_SIM, 0.9999f);
            float e_gamma = 1.0f / std::sqrt(1.0f - e_beta * e_beta);
            readback_energies_[slot] = mev_to_ebuf(e_gamma * PHYS_REST_MASS_MEV[ELECTRON_TYPE_PHYS]);

            write_spawn_genome(particles, slot, ELECTRON_TYPE_PHYS, rng, frame_counter_);
            particles.genomes[slot * GENOME_SIZE + 2] = L_ground;
            particles.genomes[slot * GENOME_SIZE + 3] = orbit_boost;  // stored for shader
            if (slot < particles.orbital_shell.size()) {
                particles.orbital_shell[slot] = static_cast<int8_t>(shell);
                particles.excitation_timer[slot] = 0;
            }
        }

        shell_fill_spawn[shell] = cap;
        electrons_left -= cap;
    }

    return first_nucleon_slot;
}

// ── Spawn at world position ──────────────────────────────────────────────────
void PhysicsSimulation::do_spawn_at_world(glm::vec2 world_pos) {
    if (!compute.is_ready()) return;

    uint32_t n = cfg.particle_count;
    if (readback_positions_.size() != n) {
        readback_positions_.resize(n);
        readback_velocities_.resize(n);
        readback_energies_.resize(n);
    }
    compute.read_current_state(vk, readback_positions_, readback_velocities_, readback_energies_);

    uint32_t type = static_cast<uint32_t>(iface.spawn_type);
    audio.play(AudioPlayer::SFX_SPAWN, frame_counter_);

    int count = iface.spawn_count;
    float scatter = iface.spawn_scatter;
    std::mt19937 rng(static_cast<uint32_t>(world_pos.x * 1000.0f + world_pos.y));
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // ── Helper: wrap position into toroidal world ─────────────────────────
    float rw = static_cast<float>(WORLD_W);
    float rh = static_cast<float>(WORLD_H);
    auto wrap_pos = [&](glm::vec2 p) -> glm::vec2 {
        p.x = std::fmod(p.x + rw, rw);
        p.y = std::fmod(p.y + rh, rh);
        return p;
    };

    // ── Molecule spawn ───────────────────────────────────────────────────
    if (iface.spawn_molecule_idx >= 0 &&
        iface.spawn_molecule_idx < MOLECULE_TEMPLATE_COUNT) {
        const auto& mol = MOLECULE_TEMPLATES[iface.spawn_molecule_idx];
        uint32_t search_start = 0;

        // Nuclear extent: radius of hex-packed nucleus (NUC_SPACING=3.8)
        auto nuc_extent = [](int Z) -> float {
            int N_def = (Z <= 2) ? Z : Z + static_cast<int>(std::round(Z * 0.31f));
            int A = Z + N_def;
            if (A <= 0) return 0.0f;
            int ring = 0, placed = 1;
            while (placed < A) { ++ring; placed += 6 * ring; }
            return ring * 3.8f + 1.9f;
        };

        // Scale template so no bonded atom pair overlaps within nuclear force range.
        // Templates designed with ~22px spacing; we need at least:
        //   actual_dist >= extent_A + extent_B + SAFE_MARGIN
        // where SAFE_MARGIN = YUKAWA_RANGE(8) + 2px buffer = 10
        const float TEMPLATE_SPACING = 22.0f;
        const float SAFE_MARGIN = 10.0f;  // keep nucleons outside Yukawa range
        float min_scale = cfg.bond_rest_length / TEMPLATE_SPACING;
        for (uint32_t ai = 0; ai < mol.atom_count; ++ai) {
            for (uint32_t aj = ai + 1; aj < mol.atom_count; ++aj) {
                float dx = mol.atoms[ai].dx - mol.atoms[aj].dx;
                float dy = mol.atoms[ai].dy - mol.atoms[aj].dy;
                float tdist = std::sqrt(dx * dx + dy * dy);
                if (tdist < 1.0f) continue;
                float safe = nuc_extent(mol.atoms[ai].Z) + nuc_extent(mol.atoms[aj].Z)
                           + SAFE_MARGIN;
                min_scale = std::max(min_scale, safe / tdist);
            }
        }
        float scale = min_scale;

        // Track spawned atom reps for pre-bonding
        struct SpawnedAtom { uint32_t rep; glm::vec2 pos; int Z; };
        std::vector<SpawnedAtom> spawned_atoms;
        spawned_atoms.reserve(mol.atom_count);

        for (uint32_t ai = 0; ai < mol.atom_count; ++ai) {
            int Z = mol.atoms[ai].Z;
            int N = default_neutron_count(Z);
            glm::vec2 atom_pos = wrap_pos(world_pos +
                glm::vec2(mol.atoms[ai].dx * scale, mol.atoms[ai].dy * scale));
            uint32_t rep = spawn_atom_at(atom_pos, Z, N, rng, search_start);
            if (rep != UINT32_MAX)
                spawned_atoms.push_back({rep, atom_pos, Z});
        }

        // Pre-create covalent bonds between nearby atoms (before GPU forces scatter them)
        if (cfg.bonds_enabled && spawned_atoms.size() > 1) {
            size_t required = static_cast<size_t>(cfg.particle_count) * MAX_BONDS_PER_PARTICLE;
            if (bond_data_.size() < required)
                bond_data_.resize(required, 0xFFFFFFFFu);

            const float FORM_R_SQ = cfg.bond_form_radius * cfg.bond_form_radius;

            // Count existing bonds for a rep
            auto count_bonds = [&](uint32_t rep) -> int {
                int cnt = 0;
                uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s)
                    if (bond_data_[base + s] != 0xFFFFFFFFu) ++cnt;
                return cnt;
            };

            // Add one bond slot (returns true if added)
            auto add_slot = [&](uint32_t rep, uint32_t partner) -> bool {
                uint32_t base = rep * MAX_BONDS_PER_PARTICLE;
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                    if (bond_data_[base + s] == 0xFFFFFFFFu) {
                        bond_data_[base + s] = partner;
                        return true;
                    }
                }
                return false;
            };

            // Sort atoms by valence (highest first) so carbons bond first
            std::vector<size_t> order(spawned_atoms.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return valence_from_Z(spawned_atoms[a].Z) > valence_from_Z(spawned_atoms[b].Z);
            });

            for (size_t ii = 0; ii < order.size(); ++ii) {
                size_t i = order[ii];
                int val_a = valence_from_Z(spawned_atoms[i].Z);
                if (val_a <= 0) continue;
                if (count_bonds(spawned_atoms[i].rep) >= val_a) continue;

                // Collect candidate partners sorted by distance
                struct Cand { size_t idx; float d2; };
                std::vector<Cand> cands;
                for (size_t j = 0; j < spawned_atoms.size(); ++j) {
                    if (j == i) continue;
                    int val_b = valence_from_Z(spawned_atoms[j].Z);
                    if (val_b <= 0) continue;
                    glm::vec2 delta = spawned_atoms[j].pos - spawned_atoms[i].pos;
                    float d2 = glm::dot(delta, delta);
                    if (d2 <= FORM_R_SQ)
                        cands.push_back({j, d2});
                }
                std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
                    return a.d2 < b.d2;
                });

                for (auto& c : cands) {
                    if (count_bonds(spawned_atoms[i].rep) >= val_a) break;
                    int val_b = valence_from_Z(spawned_atoms[c.idx].Z);
                    if (count_bonds(spawned_atoms[c.idx].rep) >= val_b) continue;

                    // Create bidirectional bond
                    if (add_slot(spawned_atoms[i].rep, spawned_atoms[c.idx].rep))
                        add_slot(spawned_atoms[c.idx].rep, spawned_atoms[i].rep);
                }
            }

            // Upload bond data for GPU
            particles.bond_partners_ptr = bond_data_.data();
            particles.bond_partners_count = static_cast<uint32_t>(bond_data_.size());
        }

        iface.spawn_molecule_idx = -1;
    }

    // ── Dynamic atom spawn (periodic table) ───────────────────────────────
    else if (iface.spawn_atom_Z > 0) {
        uint32_t search_start = 0;
        spawn_atom_at(world_pos, iface.spawn_atom_Z, iface.spawn_atom_N, rng, search_start);
    }

    // ── Group template spawn ──────────────────────────────────────────────
    else {
    const GroupTemplate* resolved_tmpl = nullptr;
    if (iface.spawn_group >= 0 && iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &GROUP_TEMPLATES[iface.spawn_group];
    } else if (iface.spawn_group >= GROUP_TEMPLATE_COUNT_VAL &&
               iface.spawn_group < GROUP_TEMPLATE_COUNT_VAL + HADRON_TEMPLATE_COUNT_VAL) {
        resolved_tmpl = &HADRON_TEMPLATES[iface.spawn_group - GROUP_TEMPLATE_COUNT_VAL];
    }

    if (resolved_tmpl) {
        const auto& tmpl = *resolved_tmpl;

        // Find nucleus center and count protons for Z
        glm::vec2 nucleus_center(0.0f);
        int nucleon_count = 0;
        int template_Z = 0;
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t t = tmpl.atoms[a].type;
            if (t == PROTON_TYPE || t == NEUTRON_TYPE || t == ANTIPROTON_TYPE_PHYS) {
                nucleus_center += glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy);
                nucleon_count++;
                if (t == PROTON_TYPE) template_Z++;
            }
        }
        if (nucleon_count > 0) nucleus_center /= static_cast<float>(nucleon_count);

        // Collect electron offsets sorted by distance (for shell assignment)
        struct ElectronEntry { float dx, dy, dist; uint32_t type; };
        std::vector<ElectronEntry> electron_entries;
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t t = tmpl.atoms[a].type;
            if (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS) {
                glm::vec2 d = glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy) - nucleus_center;
                electron_entries.push_back({tmpl.atoms[a].dx, tmpl.atoms[a].dy,
                                            glm::length(d), t});
            }
        }
        std::sort(electron_entries.begin(), electron_entries.end(),
            [](const ElectronEntry& a, const ElectronEntry& b) { return a.dist < b.dist; });

        // Pre-compute shell assignments for electrons
        int shell_fill[4] = {0, 0, 0, 0};
        struct ShellInfo { float L_ground; int shell; float boost; };
        std::vector<ShellInfo> electron_shells;
        for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
            int shell = -1;
            for (int s = 0; s < 4; ++s) {
                if (shell_fill[s] < SHELL_CAP_O[s]) { shell = s; break; }
            }
            if (shell < 0) {
                float fallback_boost = iface.orbital_drive ? cfg.orbit_boost[0] : 1.0f;
                electron_shells.push_back({120.0f, 0, fallback_boost});
                continue;
            }
            shell_fill[shell]++;
            int inner = 0;
            for (int s = 0; s < shell; ++s) inner += shell_fill[s];
            float Z_eff = std::max(1.0f, static_cast<float>(template_Z - inner));
            float n_shell = static_cast<float>(shell + 1);
            float R_target = n_shell * n_shell * R_BOHR / Z_eff;
            R_target = std::max(R_target, 8.0f);
            float R3 = R_target * R_target * R_target;
            float R2_soft = R_target * R_target + SOFTEN_SQ_O;
            float L_ground = std::sqrt(Z_eff * K_COULOMB_O * R3 / R2_soft);
            float boost = iface.orbital_drive ? cfg.orbit_boost[shell] : 1.0f;
            electron_shells.push_back({L_ground, shell, boost});
        }

        // Spawn all particles
        for (uint32_t a = 0; a < tmpl.count; ++a) {
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) {
                if (readback_energies_[i] < 0.01f) { slot = i; break; }
            }
            if (slot == UINT32_MAX) break;

            readback_positions_[slot] = wrap_pos(
                world_pos + glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy));

            uint32_t t = tmpl.atoms[a].type;
            bool is_electron = (t == ELECTRON_TYPE_PHYS || t == POSITRON_TYPE_PHYS);
            float part_speed = 0.0f;

            if (is_electron && nucleon_count > 0) {
                glm::vec2 to_electron = glm::vec2(tmpl.atoms[a].dx, tmpl.atoms[a].dy)
                                      - nucleus_center;
                float r = glm::length(to_electron);
                if (r > 1.0f) {
                    // Find this electron's shell info
                    float L_g = 120.0f;
                    float e_boost = 1.0f;
                    for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
                        if (std::abs(electron_entries[ei].dx - tmpl.atoms[a].dx) < 0.1f &&
                            std::abs(electron_entries[ei].dy - tmpl.atoms[a].dy) < 0.1f) {
                            L_g = electron_shells[ei].L_ground;
                            e_boost = electron_shells[ei].boost;
                            break;
                        }
                    }
                    float v_kep = L_g / std::max(r, 3.0f);
                    // Barrier always active: with drive ON use full boost, OFF use 30% Keplerian
                    float v_orbital = iface.orbital_drive ? v_kep * e_boost : v_kep * 0.3f;
                    glm::vec2 radial = to_electron / r;
                    glm::vec2 tangent(-radial.y, radial.x);
                    readback_velocities_[slot] = tangent * v_orbital;
                    part_speed = v_orbital;
                } else {
                    readback_velocities_[slot] = glm::vec2(0.0f);
                }
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            // Energy from rest mass + kinetic energy
            float m0_t = (t < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[t] : 0.0f;
            if (m0_t < 0.001f) {
                readback_energies_[slot] = mev_to_ebuf(std::max(part_speed / C_SIM, 0.1f));
            } else {
                float beta_t = std::min(part_speed / C_SIM, 0.9999f);
                float gamma_t = 1.0f / std::sqrt(1.0f - beta_t * beta_t);
                readback_energies_[slot] = mev_to_ebuf(gamma_t * m0_t);
            }

            write_spawn_genome(particles, slot, t, rng, frame_counter_);

            // Write L_ground + boost + shell to genome for electrons
            if (is_electron && nucleon_count > 0) {
                for (size_t ei = 0; ei < electron_entries.size(); ++ei) {
                    if (std::abs(electron_entries[ei].dx - tmpl.atoms[a].dx) < 0.1f &&
                        std::abs(electron_entries[ei].dy - tmpl.atoms[a].dy) < 0.1f) {
                        particles.genomes[slot * GENOME_SIZE + 2] = electron_shells[ei].L_ground;
                        particles.genomes[slot * GENOME_SIZE + 3] = electron_shells[ei].boost;
                        if (slot < particles.orbital_shell.size()) {
                            particles.orbital_shell[slot] = static_cast<int8_t>(electron_shells[ei].shell);
                            particles.excitation_timer[slot] = 0;
                        }
                        break;
                    }
                }
            }
        }
    } else {
        // Single particle spawn
        bool is_massless = (particles.behavior_flags[type] & (BEHAVIOR_PHOTON | BEHAVIOR_GLUON)) != 0;
        float m0 = (type < PHYS_PARTICLE_TYPES) ? PHYS_REST_MASS_MEV[type] : 0.0f;
        float KE_MeV = iface.spawn_energy_mev;
        // Convert kinetic energy → sim velocity
        float spawn_speed = (is_massless) ? C_SIM : ke_to_speed(KE_MeV, type);

        for (int c = 0; c < count; ++c) {
            uint32_t slot = UINT32_MAX;
            for (uint32_t i = 0; i < n; ++i) {
                if (readback_energies_[i] < 0.01f) { slot = i; break; }
            }
            if (slot == UINT32_MAX) break;

            glm::vec2 offset(0.0f);
            if (count > 1)
                offset = glm::vec2(gauss(rng) * scatter, gauss(rng) * scatter);

            readback_positions_[slot] = wrap_pos(world_pos + offset);

            // Velocity: random direction at KE-derived speed (massless always at c)
            if (spawn_speed > 0.01f) {
                float angle = angle_dist_(rng);
                glm::vec2 dir(std::cos(angle), std::sin(angle));
                readback_velocities_[slot] = dir * spawn_speed;
            } else {
                readback_velocities_[slot] = glm::vec2(0.0f);
            }

            // Energy buffer: rest mass + kinetic energy
            // Massless particles (photon, graviton, gluon) always travel at c — need non-zero energy
            if (m0 < 0.001f) {
                readback_energies_[slot] = mev_to_ebuf(std::max(KE_MeV, 1.0f));  // minimum 1 MeV for massless
            } else {
                readback_energies_[slot] = mev_to_ebuf(m0 + KE_MeV);
            }

            write_spawn_genome(particles, slot, type, rng, frame_counter_);
        }
    }
    }  // end outer else (spawn_atom_Z not active)

    vkDeviceWaitIdle(vk.device);
    compute.write_particle_state(vk, readback_positions_, readback_velocities_, readback_energies_);
    compute.upload_dynamic_data(vk, particles);
}
