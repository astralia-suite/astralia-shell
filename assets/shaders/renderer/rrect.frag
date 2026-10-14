#version 100
#extension GL_OES_standard_derivatives : require
precision highp float;
varying vec2 v_uv;
uniform vec2 u_size;
uniform float u_pad;
uniform float u_radius;
uniform float u_border_width;
uniform vec4 u_fill_color;
uniform vec4 u_border_color;

// One device pixel wide linear ramp: the exact box-filter coverage of a straight edge, whatever the scale or transform.
float coverage(float d) {
    float px = max(length(vec2(dFdx(d), dFdy(d))), 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0);
}

void main() {
    vec2 half_size = u_size * 0.5;
    vec2 p = (v_uv - 0.5) * (u_size + 2.0 * u_pad);
    vec2 q = abs(p) - (half_size - u_radius);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;
    float alpha = coverage(d);
    vec4 color = u_fill_color;
    if (u_border_width > 0.0) {
        color = mix(u_border_color, u_fill_color, coverage(d + u_border_width));
    }
    gl_FragColor = vec4(color.rgb, color.a * alpha);
}
