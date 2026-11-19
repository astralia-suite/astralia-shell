//! Registers the bundled fonts (text, Tabler icons, display glyphs) with fontconfig.

use std::ffi::{CString, c_int, c_void};
use std::sync::OnceLock;

const INSTALLED_FONTS: &str = "/usr/share/astralia-shell/fonts";

#[link(name = "fontconfig")]
unsafe extern "C" {
    fn FcConfigAppFontAddDir(config: *mut c_void, dir: *const u8) -> c_int;
}

/// The installed font directory when it holds the icon font, else the source tree's.
pub fn fonts_dir() -> String {
    if std::path::Path::new(INSTALLED_FONTS).join("tabler-icons.ttf").is_file() {
        return INSTALLED_FONTS.to_string();
    }
    concat!(env!("CARGO_MANIFEST_DIR"), "/assets/fonts").to_string()
}

/// Adds the font directory to the current fontconfig configuration, once; must run before the first Pango font map.
pub fn register() -> bool {
    static REGISTERED: OnceLock<bool> = OnceLock::new();
    *REGISTERED.get_or_init(|| {
        let dir = fonts_dir();
        let Ok(path) = CString::new(dir.as_str()) else {
            ::log::error!("fonts: cannot register {dir}");
            return false;
        };
        // SAFETY: a null config is the current one; `path` is NUL-terminated and outlives the call.
        let ok = unsafe { FcConfigAppFontAddDir(std::ptr::null_mut(), path.as_ptr().cast()) } != 0;
        if ok {
            ::log::info!("fonts: registered {dir}");
        } else {
            ::log::error!("fonts: cannot register {dir}");
        }
        ok
    })
}
