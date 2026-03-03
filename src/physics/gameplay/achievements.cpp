#include "physics/achievements.h"
#include "physics/ui_data.h"
#include <fstream>
#include <cstring>
#include <cstdio>

// ── Achievement definitions ─────────────────────────────────────────────────

const char* ACHIEVEMENT_CATEGORY_NAMES[ACAT_COUNT] = {
    "Nuclear Physics",
    "Element Creation",
    "Particle Zoo",
    "Thermodynamics",
    "Milestones",
    "Chemistry",
    "Scenarios",
    "Periodic Table"
};

AchievementDef ACHIEVEMENT_DEFS[ACH_COUNT] = {
    // ── Nuclear Physics ─────────────────────────────────────────────────────
    { ACH_FIRST_FUSION,        ACAT_NUCLEAR,   "First Light",           "Trigger your first fusion reaction",                 "[F]",  "ACH_FIRST_FUSION"        },
    { ACH_FIRST_FISSION,       ACAT_NUCLEAR,   "Splitting the Atom",    "Trigger your first fission reaction",                "[X]",  "ACH_FIRST_FISSION"       },
    { ACH_FIRST_ANNIHILATION,  ACAT_NUCLEAR,   "Antimatter!",           "Witness matter-antimatter annihilation",              "[A]",  "ACH_FIRST_ANNIHILATION"  },
    { ACH_FIRST_ALPHA_DECAY,   ACAT_NUCLEAR,   "Alpha Radiation",       "Observe alpha decay from a nucleus",                 "[a]",  "ACH_FIRST_ALPHA_DECAY"   },
    { ACH_FIRST_BETA_DECAY,    ACAT_NUCLEAR,   "Beta Radiation",        "Observe beta decay from a nucleus",                  "[b]",  "ACH_FIRST_BETA_DECAY"    },
    { ACH_CHAIN_REACTION,      ACAT_NUCLEAR,   "Chain Reaction",        "Trigger 3+ fission events within 1 second",          "[C]",  "ACH_CHAIN_REACTION"      },
    { ACH_FUSION_10,           ACAT_NUCLEAR,   "Stellar Nursery",       "Trigger 10 fusion reactions",                        "[*]",  "ACH_FUSION_10"           },
    { ACH_FUSION_100,          ACAT_NUCLEAR,   "Stellar Core",          "Trigger 100 fusion reactions",                       "[S]",  "ACH_FUSION_100"          },
    { ACH_FIRST_SPALLATION,    ACAT_NUCLEAR,   "Nuclear Demolition",    "Trigger nuclear spallation",                         "[!n]", "ACH_FIRST_SPALLATION"    },
    { ACH_FIRST_PHOTOELECTRIC, ACAT_NUCLEAR,   "Einstein's Nobel",      "Trigger the photoelectric effect",                   "[PE]", "ACH_FIRST_PHOTOELECTRIC" },
    { ACH_FIRST_PAIR_PRODUCTION,ACAT_NUCLEAR,  "Something from Nothing","Witness pair production",                            "[PP]", "ACH_FIRST_PAIR_PRODUCTION"},

    // ── Element Creation ────────────────────────────────────────────────────
    { ACH_FIRST_ELEMENT,       ACAT_ELEMENTS,  "Elemental",             "Create any element with bound electrons",             "[E]",  "ACH_FIRST_ELEMENT"       },
    { ACH_HYDROGEN,            ACAT_ELEMENTS,  "Simplest Atom",         "Create Hydrogen (Z=1)",                              "[H]",  "ACH_HYDROGEN"            },
    { ACH_HELIUM,              ACAT_ELEMENTS,  "Noble Gas",             "Create Helium (Z=2)",                                "[He]", "ACH_HELIUM"              },
    { ACH_LITHIUM,             ACAT_ELEMENTS,  "Third Element",         "Create Lithium (Z=3)",                               "[Li]", "ACH_LITHIUM"             },
    { ACH_CARBON,              ACAT_ELEMENTS,  "Basis of Life",         "Create Carbon (Z=6)",                                "[C]",  "ACH_CARBON"              },
    { ACH_OXYGEN,              ACAT_ELEMENTS,  "Breath of Life",        "Create Oxygen (Z=8)",                                "[O]",  "ACH_OXYGEN"              },
    { ACH_IRON,                ACAT_ELEMENTS,  "Iron Peak",             "Create Iron (Z=26) -- peak of binding energy",       "[Fe]", "ACH_IRON"                },
    { ACH_GOLD,                ACAT_ELEMENTS,  "Alchemist's Dream",     "Create Gold (Z=79)",                                 "[Au]", "ACH_GOLD"                },
    { ACH_URANIUM,             ACAT_ELEMENTS,  "Heavy Metal",           "Create Uranium (Z=92)",                              "[U]",  "ACH_URANIUM"             },
    { ACH_ELEMENTS_10,         ACAT_ELEMENTS,  "Periodic Table",        "Discover 10 distinct elements",                      "[PT]", "ACH_ELEMENTS_10"         },
    { ACH_ELEMENTS_25,         ACAT_ELEMENTS,  "Quarter Century",       "Discover 25 distinct elements",                      "[25]", "ACH_ELEMENTS_25"         },

    // ── Particle Zoo ────────────────────────────────────────────────────────
    { ACH_FIRST_POSITRON,      ACAT_PARTICLES, "Antimatter Twin",       "Create a positron",                                  "[e+]", "ACH_FIRST_POSITRON"      },
    { ACH_FIRST_NEUTRINO,      ACAT_PARTICLES, "Ghost Particle",        "Create a neutrino",                                  "[v]",  "ACH_FIRST_NEUTRINO"      },
    { ACH_FIRST_MUON,          ACAT_PARTICLES, "Who Ordered That?",     "Create a muon",                                      "[u]",  "ACH_FIRST_MUON"          },
    { ACH_FIRST_TAU,           ACAT_PARTICLES, "Third Generation",      "Create a tau lepton",                                "[T]",  "ACH_FIRST_TAU"           },
    { ACH_FIRST_ANTIPROTON,    ACAT_PARTICLES, "Mirror Proton",         "Create an antiproton",                               "[p-]", "ACH_FIRST_ANTIPROTON"    },
    { ACH_FIRST_QUARK,         ACAT_PARTICLES, "Asymptotic Freedom",    "Observe a free quark",                               "[q]",  "ACH_FIRST_QUARK"         },
    { ACH_FIRST_BOSON,         ACAT_PARTICLES, "Force Carrier",         "Create a W, Z, or Higgs boson",                      "[W]",  "ACH_FIRST_BOSON"         },
    { ACH_DARK_MATTER,         ACAT_PARTICLES, "Dark Side",             "Observe a dark matter particle",                     "[D]",  "ACH_DARK_MATTER"         },
    { ACH_PARTICLE_ZOO,        ACAT_PARTICLES, "The Standard Model",    "Have all 33 particle types present at once",         "[SM]", "ACH_PARTICLE_ZOO"        },

    // ── Thermodynamics ──────────────────────────────────────────────────────
    { ACH_TEMP_1000K,          ACAT_THERMO,    "Getting Warm",          "Reach 1,000 K",                                     "[1k]", "ACH_TEMP_1000K"          },
    { ACH_TEMP_1MK,            ACAT_THERMO,    "Stellar Temperature",   "Reach 1,000,000 K",                                 "[1M]", "ACH_TEMP_1MK"            },
    { ACH_TEMP_1GK,            ACAT_THERMO,    "Big Bang Nucleosynthesis","Reach 1,000,000,000 K",                            "[1G]", "ACH_TEMP_1GK"            },
    { ACH_TEMP_10GK,           ACAT_THERMO,    "Quark Epoch",           "Reach 10,000,000,000 K",                            "[10G]","ACH_TEMP_10GK"           },
    { ACH_ABSOLUTE_ZERO,       ACAT_THERMO,    "Absolute Zero",         "Cool the system below 2 K",                         "[0K]", "ACH_ABSOLUTE_ZERO"       },

    // ── Milestones ──────────────────────────────────────────────────────────
    { ACH_PARTICLES_1000,      ACAT_MILESTONES,"Busy Universe",         "Have 1,000+ active particles simultaneously",        "[1k]", "ACH_PARTICLES_1000"      },
    { ACH_PARTICLES_5000,      ACAT_MILESTONES,"Crowded Space",         "Have 5,000+ active particles simultaneously",        "[5k]", "ACH_PARTICLES_5000"      },
    { ACH_PARTICLES_10000,     ACAT_MILESTONES,"Particle Soup",         "Have 10,000+ active particles simultaneously",       "[10k]","ACH_PARTICLES_10000"     },
    { ACH_FIRST_ENTANGLED,     ACAT_MILESTONES,"Spooky Action",         "Create your first entangled pair",                   "[~~]", "ACH_FIRST_ENTANGLED"     },
    { ACH_ENTANGLED_10,        ACAT_MILESTONES,"Quantum Network",       "Have 10+ entangled pairs active at once",            "[QN]", "ACH_ENTANGLED_10"        },
    { ACH_FIRST_FORCE_OBJ,     ACAT_MILESTONES,"Field Engineer",        "Place your first force object",                      "[FO]", "ACH_FIRST_FORCE_OBJ"     },
    { ACH_FIRST_MIRROR,        ACAT_MILESTONES,"Mirror Mirror",         "Place a mirror",                                     "[||]", "ACH_FIRST_MIRROR"        },
    { ACH_FIRST_ACCELERATOR,   ACAT_MILESTONES,"CERN at Home",          "Fire the particle accelerator",                      "[>>]", "ACH_FIRST_ACCELERATOR"   },
    { ACH_FIRST_SAVE,          ACAT_MILESTONES,"Saved State",           "Save a simulation to disk",                          "[S]",  "ACH_FIRST_SAVE"          },
    { ACH_FIRST_LOAD,          ACAT_MILESTONES,"Time Travel",           "Load a simulation from disk",                        "[L]",  "ACH_FIRST_LOAD"          },
    { ACH_FIRST_EXPORT,        ACAT_MILESTONES,"Element Exporter",      "Export an element to a .ppel file",                  "[Ex]", "ACH_FIRST_EXPORT"        },
    { ACH_FIRST_IMPORT,        ACAT_MILESTONES,"Element Importer",      "Import an element from a .ppel file",                "[Im]", "ACH_FIRST_IMPORT"        },
    { ACH_ANNIHILATIONS_100,   ACAT_MILESTONES,"Annihilator",           "Achieve 100 total annihilations",                    "[!!]", "ACH_ANNIHILATIONS_100"   },
    { ACH_NUCLEAR_DECAYS_50,   ACAT_MILESTONES,"Radioactive",           "Witness 50 nuclear decay events",                    "[ND]", "ACH_NUCLEAR_DECAYS_50"   },
    { ACH_ANTIMATTER_ELEMENT,  ACAT_MILESTONES,"Antimatter Atom",       "Create an antimatter element",                       "[~E]", "ACH_ANTIMATTER_ELEMENT"  },
    { ACH_TRY_ALL_ENVIRONMENTS,ACAT_MILESTONES,"Explorer",              "Try all 12 environment presets",                     "[!]",  "ACH_TRY_ALL_ENVIRONMENTS"},

    // ── Chemistry & Bonds ──────────────────────────────────────────────────
    { ACH_FIRST_BOND,          ACAT_CHEMISTRY, "Chemical Bond",         "Form your first covalent bond",                      "[==]", "ACH_FIRST_BOND"          },
    { ACH_FIRST_MOLECULE,      ACAT_CHEMISTRY, "Molecular!",            "Create a molecule with bonded atoms",                "[M]",  "ACH_FIRST_MOLECULE"      },
    { ACH_MOLECULE_5_ATOMS,    ACAT_CHEMISTRY, "Complex Chemistry",     "Create a molecule with 5+ atoms",                    "[M5]", "ACH_MOLECULE_5_ATOMS"    },

    // ── Advanced Physics ──────────────────────────────────────────────────
    { ACH_FIRST_VIRTUAL_PAIR,  ACAT_PARTICLES, "Vacuum Fluctuation",    "Observe virtual particle pair creation",              "[VP]", "ACH_FIRST_VIRTUAL_PAIR"  },
    { ACH_FIRST_PION_DECAY,    ACAT_NUCLEAR,   "Pion Cascade",          "Observe pion decay",                                  "[pi]", "ACH_FIRST_PION_DECAY"    },
    { ACH_COSMIC_RAY,          ACAT_MILESTONES,"Cosmic Ray",            "Fire a particle at maximum speed",                    "[CR]", "ACH_COSMIC_RAY"          },
    { ACH_PHOTON_EMISSION,     ACAT_NUCLEAR,   "Let There Be Light",    "Observe photon emission from a decay",                "[hv]", "ACH_PHOTON_EMISSION"     },

    // ── Exploration ───────────────────────────────────────────────────────
    { ACH_FIRST_MOLECULE_EXPORT,ACAT_MILESTONES,"Molecule Exporter",    "Export a molecule to a .ppmol file",                  "[Mx]", "ACH_FIRST_MOLECULE_EXPORT"},
    { ACH_SPEED_DEMON,         ACAT_MILESTONES,"Speed Demon",           "Set simulation speed to maximum",                     "[>>]", "ACH_SPEED_DEMON"         },
    { ACH_HUNDRED_ELEMENTS,    ACAT_ELEMENTS,  "Centurion",             "Discover 100 distinct elements",                      "[100]","ACH_HUNDRED_ELEMENTS"    },
    { ACH_GRAVITON_OBSERVED,   ACAT_PARTICLES, "Gravity's Messenger",   "Observe a graviton particle",                         "[Gr]", "ACH_GRAVITON_OBSERVED"   },
    { ACH_LONG_PLAY,           ACAT_MILESTONES,"Dedicated Physicist",   "Run a simulation for 10+ minutes",                    "[10m]","ACH_LONG_PLAY"           },

    // ── Scenarios ────────────────────────────────────────────────────────
    { ACH_FIRST_SCENARIO_COMPLETE, ACAT_SCENARIOS, "First Steps",       "Complete any scenario",                                "[S1]", "ACH_FIRST_SCENARIO_COMPLETE" },
    { ACH_ALL_NUCLEAR_SCENARIOS,   ACAT_SCENARIOS, "Nuclear Physicist", "Complete all Nuclear scenarios",                        "[Nuc]","ACH_ALL_NUCLEAR_SCENARIOS"   },
    { ACH_ALL_CHEMISTRY_SCENARIOS, ACAT_SCENARIOS, "Chemist",           "Complete all Chemistry scenarios",                      "[Chm]","ACH_ALL_CHEMISTRY_SCENARIOS" },
    { ACH_ALL_COSMOLOGY_SCENARIOS, ACAT_SCENARIOS, "Cosmologist",       "Complete all Cosmology scenarios",                      "[Cos]","ACH_ALL_COSMOLOGY_SCENARIOS" },
    { ACH_SCENARIO_MASTER,         ACAT_SCENARIOS, "Scenario Master",   "Complete all non-sandbox scenarios",                    "[**]", "ACH_SCENARIO_MASTER"        },

    // ── Beyond Standard Model ────────────────────────────────────────────
    { ACH_DARK_ENERGY,    ACAT_PARTICLES, "Cosmic Accelerator",  "Observe a dark energy particle",                              "[DE]", "ACH_DARK_ENERGY"            },
    { ACH_SUSY_PARTICLE,  ACAT_PARTICLES, "Supersymmetry",       "Observe a supersymmetric particle",                           "[SY]", "ACH_SUSY_PARTICLE"          },
    { ACH_TACHYON,        ACAT_PARTICLES, "Faster Than Light",   "Observe a tachyon",                                           "[FTL]","ACH_TACHYON"                },

    // ── Advanced Nuclear ─────────────────────────────────────────────────
    { ACH_TRANSURANIC,    ACAT_ELEMENTS,  "Beyond Uranium",      "Create a transuranic element (Z > 92)",                       "[Pu]", "ACH_TRANSURANIC"            },
    { ACH_SUPERHEAVY,     ACAT_ELEMENTS,  "Island of Stability", "Create a superheavy element (Z > 110)",                       "[SH]", "ACH_SUPERHEAVY"             },
    { ACH_FISSION_100,    ACAT_NUCLEAR,   "Reactor Core",        "Trigger 100 fission events",                                  "[R]",  "ACH_FISSION_100"            },
    { ACH_FUSION_1000,    ACAT_NUCLEAR,   "Star Builder",        "Trigger 1000 fusion events",                                  "[FB]", "ACH_FUSION_1000"            },

    // ── Advanced Chemistry ───────────────────────────────────────────────
    { ACH_MOLECULES_10,      ACAT_CHEMISTRY, "Molecular Library",  "Create 10 distinct molecules",                              "[M10]","ACH_MOLECULES_10"           },
    { ACH_MOLECULE_10_ATOMS, ACAT_CHEMISTRY, "Macromolecule",      "Create a molecule with 10+ atoms",                           "[MA]", "ACH_MOLECULE_10_ATOMS"      },

    // ── Advanced Thermodynamics ──────────────────────────────────────────
    { ACH_TEMP_100GK,     ACAT_THERMO,    "Planck Epoch",        "Reach 100,000,000,000 K",                                    "[100G]","ACH_TEMP_100GK"            },

    // ── Advanced Milestones ──────────────────────────────────────────────
    { ACH_PARTICLES_50000, ACAT_MILESTONES,"Galaxy",              "Have 50,000+ active particles simultaneously",               "[50k]","ACH_PARTICLES_50000"        },
    { ACH_ENTANGLED_50,    ACAT_MILESTONES,"Quantum Fabric",      "Have 50+ entangled pairs active simultaneously",             "[Q50]","ACH_ENTANGLED_50"           },
    { ACH_FORCE_OBJECTS_5, ACAT_MILESTONES,"Field Architect",     "Place 5 force objects",                                      "[F5]", "ACH_FORCE_OBJECTS_5"        },
    { ACH_ACCELERATOR_50,  ACAT_MILESTONES,"Collider Veteran",    "Fire the particle accelerator 50 times",                     "[A50]","ACH_ACCELERATOR_50"         },
    { ACH_PLAY_1_HOUR,     ACAT_MILESTONES,"Marathon Physicist",   "Run a simulation for 1+ hour",                              "[1h]", "ACH_PLAY_1_HOUR"            },

    // ── Quasiparticles ──────────────────────────────────────────────
    { ACH_FIRST_PLASMON,       ACAT_PARTICLES, "Plasma Wave",       "Observe a plasmon",                                        "[Pl]", "ACH_FIRST_PLASMON"          },
    { ACH_FIRST_PHONON,        ACAT_PARTICLES, "Good Vibrations",   "Observe a phonon",                                         "[Ph]", "ACH_FIRST_PHONON"           },
    { ACH_FIRST_MAGNON,        ACAT_PARTICLES, "Spin Wave",         "Observe a magnon",                                         "[Mg]", "ACH_FIRST_MAGNON"           },
    { ACH_FIRST_POLARON,       ACAT_PARTICLES, "Dressed Electron",  "Observe a polaron",                                        "[Po]", "ACH_FIRST_POLARON"          },
    { ACH_FIRST_COOPER_PAIR,   ACAT_PARTICLES, "Superconductor",    "Observe a Cooper pair",                                    "[CP]", "ACH_FIRST_COOPER_PAIR"      },
    { ACH_FIRST_ROTON,         ACAT_PARTICLES, "Quantum Vortex",    "Observe a roton",                                          "[Ro]", "ACH_FIRST_ROTON"            },
    { ACH_FIRST_ELECTRON_HOLE, ACAT_PARTICLES, "Missing Electron",  "Observe an electron hole",                                 "[h+]", "ACH_FIRST_ELECTRON_HOLE"    },

    // ── Advanced Processes ──────────────────────────────────────────
    { ACH_FIRST_BREMSSTRAHLUNG,      ACAT_NUCLEAR,   "Braking Radiation",  "Observe bremsstrahlung radiation",                  "[Br]", "ACH_FIRST_BREMSSTRAHLUNG"       },
    { ACH_FIRST_HADRONIZATION,       ACAT_NUCLEAR,   "Quark Confinement",  "Observe quark hadronization",                       "[Hd]", "ACH_FIRST_HADRONIZATION"        },
    { ACH_FIRST_CARRIER_EXCHANGE,    ACAT_PARTICLES, "Force Mediator",     "Observe a force carrier exchange",                  "[Fc]", "ACH_FIRST_CARRIER_EXCHANGE"     },
    { ACH_FIRST_NEUTRINO_OSCILLATION,ACAT_PARTICLES, "Flavor Change",      "Observe neutrino flavor oscillation",               "[Nv]", "ACH_FIRST_NEUTRINO_OSCILLATION" },
    { ACH_FIRST_SHELL_TRANSITION,    ACAT_NUCLEAR,   "Quantum Leap",       "Observe an electron shell transition",              "[QL]", "ACH_FIRST_SHELL_TRANSITION"     },
    { ACH_FIRST_RECOMBINATION,       ACAT_NUCLEAR,   "Recombination",      "Observe electron-hole recombination",               "[Rc]", "ACH_FIRST_RECOMBINATION"        },
    { ACH_FIRST_MESON_DECAY,         ACAT_NUCLEAR,   "Meson Unstable",     "Observe a meson decay",                             "[Md]", "ACH_FIRST_MESON_DECAY"          },
    { ACH_FIRST_WEAK_DECAY,          ACAT_NUCLEAR,   "Weak Force",         "Observe a weak flavor change",                      "[Wk]", "ACH_FIRST_WEAK_DECAY"           },

    // ── More Elements ───────────────────────────────────────────────
    { ACH_NITROGEN,       ACAT_ELEMENTS,  "The Air We Breathe",  "Create Nitrogen (Z=7)",                                      "[N]",  "ACH_NITROGEN"                   },
    { ACH_NEON,           ACAT_ELEMENTS,  "Bright Lights",       "Create Neon (Z=10)",                                         "[Ne]", "ACH_NEON"                       },
    { ACH_SILICON,        ACAT_ELEMENTS,  "Silicon Valley",      "Create Silicon (Z=14)",                                      "[Si]", "ACH_SILICON"                    },
    { ACH_ELEMENTS_50,    ACAT_ELEMENTS,  "Half the Table",      "Discover 50 distinct elements",                              "[50]", "ACH_ELEMENTS_50"                },

    // ── Specific Molecules ──────────────────────────────────────────
    { ACH_WATER,          ACAT_CHEMISTRY, "Universal Solvent",   "Create water (H2O)",                                         "[W]",  "ACH_WATER"                      },
    { ACH_METHANE,        ACAT_CHEMISTRY, "Greenhouse Gas",      "Create methane (CH4)",                                       "[CH4]","ACH_METHANE"                    },
    { ACH_AMMONIA,        ACAT_CHEMISTRY, "Pungent",             "Create ammonia (NH3)",                                       "[NH3]","ACH_AMMONIA"                    },
    { ACH_MOLECULES_25,   ACAT_CHEMISTRY, "Chemistry Library",   "Discover 25 distinct molecules",                             "[M25]","ACH_MOLECULES_25"               },

    // ── Higher Thresholds ───────────────────────────────────────────
    { ACH_ANNIHILATIONS_500, ACAT_MILESTONES, "Matter Eraser",     "Achieve 500 total annihilations",                          "[500]","ACH_ANNIHILATIONS_500"           },
    { ACH_NUCLEAR_DECAYS_200,ACAT_MILESTONES, "Radiation Zone",    "Witness 200 nuclear decay events",                         "[200]","ACH_NUCLEAR_DECAYS_200"          },
    { ACH_FUSIONS_5000,      ACAT_NUCLEAR,    "Stellar Furnace",   "Trigger 5000 fusion events",                               "[5k]", "ACH_FUSIONS_5000"               },
    { ACH_BONDS_50,          ACAT_CHEMISTRY,  "Bond Builder",      "Form 50 total covalent bonds",                             "[B50]","ACH_BONDS_50"                   },

    // ── BSM Extras ──────────────────────────────────────────────────
    { ACH_FIRST_MONOPOLE,    ACAT_PARTICLES, "Dirac's Dream",      "Observe a magnetic monopole",                              "[MP]", "ACH_FIRST_MONOPOLE"             },
    { ACH_FIRST_NEUTRALINO,  ACAT_PARTICLES, "Dark Candidate",     "Observe a neutralino",                                     "[N1]", "ACH_FIRST_NEUTRALINO"           },
    { ACH_FIRST_GRAVITINO,   ACAT_PARTICLES, "SUSY Graviton",      "Observe a gravitino",                                      "[G~]", "ACH_FIRST_GRAVITINO"            },

    // ── Cutscenes & Lore ────────────────────────────────────────────
    { ACH_FIRST_CUTSCENE,      ACAT_MILESTONES, "Storyteller",     "Watch your first cutscene",                                "[Cs]", "ACH_FIRST_CUTSCENE"             },
    { ACH_ALL_INTROS_WATCHED,  ACAT_MILESTONES, "Lore Master",     "Watch all intro cutscenes",                                "[LM]", "ACH_ALL_INTROS_WATCHED"         },

    // ── Chirality ───────────────────────────────────────────────────
    { ACH_FIRST_CHIRAL_MOLECULE, ACAT_CHEMISTRY, "Mirror Molecule",  "Create a molecule with a chiral center",                   "[Ch]", "ACH_FIRST_CHIRAL_MOLECULE"      },
    { ACH_CHIRAL_MOLECULES_5,    ACAT_CHEMISTRY, "Stereochemist",    "Discover 5 distinct chiral molecules",                     "[C5]", "ACH_CHIRAL_MOLECULES_5"         },
    { ACH_PARITY_VIOLATION,      ACAT_NUCLEAR,   "Broken Mirror",    "Observe parity violation in a weak decay",                 "[PV]", "ACH_PARITY_VIOLATION"           },
    { ACH_HOMOCHIRAL,            ACAT_CHEMISTRY, "Homochiral World", "Have 10+ chiral molecules simultaneously",                 "[HC]", "ACH_HOMOCHIRAL"                 },
    { ACH_MIRROR_IMAGE,          ACAT_CHEMISTRY, "Enantiomer",       "Create both enantiomers of a chiral molecule",             "[LR]", "ACH_MIRROR_IMAGE"               },

    // ── CP Violation ───────────────────────────────────────────────────
    { ACH_FIRST_MESON_OSCILLATION,     ACAT_PARTICLES, "Quantum Mixing",   "Observe neutral meson oscillation",                        "[QM]", "ACH_FIRST_MESON_OSCILLATION"    },
    { ACH_FIRST_CP_VIOLATION,          ACAT_NUCLEAR,   "Broken Symmetry",  "Observe CP violation in meson decay",                      "[CP]", "ACH_FIRST_CP_VIOLATION"         },
    { ACH_MATTER_ANTIMATTER_ASYMMETRY, ACAT_MILESTONES,"Why We Exist",     "Accumulate matter-antimatter asymmetry from CP violation",  "[WE]", "ACH_MATTER_ANTIMATTER_ASYMMETRY"},
};

