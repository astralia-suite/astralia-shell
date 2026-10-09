#include <cstdlib>
#include <filesystem>
#include <print>
#include <unistd.h>

#include "check.h"

void check_json();
void check_json_write();
void check_parse_invocation();
void check_runtime_path();
void check_format_help();
void check_config_file();
void check_path_home();
void check_reactor();
void check_animation();
void check_tokens();
void check_input();
void check_module_base();
void check_glyph_thresholds();
void check_async_process();
void check_deferred_call();
void check_status_changes();
void check_network_parse();
void check_media();
void check_tray();
void check_battery_parse();
void check_bluetooth();
void check_network_more();
void check_notification();
void check_user_and_icons();
void check_cpu_temp();
void check_gpu_temp();
void check_system_stats();
void check_config_parse();
void check_monitor_overrides();
void check_legacy_config();
void check_settings_service();
void check_wallpaper_resolve();
void check_wallpaper_images();
void check_dock();
void check_hyprland();
void check_i3();
void check_session();
void check_shell();
void check_spawn_helpers();
void check_launcher_text();
void check_launcher_modes();
void check_launcher_scoring();
void check_launcher_listing();
void check_desktop_entry();
void check_launch_urls();
void check_drun_results();
void check_visit_store();
void check_submenu();
void check_launcher_model();
void check_audio_percent();
void check_osd();
void check_bar();
void check_panel();
void check_notification_module();
void check_overview_paging();
void check_overview();
void check_logout_layout();
void check_logout();
void check_polkit_layout();
void check_polkit();
void check_settings();

int main() {
    char state_dir[] = "/tmp/astralia-test-XXXXXX";
    if (mkdtemp(state_dir) != nullptr) {
        setenv("XDG_STATE_HOME", state_dir, 1);
    }
    check_spawn_helpers();
    check_json();
    check_json_write();
    check_parse_invocation();
    check_runtime_path();
    check_format_help();
    check_config_file();
    check_path_home();
    check_reactor();
    check_animation();
    check_tokens();
    check_input();
    check_module_base();
    check_glyph_thresholds();
    check_async_process();
    check_deferred_call();
    check_status_changes();
    check_network_parse();
    check_media();
    check_tray();
    check_battery_parse();
    check_bluetooth();
    check_network_more();
    check_notification();
    check_user_and_icons();
    check_cpu_temp();
    check_gpu_temp();
    check_system_stats();
    check_config_parse();
    check_monitor_overrides();
    check_legacy_config();
    check_settings_service();
    check_wallpaper_resolve();
    check_wallpaper_images();
    check_dock();
    check_hyprland();
    check_i3();
    check_session();
    check_shell();
    check_launcher_text();
    check_launcher_modes();
    check_launcher_scoring();
    check_launcher_listing();
    check_desktop_entry();
    check_launch_urls();
    check_drun_results();
    check_visit_store();
    check_submenu();
    check_launcher_model();
    check_audio_percent();
    check_osd();
    check_bar();
    check_panel();
    check_notification_module();
    check_overview_paging();
    check_overview();
    check_logout_layout();
    check_logout();
    check_polkit_layout();
    check_polkit();
    check_settings();
    std::filesystem::remove_all(state_dir);
    if (test::failures > 0) {
        std::println(stderr, "{} check(s) failed", test::failures);
        return EXIT_FAILURE;
    }
    std::println("all checks passed");
    return EXIT_SUCCESS;
}
