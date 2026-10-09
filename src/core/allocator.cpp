#include <malloc.h>

#include "core/allocator.h"

namespace astralia {

namespace {

constexpr int mmap_threshold = 1 << 20;

} // namespace

void tune_allocator(int max_arenas) {
    mallopt(M_ARENA_MAX, max_arenas);
    mallopt(M_MMAP_THRESHOLD, mmap_threshold);
}

} // namespace astralia
