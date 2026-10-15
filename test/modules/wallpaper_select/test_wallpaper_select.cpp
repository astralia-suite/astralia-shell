#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "check.h"
#include "core/pump.h"
#include "render/recording_canvas.h"

#include "config/wallpaper_select_config.h"

#include "core/animation.h"
#include "core/deferred_call.h"
#include "core/poll_reactor.h"

#include "modules/wallpaper_select/model.h"
#include "modules/wallpaper_select/view.h"

#include "service/wallpaper_service.h"

namespace {

namespace cfg = astralia::wallpaper_select_config;
using astralia::Config;
using astralia::WallpaperSelectModel;
using astralia::input::KeyEvent;
using astralia::input::KeyKind;

struct Instant {
    Instant() { astralia::animation_set_instant(true); }
    ~Instant() { astralia::animation_set_instant(false); }
};

struct Env {
    Config config;
    WallpaperSelectModel model;
    int updates = 0;
    int closed = 0;

    explicit Env(bool animated_wallpaper = false)
        : model({[this]() -> const Config & { return config; }, [this](const std::function<void(Config &)> &edit) {
                     edit(config);
                     ++updates; }},
                animated_wallpaper) {
        model.on_close_requested = [this] { model.close(); };
        model.on_closed = [this] { ++closed; };
    }

    void key(KeyKind kind) {
        KeyEvent event;
        event.kind = kind;
        model.key(event);
    }
};

struct Folder {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("astralia_test_wallpaper_select_" + std::to_string(getpid()));

