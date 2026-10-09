#pragma once

#include <string>

#include "render/image_decode.h"

#include "service/icon_service.h"

namespace astralia {

using IconSurface = SurfacePtr;

IconSurface load_app_icon(const std::string &path, int size);

} // namespace astralia
