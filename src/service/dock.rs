//! The windows of a monitor's active workspace, as the bar's dock shows them.

use crate::service::compositor::CompositorState;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct DockEntry {
    pub address: String,
    pub window_class: String,
    pub focused: bool,
}

/// Windows on `monitor`'s active workspace, left to right; the most recently focused one is marked.
pub fn dock_entries_for_monitor(state: &CompositorState, monitor: &str) -> Vec<DockEntry> {
    let Some(active) = state.by_monitor.get(monitor).map(|m| m.active_id).filter(|&id| id >= 0) else { return Vec::new() };
    let mut matched: Vec<_> = state.clients.iter().filter(|c| c.workspace_id == active).collect();
    matched.sort_by(|a, b| a.at[0].total_cmp(&b.at[0]));
    matched.into_iter().map(|c| DockEntry { address: c.address.clone(), window_class: c.window_class.clone(), focused: c.focus_history_id == 0 }).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::service::compositor::CompositorClient;

    fn client(address: &str, class: &str, workspace_id: i32, x: f64, focus_history_id: i64) -> CompositorClient {
        CompositorClient { address: address.into(), window_class: class.into(), workspace_id, at: [x, 0.0], focus_history_id, ..Default::default() }
    }

    #[test]
    fn dock_cases() {
        let mut state = CompositorState::default();
        state.by_monitor.entry("DP-1".into()).or_default().active_id = 2;
        state.by_monitor.entry("DP-2".into()).or_default().active_id = 5;
        state.clients = vec![client("0xaaa", "firefox", 2, 300.0, 1), client("0xbbb", "kitty", 2, 100.0, 0), client("0xccc", "mpv", 1, 0.0, 2)];

        let entries = dock_entries_for_monitor(&state, "DP-1");
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0], DockEntry { address: "0xbbb".into(), window_class: "kitty".into(), focused: true }, "left to right, focused first here");
        assert!(entries[1].address == "0xaaa" && !entries[1].focused);
        assert!(dock_entries_for_monitor(&state, "DP-2").is_empty(), "empty workspace");
        assert!(dock_entries_for_monitor(&state, "HDMI-1").is_empty(), "unknown output");
        state.clients[0].at = [50.0, 0.0];
        let reordered = dock_entries_for_monitor(&state, "DP-1");
        assert_eq!(reordered.iter().map(|e| e.address.as_str()).collect::<Vec<_>>(), ["0xaaa", "0xbbb"], "follows window positions");
    }
}
