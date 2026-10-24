#include <algorithm>
#include <cmath>
#include <thread>

#include "core/deferred_call.h"

#include "wayland/core/log.h"
#include "wayland/render/arc_gauge.h"
#include "wayland/render/gl_canvas.h"
#include "wayland/render/icon.h"
#include "wayland/render/image.h"
#include "wayland/render/text.h"

#include "service/wayland/media_service.h"

namespace ui = astralia::ui;

namespace {

float texture_width(const Texture &tex) {
    return static_cast<float>(tex.width) / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
}

float texture_height(const Texture &tex) {
    return static_cast<float>(tex.height) / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
}

constexpr float white[4] = {1, 1, 1, 1};

} // namespace

void GlCanvas::begin(int32_t scale) {
    if (scale != scale_) {
        cache_.clear();
        scale_ = scale;
    }
    groups_.clear();
    erase_ = false;
    renderer_->set_opacity(1.0f);
}

void GlCanvas::flush() {
    while (!groups_.empty()) {
        end_group();
    }
    renderer_->set_opacity(1.0f);
}

template <typename Draw> void GlCanvas::shape(const Draw &draw) {
    if (erase_) {
        renderer_->set_erase_blend(true);
        draw(true);
        renderer_->set_erase_blend(false);
    }
    draw(false);
}

void GlCanvas::fillet(const ui::Box &box, bool circle_on_right, const astralia::Color &color) {
    shape([&](bool erase) {
        renderer_->draw_fillet(ox() + box.x, oy() + box.y, box.w, circle_on_right,
                               erase ? white : astralia::rgba(color));
    });
}

const Texture *GlCanvas::glyphs(std::string_view text, const ui::TextStyle &style) {
    if (text.empty()) {
        return nullptr;
    }
    std::string key =
        std::string("cv:") +
        (style.family == ui::FontFamily::icon
             ? "i"
             : (style.family == ui::FontFamily::glyph ? "g" : (style.bold ? "b" : "n"))) +
        std::to_string(style.px) + ":" + std::to_string(style.max_width) +
        (style.wrap ? "w:" : ":") + std::string(text);
    return cache_.get(key, [&] {
        if (style.family == ui::FontFamily::icon) {
            return rasterize_icon(std::string(text), scale_, style.px);
        }
        if (style.family == ui::FontFamily::glyph) {
            return rasterize_display_glyph(std::string(text), scale_, style.px);
        }
        return rasterize_text_px(std::string(text), style.px, style.bold, scale_, style.max_width,
                                 style.wrap);
    });
}

void GlCanvas::rect(const ui::Box &box, const astralia::Color &fill) {
    shape([&](bool erase) {
        renderer_->draw_rect(ox() + box.x, oy() + box.y, box.w, box.h,
                             erase ? white : astralia::rgba(fill));
    });
}

void GlCanvas::polygon(std::span<const ui::Point> points, const astralia::Color &fill) {
    std::vector<float> xy;
    xy.reserve(points.size() * 2);
    for (const ui::Point &point : points) {
        xy.push_back(ox() + point.x);
        xy.push_back(oy() + point.y);
    }
    int count = static_cast<int>(points.size());
    shape([&](bool erase) {
        renderer_->draw_polygon(xy.data(), count, erase ? white : astralia::rgba(fill));
    });
}

void GlCanvas::cut_polygon(std::span<const ui::Point> points) {
    std::vector<float> xy;
    xy.reserve(points.size() * 2);
    for (const ui::Point &point : points) {
        xy.push_back(ox() + point.x);
        xy.push_back(oy() + point.y);
    }
    renderer_->set_erase_blend(true);
    renderer_->draw_polygon(xy.data(), static_cast<int>(points.size()), white);
    renderer_->set_erase_blend(false);
}

void GlCanvas::snapshot() { renderer_->grab_framebuffer(snapshot_); }

