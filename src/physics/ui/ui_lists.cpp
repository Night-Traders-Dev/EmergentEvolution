#include "physics/ui/interface.h"
#include "physics/core/phys_particles.h"
#include "physics/ui/ui_data.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>
#include <map>

// ── Lists & bestiaries ───────────────────────────────────────────────────────
// Split from interface.cpp: element list, particle list, and bestiaries.

static void format_energy_str(char* buf, size_t buf_size, float energy_MeV) {
    float eV = energy_MeV * 1e6f;
    if (eV < 1e3f)
        snprintf(buf, buf_size, "%.0f eV", eV);
    else if (eV < 1e6f)
        snprintf(buf, buf_size, "%.1f keV", eV / 1e3f);
    else if (eV < 1e9f)
        snprintf(buf, buf_size, "%.1f MeV", eV / 1e6f);
    else if (eV < 1e12f)
        snprintf(buf, buf_size, "%.2f GeV", eV / 1e9f);
    else
        snprintf(buf, buf_size, "%.2f TeV", eV / 1e12f);
}

// Helper: format age from birth frame
static void format_age_str(char* buf, size_t buf_size, uint32_t oldest_birth, uint32_t current_frame) {
    if (oldest_birth != UINT32_MAX) {
        float age_sec = (current_frame - oldest_birth) / 60.0f;
        if (age_sec < 60.0f)
            snprintf(buf, buf_size, "%.0fs", age_sec);
        else
            snprintf(buf, buf_size, "%.1fm", age_sec / 60.0f);
    } else {
        snprintf(buf, buf_size, "-");
    }
}

