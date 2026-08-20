#include "lsp/Json.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace yona::lsp {
namespace {

struct Parser {
    std::string_view s;
    std::size_t i = 0;
    std::string* err = nullptr;

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }
    bool fail(const char* msg) {
        if (err)
            *err = msg;
        return false;
    }
    int depth = 0;
    static constexpr int kMaxDepth = 64;

    bool parse_value(Json& out);
    bool parse_string(std::string& out);
};

bool Parser::parse_string(std::string& out) {
    if (i >= s.size() || s[i] != '"')
        return fail("expected string");
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"')
            return true;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                auto hex4 = [&](std::size_t at) -> int {
                    if (at + 4 > s.size())
                        return -1;
                    int v = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[at + k];
                        int d = 0;
                        if (h >= '0' && h <= '9')
                            d = h - '0';
                        else if (h >= 'a' && h <= 'f')
                            d = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F')
                            d = h - 'A' + 10;
                        else
                            return -1;
                        v = (v << 4) | d;
                    }
                    return v;
                };
                const int cu = hex4(i);
                if (cu < 0)
                    return fail("bad unicode escape");
                i += 4;
                auto cp = static_cast<std::uint32_t>(cu);
                if (cu >= 0xD800 && cu <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' &&
                    s[i + 1] == 'u') {
                    const int cu2 = hex4(i + 2);
                    if (cu2 >= 0xDC00 && cu2 <= 0xDFFF) {
                        cp = 0x10000u + ((static_cast<std::uint32_t>(cu - 0xD800) << 10) |
                                         static_cast<std::uint32_t>(cu2 - 0xDC00));
                        i += 6;
                    }
                }
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                out += e;
                break;
            }
        } else {
            out += c;
        }
    }
    return fail("unterminated string");
}

bool Parser::parse_value(Json& out) {
    skip();
    if (i >= s.size())
        return fail("unexpected end");
    char c = s[i];
    if (c == 'n') {
        if (s.substr(i, 4) != "null")
            return fail("bad null");
        i += 4;
        out = nullptr;
        return true;
    }
    if (c == 't') {
        if (s.substr(i, 4) != "true")
            return fail("bad true");
        i += 4;
        out = true;
        return true;
    }
    if (c == 'f') {
        if (s.substr(i, 5) != "false")
            return fail("bad false");
        i += 5;
        out = false;
        return true;
    }
    if (c == '"') {
        std::string str;
        if (!parse_string(str))
            return false;
        out = std::move(str);
        return true;
    }
    if (c == '[') {
        if (depth >= kMaxDepth)
            return fail("json too deep");
        ++depth;
        ++i;
        Json::Array arr;
        skip();
        if (i < s.size() && s[i] == ']') {
            ++i;
            --depth;
            out = std::move(arr);
            return true;
        }
        while (true) {
            Json item;
            if (!parse_value(item))
                return false;
            arr.push_back(std::move(item));
            skip();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                --depth;
                out = std::move(arr);
                return true;
            }
            return fail("expected ]");
        }
    }
    if (c == '{') {
        if (depth >= kMaxDepth)
            return fail("json too deep");
        ++depth;
        ++i;
        Json::Object obj;
        skip();
        if (i < s.size() && s[i] == '}') {
            ++i;
            --depth;
            out = std::move(obj);
            return true;
        }
        while (true) {
            skip();
            std::string key;
            if (!parse_string(key))
                return false;
            skip();
            if (i >= s.size() || s[i] != ':')
                return fail("expected :");
            ++i;
            Json val;
            if (!parse_value(val))
                return false;
            obj.emplace(std::move(key), std::move(val));
            skip();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                --depth;
                out = std::move(obj);
                return true;
            }
            return fail("expected }");
        }
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        std::size_t start = i;
        if (c == '-')
            ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;
        bool is_float = false;
        if (i < s.size() && s[i] == '.') {
            is_float = true;
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            is_float = true;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                ++i;
        }
        auto tok = std::string(s.substr(start, i - start));
        if (is_float)
            out = std::strtod(tok.c_str(), nullptr);
        else
            out = static_cast<std::int64_t>(std::strtoll(tok.c_str(), nullptr, 10));
        return true;
    }
    return fail("unexpected token");
}

void dump_string(std::ostream& os, const std::string& s) {
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':
            os << "\\\"";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '\n':
            os << "\\n";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\t':
            os << "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                os << buf;
            } else {
                os << static_cast<char>(c);
            }
        }
    }
    os << '"';
}

} // namespace

Json Json::parse(std::string_view text, std::string* error) {
    Parser p{text, 0, error};
    Json out;
    if (!p.parse_value(out))
        return nullptr;
    return out;
}

bool Json::as_bool(bool fallback) const {
    if (auto* b = std::get_if<bool>(&data_))
        return *b;
    return fallback;
}

std::int64_t Json::as_int(std::int64_t fallback) const {
    if (auto* n = std::get_if<std::int64_t>(&data_))
        return *n;
    if (auto* d = std::get_if<double>(&data_))
        return static_cast<std::int64_t>(*d);
    return fallback;
}

std::string Json::as_string() const {
    if (auto* s = std::get_if<std::string>(&data_))
        return *s;
    return {};
}

const Json::Array& Json::as_array() const {
    static const Array empty;
    if (auto* a = std::get_if<Array>(&data_))
        return *a;
    return empty;
}

const Json::Object& Json::as_object() const {
    static const Object empty;
    if (auto* o = std::get_if<Object>(&data_))
        return *o;
    return empty;
}

Json Json::get(const std::string& key) const {
    if (auto* o = std::get_if<Object>(&data_)) {
        auto it = o->find(key);
        if (it != o->end())
            return it->second;
    }
    return nullptr;
}

bool Json::has(const std::string& key) const {
    if (auto* o = std::get_if<Object>(&data_))
        return o->contains(key);
    return false;
}

Json& Json::operator[](const std::string& key) {
    if (!std::holds_alternative<Object>(data_))
        data_ = Object{};
    return std::get<Object>(data_)[key];
}

const Json& Json::operator[](const std::string& key) const {
    static const Json none;
    if (auto* o = std::get_if<Object>(&data_)) {
        auto it = o->find(key);
        if (it != o->end())
            return it->second;
    }
    return none;
}

std::string Json::dump() const {
    std::ostringstream os;
    if (is_null())
        os << "null";
    else if (is_bool())
        os << (as_bool() ? "true" : "false");
    else if (auto* n = std::get_if<std::int64_t>(&data_))
        os << *n;
    else if (auto* d = std::get_if<double>(&data_))
        os << *d;
    else if (auto* s = std::get_if<std::string>(&data_))
        dump_string(os, *s);
    else if (auto* a = std::get_if<Array>(&data_)) {
        os << '[';
        for (std::size_t i = 0; i < a->size(); ++i) {
            if (i)
                os << ',';
            os << (*a)[i].dump();
        }
        os << ']';
    } else if (auto* o = std::get_if<Object>(&data_)) {
        os << '{';
        bool first = true;
        for (const auto& [k, v] : *o) {
            if (!first)
                os << ',';
            first = false;
            dump_string(os, k);
            os << ':';
            os << v.dump();
        }
        os << '}';
    }
    return os.str();
}

} // namespace yona::lsp