void GlCanvas::snapshot_polygon(std::span<const ui::Point> source,
                                std::span<const ui::Point> target, float alpha) {
    if (!snapshot_.id) {
        cut_polygon(target);
        return;
    }
    float width = static_cast<float>(snapshot_.width) / static_cast<float>(snapshot_.scale);
    float height = static_cast<float>(snapshot_.height) / static_cast<float>(snapshot_.scale);
    size_t count = std::min(source.size(), target.size());
    std::vector<float> xy_uv;
    xy_uv.reserve(count * 4);
    for (size_t i = 0; i < count; ++i) {
        xy_uv.push_back(ox() + target[i].x);
        xy_uv.push_back(oy() + target[i].y);
        xy_uv.push_back(source[i].x / width);
        xy_uv.push_back(1.0f - source[i].y / height);
    }
    const float tint[4] = {1.0f, 1.0f, 1.0f, alpha};
    renderer_->draw_textured_polygon(xy_uv.data(), static_cast<int>(count), snapshot_, tint);
}

void GlCanvas::rounded(const ui::Box &box, float radius, const astralia::Color &fill,
                       float border_width, const astralia::Color &border) {
    shape([&](bool erase) {
        renderer_->draw_rounded_rect(ox() + box.x, oy() + box.y, box.w, box.h, radius, border_width,
                                     erase ? white : astralia::rgba(fill),
                                     erase ? white : astralia::rgba(border));
    });
}

ui::TextSize GlCanvas::measure(std::string_view text, const ui::TextStyle &style) {
    const Texture *tex = glyphs(text, style);
    if (tex == nullptr) {
        return {};
    }
    return {texture_width(*tex), texture_height(*tex)};
}

float GlCanvas::advance(const ui::TextStyle &style) {
    return text_advance_px(style.px, style.bold);
}

void GlCanvas::text(std::string_view text, const ui::TextStyle &style, float x, float y,
                    const astralia::Color &tint) {
    const Texture *tex = glyphs(text, style);
    if (tex != nullptr) {
        texture(*tex, {x, y, texture_width(*tex), texture_height(*tex)}, astralia::rgba(tint));
    }
}

ui::ImageId GlCanvas::image(std::string_view asset, int target_px) {
    std::string name(asset);
    if (auto found = image_ids_.find(name); found != image_ids_.end()) {
        return found->second;
    }
    std::string installed =
        name.starts_with('/') ? name : std::string(ASTRALIA_ASSET_DIR) + "/" + name;
    std::string source =
        name.starts_with('/') ? name : std::string(ASTRALIA_SOURCE_ASSET_DIR) + "/" + name;
    Texture tex = load_image_texture_first_existing({installed.c_str(), source.c_str()}, target_px);
    ui::ImageId id = ui::no_image;
    if (tex.id != 0) {
        images_.push_back(std::make_unique<Texture>(std::move(tex)));
        id = static_cast<ui::ImageId>(images_.size() - 1);
    } else {
        klog("canvas: cannot load image %s", name.c_str());
    }
    image_ids_.emplace(std::move(name), id);
    return id;
}

constexpr size_t thumb_cache = 160;
constexpr size_t thumb_in_flight = 2;

void GlCanvas::evict_thumbnails() {
    while (thumb_order_.size() > thumb_cache) {
        auto it = thumbs_.find(thumb_order_.back());
        thumb_order_.pop_back();
        if (it == thumbs_.end()) {
            continue;
        }
        if (it->second.id != ui::no_image) {
            images_[static_cast<size_t>(it->second.id)].reset();
            free_slots_.push_back(it->second.id);
        }
        thumbs_.erase(it);
    }
}

void GlCanvas::start_thumbnail(const std::string &key, int px) {
    thumbs_[key].pending = true;
    ++in_flight_;
    std::weak_ptr<bool> alive = alive_;
    std::thread([this, alive, key, px] {
        int w = 0;
        int h = 0;
        unsigned char *data = animate_decode_scaled(key, px, px, w, h);
        astralia::DeferredCall::call_later([this, alive, key, px, data, w, h] {
            if (!alive.lock()) {
                delete[] data;
                return;
            }
            --in_flight_;
            auto it = thumbs_.find(key);
            if (it == thumbs_.end()) {
                delete[] data;
                return;
            }
            it->second.pending = false;
            it->second.px = px;
            if (data != nullptr) {
                if (make_current_) {
                    make_current_();
                }
                auto texture = std::make_unique<Texture>(make_texture_rgba(w, h, data, true));
                delete[] data;
                if (it->second.id != astralia::ui::no_image) {
                    images_[static_cast<size_t>(it->second.id)] = std::move(texture);
                } else if (!free_slots_.empty()) {
                    it->second.id = free_slots_.back();
                    free_slots_.pop_back();
                    images_[static_cast<size_t>(it->second.id)] = std::move(texture);
                } else {
                    images_.push_back(std::move(texture));
                    it->second.id = static_cast<astralia::ui::ImageId>(images_.size() - 1);
                }
                evict_thumbnails();
            }
            if (on_image_ready) {
                on_image_ready();
            }
        });
    }).detach();
}

