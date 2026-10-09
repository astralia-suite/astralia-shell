#pragma once

#include "app/shell.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/bar_style.h"

#include "core/animation.h"
#include "core/input.h"

#include "service/settings_service.h"

#include "ui/geometry.h"
#include "ui/text_field.h"

namespace astralia {

enum class SettingsTab : uint8_t { animation,
                                   bar,
                                   displays,
                                   idle,
                                   logout,
                                   rain,
                                   visualizer,
                                   wallpaper };

struct SettingsTabInfo {
    SettingsTab tab;
    const char *label;
    const char *icon;
};

struct SettingsCaps {
    bool animation = false;
    bool idle = false;
    bool logout = false;
    bool rain = false;
    bool visualizer = false;
    bool animated_wallpaper = false;
    bool wallpaper_columns = false;
    bool default_wallpaper = false;
    bool bar_toggle = true;
    bool autohide = false;
    bool remember_tab = false;
    std::vector<BarStyle> bar_styles{BarStyle::islands, BarStyle::okinami, BarStyle::continuous};
};

SettingsCaps settings_caps(const Capabilities &capabilities);

enum class SettingsField : uint8_t { none,
                                     wallpaper_dir,
                                     wallpaper_animated_dir,
                                     ambient_timeout,
                                     screensaver_timeout,
                                     visualizer_fps,
                                     visualizer_thin,
                                     visualizer_size,
                                     visualizer_complexity,
                                     visualizer_glow_directions,
                                     visualizer_glow_quality };

enum class SettingsAct : uint8_t {
    close,
    tab,
    displays_monitor,
    idle_monitor,
    toggle_override,
    toggle_bar,
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
    logout_logo,
    wallpaper_default,
    wallpaper_animated,
    wallpaper_region,
    wallpaper_pick,
    wallpaper_count,
    wallpaper_remove,
    wallpaper_fill,
    wallpaper_rescan,
    field_focus,
};

struct SettingsRegion {
    ui::Box box;
    SettingsAct act = SettingsAct::close;
    int a = 0;
    int b = 0;
};

struct SettingsHooks {
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

const std::vector<SettingsTabInfo> &settings_all_tabs();
std::string settings_field_text(const Config &config, SettingsField field, const std::string &monitor);
void settings_apply_field(Config &config, SettingsField field, const std::string &text, const std::string &monitor);
bool settings_picker_less(const std::string &a, const std::string &b);
bool settings_is_image(const std::string &path);
bool settings_is_video(const std::string &path);

class SettingsModel {
  public:
    SettingsModel(SettingsCaps caps, SettingsHooks hooks);
    ~SettingsModel();
    SettingsModel(const SettingsModel &) = delete;
    SettingsModel &operator=(const SettingsModel &) = delete;

    void open();
    void close();
    void sync();
    void tick(std::chrono::steady_clock::time_point now) { animations_.tick(now); }
    void begin_frame() { regions_.clear(); }
    void add_region(const ui::Box &box, SettingsAct act, int a = 0, int b = 0) { regions_.push_back({box, act, a, b}); }
    void set_panel(const ui::Box &panel) { panel_ = panel; }

    void click(double x, double y);
    bool key(const input::KeyEvent &event);
    void scroll(float pixels);
    void commit_text(const std::string &text);
    void delete_before(uint32_t count);
    void set_preedit(std::string text);
    void toggle_caret();
    bool clickable(double x, double y) const;

    const SettingsCaps &caps() const { return caps_; }
    const std::vector<SettingsTabInfo> &tabs() const { return tabs_; }
    SettingsTab active_tab() const { return active_; }
    float tab_alpha() const { return tab_alpha_; }
    bool animating() const { return animations_.hasActive(); }
    bool is_open() const { return open_; }
    SettingsField focused() const { return focused_; }
    const TextFieldState &field() const { return field_; }
    TextFieldState &field() { return field_; }
    const TextFieldTypeAnim &field_anim() const { return field_anim_; }
    const Config &config() const { return hooks_.config(); }
    const std::vector<std::string> &monitors() const { return monitors_; }
    const std::string &displays_monitor() const { return displays_monitor_; }
    const std::string &idle_monitor() const { return idle_monitor_; }
    WallpaperPicker &picker(bool animated) { return animated ? animated_picker_ : static_picker_; }
    bool cpu_fallback(const std::string &monitor, int column) const { return hooks_.cpu_fallback && hooks_.cpu_fallback(monitor, column); }
    BarStyle effective_bar_style() const;
    void rescan(bool animated);

    std::function<void()> on_close_requested;
    std::function<void()> on_changed;
    std::function<void(bool)> on_text_focus;

  private:
    void changed();
    void select_tab(SettingsTab tab);
    void focus_field(SettingsField field);
    void commit_field();
    void scan(WallpaperPicker &picker, const std::string &dir, bool animated);
    void dispatch(const SettingsRegion &region);
    void edit(const std::function<void(Config &)> &apply);
    void dispatch_wallpaper(const SettingsRegion &region);
    void dispatch_idle(const SettingsRegion &region);

    SettingsCaps caps_;
    SettingsHooks hooks_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    AnimationManager animations_;
    std::vector<SettingsTabInfo> tabs_;
    std::vector<SettingsRegion> regions_;
    ui::Box panel_;
    bool open_ = false;
    SettingsTab active_ = SettingsTab::wallpaper;
    SettingsTab pending_ = SettingsTab::wallpaper;
    float tab_alpha_ = 1.0f;
    SettingsField focused_ = SettingsField::none;
    TextFieldState field_;
    TextFieldTypeAnim field_anim_;
    std::vector<std::string> monitors_;
    std::string displays_monitor_;
    std::string idle_monitor_;
    WallpaperPicker static_picker_;
    WallpaperPicker animated_picker_;
};

} // namespace astralia
