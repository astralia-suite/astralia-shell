#pragma once

#include <chrono>
#include <ctime>

namespace astralia {

std::tm wall_local_time();
std::chrono::milliseconds
ms_until_next_second(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

} // namespace astralia
