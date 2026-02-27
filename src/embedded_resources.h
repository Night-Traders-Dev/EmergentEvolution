#pragma once
// Embedded resource declarations for PORTABLE_BUILD
// When PORTABLE_BUILD is defined, shaders and icons are compiled into the binary
// so the exe runs without external files (except optional sound.mp3).

#include <cstddef>

#ifdef PORTABLE_BUILD

// ── Shaders (SPIR-V) ────────────────────────────────────────────────────────
extern const unsigned char physics_spv_data[];
extern const size_t        physics_spv_size;

extern const unsigned char fullscreen_vert_spv_data[];
extern const size_t        fullscreen_vert_spv_size;

extern const unsigned char fullscreen_frag_spv_data[];
extern const size_t        fullscreen_frag_spv_size;

// ── Icons (PNG) ──────────────────────────────────────────────────────────────
extern const unsigned char icon_32_png_data[];
extern const size_t        icon_32_png_size;

extern const unsigned char icon_64_png_data[];
extern const size_t        icon_64_png_size;

extern const unsigned char icon_256_png_data[];
extern const size_t        icon_256_png_size;

#endif // PORTABLE_BUILD