// ── Runtime initialization for element achievements ─────────────────────────

// Static storage for element achievement strings (must outlive ACHIEVEMENT_DEFS)
static char s_element_names[118][64];
static char s_element_descs[118][80];
static char s_element_icons[118][8];
static char s_element_steam[118][32];

void init_achievement_defs() {
    for (int z = 1; z <= 118; ++z) {
        int idx = z - 1;  // 0-based index into element arrays
        AchievementID aid = static_cast<AchievementID>(ACH_ELEMENT_BASE + idx);

        snprintf(s_element_names[idx], sizeof(s_element_names[idx]),
                 "Discover %s", ELEMENT_NAMES[z]);
        snprintf(s_element_descs[idx], sizeof(s_element_descs[idx]),
                 "Create %s (Z=%d)", ELEMENT_NAMES[z], z);
        snprintf(s_element_icons[idx], sizeof(s_element_icons[idx]),
                 "[%s]", ELEMENT_SYMBOLS[z]);
        snprintf(s_element_steam[idx], sizeof(s_element_steam[idx]),
                 "ACH_ELEMENT_%d", z);

        ACHIEVEMENT_DEFS[aid] = {
            aid,
            ACAT_PERIODIC_TABLE,
            s_element_names[idx],
            s_element_descs[idx],
            s_element_icons[idx],
            s_element_steam[idx]
        };
    }
}

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
    int slot = id / 64;
    uint64_t bit = 1ULL << (id % 64);
    if (unlocked_bits_[slot] & bit) return false;  // already unlocked
    unlocked_bits_[slot] |= bit;
    return true;
}

