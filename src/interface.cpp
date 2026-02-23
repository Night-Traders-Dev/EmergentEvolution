#include "interface.h"
#include "types.h"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <random>
#include <algorithm>
#include <string>
#include <vector>
#include "backends/imgui_impl_vulkan.h"
#include "backends/imgui_impl_glfw.h"


void Interface::init() {
    std::random_device rd;
    seed_value = static_cast<int>(rd() % 32768);
}

// Helper for UI tooltips
static void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ── Force colour helper ───────────────────────────────────────────────────────

ImVec4 Interface::force_to_color(float f) {
    float abs_f = std::abs(f);
    glm::vec4 c = calc_force_button_color(f);
    return { c.r, c.g, c.b, 1.0f };
}

// ── Main ImGui render ─────────────────────────────────────────────────────────

void Interface::render_imgui(SimConfig&       cfg,
                              Particles&       particles,
                              OrganismManager& org_manager,
                              BondManager&     bond_manager,
                              bool&            request_reset)
{
    request_reset = false;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    if (ImGui::CollapsingHeader("Soft-Body Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Viscosity (Drag)", &cfg.viscosity_strength, 0.0f, 0.5f, "%.3f");
        ImGui::SameLine(); HelpMarker("Higher values make clusters move like thick liquid.");

        ImGui::SliderFloat("Pressure Resistance", &cfg.pressure_resistance, 0.0f, 50.0f, "%.1f");
        ImGui::SameLine(); HelpMarker("Prevents organisms from collapsing into a single point.");
    }

    if (!settings_visible) {
        ImGui::Render();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // ── Settings panel ────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(480, static_cast<float>(io.DisplaySize.y)), ImGuiCond_Always);
    ImGui::Begin("SIMULATION SETTINGS",
                 nullptr,
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove   |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    mouse_within = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    // ── Generation settings ───────────────────────────────────────────────────
    ImGui::SeparatorText("Generation");

    ImGui::SliderFloat("Count Slider", &particle_count_slider, 1.0f, 317.0f, "%.0f");
    int pc = static_cast<int>(std::max(2.0f, std::pow(particle_count_slider, 2.0f)));
    ImGui::Text("Particle Count:  %d", pc);
    cfg.particle_count = static_cast<uint32_t>(pc);

    ImGui::SliderFloat("Types Slider", &particle_types_slider, 1.0f, 10.0f, "%.0f");
    int pt = static_cast<int>(particle_types_slider);
    ImGui::Text("Particle Types:  %d", pt);
    cfg.particle_types = static_cast<uint32_t>(pt);

    ImGui::Checkbox("Reset Colors on Next Run",  &reset_colors_check);
    ImGui::Checkbox("Reset Forces on Next Run",  &reset_forces_check);
    cfg.reset_colors = reset_colors_check;
    cfg.reset_forces = reset_forces_check;

    ImGui::InputInt("Seed", &seed_value);
    seed_value = std::clamp(seed_value, 0, 65535);
    cfg.generation_seed = static_cast<uint32_t>(seed_value);

    if (ImGui::Button("Reset Simulation (F2)", ImVec2(-1, 0)))
        request_reset = true;

    // ── Real-time physics settings ────────────────────────────────────────────
    ImGui::SeparatorText("Real-time Physics");

    ImGui::SliderFloat("Dampening", &dampening_slider, 0.0f, 1.0f);
    ImGui::Text("Dampening:  %.2f", dampening_slider);
    cfg.dampening = dampening_slider;

    ImGui::SliderFloat("Repulsion Radius", &repulsion_slider, 1.0f, 400.0f);
    ImGui::Text("Repulsion Radius:  %d", static_cast<int>(repulsion_slider));
    cfg.repulsion_radius = repulsion_slider;

    ImGui::SliderFloat("Interaction Radius", &interaction_slider, 1.0f, 720.0f);
    ImGui::Text("Interaction Radius:  %d", static_cast<int>(interaction_slider));
    cfg.interaction_radius = interaction_slider;

    ImGui::SliderFloat("Density Limit", &density_limit_slider, 0.0f, 720.0f);
    ImGui::Text("Density Limit:  %d", static_cast<int>(density_limit_slider));
    cfg.density_limit = density_limit_slider;

    ImGui::SliderFloat("Particle Radius", &particle_radius_slider, 1.0f, 10.0f);
    ImGui::Text("Particle Radius:  %d", static_cast<int>(particle_radius_slider));
    cfg.radius = particle_radius_slider;

    // ── Bond parameters ───────────────────────────────────────────────────────
    ImGui::SeparatorText("Chemical Bonds");
    ImGui::SliderFloat("Bond Form Radius",   &cfg.bond_form_radius,   5.0f,  80.0f, "%.1f");
    ImGui::SliderFloat("Bond Rest Length",   &cfg.bond_rest_length,   5.0f,  60.0f, "%.1f");
    ImGui::SliderFloat("Bond Break Factor",  &cfg.bond_break_factor,  1.1f,   4.0f, "%.2f");
    ImGui::SliderFloat("Bond Spring k",      &cfg.bond_spring_k,      10.0f, 200.0f, "%.0f");

    // ── Glow ──────────────────────────────────────────────────────────────────
    ImGui::Checkbox("Glow Enabled (visual hint only)", &glow_enabled);

    // ── Particle force / color grid ───────────────────────────────────────────
    ImGui::SeparatorText("Particle Values");
    ImGui::TextDisabled("Hover + scroll: change force | Right-click: zero force");
    draw_particle_grid(cfg, particles);

    // ── Archetype panel ───────────────────────────────────────────────────────
    draw_archetype_panel(particles, cfg);

    // ── Organism panel ────────────────────────────────────────────────────────
    draw_organism_panel(org_manager, bond_manager, particles, cfg);

    ImGui::Separator();
    ImGui::TextDisabled("F1: toggle UI  |  F2: reset  |  Space: pause");
    ImGui::TextDisabled("Drag: pan  |  Scroll: zoom  |  ESC: quit");
    ImGui::TextDisabled("Night-Traders-Dev 2026");

    ImGui::End();

    ImGui::Render();
}

// ── Particle grid ─────────────────────────────────────────────────────────────

// Element symbol labels (indexed 0-7: H C N O P S Na Cl)
static const char* ATOM_SYMBOLS[8] = { "H", "C", "N", "O", "P", "S", "Na", "Cl" };

void Interface::draw_particle_grid(SimConfig& cfg, Particles& particles) {
    uint32_t pt = cfg.particle_types;

    // Safety – cap at MAX_PARTICLE_TYPES
    if (pt == 0) return;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;

    ImGuiIO& io = ImGui::GetIO();
    float available_w = ImGui::GetContentRegionAvail().x;
    float cell_size   = available_w / static_cast<float>(pt + 1);
    cell_size = std::max(cell_size, 12.0f);

    for (uint32_t row = 0; row <= pt; ++row) {
        for (uint32_t col = 0; col <= pt; ++col) {
            ImGui::PushID(static_cast<int>(row * (MAX_PARTICLE_TYPES + 1) + col));

            if (row == 0 && col == 0) {
                // Corner: empty placeholder
                ImGui::Dummy(ImVec2(cell_size, cell_size));
            }
            else if (row == 0) {
                // Top row: color pickers (column headers)
                uint32_t type_idx = col - 1;
                glm::vec4& c      = particles.colors[type_idx];
                float col_arr[4]  = { c.r, c.g, c.b, c.a };
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImVec2 btn_pos = ImGui::GetCursorScreenPos();
                if (ImGui::ColorButton("##col", ImVec4(c.r, c.g, c.b, c.a),
                                       ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(cell_size, cell_size)))
                {
                    // clicking opens the picker inline below
                }
                // Draw element symbol overlay
                if (type_idx < ATOM_COUNT) {
                    const char* sym = ATOM_SYMBOLS[type_idx];
                    // Choose contrasting text color (dark on light, light on dark)
                    float lum = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
                    ImU32 txt_col = (lum > 0.5f) ? IM_COL32(0,0,0,200) : IM_COL32(255,255,255,200);
                    ImVec2 txt_size = ImGui::CalcTextSize(sym);
                    ImVec2 txt_pos = ImVec2(btn_pos.x + (cell_size - txt_size.x) * 0.5f,
                                            btn_pos.y + (cell_size - txt_size.y) * 0.5f);
                    ImGui::GetWindowDrawList()->AddText(txt_pos, txt_col, sym);
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                    ImGui::OpenPopup("color_pick");
                }
                if (ImGui::BeginPopup("color_pick")) {
                    if (ImGui::ColorPicker4("##picker", col_arr,
                                            ImGuiColorEditFlags_NoSidePreview |
                                            ImGuiColorEditFlags_NoSmallPreview))
                    {
                        c = { col_arr[0], col_arr[1], col_arr[2], col_arr[3] };
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
            }
            else if (col == 0) {
                // Left column: row color swatch with element symbol
                uint32_t type_idx = row - 1;
                glm::vec4& c      = particles.colors[type_idx];
                ImVec2 btn_pos = ImGui::GetCursorScreenPos();
                ImGui::ColorButton("##row_col",
                    ImVec4(c.r, c.g, c.b, c.a),
                    ImGuiColorEditFlags_NoTooltip,
                    ImVec2(cell_size, cell_size));
                // Draw element symbol overlay
                if (type_idx < ATOM_COUNT) {
                    const char* sym = ATOM_SYMBOLS[type_idx];
                    float lum = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
                    ImU32 txt_col = (lum > 0.5f) ? IM_COL32(0,0,0,200) : IM_COL32(255,255,255,200);
                    ImVec2 txt_size = ImGui::CalcTextSize(sym);
                    ImVec2 txt_pos = ImVec2(btn_pos.x + (cell_size - txt_size.x) * 0.5f,
                                            btn_pos.y + (cell_size - txt_size.y) * 0.5f);
                    ImGui::GetWindowDrawList()->AddText(txt_pos, txt_col, sym);
                }
            }
            else {
                // Force matrix cell: type_a = (col-1), type_b = (row-1)
                uint32_t type_a = col - 1;
                uint32_t type_b = row - 1;
                uint32_t fi     = type_a + type_b * MAX_PARTICLE_TYPES;
                float&   force  = particles.forces[fi];

                ImVec4 fcolor = force_to_color(force);

                ImGui::PushStyleColor(ImGuiCol_Button,        fcolor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(fcolor.x * 1.2f, fcolor.y * 1.2f, fcolor.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(fcolor.x * 0.8f, fcolor.y * 0.8f, fcolor.z * 0.8f, 1.0f));

                char label[16];
                std::snprintf(label, sizeof(label), "##f%u_%u", type_a, type_b);
                ImGui::Button(label, ImVec2(cell_size, cell_size));

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Force: %.2f\nScroll to adjust\nRight-click to zero", force);

                    // Scroll wheel adjusts force
                    float scroll = io.MouseWheel;
                    if (scroll != 0.0f) {
                        force = std::clamp(force + scroll * 0.1f, -1.0f, 1.0f);
                    }
                    // Right-click zeros the force
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                        force = 0.0f;
                }

                ImGui::PopStyleColor(3);
            }

            ImGui::PopID();

            // Same-line for all but last column
            if (col < pt)
                ImGui::SameLine(0, 0);
        }
    }
}

// ── Archetype panel ───────────────────────────────────────────────────────────

void Interface::draw_archetype_panel(Particles& particles, const SimConfig& cfg) {
    if (!ImGui::CollapsingHeader("Particle Archetypes"))
        return;

    static const char* archetype_names[] = {
        "Default",
        "Repeller",
        "Polar",
        "Heavy",
        "Catalyst",
        "Adhesive",
        "Radical",
        "Donor",
        "Acceptor"
    };

    static const char* flag_names[] = {
        "REPEL",    // bit 0
        "POLAR",    // bit 1
        "HEAVY",    // bit 2
        "CATALYST", // bit 3
        "RADICAL",  // bit 4
        "ADHESIVE", // bit 5
        "DONOR",    // bit 6
        "ACCEPTOR", // bit 7
        "ION+",     // bit 8
        "ION-"      // bit 9
    };

    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;

    ImGui::TextDisabled("Set behavior archetype per particle type:");

    for (uint32_t t = 0; t < pt; ++t) {
        ImGui::PushID(static_cast<int>(t));

        const glm::vec4& c = particles.colors[t];
        ImGui::ColorButton("##swatch",
            ImVec4(c.r, c.g, c.b, 1.0f),
            ImGuiColorEditFlags_NoTooltip,
            ImVec2(14, 14));
        ImGui::SameLine();

        // Archetype combo
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::Combo("##arch", &archetype_selection[t], archetype_names, IM_ARRAYSIZE(archetype_names))) {
            switch (archetype_selection[t]) {
            case 0:  particles.apply_preset_default(t);       break;
            case 1:  particles.apply_preset_repeller(t);      break;
            case 2:  particles.apply_preset_polar(t, pt);     break;
            case 3:  particles.apply_preset_heavy(t);         break;
            case 4:  particles.apply_preset_catalyst(t);      break;
            case 5:  particles.apply_preset_adhesive(t);      break;
            case 6:  particles.apply_preset_radical(t);       break;
            case 7:  particles.apply_preset_donor(t);         break;
            case 8:  particles.apply_preset_acceptor(t);      break;
            }
        }

        // Show active behavior flags
        uint32_t flags = particles.behavior_flags[t];
        if (flags == BEHAVIOR_NONE) {
            ImGui::SameLine();
            ImGui::TextDisabled("──");
        } else {
            for (int bit = 0; bit < 10; ++bit) {
                if (flags & (1u << bit)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", flag_names[bit]);
                }
            }
        }

        ImGui::PopID();
    }
}


// ── Organism panel ────────────────────────────────────────────────────────────

void Interface::draw_organism_panel(OrganismManager& org_manager,
                                    const BondManager& bond_manager,
                                    const Particles& particles,
                                    const SimConfig& cfg)
{
    if (!ImGui::CollapsingHeader("Organisms"))
        return;

    ImGui::SliderFloat("Cluster Radius", &org_manager.cluster_radius, 10.0f, 200.0f, "%.0f");

    uint32_t org_count = static_cast<uint32_t>(org_manager.organisms.size());
    ImGui::Text("Active Organisms: %u", org_count);

    // Population statistics
    ImGui::Columns(2, "popstats", false);
    ImGui::TextDisabled("Alive");  ImGui::NextColumn();
    ImGui::TextDisabled("Dust");   ImGui::NextColumn();
    ImGui::Text("%u", org_manager.alive_count); ImGui::NextColumn();
    ImGui::Text("%u", org_manager.dust_count);  ImGui::NextColumn();
    ImGui::TextDisabled("Births"); ImGui::NextColumn();
    ImGui::TextDisabled("Deaths"); ImGui::NextColumn();
    ImGui::Text("%u", org_manager.last_births); ImGui::NextColumn();
    ImGui::Text("%u", org_manager.last_deaths); ImGui::NextColumn();
    ImGui::Columns(1);

    // Population history graph
    if (org_manager.pop_history_count > 0) {
        int hist_count  = static_cast<int>(
            std::min(org_manager.pop_history_count,
                     static_cast<uint32_t>(POP_HISTORY_LEN)));
        int hist_offset = (org_manager.pop_history_count < POP_HISTORY_LEN)
                          ? 0
                          : static_cast<int>(org_manager.pop_history_idx);
        ImGui::TextDisabled("Population history:");
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::PlotLines("##pophist",
                         org_manager.pop_history,
                         hist_count,
                         hist_offset,
                         nullptr,
                         0.0f,
                         static_cast<float>(cfg.particle_count),
                         ImVec2(-1.0f, 48.0f));
        ImGui::PopStyleColor();
    }

    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;

    ImGui::TextDisabled("Force Scales (trait feedback):");
    for (uint32_t t = 0; t < pt; ++t) {
        ImGui::PushID(static_cast<int>(t));
        const glm::vec4& c = particles.colors[t];
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(c.r, c.g, c.b, 1.0f));

        float frac = (particles.trait_scales[t] - 1.0f) / 0.8f;
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 6.0f), "");
        ImGui::SameLine();
        ImGui::Text("%.2fx", particles.trait_scales[t]);

        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    // Per-type population bar chart (shows even with no detected organisms)
    {
        uint32_t total = org_manager.alive_count + org_manager.dust_count;
        if (total > 0) {
            ImGui::TextDisabled("Population by type:");
            float bar_w = ImGui::GetContentRegionAvail().x;
            float bar_h = 14.0f;
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            float x = cursor.x;
            for (uint32_t t = 0; t < pt; ++t) {
                float frac = static_cast<float>(org_manager.type_populations[t]) /
                             static_cast<float>(total);
                if (frac <= 0.0f) continue;
                float w = frac * bar_w;
                const glm::vec4& c = particles.colors[t];
                ImU32 col = IM_COL32(static_cast<int>(c.r * 255),
                                     static_cast<int>(c.g * 255),
                                     static_cast<int>(c.b * 255), 220);
                dl->AddRectFilled(ImVec2(x, cursor.y),
                                  ImVec2(x + w, cursor.y + bar_h), col);
                x += w;
            }
            ImGui::Dummy(ImVec2(bar_w, bar_h));

            // Legend row
            for (uint32_t t = 0; t < pt; ++t) {
                if (org_manager.type_populations[t] == 0) continue;
                ImGui::PushID(static_cast<int>(t) + 100);
                const glm::vec4& c = particles.colors[t];
                ImGui::ColorButton("##lc", ImVec4(c.r, c.g, c.b, 1.0f),
                                   ImGuiColorEditFlags_NoTooltip, ImVec2(10, 10));
                ImGui::SameLine();
                ImGui::Text("%u", org_manager.type_populations[t]);
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::PopID();
            }
            ImGui::NewLine();
        }
    }

    if (org_count == 0) {
        ImGui::TextDisabled("(no organisms detected yet)");
        return;
    }

    // Sort by size
    std::vector<const Organism*> sorted;
    sorted.reserve(org_count);
    for (const auto& o : org_manager.organisms)
        sorted.push_back(&o);

    std::sort(sorted.begin(), sorted.end(),
              [](const Organism* a, const Organism* b) {
                  return a->traits.size > b->traits.size;
              });

    uint32_t show = std::min(org_count, 8u);
    ImGui::TextDisabled("Top organisms (by size):");

    // MoleculeClass label/color helpers
    auto mol_label = [](MoleculeClass m) -> const char* {
        switch (m) {
        case MoleculeClass::WATER:      return "H2O ";
        case MoleculeClass::LIPID:      return "LIPD";
        case MoleculeClass::AMINO_ACID: return "AACD";
        case MoleculeClass::NUCLEOTIDE: return "NUCL";
        case MoleculeClass::RADICAL:    return "RAD!";
        case MoleculeClass::POLYMER:    return "POLY";
        default:                        return "INRG";
        }
    };
    auto mol_color = [](MoleculeClass m) -> ImVec4 {
        switch (m) {
        case MoleculeClass::WATER:      return ImVec4(0.4f, 0.7f, 1.0f, 1.0f);  // light blue
        case MoleculeClass::LIPID:      return ImVec4(1.0f, 0.9f, 0.2f, 1.0f);  // yellow
        case MoleculeClass::AMINO_ACID: return ImVec4(0.3f, 1.0f, 0.5f, 1.0f);  // green
        case MoleculeClass::NUCLEOTIDE: return ImVec4(0.7f, 0.3f, 1.0f, 1.0f);  // violet
        case MoleculeClass::RADICAL:    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // red
        case MoleculeClass::POLYMER:    return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);  // orange
        default:                        return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // grey
        }
    };

    if (ImGui::BeginTable("orgtable", 9,
                          ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 18.0f);  // type swatch
        ImGui::TableSetupColumn("Mol",  ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Bnds", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn("Spd",  ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Gen",  ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Kll",  ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Div",  ImGuiTableColumnFlags_WidthFixed, 26.0f);
        ImGui::TableSetupColumn("Energy", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < show; ++i) {
            const Organism& o = *sorted[i];
            uint32_t dt = o.traits.dominant_type;
            const glm::vec4& c = (dt < MAX_PARTICLE_TYPES)
                                  ? particles.colors[dt]
                                  : glm::vec4(1.0f);

            ImGui::TableNextRow();

            // Type color swatch
            ImGui::TableSetColumnIndex(0);
            ImGui::ColorButton("##tc", ImVec4(c.r, c.g, c.b, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(14, 12));

            // Molecule class label
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, mol_color(o.traits.mol_class));
            ImGui::TextUnformatted(mol_label(o.traits.mol_class));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Charge:%.2f  Electroneg:%.2f\nReactivity:%.2f  BondStr:%.2f",
                    o.traits.avg_charge, o.traits.avg_electroneg,
                    o.traits.avg_reactivity, o.traits.avg_bond_strength);
            }

            // Size
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", o.traits.size);

            // Bond count
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", o.traits.bond_count);

            // Speed
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f", o.traits.avg_speed);

            // Generation
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u", o.traits.generation);

            // Kills
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%u", o.traits.kills);

            // Divisions
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%u", o.traits.divisions);

            // Energy bar
            ImGui::TableSetColumnIndex(8);
            float e = o.traits.energy;
            ImVec4 ecolor = ImVec4(1.0f - e, e, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ecolor);
            ImGui::ProgressBar(e, ImVec2(-1.0f, 12.0f), "");
            ImGui::PopStyleColor();
        }

        ImGui::EndTable();
    }
}

