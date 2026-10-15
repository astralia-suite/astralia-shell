#include <algorithm>
#include <cmath>

#include "modules/bar/panel/panel.h"

#include "render/icons.h"
#include "render/scroll.h"
#include "render/sticker.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

constexpr uint64_t owner_reveal = 2;
constexpr int close_id = -1;
constexpr ScrollMetrics scroll_style{cfg::scroll_roller, cfg::scroll_overhang, cfg::scroll_cap, metrics::radius_card, cfg::scroll_band, ScrollAxis::vertical};
constexpr ui::TextStyle title_style{ui::FontFamily::text, cfg::title_px, true};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

ScrollEntry entry_of(PanelAnchor anchor) {
    switch (anchor) {
    case PanelAnchor::left:
        return ScrollEntry::from_left;
    case PanelAnchor::center:
        return ScrollEntry::from_top;
    case PanelAnchor::right:
        break;
    }
    return ScrollEntry::from_right;
}

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
        if (alive.lock() && open_ && !closing() && content_->refresh_interval().count() > 0) {
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
    if (open_ && !closing()) {
        return;
    }
    open_ = true;
    reveal_ = -1.0f;
    target_ = -1.0f;
    scroll_ = 0.0f;
    sequence_.open();
    content_->opened();
    reactor_->reschedule(timer_);
    changed();
}

