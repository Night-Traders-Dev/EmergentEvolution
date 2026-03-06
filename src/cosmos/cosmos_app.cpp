#include "cosmos/cosmos_app_internal.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <unordered_map>
#include <thread>
#include <ctime>

#if !defined(_WIN32)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

template <typename Fn>
static void run_parallel_chunks(size_t count, size_t workers, Fn&& fn) {
    if (count == 0) return;
    workers = std::max<size_t>(1, std::min(workers, count));
    if (workers <= 1) {
        fn(0, count);
        return;
    }

    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (size_t t = 1; t < workers; ++t) {
        const size_t begin = (count * t) / workers;
        const size_t end = (count * (t + 1)) / workers;
        pool.emplace_back([&, begin, end]() { fn(begin, end); });
    }
    fn(0, count / workers);
    for (auto& worker : pool) worker.join();
}

namespace {

#if !defined(_WIN32)
std::atomic<int> g_crash_log_fd{-1};

void crash_log_write(const char* msg) {
    if (!msg) return;
    int fd = g_crash_log_fd.load();
    if (fd < 0) return;
    size_t len = std::strlen(msg);
    while (len > 0) {
        ssize_t written = ::write(fd, msg, len);
        if (written <= 0) break;
        msg += written;
        len -= (size_t)written;
    }
}

void cosmos_signal_handler(int signum, siginfo_t* info, void* /*ucontext*/) {
    char line[256];
    int code = info ? info->si_code : 0;
    int pid = info ? info->si_pid : 0;
    std::snprintf(line, sizeof(line),
                  "\n=== Cosmos crash signal %d code=%d pid=%d ===\n",
                  signum, code, pid);
    crash_log_write(line);

    void* bt[64];
    int count = backtrace(bt, 64);
    int fd = g_crash_log_fd.load();
    if (fd >= 0 && count > 0)
        backtrace_symbols_fd(bt, count, fd);
    crash_log_write("=== end crash ===\n");
    _exit(128 + signum);
}
#endif

void install_crash_handlers_once() {
    static std::once_flag once;
    std::call_once(once, []() {
#if !defined(_WIN32)
        int fd = ::open("/tmp/cosmos_crash.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
        g_crash_log_fd.store(fd);
        crash_log_write("\n=== Cosmos crash handler installed ===\n");

        struct sigaction sa{};
        sa.sa_sigaction = cosmos_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
        for (int sig : signals)
            sigaction(sig, &sa, nullptr);
#endif
        std::set_terminate([]() {
#if !defined(_WIN32)
            crash_log_write("\n=== std::terminate() called ===\n");
            void* bt[64];
            int count = backtrace(bt, 64);
            int fd = g_crash_log_fd.load();
            if (fd >= 0 && count > 0)
                backtrace_symbols_fd(bt, count, fd);
            crash_log_write("=== end terminate ===\n");
#endif
            std::_Exit(1);
        });
    });
}

float spin_fragmentation_critical_omega(const CelestialBody& body, float G) {
    if (!std::isfinite(body.mass) || !std::isfinite(body.radius) ||
        body.mass <= 0.0f || body.radius <= 0.0f) {
        return 0.0f;
    }

    float radius = std::max(body.radius, body.type == CTYPE_DUST ? 0.04f : 0.1f);
    float omega = std::sqrt(std::max(G * body.mass / std::max(radius * radius * radius, 1.0e-8f), 0.0f));
    if (!std::isfinite(omega) || omega <= 0.0f)
        return 0.0f;

    MaterialComposition materials = derive_materials(body);
    float material_factor = 1.0f;
    if (gas_dominated_body(body, materials)) {
        material_factor *= 0.72f;
    } else if (roche_secondary_fluid_like(body)) {
        material_factor *= 0.82f;
    }
    if (body.type == CTYPE_COMET) material_factor *= 0.80f;
    if (body.type == CTYPE_DUST) material_factor *= 0.72f;
    if (body.type == CTYPE_ASTEROID) material_factor *= 0.92f;
    if (materials.water > 0.45f) material_factor *= 0.93f;
    if (materials.iron > 0.35f) material_factor *= 1.08f;

    switch (body.material_phase) {
    case PHASE_LIQUID:
    case PHASE_MOLTEN:
        material_factor *= 0.82f;
        break;
    case PHASE_GAS:
    case PHASE_PLASMA:
    case PHASE_COLLAPSING:
        material_factor *= 0.68f;
        break;
    default:
        break;
    }

    material_factor = std::clamp(material_factor, 0.45f, 1.20f);
    return omega * material_factor;
}

float spin_fragmentation_ratio(const CelestialBody& body, float G) {
    float critical = spin_fragmentation_critical_omega(body, G);
    if (critical <= 0.0f)
        return 0.0f;
    return std::abs(body.angular_vel) / critical;
}

glm::vec3 spin_fragmentation_axis(const CelestialBody& body) {
    if (glm::dot(body.vel, body.vel) > 1.0e-10f)
        return glm::normalize(body.vel);

    uint32_t h0 = hash_combine(body.seed, 0x5311u);
    uint32_t h1 = hash_combine(body.seed, 0x9F02u);
    uint32_t h2 = hash_combine(body.seed, 0xC733u);
    glm::vec3 axis(hash_float(h0) * 2.0f - 1.0f,
                   hash_float(h1) * 2.0f - 1.0f,
                   hash_float(h2) * 2.0f - 1.0f);
    if (glm::dot(axis, axis) < 1.0e-10f)
        axis = glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(axis);
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────────

static void seed_default_system(CosmosState& state, const CosmosConfig& cfg) {
    state.clear();

    // Sun at origin
    CelestialBody sun;
    sun.pos  = {0.0f, 0.0f, 0.0f};
    sun.vel  = {0.0f, 0.0f, 0.0f};
    sun.mass = 1.0f;
    sun.radius = solar_radius_sim_units();
    sun.temperature = 5778.0f;
    sun.type = CTYPE_STAR;
    sun.seed = 42;
    sun.fuel = 0.72f;
    sun.angular_vel = (2.0f * 3.14159265359f) / (26.0f * 24.0f * 3600.0f);
    sun.luminosity = expected_stellar_luminosity(sun.mass, sun.temperature, sun.radius,
                                                 SSTAGE_MAIN_SEQUENCE, sun.fuel);
    sun.type = classify_star_spectral(sun.temperature, sun.mass);
    sun.name = generate_body_name(sun.seed, sun.type);
    state.bodies.push_back(sun);

    // Planets in circular orbits in the XZ plane
    const float orbit_radii[] = {
        sun.radius * 6.0f,
        sun.radius * 9.5f,
        sun.radius * 14.0f,
        sun.radius * 22.0f
    };
    // Solar-mass units: roughly Mercury, Earth, Mars, Jupiter class worlds.
    const float planet_mass[] = {1.66e-7f, 3.00e-6f, 3.22e-7f, 9.54e-4f};
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
        moon.mass = 3.70e-8f;
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
        a.mass = randf(5.0e-11f, 3.0e-9f);
        a.radius = randf(0.4f, 1.6f);
        a.temperature = 100.0f;
        a.type = CTYPE_ASTEROID;
        a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a);
    }

    state.trails.resize(state.bodies.size());

    // Initialize cached planet properties for all bodies
    for (auto& b : state.bodies) refresh_body_render_state(b, &state);
}

void CosmosApp::init(GLFWwindow* window, ProgressCB progress_cb) {
    auto report = [&](float frac, const char* label) {
        if (progress_cb) progress_cb(frac, label);
    };

    install_crash_handlers_once();

    report(0.0f, "Initializing Vulkan...");
    vk.init(window);

    report(0.15f, "Creating renderer...");
    renderer.init(vk, window);

    report(0.30f, "Compiling shaders...");
    raytracer_.init(vk, renderer.render_pass());

    report(0.70f, "Initializing physics...");
    gravity_compute_.init(vk);

    report(0.90f, "Loading settings...");
    state.clear();
    cfg.body_count = 0;

    camera.target = {0, 0, 0};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    camera.azimuth = 0.0f;

    load_persistent_settings();

    report(1.0f, "Ready");
    debug_logf("init complete diagnostics=%d pause_on_invalid=%d bh=%d gpu_bh=%d verlet=%d",
               diagnostics_enabled_ ? 1 : 0, diagnostics_pause_on_invalid_ ? 1 : 0,
               cfg.barnes_hut ? 1 : 0, cfg.gpu_barnes_hut ? 1 : 0, cfg.velocity_verlet ? 1 : 0);
}

// ── Screen → world spawn position ────────────────────────────────────────────

glm::vec3 CosmosApp::screen_to_spawn_pos(double mx, double my, int fb_w, int fb_h) const {
    float W = (float)fb_w, H = (float)fb_h;
    float aspect = W / H;
    glm::dmat4 inv_vp = glm::inverse(camera.proj_matrix_d(aspect) * camera.view_matrix_d());
    float ndc_x = ((float)mx / W) * 2.0f - 1.0f;
    float ndc_y = 1.0f - ((float)my / H) * 2.0f;
    glm::dvec4 near_clip = inv_vp * glm::dvec4(ndc_x, ndc_y, -1.0, 1.0);
    glm::dvec4 far_clip  = inv_vp * glm::dvec4(ndc_x, ndc_y,  1.0, 1.0);
    glm::dvec3 near_pt = glm::dvec3(near_clip) / near_clip.w;
    glm::dvec3 far_pt  = glm::dvec3(far_clip) / far_clip.w;
    glm::dvec3 ray_dir = glm::normalize(far_pt - near_pt);
    glm::dvec3 eye = camera.eye_position_d();
    glm::dvec3 plane_normal = glm::normalize(glm::dvec3(camera.target) - eye);
    double denom = glm::dot(ray_dir, plane_normal);
    glm::vec3 pos = camera.target;
    if (std::abs(denom) > 1e-9) {
        double t = glm::dot(glm::dvec3(camera.target) - near_pt, plane_normal) / denom;
        if (t > 0.0)
            pos = glm::vec3(near_pt + ray_dir * t);
    }
    return pos;
}

// ── Body picking (screen-space hit test) ─────────────────────────────────────

int CosmosApp::pick_body(float mx, float my, float W, float H) const {
    float aspect = W / H;
    glm::dmat4 vp = camera.proj_matrix_d(aspect) * camera.view_matrix_d();
    float fov_rad = glm::radians(camera.fov);

    int best = -1;
    float best_dist = 30.0f;
    for (size_t i = 0; i < state.bodies.size(); i++) {
        const auto& b = state.bodies[i];
        glm::dvec4 clip = vp * glm::dvec4(b.pos, 1.0);
        if (clip.w <= 0.0) continue;
        glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
        float sx = (float)((ndc.x * 0.5 + 0.5) * (double)W);
        float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)H);

        float dx = sx - mx;
        float dy = sy - my;
        float d = std::sqrt(dx * dx + dy * dy);
        float sr = (float)(((double)b.radius / clip.w) * ((double)H / (2.0 * std::tan((double)fov_rad * 0.5))));
        float pick_r = std::max(sr, 12.0f);
        if (d < pick_r && d < best_dist) {
            best_dist = d;
            best = (int)i;
        }
    }
    return best;
}

void CosmosApp::destroy() {
    save_persistent_settings();
    vkDeviceWaitIdle(vk.device);
    gravity_compute_.destroy(vk);
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
    displayed_time_rate_ = 0.0;
    adaptive_substeps_last_ = 1;
    adaptive_substeps_required_ = 1;
    adaptive_substeps_saturated_ = false;
    adaptive_substep_refining_ = false;
    escaped_mass_total_ = 0.0;
    radiated_energy_total_ = 0.0;
    escaped_energy_total_ = 0.0;
    escaped_momentum_total_ = glm::dvec3(0.0);
    camera = OrbitCamera{};
    camera.distance = 600.0f;
    camera.target_distance = 600.0f;
    camera.elevation = 0.5f;
    paused = false;
}

void CosmosApp::reset_bottom_bar_menu_defaults() {
    const int preserved_nebula_render_mode = cfg.nebula_render_mode;
    const uint32_t preserved_body_count = static_cast<uint32_t>(state.count());
    const double preserved_sim_time = cfg.sim_time_accumulated;

    cfg = CosmosConfig{};
    cfg.nebula_render_mode = preserved_nebula_render_mode;
    cfg.body_count = preserved_body_count;
    cfg.sim_time_accumulated = preserved_sim_time;
    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);

    camera = OrbitCamera{};
    bottom_bar_autohide_ = true;
    diagnostics_enabled_ = true;
    diagnostics_pause_on_invalid_ = true;
    displayed_time_rate_ = paused ? 0.0 : (double)cfg.dt_scale * (reverse_time_ ? -1.0 : 1.0);
    adaptive_substeps_last_ = 1;
    adaptive_substeps_required_ = 1;
    adaptive_substeps_saturated_ = false;
    adaptive_substep_refining_ = false;
    escaped_mass_total_ = 0.0;
    radiated_energy_total_ = 0.0;
    escaped_energy_total_ = 0.0;
    escaped_momentum_total_ = glm::dvec3(0.0);

    apply_dust_debug_mode();
    update_body_tracking_cache();
}

void CosmosApp::account_escaped_mass(const CelestialBody& source, float amount,
                                     float thermal_energy) {
    if (amount <= 0.0f || !std::isfinite(amount))
        return;
    double m = (double)amount;
    glm::dvec3 v = glm::dvec3(source.vel);
    escaped_mass_total_ += m;
    escaped_momentum_total_ += v * m;
    escaped_energy_total_ += 0.5 * m * (double)glm::dot(v, v) +
                             std::max(0.0, (double)thermal_energy);
}

int CosmosApp::spawn_at(glm::vec3 pos) {
    const bool is_small_body = (spawn_type == CTYPE_ASTEROID || spawn_type == CTYPE_COMET || spawn_type == CTYPE_DUST);
    const int requested_count = std::clamp(spawn_draft_.small_body_spawn_count, 1, 1000);
    const bool use_batch = is_small_body && requested_count > 1;
    const int spawn_count = use_batch ? requested_count : 1;

    uint32_t base_seed = hash_combine(hash_combine(float_bits(pos.x), float_bits(pos.y)), float_bits(pos.z));
    base_seed = hash_combine(base_seed, (uint32_t)spawn_type);
    base_seed = hash_combine(base_seed, (uint32_t)(sim_time_ * 1000.0f) + 2166136261u);
    uint64_t wall_ticks = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::random_device rd;
    base_seed = hash_combine(base_seed, (uint32_t)wall_ticks);
    base_seed = hash_combine(base_seed, (uint32_t)(wall_ticks >> 32));
    base_seed = hash_combine(base_seed, rd());
    std::mt19937 layout_rng(base_seed ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
    std::uniform_real_distribution<float> jitter01(-1.0f, 1.0f);

    float nominal_radius = spawn_draft_.override_radius
        ? std::max(0.04f, spawn_draft_.radius)
        : std::max(0.06f, std::cbrt(std::max(spawn_mass, 1.0e-13f)) * 5.0f);
    float cluster_radius = std::max(6.0f, std::cbrt((float)spawn_count) * std::max(1.2f, nominal_radius) * 4.0f);

    auto layout_offset = [&](int index) -> glm::vec3 {
        if (!use_batch) return glm::vec3(0.0f);
        const float two_pi = 6.28318530718f;
        const int layout = std::clamp(spawn_draft_.small_body_layout, 0, 3);
        switch (layout) {
        case 1: { // Sphere
            float u = ((float)index + 0.5f) / (float)spawn_count;
            float y = 1.0f - 2.0f * u;
            float rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float theta = 2.39996323f * (float)index;
            glm::vec3 dir(std::cos(theta) * rr, y, std::sin(theta) * rr);
            float radial = cluster_radius * (0.42f + 0.58f * unit01(layout_rng));
            return dir * radial;
        }
        case 2: { // Cube
            int side = std::max(1, (int)std::ceil(std::cbrt((float)spawn_count)));
            int ix = index % side;
            int iy = (index / side) % side;
            int iz = index / (side * side);
            float denom = (side > 1) ? (float)(side - 1) : 1.0f;
            float spacing = (cluster_radius * 2.0f) / denom;
            return glm::vec3(
                ((float)ix - 0.5f * (float)(side - 1)) * spacing,
                ((float)iy - 0.5f * (float)(side - 1)) * spacing,
                ((float)iz - 0.5f * (float)(side - 1)) * spacing);
        }
        case 3: { // Torus
            float t = (float)index / (float)spawn_count;
            float u = t * two_pi;
            float frac = std::fmod((float)index * 0.61803398875f + unit01(layout_rng) * 0.2f, 1.0f);
            if (frac < 0.0f) frac += 1.0f;
            float v = frac * two_pi;
            float major = cluster_radius;
            float minor = cluster_radius * 0.30f;
            float ring = major + minor * std::cos(v);
            return glm::vec3(ring * std::cos(u), minor * std::sin(v), ring * std::sin(u));
        }
        default: { // Random
            return glm::vec3(
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius,
                jitter01(layout_rng) * cluster_radius);
        }
        }
    };

    auto spawn_single = [&](const glm::vec3& spawn_pos, int ordinal) -> int {
        CelestialBody nb;
        nb.pos = spawn_pos;
        nb.vel = glm::vec3(0.0f);
        nb.mass = spawn_mass;
        nb.radius = std::max(3.0f, std::cbrt(spawn_mass) * 5.0f);
        nb.type = (uint32_t)spawn_type;
        uint32_t pos_seed = hash_combine(hash_combine(float_bits(spawn_pos.x), float_bits(spawn_pos.y)), float_bits(spawn_pos.z));
        nb.seed = hash_combine(hash_combine(pos_seed,
            (uint32_t)state.bodies.size() * 747796405u + 2891336453u),
            (uint32_t)(sim_time_ * 1000.0f) + 1181783497u + (uint32_t)ordinal * 2654435761u);
        std::mt19937 rng(nb.seed ^ (uint32_t)(sim_time_ * 1000.0f) ^ ((uint32_t)ordinal * 1013904223u));

        // Nebulae spawn as a cloud of many small gas particles, not a single body.
        if (spawn_type == CTYPE_NEBULA) {
            float cloud_r = std::max(35.0f, std::cbrt(spawn_mass) * 120.0f);
            if (spawn_draft_.override_radius)
                cloud_r = std::max(10.0f, spawn_draft_.radius);
            glm::vec3 vel(0.0f);
            if (spawn_draft_.override_velocity)
                vel = spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
            return spawn_nebula_cloud(spawn_pos, vel, spawn_mass, cloud_r, nb.seed);
        }

        clear_ring_system(nb);
        nb.material_phase = PHASE_SOLID;
        nb.phase_intensity = 0.0f;
        nb.collapse_progress = 0.0f;
        clear_impact_signature(nb);
        nb.forced_surface = -1;
        nb.custom_material = false;
        nb.props_valid = false;
        nb.visuals_valid = false;
        nb.cached_temp_band = -1;
        nb.cached_visual_temp_band = -1;

        if (is_star_type((uint32_t)spawn_type)) {
            randomize_star_properties(nb, rng, (uint32_t)spawn_type);
            if (spawn_draft_.star_stage_hint >= 0 &&
                spawn_draft_.star_stage_hint < (int)SSTAGE_COUNT) {
                nb.stellar_stage = (uint32_t)spawn_draft_.star_stage_hint;
                if (nb.stellar_stage == SSTAGE_WHITE_DWARF) {
                    nb.mass = std::clamp(nb.mass, 0.17f, 1.44f);
                    nb.fuel = 0.0f;
                    nb.temperature = std::clamp(nb.temperature, 9000.0f, 140000.0f);
                } else if (nb.stellar_stage == SSTAGE_NEUTRON_STAR) {
                    nb.mass = std::clamp(nb.mass, 1.10f, 2.50f);
                    nb.fuel = 0.0f;
                    nb.temperature = std::clamp(std::max(nb.temperature, 70000.0f), 70000.0f, 250000.0f);
                } else if (nb.stellar_stage == SSTAGE_RED_GIANT || nb.stellar_stage == SSTAGE_AGB) {
                    nb.mass = std::max(nb.mass, 0.8f);
                    nb.temperature = std::clamp(nb.temperature * 0.62f, 2800.0f, 5600.0f);
                } else if (nb.stellar_stage == SSTAGE_SUPERGIANT || nb.stellar_stage == SSTAGE_HYPERGIANT) {
                    nb.mass = std::max(nb.mass, CORE_COLLAPSE_MIN_MASS_SOLAR);
                    nb.temperature = std::clamp(nb.temperature * 0.72f, 3200.0f, 52000.0f);
                }
                nb.radius = expected_star_radius(nb);
                nb.type = classify_star_spectral(std::max(nb.temperature, 250.0f), std::max(nb.mass, 0.003f));
                nb.luminosity = expected_stellar_luminosity(nb.mass, nb.temperature, nb.radius,
                                                            nb.stellar_stage, nb.fuel);
            }
            nb.material_phase = PHASE_PLASMA;
            nb.phase_intensity = 1.0f;
        } else if (is_black_hole_type((uint32_t)spawn_type)) {
            nb.temperature = 0.0f;
            nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
            if (spawn_type == CTYPE_BLACK_HOLE)
                nb.type = classify_black_hole(nb.mass);
            nb.material_phase = PHASE_PLASMA;
            nb.phase_intensity = 1.0f;
        } else {
            nb.temperature = 300.0f;
            if (spawn_type == CTYPE_PLANET) {
                randomize_planet_properties(nb, state, cfg, rng);
            } else if (spawn_type == CTYPE_MOON) {
                randomize_moon_properties(nb, state, rng);
            } else if (spawn_type == CTYPE_ASTEROID) {
                randomize_small_body_properties(nb, rng, false);
            } else if (spawn_type == CTYPE_COMET) {
                randomize_small_body_properties(nb, rng, true);
            } else if (spawn_type == CTYPE_DUST) {
                randomize_dust_properties(nb, rng);
            } else if (spawn_type == CTYPE_NEBULA) {
                randomize_nebula_properties(nb, rng);
            }
        }

        if (spawn_draft_.override_temperature)
            nb.temperature = std::clamp(spawn_draft_.temperature, 2.7f, 120000.0f);
        if (spawn_draft_.override_radius)
            nb.radius = std::max(0.04f, spawn_draft_.radius);
        if (spawn_draft_.override_rotation) {
            float hours = std::max(spawn_draft_.rotation_hours, 0.1f);
            float sign = (nb.angular_vel < 0.0f) ? -1.0f : 1.0f;
            nb.angular_vel = sign * (2.0f * 3.14159265359f) / (hours * 3600.0f);
        }
        if (spawn_draft_.override_material) {
            nb.custom_material = true;
            nb.custom_iron = std::max(spawn_draft_.material_iron, 0.0f);
            nb.custom_silicate = std::max(spawn_draft_.material_silicate, 0.0f);
            nb.custom_water = std::max(spawn_draft_.material_ice, 0.0f);
            nb.custom_hydrogen = std::max(spawn_draft_.material_hydrogen, 0.0f);
        }
        if (nb.type == CTYPE_NEBULA) {
            // Nebulae are hydrogen-dominated gas/dust clouds, not gas-giant planets.
            nb.material_phase = PHASE_GAS;
            nb.phase_intensity = std::max(nb.phase_intensity, 0.70f);
            nb.custom_material = true;
            nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.85f);
            nb.custom_silicate = std::clamp(nb.custom_silicate, 0.02f, 0.12f);
            nb.custom_water = std::clamp(nb.custom_water, 0.0f, 0.08f);
            nb.custom_iron = std::clamp(nb.custom_iron, 0.0f, 0.04f);
            nb.forced_surface = -1;
            clear_ring_system(nb);
            nb.props_valid = false;
            nb.visuals_valid = false;
        }
        if ((nb.type == CTYPE_PLANET || nb.type == CTYPE_MOON) && spawn_draft_.planet_look > 0) {
            float mass_earth = nb.mass / std::max(EARTH_MASS_SOLAR, 1.0e-12f);
            bool force_gas = (spawn_draft_.planet_look == 5 && nb.type == CTYPE_PLANET);
            bool likely_gas = mass_earth > 10.0f;
            if (force_gas) {
                nb.forced_surface = 4; // gas giant
                nb.custom_material = true;
                nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.70f);
                nb.custom_silicate = std::min(nb.custom_silicate, 0.20f);
                nb.custom_iron = std::min(nb.custom_iron, 0.08f);
                nb.custom_water = std::max(nb.custom_water, 0.02f);
                nb.props_valid = false;
                nb.visuals_valid = false;
            } else if (!likely_gas) {
                switch (spawn_draft_.planet_look) {
                case 1: nb.forced_surface = 0; break; // rocky
                case 2: nb.forced_surface = 1; break; // water
                case 3: nb.forced_surface = 2; break; // ice
                case 4: nb.forced_surface = 3; break; // earth-like
                default: nb.forced_surface = -1; break;
                }
                nb.props_valid = false;
                nb.visuals_valid = false;
            }
        }
        if (body_can_host_rings(nb)) {
            if (spawn_draft_.spawn_rings) {
                int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
                if (spawn_draft_.override_ring_layout) {
                    float ring_tilt = std::clamp(std::abs(nb.angular_vel) * 150.0f, 0.02f, 0.45f);
                    switch (ring_style) {
                    case 0: ring_tilt = 0.14f; break; // Saturn-like
                    case 1: ring_tilt = 1.24f; break; // Uranus-like high obliquity
                    case 2: ring_tilt = 0.52f; break; // Neptune-like
                    case 3: ring_tilt = 0.32f; break; // Torus
                    case 4: ring_tilt = 0.18f; break; // Realistic disk
                    case 5: ring_tilt = 0.90f; break; // Unrealistic geometry
                    case 6: ring_tilt = 0.22f; break; // Resonance gaps
                    default: break;
                    }
                    set_ring_system(nb,
                        nb.radius * std::max(spawn_draft_.ring_inner_mult, 1.15f),
                        nb.radius * std::max(spawn_draft_.ring_outer_mult, spawn_draft_.ring_inner_mult + 0.25f),
                        std::clamp(spawn_draft_.ring_density, 0.01f, 1.0f),
                        std::clamp(spawn_draft_.ring_ice_fraction, 0.0f, 1.0f),
                        std::clamp(ring_tilt, 0.02f, 1.30f));
                } else if (nb.ring_density <= 0.001f) {
                    set_ring_system(nb, nb.radius * 1.5f, nb.radius * 3.0f, 0.32f, 0.55f, 0.12f);
                }
            } else {
                clear_ring_system(nb);
            }
        } else {
            clear_ring_system(nb);
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
        if (spawn_draft_.override_velocity) {
            nb.vel += spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
        }

        nb.name = generate_body_name(nb.seed, nb.type);
        nb.non_attracting = (nb.type == CTYPE_DUST) ? cfg.dust_debug_non_attracting : nb.non_attracting;
        refresh_body_render_state(nb, &state);
        state.bodies.push_back(nb);
        state.trails.emplace_back();

        int host_idx = (int)state.bodies.size() - 1;
        const auto spawned = state.bodies[(size_t)host_idx];
        int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
        if (cfg.planetary_rings && spawned.ring_density > 0.001f &&
            (spawned.type == CTYPE_PLANET || spawned.type == CTYPE_MOON)) {
            float annulus = std::max(spawned.ring_outer_radius * spawned.ring_outer_radius -
                                     spawned.ring_inner_radius * spawned.ring_inner_radius, 1.0f);
            float mass_hint = std::max(spawned.mass * spawned.ring_density * 0.00012f *
                                       (annulus / std::max(spawned.radius * spawned.radius, 1.0e-5f)),
                                       std::max(cfg.min_fragment_mass, 1.0e-12f) * 48.0f);
            spawn_dust_ring(host_idx, mass_hint, spawned.ring_inner_radius, spawned.ring_outer_radius,
                            spawned.ring_density, spawned.ring_ice_fraction, spawned.seed ^ 0xD05751EDu,
                            ring_style);
            if (host_idx >= 0 && host_idx < (int)state.bodies.size())
                clear_ring_system(state.bodies[(size_t)host_idx]);
        }

        if (spawn_draft_.spawn_moons && host_idx >= 0 && host_idx < (int)state.bodies.size()) {
            spawn_moons_for_host(host_idx,
                                 std::clamp(spawn_draft_.moon_count, 1, 100),
                                 std::clamp(spawn_draft_.moon_orbit_layout, 0, 4),
                                 std::clamp(spawn_draft_.moon_inclination_deg, 0.0f, 85.0f),
                                 std::clamp(spawn_draft_.moon_spacing_scale, 0.35f, 4.0f));
        }
        return host_idx;
    };

    int first_idx = (int)state.bodies.size();
    for (int i = 0; i < spawn_count; ++i) {
        glm::vec3 offset = layout_offset(i);
        spawn_single(pos + offset, i);
    }

    if (diagnostics_enabled_)
        validate_body_state("spawn/manual", true);

    return (first_idx < (int)state.bodies.size()) ? first_idx : -1;
}

