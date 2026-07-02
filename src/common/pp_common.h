#pragma once
// ── Particle Playground — Shared Infrastructure ─────────────────────────────
// Common constants and forward declarations used by all expansions.

#include <cstdint>

// ── Canvas / world geometry ─────────────────────────────────────────────────
// These define the simulation coordinate space. All expansions share the same
// canvas dimensions so shaders, renderers, and UI can use consistent coords.

constexpr int REGION_W = 16;
constexpr int REGION_H = 16;
constexpr int WORLD_W  = REGION_W * 103;   // 1648
constexpr int WORLD_H  = REGION_H * 58;    // 928

// ── Forward declarations ────────────────────────────────────────────────────

class  VulkanContext;
struct AudioPlayer;
struct SimpleRenderer;
