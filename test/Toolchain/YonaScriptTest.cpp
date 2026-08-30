#include "Support/RepoPaths.h"
#include "Toolchain/YonaLinkUtil.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static fs::path bin_dir() {
#ifdef YONA_BINARY_DIR
  return fs::path(YONA_BINARY_DIR);
#else
  return fs::current_path();
#endif
}

static std::string exe_name(const char *stem) {
  return std::string(stem) + yona::test::link::exe_suffix();
}

static fs::path tool(const char *stem) { return bin_dir() / exe_name(stem); }

struct CmdResult {
  int status = -1;
  std::string out;
};

static CmdResult runProcess(const fs::path &Executable,
                            const std::vector<std::string> &Arguments = {},
                            yona::support::ProcessOptions Options = {}) {
  Options.CaptureStdout = true;
  if (!Options.CaptureStderr)
    Options.SuppressStderr = true;
  const auto Result =
      yona::support::executeProcess(Executable, Arguments, Options);
  CmdResult r;
  r.status = Result.ExecutionFailed ? -1 : Result.ExitCode;
  r.out = Result.StandardOutput;
  if (Options.CaptureStderr)
    r.out += Result.StandardError;
  while (!r.out.empty() && (r.out.back() == '\n' || r.out.back() == '\r'))
    r.out.pop_back();
  return r;
}

static CmdResult run_yona(const std::vector<std::string> &args,
                          const std::string &stdin_text = "") {
  yona::support::ProcessOptions Options;
  if (!stdin_text.empty())
    Options.StandardInput = stdin_text;
  return runProcess(tool("yona"), args, Options);
}

#ifndef _WIN32
TEST_CASE("format script fails clearly when clang-format is unavailable") {
  const auto script = yona::test::repo_root() / "scripts" / "format.sh";
  const auto result =
      runProcess("/bin/sh", {script.string()},
                 {.CaptureStderr = true,
                  .EnvironmentOverrides = {{"PATH", "/nonexistent"}}});
  CHECK(result.status != 0);
  CHECK(result.out.find("clang-format") != std::string::npos);
  CHECK(result.out.find("Done") == std::string::npos);
}
#endif

static fs::path write_temp_yona(const std::string &stem,
                                const std::string &body) {
  fs::path p = yona::test::link::scratch_root() / (stem + ".yona");
  std::ofstream o(p);
  o << body;
  return p;
}

TEST_CASE("yona runner is built") {
  REQUIRE(fs::exists(tool("yona")));
  REQUIRE(fs::exists(tool("yonac")));
  REQUIRE(fs::exists(tool("yona-repl")));
}

TEST_CASE("yona runs a file") {
  auto src = write_temp_yona("script_literal", "1 + 2\n");
  auto r = run_yona({src.string()});
  CHECK(r.status == 0);
  CHECK(r.out == "3");
}

#ifndef _WIN32
TEST_CASE("yona shebang script is executable") {
  auto src = write_temp_yona("script_shebang", "#!/usr/bin/env yona\n1 + 2\n");
  fs::permissions(src, fs::perms::owner_exec, fs::perm_options::add);
  auto r = run_yona({src.string()});
  CHECK(r.status == 0);
  CHECK(r.out == "3");
}
#endif

TEST_CASE("yona getArgs uses the script path") {
  auto src = write_temp_yona("script_args",
                             "import getArgs from Std\\Process in\n"
                             "case getArgs of [_|t] -> t; [] -> [] end\n");
  auto r = run_yona({src.string(), "foo", "bar"});
  CHECK(r.status == 0);
  CHECK(r.out == "[foo, bar]");
}

TEST_CASE("yona compiles piped stdin") {
  auto r = run_yona({}, "1 + 2\n");
  CHECK(r.status == 0);
  CHECK(r.out == "3");
}

TEST_CASE("yona -e compiles and runs an expression") {
  auto r = run_yona({"-e", "1 + 2"});
  CHECK(r.status == 0);
  CHECK(r.out == "3");
}

