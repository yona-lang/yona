/*
 * Std\Json C ABI — parse/stringify of the recursive Json ADT.
 */

#include "yona/Runtime/Codecs/Json.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace {

extern "C" void YonaRuntimeRelease(void *ptr);

std::string take_rc(const char *value) {
  REQUIRE(value != nullptr);
  std::string result(value);
  YonaRuntimeRelease((void *)value);
  return result;
}

std::string stringify_ok(const char *src) {
  int64_t r = YonaStdJsonParse(src);
  REQUIRE(r != 0);
  int64_t *adt = reinterpret_cast<int64_t *>(static_cast<intptr_t>(r));
  REQUIRE(adt[0] == 0);
  std::string result = take_rc(YonaStdJsonStringify(adt[3]));
  YonaRuntimeRelease(adt);
  return result;
}

void check_parse_error(const char *src) {
  INFO("source: " << src);
  int64_t result = YonaStdJsonParse(src);
  REQUIRE(result != 0);
  auto *adt = reinterpret_cast<int64_t *>(static_cast<intptr_t>(result));
  CHECK(adt[0] == 1);
  YonaRuntimeRelease(adt);
}

} // namespace

TEST_SUITE("JsonAbi") {

  TEST_CASE("parse/stringify number") { CHECK(stringify_ok("42") == "42"); }

  TEST_CASE("parse/stringify array") {
    CHECK(stringify_ok("[1, \"x\", null, false]") == "[1,\"x\",null,false]");
  }

  TEST_CASE("parse/stringify object") {
    CHECK(stringify_ok("{\"id\":1,\"ok\":true}") == "{\"id\":1,\"ok\":true}");
  }

  TEST_CASE("parse/stringify nested") {
    CHECK(stringify_ok("{\"params\":{\"x\":true,\"xs\":[2,3]}}") ==
          "{\"params\":{\"x\":true,\"xs\":[2,3]}}");
  }

  TEST_CASE("parse/stringify surrogate pair") {
    CHECK(stringify_ok("\"\\uD83D\\uDE00\"") == "\"\xF0\x9F\x98\x80\"");
  }

  TEST_CASE("parse error is Err") { check_parse_error("{"); }

  TEST_CASE("malformed containers release partial parse trees") {
    for (int i = 0; i < 32; ++i) {
      check_parse_error("[1,{\"key\":\"value\"}");
      check_parse_error("{\"key\":[1,2],\"other\"");
      check_parse_error("[\"owned\"] trailing");
    }
  }

  TEST_CASE("scalar helpers") {
    CHECK(take_rc(YonaStdJsonStringifyString("hello\nworld")) ==
          "\"hello\\nworld\"");
    CHECK(take_rc(YonaStdJsonStringifyBool(1)) == "true");
    CHECK(take_rc(YonaStdJsonNull()) == "null");
    CHECK(YonaStdJsonParseInt("42") == 42);
  }
}
