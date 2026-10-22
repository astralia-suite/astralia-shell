#include <algorithm>
#include <cairo/cairo.h>
#include <cstdint>
#include <numeric>
#include <string>

#include "config/bamboo_config.h"

#include "modules/bamboo/grow.h"
#include "modules/bamboo/random.h"

namespace astralia::bamboo {

namespace {

int random_int(int lo, int hi) {
    int span = hi - lo + 1;
    return lo + std::min(span - 1, static_cast<int>(random01() * static_cast<float>(span)));
}

std::string utf8_encode(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

} // namespace

void Grow::rebuild(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    stride_ = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width_);
    buffer_.assign(static_cast<size_t>(stride_) * static_cast<size_t>(height_), 0);
    cycles_ = 0;
    start_cycle();
}

void Grow::start_cycle() {
    phase_ = Phase::Growing;
    fade_ = 1.0f;
    tips_.clear();
    cols_ = std::max(1, static_cast<int>(static_cast<float>(width_) / bamboo_config::kCellWidthPx));
    rows_ =
        std::max(1, static_cast<int>(static_cast<float>(height_) / bamboo_config::kCellHeightPx));
    grid_.assign(static_cast<size_t>(cols_) * static_cast<size_t>(rows_), Cell{});

    int ground = rows_ - 1 - bamboo_config::kGroundMarginRows;
    for (int x = bamboo_config::kGroundInsetCols; x < cols_ - bamboo_config::kGroundInsetCols;
         ++x) {
        put(x, ground, '=', Ink::Ground);
    }

    int usable = cols_ - 2 * bamboo_config::kGroundInsetCols;
    int slot = std::max(1, usable / bamboo_config::kShootCount);
    std::vector<int> xs(bamboo_config::kShootCount);
    std::vector<int> heights(bamboo_config::kShootCount);
    for (int i = 0; i < bamboo_config::kShootCount; ++i) {
        int jitter = slot > 2 ? random_int(0, slot - 2) : 0;
        int x = bamboo_config::kGroundInsetCols + slot * i + jitter;
        int h = random_int(1, bamboo_config::kShootMaxRows);
        xs[static_cast<size_t>(i)] = x;
        heights[static_cast<size_t>(i)] = h;
        if (h >= 2) {
            put(x, ground - 2, '/', Ink::Shoot);
            put(x + 1, ground - 2, '\\', Ink::Shoot);
            put(x, ground - 1, '|', Ink::Shoot);
            put(x + 1, ground - 1, '|', Ink::Shoot);
        } else {
            put(x, ground - 1, '/', Ink::Shoot);
            put(x + 1, ground - 1, '\\', Ink::Shoot);
        }
    }

    std::vector<int> order(bamboo_config::kShootCount);
    std::iota(order.begin(), order.end(), 0);
    int culms = std::min(bamboo_config::kCulmCount, bamboo_config::kShootCount);
    for (int i = 0; i < culms; ++i) {
        std::swap(order[static_cast<size_t>(i)],
                  order[static_cast<size_t>(random_int(i, bamboo_config::kShootCount - 1))]);
    }
    for (int i = 0; i < culms; ++i) {
        size_t shoot = static_cast<size_t>(order[static_cast<size_t>(i)]);
        Tip tip;
        tip.x = xs[shoot];
        tip.y = ground - heights[shoot];
        tip.side = random01() < 0.5f ? 1 : -1;
        tip.target = std::max(1, static_cast<int>(random_range(bamboo_config::kCulmMinHeightRatio,
                                                               bamboo_config::kCulmMaxHeightRatio) *
                                                  static_cast<float>(std::max(1, ground))));
        tip.next_node =
            random_int(bamboo_config::kNodeSpacingMinRows, bamboo_config::kNodeSpacingMaxRows);
        tips_.push_back(tip);
    }
    render(1.0f);
}

void Grow::tick() {
    if (buffer_.empty()) {
        return;
    }
    switch (phase_) {
    case Phase::Growing:
        grow_tips();
        render(1.0f);
        if (std::all_of(tips_.begin(), tips_.end(), [](const Tip &tip) { return tip.done; })) {
            phase_ = Phase::Holding;
            hold_left_ = bamboo_config::kHoldTicks;
        }
        break;
    case Phase::Holding:
        if (--hold_left_ <= 0) {
            phase_ = Phase::Fading;
        }
        break;
    case Phase::Fading:
        fade_ -= bamboo_config::kFadeStep;
        if (fade_ <= 0.0f) {
            ++cycles_;
            start_cycle();
        } else {
            render(fade_);
        }
        break;
    }
}

void Grow::grow_tips() {
    std::vector<Tip> spawned;
    for (Tip &tip : tips_) {
        if (tip.done) {
            continue;
        }
        if (tip.branch) {
            advance_branch(tip);
        } else {
            advance_culm(tip, spawned);
        }
    }
    tips_.insert(tips_.end(), spawned.begin(), spawned.end());
}

void Grow::advance_culm(Tip &tip, std::vector<Tip> &spawned) {
    tip.y -= 1;
    ++tip.length;
    ++tip.since_node;
    if (tip.y < 0) {
        tip.done = true;
        spawn_branches(tip, spawned);
        return;
    }

    if (tip.since_node >= tip.next_node) {
        put(tip.x, tip.y, '=', Ink::Node);
        put(tip.x + 1, tip.y, '=', Ink::Node);
        tip.since_node = 0;
        tip.next_node =
            random_int(bamboo_config::kNodeSpacingMinRows, bamboo_config::kNodeSpacingMaxRows);
        if (static_cast<float>(tip.length) >=
            (1.0f - bamboo_config::kBranchZoneRatio) * static_cast<float>(tip.target)) {
            tip.seeds.push_back({tip.x, tip.y});
        }
    } else {
        put(tip.x, tip.y, bamboo_config::kCulmGlyph, Ink::Culm);
        put(tip.x + 1, tip.y, bamboo_config::kCulmGlyph, Ink::Culm);
    }

    if (tip.length >= tip.target) {
        tip.done = true;
        spawn_branches(tip, spawned);
    }
}

void Grow::spawn_branches(const Tip &culm, std::vector<Tip> &spawned) {
    int side = culm.side;
    for (const Seed &seed : culm.seeds) {
        if (random01() >= bamboo_config::kBranchChance) {
            continue;
        }
        Tip branch;
        branch.branch = true;
        branch.side = side;
        branch.x = side > 0 ? seed.x + 1 : seed.x;
        branch.y = seed.y;
        branch.target =
            random_int(bamboo_config::kBranchMinLength, bamboo_config::kBranchMaxLength);
        spawned.push_back(branch);
        side = -side;
    }
}

void Grow::advance_branch(Tip &tip) {
    if (!tip.grown) {
        bool flat = random01() < bamboo_config::kBranchFlatChance;
        tip.x += tip.side;
        if (!flat) {
            tip.y -= 1;
        }
        ++tip.length;
        bool inside = tip.x >= 0 && tip.x < cols_ && tip.y >= 0;
        if (inside) {
            put(tip.x, tip.y, flat ? '-' : (tip.side > 0 ? '/' : '\\'), Ink::Branch);
            tip.path.push_back({tip.x, tip.y});
        }
        if (!inside || tip.length >= tip.target) {
            tip.grown = true;
            plan_leaves(tip);
        }
        return;
    }
    if (tip.next_leaf < tip.leaves.size()) {
        const Leaf &leaf = tip.leaves[tip.next_leaf++];
        put_leaf(leaf.x, leaf.y, leaf.glyph);
    }
    tip.done = tip.next_leaf >= tip.leaves.size();
}

void Grow::plan_leaves(Tip &tip) {
    char32_t glyph = tip.side > 0 ? bamboo_config::kLeafGlyphRight : bamboo_config::kLeafGlyphLeft;
    int above = 0;
    for (size_t i = 0; i < tip.path.size(); ++i) {
        if (static_cast<int>(i + 1) % bamboo_config::kLeafSpacing != 0) {
            continue;
        }
        tip.leaves.push_back(
            {tip.path[i].first, tip.path[i].second + (above++ % 2 == 0 ? -1 : 1), glyph});
    }
    if (!tip.path.empty()) {
        tip.leaves.push_back({tip.path.back().first + tip.side, tip.path.back().second, glyph});
    }
}

void Grow::put(int x, int y, char32_t glyph, Ink ink) {
    if (x < 0 || x >= cols_ || y < 0 || y >= rows_) {
        return;
    }
    grid_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)] = {glyph,
                                                                                           ink};
}

