#pragma once

#include <string>
#include <vector>

#include "ui/geometry.h"
#include "ui/glyphs.h"
#include "ui/tokens.h"
#include "wayland/render/icon.h"
#include "wayland/render/node.h"
#include "wayland/render/renderer.h"
#include "wayland/render/text.h"
#include "wayland/render/texture.h"
#include "wayland/render/texture_cache.h"

constexpr float kPanelWidth = 400.0f;
constexpr float kPanelPadding = 20.0f;
constexpr float kPanelHeaderHeight = 32.0f;
constexpr float kPanelHeaderDividerGap = 4.0f;
constexpr float kPanelContentGap = 8.0f;
constexpr float kPanelGap = 8.0f;
constexpr float kPanelMaxHeight = 520.0f;
constexpr float kPanelActionButtonSize = 22.0f;
constexpr float kPanelListSpacing = 5.0f;
constexpr float kPanelRowGap = 6.0f;
constexpr float kPanelRowIconGap = 8.0f;
constexpr float kPanelRowActionGap = 4.0f;
constexpr float kPanelTightGap = 4.0f;
constexpr float kPanelDeviceRowHeight = 50.0f;
constexpr float kPanelDialogButtonPaddingH = 16.0f;
constexpr float kPanelConfirmButtonSize = 28.0f;
constexpr float kPanelSubPanelRowHeight = 28.0f;
constexpr float kPanelSubPanelTopMargin = 6.0f;
constexpr float kPanelDialogSpacerHeight = 14.0f;
constexpr float kPanelTrailingSpacerHeight = 4.0f;
constexpr float kPanelSubLabelHeight = 20.0f;
constexpr float kPanelSideMargin = 20.0f;

enum class PanelAnchor { Left,
                         Center,
                         Right };

inline float panel_anchor_x(PanelAnchor anchor, float surface_w, float panel_w) {
    switch (anchor) {
    case PanelAnchor::Left:
        return kPanelSideMargin;
    case PanelAnchor::Center:
        return (surface_w - panel_w) / 2.0f;
    case PanelAnchor::Right:
        return surface_w - panel_w - kPanelSideMargin;
    }
    return kPanelSideMargin;
}

// card chrome
constexpr float kCardBorderWidth = 2.0f;
constexpr float kCardTopPadding = 10.0f;
constexpr float kCardBottomPadding = 12.0f;
constexpr float kCardHorizontalPadding = 12.0f;
constexpr float kCardHeaderHeight = 32.0f;
constexpr float kCardHeaderContentGap = 8.0f;
constexpr float kCardRadius = 12.0f;
constexpr float kCardGatedHeight = 0.0f;

inline constexpr float kPanelNoBorder[4] = {0, 0, 0, 0};

enum class PanelClickKind {
    Close,
    HeaderToggle,
    HeaderAction,
    ErrorClose,
    RowConnect,
    RowForget,
    SubClose,
    SubConfirm,
    SubCancel,
    SliderDrag,
    MuteToggle,
    DeviceSelect,
    TrayActivate,
    TrayOpenMenu,
    TrayMenuBack,
    TrayMenuEntry,
    TabSelect,
    ToggleFlip,
    FieldFocus,
    WallpaperSelect,
    MonitorSelect,
    RegionSelect,
    AnimatedWallpaperSelect,
    AnimatedRegionSelect,
    MediaPlayPause,
    MediaNext,
    MediaPrevious,
    ProfileSettings,
};
struct PanelClickRegion {
    PanelClickKind kind;
    astralia::ui::Box rect;
    std::string tag;
};

bool panel_region_hit(const std::vector<PanelClickRegion> &click_regions, double x, double y);

namespace panel_chrome_detail {

const Texture *cached_text(TextureCache &cache, const std::string &s, int32_t scale);

const Texture *cached_icon(TextureCache &cache, const char *codepoint, int32_t scale);

const Texture *cached_text_clipped(TextureCache &cache, const std::string &s, int32_t scale, int max_width_px);

const Texture *cached_text_large(TextureCache &cache, const std::string &s, int32_t scale);

} // namespace panel_chrome_detail

Node *panel_draw_box(Node *parent, float x, float y, float w, float h, float border_width = astralia::metrics::border_thin);

struct PanelCardChrome {
    float content_x;
    float content_y;
    float box_h;
};

float panel_card_box_height(float content_h);

PanelCardChrome panel_draw_card(Node *root, TextureCache &tcache, int32_t scale, float x, float y, float w, float content_h, const std::string &title);

float panel_draw_header(Node *parent, TextureCache &cache, int32_t scale, const std::string &title, float panel_x, float panel_y, float panel_w, std::vector<PanelClickRegion> &click_regions);

void panel_draw_row_text(Node *tclip, const Texture *name_tex, const Texture *sub_tex, float row_h, const float *name_color, const float *sub_color);

void panel_draw_centered_text(Node *clip, TextureCache &cache, int32_t scale, const std::string &text, float box_x, float box_y, float box_w, float box_h, const float *color);

astralia::ui::Box panel_draw_toggle_switch(Node *parent, std::vector<PanelClickRegion> &click_regions, float x, float y, float track_w, float track_h, float knob_size, float knob_inset, bool active, PanelClickKind click_kind, const std::string &tag);

struct PanelRowActionLayout {
    float actions_w = 0.0f;
    const Texture *busy_tex = nullptr;
};

PanelRowActionLayout panel_measure_row_actions(TextureCache &cache, int32_t scale, bool is_busy, bool show_forget);

void panel_draw_row_actions(Node *clip, TextureCache &cache, int32_t scale, std::vector<PanelClickRegion> &click_regions, const PanelRowActionLayout &layout, float content_x, float content_w, float y, float row_h, float rx_origin, float ry_origin, bool is_connected, bool is_busy, bool show_forget, const std::string &row_tag);

float panel_confirm_subpanel_height();

float panel_draw_subpanel_top(Node *parent, TextureCache &cache, int32_t scale, const std::string &top_label, float panel_x, float sub_y, float panel_w, float sub_h, std::vector<PanelClickRegion> &click_regions);

void panel_draw_confirm_subpanel(Node *parent, TextureCache &cache, int32_t scale, const std::string &top_label, const std::string &prompt, const std::string &confirm_label, float panel_x, float sub_y, float panel_w, std::vector<PanelClickRegion> &click_regions);
