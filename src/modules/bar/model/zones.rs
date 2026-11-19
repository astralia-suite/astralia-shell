//! Which items sit in the bar's left, centre and right zones.

use std::sync::LazyLock;

use super::item::Item;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SlotKind {
    Item,
    Workspaces,
    Status,
    Dock,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Slot {
    pub kind: SlotKind,
    /// Set for `SlotKind::Item` only.
    pub item: Option<Item>,
}

impl Slot {
    const fn item(item: Item) -> Slot {
        Slot { kind: SlotKind::Item, item: Some(item) }
    }

    const fn of(kind: SlotKind) -> Slot {
        Slot { kind, item: None }
    }
}

#[derive(Debug)]
pub struct Zones {
    pub left: Vec<Slot>,
    pub center: Vec<Slot>,
    pub right: Vec<Slot>,
}

/// The items drawn together in the status pill.
pub const STATUS_MEMBERS: [Item; 3] = [Item::Network, Item::Bluetooth, Item::Volume];

pub fn zones() -> &'static Zones {
    static ZONES: LazyLock<Zones> = LazyLock::new(|| Zones {
        left: vec![Slot::item(Item::Logout), Slot::item(Item::ActiveWindow), Slot::item(Item::Media), Slot::item(Item::Tray)],
        center: vec![Slot::of(SlotKind::Status), Slot::of(SlotKind::Workspaces), Slot::item(Item::Time), Slot::item(Item::Date)],
        right: vec![Slot::of(SlotKind::Dock), Slot::item(Item::Resource), Slot::item(Item::Brightness), Slot::item(Item::Battery)],
    });
    &ZONES
}
