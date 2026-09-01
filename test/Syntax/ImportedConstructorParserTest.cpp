#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using yona::ast::ApplyExpr;
using yona::ast::AST_CASE_EXPR;
using yona::ast::AST_CONSTRUCTOR_PATTERN;
using yona::ast::AST_UNDERSCORE_PATTERN;
using yona::ast::CaseExpr;
using yona::ast::ConstructorPattern;
using yona::ast::ExprNode;
using yona::parser::Parser;

namespace {

CaseExpr *as_case(ExprNode *node) {
  REQUIRE(node);
  REQUIRE(node->get_type() == AST_CASE_EXPR);
  return static_cast<CaseExpr *>(node);
}

} // namespace

TEST_SUITE("ImportedCtorParser") {

  TEST_CASE("unregistered uppercase constructor pattern parses with a field") {
    Parser parser;
    auto result = parser.parseExpression(
        "case j of JsonObject pairs -> pairs end", "<test>");
    REQUIRE(result.has_value());
    auto *cse = as_case(result->get());
    REQUIRE(cse->clauses.size() == 1);
    REQUIRE(cse->clauses[0]->pattern);
    CHECK(cse->clauses[0]->pattern->get_type() == AST_CONSTRUCTOR_PATTERN);
    auto *cp = static_cast<ConstructorPattern *>(cse->clauses[0]->pattern);
    CHECK(cp->constructor_name == "JsonObject");
    REQUIRE(cp->sub_patterns.size() == 1);
  }

  TEST_CASE(
      "unregistered uppercase constructor pattern parses with underscore") {
    Parser parser;
    auto result =
        parser.parseExpression("case j of JsonObject _ -> 1 end", "<test>");
    REQUIRE(result.has_value());
    auto *cse = as_case(result->get());
    REQUIRE(cse->clauses.size() == 1);
    REQUIRE(cse->clauses[0]->pattern);
    CHECK(cse->clauses[0]->pattern->get_type() == AST_CONSTRUCTOR_PATTERN);
    auto *cp = static_cast<ConstructorPattern *>(cse->clauses[0]->pattern);
    CHECK(cp->constructor_name == "JsonObject");
    REQUIRE(cp->sub_patterns.size() == 1);
    CHECK(cp->sub_patterns[0]->get_type() == AST_UNDERSCORE_PATTERN);
  }

  TEST_CASE("unregistered zero-arity constructor still parses") {
    Parser parser;
    auto result =
        parser.parseExpression("case j of JsonNull -> 0 end", "<test>");
    REQUIRE(result.has_value());
    auto *cse = as_case(result->get());
    REQUIRE(cse->clauses.size() == 1);
    REQUIRE(cse->clauses[0]->pattern);
    CHECK(cse->clauses[0]->pattern->get_type() == AST_CONSTRUCTOR_PATTERN);
    auto *cp = static_cast<ConstructorPattern *>(cse->clauses[0]->pattern);
    CHECK(cp->constructor_name == "JsonNull");
    CHECK(cp->sub_patterns.empty());
  }

  TEST_CASE("invalid case binding recovers at the current arm boundary") {
    Parser parser;
    parser.register_prelude_constructors();
    auto result = parser.parseExpression(
        "case Linear 1 of Linear handle -> handle; _ -> 0 end", "<test>");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().size() <= 2);
  }

} // ImportedCtorParser