void Grow::put_leaf(int x, int y, char32_t glyph) {
    if (x < 0 || x >= cols_ || y < 0 || y >= rows_) {
        return;
    }
    const Cell &cell =
        grid_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
    if (cell.ink == Ink::None) {
        put(x, y, glyph, Ink::Leaf);
    }
}

void Grow::render(float alpha) {
    std::fill(buffer_.begin(), buffer_.end(), 0);
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        buffer_.data(), CAIRO_FORMAT_ARGB32, width_, height_, stride_);
    cairo_t *cr = cairo_create(surface);
    cairo_font_options_t *opts = cairo_font_options_create();
    cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_GRAY);
    cairo_set_font_options(cr, opts);
    cairo_font_options_destroy(opts);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, bamboo_config::kFontPx);

    cairo_font_extents_t font;
    cairo_font_extents(cr, &font);
    cairo_text_extents_t probe;
    cairo_text_extents(cr, "M", &probe);
    float origin_x =
        (static_cast<float>(width_) - static_cast<float>(cols_) * bamboo_config::kCellWidthPx) /
        2.0f;
    float origin_y =
        (static_cast<float>(height_) - static_cast<float>(rows_) * bamboo_config::kCellHeightPx) /
        2.0f;
    float inset_x = (bamboo_config::kCellWidthPx - static_cast<float>(probe.x_advance)) / 2.0f;
    float inset_y =
        (bamboo_config::kCellHeightPx - static_cast<float>(font.ascent + font.descent)) / 2.0f +
        static_cast<float>(font.ascent);

    for (int y = 0; y < rows_; ++y) {
        for (int x = 0; x < cols_; ++x) {
            const Cell &cell =
                grid_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)];
            if (cell.ink == Ink::None) {
                continue;
            }
            uint32_t argb = 0;
            switch (cell.ink) {
            case Ink::Ground:
                argb = bamboo_config::kGroundColor;
                break;
            case Ink::Shoot:
                argb = bamboo_config::kShootColor;
                break;
            case Ink::Culm:
                argb = bamboo_config::kCulmColor;
                break;
            case Ink::Node:
                argb = bamboo_config::kNodeColor;
                break;
            case Ink::Branch:
                argb = bamboo_config::kBranchColor;
                break;
            case Ink::Leaf:
                argb = bamboo_config::kLeafColor;
                break;
            case Ink::None:
                break;
            }
            cairo_set_source_rgba(cr, static_cast<double>((argb >> 16) & 0xFF) / 255.0,
                                  static_cast<double>((argb >> 8) & 0xFF) / 255.0,
                                  static_cast<double>(argb & 0xFF) / 255.0,
                                  static_cast<double>(((argb >> 24) & 0xFF) * alpha) / 255.0);
            std::string text = utf8_encode(cell.glyph);
            cairo_move_to(
                cr,
                static_cast<double>(origin_x + static_cast<float>(x) * bamboo_config::kCellWidthPx +
                                    inset_x),
                static_cast<double>(
                    origin_y + static_cast<float>(y) * bamboo_config::kCellHeightPx + inset_y));
            cairo_show_text(cr, text.c_str());
        }
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
}

std::string Grow::text() const {
    std::string out;
    for (int y = 0; y < rows_; ++y) {
        if (y > 0) {
            out += '\n';
        }
        for (int x = 0; x < cols_; ++x) {
            out += utf8_encode(
                grid_[static_cast<size_t>(y) * static_cast<size_t>(cols_) + static_cast<size_t>(x)]
                    .glyph);
        }
    }
    return out;
}

} // namespace astralia::bamboo
