#include "physics/scenarios.h"
#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include <cstring>
#include <random>
#include <cmath>

// ── Scenario setup functions ─────────────────────────────────────────────────

static void setup_first_light(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 300.0f;
    sim.reset();
    // Spawn some protons and electrons
    std::mt19937 rng(42);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    for (int i = 0; i < 20; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 200) - 100, cy + (rng() % 200) - 100),
                          1, 0, rng, search);
    }
}

static bool check_first_light(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[PHOTON_TYPE_PHYS] > 0;
}

static void setup_hydrogen_factory(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 50.0f;
    sim.reset();
    // Spawn protons and electrons nearby
    std::mt19937 rng(123);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    for (int i = 0; i < 30; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 300) - 150, cy + (rng() % 300) - 150),
                          1, 0, rng, search);
    }
}

static bool check_hydrogen_factory(const PhysicsSimulation& sim) {
    int h_count = 0;
    for (const auto& e : sim.iface.element_list) {
        if (e.Z == 1 && e.electrons >= 1) h_count++;
    }
    return h_count >= 5;
}

static void setup_solar_core(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 3;  // Solar Core
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 30000;
    sim.cfg.temperature_kelvin = 15000000.0f;
    sim.reset();
}

static bool check_solar_core(const PhysicsSimulation& sim) {
    return sim.achievements.total_fusions > 0;
}

static void setup_chain_reaction(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 6;  // Heavy Nucleus
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 20000;
    sim.cfg.temperature_kelvin = 1000.0f;
    sim.reset();
}

static bool check_chain_reaction(const PhysicsSimulation& sim) {
    return sim.achievements.is_unlocked(ACH_CHAIN_REACTION);
}

static void setup_antimatter_lab(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 4;  // Particle Soup
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 15000;
    sim.cfg.temperature_kelvin = 5000.0f;
    sim.reset();
}

static bool check_antimatter_lab(const PhysicsSimulation& sim) {
    return sim.achievements.total_annihilations > 0;
}

static void setup_water_world(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 100.0f;
    sim.cfg.bonds_enabled = true;
    sim.reset();
    // Spawn hydrogen and oxygen atoms
    std::mt19937 rng(456);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    for (int i = 0; i < 20; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 400) - 200, cy + (rng() % 400) - 200),
                          1, 0, rng, search);
    }
    for (int i = 0; i < 8; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 400) - 200, cy + (rng() % 400) - 200),
                          8, 8, rng, search);
    }
}

static bool check_water_world(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.molecule_bestiary) {
        if (e.formula == "H2O" || e.formula == "OH2") return true;
    }
    return false;
}

static void setup_chemistry_set(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 80000;
    sim.cfg.temperature_kelvin = 200.0f;
    sim.cfg.bonds_enabled = true;
    sim.reset();
    // Spawn mixed atoms
    std::mt19937 rng(789);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    int z_list[] = {1, 1, 1, 1, 6, 6, 7, 8, 8, 11, 17};
    int n_list[] = {0, 0, 0, 0, 6, 6, 7, 8, 8, 12, 18};
    for (int i = 0; i < 40; i++) {
        int pick = rng() % 11;
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 600) - 300, cy + (rng() % 600) - 300),
                          z_list[pick], n_list[pick], rng, search);
    }
}

static bool check_chemistry_set(const PhysicsSimulation& sim) {
    int distinct_molecules = 0;
    for (const auto& m : sim.iface.molecule_list) {
        if (m.atom_indices.size() > 1) distinct_molecules++;
    }
    return distinct_molecules >= 3;
}

static void setup_dark_sector(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 11;  // Dark Sector
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 25000;
    sim.cfg.temperature_kelvin = 100.0f;
    sim.reset();
}

static bool check_dark_sector(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[DARK_MATTER_TYPE_PHYS] >= 10;
}

static void setup_particle_zoo(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 4;  // Particle Soup
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 30000;
    sim.cfg.temperature_kelvin = 100000.0f;
    sim.reset();
}

static bool check_particle_zoo(const PhysicsSimulation& sim) {
    int types_seen = 0;
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; i++) {
        if (sim.iface.type_counts_display[i] > 0) types_seen++;
    }
    return types_seen >= 10;
}

static void setup_nucleosynthesis(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 3;  // Solar Core
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 50000;
    sim.cfg.temperature_kelvin = 100000000.0f;
    sim.reset();
}

