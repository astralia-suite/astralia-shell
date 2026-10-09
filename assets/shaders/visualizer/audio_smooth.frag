#version 100
precision highp float;
precision highp sampler2D;
#ifndef TWOPI
#define TWOPI 6.28318530718
#endif
#ifndef PI
#define PI 3.14159265359
#endif
#define sinusoidal(x) ((0.5 * sin((PI * (x)) - (PI / 2.0))) + 0.5)
#define ROUND_FORMULA sinusoidal
uniform int sample_mode;
uniform float sample_hybrid_weight;
uniform float sample_scale;
uniform float sample_range;
uniform float smooth_factor;
float scale_audio(float idx)
{
    return -log((-(sample_range)*idx) + 1.0) / (sample_scale);
}
vec4 fetch_sample(in sampler2D tex, int tex_sz, float s) {
    float coord = floor(s + 0.5);
    return texture2D(tex, vec2((coord + 0.5) / float(tex_sz), 0.5));
}
// ponytail: ES 1.00 forbids a non-constant loop bound (smax-smin is uniform-
// derived); cap at a fixed trip count and break early instead. 512 is >>
// the ~60-sample window the current sample_range/sample_scale/smooth_factor
// config actually produces at kVisualizerSampleSize=1024 — raise if those
// config constants change enough to widen the window past that.
const int kMaxSmoothIter = 512;
float smooth_audio(in sampler2D tex, int tex_sz, highp float idx)
{
    float smin = scale_audio(clamp(idx - smooth_factor, 0.0, 1.0)) * float(tex_sz),
          smax = scale_audio(clamp(idx + smooth_factor, 0.0, 1.0)) * float(tex_sz);
    float m = ((smax - smin) / 2.0), s, w;
    float rm = smin + m;
    if (sample_mode == 0) {
        float avg = 0.0, weight = 0.0;
        for (int n = 0; n < kMaxSmoothIter; n++) {
            s = smin + float(n);
            if (s > smax) break;
            w = ROUND_FORMULA(clamp((m - abs(rm - s)) / m, 0.0, 1.0));
            weight += w;
            avg += fetch_sample(tex, tex_sz, s).r * w;
        }
        avg /= weight;
        return avg;
    } else if (sample_mode == 2) {
        float vmax = 0.0, avg = 0.0, weight = 0.0, v;
        for (int n = 0; n < kMaxSmoothIter; n++) {
            s = smin + float(n);
            if (s >= smax) break;
            w = ROUND_FORMULA(clamp((m - abs(rm - s)) / m, 0.0, 1.0));
            weight += w;
            v = fetch_sample(tex, tex_sz, s).r * w;
            avg += v;
            if (vmax < v)
                vmax = v;
        }
        return (vmax * (1.0 - sample_hybrid_weight)) + ((avg / weight) * sample_hybrid_weight);
    } else if (sample_mode == 1) {
        float vmax = 0.0, v;
        for (int n = 0; n < kMaxSmoothIter; n++) {
            s = smin + float(n);
            if (s >= smax) break;
            w = fetch_sample(tex, tex_sz, s).r * ROUND_FORMULA(clamp((m - abs(rm - s)) / m, 0.0, 1.0));
            if (vmax < w)
                vmax = w;
        }
        return vmax;
    }
    return 0.0;
}
uniform sampler2D audioR;
uniform int audioRSize;
uniform int adjacentSampleNums;
#define adjacent(I) gl_FragColor.r += (1. - step(float(I), 1.)) * (smooth_audio(audioR, audioRSize, u + float(I - 1) * aRI) + smooth_audio(audioR, audioRSize, u - float(I - 1) * aRI));
void main()
{
    float u = gl_FragCoord.x / float(audioRSize);
    gl_FragColor.r = 0.0;
    float aRI = 1. / float(audioRSize);
    adjacent(0);
    gl_FragColor.r += (smooth_audio(audioR, audioRSize, u));
    gl_FragColor.r /= 2. * (float(adjacentSampleNums) - 1.) + 1.;
}
