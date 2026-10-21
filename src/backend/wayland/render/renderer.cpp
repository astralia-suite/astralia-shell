#include <GLES2/gl2.h>

#include <GLES2/gl2ext.h>
#include <algorithm>
#include <cmath>
#include <string>

#include "wayland/render/gl.h"
#include "wayland/render/renderer.h"
#include "wayland/render/texture.h"

namespace {

constexpr float kEdgePad = 2.0f;

} // namespace

bool Renderer::init() {
    texture_detect_caps();
    video_texture_detect_caps(eglGetCurrentDisplay());
    std::string quad_vs = gl_load_shader("renderer/quad.vert");
    auto build = [&](const char *fs_rel, const char *label) {
        std::string fs = gl_load_shader(fs_rel);
        if (quad_vs.empty() || fs.empty())
            return GLuint{0};
        return gl_compile_program(quad_vs.c_str(), fs.c_str(), label);
    };
    rect_program_ = build("renderer/rect.frag", "rect");
    tex_program_ = build("renderer/tex.frag", "tex");
    rrect_program_ = build("renderer/rrect.frag", "rrect");
    rounded_tex_program_ = build("renderer/rounded_tex.frag", "rounded_tex");
    fillet_program_ = build("renderer/fillet.frag", "fillet");
    video_program_ = build("renderer/video.frag", "video");
    if (!rect_program_ || !tex_program_ || !rrect_program_ || !rounded_tex_program_ ||
        !fillet_program_ || !video_program_)
        return false;

    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &quad_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return true;
}

void Renderer::destroy() {
    if (rect_program_)
        glDeleteProgram(rect_program_);
    if (tex_program_)
        glDeleteProgram(tex_program_);
    if (rrect_program_)
        glDeleteProgram(rrect_program_);
    if (rounded_tex_program_)
        glDeleteProgram(rounded_tex_program_);
    if (fillet_program_)
        glDeleteProgram(fillet_program_);
    if (video_program_)
        glDeleteProgram(video_program_);
    if (quad_vbo_)
        glDeleteBuffers(1, &quad_vbo_);
    if (polygon_program_)
        glDeleteProgram(polygon_program_);
    if (polygon_tex_program_)
        glDeleteProgram(polygon_tex_program_);
    if (polygon_vbo_)
        glDeleteBuffers(1, &polygon_vbo_);
    *this = Renderer{};
}

void Renderer::begin_frame(int logical_width, int logical_height, int32_t scale) {
    viewport_[0] = static_cast<float>(logical_width);
    viewport_[1] = static_cast<float>(logical_height);
    scale_ = scale > 0 ? scale : 1;
    glViewport(0, 0, logical_width * scale_, logical_height * scale_);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    opacity_ = 1.0f;
    model_stack_.assign(1, Affine2D{});
    gl_check("begin_frame");
}

void Renderer::set_erase_blend(bool on) {
    if (on)
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    else
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

Affine2D Affine2D::rotation_deg(float deg) {
    float rad = deg * 3.14159265358979323846f / 180.0f;
    float cs = std::cos(rad), sn = std::sin(rad);
    return {cs, -sn, sn, cs, 0.0f, 0.0f};
}

void Renderer::push_model(const Affine2D &local) {
    model_stack_.push_back(model_stack_.back().compose(local));
}

void Renderer::pop_model() {
    if (model_stack_.size() > 1)
        model_stack_.pop_back();
}

void Renderer::set_clip(float x, float y, float w, float h) {
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!clip_stack_.empty()) {
        const ClipRect &p = clip_stack_.back();
        x0 = std::max(x0, p.x0);
        y0 = std::max(y0, p.y0);
        x1 = std::min(x1, p.x1);
        y1 = std::min(y1, p.y1);
    }
    clip_stack_.push_back({x0, y0, x1, y1});
    apply_clip(clip_stack_.back());
}

void Renderer::clear_clip() {
    clip_stack_.pop_back();
    if (clip_stack_.empty())
        glDisable(GL_SCISSOR_TEST);
    else
        apply_clip(clip_stack_.back());
}

void Renderer::draw_rect(float x, float y, float w, float h, const float color[4]) {
    glUseProgram(rect_program_);
    set_common_uniforms(rect_program_, x, y, w, h);
    float c[4] = {color[0], color[1], color[2], color[3] * opacity_};
    glUniform4fv(glGetUniformLocation(rect_program_, "u_color"), 1, c);
    draw_quad(rect_program_);
}

