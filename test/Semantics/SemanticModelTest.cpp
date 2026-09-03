#include "yona/Semantics/SemanticModel.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

TEST_CASE("SemanticModel gives shadowed bindings distinct identities") {
  const std::string Source = "let value = 1 in (let value = 2 in value, value)";
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression(Source, "shadow.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
  Checker.check(Parsed.Expression.get());
  REQUIRE(Checker.solve_constraints());

  yona::semantics::SemanticModel Model(Parsed.Sources, Parsed.Source,
                                       Parsed.Expression.get(), &Checker,
                                       &Diagnostics);

  const std::size_t OuterDefinitionOffset = Source.find("value");
  const std::size_t InnerDefinitionOffset =
      Source.find("value", OuterDefinitionOffset + 1);
  const std::size_t InnerUseOffset =
      Source.find("value", InnerDefinitionOffset + 1);
  const std::size_t OuterUseOffset = Source.rfind("value");

  const auto *OuterDefinition = Model.occurrenceAt(OuterDefinitionOffset);
  const auto *InnerDefinition = Model.occurrenceAt(InnerDefinitionOffset);
  const auto *InnerUse = Model.occurrenceAt(InnerUseOffset);
  const auto *OuterUse = Model.occurrenceAt(OuterUseOffset);
  REQUIRE(OuterDefinition);
  REQUIRE(InnerDefinition);
  REQUIRE(InnerUse);
  REQUIRE(OuterUse);

  CHECK(OuterDefinition->IsDefinition);
  CHECK(InnerDefinition->IsDefinition);
  CHECK(OuterDefinition->Binding != InnerDefinition->Binding);
  CHECK(InnerUse->Binding == InnerDefinition->Binding);
  CHECK(OuterUse->Binding == OuterDefinition->Binding);

  CHECK(Model.definition(InnerUse->Binding) == InnerDefinition);
  CHECK(Model.definition(OuterUse->Binding) == OuterDefinition);
  CHECK(Model.references(InnerUse->Binding, true).size() == 2);
  CHECK(Model.references(OuterUse->Binding, true).size() == 2);
  CHECK(Model.references(InnerUse->Binding, false).size() == 1);

  CHECK(InnerUse->Range.Source == Parsed.Source);
  CHECK(InnerUse->Facts.Ownership != yona::semantics::OwnershipKind::Unknown);
  CHECK_FALSE(InnerUse->Facts.InferredType.empty());
  CHECK_FALSE(InnerUse->Facts.Effects.empty());
}

TEST_CASE("SemanticModel exposes range-keyed diagnostics") {
  const std::string Source = "missing";
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression(Source, "missing.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
  Checker.check(Parsed.Expression.get());
  Checker.solve_constraints();

  yona::semantics::SemanticModel Model(Parsed.Sources, Parsed.Source,
                                       Parsed.Expression.get(), &Checker,
                                       &Diagnostics);
  REQUIRE_FALSE(Model.diagnostics().empty());
  CHECK(Model.diagnostics().front().Range.Source == Parsed.Source);
  CHECK_FALSE(Model.diagnostics().front().Message.empty());
}

TEST_CASE("SemanticModel safely indexes constructor case patterns") {
  const std::string Source = "case Some 1 of\n"
                             "  Some item -> item\n"
                             "  None -> 0\n"
                             "end\n";
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression(Source, "constructors.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
  Checker.check(Parsed.Expression.get());
  Checker.solve_constraints();

  CHECK_NOTHROW([&] {
    yona::semantics::SemanticModel Model(Parsed.Sources, Parsed.Source,
                                         Parsed.Expression.get(), &Checker,
                                         &Diagnostics);
    CHECK_FALSE(Model.occurrences().empty());
  }());
}

TEST_CASE("Semantic model structural type projection survives checker "
          "destruction") {
  const std::string Source = "let value = 1 in value";
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression(Source, "durable.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  std::optional<yona::semantics::SemanticModel> Model;
  {
    yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
    REQUIRE(Checker.check(Parsed.Expression.get()));
    REQUIRE(Checker.solve_constraints());
    Model.emplace(Parsed.Sources, Parsed.Source, Parsed.Expression.get(),
                  &Checker, &Diagnostics);
  }

  REQUIRE(Model.has_value());
  REQUIRE(Model->rootFacts());
  CHECK(Model->types().format(Model->rootFacts()->Type) == "Int");
  CHECK(Model->sharedTypeArena().get() != nullptr);
  CHECK(Model->sources() == Parsed.Sources);

  auto *Let = dynamic_cast<yona::ast::LetExpr *>(Parsed.Expression.get());
  REQUIRE(Let);
  REQUIRE(Let->expr);
  const auto Binding = Model->bindingFor(Let->expr);
  REQUIRE(Binding.has_value());
  CHECK(Binding->isValid());
}

TEST_CASE("Semantic model structural effect operation catalog survives "
          "checker destruction") {
  yona::parser::Parser Parser;
  auto Result = Parser.parseExpression("1", "effects-durable.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());

  yona::compiler::DiagnosticEngine Diagnostics;
  Diagnostics.setSources(Parsed.Sources);
  std::optional<yona::semantics::SemanticModel> Model;
  {
    yona::compiler::typechecker::TypeChecker Checker(Diagnostics);
    auto &Arena = Checker.arena();
    auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
    Checker.register_effect("State", "s", {{"get", {}, Int}});
    REQUIRE(Checker.check(Parsed.Expression.get()));
    REQUIRE(Checker.solve_constraints());
    Model.emplace(Parsed.Sources, Parsed.Source, Parsed.Expression.get(),
                  &Checker, &Diagnostics);
  }

  REQUIRE(Model.has_value());
  REQUIRE(Model->effectOperations().size() == 1);
  CHECK(Model->effectOperations().front().Key.Effect.Module ==
        yona::model::ModuleIdentity{"Main"});
  CHECK(Model->effectOperations().front().Key.Effect.Name == "State");
  CHECK(Model->effectOperations().front().Key.Operation == "get");
  CHECK(Model->types().format(Model->effectOperations().front().Result) ==
        "Int");
}
