#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include "render/geometry.h"
#include "render/tokens.h"

namespace astralia::ui {

enum class FontFamily : uint8_t {
    text,
    icon,
    glyph,
};

struct TextStyle {
    FontFamily family = FontFamily::text;
    int px = 13;
    bool bold = false;
    int max_width = 0;
    bool wrap = false;

    bool operator==(const TextStyle &) const = default;
};

struct TextSize {
    float w = 0;
    float h = 0;
};

using ImageId = int;

inline constexpr ImageId no_image = -1;

struct GroupOptions {
    float scale = 1.0f;
    bool clip = false;
    float radius = 0.0f;
};

class Canvas {
  public:
    virtual ~Canvas() = default;

    virtual void rect(const Box &box, const Color &fill) = 0;
    virtual void rounded(const Box &box, float radius, const Color &fill, float border_width = 0.0f, const Color &border = {}) = 0;

    virtual TextSize measure(std::string_view text, const TextStyle &style) = 0;
    virtual float advance(const TextStyle &style) = 0;
    virtual void text(std::string_view text, const TextStyle &style, float x, float y, const Color &color) = 0;

    virtual ImageId image(std::string_view asset, int target_px) = 0;
    virtual ImageId thumbnail(std::string_view path, int px) = 0;
    virtual TextSize image_size(ImageId id) = 0;
    virtual void draw_image(ImageId id, const Box &box, const Color &tint) = 0;

    virtual void begin_group(const Box &box, const GroupOptions &options = {}) = 0;
    virtual void end_group() = 0;

    virtual void set_opacity(float opacity) = 0;

    virtual void gauge(const Box &, float, float, const Color &) {}

    std::function<void()> on_image_ready;
};

} // namespace astralia::ui