bool AchievementManager::is_unlocked(AchievementID id) const {
    if (id >= ACH_COUNT) return false;
    return (unlocked_bits_[id / 64] & (1ULL << (id % 64))) != 0;
}

int AchievementManager::unlocked_count() const {
    int count = 0;
    for (uint32_t s = 0; s < ACH_ELEMENT_SLOTS; s++) {
        uint64_t bits = unlocked_bits_[s];
        while (bits) {
            count += (bits & 1);
            bits >>= 1;
        }
    }
    return count;
}

// ── Persistence (.ppach file) ───────────────────────────────────────────────

static constexpr uint32_t PPACH_MAGIC   = 0x48434150;  // "PACH" little-endian
static constexpr uint32_t PPACH_VERSION = 6;

bool AchievementManager::save(const std::string& filepath) const {
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    f.write(reinterpret_cast<const char*>(&PPACH_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPACH_VERSION), sizeof(uint32_t));

    // v4: four uint64_t bitfield slots (256 achievement capacity)
    for (uint32_t s = 0; s < ACH_ELEMENT_SLOTS; s++)
        f.write(reinterpret_cast<const char*>(&unlocked_bits_[s]), sizeof(uint64_t));

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

    // v2: advanced counters
    f.write(reinterpret_cast<const char*>(&total_accelerator_fires), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_force_objects_placed), sizeof(uint32_t));

    // v2: scenario completion
    f.write(reinterpret_cast<const char*>(scenarios_completed), sizeof(scenarios_completed));

    // v3: bonds counter, cutscene tracking, observation flags
    f.write(reinterpret_cast<const char*>(&total_bonds_formed), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&cutscene_intros_seen), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&seen_plasmon), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_phonon), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_magnon), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_polaron), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_cooper_pair), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_roton), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_electron_hole), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_bremsstrahlung), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_hadronization), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_carrier_exchange), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_neutrino_oscillation), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_shell_transition), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_recombination), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_meson_decay), sizeof(bool));
    f.write(reinterpret_cast<const char*>(&seen_weak_decay), sizeof(bool));

    // v5: process counters
    f.write(reinterpret_cast<const char*>(&total_spallations), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_photoelectric), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_pair_productions), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_virtual_pairs), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_carrier_exchanges), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_shell_transitions), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_meson_decays), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_bremsstrahlung), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_neutrino_oscillations), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_molecules_formed), sizeof(uint32_t));

    // v6: chirality counters
    f.write(reinterpret_cast<const char*>(&total_left_handed_weak_decays), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_chiral_molecules_found), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&seen_parity_violation), sizeof(bool));

    return f.good();
}

