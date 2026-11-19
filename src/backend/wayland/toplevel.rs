//! The compositor's window list through wlr-foreign-toplevel, paired with its own clients so a
//! window address names a toplevel handle.

use std::collections::HashMap;

use smithay_client_toolkit::reexports::protocols_wlr::foreign_toplevel::v1::client::zwlr_foreign_toplevel_handle_v1::{self, ZwlrForeignToplevelHandleV1};
use smithay_client_toolkit::reexports::protocols_wlr::foreign_toplevel::v1::client::zwlr_foreign_toplevel_manager_v1::{self, ZwlrForeignToplevelManagerV1};
use wayland_client::globals::GlobalList;
use wayland_client::{Connection, Dispatch, QueueHandle, event_created_child};

use super::State;
use crate::service::compositor::{CompositorClient, ToplevelIdentity, pair_toplevels};

pub struct ToplevelHandle {
    pub handle: ZwlrForeignToplevelHandleV1,
    pub app_id: String,
    pub title: String,
}

#[derive(Default)]
pub struct ToplevelList {
    pub manager: Option<ZwlrForeignToplevelManagerV1>,
    pub handles: Vec<ToplevelHandle>,
}

impl ToplevelList {
    /// Binds the manager when the compositor offers it; without it the list stays empty.
    pub fn bind(globals: &GlobalList, qh: &QueueHandle<State>) -> ToplevelList {
        let manager = globals.bind(qh, 1..=3, ()).map_err(|e| ::log::info!("toplevel: no foreign toplevel manager: {e}")).ok();
        ToplevelList { manager, handles: Vec::new() }
    }

    fn entry(&mut self, handle: &ZwlrForeignToplevelHandleV1) -> Option<&mut ToplevelHandle> {
        self.handles.iter_mut().find(|h| h.handle == *handle)
    }
}

/// The toplevel handle of every client the compositor lists, keyed by its address.
pub fn toplevel_handles_for(list: &ToplevelList, clients: &[CompositorClient]) -> HashMap<String, ZwlrForeignToplevelHandleV1> {
    let identities: Vec<ToplevelIdentity> = list.handles.iter().map(|h| ToplevelIdentity { app_id: h.app_id.clone(), title: h.title.clone() }).collect();
    pair_toplevels(clients, &identities).into_iter().map(|(address, index)| (address, list.handles[index].handle.clone())).collect()
}

impl Dispatch<ZwlrForeignToplevelManagerV1, ()> for State {
    fn event(state: &mut Self, manager: &ZwlrForeignToplevelManagerV1, event: zwlr_foreign_toplevel_manager_v1::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        use zwlr_foreign_toplevel_manager_v1::Event;
        match event {
            Event::Toplevel { toplevel } => state.toplevels.handles.push(ToplevelHandle { handle: toplevel, app_id: String::new(), title: String::new() }),
            Event::Finished if state.toplevels.manager.as_ref() == Some(manager) => state.toplevels.manager = None,
            _ => {}
        }
    }

    event_created_child!(State, ZwlrForeignToplevelManagerV1, [
        zwlr_foreign_toplevel_manager_v1::EVT_TOPLEVEL_OPCODE => (ZwlrForeignToplevelHandleV1, ()),
    ]);
}

impl Dispatch<ZwlrForeignToplevelHandleV1, ()> for State {
    fn event(state: &mut Self, handle: &ZwlrForeignToplevelHandleV1, event: zwlr_foreign_toplevel_handle_v1::Event, _: &(), _: &Connection, _: &QueueHandle<Self>) {
        use zwlr_foreign_toplevel_handle_v1::Event;
        let list = &mut state.toplevels;
        match event {
            Event::Title { title } => {
                if let Some(entry) = list.entry(handle) {
                    entry.title = title;
                }
            }
            Event::AppId { app_id } => {
                if let Some(entry) = list.entry(handle) {
                    entry.app_id = app_id;
                }
            }
            Event::Closed => {
                handle.destroy();
                list.handles.retain(|h| h.handle != *handle);
            }
            _ => {}
        }
    }
}
