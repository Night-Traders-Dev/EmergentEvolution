#include "physics/achievements.h"
#include <fstream>
#include <cstring>

// ── Achievement definitions ─────────────────────────────────────────────────

const char* ACHIEVEMENT_CATEGORY_NAMES[ACAT_COUNT] = {
    "Nuclear Physics",
    "Element Creation",
    "Particle Zoo",
    "Thermodynamics",
    "Milestones",
    "Chemistry"
};

const AchievementDef ACHIEVEMENT_DEFS[ACH_COUNT] = {
    // ── Nuclear Physics ─────────────────────────────────────────────────────
    { ACH_FIRST_FUSION,        ACAT_NUCLEAR,   "First Light",           "Trigger your first fusion reaction",                 "[F]" },
    { ACH_FIRST_FISSION,       ACAT_NUCLEAR,   "Splitting the Atom",    "Trigger your first fission reaction",                "[X]" },
    { ACH_FIRST_ANNIHILATION,  ACAT_NUCLEAR,   "Antimatter!",           "Witness matter-antimatter annihilation",              "[A]" },
    { ACH_FIRST_ALPHA_DECAY,   ACAT_NUCLEAR,   "Alpha Radiation",       "Observe alpha decay from a nucleus",                 "[a]" },
    { ACH_FIRST_BETA_DECAY,    ACAT_NUCLEAR,   "Beta Radiation",        "Observe beta decay from a nucleus",                  "[b]" },
    { ACH_CHAIN_REACTION,      ACAT_NUCLEAR,   "Chain Reaction",        "Trigger 3+ fission events within 1 second",          "[C]" },
    { ACH_FUSION_10,           ACAT_NUCLEAR,   "Stellar Nursery",       "Trigger 10 fusion reactions",                        "[*]" },
    { ACH_FUSION_100,          ACAT_NUCLEAR,   "Stellar Core",          "Trigger 100 fusion reactions",                       "[S]" },
    { ACH_FIRST_SPALLATION,    ACAT_NUCLEAR,   "Nuclear Demolition",    "Trigger nuclear spallation",                         "[!n]"},
    { ACH_FIRST_PHOTOELECTRIC, ACAT_NUCLEAR,   "Einstein's Nobel",      "Trigger the photoelectric effect",                   "[PE]"},
    { ACH_FIRST_PAIR_PRODUCTION,ACAT_NUCLEAR,  "Something from Nothing","Witness pair production",                            "[PP]"},

    // ── Element Creation ────────────────────────────────────────────────────
    { ACH_FIRST_ELEMENT,       ACAT_ELEMENTS,  "Elemental",             "Create any element with bound electrons",             "[E]" },
    { ACH_HYDROGEN,            ACAT_ELEMENTS,  "Simplest Atom",         "Create Hydrogen (Z=1)",                              "[H]" },
    { ACH_HELIUM,              ACAT_ELEMENTS,  "Noble Gas",             "Create Helium (Z=2)",                                "[He]"},
    { ACH_LITHIUM,             ACAT_ELEMENTS,  "Third Element",         "Create Lithium (Z=3)",                               "[Li]"},
    { ACH_CARBON,              ACAT_ELEMENTS,  "Basis of Life",         "Create Carbon (Z=6)",                                "[C]" },
    { ACH_OXYGEN,              ACAT_ELEMENTS,  "Breath of Life",        "Create Oxygen (Z=8)",                                "[O]" },
    { ACH_IRON,                ACAT_ELEMENTS,  "Iron Peak",             "Create Iron (Z=26) -- peak of binding energy",       "[Fe]"},
    { ACH_GOLD,                ACAT_ELEMENTS,  "Alchemist's Dream",     "Create Gold (Z=79)",                                 "[Au]"},
    { ACH_URANIUM,             ACAT_ELEMENTS,  "Heavy Metal",           "Create Uranium (Z=92)",                              "[U]" },
    { ACH_ELEMENTS_10,         ACAT_ELEMENTS,  "Periodic Table",        "Discover 10 distinct elements",                      "[PT]"},
    { ACH_ELEMENTS_25,         ACAT_ELEMENTS,  "Quarter Century",       "Discover 25 distinct elements",                      "[25]"},

    // ── Particle Zoo ────────────────────────────────────────────────────────
    { ACH_FIRST_POSITRON,      ACAT_PARTICLES, "Antimatter Twin",       "Create a positron",                                  "[e+]"},
    { ACH_FIRST_NEUTRINO,      ACAT_PARTICLES, "Ghost Particle",        "Create a neutrino",                                  "[v]" },
    { ACH_FIRST_MUON,          ACAT_PARTICLES, "Who Ordered That?",     "Create a muon",                                      "[u]" },
    { ACH_FIRST_TAU,           ACAT_PARTICLES, "Third Generation",      "Create a tau lepton",                                "[T]" },
    { ACH_FIRST_ANTIPROTON,    ACAT_PARTICLES, "Mirror Proton",         "Create an antiproton",                               "[p-]"},
    { ACH_FIRST_QUARK,         ACAT_PARTICLES, "Asymptotic Freedom",    "Observe a free quark",                               "[q]" },
    { ACH_FIRST_BOSON,         ACAT_PARTICLES, "Force Carrier",         "Create a W, Z, or Higgs boson",                      "[W]" },
    { ACH_DARK_MATTER,         ACAT_PARTICLES, "Dark Side",             "Observe a dark matter particle",                     "[D]" },
    { ACH_PARTICLE_ZOO,        ACAT_PARTICLES, "The Standard Model",    "Have all 33 particle types present at once",         "[SM]"},

    // ── Thermodynamics ──────────────────────────────────────────────────────
    { ACH_TEMP_1000K,          ACAT_THERMO,    "Getting Warm",          "Reach 1,000 K",                                     "[1k]"},
    { ACH_TEMP_1MK,            ACAT_THERMO,    "Stellar Temperature",   "Reach 1,000,000 K",                                 "[1M]"},
    { ACH_TEMP_1GK,            ACAT_THERMO,    "Big Bang Nucleosynthesis","Reach 1,000,000,000 K",                            "[1G]"},
    { ACH_TEMP_10GK,           ACAT_THERMO,    "Quark Epoch",           "Reach 10,000,000,000 K",                            "[10G]"},
    { ACH_ABSOLUTE_ZERO,       ACAT_THERMO,    "Absolute Zero",         "Cool the system below 2 K",                         "[0K]"},

    // ── Milestones ──────────────────────────────────────────────────────────
    { ACH_PARTICLES_1000,      ACAT_MILESTONES,"Busy Universe",         "Have 1,000+ active particles simultaneously",        "[1k]"},
    { ACH_PARTICLES_5000,      ACAT_MILESTONES,"Crowded Space",         "Have 5,000+ active particles simultaneously",        "[5k]"},
    { ACH_PARTICLES_10000,     ACAT_MILESTONES,"Particle Soup",         "Have 10,000+ active particles simultaneously",       "[10k]"},
    { ACH_FIRST_ENTANGLED,     ACAT_MILESTONES,"Spooky Action",         "Create your first entangled pair",                   "[~~]"},
    { ACH_ENTANGLED_10,        ACAT_MILESTONES,"Quantum Network",       "Have 10+ entangled pairs active at once",            "[QN]"},
    { ACH_FIRST_FORCE_OBJ,     ACAT_MILESTONES,"Field Engineer",        "Place your first force object",                      "[FO]"},
    { ACH_FIRST_MIRROR,        ACAT_MILESTONES,"Mirror Mirror",         "Place a mirror",                                     "[||]"},
    { ACH_FIRST_ACCELERATOR,   ACAT_MILESTONES,"CERN at Home",          "Fire the particle accelerator",                      "[>>]"},
    { ACH_FIRST_SAVE,          ACAT_MILESTONES,"Saved State",           "Save a simulation to disk",                          "[S]" },
    { ACH_FIRST_LOAD,          ACAT_MILESTONES,"Time Travel",           "Load a simulation from disk",                        "[L]" },
    { ACH_FIRST_EXPORT,        ACAT_MILESTONES,"Element Exporter",      "Export an element to a .ppel file",                  "[Ex]"},
    { ACH_FIRST_IMPORT,        ACAT_MILESTONES,"Element Importer",      "Import an element from a .ppel file",                "[Im]"},
    { ACH_ANNIHILATIONS_100,   ACAT_MILESTONES,"Annihilator",           "Achieve 100 total annihilations",                    "[!!]"},
    { ACH_NUCLEAR_DECAYS_50,   ACAT_MILESTONES,"Radioactive",           "Witness 50 nuclear decay events",                    "[ND]"},
    { ACH_ANTIMATTER_ELEMENT,  ACAT_MILESTONES,"Antimatter Atom",       "Create an antimatter element",                       "[~E]"},
    { ACH_TRY_ALL_ENVIRONMENTS,ACAT_MILESTONES,"Explorer",              "Try all 12 environment presets",                     "[!]" },

    // ── Chemistry & Bonds ──────────────────────────────────────────────────
    { ACH_FIRST_BOND,          ACAT_CHEMISTRY, "Chemical Bond",         "Form your first covalent bond",                      "[==]"},
    { ACH_FIRST_MOLECULE,      ACAT_CHEMISTRY, "Molecular!",            "Create a molecule with bonded atoms",                "[M]" },
    { ACH_MOLECULE_5_ATOMS,    ACAT_CHEMISTRY, "Complex Chemistry",     "Create a molecule with 5+ atoms",                    "[M5]"},

    // ── Advanced Physics ──────────────────────────────────────────────────
    { ACH_FIRST_VIRTUAL_PAIR,  ACAT_PARTICLES, "Vacuum Fluctuation",    "Observe virtual particle pair creation",              "[VP]"},
    { ACH_FIRST_PION_DECAY,    ACAT_NUCLEAR,   "Pion Cascade",          "Observe pion decay",                                  "[pi]"},
    { ACH_COSMIC_RAY,          ACAT_MILESTONES,"Cosmic Ray",            "Fire a particle at maximum speed",                    "[CR]"},
    { ACH_PHOTON_EMISSION,     ACAT_NUCLEAR,   "Let There Be Light",    "Observe photon emission from a decay",                "[hv]"},

    // ── Exploration ───────────────────────────────────────────────────────
    { ACH_FIRST_MOLECULE_EXPORT,ACAT_MILESTONES,"Molecule Exporter",    "Export a molecule to a .ppmol file",                  "[Mx]"},
    { ACH_SPEED_DEMON,         ACAT_MILESTONES,"Speed Demon",           "Set simulation speed to maximum",                     "[>>]"},
    { ACH_HUNDRED_ELEMENTS,    ACAT_ELEMENTS,  "Centurion",             "Discover 100 distinct elements",                      "[100]"},
    { ACH_GRAVITON_OBSERVED,   ACAT_PARTICLES, "Gravity's Messenger",   "Observe a graviton particle",                         "[Gr]"},
    { ACH_LONG_PLAY,           ACAT_MILESTONES,"Dedicated Physicist",   "Run a simulation for 10+ minutes",                    "[10m]"},
};

