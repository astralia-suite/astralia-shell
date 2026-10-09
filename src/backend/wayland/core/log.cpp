#include <cstdarg>
#include <cstdio>
#include <string>

#include "core/log.h"
#include "wayland/core/log.h"

void klog(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string message(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0)
        vsnprintf(message.data(), message.size() + 1, fmt, args);
    va_end(args);
    astralia::log::write("info", message);
}
