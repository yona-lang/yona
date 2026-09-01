#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "Toolchain/YonaLinkUtil.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Codegen/DeriveEngine.h"
#include "yona/Semantics/AcceleratorDiag.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

using yona::SourceId;
using yona::SourceManager;
using yona::SourceRange;
using yona::ast::CaseExpr;
using yona::ast::LetExpr;
using yona::ast::PerformExpr;
using yona::ast::ValueAlias;
using yona::compiler::DiagnosticEngine;
using yona::compiler::DiagLevel;
using yona::compiler::ErrorCode;
using yona::compiler::WarningFlag;
using yona::compiler::codegen::Codegen;
using yona::compiler::codegen::CodegenSession;
using yona::compiler::codegen::DeriveAdtInfo;
using yona::compiler::emit_accelerator_diagnostic_report;
using yona::compiler::emit_accelerator_diagnostic_report_for_module;
using yona::compiler::typecheck_module_for_accelerator_report;
namespace ast = yona::ast;
namespace compiler = yona::compiler;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;
namespace fs = std::filesystem;
using std::array;
using std::exception;
using std::fstream;
using std::ifstream;
using std::ios;
using std::istreambuf_iterator;
using std::istringstream;
using std::map;
using std::ofstream;
using std::ostringstream;
using std::set;
using std::size_t;
using std::sort;
using std::string;
using std::stringstream;
using std::tuple;
using std::vector;

// ===== Helpers =====

static SourceId set_diagnostic_source(DiagnosticEngine &Diagnostics,
                                      std::string Source, std::string Name) {
  auto Sources = std::make_shared<SourceManager>();
  const SourceId Id = Sources->addSource(std::move(Name), std::move(Source));
  Diagnostics.setSources(std::move(Sources));
  return Id;
}

static string compile_to_ir(const string &code, int opt_level = 0) {
  parser::Parser parser;
  istringstream stream(code);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  if (!parse_result || !parse_result->Expression)
    return "PARSE_ERROR";

  Codegen codegen("yona_program");
  codegen.set_opt_level(opt_level);
  auto module = codegen.compile(parse_result->Expression.get());
  if (!module)
    return "CODEGEN_ERROR";

  return codegen.emit_ir();
}

static bool ir_contains(const string &ir, const string &pattern) {
  return ir.find(pattern) != string::npos;
}

static string ir_function_body(const string &ir, const string &fn_name) {
  string marker = "define internal fastcc";
  size_t start = ir.find(marker + " ");
  while (start != string::npos) {
    size_t name_pos = ir.find("@" + fn_name + "(", start);
    size_t next = ir.find("\ndefine ", start + 1);
    if (name_pos != string::npos && (next == string::npos || name_pos < next))
      return ir.substr(start,
                       next == string::npos ? string::npos : next - start);
    start = ir.find(marker + " ", start + 1);
  }
  return "";
}

static string compile_and_run(const string &code,
                              const char *run_env_key = nullptr,
                              const char *run_env_val = nullptr,
                              const char *artifact_suffix = nullptr,
                              const char *stdin_data = nullptr,
                              int opt_level = 2,
                              const fs::path *extra_module_path = nullptr) {
  parser::Parser parser;

  Codegen codegen("test_module");
  codegen.set_opt_level(opt_level);
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  if (extra_module_path)
    codegen.ModulePaths.push_back(extra_module_path->string());
  // Type check (blocking)
  DiagnosticEngine tc_diag;
  typechecker::TypeChecker type_checker(tc_diag);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  for (auto &p : codegen.ModulePaths)
    type_checker.add_module_path(p);

  istringstream stream(code);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  if (!parse_result || !parse_result->Expression)
    return "PARSE_ERROR";
  type_checker.check(parse_result->Expression.get());
  if (!type_checker.solve_constraints() || type_checker.has_errors())
    return "TYPE_ERROR";
  codegen.set_type_checker(&type_checker);

  auto module = codegen.compile(parse_result->Expression.get());
  if (!module)
    return "CODEGEN_ERROR";

  const string stem = artifact_suffix
                          ? string("yona_codegen_") + artifact_suffix + ".o"
                          : "yona_codegen_test.o";
  const string exe_stem = artifact_suffix
                              ? string("yona_codegen_") + artifact_suffix
                              : "yona_codegen_test";
  fs::path obj_path = yona::test::link::scratch_root() / stem;
  if (!codegen.emit_object_file(obj_path.string()))
    return "EMIT_ERROR";

  vector<fs::path> objs = {obj_path};
  if (!yona::test::link::append_prelude_object(objs))
    return "PRELUDE_OBJECT_ERROR";
  if (!yona::test::link::append_runtime_objects(objs))
    return "RT_COMPILE_ERROR";

  const auto extra_libs = yona::test::link::pcreLinkArguments();

  fs::path exe_path = yona::test::link::scratch_root() /
                      (exe_stem + yona::test::link::exe_suffix());
  if (!yona::test::link::link_objs_to_exe(objs, exe_path, extra_libs))
    return "LINK_ERROR";

  if (stdin_data)
    return yona::test::link::executeAndCapture(
        exe_path, {}, {.StandardInput = string(stdin_data)});
  if (run_env_key && run_env_val)
    return yona::test::link::executeAndCapture(
        exe_path, {}, {.EnvironmentOverrides = {{run_env_key, run_env_val}}});
  return yona::test::link::executeAndCapture(exe_path);
}

TEST_CASE("Logical composition normalizes higher-order Bool results") {
  const string source = R"(
import run, render from Std\Test,
       eqLaws, ordLaws, hashLaws, showLaws from Std\TraitLaws in
let orderingRender = \value -> case value of
        Less -> "Less"
        Equal -> "Equal"
        Greater -> "Greater"
    end
in render (run (
    eqLaws "Int" (\value -> show value) (\left right -> left == right) [0, 1] ++
    ordLaws "Int" (\value -> show value) (\left right -> left == right)
        (\left right -> compare left right) [0, 1] ++
    hashLaws "Int" (\value -> show value) (\left right -> left == right)
        (\value -> hash value) [0, 1] ++
    showLaws "Int" (\value -> show value) [0, 1] ++
    eqLaws "Ordering" orderingRender (\left right -> left == right)
        [Less, Equal, Greater] ++
    ordLaws "Ordering" orderingRender (\left right -> left == right)
        (\left right -> compare left right) [Less, Equal, Greater]
))
)";
  for (int opt_level = 0; opt_level <= 3; ++opt_level) {
    CAPTURE(opt_level);
    const auto result = compile_and_run(
        source, nullptr, nullptr, "higher_order_bool", nullptr, opt_level);
    CHECK(result.find("SUMMARY 12 passed, 0 failed") != string::npos);
  }
}

TEST_CASE(
    "Imported nested sequence equality preserves generic specializations") {
  const string source = R"(
import compile, findAll from Std\Regex in
findAll (compile "[0-9]+") "a1b22" == [["1"], ["22"]]
)";
  // The imported Regex implementation calls generic sequence equality both
  // for the outer result and for its inner string matches. Exercise every
  // optimization pipeline: a mismatched cached specialization used to emit a
  // two-argument call to the three-argument `seqEqBy` implementation.
  for (int opt_level = 0; opt_level <= 3; ++opt_level) {
    CAPTURE(opt_level);
    CHECK(compile_and_run(source, nullptr, nullptr, "imported_nested_seq_eq",
                          nullptr, opt_level) == "true");
  }
}

TEST_CASE(
    "Imported Std Convert intToFloat retains its native dependency owner") {
  const string source = R"(
import intToFloat from Std\Convert in
case intToFloat 42 of
    Ok value -> if value == 42.0 then 1 else 0
    Err _ -> -1
end
)";
  CHECK(compile_and_run(source, nullptr, nullptr,
                        "imported_convert_int_to_float") == "1");
}

TEST_CASE("Imported Std Convert siblings keep private dependency owners "
          "separate") {
  const string source = R"(
import intToFloat, floatToInt from Std\Convert in
case intToFloat 42 of
    Ok floatValue -> case floatToInt 7.0 of
        Ok intValue -> if floatValue == 42.0 then intValue else 0
        Err _ -> -1
    end
    Err _ -> -2
end
)";
  CHECK(compile_and_run(source, nullptr, nullptr,
                        "imported_convert_owner_isolation") == "7");
}

TEST_CASE("Imported private generic helpers retain their exact native "
          "dependencies") {
  const fs::path module_root =
      yona::test::link::scratch_root() / "yona_genfn_dependency_owner";
  fs::create_directories(module_root / "Test");

  parser::Parser module_parser;
  auto module = module_parser.parseModule(R"(
module Test\DependencyOwner

export run

extern absNative : Int -> Int = "YonaStdMathAbs"
helper value = absNative value
run value = helper value
)",
                                          "dependency_owner.yona");
  REQUIRE(module.has_value());

  Codegen module_codegen("dependency_owner_module");
  REQUIRE(module_codegen.compile_module(module.value().get()) != nullptr);
  DiagnosticEngine module_diag;
  typechecker::TypeChecker module_checker(module_diag);
  module_codegen.populate_interface_effect_rows(module.value().get(),
                                                module_checker);
  REQUIRE_FALSE(module_checker.has_errors());
  REQUIRE(module_codegen.emit_interface_file(
      (module_root / "Test" / "DependencyOwner.yonai").string()));

  const string source =
      R"(import run from Test\DependencyOwner in run (0 - 42))";
  CHECK(compile_and_run(source, nullptr, nullptr,
                        "imported_private_genfn_dependency", nullptr, 2,
                        &module_root) == "42");
}

TEST_CASE("ABI refinement leaves one canonical function") {
  const auto ir = compile_to_ir("let f x = x == 0 in f 1", 0);
  const auto occurrence_count = [&ir](const string &needle) {
    size_t count = 0;
    for (size_t pos = 0; (pos = ir.find(needle, pos)) != string::npos;
         pos += needle.size()) {
      ++count;
    }
    return count;
  };

  CHECK(ir != "CODEGEN_ERROR");
  CHECK(occurrence_count("define internal fastcc i1 @f(i64 %x)") == 1);
  CHECK(ir.find("@f.") == string::npos);
  CHECK(occurrence_count("call fastcc i1 @f(i64 1)") == 1);
}

TEST_CASE("CodegenSession owns the complete LLVM lifecycle") {
  auto Session = std::make_unique<CodegenSession>("session_lifecycle");
  auto *SessionAddress = Session.get();
  auto *ContextAddress = &Session->context();
  auto *TargetAddress = Session->targetMachine();
  REQUIRE(TargetAddress != nullptr);

  Codegen CodegenValue(std::move(Session));
  CHECK(&CodegenValue.session() == SessionAddress);
  CHECK(&CodegenValue.session().context() == ContextAddress);
  CHECK(CodegenValue.session().targetMachine() == TargetAddress);
  CHECK(CodegenValue.session().module().getName() == "session_lifecycle");
}

TEST_CASE("CodegenSession isolates mutable compilation state") {
  auto FirstSession = std::make_unique<CodegenSession>("first_session");
  auto SecondSession = std::make_unique<CodegenSession>("second_session");
  auto *First = FirstSession.get();
  auto *Second = SecondSession.get();

  REQUIRE(&First->context() != &Second->context());
  REQUIRE(&First->module() != &Second->module());
  REQUIRE(&First->builder() != &Second->builder());
  REQUIRE(First->derivations().isDerivable("Show"));
  REQUIRE(Second->derivations().isDerivable("Show"));
  First->derivations().registerStrategy(
      "FirstSessionTrait", {"derive"},
      [](const DeriveAdtInfo &) { return std::string("derive x = x\n"); });
  CHECK(First->derivations().isDerivable("FirstSessionTrait"));
  CHECK_FALSE(Second->derivations().isDerivable("FirstSessionTrait"));

  Codegen FirstCodegen(std::move(FirstSession));
  Codegen SecondCodegen(std::move(SecondSession));
  First->module().getOrInsertGlobal("FirstSessionOnly",
                                    llvm::Type::getInt64Ty(First->context()));
  CHECK(First->module().getNamedGlobal("FirstSessionOnly") != nullptr);
  CHECK(Second->module().getNamedGlobal("FirstSessionOnly") == nullptr);

  First->recordError();
  First->diagnostics().error(SourceRange::unknown(), "first session error");
  CHECK(FirstCodegen.errorCount() == 1);
  CHECK(SecondCodegen.errorCount() == 0);
  CHECK(First->diagnostics().has_errors());
  CHECK_FALSE(Second->diagnostics().has_errors());
}

static string read_file(const fs::path &path);

static string trim_cell(string value) {
  const auto first = value.find_first_not_of(" \t");
  if (first == string::npos)
    return "";
  const auto last = value.find_last_not_of(" \t");
  return value.substr(first, last - first + 1);
}

static string fixture_name(const fs::path &root, const fs::path &yona_file) {
  auto relative = fs::relative(yona_file, root);
  relative.replace_extension();
  string name = relative.generic_string();
  for (char &ch : name)
    if (ch == '/' || ch == '\\' || ch == '-' || ch == '.')
      ch = '_';
  return name;
}

static void run_yona_fixture_tree(const fs::path &fixtures_dir) {
  const bool exists =
      fs::exists(fixtures_dir) && fs::is_directory(fixtures_dir);
  REQUIRE_MESSAGE(exists,
                  "Could not find fixture directory: ", fixtures_dir.string());

  vector<fs::path> test_files;
  for (const auto &entry : fs::recursive_directory_iterator(fixtures_dir))
    if (entry.is_regular_file() && entry.path().extension() == ".yona")
      test_files.push_back(entry.path());
  sort(test_files.begin(), test_files.end());
  REQUIRE(!test_files.empty());

  for (const auto &yona_file : test_files) {
    auto expected_file = yona_file;
    expected_file.replace_extension(".expected");
    REQUIRE_MESSAGE(fs::exists(expected_file),
                    "Missing expected output for fixture: ",
                    fs::relative(yona_file, fixtures_dir).generic_string());

    string source = read_file(yona_file);
    yona::test::link::rewrite_codegen_fixture_tmp_paths(source);
    const string expected = read_file(expected_file);
    const string name = fixture_name(fixtures_dir, yona_file);
    const string stem = yona_file.stem().string();

    SUBCASE(name.c_str()) {
      const char *env_k = nullptr;
      const char *env_v = nullptr;
      const char *stdin_data = nullptr;
      if (stem == "gpu_backend_flags" || stem == "gpu_vulkan_last_note" ||
          stem == "stdlib_gpu") {
        env_k = "YONA_GPU_DISABLE_VULKAN";
        env_v = "1";
      }
      if (stem == "stdlib_json_get_import_length")
        stdin_data = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                     "\"params\":{\"capabilities\":{}}}";
      string actual =
          compile_and_run(source, env_k, env_v, name.c_str(), stdin_data);
      actual.erase(std::remove(actual.begin(), actual.end(), '\r'), actual.end());
      while (!actual.empty() &&
             (actual.back() == '\n' || actual.back() == ' '))
        actual.pop_back();
      CHECK_MESSAGE(actual == expected, "Fixture '", name, "': expected '",
                    expected, "' but got '", actual, "'");
    }
  }
}

