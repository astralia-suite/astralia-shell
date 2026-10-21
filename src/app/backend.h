#pragma once

namespace astralia {

class Backend {
  public:
    virtual ~Backend() = default;

    virtual const char *name() const = 0;
    virtual int malloc_arenas() const = 0;
    virtual int run() = 0;
};

inline constexpr const char *backend_factory_symbol = "astralia_backend_create";

using BackendFactory = Backend *(*)();

} // namespace astralia
