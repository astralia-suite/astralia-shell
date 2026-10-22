#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "service/wallpaper_service.h"

#include "check.h"

void check_wallpaper_resolve() {
    using namespace astralia;
    using test::check;
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    cfg.wallpaper_fill_modes["DP-1"] = {"fit", "tile"};

    check(wallpaper_column_path(cfg, "DP-1", 0, false) == "/dp1.png",
          "wallpaper_column_path(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 0, false) == "/global.png",
          "wallpaper_column_path(cfg, \"HDMI-1\", 0, false) == \"/global.png\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 0, false) == "fit",
          "wallpaper_fill_mode(cfg, \"DP-1\", 0, false) == \"fit\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 1, false) == "tile",
          "wallpaper_fill_mode(cfg, \"DP-1\", 1, false) == \"tile\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 2, false) == "crop",
          "wallpaper_fill_mode(cfg, \"DP-1\", 2, false) == \"crop\"");
    check(wallpaper_fill_mode(cfg, "HDMI-1", 0, false) == "crop",
          "wallpaper_fill_mode(cfg, \"HDMI-1\", 0, false) == \"crop\"");

    check(wallpaper_column_path(cfg, "DP-1", 1, false) == "/global.png",
          "wallpaper_column_path(cfg, \"DP-1\", 1, false) == \"/global.png\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 3, false) == "/global.png",
          "wallpaper_column_path(cfg, \"HDMI-1\", 3, false) == \"/global.png\"");
    check(wallpaper_column_count(cfg, "DP-1", false) == 1,
          "wallpaper_column_count(cfg, \"DP-1\", false) == 1");
    cfg.wallpaper_column_counts["DP-1"] = 2;
    check(wallpaper_column_count(cfg, "DP-1", false) == 2,
          "wallpaper_column_count(cfg, \"DP-1\", false) == 2");

    cfg.default_wallpaper = false;
    check(wallpaper_column_path(cfg, "HDMI-1", 0, false).empty(),
          "wallpaper_column_path(cfg, \"HDMI-1\", 0, false).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 1, false).empty(),
          "wallpaper_column_path(cfg, \"DP-1\", 1, false).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 0, false) == "/dp1.png",
          "wallpaper_column_path(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");

    check(wallpaper_column_override(cfg, "HDMI-1", 0, false).empty(),
          "wallpaper_column_override(cfg, \"HDMI-1\", 0, false).empty()");
    check(wallpaper_column_override(cfg, "DP-1", 0, false) == "/dp1.png",
          "wallpaper_column_override(cfg, \"DP-1\", 0, false) == \"/dp1.png\"");
    cfg.default_wallpaper = true;
    check(wallpaper_column_override(cfg, "HDMI-1", 0, false).empty(),
          "wallpaper_column_override(cfg, \"HDMI-1\", 0, false).empty()");

    cfg.wallpaper_animated_columns["DP-1"] = {"/dp1.mp4"};
    cfg.wallpaper_animated_fill_modes["DP-1"] = {"fit"};
    check(wallpaper_column_path(cfg, "DP-1", 0, true) == "/dp1.mp4",
          "wallpaper_column_path(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
    check(wallpaper_column_path(cfg, "HDMI-1", 0, true) == "/global.png",
          "wallpaper_column_path(cfg, \"HDMI-1\", 0, true) == \"/global.png\"");
    check(wallpaper_column_override(cfg, "HDMI-1", 0, true).empty(),
          "wallpaper_column_override(cfg, \"HDMI-1\", 0, true).empty()");
    check(wallpaper_column_override(cfg, "DP-1", 0, true) == "/dp1.mp4",
          "wallpaper_column_override(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
    check(wallpaper_fill_mode(cfg, "DP-1", 0, true) == "fit",
          "wallpaper_fill_mode(cfg, \"DP-1\", 0, true) == \"fit\"");
    check(wallpaper_fill_mode(cfg, "HDMI-1", 0, true) == "crop",
          "wallpaper_fill_mode(cfg, \"HDMI-1\", 0, true) == \"crop\"");
    check(wallpaper_column_count(cfg, "DP-1", true) == 1,
          "wallpaper_column_count(cfg, \"DP-1\", true) == 1");
    cfg.wallpaper_animated_column_counts["DP-1"] = 3;
    check(wallpaper_column_count(cfg, "DP-1", true) == 3,
          "wallpaper_column_count(cfg, \"DP-1\", true) == 3");

    cfg.default_wallpaper = false;
    check(wallpaper_column_path(cfg, "HDMI-1", 0, true).empty(),
          "wallpaper_column_path(cfg, \"HDMI-1\", 0, true).empty()");
    check(wallpaper_column_path(cfg, "DP-1", 0, true) == "/dp1.mp4",
          "wallpaper_column_path(cfg, \"DP-1\", 0, true) == \"/dp1.mp4\"");
}

void check_wallpaper_lock_slot() {
    using namespace astralia;
    using test::check;
    constexpr WallpaperSlot lock = WallpaperSlot::lock;
    constexpr WallpaperSlot still = WallpaperSlot::still;
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    cfg.wallpaper_columns["DP-1"] = {"/dp1.png"};
    cfg.wallpaper_column_counts["DP-1"] = 2;
    cfg.wallpaper_fill_modes["DP-1"] = {"fit"};

    check(wallpaper_column_count(cfg, "DP-1", lock) == 1,
          "the lock slot ignores the desktop column count");
    check(wallpaper_column_override(cfg, "DP-1", 0, lock).empty(),
          "the lock slot ignores the desktop columns");
    check(wallpaper_column_path(cfg, "DP-1", 0, lock) == "/global.png",
          "an empty lock column falls back to the default wallpaper");
    check(wallpaper_fill_mode(cfg, "DP-1", 0, lock) == "crop",
          "the lock slot ignores the desktop fill modes");

    wallpaper_set_column(cfg, "DP-1", 1, "/lock.png", lock);
    wallpaper_set_fill_mode(cfg, "DP-1", 1, "fit", lock);
    wallpaper_set_column_count(cfg, "DP-1", 3, lock);
    check(wallpaper_column_override(cfg, "DP-1", 1, lock) == "/lock.png" &&
              wallpaper_column_path(cfg, "DP-1", 1, lock) == "/lock.png" &&
              wallpaper_column_override(cfg, "DP-1", 0, lock).empty(),
          "the lock slot stores its own columns");
    check(wallpaper_fill_mode(cfg, "DP-1", 1, lock) == "fit" &&
              wallpaper_column_count(cfg, "DP-1", lock) == 3,
          "the lock slot stores its own fill mode and count");
    check(cfg.lock_wallpaper_columns.contains("DP-1") &&
              wallpaper_column_override(cfg, "DP-1", 0, still) == "/dp1.png" &&
              wallpaper_column_override(cfg, "DP-1", 1, still).empty() &&
              wallpaper_column_count(cfg, "DP-1", still) == 2 &&
              wallpaper_fill_mode(cfg, "DP-1", 1, still) == "crop",
          "the desktop slot is untouched");

    cfg.default_wallpaper = false;
    check(wallpaper_column_path(cfg, "DP-1", 0, lock).empty() &&
              wallpaper_column_path(cfg, "DP-1", 1, lock) == "/lock.png",
          "without the default wallpaper an empty lock column stays empty");

    check(wallpaper_lock_slot(cfg) == still, "Auto mode draws the desktop slot");
    cfg.lock_wallpaper_custom = true;
    check(wallpaper_lock_slot(cfg) == lock, "Custom mode draws the lock slot");

    cfg.wallpaper_dir = "/pictures";
    cfg.wallpaper_animated_dir = "/videos";
    cfg.lock_wallpaper_dir = "/locks";
    check(wallpaper_slot_dir(cfg, still) == "/pictures" &&
              wallpaper_slot_dir(cfg, WallpaperSlot::animated) == "/videos" &&
              wallpaper_slot_dir(cfg, lock) == "/locks",
          "every slot has its own directory");
    check(wallpaper_slot_of(false) == still && wallpaper_slot_of(true) == WallpaperSlot::animated,
          "the animated flag maps to a slot");
}

void check_wallpaper_images() {
    using namespace astralia;
    using test::check;
    Config cfg;
    cfg.wallpaper_path = "/global.png";
    check(wallpaper_image_for(cfg, "eDP-1") == "/global.png",
          "an output without an image uses the global one");
    wallpaper_set_image(cfg, "eDP-1", "/mine.png");
    check(wallpaper_image_for(cfg, "eDP-1") == "/mine.png" &&
              wallpaper_image_for(cfg, "DP-1") == "/global.png",
          "set_image targets one output");
    wallpaper_clear_image(cfg, "eDP-1");
    check(wallpaper_image_for(cfg, "eDP-1") == "/global.png" &&
              !cfg.wallpaper_columns.contains("eDP-1"),
          "clear_image restores the global image");
    cfg.wallpaper_columns["DP-1"] = {"/a.png", "/b.png"};
    wallpaper_clear_image(cfg, "DP-1");
    check(wallpaper_column_override(cfg, "DP-1", 0, false).empty() &&
              wallpaper_column_override(cfg, "DP-1", 1, false) == "/b.png",
          "clearing column 0 keeps the other columns");
}

void check_wallpaper_listing() {
    using namespace astralia;
    using test::check;
    check(wallpaper_is_image("/x/A.PNG") && wallpaper_is_image("b.jpeg") &&
              !wallpaper_is_image("c.mp4") && !wallpaper_is_image("d.txt"),
          "images are told by extension");
    check(wallpaper_is_video("/x/a.MKV") && wallpaper_is_video("b.webm") &&
              !wallpaper_is_video("c.png"),
          "videos are told by extension");
    check(wallpaper_picker_less("/x/b.jpg", "/x/a.png") &&
              wallpaper_picker_less("/x/a.png", "/x/B.png"),
          "files order by extension, then by name ignoring case");

    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("astralia_test_wallpaper_listing_" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "sub" / "deep");
    std::ofstream(root / "b.png").put('x');
    std::ofstream(root / "a.jpg").put('x');
    std::ofstream(root / "clip.mp4").put('x');
    std::ofstream(root / "sub" / "c.png").put('x');
    std::ofstream(root / "sub" / "deep" / "d.png").put('x');
    std::vector<std::string> images = wallpaper_list_files(root.string(), false, 100);
    check(images.size() == 3 && images[0].ends_with("a.jpg") && images[1].ends_with("b.png") &&
              images[2].ends_with("c.png"),
          "images are listed one folder deep");
    check(wallpaper_list_files(root.string(), false, 2).size() == 2, "the listing is capped");
    std::vector<std::string> videos = wallpaper_list_files(root.string(), true, 100);
    check(videos.size() == 1 && videos[0].ends_with("clip.mp4"), "videos are listed on request");
    check(wallpaper_list_files((root / "missing").string(), false, 100).empty(),
          "a missing folder lists nothing");
    std::filesystem::remove_all(root);
}