// ── Spawn preview body construction ─────────────────────────────────────────

uint32_t CosmosApp::draft_settings_hash() const {
    uint32_t h = 0x811C9DC5u;
    auto mix = [&](uint32_t v) { h ^= v; h *= 0x01000193u; };
    mix((uint32_t)spawn_draft_.planet_look);
    mix((uint32_t)spawn_draft_.star_stage_hint);
    mix(spawn_draft_.override_temperature ? 1u : 0u);
    mix(float_bits(spawn_draft_.temperature));
    mix(spawn_draft_.override_radius ? 1u : 0u);
    mix(float_bits(spawn_draft_.radius));
    mix(spawn_draft_.override_material ? 1u : 0u);
    mix(float_bits(spawn_draft_.material_iron));
    mix(float_bits(spawn_draft_.material_silicate));
    mix(float_bits(spawn_draft_.material_ice));
    mix(float_bits(spawn_draft_.material_hydrogen));
    mix(spawn_draft_.spawn_rings ? 1u : 0u);
    return h;
}

void CosmosApp::build_preview_body() {
    CelestialBody& nb = preview_body_;
    nb = CelestialBody{};  // reset
    nb.mass = spawn_mass;
    nb.radius = std::max(3.0f, std::cbrt(std::max(spawn_mass, 1.0e-13f)) * 5.0f);
    nb.type = (uint32_t)spawn_type;
    nb.seed = hash_combine(preview_reroll_counter_ * 747796405u + 2891336453u,
                            (uint32_t)(spawn_type) * 2654435761u);
    std::mt19937 rng(nb.seed);

    clear_ring_system(nb);
    nb.material_phase = PHASE_SOLID;
    nb.phase_intensity = 0.0f;
    nb.collapse_progress = 0.0f;
    clear_impact_signature(nb);
    nb.forced_surface = -1;
    nb.custom_material = false;
    nb.props_valid = false;
    nb.visuals_valid = false;
    nb.cached_temp_band = -1;
    nb.cached_visual_temp_band = -1;

    if (is_star_type((uint32_t)spawn_type)) {
        randomize_star_properties(nb, rng, (uint32_t)spawn_type);
        if (spawn_draft_.star_stage_hint >= 0 &&
            spawn_draft_.star_stage_hint < (int)SSTAGE_COUNT) {
            nb.stellar_stage = (uint32_t)spawn_draft_.star_stage_hint;
            nb.radius = expected_star_radius(nb);
            nb.type = classify_star_spectral(std::max(nb.temperature, 250.0f),
                                              std::max(nb.mass, 0.003f));
            nb.luminosity = expected_stellar_luminosity(nb.mass, nb.temperature, nb.radius,
                                                         nb.stellar_stage, nb.fuel);
        }
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else if (is_black_hole_type((uint32_t)spawn_type)) {
        nb.temperature = 0.0f;
        nb.radius = std::max(10.0f, std::cbrt(spawn_mass) * 4.0f);
        if (spawn_type == CTYPE_BLACK_HOLE)
            nb.type = classify_black_hole(nb.mass);
        nb.material_phase = PHASE_PLASMA;
        nb.phase_intensity = 1.0f;
    } else {
        nb.temperature = 300.0f;
        if (spawn_type == CTYPE_PLANET) {
            randomize_planet_properties(nb, state, cfg, rng);
        } else if (spawn_type == CTYPE_MOON) {
            randomize_moon_properties(nb, state, rng);
        } else if (spawn_type == CTYPE_ASTEROID) {
            randomize_small_body_properties(nb, rng, false);
        } else if (spawn_type == CTYPE_COMET) {
            randomize_small_body_properties(nb, rng, true);
        } else if (spawn_type == CTYPE_DUST) {
            randomize_dust_properties(nb, rng);
        } else if (spawn_type == CTYPE_NEBULA) {
            randomize_nebula_properties(nb, rng);
        }
    }

    if (spawn_draft_.override_temperature)
        nb.temperature = std::clamp(spawn_draft_.temperature, 2.7f, 120000.0f);
    if (spawn_draft_.override_radius)
        nb.radius = std::max(0.04f, spawn_draft_.radius);
    if (spawn_draft_.override_material) {
        nb.custom_material = true;
        nb.custom_iron = std::max(spawn_draft_.material_iron, 0.0f);
        nb.custom_silicate = std::max(spawn_draft_.material_silicate, 0.0f);
        nb.custom_water = std::max(spawn_draft_.material_ice, 0.0f);
        nb.custom_hydrogen = std::max(spawn_draft_.material_hydrogen, 0.0f);
    }
    if (nb.type == CTYPE_NEBULA) {
        nb.material_phase = PHASE_GAS;
        nb.phase_intensity = std::max(nb.phase_intensity, 0.70f);
        nb.custom_material = true;
        nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.85f);
    }
    if ((nb.type == CTYPE_PLANET || nb.type == CTYPE_MOON) && spawn_draft_.planet_look > 0) {
        float mass_earth = nb.mass / std::max(EARTH_MASS_SOLAR, 1.0e-12f);
        bool force_gas = (spawn_draft_.planet_look == 5 && nb.type == CTYPE_PLANET);
        bool likely_gas = mass_earth > 10.0f;
        if (force_gas) {
            nb.forced_surface = 4;
            nb.custom_material = true;
            nb.custom_hydrogen = std::max(nb.custom_hydrogen, 0.70f);
        } else if (!likely_gas) {
            switch (spawn_draft_.planet_look) {
            case 1: nb.forced_surface = 0; break;
            case 2: nb.forced_surface = 1; break;
            case 3: nb.forced_surface = 2; break;
            case 4: nb.forced_surface = 3; break;
            default: nb.forced_surface = -1; break;
            }
        }
        nb.props_valid = false;
        nb.visuals_valid = false;
    }
    if (body_can_host_rings(nb) && spawn_draft_.spawn_rings && nb.ring_density <= 0.001f) {
        set_ring_system(nb, nb.radius * 1.5f, nb.radius * 3.0f, 0.32f, 0.55f, 0.12f);
    } else if (!body_can_host_rings(nb)) {
        clear_ring_system(nb);
    }

    refresh_body_render_state(nb, &state);
    preview_body_valid_ = true;
    preview_last_type_ = spawn_type;
    preview_last_mass_ = spawn_mass;
    preview_last_draft_hash_ = draft_settings_hash();
}

void CosmosApp::reroll_spawn_preview() {
    preview_reroll_counter_++;
    preview_body_valid_ = false;  // triggers rebuild next frame
}

int CosmosApp::spawn_preview_body(glm::vec3 pos) {
    if (!preview_body_valid_)
        build_preview_body();

    // Nebulae still need to spawn as a cloud
    if (spawn_type == CTYPE_NEBULA) {
        float cloud_r = std::max(35.0f, std::cbrt(spawn_mass) * 120.0f);
        if (spawn_draft_.override_radius)
            cloud_r = std::max(10.0f, spawn_draft_.radius);
        glm::vec3 vel(0.0f);
        if (spawn_draft_.override_velocity)
            vel = spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;
        int idx = spawn_nebula_cloud(pos, vel, spawn_mass, cloud_r, preview_body_.seed);
        reroll_spawn_preview();
        return idx;
    }

    CelestialBody nb = preview_body_;
    nb.pos = pos;
    nb.vel = glm::vec3(0.0f);

    // Orbit insertion
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
    if (spawn_draft_.override_velocity)
        nb.vel += spawn_draft_.velocity_kms / SIM_UNIT_TO_KM;

    nb.name = generate_body_name(nb.seed, nb.type);
    nb.non_attracting = (nb.type == CTYPE_DUST) ? cfg.dust_debug_non_attracting : nb.non_attracting;
    refresh_body_render_state(nb, &state);
    state.bodies.push_back(nb);
    state.trails.emplace_back();

    int host_idx = (int)state.bodies.size() - 1;
    const auto spawned = state.bodies[(size_t)host_idx];
    int ring_style = std::clamp(spawn_draft_.ring_layout_type, 0, 6);
    if (cfg.planetary_rings && spawned.ring_density > 0.001f &&
        (spawned.type == CTYPE_PLANET || spawned.type == CTYPE_MOON)) {
        float annulus = std::max(spawned.ring_outer_radius * spawned.ring_outer_radius -
                                 spawned.ring_inner_radius * spawned.ring_inner_radius, 1.0f);
        float mass_hint = std::max(spawned.mass * spawned.ring_density * 0.00012f *
                                   (annulus / std::max(spawned.radius * spawned.radius, 1.0e-5f)),
                                   std::max(cfg.min_fragment_mass, 1.0e-12f) * 48.0f);
        spawn_dust_ring(host_idx, mass_hint, spawned.ring_inner_radius, spawned.ring_outer_radius,
                        spawned.ring_density, spawned.ring_ice_fraction, spawned.seed ^ 0xD05751EDu,
                        ring_style);
        if (host_idx >= 0 && host_idx < (int)state.bodies.size())
            clear_ring_system(state.bodies[(size_t)host_idx]);
    }
    if (spawn_draft_.spawn_moons && host_idx >= 0 && host_idx < (int)state.bodies.size()) {
        spawn_moons_for_host(host_idx,
                             std::clamp(spawn_draft_.moon_count, 1, 100),
                             std::clamp(spawn_draft_.moon_orbit_layout, 0, 4),
                             std::clamp(spawn_draft_.moon_inclination_deg, 0.0f, 85.0f),
                             std::clamp(spawn_draft_.moon_spacing_scale, 0.35f, 4.0f));
    }

    if (diagnostics_enabled_)
        validate_body_state("spawn/preview", true);

    // Re-roll preview for next spawn
    reroll_spawn_preview();

    return host_idx;
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
        try {
            int substeps = std::clamp(cfg.physics_substeps, 1, 16);
            float step_dt = dt / (float)substeps;
            for (int substep = 0; substep < substeps; ++substep)
                step_physics(step_dt);
            sim_time_ += dt;
        } catch (const std::exception& e) {
            debug_logf("step_physics exception: %s", e.what());
            paused = true;
        } catch (...) {
            debug_logf("step_physics exception: unknown");
            paused = true;
        }
    }

    // ── Spawn preview ghost body ────────────────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    raytracer_.preview.body = nullptr;
    if (spawn_menu_visible_ && !show_splash && !show_pause_menu &&
        !mouse_dragging && !mouse_panning && !io.WantCaptureMouse) {
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        spawn_preview_pos_ = screen_to_spawn_pos(last_mouse_x, last_mouse_y, fb_w, fb_h);

        // Only show preview when no body is under cursor (click would select, not spawn)
        int hover_body = pick_body((float)last_mouse_x, (float)last_mouse_y,
                                    (float)fb_w, (float)fb_h);
        if (hover_body < 0) {
            // Rebuild preview body when spawn type, mass, or draft settings change
            uint32_t dh = draft_settings_hash();
            if (!preview_body_valid_ || preview_last_type_ != spawn_type ||
                preview_last_mass_ != spawn_mass || preview_last_draft_hash_ != dh) {
                build_preview_body();
            }
            // Update position to follow cursor
            preview_body_.pos = spawn_preview_pos_;
            raytracer_.preview.body = &preview_body_;
        }
    }

    // GPU raytraced scene (draws within the active render pass)
    try {
        raytracer_.update_and_draw(vk, renderer.current_cmd(), state, camera, cfg,
                                   io.DisplaySize.x, io.DisplaySize.y, sim_time_);
    } catch (const std::exception& e) {
        debug_logf("raytracer exception: %s", e.what());
        paused = true;
    } catch (...) {
        debug_logf("raytracer exception: unknown");
        paused = true;
    }

    // DrawList overlays (trails, selection, focus indicator)
    if (!show_splash && !show_pause_menu)
        render_overlay();

    // ImGui UI panels
    render_ui();

    renderer.end_frame(vk);
}

// ── Physics (CPU N-body, 3D) ────────────────────────────────────────────────

void CosmosApp::debug_logf(const char* fmt, ...) const {
    if (!fmt) return;
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);

    auto now = std::chrono::system_clock::now();
    std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &raw_time);
#else
    localtime_r(&raw_time, &local_tm);
#endif
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);

    char msg_buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    auto append_log = [&](const char* path) {
        if (!path) return;
        FILE* fp = std::fopen(path, "a");
        if (!fp) return;
        std::fprintf(fp, "[%s][step=%llu] %s\n", time_buf,
                     (unsigned long long)diagnostics_step_counter_, msg_buf);
        std::fflush(fp);
        std::fclose(fp);
    };

    append_log("cosmos_debug.log");
#if !defined(_WIN32)
    append_log("/tmp/cosmos_debug.log");
#endif
}

bool CosmosApp::validate_body_state(const char* context, bool pause_on_invalid) {
    if (!diagnostics_enabled_) return true;

    const char* ctx = context ? context : "unknown";
    auto finite_vec3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };

    if (!std::isfinite(sim_time_)) {
        debug_logf("Invalid sim_time detected in %s; resetting to 0", ctx);
        sim_time_ = 0.0f;
    }
    if (!std::isfinite(cfg.sim_time_accumulated)) {
        debug_logf("Invalid sim_time_accumulated detected in %s; resetting to 0", ctx);
        cfg.sim_time_accumulated = 0.0;
    }

    size_t parent_fixes = 0;
    size_t invalid_count = 0;
    int sample_logs = 0;
    const size_t body_count_before = state.bodies.size();

    for (size_t i = 0; i < state.bodies.size(); ++i) {
        auto& b = state.bodies[i];
        if (b.marked_for_removal) continue;

        bool invalid = false;
        std::string reasons;
        auto add_reason = [&](const char* tag) {
            if (!reasons.empty()) reasons += ",";
            reasons += tag;
        };

        if (!finite_vec3(b.pos)) { invalid = true; add_reason("pos"); }
        if (!finite_vec3(b.vel)) { invalid = true; add_reason("vel"); }
        if (!std::isfinite(b.mass) || b.mass <= 0.0f) { invalid = true; add_reason("mass"); }
        if (!std::isfinite(b.radius) || b.radius <= 0.0f) { invalid = true; add_reason("radius"); }
        if (!std::isfinite(b.temperature)) { invalid = true; add_reason("temperature"); }
        if (!std::isfinite(b.internal_energy)) { invalid = true; add_reason("internal_energy"); }
        if (!std::isfinite(b.angular_vel)) { invalid = true; add_reason("angular_vel"); }
        if (!std::isfinite(b.age)) { invalid = true; add_reason("age"); }

        bool parent_invalid = (b.parent < -1 || b.parent >= (int)state.bodies.size() || b.parent == (int)i);
        if (parent_invalid) {
            b.parent = -1;
            ++parent_fixes;
        }

        if (!invalid) continue;

        b.marked_for_removal = true;
        ++invalid_count;
        if (sample_logs < 12) {
            debug_logf("%s invalid body idx=%zu type=%u mass=%.9g radius=%.9g pos=(%.9g,%.9g,%.9g) "
                       "vel=(%.9g,%.9g,%.9g) reasons=%s",
                       ctx, i, b.type, b.mass, b.radius,
                       b.pos.x, b.pos.y, b.pos.z,
                       b.vel.x, b.vel.y, b.vel.z, reasons.c_str());
            ++sample_logs;
        }
    }

    if (parent_fixes > 0) {
        debug_logf("%s fixed %zu invalid parent links", ctx, parent_fixes);
    }

    if (invalid_count > 0) {
        debug_logf("%s flagged %zu invalid bodies out of %zu; cleaning up",
                   ctx, invalid_count, body_count_before);
        cleanup_bodies();
        cfg.body_count = (uint32_t)state.bodies.size();
        if (pause_on_invalid && diagnostics_pause_on_invalid_) {
            paused = true;
            debug_logf("%s paused simulation after invalid-state cleanup", ctx);
        }
        return false;
    }

    if (state.trails.size() != state.bodies.size()) {
        debug_logf("%s corrected trail/body count mismatch trails=%zu bodies=%zu",
                   ctx, state.trails.size(), state.bodies.size());
        state.trails.resize(state.bodies.size());
    }
    return true;
}

