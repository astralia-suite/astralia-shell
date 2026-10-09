#version 100
precision highp float;
precision highp sampler2D;
varying vec2 vUv;
uniform sampler2D tex;
void main() { gl_FragColor = texture2D(tex, vUv); }
