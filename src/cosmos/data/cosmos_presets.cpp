#include "cosmos/cosmos_app_internal.h"
#include <cmath>
#include <random>

// ── Default System (used by preset_solar_system) ─────────────────────────────

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
        p.radius = expected_planet_radius(planet_mass[i]);
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

// ── Preset Scenarios ─────────────────────────────────────────────────────────

// Helper: make a star and push it
static int push_star(CosmosState& state, glm::vec3 pos, glm::vec3 vel,
                     float mass, float fuel, uint32_t seed, uint32_t requested_type = CTYPE_STAR) {
    CelestialBody s;
    s.pos = pos; s.vel = vel; s.mass = mass; s.fuel = fuel; s.seed = seed;
    s.temperature = expected_main_sequence_temperature(mass);
    s.type = classify_star_spectral(s.temperature, mass);
    s.stellar_stage = SSTAGE_MAIN_SEQUENCE;
    s.radius = expected_star_radius(s);
    s.angular_vel = 2.0f * 3.14159265f / (26.0f * 24.0f * 3600.0f / std::max(mass, 0.1f));
    s.luminosity = expected_stellar_luminosity(mass, s.temperature, s.radius, s.stellar_stage, fuel);
    s.name = generate_body_name(seed, s.type);
    state.bodies.push_back(s);
    state.trails.emplace_back();
    return (int)state.bodies.size() - 1;
}

// Helper: push a planet in circular orbit around body at orbit_idx
static int push_planet(CosmosState& state, const CosmosConfig& cfg,
                       int orbit_idx, float orbit_r, float mass, float temp,
                       float angle, uint32_t seed, int forced_surface = -1) {
    const auto& host = state.bodies[(size_t)orbit_idx];
    CelestialBody p;
    p.pos = host.pos + glm::vec3(std::cos(angle) * orbit_r, 0.0f, std::sin(angle) * orbit_r);
    float v = std::sqrt(cfg.G * host.mass / orbit_r);
    p.vel = host.vel + glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
    p.mass = mass; p.temperature = temp; p.type = CTYPE_PLANET;
    p.parent = orbit_idx; p.seed = seed;
    p.radius = expected_planet_radius(mass);
    p.forced_surface = forced_surface;
    p.name = generate_body_name(seed, p.type);
    state.bodies.push_back(p);
    state.trails.emplace_back();
    return (int)state.bodies.size() - 1;
}

// Helper: push a moon in circular orbit around body at host_idx
static int push_moon(CosmosState& state, const CosmosConfig& cfg,
                     int host_idx, float orbit_r, float mass, float temp,
                     float angle, uint32_t seed) {
    const auto& host = state.bodies[(size_t)host_idx];
    CelestialBody m;
    m.pos = host.pos + glm::vec3(std::cos(angle) * orbit_r, 0.0f, std::sin(angle) * orbit_r);
    float v = std::sqrt(cfg.G * host.mass / orbit_r);
    m.vel = host.vel + glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
    m.mass = mass; m.temperature = temp; m.type = CTYPE_MOON;
    m.parent = host_idx; m.seed = seed;
    m.radius = expected_planet_radius(mass);
    m.name = generate_body_name(seed, m.type);
    state.bodies.push_back(m);
    state.trails.emplace_back();
    return (int)state.bodies.size() - 1;
}

// 0: Solar System (default)
static void preset_solar_system(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    seed_default_system(state, cfg);
    // Camera far enough to see the whole system (outermost planet at ~sr*22)
    float sr = state.bodies[0].radius;
    cam.distance = sr * 30.0f; cam.target_distance = sr * 30.0f; cam.elevation = 0.5f;
}

// 1: Binary Stars — two sun-like stars with circumbinary planets
static void preset_binary_stars(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    float sep = 80.0f;
    float m1 = 1.0f, m2 = 0.75f;
    float mu = m2 / (m1 + m2);
    float v_orb = std::sqrt(cfg.G * (m1 + m2) / sep);

    int s1 = push_star(state, glm::vec3(-sep * mu, 0, 0),
                       glm::vec3(0, 0, -v_orb * mu), m1, 0.75f, 1001);
    int s2 = push_star(state, glm::vec3(sep * (1.0f - mu), 0, 0),
                       glm::vec3(0, 0, v_orb * (1.0f - mu)), m2, 0.80f, 1002);
    (void)s1; (void)s2;

    // Circumbinary planets — must orbit far enough for stability (>2.5× separation)
    glm::vec3 com_vel(0.0f);
    auto push_cb_planet = [&](float r, float mass, float temp, float angle, uint32_t seed, int surf = -1) {
        CelestialBody p;
        p.pos = glm::vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        float v = std::sqrt(cfg.G * (m1 + m2) / r);
        p.vel = com_vel + glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        p.mass = mass; p.temperature = temp; p.type = CTYPE_PLANET; p.seed = seed;
        p.radius = expected_planet_radius(mass);
        p.forced_surface = surf;
        p.name = generate_body_name(seed, p.type);
        state.bodies.push_back(p); state.trails.emplace_back();
    };
    push_cb_planet(280.0f, 4.5e-6f, 340.0f, 0.0f, 2001, 3);   // warm earth-like
    push_cb_planet(420.0f, 3.0e-6f, 260.0f, 2.1f, 2002);        // temperate
    push_cb_planet(620.0f, 8.0e-4f, 110.0f, 4.2f, 2003);        // gas giant

    // Camera far enough to see circumbinary planets (outermost at ~620)
    cam.distance = 2000.0f; cam.target_distance = 2000.0f; cam.elevation = 0.45f;
}

// 2: TRAPPIST-1 — compact red dwarf system with 7 close-in planets
static void preset_trappist(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 0.089f, 0.95f, 3001);
    float sr = state.bodies[(size_t)star].radius;

    // 7 planets at physically accurate orbital distances (real TRAPPIST-1 AU → sr multiples)
    const float orbits[] = {sr * 17.0f, sr * 23.5f, sr * 33.0f, sr * 44.0f,
                            sr * 57.0f, sr * 70.0f, sr * 92.0f};
    const float masses[] = {2.6e-6f, 4.0e-6f, 2.3e-6f, 3.8e-6f,
                            3.0e-6f, 4.1e-6f, 1.0e-6f};
    const float temps[] = {400.0f, 342.0f, 288.0f, 251.0f, 218.0f, 170.0f, 130.0f};
    const int surfs[]   = {0, 1, 3, 3, 1, 2, 2}; // rocky, water, earth, earth, water, ice, ice
    for (int i = 0; i < 7; i++) {
        float angle = i * 0.8976f;
        push_planet(state, cfg, star, orbits[i], masses[i], temps[i], angle, 3100 + i, surfs[i]);
    }

    cam.distance = sr * 110.0f; cam.target_distance = sr * 110.0f; cam.elevation = 0.6f;
}

