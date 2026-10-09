#pragma once

#include <initializer_list>
#include <string>

#include "wayland/render/texture.h"

unsigned char *load_image_decode(const std::string &path, int &width, int &height, int svg_target_px = 0, int hint_w = 0, int hint_h = 0);

Texture load_image_texture_first_existing(std::initializer_list<const char *> candidates, int svg_target_px = 0);
