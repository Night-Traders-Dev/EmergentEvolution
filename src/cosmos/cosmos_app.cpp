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

static const char* const CTYPE_NAMES[] = {
    "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula"
};

static const ImU32 CTYPE_COLORS[] = {
    IM_COL32(255, 200, 60, 255),   // Star - gold
    IM_COL32(60, 140, 220, 255),   // Planet - blue
    IM_COL32(180, 180, 190, 255),  // Moon - silver
    IM_COL32(140, 130, 110, 255),  // Asteroid - brown
    IM_COL32(160, 220, 255, 255),  // Comet - ice blue
    IM_COL32(200, 100, 255, 255),  // Black Hole - purple
    IM_COL32(120, 60, 180, 255),   // Nebula - violet
};

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

// ── Lifecycle ────────────────────────────────────────────────────────────────

static void seed_default_system(CosmosState& state, const CosmosConfig& cfg) {
    state.clear();

    // Sun at origin
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
}

void CosmosApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);
    raytracer_.init(vk, renderer.render_pass());

    seed_default_system(state, cfg);
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

void CosmosApp::reset_simulation() {
    seed_default_system(state, cfg);
    cfg.body_count = static_cast<uint32_t>(state.count());
    selected_body = -1;
    sim_time_ = 0.0f;
    camera = OrbitCamera{};
    camera.distance = 600.0f;
    camera.elevation = 0.5f;
    paused = false;
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    if (!paused && !show_splash) {
        step_physics(dt);
        sim_time_ += dt;
    }

    // GPU raytraced scene (draws within the active render pass)
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                io.DisplaySize.x, io.DisplaySize.y, sim_time_);

    // DrawList overlays (trails, selection)
    if (!show_splash && !show_pause_menu)
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

// ── Menu background (animated cosmic particles) ─────────────────────────────

void CosmosApp::draw_menu_background() {
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // Dark space fill
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(2, 4, 12, 255));

    // Init particles once
    if (!menu_bg_inited_) {
        menu_bg_inited_ = true;
        menu_particles_.resize(60);
        std::mt19937 rng(42);
        auto randf = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        for (auto& p : menu_particles_) {
            p.x = randf(0, W);
            p.y = randf(0, H);
            p.vx = randf(-12, 12);
            p.vy = randf(-12, 12);
            p.radius = randf(1.5f, 4.0f);
            // Cosmic colors: golds, oranges, blues, purples
            int variant = rng() % 4;
            if (variant == 0) { p.r = 1.0f; p.g = 0.7f; p.b = 0.2f; }       // gold
            else if (variant == 1) { p.r = 0.3f; p.g = 0.5f; p.b = 1.0f; }   // blue
            else if (variant == 2) { p.r = 0.6f; p.g = 0.3f; p.b = 0.9f; }   // purple
            else { p.r = 0.2f; p.g = 0.8f; p.b = 0.6f; }                     // teal
            p.alpha = randf(0.4f, 0.8f);
            for (int t = 0; t < 12; t++) { p.trail_x[t] = p.x; p.trail_y[t] = p.y; }
        }
    }

    menu_bg_time_ += io.DeltaTime;

    // Animated nebula glows
    for (int i = 0; i < 3; i++) {
        float phase = menu_bg_time_ * 0.15f + (float)i * 2.1f;
        float cx = W * (0.3f + 0.4f * sinf(phase));
        float cy = H * (0.3f + 0.4f * cosf(phase * 0.7f + 1.0f));
        float glow_r = 200.0f + 50.0f * sinf(phase * 1.3f);
        ImU32 center, edge;
        if (i == 0) { center = IM_COL32(255, 160, 40, 18); edge = IM_COL32(255, 80, 0, 0); }
        else if (i == 1) { center = IM_COL32(60, 100, 200, 14); edge = IM_COL32(30, 50, 180, 0); }
        else { center = IM_COL32(120, 50, 180, 12); edge = IM_COL32(80, 20, 140, 0); }
        draw_radial_glow(bg, cx, cy, glow_r, center, edge);
    }

    // Update + draw particles
    float dt = io.DeltaTime;
    for (auto& p : menu_particles_) {
        // Shift trail
        for (int t = 11; t > 0; t--) { p.trail_x[t] = p.trail_x[t-1]; p.trail_y[t] = p.trail_y[t-1]; }
        p.trail_x[0] = p.x;
        p.trail_y[0] = p.y;

        p.x += p.vx * dt;
        p.y += p.vy * dt;

        // Wrap
        if (p.x < -20) p.x += W + 40;
        if (p.x > W + 20) p.x -= W + 40;
        if (p.y < -20) p.y += H + 40;
        if (p.y > H + 20) p.y -= H + 40;

        // Draw trail
        for (int t = 1; t < 12; t++) {
            float frac = 1.0f - (float)t / 12.0f;
            int alpha = (int)(p.alpha * frac * 40.0f);
            bg->AddLine(ImVec2(p.trail_x[t-1], p.trail_y[t-1]),
                        ImVec2(p.trail_x[t], p.trail_y[t]),
                        IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha),
                        p.radius * frac * 0.6f);
        }

        // Draw particle
        int alpha = (int)(p.alpha * 255.0f);
        bg->AddCircleFilled(ImVec2(p.x, p.y), p.radius,
            IM_COL32((int)(p.r*255), (int)(p.g*255), (int)(p.b*255), alpha), 12);
    }

    // Force lines between nearby particles
    for (size_t i = 0; i < menu_particles_.size(); i++) {
        for (size_t j = i + 1; j < menu_particles_.size(); j++) {
            float dx = menu_particles_[j].x - menu_particles_[i].x;
            float dy = menu_particles_[j].y - menu_particles_[i].y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 100.0f) {
                float a = (1.0f - d / 100.0f) * 20.0f;
                bg->AddLine(ImVec2(menu_particles_[i].x, menu_particles_[i].y),
                            ImVec2(menu_particles_[j].x, menu_particles_[j].y),
                            IM_COL32(255, 180, 60, (int)a), 0.5f);
            }
        }
    }

    // Vignette
    float vig = std::min(W, H) * 0.6f;
    draw_radial_glow(bg, W * 0.5f, H * 0.5f, std::max(W, H) * 0.8f,
                     IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));

    // Subtle scanlines
    for (float y = 0; y < H; y += 3.0f) {
        bg->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(0, 0, 0, 8));
    }
}