// ── AchievementManager implementation ───────────────────────────────────────

void AchievementManager::reset_session_counters() {
    total_fusions       = 0;
    total_fissions      = 0;
    total_annihilations = 0;
    total_alpha_decays  = 0;
    total_beta_decays   = 0;
    total_nuclear_decays = 0;
    fission_recent_count = 0;
    fission_window_start = 0;
    peak_temperature     = 0.0f;
    peak_active_particles = 0;
    peak_entangled_pairs = 0;
    // Note: elements_discovered and environments_tried persist across resets
}

bool AchievementManager::unlock(AchievementID id) {
    if (id >= ACH_COUNT) return false;
    uint64_t bit = 1ULL << id;
    if (unlocked_bits_ & bit) return false;  // already unlocked
    unlocked_bits_ |= bit;
    return true;
}

bool AchievementManager::is_unlocked(AchievementID id) const {
    if (id >= ACH_COUNT) return false;
    return (unlocked_bits_ & (1ULL << id)) != 0;
}

int AchievementManager::unlocked_count() const {
    int count = 0;
    uint64_t bits = unlocked_bits_;
    while (bits) {
        count += (bits & 1);
        bits >>= 1;
    }
    return count;
}

// ── Persistence (.ppach file) ───────────────────────────────────────────────

