#pragma once

#include <memory>

#include "wayland/app/module.h"

namespace astralia::wallpaper_select {

std::unique_ptr<Module> make_module();

} // namespace astralia::wallpaper_select
