#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/backend.h"

namespace astralia {

enum class SessionKind { none,
                         wayland,
                         x11 };

struct SessionEnvironment {
    std::string override_name;
    std::string hyprland_signature;
    std::string i3_socket;
    std::string sway_socket;
    std::string session_type;
    std::string wayland_display;
    std::string display;
};

SessionEnvironment session_environment();
SessionKind detect_session(const SessionEnvironment &environment);
std::string_view session_name(SessionKind kind);
std::string backend_library_name(SessionKind kind);
std::vector<std::string> backend_search_dirs(std::string_view override_dir, std::string_view executable_dir);

struct LoadedBackend {
    void *handle = nullptr;
    std::unique_ptr<Backend> backend;
    std::string path;
};

std::expected<LoadedBackend, std::string> load_backend(SessionKind kind);

} // namespace astralia
