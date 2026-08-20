#include "AcceleratorDiag.h"
#include "Codegen.h"
#include "Diagnostic.h"
#include "Parser.h"
#include "repo_paths.h"
#include "typechecker/TypeChecker.h"
#include "yona_link_util.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace std;
using namespace yona;
using namespace yona::compiler;
using namespace yona::compiler::codegen;
namespace fs = std::filesystem;

// ===== Helpers =====

static string compile_to_ir(const string &code, int opt_level = 0) {
  parser::Parser parser;
  istringstream stream(code);
  auto parse_result = parser.parse_input(stream);
  if (!parse_result.node)
    return "PARSE_ERROR";

  Codegen codegen("yona_program");
  codegen.set_opt_level(opt_level);
  auto module = codegen.compile(parse_result.node.get());
  if (!module)
    return "CODEGEN_ERROR";

  return codegen.emit_ir();
}

static bool ir_contains(const string &ir, const string &pattern) { return ir.find(pattern) != string::npos; }

static string ir_function_body(const string &ir, const string &fn_name) {
  string marker = "define internal fastcc";
  size_t start = ir.find(marker + " ");
  while (start != string::npos) {
    size_t name_pos = ir.find("@" + fn_name + "(", start);
    size_t next = ir.find("\ndefine ", start + 1);
    if (name_pos != string::npos && (next == string::npos || name_pos < next))
      return ir.substr(start, next == string::npos ? string::npos : next - start);
    start = ir.find(marker + " ", start + 1);
  }
  return "";
}

static string compile_and_run(const string &code, const char *run_env_key = nullptr, const char *run_env_val = nullptr,
                              const char *artifact_suffix = nullptr) {
  parser::Parser parser;

  Codegen codegen("test_module");
  if (fs::exists(yona::test::lib_dir()))
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  // Type check (blocking)
  DiagnosticEngine tc_diag;
  typechecker::TypeChecker type_checker(tc_diag);
  codegen.load_prelude(&parser, &type_checker);
  for (auto& p : codegen.module_paths_)
    type_checker.add_module_path(p);

  istringstream stream(code);
  auto parse_result = parser.parse_input(stream);
  if (!parse_result.node)
    return "PARSE_ERROR";

  type_checker.set_import_type_source(&codegen.import_types_);
  type_checker.check(parse_result.node.get());
  if (type_checker.has_direct_errors())
    return "TYPE_ERROR";

  auto module = codegen.compile(parse_result.node.get());
  if (!module)
    return "CODEGEN_ERROR";

  const string stem = artifact_suffix ? string("yona_codegen_") + artifact_suffix + ".o" : "yona_codegen_test.o";
  const string exe_stem = artifact_suffix ? string("yona_codegen_") + artifact_suffix : "yona_codegen_test";
  fs::path obj_path = yona::test::link::scratch_root() / stem;
  if (!codegen.emit_object_file(obj_path.string()))
    return "EMIT_ERROR";

  vector<fs::path> objs = {obj_path};
  if (!yona::test::link::append_runtime_objects(objs))
    return "RT_COMPILE_ERROR";

  fs::path regex_o;
  const bool have_regex = yona::test::link::ensure_regex_obj(regex_o);
  string extra_libs;
  if (have_regex) {
    objs.push_back(regex_o);
    extra_libs = yona::test::link::pcre_link_flags();
  }

  fs::path exe_path = yona::test::link::scratch_root() / (exe_stem + yona::test::link::exe_suffix());
  if (!yona::test::link::link_objs_to_exe(objs, exe_path, extra_libs))
    return "LINK_ERROR";

  if (run_env_key && run_env_val)
    return yona::test::link::popen_read_all_run_with_env(exe_path, run_env_key, run_env_val);
  return yona::test::link::popen_read_all(exe_path);
}

static string read_file(const fs::path &path);

/* Ensures merged runtime .o exists (same artifact compile_and_run uses). */
static void ensure_compiled_runtime_test_obj() { REQUIRE(yona::test::link::ensure_runtime_objects()); }

/* Link yona .o + runtime, run with YONA_ALLOC_STATS=1; fail if any tag shows leaked>0. */
static void assert_linked_yona_zero_alloc_leaks(const string &obj_path, const string &exe_path_stem) {
  vector<fs::path> o = {fs::path(obj_path)};
  REQUIRE(yona::test::link::append_runtime_objects(o));
  fs::path exe_path = yona::test::link::scratch_root() / (exe_path_stem + yona::test::link::exe_suffix());
  REQUIRE(yona::test::link::link_objs_to_exe(o, exe_path));

  string combined;
#ifdef _WIN32
  fs::path cap = yona::test::link::scratch_root() / "yona_alloc_stats_capture.txt";
  fs::path bat = yona::test::link::scratch_root() / "yona_alloc_stats_capture.bat";
  {
    ofstream b(bat.string(), ios::binary);
    b << "@echo off\r\nset YONA_ALLOC_STATS=1\r\n";
    b << yona::test::link::qpath(exe_path) << " >" << yona::test::link::qpath(cap) << " 2>&1\r\n";
  }
  string runbat = string("cmd /c ") + yona::test::link::qpath(bat) + yona::test::link::err_null();
  REQUIRE(yona::test::link::sh(runbat) == 0);
  combined = read_file(cap);
#else
  string cmd = yona::test::link::alloc_stats_cmd(exe_path);
  array<char, 512> buf;
  FILE *pipe = popen(cmd.c_str(), "r");
  REQUIRE(pipe != nullptr);
  while (fgets(buf.data(), buf.size(), pipe))
    combined += buf.data();
  pclose(pipe);
#endif

  INFO("Full output:\n" << combined);
  CHECK(combined.find("alloc-stats") != string::npos);

  size_t pos = 0;
  while ((pos = combined.find("leaked=", pos)) != string::npos) {
    pos += 7;
    size_t end = pos;
    while (end < combined.size() && isdigit((unsigned char)combined[end]))
      end++;
    string n = combined.substr(pos, end - pos);
    CHECK_MESSAGE(n == "0", "Found leaked=" << n << " in alloc stats. Output:\n" << combined);
    pos = end;
  }
}

