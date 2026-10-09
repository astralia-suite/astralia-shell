#include <algorithm>
#include <cmath>

#include "config/icons.h"

#include "modules/bar/panel/tray_panel.h"
#include "modules/bar/panel/widgets.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { item = 1,
              back,
              entry };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::small_px + 1};
constexpr ui::TextStyle small_style{ui::FontFamily::text, cfg::small_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px - 4};

float rows_height(const std::vector<TrayMenuRow> &rows) {
    float height = 0.0f;
    for (const TrayMenuRow &row : rows) {
        height += row.height;
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
    menu_scroll_ = 0.0f;
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
        return cfg::empty_height;
    }
    int columns = tray_columns(width());
    float rows = static_cast<float>((static_cast<int>(items.size()) + columns - 1) / columns);
    return rows * cfg::tray_cell + (rows - 1.0f) * cfg::tray_gap;
}

void TrayPanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) {
    const std::vector<TrayItem> &items = tray_.items();
    if (items.empty()) {
        panel_widgets::centered_text(canvas, "No tray icons", ui::TextStyle{ui::FontFamily::text, cfg::text_px}, {view.x, view.y - scroll, view.w, cfg::empty_height}, palette::text_dim);
        return;
    }
    int columns = tray_columns(width());
    for (size_t i = 0; i < items.size(); ++i) {
        ui::Box cell{view.x + static_cast<float>(i % columns) * (cfg::tray_cell + cfg::tray_gap), view.y - scroll + static_cast<float>(i / columns) * (cfg::tray_cell + cfg::tray_gap), cfg::tray_cell, cfg::tray_cell};
        if (cell.y + cell.h < view.y || cell.y > view.y + view.h) {
            continue;
        }
        bool selected = !menu_key_.empty() && menu_key_ == items[i].key();
        canvas.rounded(cell, metrics::radius_md, selected ? palette::accent_alpha25 : palette::text_alpha06);
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

float TrayPanel::dialog_height() {
    if (menu_key_.empty()) {
        return 0.0f;
    }
    if (tray_.find(menu_key_) == nullptr) {
        close_menu();
        return 0.0f;
    }
    float inner = rows_height(tray_menu_rows(level(), !path_.empty()));
    return std::min(cfg::tray_menu_max, inner + 2.0f * cfg::tray_menu_pad);
}

void TrayPanel::paint_dialog(ui::Canvas &canvas, const ui::Box &box, PanelPaint &paint) {
    std::vector<TrayMenuRow> rows = tray_menu_rows(level(), !path_.empty());
    canvas.rounded(box, metrics::radius_md, palette::overlay, metrics::border_thin, palette::accent);
    float view_h = box.h - 2.0f * cfg::tray_menu_pad;
    float total = rows_height(rows);
    menu_scroll_ = std::clamp(menu_scroll_, 0.0f, std::max(0.0f, total - view_h));
    ui::Box clip{box.x + cfg::tray_menu_pad, box.y + cfg::tray_menu_pad, box.w - 2.0f * cfg::tray_menu_pad, view_h};
    canvas.begin_group(clip, {1.0f, true});
    float y = -menu_scroll_;
    std::vector<ui::Box> hits;
    std::vector<int> ids;
    std::vector<int> args;
    for (const TrayMenuRow &row : rows) {
        ui::Box r{0.0f, y, clip.w, row.height};
        y += row.height;
        if (r.y + r.h < 0.0f || r.y > view_h) {
            continue;
        }
        float mid = r.y + r.h / 2.0f;
        switch (row.kind) {
        case TrayMenuRow::Kind::separator:
            canvas.rect({cfg::tray_menu_row_pad, mid, r.w - 2.0f * cfg::tray_menu_row_pad, 1.0f}, palette::text_alpha08);
            break;
        case TrayMenuRow::Kind::loading:
            panel_widgets::text_in_row(canvas, "Loading", small_style, cfg::tray_menu_row_pad, r, palette::text_dim);
            break;
        case TrayMenuRow::Kind::back: {
            ui::TextSize glyph = canvas.measure(icon::chevron_left, icon_style);
            canvas.text(icon::chevron_left, icon_style, cfg::tray_menu_row_pad, mid - glyph.h / 2.0f, palette::text);
            panel_widgets::text_in_row(canvas, "Back", text_style, cfg::tray_menu_row_pad * 2.0f + glyph.w, r, palette::text);
            hits.push_back(r);
            ids.push_back(back);
            args.push_back(0);
            break;
        }
        case TrayMenuRow::Kind::entry: {
            const TrayMenuEntry &e = *row.entry;
            const Color &color = e.enabled ? palette::text : palette::text_dim;
            float x = cfg::tray_menu_row_pad;
            if (e.checkbox) {
                if (e.checked) {
                    ui::TextSize glyph = canvas.measure(icon::check, icon_style);
                    canvas.text(icon::check, icon_style, x, mid - glyph.h / 2.0f, color);
                }
                x += 12.0f + cfg::tray_menu_row_pad;
            }
            float chevron = e.children.empty() ? 0.0f : 12.0f + cfg::tray_menu_row_pad;
            ui::TextStyle label = text_style;
            label.max_width = std::max(20, static_cast<int>(r.w - cfg::tray_menu_row_pad - chevron - x));
            panel_widgets::text_in_row(canvas, tray_strip_mnemonic(e.label), label, x, r, color);
            if (!e.children.empty()) {
                ui::TextSize glyph = canvas.measure(icon::chevron_right, icon_style);
                canvas.text(icon::chevron_right, icon_style, r.w - cfg::tray_menu_row_pad - glyph.w, mid - glyph.h / 2.0f, palette::text_dim);
            }
            if (e.enabled) {
                hits.push_back(r);
                ids.push_back(entry);
                args.push_back(e.id);
            }
            break;
        }
        }
    }
    canvas.end_group();
    for (size_t i = 0; i < hits.size(); ++i) {
        ui::Box h{clip.x + hits[i].x, std::max(clip.y, clip.y + hits[i].y), hits[i].w, hits[i].h};
        float bottom = std::min(clip.y + hits[i].y + hits[i].h, clip.y + clip.h);
        h.h = bottom - h.y;
        if (h.h > 0.0f) {
            paint.region(h, ids[i], args[i]);
        }
    }
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
            path_.clear();
            menu_scroll_ = 0.0f;
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
            menu_scroll_ = 0.0f;
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
            menu_scroll_ = 0.0f;
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

bool TrayPanel::wheel(double, double, double dy) {
    if (menu_key_.empty()) {
        return false;
    }
    float before = menu_scroll_;
    menu_scroll_ = std::max(0.0f, menu_scroll_ + static_cast<float>(dy));
    return menu_scroll_ != before;
}

bool TrayPanel::key(const input::KeyEvent &event) {
    if (event.kind != input::KeyKind::Escape || menu_key_.empty()) {
        return false;
    }
    if (!path_.empty()) {
        path_.pop_back();
        menu_scroll_ = 0.0f;
    } else {
        close_menu();
    }
    return true;
}

} // namespace astralia
