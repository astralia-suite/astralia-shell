#pragma once

#include <EGL/egl.h>
#include <memory>
#include <wayland-client.h>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/bar/panel/panel_set.h"

#include "wayland/render/gl_canvas.h"
#include "wayland/render/overlay_panel.h"

#include "service/wayland/text_input_service.h"

class PanelSurface final : public TextInputClient {
  public:
    PanelSurface(WaylandState &app, MonitorOutput &mon);
    ~PanelSurface() override;

    bool create_surface(wl_output *output);
    bool configured() const { return !base_.layer_surface || base_.configured; }
    bool init_egl();
    void destroy();
    bool owns(wl_surface *surface) const { return surface != nullptr && surface == base_.surface; }
    void request_frame();

    astralia::PanelSet &panels() { return set_; }
    const astralia::PanelSet &panels() const { return set_; }
    bool is_open() const { return set_.any_open(); }

    void press(int button, double x, double y);
    void wheel(double x, double y, double dy);
    void key(const KeyEvent &event);
    void move(double x, double y);
    void release();
    bool wants_hand(double x, double y) const { return set_.clickable(x, y); }

    TextInputState text_input_state() const override;
    void text_input_apply_edit(const TextInputEdit &edit) override;
    void text_input_reset_preedit() override;
    void text_input_activated(TextInputService &) override {}
    void text_input_deactivated(TextInputService &) override;

  private:
    void paint();
    void state_changed(astralia::PanelId id, bool open);
    void sync_text_input();
    float card_top() const;
    void send_key(const astralia::input::KeyEvent &event);

    WaylandState &app_;
    MonitorOutput &mon_;
    astralia::PanelSet set_;
    OverlayPanelBase base_;
    GlCanvas canvas_;
    bool text_focused_ = false;
};
