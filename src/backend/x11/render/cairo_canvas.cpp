#include <algorithm>
#include <cmath>
#include <format>
#include <thread>

#include "core/deferred_call.h"
#include "core/log.h"

#include "render/app_fonts.h"
#include "render/cairo_canvas.h"
#include "render/draw.h"

namespace astralia {

namespace {

std::string font_description(const ui::TextStyle &style) {
    if (style.family == ui::FontFamily::icon) {
        return std::format("{} {}px", icon_font_family, style.px);
    }
    if (style.family == ui::FontFamily::glyph) {
        return std::format("{} {}px", glyph_font_family, style.px);
    }
    return std::format("Comic Shanns Mono{} {}px", style.bold ? " Bold" : "", style.px);
}

} // namespace

Text &CairoCanvas::face(std::string_view text, const ui::TextStyle &style) {
    register_app_fonts();
    for (Face &candidate : faces_) {
        if (candidate.style == style) {
            candidate.layout->set(text);
            return *candidate.layout;
        }
    }
    auto layout = std::make_unique<Text>(font_description(style).c_str());
    if (style.family == ui::FontFamily::icon) {
        layout->set_font_options(icon_font_options());
    }
    if (style.max_width > 0) {
        if (style.wrap) {
            layout->wrap(style.max_width);
        } else {
            layout->ellipsize(style.max_width);
        }
    }
    layout->set(text);
    faces_.push_back({style, std::move(layout)});
    return *faces_.back().layout;
}

void CairoCanvas::release_images() {
    images_.clear();
    image_ids_.clear();
    thumbs_.clear();
    thumb_order_.clear();
    free_slots_.clear();
    ++generation_;
    in_flight_ = 0;
}

void CairoCanvas::rect(const ui::Box &box, const Color &fill) {
    cairo_rectangle(cr_, box.x, box.y, box.w, box.h);
    set_source(cr_, fill);
    cairo_fill(cr_);
}

void CairoCanvas::rounded(const ui::Box &box, float radius, const Color &fill, float border_width, const Color &border) {
    rounded_rect(cr_, box.x, box.y, box.w, box.h, radius);
    set_source(cr_, fill);
    cairo_fill(cr_);
    if (border_width > 0.0f) {
        double inset = border_width / 2.0;
        rounded_rect(cr_, box.x + inset, box.y + inset, box.w - border_width, box.h - border_width, radius - inset);
        set_source(cr_, border);
        cairo_set_line_width(cr_, border_width);
        cairo_stroke(cr_);
    }
}

ui::TextSize CairoCanvas::measure(std::string_view text, const ui::TextStyle &style) {
    if (text.empty()) {
        return {};
    }
    Text &layout = face(text, style);
    PangoRectangle ink = layout.ink();
    if (style.family != ui::FontFamily::text) {
        return {static_cast<float>(ink.width), static_cast<float>(ink.height)};
    }
    return {static_cast<float>(ink.width), static_cast<float>(layout.height())};
}

float CairoCanvas::advance(const ui::TextStyle &style) {
    return static_cast<float>(face("M", style).width());
}

void CairoCanvas::text(std::string_view text, const ui::TextStyle &style, float x, float y, const Color &color) {
    if (text.empty()) {
        return;
    }
    Text &layout = face(text, style);
    set_source(cr_, color);
    if (style.family != ui::FontFamily::text) {
        PangoRectangle ink = layout.ink_exact();
        layout.draw(cr_, x - static_cast<double>(ink.x) / PANGO_SCALE, y - static_cast<double>(ink.y) / PANGO_SCALE);
    } else {
        layout.draw(cr_, x - layout.ink().x, y);
    }
}

ui::ImageId CairoCanvas::image(std::string_view asset, int target_px) {
    std::string name(asset);
    if (auto found = image_ids_.find(name); found != image_ids_.end()) {
        return found->second;
    }
    ui::ImageId id = ui::no_image;
    bool absolute = name.starts_with('/');
    for (const char *dir : {ASTRALIA_ASSET_DIR, ASTRALIA_SOURCE_ASSET_DIR}) {
        auto decoded = decode_image(absolute ? name : std::string(dir) + "/" + name, target_px);
        if (decoded) {
            images_.push_back(std::move(*decoded));
            id = static_cast<ui::ImageId>(images_.size() - 1);
            break;
        }
    }
    if (id == ui::no_image) {
        log::error("canvas: cannot load image {}", name);
    }
    image_ids_.emplace(std::move(name), id);
    return id;
}

constexpr size_t thumb_cache = 80;
constexpr size_t thumb_in_flight = 2;

void CairoCanvas::evict_thumbnails() {
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

ui::ImageId CairoCanvas::thumbnail(std::string_view path, int px) {
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
        }
        return thumb.id;
    }
    if (in_flight_ >= thumb_in_flight) {
        return ui::no_image;
    }
    thumbs_[key].pending = true;
    thumb_order_.push_front(key);
    ++in_flight_;
    std::weak_ptr<bool> alive = alive_;
    uint64_t generation = generation_;
    auto decoded = std::make_shared<SurfacePtr>();
    std::thread([this, alive, key, px, generation, decoded] {
        auto surface = decode_cover(key, px, px);
        if (surface) {
            *decoded = std::move(*surface);
        }
        DeferredCall::call_later([this, alive, key, generation, decoded] {
            if (!alive.lock() || generation != generation_) {
                return;
            }
            --in_flight_;
            auto it = thumbs_.find(key);
            if (it == thumbs_.end()) {
                return;
            }
            it->second.pending = false;
            if (*decoded) {
                if (!free_slots_.empty()) {
                    it->second.id = free_slots_.back();
                    free_slots_.pop_back();
                    images_[static_cast<size_t>(it->second.id)] = std::move(*decoded);
                } else {
                    images_.push_back(std::move(*decoded));
                    it->second.id = static_cast<ui::ImageId>(images_.size() - 1);
                }
                evict_thumbnails();
            }
            if (on_image_ready) {
                on_image_ready();
            }
        });
    }).detach();
    return ui::no_image;
}

