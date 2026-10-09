#pragma once

#include <string>
#include <vector>

#include "render/canvas.h"

namespace test {

struct Op {
    enum class Kind { rect,
                      rounded,
                      text,
                      image,
                      begin_group,
                      end_group,
                      opacity } kind;
    astralia::ui::Box box;
    float radius = 0;
    float border_width = 0;
    std::string text;
    astralia::Color color{};
    float value = 0;
};

class RecordingCanvas final : public astralia::ui::Canvas {
  public:
    std::vector<Op> ops;
    float glyph_w = 9;
    float glyph_h = 20;
    astralia::ui::ImageId image_id = 0;

    void rect(const astralia::ui::Box &box, const astralia::Color &fill) override {
        ops.push_back({Op::Kind::rect, box, 0, 0, "", fill});
    }
    void rounded(const astralia::ui::Box &box, float radius, const astralia::Color &fill, float border_width, const astralia::Color &) override {
        ops.push_back({Op::Kind::rounded, box, radius, border_width, "", fill});
    }
    astralia::ui::TextSize measure(std::string_view text, const astralia::ui::TextStyle &style) override {
        if (text.empty()) {
            return {};
        }
        if (style.family == astralia::ui::FontFamily::icon) {
            return {static_cast<float>(style.px), static_cast<float>(style.px)};
        }
        return {glyph_w * static_cast<float>(text.size()), glyph_h};
    }
    float advance(const astralia::ui::TextStyle &) override { return glyph_w; }
    void text(std::string_view text, const astralia::ui::TextStyle &, float x, float y, const astralia::Color &color) override {
        ops.push_back({Op::Kind::text, {x, y, 0, 0}, 0, 0, std::string(text), color});
    }
    astralia::ui::ImageId image(std::string_view, int) override { return image_id; }
    astralia::ui::ImageId thumbnail(std::string_view, int) override { return image_id; }
    astralia::ui::TextSize image_size(astralia::ui::ImageId) override { return {64, 64}; }
    void draw_image(astralia::ui::ImageId, const astralia::ui::Box &box, const astralia::Color &tint) override {
        ops.push_back({Op::Kind::image, box, 0, 0, "", tint});
    }
    void begin_group(const astralia::ui::Box &box, const astralia::ui::GroupOptions &options) override {
        ops.push_back({Op::Kind::begin_group, box, 0, 0, "", {}, options.scale});
    }
    void end_group() override { ops.push_back({Op::Kind::end_group, {}, 0, 0, "", {}}); }
    void set_opacity(float opacity) override { ops.push_back({Op::Kind::opacity, {}, 0, 0, "", {}, opacity}); }

    size_t count(Op::Kind kind) const {
        size_t n = 0;
        for (const Op &op : ops) {
            n += op.kind == kind;
        }
        return n;
    }
};

} // namespace test
