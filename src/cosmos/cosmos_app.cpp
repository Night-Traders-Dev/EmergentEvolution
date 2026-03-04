#include "cosmos/cosmos_app.h"
#include "common/paths.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <random>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <thread>

// ── Body color (for trail overlay coloring) ─────────────────────────────────

static ImU32 body_color(const CelestialBody& b) {
    if (is_star_type(b.type)) {
        float t = std::clamp((b.temperature - 2000.0f) / 30000.0f, 0.0f, 1.0f);
        int r = (int)(255 * std::clamp(1.4f - t * 1.2f, 0.0f, 1.0f));
        int g = (int)(255 * std::clamp(0.8f + t * 0.2f - std::abs(t - 0.4f), 0.0f, 1.0f));
        int blue = (int)(255 * std::clamp(t * 1.8f - 0.3f, 0.0f, 1.0f));
        return IM_COL32(r, g, std::min(blue, 255), 255);
    }
    if (is_black_hole_type(b.type))
        return IM_COL32(200, 100, 255, 255);
    switch (b.type) {
    case CTYPE_PLANET:     return IM_COL32(60, 140, 220, 255);
    case CTYPE_MOON:       return IM_COL32(180, 180, 190, 255);
    case CTYPE_ASTEROID:   return IM_COL32(140, 130, 110, 255);
    case CTYPE_COMET:      return IM_COL32(160, 220, 255, 255);
    case CTYPE_NEBULA:     return IM_COL32(120, 60, 180, 255);
    default:               return IM_COL32(200, 200, 200, 255);
    }
}

static const char* const CTYPE_NAMES[] = {
    "Star", "Planet", "Moon", "Asteroid", "Comet", "Black Hole", "Nebula",
    "O Star", "B Star", "A Star", "F Star", "G Star", "K Star", "M Star",
    "L Dwarf", "T Dwarf", "Y Dwarf", "Wolf-Rayet",
    "Stellar BH", "Intermediate BH", "Supermassive BH", "Primordial BH",
};

static const ImU32 CTYPE_COLORS[] = {
    IM_COL32(255, 200, 60, 255),   // Star - gold
    IM_COL32(60, 140, 220, 255),   // Planet - blue
    IM_COL32(180, 180, 190, 255),  // Moon - silver
    IM_COL32(140, 130, 110, 255),  // Asteroid - brown
    IM_COL32(160, 220, 255, 255),  // Comet - ice blue
    IM_COL32(200, 100, 255, 255),  // Black Hole - purple
    IM_COL32(120, 60, 180, 255),   // Nebula - violet
    IM_COL32(120, 140, 255, 255),  // O - deep blue
    IM_COL32(160, 180, 255, 255),  // B - blue-white
    IM_COL32(220, 220, 255, 255),  // A - white
    IM_COL32(255, 255, 200, 255),  // F - yellow-white
    IM_COL32(255, 240, 100, 255),  // G - yellow (Sun)
    IM_COL32(255, 180, 60, 255),   // K - orange
    IM_COL32(255, 100, 60, 255),   // M - red
    IM_COL32(180, 60, 40, 255),    // L - dark red-brown
    IM_COL32(140, 40, 60, 255),    // T - magenta-brown
    IM_COL32(100, 30, 50, 255),    // Y - very dark
    IM_COL32(100, 180, 255, 255),  // WR - hot blue
    IM_COL32(180, 80, 220, 255),   // Stellar BH
    IM_COL32(160, 60, 200, 255),   // Intermediate BH
    IM_COL32(140, 40, 180, 255),   // Supermassive BH
    IM_COL32(200, 100, 240, 255),  // Primordial BH
};

// ── Star / BH classification helpers ────────────────────────────────────────

static uint32_t classify_star_spectral(float temperature, float mass) {
    if (temperature > 40000.0f && mass > 16.0f)  return CTYPE_STAR_WR;
    if (temperature > 30000.0f && mass > 16.0f)  return CTYPE_STAR_O;
    if (temperature > 10000.0f && mass > 2.1f)   return CTYPE_STAR_B;
    if (temperature > 7500.0f  && mass > 1.4f)   return CTYPE_STAR_A;
    if (temperature > 6000.0f  && mass > 1.04f)  return CTYPE_STAR_F;
    if (temperature > 5200.0f  && mass > 0.8f)   return CTYPE_STAR_G;
    if (temperature > 3700.0f  && mass > 0.45f)  return CTYPE_STAR_K;
    if (temperature > 2400.0f  && mass > 0.08f)  return CTYPE_STAR_M;
    if (temperature > 1300.0f)                    return CTYPE_STAR_L;
    if (temperature > 500.0f)                     return CTYPE_STAR_T;
    return CTYPE_STAR_Y;
}

static constexpr float EARTH_RADIUS_KM_REAL = 6371.0f;
static constexpr float EARTH_RADIUS_SIM_UNITS = 8.0f;
static constexpr float SIM_UNIT_TO_KM = EARTH_RADIUS_KM_REAL / EARTH_RADIUS_SIM_UNITS;


static uint32_t classify_black_hole(float mass) {
    if (mass < 3.0f)        return CTYPE_BH_PRIMORDIAL;
    if (mass <= 20.0f)      return CTYPE_BH_STELLAR;
    if (mass <= 100000.0f)  return CTYPE_BH_INTERMEDIATE;
    return CTYPE_BH_SUPERMASSIVE;
}

static void randomize_planet_properties(CelestialBody& body, const CosmosState& state,
                                        std::mt19937& rng) {
    // Sample from broad observed exoplanet-like ranges.
    // Internal mass unit is solar masses; 1 Earth mass ~= 3.003e-6 solar masses.
    constexpr float EARTH_MASS_TO_SOLAR = 3.003e-6f;
    constexpr float PI = 3.14159265359f;

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // Log-uniform planet masses: ~0.2 Earth masses to ~3 Jupiter masses.
    float log_m_earth = std::uniform_real_distribution<float>(
        std::log10(0.2f), std::log10(1000.0f))(rng);
    float mass_earth = std::pow(10.0f, log_m_earth);
    body.mass = std::clamp(mass_earth * EARTH_MASS_TO_SOLAR, 1.0e-7f, 0.01f);

    // Mass-radius relation (very simplified):
    // rocky (<~2 M_earth), sub-neptune, then gas-giant plateau.
    float radius_earth = 1.0f;
    if (mass_earth < 2.0f) {
        radius_earth = std::pow(mass_earth, 0.28f);
    } else if (mass_earth < 130.0f) {
        radius_earth = 1.5f * std::pow(mass_earth, 0.30f);
    } else {
        radius_earth = 11.0f * std::pow(mass_earth / 318.0f, 0.04f);
    }
    radius_earth = std::clamp(radius_earth, 0.35f, 13.0f);
    body.radius = radius_earth * EARTH_RADIUS_SIM_UNITS;

    // Estimate equilibrium temperature from nearest star if available.
    // Using relative stellar scaling: T_eq ~ T_star * sqrt(R_star/(2d)) * (1-A)^(1/4)
    int nearest_star = -1;
    float nearest_dist = 1e30f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        if (!is_star_type(b.type)) continue;
        float d = glm::length(b.pos - body.pos);
        if (d > 1e-3f && d < nearest_dist) {
            nearest_dist = d;
            nearest_star = (int)i;
        }
    }

    float albedo = std::uniform_real_distribution<float>(0.08f, 0.75f)(rng);
    if (nearest_star >= 0) {
        const auto& s = state.bodies[(size_t)nearest_star];
        float ratio = std::sqrt(std::max(s.radius, 1.0f) / (2.0f * std::max(nearest_dist, 1.0f)));
        float eq_t = s.temperature * ratio * std::pow(std::max(1.0f - albedo, 0.05f), 0.25f);
        float greenhouse = std::uniform_real_distribution<float>(0.9f, 1.35f)(rng);
        body.temperature = std::clamp(eq_t * greenhouse, 60.0f, 2500.0f);
    } else {
        body.temperature = std::uniform_real_distribution<float>(90.0f, 700.0f)(rng);
    }

    // Rotation period in hours: small rocky planets trend faster than giants.
    float period_hours;
    if (mass_earth < 2.0f) {
        period_hours = std::uniform_real_distribution<float>(10.0f, 120.0f)(rng);
    } else if (mass_earth < 130.0f) {
        period_hours = std::uniform_real_distribution<float>(8.0f, 50.0f)(rng);
    } else {
        period_hours = std::uniform_real_distribution<float>(6.0f, 20.0f)(rng);
    }
    body.angular_vel = (2.0f * PI) / (period_hours * 3600.0f);
    if (u01(rng) < 0.15f) body.angular_vel *= -1.0f; // occasional retrograde spin
}

// ── Timestep formatting ─────────────────────────────────────────────────────

static const char* format_sim_time(double seconds, char* buf, size_t buf_size) {
    double abs_s = std::abs(seconds);
    if (abs_s < 1e-6)
        snprintf(buf, buf_size, "%.1f ns", seconds * 1e9);
    else if (abs_s < 1e-3)
        snprintf(buf, buf_size, "%.1f us", seconds * 1e6);
    else if (abs_s < 1.0)
        snprintf(buf, buf_size, "%.1f ms", seconds * 1e3);
    else if (abs_s < 60.0)
        snprintf(buf, buf_size, "%.1f s", seconds);
    else if (abs_s < 3600.0)
        snprintf(buf, buf_size, "%.1f min", seconds / 60.0);
    else if (abs_s < 86400.0)
        snprintf(buf, buf_size, "%.1f hr", seconds / 3600.0);
    else if (abs_s < 3.156e7)
        snprintf(buf, buf_size, "%.1f day", seconds / 86400.0);
    else if (abs_s < 3.156e10)
        snprintf(buf, buf_size, "%.2f yr", seconds / 3.156e7);
    else if (abs_s < 3.156e13)
        snprintf(buf, buf_size, "%.2f kyr", seconds / 3.156e10);
    else if (abs_s < 3.156e16)
        snprintf(buf, buf_size, "%.2f Myr", seconds / 3.156e13);
    else if (abs_s < 3.156e19)
        snprintf(buf, buf_size, "%.2f Gyr", seconds / 3.156e16);
    else
        snprintf(buf, buf_size, "%.2f Tyr", seconds / 3.156e19);
    return buf;
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
    sun.seed = 42;
    sun.type = classify_star_spectral(sun.temperature, sun.mass);
    sun.name = generate_body_name(sun.seed, sun.type);
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
        p.seed = (uint32_t)(i * 7919 + 12345);
        p.name = generate_body_name(p.seed, p.type);
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
        moon.seed = 99999;
        moon.name = generate_body_name(moon.seed, moon.type);
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
        a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a);
    }

    state.trails.resize(state.bodies.size());

    // Initialize cached planet properties for all bodies
    for (auto& b : state.bodies) refresh_planet_props(b);
}

