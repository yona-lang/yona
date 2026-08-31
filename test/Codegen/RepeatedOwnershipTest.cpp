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

std::string assert_repeated_heap_ownership(
    const std::string &source, const std::string &artifact_stem,
    const std::string &expected_output, const std::string &expected_stats = {},
    bool require_zero_leaks = true, int opt_level = 2) {
  REQUIRE(yona::test::link::ensure_runtime_objects());

  parser::Parser parser;
  Codegen codegen(artifact_stem);
  codegen.set_opt_level(opt_level);
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

  const auto object_path =
      yona::test::link::scratch_root() / ("yona_" + artifact_stem + ".o");
  REQUIRE(codegen.emit_object_file(object_path.string()));
  std::vector<fs::path> objects{object_path};
  REQUIRE(yona::test::link::append_prelude_object(objects));
  REQUIRE(yona::test::link::append_runtime_objects(objects));
  const auto executable = yona::test::link::scratch_root() /
                          (artifact_stem + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));

  const auto combined =
      yona::test::link::executeWithAllocationStats(executable);
  REQUIRE(combined != "RUN_ERROR");
  INFO("Full output:\n" << combined);
  auto normalized_output = expected_output;
  while (!normalized_output.empty() &&
         (normalized_output.back() == '\n' || normalized_output.back() == '\r'))
    normalized_output.pop_back();
  CHECK(combined.starts_with(normalized_output + "\n[alloc-stats]"));
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
    if (require_zero_leaks)
      CHECK_MESSAGE(combined.substr(position, end - position) == "0",
                    "Allocation leak in output:\n"
                        << combined);
    position = end;
  }
  REQUIRE(leak_rows > 0);
  return ir;
}

std::size_t count_ir_calls_for_value(const std::string &ir,
                                     const std::string &callee,
                                     const std::string &value) {
  std::size_t count = 0;
  for (std::size_t position = 0;
       (position = ir.find("call void @" + callee, position)) !=
       std::string::npos;
       position += callee.size()) {
    const auto line_end = ir.find('\n', position);
    if (ir.substr(position, line_end - position).find(value) !=
        std::string::npos)
      ++count;
  }
  return count;
}

} // namespace

