#include "biochem/biochem_app.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>

// ── Draw helpers ────────────────────────────────────────────────────────────

static void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                              ImU32 center_col, ImU32 edge_col) {
    constexpr int STEPS = 16;
    for (int s = STEPS; s >= 0; --s) {
        float t = (float)s / STEPS;
        float r = radius * t;
        if (r < 1.0f) continue;
        float blend = 1.0f - t;
        int a_c = (center_col >> IM_COL32_A_SHIFT) & 0xFF;
        int a_e = (edge_col   >> IM_COL32_A_SHIFT) & 0xFF;
        int a = a_c + (int)((a_e - a_c) * blend);
        int r_c = (center_col >> IM_COL32_R_SHIFT) & 0xFF, r_e = (edge_col >> IM_COL32_R_SHIFT) & 0xFF;
        int g_c = (center_col >> IM_COL32_G_SHIFT) & 0xFF, g_e = (edge_col >> IM_COL32_G_SHIFT) & 0xFF;
        int b_c = (center_col >> IM_COL32_B_SHIFT) & 0xFF, b_e = (edge_col >> IM_COL32_B_SHIFT) & 0xFF;
        int rr = r_c + (int)((r_e - r_c) * blend);
        int gg = g_c + (int)((g_e - g_c) * blend);
        int bb = b_c + (int)((b_e - b_c) * blend);
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(rr, gg, bb, a), 32);
    }
}

// ── Entity type colors ──────────────────────────────────────────────────────

static const ImU32 TYPE_COLORS[] = {
    IM_COL32(70, 160, 255, 255),   // Cell - blue
    IM_COL32(230, 150, 50, 255),   // Bacterium - orange
    IM_COL32(220, 50, 50, 255),    // Virus - red
    IM_COL32(80, 220, 80, 255),    // Nutrient - green
    IM_COL32(200, 50, 200, 255),   // Toxin - purple
    IM_COL32(255, 255, 70, 255),   // Antibody - yellow
    IM_COL32(220, 70, 70, 255),    // Red blood - red
    IM_COL32(240, 240, 255, 255),  // White blood - white
};

static ImU32 type_glow(uint32_t type) {
    ImU32 c = TYPE_COLORS[type % BIO_TYPE_COUNT];
    int r = (c >> IM_COL32_R_SHIFT) & 0xFF;
    int g = (c >> IM_COL32_G_SHIFT) & 0xFF;
    int b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    return IM_COL32(r, g, b, 25);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void BiochemApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);

    std::mt19937 rng(42);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    // Cells
    for (int i = 0; i < 30; i++) {
        BioEntity e;
        e.pos    = {randf(200, 1400), randf(200, 700)};
        e.vel    = {randf(-20, 20), randf(-20, 20)};
        e.radius = randf(10, 16);
        e.energy = randf(80, 120);
        e.type   = BIO_CELL;
        e.genome = (uint32_t)rng();
        state.entities.push_back(e);
    }

    // Bacteria
    for (int i = 0; i < 15; i++) {
        BioEntity e;
        e.pos    = {randf(100, 1500), randf(100, 800)};
        e.vel    = {randf(-30, 30), randf(-30, 30)};
        e.radius = randf(5, 8);
        e.energy = randf(50, 80);
        e.type   = BIO_BACTERIUM;
        e.genome = (uint32_t)rng();
        state.entities.push_back(e);
    }

    // Viruses
    for (int i = 0; i < 5; i++) {
        BioEntity e;
        e.pos    = {randf(400, 1200), randf(200, 700)};
        e.vel    = {randf(-50, 50), randf(-50, 50)};
        e.radius = 4.0f;
        e.energy = 30.0f;
        e.type   = BIO_VIRUS;
        e.genome = (uint32_t)rng();
        state.entities.push_back(e);
    }

    // White blood cells
    for (int i = 0; i < 3; i++) {
        BioEntity e;
        e.pos    = {randf(300, 1300), randf(200, 700)};
        e.vel    = {randf(-15, 15), randf(-15, 15)};
        e.radius = 12.0f;
        e.energy = 200.0f;
        e.type   = BIO_WHITE_BLOOD;
        state.entities.push_back(e);
    }

    // Nutrients
    for (int i = 0; i < 40; i++)
        spawn_nutrient();

    cfg.entity_count = static_cast<uint32_t>(state.count());
}

