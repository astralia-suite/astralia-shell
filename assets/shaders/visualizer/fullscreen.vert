#version 100
attribute vec3 aPos;
void main() { gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); }
