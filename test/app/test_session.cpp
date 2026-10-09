#include <string>

#include "app/session.h"

#include "check.h"

void check_session() {
    using namespace astralia;
    using test::check;
    SessionEnvironment environment;
    check(detect_session(environment) == SessionKind::none, "an empty environment has no session");
    environment.display = ":0";
    check(detect_session(environment) == SessionKind::x11, "DISPLAY alone means x11");
    environment.wayland_display = "wayland-1";
    check(detect_session(environment) == SessionKind::wayland, "WAYLAND_DISPLAY wins over DISPLAY");
    environment.session_type = "x11";
    check(detect_session(environment) == SessionKind::x11, "XDG_SESSION_TYPE wins over the display variables");
    environment.i3_socket = "/run/user/1000/i3/ipc-socket.1";
    environment.session_type = "wayland";
    check(detect_session(environment) == SessionKind::x11, "I3SOCK wins over XDG_SESSION_TYPE");
    environment.hyprland_signature = "abc_123";
    check(detect_session(environment) == SessionKind::wayland, "the Hyprland signature wins over I3SOCK");
    environment.hyprland_signature.clear();
    environment.sway_socket = "/run/user/1000/sway-ipc.1000.1.sock";
    check(detect_session(environment) == SessionKind::wayland, "the Sway socket wins over I3SOCK, which Sway sets too");
    environment.hyprland_signature = "abc_123";
    environment.override_name = "x11";
    check(detect_session(environment) == SessionKind::x11, "the override wins over everything");
    environment.override_name = "wayland";
    environment.hyprland_signature.clear();
    check(detect_session(environment) == SessionKind::wayland, "the override can force wayland");
    environment.override_name = "bogus";
    environment.sway_socket.clear();
    environment.i3_socket.clear();
    environment.session_type.clear();
    environment.wayland_display.clear();
    environment.display = ":1";
    check(detect_session(environment) == SessionKind::x11, "an unknown override is ignored");

    check(session_name(SessionKind::wayland) == "wayland" && session_name(SessionKind::x11) == "x11" && session_name(SessionKind::none) == "none", "session names");
    check(backend_library_name(SessionKind::wayland) == "libastralia-wayland.so" && backend_library_name(SessionKind::x11) == "libastralia-x11.so", "library names");

    std::vector<std::string> dirs = backend_search_dirs("/override", "/build");
    check(dirs.size() == 3 && dirs[0] == "/override" && dirs[1] == "/build", "the override is searched first, then the executable directory");
    std::vector<std::string> deduplicated = backend_search_dirs("/build", "/build");
    check(deduplicated.size() == 2 && deduplicated[0] == "/build", "duplicate directories collapse");
    std::vector<std::string> bare = backend_search_dirs("", "");
    check(bare.size() == 1, "only the install directory remains when nothing else is known");

    auto none = load_backend(SessionKind::none);
    check(!none.has_value() && none.error().find("no graphical session") != std::string::npos, "loading without a session explains why");
    setenv("ASTRALIA_BACKEND_DIR", "/nonexistent-astralia-dir", 1);
    auto missing = load_backend(SessionKind::wayland);
    unsetenv("ASTRALIA_BACKEND_DIR");
    check(missing.has_value() || missing.error().find("libastralia-wayland.so") != std::string::npos, "a missing library is named in the error");
}
