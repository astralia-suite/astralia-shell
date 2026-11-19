#version 100
attribute vec3 aPos;
varying vec2 vUv;
void main() {
    vUv = aPos.xy * 0.5 + 0.5;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
