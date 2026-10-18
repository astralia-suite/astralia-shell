#include <cstdint>
#include <string>
#include <vector>

#include "modules/bamboo/bamboo_grow.h"
#include "modules/bamboo/bamboo_random.h"

#include "check.h"

namespace {

std::vector<uint8_t> copy(const astralia::BambooFrame &frame) {
    return std::vector<uint8_t>(frame.pixels,
                                frame.pixels + static_cast<size_t>(frame.stride) * frame.height);
}

size_t ink(const std::vector<uint8_t> &pixels) {
    size_t count = 0;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            ++count;
        }
    }
    return count;
}

size_t glyphs(const std::string &text) {
    size_t count = 0;
    for (char c : text) {
        if (c != ' ' && c != '\n') {
            ++count;
        }
    }
    return count;
}

bool ascii_or_bar(const std::string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (static_cast<unsigned char>(text[i]) < 0x7F) {
            continue;
        }
        if (text.compare(i, 3, "\xE2\x94\x82") != 0) {
            return false;
        }
        i += 2;
    }
    return true;
}

std::vector<std::u32string> rows(const std::string &text) {
    std::vector<std::u32string> out(1);
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            out.emplace_back();
        } else if (c < 0x80) {
            out.back() += static_cast<char32_t>(c);
        } else {
            out.back() += U'│';
            i += 2;
        }
    }
    return out;
}

bool is_leaf(char32_t c) { return c == U'>' || c == U'<'; }

bool is_branch(char32_t c) { return c == U'-' || c == U'/' || c == U'\\'; }

size_t count_leaves(const std::string &text) {
    size_t count = 0;
    for (const std::u32string &row : rows(text)) {
        for (char32_t c : row) {
            if (is_leaf(c)) {
                ++count;
            }
        }
    }
    return count;
}

bool leaves_touch_branches(const std::string &text) {
    std::vector<std::u32string> grid = rows(text);
    for (size_t y = 0; y < grid.size(); ++y) {
        for (size_t x = 0; x < grid[y].size(); ++x) {
            if (!is_leaf(grid[y][x])) {
                continue;
            }
            bool touches = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int ny = static_cast<int>(y) + dy;
                    int nx = static_cast<int>(x) + dx;
                    if (ny < 0 || ny >= static_cast<int>(grid.size()) || nx < 0 ||
                        nx >= static_cast<int>(grid[static_cast<size_t>(ny)].size())) {
                        continue;
                    }
                    char32_t near = grid[static_cast<size_t>(ny)][static_cast<size_t>(nx)];
                    touches = touches || is_branch(near) || is_leaf(near);
                }
            }
            if (!touches) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void check_bamboo_grow() {
    using astralia::BambooPhase;
    using test::check;

    astralia::BambooGrow grow;
    check(!grow.frame().ready() && grow.text().empty(), "a bamboo with no rebuild has no frame");
    grow.tick();
    check(!grow.frame().ready(), "ticking without a rebuild does nothing");

    astralia::bamboo_seed(7);
    grow.rebuild(480, 600);
    astralia::BambooFrame frame = grow.frame();
    check(frame.ready() && frame.width == 480 && frame.height == 600 && frame.stride >= 480 * 4,
          "the bamboo buffer has the requested size");
    std::string fresh = grow.text();
    size_t shoots = glyphs(fresh);
    check(shoots > 0 && grow.phase() == BambooPhase::Growing && ink(copy(frame)) > 0,
          "a fresh scene draws the ground and the shoots as glyphs");
    check(fresh.find("==") != std::string::npos && fresh.find("/\\") != std::string::npos,
          "the ground and the shoots are ascii art");

    for (int i = 0; i < 10; ++i) {
        grow.tick();
    }
    std::string grown = grow.text();
    check(count_leaves(grown) == 0, "a culm stays leafless while it grows");
    check(glyphs(grown) > shoots && grown.find("\xE2\x94\x82\xE2\x94\x82") != std::string::npos,
          "ticking grows the culms upward as vertical bars");
    check(ascii_or_bar(grown), "the scene holds ascii and the vertical bar only");
    std::vector<uint8_t> first = copy(grow.frame());

    astralia::bamboo_seed(7);
    grow.rebuild(480, 600);
    for (int i = 0; i < 10; ++i) {
        grow.tick();
    }
    check(grow.text() == grown && copy(grow.frame()) == first,
          "the same seed gives the same frames");

    for (int i = 0; i < 500 && grow.phase() == BambooPhase::Growing; ++i) {
        grow.tick();
    }
    std::string full = grow.text();
    check(grow.phase() == BambooPhase::Holding && count_leaves(full) > 0,
          "the leaves unfurl on the branches before the hold");
    check(full.find('>') != std::string::npos && full.find('<') != std::string::npos,
          "leaves point away from the stem on both sides");
    check(leaves_touch_branches(full), "every leaf sits on a branch");

    bool held = false;
    bool faded = false;
    for (int i = 0; i < 2000 && grow.cycles() == 0; ++i) {
        grow.tick();
        held = held || grow.phase() == BambooPhase::Holding;
        faded = faded || grow.phase() == BambooPhase::Fading;
    }
    check(held && faded && grow.cycles() == 1, "a cycle grows, holds, fades and regrows");
    check(grow.phase() == BambooPhase::Growing && glyphs(grow.text()) > 0,
          "a new cycle starts with fresh shoots");

    grow.rebuild(200, 100);
    check(grow.frame().width == 200 && grow.frame().height == 100 && grow.cycles() == 0,
          "a rebuild resizes the buffer and restarts the count");
    for (int i = 0; i < 50; ++i) {
        grow.tick();
    }
    grow.rebuild(0, 0);
    check(grow.frame().width == 1 && grow.frame().height == 1, "a degenerate size is clamped");
    for (int i = 0; i < 50; ++i) {
        grow.tick();
    }
}