void Renderer::draw_rounded_rect(float x, float y, float w, float h, float radius,
                                 float border_width, const float fill[4], const float border[4]) {
    glUseProgram(rrect_program_);
    set_common_uniforms(rrect_program_, x - kEdgePad, y - kEdgePad, w + 2.0f * kEdgePad,
                        h + 2.0f * kEdgePad);
    float size[2] = {w, h};
    glUniform2fv(glGetUniformLocation(rrect_program_, "u_size"), 1, size);
    glUniform1f(glGetUniformLocation(rrect_program_, "u_pad"), kEdgePad);
    glUniform1f(glGetUniformLocation(rrect_program_, "u_radius"), radius);
    glUniform1f(glGetUniformLocation(rrect_program_, "u_border_width"), border_width);
    float fc[4] = {fill[0], fill[1], fill[2], fill[3] * opacity_};
    float bc[4] = {border[0], border[1], border[2], border[3] * opacity_};
    glUniform4fv(glGetUniformLocation(rrect_program_, "u_fill_color"), 1, fc);
    glUniform4fv(glGetUniformLocation(rrect_program_, "u_border_color"), 1, bc);
    draw_quad(rrect_program_);
}

void Renderer::draw_fillet(float x, float y, float size, bool circle_on_right,
                           const float color[4]) {
    glUseProgram(fillet_program_);
    set_common_uniforms(fillet_program_, x - kEdgePad, y - kEdgePad, size + 2.0f * kEdgePad,
                        size + 2.0f * kEdgePad);
    glUniform1f(glGetUniformLocation(fillet_program_, "u_size"), size);
    glUniform1f(glGetUniformLocation(fillet_program_, "u_pad"), kEdgePad);
    glUniform1f(glGetUniformLocation(fillet_program_, "u_circle_right"),
                circle_on_right ? 1.0f : 0.0f);
    float c[4] = {color[0], color[1], color[2], color[3] * opacity_};
    glUniform4fv(glGetUniformLocation(fillet_program_, "u_color"), 1, c);
    draw_quad(fillet_program_);
}

void Renderer::draw_texture(float x, float y, const Texture &tex, const float tint[4]) {
    float inv_scale = 1.0f / static_cast<float>(tex.scale > 0 ? tex.scale : 1);
    draw_texture_rect(x, y, static_cast<float>(tex.width) * inv_scale,
                      static_cast<float>(tex.height) * inv_scale, tex, tint);
}

