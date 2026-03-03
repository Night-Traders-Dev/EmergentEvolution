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

static const char* const BIO_TYPE_NAMES[] = {
    "Cell", "Bacterium", "Virus", "Nutrient",
    "Toxin", "Antibody", "Red Blood Cell", "White Blood Cell"
};

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

static void seed_default_population(BiochemState& state) {
    state.clear();

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
    for (int i = 0; i < 40; i++) {
        BioEntity e;
        e.pos = {randf(50, 1600), randf(50, 880)};
        e.radius = 3.0f;
        e.energy = 25.0f;
        e.type = BIO_NUTRIENT;
        state.entities.push_back(e);
    }
}

void BiochemApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);

    seed_default_population(state);
    cfg.entity_count = static_cast<uint32_t>(state.count());
}

void BiochemApp::destroy() {
    renderer.destroy(vk);
    vk.destroy();
}

void BiochemApp::reset_simulation() {
    seed_default_population(state);
    cfg.entity_count = static_cast<uint32_t>(state.count());
    selected_entity = -1;
    nutrient_timer_ = 0.0f;
    paused = false;
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void BiochemApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    if (!paused && !show_splash)
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
    if (n > 1000) return;

    for (size_t i = 0; i < n; i++) {
        auto& e = ents[i];
        if (!e.alive) continue;
        if (e.type != BIO_CELL && e.type != BIO_BACTERIUM) continue;
        if (e.energy < cfg.division_energy) continue;

        BioEntity child;
        child.type = e.type;
        child.energy = e.energy * 0.5f;
        e.energy *= 0.5f;

        float angle = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
        glm::vec2 offset(cosf(angle) * e.radius, sinf(angle) * e.radius);
        child.pos = e.pos + offset;
        e.pos -= offset;

        child.vel = e.vel + glm::vec2(cosf(angle) * 5.0f, sinf(angle) * 5.0f);
        child.radius = e.radius * (0.9f + 0.2f * static_cast<float>(rand()) / RAND_MAX);
        child.genome = e.genome;

        float roll = static_cast<float>(rand()) / RAND_MAX;
        if (roll < cfg.mutation_rate) {
            int bit = rand() % 32;
            child.genome ^= (1u << bit);
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
                float damage = cfg.infection_rate * dt * (1.0f - dist / cfg.infection_radius) * 20.0f;
                ents[j].energy -= damage;

                if (dist > 1.0f) {
                    glm::vec2 dir = diff / dist;
                    ents[i].vel += dir * 30.0f * dt;
                }

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

    size_t virus_count = 0, wbc_count = 0;
    for (auto& e : ents) {
        if (!e.alive) continue;
        if (e.type == BIO_VIRUS) virus_count++;
        if (e.type == BIO_WHITE_BLOOD) wbc_count++;
    }

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

    for (auto& wbc : ents) {
        if (!wbc.alive || wbc.type != BIO_WHITE_BLOOD) continue;

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
            glm::vec2 dir = ents[best_idx].pos - wbc.pos;
            float dist = glm::length(dir);
            if (dist > 1.0f) {
                dir /= dist;
                float chase_speed = 60.0f * cfg.immune_strength;
                wbc.vel += dir * chase_speed * dt;
                float spd = glm::length(wbc.vel);
                if (spd > 80.0f) wbc.vel *= 80.0f / spd;
            }

            float touch = wbc.radius + ents[best_idx].radius;
            if (dist < touch) {
                ents[best_idx].alive = false;
                wbc.energy += 10.0f;
            }
        }

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
                float push = overlap * 0.5f;
                ents[i].pos -= dir * push;
                ents[j].pos += dir * push;
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

    // Don't draw entities during overlays
    if (show_splash || show_pause_menu) return;

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

            bg->AddRectFilled(ImVec2(bar_x, bar_y),
                ImVec2(bar_x + bar_w, bar_y + bar_h),
                IM_COL32(20, 20, 20, 180), 1.0f);
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

// ── Menu background (animated bio particles) ────────────────────────────────

void BiochemApp::draw_menu_background() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // Dark biological background
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(3, 10, 16, 255));

    // Init particles once
    if (!menu_bg_inited_) {
        menu_bg_inited_ = true;
        menu_particles_.resize(50);
        std::mt19937 rng(99);
        auto randf = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        for (auto& p : menu_particles_) {
            p.x = randf(0, W);
            p.y = randf(0, H);
            p.vx = randf(-10, 10);
            p.vy = randf(-10, 10);
            p.radius = randf(2.0f, 6.0f);
            // Bio colors: greens, blues, teals, light purples
            int variant = rng() % 4;
            if (variant == 0) { p.r = 0.2f; p.g = 0.7f; p.b = 0.3f; }       // green
            else if (variant == 1) { p.r = 0.3f; p.g = 0.5f; p.b = 0.9f; }   // blue
            else if (variant == 2) { p.r = 0.2f; p.g = 0.7f; p.b = 0.7f; }   // teal
            else { p.r = 0.6f; p.g = 0.3f; p.b = 0.7f; }                     // purple
            p.alpha = randf(0.3f, 0.7f);
            for (int t = 0; t < 12; t++) { p.trail_x[t] = p.x; p.trail_y[t] = p.y; }
        }
    }

    menu_bg_time_ += io.DeltaTime;

    // Animated cellular glows
    for (int i = 0; i < 3; i++) {
        float phase = menu_bg_time_ * 0.12f + (float)i * 2.1f;
        float cx = W * (0.3f + 0.4f * sinf(phase));
        float cy = H * (0.3f + 0.4f * cosf(phase * 0.7f + 1.0f));
        float glow_r = 180.0f + 40.0f * sinf(phase * 1.3f);
        ImU32 center, edge;
        if (i == 0) { center = IM_COL32(30, 120, 60, 16); edge = IM_COL32(15, 60, 30, 0); }
        else if (i == 1) { center = IM_COL32(40, 80, 160, 14); edge = IM_COL32(20, 40, 100, 0); }
        else { center = IM_COL32(80, 40, 120, 12); edge = IM_COL32(40, 20, 80, 0); }
        draw_radial_glow(bg, cx, cy, glow_r, center, edge);
    }

    // Update + draw particles
    float dt = io.DeltaTime;
    for (auto& p : menu_particles_) {
        for (int t = 11; t > 0; t--) { p.trail_x[t] = p.trail_x[t-1]; p.trail_y[t] = p.trail_y[t-1]; }
        p.trail_x[0] = p.x;
        p.trail_y[0] = p.y;

        p.x += p.vx * dt;
        p.y += p.vy * dt;

        if (p.x < -20) p.x += W + 40;
        if (p.x > W + 20) p.x -= W + 40;
        if (p.y < -20) p.y += H + 40;
        if (p.y > H + 20) p.y -= H + 40;

        // Draw trail
        for (int t = 1; t < 12; t++) {
            float frac = 1.0f - (float)t / 12.0f;
            int alpha = (int)(p.alpha * frac * 35.0f);
            bg->AddLine(ImVec2(p.trail_x[t-1], p.trail_y[t-1]),
                        ImVec2(p.trail_x[t], p.trail_y[t]),
                        IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha),
                        p.radius * frac * 0.5f);
        }

        // Draw particle (cell-like)
        int alpha = (int)(p.alpha * 255.0f);
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha), 16);
        // Membrane ring
        bg->AddCircle(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*180), (int)(p.g*180), (int)(p.b*180), alpha / 2), 16, 0.8f);
    }

    // Connection lines (membrane-like)
    for (size_t i = 0; i < menu_particles_.size(); i++) {
        for (size_t j = i + 1; j < menu_particles_.size(); j++) {
            float dx = menu_particles_[j].x - menu_particles_[i].x;
            float dy = menu_particles_[j].y - menu_particles_[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 90.0f) {
                float a = (1.0f - d / 90.0f) * 18.0f;
                bg->AddLine(ImVec2(menu_particles_[i].x, menu_particles_[i].y),
                            ImVec2(menu_particles_[j].x, menu_particles_[j].y),
                            IM_COL32(60, 180, 100, (int)a), 0.5f);
            }
        }
    }

    // Vignette
    draw_radial_glow(bg, W * 0.5f, H * 0.5f, std::max(W, H) * 0.8f,
                     IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));

    // Scanlines
    for (float y = 0; y < H; y += 3.0f)
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(0, 0, 0, 6));
}

