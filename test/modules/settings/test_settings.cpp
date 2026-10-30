#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "config/settings_config.h"

#include "core/animation.h"
#include "core/deferred_call.h"
#include "core/path_home.h"
#include "core/poll_reactor.h"

#include "modules/settings/model.h"
#include "modules/settings/view.h"

#include "service/wallpaper_service.h"

#include "check.h"
#include "core/pump.h"
#include "render/recording_canvas.h"

namespace {

namespace cfg = astralia::settings_config;
using astralia::Config;
using astralia::wallpaper_column_count;
using astralia::wallpaper_column_override;
using astralia::wallpaper_fill_mode;
using astralia::WallpaperSlot;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;
using astralia::settings::Act;
using astralia::settings::Caps;
using astralia::settings::Field;
using astralia::settings::Model;
using astralia::settings::Tab;
using astralia::settings::WallpaperPicker;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

struct Env {
    Config config;
    std::vector<std::string> monitors{"DP-1", "HDMI-1"};
    astralia::settings::Model model;
    int updates = 0;

    explicit Env(astralia::settings::Caps caps = {})
        : model(std::move(caps), {[this]() -> const Config & { return config; },
                                  [this](const std::function<void(Config &)> &edit) {
                                      edit(config);
                                      ++updates;
                                  },
                                  [this] { return monitors; },
                                  [] { return std::string("HDMI-1"); },
                                  {}}) {}

