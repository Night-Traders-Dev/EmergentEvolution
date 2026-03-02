#include "physics/cutscenes.h"

// IM_COL32 macro for standalone compilation (matches imgui.h)
#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((uint32_t)(A)<<24)|((uint32_t)(B)<<16)|((uint32_t)(G)<<8)|((uint32_t)(R)))
#endif

// ══════════════════════════════════════════════════════════════════════════════
// ── Intro Cutscene Text Lines ────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

static const CutsceneTextLine INTRO_FIRST_LIGHT[] = {
    { "In the void before creation, there was nothing.", 2.5f },
    { "No light. No matter. Only the quantum vacuum, seething with possibility.", 3.0f },
    { "From this emptiness, the first particles emerge...", 2.5f },
};

static const CutsceneTextLine INTRO_HYDROGEN_FACTORY[] = {
    { "The simplest atom. One proton. One electron.", 2.5f },
    { "Yet from this humble beginning, everything is built.", 2.5f },
    { "Build the foundation of the periodic table.", 2.5f },
};

static const CutsceneTextLine INTRO_SOLAR_CORE[] = {
    { "Deep inside a star, gravity crushes hydrogen to impossible density.", 2.5f },
    { "At fifteen million degrees, atoms can no longer resist.", 2.5f },
    { "The nuclear furnace awaits ignition.", 2.5f },
};

static const CutsceneTextLine INTRO_CHAIN_REACTION[] = {
    { "Heavy nuclei are delicately balanced, teetering on the edge of stability.", 2.5f },
    { "One neutron. That's all it takes to shatter this balance.", 2.5f },
    { "And each shattered nucleus releases more neutrons...", 2.5f },
};

static const CutsceneTextLine INTRO_ANTIMATTER_LAB[] = {
    { "For every particle in the universe, there exists a perfect mirror.", 2.5f },
    { "Same mass. Opposite charge. Opposite fate.", 2.5f },
    { "When they meet... annihilation.", 2.5f },
};

static const CutsceneTextLine INTRO_WATER_WORLD[] = {
    { "Atoms don't merely collide \xe2\x80\x94 they share.", 2.5f },
    { "Electrons bridge the gap between nuclei, creating bonds.", 2.5f },
    { "From these bonds, molecules emerge \xe2\x80\x94 and with them, chemistry.", 3.0f },
};

static const CutsceneTextLine INTRO_CHEMISTRY_SET[] = {
    { "Each element has its own personality \xe2\x80\x94 its own bonding preferences.", 2.5f },
    { "Carbon reaches out four arms. Oxygen, two. Nitrogen, three.", 2.5f },
    { "The molecular workshop is open.", 2.5f },
};

static const CutsceneTextLine INTRO_DARK_SECTOR[] = {
    { "Look at the night sky. Everything you see is only 5% of the universe.", 3.0f },
    { "The rest is invisible. Dark matter. Dark energy.", 2.5f },
    { "Welcome to the other 95%.", 2.5f },
};

static const CutsceneTextLine INTRO_PARTICLE_ZOO[] = {
    { "The universe is built from a surprisingly small cast of characters.", 2.5f },
    { "Six quarks. Six leptons. Force carriers. One Higgs.", 2.5f },
    { "Discover the menagerie of fundamental particles.", 2.5f },
};

static const CutsceneTextLine INTRO_NUCLEOSYNTHESIS[] = {
    { "Stars are alchemists. They transmute hydrogen into heavier elements.", 2.5f },
    { "Helium. Carbon. Oxygen. Silicon. Iron.", 2.5f },
    { "Every atom in your body was forged in a stellar furnace.", 3.0f },
};

static const CutsceneTextLine INTRO_QUARK_SOUP[] = {
    { "One microsecond after the Big Bang.", 2.5f },
    { "The universe is a seething plasma of quarks and gluons.", 2.5f },
    { "Protons and neutrons cannot yet exist. Matter is in its most primal form.", 3.0f },
};

static const CutsceneTextLine INTRO_COSMIC_DAWN[] = {
    { "The Big Bang. All of space, time, and energy compressed to a point.", 2.5f },
    { "An unimaginable explosion expands the cosmos outward.", 2.5f },
    { "From fire to first light \xe2\x80\x94 the universe takes shape.", 2.5f },
};

