#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/reactor.h"
#include "core/signal.h"

namespace astralia {

struct PolkitRequestIdentity {
    std::string kind;
    std::uint32_t id = 0;
    std::string user_name;
};

struct PolkitRequest {
    std::string action_id;
    std::string message;
    std::string icon_name;
    std::string cookie;
    std::vector<PolkitRequestIdentity> identities;
};

class PolkitService {
  public:
    explicit PolkitService(Reactor &loop);
    ~PolkitService();
    PolkitService(const PolkitService &) = delete;
    PolkitService &operator=(const PolkitService &) = delete;

    bool pending() const;
    std::string message() const;
    PolkitRequest request() const;
    bool response_required() const;
    bool response_visible() const;
    std::string input_prompt() const;
    std::string info() const;
    bool info_is_error() const;
    void respond(std::string &response);
    void cancel();

    Signal<> changed;
    Signal<bool, const std::string &> ready;

  private:
    struct Impl;

    Reactor &loop_;
    std::unique_ptr<Impl> impl_;
    int source_ = -1;
};

} // namespace astralia