void CosmosApp::init(GLFWwindow* window) {
    vk.init(window);
    renderer.init(vk, window);
    raytracer_.init(vk, renderer.render_pass());

    state.clear();
    cfg.body_count = 0;

    camera.target = {0, 0, 0};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    camera.azimuth = 0.0f;
}

// ── Body picking (screen-space hit test) ─────────────────────────────────────

int CosmosApp::pick_body(float mx, float my, float W, float H) const {
    float aspect = W / H;
    glm::mat4 vp = camera.proj_matrix(aspect) * camera.view_matrix();
    float fov_rad = glm::radians(camera.fov);

    int best = -1;
    float best_dist = 30.0f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        glm::vec4 clip = vp * glm::vec4(b.pos, 1.0f);
        if (clip.w <= 0.0f) continue;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        float sx = (ndc.x * 0.5f + 0.5f) * W;
        float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * H;

        float dx = sx - mx;
        float dy = sy - my;
        float d = std::sqrt(dx * dx + dy * dy);
        float sr = (b.radius / clip.w) * (H / (2.0f * std::tan(fov_rad * 0.5f)));
        float pick_r = std::max(sr, 12.0f);
        if (d < pick_r && d < best_dist) {
            best_dist = d;
            best = (int)i;
        }
    }
    return best;
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
    inspector_visible_ = false;
    sim_time_ = 0.0f;
    cfg.sim_time_accumulated = 0.0;
    camera = OrbitCamera{};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    paused = false;
}

void CosmosApp::spawn_at(glm::vec3 pos) {
    CelestialBody nb;
    nb.pos = pos;
    nb.vel = glm::vec3(0.0f);
    nb.mass = spawn_mass;
    nb.radius = std::max(3.0f, std::cbrt(spawn_mass) * 5.0f);
    nb.type = (uint32_t)spawn_type;
    nb.seed = (uint32_t)(std::hash<float>{}(pos.x) ^ std::hash<float>{}(pos.y) ^
              std::hash<float>{}(pos.z) ^ (uint32_t)state.bodies.size());
    std::mt19937 rng(nb.seed ^ (uint32_t)(sim_time_ * 1000.0f));

    if (is_star_type((uint32_t)spawn_type)) {
        nb.temperature = 5778.0f;
        nb.radius = std::max(15.0f, std::cbrt(spawn_mass) * 6.0f);
        if (spawn_type == CTYPE_STAR)
            nb.type = classify_star_spectral(nb.temperature, nb.mass);
    } else if (is_black_hole_type((uint32_t)spawn_type)) {
        nb.temperature = 0.0f;
        nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
        if (spawn_type == CTYPE_BLACK_HOLE)
            nb.type = classify_black_hole(nb.mass);
    } else {
        nb.temperature = 300.0f;
        if (spawn_type == CTYPE_PLANET)
            randomize_planet_properties(nb, state, rng);

    }

    if (spawn_in_orbit_ && !state.bodies.empty()) {
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
                glm::vec3 dir = glm::normalize(diff);
                glm::vec3 perp(-dir.z, 0.0f, dir.x);
                nb.vel = state.bodies[nearest].vel + perp * v;
            }
        }
    }

    nb.name = generate_body_name(nb.seed, nb.type);
    refresh_planet_props(nb);
    state.bodies.push_back(nb);
    state.trails.emplace_back();
}

// ── Tick ─────────────────────────────────────────────────────────────────────

void CosmosApp::tick(GLFWwindow* window, float dt) {
    if (!renderer.begin_frame(vk, window))
        return;

    // WASD camera panning (breaks focus tracking)
    if (!show_splash && !show_pause_menu && !ImGui::GetIO().WantTextInput) {
        float move_speed = camera.distance * 0.5f * dt;
        glm::vec3 fwd = camera.forward_direction();
        glm::vec3 right = camera.right_direction();
        fwd.y = 0; fwd = glm::normalize(fwd);
        right.y = 0; right = glm::normalize(right);

        bool moved = false;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { camera.target += fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { camera.target -= fwd * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { camera.target += right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { camera.target -= right * move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) { camera.target.y += move_speed; moved = true; }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) { camera.target.y -= move_speed; moved = true; }
        if (moved) camera.release_focus();
    }

    // Track focused body (update position each frame so camera follows)
    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        camera.track_body(state.bodies[camera.focus_body].pos);
    } else if (camera.focus_active && camera.focus_body >= 0) {
        // Body was removed
        camera.release_focus();
    }

    // Smooth camera animation (zoom + focus lerp)
    camera.update(dt);

    if (!paused && !show_splash) {
        step_physics(dt);
        sim_time_ += dt;
    }

    // GPU raytraced scene (draws within the active render pass)
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                io.DisplaySize.x, io.DisplaySize.y, sim_time_);

    // DrawList overlays (trails, selection, focus indicator)
    if (!show_splash && !show_pause_menu)
        render_overlay();

    // ImGui UI panels
    render_ui();

    renderer.end_frame(vk);
}

// ── Physics (CPU N-body, 3D) ────────────────────────────────────────────────

void CosmosApp::step_physics(float dt) {
    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);
    float scaled_dt = dt * cfg.dt_scale;
    cfg.sim_time_accumulated += (double)scaled_dt;
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    if (n == 0) return;

    // Compute gravitational acceleration (Newtonian + GR corrections)
    std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
    float c2 = cfg.speed_of_light * cfg.speed_of_light;

    auto accumulate_gravity_pair = [&](size_t i, size_t j, std::vector<glm::vec3>& out_accel) {
            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist2 = glm::dot(diff, diff) + cfg.softening * cfg.softening;
            float dist  = std::sqrt(dist2);
            float force = cfg.G * bodies[i].mass * bodies[j].mass / dist2;
            glm::vec3 dir = diff / dist;

            // Newtonian gravity
            glm::vec3 acc_i = dir * (force / bodies[i].mass);
            glm::vec3 acc_j = -dir * (force / bodies[j].mass);

            // ── General Relativity corrections ──
            if (cfg.gr_enabled && c2 > 0.0f) {
                // 1. Post-Newtonian perihelion precession (1PN correction)
                //    a_GR = (GM/r²c²) * [ (4GM/r - v²)r̂ + 4(v·r̂)v ]
                //    This produces the correct perihelion advance rate.
                {
                    float GM_j = cfg.G * bodies[j].mass;
                    float GM_i = cfg.G * bodies[i].mass;

                    // Correction on body i from body j
                    glm::vec3 rvel_i = bodies[i].vel;
                    float vi2 = glm::dot(rvel_i, rvel_i);
                    float vr_i = glm::dot(rvel_i, dir);
                    float pn_scale_i = GM_j / (dist * c2) * cfg.gr_precession_scale;
                    glm::vec3 gr_i = pn_scale_i * ((4.0f * GM_j / dist - vi2) * dir + 4.0f * vr_i * rvel_i);
                    acc_i += gr_i;

                    // Correction on body j from body i
                    glm::vec3 rvel_j = bodies[j].vel;
                    float vj2 = glm::dot(rvel_j, rvel_j);
                    float vr_j = glm::dot(rvel_j, -dir);
                    float pn_scale_j = GM_i / (dist * c2) * cfg.gr_precession_scale;
                    glm::vec3 gr_j = pn_scale_j * ((4.0f * GM_i / dist - vj2) * (-dir) + 4.0f * vr_j * rvel_j);
                    acc_j += gr_j;
                }

                // 2. Gravitational time dilation effect on effective acceleration
                //    Objects deeper in a gravitational well experience time more slowly.
                //    Effective boost: a *= 1 + Φ/c² where Φ = -GM/r
                if (cfg.gr_time_dilation > 0.0f) {
                    float phi_j = -cfg.G * bodies[j].mass / dist;  // potential at i from j
                    float phi_i = -cfg.G * bodies[i].mass / dist;  // potential at j from i
                    float td_i = 1.0f + cfg.gr_time_dilation * phi_j / c2;
                    float td_j = 1.0f + cfg.gr_time_dilation * phi_i / c2;
                    acc_i *= td_i;
                    acc_j *= td_j;
                }

                // 3. Frame dragging (Lense-Thirring) for spinning bodies
                //    a_LT ∝ (GJ/(c²r³)) × [(v×Ĵ) - 3(r̂·Ĵ)(v×r̂)]
                //    Angular velocity serves as spin proxy
                if (cfg.gr_frame_dragging > 0.0f) {
                    for (int pair = 0; pair < 2; pair++) {
                        size_t src = (pair == 0) ? j : i;
                        size_t dst = (pair == 0) ? i : j;
                        if (std::abs(bodies[src].angular_vel) < 1e-6f) continue;

                        // Angular momentum J ∝ mass * radius² * angular_vel
                        float J = bodies[src].mass * bodies[src].radius * bodies[src].radius *
                                  bodies[src].angular_vel;
                        glm::vec3 J_hat(0, 1, 0); // spin axis assumed Y-up
                        float coeff = cfg.gr_frame_dragging * cfg.G * J / (c2 * dist * dist * dist);

                        glm::vec3 r_hat = (pair == 0) ? dir : -dir;
                        glm::vec3 v_dst = bodies[dst].vel;
                        glm::vec3 vxJ = glm::cross(v_dst, J_hat);
                        float rdotJ = glm::dot(r_hat, J_hat);
                        glm::vec3 vxr = glm::cross(v_dst, r_hat);
                        glm::vec3 a_lt = coeff * (vxJ - 3.0f * rdotJ * vxr);

                        if (pair == 0) acc_i += a_lt;
                        else           acc_j += a_lt;
                    }
                }
            }

            out_accel[i] += acc_i;
            out_accel[j] += acc_j;
    };

    constexpr size_t kParallelGravityThreshold = 256;
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
                                ? static_cast<size_t>(std::thread::hardware_concurrency())
                                : 1;
    const bool use_parallel_gravity =
        cfg.parallel_gravity && n >= kParallelGravityThreshold && hw_threads > 1;

    if (use_parallel_gravity) {
        const size_t worker_count = std::min(hw_threads, n);
        std::vector<std::vector<glm::vec3>> local_accel(
            worker_count, std::vector<glm::vec3>(n, glm::vec3(0.0f)));
        std::vector<std::thread> workers;
        workers.reserve(worker_count);

        for (size_t t = 0; t < worker_count; ++t) {
            const size_t i_begin = (n * t) / worker_count;
            const size_t i_end = (n * (t + 1)) / worker_count;

            workers.emplace_back([&, t, i_begin, i_end]() {
                auto& thread_accel = local_accel[t];
                for (size_t i = i_begin; i < i_end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    for (size_t j = i + 1; j < n; ++j) {
                        if (bodies[j].marked_for_removal) continue;
                        accumulate_gravity_pair(i, j, thread_accel);
                    }
                }
            });
        }

        for (auto& worker : workers)
            worker.join();

        for (size_t t = 0; t < worker_count; ++t) {
            const auto& thread_accel = local_accel[t];
            for (size_t i = 0; i < n; ++i)
                accel[i] += thread_accel[i];
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (bodies[j].marked_for_removal) continue;
                accumulate_gravity_pair(i, j, accel);
            }
        }
    }

    // Integrate (symplectic Euler)
    for (size_t i = 0; i < n; i++) {
        if (bodies[i].marked_for_removal) continue;
        bodies[i].vel += accel[i] * scaled_dt;
        bodies[i].vel *= cfg.damping;
        bodies[i].pos += bodies[i].vel * scaled_dt;
        bodies[i].age += scaled_dt;
    }

    // Physics subsystems
    if (cfg.collisions)         process_collisions(scaled_dt);
    if (cfg.roche_limit)        process_roche_limit(scaled_dt);
    if (cfg.temperature_system) process_temperature(scaled_dt);
    if (cfg.evaporation)        process_evaporation(scaled_dt);
    if (cfg.stellar_evolution)  process_stellar_evolution(scaled_dt);
    cleanup_bodies();

    // Refresh cached planet properties (only recomputes on temperature band changes)
    for (auto& b : state.bodies)
        refresh_planet_props(b);

    // Update trails
    n = bodies.size();
    while (state.trails.size() < n)
        state.trails.emplace_back();
    for (size_t i = 0; i < n; i++) {
        state.trails[i].push_back(bodies[i].pos);
        while (state.trails[i].size() > cfg.trail_length)
            state.trails[i].pop_front();
    }
}

