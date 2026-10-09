#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <memory>

#include "modules/bar/panel/panel.h"

namespace astralia {

enum class PanelId : int {
    tray,
    resource,
    network,
    bluetooth,
    volume,
    battery,
    media,
    brightness,
    clock,
    count
};

class PanelSet {
  public:
    explicit PanelSet(Reactor &reactor) : reactor_(reactor) {}

    void add(PanelId id, std::unique_ptr<PanelContent> content);
    Panel *find(PanelId id);
    const Panel *find(PanelId id) const;
    Panel *active();
    PanelId active_id() const;
    bool any_open() const;
    bool animating() const;

    void toggle(PanelId id);
    void close_all();
    void close_except(PanelId keep);
    void tick(std::chrono::steady_clock::time_point now);
    ui::Box paint(ui::Canvas &canvas, float surface_width, float top);
    bool clickable(double x, double y) const;
    bool contains(double x, double y) const;

    std::function<void()> on_changed;
    std::function<void(PanelId, bool)> on_state;

  private:
    static constexpr size_t slots = static_cast<size_t>(PanelId::count);

    std::array<std::unique_ptr<Panel>, slots> panels_;
    PanelId latest_ = PanelId::count;
    Reactor &reactor_;
};

} // namespace astralia