// Helper: build molecular formula string from element Z counts (Hill system: C first, H second, rest alpha)
std::string build_molecular_formula(const std::vector<PhysicsInterface::ElementSummary>& elems,
                                            const std::vector<uint32_t>& atom_indices) {
    struct FormulaEntry { const char* sym; int count; int Z; };
    std::map<int, int> z_counts;
    for (uint32_t ei : atom_indices) {
        z_counts[elems[ei].Z]++;
    }
    std::vector<FormulaEntry> entries;
    for (auto& [z, cnt] : z_counts) {
        const char* sym = (z >= 1 && z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[z] : "?";
        entries.push_back({sym, cnt, z});
    }
    std::sort(entries.begin(), entries.end(), [](const FormulaEntry& a, const FormulaEntry& b) {
        if (a.Z == 6 && b.Z != 6) return true;
        if (b.Z == 6 && a.Z != 6) return false;
        if (a.Z == 1 && b.Z != 1) return true;
        if (b.Z == 1 && a.Z != 1) return false;
        return a.sym < b.sym ? true : (a.sym > b.sym ? false : a.Z < b.Z);
    });
    std::string formula;
    for (auto& e : entries) {
        formula += e.sym;
        if (e.count > 1) formula += std::to_string(e.count);
    }
    return formula;
}

void PhysicsInterface::draw_element_list() {
    if (!show_element_list || element_list.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 520.0f;
    float max_h = io.DisplaySize.y - 120.0f;
    ImVec2 win_size(win_w, max_h * 0.7f);

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_ELEMENT_LIST]);
        ImGui::SetNextWindowSize(tile_size_[TW_ELEMENT_LIST]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(450, 150), ImVec2(660, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.06f, 0.03f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.10f, 0.04f, 0.95f));

    // Count categories
    int mol_count = 0, free_count = 0;
    int stable_count = 0, unstable_count = 0, anti_count = 0;
    for (auto& mol : molecule_list) {
        if (mol.atom_indices.size() > 1) mol_count++;
        else free_count++;
    }
    for (auto& elem : element_list) {
        if (elem.is_anti) { anti_count++; continue; }
        const IsotopeDecayEntry* decay = lookup_isotope_decay(elem.Z, elem.N);
        float hl = 0.0f;
        NuclearDecayMode dmode = NDECAY_NONE;
        if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
        else { dmode = general_stability_rule(elem.Z, elem.N, hl); }
        if (dmode == NDECAY_NONE) stable_count++;
        else unstable_count++;
    }

    char title[96];
    if (mol_count > 0)
        snprintf(title, sizeof(title), "Elements (%d) + Molecules (%d)###ElementList",
                 static_cast<int>(element_list.size()), mol_count);
    else
        snprintf(title, sizeof(title), "Elements (%d)###ElementList",
                 static_cast<int>(element_list.size()));

    bool el_open = ImGui::Begin(title, &show_element_list);
    record_window_rect(TW_ELEMENT_LIST);
    if (!el_open) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }
    draw_minimize_button(TW_ELEMENT_LIST);

    // ── Filter buttons ──
    struct FilterDef { const char* label; int mode; ImVec4 col; int count; };
    FilterDef filters[] = {
        { "All",       0, ImVec4(0.7f, 0.7f, 0.8f, 1.0f), static_cast<int>(molecule_list.empty() ? element_list.size() : molecule_list.size()) },
        { "Stable",    1, ImVec4(0.3f, 0.9f, 0.4f, 1.0f), stable_count },
        { "Unstable",  2, ImVec4(1.0f, 0.5f, 0.2f, 1.0f), unstable_count },
        { "Antimatter",3, ImVec4(0.5f, 0.8f, 1.0f, 1.0f), anti_count },
        { "Molecules", 4, ImVec4(0.4f, 0.65f, 1.0f, 1.0f), mol_count },
    };
    for (int f = 0; f < 5; ++f) {
        if (f > 0) ImGui::SameLine(0, 3);
        ImVec4 col = filters[f].col;
        bool active = (element_filter_mode == filters[f].mode);
        if (!active) col = ImVec4(col.x * 0.4f, col.y * 0.4f, col.z * 0.4f, 0.6f);
        char flabel[32];
        snprintf(flabel, sizeof(flabel), "%s:%d###ef%d", filters[f].label, filters[f].count, f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.2f, col.y * 0.2f, col.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::SmallButton(flabel))
            element_filter_mode = filters[f].mode;
        ImGui::PopStyleColor(3);
    }
    ImGui::Separator();

    // Helper lambda: check if element passes the filter
    auto elem_passes_filter = [&](const ElementSummary& elem) -> bool {
        if (element_filter_mode == 0) return true;
        if (element_filter_mode == 3) return elem.is_anti;
        if (elem.is_anti) return element_filter_mode == 0;
        const IsotopeDecayEntry* decay = lookup_isotope_decay(elem.Z, elem.N);
        float hl = 0.0f;
        NuclearDecayMode dmode = NDECAY_NONE;
        if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
        else { dmode = general_stability_rule(elem.Z, elem.N, hl); }
        if (element_filter_mode == 1) return dmode == NDECAY_NONE;
        if (element_filter_mode == 2) return dmode != NDECAY_NONE;
        return true;
    };

    // Helper: compute nuclear mass and magnetic moment for a nucleus
    auto compute_nuclear_props = [](int Z, int N, bool is_anti) -> std::pair<float, float> {
        float mass = Z * PHYS_REST_MASS_MEV[PROTON_TYPE] + N * PHYS_REST_MASS_MEV[NEUTRON_TYPE];
        // Simple shell model: unpaired nucleons contribute
        // Even-even = 0, odd-A = single particle contribution
        float mu = 0.0f;
        int A = Z + N;
        if (A == 1 && N == 0)      mu = 2.793f;   // proton
        else if (A == 1 && Z == 0) mu = -1.913f;   // neutron
        else if (A == 2 && Z == 1) mu = 0.857f;    // deuteron
        else if (A == 3 && Z == 2) mu = -2.128f;   // He-3
        else if (A == 3 && Z == 1) mu = 2.979f;    // H-3
        else if (A == 4 && Z == 2) mu = 0.0f;      // He-4 (even-even)
        else {
            // Rough: odd protons contribute ~2.793, odd neutrons ~-1.913
            if (Z % 2 != 0) mu += 2.793f;
            if (N % 2 != 0) mu -= 1.913f;
        }
        if (is_anti) mu = -mu;
        return { mass, mu };
    };

    // Use molecule_list if available
    if (!molecule_list.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "  %-24s %4s %10s %6s",
                           "Formula", "Q", "Energy", "Age");
        ImGui::Separator();

        ImGui::BeginChild("##ElemListScroll", ImVec2(0, 0), false);

        for (size_t mi = 0; mi < molecule_list.size(); ++mi) {
            auto& mol = molecule_list[mi];
            bool is_molecule = mol.atom_indices.size() > 1;

            // Apply filters
            if (element_filter_mode == 4 && !is_molecule) continue;
            if (element_filter_mode != 0 && element_filter_mode != 4 && !is_molecule) {
                if (!elem_passes_filter(element_list[mol.atom_indices[0]])) continue;
            }
            if (element_filter_mode != 0 && element_filter_mode != 4 && is_molecule) {
                // For molecules in stability filter: show if any atom matches
                bool any_match = false;
                for (uint32_t ei : mol.atom_indices) {
                    if (elem_passes_filter(element_list[ei])) { any_match = true; break; }
                }
                if (!any_match) continue;
            }

            std::string formula = build_molecular_formula(element_list, mol.atom_indices);

            // Look up common name for molecules, element name for single atoms
            const char* common_name = nullptr;
            if (is_molecule) {
                common_name = lookup_molecule_common_name(formula.c_str());
            } else {
                auto& elem0 = element_list[mol.atom_indices[0]];
                if (elem0.Z >= 1 && elem0.Z <= FULL_ELEMENT_COUNT)
                    common_name = ELEMENT_NAMES[elem0.Z];
            }

            char charge_str[16];
            if (mol.total_charge == 0) snprintf(charge_str, sizeof(charge_str), "0");
            else if (mol.total_charge > 0) snprintf(charge_str, sizeof(charge_str), "+%d", mol.total_charge);
            else snprintf(charge_str, sizeof(charge_str), "%d", mol.total_charge);

            char energy_str[32], age_str[16];
            format_energy_str(energy_str, sizeof(energy_str), mol.total_energy_MeV);
            format_age_str(age_str, sizeof(age_str), mol.oldest_birth, frame_counter_display);

            ImGui::PushID(static_cast<int>(mi + 10000));

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec4 dot_col;
            if (is_molecule) {
                dot_col = ImVec4(0.4f, 0.65f, 1.0f, 1.0f);
            } else {
                auto& elem = element_list[mol.atom_indices[0]];
                const IsotopeDecayEntry* decay = elem.is_anti ? nullptr : lookup_isotope_decay(elem.Z, elem.N);
                float hl = 0.0f;
                NuclearDecayMode dmode = NDECAY_NONE;
                if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
                else if (!elem.is_anti) { dmode = general_stability_rule(elem.Z, elem.N, hl); }

                if (elem.is_anti)             dot_col = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
                else if (dmode == NDECAY_NONE) dot_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
                else if (hl > 7200.0f)        dot_col = ImVec4(0.9f, 0.9f, 0.5f, 1.0f);
                else if (hl > 60.0f)          dot_col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
                else                          dot_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            }
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 4.0f,
                ImGui::ColorConvertFloat4ToU32(dot_col));

            uint32_t first_rep = element_list[mol.atom_indices[0]].rep;
            bool is_selected_mol = (molecule_card_atom_rep == static_cast<int32_t>(first_rep));
            bool is_selected_elem = !is_molecule && (element_card_nucleus_rep == static_cast<int32_t>(first_rep));
            // Build display string: formula (name) charge energy age
            char row_text[192];
            if (common_name) {
                char label[64];
                snprintf(label, sizeof(label), "%s (%s)", formula.c_str(), common_name);
                snprintf(row_text, sizeof(row_text), "  %-24s %4s %10s %6s",
                         label, charge_str, energy_str, age_str);
            } else {
                snprintf(row_text, sizeof(row_text), "  %-24s %4s %10s %6s",
                         formula.c_str(), charge_str, energy_str, age_str);
            }

            if (ImGui::Selectable(row_text, is_selected_mol || is_selected_elem,
                                  ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                if (is_molecule) {
                    molecule_card_atom_rep = static_cast<int32_t>(first_rep);
                    element_card_nucleus_rep = -1;
                } else {
                    element_card_nucleus_rep = static_cast<int32_t>(first_rep);
                    molecule_card_atom_rep = -1;
                }
                navigate_to_particle = static_cast<int32_t>(first_rep);
            }

            // Tooltip with physics properties
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (is_molecule) {
                    if (common_name) {
                        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "%s", formula.c_str());
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "%s", common_name);
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "Molecule: %s", formula.c_str());
                    }
                    ImGui::Text("Atoms: %d  Charge: %s", static_cast<int>(mol.atom_indices.size()), charge_str);
                    ImGui::Separator();
                    float total_mass = 0.0f, total_mu = 0.0f;
                    for (uint32_t ei : mol.atom_indices) {
                        auto& elem = element_list[ei];
                        const char* sym = (elem.Z >= 1 && elem.Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[elem.Z] : "?";
                        auto [mass, mu] = compute_nuclear_props(elem.Z, elem.N, elem.is_anti);
                        total_mass += mass;
                        total_mu += mu;
                        ImGui::Text("  %s-%d (Z=%d N=%d)", sym, elem.Z + elem.N, elem.Z, elem.N);
                    }
                    ImGui::Separator();
                    ImGui::Text("Total mass: %.1f MeV/c2", total_mass);
                    if (std::fabs(total_mu) > 0.001f)
                        ImGui::Text("Net magnetic moment: %.3f uN", total_mu);
                } else {
                    auto& elem = element_list[mol.atom_indices[0]];
                    int Z = elem.Z, A = Z + elem.N;
                    const char* sym = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?";
                    const char* name = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "Unknown";
                    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 1.0f), "%s-%d %s", sym, A, name);
                    const char* lep = elem.is_anti ? "e+" : "e-";
                    ImGui::Text("Z=%d  N=%d  %s=%d", Z, elem.N, lep, elem.electrons);
                    auto [mass, mu] = compute_nuclear_props(Z, elem.N, elem.is_anti);
                    ImGui::Separator();
                    ImGui::Text("Mass: %.1f MeV/c2  Spin: %s", mass,
                                (A % 2 == 0) ? "integer" : "half-integer");
                    ImGui::Text("Charge: %s", charge_str);
                    if (std::fabs(mu) > 0.001f)
                        ImGui::Text("Magnetic moment: %.3f uN", mu);
                }
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    } else {
        // Fallback: atom-by-atom display (no bonds)
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "%-6s %-12s %3s %3s %10s %6s",
                           "Sym", "Name", "A", "Q", "Energy", "Age");
        ImGui::Separator();

        ImGui::BeginChild("##ElemListScroll", ImVec2(0, 0), false);

        for (size_t i = 0; i < element_list.size(); ++i) {
            auto& elem = element_list[i];
            if (!elem_passes_filter(elem)) continue;

            int Z = elem.Z;
            int A = elem.Z + elem.N;
            int net_charge = Z - elem.electrons;

            const char* sym_base = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_SYMBOLS[Z] : "?";
            const char* name_base = (Z >= 1 && Z <= FULL_ELEMENT_COUNT) ? ELEMENT_NAMES[Z] : "Unknown";

            char sym[16], name[64];
            if (elem.is_anti) {
                snprintf(sym, sizeof(sym), "\xc4\x80%s", sym_base);
                snprintf(name, sizeof(name), "Anti-%s", name_base);
            } else {
                snprintf(sym, sizeof(sym), "%s", sym_base);
                snprintf(name, sizeof(name), "%s", name_base);
            }

            const IsotopeDecayEntry* decay = elem.is_anti ? nullptr : lookup_isotope_decay(Z, elem.N);
            float hl = 0.0f;
            NuclearDecayMode dmode = NDECAY_NONE;
            if (decay) { dmode = decay->mode; hl = decay->half_life_frames; }
            else if (!elem.is_anti) { dmode = general_stability_rule(Z, elem.N, hl); }

            ImVec4 stab_col;
            if (elem.is_anti)               stab_col = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
            else if (dmode == NDECAY_NONE)   stab_col = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
            else if (hl > 7200.0f)          stab_col = ImVec4(0.9f, 0.9f, 0.5f, 1.0f);
            else if (hl > 60.0f)            stab_col = ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
            else                            stab_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

            char charge_str[16];
            if (net_charge == 0) snprintf(charge_str, sizeof(charge_str), "0");
            else if (net_charge > 0) snprintf(charge_str, sizeof(charge_str), "+%d", net_charge);
            else snprintf(charge_str, sizeof(charge_str), "%d", net_charge);

            bool is_selected = (element_card_nucleus_rep == static_cast<int32_t>(elem.rep));
            ImGui::PushID(static_cast<int>(i));

            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 4.0f,
                ImGui::ColorConvertFloat4ToU32(stab_col));

            char energy_str[32], age_str[16];
            format_energy_str(energy_str, sizeof(energy_str), elem.energy_MeV);
            format_age_str(age_str, sizeof(age_str), elem.oldest_birth, frame_counter_display);

            char row_text[192];
            snprintf(row_text, sizeof(row_text), "  %-4s %-4s-%-3d  %-10s %3s %10s %6s",
                     sym, sym, A, name, charge_str, energy_str, age_str);

            if (ImGui::Selectable(row_text, is_selected, ImGuiSelectableFlags_None, ImVec2(0, 22))) {
                element_card_nucleus_rep = static_cast<int32_t>(elem.rep);
                navigate_to_particle = static_cast<int32_t>(elem.rep);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(elem.is_anti ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f) : ImVec4(0.9f, 0.85f, 0.6f, 1.0f),
                                   "%s-%d%s", sym, A, elem.is_anti ? " (antimatter)" : "");
                const char* lep = elem.is_anti ? "e+" : "e-";
                ImGui::Text("Z=%d  N=%d  %s=%d", Z, elem.N, lep, elem.electrons);
                auto [mass, mu] = compute_nuclear_props(Z, elem.N, elem.is_anti);
                ImGui::Separator();
                ImGui::Text("Mass: %.1f MeV/c2  Spin: %s", mass,
                            (A % 2 == 0) ? "integer" : "half-integer");
                ImGui::Text("Charge: %s", charge_str);
                if (std::fabs(mu) > 0.001f)
                    ImGui::Text("Magnetic moment: %.3f uN", mu);
                if (elem.is_anti) {
                    ImGui::TextColored(stab_col, "Antimatter");
                } else if (dmode == NDECAY_NONE) {
                    ImGui::TextColored(stab_col, "Stable");
                } else {
                    const char* mode_str = "";
                    switch (dmode) {
                        case NDECAY_ALPHA: mode_str = "alpha"; break;
                        case NDECAY_BETA_MINUS: mode_str = "beta-"; break;
                        case NDECAY_BETA_PLUS: mode_str = "beta+"; break;
                        case NDECAY_NEUTRON_EMISSION: mode_str = "n-emit"; break;
                        case NDECAY_PROTON_EMISSION: mode_str = "p-emit"; break;
                        default: break;
                    }
                    ImGui::TextColored(stab_col, "Unstable (%s, t1/2=%.0f frames)", mode_str, hl);
                }
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Particle List Window ─────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_particle_list(const Particles& particles) {
    if (!show_particle_list || active_particle_display == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 500.0f;
    float max_h = io.DisplaySize.y - 120.0f;
    ImVec2 win_size(win_w, max_h * 0.7f);

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_PARTICLE_LIST]);
        ImGui::SetNextWindowSize(tile_size_[TW_PARTICLE_LIST]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(460, 150), ImVec2(620, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.03f, 0.06f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.05f, 0.10f, 0.18f, 0.95f));

    char ptitle[64];
    snprintf(ptitle, sizeof(ptitle), "Particles (%u)###ParticleList", active_particle_display);

    bool pl_open = ImGui::Begin(ptitle, &show_particle_list);
    record_window_rect(TW_PARTICLE_LIST);
    if (!pl_open) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }
    draw_minimize_button(TW_PARTICLE_LIST);

    // ── Type filter buttons (same pattern as event log) ──
    float wrap_x = ImGui::GetContentRegionAvail().x - 10.0f;
    float cur_x = 0.0f;
    int active_filter_count = 0;
    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        if (type_counts_display[t] == 0) continue;
        active_filter_count++;
        char tag_label[48];
        snprintf(tag_label, sizeof(tag_label), "%s:%u###pf%u",
                 PHYS_TYPE_NAMES[t], type_counts_display[t], t);
        float btn_w = ImGui::CalcTextSize(tag_label).x + 10.0f;
        if (cur_x + btn_w > wrap_x && cur_x > 0.0f) {
            cur_x = 0.0f;
        } else if (cur_x > 0.0f) {
            ImGui::SameLine(0, 3);
        }
        ImVec4 col = PHYS_TYPE_UI_COLORS[t];
        if (!particle_type_filter[t])
            col = ImVec4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.2f, col.y * 0.2f, col.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::SmallButton(tag_label))
            particle_type_filter[t] = !particle_type_filter[t];
        ImGui::PopStyleColor(3);
        cur_x += btn_w + 3.0f;
    }
    if (active_filter_count > 1) {
        ImGui::SameLine(0, 8);
        if (ImGui::SmallButton("All"))
            for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) particle_type_filter[i] = true;
        ImGui::SameLine(0, 3);
        if (ImGui::SmallButton("None"))
            for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; ++i) particle_type_filter[i] = false;
    }
    ImGui::Separator();

    // Column headers
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), " %-4s %-12s %12s  %10s %6s",
                       "ID", "Type", "Speed", "Energy", "Age");
    ImGui::Separator();

    // Scrollable list — grouped by type
    ImGui::BeginChild("##PartListScroll", ImVec2(0, 0), false);

    uint32_t n = static_cast<uint32_t>(particles.types.size());

    // Iterate by type group for organization
    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        if (type_counts_display[t] == 0) continue;
        if (!particle_type_filter[t]) continue;

        // Type group header
        const char* tname = PHYS_TYPE_NAMES[t];
        ImVec4 tcol = PHYS_TYPE_UI_COLORS[t];
        float m0 = rest_mass_MeV(t);
        float charge = PHYS_CHARGE[t];
        float spin = PHYS_SPIN[t];

        // Magnetic moment (nuclear magnetons for baryons, Bohr magnetons for leptons)
        float mu = 0.0f;
        const char* mu_unit = "";
        if (t == PROTON_TYPE)            { mu = 2.793f;  mu_unit = " uN"; }
        else if (t == NEUTRON_TYPE)      { mu = -1.913f; mu_unit = " uN"; }
        else if (t == ANTIPROTON_TYPE_PHYS) { mu = -2.793f; mu_unit = " uN"; }
        else if (spin > 0.01f && std::fabs(charge) > 0.01f) {
            // Dirac magnetic moment: mu = q * spin (in natural units)
            mu = charge * spin;
            mu_unit = (m0 > 100.0f) ? " uN" : " uB";  // baryons: nuclear, leptons: Bohr
        }

        ImGui::PushID(static_cast<int>(t + 1000));

        // Format mass string for header
        char mass_str[32];
        if (m0 < 0.001f)           snprintf(mass_str, sizeof(mass_str), "massless");
        else if (m0 < 1.0f)        snprintf(mass_str, sizeof(mass_str), "%.1f keV", m0 * 1000.0f);
        else if (m0 < 1000.0f)     snprintf(mass_str, sizeof(mass_str), "%.1f MeV", m0);
        else if (m0 < 1.0e6f)      snprintf(mass_str, sizeof(mass_str), "%.1f GeV", m0 / 1000.0f);
        else if (m0 < 1.0e9f)      snprintf(mass_str, sizeof(mass_str), "%.1f TeV", m0 / 1.0e6f);
        else                        snprintf(mass_str, sizeof(mass_str), "%.0e MeV", m0);

        bool header_open = ImGui::TreeNodeEx(tname,
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth,
            "%s (%u)  [%s  q=%.2g  s=%.2g]", tname, type_counts_display[t],
            mass_str, charge, spin);

        if (header_open) {
            bool massless = (m0 < 0.001f);

            for (uint32_t i = 0; i < n; ++i) {
                if (particles.types[i] != t) continue;
                if (readback_energies_ptr && readback_energies_ptr[i] < 0.01f) continue;

                // Compute kinetic energy
                float KE_MeV = 0.0f;
                float speed = 0.0f;
                if (readback_velocities && i < readback_count) {
                    speed = glm::length(readback_velocities[i]);
                    float beta = std::min(speed / C_SIM, 0.9999f);
                    if (massless) {
                        KE_MeV = beta * 1.0f;
                    } else {
                        float gamma = 1.0f / std::sqrt(1.0f - beta * beta);
                        KE_MeV = (gamma - 1.0f) * m0;
                    }
                }

                char energy_str[32];
                fmt_energy_ev(energy_str, sizeof(energy_str), KE_MeV);

                char speed_str[32];
                fmt_speed(speed_str, sizeof(speed_str), speed);

                bool is_selected = (selected_particle_idx == static_cast<int32_t>(i));
                ImGui::PushID(static_cast<int>(i));

                // Color dot
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(cursor.x + 5.0f, cursor.y + 10.0f), 3.0f,
                    ImGui::ColorConvertFloat4ToU32(tcol));

                // Format age
                char age_str[16];
                if (i < static_cast<uint32_t>(particles.birth_frames.size())) {
                    float age_sec = (frame_counter_display - particles.birth_frames[i]) / 60.0f;
                    if (age_sec < 60.0f)
                        snprintf(age_str, sizeof(age_str), "%.0fs", age_sec);
                    else
                        snprintf(age_str, sizeof(age_str), "%.1fm", age_sec / 60.0f);
                } else {
                    snprintf(age_str, sizeof(age_str), "-");
                }

                char row[200];
                snprintf(row, sizeof(row), "  #%-5u %-10s %12s  %10s %6s",
                         i, tname, speed_str, energy_str, age_str);

                if (ImGui::Selectable(row, is_selected, ImGuiSelectableFlags_None, ImVec2(0, 20))) {
                    selected_particle_idx = static_cast<int32_t>(i);
                    navigate_to_particle = static_cast<int32_t>(i);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(tcol, "%s #%u", tname, i);
                    ImGui::Text("Speed: %s  Energy: %s  Age: %s", speed_str, energy_str, age_str);
                    ImGui::Separator();
                    ImGui::Text("Mass: %s  Charge: %.3g  Spin: %.2g", mass_str, charge, spin);
                    if (std::fabs(mu) > 0.001f)
                        ImGui::Text("Magnetic moment: %.3f%s", mu, mu_unit);
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Click to inspect & navigate");
                    ImGui::EndTooltip();
                }

                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ── Particle Bestiary Window ─────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_particle_bestiary() {
    if (!show_particle_bestiary) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = std::min(920.0f, io.DisplaySize.x - 20.0f);
    float max_h = io.DisplaySize.y - 80.0f;
    ImVec2 win_size(win_w, max_h * 0.75f);

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_PARTICLE_BESTIARY]);
        ImGui::SetNextWindowSize(tile_size_[TW_PARTICLE_BESTIARY]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(1200, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.08f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.06f, 0.05f, 0.12f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.08f, 0.18f, 0.95f));

    // Count active types
    int active_types = 0;
    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t)
        if (type_counts_display[t] > 0) active_types++;

    char title[80];
    snprintf(title, sizeof(title), "Particle Bestiary (%d active types)###Bestiary", active_types);

    bool pb_open = ImGui::Begin(title, &show_particle_bestiary);
    record_window_rect(TW_PARTICLE_BESTIARY);
    if (!pb_open) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }
    draw_minimize_button(TW_PARTICLE_BESTIARY);

    // Grid layout
    const float cell_w = 110.0f;
    const float cell_h = 130.0f;
    const float padding = 4.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, static_cast<int>(avail_w / (cell_w + padding)));

    ImGui::BeginChild("##BestiaryScroll", ImVec2(0, 0), false);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    int col = 0;

    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        if (col > 0) ImGui::SameLine(0, padding);

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec4 pcol = PHYS_TYPE_UI_COLORS[t];
        uint32_t count = type_counts_display[t];
        const auto& stats = type_stats[t];
        bool active = (count > 0);
        bool ever = (stats.total_spawned > 0);
        float alpha = active ? 1.0f : (ever ? 0.6f : 0.35f);

        // Cell background
        ImVec2 cell_min = cursor;
        ImVec2 cell_max = ImVec2(cursor.x + cell_w, cursor.y + cell_h);

        if (active) {
            // Subtle colored border for active particles
            dl->AddRect(cell_min, cell_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(pcol.x * 0.5f, pcol.y * 0.5f, pcol.z * 0.5f, 0.6f)),
                        3.0f, 0, 1.5f);
        }
        dl->AddRectFilled(cell_min, cell_max,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(0.08f, 0.08f, 0.12f, alpha * 0.5f)),
                          3.0f);

        // Invisible button for hover/tooltip
        ImGui::InvisibleButton(PHYS_TYPE_LABELS[t], ImVec2(cell_w, cell_h));
        bool hovered = ImGui::IsItemHovered();

        // Colored circle (particle thumbnail)
        float circle_r = 14.0f;
        ImVec2 center(cell_min.x + cell_w * 0.5f, cell_min.y + 22.0f);
        dl->AddCircleFilled(center, circle_r,
                            ImGui::ColorConvertFloat4ToU32(ImVec4(pcol.x, pcol.y, pcol.z, alpha)));
        // Glow ring for active
        if (active) {
            dl->AddCircle(center, circle_r + 2.0f,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(pcol.x, pcol.y, pcol.z, 0.3f)),
                          0, 1.5f);
        }

        // Label (centered, bold)
        {
            const char* label = PHYS_TYPE_LABELS[t];
            ImVec2 sz = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(cell_min.x + (cell_w - sz.x) * 0.5f, cell_min.y + 40.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(pcol.x, pcol.y, pcol.z, alpha)),
                        label);
        }

        // Name (centered, dim)
        {
            const char* name = PHYS_TYPE_NAMES[t];
            ImVec2 sz = ImGui::CalcTextSize(name);
            float name_x = cell_min.x + (cell_w - sz.x) * 0.5f;
            if (sz.x > cell_w - 4.0f) name_x = cell_min.x + 2.0f; // left-align if too wide
            dl->AddText(ImVec2(name_x, cell_min.y + 56.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.6f, 0.7f, alpha * 0.8f)),
                        name);
        }

        // Stats lines
        float sy = cell_min.y + 74.0f;
        ImU32 stat_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.6f, alpha));
        ImU32 count_col = active
            ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.9f, 0.8f, 1.0f))
            : stat_col;

        char buf[48];
        snprintf(buf, sizeof(buf), "Count: %u", count);
        dl->AddText(ImVec2(cell_min.x + 4.0f, sy), count_col, buf);

        snprintf(buf, sizeof(buf), "Total: %u", stats.total_spawned);
        dl->AddText(ImVec2(cell_min.x + 4.0f, sy + 14.0f), stat_col, buf);

        if (stats.lifetime_count > 0) {
            double avg_life = stats.lifetime_sum / stats.lifetime_count;
            // Convert frames to real time using current FPS
            float fps = (fps_display > 1.0f) ? fps_display : 60.0f;
            double avg_sec = avg_life / fps;
            char time_buf[24];
            if (avg_sec < 0.001)        snprintf(time_buf, sizeof(time_buf), "%.0f us", avg_sec * 1e6);
            else if (avg_sec < 1.0)     snprintf(time_buf, sizeof(time_buf), "%.0f ms", avg_sec * 1000.0);
            else if (avg_sec < 60.0)    snprintf(time_buf, sizeof(time_buf), "%.1fs", avg_sec);
            else if (avg_sec < 3600.0)  snprintf(time_buf, sizeof(time_buf), "%.1fm", avg_sec / 60.0);
            else                        snprintf(time_buf, sizeof(time_buf), "%.1fh", avg_sec / 3600.0);
            snprintf(buf, sizeof(buf), "Life: %.0ff (%s)", avg_life, time_buf);
        } else if (count > 0) {
            snprintf(buf, sizeof(buf), "Avg life: --");
        } else {
            snprintf(buf, sizeof(buf), "Peak: %u", stats.peak_count);
        }
        dl->AddText(ImVec2(cell_min.x + 4.0f, sy + 28.0f), stat_col, buf);

        // Tooltip with physical properties
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(pcol, "%s (%s)", PHYS_TYPE_NAMES[t], PHYS_TYPE_LABELS[t]);
            ImGui::Separator();

            float m0 = rest_mass_MeV(t);
            if (m0 < 0.001f)           ImGui::Text("Mass: massless");
            else if (m0 < 1.0f)        ImGui::Text("Mass: %.3f keV", m0 * 1000.0f);
            else if (m0 < 1000.0f)     ImGui::Text("Mass: %.2f MeV", m0);
            else if (m0 < 1.0e6f)      ImGui::Text("Mass: %.2f GeV", m0 / 1000.0f);
            else if (m0 < 1.0e9f)      ImGui::Text("Mass: %.2f TeV", m0 / 1.0e6f);
            else                        ImGui::Text("Mass: %.0e MeV", m0);

            if (t < PHYS_PARTICLE_TYPES) {
                ImGui::Text("Charge: %+.2f e", PHYS_CHARGE[t]);
                ImGui::Text("Spin: %.1f", PHYS_SPIN[t]);
            }
            ImGui::Separator();
            ImGui::Text("Current: %u", count);
            ImGui::Text("Total spawned: %u", stats.total_spawned);
            ImGui::Text("Peak: %u", stats.peak_count);

            if (stats.lifetime_count > 0) {
                double avg_life = stats.lifetime_sum / stats.lifetime_count;
                float fps = (fps_display > 1.0f) ? fps_display : 60.0f;
                double avg_sec = avg_life / fps;
                if (avg_sec < 0.001)
                    ImGui::Text("Avg lifetime: %.0f frames (%.0f us)", avg_life, avg_sec * 1e6);
                else if (avg_sec < 1.0)
                    ImGui::Text("Avg lifetime: %.0f frames (%.0f ms)", avg_life, avg_sec * 1000.0);
                else if (avg_sec < 60.0)
                    ImGui::Text("Avg lifetime: %.0f frames (%.1f s)", avg_life, avg_sec);
                else if (avg_sec < 3600.0)
                    ImGui::Text("Avg lifetime: %.0f frames (%.1f min)", avg_life, avg_sec / 60.0);
                else
                    ImGui::Text("Avg lifetime: %.0f frames (%.1f hr)", avg_life, avg_sec / 3600.0);
            }
            if (count > 0) {
                ImGui::Text("Avg energy: %.2f", stats.energy_sum / count);
                ImGui::Text("Avg speed: %.2f px/f", stats.speed_sum / count);
            }
            ImGui::EndTooltip();
        }

        col++;
        if (col >= cols) col = 0;
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ── Element Bestiary Window ──────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// CPK-inspired element colors by group
static ImVec4 element_color_for_Z(int Z) {
    // Periodic table group coloring
    if (Z == 1)  return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);   // hydrogen — white
    if (Z == 2)  return ImVec4(0.6f, 1.0f, 1.0f, 1.0f);    // helium — cyan
    // Alkali metals (Li, Na, K, Rb, Cs, Fr)
    if (Z==3||Z==11||Z==19||Z==37||Z==55||Z==87)
        return ImVec4(0.7f, 0.3f, 0.9f, 1.0f);   // violet
    // Alkaline earth (Be, Mg, Ca, Sr, Ba, Ra)
    if (Z==4||Z==12||Z==20||Z==38||Z==56||Z==88)
        return ImVec4(0.3f, 0.8f, 0.3f, 1.0f);    // green
    // Noble gases (Ne, Ar, Kr, Xe, Rn, Og)
    if (Z==10||Z==18||Z==36||Z==54||Z==86||Z==118)
        return ImVec4(0.4f, 0.9f, 0.9f, 1.0f);    // cyan
    // Halogens (F, Cl, Br, I, At, Ts)
    if (Z==9||Z==17||Z==35||Z==53||Z==85||Z==117)
        return ImVec4(0.4f, 0.9f, 0.5f, 1.0f);    // green-cyan
    // Carbon group
    if (Z==6) return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);      // dark grey
    if (Z==7) return ImVec4(0.2f, 0.3f, 0.9f, 1.0f);      // nitrogen — blue
    if (Z==8) return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);      // oxygen — red
    if (Z==15) return ImVec4(0.9f, 0.5f, 0.1f, 1.0f);     // phosphorus — orange
    if (Z==16) return ImVec4(0.9f, 0.8f, 0.2f, 1.0f);     // sulfur — yellow
    // Transition metals
    if ((Z>=21&&Z<=30)||(Z>=39&&Z<=48)||(Z>=72&&Z<=80)||(Z>=104&&Z<=112))
        return ImVec4(0.9f, 0.6f, 0.3f, 1.0f);    // orange
    // Lanthanides
    if (Z>=57&&Z<=71) return ImVec4(0.6f, 0.8f, 0.5f, 1.0f);  // sage
    // Actinides
    if (Z>=89&&Z<=103) return ImVec4(0.7f, 0.5f, 0.7f, 1.0f); // mauve
    // Post-transition metals, metalloids
    if (Z==13||Z==31||Z==49||Z==50||Z==81||Z==82||Z==83||Z==84||Z==113||Z==114||Z==115||Z==116)
        return ImVec4(0.6f, 0.7f, 0.7f, 1.0f);    // grey-blue
    // Metalloids
    if (Z==5||Z==14||Z==32||Z==33||Z==34||Z==51||Z==52)
        return ImVec4(0.7f, 0.7f, 0.5f, 1.0f);    // khaki
    return ImVec4(0.6f, 0.6f, 0.7f, 1.0f);         // default grey
}

