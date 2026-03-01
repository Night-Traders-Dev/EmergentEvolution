#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ── Achievement IDs ─────────────────────────────────────────────────────────
// Each achievement has a unique ID used for indexing into the unlock bitfield.
// Grouped by category for UI display.

enum AchievementID : uint32_t {
    // ── Nuclear Physics ─────────────────────────────────────────────────────
    ACH_FIRST_FUSION = 0,       // Trigger your first fusion reaction
    ACH_FIRST_FISSION,          // Trigger your first fission reaction
    ACH_FIRST_ANNIHILATION,     // Witness matter-antimatter annihilation
    ACH_FIRST_ALPHA_DECAY,      // Observe alpha decay
    ACH_FIRST_BETA_DECAY,       // Observe beta decay
    ACH_CHAIN_REACTION,         // 3+ fission events within 60 frames
    ACH_FUSION_10,              // 10 total fusion events
    ACH_FUSION_100,             // 100 total fusion events
    ACH_FIRST_SPALLATION,       // Trigger nuclear spallation
    ACH_FIRST_PHOTOELECTRIC,    // Trigger the photoelectric effect
    ACH_FIRST_PAIR_PRODUCTION,  // Witness pair production (gamma -> e+e-)

    // ── Element Creation ────────────────────────────────────────────────────
    ACH_FIRST_ELEMENT,          // Create any element (Z >= 1 with electrons)
    ACH_HYDROGEN,               // Create Hydrogen (Z=1)
    ACH_HELIUM,                 // Create Helium (Z=2)
    ACH_LITHIUM,                // Create Lithium (Z=3)
    ACH_CARBON,                 // Create Carbon (Z=6)
    ACH_OXYGEN,                 // Create Oxygen (Z=8)
    ACH_IRON,                   // Create Iron (Z=26) — peak of binding energy
    ACH_GOLD,                   // Create Gold (Z=79)
    ACH_URANIUM,                // Create Uranium (Z=92)
    ACH_ELEMENTS_10,            // Create 10 distinct elements
    ACH_ELEMENTS_25,            // Create 25 distinct elements

    // ── Particle Zoo ────────────────────────────────────────────────────────
    ACH_FIRST_POSITRON,         // Create a positron
    ACH_FIRST_NEUTRINO,         // Create a neutrino
    ACH_FIRST_MUON,             // Create a muon
    ACH_FIRST_TAU,              // Create a tau lepton
    ACH_FIRST_ANTIPROTON,       // Create an antiproton
    ACH_FIRST_QUARK,            // Observe a free quark
    ACH_FIRST_BOSON,            // Create a W/Z/Higgs boson
    ACH_DARK_MATTER,            // Observe dark matter particle
    ACH_PARTICLE_ZOO,           // Have all 33 particle types present simultaneously

    // ── Thermodynamics ──────────────────────────────────────────────────────
    ACH_TEMP_1000K,             // Reach 1,000 K
    ACH_TEMP_1MK,               // Reach 1,000,000 K (1 MK)
    ACH_TEMP_1GK,               // Reach 1,000,000,000 K (1 GK)
    ACH_TEMP_10GK,              // Reach 10,000,000,000 K (10 GK)
    ACH_ABSOLUTE_ZERO,          // Cool system below 2 K

    // ── Milestones ──────────────────────────────────────────────────────────
    ACH_PARTICLES_1000,         // Have 1000+ active particles simultaneously
    ACH_PARTICLES_5000,         // Have 5000+ active particles simultaneously
    ACH_PARTICLES_10000,        // Have 10000+ active particles simultaneously
    ACH_FIRST_ENTANGLED,        // Create first entangled pair
    ACH_ENTANGLED_10,           // 10+ entangled pairs active simultaneously
    ACH_FIRST_FORCE_OBJ,       // Place first force object
    ACH_FIRST_MIRROR,           // Place a mirror
    ACH_FIRST_ACCELERATOR,      // Fire the particle accelerator
    ACH_FIRST_SAVE,             // Save a simulation
    ACH_FIRST_LOAD,             // Load a simulation
    ACH_FIRST_EXPORT,           // Export an element
    ACH_FIRST_IMPORT,           // Import an element
    ACH_ANNIHILATIONS_100,      // 100 total annihilations
    ACH_NUCLEAR_DECAYS_50,      // 50 nuclear decay events
    ACH_ANTIMATTER_ELEMENT,     // Create an antimatter element