void CosmosApp::step_physics(float dt) {
    ++diagnostics_step_counter_;
    if (diagnostics_enabled_ && !validate_body_state("step_physics/pre", true))
        return;
    if (diagnostics_enabled_ && (diagnostics_step_counter_ % 600ull) == 0ull) {
        debug_logf("heartbeat bodies=%zu fps=%.2f sim_time=%.6g",
                   state.bodies.size(), smoothed_fps_, sim_time_);
    }

    float frame_dt = std::max(dt, 1.0e-4f);
    float instant_fps = 1.0f / frame_dt;
    if (!std::isfinite(smoothed_fps_))
        smoothed_fps_ = instant_fps;
    smoothed_fps_ = smoothed_fps_ * 0.92f + instant_fps * 0.08f;

    cfg.dt_scale = (float)std::pow(10.0, cfg.time_exponent);
    float time_sign = reverse_time_ ? -1.0f : 1.0f;
    float scaled_dt_nominal = dt * cfg.dt_scale * time_sign;
    if (std::abs(dt) > 1.0e-9f)
        displayed_time_rate_ = (double)scaled_dt_nominal / (double)dt;
    else
        displayed_time_rate_ = (double)cfg.dt_scale * (double)time_sign;
    if (!std::isfinite(displayed_time_rate_))
        displayed_time_rate_ = 0.0;
    cfg.integrator_type = std::clamp(cfg.integrator_type,
                                     (int)INTEGRATOR_VELOCITY_VERLET,
                                     (int)INTEGRATOR_PEFRL);
    cfg.velocity_verlet = (cfg.integrator_type == INTEGRATOR_VELOCITY_VERLET);
    if (!adaptive_substep_refining_) {
        adaptive_substeps_last_ = 1;
        adaptive_substeps_required_ = 1;
        adaptive_substeps_saturated_ = false;
    }
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
        ? static_cast<size_t>(std::thread::hardware_concurrency())
        : 1;
    const bool can_parallel = cfg.parallel_gravity && hw_threads > 1;
    const size_t parallel_min_batch = static_cast<size_t>(std::clamp(cfg.parallel_min_batch, 32, 100000));
    auto parallel_for = [&](size_t count, size_t min_parallel, auto&& fn) {
        if (!can_parallel || count < std::max(min_parallel, parallel_min_batch)) {
            fn(0, count);
            return;
        }
        run_parallel_chunks(count, std::min(hw_threads, count), fn);
    };
    if (n == 0) return;
    apply_dust_debug_mode();

    std::vector<glm::vec3> pos0(n, glm::vec3(0.0f));
    std::vector<glm::vec3> vel0(n, glm::vec3(0.0f));
    std::vector<uint8_t> source_active(n, 0u);
    std::vector<float> source_mass(n, 0.0f);
    std::vector<float> source_spin_y(n, 0.0f);
    parallel_for(n, 512, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const auto& b = bodies[i];
            pos0[i] = b.pos;
            vel0[i] = b.vel;
            if (!b.marked_for_removal && !b.non_attracting && b.mass > 0.0f) {
                source_active[i] = 1u;
                source_mass[i] = b.mass;
                float r = std::max(b.radius, 1.0e-4f);
                source_spin_y[i] = b.mass * r * r * b.angular_vel;
            }
        }
    });
    std::vector<size_t> source_indices;
    source_indices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (source_active[i] && source_mass[i] > 0.0f)
            source_indices.push_back(i);
    }

    float c2 = cfg.speed_of_light * cfg.speed_of_light;
    float soft2 = cfg.softening * cfg.softening;
    constexpr float kMinDist = 1.0e-6f;

    auto apply_source_accel = [&](size_t target,
                                  float src_mass,
                                  float src_spin_y,
                                  const glm::vec3& src_pos,
                                  const glm::vec3& tgt_pos,
                                  const glm::vec3& tgt_vel,
                                  glm::vec3& out_accel) {
        if (src_mass <= 0.0f) return;
        glm::vec3 diff = src_pos - tgt_pos;
        float dist2 = glm::dot(diff, diff) + soft2;
        float dist = std::sqrt(dist2);
        if (dist <= kMinDist) return;
        glm::vec3 r_hat = diff / dist;
        float GM = cfg.G * src_mass;
        glm::vec3 acc = r_hat * (GM / dist2);

        if (cfg.gr_enabled && c2 > 0.0f) {
            if (cfg.gr_precession_scale > 0.0f) {
                float v2 = glm::dot(tgt_vel, tgt_vel);
                float vr = glm::dot(tgt_vel, r_hat);
                float pn_scale = GM / (dist * c2) * cfg.gr_precession_scale;
                acc += pn_scale * ((4.0f * GM / dist - v2) * r_hat + 4.0f * vr * tgt_vel);
            }
            if (cfg.gr_time_dilation > 0.0f) {
                float phi = -GM / dist;
                float td = 1.0f + cfg.gr_time_dilation * phi / c2;
                acc *= td;
            }
            if (cfg.gr_frame_dragging > 0.0f && std::abs(src_spin_y) > 1.0e-9f) {
                float coeff = cfg.gr_frame_dragging * cfg.G * src_spin_y /
                              (c2 * dist * dist * dist);
                glm::vec3 J_hat(0.0f, 1.0f, 0.0f);
                glm::vec3 vxJ = glm::cross(tgt_vel, J_hat);
                float rdotJ = glm::dot(r_hat, J_hat);
                glm::vec3 vxr = glm::cross(tgt_vel, r_hat);
                acc += coeff * (vxJ - 3.0f * rdotJ * vxr);
            }
        }

        out_accel += acc;
    };

    auto compute_accel_direct = [&](const std::vector<glm::vec3>& pos,
                                    const std::vector<glm::vec3>& vel,
                                    std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        if (source_indices.empty())
            return;
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (bodies[i].marked_for_removal)
                    continue;
                glm::vec3 ai(0.0f);
                for (size_t src : source_indices) {
                    if (src == i) continue;
                    apply_source_accel(i, source_mass[src], source_spin_y[src],
                                       pos[src], pos[i], vel[i], ai);
                }
                out_accel[i] = ai;
            }
        });
    };

    struct BHNode {
        glm::vec3 center{0.0f};
        float half = 0.0f;
        std::array<int, 8> child{{-1, -1, -1, -1, -1, -1, -1, -1}};
        std::array<size_t, 4> leaf{};
        int leaf_count = 0;
        bool is_leaf = true;
        float mass = 0.0f;
        glm::vec3 com{0.0f};
        float spin_y = 0.0f;
    };

    auto build_barnes_hut_tree = [&](const std::vector<glm::vec3>& pos,
                                     std::vector<BHNode>& nodes,
                                     int& root_out) -> bool {
        if (source_indices.empty()) {
            root_out = -1;
            return false;
        }

        glm::vec3 bmin(std::numeric_limits<float>::max());
        glm::vec3 bmax(std::numeric_limits<float>::lowest());
        for (size_t idx : source_indices) {
            bmin = glm::min(bmin, pos[idx]);
            bmax = glm::max(bmax, pos[idx]);
        }
        glm::vec3 span = bmax - bmin;
        float half = std::max(std::max(span.x, span.y), span.z) * 0.5f;
        half = std::max(half + std::max(cfg.softening * 2.0f, 1.0f), 1.0e-3f);
        glm::vec3 root_center = (bmin + bmax) * 0.5f;

        nodes.clear();
        nodes.reserve(source_indices.size() * 2 + 8);
        auto make_node = [&](const glm::vec3& center, float node_half) -> int {
            nodes.push_back(BHNode{});
            BHNode& node = nodes.back();
            node.center = center;
            node.half = node_half;
            return (int)nodes.size() - 1;
        };
        auto child_index_for = [](const glm::vec3& p, const glm::vec3& c) {
            int idx = 0;
            if (p.x >= c.x) idx |= 1;
            if (p.y >= c.y) idx |= 2;
            if (p.z >= c.z) idx |= 4;
            return idx;
        };
        auto child_center_for = [](const glm::vec3& c, float parent_half, int oct) {
            float h = parent_half * 0.5f;
            return glm::vec3(
                c.x + ((oct & 1) ? h : -h),
                c.y + ((oct & 2) ? h : -h),
                c.z + ((oct & 4) ? h : -h));
        };
        auto ensure_child = [&](int parent_idx, int oct) -> int {
            int child_idx = nodes[(size_t)parent_idx].child[(size_t)oct];
            if (child_idx >= 0)
                return child_idx;
            glm::vec3 cc = child_center_for(nodes[(size_t)parent_idx].center,
                                            nodes[(size_t)parent_idx].half, oct);
            child_idx = make_node(cc, nodes[(size_t)parent_idx].half * 0.5f);
            nodes[(size_t)parent_idx].child[(size_t)oct] = child_idx;
            return child_idx;
        };

        constexpr int kLeafCap = 4;
        constexpr int kMaxDepth = 20;
        constexpr float kMinHalf = 1.0e-4f;
        root_out = make_node(root_center, half);
        auto insert_body = [&](auto&& self, int node_idx, size_t body_idx, int depth) -> void {
            if (node_idx < 0) return;
            BHNode& node = nodes[(size_t)node_idx];
            if (node.is_leaf) {
                if (node.leaf_count < kLeafCap || depth >= kMaxDepth || node.half <= kMinHalf) {
                    node.leaf[(size_t)node.leaf_count++] = body_idx;
                    return;
                }
                std::array<size_t, kLeafCap> reinserts{};
                int rein_count = node.leaf_count;
                for (int i = 0; i < rein_count; ++i)
                    reinserts[(size_t)i] = node.leaf[(size_t)i];
                node.leaf_count = 0;
                node.is_leaf = false;
                for (int i = 0; i < rein_count; ++i) {
                    int oct = child_index_for(pos[reinserts[(size_t)i]], nodes[(size_t)node_idx].center);
                    int child_idx = ensure_child(node_idx, oct);
                    self(self, child_idx, reinserts[(size_t)i], depth + 1);
                }
            }
            int oct = child_index_for(pos[body_idx], nodes[(size_t)node_idx].center);
            int child_idx = ensure_child(node_idx, oct);
            self(self, child_idx, body_idx, depth + 1);
        };
        for (size_t idx : source_indices)
            insert_body(insert_body, root_out, idx, 0);

        auto finalize_mass = [&](auto&& self, int node_idx) -> void {
            BHNode& node = nodes[(size_t)node_idx];
            node.mass = 0.0f;
            node.com = glm::vec3(0.0f);
            node.spin_y = 0.0f;
            if (node.is_leaf) {
                for (int i = 0; i < node.leaf_count; ++i) {
                    size_t bidx = node.leaf[(size_t)i];
                    float m = std::max(source_mass[bidx], 0.0f);
                    if (m <= 0.0f) continue;
                    node.mass += m;
                    node.com += pos[bidx] * m;
                    node.spin_y += source_spin_y[bidx];
                }
            } else {
                for (int c = 0; c < 8; ++c) {
                    int child_idx = node.child[(size_t)c];
                    if (child_idx < 0) continue;
                    self(self, child_idx);
                    const BHNode& child = nodes[(size_t)child_idx];
                    if (child.mass <= 0.0f) continue;
                    node.mass += child.mass;
                    node.com += child.com * child.mass;
                    node.spin_y += child.spin_y;
                }
            }
            if (node.mass > 0.0f)
                node.com /= node.mass;
            else
                node.com = node.center;
        };
        finalize_mass(finalize_mass, root_out);
        return true;
    };

    auto compute_accel_barnes_hut = [&](const std::vector<glm::vec3>& pos,
                                        const std::vector<glm::vec3>& vel,
                                        std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        std::vector<BHNode> nodes;
        int root = -1;
        if (!build_barnes_hut_tree(pos, nodes, root))
            return;

        float theta = std::clamp(cfg.barnes_hut_theta, 0.2f, 1.6f);
        auto traverse = [&](auto&& self, size_t target, int node_idx) -> void {
            const BHNode& node = nodes[(size_t)node_idx];
            if (node.mass <= 0.0f) return;
            if (node.is_leaf) {
                for (int i = 0; i < node.leaf_count; ++i) {
                    size_t src = node.leaf[(size_t)i];
                    if (src == target) continue;
                    apply_source_accel(target, source_mass[src], source_spin_y[src],
                                       pos[src], pos[target], vel[target], out_accel[target]);
                }
                return;
            }

            bool target_inside =
                std::abs(pos[target].x - node.center.x) <= node.half &&
                std::abs(pos[target].y - node.center.y) <= node.half &&
                std::abs(pos[target].z - node.center.z) <= node.half;
            glm::vec3 delta = node.com - pos[target];
            float dist2 = glm::dot(delta, delta) + soft2;
            float dist = std::sqrt(dist2);
            float size = node.half * 2.0f;
            if (!target_inside && dist > kMinDist && (size / dist) < theta) {
                apply_source_accel(target, node.mass, node.spin_y,
                                   node.com, pos[target], vel[target], out_accel[target]);
                return;
            }
            for (int c = 0; c < 8; ++c) {
                int child_idx = node.child[(size_t)c];
                if (child_idx >= 0)
                    self(self, target, child_idx);
            }
        };

        constexpr size_t kParallelBhTraverseThreshold = 256;
        if (can_parallel && n >= kParallelBhTraverseThreshold) {
            run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    traverse(traverse, i, root);
                }
            });
        } else {
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                traverse(traverse, i, root);
            }
        }
    };

    auto compute_accel_barnes_hut_gpu = [&](const std::vector<glm::vec3>& pos,
                                            const std::vector<glm::vec3>& vel,
                                            std::vector<glm::vec3>& out_accel) {
        std::fill(out_accel.begin(), out_accel.end(), glm::vec3(0.0f));
        std::vector<BHNode> nodes;
        int root = -1;
        if (!build_barnes_hut_tree(pos, nodes, root))
            return;
        if (root != 0 || nodes.empty()) {
            compute_accel_barnes_hut(pos, vel, out_accel);
            return;
        }

        std::vector<CosmosBhGpuNode> gpu_nodes(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            const BHNode& node = nodes[i];
            CosmosBhGpuNode gn{};
            gn.center_half = glm::vec4(node.center, node.half);
            gn.com_mass = glm::vec4(node.com, node.mass);
            gn.spin_leaf = glm::vec4(node.spin_y, (float)node.leaf_count, node.is_leaf ? 1.0f : 0.0f, 0.0f);
            gn.child0 = glm::ivec4(node.child[0], node.child[1], node.child[2], node.child[3]);
            gn.child1 = glm::ivec4(node.child[4], node.child[5], node.child[6], node.child[7]);
            gn.leaf = glm::ivec4(-1);
            for (int li = 0; li < node.leaf_count && li < 4; ++li)
                gn.leaf[li] = (int)node.leaf[(size_t)li];
            gpu_nodes[i] = gn;
        }

        std::vector<glm::vec4> gpu_sources(n, glm::vec4(0.0f));
        parallel_for(n, 256, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                gpu_sources[i] = glm::vec4(
                    source_mass[i],
                    source_spin_y[i],
                    source_active[i] ? 1.0f : 0.0f,
                    bodies[i].marked_for_removal ? 1.0f : 0.0f);
            }
        });

        float theta = std::clamp(cfg.barnes_hut_theta, 0.2f, 1.6f);
        if (!gravity_compute_.compute_barnes_hut(vk, pos, vel, gpu_sources, gpu_nodes, theta, cfg, out_accel))
            compute_accel_barnes_hut(pos, vel, out_accel);
    };

    auto compute_accel = [&](const std::vector<glm::vec3>& pos,
                             const std::vector<glm::vec3>& vel,
                             std::vector<glm::vec3>& out_accel) {
        bool use_bh = cfg.barnes_hut && (int)n >= std::max(cfg.barnes_hut_min_bodies, 16);
        if (use_bh && cfg.gpu_barnes_hut)
            compute_accel_barnes_hut_gpu(pos, vel, out_accel);
        else if (use_bh)
            compute_accel_barnes_hut(pos, vel, out_accel);
        else
            compute_accel_direct(pos, vel, out_accel);
    };

    auto integrate_kinematics = [&](float step_scaled_dt,
                                    const std::vector<glm::vec3>& in_pos,
                                    const std::vector<glm::vec3>& in_vel,
                                    const std::vector<glm::vec3>* initial_accel,
                                    std::vector<glm::vec3>& out_pos,
                                    std::vector<glm::vec3>& out_vel) {
        out_pos.assign(n, glm::vec3(0.0f));
        out_vel.assign(n, glm::vec3(0.0f));
        if (n == 0)
            return;

        auto ensure_accel = [&](std::vector<glm::vec3>& accel) {
            if (initial_accel) accel = *initial_accel;
            else compute_accel(in_pos, in_vel, accel);
        };

        switch (cfg.integrator_type) {
        case INTEGRATOR_EULER_EXPLICIT: {
            std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
            ensure_accel(accel);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        out_pos[i] = in_pos[i];
                        out_vel[i] = in_vel[i];
                        continue;
                    }
                    out_pos[i] = in_pos[i] + in_vel[i] * step_scaled_dt;
                    out_vel[i] = in_vel[i] + accel[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_EULER_SEMI_IMPLICIT: {
            std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
            ensure_accel(accel);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        out_pos[i] = in_pos[i];
                        out_vel[i] = in_vel[i];
                        continue;
                    }
                    out_vel[i] = in_vel[i] + accel[i] * step_scaled_dt;
                    out_pos[i] = in_pos[i] + out_vel[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_RK2: {
            std::vector<glm::vec3> accel0_local(n, glm::vec3(0.0f));
            ensure_accel(accel0_local);
            std::vector<glm::vec3> pos_mid(n, glm::vec3(0.0f));
            std::vector<glm::vec3> vel_mid(n, glm::vec3(0.0f));
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        pos_mid[i] = in_pos[i];
                        vel_mid[i] = in_vel[i];
                        continue;
                    }
                    pos_mid[i] = in_pos[i] + in_vel[i] * (step_scaled_dt * 0.5f);
                    vel_mid[i] = in_vel[i] + accel0_local[i] * (step_scaled_dt * 0.5f);
                }
            });
            std::vector<glm::vec3> accel_mid(n, glm::vec3(0.0f));
            compute_accel(pos_mid, vel_mid, accel_mid);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        out_pos[i] = in_pos[i];
                        out_vel[i] = in_vel[i];
                        continue;
                    }
                    out_pos[i] = in_pos[i] + vel_mid[i] * step_scaled_dt;
                    out_vel[i] = in_vel[i] + accel_mid[i] * step_scaled_dt;
                }
            });
            break;
        }
        case INTEGRATOR_FOREST_RUTH: {
            std::vector<glm::vec3> pos = in_pos;
            std::vector<glm::vec3> vel = in_vel;
            std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
            constexpr float theta = 1.0f / (2.0f - 1.2599210498948732f);
            constexpr float c1 = theta * 0.5f;
            constexpr float c2 = (1.0f - theta) * 0.5f;
            constexpr float c3 = c2;
            constexpr float c4 = c1;
            constexpr float d1 = theta;
            constexpr float d2 = 1.0f - 2.0f * theta;
            constexpr float d3 = theta;
            auto drift = [&](float coeff) {
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal) continue;
                        pos[i] += vel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            auto kick = [&](float coeff) {
                compute_accel(pos, vel, accel);
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal) continue;
                        vel[i] += accel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            drift(c1); kick(d1);
            drift(c2); kick(d2);
            drift(c3); kick(d3);
            drift(c4);
            out_pos = std::move(pos);
            out_vel = std::move(vel);
            break;
        }
        case INTEGRATOR_PEFRL: {
            std::vector<glm::vec3> pos = in_pos;
            std::vector<glm::vec3> vel = in_vel;
            std::vector<glm::vec3> accel(n, glm::vec3(0.0f));
            constexpr float xi = 0.1786178958448091f;
            constexpr float lambda = -0.2123418310626054f;
            constexpr float chi = -0.06626458266981849f;
            constexpr float c_half = (1.0f - 2.0f * lambda) * 0.5f;
            constexpr float drift_mid = 1.0f - 2.0f * (chi + xi);
            auto drift = [&](float coeff) {
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal) continue;
                        pos[i] += vel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            auto kick = [&](float coeff) {
                compute_accel(pos, vel, accel);
                parallel_for(n, 384, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        if (bodies[i].marked_for_removal) continue;
                        vel[i] += accel[i] * (step_scaled_dt * coeff);
                    }
                });
            };
            drift(xi);        kick(c_half);
            drift(chi);       kick(lambda);
            drift(drift_mid); kick(lambda);
            drift(chi);       kick(c_half);
            drift(xi);
            out_pos = std::move(pos);
            out_vel = std::move(vel);
            break;
        }
        case INTEGRATOR_VELOCITY_VERLET:
        default: {
            std::vector<glm::vec3> accel0_local(n, glm::vec3(0.0f));
            ensure_accel(accel0_local);
            std::vector<glm::vec3> pos1_local(n, glm::vec3(0.0f));
            std::vector<glm::vec3> vel_half_local(n, glm::vec3(0.0f));
            float dt2 = step_scaled_dt * step_scaled_dt;
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        pos1_local[i] = in_pos[i];
                        vel_half_local[i] = in_vel[i];
                        continue;
                    }
                    pos1_local[i] = in_pos[i] + in_vel[i] * step_scaled_dt + 0.5f * accel0_local[i] * dt2;
                    vel_half_local[i] = in_vel[i] + 0.5f * accel0_local[i] * step_scaled_dt;
                }
            });
            std::vector<glm::vec3> accel1(n, glm::vec3(0.0f));
            compute_accel(pos1_local, vel_half_local, accel1);
            parallel_for(n, 384, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) {
                        out_pos[i] = in_pos[i];
                        out_vel[i] = in_vel[i];
                        continue;
                    }
                    out_pos[i] = pos1_local[i];
                    out_vel[i] = in_vel[i] + 0.5f * (accel0_local[i] + accel1[i]) * step_scaled_dt;
                }
            });
            break;
        }
        }

        parallel_for(n, 384, [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (bodies[i].marked_for_removal) continue;
                out_vel[i] *= cfg.damping;
            }
        });
    };

    std::vector<glm::vec3> accel0(n, glm::vec3(0.0f));
    compute_accel(pos0, vel0, accel0);

    float scaled_dt = scaled_dt_nominal;
    if (cfg.adaptive_time_step) {
        float max_speed = 0.0f;
        float max_acc = 0.0f;
        if (can_parallel && n >= 512) {
            std::mutex reduce_mutex;
            run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
                float local_max_speed = 0.0f;
                float local_max_acc = 0.0f;
                for (size_t i = begin; i < end; ++i) {
                    if (bodies[i].marked_for_removal) continue;
                    local_max_speed = std::max(local_max_speed, glm::length(vel0[i]));
                    local_max_acc = std::max(local_max_acc, glm::length(accel0[i]));
                }
                std::lock_guard<std::mutex> lock(reduce_mutex);
                max_speed = std::max(max_speed, local_max_speed);
                max_acc = std::max(max_acc, local_max_acc);
            });
        } else {
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                max_speed = std::max(max_speed, glm::length(vel0[i]));
                max_acc = std::max(max_acc, glm::length(accel0[i]));
            }
        }

        float abs_nominal = std::abs(scaled_dt_nominal);
        float safety = std::clamp(cfg.adaptive_step_safety, 0.01f, 1.0f);
        float char_len = std::max(cfg.softening, 0.25f);
        float dt_vel = (max_speed > 1.0e-6f) ? (safety * char_len / max_speed) : abs_nominal;
        float dt_acc = (max_acc > 1.0e-9f) ? (safety * std::sqrt(char_len / max_acc)) : abs_nominal;
        float dt_min = std::max(cfg.adaptive_step_min, 1.0e-6f);
        float dt_max = std::max(cfg.adaptive_step_max, dt_min);
        float dt_limit = std::clamp(std::min(dt_vel, dt_acc), dt_min, dt_max);
        scaled_dt = time_sign * std::min(abs_nominal, dt_limit);
    }
    if (!std::isfinite(scaled_dt))
        scaled_dt = 0.0f;
    if (cfg.adaptive_substepping && !adaptive_substep_refining_ &&
        n > 0 && std::abs(scaled_dt) > 1.0e-9f) {
        auto estimate_segment_error = [&](float segment_scaled_dt) {
            if (std::abs(segment_scaled_dt) <= 1.0e-9f)
                return 0.0f;
            std::vector<glm::vec3> full_pos, full_vel;
            std::vector<glm::vec3> half_pos, half_vel;
            std::vector<glm::vec3> half2_pos, half2_vel;
            integrate_kinematics(segment_scaled_dt, pos0, vel0, &accel0, full_pos, full_vel);
            integrate_kinematics(segment_scaled_dt * 0.5f, pos0, vel0, &accel0, half_pos, half_vel);
            integrate_kinematics(segment_scaled_dt * 0.5f, half_pos, half_vel, nullptr, half2_pos, half2_vel);
            float max_error = 0.0f;
            for (size_t i = 0; i < n; ++i) {
                if (bodies[i].marked_for_removal) continue;
                float err = glm::length(full_pos[i] - half2_pos[i]);
                if (std::isfinite(err))
                    max_error = std::max(max_error, err);
            }
            return max_error;
        };

        float tolerance = std::clamp(cfg.adaptive_substep_tolerance, 1.0e-6f, 1.0e6f);
        int required_substeps = 1;
        constexpr int kSearchCap = 4096;
        float segment_error = estimate_segment_error(scaled_dt);
        while (segment_error > tolerance && required_substeps < kSearchCap) {
            required_substeps *= 2;
            segment_error = estimate_segment_error(scaled_dt / (float)required_substeps);
        }

        adaptive_substeps_required_ = std::max(required_substeps, 1);
        adaptive_substeps_last_ = std::min(adaptive_substeps_required_,
                                           std::max(cfg.adaptive_substep_max, 1));
        adaptive_substeps_saturated_ = adaptive_substeps_last_ < adaptive_substeps_required_;

        if (adaptive_substeps_required_ > 1 || adaptive_substeps_saturated_) {
            float refined_scaled_dt = scaled_dt / (float)adaptive_substeps_required_;
            float denom = cfg.dt_scale * time_sign;
            float refined_real_dt = (std::abs(denom) > 1.0e-9f)
                ? (refined_scaled_dt / denom)
                : (dt / (float)adaptive_substeps_required_);
            float applied_scaled_total = refined_scaled_dt * (float)adaptive_substeps_last_;
            adaptive_substep_refining_ = true;
            for (int substep = 0; substep < adaptive_substeps_last_; ++substep)
                step_physics(refined_real_dt);
            adaptive_substep_refining_ = false;
            displayed_time_rate_ = (std::abs(dt) > 1.0e-9f)
                ? (double)applied_scaled_total / (double)dt
                : 0.0;
            if (!std::isfinite(displayed_time_rate_))
                displayed_time_rate_ = 0.0;
            return;
        }
    }
    if (std::abs(dt) > 1.0e-9f)
        displayed_time_rate_ = (double)scaled_dt / (double)dt;
    if (!std::isfinite(displayed_time_rate_))
        displayed_time_rate_ = 0.0;
    cfg.sim_time_accumulated += (double)scaled_dt;

    std::vector<glm::vec3> pos1, vel1;
    integrate_kinematics(scaled_dt, pos0, vel0, &accel0, pos1, vel1);
    parallel_for(n, 384, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (bodies[i].marked_for_removal) continue;
            bodies[i].mass_loss_rate = 0.0f;
            bodies[i].vel = vel1[i];
            bodies[i].pos = pos1[i];
            bodies[i].age += scaled_dt;
        }
    });

    // Physics subsystems
    if (cfg.roche_limit || cfg.tidal_forces) process_roche_limit(scaled_dt);
    if (cfg.collisions)         process_collisions(scaled_dt);
    if (cfg.spin_fragmentation) process_spin_fragmentation(scaled_dt);
    if (cfg.temperature_system) process_temperature(scaled_dt);
    if (cfg.temperature_system || cfg.evaporation) process_space_weather(scaled_dt);
    if (cfg.material_phases)    process_material_phases(scaled_dt);
    if (cfg.evaporation)        process_evaporation(scaled_dt);
    if (cfg.stellar_evolution)  process_stellar_evolution(scaled_dt);
    if (cfg.tidal_locking)      process_tidal_locking(scaled_dt);
    if (cfg.hawking_radiation)  process_hawking_radiation(scaled_dt);
    process_orbital_elements();
    cleanup_bodies();

    n = bodies.size();
    parallel_for(n, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            enforce_body_physical_limits(bodies[i]);
    });

    // Refresh cached planet properties in parallel (independent per body).
    parallel_for(n, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            refresh_planet_props(bodies[i]);
    });
    // Visual refresh reads broader system context, so keep it ordered.
    for (auto& b : state.bodies)
        refresh_body_visuals(b, &state);

    update_body_tracking_cache();

    // Update trails
    n = bodies.size();
    while (state.trails.size() < n)
        state.trails.emplace_back();
    parallel_for(n, 256, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++) {
            state.trails[i].push_back(bodies[i].pos);
            while (state.trails[i].size() > cfg.trail_length)
                state.trails[i].pop_front();
        }
    });

    if (diagnostics_enabled_)
        validate_body_state("step_physics/post", true);
}

int CosmosApp::dominant_primary_for(int body_index) const {
    if (body_index < 0 || body_index >= (int)state.bodies.size())
        return -1;
    const auto& body = state.bodies[(size_t)body_index];
    if (body.marked_for_removal) return -1;

    if (body.parent >= 0 && body.parent < (int)state.bodies.size() && body.parent != body_index) {
        const auto& parent = state.bodies[(size_t)body.parent];
        if (!parent.marked_for_removal && parent.mass > body.mass * 0.25f)
            return body.parent;
    }

    int best = -1;
    double best_score = 0.0;
    for (int j = 0; j < (int)state.bodies.size(); ++j) {
        if (j == body_index) continue;
        const auto& cand = state.bodies[(size_t)j];
        if (cand.marked_for_removal) continue;
        if (cand.non_attracting) continue;
        if (cand.mass <= body.mass * 1.01f && !is_star_type(cand.type) && !is_black_hole_type(cand.type))
            continue;
        glm::dvec3 d = glm::dvec3(cand.pos) - glm::dvec3(body.pos);
        double d2 = glm::dot(d, d);
        if (d2 <= 1.0e-8) continue;
        double score = (double)cand.mass / d2;
        if (score > best_score) {
            best_score = score;
            best = j;
        }
    }
    return best;
}

void CosmosApp::update_body_tracking_cache() {
    const size_t n = state.bodies.size();
    tracked_primary_.assign(n, -1);
    tracked_children_count_.assign(n, 0);
    tracked_eccentricity_.assign(n, -1.0f);
    const size_t hw_threads = std::thread::hardware_concurrency() > 0
        ? static_cast<size_t>(std::thread::hardware_concurrency())
        : 1;
    const bool can_parallel = cfg.parallel_gravity && hw_threads > 1 && n >= 256;

    if (can_parallel) {
        run_parallel_chunks(n, std::min(hw_threads, n), [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                if (state.bodies[i].marked_for_removal) continue;
                tracked_primary_[i] = dominant_primary_for((int)i);
            }
        });
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (state.bodies[i].marked_for_removal) continue;
            tracked_primary_[i] = dominant_primary_for((int)i);
        }
    }

    for (size_t i = 0; i < n; ++i) {
        int primary = tracked_primary_[i];
        if (primary >= 0 && primary < (int)n)
            tracked_children_count_[(size_t)primary]++;
    }

    auto calc_ecc_range = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            int pidx = tracked_primary_[i];
            if (pidx < 0 || pidx >= (int)n) continue;
            const auto& b = state.bodies[i];
            const auto& p = state.bodies[(size_t)pidx];
            if (b.marked_for_removal || p.marked_for_removal) continue;

            glm::dvec3 r = glm::dvec3(b.pos) - glm::dvec3(p.pos);
            glm::dvec3 v = glm::dvec3(b.vel) - glm::dvec3(p.vel);
            double rmag = glm::length(r);
            if (rmag <= 1.0e-8) continue;
            double mu = (double)cfg.G * std::max((double)b.mass + (double)p.mass, 1.0e-8);
            if (mu <= 0.0) continue;
            glm::dvec3 h = glm::cross(r, v);
            glm::dvec3 evec = glm::cross(v, h) / mu - r / rmag;
            tracked_eccentricity_[i] = (float)glm::length(evec);
        }
    };
    if (can_parallel)
        run_parallel_chunks(n, std::min(hw_threads, n), calc_ecc_range);
    else
        calc_ecc_range(0, n);
}

glm::vec3 CosmosApp::verlet_auto_orbit_velocity(const CelestialBody& body, const CelestialBody& primary,
                                                float radial_scale, float tangential_scale) const {
    glm::vec3 rel = body.pos - primary.pos;
    float r0 = glm::length(rel);
    if (!std::isfinite(r0) || r0 < 1.0e-5f) return body.vel;
    glm::vec3 r_hat = rel / r0;

    glm::vec3 rel_v_now = body.vel - primary.vel;
    glm::vec3 tangent = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(rel_v_now, r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), r_hat);
    if (glm::dot(tangent, tangent) < 1.0e-8f)
        return body.vel;
    tangent = glm::normalize(tangent);
    if (glm::dot(rel_v_now, tangent) < 0.0f) tangent *= -1.0f;

    float mu = cfg.G * std::max(primary.mass + body.mass, 1.0e-8f);
    float v_circ = std::sqrt(std::max(mu / std::max(r0, 1.0e-6f), 0.0f));
    float v_rad = glm::dot(rel_v_now, r_hat) * radial_scale;
    float vt_center = std::max(v_circ * tangential_scale, 1.0e-6f);

    float orbital_period = (2.0f * 3.14159265359f * r0) / std::max(v_circ, 1.0e-4f);
    // Guard large-radius edge cases (e.g. Oort cloud distances) against float->int overflow.
    double raw_steps = (double)orbital_period * 0.18 / 0.01;
    if (!std::isfinite(raw_steps))
        raw_steps = 180.0;
    raw_steps = std::clamp(raw_steps, 48.0, 180.0);
    int steps = (int)std::lround(raw_steps);
    float dt_sim = std::clamp(orbital_period * 0.18f / (float)steps, 0.001f, 0.025f);

    auto accel = [&](const glm::vec3& r) {
        float rmag = glm::length(r);
        if (rmag < 1.0e-6f) return glm::vec3(0.0f);
        return -r * (mu / (rmag * rmag * rmag));
    };

    auto score_candidate = [&](float v_tan) {
        glm::vec3 r = rel;
        glm::vec3 v = r_hat * v_rad + tangent * v_tan;
        float score = 0.0f;
        for (int s = 0; s < steps; ++s) {
            glm::vec3 a0 = accel(r);
            glm::vec3 r1 = r + v * dt_sim + 0.5f * a0 * dt_sim * dt_sim;
            glm::vec3 a1 = accel(r1);
            glm::vec3 v1 = v + 0.5f * (a0 + a1) * dt_sim;

            float rr = glm::length(r1);
            if (!std::isfinite(rr) || rr < 1.0e-6f) return 1.0e9f;
            float radial_v = glm::dot(v1, r1 / rr);
            score += std::abs(rr - r0) / std::max(r0, 1.0e-6f);
            score += std::abs(radial_v) / std::max(v_circ, 1.0e-4f) * 0.40f;

            r = r1;
            v = v1;
        }
        score /= (float)steps;
        score += std::abs(v_tan - vt_center) / std::max(v_circ, 1.0e-4f) * 0.10f;
        return score;
    };

    float best_vt = vt_center;
    float best_score = score_candidate(best_vt);
    float lo = vt_center * 0.70f;
    float hi = vt_center * 1.30f;
    const int scans = 16;
    for (int i = 0; i <= scans; ++i) {
        float t = (float)i / (float)scans;
        float vt = lo + (hi - lo) * t;
        float sc = score_candidate(vt);
        if (sc < best_score) {
            best_score = sc;
            best_vt = vt;
        }
    }

    return primary.vel + r_hat * v_rad + tangent * best_vt;
}

