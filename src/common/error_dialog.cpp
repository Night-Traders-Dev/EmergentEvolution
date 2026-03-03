#include "common/error_dialog.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>

void show_error_dialog(const char* title, const char* message) {
    fprintf(stderr, "%s: %s\n", title, message);
    MessageBoxA(NULL, message, title, MB_OK | MB_ICONERROR);
}

#else
// Linux: try zenity, kdialog, xmessage in order; fallback to stderr only.

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool try_exec(const char* program, const char* const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child — redirect stderr to /dev/null so dialog tool noise is suppressed
        execvp(program, const_cast<char* const*>(argv));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) != 127;
}

void show_error_dialog(const char* title, const char* message) {
    fprintf(stderr, "%s: %s\n", title, message);

    std::string full = std::string(title) + "\n\n" + message;

    // Try zenity (GNOME/GTK)
    {
        const char* argv[] = {"zenity", "--error", "--title", title,
                               "--text", message, "--no-wrap", nullptr};
        if (try_exec("zenity", argv)) return;
    }
    // Try kdialog (KDE)
    {
        const char* argv[] = {"kdialog", "--error", message, "--title", title, nullptr};
        if (try_exec("kdialog", argv)) return;
    }
    // Try xmessage (bare X11)
    {
        const char* argv[] = {"xmessage", "-center", full.c_str(), nullptr};
        if (try_exec("xmessage", argv)) return;
    }
    // All failed — stderr output above is the best we can do
}
#endif
