//! Window thumbnails through hyprland-toplevel-export: one shm buffer per window, at most one
//! capture in flight per window, and none sooner than the caller's interval after the last.

use std::collections::HashMap;
use std::time::{Duration, Instant};

use smithay_client_toolkit::error::GlobalError;
use smithay_client_toolkit::globals::ProvidesBoundGlobal;
use smithay_client_toolkit::reexports::protocols_wlr::foreign_toplevel::v1::client::zwlr_foreign_toplevel_handle_v1::ZwlrForeignToplevelHandleV1;
use smithay_client_toolkit::shm::raw::RawPool;
use wayland_client::globals::GlobalList;
use wayland_client::protocol::wl_buffer::{self, WlBuffer};
use wayland_client::protocol::wl_shm::{self, WlShm};
use wayland_client::{Connection, Dispatch, Proxy, QueueHandle, WEnum};

use super::State;
use super::render::renderer::{Renderer, Texture};

#[allow(dead_code, non_camel_case_types, non_upper_case_globals, non_snake_case, unused_imports, clippy::all)]
pub mod protocol {
    use smithay_client_toolkit::reexports::protocols_wlr::foreign_toplevel::v1::client::*;
    use wayland_client;
    use wayland_client::protocol::*;

    pub mod __interfaces {
        use smithay_client_toolkit::reexports::protocols_wlr::foreign_toplevel::v1::client::__interfaces::*;
        use wayland_client::protocol::__interfaces::*;
        wayland_scanner::generate_interfaces!("src/backend/wayland/protocols/hyprland-toplevel-export-v1.xml");
    }
    use self::__interfaces::*;

    wayland_scanner::generate_client_code!("src/backend/wayland/protocols/hyprland-toplevel-export-v1.xml");
}

use protocol::hyprland_toplevel_export_frame_v1::{self, HyprlandToplevelExportFrameV1};
use protocol::hyprland_toplevel_export_manager_v1::HyprlandToplevelExportManagerV1;

/// Whether a window may be captured now: nothing in flight and the interval since the last capture passed.
pub fn due(in_flight: bool, last: Option<Instant>, now: Instant, min_interval: Duration) -> bool {
    !in_flight && last.is_none_or(|last| now.checked_duration_since(last).is_some_and(|elapsed| elapsed >= min_interval))
}

/// A window address as the compositor writes it (`0x…`), as the export handle.
fn parse_handle(address: &str) -> u32 {
    let hex = address.trim_start_matches("0x");
    let end = hex.find(|c: char| !c.is_ascii_hexdigit()).unwrap_or(hex.len());
    u64::from_str_radix(&hex[..end], 16).unwrap_or(0) as u32
}

/// The bound `wl_shm`, as the shm pool takes it.
pub struct ShmGlobal(WlShm);

impl ProvidesBoundGlobal<WlShm, 1> for ShmGlobal {
    fn bound_global(&self) -> Result<WlShm, GlobalError> {
        Ok(self.0.clone())
    }
}

#[derive(Clone, Copy, PartialEq)]
struct BufferSpec {
    width: u32,
    height: u32,
    stride: u32,
    format: wl_shm::Format,
}

#[derive(Default)]
struct Capture {
    frame: Option<HyprlandToplevelExportFrameV1>,
    pool: Option<RawPool>,
    buffer: Option<WlBuffer>,
    spec: Option<BufferSpec>,
    pending: Option<BufferSpec>,
    y_invert: bool,
    last: Option<Instant>,
    /// The latest frame as RGBA, uploaded on the next `texture`.
    pixels: Option<(i32, i32, Vec<u8>)>,
    tex: Option<Texture>,
}

impl Capture {
    fn finish(&mut self) {
        if let Some(frame) = self.frame.take() {
            frame.destroy();
        }
    }

    fn drop_buffer(&mut self) {
        if let Some(buffer) = self.buffer.take() {
            buffer.destroy();
        }
        self.pool = None;
        self.spec = None;
    }