// ── Collision Processing ────────────────────────────────────────────────────

void CosmosApp::process_collisions(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    for (size_t i = 0; i < n; i++) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (bodies[j].marked_for_removal) continue;

            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;

            if (dist < touch && dist > 0.01f) {
                glm::vec3 dir = diff / dist;
                float overlap = touch - dist;
                float total_mass = bodies[i].mass + bodies[j].mass;

                glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
                float rel_speed = glm::length(rel_vel);

                // Collision heating
                if (cfg.temperature_system) {
                    float ke = 0.5f * (bodies[i].mass * bodies[j].mass / total_mass) * rel_speed * rel_speed;
                    float heat = ke * cfg.collision_heating;
                    bodies[i].internal_energy += heat * 0.5f;
                    bodies[j].internal_energy += heat * 0.5f;
                    bodies[i].temperature += heat * 100.0f / std::max(bodies[i].mass, 0.01f);
                    bodies[j].temperature += heat * 100.0f / std::max(bodies[j].mass, 0.01f);
                }

                // Determine merge vs fragment vs bounce
                if (cfg.collision_merging && rel_speed < cfg.merge_speed_threshold) {
                    // Merge: larger absorbs smaller
                    size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                    size_t small = (big == i) ? j : i;

                    // Conserve momentum
                    glm::vec3 new_vel = (bodies[big].mass * bodies[big].vel +
                                         bodies[small].mass * bodies[small].vel) / total_mass;
                    bodies[big].vel = new_vel;
                    bodies[big].mass += bodies[small].mass;
                    // Conserve volume: r = cbrt(r1^3 + r2^3)
                    float r1 = bodies[big].radius, r2 = bodies[small].radius;
                    bodies[big].radius = std::cbrt(r1 * r1 * r1 + r2 * r2 * r2);
                    // Weighted temperature
                    bodies[big].temperature = (bodies[big].temperature * (total_mass - bodies[small].mass) +
                                               bodies[small].temperature * bodies[small].mass) / total_mass;
                    bodies[big].fuel = std::max(bodies[big].fuel, bodies[small].fuel);
                    bodies[small].marked_for_removal = true;
                }
                else if (cfg.collision_fragmentation && rel_speed > cfg.fragment_speed_threshold) {
                    size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                    size_t small = (big == i) ? j : i;

                    // Only fragment if body is large enough and hasn't been fragmented too many times
                    bool can_fragment = bodies[small].mass >= cfg.min_fragment_mass
                                     && (int)bodies[small].frag_generation < cfg.max_frag_generation;

                    if (can_fragment) {
                        spawn_fragments(bodies[small].pos, bodies[small].vel,
                                        bodies[small].mass, cfg.fragment_count,
                                        bodies[small].frag_generation);
                        bodies[small].marked_for_removal = true;
                    }

                    // Elastic bounce on the big body (always, even if no fragmentation)
                    float vel_along = glm::dot(rel_vel, dir);
                    if (vel_along < 0) {
                        float impulse = -(1.0f + 0.5f) * vel_along / total_mass;
                        bodies[big].vel += ((big == j) ? 1.0f : -1.0f) * dir * impulse * bodies[small].mass;
                    }
                }
                else {
                    // Normal elastic bounce
                    bodies[i].pos -= dir * overlap * (bodies[j].mass / total_mass);
                    bodies[j].pos += dir * overlap * (bodies[i].mass / total_mass);

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
}

// ── Roche Limit ─────────────────────────────────────────────────────────────

void CosmosApp::process_roche_limit(float /*dt*/) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    for (size_t i = 0; i < n; i++) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = 0; j < n; j++) {
            if (i == j || bodies[j].marked_for_removal) continue;
            if (bodies[i].mass <= bodies[j].mass * 10.0f) continue; // i must be much larger

            float dist = glm::length(bodies[j].pos - bodies[i].pos);
            float mass_ratio = bodies[i].mass / std::max(bodies[j].mass, 0.001f);
            float roche_dist = bodies[i].radius * 2.5f * std::cbrt(mass_ratio);

            if (dist < roche_dist && dist > bodies[i].radius) {
                // Only tidally disrupt if body is large enough and not at generation limit
                if (bodies[j].mass >= cfg.min_fragment_mass
                    && (int)bodies[j].frag_generation < cfg.max_frag_generation) {
                    spawn_fragments(bodies[j].pos, bodies[j].vel,
                                    bodies[j].mass, cfg.fragment_count,
                                    bodies[j].frag_generation);
                    bodies[j].marked_for_removal = true;
                }
            }
        }
    }
}

// ── Temperature ─────────────────────────────────────────────────────────────

void CosmosApp::process_temperature(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;

        // Stars maintain temperature based on mass and fuel
        if (is_star_type(b.type) && b.fuel > 0.05f) {
            // Mass-luminosity relation: L ∝ M^3.5
            b.luminosity = std::pow(b.mass, 3.5f) * 0.1f;
            // Stars don't cool while burning
            continue;
        }

        // Radiative cooling (exponential decay toward background 2.7K)
        float background = 2.7f;
        b.temperature -= cfg.radiative_cooling * (b.temperature - background) * dt;
        if (b.temperature < background) b.temperature = background;

        // Tidal heating from nearby massive bodies
        for (const auto& other : bodies) {
            if (&other == &b || other.marked_for_removal) continue;
            if (other.mass < b.mass * 5.0f) continue;

            float dist = glm::length(other.pos - b.pos);
            if (dist < 1.0f) continue;
            // Tidal heating ∝ M^2 * R^5 / d^6 (simplified)
            float tidal = other.mass * other.mass * b.radius / (dist * dist * dist * dist);
            b.temperature += tidal * 0.1f * dt;
        }

        // Drain internal energy into temperature
        if (b.internal_energy > 0.0f) {
            float transfer = std::min(b.internal_energy, 10.0f * dt);
            b.temperature += transfer / std::max(b.mass, 0.01f);
            b.internal_energy -= transfer;
        }
    }
}

// ── Evaporation ─────────────────────────────────────────────────────────────

void CosmosApp::process_evaporation(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;

        // Hot small bodies (comets, asteroids) lose mass
        if (b.temperature > 500.0f && b.mass < 1.0f) {
            float loss = cfg.evaporation_rate * (b.temperature / 1000.0f) * dt;
            b.mass -= loss;
            b.radius *= 0.999f; // shrink slightly

            if (b.mass < 0.001f) {
                b.marked_for_removal = true;
            }
        }
    }
}

// ── Stellar Evolution ───────────────────────────────────────────────────────

