#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/canvas.h"

#include "wayland/render/renderer.h"
#include "wayland/render/texture.h"
#include "wayland/render/texture_cache.h"
#include "wayland/render/video_texture.h"

class GlCanvas final : public astralia::ui::Canvas {
  public:
    ~GlCanvas() override { *alive_ = false; }

    void bind(Renderer &renderer) { renderer_ = &renderer; }
    void bind_context(std::function<void()> make_current) { make_current_ = std::move(make_current); }
    void begin(int32_t scale);
    void flush();

    void rect(const astralia::ui::Box &box, const astralia::Color &fill) override;
    void rounded(const astralia::ui::Box &box, float radius, const astralia::Color &fill, float border_width, const astralia::Color &border) override;
    astralia::ui::TextSize measure(std::string_view text, const astralia::ui::TextStyle &style) override;
    float advance(const astralia::ui::TextStyle &style) override;
    void text(std::string_view text, const astralia::ui::TextStyle &style, float x, float y, const astralia::Color &color) override;
    astralia::ui::ImageId image(std::string_view asset, int target_px) override;
    astralia::ui::ImageId thumbnail(std::string_view path, int px) override;
    astralia::ui::TextSize image_size(astralia::ui::ImageId id) override;
    void draw_image(astralia::ui::ImageId id, const astralia::ui::Box &box, const astralia::Color &tint) override;
    void begin_group(const astralia::ui::Box &box, const astralia::ui::GroupOptions &options) override;
    void end_group() override;
    void set_opacity(float opacity) override { renderer_->set_opacity(opacity); }
    void gauge(const astralia::ui::Box &box, float stroke, float value, const astralia::Color &fill) override;

    void texture(const Texture &tex, const astralia::ui::Box &box, const float tint[4], float radius = 0.0f);
    void texture(const Texture &tex, float x, float y, const float tint[4]);
    void video(const VideoTexture &tex, const astralia::ui::Box &box);
    void set_erase(bool on) { erase_ = on; }
    Renderer &renderer() { return *renderer_; }

  private:
    struct Group {
        float x = 0;
        float y = 0;
        bool clip = false;
        bool transformed = false;
    };

    float ox() const { return groups_.empty() ? 0.0f : groups_.back().x; }
    float oy() const { return groups_.empty() ? 0.0f : groups_.back().y; }
    template <typename Draw>
    void shape(const Draw &draw);
    const Texture *glyphs(std::string_view text, const astralia::ui::TextStyle &style);

    Renderer *renderer_ = nullptr;
    TextureCache cache_;
    std::vector<Group> groups_;
    std::vector<std::unique_ptr<Texture>> images_;
    std::unordered_map<std::string, astralia::ui::ImageId> image_ids_;
    struct Thumb {
        astralia::ui::ImageId id = astralia::ui::no_image;
        bool pending = false;
    };

    void evict_thumbnails();

    std::unordered_map<std::string, Thumb> thumbs_;
    std::deque<std::string> thumb_order_;
    std::vector<astralia::ui::ImageId> free_slots_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::function<void()> make_current_;
    size_t in_flight_ = 0;
    int32_t scale_ = 1;
    bool erase_ = false;
};
