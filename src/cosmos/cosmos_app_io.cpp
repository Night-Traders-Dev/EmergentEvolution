#include "cosmos/cosmos_app_internal.h"

#include <fstream>

namespace {

constexpr uint32_t COSMOS_MAGIC   = 0x534D4F43; // "COSM"
constexpr uint32_t COSMOS_VERSION = 4;

#pragma pack(push, 1)
struct BodyPODV1 {
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
    uint32_t name_len;
};

struct BodyPODV2 {
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
    float atmosphere_retention;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len;
};

struct BodyPODV3 {
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
    float atmosphere_retention;
    float phase_intensity;
    float collapse_progress;
    float ring_inner_radius;
    float ring_outer_radius;
    float ring_density;
    float ring_ice_fraction;
    float ring_tilt;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t material_phase;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len;
};

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
    float atmosphere_retention;
    float phase_intensity;
    float collapse_progress;
    float ring_inner_radius;
    float ring_outer_radius;
    float ring_density;
    float ring_ice_fraction;
    float ring_tilt;
    float impact_normal[3];
    float impact_crater_strength;
    float impact_heat;
    float impact_radius;
    float impact_ejecta;
    float angular_vel;
    uint32_t stellar_stage;
    uint32_t material_phase;
    uint32_t seed;
    uint32_t frag_generation;
    uint32_t name_len;
};
#pragma pack(pop)

} // namespace

bool CosmosApp::save_simulation(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&COSMOS_MAGIC), 4);
    f.write(reinterpret_cast<const char*>(&COSMOS_VERSION), 4);

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
    if (cfg.material_phases) flags |= 2048;
    if (cfg.planetary_rings) flags |= 4096;
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

    uint32_t body_count = (uint32_t)state.bodies.size();
    f.write(reinterpret_cast<const char*>(&body_count), 4);

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
        pod.atmosphere_retention = b.atmosphere_retention;
        pod.phase_intensity = b.phase_intensity;
        pod.collapse_progress = b.collapse_progress;
        pod.ring_inner_radius = b.ring_inner_radius;
        pod.ring_outer_radius = b.ring_outer_radius;
        pod.ring_density = b.ring_density;
        pod.ring_ice_fraction = b.ring_ice_fraction;
        pod.ring_tilt = b.ring_tilt;
        pod.impact_normal[0] = b.impact_normal.x;
        pod.impact_normal[1] = b.impact_normal.y;
        pod.impact_normal[2] = b.impact_normal.z;
        pod.impact_crater_strength = b.impact_crater_strength;
        pod.impact_heat = b.impact_heat;
        pod.impact_radius = b.impact_radius;
        pod.impact_ejecta = b.impact_ejecta;
        pod.angular_vel = b.angular_vel;
        pod.stellar_stage = b.stellar_stage;
        pod.material_phase = b.material_phase;
        pod.seed = b.seed;
        pod.frag_generation = b.frag_generation;
        pod.name_len = (uint32_t)b.name.size();
        f.write(reinterpret_cast<const char*>(&pod), sizeof(BodyPOD));
        if (pod.name_len > 0)
            f.write(b.name.data(), pod.name_len);
    }

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
    cfg.material_phases         = (flags & 2048) != 0;
    cfg.planetary_rings         = (flags & 4096) != 0;
    if (version < 3) {
        cfg.material_phases = true;
        cfg.planetary_rings = true;
    }

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

    uint32_t body_count = 0;
    f.read(reinterpret_cast<char*>(&body_count), 4);
    if (body_count > 10000) return false;

    state.clear();
    state.bodies.reserve(body_count);

    for (uint32_t i = 0; i < body_count; i++) {
        CelestialBody b;
        uint32_t name_len = 0;
        if (version >= 4) {
            BodyPOD pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPOD));
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
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = pod.phase_intensity;
            b.collapse_progress = pod.collapse_progress;
            b.ring_inner_radius = pod.ring_inner_radius;
            b.ring_outer_radius = pod.ring_outer_radius;
            b.ring_density = pod.ring_density;
            b.ring_ice_fraction = pod.ring_ice_fraction;
            b.ring_tilt = pod.ring_tilt;
            b.impact_normal = {pod.impact_normal[0], pod.impact_normal[1], pod.impact_normal[2]};
            b.impact_crater_strength = pod.impact_crater_strength;
            b.impact_heat = pod.impact_heat;
            b.impact_radius = pod.impact_radius;
            b.impact_ejecta = pod.impact_ejecta;
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = pod.material_phase;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else if (version >= 3) {
            BodyPODV3 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV3));
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
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = pod.phase_intensity;
            b.collapse_progress = pod.collapse_progress;
            b.ring_inner_radius = pod.ring_inner_radius;
            b.ring_outer_radius = pod.ring_outer_radius;
            b.ring_density = pod.ring_density;
            b.ring_ice_fraction = pod.ring_ice_fraction;
            b.ring_tilt = pod.ring_tilt;
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = pod.material_phase;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else if (version >= 2) {
            BodyPODV2 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV2));
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
            b.atmosphere_retention = pod.atmosphere_retention;
            b.phase_intensity = 0.0f;
            b.collapse_progress = 0.0f;
            clear_ring_system(b);
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = PHASE_SOLID;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        } else {
            BodyPODV1 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV1));
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
            b.atmosphere_retention = 1.0f;
            b.phase_intensity = 0.0f;
            b.collapse_progress = 0.0f;
            clear_ring_system(b);
            clear_impact_signature(b);
            b.angular_vel = pod.angular_vel;
            b.stellar_stage = pod.stellar_stage;
            b.material_phase = PHASE_SOLID;
            b.seed = pod.seed;
            b.frag_generation = pod.frag_generation;
            name_len = pod.name_len;
        }
        if (name_len > 0 && name_len < 256) {
            b.name.resize(name_len);
            f.read(b.name.data(), name_len);
        }
        state.bodies.push_back(std::move(b));
        state.trails.emplace_back();
    }

    f.read(reinterpret_cast<char*>(&camera.azimuth), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.elevation), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.distance), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.fov), sizeof(float));
    f.read(reinterpret_cast<char*>(&camera.target), sizeof(glm::vec3));

    cfg.body_count = (uint32_t)state.bodies.size();
    selected_body = -1;
    sim_time_ = 0.0f;

    for (auto& b : state.bodies) refresh_body_render_state(b, &state);

    return f.good();
}