// ── Splash screen ───────────────────────────────────────────────────────────

void CosmosApp::draw_splash_screen() {
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

    // Text overlay window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##CosmosSplash", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Central star glow
        float pulse = 0.7f + 0.3f * sinf(splash_time_ * 2.0f);
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 160.0f * pulse,
                         IM_COL32(255, 200, 60, 40), IM_COL32(255, 120, 0, 0));
        draw_radial_glow(dl, W * 0.5f, H * 0.4f, 80.0f * pulse,
                         IM_COL32(255, 240, 180, 60), IM_COL32(255, 200, 60, 0));

        // Orbiting dots
        for (int i = 0; i < 4; i++) {
            float orbit_r = 60.0f + (float)i * 35.0f;
            float speed = 0.8f - (float)i * 0.15f;
            float angle = splash_time_ * speed + (float)i * 1.57f;
            float px = W * 0.5f + cosf(angle) * orbit_r;
            float py = H * 0.4f + sinf(angle) * orbit_r * 0.4f;
            float dot_r = 3.0f + (float)i * 0.5f;
            dl->AddCircleFilled(ImVec2(px, py), dot_r,
                IM_COL32(60 + i * 40, 140 + i * 20, 220, 200), 12);
        }

        // Title — bottom-left
        float title_scale = 2.4f;
        ImGui::SetWindowFontScale(title_scale);

        const char* title1 = "Cosmic ";
        const char* title2 = "Sandbox";
        ImVec2 t1_size = ImGui::CalcTextSize(title1);
        ImVec2 t2_size = ImGui::CalcTextSize(title2);
        float title_x = 60.0f;
        float title_y = H - 120.0f;

        // Glow layers behind title
        for (int layer = 3; layer >= 0; layer--) {
            float offset = (float)layer * 1.5f;
            int alpha = 12 + layer * 6;
            dl->AddText(ImVec2(title_x - offset, title_y - offset),
                IM_COL32(255, 180, 40, alpha), title1);
            dl->AddText(ImVec2(title_x + t1_size.x - offset, title_y - offset),
                IM_COL32(255, 130, 0, alpha), title2);
        }

        // Solid title
        dl->AddText(ImVec2(title_x, title_y), IM_COL32(255, 220, 120, 255), title1);
        dl->AddText(ImVec2(title_x + t1_size.x, title_y), IM_COL32(255, 160, 40, 255), title2);

        // Badge — top right
        ImGui::SetWindowFontScale(1.0f);
        const char* badge = "CELESTIAL SIMULATION";
        ImVec2 badge_size = ImGui::CalcTextSize(badge);
        float badge_x = W - badge_size.x - 40.0f;
        float badge_y = 30.0f;
        float pad = 8.0f;
        dl->AddRect(ImVec2(badge_x - pad, badge_y - pad * 0.5f),
                    ImVec2(badge_x + badge_size.x + pad, badge_y + badge_size.y + pad * 0.5f),
                    IM_COL32(255, 160, 40, 200), 4.0f, 0, 1.5f);
        dl->AddText(ImVec2(badge_x, badge_y), IM_COL32(255, 180, 60, 230), badge);

        // Hint — bottom center, pulsing
        ImGui::SetWindowFontScale(1.0f);
        const char* hint = "Click or press any key to continue";
        ImVec2 hint_size = ImGui::CalcTextSize(hint);
        float hint_alpha = 120.0f + 80.0f * sinf(splash_time_ * 3.0f);
        dl->AddText(ImVec2(W * 0.5f - hint_size.x * 0.5f, H - 40.0f),
                    IM_COL32(200, 200, 210, (int)hint_alpha), hint);

        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

