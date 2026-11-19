//! The app-wide overlays, and where keys and new overlays go.

use wayland_client::backend::ObjectId;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_surface::WlSurface;

use super::render::egl::Egl;
use super::render::overlay::Overlay;
use super::render::renderer::Renderer;
use crate::modules::launcher::wayland::launcher::Launcher;
use crate::modules::logout::wayland::logout::Logout;
use crate::modules::overview::wayland::overview::Overview;
use crate::modules::polkit::wayland::polkit::Polkit;
use crate::modules::settings::wayland::settings::Settings;
use crate::modules::wallpaper_select::wayland::wallpaper_select::WallpaperSelect;

/// One app-wide overlay; each gets a variant with its module.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OverlayId {
    Launcher,
    Logout,
    Overview,
    Polkit,
    Settings,
    WallpaperSelect,
}

/// The app-wide overlays, one slot each, filled when the module starts.
#[derive(Default)]
pub struct Overlays {
    pub launcher: Option<Launcher>,
    pub logout: Option<Logout>,
    pub overview: Option<Overview>,
    pub polkit: Option<Polkit>,
    pub settings: Option<Settings>,
    pub wallpaper_select: Option<WallpaperSelect>,
}

impl Overlays {
    /// Every overlay that exists, with its id.
    pub fn all_mut(&mut self) -> Vec<(OverlayId, &mut Overlay)> {
        let mut all = Vec::new();
        if let Some(l) = &mut self.launcher {
            all.push((OverlayId::Launcher, &mut l.overlay));
        }
        if let Some(l) = &mut self.logout {
            all.push((OverlayId::Logout, &mut l.overlay));
        }
        if let Some(o) = &mut self.overview {
            all.push((OverlayId::Overview, &mut o.overlay));
        }
        if let Some(p) = &mut self.polkit {
            all.push((OverlayId::Polkit, &mut p.overlay));
        }
        if let Some(s) = &mut self.settings {
            all.push((OverlayId::Settings, &mut s.overlay));
        }
        if let Some(w) = &mut self.wallpaper_select {
            all.push((OverlayId::WallpaperSelect, &mut w.overlay));
        }
        all
    }

    pub fn all(&self) -> Vec<(OverlayId, &Overlay)> {
        let mut all = Vec::new();
        if let Some(l) = &self.launcher {
            all.push((OverlayId::Launcher, &l.overlay));
        }
        if let Some(l) = &self.logout {
            all.push((OverlayId::Logout, &l.overlay));
        }
        if let Some(o) = &self.overview {
            all.push((OverlayId::Overview, &o.overlay));
        }
        if let Some(p) = &self.polkit {
            all.push((OverlayId::Polkit, &p.overlay));
        }
        if let Some(s) = &self.settings {
            all.push((OverlayId::Settings, &s.overlay));
        }
        if let Some(w) = &self.wallpaper_select {
            all.push((OverlayId::WallpaperSelect, &w.overlay));
        }
        all
    }

    pub fn owner_of(&self, surface: &WlSurface) -> Option<OverlayId> {
        self.all().into_iter().find(|(_, o)| o.owns(surface)).map(|(id, _)| id)
    }

    /// Releases every overlay bound to `removed`; returns their destroyed surfaces.
    pub fn release_output(&mut self, egl: &Egl, removed: &WlOutput) -> Vec<ObjectId> {
        self.all_mut().into_iter().filter_map(|(_, o)| o.release_output(egl, removed)).collect()
    }

    pub fn destroy(&mut self, egl: &Egl, renderer: &Renderer) {
        if let Some(launcher) = self.launcher.take() {
            launcher.destroy(egl, renderer);
        }
        if let Some(logout) = self.logout.take() {
            logout.destroy(egl, renderer);
        }
        if let Some(overview) = self.overview.take() {
            overview.destroy(egl, renderer);
        }
        if let Some(polkit) = self.polkit.take() {
            polkit.destroy(egl, renderer);
        }
        if let Some(settings) = self.settings.take() {
            settings.destroy(egl, renderer);
        }
        if let Some(picker) = self.wallpaper_select.take() {
            picker.destroy(egl, renderer);
        }
    }
}

