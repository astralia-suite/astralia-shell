//! The bar's items.

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Item {
    Logout,
    Tray,
    Resource,
    Network,
    Bluetooth,
    Volume,
    Brightness,
    Battery,
    Media,
    Time,
    Date,
    WorkspaceInfo,
}

impl Item {
    pub const ALL: [Item; ITEM_COUNT] = [
        Item::Logout,
        Item::Tray,
        Item::Resource,
        Item::Network,
        Item::Bluetooth,
        Item::Volume,
        Item::Brightness,
        Item::Battery,
        Item::Media,
        Item::Time,
        Item::Date,
        Item::WorkspaceInfo,
    ];
}

pub const ITEM_COUNT: usize = 12;