static const CutsceneTextLine INTRO_NEUTRON_STAR[] = {
    { "When a massive star dies, its core collapses to a city-sized sphere.", 2.5f },
    { "Density beyond comprehension \xe2\x80\x94 a sugar cube weighs a billion tons.", 3.0f },
    { "Welcome to the heart of a dead star.", 2.5f },
};

static const CutsceneTextLine INTRO_MAGNETAR[] = {
    { "A neutron star, but more extreme \xe2\x80\x94 a quadrillion times Earth's magnetic field.", 3.0f },
    { "Strong enough to rearrange atomic orbitals from half a galaxy away.", 2.5f },
    { "The most magnetic object in the cosmos.", 2.5f },
};

static const CutsceneTextLine INTRO_SUSY_WORLD[] = {
    { "The Standard Model is beautiful, but incomplete.", 2.5f },
    { "Supersymmetry proposes a mirror: every fermion has a boson partner.", 2.5f },
    { "Step beyond the known. Enter the SUSY sector.", 2.5f },
};

static const CutsceneTextLine INTRO_THE_COLLIDER[] = {
    { "Humanity's most powerful microscope \xe2\x80\x94 the particle accelerator.", 2.5f },
    { "Two beams, traveling at 99.999999% the speed of light, collide head-on.", 3.0f },
    { "In the debris, nature reveals its deepest secrets.", 2.5f },
};

// ══════════════════════════════════════════════════════════════════════════════
// ── Completion Cutscene Text Lines ───────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

static const CutsceneTextLine COMP_FIRST_LIGHT[] = {
    { "A photon \xe2\x80\x94 a single quantum of light \xe2\x80\x94 bursts forth.", 2.5f },
    { "The darkness breaks. The universe can finally see itself.", 2.5f },
    { "This is how it all began.", 2.5f },
};

static const CutsceneTextLine COMP_HYDROGEN_FACTORY[] = {
    { "Hydrogen fills the cosmos \xe2\x80\x94 75% of all visible matter.", 2.5f },
    { "Each atom a tiny solar system, electron orbiting proton.", 2.5f },
    { "The raw material of stars is ready.", 2.5f },
};

static const CutsceneTextLine COMP_SOLAR_CORE[] = {
    { "Fusion \xe2\x80\x94 the power source of every star in the universe.", 2.5f },
    { "Four hydrogen nuclei become one helium, releasing pure energy.", 2.5f },
    { "A star is born.", 2.5f },
};

static const CutsceneTextLine COMP_CHAIN_REACTION[] = {
    { "The chain reaction cascades \xe2\x80\x94 each split triggering more.", 2.5f },
    { "You have unlocked the atom's most terrifying power.", 2.5f },
    { "Energy beyond imagination, from the heart of matter itself.", 2.5f },
};

static const CutsceneTextLine COMP_ANTIMATTER_LAB[] = {
    { "E = mc\xc2\xb2. Matter and antimatter convert entirely to energy.", 2.5f },
    { "The most efficient reaction in physics \xe2\x80\x94 100% mass-to-energy conversion.", 3.0f },
    { "Why the universe chose matter over antimatter remains its deepest mystery.", 3.0f },
};

static const CutsceneTextLine COMP_WATER_WORLD[] = {
    { "H\xe2\x82\x82O. Two hydrogens, one oxygen, bent at 104.5 degrees.", 2.5f },
    { "This simple molecule made life possible.", 2.5f },
    { "Water \xe2\x80\x94 the universal solvent, the cradle of biology.", 2.5f },
};

static const CutsceneTextLine COMP_CHEMISTRY_SET[] = {
    { "Molecules of increasing complexity fill the void.", 2.5f },
    { "From simple gases to the intricate machinery of life.", 2.5f },
    { "You've taken the first steps into organic chemistry.", 2.5f },
};

static const CutsceneTextLine COMP_DARK_SECTOR[] = {
    { "Dark matter clusters in vast halos, shaping galaxies from the shadows.", 2.5f },
    { "Dark energy pushes the cosmos apart, accelerating forever.", 2.5f },
    { "The invisible universe governs the visible one.", 2.5f },
};

static const CutsceneTextLine COMP_PARTICLE_ZOO[] = {
    { "The Standard Model \xe2\x80\x94 humanity's most precise theory of nature.", 2.5f },
    { "Every particle catalogued. Every force explained.", 2.5f },
    { "And yet, mysteries remain. Gravity. Dark matter. Why these particles and no others?", 3.0f },
};

