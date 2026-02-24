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
        draw_hover_tooltip(org_manager, particles, bond_manager);
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

    // ── Header: FPS + active particle count ──────────────────────────────────
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
        "%.0f fps  |  %u active  |  %u bonds",
        fps_display, active_particle_display, total_bonds_display);
    ImGui::SameLine();
    ImGui::TextDisabled("  %.2f avg E", avg_energy_display);

    // ── Generation settings ───────────────────────────────────────────────────
    ImGui::SeparatorText("Generation");

    if (!cfg.start_empty) {
        ImGui::SliderFloat("Count Slider", &particle_count_slider, 1.0f, 317.0f, "%.0f");
        int pc = static_cast<int>(std::max(2.0f, std::pow(particle_count_slider, 2.0f)));
        cfg.particle_count = static_cast<uint32_t>(pc);
        ImGui::Text("Particle Count:  %d", pc);
    } else {
        cfg.particle_count = 10000;  // fixed lab capacity — no slider needed
    }

    ImGui::SliderFloat("Types Slider", &particle_types_slider, 1.0f, 18.0f, "%.0f");
    int pt = static_cast<int>(particle_types_slider);
    ImGui::Text("Particle Types:  %d", pt);
    cfg.particle_types = static_cast<uint32_t>(pt);

    ImGui::Checkbox("Reset Colors on Next Run",  &reset_colors_check);
    ImGui::Checkbox("Reset Forces on Next Run",  &reset_forces_check);
    cfg.reset_colors = reset_colors_check;
    cfg.reset_forces = reset_forces_check;
    ImGui::SliderFloat("Force Randomness", &cfg.force_randomness, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine(); HelpMarker(
        "Blends random variation into chemistry force defaults.\n"
        "0 = pure chemistry  \xc2\xb7  1 = pure random\n"
        "0.2\xe2\x80\x930.4 recommended for emergent behaviour.");

    ImGui::InputInt("Seed", &seed_value);
    seed_value = std::clamp(seed_value, 0, 65535);
    cfg.generation_seed = static_cast<uint32_t>(seed_value);

    ImGui::Checkbox("Start Empty  (F3 lab mode)", &cfg.start_empty);
    ImGui::SameLine(); HelpMarker(
        "Start with an empty world — no particles visible.\n"
        "Use F3 to place atoms, molecules, and bio-molecules freely.\n"
        "Particle Types controls how many types are available in the force matrix.");

    if (ImGui::Button("Reset Simulation (F2)", ImVec2(-1, 0)))
        request_reset = true;

    // ── Real-time physics settings ────────────────────────────────────────────
    ImGui::SeparatorText("Real-time Physics");

    ImGui::SliderFloat("Dampening", &dampening_slider, 0.0f, 1.0f);
    ImGui::Text("Dampening:  %.2f", dampening_slider);
    cfg.dampening = dampening_slider;

    ImGui::SliderFloat("Temperature",   &cfg.temperature,      0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Gravity",       &cfg.gravity_strength, 0.0f, 5.0f, "%.4f");
    ImGui::SliderFloat("Magnetism",     &cfg.lorentz_strength, 0.0f, 2.0f, "%.4f");
    ImGui::SliderFloat("Vacuum Energy", &cfg.vacuum_energy,    0.0f, 1.0f, "%.3f");
    ImGui::SameLine(); HelpMarker(
        "Quantum vacuum fluctuations:\n"
        "  \xe2\x80\xa2 Injects virtual photon pairs (ZPE radiation)\n"
        "  \xe2\x80\xa2 Injects virtual e+/e- pairs that annihilate\n"
        "  \xe2\x80\xa2 ZPE energy floor prevents complete particle death\n"
        "  \xe2\x80\xa2 Neutrino CEvNS weak scattering always active");

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
    ImGui::SliderFloat("Bond Form Radius",    &cfg.bond_form_radius,       5.0f,  80.0f, "%.1f");
    ImGui::SliderFloat("Bond Rest Length",    &cfg.bond_rest_length,       5.0f,  60.0f, "%.1f");
    ImGui::SliderFloat("Bond Break Factor",   &cfg.bond_break_factor,      1.1f,   4.0f, "%.2f");
    ImGui::SliderFloat("Bond Spring k",       &cfg.bond_spring_k,         10.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Activation Energy",   &cfg.bond_activation_energy, 0.0f,   0.5f, "%.3f");

    // ── Periodic particle spawn ───────────────────────────────────────────────
    ImGui::SeparatorText("Particle Spawn");
    ImGui::Checkbox("Enable Periodic Spawn", &cfg.spawn_enabled);
    ImGui::SliderFloat("Spawn Interval (s)", &cfg.spawn_interval,  1.0f,  30.0f, "%.1f");
    ImGui::SliderInt ("Spawn Min",  reinterpret_cast<int*>(&cfg.spawn_min),  10,  500);
    ImGui::SliderInt ("Spawn Max",  reinterpret_cast<int*>(&cfg.spawn_max),  10, 1000);

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

    // ── System Stats panel ────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("System Stats")) {
        // Particle census
        ImGui::TextDisabled("Particle Census:");
        ImGui::Text("Active:   %u", active_particle_display);
        ImGui::SameLine(160); ImGui::TextDisabled("(energy > 0.01)");
        ImGui::Text("Dormant:  %u", dormant_particle_display);
        ImGui::SameLine(160); ImGui::TextDisabled("(lab pool)");
        ImGui::Text("Photons:  %u", photon_count_display);

        // SM particle live counts
        static const char* SM_NAMES[5] = {
            "\xce\xb1 alpha", "e\xe2\x81\xbb electron", "e\xe2\x81\xba positron",
            "\xce\xbd neutrino", "\xce\xbc muon"
        };
        bool any_sm = false;
        for (int s = 0; s < 5; ++s) if (sm_counts_display[s] > 0) { any_sm = true; break; }
        if (any_sm) {
            ImGui::Separator();
            ImGui::TextDisabled("Standard Model particles (live):");
            for (int s = 0; s < 5; ++s) {
                if (sm_counts_display[s] > 0)
                    ImGui::Text("  %-14s %u", SM_NAMES[s], sm_counts_display[s]);
            }
        }

        ImGui::Separator();

        // Energy balance
        ImGui::TextDisabled("Energy:");
        ImGui::Text("Total:    %.1f", total_energy_display);
        ImGui::Text("Average:  %.3f / particle", avg_energy_display);

        ImGui::Separator();

        // Bond network
        ImGui::TextDisabled("Chemistry:");
        ImGui::Text("Total bonds: %u", total_bonds_display);

        ImGui::Separator();

        // Genome drift
        ImGui::TextDisabled("Genome drift (population avg):");
        ImGui::Text("Electroneg:  %.3f", avg_electroneg_display);
        ImGui::SameLine(200);
        ImGui::ProgressBar((avg_electroneg_display - 0.1f) / 1.9f, ImVec2(-1, 6));
        ImGui::Text("Reactivity:  %.3f", avg_reactivity_display);
        ImGui::SameLine(200);
        ImGui::ProgressBar((avg_reactivity_display - 0.1f) / 1.9f, ImVec2(-1, 6));
        ImGui::TextDisabled("Drift indicates evolutionary pressure on genome traits.");
    }

    // ── Quantum Physics panel ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Quantum Physics")) {
        // ── Vacuum / QFT ──────────────────────────────────────────────────────
        ImGui::TextDisabled("Quantum Field / Vacuum:");
        ImGui::Text("Virtual pair events:  %u", vacuum_total_display);
        ImGui::SameLine(); HelpMarker(
            "Counts virtual photon-pair injections from vacuum fluctuations.\n"
            "Enable via the Vacuum Energy slider.");
        ImGui::TextDisabled("ZPE floor, CEvNS neutrino scattering always active");

        ImGui::Separator();

        // ── Gauge bosons (summary) ────────────────────────────────────────────
        ImGui::TextDisabled("Gauge interactions:");
        ImGui::BulletText("EM:    Coulomb + Lorentz (photon \xce\xb3)");
        ImGui::BulletText("Weak:  \xce\xb2 decay, \xce\xbc decay (W\xc2\xb1/Z\xc2\xb0)");
        ImGui::BulletText("Strong:sub-atomic Yukawa + covalent bonds");
        ImGui::BulletText("Gravity: inverse-square (slider)");
        ImGui::BulletText("CEvNS: \xce\xbd scatter off nuclei (always on)");

        ImGui::Separator();

        // ── Radioactive decay ─────────────────────────────────────────────────
        ImGui::TextDisabled("Radioactive decay:");
        ImGui::Text("Total decay events:  %u", decay_total_display);
        ImGui::BulletText("U  \xe2\x86\x92 Pb + \xce\xb1 + \xce\xb3  (alpha,  T\xc2\xbd~30 s)");
        ImGui::BulletText("Eu \xe2\x86\x92 Fe + e\xe2\x81\xbb + \xce\xb3  (\xce\xb2\xe2\x81\xbb,    T\xc2\xbd~25 s)");
        ImGui::BulletText("Sr \xe2\x86\x92 Ca + e\xe2\x81\xbb + \xce\xb3  (\xce\xb2\xe2\x81\xbb,    T\xc2\xbd~40 s)");
        ImGui::BulletText("Ni \xe2\x86\x92 Fe + e\xe2\x81\x8a       (\xce\xb2\xe2\x81\xba,    T\xc2\xbd~50 s)");
        ImGui::BulletText("\xce\xbc  \xe2\x86\x92 e\xe2\x81\xbb + \xce\xbd        (muon,  T\xc2\xbd~0.5 s)");
        ImGui::BulletText("e\xe2\x81\xba + e\xe2\x81\xbb \xe2\x86\x92 2\xce\xb3          (annihilation)");
        ImGui::Separator();
        ImGui::TextDisabled("SM particles: spawn via F3 \xe2\x86\x92 Atoms tab");
    }

    ImGui::Separator();
    ImGui::TextDisabled("F1: toggle UI  |  F2: reset  |  F3: spawn picker  |  Space: pause");
    ImGui::TextDisabled("Drag: pan  |  Scroll: zoom  |  ESC: quit");
    ImGui::TextDisabled("Night-Traders-Dev 2026");

    ImGui::End();

    // ── F3 Spawn Picker (separate floating window) ────────────────────────────
    if (spawn_menu_visible)
        draw_spawn_menu(org_manager, particles, cfg);

    // ── Hover inspection tooltip ──────────────────────────────────────────────
    draw_hover_tooltip(org_manager, particles, bond_manager);

    ImGui::Render();
}

