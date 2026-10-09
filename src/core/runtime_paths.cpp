#include <cstdlib>

#include "core/runtime_paths.h"

namespace astralia {

std::string runtime_path(std::string_view session, std::string_view runtime_dir,
                         std::string_view suffix) {
    std::string path(runtime_dir.empty() ? "/tmp" : runtime_dir);
    path += "/astralia";
    std::string id;
    for (char c : session) {
        if (c == ':') {
            continue;
        }
        id += c == '/' ? '_' : c;
    }
    if (!id.empty()) {
        path += '-';
        path += id;
    }
    path += suffix;
    return path;
}

std::string runtime_path(std::string_view suffix) {
    const char *session = std::getenv("WAYLAND_DISPLAY");
    if (session == nullptr || *session == '\0') {
        session = std::getenv("DISPLAY");
    }
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    return runtime_path(session ? session : "", runtime_dir ? runtime_dir : "", suffix);
}

} // namespace astralia
