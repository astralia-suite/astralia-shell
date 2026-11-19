#version 100
#extension GL_OES_EGL_image_external : require
precision mediump float;
varying vec2 v_uv;
uniform samplerExternalOES u_tex;
uniform float u_opacity;
void main() {
    gl_FragColor = texture2D(u_tex, v_uv);
    gl_FragColor.a *= u_opacity;
}
