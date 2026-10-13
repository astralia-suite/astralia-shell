#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <optional>
#include <utility>

#include "core/json.h"
#include "core/log.h"
#include "core/path_home.h"

#include "service/settings_service.h"

#ifndef ASTRALIA_DEFAULT_WALLPAPER
#define ASTRALIA_DEFAULT_WALLPAPER ""
#endif

namespace astralia {

namespace {

constexpr const char *config_relative_path = "astralia/config.json";

std::string home_subdir(const char *name) {
    const char *home = std::getenv("HOME");
    return std::string(home ? home : "") + "/" + name;
}

const Json *section(const Json &root, std::string_view key, std::string_view legacy_key) {
    if (const Json *found = root.find(key); found != nullptr && found->type == Json::Type::object) {
        return found;
    }
    if (const Json *found = root.find(legacy_key); found != nullptr && found->type == Json::Type::object) {
        return found;
    }
    return nullptr;
}

bool flag(const Json *object, std::string_view key, std::string_view legacy_key, bool fallback) {
    if (object == nullptr) {
        return fallback;
    }
    if (object->find(key) != nullptr) {
        return object->boolean_or(key, fallback);
    }
    return object->boolean_or(legacy_key, fallback);
}

bool flag(const Json *object, std::string_view key, bool fallback) { return flag(object, key, key, fallback); }

int integer(const Json *object, std::string_view key, int fallback) {
    return object == nullptr ? fallback : static_cast<int>(object->number_or(key, fallback));
}

uint32_t seconds(const Json *object, std::string_view key, uint32_t fallback) {
    return object == nullptr ? fallback : static_cast<uint32_t>(std::max(0.0, object->number_or(key, fallback)));
}

float real(const Json *object, std::string_view key, float fallback) {
    return object == nullptr ? fallback : static_cast<float>(object->number_or(key, fallback));
}

std::string text(const Json *object, std::string_view key, std::string_view fallback) {
    return object == nullptr ? std::string(fallback) : object->string_or(key, fallback);
}

std::vector<std::string> strings(const Json &array) {
    std::vector<std::string> out;
    for (const Json &item : array.array) {
        if (item.type == Json::Type::string) {
            out.push_back(item.string);
        }
    }
    return out;
}

void read_columns(const Json *wallpaper, std::string_view key, std::map<std::string, std::vector<std::string>> &out, bool expand) {
    const Json *map = wallpaper == nullptr ? nullptr : wallpaper->find(key);
    if (map == nullptr || map->type != Json::Type::object) {
        return;
    }
    for (const Json::Member &member : map->object) {
        if (member.value.type != Json::Type::array) {
            continue;
        }
        std::vector<std::string> values = strings(member.value);
        if (expand) {
            for (std::string &value : values) {
                value = path_expand_home(value);
            }
        }
        out[member.key] = std::move(values);
    }
}

void read_counts(const Json *wallpaper, std::string_view key, std::map<std::string, int> &out) {
    const Json *map = wallpaper == nullptr ? nullptr : wallpaper->find(key);
    if (map == nullptr || map->type != Json::Type::object) {
        return;
    }
    for (const Json::Member &member : map->object) {
        if (member.value.type == Json::Type::number) {
            out[member.key] = static_cast<int>(member.value.number);
        }
    }
}

bool is_reserved_displays_key(const std::string &key) {
    return key == "defaultBar" || key == "defaultOsd" || key == "defaultNotifications" ||
           key == "defaultWallpaper" || key == "defaultLock" || key == "defaultSpark" ||
           key == "defaultHeralds" || key == "defaultExpanse" || key == "defaultPenance";
}

Json from_float(float value) {
    return Json::from_number(std::strtod(std::format("{}", value).c_str(), nullptr));
}

Json from_strings(const std::vector<std::string> &values, bool collapse) {
    Json array = Json::make_array();
    for (const std::string &value : values) {
        array.push(Json::from_string(collapse ? path_collapse_home(value) : value));
    }
    return array;
}

Json columns_json(const std::map<std::string, std::vector<std::string>> &columns, bool collapse) {
    Json object = Json::make_object();
    for (const auto &[name, values] : columns) {
        object.set(name, from_strings(values, collapse));
    }
    return object;
}

Json counts_json(const std::map<std::string, int> &counts) {
    Json object = Json::make_object();
    for (const auto &[name, count] : counts) {
        object.set(name, Json::from_number(count));
    }
    return object;
}

std::optional<BarStyle> style_from(std::string_view value) {
    for (std::size_t i = 0; i < bar_style::count; ++i) {
        if (bar_style::names[i] == value) {
            return static_cast<BarStyle>(i);
        }
    }
    return std::nullopt;
}

Config load_initial(const std::string &path) {
    if (std::optional<std::string> existing = read_text_file(path)) {
        return parse_config(*existing);
    }
    return Config();
}

} // namespace

Config::Config()
    : wallpaper_path(ASTRALIA_DEFAULT_WALLPAPER), wallpaper_dir(home_subdir("Pictures")),
      wallpaper_animated_dir(home_subdir("Videos")) {}

namespace {

const MonitorOverride *active_override(const Config &cfg, const std::string &monitor_name) {
    auto it = cfg.monitor_overrides.find(monitor_name);
    return it != cfg.monitor_overrides.end() && it->second.enabled ? &it->second : nullptr;
}

} // namespace

bool osd_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->osd : cfg.default_osd_enabled;
}