/* Ensures merged runtime .o exists (same artifact compile_and_run uses). */
static void ensure_runtime_archive() {
  REQUIRE(yona::test::link::ensure_runtime_objects());
}

/* Link yona .o + runtime, run with YONA_ALLOC_STATS=1; fail if any tag shows
 * leaked>0. */
static void assert_linked_yona_zero_alloc_leaks(const string &obj_path,
                                                const string &exe_path_stem) {
  vector<fs::path> o = {fs::path(obj_path)};
  REQUIRE(yona::test::link::append_prelude_object(o));
  REQUIRE(yona::test::link::append_runtime_objects(o));
  fs::path exe_path = yona::test::link::scratch_root() /
                      (exe_path_stem + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(o, exe_path));

  const string combined =
      yona::test::link::executeWithAllocationStats(exe_path);
  REQUIRE(combined != "RUN_ERROR");

  INFO("Full output:\n" << combined);
  CHECK(combined.find("alloc-stats") != string::npos);

  size_t pos = 0;
  while ((pos = combined.find("leaked=", pos)) != string::npos) {
    pos += 7;
    size_t end = pos;
    while (end < combined.size() && isdigit((unsigned char)combined[end]))
      end++;
    string n = combined.substr(pos, end - pos);
    CHECK_MESSAGE(n == "0", "Found leaked=" << n << " in alloc stats. Output:\n"
                                            << combined);
    pos = end;
  }
}

static void assert_yona_source_zero_alloc_leaks(const string &source,
                                                const string &artifact_stem) {
  ensure_runtime_archive();
  parser::Parser parser;
  Codegen codegen(artifact_stem);
  codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  DiagnosticEngine diagnostics;
  typechecker::TypeChecker checker(diagnostics);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, checker);
  checker.add_module_path(yona::test::lib_dir().string());

  auto parsed = parser.parseExpression(source, artifact_stem + ".yona");
  REQUIRE(parsed.has_value());
  checker.check(parsed->Expression.get());
  REQUIRE(checker.solve_constraints());
  REQUIRE_FALSE(checker.has_errors());
  codegen.set_type_checker(&checker);
  REQUIRE(codegen.compile(parsed->Expression.get()) != nullptr);

  const auto object =
      yona::test::link::scratch_root() / ("yona_" + artifact_stem + ".o");
  REQUIRE(codegen.emit_object_file(object.string()));
  assert_linked_yona_zero_alloc_leaks(object.string(), artifact_stem);
}

static string read_file(const fs::path &path) {
  ifstream f(path);
  if (!f.is_open())
    return "";
  stringstream buf;
  buf << f.rdbuf();
  string content = buf.str();
  content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
  // Trim trailing whitespace/newlines
  while (!content.empty() && (content.back() == '\n' ||
                              content.back() == '\r' || content.back() == ' '))
    content.pop_back();
  return content;
}

// ===== IR Snapshot Tests =====

TEST_SUITE("Codegen IR") {

  TEST_CASE("Integer addition uses native add") {
    auto ir = compile_to_ir("1 + 2");
    CHECK(ir_contains(ir, "YonaRuntimePrintInt(i64 3)"));
  }

  TEST_CASE("If expression with constant condition is optimized away") {
    auto ir = compile_to_ir("if true then 1 else 0", 2);
    // O2 constant-folds: if true â†’ 1, eliminates branch
    CHECK(ir_contains(ir, "YonaRuntimePrintInt(i64 1)"));
  }

  TEST_CASE("If expression with variable generates branch") {
    auto ir = compile_to_ir("let x = 5 in if x > 3 then 1 else 0", 2);
    // O2 constant-folds this (5 > 3 = true â†’ 1)
    CHECK(ir_contains(ir, "YonaRuntimePrintInt(i64 1)"));
  }

  TEST_CASE("With expression generates close call") {
    auto ir = compile_to_ir("with fd = 0 in 42");
    CHECK(ir_contains(ir, "YonaRuntimeClose"));
  }

  TEST_CASE("Borrow inference eliminates rc_inc for closure param in foldl") {
    auto ir = compile_to_ir("let foldl fn acc seq = case seq of [] -> acc; "
                            "[h|t] -> foldl fn (fn acc h) t end in "
                            "foldl (\\a b -> a + b) 0 [1,2,3]");
    auto foldl_body = ir_function_body(ir, "foldl");
    REQUIRE(!foldl_body.empty());
    // The top-level caller and runtime support may legitimately retain other
    // heap values. The recursively forwarded closure itself remains borrowed.
    CHECK(!ir_contains(foldl_body, "call void @YonaRuntimeRetain"));
  }

  TEST_CASE("Borrowed heap params are excluded from owned cleanup paths") {
    auto borrowed_ir = compile_to_ir("let inspect xs = 1 in inspect [1,2,3]");
    auto borrowed_body = ir_function_body(borrowed_ir, "inspect");
    REQUIRE(!borrowed_body.empty());
    CHECK(!ir_contains(borrowed_body, "call void @YonaRuntimeRelease"));
    CHECK(!ir_contains(borrowed_ir, "call void @YonaRuntimeFramePush"));
    CHECK(ir_contains(borrowed_ir, "call void @YonaRuntimeRelease"));

    auto owned_ir = compile_to_ir("let keep xs = xs in keep [1,2,3]");
    auto owned_body = ir_function_body(owned_ir, "keep");
    REQUIRE(!owned_body.empty());
    CHECK(ir_contains(owned_ir, "call void @YonaRuntimeFramePush"));

    auto raising_ir =
        compile_to_ir("let always_raise xs = raise 42 in "
                      "try always_raise [1,2,3] catch _ -> 0 end");
    auto raising_body = ir_function_body(raising_ir, "always_raise");
    REQUIRE(!raising_body.empty());
    CHECK(ir_contains(raising_ir, "call void @YonaRuntimeFramePush"));
  }

  TEST_CASE("Std File native calls borrow observational heap parameters") {
    const string source = R"(
import appendFile, exists, openFile, writeBytes, seek, closeFileHandle from Std\File,
       fromString from Std\ByteArray in
let touch path content bytes = do
    appendFile path content
    exists path
    case openFile path Write of
        Linear h -> do
            writeBytes h bytes
            seek h 0 SeekSet
            closeFileHandle h
        end
    end
end in touch "/tmp/yona_file_owner_ir" "x" (fromString "y")
)";

    parser::Parser parser;
    Codegen codegen("file_native_borrow_ir");
    codegen.set_opt_level(0);
    codegen.ModulePaths.push_back(yona::test::lib_dir().string());
    DiagnosticEngine diagnostics;
    typechecker::TypeChecker checker(diagnostics);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, checker);
    checker.add_module_path(yona::test::lib_dir().string());

    auto parsed = parser.parseExpression(source, "file_native_borrow_ir.yona");
    REQUIRE(parsed.has_value());
    checker.check(parsed->Expression.get());
    REQUIRE(checker.solve_constraints());
    REQUIRE_FALSE(checker.has_errors());
    codegen.set_type_checker(&checker);
    REQUIRE(codegen.compile(parsed->Expression.get()) != nullptr);

    const auto body = ir_function_body(codegen.emit_ir(), "touch");
    REQUIRE_FALSE(body.empty());
    const auto Count = [&](const string &Needle) {
      std::size_t Result = 0;
      for (std::size_t Position = 0;
           (Position = body.find(Needle, Position)) != string::npos;
           Position += Needle.size())
        ++Result;
      return Result;
    };

    CHECK(Count("call void @YonaRuntimeRetain") == 2);
    CHECK(body.find("call void @YonaRuntimeRetain(ptr %path)") == string::npos);
    CHECK(body.find("call void @YonaRuntimeRetain(ptr %content)") ==
          string::npos);
    CHECK(body.find("call void @YonaRuntimeRetain(ptr %bytes)") ==
          string::npos);
    CHECK(Count("call void @YonaRuntimeRelease(ptr %adt_node") == 2);
  }

  TEST_CASE("Std File borrowed heap inputs are caller-released") {
    ensure_runtime_archive();
    const string source = R"(
import appendFile, remove from Std\File in
let save path content = do
    appendFile path content
    remove path
    0
end in save ("/tmp/" ++ "yona_file_native_ownership") ("borrowed" ++ "-content")
)";

    parser::Parser parser;
    Codegen codegen("file_native_ownership");
    codegen.ModulePaths.push_back(yona::test::lib_dir().string());
    DiagnosticEngine diagnostics;
    typechecker::TypeChecker checker(diagnostics);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, checker);
    checker.add_module_path(yona::test::lib_dir().string());

    auto parsed = parser.parseExpression(source, "file_native_ownership.yona");
    REQUIRE(parsed.has_value());
    checker.check(parsed->Expression.get());
    REQUIRE(checker.solve_constraints());
    REQUIRE_FALSE(checker.has_errors());
    codegen.set_type_checker(&checker);
    REQUIRE(codegen.compile(parsed->Expression.get()) != nullptr);

    const auto Object =
        yona::test::link::scratch_root() / "yona_file_native_ownership.o";
    REQUIRE(codegen.emit_object_file(Object.string()));
    assert_linked_yona_zero_alloc_leaks(Object.string(),
                                        "yona_file_native_ownership");
  }

  TEST_CASE("Std Json native observers borrow heap inputs") {
    const std::vector<std::pair<string, string>> cases = {
        {"json_parse_borrow",
         R"(import parse from Std\Json in case parse ("1" ++ "") of Ok _ -> 0; Err _ -> 1 end)"},
        {"json_stringify_borrow",
         R"(import stringify, JsonString from Std\Json in stringify (JsonString ("x" ++ "")))"},
        {"json_stringify_string_borrow",
         R"(import stringifyString from Std\Json in stringifyString ("x" ++ ""))"},
        {"json_parse_int_borrow",
         R"(import parseInt from Std\Json in parseInt ("1" ++ ""))"},
        {"json_parse_float_borrow",
         R"(import parseFloat from Std\Json in parseFloat ("1.5" ++ ""))"},
    };

    for (const auto &[name, source] : cases) {
      CAPTURE(name);
      assert_yona_source_zero_alloc_leaks(source, name);
    }
  }

  TEST_CASE("task-backed externs reject borrow masks without lifetime pins") {
    for (const string kind : {"async", "native"}) {
      CAPTURE(kind);
      const string source = "extern " + kind +
                            " inspect : String -> Int = \"YonaTestInspect\" "
                            "borrow \"1\" in inspect \"value\"";
      parser::Parser parser;
      auto parsed = parser.parseExpression(source, "extern_task_borrow.yona");
      REQUIRE(parsed.has_value());

      DiagnosticEngine diagnostics;
      Codegen codegen("extern_task_borrow", &diagnostics);
      typechecker::TypeChecker checker(diagnostics);
      YONA_TEST_INSTALL_PRELUDE(codegen, parser, checker);
      checker.check(parsed->Expression.get());
      REQUIRE(checker.solve_constraints());
      REQUIRE_FALSE(checker.has_errors());
      codegen.set_type_checker(&checker);
      (void)codegen.compile(parsed->Expression.get());
      CHECK(diagnostics.has_errors());
    }
  }

  TEST_CASE("nested try success uses inner result") {
    CHECK(compile_and_run("try (try 1 catch _ -> 2 end) catch _ -> 3 end",
                          nullptr, nullptr, "nested_try_success") == "1");
  }

  TEST_CASE("nested try inner catch does not escape") {
    CHECK(compile_and_run("try (try raise 1 catch _ -> 2 end) catch _ -> 3 end",
                          nullptr, nullptr, "nested_try_inner_catch") == "2");
  }

  TEST_CASE("nested try inner catch reraise reaches outer") {
    CHECK(compile_and_run(
              "try (try raise 1 catch _ -> raise 2 end) catch _ -> 3 end",
              nullptr, nullptr, "nested_try_reraise") == "3");
  }

  TEST_CASE("Explicit @borrow on foldl fn param eliminates rc_inc") {
    auto ir =
        compile_to_ir("let foldl @borrow fn acc seq = case seq of [] -> acc; "
                      "[h|t] -> foldl fn (fn acc h) t end in "
                      "foldl (\\a b -> a + b) 0 [1,2,3]");
    CHECK(!ir_contains(ir, "call void @YonaRuntimeRetain"));
  }

  // with expression E2E test is in test/Fixtures/Codegen/with_value.yona.

  TEST_CASE("Function generates internal LLVM function") {
    auto ir = compile_to_ir("let f x = x + 1 in f(5)");
    CHECK(ir_contains(ir, "define internal fastcc i64 @f(i64 %x)"));
    CHECK(ir_contains(ir, "add i64 %x, 1"));
  }

  TEST_CASE("Multi-arg function generates correct signature") {
    auto ir = compile_to_ir("let add x y = x + y in add 3 4");
    CHECK(ir_contains(ir, "define internal fastcc i64 @add(i64 %x, i64 %y)"));
  }

  TEST_CASE("unhandled perform lambda apply is TYPE_ERROR") {
    CHECK(
        compile_and_run(
            R"(let plan = \() -> perform Fs.read "/etc/shadow" in plan ())") ==
        "TYPE_ERROR");
  }

  TEST_CASE("HOF apply of unhandled perform lambda is TYPE_ERROR") {
    CHECK(
        compile_and_run(
            R"(let apply = \f x -> f x in apply (\() -> perform Fs.read "/etc/shadow") ())") ==
        "TYPE_ERROR");
  }

} // Codegen IR

// ===== End-to-End Fixture Tests =====
// Loads fixtures from test/Fixtures/Codegen/.