// ── Splash screen ───────────────────────────────────────────────────────────

void BiochemApp::draw_splash_screen() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    splash_time_ += io.DeltaTime;

    // Dismiss after 0.3s on any input
    if (splash_time_ > 0.3f) {
        bool dismiss = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                       ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!dismiss) {
            const ImGuiKey keys[] = {
                ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Escape,
                ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E,
                ImGuiKey_F, ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J,
                ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N, ImGuiKey_O,
                ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T,
                ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y,
                ImGuiKey_Z, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
            };
            for (auto k : keys) {
                if (ImGui::IsKeyPressed(k)) { dismiss = true; break; }
            }
        }
        if (dismiss) {
            show_splash = false;
            return;
        }
    }

    // Text overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##BiochemSplash", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Central cell glow
        float pulse = 0.7f + 0.3f * sinf(splash_time_ * 1.8f);
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 140.0f * pulse,
                         IM_COL32(40, 180, 80, 35), IM_COL32(20, 100, 40, 0));
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 70.0f * pulse,
                         IM_COL32(80, 220, 120, 50), IM_COL32(40, 160, 60, 0));

        // Cell membrane + nucleus visualization
        float cell_r = 50.0f * pulse;
        dl->AddCircle(ImVec2(W * 0.5f, H * 0.4f), cell_r,
            IM_COL32(60, 200, 100, 120), 32, 2.0f);
        dl->AddCircleFilled(ImVec2(W * 0.5f, H * 0.4f), cell_r * 0.3f,
            IM_COL32(40, 120, 180, 80), 16);

        // Orbiting organelles
        for (int i = 0; i < 5; i++) {
            float orbit_r = 25.0f + (float)i * 12.0f;
            float speed = 0.6f - (float)i * 0.08f;
            float angle = splash_time_ * speed + (float)i * 1.256f;
            float px = W * 0.5f + cosf(angle) * orbit_r;
            float py = H * 0.4f + sinf(angle) * orbit_r;
            ImU32 orga_col;
            if (i == 0) orga_col = IM_COL32(70, 160, 255, 180);
            else if (i == 1) orga_col = IM_COL32(230, 150, 50, 180);
            else if (i == 2) orga_col = IM_COL32(80, 220, 80, 180);
            else if (i == 3) orga_col = IM_COL32(220, 50, 50, 150);
            else orga_col = IM_COL32(200, 50, 200, 150);
            dl->AddCircleFilled(ImVec2(px, py), 3.0f + (float)i * 0.3f, orga_col, 8);
        }

        // Title — bottom-left
        float title_scale = 2.4f;
        ImGui::SetWindowFontScale(title_scale);

        const char* title1 = "Biochemical ";
        const char* title2 = "Simulator";
        ImVec2 t1_size = ImGui::CalcTextSize(title1);
        float title_x = 60.0f;
        float title_y = H - 120.0f;

        // Glow layers
        for (int layer = 3; layer >= 0; layer--) {
            float offset = (float)layer * 1.5f;
            int alpha = 12 + layer * 6;
            dl->AddText(ImVec2(title_x - offset, title_y - offset),
                IM_COL32(40, 200, 80, alpha), title1);
            dl->AddText(ImVec2(title_x + t1_size.x - offset, title_y - offset),
                IM_COL32(20, 160, 60, alpha), title2);
        }

        // Solid title
        dl->AddText(ImVec2(title_x, title_y), IM_COL32(100, 240, 140, 255), title1);
        dl->AddText(ImVec2(title_x + t1_size.x, title_y), IM_COL32(60, 200, 100, 255), title2);

        // Badge — top right
        ImGui::SetWindowFontScale(1.0f);
        const char* badge = "CELLULAR BIOLOGY SANDBOX";
        ImVec2 badge_size = ImGui::CalcTextSize(badge);
        float badge_x = W - badge_size.x - 40.0f;
        float badge_y = 30.0f;
        float pad = 8.0f;
        dl->AddRect(ImVec2(badge_x - pad, badge_y - pad * 0.5f),
                    ImVec2(badge_x + badge_size.x + pad, badge_y + badge_size.y + pad * 0.5f),
                    IM_COL32(60, 200, 100, 200), 4.0f, 0, 1.5f);
        dl->AddText(ImVec2(badge_x, badge_y), IM_COL32(80, 220, 120, 230), badge);

        // Hint — bottom center, pulsing
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = 120.0f + 80.0f * sinf(splash_time_ * 3.0f);
        dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 40.0f),
                    IM_COL32(180, 220, 190, (int)hint_alpha), hint);

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ── Pause menu ──────────────────────────────────────────────────────────────

