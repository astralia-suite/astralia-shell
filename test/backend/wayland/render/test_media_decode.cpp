#include <cassert>

#include "service/wayland/media_service.h"

void test_media_decode() {
    AnimateSize below = animate_decode_size(1920, 1080, animate_max_decode_dim);
    assert(below.w == 1920 && below.h == 1080);

    AnimateSize clamped = animate_decode_size(3840, 2160, animate_max_decode_dim);
    assert(clamped.h == animate_max_decode_dim);
    assert(clamped.w == 2560);

    AnimateSize tall = animate_decode_size(1000, 4000, animate_max_decode_dim);
    assert(tall.h == animate_max_decode_dim);
    assert(tall.w == 360);

    assert(animate_scale_filter(200, 200, AnimateFit::Crop) ==
           "scale=200:200:force_original_aspect_ratio=increase,crop=200:200,"
           "format=rgba");
    assert(animate_scale_filter(320, 180, AnimateFit::Fit) ==
           "scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:"
           "(ow-iw)/2:(oh-ih)/2:color=black,format=rgba");
}