TEST_SUITE("Codegen E2E") {

  TEST_CASE("CMake provides the active-build Prelude object") {
#ifdef YONA_TEST_PRELUDE_OBJECT
    const fs::path prelude_object(YONA_TEST_PRELUDE_OBJECT);
    CHECK(prelude_object.is_absolute());
    CHECK(fs::exists(prelude_object));
    CHECK(prelude_object != yona::test::lib_dir() / "Prelude.o");
#else
    FAIL_CHECK("YONA_TEST_PRELUDE_OBJECT is missing from the generated test "
               "configuration");
#endif
  }

  TEST_CASE("Stdlib expected fixtures all have source files") {
    const auto fixtures = yona::test::repo_root() / "test" / "stdlib";
    size_t expected_count = 0;
    for (const auto &entry : fs::recursive_directory_iterator(fixtures)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".expected")
        continue;
      ++expected_count;
      auto source = entry.path();
      source.replace_extension(".yona");
      REQUIRE_MESSAGE(fs::exists(source), "Missing source for stdlib fixture: ",
                      fs::relative(source, fixtures).generic_string());
    }
    CHECK(expected_count > 0);
  }

  TEST_CASE("Fixture-based codegen tests") {
    fs::path fixtures_dir = yona::test::codegen_fixtures_dir();
    // Set up and tear down scratch files that specific fixtures read from.
    // The fixtures foldl_iterator and iterator_gen_lines assume a
    // /tmp/yona_iter_gen_lines_test.txt file with 3 lines totalling 14 bytes.
    struct ScratchFiles {
      std::vector<fs::path> paths;
      ScratchFiles() {
        auto p =
            yona::test::link::scratch_root() / "yona_iter_gen_lines_test.txt";
        std::ofstream(p) << "abcde\nfghij\nklmn\n";
        paths.push_back(p);
        auto linear_file =
            yona::test::link::scratch_root() / "yona_linear_file_case.txt";
        std::ofstream(linear_file) << "linear resource fixture\n";
        paths.push_back(linear_file);
#if !defined(__linux__)
        auto rel =
            yona::test::link::scratch_root() / "yona_stub_os_release.txt";
        std::ofstream(rel) << "NAME=Stub\nVERSION=1\n";
        paths.push_back(rel);
#endif
      }
      ~ScratchFiles() {
        std::error_code ec;
        for (auto &p : paths)
          fs::remove(p, ec);
      }
    } scratch_files;
    run_yona_fixture_tree(fixtures_dir);
  }

  TEST_CASE("Imported IO wrappers preserve promise await metadata") {
    const string source = R"(
import println from Std\Io in do
    println "first"
    println "second"
    0
end
)";

    parser::Parser parser;
    DiagnosticEngine diagnostics;
    typechecker::TypeChecker type_checker(diagnostics);
    Codegen codegen("imported_io_awaits", &diagnostics);
    codegen.set_opt_level(0);
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    for (const auto &path : codegen.ModulePaths)
      type_checker.add_module_path(path);

    auto parsed = parser.parseExpression(source, "imported_io_awaits.yona");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);
    type_checker.check(parsed->Expression.get());
    REQUIRE(type_checker.solve_constraints());
    REQUIRE_FALSE(type_checker.has_errors());
    codegen.set_type_checker(&type_checker);
    REQUIRE(codegen.compile(parsed->Expression.get()));

    const string ir = codegen.emit_ir();
    size_t awaits = 0;
    for (size_t position = 0;
         (position = ir.find("call i64 @YonaRuntimeIoAwait", position)) !=
         string::npos;
         position += 4)
      ++awaits;
    CHECK(awaits == 1);

    size_t wrapper_calls = 0;
    for (size_t position = 0;
         (position = ir.find("call fastcc i64 @println__genfn", position)) !=
         string::npos;
         position += 4)
      ++wrapper_calls;
    CHECK(wrapper_calls == 2);
  }

  TEST_CASE("Stdlib conformance fixtures") {
    run_yona_fixture_tree(yona::test::repo_root() / "test" / "stdlib");
  }

  TEST_CASE("Stdlib network conformance fixtures") {
    run_yona_fixture_tree(yona::test::repo_root() / "test" / "stdlib" /
                          "network");
  }

  TEST_CASE("Stdlib GPU conformance fixtures") {
    run_yona_fixture_tree(yona::test::repo_root() / "test" / "stdlib" / "gpu");
  }

  TEST_CASE("stdlib manifest has complete suite coverage") {
    const auto root = yona::test::repo_root();
    const auto manifest = root / "test" / "stdlib" / "manifest.md";
    REQUIRE_MESSAGE(fs::exists(manifest),
                    "Missing stdlib conformance manifest");

    set<string> documented_modules;
    istringstream api(read_file(root / "docs" / "api" / "README.md"));
    string line;
    while (getline(api, line)) {
      const auto begin = line.find("[Std.");
      const auto end = line.find("](", begin);
      if (begin != string::npos && end != string::npos)
        documented_modules.insert(line.substr(begin + 1, end - begin - 1));
    }

    set<string> manifest_modules;
    istringstream rows(read_file(manifest));
    while (getline(rows, line)) {
      if (line.empty() || line[0] != '|' || line.find("---") != string::npos)
        continue;
      vector<string> columns;
      string column;
      istringstream row(line);
      while (getline(row, column, '|'))
        columns.push_back(column);
      if (columns.size() < 5 || columns[1].find("Module") != string::npos)
        continue;
      const string module = trim_cell(columns[1]);
      const string tier = trim_cell(columns[2]);
      const string script = trim_cell(columns[3]);
      const string contracts = trim_cell(columns[4]);
      REQUIRE((tier == "pure" || tier == "runtime" || tier == "network" ||
               tier == "gpu"));
      REQUIRE_FALSE(contracts.empty());
      REQUIRE(fs::exists(root / "test" / "stdlib" / (script + ".yona")));
      REQUIRE(fs::exists(root / "test" / "stdlib" / (script + ".expected")));
      manifest_modules.insert(module);
    }
    CHECK(manifest_modules == documented_modules);
  }

} // Codegen E2E

TEST_CASE("print tuple containing seq") {
  CHECK(compile_and_run("(42, [1, 2, 3])", nullptr, nullptr,
                        "tc_print_tuple_int_seq") == "(42, [1, 2, 3])");
}

TEST_CASE("Immediately applied expression lambdas use the closure call path") {
  CHECK(compile_and_run(R"((\_ -> 42) ())", nullptr, nullptr,
                        "tc_immediate_lambda_unit") == "42");
  CHECK(compile_and_run(R"((\value -> value + 1) 41)", nullptr, nullptr,
                        "tc_immediate_lambda_value") == "42");
  CHECK(compile_and_run(R"(let offset = 2 in (\value -> value + offset) 40)",
                        nullptr, nullptr,
                        "tc_immediate_lambda_capture") == "42");
  CHECK(
      compile_and_run(
          R"(let increment value = value + 1, decrement value = value - 1 in (if true then increment else decrement) 41)",
          nullptr, nullptr, "tc_immediate_expression_callee") == "42");
}

TEST_CASE("print nested seq") {
  CHECK(compile_and_run("[[1, 2], [3]]", nullptr, nullptr,
                        "tc_print_nested_seq") == "[[1, 2], [3]]");
}

TEST_CASE("ADT patterns preserve nested tuple field and function types") {
  REQUIRE(yona::test::link::ensure_runtime_objects());
  auto module_root =
      yona::test::link::scratch_root() / "yona_adt_pattern_types";
  fs::create_directories(module_root / "Test");

  parser::Parser module_parser;
  auto module_result = module_parser.parseModule(R"YT(
module Test\AdtPatternTypes

export runThunk, renderBox

type Thunk = Thunk (() -> Int)
type Box = Box (Int, Int, Seq String)

runThunk : Unit -> Int
runThunk _ =
    case Thunk (\() -> 42) of
        Thunk deferred -> deferred ()
    end

renderBox : Unit -> String
renderBox _ =
    import intToString from Std\Types in
    case Box (42, 0, ["ignored"]) of
        Box ((value, _, _)) -> intToString value
    end
)YT",
                                                 "adt_pattern_types.yona");
  REQUIRE(module_result.has_value());

  DiagnosticEngine module_diag;
  Codegen module_codegen("adt_pattern_types_module", &module_diag);
  module_codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  REQUIRE(module_codegen.compile_module(module_result.value().get()) !=
          nullptr);
  CHECK_FALSE(module_diag.has_errors());
  auto module_object =
      yona::test::link::scratch_root() / "adt_pattern_types_module.o";
  REQUIRE(module_codegen.emit_object_file(module_object.string()));
  REQUIRE(module_codegen.emit_interface_file(
      (module_root / "Test" / "AdtPatternTypes.yonai").string()));

  parser::Parser expression_parser;
  std::istringstream expression_stream(
      "import runThunk, renderBox from Test\\AdtPatternTypes in "
      "if runThunk () == 42 then renderBox () else \"wrong\"");
  auto expression_result =
      expression_parser.parseExpression(expression_stream.str(), "<stream>");
  REQUIRE(expression_result);
  REQUIRE(expression_result->Expression != nullptr);

  DiagnosticEngine expression_diag;
  Codegen expression_codegen("adt_pattern_types_expression", &expression_diag);
  expression_codegen.ModulePaths.push_back(module_root.string());
  expression_codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  REQUIRE(expression_codegen.compile(expression_result->Expression.get()) !=
          nullptr);
  CHECK_FALSE(expression_diag.has_errors());
  auto expression_object =
      yona::test::link::scratch_root() / "adt_pattern_types_expression.o";
  REQUIRE(expression_codegen.emit_object_file(expression_object.string()));

  std::vector<fs::path> objects = {module_object, expression_object};
  REQUIRE(yona::test::link::append_runtime_objects(objects));
  auto executable = yona::test::link::scratch_root() /
                    ("adt_pattern_types" + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));
  CHECK(yona::test::link::executeAndCapture(executable) == "42");
}

TEST_CASE("Imported GENFN source preserves private record constructor fields") {
  REQUIRE(yona::test::link::ensure_runtime_objects());
  auto module_root =
      yona::test::link::scratch_root() / "yona_private_record_genfn";
  fs::create_directories(module_root / "Test");

  parser::Parser module_parser;
  auto module_result = module_parser.parseModule(R"YT(
module Test\PrivateRecordGenfn

export run

type Holder = Holder { thunk: (() -> Int) }

run : Unit -> Int
run _ =
    case Holder { thunk = \() -> 7 } of
        Holder { thunk = fn } -> fn ()
    end
)YT",
                                                 "private_record_genfn.yona");
  REQUIRE(module_result.has_value());

  Codegen module_codegen("private_record_genfn_module");
  module_codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  REQUIRE(module_codegen.compile_module(module_result.value().get()) !=
          nullptr);
  auto module_object =
      yona::test::link::scratch_root() / "private_record_genfn_module.o";
  REQUIRE(module_codegen.emit_object_file(module_object.string()));
  REQUIRE(module_codegen.emit_interface_file(
      (module_root / "Test" / "PrivateRecordGenfn.yonai").string()));

  parser::Parser expression_parser;
  std::istringstream expression_stream(
      "import run from Test\\PrivateRecordGenfn in run ()");
  auto expression_result =
      expression_parser.parseExpression(expression_stream.str(), "<stream>");
  REQUIRE(expression_result);
  REQUIRE(expression_result->Expression != nullptr);

  DiagnosticEngine expression_diag;
  Codegen expression_codegen("private_record_genfn_expression",
                             &expression_diag);
  expression_codegen.ModulePaths.push_back(module_root.string());
  expression_codegen.ModulePaths.push_back(yona::test::lib_dir().string());
  REQUIRE(expression_codegen.compile(expression_result->Expression.get()) !=
          nullptr);
  CHECK_FALSE(expression_diag.has_errors());
  auto expression_object =
      yona::test::link::scratch_root() / "private_record_genfn_expression.o";
  REQUIRE(expression_codegen.emit_object_file(expression_object.string()));

  std::vector<fs::path> objects = {module_object, expression_object};
  REQUIRE(yona::test::link::append_runtime_objects(objects));
  auto executable = yona::test::link::scratch_root() /
                    ("private_record_genfn" + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));
  CHECK(yona::test::link::executeAndCapture(executable) == "7");
}

TEST_CASE("print set of seq") {
  CHECK(compile_and_run("{[1, 2]}", nullptr, nullptr, "tc_print_set_of_seq") ==
        "{[1, 2]}");
}

TEST_CASE("Set difference transfers its consumed left operand") {
  auto ir = compile_to_ir(
      "let difference left right = left -- right in difference {1, 2} {2}");
  CHECK(ir_contains(
      ir, "call ptr @YonaRuntimeSetDifference(ptr %left, ptr %right)"));
  CHECK(ir_contains(ir, "call void @YonaRuntimeFrameTransfer(ptr %left)"));
  CHECK_FALSE(ir_contains(ir, "call void @YonaRuntimeRelease(ptr %set2)"));
  CHECK(ir_contains(ir, "call void @YonaRuntimeRelease(ptr %set4)"));

  CHECK(
      compile_and_run(
          "let difference left right = left -- right in difference {1, 2} {2}",
          nullptr, nullptr, "set_difference_transfer") == "{1}");
}

TEST_CASE("print dict of seq") {
  CHECK(compile_and_run("{1: [10, 20]}", nullptr, nullptr,
                        "tc_print_dict_of_seq") == "{1: [10, 20]}");
}

TEST_CASE("print seq of sets") {
  CHECK(compile_and_run("[{1}, {2}]", nullptr, nullptr,
                        "tc_print_seq_of_sets") == "[{1}, {2}]");
}

TEST_CASE("print set of seqs dumps collections not addresses") {
  string out =
      compile_and_run("{[1], [2]}", nullptr, nullptr, "tc_print_set_of_seqs");
  CHECK_MESSAGE(out.find("[1]") != string::npos,
                (string("expected [1] in '") + out + "'"));
  CHECK_MESSAGE(out.find("[2]") != string::npos,
                (string("expected [2] in '") + out + "'"));
  CHECK_MESSAGE(out.find(": ") == string::npos,
                (string("set must not print as dict '") + out + "'"));
}

TEST_CASE("dropping a set of seqs releases inner heap objects") {
  ensure_runtime_archive();
  string source = "let s = {[1], [2]} in 0";

  parser::Parser parser;
  Codegen codegen("hamt_set_seq_drop_test");
  DiagnosticEngine tc_diag;
  typechecker::TypeChecker type_checker(tc_diag);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
  istringstream stream(source);
  auto pr = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(pr);
  REQUIRE(pr->Expression);
  type_checker.check(pr->Expression.get());
  REQUIRE(!type_checker.has_direct_errors());
  auto module = codegen.compile(pr->Expression.get());
  REQUIRE(module);

  fs::path obj_path =
      yona::test::link::scratch_root() / "yona_hamt_set_seq_drop.o";
  REQUIRE(codegen.emit_object_file(obj_path.string()));
  assert_linked_yona_zero_alloc_leaks(obj_path.string(),
                                      "yona_hamt_set_seq_drop");
}

// ===== Perceus exception cleanup (phase 3) =====
//
// Verifies that when `raise` unwinds past frames that own heap values
// (seq/set/dict/etc.), those values are rc_dec'd rather than leaked.
// The fixture perceus_raise_no_leak.yona raises mid-recursion inside
// a try/catch; without phase-3 cleanup, each intermediate `acc` binding
// leaks one SEQ on the raise path.
//
// We compile the fixture, link it the same way the E2E suite does, run
// it with YONA_ALLOC_STATS=1, and parse stderr for `leaked=N` on each
// type tag. All must be zero.

