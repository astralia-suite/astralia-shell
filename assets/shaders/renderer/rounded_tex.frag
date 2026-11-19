#version 100
#extension GL_OES_standard_derivatives : require
precision highp float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform vec4 u_color;
uniform vec2 u_size;
uniform float u_pad;
uniform float u_radius;

// One device pixel wide linear ramp: the exact box-filter coverage of a straight edge, whatever the scale or transform.
float coverage(float d) {
    float px = max(length(vec2(dFdx(d), dFdy(d))), 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0);
}

void main() {
    vec2 half_size = u_size * 0.5;
    vec2 full = u_size + 2.0 * u_pad;
    vec2 p = (v_uv - 0.5) * full;
    float d = length(max(abs(p) - (half_size - u_radius), 0.0)) - u_radius;
    vec4 texel = texture2D(u_tex, (p + half_size) / u_size);
    vec4 color = texel * u_color;
    gl_FragColor = vec4(color.rgb, color.a * coverage(d));
}
