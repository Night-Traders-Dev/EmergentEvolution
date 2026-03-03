#include "biochem/biochem_app.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

// ── Lifecycle ────────────────────────────────────────────────────────────────

void BiochemApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);

    // Seed initial population
    auto randf = [](float lo, float hi) {
        return lo + static_cast<float>(rand()) / RAND_MAX * (hi - lo);
    };

    // Cells
    for (int i = 0; i < 30; i++) {
        BioEntity e;
        e.pos    = {randf(200, 1400), randf(200, 700)};
        e.vel    = {randf(-20, 20), randf(-20, 20)};
        e.radius = randf(10, 16);
        e.energy = randf(80, 120);
        e.type   = BIO_CELL;
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
        state.entities.push_back(e);
    }

    // A few viruses
    for (int i = 0; i < 5; i++) {
        BioEntity e;
        e.pos    = {randf(400, 1200), randf(200, 700)};
        e.vel    = {randf(-50, 50), randf(-50, 50)};
        e.radius = 4.0f;
        e.energy = 30.0f;
        e.type   = BIO_VIRUS;
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

    // Simple interactions: eating nutrients, virus infection
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (ents[i].type != BIO_CELL && ents[i].type != BIO_BACTERIUM) continue;

        for (size_t j = 0; j < ents.size(); j++) {
            if (i == j || !ents[j].alive) continue;

            glm::vec2 diff = ents[j].pos - ents[i].pos;
            float dist = glm::length(diff);
            float touch = ents[i].radius + ents[j].radius;

            // Eat nutrients
            if (ents[j].type == BIO_NUTRIENT && dist < touch) {
                ents[i].energy += ents[j].energy;
                ents[j].alive = false;
            }
        }
    }

    // Remove dead entities periodically (every ~100 entities dead)
    size_t dead = 0;
    for (const auto& e : ents)
        if (!e.alive) dead++;
    if (dead > 100) {
        ents.erase(std::remove_if(ents.begin(), ents.end(),
            [](const BioEntity& e) { return !e.alive; }), ents.end());
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
    ImGui::End();

    // Settings panel
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Biochem Settings");
    ImGui::SliderFloat("Nutrient Rate",   &cfg.nutrient_rate,   0.1f, 10.0f);
    ImGui::SliderFloat("Metabolism",       &cfg.metabolism_rate,  0.1f, 5.0f);
    ImGui::SliderFloat("Division Energy",  &cfg.division_energy,  50.0f, 300.0f);
    ImGui::SliderFloat("Mutation Rate",    &cfg.mutation_rate,    0.0f, 0.1f, "%.3f");
    ImGui::SliderFloat("Infection Radius", &cfg.infection_radius, 5.0f, 50.0f);
    ImGui::SliderFloat("Immune Strength",  &cfg.immune_strength,  0.1f, 5.0f);
    ImGui::SliderFloat("Viscosity",        &cfg.viscosity,        0.90f, 1.0f, "%.3f");
    ImGui::SliderFloat("Time Scale",       &cfg.dt_scale,         0.1f, 5.0f);
    ImGui::Checkbox("Immune System",       &cfg.immune_system);
    ImGui::Checkbox("Energy Bars",         &cfg.show_energy_bars);
    ImGui::End();

    // Population stats
    ImGui::SetNextWindowPos({io.DisplaySize.x - 220.0f, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({210, 200}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Population");
    static const char* type_names[] = {
        "Cells", "Bacteria", "Viruses", "Nutrients",
        "Toxins", "Antibodies", "Red Blood", "White Blood"
    };
    static const ImVec4 type_colors[] = {
        {0.3f,0.7f,1.0f,1}, {0.9f,0.6f,0.2f,1}, {0.9f,0.2f,0.2f,1}, {0.3f,0.9f,0.3f,1},
        {0.8f,0.2f,0.8f,1}, {1.0f,1.0f,0.3f,1}, {0.9f,0.3f,0.3f,1}, {1.0f,1.0f,1.0f,1}
    };
    for (int t = 0; t < BIO_TYPE_COUNT; t++) {
        size_t n = state.count_type(static_cast<BioEntityType>(t));
        if (n > 0)
            ImGui::TextColored(type_colors[t], "%s: %zu", type_names[t], n);
    }
    ImGui::End();
}
