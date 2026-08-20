#include "lsp/Utf16.h"

#include <cctype>
#include <cstdint>

namespace yona::lsp {
namespace {

std::uint32_t decode_utf8(std::string_view s, std::size_t& i) {
    if (i >= s.size())
        return 0;
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        const auto cp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        const auto cp = ((b0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        const auto cp = ((b0 & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                        ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                        (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

std::size_t utf16_width(std::uint32_t cp) {
    return cp > 0xFFFF ? 2 : 1;
}

} // namespace

Position offset_to_position(std::string_view utf8, std::size_t byte_offset) {
    if (byte_offset > utf8.size())
        byte_offset = utf8.size();
    Position pos;
    std::size_t i = 0;
    while (i < byte_offset) {
        if (utf8[i] == '\n') {
            ++pos.line;
            pos.character = 0;
            ++i;
            continue;
        }
        if (utf8[i] == '\r') {
            ++pos.line;
            pos.character = 0;
            ++i;
            if (i < byte_offset && utf8[i] == '\n')
                ++i;
            continue;
        }
        const auto cp = decode_utf8(utf8, i);
        pos.character += utf16_width(cp);
    }
    return pos;
}

std::size_t position_to_offset(std::string_view utf8, Position pos) {
    std::size_t i = 0;
    std::size_t line = 0;
    while (i < utf8.size() && line < pos.line) {
        if (utf8[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (utf8[i] == '\r') {
            ++line;
            ++i;
            if (i < utf8.size() && utf8[i] == '\n')
                ++i;
            continue;
        }
        decode_utf8(utf8, i);
    }
    std::size_t col = 0;
    while (i < utf8.size() && utf8[i] != '\n' && utf8[i] != '\r' && col < pos.character) {
        const auto start = i;
        const auto cp = decode_utf8(utf8, i);
        col += utf16_width(cp);
        if (col > pos.character)
            return start;
    }
    return i;
}

Range source_to_range(std::string_view utf8, const SourceLocation& loc) {
    Range r;
    r.start = offset_to_position(utf8, loc.offset);
    r.end = offset_to_position(utf8, loc.offset + loc.length);
    if (loc.length == 0 && loc.line > 0) {
        r.start.line = loc.line - 1;
        r.start.character = loc.column > 0 ? loc.column - 1 : 0;
        r.end = r.start;
    }
    return r;
}

std::string file_uri(std::string_view path) {
    std::string norm(path);
#ifdef _WIN32
    for (char& c : norm) {
        if (c == '\\')
            c = '/';
    }
#endif
    std::string out = "file://";
    if (!norm.empty() && norm[0] != '/')
        out += '/';
    for (unsigned char c : norm) {
        if (std::isalnum(c) || c == '/' || c == '_' || c == '-' || c == '.' || c == '~' || c == ':')
            out += static_cast<char>(c);
        else {
            static const char hex[] = "0123456789ABCDEF";
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string uri_to_path(std::string_view uri) {
    std::string_view rest = uri;
    if (rest.starts_with("file://"))
        rest.remove_prefix(7);
    std::string out;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '%' && i + 2 < rest.size()) {
            auto hex = [](char c) {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return 0;
            };
            out += static_cast<char>((hex(rest[i + 1]) << 4) | hex(rest[i + 2]));
            i += 2;
        } else {
            out += rest[i];
        }
    }
#ifdef _WIN32
    if (out.size() >= 3 && out[0] == '/' && std::isalpha(static_cast<unsigned char>(out[1])) && out[2] == ':')
        out.erase(out.begin());
#endif
    return out;
}

} // namespace yona::lsp