bool CosmosApp::spawn_dust_ring(int host_index, float total_mass, float inner_radius, float outer_radius,
                                float density, float ice_fraction, uint32_t seed_hint, int ring_style) {
    if (!cfg.planetary_rings || host_index < 0 || host_index >= (int)state.bodies.size())
        return false;
    if (total_mass <= 0.0f || !std::isfinite(total_mass))
        return false;

    const CelestialBody host = state.bodies[(size_t)host_index];
    if (host.marked_for_removal || is_star_type(host.type) || is_black_hole_type(host.type))
        return false;

    float ring_mass = total_mass * std::clamp(cfg.ring_mass_scale, 0.1f, 5.0f);
    float density_scaled = density * std::clamp(cfg.ring_density_scale, 0.2f, 3.0f);
    float inner = std::max(inner_radius * std::clamp(cfg.ring_inner_scale, 0.6f, 3.0f), host.radius * 1.15f);
    float outer = std::max(outer_radius * std::clamp(cfg.ring_outer_scale, 0.6f, 3.0f), inner + host.radius * 0.18f);
    int style = std::clamp(ring_style, 0, 6);
    float width = outer - inner;
    if (!std::isfinite(inner) || !std::isfinite(outer) || width <= 1.0e-4f)
        return false;

    float safe_min_mass = std::max(1.0e-13f, std::min(cfg.min_fragment_mass * 0.08f, 1.0e-10f));
    float annulus_span = std::sqrt(std::max(width / std::max(host.radius, 0.1f), 0.1f));
    int count = std::clamp((int)std::round((24.0f + density_scaled * 82.0f + annulus_span * 26.0f +
                                           std::sqrt(ring_mass / std::max(safe_min_mass, 1.0e-13f)) * 0.18f) *
                                           std::clamp(cfg.ring_particle_scale, 0.2f, 5.0f)),
                           24, 420);

    if (cfg.dynamic_budget_enabled) {
        int attract_cap = std::max(cfg.dynamic_max_fragments, 0);
        int non_attract_cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int total_budget_cap = std::max(attract_cap + non_attract_cap + 256, 1);
        count = std::min(count, std::max(0, total_budget_cap - (int)state.bodies.size()));

        // Ring particles are always non_attracting, so check against that cap.
        int cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int current = 0;
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            if (b.non_attracting) ++current;
        }
        count = std::min(count, std::max(0, cap - current));
    }
    // Rings need at least 8 particles to look like a ring, not a single asteroid
    if (count < 8)
        return false;

    count = std::min(count, std::max(8, (int)std::floor(ring_mass / safe_min_mass)));
    if (count < 8)
        return false;

    uint32_t seed = hash_combine(host.seed, seed_hint == 0u ? 0xD057CAFEu : seed_hint);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    float tilt = std::clamp(std::max(host.ring_tilt, 0.02f), 0.02f, 1.30f);
    float yaw = u01(rng) * 6.28318530718f;
    glm::vec3 axis(std::sin(tilt) * std::cos(yaw), std::cos(tilt), std::sin(tilt) * std::sin(yaw));
    if (glm::dot(axis, axis) < 1.0e-8f) axis = glm::vec3(0.0f, 1.0f, 0.0f);
    axis = glm::normalize(axis);
    glm::vec3 basis_u = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(basis_u, basis_u) < 1.0e-8f)
        basis_u = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
    basis_u = glm::normalize(basis_u);
    glm::vec3 basis_v = glm::normalize(glm::cross(axis, basis_u));

    std::vector<float> weights((size_t)count, 0.0f);
    float wsum = 0.0f;
    for (int i = 0; i < count; ++i) {
        weights[(size_t)i] = 0.55f + u01(rng);
        wsum += weights[(size_t)i];
    }

    std::vector<float> masses((size_t)count, safe_min_mass);
    float rem_mass = std::max(0.0f, ring_mass - safe_min_mass * (float)count);
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] += rem_mass * (weights[(size_t)i] / std::max(wsum, 1.0e-6f));
    masses.back() += ring_mass - std::accumulate(masses.begin(), masses.end(), 0.0f);

    float style_thickness = 1.0f;
    if (style == 3) style_thickness = 2.1f;      // torus
    if (style == 5) style_thickness = 1.8f;      // exaggerated geometry
    if (style == 6) style_thickness = 0.9f;      // resonance gaps stay relatively thin
    float max_vertical = std::max(host.radius * (0.006f + density_scaled * 0.02f) *
                                  std::clamp(cfg.ring_thickness_scale, 0.3f, 4.0f) * style_thickness, 0.01f);
    float base_temp = std::clamp(host.temperature * (0.14f + (1.0f - ice_fraction) * 0.12f),
                                 30.0f, 900.0f);
    bool spawned_any = false;

    for (int i = 0; i < count; ++i) {
        float theta = u01(rng) * 6.28318530718f;
        float radial_t = std::pow(u01(rng), 0.75f);
        if (style == 0) { // Saturn
            radial_t = std::pow(u01(rng), 0.60f);
        } else if (style == 1) { // Uranus
            radial_t = std::pow(u01(rng), 1.35f);
        } else if (style == 2) { // Neptune
            radial_t = std::pow(u01(rng), 1.85f);
        } else if (style == 3) { // Torus
            radial_t = std::clamp(0.50f + (u01(rng) * 2.0f - 1.0f) * 0.18f, 0.0f, 1.0f);
        } else if (style == 5) { // Unrealistic geometries
            float lobes = 0.5f + 0.5f * std::sin(theta * 3.0f + u01(rng) * 6.28318530718f);
            radial_t = std::clamp(0.20f + lobes * 0.70f + (u01(rng) * 2.0f - 1.0f) * 0.18f, 0.0f, 1.0f);
        } else if (style == 6) { // Resonance gaps
            for (int iter = 0; iter < 8; ++iter) {
                float candidate = std::pow(u01(rng), 0.78f);
                float g0 = std::abs(candidate - 0.22f);
                float g1 = std::abs(candidate - 0.48f);
                float g2 = std::abs(candidate - 0.74f);
                bool near_gap = (g0 < 0.04f) || (g1 < 0.05f) || (g2 < 0.05f);
                if (!near_gap || u01(rng) < 0.10f) {
                    radial_t = candidate;
                    break;
                }
            }
        }
        float radial = inner + width * radial_t;
        float vertical = (u01(rng) * 2.0f - 1.0f) * max_vertical;
        glm::vec3 ring_dir = basis_u * std::cos(theta) + basis_v * std::sin(theta);
        glm::vec3 rel = ring_dir * radial + axis * vertical;

        CelestialBody dust;
        // Ring particles are a mixture of dust, asteroids, and comets.
        float type_roll = u01(rng);
        if (type_roll < 0.50f)
            dust.type = CTYPE_DUST;
        else if (type_roll < 0.80f)
            dust.type = CTYPE_ASTEROID;
        else
            dust.type = CTYPE_COMET;
        dust.parent = host_index;
        dust.mass = std::max(masses[(size_t)i], safe_min_mass);
        float density_factor = 1.3f + (1.0f - ice_fraction) * 1.2f;
        dust.radius = std::max(dust.type == CTYPE_DUST ? 0.07f : 0.15f,
                               std::cbrt(dust.mass / std::max(density_factor, 0.1f)) *
                               (dust.type == CTYPE_DUST ? 11.0f : 6.0f));
        dust.pos = host.pos + rel;
        dust.vel = host.vel;
        dust.temperature = std::clamp(base_temp * (0.88f + u01(rng) * 0.24f), 25.0f, 5000.0f);
        dust.atmosphere_retention = 0.0f;
        dust.material_phase = (dust.temperature < 170.0f) ? PHASE_ICE : PHASE_SOLID;
        dust.phase_intensity = std::clamp(0.25f + ice_fraction * 0.50f + u01(rng) * 0.15f, 0.10f, 1.0f);
        dust.seed = hash_combine(seed, (uint32_t)(i * 2654435761u + 9719u));
        dust.frag_generation = std::max<uint32_t>(1u, host.frag_generation + 1u);
        dust.angular_vel = (u01(rng) * 2.0f - 1.0f) * (dust.type == CTYPE_DUST ? 0.03f : 0.01f);
        dust.non_attracting = true; // all ring debris is non-attracting
        dust.name = generate_body_name(dust.seed, dust.type);
        clear_ring_system(dust);
        clear_impact_signature(dust);

        // Initialize ring dynamics with a velocity-Verlet tuned orbital velocity.
        dust.vel = verlet_auto_orbit_velocity(dust, host, 0.0f, 1.0f + (u01(rng) * 2.0f - 1.0f) * 0.04f);
        dust.vel += axis * ((u01(rng) * 2.0f - 1.0f) * 0.00018f);
        if (!std::isfinite(dust.vel.x) || !std::isfinite(dust.vel.y) || !std::isfinite(dust.vel.z))
            continue;

        refresh_body_render_state(dust, &state);
        state.bodies.push_back(dust);
        state.trails.emplace_back();
        spawned_any = true;
    }

    return spawned_any;
}

// ── Nebula Cloud ─────────────────────────────────────────────────────────────
// Spawns a nebula as a cloud of many small gas/dust particles instead of a
// single volumetric body. Particles attract each other via gravity and will
// naturally collapse over time. Returns the index of the central seed particle,
// or -1 on failure.
int CosmosApp::spawn_nebula_cloud(glm::vec3 center, glm::vec3 base_vel,
                                  float total_mass, float cloud_radius,
                                  uint32_t seed) {
    if (total_mass <= 0.0f || cloud_radius <= 0.0f)
        return -1;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // Particle count scales with mass and radius; budget-aware.
    int count = std::clamp((int)std::round(60.0f + std::sqrt(total_mass / 0.001f) * 12.0f +
                                           cloud_radius * 0.4f), 40, 300);

    if (cfg.dynamic_budget_enabled) {
        int cap = std::max(cfg.dynamic_max_fragments, 0);
        int current = 0;
        for (const auto& b : state.bodies) {
            if (!b.marked_for_removal && !b.non_attracting) ++current;
        }
        count = std::min(count, std::max(0, cap - current));
    }
    if (count < 8) return -1;

    // Distribute mass with some variance — a few heavier clumps, many lighter wisps.
    std::vector<float> masses((size_t)count);
    float wsum = 0.0f;
    for (int i = 0; i < count; ++i) {
        // Power-law weights: most particles are small, a few are large clumps.
        float w = std::pow(u01(rng), 0.6f) + 0.1f;
        masses[(size_t)i] = w;
        wsum += w;
    }
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] = total_mass * (masses[(size_t)i] / std::max(wsum, 1.0e-6f));

    // Cloud shape: ellipsoidal with random axis ratios for asymmetry.
    glm::vec3 axis_scale(
        0.6f + u01(rng) * 1.0f,
        0.4f + u01(rng) * 0.7f,
        0.5f + u01(rng) * 1.1f);
    float max_axis = std::max({axis_scale.x, axis_scale.y, axis_scale.z});
    axis_scale /= max_axis; // normalize so largest axis = 1.0

    // Random rotation basis for the cloud orientation.
    float yaw = u01(rng) * 6.28318530718f;
    float pitch = (u01(rng) - 0.5f) * 3.14159265f;
    glm::vec3 fwd(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                  std::cos(pitch) * std::sin(yaw));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    if (glm::dot(right, right) < 1e-6f)
        right = glm::normalize(glm::cross(fwd, glm::vec3(1, 0, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    int seed_idx = -1;
    float base_temp = 20.0f + u01(rng) * 80.0f; // cold gas cloud (20-100K)

    for (int i = 0; i < count; ++i) {
        CelestialBody p;
        p.type = CTYPE_DUST;
        p.mass = std::max(masses[(size_t)i], 1.0e-12f);

        // Position: Gaussian distribution with ellipsoidal shaping.
        // Sigma = cloud_radius/3 so ~99% of particles within cloud_radius.
        float sigma = cloud_radius / 3.0f;
        glm::vec3 local(gauss(rng) * sigma * axis_scale.x,
                        gauss(rng) * sigma * axis_scale.y,
                        gauss(rng) * sigma * axis_scale.z);
        // Rotate into cloud orientation.
        glm::vec3 world_offset = right * local.x + up * local.y + fwd * local.z;
        p.pos = center + world_offset;

        // Radius: tiny gas clumps — visual size from mass.
        float density_factor = 0.8f + u01(rng) * 0.8f;
        p.radius = std::max(0.8f, std::cbrt(p.mass / density_factor) * 35.0f);

        // Velocity: base velocity + slow turbulent motion + slight bulk rotation.
        glm::vec3 offset_from_center = p.pos - glm::vec3(center);
        float dist = glm::length(offset_from_center);
        glm::vec3 turbulent(gauss(rng) * 0.0003f, gauss(rng) * 0.0003f, gauss(rng) * 0.0003f);
        // Slight bulk rotation around cloud axis.
        glm::vec3 rot_vel(0.0f);
        if (dist > 0.1f) {
            glm::vec3 dir = offset_from_center / dist;
            glm::vec3 tangent = glm::normalize(glm::cross(fwd, dir));
            if (glm::dot(tangent, tangent) > 1e-6f)
                rot_vel = tangent * (0.0001f + u01(rng) * 0.0002f);
        }
        p.vel = base_vel + turbulent + rot_vel;

        p.temperature = std::clamp(base_temp * (0.8f + u01(rng) * 0.4f), 10.0f, 200.0f);
        p.material_phase = PHASE_GAS;
        p.phase_intensity = std::clamp(0.5f + u01(rng) * 0.4f, 0.3f, 0.95f);
        p.atmosphere_retention = 0.8f + u01(rng) * 0.2f;
        p.seed = hash_combine(seed, (uint32_t)(i * 2654435761u + 7919u));
        p.custom_material = true;
        p.custom_hydrogen = 0.88f + u01(rng) * 0.08f;
        p.custom_silicate = 0.02f + u01(rng) * 0.04f;
        p.custom_water   = 0.01f + u01(rng) * 0.03f;
        p.custom_iron    = 0.005f + u01(rng) * 0.01f;
        p.angular_vel = (u01(rng) - 0.5f) * 0.001f;
        p.non_attracting = false; // must attract to collapse under gravity
        p.name = generate_body_name(p.seed, p.type);
        clear_ring_system(p);
        clear_impact_signature(p);
        p.props_valid = false;
        p.visuals_valid = false;

        refresh_body_render_state(p, &state);
        state.bodies.push_back(p);
        state.trails.emplace_back();

        // Track the heaviest particle as the "seed" (central clump).
        if (seed_idx < 0 || p.mass > state.bodies[(size_t)seed_idx].mass)
            seed_idx = (int)state.bodies.size() - 1;
    }

    return seed_idx;
}

void CosmosApp::spawn_moons_for_host(int host_index, int moon_count,
                                     int orbit_layout, float inclination_deg,
                                     float spacing_scale) {
    if (host_index < 0 || host_index >= (int)state.bodies.size())
        return;
    const CelestialBody host_snapshot = state.bodies[(size_t)host_index];
    if (host_snapshot.marked_for_removal || host_snapshot.type != CTYPE_PLANET ||
        is_star_type(host_snapshot.type) || is_black_hole_type(host_snapshot.type))
        return;

    int count = std::clamp(moon_count, 1, 100);
    int layout = std::clamp(orbit_layout, 0, 4);
    float inc_deg = std::clamp(inclination_deg, 0.0f, 85.0f);
    float spacing = std::clamp(spacing_scale, 0.35f, 4.0f);
    uint32_t base_seed = host_snapshot.seed ^ (uint32_t)(std::abs((int)(sim_time_ * 1000.0f))) ^ 0xA11CE55u;
    std::mt19937 moon_rng(base_seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float two_pi = 6.28318530718f;
    const float inc_rad_base = glm::radians(inc_deg);
    const float compact_scale = (layout == 1) ? 0.74f : (layout == 2 ? 1.35f : 1.0f);
    const float spacing_step = 0.28f + 0.92f * spacing * compact_scale;

    auto random_unit = [&]() -> glm::vec3 {
        float z = u01(moon_rng) * 2.0f - 1.0f;
        float t = u01(moon_rng) * two_pi;
        float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        return glm::vec3(std::cos(t) * r, z, std::sin(t) * r);
    };

    float base_phase = u01(moon_rng) * two_pi;
    for (int mi = 0; mi < count; ++mi) {
        CelestialBody moon;
        moon.type = CTYPE_MOON;
        float mass_scale = 1.0f / std::sqrt(std::max((float)count, 1.0f));
        moon.mass = std::clamp(host_snapshot.mass * (0.00008f + u01(moon_rng) * 0.0045f) * mass_scale,
                               1.0e-9f, host_snapshot.mass * 0.15f);
        moon.seed = hash_combine(base_seed, (uint32_t)(mi * 7919 + 17));
        randomize_moon_properties(moon, state, moon_rng);
        moon.parent = host_index;
        float orbit_r = host_snapshot.radius * (2.2f + spacing_step * (float)mi + u01(moon_rng) * (1.2f + 0.9f * spacing));
        if (layout == 3) { // resonant chain
            orbit_r = host_snapshot.radius * (2.3f * std::pow(1.58f, (float)mi));
        }
        float theta = base_phase + (layout == 3 ? (float)mi * 2.0943951f : (float)mi * 0.43f) + u01(moon_rng) * 0.35f;
        float inc_use = inc_rad_base;
        if (layout == 4) inc_use = glm::radians(70.0f);
        if (layout == 1) inc_use *= 0.45f;
        if (layout == 2) inc_use *= 1.35f;
        float inc_jitter = (u01(moon_rng) * 2.0f - 1.0f) * inc_use;

        glm::vec3 rel(0.0f);
        if (layout == 4) { // isotropic cloud
            rel = random_unit() * orbit_r;
        } else {
            rel = glm::vec3(
                std::cos(theta) * orbit_r,
                std::sin(inc_jitter) * orbit_r * (0.15f + 0.85f * u01(moon_rng)),
                std::sin(theta) * orbit_r);
        }
        moon.pos = host_snapshot.pos + rel;
        glm::vec3 r_hat = glm::normalize(rel);
        glm::vec3 orbit_normal = (layout == 4) ? random_unit() : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 tangent = glm::cross(orbit_normal, r_hat);
        if (glm::dot(tangent, tangent) < 1.0e-8f)
            tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), r_hat);
        if (glm::dot(tangent, tangent) < 1.0e-8f)
            tangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), r_hat);
        tangent = glm::normalize(tangent);
        float v = std::sqrt(std::max(cfg.G * host_snapshot.mass / std::max(glm::length(rel), 1.0e-4f), 0.0f));
        moon.vel = host_snapshot.vel + tangent * v;
        moon.name = generate_body_name(moon.seed, moon.type);
        refresh_body_render_state(moon, &state);
        state.bodies.push_back(moon);
        state.trails.emplace_back();
    }
}

void CosmosApp::spawn_ring_for_host(int host_index, float inner_mult, float outer_mult,
                                    float density, float ice_fraction, int ring_style) {
    if (host_index < 0 || host_index >= (int)state.bodies.size())
        return;
    CelestialBody& host = state.bodies[(size_t)host_index];
    if (host.marked_for_removal || is_star_type(host.type) || is_black_hole_type(host.type))
        return;
    if (!(host.type == CTYPE_PLANET || host.type == CTYPE_MOON || host.type == CTYPE_NEBULA))
        return;

    float inner = std::max(host.radius * std::max(inner_mult, 1.15f), host.radius * 1.15f);
    float outer = std::max(host.radius * std::max(outer_mult, inner_mult + 0.2f), inner + host.radius * 0.20f);
    host.ring_inner_radius = inner;
    host.ring_outer_radius = outer;
    host.ring_density = std::clamp(density, 0.01f, 1.0f);
    host.ring_ice_fraction = std::clamp(ice_fraction, 0.0f, 1.0f);
    host.ring_tilt = std::clamp(std::max(std::abs(host.angular_vel) * 150.0f, 0.02f), 0.02f, 1.30f);

    float annulus = std::max(host.ring_outer_radius * host.ring_outer_radius -
                             host.ring_inner_radius * host.ring_inner_radius, 1.0f);
    float mass_hint = std::max(host.mass * host.ring_density * 0.0000025f *
                               (annulus / std::max(host.radius * host.radius, 1.0e-5f)),
                               std::max(cfg.min_fragment_mass, 1.0e-12f) * 2.0f);
    spawn_dust_ring(host_index, mass_hint, host.ring_inner_radius, host.ring_outer_radius,
                    host.ring_density, host.ring_ice_fraction,
                    hash_combine(host.seed, 0xA77A11u),
                    ring_style);
    clear_ring_system(host);
    host.props_valid = false;
    host.visuals_valid = false;
}

void CosmosApp::apply_dust_debug_mode() {
    for (auto& b : state.bodies) {
        if (b.marked_for_removal || b.type != CTYPE_DUST)
            continue;
        b.non_attracting = cfg.dust_debug_non_attracting;
    }
}

// ── Collision Processing ────────────────────────────────────────────────────

