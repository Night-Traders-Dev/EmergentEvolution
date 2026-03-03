#include "cosmos/cosmos_app.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <random>

// ── Body color (for trail overlay coloring) ─────────────────────────────────

static ImU32 body_color(const CelestialBody& b) {
    switch (b.type) {
    case CTYPE_STAR: {
        float t = std::clamp((b.temperature - 2000.0f) / 30000.0f, 0.0f, 1.0f);
        int r = (int)(255 * std::clamp(1.4f - t * 1.2f, 0.0f, 1.0f));
        int g = (int)(255 * std::clamp(0.8f + t * 0.2f - std::abs(t - 0.4f), 0.0f, 1.0f));
        int blue = (int)(255 * std::clamp(t * 1.8f - 0.3f, 0.0f, 1.0f));
        return IM_COL32(r, g, std::min(blue, 255), 255);
    }
    case CTYPE_PLANET:     return IM_COL32(60, 140, 220, 255);
    case CTYPE_MOON:       return IM_COL32(180, 180, 190, 255);
    case CTYPE_ASTEROID:   return IM_COL32(140, 130, 110, 255);
    case CTYPE_COMET:      return IM_COL32(160, 220, 255, 255);
    case CTYPE_BLACK_HOLE: return IM_COL32(200, 100, 255, 255);
    case CTYPE_NEBULA:     return IM_COL32(120, 60, 180, 255);
    default:               return IM_COL32(200, 200, 200, 255);
    }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void CosmosApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);
    raytracer_.init(vk, renderer.render_pass());

    // Seed a solar system: sun at origin, planets in XZ plane (Y is up)
    CelestialBody sun;
    sun.pos  = {0.0f, 0.0f, 0.0f};
    sun.vel  = {0.0f, 0.0f, 0.0f};
    sun.mass = 100.0f;
    sun.radius = 30.0f;
    sun.temperature = 5778.0f;
    sun.type = CTYPE_STAR;
    state.bodies.push_back(sun);

    // Planets in circular orbits in the XZ plane
    const float orbit_radii[] = {100.0f, 170.0f, 250.0f, 350.0f};
    const float planet_mass[] = {0.5f, 1.0f, 0.8f, 2.0f};
    const float planet_temp[] = {700.0f, 300.0f, 200.0f, 120.0f};
    for (int i = 0; i < 4; i++) {
        CelestialBody p;
        float angle = static_cast<float>(i) * 1.57f;
        p.pos  = glm::vec3(std::cos(angle) * orbit_radii[i], 0.0f,
                            std::sin(angle) * orbit_radii[i]);
        float v = std::sqrt(cfg.G * sun.mass / orbit_radii[i]);
        p.vel  = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        p.mass = planet_mass[i];
        p.radius = 8.0f + planet_mass[i] * 3.0f;
        p.temperature = planet_temp[i];
        p.type = CTYPE_PLANET;
        p.parent = 0;
        state.bodies.push_back(p);
    }

    // A moon
    {
        CelestialBody moon;
        moon.pos = state.bodies[2].pos + glm::vec3(25.0f, 0.0f, 0.0f);
        float moon_v = std::sqrt(cfg.G * state.bodies[2].mass / 25.0f);
        moon.vel = state.bodies[2].vel + glm::vec3(0.0f, 0.0f, moon_v);
        moon.mass = 0.05f;
        moon.radius = 4.0f;
        moon.temperature = 200.0f;
        moon.type = CTYPE_MOON;
        moon.parent = 2;
        state.bodies.push_back(moon);
    }

    // Asteroids
    std::mt19937 rng(42);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 20; i++) {
        CelestialBody a;
        float r = randf(420.0f, 500.0f);
        float angle = randf(0.0f, 6.2832f);
        a.pos = glm::vec3(std::cos(angle) * r, randf(-10.0f, 10.0f),
                           std::sin(angle) * r);
        float v = std::sqrt(cfg.G * sun.mass / r) * randf(0.9f, 1.1f);
        a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        a.mass = randf(0.01f, 0.05f);
        a.radius = randf(2.0f, 4.0f);
        a.temperature = 100.0f;
        a.type = CTYPE_ASTEROID;
        state.bodies.push_back(a);
    }

    state.trails.resize(state.bodies.size());
    cfg.body_count = static_cast<uint32_t>(state.count());

    camera.target = {0, 0, 0};
    camera.distance = 600.0f;
    camera.elevation = 0.5f;
    camera.azimuth = 0.0f;
}

void CosmosApp::destroy() {
    vkDeviceWaitIdle(vk.device);
    raytracer_.destroy(vk);
    renderer.destroy(vk);
    vk.destroy();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    if (!paused) {
        step_physics(dt);
        sim_time_ += dt;
    }

    // GPU raytraced scene (draws within the active render pass)
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                io.DisplaySize.x, io.DisplaySize.y, sim_time_);

    // DrawList overlays (trails, selection)
    render_overlay();

    // ImGui UI panels
    render_ui();

    renderer.end_frame(vk);
}