// 3: Hot Jupiter — sun-like star with a massive planet dangerously close
static void preset_hot_jupiter(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.2f, 0.70f, 4001);
    float sr = state.bodies[(size_t)star].radius;

    push_planet(state, cfg, star, sr * 3.5f, 2.0e-3f, 1800.0f, 0.0f, 4002, 4); // hot gas giant
    push_planet(state, cfg, star, sr * 12.0f, 3.0e-6f, 310.0f, 1.8f, 4003, 3);
    push_planet(state, cfg, star, sr * 20.0f, 5.0e-6f, 220.0f, 3.6f, 4004, 1);

    cam.distance = sr * 28.0f; cam.target_distance = sr * 28.0f; cam.elevation = 0.3f;
}

// 4: Giant Impact — Earth and Theia approaching collision (Moon formation)
static void preset_giant_impact(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.0f, 0.75f, 5001);
    float sr = state.bodies[(size_t)star].radius;

    // Earth
    float earth_r = sr * 14.0f;
    int earth = push_planet(state, cfg, star, earth_r, 3.0e-6f, 300.0f, 0.0f, 5002, 3);

    // Theia — on a grazing trajectory
    CelestialBody theia;
    const auto& eb = state.bodies[(size_t)earth];
    theia.pos = eb.pos + glm::vec3(60.0f, 8.0f, 30.0f);
    theia.vel = eb.vel + glm::vec3(-0.035f, -0.003f, -0.015f);
    theia.mass = 3.2e-7f; // Mars-sized
    theia.temperature = 1800.0f;
    theia.type = CTYPE_PLANET; theia.seed = 5003;
    theia.radius = expected_planet_radius(theia.mass);
    theia.forced_surface = 0; // rocky
    theia.name = "Theia";
    state.bodies.push_back(theia); state.trails.emplace_back();

    cam.focus_on(eb.pos, earth, eb.radius);
    cam.distance = std::max(eb.radius * 20.0f, 200.0f);
    cam.target_distance = cam.distance;
    cam.elevation = 0.25f;
}

// 5: Stellar Graveyard — neutron star, white dwarf, and stellar black hole
static void preset_stellar_graveyard(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();

    // Neutron star (pulsar)
    CelestialBody ns;
    ns.pos = glm::vec3(0); ns.vel = glm::vec3(0);
    ns.mass = 1.5f; ns.temperature = 150000.0f;
    ns.stellar_stage = SSTAGE_NEUTRON_STAR; ns.fuel = 0.0f; ns.seed = 6001;
    ns.type = classify_star_spectral(ns.temperature, ns.mass);
    ns.radius = expected_star_radius(ns);
    ns.angular_vel = 200.0f; // fast spinner
    ns.luminosity = expected_stellar_luminosity(ns.mass, ns.temperature, ns.radius, ns.stellar_stage, 0.0f);
    ns.name = "Pulsar";
    state.bodies.push_back(ns); state.trails.emplace_back();

    // White dwarf orbiting at distance
    CelestialBody wd;
    float wd_r = 350.0f;
    float wd_v = std::sqrt(cfg.G * ns.mass / wd_r);
    wd.pos = glm::vec3(wd_r, 0, 0); wd.vel = glm::vec3(0, 0, wd_v);
    wd.mass = 0.6f; wd.temperature = 28000.0f;
    wd.stellar_stage = SSTAGE_WHITE_DWARF; wd.fuel = 0.0f; wd.seed = 6002;
    wd.type = classify_star_spectral(wd.temperature, wd.mass);
    wd.radius = expected_star_radius(wd);
    wd.angular_vel = 0.5f;
    wd.luminosity = expected_stellar_luminosity(wd.mass, wd.temperature, wd.radius, wd.stellar_stage, 0.0f);
    wd.name = "White Dwarf";
    state.bodies.push_back(wd); state.trails.emplace_back();

    // Stellar black hole further out
    CelestialBody bh;
    float bh_r = 800.0f;
    float bh_v = std::sqrt(cfg.G * (ns.mass + wd.mass) / bh_r);
    bh.pos = glm::vec3(-bh_r, 0, 0); bh.vel = glm::vec3(0, 0, -bh_v);
    bh.mass = 12.0f; bh.temperature = 0.0f; bh.seed = 6003;
    bh.type = classify_black_hole(bh.mass);
    bh.radius = std::max(2.0f * cfg.G * bh.mass / (300.0f * 300.0f) * 80.0f, 3.0f);
    bh.angular_vel = 15.0f;
    bh.name = "Stellar Black Hole";
    state.bodies.push_back(bh); state.trails.emplace_back();

    cam.distance = 3000.0f; cam.target_distance = 3000.0f; cam.elevation = 0.55f;
}

// 6: Protoplanetary Disk — young star surrounded by dust and forming planets
static void preset_protoplanetary_disk(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 0.8f, 0.98f, 7001);

    // Scatter asteroids/dust in a disk
    std::mt19937 rng(7777);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 150; i++) {
        CelestialBody a;
        float r = randf(120.0f, 800.0f);
        float angle = randf(0.0f, 6.2832f);
        float y_spread = randf(-12.0f, 12.0f) * (r / 800.0f);
        a.pos = glm::vec3(std::cos(angle) * r, y_spread, std::sin(angle) * r);
        float v = std::sqrt(cfg.G * state.bodies[0].mass / r) * randf(0.92f, 1.08f);
        a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        a.mass = randf(1.0e-10f, 5.0e-8f);
        a.radius = randf(0.3f, 2.0f);
        a.temperature = randf(50.0f, 400.0f);
        a.type = (randf(0, 1) < 0.3f) ? CTYPE_DUST : CTYPE_ASTEROID;
        a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a); state.trails.emplace_back();
    }

    // A few proto-planets forming in the disk
    push_planet(state, cfg, star, 220.0f, 8.0e-7f, 500.0f, 0.5f, 7101, 0);
    push_planet(state, cfg, star, 380.0f, 2.0e-6f, 280.0f, 2.8f, 7102);
    push_planet(state, cfg, star, 550.0f, 5.0e-5f, 150.0f, 5.0f, 7103, 4);

    cam.distance = 3000.0f; cam.target_distance = 3000.0f; cam.elevation = 0.65f;
}