bool AchievementManager::load(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPACH_MAGIC || version < 1 || version > 6)
        return false;

    // Clear all bitfield slots first
    for (uint32_t s = 0; s < ACH_ELEMENT_SLOTS; s++)
        unlocked_bits_[s] = 0;

    if (version == 1) {
        // v1: single uint64_t bitfield
        f.read(reinterpret_cast<char*>(&unlocked_bits_[0]), sizeof(uint64_t));
    } else if (version <= 3) {
        // v2-v3: two uint64_t bitfield slots
        f.read(reinterpret_cast<char*>(&unlocked_bits_[0]), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&unlocked_bits_[1]), sizeof(uint64_t));
    } else {
        // v4+: four uint64_t bitfield slots
        for (uint32_t s = 0; s < ACH_ELEMENT_SLOTS; s++)
            f.read(reinterpret_cast<char*>(&unlocked_bits_[s]), sizeof(uint64_t));
    }

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

    if (version >= 2) {
        // v2: advanced counters
        f.read(reinterpret_cast<char*>(&total_accelerator_fires), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_force_objects_placed), sizeof(uint32_t));
        // v2: scenario completion
        f.read(reinterpret_cast<char*>(scenarios_completed), sizeof(scenarios_completed));
    }

    if (version >= 3) {
        // v3: bonds counter, cutscene tracking, observation flags
        f.read(reinterpret_cast<char*>(&total_bonds_formed), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&cutscene_intros_seen), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&seen_plasmon), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_phonon), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_magnon), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_polaron), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_cooper_pair), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_roton), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_electron_hole), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_bremsstrahlung), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_hadronization), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_carrier_exchange), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_neutrino_oscillation), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_shell_transition), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_recombination), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_meson_decay), sizeof(bool));
        f.read(reinterpret_cast<char*>(&seen_weak_decay), sizeof(bool));
    }

    if (version >= 5) {
        // v5: process counters
        f.read(reinterpret_cast<char*>(&total_spallations), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_photoelectric), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_pair_productions), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_virtual_pairs), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_carrier_exchanges), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_shell_transitions), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_meson_decays), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_bremsstrahlung), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_neutrino_oscillations), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_molecules_formed), sizeof(uint32_t));
    }

    if (version >= 6) {
        // v6: chirality counters
        f.read(reinterpret_cast<char*>(&total_left_handed_weak_decays), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&total_chiral_molecules_found), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&seen_parity_violation), sizeof(bool));
    }

    return f.good();
}

