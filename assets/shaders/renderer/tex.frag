#version 100
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform vec4 u_color;
void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }
