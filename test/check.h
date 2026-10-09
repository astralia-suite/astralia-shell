#pragma once

#include <print>
#include <source_location>
#include <string_view>

namespace test {

inline int failures = 0;

inline void check(bool condition, std::string_view what,
                  std::source_location where = std::source_location::current()) {
    if (condition) {
        return;
    }
    ++failures;
    std::println(stderr, "FAIL {}:{}: {}", where.file_name(), where.line(), what);
}

} // namespace test
