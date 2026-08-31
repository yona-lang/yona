#include "Support/RepoPaths.h"
#include "yona/Semantics/InterfaceCatalog.h"
#include "yona/Semantics/GenericFunctionSource.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>

TEST_CASE("Semantics interface catalog installs Prelude without Codegen") {
  const auto LibraryPath = yona::test::repo_root() / "lib";
  yona::semantics::InterfaceCatalog Catalog({LibraryPath.string()});
  yona::parser::Parser Parser;
  yona::compiler::DiagnosticEngine Diagnostics;
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);

  Checker.add_module_path(LibraryPath.string());
  Checker.set_import_type_source(&Catalog);
  const auto Installed = Catalog.installPrelude(Parser, Checker);
  REQUIRE(Installed.has_value());
  REQUIRE(*Installed);

  const auto Identity = Catalog.imported_function_sig("Prelude", "identity");
  REQUIRE(Identity.has_value());
  REQUIRE(Identity->param_descriptors.size() == 1);
  CHECK(Identity->return_descriptor == "INT");

  const auto Some = Catalog.imported_function_sig("Prelude", "Some");
  REQUIRE(Some.has_value());
  REQUIRE(Some->param_descriptors.size() == 1);
  CHECK(Some->param_descriptors.front() == "VAR(a)");
  CHECK(Some->return_descriptor == "ADT(Option,VAR(a))");

  auto Result = Parser.parseExpression(
      "import identity from Prelude in identity 42", "catalog.yona");
  REQUIRE(Result.has_value());
  auto Parsed = std::move(Result.value());
  Diagnostics.setSources(Parsed.Sources);
  Checker.check(Parsed.Expression.get());
  CHECK(Checker.solve_constraints());
  CHECK_FALSE(Checker.has_errors());
}

TEST_CASE("Semantics generic source service retains GENFN source ownership") {
  yona::semantics::GenericFunctionSourceService Service;
  const std::vector<yona::semantics::GenericConstructorMetadata> Constructors{
      {"Some", "Option", 0, 1, {"value"}}, {"None", "Option", 1, 0, {}}};

  auto Parsed = Service.parseGenericModule(
      "identityOption",
      "identityOption value = case value of\nSome x -> x\nNone -> 0 end",
      Constructors);
  REQUIRE(Parsed.has_value());
  REQUIRE(Parsed->Sources);
  REQUIRE(Parsed->Module);
  REQUIRE(Parsed->Module->functions.size() == 1);
  CHECK(Parsed->Module->functions.front()->name == "identityOption");
}
