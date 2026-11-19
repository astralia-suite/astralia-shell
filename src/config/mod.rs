pub mod bar_layout;
pub mod file;
#[allow(dead_code)]
pub mod panel_config;
#[cfg_attr(not(test), allow(dead_code))]
pub mod settings;
#[allow(dead_code)]
pub mod visualizer_config;
#[allow(dead_code)]
pub mod rain_config;
#[allow(dead_code)]
pub mod bamboo_config;
#[allow(dead_code)]
pub mod idle_config;
#[allow(dead_code)]
pub mod launcher_config;
#[allow(dead_code)]
pub mod logout_config;
#[allow(dead_code)]
pub mod notification_config;
#[allow(dead_code)]
pub mod osd_config;
#[allow(dead_code)]
pub mod overview_config;
#[allow(dead_code)]
pub mod settings_config;
#[allow(dead_code)]
pub mod wallpaper_config;
#[allow(dead_code)]
pub mod wallpaper_select_config;
#[allow(dead_code)]
pub mod lock_config;
#[allow(dead_code)]
pub mod polkit_config;

#[cfg(test)]
mod tests {
    #[test]
    fn eye_candy_constants() {
        assert_eq!(super::rain_config::FALL_INTERVAL_MS, 45.0);
        assert_eq!(super::bamboo_config::CULM_GLYPH, '│');
        assert_eq!(super::visualizer_config::SAMPLE_SIZE, 1024);
        assert_eq!(super::visualizer_config::FRAGMENT_SIZE, 4096);
        assert_eq!(super::idle_config::IDLE_LAYER_NAMESPACE, "astralia-shell-idle-overlay");
    }
}
