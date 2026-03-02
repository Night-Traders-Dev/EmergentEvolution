#include "physics/interface.h"
#include "physics/phys_particles.h"
#include "physics/ui_data.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

// ── Tool panels ──────────────────────────────────────────────────────────────
// Split from interface.cpp: decay log, nuclear debug, accelerator panel,
// force object panel, measurement panel.

void PhysicsInterface::draw_decay_log() {
    if (!show_decay_log) return;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = 420.0f;
    float max_h = io.DisplaySize.y - 120.0f;
    float win_h = std::min(500.0f, max_h);
    ImVec2 win_size(win_w, win_h);

    ImGui::SetNextWindowPos(clamp_window_pos(
        ImVec2(io.DisplaySize.x * 0.5f - win_w * 0.5f,
               io.DisplaySize.y * 0.5f - win_h * 0.5f), win_size),
        ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(340, 200), ImVec2(560, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.06f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.10f, 0.03f, 0.95f));

    char title[64];
    snprintf(title, sizeof(title), "Event Log (%d)###DecayLog",
             static_cast<int>(decay_log.size()));

    if (!ImGui::Begin(title, &show_decay_log)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    // Event type labels and colors
    static const char* TYPE_LABELS[] = {
        "Decay", "Nuclear", "Fusion", "Fission", "Annihil.",
        "Photo-e", "Spall.", "Pair", "Pion", "VMD", "Photodis.",
        "Bond+", "Bond-", "Brems.", "Neutrino", "Weak"
    };
    static const ImVec4 TYPE_COLORS[] = {
        ImVec4(1.0f, 0.8f, 0.5f, 1.0f),   // PARTICLE_DECAY — warm yellow
        ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // NUCLEAR_DECAY — orange
        ImVec4(0.4f, 0.9f, 0.6f, 1.0f),   // FUSION — green
        ImVec4(1.0f, 0.6f, 0.2f, 1.0f),   // FISSION — amber
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f),   // ANNIHILATION — red
        ImVec4(0.3f, 0.7f, 1.0f, 1.0f),   // PHOTOELECTRIC — blue
        ImVec4(1.0f, 0.5f, 0.3f, 1.0f),   // SPALLATION — orange
        ImVec4(0.3f, 0.7f, 1.0f, 1.0f),   // PAIR_PRODUCTION — blue
        ImVec4(0.4f, 0.9f, 0.6f, 1.0f),   // PION_PRODUCTION — green
        ImVec4(0.9f, 0.3f, 0.9f, 1.0f),   // VMD — magenta
        ImVec4(0.8f, 0.6f, 1.0f, 1.0f),   // PHOTODISINTEGRATION — purple
        ImVec4(0.3f, 0.85f, 0.5f, 1.0f),  // BOND_FORMED — teal-green
        ImVec4(0.9f, 0.45f, 0.3f, 1.0f),  // BOND_BROKEN — warm red
        ImVec4(0.8f, 0.8f, 1.0f, 1.0f),   // BREMSSTRAHLUNG — pale blue
        ImVec4(0.5f, 1.0f, 0.8f, 1.0f),   // NEUTRINO — mint green
        ImVec4(0.7f, 0.8f, 1.0f, 1.0f),   // WEAK_SCATTER — light blue
    };

    // Summary counts by type
    int type_counts[DEVT_COUNT] = {};
    for (const auto& e : decay_log) {
        if (e.type < DEVT_COUNT) type_counts[e.type]++;
    }

    // Filter toggles — click type label to show/hide that event type
    float wrap_x = ImGui::GetContentRegionAvail().x - 60.0f;
    float cur_x = 0.0f;
    for (int t = 0; t < DEVT_COUNT; ++t) {
        if (type_counts[t] == 0) continue;
        char tag_label[32];
        snprintf(tag_label, sizeof(tag_label), "%s:%d###filt%d", TYPE_LABELS[t], type_counts[t], t);
        float btn_w = ImGui::CalcTextSize(tag_label).x + 10.0f;
        if (cur_x + btn_w > wrap_x && cur_x > 0.0f) {
            cur_x = 0.0f;  // wrap to next line
        } else if (cur_x > 0.0f) {
            ImGui::SameLine(0, 4);
        }
        ImVec4 col = TYPE_COLORS[t];
        if (!event_filter[t]) col = ImVec4(col.x * 0.3f, col.y * 0.3f, col.z * 0.3f, 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.2f, col.y * 0.2f, col.z * 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (ImGui::SmallButton(tag_label)) {
            event_filter[t] = !event_filter[t];
            expanded_event_idx = -1;
        }
        ImGui::PopStyleColor(3);
        cur_x += btn_w + 4.0f;
    }
    if (decay_log.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "No events yet");
    }

    ImGui::Spacing();

    // Clear button
    if (!decay_log.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 0.5f));
        if (ImGui::SmallButton("Clear")) {
            decay_log.clear();
            expanded_event_idx = -1;
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // If scrolling to a specific event, ensure its type filter is enabled
    if (scroll_to_event_idx >= 0 && scroll_to_event_idx < static_cast<int>(decay_log.size())) {
        auto t = decay_log[scroll_to_event_idx].type;
        if (t < DEVT_COUNT) event_filter[t] = true;
    }

    // Scrollable event list (newest at top)
    ImGui::BeginChild("##DecayLogScroll", ImVec2(0, 0), false);

    for (int idx = static_cast<int>(decay_log.size()) - 1; idx >= 0; --idx) {
        const auto& entry = decay_log[idx];
        if (entry.type < DEVT_COUNT && !event_filter[entry.type]) continue;
        bool is_expanded = (expanded_event_idx == idx);
        bool has_details = !entry.details.empty();

        ImGui::PushID(idx);

        // Clickable row
        char row_label[384];
        {
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &entry.timestamp);
#else
            localtime_r(&entry.timestamp, &tm_buf);
#endif
            const char* tag = (entry.type < DEVT_COUNT) ? TYPE_LABELS[entry.type] : "?";
            const char* arrow = has_details ? (is_expanded ? "v " : "> ") : "  ";
            snprintf(row_label, sizeof(row_label), "%s%02d:%02d:%02d  [%-8s]  %s",
                     arrow,
                     tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                     tag, entry.description.c_str());
        }

        ImVec4 tag_color = (entry.type < DEVT_COUNT) ? TYPE_COLORS[entry.type] : ImVec4(1,1,1,1);
        ImGui::PushStyleColor(ImGuiCol_Text, entry.color);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(tag_color.x * 0.15f, tag_color.y * 0.15f,
                                                       tag_color.z * 0.15f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(tag_color.x * 0.25f, tag_color.y * 0.25f,
                                                              tag_color.z * 0.25f, 0.6f));
        // Scroll to this event if requested by notification click
        if (scroll_to_event_idx == idx) {
            ImGui::SetScrollHereY(0.3f);
            scroll_to_event_idx = -1;
        }

        if (ImGui::Selectable(row_label, is_expanded, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
            if (has_details)
                expanded_event_idx = is_expanded ? -1 : idx;
        }
        ImGui::PopStyleColor(3);

        // Show detail text when expanded
        if (is_expanded && has_details) {
            ImGui::Indent(20.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.75f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 10.0f);
            ImGui::TextWrapped("%s", entry.details.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Unindent(20.0f);
            ImGui::Spacing();
        }

        ImGui::PopID();
    }

    // Auto-scroll to top (newest) when new events arrive
    if (ImGui::GetScrollY() < 10.0f)
        ImGui::SetScrollHereY(0.0f);

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Nuclear Reactions Debug Window ──────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_nuclear_debug(SimConfig& cfg) {
    if (!show_nuclear_debug) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 nuc_size(320, 540);
    ImGui::SetNextWindowPos(clamp_window_pos(
        ImVec2(io.DisplaySize.x - 680, 60), nuc_size), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(nuc_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 300), ImVec2(500, 800));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.07f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.14f, 0.08f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.22f, 0.12f, 0.03f, 0.95f));

    if (!ImGui::Begin("Nuclear Reactions###NuclearDebug", &show_nuclear_debug)) {
        wobble_window(4.0f);
        ImGui::End();
        ImGui::PopStyleColor(3);
        return;
    }

    float w = ImGui::GetContentRegionAvail().x;

    // ── Reaction Toggles ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Reaction Toggles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Fusion", &cfg.fusion_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Fission", &cfg.fission_enabled);

        ImGui::Checkbox("Spallation", &cfg.spallation_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Compton", &cfg.compton_enabled);

        ImGui::Checkbox("Annihilation", &cfg.annihilation_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Decay", &cfg.decay_enabled);

        ImGui::Checkbox("Nuclear Decay", &cfg.nuclear_decay_enabled);
        ImGui::SameLine(w * 0.5f);
        ImGui::Checkbox("Virtual Pairs", &cfg.virtual_pairs_enabled);
    }

    ImGui::Spacing();

    // ── Fusion ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fusion")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Coulomb Barrier (keV)##fus", &cfg.fusion_threshold_keV, 0.0f, 2000.0f, "%.0f keV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gamow peak barrier energy.\n550 keV = p+p (solar core).\nLower = easier fusion.");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Radius (px)##fus", &cfg.fusion_radius, 2.0f, 30.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Collision distance for fusion check.\nDefault: 8 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##fus", &cfg.max_fusions_per_frame, 1, 20);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum fusion events per tick.\nDefault: 5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Energy##fus", &cfg.fusion_min_energy, 0.01f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min particle energy to participate.\nDefault: 0.1");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("p+n Min KE (keV)##fus", &cfg.fusion_pn_min_ke_keV, 0.1f, 100.0f, "%.1f keV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min CM kinetic energy for p+n capture.\nDefault: 1.0 keV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Binding Energy (MeV)##fus", &cfg.fusion_binding_mev, 0.5f, 10.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deuteron binding Q-value.\nDefault: 2.22 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Leptonic Q (MeV)##fus", &cfg.fusion_leptonic_q_mev, 0.0f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("p+p chain e+ + v energy.\nDefault: 0.42 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Product Separation##fus", &cfg.fusion_product_separation, 1.0f, 10.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Nucleon pair separation distance.\nDefault: 3.0 px");
    }

    // ── Fission ──────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Fission")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Neutron Threshold##fis", &cfg.fission_neutron_threshold, 0.1f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum neutron energy to trigger fission.\nDefault: 0.6");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Min Cluster Size##fis", &cfg.min_fission_cluster, 2, 100);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum nucleons in target nucleus.\nDefault: 20");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##fis", &cfg.max_fissions_per_frame, 1, 10);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum fission events per tick.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Cluster Radius (px)##fis", &cfg.fission_cluster_radius, 4.0f, 30.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Search radius for nucleons.\nDefault: 12.0 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Barrier (MeV)##fis", &cfg.fission_barrier_mev, 0.1f, 20.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min neutron KE to trigger fission.\nU-235 barrier: ~5.75 MeV\nDefault: 5.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Min Protons##fis", &cfg.fission_min_protons, 1, 40);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min protons for Coulomb instability.\nDefault: 8");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Fissility (Z\xC2\xB2/A)##fis", &cfg.fission_fissility_threshold, 1.0f, 50.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bohr-Wheeler fissility threshold.\nZ\xC2\xB2/A must exceed this for fission.\nU-235: 36.0, C-12: 3.0, Fe-56: 12.1\nDefault: 35.0 (real physics)");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Energy/Nucleon (MeV)##fis", &cfg.fission_energy_per_nucleon, 0.1f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("MeV released per nucleon.\nDefault: 1.0");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Fragment Energy (MeV)##fis", &cfg.fission_fragment_energy_mev, 0.1f, 5.0f, "%.2f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Energy boost per fragment.\nDefault: 1.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Free Neutrons Min##fis", &cfg.fission_free_neutrons_min, 0, 5);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Min chain-reaction neutrons.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Free Neutrons Max##fis", &cfg.fission_free_neutrons_max, 1, 8);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max chain-reaction neutrons.\nDefault: 3");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Neutron KE (MeV)##fis", &cfg.fission_neutron_ke_mev, 0.5f, 10.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("KE of ejected neutrons.\nDefault: 2.0 MeV");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Spawn Radius (px)##fis", &cfg.fission_spawn_radius, 1.0f, 20.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Neutron spawn distance.\nDefault: 5.0 px");
    }

    // ── Particle Decay ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Particle Decay")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Energy Threshold##pdec", &cfg.decay_threshold, 0.01f, 0.50f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Particle decays when energy drops below this.\nDefault: 0.08\nLower = particles live longer before decaying.");
    }

    // ── Nuclear Decay ────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Nuclear Decay")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Rate Multiplier##ndec", &cfg.decay_rate_multiplier, 0.01f, 10.0f, "%.2fx",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scales all nuclear half-lives.\n< 1 = faster decay, > 1 = slower.\nDefault: 1.0");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Alpha KE (MeV)##ndec", &cfg.alpha_ke_mev, 0.5f, 20.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Kinetic energy of emitted alpha particles.\nDefault: 5.0 MeV (typical: 4-6 MeV)");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Nucleon KE (MeV)##ndec", &cfg.nucleon_emit_ke_mev, 0.1f, 10.0f, "%.1f MeV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Kinetic energy of emitted neutrons/protons.\nDefault: 2.0 MeV (typical: 1-3 MeV)");
    }

    // ── Compton / Photoelectric ──────────────────────────────────────────
    if (ImGui::CollapsingHeader("Compton / Photoelectric")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Radius (px)##comp", &cfg.compton_radius, 5.0f, 60.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Photon-electron interaction distance.\nDefault: 25 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##comp", &cfg.max_compton_per_frame, 1, 20);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum Compton interactions per tick.\nDefault: 8");
    }

    // ── Spallation ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Spallation")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Speed (px/f)##spal", &cfg.spallation_min_speed, 20.0f, 300.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum projectile speed to trigger spallation.\nDefault: 120 px/frame");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Min Energy##spal", &cfg.spallation_min_energy, 0.1f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum projectile energy.\nDefault: 0.5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("Max/frame##spal", &cfg.max_spallations_per_frame, 1, 10);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum spallation events per tick.\nDefault: 3");
    }

    // ── Annihilation ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Annihilation")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Contact Radius (px)##annih", &cfg.annihilation_radius, 1.0f, 20.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Matter-antimatter annihilation distance.\nDefault: 5.0 px");
    }

    // ── Virtual Pairs / Casimir ─────────────────────────────────────────
    if (ImGui::CollapsingHeader("Virtual Pairs")) {
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Vacuum Energy##vpair", &cfg.vacuum_energy, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vacuum fluctuation rate.\nHigher = more spontaneous pairs.\n0 = off. Default: 0.5");

        ImGui::SetNextItemWidth(-1);
        int vp_max = static_cast<int>(cfg.virtual_pair_max_per_tick);
        if (ImGui::SliderInt("Max/frame##vpair", &vp_max, 1, 10))
            cfg.virtual_pair_max_per_tick = static_cast<uint32_t>(vp_max);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Maximum virtual pairs spawned per tick.\nDefault: 2");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Casimir Radius##vpair", &cfg.casimir_radius, 5.0f, 60.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Detection range for Casimir force.\nDefault: 30 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Casimir Strength##vpair", &cfg.casimir_strength, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Casimir attractive force scaling.\n0 = no Casimir effect.\nDefault: 0.5");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Pair Scatter##vpair", &cfg.virtual_pair_scatter, 1.0f, 10.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawn separation of virtual pair.\nDefault: 3.0 px");
    }

    // ── Covalent Bonds ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Covalent Bonds")) {
        ImGui::Checkbox("Bonds Enabled##cbond", &cfg.bonds_enabled);

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Spring K##cbond", &cfg.bond_spring_k, 10.0f, 2000.0f, "%.0f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bond spring constant (Hooke's law).\nHigher = stiffer bonds.\nDefault: 500");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Rest Length (px)##cbond", &cfg.bond_rest_length, 8.0f, 80.0f, "%.1f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Equilibrium bond length.\nMust exceed nuclear radii + 10px.\nDefault: 36 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Break Factor##cbond", &cfg.bond_break_factor, 1.5f, 5.0f, "%.1fx");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bond breaks at distance > rest * factor.\nDefault: 2.2x");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Form Radius (px)##cbond", &cfg.bond_form_radius, 15.0f, 100.0f, "%.0f px");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max distance for new bond formation.\nDefault: 44 px");

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Activation Energy##cbond", &cfg.bond_activation_energy, 0.001f, 0.2f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max relative velocity for bonding (too fast = scattering).\nDefault: 0.02");

        // Count active bonds
        uint32_t active_bonds = 0;
        if (bond_data_ptr && bond_data_count > 0) {
            uint32_t max_p = bond_data_count / MAX_BONDS_PER_PARTICLE;
            for (uint32_t bi = 0; bi < max_p; ++bi) {
                for (uint32_t s = 0; s < MAX_BONDS_PER_PARTICLE; ++s) {
                    uint32_t partner = bond_data_ptr[bi * MAX_BONDS_PER_PARTICLE + s];
                    if (partner != 0xFFFFFFFFu && bi < partner) active_bonds++;
                }
            }
        }
        ImGui::TextColored(ImVec4(0.4f, 0.65f, 1.0f, 1.0f), "Active bonds: %u", active_bonds);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Reset to Defaults ────────────────────────────────────────────────
    if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        cfg.fusion_threshold_keV      = 550.0f;
        cfg.fusion_radius             = 8.0f;
        cfg.max_fusions_per_frame     = 5;
        cfg.fission_neutron_threshold = 0.6f;
        cfg.min_fission_cluster       = 20;
        cfg.max_fissions_per_frame    = 2;
        cfg.fission_barrier_mev       = 5.0f;
        cfg.fission_min_protons       = 8;
        cfg.fission_fissility_threshold = 35.0f;
        cfg.decay_threshold           = 0.08f;
        cfg.alpha_ke_mev              = 5.0f;
        cfg.nucleon_emit_ke_mev       = 2.0f;
        cfg.decay_rate_multiplier     = 1.0f;
        cfg.compton_radius            = 25.0f;
        cfg.max_compton_per_frame     = 8;
        cfg.spallation_min_speed      = 120.0f;
        cfg.spallation_min_energy     = 0.5f;
        cfg.max_spallations_per_frame = 3;
        cfg.annihilation_radius       = 5.0f;
        cfg.virtual_pair_threshold    = 2.1f;
        cfg.virtual_pair_max_per_tick = 2;

        cfg.fusion_enabled            = true;
        cfg.fission_enabled           = true;
        cfg.spallation_enabled        = true;
        cfg.compton_enabled           = true;
        cfg.annihilation_enabled      = true;
        cfg.decay_enabled             = true;
        cfg.nuclear_decay_enabled     = true;
        cfg.virtual_pairs_enabled     = true;

        cfg.bonds_enabled             = true;
        cfg.bond_spring_k             = 500.0f;
        cfg.bond_rest_length          = 36.0f;
        cfg.bond_break_factor         = 2.2f;
        cfg.bond_form_radius          = 44.0f;
        cfg.bond_activation_energy    = 0.02f;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset all nuclear reaction parameters to Standard Model defaults");

    wobble_window(4.0f);
    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Particle Accelerator Panel ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_accelerator_panel() {
    ImGuiIO& io = ImGui::GetIO();
    float max_h = io.DisplaySize.y - 64.0f;

    ImVec2 accel_size(300, std::min(420.0f, max_h));
    ImGui::SetNextWindowPos(clamp_window_pos(
        ImVec2(io.DisplaySize.x - 650, 10), accel_size), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(accel_size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(340, max_h));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.04f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.18f, 0.06f, 0.03f, 0.95f));

    bool panel_open = accel_mode;
    if (!ImGui::Begin("Particle Accelerator", &panel_open)) {
        ImGui::End();
        ImGui::PopStyleColor(3);
        if (!panel_open) {
            accel_mode = false;
            accel_phase = 0;
            accel_source_idx = -1;
            accel_stream_timer = 0;
            accel_free_origin_set = false;
        }
        return;
    }
    if (!panel_open) {
        accel_mode = false;
        accel_phase = 0;
        accel_source_idx = -1;
        accel_stream_timer = 0;
        accel_free_origin_set = false;
    }

    // ── Status ──
    if (accel_phase == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Click a particle to set as target");
        ImGui::SameLine();
        if (ImGui::SmallButton("Skip##notgt")) {
            accel_phase = 1;
            accel_source_idx = -1;  // free-fire mode (no target)
            accel_free_origin_set = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Fire without a target\n(click to place origin, then click to aim)");
    } else if (accel_source_idx >= 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Click to fire at target!");
        ImGui::SameLine();
        if (ImGui::SmallButton("Change Target")) {
            accel_phase = 0;
            accel_source_idx = -1;
            accel_free_origin_set = false;
        }
    } else if (!accel_free_origin_set) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Click to place fire origin");
        ImGui::SameLine();
        if (ImGui::SmallButton("Set Target")) {
            accel_phase = 0;
            accel_source_idx = -1;
            accel_free_origin_set = false;
        }
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Click to aim and fire!");
        ImGui::SameLine();
        if (ImGui::SmallButton("Move Origin")) {
            accel_free_origin_set = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Set Target")) {
            accel_phase = 0;
            accel_source_idx = -1;
            accel_free_origin_set = false;
        }
    }

    ImGui::Separator();

    // ── Projectile Type ──
    if (ImGui::CollapsingHeader("Projectile", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Common types in rows
        static const int ROW1[] = { PROTON_TYPE, NEUTRON_TYPE, ELECTRON_TYPE_PHYS,
                                     PHOTON_TYPE_PHYS, POSITRON_TYPE_PHYS, NEUTRINO_TYPE_PHYS };
        static const int ROW2[] = { MUON_TYPE_PHYS, ANTIMUON_TYPE_PHYS,
                                     UP_QUARK_TYPE, DOWN_QUARK_TYPE,
                                     GLUON_TYPE_PHYS, HIGGS_TYPE_PHYS };

        auto draw_type_row = [&](const int* types, int count) {
            for (int r = 0; r < count; ++r) {
                int t = types[r];
                ImVec4 col = PHYS_TYPE_UI_COLORS[t];
                bool selected = (accel_fire_type == t);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x*0.6f, col.y*0.6f, col.z*0.6f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x*0.7f, col.y*0.7f, col.z*0.7f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, col);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x*0.25f, col.y*0.25f, col.z*0.25f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x*0.4f, col.y*0.4f, col.z*0.4f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0,0,0,0));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                }
                char id[32];
                snprintf(id, sizeof(id), "%s##acc%d", PHYS_TYPE_LABELS[t], t);
                if (ImGui::Button(id, ImVec2(38, 24)))
                    accel_fire_type = t;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", PHYS_TYPE_NAMES[t]);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (r < count - 1) ImGui::SameLine(0, 4);
            }
        };

        draw_type_row(ROW1, 6);
        draw_type_row(ROW2, 6);

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Selected: %s",
                           PHYS_TYPE_NAMES[accel_fire_type]);
    }

    // ── Speed ──
    if (ImGui::CollapsingHeader("Speed", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Show slider as fraction of c (0.03c – 1.00c)
        float beta = accel_speed / C_SIM;
        char slider_label[32];
        snprintf(slider_label, sizeof(slider_label), "%.3fc", beta);
        ImGui::SliderFloat("##AccelSpeed", &accel_speed, 10.0f, C_SIM, slider_label);

        // Real-world speed annotation
        char spd_buf[32];
        fmt_speed(spd_buf, sizeof(spd_buf), accel_speed);
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.9f, 1.0f), "%s", spd_buf);

        // Massless particles always travel at c — note this
        float m0 = rest_mass_MeV(accel_fire_type);
        if (m0 < 0.001f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "(massless: always c)");
        }
    }

    // ── Fire Mode ──
    if (ImGui::CollapsingHeader("Fire Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::RadioButton("Single Shot",  &accel_fire_mode, 0);
        ImGui::RadioButton("Triple Shot",  &accel_fire_mode, 1);
        ImGui::RadioButton("Stream",       &accel_fire_mode, 2);
        if (accel_fire_mode == 2) {
            int interval = static_cast<int>(accel_stream_interval);
            ImGui::SliderInt("Rate##stream", &interval, 1, 10, "every %d frames");
            accel_stream_interval = static_cast<uint32_t>(interval);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Force Object Panel ──────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_force_object_panel(ForceObject* objects) {
    if (selected_force_obj_idx < 0) return;
    int idx = selected_force_obj_idx;
    if (idx >= static_cast<int>(MAX_FORCE_OBJECTS) || !objects[idx].active) {
        selected_force_obj_idx = -1;
        return;
    }

    ForceObject& obj = objects[idx];

    static const char* fo_type_names[] = {
        "EM Field", "Strong Force", "Weak Force", "Gravity Well", "Heat Source",
        "Mirror", "Coulomb Source", "Vortex", "Potential Well"
    };
    static const ImVec4 fo_type_colors[] = {
        ImVec4(0.3f, 0.5f, 1.0f, 1.0f),   // EM
        ImVec4(0.3f, 0.9f, 0.4f, 1.0f),   // Strong
        ImVec4(0.7f, 0.3f, 0.9f, 1.0f),   // Weak
        ImVec4(0.9f, 0.7f, 0.2f, 1.0f),   // Gravity
        ImVec4(1.0f, 0.4f, 0.2f, 1.0f),   // Heat
        ImVec4(0.7f, 0.7f, 0.8f, 1.0f),   // Mirror
        ImVec4(0.9f, 0.3f, 0.3f, 1.0f),   // Coulomb
        ImVec4(0.2f, 0.8f, 0.9f, 1.0f),   // Vortex
        ImVec4(0.9f, 0.9f, 0.2f, 1.0f),   // Well
    };

    const char* type_name = (obj.force_type < FORCE_OBJ_COUNT) ? fo_type_names[obj.force_type] : "Unknown";
    ImVec4 type_color = (obj.force_type < FORCE_OBJ_COUNT) ? fo_type_colors[obj.force_type] : ImVec4(1,1,1,1);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(clamp_window_pos(
        ImVec2(io.DisplaySize.x - 260, 10), ImVec2(250, 300)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("##ForceObjPanel", nullptr, panel_flags)) {
        // Header
        ImGui::TextColored(type_color, "%s", type_name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "#%d", idx);
        ImGui::Separator();

        float col_w = 90.0f;
        if (obj.force_type == FORCE_OBJ_MIRROR) {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Endpoint 1");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj.x, obj.y);
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Endpoint 2");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj._pad0, obj._pad1);

            ImGui::Spacing();
            ImGui::TextColored(type_color, "Elasticity");
            ImGui::SliderFloat("##fo_str", &obj.strength, 0.0f, 1.0f, "%.2f");

            ImGui::TextColored(type_color, "Thickness");
            ImGui::SliderFloat("##fo_rad", &obj.radius, 1.0f, 20.0f, "%.1f");
        } else {
            ImGui::TextColored(ImVec4(0.451f, 0.478f, 0.580f, 1.0f), "Position");
            ImGui::SameLine(col_w);
            ImGui::Text("%.0f, %.0f", obj.x, obj.y);

            ImGui::Spacing();
            ImGui::TextColored(type_color, "Strength");
            ImGui::SliderFloat("##fo_str", &obj.strength, 0.1f, 10.0f, "%.2f");

            ImGui::TextColored(type_color, "Radius");
            ImGui::SliderFloat("##fo_rad", &obj.radius, 10.0f, 200.0f, "%.0f");
        }

        // Action buttons
        ImGui::Spacing();
        ImGui::Separator();

        if (force_obj_move_mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.80f));
            if (ImGui::Button("Moving...", ImVec2(72, 26))) {
                force_obj_move_mode = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Click to place");
        } else {
            if (ImGui::Button("Move", ImVec2(72, 26))) {
                force_obj_move_mode = true;
            }
        }
        if (!force_obj_move_mode) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.2f, 0.2f, 0.90f));
            if (ImGui::Button("Delete", ImVec2(72, 26))) {
                obj.active = 0;
                selected_force_obj_idx = -1;
                force_obj_move_mode = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(72, 26))) {
                selected_force_obj_idx = -1;
                force_obj_move_mode = false;
            }
        }
    }
    ImGui::End();
}

