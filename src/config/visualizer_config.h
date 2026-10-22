#pragma once

namespace astralia::visualizer_config {

// canvas sizing
constexpr float kCanvasFraction = 0.7f;
constexpr int kCanvasMin = 200;

// bar visualizer
constexpr float kBarWidth = 10.0f;
constexpr float kBarSpacing = 7.0f;
constexpr float kBarRadius = 3.0f;
constexpr float kBarHeightRatio = 0.7f;
constexpr float kBarOpacity = 0.6f;
constexpr float kBarMinHeight = 5.0f;
constexpr int kBarFps = 60;

// default window size
constexpr int kDefaultWindow = 1000;

// audio capture
constexpr unsigned int kSampleRate = 11000;
constexpr int kChannels = 2;
constexpr int kSampleSize = 1024;
constexpr int kFragmentSize = 4096;
constexpr float kFftScale = 10.2f;
constexpr float kFftCutOff = 0.3f;

// gravity & smoothing
constexpr int kGravityAverageFrames = 5;
constexpr float kGravityStep = 4.2f;
constexpr float kSampleHybridWeight = 0.065f;
constexpr int kSampleMode = 0;
constexpr int kAdjacentSampleNums = 1;
constexpr float kSampleRange = 0.9f;
constexpr float kSampleScale = 8.0f;
constexpr float kSmoothFactor = 0.025f;
constexpr int kFps = 60;

// clamp ranges
constexpr int kFpsMin = 15;
constexpr int kFpsMax = 144;
constexpr float kParticleThinMin = 0.0f;
constexpr float kParticleThinMax = 0.95f;
constexpr int kParticleSize = 8;
constexpr int kComplexityMin = 1;
constexpr int kComplexityMax = 3;
constexpr float kGlowDirectionsMin = 4.0f;
constexpr float kGlowDirectionsMax = 32.0f;
constexpr float kGlowQualityMin = 2.0f;
constexpr float kGlowQualityMax = 8.0f;

} // namespace astralia::visualizer_config

namespace astralia {

enum class VisualizerShape { Bar, Sphere };

struct VisualizerParams {
    VisualizerShape visualizer_shape = VisualizerShape::Bar;
    int fps = visualizer_config::kFps;
    float particle_thin = 0.12f;
    int particle_size = visualizer_config::kParticleSize;
    int fractal_complexity = 3;
    float glow_directions = 16.0f;
    float glow_quality = 6.0f;

    bool operator==(const VisualizerParams &) const = default;
};

} // namespace astralia