// 7: Ringed Worlds — Saturn-like system showcasing rings
static void preset_ringed_worlds(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.0f, 0.72f, 8001);
    float sr = state.bodies[(size_t)star].radius;

    // Saturn analogue with rings
    int saturn = push_planet(state, cfg, star, sr * 18.0f, 2.86e-4f, 95.0f, 0.0f, 8002, 4);
    auto& sb = state.bodies[(size_t)saturn];
    set_ring_system(sb, sb.radius * 1.5f, sb.radius * 3.2f, 0.65f, 0.55f, 0.45f);

    // Ice giant with thin rings
    int ice = push_planet(state, cfg, star, sr * 28.0f, 4.4e-5f, 60.0f, 2.5f, 8003, 4);
    auto& ib = state.bodies[(size_t)ice];
    set_ring_system(ib, ib.radius * 1.8f, ib.radius * 2.8f, 0.25f, 0.70f, 0.85f);

    // Rocky planet with debris ring
    int rocky = push_planet(state, cfg, star, sr * 9.0f, 5.0e-6f, 250.0f, 4.0f, 8004, 0);
    auto& rb = state.bodies[(size_t)rocky];
    set_ring_system(rb, rb.radius * 2.0f, rb.radius * 4.0f, 0.15f, 0.30f, 0.20f);

    // Inner terrestrial
    push_planet(state, cfg, star, sr * 5.0f, 2.5e-6f, 380.0f, 1.0f, 8005, 0);

    // Some moons around saturn
    push_moon(state, cfg, saturn, sb.radius * 5.0f, 2.2e-8f, 75.0f, 0.3f, 8101);
    push_moon(state, cfg, saturn, sb.radius * 8.0f, 6.8e-8f, 65.0f, 2.1f, 8102);
    push_moon(state, cfg, saturn, sb.radius * 12.0f, 1.0e-7f, 55.0f, 4.5f, 8103);

    cam.distance = sr * 35.0f; cam.target_distance = sr * 35.0f; cam.elevation = 0.4f;
}

// 8: Star Cluster — loose group of diverse stars
static void preset_star_cluster(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    std::mt19937 rng(9999);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };

    int n_stars = 12;
    float cluster_r = 40000.0f;  // must be >> star radius (~873) to avoid immediate collisions
    for (int i = 0; i < n_stars; i++) {
        // Plummer sphere distribution
        float u = randf(0.0f, 1.0f);
        float r = cluster_r * std::pow(std::pow(u, -2.0f/3.0f) - 1.0f, -0.5f) * 0.3f;
        r = std::min(r, cluster_r * 1.5f);
        float theta = randf(0.0f, 3.14159f);
        float phi = randf(0.0f, 6.28318f);
        glm::vec3 pos(r * std::sin(theta) * std::cos(phi),
                      r * std::sin(theta) * std::sin(phi) * 0.4f,
                      r * std::cos(theta));

        float mass = std::pow(10.0f, randf(-0.5f, 1.2f)); // 0.3 to 15 solar masses
        mass = std::clamp(mass, 0.08f, 50.0f);
        float fuel = randf(0.50f, 0.95f);

        // Orbital velocity for cluster virial equilibrium (approximate)
        float total_mass = (float)n_stars * 2.0f; // rough estimate
        float v_circ = std::sqrt(cfg.G * total_mass / std::max(r, 10.0f)) * randf(0.3f, 0.8f);
        glm::vec3 vel = glm::normalize(glm::cross(pos, glm::vec3(0, 1, 0)) + glm::vec3(0.01f)) * v_circ;

        push_star(state, pos, vel, mass, fuel, 9100 + i);
    }

    cam.distance = 100000.0f; cam.target_distance = 100000.0f; cam.elevation = 0.35f;
}

// 9: Comet Shower — inner system with comets streaming in
static void preset_comet_shower(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.0f, 0.72f, 10001);
    float sr = state.bodies[(size_t)star].radius;

    push_planet(state, cfg, star, sr * 8.0f, 1.6e-7f, 440.0f, 0.0f, 10002, 0);
    push_planet(state, cfg, star, sr * 13.0f, 3.0e-6f, 290.0f, 1.7f, 10003, 3);
    push_planet(state, cfg, star, sr * 20.0f, 3.2e-7f, 210.0f, 3.5f, 10004, 0);

    // Comets incoming from all directions
    std::mt19937 rng(10101);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 40; i++) {
        CelestialBody c;
        float dist = randf(500.0f, 1200.0f);
        float theta = randf(0.0f, 3.14159f);
        float phi = randf(0.0f, 6.28318f);
        c.pos = glm::vec3(dist * std::sin(theta) * std::cos(phi),
                           dist * std::sin(theta) * std::sin(phi) * 0.6f,
                           dist * std::cos(theta));
        // Aim roughly toward the star with some scatter
        glm::vec3 to_star = -glm::normalize(c.pos);
        float speed = std::sqrt(2.0f * cfg.G * 1.0f / dist) * randf(0.6f, 1.1f);
        glm::vec3 scatter(randf(-0.15f, 0.15f), randf(-0.08f, 0.08f), randf(-0.15f, 0.15f));
        c.vel = (to_star + scatter) * speed;
        c.mass = randf(1.0e-12f, 5.0e-10f);
        c.radius = randf(0.2f, 1.0f);
        c.temperature = randf(30.0f, 80.0f);
        c.type = CTYPE_COMET;
        c.seed = (uint32_t)(rng());
        c.name = generate_body_name(c.seed, c.type);
        state.bodies.push_back(c); state.trails.emplace_back();
    }

    cam.distance = sr * 28.0f; cam.target_distance = sr * 28.0f; cam.elevation = 0.55f;
}

