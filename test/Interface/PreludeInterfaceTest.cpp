#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using yona::compiler::DiagnosticEngine;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;

namespace {

std::string read_prelude_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::string contents{std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>()};
  // Interface files are written through a text stream, so Windows stores
  // CRLF. Keep content assertions and bootstrap equality platform-neutral.
  for (size_t pos = 0; (pos = contents.find("\r\n", pos)) != std::string::npos;)
    contents.erase(pos, 1);
  return contents;
}

bool compile_prelude_interface(const std::string &source,
                               const std::vector<fs::path> &module_paths,
                               const fs::path &output) {
  DiagnosticEngine diagnostics;
  parser::Parser parser;
  typechecker::TypeChecker checker(diagnostics);
  Codegen codegen("prelude_interface_regeneration", &diagnostics);
  for (const auto &path : module_paths) {
    codegen.ModulePaths.push_back(path.string());
    checker.add_module_path(path.string());
  }
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, checker);

  auto parsed = parser.parseModule(source, "Prelude.yona");
  if (!parsed.has_value())
    return false;
  checker.check_module(parsed.value().get());
  if (!checker.solve_constraints() || checker.has_errors())
    return false;
  codegen.set_type_checker(&checker);
  if (!codegen.compile_module(parsed.value().get()) || codegen.errorCount())
    return false;
  return codegen.emit_interface_file(output.string());
}

} // namespace

TEST_CASE("Prelude interface regeneration is byte-identical across bootstrap "
          "passes") {
  const auto repo = yona::test::repo_root();
  const auto source_path = repo / "lib" / "Prelude.yona";
  const auto scratch = repo / "test" / "_scratch" / "prelude-interface";
  const auto pass_one = scratch / "pass-one.yonai";
  const auto pass_two = scratch / "pass-two.yonai";
  const auto bootstrap = scratch / "bootstrap";
  fs::create_directories(bootstrap);

  const auto source = read_prelude_file(source_path);
  REQUIRE_FALSE(source.empty());
  REQUIRE(compile_prelude_interface(source, {repo / "lib"}, pass_one));
  const auto generated = read_prelude_file(pass_one);
  CHECK(generated.find("FN Eq_Seq_eq 2 Seq(VAR(element)) Seq(VAR(element)) -> "
                       "BOOL borrow 11") != std::string::npos);
  CHECK(generated.find(
            "FN Semigroup_Dict_combine 2 Dict(VAR(key),VAR(value)) "
            "Dict(VAR(key),VAR(value)) -> Dict(VAR(key),VAR(value))") !=
        std::string::npos);
  CHECK(generated.find("INSTANCE Shareable Seq\n  PARAM element\n"
                       "  CONSTRAINT Shareable element") != std::string::npos);
  CHECK(generated.find("  IMPL send ") == std::string::npos);
  CHECK(generated.find("FN Send_") == std::string::npos);
  CHECK(generated.find("FN Shareable_") == std::string::npos);
  fs::copy_file(pass_one, bootstrap / "Prelude.yonai",
                fs::copy_options::overwrite_existing);
  REQUIRE(
      compile_prelude_interface(source, {bootstrap, repo / "lib"}, pass_two));
  CHECK(read_prelude_file(pass_one) == read_prelude_file(pass_two));
}