TEST_SUITE("PerceusExceptionCleanup") {

  TEST_CASE("raise through heap-owning frames does not leak") {
    ensure_runtime_archive();
    // Reuse the E2E fixture by running the full compile-link pipeline
    // with YONA_ALLOC_STATS=1 and capturing stderr.
    fs::path fixtures_dir = yona::test::codegen_fixtures_dir();
    REQUIRE(fs::is_directory(fixtures_dir));
    string source = read_file(fixtures_dir / "perceus_raise_no_leak.yona");
    REQUIRE(!source.empty());

    // Compile + link (duplicates compile_and_run's skeleton to add env)
    parser::Parser parser;
    Codegen codegen("perceus_raise_test");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.ModulePaths.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    istringstream stream(source);
    auto pr = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(pr);
    REQUIRE(pr->Expression);
    type_checker.check(pr->Expression.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr->Expression.get());
    REQUIRE(module);

    fs::path obj_path =
        yona::test::link::scratch_root() / "yona_perceus_raise.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));

    assert_linked_yona_zero_alloc_leaks(obj_path.string(),
                                        "yona_perceus_raise");
  }

  TEST_CASE("borrowed temporary heap arguments are released by caller") {
    ensure_runtime_archive();
    string source = "let inspect xs = 1 in inspect [1,2,3]";

    parser::Parser parser;
    Codegen codegen("borrowed_temp_cleanup_test");
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    istringstream stream(source);
    auto pr = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(pr);
    REQUIRE(pr->Expression);
    type_checker.check(pr->Expression.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr->Expression.get());
    REQUIRE(module);

    fs::path obj_path =
        yona::test::link::scratch_root() / "yona_borrowed_temp_cleanup.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));
    assert_linked_yona_zero_alloc_leaks(obj_path.string(),
                                        "yona_borrowed_temp_cleanup");
  }

  TEST_CASE("generated entry point releases its printed heap result") {
    ensure_runtime_archive();

    parser::Parser parser;
    Codegen codegen("root_heap_result_cleanup_test");
    DiagnosticEngine diagnostics;
    typechecker::TypeChecker type_checker(diagnostics);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    auto parsed = parser.parseExpression("[1]", "root_heap_result.yona");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);
    type_checker.check(parsed->Expression.get());
    REQUIRE(type_checker.solve_constraints());
    REQUIRE_FALSE(type_checker.has_errors());
    codegen.set_type_checker(&type_checker);
    REQUIRE(codegen.compile(parsed->Expression.get()));

    const auto object_path =
        yona::test::link::scratch_root() / "yona_root_heap_result.o";
    REQUIRE(codegen.emit_object_file(object_path.string()));
    assert_linked_yona_zero_alloc_leaks(object_path.string(),
                                        "yona_root_heap_result");
  }

  TEST_CASE("generated binary IO releases submitted and returned ByteArrays") {
    ensure_runtime_archive();
    string source = read_file(yona::test::codegen_fixtures_dir() /
                              "binary_write_read.yona");
    REQUIRE_FALSE(source.empty());
    yona::test::link::rewrite_codegen_fixture_tmp_paths(source);

    parser::Parser parser;
    Codegen codegen("binary_byte_array_cleanup_test");
    codegen.ModulePaths.push_back(yona::test::lib_dir().string());
    DiagnosticEngine diagnostics;
    typechecker::TypeChecker type_checker(diagnostics);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    type_checker.add_module_path(yona::test::lib_dir().string());
    auto parsed =
        parser.parseExpression(source, "binary_write_read_cleanup.yona");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);
    type_checker.check(parsed->Expression.get());
    REQUIRE(type_checker.solve_constraints());
    REQUIRE_FALSE(type_checker.has_errors());
    codegen.set_type_checker(&type_checker);
    REQUIRE(codegen.compile(parsed->Expression.get()));

    const auto object_path =
        yona::test::link::scratch_root() / "yona_binary_byte_array_cleanup.o";
    REQUIRE(codegen.emit_object_file(object_path.string()));
    assert_linked_yona_zero_alloc_leaks(object_path.string(),
                                        "yona_binary_byte_array_cleanup");
  }

  TEST_CASE("raise through grouped-let task group frees bump arena") {
    ensure_runtime_archive();
    fs::path fixtures_dir = yona::test::codegen_fixtures_dir();
    REQUIRE(fs::is_directory(fixtures_dir));
    string source = read_file(fixtures_dir / "task_group_raise_arena.yona");
    REQUIRE(!source.empty());

    parser::Parser parser;
    Codegen codegen("task_group_raise_arena_test");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.ModulePaths.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    istringstream stream(source);
    auto pr = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(pr);
    REQUIRE(pr->Expression);
    type_checker.check(pr->Expression.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr->Expression.get());
    REQUIRE(module);

    fs::path obj_path =
        yona::test::link::scratch_root() / "yona_task_group_raise_arena.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));

    assert_linked_yona_zero_alloc_leaks(obj_path.string(),
                                        "yona_task_group_raise_arena");
  }

  TEST_CASE("grouped let task group happy path (no raise)") {
    ensure_runtime_archive();
    string source = "let a = [1, 2], b = [3, 4] in case a of [x|_] -> case b "
                    "of [y|_] -> x + y; "
                    "_ -> 0 end; _ -> 0 end";
    string actual = compile_and_run(source);
    CHECK(actual == "4");
    // Same program through link + alloc stats (arena + group teardown on
    // success path)
    parser::Parser parser;
    Codegen codegen("task_group_happy_test");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.ModulePaths.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    istringstream stream(source);
    auto pr = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(pr);
    REQUIRE(pr->Expression);
    type_checker.check(pr->Expression.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto happy_mod = codegen.compile(pr->Expression.get());
    REQUIRE(happy_mod);
    fs::path obj_path =
        yona::test::link::scratch_root() / "yona_task_group_happy.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));
    assert_linked_yona_zero_alloc_leaks(obj_path.string(),
                                        "yona_task_group_happy");
  }

} // PerceusExceptionCleanup

// ===== Module Compilation Tests =====

TEST_SUITE("Codegen Modules") {

  TEST_CASE("Module compiles to object with mangled exports") {
    // Parse module
    parser::Parser parser;
    string source = R"(
module Test\Arith

export add, mul

add x y = x + y
mul x y = x * y
)";
    auto result = parser.parseModule(source, "test_arith.yona");
    REQUIRE(result.has_value());

    // Compile module
    Codegen codegen("test_arith");
    auto mod = codegen.compile_module(result.value().get());
    REQUIRE(mod != nullptr);

    // Check that mangled exports exist in the IR
    string ir = codegen.emit_ir();
    CHECK(ir.find("YonaTestArithAdd") != string::npos);
    CHECK(ir.find("YonaTestArithMul") != string::npos);
  }

  TEST_CASE("Module name mangling") {
    CHECK(Codegen::mangle_name("Test\\Arith", "add") == "YonaTestArithAdd");
    CHECK(Codegen::mangle_name("Std\\Math", "abs") == "YonaStdMathAbs");
    CHECK(Codegen::mangle_name("My\\Deep\\Module", "func") ==
          "YonaMyDeepModuleFunc");
  }

  TEST_CASE("Module cross-language linking") {
    // Compile a module
    parser::Parser parser;
    string source = R"(
module Test\CrossLang

export double, negate

double x = x * 2
negate x = 0 - x
)";
    auto result = parser.parseModule(source, "cross.yona");
    REQUIRE(result.has_value());

    Codegen codegen("cross_lang");
    auto mod = codegen.compile_module(result.value().get());
    REQUIRE(mod != nullptr);

    fs::path obj_path = yona::test::link::scratch_root() / "cross_lang_test.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));

    fs::path c_src = yona::test::link::scratch_root() / "cross_lang_caller.c";
    {
      ofstream f(c_src);
      f << "#include <stdio.h>\n#include <stdint.h>\n"
        << "extern int64_t YonaTestCrossLangDouble(int64_t);\n"
        << "extern int64_t YonaTestCrossLangNegate(int64_t);\n"
        << "int main() { printf(\"%ld %ld\", "
        << "(long)YonaTestCrossLangDouble(21), "
        << "(long)YonaTestCrossLangNegate(5)); return 0; }\n";
    }

    fs::path exe_out = yona::test::link::scratch_root() /
                       ("cross_lang_run" + yona::test::link::exe_suffix());
    vector<string> link_args = {c_src.string(), obj_path.string(), "-o",
                                exe_out.string()};
#ifdef _WIN32
    link_args.insert(link_args.end(), {"-Xlinker", "/SUBSYSTEM:CONSOLE"});
#else
    link_args.insert(link_args.end(), {"-lm", "-lpthread"});
#endif
    const auto link_result = yona::support::executeProcess(
        yona::test::link::cc(), link_args, {.SuppressStderr = true});
    REQUIRE_FALSE(link_result.ExecutionFailed);
    REQUIRE(link_result.ExitCode == 0);

    const string output = yona::test::link::executeAndCapture(exe_out);

    CHECK(output == "42 -5");
  }

  TEST_CASE("Multi-module Yona linking") {
    // Compile a module
    parser::Parser p1;
    string mod_source = R"(
module Test\Calc

export square, cube

square x = x * x
cube x = x * x * x
)";
    auto mod_result = p1.parseModule(mod_source, "calc.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("calc_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "calc_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));

    // Compile expression that imports from the module
    parser::Parser p2;
    string expr_source =
        "import square, cube from Test\\Calc in square 3 + cube 2";
    istringstream stream(expr_source);
    auto expr_result = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(expr_result);
    REQUIRE(expr_result->Expression != nullptr);

    Codegen expr_codegen("expr_test");
    auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "calc_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    vector<fs::path> calc_objs = {expr_obj, mod_obj};
    REQUIRE(yona::test::link::append_runtime_objects(calc_objs));
    fs::path exe_out = yona::test::link::scratch_root() /
                       ("calc_link_test" + yona::test::link::exe_suffix());
    REQUIRE(yona::test::link::link_objs_to_exe(calc_objs, exe_out));

    const string output = yona::test::link::executeAndCapture(exe_out);

    CHECK(output == "17"); // square(3)=9 + cube(2)=8 = 17
  }

  TEST_CASE("Re-exports") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_reexport";

    // Step 1: Compile source module Test\Arith
    parser::Parser p1;
    string arith_source = R"(
module Test\Arith

export add, mul

