// A small JSON reader and string quoting, for NebulaNet's messages (galaxy/nebulanet.h): objects, arrays, strings,
// numbers, true, false and null, nested at most 16 deep. Nothing else in Nebula reads JSON.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::net::json {

struct Member;

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    // Member is declared after Value: a std::pair<std::string, Value> here would need Value complete already, which
    // libstdc++ rejects under clang.
    std::vector<Member> object;

    // An object's member, or null when there's none (or this isn't an object).
    [[nodiscard]] const Value* find(std::string_view key) const;
    // An object's string member, or `fallback`.
    [[nodiscard]] std::string text(std::string_view key, std::string fallback = {}) const;
    // An object's whole-number member, or `fallback`.
    [[nodiscard]] long long integer(std::string_view key, long long fallback = 0) const;
    // An object's member that is an array of strings (other items left out).
    [[nodiscard]] std::vector<std::string> strings(std::string_view key) const;
};

// An object's member.
struct Member {
    std::string key;
    Value value;
};

// The value `text` holds, or nothing when it isn't JSON (or nests too deep).
[[nodiscard]] std::optional<Value> parse(std::string_view text);
// `text` as a JSON string, quotes included.
[[nodiscard]] std::string quote(std::string_view text);

}  // namespace galaxy::net::json
