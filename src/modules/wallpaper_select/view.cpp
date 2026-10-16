#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "config/wallpaper_select_config.h"

#include "modules/wallpaper_select/view.h"

#include "render/sticker.h"
#include "render/tokens.h"

namespace astralia {

namespace {

namespace cfg = wallpaper_select_config;

constexpr ui::TextStyle label_style{ui::FontFamily::text, cfg::label_px, true};

float lerp(float a, float b, float t) { return a + (b - a) * t; }

void draw_cover(ui::Canvas &canvas, const ui::Box &inner, float radius, ui::ImageId image,
                float reveal) {
    canvas.begin_group(inner, {.clip = true, .radius = radius});
    ui::TextSize size = image == ui::no_image ? ui::TextSize{} : canvas.image_size(image);
    if (reveal < 1.0f || size.w <= 0.0f || size.h <= 0.0f) {
        canvas.rect({0.0f, 0.0f, inner.w, inner.h}, palette::surface);
    }
    if (size.w > 0.0f && size.h > 0.0f) {
        float scale = std::max(inner.w / size.w, inner.h / size.h);
        float w = size.w * scale;
        float h = size.h * scale;
        Color tint = palette::white;
        tint.a = reveal;
        canvas.draw_image(image, {(inner.w - w) / 2.0f, (inner.h - h) / 2.0f, w, h}, tint);
    }
    canvas.end_group();
}

void draw_card(ui::Canvas &canvas, const ui::Box &box, float zoom, ui::ImageId image,
               float reveal) {
    float radius = metrics::radius_card * zoom;
    float border = metrics::border_thick * zoom;
    sticker(canvas, box, radius, palette::paper, palette::ink, border,
            metrics::shadow_offset * zoom);
    ui::Box inner{box.x + border, box.y + border, std::max(0.0f, box.w - border * 2.0f),
                  std::max(0.0f, box.h - border * 2.0f)};
    draw_cover(canvas, inner, std::max(0.0f, radius - border), image, reveal);
}

float neighbour_scale(float distance) {
    return lerp(1.0f, cfg::neighbour_scale, std::min(distance, 1.0f));
}

float centre_offset(float distance, float width) {
    float near = width / 2.0f + cfg::card_gap + width * cfg::neighbour_scale / 2.0f;
    float side = width * cfg::neighbour_scale + cfg::card_gap;
    float magnitude = std::abs(distance);
    float offset = magnitude <= 1.0f ? magnitude * near : near + (magnitude - 1.0f) * side;
    return distance < 0.0f ? -offset : offset;
}

void centred_text(ui::Canvas &canvas, const std::string &text, float cx, float y,
                  const Color &color) {
    ui::TextSize size = canvas.measure(text, label_style);
    canvas.text(text, label_style, cx - size.w / 2.0f, y, color);
}

} // namespace

void paint_wallpaper_select(ui::Canvas &canvas, WallpaperSelectModel &model, float width,
                            float height) {
    model.begin_frame();
    canvas.set_opacity(1.0f);
    float columns = static_cast<float>(model.column_count());
    float x0 = std::floor(width * static_cast<float>(model.column()) / columns);
    float x1 = std::floor(width * static_cast<float>(model.column() + 1) / columns);
    width = x1 - x0;
    int centre_px = std::min(cfg::centre_px, static_cast<int>(std::max(width, height)));

    if (model.waiting()) {
        if (canvas.thumbnail(model.current(), centre_px) != ui::no_image) {
            model.reveal();
        } else {
            model.reveal_if_overdue(std::chrono::steady_clock::now());
        }
        if (model.waiting()) {
            return;
        }
    }

    float zoom = std::clamp(model.zoom(), 0.0f, 1.0f);
    canvas.rect({x0, 0.0f, width, height}, palette::base);

    const std::vector<std::string> &files = model.files();
    if (files.empty()) {
        if (!model.scanning()) {
            centred_text(canvas,
                         (model.animated() ? "No videos in " : "No images in ") + model.directory(),
                         x0 + width / 2.0f, height / 2.0f, palette::text);
        }
        return;
    }

    float target_w = width * cfg::card_scale;
    float target_h = height * cfg::card_scale;
    float stack_h = target_h + cfg::label_gap + cfg::label_height;
    float target_cy = (height - stack_h) / 2.0f + target_h / 2.0f;
    float card_w = lerp(width, target_w, zoom);
    float card_h = lerp(height, target_h, zoom);
    float cx = x0 + width / 2.0f;
    float cy = lerp(height / 2.0f, target_cy, zoom);

    float position = model.position();
    int first = std::max(0, static_cast<int>(std::floor(position)) - cfg::visible_neighbours);
    int last = std::min(static_cast<int>(files.size()) - 1,
                        static_cast<int>(std::ceil(position)) + cfg::visible_neighbours);

    std::vector<int> request_order(static_cast<size_t>(last - first + 1));
    std::iota(request_order.begin(), request_order.end(), first);
    std::ranges::sort(request_order, {}, [position](int index) {
        return std::abs(static_cast<float>(index) - position);
    });
    bool settled = std::abs(position - static_cast<float>(model.selected())) < 0.01f;
    std::vector<ui::ImageId> images(request_order.size(), ui::no_image);
    for (int index : request_order) {
        const std::string &path = files[static_cast<size_t>(index)];
        bool sharp = path == model.target() || (settled && index == model.selected());
        images[static_cast<size_t>(index - first)] =
            canvas.thumbnail(path, sharp ? centre_px : cfg::neighbour_px);
    }

    canvas.begin_group({x0, 0.0f, width, height}, {.clip = true});
    for (int ring = cfg::visible_neighbours + 1; ring >= 0; --ring) {
        for (int index = first; index <= last; ++index) {
            float distance = static_cast<float>(index) - position;
            if (static_cast<int>(std::floor(std::abs(distance) + 0.5f)) != ring) {
                continue;
            }
            float scale = neighbour_scale(std::abs(distance));
            float w = card_w * scale;
            float h = card_h * scale;
            ui::Box box{cx + centre_offset(distance, card_w) - w / 2.0f, cy - h / 2.0f, w, h};
            float fade =
                std::abs(distance) <= 1.0f
                    ? 1.0f
                    : std::max(0.0f, 1.0f - (std::abs(distance) - 1.0f) * cfg::neighbour_fade);
            canvas.set_opacity(fade);
            ui::ImageId image = images[static_cast<size_t>(index - first)];
            float reveal = model.fade(files[static_cast<size_t>(index)], image != ui::no_image);
            draw_card(canvas, {box.x - x0, box.y, box.w, box.h}, zoom, image, reveal);
            model.add_card(box, index);
        }
    }
    canvas.set_opacity(1.0f);
    canvas.end_group();

    if (zoom < 1.0f) {
        return;
    }
    const std::string name =
        std::filesystem::path(files[static_cast<size_t>(model.selected())]).filename().string();
    std::string counter =
        std::to_string(model.selected() + 1) + " / " + std::to_string(files.size());
    std::string label = name + "   " + counter;
    ui::TextSize size = canvas.measure(label, label_style);
    float label_w = size.w + cfg::label_padding * 2.0f;
    float label_y = target_cy + target_h / 2.0f + cfg::label_gap;
    sticker(canvas, {cx - label_w / 2.0f, label_y, label_w, cfg::label_height},
            metrics::radius_widget, palette::paper);
    canvas.text(label, label_style, cx - size.w / 2.0f,
                label_y + (cfg::label_height - size.h) / 2.0f, palette::text);
}

} // namespace astralia
