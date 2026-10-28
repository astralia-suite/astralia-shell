#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <thread>

#include "core/deferred_call.h"
#include "core/log.h"

#include "render/decode.h"
#include "render/theme.h"

#include "service/palette_export.h"
#include "service/theme_service.h"
#include "service/wallpaper_service.h"

namespace astralia {

namespace {

constexpr int sample_side = 256;
constexpr float same_hue_epsilon = 0.5f;

bool seed_differs(const Seed &a, const Seed &b) {
    return std::fabs(a.hue - b.hue) > same_hue_epsilon;
}

std::optional<Extraction> extract_from_video(const std::string &path,
                                             const ThemeService::VideoFrameDecoder &decoder) {
    int w = 0;
    int h = 0;
    std::unique_ptr<unsigned char[]> data(decoder ? decoder(path, w, h) : nullptr);
    if (!data) {
        log::info("theme: could not decode a frame of {}", path);
        return std::nullopt;
    }
    Extraction extraction = extract_seeds(data.get(), w, h, false);
    return extraction.found ? std::optional<Extraction>(extraction) : std::nullopt;
}

std::optional<Extraction> extract_from_file(const std::string &path,
                                            const ThemeService::VideoFrameDecoder &decoder) {
    if (path.empty()) {
        return std::nullopt;
    }
    if (wallpaper_is_video(path)) {
        return extract_from_video(path, decoder);
    }
    auto pixels = decode_pixels(path, [](int w, int h) {
        return static_cast<double>(sample_side) / std::max(1, std::max(w, h));
    });
    if (!pixels) {
        log::info("theme: could not decode {}", path);
        return std::nullopt;
    }
    Extraction extraction =
        extract_seeds(pixels->data.get(), pixels->width, pixels->height, pixels->premultiplied);
    if (!extraction.found) {
        log::info("theme: no usable colour in {}", path);
        return std::nullopt;
    }
    return extraction;
}

std::optional<Extraction> resolve_theme(const std::string &path,
                                        const ThemeService::VideoFrameDecoder &decoder) {
    if (std::optional<Extraction> extraction = extract_from_file(path, decoder)) {
        return extraction;
    }
    return extract_from_file(default_wallpaper_path(), decoder);
}

} // namespace

std::string theme_wallpaper_path(const Config &cfg, const std::string &monitor_name) {
    if (!cfg.capture_monitor.empty() && cfg.capture_column >= 0 &&
        cfg.capture_column <
            wallpaper_column_count(cfg, cfg.capture_monitor, cfg.wallpaper_animated_enabled)) {
        return cfg.wallpaper_animated_enabled
                   ? wallpaper_column_override(cfg, cfg.capture_monitor, cfg.capture_column, true)
                   : wallpaper_column_path(cfg, cfg.capture_monitor, cfg.capture_column, false);
    }
    if (cfg.wallpaper_animated_enabled) {
        if (!monitor_name.empty()) {
            return wallpaper_column_override(cfg, monitor_name, 0, true);
        }
        for (const auto &[name, columns] : cfg.wallpaper_animated_columns) {
            if (!columns.empty() && !columns.front().empty()) {
                return columns.front();
            }
        }
        return "";
    }
    if (!monitor_name.empty()) {
        return wallpaper_image_for(cfg, monitor_name);
    }
    for (const auto &[name, columns] : cfg.wallpaper_columns) {
        if (!columns.empty() && !columns.front().empty()) {
            return columns.front();
        }
    }
    return cfg.default_wallpaper ? cfg.wallpaper_path : "";
}

Extraction default_palette_extraction() {
    return {{{307.6f, 0.111f, 0.712f},
             {355.3f, 0.095f, 0.808f},
             {297.5f, 0.127f, 0.495f},
             {306.7f, 0.060f, 0.902f}},
            true};
}

ThemeSource theme_source(const Config &cfg, const std::string &monitor_name) {
    ThemeSource source;
    source.scheme = cfg.color_scheme == ColorScheme::dark ? Scheme::dark : Scheme::light;
    if (cfg.palette_source == PaletteSource::default_palette) {
        source.fixed = true;
        return source;
    }
    source.path = theme_wallpaper_path(cfg, monitor_name);
    if (source.path.empty()) {
        return source;
    }
    std::error_code ec;
    auto modified = std::filesystem::last_write_time(source.path, ec);
    if (!ec) {
        source.modified =
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch())
                .count();
    }
    return source;
}

bool extraction_differs(const std::optional<Extraction> &a, const Extraction &b) {
    if (!a || a->seeds.size() != b.seeds.size()) {
        return true;
    }
    for (size_t i = 0; i < b.seeds.size(); ++i) {
        if (seed_differs(a->seeds[i], b.seeds[i])) {
            return true;
        }
    }
    return false;
}

ThemeService::ThemeService() : life_(std::make_shared<int>(0)) {}

void ThemeService::sync(const Config &cfg, const std::string &monitor_name) {
    ThemeSource source = theme_source(cfg, monitor_name);
    if (has_source_ && source == source_) {
        return;
    }
    bool first = !has_source_;
    has_source_ = true;
    source_ = source;
    uint64_t gen = ++generation_;
    if (source.fixed) {
        apply(default_palette_extraction(), source.scheme);
        return;
    }
    if (first) {
        if (std::optional<Extraction> extraction = resolve_theme(source.path, decode_video_frame)) {
            apply(*extraction, source.scheme);
        }
        return;
    }
    std::weak_ptr<int> life = life_;
    std::thread([this, path = source.path, scheme = source.scheme, decoder = decode_video_frame,
                 gen, life] {
        std::optional<Extraction> extraction = resolve_theme(path, decoder);
        DeferredCall::call_later([this, extraction, scheme, gen, life] {
            auto keep = life.lock();
            if (!keep || gen != generation_ || !extraction) {
                return;
            }
            apply(*extraction, scheme);
        });
    }).detach();
}

void ThemeService::apply(const Extraction &extraction, Scheme scheme) {
    if (!extraction_differs(applied_, extraction) && applied_scheme_ == scheme) {
        return;
    }
    applied_ = extraction;
    applied_scheme_ = scheme;
    Theme theme = theme_from_seed(extraction, scheme);
    theme_apply(theme);
    export_palette(theme);
    changed.emit();
}

} // namespace astralia
