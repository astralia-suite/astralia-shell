#include <sys/stat.h>

#include "service/user_service.h"

#include "wayland/app/user_info.h"

#ifndef ASTRALIA_SHELL_PROFILE_MEDIA
#define ASTRALIA_SHELL_PROFILE_MEDIA ""
#endif

namespace user_info {

namespace {

const astralia::UserService &user() {
    static const astralia::UserService instance;
    return instance;
}

} // namespace

std::string username() { return user().name(); }

std::string os_pretty_name() { return user().os_name(); }

std::string uptime_string() { return user().uptime(); }

std::string profile_media_path() {
    const char *candidates[] = {ASTRALIA_SHELL_PROFILE_MEDIA,
                                "assets/gifs/profile.gif"};
    for (const char *c : candidates) {
        if (c && *c) {
            struct stat st{};
            if (stat(c, &st) == 0)
                return c;
        }
    }
    return "";
}

} // namespace user_info