static const CutsceneTextLine COMP_NUCLEOSYNTHESIS[] = {
    { "Iron \xe2\x80\x94 element 26 \xe2\x80\x94 the end of the line for stellar fusion.", 2.5f },
    { "Beyond iron, fusion costs more energy than it releases.", 2.5f },
    { "For heavier elements, the universe needs something more violent \xe2\x80\x94 a supernova.", 3.0f },
};

static const CutsceneTextLine COMP_QUARK_SOUP[] = {
    { "As the plasma cools, quarks are imprisoned \xe2\x80\x94 confined within hadrons forever.", 3.0f },
    { "The strong force strings stretch, then snap, creating new quark pairs.", 2.5f },
    { "Confinement. The universe's first phase transition.", 2.5f },
};

static const CutsceneTextLine COMP_COSMIC_DAWN[] = {
    { "380,000 years after the Big Bang, atoms finally form.", 2.5f },
    { "Photons, once trapped in the plasma, stream free across the universe.", 2.5f },
    { "The cosmic microwave background \xe2\x80\x94 the oldest light in existence \xe2\x80\x94 is born.", 3.0f },
};

static const CutsceneTextLine COMP_NEUTRON_STAR[] = {
    { "Superfluid neutrons flow without friction. Cooper pairs waltz in quantum unison.", 3.0f },
    { "Phonons vibrate through the nuclear lattice. Rotons spin like quantum whirlpools.", 3.0f },
    { "The densest laboratory in the universe teems with quantum phenomena.", 2.5f },
};

static const CutsceneTextLine COMP_MAGNETAR[] = {
    { "Magnons \xe2\x80\x94 quantized spin waves \xe2\x80\x94 ripple through the magnetized matter.", 3.0f },
    { "Phonons, Cooper pairs, polarons \xe2\x80\x94 a zoo of quasiparticles thrives.", 2.5f },
    { "In extremity, new physics emerges.", 2.5f },
};

static const CutsceneTextLine COMP_SUSY_WORLD[] = {
    { "Selectrons, squarks, gluinos \xe2\x80\x94 the supersymmetric partners revealed.", 2.5f },
    { "The neutralino, stable and invisible, may be dark matter itself.", 2.5f },
    { "If SUSY exists, it would unify the forces and solve the hierarchy problem.", 3.0f },
};

static const CutsceneTextLine COMP_THE_COLLIDER[] = {
    { "Muons, pions, kaons \xe2\x80\x94 exotic particles pour from the collision point.", 2.5f },
    { "The Higgs boson, last piece of the Standard Model, discovered in the wreckage.", 3.0f },
    { "Every collision asks the same question: what else is out there?", 2.5f },
};

// ══════════════════════════════════════════════════════════════════════════════
// ── Cutscene Definition Arrays ───────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

#define CS_LINES(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))

// Empty stub for sandbox scenarios
static const CutsceneTextLine STUB_EMPTY[] = { { "", 0.0f } };

