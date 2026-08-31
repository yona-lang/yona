#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using yona::compiler::DiagnosticEngine;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;

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
}
