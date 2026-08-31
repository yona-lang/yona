#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

class ScopedUnsetEnvironment final {
public:
  explicit ScopedUnsetEnvironment(const char *Name) : Name(Name) {
    if (const char *Value = std::getenv(Name))
      Previous = Value;
#ifdef _WIN32
    (void)_putenv_s(Name, "");
#else
    (void)unsetenv(Name);
#endif
  }

  ~ScopedUnsetEnvironment() {
#ifdef _WIN32
    (void)_putenv_s(Name.c_str(), Previous.value_or("").c_str());
#else
    if (Previous)
      (void)setenv(Name.c_str(), Previous->c_str(), 1);
    else
      (void)unsetenv(Name.c_str());
#endif
  }

  ScopedUnsetEnvironment(const ScopedUnsetEnvironment &) = delete;
  ScopedUnsetEnvironment &operator=(const ScopedUnsetEnvironment &) = delete;

private:
  std::string Name;
  std::optional<std::string> Previous;
};

} // namespace

TEST_CASE("semantic test setup installs configured Prelude without YONA_PATH") {
  ScopedUnsetEnvironment Environment("YONA_PATH");
  REQUIRE(std::getenv("YONA_PATH") == nullptr);

  yona::parser::Parser Parser;
  yona::compiler::codegen::Codegen Codegen("semantic_setup_paths");
  const std::string ExplicitPath = "/explicit/test/module/path";
  Codegen.ModulePaths.push_back(ExplicitPath);
  yona::compiler::DiagnosticEngine Diagnostics;
  yona::compiler::typechecker::TypeChecker Checker(Diagnostics);

  yona::test::SemanticSetup Setup(Codegen, Parser, Checker);

  REQUIRE_FALSE(Codegen.ModulePaths.empty());
  CHECK(Codegen.ModulePaths.front() == ExplicitPath);
  const auto ConfiguredLib = fs::canonical(yona::test::lib_dir()).string();
  CHECK(std::find(Codegen.ModulePaths.begin(), Codegen.ModulePaths.end(),
                  ConfiguredLib) != Codegen.ModulePaths.end());
}