void BiochemApp::destroy() {
    renderer.destroy(vk);
    vk.destroy();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void BiochemApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    if (!paused)
        step_simulation(dt);

    render_entities();
    render_ui();

    renderer.end_frame(vk);
}

// ── Simulation ──────────────────────────────────────────────────────────────

void BiochemApp::spawn_nutrient() {
    auto randf = [](float lo, float hi) {
        return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
    };
    BioEntity e;
    e.pos    = {randf(50, 1600), randf(50, 880)};
    e.vel    = {0, 0};
    e.radius = 3.0f;
    e.energy = 25.0f;
    e.type   = BIO_NUTRIENT;
    state.entities.push_back(e);
}

void BiochemApp::step_simulation(float dt) {
    float scaled_dt = dt * cfg.dt_scale;
    auto& ents = state.entities;

    // Spawn nutrients over time
    nutrient_timer_ += scaled_dt;
    float interval = 1.0f / std::max(cfg.nutrient_rate, 0.1f);
    while (nutrient_timer_ >= interval) {
        nutrient_timer_ -= interval;
        spawn_nutrient();
    }

    // Update each entity
    for (auto& e : ents) {
        if (!e.alive) continue;

        // Movement + damping
        e.pos += e.vel * scaled_dt;
        e.vel *= cfg.viscosity;
        e.age += scaled_dt;

        // Wrap around world
        if (e.pos.x < 0)    e.pos.x += 1648.0f;
        if (e.pos.x > 1648) e.pos.x -= 1648.0f;
        if (e.pos.y < 0)    e.pos.y += 928.0f;
        if (e.pos.y > 928)  e.pos.y -= 928.0f;

        // Metabolism — cells and bacteria consume energy
        if (e.type == BIO_CELL || e.type == BIO_BACTERIUM) {
            e.energy -= cfg.metabolism_rate * scaled_dt;
            if (e.energy <= 0) {
                e.alive = false;
                continue;
            }
        }

        // Viruses die after a while
        if (e.type == BIO_VIRUS && e.age > 30.0f)
            e.alive = false;
    }

    // Eating nutrients
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (ents[i].type != BIO_CELL && ents[i].type != BIO_BACTERIUM) continue;

        for (size_t j = 0; j < ents.size(); j++) {
            if (i == j || !ents[j].alive) continue;

            glm::vec2 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (ents[j].type == BIO_NUTRIENT && dist < touch) {
                ents[i].energy += ents[j].energy;
                ents[j].alive = false;
            }
        }
    }

    // Subsystems
    process_repulsion();
    process_cell_division();
    process_virus_infection(scaled_dt);
    if (cfg.immune_system)
        process_antibody_response(scaled_dt);

    // Remove dead entities periodically
    size_t dead = 0;
    for (const auto& e : ents)
        if (!e.alive) dead++;
    if (dead > 50) {
        // Adjust selected_entity index
        if (selected_entity >= 0) {
            int new_idx = 0;
            for (int i = 0; i < selected_entity && i < (int)ents.size(); i++) {
                if (ents[i].alive) new_idx++;
            }
            if (selected_entity < (int)ents.size() && ents[selected_entity].alive)
                selected_entity = new_idx;
            else
                selected_entity = -1;
        }
        ents.erase(std::remove_if(ents.begin(), ents.end(),
            [](const BioEntity& e) { return !e.alive; }), ents.end());
    }
}