    void click(astralia::settings::Act act, int a = 0, int b = 0) {
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
    astralia::settings::apply_field(config, astralia::settings::Field::ambient_timeout, "5", "");
    test::check(config.ambient_timeout == cfg::idle_timeout_min, "timeouts clamp to the minimum");
    astralia::settings::apply_field(config, astralia::settings::Field::ambient_timeout, "9999", "");
    test::check(config.ambient_timeout == cfg::idle_timeout_max, "and to the maximum");
    astralia::settings::apply_field(config, astralia::settings::Field::screensaver_timeout, "200",
                                    "DP-1");
    config.monitor_overrides["DP-1"].enabled = true;
    test::check(config.monitor_overrides["DP-1"].screensaver_timeout == 200 &&
                    config.screensaver_timeout == 300,
                "monitor fields write the override");
    astralia::settings::apply_field(config, astralia::settings::Field::visualizer_fps, "abc", "");
    test::check(config.visualizer.fps == 60, "unparsable text keeps the value");
    astralia::settings::apply_field(config, astralia::settings::Field::visualizer_thin, "2", "");
    test::check(config.visualizer.particle_thin == 0.95f, "floats clamp");
    astralia::settings::apply_field(config, astralia::settings::Field::wallpaper_dir, "/tmp/x", "");
    test::check(config.wallpaper_dir == "/tmp/x", "directories are stored");
    astralia::settings::apply_field(config, astralia::settings::Field::lock_wallpaper_dir,
                                    "/tmp/lock", "");
    test::check(config.lock_wallpaper_dir == "/tmp/lock" && config.wallpaper_dir == "/tmp/x" &&
                    astralia::settings::field_text(
                        config, astralia::settings::Field::lock_wallpaper_dir, "") == "/tmp/lock",
                "the lock directory is its own field");
    test::check(astralia::settings::field_text(config, astralia::settings::Field::visualizer_thin,
                                               "") == "0.95",
                "floats print without trailing zeros");
    test::check(astralia::settings::field_text(config, astralia::settings::Field::visualizer_fps,
                                               "") == "60",
                "integers print plainly");
    test::check(astralia::settings::field_text(
                    config, astralia::settings::Field::screensaver_timeout, "DP-1") == "200",
                "monitor values read the override");
}

void check_tabs() {
    Instant instant;
    Env minimal;
    std::vector<astralia::settings::Tab> order;
    for (const astralia::settings::TabInfo &info : minimal.model.tabs()) {
        order.push_back(info.tab);
    }
    test::check(order == std::vector<astralia::settings::Tab>{astralia::settings::Tab::bar,
                                                              astralia::settings::Tab::palette,
                                                              astralia::settings::Tab::displays,
                                                              astralia::settings::Tab::wallpaper},
                "a minimal backend has four tabs");

    astralia::settings::Caps full;
    full.animation = full.idle = full.rain = full.visualizer = true;
    Env rich(full);
    test::check(rich.model.tabs().size() == 6 &&
                    rich.model.tabs().front().tab == astralia::settings::Tab::general,
                "capabilities add the extra tabs in order");
    test::check(rich.model.tabs()[1].tab == astralia::settings::Tab::ambient,
                "the remaining tabs follow alphabetically after general");

    astralia::settings::Caps with_lock = full;
    with_lock.lock_wallpaper = true;
    Env locking(with_lock);
    std::vector<astralia::settings::Tab> locking_order;
    for (const astralia::settings::TabInfo &info : locking.model.tabs()) {
        locking_order.push_back(info.tab);
    }
    test::check(locking_order ==
                    std::vector<astralia::settings::Tab>{
                        astralia::settings::Tab::general, astralia::settings::Tab::ambient,
                        astralia::settings::Tab::bar, astralia::settings::Tab::palette,
                        astralia::settings::Tab::displays, astralia::settings::Tab::lock,
                        astralia::settings::Tab::wallpaper},
                "the lock screen tab sits between displays and wallpaper");
    astralia::settings::Caps lock_only;
    lock_only.lock_wallpaper = true;
    Env lock_minimal(lock_only);
    test::check(lock_minimal.model.tabs().size() == 5 &&
                    lock_minimal.model.tabs()[3].tab == astralia::settings::Tab::lock,
                "a backend that can lock gains the tab on its own");

    minimal.model.open();
    test::check(minimal.model.in_menu(), "the overlay opens on the tile menu");
    minimal.click(astralia::settings::Act::tab,
                  static_cast<int>(astralia::settings::Tab::displays));
    test::check(!minimal.model.in_menu(), "picking a tile opens its page");
    minimal.click(astralia::settings::Act::menu);
    test::check(minimal.model.in_menu(), "the back key returns to the menu");
    minimal.click(astralia::settings::Act::tab,
                  static_cast<int>(astralia::settings::Tab::displays));
    test::check(minimal.model.active_tab() == astralia::settings::Tab::displays &&
                    minimal.model.tab_alpha() == 1.0f,
                "a tab switch settles at once when instant");
    minimal.model.close();
    minimal.model.open();
    test::check(minimal.model.in_menu() &&
                    minimal.model.active_tab() == astralia::settings::Tab::wallpaper,
                "reopening starts over");

    Env keeper{astralia::settings::Caps{}};
    keeper.model.open();
    keeper.model.key(key(KeyKind::Down));
    test::check(keeper.model.in_menu(), "arrows do nothing on the menu");
    keeper.click(astralia::settings::Act::tab,
                 static_cast<int>(astralia::settings::Tab::wallpaper));
    keeper.model.key(key(KeyKind::Down));
    test::check(keeper.model.active_tab() == astralia::settings::Tab::wallpaper,
                "down stops at the last tab");
    keeper.model.key(key(KeyKind::Up));
    test::check(keeper.model.active_tab() == astralia::settings::Tab::displays,
                "up moves to the previous tab");
    keeper.model.key(key(KeyKind::Down));
    test::check(keeper.model.active_tab() == astralia::settings::Tab::wallpaper,
                "down moves to the next tab");
    keeper.click(astralia::settings::Act::tab, static_cast<int>(astralia::settings::Tab::palette));
    keeper.click(astralia::settings::Act::palette_source, 1);
    test::check(keeper.config.palette_source == astralia::PaletteSource::region_capture,
                "the palette source switches to region capture");
    keeper.click(astralia::settings::Act::palette_source, 0);
    test::check(keeper.config.palette_source == astralia::PaletteSource::default_palette,
                "the palette source switches back to the default");
    auto palette_texts = [&] {
        test::RecordingCanvas canvas;
        astralia::settings::Art art{"someone", "up 3 hours", {}};
        keeper.model.begin_frame();
        astralia::settings::paint(canvas, keeper.model, art, 1920, 1200);
        std::vector<std::string> texts;
        for (const test::Op &op : canvas.ops) {
            if (op.kind == test::Op::Kind::text) {
                texts.push_back(op.text);
            }
        }
        return texts;
    };
    std::vector<std::string> shown = palette_texts();
    test::check(std::ranges::find(shown, "Default") != shown.end() &&
                    std::ranges::find(shown, "Region Capture") == shown.end(),
                "the palette page paints and hides Region Capture on the default source");
    keeper.click(astralia::settings::Act::palette_source, 1);
    shown = palette_texts();
    test::check(std::ranges::find(shown, "Region Capture") != shown.end(),
                "Region Capture shows on the region source");
    keeper.click(astralia::settings::Act::palette_source, 0);

    int closes = 0;
    keeper.model.on_close_requested = [&] { ++closes; };
    keeper.model.key(key(KeyKind::Escape));
    test::check(closes == 0 && keeper.model.in_menu(), "escape on a page returns to the menu");
    keeper.model.key(key(KeyKind::Escape));
    test::check(closes == 1, "escape on the menu asks to close");
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
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(3));
    animated.click(astralia::settings::Act::tab, static_cast<int>(astralia::settings::Tab::bar));
    test::check(animated.model.active_tab() == astralia::settings::Tab::wallpaper &&
                    animated.model.animating(),
                "a tab switch fades out first");
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(4));
    animated.model.tick(std::chrono::steady_clock::now() + std::chrono::seconds(5));
    test::check(animated.model.active_tab() == astralia::settings::Tab::bar &&
                    animated.model.tab_alpha() == 1.0f,
                "then fades the new tab in");
}

