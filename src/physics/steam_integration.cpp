#include "physics/steam_integration.h"
#include <cstdio>

#ifdef HAS_STEAM

#include <steam/steam_api.h>

// Steam App ID — set to your real App ID before release.
// During development with steam_appid.txt, this value is ignored.
#ifndef STEAM_APP_ID
#define STEAM_APP_ID 480  // Spacewar (Valve's test app)
#endif

namespace steam {

static bool g_initialized = false;

bool init() {
    if (SteamAPI_RestartAppIfNecessary(STEAM_APP_ID)) {
        // Steam client will relaunch us — exit immediately
        return false;
    }
    if (!SteamAPI_Init()) {
        fprintf(stderr, "[steam] SteamAPI_Init failed — Steam not running?\n");
        return false;
    }
    g_initialized = true;

    // Request current stats so achievements work
    ISteamUserStats* stats = SteamUserStats();
    if (stats) stats->RequestCurrentStats();

    return true;
}

void run_callbacks() {
    if (g_initialized) SteamAPI_RunCallbacks();
}

void shutdown() {
    if (g_initialized) {
        SteamAPI_Shutdown();
        g_initialized = false;
    }
}

void unlock_achievement(const char* api_name) {
    if (!g_initialized || !api_name) return;
    ISteamUserStats* stats = SteamUserStats();
    if (!stats) return;
    bool already = false;
    stats->GetAchievement(api_name, &already);
    if (!already) {
        stats->SetAchievement(api_name);
        stats->StoreStats();
    }
}

void clear_achievement(const char* api_name) {
    if (!g_initialized || !api_name) return;
    ISteamUserStats* stats = SteamUserStats();
    if (!stats) return;
    stats->ClearAchievement(api_name);
    stats->StoreStats();
}

bool cloud_write(const char* filename, const void* data, int size) {
    if (!g_initialized) return false;
    ISteamRemoteStorage* rs = SteamRemoteStorage();
    if (!rs) return false;
    return rs->FileWrite(filename, data, size);
}

int cloud_read(const char* filename, void* buffer, int buffer_size) {
    if (!g_initialized) return -1;
    ISteamRemoteStorage* rs = SteamRemoteStorage();
    if (!rs) return -1;
    int32 file_size = rs->GetFileSize(filename);
    if (file_size <= 0 || file_size > buffer_size) return -1;
    return rs->FileRead(filename, buffer, buffer_size);
}

bool cloud_exists(const char* filename) {
    if (!g_initialized) return false;
    ISteamRemoteStorage* rs = SteamRemoteStorage();
    if (!rs) return false;
    return rs->FileExists(filename);
}

} // namespace steam

#else  // !HAS_STEAM — no-op stubs

namespace steam {

bool init() { return true; }  // non-Steam build: always "OK", no relaunch
void run_callbacks() {}
void shutdown() {}
void unlock_achievement(const char*) {}
void clear_achievement(const char*) {}
bool cloud_write(const char*, const void*, int) { return false; }
int  cloud_read(const char*, void*, int) { return -1; }
bool cloud_exists(const char*) { return false; }

} // namespace steam

#endif // HAS_STEAM