bool notifications_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->notifications : cfg.default_notifications_enabled;
}

bool autohide_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->autohide : cfg.autohide;
}

bool ambient_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    if (!cfg.idle_management_enabled) {
        return false;
    }
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->ambient_enabled : cfg.ambient_enabled;
}

uint32_t ambient_effective_timeout_seconds(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->ambient_timeout_seconds : cfg.ambient_timeout_seconds;
}

bool screensaver_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    if (!cfg.idle_management_enabled) {
        return false;
    }
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->screensaver_enabled : cfg.screensaver_enabled;
}

uint32_t screensaver_effective_timeout_seconds(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->screensaver_timeout_seconds : cfg.screensaver_timeout_seconds;
}

bool lock_effective_enabled(const Config &cfg, const std::string &monitor_name) {
    const MonitorOverride *override = active_override(cfg, monitor_name);
    return override != nullptr ? override->lock : cfg.default_lock_panel_enabled;
}

Config parse_config(std::string_view source) {
    Config cfg;
    std::optional<Json> parsed = parse_json(source);
    if (!parsed || parsed->type != Json::Type::object) {
        return cfg;
    }
    const Json &root = *parsed;

    const Json *bar = section(root, "bar", "qixing");
    cfg.autohide = flag(bar, "autohideEnabled", cfg.autohide);
    if (std::optional<BarStyle> style = style_from(text(bar, "style", bar_style::names[0]))) {
        cfg.bar_style = *style;
    }

    const Json *wallpaper = section(root, "wallpaper", "expanse");
    cfg.wallpaper_path = path_expand_home(text(wallpaper, "path", cfg.wallpaper_path));
    cfg.wallpaper_dir = path_expand_home(text(wallpaper, "dir", cfg.wallpaper_dir));
    read_columns(wallpaper, "columns", cfg.wallpaper_columns, true);
    read_counts(wallpaper, "columnCounts", cfg.wallpaper_column_counts);
    read_columns(wallpaper, "fillModes", cfg.wallpaper_fill_modes, false);
    cfg.wallpaper_animated_enabled = flag(wallpaper, "animatedEnabled", cfg.wallpaper_animated_enabled);
    cfg.wallpaper_animated_dir = path_expand_home(text(wallpaper, "animatedDir", cfg.wallpaper_animated_dir));
    read_columns(wallpaper, "animatedColumns", cfg.wallpaper_animated_columns, true);
    read_counts(wallpaper, "animatedColumnCounts", cfg.wallpaper_animated_column_counts);
    read_columns(wallpaper, "animatedFillModes", cfg.wallpaper_animated_fill_modes, false);

    const Json *displays = root.find("displays");
    if (displays != nullptr && displays->type != Json::Type::object) {
        displays = nullptr;
    }
    cfg.default_osd_enabled = flag(displays, "defaultOsd", "defaultSpark", cfg.default_osd_enabled);
    cfg.default_notifications_enabled = flag(displays, "defaultNotifications", "defaultHeralds", cfg.default_notifications_enabled);
    cfg.default_wallpaper_enabled = flag(displays, "defaultWallpaper", "defaultExpanse", cfg.default_wallpaper_enabled);
    cfg.default_lock_panel_enabled = flag(displays, "defaultLock", "defaultPenance", cfg.default_lock_panel_enabled);
    if (displays != nullptr) {
        for (const Json::Member &member : displays->object) {
            if (is_reserved_displays_key(member.key) || member.value.type != Json::Type::object) {
                continue;
            }
            const Json *value = &member.value;
            MonitorOverride mo;
            mo.enabled = flag(value, "_enabled", mo.enabled);
            mo.osd = flag(value, "osd", "spark", mo.osd);
            mo.notifications = flag(value, "notifications", "heralds", mo.notifications);
            mo.autohide = flag(value, "autohide", mo.autohide);
            mo.ambient_enabled = flag(value, "ambientEnabled", mo.ambient_enabled);
            mo.ambient_timeout_seconds = seconds(value, "ambientTimeoutSeconds", mo.ambient_timeout_seconds);
            mo.screensaver_enabled = flag(value, "screensaverEnabled", mo.screensaver_enabled);
            mo.screensaver_timeout_seconds = seconds(value, "screensaverTimeoutSeconds", mo.screensaver_timeout_seconds);
            mo.lock = flag(value, "lock", "penance", mo.lock);
            cfg.monitor_overrides[member.key] = mo;
        }
    }

    const Json *launcher = section(root, "launcher", "launcher");
    cfg.launcher_lightning = flag(launcher, "lightning", cfg.launcher_lightning);

    const Json *logout = section(root, "logout", "starward");
    cfg.logout_animated_logo = flag(logout, "animatedLogo", cfg.logout_animated_logo);

    const Json *animation = section(root, "animation", "animation");
    cfg.animations_disabled = flag(animation, "disabled", cfg.animations_disabled);

    const Json *idle = section(root, "idle", "blink");
    cfg.idle_management_enabled = flag(idle, "enabled", cfg.idle_management_enabled);
    cfg.ambient_enabled = flag(idle, "ambientEnabled", cfg.ambient_enabled);
    cfg.ambient_timeout_seconds = seconds(idle, "ambientTimeoutSeconds", cfg.ambient_timeout_seconds);
    cfg.screensaver_enabled = flag(idle, "screensaverEnabled", cfg.screensaver_enabled);
    cfg.screensaver_timeout_seconds = seconds(idle, "screensaverTimeoutSeconds", cfg.screensaver_timeout_seconds);

    const Json *visualizer = section(root, "visualizer", "resonance");
    cfg.visualizer.fps = std::clamp(integer(visualizer, "fps", cfg.visualizer.fps), kVisualizerFpsMin, kVisualizerFpsMax);
    cfg.visualizer.particle_thin = std::clamp(real(visualizer, "particleThin", cfg.visualizer.particle_thin), kVisualizerParticleThinMin, kVisualizerParticleThinMax);
    cfg.visualizer.particle_size = kVisualizerParticleSize;
    cfg.visualizer.fractal_complexity = std::clamp(integer(visualizer, "fractalComplexity", cfg.visualizer.fractal_complexity), kVisualizerComplexityMin, kVisualizerComplexityMax);
    cfg.visualizer.glow_directions = std::clamp(real(visualizer, "glowDirections", cfg.visualizer.glow_directions), kVisualizerGlowDirectionsMin, kVisualizerGlowDirectionsMax);
    cfg.visualizer.glow_quality = std::clamp(real(visualizer, "glowQuality", cfg.visualizer.glow_quality), kVisualizerGlowQualityMin, kVisualizerGlowQualityMax);
    cfg.visualizer.visualizer_shape = text(visualizer, "visualizerShape", "bar") == "sphere" ? VisualizerShape::Sphere : VisualizerShape::Bar;

    const Json *rain = root.find("rain");
    if (rain != nullptr && rain->type != Json::Type::object) {
        rain = nullptr;
    }
    cfg.rain.mode = text(rain, "mode", "matrix") == "stiletto" ? RainMode::Stiletto : RainMode::Matrix;
    cfg.rain.async_speed = flag(rain, "asyncSpeed", false);
    return cfg;
}

