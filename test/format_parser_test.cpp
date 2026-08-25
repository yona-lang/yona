#include <doctest/doctest.h>

#include "Parser.h"

using namespace yona::parser;

TEST_SUITE("Std Format parser regressions") {

TEST_CASE("format accepts an empty-brace placeholder string") {
    Parser parser;
    auto result = parser.parse_expression(
        R"(import format from Std\Format in format "{}" ["ok"])",
        "<format-test>");

    CHECK(result.has_value());
}

TEST_CASE("interpolated strings can be function arguments") {
    Parser parser;
    auto result = parser.parse_expression(R"(identity "hello {name}")", "<format-test>");

    CHECK(result.has_value());
}

TEST_CASE("existing string interpolation remains valid") {
    Parser parser;
    auto result = parser.parse_expression(R"(let name = "Yona" in "hello {name}")", "<format-test>");

    CHECK(result.has_value());
}

} // TEST_SUITE
