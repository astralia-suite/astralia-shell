#include <algorithm>
#include <cmath>

#include "modules/bar/panel/panel.h"

#include "config/icons.h"

#include "ui/tokens.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

constexpr uint64_t owner_reveal = 2;
constexpr int close_id = -1;
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::title_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

bool inside(const ui::Box &box, double x, double y) {
    return box.w > 0 && x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
}

} // namespace

Panel::Panel(std::unique_ptr<PanelContent> content, Reactor &reactor) : content_(std::move(content)), reactor_(&reactor) {
    content_->changed = [this] { changed(); };
    std::weak_ptr<bool> alive = alive_;
    timer_ = reactor.add_timer([this, alive] {
        auto live = alive.lock();
        if (!live || !open_ || content_->refresh_interval().count() == 0) {
            return std::chrono::milliseconds(std::chrono::hours(1));
        }
        return content_->refresh_interval(); }, [this, alive] {
        if (alive.lock() && open_ && !closing_ && content_->refresh_interval().count() > 0) {
            content_->refresh();
            changed();
        } });
}

void Panel::changed() {
    if (on_changed) {
        on_changed();
    }
}

void Panel::open() {
    if (open_ && !closing_) {
        return;
    }
    open_ = true;
    closing_ = false;
    reveal_ = -1.0f;
    target_ = -1.0f;
    scroll_ = 0.0f;
    content_->opened();
    reactor_->reschedule(timer_);
    changed();
}

void Panel::close() {
    if (!open_ || closing_) {
        return;
    }
    closing_ = true;
    dragging_ = false;
    content_->closed();
    animations_.animate(std::max(reveal_, 0.0f), 0.0f, cfg::reveal_ms, astralia::Easing::EaseOutCubic, [this](float v) { reveal_ = v; }, [this] {
        open_ = false;
        closing_ = false;
        reveal_ = -1.0f;
        target_ = -1.0f;
        if (on_closed) {
            on_closed();
        } }, owner_reveal);
    changed();
}

PanelRegion *Panel::region_at(double x, double y) {
    double lx = x - card_.x;
    double ly = y - card_.y;
    for (PanelRegion &region : regions_) {
        if (inside(region.box, lx, ly)) {
            return &region;
        }
    }
    return nullptr;
}

bool Panel::contains(double x, double y) const {
    return open_ && (inside(card_, x, y) || inside(dialog_, x, y));
}

bool Panel::clickable(double x, double y) const {
    if (!open_) {
        return false;
    }
    double lx = x - card_.x;
    double ly = y - card_.y;
    return std::ranges::any_of(regions_, [&](const PanelRegion &r) { return inside(r.box, lx, ly); });
}

bool Panel::press(double x, double y, input::Button button) {
    if (!open_ || closing_) {
        return false;
    }
    if (button != input::Button::Left && button != input::Button::Right) {
        return false;
    }
    if (PanelRegion *region = region_at(x, y)) {
        if (region->id == close_id) {
            close();
            return true;
        }
        PanelRegion hit = *region;
        if (hit.drag) {
            dragging_ = true;
            drag_region_ = hit;
        }
        content_->pressed = button;
        bool handled = content_->activate(hit, x - card_.x, y - card_.y);
        if (handled) {
            changed();
        }
        return true;
    }
    if (!inside(card_, x, y) && !inside(dialog_, x, y)) {
        if (content_->dismiss_dialog()) {
            changed();
        } else {
            close();
        }
    }
    return true;
}

bool Panel::move(double x, double y) {
    if (!dragging_) {
        return false;
    }
    if (content_->drag(drag_region_, x - card_.x, y - card_.y)) {
        changed();
    }
    return true;
}

bool Panel::release() {
    if (!dragging_) {
        return false;
    }
    dragging_ = false;
    content_->drop(drag_region_);
    changed();
    return true;
}

bool Panel::wheel(double x, double y, double dy) {
    if (!open_ || closing_) {
        return false;
    }
    if (content_->wheel(x - card_.x, y - card_.y, dy)) {
        changed();
        return true;
    }
    if (!content_->scrollable()) {
        return false;
    }
    float next = std::clamp(scroll_ + static_cast<float>(dy), 0.0f, std::max(0.0f, content_h_ - view_h_));
    if (next == scroll_) {
        return false;
    }
    scroll_ = next;
    changed();
    return true;
}

bool Panel::key(const input::KeyEvent &event) {
    if (!open_ || closing_) {
        return false;
    }
    if (content_->key(event)) {
        changed();
        return true;
    }
    if (event.kind == input::KeyKind::Escape) {
        close();
        return true;
    }
    return false;
}

float Panel::card_x(float surface_width) const {
    float width = content_->width();
    switch (content_->anchor()) {
    case PanelAnchor::left:
        return cfg::side_margin;
    case PanelAnchor::center:
        return (surface_width - width) / 2.0f;
    case PanelAnchor::right:
        break;
    }
    return surface_width - width - cfg::side_margin;
}

