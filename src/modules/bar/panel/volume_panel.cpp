#include <algorithm>
#include <cmath>
#include <format>

#include "render/icons.h"

#include "modules/bar/panel/volume_panel.h"
#include "modules/bar/panel/widgets.h"

#include "render/glyphs.h"

namespace astralia {

namespace {

namespace cfg = panel_config;

enum Action { slider = 1,
              mute,
              device };

constexpr ui::TextStyle text_style{ui::FontFamily::text, cfg::text_px};
constexpr ui::TextStyle icon_style{ui::FontFamily::icon, cfg::icon_px};

const char *node_icon(const AudioNode &node) {
    return icon::volume_threshold(node.muted, node.percent);
}

void add_device_slider(std::vector<VolumeRow> &rows, const AudioService &audio, const char *title, uint32_t id) {
    std::optional<AudioNode> node = audio.node(id);
    rows.push_back({VolumeRow::Kind::label, cfg::volume_label_row, title, node ? node->label : std::string()});
    rows.push_back({VolumeRow::Kind::slider, cfg::volume_slider_row, {}, {}, node.value_or(AudioNode{})});
}

} // namespace

std::vector<VolumeRow> volume_rows(const AudioService &audio) {
    std::vector<VolumeRow> rows;
    add_device_slider(rows, audio, "Output", audio.sink_id());
    add_device_slider(rows, audio, "Input", audio.source_id());
    rows.push_back({VolumeRow::Kind::divider, 1.0f});
    rows.push_back({VolumeRow::Kind::section, cfg::volume_section_row, "Applications"});
    for (const AudioNode &stream : audio.nodes(AudioNodeKind::playback)) {
        rows.push_back({VolumeRow::Kind::app, cfg::volume_app_row, stream.label, {}, stream});
    }
    rows.push_back({VolumeRow::Kind::divider, 1.0f});
    auto add_devices = [&](const char *title, AudioNodeKind kind, uint32_t selected) {
        rows.push_back({VolumeRow::Kind::section, cfg::volume_section_row, title});
        for (const AudioNode &node : audio.nodes(kind)) {
            rows.push_back({VolumeRow::Kind::device, cfg::volume_device_row, node.label, {}, node, node.id == selected});
        }
    };
    add_devices("Output Device", AudioNodeKind::sink, audio.sink_id());
    add_devices("Input Device", AudioNodeKind::source, audio.source_id());
    rows.push_back({VolumeRow::Kind::section, cfg::trailing_spacer, ""});
    return rows;
}

VolumePanel::VolumePanel(AudioService &audio) : audio_(audio) {
    audio_.changed.connect([notify = notifier()](AudioKind) { notify(); });
}

void VolumePanel::opened() {
    selected_ = 0;
    dragging_ = 0;
    hovered_ = 0;
}

void VolumePanel::closed() {
    selected_ = 0;
    dragging_ = 0;
    hovered_ = 0;
}

bool VolumePanel::hover(int id, int a) {
    uint32_t next = id == slider && a > 0 ? static_cast<uint32_t>(a) : 0;
    if (next == hovered_) {
        return false;
    }
    hovered_ = next;
    return true;
}

float VolumePanel::content_height(ui::Canvas &) {
    float height = 0.0f;
    for (const VolumeRow &row : volume_rows(audio_)) {
        height += row.height + cfg::list_spacing;
    }
    return std::max(0.0f, height - cfg::list_spacing);
}

void VolumePanel::paint(ui::Canvas &canvas, const ui::Box &view, float scroll, PanelPaint &paint) {
    float y = view.y - scroll;
    for (const VolumeRow &row : volume_rows(audio_)) {
        ui::Box box{view.x, y, view.w, row.height};
        y += row.height + cfg::list_spacing;
        if (box.y + box.h < view.y || box.y > view.y + view.h) {
            continue;
        }
        switch (row.kind) {
        case VolumeRow::Kind::label: {
            ui::TextSize title = canvas.measure(row.text, text_style);
            panel_widgets::text_in_row(canvas, row.text, text_style, box.x, box, palette::text);
            if (!row.detail.empty()) {
                ui::TextStyle clipped = text_style;
                clipped.max_width = static_cast<int>(cfg::volume_label_cap);
                panel_widgets::text_in_row(canvas, "\xE2\x80\x94 " + row.detail, clipped, box.x + title.w + cfg::tight_gap, box, palette::text_dim);
            }
            break;
        }
        case VolumeRow::Kind::slider: {
            const AudioNode &node = row.node;
            ui::Box button{box.x + box.w - cfg::volume_button, box.y + (box.h - cfg::volume_button) / 2.0f, cfg::volume_button, cfg::volume_button};
            panel_widgets::icon_button(canvas, button, node_icon(node), palette::text);
            paint.region(button, mute, static_cast<int>(node.id));
            float percent_right = button.x - cfg::row_gap;
            std::string percent = node.muted ? std::string("muted") : std::format("{}%", node.percent);
            panel_widgets::right_text(canvas, percent, text_style, percent_right, box, palette::text_dim);
            ui::Box track{box.x, box.y, percent_right - cfg::volume_percent_width - cfg::volume_slider_gap - box.x, box.h};
            panel_widgets::slider(canvas, track, node.muted ? 0.0f : static_cast<float>(node.percent) / 100.0f, node.muted, node.id == selected_ || node.id == hovered_ || node.id == dragging_);
            paint.region(track, slider, static_cast<int>(node.id), 0, true);
            break;
        }
        case VolumeRow::Kind::section:
            if (!row.text.empty()) {
                ui::TextSize size = canvas.measure(row.text, text_style);
                canvas.text(row.text, text_style, box.x, box.y + box.h - size.h, palette::text);
            }
            break;
        case VolumeRow::Kind::divider:
            canvas.rect({box.x, box.y, box.w, 1.0f}, palette::text_alpha06);
            break;
        case VolumeRow::Kind::app: {
            const AudioNode &node = row.node;
            ui::Box button{box.x + box.w - cfg::volume_button, box.y, cfg::volume_button, cfg::volume_button};
            panel_widgets::icon_button(canvas, button, node_icon(node), palette::text);
            paint.region(button, mute, static_cast<int>(node.id));
            float percent_right = button.x - cfg::row_gap;
            std::string percent = node.muted ? std::string("muted") : std::format("{}%", node.percent);
            ui::TextSize percent_size = canvas.measure(percent, text_style);
            canvas.text(percent, text_style, percent_right - percent_size.w, box.y, palette::text_dim);
            float name_right = percent_right - cfg::volume_percent_width - cfg::row_gap;
            ui::TextStyle clipped = text_style;
            clipped.max_width = static_cast<int>(std::max(0.0f, name_right - box.x));
            canvas.text(row.text, clipped, box.x, box.y, palette::text);
            ui::Box track{box.x, box.y + cfg::volume_text_row + cfg::volume_app_gap, name_right - box.x, cfg::volume_app_slider};
            panel_widgets::slider(canvas, track, node.muted ? 0.0f : static_cast<float>(node.percent) / 100.0f, node.muted, node.id == selected_ || node.id == hovered_ || node.id == dragging_, cfg::volume_app_track);
            paint.region(track, slider, static_cast<int>(node.id), 0, true);
            break;
        }
        case VolumeRow::Kind::device: {
            float dot = cfg::volume_indicator;
            float cy = box.y + box.h / 2.0f;
            canvas.rounded({box.x, cy - dot / 2.0f, dot, dot}, dot / 2.0f, row.selected ? palette::accent : palette::text_alpha20);
            if (row.selected) {
                float inner = cfg::volume_indicator_dot;
                canvas.rounded({box.x + (dot - inner) / 2.0f, cy - inner / 2.0f, inner, inner}, inner / 2.0f, palette::text);
            }
            float text_x = box.x + dot + cfg::row_icon_gap;
            ui::TextStyle clipped = text_style;
            clipped.max_width = static_cast<int>(std::max(0.0f, box.x + box.w - text_x));
            panel_widgets::text_in_row(canvas, row.text, clipped, text_x, box, row.selected ? palette::accent : palette::text);
            paint.region(box, device, static_cast<int>(row.node.id));
            break;
        }
        }
    }
}

void VolumePanel::step(uint32_t id, int delta) {
    if (std::optional<AudioNode> node = audio_.node(id)) {
        set_percent(id, std::clamp(node->percent + delta, 0, 100));
    }
}

void VolumePanel::set_percent(uint32_t id, int percent) {
    std::optional<AudioNode> node = audio_.node(id);
    if (node && node->percent != percent) {
        audio_.set_volume(id, percent);
    }
}

bool VolumePanel::activate(const PanelRegion &region, double x, double) {
    auto id = static_cast<uint32_t>(region.a);
    switch (region.id) {
    case slider:
        selected_ = id;
        dragging_ = id;
        set_percent(id, static_cast<int>(std::lround(panel_widgets::slider_value(region.box, x) * 100.0f)));
        return true;
    case mute:
        if (std::optional<AudioNode> node = audio_.node(id)) {
            audio_.set_mute(id, !node->muted);
        }
        return true;
    case device:
        audio_.set_default(id);
        return true;
    default:
        return false;
    }
}

bool VolumePanel::drag(const PanelRegion &region, double x, double) {
    if (region.id != slider) {
        return false;
    }
    set_percent(static_cast<uint32_t>(region.a), static_cast<int>(std::lround(panel_widgets::slider_value(region.box, x) * 100.0f)));
    return true;
}

bool VolumePanel::key(const input::KeyEvent &event) {
    if (selected_ != 0 && (event.kind == input::KeyKind::Left || event.kind == input::KeyKind::Right)) {
        step(selected_, event.kind == input::KeyKind::Right ? 1 : -1);
        return true;
    }
    return false;
}

bool VolumePanel::wheel(double, double, double) {
    return false;
}

} // namespace astralia
