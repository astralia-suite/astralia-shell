#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <format>
#include <unistd.h>

#include "app/session.h"

#ifndef ASTRALIA_BACKEND_DIR
#define ASTRALIA_BACKEND_DIR "/usr/lib/astralia"
#endif

namespace astralia {

namespace {

std::string environment_value(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr ? value : "";
}

std::string executable_dir() {
    std::error_code error;
    std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string() : path.parent_path().string();
}

} // namespace

SessionEnvironment session_environment() {
    return {environment_value("ASTRALIA_BACKEND"), environment_value("HYPRLAND_INSTANCE_SIGNATURE"),
            environment_value("I3SOCK"), environment_value("SWAYSOCK"), environment_value("XDG_SESSION_TYPE"),
            environment_value("WAYLAND_DISPLAY"), environment_value("DISPLAY")};
}

SessionKind detect_session(const SessionEnvironment &environment) {
    if (environment.override_name == "wayland") {
        return SessionKind::wayland;
    }
    if (environment.override_name == "x11") {
        return SessionKind::x11;
    }
    if (!environment.hyprland_signature.empty() || !environment.sway_socket.empty()) {
        return SessionKind::wayland;
    }
    if (!environment.i3_socket.empty()) {
        return SessionKind::x11;
    }
    if (environment.session_type == "wayland") {
        return SessionKind::wayland;
    }
    if (environment.session_type == "x11") {
        return SessionKind::x11;
    }
    if (!environment.wayland_display.empty()) {
        return SessionKind::wayland;
    }
    if (!environment.display.empty()) {
        return SessionKind::x11;
    }
    return SessionKind::none;
}

std::string_view session_name(SessionKind kind) {
    switch (kind) {
    case SessionKind::wayland:
        return "wayland";
    case SessionKind::x11:
        return "x11";
    case SessionKind::none:
        break;
    }
    return "none";
}

std::string backend_library_name(SessionKind kind) {
    return std::format("libastralia-{}.so", session_name(kind));
}

std::vector<std::string> backend_search_dirs(std::string_view override_dir, std::string_view executable_dir) {
    std::vector<std::string> dirs;
    for (std::string_view dir : {override_dir, executable_dir, std::string_view(ASTRALIA_BACKEND_DIR)}) {
        if (!dir.empty() && std::ranges::find(dirs, dir) == dirs.end()) {
            dirs.emplace_back(dir);
        }
    }
    return dirs;
}

std::expected<LoadedBackend, std::string> load_backend(SessionKind kind) {
    if (kind == SessionKind::none) {
        return std::unexpected("no graphical session found: set WAYLAND_DISPLAY or DISPLAY, or ASTRALIA_BACKEND=wayland|x11");
    }
    std::string library = backend_library_name(kind);
    std::string last_error = "not found";
    for (const std::string &dir : backend_search_dirs(environment_value("ASTRALIA_BACKEND_DIR"), executable_dir())) {
        std::string path = dir + "/" + library;
        if (access(path.c_str(), R_OK) != 0) {
            continue;
        }
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            last_error = dlerror();
            continue;
        }
        void *symbol = dlsym(handle, backend_factory_symbol);
        if (symbol == nullptr) {
            last_error = std::format("{} has no {}", path, backend_factory_symbol);
            continue;
        }
        auto factory = reinterpret_cast<BackendFactory>(symbol);
        return LoadedBackend{handle, std::unique_ptr<Backend>(factory()), path};
    }
    return std::unexpected(std::format("cannot load {}: {}", library, last_error));
}

} // namespace astralia