void CosmosApp::trigger_stellar_supernova(size_t index, float dt, bool thermonuclear,
                                          glm::vec3 impact_axis, float ejecta_speed) {
    if (index >= state.bodies.size()) return;
    CelestialBody& b = state.bodies[index];
    if (b.marked_for_removal) return;

    float progenitor_mass = std::max(b.mass, 0.01f);
    StellarRemnantKind remnant = stellar_remnant_kind(progenitor_mass, thermonuclear);
    float remnant_mass = 0.0f;

    switch (remnant) {
    case REMNANT_WHITE_DWARF:
        remnant_mass = std::clamp(0.48f + progenitor_mass * 0.10f, 0.45f, 1.30f);
        break;
    case REMNANT_NEUTRON_STAR:
        remnant_mass = std::clamp(1.25f + (progenitor_mass - CORE_COLLAPSE_MIN_MASS_SOLAR) * 0.045f,
                                  1.25f, 2.40f);
        break;
    case REMNANT_BLACK_HOLE:
        remnant_mass = std::clamp(progenitor_mass * 0.35f, 3.0f, progenitor_mass * 0.85f);
        break;
    case REMNANT_NONE:
    default:
        remnant_mass = 0.0f;
        break;
    }

    float ejecta_mass = std::max(progenitor_mass - remnant_mass, 0.0f);
    int burst_count = std::clamp(cfg.fragment_count * 2, cfg.fragment_count, 24);
    glm::vec3 axis = glm::length(impact_axis) > 1.0e-4f
        ? glm::normalize(impact_axis)
        : glm::normalize(glm::vec3(0.7f, 0.3f, 0.2f));
    float burst_speed = ejecta_speed > 0.0f
        ? ejecta_speed
        : (thermonuclear ? std::max(22.0f, progenitor_mass * 2.0f)
                         : std::max(28.0f, progenitor_mass * 1.4f));

    if (ejecta_mass > 1.0e-4f) {
        spawn_fragments(b.pos, b.vel, ejecta_mass, burst_count,
                        b.frag_generation, std::max(b.temperature, 6000.0f),
                        axis, burst_speed, &b, thermonuclear ? 1.6f : 1.2f);
        register_mass_loss(b, ejecta_mass, std::max(dt, 1.0e-4f));
    }

    if (thermonuclear || remnant == REMNANT_NONE) {
        b.marked_for_removal = true;
        return;
    }

    b.mass = remnant_mass;
    b.fuel = 0.0f;
    b.internal_energy *= 0.1f;
    clear_ring_system(b);
    clear_impact_signature(b);

    if (remnant == REMNANT_WHITE_DWARF) {
        b.stellar_stage = SSTAGE_WHITE_DWARF;
        b.temperature = std::clamp(22000.0f + progenitor_mass * 1800.0f, 9000.0f, 120000.0f);
        b.radius = expected_star_radius(b);
        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
        b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else if (remnant == REMNANT_NEUTRON_STAR) {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 120000.0f;
        b.radius = expected_star_radius(b);
        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
        b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        float spin_sign = (b.angular_vel < 0.0f) ? -1.0f : 1.0f;
        b.angular_vel = spin_sign * std::clamp(std::abs(b.angular_vel) * 4.0f + 0.002f, 0.002f, 0.05f);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    } else {
        b.stellar_stage = SSTAGE_NEUTRON_STAR;
        b.temperature = 0.0f;
        b.luminosity = 0.0f;
        b.type = classify_black_hole(b.mass);
        b.radius = std::max(0.5f, 2.0f * b.mass);
        b.material_phase = PHASE_PLASMA;
        b.phase_intensity = 1.0f;
    }

    b.vel += axis * (burst_speed * 0.04f / std::max(b.mass, 0.1f));
    refresh_body_render_state(b, &state);
}

bool CosmosApp::handle_stellar_collision_supernova(size_t i, size_t j, float rel_speed,
                                                   float impact_energy, float escape_speed,
                                                   const glm::vec3& impact_axis, float dt) {
    if (!is_star_type(state.bodies[i].type) || !is_star_type(state.bodies[j].type))
        return false;

    CelestialBody& a = state.bodies[i];
    CelestialBody& b = state.bodies[j];
    float total_mass = a.mass + b.mass;
    bool white_dwarf_pair = (a.stellar_stage == SSTAGE_WHITE_DWARF || b.stellar_stage == SSTAGE_WHITE_DWARF);
    bool thermonuclear = white_dwarf_pair &&
        total_mass >= CHANDRASEKHAR_LIMIT_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 0.7f, escape_speed * 1.1f);
    bool core_collapse = total_mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
        rel_speed >= std::max(cfg.fragment_speed_threshold * 1.15f, escape_speed * 1.6f) &&
        impact_energy > total_mass * 25.0f;

    if (!thermonuclear && !core_collapse)
        return false;

    size_t big = (a.mass >= b.mass) ? i : j;
    size_t small = (big == i) ? j : i;
    CelestialBody merged = state.bodies[big];

    merged.pos = (a.pos * a.mass + b.pos * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.vel = (a.vel * a.mass + b.vel * b.mass) / std::max(total_mass, 1.0e-6f);
    merged.mass = total_mass;
    merged.radius = std::cbrt(std::max(a.radius * a.radius * a.radius +
                                       b.radius * b.radius * b.radius, 1.0f));
    merged.temperature = std::max((a.temperature * a.mass + b.temperature * b.mass) /
                                  std::max(total_mass, 1.0e-6f),
                                  thermonuclear ? 140000.0f : 90000.0f);
    merged.fuel = thermonuclear ? 0.0f : merged_star_fuel(a, b, total_mass) * 0.7f;
    merged.internal_energy = a.internal_energy + b.internal_energy + impact_energy * 0.15f;
    merged.angular_vel = (a.angular_vel * a.mass + b.angular_vel * b.mass) /
                         std::max(total_mass, 1.0e-6f);
    merged.type = classify_star_spectral(std::max(merged.temperature, 250.0f), std::max(merged.mass, 0.003f));
    merged.stellar_stage = thermonuclear ? SSTAGE_WHITE_DWARF :
        ((a.stellar_stage == SSTAGE_RED_GIANT || b.stellar_stage == SSTAGE_RED_GIANT)
            ? SSTAGE_RED_GIANT : SSTAGE_MAIN_SEQUENCE);

    state.bodies[big] = merged;
    state.bodies[small].marked_for_removal = true;
    trigger_stellar_supernova(big, dt, thermonuclear, impact_axis, std::max(rel_speed * 0.6f, 24.0f));
    return true;
}

void CosmosApp::process_collisions(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();

    // Build candidate pair list -- spatial hash grid for n >= 64, brute force otherwise.
    std::vector<std::pair<size_t, size_t>> pairs;

    if (n < 64 || !cfg.spatial_hash_collisions) {
        // Brute-force: O(n^2) is cheaper than grid overhead for tiny counts.
        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (bodies[j].marked_for_removal) continue;
                pairs.emplace_back(i, j);
            }
        }
    } else {
        // Find max radius to set cell size.
        float max_radius = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (!bodies[i].marked_for_removal)
                max_radius = std::max(max_radius, bodies[i].radius);
        }
        float cell_size = std::max(2.0f * max_radius, 1.0e-6f);
        float inv_cell = 1.0f / cell_size;

        // Spatial hash: cell coord -> list of body indices.
        auto cell_hash = [](int cx, int cy, int cz) -> int64_t {
            // Combine three 21-bit signed cell coords into a single int64.
            return (int64_t(cx) * int64_t(73856093)) ^
                   (int64_t(cy) * int64_t(19349663)) ^
                   (int64_t(cz) * int64_t(83492791));
        };

        std::unordered_map<int64_t, std::vector<size_t>> grid;
        grid.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            if (bodies[i].marked_for_removal) continue;
            int cx = (int)std::floor(bodies[i].pos.x * inv_cell);
            int cy = (int)std::floor(bodies[i].pos.y * inv_cell);
            int cz = (int)std::floor(bodies[i].pos.z * inv_cell);
            grid[cell_hash(cx, cy, cz)].push_back(i);
        }

        // Collect unique candidate pairs from 27-cell neighborhoods.
        // To avoid duplicates: for each cell, check the 13 "forward" neighbors
        // plus the cell itself (self-pairs within the cell).
        const int offsets[14][3] = {
            { 0, 0, 0},  // self
            { 1, 0, 0},  { 0, 1, 0},  { 0, 0, 1},
            { 1, 1, 0},  { 1,-1, 0},  { 1, 0, 1},  { 1, 0,-1},
            { 0, 1, 1},  { 0, 1,-1},  { 1, 1, 1},  { 1, 1,-1},
            { 1,-1, 1},  { 1,-1,-1}
        };

        for (auto& [key, cell] : grid) {
            if (cell.empty()) continue;
            // Recover cell coords from first body in the cell.
            size_t rep = cell[0];
            int cx = (int)std::floor(bodies[rep].pos.x * inv_cell);
            int cy = (int)std::floor(bodies[rep].pos.y * inv_cell);
            int cz = (int)std::floor(bodies[rep].pos.z * inv_cell);

            // Self-pairs within this cell.
            for (size_t a = 0; a < cell.size(); ++a) {
                for (size_t b = a + 1; b < cell.size(); ++b) {
                    size_t ii = cell[a], jj = cell[b];
                    if (ii > jj) std::swap(ii, jj);
                    pairs.emplace_back(ii, jj);
                }
            }

            // Cross-pairs with 13 forward-neighbor cells.
            for (int d = 1; d < 14; ++d) {
                int64_t nkey = cell_hash(cx + offsets[d][0],
                                         cy + offsets[d][1],
                                         cz + offsets[d][2]);
                auto it = grid.find(nkey);
                if (it == grid.end()) continue;
                const auto& ncell = it->second;
                for (size_t ai : cell) {
                    for (size_t bi : ncell) {
                        size_t ii = ai, jj = bi;
                        if (ii == jj) continue;
                        if (ii > jj) std::swap(ii, jj);
                        pairs.emplace_back(ii, jj);
                    }
                }
            }
        }

        // Deduplicate pairs (hash collisions can produce the same cell key for
        // different actual grid cells, yielding duplicate pairs).
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    }

    // Process all candidate pairs.
    for (auto& [i, j] : pairs) {
        if (bodies[i].marked_for_removal || bodies[j].marked_for_removal) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;

            glm::vec3 prev_diff = diff - rel_vel * dt;
            glm::vec3 seg = diff - prev_diff;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_diff, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_diff + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);

            bool overlap_now = (dist < touch);
            bool swept_hit = (sweep_dist < touch);
            if (!overlap_now && !swept_hit) continue;

            bool nebula_pair = (bodies[i].type == CTYPE_NEBULA || bodies[j].type == CTYPE_NEBULA);
            if (nebula_pair) {
                float contact_dist = overlap_now ? dist : sweep_dist;
                float overlap = std::max(touch - contact_dist, 0.0f);
                float overlap_fraction = overlap / std::max(touch, 1.0e-6f);

                int nebula_i = (bodies[i].type == CTYPE_NEBULA) ? 1 : 0;
                int nebula_j = (bodies[j].type == CTYPE_NEBULA) ? 1 : 0;

                // Nebulae are diffuse volumes: no rigid-body bounce/depenetration.
                // We only apply soft drag so bodies are entrained by cloud flow.
                if (nebula_i && !nebula_j) {
                    float drag = std::clamp((0.12f + overlap_fraction * 0.70f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.94f);
                    bodies[j].vel = glm::mix(bodies[j].vel, bodies[i].vel, drag);
                } else if (nebula_j && !nebula_i) {
                    float drag = std::clamp((0.12f + overlap_fraction * 0.70f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.94f);
                    bodies[i].vel = glm::mix(bodies[i].vel, bodies[j].vel, drag);
                } else {
                    float mutual = std::clamp((0.06f + overlap_fraction * 0.35f) * std::max(dt, 1.0e-4f) * 8.0f, 0.0f, 0.65f);
                    glm::vec3 vcm = (bodies[i].vel * bodies[i].mass + bodies[j].vel * bodies[j].mass) /
                                    std::max(bodies[i].mass + bodies[j].mass, 1.0e-6f);
                    bodies[i].vel = glm::mix(bodies[i].vel, vcm, mutual);
                    bodies[j].vel = glm::mix(bodies[j].vel, vcm, mutual);
                }

                if (cfg.temperature_system) {
                    float thermal_kick = std::min(glm::length(rel_vel) * overlap_fraction * 18.0f, 1200.0f);
                    if (nebula_i) {
                        bodies[i].temperature += thermal_kick * 0.10f;
                        bodies[i].internal_energy += thermal_kick * 0.002f;
                    }
                    if (nebula_j) {
                        bodies[j].temperature += thermal_kick * 0.10f;
                        bodies[j].internal_energy += thermal_kick * 0.002f;
                    }
                    if (nebula_i && !nebula_j) {
                        bodies[j].temperature += thermal_kick * 0.25f;
                        bodies[j].internal_energy += thermal_kick * 0.004f;
                    } else if (nebula_j && !nebula_i) {
                        bodies[i].temperature += thermal_kick * 0.25f;
                        bodies[i].internal_energy += thermal_kick * 0.004f;
                    }
                }
                continue;
            }

            float contact_dist = overlap_now ? dist : sweep_dist;
            glm::vec3 normal = overlap_now ? (diff / std::max(dist, 1.0e-6f))
                                           : (closest_rel / std::max(sweep_dist, 1.0e-6f));
            if (glm::length(normal) < 1.0e-5f) {
                if (glm::length(rel_vel) > 1.0e-5f) {
                    normal = glm::normalize(rel_vel);
                } else {
                    // Deterministic fallback axis for near-identical overlap states.
                    uint32_t h = (uint32_t)(i * 73856093u) ^ (uint32_t)(j * 19349663u);
                    float x = ((h & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float y = (((h >> 10) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    float z = (((h >> 20) & 1023u) / 1023.0f) * 2.0f - 1.0f;
                    normal = glm::normalize(glm::vec3(x, y, z));
                    if (glm::length(normal) < 1.0e-5f)
                        normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
            }

            float overlap = std::max(touch - contact_dist, 0.0f);
            float overlap_fraction = overlap / std::max(touch, 1.0e-6f);
            float total_mass = bodies[i].mass + bodies[j].mass;
            if (total_mass <= 1.0e-8f) continue;
            bool soft_body_pair =
                !is_star_type(bodies[i].type) && !is_star_type(bodies[j].type) &&
                !is_black_hole_type(bodies[i].type) && !is_black_hole_type(bodies[j].type) &&
                bodies[i].type != CTYPE_NEBULA && bodies[j].type != CTYPE_NEBULA;
            bool use_rigid_response = cfg.collision_rigid_body_dynamics || !soft_body_pair;

            // Immediate depenetration to avoid sticky overlap accumulation.
            if (use_rigid_response && overlap_now && overlap > 0.0f) {
                float separate_scale = std::clamp(cfg.rigid_collision_separation, 0.0f, 3.0f);
                bodies[i].pos -= normal * overlap * separate_scale * (bodies[j].mass / std::max(total_mass, 1.0e-6f));
                bodies[j].pos += normal * overlap * separate_scale * (bodies[i].mass / std::max(total_mass, 1.0e-6f));
            }
            float rel_speed = glm::length(rel_vel);
            float vel_along = glm::dot(rel_vel, normal);
            float closing_speed = std::max(-vel_along, 0.0f);
            float escape_speed = body_escape_speed(bodies[i], bodies[j], cfg.G);

            bool small_soft_pair = soft_body_pair &&
                std::max(bodies[i].radius, bodies[j].radius) < (EARTH_RADIUS_SIM_UNITS * 0.95f);
            bool compact_i = is_star_type(bodies[i].type) || is_black_hole_type(bodies[i].type);
            bool compact_j = is_star_type(bodies[j].type) || is_black_hole_type(bodies[j].type);
            bool compact_vs_soft = (compact_i != compact_j);

            float infall_speed = soft_body_pair ? std::max(closing_speed, escape_speed * 0.45f) : closing_speed;
            float compression_speed = overlap_fraction * escape_speed * (soft_body_pair ? 2.8f : 1.6f);
            float impact_speed = std::max(rel_speed, std::max(infall_speed, compression_speed));
            float reduced_mass = (bodies[i].mass * bodies[j].mass) / std::max(total_mass, 1.0e-6f);
            float impact_energy = 0.5f * reduced_mass * impact_speed * impact_speed;

            float binding_i = body_gravitational_binding_energy(bodies[i], cfg.G);
            float binding_j = body_gravitational_binding_energy(bodies[j], cfg.G);
            float combined_binding = binding_i + binding_j;
            float disruption_i = impact_energy / std::max(binding_i, 1.0e-6f);
            float disruption_j = impact_energy / std::max(binding_j, 1.0e-6f);
            float combined_disruption = impact_energy / std::max(combined_binding, 1.0e-6f);
            glm::vec3 impact_axis = (rel_speed > 1.0e-5f) ? (rel_vel / rel_speed) : normal;
            float spin_ratio_i = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[i], cfg.G) : 0.0f;
            float spin_ratio_j = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[j], cfg.G) : 0.0f;
            float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
            float spin_over_i = std::max(spin_ratio_i - spin_threshold, 0.0f);
            float spin_over_j = std::max(spin_ratio_j - spin_threshold, 0.0f);
            if (cfg.spin_fragmentation) {
                float spin_disruption_i = spin_over_i * (soft_body_pair ? 0.34f : 0.12f);
                float spin_disruption_j = spin_over_j * (soft_body_pair ? 0.34f : 0.12f);
                disruption_i += spin_disruption_i;
                disruption_j += spin_disruption_j;
                combined_disruption += (spin_disruption_i + spin_disruption_j) *
                    (soft_body_pair ? 0.55f : 0.28f);
            }

            if (soft_body_pair && cfg.collision_sph) {
                float h = touch * 1.25f;
                float q = std::clamp(1.0f - contact_dist / std::max(h, 1.0e-6f), 0.0f, 1.0f);
                if (q > 0.0f) {
                    float inv_total_mass = 1.0f / std::max(total_mass, 1.0e-6f);
                    float pressure_term = q * q * (0.18f + overlap_fraction * 0.65f) *
                        std::clamp(cfg.collision_sph_pressure, 0.0f, 4.0f);
                    glm::vec3 pressure_push = normal * pressure_term;
                    bodies[i].vel -= pressure_push * (bodies[j].mass * inv_total_mass);
                    bodies[j].vel += pressure_push * (bodies[i].mass * inv_total_mass);

                    glm::vec3 post_rel = bodies[j].vel - bodies[i].vel;
                    float viscosity_term = (0.10f + 0.65f * q) * std::max(dt, 1.0e-4f) *
                        std::clamp(cfg.collision_sph_viscosity, 0.0f, 4.0f);
                    glm::vec3 viscosity = post_rel * viscosity_term;
                    bodies[i].vel += viscosity * (bodies[j].mass * inv_total_mass);
                    bodies[j].vel -= viscosity * (bodies[i].mass * inv_total_mass);

                    if (cfg.temperature_system) {
                        float sph_heat = impact_energy * q * (0.05f + overlap_fraction * 0.10f) *
                            std::clamp(cfg.collision_sph_heat, 0.0f, 4.0f);
                        bodies[i].internal_energy += sph_heat * 0.5f;
                        bodies[j].internal_energy += sph_heat * 0.5f;
                    }
                }
            }

            // Collision heating (kinetic -> thermal/internal).
            if (cfg.temperature_system) {
                float heat = impact_energy * cfg.collision_heating * (soft_body_pair ? 1.35f : 0.85f);
                bodies[i].internal_energy += heat * 0.5f;
                bodies[j].internal_energy += heat * 0.5f;
                bodies[i].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[i].mass, 1.0e-6f), 18000.0f);
                bodies[j].temperature += std::min(heat * (soft_body_pair ? 80.0f : 16.0f) /
                                                  std::max(bodies[j].mass, 1.0e-6f), 18000.0f);
            }

            if (handle_stellar_collision_supernova(i, j, rel_speed, impact_energy,
                                                   escape_speed, impact_axis, dt)) {
                continue;
            }

            float merge_speed_limit = 0.0f;
            float fragment_speed_limit = 0.0f;
            if (soft_body_pair) {
                if (small_soft_pair) {
                    merge_speed_limit = std::max(escape_speed * 0.20f, 1.0e-4f);
                    fragment_speed_limit = std::max(escape_speed * 0.35f, 2.5e-4f);
                } else {
                    merge_speed_limit = std::max(std::max(escape_speed * 0.55f, cfg.merge_speed_threshold * 0.0005f), 0.0015f);
                    fragment_speed_limit = std::max(std::max(escape_speed * 2.20f, cfg.fragment_speed_threshold * 0.0010f),
                                                    merge_speed_limit * 2.0f);
                }
            } else {
                merge_speed_limit = std::max(cfg.merge_speed_threshold, escape_speed * 0.95f);
                fragment_speed_limit = std::max(cfg.fragment_speed_threshold, escape_speed * 1.15f);
            }

            bool catastrophic_fragment =
                (impact_speed >= (soft_body_pair ? fragment_speed_limit * 1.15f : fragment_speed_limit) ||
                 combined_disruption > (soft_body_pair ? 0.35f : 0.72f) ||
                 disruption_i > (soft_body_pair ? 0.38f : 0.92f) ||
                 disruption_j > (soft_body_pair ? 0.38f : 0.92f) ||
                 (compact_vs_soft && (swept_hit || overlap_fraction > 0.005f)));

            bool optional_fragment = cfg.collision_fragmentation &&
                (impact_speed >= fragment_speed_limit * (soft_body_pair ? 0.90f : 0.90f) ||
                 combined_disruption > (soft_body_pair ? 0.22f : 0.60f) ||
                 disruption_i > (soft_body_pair ? 0.26f : 0.75f) ||
                 disruption_j > (soft_body_pair ? 0.26f : 0.75f));
            bool spin_fragment_contact = cfg.spin_fragmentation &&
                (spin_over_i > 0.0f || spin_over_j > 0.0f) &&
                (overlap_now || swept_hit || overlap_fraction > (soft_body_pair ? 0.01f : 0.002f)) &&
                impact_speed > merge_speed_limit * (soft_body_pair ? 0.45f : 0.70f);

            bool fragment_candidate = catastrophic_fragment || optional_fragment || spin_fragment_contact ||
                (soft_body_pair && swept_hit && impact_speed > fragment_speed_limit * 0.75f);
            bool ultra_gentle_soft = soft_body_pair &&
                impact_speed < merge_speed_limit * 0.95f &&
                combined_disruption < 0.030f &&
                overlap_fraction < 0.12f;
            if (small_soft_pair) {
                ultra_gentle_soft = impact_speed < merge_speed_limit * 0.60f &&
                    combined_disruption < 0.005f &&
                    overlap_fraction < 0.01f;
            }
            if (ultra_gentle_soft)
                fragment_candidate = false;
            if (small_soft_pair && cfg.collision_fragmentation &&
                (overlap_now || swept_hit) && !ultra_gentle_soft)
                fragment_candidate = true;

            bool sticky_soft_merge = soft_body_pair && !cfg.collision_fragmentation &&
                overlap_now && !fragment_candidate &&
                impact_speed < fragment_speed_limit * 0.95f &&
                combined_disruption < 0.20f;

            bool merge_candidate = cfg.collision_merging &&
                !swept_hit &&
                ((overlap_now &&
                  rel_speed <= merge_speed_limit &&
                  closing_speed <= merge_speed_limit &&
                  impact_speed <= fragment_speed_limit * (soft_body_pair ? 0.70f : 0.90f) &&
                  overlap_fraction >= (soft_body_pair ? 0.03f : 0.006f) &&
                  combined_disruption < (soft_body_pair ? 0.06f : 0.55f)) ||
                 sticky_soft_merge) &&
                !fragment_candidate;
            if (soft_body_pair && cfg.collision_fragmentation)
                merge_candidate = merge_candidate && ultra_gentle_soft;
            if (small_soft_pair && cfg.collision_fragmentation)
                merge_candidate = false;

            if (merge_candidate) {
                size_t big = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small = (big == i) ? j : i;
                CelestialBody pre_big = bodies[big];
                CelestialBody pre_small = bodies[small];

                glm::vec3 com_pos = (pre_big.pos * pre_big.mass + pre_small.pos * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                glm::vec3 com_vel = (pre_big.vel * pre_big.mass + pre_small.vel * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);
                float merged_temp = (pre_big.temperature * pre_big.mass + pre_small.temperature * pre_small.mass) /
                                    std::max(total_mass, 1.0e-6f);

                bodies[big].pos = com_pos;
                bodies[big].vel = com_vel;
                bodies[big].mass = total_mass;
                bodies[big].radius = std::cbrt(std::max(pre_big.radius * pre_big.radius * pre_big.radius +
                                                        pre_small.radius * pre_small.radius * pre_small.radius,
                                                        1.0e-6f));
                bodies[big].temperature = merged_temp +
                    std::min(impact_energy * (soft_body_pair ? 28.0f : 10.0f) / std::max(total_mass, 1.0e-6f), 4500.0f);
                bodies[big].internal_energy = pre_big.internal_energy + pre_small.internal_energy + impact_energy * 0.06f;
                bodies[big].angular_vel = (pre_big.angular_vel * pre_big.mass + pre_small.angular_vel * pre_small.mass) /
                                          std::max(total_mass, 1.0e-6f);

                if (is_star_type(pre_big.type) && is_star_type(pre_small.type)) {
                    bodies[big].fuel = merged_star_fuel(pre_big, pre_small, total_mass);
                    bodies[big].luminosity = expected_stellar_luminosity(
                        bodies[big].mass, bodies[big].temperature, bodies[big].radius,
                        bodies[big].stellar_stage, bodies[big].fuel);
                    bodies[big].type = classify_star_spectral(std::max(bodies[big].temperature, 250.0f),
                                                              std::max(bodies[big].mass, 0.003f));
                    if (bodies[big].mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
                        (pre_big.stellar_stage == SSTAGE_RED_GIANT ||
                         pre_small.stellar_stage == SSTAGE_RED_GIANT ||
                         bodies[big].fuel < 0.18f)) {
                        trigger_stellar_supernova(big, dt, false, impact_axis, std::max(rel_speed * 0.45f, 18.0f));
                    }
                    if ((pre_big.stellar_stage == SSTAGE_WHITE_DWARF || pre_small.stellar_stage == SSTAGE_WHITE_DWARF) &&
                        bodies[big].mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
                        trigger_stellar_supernova(big, dt, true, impact_axis, std::max(rel_speed * 0.50f, 22.0f));
                    }
                } else {
                    bodies[big].fuel = std::max(pre_big.fuel, pre_small.fuel);
                }

                // Gentle post-merge ejecta only for high-disruption mergers.
                if (!is_star_type(pre_big.type) && !is_black_hole_type(pre_big.type) &&
                    !is_star_type(pre_small.type) && !is_black_hole_type(pre_small.type) &&
                    cfg.collision_fragmentation) {
                    float ejecta_fraction = std::clamp((combined_disruption - (soft_body_pair ? 0.10f : 0.22f)) *
                                                       (soft_body_pair ? 0.35f : 0.20f),
                                                       0.0f, soft_body_pair ? 0.18f : 0.10f);
                    float ejecta_mass = total_mass * ejecta_fraction;
                    if (ejecta_mass > 1.0e-8f) {
                        int ejecta_count = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 8);
                        float merger_ejecta_speed = soft_body_pair
                            ? std::clamp(escape_speed * 0.55f + closing_speed * 0.20f, 0.004f, 0.045f)
                            : std::max(impact_speed * 0.35f, 2.0f);
                        spawn_fragments((pre_big.pos + pre_small.pos) * 0.5f, com_vel, ejecta_mass,
                                        ejecta_count, std::max(pre_big.frag_generation, pre_small.frag_generation),
                                        std::max(pre_big.temperature, pre_small.temperature),
                                        (big == i) ? normal : -normal,
                                        merger_ejecta_speed,
                                        &pre_small, combined_disruption);
                        register_mass_loss(bodies[big], ejecta_mass, std::max(dt, 1.0e-4f));
                        float remaining_mass = std::max(total_mass - ejecta_mass, 1.0e-8f);
                        float mass_scale = std::cbrt(remaining_mass / std::max(total_mass, 1.0e-8f));
                        bodies[big].mass = remaining_mass;
                        bodies[big].radius = std::max(bodies[big].radius * mass_scale, 0.1f);
                    }

                    apply_impact_signature(
                        bodies[big], (big == i) ? normal : -normal,
                        std::clamp(combined_disruption, 0.0f, 1.2f),
                        pre_small.mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(total_mass, 1.0e-6f), 0.0f, 1.5f),
                        std::clamp(pre_small.radius / std::max(bodies[big].radius, 0.1f), 0.08f, 0.95f));
                }

                bodies[big].props_valid = false;
                bodies[big].visuals_valid = false;
                bodies[small].marked_for_removal = true;
                continue;
            }

            bool closing_contact = vel_along < 0.0f || (soft_body_pair && dist < touch * 0.99f) || swept_hit;
            if (!closing_contact) continue;

            bool fragmenting = fragment_candidate;
            if (use_rigid_response) {
                float restitution = fragmenting ? 0.0f
                    : (soft_body_pair ? 0.0f : std::clamp(cfg.rigid_collision_restitution, 0.0f, 1.2f));
                float inv_mass_sum = (1.0f / std::max(bodies[i].mass, 1.0e-6f)) +
                                     (1.0f / std::max(bodies[j].mass, 1.0e-6f));
                float impulse_speed = (vel_along < -1.0e-5f) ? vel_along : (soft_body_pair ? 0.0f : -impact_speed * 0.35f);
                float j_impulse = -(1.0f + restitution) * impulse_speed / std::max(inv_mass_sum, 1.0e-6f);
                glm::vec3 impulse = normal * j_impulse;
                bodies[i].vel -= impulse / std::max(bodies[i].mass, 1.0e-6f);
                bodies[j].vel += impulse / std::max(bodies[j].mass, 1.0e-6f);

                // Tangential damping keeps non-fragment impacts from behaving like pinballs.
                glm::vec3 post_rel = bodies[j].vel - bodies[i].vel;
                glm::vec3 tangential = post_rel - normal * glm::dot(post_rel, normal);
                float tangential_len = glm::length(tangential);
                if (tangential_len > 1.0e-6f) {
                    glm::vec3 tangent_dir = tangential / tangential_len;
                    float tangential_reduce = tangential_len * (fragmenting ? 0.80f : (soft_body_pair ? 0.90f : 0.35f));
                    float jt = tangential_reduce / std::max(inv_mass_sum, 1.0e-6f);
                    glm::vec3 tangent_impulse = tangent_dir * jt;
                    bodies[i].vel += tangent_impulse / std::max(bodies[i].mass, 1.0e-6f);
                    bodies[j].vel -= tangent_impulse / std::max(bodies[j].mass, 1.0e-6f);
                }
            }

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            auto can_fragment = [&](const CelestialBody& body) {
                return body.mass >= effective_min_frag_mass &&
                       (int)body.frag_generation < cfg.max_frag_generation &&
                       !is_star_type(body.type) &&
                       !is_black_hole_type(body.type);
            };

            if (fragmenting) {
                bool fragment_i = can_fragment(bodies[i]) &&
                    (disruption_i > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[i].mass <= bodies[j].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));
                bool fragment_j = can_fragment(bodies[j]) &&
                    (disruption_j > (soft_body_pair ? 0.20f : 0.78f) ||
                     (bodies[j].mass <= bodies[i].mass && combined_disruption > (soft_body_pair ? 0.15f : 0.58f)));
                if (cfg.spin_fragmentation && spin_over_i > 0.0f && can_fragment(bodies[i]))
                    fragment_i = true;
                if (cfg.spin_fragmentation && spin_over_j > 0.0f && can_fragment(bodies[j]))
                    fragment_j = true;

                if (combined_disruption > (soft_body_pair ? 0.30f : 0.90f) &&
                    can_fragment(bodies[i]) && can_fragment(bodies[j])) {
                    fragment_i = true;
                    fragment_j = true;
                }
                if (!fragment_i && !fragment_j) {
                    if (bodies[i].mass <= bodies[j].mass && (can_fragment(bodies[i]) || bodies[i].mass > 2.0e-9f))
                        fragment_i = true;
                    else if (can_fragment(bodies[j]) || bodies[j].mass > 2.0e-9f)
                        fragment_j = true;
                }

                int shock_fragments = std::clamp(
                    cfg.fragment_count + (int)std::floor(combined_disruption * (soft_body_pair ? 4.0f : 2.0f)),
                    cfg.fragment_count, 12);
                if (soft_body_pair && !compact_vs_soft)
                    shock_fragments = std::clamp(shock_fragments, 2, 6);
                if (compact_vs_soft)
                    shock_fragments = std::clamp(std::max(2, cfg.fragment_count / 2), 2, 6);
                float ejecta_speed = 0.0f;
                if (soft_body_pair && !compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.40f + combined_disruption * 0.60f) + closing_speed * 0.18f,
                        0.004f, 0.060f);
                } else if (compact_vs_soft) {
                    ejecta_speed = std::clamp(
                        escape_speed * (0.65f + combined_disruption * 0.70f) + closing_speed * 0.28f,
                        0.010f, 0.20f);
                } else {
                    ejecta_speed = std::max(2.0f, impact_speed * (0.32f + combined_disruption * 0.18f));
                }

                if (fragment_i) {
                    bool keep_remnant = false;
                    float removed = bodies[i].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[i].type == CTYPE_PLANET || bodies[i].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.32f + disruption_i * 0.24f +
                            std::min(overlap_fraction, 0.6f) * 0.30f +
                            spin_over_i * 0.16f, 0.10f, 0.70f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.45f : 0.28f);
                        float remnant_mass = bodies[i].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[i].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[i].mass, 1.0e-12f));
                            bodies[i].mass = remnant_mass;
                            bodies[i].radius = std::max(bodies[i].radius * mass_scale, 0.1f);
                            bodies[i].frag_generation = std::min<uint32_t>(bodies[i].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            if (cfg.spin_fragmentation) {
                                float post_spin_cap = spin_fragmentation_critical_omega(bodies[i], cfg.G) *
                                    std::max(spin_threshold * 0.92f, 0.35f);
                                if (post_spin_cap > 0.0f) {
                                    bodies[i].angular_vel = std::copysign(
                                        std::min(std::abs(bodies[i].angular_vel), post_spin_cap),
                                        bodies[i].angular_vel);
                                }
                            }
                            bodies[i].props_valid = false;
                            bodies[i].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[i].pos, bodies[i].vel, removed,
                                        shock_fragments, bodies[i].frag_generation,
                                        bodies[i].temperature, -impact_axis, ejecta_speed,
                                        &bodies[i], std::max(disruption_i, combined_disruption));
                        register_mass_loss(bodies[i], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[i].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[i], normal,
                            std::clamp(disruption_i + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[i].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[i], normal,
                        std::clamp(disruption_i + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[j].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.90f));
                }

                if (fragment_j) {
                    bool keep_remnant = false;
                    float removed = bodies[j].mass;
                    if (soft_body_pair && !compact_vs_soft &&
                        (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON)) {
                        float severity = std::clamp(
                            0.28f + combined_disruption * 0.32f + disruption_j * 0.24f +
                            std::min(overlap_fraction, 0.6f) * 0.30f +
                            spin_over_j * 0.16f, 0.10f, 0.70f);
                        float remnant_fraction = std::max(1.0f - severity,
                            impact_speed < fragment_speed_limit * 1.10f ? 0.45f : 0.28f);
                        float remnant_mass = bodies[j].mass * remnant_fraction;
                        if (remnant_mass > effective_min_frag_mass * 2.0f) {
                            keep_remnant = true;
                            removed = bodies[j].mass - remnant_mass;
                            float mass_scale = std::cbrt(remnant_mass / std::max(bodies[j].mass, 1.0e-12f));
                            bodies[j].mass = remnant_mass;
                            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
                            bodies[j].frag_generation = std::min<uint32_t>(bodies[j].frag_generation + 1u,
                                                                           (uint32_t)cfg.max_frag_generation);
                            if (cfg.spin_fragmentation) {
                                float post_spin_cap = spin_fragmentation_critical_omega(bodies[j], cfg.G) *
                                    std::max(spin_threshold * 0.92f, 0.35f);
                                if (post_spin_cap > 0.0f) {
                                    bodies[j].angular_vel = std::copysign(
                                        std::min(std::abs(bodies[j].angular_vel), post_spin_cap),
                                        bodies[j].angular_vel);
                                }
                            }
                            bodies[j].props_valid = false;
                            bodies[j].visuals_valid = false;
                        }
                    }

                    if (removed > effective_min_frag_mass * 0.25f) {
                        spawn_fragments(bodies[j].pos, bodies[j].vel, removed,
                                        shock_fragments, bodies[j].frag_generation,
                                        bodies[j].temperature, impact_axis, ejecta_speed,
                                        &bodies[j], std::max(disruption_j, combined_disruption));
                        register_mass_loss(bodies[j], removed, std::max(dt, 1.0e-4f));
                    }
                    if (!keep_remnant) {
                        bodies[j].marked_for_removal = true;
                    } else {
                        apply_impact_signature(
                            bodies[j], -normal,
                            std::clamp(disruption_j + combined_disruption * 0.30f, 0.0f, 1.4f),
                            removed / std::max(removed + bodies[j].mass, 1.0e-8f),
                            std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.6f),
                            0.30f);
                    }
                } else {
                    apply_impact_signature(
                        bodies[j], -normal,
                        std::clamp(disruption_j + combined_disruption * 0.20f, 0.0f, 1.2f),
                        bodies[i].mass / std::max(total_mass, 1.0e-6f),
                        std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 1.4f),
                        std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.90f));
                }
            } else if (!is_star_type(bodies[i].type) && !is_black_hole_type(bodies[i].type) &&
                       !is_star_type(bodies[j].type) && !is_black_hole_type(bodies[j].type)) {
                apply_impact_signature(
                    bodies[i], normal,
                    std::clamp(disruption_i * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[j].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[i].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[j].radius / std::max(bodies[i].radius, 0.1f), 0.08f, 0.80f));
                apply_impact_signature(
                    bodies[j], -normal,
                    std::clamp(disruption_j * 0.40f + combined_disruption * 0.16f, 0.0f, 0.70f),
                    bodies[i].mass / std::max(total_mass, 1.0e-6f),
                    std::clamp(impact_energy * cfg.collision_heating / std::max(bodies[j].mass, 1.0e-6f), 0.0f, 0.9f),
                    std::clamp(bodies[i].radius / std::max(bodies[j].radius, 0.1f), 0.08f, 0.80f));
            }
    }
}

