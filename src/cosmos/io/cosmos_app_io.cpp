#include "cosmos/cosmos_app_internal.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <type_traits>

namespace {

constexpr uint32_t COSMOS_MAGIC   = 0x534D4F43; // "COSM"
constexpr uint32_t COSMOS_VERSION = 13;
constexpr uint32_t COSMOS_SETTINGS_MAGIC   = 0x54475343; // "CSGT"
constexpr uint32_t COSMOS_SETTINGS_VERSION = 4;
constexpr const char* COSMOS_SETTINGS_PATH = "cosmos_settings.bin";

struct PersistedUiSettingsV1 {
    int32_t spawn_type = CTYPE_PLANET;
    float spawn_mass = 3.003e-6f;
    uint8_t spawn_in_orbit = 0;
    uint8_t spawn_menu_visible = 1;
    uint8_t settings_visible = 1;
    uint8_t body_list_visible = 1;
    uint8_t bottom_bar_autohide = 1;
    uint8_t override_temperature = 0;
    float temperature = 300.0f;
    uint8_t override_radius = 0;
    float radius = 8.0f;
    uint8_t override_rotation = 0;
    float rotation_hours = 24.0f;
    uint8_t override_velocity = 0;
    float velocity_kms[3] = {0.0f, 0.0f, 0.0f};
    uint8_t override_material = 0;
    float material_iron = 0.20f;
    float material_silicate = 0.60f;
    float material_ice = 0.20f;
    float material_hydrogen = 0.0f;
    int32_t planet_look = 0;
    uint8_t spawn_rings = 0;
    uint8_t spawn_moons = 0;
    int32_t moon_count = 1;
    uint8_t override_ring_layout = 0;
    float ring_inner_mult = 1.6f;
    float ring_outer_mult = 3.0f;
    float ring_density = 0.35f;
    float ring_ice_fraction = 0.55f;
    int32_t small_body_spawn_count = 1;
    int32_t small_body_layout = 0;
};

struct PersistedUiSettingsV2 {
    int32_t spawn_type = CTYPE_PLANET;
    float spawn_mass = 3.003e-6f;
    uint8_t spawn_in_orbit = 0;
    uint8_t spawn_menu_visible = 1;
    uint8_t settings_visible = 1;
    uint8_t body_list_visible = 1;
    uint8_t bottom_bar_autohide = 1;
    uint8_t override_temperature = 0;
    float temperature = 300.0f;
    uint8_t override_radius = 0;
    float radius = 8.0f;
    uint8_t override_rotation = 0;
    float rotation_hours = 24.0f;
    uint8_t override_velocity = 0;
    float velocity_kms[3] = {0.0f, 0.0f, 0.0f};
    uint8_t override_material = 0;
    float material_iron = 0.20f;
    float material_silicate = 0.60f;
    float material_ice = 0.20f;
    float material_hydrogen = 0.0f;
    int32_t planet_look = 0;
    uint8_t spawn_rings = 0;
    uint8_t spawn_moons = 0;
    int32_t moon_count = 1;
    int32_t moon_orbit_layout = 0;
    float moon_inclination_deg = 8.0f;
    float moon_spacing_scale = 1.0f;
    uint8_t override_ring_layout = 0;
    float ring_inner_mult = 1.6f;
    float ring_outer_mult = 3.0f;
    float ring_density = 0.35f;
    float ring_ice_fraction = 0.55f;
    int32_t small_body_spawn_count = 1;
    int32_t small_body_layout = 0;
};

struct PersistedUiSettingsV3 {
    int32_t spawn_type = CTYPE_PLANET;
    float spawn_mass = 3.003e-6f;
    uint8_t spawn_in_orbit = 0;
    uint8_t spawn_menu_visible = 1;
    uint8_t settings_visible = 1;
    uint8_t body_list_visible = 1;
    uint8_t bottom_bar_autohide = 1;
    uint8_t override_temperature = 0;
    float temperature = 300.0f;
    uint8_t override_radius = 0;
    float radius = 8.0f;
    uint8_t override_rotation = 0;
    float rotation_hours = 24.0f;
    uint8_t override_velocity = 0;
    float velocity_kms[3] = {0.0f, 0.0f, 0.0f};
    uint8_t override_material = 0;
    float material_iron = 0.20f;
    float material_silicate = 0.60f;
    float material_ice = 0.20f;
    float material_hydrogen = 0.0f;
    int32_t planet_look = 0;
    uint8_t spawn_rings = 0;
    uint8_t spawn_moons = 0;
    int32_t moon_count = 1;
    int32_t moon_orbit_layout = 0;
    float moon_inclination_deg = 8.0f;
    float moon_spacing_scale = 1.0f;
    uint8_t override_ring_layout = 0;
    int32_t ring_layout_type = 4;
    float ring_inner_mult = 1.6f;
    float ring_outer_mult = 3.0f;
    float ring_density = 0.35f;
    float ring_ice_fraction = 0.55f;
    int32_t small_body_spawn_count = 1;
    int32_t small_body_layout = 0;
};

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

struct BodyPODV5 {
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
    uint8_t non_attracting;
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
    if (cfg.roche_limit_fluid) flags |= 8192;
    if (cfg.roche_limit_rigid) flags |= 16384;
    if (cfg.dynamic_budget_enabled) flags |= 32768;
    if (cfg.dust_debug_non_attracting) flags |= 65536;
    if (cfg.adaptive_time_step) flags |= 131072;
    if (cfg.barnes_hut) flags |= 262144;
    if (cfg.velocity_verlet) flags |= 524288;
    if (cfg.gpu_barnes_hut) flags |= 1048576;
    if (cfg.collision_sph) flags |= 2097152;
    if (cfg.collision_rigid_body_dynamics) flags |= 4194304;
    if (cfg.spin_fragmentation) flags |= 8388608;
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
    f.write(reinterpret_cast<const char*>(&cfg.spin_fragmentation_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.dynamic_max_fragments), sizeof(int));
    f.write(reinterpret_cast<const char*>(&cfg.dynamic_max_non_attracting), sizeof(int));
    f.write(reinterpret_cast<const char*>(&cfg.dynamic_explosion_density), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.dynamic_reduction_percent), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.dynamic_target_fps), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_inner_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_outer_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_density_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_thickness_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_particle_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.ring_mass_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.adaptive_step_safety), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.adaptive_step_min), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.adaptive_step_max), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.barnes_hut_theta), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.barnes_hut_min_bodies), sizeof(int));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_gravity_advection_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_gravity_collapse_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_gravity_compress_scale), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_sink_threshold), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_sink_min_mass), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_sink_spawn_fraction), sizeof(float));
    f.write(reinterpret_cast<const char*>(&cfg.nebula_sink_consume_fraction), sizeof(float));
    uint8_t sink_flag = cfg.nebula_sink_formation ? 1u : 0u;
    f.write(reinterpret_cast<const char*>(&sink_flag), sizeof(uint8_t));

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
        pod.non_attracting = b.non_attracting ? 1u : 0u;
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
    if (version >= 5) {
        cfg.roche_limit_fluid   = (flags & 8192) != 0;
        cfg.roche_limit_rigid   = (flags & 16384) != 0;
    } else {
        cfg.roche_limit_fluid = true;
        cfg.roche_limit_rigid = true;
    }
    if (version >= 6) cfg.dynamic_budget_enabled = (flags & 32768) != 0;
    else cfg.dynamic_budget_enabled = true;
    if (version >= 7) cfg.dust_debug_non_attracting = (flags & 65536) != 0;
    else cfg.dust_debug_non_attracting = true;
    if (version >= 9) cfg.adaptive_time_step = (flags & 131072) != 0;
    else cfg.adaptive_time_step = false;
    if (version >= 9) cfg.barnes_hut = (flags & 262144) != 0;
    else cfg.barnes_hut = true;
    if (version >= 9) cfg.velocity_verlet = (flags & 524288) != 0;
    else cfg.velocity_verlet = true;
    if (version >= 10) cfg.gpu_barnes_hut = (flags & 1048576) != 0;
    else cfg.gpu_barnes_hut = false;
    if (version >= 12) cfg.collision_sph = (flags & 2097152) != 0;
    else cfg.collision_sph = true;
    if (version >= 12) cfg.collision_rigid_body_dynamics = (flags & 4194304) != 0;
    else cfg.collision_rigid_body_dynamics = true;
    if (version >= 13) cfg.spin_fragmentation = (flags & 8388608) != 0;
    else cfg.spin_fragmentation = true;
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
    if (version >= 13) {
        f.read(reinterpret_cast<char*>(&cfg.spin_fragmentation_threshold), sizeof(float));
    } else {
        cfg.spin_fragmentation_threshold = 0.92f;
    }
    if (version >= 6) {
        f.read(reinterpret_cast<char*>(&cfg.dynamic_max_fragments), sizeof(int));
        f.read(reinterpret_cast<char*>(&cfg.dynamic_max_non_attracting), sizeof(int));
        f.read(reinterpret_cast<char*>(&cfg.dynamic_explosion_density), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.dynamic_reduction_percent), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.dynamic_target_fps), sizeof(float));
    } else {
        cfg.dynamic_max_fragments = 300;
        cfg.dynamic_max_non_attracting = 900;
        cfg.dynamic_explosion_density = 0.25f;
        cfg.dynamic_reduction_percent = 0.20f;
        cfg.dynamic_target_fps = 60.0f;
    }
    if (version >= 8) {
        f.read(reinterpret_cast<char*>(&cfg.ring_inner_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.ring_outer_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.ring_density_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.ring_thickness_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.ring_particle_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.ring_mass_scale), sizeof(float));
        if (version >= 9) {
            f.read(reinterpret_cast<char*>(&cfg.adaptive_step_safety), sizeof(float));
            f.read(reinterpret_cast<char*>(&cfg.adaptive_step_min), sizeof(float));
            f.read(reinterpret_cast<char*>(&cfg.adaptive_step_max), sizeof(float));
            f.read(reinterpret_cast<char*>(&cfg.barnes_hut_theta), sizeof(float));
            f.read(reinterpret_cast<char*>(&cfg.barnes_hut_min_bodies), sizeof(int));
        } else {
            cfg.adaptive_step_safety = 0.22f;
            cfg.adaptive_step_min = 1.0e-4f;
            cfg.adaptive_step_max = 500000.0f;
            cfg.barnes_hut_theta = 0.72f;
            cfg.barnes_hut_min_bodies = 128;
        }
    } else {
        cfg.ring_inner_scale = 1.0f;
        cfg.ring_outer_scale = 1.0f;
        cfg.ring_density_scale = 1.0f;
        cfg.ring_thickness_scale = 1.0f;
        cfg.ring_particle_scale = 1.0f;
        cfg.ring_mass_scale = 1.0f;
        cfg.adaptive_step_safety = 0.22f;
        cfg.adaptive_step_min = 1.0e-4f;
        cfg.adaptive_step_max = 500000.0f;
        cfg.barnes_hut_theta = 0.72f;
        cfg.barnes_hut_min_bodies = 128;
    }

    // Clamp legacy/invalid values from old saves to sane runtime ranges.
    cfg.merge_speed_threshold = std::max(cfg.merge_speed_threshold, 0.1f);
    cfg.fragment_speed_threshold = std::max(cfg.fragment_speed_threshold, 0.1f);
    cfg.fragment_count = std::clamp(cfg.fragment_count, 1, 12);
    cfg.min_fragment_mass = std::clamp(cfg.min_fragment_mass, 1.0e-9f, 10.0f);
    cfg.max_frag_generation = std::clamp(cfg.max_frag_generation, 0, 8);
    cfg.spin_fragmentation_threshold = std::clamp(cfg.spin_fragmentation_threshold, 0.50f, 2.0f);
    cfg.body_label_min_distance = std::clamp(cfg.body_label_min_distance, 0.0f, 1.0e8f);
    cfg.body_label_max_distance = std::clamp(cfg.body_label_max_distance,
                                             std::max(cfg.body_label_min_distance, 1.0e-3f), 1.0e8f);
    cfg.dynamic_max_fragments = std::clamp(cfg.dynamic_max_fragments, 0, 10000);
    cfg.dynamic_max_non_attracting = std::clamp(cfg.dynamic_max_non_attracting, 0, 50000);
    cfg.dynamic_explosion_density = std::clamp(cfg.dynamic_explosion_density, 0.01f, 1.0f);
    cfg.dynamic_reduction_percent = std::clamp(cfg.dynamic_reduction_percent, 0.01f, 1.0f);
    cfg.dynamic_target_fps = std::clamp(cfg.dynamic_target_fps, 1.0f, 1000.0f);
    cfg.ring_inner_scale = std::clamp(cfg.ring_inner_scale, 0.6f, 3.0f);
    cfg.ring_outer_scale = std::clamp(cfg.ring_outer_scale, 0.6f, 3.0f);
    cfg.ring_density_scale = std::clamp(cfg.ring_density_scale, 0.2f, 3.0f);
    cfg.ring_thickness_scale = std::clamp(cfg.ring_thickness_scale, 0.3f, 4.0f);
    cfg.ring_particle_scale = std::clamp(cfg.ring_particle_scale, 0.2f, 5.0f);
    cfg.ring_mass_scale = std::clamp(cfg.ring_mass_scale, 0.1f, 5.0f);
    cfg.adaptive_step_safety = std::clamp(cfg.adaptive_step_safety, 0.01f, 1.0f);
    cfg.adaptive_step_min = std::clamp(cfg.adaptive_step_min, 1.0e-6f, 1.0e6f);
    cfg.adaptive_step_max = std::clamp(cfg.adaptive_step_max,
                                       std::max(cfg.adaptive_step_min, 1.0e-6f), 1.0e8f);
    cfg.barnes_hut_theta = std::clamp(cfg.barnes_hut_theta, 0.2f, 1.6f);
    cfg.barnes_hut_min_bodies = std::clamp(cfg.barnes_hut_min_bodies, 16, 20000);
    if (!cfg.collision_sph && !cfg.collision_rigid_body_dynamics)
        cfg.collision_rigid_body_dynamics = true;
    if (version >= 11) {
        f.read(reinterpret_cast<char*>(&cfg.nebula_gravity_advection_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_gravity_collapse_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_gravity_compress_scale), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_sink_threshold), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_sink_min_mass), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_sink_spawn_fraction), sizeof(float));
        f.read(reinterpret_cast<char*>(&cfg.nebula_sink_consume_fraction), sizeof(float));
        uint8_t sink_flag = 1u;
        f.read(reinterpret_cast<char*>(&sink_flag), sizeof(uint8_t));
        cfg.nebula_sink_formation = (sink_flag != 0);
    } else {
        cfg.nebula_gravity_advection_scale = 0.020f;
        cfg.nebula_gravity_collapse_scale = 0.045f;
        cfg.nebula_gravity_compress_scale = 0.220f;
        cfg.nebula_sink_formation = true;
        cfg.nebula_sink_threshold = 1.05f;
        cfg.nebula_sink_min_mass = 2.0e-4f;
        cfg.nebula_sink_spawn_fraction = 0.018f;
        cfg.nebula_sink_consume_fraction = 0.95f;
    }
    cfg.nebula_gravity_advection_scale = std::clamp(cfg.nebula_gravity_advection_scale, 0.0f, 0.25f);
    cfg.nebula_gravity_collapse_scale = std::clamp(cfg.nebula_gravity_collapse_scale, 0.0f, 0.30f);
    cfg.nebula_gravity_compress_scale = std::clamp(cfg.nebula_gravity_compress_scale, 0.0f, 1.50f);
    cfg.nebula_sink_threshold = std::clamp(cfg.nebula_sink_threshold, 0.05f, 8.0f);
    cfg.nebula_sink_min_mass = std::clamp(cfg.nebula_sink_min_mass, 1.0e-7f, 1.0f);
    cfg.nebula_sink_spawn_fraction = std::clamp(cfg.nebula_sink_spawn_fraction, 0.001f, 0.50f);
    cfg.nebula_sink_consume_fraction = std::clamp(cfg.nebula_sink_consume_fraction, 0.05f, 1.0f);
    if (version <= 4 && cfg.min_fragment_mass >= 0.05f)
        cfg.min_fragment_mass = 1.0e-8f;

    uint32_t body_count = 0;
    f.read(reinterpret_cast<char*>(&body_count), 4);
    if (body_count > 10000) return false;

    state.clear();
    state.bodies.reserve(body_count);

    for (uint32_t i = 0; i < body_count; i++) {
        CelestialBody b;
        uint32_t name_len = 0;
        if (version >= 6) {
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
            b.non_attracting = pod.non_attracting != 0;
            name_len = pod.name_len;
        } else if (version >= 4) {
            BodyPODV5 pod{};
            f.read(reinterpret_cast<char*>(&pod), sizeof(BodyPODV5));
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
            b.non_attracting = false;
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
    f << "non_attracting " << (b.non_attracting ? 1 : 0) << "\n";

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
        else if (key == "non_attracting") {
            int v = 0;
            f >> v;
            b.non_attracting = (v != 0);
        }
    }

    if (b.name == "Unnamed") b.name.clear();
    refresh_body_render_state(b, &state);
    state.bodies.push_back(std::move(b));
    state.trails.emplace_back();
    return true;
}

