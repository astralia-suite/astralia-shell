#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/reactor.h"

namespace astralia::ui {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct OutputInfo {
    std::string name;
    Rect geometry;
    float scale = 1.0f;
};

enum class FocusMode {
    none,
    on_demand,
    exclusive,
};

struct SurfaceSpec {
    std::string name;
    int width = 0;
    int height = 0;
    FocusMode focus = FocusMode::none;
};

class Surface {
  public:
    virtual ~Surface() = default;

    virtual bool open(std::string_view output) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual void resize(int width, int height) = 0;
    virtual void set_focus(FocusMode mode) = 0;
    virtual void set_input_region(const std::vector<Rect> &region) = 0;
    virtual void request_frame() = 0;
};

class Host {
  public:
    virtual ~Host() = default;

    virtual Reactor &reactor() = 0;
    virtual std::vector<OutputInfo> outputs() const = 0;
    virtual std::unique_ptr<Surface> create_surface(const SurfaceSpec &spec) = 0;
};

} // namespace astralia::ui