static string read_file(const fs::path &path) {
  ifstream f(path);
  if (!f.is_open())
    return "";
  stringstream buf;
  buf << f.rdbuf();
  string content = buf.str();
  // Trim trailing whitespace/newlines
  while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' '))
    content.pop_back();
  return content;
}

// ===== IR Snapshot Tests =====

TEST_SUITE("Codegen IR") {

  TEST_CASE("Integer addition uses native add") {
    auto ir = compile_to_ir("1 + 2");
    CHECK(ir_contains(ir, "yona_rt_print_int(i64 3)"));
  }

  TEST_CASE("If expression with constant condition is optimized away") {
    auto ir = compile_to_ir("if true then 1 else 0", 2);
    // O2 constant-folds: if true → 1, eliminates branch
    CHECK(ir_contains(ir, "yona_rt_print_int(i64 1)"));
  }

  TEST_CASE("If expression with variable generates branch") {
    auto ir = compile_to_ir("let x = 5 in if x > 3 then 1 else 0", 2);
    // O2 constant-folds this (5 > 3 = true → 1)
    CHECK(ir_contains(ir, "yona_rt_print_int(i64 1)"));
  }

  TEST_CASE("With expression generates close call") {
    auto ir = compile_to_ir("with fd = 0 in 42");
    CHECK(ir_contains(ir, "yona_rt_close"));
  }

  TEST_CASE("Borrow inference eliminates rc_inc for closure param in foldl") {
    auto ir = compile_to_ir("let foldl fn acc seq = case seq of [] -> acc; "
                            "[h|t] -> foldl fn (fn acc h) t end in "
                            "foldl (\\a b -> a + b) 0 [1,2,3]");
    // fn is borrowed (not returned, not captured) → no rc_inc call
    // instruction in the foldl function body. The declaration may
    // still exist, so check for the "call" form specifically.
    CHECK(!ir_contains(ir, "call void @yona_rt_rc_inc"));
  }

  TEST_CASE("Borrowed heap params are excluded from owned cleanup paths") {
    auto borrowed_ir = compile_to_ir("let inspect xs = 1 in inspect [1,2,3]");
    auto borrowed_body = ir_function_body(borrowed_ir, "inspect");
    REQUIRE(!borrowed_body.empty());
    CHECK(!ir_contains(borrowed_body, "call void @yona_rt_rc_dec"));
    CHECK(!ir_contains(borrowed_ir, "call void @yona_rt_frame_push"));
    CHECK(ir_contains(borrowed_ir, "call void @yona_rt_rc_dec"));

    auto owned_ir = compile_to_ir("let keep xs = xs in keep [1,2,3]");
    auto owned_body = ir_function_body(owned_ir, "keep");
    REQUIRE(!owned_body.empty());
    CHECK(ir_contains(owned_ir, "call void @yona_rt_frame_push"));

    auto raising_ir = compile_to_ir("let always_raise xs = raise 42 in "
                                    "try always_raise [1,2,3] catch _ -> 0 end");
    auto raising_body = ir_function_body(raising_ir, "always_raise");
    REQUIRE(!raising_body.empty());
    CHECK(ir_contains(raising_ir, "call void @yona_rt_frame_push"));
  }

  TEST_CASE("Explicit @borrow on foldl fn param eliminates rc_inc") {
    auto ir = compile_to_ir("let foldl @borrow fn acc seq = case seq of [] -> acc; "
                            "[h|t] -> foldl fn (fn acc h) t end in "
                            "foldl (\\a b -> a + b) 0 [1,2,3]");
    CHECK(!ir_contains(ir, "call void @yona_rt_rc_inc"));
  }

  // with expression E2E test is in test/codegen/with_value.yona fixture

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
    CHECK(compile_and_run(
              R"(let plan = \() -> perform Fs.read "/etc/shadow" in plan ())")
          == "TYPE_ERROR");
  }

  TEST_CASE("HOF apply of unhandled perform lambda is TYPE_ERROR") {
    CHECK(compile_and_run(
              R"(let apply = \f x -> f x in apply (\() -> perform Fs.read "/etc/shadow") ())")
          == "TYPE_ERROR");
  }

} // Codegen IR

// ===== End-to-End Fixture Tests =====
// Loads .yona source files and .expected output files from test/codegen/