// ── Roche Limit ─────────────────────────────────────────────────────────────

void CosmosApp::process_roche_limit(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    bool allow_disruption = cfg.roche_limit && (cfg.roche_limit_fluid || cfg.roche_limit_rigid);
    struct PendingDustRing {
        int host_index = -1;
        float total_mass = 0.0f;
        float inner = 0.0f;
        float outer = 0.0f;
        float density = 0.0f;
        float ice_fraction = 0.0f;
        uint32_t seed_hint = 0u;
    };
    std::vector<PendingDustRing> pending_rings;

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = 0; j < n; ++j) {
            if (i == j || bodies[j].marked_for_removal) continue;
            if (is_black_hole_type(bodies[j].type)) continue;
            if (bodies[i].mass <= bodies[j].mass * 2.5f) continue;

            glm::vec3 rel_vel = bodies[j].vel - bodies[i].vel;
            glm::vec3 delta = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(delta);
            bool fluid_like_secondary = roche_secondary_fluid_like(bodies[j]);
            float roche_fluid = roche_distance_for_mode(bodies[i], bodies[j], true) *
                std::clamp(cfg.roche_fluid_scale, 0.25f, 4.0f);
            float roche_rigid = roche_distance_for_mode(bodies[i], bodies[j], false) *
                std::clamp(cfg.roche_rigid_scale, 0.25f, 4.0f);
            float heating_limit = std::max(roche_fluid, roche_rigid);

            float disruption_limit = 0.0f;
            bool using_fluid_limit = false;
            if (allow_disruption) {
                if (fluid_like_secondary && cfg.roche_limit_fluid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (!fluid_like_secondary && cfg.roche_limit_rigid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else if (cfg.roche_limit_fluid && !cfg.roche_limit_rigid) {
                    disruption_limit = roche_fluid;
                    using_fluid_limit = true;
                } else if (cfg.roche_limit_rigid && !cfg.roche_limit_fluid) {
                    disruption_limit = roche_rigid;
                    using_fluid_limit = false;
                } else {
                    disruption_limit = fluid_like_secondary ? roche_fluid : roche_rigid;
                    using_fluid_limit = fluid_like_secondary;
                }
            }

            glm::vec3 prev_delta = delta - rel_vel * dt;
            glm::vec3 seg = delta - prev_delta;
            float seg_len2 = glm::dot(seg, seg);
            float sweep_t = 0.0f;
            if (seg_len2 > 1.0e-8f)
                sweep_t = std::clamp(-glm::dot(prev_delta, seg) / seg_len2, 0.0f, 1.0f);
            glm::vec3 closest_rel = prev_delta + seg * sweep_t;
            float sweep_dist = glm::length(closest_rel);
            float eval_dist = std::min(dist, sweep_dist);

            float interaction_limit = 0.0f;
            if (cfg.tidal_forces) interaction_limit = heating_limit;
            if (allow_disruption) interaction_limit = std::max(interaction_limit, disruption_limit);
            if (interaction_limit <= 0.0f || eval_dist >= interaction_limit) continue;
            if (eval_dist <= std::max(bodies[i].radius * 1.01f, 1.0e-4f)) continue;

            float heating_overflow = std::clamp((heating_limit - eval_dist) /
                std::max(heating_limit, 1.0e-4f), 0.0f, 1.0f);
            float disruption_overflow = (allow_disruption && disruption_limit > 0.0f)
                ? std::clamp((disruption_limit - eval_dist) / std::max(disruption_limit, 1.0e-4f), 0.0f, 1.0f)
                : 0.0f;
            float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
            float spin_ratio = cfg.spin_fragmentation ? spin_fragmentation_ratio(bodies[j], cfg.G) : 0.0f;
            float spin_overflow = cfg.spin_fragmentation ? std::max(spin_ratio - spin_threshold, 0.0f) : 0.0f;
            bool spin_assisted_disruption = cfg.spin_fragmentation &&
                spin_overflow > 0.0f &&
                eval_dist < heating_limit * (using_fluid_limit ? 1.20f : 1.08f);
            glm::vec3 tidal_axis = (sweep_dist < dist && sweep_dist > 1.0e-5f)
                ? (closest_rel / sweep_dist)
                : glm::normalize(delta);
            if (glm::length(tidal_axis) < 1.0e-5f)
                tidal_axis = glm::vec3(0.0f, 1.0f, 0.0f);

            // Baseline tidal work/heat in Roche zone.
            float tide_strain = cfg.G * bodies[i].mass * bodies[j].radius /
                std::max(eval_dist * eval_dist * eval_dist, 1.0e-6f);
            float tidal_work = tide_strain * (0.45f + heating_overflow * 2.0f) * dt * 1400.0f *
                std::clamp(cfg.tidal_heating_scale, 0.0f, 4.0f);
            if (tidal_work > 0.0f) {
                bodies[j].internal_energy += tidal_work;
                bodies[j].temperature += std::min(tidal_work / std::max(bodies[j].mass * 0.08f, 1.0e-7f), 4000.0f);
                if (bodies[j].type == CTYPE_PLANET || bodies[j].type == CTYPE_MOON) {
                    bodies[j].impact_heat = std::max(
                        bodies[j].impact_heat,
                        std::clamp(heating_overflow * 0.75f, 0.0f, 0.95f));
                    bodies[j].visuals_valid = false;
                }
            }
            if ((!allow_disruption || eval_dist >= disruption_limit) && !spin_assisted_disruption)
                continue;

            float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
            if (bodies[j].mass < effective_min_frag_mass ||
                (int)bodies[j].frag_generation >= cfg.max_frag_generation) {
                continue;
            }

            // Partial Roche stripping (instead of instant full deletion).
            float secondary_mass = bodies[j].mass;
            float local_orbital_speed = std::sqrt(std::max(cfg.G * bodies[i].mass /
                std::max(eval_dist, 1.0e-6f), 0.0f));
            float speed_ratio = glm::length(rel_vel) / std::max(local_orbital_speed, 1.0e-4f);
            float dynamic_overflow = disruption_overflow +
                std::clamp((speed_ratio - 0.8f) * 0.28f, 0.0f, 0.45f) +
                spin_overflow * (using_fluid_limit ? 0.58f : 0.40f);
            float strip_fraction = using_fluid_limit
                ? std::clamp(0.10f + dynamic_overflow * 0.92f, 0.04f, 0.98f)
                : std::clamp(0.06f + dynamic_overflow * 0.66f, 0.02f, 0.86f);
            if (spin_assisted_disruption && eval_dist >= disruption_limit) {
                float spin_only_floor = using_fluid_limit ? 0.10f : 0.06f;
                float spin_only_cap = using_fluid_limit ? 0.40f : 0.28f;
                strip_fraction = std::clamp(std::max(strip_fraction,
                                                     spin_only_floor + spin_overflow * (using_fluid_limit ? 0.18f : 0.12f)),
                                            spin_only_floor, spin_only_cap);
            }
            if (eval_dist < disruption_limit * (using_fluid_limit ? 0.62f : 0.52f) ||
                disruption_overflow > (using_fluid_limit ? 0.72f : 0.82f))
                strip_fraction = std::max(strip_fraction, using_fluid_limit ? 0.93f : 0.78f);
            float stripped_mass = secondary_mass * strip_fraction;
            if (stripped_mass <= 1.0e-9f) continue;

            float ring_fraction = (cfg.planetary_rings && body_can_host_rings(bodies[i]))
                ? std::clamp((using_fluid_limit ? 0.18f : 0.08f) + disruption_overflow * 0.32f, 0.0f, 0.75f)
                : 0.0f;
            float ring_mass = stripped_mass * ring_fraction;
            if (ring_mass > 0.0f) {
                add_ring_material(bodies[i], bodies[j], ring_mass, disruption_limit, cfg);
                pending_rings.push_back(PendingDustRing{
                    (int)i,
                    ring_mass,
                    bodies[i].ring_inner_radius,
                    bodies[i].ring_outer_radius,
                    bodies[i].ring_density,
                    bodies[i].ring_ice_fraction,
                    hash_combine(bodies[i].seed, bodies[j].seed)
                });
                clear_ring_system(bodies[i]);
            }

            float fragment_mass = std::max(stripped_mass - ring_mass, 0.0f);
            if (fragment_mass > 1.0e-9f) {
                int fragment_count = std::clamp(
                    std::max(1, cfg.fragment_count / 2 +
                                (int)std::floor((disruption_overflow + spin_overflow * 0.85f) *
                                                (using_fluid_limit ? 3.8f : 2.4f))),
                                                1, 10);
                float speed_scale = using_fluid_limit ? 0.08f : 0.055f;
                float spin_surface_speed = std::abs(bodies[j].angular_vel) *
                    std::max(bodies[j].radius, bodies[j].type == CTYPE_DUST ? 0.04f : 0.1f);
                float roche_ejecta_speed = std::clamp(
                    local_orbital_speed * (speed_scale + dynamic_overflow * 0.13f) +
                    spin_surface_speed * (using_fluid_limit ? 0.05f : 0.07f),
                                                      0.0003f, using_fluid_limit ? 0.012f : 0.009f);
                spawn_fragments(bodies[j].pos, bodies[j].vel, fragment_mass, fragment_count,
                                bodies[j].frag_generation, bodies[j].temperature,
                                tidal_axis, roche_ejecta_speed,
                                &bodies[j], 0.45f + disruption_overflow * 0.90f + spin_overflow * 0.50f);
            }

            register_mass_loss(bodies[j], stripped_mass, std::max(dt, 1.0e-4f));
            float remaining_mass = std::max(secondary_mass - stripped_mass, 0.0f);
            if (remaining_mass <= effective_min_frag_mass) {
                bodies[j].marked_for_removal = true;
                continue;
            }

            float mass_scale = std::cbrt(remaining_mass / std::max(secondary_mass, 1.0e-8f));
            bodies[j].mass = remaining_mass;
            bodies[j].radius = std::max(bodies[j].radius * mass_scale, 0.1f);
            if (cfg.spin_fragmentation) {
                float post_spin_cap = spin_fragmentation_critical_omega(bodies[j], cfg.G) *
                    std::max(spin_threshold * 0.94f, 0.25f);
                if (post_spin_cap > 0.0f) {
                    float damped_spin = std::abs(bodies[j].angular_vel) * std::max(0.48f, mass_scale * 0.78f);
                    bodies[j].angular_vel = std::copysign(std::min(damped_spin, post_spin_cap), bodies[j].angular_vel);
                }
            }
            bodies[j].props_valid = false;
            bodies[j].visuals_valid = false;
            apply_impact_signature(
                bodies[j], -tidal_axis,
                std::clamp(disruption_overflow * 0.8f + spin_overflow * 0.35f, 0.0f, 1.2f),
                stripped_mass / std::max(secondary_mass, 1.0e-8f),
                std::clamp(tidal_work / std::max(bodies[j].mass, 1.0e-8f), 0.0f, 1.0f),
                std::clamp(0.18f + disruption_overflow * 0.45f + spin_overflow * 0.18f, 0.08f, 0.92f));
        }
    }

    for (const auto& ring : pending_rings) {
        spawn_dust_ring(ring.host_index, ring.total_mass, ring.inner, ring.outer,
                        ring.density, ring.ice_fraction, ring.seed_hint);
    }
}

void CosmosApp::process_spin_fragmentation(float dt) {
    auto& bodies = state.bodies;
    size_t n = bodies.size();
    float effective_min_frag_mass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
    float spin_threshold = std::max(cfg.spin_fragmentation_threshold, 0.05f);
    float dt_limited = std::clamp(dt, 1.0e-4f, 2.0f);

    for (size_t i = 0; i < n; ++i) {
        if (bodies[i].marked_for_removal) continue;
        if (is_star_type(bodies[i].type) || is_black_hole_type(bodies[i].type) ||
            bodies[i].type == CTYPE_NEBULA) {
            continue;
        }

        float critical = spin_fragmentation_critical_omega(bodies[i], cfg.G);
        if (critical <= 0.0f) continue;

        float spin_ratio = spin_fragmentation_ratio(bodies[i], cfg.G);
        if (!std::isfinite(spin_ratio) || spin_ratio <= spin_threshold)
            continue;

        float spin_cap = critical * std::max(spin_threshold * 0.94f, 0.25f);
        if (bodies[i].mass < effective_min_frag_mass * 2.0f ||
            (int)bodies[i].frag_generation >= cfg.max_frag_generation) {
            bodies[i].angular_vel = std::copysign(std::min(std::abs(bodies[i].angular_vel), spin_cap),
                                                  bodies[i].angular_vel);
            continue;
        }

        float overflow = spin_ratio - spin_threshold;
        float max_strip = (bodies[i].type == CTYPE_ASTEROID || bodies[i].type == CTYPE_COMET || bodies[i].type == CTYPE_DUST)
            ? 0.38f : 0.30f;
        float strip_fraction = std::clamp(0.06f + overflow * 0.24f + dt_limited * 0.02f, 0.04f, max_strip);
        if (spin_ratio > spin_threshold * 1.35f)
            strip_fraction = std::max(strip_fraction, max_strip * 0.72f);

        float original_mass = bodies[i].mass;
        float stripped_mass = std::min(original_mass * strip_fraction,
                                       std::max(original_mass - effective_min_frag_mass * 1.25f, 0.0f));
        if (stripped_mass <= effective_min_frag_mass * 0.25f) {
            bodies[i].angular_vel = std::copysign(std::min(std::abs(bodies[i].angular_vel), spin_cap),
                                                  bodies[i].angular_vel);
            continue;
        }

        MaterialComposition materials = derive_materials(bodies[i]);
        bool gas_dominated = gas_dominated_body(bodies[i], materials);
        bool fluid_like = roche_secondary_fluid_like(bodies[i]);
        float radius = std::max(bodies[i].radius, bodies[i].type == CTYPE_DUST ? 0.04f : 0.1f);
        float surface_speed = std::abs(bodies[i].angular_vel) * radius;
        float excess_surface_speed = std::max(surface_speed - critical * radius * spin_threshold, 0.0f);
        float ejecta_speed = std::clamp(
            excess_surface_speed * (gas_dominated ? 0.12f : 0.18f) +
            critical * radius * (fluid_like ? 0.014f : 0.020f),
            0.00025f,
            gas_dominated ? 0.0045f : (bodies[i].type == CTYPE_DUST ? 0.006f : 0.015f));
        int fragment_count = std::clamp(
            std::max(2, cfg.fragment_count / 2 + (int)std::floor(overflow * 5.0f)),
            2, 8);
        glm::vec3 fragment_axis = spin_fragmentation_axis(bodies[i]);
        uint32_t parent_generation = bodies[i].frag_generation;
        float temperature = bodies[i].temperature;

        spawn_fragments(bodies[i].pos, bodies[i].vel, stripped_mass, fragment_count,
                        parent_generation, temperature, fragment_axis, ejecta_speed,
                        &bodies[i], 0.30f + overflow * 0.75f);
        register_mass_loss(bodies[i], stripped_mass, std::max(dt_limited, 1.0e-4f));

        float remaining_mass = std::max(original_mass - stripped_mass, 0.0f);
        if (remaining_mass <= effective_min_frag_mass) {
            bodies[i].marked_for_removal = true;
            continue;
        }

        float mass_scale = std::cbrt(remaining_mass / std::max(original_mass, 1.0e-12f));
        bodies[i].mass = remaining_mass;
        bodies[i].radius = std::max(bodies[i].radius * mass_scale, bodies[i].type == CTYPE_DUST ? 0.06f : 0.1f);
        bodies[i].frag_generation = std::min<uint32_t>(parent_generation + 1u, (uint32_t)cfg.max_frag_generation);
        float post_spin_cap = spin_fragmentation_critical_omega(bodies[i], cfg.G) *
            std::max(spin_threshold * 0.94f, 0.25f);
        if (post_spin_cap > 0.0f) {
            float damped_spin = std::abs(bodies[i].angular_vel) * std::max(0.52f, mass_scale * 0.82f);
            bodies[i].angular_vel = std::copysign(std::min(damped_spin, post_spin_cap), bodies[i].angular_vel);
        }
        if (cfg.temperature_system) {
            float spin_heat = stripped_mass * surface_speed * surface_speed * 0.04f;
            bodies[i].internal_energy += spin_heat;
            bodies[i].temperature += std::min(spin_heat / std::max(bodies[i].mass * 0.08f, 1.0e-7f), 2200.0f);
        }
        bodies[i].props_valid = false;
        bodies[i].visuals_valid = false;
        apply_impact_signature(
            bodies[i], fragment_axis,
            std::clamp(0.24f + overflow * 0.55f, 0.0f, 1.2f),
            stripped_mass / std::max(original_mass, 1.0e-8f),
            std::clamp(surface_speed * 0.08f, 0.0f, 1.0f),
            std::clamp(0.18f + overflow * 0.25f, 0.10f, 0.85f));
    }
}

// ── Temperature ─────────────────────────────────────────────────────────────

