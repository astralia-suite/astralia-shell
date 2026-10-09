#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <jpeglib.h>

#include "render/image_decode.h"

namespace astralia {

namespace {

struct CloseFile {
    void operator()(std::FILE *file) const { std::fclose(file); }
};

struct JpegError {
    jpeg_error_mgr manager;
    std::jmp_buf jump;
};

uint32_t premultiply(uint8_t channel, uint8_t alpha) { return (channel * alpha + 127) / 255; }

// Rewrites the RGBA buffer in place as cairo's premultiplied ARGB32 and hands it to the surface.
SurfacePtr adopt(Pixels pixels) {
    auto *words = reinterpret_cast<uint32_t *>(pixels.data.get());
    const uint8_t *in = pixels.data.get();
    for (size_t i = 0, count = static_cast<size_t>(pixels.width) * pixels.height; i < count; ++i, in += 4) {
        uint8_t alpha = in[3];
        uint32_t r = pixels.premultiplied ? in[0] : premultiply(in[0], alpha);
        uint32_t g = pixels.premultiplied ? in[1] : premultiply(in[1], alpha);
        uint32_t b = pixels.premultiplied ? in[2] : premultiply(in[2], alpha);
        words[i] = static_cast<uint32_t>(alpha) << 24 | r << 16 | g << 8 | b;
    }
    SurfacePtr surface(cairo_image_surface_create_for_data(pixels.data.get(), CAIRO_FORMAT_ARGB32, pixels.width, pixels.height, pixels.width * 4));
    static cairo_user_data_key_t key;
    if (cairo_surface_set_user_data(surface.get(), &key, pixels.data.get(), [](void *data) { delete[] static_cast<uint8_t *>(data); }) == CAIRO_STATUS_SUCCESS) {
        pixels.data.release();
    }
    return surface;
}

double fit_scale(double width, double height, int fit_size) {
    return fit_size > 0 ? std::min(fit_size / width, fit_size / height) : 1.0;
}

SurfacePtr scaled(SurfacePtr source, int fit_size) {
    int width = cairo_image_surface_get_width(source.get());
    int height = cairo_image_surface_get_height(source.get());
    double scale = fit_scale(width, height, fit_size);
    if (scale == 1.0) {
        return source;
    }
    int target_width = std::max(1, static_cast<int>(std::lround(width * scale)));
    int target_height = std::max(1, static_cast<int>(std::lround(height * scale)));
    SurfacePtr target(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_width, target_height));
    cairo_t *cr = cairo_create(target.get());
    cairo_scale(cr, static_cast<double>(target_width) / width,
                static_cast<double>(target_height) / height);
    cairo_set_source_surface(cr, source.get(), 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    return target;
}

} // namespace

std::expected<SurfacePtr, std::string> decode_image(const std::string &path, int fit_size) {
    auto pixels = decode_pixels(path, [fit_size](int width, int height) { return fit_scale(width, height, fit_size); }, fit_size, fit_size);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    return scaled(adopt(std::move(*pixels)), fit_size);
}

std::expected<SurfacePtr, std::string> decode_cover(const std::string &path, int width, int height) {
    int svg_size = 2 * std::max(width, height);
    auto pixels = decode_pixels(path, [width, height](int image_width, int image_height) { return cover(image_width, image_height, width, height).scale; }, svg_size, svg_size);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    SurfacePtr source = adopt(std::move(*pixels));
    cairo_surface_t *image = source.get();
    Placement placement = cover(cairo_image_surface_get_width(image), cairo_image_surface_get_height(image), width, height);
    SurfacePtr target(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height));
    cairo_t *cr = cairo_create(target.get());
    cairo_translate(cr, placement.x, placement.y);
    cairo_scale(cr, placement.scale, placement.scale);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    return target;
}

bool surface_opaque(cairo_surface_t *surface) {
    cairo_surface_flush(surface);
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    const uint8_t *pixels = cairo_image_surface_get_data(surface);
    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const uint32_t *>(pixels + static_cast<std::size_t>(y) * stride);
        for (int x = 0; x < width; ++x) {
            if (row[x] >> 24 != 0xff) {
                return false;
            }
        }
    }
    return true;
}

bool write_jpeg(cairo_surface_t *surface, const char *path, int quality) {
    std::unique_ptr<std::FILE, CloseFile> file(std::fopen(path, "wb"));
    if (!file) {
        return false;
    }
    cairo_surface_flush(surface);
    jpeg_compress_struct encoder;
    JpegError error;
    encoder.err = jpeg_std_error(&error.manager);
    error.manager.error_exit = [](j_common_ptr info) { std::longjmp(reinterpret_cast<JpegError *>(info->err)->jump, 1); };
    error.manager.output_message = [](j_common_ptr) {};
    if (setjmp(error.jump) != 0) {
        jpeg_destroy_compress(&encoder);
        return false;
    }
    jpeg_create_compress(&encoder);
    jpeg_stdio_dest(&encoder, file.get());
    encoder.image_width = static_cast<JDIMENSION>(cairo_image_surface_get_width(surface));
    encoder.image_height = static_cast<JDIMENSION>(cairo_image_surface_get_height(surface));
    encoder.input_components = 4;
    encoder.in_color_space = JCS_EXT_BGRX;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, quality, TRUE);
    jpeg_start_compress(&encoder, TRUE);
    uint8_t *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    while (encoder.next_scanline < encoder.image_height) {
        JSAMPROW row = pixels + static_cast<std::size_t>(encoder.next_scanline) * stride;
        jpeg_write_scanlines(&encoder, &row, 1);
    }
    jpeg_finish_compress(&encoder);
    jpeg_destroy_compress(&encoder);
    return std::fflush(file.get()) == 0;
}

} // namespace astralia
