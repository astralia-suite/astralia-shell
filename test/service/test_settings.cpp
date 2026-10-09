#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "core/poll_reactor.h"

#include "service/settings_service.h"

#include "check.h"
#include "core/pump.h"

namespace {

std::string sample_config() {
    return R"({
  "animation": {"disabled": false},
  "bar": {"autohideEnabled": true, "style": "continuous"},
  "displays": {
    "defaultLock": true,
    "defaultNotifications": true,
    "defaultOsd": false,
    "defaultWallpaper": false,
    "eDP-1": {"_enabled": true, "osd": true, "lock": false, "ambientTimeoutSeconds": 90}
  },
  "idle": {"ambientEnabled": true, "ambientTimeoutSeconds": 150, "enabled": false, "screensaverEnabled": true, "screensaverTimeoutSeconds": 300},
  "logout": {"animatedLogo": true},
  "rain": {"asyncSpeed": true, "mode": "stiletto"},
  "visualizer": {"fps": 30, "fractalComplexity": 3, "glowDirections": 16.0, "glowQuality": 6.0, "particleSize": 8, "particleThin": 0.11999999731779099, "visualizerShape": "sphere"},
  "wallpaper": {
    "animatedColumnCounts": {},
    "animatedColumns": {"eDP-1": ["~/clips/a.mp4"]},
    "animatedDir": "~/clips",
    "animatedEnabled": false,
    "animatedFillModes": {"eDP-1": ["crop"]},
    "columnCounts": {"eDP-1": 2},
    "columns": {"eDP-1": ["~/Pictures/a.jpg", "/abs/b.png"]},
    "dir": "~/Pictures",
    "fillModes": {"eDP-1": ["fit", "crop"]}
  }
})";
}

} // namespace

void check_config_parse() {
    using namespace astralia;
    using test::check;
    setenv("HOME", "/home/tester", 1);
    Config defaults = parse_config("");
    check(defaults == Config(), "empty text gives the defaults");
    check(parse_config("not json") == Config(), "invalid json gives the defaults");
    check(parse_config("[1,2]") == Config(), "a non-object gives the defaults");

    Config cfg = parse_config(sample_config());
    check(cfg.autohide && cfg.bar_style == BarStyle::continuous, "bar section");
    check(!cfg.default_osd_enabled && !cfg.default_wallpaper_enabled && cfg.default_lock_panel_enabled && cfg.default_bar_enabled, "display defaults, with defaultBar absent");
    check(cfg.monitor_overrides.contains("eDP-1") && cfg.monitor_overrides["eDP-1"].enabled && cfg.monitor_overrides["eDP-1"].osd && !cfg.monitor_overrides["eDP-1"].lock, "monitor override fields");
    check(cfg.monitor_overrides["eDP-1"].ambient_timeout_seconds == 90 && cfg.monitor_overrides["eDP-1"].screensaver_timeout_seconds == 300, "override seconds keep defaults when absent");
    check(!cfg.idle_management_enabled, "idle disabled");
    check(cfg.rain.mode == RainMode::Stiletto && cfg.rain.async_speed, "rain");
    check(cfg.visualizer.visualizer_shape == VisualizerShape::Sphere && cfg.visualizer.fps == 30, "visualizer");
    check(cfg.wallpaper_columns["eDP-1"] == std::vector<std::string>{"/home/tester/Pictures/a.jpg", "/abs/b.png"}, "home is expanded in columns");
    check(cfg.wallpaper_dir == "/home/tester/Pictures" && cfg.wallpaper_animated_dir == "/home/tester/clips", "home is expanded in directories");
    check(cfg.wallpaper_column_counts["eDP-1"] == 2 && cfg.wallpaper_fill_modes["eDP-1"] == std::vector<std::string>{"fit", "crop"}, "counts and fill modes");

    Config round = parse_config(serialize_config(cfg));
    check(round == cfg, "serialize then parse round-trips");
    check(serialize_config(round) == serialize_config(cfg), "serialization is stable");
    std::string text = serialize_config(cfg);
    check(text.find("\"autohideEnabled\": true") != std::string::npos, "bar key is written");
    check(text.find("~/Pictures/a.jpg") != std::string::npos, "home is collapsed when saving");
    check(text.find("\"particleThin\": 0.12") != std::string::npos, "floats are written in their shortest form");

    check(parse_config(R"({"bar": {"style": "classic"}})").bar_style == BarStyle::islands, "an unknown style falls back");
    check(parse_config(R"({"qixing": {"autohideEnabled": true}})").autohide, "legacy section names still load");
    check(parse_config(R"({"displays": {"defaultSpark": false}})").default_osd_enabled == false, "legacy display keys still load");
    check(parse_config(R"({"visualizer": {"fps": 9999, "glowQuality": 100}})").visualizer.fps == kVisualizerFpsMax, "visualizer values are clamped");
    check(parse_config(R"({"bar": {"autohideEnabled": "yes"}})").autohide == false, "a wrongly typed value keeps the default");
}

