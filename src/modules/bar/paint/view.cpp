#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "config/bar_layout.h"

#include "render/sticker.h"

#include "modules/bar/model/zones.h"
#include "modules/bar/paint/chips.h"
#include "modules/bar/paint/view.h"

#include "service/icon_service.h"

namespace astralia::bar {

namespace {

namespace cfg = bar_layout;

Color function_fill(Item id, const StyleSpec &style) {
    if (style.uniform_fill) {
        return chip_fill();
    }
    switch (id) {
    case Item::resource:
    case Item::battery:
        return palette::pop_c;
    case Item::network:
    case Item::media:
        return palette::accent_alt;
    case Item::bluetooth:
        return palette::pop_a;
    case Item::volume:
    case Item::time:
        return palette::accent;
    case Item::date:
        return palette::pop_a;
    case Item::brightness:
        return palette::pop_b;
    default:
        return style.bg;
    }
}

const Color &workspace_fill(const BarWorkspace &ws, const Color &active, const Color &occupied,
                            const Color &empty) {
    return ws.active ? active : ws.occupied ? occupied : empty;
}

void paint_item(ui::Canvas &canvas, const StyleSpec &style, const ItemView &view, Item id) {
    if (!view.visible || view.grouped || view.box.w <= 0.0f) {
        return;
    }
    paint_pill(canvas, style, view.box, view.tint.value_or(function_fill(id, style)));
    if (!view.detail.empty()) {
        paint_two_lines(canvas, style, view.box, view.glyph, view.label, view.detail, view.expand);
    } else {
        paint_icon_label(canvas, style, view.box, view.glyph, view.label, view.expand);
    }
    if (view.alert) {
        canvas.rounded(view.box, metrics::radius_widget, palette::critical_alpha15);
    }
}

void paint_status(ui::Canvas &canvas, const Model &model) {
    const Layout &layout = model.layout();
    if (layout.status.w <= 0.0f) {
        return;
    }
    std::vector<IconCell> cells;
    for (Item id : status_members) {
        const ItemView &view = model.item(id);
        if (view.visible) {
            cells.push_back({view.glyph, view.icon_w, view.label, view.label_w, view.expand});
        }
    }
    paint_icon_row(canvas, model.style(), layout.status, cells, model.style().icon_gap);
}

void paint_workspaces(ui::Canvas &canvas, const Model &model) {
    const Layout &layout = model.layout();
    if (layout.workspaces.w <= 0.0f) {
        return;
    }
    const StyleSpec &style = model.style();
    if (style.workspace_badges) {
        for (const BarWorkspace &ws : model.workspaces()) {
            paint_badge(canvas, style, ws.box, std::to_string(ws.id),
                        workspace_fill(ws, palette::pop_a, palette::pop_b, palette::accent));
        }
        return;
    }
    paint_pill(canvas, style, layout.workspaces, style.bg);
    for (const BarWorkspace &ws : model.workspaces()) {
        sticker(canvas, ws.box, ws.box.h / 2.0f,
                workspace_fill(ws, palette::accent, palette::pop_b, palette::paper),
                palette::border_outer, metrics::border_thin, 0.0f);
    }
}

void paint_dock(ui::Canvas &canvas, const Model &model) {
    const Layout &layout = model.layout();
    if (layout.dock.w <= 0.0f) {
        return;
    }
    const StyleSpec &style = model.style();
    paint_pill(canvas, style, layout.dock, style.bg);
    float pad = layout.height * style.pad_ratio;
    float y = std::round((layout.height - cfg::dock_icon) / 2.0f - style.text_lift);
    for (const DockSlot &slot : model.dock()) {
        std::string path = window_icon_path(slot.entry.window_class);
        if (path.empty()) {
            continue;
        }
        ui::ImageId image = canvas.image(path, static_cast<int>(cfg::dock_icon));
        if (image == ui::no_image) {
            continue;
        }
        ui::TextSize size = canvas.image_size(image);
        float scale = size.w > 0.0f && size.h > 0.0f
                          ? std::min(cfg::dock_icon / size.w, cfg::dock_icon / size.h)
                          : 1.0f;
        float w = std::round(size.w * scale);
        float h = std::round(size.h * scale);
        float x = std::round(layout.dock.x + pad + slot.x);
        float alpha = (slot.entry.focused ? cfg::dock_focused_alpha : cfg::dock_unfocused_alpha) *
                      slot.appear;
        canvas.draw_image(image,
                          {x + std::round((cfg::dock_icon - w) / 2.0f),
                           y + std::round((cfg::dock_icon - h) / 2.0f), w, h},
                          with_alpha(palette::white, alpha));
    }
}

} // namespace

void paint(ui::Canvas &canvas, const Model &model, const ui::Box *dirty) {
    const Layout &layout = model.layout();
    const StyleSpec &style = model.style();
    auto touches = [&](const ui::Box &box) {
        return dirty == nullptr || (box.x < dirty->x + dirty->w && box.x + box.w > dirty->x);
    };
    for (size_t i = 0; i < item_count; ++i) {
        Item id = static_cast<Item>(i);
        const ItemView &view = model.item(id);
        if (touches(view.box)) {
            paint_item(canvas, style, view, id);
        }
    }
    if (touches(layout.status)) {
        paint_status(canvas, model);
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
        canvas.rect({std::floor(x), (layout.height - h) / 2.0f, cfg::divider_width, h},
                    palette::text_alpha20);
    }
}

} // namespace astralia::bar