TEST_SUITE("Repeated heap ownership") {
  TEST_CASE("sortBy reuses its pattern-bound tail across consuming filters") {
    assert_repeated_heap_ownership(
        "import sortBy from Std\\List in "
        "case sortBy (\\a b -> a - b) [3, 1, 2] of "
        "[a, b, c] -> a * 100 + b * 10 + c; _ -> 0 end",
        "sort_by_repeated_tail", "123");
  }

  TEST_CASE("a repeated predicate call preserves its captured heap pivot") {
    assert_repeated_heap_ownership(
        "import filter from Std\\List in "
        "let consume xs = case xs of [limit] -> limit; _ -> 0 end, "
        "pivot = [2], "
        "predicate = \\value -> value < consume pivot "
        "in case filter predicate [3, 1, 2] of "
        "[only] -> only; _ -> 0 end",
        "repeated_captured_pivot", "1");
  }

  TEST_CASE("case patterns adopt anonymous captured closures exactly once") {
    const auto ir =
        assert_repeated_heap_ownership("let pivot = [1], apply f = f 1 in "
                                       "case (\\x -> case pivot of "
                                       "[value] -> value + x; _ -> 0 end) of "
                                       "function -> apply function end",
                                       "pattern_adopted_anonymous_closure", "2",
                                       "tag=CLOSURE allocs=1 frees=1 leaked=0");
    CHECK(count_ir_calls_for_value(ir, "YonaRuntimeRelease",
                                   "%lambda_0_closure") == 1);
  }

  TEST_CASE("case patterns balance adopted temporary sequence ownership") {
    assert_repeated_heap_ownership("let consume values = case values of "
                                   "[value] -> value; _ -> 0 end in "
                                   "case [4] of values -> consume values end",
                                   "pattern_adopted_temporary_sequence", "4",
                                   "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("captured heap ownership survives a PHI before a consuming call") {
    const auto ir = assert_repeated_heap_ownership(
        "let consume values = case values of "
        "[value] -> value; _ -> 0 end, "
        "left = [5], right = [6], "
        "predicate = \\value -> "
        "consume (if value > 0 then left else right) in "
        "predicate 1 + predicate (0 - 1)",
        "captured_phi_consuming_call", "11",
        "tag=CLOSURE allocs=1 frees=1 leaked=0", true, 0);
    CHECK(ir.find("%if.heap.borrowed") != std::string::npos);
    CHECK(count_ir_calls_for_value(ir, "YonaRuntimeRetain",
                                   "%if.heap.borrowed") == 1);
  }

  TEST_CASE("borrowed captured PHIs are not released after sequence join") {
    const auto ir = assert_repeated_heap_ownership(
        "let left = [2], right = [4], "
        "combine = \\choice -> case "
        "((if choice then left else right) ++ [3]) of "
        "[a, b] -> a * 10 + b; _ -> 0 end in "
        "combine true + combine false",
        "captured_phi_borrowed_join", "66",
        "tag=CLOSURE allocs=1 frees=1 leaked=0", true, 0);
    CHECK(ir.find("%if.heap.borrowed") != std::string::npos);
    CHECK(count_ir_calls_for_value(ir, "YonaRuntimeRelease",
                                   "%if.heap.borrowed") == 0);
  }

  TEST_CASE("shadowed heap bindings keep independent transfer state") {
    assert_repeated_heap_ownership(
        "let consume xs = case xs of [value] -> value; _ -> 0 end, "
        "outer = [1], "
        "wrapped = Some (let outer = [2] in outer) "
        "in case wrapped of "
        "Some inner -> case inner of "
        "[innerValue] -> innerValue * 10 + consume outer; _ -> 0 end; "
        "None -> 0 end",
        "shadowed_heap_transfer", "21", "tag=SEQ allocs=2 frees=2 leaked=0");
  }

  TEST_CASE("case-local analysis does not transfer an outer heap binding") {
    assert_repeated_heap_ownership(
        "let consume xs = case xs of [value] -> value; _ -> 0 end, "
        "xs = [1], "
        "first = case 0 of _ -> consume xs end "
        "in first * 10 + consume xs",
        "outer_heap_used_after_case", "11",
        "tag=SEQ allocs=1 frees=1 leaked=0");
  }

  TEST_CASE("same-name lambda parameters do not inherit pattern ownership") {
    assert_repeated_heap_ownership(
        "let consume xs = case xs of [value] -> value; _ -> 0 end "
        "in case [1, 3] of "
        "[head|xs] -> let use xs = consume xs * 10 + consume xs "
        "in use [2] + consume xs; _ -> 0 end",
        "lambda_parameter_shadows_pattern", "25");
  }

  TEST_CASE("nested let cleanup does not retain an unrelated heap result") {
    assert_repeated_heap_ownership("case (let x = [1] in [2]) of "
                                   "[value] -> value; _ -> 0 end",
                                   "nested_let_fresh_result", "2",
                                   "tag=SEQ allocs=2 frees=2 leaked=0");
  }

  TEST_CASE("sequence join releases anonymous borrowed operands") {
    assert_repeated_heap_ownership("case ([1] ++ [2]) of "
                                   "[a, b] -> a * 10 + b; _ -> 0 end",
                                   "join_anonymous_operands", "12",
                                   "tag=SEQ allocs=3 frees=3 leaked=0");
  }

  TEST_CASE("sequence join preserves named borrowed operands") {
    assert_repeated_heap_ownership(
        "let left = [1], joined = left ++ [2] "
        "in case joined of [a, b] -> "
        "case left of [c] -> a * 100 + b * 10 + c; _ -> 0 end; "
        "_ -> 0 end",
        "join_named_operand_reuse", "121", "tag=SEQ allocs=2 frees=2 leaked=0");
  }

  TEST_CASE("lifted dictionary trait methods preserve captured values") {
    assert_repeated_heap_ownership(
        read_file(yona::test::codegen_fixtures_dir() /
                  "dict_lifted_trait_lifetime.yona"),
        "dict_lifted_trait_lifetime_ownership",
        read_file(yona::test::codegen_fixtures_dir() /
                  "dict_lifted_trait_lifetime.expected"),
        {}, false);
  }
}
