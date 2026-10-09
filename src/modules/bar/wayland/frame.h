#pragma once

#include <cstdint>
#include <vector>

#include "modules/bar/style.h"

#include "wayland/render/node.h"
#include "wayland/render/texture.h"

struct BarDecor {
    Texture fillet_left;
    Texture fillet_right;
    Texture fillet_inner_left;
    Texture fillet_inner_right;
    int fillet_px = 0;
    int fillet_inner_px = 0;
    Texture hug_outer_left;
    Texture hug_outer_right;
    Texture hug_inner_left;
    Texture hug_inner_right;
    int hug_px = 0;
    int hug_inner_px = 0;
};

std::vector<uint8_t> fillet_rgba(int size, bool circle_on_right);

void bar_frame_base(Node *content, const astralia::BarStyleSpec &style, const astralia::BarFrame &frame, float width, float height);
void bar_frame_overlay(Node *content, BarDecor &decor, const astralia::BarStyleSpec &style, const astralia::BarFrame &frame, float width, float height, int32_t scale, int32_t hug_radius, bool left_flush, bool right_flush);
