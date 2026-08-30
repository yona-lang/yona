#include "yona/Syntax/YonaStyle.h"

#include <doctest/doctest.h>

TEST_SUITE("Yona source style") {

  TEST_CASE("camel case value names are accepted") {
    auto Result = yona::syntax::checkYonaStyle("let someValue = 1 in someValue",
                                               "<style-test>");

    REQUIRE(Result.has_value());
    CHECK(Result->empty());
  }

  TEST_CASE("underscored value names are rejected") {
    auto Result = yona::syntax::checkYonaStyle(
        "let invalid_name = 1 in invalid_name", "<style-test>");

    REQUIRE(Result.has_value());
    REQUIRE(Result->size() == 2);
    CHECK(Result->front().Message.find("camelCase") != std::string::npos);
  }

  TEST_CASE("public acronyms are treated as words") {
    auto Canonical = yona::syntax::checkYonaStyle(
        R"(import mapGpu from Std\Gpu in mapGpu)", "<style-test>");
    auto NonCanonical = yona::syntax::checkYonaStyle("import mapG"
                                                     "PU from Std\\"
                                                     "G"
                                                     "PU in mapG"
                                                     "PU",
                                                     "<style-test>");

    REQUIRE(Canonical.has_value());
    CHECK(Canonical->empty());
    REQUIRE(NonCanonical.has_value());
    CHECK_FALSE(NonCanonical->empty());
  }

  TEST_CASE("lexer failures remain structured diagnostics") {
    auto Result =
        yona::syntax::checkYonaStyle("\"unterminated", "<style-test>");

    CHECK_FALSE(Result.has_value());
    CHECK_FALSE(Result.error().empty());
  }

} // TEST_SUITE
