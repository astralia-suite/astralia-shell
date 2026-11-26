pub mod bar_layout;
pub mod file;
pub mod panel_config;
pub mod settings;
pub mod visualizer_config;
pub mod rain_config;
pub mod bamboo_config;
pub mod launcher_config;
pub mod logout_config;
pub mod notification_config;
pub mod osd_config;
pub mod overview_config;
pub mod settings_config;
pub mod wallpaper_config;
pub mod wallpaper_select_config;
pub mod lock_config;
pub mod polkit_config;

#[cfg(test)]
mod tests {
    #[test]
    fn eye_candy_constants() {
        assert_eq!(super::rain_config::FALL_INTERVAL_MS, 45.0);
        assert_eq!(super::bamboo_config::CULM_GLYPH, '│');
        assert_eq!(super::visualizer_config::SAMPLE_SIZE, 1024);
        assert_eq!(super::visualizer_config::FRAGMENT_SIZE, 4096);    }
}
