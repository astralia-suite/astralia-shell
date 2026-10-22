#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/shell.h"

#include "config/bar_layout.h"

#include "core/animation.h"
#include "core/input.h"

#include "service/settings_service.h"
#include "service/wallpaper_service.h"

#include "render/geometry.h"
#include "render/scroll_sequence.h"
#include "render/text_field.h"

namespace astralia::settings {

enum class Tab : uint8_t { ambient, bar, displays, general, lock, wallpaper };

struct TabInfo {
    Tab tab;
    const char *label;
    const char *icon;
};

struct Caps {
    bool animation = false;
    bool idle = false;
    bool launcher = false;
    bool rain = false;
    bool visualizer = false;
    bool animated_wallpaper = false;
    bool lock_wallpaper = false;
    bool wallpaper_columns = false;
    bool default_wallpaper = false;
    bool autohide = false;
    std::vector<BarStyle> bar_styles{BarStyle::islands, BarStyle::okinami, BarStyle::continuous};
};

Caps settings_caps(const Capabilities &capabilities);

enum class Field : uint8_t {
    none,
    wallpaper_dir,
    wallpaper_animated_dir,
    lock_wallpaper_dir,
    ambient_timeout,
    screensaver_timeout,
    visualizer_fps,
    visualizer_thin,
    visualizer_size,
    visualizer_complexity,
    visualizer_glow_directions,
    visualizer_glow_quality
};

enum class Act : uint8_t {
    close,
    tab,
    menu,
    displays_monitor,
    toggle_override,
    toggle_osd,
    toggle_notifications,
    toggle_autohide,
    idle_enable,
    idle_ambient,
    idle_screensaver,
    idle_ambient_reset,
    idle_screensaver_reset,
    bar_style,
    rain_mode,
    rain_async,
    visualizer_shape,
    visualizer_reset,
    animation_disable,
    launcher_lightning,
    wallpaper_default,
    wallpaper_animated,
    lock_wallpaper_mode,
    wallpaper_region,
    wallpaper_pick,
    wallpaper_count,
    wallpaper_remove,
    wallpaper_fill,
    wallpaper_rescan,
    dropdown_toggle,
    dropdown_dismiss,
    capture_pick,
    field_focus,
};

struct Region {
    ui::Box box;
    Act act = Act::close;
    int a = 0;
    int b = 0;
};

struct Hooks {
    std::function<const Config &()> config;
    std::function<void(const std::function<void(Config &)> &)> update;
    std::function<std::vector<std::string>()> monitors;
    std::function<std::string()> focused_monitor;
    std::function<bool(const std::string &, int)> cpu_fallback;
};

struct WallpaperPicker {
    std::string dir;
    bool scanning = false;
    std::vector<std::string> files;
    uint64_t generation = 0;
    std::string region;
    int column = 0;
    float scroll = 0.0f;
    float grid_width = 0.0f;
    float grid_height = 0.0f;
};

const std::vector<TabInfo> &all_tabs();
std::string field_text(const Config &config, Field field, const std::string &monitor);
void apply_field(Config &config, Field field, const std::string &text, const std::string &monitor);

class Model {
  public:
    Model(Caps caps, Hooks hooks);
    ~Model();
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    void open();
    void close();
    void sync();
    void tick(std::chrono::steady_clock::time_point now) { animations_.tick(now); }
    void begin_frame() { regions_.clear(); }
    void add_region(const ui::Box &box, Act act, int a = 0, int b = 0) {
        regions_.push_back({box, act, a, b});
    }
    void add_region_front(const ui::Box &box, Act act, int a = 0, int b = 0) {
        regions_.insert(regions_.begin(), {box, act, a, b});
    }
    int dropdown_open() const { return dropdown_open_; }
    void set_panel(const ui::Box &panel) { panel_ = panel; }

    void click(double x, double y);
    bool key(const input::KeyEvent &event);
    void scroll(float pixels);
    void commit_text(const std::string &text);
    void delete_before(uint32_t count);
    void set_preedit(std::string text);
    void toggle_caret();
    bool clickable(double x, double y) const;

    const Caps &caps() const { return caps_; }
    const std::vector<TabInfo> &tabs() const { return tabs_; }
    Tab active_tab() const { return active_; }
    bool in_menu() const { return menu_; }
    float tab_alpha() const { return tab_alpha_; }
    bool animating() const { return animations_.has_active(); }
    bool is_open() const { return open_; }
    const ScrollSequence &sequence() const { return sequence_; }
    Field focused() const { return focused_; }
    const TextFieldState &field() const { return field_; }
    TextFieldState &field() { return field_; }
    const TextFieldTypeAnim &field_anim() const { return field_anim_; }
    const Config &config() const { return hooks_.config(); }
    const std::vector<std::string> &monitors() const { return monitors_; }
    const std::string &displays_monitor() const { return displays_monitor_; }
    WallpaperPicker &picker(WallpaperSlot slot) {
        switch (slot) {
        case WallpaperSlot::animated:
            return animated_picker_;
        case WallpaperSlot::lock:
            return lock_picker_;
        case WallpaperSlot::still:
            break;
        }
        return static_picker_;
    }
    bool cpu_fallback(const std::string &monitor, int column) const {
        return hooks_.cpu_fallback && hooks_.cpu_fallback(monitor, column);
    }
    BarStyle effective_bar_style() const;
    void rescan(WallpaperSlot slot);

    std::function<void()> on_close_requested;
    std::function<void()> on_closed;
    std::function<void()> on_changed;
    std::function<void(bool)> on_text_focus;

  private:
    void changed();
    void select_tab(Tab tab);
    void show_menu();
    void go(bool menu, Tab tab);
    void focus_field(Field field);
    void commit_field();
    void scan(WallpaperPicker &picker, const std::string &dir, bool animated);
    void dispatch(const Region &region);
    void edit(const std::function<void(Config &)> &apply);
    void dispatch_wallpaper(const Region &region);
    void dispatch_idle(const Region &region);

    Caps caps_;
    Hooks hooks_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    AnimationManager animations_;
    ScrollSequence sequence_{animations_, 30};
    std::vector<TabInfo> tabs_;
    std::vector<Region> regions_;
    int dropdown_open_ = -1;
    ui::Box panel_;
    bool open_ = false;
    Tab active_ = Tab::wallpaper;
    Tab pending_ = Tab::wallpaper;
    bool menu_ = true;
    bool pending_menu_ = true;
    float tab_alpha_ = 1.0f;
    Field focused_ = Field::none;
    TextFieldState field_;
    TextFieldTypeAnim field_anim_;
    std::vector<std::string> monitors_;
    std::string displays_monitor_;
    WallpaperPicker static_picker_;
    WallpaperPicker animated_picker_;
    WallpaperPicker lock_picker_;
};

} // namespace astralia::settings
