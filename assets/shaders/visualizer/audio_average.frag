#version 100
precision highp float;
precision highp sampler2D;
#ifndef TWOPI
#define TWOPI 6.28318530718
#endif
#define window(t, sz) (0.53836 - (0.46164 * cos(TWOPI * t / sz)))
uniform int avgFrames;
uniform sampler2D audioR0;
uniform sampler2D audioR1;
uniform sampler2D audioR2;
uniform sampler2D audioR3;
uniform sampler2D audioR4;
uniform float u_texWidth;

vec4 fetch(sampler2D tex) {
    return texture2D(tex, vec2(gl_FragCoord.x / u_texWidth, 0.5));
}

void main() {
    float r = 0.0;
    r += window(float(0), float(avgFrames - 1)) * fetch(audioR0).r;
    r += window(float(1), float(avgFrames - 1)) * fetch(audioR1).r;
    r += window(float(2), float(avgFrames - 1)) * fetch(audioR2).r;
    r += window(float(3), float(avgFrames - 1)) * fetch(audioR3).r;
    r += window(float(4), float(avgFrames - 1)) * fetch(audioR4).r;
    gl_FragColor.r = (r / float(avgFrames));
}