void CosmosApp::process_temperature(float dt) {
    auto& bodies = state.bodies;
    const float background = 2.7f;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) && b.fuel > 0.05f) {
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
            continue;
        }
        b.temperature -= cfg.radiative_cooling * (b.temperature - background) * dt;
        if (b.temperature < background) b.temperature = background;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || is_star_type(b.type)) continue;

        float eq_t4_sum = std::pow(background, 4.0f);
        for (size_t j = 0; j < bodies.size(); ++j) {
            if (i == j || bodies[j].marked_for_removal || !is_star_type(bodies[j].type)) continue;
            float eq_t = equilibrium_temperature_from_star(b, bodies[j]);
            eq_t4_sum += std::pow(eq_t, 4.0f);
        }

        float target_temp = std::pow(std::max(eq_t4_sum, std::pow(background, 4.0f)), 0.25f);
        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float greenhouse = 1.0f + std::min(b.cached_props.atmosphere.pressure * 0.015f, 0.35f);
            target_temp *= greenhouse;
        }

        float thermal_response = 0.35f / (0.6f + std::cbrt(std::max(b.mass, 1.0e-5f)) * 0.8f);
        b.temperature += (target_temp - b.temperature) * std::min(thermal_response * dt, 1.0f);
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].marked_for_removal) continue;
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            if (bodies[j].marked_for_removal) continue;

            glm::vec3 diff = bodies[j].pos - bodies[i].pos;
            float dist = glm::length(diff);
            float touch = bodies[i].radius + bodies[j].radius;
            if (dist < touch * 1.15f) {
                float contact = 1.0f - dist / std::max(touch * 1.15f, 1.0e-3f);
                float delta = bodies[j].temperature - bodies[i].temperature;
                float exchange = delta * contact * std::min(0.25f * dt, 0.5f);
                float inv_heat_i = 1.0f / std::max(bodies[i].mass, 0.01f);
                float inv_heat_j = 1.0f / std::max(bodies[j].mass, 0.01f);
                bodies[i].temperature += exchange * inv_heat_i;
                bodies[j].temperature -= exchange * inv_heat_j;
            }

            if (cfg.tidal_forces) {
                size_t big_idx = (bodies[i].mass >= bodies[j].mass) ? i : j;
                size_t small_idx = (big_idx == i) ? j : i;
                CelestialBody& big = bodies[big_idx];
                CelestialBody& small = bodies[small_idx];

                if (dist > std::max(big.radius * 1.05f, 1.0f) && big.mass > small.mass * 2.0f) {
                    bool fluid_like_small = roche_secondary_fluid_like(small);
                    float roche_fluid = roche_distance_for_mode(big, small, true) *
                        std::clamp(cfg.roche_fluid_scale, 0.25f, 4.0f);
                    float roche_rigid = roche_distance_for_mode(big, small, false) *
                        std::clamp(cfg.roche_rigid_scale, 0.25f, 4.0f);
                    float roche_dist = 0.0f;
                    if (cfg.roche_limit_fluid && cfg.roche_limit_rigid)
                        roche_dist = fluid_like_small ? roche_fluid : roche_rigid;
                    else if (cfg.roche_limit_fluid)
                        roche_dist = roche_fluid;
                    else if (cfg.roche_limit_rigid)
                        roche_dist = roche_rigid;
                    else
                        roche_dist = std::max(roche_fluid, roche_rigid);
                    float tidal_reach = std::max(roche_dist * 6.0f, big.radius * 3.0f);
                    if (dist < tidal_reach) {
                        float proximity = 1.0f - std::clamp((dist - big.radius) /
                            std::max(tidal_reach - big.radius, 1.0e-3f), 0.0f, 1.0f);
                        float orbital_rate = std::sqrt(std::max(cfg.G * (big.mass + small.mass) /
                            std::max(dist * dist * dist, 1.0e-6f), 0.0f));
                        float spin_mismatch = std::abs(std::abs(small.angular_vel) - orbital_rate);
                        float shear_speed = glm::length(small.vel - big.vel);
                        float strain = cfg.G * big.mass * small.radius / std::max(dist * dist * dist, 1.0e-6f);
                        float dissipation = strain * strain *
                            (0.20f + spin_mismatch * 3600.0f + shear_speed * 0.12f) *
                            proximity * proximity * dt * 18000.0f *
                            std::clamp(cfg.tidal_heating_scale, 0.0f, 4.0f);
                        if (dissipation > 0.0f) {
                            small.internal_energy += dissipation;
                            small.temperature += std::min(dissipation / std::max(small.mass * 0.08f, 1.0e-7f), 12000.0f);
                            if (small.type == CTYPE_PLANET || small.type == CTYPE_MOON) {
                                small.impact_heat = std::max(small.impact_heat, std::clamp(proximity * 0.75f, 0.0f, 0.95f));
                                small.visuals_valid = false;
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (b.internal_energy > 0.0f) {
            float transfer_rate = 15.0f;
            if (b.impact_heat > 0.01f || b.material_phase == PHASE_MOLTEN || b.material_phase == PHASE_PLASMA)
                transfer_rate = 95.0f;
            float transfer = std::min(b.internal_energy, transfer_rate * dt);
            b.temperature += transfer / std::max(b.mass, 0.01f);
            b.internal_energy -= transfer;
        }

        if (b.impact_heat > 0.0f || b.impact_ejecta > 0.0f || b.impact_crater_strength > 0.0f) {
            float mass_scale = std::clamp(std::cbrt(std::max(b.mass, 1.0e-6f)) + 0.35f, 0.35f, 6.0f);
            b.impact_heat = std::max(0.0f, b.impact_heat - dt * (0.000028f + cfg.radiative_cooling * 0.020f) / mass_scale);
            b.impact_ejecta = std::max(0.0f, b.impact_ejecta - dt * 0.000020f *
                (0.55f + std::clamp(b.atmosphere_retention, 0.0f, 1.0f) * 0.65f));
            b.impact_crater_strength = std::max(0.0f, b.impact_crater_strength - dt * 0.0000018f / mass_scale);
            b.impact_radius = std::max(0.0f, b.impact_radius - dt * 0.0000012f / mass_scale);
        }
    }
}

void CosmosApp::process_material_phases(float dt) {
    auto& bodies = state.bodies;
    float phase_rate = std::clamp(cfg.material_phase_rate, 0.1f, 4.0f);
    struct PendingDustRing {
        int host_index = -1;
        float total_mass = 0.0f;
        float inner = 0.0f;
        float outer = 0.0f;
        float density = 0.0f;
        float ice_fraction = 0.0f;
        uint32_t seed_hint = 0u;
    };
    std::vector<PendingDustRing> pending_rings;

    for (size_t idx = 0; idx < bodies.size(); ++idx) {
        auto& b = bodies[idx];
        if (b.marked_for_removal) continue;

        MaterialComposition materials = derive_materials(b);
        MaterialPhase prev_phase = static_cast<MaterialPhase>(b.material_phase);
        MaterialPhase next_phase = infer_material_phase(b, materials);
        float next_intensity = phase_intensity_for_body(b, next_phase, materials);

        if (cfg.planetary_rings && b.ring_density > 0.001f) {
            if (!body_can_host_rings(b) || is_star_type(b.type) || is_black_hole_type(b.type)) {
                clear_ring_system(b);
            } else {
                float annulus = std::max(b.ring_outer_radius * b.ring_outer_radius -
                                         b.ring_inner_radius * b.ring_inner_radius, 1.0f);
                float mass_hint = std::max(b.mass * b.ring_density * 0.0000020f *
                                           (annulus / std::max(b.radius * b.radius, 1.0e-5f)),
                                           std::max(cfg.min_fragment_mass, 1.0e-12f) * 2.0f);
                pending_rings.push_back(PendingDustRing{
                    (int)idx,
                    mass_hint,
                    b.ring_inner_radius,
                    b.ring_outer_radius,
                    b.ring_density,
                    b.ring_ice_fraction,
                    hash_combine(b.seed, (uint32_t)(idx + 0xD057u))
                });
                clear_ring_system(b);
            }
        }

        if (!is_star_type(b.type) && !is_black_hole_type(b.type)) {
            if ((next_phase == PHASE_MOLTEN || next_phase == PHASE_PLASMA) &&
                (b.type == CTYPE_PLANET || b.type == CTYPE_MOON ||
                 b.type == CTYPE_ASTEROID || b.type == CTYPE_COMET || b.type == CTYPE_DUST)) {
                float boiloff = std::clamp((b.temperature - 900.0f) / 1800.0f, 0.0f, 1.0f);
                b.internal_energy += boiloff * dt * 1.8f * phase_rate;
                if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
                    float prev_retention = b.atmosphere_retention;
                    b.atmosphere_retention = std::clamp(
                        b.atmosphere_retention - boiloff * dt * 0.0012f * phase_rate, 0.0f, 1.0f);
                    if (std::abs(prev_retention - b.atmosphere_retention) > 1.0e-6f)
                        b.props_valid = false;
                }
            }

            bool gas_dominated = gas_dominated_body(b, materials);
            // Only nebulae collapse toward stars — gas giant planets should NOT ignite.
            if (b.type == CTYPE_NEBULA &&
                b.mass >= HYDROGEN_BURNING_MASS_SOLAR * 0.65f) {
                float gravity_drive = std::clamp(cfg.G * b.mass /
                    std::max(b.radius * b.radius, 1.0e-6f), 0.0f, 2.0f);
                float mass_drive = std::clamp((b.mass / HYDROGEN_BURNING_MASS_SOLAR) - 0.65f, 0.0f, 2.5f);
                float temp_drive = std::clamp((b.temperature - 250.0f) / 2800.0f, 0.0f, 1.2f);
                float energy_drive = std::clamp(b.internal_energy / std::max(b.mass * 30.0f, 0.04f), 0.0f, 1.4f);
                float collapse_drive = mass_drive * 0.70f + gravity_drive * 0.30f +
                                       temp_drive * 0.35f + energy_drive * 0.25f +
                                       (b.type == CTYPE_NEBULA ? 0.08f : 0.0f);
                if (collapse_drive > 0.15f) {
                    // Slow collapse — nebulae should persist as clouds for a long time.
                    float advance = std::min(0.04f, dt * (0.00035f + collapse_drive * 0.0012f) * phase_rate);
                    b.collapse_progress = std::clamp(b.collapse_progress + advance, 0.0f, 1.25f);
                    next_phase = PHASE_COLLAPSING;
                    next_intensity = std::max(next_intensity, std::clamp(b.collapse_progress, 0.15f, 1.0f));

                    float proto_temp = std::max(250.0f,
                        expected_main_sequence_temperature(std::max(b.mass, 0.003f)) * 0.88f);
                    // Very gradual heating — cloud should stay cool for extended periods.
                    b.temperature += (proto_temp - b.temperature) * std::min(0.012f * dt, 0.06f);

                    CelestialBody proto = b;
                    proto.type = classify_star_spectral(proto_temp, std::max(proto.mass, 0.003f));
                    proto.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                    float target_radius = expected_star_radius(proto) * 1.35f;
                    b.radius = glm::mix(b.radius, target_radius, std::min(0.10f * dt, 0.22f));
                    b.internal_energy += collapse_drive * dt * 2.8f;

                    if (b.collapse_progress >= 1.0f && b.temperature >= 4500.0f) {
                        b.type = classify_star_spectral(std::max(b.temperature, proto_temp), std::max(b.mass, 0.003f));
                        b.stellar_stage = SSTAGE_MAIN_SEQUENCE;
                        b.fuel = std::max(b.fuel, 0.92f);
                        b.temperature = std::clamp(std::max(b.temperature, proto_temp), 250.0f, 60000.0f);
                        b.radius = expected_star_radius(b);
                        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                                   b.stellar_stage, b.fuel);
                        b.material_phase = PHASE_PLASMA;
                        b.phase_intensity = 1.0f;
                        b.collapse_progress = 1.0f;
                        clear_ring_system(b);
                        b.props_valid = false;
                        b.visuals_valid = false;
                        continue;
                    }
                } else {
                    b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
                }
            } else {
                b.collapse_progress = std::max(0.0f, b.collapse_progress - dt * 0.0004f);
            }
        } else {
            if (b.ring_density > 0.0f)
                clear_ring_system(b);
            b.material_phase = PHASE_PLASMA;
            b.phase_intensity = 1.0f;
            continue;
        }

        if (next_phase != prev_phase ||
            std::abs(next_intensity - b.phase_intensity) > 0.03f) {
            b.props_valid = false;
            b.visuals_valid = false;
        }
        b.material_phase = next_phase;
        b.phase_intensity = next_intensity;
    }

    for (const auto& ring : pending_rings) {
        spawn_dust_ring(ring.host_index, ring.total_mass, ring.inner, ring.outer,
                        ring.density, ring.ice_fraction, ring.seed_hint);
    }
}

void CosmosApp::process_space_weather(float dt) {
    auto& bodies = state.bodies;
    constexpr float kPi = 3.14159265359f;

    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& target = bodies[i];
        if (target.marked_for_removal) continue;
        if (is_star_type(target.type) || is_black_hole_type(target.type)) continue;

        float stellar_flux = 0.0f;
        float quasar_flux = 0.0f;
        float particle_flux = estimate_space_weather_flux(target, &state, cfg.G, &stellar_flux, &quasar_flux);
        if (particle_flux <= 0.0f) continue;

        float shielding = magnetic_shielding_score(target, cfg.G);
        float unshielded_flux = particle_flux * (1.0f - std::min(shielding, 1.0f) * 0.72f);
        float heating_flux = stellar_flux * (0.22f + (1.0f - std::min(shielding, 1.0f)) * 0.55f) +
                             quasar_flux * (0.95f + (1.0f - std::min(shielding, 1.0f)) * 0.85f);

        if (cfg.stellar_wind_pressure) {
            glm::vec3 wind_accel(0.0f);
            float cross_section = kPi * std::max(target.radius * target.radius, 1.0e-4f);
            float mass = std::max(target.mass, 1.0e-9f);
            float shield_scale = std::clamp(1.0f - std::min(shielding, 1.25f) * 0.45f, 0.15f, 1.0f);
            float type_scale = (target.type == CTYPE_DUST) ? 2.8f :
                               (target.type == CTYPE_COMET ? 2.1f :
                               (target.type == CTYPE_ASTEROID ? 1.3f :
                               ((target.cached_props.surface == SURF_GAS) ? 0.45f : 0.85f)));
            for (size_t j = 0; j < bodies.size(); ++j) {
                if (i == j) continue;
                const CelestialBody& source = bodies[j];
                if (source.marked_for_removal || !is_star_type(source.type))
                    continue;
                glm::vec3 delta = target.pos - source.pos;
                float dist2 = std::max(glm::dot(delta, delta), source.radius * source.radius * 4.0f);
                float dist = std::sqrt(dist2);
                if (dist <= 1.0e-5f)
                    continue;
                float luminosity = std::max(estimate_stellar_luminosity_units(source), 0.0f);
                float pressure = cfg.stellar_wind_pressure_scale * luminosity /
                                 std::max(4.0f * kPi * dist2 * std::max(cfg.speed_of_light, 1.0f), 1.0e-6f);
                float accel_mag = pressure * cross_section * shield_scale * type_scale / mass;
                wind_accel += (delta / dist) * accel_mag;
            }
            if (glm::dot(wind_accel, wind_accel) > 0.0f)
                target.vel += wind_accel * dt;
        }

        if (cfg.temperature_system) {
            float heat_gain = heating_flux * dt * std::max(target.radius, 0.5f) * 0.55f;
            target.internal_energy += heat_gain;
            target.temperature += heat_gain / std::max(target.mass * 8.0f, 0.02f);
        }

        if (cfg.evaporation &&
            (target.type == CTYPE_PLANET || target.type == CTYPE_MOON) &&
            target.cached_props.atmosphere.pressure > 0.001f) {
            float escape = body_escape_velocity(target, cfg.G);
            float retention_resist = 0.20f + escape * 0.30f + shielding * 0.90f;
            float erosion = unshielded_flux * (0.0004f + cfg.evaporation_rate * 0.0022f) * dt /
                            std::max(retention_resist, 0.08f);
            if (target.cached_props.planet_class == PCLASS_GAS_GIANT ||
                target.cached_props.planet_class == PCLASS_ICE_GIANT) {
                erosion *= 0.10f;
            } else if (target.cached_props.planet_class == PCLASS_DWARF) {
                erosion *= 1.45f;
            }

            float prev_retention = target.atmosphere_retention;
            target.atmosphere_retention = std::clamp(target.atmosphere_retention - erosion, 0.0f, 1.0f);
            if (std::abs(target.atmosphere_retention - prev_retention) > 1.0e-6f) {
                target.props_valid = false;
                target.visuals_valid = false;
                float proxy_loss = std::min(target.mass * erosion * 0.01f, target.mass * 0.0015f);
                register_mass_loss(target, proxy_loss, dt);
            }
        }
    }
}

// ── Evaporation ─────────────────────────────────────────────────────────────

void CosmosApp::process_evaporation(float dt) {
    auto& bodies = state.bodies;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type)) continue;

        if (b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            float vapor_threshold = 650.0f;
            if (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                b.cached_props.planet_class == PCLASS_ICE_GIANT) {
                vapor_threshold = 900.0f;
            }

            if (b.temperature > vapor_threshold) {
                float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
                float escape = body_escape_velocity(b, cfg.G);
                float escape_resist = 0.18f + escape * 0.42f;
                float class_scale = (b.cached_props.planet_class == PCLASS_GAS_GIANT ||
                                     b.cached_props.planet_class == PCLASS_ICE_GIANT) ? 0.45f : 1.0f;
                float loss = cfg.evaporation_rate * heat_factor * dt * 0.060f * class_scale /
                             std::max(escape_resist, 0.12f);
                loss = std::min(loss, b.mass * 0.030f);
                if (loss > 0.0f) {
                    b.mass -= loss;
                    b.radius = std::max(b.radius * 0.99985f, 0.08f);
                    b.atmosphere_retention = std::max(0.0f, b.atmosphere_retention - loss / std::max(b.mass + loss, 1.0e-12f));
                    b.props_valid = false;
                    b.visuals_valid = false;
                    register_mass_loss(b, loss, dt);
                }
            }
            continue;
        }

        if (b.type != CTYPE_ASTEROID && b.type != CTYPE_COMET &&
            b.type != CTYPE_NEBULA && b.type != CTYPE_DUST) continue;

        float vapor_threshold = (b.type == CTYPE_DUST) ? 520.0f :
                                ((b.type == CTYPE_COMET) ? 220.0f : 700.0f);
        if (b.temperature <= vapor_threshold) continue;

        float heat_factor = (b.temperature - vapor_threshold) / std::max(vapor_threshold, 1.0f);
        float loss = cfg.evaporation_rate * heat_factor * dt *
            (b.type == CTYPE_NEBULA ? 0.12f : (b.type == CTYPE_COMET ? 0.07f : (b.type == CTYPE_DUST ? 0.018f : 0.05f)));
        loss = std::min(loss, b.mass * (b.type == CTYPE_DUST ? 0.03f : 0.08f));
        if (loss <= 0.0f) continue;

        b.mass -= loss;
        b.radius = std::max(b.radius * (b.type == CTYPE_DUST ? 0.99985f : 0.9996f),
                            b.type == CTYPE_DUST ? 0.06f : 0.05f);
        register_mass_loss(b, loss, dt);

        if (b.mass < (b.type == CTYPE_DUST ? 8.0e-12f : 5.0e-11f)) {
            b.marked_for_removal = true;
        }
    }
}

// ── Stellar Evolution ───────────────────────────────────────────────────────

void CosmosApp::process_stellar_evolution(float dt) {
    auto& bodies = state.bodies;
    const float dt_step = std::abs(dt);
    if (dt_step <= 0.0f)
        return;

    struct PendingSinkStar {
        CelestialBody body;
    };
    std::vector<PendingSinkStar> pending_sink_stars;
    pending_sink_stars.reserve(16);

    // Dense nebula sink creation: convert converging cloud cores into attracting
    // protostar/star particles and consume gas mass from the host cloud.
    if (cfg.nebula_sink_formation) for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& host = bodies[i];
        if (host.marked_for_removal) continue;
        if (is_star_type(host.type) || is_black_hole_type(host.type)) continue;
        if (host.type != CTYPE_NEBULA) continue;
        if (host.non_attracting) continue;
        if (host.mass < std::max(cfg.nebula_sink_min_mass * 1.5f, 1.0e-7f)) continue;

        float sink_radius = std::max(host.radius * 1.1f, 20.0f);
        float converging = 0.0f;
        float local_feed = 0.0f;
        glm::vec3 mean_flow(0.0f);

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            const CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;
            MaterialComposition dm = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, dm)) continue;

            glm::vec3 delta = donor.pos - host.pos;
            float dist = glm::length(delta);
            if (dist > sink_radius + donor.radius * 2.0f) continue;
            glm::vec3 rel_v = donor.vel - host.vel;
            float weight = 1.0f - std::clamp(dist / std::max(sink_radius, 1.0e-5f), 0.0f, 1.0f);
            if (weight <= 0.0f) continue;
            local_feed += donor.mass * weight;
            mean_flow += rel_v * weight;
            if (dist > 1.0e-5f) {
                float inward = -glm::dot(rel_v, delta) / std::max(dist, 1.0e-5f);
                if (inward > 0.0f) converging += inward * weight;
            }
        }

        float density = body_density(host);
        float gravity_drive = cfg.G * host.mass / std::max(host.radius * host.radius, 1.0e-6f);
        float collapse_metric =
            density * 2.0e6f +
            converging * 0.20f +
            gravity_drive * 0.50f +
            host.collapse_progress * 1.50f +
            host.phase_intensity * 0.15f;
        if (collapse_metric < cfg.nebula_sink_threshold) continue;

        float spawn_frac = std::clamp(cfg.nebula_sink_spawn_fraction, 0.001f, 0.50f);
        float spawn_mass = std::max(host.mass * spawn_frac, local_feed * 0.22f);
        spawn_mass = std::clamp(spawn_mass, std::max(cfg.nebula_sink_min_mass, 1.0e-7f), host.mass * 0.28f);
        if (spawn_mass < std::max(cfg.nebula_sink_min_mass, 1.0e-7f)) continue;

        CelestialBody sink = host;
        sink.mass = spawn_mass;
        float seed_phase = hash_float(hash_combine(host.seed, (uint32_t)(i * 2654435761u + 17u)));
        float seed_phi = hash_float(hash_combine(host.seed, (uint32_t)(i * 2246822519u + 29u))) * 6.28318530718f;
        float z = seed_phase * 2.0f - 1.0f;
        float rxy = std::sqrt(std::max(0.0f, 1.0f - z * z));
        glm::vec3 dir(std::cos(seed_phi) * rxy, z, std::sin(seed_phi) * rxy);
        sink.pos = host.pos + dir * std::max(host.radius * 0.22f, 8.0f);
        if (glm::length(mean_flow) > 1.0e-6f)
            sink.vel = host.vel + glm::normalize(mean_flow) * std::min(0.35f * glm::length(mean_flow), 2.5f);
        else
            sink.vel = host.vel;

        sink.stellar_stage = SSTAGE_MAIN_SEQUENCE;
        float proto_temp = std::max(220.0f, expected_main_sequence_temperature(std::max(sink.mass, 0.003f)) * 0.54f);
        sink.temperature = std::clamp(std::max(host.temperature, proto_temp), 120.0f, 26000.0f);
        sink.type = classify_star_spectral(std::max(sink.temperature, 250.0f), std::max(sink.mass, 0.003f));
        sink.radius = std::max(expected_star_radius(sink) * 1.75f, std::cbrt(std::max(sink.mass, 1.0e-8f)) * 44.0f);
        sink.fuel = std::clamp(std::max(host.fuel, 0.65f), 0.25f, 1.0f);
        sink.luminosity = expected_stellar_luminosity(sink.mass, sink.temperature, sink.radius,
                                                      sink.stellar_stage, sink.fuel);
        sink.material_phase = PHASE_COLLAPSING;
        sink.phase_intensity = std::max(host.phase_intensity, 0.55f);
        sink.collapse_progress = std::clamp(host.collapse_progress + 0.18f, 0.20f, 0.98f);
        sink.non_attracting = false;
        sink.props_valid = false;
        sink.visuals_valid = false;
        clear_ring_system(sink);
        clear_impact_signature(sink);
        sink.name = generate_body_name(hash_combine(host.seed, (uint32_t)(i * 747796405u + 0x53544B52u)), sink.type);

        float consumed = spawn_mass * std::clamp(cfg.nebula_sink_consume_fraction, 0.05f, 1.0f);
        host.mass = std::max(host.mass - consumed, 1.0e-8f);
        host.collapse_progress = std::clamp(host.collapse_progress + 0.04f, 0.0f, 1.35f);
        host.internal_energy += consumed * 2.5f;
        host.temperature = std::clamp(host.temperature + collapse_metric * 6.0f, 15.0f, 60000.0f);
        host.props_valid = false;
        host.visuals_valid = false;
        register_mass_loss(host, consumed, std::max(dt_step, 1.0e-4f));

        pending_sink_stars.push_back(PendingSinkStar{sink});
    }

    std::vector<float> recent_star_accretion(bodies.size(), 0.0f);

    // Cloud concentration: dense gas/dust environments collapse toward protostars.
    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& host = bodies[i];
        if (host.marked_for_removal) continue;
        if (is_star_type(host.type) || is_black_hole_type(host.type)) continue;

        bool cloud_host = (host.type == CTYPE_NEBULA) ||
            ((host.type == CTYPE_DUST || host.type == CTYPE_COMET) &&
             host.mass > 2.0e-4f && !host.non_attracting);
        if (!cloud_host) continue;

        float host_mass_before = std::max(host.mass, 1.0e-12f);
        float capture = cloud_capture_radius(host);
        float mass_gain = 0.0f;
        float hydrogen_gain = 0.0f;
        float concentration_gain = 0.0f;

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;

            MaterialComposition donor_m = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, donor_m)) continue;

            glm::vec3 delta = donor.pos - host.pos;
            float dist = glm::length(delta);
            if (dist > capture + donor.radius * 1.8f) continue;

            glm::vec3 rel_v = donor.vel - host.vel;
            float rel_speed = glm::length(rel_v);
            float esc = std::sqrt(std::max(2.0f * cfg.G * (host.mass + donor.mass) /
                                           std::max(dist, 1.0e-5f), 0.0f));
            if (rel_speed > esc * 1.85f && dist > host.radius * 1.15f) continue;

            float proximity = 1.0f - std::clamp(dist / std::max(capture + donor.radius, 1.0e-5f), 0.0f, 1.0f);
            float absorb = (0.06f + proximity * 0.36f +
                            (donor.non_attracting ? 0.28f : 0.0f) +
                            std::clamp((esc - rel_speed) / std::max(esc, 1.0e-5f), -0.12f, 0.25f)) * dt_step;
            if (dist < host.radius * 0.95f)
                absorb = std::max(absorb, 0.92f);
            absorb = std::clamp(absorb, 0.0f, 1.0f);
            if (absorb <= 1.0e-5f) continue;

            float gained = donor.mass * absorb;
            if (gained <= 1.0e-12f) continue;

            float donor_before = donor.mass;
            donor.mass = std::max(donor.mass - gained, 0.0f);
            register_mass_loss(donor, gained, std::max(dt_step, 1.0e-4f));
            concentration_gain += gained * (0.45f + proximity * 0.55f);
            mass_gain += gained;
            hydrogen_gain += gained * donor_m.hydrogen;

            if (donor.mass <= std::max(1.0e-12f, cfg.min_fragment_mass * 0.10f)) {
                donor.marked_for_removal = true;
            } else {
                float mass_scale = std::cbrt(donor.mass / std::max(donor_before, 1.0e-12f));
                donor.radius = std::max(donor.radius * mass_scale, donor.type == CTYPE_DUST ? 0.06f : 0.1f);
                donor.props_valid = false;
                donor.visuals_valid = false;
            }
        }

        if (mass_gain <= 0.0f) continue;

        host.mass += mass_gain;
        float gain_ratio = mass_gain / std::max(host_mass_before, 1.0e-8f);
        float concentration_ratio = concentration_gain / std::max(host_mass_before, 1.0e-8f);
        host.temperature = std::clamp(host.temperature +
            std::min(200.0f * gain_ratio + concentration_ratio * 60.0f, 1500.0f),
            15.0f, 60000.0f);
        host.internal_energy += mass_gain * (4.0f + concentration_ratio * 3.0f);
        host.collapse_progress = std::clamp(
            host.collapse_progress + gain_ratio * 0.12f + concentration_ratio * 0.06f + dt_step * 0.00008f,
            0.0f, 1.35f);

        // Only promote dust/comet to nebula — never convert planets/moons.
        if (host.type != CTYPE_NEBULA && host.mass > 8.0e-4f &&
            (host.type == CTYPE_DUST || host.type == CTYPE_COMET)) {
            host.type = CTYPE_NEBULA;
            host.material_phase = PHASE_GAS;
            host.phase_intensity = std::max(host.phase_intensity, 0.45f);
        }

        host.fuel = std::clamp((host.fuel * host_mass_before + hydrogen_gain * 0.90f) /
                               std::max(host.mass, 1.0e-8f), 0.0f, 1.0f);

        float proto_threshold = HYDROGEN_BURNING_MASS_SOLAR * 0.92f;
        bool ignite = host.mass >= proto_threshold &&
            host.collapse_progress > 0.85f &&
            host.temperature > 3500.0f;
        if (ignite) {
            float proto_temp = std::max(expected_main_sequence_temperature(std::max(host.mass, 0.003f)) * 0.96f, 250.0f);
            host.type = classify_star_spectral(std::max(host.temperature, proto_temp), std::max(host.mass, 0.003f));
            host.stellar_stage = SSTAGE_MAIN_SEQUENCE;
            host.fuel = std::clamp(std::max(host.fuel, 0.72f), 0.12f, 1.0f);
            host.temperature = std::clamp(std::max(host.temperature, proto_temp), 250.0f, 60000.0f);
            host.radius = expected_star_radius(host);
            host.luminosity = expected_stellar_luminosity(host.mass, host.temperature, host.radius,
                                                          host.stellar_stage, host.fuel);
            host.material_phase = PHASE_PLASMA;
            host.phase_intensity = 1.0f;
            host.collapse_progress = 1.0f;
            clear_ring_system(host);
        } else {
            float target_radius = std::max(18.0f, std::cbrt(std::max(host.mass, 1.0e-8f)) * 105.0f);
            host.radius = glm::mix(host.radius, target_radius, std::clamp(0.02f + gain_ratio * 0.22f, 0.02f, 0.30f));
            host.material_phase = PHASE_COLLAPSING;
            host.phase_intensity = std::max(host.phase_intensity, std::clamp(host.collapse_progress, 0.2f, 1.0f));
        }

        host.props_valid = false;
        host.visuals_valid = false;
    }

    for (const auto& p : pending_sink_stars) {
        CelestialBody sink = p.body;
        refresh_body_render_state(sink, &state);
        state.bodies.push_back(sink);
        state.trails.emplace_back();
    }
    recent_star_accretion.resize(bodies.size(), 0.0f);

    // Stellar accretion: stars gain mass and hydrogen fuel from nearby feedstock.
    for (size_t i = 0; i < bodies.size(); ++i) {
        CelestialBody& star = bodies[i];
        if (star.marked_for_removal) continue;
        if (!is_star_type(star.type)) continue;

        float mass_before = std::max(star.mass, 1.0e-8f);
        float capture = stellar_capture_radius(star);
        float mass_gain = 0.0f;
        float hydrogen_gain = 0.0f;
        float kinetic_gain = 0.0f;

        for (size_t j = 0; j < bodies.size(); ++j) {
            if (j == i) continue;
            CelestialBody& donor = bodies[j];
            if (donor.marked_for_removal) continue;
            if (is_star_type(donor.type) || is_black_hole_type(donor.type)) continue;

            MaterialComposition donor_m = derive_materials(donor);
            if (!body_is_cloud_feedstock(donor, donor_m)) continue;

            float donor_reach = capture + donor.radius * (donor.type == CTYPE_DUST ? 6.0f : 2.0f);
            glm::vec3 delta = donor.pos - star.pos;
            float dist = glm::length(delta);
            if (dist > donor_reach) continue;

            glm::vec3 rel_v = donor.vel - star.vel;
            float rel_speed = glm::length(rel_v);
            float esc = std::sqrt(std::max(2.0f * cfg.G * star.mass / std::max(dist, 1.0e-5f), 0.0f));
            if (rel_speed > esc * 2.4f && dist > star.radius * 1.1f) continue;

            float proximity = 1.0f - std::clamp(dist / std::max(donor_reach, 1.0e-5f), 0.0f, 1.0f);
            float absorb = (0.09f + proximity * 0.78f +
                            (donor.non_attracting ? 0.25f : 0.0f) +
                            std::clamp((esc - rel_speed) / std::max(esc, 1.0e-5f), -0.12f, 0.42f)) * dt_step;
            if (dist <= star.radius * 1.05f)
                absorb = std::max(absorb, 0.98f);
            absorb = std::clamp(absorb, 0.0f, 1.0f);
            if (absorb <= 1.0e-5f) continue;

            float gained = donor.mass * absorb;
            if (gained <= 1.0e-12f) continue;

            float donor_before = donor.mass;
            donor.mass = std::max(donor.mass - gained, 0.0f);
            register_mass_loss(donor, gained, std::max(dt_step, 1.0e-4f));
            mass_gain += gained;
            hydrogen_gain += gained * donor_m.hydrogen;
            kinetic_gain += 0.5f * gained * rel_speed * rel_speed;

            if (donor.mass <= std::max(1.0e-12f, cfg.min_fragment_mass * 0.10f)) {
                donor.marked_for_removal = true;
            } else {
                float mass_scale = std::cbrt(donor.mass / std::max(donor_before, 1.0e-12f));
                donor.radius = std::max(donor.radius * mass_scale, donor.type == CTYPE_DUST ? 0.06f : 0.1f);
                donor.props_valid = false;
                donor.visuals_valid = false;
            }
        }

        if (mass_gain <= 0.0f) continue;

        star.mass += mass_gain;
        float acc_ratio = mass_gain / std::max(mass_before, 1.0e-8f);
        float fuel_mass = star.fuel * mass_before + hydrogen_gain * 0.92f + mass_gain * 0.03f;
        star.fuel = std::clamp(fuel_mass / std::max(star.mass, 1.0e-8f) +
                               std::clamp(acc_ratio * 0.06f, 0.0f, 0.10f), 0.0f, 1.0f);
        float heating = kinetic_gain / std::max(star.mass, 1.0e-8f) * 0.65f + acc_ratio * 1800.0f;
        star.temperature = std::clamp(star.temperature + std::min(heating, 22000.0f), 1800.0f, 140000.0f);
        star.internal_energy += kinetic_gain * 0.18f + mass_gain * 10.0f;
        star.type = classify_star_spectral(std::max(star.temperature, 250.0f), std::max(star.mass, 0.003f));
        star.radius = glm::mix(star.radius, expected_star_radius(star),
                               std::clamp(0.04f + acc_ratio * 0.55f, 0.04f, 0.32f));
        star.luminosity = expected_stellar_luminosity(star.mass, star.temperature, star.radius,
                                                      star.stellar_stage, star.fuel);
        star.props_valid = false;
        star.visuals_valid = false;
        recent_star_accretion[i] = mass_gain;
    }

    for (size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal) continue;
        if (!is_star_type(b.type)) continue;

        if (b.stellar_stage == SSTAGE_WHITE_DWARF && b.mass >= CHANDRASEKHAR_LIMIT_SOLAR) {
            trigger_stellar_supernova(i, dt_step, true);
            continue;
        }

        float accreted_mass = recent_star_accretion[i];
        if (accreted_mass > 0.0f) {
            float pre_mass = std::max(b.mass - accreted_mass, 1.0e-8f);
            float acc_ratio = accreted_mass / pre_mass;
            bool runaway_supernova =
                b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR &&
                ((acc_ratio > 0.18f && b.fuel < 0.45f) ||
                 (b.mass > 40.0f && acc_ratio > 0.08f && b.fuel < 0.30f));
            bool collapse_to_bh =
                (b.mass > 85.0f) ||
                (b.mass > 60.0f && acc_ratio > 0.22f && b.fuel < 0.25f);
            if (runaway_supernova || collapse_to_bh) {
                float escape = body_escape_velocity(b, cfg.G);
                float ejecta_speed = std::max(escape * (collapse_to_bh ? 0.65f : 0.85f), 24.0f);
                trigger_stellar_supernova(i, dt_step, false, glm::vec3(0.0f, 1.0f, 0.0f), ejecta_speed);
                continue;
            }
        }

        bool evolved_star =
            b.stellar_stage == SSTAGE_RED_GIANT ||
            b.stellar_stage == SSTAGE_SUPERGIANT ||
            b.stellar_stage == SSTAGE_HYPERGIANT;
        bool compact_star =
            b.stellar_stage == SSTAGE_WHITE_DWARF ||
            b.stellar_stage == SSTAGE_NEUTRON_STAR;

        // Main-sequence lifetime roughly scales as M^-2.5, with post-main-sequence
        // phases burning the remaining fuel much faster.
        float stellar_mass = std::clamp(b.mass, 0.08f, 120.0f);
        float lifetime_scale = cfg.stellar_timescale * 250000.0f;
        float main_sequence_lifetime = lifetime_scale / std::pow(stellar_mass, 2.5f);
        float burn_multiplier = compact_star ? 0.0f : (evolved_star ? 7.5f : 1.0f);
        float burn_rate = (burn_multiplier > 0.0f)
            ? dt_step * burn_multiplier / std::max(main_sequence_lifetime, cfg.stellar_timescale * 1200.0f)
            : 0.0f;
        b.fuel -= burn_rate;
        if (b.fuel < 0.0f) b.fuel = 0.0f;

        float wind_factor = compact_star ? 0.0f : (evolved_star ? 2.6f : 1.0f);
        float wind_loss = std::min(b.mass * wind_factor * (0.00002f + b.luminosity * 0.000002f) * dt_step,
                                   b.mass * (compact_star ? 0.0f : 0.005f));
        if (wind_loss > 0.0f) {
            b.mass -= wind_loss;
            register_mass_loss(b, wind_loss, dt_step);
        }

        // Reclassify spectral type as temperature changes
        b.type = classify_star_spectral(b.temperature, b.mass);

        // Main sequence → evolved giant / supergiant
        float giant_threshold = (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.45f : 0.30f;
        if (b.stellar_stage == SSTAGE_MAIN_SEQUENCE && b.fuel < giant_threshold) {
            if (b.mass >= 20.0f)
                b.stellar_stage = SSTAGE_HYPERGIANT;
            else if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR)
                b.stellar_stage = SSTAGE_SUPERGIANT;
            else
                b.stellar_stage = SSTAGE_RED_GIANT;
            b.radius = expected_star_radius(b);
            b.temperature = std::clamp(b.temperature *
                ((b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) ? 0.66f : 0.54f), 2800.0f, 32000.0f);
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
        }

        if (evolved_star && b.fuel < 0.05f) {
            if (b.mass >= CORE_COLLAPSE_MIN_MASS_SOLAR) {
                trigger_stellar_supernova(i, dt_step, false);
                continue;
            }

            // Lower-mass stars end as white dwarfs after envelope loss.
            b.stellar_stage = SSTAGE_WHITE_DWARF;
            float lost = std::max(b.mass - std::clamp(0.48f + b.mass * 0.10f, 0.45f, 1.30f), 0.0f);
            if (lost > 0.0f) {
                b.mass -= lost;
                register_mass_loss(b, lost, dt_step);
            }
            b.temperature = std::clamp(22000.0f + b.mass * 4000.0f, 9000.0f, 120000.0f);
            b.radius = expected_star_radius(b);
            b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                       b.stellar_stage, b.fuel);
            b.type = classify_star_spectral(std::max(b.temperature, 250.0f), std::max(b.mass, 0.003f));
        }

        b.luminosity = expected_stellar_luminosity(b.mass, b.temperature, b.radius,
                                                   b.stellar_stage, b.fuel);
    }
}

