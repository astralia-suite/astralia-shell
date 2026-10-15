#pragma once

#include <memory>

#include "wayland/app/module.h"

std::unique_ptr<Module> make_wallpaper_select_module();
