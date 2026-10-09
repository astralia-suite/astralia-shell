#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <execinfo.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/log.h"

namespace astralia::log {

namespace {

FILE *open_state_file() {
    const char *state_home = std::getenv("XDG_STATE_HOME");
    const char *home = std::getenv("HOME");
    std::string base = state_home && *state_home ? std::string(state_home)
                                                 : std::string(home ? home : "") + "/.local/state";
    for (std::size_t pos = 1; pos <= base.size(); ++pos) {
        if (pos == base.size() || base[pos] == '/') {
            mkdir(base.substr(0, pos).c_str(), 0755);
        }
    }
    std::string dir = base + "/astralia";
    mkdir(dir.c_str(), 0755);
    return std::fopen((dir + "/astralia.log").c_str(), "a");
}

FILE *&state_file() {
    static FILE *file = open_state_file();
    return file;
}

void emit(std::string_view level, std::string_view message) {
    timeval tv;
    gettimeofday(&tv, nullptr);
    char stamp[32];
    tm local{};
    localtime_r(&tv.tv_sec, &local);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    FILE *outs[2] = {stderr, state_file()};
    for (FILE *out : outs) {
        if (!out) {
            continue;
        }
        std::fprintf(out, "[%s.%03ld] [%.*s] %.*s\n", stamp, static_cast<long>(tv.tv_usec / 1000),
                     static_cast<int>(level.size()), level.data(), static_cast<int>(message.size()),
                     message.data());
        std::fflush(out);
    }
}

void terminate_handler() {
    if (std::exception_ptr eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            emit("error", std::string("terminate: uncaught exception: ") + e.what());
        } catch (...) {
            emit("error", "terminate: uncaught exception of unknown type");
        }
    } else {
        emit("error", "terminate: called with no active exception");
    }
    std::abort();
}

void crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    char header[64];
    int header_len = std::snprintf(header, sizeof header, "astralia: crashed on signal %d\n", sig);
    int fds[2] = {STDERR_FILENO, state_file() ? fileno(state_file()) : -1};
    for (int fd : fds) {
        if (fd < 0) {
            continue;
        }
        (void)!::write(fd, header, static_cast<std::size_t>(header_len));
        backtrace_symbols_fd(frames, n, fd);
    }
    _exit(128 + sig);
}

} // namespace

void write(std::string_view level, std::string_view message) { emit(level, message); }

void writef(std::string_view level, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    emit(level, buffer);
}

void install_crash_handler() {
    state_file();
    void *warmup[8];
    backtrace(warmup, 8);
    std::set_terminate(terminate_handler);
    struct sigaction sa{};
    sa.sa_handler = crash_handler;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) {
        sigaction(sig, &sa, nullptr);
    }
}

} // namespace astralia::log
