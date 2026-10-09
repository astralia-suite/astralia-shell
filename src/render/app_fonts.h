#pragma once

#include <cairo.h>

namespace astralia {

inline constexpr const char *icon_font_family = "tabler-icons";
inline constexpr const char *glyph_font_family = "Yuji Mai";

bool register_app_fonts();

// Unhinted greyscale outlines: hinting thins Tabler's strokes, while text stays hinted.
const cairo_font_options_t *icon_font_options();

} // namespace astralia
