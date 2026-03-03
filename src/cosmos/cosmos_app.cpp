#include "cosmos/cosmos_app.h"
#include "imgui.h"
#include <cmath>

// ── Lifecycle ────────────────────────────────────────────────────────────────

void CosmosApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);

    // Seed a simple solar system for demonstration
    CelestialBody sun;
    sun.pos  = {824.0f, 464.0f};   // center of world
    sun.vel  = {0.0f, 0.0f};
    sun.mass = 100.0f;
    sun.radius = 30.0f;
    sun.temperature = 5778.0f;
    sun.type = CTYPE_STAR;
    state.bodies.push_back(sun);

    // A few planets in circular orbits
    const float orbit_radii[] = {100.0f, 170.0f, 250.0f, 350.0f};
    const float planet_mass[] = {0.5f, 1.0f, 0.8f, 2.0f};
    for (int i = 0; i < 4; i++) {
        CelestialBody p;
        float angle = static_cast<float>(i) * 1.57f;
        p.pos  = sun.pos + glm::vec2(std::cos(angle), std::sin(angle)) * orbit_radii[i];
        // Circular orbit velocity: v = sqrt(G * M / r)
        float v = std::sqrt(cfg.G * sun.mass / orbit_radii[i]);
        p.vel  = glm::vec2(-std::sin(angle), std::cos(angle)) * v;
        p.mass = planet_mass[i];
        p.radius = 8.0f + planet_mass[i] * 3.0f;
        p.temperature = 300.0f;
        p.type = CTYPE_PLANET;
        p.parent = 0;
        state.bodies.push_back(p);
    }

    cfg.body_count = static_cast<uint32_t>(state.count());
}

void CosmosApp::destroy() {
    renderer.destroy(vk);
    vk.destroy();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    if (!paused)
        step_physics(dt);

    render_ui();

    renderer.end_frame(vk);
}

// ── Physics (CPU N-body) ─────────────────────────────────────────────────────

void CosmosApp::step_physics(float dt) {
    float scaled_dt = dt * cfg.dt_scale;
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    // Compute gravitational acceleration
    std::vector<glm::vec2> accel(n, {0.0f, 0.0f});
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            glm::vec2 diff = bodies[j].pos - bodies[i].pos;
            float dist2 = glm::dot(diff, diff) + cfg.softening * cfg.softening;
            float dist  = std::sqrt(dist2);
            float force = cfg.G * bodies[i].mass * bodies[j].mass / dist2;
            glm::vec2 dir = diff / dist;

            accel[i] += dir * (force / bodies[i].mass);
            accel[j] -= dir * (force / bodies[j].mass);
        }
    }

    // Integrate (symplectic Euler)
    for (size_t i = 0; i < n; i++) {
        bodies[i].vel += accel[i] * scaled_dt;
        bodies[i].vel *= cfg.damping;
        bodies[i].pos += bodies[i].vel * scaled_dt;
    }
}

// ── UI ──────────────────────────────────────────────────────────────────────

void CosmosApp::render_ui() {
    ImGuiIO& io = ImGui::GetIO();

    // Top bar
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({io.DisplaySize.x, 36});
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "Cosmic Sandbox");
    ImGui::SameLine();
    ImGui::TextColored({0.5f, 0.5f, 0.6f, 1.0f}, "  |  Bodies: %zu", state.count());
    ImGui::SameLine();
    if (ImGui::SmallButton(paused ? "Resume" : "Pause"))
        paused = !paused;
    ImGui::SameLine();
    ImGui::TextColored({0.4f, 0.4f, 0.5f, 1.0f},
        "  %.0f FPS", io.Framerate);
    ImGui::End();

    // Settings panel
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({240, 220}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Cosmos Settings");
    ImGui::SliderFloat("G",          &cfg.G,        0.1f, 10.0f);
    ImGui::SliderFloat("Time Scale", &cfg.dt_scale,  0.1f, 10.0f);
    ImGui::SliderFloat("Softening",  &cfg.softening, 1.0f, 50.0f);
    ImGui::Checkbox("Collisions",    &cfg.collisions);
    ImGui::Checkbox("Tidal Forces",  &cfg.tidal_forces);
    ImGui::Checkbox("Show Orbits",   &cfg.show_orbits);
    ImGui::Checkbox("Show Trails",   &cfg.show_trails);
    ImGui::End();

    // Body list
    ImGui::SetNextWindowPos({io.DisplaySize.x - 260.0f, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({250, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bodies");
    static const char* type_names[] = {"Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula"};
    for (size_t i = 0; i < state.count(); i++) {
        const auto& b = state.bodies[i];
        const char* tn = (b.type < CTYPE_COUNT) ? type_names[b.type] : "?";
        ImGui::Text("%zu. %s  m=%.1f", i, tn, b.mass);
    }
    ImGui::End();
}
