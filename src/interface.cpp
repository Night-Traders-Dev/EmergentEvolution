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
                              bool&            request_reset)
{
    request_reset = false;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

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

    // ── Glow ──────────────────────────────────────────────────────────────────
    ImGui::Checkbox("Glow Enabled (visual hint only)", &glow_enabled);

    // ── Particle force / color grid ───────────────────────────────────────────
    ImGui::SeparatorText("Particle Values");
    ImGui::TextDisabled("Hover + scroll: change force | Right-click: zero force");
    draw_particle_grid(cfg, particles);

    // ── Organism panel ────────────────────────────────────────────────────────
    draw_organism_panel(org_manager, particles, cfg);

    ImGui::Separator();
    ImGui::TextDisabled("F1: toggle UI  |  F2: reset  |  Space: pause");
    ImGui::TextDisabled("Drag: pan  |  Scroll: zoom  |  ESC: quit");
    ImGui::TextDisabled("CodeNoodles 2026");

    ImGui::End();

    ImGui::Render();
}

// ── Particle grid ─────────────────────────────────────────────────────────────

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
                if (ImGui::ColorButton("##col", ImVec4(c.r, c.g, c.b, c.a),
                                       ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(cell_size, cell_size)))
                {
                    // clicking opens the picker inline below
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
                // Left column: row color swatch
                uint32_t type_idx = row - 1;
                glm::vec4& c      = particles.colors[type_idx];
                ImGui::ColorButton("##row_col",
                    ImVec4(c.r, c.g, c.b, c.a),
                    ImGuiColorEditFlags_NoTooltip,
                    ImVec2(cell_size, cell_size));
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

// ── Organism panel ────────────────────────────────────────────────────────────

void Interface::draw_organism_panel(OrganismManager& org_manager,
                                     const Particles& particles,
                                     const SimConfig& cfg)
{
    if (!ImGui::CollapsingHeader("Organisms"))
        return;

    // Cluster radius slider
    ImGui::SliderFloat("Cluster Radius", &org_manager.cluster_radius, 10.0f, 200.0f, "%.0f");

    uint32_t org_count = static_cast<uint32_t>(org_manager.organisms.size());
    ImGui::Text("Active Organisms: %u", org_count);

    // Trait scales bar (per active type)
    uint32_t pt = cfg.particle_types;
    if (pt > MAX_PARTICLE_TYPES) pt = MAX_PARTICLE_TYPES;
    ImGui::TextDisabled("Force Scales (trait feedback):");
    for (uint32_t t = 0; t < pt; ++t) {
        ImGui::PushID(static_cast<int>(t));
        const glm::vec4& c = particles.colors[t];
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(c.r, c.g, c.b, 1.0f));
        char label[32];
        std::snprintf(label, sizeof(label), "%.2fx##scale%u", particles.trait_scales[t], t);
        float frac = (particles.trait_scales[t] - 1.0f) / 0.8f; // 1.0–1.8 → 0–1
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 6.0f), "");
        ImGui::SameLine();
        ImGui::Text("%.2fx", particles.trait_scales[t]);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    if (org_count == 0) {
        ImGui::TextDisabled("(no organisms detected yet)");
        return;
    }

    // Sort top 8 organisms by size (descending), without modifying the vector
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

    if (ImGui::BeginTable("orgtable", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Size",  ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Gen",   ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Kills", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Divs",  ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < show; ++i) {
            const Organism& o = *sorted[i];
            uint32_t dt = o.traits.dominant_type;
            const glm::vec4& c = (dt < MAX_PARTICLE_TYPES)
                                  ? particles.colors[dt]
                                  : glm::vec4(1.0f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::ColorButton("##tc", ImVec4(c.r, c.g, c.b, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(16, 14));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", o.traits.size);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", o.traits.avg_speed);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", o.traits.generation);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%u", o.traits.kills);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u", o.traits.divisions);
        }
        ImGui::EndTable();
    }
}