// ── Particle grid ─────────────────────────────────────────────────────────────

// Element symbol labels (indexed 0-17: H C N O P S Na Cl Fe Ni Si Ca Ti Sr Au Pb Eu U)
static const char* ATOM_SYMBOLS[ATOM_COUNT] = {
    "H", "C", "N", "O", "P", "S", "Na", "Cl",
    "Fe", "Ni", "Si", "Ca", "Ti", "Sr", "Au", "Pb", "Eu", "U"
};

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

// ── F3 Spawn Picker ───────────────────────────────────────────────────────────

void Interface::draw_spawn_menu(const OrganismManager& org_manager,
                                 const Particles&       particles,
                                 const SimConfig&       cfg)
{
    ImGuiIO& io  = ImGui::GetIO();
    ImVec2 center = { io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f };
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 520.0f, 580.0f }, ImGuiCond_Appearing);

    if (!ImGui::Begin("Spawn Picker [F3]", &spawn_menu_visible,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Prevent world clicks from passing through this window
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
        mouse_within = true;

    // Status line
    if (pending_spawn)
        ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f },
                           ">> Left-click in world to place <<");
    else
        ImGui::TextDisabled(
            "Select an item below, then left-click in the world to place it");
    ImGui::Separator();

    // ── Placement Settings ────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Placement Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Energy", &spawn_energy, 0.05f, 1.0f, "%.2f");
        ImGui::SameLine(); HelpMarker(
            "Initial energy of placed particles.\n"
            "1.0 = full brightness / fast.  0.05 = dim / cold.");

        if (spawn_tab == 0) {
            // Count quick-select buttons
            ImGui::Text("Count:");
            ImGui::SameLine();
            static const int COUNTS[] = { 1, 5, 10, 25, 50 };
            for (int k = 0; k < 5; ++k) {
                ImGui::PushID(k + 400);
                char lbl[6]; std::snprintf(lbl, sizeof(lbl), "x%d", COUNTS[k]);
                bool sel = (spawn_count == COUNTS[k]);
                if (sel)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                if (ImGui::SmallButton(lbl)) spawn_count = COUNTS[k];
                if (sel) ImGui::PopStyleColor();
                ImGui::PopID();
                if (k < 4) ImGui::SameLine();
            }

            if (spawn_count > 1) {
                ImGui::SliderFloat("Scatter Radius", &spawn_scatter, 0.0f, 250.0f, "%.0f px");
                ImGui::SameLine(); HelpMarker("How far apart the atoms are spread around the click point.");
            }
        }
    }
    ImGui::Separator();

    if (ImGui::BeginTabBar("SpawnTabs")) {

        // ── Atoms tab ─────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Atoms")) {
            ImGui::TextDisabled("Choose atom type:");
            ImGui::Spacing();

            // Atoms 0–17 (elements) + SM free particles (19–23)
            static constexpr int SPAWN_TYPE_COUNT = 23;  // ATOM_COUNT + 5 SM types
            static const uint32_t SPAWN_TYPES[SPAWN_TYPE_COUNT] = {
                0,1,2,3,4,5,6,7, 8,9,10,11,12,13,14,15,16,17,  // elements
                ALPHA_TYPE, ELECTRON_TYPE, POSITRON_TYPE, NEUTRINO_TYPE, MUON_TYPE
            };
            static const char* ATOM_LABELS[SPAWN_TYPE_COUNT] = {
                "H","C","N","O","P","S","Na","Cl",
                "Fe","Ni","Si","Ca","Ti","Sr","Au","Pb","Eu","U",
                "\xce\xb1", "e\xe2\x81\xbb", "e\xe2\x81\x8a", "\xce\xbd", "\xce\xbc"
            };
            static const char* ATOM_FULL[SPAWN_TYPE_COUNT] = {
                "Hydrogen","Carbon","Nitrogen","Oxygen",
                "Phosphorus","Sulfur","Sodium","Chlorine",
                "Iron","Nickel","Silicon","Calcium",
                "Titanium","Strontium","Gold","Lead",
                "Europium","Uranium",
                "Alpha particle (He-4)",
                "Free electron (e\xe2\x81\xbb)",
                "Positron (e\xe2\x81\x8a) — annihilates with e\xe2\x81\xbb",
                "Neutrino \xe2\x80\x94 near-zero interaction",
                "Muon \xe2\x80\x94 decays to e\xe2\x81\xbb + \xce\xbd"
            };

            // Separator before SM particles
            bool sm_sep_drawn = false;

            for (int ti = 0; ti < SPAWN_TYPE_COUNT; ++ti) {
                uint32_t t = SPAWN_TYPES[ti];
                if (ti == static_cast<int>(ATOM_COUNT) && !sm_sep_drawn) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Standard Model free particles:");
                    sm_sep_drawn = true;
                }
                ImGui::PushID(static_cast<int>(t));
                if (t < particles.colors.size()) {
                    const glm::vec4& col = particles.colors[t];
                    bool sel = (static_cast<uint32_t>(spawn_atom_type) == t && spawn_tab == 0);
                    float bright = sel ? 1.25f : 0.72f;
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(col.r * bright, col.g * bright, col.b * bright, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(col.r, col.g, col.b, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(col.r * 1.35f, col.g * 1.35f, col.b * 1.35f, 1.0f));

                    if (ImGui::Button(ATOM_LABELS[ti], { 58.0f, 44.0f })) {
                        spawn_atom_type = static_cast<int>(t);
                        spawn_tab       = 0;
                        pending_spawn   = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", ATOM_FULL[ti]);

                    ImGui::PopStyleColor(3);
                }
                ImGui::PopID();
                if ((ti + 1) % 6 != 0)
                    ImGui::SameLine();
            }

            ImGui::EndTabItem();
        }

        // ── Groups tab ────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Groups")) {
            ImGui::TextDisabled("Choose molecule template:");
            ImGui::Spacing();

            struct MolTemplate { const char* label; const char* desc; };
            static const MolTemplate TEMPLATES[14] = {
                { "H2O",   "Water: 1 O + 2 H  (bent, ~105 deg)"             },
                { "CH4",   "Methane: 1 C + 4 H  (tetrahedral)"              },
                { "NaCl",  "Salt: Na-Cl ionic pair"                          },
                { "NH3",   "Ammonia: 1 N + 3 H  (trigonal pyramidal)"       },
                { "CO2",   "Carbon dioxide: O=C=O  (linear)"                 },
                { "Gly",   "Glycine: N-C-C(=O)  amino acid backbone"        },
                { "C6H6",  "Benzene: 6-carbon aromatic ring + 6 H"          },
                { "SiO4",  "Silicate: Si + 4 O  (tetrahedral, rock-forming)" },
                { "Fe2O3", "Hematite: 2 Fe + 3 O  (iron oxide mineral)"     },
                { "EtOH",  "Ethanol: 2 C + 6 H + 1 O  (alcohol)"           },
                { "CaCO3", "Calcite: Ca + C + 3 O  (limestone mineral)"     },
                { "Au3",   "Gold trimer: 3 Au  (metallic nano-cluster)"     },
                { "UO2",   "Uranium oxide: U + 2 O  (nuclear fuel analog)"  },
                { "FeS2",  "Pyrite: Fe + 2 S  (fool's gold mineral)"        },
            };

            for (int i = 0; i < 14; ++i) {
                ImGui::PushID(i + 100);
                bool sel = (spawn_group_idx == i && spawn_tab == 1);
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(0.18f, 0.52f, 0.18f, 1.0f)
                        : ImVec4(0.22f, 0.28f, 0.38f, 1.0f));

                if (ImGui::Button(TEMPLATES[i].label, { 78.0f, 44.0f })) {
                    spawn_group_idx = i;
                    spawn_tab       = 1;
                    pending_spawn   = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TEMPLATES[i].desc);

                ImGui::PopStyleColor();
                ImGui::PopID();
                if ((i + 1) % 3 != 0)
                    ImGui::SameLine();
            }

            ImGui::EndTabItem();
        }

        // ── Organisms tab ─────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Organisms")) {
            ImGui::SeparatorText("Live Organisms");

            const auto& orgs   = org_manager.organisms;
            auto org_count = static_cast<uint32_t>(orgs.size());

            if (org_count == 0) {
                ImGui::TextDisabled("(no organisms detected yet)");
            } else {
                // Sort by size descending
                std::vector<const Organism*> sorted;
                sorted.reserve(org_count);
                for (const auto& o : orgs) sorted.push_back(&o);
                std::sort(sorted.begin(), sorted.end(),
                          [](const Organism* a, const Organism* b) {
                              return a->traits.size > b->traits.size;
                          });

                static const char* MOL_LBL[] = {
                    "INRG", "H2O ", "LIPD", "AACD", "NUCL", "RAD!", "POLY"
                };
                uint32_t show = std::min(org_count, 8u);

                if (ImGui::BeginTable("spawn_orgtbl", 5,
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_ScrollY,
                                      ImVec2(0.0f, 180.0f)))
                {
                    ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 16.0f);
                    ImGui::TableSetupColumn("Mol",  ImGuiTableColumnFlags_WidthFixed, 38.0f);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Spd",  ImGuiTableColumnFlags_WidthFixed, 44.0f);
                    ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (uint32_t i = 0; i < show; ++i) {
                        const Organism& o  = *sorted[i];
                        auto live_idx      = static_cast<int>(sorted[i] - orgs.data());
                        uint32_t dt        = o.traits.dominant_type;
                        const glm::vec4& c = (dt < MAX_PARTICLE_TYPES)
                                              ? particles.colors[dt] : glm::vec4(1.0f);

                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::ColorButton("##tc",
                            ImVec4(c.r, c.g, c.b, 1.0f),
                            ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));

                        ImGui::TableSetColumnIndex(1);
                        auto mc = static_cast<uint8_t>(o.traits.mol_class);
                        ImGui::TextUnformatted(mc < 7 ? MOL_LBL[mc] : "???");

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%u", o.traits.size);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.1f", o.traits.avg_speed);

                        ImGui::TableSetColumnIndex(4);
                        ImGui::PushID(static_cast<int>(i) + 200);
                        bool sel = (spawn_organism_idx == live_idx && spawn_tab == 2);
                        if (sel)
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                        if (ImGui::SmallButton(sel ? "Place " : "Select")) {
                            spawn_organism_idx = live_idx;
                            spawn_tab          = 2;
                            pending_spawn      = true;
                        }
                        if (sel) ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            // Predefined templates
            ImGui::Spacing();
            ImGui::SeparatorText("Predefined Templates");

            struct OrgTemplate { const char* label; const char* desc; int tidx; };
            static const OrgTemplate ORG_TMPL[3] = {
                { "Water Cluster", "5x H2O arranged in a pentagon ring",    -10 },
                { "Salt Lattice",  "4x NaCl ionic crystal 2x2 fragment",   -11 },
                { "Lipid Stub",    "C6H12O2 short fatty acid chain",        -12 },
            };

            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(i + 300);
                bool sel = (spawn_organism_idx == ORG_TMPL[i].tidx && spawn_tab == 2);
                if (sel)
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                if (ImGui::Button(ORG_TMPL[i].label, { 145.0f, 36.0f })) {
                    spawn_organism_idx = ORG_TMPL[i].tidx;
                    spawn_tab          = 2;
                    pending_spawn      = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", ORG_TMPL[i].desc);
                if (sel) ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 2) ImGui::SameLine();
            }

            ImGui::EndTabItem();
        }

        // ── Organics tab ──────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Organics")) {
            ImGui::TextDisabled("Bio-molecule templates:");
            ImGui::Spacing();

            struct OrgTemplate { const char* label; const char* desc; };
            static const OrgTemplate ORG_MOLS[8] = {
                { "Gly",    "Glycine: simplest amino acid  NH2-CH2-COOH  (8 atoms)"            },
                { "Ala",    "Alanine: amino acid w/ methyl side-chain  (10 atoms)"             },
                { "Glc",    "Glucose: hexose sugar ring  C6H12O6 (simplified, 11 atoms)"       },
                { "Rib",    "Ribose: pentose sugar ring  C5H10O4 (simplified, 9 atoms)"        },
                { "ButAc",  "Butyric acid: short fatty acid chain  C4H8O2  (12 atoms)"         },
                { "GlyP",   "Glycerophosphate: lipid head group  P+C+N  (10 atoms)"            },
                { "Ade",    "Adenine: purine nucleobase (DNA/RNA)  C5H5N5  (10 atoms)"         },
                { "Cyt",    "Cytosine: pyrimidine nucleobase (DNA/RNA)  C4H5N3O  (8 atoms)"    },
            };

            static const ImVec4 ORG_COLORS[4] = {
                { 0.45f, 0.22f, 0.52f, 1.0f },  // protein — purple
                { 0.55f, 0.42f, 0.12f, 1.0f },  // lipid   — brown
                { 0.22f, 0.50f, 0.28f, 1.0f },  // carb    — green
                { 0.22f, 0.36f, 0.58f, 1.0f },  // nucleic — blue
            };
            // Category index: 0,1 = protein; 2,3 = carb; 4,5 = lipid; 6,7 = nucleic
            static const int ORG_CAT[8] = { 0, 0, 2, 2, 1, 1, 3, 3 };

            ImGui::SeparatorText("Proteins"); ImGui::Spacing();
            for (int i = 0; i < 2; ++i) {
                ImGui::PushID(i + 400);
                bool sel = (spawn_organic_idx == i && spawn_tab == 3);
                const ImVec4& base_col = ORG_COLORS[ORG_CAT[i]];
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(base_col.x*1.4f, base_col.y*1.4f, base_col.z*1.4f, 1.0f)
                        : base_col);
                if (ImGui::Button(ORG_MOLS[i].label, { 88.0f, 40.0f })) {
                    spawn_organic_idx = i;
                    spawn_tab         = 3;
                    pending_spawn     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ORG_MOLS[i].desc);
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 1) ImGui::SameLine();
            }

            ImGui::Spacing(); ImGui::SeparatorText("Carbohydrates"); ImGui::Spacing();
            for (int i = 2; i < 4; ++i) {
                ImGui::PushID(i + 400);
                bool sel = (spawn_organic_idx == i && spawn_tab == 3);
                const ImVec4& base_col = ORG_COLORS[ORG_CAT[i]];
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(base_col.x*1.4f, base_col.y*1.4f, base_col.z*1.4f, 1.0f)
                        : base_col);
                if (ImGui::Button(ORG_MOLS[i].label, { 88.0f, 40.0f })) {
                    spawn_organic_idx = i;
                    spawn_tab         = 3;
                    pending_spawn     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ORG_MOLS[i].desc);
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 3) ImGui::SameLine();
            }

            ImGui::Spacing(); ImGui::SeparatorText("Lipids"); ImGui::Spacing();
            for (int i = 4; i < 6; ++i) {
                ImGui::PushID(i + 400);
                bool sel = (spawn_organic_idx == i && spawn_tab == 3);
                const ImVec4& base_col = ORG_COLORS[ORG_CAT[i]];
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(base_col.x*1.4f, base_col.y*1.4f, base_col.z*1.4f, 1.0f)
                        : base_col);
                if (ImGui::Button(ORG_MOLS[i].label, { 88.0f, 40.0f })) {
                    spawn_organic_idx = i;
                    spawn_tab         = 3;
                    pending_spawn     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ORG_MOLS[i].desc);
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 5) ImGui::SameLine();
            }

            ImGui::Spacing(); ImGui::SeparatorText("Nucleic Acids"); ImGui::Spacing();
            for (int i = 6; i < 8; ++i) {
                ImGui::PushID(i + 400);
                bool sel = (spawn_organic_idx == i && spawn_tab == 3);
                const ImVec4& base_col = ORG_COLORS[ORG_CAT[i]];
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(base_col.x*1.4f, base_col.y*1.4f, base_col.z*1.4f, 1.0f)
                        : base_col);
                if (ImGui::Button(ORG_MOLS[i].label, { 88.0f, 40.0f })) {
                    spawn_organic_idx = i;
                    spawn_tab         = 3;
                    pending_spawn     = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ORG_MOLS[i].desc);
                ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 7) ImGui::SameLine();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Cancel / status footer
    ImGui::Separator();
    if (pending_spawn) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Cancel Placement", { -1.0f, 0.0f }))
            pending_spawn = false;
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("(nothing selected)");
    }

    ImGui::End();
}

