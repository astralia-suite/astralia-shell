#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "core/log.h"

#include "render/oklab.h"

#include "service/palette_export.h"

namespace astralia {

namespace {

constexpr float bright_mix = 0.2f;
constexpr float min_contrast = 4.5f;
constexpr float lightness_step = 0.01f;
constexpr int max_steps = 100;

uint32_t hex_of(const Color &c) {
    auto channel = [](float v) {
        return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    return channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b);
}

Color readable(const Color &c, const Theme &theme) {
    Oklch lch = oklch_from_color(c);
    float step = oklch_from_color(theme.text).L > oklch_from_color(theme.accent_container).L
                     ? lightness_step
                     : -lightness_step;
    Color out = c;
    for (int i = 0; i < max_steps && contrast_ratio(out, theme.accent_container) < min_contrast;
         ++i) {
        lch.L = std::clamp(lch.L + step, 0.0f, 1.0f);
        out = color_from_oklch(lch);
    }
    return out;
}

Color bright(const Color &c, const Theme &theme) { return lerp_color(c, theme.text, bright_mix); }

std::array<Color, 16> ansi_colors(const Theme &theme) {
    Color critical = readable(theme.critical, theme);
    Color green = readable(theme.pop_a, theme);
    Color yellow = readable(theme.warn, theme);
    Color blue = readable(theme.pop_b, theme);
    Color magenta = readable(theme.accent, theme);
    Color cyan = readable(theme.accent_alt, theme);
    return {theme.ink,
            critical,
            green,
            yellow,
            blue,
            magenta,
            cyan,
            readable(theme.text_muted, theme),
            readable(theme.text_dim, theme),
            bright(critical, theme),
            bright(green, theme),
            bright(yellow, theme),
            bright(blue, theme),
            bright(magenta, theme),
            bright(cyan, theme),
            theme.text};
}

} // namespace

std::filesystem::path palette_export_path() {
    const char *state = std::getenv("XDG_STATE_HOME");
    const char *home = std::getenv("HOME");
    if (state && state[0] == '/') {
        return std::filesystem::path(state) / "astralia" / "palette";
    }
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "astralia" / "palette";
    }
    return {};
}

std::string palette_export_text(const Theme &theme) {
    std::string text;
    char line[48];
    auto put = [&](const char *key, const Color &c) {
        std::snprintf(line, sizeof(line), "%s=%06x\n", key, hex_of(c));
        text += line;
    };
    put("fg", theme.text);
    put("bg", theme.accent_container);
    put("cursor", readable(theme.accent, theme));
    std::array<Color, 16> colors = ansi_colors(theme);
    for (size_t i = 0; i < colors.size(); ++i) {
        put(("color" + std::to_string(i)).c_str(), colors[i]);
    }
    return text;
}

std::string palette_export_css(const Theme &theme) {
    std::string css = ":root {\n";
    char line[96];
    auto put = [&](const char *name, const Color &c, const char *flag) {
        std::snprintf(line, sizeof(line), "  --%s: #%06x%s;\n", name, hex_of(c), flag);
        css += line;
    };
    put("astralia-ink", theme.ink, "");
    put("astralia-paper", theme.paper, "");
    put("astralia-surface", theme.surface, "");
    put("astralia-surface-alt", theme.surface_alt, "");
    put("astralia-accent", theme.accent, "");
    put("astralia-accent-alt", theme.accent_alt, "");
    put("astralia-pop-a", theme.pop_a, "");
    put("astralia-pop-b", theme.pop_b, "");
    put("astralia-pop-c", theme.pop_c, "");
    put("astralia-critical", theme.critical, "");
    put("astralia-warn", theme.warn, "");
    put("astralia-text-muted", theme.text_muted, "");
    put("zen-primary-color", theme.accent, " !important");
    put("zen-colors-primary", theme.text, " !important");
    put("zen-colors-secondary", theme.surface_alt, " !important");
    put("zen-colors-tertiary", theme.paper, " !important");
    put("zen-colors-border", theme.surface_alt, " !important");
    put("zen-colors-input-bg", theme.surface, " !important");
    css += "}\n";
    return css;
}

namespace {

bool write_atomic(const std::filesystem::path &path, const std::string &text) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        out << text;
        if (!out) {
            log::error("palette export: cannot write {}", temp.string());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        log::error("palette export: cannot rename onto {}: {}", path.string(), ec.message());
        return false;
    }
    return true;
}

} // namespace

bool export_palette(const Theme &theme, const std::filesystem::path &path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        log::error("palette export: cannot create {}: {}", path.parent_path().string(),
                   ec.message());
        return false;
    }
    std::filesystem::path css = path;
    css += ".css";
    bool plain = write_atomic(path, palette_export_text(theme));
    bool styled = write_atomic(css, palette_export_css(theme));
    return plain && styled;
}

bool export_palette(const Theme &theme) { return export_palette(theme, palette_export_path()); }

} // namespace astralia
