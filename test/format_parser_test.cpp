#include <doctest/doctest.h>

#include "Parser.h"

using namespace yona::parser;
using namespace yona::ast;

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

TEST_CASE("wildcard module imports compose with following selective imports") {
    Parser parser;
    auto result = parser.parse_expression(
        R"(import Std\Channel, alloc from Std\ByteArray in alloc 1)",
        "<import-test>");

    REQUIRE(result.has_value());
    auto* import = dynamic_cast<ImportExpr*>(result->get());
    REQUIRE(import);
    REQUIRE(import->clauses.size() == 2);
    CHECK(dynamic_cast<ModuleImport*>(import->clauses[0]) != nullptr);
    CHECK(dynamic_cast<FunctionsImport*>(import->clauses[1]) != nullptr);
}

TEST_CASE("parenthesized lambda parameter is one tuple pattern") {
    Parser parser;
    auto result = parser.parse_expression(R"(\(left, right) -> left + right)",
                                          "<lambda-test>");

    REQUIRE(result.has_value());
    auto* lambda = dynamic_cast<FunctionExpr*>(result->get());
    REQUIRE(lambda);
    REQUIRE(lambda->patterns.size() == 1);
    auto* tuple = dynamic_cast<TuplePattern*>(lambda->patterns[0]);
    REQUIRE(tuple);
    CHECK(tuple->patterns.size() == 2);
}

TEST_CASE("juxtaposed lambda parameters remain separate") {
    Parser parser;
    auto result = parser.parse_expression(R"(\left right -> left + right)",
                                          "<lambda-test>");

    REQUIRE(result.has_value());
    auto* lambda = dynamic_cast<FunctionExpr*>(result->get());
    REQUIRE(lambda);
    CHECK(lambda->patterns.size() == 2);
}

TEST_CASE("instance methods preserve consecutive borrowed parameters") {
    Parser parser;
    auto result = parser.parse_module(R"(module Test
trait Same a
    same : a -> a -> Bool
end
instance Same Int
    same @borrow left @borrow right = left == right
end
)", "<borrow-test>");

    REQUIRE(result.has_value());
    REQUIRE(result.value()->instance_declarations.size() == 1);
    const auto* instance = result.value()->instance_declarations.front();
    REQUIRE(instance->methods.size() == 1);
    const auto* method = instance->methods.front();
    REQUIRE(method->patterns.size() == 2);
    REQUIRE(method->param_borrow.size() == 2);
    CHECK(method->param_borrow[0]);
    CHECK(method->param_borrow[1]);
}

} // TEST_SUITE
