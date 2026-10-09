#include <string_view>
#include <vector>

#include "core/cli.h"
#include "core/ipc.h"
#include "core/runtime_paths.h"

#include "check.h"

namespace {

astralia::Invocation parse(std::vector<std::string_view> args) {
    return astralia::parse_invocation(args);
}

} // namespace

void check_parse_invocation() {
    using astralia::Mode;
    using test::check;
    check(parse({}).mode == Mode::daemon, "no arguments starts the daemon");
    check(parse({"debug"}).mode == Mode::debug, "debug runs in the foreground");
    astralia::Invocation kill = parse({"kill"});
    check(kill.mode == Mode::client && kill.command == "kill", "other verbs go to the client");
    astralia::Invocation locked = parse({"start-locked"});
    check(locked.mode == Mode::daemon && locked.locked, "start-locked starts the daemon locked");
    check(parse({"debug", "start-locked"}).mode == Mode::debug, "start-locked combines with debug");
    astralia::Invocation joined = parse({"debug", "now"});
    check(joined.mode == Mode::client && joined.command == "debug now",
          "multiple arguments are joined with spaces");
}

void check_runtime_path() {
    using astralia::runtime_path;
    using test::check;
    check(runtime_path(":1", "/run/user/1000", ".sock") == "/run/user/1000/astralia-1.sock",
          "display number names the socket");
    check(runtime_path(":0.0", "/run/user/1000", ".lock") == "/run/user/1000/astralia-0.0.lock",
          "screen number is kept");
    check(runtime_path("wayland-1", "/run/user/1000", ".sock") ==
              "/run/user/1000/astralia-wayland-1.sock",
          "wayland socket name names the socket");
    check(runtime_path("/tmp/launch/org:0", "", ".log") == "/tmp/astralia-_tmp_launch_org0.log",
          "slashes become underscores and an empty runtime dir falls back to /tmp");
    check(runtime_path("", "/run/user/1000", ".sock") == "/run/user/1000/astralia.sock",
          "unset session drops the suffix");
}

void check_format_help() {
    using test::check;
    std::vector<astralia::IpcHandler> handlers{
        {"kill", {}, "quit"},
        {"help", {}, "list verbs"},
        {"reload", {}, "reload config"},
    };
    check(astralia::format_help(handlers) == "astralia <verb>:\n"
                                             "  help    list verbs\n"
                                             "  kill    quit\n"
                                             "  reload  reload config\n",
          "help is sorted and aligned");
}
