#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "render/oklab.h"

#include "service/palette_export.h"

#include "check.h"
#include "palette_fixture.h"

void check_palette_export() {
    using namespace astralia;
    using test::check;

    Theme theme = theme_from_seed(test::base_extraction());
    std::string text = palette_export_text(theme);
    check(std::count(text.begin(), text.end(), '\n') == 19,
          "the export lists fg, bg, cursor and sixteen ANSI colors");
    check(text.rfind("fg=", 0) == 0, "the export starts with the foreground");

    auto hex = [](const Color &c) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x", static_cast<int>(c.r * 255.0f + 0.5f),
                      static_cast<int>(c.g * 255.0f + 0.5f), static_cast<int>(c.b * 255.0f + 0.5f));
        return std::string(buf);
    };
    check(text.find("bg=" + hex(theme.accent_container) + "\n") != std::string::npos,
          "the background is the pastel container color");
    check(text.find("color0=" + hex(theme.ink) + "\n") != std::string::npos,
          "ANSI black is the ink color");
    check(text.find("color15=" + hex(theme.text) + "\n") != std::string::npos,
          "bright white is the text color so bold text stays visible");
    check(text.find("fg=" + hex(theme.text) + "\n") != std::string::npos,
          "the foreground is the text color");

    for (float tone : {0.9f, 0.5f, 0.2f}) {
        Extraction wallpaper = test::base_extraction();
        wallpaper.tone = tone;
        Theme themed = theme_from_seed(wallpaper);
        std::string themed_text = palette_export_text(themed);
        bool readable = true;
        for (int i = 1; i <= 14; ++i) {
            std::string key = "color" + std::to_string(i) + "=";
            size_t at = themed_text.find("\n" + key);
            unsigned value = std::stoul(themed_text.substr(at + key.size() + 1, 6), nullptr, 16);
            Color c{static_cast<float>(value >> 16 & 0xff) / 255.0f,
                    static_cast<float>(value >> 8 & 0xff) / 255.0f,
                    static_cast<float>(value & 0xff) / 255.0f, 1.0f};
            readable = readable && contrast_ratio(c, themed.accent_container) >= 4.4f;
        }
        check(readable, "every ANSI text color is readable on the background");
        check(themed_text.find("color15=" + hex(themed.accent_container) + "\n") ==
                  std::string::npos,
              "bright white is never the background color");
        check(contrast_ratio(themed.text, themed.accent_container) >= 4.5f &&
                  themed_text.find("fg=" + hex(themed.text) + "\n") != std::string::npos,
              "the foreground reads on the background for every wallpaper tone");
    }

    std::filesystem::path dir = std::filesystem::temp_directory_path() / "astralia-palette-test";
    std::filesystem::remove_all(dir);
    std::filesystem::path file = dir / "nested" / "palette";
    check(export_palette(theme, file), "export creates the missing directories");
    std::stringstream read;
    read << std::ifstream(file).rdbuf();
    check(read.str() == text, "the exported file holds the palette text");
    check(!std::filesystem::exists(file.string() + ".tmp"), "no temporary file is left behind");

    std::string css = palette_export_css(theme);
    check(css.rfind(":root {", 0) == 0 && css.back() == '\n', "the css export is one :root rule");
    check(css.find("--astralia-accent: #" + hex(theme.accent) + ";") != std::string::npos,
          "the css export names the accent");
    check(css.find("--zen-primary-color: #" + hex(theme.accent) + " !important;") !=
              std::string::npos,
          "the css export drives Zen's primary color");
    check(css.find("--zen-colors-tertiary: #" + hex(theme.paper) + " !important;") !=
              std::string::npos,
          "the css export drives Zen's toolbar color");
    std::stringstream css_read;
    css_read << std::ifstream(file.string() + ".css").rdbuf();
    check(css_read.str() == css, "the css file holds the css export");

    Theme other = theme;
    other.ink = theme.paper;
    check(export_palette(other, file), "export overwrites an existing file");
    std::stringstream reread;
    reread << std::ifstream(file).rdbuf();
    check(reread.str() == palette_export_text(other), "the file follows the latest theme");
    check(!export_palette(theme, std::filesystem::path()), "an empty path is refused");
    std::filesystem::remove_all(dir);
}
