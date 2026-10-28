#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/signal.h"

#include "render/palette_extract.h"
#include "render/theme.h"

#include "service/settings_service.h"

namespace astralia {

struct ThemeSource {
    std::string path;
    int64_t modified = 0;
    bool fixed = false;
    Scheme scheme = Scheme::light;

    bool operator==(const ThemeSource &) const = default;
};

std::string theme_wallpaper_path(const Config &cfg, const std::string &monitor_name);
ThemeSource theme_source(const Config &cfg, const std::string &monitor_name);
Extraction default_palette_extraction();
bool extraction_differs(const std::optional<Extraction> &a, const Extraction &b);

class ThemeService {
  public:
    ThemeService();
    ThemeService(const ThemeService &) = delete;
    ThemeService &operator=(const ThemeService &) = delete;

    void sync(const Config &cfg, const std::string &monitor_name);

    using VideoFrameDecoder =
        std::function<unsigned char *(const std::string &path, int &width, int &height)>;
    VideoFrameDecoder decode_video_frame;

    Signal<> changed;

  private:
    void apply(const Extraction &extraction, Scheme scheme);

    ThemeSource source_;
    bool has_source_ = false;
    std::optional<Extraction> applied_;
    std::optional<Scheme> applied_scheme_;
    uint64_t generation_ = 0;
    std::shared_ptr<int> life_;
};

} // namespace astralia