// ── LifetimeStats persistence (.ppstats file) ──────────────────────────────

static constexpr uint32_t PPST_MAGIC   = 0x54535050;  // "PPST" little-endian
static constexpr uint32_t PPST_VERSION = 3;

bool LifetimeStats::save(const std::string& filepath) const {
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    f.write(reinterpret_cast<const char*>(&PPST_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&PPST_VERSION), sizeof(uint32_t));

    // Global totals
    f.write(reinterpret_cast<const char*>(&total_ticks), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_play_time), sizeof(double));
    f.write(reinterpret_cast<const char*>(&total_simulations), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&total_fusions), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_fissions), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_annihilations), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_decays), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_bonds_formed), sizeof(uint64_t));

    // Per-particle-type arrays
    f.write(reinterpret_cast<const char*>(particles_spawned), sizeof(particles_spawned));
    f.write(reinterpret_cast<const char*>(particles_peak), sizeof(particles_peak));

    // Per-element arrays
    f.write(reinterpret_cast<const char*>(elements_created), sizeof(elements_created));
    f.write(reinterpret_cast<const char*>(elements_peak), sizeof(elements_peak));

    // Molecules
    f.write(reinterpret_cast<const char*>(&total_molecules_discovered), sizeof(uint32_t));

    // All-time peaks
    f.write(reinterpret_cast<const char*>(&peak_temperature), sizeof(float));
    f.write(reinterpret_cast<const char*>(&peak_particles), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&peak_entangled), sizeof(uint32_t));

    // v2: extended reaction counters
    f.write(reinterpret_cast<const char*>(&total_spallations), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_photoelectric), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_pair_productions), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_virtual_pairs), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_carrier_exchanges), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_shell_transitions), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_meson_decays), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_bremsstrahlung), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_neutrino_oscillations), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_accelerator_fires), sizeof(uint64_t));

    // v2: particle aggregate stats
    f.write(reinterpret_cast<const char*>(&total_particles_spawned), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&distinct_particle_types_seen), sizeof(uint32_t));

    // v2: element stats
    f.write(reinterpret_cast<const char*>(&highest_z_created), sizeof(uint32_t));

    // v2: molecule stats
    f.write(reinterpret_cast<const char*>(&total_molecules_formed), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&largest_molecule_atoms), sizeof(uint32_t));

    // v2: gameplay stats
    f.write(reinterpret_cast<const char*>(&total_scenarios_completed), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&environments_explored), sizeof(uint32_t));

    // v3: chirality stats
    f.write(reinterpret_cast<const char*>(&total_chiral_molecules_found), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&total_left_handed_weak_decays), sizeof(uint64_t));
    f.write(reinterpret_cast<const char*>(&distinct_chiral_formulas), sizeof(uint32_t));

    return f.good();
}