    /// A buffer matching `spec`, reusing the last one when it already does.
    fn ensure_buffer(&mut self, shm: &ShmGlobal, spec: BufferSpec, qh: &QueueHandle<State>) -> Option<&WlBuffer> {
        if self.buffer.is_none() || self.spec != Some(spec) {
            self.drop_buffer();
            if spec.width == 0 || spec.height == 0 || spec.stride < spec.width * 4 {
                return None;
            }
            let size = spec.stride as usize * spec.height as usize;
            let mut pool = RawPool::new(size, shm).map_err(|e| ::log::warn!("toplevel_export: shm pool failed: {e}")).ok()?;
            self.buffer = Some(pool.create_buffer(0, spec.width as i32, spec.height as i32, spec.stride as i32, spec.format, (), qh));
            self.pool = Some(pool);
            self.spec = Some(spec);
        }
        self.buffer.as_ref()
    }

    /// Copies the shm contents out as upright RGBA.
    // ponytail: CPU swizzle of every frame; upload BGRA directly (EXT_texture_format_BGRA8888) if capture shows in profiles.
    fn read(&mut self) {
        let (Some(pool), Some(spec)) = (&mut self.pool, self.spec) else { return };
        let (w, h, stride) = (spec.width as usize, spec.height as usize, spec.stride as usize);
        let src = &pool.mmap()[..];
        if src.len() < stride * h {
            return;
        }
        let mut rgba = Vec::with_capacity(w * h * 4);
        let rows = src.chunks_exact(stride).take(h);
        let rows: Box<dyn Iterator<Item = &[u8]>> = if self.y_invert { Box::new(rows.rev()) } else { Box::new(rows) };
        for row in rows {
            for px in row[..w * 4].as_chunks::<4>().0 {
                rgba.extend_from_slice(&[px[2], px[1], px[0], px[3]]);
            }
        }
        self.pixels = Some((w as i32, h as i32, rgba));
    }

    fn release(mut self, renderer: &Renderer) {
        self.finish();
        self.drop_buffer();
        if let Some(tex) = self.tex.take() {
            renderer.delete_texture(tex);
        }
    }
}

pub struct ToplevelExportState {
    manager: Option<HyprlandToplevelExportManagerV1>,
    shm: Option<ShmGlobal>,
    captures: HashMap<String, Capture>,
}

impl ToplevelExportState {
    /// Binds the export manager and `wl_shm`; without either nothing is ever captured.
    pub fn bind(globals: &GlobalList, qh: &QueueHandle<State>) -> ToplevelExportState {
        let manager = globals.bind(qh, 1..=2, ()).map_err(|e| ::log::info!("toplevel_export: unavailable: {e}")).ok();
        let shm = globals.bind(qh, 1..=1, ()).map_err(|e| ::log::warn!("toplevel_export: no wl_shm: {e}")).ok().map(ShmGlobal);
        ToplevelExportState { manager, shm, captures: HashMap::new() }
    }

    /// Starts a capture of the window at `address` (through its toplevel handle when given) unless
    /// one is in flight or the last ended less than `min_interval` ago.
    pub fn request(&mut self, qh: &QueueHandle<State>, address: &str, toplevel: Option<&ZwlrForeignToplevelHandleV1>, min_interval: Duration) {
        let (Some(manager), Some(_)) = (&self.manager, &self.shm) else { return };
        let cap = self.captures.entry(address.to_owned()).or_default();
        if !due(cap.frame.is_some(), cap.last, Instant::now(), min_interval) {
            return;
        }
        let frame = match toplevel {
            Some(_) if manager.version() < 2 => return,
            Some(toplevel) => manager.capture_toplevel_with_wlr_toplevel_handle(0, toplevel, qh, address.to_owned()),
            None => manager.capture_toplevel(0, parse_handle(address), qh, address.to_owned()),
        };
        cap.frame = Some(frame);
    }

