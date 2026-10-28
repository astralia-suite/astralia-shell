#pragma once

#include <filesystem>
#include <string>

#include "render/theme.h"

namespace astralia {

std::filesystem::path palette_export_path();
std::string palette_export_text(const Theme &theme);
std::string palette_export_css(const Theme &theme);
bool export_palette(const Theme &theme, const std::filesystem::path &path);
bool export_palette(const Theme &theme);

} // namespace astralia