void check_scroll_stages() {
    using Kind = test::Op::Kind;
    using std::chrono::seconds;
    using std::chrono::steady_clock;
    Env env{astralia::settings::Caps{}};
    int closed = 0;
    int closes = 0;
    env.model.on_closed = [&] { ++closed; };
    env.model.on_close_requested = [&] { ++closes; };
    astralia::settings::Art art{"someone", "up 3 hours", {}};

    env.model.open();
    test::check(env.model.is_open() && env.model.sequence().slide() == 1.0f &&
                    env.model.animating(),
                "the closed scroll starts below the screen");
    test::RecordingCanvas rising;
    astralia::settings::paint(rising, env.model, art, 1920, 1200);
    test::check(rising.count(Kind::rounded) > 0 && rising.count(Kind::text) == 0 &&
                    !env.model.clickable(960, 600),
                "the closed scroll shows no content");
    env.model.click(5, 5);
    test::check(closes == 0, "clicks are ignored before the scroll is open");

    env.model.tick(steady_clock::now() + seconds(1));
    env.model.tick(steady_clock::now() + seconds(2));
    test::check(env.model.sequence().settled() && env.model.sequence().grid() == 0.0f,
                "the scroll unrolls before the content");
    test::RecordingCanvas unrolled;
    astralia::settings::paint(unrolled, env.model, art, 1920, 1200);
    test::check(unrolled.count(Kind::text) == 0, "the open scroll is still empty");

    env.model.tick(steady_clock::now() + seconds(3));
    test::RecordingCanvas shown;
    astralia::settings::paint(shown, env.model, art, 1920, 1200);
    test::check(shown.count(Kind::text) > 0 && shown.count(Kind::begin_group) == 0,
                "then the content appears");
    env.model.click(5, 5);
    test::check(closes == 1, "and clicks work");

    env.model.close();
    test::check(env.model.is_open() && closed == 0,
                "closing keeps the overlay until the scroll has dropped");
    test::check(!env.model.key(key(KeyKind::Down)), "keys are ignored while closing");
    env.model.tick(steady_clock::now() + seconds(4));
    test::RecordingCanvas fading;
    astralia::settings::paint(fading, env.model, art, 1920, 1200);
    test::check(fading.count(Kind::text) == 0 && env.model.sequence().scroll() == 1.0f &&
                    closed == 0,
                "the content goes before the scroll rolls up");
    env.model.tick(steady_clock::now() + seconds(5));
    env.model.tick(steady_clock::now() + seconds(6));
    test::check(!env.model.is_open() && closed == 1, "the overlay closes once, after the drop");
}