ui::TextSize CairoCanvas::image_size(ui::ImageId id) {
    if (id < 0 || static_cast<size_t>(id) >= images_.size()) {
        return {};
    }
    cairo_surface_t *surface = images_[static_cast<size_t>(id)].get();
    return {static_cast<float>(cairo_image_surface_get_width(surface)), static_cast<float>(cairo_image_surface_get_height(surface))};
}

void CairoCanvas::draw_image(ui::ImageId id, const ui::Box &box, const Color &) {
    if (id < 0 || static_cast<size_t>(id) >= images_.size()) {
        return;
    }
    cairo_surface_t *surface = images_[static_cast<size_t>(id)].get();
    double scale_x = box.w / cairo_image_surface_get_width(surface);
    double scale_y = box.h / cairo_image_surface_get_height(surface);
    cairo_save(cr_);
    cairo_translate(cr_, box.x, box.y);
    cairo_scale(cr_, scale_x, scale_y);
    cairo_set_source_surface(cr_, surface, 0, 0);
    cairo_paint(cr_);
    cairo_restore(cr_);
}

void CairoCanvas::gauge(const ui::Box &box, float stroke, float value, const Color &fill) {
    constexpr int segments = 10;
    constexpr double gap = 6.0 * M_PI / 180.0;
    double step = 2.0 * M_PI / segments;
    double span = step - gap;
    double radius = box.w / 2.0 - stroke / 2.0;
    double lit = std::clamp(value, 0.0f, 1.0f) * segments;
    cairo_save(cr_);
    cairo_set_line_width(cr_, stroke);
    cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
    for (int i = 0; i < segments; ++i) {
        double start = -M_PI / 2.0 + i * step + gap / 2.0;
        cairo_set_source_rgba(cr_, fill.r, fill.g, fill.b, 0.15);
        cairo_new_sub_path(cr_);
        cairo_arc(cr_, box.x + box.w / 2.0, box.y + box.h / 2.0, radius, start, start + span);
        cairo_stroke(cr_);
        double filled = std::clamp(lit - i, 0.0, 1.0);
        if (filled > 0.0) {
            set_source(cr_, fill);
            cairo_new_sub_path(cr_);
            cairo_arc(cr_, box.x + box.w / 2.0, box.y + box.h / 2.0, radius, start, start + span * filled);
            cairo_stroke(cr_);
        }
    }
    cairo_restore(cr_);
}

void CairoCanvas::begin_group(const ui::Box &box, const ui::GroupOptions &options) {
    cairo_save(cr_);
    cairo_translate(cr_, box.x, box.y);
    if (options.scale != 1.0f) {
        cairo_translate(cr_, box.w / 2.0, box.h / 2.0);
        cairo_scale(cr_, options.scale, options.scale);
        cairo_translate(cr_, -box.w / 2.0, -box.h / 2.0);
    }
    if (options.clip && options.radius > 0.0f) {
        rounded_rect(cr_, 0, 0, box.w, box.h, options.radius);
        cairo_clip(cr_);
    } else if (options.clip) {
        cairo_rectangle(cr_, 0, 0, box.w, box.h);
        cairo_clip(cr_);
    }
}

void CairoCanvas::end_group() {
    cairo_restore(cr_);
}

} // namespace astralia
