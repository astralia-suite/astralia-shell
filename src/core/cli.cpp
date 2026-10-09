#include <utility>

#include "core/cli.h"

namespace astralia {

Invocation parse_invocation(std::span<const std::string_view> args) {
    bool locked = !args.empty() && args.back() == "start-locked";
    if (locked) {
        args = args.first(args.size() - 1);
    }
    if (args.empty()) {
        return {Mode::daemon, {}, locked};
    }
    if (args.size() == 1 && args[0] == "debug") {
        return {Mode::debug, {}, locked};
    }
    std::string command;
    for (std::string_view arg : args) {
        if (!command.empty()) {
            command += ' ';
        }
        command += arg;
    }
    return {Mode::client, std::move(command)};
}

} // namespace astralia
