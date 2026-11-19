#version 100
precision highp float;
precision highp sampler2D;
uniform sampler2D audioR;
uniform float u_texWidth;
void main() {
    gl_FragColor.r = texture2D(audioR, vec2(gl_FragCoord.x / u_texWidth, 0.5)).r;
}