// 10: Rogue Planet — planet with moons, drifting through space
static void preset_rogue_planet(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    CelestialBody p;
    p.pos = glm::vec3(0); p.vel = glm::vec3(0);
    p.mass = 3.5e-4f; // ~120 Earth masses, sub-brown-dwarf
    p.radius = expected_planet_radius(p.mass);
    p.temperature = 55.0f;
    p.type = CTYPE_PLANET; p.seed = 11001;
    p.forced_surface = 4; // gas giant look
    p.name = "Wanderer";
    state.bodies.push_back(p); state.trails.emplace_back();
    int pi = 0;
    auto& pb = state.bodies[(size_t)pi];
    set_ring_system(pb, pb.radius * 1.6f, pb.radius * 2.8f, 0.40f, 0.60f, 0.35f);

    push_moon(state, cfg, pi, pb.radius * 4.5f, 3.7e-8f, 45.0f, 0.0f, 11101);
    push_moon(state, cfg, pi, pb.radius * 7.0f, 1.2e-7f, 38.0f, 1.8f, 11102);
    push_moon(state, cfg, pi, pb.radius * 10.0f, 6.5e-8f, 32.0f, 3.5f, 11103);
    push_moon(state, cfg, pi, pb.radius * 15.0f, 2.0e-8f, 28.0f, 5.0f, 11104);

    // Some captured asteroids
    std::mt19937 rng(11111);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 15; i++) {
        CelestialBody a;
        float r = randf(pb.radius * 18.0f, pb.radius * 35.0f);
        float angle = randf(0.0f, 6.2832f);
        float incl = randf(-0.3f, 0.3f);
        a.pos = glm::vec3(std::cos(angle) * r, std::sin(incl) * r * 0.3f, std::sin(angle) * r);
        float v = std::sqrt(cfg.G * p.mass / r) * randf(0.85f, 1.15f);
        a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        a.mass = randf(1e-11f, 5e-9f);
        a.radius = randf(0.2f, 1.2f);
        a.temperature = 25.0f;
        a.type = CTYPE_ASTEROID; a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a); state.trails.emplace_back();
    }

    cam.distance = pb.radius * 45.0f; cam.target_distance = pb.radius * 45.0f; cam.elevation = 0.35f;
    cam.focus_on(p.pos, pi, pb.radius);
}

// 11: Supermassive Black Hole — galactic center with orbiting stars
static void preset_supermassive_bh(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    CelestialBody bh;
    bh.pos = glm::vec3(0); bh.vel = glm::vec3(0);
    bh.mass = 4.0e6f; // Sgr A* scale
    bh.temperature = 0.0f; bh.seed = 12001;
    bh.type = classify_black_hole(bh.mass);
    bh.radius = 15.0f; // visual radius
    bh.angular_vel = 80.0f;
    bh.name = "Sagittarius A*";
    state.bodies.push_back(bh); state.trails.emplace_back();

    // Stars in various orbits
    std::mt19937 rng(12345);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 16; i++) {
        float r = randf(3000.0f, 30000.0f);
        float angle = randf(0.0f, 6.2832f);
        float incl = randf(-0.4f, 0.4f);
        glm::vec3 pos(std::cos(angle) * r, std::sin(incl) * r * 0.3f, std::sin(angle) * r);
        float v = std::sqrt(cfg.G * bh.mass / r);
        glm::vec3 vel(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        // Randomize orbital plane slightly
        vel.y = randf(-0.15f, 0.15f) * v;

        float mass = std::pow(10.0f, randf(-0.2f, 1.5f));
        mass = std::clamp(mass, 0.3f, 30.0f);
        push_star(state, pos, vel, mass, randf(0.4f, 0.95f), 12100 + i);
    }

    cam.distance = 80000.0f; cam.target_distance = 80000.0f; cam.elevation = 0.4f;
}

// 12: Habitable Zone Tour — 4 diverse habitable worlds around different stars
static void preset_habitable_zone(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();

    // Space systems far enough apart that mutual gravity is negligible
    float sep = 50000.0f;

    // System 1: Earth twin around sun-like star
    int s1 = push_star(state, glm::vec3(-sep, 0, 0), glm::vec3(0), 1.0f, 0.72f, 13001);
    float sr1 = state.bodies[(size_t)s1].radius;
    push_planet(state, cfg, s1, sr1 * 14.0f, 3.0e-6f, 288.0f, 0.0f, 13101, 3); // earth-like

    // System 2: Ocean world around K dwarf
    int s2 = push_star(state, glm::vec3(sep, 0, 0), glm::vec3(0), 0.55f, 0.85f, 13002);
    float sr2 = state.bodies[(size_t)s2].radius;
    push_planet(state, cfg, s2, sr2 * 8.0f, 5.0e-6f, 305.0f, 1.2f, 13201, 1); // water world

    // System 3: Desert super-Earth around F star
    int s3 = push_star(state, glm::vec3(0, 0, -sep), glm::vec3(0), 1.4f, 0.65f, 13003);
    float sr3 = state.bodies[(size_t)s3].radius;
    push_planet(state, cfg, s3, sr3 * 18.0f, 7.0e-6f, 320.0f, 2.5f, 13301, 0); // rocky/desert

    // System 4: Icy moon candidate around gas giant orbiting a G star
    int s4 = push_star(state, glm::vec3(0, 0, sep), glm::vec3(0), 0.9f, 0.78f, 13004);
    float sr4 = state.bodies[(size_t)s4].radius;
    int gj = push_planet(state, cfg, s4, sr4 * 16.0f, 6.0e-4f, 130.0f, 0.0f, 13401, 4);
    auto& gjb = state.bodies[(size_t)gj];
    push_moon(state, cfg, gj, gjb.radius * 5.0f, 8.0e-8f, 100.0f, 0.0f, 13411);
    push_moon(state, cfg, gj, gjb.radius * 8.0f, 1.3e-7f, 85.0f, 2.0f, 13412);

    cam.distance = 25000.0f; cam.target_distance = 25000.0f; cam.elevation = 0.5f;
}

// 13: Stellar Evolution — stars at every life stage
static void preset_stellar_evolution(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    float spacing = 30000.0f;  // far enough apart to avoid mutual gravity

    // Main sequence sun
    push_star(state, glm::vec3(-spacing * 2, 0, 0), glm::vec3(0), 1.0f, 0.72f, 14001);

    // Red giant
    {
        CelestialBody s;
        s.pos = glm::vec3(-spacing, 0, 0); s.vel = glm::vec3(0);
        s.mass = 2.0f; s.fuel = 0.12f; s.seed = 14002;
        s.stellar_stage = SSTAGE_RED_GIANT;
        s.temperature = 3800.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.radius = expected_star_radius(s);
        s.angular_vel = 0.001f;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius, s.stellar_stage, s.fuel);
        s.name = "Red Giant";
        state.bodies.push_back(s); state.trails.emplace_back();
    }

    // Blue supergiant
    {
        CelestialBody s;
        s.pos = glm::vec3(0, 0, 0); s.vel = glm::vec3(0);
        s.mass = 25.0f; s.fuel = 0.45f; s.seed = 14003;
        s.stellar_stage = SSTAGE_SUPERGIANT;
        s.temperature = 28000.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.radius = expected_star_radius(s);
        s.angular_vel = 0.05f;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius, s.stellar_stage, s.fuel);
        s.name = "Blue Supergiant";
        state.bodies.push_back(s); state.trails.emplace_back();
    }

    // White dwarf
    {
        CelestialBody s;
        s.pos = glm::vec3(spacing, 0, 0); s.vel = glm::vec3(0);
        s.mass = 0.6f; s.fuel = 0.0f; s.seed = 14004;
        s.stellar_stage = SSTAGE_WHITE_DWARF;
        s.temperature = 25000.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.radius = expected_star_radius(s);
        s.angular_vel = 2.0f;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius, s.stellar_stage, s.fuel);
        s.name = "White Dwarf";
        state.bodies.push_back(s); state.trails.emplace_back();
    }

    // Neutron star
    {
        CelestialBody s;
        s.pos = glm::vec3(spacing * 2, 0, 0); s.vel = glm::vec3(0);
        s.mass = 1.4f; s.fuel = 0.0f; s.seed = 14005;
        s.stellar_stage = SSTAGE_NEUTRON_STAR;
        s.temperature = 120000.0f;
        s.type = classify_star_spectral(s.temperature, s.mass);
        s.radius = expected_star_radius(s);
        s.angular_vel = 300.0f;
        s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius, s.stellar_stage, s.fuel);
        s.name = "Neutron Star";
        state.bodies.push_back(s); state.trails.emplace_back();
    }

    cam.distance = 80000.0f; cam.target_distance = 80000.0f; cam.elevation = 0.15f;
}

// 14: Figure Eight — three equal-mass stars in a figure-8 orbit (Chenciner-Montgomery)
static void preset_figure_eight(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    // Scaled Chenciner-Montgomery initial conditions
    float scale = 200.0f;
    float m = 1.0f;
    // Body 1 at origin with velocity
    push_star(state, glm::vec3(-0.97000436f * scale, 0.0f, 0.24308753f * scale),
              glm::vec3(-0.93240737f * 0.05f, 0.0f, -0.86473146f * 0.05f),
              m, 0.80f, 15001);
    // Body 2
    push_star(state, glm::vec3(0.97000436f * scale, 0.0f, -0.24308753f * scale),
              glm::vec3(-0.93240737f * 0.05f, 0.0f, -0.86473146f * 0.05f),
              m, 0.80f, 15002);
    // Body 3 at origin
    push_star(state, glm::vec3(0.0f, 0.0f, 0.0f),
              glm::vec3(2.0f * 0.93240737f * 0.05f, 0.0f, 2.0f * 0.86473146f * 0.05f),
              m, 0.80f, 15003);

    cam.distance = 4000.0f; cam.target_distance = 4000.0f; cam.elevation = 0.8f;
}

// 15: Asteroid Belt — dense belt between inner rocky and outer gas planets
static void preset_asteroid_belt(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.0f, 0.72f, 16001);
    float sr = state.bodies[(size_t)star].radius;

    // Inner rocky planets
    push_planet(state, cfg, star, sr * 5.0f, 1.6e-7f, 600.0f, 0.0f, 16002, 0);
    push_planet(state, cfg, star, sr * 9.0f, 3.0e-6f, 300.0f, 1.4f, 16003, 3);
    push_planet(state, cfg, star, sr * 13.0f, 3.2e-7f, 220.0f, 3.0f, 16004, 0);

    // Dense asteroid belt
    std::mt19937 rng(16161);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 120; i++) {
        CelestialBody a;
        float r = randf(sr * 16.0f, sr * 22.0f);
        float angle = randf(0.0f, 6.2832f);
        float y = randf(-8.0f, 8.0f);
        a.pos = glm::vec3(std::cos(angle) * r, y, std::sin(angle) * r);
        float v = std::sqrt(cfg.G * 1.0f / r) * randf(0.95f, 1.05f);
        a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
        a.mass = randf(5.0e-12f, 8.0e-9f);
        a.radius = randf(0.3f, 1.8f);
        a.temperature = randf(80.0f, 200.0f);
        a.type = CTYPE_ASTEROID; a.seed = (uint32_t)(rng());
        a.name = generate_body_name(a.seed, a.type);
        state.bodies.push_back(a); state.trails.emplace_back();
    }

    // Outer gas giants
    int jup = push_planet(state, cfg, star, sr * 30.0f, 9.5e-4f, 120.0f, 4.5f, 16005, 4);
    auto& jb = state.bodies[(size_t)jup];
    set_ring_system(jb, jb.radius * 1.8f, jb.radius * 2.5f, 0.15f, 0.30f, 0.10f);
    push_planet(state, cfg, star, sr * 45.0f, 2.8e-4f, 80.0f, 1.0f, 16006, 4);

    cam.distance = sr * 55.0f; cam.target_distance = sr * 55.0f; cam.elevation = 0.5f;
}

// 16: Wolf-Rayet Supergiant — massive dying star shedding material
static void preset_wolf_rayet(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    CelestialBody s;
    s.pos = glm::vec3(0); s.vel = glm::vec3(0);
    s.mass = 40.0f; s.fuel = 0.15f; s.seed = 17001;
    s.stellar_stage = SSTAGE_SUPERGIANT;
    s.temperature = 45000.0f;
    s.type = CTYPE_STAR_WR;
    s.radius = expected_star_radius(s);
    s.angular_vel = 0.2f;
    s.luminosity = expected_stellar_luminosity(s.mass, s.temperature, s.radius, s.stellar_stage, s.fuel);
    s.name = "WR 104";
    state.bodies.push_back(s); state.trails.emplace_back();

    // Companion star — both orbit COM
    float m_comp = 8.0f;
    float comp_r = 400.0f;
    float mu_wr = m_comp / (s.mass + m_comp);
    float v_orb = std::sqrt(cfg.G * (s.mass + m_comp) / comp_r);
    // Adjust WR star to orbit COM
    state.bodies[0].pos = glm::vec3(-comp_r * mu_wr, 0, 0);
    state.bodies[0].vel = glm::vec3(0, 0, -v_orb * mu_wr);
    push_star(state, glm::vec3(comp_r * (1.0f - mu_wr), 0, 0),
              glm::vec3(0, 0, v_orb * (1.0f - mu_wr)), m_comp, 0.55f, 17002);

    // Ejected material / nebular shell
    std::mt19937 rng(17171);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 30; i++) {
        CelestialBody d;
        float r = randf(s.radius * 2.0f, 600.0f);
        float theta = randf(0.0f, 3.14159f);
        float phi = randf(0.0f, 6.28318f);
        d.pos = glm::vec3(r * std::sin(theta) * std::cos(phi),
                           r * std::sin(theta) * std::sin(phi),
                           r * std::cos(theta));
        float speed = randf(0.005f, 0.04f);
        d.vel = glm::normalize(d.pos) * speed;
        d.mass = randf(1e-9f, 5e-7f);
        d.radius = randf(0.5f, 3.0f);
        d.temperature = randf(2000.0f, 8000.0f);
        d.type = CTYPE_DUST; d.seed = (uint32_t)(rng());
        d.name = generate_body_name(d.seed, d.type);
        state.bodies.push_back(d); state.trails.emplace_back();
    }

    cam.distance = 3000.0f; cam.target_distance = 3000.0f; cam.elevation = 0.3f;
}

