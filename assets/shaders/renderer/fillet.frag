#version 100
#extension GL_OES_standard_derivatives : require
precision highp float;
varying vec2 v_uv;
uniform float u_size;
uniform float u_pad;
uniform float u_circle_right;
uniform vec4 u_color;

// One device pixel wide linear ramp: the exact box-filter coverage of a straight edge, whatever the scale or transform.
float coverage(float d) {
    float px = max(length(vec2(dFdx(d), dFdy(d))), 1e-5);
    return clamp(0.5 - d / px, 0.0, 1.0);
}

// The square of side u_size minus the circle of the same radius centred on its bottom edge.
void main() {
    vec2 p = v_uv * (u_size + 2.0 * u_pad) - u_pad;
    vec2 centre = vec2(u_circle_right > 0.5 ? u_size : 0.0, u_size);
    float outside_circle = coverage(u_size - length(p - centre));
    vec2 q = abs(p - 0.5 * u_size) - 0.5 * u_size;
    float inside_box = coverage(length(max(q, 0.0)) + min(max(q.x, q.y), 0.0));
    gl_FragColor = vec4(u_color.rgb, u_color.a * outside_circle * inside_box);
}
