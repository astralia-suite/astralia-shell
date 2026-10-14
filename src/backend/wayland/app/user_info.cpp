#include "service/user_service.h"

#include "wayland/app/user_info.h"

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

std::string face_path() { return user().face_path(); }

} // namespace user_info
