#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "config/bar_layout.h"
#include "config/launcher_config.h"
#include "config/rain_config.h"
#include "config/visualizer_config.h"

#include "core/config_file.h"
#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

std::string default_wallpaper_path();

struct MonitorOverride {
    bool enabled = false;
    bool osd = true;
    bool notifications = true;
    bool autohide = false;
    bool ambient_enabled = true;
    uint32_t ambient_timeout = 150;
    bool screensaver_enabled = true;
    uint32_t screensaver_timeout = 300;
    bool lock = true;

    bool operator==(const MonitorOverride &) const = default;
};

enum class PaletteSource : uint8_t { default_palette, region_capture };

struct Config {
    std::string wallpaper_path;
    std::string wallpaper_dir;

    std::map<std::string, std::vector<std::string>> wallpaper_columns;
    std::map<std::string, int> wallpaper_column_counts;
    std::map<std::string, std::vector<std::string>> wallpaper_fill_modes;

    bool wallpaper_animated_enabled = false;
    std::string wallpaper_animated_dir;
    std::map<std::string, std::vector<std::string>> wallpaper_animated_columns;
    std::map<std::string, int> wallpaper_animated_column_counts;
    std::map<std::string, std::vector<std::string>> wallpaper_animated_fill_modes;

    bool lock_wallpaper_custom = false;
    std::string lock_wallpaper_dir;
    std::map<std::string, std::vector<std::string>> lock_wallpaper_columns;
    std::map<std::string, int> lock_wallpaper_column_counts;
    std::map<std::string, std::vector<std::string>> lock_wallpaper_fill_modes;

    PaletteSource palette_source = PaletteSource::default_palette;
    std::string capture_monitor;
    int capture_column = 0;

    bool autohide = false;
    BarStyle bar_style = BarStyle::islands;
    bool default_osd = true;
    bool default_notifications = true;
    bool default_wallpaper = true;
    bool default_lock = true;
    std::map<std::string, MonitorOverride> monitor_overrides;

    bool launcher_lightning = true;

    bool animations_disabled = false;

    std::string logout_logo_path;
    bool sync_compositor_borders = true;
    std::string idle_logo_path;

    bool idle_enabled = true;
    bool ambient_enabled = true;
    uint32_t ambient_timeout = 150;
    bool screensaver_enabled = true;
    uint32_t screensaver_timeout = 300;

    VisualizerParams visualizer;
    RainParams rain;

    Config();
    bool operator==(const Config &) const = default;
};

bool osd_enabled_for(const Config &cfg, const std::string &monitor_name);
bool notifications_enabled_for(const Config &cfg, const std::string &monitor_name);
bool autohide_enabled_for(const Config &cfg, const std::string &monitor_name);
bool ambient_enabled_for(const Config &cfg, const std::string &monitor_name);
uint32_t ambient_timeout_for(const Config &cfg, const std::string &monitor_name);
bool screensaver_enabled_for(const Config &cfg, const std::string &monitor_name);
uint32_t screensaver_timeout_for(const Config &cfg, const std::string &monitor_name);
bool lock_enabled_for(const Config &cfg, const std::string &monitor_name);

Config parse_config(std::string_view text);
std::string serialize_config(const Config &cfg);

class SettingsService {
  public:
    explicit SettingsService(Reactor &loop);

    const Config &config() const { return config_; }
    void update(const std::function<void(Config &)> &edit);

    Signal<> changed;

  private:
    void store(Config next);
    void reload();

    std::string path_;
    Config config_;
    FileWatch watch_;
};

} // namespace astralia