void CosmosApp::save_persistent_settings() const {
    static_assert(std::is_trivially_copyable<CosmosConfig>::value,
                  "CosmosConfig must stay trivially copyable for persistence.");

    std::ofstream f(COSMOS_SETTINGS_PATH, std::ios::binary | std::ios::trunc);
    if (!f) return;

    uint32_t magic = COSMOS_SETTINGS_MAGIC;
    uint32_t version = COSMOS_SETTINGS_VERSION;
    uint32_t cfg_size = (uint32_t)sizeof(CosmosConfig);
    f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    f.write(reinterpret_cast<const char*>(&version), sizeof(version));
    f.write(reinterpret_cast<const char*>(&cfg_size), sizeof(cfg_size));
    f.write(reinterpret_cast<const char*>(&cfg), sizeof(CosmosConfig));

    PersistedUiSettingsV3 ui{};
    ui.spawn_type = spawn_type;
    ui.spawn_mass = spawn_mass;
    ui.spawn_in_orbit = spawn_in_orbit_ ? 1u : 0u;
    ui.spawn_menu_visible = spawn_menu_visible_ ? 1u : 0u;
    ui.settings_visible = settings_visible_ ? 1u : 0u;
    ui.body_list_visible = body_list_visible_ ? 1u : 0u;
    ui.bottom_bar_autohide = bottom_bar_autohide_ ? 1u : 0u;
    ui.override_temperature = spawn_draft_.override_temperature ? 1u : 0u;
    ui.temperature = spawn_draft_.temperature;
    ui.override_radius = spawn_draft_.override_radius ? 1u : 0u;
    ui.radius = spawn_draft_.radius;
    ui.override_rotation = spawn_draft_.override_rotation ? 1u : 0u;
    ui.rotation_hours = spawn_draft_.rotation_hours;
    ui.override_velocity = spawn_draft_.override_velocity ? 1u : 0u;
    ui.velocity_kms[0] = spawn_draft_.velocity_kms.x;
    ui.velocity_kms[1] = spawn_draft_.velocity_kms.y;
    ui.velocity_kms[2] = spawn_draft_.velocity_kms.z;
    ui.override_material = spawn_draft_.override_material ? 1u : 0u;
    ui.material_iron = spawn_draft_.material_iron;
    ui.material_silicate = spawn_draft_.material_silicate;
    ui.material_ice = spawn_draft_.material_ice;
    ui.material_hydrogen = spawn_draft_.material_hydrogen;
    ui.planet_look = spawn_draft_.planet_look;
    ui.spawn_rings = spawn_draft_.spawn_rings ? 1u : 0u;
    ui.spawn_moons = spawn_draft_.spawn_moons ? 1u : 0u;
    ui.moon_count = spawn_draft_.moon_count;
    ui.moon_orbit_layout = spawn_draft_.moon_orbit_layout;
    ui.moon_inclination_deg = spawn_draft_.moon_inclination_deg;
    ui.moon_spacing_scale = spawn_draft_.moon_spacing_scale;
    ui.override_ring_layout = spawn_draft_.override_ring_layout ? 1u : 0u;
    ui.ring_layout_type = spawn_draft_.ring_layout_type;
    ui.ring_inner_mult = spawn_draft_.ring_inner_mult;
    ui.ring_outer_mult = spawn_draft_.ring_outer_mult;
    ui.ring_density = spawn_draft_.ring_density;
    ui.ring_ice_fraction = spawn_draft_.ring_ice_fraction;
    ui.small_body_spawn_count = spawn_draft_.small_body_spawn_count;
    ui.small_body_layout = spawn_draft_.small_body_layout;
    f.write(reinterpret_cast<const char*>(&ui), sizeof(ui));
}