// ── Hover inspection tooltip ──────────────────────────────────────────────────

void Interface::draw_hover_tooltip(const OrganismManager& org_manager,
                                    const Particles&       particles,
                                    const BondManager&     bond_manager)
{
    if (hover_particle_idx < 0 || mouse_within) return;
    auto pi = static_cast<uint32_t>(hover_particle_idx);
    if (pi >= particles.types.size()) return;

    float    energy  = (hover_energies_ptr && pi < hover_energies_ptr->size())
                        ? (*hover_energies_ptr)[pi] : 0.0f;
    uint32_t ptype   = particles.types[pi];
    uint32_t bonds   = (pi < bond_manager.bond_counts.size())
                        ? bond_manager.bond_counts[pi] : 0u;
    uint32_t valence = (ptype < ATOM_COUNT) ? ATOM_VALENCE[ptype] : 0u;
    bool     is_photon = (ptype == PHOTON_TYPE);

    static const char* ATOM_FULL[ATOM_COUNT] = {
        "Hydrogen","Carbon","Nitrogen","Oxygen",
        "Phosphorus","Sulfur","Sodium","Chlorine",
        "Iron","Nickel","Silicon","Calcium",
        "Titanium","Strontium","Gold","Lead",
        "Europium","Uranium"
    };
    static const char* ATOM_SYM[ATOM_COUNT] = {
        "H","C","N","O","P","S","Na","Cl",
        "Fe","Ni","Si","Ca","Ti","Sr","Au","Pb","Eu","U"
    };

    // SM free particle names / symbols
    static const char* SM_NAMES[5] = {
        "Alpha particle (He-4)", "Free Electron", "Positron",
        "Electron Neutrino", "Muon"
    };
    static const char* SM_SYMS[5] = {
        "\xce\xb1", "e\xe2\x81\xbb", "e\xe2\x81\x8a", "\xce\xbd", "\xce\xbc"
    };
    bool is_sm = (ptype >= ALPHA_TYPE && ptype < ALPHA_TYPE + 5u);

    const char* type_name = is_photon ? "Photon"
                          : is_sm     ? SM_NAMES[ptype - ALPHA_TYPE]
                          : (ptype < ATOM_COUNT ? ATOM_FULL[ptype] : "Unknown");
    const char* type_sym  = is_photon ? "\xce\xb3"
                          : is_sm     ? SM_SYMS[ptype - ALPHA_TYPE]
                          : (ptype < ATOM_COUNT ? ATOM_SYM[ptype] : "?");
    const glm::vec4& col  = (ptype < MAX_PARTICLE_TYPES)
                             ? particles.colors[ptype] : glm::vec4(1.0f);

    // Find the organism that contains this particle, if any
    const Organism* org_ptr = nullptr;
    for (const auto& o : org_manager.organisms) {
        for (uint32_t mi : o.particle_indices) {
            if (mi == pi) { org_ptr = &o; break; }
        }
        if (org_ptr) break;
    }

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(260.0f);

    // ── Header: swatch + type name ────────────────────────────────────────────
    ImGui::ColorButton("##hc", ImVec4(col.r, col.g, col.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
    ImGui::SameLine();
    ImGui::Text("%s  —  %s", type_sym, type_name);
    ImGui::TextDisabled("Particle #%u", pi);
    ImGui::Separator();

    // ── Energy ────────────────────────────────────────────────────────────────
    ImGui::Text("Energy:");
    ImGui::SameLine(70.0f);
    ImVec4 ecol = ImVec4(1.0f - energy, energy * 0.85f, 0.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ecol);
    ImGui::ProgressBar(energy, ImVec2(130.0f, 10.0f), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%.2f", energy);

    if (is_photon) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f),
                           "Massless EM energy carrier");
    } else {
        // ── Bonds ─────────────────────────────────────────────────────────────
        ImGui::Text("Bonds: %u / %u", bonds, valence);
        if (bonds < valence)
            ImGui::SameLine(), ImGui::TextDisabled("(%u free)", valence - bonds);

        // ── Genome ────────────────────────────────────────────────────────────
        if (pi * 4 + 3 < particles.genomes.size()) {
            float charge   = particles.genomes[pi*4+0];
            float eneg     = particles.genomes[pi*4+1];
            float react    = particles.genomes[pi*4+2];
            float bond_str = particles.genomes[pi*4+3];
            ImGui::Separator();
            ImGui::Text("Charge:       %+.2f", charge);
            ImGui::Text("Electroneg:    %.2f", eneg);
            ImGui::Text("Reactivity:    %.2f", react);
            ImGui::Text("Bond Str:     %+.2f", bond_str);
        }
    }

    // ── Organism / cluster ────────────────────────────────────────────────────
    if (org_ptr) {
        static const char* MOL_NAMES[] = {
            "Inorganic","Water","Lipid","Amino Acid","Nucleotide","Radical","Polymer"
        };
        auto mc = static_cast<uint8_t>(org_ptr->traits.mol_class);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Cluster: %s",
                           mc < 7 ? MOL_NAMES[mc] : "Unknown");
        ImGui::Text("%u atoms  •  %u bonds  •  speed %.1f",
                    org_ptr->traits.size,
                    org_ptr->traits.bond_count,
                    org_ptr->traits.avg_speed);
        ImGui::Text("Avg energy: %.2f", org_ptr->traits.energy);
        if (org_ptr->traits.generation > 0)
            ImGui::Text("Gen %u  •  Kills %u  •  Divs %u",
                        org_ptr->traits.generation,
                        org_ptr->traits.kills,
                        org_ptr->traits.divisions);
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