void CosmosApp::process_stellar_evolution(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (!is_star_type(b.type)) continue;

        // Burn fuel: more massive stars burn faster
        float burn_rate = dt / (cfg.stellar_timescale * std::max(b.mass * b.mass, 1.0f));
        b.fuel -= burn_rate;
        if (b.fuel < 0.0f) b.fuel = 0.0f;

        // Reclassify spectral type as temperature changes
        b.type = classify_star_spectral(b.temperature, b.mass);

        // Main sequence → red giant
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE && b.fuel < 0.3f) {
            b.stellar_stage = SSTAGE_RED_GIANT;
            b.radius *= 5.0f;
            b.temperature *= 0.5f;
            b.luminosity *= 100.0f;
        }

        // Red giant → end state
        if (b.stellar_stage == SSTAGE_RED_GIANT && b.fuel < 0.05f) {
            if (b.mass > 8.0f) {
                // Supernova → neutron star
                b.stellar_stage = SSTAGE_NEUTRON_STAR;

                // Spawn debris
                float debris_mass = b.mass * 0.7f;
                spawn_fragments(b.pos, b.vel, debris_mass, cfg.fragment_count);

                // Remnant neutron star
                b.mass *= 0.3f;
                b.radius = 3.0f;
                b.temperature = 100000.0f;
                b.luminosity = 0.01f;

                // Give debris a velocity kick
                size_t n = state.bodies.size();
                size_t frag_start = (n > (size_t)cfg.fragment_count) ? (n - (size_t)cfg.fragment_count) : 0;
                for (size_t k = frag_start; k < n; k++) {
                    glm::vec3 kick_dir = glm::normalize(state.bodies[k].pos - b.pos);
                    state.bodies[k].vel += kick_dir * 30.0f;
                    state.bodies[k].temperature = 5000.0f;
                }
            } else {
                // White dwarf
                b.stellar_stage = SSTAGE_WHITE_DWARF;
                b.radius = std::max(2.0f, b.radius * 0.02f);
                b.temperature = 20000.0f;
                b.luminosity = 0.001f;
            }
        }
    }
}

// ── Cleanup ─────────────────────────────────────────────────────────────────

void CosmosApp::cleanup_bodies() {
    auto& bodies = state.bodies;
    bool any_removed = false;
    for (const auto& b : bodies) {
        if (b.marked_for_removal) { any_removed = true; break; }
    }
    if (!any_removed) return;

    // Build index mapping (old → new)
    std::vector<int> remap(bodies.size(), -1);
    int new_idx = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal)
            remap[i] = new_idx++;
    }

    // Fix selected body
    if (selected_body >= 0 && selected_body < (int)bodies.size()) {
        selected_body = remap[selected_body];
    }

    // Fix parent indices
    for (auto& b : bodies) {
        if (b.parent >= 0 && b.parent < (int)remap.size())
            b.parent = remap[b.parent];
    }

    // Remove bodies and trails
    size_t write = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].marked_for_removal) {
            if (write != i) {
                bodies[write] = bodies[i];
                if (i < state.trails.size())
                    state.trails[write] = std::move(state.trails[i]);
            }
            write++;
        }
    }
    bodies.resize(write);
    state.trails.resize(write);
}

// ── Fragment Spawning ───────────────────────────────────────────────────────

