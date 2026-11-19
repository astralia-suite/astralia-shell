#version 100
precision highp float;
precision highp int;
precision highp sampler2D;
uniform vec2 resolution;
uniform sampler2D audioL;
uniform sampler2D audioR;
uniform float time;
uniform int audioLSize;
uniform int audioRSize;
#define FragColor gl_FragColor
#define texture texture2D
#define AUDIO1D(t, x) texture(t, vec2((x), 0.5))