void CosmosApp::load_persistent_settings() {
    std::ifstream f(COSMOS_SETTINGS_PATH, std::ios::binary);
    if (!f) return;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t cfg_size = 0;
    bool rewrite_legacy_settings = false;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&cfg_size), sizeof(cfg_size));
    if (!f.good() || magic != COSMOS_SETTINGS_MAGIC || version > COSMOS_SETTINGS_VERSION)
        return;

    if (!(cfg_size > 0 && cfg_size < (1u << 20))) {
        return;
    }

    if (version >= 4) {
        CosmosConfig loaded_cfg = cfg;
        size_t copy_bytes = std::min<size_t>(cfg_size, sizeof(CosmosConfig));
        f.read(reinterpret_cast<char*>(&loaded_cfg), (std::streamsize)copy_bytes);
        if (!f.good()) return;
        if (cfg_size > copy_bytes)
            f.seekg((std::streamoff)(cfg_size - copy_bytes), std::ios::cur);
        cfg = loaded_cfg;
    } else {
        // Versions 1-3 wrote CosmosConfig as raw bytes while fields were being inserted
        // in the middle of the struct. Loading those bytes into the current layout corrupts
        // unrelated toggles, so keep defaults and only preserve the UI block.
        rewrite_legacy_settings = true;
        f.seekg((std::streamoff)cfg_size, std::ios::cur);
        if (!f.good()) return;
    }
    if (!f.good()) return;

    cfg.spin_fragmentation_threshold = std::clamp(cfg.spin_fragmentation_threshold, 0.50f, 2.0f);
    cfg.fragment_count = std::clamp(cfg.fragment_count, 1, 12);
    cfg.max_frag_generation = std::clamp(cfg.max_frag_generation, 0, 8);
    cfg.body_label_min_distance = std::clamp(cfg.body_label_min_distance, 0.0f, 1.0e8f);
    cfg.body_label_max_distance = std::clamp(cfg.body_label_max_distance,
                                             std::max(cfg.body_label_min_distance, 1.0e-3f), 1.0e8f);

    if (version == 1) {
        PersistedUiSettingsV1 ui{};
        f.read(reinterpret_cast<char*>(&ui), sizeof(ui));
        if (!f.good()) return;

        spawn_type = std::clamp(ui.spawn_type, 0, (int)CTYPE_COUNT - 1);
        spawn_mass = std::clamp(ui.spawn_mass, 1.0e-13f, 500.0f);
        spawn_in_orbit_ = ui.spawn_in_orbit != 0;
        spawn_menu_visible_ = ui.spawn_menu_visible != 0;
        settings_visible_ = ui.settings_visible != 0;
        body_list_visible_ = ui.body_list_visible != 0;
        bottom_bar_autohide_ = ui.bottom_bar_autohide != 0;
        spawn_draft_.override_temperature = ui.override_temperature != 0;
        spawn_draft_.temperature = std::clamp(ui.temperature, 2.7f, 120000.0f);
        spawn_draft_.override_radius = ui.override_radius != 0;
        spawn_draft_.radius = std::max(ui.radius, 0.04f);
        spawn_draft_.override_rotation = ui.override_rotation != 0;
        spawn_draft_.rotation_hours = std::clamp(ui.rotation_hours, 0.1f, 2000.0f);
        spawn_draft_.override_velocity = ui.override_velocity != 0;
        spawn_draft_.velocity_kms = glm::vec3(
            std::clamp(ui.velocity_kms[0], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[1], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[2], -200.0f, 200.0f));
        spawn_draft_.override_material = ui.override_material != 0;
        spawn_draft_.material_iron = std::clamp(ui.material_iron, 0.0f, 1.0f);
        spawn_draft_.material_silicate = std::clamp(ui.material_silicate, 0.0f, 1.0f);
        spawn_draft_.material_ice = std::clamp(ui.material_ice, 0.0f, 1.0f);
        spawn_draft_.material_hydrogen = std::clamp(ui.material_hydrogen, 0.0f, 1.0f);
        spawn_draft_.planet_look = std::clamp(ui.planet_look, 0, 5);
        spawn_draft_.spawn_rings = ui.spawn_rings != 0;
        spawn_draft_.spawn_moons = ui.spawn_moons != 0;
        spawn_draft_.moon_count = std::clamp(ui.moon_count, 1, 100);
        spawn_draft_.moon_orbit_layout = 0;
        spawn_draft_.moon_inclination_deg = 8.0f;
        spawn_draft_.moon_spacing_scale = 1.0f;
        spawn_draft_.override_ring_layout = ui.override_ring_layout != 0;
        spawn_draft_.ring_layout_type = 4;
        spawn_draft_.ring_inner_mult = std::clamp(ui.ring_inner_mult, 1.15f, 4.0f);
        spawn_draft_.ring_outer_mult = std::clamp(ui.ring_outer_mult, 1.5f, 8.0f);
        spawn_draft_.ring_density = std::clamp(ui.ring_density, 0.01f, 1.0f);
        spawn_draft_.ring_ice_fraction = std::clamp(ui.ring_ice_fraction, 0.0f, 1.0f);
        spawn_draft_.small_body_spawn_count = std::clamp(ui.small_body_spawn_count, 1, 1000);
        spawn_draft_.small_body_layout = std::clamp(ui.small_body_layout, 0, 3);
    } else if (version == 2) {
        PersistedUiSettingsV2 ui{};
        f.read(reinterpret_cast<char*>(&ui), sizeof(ui));
        if (!f.good()) return;

        spawn_type = std::clamp(ui.spawn_type, 0, (int)CTYPE_COUNT - 1);
        spawn_mass = std::clamp(ui.spawn_mass, 1.0e-13f, 500.0f);
        spawn_in_orbit_ = ui.spawn_in_orbit != 0;
        spawn_menu_visible_ = ui.spawn_menu_visible != 0;
        settings_visible_ = ui.settings_visible != 0;
        body_list_visible_ = ui.body_list_visible != 0;
        bottom_bar_autohide_ = ui.bottom_bar_autohide != 0;
        spawn_draft_.override_temperature = ui.override_temperature != 0;
        spawn_draft_.temperature = std::clamp(ui.temperature, 2.7f, 120000.0f);
        spawn_draft_.override_radius = ui.override_radius != 0;
        spawn_draft_.radius = std::max(ui.radius, 0.04f);
        spawn_draft_.override_rotation = ui.override_rotation != 0;
        spawn_draft_.rotation_hours = std::clamp(ui.rotation_hours, 0.1f, 2000.0f);
        spawn_draft_.override_velocity = ui.override_velocity != 0;
        spawn_draft_.velocity_kms = glm::vec3(
            std::clamp(ui.velocity_kms[0], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[1], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[2], -200.0f, 200.0f));
        spawn_draft_.override_material = ui.override_material != 0;
        spawn_draft_.material_iron = std::clamp(ui.material_iron, 0.0f, 1.0f);
        spawn_draft_.material_silicate = std::clamp(ui.material_silicate, 0.0f, 1.0f);
        spawn_draft_.material_ice = std::clamp(ui.material_ice, 0.0f, 1.0f);
        spawn_draft_.material_hydrogen = std::clamp(ui.material_hydrogen, 0.0f, 1.0f);
        spawn_draft_.planet_look = std::clamp(ui.planet_look, 0, 5);
        spawn_draft_.spawn_rings = ui.spawn_rings != 0;
        spawn_draft_.spawn_moons = ui.spawn_moons != 0;
        spawn_draft_.moon_count = std::clamp(ui.moon_count, 1, 100);
        spawn_draft_.moon_orbit_layout = std::clamp(ui.moon_orbit_layout, 0, 4);
        spawn_draft_.moon_inclination_deg = std::clamp(ui.moon_inclination_deg, 0.0f, 85.0f);
        spawn_draft_.moon_spacing_scale = std::clamp(ui.moon_spacing_scale, 0.35f, 4.0f);
        spawn_draft_.override_ring_layout = ui.override_ring_layout != 0;
        spawn_draft_.ring_layout_type = 4;
        spawn_draft_.ring_inner_mult = std::clamp(ui.ring_inner_mult, 1.15f, 4.0f);
        spawn_draft_.ring_outer_mult = std::clamp(ui.ring_outer_mult, 1.5f, 8.0f);
        spawn_draft_.ring_density = std::clamp(ui.ring_density, 0.01f, 1.0f);
        spawn_draft_.ring_ice_fraction = std::clamp(ui.ring_ice_fraction, 0.0f, 1.0f);
        spawn_draft_.small_body_spawn_count = std::clamp(ui.small_body_spawn_count, 1, 1000);
        spawn_draft_.small_body_layout = std::clamp(ui.small_body_layout, 0, 3);
    } else if (version >= 3) {
        PersistedUiSettingsV3 ui{};
        f.read(reinterpret_cast<char*>(&ui), sizeof(ui));
        if (!f.good()) return;

        spawn_type = std::clamp(ui.spawn_type, 0, (int)CTYPE_COUNT - 1);
        spawn_mass = std::clamp(ui.spawn_mass, 1.0e-13f, 500.0f);
        spawn_in_orbit_ = ui.spawn_in_orbit != 0;
        spawn_menu_visible_ = ui.spawn_menu_visible != 0;
        settings_visible_ = ui.settings_visible != 0;
        body_list_visible_ = ui.body_list_visible != 0;
        bottom_bar_autohide_ = ui.bottom_bar_autohide != 0;
        spawn_draft_.override_temperature = ui.override_temperature != 0;
        spawn_draft_.temperature = std::clamp(ui.temperature, 2.7f, 120000.0f);
        spawn_draft_.override_radius = ui.override_radius != 0;
        spawn_draft_.radius = std::max(ui.radius, 0.04f);
        spawn_draft_.override_rotation = ui.override_rotation != 0;
        spawn_draft_.rotation_hours = std::clamp(ui.rotation_hours, 0.1f, 2000.0f);
        spawn_draft_.override_velocity = ui.override_velocity != 0;
        spawn_draft_.velocity_kms = glm::vec3(
            std::clamp(ui.velocity_kms[0], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[1], -200.0f, 200.0f),
            std::clamp(ui.velocity_kms[2], -200.0f, 200.0f));
        spawn_draft_.override_material = ui.override_material != 0;
        spawn_draft_.material_iron = std::clamp(ui.material_iron, 0.0f, 1.0f);
        spawn_draft_.material_silicate = std::clamp(ui.material_silicate, 0.0f, 1.0f);
        spawn_draft_.material_ice = std::clamp(ui.material_ice, 0.0f, 1.0f);
        spawn_draft_.material_hydrogen = std::clamp(ui.material_hydrogen, 0.0f, 1.0f);
        spawn_draft_.planet_look = std::clamp(ui.planet_look, 0, 5);
        spawn_draft_.spawn_rings = ui.spawn_rings != 0;
        spawn_draft_.spawn_moons = ui.spawn_moons != 0;
        spawn_draft_.moon_count = std::clamp(ui.moon_count, 1, 100);
        spawn_draft_.moon_orbit_layout = std::clamp(ui.moon_orbit_layout, 0, 4);
        spawn_draft_.moon_inclination_deg = std::clamp(ui.moon_inclination_deg, 0.0f, 85.0f);
        spawn_draft_.moon_spacing_scale = std::clamp(ui.moon_spacing_scale, 0.35f, 4.0f);
        spawn_draft_.override_ring_layout = ui.override_ring_layout != 0;
        spawn_draft_.ring_layout_type = std::clamp(ui.ring_layout_type, 0, 6);
        spawn_draft_.ring_inner_mult = std::clamp(ui.ring_inner_mult, 1.15f, 4.0f);
        spawn_draft_.ring_outer_mult = std::clamp(ui.ring_outer_mult, 1.5f, 8.0f);
        spawn_draft_.ring_density = std::clamp(ui.ring_density, 0.01f, 1.0f);
        spawn_draft_.ring_ice_fraction = std::clamp(ui.ring_ice_fraction, 0.0f, 1.0f);
        spawn_draft_.small_body_spawn_count = std::clamp(ui.small_body_spawn_count, 1, 1000);
        spawn_draft_.small_body_layout = std::clamp(ui.small_body_layout, 0, 3);
    }

    if (rewrite_legacy_settings)
        save_persistent_settings();
}
