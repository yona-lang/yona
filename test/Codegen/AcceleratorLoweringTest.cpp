#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "yona/Codegen/AcceleratorLowering.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Ast.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

using yona::ast::ApplyExpr;
using yona::ast::AstNode;
using yona::ast::ExprCall;
using yona::ast::ExprNode;
using yona::ast::ImportExpr;
using yona::ast::MainNode;
using yona::ast::ValueExpr;
using yona::compiler::AccelKernel;
using yona::compiler::AccelMatch;
using yona::compiler::collect_transparent_matches;
using yona::compiler::DiagnosticEngine;
using yona::compiler::is_unlowerable_column_apply;
using yona::compiler::match_transparent_apply;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;
namespace fs = std::filesystem;

static auto parse_expr(parser::Parser &parser, const char *source) {
  auto parse_result = parser.parseExpression(source, "<accelerator-test>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  return parse_result;
}

static void add_lib_paths(Codegen &codegen) {
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
}

TEST_CASE("transparent lowering matches IntArray map add/mul and foldl sum") {
  parser::Parser parser;
  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
let xs = fromSeq [1, 2, 3, 4, 5] in
let shifted = map (\x -> x + 1) xs in
let doubled = map (\x -> x * 2) shifted in
foldl (\a b -> a + b) 0 doubled
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  REQUIRE(matches.size() == 3);
  CHECK(matches[0].kernel == AccelKernel::IntMapAdd);
  CHECK(matches[1].kernel == AccelKernel::IntMapMul);
  CHECK(matches[2].kernel == AccelKernel::IntReduceSum);
  CHECK(std::string(matches[0].abi_symbol) == "YonaStdGpuRawMapAdd");
  CHECK(matches[0].binding.find("IntArray") != std::string::npos);
}

TEST_CASE("transparent lowering matches IntArray filter greater-than") {
  parser::Parser parser;
  const char *source =
      R"YT(import map, filter, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (filter (\x -> x > 2) (fromSeq [1, 2, 3, 4, 5]))
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  bool saw_filter = false;
  bool saw_reduce = false;
  for (auto &m : matches) {
    if (m.kernel == AccelKernel::IntFilterGt)
      saw_filter = true;
    if (m.kernel == AccelKernel::IntReduceSum)
      saw_reduce = true;
  }
  CHECK(saw_filter);
  CHECK(saw_reduce);
}

TEST_CASE("transparent lowering matches IntArray map subtract and negate") {
  parser::Parser parser;
  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
let xs = fromSeq [1, 2, 3, 4, 5] in
let shifted = map (\x -> x - 1) xs in
let negated = map (\x -> 0 - x) shifted in
foldl (\a b -> a + b) 0 negated
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  REQUIRE(matches.size() == 3);
  CHECK(matches[0].kernel == AccelKernel::IntMapAdd);
  CHECK(matches[0].scalar_is_literal);
  CHECK(matches[0].lit_i64 == -1);
  CHECK(matches[1].kernel == AccelKernel::IntMapMul);
  CHECK(matches[1].scalar_is_literal);
  CHECK(matches[1].lit_i64 == -1);
  CHECK(matches[2].kernel == AccelKernel::IntReduceSum);
}

TEST_CASE("transparent lowering matches IntArray filter less-than") {
  parser::Parser parser;
  const char *source = R"YT(import filter, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (filter (\x -> x < 3) (fromSeq [1, 2, 3, 4, 5]))
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  bool saw_filter = false;
  for (auto &m : matches) {
    if (m.kernel == AccelKernel::IntFilterLt) {
      saw_filter = true;
      CHECK(std::string(m.abi_symbol) == "YonaStdGpuRawFilterLessThan");
    }
  }
  CHECK(saw_filter);
}

TEST_CASE("transparent lowering matches FloatArray scale and sum") {
  parser::Parser parser;
  const char *source = R"YT(import fill, map, foldl from Std\FloatArray in
let a = fill 4 1.5 in
foldl (\a b -> a + b) 0.0 (map (\x -> x * 2.0) a)
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  bool saw_scale = false;
  bool saw_sum = false;
  for (auto &m : matches) {
    if (m.kernel == AccelKernel::FloatScale)
      saw_scale = true;
    if (m.kernel == AccelKernel::FloatReduceSum)
      saw_sum = true;
  }
  CHECK(matches.size() == 2);
  CHECK(saw_scale);
  CHECK(saw_sum);
}

TEST_CASE("transparent lowering matches IntArray map square") {
  parser::Parser parser;
  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x * x) (fromSeq [1, 2, 3]))
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  bool saw_square = false;
  bool saw_reduce = false;
  for (auto &m : matches) {
    if (m.kernel == AccelKernel::IntMapSquare)
      saw_square = true;
    if (m.kernel == AccelKernel::IntReduceSum)
      saw_reduce = true;
  }
  CHECK(saw_square);
  CHECK(saw_reduce);
}

