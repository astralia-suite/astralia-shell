#pragma once

#include <string>

#include "service/settings_service.h"

#include "config/bar_layout.h"
#include "config/rain_config.h"

#include "wayland/config/visualizer_gl_config.h"

using Config = astralia::Config;
using MonitorOverride = astralia::MonitorOverride;

using astralia::ambient_enabled_for;
using astralia::ambient_timeout_for;
using astralia::autohide_enabled_for;
using astralia::BarStyle;
using astralia::lock_enabled_for;
using astralia::notifications_enabled_for;
using astralia::osd_enabled_for;
using astralia::screensaver_enabled_for;
using astralia::screensaver_timeout_for;

std::string config_path();

Config load_config();

void save_config(const Config &cfg);

int config_watch_init(const std::string &path);

struct ConfigWatchEvent {
    bool changed = false;
    bool removed = false;
};

ConfigWatchEvent config_watch_poll(int fd);