static bool check_nucleosynthesis(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list) {
        if (e.Z >= 26) return true;  // Iron or heavier
    }
    return false;
}

static void setup_free_lab(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 100000;
    sim.cfg.temperature_kelvin = 300.0f;
    sim.reset();
}

static void setup_free_space(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 5;  // Space
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 20000;
    sim.cfg.temperature_kelvin = 2.7f;
    sim.reset();
}

// ── Scenario definitions ────────────────────────────────────────────────────

static const Scenario SCENARIOS[] = {
    { "First Light", "Create your first photon from atomic interactions.",
      "Nuclear", setup_first_light, check_first_light,
      "Create a photon", "Spawn protons and electrons close together" },

    { "Hydrogen Factory", "Build 5 complete hydrogen atoms with electrons.",
      "Nuclear", setup_hydrogen_factory, check_hydrogen_factory,
      "Build 5 hydrogen atoms", "Spawn protons and electrons nearby — they'll bind!" },

    { "Solar Core", "Trigger your first nuclear fusion reaction.",
      "Nuclear", setup_solar_core, check_solar_core,
      "Trigger hydrogen fusion", "High temperature + dense protons = fusion" },

    { "Chain Reaction", "Cause a fission chain reaction (3+ fissions in 60 frames).",
      "Nuclear", setup_chain_reaction, check_chain_reaction,
      "Trigger a chain reaction", "Heavy nuclei + fast neutrons = fission cascade" },

    { "Antimatter Lab", "Create and observe particle-antiparticle annihilation.",
      "Nuclear", setup_antimatter_lab, check_antimatter_lab,
      "Witness annihilation", "Look for matter-antimatter collisions" },

    { "Water World", "Form H2O water molecules through covalent bonding.",
      "Chemistry", setup_water_world, check_water_world,
      "Create an H2O molecule", "Bring hydrogen atoms near oxygen — bonds form at close range" },

    { "Chemistry Set", "Create 3 different molecule types from mixed atoms.",
      "Chemistry", setup_chemistry_set, check_chemistry_set,
      "Create 3 distinct molecules", "Different atom combinations create different molecules" },

    { "Dark Sector", "Observe dark matter particles clustering under gravity.",
      "Cosmology", setup_dark_sector, check_dark_sector,
      "Get 10+ dark matter particles", "DM only interacts via gravity — watch it cluster" },

    { "Particle Zoo", "Discover 10 different particle types simultaneously.",
      "Cosmology", setup_particle_zoo, check_particle_zoo,
      "Have 10 particle types present", "High energy produces exotic particles" },

    { "Stellar Nucleosynthesis", "Build elements up to Iron through fusion chains.",
      "Cosmology", setup_nucleosynthesis, check_nucleosynthesis,
      "Create Iron (Z=26) or heavier", "Extreme temperature fuses lighter elements into heavier ones" },

    { "Free Play: Lab", "Empty lab. Create whatever you want!",
      "Sandbox", setup_free_lab, nullptr,
      nullptr, nullptr },

    { "Free Play: Space", "Cosmic void with primordial particles.",
      "Sandbox", setup_free_space, nullptr,
      nullptr, nullptr },
};

static constexpr int SCENARIO_COUNT = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

// ── ScenarioManager implementation ──────────────────────────────────────────

void ScenarioManager::start_scenario(int idx, PhysicsSimulation& sim) {
    if (idx < 0 || idx >= SCENARIO_COUNT) return;
    scenario_idx = idx;
    goal_complete = false;
    active = true;
    SCENARIOS[idx].setup(sim);
}

void ScenarioManager::check_goal(const PhysicsSimulation& sim) {
    if (!active || scenario_idx < 0 || goal_complete) return;
    auto check = SCENARIOS[scenario_idx].check_goal;
    if (check && check(sim)) {
        goal_complete = true;
    }
}

void ScenarioManager::end() {
    active = false;
    scenario_idx = -1;
    goal_complete = false;
}

const Scenario& ScenarioManager::current() const {
    static const Scenario empty = { "", "", "", nullptr, nullptr, nullptr, nullptr };
    if (scenario_idx < 0 || scenario_idx >= SCENARIO_COUNT) return empty;
    return SCENARIOS[scenario_idx];
}

int ScenarioManager::scenario_count() {
    return SCENARIO_COUNT;
}

const Scenario& ScenarioManager::get(int idx) {
    return SCENARIOS[idx];
}