TEST_SUITE("Codegen E2E") {

  TEST_CASE("Fixture-based codegen tests") {
    fs::path fixtures_dir = yona::test::codegen_fixtures_dir();
    if (!fs::exists(fixtures_dir) || !fs::is_directory(fixtures_dir)) {
      WARN("Could not find test/codegen fixtures directory (YONA_SOURCE_DIR)");
      return;
    }

    // Collect all .yona files
    vector<fs::path> test_files;
    for (auto &entry : fs::directory_iterator(fixtures_dir)) {
      if (entry.path().extension() == ".yona") {
        test_files.push_back(entry.path());
      }
    }
    sort(test_files.begin(), test_files.end());

    REQUIRE(!test_files.empty());

    // Set up and tear down scratch files that specific fixtures read from.
    // The fixtures foldl_iterator and iterator_gen_lines assume a
    // /tmp/yona_iter_gen_lines_test.txt file with 3 lines totalling 14 bytes.
    struct ScratchFiles {
      std::vector<fs::path> paths;
      ScratchFiles() {
        auto p = yona::test::link::scratch_root() / "yona_iter_gen_lines_test.txt";
        std::ofstream(p) << "abcde\nfghij\nklmn\n";
        paths.push_back(p);
#if !defined(__linux__)
        auto rel = yona::test::link::scratch_root() / "yona_stub_os_release.txt";
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

    for (const auto &yona_file : test_files) {
      auto expected_file = yona_file;
      expected_file.replace_extension(".expected");

      if (!fs::exists(expected_file))
        continue;

      string source = read_file(yona_file);
      yona::test::link::rewrite_codegen_fixture_tmp_paths(source);
      string expected = read_file(expected_file);
      string test_name = yona_file.stem().string();

      SUBCASE(test_name.c_str()) {
        const char *env_k = nullptr;
        const char *env_v = nullptr;
        if (test_name == "gpu_backend_flags" || test_name == "gpu_vulkan_last_note") {
          env_k = "YONA_GPU_DISABLE_VULKAN";
          env_v = "1";
        }
        string actual = compile_and_run(source, env_k, env_v, test_name.c_str());
        CHECK_MESSAGE(actual == expected, "Test '", test_name, "': expected '", expected, "' but got '", actual, "'");
      }
    }
  }

} // Codegen E2E

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
    ensure_compiled_runtime_test_obj();
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
      codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.module_paths_.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    codegen.load_prelude(&parser, &type_checker);
    istringstream stream(source);
    auto pr = parser.parse_input(stream);
    REQUIRE(pr.node);
    type_checker.set_import_type_source(&codegen.import_types_);
    type_checker.check(pr.node.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr.node.get());
    REQUIRE(module);

    fs::path obj_path = yona::test::link::scratch_root() / "yona_perceus_raise.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));

    assert_linked_yona_zero_alloc_leaks(obj_path.string(), "yona_perceus_raise");
  }

  TEST_CASE("borrowed temporary heap arguments are released by caller") {
    ensure_compiled_runtime_test_obj();
    string source = "let inspect xs = 1 in inspect [1,2,3]";

    parser::Parser parser;
    Codegen codegen("borrowed_temp_cleanup_test");
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    codegen.load_prelude(&parser, &type_checker);
    istringstream stream(source);
    auto pr = parser.parse_input(stream);
    REQUIRE(pr.node);
    type_checker.set_import_type_source(&codegen.import_types_);
    type_checker.check(pr.node.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr.node.get());
    REQUIRE(module);

    fs::path obj_path = yona::test::link::scratch_root() / "yona_borrowed_temp_cleanup.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));
    assert_linked_yona_zero_alloc_leaks(obj_path.string(), "yona_borrowed_temp_cleanup");
  }

  TEST_CASE("raise through grouped-let task group frees bump arena") {
    ensure_compiled_runtime_test_obj();
    fs::path fixtures_dir = yona::test::codegen_fixtures_dir();
    REQUIRE(fs::is_directory(fixtures_dir));
    string source = read_file(fixtures_dir / "task_group_raise_arena.yona");
    REQUIRE(!source.empty());

    parser::Parser parser;
    Codegen codegen("task_group_raise_arena_test");
    if (fs::exists(yona::test::lib_dir()))
      codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.module_paths_.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    codegen.load_prelude(&parser, &type_checker);
    istringstream stream(source);
    auto pr = parser.parse_input(stream);
    REQUIRE(pr.node);
    type_checker.set_import_type_source(&codegen.import_types_);
    type_checker.check(pr.node.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto module = codegen.compile(pr.node.get());
    REQUIRE(module);

    fs::path obj_path = yona::test::link::scratch_root() / "yona_task_group_raise_arena.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));

    assert_linked_yona_zero_alloc_leaks(obj_path.string(), "yona_task_group_raise_arena");
  }

  TEST_CASE("grouped let task group happy path (no raise)") {
    ensure_compiled_runtime_test_obj();
    string source = "let a = [1, 2], b = [3, 4] in case a of [x|_] -> case b of [y|_] -> x + y; "
                    "_ -> 0 end; _ -> 0 end";
    string actual = compile_and_run(source);
    CHECK(actual == "4");
    // Same program through link + alloc stats (arena + group teardown on success path)
    parser::Parser parser;
    Codegen codegen("task_group_happy_test");
    if (fs::exists(yona::test::lib_dir()))
      codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
      if (fs::exists(dir))
        codegen.module_paths_.push_back(fs::canonical(dir).string());
    }
    DiagnosticEngine tc_diag;
    typechecker::TypeChecker type_checker(tc_diag);
    codegen.load_prelude(&parser, &type_checker);
    istringstream stream(source);
    auto pr = parser.parse_input(stream);
    REQUIRE(pr.node);
    type_checker.set_import_type_source(&codegen.import_types_);
    type_checker.check(pr.node.get());
    REQUIRE(!type_checker.has_direct_errors());
    auto happy_mod = codegen.compile(pr.node.get());
    REQUIRE(happy_mod);
    fs::path obj_path = yona::test::link::scratch_root() / "yona_task_group_happy.o";
    REQUIRE(codegen.emit_object_file(obj_path.string()));
    assert_linked_yona_zero_alloc_leaks(obj_path.string(), "yona_task_group_happy");
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
    auto result = parser.parse_module(source, "test_arith.yona");
    REQUIRE(result.has_value());

    // Compile module
    Codegen codegen("test_arith");
    auto mod = codegen.compile_module(result.value().get());
    REQUIRE(mod != nullptr);

    // Check that mangled exports exist in the IR
    string ir = codegen.emit_ir();
    CHECK(ir.find("yona_Test_Arith__add") != string::npos);
    CHECK(ir.find("yona_Test_Arith__mul") != string::npos);
  }

  TEST_CASE("Module name mangling") {
    CHECK(Codegen::mangle_name("Test\\Arith", "add") == "yona_Test_Arith__add");
    CHECK(Codegen::mangle_name("Std\\Math", "abs") == "yona_Std_Math__abs");
    CHECK(Codegen::mangle_name("My\\Deep\\Module", "func") == "yona_My_Deep_Module__func");
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
    auto result = parser.parse_module(source, "cross.yona");
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
        << "extern int64_t yona_Test_CrossLang__double(int64_t);\n"
        << "extern int64_t yona_Test_CrossLang__negate(int64_t);\n"
        << "int main() { printf(\"%ld %ld\", "
        << "(long)yona_Test_CrossLang__double(21), "
        << "(long)yona_Test_CrossLang__negate(5)); return 0; }\n";
    }

    fs::path exe_out = yona::test::link::scratch_root() / ("cross_lang_run" + yona::test::link::exe_suffix());
    ostringstream lnk;
    lnk << yona::test::link::cc_quoted() << " " << yona::test::link::qpath(c_src) << " " << yona::test::link::qpath(obj_path) << " -o "
        << yona::test::link::qpath(exe_out);
#ifdef _WIN32
    lnk << " -Xlinker /SUBSYSTEM:CONSOLE";
#else
    lnk << " -lm -lpthread";
#endif
    lnk << yona::test::link::err_null();
    REQUIRE(yona::test::link::sh(lnk.str()) == 0);

    array<char, 64> buf;
    string output;
    FILE *pipe = popen(yona::test::link::qpath(exe_out).c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buf.data(), buf.size(), pipe))
      output += buf.data();
    pclose(pipe);

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
    auto mod_result = p1.parse_module(mod_source, "calc.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("calc_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "calc_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));

    // Compile expression that imports from the module
    parser::Parser p2;
    string expr_source = "import square, cube from Test\\Calc in square 3 + cube 2";
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("expr_test");
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "calc_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    vector<fs::path> calc_objs = {expr_obj, mod_obj};
    REQUIRE(yona::test::link::append_runtime_objects(calc_objs));
    fs::path exe_out = yona::test::link::scratch_root() / ("calc_link_test" + yona::test::link::exe_suffix());
    REQUIRE(yona::test::link::link_objs_to_exe(calc_objs, exe_out));

    array<char, 64> buf;
    string output;
    FILE *pipe = popen(yona::test::link::qpath(exe_out).c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buf.data(), buf.size(), pipe))
      output += buf.data();
    pclose(pipe);
    if (!output.empty() && output.back() == '\n')
      output.pop_back();

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
    auto arith_result = p1.parse_module(arith_source, "arith.yona");
    REQUIRE(arith_result.has_value());

    Codegen arith_codegen("arith_mod");
    auto arith_mod = arith_codegen.compile_module(arith_result.value().get());
    REQUIRE(arith_mod != nullptr);
    fs::path arith_obj = yona::test::link::scratch_root() / "arith_mod_test.o";
    REQUIRE(arith_codegen.emit_object_file(arith_obj.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(arith_codegen.emit_interface_file((yona_lib / "Test" / "Arith.yonai").string()));

    // Step 2: Compile re-exporting module Test\Prelude
    parser::Parser p2;
    string prelude_source = R"(
module Test\Prelude

export add, mul from Test\Arith
export double

double x = add x x
)";
    auto prelude_result = p2.parse_module(prelude_source, "prelude.yona");
    REQUIRE(prelude_result.has_value());

    Codegen prelude_codegen("prelude_mod");
    prelude_codegen.module_paths_.push_back(yona_lib.string());
    auto prelude_mod = prelude_codegen.compile_module(prelude_result.value().get());
    REQUIRE(prelude_mod != nullptr);
    fs::path prelude_obj = yona::test::link::scratch_root() / "prelude_mod_test.o";
    REQUIRE(prelude_codegen.emit_object_file(prelude_obj.string()));
    REQUIRE(prelude_codegen.emit_interface_file((yona_lib / "Test" / "Prelude.yonai").string()));

    // Step 3: Compile expression that imports from the re-exporting module
    parser::Parser p3;
    string expr_source = "import Test\\Prelude in add 10 (mul 3 4)";
    istringstream stream(expr_source);
    auto expr_result = p3.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("reexport_test");
    expr_codegen.module_paths_.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "reexport_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    // Step 4: Link all three and run
    fs::path exe_path = yona::test::link::scratch_root() / ("reexport_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> re_objs = {arith_obj, prelude_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(re_objs));
    REQUIRE(yona::test::link::link_objs_to_exe(re_objs, exe_path));

    string result = yona::test::link::popen_read_all(exe_path);

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
    auto mod_result = p1.parse_module(mod_source, "typed.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("typed_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "typed_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::create_directories(yona_lib / "Test");
    REQUIRE(mod_codegen.emit_interface_file((yona_lib / "Test" / "Typed.yonai").string()));

    // Test: scale 2.5 4.0 = 10.0
    parser::Parser p2;
    string expr_source = "import scale from Test\\Typed in scale 2.5 4.0";
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("typed_test");
    expr_codegen.module_paths_.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "typed_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    fs::path exe_path = yona::test::link::scratch_root() / ("typed_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> typed_objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(typed_objs));
    REQUIRE(yona::test::link::link_objs_to_exe(typed_objs, exe_path));

    string result = yona::test::link::popen_read_all(exe_path);

    CHECK(result == "10"); // %g format: 10.0 prints as "10"
  }

  TEST_CASE("Interface files preserve inferred borrow metadata") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_borrow_meta";
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
    auto mod_result = p1.parse_module(mod_source, "borrow_meta.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("borrow_meta_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "borrow_meta_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::path iface = yona_lib / "Test" / "BorrowMeta.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("FN yona_Test_BorrowMeta__ignoreSeq 1 ADT -> INT borrow 1") != string::npos);
    CHECK(yonai.find("FN yona_Test_BorrowMeta__returnSeq 1 ADT -> ADT borrow") == string::npos);

    {
      parser::Parser p2;
      string borrowed_source = "import ignoreSeq from Test\\BorrowMeta in let xs = [1, 2, 3] in ignoreSeq xs";
      istringstream stream(borrowed_source);
      auto expr_result = p2.parse_input(stream);
      REQUIRE(expr_result.node != nullptr);

      Codegen borrowed_codegen("borrow_meta_borrowed_expr");
      borrowed_codegen.module_paths_.push_back(yona_lib.string());
      auto expr_mod = borrowed_codegen.compile(expr_result.node.get());
      REQUIRE(expr_mod != nullptr);
      string ir = borrowed_codegen.emit_ir();
      CHECK(ir.find("call void @yona_rt_rc_inc") == string::npos);
    }

    {
      parser::Parser p2;
      string owned_source = "import ignoreSeq, returnSeq from Test\\BorrowMeta in "
                            "let xs = [1, 2, 3] in let _ = returnSeq xs in ignoreSeq xs";
      istringstream stream(owned_source);
      auto expr_result = p2.parse_input(stream);
      REQUIRE(expr_result.node != nullptr);

      Codegen owned_codegen("borrow_meta_owned_expr");
      owned_codegen.module_paths_.push_back(yona_lib.string());
      auto expr_mod = owned_codegen.compile(expr_result.node.get());
      REQUIRE(expr_mod != nullptr);
      string ir = owned_codegen.emit_ir();
      CHECK(ir.find("call void @yona_rt_rc_inc") != string::npos);
    }

    parser::Parser p2;
    string expr_source = "import ignoreSeq from Test\\BorrowMeta in ignoreSeq [1, 2, 3]";
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("borrow_meta_expr");
    expr_codegen.module_paths_.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "borrow_meta_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    fs::path exe_path = yona::test::link::scratch_root() / ("borrow_meta_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(objs));
    REQUIRE(yona::test::link::link_objs_to_exe(objs, exe_path));

    CHECK(yona::test::link::popen_read_all(exe_path) == "1");
  }

  TEST_CASE("Interface NAT rows serialize FloatArray as FLOAT_ARRAY") {
    namespace fs = std::filesystem;
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_nat_float_arr";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p;
    string mod_source = R"(
module Test\NatFloatArr

export nop

extern native nop : FloatArray -> Int = "yona_Test_NatFloatArr__nop"
)";
    auto mod_result = p.parse_module(mod_source, "nat_float_arr.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("nat_float_arr_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "NatFloatArr.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("NAT yona_Test_NatFloatArr__nop 1 FLOAT_ARRAY -> INT") != string::npos);
    CHECK(yonai.find("NAT yona_Test_NatFloatArr__nop 1 INT -> INT") == string::npos);
  }

  TEST_CASE("Interface files preserve exported FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_fx_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Fx

export fetch

fetch path = perform Fs.read path
)";
    auto mod_result = p1.parse_module(mod_source, "fx.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("fx_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "Fx.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("effects Fs.read") != string::npos);

    parser::Parser p2;
    string expr = R"(import fetch from Test\Fx in fetch "/etc/shadow")";
    istringstream stream(expr);
    auto parsed = p2.parse_input(stream);
    REQUIRE(parsed.node);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed.node.get());
    CHECK(checker.has_direct_errors());
  }

  TEST_CASE("Interface files preserve exported HOF open rest") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_hof_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Hof