add x y = x + y
mul x y = x * y
)";
    auto arith_result = p1.parseModule(arith_source, "arith.yona");
    REQUIRE(arith_result.has_value());

    Codegen arith_codegen("arith_mod");
    auto arith_mod = arith_codegen.compile_module(arith_result.value().get());
    REQUIRE(arith_mod != nullptr);
    fs::path arith_obj = yona::test::link::scratch_root() / "arith_mod_test.o";
    REQUIRE(arith_codegen.emit_object_file(arith_obj.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(arith_codegen.emit_interface_file(
        (yona_lib / "Test" / "Arith.yonai").string()));

    // Step 2: Compile re-exporting module Test\Prelude
    parser::Parser p2;
    string prelude_source = R"(
module Test\Prelude

export add, mul from Test\Arith
export double

double x = add x x
)";
    auto prelude_result = p2.parseModule(prelude_source, "prelude.yona");
    REQUIRE(prelude_result.has_value());

    Codegen prelude_codegen("prelude_mod");
    prelude_codegen.ModulePaths.push_back(yona_lib.string());
    auto prelude_mod =
        prelude_codegen.compile_module(prelude_result.value().get());
    REQUIRE(prelude_mod != nullptr);
    fs::path prelude_obj =
        yona::test::link::scratch_root() / "prelude_mod_test.o";
    REQUIRE(prelude_codegen.emit_object_file(prelude_obj.string()));
    REQUIRE(prelude_codegen.emit_interface_file(
        (yona_lib / "Test" / "Prelude.yonai").string()));

    // Step 3: Compile expression that imports from the re-exporting module
    parser::Parser p3;
    string expr_source = "import Test\\Prelude in add 10 (mul 3 4)";
    istringstream stream(expr_source);
    auto expr_result = p3.parseExpression(stream.str(), "<stream>");
    REQUIRE(expr_result);
    REQUIRE(expr_result->Expression != nullptr);

    Codegen expr_codegen("reexport_test");
    expr_codegen.ModulePaths.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj =
        yona::test::link::scratch_root() / "reexport_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    // Step 4: Link all three and run
    fs::path exe_path = yona::test::link::scratch_root() /
                        ("reexport_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> re_objs = {arith_obj, prelude_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(re_objs));
    REQUIRE(yona::test::link::link_objs_to_exe(re_objs, exe_path));

    string result = yona::test::link::executeAndCapture(exe_path);

    CHECK(result == "22"); // add(10, mul(3,4)) = 10 + 12 = 22
  }

  TEST_CASE("Type-annotated module functions") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_typed";

    parser::Parser p1;
    string mod_source = R"(
module Test\Typed

export scale, greet

scale : Float -> Float -> Float
scale factor x = factor * x
greet : String -> String
greet name = "Hello " ++ name
)";
    auto mod_result = p1.parseModule(mod_source, "typed.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("typed_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "typed_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(mod_codegen.emit_interface_file(
        (yona_lib / "Test" / "Typed.yonai").string()));

    // Test: scale 2.5 4.0 = 10.0
    parser::Parser p2;
    string expr_source = "import scale from Test\\Typed in scale 2.5 4.0";
    istringstream stream(expr_source);
    auto expr_result = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(expr_result);
    REQUIRE(expr_result->Expression != nullptr);

    Codegen expr_codegen("typed_test");
    expr_codegen.ModulePaths.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "typed_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    fs::path exe_path = yona::test::link::scratch_root() /
                        ("typed_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> typed_objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(typed_objs));
    REQUIRE(yona::test::link::link_objs_to_exe(typed_objs, exe_path));

    string result = yona::test::link::executeAndCapture(exe_path);

    CHECK(result == "10"); // %g format: 10.0 prints as "10"
  }

  TEST_CASE("Type signatures accept parameterized collection types") {
    parser::Parser parser;
    auto module_result = parser.parseModule(R"(
module Test\TypedSeq

export isEmpty

isEmpty : Seq Int -> Bool
isEmpty values = case values of
    [] -> true
    [_ | _] -> false
end
)",
                                            "typed_seq.yona");
    REQUIRE(module_result.has_value());

    DiagnosticEngine diag;
    Codegen codegen("typed_seq", &diag);
    auto module = codegen.compile_module(module_result.value().get());
    REQUIRE(module != nullptr);
    CHECK_FALSE(diag.has_errors());

    auto interface_path = yona::test::link::scratch_root() / "TypedSeq.yonai";
    REQUIRE(codegen.emit_interface_file(interface_path.string()));
    const auto interface_text = read_file(interface_path);
    CHECK(interface_text.find("FN isEmpty 1 Seq(INT) -> BOOL") !=
          std::string::npos);
  }

  TEST_CASE("Interface signatures preserve every unparenthesized Dict type "
            "argument") {
    parser::Parser parser;
    auto module_result = parser.parseModule(R"(
module Test\TypedDict

export keep

keep : Dict key value -> Dict key value
keep dictionary = dictionary
)",
                                            "typed_dict.yona");
    REQUIRE(module_result.has_value());

    DiagnosticEngine diag;
    Codegen codegen("typed_dict", &diag);
    auto module = codegen.compile_module(module_result.value().get());
    REQUIRE(module != nullptr);
    CHECK_FALSE(diag.has_errors());

    auto interface_path = yona::test::link::scratch_root() / "TypedDict.yonai";
    REQUIRE(codegen.emit_interface_file(interface_path.string()));
    const auto interface_text = read_file(interface_path);
    CHECK(interface_text.find("FN keep 1 Dict(VAR(key),VAR(value)) -> "
                              "Dict(VAR(key),VAR(value))") !=
          std::string::npos);
  }

  TEST_CASE(
      "Std String join reads sequence elements after the runtime header") {
    CHECK(
        compile_and_run(
            R"(import join from Std\String in join "," ["alpha", "beta", "gamma"])",
            nullptr, nullptr,
            "std_string_join_seq_header") == "alpha,beta,gamma");
  }

  TEST_CASE("Std Format reads string arguments after the sequence header") {
    CHECK(compile_and_run(
              R"(import format from Std\Format in format "{}" ["ok"])", nullptr,
              nullptr, "std_format_string_seq_header") == "ok");
  }

  TEST_CASE("Std Format renders converted integer arguments") {
    CHECK(compile_and_run(
              R"(import format from Std\Format, intToString from Std\Types in
format "answer: {}" [intToString 42])",
              nullptr, nullptr, "std_format_integer") == "answer: 42");
  }

  TEST_CASE("Std Format replaces repeated placeholders in order") {
    CHECK(
        compile_and_run(
            R"(import format from Std\Format in format "{} + {} = {}" ["one", "two", "three"])",
            nullptr, nullptr,
            "std_format_repeated_placeholders") == "one + two = three");
  }

  TEST_CASE("Std Test renders an empty report") {
    CHECK(compile_and_run(
              R"(import run, render from Std\Test in render (run []))", nullptr,
              nullptr,
              "std_test_empty_report") == "SUMMARY 0 passed, 0 failed");
  }

  TEST_CASE("Std Test executes every case and renders its report") {
    CHECK(compile_and_run(R"(
import testCase, check, run, render from Std\Test in
let cases = [
    testCase "first" (\_ -> check "first passed" true),
    testCase "second" (\_ -> check "second passed" true)
]
in render (run cases)
)",
                          nullptr, nullptr, "std_test_framework") ==
          "PASS first\nPASS second\nSUMMARY 2 passed, 0 failed");
  }

  TEST_CASE("Imported annotated functions receive every application argument") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_annotated_arity";

    parser::Parser module_parser;
    auto module_result = module_parser.parseModule(R"(
module Test\AnnotatedArity

export select

select : String -> Bool -> String
select message condition = if condition then message else "no"
)",
                                                   "annotated_arity.yona");
    REQUIRE(module_result.has_value());

    Codegen module_codegen("annotated_arity_module");
    REQUIRE(module_codegen.compile_module(module_result.value().get()) !=
            nullptr);
    fs::path module_object =
        yona::test::link::scratch_root() / "annotated_arity_module.o";
    REQUIRE(module_codegen.emit_object_file(module_object.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(module_codegen.emit_interface_file(
        (yona_lib / "Test" / "AnnotatedArity.yonai").string()));

    parser::Parser expression_parser;
    std::istringstream expression_source(
        "import select from Test\\AnnotatedArity in "
        "let invoke = \\_ -> select \"yes\" true in invoke ()");
    auto expression_result =
        expression_parser.parseExpression(expression_source.str(), "<stream>");
    REQUIRE(expression_result);
    REQUIRE(expression_result->Expression != nullptr);

    Codegen expression_codegen("annotated_arity_expression");
    expression_codegen.ModulePaths.push_back(yona_lib.string());
    REQUIRE(expression_codegen.compile(expression_result->Expression.get()) !=
            nullptr);
    fs::path expression_object =
        yona::test::link::scratch_root() / "annotated_arity_expression.o";
    REQUIRE(expression_codegen.emit_object_file(expression_object.string()));

    fs::path executable = yona::test::link::scratch_root() /
                          ("annotated_arity" + yona::test::link::exe_suffix());
    std::vector<fs::path> objects = {module_object, expression_object};
    REQUIRE(yona::test::link::append_runtime_objects(objects));
    REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));
    CHECK(yona::test::link::executeAndCapture(executable) == "yes");
  }

  TEST_CASE("Annotated ADT case functions heap-box non-recursive results") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_adt_case_result";

    parser::Parser module_parser;
    auto module_result = module_parser.parseModule(R"(
module Test\AdtCaseResult

export run, value
export type Report

type Report = Report { number: Int }

run : Seq Int -> Report
run values = case values of
    [] -> Report { number = 0 }
    [_ | rest] -> run rest
end

value : Report -> Int
value report = case report of
    Report { number = number } -> number
end
)",
                                                   "adt_case_result.yona");
    REQUIRE(module_result.has_value());

    Codegen module_codegen("adt_case_result_module");
    REQUIRE(module_codegen.compile_module(module_result.value().get()) !=
            nullptr);
    fs::path module_object =
        yona::test::link::scratch_root() / "adt_case_result_module.o";
    REQUIRE(module_codegen.emit_object_file(module_object.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(module_codegen.emit_interface_file(
        (yona_lib / "Test" / "AdtCaseResult.yonai").string()));

    parser::Parser expression_parser;
    std::istringstream expression_source(
        "import run, value from Test\\AdtCaseResult in value (run [])");
    auto expression_result =
        expression_parser.parseExpression(expression_source.str(), "<stream>");
    REQUIRE(expression_result);
    REQUIRE(expression_result->Expression != nullptr);

    Codegen expression_codegen("adt_case_result_expression");
    expression_codegen.ModulePaths.push_back(yona_lib.string());
    REQUIRE(expression_codegen.compile(expression_result->Expression.get()) !=
            nullptr);
    fs::path expression_object =
        yona::test::link::scratch_root() / "adt_case_result_expression.o";
    REQUIRE(expression_codegen.emit_object_file(expression_object.string()));

    fs::path executable = yona::test::link::scratch_root() /
                          ("adt_case_result" + yona::test::link::exe_suffix());
    std::vector<fs::path> objects = {module_object, expression_object};
    REQUIRE(yona::test::link::append_runtime_objects(objects));
    REQUIRE(yona::test::link::link_objs_to_exe(objects, executable));
    CHECK(yona::test::link::executeAndCapture(executable) == "0");
  }

  TEST_CASE("Interface files preserve inferred borrow metadata") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_borrow_meta";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\BorrowMeta

export ignoreSeq, returnSeq

ignoreSeq : Seq -> Int
ignoreSeq xs = 1

returnSeq : Seq -> Seq
returnSeq xs = xs
)";
    auto mod_result = p1.parseModule(mod_source, "borrow_meta.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("borrow_meta_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj =
        yona::test::link::scratch_root() / "borrow_meta_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::path iface = yona_lib / "Test" / "BorrowMeta.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN ignoreSeq 1 Seq(VAR(element)) "
                     "-> INT borrow 1") != string::npos);
    CHECK(yonai.find("FN returnSeq 1 Seq(VAR(element)) "
                     "-> Seq(VAR(element)) borrow") == string::npos);

    {
      parser::Parser p2;
      string borrowed_source = "import ignoreSeq from Test\\BorrowMeta in let "
                               "xs = [1, 2, 3] in ignoreSeq xs";
      istringstream stream(borrowed_source);
      auto expr_result = p2.parseExpression(stream.str(), "<stream>");
      REQUIRE(expr_result);
      REQUIRE(expr_result->Expression != nullptr);

      Codegen borrowed_codegen("borrow_meta_borrowed_expr");
      borrowed_codegen.ModulePaths.push_back(yona_lib.string());
      auto expr_mod = borrowed_codegen.compile(expr_result->Expression.get());
      REQUIRE(expr_mod != nullptr);
      string ir = borrowed_codegen.emit_ir();
      CHECK(ir.find("call void @YonaRuntimeRetain") == string::npos);
    }

    {
      parser::Parser p2;
      string owned_source =
          "import ignoreSeq, returnSeq from Test\\BorrowMeta in "
          "let xs = [1, 2, 3] in let _ = returnSeq xs in ignoreSeq xs";
      istringstream stream(owned_source);
      auto expr_result = p2.parseExpression(stream.str(), "<stream>");
      REQUIRE(expr_result);
      REQUIRE(expr_result->Expression != nullptr);

      Codegen owned_codegen("borrow_meta_owned_expr");
      owned_codegen.ModulePaths.push_back(yona_lib.string());
      auto expr_mod = owned_codegen.compile(expr_result->Expression.get());
      REQUIRE(expr_mod != nullptr);
      string ir = owned_codegen.emit_ir();
      CHECK(ir.find("call void @YonaRuntimeRetain") != string::npos);
    }

    parser::Parser p2;
    string expr_source =
        "import ignoreSeq from Test\\BorrowMeta in ignoreSeq [1, 2, 3]";
    istringstream stream(expr_source);
    auto expr_result = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(expr_result);
    REQUIRE(expr_result->Expression != nullptr);

    Codegen expr_codegen("borrow_meta_expr");
    expr_codegen.ModulePaths.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj =
        yona::test::link::scratch_root() / "borrow_meta_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    fs::path exe_path =
        yona::test::link::scratch_root() /
        ("borrow_meta_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(objs));
    REQUIRE(yona::test::link::link_objs_to_exe(objs, exe_path));

    CHECK(yona::test::link::executeAndCapture(exe_path) == "1");
  }

  TEST_CASE("Set difference owns its left parameter and borrows its right "
            "parameter") {
    namespace fs = std::filesystem;
    auto module_root =
        yona::test::link::scratch_root() / "yona_set_difference_borrow";
    fs::create_directories(module_root / "Test");

    parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\SetDifferenceBorrow

export difference

difference : Set Int -> Set Int -> Set Int
difference left right = left -- right
)",
                                     "set_difference_borrow.yona");
    REQUIRE(result.has_value());

    Codegen codegen("set_difference_borrow_module");
    REQUIRE(codegen.compile_module(result.value().get()) != nullptr);
    auto interface_path = module_root / "Test" / "SetDifferenceBorrow.yonai";
    REQUIRE(codegen.emit_interface_file(interface_path.string()));

    const auto interface_text = read_file(interface_path);
    CHECK(interface_text.find("FN difference 2 "
                              "Set(INT) Set(INT) -> Set(INT) borrow 01") !=
          string::npos);
  }

  TEST_CASE("Import wrappers preserve owned parameter escape metadata") {
    namespace fs = std::filesystem;
    parser::Parser parser;
    auto module_result = parser.parseModule(R"(
module Test\ImportBorrowMeta

export returnSeq

returnSeq : Seq Int -> Seq Int
returnSeq values = import identity from Prelude in values
)",
                                            "import_borrow_meta.yona");
    REQUIRE(module_result.has_value());

    Codegen codegen("import_borrow_meta");
    REQUIRE(codegen.compile_module(module_result.value().get()) != nullptr);
    fs::path iface =
        yona::test::link::scratch_root() / "ImportBorrowMeta.yonai";
    REQUIRE(codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN returnSeq 1 SEQ -> SEQ borrow") == string::npos);
  }

  TEST_CASE("Interface NAT rows serialize FloatArray as FLOAT_ARRAY") {
    namespace fs = std::filesystem;
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_nat_float_arr";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p;
    string mod_source = R"(
module Test\NatFloatArr

export nop

extern native nop : FloatArray -> Int = "YonaTestNatFloatArrNop"
)";
    auto mod_result = p.parseModule(mod_source, "nat_float_arr.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("nat_float_arr_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "NatFloatArr.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("NAT nop 1 FLOAT_ARRAY -> INT") != string::npos);
    CHECK(yonai.find("NAT nop 1 INT -> INT") == string::npos);
  }

  TEST_CASE("Interface extern Seq annotations serialize as SEQ") {
    namespace fs = std::filesystem;
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_extern_seq";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p;
    string mod_source = R"(
module Test\ExternSeq

export values

extern values : String -> Seq = "YonaTestExternSeqValues"
)";
    auto mod_result = p.parseModule(mod_source, "extern_seq.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("extern_seq_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "ExternSeq.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN values 1 STRING -> Seq(VAR(element))") !=
          string::npos);
  }

  TEST_CASE("Interface externs preserve parameterized ADT result descriptors") {
    namespace fs = std::filesystem;
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_extern_result";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p;
    string mod_source = R"(
module Test\ExternResult

export read

extern read : Int -> Result (String, String) = "YonaTestExternResultRead"
)";
    auto mod_result = p.parseModule(mod_source, "extern_result.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("extern_result_mod");
    mod_codegen.ModulePaths = {yona::test::lib_dir().string()};
    YONA_TEST_INSTALL_PARSER_PRELUDE(mod_codegen, p);
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "ExternResult.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN read 1 INT -> "
                     "ADT(Result,STRING,STRING)") != string::npos);
  }

  TEST_CASE("Parameterized ADT case fields retain their floating-point ABI") {
    CHECK(compile_and_run(R"(
let actual = Ok 1.5, expected = Ok 1.5 in
if actual == expected then 1 else 0
)",
                          nullptr, nullptr, "tc_result_float_eq") == "1");
  }

  TEST_CASE("Interface files preserve exported FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_fx_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Fx

export fetch

fetch path = perform Fs.read path
)";
    auto mod_result = p1.parseModule(mod_source, "fx.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("fx_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    DiagnosticEngine module_diag;
    typechecker::TypeChecker module_checker(module_diag);
    mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                               module_checker);
    fs::path iface = yona_lib / "Test" / "Fx.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN fetch 1 VAR(a) -> VAR(b)") != string::npos);
    CHECK(yonai.find("effects Fs.read") != string::npos);

    parser::Parser p2;
    string expr = R"(import fetch from Test\Fx in fetch "/etc/shadow")";
    istringstream stream(expr);
    auto parsed = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed->Expression.get());
    CHECK(checker.has_direct_errors());
  }

  TEST_CASE("Interface files serialize inferred polymorphic signatures") {
    namespace fs = std::filesystem;
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_inferred_signatures";
    fs::create_directories(yona_lib / "Test");

    parser::Parser module_parser;
    auto module = module_parser.parseModule(R"(
module Test\Inferred

export identity, apply, pair, value

identity x = x
apply f x = f x
pair x = (x, x)
value = 42
)",
                                            "inferred_signatures.yona");
    REQUIRE(module.has_value());

    Codegen module_codegen("inferred_signatures_mod");
    REQUIRE(module_codegen.compile_module(module.value().get()) != nullptr);
    DiagnosticEngine module_diag;
    typechecker::TypeChecker module_checker(module_diag);
    module_codegen.populate_interface_effect_rows(module.value().get(),
                                                  module_checker);
    REQUIRE_FALSE(module_checker.has_errors());

    const fs::path interface_path = yona_lib / "Test" / "Inferred.yonai";
    REQUIRE(module_codegen.emit_interface_file(interface_path.string()));
    const string yonai = read_file(interface_path);
    CHECK(yonai.find("FN identity 1 VAR(a) -> VAR(a)") != string::npos);
    CHECK(yonai.find("FN apply 2 FUNCTION(VAR(a),VAR(b)) VAR(a) -> VAR(b)") !=
          string::npos);
    CHECK(yonai.find("FN pair 1 VAR(a) -> TUPLE(VAR(a),VAR(a))") !=
          string::npos);
    CHECK(yonai.find("FN value 0 -> INT") != string::npos);

    parser::Parser client_parser;
    auto client = client_parser.parseExpression(
        R"(import identity, apply from Test\Inferred in
            (identity 1, identity "two", apply identity 3,
             apply identity "four"))",
        "inferred_signatures_client.yona");
    REQUIRE(client.has_value());
    REQUIRE(client->Expression != nullptr);
    DiagnosticEngine client_diag;
    typechecker::TypeChecker client_checker(client_diag);
    client_checker.add_module_path(yona_lib.string());
    client_checker.check(client->Expression.get());
    CHECK_FALSE(client_checker.has_errors());
  }

  TEST_CASE("GENFN native dependencies win exported C symbol collisions") {
    namespace fs = std::filesystem;
    const fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_native_symbol_collision";
    fs::create_directories(yona_lib / "Test");
    const fs::path interface_path = yona_lib / "Test" / "Collision.yonai";

    parser::Parser module_parser;
    auto module = module_parser.parseModule(R"(
module Test\Collision

export available

extern rawAvailable : Int -> Bool = "YonaTestCollisionAvailable"

available : () -> Bool
available () = rawAvailable 0
)",
                                            "native_symbol_collision.yona");
    REQUIRE(module.has_value());

    Codegen module_codegen("native_symbol_collision");
    REQUIRE(module_codegen.compile_module(module.value().get()) != nullptr);
    REQUIRE(module_codegen.emit_interface_file(interface_path.string()));

    const string yonai = read_file(interface_path);
    CHECK(yonai.find("GENFN_DEP available rawAvailable NATIVE "
                     "YonaTestCollisionAvailable FN 1 INT -> BOOL") !=
          string::npos);
    CHECK(yonai.find("GENFN_DEP available rawAvailable YONA Test\\Collision "
                     "available") == string::npos);

    parser::Parser client_parser;
    auto client = client_parser.parseExpression(
        R"(import available from Test\Collision in available ())",
        "native_symbol_collision_client.yona");
    REQUIRE(client.has_value());
    REQUIRE(client->Expression != nullptr);

    Codegen client_codegen("native_symbol_collision_client");
    client_codegen.set_opt_level(0);
    client_codegen.ModulePaths.push_back(yona_lib.string());
    REQUIRE(client_codegen.compile(client->Expression.get()) != nullptr);
    const string ir = client_codegen.emit_ir();
    CHECK(ir.find("call i1 @YonaTestCollisionAvailable(i64 0)") !=
          string::npos);
    CHECK(ir.find("rawAvailable__genfn") == string::npos);
  }

  TEST_CASE("Nested GENFN dependency overlays use the innermost provenance") {
    const fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_nested_provenance";
    fs::create_directories(yona_lib / "Test");
    const fs::path interface_path =
        yona_lib / "Test" / "NestedProvenance.yonai";

    ofstream(interface_path) << R"(MODULE Test\NestedProvenance
FN inner 1 INT -> INT
FN top 1 INT -> INT
FN yonaHelper 1 INT -> INT
GENFN_DEP inner helper YONA Test\NestedProvenance yonaHelper FN 1 INT -> INT
GENFN_BEGIN inner inner
inner value = helper value
GENFN_END
GENFN_DEP top helper NATIVE YonaStdMathAbs FN 1 INT -> INT
GENFN_BEGIN top top
top value = inner value
GENFN_END
GENFN_BEGIN yonaHelper yonaHelper
yonaHelper value = value + 1
GENFN_END
)";

    const string source = R"(import top from Test\NestedProvenance in top 5)";
    CHECK(compile_and_run(source, nullptr, nullptr, "nested_genfn_provenance",
                          nullptr, 0, &yona_lib) == "6");
  }

  TEST_CASE("Opaque exported ADTs omit constructors from their interface") {
    namespace fs = std::filesystem;
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_opaque";
    fs::create_directories(yona_lib / "Test");

    parser::Parser parser;
    auto mod_result = parser.parseModule(R"(
module Test\Token

export type Token opaque
export make, value

type Token = MkToken Int
make n = MkToken n
value (MkToken n) = n
)",
                                         "Token.yona");
    REQUIRE(mod_result.has_value());

    Codegen codegen("opaque_token_mod");
    REQUIRE(codegen.compile_module(mod_result.value().get()) != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "opaque_token_mod.o";
    REQUIRE(codegen.emit_object_file(mod_obj.string()));
    fs::path iface = yona_lib / "Test" / "Token.yonai";
    REQUIRE(codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("ADT Token 1 1 opaque") != string::npos);
    CHECK(yonai.find("CTOR MkToken") == string::npos);

    parser::Parser client_parser;
    auto client_result = client_parser.parseExpression(
        R"(import make, value from Test\Token in value (make 7))",
        "client.yona");
    REQUIRE(client_result.has_value());
    Codegen client_codegen("opaque_token_client");
    client_codegen.ModulePaths.push_back(yona_lib.string());
    REQUIRE(client_codegen.compile(client_result.value().get()) != nullptr);
    CHECK(client_codegen.errorCount() == 0);
    fs::path client_obj =
        yona::test::link::scratch_root() / "opaque_token_client.o";
    REQUIRE(client_codegen.emit_object_file(client_obj.string()));
    vector<fs::path> objects = {mod_obj, client_obj};
    REQUIRE(yona::test::link::append_runtime_objects(objects));
    fs::path exe = yona::test::link::scratch_root() /
                   ("opaque_token_client" + yona::test::link::exe_suffix());
    REQUIRE(yona::test::link::link_objs_to_exe(objects, exe));
    CHECK(yona::test::link::executeAndCapture(exe) == "7");

    parser::Parser hidden_ctor_parser;
    auto hidden_ctor_result = hidden_ctor_parser.parseExpression(
        R"(import Test\Token in MkToken 7)", "hidden-ctor.yona");
    REQUIRE(hidden_ctor_result.has_value());
    Codegen hidden_ctor_codegen("opaque_hidden_ctor");
    hidden_ctor_codegen.ModulePaths.push_back(yona_lib.string());
    CHECK(hidden_ctor_codegen.compile(hidden_ctor_result.value().get()) ==
          nullptr);
    CHECK(hidden_ctor_codegen.errorCount() > 0);

    auto transparent_result = parser.parseModule(R"(
module Test\Transparent
export type Transparent
type Transparent = Visible Int
)",
                                                 "Transparent.yona");
    REQUIRE(transparent_result.has_value());
    Codegen transparent_codegen("transparent_token_mod");
    REQUIRE(transparent_codegen.compile_module(
                transparent_result.value().get()) != nullptr);
    fs::path transparent_iface = yona_lib / "Test" / "Transparent.yonai";
    REQUIRE(
        transparent_codegen.emit_interface_file(transparent_iface.string()));
    CHECK(read_file(transparent_iface).find("CTOR Visible") != string::npos);
  }

  TEST_CASE("Interface files preserve exported HOF open rest") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_hof_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Hof

