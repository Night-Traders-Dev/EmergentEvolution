#pragma once
// ── Launch utilities — spawn sibling executables ────────────────────────────

#include <string>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

// Resolve directory containing the current executable
inline std::string get_exe_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* sep = strrchr(path, '\\');
    if (sep) sep[1] = '\0';
    return path;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "./";
    buf[len] = '\0';
    char* sep = strrchr(buf, '/');
    if (sep) sep[1] = '\0';
    return buf;
#endif
}

// Launch a sibling executable (located in the same directory as this binary)
inline void launch_sibling_exe(const char* exe_name) {
    std::string dir = get_exe_dir();

#ifdef _WIN32
    std::string cmd = dir + exe_name + ".exe";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CreateProcessA(cmd.c_str(), nullptr, nullptr, nullptr,
                   FALSE, 0, nullptr, dir.c_str(), &si, &pi);
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#else
    std::string cmd = dir + exe_name;
    pid_t pid = fork();
    if (pid == 0) {
        execl(cmd.c_str(), exe_name, nullptr);
        _exit(127);
    }
#endif
}
