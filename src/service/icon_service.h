#pragma once

#include <string>
#include <vector>

namespace astralia {

std::vector<std::string> icon_theme_order(const std::string &active);
std::string icon_direct_path(const std::string &icon_field);
std::string resolve_app_icon_path(const std::string &icon_field);
std::string resolve_window_icon_path(const std::string &window_class);

} // namespace astralia
