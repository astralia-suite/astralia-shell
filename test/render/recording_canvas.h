#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "render/canvas.h"

namespace test {

struct Op {
    enum class Kind {
        rect,
        rounded,
        fillet,
        erase,
        text,
        image,
        begin_group,
        end_group,
        opacity,
        polygon,
        cut_polygon,
        snapshot,
        snapshot_polygon
    } kind;
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
    std::vector<std::pair<std::string, int>> thumbnails;

    void rect(const astralia::ui::Box &box, const astralia::Color &fill) override {
        ops.push_back({Op::Kind::rect, box, 0, 0, "", fill});
    }
    void rounded(const astralia::ui::Box &box, float radius, const astralia::Color &fill,
                 float border_width, const astralia::Color &) override {
        ops.push_back({Op::Kind::rounded, box, radius, border_width, "", fill});
    }
    void fillet(const astralia::ui::Box &box, bool circle_on_right,
                const astralia::Color &color) override {
        ops.push_back({Op::Kind::fillet, box, 0, 0, "", color, circle_on_right ? 1.0f : 0.0f});
    }
    void set_erase(bool on) override {
        ops.push_back({Op::Kind::erase, {}, 0, 0, "", {}, on ? 1.0f : 0.0f});
    }
    astralia::ui::TextSize measure(std::string_view text,
                                   const astralia::ui::TextStyle &style) override {
        if (text.empty()) {
            return {};
        }
        if (style.family == astralia::ui::FontFamily::icon) {
            return {static_cast<float>(style.px), static_cast<float>(style.px)};
        }
        return {glyph_w * static_cast<float>(text.size()), glyph_h};
    }
    float advance(const astralia::ui::TextStyle &) override { return glyph_w; }
    void text(std::string_view text, const astralia::ui::TextStyle &, float x, float y,
              const astralia::Color &color) override {
        ops.push_back({Op::Kind::text, {x, y, 0, 0}, 0, 0, std::string(text), color});
    }
    astralia::ui::ImageId image(std::string_view, int) override { return image_id; }
    astralia::ui::ImageId thumbnail(std::string_view path, int px) override {
        thumbnails.emplace_back(std::string(path), px);
        return image_id;
    }
    astralia::ui::TextSize image_size(astralia::ui::ImageId) override { return {64, 64}; }
    void draw_image(astralia::ui::ImageId, const astralia::ui::Box &box,
                    const astralia::Color &tint) override {
        ops.push_back({Op::Kind::image, box, 0, 0, "", tint});
    }
    void begin_group(const astralia::ui::Box &box,
                     const astralia::ui::GroupOptions &options) override {
        ops.push_back({Op::Kind::begin_group, box, 0, 0, "", {}, options.scale});
    }
    void end_group() override { ops.push_back({Op::Kind::end_group, {}, 0, 0, "", {}}); }
    void set_opacity(float opacity) override {
        ops.push_back({Op::Kind::opacity, {}, 0, 0, "", {}, opacity});
    }

    void polygon(std::span<const astralia::ui::Point> points,
                 const astralia::Color &fill) override {
        ops.push_back(
            {Op::Kind::polygon, bounds(points), 0, 0, "", fill, static_cast<float>(points.size())});
    }
    void cut_polygon(std::span<const astralia::ui::Point> points) override {
        ops.push_back({Op::Kind::cut_polygon,
                       bounds(points),
                       0,
                       0,
                       "",
                       {},
                       static_cast<float>(points.size())});
    }

    void snapshot() override { ops.push_back({Op::Kind::snapshot, {}, 0, 0, "", {}}); }
    void snapshot_polygon(std::span<const astralia::ui::Point>,
                          std::span<const astralia::ui::Point> target, float alpha) override {
        ops.push_back({Op::Kind::snapshot_polygon, bounds(target), 0, 0, "", {}, alpha});
    }

    static astralia::ui::Box bounds(std::span<const astralia::ui::Point> points) {
        if (points.empty()) {
            return {};
        }
        float x0 = points[0].x;
        float y0 = points[0].y;
        float x1 = x0;
        float y1 = y0;
        for (const astralia::ui::Point &point : points) {
            x0 = std::min(x0, point.x);
            y0 = std::min(y0, point.y);
            x1 = std::max(x1, point.x);
            y1 = std::max(y1, point.y);
        }
        return {x0, y0, x1 - x0, y1 - y0};
    }

    size_t count(Op::Kind kind) const {
        size_t n = 0;
        for (const Op &op : ops) {
            n += op.kind == kind;
        }
        return n;
    }
};

} // namespace test
