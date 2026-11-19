//! The clipboard through `wl_data_device`: copy offers our text, paste reads the best text type of
//! the current selection without blocking the loop.

use std::cell::{Cell, RefCell};
use std::io::{ErrorKind, Read, Write};
use std::os::fd::AsRawFd;
use std::rc::Rc;

use calloop::{LoopHandle, PostAction};
use smithay_client_toolkit::data_device_manager::data_device::DataDevice;
use smithay_client_toolkit::data_device_manager::data_source::CopyPasteSource;
use smithay_client_toolkit::data_device_manager::{DataDeviceManagerState, WritePipe};
use wayland_client::QueueHandle;
use wayland_client::protocol::wl_data_source::WlDataSource;
use wayland_client::protocol::wl_seat::WlSeat;

use super::State;
use crate::base::input;

/// Text types, best first.
const MIMES: [&str; 5] = ["text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING"];
const MAX_PASTE: usize = 1 << 20;

/// Higher is better; 0 is not text.
pub fn mime_rank(mime: &str) -> usize {
    MIMES.iter().position(|m| *m == mime).map_or(0, |i| MIMES.len() - i)
}

/// The best text type among `offered`.
pub fn best_mime(offered: &[String]) -> Option<String> {
    offered.iter().filter(|m| mime_rank(m) > 0).max_by_key(|m| mime_rank(m)).cloned()
}

fn set_nonblocking(fd: i32) {
    // SAFETY: plain `fcntl` flag calls on a descriptor we own.
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL);
        libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
    }
}

/// The seat's data device and our current selection, shared with the fields that copy and paste.
pub struct Clipboard {
    manager: DataDeviceManagerState,
    device: RefCell<Option<DataDevice>>,
    source: RefCell<Option<CopyPasteSource>>,
    text: RefCell<Rc<str>>,
    /// The latest key press serial, which a selection must carry.
    pub serial: Cell<u32>,
    qh: QueueHandle<State>,
    handle: LoopHandle<'static, State>,
}

impl Clipboard {
    pub fn new(manager: DataDeviceManagerState, qh: QueueHandle<State>, handle: LoopHandle<'static, State>) -> Clipboard {
        Clipboard { manager, device: RefCell::default(), source: RefCell::default(), text: RefCell::new(Rc::from("")), serial: Cell::new(0), qh, handle }
    }

    pub fn bind_seat(&self, seat: &WlSeat) {
        if self.device.borrow().is_none() {
            *self.device.borrow_mut() = Some(self.manager.get_data_device(&self.qh, seat));
        }
    }

    /// A paste target asked for our selection: write the text off the loop thread.
    pub fn send(&self, source: &WlDataSource, pipe: WritePipe) {
        if !self.source.borrow().as_ref().is_some_and(|s| s.inner() == source) {
            return;
        }
        let text = self.text.borrow().clone().to_string();
        crate::service::runtime::spawn(async move {
            let _ = blocking::unblock(move || {
                let mut file = std::fs::File::from(std::os::fd::OwnedFd::from(pipe));
                file.write_all(text.as_bytes())
            })
            .await;
        });
    }

    /// Another client took the selection.
    pub fn cancelled(&self, source: &WlDataSource) {
        let mut ours = self.source.borrow_mut();
        if ours.as_ref().is_some_and(|s| s.inner() == source) {
            *ours = None;
        }
    }
}

impl input::Clipboard for Clipboard {
    fn set(&self, text: String) {
        let device = self.device.borrow();
        let Some(device) = device.as_ref() else {
            ::log::info!("clipboard: no data device");
            return;
        };
        let source = self.manager.create_copy_paste_source(&self.qh, MIMES);
        *self.text.borrow_mut() = Rc::from(text);
        source.set_selection(device, self.serial.get());
        *self.source.borrow_mut() = Some(source);
    }

    fn request(&self, done: Box<dyn FnOnce(String)>) {
        if self.source.borrow().is_some() {
            done(self.text.borrow().to_string());
            return;
        }
        let offer = self.device.borrow().as_ref().and_then(|d| d.data().selection_offer());
        let Some(offer) = offer else { return done(String::new()) };
        let Some(mime) = offer.with_mime_types(best_mime) else { return done(String::new()) };
        let pipe = match offer.receive(mime) {
            Ok(pipe) => pipe,
            Err(e) => {
                ::log::error!("clipboard: cannot read the selection: {e}");
                return done(String::new());
            }
        };
        let received = Rc::new(RefCell::new(Vec::new()));
        let mut done = Some(done);
        let inserted = self.handle.insert_source(pipe, move |(), file, _| {
            set_nonblocking(file.as_raw_fd());
            let mut buf = [0u8; 4096];
            loop {
                // SAFETY: the pipe is only read here; `NoIoDrop` keeps it open.
                match unsafe { file.get_mut() }.read(&mut buf) {
                    Ok(n) if n > 0 && received.borrow().len() < MAX_PASTE => received.borrow_mut().extend_from_slice(&buf[..n]),
                    Err(e) if e.kind() == ErrorKind::WouldBlock => return PostAction::Continue,
                    Err(e) if e.kind() == ErrorKind::Interrupted => {}
                    _ => break,
                }
            }
            if let Some(done) = done.take() {
                done(String::from_utf8_lossy(&received.borrow()).into_owned());
            }
            PostAction::Remove
        });
        if let Err(e) = inserted {
            ::log::error!("clipboard: cannot watch the selection pipe: {e}");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn clipboard_mime_rank() {
        assert!(mime_rank("text/plain;charset=utf-8") > mime_rank("text/plain"), "UTF-8 text first");
        assert!(mime_rank("text/plain") > mime_rank("UTF8_STRING"), "then plain text");
        assert!(mime_rank("UTF8_STRING") > mime_rank("TEXT") && mime_rank("TEXT") > mime_rank("STRING"));
        assert_eq!(mime_rank("image/png"), 0, "anything else is not text");
        let offered = ["image/png", "UTF8_STRING", "text/plain"].map(String::from);
        assert_eq!(best_mime(&offered), Some("text/plain".to_string()), "the best text type of an offer");
        assert_eq!(best_mime(&["image/png".to_string()]), None);
    }
}
