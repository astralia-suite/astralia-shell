#include "modules/visualizer/wayland/visualizer_shaders.h"

#include "wayland/render/gl.h"

// sphere / glow stages carry lygia's pnoise (MIT); the audio-pass shaders under
// assets/shaders/visualizer/ are adapted from GLava by jarcode-foss (GPL-3.0).
// See assets/shaders/NOTICE.

namespace visualizer_shaders {

namespace {

std::string frag(const char *name) {
    return gl_load_shader((std::string("visualizer/sphere/") + name).c_str());
}

} // namespace

std::string sphere1_vs() {
    return frag("sphere1_vert_head.glsl") + frag("lygia_pnoise.glsl") + frag("common.glsl") + frag("sphere1_vert_main.glsl");
}

std::string sphere2_fs() {
    return frag("sphere_head.glsl") + frag("common.glsl") + frag("defaults.glsl") + frag("sphere2_main.glsl");
}

std::string glow_fs() {
    return frag("glow_head.glsl") + frag("common.glsl") + frag("glow_main.glsl");
}

} // namespace visualizer_shaders
