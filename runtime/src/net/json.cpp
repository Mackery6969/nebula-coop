#include "galaxy/net/json.h"

#include <cmath>
#include <cstdlib>

namespace galaxy::net::json {

namespace {

constexpr int kMaxDepth = 16;

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    bool value(Value& out, int depth) {
        if (depth > kMaxDepth) return false;
        space();
        if (at_ >= text_.size()) return false;
        const char c = text_[at_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.kind = Value::Kind::String;
            return string(out.string);
        }
        if (word("true")) {
            out.kind = Value::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (word("false")) {
            out.kind = Value::Kind::Bool;
            return true;
        }
        if (word("null")) return true;
        return number(out);
    }

    bool done() {
        space();
        return at_ == text_.size();
    }

private:
    void space() {
        while (at_ < text_.size() && std::string_view(" \t\n\r").find(text_[at_]) != std::string_view::npos) {
            ++at_;
        }
    }

    bool word(std::string_view w) {
        if (text_.substr(at_, w.size()) != w) return false;
        at_ += w.size();
        return true;
    }

    bool number(Value& out) {
        const std::size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') ++at_;
        while (at_ < text_.size() && ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.' ||
                                      text_[at_] == 'e' || text_[at_] == 'E' || text_[at_] == '+' ||
                                      text_[at_] == '-')) {
            ++at_;
        }
        if (at_ == start) return false;
        const std::string digits(text_.substr(start, at_ - start));
        char* end = nullptr;
        out.number = std::strtod(digits.c_str(), &end);
        out.kind = Value::Kind::Number;
        return end == digits.c_str() + digits.size() && std::isfinite(out.number);
    }

    static void put_utf8(std::string& out, unsigned code) {
        if (code < 0x80u) {
            out += static_cast<char>(code);
        } else if (code < 0x800u) {
            out += static_cast<char>(0xC0u | (code >> 6));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        } else if (code < 0x10000u) {
            out += static_cast<char>(0xE0u | (code >> 12));
            out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (code >> 18));
            out += static_cast<char>(0x80u | ((code >> 12) & 0x3Fu));
            out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (code & 0x3Fu));
        }
    }

    bool hex4(unsigned& out) {
        if (at_ + 4u > text_.size()) return false;
        out = 0u;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        ++at_;  // the opening quote
        out.clear();
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20u) return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) return false;
            const char e = text_[at_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = 0u;
                if (!hex4(code)) return false;
                if (code >= 0xD800u && code < 0xDC00u && text_.substr(at_, 2) == "\\u") {
                    at_ += 2;
                    unsigned low = 0u;
                    if (!hex4(low) || low < 0xDC00u || low >= 0xE000u) return false;
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                }
                put_utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool array(Value& out, int depth) {
        ++at_;
        out.kind = Value::Kind::Array;
        space();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return true;
        }
        for (;;) {
            Value item;
            if (!value(item, depth + 1)) return false;
            out.array.push_back(std::move(item));
            space();
            if (at_ >= text_.size()) return false;
            if (text_[at_] == ']') {
                ++at_;
                return true;
            }
            if (text_[at_++] != ',') return false;
        }
    }

    bool object(Value& out, int depth) {
        ++at_;
        out.kind = Value::Kind::Object;
        space();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return true;
        }
        for (;;) {
            space();
            std::string key;
            if (at_ >= text_.size() || text_[at_] != '"' || !string(key)) return false;
            space();
            if (at_ >= text_.size() || text_[at_++] != ':') return false;
            auto item = std::make_unique<Value>();
            if (!value(*item, depth + 1)) return false;
            out.object.emplace_back(std::move(key), std::move(item));
            space();
            if (at_ >= text_.size()) return false;
            if (text_[at_] == '}') {
                ++at_;
                return true;
            }
            if (text_[at_++] != ',') return false;
        }
    }

    std::string_view text_;
    std::size_t at_ = 0u;
};

}  // namespace

const Value* Value::find(std::string_view key) const {
    if (kind != Kind::Object) return nullptr;
    for (const auto& [name, value] : object) {
        if (name == key) return value.get();
    }
    return nullptr;
}

std::string Value::text(std::string_view key, std::string fallback) const {
    const Value* value = find(key);
    return value != nullptr && value->kind == Kind::String ? value->string : fallback;
}

long long Value::integer(std::string_view key, long long fallback) const {
    const Value* value = find(key);
    if (value == nullptr || value->kind != Kind::Number || value->number != std::floor(value->number)) return fallback;
    return static_cast<long long>(value->number);
}

std::vector<std::string> Value::strings(std::string_view key) const {
    std::vector<std::string> out;
    const Value* value = find(key);
    if (value == nullptr || value->kind != Kind::Array) return out;
    for (const Value& item : value->array) {
        if (item.kind == Kind::String) out.push_back(item.string);
    }
    return out;
}

std::optional<Value> parse(std::string_view text) {
    Reader reader(text);
    Value out;
    if (!reader.value(out, 0) || !reader.done()) return std::nullopt;
    return out;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20u) {
                static const char* const kHex = "0123456789abcdef";
                out += "\\u00";
                out += kHex[(c >> 4) & 0xF];
                out += kHex[c & 0xF];
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

}  // namespace galaxy::net::json
