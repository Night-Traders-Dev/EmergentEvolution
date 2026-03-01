#include "physics/interface.h"
#include "physics/phys_particles.h"
#include "physics/ui_data.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ── Detail cards ─────────────────────────────────────────────────────────────
// Split from interface.cpp: particle info card, element card, molecule card.

void PhysicsInterface::draw_info_card(const Particles& particles) {
    // Show pinned selection, or hover preview
    bool pinned = (selected_particle_idx >= 0);
    int32_t show_idx = pinned ? selected_particle_idx : hover_particle_idx;
    if (show_idx < 0) return;
    uint32_t idx = static_cast<uint32_t>(show_idx);
    if (idx >= particles.types.size()) {
        if (pinned) selected_particle_idx = -1;
        return;
    }

    uint32_t ptype = particles.types[idx];
    const char* name = (ptype < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_NAMES[ptype] : "Unknown";

    float charge = 0.0f, spin = 0.0f, color_charge = 0.0f, decay_rate = 0.0f;
    if (idx * GENOME_SIZE + 3 < particles.genomes.size()) {
        charge       = particles.genomes[idx * GENOME_SIZE + 0];
        spin         = particles.genomes[idx * GENOME_SIZE + 1];
        color_charge = particles.genomes[idx * GENOME_SIZE + 2];
        decay_rate   = particles.genomes[idx * GENOME_SIZE + 3];
    }

    ImGuiIO& io = ImGui::GetIO();
    // Bottom-right default position — user can drag elsewhere
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, io.DisplaySize.y - 60),
                            ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));

    ImGuiWindowFlags card_flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_Appearing);

    if (ImGui::Begin("##InfoCard", nullptr, card_flags)) {
        // Particle name with color
        ImVec4 pcolor = (ptype < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_UI_COLORS[ptype] : ImVec4(1,1,1,1);
        ImGui::TextColored(pcolor, "%s", name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "#%u", idx);
        if (pinned) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.302f, 0.749f, 0.953f, 0.6f), "(selected)");
        }

        ImGui::Separator();

        // Two-column layout
        float col_w = 80.0f;

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Charge");
        ImGui::SameLine(col_w);
        ImGui::Text("%+.2f", charge);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Spin");
        ImGui::SameLine(col_w);
        ImGui::Text("%+.1f", spin);

        // Color charge for quarks
        if (ptype >= UP_QUARK_TYPE && ptype <= ANTI_BOTTOM_TYPE) {
            int cc = static_cast<int>(color_charge);
            const char* color_name = "?";
            if (cc == 1 || cc == -1)  color_name = (cc > 0) ? "Red" : "Anti-Red";
            if (cc == 2 || cc == -2)  color_name = (cc > 0) ? "Green" : "Anti-Green";
            if (cc == 3 || cc == -3)  color_name = (cc > 0) ? "Blue" : "Anti-Blue";
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Color");
            ImGui::SameLine(col_w);
            ImGui::Text("%s", color_name);
        }

        // Mass tier
        if (ptype < PHYS_PARTICLE_TYPES) {
            const char* mass_name = "light";
            uint32_t bhv = (ptype < MAX_PARTICLE_TYPES) ? particles.behavior_flags[ptype] : 0;
            if (bhv & BEHAVIOR_PHOTON)      mass_name = "massless";
            if (bhv & BEHAVIOR_NEUTRINO)    mass_name = "~massless";
            if (bhv & BEHAVIOR_MASS_MEDIUM) mass_name = "medium";
            if (bhv & BEHAVIOR_MASS_HEAVY)  mass_name = "heavy";
            if (bhv & BEHAVIOR_MASS_DENSE)  mass_name = "dense";
            if (bhv & BEHAVIOR_MASS_ULTRA)  mass_name = "ultra-heavy";
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mass");
            ImGui::SameLine(col_w);
            ImGui::Text("%s", mass_name);
        }

        if (decay_rate > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Decay");
            ImGui::SameLine(col_w);
            ImGui::Text("%.3f", decay_rate);
        }

        // ── Age ──────────────────────────────────────────────────────────
        if (idx < particles.birth_frames.size()) {
            uint32_t age_frames = frame_counter_display - particles.birth_frames[idx];
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f)
                ImGui::Text("%.1f s", age_sec);
            else
                ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // ── Speed, Momentum, Energy, Temperature, Magnetic Moment ──
        if (readback_velocities && idx < readback_count) {
            glm::vec2 vel = readback_velocities[idx];
            float speed = glm::length(vel);
            float beta = std::min(speed / C_SIM, 0.9999f);

            // Speed — real-world units
            {
                char spd_buf[32];
                fmt_speed(spd_buf, sizeof(spd_buf), speed);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", spd_buf);
            }

            // Momentum — relativistic p = γm₀βc (MeV/c)
            {
                char mom_buf[32];
                fmt_momentum(mom_buf, sizeof(mom_buf), speed, ptype);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", mom_buf);
            }

            // Kinetic energy — KE = (γ-1)m₀c² for massive, E = pc for massless
            {
                float m0 = rest_mass_MeV(ptype);
                float KE_MeV;
                if (m0 < 0.001f) {
                    KE_MeV = beta * 1.0f;  // massless: all energy is kinetic
                    if (KE_MeV < 0.001f) KE_MeV = 0.0f;
                } else {
                    float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                    KE_MeV = (gamma - 1.0f) * m0;
                }

                char e_buf[32];
                fmt_energy_ev(e_buf, sizeof(e_buf), KE_MeV);
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
                ImGui::SameLine(col_w);
                ImGui::Text("%s", e_buf);
            }

            // Temperature — from kinetic energy
            {
                // Sim mass for KE-based temp (keep existing sim-unit mass for thermal calc)
                auto get_sim_mass = [](uint32_t t) -> float {
                    if (t <= 1 || t == 5) return 40.0f;
                    if (t == 2 || t == 4) return 1.0f;
                    if (t == 7 || t == 8) return 200.0f;
                    if (t == 9 || t == 10) return 3333.0f;
                    if (t == 6 || t == 11 || t == 12) return 0.01f;
                    if (t == 3 || t == 25) return 0.01f;
                    return 1.0f;
                };
                float sim_mass = get_sim_mass(ptype);
                float ke = 0.5f * sim_mass * speed * speed;
                float particle_temp = ke * 0.1f;
                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Temp");
                ImGui::SameLine(col_w);
                if (particle_temp < 1000.0f)
                    ImGui::Text("%.1f K", particle_temp);
                else
                    ImGui::Text("%.1fk K", particle_temp / 1000.0f);
            }

            // ── Magnetic moment (intrinsic) ──────────────────────────────
            // Nucleons: anomalous moments from quark substructure
            // Leptons: Dirac g≈2 (accurate to 0.1%)
            {
                float mu_val = 0.0f;
                const char* mu_unit = "";
                if (ptype == PROTON_TYPE) {
                    mu_val = 2.793f * spin;
                    mu_unit = "\xCE\xBC\x4E";  // μN
                } else if (ptype == ANTIPROTON_TYPE_PHYS) {
                    mu_val = -2.793f * spin;
                    mu_unit = "\xCE\xBC\x4E";
                } else if (ptype == NEUTRON_TYPE) {
                    mu_val = -1.913f * spin;
                    mu_unit = "\xCE\xBC\x4E";
                } else if (std::abs(charge) > 0.01f && std::abs(spin) > 0.01f) {
                    // Dirac g=2: μ = q·S (in Bohr magnetons for leptons, μN for quarks)
                    bool is_lepton = (ptype == ELECTRON_TYPE_PHYS || ptype == POSITRON_TYPE_PHYS ||
                                      ptype == MUON_TYPE_PHYS || ptype == ANTIMUON_TYPE_PHYS ||
                                      ptype == TAU_TYPE_PHYS || ptype == ANTITAU_TYPE_PHYS);
                    if (is_lepton) {
                        mu_val = charge * spin;  // in Bohr magnetons
                        mu_unit = "\xCE\xBC\x42";  // μB
                    } else {
                        mu_val = charge * spin;  // in natural units
                        mu_unit = "\xCE\xBC";
                    }
                }
                if (std::abs(mu_val) > 0.001f) {
                    ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
                    ImGui::SameLine(col_w);
                    ImGui::Text("%+.3f %s", mu_val, mu_unit);
                }
            }
        }

        // ── Orbital relationship ─────────────────────────────────────────
        if (idx < particles.orbital_parent.size()) {
            int32_t parent = particles.orbital_parent[idx];
            if (parent >= 0 && parent != static_cast<int32_t>(idx) &&
                static_cast<uint32_t>(parent) < particles.types.size()) {
                uint32_t parent_type = particles.types[parent];
                const char* parent_name = (parent_type < PHYS_PARTICLE_TYPES)
                    ? PHYS_TYPE_NAMES[parent_type] : "Unknown";
                ImVec4 parent_color = (parent_type < PHYS_PARTICLE_TYPES)
                    ? PHYS_TYPE_UI_COLORS[parent_type] : ImVec4(1,1,1,1);

                // Determine relationship label
                bool is_nucleon = (ptype <= 1 || ptype == 5);
                const char* relation = is_nucleon ? "Bound to" : "Orbiting";

                ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", relation);
                ImGui::SameLine(col_w);
                ImGui::PushStyleColor(ImGuiCol_Text, parent_color);
                char btn_label[64];
                snprintf(btn_label, sizeof(btn_label), "%s #%d", parent_name, parent);
                if (ImGui::SmallButton(btn_label)) {
                    navigate_to_particle = parent;
                }
                ImGui::PopStyleColor();

                // Show orbital shell info for bound electrons/positrons
                if ((ptype == ELECTRON_TYPE_PHYS || ptype == POSITRON_TYPE_PHYS) &&
                    idx < particles.orbital_shell.size()) {
                    int8_t sh = particles.orbital_shell[idx];
                    if (sh >= 0 && sh <= 3) {
                        static const char* shell_names[] = {"1s", "2sp", "3spd", "4spdf"};
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Shell");
                        ImGui::SameLine(col_w);
                        ImGui::Text("%s", shell_names[sh]);
                        if (idx < particles.excitation_timer.size() && particles.excitation_timer[idx] > 0) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                                "(excited %u frames)", particles.excitation_timer[idx]);
                        }
                    }
                }
            }
        }

        // ── Bond partners ────────────────────────────────────────────────
        if (particles.bond_partners_ptr && idx < particles.bond_partners_count / MAX_BONDS_PER_PARTICLE) {
            bool has_bond = false;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                uint32_t partner = particles.bond_partners_ptr[idx * MAX_BONDS_PER_PARTICLE + s];
                if (partner != 0xFFFFFFFF && partner < particles.types.size()) {
                    if (!has_bond) {
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Bonds");
                        ImGui::SameLine(col_w);
                        has_bond = true;
                    } else {
                        ImGui::SameLine();
                    }
                    uint32_t bp_type = particles.types[partner];
                    ImVec4 bp_color = (bp_type < PHYS_PARTICLE_TYPES)
                        ? PHYS_TYPE_UI_COLORS[bp_type] : ImVec4(1,1,1,1);
                    const char* bp_label = (bp_type < PHYS_PARTICLE_TYPES)
                        ? PHYS_TYPE_LABELS[bp_type] : "?";
                    char bp_btn[32];
                    snprintf(bp_btn, sizeof(bp_btn), "%s##bp%u", bp_label, s);
                    ImGui::PushStyleColor(ImGuiCol_Text, bp_color);
                    if (ImGui::SmallButton(bp_btn)) {
                        navigate_to_particle = static_cast<int32_t>(partner);
                    }
                    ImGui::PopStyleColor();
                }
            }
        }

        // ── Element membership ─────────────────────────────────────────
        if (idx < particles.orbital_parent.size()) {
            // Find nucleus representative for this particle
            int32_t nuc_rep = -1;
            bool is_nucleon = (ptype == PROTON_TYPE || ptype == NEUTRON_TYPE || ptype == ANTIPROTON_TYPE_PHYS);
            bool is_lepton  = (ptype == ELECTRON_TYPE_PHYS || ptype == POSITRON_TYPE_PHYS);

            if (is_nucleon || is_lepton) {
                int32_t op = particles.orbital_parent[idx];
                if (op >= 0 && static_cast<uint32_t>(op) < particles.types.size()) {
                    nuc_rep = op;
                }
                // If this IS a nucleus rep (proton or antiproton with no parent)
                if (is_nucleon && nuc_rep < 0 && (ptype == PROTON_TYPE || ptype == ANTIPROTON_TYPE_PHYS)) {
                    nuc_rep = static_cast<int32_t>(idx);
                }
            }

            if (nuc_rep >= 0) {
                // Determine if this is an antimatter nucleus
                uint32_t rep_type = particles.types[static_cast<uint32_t>(nuc_rep)];
                bool is_anti = (rep_type == ANTIPROTON_TYPE_PHYS);

                // Count constituents for this nucleus
                int Z = 0, N_count = 0, lepton_count = 0;
                uint32_t n_total = static_cast<uint32_t>(particles.types.size());
                for (uint32_t pi = 0; pi < n_total; ++pi) {
                    if (particles.orbital_parent.size() <= pi) break;
                    int32_t their_parent = particles.orbital_parent[pi];
                    if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;
                    uint32_t pt = particles.types[pi];
                    if (is_anti) {
                        if (pt == ANTIPROTON_TYPE_PHYS) Z++;
                        else if (pt == NEUTRON_TYPE) N_count++;
                        else if (pt == POSITRON_TYPE_PHYS) lepton_count++;
                    } else {
                        if (pt == PROTON_TYPE) Z++;
                        else if (pt == NEUTRON_TYPE) N_count++;
                        else if (pt == ELECTRON_TYPE_PHYS) lepton_count++;
                    }
                }

                if (Z > 0 && Z <= FULL_ELEMENT_COUNT) {
                    int A = Z + N_count;
                    int net_charge = Z - lepton_count;

                    ImGui::Spacing();
                    ImGui::Separator();

                    // Element header with symbol
                    ImVec4 elem_header_color = is_anti
                        ? ImVec4(0.0f, 0.85f, 0.95f, 1.0f)   // cyan for antimatter
                        : ImVec4(0.302f, 0.749f, 0.953f, 1.0f);
                    ImGui::TextColored(elem_header_color, is_anti ? "Anti-Element" : "Element");
                    ImGui::SameLine(col_w);

                    // Clickable element button
                    char elem_label[96];
                    const char* prefix = is_anti ? "Anti-" : "";
                    if (net_charge == 0)
                        snprintf(elem_label, sizeof(elem_label), "%s%s-%d (%s%s)",
                                 prefix, ELEMENT_SYMBOLS[Z], A, prefix, ELEMENT_NAMES[Z]);
                    else
                        snprintf(elem_label, sizeof(elem_label), "%s%s-%d %s%d",
                                 prefix, ELEMENT_SYMBOLS[Z], A,
                                 net_charge > 0 ? "+" : "", net_charge);

                    ImVec4 elem_btn_color = is_anti
                        ? ImVec4(0.6f, 0.9f, 0.95f, 1.0f)   // cyan-tinted for antimatter
                        : ImVec4(0.9f, 0.85f, 0.6f, 1.0f);  // gold for matter
                    ImGui::PushStyleColor(ImGuiCol_Text, elem_btn_color);
                    if (ImGui::SmallButton(elem_label)) {
                        element_card_nucleus_rep = nuc_rep;
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click for %selement details", prefix);

                    // ── Molecule membership ───────────────────────────────
                    // Check if this atom's nucleus belongs to a molecule
                    for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
                        auto& mol = molecule_list[mi];
                        if (mol.atom_indices.size() < 2) continue;  // single atoms aren't molecules
                        bool found = false;
                        for (uint32_t ei : mol.atom_indices) {
                            if (ei < element_list.size() &&
                                static_cast<int32_t>(element_list[ei].rep) == nuc_rep) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) continue;

                        std::string mol_formula = build_molecular_formula(element_list, mol.atom_indices);
                        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Molecule");
                        ImGui::SameLine(col_w);

                        char mol_label[128];
                        snprintf(mol_label, sizeof(mol_label), "%s (%d atoms)",
                                 mol_formula.c_str(), static_cast<int>(mol.atom_indices.size()));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.65f, 1.0f, 1.0f));
                        if (ImGui::SmallButton(mol_label)) {
                            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
                            molecule_card_atom_rep = static_cast<int32_t>(first_rep);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Click for molecule details");
                        break;  // only one molecule per atom
                    }
                }
            }
        }

        // Action buttons (only when pinned/selected)
        if (pinned) {
            ImGui::Spacing();
            ImGui::Separator();

            if (particle_move_mode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
                if (ImGui::Button("Moving...", ImVec2(72, 26))) {
                    particle_move_mode = false;
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
            } else {
                if (ImGui::Button("Move", ImVec2(72, 26))) {
                    particle_move_mode = true;
                }
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
                if (ImGui::Button("Delete", ImVec2(72, 26))) {
                    request_delete_particle = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(72, 26))) {
                    selected_particle_idx = -1;
                    particle_move_mode = false;
                }
            }
        }
    }
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Element Detail Card ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_element_card(const Particles& particles) {
    int32_t nuc_rep = element_card_nucleus_rep;
    if (nuc_rep < 0 || static_cast<uint32_t>(nuc_rep) >= particles.types.size()) {
        element_card_nucleus_rep = -1;
        return;
    }

    // Determine if this is an antimatter nucleus
    uint32_t rep_type = particles.types[static_cast<uint32_t>(nuc_rep)];
    bool is_anti = (rep_type == ANTIPROTON_TYPE_PHYS);

    // Gather all particles belonging to this nucleus
    uint32_t n_total = static_cast<uint32_t>(particles.types.size());
    int Z = 0, N_count = 0, lepton_count = 0;
    std::vector<uint32_t> nucleon_indices;
    std::vector<uint32_t> lepton_indices;
    glm::vec2 sum_pos(0.0f);
    float total_energy_MeV = 0.0f;
    float total_momentum_MeV = 0.0f;  // sum of γm₀β per constituent (MeV/c)
    glm::vec2 com_vel(0.0f);          // center-of-mass velocity (sim units)
    float total_sim_mass = 0.0f;
    uint32_t oldest_birth = UINT32_MAX;
    float total_mu_N = 0.0f;  // net magnetic moment in nuclear magnetons

    for (uint32_t pi = 0; pi < n_total; ++pi) {
        if (pi >= particles.orbital_parent.size()) break;
        int32_t their_parent = particles.orbital_parent[pi];
        if (their_parent != nuc_rep && static_cast<int32_t>(pi) != nuc_rep) continue;

        uint32_t pt = particles.types[pi];
        bool counted = false;
        if (is_anti) {
            if (pt == ANTIPROTON_TYPE_PHYS) { Z++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == NEUTRON_TYPE)    { N_count++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == POSITRON_TYPE_PHYS) { lepton_count++; lepton_indices.push_back(pi); counted = true; }
        } else {
            if (pt == PROTON_TYPE)          { Z++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == NEUTRON_TYPE)    { N_count++; nucleon_indices.push_back(pi); counted = true; }
            else if (pt == ELECTRON_TYPE_PHYS) { lepton_count++; lepton_indices.push_back(pi); counted = true; }
        }
        if (!counted) continue;

        // Accumulate magnetic moment (in nuclear magnetons)
        if (pi * GENOME_SIZE + 1 < particles.genomes.size()) {
            float sp = particles.genomes[pi * GENOME_SIZE + 1];
            float ch = particles.genomes[pi * GENOME_SIZE + 0];
            if (pt == PROTON_TYPE)              total_mu_N += 2.793f * sp;
            else if (pt == ANTIPROTON_TYPE_PHYS) total_mu_N += -2.793f * sp;
            else if (pt == NEUTRON_TYPE)        total_mu_N += -1.913f * sp;
            else if (pt == ELECTRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;  // μ_B → μ_N
            else if (pt == POSITRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;
        }

        sum_pos += particles.positions[pi];

        if (readback_velocities && pi < readback_count) {
            // Sim-mass weighted velocity for CoM
            float sim_mass = (pt <= 1 || pt == 5) ? 40.0f : 1.0f;
            com_vel += readback_velocities[pi] * sim_mass;
            total_sim_mass += sim_mass;

            // Kinetic energy and momentum per constituent
            float m0 = rest_mass_MeV(pt);
            float spd = glm::length(readback_velocities[pi]);
            float beta = std::min(spd / C_SIM, 0.9999f);
            float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
            total_energy_MeV += (gamma - 1.0f) * m0;  // KE = (γ-1)m₀c²
            total_momentum_MeV += gamma * m0 * beta;   // p = γm₀βc (MeV/c)
        }

        if (pi < particles.birth_frames.size()) {
            if (particles.birth_frames[pi] < oldest_birth)
                oldest_birth = particles.birth_frames[pi];
        }
    }

    if (Z == 0) { element_card_nucleus_rep = -1; return; }

    int A = Z + N_count;
    int net_charge = Z - lepton_count;
    float total_mass = Z * 40.0f + N_count * 40.0f + lepton_count * 1.0f;
    // Center-of-mass speed for speed display
    float com_speed_sim = (total_sim_mass > 0.0f) ? glm::length(com_vel) / total_sim_mass : 0.0f;
    // Shell configuration string
    const int SHELL_CAP[] = {2, 8, 18, 32, 32, 18, 8};
    char shell_str[64] = {};
    {
        int remaining = lepton_count;
        int pos = 0;
        for (int s = 0; s < 7 && remaining > 0; ++s) {
            int in_shell = std::min(remaining, SHELL_CAP[s]);
            if (s > 0) shell_str[pos++] = '/';
            pos += snprintf(shell_str + pos, sizeof(shell_str) - pos, "%d", in_shell);
            remaining -= in_shell;
        }
    }

    // Window — bottom-right, to the left of info card
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 590, io.DisplaySize.y - 60),
                            ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoNav;

    bool open = true;
    char title[96];
    const char* title_prefix = is_anti ? "Anti-" : "";
    snprintf(title, sizeof(title), "%s%s (%s%s-%d)###ElementCard",
             title_prefix,
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "?",
             title_prefix,
             (Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?", A);

    // Antimatter: cyan-tinted title bar; matter: warm gold
    ImVec4 title_bg = is_anti ? ImVec4(0.02f, 0.10f, 0.12f, 0.95f) : ImVec4(0.12f, 0.10f, 0.05f, 0.95f);
    ImVec4 title_bg_active = is_anti ? ImVec4(0.04f, 0.16f, 0.20f, 0.95f) : ImVec4(0.20f, 0.16f, 0.06f, 0.95f);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, title_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, title_bg_active);

    if (ImGui::Begin(title, &open, flags)) {
        float col_w = 110.0f;

        // Big element symbol + name
        if (Z <= FULL_ELEMENT_COUNT) {
            ImVec4 sym_color = is_anti ? ImVec4(0.6f, 0.95f, 1.0f, 1.0f) : ImVec4(0.9f, 0.85f, 0.6f, 1.0f);
            ImVec4 name_color = is_anti ? ImVec4(0.4f, 0.85f, 0.9f, 1.0f) : ImVec4(0.8f, 0.75f, 0.5f, 1.0f);
            if (is_anti) {
                ImGui::TextColored(sym_color, "%s%s", title_prefix, ELEMENT_SYMBOLS[Z]);
                ImGui::SameLine();
                ImGui::TextColored(name_color, "Anti-%s", ELEMENT_NAMES[Z]);
            } else {
                ImGui::TextColored(sym_color, "%s", ELEMENT_SYMBOLS[Z]);
                ImGui::SameLine();
                ImGui::TextColored(name_color, "%s", ELEMENT_NAMES[Z]);
            }
        }
        if (is_anti) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 0.7f), "(antimatter)");
        }

        ImGui::Separator();

        // Composition
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), is_anti ? "Antiprotons" : "Atomic No.");
        ImGui::SameLine(col_w); ImGui::Text("Z = %d", Z);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Neutrons");
        ImGui::SameLine(col_w); ImGui::Text("N = %d", N_count);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mass No.");
        ImGui::SameLine(col_w); ImGui::Text("A = %d", A);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), is_anti ? "Positrons" : "Electrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", lepton_count);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Shells");
        ImGui::SameLine(col_w); ImGui::Text("%s", shell_str);

        ImGui::Spacing();
        ImGui::Separator();

        // Charge
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Net Charge");
        ImGui::SameLine(col_w);
        if (net_charge == 0)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "0 (neutral)");
        else if (net_charge > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%d (ion)", net_charge);
        else
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%d (ion)", net_charge);

        // Mass
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Mass");
        ImGui::SameLine(col_w); ImGui::Text("%.0f u", total_mass);

        // Particles
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Particles");
        ImGui::SameLine(col_w); ImGui::Text("%d", Z + N_count + lepton_count);

        // Speed — center-of-mass
        {
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), com_speed_sim);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
            ImGui::SameLine(col_w); ImGui::Text("%s", spd_buf);
        }

        // Momentum — sum of constituent relativistic momenta
        {
            char mom_buf[32];
            if (total_momentum_MeV < 0.001f)
                snprintf(mom_buf, sizeof(mom_buf), "~0");
            else if (total_momentum_MeV < 1.0f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f keV/c", total_momentum_MeV * 1e3f);
            else if (total_momentum_MeV < 1e3f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f MeV/c", total_momentum_MeV);
            else if (total_momentum_MeV < 1e6f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f GeV/c", total_momentum_MeV / 1e3f);
            else
                snprintf(mom_buf, sizeof(mom_buf), "%.2f TeV/c", total_momentum_MeV / 1e6f);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
            ImGui::SameLine(col_w); ImGui::Text("%s", mom_buf);
        }

        // Energy — relativistic total (sum of γm₀c² for all constituents)
        {
            char e_buf[32];
            fmt_energy_ev(e_buf, sizeof(e_buf), total_energy_MeV);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
            ImGui::SameLine(col_w); ImGui::Text("%s", e_buf);
        }

        // Age
        if (oldest_birth != UINT32_MAX) {
            uint32_t age_frames = frame_counter_display - oldest_birth;
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f) ImGui::Text("%.1f s", age_sec);
            else ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // Magnetic moment — net of all constituents (in nuclear magnetons)
        if (std::abs(total_mu_N) > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
            ImGui::SameLine(col_w);
            ImGui::Text("%+.2f \xCE\xBC\x4E", total_mu_N);
        }

        // Stability — look up isotope decay table
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Stability");
        ImGui::SameLine(col_w);
        {
            const IsotopeDecayEntry* iso = lookup_isotope_decay(Z, N_count);
            NuclearDecayMode dmode = NDECAY_NONE;
            float hl = 0.0f;
            if (iso) { dmode = iso->mode; hl = iso->half_life_frames; }
            else dmode = general_stability_rule(Z, N_count, hl);

            if (dmode == NDECAY_NONE) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Stable");
            } else {
                const char* mode_str = "?";
                switch (dmode) {
                    case NDECAY_ALPHA:            mode_str = "\xce\xb1"; break;
                    case NDECAY_BETA_MINUS:       mode_str = "\xce\xb2\xe2\x81\xbb"; break;
                    case NDECAY_BETA_PLUS:        mode_str = "\xce\xb2\xe2\x81\xba"; break;
                    case NDECAY_NEUTRON_EMISSION: mode_str = "n-emit"; break;
                    case NDECAY_PROTON_EMISSION:  mode_str = "p-emit"; break;
                    default: break;
                }
                float hl_sec = hl / 60.0f;
                ImVec4 color;
                if (hl < 12.0f) color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);       // instant — red
                else if (hl < 300.0f) color = ImVec4(1.0f, 0.5f, 0.2f, 1.0f);  // short — orange
                else if (hl < 7200.0f) color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); // medium — yellow
                else color = ImVec4(0.8f, 0.8f, 0.4f, 1.0f);                   // long — pale yellow

                if (hl_sec < 60.0f)
                    ImGui::TextColored(color, "%s  t\xc2\xbd=%.1fs", mode_str, hl_sec);
                else
                    ImGui::TextColored(color, "%s  t\xc2\xbd=%.1fmin", mode_str, hl_sec / 60.0f);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Clickable constituent list
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Nucleons:");
        ImGui::SameLine(col_w);
        for (size_t ni = 0; ni < nucleon_indices.size() && ni < 12; ++ni) {
            uint32_t pi = nucleon_indices[ni];
            uint32_t pt = particles.types[pi];
            ImVec4 c = (pt < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_UI_COLORS[pt] : ImVec4(1,1,1,1);
            const char* lb = (pt < PHYS_PARTICLE_TYPES) ? PHYS_TYPE_LABELS[pt] : "?";
            char btn[32];
            snprintf(btn, sizeof(btn), "%s##en%zu", lb, ni);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            if (ImGui::SmallButton(btn)) navigate_to_particle = static_cast<int32_t>(pi);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        if (nucleon_indices.size() > 12) ImGui::Text("+%zu", nucleon_indices.size() - 12);
        else ImGui::NewLine();

        if (!lepton_indices.empty()) {
            const char* lepton_label = is_anti ? "Positrons:" : "Electrons:";
            const char* lepton_btn_label = is_anti ? "e+" : "e-";
            uint32_t lepton_type = is_anti ? POSITRON_TYPE_PHYS : ELECTRON_TYPE_PHYS;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "%s", lepton_label);
            ImGui::SameLine(col_w);
            for (size_t ei = 0; ei < lepton_indices.size() && ei < 12; ++ei) {
                uint32_t pi = lepton_indices[ei];
                char btn[32];
                snprintf(btn, sizeof(btn), "%s##ee%zu", lepton_btn_label, ei);
                ImGui::PushStyleColor(ImGuiCol_Text, PHYS_TYPE_UI_COLORS[lepton_type]);
                if (ImGui::SmallButton(btn)) navigate_to_particle = static_cast<int32_t>(pi);
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            if (lepton_indices.size() > 12) ImGui::Text("+%zu", lepton_indices.size() - 12);
            else ImGui::NewLine();
        }

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (element_move_mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
            if (ImGui::Button("Moving...", ImVec2(86, 26))) {
                element_move_mode = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
        } else {
            if (ImGui::Button("Move", ImVec2(72, 26))) {
                element_move_mode = true;
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
            if (ImGui::Button("Delete", ImVec2(72, 26))) {
                request_element_delete = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.15f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.55f, 0.2f, 0.90f));
            if (ImGui::Button("Duplicate", ImVec2(86, 26))) {
                request_element_duplicate = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.50f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.40f, 0.65f, 0.90f));
            if (ImGui::Button("Export", ImVec2(72, 26))) {
                request_element_export = true;
                export_element_rep = nuc_rep;
            }
            ImGui::PopStyleColor(2);
        }

        // Navigate + Close
        if (ImGui::Button("Navigate", ImVec2(100, 26))) {
            navigate_to_particle = nuc_rep;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 26))) {
            element_card_nucleus_rep = -1;
            element_move_mode = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!open) element_card_nucleus_rep = -1;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Molecule Detail Card ────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_molecule_card(const Particles& particles) {
    if (molecule_card_atom_rep < 0) return;

    // Resolve stable atom rep → current molecule_list index
    int32_t mol_idx = -1;
    for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
        for (uint32_t ei : molecule_list[mi].atom_indices) {
            if (ei < element_list.size() &&
                static_cast<int32_t>(element_list[ei].rep) == molecule_card_atom_rep) {
                mol_idx = static_cast<int32_t>(mi);
                break;
            }
        }
        if (mol_idx >= 0) break;
    }
    if (mol_idx < 0) { molecule_card_atom_rep = -1; return; }

    auto& mol = molecule_list[static_cast<size_t>(mol_idx)];
    if (mol.atom_indices.empty()) { molecule_card_atom_rep = -1; return; }

    // Build formula for title
    std::string formula = build_molecular_formula(element_list, mol.atom_indices);

    // Aggregate physics from all constituent particles
    int total_Z = 0, total_N = 0, total_leptons = 0;
    int total_particles = 0;
    float total_energy_MeV = 0.0f;
    float total_momentum_MeV = 0.0f;
    glm::vec2 com_vel(0.0f);
    float total_sim_mass = 0.0f;
    uint32_t oldest_birth = UINT32_MAX;
    float total_mu_N = 0.0f;  // net magnetic moment in nuclear magnetons
    uint32_t n_total = static_cast<uint32_t>(particles.types.size());

    // Collect per-atom data
    struct AtomInfo {
        int Z, N, electrons;
        uint32_t rep;
        const char* sym;
        const char* name;
    };
    std::vector<AtomInfo> atoms;

    for (uint32_t ei : mol.atom_indices) {
        if (ei >= element_list.size()) continue;
        auto& elem = element_list[ei];
        total_Z += elem.Z;
        total_N += elem.N;
        total_leptons += elem.electrons;

        const char* sym = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[elem.Z] : "?";
        const char* name = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[elem.Z] : "?";
        atoms.push_back({elem.Z, elem.N, elem.electrons, elem.rep, sym, name});

        // Scan all particles belonging to this atom for physics data
        for (uint32_t pi = 0; pi < n_total; ++pi) {
            if (pi >= particles.orbital_parent.size()) break;
            int32_t par = particles.orbital_parent[pi];
            if (par != static_cast<int32_t>(elem.rep) &&
                static_cast<int32_t>(pi) != static_cast<int32_t>(elem.rep)) continue;

            uint32_t pt = particles.types[pi];
            bool is_nucleon = (pt == PROTON_TYPE || pt == NEUTRON_TYPE);
            bool is_lepton = (pt == ELECTRON_TYPE_PHYS);
            if (!is_nucleon && !is_lepton) continue;
            total_particles++;

            // Accumulate magnetic moment
            if (pi * GENOME_SIZE + 1 < particles.genomes.size()) {
                float sp = particles.genomes[pi * GENOME_SIZE + 1];
                float ch = particles.genomes[pi * GENOME_SIZE + 0];
                if (pt == PROTON_TYPE)              total_mu_N += 2.793f * sp;
                else if (pt == NEUTRON_TYPE)        total_mu_N += -1.913f * sp;
                else if (pt == ELECTRON_TYPE_PHYS)  total_mu_N += ch * sp * 1836.15f;
            }

            if (readback_velocities && pi < readback_count) {
                float sim_mass = (pt <= 1) ? 40.0f : 1.0f;
                com_vel += readback_velocities[pi] * sim_mass;
                total_sim_mass += sim_mass;

                float m0 = rest_mass_MeV(pt);
                float spd = glm::length(readback_velocities[pi]);
                float beta = std::min(spd / C_SIM, 0.9999f);
                float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                total_energy_MeV += (gamma - 1.0f) * m0;
                total_momentum_MeV += gamma * m0 * beta;
            }

            if (pi < particles.birth_frames.size() &&
                particles.birth_frames[pi] < oldest_birth)
                oldest_birth = particles.birth_frames[pi];
        }
    }

    int net_charge = total_Z - total_leptons;
    float com_speed_sim = (total_sim_mass > 0.0f) ? glm::length(com_vel) / total_sim_mass : 0.0f;

    // Count bonds within molecule
    int bond_count = 0;
    if (bond_data_ptr && bond_data_count > 0) {
        for (auto& atom : atoms) {
            uint32_t base = atom.rep * MAX_BONDS_PER_PARTICLE;
            for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                if (base + s >= bond_data_count) break;
                uint32_t partner = bond_data_ptr[base + s];
                if (partner != 0xFFFFFFFFu && atom.rep < partner) bond_count++;
            }
        }
    }

    // Window position — next to element card position
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 590, io.DisplaySize.y - 60),
                            ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoNav;

    bool open = true;
    char title[128];
    snprintf(title, sizeof(title), "Molecule: %s###MoleculeCard", formula.c_str());

    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.04f, 0.07f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.06f, 0.12f, 0.24f, 0.95f));

    if (ImGui::Begin(title, &open, flags)) {
        float col_w = 120.0f;

        // Big formula display
        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "%s", formula.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.55f, 0.85f, 0.8f), "(%d atoms)", static_cast<int>(atoms.size()));
        ImGui::Separator();

        // Composition
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Protons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_Z);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Neutrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_N);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Electrons");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_leptons);

        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Bonds");
        ImGui::SameLine(col_w); ImGui::Text("%d", bond_count);

        ImGui::Spacing();
        ImGui::Separator();

        // Charge
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Net Charge");
        ImGui::SameLine(col_w);
        if (net_charge == 0)
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "0 (neutral)");
        else if (net_charge > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%d (ion)", net_charge);
        else
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%d (ion)", net_charge);

        // Mass
        float total_mass = total_Z * 40.0f + total_N * 40.0f + total_leptons * 1.0f;
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Total Mass");
        ImGui::SameLine(col_w); ImGui::Text("%.0f u", total_mass);

        // Particles
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Particles");
        ImGui::SameLine(col_w); ImGui::Text("%d", total_particles);

        // Speed
        {
            char spd_buf[32];
            fmt_speed(spd_buf, sizeof(spd_buf), com_speed_sim);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Speed");
            ImGui::SameLine(col_w); ImGui::Text("%s", spd_buf);
        }

        // Momentum
        {
            char mom_buf[32];
            if (total_momentum_MeV < 0.001f)
                snprintf(mom_buf, sizeof(mom_buf), "~0");
            else if (total_momentum_MeV < 1.0f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f keV/c", total_momentum_MeV * 1e3f);
            else if (total_momentum_MeV < 1e3f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f MeV/c", total_momentum_MeV);
            else if (total_momentum_MeV < 1e6f)
                snprintf(mom_buf, sizeof(mom_buf), "%.2f GeV/c", total_momentum_MeV / 1e3f);
            else
                snprintf(mom_buf, sizeof(mom_buf), "%.2f TeV/c", total_momentum_MeV / 1e6f);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Momentum");
            ImGui::SameLine(col_w); ImGui::Text("%s", mom_buf);
        }

        // Energy
        {
            char e_buf[32];
            fmt_energy_ev(e_buf, sizeof(e_buf), total_energy_MeV);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Energy");
            ImGui::SameLine(col_w); ImGui::Text("%s", e_buf);
        }

        // Age
        if (oldest_birth != UINT32_MAX) {
            uint32_t age_frames = frame_counter_display - oldest_birth;
            float age_sec = age_frames / 60.0f;
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Age");
            ImGui::SameLine(col_w);
            if (age_sec < 60.0f) ImGui::Text("%.1f s", age_sec);
            else ImGui::Text("%.1f min", age_sec / 60.0f);
        }

        // Magnetic moment — net of all constituent particles
        if (std::abs(total_mu_N) > 0.001f) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Mag.Moment");
            ImGui::SameLine(col_w);
            ImGui::Text("%+.2f \xCE\xBC\x4E", total_mu_N);
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Constituent atoms — clickable buttons that open element cards
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Atoms:");
        ImGui::SameLine(col_w);
        for (size_t ai = 0; ai < atoms.size() && ai < 12; ++ai) {
            auto& a = atoms[ai];
            int A = a.Z + a.N;
            char btn[32];
            snprintf(btn, sizeof(btn), "%s-%d##ma%zu", a.sym, A, ai);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.85f, 0.6f, 1.0f));
            if (ImGui::SmallButton(btn)) {
                element_card_nucleus_rep = static_cast<int32_t>(a.rep);
                navigate_to_particle = static_cast<int32_t>(a.rep);
            }
            ImGui::PopStyleColor();
            if (ai + 1 < atoms.size() && ai < 11) ImGui::SameLine();
        }
        if (atoms.size() > 12) ImGui::Text("+%zu more", atoms.size() - 12);

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Navigate", ImVec2(72, 26))) {
            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
            navigate_to_particle = static_cast<int32_t>(first_rep);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.30f, 0.50f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.40f, 0.65f, 0.90f));
        if (ImGui::Button("Export", ImVec2(72, 26))) {
            request_molecule_export = true;
            export_molecule_atom_rep = molecule_card_atom_rep;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(72, 26))) {
            molecule_card_atom_rep = -1;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    if (!open) molecule_card_atom_rep = -1;
}
