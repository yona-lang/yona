#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "Toolchain/YonaLinkUtil.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using yona::compiler::DiagnosticEngine;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;

namespace {

std::string compile_and_run(const std::string &source,
                            const std::string &artifact_stem) {
  REQUIRE(yona::test::link::ensure_runtime_objects());

  parser::Parser parser;
  Codegen codegen(artifact_stem);
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());

  DiagnosticEngine diagnostics;
  typechecker::TypeChecker type_checker(diagnostics);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  for (const auto &path : codegen.ModulePaths)
    type_checker.add_module_path(path);

  const auto parsed = parser.parseExpression(source, "<generic-abi-runtime>");
  REQUIRE(parsed);
  REQUIRE(parsed->Expression);
  type_checker.check(parsed->Expression.get());
  REQUIRE(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  codegen.set_type_checker(&type_checker);
  REQUIRE(codegen.compile(parsed->Expression.get()));

  const auto object_path =
      yona::test::link::scratch_root() / ("yona_" + artifact_stem + ".o");
  REQUIRE(codegen.emit_object_file(object_path.string()));
  std::vector<fs::path> objects{object_path};
  REQUIRE(yona::test::link::append_prelude_object(objects));
  REQUIRE(yona::test::link::append_runtime_objects(objects));
  const auto executable = yona::test::link::scratch_root() /
                          (artifact_stem + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));
  return yona::test::link::executeAndCapture(executable);
}

} // namespace

TEST_SUITE("Generic ABI stability") {
  TEST_CASE("mixed Foldable specializations retain their scalar return ABI") {
    constexpr auto Source = R"(
      import fromString as bytesFromString from Std\ByteArray,
             fromSeq as intsFromSeq from Std\IntArray,
             fill as floatsFill from Std\FloatArray
      in
      (foldLeft "abc" (\acc c -> acc + 1) 0,
       foldLeft (bytesFromString "AB") (\acc b -> acc + b) 0,
       foldLeft (intsFromSeq [1, 2]) (\acc n -> acc + n) 0,
       foldLeft {1: 2} (\acc pair -> acc + 1) 0,
       foldLeft {1, 2} (\acc n -> acc + n) 0,
       foldLeft (floatsFill 2 1.5) (\acc n -> acc + n) 0.0)
    )";

    parser::Parser parser;
    Codegen codegen("generic_abi_stability");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());

    DiagnosticEngine diagnostics;
    typechecker::TypeChecker type_checker(diagnostics);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    for (const auto &path : codegen.ModulePaths)
      type_checker.add_module_path(path);

    const auto parsed = parser.parseExpression(Source, "<generic-abi>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);
    type_checker.check(parsed->Expression.get());
    REQUIRE(type_checker.solve_constraints());
    REQUIRE_FALSE(type_checker.has_errors());
    codegen.set_type_checker(&type_checker);

    REQUIRE(codegen.compile(parsed->Expression.get()));
    const std::string ir = codegen.emit_ir();
    CHECK(ir.find("i64 @YonaPreludeFoldableDictTupleFoldLeft(") !=
          std::string::npos);
    CHECK(ir.find("i64 @YonaPreludeFoldableSetElementFoldLeft(") !=
          std::string::npos);
    CHECK(ir.find("ptr @YonaPreludeFoldableDictTupleFoldLeft(") ==
          std::string::npos);
    CHECK(ir.find("ptr @YonaPreludeFoldableSetElementFoldLeft(") ==
          std::string::npos);
  }

  TEST_CASE("repeated scalar Foldable specializations preserve folder owners") {
    constexpr auto Source = R"(
      import fromString as bytesFromString from Std\ByteArray,
             fromSeq as intsFromSeq from Std\IntArray
      in
      (foldLeft (bytesFromString "A") (\acc b -> acc + b) 0,
       foldLeft (intsFromSeq [1]) (\acc n -> acc + n) 0,
       foldLeft (intsFromSeq [2]) (\acc n -> acc + n) 0)
    )";

    CHECK(compile_and_run(Source, "generic_foldable_folder_owner") ==
          "(65, 1, 2)");
  }
}
