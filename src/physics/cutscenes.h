#pragma once
#include <cstdint>

// ── Cutscene Scene Types ─────────────────────────────────────────────────────
// Each type produces a different particle animation style.

enum CutsceneScene {
    CS_NUCLEUS,     // Glowing center + orbiting particles (Nuclear scenarios)
    CS_NEBULA,      // Slowly drifting cloud of particles (Cosmic/Dark)
    CS_COLLISION,   // Two streams meeting at center (Collider/Antimatter)
    CS_EXPANSION,   // Particles expanding outward from center (Big Bang)
    CS_LATTICE,     // Dense vibrating grid (Neutron Star/Magnetar)
    CS_MOLECULE,    // Small bonded clusters with force lines (Chemistry)
};

// ── Cutscene Data ────────────────────────────────────────────────────────────

struct CutsceneTextLine {
    const char* text;
    float hold;  // seconds to hold after fade-in (total = hold + 1.0s for fades)
};

struct CutsceneDef {
    const char* title;           // Big glow title text
    CutsceneScene scene;         // Particle animation type
    uint32_t color1, color2;     // Primary + accent particle colors (IM_COL32)
    uint32_t bg_glow;            // Background nebula glow color
    const CutsceneTextLine* lines;
    int line_count;
};

// 18 entries each (index = scenario index). Entries 10,11 (sandbox) are empty stubs.
extern const CutsceneDef CUTSCENE_INTROS[18];
extern const CutsceneDef CUTSCENE_COMPLETIONS[18];

static constexpr int CUTSCENE_SCENARIO_COUNT = 18;