export apply

apply f x = f x
)";
    auto mod_result = p1.parseModule(mod_source, "hof.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("hof_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    DiagnosticEngine module_diag;
    typechecker::TypeChecker module_checker(module_diag);
    mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                               module_checker);
    fs::path iface = yona_lib / "Test" / "Hof.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN apply 2 FUNCTION(VAR(a),VAR(b)) VAR(a) -> VAR(b)") !=
          string::npos);
    CHECK(yonai.find("effects |") != string::npos);
    CHECK(yonai.find("hof") != string::npos);

    parser::Parser p2;
    string expr =
        R"(import apply from Test\Hof in apply (\() -> perform Fs.read "/etc/shadow") ())";
    istringstream stream(expr);
    auto parsed = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed->Expression.get());
    CHECK(checker.has_direct_errors());
  }

  TEST_CASE("Interface effect schemes preserve two independent callback rows") {
    namespace fs = std::filesystem;
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_effect_scheme";
    fs::create_directories(yona_lib / "Test");

    parser::Parser exporter_parser;
    auto exporter = exporter_parser.parseModule(R"(
module Test\EffectScheme

export use

use f g n = (f n, g n)
)",
                                                "effect_scheme_export.yona");
    REQUIRE(exporter.has_value());

    Codegen exporter_codegen("effect_scheme_export");
    REQUIRE(exporter_codegen.compile_module(exporter.value().get()) != nullptr);
    DiagnosticEngine exporter_diag;
    typechecker::TypeChecker exporter_checker(exporter_diag);
    exporter_codegen.populate_interface_effect_rows(exporter.value().get(),
                                                    exporter_checker);
    const fs::path iface = yona_lib / "Test" / "EffectScheme.yonai";
    REQUIRE(exporter_codegen.emit_interface_file(iface.string()));
    const string yonai = read_file(iface);
    CHECK(yonai.find("FN use 3 FUNCTION(VAR(a),VAR(b)) FUNCTION(VAR(a),VAR(c)) "
                     "VAR(a) -> TUPLE(VAR(b),VAR(c))") != string::npos);
    CHECK(yonai.find("effectscheme $#") != string::npos);

    parser::Parser client_parser;
    istringstream client_source(
        "import use from Test\\EffectScheme in\n"
        "use (\\x -> perform State.get ()) (\\x -> perform Log.log ()) 0");
    auto client =
        client_parser.parseExpression(client_source.str(), "<stream>");
    REQUIRE(client);
    REQUIRE(client->Expression != nullptr);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    auto *int_type = checker.arena().make_con(typechecker::TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_type}});
    checker.register_effect("Log", "", {{"log", {}, int_type}});
    Codegen importer_codegen("effect_scheme_import");
    importer_codegen.ModulePaths.push_back(yona_lib.string());
    checker.add_module_path(yona_lib.string());
    checker.check(client->Expression.get());

    REQUIRE(checker.has_direct_errors());
    size_t e0202 = 0;
    bool saw_state = false;
    bool saw_log = false;
    for (const auto &record : diag.records()) {
      if (record.level != DiagLevel::Error || record.code != ErrorCode::E0202)
        continue;
      ++e0202;
      saw_state = saw_state || record.message.find("State.get") != string::npos;
      saw_log = saw_log || record.message.find("Log.log") != string::npos;
    }
    CHECK(e0202 == 2);
    CHECK(saw_state);
    CHECK(saw_log);

    parser::Parser sibling_parser;
    auto sibling = sibling_parser.parseModule(R"(
module Test\EffectSchemeClient

export run

get x = do perform State.get (); x end
log x = do perform Log.log (); x end
ping x = do perform Net.ping (); x end
audit x = do perform Audit.write (); x end
run n = import use from Test\EffectScheme in
  (use get log n, use ping audit n)
)",
                                              "effect_scheme_client.yona");
    REQUIRE(sibling.has_value());
    DiagnosticEngine sibling_diag;
    typechecker::TypeChecker sibling_checker(sibling_diag);
    auto *sibling_int =
        sibling_checker.arena().make_con(typechecker::TyCon::Int);
    sibling_checker.register_effect("State", "s", {{"get", {}, sibling_int}});
    sibling_checker.register_effect("Log", "", {{"log", {}, sibling_int}});
    sibling_checker.register_effect("Net", "", {{"ping", {}, sibling_int}});
    sibling_checker.register_effect("Audit", "", {{"write", {}, sibling_int}});
    sibling_checker.add_module_path(yona_lib.string());
    sibling_checker.check_module(sibling.value().get());
    REQUIRE_FALSE(sibling_checker.has_direct_errors());
    REQUIRE_FALSE(sibling_diag.has_errors());
    REQUIRE(sibling.value()->functions.size() == 5);
    CHECK(sibling_checker.closed_effect_ops(
              sibling_checker.type_of(sibling.value()->functions[4])) ==
          vector<string>{"Audit.write", "Log.log", "Net.ping", "State.get"});
  }

  TEST_CASE("Interface files preserve sibling-wrapped FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_wrap_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Wrap

export wrap

readSecret = \() -> perform Fs.read "/etc/shadow"
wrap = \() -> readSecret ()
)";
    auto mod_result = p1.parseModule(mod_source, "wrap.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("wrap_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    DiagnosticEngine module_diag;
    typechecker::TypeChecker module_checker(module_diag);
    mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                               module_checker);
    fs::path iface = yona_lib / "Test" / "Wrap.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN wrap 0 -> FUNCTION(UNIT,VAR(a)) effects Fs.read") !=
          string::npos);

    parser::Parser p2;
    string expr = R"(import wrap from Test\Wrap in wrap ())";
    istringstream stream(expr);
    auto parsed = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed->Expression.get());
    CHECK(checker.has_direct_errors());
    CHECK(std::any_of(diag.records().begin(), diag.records().end(),
                      [](const auto &record) {
                        return record.level == DiagLevel::Error &&
                               record.code == ErrorCode::E0202;
                      }));
    CHECK_FALSE(std::any_of(diag.records().begin(), diag.records().end(),
                            [](const auto &record) {
                              return record.level == DiagLevel::Error &&
                                     record.code == ErrorCode::E0100;
                            }));

    parser::Parser p3;
    string handled = R"(
