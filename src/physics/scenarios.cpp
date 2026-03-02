#include "physics/scenarios.h"
#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include <cstring>
#include <random>
#include <cmath>
#include <imgui.h>

// ── Scenario setup functions ─────────────────────────────────────────────────

static void setup_first_light(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 300.0f;
    sim.reset();
    std::mt19937 rng(42);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    for (int i = 0; i < 20; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 200) - 100, cy + (rng() % 200) - 100),
                          1, 0, rng, search);
    }
}

static void setup_hydrogen_factory(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 50.0f;
    sim.reset();
    std::mt19937 rng(123);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    for (int i = 0; i < 30; i++) {
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 300) - 150, cy + (rng() % 300) - 150),
                          1, 0, rng, search);
    }
}

static void setup_solar_core(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 3;  // Solar Core
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 30000;
    sim.cfg.temperature_kelvin = 15000000.0f;
    sim.reset();
}

static void setup_chain_reaction(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 6;  // Heavy Nucleus
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 20000;
    sim.cfg.temperature_kelvin = 1000.0f;
    sim.reset();
}

static void setup_antimatter_lab(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 4;  // Particle Soup
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 15000;
    sim.cfg.temperature_kelvin = 5000.0f;
    sim.reset();
}

static void setup_water_world(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 50000;
    sim.cfg.temperature_kelvin = 100.0f;
    sim.cfg.bonds_enabled = true;
    sim.reset();
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

static void setup_chemistry_set(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 80000;
    sim.cfg.temperature_kelvin = 200.0f;
    sim.cfg.bonds_enabled = true;
    sim.reset();
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

static void setup_dark_sector(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 11;  // Dark Sector
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 25000;
    sim.cfg.temperature_kelvin = 100.0f;
    sim.reset();
}

static void setup_particle_zoo(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 4;  // Particle Soup
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 30000;
    sim.cfg.temperature_kelvin = 100000.0f;
    sim.reset();
}

static void setup_nucleosynthesis(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 3;  // Solar Core
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 50000;
    sim.cfg.temperature_kelvin = 100000000.0f;
    sim.reset();
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

// ── Task check functions ─────────────────────────────────────────────────────

// First Light tasks
static bool check_particles_present(const PhysicsSimulation& sim) {
    return sim.iface.active_particle_display > 15;
}
static bool check_any_hydrogen(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 1) return true;
    return false;
}
static bool check_photon_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[PHOTON_TYPE_PHYS] > 0;
}

// Hydrogen Factory tasks
static bool check_1_hydrogen(const PhysicsSimulation& sim) {
    int h = 0;
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 1 && e.electrons >= 1) h++;
    return h >= 1;
}
static bool check_3_hydrogen(const PhysicsSimulation& sim) {
    int h = 0;
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 1 && e.electrons >= 1) h++;
    return h >= 3;
}
static bool check_5_hydrogen(const PhysicsSimulation& sim) {
    int h = 0;
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 1 && e.electrons >= 1) h++;
    return h >= 5;
}

// Solar Core tasks
static bool check_temp_1M(const PhysicsSimulation& sim) {
    return sim.iface.emergent_temp_display >= 1000000.0f;
}
static bool check_first_fusion(const PhysicsSimulation& sim) {
    return sim.achievements.total_fusions > 0;
}
static bool check_helium_exists(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 2) return true;
    return false;
}
static bool check_10_fusions(const PhysicsSimulation& sim) {
    return sim.achievements.total_fusions >= 10;
}

// Chain Reaction tasks
static bool check_first_fission(const PhysicsSimulation& sim) {
    return sim.achievements.total_fissions > 0;
}
static bool check_chain_reaction(const PhysicsSimulation& sim) {
    return sim.achievements.is_unlocked(ACH_CHAIN_REACTION);
}
static bool check_20_fissions(const PhysicsSimulation& sim) {
    return sim.achievements.total_fissions >= 20;
}