TEST_CASE("yona -e getArgs argv0 is -e") {
  auto r =
      run_yona({"-e", "import getArgs from Std\\Process in getArgs", "foo"});
  CHECK(r.status == 0);
  CHECK(r.out == "[-e, foo]");
}

TEST_CASE("yona rejects a module file") {
  auto src = write_temp_yona("script_module", "module Foo\nexport x\nx = 1\n");
  auto r = run_yona({src.string()});
  CHECK(r.status != 0);
}

TEST_CASE("yona missing file is non-zero") {
  auto r = run_yona(
      {(yona::test::link::scratch_root() / "no_such_script.yona").string()});
  CHECK(r.status != 0);
}

TEST_CASE("yona unknown flag is non-zero") {
  auto r = run_yona({"--not-a-real-flag"});
  CHECK(r.status == 2);
}

TEST_CASE("yona --version matches yonac --version") {
  auto yona_v = runProcess(tool("yona"), {"--version"});
  auto yonac_v = runProcess(tool("yonac"), {"--version"});
  CHECK(yona_v.status == 0);
  CHECK(yonac_v.status == 0);
  CHECK(yona_v.out == yonac_v.out);
  CHECK(!yona_v.out.empty());
}

TEST_CASE("yonac - reads stdin") {
  fs::path out = yona::test::link::scratch_root() /
                 ("yonac_stdin" + yona::test::link::exe_suffix());
  auto compile = runProcess(
      tool("yonac"), {"--sysroot", bin_dir().string(), "-", "-o", out.string()},
      {.StandardInput = "1 + 2"});
  CHECK(compile.status == 0);
  REQUIRE(fs::exists(out));
  auto run = runProcess(out);
  CHECK(run.status == 0);
  CHECK(run.out == "3");
}

TEST_CASE("yonac -e is rejected") {
  auto r = runProcess(tool("yonac"), {"-e", "1 + 2"});
  CHECK(r.status != 0);
}

TEST_CASE("yonac rejects module codegen errors before emitting artifacts") {
  auto src = write_temp_yona(
      "module_codegen_error",
      "module Test\\CodegenError\n"
      "export broken\n"
      // The trait method is valid during generic type inference, but there
      // is deliberately no Semigroup Int instance for the exporter's
      // placeholder ABI.  This reaches the module-codegen error path.
      "broken value = combine value value\n");
  auto object = yona::test::link::scratch_root() / "module_codegen_error.o";
  auto interface =
      yona::test::link::scratch_root() / "module_codegen_error.yonai";
  std::error_code ec;
  fs::remove(object, ec);
  fs::remove(interface, ec);

  auto result = runProcess(tool("yonac"),
                           {"--sysroot", bin_dir().string(), "-I",
                            yona::test::lib_dir().string(), "--emit-obj", "-o",
                            object.string(), src.string()},
                           {.CaptureStderr = true});

  CHECK(result.status != 0);
#ifndef _WIN32
  CHECK(result.status != 139);
#endif
  CHECK(result.out.find("undefined function 'combine'") != std::string::npos);
  CHECK(result.out.find("Module verification failed") == std::string::npos);
  CHECK_FALSE(fs::exists(object));
  CHECK_FALSE(fs::exists(interface));
}

static CmdResult run_yonac_ir(const fs::path &src,
                              const std::vector<std::string> &extra = {}) {
  std::vector<std::string> Arguments = {"--sysroot", bin_dir().string(), "-I",
                                        yona::test::lib_dir().string(),
                                        "--emit-ir"};
  Arguments.insert(Arguments.end(), extra.begin(), extra.end());
  Arguments.push_back(src.string());
  return runProcess(tool("yonac"), Arguments, {.CaptureStderr = true});
}

static CmdResult run_yonac_typed_core(const fs::path &src) {
  return runProcess(tool("yonac"),
                    {"--sysroot", bin_dir().string(), "-I",
                     yona::test::lib_dir().string(), "--emit-typed-core",
                     src.string()},
                    {.CaptureStderr = true});
}

