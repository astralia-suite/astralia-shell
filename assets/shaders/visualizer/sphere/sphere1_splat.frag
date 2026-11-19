#version 100
precision highp float;
// Point-sprite fragment shader for the particle-accumulation pass (paired
// with sphere1_vert_head.glsl+sphere1_vert_main.glsl). Evaluates the same
// distance-based splat falloff the old -size..size double loop
// (sphere1_main.glsl's processZLayer) did, but against gl_PointCoord instead
// of a manual pixel loop. sphere_visualizer.cpp's draw call additive-blends
// (glBlendFunc(GL_ONE, GL_ONE)) this into a plain texture, which is what
// actually replaces imageAtomicAdd's per-pixel sum.
varying float vSplatSize;
varying float vSplatFeather;
varying float vSplatOpacity;
void main() {
    vec2 offset = (gl_PointCoord - vec2(0.5)) * 2.0;
    float d = length(offset) * vSplatSize;
    float coverage = mix(step(d, vSplatSize),
        1.0 - smoothstep(vSplatSize - vSplatFeather * vSplatSize, vSplatSize, d),
        vSplatFeather);
    // The ES 3.2 version scaled this by 100000 for its raw uint accumulator;
    // here it accumulates directly in the 0..1 blend target, so no such scale.
    gl_FragColor = vec4(coverage * vSplatOpacity, 0.0, 0.0, 1.0);
}
