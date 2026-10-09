#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

#include "config/overview_config.h"

#include "core/spawn.h"

#include "modules/overview/model.h"

#include "service/icon_service.h"

namespace astralia {

namespace {

namespace cfg = overview_config;

constexpr OverviewGrid grid{cfg::columns, cfg::rows};
constexpr uint64_t owner_slide = 5;
constexpr uint64_t owner_indicator_x = 3;
constexpr uint64_t owner_indicator_y = 4;

bool inside(const ui::Box &box, double x, double y) {
    return x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;
}

struct Block {
    int group = 0;
    float x = 0.0f;
    float y = 0.0f;
    float cell_w = 0.0f;
    float cell_h = 0.0f;
};

Block make_block(double work_w, double work_h, int group, float scale, float x, float y) {
    return {group, x, y, std::round(static_cast<float>(std::max(1.0, work_w) * scale)), std::round(static_cast<float>(std::max(1.0, work_h) * scale))};
}

std::vector<float> separation_steps(const std::vector<CompositorMonitor> &monitors, double CompositorMonitor::*position, int extent_index) {
    std::vector<size_t> order(monitors.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::sort(order, [&](size_t a, size_t b) { return monitors[a].*position < monitors[b].*position; });
    std::vector<float> steps(monitors.size(), 0.0f);
    for (size_t i = 1; i < order.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            const CompositorMonitor &before = monitors[order[j]];
            const CompositorMonitor &after = monitors[order[i]];
            if (before.*position + compositor_logical_size(before)[static_cast<size_t>(extent_index)] <= after.*position + 0.5) {
                steps[order[i]] = std::max(steps[order[i]], steps[order[j]] + 1.0f);
            }
        }
    }
    return steps;
}

uint64_t tile_owner(const std::string &address, int component) {
    return 1000 + (std::hash<std::string>{}(address) << 2) + static_cast<uint64_t>(component);
}

} // namespace

OverviewModel::OverviewModel(Compositor &compositor) : compositor_(compositor) {}

void OverviewModel::compositor_action() {
    if (on_compositor_action) {
        on_compositor_action();
    }
}

int OverviewModel::active_workspace(const std::string &monitor_name) const {
    const CompositorState &state = compositor_.state();
    auto it = state.by_monitor.find(monitor_name);
    if (it == state.by_monitor.end() || it->second.active_id < 1) {
        it = state.by_monitor.find(state.focused_monitor);
    }
    return it != state.by_monitor.end() && it->second.active_id >= 1 ? it->second.active_id : 1;
}

const CompositorMonitor *OverviewModel::bound_monitor() const {
    return compositor_monitor(compositor_.state(), output_name_);
}

const std::string &OverviewModel::icon_path(const std::string &window_class) {
    auto it = icon_paths_.find(window_class);
    if (it == icon_paths_.end()) {
        it = icon_paths_.emplace(window_class, resolve_window_icon_path(window_class)).first;
    }
    return it->second;
}

void OverviewModel::open(std::string output_name, float width, float height) {
    output_name_ = std::move(output_name);
    width_ = width;
    height_ = height;
    selected_ = active_workspace(output_name_);
    group_ = overview_page_of(grid, selected_);
    global_mode_ = false;
    dragging_ = false;
    closing_ = false;
    open_ = true;
    indicator_tracking_ = false;
    indicator_visible_ = false;
    slide_y_ = height;
    animations_.animate(slide_y_, 0.0f, cfg::anim_ms, astralia::Easing::EaseOutCubic, [this](float v) { slide_y_ = v; }, {}, owner_slide);
}

void OverviewModel::close() {
    if (!open_ || closing_) {
        return;
    }
    dragging_ = false;
    closing_ = true;
    animations_.animate(slide_y_, height_, cfg::anim_ms, astralia::Easing::EaseOutCubic, [this](float v) { slide_y_ = v; }, [this] {
        open_ = false;
        closing_ = false;
        tiles_.clear();
        tile_anim_.clear();
        layout_ = {};
        indicator_visible_ = false;
        if (on_closed) {
            on_closed();
        } }, owner_slide);
}

const OverviewCell *OverviewModel::find_cell(int workspace) const {
    for (const OverviewCell &cell : layout_.cells) {
        if (cell.workspace == workspace) {
            return &cell;
        }
    }
    return nullptr;
}

const OverviewCell *OverviewModel::cell_at(double x, double y) const {
    for (const OverviewCell &cell : layout_.cells) {
        if (inside(cell.rect, x, y)) {
            return &cell;
        }
    }
    return nullptr;
}

void OverviewModel::compute_layout(float width, float height) {
    layout_ = {};
    const CompositorState &state = compositor_.state();
    std::vector<Block> blocks;
    float spacing = std::round(cfg::spacing);
    float pad = cfg::padding;

    if (global_mode_ && !state.monitors.empty()) {
        float gap_x = spacing * (cfg::columns - 1) + pad * 2.0f + cfg::global_block_spacing;
        float gap_y = spacing * (cfg::rows - 1) + pad * 2.0f + cfg::global_block_spacing;
        float frame = 2.0f * (pad + cfg::margin);
        const std::vector<CompositorMonitor> &ms = state.monitors;
        std::vector<float> steps_x = separation_steps(ms, &CompositorMonitor::x, 0);
        std::vector<float> steps_y = separation_steps(ms, &CompositorMonitor::y, 1);
        double min_x = ms[0].x;
        double min_y = ms[0].y;
        for (const CompositorMonitor &m : ms) {
            min_x = std::min(min_x, m.x);
            min_y = std::min(min_y, m.y);
        }
        float scale = cfg::global_scale;
        for (size_t i = 0; i < ms.size(); ++i) {
            WorkArea area = compositor_work_area(ms[i]);
            float fixed_x = steps_x[i] * gap_x + spacing * (cfg::columns - 1) + frame;
            float fixed_y = steps_y[i] * gap_y + spacing * (cfg::rows - 1) + frame;
            float per_x = static_cast<float>((ms[i].x - min_x + std::max(1.0, area.width)) * cfg::columns);
            float per_y = static_cast<float>((ms[i].y - min_y + std::max(1.0, area.height)) * cfg::rows);
            scale = std::min({scale, (width - fixed_x) / per_x, (height - fixed_y) / per_y});
        }
        scale = std::max(scale, 0.01f);
        layout_.scale = scale;
        for (size_t i = 0; i < ms.size(); ++i) {
            WorkArea area = compositor_work_area(ms[i]);
            float x = static_cast<float>((ms[i].x - min_x) * cfg::columns * scale) + steps_x[i] * gap_x;
            float y = static_cast<float>((ms[i].y - min_y) * cfg::rows * scale) + steps_y[i] * gap_y;
            blocks.push_back(make_block(area.width, area.height, overview_page_of(grid, active_workspace(ms[i].name)), scale, x, y));
        }
    } else {
        double work_w = width;
        double work_h = height;
        if (const CompositorMonitor *monitor = bound_monitor()) {
            WorkArea area = compositor_work_area(*monitor);
            if (area.width > 0 && area.height > 0) {
                work_w = area.width;
                work_h = area.height;
            }
        }
        work_w = std::max(1.0, work_w);
        work_h = std::max(1.0, work_h);
        float frame = 2.0f * (pad + cfg::margin);
        float fit_x = (width - frame - spacing * (cfg::columns - 1)) / (static_cast<float>(work_w) * cfg::columns);
        float fit_y = (height - frame - spacing * (cfg::rows - 1)) / (static_cast<float>(work_h) * cfg::rows);
        layout_.scale = std::max(0.01f, std::min({cfg::scale, fit_x, fit_y}));
        blocks.push_back(make_block(work_w, work_h, group_, layout_.scale, 0.0f, 0.0f));
    }

    float grid_w = 0.0f;
    float grid_h = 0.0f;
    for (const Block &b : blocks) {
        grid_w = std::max(grid_w, b.x + b.cell_w * cfg::columns + spacing * (cfg::columns - 1));
        grid_h = std::max(grid_h, b.y + b.cell_h * cfg::rows + spacing * (cfg::rows - 1));
    }
    float root_w = grid_w + pad * 2.0f + cfg::margin * 2.0f;
    float root_h = grid_h + pad * 2.0f + cfg::margin * 2.0f;
    float origin_x = std::round((width - root_w) / 2.0f) + cfg::margin;
    float origin_y = std::round((height - root_h) / 2.0f) + slide_y_ + cfg::margin;

    for (const Block &b : blocks) {
        layout_.panels.push_back({origin_x + b.x, origin_y + b.y, b.cell_w * cfg::columns + spacing * (cfg::columns - 1) + pad * 2.0f, b.cell_h * cfg::rows + spacing * (cfg::rows - 1) + pad * 2.0f});
        for (int row = 0; row < cfg::rows; ++row) {
            for (int col = 0; col < cfg::columns; ++col) {
                layout_.cells.push_back({overview_workspace_at(grid, b.group, row, col), {origin_x + b.x + pad + static_cast<float>(col) * (b.cell_w + spacing), origin_y + b.y + pad + static_cast<float>(row) * (b.cell_h + spacing), b.cell_w, b.cell_h}});
            }
        }
    }
}

void OverviewModel::rebuild_tiles() {
    tiles_.clear();
    const CompositorState &state = compositor_.state();
    std::vector<const CompositorClient *> visible;
    for (const CompositorClient &client : state.clients) {
        if (find_cell(client.workspace_id) != nullptr) {
            visible.push_back(&client);
        }
    }
    std::ranges::sort(visible, [](const CompositorClient *a, const CompositorClient *b) {
        if (a->pinned != b->pinned) {
            return !a->pinned;
        }
        if (a->floating != b->floating) {
            return !a->floating;
        }
        if ((a->fullscreen > 0) != (b->fullscreen > 0)) {
            return !(a->fullscreen > 0);
        }
        if (a->workspace_id != b->workspace_id) {
            return a->workspace_id < b->workspace_id;
        }
        return a->focus_history_id > b->focus_history_id;
    });

    const CompositorMonitor *fallback = bound_monitor();
    for (const CompositorClient *client : visible) {
        const CompositorMonitor *source = compositor_monitor(state, client->monitor_id);
        if (source == nullptr) {
            source = fallback;
        }
        WorkArea area = source != nullptr ? compositor_work_area(*source) : WorkArea{0.0, 0.0, width_, height_};
        area.width = std::max(1.0, area.width);
        area.height = std::max(1.0, area.height);
        ui::Box cell = find_cell(client->workspace_id)->rect;
        double scale = std::min(cell.w / area.width, cell.h / area.height);
        double raw_w = std::max(1.0, client->size[0] * scale);
        double raw_h = std::max(1.0, client->size[1] * scale);
        double w = std::min(raw_w, static_cast<double>(cell.w));
        double h = std::min(raw_h, static_cast<double>(cell.h));
        double x = std::clamp(std::max((client->at[0] - area.x) * scale, 0.0), 0.0, std::max(0.0, cell.w - w));
        double y = std::clamp(std::max((client->at[1] - area.y) * scale, 0.0), 0.0, std::max(0.0, cell.h - h));
        OverviewTile tile;
        tile.address = client->address;
        tile.window_class = client->window_class;
        tile.workspace = client->workspace_id;
        tile.rect = {static_cast<float>(cell.x + x), static_cast<float>(cell.y + y), static_cast<float>(w), static_cast<float>(h)};
        tiles_.push_back(tile);

        auto [it, fresh] = tile_anim_.try_emplace(tile.address);
        TileAnim &anim = it->second;
        if (fresh) {
            anim.current = anim.target = tile.rect;
        } else if (!(anim.target == tile.rect)) {
            const std::string address = tile.address;
            auto animate = [&](int component, float from, float to, float ui::Box::*field) {
                animations_.animate(from, to, cfg::anim_ms, astralia::Easing::EaseOutCubic, [this, address, field](float v) { tile_anim_[address].current.*field = v; }, {}, tile_owner(address, component));
            };
            animate(0, anim.target.x, tile.rect.x, &ui::Box::x);
            animate(1, anim.target.y, tile.rect.y, &ui::Box::y);
            animate(2, anim.target.w, tile.rect.w, &ui::Box::w);
            animate(3, anim.target.h, tile.rect.h, &ui::Box::h);
            tile_anim_[address].target = tile.rect;
        }
    }
    for (auto it = tile_anim_.begin(); it != tile_anim_.end();) {
        bool live = std::ranges::any_of(tiles_, [&](const OverviewTile &t) { return t.address == it->first; });
        it = live ? std::next(it) : tile_anim_.erase(it);
    }
    if (dragging_) {
        std::ranges::stable_partition(tiles_, [this](const OverviewTile &t) { return t.address != drag_address_; });
    }
}

void OverviewModel::update_indicator() {
    indicator_visible_ = false;
    int active = active_workspace(global_mode_ ? compositor_.state().focused_monitor : output_name_);
    int page = overview_page_of(grid, active);
    const OverviewCell *cell = find_cell(active);
    if (cell == nullptr) {
        indicator_tracking_ = false;
        return;
    }
    if (slide_y_ != 0.0f) {
        return;
    }
    const ui::Box &rect = cell->rect;
    if (!indicator_tracking_ || indicator_page_ != page) {
        indicator_ = indicator_target_ = rect;
        indicator_tracking_ = true;
        indicator_page_ = page;
    } else if (indicator_target_.x != rect.x || indicator_target_.y != rect.y) {
        animations_.animate(indicator_.x, rect.x, cfg::anim_ms, astralia::Easing::EaseOutCubic, [this](float v) { indicator_.x = v; }, {}, owner_indicator_x);
        animations_.animate(indicator_.y, rect.y, cfg::anim_ms, astralia::Easing::EaseOutCubic, [this](float v) { indicator_.y = v; }, {}, owner_indicator_y);
        indicator_target_ = rect;
    }
    indicator_.w = rect.w;
    indicator_.h = rect.h;
    indicator_visible_ = true;
}

void OverviewModel::sync(float width, float height) {
    if (!open_) {
        return;
    }
    width_ = width;
    height_ = height;
    compute_layout(width, height);
    rebuild_tiles();
    update_indicator();
}

ui::Box OverviewModel::tile_rect(const OverviewTile &tile) const {
    ui::Box rect = tile.rect;
    if (auto it = tile_anim_.find(tile.address); it != tile_anim_.end()) {
        rect = it->second.current;
    }
    if (dragging_ && tile.address == drag_address_) {
        rect.x = static_cast<float>(drag_pointer_x_ - drag_offset_x_);
        rect.y = static_cast<float>(drag_pointer_y_ - drag_offset_y_);
    }
    return rect;
}

void OverviewModel::select(int workspace, bool shift, bool alt) {
    selected_ = workspace;
    group_ = overview_page_of(grid, workspace);
    compositor_action();
    if (shift) {
        compositor_.swap_workspace(workspace, global_mode_);
    } else if (alt) {
        compositor_.move_workspace_in(workspace, global_mode_);
    } else {
        compositor_.focus_workspace(workspace, global_mode_);
    }
}

bool OverviewModel::key(const input::KeyEvent &event) {
    if (!open_ || closing_) {
        return false;
    }
    if (global_mode_) {
        group_ = overview_page_of(grid, selected_);
    }
    uint32_t sym = event.base_sym != 0 ? event.base_sym : (event.text.size() == 1 ? static_cast<unsigned char>(event.text[0]) : 0u);
    switch (event.kind) {
    case input::KeyKind::Left:
    case input::KeyKind::Right:
    case input::KeyKind::Up:
    case input::KeyKind::Down: {
        int d_col = event.kind == input::KeyKind::Left ? -1 : event.kind == input::KeyKind::Right ? 1
                                                                                                  : 0;
        int d_row = event.kind == input::KeyKind::Up ? -1 : event.kind == input::KeyKind::Down ? 1
                                                                                               : 0;
        select(overview_step(grid, group_, selected_, d_col, d_row), event.shift, event.alt);
        return true;
    }
    case input::KeyKind::Tab:
        if (!global_mode_ && compositor_.state().monitors.size() < 2) {
            spawn_detached("notify-send 'Overview' 'This device only has one display.'");
            return true;
        }
        global_mode_ = !global_mode_;
        dragging_ = false;
        indicator_tracking_ = false;
        group_ = overview_page_of(grid, selected_);
        return true;
    case input::KeyKind::Escape:
        if (on_close_requested) {
            on_close_requested();
        }
        return true;
    case input::KeyKind::Text:
        if (sym >= '0' && sym <= '9') {
            int position = sym == '0' ? 10 : static_cast<int>(sym - '0');
            if (position <= grid.per_page()) {
                select(group_ * grid.per_page() + position, event.shift, event.alt);
            }
            return true;
        }
        if (sym == 'd') {
            compositor_action();
            if (event.ctrl) {
                compositor_.close_windows(CloseScope::all, -1);
            } else if (event.shift) {
                if (const CompositorMonitor *monitor = bound_monitor()) {
                    compositor_.close_windows(CloseScope::monitor, monitor->id);
                }
            } else {
                compositor_.close_windows(CloseScope::workspace, selected_);
            }
            return true;
        }
        return false;
    default:
        return false;
    }
}

void OverviewModel::press(double x, double y) {
    if (!open_ || closing_) {
        return;
    }
    for (auto it = tiles_.rbegin(); it != tiles_.rend(); ++it) {
        ui::Box drawn = tile_rect(*it);
        if (!inside(drawn, x, y)) {
            continue;
        }
        dragging_ = true;
        drag_address_ = it->address;
        drag_from_ = it->workspace;
        drag_target_ = -1;
        drag_offset_x_ = x - drawn.x;
        drag_offset_y_ = y - drawn.y;
        drag_pointer_x_ = x;
        drag_pointer_y_ = y;
        return;
    }
    if (layout_.cells.empty()) {
        if (on_close_requested) {
            on_close_requested();
        }
        return;
    }
    if (const OverviewCell *cell = cell_at(x, y)) {
        select(cell->workspace, false, false);
        return;
    }
    constexpr float m = cfg::margin;
    bool near_panel = std::ranges::any_of(layout_.panels, [&](const ui::Box &p) { return inside({p.x - m, p.y - m, p.w + 2.0f * m, p.h + 2.0f * m}, x, y); });
    if (!near_panel && on_close_requested) {
        on_close_requested();
    }
}

void OverviewModel::move(double x, double y) {
    if (!dragging_) {
        return;
    }
    drag_pointer_x_ = x;
    drag_pointer_y_ = y;
    const OverviewCell *cell = cell_at(x, y);
    drag_target_ = cell != nullptr ? cell->workspace : -1;
}

void OverviewModel::release() {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    const OverviewCell *cell = cell_at(drag_pointer_x_, drag_pointer_y_);
    if (cell == nullptr) {
        return;
    }
    compositor_action();
    if (cell->workspace != drag_from_) {
        compositor_.move_window(drag_address_, cell->workspace, global_mode_);
    } else {
        select(cell->workspace, false, false);
    }
}

bool OverviewModel::clickable(double x, double y) const {
    if (!open_) {
        return false;
    }
    for (const OverviewTile &tile : tiles_) {
        if (inside(tile_rect(tile), x, y)) {
            return true;
        }
    }
    return cell_at(x, y) != nullptr;
}

} // namespace astralia
