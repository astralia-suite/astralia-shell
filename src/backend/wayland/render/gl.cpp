#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>

#include "wayland/core/log.h"

#include "wayland/render/gl.h"

#ifndef ASTRALIA_SHELL_SHADER_DIR
#define ASTRALIA_SHELL_SHADER_DIR ""
#endif

namespace {

PFNGLGETGRAPHICSRESETSTATUSKHRPROC get_graphics_reset_status = nullptr;
thread_local GLenum last_reset_status = GL_NO_ERROR;

bool gl_has_extension(std::string_view name) {
    const char *list = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (!list)
        return false;
    std::string_view rest(list);
    while (!rest.empty()) {
        size_t end = rest.find(' ');
        if (rest.substr(0, end) == name)
            return true;
        if (end == std::string_view::npos)
            break;
        rest.remove_prefix(end + 1);
    }
    return false;
}

bool gl_version_at_least(int want_major, int want_minor) {
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    int major = 0;
    int minor = 0;
    if (!version || std::sscanf(version, "OpenGL ES %d.%d", &major, &minor) != 2)
        return false;
    return major > want_major || (major == want_major && minor >= want_minor);
}

const char *reset_status_name(GLenum status) {
    switch (status) {
    case GL_GUILTY_CONTEXT_RESET_KHR:
        return "guilty";
    case GL_INNOCENT_CONTEXT_RESET_KHR:
        return "innocent";
    case GL_UNKNOWN_CONTEXT_RESET_KHR:
        return "unknown";
    default:
        return "unrecognized";
    }
}

} // namespace

std::string gl_load_shader(const char *rel) {
    const std::string candidates[] = {
        std::string(ASTRALIA_SHELL_SHADER_DIR) + "/" + rel,
        std::string("assets/shaders/") + rel,
    };
    for (const std::string &path : candidates) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            continue;
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
    klog("shader: cannot read %s", rel);
    return {};
}

GLuint gl_compile_program_files(const char *vs_rel, const char *fs_rel, const char *label) {
    std::string vs = gl_load_shader(vs_rel);
    std::string fs = gl_load_shader(fs_rel);
    if (vs.empty() || fs.empty())
        return 0;
    return gl_compile_program(vs.c_str(), fs.c_str(), label);
}

GLuint gl_compile_program(const char *vs_src, const char *fs_src, const char *label) {
    auto compile = [label](GLenum type, const char *src) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char info[512];
            glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
            klog("shader compile failed (%s): %s", label ? label : "?", info);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs)
        return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_pos");
    glBindAttribLocation(program, 0, "aPos");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[512];
        glGetProgramInfoLog(program, sizeof(info), nullptr, info);
        klog("program link failed (%s): %s", label ? label : "?", info);
        glDeleteProgram(program);
        return 0;
    }
    klog("gl: linked program %u (%s)", program, label ? label : "?");
    return program;
}

void gl_check(const char *where) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
        klog("gl: %s -> 0x%04x", where ? where : "?", err);
}

bool gl_make_current(EGLDisplay display, EGLSurface surface, EGLContext context) {
    if (!eglMakeCurrent(display, surface, surface, context)) {
        klog("gl: eglMakeCurrent failed, egl error 0x%04x", eglGetError());
        gl_poll_graphics_reset("make_current");
        return false;
    }
    if (surface != EGL_NO_SURFACE && !eglSwapInterval(display, 0))
        klog("gl: eglSwapInterval(0) failed, egl error 0x%04x", eglGetError());
    return true;
}

void gl_release_if_current(EGLDisplay display, EGLSurface surface) {
    if (surface == EGL_NO_SURFACE || eglGetCurrentDisplay() != display)
        return;
    if (eglGetCurrentSurface(EGL_DRAW) != surface && eglGetCurrentSurface(EGL_READ) != surface)
        return;
    if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, eglGetCurrentContext()))
        return;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void gl_reset_detection_init() {
    const char *proc = nullptr;
    if (gl_version_at_least(3, 2))
        proc = "glGetGraphicsResetStatus";
    else if (gl_has_extension("GL_KHR_robustness"))
        proc = "glGetGraphicsResetStatusKHR";
    else if (gl_has_extension("GL_EXT_robustness"))
        proc = "glGetGraphicsResetStatusEXT";
    if (proc)
        get_graphics_reset_status = reinterpret_cast<PFNGLGETGRAPHICSRESETSTATUSKHRPROC>(eglGetProcAddress(proc));
    if (get_graphics_reset_status)
        klog("gl: reset detection via %s", proc);
    else
        klog("gl: reset detection unavailable");
}

bool gl_poll_graphics_reset(const char *where) {
    if (!get_graphics_reset_status || eglGetCurrentContext() == EGL_NO_CONTEXT)
        return false;
    GLenum status = get_graphics_reset_status();
    if (status == last_reset_status)
        return status != GL_NO_ERROR;
    last_reset_status = status;
    if (status == GL_NO_ERROR) {
        klog("gl: graphics reset completed (%s)", where ? where : "?");
        return false;
    }
    klog("gl: graphics reset detected (%s), status %s 0x%04x", where ? where : "?", reset_status_name(status), status);
    return true;
}
