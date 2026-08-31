#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "Toolchain/YonaLinkUtil.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using yona::compiler::DiagnosticEngine;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;

namespace {

std::string read_file(const fs::path &path) {
  std::ifstream stream(path);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

std::string assert_zero_alloc_leaks(const std::string &source,
                                    const std::string &artifact_stem,
                                    const std::string &expected_output = {},
                                    const std::string &expected_stats = {},
                                    const fs::path &module_root = {},
                                    const fs::path &module_object = {},
                                    std::size_t minimum_retain_calls = 0) {
  REQUIRE(yona::test::link::ensure_runtime_objects());

  parser::Parser parser;
  Codegen codegen(artifact_stem);
  if (!module_root.empty())
    codegen.ModulePaths.push_back(module_root.string());
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  DiagnosticEngine diagnostics;
  typechecker::TypeChecker type_checker(diagnostics);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  for (const auto &path : codegen.ModulePaths)
    type_checker.add_module_path(path);

  const auto parsed = parser.parseExpression(source, "<stream>");
  REQUIRE(parsed);
  REQUIRE(parsed->Expression);
  type_checker.check(parsed->Expression.get());
  REQUIRE(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  codegen.set_type_checker(&type_checker);
  REQUIRE(codegen.compile(parsed->Expression.get()));
  const auto ir = codegen.emit_ir();
  if (minimum_retain_calls > 0) {
    std::size_t retain_calls = 0;
    for (std::size_t position = 0;
         (position = ir.find("call void @YonaRuntimeRetain", position)) !=
         std::string::npos;
         position += 4)
      ++retain_calls;
    CHECK(retain_calls >= minimum_retain_calls);
  }

  const auto object_path =
      yona::test::link::scratch_root() / ("yona_" + artifact_stem + ".o");
  REQUIRE(codegen.emit_object_file(object_path.string()));
  std::vector<fs::path> objects;
  if (!module_object.empty())
    objects.push_back(module_object);
  objects.push_back(object_path);
  REQUIRE(yona::test::link::append_prelude_object(objects));
  REQUIRE(yona::test::link::append_runtime_objects(objects));
  const auto executable = yona::test::link::scratch_root() /
                          (artifact_stem + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));

  const auto combined =
      yona::test::link::executeWithAllocationStats(executable);
  REQUIRE(combined != "RUN_ERROR");
  INFO("Full output:\n" << combined);
  REQUIRE(combined.find("alloc-stats") != std::string::npos);
  if (!expected_output.empty())
    CHECK(combined.starts_with(expected_output + "\n[alloc-stats]"));
  if (!expected_stats.empty())
    CHECK(combined.find(expected_stats) != std::string::npos);

  std::size_t leak_rows = 0;
  for (std::size_t position = 0;
       (position = combined.find("leaked=", position)) != std::string::npos;) {
    position += 7;
    std::size_t end = position;
    while (end < combined.size() &&
           std::isdigit(static_cast<unsigned char>(combined[end])))
      ++end;
    ++leak_rows;
    const auto leaked = combined.substr(position, end - position);
    CHECK_MESSAGE(leaked == "0",
                  "Found leaked=" << leaked << " in alloc stats");
    position = end;
  }
  REQUIRE(leak_rows > 0);
  return ir;
}

void assert_generic_record_zero_alloc_leaks(
    const std::string &source, const std::string &artifact_stem,
    const std::string &expected_output,
    const std::string &expected_stats = "tag=SEQ allocs=2 frees=2 leaked=0",
    std::size_t minimum_retain_calls = 3) {
  const auto module_root =
      yona::test::link::scratch_root() / ("yona_" + artifact_stem + "_modules");
  fs::create_directories(module_root / "Test");

  parser::Parser module_parser;
  auto module = module_parser.parseModule(R"(
module Test\PatternRecord

export type Box
export make

type Box a = Box { item : a }
make : a -> Box a
make value = Box { item = value }
)",
                                          "pattern_record.yona");
  REQUIRE(module.has_value());

  DiagnosticEngine module_diagnostics;
  Codegen module_codegen(artifact_stem + "_module", &module_diagnostics);
  module_codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  REQUIRE(module_codegen.compile_module(module.value().get()));
  REQUIRE_FALSE(module_diagnostics.has_errors());
  const auto module_object = yona::test::link::scratch_root() /
                             ("yona_" + artifact_stem + "_module.o");
  REQUIRE(module_codegen.emit_object_file(module_object.string()));
  REQUIRE(module_codegen.emit_interface_file(
      (module_root / "Test" / "PatternRecord.yonai").string()));

  assert_zero_alloc_leaks(source, artifact_stem, expected_output,
                          expected_stats, module_root, module_object,
                          minimum_retain_calls);
}

} // namespace

