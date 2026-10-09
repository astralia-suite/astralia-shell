#pragma once

#include <GLES2/gl2.h>

#include "wayland/config/visualizer_config.h"

class SphereVisualizer {
  public:
    bool init();
    void destroy();

    void render(int width, int height, int tick, GLuint audio_l_tex, GLuint audio_r_tex, int audio_size, const VisualizerParams &params);

  private:
    void ensure_targets(int canvas);
    void ensure_particle_grid(int canvas, float particle_thin);
    void draw_quad();
    void present(int width, int height, int canvas);
    void set_audio_uniforms(GLuint prog, GLuint audio_l_tex, GLuint audio_r_tex, int audio_size, int tick, int canvas, const VisualizerParams &params);

    GLuint sphere1_prog_ = 0;
    GLuint sphere2_prog_ = 0;
    GLuint glow_prog_ = 0;
    GLuint present_prog_ = 0;

    GLuint fbo_[2] = {0, 0};
    GLuint fbo_tex_[2] = {0, 0};
    GLuint glow_fbo_ = 0;
    GLuint glow_tex_ = 0;

    GLuint vbo_ = 0;

    GLuint particle_vbo_ = 0;
    int particle_count_ = 0;
    float particle_grid_thin_ = -1.0f;
    int particle_grid_canvas_ = -1;

    int canvas_ = 0;
    bool ready_ = false;
};
