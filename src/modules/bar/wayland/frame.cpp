#include <algorithm>
#include <cmath>

#include "modules/bar/wayland/frame.h"

#include "render/tokens.h"

namespace {

void build_corner(Texture &tex, int size, bool circle_on_right, int32_t scale) {
    std::vector<uint8_t> mask = fillet_rgba(size, circle_on_right);
    tex = make_texture_rgba(size, size, mask.data());
    tex.scale = scale;
}

void ensure_fillets(BarDecor &decor, const astralia::BarStyleSpec &style, int32_t scale) {
    int px = static_cast<int>(std::lround(style.fillet_radius * static_cast<float>(scale)));
    int inner_px = static_cast<int>(std::lround((style.fillet_radius + style.border_width) * static_cast<float>(scale)));
    if (px == decor.fillet_px && inner_px == decor.fillet_inner_px)
        return;
    build_corner(decor.fillet_left, px, false, scale);
    build_corner(decor.fillet_right, px, true, scale);
    build_corner(decor.fillet_inner_left, inner_px, false, scale);
    build_corner(decor.fillet_inner_right, inner_px, true, scale);
    decor.fillet_px = px;
    decor.fillet_inner_px = inner_px;
}

void ensure_hug(BarDecor &decor, const astralia::BarStyleSpec &style, int32_t hug_radius, int32_t scale) {
    int px = static_cast<int>(std::lround(static_cast<float>(hug_radius) * static_cast<float>(scale)));
    int inner_px = static_cast<int>(std::lround((static_cast<float>(hug_radius) + style.border_width) * static_cast<float>(scale)));
    if (px == decor.hug_px && inner_px == decor.hug_inner_px)
        return;
    if (px > 0) {
        build_corner(decor.hug_outer_left, px, true, scale);
        build_corner(decor.hug_outer_right, px, false, scale);
        build_corner(decor.hug_inner_left, inner_px, true, scale);
        build_corner(decor.hug_inner_right, inner_px, false, scale);
    }
    decor.hug_px = px;
    decor.hug_inner_px = inner_px;
}

} // namespace

std::vector<uint8_t> fillet_rgba(int size, bool circle_on_right) {
    std::vector<uint8_t> mask(static_cast<size_t>(size) * size * 4, 255);
    float radius = static_cast<float>(size);
    float cx = circle_on_right ? radius : 0.0f;
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float dist = std::hypot(static_cast<float>(px) + 0.5f - cx, static_cast<float>(py) + 0.5f - radius);
            float alpha = std::clamp(dist - radius + 0.5f, 0.0f, 1.0f);
            mask[(static_cast<size_t>(py) * size + px) * 4 + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
        }
    }
    return mask;
}

void bar_frame_base(Node *content, const astralia::BarStyleSpec &style, const astralia::BarFrame &frame, float width, float height) {
    if (style.continuous) {
        node_add_rrect(content, 0.0f, 0.0f, width, height, height * style.radius_ratio, style.border_width, astralia::rgba(style.bg), astralia::rgba(style.border));
        return;
    }
    if (!style.has_rail())
        return;
    float radius = style.island_radius;
    float bw = style.border_width;
    node_add_rect(content, 0.0f, 0.0f, width, style.rail_height, astralia::rgba(style.border));
    for (const astralia::IslandShape &island : frame.islands)
        node_add_rrect(content, island.outer_x, -radius, island.outer_width, height + radius, radius, 0.0f, astralia::rgba(style.border), astralia::rgba(style.border));
    node_add_rect(content, 0.0f, 0.0f, width, style.rail_height - bw, astralia::rgba(style.bg))->punch = true;
    for (const astralia::IslandShape &island : frame.islands) {
        Node *inner = node_add_rrect(content, island.inner_x, -radius, island.inner_width, height + radius - bw, radius - bw, 0.0f, astralia::rgba(style.bg), astralia::rgba(style.bg));
        inner->punch = true;
    }
}

void bar_frame_overlay(Node *content, BarDecor &decor, const astralia::BarStyleSpec &style, const astralia::BarFrame &frame, float width, float height, int32_t scale, int32_t hug_radius, bool left_flush, bool right_flush) {
    if (!style.has_rail())
        return;
    float bw = style.border_width;
    ensure_fillets(decor, style, scale);
    for (const astralia::Fillet &fillet : frame.fillets)
        node_add_texture(content, fillet.right_of_island ? fillet.edge_x : fillet.edge_x - style.fillet_radius, style.rail_height, fillet.right_of_island ? decor.fillet_right : decor.fillet_left, astralia::rgba(style.border));
    for (const astralia::Fillet &fillet : frame.fillets)
        node_add_texture(content, fillet.right_of_island ? fillet.edge_x - bw : fillet.edge_x - style.fillet_radius, style.rail_height - bw, fillet.right_of_island ? decor.fillet_inner_right : decor.fillet_inner_left, astralia::rgba(style.bg))->punch = true;

    if (hug_radius <= 0)
        return;
    ensure_hug(decor, style, hug_radius, scale);
    float hf = static_cast<float>(hug_radius);
    if (left_flush) {
        node_add_texture(content, 0.0f, height, decor.hug_outer_left, astralia::rgba(style.border));
        node_add_texture(content, -bw, height - bw, decor.hug_inner_left, astralia::rgba(style.bg))->punch = true;
    }
    if (right_flush) {
        node_add_texture(content, width - hf, height, decor.hug_outer_right, astralia::rgba(style.border));
        node_add_texture(content, width - hf, height - bw, decor.hug_inner_right, astralia::rgba(style.bg))->punch = true;
    }
}
