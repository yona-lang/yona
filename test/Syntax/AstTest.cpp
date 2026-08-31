#include "yona/Syntax/Ast.h"

#include <doctest/doctest.h>

#include <type_traits>

namespace yona::ast {

static_assert(std::is_base_of_v<LiteralExpr<std::nullptr_t>, UnitExpr>);

TEST_SUITE("AST") {

  TEST_CASE("unit expressions retain their null literal type") {
    CHECK(std::is_base_of_v<LiteralExpr<std::nullptr_t>, UnitExpr>);
  }

} // TEST_SUITE

} // namespace yona::ast
