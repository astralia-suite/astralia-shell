//! dma-buf `EGLImage` import of NV12 VAAPI surfaces, sampled as `GL_TEXTURE_EXTERNAL_OES`.

use std::ffi::c_void;

use glow::HasContext;
use khronos_egl as egl;

use super::egl::Egl;
use crate::service::media_decode::MediaDrmFrame;

pub const DRM_FORMAT_MOD_INVALID: u64 = 0x00ff_ffff_ffff_ffff;
pub const DRM_FORMAT_NV12: u32 = u32::from_le_bytes(*b"NV12");
pub const TEXTURE_EXTERNAL_OES: u32 = 0x8D65;
const LINUX_DMA_BUF_EXT: u32 = 0x3270;
const LINUX_DRM_FOURCC_EXT: i32 = 0x3271;
const DMA_BUF_PLANE0_FD_EXT: i32 = 0x3272;
const DMA_BUF_PLANE0_OFFSET_EXT: i32 = 0x3273;
const DMA_BUF_PLANE0_PITCH_EXT: i32 = 0x3274;
const DMA_BUF_PLANE1_FD_EXT: i32 = 0x3275;
const DMA_BUF_PLANE1_OFFSET_EXT: i32 = 0x3276;
const DMA_BUF_PLANE1_PITCH_EXT: i32 = 0x3277;
const DMA_BUF_PLANE0_MODIFIER_LO_EXT: i32 = 0x3443;
const DMA_BUF_PLANE0_MODIFIER_HI_EXT: i32 = 0x3444;
const DMA_BUF_PLANE1_MODIFIER_LO_EXT: i32 = 0x3445;
const DMA_BUF_PLANE1_MODIFIER_HI_EXT: i32 = 0x3446;

#[derive(Clone, Copy, Debug)]
pub struct ImportPlane {
    pub fd: i32,
    pub modifier: u64,
    pub offset: i32,
    pub pitch: i32,
}

/// The `eglCreateImageKHR` attributes for a two-plane NV12 dma-buf; modifiers only when known.
pub fn video_import_attribs(width: i32, height: i32, planes: [ImportPlane; 2]) -> Vec<i32> {
    let [y, uv] = planes;
    let mut attribs = vec![
        egl::WIDTH,
        width,
        egl::HEIGHT,
        height,
        LINUX_DRM_FOURCC_EXT,
        DRM_FORMAT_NV12 as i32,
        DMA_BUF_PLANE0_FD_EXT,
        y.fd,
        DMA_BUF_PLANE0_OFFSET_EXT,
        y.offset,
        DMA_BUF_PLANE0_PITCH_EXT,
        y.pitch,
        DMA_BUF_PLANE1_FD_EXT,
        uv.fd,
        DMA_BUF_PLANE1_OFFSET_EXT,
        uv.offset,
        DMA_BUF_PLANE1_PITCH_EXT,
        uv.pitch,
    ];
    for (plane, lo, hi) in [(y, DMA_BUF_PLANE0_MODIFIER_LO_EXT, DMA_BUF_PLANE0_MODIFIER_HI_EXT), (uv, DMA_BUF_PLANE1_MODIFIER_LO_EXT, DMA_BUF_PLANE1_MODIFIER_HI_EXT)] {
        if plane.modifier != DRM_FORMAT_MOD_INVALID {
            attribs.extend([lo, (plane.modifier & 0xffff_ffff) as u32 as i32, hi, (plane.modifier >> 32) as u32 as i32]);
        }
    }
    attribs.push(egl::NONE);
    attribs
}

type CreateImage = unsafe extern "C" fn(*mut c_void, *mut c_void, u32, *mut c_void, *const i32) -> *mut c_void;
type DestroyImage = unsafe extern "C" fn(*mut c_void, *mut c_void) -> u32;
type TargetTexture = unsafe extern "C" fn(u32, *mut c_void);

/// The import entry points, present when the four extensions are.
#[derive(Clone, Copy)]
pub struct VideoCaps {
    display: *mut c_void,
    create_image: CreateImage,
    destroy_image: DestroyImage,
    target_texture: TargetTexture,
}

