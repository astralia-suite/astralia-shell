#pragma once

#include <cstddef>
#include <string_view>

namespace astralia::polkit {

std::size_t utf8_length(std::string_view text);
double card_height(bool show_info);
std::size_t visible_dots(std::size_t length, double width);

} // namespace astralia::polkit
