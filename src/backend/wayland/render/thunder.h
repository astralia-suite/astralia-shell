#pragma once

#include <GLES3/gl32.h>

#include "wayland/render/renderer.h"

struct ThunderBurst {
    GLuint bolt_program = 0;
    GLuint shock_program = 0;
    GLuint rect_program = 0;
    bool bolt_tried = false;
    bool shock_tried = false;
    bool rect_tried = false;
    bool bolt_logged = false;
    bool shock_logged = false;
};

struct ThunderParams {
    float ax = 0.0f;
    float ay = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
    float time_s = 0.0f;
    float progress = 1.0f;
    float intensity = 1.0f;
    float seed = 0.0f;
    float amp = 14.0f;
    float thick = 1.0f;
    float pad = 42.0f;
    bool stitch = false;
    const float *core = nullptr;
    const float *glow = nullptr;
};

struct ThunderShockParams {
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float time_s = 0.0f;
    float progress = 0.0f;
    float intensity = 1.0f;
    const float *core = nullptr;
    const float *glow = nullptr;
};

struct ThunderRectShockParams {
    float cx = 0.0f;
    float cy = 0.0f;
    float half_w = 0.0f;
    float half_h = 0.0f;
    float corner = 0.0f;
    float reach = 0.0f;
    float time_s = 0.0f;
    float progress = 0.0f;
    float intensity = 1.0f;
    const float *core = nullptr;
    const float *glow = nullptr;
};

void thunder_burst_draw(ThunderBurst &tb, Renderer &renderer, const ThunderParams &p);

void thunder_shock_draw(ThunderBurst &tb, Renderer &renderer, const ThunderShockParams &p);

void thunder_rect_shock_draw(ThunderBurst &tb, Renderer &renderer, const ThunderRectShockParams &p);