void CosmosApp::spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count, uint32_t parent_generation) {
    if (count < 1) return;

    // Generate random proportions using stick-breaking:
    // draw (count-1) random break points in [0,1], sort, then differences give proportions.
    // This produces naturally varied sizes that always sum to total_mass.
    float breaks[13]; // max count=12 → 11 breaks + 0.0 + 1.0
    breaks[0] = 0.0f;
    for (int i = 1; i < count; i++)
        breaks[i] = (float)rand() / RAND_MAX;
    breaks[count] = 1.0f;
    // Simple insertion sort (count is small, max 12)
    for (int i = 1; i < count; i++) {
        float key = breaks[i];
        int j = i - 1;
        while (j >= 0 && breaks[j] > key) { breaks[j + 1] = breaks[j]; j--; }
        breaks[j + 1] = key;
    }

    float mass_remaining = total_mass;
    for (int i = 0; i < count; i++) {
        float proportion = breaks[i + 1] - breaks[i];
        float frag_mass = total_mass * proportion;
        // Ensure no fragment has zero mass
        if (frag_mass < 0.001f) frag_mass = 0.001f;
        mass_remaining -= frag_mass;

        // Radius from mass (volume-preserving: r proportional to cbrt(m))
        float frag_radius = std::max(1.5f, std::cbrt(frag_mass) * 3.0f);

        CelestialBody frag;
        float theta = (float)rand() / RAND_MAX * 6.2832f;
        float phi = (float)rand() / RAND_MAX * 3.1416f - 1.5708f;
        glm::vec3 dir(cosf(phi) * cosf(theta), sinf(phi), cosf(phi) * sinf(theta));

        frag.pos = pos + dir * (frag_radius * 2.0f + 2.0f);
        frag.vel = vel + dir * (5.0f + (float)rand() / RAND_MAX * 10.0f);
        frag.mass = frag_mass;
        frag.radius = frag_radius;
        frag.temperature = 300.0f;
        frag.type = CTYPE_ASTEROID;
        frag.fuel = 0.0f;
        frag.seed = (uint32_t)rand();
        frag.frag_generation = parent_generation + 1;
        frag.name = generate_body_name(frag.seed, frag.type);
        refresh_planet_props(frag);

        state.bodies.push_back(frag);
        state.trails.emplace_back();
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

    // Selection highlight — animated ring
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        const auto& b = state.bodies[selected_body];
        auto p = project(b.pos, vp, W, H);
        if (p.visible) {
            float sr = screen_radius(b.radius, p.depth, fov_rad, H);
            sr = std::max(sr, 6.0f);

            // Animated selection ring
            float pulse = 0.85f + 0.15f * std::sin(sim_time_ * 4.0f);
            float ring_r = (sr + 6.0f) * pulse;

            // Outer glow
            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r + 2.0f,
                IM_COL32(255, 200, 60, 40), 48, 3.0f);
            // Main ring
            fg->AddCircle(ImVec2(p.sx, p.sy), ring_r,
                IM_COL32(255, 220, 100, 200), 48, 2.0f);

            // Corner brackets (like a targeting reticle)
            float bk = ring_r + 8.0f;
            float bl = 8.0f;
            ImU32 bk_col = IM_COL32(255, 255, 255, 120);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk + bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy - bk), ImVec2(p.sx - bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk - bl, p.sy - bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy - bk), ImVec2(p.sx + bk, p.sy - bk + bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk + bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx - bk, p.sy + bk), ImVec2(p.sx - bk, p.sy + bk - bl), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk - bl, p.sy + bk), bk_col, 1.5f);
            fg->AddLine(ImVec2(p.sx + bk, p.sy + bk), ImVec2(p.sx + bk, p.sy + bk - bl), bk_col, 1.5f);

            // Body name label above
            const char* name = b.name.empty() ? CTYPE_NAMES[std::min(b.type, (uint32_t)CTYPE_COUNT - 1)] : b.name.c_str();
            ImVec2 name_size = ImGui::CalcTextSize(name);
            float label_x = p.sx - name_size.x * 0.5f;
            float label_y = p.sy - ring_r - 20.0f;
            fg->AddRectFilled(ImVec2(label_x - 4, label_y - 2),
                              ImVec2(label_x + name_size.x + 4, label_y + name_size.y + 2),
                              IM_COL32(10, 10, 20, 180), 3.0f);
            fg->AddText(ImVec2(label_x, label_y), IM_COL32(255, 220, 100, 240), name);
        }
    }

    // Focus indicator (when camera is tracking a body)
    if (camera.focus_active && camera.focus_body >= 0 &&
        camera.focus_body < (int)state.bodies.size()) {
        // Small "TRACKING" label in top-right
        const char* track_label = "TRACKING";
        ImVec2 tl_size = ImGui::CalcTextSize(track_label);
        float tx = W - tl_size.x - 16.0f;
        float ty = 44.0f;
        fg->AddRectFilled(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                          IM_COL32(255, 180, 40, 30), 3.0f);
        fg->AddRect(ImVec2(tx - 6, ty - 2), ImVec2(tx + tl_size.x + 6, ty + tl_size.y + 2),
                    IM_COL32(255, 180, 40, 120), 3.0f, 0, 1.0f);
        float alpha = 160.0f + 60.0f * std::sin(sim_time_ * 3.0f);
        fg->AddText(ImVec2(tx, ty), IM_COL32(255, 200, 80, (int)alpha), track_label);
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

        // Save
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 2));
        if (ImGui::Button("Save Simulation", ImVec2(btn_w, btn_h))) {
            show_save_dialog_ = true;
            show_pause_menu = false;
        }

        // Load
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 3));
        if (ImGui::Button("Load Simulation", ImVec2(btn_w, btn_h))) {
            show_load_dialog_ = true;
            show_pause_menu = false;
        }

        // Empty Simulation
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 4));
        if (ImGui::Button("Empty Simulation", ImVec2(btn_w, btn_h))) {
            state.clear();
            cfg.body_count = 0;
            selected_body = -1;
            sim_time_ = 0.0f;
            show_pause_menu = false;
            paused = false;
        }

        // Return to Launcher
        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 5));
        if (ImGui::Button("Return to Launcher", ImVec2(btn_w, btn_h))) {
            request_launcher = true;
            request_quit = true;
        }

        // Quit — red tinted
        ImGui::PopStyleColor(3);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.08f, 0.08f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.50f, 0.12f, 0.12f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.60f, 0.15f, 0.15f, 1.00f));

        ImGui::SetCursorPos(ImVec2(btn_x, btn_y + btn_spacing * 6));
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

    // Type selection helper lambda
    auto type_button = [&](int t, float btn_w, float default_mass) {
        ImU32 col = CTYPE_COLORS[t];
        float r = (float)((col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        float g = (float)((col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        float b_c = (float)((col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        bool sel = (spawn_type == t);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r*0.5f, g*0.5f, b_c*0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r*0.6f, g*0.6f, b_c*0.6f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r*0.7f, g*0.7f, b_c*0.7f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.9f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(r*0.25f, g*0.25f, b_c*0.25f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r*0.4f, g*0.4f, b_c*0.4f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(r*0.5f, g*0.5f, b_c*0.5f, 1.0f));
        }
        if (ImGui::Button(CTYPE_NAMES[t], ImVec2(btn_w, 24))) {
            spawn_type = t;
            spawn_mass = default_mass;
        }
        if (sel) { ImGui::PopStyleColor(4); ImGui::PopStyleVar(); }
        else     { ImGui::PopStyleColor(3); }
    };

    if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen)) {
        float bw = 76.0f;
        type_button(CTYPE_PLANET, bw, 1.0f);    ImGui::SameLine();
        type_button(CTYPE_MOON, bw, 0.05f);     ImGui::SameLine();
        type_button(CTYPE_ASTEROID, bw, 0.02f);
        type_button(CTYPE_COMET, bw, 0.01f);    ImGui::SameLine();
        type_button(CTYPE_NEBULA, bw, 0.001f);
    }

    if (ImGui::CollapsingHeader("Stars")) {
        float bw = 76.0f;
        type_button(CTYPE_STAR, bw, 1.0f);
        ImGui::SameLine(); type_button(CTYPE_STAR_O, bw, 30.0f);
        ImGui::SameLine(); type_button(CTYPE_STAR_B, bw, 5.0f);
        type_button(CTYPE_STAR_A, bw, 1.8f);
        ImGui::SameLine(); type_button(CTYPE_STAR_F, bw, 1.2f);
        ImGui::SameLine(); type_button(CTYPE_STAR_G, bw, 1.0f);
        type_button(CTYPE_STAR_K, bw, 0.6f);
        ImGui::SameLine(); type_button(CTYPE_STAR_M, bw, 0.2f);
        ImGui::SameLine(); type_button(CTYPE_STAR_L, bw, 0.06f);
        type_button(CTYPE_STAR_T, bw, 0.04f);
        ImGui::SameLine(); type_button(CTYPE_STAR_Y, bw, 0.02f);
        ImGui::SameLine(); type_button(CTYPE_STAR_WR, bw, 20.0f);
    }

    if (ImGui::CollapsingHeader("Black Holes")) {
        float bw = 120.0f;
        type_button(CTYPE_BLACK_HOLE, bw, 200.0f);
        ImGui::SameLine(); type_button(CTYPE_BH_STELLAR, bw, 10.0f);
        type_button(CTYPE_BH_INTERMEDIATE, bw, 1000.0f);
        ImGui::SameLine(); type_button(CTYPE_BH_SUPERMASSIVE, bw, 1000000.0f);
        type_button(CTYPE_BH_PRIMORDIAL, bw, 0.5f);
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

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "Middle-click in viewport to place");

        char label[64];
        snprintf(label, sizeof(label), "Spawn %s at Origin", CTYPE_NAMES[spawn_type % CTYPE_COUNT]);
        if (ImGui::Button(label, ImVec2(-1, 36))) {
            spawn_at(camera.target);
        }
        ImGui::PopStyleColor(3);
    }

    // Quick spawn presets
    if (ImGui::CollapsingHeader("Quick Presets")) {
        if (ImGui::Button("Add Solar System", ImVec2(-1, 0))) {
            glm::vec3 offset = camera.target;
            CelestialBody s;
            s.pos = offset; s.mass = 100.0f; s.radius = 30.0f;
            s.temperature = 5778.0f; s.type = classify_star_spectral(5778.0f, 100.0f);
            s.seed = 42;
            s.name = generate_body_name(s.seed, s.type);
            int star_idx = (int)state.bodies.size();
            state.bodies.push_back(s); state.trails.emplace_back();

            float radii[] = {80, 140, 210, 300};
            float masses[] = {0.3f, 0.8f, 0.5f, 1.5f};
            float temps[] = {600.0f, 300.0f, 180.0f, 90.0f};
            for (int i = 0; i < 4; i++) {
                CelestialBody p;
                float angle = (float)i * 1.57f;
                p.pos = offset + glm::vec3(cosf(angle) * radii[i], 0, sinf(angle) * radii[i]);
                float v = std::sqrt(cfg.G * s.mass / radii[i]);
                p.vel = s.vel + glm::vec3(-sinf(angle) * v, 0, cosf(angle) * v);
                p.mass = masses[i]; p.radius = 6 + masses[i] * 3;
                p.temperature = temps[i];
                p.type = CTYPE_PLANET; p.parent = star_idx;
                p.seed = (uint32_t)(i * 31337 + 54321);
                p.name = generate_body_name(p.seed, p.type);
                refresh_planet_props(p);
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
            s1.type = classify_star_spectral(8000.0f, 50.0f); s1.seed = 111;
            s1.name = generate_body_name(s1.seed, s1.type);
            state.bodies.push_back(s1); state.trails.emplace_back();

            CelestialBody s2;
            s2.pos = center - glm::vec3(sep * 0.5f, 0, 0);
            s2.vel = glm::vec3(0, 0, -v * 0.5f);
            s2.mass = 50.0f; s2.radius = 22.0f; s2.temperature = 3500.0f;
            s2.type = classify_star_spectral(3500.0f, 50.0f); s2.seed = 222;
            s2.name = generate_body_name(s2.seed, s2.type);
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
                if (is_star_type(b.type)) {
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
                a.seed = (uint32_t)(rng());
                a.name = generate_body_name(a.seed, a.type);
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

    // ── Global modern styling ────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.10f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.14f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.12f, 0.10f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.16f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.16f, 0.26f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.24f, 0.20f, 0.34f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.60f, 0.20f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.75f, 0.25f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.18f, 0.32f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.28f, 0.24f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.12f, 0.22f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.34f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.24f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.20f, 0.35f, 0.40f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.25f, 0.40f, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.88f, 0.94f, 1.0f));

    // ── Normal UI ────────────────────────────────────────────────────────────

    // Show inspector when a body is selected
    if (selected_body >= 0 && selected_body < (int)state.bodies.size()) {
        inspector_visible_ = true;
    }

    // Settings panel
    if (settings_visible_) {
    ImGui::SetNextWindowPos({10, 46}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 760}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Cosmos Settings", &settings_visible_);
    ImGui::SliderFloat("G",          &cfg.G,        0.1f, 10.0f);
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
    ImGui::Text("Collision Physics");
    ImGui::Checkbox("Merging",        &cfg.collision_merging);
    ImGui::Checkbox("Fragmentation",  &cfg.collision_fragmentation);
    ImGui::SliderFloat("Merge Speed",    &cfg.merge_speed_threshold,    1.0f, 20.0f);
    ImGui::SliderFloat("Fragment Speed", &cfg.fragment_speed_threshold, 10.0f, 50.0f);
    ImGui::SliderInt("Fragment Count",   &cfg.fragment_count,           1, 12);
    ImGui::SliderFloat("Min Frag Mass",  &cfg.min_fragment_mass,        0.01f, 1.0f, "%.2f");
    ImGui::SliderInt("Max Frag Depth",   &cfg.max_frag_generation,      0, 5);

    ImGui::Separator();
    ImGui::Text("Thermal");
    ImGui::Checkbox("Temperature", &cfg.temperature_system);
    ImGui::Checkbox("Evaporation", &cfg.evaporation);
    ImGui::Checkbox("Roche Limit", &cfg.roche_limit);
    if (cfg.temperature_system)
        ImGui::SliderFloat("Cooling", &cfg.radiative_cooling, 0.0f, 0.01f, "%.4f");

    ImGui::Separator();
    ImGui::Text("Stellar");
    ImGui::Checkbox("Stellar Evolution", &cfg.stellar_evolution);
    if (cfg.stellar_evolution)
        ImGui::SliderFloat("Star Timescale", &cfg.stellar_timescale, 10.0f, 500.0f);

    ImGui::Separator();
    ImGui::Text("Lighting");
    ImGui::Checkbox("Star Lighting",    &cfg.star_lighting);
    ImGui::Checkbox("Uniform Lighting", &cfg.uniform_lighting);
    if (cfg.star_lighting) {
        ImGui::Checkbox("Fast Star Lighting", &cfg.fast_star_lighting);
        ImGui::SliderFloat("Ambient", &cfg.ambient_strength, 0.0f, 0.5f);
    }

    ImGui::Separator();
    ImGui::Text("Time Control");
    {
        float exp_f = (float)cfg.time_exponent;
        if (ImGui::SliderFloat("Time Rate", &exp_f, -9.0f, 21.0f, "")) {
            cfg.time_exponent = (double)exp_f;
        }
        char rate_buf[64], time_buf[64];
        double rate = std::pow(10.0, cfg.time_exponent);
        format_sim_time(rate, rate_buf, sizeof(rate_buf));
        ImGui::SameLine();
        ImGui::Text("%s/s", rate_buf);

        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        ImGui::Text("Sim Time: %s", time_buf);

        struct Preset { const char* label; double exp; };
        static const Preset presets[] = {
            {"1 s/s",   0.0},   {"1 min/s", 1.778},
            {"1 hr/s",  3.556}, {"1 day/s", 4.937},
            {"1 yr/s",  7.499}, {"1 Myr/s", 13.499},
            {"1 Gyr/s", 16.499},
        };
        for (int i = 0; i < 7; i++) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::SmallButton(presets[i].label))
                cfg.time_exponent = presets[i].exp;
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "General Relativity");
    ImGui::Checkbox("GR Corrections", &cfg.gr_enabled);
    ImGui::Checkbox("Parallel Gravity", &cfg.parallel_gravity);
    if (cfg.gr_enabled) {
        ImGui::SliderFloat("Precession", &cfg.gr_precession_scale, 0.0f, 10.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Perihelion precession (1PN correction)\n1.0 = physical value");
        ImGui::SliderFloat("Time Dilation", &cfg.gr_time_dilation, 0.0f, 5.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gravitational time dilation effect");
        ImGui::SliderFloat("Frame Drag", &cfg.gr_frame_dragging, 0.0f, 5.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lense-Thirring frame dragging\nfrom spinning bodies");
        ImGui::SliderFloat("Speed of Light", &cfg.speed_of_light, 50.0f, 1000.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("c in simulation units\n(lower = stronger GR effects)");
    }

    ImGui::End();
    } // settings_visible_

    // Body Inspector
    draw_inspector();

    // Spawn menu
    draw_spawn_menu();

    // File dialog
    draw_file_dialog();

    // Save status toast
    if (save_status_timer_ > 0.0f) {
        save_status_timer_ -= io.DeltaTime;
        float alpha = std::min(save_status_timer_, 1.0f);
        ImVec2 text_size = ImGui::CalcTextSize(last_save_status_.c_str());
        float tx = io.DisplaySize.x * 0.5f - text_size.x * 0.5f - 12;
        float ty = io.DisplaySize.y * 0.5f - 20;
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(tx - 8, ty - 4), ImVec2(tx + text_size.x + 20, ty + text_size.y + 8),
            IM_COL32(20, 20, 30, (int)(200 * alpha)), 6.0f);
        ImGui::GetForegroundDrawList()->AddText(ImVec2(tx, ty),
            IM_COL32(255, 220, 80, (int)(255 * alpha)), last_save_status_.c_str());
    }

    // Ctrl+S / Ctrl+L hotkeys
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && !show_save_dialog_) {
        show_save_dialog_ = true;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L) && !show_load_dialog_) {
        show_load_dialog_ = true;
    }

    // Body list
    if (body_list_visible_) {
    ImGui::SetNextWindowPos({io.DisplaySize.x - 280.0f, 320}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({270, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Bodies", &body_list_visible_);
    for (size_t i = 0; i < state.count(); i++) {
        const auto& b = state.bodies[i];
        const char* tn = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "?";
        bool is_sel = ((int)i == selected_body);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0.3f, 1));

        char buf[128];
        const char* display_name = b.name.empty() ? tn : b.name.c_str();
        snprintf(buf, sizeof(buf), "%zu. %s (%.1f M)", i, display_name, b.mass);
        if (ImGui::Selectable(buf, is_sel))
            selected_body = (int)i;

        if (is_sel) ImGui::PopStyleColor();
    }
    ImGui::End();
    } // body_list_visible_

    // Bottom bar
    draw_bottom_bar();

    ImGui::PopStyleColor(18);
    ImGui::PopStyleVar(6);
}

// ── Inspector panel ─────────────────────────────────────────────────────────

void CosmosApp::draw_inspector() {
    if (!inspector_visible_) return;
    if (selected_body < 0 || selected_body >= (int)state.bodies.size()) {
        inspector_visible_ = false;
        return;
    }

    auto& b = state.bodies[selected_body];
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 320.0f, 46.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 200), ImVec2(400, 800));

    if (!ImGui::Begin("Inspector", &inspector_visible_)) {
        ImGui::End();
        return;
    }

    // ── Header: name + type ──
    const char* type_name = (b.type < CTYPE_COUNT) ? CTYPE_NAMES[b.type] : "Unknown";
    const char* display_name = b.name.empty() ? type_name : b.name.c_str();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::TextWrapped("%s", display_name);
    ImGui::PopStyleColor();

    if (!b.name.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1.0f), "(%s)", type_name);
    }

    // Focus / Track button
    ImGui::SameLine(ImGui::GetWindowWidth() - 72);
    bool is_tracked = camera.focus_active && camera.focus_body == selected_body;
    if (is_tracked) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.1f, 0.9f));
        if (ImGui::SmallButton("Untrack")) camera.release_focus();
        ImGui::PopStyleColor();
    } else {
        if (ImGui::SmallButton("Track")) {
            camera.focus_on(b.pos, selected_body);
            camera.target_distance = b.radius * 8.0f;
        }
    }

    ImGui::Separator();

    // ── Core properties ──
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Properties");

    ImGui::Columns(2, "##props", false);
    ImGui::SetColumnWidth(0, 110);

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mass");
    ImGui::NextColumn();
    constexpr double SOLAR_MASS_KG = 1.98847e30;
    constexpr double KG_TO_LBS = 2.20462262185;
    double mass_kg = (double)b.mass * SOLAR_MASS_KG;
    double mass_lbs = mass_kg * KG_TO_LBS;
    ImGui::Text("%.3e kg / %.3e lbs", mass_kg, mass_lbs);

    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Radius");
    ImGui::NextColumn();
    constexpr float KM_TO_MILES = 0.6213712f;
    float radius_km = b.radius;
    float radius_miles = radius_km * KM_TO_MILES;
    ImGui::Text("%.1f km / %.1f mi", radius_km, radius_miles);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Temperature");
    ImGui::NextColumn();
    float temp_c = b.temperature - 273.15f;
    float temp_f = temp_c * 9.0f / 5.0f + 32.0f;
    ImGui::Text("%.0f K (%.1f C / %.1f F)", b.temperature, temp_c, temp_f);

    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Speed");
    ImGui::NextColumn();
    constexpr float KMH_TO_MPH = 0.6213712f;
    float speed_kmh = glm::length(b.vel) * SIM_UNIT_TO_KM * 3600.0f;
    float speed_mph = speed_kmh * KMH_TO_MPH;
    ImGui::Text("%.1f km/h / %.1f mph", speed_kmh, speed_mph);
    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Position");
    ImGui::NextColumn();
    ImGui::Text("%.0f, %.0f, %.0f", b.pos.x, b.pos.y, b.pos.z);
    ImGui::NextColumn();

    if (std::abs(b.angular_vel) > 1e-6f) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Spin");
        ImGui::NextColumn();
        ImGui::Text("%.3f rad/s", b.angular_vel);
        ImGui::NextColumn();
    }

    char age_buf[64];
    format_sim_time((double)b.age, age_buf, sizeof(age_buf));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Age");
    ImGui::NextColumn();
    ImGui::Text("%s", age_buf);
    ImGui::NextColumn();

    ImGui::Columns(1);

    // ── Orbital info ──
    if (b.parent >= 0 && b.parent < (int)state.bodies.size()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Orbit");

        const auto& par = state.bodies[b.parent];
        const char* par_name = par.name.empty()
            ? CTYPE_NAMES[std::min(par.type, (uint32_t)CTYPE_COUNT - 1)]
            : par.name.c_str();

        ImGui::Columns(2, "##orbit", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Parent");
        ImGui::NextColumn();
        if (ImGui::SmallButton(par_name)) {
            selected_body = b.parent;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to select parent");
        ImGui::NextColumn();

        float orb_dist = glm::length(b.pos - par.pos);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Distance");
        ImGui::NextColumn();
        ImGui::Text("%.1f", orb_dist);
        ImGui::NextColumn();

        if (orb_dist > 0.1f) {
            float orb_v = std::sqrt(cfg.G * par.mass / orb_dist);
            float period = 2.0f * 3.14159f * orb_dist / std::max(orb_v, 0.01f);
            char period_buf[64];
            format_sim_time((double)period, period_buf, sizeof(period_buf));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Period");
            ImGui::NextColumn();
            ImGui::Text("%s", period_buf);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Star info ──
    if (is_star_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "Stellar");

        static const char* STAGE_NAMES[] = {
            "Main Sequence", "Subgiant", "Red Giant", "Horizontal Branch",
            "AGB", "Supergiant", "Hypergiant", "White Dwarf", "Neutron Star"
        };

        ImGui::Columns(2, "##star", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Stage");
        ImGui::NextColumn();
        const char* stage = (b.stellar_stage < SSTAGE_COUNT) ? STAGE_NAMES[b.stellar_stage] : "?";
        ImGui::Text("%s", stage);
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Fuel");
        ImGui::NextColumn();
        ImGui::ProgressBar(b.fuel, ImVec2(-1, 14));
        ImGui::NextColumn();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Luminosity");
        ImGui::NextColumn();
        ImGui::Text("%.2f L", b.luminosity);
        ImGui::NextColumn();

        ImGui::Columns(1);
    }

    // ── Black hole info ──
    if (is_black_hole_type(b.type)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "Black Hole");

        float rs = 2.0f * cfg.G * b.mass / (cfg.speed_of_light * cfg.speed_of_light);

        ImGui::Columns(2, "##bh", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Schwarzschild r");
        ImGui::NextColumn();
        ImGui::Text("%.4f", rs);
        ImGui::NextColumn();

        ImGui::Columns(1);
    }

    // ── Planet / Moon properties ──
    if ((b.type == CTYPE_PLANET || b.type == CTYPE_MOON) && b.props_valid) {
        const auto& pp = b.cached_props;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Surface & Atmosphere");

        static const char* SURF_NAMES[] = {"Rocky", "Liquid", "Frozen", "Gas Giant", "Mixed"};
        static const char* OCEAN_NAMES[] = {"None", "Water", "Methane", "Ammonia", "Lava"};
        static const char* WEATHER_NAMES[] = {"None", "Storms", "Rain", "Snow", "Dust"};

        ImGui::Columns(2, "##planet", false);
        ImGui::SetColumnWidth(0, 110);

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Surface");
        ImGui::NextColumn();
        ImGui::Text("%s", SURF_NAMES[pp.surface]);
        ImGui::NextColumn();

        // Atmosphere
        if (pp.atmosphere.pressure > 0.01f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Atm Pressure");
            ImGui::NextColumn();
            ImGui::Text("%.2f atm", pp.atmosphere.pressure);
            ImGui::NextColumn();

            // Show dominant gas
            float max_frac = 0;
            const char* dom_gas = "N2";
            struct GasEntry { float frac; const char* name; };
            GasEntry gases[] = {
                {pp.atmosphere.n2_frac, "N2"},   {pp.atmosphere.o2_frac, "O2"},
                {pp.atmosphere.co2_frac, "CO2"}, {pp.atmosphere.h2_frac, "H2"},
                {pp.atmosphere.he_frac, "He"},   {pp.atmosphere.ch4_frac, "CH4"},
                {pp.atmosphere.nh3_frac, "NH3"},
            };
            for (auto& g : gases) {
                if (g.frac > max_frac) { max_frac = g.frac; dom_gas = g.name; }
            }

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Composition");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", dom_gas, max_frac * 100.0f);
            // Show secondary gas if significant
            float second_max = 0;
            const char* second_gas = "";
            for (auto& g : gases) {
                if (g.frac > second_max && g.name != dom_gas) {
                    second_max = g.frac; second_gas = g.name;
                }
            }
            if (second_max > 0.05f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), "%s %.0f%%",
                                   second_gas, second_max * 100.0f);
            }
            ImGui::NextColumn();

            if (pp.atmosphere.has_clouds) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Clouds");
                ImGui::NextColumn();
                ImGui::Text("%.0f%%", pp.cloud_coverage);
                ImGui::NextColumn();
            }
        }

        // Ocean
        if (pp.ocean_type != OCEAN_NONE) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean");
            ImGui::NextColumn();
            ImGui::Text("%s %.0f%%", OCEAN_NAMES[pp.ocean_type], pp.ocean_coverage);
            ImGui::NextColumn();

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Ocean Depth");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.ocean_depth);
            ImGui::NextColumn();
        }

        // Terrain
        if (pp.has_mountains) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Mountains");
            ImGui::NextColumn();
            ImGui::Text("%.1f km", pp.mountain_height);
            ImGui::NextColumn();
        }
        if (pp.has_continents) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Continents");
            ImGui::NextColumn();
            ImGui::Text("%d", pp.continent_count);
            ImGui::NextColumn();
        }

        // Weather
        if (pp.has_weather) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Weather");
            ImGui::NextColumn();
            ImGui::Text("%s", WEATHER_NAMES[pp.weather_type]);
            ImGui::NextColumn();
        }

        // Vegetation
        if (pp.vegetation_coverage > 1.0f) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Vegetation");
            ImGui::NextColumn();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%.0f%%",
                               pp.vegetation_coverage);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // ── Actions ──
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Body", ImVec2(-1, 0))) {
        b.marked_for_removal = true;
        if (camera.focus_body == selected_body) camera.release_focus();
        selected_body = -1;
        inspector_visible_ = false;
    }

    ImGui::End();
}

