#include "yona/runtime/utf16.h"

#include <string.h>

static uint32_t yona_utf8_decode(const char* s, size_t n, size_t* i) {
    if (*i >= n)
        return 0;
    const unsigned char b0 = (unsigned char)s[*i];
    if (b0 < 0x80) {
        ++*i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && *i + 1 < n) {
        const uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)((unsigned char)s[*i + 1] & 0x3F);
        *i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && *i + 2 < n) {
        const uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)((unsigned char)s[*i + 1] & 0x3F) << 6) |
                            (uint32_t)((unsigned char)s[*i + 2] & 0x3F);
        *i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && *i + 3 < n) {
        const uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)((unsigned char)s[*i + 1] & 0x3F) << 12) |
                            ((uint32_t)((unsigned char)s[*i + 2] & 0x3F) << 6) |
                            (uint32_t)((unsigned char)s[*i + 3] & 0x3F);
        *i += 4;
        return cp;
    }
    ++*i;
    return 0xFFFD;
}

static size_t yona_utf16_width(uint32_t cp) {
    return cp > 0xFFFFu ? 2u : 1u;
}

void yona_utf8_offset_to_utf16(const char* utf8, size_t nbytes, size_t byte_offset, int64_t* out_line,
                               int64_t* out_character) {
    int64_t line = 0;
    int64_t character = 0;
    if (!utf8) {
        if (out_line)
            *out_line = 0;
        if (out_character)
            *out_character = 0;
        return;
    }
    if (byte_offset > nbytes)
        byte_offset = nbytes;
    size_t i = 0;
    while (i < byte_offset) {
        if (utf8[i] == '\n') {
            ++line;
            character = 0;
            ++i;
            continue;
        }
        if (utf8[i] == '\r') {
            ++line;
            character = 0;
            ++i;
            if (i < byte_offset && utf8[i] == '\n')
                ++i;
            continue;
        }
        const uint32_t cp = yona_utf8_decode(utf8, nbytes, &i);
        character += (int64_t)yona_utf16_width(cp);
    }
    if (out_line)
        *out_line = line;
    if (out_character)
        *out_character = character;
}

size_t yona_utf16_position_to_utf8(const char* utf8, size_t nbytes, int64_t line, int64_t character) {
    if (!utf8)
        return 0;
    size_t i = 0;
    int64_t cur_line = 0;
    while (i < nbytes && cur_line < line) {
        if (utf8[i] == '\n') {
            ++cur_line;
            ++i;
            continue;
        }
        if (utf8[i] == '\r') {
            ++cur_line;
            ++i;
            if (i < nbytes && utf8[i] == '\n')
                ++i;
            continue;
        }
        yona_utf8_decode(utf8, nbytes, &i);
    }
    int64_t col = 0;
    while (i < nbytes && utf8[i] != '\n' && utf8[i] != '\r' && col < character) {
        const size_t start = i;
        const uint32_t cp = yona_utf8_decode(utf8, nbytes, &i);
        col += (int64_t)yona_utf16_width(cp);
        if (col > character)
            return start;
    }
    return i;
}

int64_t yona_utf8_offset_to_utf16_line(const char* utf8, int64_t byte_offset) {
    int64_t line = 0, character = 0;
    const char* s = utf8 ? utf8 : "";
    const size_t n = strlen(s);
    const size_t off = byte_offset < 0 ? 0 : (size_t)byte_offset;
    yona_utf8_offset_to_utf16(s, n, off, &line, &character);
    return line;
}

int64_t yona_utf8_offset_to_utf16_character(const char* utf8, int64_t byte_offset) {
    int64_t line = 0, character = 0;
    const char* s = utf8 ? utf8 : "";
    const size_t n = strlen(s);
    const size_t off = byte_offset < 0 ? 0 : (size_t)byte_offset;
    yona_utf8_offset_to_utf16(s, n, off, &line, &character);
    return character;
}

int64_t yona_utf16_position_to_utf8_offset(const char* utf8, int64_t line, int64_t character) {
    const char* s = utf8 ? utf8 : "";
    return (int64_t)yona_utf16_position_to_utf8(s, strlen(s), line, character);
}

int64_t yona_Std_Utf16__offsetToLine(const char* utf8, int64_t byte_offset) {
    return yona_utf8_offset_to_utf16_line(utf8, byte_offset);
}

int64_t yona_Std_Utf16__offsetToCharacter(const char* utf8, int64_t byte_offset) {
    return yona_utf8_offset_to_utf16_character(utf8, byte_offset);
}

int64_t yona_Std_Utf16__positionToOffset(const char* utf8, int64_t line, int64_t character) {
    return yona_utf16_position_to_utf8_offset(utf8, line, character);
}
