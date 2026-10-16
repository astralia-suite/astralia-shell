#include <array>
#include <cairo.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <print>
#include <set>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "palette_fixture.h"

#include "core/config_file.h"
#include "core/json.h"
#include "core/pointer.h"

#include "modules/bar/frame.h"
#include "modules/bar/style.h"

#include "render/cairo_canvas.h"
#include "render/cover_cache.h"
#include "render/image_decode.h"

#include "service/settings_service.h"
#include "service/wallpaper_service.h"

#include "render/app_icon.h"
#include "render/glyphs.h"
#include "render/tokens.h"

#include "service/audio_service.h"
#include "service/bluetooth_service.h"
#include "service/network_service.h"

namespace {

int failures = 0;

void check(bool condition, std::string_view what,
           std::source_location where = std::source_location::current()) {
    if (condition) {
        return;
    }
    ++failures;
    std::println(stderr, "FAIL {}:{}: {}", where.file_name(), where.line(), what);
}

void check_color() {
    constexpr astralia::Color opaque = astralia::color("#FF8000");
    check(opaque.r == 1.0f && opaque.g == 128 / 255.0f && opaque.b == 0.0f && opaque.a == 1.0f,
          "six hex digits parse as opaque");
    constexpr astralia::Color translucent = astralia::color("0a061480");
    check(translucent.r == 10 / 255.0f && translucent.a == 128 / 255.0f,
          "eight hex digits carry alpha without a leading #");
}

void check_cover() {
    using astralia::cover;
    astralia::Placement wide = cover(200, 100, 100, 100);
    check(wide.scale == 1.0 && wide.x == -50.0 && wide.y == 0.0,
          "a wider image crops left and right");
    astralia::Placement tall = cover(100, 400, 200, 200);
    check(tall.scale == 2.0 && tall.x == 0.0 && tall.y == -300.0,
          "a taller image scales up and crops top and bottom");
    astralia::Placement exact = cover(640, 360, 1280, 720);
    check(exact.scale == 2.0 && exact.x == 0.0 && exact.y == 0.0, "same aspect fills exactly");
    astralia::Placement inside = astralia::contain(200, 100, 100, 100);
    check(inside.scale == 0.5 && inside.x == 0.0 && inside.y == 25.0,
          "a wider image fits inside and centers vertically");
    astralia::Placement upright = astralia::contain(100, 400, 200, 200);
    check(upright.scale == 0.5 && upright.x == 75.0 && upright.y == 0.0,
          "a taller image fits inside and centers horizontally");
}

void check_cover_cache() {
    using astralia::cover_cache_name;
    check(astralia::jpeg_reduction(0.0266) == 8, "a tiny target decodes at one eighth");
    check(astralia::jpeg_reduction(0.125) == 8, "an exact eighth is allowed");
    check(astralia::jpeg_reduction(0.2) == 4, "a fifth needs a quarter");
    check(astralia::jpeg_reduction(0.5) == 2, "a half is allowed");
    check(astralia::jpeg_reduction(0.6) == 1, "above a half decodes in full");
    check(astralia::jpeg_reduction(1.0) == 1, "full scale decodes in full");

    constexpr int64_t day = 24 * 3600;
    constexpr uintmax_t mib = 1024 * 1024;
    using Entries = std::vector<astralia::CoverCacheEntry>;
    check(astralia::cover_cache_expired({}, 1000 * day).empty(), "an empty cache expires nothing");
    check(astralia::cover_cache_expired(
              Entries{{"a.png", mib, 1000 * day}, {"b.png", mib, 999 * day}}, 1000 * day)
              .empty(),
          "recent entries under the cap stay");
    check(astralia::cover_cache_expired(
              Entries{{"old.png", mib, 900 * day}, {"new.png", mib, 999 * day}}, 1000 * day) ==
              std::vector<std::string>{"old.png"},
          "entries unused for 90 days expire");
    check(astralia::cover_cache_expired(Entries{{"a.png", 70 * mib, 990 * day},
                                                {"b.png", 70 * mib, 995 * day},
                                                {"c.png", 70 * mib, 999 * day}},
                                        1000 * day) == std::vector<std::string>{"a.png", "b.png"},
          "the least recently used go first over the cap");
    check(astralia::cover_cache_expired(
              Entries{{"x.1.tmp", mib, 1000 * day - 7200}, {"y.2.tmp", mib, 1000 * day - 60}},
              1000 * day) == std::vector<std::string>{"x.1.tmp"},
          "only stale temporary files expire");

    std::string name = cover_cache_name("/w/a.jpg", 100, 5, 1920, 1200);
    check(name.size() == 16 && !name.contains('.'), "cache names are a bare hash");
    check(name == cover_cache_name("/w/a.jpg", 100, 5, 1920, 1200),
          "the same input gives the same name");
    check(name != cover_cache_name("/w/b.jpg", 100, 5, 1920, 1200),
          "a different path changes the name");
    check(name != cover_cache_name("/w/a.jpg", 101, 5, 1920, 1200),
          "a different size changes the name");
    check(name != cover_cache_name("/w/a.jpg", 100, 6, 1920, 1200), "a new mtime changes the name");
    check(name != cover_cache_name("/w/a.jpg", 100, 5, 1920, 1080),
          "a different target changes the name");
    check(name != cover_cache_name("/w/a.jpg", 100, 5, 1920, 1200, true),
          "a contained decode caches apart from a cover");

    astralia::SurfacePtr opaque(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4));
    astralia::SurfacePtr clear(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 4));
    cairo_t *cr = cairo_create(opaque.get());
    cairo_set_source_rgb(cr, 0.2, 0.4, 0.6);
    cairo_paint(cr);
    cairo_destroy(cr);
    check(astralia::surface_opaque(opaque.get()), "a painted surface is opaque");
    check(!astralia::surface_opaque(clear.get()), "a transparent surface is not opaque");
    std::string jpeg =
        (std::filesystem::temp_directory_path() / std::format("astralia-test-{}.jpg", getpid()))
            .string();
    check(astralia::write_jpeg(opaque.get(), jpeg.c_str(), 90), "an opaque surface writes as jpeg");
    auto decoded = astralia::decode_image(jpeg);
    check(decoded && cairo_image_surface_get_width(decoded->get()) == 4 &&
              cairo_image_surface_get_height(decoded->get()) == 4 &&
              astralia::surface_opaque(decoded->get()),
          "a written jpeg decodes back opaque at the same size");
    std::filesystem::remove(jpeg);
}

