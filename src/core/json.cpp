#include <charconv>
#include <cmath>
#include <cstdint>
#include <utility>

#include "core/json.h"

namespace astralia {

namespace {

constexpr int max_depth = 64;

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Json> parse() {
        std::optional<Json> value = parse_value(0);
        skip_space();
        if (!value || pos_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

  private:
    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\t' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(std::string_view literal) {
        if (!text_.substr(pos_).starts_with(literal)) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    std::optional<Json> parse_value(int depth) {
        skip_space();
        if (pos_ >= text_.size() || depth > max_depth) {
            return std::nullopt;
        }
        Json value;
        char c = text_[pos_];
        if (c == '{') {
            return parse_object(depth);
        }
        if (c == '[') {
            return parse_array(depth);
        }
        if (c == '"') {
            value.type = Json::Type::string;
            if (!parse_string(value.string)) {
                return std::nullopt;
            }
            return value;
        }
        if (consume("true")) {
            value.type = Json::Type::boolean;
            value.boolean = true;
            return value;
        }
        if (consume("false")) {
            value.type = Json::Type::boolean;
            return value;
        }
        if (consume("null")) {
            return value;
        }
        const char *first = text_.data() + pos_;
        const char *last = text_.data() + text_.size();
        auto [end, error] = std::from_chars(first, last, value.number);
        if (error != std::errc{} || end == first) {
            return std::nullopt;
        }
        pos_ += static_cast<std::size_t>(end - first);
        value.type = Json::Type::number;
        return value;
    }

    std::optional<Json> parse_object(int depth) {
        Json value;
        value.type = Json::Type::object;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return value;
        }
        while (true) {
            skip_space();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"' || !parse_string(key)) {
                return std::nullopt;
            }
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return std::nullopt;
            }
            ++pos_;
            std::optional<Json> member = parse_value(depth + 1);
            if (!member) {
                return std::nullopt;
            }
            value.object.push_back({std::move(key), std::move(*member)});
            skip_space();
            if (pos_ >= text_.size()) {
                return std::nullopt;
            }
            char c = text_[pos_++];
            if (c == '}') {
                return value;
            }
            if (c != ',') {
                return std::nullopt;
            }
        }
    }

    std::optional<Json> parse_array(int depth) {
        Json value;
        value.type = Json::Type::array;
        ++pos_;
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return value;
        }
        while (true) {
            std::optional<Json> element = parse_value(depth + 1);
            if (!element) {
                return std::nullopt;
            }
            value.array.push_back(std::move(*element));
            skip_space();
            if (pos_ >= text_.size()) {
                return std::nullopt;
            }
            char c = text_[pos_++];
            if (c == ']') {
                return value;
            }
            if (c != ',') {
                return std::nullopt;
            }
        }
    }

    static void append_utf8(std::string &out, uint32_t code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool parse_hex4(uint32_t &code) {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        auto [end, error] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, code, 16);
        if (error != std::errc{} || end != text_.data() + pos_ + 4) {
            return false;
        }
        pos_ += 4;
        return true;
    }

    bool parse_string(std::string &out) {
        ++pos_;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            char escape = text_[pos_++];
            switch (escape) {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'u': {
                uint32_t code = 0;
                if (!parse_hex4(code)) {
                    return false;
                }
                if (code >= 0xD800 && code < 0xDC00 && text_.substr(pos_).starts_with("\\u")) {
                    pos_ += 2;
                    uint32_t low = 0;
                    if (!parse_hex4(low) || low < 0xDC00 || low >= 0xE000) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                append_utf8(out, code);
                break;
            }
            default:
                out += escape;
                break;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

Json Json::from_bool(bool value) {
    Json json;
    json.type = Type::boolean;
    json.boolean = value;
    return json;
}

Json Json::from_number(double value) {
    Json json;
    json.type = Type::number;
    json.number = value;
    return json;
}

Json Json::from_string(std::string value) {
    Json json;
    json.type = Type::string;
    json.string = std::move(value);
    return json;
}

Json Json::make_array() {
    Json json;
    json.type = Type::array;
    return json;
}

Json Json::make_object() {
    Json json;
    json.type = Type::object;
    return json;
}

Json &Json::set(std::string key, Json value) {
    type = Type::object;
    for (Member &member : object) {
        if (member.key == key) {
            member.value = std::move(value);
            return member.value;
        }
    }
    object.push_back({std::move(key), std::move(value)});
    return object.back().value;
}

void Json::push(Json value) {
    type = Type::array;
    array.push_back(std::move(value));
}

const Json *Json::find(std::string_view key) const {
    for (const Member &member : object) {
        if (member.key == key) {
            return &member.value;
        }
    }
    return nullptr;
}

double Json::number_or(std::string_view key, double fallback) const {
    const Json *value = find(key);
    return value != nullptr && value->type == Type::number ? value->number : fallback;
}

std::string Json::string_or(std::string_view key, std::string_view fallback) const {
    const Json *value = find(key);
    return value != nullptr && value->type == Type::string ? value->string : std::string(fallback);
}

bool Json::boolean_or(std::string_view key, bool fallback) const {
    const Json *value = find(key);
    return value != nullptr && value->type == Type::boolean ? value->boolean : fallback;
}

std::optional<Json> parse_json(std::string_view text) {
    return Parser(text).parse();
}

namespace {

void write_string(std::string &out, std::string_view text) {
    out += '"';
    for (char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out += digits[(c >> 4) & 0xf];
                out += digits[c & 0xf];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void write_number(std::string &out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buffer[32];
    auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

void write_value(std::string &out, const Json &value, bool pretty, int depth) {
    auto newline = [&](int level) {
        if (pretty) {
            out += '\n';
            out.append(static_cast<std::size_t>(level) * 2, ' ');
        }
    };
    switch (value.type) {
    case Json::Type::null:
        out += "null";
        break;
    case Json::Type::boolean:
        out += value.boolean ? "true" : "false";
        break;
    case Json::Type::number:
        write_number(out, value.number);
        break;
    case Json::Type::string:
        write_string(out, value.string);
        break;
    case Json::Type::array:
        if (value.array.empty()) {
            out += "[]";
            break;
        }
        out += '[';
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            newline(depth + 1);
            write_value(out, value.array[i], pretty, depth + 1);
        }
        newline(depth);
        out += ']';
        break;
    case Json::Type::object:
        if (value.object.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (std::size_t i = 0; i < value.object.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            newline(depth + 1);
            write_string(out, value.object[i].key);
            out += pretty ? ": " : ":";
            write_value(out, value.object[i].value, pretty, depth + 1);
        }
        newline(depth);
        out += '}';
        break;
    }
}

} // namespace

std::string write_json(const Json &value, bool pretty) {
    std::string out;
    write_value(out, value, pretty, 0);
    return out;
}

} // namespace astralia
