#include <algorithm>

#include "wayland/app/monitor_output.h"
#include "wayland/app/wayland_state.h"

#include "modules/lock/wayland/lock_wallpaper.h"

#include "service/wallpaper_service.h"

void LockWallpaper::load(WaylandState &app) {
    app_ = &app;
    loaded_ = true;
}

void LockWallpaper::release() {
    for (auto &entry : outputs_)
        for (auto &column : entry.second->columns)
            if (column)
                wallpaper_column_clear(*column, entry.second->gl);
    outputs_.clear();
    loaded_ = false;
    app_ = nullptr;
}

void LockWallpaper::draw(const std::string &output_name, GlCanvas &canvas, int32_t w, int32_t h) {
    if (!loaded_ || !app_ || w <= 0 || h <= 0)
        return;
    Output &output = output_for(output_name);
    int32_t scale = scale_of(output_name);
    if (output.width != w || output.height != h || output.scale != scale)
        sync(output_name, output, w, h, scale);

    size_t count = std::max<size_t>(output.columns.size(), 1);
    float column_w = static_cast<float>(w) / static_cast<float>(count);
    for (size_t i = 0; i < output.columns.size(); ++i)
        if (output.columns[i])
            wallpaper_column_draw(*output.columns[i], canvas, static_cast<float>(i) * column_w,
                                  column_w, static_cast<float>(h));
}

LockWallpaper::Output &LockWallpaper::output_for(const std::string &output_name) {
    std::unique_ptr<Output> &slot = outputs_[output_name];
    if (slot)
        return *slot;
    slot = std::make_unique<Output>();
    WaylandState &app = *app_;
    slot->gl.display = app.egl_display;
    slot->gl.make_current = [&app] { app_detail::rest_egl_current(app); };
    slot->gl.request_frame = [this] {
        if (request_frames)
            request_frames();
    };
    return *slot;
}

int32_t LockWallpaper::scale_of(const std::string &output_name) const {
    for (auto &mon : app_->outputs)
        if (mon->output.name == output_name)
            return std::max(1, mon->output.scale);
    return 1;
}

void LockWallpaper::sync(const std::string &output_name, Output &output, int32_t w, int32_t h,
                         int32_t scale) {
    const Config &cfg = app_->cfg;
    constexpr astralia::WallpaperSlot slot = astralia::WallpaperSlot::lock;
    size_t count = static_cast<size_t>(astralia::wallpaper_column_count(cfg, output_name, slot));

    for (size_t i = count; i < output.columns.size(); ++i)
        if (output.columns[i])
            wallpaper_column_clear(*output.columns[i], output.gl);
    output.columns.resize(count);
    output.width = w;
    output.height = h;
    output.scale = scale;

    int target_w = (w / static_cast<int>(count)) * scale;
    int target_h = h * scale;
    for (size_t i = 0; i < count; ++i) {
        std::unique_ptr<WallpaperColumn> &column = output.columns[i];
        if (!column)
            column = std::make_unique<WallpaperColumn>();

        int index = static_cast<int>(i);
        std::string path = astralia::wallpaper_column_path(cfg, output_name, index, slot);
        if (path.empty()) {
            wallpaper_column_clear(*column, output.gl);
            continue;
        }
        FillMode mode = astralia::wallpaper_fill_mode(cfg, output_name, index, slot) == "fit"
                            ? FillMode::Fit
                            : FillMode::Crop;
        wallpaper_column_set_static(*column, output.gl, path, target_w, target_h, mode);
        column->pending_transition = WallpaperTransition::None;
    }
}
