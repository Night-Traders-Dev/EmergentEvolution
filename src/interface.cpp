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

    // ── F4 Quantum Field Display ──────────────────────────────────────────────
    if (quantum_field_visible)
        draw_quantum_field_display(particles);

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
    if (!ImGui::CollapsingHeader("Aggregates & Cells"))
        return;

    ImGui::SliderFloat("Cluster Radius", &org_manager.cluster_radius, 10.0f, 200.0f, "%.0f");

    uint32_t org_count = static_cast<uint32_t>(org_manager.organisms.size());

    // Aggregate + cell hierarchy counts
    ImGui::Text("Clusters: %u active  |  %u dust",
        org_manager.alive_count, org_manager.dust_count);
    ImGui::Separator();
    ImGui::Columns(3, "cellcounts", false);
    ImGui::TextDisabled("Vesicles");   ImGui::NextColumn();
    ImGui::TextDisabled("Proto-cells"); ImGui::NextColumn();
    ImGui::TextDisabled("Cells");      ImGui::NextColumn();
    ImGui::Text("%u", org_manager.vesicle_count);   ImGui::NextColumn();
    ImGui::Text("%u", org_manager.protocell_count); ImGui::NextColumn();
    if (org_manager.cell_count > 0)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
            "\xe2\x98\x85 %u", org_manager.cell_count);
    else
        ImGui::Text("0");
    ImGui::NextColumn();
    ImGui::Columns(1);
    if (org_manager.cell_count == 0)
        ImGui::TextDisabled("(lipid ring + enclosed nucleotide = cell)");
    ImGui::Separator();

    // Deaths / births
    ImGui::Columns(2, "popstats", false);
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

        float frac = (particles.trait_scales[t] - 1.0f) / 1.5f;  // cap 2.5× → full bar
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

    // Filter to vesicle+ only — plain aggregates (water, minerals) are not organisms
    std::vector<const Organism*> sorted;
    sorted.reserve(org_count);
    for (const auto& o : org_manager.organisms)
        if (o.traits.complexity >= ComplexityLevel::VESICLE)
            sorted.push_back(&o);

    if (sorted.empty()) {
        ImGui::TextDisabled("No vesicles detected yet.");
        ImGui::TextDisabled("C/H particles must form a ring (size\xe2\x89\xa58)");
        ImGui::TextDisabled("to qualify. Adjust forces or wait.");
        return;
    }

    // Sort by complexity desc, then size desc
    std::sort(sorted.begin(), sorted.end(),
              [](const Organism* a, const Organism* b) {
                  if (a->traits.complexity != b->traits.complexity)
                      return a->traits.complexity > b->traits.complexity;
                  return a->traits.size > b->traits.size;
              });

    uint32_t show = std::min(static_cast<uint32_t>(sorted.size()), 8u);
    ImGui::TextDisabled("Cells / Vesicles (by complexity, then size):");

    // Complexity tier label + colour (replaces raw mol_class label in table)
    auto cell_label = [](ComplexityLevel c) -> const char* {
        switch (c) {
        case ComplexityLevel::CELL:       return "CELL";
        case ComplexityLevel::PROTO_CELL: return "PCLL";
        default:                          return "VSIC";  // VESICLE
        }
    };
    auto cell_color = [](ComplexityLevel c) -> ImVec4 {
        switch (c) {
        case ComplexityLevel::CELL:       return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // bright green
        case ComplexityLevel::PROTO_CELL: return ImVec4(0.6f, 1.0f, 0.4f, 1.0f);  // lime
        default:                          return ImVec4(1.0f, 0.9f, 0.2f, 1.0f);  // yellow (vesicle)
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

            // Complexity tier label
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, cell_color(o.traits.complexity));
            ImGui::TextUnformatted(cell_label(o.traits.complexity));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Ring: %.2f  Nucleotide: %s\n"
                    "Charge:%.2f  Electroneg:%.2f\n"
                    "Reactivity:%.2f  BondStr:%.2f",
                    o.traits.ring_factor,
                    o.traits.has_nucleotide_inside ? "yes" : "no",
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

            // Atoms 0–17 (elements) + SM free particles (18–29 skipping 18=photon)
            // Photon is in SM section at index 18 directly
            static constexpr int SPAWN_TYPE_COUNT = 30;  // 18 atoms + 12 SM particles
            static const uint32_t SPAWN_TYPES[SPAWN_TYPE_COUNT] = {
                0,1,2,3,4,5,6,7, 8,9,10,11,12,13,14,15,16,17,  // elements
                PHOTON_TYPE, ALPHA_TYPE, ELECTRON_TYPE, POSITRON_TYPE,
                NEUTRINO_TYPE, MUON_TYPE, TAU_TYPE,
                MU_NEUTRINO_TYPE, TAU_NEUTRINO_TYPE,
                W_BOSON_TYPE, Z_BOSON_TYPE, HIGGS_TYPE
            };
            static const char* ATOM_LABELS[SPAWN_TYPE_COUNT] = {
                "H","C","N","O","P","S","Na","Cl",
                "Fe","Ni","Si","Ca","Ti","Sr","Au","Pb","Eu","U",
                "\xce\xb3",          // photon γ
                "\xce\xb1",          // alpha α
                "e\xe2\x81\xbb",     // electron e-
                "e\xe2\x81\x8a",     // positron e+
                "\xce\xbd\xe2\x82\x91",  // νe
                "\xce\xbc",          // muon μ
                "\xcf\x84",          // tau τ
                "\xce\xbd\xce\xbc",  // νμ
                "\xce\xbd\xcf\x84",  // ντ
                "W\xc2\xb1",         // W boson
                "Z\xe2\x81\xb0",     // Z boson
                "H\xe2\x81\xb0"      // Higgs H0
            };
            static const char* ATOM_FULL[SPAWN_TYPE_COUNT] = {
                "Hydrogen","Carbon","Nitrogen","Oxygen",
                "Phosphorus","Sulfur","Sodium","Chlorine",
                "Iron","Nickel","Silicon","Calcium",
                "Titanium","Strontium","Gold","Lead",
                "Europium","Uranium",
                "Photon (\xce\xb3)  — massless, ballistic",
                "Alpha particle (\xce\xb1)  — He-4 nucleus",
                "Electron (e\xe2\x81\xbb)  — free lepton",
                "Positron (e\xe2\x81\x8a)  — annihilates with e\xe2\x81\xbb",
                "Electron neutrino (\xce\xbd\xe2\x82\x91)  — near-zero interaction",
                "Muon (\xce\xbc\xe2\x81\xbb)  — decays to e\xe2\x81\xbb + neutrinos",
                "Tau (\xcf\x84\xe2\x81\xbb)  — heaviest lepton, fast decay",
                "Muon neutrino (\xce\xbd\xce\xbc)  — from muon decay",
                "Tau neutrino (\xce\xbd\xcf\x84)  — from tau decay",
                "W boson (W\xc2\xb1)  — charged weak-force carrier",
                "Z boson (Z\xe2\x81\xb0)  — neutral weak-force carrier",
                "Higgs boson (H\xe2\x81\xb0)  — scalar field quantum"
            };

            // Separator before SM particles
            bool sm_sep_drawn = false;

            for (int ti = 0; ti < SPAWN_TYPE_COUNT; ++ti) {
                uint32_t t = SPAWN_TYPES[ti];
                if (ti == static_cast<int>(ATOM_COUNT) && !sm_sep_drawn) {
                    ImGui::Spacing();
                    ImGui::SeparatorText("Standard Model");
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
            ImGui::TextDisabled("Earth molecules — click to place:");
            ImGui::Spacing();

            struct MolTemplate { const char* label; const char* desc; };
            // Indices match MOLECULES[] in simulation.cpp (0-48)
            static const MolTemplate TEMPLATES[49] = {
                // Common (0-13)
                { "H2O",    "Water: O + 2H  (bent ~105 deg)"                    },
                { "CH4",    "Methane: C + 4H  (tetrahedral)"                    },
                { "NaCl",   "Salt: Na-Cl  (ionic pair)"                         },
                { "NH3",    "Ammonia: N + 3H  (trigonal pyramidal)"             },
                { "CO2",    "Carbon dioxide: O=C=O  (linear)"                   },
                { "Gly",    "Glycine: N-C-C(=O)  amino acid backbone"           },
                { "C6H6",   "Benzene: 6-C aromatic ring + 6H"                   },
                { "SiO4",   "Silicate: Si + 4O  (tetrahedral, rock-forming)"    },
                { "Fe2O3",  "Hematite: 2Fe + 3O  (iron oxide mineral)"          },
                { "EtOH",   "Ethanol: 2C + 6H + O  (alcohol)"                  },
                { "CaCO3",  "Calcite: Ca + C + 3O  (limestone)"                 },
                { "Au3",    "Gold trimer: 3 Au  (metallic cluster)"             },
                { "UO2",    "Uranium dioxide: U + 2O  (nuclear fuel)"           },
                { "FeS2",   "Pyrite: Fe + 2S  (fool's gold)"                    },
                // Atmospheric (14-21)
                { "O2",     "Dioxygen: O=O  (atmospheric O2)"                   },
                { "N2",     "Dinitrogen: N=N  (78% of atmosphere)"              },
                { "O3",     "Ozone: bent O3  (stratospheric shield)"            },
                { "CO",     "Carbon monoxide: C=O  (combustion product)"        },
                { "NO",     "Nitric oxide: N=O  (radical, signalling)"          },
                { "NO2",    "Nitrogen dioxide: bent NO2  (smog, brown gas)"     },
                { "SO2",    "Sulfur dioxide: bent SO2  (volcanic, acid rain)"   },
                { "H2S",    "Hydrogen sulfide: bent H2S  (rotten egg smell)"    },
                // Acids / bases (22-27)
                { "HCl",    "Hydrochloric acid: H-Cl  (stomach acid)"           },
                { "NaOH",   "Sodium hydroxide: Na-O-H  (lye, strong base)"      },
                { "H2O2",   "Hydrogen peroxide: H-O-O-H  (bleach, antiseptic)" },
                { "H2SO4",  "Sulfuric acid: S + 4O + 2H  (simplified)"         },
                { "HNO3",   "Nitric acid: N + 3O + H  (simplified)"            },
                { "H3PO4",  "Phosphoric acid: P + 4O + 3H  (biological)"       },
                // Organic basics (28-38)
                { "CH2O",   "Formaldehyde: H2C=O  (simplest aldehyde)"         },
                { "HCN",    "Hydrogen cyanide: H-C=N  (toxic, prebiotic)"      },
                { "C2H6",   "Ethane: H3C-CH3  (natural gas component)"         },
                { "C2H4",   "Ethylene: H2C=CH2  (plant hormone, plastic)"      },
                { "C2H2",   "Acetylene: HC=CH  (welding gas)"                  },
                { "MeOH",   "Methanol: H3C-OH  (wood alcohol)"                 },
                { "AcOH",   "Acetic acid: CH3-COOH  (vinegar)"                 },
                { "Urea",   "Urea: (NH2)2C=O  (biological waste, fertiliser)"  },
                { "Acetone","Acetone: (CH3)2C=O  (solvent)"                    },
                { "C3H8",   "Propane: CH3-CH2-CH3  (LP gas)"                   },
                { "MeNH2",  "Methylamine: CH3-NH2  (prebiotic amine)"          },
                // Minerals (39-44)
                { "SiO2",   "Quartz: O-Si-O  (most common mineral)"            },
                { "TiO2",   "Rutile: O-Ti-O  (titanium ore)"                   },
                { "NiO",    "Nickel oxide: Ni-O  (smelting product)"           },
                { "CaO",    "Quicklime: Ca-O  (cement precursor)"              },
                { "FeO",    "Wuestite: Fe-O  (iron ore)"                       },
                { "NiS",    "Millerite: Ni-S  (nickel ore)"                    },
                // Misc (45-48)
                { "SO3",    "Sulfur trioxide: planar SO3  (acid anhydride)"    },
                { "ClNO",   "Nitrosyl chloride: Cl-N=O  (reactive halide)"     },
                { "CS2",    "Carbon disulfide: S=C=S  (solvent)"               },
                { "PH3",    "Phosphine: trigonal PH3  (toxic, prebiotic?)"     },
            };

            // Section separators by category
            static const struct { int start; const char* label; } SECTIONS[] = {
                { 0,  "Common"      },
                { 14, "Atmospheric" },
                { 22, "Acids & Bases"},
                { 28, "Organic"     },
                { 39, "Minerals"    },
                { 45, "Other"       },
            };
            static constexpr int NSEC = 6;
            int sec = 0;
            for (int i = 0; i < 49; ++i) {
                // Print section header when we hit a section boundary
                if (sec < NSEC && i == SECTIONS[sec].start) {
                    if (i != 0) ImGui::Spacing();
                    ImGui::SeparatorText(SECTIONS[sec].label);
                    ++sec;
                }
                ImGui::PushID(i + 100);
                bool sel = (spawn_group_idx == i && spawn_tab == 1);
                ImGui::PushStyleColor(ImGuiCol_Button,
                    sel ? ImVec4(0.18f, 0.52f, 0.18f, 1.0f)
                        : ImVec4(0.22f, 0.28f, 0.38f, 1.0f));

                if (ImGui::Button(TEMPLATES[i].label, { 72.0f, 36.0f })) {
                    spawn_group_idx = i;
                    spawn_tab       = 1;
                    pending_spawn   = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", TEMPLATES[i].desc);

                ImGui::PopStyleColor();
                ImGui::PopID();
                // 4 per row within a section — reset at each section start
                int pos_in_row = (i - SECTIONS[std::max(0, sec-1)].start);
                if ((pos_in_row + 1) % 4 != 0 && i != 48)
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

            // ── Chemistry presets (3 in a row) ────────────────────────────────
            static const OrgTemplate CHEM_TMPL[3] = {
                { "Water Cluster", "5x H2O arranged in a pentagon ring",  -10 },
                { "Salt Lattice",  "4x NaCl ionic crystal 2x2 fragment", -11 },
                { "Lipid Stub",    "C6H12O2 short fatty acid chain",      -12 },
            };
            for (int i = 0; i < 3; ++i) {
                ImGui::PushID(i + 300);
                bool sel = (spawn_organism_idx == CHEM_TMPL[i].tidx && spawn_tab == 2);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                if (ImGui::Button(CHEM_TMPL[i].label, { 145.0f, 36.0f })) {
                    spawn_organism_idx = CHEM_TMPL[i].tidx;
                    spawn_tab = 2; pending_spawn = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", CHEM_TMPL[i].desc);
                if (sel) ImGui::PopStyleColor();
                ImGui::PopID();
                if (i < 2) ImGui::SameLine();
            }

            // ── Cell structures (3 proto-cells / 3 cells, 3-per-row) ──────────
            ImGui::Spacing();
            ImGui::SeparatorText("Cell Structures  (area cleared on spawn)");
            static const OrgTemplate CELL_TMPL[6] = {
                { "Proto-Cell",
                  "16-C ring R=57 (spacing=bond_rest=22px) + glycine-like core\n"
                  "Core [N C2 O2]: (N+O)*2=6>5, C>0 \xe2\x86\x92 AMINO_ACID \xe2\x86\x92 PROTO_CELL\n"
                  "Cross-ring bonds (i\xe2\x86\x92i+2) triangulate membrane. Clear=118px.", -13 },
                { "Proto-Cell L",
                  "20-C ring R=70 (spacing=bond_rest=22px) + ribosome-like core\n"
                  "Core [N3 C3 O2]: (N+O)*2=10>8, C>0 \xe2\x86\x92 AMINO_ACID \xe2\x86\x92 PROTO_CELL\n"
                  "Cross-ring bonds for stiffness. Clear=130px.",    -15 },
                { "Proto-Cell N",
                  "16-C ring R=57 + N-rich amino acid core\n"
                  "Core [N2 C O2]: (N+O)*2=8>5, C>0 \xe2\x86\x92 AMINO_ACID \xe2\x86\x92 PROTO_CELL\n"
                  "Cross-ring bonds. Clear=118px.",                  -17 },
                { "Cell",
                  "16-C ring R=57 + DNA/ATP/ribosome organelle core\n"
                  "Core [P5 N2 C]: ATP triphosphate + DNA backbone + base pair\n"
                  "P*3=15>8 \xe2\x86\x92 NUCLEOTIDE \xe2\x86\x92 CELL. Clear=118px.",   -14 },
                { "Cell L",
                  "20-C ring R=70 + full organelle core\n"
                  "Core [P5 N2 C2 O S2]: DNA + ATP + Fe-S cluster (mitochondria)\n"
                  "P*3=15>12 \xe2\x86\x92 NUCLEOTIDE \xe2\x86\x92 CELL. Clear=130px.",  -16 },
                { "Cell P",
                  "16-C ring R=57 + P-rich nucleotide core\n"
                  "Core [P3 N C]: P*3=9>5 \xe2\x86\x92 NUCLEOTIDE \xe2\x86\x92 CELL\n"
                  "Cross-ring bonds. Clear=118px.",                  -18 },
            };
            for (int i = 0; i < 6; ++i) {
                ImGui::PushID(i + 310);
                bool sel = (spawn_organism_idx == CELL_TMPL[i].tidx && spawn_tab == 2);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                if (ImGui::Button(CELL_TMPL[i].label, { 145.0f, 36.0f })) {
                    spawn_organism_idx = CELL_TMPL[i].tidx;
                    spawn_tab = 2; pending_spawn = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", CELL_TMPL[i].desc);
                if (sel) ImGui::PopStyleColor();
                ImGui::PopID();
                if (i % 3 != 2) ImGui::SameLine();
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

// ── F4 Quantum Field Display ──────────────────────────────────────────────────
// Shows the Standard Model particle census, per-field excitation meters, and
// inter-field coupling diagram for the current simulation state.

void Interface::draw_quantum_field_display(const Particles& particles) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f },
                            ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 740.0f, 560.0f }, ImGuiCond_Appearing);

    if (!ImGui::Begin("Quantum Fields [F4]", &quantum_field_visible,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End(); return;
    }

    // ── Pre-compute field strengths from live particle census ──────────────
    const uint32_t* tc = type_counts_display;  // short alias

    // EM field: photons + free charges (electrons, positrons, ions)
    uint32_t charged = tc[ELECTRON_TYPE] + tc[POSITRON_TYPE]
                     + tc[6]  // Na+
                     + tc[7]; // Cl-
    float em_strength = std::min(1.0f,
        (tc[PHOTON_TYPE] * 0.05f + charged * 0.1f) / 10.0f);

    // Weak nuclear: W/Z bosons + all leptons
    uint32_t leptons = tc[ELECTRON_TYPE] + tc[POSITRON_TYPE]
                     + tc[NEUTRINO_TYPE] + tc[MUON_TYPE]
                     + tc[TAU_TYPE] + tc[MU_NEUTRINO_TYPE] + tc[TAU_NEUTRINO_TYPE];
    float weak_strength = std::min(1.0f,
        (tc[W_BOSON_TYPE] * 0.5f + tc[Z_BOSON_TYPE] * 0.5f + leptons * 0.05f) / 5.0f);

    // Higgs: always has vacuum value 0.12; excited by Higgs bosons + mass content
    uint32_t massive = active_particle_display;
    float higgs_strength = std::min(1.0f,
        0.12f + tc[HIGGS_TYPE] * 0.4f + massive * 0.00003f);

    // Gravitational: scaled by total mass-weighted active particles
    float grav_strength = std::min(1.0f, massive * 0.00005f);

    // Strong (QCD): always 0 — no free quarks/gluons in sim
    float strong_strength = 0.0f;

    // ── Layout: SM diagram (left) | field meters (right) ──────────────────
    ImGui::Columns(2, "qf_cols", false);
    ImGui::SetColumnWidth(0, 340.0f);

    // ════════════════════════════════════════════════════════════
    // LEFT: Standard Model particle grid
    // ════════════════════════════════════════════════════════════
    ImGui::TextColored({ 0.9f, 0.85f, 0.3f, 1.f }, "Standard Model");
    ImGui::Spacing();

    // Helper: draw one particle box with dynamic highlight
    auto draw_particle_box = [&](uint32_t type_idx, const char* symbol,
                                  bool confined) {
        uint32_t count = (type_idx < MAX_PARTICLE_TYPES) ? tc[type_idx] : 0u;
        bool     active = count > 0 && !confined;

        ImVec4 bg, txt;
        if (confined) {
            bg  = { 0.15f, 0.15f, 0.17f, 1.f };
            txt = { 0.4f,  0.4f,  0.4f,  1.f };
        } else if (type_idx < particles.colors.size()) {
            const glm::vec4& c = particles.colors[type_idx];
            float bright = active ? 0.55f : 0.22f;
            bg  = { c.r * bright, c.g * bright, c.b * bright, 1.f };
            txt = active ? ImVec4{ c.r, c.g, c.b, 1.f }
                         : ImVec4{ 0.4f, 0.4f, 0.4f, 1.f };
        } else {
            bg  = { 0.2f, 0.2f, 0.2f, 1.f };
            txt = { 0.6f, 0.6f, 0.6f, 1.f };
        }

        ImVec2 sz { 54.0f, 40.0f };
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bg);
        ImGui::PushStyleColor(ImGuiCol_Text, txt);

        // Active particle has a bright border
        if (active) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
            ImGui::PushStyleColor(ImGuiCol_Border,
                ImVec4(txt.x, txt.y, txt.z, 0.9f));
        }

        char lbl[32];
        if (!confined && count > 0)
            std::snprintf(lbl, sizeof(lbl), "%s\n%u", symbol, count);
        else
            std::snprintf(lbl, sizeof(lbl), "%s", symbol);

        ImGui::PushID(static_cast<int>(type_idx) + 2000);
        ImGui::Button(lbl, sz);
        ImGui::PopID();

        if (active) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();  // border
        }
        ImGui::PopStyleColor(4);
    };

    // Generation headers
    ImGui::TextDisabled("         Gen I     Gen II    Gen III");

    // Up-type quarks (u c t) — confined
    ImGui::TextColored({0.5f,0.5f,0.5f,1.f}, "Up q. ");
    ImGui::SameLine();
    draw_particle_box(99, "u", true); ImGui::SameLine();
    draw_particle_box(99, "c", true); ImGui::SameLine();
    draw_particle_box(99, "t", true);
    ImGui::Spacing();

    // Down-type quarks (d s b) — confined
    ImGui::TextColored({0.5f,0.5f,0.5f,1.f}, "Down q");
    ImGui::SameLine();
    draw_particle_box(99, "d", true); ImGui::SameLine();
    draw_particle_box(99, "s", true); ImGui::SameLine();
    draw_particle_box(99, "b", true);
    ImGui::Spacing();

    // Charged leptons (e μ τ)
    ImGui::TextColored({0.7f,0.7f,1.0f,1.f}, "Lept. ");
    ImGui::SameLine();
    draw_particle_box(ELECTRON_TYPE,     "e\xe2\x81\xbb",    false); ImGui::SameLine();
    draw_particle_box(MUON_TYPE,         "\xce\xbc\xe2\x81\xbb", false); ImGui::SameLine();
    draw_particle_box(TAU_TYPE,          "\xcf\x84\xe2\x81\xbb", false);
    ImGui::Spacing();

    // Neutrinos (νe νμ ντ)
    ImGui::TextColored({0.6f,0.6f,0.8f,1.f}, "Neutr.");
    ImGui::SameLine();
    draw_particle_box(NEUTRINO_TYPE,     "\xce\xbd\xe2\x82\x91",  false); ImGui::SameLine();
    draw_particle_box(MU_NEUTRINO_TYPE,  "\xce\xbd\xce\xbc",      false); ImGui::SameLine();
    draw_particle_box(TAU_NEUTRINO_TYPE, "\xce\xbd\xcf\x84",      false);
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::TextColored({ 0.9f, 0.85f, 0.3f, 1.f }, "Bosons");
    ImGui::Spacing();

    draw_particle_box(PHOTON_TYPE,  "\xce\xb3",     false); ImGui::SameLine();
    draw_particle_box(W_BOSON_TYPE, "W\xc2\xb1",    false); ImGui::SameLine();
    draw_particle_box(Z_BOSON_TYPE, "Z\xe2\x81\xb0",false); ImGui::SameLine();
    draw_particle_box(99,           "g",             true);  ImGui::SameLine();  // gluon confined
    draw_particle_box(HIGGS_TYPE,   "H\xe2\x81\xb0",false);
    ImGui::Spacing();

    ImGui::Separator();
    ImGui::TextColored({ 0.9f, 0.85f, 0.3f, 1.f }, "Composite");
    ImGui::Spacing();
    draw_particle_box(ALPHA_TYPE, "\xce\xb1 (He4)", false);

    // ════════════════════════════════════════════════════════════
    // RIGHT: Quantum field meters + stats
    // ════════════════════════════════════════════════════════════
    ImGui::NextColumn();

    ImGui::TextColored({ 0.9f, 0.85f, 0.3f, 1.f }, "Active Quantum Fields");
    ImGui::Spacing();

    // Field meter helper: colored progress bar + coupling list
    auto draw_field = [&](const char* name, float strength,
                           ImVec4 color, const char* coupling) {
        ImGui::TextColored(color, "%s", name);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(strength, { -1, 14 }, "");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.f));
        ImGui::TextWrapped("  %s", coupling);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    };

    draw_field("Electromagnetic (QED)",
               em_strength,
               { 0.95f, 0.85f, 0.1f, 1.f },
               "couples to: \xce\xb3  e\xe2\x81\xbb  e\xe2\x81\x8a  charges");

    draw_field("Weak Nuclear (EW)",
               weak_strength,
               { 0.5f, 0.55f, 1.0f, 1.f },
               "couples to: W\xc2\xb1  Z\xe2\x81\xb0  all leptons");

    draw_field("Strong / QCD",
               strong_strength,
               { 0.4f, 0.8f, 0.3f, 1.f },
               "confined — no free quarks in this sim");

    draw_field("Higgs Field",
               higgs_strength,
               { 1.0f, 0.6f, 0.15f, 1.f },
               "couples to: H\xe2\x81\xb0  all massive particles\n  (vacuum vev = 246 GeV analogue)");

    draw_field("Gravity",
               grav_strength,
               { 0.65f, 0.55f, 0.45f, 1.f },
               "couples to: all massive particles");

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored({ 0.9f, 0.85f, 0.3f, 1.f }, "Vacuum & Decay");
    ImGui::Spacing();

    ImGui::Text("Active particles:   %u", active_particle_display);
    ImGui::Text("Dormant (pool):     %u", dormant_particle_display);
    ImGui::Text("Total photons:      %u", tc[PHOTON_TYPE]);
    ImGui::Text("Total decays:       %u", decay_total_display);
    ImGui::Text("Total energy:       %.1f", total_energy_display);
    ImGui::Spacing();

    // Mini-table of all SM particle counts
    ImGui::Separator();
    ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.f }, "SM Particle Census");
    if (ImGui::BeginTable("sm_census", 4,
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        struct SMEntry { const char* sym; uint32_t type_idx; };
        static const SMEntry SM_LIST[] = {
            { "\xce\xb3",            PHOTON_TYPE       },
            { "\xce\xb1",            ALPHA_TYPE        },
            { "e\xe2\x81\xbb",       ELECTRON_TYPE     },
            { "e\xe2\x81\x8a",       POSITRON_TYPE     },
            { "\xce\xbd\xe2\x82\x91",NEUTRINO_TYPE     },
            { "\xce\xbc\xe2\x81\xbb",MUON_TYPE         },
            { "\xcf\x84\xe2\x81\xbb",TAU_TYPE          },
            { "\xce\xbd\xce\xbc",    MU_NEUTRINO_TYPE  },
            { "\xce\xbd\xcf\x84",    TAU_NEUTRINO_TYPE },
            { "W\xc2\xb1",           W_BOSON_TYPE      },
            { "Z\xe2\x81\xb0",       Z_BOSON_TYPE      },
            { "H\xe2\x81\xb0",       HIGGS_TYPE        },
        };
        static constexpr int N_SM = 12;
        for (int k = 0; k < N_SM; ++k) {
            ImGui::TableNextColumn();
            uint32_t cnt = (SM_LIST[k].type_idx < MAX_PARTICLE_TYPES)
                         ? tc[SM_LIST[k].type_idx] : 0u;
            if (cnt > 0 && SM_LIST[k].type_idx < particles.colors.size()) {
                const glm::vec4& c = particles.colors[SM_LIST[k].type_idx];
                ImGui::TextColored({ c.r, c.g, c.b, 1.f },
                    "%s:%u", SM_LIST[k].sym, cnt);
            } else {
                ImGui::TextDisabled("%s:0", SM_LIST[k].sym);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Columns(1);

    // ── Bottom: inter-field coupling diagram (text schematic) ─────────────
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored({ 0.7f, 0.7f, 0.7f, 1.f }, "Field Coupling Schematic");
    ImGui::Spacing();

    // Color each segment by which field is active
    auto field_color = [](float s) -> ImVec4 {
        return s > 0.1f ? ImVec4(1.f, 1.f, 0.4f, 1.f)
                        : ImVec4(0.4f, 0.4f, 0.4f, 1.f);
    };
    // Draw schematic as colored text segments
    ImGui::TextColored({ 0.6f, 0.6f, 0.6f, 1.f },
        "Quarks/Gluons  -[Strong]->  Hadrons  -[Gravity]->  ...");
    ImGui::TextColored(field_color(em_strength),
        "e\xe2\x81\xbb + e\xe2\x81\x8a  -[EM]->  \xce\xb3 + \xce\xb3   (annihilation active: %s)",
        (tc[ELECTRON_TYPE] > 0 && tc[POSITRON_TYPE] > 0) ? "YES" : "no");
    ImGui::TextColored(field_color(weak_strength),
        "W\xc2\xb1 / Z\xe2\x81\xb0  -[Weak]->  leptons + \xce\xbd   (%u active bosons)",
        tc[W_BOSON_TYPE] + tc[Z_BOSON_TYPE]);
    ImGui::TextColored(field_color(higgs_strength),
        "H\xe2\x81\xb0  -[Higgs]->  2\xce\xb3   (%u Higgs present)", tc[HIGGS_TYPE]);
    ImGui::TextColored(field_color(grav_strength),
        "All mass  -[Gravity]->  clusters   (%u massive active)", active_particle_display);

    ImGui::End();
}