bool CosmosApp::export_body(int index, const std::string& path) {
    if (index < 0 || index >= (int)state.bodies.size()) return false;
    const auto& b = state.bodies[index];

    std::ofstream f(path);
    if (!f) return false;

    f << "CSBODY 3\n";
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
    f << "atmosphere_retention " << b.atmosphere_retention << "\n";
    f << "material_phase " << b.material_phase << "\n";
    f << "phase_intensity " << b.phase_intensity << "\n";
    f << "collapse_progress " << b.collapse_progress << "\n";
    f << "ring " << b.ring_inner_radius << " " << b.ring_outer_radius << " "
      << b.ring_density << " " << b.ring_ice_fraction << " " << b.ring_tilt << "\n";
    f << "impact_normal " << b.impact_normal.x << " " << b.impact_normal.y << " " << b.impact_normal.z << "\n";
    f << "impact_state " << b.impact_crater_strength << " " << b.impact_heat << " "
      << b.impact_radius << " " << b.impact_ejecta << "\n";
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
        else if (key == "atmosphere_retention") { f >> b.atmosphere_retention; }
        else if (key == "material_phase") { f >> b.material_phase; }
        else if (key == "phase_intensity") { f >> b.phase_intensity; }
        else if (key == "collapse_progress") { f >> b.collapse_progress; }
        else if (key == "ring") {
            f >> b.ring_inner_radius >> b.ring_outer_radius >> b.ring_density
              >> b.ring_ice_fraction >> b.ring_tilt;
        }
        else if (key == "impact_normal") { f >> b.impact_normal.x >> b.impact_normal.y >> b.impact_normal.z; }
        else if (key == "impact_state") {
            f >> b.impact_crater_strength >> b.impact_heat >> b.impact_radius >> b.impact_ejecta;
        }
        else if (key == "angular_vel") { f >> b.angular_vel; }
        else if (key == "stellar_stage") { f >> b.stellar_stage; }
        else if (key == "parent") { f >> b.parent; }
        else if (key == "frag_generation") { f >> b.frag_generation; }
    }

    if (b.name == "Unnamed") b.name.clear();
    refresh_body_render_state(b, &state);
    state.bodies.push_back(std::move(b));
    state.trails.emplace_back();
    return true;
}
