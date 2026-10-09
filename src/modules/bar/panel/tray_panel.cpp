#include <algorithm>
#include <cmath>

#include "render/icons.h"

#include "modules/bar/panel/tray_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { item = 1,
              back,
              entry };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

float rows_height(const std::vector<TrayMenuRow> &rows) {
    float height = 0.0f;
    for (const TrayMenuRow &row : rows) {
        height += row.height;
    }
    if (!rows.empty()) {
        height += static_cast<float>(rows.size() - 1) * cfg::tray_menu_gap;
    }
    return height;
}

} // namespace

int tray_columns(float width) {
    return std::max(1, static_cast<int>((width - 2.0f * cfg::padding + cfg::tray_gap) / (cfg::tray_cell + cfg::tray_gap)));
}

const std::vector<TrayMenuEntry> *tray_menu_level(const std::vector<TrayMenuEntry> *root, const std::vector<int32_t> &path) {
    const std::vector<TrayMenuEntry> *level = root;
    for (int32_t id : path) {
        if (level == nullptr) {
            return nullptr;
        }
        auto it = std::ranges::find(*level, id, &TrayMenuEntry::id);
        level = it == level->end() ? nullptr : &it->children;
    }
    return level;
}

std::vector<TrayMenuRow> tray_menu_rows(const std::vector<TrayMenuEntry> *level, bool show_back) {
    std::vector<TrayMenuRow> rows;
    if (show_back) {
        rows.push_back({TrayMenuRow::Kind::back, cfg::tray_menu_row});
    }
    if (level == nullptr) {
        rows.push_back({TrayMenuRow::Kind::loading, cfg::tray_menu_row});
        return rows;
    }
    for (const TrayMenuEntry &e : *level) {
        if (!e.visible) {
            continue;
        }
        if (e.separator) {
            rows.push_back({TrayMenuRow::Kind::separator, cfg::tray_menu_separator, &e});
        } else {
            rows.push_back({TrayMenuRow::Kind::entry, cfg::tray_menu_row, &e});
        }
    }
    return rows;
}

TrayPanel::TrayPanel(TrayService &tray) : tray_(tray) {
    tray_.changed.connect(notifier());
}

void TrayPanel::closed() {
    close_menu();
}

void TrayPanel::close_menu() {
    menu_key_.clear();
    path_.clear();
    menu_anchor_ = {};
    hover_id_ = -1;
    hover_a_ = -1;
}

const std::vector<TrayMenuEntry> *TrayPanel::level() const {
    if (menu_key_.empty()) {
        return nullptr;
    }
    return tray_menu_level(tray_.menu(menu_key_), path_);
}

float TrayPanel::content_height(ui::Canvas &) {
    const std::vector<TrayItem> &items = tray_.items();
    if (items.empty()) {
        return cfg::tray_empty_height;
    }
    int columns = tray_columns(width());
    float rows = static_cast<float>((static_cast<int>(items.size()) + columns - 1) / columns);
    return rows * cfg::tray_cell + (rows - 1.0f) * cfg::tray_gap;
}

void TrayPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) {
    if (!menu_key_.empty() && tray_.find(menu_key_) == nullptr) {
        close_menu();
    }
    const std::vector<TrayItem> &items = tray_.items();
    if (items.empty()) {
        ui::TextSize size = canvas.measure("No tray icons", text_style);
        canvas.text("No tray icons", text_style, view.x + (view.w - size.w) / 2.0f, view.y - scroll, palette::text_dim);
        return;
    }
    int columns = tray_columns(width());
    for (size_t i = 0; i < items.size(); ++i) {
        ui::Box cell{view.x + static_cast<float>(i % columns) * (cfg::tray_cell + cfg::tray_gap), view.y - scroll + static_cast<float>(i / columns) * (cfg::tray_cell + cfg::tray_gap), cfg::tray_cell, cfg::tray_cell};
        if (cell.y + cell.h < view.y || cell.y > view.y + view.h) {
            continue;
        }
        canvas.rounded(cell, metrics::radius_md, palette::text_alpha06);
        std::string path = tray_item_icon_path(items[i]);
        ui::ImageId image = path.empty() ? ui::no_image : canvas.image(path, static_cast<int>(cfg::tray_icon));
        ui::Box target{cell.x + (cell.w - cfg::tray_icon) / 2.0f, cell.y + (cell.h - cfg::tray_icon) / 2.0f, cfg::tray_icon, cfg::tray_icon};
        if (image != ui::no_image) {
            ui::TextSize size = canvas.image_size(image);
            float scale = size.w > 0.0f && size.h > 0.0f ? cfg::tray_icon / std::max(size.w, size.h) : 1.0f;
            canvas.draw_image(image, {cell.x + (cell.w - size.w * scale) / 2.0f, cell.y + (cell.h - size.h * scale) / 2.0f, size.w * scale, size.h * scale}, palette::text);
        } else {
            panel_widgets::centered_text(canvas, icon::apps, ui::TextStyle{ui::FontFamily::icon, cfg::icon_px}, target, palette::text);
        }
        paint.region(cell, item, static_cast<int>(i));
    }
}

