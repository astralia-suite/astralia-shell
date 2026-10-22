#pragma once

#include <string>
#include <string_view>

namespace astralia::lock::pam {

struct Result {
    bool success = false;
    std::string message;
};

Result authenticate(std::string_view user, std::string_view password, std::string_view pam_dir);

void secure_clear(std::string &value);

} // namespace astralia::lock::pam