void Renderer::draw_texture_rect(float x, float y, float w, float h, const Texture &tex,
                                 const float tint[4]) {
    x = std::round(x);
    y = std::round(y);
    glUseProgram(tex_program_);
    set_common_uniforms(tex_program_, x, y, w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glUniform1i(glGetUniformLocation(tex_program_, "u_tex"), 0);
    float tc[4] = {tint[0], tint[1], tint[2], tint[3] * opacity_};
    glUniform4fv(glGetUniformLocation(tex_program_, "u_color"), 1, tc);
    draw_quad(tex_program_);
}

void Renderer::draw_texture_rect_rounded(float x, float y, float w, float h, float radius,
                                         const Texture &tex, const float tint[4]) {
    x = std::round(x);
    y = std::round(y);
    glUseProgram(rounded_tex_program_);
    set_common_uniforms(rounded_tex_program_, x - kEdgePad, y - kEdgePad, w + 2.0f * kEdgePad,
                        h + 2.0f * kEdgePad);
    glUniform1f(glGetUniformLocation(rounded_tex_program_, "u_pad"), kEdgePad);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glUniform1i(glGetUniformLocation(rounded_tex_program_, "u_tex"), 0);
    float tc[4] = {tint[0], tint[1], tint[2], tint[3] * opacity_};
    glUniform4fv(glGetUniformLocation(rounded_tex_program_, "u_color"), 1, tc);
    float size[2] = {w, h};
    glUniform2fv(glGetUniformLocation(rounded_tex_program_, "u_size"), 1, size);
    glUniform1f(glGetUniformLocation(rounded_tex_program_, "u_radius"), radius);
    draw_quad(rounded_tex_program_);
}

void Renderer::draw_video_texture_rect(float x, float y, float w, float h,
                                       const VideoTexture &tex) {
    x = std::round(x);
    y = std::round(y);
    glUseProgram(video_program_);
    set_common_uniforms(video_program_, x, y, w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex.tex);
    glUniform1i(glGetUniformLocation(video_program_, "u_tex"), 0);
    glUniform1f(glGetUniformLocation(video_program_, "u_opacity"), opacity_);
    draw_quad(video_program_);
    gl_check("draw_video");
}

void Renderer::draw_custom(GLuint program, float x, float y, float w, float h,
                           const std::function<void(GLuint)> &set_uniforms) {
    if (!program)
        return;
    glUseProgram(program);
    set_common_uniforms(program, x, y, w, h);
    if (set_uniforms)
        set_uniforms(program);
    draw_quad(program);
    gl_check("draw_custom");
}

void Renderer::draw_polygon(const float *xy, int count, const float color[4]) {
    if (count < 3)
        return;
    if (!polygon_tried_) {
        polygon_tried_ = true;
        polygon_program_ =
            gl_compile_program_files("renderer/polygon.vert", "renderer/rect.frag", "polygon");
        glGenBuffers(1, &polygon_vbo_);
    }
    if (!polygon_program_)
        return;
    glUseProgram(polygon_program_);
    set_common_uniforms(polygon_program_, 0.0f, 0.0f, 0.0f, 0.0f);
    float c[4] = {color[0], color[1], color[2], color[3] * opacity_};
    glUniform4fv(glGetUniformLocation(polygon_program_, "u_color"), 1, c);
    glBindBuffer(GL_ARRAY_BUFFER, polygon_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count) * 2 * sizeof(float), xy,
                 GL_STREAM_DRAW);
    GLint loc = glGetAttribLocation(polygon_program_, "a_pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_FAN, 0, count);
    glDisableVertexAttribArray(loc);
    gl_check("draw_polygon");
}

void Renderer::draw_textured_polygon(const float *xy_uv, int count, const Texture &tex,
                                     const float tint[4]) {
    if (count < 3 || !tex.id)
        return;
    if (!polygon_tex_tried_) {
        polygon_tex_tried_ = true;
        polygon_tex_program_ = gl_compile_program_files("renderer/polygon_tex.vert",
                                                        "renderer/tex.frag", "polygon_tex");
        if (!polygon_vbo_)
            glGenBuffers(1, &polygon_vbo_);
    }
    if (!polygon_tex_program_)
        return;
    glUseProgram(polygon_tex_program_);
    set_common_uniforms(polygon_tex_program_, 0.0f, 0.0f, 0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glUniform1i(glGetUniformLocation(polygon_tex_program_, "u_tex"), 0);
    float tc[4] = {tint[0], tint[1], tint[2], tint[3] * opacity_};
    glUniform4fv(glGetUniformLocation(polygon_tex_program_, "u_color"), 1, tc);
    glBindBuffer(GL_ARRAY_BUFFER, polygon_vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count) * 4 * sizeof(float), xy_uv,
                 GL_STREAM_DRAW);
    GLint pos = glGetAttribLocation(polygon_tex_program_, "a_pos");
    GLint uv = glGetAttribLocation(polygon_tex_program_, "a_uv");
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(uv);
    glVertexAttribPointer(uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void *>(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_FAN, 0, count);
    glDisableVertexAttribArray(pos);
    glDisableVertexAttribArray(uv);
    gl_check("draw_textured_polygon");
}

void Renderer::grab_framebuffer(Texture &tex) {
    int width = static_cast<int>(viewport_[0]) * scale_;
    int height = static_cast<int>(viewport_[1]) * scale_;
    if (width <= 0 || height <= 0)
        return;
    if (!tex.id) {
        glGenTextures(1, &tex.id);
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, width, height, 0);
    tex.width = width;
    tex.height = height;
    tex.scale = scale_;
    gl_check("grab_framebuffer");
}

void Renderer::apply_clip(const ClipRect &r) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(r.x0 * scale_), static_cast<GLint>((viewport_[1] - r.y1) * scale_),
              static_cast<GLsizei>(std::max(0.0f, r.x1 - r.x0) * scale_),
              static_cast<GLsizei>(std::max(0.0f, r.y1 - r.y0) * scale_));
}

void Renderer::set_common_uniforms(GLuint program, float x, float y, float w, float h) {
    glUniform2fv(glGetUniformLocation(program, "u_viewport"), 1, viewport_);
    float rect[4] = {x, y, w, h};
    glUniform4fv(glGetUniformLocation(program, "u_rect"), 1, rect);
    const Affine2D &m = model_stack_.back();
    float ab[4] = {m.a, m.b, m.c, m.d};
    float t[2] = {m.tx, m.ty};
    glUniform4fv(glGetUniformLocation(program, "u_model_ab"), 1, ab);
    glUniform2fv(glGetUniformLocation(program, "u_model_t"), 1, t);
}

void Renderer::draw_quad(GLuint program) {
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    GLint loc = glGetAttribLocation(program, "a_pos");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(loc);
}
