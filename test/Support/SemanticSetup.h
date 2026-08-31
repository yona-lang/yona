#ifndef YONA_TEST_SUPPORT_SEMANTICSETUP_H
#define YONA_TEST_SUPPORT_SEMANTICSETUP_H

#include "Support/RepoPaths.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/InterfaceCatalog.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Syntax/Parser.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace yona::test {

/// Keeps an InterfaceCatalog alive for the parser/type checker pair used by a
/// test. Codegen receives only lowering metadata through loadPrelude().
class SemanticSetup final {
public:
  SemanticSetup(compiler::codegen::Codegen &Codegen, parser::Parser &Parser,
                compiler::typechecker::TypeChecker &Checker)
      : Catalog(makeCatalog(Codegen)) {
    Codegen.loadPrelude();
    Catalog.appendEnvironmentSearchPaths();
    const auto Installed = Catalog.installPrelude(Parser, Checker);
    if (!Installed || !*Installed)
      throw std::runtime_error("unable to install Prelude interface in test");
    Checker.set_import_type_source(&Catalog);
  }

private:
  static semantics::InterfaceCatalog
  makeCatalog(compiler::codegen::Codegen &Codegen) {
    std::error_code Error;
    const auto ConfiguredLib =
        std::filesystem::weakly_canonical(lib_dir(), Error);
    if (!Error && std::filesystem::is_directory(ConfiguredLib, Error)) {
      const auto Path = ConfiguredLib.string();
      if (std::find(Codegen.ModulePaths.begin(), Codegen.ModulePaths.end(),
                    Path) == Codegen.ModulePaths.end())
        Codegen.ModulePaths.push_back(Path);
    }
    return semantics::InterfaceCatalog(Codegen.ModulePaths);
  }

  semantics::InterfaceCatalog Catalog;
};

/// Parser-only tests still need Prelude constructor metadata. The private
/// checker exists solely to complete the semantic catalog installation.
class ParserPreludeSetup final {
public:
  ParserPreludeSetup(compiler::codegen::Codegen &Codegen,
                     parser::Parser &Parser)
      : Diagnostics(), Checker(Diagnostics), Setup(Codegen, Parser, Checker) {}

private:
  compiler::DiagnosticEngine Diagnostics;
  compiler::typechecker::TypeChecker Checker;
  SemanticSetup Setup;
};

} // namespace yona::test

#define YONA_TEST_JOIN_IMPL(Left, Right) Left##Right
#define YONA_TEST_JOIN(Left, Right) YONA_TEST_JOIN_IMPL(Left, Right)
#define YONA_TEST_INSTALL_PRELUDE(Codegen, Parser, Checker)                    \
  yona::test::SemanticSetup YONA_TEST_JOIN(SemanticSetup_,                     \
                                           __LINE__)(Codegen, Parser, Checker)
#define YONA_TEST_INSTALL_PARSER_PRELUDE(Codegen, Parser)                      \
  yona::test::ParserPreludeSetup YONA_TEST_JOIN(ParserPreludeSetup_,           \
                                                __LINE__)(Codegen, Parser)

#endif /* YONA_TEST_SUPPORT_SEMANTICSETUP_H */
