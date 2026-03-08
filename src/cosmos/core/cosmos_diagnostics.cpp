#include "cosmos/cosmos_app_internal.h"
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>

#if !defined(_WIN32)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#if !defined(_WIN32)
std::atomic<int> g_crash_log_fd{-1};

void crash_log_write(const char* msg) {
    if (!msg) return;
    int fd = g_crash_log_fd.load();
    if (fd < 0) return;
    size_t len = std::strlen(msg);
    while (len > 0) {
        ssize_t written = ::write(fd, msg, len);
        if (written <= 0) break;
        msg += written;
        len -= (size_t)written;
    }
}

void cosmos_signal_handler(int signum, siginfo_t* info, void* /*ucontext*/) {
    char line[256];
    int code = info ? info->si_code : 0;
    int pid = info ? info->si_pid : 0;
    std::snprintf(line, sizeof(line),
                  "\n=== Cosmos crash signal %d code=%d pid=%d ===\n",
                  signum, code, pid);
    crash_log_write(line);

    void* bt[64];
    int count = backtrace(bt, 64);
    int fd = g_crash_log_fd.load();
    if (fd >= 0 && count > 0)
        backtrace_symbols_fd(bt, count, fd);
    crash_log_write("=== end crash ===\n");
    _exit(128 + signum);
}
#endif

void install_crash_handlers_once() {
    static std::once_flag once;
    std::call_once(once, []() {
#if !defined(_WIN32)
        int fd = ::open("/tmp/cosmos_crash.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
        g_crash_log_fd.store(fd);
        crash_log_write("\n=== Cosmos crash handler installed ===\n");

        struct sigaction sa{};
        sa.sa_sigaction = cosmos_signal_handler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
        for (int sig : signals)
            sigaction(sig, &sa, nullptr);
#endif
        std::set_terminate([]() {
#if !defined(_WIN32)
            crash_log_write("\n=== std::terminate() called ===\n");
            void* bt[64];
            int count = backtrace(bt, 64);
            int fd = g_crash_log_fd.load();
            if (fd >= 0 && count > 0)
                backtrace_symbols_fd(bt, count, fd);
            crash_log_write("=== end terminate ===\n");
#endif
            std::_Exit(1);
        });
    });
}

} // namespace

void cosmos_install_crash_handlers() { install_crash_handlers_once(); }

void CosmosApp::debug_logf(const char* fmt, ...) const {
    if (!fmt) return;
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);

    auto now = std::chrono::system_clock::now();
    std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &raw_time);
#else
    localtime_r(&raw_time, &local_tm);
#endif
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);

    char msg_buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    auto append_log = [&](const char* path) {
        if (!path) return;
        FILE* fp = std::fopen(path, "a");
        if (!fp) return;
        std::fprintf(fp, "[%s][step=%llu] %s\n", time_buf,
                     (unsigned long long)diagnostics_step_counter_, msg_buf);
        std::fflush(fp);
        std::fclose(fp);
    };

    append_log("cosmos_debug.log");
#if !defined(_WIN32)
    append_log("/tmp/cosmos_debug.log");
#endif
}

bool CosmosApp::validate_body_state(const char* context, bool pause_on_invalid) {
    if (!diagnostics_enabled_) return true;

    const char* ctx = context ? context : "unknown";
    auto finite_vec3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };

    if (!std::isfinite(sim_time_)) {
        debug_logf("Invalid sim_time detected in %s; resetting to 0", ctx);
        sim_time_ = 0.0f;
    }
    if (!std::isfinite(cfg.sim_time_accumulated)) {
        debug_logf("Invalid sim_time_accumulated detected in %s; resetting to 0", ctx);
        cfg.sim_time_accumulated = 0.0;
    }

    size_t parent_fixes = 0;
    size_t invalid_count = 0;
    int sample_logs = 0;
    const size_t body_count_before = state.bodies.size();

    for (size_t i = 0; i < state.bodies.size(); ++i) {
        auto& b = state.bodies[i];
        if (b.marked_for_removal) continue;

        bool invalid = false;
        std::string reasons;
        auto add_reason = [&](const char* tag) {
            if (!reasons.empty()) reasons += ",";
            reasons += tag;
        };

        if (!finite_vec3(b.pos)) { invalid = true; add_reason("pos"); }
        if (!finite_vec3(b.vel)) { invalid = true; add_reason("vel"); }
        if (!std::isfinite(b.mass) || b.mass <= 0.0f) { invalid = true; add_reason("mass"); }
        if (!std::isfinite(b.radius) || b.radius <= 0.0f) { invalid = true; add_reason("radius"); }
        if (!std::isfinite(b.temperature)) { invalid = true; add_reason("temperature"); }
        if (!std::isfinite(b.internal_energy)) { invalid = true; add_reason("internal_energy"); }
        if (!std::isfinite(b.angular_vel)) { invalid = true; add_reason("angular_vel"); }
        if (!std::isfinite(b.age)) { invalid = true; add_reason("age"); }

        bool parent_invalid = (b.parent < -1 || b.parent >= (int)state.bodies.size() || b.parent == (int)i);
        if (parent_invalid) {
            b.parent = -1;
            ++parent_fixes;
        }

        if (!invalid) continue;

        b.marked_for_removal = true;
        ++invalid_count;
        if (sample_logs < 12) {
            debug_logf("%s invalid body idx=%zu type=%u mass=%.9g radius=%.9g pos=(%.9g,%.9g,%.9g) "
                       "vel=(%.9g,%.9g,%.9g) reasons=%s",
                       ctx, i, b.type, b.mass, b.radius,
                       b.pos.x, b.pos.y, b.pos.z,
                       b.vel.x, b.vel.y, b.vel.z, reasons.c_str());
            ++sample_logs;
        }
    }

    if (parent_fixes > 0) {
        debug_logf("%s fixed %zu invalid parent links", ctx, parent_fixes);
    }

    if (invalid_count > 0) {
        debug_logf("%s flagged %zu invalid bodies out of %zu; cleaning up",
                   ctx, invalid_count, body_count_before);
        cleanup_bodies();
        cfg.body_count = (uint32_t)state.bodies.size();
        if (pause_on_invalid && diagnostics_pause_on_invalid_) {
            paused = true;
            debug_logf("%s paused simulation after invalid-state cleanup", ctx);
        }
        return false;
    }

    if (state.trails.size() != state.bodies.size()) {
        debug_logf("%s corrected trail/body count mismatch trails=%zu bodies=%zu",
                   ctx, state.trails.size(), state.bodies.size());
        state.trails.resize(state.bodies.size());
    }
    return true;
}
