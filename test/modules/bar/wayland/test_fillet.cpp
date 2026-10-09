#include <cassert>

#include "modules/bar/wayland/frame.h"

namespace {

uint8_t alpha_at(const std::vector<uint8_t> &rgba, int size, int x, int y) {
    return rgba[(static_cast<size_t>(y) * size + x) * 4 + 3];
}

} // namespace

void test_bar_fillet() {
    const int size = 12;

    std::vector<uint8_t> left = fillet_rgba(size, false);
    assert(left.size() == static_cast<size_t>(size) * size * 4);
    assert(alpha_at(left, size, size - 1, 0) == 255);
    assert(alpha_at(left, size, size - 1, 2) == 255);
    assert(alpha_at(left, size, 0, size - 1) == 0);
    assert(alpha_at(left, size, 1, size - 2) == 0);

    std::vector<uint8_t> right = fillet_rgba(size, true);
    assert(alpha_at(right, size, 0, 0) == 255);
    assert(alpha_at(right, size, 0, 2) == 255);
    assert(alpha_at(right, size, size - 1, size - 1) == 0);
    assert(alpha_at(right, size, size - 2, size - 2) == 0);

    for (size_t i = 0; i < left.size(); i += 4)
        assert(left[i] == 255 && left[i + 1] == 255 && left[i + 2] == 255);
}