static constexpr uint32_t PPACH_MAGIC   = 0x48434150;  // "PACH" little-endian
static constexpr uint32_t PPACH_VERSION = 1;

bool AchievementManager::save(const std::string& filepath) const {
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    f.write(reinterpret_cast<const char*>(&PPACH_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPACH_VERSION), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&unlocked_bits_), sizeof(uint64_t));

    // Counters
    f.write(reinterpret_cast<const char*>(&total_fusions), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_fissions), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_annihilations), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_alpha_decays), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_beta_decays), sizeof(uint32_t));

    // Element discovery
    f.write(reinterpret_cast<const char*>(elements_discovered), sizeof(elements_discovered));
    f.write(reinterpret_cast<const char*>(&distinct_elements_count), sizeof(int));

    // Environment exploration
    f.write(reinterpret_cast<const char*>(environments_tried), sizeof(environments_tried));

    // Peak records
    f.write(reinterpret_cast<const char*>(&peak_temperature), sizeof(float));
    f.write(reinterpret_cast<const char*>(&peak_active_particles), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&peak_entangled_pairs), sizeof(uint32_t));

    return f.good();
}

bool AchievementManager::load(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPACH_MAGIC || version != PPACH_VERSION)
        return false;

    f.read(reinterpret_cast<char*>(&unlocked_bits_), sizeof(uint64_t));

    // Counters
    f.read(reinterpret_cast<char*>(&total_fusions), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&total_fissions), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&total_annihilations), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&total_alpha_decays), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&total_beta_decays), sizeof(uint32_t));

    // Element discovery
    f.read(reinterpret_cast<char*>(elements_discovered), sizeof(elements_discovered));
    f.read(reinterpret_cast<char*>(&distinct_elements_count), sizeof(int));

    // Environment exploration
    f.read(reinterpret_cast<char*>(environments_tried), sizeof(environments_tried));

    // Peak records
    f.read(reinterpret_cast<char*>(&peak_temperature), sizeof(float));
    f.read(reinterpret_cast<char*>(&peak_active_particles), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&peak_entangled_pairs), sizeof(uint32_t));

    return f.good();
}