    // ── Environment Presets ─────────────────────────────────────────────────
    ACH_TRY_ALL_ENVIRONMENTS,   // Try all 12 environment presets

    // ── Chemistry & Bonds ─────────────────────────────────────────────────
    ACH_FIRST_BOND,             // Form your first covalent bond
    ACH_FIRST_MOLECULE,         // Create a molecule with bonded atoms
    ACH_MOLECULE_5_ATOMS,       // Create a molecule with 5+ atoms

    // ── Advanced Physics ──────────────────────────────────────────────────
    ACH_FIRST_VIRTUAL_PAIR,     // Observe virtual particle pair creation
    ACH_FIRST_PION_DECAY,       // Observe pion decay
    ACH_COSMIC_RAY,             // Accelerate particle to maximum speed
    ACH_PHOTON_EMISSION,        // Observe photon emission from decay

    // ── Exploration ───────────────────────────────────────────────────────
    ACH_FIRST_MOLECULE_EXPORT,  // Export a molecule to .ppmol
    ACH_SPEED_DEMON,            // Set simulation speed to maximum
    ACH_HUNDRED_ELEMENTS,       // Discover 100 distinct elements
    ACH_GRAVITON_OBSERVED,      // Observe a graviton
    ACH_LONG_PLAY,              // Run simulation for 10+ minutes

    ACH_COUNT                   // Total number of achievements
};

static_assert(ACH_COUNT <= 64, "Achievement count exceeds uint64_t bitfield capacity");

// ── Achievement metadata ────────────────────────────────────────────────────

enum AchievementCategory : uint8_t {
    ACAT_NUCLEAR = 0,
    ACAT_ELEMENTS,
    ACAT_PARTICLES,
    ACAT_THERMO,
    ACAT_MILESTONES,
    ACAT_CHEMISTRY,
    ACAT_COUNT
};

struct AchievementDef {
    AchievementID id;
    AchievementCategory category;
    const char* name;
    const char* description;
    const char* icon;           // short emoji-like symbol for display
    const char* steam_api_name; // Steam API achievement name (e.g., "ACH_FIRST_FUSION")
};

// Defined in achievements.cpp
extern const AchievementDef ACHIEVEMENT_DEFS[ACH_COUNT];
extern const char* ACHIEVEMENT_CATEGORY_NAMES[ACAT_COUNT];

// ── AchievementManager ──────────────────────────────────────────────────────

class AchievementManager {
public:
    void reset_session_counters();

    // Unlock an achievement. Returns true if newly unlocked (first time).
    bool unlock(AchievementID id);

    // Check if already unlocked
    bool is_unlocked(AchievementID id) const;

    // Get total unlocked count
    int unlocked_count() const;

    // Persistence
    bool save(const std::string& filepath) const;
    bool load(const std::string& filepath);

    // ── Event counters (accumulated during session, used for threshold checks) ──
    uint32_t total_fusions       = 0;
    uint32_t total_fissions      = 0;
    uint32_t total_annihilations = 0;
    uint32_t total_alpha_decays   = 0;
    uint32_t total_beta_decays    = 0;
    uint32_t total_nuclear_decays = 0;

    // Chain reaction tracking
    uint32_t fission_recent_count = 0;   // fissions in current window
    uint32_t fission_window_start = 0;   // frame when window started

    // Element discovery set (Z values seen)
    bool elements_discovered[120] = {};  // Z=0..119
    int  distinct_elements_count  = 0;

    // Environment exploration
    bool environments_tried[12] = {};

    // Peak records
    float    peak_temperature     = 0.0f;
    uint32_t peak_active_particles = 0;
    uint32_t peak_entangled_pairs = 0;

    // Session time tracking (for ACH_LONG_PLAY)
    float session_time_ = 0.0f;

private:
    // Bitfield for unlocked achievements (ACH_COUNT < 64, fits in 2 uint32s)
    uint64_t unlocked_bits_ = 0;
};
