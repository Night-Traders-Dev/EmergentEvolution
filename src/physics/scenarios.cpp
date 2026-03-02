#include "physics/scenarios.h"
#include "physics/simulation.h"
#include "physics/phys_particles.h"
#include <cstring>
#include <random>
#include <cmath>
#include <imgui.h>

// ── Spawn rules helpers ──────────────────────────────────────────────────────

static ScenarioSpawnRules make_rules(const int* types, int type_count,
                                      int max_z, bool mol, bool fo, bool had) {
    ScenarioSpawnRules r;
    r.allowed_base[0] = 0; r.allowed_base[1] = 0;
    for (int i = 0; i < type_count; i++) {
        uint32_t t = static_cast<uint32_t>(types[i]);
        if (t < 64)  r.allowed_base[0] |= (1ULL << t);
        else if (t < 128) r.allowed_base[1] |= (1ULL << (t - 64));
    }
    r.max_element_Z = max_z;
    r.allow_molecules = mol;
    r.allow_force_objects = fo;
    r.allow_hadrons = had;
    return r;
}

static ScenarioSpawnRules rules_sm_all(int max_z, bool mol, bool fo, bool had) {
    ScenarioSpawnRules r;
    r.allowed_base[0] = 0; r.allowed_base[1] = 0;
    for (int t = 0; t <= 32; t++)
        r.allowed_base[0] |= (1ULL << t);
    r.max_element_Z = max_z;
    r.allow_molecules = mol;
    r.allow_force_objects = fo;
    r.allow_hadrons = had;
    return r;
}

// Unrestricted (sandbox)
static constexpr ScenarioSpawnRules RULES_UNRESTRICTED = {};

// ── Per-scenario spawn type lists ────────────────────────────────────────────

static const int TYPES_FIRST_LIGHT[] = {0, 1, 2, 3, 4};
static const int TYPES_H_FACTORY[]   = {0, 1, 2, 3};
static const int TYPES_SOLAR[]       = {0, 1, 2, 3, 4, 6};
static const int TYPES_CHAIN[]       = {0, 1, 2, 3};
static const int TYPES_ANTIMATTER[]  = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11};
static const int TYPES_WATER[]       = {0, 1, 2, 3};
static const int TYPES_CHEM[]        = {0, 1, 2, 3};
static const int TYPES_DARK[]        = {0, 1, 2, 3, 4, 30, 31, 32};
static const int TYPES_NUCLEO[]      = {0, 1, 2, 3, 4, 6};
static const int TYPES_QGP[]         = {3, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28};
static const int TYPES_NEUTRON[]     = {0, 1, 2, 3, 6};
static const int TYPES_SUSY[]        = {0, 1, 2, 3, 4, 6, 7, 8, 25, 26, 27, 28, 29,
                                         39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};

#define ARRLEN(a) (static_cast<int>(sizeof(a) / sizeof(a[0])))

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

// ── Cosmic Evolution scenario setups ─────────────────────────────────────────

static void setup_quark_soup(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 7;  // Quark-Gluon Plasma
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 40000;
    sim.cfg.temperature_kelvin = 1.0e10f;
    sim.cfg.carrier_mode_enabled = true;
    sim.cfg.quasi_mode_enabled = true;
    sim.reset();
}

static void setup_cosmic_dawn(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 13;  // Big Bang
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 50000;
    sim.cfg.temperature_kelvin = 1.0e9f;
    sim.reset();
}

static void setup_neutron_star_scenario(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 2;  // Neutron Star Surface
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 30000;
    sim.cfg.temperature_kelvin = 100.0f;
    sim.cfg.quasi_mode_enabled = true;
    sim.reset();
}

static void setup_magnetar(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 2;  // Neutron Star Surface
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 25000;
    sim.cfg.temperature_kelvin = 500.0f;
    sim.cfg.lorentz_strength = 8.0f;
    sim.cfg.magnetic_coupling = 1.0f;
    sim.cfg.magnetic_feedback_enabled = true;
    sim.cfg.quasi_mode_enabled = true;
    sim.reset();
}

static void setup_susy_world(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 12;  // SUSY Sector
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 20000;
    sim.cfg.temperature_kelvin = 1.0e11f;
    sim.reset();
}

