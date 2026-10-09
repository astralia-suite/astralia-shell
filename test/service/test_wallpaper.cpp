#include <string>

#include "service/wallpaper_service.h"

#include "check.h"

void check_wallpaper_resolve() {
    using namespace astralia;
    using test::check;
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    cfg.wallpaper_fill_modes["DP-1"] = {"fit", "tile"};

    check(wallpaper_column_path(cfg, "DP-1", 0, false) == "/dp1.png", "wallpaper_column_path(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 0, false) == "/global.png", "wallpaper_column_path(cfg, \"HDMI-1\", 0, false) == \"/global.png\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 0, false) == "fit", "wallpaper_fill_mode(cfg, \"DP-1\", 0, false) == \"fit\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 1, false) == "tile", "wallpaper_fill_mode(cfg, \"DP-1\", 1, false) == \"tile\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 2, false) == "crop", "wallpaper_fill_mode(cfg, \"DP-1\", 2, false) == \"crop\"");
    check(wallpaper_fill_mode(cfg, "HDMI-1", 0, false) == "crop", "wallpaper_fill_mode(cfg, \"HDMI-1\", 0, false) == \"crop\"");

    check(wallpaper_column_path(cfg, "DP-1", 1, false) == "/global.png", "wallpaper_column_path(cfg, \"DP-1\", 1, false) == \"/global.png\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 3, false) == "/global.png", "wallpaper_column_path(cfg, \"HDMI-1\", 3, false) == \"/global.png\"");
    check(wallpaper_column_count(cfg, "DP-1", false) == 1, "wallpaper_column_count(cfg, \"DP-1\", false) == 1");
    cfg.wallpaper_column_counts["DP-1"] = 2;
    check(wallpaper_column_count(cfg, "DP-1", false) == 2, "wallpaper_column_count(cfg, \"DP-1\", false) == 2");

    cfg.default_wallpaper_enabled = false;
    check(wallpaper_column_path(cfg, "HDMI-1", 0, false).empty(), "wallpaper_column_path(cfg, \"HDMI-1\", 0, false).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 1, false).empty(), "wallpaper_column_path(cfg, \"DP-1\", 1, false).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 0, false) == "/dp1.png", "wallpaper_column_path(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");

    check(wallpaper_column_override(cfg, "HDMI-1", 0, false).empty(), "wallpaper_column_override(cfg, \"HDMI-1\", 0, false).empty()");
    check(wallpaper_column_override(cfg, "DP-1", 0, false) == "/dp1.png", "wallpaper_column_override(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");
    cfg.default_wallpaper_enabled = true;
    check(wallpaper_column_override(cfg, "HDMI-1", 0, false).empty(), "wallpaper_column_override(cfg, \"HDMI-1\", 0, false).empty()");

    cfg.wallpaper_animated_columns["DP-1"] = {"/dp1.mp4"};
    cfg.wallpaper_animated_fill_modes["DP-1"] = {"fit"};
    check(wallpaper_column_path(cfg, "DP-1", 0, true) == "/dp1.mp4", "wallpaper_column_path(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 0, true) == "/global.png", "wallpaper_column_path(cfg, \"HDMI-1\", 0, true) == \"/global.png\"");
    check(wallpaper_column_override(cfg, "HDMI-1", 0, true).empty(), "wallpaper_column_override(cfg, \"HDMI-1\", 0, true).empty()");
    check(wallpaper_column_override(cfg, "DP-1", 0, true) == "/dp1.mp4", "wallpaper_column_override(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 0, true) == "fit", "wallpaper_fill_mode(cfg, \"DP-1\", 0, true) == \"fit\"");
    check(wallpaper_fill_mode(cfg, "HDMI-1", 0, true) == "crop", "wallpaper_fill_mode(cfg, \"HDMI-1\", 0, true) == \"crop\"");
    check(wallpaper_column_count(cfg, "DP-1", true) == 1, "wallpaper_column_count(cfg, \"DP-1\", true) == 1");
    cfg.wallpaper_animated_column_counts["DP-1"] = 3;
    check(wallpaper_column_count(cfg, "DP-1", true) == 3, "wallpaper_column_count(cfg, \"DP-1\", true) == 3");

    cfg.default_wallpaper_enabled = false;
    check(wallpaper_column_path(cfg, "HDMI-1", 0, true).empty(), "wallpaper_column_path(cfg, \"HDMI-1\", 0, true).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 0, true) == "/dp1.mp4", "wallpaper_column_path(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
}

void check_wallpaper_images() {
    using namespace astralia;
    using test::check;
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    check(wallpaper_image_for(cfg, "eDP-1") == "/global.png", "an output without an image uses the global one");
    wallpaper_set_image(cfg, "eDP-1", "/mine.png");
    check(wallpaper_image_for(cfg, "eDP-1") == "/mine.png" && wallpaper_image_for(cfg, "DP-1") == "/global.png", "set_image targets one output");
    wallpaper_clear_image(cfg, "eDP-1");
    check(wallpaper_image_for(cfg, "eDP-1") == "/global.png" && !cfg.wallpaper_columns.contains("eDP-1"), "clear_image restores the global image");
    cfg.wallpaper_columns["DP-1"] = {"/a.png", "/b.png"};
    wallpaper_clear_image(cfg, "DP-1");
    check(wallpaper_column_override(cfg, "DP-1", 0, false).empty() && wallpaper_column_override(cfg, "DP-1", 1, false) == "/b.png", "clearing column 0 keeps the other columns");
}
