#include "render/app_icon.h"

namespace astralia {

IconSurface load_app_icon(const std::string &path, int size) {
    if (path.empty()) {
        return nullptr;
    }
    auto surface = decode_image(path, size);
    return surface ? std::move(*surface) : nullptr;
}

} // namespace astralia