TEST_CASE("yonac fails E0500 on unproven head") {
  auto src =
      write_temp_yona("e0500_head", "import head from Std\\List in let f x = x "
                                    "in let xs = f [1, 2] in head xs\n");
  auto r = run_yonac_ir(src);
  CHECK(r.status != 0);
  CHECK(r.out.find("E0500") != std::string::npos);
}

TEST_CASE("yonac --Wno-refinement allows unproven head") {
  auto src = write_temp_yona("e0500_head_allow",
                             "import head from Std\\List in let f x = x in let "
                             "xs = f [1, 2] in head xs\n");
  auto r = run_yonac_ir(src, {"--Wno-refinement"});
  CHECK(r.status == 0);
}

TEST_CASE("yonac fails E0600 on use-after-consume") {
  auto src = write_temp_yona("e0600_uac",
                             "let makeHandle x = Linear x, conn = makeHandle "
                             "0, conn2 = conn, conn3 = conn in conn3\n");
  auto r = run_yonac_ir(src);
  CHECK(r.status != 0);
  CHECK(r.out.find("E0600") != std::string::npos);
}

TEST_CASE("yonac fails E0600 in a module function") {
  auto src = write_temp_yona("e0600_mod",
                             "module Test\\LinFail\n\nexport bad\n\n"
                             "bad x =\n"
                             "  let makeHandle y = Linear y, conn = makeHandle "
                             "x, conn2 = conn, conn3 = conn in conn3\n");
  auto r = run_yonac_ir(src);
  CHECK(r.status != 0);
  CHECK(r.out.find("E0600") != std::string::npos);
}

TEST_CASE("yonac leak warning is E0602 not unhandled-effect") {
  auto src = write_temp_yona("e0602_leak", "let conn = Linear 0 in 42\n");
  auto r = run_yonac_ir(src);
  CHECK(r.status == 0);
  CHECK(r.out.find("E0602") != std::string::npos);
  CHECK(r.out.find("unhandled-effect") == std::string::npos);
}

TEST_CASE("yonac --Wno-linear permits a linear use-after-consume") {
  auto src = write_temp_yona("e0600_uac_allow",
                             "let makeHandle x = Linear x, conn = makeHandle "
                             "0, conn2 = conn, conn3 = conn in conn3\n");
  auto r = run_yonac_ir(src, {"--Wno-linear"});
  CHECK(r.status == 0);
  CHECK(r.out.find("E0600") == std::string::npos);
}

TEST_CASE("yonac --Wincomplete-patterns warns without failing") {
  auto src = write_temp_yona("incomplete_patterns",
                             "case Some 1 of Some x -> x end\n");
  auto r = run_yonac_ir(src, {"--Wincomplete-patterns"});
  CHECK(r.status == 0);
  CHECK(r.out.find("Wincomplete-patterns") != std::string::npos);
  CHECK(r.out.find("None") != std::string::npos);
}