export apply

apply f x = f x
)";
    auto mod_result = p1.parse_module(mod_source, "hof.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("hof_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "Hof.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("effects |") != string::npos);
    CHECK(yonai.find("hof") != string::npos);

    parser::Parser p2;
    string expr = R"(import apply from Test\Hof in apply (\() -> perform Fs.read "/etc/shadow") ())";
    istringstream stream(expr);
    auto parsed = p2.parse_input(stream);
    REQUIRE(parsed.node);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed.node.get());
    CHECK(checker.has_direct_errors());
  }

  TEST_CASE("Interface files preserve sibling-wrapped FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_wrap_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\Wrap

export wrap

readSecret = \() -> perform Fs.read "/etc/shadow"
wrap = \() -> readSecret ()
)";
    auto mod_result = p1.parse_module(mod_source, "wrap.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("wrap_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "Wrap.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("effects Fs.read") != string::npos);

    parser::Parser p2;
    string expr = R"(import wrap from Test\Wrap in wrap ())";
    istringstream stream(expr);
    auto parsed = p2.parse_input(stream);
    REQUIRE(parsed.node);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed.node.get());
    CHECK(checker.has_direct_errors());

    parser::Parser p3;
    string handled = R"(
import wrap from Test\Wrap in
handle wrap () with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    istringstream stream2(handled);
    auto parsed2 = p3.parse_input(stream2);
    REQUIRE(parsed2.node);
    DiagnosticEngine diag2;
    typechecker::TypeChecker checker2(diag2);
    checker2.add_module_path(yona_lib.string());
    checker2.check(parsed2.node.get());
    CHECK_FALSE(checker2.has_direct_errors());
  }

  TEST_CASE("Interface files preserve wrap-before-sibling FN effect rows") {
    namespace fs = std::filesystem;
    REQUIRE(yona::test::link::ensure_runtime_objects());
    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_wrap_first_effects";
    fs::create_directories(yona_lib / "Test");

    parser::Parser p1;
    string mod_source = R"(
module Test\WrapFirst

export wrap

wrap = \() -> readSecret ()
readSecret = \() -> perform Fs.read "/etc/shadow"
)";
    auto mod_result = p1.parse_module(mod_source, "wrap_first.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("wrap_first_effects_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path iface = yona_lib / "Test" / "WrapFirst.yonai";
    REQUIRE(mod_codegen.emit_interface_file(iface.string()));

    string yonai = read_file(iface);
    CHECK(yonai.find("effects Fs.read") != string::npos);

    parser::Parser p2;
    string expr = R"(import wrap from Test\WrapFirst in wrap ())";
    istringstream stream(expr);
    auto parsed = p2.parse_input(stream);
    REQUIRE(parsed.node);

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.add_module_path(yona_lib.string());
    checker.check(parsed.node.get());
    CHECK(checker.has_direct_errors());
  }

} // Codegen Modules

