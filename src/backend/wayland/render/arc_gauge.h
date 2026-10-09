#pragma once

#include "render/tokens.h"
#include "wayland/render/texture.h"
#include "wayland/render/texture_cache.h"

const Texture *cached_arc_gauge(TextureCache &tcache, int32_t scale, float diameter, float stroke, float value01, const astralia::Color &fill_color);