/// Whether running a verb that opens `keep` closes overlay `id`. The polkit card stays: a verb must
/// not cancel an authentication.
pub fn closed_by_verb(id: OverlayId, keep: Option<OverlayId>) -> bool {
    id != OverlayId::Polkit && keep != Some(id)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KeyRoute<O, W = ()> {
    /// The session lock takes every key while it is active.
    Lock,
    /// A toplevel window with the keyboard focus.
    Window(W),
    Overlay(O),
    Monitor(usize),
    Nowhere,
}

/// A key goes to the lock while it is active, else to the toplevel window or the overlay owning the
/// keyboard-focused surface, else to the first monitor with an open panel, else nowhere.
pub fn route_key<S, O, W>(
    lock_active: bool,
    focused: Option<&S>,
    window_of: impl Fn(&S) -> Option<W>,
    owner_of: impl Fn(&S) -> Option<O>,
    monitors_open: &[bool],
) -> KeyRoute<O, W> {
    if lock_active {
        return KeyRoute::Lock;
    }
    if let Some(id) = focused.and_then(window_of) {
        return KeyRoute::Window(id);
    }
    if let Some(id) = focused.and_then(owner_of) {
        return KeyRoute::Overlay(id);
    }
    match monitors_open.iter().position(|&open| open) {
        Some(i) => KeyRoute::Monitor(i),
        None => KeyRoute::Nowhere,
    }
}

/// The output an overlay opens on: the compositor's focused monitor, else the one the pointer was
/// last on, else the first.
pub fn select_output(names: &[&str], focused: &str, pointer_hint: Option<usize>) -> Option<usize> {
    if !focused.is_empty()
        && let Some(i) = names.iter().position(|n| *n == focused)
    {
        return Some(i);
    }
    pointer_hint.filter(|&i| i < names.len()).or(if names.is_empty() { None } else { Some(0) })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_route_toplevel() {
        let overlays = |s: &u32| if *s == 10 { Some('l') } else { None };
        let windows = |s: &u32| if *s == 40 { Some('r') } else { None };
        assert_eq!(route_key(true, Some(&40), windows, overlays, &[true]), KeyRoute::Lock, "the lock beats a focused window");
        assert_eq!(route_key(false, Some(&40), windows, overlays, &[true]), KeyRoute::Window('r'), "a focused window beats an open panel");
        assert_eq!(route_key(false, Some(&10), windows, overlays, &[false]), KeyRoute::Overlay('l'), "an overlay with the focus still gets its keys");
        assert_eq!(route_key(false, None, windows, overlays, &[false]), KeyRoute::Nowhere, "no focused window falls through");
    }

    #[test]
    fn key_route_order() {
        let none = |_: &u32| None::<()>;
        let overlays = |s: &u32| match s {
            10 => Some('l'),
            20 => Some('o'),
            _ => None,
        };
        assert_eq!(route_key(false, Some(&20), none, overlays, &[true, false]), KeyRoute::Overlay('o'), "the focused overlay wins over an open panel");
        assert_eq!(route_key(false, Some(&30), none, overlays, &[false, true]), KeyRoute::Monitor(1), "else the first monitor with an open panel");
        assert_eq!(route_key(false, None, none, overlays, &[true, true]), KeyRoute::Monitor(0));
        assert_eq!(route_key(false, None, none, overlays, &[false, false]), KeyRoute::Nowhere, "else nowhere");
    }

    #[test]
    fn key_route_locked() {
        let none = |_: &u32| None::<()>;
        let overlays = |s: &u32| if *s == 10 { Some('l') } else { None };
        assert_eq!(route_key(true, Some(&10), none, overlays, &[true]), KeyRoute::Lock, "an active lock wins over a focused overlay and an open panel");
        assert_eq!(route_key(true, None, none, overlays, &[false]), KeyRoute::Lock);
        assert_eq!(route_key(false, Some(&10), none, overlays, &[true]), KeyRoute::Overlay('l'), "after unlock the usual order returns");
    }

    #[test]
    fn polkit_not_closed_by_verbs() {
        use crate::app::shell::ShellVerb;
        for verb in ShellVerb::ALL {
            assert!(!closed_by_verb(OverlayId::Polkit, super::super::overlay_of(verb)), "{} leaves the polkit card open", verb.name());
        }
        assert!(closed_by_verb(OverlayId::Settings, Some(OverlayId::Launcher)), "other overlays still close under a verb");
        assert!(!closed_by_verb(OverlayId::Launcher, Some(OverlayId::Launcher)), "the verb's own overlay is kept");
    }

    #[test]
    fn output_selection() {
        let names = ["DP-1", "DP-2", "HDMI-A-1"];
        assert_eq!(select_output(&names, "DP-2", Some(2)), Some(1), "the compositor's focused monitor first");
        assert_eq!(select_output(&names, "gone", Some(2)), Some(2), "then the monitor the pointer was last on");
        assert_eq!(select_output(&names, "", None), Some(0), "then the first monitor");
        assert_eq!(select_output(&[], "DP-1", None), None);
    }
}