// ── Pause menu ──────────────────────────────────────────────────────────────

void CosmosApp::draw_pause_menu() {
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.75f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("##CosmosPause", nullptr, flags)) {
        float cx = W * 0.5f, cy = H * 0.5f;

        // Title
        ImGui::SetWindowFontScale(2.0f);
        const char* title = "PAUSED";
        ImVec2 title_size = ImGui::CalcTextSize(title);
        float title_y = cy - 160.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Glow behind title
        for (int layer = 2; layer >= 0; layer--) {
            float off = (float)layer * 2.0f;
            dl->AddText(ImVec2(cx - title_size.x * 0.5f - off, title_y - off),
                IM_COL32(255, 180, 40, 15 + layer * 10), title);
        }
        dl->AddText(ImVec2(cx - title_size.x * 0.5f, title_y),
            IM_COL32(255, 200, 80, 255), title);
        ImGui::SetWindowFontScale(1.0f);

        // Buttons
        float btn_w = 200.0f, btn_h = 40.0f, btn_spacing = 52.0f;
        float btn_x = cx - btn_w * 0.5f;
        float btn_y = cy - 60.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.14f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.18f, 0.22f, 0.35f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.25f, 0.30f, 0.45f, 1.00f));

        // Resume
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y));
        if (ImGui::Button("Resume", ImVec2(btn_w, btn_h))) {
            show_pause_menu = false;
            paused = false;
        }

        // New Simulation
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing));
        if (ImGui::Button("New Simulation", ImVec2(btn_w, btn_h))) {
            reset_simulation();
            show_pause_menu = false;
        }

        // Quit — red tinted
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
            IM_COL32(160, 160, 170, 100), hint);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ── Spawn menu ──────────────────────────────────────────────────────────────

