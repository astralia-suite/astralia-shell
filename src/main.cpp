#include <csignal>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <print>

#include "app/session.h"
#include "app/shell.h"

#include "core/allocator.h"
#include "core/cli.h"
#include "core/daemon.h"
#include "core/ipc.h"
#include "core/log.h"
#include "core/runtime_paths.h"
#include "core/single_instance.h"

int main(int argc, char **argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::vector<std::string_view> args(argv + 1, argv + argc);
    astralia::Invocation invocation = astralia::parse_invocation(args);
    if (invocation.mode == astralia::Mode::client && invocation.command == "modules") {
        std::print("{}", astralia::shell_report());
        return EXIT_SUCCESS;
    }
    if (invocation.mode == astralia::Mode::client) {
        return astralia::run_ipc_client(astralia::runtime_path(".sock"), invocation.command);
    }
    astralia::log::install_crash_handler();
    astralia::SessionKind kind = astralia::detect_session(astralia::session_environment());
    auto loaded = astralia::load_backend(kind);
    if (!loaded) {
        astralia::log::error("{}", loaded.error());
        return EXIT_FAILURE;
    }
    auto instance = astralia::SingleInstance::acquire(astralia::runtime_path(".lock"));
    if (!instance) {
        astralia::log::error("{}", instance.error());
        return EXIT_FAILURE;
    }
    if (invocation.mode == astralia::Mode::daemon) {
        astralia::daemonize();
    }
    astralia::log::info("starting the {} backend from {}", loaded->backend->name(), loaded->path);
    astralia::tune_allocator(loaded->backend->malloc_arenas());
    return loaded->backend->run();
}