// ── Cell Division ───────────────────────────────────────────────────────────

void BiochemApp::process_cell_division() {
    auto& ents = state.entities;
    size_t n = ents.size();

    // Cap total entities
    if (n > 1000) return;

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;
        if (e.type != BIO_CELL && e.type != BIO_BACTERIUM) continue;
        if (e.energy < cfg.division_energy) continue;

        // Split into 2 children
        BioEntity child;
        child.type = e.type;
        child.energy = e.energy * 0.5f;
        e.energy *= 0.5f;

        // Offset positions
        float angle = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
        glm::vec2 offset(cosf(angle) * e.radius, sinf(angle) * e.radius);
        child.pos = e.pos + offset;
        e.pos -= offset;

        child.vel = e.vel + glm::vec2(cosf(angle) * 5.0f, sinf(angle) * 5.0f);
        child.radius = e.radius * (0.9f + 0.2f * static_cast<float>(rand()) / RAND_MAX);
        child.genome = e.genome;

        // Mutation
        float roll = static_cast<float>(rand()) / RAND_MAX;
        if (roll < cfg.mutation_rate) {
            int bit = rand() % 32;
            child.genome ^= (1u << bit);
            // Vary size slightly based on mutation
            child.radius *= (0.85f + 0.3f * static_cast<float>(rand()) / RAND_MAX);
        }

        child.alive = true;
        child.age = 0.0f;
        ents.push_back(child);
    }
}

// ── Virus Infection ─────────────────────────────────────────────────────────

void BiochemApp::process_virus_infection(float dt) {
    auto& ents = state.entities;
    size_t n = ents.size();

    for (size_t i = 0; i < n; i++) {
        if (!ents[i].alive || ents[i].type != BIO_VIRUS) continue;

        for (size_t j = 0; j < n; j++) {
            if (i == j || !ents[j].alive) continue;
            if (ents[j].type != BIO_CELL) continue;

            glm::vec2 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);

            if (dist < cfg.infection_radius) {
                // Deal damage proportional to proximity
                float damage = cfg.infection_rate * dt * (1.0f - dist / cfg.infection_radius) * 20.0f;
                ents[j].energy -= damage;

                // Push virus toward cell
                if (dist > 1.0f) {
                    glm::vec2 dir = diff / dist;
                    ents[i].vel += dir * 30.0f * dt;
                }

                // Cell dies from infection → burst-spawn new viruses
                if (ents[j].energy < 30.0f && ents[j].alive) {
                    ents[j].alive = false;
                    int spawn_count = 2 + rand() % 2;
                    for (int k = 0; k < spawn_count && ents.size() < 1200; k++) {
                        BioEntity v;
                        float a = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
                        v.pos = ents[j].pos + glm::vec2(cosf(a) * 8.0f, sinf(a) * 8.0f);
                        v.vel = glm::vec2(cosf(a) * 40.0f, sinf(a) * 40.0f);
                        v.radius = 4.0f;
                        v.energy = 30.0f;
                        v.type = BIO_VIRUS;
                        v.genome = ents[i].genome;
                        ents.push_back(v);
                    }
                }
            }
        }
    }
}

// ── Antibody / Immune Response ──────────────────────────────────────────────

