#pragma once

namespace astralia::lock {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

float icon_box_size();

float card_width(float output_h);
float card_height(float output_h);
float center_scale(float output_h);

void columns(float card_w, float card_h, float center_w, Rect &left, Rect &center, Rect &right);

float side_card_height(float column_h);

float content_height(float clock_h, float date_h, float message_h);

int fetch_colour_count(float available_w, int max_count);

float dot_row_width(int count);
float dot_x(int index, int count, float field_width);

bool dev_unlock_hit(double x, double y, double output_h);

void panel_origin(float output_w, float output_h, float panel_w, float panel_h, float &out_x,
                  float &out_y);

} // namespace astralia::lock
