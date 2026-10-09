#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "config/panel_config.h"

#include "core/animation.h"
#include "core/input.h"
#include "core/reactor.h"

#include "ui/canvas.h"

namespace astralia {

enum class PanelAnchor { left,
                         center,
                         right };

struct PanelRegion {
    ui::Box box;
    int id = 0;
    int a = 0;
    int b = 0;
    bool drag = false;
};

class PanelPaint {
  public:
    explicit PanelPaint(std::vector<PanelRegion> &regions) : regions_(regions) {}

    void region(const ui::Box &box, int id, int a = 0, int b = 0, bool drag = false) { regions_.push_back({box, id, a, b, drag}); }

  private:
    std::vector<PanelRegion> &regions_;
};

class PanelContent {
  public:
    virtual ~PanelContent() = default;

    virtual std::string_view title() const = 0;
    virtual PanelAnchor anchor() const { return PanelAnchor::right; }
    virtual float width() const { return panel_config::width; }
    virtual float max_height() const { return panel_config::max_height; }
    virtual void opened() {}
    virtual void closed() {}
    virtual float content_height(ui::Canvas &canvas) = 0;
    virtual void paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) = 0;
    virtual bool activate(const PanelRegion &, double, double) { return false; }
    virtual bool drag(const PanelRegion &, double, double) { return false; }
    virtual void drop(const PanelRegion &) {}
    virtual bool key(const input::KeyEvent &) { return false; }
    virtual bool wheel(double, double, double) { return false; }
    virtual bool scrollable() const { return true; }
    virtual bool wants_text() const { return false; }
    virtual ui::Box cursor() const { return {}; }
    virtual void paint_header(ui::Canvas &, const ui::Box &, PanelPaint &) {}
    virtual float dialog_height() { return 0.0f; }
    virtual void paint_dialog(ui::Canvas &, const ui::Box &, PanelPaint &) {}
    virtual bool dismiss_dialog() { return false; }
    virtual std::chrono::milliseconds refresh_interval() const { return std::chrono::milliseconds(0); }
    virtual void refresh() {}

    AnimationManager &animations() { return animations_; }
    std::function<void()> guarded(std::function<void()> fn) {
        return [alive = std::weak_ptr<bool>(alive_), fn = std::move(fn)] {
            if (alive.lock()) {
                fn();
            }
        };
    }
    std::function<void()> notifier() {
        return guarded([this] {
            if (changed) {
                changed();
            }
        });
    }
    std::function<void()> changed;
    input::Button pressed = input::Button::Left;

  private:
    AnimationManager animations_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

class Panel {
  public:
    Panel(std::unique_ptr<PanelContent> content, Reactor &reactor);
    ~Panel() { *alive_ = false; }

    void open();
    void close();
    void toggle() { is_open() ? close() : open(); }
    void tick(std::chrono::steady_clock::time_point now) {
        animations_.tick(now);
        content_->animations().tick(now);
    }

    bool press(double x, double y, input::Button button = input::Button::Left);
    bool move(double x, double y);
    bool release();
    bool wheel(double x, double y, double dy);
    bool key(const input::KeyEvent &event);
    bool clickable(double x, double y) const;
    bool contains(double x, double y) const;

    bool is_open() const { return open_; }
    bool animating() const { return animations_.hasActive() || content_->animations().hasActive(); }
    bool dragging() const { return dragging_; }
    bool closing() const { return closing_; }
    const ui::Box &dialog() const { return dialog_; }
    PanelContent &content() { return *content_; }
    const PanelContent &content() const { return *content_; }
    const ui::Box &card() const { return card_; }

    float card_x(float surface_width) const;
    ui::Box paint(ui::Canvas &canvas, float surface_width, float top) { return paint_at(canvas, card_x(surface_width), top); }
    ui::Box paint_at(ui::Canvas &canvas, float x, float top);
    ui::Box extent() const;

    std::function<void()> on_changed;
    std::function<void()> on_closed;

  private:
    void changed();
    PanelRegion *region_at(double x, double y);

    std::unique_ptr<PanelContent> content_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    AnimationManager animations_;
    int timer_ = -1;
    Reactor *reactor_ = nullptr;
    std::vector<PanelRegion> regions_;
    bool open_ = false;
    bool closing_ = false;
    float reveal_ = -1.0f;
    float target_ = -1.0f;
    float scroll_ = 0.0f;
    float content_h_ = 0.0f;
    float view_h_ = 0.0f;
    ui::Box card_;
    ui::Box dialog_;
    bool dragging_ = false;
    PanelRegion drag_region_;
};

} // namespace astralia
