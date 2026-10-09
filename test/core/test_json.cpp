#include "core/json.h"

#include "check.h"

void check_json() {
    using astralia::Json;
    using test::check;
    auto parsed = astralia::parse_json(R"({"a": 1.5, "b": [true, false, null], "c": {"d": "xé\n"}, "e": -3})");
    check(parsed.has_value(), "valid document parses");
    if (!parsed) {
        return;
    }
    check(parsed->number_or("a", 0) == 1.5 && parsed->number_or("e", 0) == -3, "numbers parse");
    const Json *list = parsed->find("b");
    check(list != nullptr && list->array.size() == 3 && list->array[0].boolean && !list->array[1].boolean && list->array[2].type == Json::Type::null, "array of literals");
    const Json *nested = parsed->find("c");
    check(nested != nullptr && nested->string_or("d") == "x\xC3\xA9\n", "escapes decode to UTF-8");
    check(parsed->string_or("missing", "fallback") == "fallback", "missing key falls back");
    check(!astralia::parse_json("{\"a\": }").has_value(), "missing value rejected");
    check(!astralia::parse_json("[1, 2").has_value(), "unterminated array rejected");
    check(!astralia::parse_json("{} x").has_value(), "trailing text rejected");
}

void check_json_write() {
    using astralia::Json;
    using test::check;
    const char *text = R"({"a":1.5,"b":[true,false,null],"c":{"d":"x\"\\\n\t\u0001é"},"e":-3,"f":[],"g":{}})";
    auto parsed = astralia::parse_json(text);
    check(parsed.has_value(), "writer sample parses");
    if (!parsed) {
        return;
    }
    std::string compact = astralia::write_json(*parsed);
    check(compact == R"({"a":1.5,"b":[true,false,null],"c":{"d":"x\"\\\n\t\u0001é"},"e":-3,"f":[],"g":{}})", "compact output keeps order and escapes");
    auto again = astralia::parse_json(astralia::write_json(*parsed, true));
    check(again.has_value() && astralia::write_json(*again) == compact, "pretty output round-trips");
    std::string pretty = astralia::write_json(*parsed, true);
    check(pretty.starts_with("{\n  \"a\": 1.5,\n"), "pretty output indents by two spaces");
    Json whole;
    whole.type = Json::Type::number;
    whole.number = 16.0;
    check(astralia::write_json(whole) == "16", "whole numbers have no fraction");
}
