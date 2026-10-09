#include <cassert>
#include <cstdio>
#include <filesystem>
#include <jpeglib.h>
#include <vector>

#include "wayland/render/image.h"

static bool has_nonzero_byte(const unsigned char *data, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (data[i] != 0)
            return true;
    return false;
}

static void test_decode_png() {
    int width = 0, height = 0;
    unsigned char *data =
        load_image_decode(ASTRALIA_SHELL_DEFAULT_WALLPAPER, width, height);
    assert(data);
    assert(width == 1920);
    assert(height == 1080);
    assert(has_nonzero_byte(data, static_cast<size_t>(width) * height * 4));
    delete[] data;
}

static void test_decode_svg() {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "astralia-shell-test-icon.svg";
    std::FILE *f = std::fopen(path.c_str(), "w");
    assert(f);
    std::fputs("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
               "<rect width='24' height='24' fill='red'/></svg>",
               f);
    std::fclose(f);

    int width = 0, height = 0;
    unsigned char *data = load_image_decode(path.string(), width, height, 32);
    assert(data);
    assert(width == 32);
    assert(height == 32);
    assert(has_nonzero_byte(data, static_cast<size_t>(width) * height * 4));
    assert(data[0] == 255 && data[1] == 0 && data[2] == 0 && data[3] == 255);
    delete[] data;

    assert(!load_image_decode(path.string(), width, height));

    std::filesystem::remove(path);
}

static void test_decode_truncated() {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "astralia-shell-test-truncated";
    std::filesystem::copy_file(ASTRALIA_SHELL_DEFAULT_WALLPAPER, path, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(path, std::filesystem::file_size(path) / 2);
    int width = 0, height = 0;
    assert(!load_image_decode(path.string(), width, height));

    std::FILE *f = std::fopen(path.c_str(), "wb");
    assert(f);
    std::fputs("\xFF\xD8\xFF\xE0garbage", f);
    std::fclose(f);
    assert(!load_image_decode(path.string(), width, height));

    std::filesystem::remove(path);
}

static void write_test_jpeg(const std::filesystem::path &path, int width, int height) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    assert(f);
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = static_cast<JDIMENSION>(width);
    cinfo.image_height = static_cast<JDIMENSION>(height);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, TRUE);
    std::vector<unsigned char> row(static_cast<size_t>(width) * 3, 200);
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned char *p = row.data();
        jpeg_write_scanlines(&cinfo, &p, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    std::fclose(f);
}

static void test_decode_jpeg_size_hint() {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "astralia-shell-test-hint.jpg";
    write_test_jpeg(path, 640, 480);

    int width = 0, height = 0;
    unsigned char *data = load_image_decode(path.string(), width, height);
    assert(data && width == 640 && height == 480);
    delete[] data;

    data = load_image_decode(path.string(), width, height, 0, 100, 75);
    assert(data && width == 160 && height == 120);
    assert(has_nonzero_byte(data, static_cast<size_t>(width) * height * 4));
    delete[] data;

    data = load_image_decode(path.string(), width, height, 0, 400, 300);
    assert(data && width == 640 && height == 480);
    delete[] data;

    data = load_image_decode(path.string(), width, height, 0, 100, 400);
    assert(data && width >= 100 && height >= 400);
    delete[] data;

    std::filesystem::remove(path);
}

void test_image_decode() {
    test_decode_png();
    test_decode_svg();
    test_decode_truncated();
    test_decode_jpeg_size_hint();
}
