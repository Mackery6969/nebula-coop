// A small JSON reader and string quoting, for NebulaNet's messages (galaxy/nebulanet.h): objects, arrays, strings,
// numbers, true, false and null, nested at most 16 deep. Nothing else in Nebula reads JSON.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::net::json {

// Forward declarations: ValueMembers holds vectors of Value, so Value must be declared first.
struct Value;
struct ValueMembers;

// Value: scalar types live on Value itself; nested arrays/objects live on a pimpl so the
// compiler sees Value as complete before std::vector<Value> and std::vector<std::pair<std::string, Value>> are used.
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::unique_ptr<ValueMembers> members_;

    Value() = default;
    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value();

    // An object's member, or null when there's none (or this isn't an object).
    [[nodiscard]] const Value* find(std::string_view key) const;
    // An object's string member, or `fallback`.
    [[nodiscard]] std::string text(std::string_view key, std::string fallback = {}) const;
    // An object's whole-number member, or `fallback`.
    [[nodiscard]] long long integer(std::string_view key, long long fallback = 0) const;
    // An object's member that is an array of strings (other items left out).
    [[nodiscard]] std::vector<std::string> strings(std::string_view key) const;
};

// Nested-value storage: defined after Value so its vectors can hold Value objects.
struct ValueMembers {
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;
};

// The value `text` holds, or nothing when it isn't JSON (or nests too deep).
[[nodiscard]] std::optional<Value> parse(std::string_view text);
// `text` as a JSON string, quotes included.
[[nodiscard]] std::string quote(std::string_view text);

}  // namespace galaxy::net::json
