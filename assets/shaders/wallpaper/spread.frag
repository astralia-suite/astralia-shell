#version 100
// Ported from pocket-dots (wallfx.frag, contagion), GPL-3.0, by Diego Malagrida.
precision highp float;
varying vec2 v_uv;
uniform sampler2D u_from;
uniform sampler2D u_to;
uniform sampler2D u_map;
uniform vec4 u_from_uv;
uniform vec4 u_to_uv;
uniform vec4 u_fill;
uniform float u_progress;
uniform vec2 u_map_size;
uniform vec4 u_ember;
uniform float u_seed;

const float PI = 3.14159265;
const float LIFE = 0.05;
const float GLOW = 0.9;

vec4 s_from(vec2 uv) {
    vec2 p = uv * u_from_uv.xy + u_from_uv.zw;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
        return u_fill;
    return texture2D(u_from, p);
}

vec4 s_to(vec2 uv) {
    vec2 p = uv * u_to_uv.xy + u_to_uv.zw;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
        return u_fill;
    return texture2D(u_to, p);
}

float settle(float x) {
    x = clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7)) + u_seed * 13.17) * 43758.5453);
}

float word(vec2 bytes) {
    return (floor(bytes.x * 255.0 + 0.5) * 256.0 + floor(bytes.y * 255.0 + 0.5)) / 65535.0;
}

vec4 map_at(vec2 cell) {
    cell = clamp(cell, vec2(0.0), u_map_size - 1.0);
    return texture2D(u_map, (cell + 0.5) / u_map_size);
}

float arrival_at(vec2 cell) { return word(map_at(cell).rg); }

float glow_at(vec2 cell) { return word(map_at(cell).ba); }

float surface(vec2 uv) {
    vec2 p = uv * u_map_size - 0.5;
    vec2 i = floor(p);
    vec2 f = p - i;
    return mix(mix(glow_at(i), glow_at(i + vec2(1.0, 0.0)), f.x),
               mix(glow_at(i + vec2(0.0, 1.0)), glow_at(i + vec2(1.0, 1.0)), f.x), f.y);
}

vec2 cam(vec2 uv, float z) { return 0.5 + (uv - 0.5) / z; }

void main() {
    vec2 uv = v_uv;
    float t = u_progress;
    float energy = sin(PI * t);
    float front = settle(t) * (1.0 + LIFE + 0.01) - 0.01;
    float s = surface(uv);
    vec2 cell = floor(uv * u_map_size);
    float a = arrival_at(cell);
    float flick = hash(cell);
    vec3 hot = mix(u_ember.rgb, vec3(1.0), 0.18);
    vec3 c;
    if (a > front) {
        c = s_from(uv).rgb;
        float ahead = a - front;
        if (flick > 0.985 && ahead < 0.02)
            c = mix(c, hot, (1.0 - ahead / 0.02) * 0.8 * GLOW * step(0.0, front));
    } else {
        c = s_to(cam(uv, 1.0 + 0.025 * (1.0 - settle(t)))).rgb;
        bool edge = max(max(arrival_at(cell + vec2(1.0, 0.0)), arrival_at(cell - vec2(1.0, 0.0))),
                        max(arrival_at(cell + vec2(0.0, 1.0)),
                            arrival_at(cell - vec2(0.0, 1.0)))) > front;
        float life = clamp((front - a) / LIFE, 0.0, 1.0);
        float burn = (1.0 - life) * (1.0 - life) * (edge ? 1.0 : 0.55);
        c = mix(c, hot, burn * (0.55 + 0.45 * flick) * 0.85 * GLOW);
    }
    c += u_ember.rgb * 0.22 * GLOW * energy * exp(-abs(s - front) / 0.006);
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