void CosmosApp::draw_spawn_menu() {
    if (!spawn_menu_visible_) return;

    ImGui::SetNextWindowPos({10, 500}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({280, 360}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(320, 600));

    if (!ImGui::Begin("Spawn Bodies", &spawn_menu_visible_)) {
        ImGui::End();
        return;
    }

    // Type buttons in a grid
    if (ImGui::CollapsingHeader("Body Type", ImGuiTreeNodeFlags_DefaultOpen)) {
        float btn_w = 80.0f;
        for (int t = 0; t < CTYPE_COUNT; t++) {
            ImU32 col = CTYPE_COLORS[t];
            float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
            float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
            float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

            bool selected = (spawn_type == t);
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.9f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.25f, g * 0.25f, b * 0.25f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.4f, g * 0.4f, b * 0.4f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.5f, g * 0.5f, b * 0.5f, 1.0f));
            }

            if (ImGui::Button(CTYPE_NAMES[t], ImVec2(btn_w, 28))) {
                spawn_type = t;
                // Set sensible default mass for type
                if (t == CTYPE_STAR) spawn_mass = 100.0f;
                else if (t == CTYPE_PLANET) spawn_mass = 1.0f;
                else if (t == CTYPE_MOON) spawn_mass = 0.05f;
                else if (t == CTYPE_ASTEROID) spawn_mass = 0.02f;
                else if (t == CTYPE_COMET) spawn_mass = 0.01f;
                else if (t == CTYPE_BLACK_HOLE) spawn_mass = 200.0f;
                else if (t == CTYPE_NEBULA) spawn_mass = 0.001f;
            }

            if (selected) {
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
            } else {
                ImGui::PopStyleColor(3);
            }

            // 3 per row
            if (t % 3 != 2 && t < CTYPE_COUNT - 1) ImGui::SameLine();
        }
    }

    ImGui::Separator();

    // Properties
    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Mass", &spawn_mass, 0.001f, 500.0f, "%.3f",
                            ImGuiSliderFlags_Logarithmic);

        ImGui::Checkbox("Orbital velocity", &spawn_in_orbit_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawn with circular orbital velocity\naround the nearest massive body");
    }

    ImGui::Separator();

    // Spawn button
    {
        ImU32 col = CTYPE_COLORS[spawn_type % CTYPE_COUNT];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r * 0.4f, g * 0.4f, b * 0.4f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 0.6f, g * 0.6f, b * 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r * 0.7f, g * 0.7f, b * 0.7f, 1.0f));

        char label[64];
        snprintf(label, sizeof(label), "Spawn %s at Camera", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
        if (ImGui::Button(label, ImVec2(-1, 36))) {
            CelestialBody nb;
            nb.pos = camera.target;
            nb.vel = glm::vec3(0.0f);
            nb.mass = spawn_mass;
            nb.radius = std::max(3.0f, std::cbrt(spawn_mass) * 5.0f);
            nb.type = (uint32_t)spawn_type;

            // Set temperature based on type
            if (spawn_type == CTYPE_STAR) nb.temperature = 5778.0f;
            else if (spawn_type == CTYPE_BLACK_HOLE) nb.temperature = 0.0f;
            else nb.temperature = 300.0f;

            // Set radius based on type
            if (spawn_type == CTYPE_STAR) nb.radius = std::max(15.0f, std::cbrt(spawn_mass) * 6.0f);
            else if (spawn_type == CTYPE_BLACK_HOLE) nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);

            // Orbital velocity if enabled
            if (spawn_in_orbit_ && !state.bodies.empty()) {
                // Find nearest massive body
                int nearest = -1;
                float nearest_dist = 1e9f;
                for (size_t i = 0; i < state.bodies.size(); i++) {
                    float d = glm::length(state.bodies[i].pos - nb.pos);
                    if (d > 0.1f && d < nearest_dist && state.bodies[i].mass > nb.mass) {
                        nearest_dist = d;
                        nearest = (int)i;
                    }
                }
                if (nearest >= 0) {
                    nb.parent = nearest;
                    glm::vec3 diff = nb.pos - state.bodies[nearest].pos;
                    float dist = glm::length(diff);
                    if (dist > 0.1f) {
                        float v = std::sqrt(cfg.G * state.bodies[nearest].mass / dist);
                        // Perpendicular velocity in XZ plane
                        glm::vec3 dir = glm::normalize(diff);
                        glm::vec3 perp(-dir.z, 0.0f, dir.x);
                        nb.vel = state.bodies[nearest].vel + perp * v;
                    }
                }
            }

            state.bodies.push_back(nb);
            state.trails.emplace_back();
        }
        ImGui::PopStyleColor(3);
    }

    // Quick spawn presets
    if (ImGui::CollapsingHeader("Quick Presets")) {
        if (ImGui::Button("Add Solar System", ImVec2(-1, 0))) {
            glm::vec3 offset = camera.target;
            CelestialBody s;
            s.pos = offset; s.mass = 100.0f; s.radius = 30.0f;
            s.temperature = 5778.0f; s.type = CTYPE_STAR;
            int star_idx = (int)state.bodies.size();
            state.bodies.push_back(s); state.trails.emplace_back();

            float radii[] = {80, 140, 210, 300};
            float masses[] = {0.3f, 0.8f, 0.5f, 1.5f};
            for (int i = 0; i < 4; i++) {
                CelestialBody p;
                float angle = (float)i * 1.57f;
                p.pos = offset + glm::vec3(cosf(angle) * radii[i], 0, sinf(angle) * radii[i]);
                float v = std::sqrt(cfg.G * s.mass / radii[i]);
                p.vel = s.vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                p.mass = masses[i]; p.radius = 6 + masses[i] * 3;
                p.type = CTYPE_PLANET; p.parent = star_idx;
                state.bodies.push_back(p); state.trails.emplace_back();
            }
        }

        if (ImGui::Button("Add Binary Stars", ImVec2(-1, 0))) {
            glm::vec3 center = camera.target;
            float sep = 60.0f;
            float v = std::sqrt(cfg.G * 50.0f / sep);

            CelestialBody s1;
            s1.pos = center + glm::vec3(sep * 0.5f, 0, 0);
            s1.vel = glm::vec3(0, 0, v * 0.5f);
            s1.mass = 50.0f; s1.radius = 22.0f; s1.temperature = 8000.0f;
            s1.type = CTYPE_STAR;
            state.bodies.push_back(s1); state.trails.emplace_back();

            CelestialBody s2;
            s2.pos = center - glm::vec3(sep * 0.5f, 0, 0);
            s2.vel = glm::vec3(0, 0, -v * 0.5f);
            s2.mass = 50.0f; s2.radius = 22.0f; s2.temperature = 3500.0f;
            s2.type = CTYPE_STAR;
            state.bodies.push_back(s2); state.trails.emplace_back();
        }

        if (ImGui::Button("Add Asteroid Belt", ImVec2(-1, 0))) {
            std::mt19937 rng((unsigned)sim_time_);
            auto randf = [&](float lo, float hi) {
                return std::uniform_real_distribution<float>(lo, hi)(rng);
            };
            // Find nearest star for orbital reference
            float nearest_mass = 100.0f;
            glm::vec3 nearest_pos = camera.target;
            glm::vec3 nearest_vel(0);
            for (auto& b : state.bodies) {
                if (b.type == CTYPE_STAR) {
                    float d = glm::length(b.pos - camera.target);
                    if (d < 800.0f) {
                        nearest_mass = b.mass;
                        nearest_pos = b.pos;
                        nearest_vel = b.vel;
                    }
                }
            }
            for (int i = 0; i < 30; i++) {
                CelestialBody a;
                float r = randf(400.0f, 500.0f);
                float angle = randf(0, 6.2832f);
                a.pos = nearest_pos + glm::vec3(cosf(angle) * r, randf(-10, 10), sinf(angle) * r);
                float v = std::sqrt(cfg.G * nearest_mass / r) * randf(0.9f, 1.1f);
                a.vel = nearest_vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                a.mass = randf(0.01f, 0.05f); a.radius = randf(2, 4);
                a.type = CTYPE_ASTEROID;
                state.bodies.push_back(a); state.trails.emplace_back();
            }
        }
    }

    ImGui::End();
}

// ── UI ──────────────────────────────────────────────────────────────────────

void CosmosApp::render_ui() {
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
        "  |  Drag: orbit  Scroll: zoom  Escape: menu  Right-click: select");
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
        const char* tn = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "?";

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

    // Spawn menu
    draw_spawn_menu();

    // Body list
    ImGui::SetNextWindowPos({io.DisplaySize.x - 280.0f, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bodies");
    for (size_t i = 0; i < state.count(); i++) {
        const auto& b = state.bodies[i];
        const char* tn = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "?";
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
