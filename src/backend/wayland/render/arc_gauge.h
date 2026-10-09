#pragma once

#include "ui/tokens.h"
#include "wayland/render/node.h"
#include "wayland/render/texture.h"
#include "wayland/render/texture_cache.h"

const Texture *cached_arc_gauge(TextureCache &tcache, int32_t scale, float diameter, float stroke, float value01, const astralia::Color &fill_color);

float draw_arc_gauge(Node *root, TextureCache &tcache, int32_t scale, float x, float y, float diameter, float stroke, float value01, const astralia::Color &fill_color, const Texture *icon_tex, const float *icon_color, const Texture *value_tex, const float *value_color, const Texture *sub_tex, const float *sub_label_color, float icon_value_gap, float label_spacing);