// 17: Collision Course — two star systems approaching each other
static void preset_collision_course(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();

    // System A: star with 2 planets, moving right
    glm::vec3 posA(-600, 0, 0), velA(0.025f, 0, 0.005f);
    int sA = push_star(state, posA, velA, 1.0f, 0.72f, 18001);
    float srA = state.bodies[(size_t)sA].radius;
    push_planet(state, cfg, sA, srA * 8.0f, 3.0e-6f, 300.0f, 0.0f, 18101, 3);
    push_planet(state, cfg, sA, srA * 16.0f, 5.0e-4f, 130.0f, 2.5f, 18102, 4);

    // System B: star with 2 planets, moving left
    glm::vec3 posB(600, 0, 50), velB(-0.025f, 0, -0.005f);
    int sB = push_star(state, posB, velB, 0.8f, 0.80f, 18002);
    float srB = state.bodies[(size_t)sB].radius;
    push_planet(state, cfg, sB, srB * 7.0f, 2.0e-6f, 340.0f, 1.2f, 18201, 1);
    push_planet(state, cfg, sB, srB * 14.0f, 8.0e-5f, 160.0f, 3.8f, 18202, 4);

    cam.distance = 5000.0f; cam.target_distance = 5000.0f; cam.elevation = 0.4f;
}

// 18: Nebula Collapse — gas cloud collapsing into a protostar
static void preset_nebula_collapse(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    std::mt19937 rng(19191);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    // Create a cloud of gas/dust particles
    float cloud_radius = 500.0f;
    float total_mass = 3.0f; // enough to eventually form a star
    int count = 200;
    float sigma = cloud_radius / 3.0f;

    // Random rotation for cloud shape
    glm::vec3 axis_scale(0.9f, 0.5f, 1.1f);

    // Give the cloud slow net rotation
    glm::vec3 rot_axis = glm::normalize(glm::vec3(0.1f, 1.0f, 0.05f));
    float rot_speed = 0.003f;

    for (int i = 0; i < count; i++) {
        CelestialBody p;
        float mass_weight = std::pow(randf(0.0f, 1.0f), 0.6f) + 0.1f;
        p.mass = total_mass * mass_weight / (float)count;

        glm::vec3 local(gauss(rng) * sigma * axis_scale.x,
                        gauss(rng) * sigma * axis_scale.y,
                        gauss(rng) * sigma * axis_scale.z);
        p.pos = local;

        // Rotational velocity + turbulence
        glm::vec3 r_vec = p.pos;
        glm::vec3 v_rot = glm::cross(rot_axis, r_vec) * rot_speed;
        glm::vec3 v_turb(gauss(rng) * 0.003f, gauss(rng) * 0.002f, gauss(rng) * 0.003f);
        p.vel = v_rot + v_turb;

        p.radius = std::cbrt(p.mass * 1.0e6f) * 2.5f + randf(1.0f, 4.0f);
        p.temperature = randf(15.0f, 60.0f); // cold molecular cloud
        p.type = CTYPE_NEBULA;
        p.seed = (uint32_t)(rng());
        p.material_phase = PHASE_GAS;
        p.phase_intensity = randf(0.5f, 0.9f);
        p.name = generate_body_name(p.seed, p.type);
        state.bodies.push_back(p); state.trails.emplace_back();
    }

    cam.distance = 1500.0f; cam.target_distance = 1500.0f; cam.elevation = 0.55f;
}

// 19: Pulsar Binary — millisecond pulsar with a white dwarf companion being stripped
static void preset_pulsar_binary(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();

    float m1 = 1.8f, m2 = 0.4f;
    float sep = 60.0f;
    float mu = m2 / (m1 + m2);  // mass fraction for COM
    float v_orb = std::sqrt(cfg.G * (m1 + m2) / sep);

    // Millisecond pulsar — orbits COM
    CelestialBody ns;
    ns.pos = glm::vec3(-sep * mu, 0, 0);
    ns.vel = glm::vec3(0, 0, -v_orb * mu);
    ns.mass = m1; ns.temperature = 200000.0f;
    ns.stellar_stage = SSTAGE_NEUTRON_STAR; ns.fuel = 0.0f; ns.seed = 19001;
    ns.type = classify_star_spectral(ns.temperature, ns.mass);
    ns.radius = expected_star_radius(ns);
    ns.angular_vel = 600.0f; // millisecond pulsar
    ns.luminosity = expected_stellar_luminosity(ns.mass, ns.temperature, ns.radius, ns.stellar_stage, 0.0f);
    ns.name = "PSR J1614-2230";
    state.bodies.push_back(ns); state.trails.emplace_back();

    // White dwarf companion — orbits COM
    CelestialBody wd;
    wd.pos = glm::vec3(sep * (1.0f - mu), 0, 0);
    wd.vel = glm::vec3(0, 0, v_orb * (1.0f - mu));
    wd.mass = m2; wd.temperature = 18000.0f;
    wd.stellar_stage = SSTAGE_WHITE_DWARF; wd.fuel = 0.0f; wd.seed = 19002;
    wd.type = classify_star_spectral(wd.temperature, wd.mass);
    wd.radius = expected_star_radius(wd);
    wd.angular_vel = 1.0f;
    wd.luminosity = expected_stellar_luminosity(wd.mass, wd.temperature, wd.radius, wd.stellar_stage, 0.0f);
    wd.name = "WD Companion";
    state.bodies.push_back(wd); state.trails.emplace_back();

    cam.distance = 250.0f; cam.target_distance = 250.0f; cam.elevation = 0.3f;
}

