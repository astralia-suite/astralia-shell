#pragma once

#include <memory>
#include <vector>

#include "app/shell.h"

#include "wayland/app/module.h"
#include "wayland/app/per_monitor_module.h"

std::vector<std::unique_ptr<Module>> build_app_modules(const astralia::Capabilities &capabilities);
std::vector<std::unique_ptr<PerMonitorModule>> build_per_monitor_modules(const astralia::Capabilities &capabilities);

void start_session_lock(WaylandState &app);
