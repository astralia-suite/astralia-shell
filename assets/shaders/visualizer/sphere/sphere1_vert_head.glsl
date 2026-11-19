#version 100
precision highp float;
precision highp int;
precision highp sampler2D;
// ES 2.0 replacement for the ES 3.2 atomic-image particle-accumulation pass
// (see sphere1_main.glsl on the ES 3.2 branch, and sphere2_main.glsl's
// matching imageAtomicExchange read side). There is no image load/store or
// atomics on ES 2.0, so the per-fragment scatter-add is done the ES
// 2.0-native way instead: each particle is a real GL_POINTS vertex (one per
// surviving grid pixel, built host-side in SphereVisualizer::ensure_particle_grid
// with the same particleThin dropout hash this file's fragment-shader
// ancestor used to run per-pixel), and additive blending
// (glBlendFunc(GL_ONE, GL_ONE) in sphere_visualizer.cpp) accumulates
// overlapping splats order-independently, the same way imageAtomicAdd did.
// Reference port: ~/workspace/ncs's opengles2 branch (ncs-1.vert/ncs-1.frag).
//
// NOTE: this reads audioL/audioR via vertex texture fetch (VTF). VTF is
// optional on ES 2.0 (GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS may be 0 on some
// hardware); this path assumes it is available.
attribute vec2 aPos;
uniform vec2 resolution;
uniform sampler2D audioL;
uniform sampler2D audioR;
uniform float time;
uniform int audioLSize;
uniform int audioRSize;
varying float vSplatSize;
varying float vSplatFeather;
varying float vSplatOpacity;
#define AUDIO1D(t, x) texture2D(t, vec2((x), 0.5))
