#version 100
precision highp float;
varying vec2 v_uv;
uniform vec2 u_size;
uniform vec2 u_center;
uniform vec2 u_half;
uniform float u_corner;
uniform float u_time;
uniform float u_progress;
uniform float u_reach;
uniform float u_intensity;
uniform vec4 u_core;
uniform vec4 u_glow;

float hash1(float n) { return fract(sin(n) * 43758.5453123); }

float vnoise(float x) {
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash1(i), hash1(i + 1.0), u);
}

void main() {
    vec2 p = v_uv * u_size - u_center + vec2(0.001, 0.001);
    float corner = max(u_corner, 0.0);
    float front = u_progress * u_reach;
    vec2 q0 = abs(p) - u_half + vec2(corner);
    float sd_card = length(max(q0, vec2(0.0))) + min(max(q0.x, q0.y), 0.0) - corner;
    vec2 q = abs(p) - (u_half + vec2(front)) + vec2(corner);
    float sd = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - corner;
    float ang = atan(p.y, p.x);
    float tw = mod(u_time, 128.0);

    float wob = (vnoise(ang * 5.0 + tw * 7.0) - 0.5) * 14.0;
    wob += (vnoise(ang * 17.0 - tw * 5.0) - 0.5) * 6.0;

    float ring = abs(sd - wob);
    float width = max(6.0 + 26.0 * u_progress, 0.001);
    float core = exp(-ring * ring / (width * width));
    float glow = exp(-ring / (width * 1.7));
    float front_safe = max(front, 0.001);
    float inside =
        (1.0 - smoothstep(front_safe * 0.35, front_safe, max(sd_card, 0.0))) * 0.10;

    float fade = 1.0 - u_progress;
    float f = (core + glow * 0.5 + inside) * u_intensity * fade;
    f *= float(f == f);
    f = min(f, 8.0);
    float a = clamp(f, 0.0, 1.0);
    vec3 col = mix(u_glow.rgb, u_core.rgb, clamp(f * f, 0.0, 1.0));
    gl_FragColor = vec4(col, a);
}