// ── Bottom bar ──────────────────────────────────────────────────────────────

void CosmosApp::draw_bottom_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float bar_h = 36.0f;
    float display_w = io.DisplaySize.x;
    float display_h = io.DisplaySize.y;
    float dt = io.DeltaTime;

    // Auto-hide animation
    bool mouse_near_bottom = (io.MousePos.y > display_h - 8.0f);
    float current_bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);
    bool mouse_over_bar = (io.MousePos.y > current_bar_y && bottom_bar_offset_ < 0.5f);
    bool keep_visible = show_menu_popup_ || show_pause_menu;
    float target = (mouse_near_bottom || mouse_over_bar || keep_visible) ? 0.0f : 1.0f;
    bottom_bar_offset_ += (target - bottom_bar_offset_) * std::min(1.0f, 8.0f * dt);
    if (bottom_bar_offset_ < 0.005f) bottom_bar_offset_ = 0.0f;
    if (bottom_bar_offset_ > 0.995f) bottom_bar_offset_ = 1.0f;

    float bar_y = display_h - bar_h + bottom_bar_offset_ * (bar_h + 4.0f);

    ImGui::SetNextWindowPos(ImVec2(0, bar_y));
    ImGui::SetNextWindowSize(ImVec2(display_w, bar_h));

    ImGuiWindowFlags bar_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.04f, 0.02f, 0.80f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    if (ImGui::Begin("##CosmosBottomBar", nullptr, bar_flags)) {
        // Left: Menu button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.15f, 0.05f, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.22f, 0.08f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.30f, 0.10f, 1.0f));
        if (ImGui::Button("Menu", ImVec2(70, 24))) {
            show_menu_popup_ = !show_menu_popup_;
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(0.4f, 0.35f, 0.2f, 0.5f), "|");
        ImGui::SameLine(0, 8);

        // Taskbar entries
        struct TBEntry { const char* label; bool* visible; };
        TBEntry entries[] = {
            {"Settings",  &settings_visible_},
            {"Spawn",     &spawn_menu_visible_},
            {"Bodies",    &body_list_visible_},
            {"Inspector", &inspector_visible_},
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

        for (int i = 0; i < 4; i++) {
            bool vis = *entries[i].visible;
            if (vis) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.14f, 0.05f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.20f, 0.08f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.16f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.06f, 0.02f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.10f, 0.04f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.08f, 0.03f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.5f, 0.3f, 0.8f));
            }

            char btn_id[64];
            snprintf(btn_id, sizeof(btn_id), "%s###CTB_%d", entries[i].label, i);
            if (ImGui::Button(btn_id, ImVec2(0, 22))) {
                *entries[i].visible = !(*entries[i].visible);
            }

            if (vis) {
                ImVec2 rmin = ImGui::GetItemRectMin();
                ImVec2 rmax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(rmin.x + 2, rmax.y - 2), ImVec2(rmax.x - 2, rmax.y),
                    IM_COL32(255, 200, 60, 200));
            }

            ImGui::PopStyleColor(4);
            ImGui::SameLine(0, 4);
        }
        ImGui::PopStyleVar(2);

        // Right: sim time + rate
        char time_buf[64], rate_buf[64];
        format_sim_time(cfg.sim_time_accumulated, time_buf, sizeof(time_buf));
        format_sim_time(std::pow(10.0, cfg.time_exponent), rate_buf, sizeof(rate_buf));

        char right_text[256];
        snprintf(right_text, sizeof(right_text), "T: %s  |  %s/s  |  %zu bodies",
                 time_buf, rate_buf, state.count());
        ImVec2 text_size = ImGui::CalcTextSize(right_text);
        ImGui::SameLine(display_w - text_size.x - 16.0f);
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.3f, 0.9f), "%s", right_text);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Menu popup
    if (show_menu_popup_) {
        float popup_w = 200.0f;
        float popup_h = 380.0f;
        float popup_x = 12.0f;
        float popup_y = std::max(10.0f, bar_y - popup_h - 4.0f);
        ImGui::SetNextWindowPos(ImVec2(popup_x, popup_y));
        ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h));

        ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.06f, 0.02f, 0.95f));

        if (ImGui::Begin("##CosmosMenuPopup", &show_menu_popup_, popup_flags)) {
            if (ImGui::TreeNodeEx("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::MenuItem(paused ? "Resume (Space)" : "Pause (Space)")) {
                    paused = !paused; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("New Simulation")) {
                    reset_simulation(); show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Empty Universe")) {
                    state.clear(); cfg.body_count = 0;
                    selected_body = -1; sim_time_ = 0.0f;
                    cfg.sim_time_accumulated = 0.0;
                    show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save (Ctrl+S)")) {
                    show_save_dialog_ = true; show_menu_popup_ = false;
                }
                if (ImGui::MenuItem("Load (Ctrl+L)")) {
                    show_load_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Import Body...")) {
                    show_import_dialog_ = true; show_menu_popup_ = false;
                }
                if (selected_body >= 0 && ImGui::MenuItem("Export Selected Body...")) {
                    show_export_dialog_ = true; show_menu_popup_ = false;
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("View", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool tmp;
                tmp = settings_visible_;
                if (ImGui::MenuItem("Settings", nullptr, tmp)) { settings_visible_ = !settings_visible_; show_menu_popup_ = false; }
                tmp = spawn_menu_visible_;
                if (ImGui::MenuItem("Spawn Menu", nullptr, tmp)) { spawn_menu_visible_ = !spawn_menu_visible_; show_menu_popup_ = false; }
                tmp = body_list_visible_;
                if (ImGui::MenuItem("Body List", nullptr, tmp)) { body_list_visible_ = !body_list_visible_; show_menu_popup_ = false; }
                tmp = inspector_visible_;
                if (ImGui::MenuItem("Inspector", nullptr, tmp)) { inspector_visible_ = !inspector_visible_; show_menu_popup_ = false; }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Navigation")) {
                if (ImGui::MenuItem("Return to Launcher")) { request_launcher = true; request_quit = true; }
                if (ImGui::MenuItem("Quit")) { request_quit = true; }
                ImGui::TreePop();
            }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

// ── Save / Load ─────────────────────────────────────────────────────────────

static constexpr uint32_t COSMOS_MAGIC   = 0x534D4F43; // "COSM"
static constexpr uint32_t COSMOS_VERSION = 1;

// POD struct for binary serialization of one body (fixed-size fields only)
#pragma pack(push, 1)
struct BodyPOD {
    float pos[3];
    float vel[3];
    float mass;
    float radius;
    float temperature;
    uint32_t type;
    int32_t parent;
    float age;
    float internal_energy;
    float luminosity;
    float fuel;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len; // followed by name_len bytes of name
};
#pragma pack(pop)

bool CosmosApp::save_simulation(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // Header
    f.write(reinterpret_cast<const char*>(&COSMOS_MAGIC), 4);
    f.write(reinterpret_cast<const char*>(&COSMOS_VERSION), 4);

    // Config (selected POD fields)
    f.write(reinterpret_cast<const char*>(&cfg.G), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.time_exponent), sizeof(double));
    f.write(reinterpret_cast<const char*>(&cfg.sim_time_accumulated), sizeof(double));
    f.write(reinterpret_cast<const char*>(&cfg.softening), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.damping), sizeof(float));

    uint32_t flags = 0;
    if (cfg.collisions) flags |= 1;
    if (cfg.tidal_forces) flags |= 2;
    if (cfg.collision_merging) flags |= 4;
    if (cfg.collision_fragmentation) flags |= 8;
    if (cfg.roche_limit) flags |= 16;
    if (cfg.temperature_system) flags |= 32;
    if (cfg.evaporation) flags |= 64;
    if (cfg.stellar_evolution) flags |= 128;
    if (cfg.star_lighting) flags |= 256;
    if (cfg.uniform_lighting) flags |= 512;
    if (cfg.parallel_gravity) flags |= 1024;
    if (cfg.fast_star_lighting) flags |= 2048;
    f.write(reinterpret_cast<const char*>(&flags), sizeof(uint32_t));

    f.write(reinterpret_cast<const char*>(&cfg.merge_speed_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.fragment_speed_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.fragment_count), sizeof(int));
    f.write(reinterpret_cast<const char*>(&cfg.radiative_cooling), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.collision_heating), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.evaporation_rate), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.stellar_timescale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ambient_strength), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.min_fragment_mass), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.max_frag_generation), sizeof(int));

    // Body count
    uint32_t body_count = (uint32_t)state.bodies.size();
    f.write(reinterpret_cast<const char*>(&body_count), 4);

    // Bodies
    for (const auto& b : state.bodies) {
        BodyPOD pod{};
        pod.pos[0] = b.pos.x; pod.pos[1] = b.pos.y; pod.pos[2] = b.pos.z;
        pod.vel[0] = b.vel.x; pod.vel[1] = b.vel.y; pod.vel[2] = b.vel.z;
        pod.mass = b.mass;
        pod.radius = b.radius;
        pod.temperature = b.temperature;
        pod.type = b.type;
        pod.parent = b.parent;
        pod.age = b.age;
        pod.internal_energy = b.internal_energy;
        pod.luminosity = b.luminosity;
        pod.fuel = b.fuel;
        pod.angular_vel = b.angular_vel;
        pod.stellar_stage = b.stellar_stage;
        pod.seed = b.seed;
        pod.frag_generation = b.frag_generation;
        pod.name_len = (uint32_t)b.name.size();
        f.write(reinterpret_cast<const char*>(&pod), sizeof(BodyPOD));
        if (pod.name_len > 0)
            f.write(b.name.data(), pod.name_len);
    }

    // Camera state
    f.write(reinterpret_cast<const char*>(&camera.azimuth), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.elevation), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.distance), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.fov), sizeof(float));
    f.write(reinterpret_cast<const char*>(&camera.target), sizeof(glm::vec3));

    return f.good();
}

