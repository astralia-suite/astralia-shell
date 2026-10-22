#include <algorithm>
#include <format>

#include "core/log.h"

#include "render/cover_cache.h"
#include "render/tokens.h"
#include "render/wallpaper_paint.h"

#include "service/wallpaper_service.h"

namespace astralia {

namespace {

constexpr const char *fit_mode = "fit";

}

void paint_wallpaper(cairo_t *cr, const Config &config, const std::string &output_name, double x,
                     double y, int width, int height, WallpaperSlot slot) {
    cairo_save(cr);
    cairo_rectangle(cr, x, y, width, height);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, palette::base.r, palette::base.g, palette::base.b);
    cairo_paint(cr);
    int columns = std::max(1, wallpaper_column_count(config, output_name, slot));
    for (int i = 0; i < columns; ++i) {
        std::string path = wallpaper_column_path(config, output_name, i, slot);
        if (path.empty()) {
            continue;
        }
        int left = width * i / columns;
        int cell_width = width * (i + 1) / columns - left;
        bool contained = wallpaper_fill_mode(config, output_name, i, slot) == fit_mode;
        auto image = contained ? load_contained(path, cell_width, height)
                               : load_cover(path, cell_width, height);
        if (!image) {
            log::error("wallpaper {} for {}: {}", path, output_name, image.error());
            continue;
        }
        cairo_surface_t *surface = image->get();
        double offset_x = x + left + (cell_width - cairo_image_surface_get_width(surface)) / 2.0;
        double offset_y = y + (height - cairo_image_surface_get_height(surface)) / 2.0;
        cairo_set_source_surface(cr, surface, offset_x, offset_y);
        cairo_paint(cr);
    }
    cairo_restore(cr);
}

std::string wallpaper_signature(const Config &config, const std::string &output_name,
                                WallpaperSlot slot) {
    int columns = std::max(1, wallpaper_column_count(config, output_name, slot));
    std::string signature = std::format("{}:{}", output_name, columns);
    for (int i = 0; i < columns; ++i) {
        signature += std::format("|{}@{}", wallpaper_column_path(config, output_name, i, slot),
                                 wallpaper_fill_mode(config, output_name, i, slot));
    }
    return signature;
}

} // namespace astralia
