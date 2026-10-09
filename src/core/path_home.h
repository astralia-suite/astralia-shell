#pragma once

#include <string>

namespace astralia {

std::string path_collapse_home(const std::string &path);

std::string path_expand_home(const std::string &path);

} // namespace astralia