// Antimatter Lab tasks
static bool check_positron_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[POSITRON_TYPE_PHYS] > 0;
}
static bool check_antiproton_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[ANTIPROTON_TYPE_PHYS] > 0;
}
static bool check_first_annihilation(const PhysicsSimulation& sim) {
    return sim.achievements.total_annihilations > 0;
}
static bool check_10_annihilations(const PhysicsSimulation& sim) {
    return sim.achievements.total_annihilations >= 10;
}

// Water World tasks
static bool check_any_bond(const PhysicsSimulation& sim) {
    return sim.achievements.is_unlocked(ACH_FIRST_BOND)
        || sim.achievements.is_unlocked(ACH_FIRST_MOLECULE);
}
static bool check_h2_molecule(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_bestiary)
        if (m.formula == "H2") return true;
    return false;
}
static bool check_oxygen_molecule(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_bestiary) {
        if (m.formula.find('O') != std::string::npos) return true;
    }
    return false;
}
static bool check_h2o_molecule(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_bestiary) {
        if (m.formula == "H2O" || m.formula == "OH2") return true;
    }
    return false;
}

// Chemistry Set tasks
static bool check_any_molecule(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_list)
        if (m.atom_indices.size() > 1) return true;
    return false;
}
static bool check_3_molecules(const PhysicsSimulation& sim) {
    int distinct = 0;
    for (const auto& m : sim.iface.molecule_list)
        if (m.atom_indices.size() > 1) distinct++;
    return distinct >= 3;
}
static bool check_5_atom_molecule(const PhysicsSimulation& sim) {
    return sim.achievements.is_unlocked(ACH_MOLECULE_5_ATOMS);
}

// Dark Sector tasks
static bool check_5_dark_matter(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[DARK_MATTER_TYPE_PHYS] >= 5;
}
static bool check_10_dark_matter(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[DARK_MATTER_TYPE_PHYS] >= 10;
}
static bool check_dark_energy_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[DARK_ENERGY_TYPE_PHYS] > 0;
}

