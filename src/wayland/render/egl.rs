//! EGL display, config and the one shared OpenGL ES 2.0 context.

use std::ffi::c_void;

use khronos_egl as egl;

const CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT: egl::Int = 0x3138;
const LOSE_CONTEXT_ON_RESET_EXT: egl::Int = 0x31BF;
const GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV: egl::Int = 0x334C;

/// One OpenGL ES 2.0 context shared by every surface.
pub struct Egl {
    pub egl: egl::Instance<egl::Static>,
    pub display: egl::Display,
    pub config: egl::Config,
    pub context: egl::Context,
    /// Surface the context rests on between window draws; `None` with `EGL_KHR_surfaceless_context`.
    pub rest_surface: Option<egl::Surface>,
    /// The attributes `context` was created with, reused for share contexts.
    context_attribs: Vec<egl::Int>,
}

impl Egl {
    pub fn from_wayland(display_ptr: *mut c_void) -> Result<Egl, String> {
        let instance = egl::Instance::new(egl::Static);
        // SAFETY: `display_ptr` is the live `wl_display` of this process's connection.
        let display = unsafe { instance.get_display(display_ptr) }.ok_or("egl: no display")?;
        Egl::init(instance, display, egl::WINDOW_BIT | egl::PBUFFER_BIT)
    }

    /// An offscreen display for tests (`EGL_PLATFORM_SURFACELESS_MESA`).
    #[cfg(test)]
    pub fn surfaceless() -> Result<Egl, String> {
        const PLATFORM_SURFACELESS_MESA: egl::Enum = 0x31DD;
        let instance = egl::Instance::new(egl::Static);
        // SAFETY: the surfaceless platform takes no native display.
        let display = unsafe { instance.get_platform_display(PLATFORM_SURFACELESS_MESA, egl::DEFAULT_DISPLAY, &[egl::ATTRIB_NONE]) }
            .map_err(|e| format!("egl: no surfaceless display: {e}"))?;
        // The surfaceless platform has no window configs.
        Egl::init(instance, display, egl::PBUFFER_BIT)
    }

    fn init(instance: egl::Instance<egl::Static>, display: egl::Display, surface_type: egl::Int) -> Result<Egl, String> {
        instance.initialize(display).map_err(|e| format!("egl: eglInitialize failed: {e}"))?;
        instance.bind_api(egl::OPENGL_ES_API).map_err(|e| format!("egl: eglBindAPI failed: {e}"))?;
        let config_attribs = [
            egl::SURFACE_TYPE,
            surface_type,
            egl::RENDERABLE_TYPE,
            egl::OPENGL_ES2_BIT,
            egl::RED_SIZE,
            8,
            egl::GREEN_SIZE,
            8,
            egl::BLUE_SIZE,
            8,
            egl::ALPHA_SIZE,
            8,
            egl::NONE,
        ];
        let config = instance
            .choose_first_config(display, &config_attribs)
            .ok()
            .flatten()
            .ok_or("egl: no RGBA8888 OpenGL ES 2 config")?;
        let extensions = instance.query_string(Some(display), egl::EXTENSIONS).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        let has = |name: &str| extensions.split_ascii_whitespace().any(|e| e == name);

        let mut context = None;
        let mut context_attribs = vec![egl::CONTEXT_MAJOR_VERSION, 2, egl::NONE];
        if has("EGL_EXT_create_context_robustness") {
            let purge = has("EGL_NV_robustness_video_memory_purge");
            let mut attribs = vec![egl::CONTEXT_MAJOR_VERSION, 2, CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, LOSE_CONTEXT_ON_RESET_EXT];
            if purge {
                attribs.extend([GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV, egl::TRUE as egl::Int]);
            }
            attribs.push(egl::NONE);
            match instance.create_context(display, config, None, &attribs) {
                Ok(c) => {
                    ::log::info!("egl: robust context, lose on reset{}", if purge { ", video memory purge" } else { "" });
                    context = Some(c);
                    context_attribs = attribs;
                }
                Err(e) => ::log::info!("egl: robust context creation failed, egl error 0x{:04x}, falling back to a plain context", e.native()),
            }
        }
        let context = match context {
            Some(c) => c,
            None => instance.create_context(display, config, None, &context_attribs).map_err(|e| {
                format!("egl: OpenGL ES 2.0 context creation failed, egl error 0x{:04x}", e.native())
            })?,
        };

        let rest_surface = if has("EGL_KHR_surfaceless_context") {
            None
        } else {
            let surface = instance
                .create_pbuffer_surface(display, config, &[egl::WIDTH, 1, egl::HEIGHT, 1, egl::NONE])
                .map_err(|e| format!("egl: rest pbuffer failed: {e}"))?;
            ::log::info!("egl: no EGL_KHR_surfaceless_context, resting on a 1x1 pbuffer");
            Some(surface)
        };
        Ok(Egl { egl: instance, display, config, context, rest_surface, context_attribs })
    }

    /// Makes the context current on `surface`, or on the rest surface for `None`.
    pub fn make_current(&self, surface: Option<egl::Surface>) -> bool {
        let target = surface.or(self.rest_surface);
        if let Err(e) = self.egl.make_current(self.display, target, target, Some(self.context)) {
            ::log::error!("egl: eglMakeCurrent failed 0x{:04x}", e.native());
            return false;
        }
        if surface.is_some() {
            // Frame callbacks pace drawing, never the swap.
            let _ = self.egl.swap_interval(self.display, 0);
        }
        true
    }

    /// A context sharing objects with the main one, created with its exact attributes, for a render thread.
    pub fn share_context(&self) -> Option<egl::Context> {
        self.egl
            .create_context(self.display, self.config, Some(self.context), &self.context_attribs)
            .inspect_err(|e| ::log::error!("egl: share context creation failed 0x{:04x}", e.native()))
            .ok()
    }

    pub fn rest(&self) {
        self.make_current(None);
    }

    /// Moves the context off `surface` before it is destroyed.
    pub fn release_if_current(&self, surface: egl::Surface) {
        if self.egl.get_current_surface(egl::DRAW) == Some(surface) {
            self.rest();
        }
    }

    pub fn gl(&self) -> glow::Context {
        load_gl(&self.egl)
    }
}

/// GL entry points through EGL, for whichever context is current on the calling thread.
pub fn load_gl(instance: &egl::Instance<egl::Static>) -> glow::Context {
    // SAFETY: the loader returns EGL's own entry points for the current client API.
    unsafe { glow::Context::from_loader_function(|name| instance.get_proc_address(name).map_or(std::ptr::null(), |f| f as *const c_void)) }
}