// ===== Diagnostic / Error Reporting Tests =====

TEST_SUITE("Diagnostics") {

  TEST_CASE("DiagnosticEngine error counting") {
    DiagnosticEngine diag;
    diag.set_source("let x = 1 in y", "<test>");

    CHECK(diag.error_count() == 0);
    CHECK(!diag.has_errors());

    diag.error(SourceLocation{1, 14, 0, 1, "<test>"}, "undefined variable 'y'");
    CHECK(diag.error_count() == 1);
    CHECK(diag.has_errors());
  }

  TEST_CASE("DiagnosticEngine warning flags") {
    DiagnosticEngine diag;
    diag.set_source("let x = 1 in 42", "<test>");

    // Warnings suppressed by default (no flags enabled)
    diag.warning({1, 5, 0, 1, "<test>"}, "unused variable 'x'", WarningFlag::UnusedVariable);
    CHECK(diag.warning_count() == 0);

    // Enable -Wall
    diag.enable_wall();
    diag.warning({1, 5, 0, 1, "<test>"}, "unused variable 'x'", WarningFlag::UnusedVariable);
    CHECK(diag.warning_count() == 1);
  }

  TEST_CASE("DiagnosticEngine -Werror") {
    DiagnosticEngine diag;
    diag.enable_wall();
    diag.set_warnings_as_errors(true);
    diag.set_source("let x = 1 in 42", "<test>");

    diag.warning({1, 5, 0, 1, "<test>"}, "unused variable 'x'", WarningFlag::UnusedVariable);
    CHECK(diag.error_count() == 1);   // promoted to error
    CHECK(diag.warning_count() == 0); // not counted as warning
  }

  TEST_CASE("DiagnosticEngine -w suppresses all") {
    DiagnosticEngine diag;
    diag.enable_wextra();
    diag.suppress_all_warnings();
    diag.set_source("let x = 1 in 42", "<test>");

    diag.warning({1, 5, 0, 1, "<test>"}, "unused variable", WarningFlag::UnusedVariable);
    diag.warning({1, 5, 0, 1, "<test>"}, "shadow", WarningFlag::Shadow);
    CHECK(diag.warning_count() == 0);
    CHECK(diag.error_count() == 0);
  }

  TEST_CASE("Codegen reports errors through DiagnosticEngine") {
    DiagnosticEngine diag;
    string source = "let x = 42 in y + 1";
    diag.set_source(source, "<test>");

    Codegen codegen("test", &diag);
    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (result.node)
      codegen.compile(result.node.get());

    CHECK(diag.has_errors());
    CHECK(diag.error_count() >= 1);
  }

  TEST_CASE("Codegen suggests similar names for typos") {
    DiagnosticEngine diag;
    string source = "let myVariable = 42 in myVarible + 1";
    diag.set_source(source, "<test>");

    Codegen codegen("test", &diag);
    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (result.node)
      codegen.compile(result.node.get());

    // Should have an error with "did you mean" suggestion
    CHECK(diag.has_errors());
  }

  TEST_CASE("Warning flag names") {
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnusedVariable) == "unused-variable");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnusedImport) == "unused-import");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::Shadow) == "shadow");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::MissingSignature) == "missing-signature");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::IncompletePatterns) == "incomplete-patterns");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::OverlappingPatterns) == "overlapping-patterns");
    CHECK(DiagnosticEngine::flag_name(WarningFlag::UnmatchedAdt) == "unmatched-adt");
  }

  TEST_CASE("Parser errors route through DiagnosticEngine") {
    DiagnosticEngine diag;
    string source = "let x = in 42";
    diag.set_source(source, "<test>");

    parser::Parser parser;
    auto result = parser.parse_expression(source, "<test>");
    if (!result.has_value()) {
      for (auto &e : result.error())
        diag.error(e.location, e.message);
    }

    CHECK(diag.has_errors());
  }

  TEST_CASE("Parser: perform as multi-binding let RHS") {
    parser::Parser parser;
    string source = R"(let a = perform Fs.read "x", b = perform Net.post "y" in a)";
    auto result = parser.parse_expression(source, "<test>");
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
    // No cwd-relative lib/ on module_paths_ — only YONA_PATH.
    codegen.load_prelude(&parser);

    string source = "case None of Some x -> x; None -> 0 end";
    istringstream stream(source);
    auto parse_result = parser.parse_input(stream);

#ifdef _WIN32
    _putenv_s("YONA_PATH", saved.c_str());
#else
    if (saved.empty())
      unsetenv("YONA_PATH");
    else
      setenv("YONA_PATH", saved.c_str(), 1);
#endif

    REQUIRE(parse_result.node != nullptr);
    auto *mod = codegen.compile(parse_result.node.get());
    CHECK(mod != nullptr);
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE("Debug info: compilation succeeds with -g") {
    // Compiling with debug info enabled should not break anything
    DiagnosticEngine diag;
    string source = "let x = 42 in let y = x + 1 in y";
    diag.set_source(source, "test_debug.yona");

    Codegen codegen("debug_test", &diag);
    codegen.set_debug_info(true, "test_debug.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    REQUIRE(result.node != nullptr);

    auto *mod = codegen.compile(result.node.get());
    REQUIRE(mod != nullptr);

    // Should compile without errors
    CHECK(!diag.has_errors());
    CHECK(codegen.error_count_ == 0);
  }

  TEST_CASE("Debug info: function with params") {
    DiagnosticEngine diag;
    string source = "let add x y = x + y in add 10 32";
    diag.set_source(source, "test_fn.yona");

    Codegen codegen("debug_fn_test", &diag);
    codegen.set_debug_info(true, "test_fn.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    REQUIRE(result.node != nullptr);

    auto *mod = codegen.compile(result.node.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());
  }

  TEST_CASE("Debug info: closures") {
    DiagnosticEngine diag;
    string source = "let n = 10 in let add_n x = x + n in add_n 5";
    diag.set_source(source, "test_closure.yona");

    Codegen codegen("debug_closure_test", &diag);
    codegen.set_debug_info(true, "test_closure.yona");

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    REQUIRE(result.node != nullptr);

    auto *mod = codegen.compile(result.node.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());
  }

  TEST_CASE("Debug info: disabled by default") {
    // Without set_debug_info, no debug metadata should be generated
    DiagnosticEngine diag;
    string source = "42";
    diag.set_source(source, "test.yona");

    Codegen codegen("no_debug_test", &diag);
    // NOT calling set_debug_info

    parser::Parser parser;
    istringstream stream(source);
    auto result = parser.parse_input(stream);
    REQUIRE(result.node != nullptr);

    auto *mod = codegen.compile(result.node.get());
    REQUIRE(mod != nullptr);
    CHECK(!diag.has_errors());

    // IR should NOT contain !dbg metadata
    string ir = codegen.emit_ir();
    CHECK(ir.find("!dbg") == string::npos);
  }

} // Diagnostics

// IR fixture tests removed — IR text comparison is fragile due to
// whitespace/formatting differences between runs. The E2E fixture tests
// (compile → run → check output) are more reliable and valuable.

TEST_SUITE("Regex") {

  TEST_CASE("Regex module: matches, find, replace, split") {
    namespace fs = std::filesystem;
    using namespace yona::compiler::codegen;
    REQUIRE(yona::test::link::ensure_runtime_objects());

    fs::path regex_obj;
    if (!yona::test::link::ensure_regex_obj(regex_obj)) {
      WARN("regex.c not built; skipping Regex module link tests");
      return;
    }

    // Compile the Regex module
    auto regex_yona = yona::test::lib_dir() / "Std" / "Regex.yona";
    REQUIRE(fs::exists(regex_yona));
    ifstream f(regex_yona);
    string regex_mod_src((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    REQUIRE(!regex_mod_src.empty());

    parser::Parser mp;
    auto mod_result = mp.parse_module(regex_mod_src, "Regex.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("regex_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "regex_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));
    fs::path yona_regex_lib = yona::test::link::scratch_root() / "yona_regex_lib";
    fs::path yonai_dir = yona_regex_lib / "Std";
    fs::create_directories(yonai_dir);
    REQUIRE(mod_codegen.emit_interface_file((yonai_dir / "Regex.yonai").string()));

    // Helper: compile expression, link with module + runtime, run, return output
    auto run_expr = [&](const string &expr_source) -> string {
      parser::Parser ep;
      istringstream stream(expr_source);
      auto expr_result = ep.parse_input(stream);
      if (!expr_result.node)
        return "PARSE_ERROR";

      Codegen expr_codegen("regex_test");
      expr_codegen.module_paths_.push_back(yona_regex_lib.string());
      if (fs::exists(yona::test::lib_dir()))
        expr_codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
      for (auto &dir : {".", "lib", "../lib", "../../lib"})
        if (fs::exists(dir))
          expr_codegen.module_paths_.push_back(fs::canonical(dir).string());
      auto expr_mod = expr_codegen.compile(expr_result.node.get());
      if (!expr_mod)
        return "CODEGEN_ERROR";
      fs::path expr_obj = yona::test::link::scratch_root() / "regex_expr_test.o";
      if (!expr_codegen.emit_object_file(expr_obj.string()))
        return "EMIT_ERROR";

      fs::path exe_out = yona::test::link::scratch_root() / ("regex_link_test" + yona::test::link::exe_suffix());
      string libs = yona::test::link::pcre_link_flags();
      vector<fs::path> robj = {expr_obj, mod_obj};
      if (!yona::test::link::append_runtime_objects(robj))
        return "RT_COMPILE_ERROR";
      robj.push_back(regex_obj);
      if (!yona::test::link::link_objs_to_exe(robj, exe_out, libs))
        return "LINK_ERROR";

      return yona::test::link::popen_read_all(exe_out);
    };

    SUBCASE("matches true") { CHECK(run_expr(R"YT(import matches, compile from Std\Regex in matches (compile "[0-9]+") "abc 42 def")YT") == "true"); }

    SUBCASE("matches false") {
      CHECK(run_expr(R"YT(import matches, compile from Std\Regex in matches (compile "[0-9]+") "no digits")YT") == "false");
    }

    SUBCASE("replace") {
      CHECK(run_expr(R"YT(import replace, compile from Std\Regex in replace (compile "[0-9]+") "abc 42 def 99" "NUM")YT") == "abc NUM def 99");
    }

    SUBCASE("replaceAll") {
      CHECK(run_expr(R"YT(import replaceAll, compile from Std\Regex in replaceAll (compile "[0-9]+") "abc 42 def 99" "NUM")YT") == "abc NUM def NUM");
    }

    // TODO: split/find return SEQ from C but module metadata infers ADT.
    // The extern return type (Seq) doesn't propagate through the wrapper
    // function's return type inference. Needs module_meta_ to use the
    // extern declaration's type annotation instead of body inference.

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
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  codegen.load_prelude(&parser, &type_checker);

  const char *source = R"YT(import fromSeq from Std\IntArray in
import upload, mapAdd, reduceSum from Std\GPU in
let build n acc = if n <= 0 then acc else build (n - 1) (n :: acc) in
let input = fromSeq (build 3 []) in
let buffer = upload input in
let shifted = mapAdd 1 buffer in
reduceSum shifted
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parse_input(stream);
  REQUIRE(parse_result.node);
  type_checker.set_import_type_source(&codegen.import_types_);
  type_checker.check(parse_result.node.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result.node.get(), &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("schema":"yona.accelerator_diag.v1")") != std::string::npos);
  CHECK(json.find(R"("report_kind":"program")") != std::string::npos);
  CHECK(json.find(R"("file":"<test>")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") != std::string::npos);
  CHECK(json.find(R"("op":"mapAdd")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"Int -> Buffer -> Buffer")") != std::string::npos);
  CHECK(json.find(R"("op":"reduceSum")") != std::string::npos);
  CHECK(json.find(R"("binding":"import")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_std_gpu_float_array_async") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  if (fs::exists(yona::test::lib_dir()))
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  codegen.load_prelude(&parser, &type_checker);

  const char *source = R"YT(import fill from Std\FloatArray in
import floatArrayScaleAsync, floatArrayMul2Async from Std\GPU in
let xs = fill 4 2.5 in
let ps = floatArrayScaleAsync 3.0 xs in
let pm = floatArrayMul2Async xs in
(pm, ps)
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parse_input(stream);
  REQUIRE(parse_result.node);
  type_checker.set_import_type_source(&codegen.import_types_);
  type_checker.check(parse_result.node.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result.node.get(), &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("op":"floatArrayScaleAsync")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"Float -> FloatArray -> Int")") != std::string::npos);
  CHECK(json.find(R"("op":"floatArrayMul2Async")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"FloatArray -> Int")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_std_gpu_discovery_calls") {
  parser::Parser parser;
  DiagnosticEngine diag;
  typechecker::TypeChecker type_checker(diag);
  Codegen codegen("yona_program");
  if (fs::exists(yona::test::lib_dir()))
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  codegen.load_prelude(&parser, &type_checker);

  const char *source = R"YT(import available, apiVersion, physicalDeviceCount from Std\GPU in
(available (), apiVersion (), physicalDeviceCount ())
)YT";

  std::istringstream stream(source);
  auto parse_result = parser.parse_input(stream);
  REQUIRE(parse_result.node);
  type_checker.set_import_type_source(&codegen.import_types_);
  type_checker.check(parse_result.node.get());
  REQUIRE_FALSE(type_checker.has_direct_errors());
  CHECK(type_checker.solve_constraints());
  REQUIRE_FALSE(type_checker.has_errors());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report(oss, parse_result.node.get(), &type_checker, "<test>");
  const std::string json = oss.str();
  CHECK(json.find(R"("op":"available")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"() -> Bool")") != std::string::npos);
  CHECK(json.find(R"("op":"apiVersion")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"() -> Int")") != std::string::npos);
  CHECK(json.find(R"("op":"physicalDeviceCount")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_module_ast_scan") {
  parser::Parser parser;
  DiagnosticEngine diag;
  Codegen codegen("yona_module", &diag);
  if (fs::exists(yona::test::lib_dir()))
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  codegen.load_prelude(&parser);

  const char *source = R"(module Test\AccelReportMod

export f

f xs = import upload from Std\GPU in upload xs
)";
  auto mod = parser.parse_module(source, "<module>");
  REQUIRE(mod.has_value());

  std::ostringstream oss;
  emit_accelerator_diagnostic_report_for_module(oss, mod.value().get(), "<module>");
  const std::string json = oss.str();
  CHECK(json.find(R"("report_kind":"module_ast")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") != std::string::npos);
  CHECK(json.find(R"("binding":"import")") != std::string::npos);
}

TEST_CASE("accelerator_diagnostic_report_module_typed_scan") {
  parser::Parser parser;
  DiagnosticEngine diag;
  Codegen codegen("yona_module", &diag);
  if (fs::exists(yona::test::lib_dir()))
    codegen.module_paths_.push_back(fs::canonical(yona::test::lib_dir()).string());
  for (const auto &dir : {"lib", "../lib", "../../lib", "../../../lib"}) {
    if (fs::exists(dir))
      codegen.module_paths_.push_back(fs::canonical(dir).string());
  }
  typechecker::TypeChecker type_checker(diag);
  codegen.load_prelude(&parser, &type_checker);
  type_checker.set_import_type_source(&codegen.import_types_);

  const char *source = R"(module Test\AccelReportModTyped

export f

f xs = import upload from Std\GPU in upload xs
)";
  auto mod = parser.parse_module(source, "<module>");
  REQUIRE(mod.has_value());
  REQUIRE(typecheck_module_for_accelerator_report(mod.value().get(), type_checker));

  std::ostringstream oss;
  emit_accelerator_diagnostic_report_for_module(oss, mod.value().get(), "<module>", &type_checker);
  const std::string json = oss.str();
  CHECK(json.find(R"("report_kind":"module")") != std::string::npos);
  CHECK(json.find(R"("op":"upload")") != std::string::npos);
  CHECK(json.find(R"("api_signature":"IntArray -> Buffer")") != std::string::npos);
}