void check_actions() {
    Instant instant;
    astralia::settings::Caps caps;
    caps.rain = caps.visualizer = caps.animation = caps.idle = true;
    Env env(caps);
    env.model.open();

    env.click(astralia::settings::Act::bar_style, 1);
    test::check(env.config.bar_style == astralia::BarStyle::okinami, "the bar style tile applies");
    astralia::settings::Caps limited;
    limited.bar_styles = {astralia::BarStyle::continuous, astralia::BarStyle::okinami};
    Env x11(limited);
    x11.config.bar_style = astralia::BarStyle::pop;
    test::check(x11.model.effective_bar_style() == astralia::BarStyle::continuous,
                "an unsupported style shows as continuous");

    env.click(astralia::settings::Act::toggle_osd);
    test::check(!env.config.default_osd, "default toggles flip the default");
    env.click(astralia::settings::Act::displays_monitor, 0);
    test::check(env.model.displays_monitor() == "DP-1", "monitor tiles select a monitor");
    env.click(astralia::settings::Act::toggle_override);
    test::check(env.config.monitor_overrides["DP-1"].enabled &&
                    !env.config.monitor_overrides["DP-1"].osd,
                "the override starts from the defaults");
    env.click(astralia::settings::Act::toggle_notifications);
    test::check(!env.config.monitor_overrides["DP-1"].notifications &&
                    env.config.default_notifications,
                "override toggles flip the override");
    env.click(astralia::settings::Act::toggle_override);
    test::check(!env.config.monitor_overrides["DP-1"].enabled, "the override switches off");
    env.click(astralia::settings::Act::displays_monitor, -1);
    test::check(env.model.displays_monitor().empty(), "the default tile clears the selection");

    env.click(astralia::settings::Act::idle_enable, 0, 1);
    test::check(!env.config.idle_enabled, "idle management flips");
    env.click(astralia::settings::Act::displays_monitor, 1);
    test::check(env.model.displays_monitor() == "HDMI-1", "idle shares the display selection");
    env.click(astralia::settings::Act::toggle_override, 0, 1);
    env.click(astralia::settings::Act::idle_ambient, 0, 1);
    test::check(!env.config.monitor_overrides["HDMI-1"].ambient_enabled &&
                    env.config.ambient_enabled,
                "idle toggles write the override");
    env.config.monitor_overrides["HDMI-1"].ambient_timeout = 20;
    env.click(astralia::settings::Act::idle_ambient_reset);
    test::check(env.config.monitor_overrides["HDMI-1"].ambient_timeout ==
                    env.config.ambient_timeout,
                "reset restores the default timeout");

    env.click(astralia::settings::Act::rain_mode, 1);
    env.click(astralia::settings::Act::rain_async);
    test::check(env.config.rain.mode == astralia::RainMode::Stiletto && env.config.rain.async_speed,
                "rain settings apply");
    env.click(astralia::settings::Act::visualizer_shape, 1);
    env.config.visualizer.fps = 30;
    env.click(astralia::settings::Act::visualizer_reset,
              static_cast<int>(astralia::settings::Field::visualizer_fps));
    test::check(env.config.visualizer.visualizer_shape == astralia::VisualizerShape::Sphere &&
                    env.config.visualizer.fps == 60,
                "visualizer settings apply and reset");
    env.click(astralia::settings::Act::animation_disable);
    test::check(env.config.animations_disabled, "single toggles flip");
    env.click(astralia::settings::Act::wallpaper_default);
    env.click(astralia::settings::Act::wallpaper_animated);
    test::check(!env.config.default_wallpaper && env.config.wallpaper_animated_enabled,
                "wallpaper mode toggles flip");
}

