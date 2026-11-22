//! Which panel each item opens.

use super::item::Item;
use crate::modules::bar::panel::core::panel_set::PanelId;

pub fn panel_for(item: Item) -> Option<PanelId> {
    Some(match item {
        Item::Tray => PanelId::Tray,
        Item::Resource => PanelId::Resource,
        Item::Network => PanelId::Network,
        Item::Bluetooth => PanelId::Bluetooth,
        Item::Volume => PanelId::Volume,
        Item::Battery => PanelId::Battery,
        Item::Media => PanelId::Media,
        Item::Brightness => PanelId::Brightness,
        Item::Time => PanelId::Time,
        Item::Date => PanelId::Date,
        Item::Logout | Item::ActiveWindow | Item::WorkspaceInfo => return None,
    })
}

pub fn item_for(panel: PanelId) -> Option<Item> {
    Item::ALL.into_iter().find(|&item| panel_for(item) == Some(panel))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bar_items_map_panels() {
        for item in [Item::Logout, Item::ActiveWindow, Item::WorkspaceInfo] {
            assert_eq!(panel_for(item), None, "{item:?} opens no panel");
        }
        assert_eq!(panel_for(Item::Tray), Some(PanelId::Tray));
        assert_eq!(panel_for(Item::Brightness), Some(PanelId::Brightness));
        for panel in PanelId::ALL {
            let item = item_for(panel).unwrap_or_else(|| panic!("{panel:?} has an item"));
            assert_eq!(panel_for(item), Some(panel));
        }
    }
}