// ── Physics (CPU N-body, 3D) ────────────────────────────────────────────────

void CosmosApp::step_physics(float dt) {
    float scaled_dt = dt * cfg.dt_scale;
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    // Compute gravitational acceleration
    std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist2 = glm::dot(diff, diff) + cfg.softening * cfg.softening;
            float dist  = std::sqrt(dist2);
            float force = cfg.G * bodies[i].mass * bodies[j].mass / dist2;
            glm::vec3 dir = diff / dist;

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

    // Collisions
    if (cfg.collisions) {
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                glm::vec3 diff = bodies[j].pos - bodies[i].pos;
                float dist = glm::length(diff);
                float touch = bodies[i].radius + bodies[j].radius;
                if (dist < touch && dist > 0.01f) {
                    glm::vec3 dir = diff / dist;
                    float overlap = touch - dist;
                    float total_mass = bodies[i].mass + bodies[j].mass;
                    bodies[i].pos -= dir * overlap * (bodies[j].mass / total_mass);
                    bodies[j].pos += dir * overlap * (bodies[i].mass / total_mass);

                    glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
                    float vel_along = glm::dot(rel_vel, dir);
                    if (vel_along < 0) {
                        float restitution = 0.8f;
                        float impulse = -(1.0f + restitution) * vel_along / total_mass;
                        bodies[i].vel -= dir * impulse * bodies[j].mass;
                        bodies[j].vel += dir * impulse * bodies[i].mass;
                    }
                }
            }
        }
    }

    // Update trails
    while (state.trails.size() < n)
        state.trails.emplace_back();
    for (size_t i = 0; i < n; i++) {
        state.trails[i].push_back(bodies[i].pos);
        while (state.trails[i].size() > cfg.trail_length)
            state.trails[i].pop_front();
    }
}

// ── 3D Projection (for overlay) ─────────────────────────────────────────────

CosmosApp::Projected CosmosApp::project(const glm::vec3& world_pos,
                                         const glm::mat4& vp,
                                         float screen_w, float screen_h) const {
    glm::vec4 clip = vp * glm::vec4(world_pos, 1.0f);
    if (clip.w <= 0.0f)
        return {0, 0, 0, false};

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x * 0.5f + 0.5f) * screen_w;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * screen_h;

    bool visible = (ndc.x >= -1.2f && ndc.x <= 1.2f &&
                    ndc.y >= -1.2f && ndc.y <= 1.2f &&
                    ndc.z >= 0.0f && ndc.z <= 1.0f);
    return {sx, sy, clip.w, visible};
}

float CosmosApp::screen_radius(float world_radius, float depth,
                                float fov_rad, float screen_h) const {
    if (depth <= 0.0f) return 0.0f;
    return (world_radius / depth) * (screen_h / (2.0f * std::tan(fov_rad * 0.5f)));
}

// ── Overlay rendering (trails + selection on DrawList) ──────────────────────