// 20: Trojan Asteroids — Jupiter-like planet with Trojans at L4/L5
static void preset_trojans(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 1.0f, 0.72f, 20001);
    float sr = state.bodies[(size_t)star].radius;

    // Jupiter analogue
    float jup_r = sr * 22.0f;
    int jup = push_planet(state, cfg, star, jup_r, 9.5e-4f, 130.0f, 0.0f, 20002, 4);
    auto& jb = state.bodies[(size_t)jup];
    set_ring_system(jb, jb.radius * 1.5f, jb.radius * 2.2f, 0.12f, 0.20f, 0.08f);

    // L4 and L5 Trojan swarms (60° ahead and behind Jupiter)
    float jup_angle = 0.0f;
    std::mt19937 rng(20202);
    auto randf = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int group = 0; group < 2; group++) {
        float l_angle = jup_angle + (group == 0 ? 1.0472f : -1.0472f); // ±60°
        for (int i = 0; i < 35; i++) {
            CelestialBody a;
            float angle_scatter = randf(-0.25f, 0.25f);
            float r_scatter = randf(-jup_r * 0.15f, jup_r * 0.15f);
            float r = jup_r + r_scatter;
            float angle = l_angle + angle_scatter;
            float y = randf(-5.0f, 5.0f);
            a.pos = glm::vec3(std::cos(angle) * r, y, std::sin(angle) * r);
            float v = std::sqrt(cfg.G * 1.0f / r) * randf(0.97f, 1.03f);
            a.vel = glm::vec3(-std::sin(angle) * v, 0.0f, std::cos(angle) * v);
            a.mass = randf(1e-12f, 1e-9f);
            a.radius = randf(0.2f, 1.2f);
            a.temperature = randf(100.0f, 180.0f);
            a.type = CTYPE_ASTEROID; a.seed = (uint32_t)(rng());
            a.name = generate_body_name(a.seed, a.type);
            state.bodies.push_back(a); state.trails.emplace_back();
        }
    }

    // Inner rocky planets
    push_planet(state, cfg, star, sr * 6.0f, 1.6e-7f, 500.0f, 1.2f, 20003, 0);
    push_planet(state, cfg, star, sr * 10.0f, 3.0e-6f, 290.0f, 3.0f, 20004, 3);

    cam.distance = sr * 30.0f; cam.target_distance = sr * 30.0f; cam.elevation = 0.6f;
}

// 21: Exomoon System — detailed gas giant with diverse moon system
static void preset_exomoon_system(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    int star = push_star(state, glm::vec3(0), glm::vec3(0), 0.85f, 0.80f, 21001);
    float sr = state.bodies[(size_t)star].radius;

    // Large gas giant host
    int host = push_planet(state, cfg, star, sr * 15.0f, 1.2e-3f, 140.0f, 0.0f, 21002, 4);
    auto& hb = state.bodies[(size_t)host];
    set_ring_system(hb, hb.radius * 1.4f, hb.radius * 3.0f, 0.50f, 0.45f, 0.30f);

    // Diverse moons: volcanic, icy, ocean, captured asteroid
    push_moon(state, cfg, host, hb.radius * 3.5f, 1.5e-7f, 800.0f, 0.0f, 21101); // volcanic Io-like
    auto& volcanic = state.bodies.back();
    volcanic.temperature = 800.0f; // tidally heated

    push_moon(state, cfg, host, hb.radius * 5.5f, 3.0e-7f, 270.0f, 1.2f, 21102); // ocean Europa-like
    push_moon(state, cfg, host, hb.radius * 8.0f, 5.0e-7f, 110.0f, 2.6f, 21103); // icy Ganymede-like
    push_moon(state, cfg, host, hb.radius * 11.0f, 4.0e-7f, 95.0f, 4.0f, 21104);  // Callisto-like

    // Tiny irregular captured moons in distant retrograde orbits
    push_moon(state, cfg, host, hb.radius * 18.0f, 5.0e-10f, 60.0f, 5.5f, 21105);
    push_moon(state, cfg, host, hb.radius * 22.0f, 3.0e-10f, 55.0f, 0.8f, 21106);

    cam.focus_on(hb.pos, host, hb.radius);
    cam.distance = hb.radius * 30.0f; cam.target_distance = hb.radius * 30.0f; cam.elevation = 0.35f;
}

// 22: Hierarchical Triple — close binary orbited by distant third star with planets
static void preset_hierarchical_triple(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    // Inner binary
    float sep = 50.0f;
    float m1 = 1.2f, m2 = 0.9f;
    float mu = m2 / (m1 + m2);
    float v_inner = std::sqrt(cfg.G * (m1 + m2) / sep);

    int s1 = push_star(state, glm::vec3(-sep * mu, 0, 0),
                       glm::vec3(0, 0, -v_inner * mu), m1, 0.68f, 22001);
    int s2 = push_star(state, glm::vec3(sep * (1 - mu), 0, 0),
                       glm::vec3(0, 0, v_inner * (1 - mu)), m2, 0.75f, 22002);
    (void)s1; (void)s2;

    // Distant third star with planets
    float outer_r = 800.0f;
    float m3 = 0.5f;
    float v_outer = std::sqrt(cfg.G * (m1 + m2 + m3) / outer_r);
    int s3 = push_star(state, glm::vec3(outer_r, 0, 0),
                       glm::vec3(0, 0, v_outer), m3, 0.90f, 22003);
    float sr3 = state.bodies[(size_t)s3].radius;

    push_planet(state, cfg, s3, sr3 * 6.0f, 4.0e-6f, 260.0f, 0.5f, 22101, 3);
    push_planet(state, cfg, s3, sr3 * 10.0f, 8.0e-5f, 120.0f, 2.8f, 22102, 4);

    cam.distance = 4000.0f; cam.target_distance = 4000.0f; cam.elevation = 0.45f;
}

