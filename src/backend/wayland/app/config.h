#pragma once

#include <string>

#include "service/settings_service.h"

#include "config/bar_layout.h"
#include "wayland/config/rain_config.h"
#include "wayland/config/visualizer_config.h"

using Config = astralia::Config;
using MonitorOverride = astralia::MonitorOverride;

using astralia::ambient_effective_enabled;
using astralia::ambient_effective_timeout_seconds;
using astralia::autohide_effective_enabled;
using astralia::BarStyle;
using astralia::lock_effective_enabled;
using astralia::notifications_effective_enabled;
using astralia::osd_effective_enabled;
using astralia::screensaver_effective_enabled;
using astralia::screensaver_effective_timeout_seconds;

std::string config_path();

Config load_config();

void save_config(const Config &cfg);

int config_watch_init(const std::string &path);

struct ConfigWatchEvent {
    bool changed = false;
    bool removed = false;
};

ConfigWatchEvent config_watch_poll(int fd);
