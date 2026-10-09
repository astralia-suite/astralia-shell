#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace astralia {

// Decoded image as packed RGBA rows. SVG renders come out premultiplied, everything else straight.
// The buffer is allocated with `new[]`, so a backend may adopt it after `release()`.
struct Pixels {
    std::unique_ptr<uint8_t[]> data;
    int width = 0;
    int height = 0;
    bool premultiplied = false;
};

struct Placement {
    double scale;
    double x;
    double y;
};

Placement cover(int image_width, int image_height, int area_width, int area_height);
int jpeg_reduction(double required_scale);

// `jpeg_scale(width, height)` is the scale the image will be drawn at, letting a JPEG decode at a
// reduced size. An SVG renders to fit `svg_width` x `svg_height`, or at its own size when both are 0.
std::expected<Pixels, std::string> decode_pixels(const std::string &path, const std::function<double(int, int)> &jpeg_scale = {}, int svg_width = 0, int svg_height = 0);

} // namespace astralia