TEST_CASE("yonac --Werror promotes incomplete pattern warnings") {
  auto src = write_temp_yona("incomplete_patterns_werror",
                             "case Some 1 of Some x -> x end\n");
  auto r = run_yonac_ir(src, {"--Werror", "--Wincomplete-patterns"});
  CHECK(r.status != 0);
  CHECK(r.out.find("Wincomplete-patterns") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free accepts a pure expression") {
  auto src =
      write_temp_yona("effect_free_pure", "let add x y = x + y in add 20 22\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status == 0);
  CHECK(r.out.find("E0203") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects incomplete finite ADT cases") {
  auto src = write_temp_yona("effect_free_incomplete_case",
                             "case Some 1 of Some x -> x end\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
  CHECK(r.out.find("None") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free accepts wildcard finite ADT cases") {
  auto src = write_temp_yona("effect_free_wildcard_case",
                             "case Some 1 of Some x -> x; _ -> 0 end\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status == 0);
  CHECK(r.out.find("E0203") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects guarded finite ADT cases") {
  auto src = write_temp_yona("effect_free_guarded_case",
                             "case Some 1 of Some x if x > 0 -> x end\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
  CHECK(r.out.find("None") != std::string::npos);
  CHECK(r.out.find("Some") != std::string::npos);
}

TEST_CASE("yonac warns for overlapping patterns and rejects incomplete Bool in "
          "strict mode") {
  auto overlap = write_temp_yona("overlapping_patterns",
                                 "case true of _ -> 1; true -> 2 end\n");
  auto warning = run_yonac_ir(overlap, {"--Woverlapping-patterns"});
  CHECK(warning.status == 0);
  CHECK(warning.out.find(
            "earlier unguarded arms already cover every value it can match") !=
        std::string::npos);
  auto wall = run_yonac_ir(overlap, {"--Wall"});
  CHECK(wall.status == 0);
  CHECK(wall.out.find("unreachable pattern") != std::string::npos);
  auto werror = run_yonac_ir(overlap, {"--Woverlapping-patterns", "--Werror"});
  CHECK(werror.status != 0);

  auto incomplete =
      write_temp_yona("incomplete_bool", "case true of true -> 1 end\n");
  auto strict = run_yonac_ir(incomplete, {"--require-effect-free"});
  CHECK(strict.status != 0);
  CHECK(strict.out.find("E0203") != std::string::npos);
  CHECK(strict.out.find("False") != std::string::npos);
}

TEST_CASE(
    "yonac --require-effect-free rejects incomplete module finite ADT cases") {
  auto src = write_temp_yona("effect_free_module_incomplete_case",
                             "module Test\\StrictCase\n"
                             "export type Choice\n"
                             "export choose\n"
                             "type Choice = First Int | Second Int\n"
                             "choose value = case value of First x -> x end\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
  CHECK(r.out.find("Second") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free requires structural recursion") {
  auto structural = write_temp_yona("effect_free_structural_recursion",
                                    "module Test\\StructuralRecursion\n"
                                    "export sum\n"
                                    "type Nat = Zero | Succ Nat\n"
                                    "sum n = case n of Zero -> 0; Succ rest -> "
                                    "let next = rest in sum next end\n");
  auto structural_result = run_yonac_ir(structural, {"--require-effect-free"});
  CHECK(structural_result.status == 0);

  auto direct = write_temp_yona("effect_free_direct_recursion",
                                "module Test\\DirectRecursion\n"
                                "export loop\n"
                                "loop n = loop n\n");
  auto direct_result = run_yonac_ir(direct, {"--require-effect-free"});
  CHECK(direct_result.status != 0);
  CHECK(direct_result.out.find("E0203") != std::string::npos);

  auto mutual = write_temp_yona("effect_free_mutual_recursion",
                                "module Test\\MutualRecursion\n"
                                "export even\n"
                                "even n = odd n\n"
                                "odd n = even n\n");
  auto mutual_result = run_yonac_ir(mutual, {"--require-effect-free"});
  CHECK(mutual_result.status != 0);
  CHECK(mutual_result.out.find("E0203") != std::string::npos);
  CHECK(mutual_result.out.find("mutual recursion") != std::string::npos);

  auto mutual_structural = write_temp_yona(
      "effect_free_mutual_structural",
      "module Test\\MutualStructural\n"
      "export even\nexport odd\n"
      "type Nat = Zero | Succ Nat\n"
      "even n = case n of Zero -> true; Succ rest -> odd rest end\n"
      "odd n = case n of Zero -> false; Succ rest -> even rest end\n");
  auto mutual_structural_result =
      run_yonac_ir(mutual_structural, {"--require-effect-free"});
  CHECK(mutual_structural_result.status == 0);
  CHECK(mutual_structural_result.out.find("E0203") == std::string::npos);

  auto lexical =
      write_temp_yona("effect_free_lexical_structural",
                      "module Test\\LexicalStructural\n"
                      "export walk\n"
                      "type Nat = Zero | Succ Nat\n"
                      "walk stable changing = case changing of Zero -> stable; "
                      "Succ rest -> walk stable rest end\n");
  CHECK(run_yonac_ir(lexical, {"--require-effect-free"}).status == 0);

  auto incompatible = write_temp_yona(
      "effect_free_incompatible_cycle",
      "module Test\\IncompatibleCycle\n"
      "export left\nexport right\n"
      "type Nat = Zero | Succ Nat\n"
      "left a b = case a of Zero -> b; Succ rest -> right b rest end\n"
      "right a b = case a of Zero -> b; Succ rest -> left b rest end\n");
  auto incompatible_result =
      run_yonac_ir(incompatible, {"--require-effect-free"});
  CHECK(incompatible_result.status != 0);
  CHECK(incompatible_result.out.find("E0203") != std::string::npos);
  CHECK(incompatible_result.out.find("recursive component 'left, right'") !=
        std::string::npos);
  CHECK(incompatible_result.out.find("left -> right") != std::string::npos);
  CHECK(incompatible_result.out.find("mutual recursion has no provable "
                                     "lexicographic structural descent") !=
        std::string::npos);
  CHECK(incompatible_result.out.find("Repair:") != std::string::npos);
  CHECK(incompatible_result.out.find("Succ rest -> loop rest") !=
        std::string::npos);

  auto numeric = write_temp_yona(
      "effect_free_numeric_recursion",
      "module Test\\NumericRecursion\nexport loop\nloop n = loop (n - 1)\n");
  auto numeric_result = run_yonac_ir(numeric, {"--require-effect-free"});
  CHECK(numeric_result.status != 0);
  CHECK(numeric_result.out.find("E0203") != std::string::npos);
  CHECK(numeric_result.out.find("recursive component 'loop'") !=
        std::string::npos);
  CHECK(numeric_result.out.find("loop -> loop") != std::string::npos);
  CHECK(
      numeric_result.out.find(
          "recursive call has no provable lexicographic structural descent") !=
      std::string::npos);
  CHECK(numeric_result.out.find("Repair:") != std::string::npos);
  CHECK(numeric_result.out.find("Succ rest -> loop rest") != std::string::npos);

  auto callable_alias = write_temp_yona("effect_free_callable_alias_recursion",
                                        "module Test\\CallableAliasRecursion\n"
                                        "export loop\n"
                                        "loop n = let f = loop in f n\n");
  auto callable_alias_result =
      run_yonac_ir(callable_alias, {"--require-effect-free"});
  CHECK(callable_alias_result.status != 0);
  CHECK(callable_alias_result.out.find("E0203") != std::string::npos);

  auto lexical_capture =
      write_temp_yona("effect_free_lexical_capture",
                      "module Test\\LexicalCapture\n"
                      "export outer\n"
                      "f n = outer n\n"
                      "outer n = let f = identity in let g x = f x in g n\n");
  auto lexical_capture_result =
      run_yonac_ir(lexical_capture, {"--require-effect-free"});
  CHECK(lexical_capture_result.status == 0);
  CHECK(lexical_capture_result.out.find("E0203") == std::string::npos);

  auto guarded_function =
      write_temp_yona("effect_free_guarded_function_recursion",
                      "module Test\\GuardedFunctionRecursion\n"
                      "export loop\n"
                      "type Nat = Zero | Succ Nat\n"
                      "loop (Succ rest) = if true -> loop rest\n");
  auto guarded_function_result =
      run_yonac_ir(guarded_function, {"--require-effect-free"});
  CHECK(guarded_function_result.status != 0);
  CHECK(guarded_function_result.out.find("E0203") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free explains incompatible recursive arity") {
  auto src = write_temp_yona("effect_free_incompatible_arity",
                             "module Test\\IncompatibleArity\n"
                             "export left\nexport right\n"
                             "left n = right n n\n"
                             "right a b = left a\n");
  auto result = run_yonac_ir(src, {"--require-effect-free"});

  CHECK(result.status != 0);
  CHECK(result.out.find("E0203") != std::string::npos);
  CHECK(result.out.find("recursive component 'left, right'") !=
        std::string::npos);
  CHECK(result.out.find("left -> right") != std::string::npos);
  CHECK(
      result.out.find("recursive component members have incompatible arity") !=
      std::string::npos);
  CHECK(result.out.find("same number of parameters") != std::string::npos);
  CHECK(result.out.find("reshape the recursive component") !=
        std::string::npos);
  CHECK(result.out.find("Succ rest -> loop rest") == std::string::npos);
  CHECK(result.out.find("constructor field") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free help states its conservative scope") {
  auto result = runProcess(tool("yonac"), {"--help"}, {.CaptureStderr = true});

  CHECK(result.status == 0);
  CHECK(result.out.find("closed empty effect rows") != std::string::npos);
  CHECK(result.out.find("finite case coverage") != std::string::npos);
  CHECK(result.out.find("local recursive") != std::string::npos);
  CHECK(result.out.find("SCCs (not a global termination proof)") !=
        std::string::npos);
  CHECK(result.out.find("not a global termination proof") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects imported functions without "
          "effect rows") {
  fs::path modules = yona::test::link::scratch_root() / "unknown_effect_rows";
  fs::create_directories(modules / "Test");
  {
    std::ofstream iface(modules / "Test" / "Unknown.yonai");
    iface << "MODULE Test\\Unknown\nFN f 0 -> INT\n";
  }
  auto src = write_temp_yona("effect_free_unknown_import",
                             "import f from Test\\Unknown in f\n");
  auto r = run_yonac_ir(src, {"--require-effect-free", "-I", modules.string()});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
}

TEST_CASE(
    "yonac selective imports preserve package-qualified module separators") {
  fs::path modules =
      yona::test::link::scratch_root() / "package_qualified_import";
  fs::create_directories(modules / "Foo");
  {
    std::ofstream iface(modules / "Foo" / "Bar.yonai");
    iface << "MODULE Foo\\Bar\nFN identity 1 INT -> INT effects -\n";
  }
  auto src =
      write_temp_yona("package_qualified_import",
                      "module Test\\PackageQualifiedImport\n"
                      "export call\n"
                      "call n = import identity from Foo\\Bar in identity n\n");
  auto r = run_yonac_ir(src, {"--require-effect-free", "-I", modules.string()});
  CHECK(r.status == 0);
  CHECK(r.out.find("E0203") == std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects imported open rows through "
          "value aliases") {
  auto src = write_temp_yona("effect_free_imported_open_value_alias",
                             "module Test\\ImportedOpenValueAlias\n"
                             "export forward\n"
                             "forward a b = get a b\n"
                             "get = import gcd from Std\\Math in gcd\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
  CHECK(r.out.find("closed empty effect row") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free preserves later imported open rows in "
          "mutual SCCs") {
  auto src = write_temp_yona("effect_free_mutual_later_imported_open_row",
                             "module Test\\MutualImportedBranch\n"
                             "export even\nexport odd\n"
                             "type Nat = Zero | Succ Nat\n"
                             "even n = case n of Succ rest -> odd rest; Zero "
                             "-> import gcd from Std\\Math in gcd 1 1 end\n"
                             "odd n = case n of Succ rest -> even rest; Zero "
                             "-> import gcd from Std\\Math in gcd 1 1 end\n");
  auto r = run_yonac_ir(src, {"--require-effect-free"});
  CHECK(r.status != 0);
  CHECK(r.out.find("E0203") != std::string::npos);
  CHECK(r.out.find("closed empty effect row") != std::string::npos);
}

TEST_CASE("yonac module dependencies respect whole-module import bindings") {
  auto src =
      write_temp_yona("module_dependency_wildcard_import_shadow",
                      "module Test\\WildcardShadow\n"
                      "export both\n"
                      "apply = (outer identity 1, outer identity \"two\")\n"
                      "outer f x = import Std\\Function in apply x f\n"
                      "both = apply\n");
  auto r = run_yonac_ir(src);
  CHECK(r.status == 0);
  CHECK(r.out.find("E0100") == std::string::npos);
}

TEST_CASE(
    "yonac keeps callbacks returned by recursive calls effect-polymorphic") {
  auto src = write_temp_yona("preliminary_independent_callback_rows",
                             "module Test\\PreliminaryIndependentRows\n"
                             "export result\n"
                             "left f g n = do use f g n; f end\n"
                             "right f g n = do use f g n; g end\n"
                             "use f g n = ((left f g n) n, (right f g n) n)\n"
                             "get x = do perform State.get (); x end\n"
                             "log x = do perform Log.log (); x end\n"
                             "result = use get log 0\n");

  auto result = run_yonac_ir(src);

  CHECK(result.status == 0);
  CHECK(result.out.find("E0100") == std::string::npos);
}

TEST_CASE("yonac typed core retains effects from independent HOF callbacks") {
  auto src = write_temp_yona("independent_callback_effects",
                             "module Test\\IndependentCallbackEffects\n"
                             "export result\n"
                             "use f g n = (f n, g n)\n"
                             "get x = do perform State.get (); x end\n"
                             "log x = do perform Log.log (); x end\n"
                             "result = use get log 0\n");

  auto result = run_yonac_typed_core(src);

  REQUIRE(result.status == 0);
  const auto result_start = result.out.find("function result ");
  REQUIRE(result_start != std::string::npos);
  const auto result_end = result.out.find('\n', result_start);
  const auto result_line =
      result.out.substr(result_start, result_end - result_start);
  CHECK(result_line.find("State.get") != std::string::npos);
  CHECK(result_line.find("Log.log") != std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects module higher-order open rows") {
  auto forward = write_temp_yona("effect_free_forward_higher_order",
                                 "module Test\\ForwardHigherOrderEffects\n"
                                 "export forward\nexport apply\n"
                                 "forward f x = apply f x\n"
                                 "apply f x = f x\n");
  auto forward_result = run_yonac_ir(forward, {"--require-effect-free"});
  CHECK(forward_result.status != 0);
  CHECK(forward_result.out.find("E0203") != std::string::npos);
  CHECK(forward_result.out.find("closed empty effect row") !=
        std::string::npos);

  auto recursive = write_temp_yona(
      "effect_free_recursive_higher_order",
      "module Test\\RecursiveHigherOrderEffects\n"
      "export app\n"
      "type Nat = Zero | Succ Nat\n"
      "app f n = case n of Zero -> f n; Succ rest -> app f rest end\n");
  auto recursive_result = run_yonac_ir(recursive, {"--require-effect-free"});
  CHECK(recursive_result.status != 0);
  CHECK(recursive_result.out.find("E0203") != std::string::npos);
  CHECK(recursive_result.out.find("closed empty effect row") !=
        std::string::npos);
}

TEST_CASE("yonac --require-effect-free rejects recursive HOF rows in either "
          "arm order") {
  const std::vector<std::pair<std::string, std::string>> modules = {
      {"effect_free_hof_callback_first",
       "module Test\\CallbackFirst\n"
       "export app\n"
       "type Nat = Zero | Succ Nat\n"
       "app f n = case n of Zero -> f n; Succ rest -> app f rest end\n"},
      {"effect_free_hof_recursive_first",
       "module Test\\RecursiveFirst\n"
       "export app\n"
       "type Nat = Zero | Succ Nat\n"
       "app f n = case n of Succ rest -> app f rest; Zero -> f n end\n"},
  };

  for (const auto &[stem, source] : modules) {
    CAPTURE(stem);
    auto result =
        run_yonac_ir(write_temp_yona(stem, source), {"--require-effect-free"});
    CHECK(result.status != 0);
    CHECK(result.out.find("E0203") != std::string::npos);
    CHECK(result.out.find("closed empty effect row") != std::string::npos);
  }
}

TEST_CASE("yonac --require-effect-free preserves HOF rows unified with "
          "preliminary tails") {
  auto source = write_temp_yona(
      "effect_free_hof_rank_contamination",
      "module Test\\RankContamination\n"
      "export a\n"
      "export b\n"
      "type Nat = Zero | Succ Nat\n"
      "a f n = case n of Succ rest -> (a f) rest; Zero -> b f n end\n"
      "b f n = case n of Succ rest -> (a f) rest; Zero -> f n end\n");

  auto result = run_yonac_ir(source, {"--require-effect-free"});

  CHECK(result.status != 0);
  CHECK(result.out.find("requires 'a' to have a closed empty effect row") !=
        std::string::npos);
  CHECK(result.out.find("requires 'b' to have a closed empty effect row") !=
        std::string::npos);
}