ui::Box Panel::extent() const {
    if (dialog_.w <= 0.0f) {
        return card_;
    }
    float right = std::max(card_.x + card_.w, dialog_.x + dialog_.w);
    float bottom = std::max(card_.y + card_.h, dialog_.y + dialog_.h);
    return {card_.x, card_.y, right - card_.x, bottom - card_.y};
}

ui::Box Panel::paint_at(ui::Canvas &canvas, float x, float top) {
    if (!open_) {
        return {};
    }
    float width = content_->width();
    content_h_ = content_->content_height(canvas);
    float chrome = cfg::padding + cfg::header_height + cfg::header_divider_gap + 1.0f + cfg::content_gap + cfg::padding;
    float height = std::min(content_->max_height(), chrome + content_h_);
    if (!closing_) {
        if (reveal_ < 0.0f) {
            reveal_ = 0.0f;
            target_ = height;
            animations_.animate(0.0f, height, cfg::reveal_ms, astralia::Easing::EaseOutCubic, [this](float v) { reveal_ = v; }, {}, owner_reveal);
        } else if (std::fabs(height - target_) > 0.5f) {
            target_ = height;
            animations_.animate(reveal_, height, cfg::reveal_ms, astralia::Easing::EaseOutCubic, [this](float v) { reveal_ = v; }, {}, owner_reveal);
        }
    }
    float full = closing_ ? std::max(target_, height) : height;
    card_ = {x, top, width, full};
    std::vector<PanelRegion> chrome_regions;
    std::vector<PanelRegion> body;
    std::vector<PanelRegion> dialog;
    PanelPaint chrome_paint(chrome_regions);

    float visible = std::max(0.0f, reveal_);
    canvas.begin_group({x, top, width, std::min(visible, full)}, {1.0f, visible + 0.5f < full});
    canvas.rounded({0, 0, width, full}, metrics::radius_md, palette::overlay, metrics::border_thin, palette::accent);
    ui::TextSize title = canvas.measure(content_->title(), title_style);
    canvas.text(content_->title(), title_style, cfg::padding, cfg::padding + (cfg::header_height - title.h) / 2.0f, palette::text);
    ui::Box close{width - cfg::padding - cfg::close_button, cfg::padding + (cfg::header_height - cfg::close_button) / 2.0f, cfg::close_button, cfg::close_button};
    ui::TextSize glyph = canvas.measure(icon::close, icon_style);
    canvas.text(icon::close, icon_style, close.x + (close.w - glyph.w) / 2.0f, close.y + (close.h - glyph.h) / 2.0f, palette::text);
    chrome_paint.region(close, close_id);
    content_->paint_header(canvas, {cfg::padding, cfg::padding, close.x - cfg::row_gap - cfg::padding, cfg::header_height}, chrome_paint);
    float divider_y = cfg::padding + cfg::header_height + cfg::header_divider_gap;
    canvas.rect({cfg::padding, divider_y, width - 2.0f * cfg::padding, 1.0f}, palette::text_alpha06);

    float view_top = divider_y + 1.0f + cfg::content_gap;
    view_h_ = std::max(0.0f, full - cfg::padding - view_top);
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content_h_ - view_h_));
    canvas.begin_group({cfg::padding, view_top, width - 2.0f * cfg::padding, view_h_}, {1.0f, true});
    PanelPaint body_paint(body);
    content_->paint(canvas, {0.0f, 0.0f, width - 2.0f * cfg::padding, view_h_}, scroll_, body_paint);
    canvas.end_group();
    canvas.end_group();

    for (PanelRegion &region : body) {
        region.box.x += cfg::padding;
        region.box.y += view_top;
        float bottom = std::min(region.box.y + region.box.h, view_top + view_h_);
        float topc = std::max(region.box.y, view_top);
        region.box.y = topc;
        region.box.h = std::max(0.0f, bottom - topc);
    }

    dialog_ = {};
    float dialog_h = closing_ ? 0.0f : content_->dialog_height();
    if (dialog_h > 0.0f) {
        float dialog_y = full + cfg::gap_below_bar;
        canvas.begin_group({x, top + dialog_y, width, dialog_h}, {});
        PanelPaint dialog_paint(dialog);
        content_->paint_dialog(canvas, {0.0f, 0.0f, width, dialog_h}, dialog_paint);
        canvas.end_group();
        for (PanelRegion &region : dialog) {
            region.box.y += dialog_y;
        }
        dialog_ = {x, top + dialog_y, width, dialog_h};
    }

    regions_.clear();
    regions_.insert(regions_.end(), dialog.begin(), dialog.end());
    regions_.insert(regions_.end(), chrome_regions.begin(), chrome_regions.end());
    regions_.insert(regions_.end(), body.begin(), body.end());
    return card_;
}

} // namespace astralia
