#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/animation.h"
#include "core/input.h"

#include "modules/overview/paging.h"

#include "service/compositor_service.h"

#include "render/geometry.h"

namespace astralia {

struct OverviewCell {
    int workspace = -1;
    ui::Box rect;
};

struct OverviewLayout {
    std::vector<ui::Box> panels;
    float scale = 0.0f;
    std::vector<OverviewCell> cells;
};

struct OverviewTile {
    std::string address;
    std::string window_class;
    int workspace = -1;
    ui::Box rect;
};

class OverviewModel {
  public:
    explicit OverviewModel(Compositor &compositor);

    void open(std::string output_name, float width, float height);
    void close();
    void tick(std::chrono::steady_clock::time_point now) { animations_.tick(now); }
    void sync(float width, float height);

    bool key(const input::KeyEvent &event);
    void press(double x, double y);
    void move(double x, double y);
    void release();
    bool clickable(double x, double y) const;

    bool is_open() const { return open_; }
    bool dragging() const { return dragging_; }
    bool animating() const { return animations_.hasActive(); }
    bool global_mode() const { return global_mode_; }
    int selected() const { return selected_; }
    int drag_target() const { return drag_target_; }
    float slide_y() const { return slide_y_; }
    const OverviewLayout &layout() const { return layout_; }
    const std::vector<OverviewTile> &tiles() const { return tiles_; }
    ui::Box tile_rect(const OverviewTile &tile) const;
    const ui::Box *indicator() const { return indicator_visible_ ? &indicator_ : nullptr; }
    const std::string &icon_path(const std::string &window_class);
    const std::string &output_name() const { return output_name_; }

    std::function<void()> on_close_requested;
    std::function<void()> on_closed;
    std::function<void()> on_compositor_action;

  private:
    void select(int workspace, bool shift, bool alt);
    void compute_layout(float width, float height);
    void rebuild_tiles();
    void update_indicator();
    const OverviewCell *find_cell(int workspace) const;
    const OverviewCell *cell_at(double x, double y) const;
    int active_workspace(const std::string &monitor_name) const;
    const CompositorMonitor *bound_monitor() const;
    void compositor_action();

    Compositor &compositor_;
    AnimationManager animations_;
    bool open_ = false;
    bool closing_ = false;
    bool global_mode_ = false;
    std::string output_name_;
    float width_ = 0.0f;
    float height_ = 0.0f;
    int group_ = 0;
    int selected_ = -1;
    float slide_y_ = 0.0f;
    OverviewLayout layout_;
    std::vector<OverviewTile> tiles_;
    struct TileAnim {
        ui::Box current;
        ui::Box target;
    };
    std::unordered_map<std::string, TileAnim> tile_anim_;
    ui::Box indicator_;
    ui::Box indicator_target_;
    bool indicator_visible_ = false;
    bool indicator_tracking_ = false;
    int indicator_page_ = -1;
    bool dragging_ = false;
    std::string drag_address_;
    int drag_from_ = -1;
    int drag_target_ = -1;
    double drag_pointer_x_ = 0.0;
    double drag_pointer_y_ = 0.0;
    double drag_offset_x_ = 0.0;
    double drag_offset_y_ = 0.0;
    std::unordered_map<std::string, std::string> icon_paths_;
};

} // namespace astralia
