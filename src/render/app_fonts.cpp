#include <array>
#include <fontconfig/fontconfig.h>
#include <string>

#include "core/log.h"

#include "render/app_fonts.h"

namespace astralia {

namespace {

bool add_font(const char *name) {
    constexpr std::array<const char *, 2> dirs{ASTRALIA_ASSET_DIR "/fonts", ASTRALIA_SOURCE_ASSET_DIR "/fonts"};
    for (const char *dir : dirs) {
        std::string path = std::string(dir) + "/" + name;
        if (FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(path.c_str()))) {
            return true;
        }
    }
    log::error("cannot load the font {}/{}", dirs[0], name);
    return false;
}

} // namespace

bool register_app_fonts() {
    static const bool registered = [] {
        bool icons = add_font("tabler-icons.ttf");
        bool text = add_font("ComicShannsMono-Regular.otf");
        bool glyphs = add_font("YujiMai.ttf");
        return icons && text && glyphs;
    }();
    return registered;
}

const cairo_font_options_t *icon_font_options() {
    static cairo_font_options_t *options = [] {
        cairo_font_options_t *created = cairo_font_options_create();
        cairo_font_options_set_antialias(created, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(created, CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(created, CAIRO_HINT_METRICS_OFF);
        return created;
    }();
    return options;
}

} // namespace astralia