void BiochemApp::process_antibody_response(float dt) {
    auto& ents = state.entities;

    // Count viruses and WBCs
    size_t virus_count = 0, wbc_count = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.type == BIO_VIRUS) virus_count++;
        if (e.type == BIO_WHITE_BLOOD) wbc_count++;
    }

    // Auto-spawn WBCs when virus count exceeds WBC count
    if (virus_count > wbc_count && ents.size() < 1200) {
        BioEntity wbc;
        wbc.pos = {static_cast<float>(rand() % 1600 + 24),
                   static_cast<float>(rand() % 880 + 24)};
        wbc.vel = {0, 0};
        wbc.radius = 12.0f;
        wbc.energy = 200.0f;
        wbc.type = BIO_WHITE_BLOOD;
        ents.push_back(wbc);
    }

    // WBCs chase nearest virus/bacterium and destroy on contact
    for (auto& wbc : ents) {
        if (!wbc.alive || wbc.type != BIO_WHITE_BLOOD) continue;

        // Find nearest threat
        float best_dist = 999999.0f;
        int best_idx = -1;
        for (size_t j = 0; j < ents.size(); j++) {
            if (!ents[j].alive) continue;
            if (ents[j].type != BIO_VIRUS && ents[j].type != BIO_TOXIN) continue;
            float d = glm::length(ents[j].pos - wbc.pos);
            if (d < best_dist) {
                best_dist = d;
                best_idx = (int)j;
            }
        }

        if (best_idx >= 0) {
            // Chase
            glm::vec2 dir = ents[best_idx].pos - wbc.pos;
            float dist = glm::length(dir);
            if (dist > 1.0f) {
                dir /= dist;
                float chase_speed = 60.0f * cfg.immune_strength;
                wbc.vel += dir * chase_speed * dt;
                // Limit speed
                float spd = glm::length(wbc.vel);
                if (spd > 80.0f) wbc.vel *= 80.0f / spd;
            }

            // Destroy on contact
            float touch = wbc.radius + ents[best_idx].radius;
            if (dist < touch) {
                ents[best_idx].alive = false;
                wbc.energy += 10.0f; // small energy reward
            }
        }

        // WBCs slowly lose energy
        wbc.energy -= 0.3f * dt;
        if (wbc.energy <= 0) wbc.alive = false;
    }
}

// ── Repulsion ───────────────────────────────────────────────────────────────

void BiochemApp::process_repulsion() {
    auto& ents = state.entities;
    size_t n = ents.size();

    for (size_t i = 0; i < n; i++) {
        if (!ents[i].alive) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!ents[j].alive) continue;

            glm::vec2 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            if (dist < touch && dist > 0.1f) {
                float overlap = touch - dist;
                glm::vec2 dir = diff / dist;
                // Push proportional to overlap
                float push = overlap * 0.5f;
                ents[i].pos -= dir * push;
                ents[j].pos += dir * push;
                // Bounce velocities slightly
                ents[i].vel -= dir * push * 2.0f;
                ents[j].vel += dir * push * 2.0f;
            }
        }
    }
}

// ── Entity rendering (ImGui DrawList) ───────────────────────────────────────