void check_wallpaper() {
    Instant instant;
    astralia::PollReactor reactor = std::move(*astralia::PollReactor::create());
    astralia::DeferredCall::attach(reactor);

    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("astralia_test_settings_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sub");
    std::ofstream(root / "b.png").put('x');
    std::ofstream(root / "a.jpg").put('x');
    std::ofstream(root / "notes.txt").put('x');
    std::ofstream(root / "sub" / "c.png").put('x');

    astralia::settings::Caps caps;
    caps.wallpaper_columns = true;
    Env env(caps);
    env.config.wallpaper_dir = root.string();
    env.model.open();
    env.click(astralia::settings::Act::tab, static_cast<int>(astralia::settings::Tab::wallpaper));
    test::check(env.model.picker(WallpaperSlot::still).scanning &&
                    env.model.picker(WallpaperSlot::still).region == "HDMI-1",
                "opening scans and picks the focused monitor");
    test::check(
        test::pump_until(reactor, [&] { return !env.model.picker(WallpaperSlot::still).scanning; }),
        "the scan finishes");
    const std::vector<std::string> &files = env.model.picker(WallpaperSlot::still).files;
    test::check(files.size() == 3, "images are found one folder deep, other files are skipped");
    test::check(files.size() == 3 && files[0].ends_with("a.jpg") && files[1].ends_with("b.png") &&
                    files[2].ends_with("c.png"),
                "files sort by extension then name");

    env.click(astralia::settings::Act::wallpaper_pick, 1, 0);
    test::check(astralia::wallpaper_column_override(env.config, "HDMI-1", 0, false) == files[1],
                "picking sets the column image");
    env.click(astralia::settings::Act::wallpaper_count, 1, 0);
    test::check(astralia::wallpaper_column_count(env.config, "HDMI-1", false) == 2,
                "the stepper adds a column");
    env.click(astralia::settings::Act::wallpaper_region, 0, 1);
    test::check(env.model.picker(WallpaperSlot::still).region == "DP-1" &&
                    env.model.picker(WallpaperSlot::still).column == 1,
                "chips select a region and column");
    env.click(astralia::settings::Act::wallpaper_fill, 1, 0);
    test::check(astralia::wallpaper_fill_mode(env.config, "DP-1", 1, false) == "fit",
                "fill mode applies");
    env.click(astralia::settings::Act::dropdown_toggle, 1);
    test::check(env.model.dropdown_open() == 1, "the drop-down opens");
    env.model.begin_frame();
    env.model.add_region({0, 0, 10, 10}, astralia::settings::Act::wallpaper_remove);
    env.model.add_region_front({0, 0, 10, 10}, astralia::settings::Act::capture_pick, 0, 1);
    env.model.click(5, 5);
    test::check(env.config.capture_monitor == "DP-1" && env.config.capture_column == 1 &&
                    env.model.dropdown_open() == -1,
                "an open list row wins over content beneath and closes the list");
    env.click(astralia::settings::Act::capture_pick, -1, 0);
    test::check(env.config.capture_monitor.empty(), "Auto clears region capture");
    env.click(astralia::settings::Act::dropdown_toggle, 1);
    env.click(astralia::settings::Act::dropdown_dismiss);
    test::check(env.model.dropdown_open() == -1, "dismiss closes the list");
    env.click(astralia::settings::Act::wallpaper_region, 1, 0);
    env.click(astralia::settings::Act::wallpaper_remove, 0, 0);
    test::check(astralia::wallpaper_column_override(env.config, "HDMI-1", 0, false).empty(),
                "remove clears the column");
    env.click(astralia::settings::Act::wallpaper_count, -1, 0);
    env.click(astralia::settings::Act::wallpaper_count, -1, 0);
    test::check(astralia::wallpaper_column_count(env.config, "HDMI-1", false) == 1,
                "the stepper stops at one");

    env.click(astralia::settings::Act::field_focus,
              static_cast<int>(astralia::settings::Field::wallpaper_dir));
    test::check(env.model.focused() == astralia::settings::Field::wallpaper_dir &&
                    env.model.field().text == astralia::path_collapse_home(root.string()),
                "focusing a field loads its text");
    for (int i = 0; i < 3; ++i) {
        env.model.key(key(KeyKind::Backspace));
    }
    env.model.key(key(KeyKind::Text, "x"));
    test::check(env.model.field().text.back() == 'x', "typing edits the buffer");
    env.model.key(key(KeyKind::Escape));
    test::check(env.model.focused() == astralia::settings::Field::none &&
                    env.config.wallpaper_dir == root.string(),
                "escape cancels the edit");
    env.click(astralia::settings::Act::field_focus,
              static_cast<int>(astralia::settings::Field::wallpaper_dir));
    env.model.commit_text("/");
    env.model.key(key(KeyKind::Enter));
    test::check(env.model.focused() == astralia::settings::Field::none &&
                    env.config.wallpaper_dir == root.string() + "/",
                "enter commits the edit");

    env.model.close();
    test::check(env.model.picker(WallpaperSlot::still).files.empty(),
                "closing drops the scanned files");
    std::filesystem::remove_all(root);
}

