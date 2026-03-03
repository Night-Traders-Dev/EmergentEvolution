#pragma once
// Steam integration wrapper — all Steamworks API calls are behind HAS_STEAM guards.
// When HAS_STEAM is not defined, all functions compile as no-ops.

namespace steam {

// Initialize Steam API. Returns true if Steam is running and init succeeded.
// Returns false if Steam is not available or if the app should relaunch via Steam.
bool init();

// Must be called each frame from the main loop.
void run_callbacks();

// Shutdown cleanly.
void shutdown();

// ── Achievements ─────────────────────────────────────────────────────────────

// Set a Steam achievement by API name (e.g., "ACH_FIRST_FUSION").
void unlock_achievement(const char* api_name);

// Clear an achievement (development/testing only).
void clear_achievement(const char* api_name);

// ── Cloud Saves ──────────────────────────────────────────────────────────────

// Write data to Steam Cloud. Returns true on success.
bool cloud_write(const char* filename, const void* data, int size);

// Read data from Steam Cloud. Returns bytes read, or -1 on failure.
int cloud_read(const char* filename, void* buffer, int buffer_size);

// Check if a file exists in Steam Cloud.
bool cloud_exists(const char* filename);

} // namespace steam