float TrayPanel::popup_height() const {
    if (menu_key_.empty() || tray_.find(menu_key_) == nullptr) {
        return 0.0f;
    }
    return rows_height(tray_menu_rows(level(), !path_.empty())) + 2.0f * cfg::tray_menu_pad;
}

void TrayPanel::paint_popup(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) {
    std::vector<TrayMenuRow> rows = tray_menu_rows(level(), !path_.empty());
    canvas.rounded(box, cfg::tray_menu_radius, palette::overlay, cfg::tray_menu_border, palette::accent);
    float x = box.x + cfg::tray_menu_pad;
    float width = box.w - 2.0f * cfg::tray_menu_pad;
    float y = box.y + cfg::tray_menu_pad;
    for (const TrayMenuRow &row : rows) {
        ui::Box r{x, y, width, row.height};
        y += row.height + cfg::tray_menu_gap;
        switch (row.kind) {
        case TrayMenuRow::Kind::separator:
            canvas.rect({r.x + cfg::tray_menu_separator_inset, r.y + r.h / 2.0f, r.w - 2.0f * cfg::tray_menu_separator_inset, 1.0f}, palette::text_alpha06);
            break;
        case TrayMenuRow::Kind::loading:
            panel_widgets::text_in_row(canvas, "Loading\xE2\x80\xA6", text_style, r.x + cfg::tray_menu_row_pad, r, palette::text_dim);
            break;
        case TrayMenuRow::Kind::back: {
            if (hover_id_ == back) {
                canvas.rounded(r, metrics::radius_sm, palette::text_alpha08);
            }
            panel_widgets::centered_text(canvas, icon::chevron_left, icon_style, {r.x, r.y, cfg::tray_menu_row, r.h}, palette::text);
            paint.region(r, back);
            break;
        }
        case TrayMenuRow::Kind::entry: {
            const TrayMenuEntry &e = *row.entry;
            const Color &color = e.enabled ? palette::text : palette::text_dim;
            if (e.enabled && hover_id_ == entry && hover_a_ == e.id) {
                canvas.rounded(r, metrics::radius_sm, palette::text_alpha08);
            }
            float text_x = r.x + cfg::tray_menu_row_pad;
            if (e.checkbox) {
                ui::TextSize glyph = canvas.measure(icon::check, icon_style);
                if (e.checked) {
                    canvas.text(icon::check, icon_style, text_x, r.y + (r.h - glyph.h) / 2.0f, color);
                }
                text_x += glyph.w + cfg::tray_menu_row_pad;
            }
            ui::TextStyle label = text_style;
            label.max_width = std::max(0, static_cast<int>(r.x + r.w - text_x - cfg::tray_menu_label_offset));
            panel_widgets::text_in_row(canvas, tray_strip_mnemonic(e.label), label, text_x, r, color);
            if (!e.children.empty()) {
                ui::TextSize glyph = canvas.measure(icon::chevron_right, icon_style);
                canvas.text(icon::chevron_right, icon_style, r.x + r.w - cfg::tray_menu_row_pad - glyph.w, r.y + (r.h - glyph.h) / 2.0f, palette::text_dim);
            }
            if (e.enabled) {
                paint.region(r, entry, e.id);
            }
            break;
        }
        }
    }
}

bool TrayPanel::popup_hover(int id, int a) {
    if (id == hover_id_ && a == hover_a_) {
        return false;
    }
    hover_id_ = id;
    hover_a_ = a;
    return true;
}

bool TrayPanel::dismiss_dialog() {
    if (menu_key_.empty()) {
        return false;
    }
    close_menu();
    return true;
}

bool TrayPanel::activate(const PanelRegion &region, double, double) {
    switch (region.id) {
    case item: {
        const std::vector<TrayItem> &items = tray_.items();
        if (region.a < 0 || static_cast<size_t>(region.a) >= items.size()) {
            return false;
        }
        const TrayItem &target = items[static_cast<size_t>(region.a)];
        if (pressed == input::Button::Right) {
            if (!target.has_menu()) {
                close_menu();
                return true;
            }
            menu_key_ = target.key();
            menu_anchor_ = region.box;
            path_.clear();
            tray_.request_menu(menu_key_);
            return true;
        }
        close_menu();
        tray_.activate(target.key());
        return true;
    }
    case back:
        if (!path_.empty()) {
            path_.pop_back();
        } else {
            close_menu();
        }
        return true;
    case entry: {
        const std::vector<TrayMenuEntry> *current = level();
        if (current == nullptr) {
            return true;
        }
        auto it = std::ranges::find(*current, region.a, &TrayMenuEntry::id);
        if (it == current->end()) {
            return true;
        }
        if (!it->children.empty()) {
            path_.push_back(region.a);
            return true;
        }
        std::string key = menu_key_;
        close_menu();
        tray_.menu_clicked(key, region.a);
        return true;
    }
    default:
        return false;
    }
}

bool TrayPanel::key(const input::KeyEvent &event) {
    if (event.kind != input::KeyKind::Escape || menu_key_.empty()) {
        return false;
    }
    if (!path_.empty()) {
        path_.pop_back();
    } else {
        close_menu();
    }
    return true;
}

} // namespace astralia
