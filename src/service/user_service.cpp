#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <vector>

#include "service/user_service.h"

namespace astralia {

std::string user_parse_os_name(const std::string &os_release) {
    std::istringstream stream(os_release);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.starts_with("PRETTY_NAME=")) {
            continue;
        }
        std::string value = line.substr(12);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }
    return "Linux";
}

std::string UserService::os_name() const {
    std::ifstream file("/etc/os-release");
    std::stringstream text;
    text << file.rdbuf();
    return user_parse_os_name(text.str());
}

UserService::UserService() : name_("unknown") {
    const passwd *entry = getpwuid(getuid());
    if (entry == nullptr) {
        return;
    }
    if (entry->pw_gecos != nullptr && entry->pw_gecos[0] != '\0') {
        std::string full = entry->pw_gecos;
        std::string first = full.substr(0, full.find(','));
        if (!first.empty()) {
            name_ = first;
            return;
        }
    }
    if (entry->pw_name != nullptr) {
        name_ = entry->pw_name;
    }
}

std::string UserService::face_path() const {
    std::vector<std::string> candidates;
    if (const char *home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        candidates.push_back(std::string(home) + "/.face");
        candidates.push_back(std::string(home) + "/.face.icon");
    }
    if (const passwd *entry = getpwuid(getuid()); entry != nullptr && entry->pw_name != nullptr) {
        candidates.push_back(std::string("/var/lib/AccountsService/icons/") + entry->pw_name);
    }
    for (const std::string &path : candidates) {
        if (access(path.c_str(), R_OK) == 0) {
            return path;
        }
    }
    return {};
}

std::string UserService::uptime() const {
    struct sysinfo info{};
    if (sysinfo(&info) != 0) {
        return {};
    }
    char text[32];
    std::snprintf(text, sizeof(text), "Up: %02ld:%02ld", info.uptime / 3600,
                  (info.uptime % 3600) / 60);
    return text;
}

} // namespace astralia
