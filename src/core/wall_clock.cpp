#include "core/wall_clock.h"

namespace astralia {

std::tm wall_local_time() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    return local;
}

std::chrono::milliseconds ms_until_next_second(std::chrono::system_clock::time_point now) {
    auto next = std::chrono::floor<std::chrono::seconds>(now) + std::chrono::seconds(1);
    return std::chrono::ceil<std::chrono::milliseconds>(next - now);
}

} // namespace astralia
