#pragma once

#include <cairo.h>
#include <string>

#include "service/settings_service.h"
#include "service/wallpaper_service.h"

namespace astralia {

void paint_wallpaper(cairo_t *cr, const Config &config, const std::string &output_name, double x,
                     double y, int width, int height, WallpaperSlot slot = WallpaperSlot::still);
std::string wallpaper_signature(const Config &config, const std::string &output_name,
                                WallpaperSlot slot = WallpaperSlot::still);

} // namespace astralia