void BiochemApp::draw_pause_menu() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;

    // Escape to resume
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        show_pause_menu = false;
        paused = false;
        return;
    }

    // Semi-transparent overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.04f, 0.06f, 0.78f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##BiochemPause", nullptr, flags)) {
        float cx = W * 0.5f, cy = H * 0.5f;

        // Title
        ImGui::SetWindowFontScale(2.0f);
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_y = cy - 160.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int layer = 2; layer >= 0; layer--) {
            float off = (float)layer * 2.0f;
            dl->AddText(ImVec2(cx - title_size.x * 0.5f - off, title_y - off),
                IM_COL32(60, 200, 100, 15 + layer * 10), title);
        }
        dl->AddText(ImVec2(cx - title_size.x * 0.5f, title_y),
            IM_COL32(80, 230, 120, 255), title);
        ImGui::SetWindowFontScale(1.0f);

        // Buttons
        float btn_w = 200.0f, btn_h = 40.0f, btn_spacing = 52.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.16f, 0.14f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.14f, 0.24f, 0.20f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.20f, 0.32f, 0.28f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            paused = false;
        }

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            reset_simulation();
            show_pause_menu = false;
        }

        // Quit
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Quit", ImVec2(btn_w, btn_h))) {
            request_quit = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(); // FrameRounding

        // Hint
        const char* hint = "Press Escape to resume";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(cx - hint_size.x * 0.5f, H - 60.0f),
            IM_COL32(140, 170, 150, 100), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Spawn menu ──────────────────────────────────────────────────────────────

void BiochemApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGui::SetNextWindowPos({10, 400}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 340}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(240, 180), ImVec2(300, 500));

    if (!ImGui::Begin("Spawn Entities", &spawn_menu_visible_)) {
        ImGui::End();
        return;
    }

    // Type buttons
    if (ImGui::CollapsingHeader("Entity Type", ImGuiTreeNodeFlags_DefaultOpen)) {
        float btn_w = 110.0f;
        for (int t = 0; t < BIO_TYPE_COUNT; t++) {
            ImU32 col = TYPE_COLORS[t];
            float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

            bool selected = (spawn_bio_type_ == t);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.9f, 0.5f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.2f, g * 0.2f, b * 0.2f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.45f, g * 0.45f, b * 0.45f, 1.0f));
            }

            if (ImGui::Button(BIO_TYPE_NAMES[t], ImVec2(btn_w, 26))) {
                spawn_bio_type_ = t;
                // Default energy by type
                if (t == BIO_CELL) spawn_energy_ = 100.0f;
                else if (t == BIO_BACTERIUM) spawn_energy_ = 60.0f;
                else if (t == BIO_VIRUS) spawn_energy_ = 30.0f;
                else if (t == BIO_NUTRIENT) spawn_energy_ = 25.0f;
                else if (t == BIO_TOXIN) spawn_energy_ = 50.0f;
                else if (t == BIO_ANTIBODY) spawn_energy_ = 80.0f;
                else if (t == BIO_RED_BLOOD) spawn_energy_ = 100.0f;
                else if (t == BIO_WHITE_BLOOD) spawn_energy_ = 200.0f;
            }

            if (selected) {
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
            } else {
                ImGui::PopStyleColor(3);
            }

            // 2 per row
            if (t % 2 == 0 && t < BIO_TYPE_COUNT - 1) ImGui::SameLine();
        }
    }

    ImGui::Separator();

    // Properties
    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Energy", &spawn_energy_, 5.0f, 500.0f, "%.0f",
                            ImGuiSliderFlags_Logarithmic);
    }

    ImGui::Separator();

    // Spawn button
    {
        int t = spawn_bio_type_ % BIO_TYPE_COUNT;
        ImU32 col = TYPE_COLORS[t];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.35f, g * 0.35f, b * 0.35f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.65f, g * 0.65f, b * 0.65f, 1.0f));

        char label[64];
        snprintf(label, sizeof(label), "Spawn %s", BIO_TYPE_NAMES[t]);
        if (ImGui::Button(label, ImVec2(-1, 34))) {
            ImGuiIO& io = ImGui::GetIO();
            BioEntity e;
            // Spawn at random position
            e.pos = {static_cast<float>(rand() % (int)io.DisplaySize.x),
                     static_cast<float>(rand() % (int)io.DisplaySize.y)};
            e.vel = {0, 0};
            e.energy = spawn_energy_;
            e.type = (uint32_t)spawn_bio_type_;
            e.genome = (uint32_t)rand();

            // Set radius by type
            if (t == BIO_CELL) e.radius = 12.0f;
            else if (t == BIO_BACTERIUM) e.radius = 6.0f;
            else if (t == BIO_VIRUS) e.radius = 4.0f;
            else if (t == BIO_NUTRIENT) e.radius = 3.0f;
            else if (t == BIO_TOXIN) e.radius = 4.0f;
            else if (t == BIO_ANTIBODY) e.radius = 5.0f;
            else if (t == BIO_RED_BLOOD) e.radius = 5.0f;
            else if (t == BIO_WHITE_BLOOD) e.radius = 12.0f;

            state.entities.push_back(e);
        }
        ImGui::PopStyleColor(3);
    }

    // Quick spawn presets
    if (ImGui::CollapsingHeader("Quick Presets")) {
        if (ImGui::Button("Add Cell Colony (10)", ImVec2(-1, 0))) {
            ImGuiIO& io = ImGui::GetIO();
            for (int i = 0; i < 10; i++) {
                BioEntity e;
                float cx = io.DisplaySize.x * 0.5f;
                float cy = io.DisplaySize.y * 0.5f;
                float angle = (float)i / 10.0f * 6.2832f;
                e.pos = {cx + cosf(angle) * 40.0f, cy + sinf(angle) * 40.0f};
                e.vel = {cosf(angle) * 5.0f, sinf(angle) * 5.0f};
                e.radius = 12.0f;
                e.energy = 100.0f;
                e.type = BIO_CELL;
                e.genome = (uint32_t)rand();
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Virus Outbreak (8)", ImVec2(-1, 0))) {
            for (int i = 0; i < 8; i++) {
                BioEntity e;
                float angle = (float)i / 8.0f * 6.2832f;
                e.pos = {800 + cosf(angle) * 60.0f, 450 + sinf(angle) * 60.0f};
                e.vel = {cosf(angle) * 30.0f, sinf(angle) * 30.0f};
                e.radius = 4.0f;
                e.energy = 30.0f;
                e.type = BIO_VIRUS;
                e.genome = (uint32_t)rand();
                state.entities.push_back(e);
            }
        }

        if (ImGui::Button("Nutrient Burst (20)", ImVec2(-1, 0))) {
            for (int i = 0; i < 20; i++)
                spawn_nutrient();
        }

        if (ImGui::Button("Immune Response (5 WBC)", ImVec2(-1, 0))) {
            for (int i = 0; i < 5; i++) {
                BioEntity e;
                e.pos = {static_cast<float>(rand() % 1600 + 24),
                         static_cast<float>(rand() % 880 + 24)};
                e.radius = 12.0f;
                e.energy = 200.0f;
                e.type = BIO_WHITE_BLOOD;
                state.entities.push_back(e);
            }
        }
    }

    ImGui::End();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void BiochemApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();

    bool any_overlay = show_splash || show_pause_menu;

    // Animated background for fullscreen overlays
    if (any_overlay)
        draw_menu_background();

    // Splash screen blocks all other UI
    if (show_splash) {
        draw_splash_screen();
        return;
    }

    // Pause menu blocks all other UI
    if (show_pause_menu) {
        draw_pause_menu();
        return;
    }

    // ── Normal UI ────────────────────────────────────────────────────────────

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
        "  |  Left-click: select   Escape: menu");
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
    static const char* pop_type_names[] = {
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
            ImGui::TextColored(type_colors_v[t], "%s: %zu", pop_type_names[t], n);
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
                "%s #%d", pop_type_names[e.type % BIO_TYPE_COUNT], selected_entity);
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

    // Spawn menu
    draw_spawn_menu();
}