void check_bar_styles() {
    using namespace astralia;
    test::apply_base_theme();
    const BarStyleSpec &okinami = bar_style_spec(BarStyle::okinami, false);
    BarFrame frame = bar_frame(okinami, 100.0f, IslandSpan{150.0f, 250.0f}, 300.0f, 400.0f);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 40);
    cairo_t *cr = cairo_create(surface);
    CairoCanvas canvas;
    canvas.bind(cr);
    bar_frame_base(canvas, okinami, frame, 400, 40);
    bar_frame_overlay(canvas, okinami, frame, 400, 40, 0.0f, false, false);
    cairo_surface_flush(surface);
    const unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    auto alpha = [&](int x, int y) { return static_cast<int>(data[y * stride + x * 4 + 3]); };
    auto is = [&](int x, int y, const Color &c) {
        const unsigned char *p = data + y * stride + x * 4;
        auto close = [](int a, float b) {
            return std::abs(a - static_cast<int>(std::lround(b * 255.0f))) <= 1;
        };
        return p[3] == 255 && close(p[2], c.r) && close(p[1], c.g) && close(p[0], c.b);
    };
    check(is(50, 2, palette::accent_container) && is(125, 2, palette::accent_container),
          "rail and island overlap share one fill");
    check(is(125, 5, palette::ink), "rail border line is the outline colour");
    check(is(50, 5, palette::accent_container) && is(50, 20, palette::accent_container),
          "island interior replaces the rail line");
    check(alpha(125, 20) == 0, "the gap under the rail is transparent");
    int fx = static_cast<int>(frame.fillets.front().edge_x);
    int fr = static_cast<int>(okinami.fillet_radius);
    int fy = static_cast<int>(okinami.rail_height);
    check(alpha(fx + fr - 2, fy + fr - 1) == 0,
          "the fillet cutout leaves the circle side transparent");
    check(alpha(fx + 1, fy + 2) == 255, "the fillet fills the corner beside the island");
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

} // namespace

void check_input_convert() {
    namespace in = astralia::input;
    in::PointerEvent left = astralia::pointer_event(10, 20, XCB_BUTTON_INDEX_1, true);
    check(left.button == in::Button::Left && left.pressed && left.x == 10 && left.y == 20,
          "left button press");
    check(astralia::pointer_event(0, 0, XCB_BUTTON_INDEX_3, false).button == in::Button::Right,
          "right button");
    check(astralia::pointer_event(0, 0, XCB_BUTTON_INDEX_2, false).button == in::Button::Middle,
          "middle button");
    check(astralia::pointer_event(0, 0, 8, false).button == in::Button::Other, "extra button");
    check(!astralia::scroll_event(XCB_BUTTON_INDEX_1).has_value(), "left button is not a scroll");
    check(astralia::scroll_event(XCB_BUTTON_INDEX_4)->dy < 0 &&
              astralia::scroll_event(XCB_BUTTON_INDEX_5)->dy > 0,
          "wheel buttons give signed scroll");

    astralia::KeyEvent text{astralia::KeyKind::text, "a"};
    check(astralia::to_neutral(text).kind == in::KeyKind::Text &&
              astralia::to_neutral(text).text == "a",
          "text key");
    check(astralia::to_neutral(astralia::KeyEvent{astralia::KeyKind::escape, ""}).kind ==
              in::KeyKind::Escape,
          "escape key");
    check(astralia::to_neutral(astralia::KeyEvent{astralia::KeyKind::backspace, ""}).kind ==
              in::KeyKind::Backspace,
          "backspace key");
    check(astralia::to_neutral(astralia::KeyEvent{astralia::KeyKind::enter, ""}).kind ==
              in::KeyKind::Enter,
          "enter key");
    check(astralia::to_neutral(astralia::KeyEvent{astralia::KeyKind::down, ""}).kind ==
              in::KeyKind::Down,
          "arrow key");
}

int main() {
    check_color();
    check_cover();
    check_cover_cache();
    check_bar_styles();
    check_input_convert();
    if (failures > 0) {
        std::println(stderr, "{} check(s) failed", failures);
        return EXIT_FAILURE;
    }
    std::println("all checks passed");
    return EXIT_SUCCESS;
}