// ══════════════════════════════════════════════════════════════════════════════
// ── Measurement Panel ───────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

void PhysicsInterface::draw_measurement_panel() {
    ImGuiIO& io = ImGui::GetIO();
    float panel_w = 280.0f;
    float max_h = io.DisplaySize.y - 64.0f;

    ImGui::SetNextWindowPos(clamp_window_pos(
        ImVec2(io.DisplaySize.x - panel_w - 10, 10), ImVec2(panel_w, 200)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(200, 80), ImVec2(400, max_h));
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin("Measurements", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // ── Thermometer probes ───────────────────────────────────────────────
    if (!thermo_probes.empty() && ImGui::CollapsingHeader("Thermometer Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(thermo_probes.size()); ++i) {
            auto& p = thermo_probes[i];
            ImGui::PushID(i);
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Probe %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            ImGui::Text("  T: %.0f K  (%u particles)", p.local_temp, p.local_count);
            ImGui::SliderFloat("Radius", &p.radius, 20.0f, 300.0f, "%.0f");
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            thermo_probes.erase(thermo_probes.begin() + remove_idx);
    }

    // ── Velocity meters ──────────────────────────────────────────────────
    if (!velocity_meters.empty() && ImGui::CollapsingHeader("Velocity Meters", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(velocity_meters.size()); ++i) {
            auto& vm = velocity_meters[i];
            ImGui::PushID(1000 + i);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Particle #%d", vm.particle_idx);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            if (vm.active && readback_velocities && static_cast<uint32_t>(vm.particle_idx) < readback_count) {
                glm::vec2 v = readback_velocities[vm.particle_idx];
                float speed = glm::length(v);
                char spd_buf[32];
                fmt_speed(spd_buf, sizeof(spd_buf), speed);
                ImGui::Text("  Speed: %s", spd_buf);
            }
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            velocity_meters.erase(velocity_meters.begin() + remove_idx);
    }

    // ── Distance rulers ──────────────────────────────────────────────────
    if (!distance_rulers.empty() && ImGui::CollapsingHeader("Distance Rulers", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(distance_rulers.size()); ++i) {
            auto& r = distance_rulers[i];
            ImGui::PushID(2000 + i);
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "Ruler %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            float nm = r.distance * WORLD_TO_NM;
            if (nm < 0.01f)
                ImGui::Text("  Distance: %.2f pm", nm * 1000.0f);
            else if (nm < 1.0f)
                ImGui::Text("  Distance: %.3f nm", nm);
            else
                ImGui::Text("  Distance: %.2f nm", nm);
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            distance_rulers.erase(distance_rulers.begin() + remove_idx);
    }

    // ── Density counters ─────────────────────────────────────────────────
    if (!density_counters.empty() && ImGui::CollapsingHeader("Density Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove_idx = -1;
        for (int i = 0; i < static_cast<int>(density_counters.size()); ++i) {
            auto& dc = density_counters[i];
            ImGui::PushID(3000 + i);
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Counter %d", i + 1);
            ImGui::SameLine(200);
            if (ImGui::SmallButton("X")) remove_idx = i;
            ImGui::Text("  Count: %u  Density: %.4f", dc.count, dc.density);
            ImGui::SliderFloat("Radius", &dc.radius, 20.0f, 300.0f, "%.0f");
            ImGui::PopID();
            ImGui::Separator();
        }
        if (remove_idx >= 0)
            density_counters.erase(density_counters.begin() + remove_idx);
    }

    // ── Clear All ────────────────────────────────────────────────────────
    ImGui::Spacing();
    if (ImGui::Button("Clear All", ImVec2(-1, 0))) {
        thermo_probes.clear();
        velocity_meters.clear();
        distance_rulers.clear();
        density_counters.clear();
    }

    ImGui::End();
}
