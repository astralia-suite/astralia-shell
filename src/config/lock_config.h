#pragma once

#include <cstdint>

namespace astralia::lock_config {

// development unlock corner
inline constexpr float dev_unlock_width = 140.0f;
inline constexpr float dev_unlock_height = 48.0f;

// layer namespace
inline constexpr const char *layer_namespace = "astralia-shell-lock";

// background & dot
inline constexpr float bg_border_width = 5.0f;

inline constexpr float dot_size = 18.0f;

// fonts
inline constexpr float font_icon = 200.0f;
inline constexpr float font_clock = 120.0f;
inline constexpr float font_date = 34.0f;
inline constexpr float font_normal = 20.0f;
inline constexpr float font_mono = 18.0f;

// card layout
inline constexpr float card_height_mult = 0.7f;
inline constexpr float card_ratio = 16.0f / 9.0f;
inline constexpr float panel_gap = 25.0f;
inline constexpr float center_width = 560.0f;
inline constexpr float center_ref_height = 1440.0f;

// icon box
inline constexpr float icon_box_margin = 20.0f;

// clock/date/avatar/input gaps
inline constexpr float clock_gap = 10.0f;
inline constexpr float gap_clock_date = 6.0f;
inline constexpr float gap_date_avatar = 40.0f;
inline constexpr float gap_avatar_input = 34.0f;
inline constexpr float gap_input_message = 18.0f;

// input & pill
inline constexpr float input_height = 56.0f;
inline constexpr float input_width_frac = 0.85f;
inline constexpr float pill_pad = 12.0f;
inline constexpr float pill_icon_size = 26.0f;
inline constexpr float pill_button_size = 44.0f;

// profile
inline constexpr float profile_border_width = 4.0f;
inline constexpr float profile_size = 180.0f;

// side panel
inline constexpr float side_panel_pad = 20.0f;

// fetch
inline constexpr float fetch_line_gap = 6.0f;
inline constexpr float fetch_chip_pad = 8.0f;
inline constexpr float fetch_color_box = 26.0f;
inline constexpr float fetch_color_gap = 10.0f;

// card chrome
inline constexpr float card_border_width = 2.0f;
inline constexpr float card_header_gap = 12.0f;
inline constexpr float card_tag_pad = 6.0f;
inline constexpr float battery_row_gap = 8.0f;
inline constexpr float battery_icon_gap = 10.0f;
inline constexpr float battery_bar_height = 6.0f;
inline constexpr float battery_bar_radius = 3.0f;

// media
inline constexpr float media_art = 116.0f;
inline constexpr float media_btn_size = 40.0f;
inline constexpr float media_btn_gap = 12.0f;
inline constexpr float media_text_gap = 4.0f;

// resource gauges
inline constexpr float res_tile_gap = 14.0f;
inline constexpr float res_icon_size = 26.0f;
inline constexpr float res_value_font = 32.0f;
inline constexpr float res_temp_warn_c = 85.0f;
inline constexpr float res_gauge_stroke_ratio = 6.0f / 68.0f;
inline constexpr float res_gauge_icon_value_gap = 2.0f;
inline constexpr float res_gauge_label_gap = 6.0f;
inline constexpr const char *res_gauge_gpu_color_hex = "#3FB6C8";

// notifications
inline constexpr float notif_card_gap = 8.0f;
inline constexpr float notif_card_pad = 12.0f;
inline constexpr int notif_max_cards = 6;

// scale state
inline constexpr float scale_hidden = 0.0f;
inline constexpr float scale_full = 1.0f;

// animation durations
inline constexpr float anim_fast_ms = 100.0f;

// transition timeline in milliseconds
inline constexpr float slash_ms = 2800.0f;
inline constexpr int star_count = 5;
inline constexpr int flurry_count = 14;
inline constexpr int slash_count = star_count + flurry_count;
inline constexpr float slash_first_start_ms = 40.0f;
inline constexpr float slash_strike_ms = 260.0f;
inline constexpr float star_advance_ms = 100.0f;
inline constexpr float slash_wait_ms = 600.0f;
inline constexpr float slash_final_draw_ms = 70.0f;
inline constexpr float slash_final_fade_ms = 280.0f;
inline constexpr float slash_impact_ms = 60.0f;
inline constexpr float slash_shock_ms = 200.0f;
inline constexpr float slash_flash_hold_ms = 60.0f;
inline constexpr float slash_flash_ms = 400.0f;
inline constexpr float slash_final_beam_ms = 280.0f;
inline constexpr float slash_sliver_ms = 420.0f;
inline constexpr float slash_dim_ramp_ms = 300.0f;
inline constexpr float slash_dim_lift_ms = 320.0f;
inline constexpr float slash_shatter_hold_ms = 25.0f;
inline constexpr float slash_shatter_spread_ms = 200.0f;
inline constexpr float slash_fall_ms = 320.0f;

// transition star
struct StarCorner {
    float x;
    float y;
};

inline constexpr StarCorner star_corners[star_count] = {
    {0.50f, 0.24f}, {0.78f, 0.77f}, {0.24f, 0.36f}, {0.80f, 0.36f}, {0.16f, 0.79f},
};

// transition flurry
struct FlurrySlash {
    float degrees;
    float offset;
    float start_ms;
};

inline constexpr float reference_width = 16.0f;
inline constexpr float reference_height = 9.0f;
inline constexpr FlurrySlash flurry_slashes[flurry_count] = {
    {-89.0f, 0.28f, 480.0f},  {34.0f, 0.16f, 560.0f},    {-10.0f, -0.22f, 720.0f},
    {-32.0f, 0.16f, 800.0f},  {-63.0f, -0.29f, 830.0f},  {11.0f, 0.26f, 880.0f},
    {77.0f, -0.30f, 910.0f},  {53.0f, 0.34f, 960.0f},    {-79.0f, -0.34f, 1040.0f},
    {69.0f, 0.30f, 1070.0f},  {-71.0f, -0.30f, 1120.0f}, {-55.0f, 0.23f, 1200.0f},
    {85.0f, -0.29f, 1220.0f}, {61.0f, 0.25f, 1240.0f},
};

// transition timeline as shares of progress
inline constexpr float slash_first_start = slash_first_start_ms / slash_ms;
inline constexpr float slash_draw_span = slash_strike_ms / slash_ms;
inline constexpr float slash_final_start =
    (slash_first_start_ms + flurry_slashes[flurry_count - 1].start_ms + slash_strike_ms +
     slash_wait_ms) /
    slash_ms;
inline constexpr float slash_final_draw_span = slash_final_draw_ms / slash_ms;
inline constexpr float slash_final_fade = slash_final_fade_ms / slash_ms;
inline constexpr float slash_impact_span = slash_impact_ms / slash_ms;
inline constexpr float slash_shock_span = slash_shock_ms / slash_ms;
inline constexpr float slash_flash_hold = slash_flash_hold_ms / slash_ms;
inline constexpr float slash_flash_span = slash_flash_ms / slash_ms;
inline constexpr float slash_final_beam_span = slash_final_beam_ms / slash_ms;
inline constexpr float slash_sliver_span = slash_sliver_ms / slash_ms;
inline constexpr float slash_dim_ramp = slash_dim_ramp_ms / slash_ms;
inline constexpr float slash_dim_lift = slash_dim_lift_ms / slash_ms;
inline constexpr float slash_shatter_hold = slash_shatter_hold_ms / slash_ms;
inline constexpr float slash_shatter_spread = slash_shatter_spread_ms / slash_ms;
inline constexpr float slash_fall_span = slash_fall_ms / slash_ms;

// transition geometry
inline constexpr float slash_gap_frac = 0.012f;
inline constexpr float slash_min_piece_area = 1.0f;
inline constexpr float slash_final_reach = 0.05f;
inline constexpr float final_tilt = 0.14f;
inline constexpr int sliver_count = 36;
inline constexpr float sliver_length_min = 24.0f;
inline constexpr float sliver_length_max = 80.0f;
inline constexpr float sliver_thick_min = 2.0f;
inline constexpr float sliver_thick_max = 4.0f;
inline constexpr float sliver_spread_frac = 0.14f;
inline constexpr float sliver_drift_frac = 0.10f;
inline constexpr float sliver_along_frac = 0.12f;
inline constexpr float slash_strike_overshoot = 0.05f;
inline constexpr float slash_shard_drift = 0.06f;
inline constexpr float slash_shard_spin = 0.25f;
inline constexpr float slash_shard_rank_noise = 0.25f;
inline constexpr int slash_ray_count = 18;
inline constexpr float slash_ray_half_angle = 0.012f;

// transition thunder
inline constexpr float strike_amp = 13.0f;
inline constexpr float strike_peak = 1.2f;
inline constexpr float final_thick = 3.4f;
inline constexpr float final_amp_mult = 2.4f;
inline constexpr float final_intensity = 2.8f;
inline constexpr float final_seed = 21.0f;
inline constexpr float shock_radius_frac = 0.6f;
inline constexpr float shock_intensity = 1.3f;

// transition paint
inline constexpr float cutmark_width = 4.0f;
inline constexpr float cutmark_alpha = 0.85f;
inline constexpr float fallback_bolt_width = 3.0f;
inline constexpr float bolt_glow_near_scale = 2.8f;
inline constexpr float bolt_glow_far_scale = 6.0f;
inline constexpr float bolt_glow_near_alpha = 0.35f;
inline constexpr float bolt_glow_far_alpha = 0.15f;
inline constexpr float slash_ray_alpha = 0.5f;
inline constexpr float slash_flash_peak = 0.9f;
inline constexpr float dim_peak = 0.5f;
inline constexpr const char *dim_hex = "#0B0620";
inline constexpr const char *flash_hex = "#EDE6FF";
inline constexpr float beam_peak_frac = 0.05f;
inline constexpr float final_beam_frac = 0.3f;
inline constexpr float final_thin_frac = 0.012f;
inline constexpr float beam_glow_scale = 1.8f;
inline constexpr float beam_glow_alpha = 0.3f;
inline constexpr float sliver_alpha = 0.9f;

// fail timer
inline constexpr float timer_fail_ms = 3000.0f;

// text strings
inline constexpr const char *fail_text = "Skill Issue";
inline constexpr const char *placeholder_text = "Enter your password";
inline constexpr const char *loading_text = "Loading...";

// assets
inline constexpr const char *echo_asset = "polkit/electro.png";

// x11 tick
inline constexpr int tick_ms = 1000;
inline constexpr int grab_attempts = 100;
inline constexpr int grab_retry_ms = 10;

enum AnimOwner : uint64_t {
    owner_dot_row_x = 8,
    owner_avatar_frame = 10,
    owner_dot_base = 1000,
};

} // namespace astralia::lock_config