bool LifetimeStats::load(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) return false;

    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    if (magic != PPST_MAGIC || version < 1 || version > 3)
        return false;

    // Global totals
    f.read(reinterpret_cast<char*>(&total_ticks), sizeof(uint64_t));
    f.read(reinterpret_cast<char*>(&total_play_time), sizeof(double));
    f.read(reinterpret_cast<char*>(&total_simulations), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&total_fusions), sizeof(uint64_t));
    f.read(reinterpret_cast<char*>(&total_fissions), sizeof(uint64_t));
    f.read(reinterpret_cast<char*>(&total_annihilations), sizeof(uint64_t));
    f.read(reinterpret_cast<char*>(&total_decays), sizeof(uint64_t));
    f.read(reinterpret_cast<char*>(&total_bonds_formed), sizeof(uint64_t));

    // Per-particle-type arrays
    f.read(reinterpret_cast<char*>(particles_spawned), sizeof(particles_spawned));
    f.read(reinterpret_cast<char*>(particles_peak), sizeof(particles_peak));

    // Per-element arrays
    f.read(reinterpret_cast<char*>(elements_created), sizeof(elements_created));
    f.read(reinterpret_cast<char*>(elements_peak), sizeof(elements_peak));

    // Molecules
    f.read(reinterpret_cast<char*>(&total_molecules_discovered), sizeof(uint32_t));

    // All-time peaks
    f.read(reinterpret_cast<char*>(&peak_temperature), sizeof(float));
    f.read(reinterpret_cast<char*>(&peak_particles), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&peak_entangled), sizeof(uint32_t));

    if (version >= 2) {
        // v2: extended reaction counters
        f.read(reinterpret_cast<char*>(&total_spallations), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_photoelectric), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_pair_productions), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_virtual_pairs), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_carrier_exchanges), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_shell_transitions), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_meson_decays), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_bremsstrahlung), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_neutrino_oscillations), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_accelerator_fires), sizeof(uint64_t));

        // v2: particle aggregate stats
        f.read(reinterpret_cast<char*>(&total_particles_spawned), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&distinct_particle_types_seen), sizeof(uint32_t));

        // v2: element stats
        f.read(reinterpret_cast<char*>(&highest_z_created), sizeof(uint32_t));

        // v2: molecule stats
        f.read(reinterpret_cast<char*>(&total_molecules_formed), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&largest_molecule_atoms), sizeof(uint32_t));

        // v2: gameplay stats
        f.read(reinterpret_cast<char*>(&total_scenarios_completed), sizeof(uint32_t));
        f.read(reinterpret_cast<char*>(&environments_explored), sizeof(uint32_t));
    }

    if (version >= 3) {
        // v3: chirality stats
        f.read(reinterpret_cast<char*>(&total_chiral_molecules_found), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&total_left_handed_weak_decays), sizeof(uint64_t));
        f.read(reinterpret_cast<char*>(&distinct_chiral_formulas), sizeof(uint32_t));
    }

    return f.good();
}
