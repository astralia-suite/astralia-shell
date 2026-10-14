#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/signal.h"

#include "render/palette_extract.h"
#include "service/settings_service.h"

namespace astralia {

struct ThemeSource {
    std::string path;
    int64_t modified = 0;

    bool operator==(const ThemeSource &) const = default;
};

std::string theme_wallpaper_path(const Config &cfg, const std::string &monitor_name);
ThemeSource theme_source(const Config &cfg, const std::string &monitor_name);
bool theme_extraction_differs(const std::optional<Extraction> &a, const Extraction &b);

class ThemeService {
  public:
    ThemeService();
    ThemeService(const ThemeService &) = delete;
    ThemeService &operator=(const ThemeService &) = delete;

    void sync(const Config &cfg, const std::string &monitor_name);

    Signal<> changed;

  private:
    void apply(const Extraction &extraction);

    ThemeSource source_;
    bool has_source_ = false;
    std::optional<Extraction> applied_;
    uint64_t generation_ = 0;
    std::shared_ptr<int> life_;
};

} // namespace astralia
