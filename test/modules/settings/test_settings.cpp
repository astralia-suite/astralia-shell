#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "check.h"
#include "core/pump.h"
#include "ui/recording_canvas.h"

#include "config/settings_config.h"

#include "core/animation.h"
#include "core/deferred_call.h"
#include "core/path_home.h"
#include "core/poll_reactor.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "service/wallpaper_service.h"

namespace {

namespace cfg = astralia::settings_config;
using astralia::Config;
using astralia::SettingsAct;
using astralia::SettingsCaps;
using astralia::SettingsField;
using astralia::SettingsModel;
using astralia::SettingsTab;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

struct Env {
    Config config;
    std::vector<std::string> monitors{"DP-1", "HDMI-1"};
    SettingsModel model;
    int updates = 0;

    explicit Env(SettingsCaps caps = {})
        : model(std::move(caps), {[this]() -> const Config & { return config; }, [this](const std::function<void(Config &)> &edit) {
                                      edit(config);
                                      ++updates; }, [this] { return monitors; }, [] { return std::string("HDMI-1"); }, {}}) {}

    void click(SettingsAct act, int a = 0, int b = 0) {
        model.begin_frame();
        model.add_region({0, 0, 10, 10}, act, a, b);
        model.click(5, 5);
    }
};

KeyEvent key(KeyKind kind, std::string text = "") {
    KeyEvent event;
    event.kind = kind;
    event.text = std::move(text);
    return event;
}

void check_fields() {
    Config config;
    astralia::settings_apply_field(config, SettingsField::ambient_timeout, "5", "");
    test::check(config.ambient_timeout_seconds == cfg::idle_timeout_min, "timeouts clamp to the minimum");
    astralia::settings_apply_field(config, SettingsField::ambient_timeout, "9999", "");
    test::check(config.ambient_timeout_seconds == cfg::idle_timeout_max, "and to the maximum");
    astralia::settings_apply_field(config, SettingsField::screensaver_timeout, "200", "DP-1");
    config.monitor_overrides["DP-1"].enabled = true;
    test::check(config.monitor_overrides["DP-1"].screensaver_timeout_seconds == 200 && config.screensaver_timeout_seconds == 300, "monitor fields write the override");
    astralia::settings_apply_field(config, SettingsField::visualizer_fps, "abc", "");
    test::check(config.visualizer.fps == 60, "unparsable text keeps the value");
    astralia::settings_apply_field(config, SettingsField::visualizer_thin, "2", "");
    test::check(config.visualizer.particle_thin == 0.95f, "floats clamp");
    astralia::settings_apply_field(config, SettingsField::wallpaper_dir, "/tmp/x", "");
    test::check(config.wallpaper_dir == "/tmp/x", "directories are stored");
    test::check(astralia::settings_field_text(config, SettingsField::visualizer_thin, "") == "0.95", "floats print without trailing zeros");
    test::check(astralia::settings_field_text(config, SettingsField::visualizer_fps, "") == "60", "integers print plainly");
    test::check(astralia::settings_field_text(config, SettingsField::screensaver_timeout, "DP-1") == "200", "monitor values read the override");
}

void check_tabs() {
    Instant instant;
    Env minimal;
    std::vector<SettingsTab> order;
    for (const astralia::SettingsTabInfo &info : minimal.model.tabs()) {
        order.push_back(info.tab);
    }
    test::check(order == std::vector<SettingsTab>{SettingsTab::bar, SettingsTab::displays, SettingsTab::wallpaper}, "a minimal backend has three tabs");

    SettingsCaps full;
    full.animation = full.idle = full.logout = full.rain = full.visualizer = true;
    Env rich(full);
    test::check(rich.model.tabs().size() == 8 && rich.model.tabs().front().tab == SettingsTab::animation, "capabilities add the extra tabs in order");

    minimal.model.open();
    test::check(minimal.model.active_tab() == SettingsTab::wallpaper, "the overlay opens on the wallpaper tab");
    minimal.click(SettingsAct::tab, static_cast<int>(SettingsTab::displays));
    test::check(minimal.model.active_tab() == SettingsTab::displays && minimal.model.tab_alpha() == 1.0f, "a tab switch settles at once when instant");
    minimal.model.close();
    minimal.model.open();
    test::check(minimal.model.active_tab() == SettingsTab::wallpaper, "reopening starts over");

    SettingsCaps remembering;
    remembering.remember_tab = true;
    Env keeper(remembering);
    keeper.model.open();
    test::check(keeper.model.active_tab() == SettingsTab::displays, "a remembering backend starts on displays");
    keeper.model.key(key(KeyKind::Down));
    test::check(keeper.model.active_tab() == SettingsTab::wallpaper, "down moves to the next tab");
    keeper.model.key(key(KeyKind::Down));
    test::check(keeper.model.active_tab() == SettingsTab::wallpaper, "and stops at the last");
    keeper.model.key(key(KeyKind::Up));
    keeper.model.close();
    keeper.model.open();
    test::check(keeper.model.active_tab() == SettingsTab::displays, "the tab is remembered");

    int closes = 0;
    keeper.model.on_close_requested = [&] { ++closes; };
    keeper.model.key(key(KeyKind::Escape));
    test::check(closes == 1, "escape asks to close");
    keeper.model.begin_frame();
    keeper.model.set_panel({100, 100, 200, 200});
    keeper.model.click(10, 10);
    test::check(closes == 2, "a click outside the card asks to close");
    keeper.model.click(150, 150);
    test::check(closes == 2, "a click inside the card does not");
}

void check_tab_fade() {
    Env animated;
    animated.model.open();
    animated.click(SettingsAct::tab, static_cast<int>(SettingsTab::bar));
    test::check(animated.model.active_tab() == SettingsTab::wallpaper && animated.model.animating(), "a tab switch fades out first");
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    test::check(animated.model.active_tab() == SettingsTab::bar && animated.model.tab_alpha() == 1.0f, "then fades the new tab in");
}

void check_actions() {
    Instant instant;
    SettingsCaps caps;
    caps.rain = caps.visualizer = caps.animation = caps.logout = caps.idle = true;
    Env env(caps);
    env.model.open();

    env.click(SettingsAct::bar_style, 1);
    test::check(env.config.bar_style == astralia::BarStyle::okinami, "the bar style tile applies");
    SettingsCaps limited;
    limited.bar_styles = {astralia::BarStyle::continuous, astralia::BarStyle::okinami};
    Env x11(limited);
    x11.config.bar_style = astralia::BarStyle::islands;
    test::check(x11.model.effective_bar_style() == astralia::BarStyle::continuous, "an unsupported style shows as continuous");

    env.click(SettingsAct::toggle_osd);
    test::check(!env.config.default_osd_enabled, "default toggles flip the default");
    env.click(SettingsAct::displays_monitor, 0);
    test::check(env.model.displays_monitor() == "DP-1", "monitor tiles select a monitor");
    env.click(SettingsAct::toggle_override);
    test::check(env.config.monitor_overrides["DP-1"].enabled && !env.config.monitor_overrides["DP-1"].osd, "the override starts from the defaults");
    env.click(SettingsAct::toggle_notifications);
    test::check(!env.config.monitor_overrides["DP-1"].notifications && env.config.default_notifications_enabled, "override toggles flip the override");
    env.click(SettingsAct::toggle_override);
    test::check(!env.config.monitor_overrides["DP-1"].enabled, "the override switches off");
    env.click(SettingsAct::displays_monitor, -1);
    test::check(env.model.displays_monitor().empty(), "the default tile clears the selection");

    env.click(SettingsAct::idle_enable, 0, 1);
    test::check(!env.config.idle_management_enabled, "idle management flips");
    env.click(SettingsAct::idle_monitor, 1);
    test::check(env.model.idle_monitor() == "HDMI-1", "idle monitors are chosen separately");
    env.click(SettingsAct::toggle_override, 0, 1);
    env.click(SettingsAct::idle_ambient, 0, 1);
    test::check(!env.config.monitor_overrides["HDMI-1"].ambient_enabled && env.config.ambient_enabled, "idle toggles write the override");
    env.config.monitor_overrides["HDMI-1"].ambient_timeout_seconds = 20;
    env.click(SettingsAct::idle_ambient_reset);
    test::check(env.config.monitor_overrides["HDMI-1"].ambient_timeout_seconds == env.config.ambient_timeout_seconds, "reset restores the default timeout");

    env.click(SettingsAct::rain_mode, 1);
    env.click(SettingsAct::rain_async);
    test::check(env.config.rain.mode == astralia::RainMode::Stiletto && env.config.rain.async_speed, "rain settings apply");
    env.click(SettingsAct::visualizer_shape, 1);
    env.config.visualizer.fps = 30;
    env.click(SettingsAct::visualizer_reset, static_cast<int>(SettingsField::visualizer_fps));
    test::check(env.config.visualizer.visualizer_shape == astralia::VisualizerShape::Sphere && env.config.visualizer.fps == 60, "visualizer settings apply and reset");
    env.click(SettingsAct::animation_disable);
    env.click(SettingsAct::logout_logo);
    test::check(env.config.animations_disabled && !env.config.logout_animated_logo, "single toggles flip");
    env.click(SettingsAct::wallpaper_default);
    env.click(SettingsAct::wallpaper_animated);
    test::check(!env.config.default_wallpaper_enabled && env.config.wallpaper_animated_enabled, "wallpaper mode toggles flip");
}

void check_wallpaper() {
    Instant instant;
    astralia::PollReactor reactor = std::move(*astralia::PollReactor::create());
    astralia::DeferredCall::attach(reactor);

    std::filesystem::path root = std::filesystem::temp_directory_path() / ("astralia_test_settings_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sub");
    std::ofstream(root / "b.png").put('x');
    std::ofstream(root / "a.jpg").put('x');
    std::ofstream(root / "notes.txt").put('x');
    std::ofstream(root / "sub" / "c.png").put('x');

    SettingsCaps caps;
    caps.wallpaper_columns = true;
    Env env(caps);
    env.config.wallpaper_dir = root.string();
    env.model.open();
    test::check(env.model.picker(false).scanning && env.model.picker(false).region == "HDMI-1", "opening scans and picks the focused monitor");
    test::check(test::pump_until(reactor, [&] { return !env.model.picker(false).scanning; }), "the scan finishes");
    const std::vector<std::string> &files = env.model.picker(false).files;
    test::check(files.size() == 3, "images are found one folder deep, other files are skipped");
    test::check(files.size() == 3 && files[0].ends_with("a.jpg") && files[1].ends_with("b.png") && files[2].ends_with("c.png"), "files sort by extension then name");

    env.click(SettingsAct::wallpaper_pick, 1, 0);
    test::check(astralia::wallpaper_column_override(env.config, "HDMI-1", 0, false) == files[1], "picking sets the column image");
    env.click(SettingsAct::wallpaper_count, 1, 0);
    test::check(astralia::wallpaper_column_count(env.config, "HDMI-1", false) == 2, "the stepper adds a column");
    env.click(SettingsAct::wallpaper_region, 0, 1);
    test::check(env.model.picker(false).region == "DP-1" && env.model.picker(false).column == 1, "chips select a region and column");
    env.click(SettingsAct::wallpaper_fill, 1, 0);
    test::check(astralia::wallpaper_fill_mode(env.config, "DP-1", 1, false) == "fit", "fill mode applies");
    env.click(SettingsAct::wallpaper_region, 1, 0);
    env.click(SettingsAct::wallpaper_remove, 0, 0);
    test::check(astralia::wallpaper_column_override(env.config, "HDMI-1", 0, false).empty(), "remove clears the column");
    env.click(SettingsAct::wallpaper_count, -1, 0);
    env.click(SettingsAct::wallpaper_count, -1, 0);
    test::check(astralia::wallpaper_column_count(env.config, "HDMI-1", false) == 1, "the stepper stops at one");

    env.click(SettingsAct::field_focus, static_cast<int>(SettingsField::wallpaper_dir));
    test::check(env.model.focused() == SettingsField::wallpaper_dir && env.model.field().text == astralia::path_collapse_home(root.string()), "focusing a field loads its text");
    for (int i = 0; i < 3; ++i) {
        env.model.key(key(KeyKind::Backspace));
    }
    env.model.key(key(KeyKind::Text, "x"));
    test::check(env.model.field().text.back() == 'x', "typing edits the buffer");
    env.model.key(key(KeyKind::Escape));
    test::check(env.model.focused() == SettingsField::none && env.config.wallpaper_dir == root.string(), "escape cancels the edit");
    env.click(SettingsAct::field_focus, static_cast<int>(SettingsField::wallpaper_dir));
    env.model.commit_text("/");
    env.model.key(key(KeyKind::Enter));
    test::check(env.model.focused() == SettingsField::none && env.config.wallpaper_dir == root.string() + "/", "enter commits the edit");

    env.model.close();
    test::check(env.model.picker(false).files.empty(), "closing drops the scanned files");
    std::filesystem::remove_all(root);
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    SettingsCaps caps;
    caps.remember_tab = true;
    Env env(caps);
    env.model.open();
    test::RecordingCanvas canvas;
    astralia::SettingsArt art{"someone", "up 3 hours", {}};
    astralia::SettingsFrame frame = astralia::paint_settings(canvas, env.model, art, 1920, 1200);
    test::check(frame.panel.w == cfg::card_max_width && frame.panel.h == cfg::card_max_height, "a large output caps the card");
    test::check(frame.panel.x == (1920 - cfg::card_max_width) / 2, "the card is centered");

    test::RecordingCanvas small;
    astralia::SettingsFrame narrow = astralia::paint_settings(small, env.model, art, 600, 400);
    test::check(narrow.panel.w == 520 && narrow.panel.h == 320, "a small output leaves a margin");
    bool name = false;
    for (const test::Op &op : canvas.ops) {
        name = name || (op.kind == Kind::text && op.text == "someone");
    }
    test::check(name, "the expanded rail shows the user name");
    bool collapsed_name = false;
    for (const test::Op &op : small.ops) {
        collapsed_name = collapsed_name || (op.kind == Kind::text && op.text == "someone");
    }
    test::check(!collapsed_name, "the collapsed rail hides it");

    int closes = 0;
    env.model.on_close_requested = [&] { ++closes; };
    astralia::paint_settings(canvas, env.model, art, 1920, 1200);
    float close_x = frame.panel.x + frame.panel.w - cfg::card_padding - cfg::action_button_size / 2.0f;
    float close_y = frame.panel.y + cfg::card_padding + cfg::header_height / 2.0f;
    test::check(env.model.clickable(close_x, close_y), "the close button is clickable");
    env.model.click(close_x, close_y);
    test::check(closes == 1, "and closes");

    float rail_x = frame.panel.x + cfg::card_padding + 20.0f;
    bool found_tab = false;
    for (float y = frame.panel.y; y < frame.panel.y + frame.panel.h; y += 4.0f) {
        env.model.begin_frame();
        astralia::paint_settings(canvas, env.model, art, 1920, 1200);
        if (env.model.clickable(rail_x, y) && y > frame.panel.y + 150.0f) {
            env.model.click(rail_x, y);
            found_tab = env.model.active_tab() == SettingsTab::bar || env.model.active_tab() == SettingsTab::wallpaper || env.model.active_tab() == SettingsTab::displays;
            break;
        }
    }
    test::check(found_tab, "rail items are clickable");
}

} // namespace

void check_settings() {
    check_fields();
    check_tabs();
    check_tab_fade();
    check_actions();
    check_wallpaper();
    check_view();
}