void check_lock_wallpaper() {
    Instant instant;
    astralia::PollReactor reactor = std::move(*astralia::PollReactor::create());
    astralia::DeferredCall::attach(reactor);

    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("astralia_test_settings_lock_" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    std::ofstream(root / "b.png").put('x');
    std::ofstream(root / "a.jpg").put('x');

    constexpr int lock_flag = static_cast<int>(WallpaperSlot::lock);
    astralia::settings::Caps caps;
    caps.wallpaper_columns = true;
    caps.lock_wallpaper = true;
    Env env(caps);
    env.config.lock_wallpaper_dir = root.string();
    env.model.open();
    env.click(astralia::settings::Act::tab, static_cast<int>(astralia::settings::Tab::lock));
    astralia::settings::WallpaperPicker &picker = env.model.picker(WallpaperSlot::lock);
    test::check(!picker.scanning && picker.dir.empty() && picker.files.empty(),
                "Auto mode loads nothing for the lock screen");

    env.click(astralia::settings::Act::lock_wallpaper_mode, 1);
    test::check(env.config.lock_wallpaper_custom && picker.scanning && picker.region == "HDMI-1",
                "Custom mode scans the lock directory");
    test::check(test::pump_until(reactor, [&] { return !picker.scanning; }),
                "the lock scan finishes");
    test::check(picker.files.size() == 2 && picker.files[0].ends_with("a.jpg") &&
                    picker.files[1].ends_with("b.png"),
                "the lock picker lists its own directory");
    test::check(!env.model.picker(WallpaperSlot::still).scanning &&
                    env.model.picker(WallpaperSlot::still).files.empty(),
                "the desktop picker is not touched");

    env.click(astralia::settings::Act::wallpaper_pick, 1, lock_flag);
    test::check(
        wallpaper_column_override(env.config, "HDMI-1", 0, WallpaperSlot::lock) ==
                picker.files[1] &&
            wallpaper_column_override(env.config, "HDMI-1", 0, WallpaperSlot::still).empty(),
        "picking sets only the lock column");
    env.click(astralia::settings::Act::wallpaper_count, 1, lock_flag);
    test::check(wallpaper_column_count(env.config, "HDMI-1", WallpaperSlot::lock) == 2 &&
                    wallpaper_column_count(env.config, "HDMI-1", WallpaperSlot::still) == 1,
                "the stepper changes only the lock count");
    env.click(astralia::settings::Act::wallpaper_region, 0, 1 | (lock_flag << 8));
    test::check(picker.region == "DP-1" && picker.column == 1 &&
                    env.model.picker(WallpaperSlot::still).region == "HDMI-1" &&
                    env.model.picker(WallpaperSlot::still).column == 0,
                "chips move only the lock selection");
    env.click(astralia::settings::Act::wallpaper_fill, 1, lock_flag);
    test::check(wallpaper_fill_mode(env.config, "DP-1", 1, WallpaperSlot::lock) == "fit" &&
                    wallpaper_fill_mode(env.config, "DP-1", 1, WallpaperSlot::still) == "crop",
                "fill mode applies to the lock slot only");
    env.click(astralia::settings::Act::wallpaper_region, 1, 0 | (lock_flag << 8));
    env.click(astralia::settings::Act::wallpaper_remove, 0, lock_flag);
    test::check(wallpaper_column_override(env.config, "HDMI-1", 0, WallpaperSlot::lock).empty(),
                "remove clears the lock column");

    env.click(astralia::settings::Act::field_focus,
              static_cast<int>(astralia::settings::Field::lock_wallpaper_dir));
    test::check(env.model.focused() == astralia::settings::Field::lock_wallpaper_dir &&
                    env.model.field().text == astralia::path_collapse_home(root.string()),
                "the lock directory field loads its text");
    env.model.commit_text("/");
    env.model.key(key(KeyKind::Enter));
    test::check(env.config.lock_wallpaper_dir == root.string() + "/",
                "enter commits the lock directory");

    env.click(astralia::settings::Act::lock_wallpaper_mode, 0);
    test::check(!env.config.lock_wallpaper_custom, "Auto mode switches back");

    env.model.close();
    test::check(picker.files.empty() && picker.dir.empty() && !picker.scanning,
                "closing clears the lock picker");
    std::filesystem::remove_all(root);
}

void check_lock_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    astralia::settings::Caps caps;
    caps.lock_wallpaper = true;
    caps.wallpaper_columns = true;
    Env env(caps);
    env.model.open();
    env.click(astralia::settings::Act::tab, static_cast<int>(astralia::settings::Tab::lock));
    astralia::settings::Art art{"someone", "up 3 hours", {}};

    auto paint_texts = [&] {
        test::RecordingCanvas canvas;
        env.model.begin_frame();
        astralia::settings::paint(canvas, env.model, art, 1920, 1200);
        std::vector<std::string> texts;
        for (const test::Op &op : canvas.ops) {
            if (op.kind == Kind::text) {
                texts.push_back(op.text);
            }
        }
        return texts;
    };
    auto has = [](const std::vector<std::string> &texts, const std::string &wanted) {
        return std::ranges::find(texts, wanted) != texts.end();
    };

    std::vector<std::string> automatic = paint_texts();
    test::check(has(automatic, "Auto") && has(automatic, "Custom") && !has(automatic, "Display"),
                "Auto mode shows only the selector");
    env.click(astralia::settings::Act::lock_wallpaper_mode, 1);
    std::vector<std::string> custom = paint_texts();
    test::check(has(custom, "Display") && has(custom, "Dir") && has(custom, "Crop"),
                "Custom mode shows the column controls");
}

