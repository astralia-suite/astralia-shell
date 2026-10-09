#pragma once

#include <cairo.h>
#include <expected>
#include <memory>
#include <string>

#include "render/decode.h"

namespace astralia {

struct DestroySurface {
    void operator()(cairo_surface_t *surface) const { cairo_surface_destroy(surface); }
};

using SurfacePtr = std::unique_ptr<cairo_surface_t, DestroySurface>;

std::expected<SurfacePtr, std::string> decode_image(const std::string &path, int fit_size = 0);
std::expected<SurfacePtr, std::string> decode_cover(const std::string &path, int width, int height);
bool surface_opaque(cairo_surface_t *surface);
bool write_jpeg(cairo_surface_t *surface, const char *path, int quality);

} // namespace astralia