import wrap from Test\Wrap in
handle wrap () with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    istringstream stream2(handled);
    auto parsed2 = p3.parseExpression(stream2.str(), "<stream>");
    REQUIRE(parsed2);
    REQUIRE(parsed2->Expression);
    DiagnosticEngine diag2;
    typechecker::TypeChecker checker2(diag2);
    checker2.add_module_path(yona_lib.string());
    checker2.check(parsed2->Expression.get());
    CHECK_FALSE(checker2.has_direct_errors());
  }

  TEST_CASE("Interface files preserve wrap-before-sibling FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib =
        yona::test::link::scratch_root() / "yona_lib_wrap_first_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\WrapFirst

export wrap

wrap = \() -> readSecret ()
readSecret = \() -> perform Fs.read "/etc/shadow"
)";
    auto mod_result = p1.parseModule(mod_source, "wrap_first.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("wrap_first_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    DiagnosticEngine module_diag;
    typechecker::TypeChecker module_checker(module_diag);
    mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                               module_checker);
    fs::path iface = yona_lib / "Test" / "WrapFirst.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("effects Fs.read") != string::npos);

    parser::Parser p2;
    string expr = R"(import wrap from Test\WrapFirst in wrap ())";
    istringstream stream(expr);
    auto parsed = p2.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed->Expression.get());
    CHECK(checker.has_direct_errors());
  }

  TEST_CASE(
      "Module compile of exported wrapper around private helper has no E0104") {
    // CLI calls populate_interface_effect_rows after compile_module.
    // Per-function check() cannot see unexported siblings; check_module must be
    // used instead.
    parser::Parser parser;
    string mod_source = R"(
module Secret

export doubledSquare

helper x = x * x
doubledSquare x = 2 * helper x
)";
    auto mod_result = parser.parseModule(mod_source, "Secret.yona");
    REQUIRE(mod_result.has_value());

    DiagnosticEngine diag;
    Codegen codegen("secret_helper_mod", &diag);
    auto llvm_mod = codegen.compile_module(mod_result.value().get());
    REQUIRE(llvm_mod != nullptr);
    CHECK(codegen.errorCount() == 0);

    typechecker::TypeChecker tc(diag);
    codegen.populate_interface_effect_rows(mod_result.value().get(), tc);

    bool saw_e0104 = false;
    for (const auto &rec : diag.records()) {
      if (rec.level == DiagLevel::Error && rec.code == ErrorCode::E0104)
        saw_e0104 = true;
    }
    CHECK_FALSE(saw_e0104);
    CHECK_FALSE(tc.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

} // Codegen Modules

// ===== Diagnostic / Error Reporting Tests =====

TEST_SUITE("Diagnostics") {

  TEST_CASE("DiagnosticEngine error counting") {
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, "let x = 1 in y", "<test>");

    CHECK(diag.error_count() == 0);
    CHECK(!diag.has_errors());

    diag.error(SourceRange{DiagnosticSource, 1, 14, 0, 1},
               "undefined variable 'y'");
    CHECK(diag.error_count() == 1);
    CHECK(diag.has_errors());
  }

  TEST_CASE("DiagnosticEngine warning flags") {
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, "let x = 1 in 42", "<test>");

    // Warnings suppressed by default (no flags enabled)
    diag.warning({DiagnosticSource, 1, 5, 0, 1}, "unused variable 'x'",
                 WarningFlag::UnusedVariable);
    CHECK(diag.warning_count() == 0);

    // Enable -Wall
    diag.enable_wall();
    diag.warning({DiagnosticSource, 1, 5, 0, 1}, "unused variable 'x'",
                 WarningFlag::UnusedVariable);
    CHECK(diag.warning_count() == 1);
  }

  TEST_CASE("DiagnosticEngine -Werror") {
    DiagnosticEngine diag;
    diag.enable_wall();
    diag.set_warnings_as_errors(true);
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, "let x = 1 in 42", "<test>");

    diag.warning({DiagnosticSource, 1, 5, 0, 1}, "unused variable 'x'",
                 WarningFlag::UnusedVariable);
    CHECK(diag.error_count() == 1);   // promoted to error
    CHECK(diag.warning_count() == 0); // not counted as warning
  }

  TEST_CASE("DiagnosticEngine -w suppresses all") {
    DiagnosticEngine diag;
    diag.enable_wextra();
    diag.suppress_all_warnings();
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, "let x = 1 in 42", "<test>");

    diag.warning({DiagnosticSource, 1, 5, 0, 1}, "unused variable",
                 WarningFlag::UnusedVariable);
    diag.warning({DiagnosticSource, 1, 5, 0, 1}, "shadow", WarningFlag::Shadow);
    CHECK(diag.warning_count() == 0);
    CHECK(diag.error_count() == 0);
  }

  TEST_CASE("Codegen reports errors through DiagnosticEngine") {
    DiagnosticEngine diag;
    string source = "let x = 42 in y + 1";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "<test>");

    Codegen codegen("test", &diag);
    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    if (result && result->Expression)
      codegen.compile(result->Expression.get());

    CHECK(diag.has_errors());
    CHECK(diag.error_count() >= 1);
  }

  TEST_CASE("Expression diagnostics do not verify incomplete case IR") {
    const string source = "case 1 of 1 -> missing end";
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "case-diagnostic.yona");

    parser::Parser parser;
    auto result = parser.parseExpression(source, "case-diagnostic.yona");
    REQUIRE(result.has_value());
    REQUIRE(result->Expression != nullptr);

    Codegen codegen("case_diagnostic", &diag);
    std::ostringstream captured_stderr;
    auto *module = [&]() {
      struct RestoreStderr {
        std::streambuf *previous;
        ~RestoreStderr() { std::cerr.rdbuf(previous); }
      } restore{std::cerr.rdbuf(captured_stderr.rdbuf())};
      return codegen.compile(result->Expression.get());
    }();

    CHECK(module == nullptr);
    CHECK(diag.has_errors());
    CHECK(diag.error_count() >= 1);
    CHECK(captured_stderr.str().find("Module verification failed") ==
          string::npos);
  }

  TEST_CASE("Codegen suggests similar names for typos") {
    DiagnosticEngine diag;
    string source = "let myVariable = 42 in myVarible + 1";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "<test>");

    Codegen codegen("test", &diag);
    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    if (result && result->Expression)
      codegen.compile(result->Expression.get());

    // Should have an error with "did you mean" suggestion
    CHECK(diag.has_errors());
  }

  TEST_CASE("Warning flag names") {
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnusedVariable) ==
          "unused-variable");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnusedImport) ==
          "unused-import");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::Shadow) == "shadow");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::MissingSignature) ==
          "missing-signature");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::IncompletePatterns) ==
          "incomplete-patterns");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::OverlappingPatterns) ==
          "overlapping-patterns");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnmatchedAdt) ==
          "unmatched-adt");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::LinearLeak) ==
          "linear-leak");
  }

  TEST_CASE("Non-exhaustive ADT cases emit -Wincomplete-patterns") {
    const string source = "case Some 1 of Some x -> x end";
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "incomplete-case.yona");
    diag.enable_warning(WarningFlag::IncompletePatterns);

    parser::Parser parser;
    Codegen codegen("incomplete_case", &diag);
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);
    auto parsed = parser.parseExpression(source, "incomplete-case.yona");
    REQUIRE(parsed.has_value());
    auto coverage = codegen.finite_case_coverage(
        static_cast<CaseExpr *>(parsed.value().get()));
    REQUIRE(coverage.has_value());
    CHECK(coverage->adt_name == "Option");
    CHECK(coverage->missing == vector<string>{"None"});
    REQUIRE(codegen.compile(parsed.value().get()) != nullptr);

    REQUIRE(diag.warning_count() == 1);
    CHECK(diag.records().back().message.find(
              "non-exhaustive pattern match on Option") != string::npos);
    CHECK(diag.records().back().message.find("None") != string::npos);
  }

  TEST_CASE("Wildcard case arm satisfies ADT exhaustiveness") {
    const string source = "case Some 1 of Some x -> x; _ -> 0 end";
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "complete-case.yona");
    diag.enable_warning(WarningFlag::IncompletePatterns);

    parser::Parser parser;
    Codegen codegen("complete_case", &diag);
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);
    auto parsed = parser.parseExpression(source, "complete-case.yona");
    REQUIRE(parsed.has_value());
    CHECK_FALSE(
        codegen
            .finite_case_coverage(static_cast<CaseExpr *>(parsed.value().get()))
            .has_value());
    REQUIRE(codegen.compile(parsed.value().get()) != nullptr);

    CHECK(diag.warning_count() == 0);
  }

  TEST_CASE("Guarded constructor arm does not satisfy ADT exhaustiveness") {
    const string source = "case Some 1 of Some x if x > 0 -> x end";
    DiagnosticEngine diag;
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "guarded-case.yona");
    diag.enable_warning(WarningFlag::IncompletePatterns);

    parser::Parser parser;
    Codegen codegen("guarded_case", &diag);
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);
    auto parsed = parser.parseExpression(source, "guarded-case.yona");
    REQUIRE(parsed.has_value());
    auto coverage = codegen.finite_case_coverage(
        static_cast<CaseExpr *>(parsed.value().get()));
    REQUIRE(coverage.has_value());
    CHECK(coverage->missing == vector<string>{"None", "Some"});
    REQUIRE(codegen.compile(parsed.value().get()) != nullptr);

    REQUIRE(diag.warning_count() == 1);
    CHECK(diag.records().back().message.find("None, Some") != string::npos);
  }

  TEST_CASE("Case analysis finds shadowed and missing Bool arms") {
    const string source = "case true of true -> 1; true -> 2 end";
    parser::Parser parser;
    Codegen codegen("bool_case");
    auto parsed = parser.parseExpression(source, "bool-case.yona");
    REQUIRE(parsed.has_value());
    auto analysis = codegen.analyze_case_patterns(
        static_cast<CaseExpr *>(parsed.value().get()));
    CHECK(analysis.unreachable_clauses == vector<size_t>{1});
    REQUIRE(analysis.incomplete.has_value());
    CHECK(analysis.incomplete->adt_name == "Bool");
    CHECK(analysis.incomplete->missing == vector<string>{"False"});
  }

  TEST_CASE("Case analysis treats both Bool arms as exhaustive") {
    const string source = "case true of true -> 1; false -> 0 end";
    parser::Parser parser;
    Codegen codegen("complete_bool_case");
    auto parsed = parser.parseExpression(source, "bool-complete.yona");
    REQUIRE(parsed.has_value());
    auto analysis = codegen.analyze_case_patterns(
        static_cast<CaseExpr *>(parsed.value().get()));
    CHECK(analysis.unreachable_clauses.empty());
    CHECK_FALSE(analysis.incomplete.has_value());
  }

  TEST_CASE("Case analysis distinguishes nested constructor coverage") {
    parser::Parser parser;
    Codegen codegen("nested_case");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);

    auto partial = parser.parseExpression(
        "case Some 1 of Some 1 -> 1; Some _ -> 2; None -> 3 end",
        "nested-partial.yona");
    REQUIRE(partial.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(partial.value().get()))
              .unreachable_clauses.empty());

    auto covered = parser.parseExpression(
        "case Some 1 of Some _ -> 1; Some 1 -> 2; None -> 3 end",
        "nested-covered.yona");
    REQUIRE(covered.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(covered.value().get()))
              .unreachable_clauses == vector<size_t>{1});

    auto tuple = parser.parseExpression(
        "case (true, 1) of (true, _) -> 1; (true, 1) -> 2; _ -> 3 end",
        "tuple-covered.yona");
    REQUIRE(tuple.has_value());
    CHECK(static_cast<CaseExpr *>(tuple.value().get())
              ->clauses[0]
              ->pattern->get_type() == ast::AST_TUPLE_PATTERN);
    CHECK(
        codegen
            .analyze_case_patterns(static_cast<CaseExpr *>(tuple.value().get()))
            .unreachable_clauses == vector<size_t>{1});

    auto sequence = parser.parseExpression(
        "case [1] of [_] -> 1; [1] -> 2; _ -> 3 end", "sequence-covered.yona");
    REQUIRE(sequence.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(sequence.value().get()))
              .unreachable_clauses == vector<size_t>{1});
  }

  TEST_CASE("Case analysis preserves aliases and sequence shape coverage") {
    parser::Parser parser;
    Codegen codegen("structured_case");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);
    auto alias = parser.parseExpression(
        "case Some 1 of whole @ Some _ -> 1; Some 1 -> 2; None -> 3 end",
        "alias-covered.yona");
    REQUIRE(alias.has_value());
    CHECK(
        codegen
            .analyze_case_patterns(static_cast<CaseExpr *>(alias.value().get()))
            .unreachable_clauses == vector<size_t>{1});
    auto head_tail = parser.parseExpression(
        "case [1] of [x | xs] -> 1; [1 | rest] -> 2; [] -> 3 end",
        "head-tail-covered.yona");
    REQUIRE(head_tail.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(head_tail.value().get()))
              .unreachable_clauses == vector<size_t>{1});
    auto alternatives = parser.parseExpression(
        "case Some 1 of Some _ | None -> 1; None -> 2 end",
        "alternative-covered.yona");
    REQUIRE(alternatives.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(alternatives.value().get()))
              .unreachable_clauses == vector<size_t>{1});
  }

  TEST_CASE("Case analysis keeps partial and guarded arms useful") {
    parser::Parser parser;
    Codegen codegen("conservative_case");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);
    auto literals = parser.parseExpression(
        "case \"a\" of \"a\" -> 1; \"b\" -> 2; _ -> 3 end",
        "distinct-literals.yona");
    REQUIRE(literals.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(literals.value().get()))
              .unreachable_clauses.empty());
    auto guarded = parser.parseExpression(
        "case Some 1 of Some _ if true -> 1; Some 1 -> 2; None -> 3 end",
        "guarded-useful.yona");
    REQUIRE(guarded.has_value());
    CHECK(codegen
              .analyze_case_patterns(
                  static_cast<CaseExpr *>(guarded.value().get()))
              .unreachable_clauses.empty());
  }

  TEST_CASE("Parser errors route through DiagnosticEngine") {
    DiagnosticEngine diag;
    string source = "let x = in 42";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "<test>");

    parser::Parser parser;
    auto result = parser.parseExpression(source, "<test>");
    if (!result.has_value()) {
      for (auto &e : result.error())
        diag.error(e.Range, e.Message);
    }

    CHECK(diag.has_errors());
  }

  TEST_CASE("Parser: perform as multi-binding let RHS") {
    parser::Parser parser;
    string source =
        R"(let a = perform Fs.read "x", b = perform Net.post "y" in a)";
    auto result = parser.parseExpression(source, "<test>");
    REQUIRE(result.has_value());
    auto *let = dynamic_cast<LetExpr *>(result.value().get());
    REQUIRE(let != nullptr);
    REQUIRE(let->aliases.size() == 2);
    auto *a = dynamic_cast<ValueAlias *>(let->aliases[0]);
    auto *b = dynamic_cast<ValueAlias *>(let->aliases[1]);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    auto *pa = dynamic_cast<PerformExpr *>(a->expr);
    auto *pb = dynamic_cast<PerformExpr *>(b->expr);
    REQUIRE(pa != nullptr);
    REQUIRE(pb != nullptr);
    CHECK(pa->effect_name == "Fs");
    CHECK(pa->operation_name == "read");
    REQUIRE(pa->args.size() == 1);
    CHECK(pb->effect_name == "Net");
    CHECK(pb->operation_name == "post");
    REQUIRE(pb->args.size() == 1);
  }

  TEST_CASE("Prelude constructors load via YONA_PATH") {
    REQUIRE(fs::exists(yona::test::lib_dir() / "Prelude.yonai"));

    const char *old = std::getenv("YONA_PATH");
    const string saved = old ? old : "";
#ifdef _WIN32
    _putenv_s("YONA_PATH", yona::test::lib_dir().string().c_str());
#else
    setenv("YONA_PATH", yona::test::lib_dir().string().c_str(), 1);
#endif

    parser::Parser parser;
    DiagnosticEngine diag;
    Codegen codegen("prelude_yona_path", &diag);
    // No cwd-relative lib/ on ModulePaths â€” only YONA_PATH.
    YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);

    string source = "case None of Some x -> x; None -> 0 end";
    istringstream stream(source);
    auto parse_result = parser.parseExpression(stream.str(), "<stream>");