bool CosmosApp::load_simulation(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (magic != COSMOS_MAGIC || version > COSMOS_VERSION) return false;

    // Config
    f.read(reinterpret_cast<char*>(&cfg.G), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.time_exponent), sizeof(double));
    f.read(reinterpret_cast<char*>(&cfg.sim_time_accumulated), sizeof(double));
    f.read(reinterpret_cast<char*>(&cfg.softening), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.damping), sizeof(float));

    uint32_t flags = 0;
    f.read(reinterpret_cast<char*>(&flags), sizeof(uint32_t));
    cfg.collisions              = (flags & 1) != 0;
    cfg.tidal_forces            = (flags & 2) != 0;
    cfg.collision_merging       = (flags & 4) != 0;
    cfg.collision_fragmentation = (flags & 8) != 0;
    cfg.roche_limit             = (flags & 16) != 0;
    cfg.temperature_system      = (flags & 32) != 0;
    cfg.evaporation             = (flags & 64) != 0;
    cfg.stellar_evolution       = (flags & 128) != 0;
    cfg.star_lighting           = (flags & 256) != 0;
    cfg.uniform_lighting        = (flags & 512) != 0;
    cfg.parallel_gravity        = (flags & 1024) != 0;
    cfg.fast_star_lighting      = (flags & 2048) != 0;

    f.read(reinterpret_cast<char*>(&cfg.merge_speed_threshold), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.fragment_speed_threshold), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.fragment_count), sizeof(int));
    f.read(reinterpret_cast<char*>(&cfg.radiative_cooling), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.collision_heating), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.evaporation_rate), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.stellar_timescale), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.ambient_strength), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.min_fragment_mass), sizeof(float));
    f.read(reinterpret_cast<char*>(&cfg.max_frag_generation), sizeof(int));

    // Body count
    uint32_t body_count = 0;
    f.read(reinterpret_cast<char*>(&body_count), 4);
    if (body_count > 10000) return false; // sanity check

    state.clear();
    state.bodies.reserve(body_count);

    for (uint32_t i = 0; i < body_count; i++) {
        BodyPOD pod{};
        f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPOD));

        CelestialBody b;
        b.pos = {pod.pos[0], pod.pos[1], pod.pos[2]};
        b.vel = {pod.vel[0], pod.vel[1], pod.vel[2]};
        b.mass = pod.mass;
        b.radius = pod.radius;
        b.temperature = pod.temperature;
        b.type = pod.type;
        b.parent = pod.parent;
        b.age = pod.age;
        b.internal_energy = pod.internal_energy;
        b.luminosity = pod.luminosity;
        b.fuel = pod.fuel;
        b.angular_vel = pod.angular_vel;
        b.stellar_stage = pod.stellar_stage;
        b.seed = pod.seed;
        b.frag_generation = pod.frag_generation;
        if (pod.name_len > 0 && pod.name_len < 256) {
            b.name.resize(pod.name_len);
            f.read(b.name.data(), pod.name_len);
        }
        state.bodies.push_back(std::move(b));
        state.trails.emplace_back();
    }

    // Camera state
    f.read(reinterpret_cast<char*>(&camera.azimuth), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.elevation), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.distance), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.fov), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.target), sizeof(glm::vec3));

    cfg.body_count = (uint32_t)state.bodies.size();
    selected_body = -1;
    sim_time_ = 0.0f;

    // Refresh cached planet properties for all loaded bodies
    for (auto& b : state.bodies) refresh_planet_props(b);

    return f.good();
}

