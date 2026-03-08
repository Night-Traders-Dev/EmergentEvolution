#pragma once
// ── Shared UI data for cosmos panels ─────────────────────────────────────────
// Defined in cosmos_ui_data.cpp

#include "imgui.h"
#include "cosmos/cosmos_types.h"
#include <cstddef>

extern const char* const CTYPE_NAMES[];
extern const char* const PLANET_CLASS_NAMES[];
extern const char* const MATERIAL_PHASE_NAMES[];
extern const ImU32       CTYPE_COLORS[];

ImVec4      star_tint_ui(const CelestialBody& b);
ImU32       body_color(const CelestialBody& b);
const char* format_sim_time(double seconds, char* buf, size_t buf_size);

void show_bottom_bar_tooltip(const char* label);
void show_hover_tooltip(const char* tip);
void draw_radial_glow(ImDrawList* dl, float cx, float cy, float radius,
                      ImU32 center_col, ImU32 edge_col);
