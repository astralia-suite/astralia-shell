#pragma once

#include <cstdint>
#include <string>

#include "wayland/render/text.h"

inline constexpr int ASTRALIA_SHELL_ICON_PX = 18;

RasterizedText rasterize_icon(const std::string &codepoint_utf8, int32_t scale = 1, int px = ASTRALIA_SHELL_ICON_PX);

RasterizedText rasterize_display_glyph(const std::string &codepoint_utf8, int32_t scale = 1, int px = 55);

Texture make_icon_texture(const std::string &codepoint_utf8, int32_t scale = 1);