std::string serialize_config(const Config &cfg) {
    Json wallpaper = Json::make_object();
    wallpaper.set("path", Json::from_string(path_collapse_home(cfg.wallpaper_path)));
    wallpaper.set("dir", Json::from_string(path_collapse_home(cfg.wallpaper_dir)));
    wallpaper.set("columns", columns_json(cfg.wallpaper_columns, true));
    wallpaper.set("columnCounts", counts_json(cfg.wallpaper_column_counts));
    wallpaper.set("fillModes", columns_json(cfg.wallpaper_fill_modes, false));
    wallpaper.set("animatedEnabled", Json::from_bool(cfg.wallpaper_animated_enabled));
    wallpaper.set("animatedDir", Json::from_string(path_collapse_home(cfg.wallpaper_animated_dir)));
    wallpaper.set("animatedColumns", columns_json(cfg.wallpaper_animated_columns, true));
    wallpaper.set("animatedColumnCounts", counts_json(cfg.wallpaper_animated_column_counts));
    wallpaper.set("animatedFillModes", columns_json(cfg.wallpaper_animated_fill_modes, false));

    Json displays = Json::make_object();
    displays.set("defaultOsd", Json::from_bool(cfg.default_osd_enabled));
    displays.set("defaultNotifications", Json::from_bool(cfg.default_notifications_enabled));
    displays.set("defaultWallpaper", Json::from_bool(cfg.default_wallpaper_enabled));
    displays.set("defaultLock", Json::from_bool(cfg.default_lock_panel_enabled));
    for (const auto &[name, ov] : cfg.monitor_overrides) {
        Json mo = Json::make_object();
        mo.set("_enabled", Json::from_bool(ov.enabled));
        mo.set("osd", Json::from_bool(ov.osd));
        mo.set("notifications", Json::from_bool(ov.notifications));
        mo.set("autohide", Json::from_bool(ov.autohide));
        mo.set("ambientEnabled", Json::from_bool(ov.ambient_enabled));
        mo.set("ambientTimeoutSeconds", Json::from_number(ov.ambient_timeout_seconds));
        mo.set("screensaverEnabled", Json::from_bool(ov.screensaver_enabled));
        mo.set("screensaverTimeoutSeconds", Json::from_number(ov.screensaver_timeout_seconds));
        mo.set("lock", Json::from_bool(ov.lock));
        displays.set(name, std::move(mo));
    }

    Json idle = Json::make_object();
    idle.set("enabled", Json::from_bool(cfg.idle_management_enabled));
    idle.set("ambientEnabled", Json::from_bool(cfg.ambient_enabled));
    idle.set("ambientTimeoutSeconds", Json::from_number(cfg.ambient_timeout_seconds));
    idle.set("screensaverEnabled", Json::from_bool(cfg.screensaver_enabled));
    idle.set("screensaverTimeoutSeconds", Json::from_number(cfg.screensaver_timeout_seconds));

    Json visualizer = Json::make_object();
    visualizer.set("fps", Json::from_number(cfg.visualizer.fps));
    visualizer.set("particleThin", from_float(cfg.visualizer.particle_thin));
    visualizer.set("particleSize", Json::from_number(cfg.visualizer.particle_size));
    visualizer.set("fractalComplexity", Json::from_number(cfg.visualizer.fractal_complexity));
    visualizer.set("glowDirections", from_float(cfg.visualizer.glow_directions));
    visualizer.set("glowQuality", from_float(cfg.visualizer.glow_quality));
    visualizer.set("visualizerShape", Json::from_string(cfg.visualizer.visualizer_shape == VisualizerShape::Sphere ? "sphere" : "bar"));

    Json rain = Json::make_object();
    rain.set("mode", Json::from_string(cfg.rain.mode == RainMode::Stiletto ? "stiletto" : "matrix"));
    rain.set("asyncSpeed", Json::from_bool(cfg.rain.async_speed));

    Json bar = Json::make_object();
    bar.set("autohideEnabled", Json::from_bool(cfg.autohide));
    bar.set("style", Json::from_string(std::string(bar_style::names[static_cast<std::size_t>(cfg.bar_style)])));

    Json launcher = Json::make_object();
    launcher.set("lightning", Json::from_bool(cfg.launcher_lightning));

    Json logout = Json::make_object();
    logout.set("animatedLogo", Json::from_bool(cfg.logout_animated_logo));
    Json animation = Json::make_object();
    animation.set("disabled", Json::from_bool(cfg.animations_disabled));

    Json root = Json::make_object();
    root.set("bar", std::move(bar));
    root.set("wallpaper", std::move(wallpaper));
    root.set("displays", std::move(displays));
    root.set("launcher", std::move(launcher));
    root.set("logout", std::move(logout));
    root.set("animation", std::move(animation));
    root.set("idle", std::move(idle));
    root.set("visualizer", std::move(visualizer));
    root.set("rain", std::move(rain));
    return write_json(root, true) + "\n";
}

SettingsService::SettingsService(Reactor &loop)
    : path_(user_config_path(config_relative_path)), config_(load_initial(path_)),
      watch_(loop, path_, [this] { reload(); }) {}

void SettingsService::update(const std::function<void(Config &)> &edit) {
    Config next = config_;
    edit(next);
    store(std::move(next));
}

void SettingsService::store(Config next) {
    if (next == config_) {
        return;
    }
    if (!write_text_file(path_, serialize_config(next))) {
        return;
    }
    config_ = std::move(next);
    changed.emit();
}

void SettingsService::reload() {
    std::optional<std::string> source = read_text_file(path_);
    if (!source) {
        log::info("settings: config removed, using defaults");
        if (config_ == Config()) {
            return;
        }
        config_ = Config();
        changed.emit();
        return;
    }
    Config next = parse_config(*source);
    if (next == config_) {
        return;
    }
    log::info("settings: reloaded {}", path_);
    config_ = std::move(next);
    changed.emit();
}

} // namespace astralia