void check_monitor_overrides() {
    using namespace astralia;
    using test::check;
    Config cfg;
    check(osd_effective_enabled(cfg, "DP-1") == cfg.default_osd_enabled, "default osd");
    check(notifications_effective_enabled(cfg, "DP-1") == cfg.default_notifications_enabled, "default notifications");
    check(autohide_effective_enabled(cfg, "DP-1") == cfg.autohide, "default autohide");
    cfg.monitor_overrides["DP-1"] = MonitorOverride{.enabled = false, .osd = false, .notifications = false, .autohide = true};
    check(osd_effective_enabled(cfg, "DP-1") == cfg.default_osd_enabled, "a disabled override is ignored");
    cfg.monitor_overrides["DP-1"].enabled = true;
    check(!osd_effective_enabled(cfg, "DP-1") && !notifications_effective_enabled(cfg, "DP-1") && autohide_effective_enabled(cfg, "DP-1"), "an enabled override wins");
    check(osd_effective_enabled(cfg, "HDMI-1") == cfg.default_osd_enabled, "other outputs keep the default");
    check(lock_effective_enabled(cfg, "HDMI-1") == cfg.default_lock_panel_enabled, "default lock");
    cfg.monitor_overrides["DP-1"].lock = false;
    check(!lock_effective_enabled(cfg, "DP-1"), "lock override");
    cfg.monitor_overrides["DP-1"].bar = false;
    check(!bar_effective_enabled(cfg, "DP-1") && bar_effective_enabled(cfg, "HDMI-1"), "bar override");
    cfg.idle_management_enabled = false;
    check(!ambient_effective_enabled(cfg, "HDMI-1") && !screensaver_effective_enabled(cfg, "DP-1"), "idle management off disables ambient and screensaver everywhere");
    cfg.idle_management_enabled = true;
    cfg.monitor_overrides["DP-1"].ambient_timeout_seconds = 42;
    check(ambient_effective_timeout_seconds(cfg, "DP-1") == 42 && ambient_effective_timeout_seconds(cfg, "HDMI-1") == cfg.ambient_timeout_seconds, "ambient timeout override");
}

void check_legacy_config() {
    using namespace astralia;
    using test::check;
    Config cfg = config_from_legacy("# c\nbar = on\nosd = off\nnotifications = on\nbar_style = okinami\nwallpaper_dir = ~/Pics\nHDMI-1.osd = on\nHDMI-1.bar = off\nbad line\n", "eDP-1 = ~/a.png\n* = /g.png\n", "/home/u");
    check(cfg.default_bar_enabled && !cfg.default_osd_enabled && cfg.default_notifications_enabled, "legacy defaults");
    check(cfg.bar_style == BarStyle::okinami && cfg.wallpaper_dir == "/home/u/Pics", "legacy style and directory");
    const MonitorOverride &mo = cfg.monitor_overrides["HDMI-1"];
    check(mo.enabled && mo.osd && !mo.bar && mo.notifications, "an override takes unspecified features from the defaults");
    check(cfg.wallpaper_columns["eDP-1"] == std::vector<std::string>{"/home/u/a.png"} && cfg.wallpaper_path == "/g.png", "legacy images become columns, and * the global image");
    check(config_from_legacy("", "", "/home/u") == Config(), "no legacy text gives the defaults");
}

void check_settings_service() {
    using namespace astralia;
    using test::check;
    char tmpl[] = "/tmp/astralia-settings-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    check(dir != nullptr, "temporary directory");
    if (dir == nullptr) {
        return;
    }
    setenv("HOME", dir, 1);
    setenv("XDG_CONFIG_HOME", (std::string(dir) + "/.config").c_str(), 1);
    std::string config_path = std::string(dir) + "/.config/astralia/config.json";
    {
        std::filesystem::create_directories(std::string(dir) + "/.config/astralia-shell");
        std::ofstream(std::string(dir) + "/.config/astralia-shell/settings.conf") << "osd = off\nbar_style = continuous\n";
        std::ofstream(std::string(dir) + "/.config/astralia-shell/wallpaper.conf") << "eDP-1 = ~/w.png\n";
    }
    auto created = PollReactor::create();
    check(created.has_value(), "reactor");
    if (!created) {
        return;
    }
    PollReactor reactor = std::move(*created);
    {
        SettingsService service(reactor);
        check(std::filesystem::exists(config_path), "the migrated config is written");
        check(!service.config().default_osd_enabled && service.config().bar_style == BarStyle::continuous, "the legacy files are imported");
        check(service.config().wallpaper_columns.at("eDP-1") == std::vector<std::string>{std::string(dir) + "/w.png"}, "the legacy wallpaper is imported");

        int emitted = 0;
        service.changed.connect([&] { ++emitted; });
        service.update([](Config &cfg) { cfg.autohide = true; });
        check(emitted == 1 && service.config().autohide, "update stores and emits");
        service.update([](Config &cfg) { cfg.autohide = true; });
        check(emitted == 1, "an unchanged update emits nothing");
        reactor.run_once(50);
        check(emitted == 1, "the service's own write is not reloaded as a change");

        {
            Config external = service.config();
            external.rain.mode = RainMode::Stiletto;
            std::ofstream(config_path, std::ios::trunc) << serialize_config(external);
        }
        check(test::pump_until(reactor, [&] { return emitted == 2; }), "an external edit is reloaded");
        check(service.config().rain.mode == RainMode::Stiletto, "the external value is applied");

        std::filesystem::remove(config_path);
        check(test::pump_until(reactor, [&] { return std::filesystem::exists(config_path); }), "a removed config is rewritten with defaults");
    }
    std::filesystem::remove_all(dir);
}
