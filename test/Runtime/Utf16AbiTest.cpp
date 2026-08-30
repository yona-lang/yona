/*
 * UTF-8 ↔ UTF-16 offset conversion — cases copied from test/Lsp/LspTest.cpp
 * (do not edit that file). A future Yona yls uses this C ABI / Std\Utf16.
 */

#include "yona/Runtime/Codecs/Utf16.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

TEST_SUITE("Utf16Abi") {

  TEST_CASE("UTF-16 mapper: ASCII") {
    const char *s = "ab\ncd";
    int64_t line = -1, character = -1;
    YonaRuntimeUtf8OffsetToUtf16(s, 5, 4, &line, &character);
    CHECK(line == 1);
    CHECK(character == 1);
    CHECK(YonaRuntimeUtf16PositionToUtf8(s, 5, 1, 1) == 4);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Line(s, 4) == 1);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Character(s, 4) == 1);
    CHECK(YonaRuntimeUtf16PositionToUtf8Offset(s, 1, 1) == 4);
  }

  TEST_CASE("UTF-16 mapper: non-BMP emoji") {
    // U+1F600 is F0 9F 98 80 — one codepoint, two UTF-16 units
    std::string s = "a\xF0\x9F\x98\x80"
                    "b";
    int64_t line = -1, character = -1;
    YonaRuntimeUtf8OffsetToUtf16(s.data(), s.size(), 5, &line, &character);
    CHECK(line == 0);
    CHECK(character == 3);
    CHECK(YonaRuntimeUtf16PositionToUtf8(s.data(), s.size(), 0, 3) == 5);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Character(s.c_str(), 5) == 3);
    CHECK(YonaRuntimeUtf16PositionToUtf8Offset(s.c_str(), 0, 3) == 5);
  }

  TEST_CASE("UTF-16 mapper: CRLF") {
    const char *s = "a\r\nb";
    int64_t line = -1, character = -1;
    YonaRuntimeUtf8OffsetToUtf16(s, 4, 3, &line, &character);
    CHECK(line == 1);
    CHECK(character == 0);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Line(s, 3) == 1);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Character(s, 3) == 0);
  }

  TEST_CASE("UTF-16 mapper: combining character is its own unit") {
    // U+0065 LATIN SMALL LETTER E + U+0301 COMBINING ACUTE ACCENT + 'x'
    std::string s = "e\xCC\x81x";
    int64_t line = -1, character = -1;
    YonaRuntimeUtf8OffsetToUtf16(s.data(), s.size(), 3, &line, &character);
    CHECK(character == 2);
    CHECK(YonaRuntimeUtf16PositionToUtf8(s.data(), s.size(), 0, 2) == 3);
    CHECK(YonaRuntimeUtf8OffsetToUtf16Character(s.c_str(), 3) == 2);
    CHECK(YonaRuntimeUtf16PositionToUtf8Offset(s.c_str(), 0, 2) == 3);
  }

} // TEST_SUITE("Utf16Abi")