void Panel::close() {
    if (!open_ || closing()) {
        return;
    }
    dragging_ = false;
    content_->closed();
    sequence_.close([this] {
        open_ = false;
        reveal_ = -1.0f;
        target_ = -1.0f;
        if (on_closed) {
            on_closed();
        }
    });
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
    if (!open_ || closing() || !sequence_.settled()) {
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

bool Panel::hover(double x, double y) {
    if (!open_ || closing() || !sequence_.settled() || dragging_) {
        return false;
    }
    const PanelRegion *region = region_at(x, y);
    if (!content_->hover(region != nullptr ? region->id : -1, region != nullptr ? region->a : -1)) {
        return false;
    }
    changed();
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
    if (!open_ || closing() || !sequence_.settled()) {
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
    if (!open_ || closing()) {
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

Panel::Inset Panel::inset() {
    return {scroll_style.overhang, scroll_style.roller_w};
}

ui::Box Panel::extent() const {
    Inset room = inset();
    float left = card_.x - room.x;
    float top = card_.y - room.y;
    float right = card_.x + card_.w + room.x;
    float bottom = card_.y + card_.h + room.y;
    if (dialog_.w > 0.0f) {
        right = std::max(right, dialog_.x + dialog_.w);
        bottom = std::max(bottom, dialog_.y + dialog_.h);
    }
    return {left, top, right - left, bottom - top};
}

ui::Box Panel::popup_anchor() const {
    ui::Box anchor = content_->popup_anchor();
    return {card_.x + anchor.x, card_.y + anchor.y, anchor.w, anchor.h};
}

void Panel::paint_popup(ui::Canvas &canvas) {
    popup_regions_.clear();
    PanelPaint paint(popup_regions_);
    content_->paint_popup(canvas, {0.0f, 0.0f, content_->popup_width(), content_->popup_height()}, paint);
}

bool Panel::popup_clickable(double x, double y) const {
    return std::ranges::any_of(popup_regions_, [&](const PanelRegion &r) { return inside(r.box, x, y); });
}

bool Panel::hover_popup(double x, double y) {
    int id = -1;
    int a = -1;
    for (const PanelRegion &region : popup_regions_) {
        if (inside(region.box, x, y)) {
            id = region.id;
            a = region.a;
            break;
        }
    }
    if (!content_->popup_hover(id, a)) {
        return false;
    }
    changed();
    return true;
}

bool Panel::press_popup(double x, double y, input::Button button) {
    if (button != input::Button::Left && button != input::Button::Right) {
        return false;
    }
    for (const PanelRegion &region : popup_regions_) {
        if (inside(region.box, x, y)) {
            PanelRegion hit = region;
            content_->pressed = button;
            if (content_->activate(hit, x, y)) {
                changed();
            }
            return true;
        }
    }
    return true;
}

void Panel::dismiss_popup() {
    if (content_->dismiss_dialog()) {
        changed();
    }
}

ui::Box Panel::paint_at(ui::Canvas &canvas, float x, float top, float surface_width) {
    if (!open_) {
        return {};
    }
    float width = content_->width();
    content_h_ = content_->content_height(canvas);
    bool header = content_->has_header();
    float chrome = header ? cfg::padding + cfg::header_height + cfg::header_divider_gap + 1.0f + cfg::content_gap + cfg::padding : 2.0f * cfg::padding;
    float height = std::min(content_->max_height(), chrome + content_h_);
    if (!closing()) {
        if (reveal_ < 0.0f) {
            reveal_ = height;
            target_ = height;
        } else if (std::fabs(height - target_) > 0.5f) {
            target_ = height;
            animations_.animate(reveal_, height, cfg::reveal_ms, astralia::Easing::EaseOutCubic, [this](float v) { reveal_ = v; }, {}, owner_reveal);
        }
    }
    float full = closing() ? std::max(target_, height) : height;
    float visible = reveal_ < 0.0f ? full : std::min(reveal_, full);

    float rod = scroll_style.roller_w;
    card_ = {x, top + rod, width, full};
    ScrollOffset slide = scroll_slide_offset(entry_of(content_->anchor()), sequence_.slide(), scroll_bounds(scroll_geometry(card_, 0.0f, scroll_style)), surface_width, 0.0f);
    float px = x + slide.dx;
    float py = top + rod + slide.dy;
    paint_scroll(canvas, scroll_geometry({px, py, width, visible}, sequence_.scroll(), scroll_style), scroll_style);

    regions_.clear();
    dialog_ = {};
    float grid = sequence_.grid();
    if (grid <= 0.0f) {
        return card_;
    }

    std::vector<PanelRegion> chrome_regions;
    std::vector<PanelRegion> body;
    std::vector<PanelRegion> dialog;
    PanelPaint chrome_paint(chrome_regions);

    canvas.set_opacity(grid);
    canvas.begin_group({px, py, width, visible}, {1.0f, visible + 0.5f < full});
    float view_top = cfg::padding;
    if (header) {
        ui::TextSize title = canvas.measure(content_->title(), title_style);
        float tag_pad = cfg::header_tag_pad;
        sticker(canvas, {cfg::padding - tag_pad, cfg::padding + (cfg::header_height - title.h) / 2.0f - tag_pad / 2.0f, title.w + 2.0f * tag_pad + metrics::shadow_widget, title.h + tag_pad + metrics::shadow_widget}, metrics::radius_widget, content_->tint(), palette::ink, metrics::border_thin, metrics::shadow_widget);
        canvas.text(content_->title(), title_style, cfg::padding, cfg::padding + (cfg::header_height - title.h) / 2.0f, palette::text);
        ui::Box close{width - cfg::padding - cfg::close_button, cfg::padding + (cfg::header_height - cfg::close_button) / 2.0f, cfg::close_button, cfg::close_button};
        selectable(canvas, close, metrics::radius_widget, false, false, palette::accent_container);
        ui::TextSize glyph = canvas.measure(icon::close, icon_style);
        canvas.text(icon::close, icon_style, close.x + (close.w - glyph.w) / 2.0f, close.y + (close.h - glyph.h) / 2.0f, palette::text);
        chrome_paint.region(close, close_id);
        content_->paint_header(canvas, {cfg::padding, cfg::padding, close.x - cfg::row_gap - cfg::padding, cfg::header_height}, chrome_paint);
        float divider_y = cfg::padding + cfg::header_height + cfg::header_divider_gap;
        canvas.rect({cfg::padding, divider_y, width - 2.0f * cfg::padding, 1.0f}, palette::ink);
        view_top = divider_y + 1.0f + cfg::content_gap;
    }
    view_h_ = std::max(0.0f, full - cfg::padding - view_top);
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content_h_ - view_h_));
    canvas.begin_group({cfg::padding, view_top, width - 2.0f * cfg::padding, view_h_}, {1.0f, true});
    PanelPaint body_paint(body);
    content_->paint(canvas, {0.0f, 0.0f, width - 2.0f * cfg::padding, view_h_}, scroll_, body_paint);
    canvas.end_group();
    canvas.end_group();
    canvas.set_opacity(1.0f);

    for (PanelRegion &region : body) {
        region.box.x += cfg::padding;
        region.box.y += view_top;
        float bottom = std::min(region.box.y + region.box.h, view_top + view_h_);
        float topc = std::max(region.box.y, view_top);
        region.box.y = topc;
        region.box.h = std::max(0.0f, bottom - topc);
    }

    float dialog_h = closing() || !sequence_.settled() ? 0.0f : content_->dialog_height();
    if (dialog_h > 0.0f) {
        float dialog_y = full + rod + cfg::gap_below_bar;
        canvas.begin_group({px, py + dialog_y, width, dialog_h}, {});
        PanelPaint dialog_paint(dialog);
        content_->paint_dialog(canvas, {0.0f, 0.0f, width, dialog_h}, dialog_paint);
        canvas.end_group();
        for (PanelRegion &region : dialog) {
            region.box.y += dialog_y;
        }
        dialog_ = {px, py + dialog_y, width, dialog_h};
    }

    regions_.insert(regions_.end(), dialog.begin(), dialog.end());
    regions_.insert(regions_.end(), chrome_regions.begin(), chrome_regions.end());
    regions_.insert(regions_.end(), body.begin(), body.end());
    return card_;
}

} // namespace astralia