TEST_CASE("transparent lowering rejects arbitrary map lambdas") {
  parser::Parser parser;
  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x + x * x) (fromSeq [1, 2, 3]))
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  bool saw_map = false;
  bool saw_reduce = false;
  for (auto &m : matches) {
    if (m.kernel == AccelKernel::IntMapMul ||
        m.kernel == AccelKernel::IntMapAdd)
      saw_map = true;
    if (m.kernel == AccelKernel::IntReduceSum)
      saw_reduce = true;
  }
  CHECK_FALSE(saw_map);
  CHECK(saw_reduce);
}

TEST_CASE("transparent lowering emits GPU ABI in IR for IntArray map add") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  add_lib_paths(codegen);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
let xs = fromSeq [1, 2, 3] in
foldl (\a b -> a + b) 0 (map (\x -> x + 1) xs)
)YT";
  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  auto *mod = codegen.compile(parse_result->Expression.get());
  REQUIRE(mod);
  const std::string ir = codegen.emit_ir();
  CHECK(ir.find("YonaStdGpuRawMapAdd") != std::string::npos);
  CHECK(ir.find("YonaStdGpuRawReduceSum") != std::string::npos);
  CHECK(ir.find("YonaStdIntArrayMap") == std::string::npos);
}

TEST_CASE("transparent lowering emits GPU ABI in IR for IntArray map square") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  add_lib_paths(codegen);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x * x) (fromSeq [1, 2, 3]))
)YT";
  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  auto *mod = codegen.compile(parse_result->Expression.get());
  REQUIRE(mod);
  const std::string ir = codegen.emit_ir();
  CHECK(ir.find("YonaStdGpuRawMapSquare") != std::string::npos);
  CHECK(ir.find("YonaStdGpuRawReduceSum") != std::string::npos);
  CHECK(ir.find("YonaStdIntArrayMap") == std::string::npos);
}

TEST_CASE(
    "transparent lowering leaves compound map on the IntArray closure path") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  add_lib_paths(codegen);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x + x * x) (fromSeq [1, 2, 3]))
)YT";
  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  auto *mod = codegen.compile(parse_result->Expression.get());
  REQUIRE(mod);
  const std::string ir = codegen.emit_ir();
  CHECK(ir.find("YonaStdIntArrayMap") != std::string::npos);
  CHECK(ir.find("YonaStdGpuRawMapSquare") == std::string::npos);
  CHECK(ir.find("YonaStdGpuRawReduceSum") != std::string::npos);
}

TEST_CASE("transparent lowering can be disabled and keeps host IntArray map") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  add_lib_paths(codegen);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  codegen.set_accelerator_lowering(false);

  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x + 1) (fromSeq [1, 2, 3]))
)YT";
  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  auto *mod = codegen.compile(parse_result->Expression.get());
  REQUIRE(mod);
  const std::string ir = codegen.emit_ir();
  CHECK(ir.find("YonaStdIntArrayMap") != std::string::npos);
  CHECK(ir.find("YonaStdGpuRawMapAdd") == std::string::npos);
}

TEST_CASE("transparent lowering does not rewrite Std\\List.map") {
  parser::Parser parser;
  const char *source = R"YT(import map, foldl from Std\List in
foldl (\a b -> a + b) 0 (map (\x -> x + 1) [1, 2, 3])
)YT";
  auto parsed = parse_expr(parser, source);
  auto matches = collect_transparent_matches(parsed->Expression.get());
  for (auto &m : matches)
    CHECK(m.kernel != AccelKernel::IntMapAdd);
}

TEST_CASE("is_unlowerable_column_apply detects compound map") {
  parser::Parser parser;
  const char *source = R"YT(import map, fromSeq from Std\IntArray in
map (\x -> x + x * x) (fromSeq [1, 2, 3])
)YT";
  auto parsed = parse_expr(parser, source);
  bool found = false;
  std::function<void(AstNode *)> walk;
  walk = [&](AstNode *n) {
    if (!n || found)
      return;
    if (auto *ae = dynamic_cast<ApplyExpr *>(n)) {
      if (is_unlowerable_column_apply(ae)) {
        found = true;
        CHECK_FALSE(match_transparent_apply(ae).has_value());
      }
      for (auto &a : ae->args) {
        if (std::holds_alternative<ExprNode *>(a))
          walk(std::get<ExprNode *>(a));
        else
          walk(std::get<ValueExpr *>(a));
      }
      if (auto *ec = dynamic_cast<ExprCall *>(ae->call))
        walk(ec->expr);
      return;
    }
    if (auto *main = dynamic_cast<MainNode *>(n)) {
      walk(main->node);
      return;
    }
    if (auto *imp = dynamic_cast<ImportExpr *>(n)) {
      walk(imp->expr);
      return;
    }
  };
  walk(parsed->Expression.get());
  CHECK(found);
}

TEST_CASE("strict accelerator errors on compound map (E0700)") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program", &diag);
  add_lib_paths(codegen);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  codegen.set_strict_accelerator(true);

  const char *source = R"YT(import map, fromSeq, foldl from Std\IntArray in
foldl (\a b -> a + b) 0 (map (\x -> x + x * x) (fromSeq [1, 2, 3]))
)YT";
  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());
  auto *mod = codegen.compile(parse_result->Expression.get());
  (void)mod;
  CHECK(codegen.errorCount() >= 1);
  CHECK(diag.has_errors());
}