void BiochemApp::render_entities() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;

    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // Dark blue-green background
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 12, 18, 255));

    // Subtle grid
    for (float x = 0; x < W; x += 60.0f)
        bg->AddLine(ImVec2(x, 0), ImVec2(x, H), IM_COL32(20, 35, 45, 60));
    for (float y = 0; y < H; y += 60.0f)
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(20, 35, 45, 60));

    auto& ents = state.entities;

    for (size_t i = 0; i < ents.size(); i++) {
        const auto& e = ents[i];
        if (!e.alive) continue;

        float ex = e.pos.x, ey = e.pos.y;
        ImU32 col = TYPE_COLORS[e.type % BIO_TYPE_COUNT];
        int cr = (col >> IM_COL32_R_SHIFT) & 0xFF;
        int cg = (col >> IM_COL32_G_SHIFT) & 0xFF;
        int cb = (col >> IM_COL32_B_SHIFT) & 0xFF;

        // Glow for cells and white blood cells
        if (e.type == BIO_CELL || e.type == BIO_WHITE_BLOOD) {
            draw_radial_glow(bg, ex, ey, e.radius * 3.0f,
                type_glow(e.type), IM_COL32(0, 0, 0, 0));
        }

        // Body circle
        bg->AddCircleFilled(ImVec2(ex, ey), e.radius, col, 24);
        bg->AddCircle(ImVec2(ex, ey), e.radius,
            IM_COL32(cr / 2, cg / 2, cb / 2, 180), 24, 1.0f);

        // Virus spike decorations
        if (e.type == BIO_VIRUS) {
            float spike_phase = e.age * 0.5f;
            for (int s = 0; s < 8; s++) {
                float a = spike_phase + s * (6.2832f / 8.0f);
                float sx1 = ex + cosf(a) * e.radius;
                float sy1 = ey + sinf(a) * e.radius;
                float sx2 = ex + cosf(a) * (e.radius + 4.0f);
                float sy2 = ey + sinf(a) * (e.radius + 4.0f);
                bg->AddLine(ImVec2(sx1, sy1), ImVec2(sx2, sy2),
                    IM_COL32(220, 50, 50, 150), 1.5f);
                bg->AddCircleFilled(ImVec2(sx2, sy2), 1.5f,
                    IM_COL32(220, 50, 50, 200));
            }
        }

        // Bacterium flagella decoration
        if (e.type == BIO_BACTERIUM) {
            float speed = glm::length(e.vel);
            if (speed > 1.0f) {
                glm::vec2 tail_dir = -glm::normalize(e.vel);
                for (int t = 0; t < 3; t++) {
                    float offset_a = (float)t * 0.5f - 0.5f;
                    float wave = sinf(e.age * 8.0f + (float)t * 2.0f) * 4.0f;
                    float tx = ex + tail_dir.x * (e.radius + 5.0f + t * 3.0f)
                              + tail_dir.y * (wave + offset_a * 3.0f);
                    float ty = ey + tail_dir.y * (e.radius + 5.0f + t * 3.0f)
                              - tail_dir.x * (wave + offset_a * 3.0f);
                    bg->AddLine(ImVec2(ex + tail_dir.x * e.radius,
                                       ey + tail_dir.y * e.radius),
                                ImVec2(tx, ty),
                                IM_COL32(230, 150, 50, 100), 1.0f);
                }
            }
        }

        // Energy bars above cells/bacteria
        if (cfg.show_energy_bars &&
            (e.type == BIO_CELL || e.type == BIO_BACTERIUM || e.type == BIO_WHITE_BLOOD)) {
            float bar_w = e.radius * 2.0f;
            float bar_h = 3.0f;
            float bar_x = ex - bar_w * 0.5f;
            float bar_y = ey - e.radius - 8.0f;
            float fill = std::clamp(e.energy / cfg.division_energy, 0.0f, 1.0f);

            // Background
            bg->AddRectFilled(ImVec2(bar_x, bar_y),
                ImVec2(bar_x + bar_w, bar_y + bar_h),
                IM_COL32(20, 20, 20, 180), 1.0f);
            // Fill (green → yellow → red)
            int gr = (int)(255 * (1.0f - fill));
            int gg = (int)(255 * fill);
            bg->AddRectFilled(ImVec2(bar_x, bar_y),
                ImVec2(bar_x + bar_w * fill, bar_y + bar_h),
                IM_COL32(gr, gg, 30, 200), 1.0f);
        }

        // Selection highlight
        if ((int)i == selected_entity) {
            bg->AddCircle(ImVec2(ex, ey), e.radius + 5.0f,
                IM_COL32(255, 255, 100, 200), 24, 2.0f);
        }
    }
}

// ── UI ──────────────────────────────────────────────────────────────────────

void BiochemApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();

    // Top bar
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({io.DisplaySize.x, 36});
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextColored({0.3f, 0.9f, 0.4f, 1.0f}, "Biochemical Simulator");
    ImGui::SameLine();
    ImGui::TextColored({0.5f, 0.5f, 0.6f, 1.0f},
        "  |  Entities: %zu alive", state.count_alive());
    ImGui::SameLine();
    if (ImGui::SmallButton(paused ? "Resume" : "Pause"))
        paused = !paused;
    ImGui::SameLine();
    ImGui::TextColored({0.4f, 0.4f, 0.5f, 1.0f}, "  %.0f FPS", io.Framerate);
    ImGui::SameLine();
    ImGui::TextColored({0.3f, 0.3f, 0.4f, 1.0f},
        "  |  Left-click: select   Escape: quit");
    ImGui::End();

    // Settings panel
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 340}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Biochem Settings");
    ImGui::SliderFloat("Nutrient Rate",   &cfg.nutrient_rate,   0.1f, 10.0f);
    ImGui::SliderFloat("Metabolism",       &cfg.metabolism_rate,  0.1f, 5.0f);
    ImGui::SliderFloat("Division Energy",  &cfg.division_energy,  50.0f, 300.0f);
    ImGui::SliderFloat("Mutation Rate",    &cfg.mutation_rate,    0.0f, 0.1f, "%.3f");
    ImGui::SliderFloat("Infection Radius", &cfg.infection_radius, 5.0f, 50.0f);
    ImGui::SliderFloat("Infection Rate",   &cfg.infection_rate,   0.1f, 2.0f);
    ImGui::SliderFloat("Immune Strength",  &cfg.immune_strength,  0.1f, 5.0f);
    ImGui::SliderFloat("Viscosity",        &cfg.viscosity,        0.90f, 1.0f, "%.3f");
    ImGui::SliderFloat("Time Scale",       &cfg.dt_scale,         0.1f, 5.0f);
    ImGui::Checkbox("Immune System",       &cfg.immune_system);
    ImGui::Checkbox("Energy Bars",         &cfg.show_energy_bars);
    ImGui::End();

    // Population stats
    ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210, 220}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Population");
    static const char* type_names[] = {
        "Cells", "Bacteria", "Viruses", "Nutrients",
        "Toxins", "Antibodies", "Red Blood", "White Blood"
    };
    static const ImVec4 type_colors_v[] = {
        {0.3f,0.7f,1.0f,1}, {0.9f,0.6f,0.2f,1}, {0.9f,0.2f,0.2f,1}, {0.3f,0.9f,0.3f,1},
        {0.8f,0.2f,0.8f,1}, {1.0f,1.0f,0.3f,1}, {0.9f,0.3f,0.3f,1}, {1.0f,1.0f,1.0f,1}
    };
    size_t total_alive = 0;
    for (int t = 0; t < BIO_TYPE_COUNT; t++) {
        size_t n = state.count_type(static_cast<BioEntityType>(t));
        total_alive += n;
        if (n > 0)
            ImGui::TextColored(type_colors_v[t], "%s: %zu", type_names[t], n);
    }
    ImGui::Separator();
    ImGui::Text("Total: %zu", total_alive);
    ImGui::End();

    // Entity inspector (when selected)
    if (selected_entity >= 0 && selected_entity < (int)state.entities.size()) {
        const auto& e = state.entities[selected_entity];
        if (e.alive) {
            ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 280}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({210, 220}, ImGuiCond_FirstUseEver);
            ImGui::Begin("Entity Inspector");

            ImGui::TextColored(type_colors_v[e.type % BIO_TYPE_COUNT],
                "%s #%d", type_names[e.type % BIO_TYPE_COUNT], selected_entity);
            ImGui::Separator();
            ImGui::Text("Energy:  %.1f", e.energy);
            ImGui::Text("Age:     %.1f s", e.age);
            ImGui::Text("Radius:  %.1f", e.radius);
            ImGui::Text("Speed:   %.1f", glm::length(e.vel));
            ImGui::Text("Pos:     (%.0f, %.0f)", e.pos.x, e.pos.y);
            ImGui::Text("Genome:  %08X", e.genome);
            ImGui::Spacing();
            if (ImGui::Button("Kill")) {
                state.entities[selected_entity].alive = false;
                selected_entity = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect"))
                selected_entity = -1;

            ImGui::End();
        } else {
            selected_entity = -1;
        }
    }
}
