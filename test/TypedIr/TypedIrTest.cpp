#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"
#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/TypedIr.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <utility>

TEST_CASE("TypedIr projects owned semantic facts into function values") {
  const std::string Source = "let value = 40 + 2 in value";
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression(Source, "typed-ir.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
  Checker.check(Parsed.Expression.get());
  REQUIRE(Checker.solve_constraints());

  yona::semantics::SemanticModel Semantics(Parsed.Sources, Parsed.Source,
                                           Parsed.Expression.get(), &Checker,
                                           &Diagnostics);
  auto Module = yona::typed_ir::buildEntryModule(Semantics, "Main");
  const auto *Function = Module.findFunction("main");
  REQUIRE(Function);
  REQUIRE(Function->result());
  const auto ResultId = *Function->result();

  const auto *Value = Function->findValue(ResultId);
  REQUIRE(Value);
  CHECK(Value->type().find("Int") != std::string_view::npos);
  CHECK(Value->typeOwnership() == yona::semantics::OwnershipKind::Unrestricted);
  CHECK(Value->ownership() == yona::typed_ir::ValueOwnershipKind::Trivial);
  CHECK(Value->range().Source == Parsed.Source);
}

TEST_CASE("TypedIr makes linear ownership transitions explicit") {
  yona::semantics::NodeSemantics Facts;
  Facts.InferredType = "Linear FileHandle";
  Facts.Effects = "{}";
  Facts.Ownership = yona::semantics::OwnershipKind::Linear;

  yona::typed_ir::Function Function("close", "Linear FileHandle -> Unit",
                                    "{Fs.close}");
  const auto Handle = Function.appendValue(
      yona::typed_ir::ValueKind::Parameter, "handle", Facts,
      yona::typed_ir::ValueOwnershipKind::Owned, yona::SourceRange::unknown());
  REQUIRE(Function.findValue(Handle));
  CHECK(Function.parameters().size() == 1);
  CHECK(Function.findValue(Handle)->ownership() ==
        yona::typed_ir::ValueOwnershipKind::Owned);

  Function.transfer(Handle);
  CHECK(Function.findValue(Handle)->ownership() ==
        yona::typed_ir::ValueOwnershipKind::Transferred);
  CHECK_THROWS_AS(Function.setResult(Handle), std::invalid_argument);
}

TEST_CASE("TypedIr rejects implicit ownership and duplicate functions") {
  yona::semantics::NodeSemantics Unknown;
  Unknown.InferredType = "a";
  yona::typed_ir::Function Function("identity", "a -> a", "{}");
  CHECK_THROWS_AS(static_cast<void>(Function.appendValue(
                      yona::typed_ir::ValueKind::Parameter, "value", Unknown,
                      yona::typed_ir::ValueOwnershipKind::Borrowed,
                      yona::SourceRange::unknown())),
                  std::invalid_argument);

  yona::semantics::NodeSemantics Linear;
  Linear.InferredType = "Linear a";
  Linear.Ownership = yona::semantics::OwnershipKind::Linear;
  CHECK_THROWS_AS(static_cast<void>(Function.appendValue(
                      yona::typed_ir::ValueKind::Parameter, "value", Linear,
                      yona::typed_ir::ValueOwnershipKind::Trivial,
                      yona::SourceRange::unknown())),
                  std::invalid_argument);

  yona::semantics::NodeSemantics Integer;
  Integer.InferredType = "Int";
  Integer.Ownership = yona::semantics::OwnershipKind::Unrestricted;
  CHECK_THROWS_AS(static_cast<void>(Function.appendConstant(
                      "invalid", Integer, true, yona::SourceRange::unknown())),
                  std::invalid_argument);

  yona::typed_ir::Module Module("Main", yona::SourceId(0));
  Module.addFunction(yona::typed_ir::Function("main", "() -> Unit", "{}"));
  CHECK_THROWS_AS(
      Module.addFunction(yona::typed_ir::Function("main", "() -> Unit", "{}")),
      std::invalid_argument);
}