ui::ImageId GlCanvas::thumbnail(std::string_view path, int px) {
    std::string key(path);
    auto found = thumbs_.find(key);
    if (found != thumbs_.end()) {
        Thumb &thumb = found->second;
        if (thumb.id != ui::no_image) {
            auto order = std::ranges::find(thumb_order_, key);
            if (order != thumb_order_.end() && order != thumb_order_.begin()) {
                thumb_order_.erase(order);
                thumb_order_.push_front(key);
            }
            if (px > thumb.px && !thumb.pending && in_flight_ < thumb_in_flight) {
                start_thumbnail(key, px);
            }
        }
        return thumb.id;
    }
    if (in_flight_ >= thumb_in_flight) {
        return ui::no_image;
    }
    thumb_order_.push_front(key);
    start_thumbnail(key, px);
    return ui::no_image;
}

ui::TextSize GlCanvas::image_size(ui::ImageId id) {
    if (id < 0 || static_cast<size_t>(id) >= images_.size()) {
        return {};
    }
    const Texture &tex = *images_[static_cast<size_t>(id)];
    return {texture_width(tex), texture_height(tex)};
}

void GlCanvas::draw_image(ui::ImageId id, const ui::Box &box, const astralia::Color &tint) {
    if (id < 0 || static_cast<size_t>(id) >= images_.size()) {
        return;
    }
    texture(*images_[static_cast<size_t>(id)], box, astralia::rgba(tint));
}

void GlCanvas::gauge(const ui::Box &box, float stroke, float value, const astralia::Color &fill) {
    if (const Texture *tex = cached_arc_gauge(cache_, scale_, box.w, stroke, value, fill)) {
        texture(*tex, std::round(box.x), std::round(box.y),
                astralia::rgba(astralia::palette::white));
    }
}

void GlCanvas::begin_group(const ui::Box &box, const ui::GroupOptions &options) {
    Group group{ox() + box.x, oy() + box.y};
    group.transformed = options.scale != 1.0f || options.rotation != 0.0f;
    if (group.transformed) {
        float cx = group.x + box.w * 0.5f;
        float cy = group.y + box.h * 0.5f;
        renderer_->push_model(Affine2D::translation(cx, cy)
                                  .compose(Affine2D::scaling(options.scale))
                                  .compose(Affine2D::rotation_deg(options.rotation))
                                  .compose(Affine2D::translation(-cx, -cy)));
    }
    group.clip = options.clip && !group.transformed;
    if (group.clip) {
        renderer_->set_clip(group.x, group.y, box.w, box.h);
    }
    groups_.push_back(group);
}

void GlCanvas::end_group() {
    if (groups_.empty()) {
        return;
    }
    Group group = groups_.back();
    groups_.pop_back();
    if (group.clip) {
        renderer_->clear_clip();
    }
    if (group.transformed) {
        renderer_->pop_model();
    }
}

void GlCanvas::texture(const Texture &tex, const ui::Box &box, const float tint[4], float radius) {
    if (tex.id == 0) {
        return;
    }
    float x = ox() + box.x;
    float y = oy() + box.y;
    shape([&](bool erase) {
        if (radius > 0.0f) {
            renderer_->draw_texture_rect_rounded(x, y, box.w, box.h, radius, tex,
                                                 erase ? white : tint);
        } else {
            renderer_->draw_texture_rect(x, y, box.w, box.h, tex, erase ? white : tint);
        }
    });
}

void GlCanvas::texture(const Texture &tex, float x, float y, const float tint[4]) {
    texture(tex, {x, y, texture_width(tex), texture_height(tex)}, tint);
}

void GlCanvas::video(const VideoTexture &tex, const ui::Box &box) {
    if (tex.tex != 0) {
        renderer_->draw_video_texture_rect(ox() + box.x, oy() + box.y, box.w, box.h, tex);
    }
}