void PhysicsInterface::draw_element_bestiary() {
    if (!show_element_bestiary) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = std::min(960.0f, io.DisplaySize.x - 20.0f);
    float max_h = io.DisplaySize.y - 80.0f;
    ImVec2 win_size(win_w, max_h * 0.75f);

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_ELEMENT_BESTIARY]);
        ImGui::SetNextWindowSize(tile_size_[TW_ELEMENT_BESTIARY]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(600, 300), ImVec2(1200, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.04f, 0.07f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.05f, 0.06f, 0.10f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.08f, 0.09f, 0.16f, 0.95f));

    int active_elements = 0, discovered_elements = 0;
    for (int z = 1; z <= FULL_ELEMENT_COUNT; ++z) {
        if (element_stats[z].current_count > 0) active_elements++;
        if (element_stats[z].total_spawned > 0) discovered_elements++;
    }

    char title[96];
    snprintf(title, sizeof(title), "Element Bestiary (%d/%d discovered)###ElemBestiary",
             discovered_elements, FULL_ELEMENT_COUNT);

    bool eb_open = ImGui::Begin(title, &show_element_bestiary);
    record_window_rect(TW_ELEMENT_BESTIARY);
    if (!eb_open) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }
    draw_minimize_button(TW_ELEMENT_BESTIARY);

    const float cell_w = 88.0f;
    const float cell_h = 105.0f;
    const float padding = 3.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, static_cast<int>(avail_w / (cell_w + padding)));

    ImGui::BeginChild("##ElemBestiaryScroll", ImVec2(0, 0), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int col = 0;

    for (int Z = 1; Z <= FULL_ELEMENT_COUNT; ++Z) {
        if (col > 0) ImGui::SameLine(0, padding);

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        const auto& stats = element_stats[Z];
        ImVec4 ecol = element_color_for_Z(Z);
        bool active = (stats.current_count > 0);
        bool discovered = (stats.total_spawned > 0);
        float alpha = active ? 1.0f : (discovered ? 0.6f : 0.25f);

        ImVec2 cell_min = cursor;
        ImVec2 cell_max = ImVec2(cursor.x + cell_w, cursor.y + cell_h);

        // Cell background
        dl->AddRectFilled(cell_min, cell_max,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.07f, 0.11f, alpha * 0.6f)),
                          3.0f);
        if (active) {
            dl->AddRect(cell_min, cell_max,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ecol.x * 0.6f, ecol.y * 0.6f, ecol.z * 0.6f, 0.7f)),
                        3.0f, 0, 1.5f);
        }

        // Invisible button for hover
        char iid[16];
        snprintf(iid, sizeof(iid), "##ez%d", Z);
        ImGui::InvisibleButton(iid, ImVec2(cell_w, cell_h));
        bool hovered = ImGui::IsItemHovered();

        // Z number (top-left, small)
        char zbuf[8];
        snprintf(zbuf, sizeof(zbuf), "%d", Z);
        dl->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 2.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.5f, alpha)), zbuf);

        // Element symbol (large, centered, colored)
        {
            const char* sym = ELEMENT_SYMBOLS[Z];
            ImVec2 sz = ImGui::CalcTextSize(sym);
            dl->AddText(ImVec2(cell_min.x + (cell_w - sz.x) * 0.5f, cell_min.y + 14.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(ecol.x, ecol.y, ecol.z, alpha)),
                        sym);
        }

        // Element name (dim, centered)
        {
            const char* name = ELEMENT_NAMES[Z];
            ImVec2 sz = ImGui::CalcTextSize(name);
            float nx = cell_min.x + (cell_w - sz.x) * 0.5f;
            if (sz.x > cell_w - 4.0f) nx = cell_min.x + 2.0f;
            dl->AddText(ImVec2(nx, cell_min.y + 32.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.6f, alpha * 0.8f)),
                        name);
        }

        // Stats
        ImU32 stat_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.6f, alpha));
        char buf[48];

        if (active || discovered) {
            snprintf(buf, sizeof(buf), "Count: %u", stats.current_count);
            ImU32 cc = active
                ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.9f, 0.8f, 1.0f))
                : stat_col;
            dl->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 50.0f), cc, buf);

            snprintf(buf, sizeof(buf), "Total: %u", stats.total_spawned);
            dl->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 64.0f), stat_col, buf);

            if (stats.lifetime_count > 0) {
                double avg_life = stats.lifetime_sum / stats.lifetime_count;
                float fps = (fps_display > 1.0f) ? fps_display : 60.0f;
                double avg_sec = avg_life / fps;
                char time_buf[24];
                if (avg_sec < 0.001)        snprintf(time_buf, sizeof(time_buf), "%.0f us", avg_sec * 1e6);
                else if (avg_sec < 1.0)     snprintf(time_buf, sizeof(time_buf), "%.0f ms", avg_sec * 1000.0);
                else if (avg_sec < 60.0)    snprintf(time_buf, sizeof(time_buf), "%.1fs", avg_sec);
                else                        snprintf(time_buf, sizeof(time_buf), "%.1fm", avg_sec / 60.0);
                snprintf(buf, sizeof(buf), "Life: %.0ff (%s)", avg_life, time_buf);
            } else {
                snprintf(buf, sizeof(buf), "Peak: %u", stats.peak_count);
            }
            dl->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 78.0f), stat_col, buf);
        }

        // Tooltip
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ecol, "%s (%s) — Z=%d", ELEMENT_NAMES[Z], ELEMENT_SYMBOLS[Z], Z);
            ImGui::Separator();
            ImGui::Text("Current: %u", stats.current_count);
            ImGui::Text("Total spawned: %u", stats.total_spawned);
            ImGui::Text("Peak: %u", stats.peak_count);
            if (stats.lifetime_count > 0) {
                double avg_life = stats.lifetime_sum / stats.lifetime_count;
                float fps = (fps_display > 1.0f) ? fps_display : 60.0f;
                double avg_sec = avg_life / fps;
                if (avg_sec < 1.0)
                    ImGui::Text("Avg lifetime: %.0f frames (%.0f ms)", avg_life, avg_sec * 1000.0);
                else
                    ImGui::Text("Avg lifetime: %.0f frames (%.1f s)", avg_life, avg_sec);
            }
            if (stats.current_count > 0)
                ImGui::Text("Avg energy: %.2f MeV", stats.energy_sum / stats.current_count);
            // Chirality info for common chiral-center atoms
            if (Z == 6 || Z == 14 || Z == 7 || Z == 15 || Z == 16) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f), "Chiral center atom");
                ImGui::TextWrapped("Can form chiral centers when bonded to 3+ "
                    "different substituent types in a molecule.");
            }
            ImGui::EndTooltip();
        }

        col++;
        if (col >= cols) col = 0;
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ── Molecule Bestiary Window ────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_molecule_bestiary() {
    if (!show_molecule_bestiary) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = std::min(740.0f, io.DisplaySize.x - 20.0f);
    float max_h = io.DisplaySize.y - 80.0f;
    ImVec2 win_size(win_w, max_h * 0.65f);

    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_MOLECULE_BESTIARY]);
        ImGui::SetNextWindowSize(tile_size_[TW_MOLECULE_BESTIARY]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 200), ImVec2(1000, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.06f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.04f, 0.07f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.07f, 0.11f, 0.15f, 0.95f));

    char title[64];
    snprintf(title, sizeof(title), "Molecule Bestiary (%d discovered)###MolBestiary",
             static_cast<int>(molecule_bestiary.size()));

    bool mb_open = ImGui::Begin(title, &show_molecule_bestiary);
    record_window_rect(TW_MOLECULE_BESTIARY);
    if (!mb_open) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }
    draw_minimize_button(TW_MOLECULE_BESTIARY);

    if (molecule_bestiary.empty()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const char* msg = "No molecules discovered yet";
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((avail.x - sz.x) * 0.5f, avail.y * 0.4f));
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.5f, 1.0f), "%s", msg);
        const char* hint = "Create bonds between atoms to discover molecules";
        ImVec2 hsz = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPosX((avail.x - hsz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.4f, 1.0f), "%s", hint);
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Sort by times_seen descending for display
    std::vector<size_t> sorted_idx(molecule_bestiary.size());
    for (size_t i = 0; i < sorted_idx.size(); ++i) sorted_idx[i] = i;
    std::sort(sorted_idx.begin(), sorted_idx.end(), [&](size_t a, size_t b) {
        return molecule_bestiary[a].times_seen > molecule_bestiary[b].times_seen;
    });

    const float cell_w = 120.0f;
    const float cell_h = 95.0f;
    const float padding = 4.0f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, static_cast<int>(avail_w / (cell_w + padding)));

    ImGui::BeginChild("##MolBestiaryScroll", ImVec2(0, 0), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int col = 0;

    for (size_t si = 0; si < sorted_idx.size(); ++si) {
        const auto& entry = molecule_bestiary[sorted_idx[si]];
        if (col > 0) ImGui::SameLine(0, padding);

        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImVec2 cell_min = cursor;
        ImVec2 cell_max = ImVec2(cursor.x + cell_w, cursor.y + cell_h);

        // Cell background
        dl->AddRectFilled(cell_min, cell_max,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.08f, 0.10f, 0.7f)),
                          3.0f);
        dl->AddRect(cell_min, cell_max,
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.3f, 0.4f, 0.4f)),
                    3.0f, 0, 1.0f);

        char iid[32];
        snprintf(iid, sizeof(iid), "##mb%zu", si);
        ImGui::InvisibleButton(iid, ImVec2(cell_w, cell_h));
        bool hovered = ImGui::IsItemHovered();

        // Formula (large, colored)
        {
            ImVec4 fcol(0.6f, 0.9f, 1.0f, 1.0f);
            ImVec2 sz = ImGui::CalcTextSize(entry.formula.c_str());
            float fx = cell_min.x + (cell_w - sz.x) * 0.5f;
            dl->AddText(ImVec2(fx, cell_min.y + 6.0f),
                        ImGui::ColorConvertFloat4ToU32(fcol),
                        entry.formula.c_str());
        }

        // Name (if known)
        if (!entry.name.empty()) {
            ImVec2 sz = ImGui::CalcTextSize(entry.name.c_str());
            float nx = cell_min.x + (cell_w - sz.x) * 0.5f;
            if (sz.x > cell_w - 4.0f) nx = cell_min.x + 2.0f;
            dl->AddText(ImVec2(nx, cell_min.y + 24.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.8f, 0.8f, 0.6f, 0.9f)),
                        entry.name.c_str());
        }

        // Chirality badge (top-right corner)
        if (entry.is_chiral) {
            const char* badge = "Chiral";
            ImVec2 bsz = ImGui::CalcTextSize(badge);
            float bx = cell_max.x - bsz.x - 4.0f;
            dl->AddRectFilled(ImVec2(bx - 2.0f, cell_min.y + 2.0f),
                              ImVec2(bx + bsz.x + 2.0f, cell_min.y + bsz.y + 4.0f),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.2f, 0.8f, 0.5f)), 2.0f);
            dl->AddText(ImVec2(bx, cell_min.y + 3.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.7f, 1.0f, 1.0f)), badge);
        }

        // Stats
        float sy = entry.name.empty() ? (cell_min.y + 28.0f) : (cell_min.y + 42.0f);
        ImU32 stat_col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
        char buf[48];

        snprintf(buf, sizeof(buf), "Seen: %u", entry.times_seen);
        dl->AddText(ImVec2(cell_min.x + 4.0f, sy), stat_col, buf);

        snprintf(buf, sizeof(buf), "Atoms: %u", entry.atom_count);
        dl->AddText(ImVec2(cell_min.x + 4.0f, sy + 14.0f), stat_col, buf);

        if (entry.first_seen_time > 0) {
            auto t = static_cast<time_t>(entry.first_seen_time);
            auto* tm = std::localtime(&t);
            char ts[24];
            std::strftime(ts, sizeof(ts), "%m/%d %H:%M", tm);
            dl->AddText(ImVec2(cell_min.x + 4.0f, sy + 28.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.5f, 0.8f)), ts);
        }

        // Tooltip
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "%s", entry.formula.c_str());
            if (!entry.name.empty())
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "%s", entry.name.c_str());
            ImGui::Separator();
            ImGui::Text("Times seen: %u", entry.times_seen);
            ImGui::Text("Atoms: %u", entry.atom_count);
            if (entry.first_seen_time > 0) {
                auto t = static_cast<time_t>(entry.first_seen_time);
                auto* tm = std::localtime(&t);
                char ts[48];
                std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
                ImGui::Text("Discovered: %s", ts);
            }
            ImGui::Text("Session: %u", entry.first_seen_session);
            if (entry.is_chiral) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 1.0f, 1.0f),
                    "Chiral (%u center%s)", entry.chiral_centers,
                    entry.chiral_centers == 1 ? "" : "s");
                ImGui::TextWrapped("Non-superimposable on its mirror image. "
                    "Chiral molecules are fundamental to biochemistry.");
            }
            ImGui::EndTooltip();
        }

        col++;
        if (col >= cols) col = 0;
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleColor(3);
}