/// Checks the dma-buf import extensions and loads their entry points; the GL context must be current.
pub fn video_texture_detect_caps(egl: &Egl, gl: &glow::Context) -> Option<VideoCaps> {
    let egl_ext = egl.egl.query_string(Some(egl.display), egl::EXTENSIONS).map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let has_egl = |name: &str| egl_ext.split_ascii_whitespace().any(|e| e == name);
    let gl_ext = gl.supported_extensions();
    if !has_egl("EGL_EXT_image_dma_buf_import") || !has_egl("EGL_KHR_image_base") || !gl_ext.contains("GL_OES_EGL_image") || !gl_ext.contains("GL_OES_EGL_image_external") {
        return None;
    }
    let procs = (egl.egl.get_proc_address("eglCreateImageKHR"), egl.egl.get_proc_address("eglDestroyImageKHR"), egl.egl.get_proc_address("glEGLImageTargetTexture2DOES"));
    let (Some(create), Some(destroy), Some(target)) = procs else {
        ::log::info!("video_texture: dma-buf extensions advertised but proc addresses missing, disabling zero-copy import");
        return None;
    };
    // SAFETY: the advertised extensions define these entry points with these signatures.
    unsafe {
        Some(VideoCaps {
            display: egl.display.as_ptr(),
            create_image: std::mem::transmute::<extern "system" fn(), CreateImage>(create),
            destroy_image: std::mem::transmute::<extern "system" fn(), DestroyImage>(destroy),
            target_texture: std::mem::transmute::<extern "system" fn(), TargetTexture>(target),
        })
    }
}

/// An imported surface; freed with `VideoCaps::release` while the context is current.
pub struct VideoTexture {
    pub tex: glow::Texture,
    image: *mut c_void,
    pub width: i32,
    pub height: i32,
}

impl VideoCaps {
    /// Imports `frame` as an external texture; the context must be current.
    pub fn import(&self, gl: &glow::Context, frame: &MediaDrmFrame) -> Option<VideoTexture> {
        use std::os::fd::AsRawFd;
        let plane = |p: &crate::service::media_decode::MediaDrmPlane| ImportPlane { fd: p.fd.as_raw_fd(), modifier: p.modifier, offset: p.offset, pitch: p.pitch };
        let attribs = video_import_attribs(frame.width, frame.height, [plane(&frame.planes[0]), plane(&frame.planes[1])]);
        // SAFETY: the fds stay open for the call; `attribs` ends with EGL_NONE.
        let image = unsafe { (self.create_image)(self.display, std::ptr::null_mut(), LINUX_DMA_BUF_EXT, std::ptr::null_mut(), attribs.as_ptr()) };
        if image.is_null() {
            ::log::info!("video_texture: combined nv12 plane import failed");
            return None;
        }
        // SAFETY: the context is current; `image` is a live EGLImage.
        unsafe {
            let Ok(tex) = gl.create_texture() else {
                (self.destroy_image)(self.display, image);
                return None;
            };
            gl.bind_texture(TEXTURE_EXTERNAL_OES, Some(tex));
            for (pname, value) in [
                (glow::TEXTURE_MIN_FILTER, glow::LINEAR),
                (glow::TEXTURE_MAG_FILTER, glow::LINEAR),
                (glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE),
                (glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE),
            ] {
                gl.tex_parameter_i32(TEXTURE_EXTERNAL_OES, pname, value as i32);
            }
            (self.target_texture)(TEXTURE_EXTERNAL_OES, image);
            gl.bind_texture(TEXTURE_EXTERNAL_OES, None);
            Some(VideoTexture { tex, image, width: frame.width, height: frame.height })
        }
    }

    pub fn release(&self, gl: &glow::Context, tex: VideoTexture) {
        // SAFETY: the context is current; both objects were created by `import`.
        unsafe {
            gl.delete_texture(tex.tex);
            (self.destroy_image)(self.display, tex.image);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn video_import_attribs_layout() {
        let plane = |modifier| ImportPlane { fd: 7, modifier, offset: 16, pitch: 1920 };
        let plain = video_import_attribs(1920, 1080, [plane(DRM_FORMAT_MOD_INVALID), ImportPlane { fd: 8, ..plane(DRM_FORMAT_MOD_INVALID) }]);
        assert_eq!(plain.len(), 19, "18 values and EGL_NONE without modifiers");
        assert_eq!(&plain[..8], &[egl::WIDTH, 1920, egl::HEIGHT, 1080, LINUX_DRM_FOURCC_EXT, DRM_FORMAT_NV12 as i32, DMA_BUF_PLANE0_FD_EXT, 7]);
        assert_eq!(plain[13], 8, "plane 1 fd");
        assert_eq!(*plain.last().unwrap(), egl::NONE);

        let tiled = video_import_attribs(64, 32, [plane(0x0100_0000_0000_0004), plane(0x0100_0000_0000_0004)]);
        assert_eq!(tiled.len(), 27, "both planes add their modifier halves");
        assert_eq!(&tiled[18..22], &[DMA_BUF_PLANE0_MODIFIER_LO_EXT, 4, DMA_BUF_PLANE0_MODIFIER_HI_EXT, 0x0100_0000]);
        assert_eq!(*tiled.last().unwrap(), egl::NONE);
    }
}
