#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <pango/pangocairo.h>
#include <string>
#include <vector>

#include "wayland/render/text_elide.h"
#include "wayland/render/texture.h"

inline constexpr const char *ASTRALIA_SHELL_FONT = "ComicShannsMono Nerd Font 13";

inline constexpr const char *ASTRALIA_SHELL_FONT_SMALL = "ComicShannsMono Nerd Font 9";

inline constexpr const char *ASTRALIA_SHELL_FONT_LARGE =
    "ComicShannsMono Nerd Font Bold 20";

struct RasterizedText {
    int width = 0;
    int height = 0;
    int32_t scale = 1;
    std::vector<uint8_t> rgba;
};

RasterizedText surface_to_rgba(cairo_surface_t *surface, int width, int height);

Texture make_texture_from_raster(const RasterizedText &raster, bool mipmapped = false);

Texture make_text_texture(const std::string &text, int32_t scale = 1);

PangoFontDescription *astralia_shell_font_description();

PangoFontDescription *astralia_shell_font_description_small();

PangoFontDescription *astralia_shell_font_description_large();

cairo_font_options_t *astralia_shell_font_options();

cairo_font_options_t *astralia_shell_icon_font_options();

void font_ascent_descent(PangoFontDescription *desc, int &ascent, int &descent);

float astralia_shell_text_advance();

float text_advance_px(int px, bool bold = false);

RasterizedText rasterize_text_with(const std::string &text, PangoFontDescription *desc, int32_t scale = 1, int max_width_px = 0, bool wrap = false);

RasterizedText rasterize_text(const std::string &text, int32_t scale = 1, int max_width_px = 0);

RasterizedText rasterize_text_small(const std::string &text, int32_t scale = 1, int max_width_px = 0);

RasterizedText rasterize_text_large(const std::string &text, int32_t scale = 1, int max_width_px = 0);

RasterizedText rasterize_text_px(const std::string &text, int px, bool bold = false, int32_t scale = 1, int max_width_px = 0, bool wrap = false);
