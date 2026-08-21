#include "Parser.h"
#include <doctest/doctest.h>
#include <string>
#include <string_view>

using namespace yona::parser;
using namespace yona::ast;

namespace {

bool parse_ok(std::string_view source) {
    Parser parser;
    return parser.parse_expression(source, "<test>").has_value();
}

std::string first_error(std::string_view source) {
    Parser parser;
    auto result = parser.parse_expression(source, "<test>");
    if (result.has_value())
        return {};
    if (result.error().empty())
        return "<no message>";
    return result.error().front().message;
}

bool error_mentions(std::string_view source, std::string_view needle) {
    auto msg = first_error(source);
    return msg.find(needle) != std::string::npos;
}

TryCatchExpr *as_try(ExprNode *node) {
    REQUIRE(node);
    REQUIRE(node->get_type() == AST_TRY_CATCH_EXPR);
    return static_cast<TryCatchExpr *>(node);
}

} // namespace

TEST_SUITE("TryCatchParser") {

TEST_CASE("simple try/catch consumes closing end") {
    Parser parser;
    auto result = parser.parse_expression("try 42 catch _ -> 0 end", "<test>");
    REQUIRE(result.has_value());
    auto *tc = as_try(result->get());
    REQUIRE(tc->tryExpr);
    CHECK(tc->tryExpr->get_type() == AST_INTEGER_EXPR);
    REQUIRE(tc->catchExpr);
    REQUIRE(tc->catchExpr->patterns.size() == 1);
}

TEST_CASE("missing closing end is a parse error") {
    CHECK_FALSE(parse_ok("try 42 catch _ -> 0"));
    CHECK(error_mentions("try 42 catch _ -> 0", "end"));
}

TEST_CASE("missing catch is a parse error") {
    CHECK_FALSE(parse_ok("try 42 end"));
    CHECK(error_mentions("try 42 end", "catch"));
}

TEST_CASE("trailing tokens after try/catch end are rejected") {
    CHECK_FALSE(parse_ok("try 42 catch _ -> 0 end 999"));
    CHECK_FALSE(parse_ok("try 42 catch _ -> 0 end end"));
    CHECK(error_mentions("try 42 catch _ -> 0 end 999", "Unexpected"));
}

TEST_CASE("nested try/catch each consume their own end") {
    Parser parser;
    auto result = parser.parse_expression(
        "try (try raise 1 catch _ -> 2 end) catch _ -> 3 end", "<test>");
    REQUIRE(result.has_value());
    auto *outer = as_try(result->get());
    REQUIRE(outer->tryExpr);
    REQUIRE(outer->tryExpr->get_type() == AST_TRY_CATCH_EXPR);
    auto *inner = static_cast<TryCatchExpr *>(outer->tryExpr);
    REQUIRE(inner->tryExpr);
    CHECK(inner->tryExpr->get_type() == AST_RAISE_EXPR);
}

TEST_CASE("unparenthesized nested try/catch parses both handlers") {
    Parser parser;
    auto result = parser.parse_expression(
        "try try raise 1 catch _ -> 2 end catch _ -> 3 end", "<test>");
    REQUIRE(result.has_value());
    auto *outer = as_try(result->get());
    REQUIRE(outer->tryExpr);
    CHECK(outer->tryExpr->get_type() == AST_TRY_CATCH_EXPR);
    REQUIRE(outer->catchExpr);
    REQUIRE(outer->catchExpr->patterns.size() == 1);
}

TEST_CASE("try inside do does not steal the do end") {
    Parser parser;
    auto result = parser.parse_expression(
        "do try 1 catch _ -> 2 end 3 end", "<test>");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_DO_EXPR);
    auto *doe = static_cast<DoExpr *>(result->get());
    REQUIRE(doe->steps.size() == 2);
    CHECK(doe->steps[0]->get_type() == AST_TRY_CATCH_EXPR);
    CHECK(doe->steps[1]->get_type() == AST_INTEGER_EXPR);
}

TEST_CASE("try as let binding RHS does not swallow in") {
    Parser parser;
    auto result = parser.parse_expression(
        "let x = try 1 catch _ -> 2 end in x", "<test>");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_LET_EXPR);
    auto *let = static_cast<LetExpr *>(result->get());
    REQUIRE(let->aliases.size() == 1);
    auto *alias = dynamic_cast<ValueAlias *>(let->aliases[0]);
    REQUIRE(alias);
    REQUIRE(alias->expr);
    CHECK(alias->expr->get_type() == AST_TRY_CATCH_EXPR);
    REQUIRE(let->expr);
    CHECK(let->expr->get_type() == AST_IDENTIFIER_EXPR);
}

TEST_CASE("try as let body still parses") {
    Parser parser;
    auto result = parser.parse_expression(
        "let always_raise xs = raise 42 in try always_raise [1] catch _ -> 0 end",
        "<test>");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_LET_EXPR);
    auto *let = static_cast<LetExpr *>(result->get());
    REQUIRE(let->expr);
    CHECK(let->expr->get_type() == AST_TRY_CATCH_EXPR);
}

TEST_CASE("try as case scrutinee consumes end before of") {
    Parser parser;
    auto result = parser.parse_expression(
        "case try 1 catch _ -> 2 end of _ -> 3 end", "<test>");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_CASE_EXPR);
    auto *cse = static_cast<CaseExpr *>(result->get());
    REQUIRE(cse->expr);
    CHECK(cse->expr->get_type() == AST_TRY_CATCH_EXPR);
    REQUIRE(cse->clauses.size() == 1);
}

TEST_CASE("try inside a case arm does not steal the case end") {
    Parser parser;
    auto result = parser.parse_expression(
        "case 1 of _ -> try 2 catch _ -> 0 end end", "<test>");
    REQUIRE(result.has_value());
    REQUIRE((*result)->get_type() == AST_CASE_EXPR);
    auto *cse = static_cast<CaseExpr *>(result->get());
    REQUIRE(cse->clauses.size() == 1);
    REQUIRE(cse->clauses[0]->body);
    CHECK(cse->clauses[0]->body->get_type() == AST_TRY_CATCH_EXPR);
}

TEST_CASE("multiple catch arms under one catch") {
    Parser parser;
    auto result = parser.parse_expression(
        "try raise 1 catch\n"
        "    _ -> 10\n"
        "    _ -> 20\n"
        "end",
        "<test>");
    REQUIRE(result.has_value());
    auto *tc = as_try(result->get());
    REQUIRE(tc->catchExpr);
    CHECK(tc->catchExpr->patterns.size() == 2);
}

TEST_CASE("repeated catch keywords collect every arm") {
    Parser parser;
    auto result = parser.parse_expression(
        "try raise 1 catch _ -> 10 catch _ -> 20 end", "<test>");
    REQUIRE(result.has_value());
    auto *tc = as_try(result->get());
    REQUIRE(tc->catchExpr);
    CHECK(tc->catchExpr->patterns.size() == 2);
}

TEST_CASE("empty catch before end is a parse error") {
    CHECK_FALSE(parse_ok("try 42 catch end"));
}

} // TryCatchParser
