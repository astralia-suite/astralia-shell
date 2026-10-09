#pragma once

#include <GLES2/gl2.h>

#include "wayland/config/visualizer_config.h"

class BarVisualizer {
  public:
    bool init();
    void destroy();

    void render(int width, int height, int tick, GLuint audio_l_tex, GLuint audio_r_tex, int audio_size, const VisualizerParams &params);

  private:
    void draw_quad();

    GLuint prog_ = 0;
    GLuint vbo_ = 0;
    bool ready_ = false;
};
