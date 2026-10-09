#include <cstring>

#include "config/polkit_config.h"

#include "modules/polkit/model.h"

namespace astralia {

namespace {

namespace cfg = polkit_config;

constexpr float scale_ms = 350.0f;
constexpr uint64_t owner_card_scale = 10;
constexpr uint64_t owner_dots = 1000;

} // namespace

PolkitModel::~PolkitModel() {
    clear_password();
}

void PolkitModel::clear_password() {
    explicit_bzero(field_.text.data(), field_.text.size());
    field_.text.clear();
    field_.preedit.clear();
    field_.error_message.clear();
    text_field_type_anim_clear(dot_anim_, animations_, owner_dots);
}

PolkitPrompt polkit_prompt(const PolkitService &service) {
    PolkitRequest request = service.request();
    PolkitPrompt prompt;
    prompt.message = request.message.empty() ? request.action_id : request.message;
    prompt.needs_input = service.response_required();
    prompt.info = service.info();
    prompt.info_is_error = service.info_is_error();
    return prompt;
}

void PolkitModel::reset() {
    animations_.cancelForOwner(owner_card_scale);
    clear_password();
    open_ = false;
    closing_ = false;
    last_error_ = false;
    card_scale_ = 0.0f;
}

bool PolkitModel::sync(bool pending) {
    if (pending && (!open_ || closing_)) {
        begin_open();
        return true;
    }
    if (!pending && open_ && !closing_) {
        begin_close();
        return true;
    }
    return false;
}

void PolkitModel::begin_open() {
    open_ = true;
    closing_ = false;
    last_error_ = false;
    animations_.animate(card_scale_, 1.0f, scale_ms, astralia::Easing::EaseOutBack, [this](float v) { card_scale_ = v; }, {}, owner_card_scale);
}

void PolkitModel::begin_close() {
    clear_password();
    last_error_ = false;
    closing_ = true;
    animations_.animate(card_scale_, 0.0f, scale_ms, astralia::Easing::EaseInBack, [this](float v) { card_scale_ = v; }, [this] {
        open_ = false;
        closing_ = false;
        if (on_closed) {
            on_closed();
        } }, owner_card_scale);
}

void PolkitModel::set_prompt(PolkitPrompt prompt) {
    if (prompt.info_is_error && !last_error_) {
        field_.error_message = cfg::error_text;
    }
    last_error_ = prompt.info_is_error;
    prompt_ = std::move(prompt);
}

PolkitKey PolkitModel::key(const input::KeyEvent &event) {
    switch (text_field_handle_key(field_, event)) {
    case TextFieldResult::Changed:
        text_field_type_anim_sync(dot_anim_, animations_, owner_dots, field_.text);
        return PolkitKey::changed;
    case TextFieldResult::Committed:
        return field_.text.empty() ? PolkitKey::none : PolkitKey::submit;
    case TextFieldResult::Cancelled:
        return PolkitKey::cancel;
    case TextFieldResult::None:
        break;
    }
    return PolkitKey::none;
}

std::string PolkitModel::take_password() {
    std::string password = std::move(field_.text);
    field_.text.clear();
    text_field_type_anim_clear(dot_anim_, animations_, owner_dots);
    return password;
}

void PolkitModel::tick(std::chrono::steady_clock::time_point now) {
    animations_.tick(now);
}

} // namespace astralia
