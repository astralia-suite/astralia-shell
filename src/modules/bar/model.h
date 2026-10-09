#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/animation.h"

#include "modules/bar/style.h"

#include "service/audio_service.h"
#include "service/battery_service.h"
#include "service/bluetooth_service.h"
#include "service/brightness_service.h"
#include "service/compositor_service.h"
#include "service/dock_service.h"
#include "service/network_service.h"

#include "ui/canvas.h"

namespace astralia {

enum class BarItem : int {
    logout,
    tray,
    resource,
    network,
    bluetooth,
    volume,
    brightness,
    battery,
    media,
    clock,
    count
};

inline constexpr size_t bar_item_count = static_cast<size_t>(BarItem::count);

struct BarSources {
    AudioService *audio = nullptr;
    BatteryService *battery = nullptr;
    BluetoothService *bluetooth = nullptr;
    NetworkService *network = nullptr;
    BrightnessService *brightness = nullptr;
    std::function<const CompositorState &()> compositor;
    std::string output;
    bool resource = false;
    int workspace_slots = 0;
};

struct BarItemView {
    const char *glyph = nullptr;
    std::string label;
    std::optional<Color> border;
    bool alert = false;
    bool visible = false;
    float expand = 0.0f;
    bool hovered = false;
    float icon_w = 0.0f;
    float label_w = 0.0f;
    ui::Box box;
};

struct BarWorkspace {
    int id = 0;
    bool active = false;
    bool occupied = false;
    float grow = 0.0f;
    ui::Box box;
};

struct BarDockSlot {
    DockEntry entry;
    float x = 0.0f;
};

enum class BarActionKind { none,
                           toggle,
                           overview,
                           workspace };

struct BarAction {
    BarActionKind kind = BarActionKind::none;
    BarItem item = BarItem::count;
    int workspace = 0;
};

struct BarLayout {
    std::vector<float> dividers;
    std::optional<float> left_end;
    std::optional<IslandSpan> center;
    std::optional<float> right_start;
    ui::Box workspaces;
    ui::Box overview;
    ui::Box dock;
    float height = 0.0f;
    float width = 0.0f;
};

const char *bar_wifi_glyph(int strength);
const char *bar_bluetooth_glyph(const BluetoothStatus &status);
std::string bar_bluetooth_label(const BluetoothStatus &status);
std::chrono::milliseconds ms_until_next_second(std::chrono::system_clock::time_point now);
const char *bar_battery_glyph(const BatteryStatus &status);
std::string bar_battery_label(const BatteryStatus &status);

class BarModel {
  public:
    using Clock = std::chrono::steady_clock;

    BarModel(BarSources sources, Clock::time_point now);

    void set_style(const BarStyleSpec &style) { style_ = &style; }
    const BarStyleSpec &style() const { return *style_; }
    void set_clock_label(std::string label) { clock_label_ = std::move(label); }
    void refresh(Clock::time_point now);
    void set_open(std::optional<BarItem> item, Clock::time_point now);
    bool hover(std::optional<double> x, std::optional<double> y, Clock::time_point now);
    bool tick(Clock::time_point now);
    void layout(ui::Canvas &canvas, float width);

    BarAction press(double x, double y) const;
    bool clickable(double x, double y) const;
    bool animating() const { return animations_.hasActive(); }
    bool peeking() const { return peek_active_; }
    std::chrono::milliseconds until_idle(Clock::time_point now) const;

    const BarItemView &item(BarItem id) const { return items_[static_cast<size_t>(id)]; }
    const std::vector<BarWorkspace> &workspaces() const { return workspaces_; }
    const std::vector<BarDockSlot> &dock() const { return dock_; }
    const BarLayout &layout() const { return layout_; }
    BarFrame frame() const;
    std::optional<BarItem> open() const { return open_; }

  private:
    bool apply_hover(Clock::time_point now);
    void refresh_workspaces();
    void refresh_dock();
    bool update_expand(BarItem id, bool target, bool instant);
    float item_width(const BarItemView &view, float pad) const;
    BarItemView &mut(BarItem id) { return items_[static_cast<size_t>(id)]; }
    std::array<std::string, bar_item_count> measured_glyph_;
    std::array<std::string, bar_item_count> measured_label_;

    BarSources sources_;
    const BarStyleSpec *style_;
    std::array<BarItemView, bar_item_count> items_{};
    std::vector<BarWorkspace> workspaces_;
    std::unordered_map<int, float> grow_;
    std::unordered_map<int, bool> was_active_;
    std::vector<BarDockSlot> dock_;
    std::unordered_map<std::string, float> dock_x_;
    BarLayout layout_;
    AnimationManager animations_;
    std::string clock_label_;
    std::optional<BarItem> open_;
    std::optional<BarItem> hovered_;
    std::optional<BarItem> linger_;
    Clock::time_point linger_until_{};
    std::optional<double> pointer_x_;
    std::optional<double> pointer_y_;
    Clock::time_point started_;
    bool peek_ready_ = false;
    bool peek_active_ = false;
    Clock::time_point peek_deadline_{};
    int peek_level_ = -1;
    bool peek_muted_ = false;
};

} // namespace astralia
