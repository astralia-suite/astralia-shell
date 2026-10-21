#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "core/runtime_paths.h"

#include "modules/lock/start_locked.h"

namespace astralia {

bool lock_consume_start_locked() {
    const char *env = std::getenv("ASTRALIA_START_LOCKED");
    if (!env || std::string_view(env) != "1") {
        return false;
    }
    std::string marker = runtime_path(".locked");
    if (std::filesystem::exists(marker)) {
        return false;
    }
    std::ofstream(marker) << "locked\n";
    return true;
}

} // namespace astralia
