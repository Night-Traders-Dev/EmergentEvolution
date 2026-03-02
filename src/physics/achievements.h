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

    // ── Scenarios ─────────────────────────────────────────────────────────
    ACH_FIRST_SCENARIO_COMPLETE,// Complete any scenario
    ACH_ALL_NUCLEAR_SCENARIOS,  // Complete all Nuclear scenarios
    ACH_ALL_CHEMISTRY_SCENARIOS,// Complete all Chemistry scenarios
    ACH_ALL_COSMOLOGY_SCENARIOS,// Complete all Cosmology scenarios
    ACH_SCENARIO_MASTER,        // Complete all non-sandbox scenarios

    // ── Beyond Standard Model ─────────────────────────────────────────────
    ACH_DARK_ENERGY,            // Observe a dark energy particle
    ACH_SUSY_PARTICLE,          // Observe a SUSY particle
    ACH_TACHYON,                // Observe a tachyon

    // ── Advanced Nuclear ──────────────────────────────────────────────────
    ACH_TRANSURANIC,            // Create transuranic element (Z > 92)
    ACH_SUPERHEAVY,             // Create superheavy element (Z > 110)
    ACH_FISSION_100,            // Trigger 100 fission events
    ACH_FUSION_1000,            // Trigger 1000 fusion events

    // ── Advanced Chemistry ────────────────────────────────────────────────
    ACH_MOLECULES_10,           // Create 10 distinct molecules
    ACH_MOLECULE_10_ATOMS,      // Create molecule with 10+ atoms

    // ── Advanced Thermodynamics ───────────────────────────────────────────
    ACH_TEMP_100GK,             // Reach 100,000,000,000 K (100 GK)

    // ── Advanced Milestones ───────────────────────────────────────────────
    ACH_PARTICLES_50000,        // Have 50,000+ active particles simultaneously
    ACH_ENTANGLED_50,           // 50+ entangled pairs active simultaneously
    ACH_FORCE_OBJECTS_5,        // Place 5 force objects
    ACH_ACCELERATOR_50,         // Fire accelerator 50 times
    ACH_PLAY_1_HOUR,            // Run simulation for 1+ hour

    // ── Quasiparticles ──────────────────────────────────────────────────
    ACH_FIRST_PLASMON,          // Observe a plasmon (collective electron oscillation)
    ACH_FIRST_PHONON,           // Observe a phonon (lattice vibration)
    ACH_FIRST_MAGNON,           // Observe a magnon (spin wave)
    ACH_FIRST_POLARON,          // Observe a polaron (dressed electron)
    ACH_FIRST_COOPER_PAIR,      // Observe a Cooper pair (superconductivity)
    ACH_FIRST_ROTON,            // Observe a roton (quantum vortex)
    ACH_FIRST_ELECTRON_HOLE,    // Observe an electron hole

    // ── Advanced Processes ──────────────────────────────────────────────
    ACH_FIRST_BREMSSTRAHLUNG,   // Observe bremsstrahlung radiation
    ACH_FIRST_HADRONIZATION,    // Observe quark hadronization
    ACH_FIRST_CARRIER_EXCHANGE, // Observe force carrier exchange
    ACH_FIRST_NEUTRINO_OSCILLATION, // Observe neutrino flavor oscillation
    ACH_FIRST_SHELL_TRANSITION, // Observe electron shell transition
    ACH_FIRST_RECOMBINATION,    // Observe electron-hole recombination
    ACH_FIRST_MESON_DECAY,      // Observe meson decay
    ACH_FIRST_WEAK_DECAY,       // Observe weak flavor change

    // ── More Elements ───────────────────────────────────────────────────
    ACH_NITROGEN,               // Create Nitrogen (Z=7)
    ACH_NEON,                   // Create Neon (Z=10)
    ACH_SILICON,                // Create Silicon (Z=14)
    ACH_ELEMENTS_50,            // Discover 50 distinct elements

    // ── Specific Molecules ──────────────────────────────────────────────
    ACH_WATER,                  // Create water (H2O)
    ACH_METHANE,                // Create methane (CH4)
    ACH_AMMONIA,                // Create ammonia (NH3)
    ACH_MOLECULES_25,           // Discover 25 distinct molecules

    // ── Higher Thresholds ───────────────────────────────────────────────
    ACH_ANNIHILATIONS_500,      // 500 total annihilations
    ACH_NUCLEAR_DECAYS_200,     // 200 nuclear decay events
    ACH_FUSIONS_5000,           // 5000 fusion events
    ACH_BONDS_50,               // Form 50 total covalent bonds

    // ── BSM Extras ──────────────────────────────────────────────────────
    ACH_FIRST_MONOPOLE,         // Observe a magnetic monopole
    ACH_FIRST_NEUTRALINO,       // Observe a neutralino (LSP)
    ACH_FIRST_GRAVITINO,        // Observe a gravitino

    // ── Cutscenes & Lore ────────────────────────────────────────────────
    ACH_FIRST_CUTSCENE,         // Watch your first cutscene
    ACH_ALL_INTROS_WATCHED,     // Watch all intro cutscenes

    // ── Periodic Table Collection (Z=1..118) ────────────────────────────
    // One achievement per element — contiguous range for easy indexing
    ACH_ELEMENT_BASE,                           // = first element achievement slot
    ACH_ELEMENT_1   = ACH_ELEMENT_BASE,         // Hydrogen
    ACH_ELEMENT_118 = ACH_ELEMENT_BASE + 117,   // Oganesson

    ACH_COUNT                   // Total number of achievements
};