    Folder() {
        std::filesystem::create_directories(root);
        std::ofstream(root / "c.png").put('x');
        std::ofstream(root / "a.png").put('x');
        std::ofstream(root / "b.jpg").put('x');
        std::ofstream(root / "notes.txt").put('x');
    }
    ~Folder() { std::filesystem::remove_all(root); }
    std::string file(const char *name) const { return (root / name).string(); }
};

bool opened(astralia::PollReactor &reactor, Env &env, const Folder &folder) {
    env.config.wallpaper_dir = folder.root.string();
    env.config.wallpaper_columns["DP-1"] = {folder.file("b.jpg")};
    env.model.open("DP-1", 0);
    return test::pump_until(reactor, [&] { return !env.model.scanning(); });
}

void check_listing(astralia::PollReactor &reactor) {
    Folder folder;
    Env env;
    test::check(opened(reactor, env, folder), "the scan finishes");
    const std::vector<std::string> &files = env.model.files();
    test::check(files.size() == 3 && files[0] == folder.file("b.jpg") && files[1] == folder.file("a.png") && files[2] == folder.file("c.png"), "images sort by extension then name and other files are skipped");
    test::check(env.model.is_open() && env.model.selected() == 0 && env.model.zoom() == 1.0f, "the current wallpaper starts selected and zoomed out");

    env.config.wallpaper_columns["DP-1"] = {folder.file("c.png")};
    env.model.close();
    env.model.open("DP-1", 0);
    test::check(test::pump_until(reactor, [&] { return !env.model.scanning(); }) && env.model.selected() == 2, "a wallpaper inside the folder is found by path");

    env.model.close();
    env.config.wallpaper_columns["DP-1"] = {"/elsewhere/z.png"};
    env.model.open("DP-1", 0);
    test::check(test::pump_until(reactor, [&] { return !env.model.scanning(); }) && env.model.files().size() == 4 && env.model.files().front() == "/elsewhere/z.png" && env.model.selected() == 0, "a wallpaper outside the folder is put in front");
}

void check_stepping(astralia::PollReactor &reactor) {
    Folder folder;
    Env env;
    opened(reactor, env, folder);
    env.model.step(1);
    test::check(env.model.selected() == 1 && env.model.position() == 1.0f, "stepping moves the selection and the scroller");
    env.model.step(10);
    test::check(env.model.selected() == 2, "stepping stops at the last image");
    env.model.step(-10);
    test::check(env.model.selected() == 0, "stepping stops at the first image");

    env.model.scroll(0.6f);
    test::check(env.model.selected() == 0, "a part of a wheel notch does not step");
    env.model.scroll(0.6f);
    test::check(env.model.selected() == 1, "wheel notches add up to a step");
    env.model.scroll(-1.2f);
    test::check(env.model.selected() == 0, "scrolling back steps back, the remainder carried over");
    env.model.scroll(2.0f * cfg::wheel_notch);
    test::check(env.model.selected() == 2, "a fast wheel steps more than once");

    env.key(KeyKind::Home);
    test::check(env.model.selected() == 0, "Home selects the first image");
    env.key(KeyKind::Right);
    test::check(env.model.selected() == 1, "Right steps forward");
    env.key(KeyKind::Left);
    test::check(env.model.selected() == 0, "Left steps back");
    env.key(KeyKind::End);
    test::check(env.model.selected() == 2, "End selects the last image");
}

void check_apply(astralia::PollReactor &reactor) {
    Folder folder;
    Env env;
    opened(reactor, env, folder);
    env.model.step(1);
    env.key(KeyKind::Enter);
    test::check(env.updates == 1 && astralia::wallpaper_column_override(env.config, "DP-1", 0, false) == folder.file("a.png"), "Enter sets the wallpaper of the opened column");
    test::check(!env.model.is_open() && env.closed == 1, "applying closes the selector");

    env.model.open("DP-1", 0);
    test::pump_until(reactor, [&] { return !env.model.scanning(); });
    env.key(KeyKind::Enter);
    test::check(env.updates == 1 && !env.model.is_open(), "applying the current wallpaper changes nothing");

    env.model.open("DP-1", 0);
    test::pump_until(reactor, [&] { return !env.model.scanning(); });
    env.key(KeyKind::End);
    env.key(KeyKind::Escape);
    test::check(env.updates == 1 && !env.model.is_open() && env.closed == 3, "Escape closes without a change");
}

void check_clicks(astralia::PollReactor &reactor) {
    Folder folder;
    Env env;
    opened(reactor, env, folder);
    env.model.begin_frame();
    env.model.add_card({0, 0, 10, 10}, 0);
    env.model.add_card({20, 0, 10, 10}, 1);
    test::check(env.model.clickable(25, 5) && !env.model.clickable(15, 5), "cards are clickable, the gap is not");
    env.model.click(25, 5);
    test::check(env.model.selected() == 1 && env.updates == 0, "clicking a side card selects it");
    env.model.click(25, 5);
    test::check(env.updates == 1 && !env.model.is_open(), "clicking the selected card applies it");

    opened(reactor, env, folder);
    env.model.begin_frame();
    env.model.add_card({0, 0, 10, 10}, 0);
    env.model.click(500, 500);
    test::check(!env.model.is_open() && env.updates == 1, "clicking outside the cards closes without a change");
}

void check_availability() {
    Env plain;
    test::check(plain.model.available(), "the selector is available");
    Env x11;
    x11.config.wallpaper_animated_enabled = true;
    test::check(x11.model.available(), "a backend without animated wallpapers ignores the setting");
}

void check_animated(astralia::PollReactor &reactor) {
    Folder folder;
    std::ofstream(folder.root / "y.mp4").put('x');
    std::ofstream(folder.root / "x.webm").put('x');
    Env env(true);
    env.config.wallpaper_animated_enabled = true;
    env.config.wallpaper_animated_dir = folder.root.string();
    env.config.wallpaper_animated_columns["DP-1"] = {"", folder.file("y.mp4")};
    env.config.wallpaper_animated_column_counts["DP-1"] = 2;
    env.model.open("DP-1", 1);
    test::check(test::pump_until(reactor, [&] { return !env.model.scanning(); }), "the animated scan finishes");
    test::check(env.model.animated() && env.model.column_count() == 2, "the animated column count is used");
    test::check(env.model.files().size() == 2 && env.model.files()[0] == folder.file("y.mp4") && env.model.files()[1] == folder.file("x.webm"), "only videos are listed, starting from the current one");
    env.model.step(1);
    env.key(KeyKind::Enter);
    test::check(astralia::wallpaper_column_override(env.config, "DP-1", 1, true) == folder.file("x.webm") && env.config.wallpaper_columns.empty(), "applying sets the animated column and leaves the static ones alone");
}

void check_view(astralia::PollReactor &reactor) {
    using Kind = test::Op::Kind;
    Folder folder;
    Env env;
    opened(reactor, env, folder);
    test::RecordingCanvas canvas;
    astralia::paint_wallpaper_select(canvas, env.model, 1920.0f, 1080.0f);
    test::check(canvas.count(Kind::begin_group) == 4, "the current wallpaper and its neighbours are drawn inside the column clip");
    float centre_w = 1920.0f * cfg::card_scale;
    bool shrunk = false;
    for (const test::Op &op : canvas.ops) {
        if (op.kind == Kind::begin_group && op.box.w < 1920.0f && op.box.w > centre_w - 8.0f) {
            shrunk = true;
        }
    }
    test::check(shrunk, "the current wallpaper is zoomed out to a card");
    test::check(env.model.clickable(960.0f, 500.0f), "the cards register for clicks");
    bool label = false;
    for (const test::Op &op : canvas.ops) {
        label = label || (op.kind == Kind::text && op.text.find("b.jpg") != std::string::npos);
    }
    test::check(label, "the file name is shown");
}

} // namespace

void check_wallpaper_select() {
    Instant instant;
    astralia::PollReactor reactor = std::move(*astralia::PollReactor::create());
    astralia::DeferredCall::attach(reactor);
    check_listing(reactor);
    check_stepping(reactor);
    check_apply(reactor);
    check_clicks(reactor);
    check_availability();
    check_animated(reactor);
    check_view(reactor);
}
