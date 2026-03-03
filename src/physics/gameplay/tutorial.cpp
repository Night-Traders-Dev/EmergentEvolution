#include "physics/tutorial.h"
#include "physics/simulation.h"

// ── Tutorial step definitions ────────────────────────────────────────────────

static bool check_particles_spawned(const PhysicsSimulation& sim) {
    return sim.iface.active_particle_display > 10;
}

static bool check_zoom_changed(const PhysicsSimulation& sim) {
    return std::abs(sim.cfg.camera_zoom - 1.0f) > 0.05f;
}

static bool check_spawn_menu(const PhysicsSimulation& sim) {
    return sim.iface.spawn_menu_visible && sim.iface.spawn_type > 0;
}

static bool check_element_exists(const PhysicsSimulation& sim) {
    return !sim.iface.element_list.empty();
}

static bool check_force_object(const PhysicsSimulation& sim) {
    return sim.force_object_count_ > 0;
}

static bool check_time_changed(const PhysicsSimulation& sim) {
    return std::abs(sim.cfg.time_scale - 1.0f) > 0.01f;
}

static const TutorialStep TUTORIAL_STEPS[] = {
    {
        "Welcome to Particle Playground!",
        "This simulator lets you explore particle physics — from\n"
        "quarks to atoms to molecules. Click anywhere to continue.",
        nullptr  // manual advance
    },
    {
        "Spawning Particles",
        "Click anywhere in the world to spawn particles.\n"
        "Try clicking a few times to create some!",
        check_particles_spawned
    },
    {
        "Camera Controls",
        "Scroll to zoom in and out. Right-click and drag to pan.\n"
        "You can also use WASD keys to move the camera.",
        check_zoom_changed
    },
    {
        "Spawn Menu",
        "Press F3 to open the Spawn Menu. Select different\n"
        "particle types — protons, electrons, photons and\n"
        "more. Try selecting a different type!",
        check_spawn_menu
    },
    {
        "Building Elements",
        "Spawn protons and neutrons close together — they'll\n"
        "bind into nuclei! Add electrons to form complete atoms.\n"
        "Watch for element creation in the bottom bar.",
        check_element_exists
    },
    {
        "Force Objects",
        "Open the Tools popup (wrench icon in bottom bar) and\n"
        "place a force object like an EM Field or Gravity Well.\n"
        "These create persistent forces in the simulation.",
        check_force_object
    },
    {
        "Time Controls",
        "Use [ and ] keys to slow down or speed up time.\n"
        "Or use the speed buttons in the bottom bar.\n"
        "Space pauses and unpauses the simulation.",
        check_time_changed
    },
    {
        "Molecule Import",
        "You can import custom molecules from .ppmol files!\n"
        "Use the Spawn Menu > Molecules section, or load\n"
        "directly with File > Import Molecule.\n\n"
        "Generate .ppmol files with the ppmol_gen tool:\n"
        "  ppmol_gen gen molecule.ppmol H2O",
        nullptr  // manual advance
    },
    {
        "Explore!",
        "You've learned the basics! Some things to try:\n"
        "- Trigger nuclear fusion at high temperatures\n"
        "- Create molecules by bonding atoms together\n"
        "- Use the Particle Accelerator tool\n"
        "- Try different Environment presets (F2 > Env)\n\n"
        "Press Escape for the pause menu. Have fun!",
        nullptr  // manual advance (final step)
    },
};

static constexpr int TUTORIAL_STEP_COUNT = sizeof(TUTORIAL_STEPS) / sizeof(TUTORIAL_STEPS[0]);

// ── TutorialManager implementation ───────────────────────────────────────────

void TutorialManager::start() {
    active = true;
    current_step = 0;
}

void TutorialManager::advance() {
    current_step++;
    if (current_step >= TUTORIAL_STEP_COUNT) {
        active = false;
        current_step = 0;
    }
}

void TutorialManager::skip() {
    active = false;
    current_step = 0;
}

bool TutorialManager::is_step_complete(const PhysicsSimulation& sim) const {
    if (current_step >= TUTORIAL_STEP_COUNT) return false;
    auto check = TUTORIAL_STEPS[current_step].check_complete;
    return check && check(sim);
}

const TutorialStep& TutorialManager::current() const {
    static const TutorialStep empty = { "", "", nullptr };
    if (current_step >= TUTORIAL_STEP_COUNT) return empty;
    return TUTORIAL_STEPS[current_step];
}

int TutorialManager::step_count() const {
    return TUTORIAL_STEP_COUNT;
}
