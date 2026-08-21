/*
 * Std\Json C ABI — parse/stringify of the recursive Json ADT.
 */

#include "yona/runtime/json.h"

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <string>

namespace {

const char* stringify_ok(const char* src) {
    int64_t r = yona_Std_Json__parse(src);
    REQUIRE(r != 0);
    int64_t* adt = reinterpret_cast<int64_t*>(static_cast<intptr_t>(r));
    REQUIRE(adt[0] == 0);
    return yona_Std_Json__stringify(adt[3]);
}

} // namespace

TEST_SUITE("JsonAbi") {

TEST_CASE("parse/stringify number") {
    CHECK(std::string(stringify_ok("42")) == "42");
}

TEST_CASE("parse/stringify array") {
    CHECK(std::string(stringify_ok("[1, \"x\", null, false]")) == "[1,\"x\",null,false]");
}

TEST_CASE("parse/stringify object") {
    CHECK(std::string(stringify_ok("{\"id\":1,\"ok\":true}")) == "{\"id\":1,\"ok\":true}");
}

TEST_CASE("parse/stringify nested") {
    CHECK(std::string(stringify_ok("{\"params\":{\"x\":true,\"xs\":[2,3]}}")) ==
          "{\"params\":{\"x\":true,\"xs\":[2,3]}}");
}

TEST_CASE("parse/stringify surrogate pair") {
    CHECK(std::string(stringify_ok("\"\\uD83D\\uDE00\"")) == "\"\xF0\x9F\x98\x80\"");
}

TEST_CASE("parse error is Err") {
    int64_t r = yona_Std_Json__parse("{");
    REQUIRE(r != 0);
    int64_t* adt = reinterpret_cast<int64_t*>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 1);
}

TEST_CASE("scalar helpers") {
    CHECK(std::string(yona_Std_Json__stringifyString("hello\nworld")) == "\"hello\\nworld\"");
    CHECK(std::string(yona_Std_Json__stringifyBool(1)) == "true");
    CHECK(std::string(yona_Std_Json__null()) == "null");
    CHECK(yona_Std_Json__parseInt("42") == 42);
}

}
