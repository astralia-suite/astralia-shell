#pragma once

#include <functional>
#include <string>

namespace astralia {

class Clipboard {
  public:
    virtual ~Clipboard() = default;

    virtual void set(std::string text) = 0;
    virtual void request(std::function<void(std::string)> done) = 0;
};

} // namespace astralia
