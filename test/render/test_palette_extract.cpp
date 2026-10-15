#include <cmath>
#include <vector>

#include "check.h"

#include "render/oklab.h"
#include "render/palette_extract.h"

namespace {

using astralia::Color;

struct Image {
    int width;
    int height;
    std::vector<uint8_t> data;

    Image(int w, int h, Color fill) : width(w), height(h), data(static_cast<size_t>(w) * h * 4) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                set(x, y, fill);
            }
        }
    }

    void set(int x, int y, Color c) {
        uint8_t *p = &data[(static_cast<size_t>(y) * width + x) * 4];
        p[0] = static_cast<uint8_t>(std::lround(c.r * 255.0f));
        p[1] = static_cast<uint8_t>(std::lround(c.g * 255.0f));
        p[2] = static_cast<uint8_t>(std::lround(c.b * 255.0f));
        p[3] = static_cast<uint8_t>(std::lround(c.a * 255.0f));
    }
};

float hue_of(const char *hex) { return astralia::oklch_from_color(astralia::color(hex)).h; }

} // namespace

void check_palette_extract() {
    using namespace astralia;
    using test::check;

    Image solid(40, 30, color("#E0405A"));
    Extraction a = extract_seeds(solid.data.data(), solid.width, solid.height, false);
    check(a.found && hue_distance(a.primary.hue, hue_of("#E0405A")) < 2.0f, "a solid colour seeds its own hue");
    check(!a.secondary.has_value(), "a solid colour has no secondary hue");
    Extraction again = extract_seeds(solid.data.data(), solid.width, solid.height, false);
    check(again.primary.hue == a.primary.hue && again.primary.chroma == a.primary.chroma, "extraction is deterministic");

    Image split(200, 100, color("#2060E0"));
    for (int y = 0; y < split.height; ++y) {
        for (int x = 0; x < 80; ++x) {
            split.set(x, y, color("#E0A020"));
        }
    }
    Extraction b = extract_seeds(split.data.data(), split.width, split.height, false);
    check(b.found && hue_distance(b.primary.hue, hue_of("#2060E0")) < 3.0f, "the larger of two colours wins");
    check(b.secondary.has_value() && hue_distance(b.secondary->hue, hue_of("#E0A020")) < 3.0f, "the smaller colour becomes the secondary");

    Image trio(300, 100, color("#2060E0"));
    for (int y = 0; y < trio.height; ++y) {
        for (int x = 0; x < 60; ++x) {
            trio.set(x, y, color("#E0A020"));
        }
        for (int x = 60; x < 120; ++x) {
            trio.set(x, y, color("#A040E0"));
        }
    }
    Extraction t3 = extract_seeds(trio.data.data(), trio.width, trio.height, false);
    float gap = t3.secondary ? hue_distance(t3.secondary->hue, t3.primary.hue) : 0.0f;
    check(t3.secondary.has_value() && gap >= 40.0f && gap <= 90.0f, "the secondary prefers a neighbouring hue");

    Image grey(100, 100, color("#808080"));
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            grey.set(x, y, color("#D01040"));
        }
    }
    Extraction c = extract_seeds(grey.data.data(), grey.width, grey.height, false);
    check(c.found && hue_distance(c.primary.hue, hue_of("#D01040")) < 3.0f, "a grey background cannot beat a saturated patch");

    Image plain(50, 50, color("#707070"));
    Extraction d = extract_seeds(plain.data.data(), plain.width, plain.height, false);
    check(!d.found, "a grey image has no usable colour");

    Image clear(50, 50, Color{1.0f, 0.0f, 0.0f, 0.0f});
    Extraction e = extract_seeds(clear.data.data(), clear.width, clear.height, false);
    check(!e.found, "a fully transparent image has no usable colour");

    Extraction f = extract_seeds(nullptr, 0, 0, false);
    check(!f.found, "no pixels have no usable colour");

    Image half(40, 40, color("#E0405A"));
    for (int i = 0; i < 40 * 40; ++i) {
        uint8_t *p = &half.data[static_cast<size_t>(i) * 4];
        p[0] = static_cast<uint8_t>(p[0] * 0.8f);
        p[1] = static_cast<uint8_t>(p[1] * 0.8f);
        p[2] = static_cast<uint8_t>(p[2] * 0.8f);
        p[3] = 204;
    }
    Extraction g = extract_seeds(half.data.data(), half.width, half.height, true);
    check(g.found && hue_distance(g.primary.hue, hue_of("#E0405A")) < 3.0f, "premultiplied pixels are read back to the same hue");
}