void check_view() {
    using Kind = test::Op::Kind;
    Instant instant;
    astralia::settings::Caps caps;
    Env env(caps);
    env.model.open();
    test::RecordingCanvas canvas;
    astralia::settings::Art art{"someone", "up 3 hours", {}};
    astralia::settings::Frame frame = astralia::settings::paint(canvas, env.model, art, 1920, 1200);
    test::check(frame.panel.w == cfg::card_max_width && frame.panel.h == cfg::card_max_height,
                "a large output caps the card");
    test::check(frame.panel.x == (1920 - cfg::card_max_width) / 2, "the card is centered");

    test::RecordingCanvas small;
    astralia::settings::Frame narrow = astralia::settings::paint(small, env.model, art, 600, 400);
    test::check(narrow.panel.w == 520 && narrow.panel.h == 320, "a small output leaves a margin");
    bool name = false;
    for (const test::Op &op : canvas.ops) {
        name = name || (op.kind == Kind::text && op.text == "someone");
    }
    test::check(name, "the menu shows the user name");
    bool collapsed_name = false;
    for (const test::Op &op : small.ops) {
        collapsed_name = collapsed_name || (op.kind == Kind::text && op.text == "someone");
    }
    test::check(collapsed_name, "also on a small output");

    int closes = 0;
    env.model.on_close_requested = [&] { ++closes; };
    astralia::settings::paint(canvas, env.model, art, 1920, 1200);
    float close_x =
        frame.panel.x + frame.panel.w - cfg::card_padding - cfg::action_button_size / 2.0f;
    float close_y = frame.panel.y + cfg::card_padding + cfg::header_height / 2.0f;
    test::check(env.model.clickable(close_x, close_y), "the close button is clickable");
    env.model.click(close_x, close_y);
    test::check(closes == 1, "and closes");

    float tile_x = frame.panel.x + cfg::card_padding + 20.0f;
    bool found_tab = false;
    for (float y = frame.panel.y; y < frame.panel.y + frame.panel.h; y += 4.0f) {
        env.model.begin_frame();
        astralia::settings::paint(canvas, env.model, art, 1920, 1200);
        if (env.model.clickable(tile_x, y) && y > frame.panel.y + 150.0f) {
            env.model.click(tile_x, y);
            found_tab = env.model.active_tab() == astralia::settings::Tab::bar ||
                        env.model.active_tab() == astralia::settings::Tab::wallpaper ||
                        env.model.active_tab() == astralia::settings::Tab::palette ||
                        env.model.active_tab() == astralia::settings::Tab::displays;
            break;
        }
    }
    test::check(found_tab, "menu tiles are clickable");
}

} // namespace

void check_settings() {
    check_fields();
    check_tabs();
    check_tab_fade();
    check_scroll_stages();
    check_actions();
    check_wallpaper();
    check_lock_wallpaper();
    check_view();
    check_lock_view();
}
