#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "wayland/render/gl_canvas.h"
#include "wayland/render/wallpaper_column.h"

struct WaylandState;

namespace astralia::lock {

class Wallpaper {
  public:
    void load(WaylandState &app);
    bool loaded() const { return loaded_; }
    bool ready(const std::string &output_name) const;
    void draw(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h);
    void release();

    std::function<void()> request_frames;

  private:
    struct Output {
        WallpaperColumnGl gl;
        std::vector<std::unique_ptr<WallpaperColumn>> columns;
        int32_t width = 0;
        int32_t height = 0;
        int32_t scale = 0;
    };

    Output &output_for(const std::string &output_name);
    void sync(const std::string &output_name, Output &output, int32_t w, int32_t h, int32_t scale);
    int32_t scale_of(const std::string &output_name) const;

    WaylandState *app_ = nullptr;
    bool loaded_ = false;
    std::unordered_map<std::string, std::unique_ptr<Output>> outputs_;
};

} // namespace astralia::lock
