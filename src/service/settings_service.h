#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "config/bar_style.h"
#include "config/rain_config.h"
#include "config/visualizer_config.h"

#include "core/config_file.h"
#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

struct MonitorOverride {
    bool enabled = false;
    bool bar = true;
    bool osd = true;
    bool notifications = true;
    bool autohide = false;
    bool ambient_enabled = true;
    uint32_t ambient_timeout_seconds = 150;
    bool screensaver_enabled = true;
    uint32_t screensaver_timeout_seconds = 300;
    bool lock = true;

    bool operator==(const MonitorOverride &) const = default;
};

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

    bool autohide = false;
    BarStyle bar_style = BarStyle::islands;
    bool default_bar_enabled = true;
    bool default_osd_enabled = true;
    bool default_notifications_enabled = true;
    bool default_wallpaper_enabled = true;
    bool default_lock_panel_enabled = true;
    std::map<std::string, MonitorOverride> monitor_overrides;

    bool logout_animated_logo = true;
    bool animations_disabled = false;

    bool idle_management_enabled = true;
    bool ambient_enabled = true;
    uint32_t ambient_timeout_seconds = 150;
    bool screensaver_enabled = true;
    uint32_t screensaver_timeout_seconds = 300;

    VisualizerParams visualizer;
    RainParams rain;

    Config();
    bool operator==(const Config &) const = default;
};

bool bar_effective_enabled(const Config &cfg, const std::string &monitor_name);
bool osd_effective_enabled(const Config &cfg, const std::string &monitor_name);
bool notifications_effective_enabled(const Config &cfg, const std::string &monitor_name);
bool autohide_effective_enabled(const Config &cfg, const std::string &monitor_name);
bool ambient_effective_enabled(const Config &cfg, const std::string &monitor_name);
uint32_t ambient_effective_timeout_seconds(const Config &cfg, const std::string &monitor_name);
bool screensaver_effective_enabled(const Config &cfg, const std::string &monitor_name);
uint32_t screensaver_effective_timeout_seconds(const Config &cfg, const std::string &monitor_name);
bool lock_effective_enabled(const Config &cfg, const std::string &monitor_name);

Config parse_config(std::string_view text);
std::string serialize_config(const Config &cfg);
Config config_from_legacy(std::string_view settings_text, std::string_view wallpaper_text, std::string_view home);

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
