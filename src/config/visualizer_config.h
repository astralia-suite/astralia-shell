#pragma once

namespace astralia::visualizer_config {

// canvas sizing
constexpr float canvas_fraction = 0.7f;
constexpr int canvas_min = 200;

// bar visualizer
constexpr float bar_width = 10.0f;
constexpr float bar_spacing = 7.0f;
constexpr float bar_radius = 3.0f;
constexpr float bar_height_ratio = 0.7f;
constexpr float bar_opacity = 0.6f;
constexpr float bar_min_height = 5.0f;
constexpr int bar_fps = 60;

// default window size
constexpr int default_window = 1000;

// audio capture
constexpr unsigned int sample_rate = 11000;
constexpr int channels = 2;
constexpr int sample_size = 1024;
constexpr int fragment_size = 4096;
constexpr float fft_scale = 10.2f;
constexpr float fft_cut_off = 0.3f;

// gravity & smoothing
constexpr int gravity_average_frames = 5;
constexpr float gravity_step = 4.2f;
constexpr float sample_hybrid_weight = 0.065f;
constexpr int sample_mode = 0;
constexpr int adjacent_sample_nums = 1;
constexpr float sample_range = 0.9f;
constexpr float sample_scale = 8.0f;
constexpr float smooth_factor = 0.025f;
constexpr int fps = 60;

// clamp ranges
constexpr int fps_min = 15;
constexpr int fps_max = 144;
constexpr float particle_thin_min = 0.0f;
constexpr float particle_thin_max = 0.95f;
constexpr int particle_size = 8;
constexpr int complexity_min = 1;
constexpr int complexity_max = 3;
constexpr float glow_directions_min = 4.0f;
constexpr float glow_directions_max = 32.0f;
constexpr float glow_quality_min = 2.0f;
constexpr float glow_quality_max = 8.0f;

} // namespace astralia::visualizer_config

namespace astralia {

enum class VisualizerShape { Bar, Sphere };

struct VisualizerParams {
    VisualizerShape visualizer_shape = VisualizerShape::Bar;
    int fps = visualizer_config::fps;
    float particle_thin = 0.12f;
    int particle_size = visualizer_config::particle_size;
    int fractal_complexity = 3;
    float glow_directions = 16.0f;
    float glow_quality = 6.0f;

    bool operator==(const VisualizerParams &) const = default;
};

} // namespace astralia
