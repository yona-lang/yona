#include "Parser.h"
#include <doctest/doctest.h>
#include <sstream>

using namespace yona::parser;
using namespace yona::ast;

TEST_SUITE("SeqOps") {

TEST_CASE("cons-right :> parses as ConsRightExpr") {
    Parser parser;
    auto result = parser.parse_expression("[1, 2] :> 3");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_CONS_RIGHT_EXPR);
}

TEST_CASE("membership in parses as InExpr") {
    Parser parser;
    auto result = parser.parse_expression("2 in [1, 2, 3]");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_IN_EXPR);
}

TEST_CASE("remove -- parses") {
    Parser parser;
    auto result = parser.parse_expression("[1, 2, 3] -- [2]");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_REMOVE_EXPR);
}

TEST_CASE("let in is still a let, not membership") {
    Parser parser;
    auto result = parser.parse_expression("let x = 2 in x");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_LET_EXPR);
}

TEST_CASE("let binding if-else does not consume in as membership") {
    Parser parser;
    auto result = parser.parse_expression(
        "let f x = if x <= 0 then 0 else f (x - 1) in f 10");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_LET_EXPR);
    auto *let = static_cast<LetExpr *>(result->get());
    REQUIRE(let->aliases.size() == 1);
    auto *alias = dynamic_cast<LambdaAlias *>(let->aliases[0]);
    REQUIRE(alias);
    REQUIRE(alias->lambda);
    REQUIRE(!alias->lambda->bodies.empty());
    auto *body = dynamic_cast<BodyWithoutGuards *>(alias->lambda->bodies[0]);
    REQUIRE(body);
    REQUIRE(body->expr);
    CHECK(body->expr->get_type() == AST_IF_EXPR);
}

TEST_CASE("if condition still parses in as membership") {
    Parser parser;
    auto result = parser.parse_expression("if 2 in [1, 2, 3] then 1 else 0");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_IF_EXPR);
    auto *ife = static_cast<IfExpr *>(result->get());
    REQUIRE(ife->condition);
    CHECK(ife->condition->get_type() == AST_IN_EXPR);
}

TEST_CASE("let of if with membership in condition") {
    Parser parser;
    auto result = parser.parse_expression(
        "let x = if 2 in [1, 2, 3] then 1 else 0 in x");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_LET_EXPR);
}

TEST_CASE("nested let does not consume outer in as membership") {
    Parser parser;
    auto result = parser.parse_expression("let y = let z = 1 in z * 2 in y");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_LET_EXPR);
    auto *outer = static_cast<LetExpr *>(result->get());
    REQUIRE(outer->aliases.size() == 1);
    auto *alias = dynamic_cast<ValueAlias *>(outer->aliases[0]);
    REQUIRE(alias);
    REQUIRE(alias->expr);
    CHECK(alias->expr->get_type() == AST_LET_EXPR);
    REQUIRE(outer->expr);
    CHECK(outer->expr->get_type() == AST_IDENTIFIER_EXPR);
}

TEST_CASE("perform let RHS does not consume in as membership") {
    Parser parser;
    auto result = parser.parse_expression(
        R"(let plan = \() -> perform Fs.read "x" in plan ())");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_LET_EXPR);
}

TEST_CASE("raise let RHS does not consume in as membership") {
    Parser parser;
    auto result = parser.parse_expression(
        "let always_raise xs = raise 42 in try always_raise [1] catch _ -> 0 end");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_LET_EXPR);
}

TEST_CASE("parenthesized membership in nested let body") {
    Parser parser;
    auto result = parser.parse_expression(
        "let y = let z = 1 in (2 in [1, 2, 3]) in y");
    REQUIRE(result.has_value());
    CHECK((*result)->get_type() == AST_LET_EXPR);
}

} // SeqOps