void CosmosApp::render_overlay() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float aspect = W / H;
    float fov_rad = glm::radians(camera.fov);

    glm::mat4 view = camera.view_matrix();
    glm::mat4 proj = camera.proj_matrix(aspect);
    glm::mat4 vp = proj * view;

    ImDrawList* fg = ImGui::GetForegroundDrawList();

    // Draw trails
    if (cfg.show_trails) {
        for (size_t i = 0; i < state.trails.size() && i < state.bodies.size(); i++) {
            auto& trail = state.trails[i];
            if (trail.size() < 2) continue;

            ImU32 c = body_color(state.bodies[i]);
            int cr = (c >> IM_COL32_R_SHIFT) & 0xFF;
            int cg = (c >> IM_COL32_G_SHIFT) & 0xFF;
            int cb = (c >> IM_COL32_B_SHIFT) & 0xFF;

            for (size_t j = 1; j < trail.size(); j++) {
                auto p0 = project(trail[j - 1], vp, W, H);
                auto p1 = project(trail[j], vp, W, H);
                if (!p0.visible || !p1.visible) continue;

                float frac = (float)j / (float)trail.size();
                int alpha = (int)(frac * 80.0f);
                float width = 1.0f + frac * 1.5f;
                fg->AddLine(ImVec2(p0.sx, p0.sy), ImVec2(p1.sx, p1.sy),
                            IM_COL32(cr, cg, cb, alpha), width);
            }
        }
    }

    // Selection highlight
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        const auto& b = state.bodies[selected_body];
        auto p = project(b.pos, vp, W, H);
        if (p.visible) {
            float sr = screen_radius(b.radius, p.depth, fov_rad, H);
            sr = std::max(sr, 4.0f);
            fg->AddCircle(ImVec2(p.sx, p.sy), sr + 4.0f,
                IM_COL32(255, 255, 100, 180), 32, 2.0f);
        }
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
    ImGui::TextColored({0.4f, 0.4f, 0.5f, 1.0f}, "  %.0f FPS", io.Framerate);
    ImGui::SameLine();
    ImGui::TextColored({0.3f, 0.3f, 0.4f, 1.0f},
        "  |  Drag: orbit  Scroll: zoom  R: reset  Right-click: select");
    ImGui::End();

    // Settings panel
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 440}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Cosmos Settings");
    ImGui::SliderFloat("G",          &cfg.G,        0.1f, 10.0f);
    ImGui::SliderFloat("Time Scale", &cfg.dt_scale,  0.1f, 30.0f);
    ImGui::SliderFloat("Softening",  &cfg.softening, 1.0f, 50.0f);
    ImGui::Checkbox("Collisions",    &cfg.collisions);
    ImGui::Checkbox("Tidal Forces",  &cfg.tidal_forces);
    ImGui::Checkbox("Show Trails",   &cfg.show_trails);

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::SliderFloat("FOV", &camera.fov, 20.0f, 90.0f);
    float log_dist = std::log10(camera.distance);
    if (ImGui::SliderFloat("Distance", &log_dist, 1.0f, 3.7f, "10^%.1f")) {
        camera.distance = std::pow(10.0f, log_dist);
    }
    int trail_len = (int)cfg.trail_length;
    if (ImGui::SliderInt("Trail Length", &trail_len, 0, 500))
        cfg.trail_length = (uint32_t)trail_len;
    if (ImGui::Button("Reset Camera"))
        camera = OrbitCamera{};

    ImGui::Separator();
    ImGui::Text("Lighting");
    ImGui::Checkbox("Star Lighting",    &cfg.star_lighting);
    ImGui::Checkbox("Uniform Lighting", &cfg.uniform_lighting);
    if (cfg.star_lighting)
        ImGui::SliderFloat("Ambient", &cfg.ambient_strength, 0.0f, 0.5f);

    ImGui::End();

    // Body Inspector (when selected)
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        const auto& b = state.bodies[selected_body];
        static const char* type_names[] = {
            "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula"
        };
        const char* tn = (b.type < CTYPE_COUNT) ? type_names[b.type] : "?";

        ImGui::SetNextWindowPos({io.DisplaySize.x - 280.0f, 46}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({270, 260}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Body Inspector");

        ImU32 c = body_color(b);
        ImVec4 cv((float)((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                  (float)((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                  (float)((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f, 1.0f);
        ImGui::TextColored(cv, "%s #%d", tn, selected_body);
        ImGui::Separator();
        ImGui::Text("Mass:        %.2f", b.mass);
        ImGui::Text("Radius:      %.1f", b.radius);
        ImGui::Text("Temperature: %.0f K", b.temperature);
        ImGui::Text("Position:    (%.0f, %.0f, %.0f)", b.pos.x, b.pos.y, b.pos.z);
        ImGui::Text("Speed:       %.2f", glm::length(b.vel));
        if (b.parent >= 0 && b.parent < (int)state.bodies.size()) {
            float orbit_r = glm::length(b.pos - state.bodies[b.parent].pos);
            ImGui::Text("Orbit radius: %.0f", orbit_r);
        }
        ImGui::Spacing();
        if (ImGui::Button("Focus Camera")) {
            camera.target = b.pos;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect")) {
            selected_body = -1;
        }
        ImGui::End();
    }

    // Spawn tool
    ImGui::SetNextWindowPos({10, 500}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 140}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Spawn Tool");
    static const char* spawn_type_names[] = {
        "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula"
    };
    ImGui::Combo("Type", &spawn_type, spawn_type_names, CTYPE_COUNT);
    ImGui::SliderFloat("Mass", &spawn_mass, 0.01f, 200.0f, "%.2f",
                        ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Spawn at Camera Target")) {
        CelestialBody nb;
        nb.pos = camera.target;
        nb.vel = glm::vec3(0.0f);
        nb.mass = spawn_mass;
        nb.radius = std::max(3.0f, spawn_mass * 2.0f);
        nb.type = (uint32_t)spawn_type;
        nb.temperature = (spawn_type == CTYPE_STAR) ? 5778.0f : 300.0f;
        state.bodies.push_back(nb);
        state.trails.emplace_back();
    }
    ImGui::End();

    // Body list
    ImGui::SetNextWindowPos({io.DisplaySize.x - 280.0f, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bodies");
    static const char* type_names[] = {
        "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula"
    };
    for (size_t i = 0; i < state.count(); i++) {
        const auto& b = state.bodies[i];
        const char* tn = (b.type < CTYPE_COUNT) ? type_names[b.type] : "?";
        bool is_sel = ((int)i == selected_body);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.3f, 1));

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu. %s m=%.1f", i, tn, b.mass);
        if (ImGui::Selectable(buf, is_sel))
            selected_body = (int)i;

        if (is_sel) ImGui::PopStyleColor();
    }
    ImGui::End();
}
