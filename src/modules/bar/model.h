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

#include "render/canvas.h"

namespace astralia::bar {

enum class Item : int {
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

inline constexpr size_t item_count = static_cast<size_t>(Item::count);

struct Sources {
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

struct ItemView {
    const char *glyph = nullptr;
    std::string label;
    std::optional<Color> tint;
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

struct DockSlot {
    DockEntry entry;
    float x = 0.0f;
    float appear = 1.0f;
};

enum class ActionKind { none, toggle, overview, workspace };

struct Action {
    ActionKind kind = ActionKind::none;
    Item item = Item::count;
    int workspace = 0;
};

struct Layout {
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

const char *wifi_glyph(int strength);
const char *bluetooth_glyph(const BluetoothStatus &status);
std::string bluetooth_label(const BluetoothStatus &status);
std::chrono::milliseconds ms_until_next_second(std::chrono::system_clock::time_point now);
const char *battery_glyph(const BatteryStatus &status);
std::string battery_label(const BatteryStatus &status);

class Model {
  public:
    using Clock = std::chrono::steady_clock;

    Model(Sources sources, Clock::time_point now);

    void set_style(const StyleSpec &style) { style_ = &style; }
    const StyleSpec &style() const { return *style_; }
    void set_clock_label(std::string label) { clock_label_ = std::move(label); }
    void refresh(Clock::time_point now);
    void set_open(std::optional<Item> item, Clock::time_point now);
    bool hover(std::optional<double> x, std::optional<double> y, Clock::time_point now);
    bool tick(Clock::time_point now);
    void layout(ui::Canvas &canvas, float width);

    Action press(double x, double y) const;
    bool clickable(double x, double y) const;
    bool animating() const { return animations_.has_active(); }
    bool peeking() const { return peek_active_; }
    std::chrono::milliseconds until_idle(Clock::time_point now) const;

    const ItemView &item(Item id) const { return items_[static_cast<size_t>(id)]; }
    const std::vector<BarWorkspace> &workspaces() const { return workspaces_; }
    const std::vector<DockSlot> &dock() const { return dock_; }
    const Layout &layout() const { return layout_; }
    Frame frame() const;
    std::optional<Item> open() const { return open_; }

  private:
    bool apply_hover(Clock::time_point now);
    void refresh_workspaces();
    void refresh_dock();
    bool update_expand(Item id, bool target, bool instant);
    float item_width(const ItemView &view, float pad) const;
    ItemView &mut(Item id) { return items_[static_cast<size_t>(id)]; }
    std::array<std::string, item_count> measured_glyph_;
    std::array<std::string, item_count> measured_label_;

    Sources sources_;
    const StyleSpec *style_;
    std::array<ItemView, item_count> items_{};
    std::vector<BarWorkspace> workspaces_;
    std::unordered_map<int, float> grow_;
    std::unordered_map<int, bool> was_active_;
    std::vector<DockSlot> dock_;
    struct DockMotion {
        float x = 0.0f;
        float target = 0.0f;
        float appear = 1.0f;
        uint64_t owner = 0;
    };
    std::unordered_map<std::string, DockMotion> dock_motion_;
    uint64_t next_dock_owner_ = 0;
    float dock_w_ = 0.0f;
    float dock_w_target_ = 0.0f;
    bool dock_seen_ = false;
    Layout layout_;
    AnimationManager animations_;
    std::string clock_label_;
    std::optional<Item> open_;
    std::optional<Item> hovered_;
    std::optional<Item> linger_;
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

} // namespace astralia::bar