TEST_SUITE("Pattern ownership") {

  TEST_CASE(
      "owned tuple aliases transfer heap children and release aggregate") {
    assert_zero_alloc_leaks("let (x, y) = ([1], [2]) in 0",
                            "owned_tuple_alias_drop");
  }

  TEST_CASE("destructuring a named tuple preserves its enclosing owner") {
    assert_zero_alloc_leaks(
        "let t = ([1], [2]) in "
        "let (x, y) = t in "
        "case t of (a, b) -> case a of [value] -> value; _ -> 0 end end",
        "named_tuple_alias_reuse", "1");
  }

  TEST_CASE("named tuple reuse survives an intervening tuple allocation") {
    assert_zero_alloc_leaks(
        "let t = ([1], [2]) in "
        "let (x, y) = t in "
        "let q = (3, 4) in "
        "case q of _ -> case t of "
        "(a, b) -> case a of [value] -> value; _ -> 0 end end end",
        "named_tuple_alias_slot_reuse", "1");
  }

  TEST_CASE("failed tuple patterns release retained prefix bindings") {
    assert_zero_alloc_leaks("case ([1], :no) of (x, :yes) -> 1; _ -> 0 end",
                            "tuple_pattern_symbol_prefix_mismatch", "0",
                            "tag=SEQ allocs=1 frees=1 leaked=0");
    assert_zero_alloc_leaks("case ([1], 2) of (x, 3) -> 1; _ -> 0 end",
                            "tuple_pattern_literal_prefix_mismatch", "0",
                            "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("temporary scalar constructor cases release their scrutinee") {
    assert_zero_alloc_leaks("case Some 1 of Some x -> 0; None -> 0 end",
                            "temporary_scalar_constructor_case");
  }

  TEST_CASE("constructor bindings are released when a guard rejects an arm") {
    assert_zero_alloc_leaks(
        "case Some [1] of Some xs if false -> 1; Some xs -> 0; None -> 2 end",
        "constructor_binding_guard_failure", "0");
  }

  TEST_CASE("constructor bindings may escape as the selected arm result") {
    assert_zero_alloc_leaks("let extracted = "
                            "case Some [1] of Some xs -> xs; None -> [] end in "
                            "case extracted of [x] -> x; _ -> 0 end",
                            "constructor_binding_escape");
  }

  TEST_CASE("record bindings survive guards and escape temporary scrutinees") {
    assert_generic_record_zero_alloc_leaks(
        "import Test\\PatternRecord in "
        "let extracted = case make [1] of "
        "Box { item = value } if false -> []; "
        "Box { item = value } -> value end in "
        "let replacement = [9] in "
        "case replacement of _ -> "
        "case extracted of [result] -> result; _ -> 0 end end",
        "generic_record_binding_lifetime", "1");
  }

  TEST_CASE("generic record tuple fields retain their concrete shape") {
    assert_generic_record_zero_alloc_leaks(
        "import Test\\PatternRecord in "
        "case make ([1], [2]) of "
        "Box { item = (left, right) } -> "
        "case left of [x] -> "
        "case right of [y] -> x * 10 + y; _ -> 0 end; _ -> 0 end end",
        "generic_record_tuple_shape", "12");
  }

  TEST_CASE("generic record function fields retain their return shape") {
    assert_generic_record_zero_alloc_leaks(
        "import Test\\PatternRecord in "
        "let values n = [n] in "
        "case make values of Box { item = f } -> "
        "case f 1 of [x] -> x; _ -> 0 end end",
        "generic_record_function_shape", "1",
        "tag=SEQ allocs=1 frees=1 leaked=0", 2);
  }

  TEST_CASE("temporary heap-field constructor cases isolate field ownership") {
    assert_zero_alloc_leaks("case Some [1] of Some xs -> 0; None -> 0 end",
                            "temporary_heap_constructor_case");
  }

  TEST_CASE("constructors retain heap values borrowed from named bindings") {
    assert_zero_alloc_leaks(
        "let xs = [1], wrapped = Some xs in "
        "case wrapped of Some _ -> case xs of [x] -> x; _ -> 0 end; "
        "None -> 0 end",
        "named_constructor_field");
  }

  TEST_CASE("returned constructors own temporary heap fields exactly once") {
    assert_zero_alloc_leaks(
        "let make xs = Some xs in "
        "case make [1] of "
        "Some values -> case values of [x] -> x; _ -> 0 end; "
        "None -> 0 end",
        "returned_temporary_constructor");
  }

  TEST_CASE("nested constructors transfer temporary heap ownership") {
    assert_zero_alloc_leaks(
        "case Some (Some [1]) of "
        "Some inner -> case inner of Some xs -> 0; None -> 0 end; "
        "None -> 0 end",
        "nested_temporary_constructor");
  }

  TEST_CASE("multi-field constructors transfer each temporary heap field") {
    assert_zero_alloc_leaks("import TestReport from Std\\Test in "
                            "let report = TestReport 1 2 [\"line\"] in 0",
                            "multi_field_temporary_constructor");
  }

  TEST_CASE("constructors transfer heap results returned from nested lets") {
    assert_zero_alloc_leaks("case Some (let temporary = [1] in temporary) of "
                            "Some values -> 0; None -> 1 end",
                            "nested_let_constructor_field", "0",
                            "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("nested let shadowing restores outer constructor provenance") {
    assert_zero_alloc_leaks(
        "let outer = 1 in "
        "let wrapped = Some (let outer = [2] in outer) in "
        "case wrapped of Some inner -> "
        "case inner of [innerValue] -> "
        "innerValue * 10 + outer; _ -> 0 end; None -> 0 end",
        "shadowed_nested_let_constructor_field", "21",
        "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("nested let results preserve enclosing captured ownership") {
    assert_zero_alloc_leaks(
        "let captured = [3] in "
        "let make = \\() -> Some (let alias = captured in alias) in "
        "case make () of Some values -> "
        "case values of [result] -> result; _ -> 0 end; None -> 0 end",
        "captured_nested_let_constructor_field", "3",
        "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("terminated case arms release pattern owners before raising") {
    const auto ir = assert_zero_alloc_leaks(
        "try case ([1], 2) of (x, y) -> raise 1 end catch _ -> 0 end",
        "terminated_tuple_pattern_arm", "0",
        "tag=SEQ allocs=1 frees=1 leaked=0");

    const auto raise = ir.find("call void @YonaRuntimeRaise");
    REQUIRE(raise != std::string::npos);
    const auto unreachable = ir.find("unreachable", raise);
    REQUIRE(unreachable != std::string::npos);
    std::size_t releases = 0;
    for (std::size_t position = 0;
         (position = ir.find("call void @YonaRuntimeRelease", position)) !=
             std::string::npos &&
         position < raise;
         position += 4)
      ++releases;
    CHECK(releases == 2);
    CHECK(raise < unreachable);
    CHECK(ir.find("call void @YonaRuntimeRelease", raise) == std::string::npos);

    const auto frame_ir =
        assert_zero_alloc_leaks("let terminate xs = "
                                "try case (if true then xs else xs) of "
                                "[x] -> raise 1; _ -> 2 end catch _ -> 0 end "
                                "in terminate [1]",
                                "terminated_case_arm_frame_transfer", "0",
                                "tag=SEQ allocs=1 frees=1 leaked=0");
    const auto frame_transfer =
        frame_ir.find("call void @YonaRuntimeFrameTransfer");
    REQUIRE(frame_transfer != std::string::npos);
    const auto frame_release =
        frame_ir.find("call void @YonaRuntimeRelease", frame_transfer);
    REQUIRE(frame_release != std::string::npos);
    const auto frame_raise =
        frame_ir.find("call void @YonaRuntimeRaise", frame_release);
    REQUIRE(frame_raise != std::string::npos);
    CHECK(frame_transfer < frame_release);
    CHECK(frame_release < frame_raise);

    const auto caught_ir = assert_zero_alloc_leaks(
        "case ([1], 2) of (x, y) -> "
        "let caught = try raise 1 catch _ -> 0 end in "
        "let replacement = [9] in "
        "case replacement of _ -> "
        "case x of [value] -> value; _ -> 0 end end end",
        "caught_raise_preserves_outer_pattern_owner", "1",
        "tag=SEQ allocs=2 frees=2 leaked=0");
    const auto caught_raise = caught_ir.find("call void @YonaRuntimeRaise");
    REQUIRE(caught_raise != std::string::npos);
    CHECK(caught_ir.rfind("call void @YonaRuntimeRelease", caught_raise) ==
          std::string::npos);
  }

  TEST_CASE("generated channel programs release their endpoint graph") {
    const auto fixture =
        yona::test::codegen_fixtures_dir() / "channel_basic.yona";
    REQUIRE(fs::exists(fixture));
    assert_zero_alloc_leaks(read_file(fixture), "channel_basic_allocations");
  }

  TEST_CASE("raw channel natives consume references on all return paths") {
    const auto fixtures = yona::test::codegen_fixtures_dir();
    for (const auto *name :
         {"channel_capacity", "channel_try_recv_empty", "channel_deadlock_recv",
          "channel_deadlock_send"}) {
      CAPTURE(name);
      const auto fixture = fixtures / (std::string(name) + ".yona");
      REQUIRE(fs::exists(fixture));
      assert_zero_alloc_leaks(read_file(fixture),
                              std::string(name) + "_allocations");
    }
  }
}
