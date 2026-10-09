#include <cassert>

#include "service/wayland/media_service.h"

void test_animated_image() {
    assert(animate_frame_index(0.0f, 15.0f, 36) == 0);
    assert(animate_frame_index(1.0f, 15.0f, 36) == 15);
    assert(animate_frame_index(3.0f, 15.0f, 36) == 9);
    assert(animate_frame_index(1.0f, 15.0f, 1) == 0);
    assert(animate_frame_index(1.0f, 15.0f, 0) == 0);
    assert(animate_frame_index(-1.0f, 15.0f, 36) == 0);
    assert(animate_frame_index(2.0f, 0.0f, 36) == 0);

    assert(kAnimateMaxSeconds * 30 >= 210);
    assert(animate_frame_index(7.0f, 30.0f, 210) == 0);

    AnimateSize below = animate_decode_size(1920, 1080, kAnimateMaxDecodeDim);
    assert(below.w == 1920 && below.h == 1080);

    AnimateSize clamped = animate_decode_size(3840, 2160, kAnimateMaxDecodeDim);
    assert(clamped.h == kAnimateMaxDecodeDim);
    assert(clamped.w == 2560);

    AnimateSize tall = animate_decode_size(1000, 4000, kAnimateMaxDecodeDim);
    assert(tall.h == kAnimateMaxDecodeDim);
    assert(tall.w == 360);

    AnimateSize portrait = animate_fit_size(461, 512, 250);
    assert(portrait.w == 225 && portrait.h == 250);

    AnimateSize square = animate_fit_size(640, 640, 250);
    assert(square.w == 250 && square.h == 250);

    AnimateSize small = animate_fit_size(100, 80, 250);
    assert(small.w == 100 && small.h == 80);

    assert(animate_scale_filter(200, 200, AnimateFit::Crop) == "scale=200:200:force_original_aspect_ratio=increase,crop=200:200,"
                                                               "format=rgba");
    assert(animate_scale_filter(320, 180, AnimateFit::Fit) == "scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:"
                                                              "(ow-iw)/2:(oh-ih)/2:color=black,format=rgba");
}
