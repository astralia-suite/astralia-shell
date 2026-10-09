#include <algorithm>
#include <cmath>

#include "config/bar_layout.h"
#include "render/icons.h"

#include "modules/bar/view.h"

#include "service/icon_service.h"

namespace astralia {

namespace {

namespace cfg = bar_layout;

constexpr ui::TextStyle icon_style{ui::FontFamily::icon, static_cast<int>(cfg::icon_px)};
constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};

void capsule_background(ui::Canvas &canvas, const BarStyleSpec &style, const ui::Box &box, const Color &border) {
    if (style.has_pill_bg()) {
        canvas.rounded(box, box.h * style.radius_ratio, style.bg, style.border_width, border);
    }
}

void paint_item(ui::Canvas &canvas, const BarStyleSpec &style, const BarItemView &view) {
    if (!view.visible || view.box.w <= 0.0f) {
        return;
    }
    float pad = view.box.h * style.pad_ratio;
    canvas.begin_group(view.box, {1.0f, true});
    ui::Box local{0.0f, 0.0f, view.box.w, view.box.h};
    capsule_background(canvas, style, local, view.border.value_or(style.border));
    ui::TextSize icon = canvas.measure(view.glyph, icon_style);
    canvas.text(view.glyph, icon_style, pad, (local.h - icon.h) / 2.0f, palette::text);
    if (!view.label.empty() && view.expand > 0.0f) {
        ui::TextSize label = canvas.measure(view.label, text_style);
        canvas.text(view.label, text_style, pad + view.icon_w + cfg::label_gap, (local.h - label.h) / 2.0f, with_alpha(palette::text, view.expand));
    }
    canvas.end_group();
    if (view.alert) {
        canvas.rounded(view.box, metrics::radius_md, palette::critical_alpha15);
    }
}

void paint_workspaces(ui::Canvas &canvas, const BarModel &model) {
    const BarLayout &layout = model.layout();
    if (layout.workspaces.w <= 0.0f) {
        return;
    }
    const BarStyleSpec &style = model.style();
    capsule_background(canvas, style, layout.workspaces, style.border);
    for (const BarWorkspace &ws : model.workspaces()) {
        const Color &color = ws.active ? palette::accent_alt : ws.occupied ? palette::accent
                                                                           : palette::text_dim;
        canvas.rounded(ws.box, ws.box.h / 2.0f, color);
    }
    ui::TextSize glyph = canvas.measure(icon::overview, icon_style);
    float x = layout.overview.x + cfg::workspace_overview_gap / 2.0f;
    canvas.text(icon::overview, icon_style, x, (layout.height - glyph.h) / 2.0f, palette::text);
}

void paint_dock(ui::Canvas &canvas, const BarModel &model) {
    const BarLayout &layout = model.layout();
    if (layout.dock.w <= 0.0f) {
        return;
    }
    const BarStyleSpec &style = model.style();
    capsule_background(canvas, style, layout.dock, style.border);
    float pad = layout.height * style.pad_ratio;
    float y = std::round((layout.height - cfg::dock_icon) / 2.0f);
    for (const BarDockSlot &slot : model.dock()) {
        std::string path = resolve_window_icon_path(slot.entry.window_class);
        if (path.empty()) {
            continue;
        }
        ui::ImageId image = canvas.image(path, static_cast<int>(cfg::dock_icon));
        if (image == ui::no_image) {
            continue;
        }
        ui::TextSize size = canvas.image_size(image);
        float scale = size.w > 0.0f && size.h > 0.0f ? std::min(cfg::dock_icon / size.w, cfg::dock_icon / size.h) : 1.0f;
        float w = std::round(size.w * scale);
        float h = std::round(size.h * scale);
        float x = std::round(layout.dock.x + pad + slot.x);
        float alpha = slot.entry.focused ? cfg::dock_focused_alpha : cfg::dock_unfocused_alpha;
        canvas.draw_image(image, {x + std::round((cfg::dock_icon - w) / 2.0f), y + std::round((cfg::dock_icon - h) / 2.0f), w, h}, with_alpha(palette::text, alpha));
    }
}

} // namespace

void paint_bar(ui::Canvas &canvas, const BarModel &model, const ui::Box *dirty) {
    const BarLayout &layout = model.layout();
    const BarStyleSpec &style = model.style();
    auto touches = [&](const ui::Box &box) { return dirty == nullptr || (box.x < dirty->x + dirty->w && box.x + box.w > dirty->x); };
    for (size_t i = 0; i < bar_item_count; ++i) {
        const BarItemView &view = model.item(static_cast<BarItem>(i));
        if (touches(view.box)) {
            paint_item(canvas, style, view);
        }
    }
    if (touches(layout.workspaces)) {
        paint_workspaces(canvas, model);
    }
    if (touches(layout.dock)) {
        paint_dock(canvas, model);
    }
    for (float x : layout.dividers) {
        if (!touches({x, 0.0f, cfg::divider_width, layout.height})) {
            continue;
        }
        float h = layout.height * cfg::divider_height_ratio;
        canvas.rect({std::floor(x), (layout.height - h) / 2.0f, cfg::divider_width, h}, palette::text_alpha20);
    }
}

} // namespace astralia
