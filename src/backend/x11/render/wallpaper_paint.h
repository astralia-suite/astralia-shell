#pragma once

#include <cairo.h>
#include <string>

#include "service/settings_service.h"

namespace astralia {

void paint_wallpaper(cairo_t *cr, const Config &config, const std::string &output_name, double x, double y, int width, int height);
std::string wallpaper_signature(const Config &config, const std::string &output_name);

} // namespace astralia