// ── Import / Export individual bodies ────────────────────────────────────────
// Uses a simple text format (.csbody) for portability:
//   name, type, mass, radius, temperature, seed, pos(x,y,z), vel(x,y,z), fuel, etc.

bool CosmosApp::export_body(int index, const std::string& path) {
    if (index < 0 || index >= (int)state.bodies.size()) return false;
    const auto& b = state.bodies[index];

    std::ofstream f(path);
    if (!f) return false;

    f << "CSBODY 1\n";
    f << "name " << (b.name.empty() ? "Unnamed" : b.name) << "\n";
    f << "type " << b.type << "\n";
    f << "mass " << b.mass << "\n";
    f << "radius " << b.radius << "\n";
    f << "temperature " << b.temperature << "\n";
    f << "seed " << b.seed << "\n";
    f << "pos " << b.pos.x << " " << b.pos.y << " " << b.pos.z << "\n";
    f << "vel " << b.vel.x << " " << b.vel.y << " " << b.vel.z << "\n";
    f << "fuel " << b.fuel << "\n";
    f << "age " << b.age << "\n";
    f << "luminosity " << b.luminosity << "\n";
    f << "internal_energy " << b.internal_energy << "\n";
    f << "angular_vel " << b.angular_vel << "\n";
    f << "stellar_stage " << b.stellar_stage << "\n";
    f << "parent " << b.parent << "\n";
    f << "frag_generation " << b.frag_generation << "\n";

    return f.good();
}

bool CosmosApp::import_body(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string header;
    int version = 0;
    f >> header >> version;
    if (header != "CSBODY" || version < 1) return false;

    CelestialBody b;
    std::string key;
    while (f >> key) {
        if (key == "name") {
            std::getline(f >> std::ws, b.name);
        } else if (key == "type") { f >> b.type; }
        else if (key == "mass") { f >> b.mass; }
        else if (key == "radius") { f >> b.radius; }
        else if (key == "temperature") { f >> b.temperature; }
        else if (key == "seed") { f >> b.seed; }
        else if (key == "pos") { f >> b.pos.x >> b.pos.y >> b.pos.z; }
        else if (key == "vel") { f >> b.vel.x >> b.vel.y >> b.vel.z; }
        else if (key == "fuel") { f >> b.fuel; }
        else if (key == "age") { f >> b.age; }
        else if (key == "luminosity") { f >> b.luminosity; }
        else if (key == "internal_energy") { f >> b.internal_energy; }
        else if (key == "angular_vel") { f >> b.angular_vel; }
        else if (key == "stellar_stage") { f >> b.stellar_stage; }
        else if (key == "parent") { f >> b.parent; }
        else if (key == "frag_generation") { f >> b.frag_generation; }
    }

    if (b.name == "Unnamed") b.name.clear();
    refresh_planet_props(b);
    state.bodies.push_back(std::move(b));
    state.trails.emplace_back();
    return true;
}

// ── File dialog ─────────────────────────────────────────────────────────────

void CosmosApp::draw_file_dialog() {
    bool any_dialog = show_save_dialog_ || show_load_dialog_ ||
                      show_export_dialog_ || show_import_dialog_;
    if (!any_dialog) return;

    const char* title = show_save_dialog_   ? "Save Simulation" :
                        show_load_dialog_   ? "Load Simulation" :
                        show_export_dialog_ ? "Export Body" :
                                              "Import Body";

    const char* extension = (show_save_dialog_ || show_load_dialog_) ? ".cssim" : ".csbody";

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 200, io.DisplaySize.y * 0.5f - 100),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Appearing);

    bool open = true;
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("File path (%s):", extension);
        ImGui::InputText("##FilePath", file_path_buf_, sizeof(file_path_buf_));

        // Default path suggestion
        if (file_path_buf_[0] == '\0') {
            std::string def = get_data_dir();
            if (show_save_dialog_ || show_load_dialog_)
                def += "cosmos_save" + std::string(extension);
            else if (show_export_dialog_ && selected_body >= 0 &&
                     selected_body < (int)state.bodies.size())
                def += state.bodies[selected_body].name + extension;
            else
                def += "body" + std::string(extension);
            strncpy(file_path_buf_, def.c_str(), sizeof(file_path_buf_) - 1);
        }

        // List existing files in data dir
        if (show_load_dialog_ || show_import_dialog_) {
            ImGui::Separator();
            ImGui::Text("Existing files:");
            std::error_code ec;
            std::string dir = get_data_dir();
            if (std::filesystem::exists(dir, ec)) {
                for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    std::string fn = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    if (ext == extension) {
                        if (ImGui::Selectable(fn.c_str())) {
                            strncpy(file_path_buf_, entry.path().string().c_str(),
                                    sizeof(file_path_buf_) - 1);
                        }
                    }
                }
            }
        }

        ImGui::Separator();

        const char* action_label = (show_save_dialog_ || show_export_dialog_) ? "Save" : "Load";
        if (ImGui::Button(action_label, ImVec2(120, 30))) {
            bool ok = false;
            if (show_save_dialog_) {
                ok = save_simulation(file_path_buf_);
                last_save_status_ = ok ? "Saved successfully" : "Save failed";
            } else if (show_load_dialog_) {
                ok = load_simulation(file_path_buf_);
                last_save_status_ = ok ? "Loaded successfully" : "Load failed";
            } else if (show_export_dialog_) {
                ok = export_body(selected_body, file_path_buf_);
                last_save_status_ = ok ? "Exported successfully" : "Export failed";
            } else if (show_import_dialog_) {
                ok = import_body(file_path_buf_);
                last_save_status_ = ok ? "Imported successfully" : "Import failed";
            }
            save_status_timer_ = 3.0f;
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
            file_path_buf_[0] = '\0';
        }
    }
    ImGui::End();

    if (!open) {
        show_save_dialog_ = show_load_dialog_ = show_export_dialog_ = show_import_dialog_ = false;
        file_path_buf_[0] = '\0';
    }
}
