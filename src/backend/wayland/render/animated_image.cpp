#include <algorithm>

#include "wayland/render/animated_image.h"
#include "wayland/render/texture.h"

void animated_image_set_source(AnimatedImage &img, std::string source_path, const AnimatedImageStyle &style) {
    img.source = std::move(source_path);
    img.style = style;
}

void animated_image_show(AnimatedImage &img, std::function<void()> on_ready) {
    if (img.shown)
        return;
    img.shown = true;
    img.started = std::chrono::steady_clock::now();
    animate_job_start(img.job, img.source, img.style.decode, std::move(on_ready));
}

void animated_image_hide(AnimatedImage &img) {
    if (!img.shown)
        return;
    img.shown = false;
    img.frames.clear();
    img.cur_frame = 0;
}

void animated_image_tick(AnimatedImage &img, std::chrono::steady_clock::time_point now) {
    if (!img.shown || !img.job.ready || img.job.frame_count <= 0)
        return;
    if (static_cast<int>(img.frames.size()) < img.job.frame_count) {
        int i = static_cast<int>(img.frames.size());
        int w = 0, h = 0;
        unsigned char *data = animate_job_frame(img.job, i, w, h);
        Texture t;
        if (data) {
            t = make_texture_rgba(w, h, data, false);
            delete[] data;
        }
        img.frames.push_back(std::move(t));
    }
    float elapsed = std::chrono::duration<float>(now - img.started).count();
    img.cur_frame = animate_frame_index(elapsed, img.style.decode.fps, static_cast<int>(img.frames.size()));
}

void animated_image_draw(AnimatedImage &img, GlCanvas &canvas, float x, float y, float w, float h, float alpha) {
    const AnimatedImageStyle &s = img.style;
    float radius = s.circular ? w * 0.5f : 0.0f;
    if (s.ring_fill || s.border_color) {
        auto faded = [alpha](const float *c) { return c ? astralia::Color{c[0], c[1], c[2], c[3] * alpha} : astralia::Color{0, 0, 0, 0}; };
        canvas.rounded({x, y, w, h}, radius, faded(s.ring_fill), s.border_width, faded(s.border_color));
    }

    if (img.frames.empty())
        return;
    int fi =
        std::clamp(img.cur_frame, 0, static_cast<int>(img.frames.size()) - 1);
    const Texture &ft = img.frames[static_cast<size_t>(fi)];
    if (!ft.id)
        return;

    const float tint[4] = {1, 1, 1, alpha};
    float inset = s.border_width;
    float ix = x + inset, iy = y + inset;
    float iw = w - 2 * inset, ih = h - 2 * inset;
    if (s.decode.fit == AnimateFit::Fit && ft.width > 0 && ft.height > 0) {
        float scale = std::min(iw / static_cast<float>(ft.width), ih / static_cast<float>(ft.height));
        float fw = static_cast<float>(ft.width) * scale;
        float fh = static_cast<float>(ft.height) * scale;
        ix += (iw - fw) / 2.0f;
        iy += (ih - fh) / 2.0f;
        iw = fw;
        ih = fh;
    }
    canvas.texture(ft, {ix, iy, iw, ih}, tint, s.circular ? iw * 0.5f : 0.0f);
}

bool animated_image_animating(const AnimatedImage &img) {
    return img.shown && !img.frames.empty() && img.job.frame_count > 1 && img.style.decode.fps > 0;
}