// ── Tidal locking ────────────────────────────────────────────────────────────

void CosmosApp::process_tidal_locking(float dt) {
    auto& bodies = state.bodies;
    const size_t n = bodies.size();
    const float rate = std::max(cfg.tidal_locking_rate, 0.0f);
    if (rate <= 0.0f) return;

    for (size_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal || b.non_attracting) continue;
        if (is_star_type(b.type) || is_black_hole_type(b.type) || b.type == CTYPE_NEBULA) continue;

        int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
        if (primary < 0 || primary >= (int)n) continue;
        const auto& host = bodies[(size_t)primary];
        if (host.marked_for_removal) continue;

        glm::vec3 diff = host.pos - b.pos;
        float dist = glm::length(diff);
        if (dist < 1.0e-3f) continue;

        // Tidal torque ∝ (M_host * R_body²) / dist³
        float tidal_strength = (host.mass * b.radius * b.radius) /
                               (dist * dist * dist);
        tidal_strength *= rate * 50.0f;

        // Target: synchronous rotation = orbital angular velocity
        float orbital_speed = glm::length(b.vel - host.vel);
        float target_angular_vel = orbital_speed / std::max(dist, 0.01f);

        float delta = target_angular_vel - std::abs(b.angular_vel);
        float adjustment = std::clamp(tidal_strength * std::abs(dt), 0.0f, 1.0f);
        float sign = (b.angular_vel >= 0.0f) ? 1.0f : -1.0f;

        b.angular_vel += sign * delta * adjustment;
        b.tidal_lock_progress = std::clamp(
            1.0f - std::abs(delta) / std::max(target_angular_vel, 1.0e-6f),
            0.0f, 1.0f);
    }
}

// ── Orbital elements computation ─────────────────────────────────────────────

void CosmosApp::process_orbital_elements() {
    auto& bodies = state.bodies;
    const size_t n = bodies.size();

    for (size_t i = 0; i < n; ++i) {
        auto& b = bodies[i];
        if (b.marked_for_removal) continue;

        int primary = (tracked_primary_.size() > i) ? tracked_primary_[i] : -1;
        if (primary < 0 || primary >= (int)n) {
            b.orbital_period = 0.0f;
            b.orbital_eccentricity = 0.0f;
            b.orbital_semi_major = 0.0f;
            continue;
        }
        const auto& host = bodies[(size_t)primary];
        if (host.marked_for_removal) continue;

        glm::vec3 r_vec = b.pos - host.pos;
        glm::vec3 v_vec = b.vel - host.vel;
        float r = glm::length(r_vec);
        float v = glm::length(v_vec);
        if (r < 1.0e-6f) continue;

        float mu = cfg.G * (host.mass + b.mass);
        if (mu < 1.0e-12f) continue;

        float energy = 0.5f * v * v - mu / r;

        if (energy < -1.0e-9f)
            b.orbital_semi_major = -mu / (2.0f * energy);
        else
            b.orbital_semi_major = r;

        glm::vec3 h = glm::cross(r_vec, v_vec);
        glm::vec3 e_vec = glm::cross(v_vec, h) / mu - r_vec / r;
        b.orbital_eccentricity = std::clamp(glm::length(e_vec), 0.0f, 10.0f);

        if (b.orbital_semi_major > 0.0f && energy < -1.0e-9f) {
            float a3 = b.orbital_semi_major * b.orbital_semi_major * b.orbital_semi_major;
            b.orbital_period = 6.283185f * std::sqrt(a3 / mu);
        } else {
            b.orbital_period = 0.0f;
        }

        if (b.orbital_period > 1.0e-3f) {
            float dt_fraction = sim_time_ / b.orbital_period;
            b.season_phase = std::fmod(dt_fraction * 6.283185f, 6.283185f);
        }

        if (is_star_type(b.type) && b.luminosity > 0.0f) {
            b.habitable_zone_inner = std::sqrt(b.luminosity / 1.1f);
            b.habitable_zone_outer = std::sqrt(b.luminosity / 0.53f);
        }

        if (is_black_hole_type(b.type) && b.mass > 0.0f)
            b.hawking_temperature = 6.17e-8f / b.mass;
    }
}

// ── Hawking radiation ────────────────────────────────────────────────────────

void CosmosApp::process_hawking_radiation(float dt) {
    auto& bodies = state.bodies;
    const float scale = std::max(cfg.hawking_radiation_scale, 0.0f);
    if (scale <= 0.0f) return;

    for (auto& b : bodies) {
        if (b.marked_for_removal) continue;
        if (!is_black_hole_type(b.type)) continue;
        if (b.mass <= 0.0f) continue;

        float abs_dt = std::abs(dt);
        float dm = scale * 1.0e-12f / (b.mass * b.mass) * abs_dt;
        dm = std::min(dm, b.mass * 0.5f);

        b.mass -= dm;
        b.mass_loss_rate += dm / std::max(abs_dt, 1.0e-9f);
        b.mass_loss_total += dm;
        b.temperature += dm * 1.0e6f / std::max(b.mass, 1.0e-12f);

        if (b.type == CTYPE_BH_PRIMORDIAL && b.mass < 1.0e-10f) {
            b.marked_for_removal = true;
            b.temperature = 1.0e12f;
        }
    }
}

// ── Cleanup ─────────────────────────────────────────────────────────────────

void CosmosApp::cleanup_bodies() {
    auto& bodies = state.bodies;

    auto finite_vec3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    for (auto& b : bodies) {
        bool invalid = !finite_vec3(b.pos) || !finite_vec3(b.vel) ||
            !std::isfinite(b.mass) || !std::isfinite(b.radius) ||
            !std::isfinite(b.temperature) || !std::isfinite(b.internal_energy) ||
            b.mass <= 0.0f || b.radius <= 0.0f;
        if (invalid)
            b.marked_for_removal = true;
        if (is_star_type(b.type) || is_black_hole_type(b.type) ||
            b.type == CTYPE_PLANET || b.type == CTYPE_MOON) {
            b.non_attracting = false;
        }
    }

    if (cfg.dynamic_budget_enabled) {
        int max_fragments = std::max(cfg.dynamic_max_fragments, 0);
        int max_non_attracting = std::max(cfg.dynamic_max_non_attracting, 0);
        float reduction = std::clamp(cfg.dynamic_reduction_percent, 0.01f, 1.0f);
        float target_fps = std::max(cfg.dynamic_target_fps, 1.0f);

        std::vector<size_t> attracting_fragments;
        std::vector<size_t> non_attracting_fragments;
        attracting_fragments.reserve(bodies.size());
        non_attracting_fragments.reserve(bodies.size());

        for (size_t i = 0; i < bodies.size(); ++i) {
            const auto& b = bodies[i];
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (b.non_attracting) non_attracting_fragments.push_back(i);
            else attracting_fragments.push_back(i);
        }

        if ((int)attracting_fragments.size() > max_fragments) {
            int to_convert = (int)attracting_fragments.size() - max_fragments;
            std::sort(attracting_fragments.begin(), attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_convert && k < (int)attracting_fragments.size(); ++k) {
                size_t idx = attracting_fragments[(size_t)k];
                bodies[idx].non_attracting = true;
                non_attracting_fragments.push_back(idx);
            }
        }

        int desired_non_attracting = max_non_attracting;
        if (smoothed_fps_ < target_fps) {
            float deficit = std::clamp((target_fps - smoothed_fps_) / target_fps, 0.0f, 1.0f);
            int fps_cut = (int)std::ceil((float)non_attracting_fragments.size() *
                                         reduction * (0.50f + deficit * 1.50f));
            desired_non_attracting = std::max(0, desired_non_attracting - fps_cut);
        }

        if ((int)non_attracting_fragments.size() > desired_non_attracting) {
            int overflow = (int)non_attracting_fragments.size() - desired_non_attracting;
            int chunk = (int)std::ceil((float)non_attracting_fragments.size() * reduction);
            int to_remove = std::max(overflow, chunk);
            std::sort(non_attracting_fragments.begin(), non_attracting_fragments.end(),
                      [&](size_t a, size_t b) {
                          const auto& ba = bodies[a];
                          const auto& bb = bodies[b];
                          if (std::abs(ba.mass - bb.mass) > 1.0e-12f)
                              return ba.mass < bb.mass;
                          return ba.age > bb.age;
                      });
            for (int k = 0; k < to_remove && k < (int)non_attracting_fragments.size(); ++k) {
                bodies[non_attracting_fragments[(size_t)k]].marked_for_removal = true;
            }
        }
    }

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

    // Fix camera focus body
    if (camera.focus_body >= 0 && camera.focus_body < (int)bodies.size()) {
        int new_focus = remap[camera.focus_body];
        if (new_focus < 0) {
            camera.release_focus();
        } else {
            camera.focus_body = new_focus;
        }
    } else if (camera.focus_body >= (int)bodies.size()) {
        camera.release_focus();
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

void CosmosApp::spawn_fragments(glm::vec3 pos, glm::vec3 vel, float total_mass, int count,
                                uint32_t parent_generation, float source_temperature,
                                glm::vec3 impact_axis, float ejecta_speed,
                                const CelestialBody* source_body, float shock_ratio) {
    if (count < 1 || total_mass <= 0.0f) return;
    if (!std::isfinite(total_mass) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
        !std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z)) return;
    if ((int)parent_generation >= cfg.max_frag_generation) return;

    size_t body_count = state.bodies.size();
    if (cfg.dynamic_budget_enabled) {
        int attract_cap = std::max(cfg.dynamic_max_fragments, 0);
        int non_attract_cap = std::max(cfg.dynamic_max_non_attracting, 0);
        int total_budget_cap = std::max(attract_cap + non_attract_cap + 256, 1);
        if ((int)body_count >= total_budget_cap) return;
        int capacity = total_budget_cap - (int)body_count;
        count = std::min(count, capacity);

        int per_event_cap = std::max(
            1, (int)std::ceil((float)non_attract_cap *
                std::clamp(cfg.dynamic_explosion_density, 0.01f, 1.0f)));
        count = std::min(count, per_event_cap);
    }
    if (count < 1) return;

    float kMinFragmentMass = std::max(1.0e-12f, std::min(cfg.min_fragment_mass, 1.0e-9f));
    int max_count = std::max(1, (int)std::floor(total_mass / kMinFragmentMass));
    count = std::min(count, max_count);
    if (count < 1) count = 1;

    uint32_t seed = (uint32_t)(std::hash<float>{}(pos.x) ^ std::hash<float>{}(pos.y) ^
                               std::hash<float>{}(pos.z) ^ std::hash<float>{}(total_mass) ^
                               (parent_generation * 2654435761u));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    std::vector<float> weights((size_t)count, 0.0f);
    float weight_sum = 0.0f;
    for (int i = 0; i < count; ++i) {
        weights[(size_t)i] = 0.25f + u01(rng);
        weight_sum += weights[(size_t)i];
    }

    std::vector<float> masses((size_t)count, kMinFragmentMass);
    float remaining_mass = total_mass - kMinFragmentMass * (float)count;
    if (remaining_mass < 0.0f) remaining_mass = 0.0f;
    for (int i = 0; i < count; ++i)
        masses[(size_t)i] += remaining_mass * (weights[(size_t)i] / std::max(weight_sum, 1.0e-6f));
    masses.back() += total_mass - std::accumulate(masses.begin(), masses.end(), 0.0f);

    auto safe_normalize = [](const glm::vec3& v, const glm::vec3& fallback) {
        float l2 = glm::dot(v, v);
        if (!std::isfinite(l2) || l2 < 1.0e-12f) return fallback;
        return v / std::sqrt(l2);
    };
    glm::vec3 axis = safe_normalize(impact_axis, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(axis, axis) < 1.0e-8f) {
        axis = safe_normalize(glm::vec3(u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f,
                                        u01(rng) * 2.0f - 1.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    }
    MaterialComposition source_materials{};
    if (source_body != nullptr) {
        source_materials = derive_materials(*source_body);
    } else {
        source_materials.silicate = 0.75f;
        source_materials.iron = 0.25f;
    }
    bool energetic_source = source_body != nullptr &&
        (is_star_type(source_body->type) || is_black_hole_type(source_body->type) ||
         source_body->type == CTYPE_NEBULA);
    float min_ejecta = energetic_source ? 4.0f : 0.004f;
    float max_ejecta = energetic_source ? 120.0f : 0.080f;
    float base_ejecta = std::clamp(std::max(ejecta_speed, min_ejecta), min_ejecta, max_ejecta);
    if (!std::isfinite(base_ejecta)) base_ejecta = min_ejecta;
    bool gas_source = (source_body != nullptr) && gas_dominated_body(*source_body, source_materials);
    float normalized_shock = std::clamp(shock_ratio, 0.0f, 2.0f);
    float gas_frag_chance = gas_source
        ? std::clamp(source_materials.hydrogen * (0.28f + normalized_shock * 0.22f), 0.0f, 0.78f)
        : 0.0f;
    float icy_frag_chance = std::clamp(source_materials.water * (0.22f + std::max(0.0f, 0.70f - normalized_shock) * 0.30f),
                                       0.0f, 0.62f);
    float molten_bias = std::clamp((source_temperature - 900.0f) / 1500.0f + normalized_shock * 0.55f +
                                   source_materials.silicate * 0.16f + source_materials.iron * 0.20f,
                                   0.0f, 1.35f);

    // Planetary breakups should produce a few dominant chunks instead of equal tiny shards.
    if (!energetic_source && count <= 6 && remaining_mass > 0.0f) {
        int dom = (int)(u01(rng) * (float)count);
        if (dom >= count) dom = count - 1;
        float target_dom = std::clamp(total_mass * (0.35f + u01(rng) * 0.20f),
                                      total_mass * 0.25f, total_mass * 0.60f);
        float current_dom = masses[(size_t)dom];
        float extra = std::max(target_dom - current_dom, 0.0f);
        if (extra > 0.0f) {
            float donor_total = 0.0f;
            for (int i = 0; i < count; ++i) {
                if (i == dom) continue;
                donor_total += std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
            }
            if (donor_total > 1.0e-12f) {
                for (int i = 0; i < count; ++i) {
                    if (i == dom) continue;
                    float avail = std::max(masses[(size_t)i] - kMinFragmentMass, 0.0f);
                    float take = extra * (avail / donor_total);
                    masses[(size_t)i] -= take;
                    masses[(size_t)dom] += take;
                }
            }
        }
    }

    std::vector<glm::vec3> rel_vels((size_t)count, glm::vec3(0.0f));
    glm::vec3 momentum_bias(0.0f);
    for (int i = 0; i < count; ++i) {
        float theta = u01(rng) * 6.28318530718f;
        float z = u01(rng) * 2.0f - 1.0f;
        float r = std::sqrt(std::max(1.0f - z * z, 0.0f));
        glm::vec3 rand_dir(r * std::cos(theta), z, r * std::sin(theta));
        glm::vec3 dir;
        float speed = 0.0f;
        if (energetic_source) {
            dir = safe_normalize(glm::mix(rand_dir, axis, 0.45f + 0.25f * u01(rng)), axis);
            speed = base_ejecta * (0.55f + u01(rng) * 0.90f);
        } else {
            glm::vec3 planar = rand_dir - axis * glm::dot(rand_dir, axis);
            if (glm::length(planar) < 1.0e-5f) {
                planar = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::length(planar) < 1.0e-5f)
                    planar = glm::cross(axis, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            if (glm::length(planar) < 1.0e-5f)
                planar = glm::vec3(1.0f, 0.0f, 0.0f);
            dir = safe_normalize(glm::mix(safe_normalize(planar, glm::vec3(1.0f, 0.0f, 0.0f)), axis,
                                          0.12f + 0.08f * u01(rng)), axis);
            speed = base_ejecta * (0.65f + u01(rng) * 0.45f);
        }
        rel_vels[(size_t)i] = dir * speed;
        momentum_bias += rel_vels[(size_t)i] * masses[(size_t)i];
    }
    momentum_bias /= std::max(total_mass, 1.0e-6f);
    for (auto& rv : rel_vels)
        rv -= momentum_bias;

    int attracting_fragments_now = 0;
    if (cfg.dynamic_budget_enabled) {
        for (const auto& b : state.bodies) {
            if (b.marked_for_removal) continue;
            if (!fragment_like_body(b)) continue;
            if (!b.non_attracting) ++attracting_fragments_now;
        }
    }
    int max_attracting_fragments = std::max(cfg.dynamic_max_fragments, 0);

    for (int i = 0; i < count; i++) {
        float frag_mass = masses[(size_t)i];
        float frag_radius = std::max(1.2f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 3.0f);

        CelestialBody frag;
        glm::vec3 dir = safe_normalize(rel_vels[(size_t)i] + axis * 0.1f, axis);
        frag.pos = pos + dir * (frag_radius * 1.5f + 1.0f);
        frag.vel = vel + rel_vels[(size_t)i];
        if (!std::isfinite(frag.vel.x) || !std::isfinite(frag.vel.y) || !std::isfinite(frag.vel.z) ||
            !std::isfinite(frag.pos.x) || !std::isfinite(frag.pos.y) || !std::isfinite(frag.pos.z))
            continue;
        frag.mass = frag_mass;
        frag.radius = frag_radius;
        frag.temperature = std::clamp(source_temperature +
                                      base_ejecta * (5.0f + normalized_shock * 6.5f) *
                                      (0.55f + u01(rng) * 0.95f),
                                      40.0f, 30000.0f);
        float type_roll = u01(rng);
        if (gas_source && type_roll < gas_frag_chance) {
            frag.type = CTYPE_NEBULA;
            frag.radius = std::max(frag_radius * 1.35f, std::cbrt(std::max(frag_mass, kMinFragmentMass)) * 24.0f);
            frag.temperature = std::max(60.0f, frag.temperature * 0.55f);
            frag.atmosphere_retention = 1.0f;
            frag.material_phase = (normalized_shock > 0.95f) ? PHASE_COLLAPSING : PHASE_GAS;
            frag.phase_intensity = std::clamp(0.40f + source_materials.hydrogen * 0.35f + normalized_shock * 0.18f,
                                              0.25f, 1.0f);
            frag.collapse_progress = std::clamp(source_materials.hydrogen * 0.12f + normalized_shock * 0.08f,
                                                0.0f, 0.28f);
        } else if (type_roll < gas_frag_chance + icy_frag_chance && frag.temperature < 520.0f) {
            frag.type = CTYPE_COMET;
            frag.atmosphere_retention = 0.18f;
            frag.material_phase = (frag.temperature < 170.0f) ? PHASE_ICE : PHASE_LIQUID;
            frag.phase_intensity = std::clamp(0.28f + source_materials.water * 0.60f, 0.20f, 1.0f);
        } else {
            frag.type = CTYPE_ASTEROID;
            frag.atmosphere_retention = 0.02f;
            if (molten_bias > 0.28f || frag.temperature > 950.0f) {
                frag.material_phase = (frag.temperature > 2400.0f) ? PHASE_PLASMA : PHASE_MOLTEN;
                frag.phase_intensity = std::clamp(0.24f + molten_bias * 0.65f, 0.18f, 1.0f);
            } else if (source_materials.water > 0.28f && frag.temperature < 170.0f) {
                frag.material_phase = PHASE_ICE;
                frag.phase_intensity = std::clamp(0.22f + source_materials.water * 0.55f, 0.18f, 1.0f);
            } else {
                frag.material_phase = PHASE_SOLID;
                frag.phase_intensity = std::clamp(0.18f + source_materials.silicate * 0.35f +
                                                  source_materials.iron * 0.20f, 0.12f, 0.85f);
            }
        }
        frag.fuel = 0.0f;
        frag.internal_energy = normalized_shock * frag_mass * (12.0f + base_ejecta * 0.9f);
        frag.seed = rng();
        frag.frag_generation = parent_generation + 1;
        frag.angular_vel = (u01(rng) * 2.0f - 1.0f) * 0.01f;
        if (cfg.dynamic_budget_enabled) {
            if (attracting_fragments_now >= max_attracting_fragments) {
                frag.non_attracting = true;
            } else {
                frag.non_attracting = false;
                ++attracting_fragments_now;
            }
        } else {
            frag.non_attracting = false;
        }
        frag.name = generate_body_name(frag.seed, frag.type);
        clear_ring_system(frag);
        clear_impact_signature(frag);
        refresh_body_render_state(frag, &state);

        state.bodies.push_back(frag);
        state.trails.emplace_back();
    }
}

// ── 3D Projection (for overlay) ─────────────────────────────────────────────

CosmosApp::Projected CosmosApp::project(const glm::vec3& world_pos,
                                         const glm::dmat4& vp,
                                         float screen_w, float screen_h) const {
    glm::dvec4 clip = vp * glm::dvec4(world_pos, 1.0);
    if (clip.w <= 0.0)
        return {0, 0, 0, false};

    glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    float sx = (float)((ndc.x * 0.5 + 0.5) * (double)screen_w);
    float sy = (float)((1.0 - (ndc.y * 0.5 + 0.5)) * (double)screen_h);

    bool visible = (ndc.x >= -1.2f && ndc.x <= 1.2f &&
                    ndc.y >= -1.2f && ndc.y <= 1.2f &&
                    ndc.z >= 0.0f && ndc.z <= 1.0f);
    return {sx, sy, (float)clip.w, visible};
}

float CosmosApp::screen_radius(float world_radius, float depth,
                                float fov_rad, float screen_h) const {
    if (depth <= 0.0f) return 0.0f;
    return (world_radius / depth) * (screen_h / (2.0f * std::tan(fov_rad * 0.5f)));
}