// Particle Zoo tasks
static bool check_5_types(const PhysicsSimulation& sim) {
    int types = 0;
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; i++)
        if (sim.iface.type_counts_display[i] > 0) types++;
    return types >= 5;
}
static bool check_neutrino_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[NEUTRINO_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[MU_NEUTRINO_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[TAU_NEUTRINO_TYPE_PHYS] > 0;
}
static bool check_10_types(const PhysicsSimulation& sim) {
    int types = 0;
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; i++)
        if (sim.iface.type_counts_display[i] > 0) types++;
    return types >= 10;
}
static bool check_exotic_particle(const PhysicsSimulation& sim) {
    // Any quark or gauge boson
    for (uint32_t q = UP_QUARK_TYPE; q <= ANTI_BOTTOM_TYPE; ++q)
        if (sim.iface.type_counts_display[q] > 0) return true;
    if (sim.iface.type_counts_display[W_PLUS_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[W_MINUS_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[Z_BOSON_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[HIGGS_TYPE_PHYS] > 0)
        return true;
    return false;
}

// Nucleosynthesis tasks
static bool check_carbon_exists(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 6) return true;
    return false;
}
static bool check_oxygen_element(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list)
        if (e.Z == 8) return true;
    return false;
}
static bool check_iron_or_heavier(const PhysicsSimulation& sim) {
    for (const auto& e : sim.iface.element_list)
        if (e.Z >= 26) return true;
    return false;
}

// ── Task arrays ──────────────────────────────────────────────────────────────

static const ScenarioTask TASKS_FIRST_LIGHT[] = {
    { "In the beginning, the universe was filled with hydrogen — the simplest atom.",
      "Observe the primordial hydrogen",
      "Watch the particles — protons and electrons fill the void",
      check_particles_present },
    { "As gravity pulls matter together, atoms begin to interact...",
      "Bring atoms close together",
      "Protons capture electrons to form hydrogen atoms",
      check_any_hydrogen },
    { "And then... light. The first photon bursts forth from atomic interaction.",
      "Create a photon",
      "Energetic collisions between atoms can emit photons",
      check_photon_exists },
};

static const ScenarioTask TASKS_HYDROGEN_FACTORY[] = {
    { "A single proton captures a wandering electron — the first complete atom.",
      "Build your first hydrogen atom",
      "Spawn protons near electrons — they'll bind!",
      check_1_hydrogen },
    { "One by one, atoms take shape in the quantum void.",
      "Build 3 hydrogen atoms",
      "Keep spawning protons and electrons nearby",
      check_3_hydrogen },
    { "A small cloud of hydrogen — the building block of everything to come.",
      "Build 5 hydrogen atoms",
      "Patience — electron capture takes time at low temperature",
      check_5_hydrogen },
};

static const ScenarioTask TASKS_SOLAR_CORE[] = {
    { "Deep within a collapsing cloud of gas, pressure and heat build to unimaginable levels.",
      "Raise temperature above 1 million K",
      "The thermostat coupling slider controls how quickly temperature rises",
      check_temp_1M },
    { "At fifteen million degrees, hydrogen nuclei overcome their mutual repulsion...",
      "Trigger your first fusion reaction",
      "High temperature + dense protons = fusion",
      check_first_fusion },
    { "Two hydrogen nuclei merge, releasing enormous energy — helium is born.",
      "Create Helium through fusion",
      "Fusion of protons produces helium nuclei",
      check_helium_exists },
    { "The star sustains itself. A self-fueling engine of nuclear fire.",
      "Trigger 10 fusion reactions",
      "Dense, hot environments produce continuous fusion",
      check_10_fusions },
};

static const ScenarioTask TASKS_CHAIN_REACTION[] = {
    { "Heavy nuclei are inherently unstable, waiting to split apart...",
      "Observe a fission event",
      "Heavy nuclei + fast neutrons = fission",
      check_first_fission },
    { "Each fission releases neutrons that trigger more fissions — a cascade begins.",
      "Trigger a chain reaction (3+ fissions in 1 second)",
      "Concentrate heavy nuclei together for cascading fission",
      check_chain_reaction },
    { "The chain reaction sustains itself. You've unlocked the atom's power.",
      "Produce 20 fission events total",
      "Keep the reaction going — each split releases more neutrons",
      check_20_fissions },
};

static const ScenarioTask TASKS_ANTIMATTER_LAB[] = {
    { "For every particle, there exists a mirror twin — identical but opposite in every charge.",
      "Create a positron",
      "High-energy collisions can produce positron-electron pairs",
      check_positron_exists },
    { "A proton's shadow emerges from the quantum vacuum.",
      "Create an antiproton",
      "Even higher energies are needed to produce antiprotons",
      check_antiproton_exists },
    { "When matter meets antimatter... pure energy.",
      "Witness annihilation",
      "Bring particles and antiparticles together",
      check_first_annihilation },
    { "E = mc-squared. The most efficient energy conversion in the universe.",
      "Trigger 10 annihilations",
      "More matter-antimatter pairs means more annihilation events",
      check_10_annihilations },
};

static const ScenarioTask TASKS_WATER_WORLD[] = {
    { "Atoms don't just collide — they share. Electrons bridge the gap between nuclei.",
      "Form your first covalent bond",
      "Bring atoms close together — bonds form within bonding radius",
      check_any_bond },
    { "Two hydrogen atoms linked by a shared electron cloud — the simplest molecule.",
      "Create a hydrogen molecule (H2)",
      "Two hydrogen atoms near each other will bond",
      check_h2_molecule },
    { "Oxygen, the great reactor — eager to bond with almost anything.",
      "Create any oxygen-containing molecule",
      "Bring oxygen atoms near hydrogen or other atoms",
      check_oxygen_molecule },
    { "Water. The molecule that made life possible. Two parts hydrogen, one part oxygen.",
      "Create H2O",
      "An oxygen atom needs two hydrogen atoms nearby",
      check_h2o_molecule },
};

static const ScenarioTask TASKS_CHEMISTRY_SET[] = {
    { "A workshop of atoms, each with its own personality and bonding preferences.",
      "Create any molecule",
      "Bring different atoms close together",
      check_any_molecule },
    { "Different combinations, different properties. Chemistry is combinatorics.",
      "Create 3 different molecules",
      "Mix different atom types for variety",
      check_3_molecules },
    { "Complex molecules emerge — the foundation of organic chemistry.",
      "Create a molecule with 5+ atoms",
      "Large molecules need many atoms in close proximity",
      check_5_atom_molecule },
};

static const ScenarioTask TASKS_DARK_SECTOR[] = {
    { "Most of the universe is invisible. Dark matter neither emits nor absorbs light.",
      "Observe 5 dark matter particles",
      "DM only interacts via gravity — look for the invisible clustering",
      check_5_dark_matter },
    { "Dark matter clusters under gravity alone — shaping galaxies from the shadows.",
      "Get 10+ dark matter particles",
      "The Dark Sector environment naturally produces dark matter",
      check_10_dark_matter },
    { "And darker still — dark energy, the force that tears the cosmos apart.",
      "Observe a dark energy particle",
      "Dark energy particles appear in high-energy dark sector environments",
      check_dark_energy_exists },
};

static const ScenarioTask TASKS_PARTICLE_ZOO[] = {
    { "The universe is built from a menagerie of fundamental particles.",
      "Discover 5 particle types",
      "High-energy environments produce diverse particles",
      check_5_types },
    { "The ghost particle — trillions pass through you every second, touching nothing.",
      "Create a neutrino",
      "Neutrinos are produced in weak interactions and beta decay",
      check_neutrino_exists },
    { "Quarks, leptons, bosons — the cast of characters grows.",
      "Discover 10 particle types",
      "Raise the energy to unlock more exotic species",
      check_10_types },
    { "At the highest energies, the rarest particles flicker into existence.",
      "Create an exotic particle (quark or boson)",
      "Quarks and bosons appear at extreme temperatures",
      check_exotic_particle },
};

static const ScenarioTask TASKS_NUCLEOSYNTHESIS[] = {
    { "Stars are forges. Hydrogen becomes helium in their cores.",
      "Create Helium (Z=2)",
      "Proton-proton fusion chain produces helium",
      check_helium_exists },
    { "Three helium nuclei collide simultaneously — the triple-alpha process creates carbon, the backbone of life.",
      "Create Carbon (Z=6)",
      "Higher temperatures enable heavier element fusion",
      check_carbon_exists },
    { "Carbon captures another helium — oxygen, the breath of worlds.",
      "Create Oxygen (Z=8)",
      "Carbon-helium fusion produces oxygen",
      check_oxygen_element },
    { "Iron. The end of the line for stellar fusion. Beyond this, only supernovae can forge heavier elements.",
      "Create Iron (Z=26) or heavier",
      "Extreme temperature fuses lighter elements into heavier ones",
      check_iron_or_heavier },
};

// ── Scenario definitions ────────────────────────────────────────────────────

static const Scenario SCENARIOS[] = {
    { "First Light",
      "Create your first photon from atomic interactions.",
      "Nuclear",
      "The Dawn of Stars",
      setup_first_light,
      TASKS_FIRST_LIGHT, 3 },

    { "Hydrogen Factory",
      "Build complete hydrogen atoms with electrons.",
      "Nuclear",
      "Building the Periodic Table",
      setup_hydrogen_factory,
      TASKS_HYDROGEN_FACTORY, 3 },

    { "Solar Core",
      "Trigger nuclear fusion in the heart of a star.",
      "Nuclear",
      "Heart of a Star",
      setup_solar_core,
      TASKS_SOLAR_CORE, 4 },

    { "Chain Reaction",
      "Cause a fission chain reaction.",
      "Nuclear",
      "Critical Mass",
      setup_chain_reaction,
      TASKS_CHAIN_REACTION, 3 },

    { "Antimatter Lab",
      "Create and observe particle-antiparticle annihilation.",
      "Nuclear",
      "Through the Looking Glass",
      setup_antimatter_lab,
      TASKS_ANTIMATTER_LAB, 4 },

    { "Water World",
      "Form H2O water molecules through covalent bonding.",
      "Chemistry",
      "Recipe for Life",
      setup_water_world,
      TASKS_WATER_WORLD, 4 },

    { "Chemistry Set",
      "Create different molecule types from mixed atoms.",
      "Chemistry",
      "The Molecular Workshop",
      setup_chemistry_set,
      TASKS_CHEMISTRY_SET, 3 },

    { "Dark Sector",
      "Observe dark matter and dark energy.",
      "Cosmology",
      "The Invisible Universe",
      setup_dark_sector,
      TASKS_DARK_SECTOR, 3 },

    { "Particle Zoo",
      "Discover the menagerie of fundamental particles.",
      "Cosmology",
      "Catalogue of Creation",
      setup_particle_zoo,
      TASKS_PARTICLE_ZOO, 4 },

    { "Stellar Nucleosynthesis",
      "Build elements up to Iron through fusion chains.",
      "Cosmology",
      "Forging the Elements",
      setup_nucleosynthesis,
      TASKS_NUCLEOSYNTHESIS, 4 },

    { "Free Play: Lab",
      "Empty lab. Create whatever you want!",
      "Sandbox",
      nullptr,
      setup_free_lab,
      nullptr, 0 },

    { "Free Play: Space",
      "Cosmic void with primordial particles.",
      "Sandbox",
      nullptr,
      setup_free_space,
      nullptr, 0 },
};

static constexpr int SCENARIO_COUNT = sizeof(SCENARIOS) / sizeof(SCENARIOS[0]);

// ── ScenarioManager implementation ──────────────────────────────────────────

void ScenarioManager::start_scenario(int idx, PhysicsSimulation& sim) {
    if (idx < 0 || idx >= SCENARIO_COUNT) return;
    scenario_idx = idx;
    goal_complete = false;
    current_task = 0;
    task_just_completed = false;
    task_complete_timer = 0.0f;
    narrative_timer = 0.0f;
    active = true;
    SCENARIOS[idx].setup(sim);
}

void ScenarioManager::check_goal(const PhysicsSimulation& sim) {
    if (!active || scenario_idx < 0 || goal_complete) return;

    const Scenario& s = SCENARIOS[scenario_idx];
    if (s.task_count <= 0 || !s.tasks) return;  // sandbox

    float dt = ImGui::GetIO().DeltaTime;
    narrative_timer += dt;

    // If a task was just completed, auto-advance after 3 seconds
    if (task_just_completed) {
        task_complete_timer += dt;
        if (task_complete_timer >= 3.0f) {
            task_just_completed = false;
            task_complete_timer = 0.0f;
            current_task++;
            narrative_timer = 0.0f;  // reset for next task's narrative
            if (current_task >= s.task_count) {
                goal_complete = true;
                return;
            }
        }
        return;  // don't check next task during transition
    }

    // Check current task
    if (current_task < s.task_count && s.tasks[current_task].check) {
        if (s.tasks[current_task].check(sim)) {
            task_just_completed = true;
            task_complete_timer = 0.0f;
            // If this was the last task, mark complete immediately
            if (current_task >= s.task_count - 1) {
                goal_complete = true;
            }
        }
    }
}

void ScenarioManager::end() {
    active = false;
    scenario_idx = -1;
    goal_complete = false;
    current_task = 0;
    task_just_completed = false;
    task_complete_timer = 0.0f;
    narrative_timer = 0.0f;
}

const Scenario& ScenarioManager::current() const {
    static const Scenario empty = { "", "", "", nullptr, nullptr, nullptr, 0 };
    if (scenario_idx < 0 || scenario_idx >= SCENARIO_COUNT) return empty;
    return SCENARIOS[scenario_idx];
}

const ScenarioTask* ScenarioManager::current_task_ptr() const {
    if (scenario_idx < 0 || scenario_idx >= SCENARIO_COUNT) return nullptr;
    const Scenario& s = SCENARIOS[scenario_idx];
    if (!s.tasks || current_task >= s.task_count) return nullptr;
    return &s.tasks[current_task];
}

int ScenarioManager::total_tasks() const {
    if (scenario_idx < 0 || scenario_idx >= SCENARIO_COUNT) return 0;
    return SCENARIOS[scenario_idx].task_count;
}

int ScenarioManager::scenario_count() {
    return SCENARIO_COUNT;
}

const Scenario& ScenarioManager::get(int idx) {
    return SCENARIOS[idx];
}
