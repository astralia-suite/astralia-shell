#pragma once

#include <string>

namespace astralia {

std::string user_parse_os_name(const std::string &os_release);

class UserService {
  public:
    UserService();

    const std::string &name() const { return name_; }
    std::string os_name() const;
    std::string uptime() const;
    std::string face_path() const;

  private:
    std::string name_;
};

} // namespace astralia