static void setup_the_collider(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 10;  // Particle Accelerator
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 15000;
    sim.cfg.temperature_kelvin = 5000.0f;
    sim.cfg.carrier_mode_enabled = true;
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

// ── Cosmic Evolution check functions ─────────────────────────────────────────

// Quark Soup checks
static bool check_5_quarks(const PhysicsSimulation& sim) {
    int quarks = 0;
    for (uint32_t q = UP_QUARK_TYPE; q <= ANTI_BOTTOM_TYPE; ++q)
        quarks += sim.iface.type_counts_display[q];
    return quarks >= 5;
}
static bool check_gluon_visible(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[GLUON_TYPE_PHYS] > 0;
}
static bool check_plasmon_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[PLASMON_TYPE_PHYS] > 0;
}

// Cosmic Dawn checks
static bool check_temp_1G(const PhysicsSimulation& sim) {
    return sim.iface.emergent_temp_display >= 1.0e9f;
}
static bool check_pair_produced(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[POSITRON_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[NEUTRINO_TYPE_PHYS] > 0;
}
static bool check_5_photons(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[PHOTON_TYPE_PHYS] >= 5;
}

// Neutron Star checks
static bool check_50_neutrons(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[NEUTRON_TYPE] >= 50;
}
static bool check_phonon_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[PHONON_TYPE_PHYS] > 0;
}
static bool check_cooper_pair_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[COOPER_PAIR_TYPE_PHYS] > 0;
}
static bool check_roton_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[ROTON_TYPE_PHYS] > 0;
}

// Magnetar checks
static bool check_strong_bfield(const PhysicsSimulation& sim) {
    return sim.iface.emergent_bfield_display > 0.3f;
}
static bool check_magnon_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[MAGNON_TYPE_PHYS] > 0;
}
static bool check_3_qp_types(const PhysicsSimulation& sim) {
    int qp_types = 0;
    for (uint32_t t = ELECTRON_HOLE_TYPE_PHYS; t <= ROTON_TYPE_PHYS; ++t)
        if (sim.iface.type_counts_display[t] > 0) qp_types++;
    return qp_types >= 3;
}

// SUSY World checks
static bool check_3_susy_types(const PhysicsSimulation& sim) {
    int susy_count = 0;
    uint32_t susy_types[] = { SELECTRON_TYPE_PHYS, SMUON_TYPE_PHYS,
        SQUARK_TYPE_PHYS, GLUINO_TYPE_PHYS, NEUTRALINO_TYPE_PHYS, GRAVITINO_TYPE_PHYS };
    for (auto t : susy_types)
        if (sim.iface.type_counts_display[t] > 0) susy_count++;
    return susy_count >= 3;
}
static bool check_neutralino_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[NEUTRALINO_TYPE_PHYS] > 0;
}
static bool check_polaron_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[POLARON_TYPE_PHYS] > 0;
}