#ifdef _WIN32
    _putenv_s("YONA_PATH", saved.c_str());
#else
    if (saved.empty())
      unsetenv("YONA_PATH");
    else
      setenv("YONA_PATH", saved.c_str(), 1);
#endif

    REQUIRE(parse_result);
    REQUIRE(parse_result->Expression != nullptr);
    auto *mod = codegen.compile(parse_result->Expression.get());
    CHECK(mod != nullptr);
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE("Debug info: compilation succeeds with -g") {
    // Compiling with debug info enabled should not break anything
    DiagnosticEngine diag;
    string source = "let x = 42 in let y = x + 1 in y";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "test_debug.yona");

    Codegen codegen("debug_test", &diag);
    codegen.set_debug_info(true, "test_debug.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);

    auto *mod = codegen.compile(result->Expression.get());
    REQUIRE(mod != nullptr);

    // Should compile without errors
    CHECK(!diag.has_errors());
    CHECK(codegen.errorCount() == 0);
  }

  TEST_CASE("Debug info: function with params") {
    DiagnosticEngine diag;
    string source = "let add x y = x + y in add 10 32";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "test_fn.yona");

    Codegen codegen("debug_fn_test", &diag);
    codegen.set_debug_info(true, "test_fn.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);

    auto *mod = codegen.compile(result->Expression.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());
  }

  TEST_CASE("Debug info: closures") {
    DiagnosticEngine diag;
    string source = "let n = 10 in let add_n x = x + n in add_n 5";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "test_closure.yona");

    Codegen codegen("debug_closure_test", &diag);
    codegen.set_debug_info(true, "test_closure.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);

    auto *mod = codegen.compile(result->Expression.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());
  }

  TEST_CASE("Debug info: disabled by default") {
    // Without set_debug_info, no debug metadata should be generated
    DiagnosticEngine diag;
    string source = "42";
    [[maybe_unused]] const SourceId DiagnosticSource =
        set_diagnostic_source(diag, source, "test.yona");

    Codegen codegen("no_debug_test", &diag);
    // NOT calling set_debug_info

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);

    auto *mod = codegen.compile(result->Expression.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());

    // IR should NOT contain !dbg metadata
    string ir = codegen.emit_ir();
    CHECK(ir.find("!dbg") == string::npos);
  }

} // Diagnostics

// IR fixture tests removed â€” IR text comparison is fragile due to
// whitespace/formatting differences between runs. The E2E fixture tests
// (compile â†’ run â†’ check output) are more reliable and valuable.

TEST_SUITE("Imported HOF") {

  TEST_CASE("Lexical self analysis descends into nested guarded functions") {
    parser::Parser parser;
    auto parsed = parser.parseModule(R"(
module Test\NestedGuardedSelf
inner n = if true -> outer n
)",
                                     "nested_guarded_self.yona");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Module->functions.size() == 1);
    auto *nested = parsed->Module->functions.front();
    REQUIRE(nested->bodies.size() == 1);
    REQUIRE(dynamic_cast<ast::BodyWithGuards *>(nested->bodies.front()) !=
            nullptr);

    parsed->Module->functions.clear();
    vector<ast::FunctionBody *> bodies = {
        new ast::BodyWithoutGuards(nested->Range, nested)};
    ast::FunctionExpr outer(nested->Range, "outer", {}, std::move(bodies));
    CHECK(Codegen::function_references_lexical_self(&outer));
  }

  TEST_CASE("Aliased imported recursive GENFN remains first-class") {
    CHECK(
        compile_and_run(
            R"(import map as listMap from Std\List in let m = listMap in m (\x -> x + 1) [1, 2, 3])",
            nullptr, nullptr, "imported_hof_recursive_alias") == "[2, 3, 4]");
  }

  TEST_CASE("Imported module function as first-class HOF argument") {
    // `length` is used as a value (passed to `map`), not called. Must
    // materialize a closure; wrapping in a lambda already works.
    CHECK(
        compile_and_run(
            R"(import map from Std\List, length from Std\String in map length ["ab", "abc"])",
            nullptr, nullptr, "imported_hof_length") == "[2, 3]");
  }

  TEST_CASE("Imported module function as first-class HOF argument on Stream") {
    // Stream.map is lazy ADT / generator shaped. Imported `length` as a
    // value must work on that path too â€” do not require `\s -> length s`.
    CHECK(
        compile_and_run(
            R"(import map, fromSeq, toSeq from Std\Stream, length from Std\String in toSeq (map length (fromSeq ["ab", "abc"])))",
            nullptr, nullptr, "imported_hof_stream_length") == "[2, 3]");
  }

  TEST_CASE("Stream.map applied to a Seq is a type error") {
    // Stream.map cases on Yield/Nil. A Seq is not a Stream; compiling
    // `toSeq (map length ["ab", "abc"])` must fail at typecheck (E0100),
    // not produce a crashing binary. Use fromSeq to lift the Seq.
    const char *source =
        R"(import map, toSeq from Std\Stream, length from Std\String in toSeq (map length ["ab", "abc"]))";

    parser::Parser parser;
    Codegen codegen("stream_map_seq_tyerr");
    if (fs::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.ModulePaths.push_back(fs::canonical(dir).string());
    }

    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);
    for (auto &p : codegen.ModulePaths)
      type_checker.add_module_path(p);

    istringstream stream(source);
    auto parse_result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parse_result);
    REQUIRE(parse_result->Expression);
    type_checker.check(parse_result->Expression.get());

    CHECK(type_checker.has_direct_errors());
    CHECK(tc_diag.has_errors());
    bool saw_e0100 = false;
    bool mentions_seq = false;
    bool mentions_stream_or_adt = false;
    for (auto &rec : tc_diag.records()) {
      if (rec.code && *rec.code == ErrorCode::E0100)
        saw_e0100 = true;
      if (rec.message.find("Seq") != string::npos)
        mentions_seq = true;
      if (rec.message.find("Stream") != string::npos ||
          rec.message.find("ADT") != string::npos)
        mentions_stream_or_adt = true;
    }
    CHECK(saw_e0100);
    CHECK(mentions_seq);
    CHECK(mentions_stream_or_adt);
  }

} // Imported HOF

TEST_SUITE("Regex") {

  TEST_CASE("Regex module: matches, find, replace, split") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());

    // Compile the Regex module
    auto regex_yona = yona::test::lib_dir() / "Std" / "Regex.yona";
    REQUIRE(fs::exists(regex_yona));
    ifstream f(regex_yona);
    string regex_mod_src((istreambuf_iterator<char>(f)),
                         istreambuf_iterator<char>());
    REQUIRE(!regex_mod_src.empty());

    parser::Parser mp;
    auto mod_result = mp.parseModule(regex_mod_src, "Regex.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("regex_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "regex_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::path yona_regex_lib =
        yona::test::link::scratch_root() / "yona_regex_lib";
    fs::path yonai_dir = yona_regex_lib / "Std";
    fs::create_directories(yonai_dir);
    REQUIRE(
        mod_codegen.emit_interface_file((yonai_dir / "Regex.yonai").string()));

    // Helper: compile expression, link with module + runtime, run, return
    // output
    auto run_expr = [&](const string &expr_source) -> string {
      parser::Parser ep;
      istringstream stream(expr_source);
      auto expr_result = ep.parseExpression(stream.str(), "<stream>");
      if (!expr_result || !expr_result->Expression)
        return "PARSE_ERROR";

      Codegen expr_codegen("regex_test");
      expr_codegen.ModulePaths.push_back(yona_regex_lib.string());
      if (fs::exists(yona::test::lib_dir()))
        expr_codegen.ModulePaths.push_back(
            fs::canonical(yona::test::lib_dir()).string());
      for (auto &dir : {".", "lib", "../lib", "../../lib"})
        if (fs::exists(dir))
          expr_codegen.ModulePaths.push_back(fs::canonical(dir).string());
      auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
      if (!expr_mod)
        return "CODEGEN_ERROR";
      fs::path expr_obj =
          yona::test::link::scratch_root() / "regex_expr_test.o";
      if (!expr_codegen.emit_object_file(expr_obj.string()))
        return "EMIT_ERROR";

      fs::path exe_out = yona::test::link::scratch_root() /
                         ("regex_link_test" + yona::test::link::exe_suffix());
      const auto libs = yona::test::link::pcreLinkArguments();
      // C-backed Std modules ship their public ABI in the runtime object. The
      // source module above is compiled to validate and emit its interface,
      // but linking both its export trampolines and the runtime ABI would
      // intentionally define the same public symbols twice.
      vector<fs::path> robj = {expr_obj};
      if (!yona::test::link::append_runtime_objects(robj))
        return "RT_COMPILE_ERROR";
      if (!yona::test::link::link_objs_to_exe(robj, exe_out, libs))
        return "LINK_ERROR";

      return yona::test::link::executeAndCapture(exe_out);
    };

    SUBCASE("matches true") {
      CHECK(
          run_expr(
              R"YT(import matches, compile from Std\Regex in matches (compile "[0-9]+") "abc 42 def")YT") ==
          "true");
    }

    SUBCASE("matches false") {
      CHECK(
          run_expr(
              R"YT(import matches, compile from Std\Regex in matches (compile "[0-9]+") "no digits")YT") ==
          "false");
    }

    SUBCASE("replace") {
      CHECK(
          run_expr(
              R"YT(import replace, compile from Std\Regex in replace (compile "[0-9]+") "abc 42 def 99" "NUM")YT") ==
          "abc NUM def 99");
    }

    SUBCASE("replaceAll") {
      CHECK(
          run_expr(
              R"YT(import replaceAll, compile from Std\Regex in replaceAll (compile "[0-9]+") "abc 42 def 99" "NUM")YT") ==
          "abc NUM def NUM");
    }

    SUBCASE("find match") {
      CHECK(run_expr(R"YT(import find, compile from Std\Regex in
            case find (compile "([a-z]+)([0-9]+)") "test123" of
                [] -> "none"
                [m | _] -> m
            end)YT") == "test123");
    }

    SUBCASE("find no match") {
      CHECK(run_expr(R"YT(import find, compile from Std\Regex in
            case find (compile "[0-9]+") "no digits" of
                [] -> "none"
                [m | _] -> m
            end)YT") == "none");
    }
  }

} // Regex

TEST_CASE("accelerator_diagnostic_report_std_gpu_sites") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"YT(import fromSeq from Std\IntArray in
import upload, mapAdd, reduceSum from Std\Gpu in
let build n acc = if n <= 0 then acc else build (n - 1) (n :: acc) in
let input = fromSeq (build 3 []) in
let buffer = upload input in
let shifted = mapAdd 1 buffer in
reduceSum shifted
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result->Expression.get(),
                                     &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("schema":"yona.accelerator_diag")") != std::string::npos);
  CHECK(json.find(R"("report_kind":"program")") != std::string::npos);
  CHECK(json.find(R"("file":"<test>")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") !=
        std::string::npos);
  CHECK(json.find(R"("op":"mapAdd")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"Int -> Buffer -> Buffer")") !=
        std::string::npos);
  CHECK(json.find(R"("op":"reduceSum")") != std::string::npos);
  CHECK(json.find(R"("binding":"import")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_canonical_std_gpu_fqn") {
  parser::Parser parser;
  std::istringstream stream(R"(Std\Gpu::available ())");
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result->Expression.get(),
                                     nullptr, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("op":"available")") != std::string::npos);
  CHECK(json.find(R"("binding":"Std\\Gpu")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_std_gpu_float_array_async") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"YT(import fill from Std\FloatArray in
import floatArrayScaleAsync, floatArrayMul2Async from Std\Gpu in
let xs = fill 4 2.5 in
let ps = floatArrayScaleAsync 3.0 xs in
let pm = floatArrayMul2Async xs in
(pm, ps)
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result->Expression.get(),
                                     &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("op":"floatArrayScaleAsync")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"Float -> FloatArray -> Int")") !=
        std::string::npos);
  CHECK(json.find(R"("op":"floatArrayMul2Async")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"FloatArray -> Int")") !=
        std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_std_gpu_discovery_calls") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source =
      R"YT(import available, physicalDeviceCount from Std\Gpu in
(available (), physicalDeviceCount ())
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parseExpression(stream.str(), "<stream>");
  REQUIRE(parse_result);
  REQUIRE(parse_result->Expression);
  type_checker.check(parse_result->Expression.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result->Expression.get(),
                                     &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("op":"available")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"() -> Bool")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"() -> Int")") != std::string::npos);
  CHECK(json.find(R"("op":"physicalDeviceCount")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_module_ast_scan") {
  parser::Parser parser;
  DiagnosticEngine diag;
  Codegen codegen("yona_module", &diag);
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  YONA_TEST_INSTALL_PARSER_PRELUDE(codegen, parser);

  const char *source = R"(module Test\AccelReportMod

export f

f xs = import upload from Std\Gpu in upload xs
)";
  auto mod = parser.parseModule(source, "<module>");
  REQUIRE(mod.has_value());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report_for_module(oss, mod.value().get(),
                                                "<module>");
  const std::string json = oss.str();
  CHECK(json.find(R"("report_kind":"module_ast")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") !=
        std::string::npos);
  CHECK(json.find(R"("binding":"import")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_module_typed_scan") {
  parser::Parser parser;
  DiagnosticEngine diag;
  Codegen codegen("yona_module", &diag);
  if (fs::exists(yona::test::lib_dir()))
    codegen.ModulePaths.push_back(
        fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.ModulePaths.push_back(fs::canonical(dir).string());
  }
  typechecker::TypeChecker type_checker(diag);
  YONA_TEST_INSTALL_PRELUDE(codegen, parser, type_checker);

  const char *source = R"(module Test\AccelReportModTyped

export f

f xs = import upload from Std\Gpu in upload xs
)";
  auto mod = parser.parseModule(source, "<module>");
  REQUIRE(mod.has_value());
  REQUIRE(
      typecheck_module_for_accelerator_report(mod.value().get(), type_checker));

  std::ostringstream oss;
  emit_accelerator_diagnostic_report_for_module(oss, mod.value().get(),
                                                "<module>", &type_checker);
  const std::string json = oss.str();
  CHECK(json.find(R"("report_kind":"module")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") !=
        std::string::npos);
}
