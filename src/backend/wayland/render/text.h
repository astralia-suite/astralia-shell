#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <pango/pangocairo.h>
#include <string>
#include <vector>

#include "wayland/render/texture.h"

struct RasterizedText {
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    std::vector<uint8_t> rgba;
};

RasterizedText surface_to_rgba(cairo_surface_t *surface, int width, int height);

Texture make_texture_from_raster(const RasterizedText &raster, bool mipmapped = false);

cairo_font_options_t *astralia_shell_font_options();

float text_advance_px(int px, bool bold = false);

RasterizedText rasterize_text_px(const std::string &text, int px, bool bold = false, int32_t scale = 1, int max_width_px = 0, bool wrap = false);