// Collider checks
static bool check_accelerator_fired(const PhysicsSimulation& sim) {
    return sim.achievements.total_accelerator_fires > 0;
}
static bool check_muon_exists(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[MUON_TYPE_PHYS] > 0;
}
static bool check_15_types(const PhysicsSimulation& sim) {
    int types = 0;
    for (uint32_t i = 0; i < PHYS_PARTICLE_TYPES; i++)
        if (sim.iface.type_counts_display[i] > 0) types++;
    return types >= 15;
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

// ── Cosmic Evolution task arrays ─────────────────────────────────────────────

static const ScenarioTask TASKS_QUARK_SOUP[] = {
    { "In the first microsecond, the universe was a seething plasma of quarks and gluons — "
      "matter in its most primal form, before protons or neutrons could exist.",
      "Observe 5+ free quarks",
      "The QGP environment naturally deconfines quarks from hadrons",
      check_5_quarks },
    { "Between quarks, gluons fly ceaselessly — the strong force carriers that bind the "
      "universe together. Here they are free, unconfined.",
      "Observe gluon exchange",
      "With Carrier Mode on, you can see gluons flying between quarks",
      check_gluon_visible },
    { "The dense electron sea begins to oscillate collectively — "
      "a plasmon, the first quasiparticle, emerges from the chaos.",
      "Create a plasmon",
      "Plasmons emerge when electrons cluster densely in hot plasma",
      check_plasmon_exists },
};

static const ScenarioTask TASKS_COSMIC_DAWN[] = {
    { "Moments after the Big Bang, the universe is an inferno of pure energy. "
      "Temperature defines everything — what can exist, what must annihilate.",
      "Reach 1 billion K",
      "The Big Bang environment starts extremely hot",
      check_temp_1G },
    { "From the seething vacuum, particle-antiparticle pairs burst into existence. "
      "Most annihilate instantly, but some survive.",
      "Observe pair production",
      "High-energy collisions create positrons and neutrinos",
      check_pair_produced },
    { "As the universe cools, protons capture electrons for the first time. "
      "Hydrogen — the simplest atom, and the seed of all structure.",
      "Create hydrogen",
      "Cooling allows protons to capture electrons — recombination",
      check_any_hydrogen },
    { "With neutral atoms formed, photons finally stream free. "
      "The universe becomes transparent — this is the cosmic dawn.",
      "Create 5 photons",
      "Photons are released as electrons settle into orbitals",
      check_5_photons },
};

static const ScenarioTask TASKS_NEUTRON_STAR[] = {
    { "A massive star has died. Its core, compressed beyond imagination, "
      "is now an ocean of neutrons packed tighter than atomic nuclei.",
      "Observe 50+ neutrons",
      "The neutron star environment is dominated by dense neutron matter",
      check_50_neutrons },
    { "In this crystalline sea, vibrations propagate as phonons — "
      "lattice vibrations that carry energy through the nuclear solid.",
      "Create a phonon",
      "Phonons emerge when nucleons cluster densely enough to form a lattice",
      check_phonon_exists },
    { "At low temperature, neutrons pair up through quantum attraction. "
      "Cooper pairs — the basis of neutron star superfluidity.",
      "Form a Cooper pair",
      "Cold, slow neutrons near each other can pair into a superfluid condensate",
      check_cooper_pair_exists },
    { "The superfluid stirs. A vortex excitation — a roton — "
      "ripples through the condensate like a quantum whirlpool.",
      "Observe a roton",
      "Energetic Cooper pairs can shed roton vortex excitations",
      check_roton_exists },
};

static const ScenarioTask TASKS_MAGNETAR[] = {
    { "Some neutron stars are born with magnetic fields a quadrillion times stronger "
      "than Earth's. This is a magnetar — the most magnetic object in the universe.",
      "Build strong magnetic field",
      "Fast-moving charges generate emergent B-field — watch it grow",
      check_strong_bfield },
    { "In the intense field, spin waves propagate through the magnetized matter. "
      "Each magnon carries a quantum of magnetic excitation.",
      "Create a magnon",
      "Magnons form when fast charged particles move through strong B-fields",
      check_magnon_exists },
    { "The extreme environment breeds diversity — multiple quasiparticle species "
      "coexist in this magnetic crucible.",
      "Observe 3 different quasiparticle types",
      "Phonons, magnons, Cooper pairs, rotons — they all arise in neutron stars",
      check_3_qp_types },
};

static const ScenarioTask TASKS_SUSY_WORLD[] = {
    { "Beyond the Standard Model lies a mirror world — supersymmetry. "
      "Every fermion has a boson partner, every boson a fermion twin.",
      "Discover 3 SUSY particles",
      "The SUSY Sector environment produces selectrons, squarks, gluinos, and more",
      check_3_susy_types },
    { "The lightest supersymmetric particle — the neutralino — is stable. "
      "It may be dark matter itself, invisible yet gravitating.",
      "Observe a neutralino",
      "Heavier SUSY particles decay to the stable neutralino (LSP)",
      check_neutralino_exists },
    { "In this dense plasma, electrons become dressed in phonon clouds — "
      "polarons, where quantum many-body physics meets particle physics.",
      "Create a polaron",
      "Polarons form when electrons are surrounded by ions in dense environments",
      check_polaron_exists },
};

static const ScenarioTask TASKS_THE_COLLIDER[] = {
    { "The particle accelerator is humanity's most powerful microscope — "
      "smashing particles together to reveal nature's deepest secrets.",
      "Fire the accelerator",
      "Click the accelerator tool or use the keyboard shortcut to fire",
      check_accelerator_fired },
    { "From the collision debris, a muon emerges — the electron's heavier cousin, "
      "unstable and fleeting, a messenger from the second generation.",
      "Create a muon",
      "High-energy collisions produce muons — heavier leptons from the second family",
      check_muon_exists },
    { "Energy converts to mass and back again. Positron-electron pairs "
      "materialize from pure collision energy — E = mc².",
      "Observe pair production",
      "Energetic collisions near nuclei can produce electron-positron pairs",
      check_positron_exists },
    { "The particle zoo grows. Each collision reveals new species — "
      "quarks, bosons, leptons, all catalogued in the Standard Model.",
      "Discover 15 particle types",
      "Keep firing the accelerator — higher energies unlock rarer particles",
      check_15_types },
};

// ── Chirality scenario setups ─────────────────────────────────────────────

static void setup_chirality_lab(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 0;
    sim.cfg.start_empty = true;
    sim.cfg.pool_size = 80000;
    sim.cfg.temperature_kelvin = 150.0f;
    sim.cfg.bonds_enabled = true;
    sim.reset();
    std::mt19937 rng(314159);
    uint32_t search = 0;
    float cx = WORLD_W * 0.5f, cy = WORLD_H * 0.5f;
    // Mix of C, H, N, O, F, Cl — maximizes chirality opportunities
    int z_list[] = {6, 6, 6, 6, 1, 1, 1, 1, 1, 1, 7, 8, 8, 9, 17};
    int n_list[] = {6, 6, 6, 6, 0, 0, 0, 0, 0, 0, 7, 8, 8, 10, 18};
    for (int i = 0; i < 50; i++) {
        int pick = rng() % 15;
        sim.spawn_atom_at(glm::vec2(cx + (rng() % 700) - 350, cy + (rng() % 500) - 250),
                          z_list[pick], n_list[pick], rng, search);
    }
}

static void setup_parity_lab(PhysicsSimulation& sim) {
    sim.cfg.environment_mode = 4;  // Particle Soup
    sim.cfg.start_empty = false;
    sim.cfg.particle_count = 25000;
    sim.cfg.temperature_kelvin = 10000.0f;
    sim.cfg.bonds_enabled = true;
    sim.cfg.carrier_mode_enabled = true;
    sim.reset();
}

// ── Chirality check functions ────────────────────────────────────────────

static bool check_first_chiral(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_list)
        if (m.is_chiral) return true;
    return false;
}
static bool check_3_chiral(const PhysicsSimulation& sim) {
    int count = 0;
    for (const auto& m : sim.iface.molecule_list)
        if (m.is_chiral) count++;
    return count >= 3;
}
static bool check_chiral_bestiary(const PhysicsSimulation& sim) {
    int count = 0;
    for (const auto& m : sim.iface.molecule_bestiary)
        if (m.is_chiral) count++;
    return count >= 3;
}
static bool check_4_atom_molecule(const PhysicsSimulation& sim) {
    for (const auto& m : sim.iface.molecule_list)
        if (m.atom_indices.size() >= 4) return true;
    return false;
}
static bool check_parity_observed(const PhysicsSimulation& sim) {
    return sim.achievements.seen_parity_violation;
}
static bool check_beta_decay_seen(const PhysicsSimulation& sim) {
    return sim.achievements.total_beta_decays > 0;
}
static bool check_weak_boson_seen(const PhysicsSimulation& sim) {
    return sim.iface.type_counts_display[W_PLUS_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[W_MINUS_TYPE_PHYS] > 0
        || sim.iface.type_counts_display[Z_BOSON_TYPE_PHYS] > 0;
}
static bool check_5_weak_decays(const PhysicsSimulation& sim) {
    return sim.achievements.total_left_handed_weak_decays >= 5;
}

// ── Chirality task arrays ────────────────────────────────────────────────

static const int TYPES_CHIRALITY[] = {0, 1, 2, 3};
static const int TYPES_PARITY[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 25, 26, 27, 28};

static const ScenarioTask TASKS_CHIRALITY_LAB[] = {
    { "In chemistry, some molecules possess a remarkable property: they cannot be "
      "superimposed on their mirror image, just as your left hand cannot perfectly "
      "overlap your right. This is chirality — the handedness of molecules.",
      "Create a molecule with 4+ atoms",
      "Bring different atoms close together to form bonds",
      check_4_atom_molecule },
    { "A chiral center forms when a carbon atom bonds to three or more different "
      "types of atoms. The resulting molecule comes in two mirror-image forms — "
      "enantiomers — like a left glove and a right glove.",
      "Create a chiral molecule",
      "Bond a carbon to different atom types (H, N, O, F, Cl)",
      check_first_chiral },
    { "Life itself chose chirality. Nearly all amino acids in living organisms "
      "are left-handed (L-form). Nearly all sugars are right-handed (D-form). "
      "This homochirality is one of biology's deepest mysteries.",
      "Create 3 chiral molecules simultaneously",
      "Build more complex molecules with carbon centers and mixed substituents",
      check_3_chiral },
    { "Your collection grows. Each chiral molecule you discover adds to the "
      "stereochemical library — a catalogue of molecular handedness.",
      "Discover 3 distinct chiral molecule formulas",
      "Try building molecules with different elemental compositions",
      check_chiral_bestiary },
};

static const ScenarioTask TASKS_PARITY_LAB[] = {
    { "In 1956, Chien-Shiung Wu shattered a fundamental assumption of physics. "
      "She proved that the weak nuclear force — unlike gravity, electromagnetism, "
      "and the strong force — does NOT treat left and right equally.",
      "Observe a weak force boson (W or Z)",
      "High-energy collisions can produce W and Z bosons",
      check_weak_boson_seen },
    { "Beta decay is the weak force in action: a neutron transforms into a proton, "
      "emitting an electron and an antineutrino. But the emitted electron is always "
      "left-handed — parity is maximally violated.",
      "Observe beta decay",
      "Neutron-rich nuclei undergo beta minus decay",
      check_beta_decay_seen },
    { "The weak force only couples to left-handed particles and right-handed "
      "antiparticles. This parity violation is not a small effect — it is absolute. "
      "Nature has a preferred handedness at its most fundamental level.",
      "Observe parity violation in a weak decay",
      "Any beta decay or weak flavor change demonstrates parity violation",
      check_parity_observed },
    { "Wu's experiment used cobalt-60 nuclei aligned in a magnetic field. "
      "The electrons from beta decay preferentially flew in one direction — "
      "proof that the universe itself is not ambidextrous.",
      "Trigger 5 parity-violating weak decays",
      "More weak interactions give more parity violations",
      check_5_weak_decays },
};

// ── Scenario definitions ────────────────────────────────────────────────────

static const Scenario SCENARIOS[] = {
    { "First Light",
      "Create your first photon from atomic interactions.",
      "Nuclear", "The Dawn of Stars",
      setup_first_light, TASKS_FIRST_LIGHT, 3,
      make_rules(TYPES_FIRST_LIGHT, ARRLEN(TYPES_FIRST_LIGHT), 2, false, false, false) },

    { "Hydrogen Factory",
      "Build complete hydrogen atoms with electrons.",
      "Nuclear", "Building the Periodic Table",
      setup_hydrogen_factory, TASKS_HYDROGEN_FACTORY, 3,
      make_rules(TYPES_H_FACTORY, ARRLEN(TYPES_H_FACTORY), 1, false, false, false) },

    { "Solar Core",
      "Trigger nuclear fusion in the heart of a star.",
      "Nuclear", "Heart of a Star",
      setup_solar_core, TASKS_SOLAR_CORE, 4,
      make_rules(TYPES_SOLAR, ARRLEN(TYPES_SOLAR), 2, false, true, false) },

    { "Chain Reaction",
      "Cause a fission chain reaction.",
      "Nuclear", "Critical Mass",
      setup_chain_reaction, TASKS_CHAIN_REACTION, 3,
      make_rules(TYPES_CHAIN, ARRLEN(TYPES_CHAIN), 92, false, false, false) },

    { "Antimatter Lab",
      "Create and observe particle-antiparticle annihilation.",
      "Nuclear", "Through the Looking Glass",
      setup_antimatter_lab, TASKS_ANTIMATTER_LAB, 4,
      make_rules(TYPES_ANTIMATTER, ARRLEN(TYPES_ANTIMATTER), 2, false, false, false) },

    { "Water World",
      "Form H2O water molecules through covalent bonding.",
      "Chemistry", "Recipe for Life",
      setup_water_world, TASKS_WATER_WORLD, 4,
      make_rules(TYPES_WATER, ARRLEN(TYPES_WATER), 8, true, false, false) },

    { "Chemistry Set",
      "Create different molecule types from mixed atoms.",
      "Chemistry", "The Molecular Workshop",
      setup_chemistry_set, TASKS_CHEMISTRY_SET, 3,
      make_rules(TYPES_CHEM, ARRLEN(TYPES_CHEM), 17, true, false, false) },

    { "Dark Sector",
      "Observe dark matter and dark energy.",
      "Cosmology", "The Invisible Universe",
      setup_dark_sector, TASKS_DARK_SECTOR, 3,
      make_rules(TYPES_DARK, ARRLEN(TYPES_DARK), 2, false, true, false) },

    { "Particle Zoo",
      "Discover the menagerie of fundamental particles.",
      "Cosmology", "Catalogue of Creation",
      setup_particle_zoo, TASKS_PARTICLE_ZOO, 4,
      rules_sm_all(26, false, true, true) },

    { "Stellar Nucleosynthesis",
      "Build elements up to Iron through fusion chains.",
      "Cosmology", "Forging the Elements",
      setup_nucleosynthesis, TASKS_NUCLEOSYNTHESIS, 4,
      make_rules(TYPES_NUCLEO, ARRLEN(TYPES_NUCLEO), 26, false, true, false) },

    { "Free Play: Lab",
      "Empty lab. Create whatever you want!",
      "Sandbox", nullptr,
      setup_free_lab, nullptr, 0,
      RULES_UNRESTRICTED },

    { "Free Play: Space",
      "Cosmic void with primordial particles.",
      "Sandbox", nullptr,
      setup_free_space, nullptr, 0,
      RULES_UNRESTRICTED },

    // ── Cosmic Evolution arc (indices 12-17) ─────────────────────────────

    { "Quark Soup",
      "Witness the universe's first microsecond — quarks and gluons in primal chaos.",
      "Cosmology", "The First Microsecond",
      setup_quark_soup, TASKS_QUARK_SOUP, 3,
      make_rules(TYPES_QGP, ARRLEN(TYPES_QGP), -1, false, false, true) },

    { "Cosmic Dawn",
      "From the Big Bang inferno to the first atoms and light.",
      "Cosmology", "From Fire to First Light",
      setup_cosmic_dawn, TASKS_COSMIC_DAWN, 4,
      rules_sm_all(2, false, false, true) },

    { "Neutron Star",
      "Explore the superfluid heart of a dead star.",
      "Cosmology", "Heart of a Dead Star",
      setup_neutron_star_scenario, TASKS_NEUTRON_STAR, 4,
      make_rules(TYPES_NEUTRON, ARRLEN(TYPES_NEUTRON), 2, false, true, false) },

    { "Magnetar",
      "The most magnetic object in the universe breeds spin waves.",
      "Cosmology", "The Most Magnetic Object in the Universe",
      setup_magnetar, TASKS_MAGNETAR, 3,
      make_rules(TYPES_NEUTRON, ARRLEN(TYPES_NEUTRON), 2, false, true, false) },

    { "SUSY World",
      "Step beyond the Standard Model into supersymmetry.",
      "Cosmology", "Beyond the Standard Model",
      setup_susy_world, TASKS_SUSY_WORLD, 3,
      make_rules(TYPES_SUSY, ARRLEN(TYPES_SUSY), -1, false, false, false) },

    { "The Collider",
      "Smash particles together at humanity's frontier of discovery.",
      "Cosmology", "Smashing the Frontier",
      setup_the_collider, TASKS_THE_COLLIDER, 4,
      rules_sm_all(26, false, true, true) },

    // ── Chirality arc (indices 18-19) ─────────────────────────────────

    { "Chirality Lab",
      "Explore molecular handedness — create chiral molecules with mirror-image forms.",
      "Chemistry", "The Mirror Molecule",
      setup_chirality_lab, TASKS_CHIRALITY_LAB, 4,
      make_rules(TYPES_CHIRALITY, ARRLEN(TYPES_CHIRALITY), 17, true, false, false) },

    { "Parity Violation",
      "Discover why the universe is not symmetric — the weak force breaks the mirror.",
      "Nuclear", "Wu's Experiment",
      setup_parity_lab, TASKS_PARITY_LAB, 4,
      make_rules(TYPES_PARITY, ARRLEN(TYPES_PARITY), 26, true, true, true) },
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
    static const Scenario empty = { "", "", "", nullptr, nullptr, nullptr, 0, {} };
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
