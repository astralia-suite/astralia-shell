#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <jpeglib.h>
#include <resvg/resvg.h>
#include <string_view>

#include "render/decode.h"

namespace {

// stb allocates through `new[]` so its buffers share `Pixels`' ownership.
void *stb_malloc(size_t size) { return new uint8_t[size]; }

void stb_free(void *data) { delete[] static_cast<uint8_t *>(data); }

void *stb_realloc(void *data, size_t old_size, size_t new_size) {
    auto *grown = new uint8_t[new_size];
    if (data != nullptr) {
        std::memcpy(grown, data, std::min(old_size, new_size));
        stb_free(data);
    }
    return grown;
}

} // namespace

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MALLOC(size) stb_malloc(size)
#define STBI_FREE(data) stb_free(data)
#define STBI_REALLOC_SIZED(data, old_size, new_size) stb_realloc(data, old_size, new_size)
#include <stb/stb_image.h>

namespace astralia {

namespace {

struct CloseFile {
    void operator()(std::FILE *file) const { std::fclose(file); }
};

struct JpegError {
    jpeg_error_mgr manager;
    std::jmp_buf jump;
};

struct DestroyTree {
    void operator()(resvg_render_tree *tree) const { resvg_tree_destroy(tree); }
};

enum class Format { jpeg,
                    svg,
                    other };

Format sniff(std::FILE *file) {
    unsigned char head[64] = {};
    size_t n = std::fread(head, 1, sizeof(head), file);
    std::rewind(file);
    if (n >= 2 && head[0] == 0xFF && head[1] == 0xD8) {
        return Format::jpeg;
    }
    std::string_view text(reinterpret_cast<const char *>(head), n);
    if (text.find("<svg") != std::string_view::npos || text.find("<?xml") != std::string_view::npos) {
        return Format::svg;
    }
    return Format::other;
}

std::expected<Pixels, std::string> decode_jpeg(std::FILE *file, const std::function<double(int, int)> &jpeg_scale) {
    jpeg_decompress_struct decoder;
    JpegError error;
    decoder.err = jpeg_std_error(&error.manager);
    error.manager.error_exit = [](j_common_ptr info) { std::longjmp(reinterpret_cast<JpegError *>(info->err)->jump, 1); };
    error.manager.output_message = [](j_common_ptr) {};
    // A raw pointer, since longjmp skips destructors.
    uint8_t *volatile data = nullptr;
    if (setjmp(error.jump) != 0) {
        jpeg_destroy_decompress(&decoder);
        delete[] data;
        return std::unexpected(std::string("jpeg decode failed"));
    }
    jpeg_create_decompress(&decoder);
    jpeg_stdio_src(&decoder, file);
    jpeg_read_header(&decoder, TRUE);
    decoder.out_color_space = JCS_EXT_RGBA;
    decoder.scale_num = 1;
    decoder.scale_denom = jpeg_scale ? static_cast<unsigned>(jpeg_reduction(jpeg_scale(static_cast<int>(decoder.image_width), static_cast<int>(decoder.image_height)))) : 1;
    jpeg_start_decompress(&decoder);
    auto width = static_cast<int>(decoder.output_width);
    auto height = static_cast<int>(decoder.output_height);
    data = new uint8_t[static_cast<size_t>(width) * height * 4];
    while (decoder.output_scanline < decoder.output_height) {
        JSAMPROW row = data + static_cast<size_t>(decoder.output_scanline) * width * 4;
        jpeg_read_scanlines(&decoder, &row, 1);
    }
    jpeg_finish_decompress(&decoder);
    jpeg_destroy_decompress(&decoder);
    Pixels pixels;
    pixels.data.reset(data);
    pixels.width = width;
    pixels.height = height;
    return pixels;
}

std::expected<Pixels, std::string> decode_svg(const std::string &path, int fit_width, int fit_height) {
    static resvg_options *options = resvg_options_create();
    resvg_render_tree *raw = nullptr;
    if (int32_t status = resvg_parse_tree_from_file(path.c_str(), options, &raw); status != RESVG_OK) {
        return std::unexpected("resvg error " + std::to_string(status));
    }
    std::unique_ptr<resvg_render_tree, DestroyTree> tree(raw);
    resvg_size size = resvg_get_image_size(tree.get());
    if (size.width <= 0 || size.height <= 0) {
        return std::unexpected(std::string("empty svg"));
    }
    double scale = fit_width > 0 && fit_height > 0 ? std::min(fit_width / size.width, fit_height / size.height) : 1.0;
    Pixels pixels;
    pixels.width = std::max(1, static_cast<int>(std::lround(size.width * scale)));
    pixels.height = std::max(1, static_cast<int>(std::lround(size.height * scale)));
    pixels.premultiplied = true;
    pixels.data.reset(new uint8_t[static_cast<size_t>(pixels.width) * pixels.height * 4]());
    resvg_transform transform{static_cast<float>(scale), 0, 0, static_cast<float>(scale), 0, 0};
    resvg_render(tree.get(), transform, static_cast<uint32_t>(pixels.width), static_cast<uint32_t>(pixels.height), reinterpret_cast<char *>(pixels.data.get()));
    return pixels;
}

} // namespace

Placement cover(int image_width, int image_height, int area_width, int area_height) {
    double scale = std::max(static_cast<double>(area_width) / image_width, static_cast<double>(area_height) / image_height);
    return {scale, (area_width - image_width * scale) / 2.0, (area_height - image_height * scale) / 2.0};
}

int jpeg_reduction(double required_scale) {
    for (int reduction : {8, 4, 2}) {
        if (1.0 / reduction >= required_scale) {
            return reduction;
        }
    }
    return 1;
}

std::expected<Pixels, std::string> decode_pixels(const std::string &path, const std::function<double(int, int)> &jpeg_scale, int svg_width, int svg_height) {
    std::unique_ptr<std::FILE, CloseFile> file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        return std::unexpected(std::string("cannot open file"));
    }
    switch (sniff(file.get())) {
    case Format::jpeg:
        return decode_jpeg(file.get(), jpeg_scale);
    case Format::svg:
        return decode_svg(path, svg_width, svg_height);
    case Format::other:
        break;
    }
    Pixels pixels;
    int channels = 0;
    pixels.data.reset(stbi_load_from_file(file.get(), &pixels.width, &pixels.height, &channels, 4));
    if (!pixels.data) {
        return std::unexpected(std::string(stbi_failure_reason()));
    }
    return pixels;
}

} // namespace astralia