const CutsceneDef CUTSCENE_INTROS[18] = {
    // 0: First Light
    { "The Dawn of Stars", CS_NEBULA,
      IM_COL32(60, 100, 200, 255), IM_COL32(0, 200, 255, 180),
      IM_COL32(10, 20, 60, 60),
      CS_LINES(INTRO_FIRST_LIGHT) },

    // 1: Hydrogen Factory
    { "Building the Periodic Table", CS_NEBULA,
      IM_COL32(80, 140, 255, 255), IM_COL32(200, 220, 255, 200),
      IM_COL32(15, 25, 70, 50),
      CS_LINES(INTRO_HYDROGEN_FACTORY) },

    // 2: Solar Core
    { "Heart of a Star", CS_NUCLEUS,
      IM_COL32(255, 160, 40, 255), IM_COL32(255, 220, 80, 200),
      IM_COL32(60, 30, 10, 60),
      CS_LINES(INTRO_SOLAR_CORE) },

    // 3: Chain Reaction
    { "Critical Mass", CS_NUCLEUS,
      IM_COL32(60, 200, 80, 255), IM_COL32(255, 80, 60, 180),
      IM_COL32(20, 40, 15, 50),
      CS_LINES(INTRO_CHAIN_REACTION) },

    // 4: Antimatter Lab
    { "Through the Looking Glass", CS_COLLISION,
      IM_COL32(0, 220, 255, 255), IM_COL32(255, 60, 200, 200),
      IM_COL32(30, 10, 50, 50),
      CS_LINES(INTRO_ANTIMATTER_LAB) },

    // 5: Water World
    { "Recipe for Life", CS_NEBULA,
      IM_COL32(60, 140, 255, 255), IM_COL32(200, 230, 255, 200),
      IM_COL32(10, 20, 50, 50),
      CS_LINES(INTRO_WATER_WORLD) },

    // 6: Chemistry Set
    { "The Molecular Workshop", CS_MOLECULE,
      IM_COL32(80, 220, 100, 255), IM_COL32(240, 220, 60, 200),
      IM_COL32(15, 35, 15, 50),
      CS_LINES(INTRO_CHEMISTRY_SET) },

    // 7: Dark Sector
    { "The Invisible Universe", CS_NEBULA,
      IM_COL32(80, 40, 120, 200), IM_COL32(60, 30, 100, 140),
      IM_COL32(20, 8, 35, 40),
      CS_LINES(INTRO_DARK_SECTOR) },

    // 8: Particle Zoo
    { "Catalogue of Creation", CS_NEBULA,
      IM_COL32(200, 120, 255, 255), IM_COL32(255, 200, 100, 200),
      IM_COL32(30, 15, 50, 50),
      CS_LINES(INTRO_PARTICLE_ZOO) },

    // 9: Stellar Nucleosynthesis
    { "Forging the Elements", CS_NUCLEUS,
      IM_COL32(255, 140, 40, 255), IM_COL32(80, 160, 255, 200),
      IM_COL32(50, 25, 10, 55),
      CS_LINES(INTRO_NUCLEOSYNTHESIS) },

    // 10: Free Play: Lab (no cutscene)
    { "", CS_NEBULA, 0, 0, 0, STUB_EMPTY, 0 },

    // 11: Free Play: Space (no cutscene)
    { "", CS_NEBULA, 0, 0, 0, STUB_EMPTY, 0 },

    // 12: Quark Soup
    { "The First Microsecond", CS_EXPANSION,
      IM_COL32(255, 80, 40, 255), IM_COL32(255, 160, 60, 220),
      IM_COL32(60, 15, 10, 60),
      CS_LINES(INTRO_QUARK_SOUP) },

    // 13: Cosmic Dawn
    { "From Fire to First Light", CS_EXPANSION,
      IM_COL32(255, 220, 180, 255), IM_COL32(255, 160, 60, 220),
      IM_COL32(50, 30, 10, 55),
      CS_LINES(INTRO_COSMIC_DAWN) },

    // 14: Neutron Star
    { "Heart of a Dead Star", CS_LATTICE,
      IM_COL32(80, 120, 200, 255), IM_COL32(140, 160, 190, 180),
      IM_COL32(10, 15, 35, 50),
      CS_LINES(INTRO_NEUTRON_STAR) },

    // 15: Magnetar
    { "The Most Magnetic Object", CS_LATTICE,
      IM_COL32(160, 80, 255, 255), IM_COL32(0, 200, 255, 200),
      IM_COL32(30, 10, 50, 55),
      CS_LINES(INTRO_MAGNETAR) },

    // 16: SUSY World
    { "Beyond the Standard Model", CS_NEBULA,
      IM_COL32(0, 200, 180, 255), IM_COL32(255, 200, 60, 200),
      IM_COL32(10, 35, 30, 50),
      CS_LINES(INTRO_SUSY_WORLD) },

    // 17: The Collider
    { "Smashing the Frontier", CS_COLLISION,
      IM_COL32(0, 200, 255, 255), IM_COL32(220, 240, 255, 220),
      IM_COL32(10, 25, 50, 55),
      CS_LINES(INTRO_THE_COLLIDER) },
};