// 23: Tatooine Sunset — circumbinary habitable planet (two suns in the sky)
static void preset_tatooine(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();
    float sep = 40.0f;
    float m1 = 0.7f, m2 = 0.35f;
    float mu = m2 / (m1 + m2);
    float v = std::sqrt(cfg.G * (m1 + m2) / sep);

    int s1 = push_star(state, glm::vec3(-sep * mu, 0, 0),
                       glm::vec3(0, 0, -v * mu), m1, 0.82f, 23001);
    int s2 = push_star(state, glm::vec3(sep * (1 - mu), 0, 0),
                       glm::vec3(0, 0, v * (1 - mu)), m2, 0.90f, 23002);
    (void)s1; (void)s2;

    // Habitable circumbinary planet
    float cb_r = 200.0f;
    CelestialBody p;
    p.pos = glm::vec3(cb_r, 0, 0);
    float cb_v = std::sqrt(cfg.G * (m1 + m2) / cb_r);
    p.vel = glm::vec3(0, 0, cb_v);
    p.mass = 4.0e-6f; p.temperature = 280.0f;
    p.type = CTYPE_PLANET; p.seed = 23101;
    p.radius = expected_planet_radius(p.mass);
    p.forced_surface = 3; // earth-like
    p.name = "Tatooine";
    state.bodies.push_back(p); state.trails.emplace_back();
    int pi = (int)state.bodies.size() - 1;
    auto& pb = state.bodies[(size_t)pi];

    push_moon(state, cfg, pi, pb.radius * 6.0f, 2.0e-8f, 200.0f, 0.0f, 23201);
    push_moon(state, cfg, pi, pb.radius * 10.0f, 8.0e-9f, 160.0f, 2.5f, 23202);

    cam.focus_on(p.pos, pi, pb.radius);
    cam.distance = std::max(pb.radius * 20.0f, 200.0f);
    cam.target_distance = cam.distance;
    cam.elevation = 0.2f;
}

// 24: Black Hole Accretion — stellar black hole tearing apart a star
static void preset_bh_accretion(CosmosState& state, const CosmosConfig& cfg, OrbitCamera& cam) {
    state.clear();

    float m_bh = 15.0f, m_donor = 8.0f;
    float orbit_r = 200.0f;
    float mu = m_donor / (m_bh + m_donor);
    float v_orb = std::sqrt(cfg.G * (m_bh + m_donor) / orbit_r);

    // Stellar black hole — orbits COM
    CelestialBody bh;
    bh.pos = glm::vec3(-orbit_r * mu, 0, 0);
    bh.vel = glm::vec3(0, 0, -v_orb * mu);
    bh.mass = m_bh; bh.temperature = 0.0f; bh.seed = 24001;
    bh.type = classify_black_hole(bh.mass);
    bh.radius = std::max(2.0f * cfg.G * bh.mass / (300.0f * 300.0f) * 80.0f, 3.0f);
    bh.angular_vel = 50.0f;
    bh.name = "Cygnus X-1";
    state.bodies.push_back(bh); state.trails.emplace_back();

    // Donor star — orbits COM
    CelestialBody donor;
    donor.pos = glm::vec3(orbit_r * (1.0f - mu), 0, 0);
    donor.vel = glm::vec3(0, 0, v_orb * (1.0f - mu));
    donor.mass = m_donor; donor.fuel = 0.50f; donor.seed = 24002;
    donor.stellar_stage = SSTAGE_SUPERGIANT;
    donor.temperature = 25000.0f;
    donor.type = classify_star_spectral(donor.temperature, donor.mass);
    donor.radius = expected_star_radius(donor);
    donor.angular_vel = 0.04f;
    donor.luminosity = expected_stellar_luminosity(donor.mass, donor.temperature, donor.radius, donor.stellar_stage, donor.fuel);
    donor.name = "Blue Supergiant";
    state.bodies.push_back(donor); state.trails.emplace_back();

    cam.distance = 600.0f; cam.target_distance = 600.0f; cam.elevation = 0.3f;
}

const CosmosPreset COSMOS_PRESETS[] = {
    {"Solar System",          "Sun with 4 planets, a moon, and an asteroid belt",                    preset_solar_system},
    {"Binary Stars",          "Two stars in mutual orbit with circumbinary planets",                  preset_binary_stars},
    {"TRAPPIST-1",            "Red dwarf with 7 tightly packed rocky/water worlds",                  preset_trappist},
    {"Hot Jupiter",           "Gas giant dangerously close to its star",                             preset_hot_jupiter},
    {"Giant Impact",          "Earth and Theia moments before the Moon-forming collision",           preset_giant_impact},
    {"Stellar Graveyard",     "Neutron star, white dwarf, and stellar black hole",                   preset_stellar_graveyard},
    {"Protoplanetary Disk",   "Young star surrounded by a disk of dust and forming planets",         preset_protoplanetary_disk},
    {"Ringed Worlds",         "Gas giants and rocky planets with spectacular ring systems",          preset_ringed_worlds},
    {"Star Cluster",          "A dozen diverse stars in a loose open cluster",                       preset_star_cluster},
    {"Comet Shower",          "Inner solar system under bombardment from Oort cloud comets",         preset_comet_shower},
    {"Rogue Planet",          "A wandering gas giant with moons and captured asteroids",             preset_rogue_planet},
    {"Supermassive Black Hole","Galactic center with stars orbiting a 4-million solar mass black hole", preset_supermassive_bh},
    {"Habitable Zone Tour",   "Four different habitable worlds around various star types",           preset_habitable_zone},
    {"Stellar Evolution",     "Stars at every life stage from main sequence to neutron star",        preset_stellar_evolution},
    {"Figure Eight",          "Three equal-mass stars in a stable figure-8 choreography",            preset_figure_eight},
    {"Asteroid Belt",         "Rocky planets, a dense asteroid belt, and outer gas giants",          preset_asteroid_belt},
    {"Wolf-Rayet Star",       "Massive dying star shedding its outer layers",                        preset_wolf_rayet},
    {"Collision Course",      "Two star systems on a direct approach toward each other",             preset_collision_course},
    {"Nebula Collapse",       "Giant gas cloud collapsing under gravity to form stars",              preset_nebula_collapse},
    {"Pulsar Binary",         "Millisecond pulsar stripping mass from a white dwarf companion",     preset_pulsar_binary},
    {"Trojan Asteroids",      "Jupiter-like planet with asteroid swarms at L4 and L5 points",       preset_trojans},
    {"Exomoon System",        "Gas giant with volcanic, ocean, and icy moons in detail",            preset_exomoon_system},
    {"Hierarchical Triple",   "Close binary star orbited by a distant third star with planets",     preset_hierarchical_triple},
    {"Tatooine",              "Habitable world orbiting twin suns with two moons",                  preset_tatooine},
    {"Black Hole Accretion",  "Stellar black hole tearing apart a blue supergiant companion",      preset_bh_accretion},
};
const int COSMOS_PRESET_COUNT = (int)(sizeof(COSMOS_PRESETS) / sizeof(COSMOS_PRESETS[0]));
