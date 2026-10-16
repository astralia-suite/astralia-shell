#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace astralia {

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
Placement contain(int image_width, int image_height, int area_width, int area_height);
int jpeg_reduction(double required_scale);

std::expected<Pixels, std::string>
decode_pixels(const std::string &path, const std::function<double(int, int)> &jpeg_scale = {},
              int svg_width = 0, int svg_height = 0);

} // namespace astralia
