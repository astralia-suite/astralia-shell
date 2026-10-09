#include <filesystem>

#include "render/decode.h"

#include "wayland/core/log.h"

#include "wayland/render/image.h"

unsigned char *load_image_decode(const std::string &path, int &width, int &height, int svg_target_px, int hint_w, int hint_h) {
    auto jpeg_scale = [hint_w, hint_h](int w, int h) { return hint_w > 0 && hint_h > 0 ? astralia::cover(w, h, hint_w, hint_h).scale : 1.0; };
    auto pixels = astralia::decode_pixels(path, jpeg_scale, svg_target_px, svg_target_px);
    if (!pixels) {
        klog("image: failed to load '%s': %s", path.c_str(), pixels.error().c_str());
        return nullptr;
    }
    // Textures take straight alpha.
    if (pixels->premultiplied) {
        uint8_t *p = pixels->data.get();
        for (size_t i = 0, n = static_cast<size_t>(pixels->width) * pixels->height; i < n; ++i, p += 4) {
            if (p[3] != 0 && p[3] != 255) {
                p[0] = static_cast<uint8_t>(p[0] * 255 / p[3]);
                p[1] = static_cast<uint8_t>(p[1] * 255 / p[3]);
                p[2] = static_cast<uint8_t>(p[2] * 255 / p[3]);
            }
        }
    }
    width = pixels->width;
    height = pixels->height;
    return pixels->data.release();
}

namespace {

Texture load_image_texture(const std::string &path, int svg_target_px) {
    int width = 0, height = 0;
    unsigned char *data = load_image_decode(path, width, height, svg_target_px);
    if (!data)
        return Texture{};
    Texture tex = make_texture_rgba(width, height, data, true);
    delete[] data;
    return tex;
}

} // namespace

Texture load_image_texture_first_existing(std::initializer_list<const char *> candidates, int svg_target_px) {
    for (const char *candidate : candidates)
        if (std::filesystem::exists(candidate))
            return load_image_texture(candidate, svg_target_px);
    return Texture{};
}
