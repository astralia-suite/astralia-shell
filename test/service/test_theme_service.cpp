#include <chrono>
#include <filesystem>
#include <fstream>

#include "check.h"
#include "palette_fixture.h"

#include "service/theme_service.h"

void check_theme_service() {
    using namespace astralia;
    using test::check;

    Config cfg;
    cfg.wallpaper_path = "/global.png";
    check(theme_wallpaper_path(cfg, "") == "/global.png", "the global wallpaper drives the theme without a monitor");
    cfg.wallpaper_path.clear();
    check(theme_wallpaper_path(cfg, "").empty(), "no wallpaper gives an empty path");
    cfg.wallpaper_columns["DP-1"] = {"", "/second.png"};
    cfg.wallpaper_columns["DP-2"] = {"/dp2.png"};
    check(theme_wallpaper_path(cfg, "") == "/dp2.png", "a column image beats the global one without a monitor");

    check(theme_wallpaper_path(cfg, "DP-1") == "", "a monitor with a blank column falls back to the blank global path");
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    check(theme_wallpaper_path(cfg, "DP-1") == "/dp1.png", "a monitor uses its own column image");
    cfg.wallpaper_columns.clear();
    std::filesystem::path file = std::filesystem::temp_directory_path() / "astralia-theme-source-test.png";
    std::ofstream(file) << "x";
    cfg.wallpaper_path = file.string();
    ThemeSource first = theme_source(cfg, "");
    check(first.path == file.string() && first.modified != 0, "a source records its path and mtime");
    check(first == theme_source(cfg, ""), "an unchanged file gives an equal source");
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
    check(!(first == theme_source(cfg, "")), "a changed mtime gives a different source");
    std::filesystem::remove(file);
    check(theme_source(cfg, "").modified == 0, "a missing file records no mtime");

    Extraction a{{200.0f, 0.1f, 0.7f}, std::nullopt, true};
    Extraction b{{200.2f, 0.1f, 0.7f}, std::nullopt, true};
    Extraction c{{250.0f, 0.1f, 0.7f}, std::nullopt, true};
    Extraction d{{200.0f, 0.1f, 0.7f}, Seed{300.0f, 0.1f, 0.7f}, true};
    check(!theme_extraction_differs(a, b), "a negligible hue shift is the same theme");
    check(theme_extraction_differs(a, c), "a different hue is a new theme");
    check(theme_extraction_differs(a, d), "a new secondary hue is a new theme");

    ThemeService service;
    int emitted = 0;
    service.changed.connect([&emitted] { ++emitted; });
    Config defaults;
    check(std::filesystem::exists(default_wallpaper_path()), "the default wallpaper asset exists");
    service.sync(defaults, "");
    check(emitted == 1, "the first sync applies the theme at once");
    service.sync(defaults, "");
    check(emitted == 1, "an unchanged source does nothing");
    test::apply_base_theme();
}