static constexpr uint32_t ACH_ELEMENT_SLOTS = 4;  // number of uint64_t slots in bitfield
static_assert(ACH_COUNT <= 256, "Achievement count exceeds quad uint64_t bitfield capacity");

// ── Achievement metadata ────────────────────────────────────────────────────

enum AchievementCategory : uint8_t {
    ACAT_NUCLEAR = 0,
    ACAT_ELEMENTS,
    ACAT_PARTICLES,
    ACAT_THERMO,
    ACAT_MILESTONES,
    ACAT_CHEMISTRY,
    ACAT_SCENARIOS,
    ACAT_PERIODIC_TABLE,    // Rendered as grid, not list
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

// Defined in achievements.cpp — element entries filled at runtime by init_achievement_defs()
extern AchievementDef ACHIEVEMENT_DEFS[ACH_COUNT];
extern const char* ACHIEVEMENT_CATEGORY_NAMES[ACAT_COUNT];
void init_achievement_defs();  // Must be called once at startup

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

    // Session time tracking (for ACH_LONG_PLAY / ACH_PLAY_1_HOUR)
    float session_time_ = 0.0f;

    // Advanced counters
    uint32_t total_accelerator_fires    = 0;
    uint32_t total_force_objects_placed = 0;
    uint32_t total_bonds_formed         = 0;

    // Process counters (accumulated per session for lifetime stats)
    uint32_t total_spallations       = 0;
    uint32_t total_photoelectric     = 0;
    uint32_t total_pair_productions  = 0;
    uint32_t total_virtual_pairs     = 0;
    uint32_t total_carrier_exchanges = 0;
    uint32_t total_shell_transitions = 0;
    uint32_t total_meson_decays      = 0;
    uint32_t total_bremsstrahlung    = 0;
    uint32_t total_neutrino_oscillations = 0;
    uint32_t total_molecules_formed  = 0;  // every molecule detection (not just distinct)

    // Scenario completion tracking (indexed by scenario_idx)
    bool scenarios_completed[20] = {};

    // Cutscene tracking: bit i = intro for scenario i has been watched
    uint32_t cutscene_intros_seen = 0;

    // Quasiparticle observation flags (one bit each — have we ever seen one?)
    bool seen_plasmon     = false;
    bool seen_phonon      = false;
    bool seen_magnon      = false;
    bool seen_polaron     = false;
    bool seen_cooper_pair = false;
    bool seen_roton       = false;
    bool seen_electron_hole = false;

    // Process observation flags
    bool seen_bremsstrahlung       = false;
    bool seen_hadronization        = false;
    bool seen_carrier_exchange     = false;
    bool seen_neutrino_oscillation = false;
    bool seen_shell_transition     = false;
    bool seen_recombination        = false;
    bool seen_meson_decay          = false;
    bool seen_weak_decay           = false;

private:
    // Bitfield for unlocked achievements (ACH_COUNT <= 256, four uint64_t slots)
    uint64_t unlocked_bits_[ACH_ELEMENT_SLOTS] = {};
};

// ── Lifetime Stats ──────────────────────────────────────────────────────────
// Persistent career statistics accumulated across all sessions.

static constexpr int LIFETIME_PARTICLE_TYPES = 74;
static constexpr int LIFETIME_ELEMENT_COUNT  = 119;  // Z=0..118

struct LifetimeStats {
    // Global totals
    uint64_t total_ticks           = 0;
    double   total_play_time       = 0.0;   // seconds
    uint32_t total_simulations     = 0;
    uint64_t total_fusions         = 0;
    uint64_t total_fissions        = 0;
    uint64_t total_annihilations   = 0;
    uint64_t total_decays          = 0;
    uint64_t total_bonds_formed    = 0;

    // Extended reaction counters
    uint64_t total_spallations       = 0;
    uint64_t total_photoelectric     = 0;
    uint64_t total_pair_productions  = 0;
    uint64_t total_virtual_pairs     = 0;
    uint64_t total_carrier_exchanges = 0;
    uint64_t total_shell_transitions = 0;
    uint64_t total_meson_decays      = 0;
    uint64_t total_bremsstrahlung    = 0;
    uint64_t total_neutrino_oscillations = 0;
    uint64_t total_accelerator_fires = 0;

    // Per-particle-type (74 types)
    uint64_t particles_spawned[LIFETIME_PARTICLE_TYPES] = {};
    uint32_t particles_peak[LIFETIME_PARTICLE_TYPES]    = {};
    uint64_t total_particles_spawned = 0;     // grand total across all types
    uint32_t distinct_particle_types_seen = 0; // how many of 74 types ever observed

    // Per-element (Z=0..118)
    uint64_t elements_created[LIFETIME_ELEMENT_COUNT] = {};
    uint32_t elements_peak[LIFETIME_ELEMENT_COUNT]    = {};
    uint32_t highest_z_created = 0;

    // Molecules
    uint32_t total_molecules_discovered = 0;  // distinct molecule formulas
    uint64_t total_molecules_formed     = 0;  // every molecule detection
    uint32_t largest_molecule_atoms     = 0;  // most atoms in a single molecule

    // Gameplay
    uint32_t total_scenarios_completed = 0;
    uint32_t environments_explored     = 0;   // distinct environments tried

    // All-time peaks
    float    peak_temperature    = 0.0f;
    uint32_t peak_particles      = 0;
    uint32_t peak_entangled      = 0;

    // Persistence
    bool save(const std::string& filepath) const;
    bool load(const std::string& filepath);
};
