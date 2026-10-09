#pragma once

#include <functional>
#include <string>

#include "core/animation.h"
#include "core/input.h"

#include "ui/marquee.h"
#include "ui/text_field.h"

#include "service/polkit_service.h"

namespace astralia {

struct PolkitPrompt {
    std::string message;
    bool needs_input = false;
    std::string info;
    bool info_is_error = false;
};

enum class PolkitKey { none,
                       changed,
                       submit,
                       cancel };

PolkitPrompt polkit_prompt(const PolkitService &service);

class PolkitModel {
  public:
    PolkitModel() = default;
    ~PolkitModel();
    PolkitModel(const PolkitModel &) = delete;
    PolkitModel &operator=(const PolkitModel &) = delete;

    bool sync(bool pending);
    void reset();
    void set_prompt(PolkitPrompt prompt);
    PolkitKey key(const input::KeyEvent &event);
    std::string take_password();
    void tick(std::chrono::steady_clock::time_point now);

    bool open() const { return open_; }
    bool closing() const { return closing_; }
    bool animating() const { return animations_.hasActive(); }
    float card_scale() const { return card_scale_; }
    const PolkitPrompt &prompt() const { return prompt_; }
    const std::string &password() const { return field_.text; }
    const std::string &error() const { return field_.error_message; }
    bool show_info() const { return !prompt_.info.empty() && !prompt_.info_is_error; }
    const TextFieldTypeAnim &dot_anim() const { return dot_anim_; }
    MarqueeTextState &marquee() { return marquee_; }
    AnimationManager &animations() { return animations_; }

    std::function<void()> on_closed;

  private:
    void begin_open();
    void begin_close();
    void clear_password();

    bool open_ = false;
    bool closing_ = false;
    float card_scale_ = 0.0f;
    bool last_error_ = false;
    PolkitPrompt prompt_;
    TextFieldState field_;
    TextFieldTypeAnim dot_anim_;
    MarqueeTextState marquee_;
    AnimationManager animations_;
};

} // namespace astralia
