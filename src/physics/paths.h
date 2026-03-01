#pragma once
// Platform-appropriate data directory for saves, settings, achievements, etc.
//
// Returns:
//   Linux:    $XDG_DATA_HOME/particle_playground/  (default ~/.local/share/particle_playground/)
//   Windows:  %APPDATA%\ParticlePlayground\
//   Portable: saves/  (when PORTABLE_PATHS CMake option is ON)
//
// The directory is created on first call if it doesn't exist.

#include <string>
#include <filesystem>
#include <cstdlib>

#ifdef PORTABLE_PATHS

inline const std::string& get_data_dir() {
    static std::string dir = [] {
        std::string d = "saves/";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

#else  // Platform-standard paths

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

inline const std::string& get_data_dir() {
    static std::string dir = [] {
        std::string d;
        std::error_code ec;
#ifdef _WIN32
        char appdata[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
            d = std::string(appdata) + "\\ParticlePlayground\\";
        } else {
            d = "saves/";
        }
#else
        const char* xdg = std::getenv("XDG_DATA_HOME");
        if (xdg && xdg[0]) {
            d = std::string(xdg) + "/particle_playground/";
        } else {
            const char* home = std::getenv("HOME");
            if (home && home[0]) {
                d = std::string(home) + "/.local/share/particle_playground/";
            } else {
                d = "saves/";
            }
        }
#endif
        std::filesystem::create_directories(d, ec);
        return d;
    }();
    return dir;
}

#endif  // PORTABLE_PATHS