const CutsceneDef CUTSCENE_COMPLETIONS[18] = {
    // 0: First Light
    { "First Light", CS_NUCLEUS,
      IM_COL32(255, 240, 180, 255), IM_COL32(255, 200, 60, 220),
      IM_COL32(50, 40, 15, 60),
      CS_LINES(COMP_FIRST_LIGHT) },

    // 1: Hydrogen Factory
    { "Hydrogen Factory", CS_NUCLEUS,
      IM_COL32(80, 160, 255, 255), IM_COL32(0, 220, 255, 200),
      IM_COL32(10, 25, 55, 50),
      CS_LINES(COMP_HYDROGEN_FACTORY) },

    // 2: Solar Core
    { "Solar Core", CS_EXPANSION,
      IM_COL32(255, 200, 60, 255), IM_COL32(255, 120, 30, 220),
      IM_COL32(60, 30, 5, 65),
      CS_LINES(COMP_SOLAR_CORE) },

    // 3: Chain Reaction
    { "Chain Reaction", CS_EXPANSION,
      IM_COL32(100, 255, 80, 255), IM_COL32(255, 100, 40, 200),
      IM_COL32(25, 50, 15, 55),
      CS_LINES(COMP_CHAIN_REACTION) },

    // 4: Antimatter Lab
    { "Antimatter Lab", CS_EXPANSION,
      IM_COL32(255, 200, 255, 255), IM_COL32(0, 200, 255, 220),
      IM_COL32(40, 15, 50, 55),
      CS_LINES(COMP_ANTIMATTER_LAB) },

    // 5: Water World
    { "Water World", CS_MOLECULE,
      IM_COL32(60, 160, 255, 255), IM_COL32(200, 240, 255, 200),
      IM_COL32(10, 25, 55, 50),
      CS_LINES(COMP_WATER_WORLD) },

    // 6: Chemistry Set
    { "Chemistry Set", CS_MOLECULE,
      IM_COL32(100, 230, 120, 255), IM_COL32(255, 230, 80, 200),
      IM_COL32(15, 40, 15, 50),
      CS_LINES(COMP_CHEMISTRY_SET) },

    // 7: Dark Sector
    { "Dark Sector", CS_NEBULA,
      IM_COL32(100, 50, 160, 220), IM_COL32(40, 20, 80, 140),
      IM_COL32(25, 10, 40, 45),
      CS_LINES(COMP_DARK_SECTOR) },

    // 8: Particle Zoo
    { "Particle Zoo", CS_NEBULA,
      IM_COL32(180, 100, 255, 255), IM_COL32(100, 200, 255, 200),
      IM_COL32(30, 15, 50, 50),
      CS_LINES(COMP_PARTICLE_ZOO) },

    // 9: Stellar Nucleosynthesis
    { "Stellar Nucleosynthesis", CS_NUCLEUS,
      IM_COL32(200, 200, 200, 255), IM_COL32(255, 140, 40, 200),
      IM_COL32(40, 30, 15, 55),
      CS_LINES(COMP_NUCLEOSYNTHESIS) },

    // 10: Free Play: Lab (no cutscene)
    { "", CS_NEBULA, 0, 0, 0, STUB_EMPTY, 0 },

    // 11: Free Play: Space (no cutscene)
    { "", CS_NEBULA, 0, 0, 0, STUB_EMPTY, 0 },

    // 12: Quark Soup
    { "Quark Soup", CS_LATTICE,
      IM_COL32(200, 60, 30, 255), IM_COL32(255, 140, 40, 200),
      IM_COL32(50, 10, 5, 55),
      CS_LINES(COMP_QUARK_SOUP) },

    // 13: Cosmic Dawn
    { "Cosmic Dawn", CS_NEBULA,
      IM_COL32(255, 240, 200, 255), IM_COL32(100, 180, 255, 200),
      IM_COL32(40, 30, 15, 50),
      CS_LINES(COMP_COSMIC_DAWN) },

    // 14: Neutron Star
    { "Neutron Star", CS_LATTICE,
      IM_COL32(60, 100, 180, 255), IM_COL32(120, 160, 220, 180),
      IM_COL32(8, 12, 30, 50),
      CS_LINES(COMP_NEUTRON_STAR) },

    // 15: Magnetar
    { "Magnetar", CS_LATTICE,
      IM_COL32(180, 80, 255, 255), IM_COL32(0, 180, 255, 200),
      IM_COL32(30, 10, 50, 50),
      CS_LINES(COMP_MAGNETAR) },

    // 16: SUSY World
    { "SUSY World", CS_NEBULA,
      IM_COL32(0, 200, 180, 255), IM_COL32(255, 200, 60, 200),
      IM_COL32(10, 35, 30, 50),
      CS_LINES(COMP_SUSY_WORLD) },

    // 17: The Collider
    { "The Collider", CS_EXPANSION,
      IM_COL32(0, 200, 255, 255), IM_COL32(255, 200, 100, 220),
      IM_COL32(10, 25, 50, 55),
      CS_LINES(COMP_THE_COLLIDER) },
};