    /// Uploads every capture that finished since the last call; run with the context current.
    pub fn upload(&mut self, renderer: &Renderer) {
        for cap in self.captures.values_mut() {
            let Some((w, h, rgba)) = cap.pixels.take() else { continue };
            match &cap.tex {
                Some(tex) if tex.width == w && tex.height == h => renderer.update_rgba(tex, &rgba),
                _ => {
                    if let Some(old) = cap.tex.take() {
                        renderer.delete_texture(old);
                    }
                    cap.tex = renderer.upload_rgba(w, h, 1, &rgba);
                }
            }
        }
    }

    /// The latest uploaded capture of `address`, upright.
    pub fn texture(&self, address: &str) -> Option<&Texture> {
        self.captures.get(address)?.tex.as_ref()
    }

    /// Drops every window not in `live`, with its buffer, texture and any capture in flight.
    pub fn prune(&mut self, renderer: &Renderer, live: &[String]) {
        let gone: Vec<String> = self.captures.keys().filter(|a| !live.contains(a)).cloned().collect();
        for address in gone {
            if let Some(cap) = self.captures.remove(&address) {
                cap.release(renderer);
            }
        }
    }

    pub fn destroy(&mut self, renderer: &Renderer) {
        self.prune(renderer, &[]);
    }
}

impl Dispatch<HyprlandToplevelExportManagerV1, ()> for State {
    fn event(_: &mut Self, _: &HyprlandToplevelExportManagerV1, _: <HyprlandToplevelExportManagerV1 as Proxy>::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {}
}

impl Dispatch<HyprlandToplevelExportFrameV1, String> for State {
    fn event(state: &mut Self, _: &HyprlandToplevelExportFrameV1, event: hyprland_toplevel_export_frame_v1::Event, address: &String, _: &Connection, qh: &QueueHandle<Self>) {
        use hyprland_toplevel_export_frame_v1::Event;
        let export = &mut state.capture;
        let Some(cap) = export.captures.get_mut(address) else { return };
        match event {
            Event::Buffer { format: WEnum::Value(format), width, height, stride } => cap.pending = Some(BufferSpec { width, height, stride, format }),
            Event::Flags { flags } => cap.y_invert = matches!(flags, WEnum::Value(f) if f.contains(hyprland_toplevel_export_frame_v1::Flags::YInvert)),
            Event::BufferDone => {
                let copied = match (cap.pending.take(), &export.shm) {
                    (Some(spec), Some(shm)) => match cap.ensure_buffer(shm, spec, qh) {
                        Some(buffer) => {
                            let buffer = buffer.clone();
                            if let Some(frame) = &cap.frame {
                                frame.copy(&buffer, 1);
                            }
                            true
                        }
                        None => false,
                    },
                    _ => false,
                };
                if !copied {
                    cap.finish();
                }
            }
            Event::Ready { .. } => {
                cap.read();
                cap.last = Some(Instant::now());
                cap.finish();
            }
            Event::Failed => cap.finish(),
            _ => {}
        }
    }
}

impl Dispatch<WlShm, ()> for State {
    fn event(_: &mut Self, _: &WlShm, _: wl_shm::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {}
}

impl Dispatch<WlBuffer, ()> for State {
    fn event(_: &mut Self, _: &WlBuffer, _: wl_buffer::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {}
}

#[cfg(test)]
mod tests {
    use std::time::{Duration, Instant};

    use super::{due, parse_handle};

    #[test]
    fn capture_throttle() {
        let now = Instant::now();
        let interval = Duration::from_millis(500);
        assert!(due(false, None, now, interval), "a window never captured is due");
        assert!(!due(true, None, now, interval), "a capture in flight blocks the next");
        assert!(!due(false, Some(now - Duration::from_millis(200)), now, interval), "inside the interval is skipped");
        assert!(due(false, Some(now - Duration::from_millis(500)), now, interval), "the interval's end is due");
        assert!(!due(true, Some(now - Duration::from_secs(5)), now, interval), "in flight wins over an old capture");
        assert!(!due(false, Some(now + Duration::from_secs(1)), now, interval), "a capture stamped after now is not due");
        assert_eq!(parse_handle("0x5612ab30"), 0x5612_ab30);
        assert_eq!(parse_handle("deadbeef"), 0xdead_beef);
        assert_eq!(parse_handle("zz"), 0);
    }
}
