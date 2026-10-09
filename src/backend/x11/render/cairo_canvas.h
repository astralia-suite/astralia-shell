#pragma once

#include <cairo.h>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/canvas.h"

#include "render/image_decode.h"
#include "render/text.h"

namespace astralia {

class CairoCanvas final : public ui::Canvas {
  public:
    ~CairoCanvas() override { *alive_ = false; }
    void bind(cairo_t *cr) { cr_ = cr; }
    void release_images();

    void rect(const ui::Box &box, const Color &fill) override;
    void rounded(const ui::Box &box, float radius, const Color &fill, float border_width, const Color &border) override;
    ui::TextSize measure(std::string_view text, const ui::TextStyle &style) override;
    float advance(const ui::TextStyle &style) override;
    void text(std::string_view text, const ui::TextStyle &style, float x, float y, const Color &color) override;
    ui::ImageId image(std::string_view asset, int target_px) override;
    ui::ImageId thumbnail(std::string_view path, int px) override;
    ui::TextSize image_size(ui::ImageId id) override;
    void draw_image(ui::ImageId id, const ui::Box &box, const Color &tint) override;
    void begin_group(const ui::Box &box, const ui::GroupOptions &options) override;
    void end_group() override;
    void set_opacity(float) override {}

  private:
    struct Face {
        ui::TextStyle style;
        std::unique_ptr<Text> layout;
    };

    Text &face(std::string_view text, const ui::TextStyle &style);

    struct Thumb {
        ui::ImageId id = ui::no_image;
        bool pending = false;
    };

    void evict_thumbnails();

    std::unordered_map<std::string, Thumb> thumbs_;
    std::deque<std::string> thumb_order_;
    std::vector<ui::ImageId> free_slots_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    uint64_t generation_ = 0;
    size_t in_flight_ = 0;
    cairo_t *cr_ = nullptr;
    std::vector<Face> faces_;
    std::vector<SurfacePtr> images_;
    std::unordered_map<std::string, ui::ImageId> image_ids_;
};

} // namespace astralia
