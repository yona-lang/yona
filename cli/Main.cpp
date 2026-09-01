// yonac — Yona compiler
//
// Compiles Yona source code to native executables or object files via LLVM.
//
// Usage:
//   yonac input.yona                  # compile expression to executable
//   yonac input.yona -o output        # compile to output
//   yonac - --emit-ir                 # compile stdin (print LLVM IR)
//   yonac - --emit-obj                # emit object file from stdin
//   yonac module.yona                 # compile module to .o + .yonai
//   yonac -I lib main.yona            # compile with module search path
//   yonac -Wall -Werror main.yona     # enable warnings, treat as errors
//   yonac --Wno-refinement f.yona     # skip E0500
//   yonac --Wno-linear f.yona         # skip E0600/E0601/E0602
//   yonac --Wno-linear-leak f.yona    # keep E0600/E0601, hide E0602
//   yonac --require-effect-free f.yona # reject non-empty or open effect rows
//   yonac --check-style f.yona         # parse and check Yona naming style
//   yonac --emit-typed-core f.yona    # dump typed-core (no LLVM codegen)
//   yonac --emit-accelerator-report f.yona -I lib  # JSON: Std\Gpu +
//   transparent sites yonac --no-accelerator-lowering f.yona         # keep
//   host map/foldl closures yonac --strict-accelerator f.yona              #
//   E0700 on unlowerable column lambdas yonac --emit-accelerator-report
//   --emit-accelerator-report-with-types mod.yona -I lib  # module + types

#include "yona/Codegen/Codegen.h"
#include "yona/Runtime/Generated/VulkanLinkConfig.h"
#include "yona/Semantics/AcceleratorDiag.h"
#include "yona/Semantics/InterfaceCatalog.h"
#include "yona/Semantics/LinearityChecker.h"
#include "yona/Semantics/RefinementChecker.h"
#include "yona/Semantics/TerminationAnalysis.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Support/Process.h"
#include "yona/Support/Version.h"
#include "yona/Syntax/ModuleSource.h"
#include "yona/Syntax/Parser.h"
#include "yona/Syntax/YonaStyle.h"
#include "yona/Toolchain/InProcessLld.h"
#include "yona/Toolchain/LinkerPlan.h"
#include "yona/TypedCore/Abi.h"

#include <llvm/Support/Signals.h>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

using yona::is_module_source;
using yona::SourceManager;
using yona::SourceRange;
using yona::compiler::DiagnosticEngine;
using yona::compiler::ErrorCode;
using yona::compiler::WarningFlag;
using yona::compiler::codegen::Codegen;
using yona::compiler::typechecker::TypeChecker;
namespace ast = yona::ast;
namespace compiler = yona::compiler;
namespace parser = yona::parser;
namespace semantics = yona::semantics;
namespace termination_analysis = yona::compiler::termination_analysis;
namespace typechecker = yona::compiler::typechecker;
using compiler::emit_accelerator_diagnostic_report;
using compiler::emit_accelerator_diagnostic_report_for_module;
using compiler::typecheck_module_for_accelerator_report;
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
namespace filesystem = std::filesystem;
using std::find;
using std::fstream;
using std::function;
using std::get;
using std::ifstream;
using std::istringstream;
using std::map;
using std::move;
using std::optional;
using std::remove;
using std::size_t;
using std::string;
using std::stringstream;
using std::to_string;
using std::unordered_set;
using std::vector;
using std::visit;

static const char *yonac_cc_exe() {
  const char *e = getenv("YONAC_CC");
  if (e && *e)
    return e;
#ifdef _WIN32
  return "clang";
#else
  return "cc";
#endif
}

static int runProcess(const filesystem::path &Executable,
                      const vector<string> &Arguments,
                      bool SuppressStderr = false) {
  auto Result = yona::support::executeProcess(
      Executable, Arguments, {.SuppressStderr = SuppressStderr});
  if (Result.ExecutionFailed && !Result.Error.empty())
    cerr << "Error: " << Result.Error << endl;
  return Result.ExitCode;
}

#ifndef _WIN32
/** Directory that contains libvulkan for -L / rpath.
 *  Prefer CMake-configured dir (same as Vulkan::Vulkan at configure time), else
 *  VULKAN_SDK/lib, else $HOMEBREW_PREFIX/lib. Never hardcode install prefixes.
 */
static string yona_posix_vulkan_lib_dir() {
#if YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR
  {
    filesystem::path p(YONA_CONFIGURED_VULKAN_LIB_DIR);
    if (filesystem::is_directory(p))
      return p.string();
  }
#endif
  const char *sdk = getenv("VULKAN_SDK");
  if (sdk && sdk[0]) {
    filesystem::path lib = filesystem::path(sdk) / "lib";
    if (filesystem::is_directory(lib))
      return lib.string();
  }
  const char *brew = getenv("HOMEBREW_PREFIX");
  if (brew && brew[0]) {
    filesystem::path lib = filesystem::path(brew) / "lib";
    if (filesystem::is_directory(lib))
      return lib.string();
  }
  return {};
}
#endif

#ifdef _WIN32
/** Full path to vulkan-1.lib for the packaged Vulkan-enabled yona_runtime.
 *  Prefer CMake-configured path (same as Vulkan::Vulkan at configure time),
 * else VULKAN_SDK. */
static string yona_windows_vulkan_import_lib_path() {
#if YONA_HAVE_CONFIGURED_VULKAN_IMPORT_LIB
  {
    filesystem::path p(YONA_CONFIGURED_VULKAN_IMPORT_LIB_PATH);
    if (filesystem::exists(p))
      return p.string();
  }
#endif
  const char *sdk = getenv("VULKAN_SDK");
  if (!sdk || !sdk[0])
    return {};
  using std::filesystem::path;
  const char *cands[] = {"Lib/vulkan-1.lib", "Lib32/vulkan-1.lib",
                         "lib/vulkan-1.lib"};
  for (const char *rel : cands) {
    path cand = path(sdk) / rel;
    if (exists(cand))
      return cand.string();
  }
  return {};
}
#endif

static filesystem::path canonical_if_exists(const filesystem::path &p) {
  std::error_code ec;
  if (!filesystem::exists(p, ec))
    return {};
  auto c = filesystem::weakly_canonical(p, ec);
  return ec ? p : c;
}

static bool append_runtime_link_manifest(vector<string> &arguments,
                                         const filesystem::path &runtime,
                                         bool for_lld) {
  ifstream manifest(runtime.parent_path() /
                    (for_lld ? "yona_runtime_link.lld.args"
                             : "yona_runtime_link.driver.args"));
  if (!manifest)
    return false;
  for (string argument; getline(manifest, argument);)
    if (!argument.empty()) {
      if (argument.front() != '-' && filesystem::path(argument).is_relative())
        argument = (runtime.parent_path() / argument).string();
      arguments.push_back(std::move(argument));
    }
  return true;
}

// CMake builds the Prelude object beside the active runtime archive. Prefer
// that matched artifact over incidental Prelude.o files in user, source, or
// working directories; otherwise a stale object can carry an incompatible
// runtime ABI into the final link.
static filesystem::path
find_active_build_prelude_object(const filesystem::path &sysroot) {
#ifdef _WIN32
  constexpr const char *PreludeObjectName = "Prelude.obj";
#else
  constexpr const char *PreludeObjectName = "Prelude.o";
#endif
  return canonical_if_exists(sysroot / "artifacts" / PreludeObjectName);
}

static bool run_overlay_checkers(ast::AstNode *root, DiagnosticEngine &diag,
                                 typechecker::TypeChecker &tc,
                                 bool skip_refinement, bool skip_linear) {
  if (root) {
    if (!skip_refinement) {
      typechecker::RefinementChecker refinement_checker(diag, &tc);
      refinement_checker.check(root);
    }
    if (!skip_linear) {
      typechecker::LinearityChecker linearity_checker(diag, &tc);
      linearity_checker.check(root);
    }
  }
  return !diag.has_errors();
}

static string format_missing_constructors(const vector<string> &missing) {
  string names;
  for (size_t i = 0; i < missing.size(); ++i) {
    if (i)
      names += ", ";
    names += missing[i];
  }
  return names;
}

static bool collect_incomplete_cases(ast::AstNode *node, Codegen &codegen,
                                     DiagnosticEngine &diag) {
  if (!node)
    return true;

  bool ok = true;
  const auto walk = [&](ast::AstNode *child) {
    if (!collect_incomplete_cases(child, codegen, diag))
      ok = false;
  };

  switch (node->get_type()) {
  case ast::AST_MAIN:
    walk(static_cast<ast::MainNode *>(node)->node);
    break;
  case ast::AST_MODULE_DECL: {
    auto *module = static_cast<ast::ModuleDecl *>(node);
    for (auto *function : module->functions)
      walk(function);
    for (auto *trait : module->trait_declarations)
      for (const auto &method : trait->methods)
        walk(method.default_impl);
    for (auto *instance : module->instance_declarations)
      for (auto *method : instance->methods)
        walk(method);
    for (auto *external : module->extern_declarations)
      walk(external->body);
    break;
  }
  case ast::AST_FUNCTION_EXPR: {
    auto *function = static_cast<ast::FunctionExpr *>(node);
    for (auto *body : function->bodies) {
      if (auto *guarded = dynamic_cast<ast::BodyWithGuards *>(body)) {
        walk(guarded->guard);
        walk(guarded->expr);
      } else if (auto *plain = dynamic_cast<ast::BodyWithoutGuards *>(body)) {
        walk(plain->expr);
      }
    }
    break;
  }
  case ast::AST_CASE_EXPR: {
    auto *case_expr = static_cast<ast::CaseExpr *>(node);
    if (auto coverage = codegen.finite_case_coverage(case_expr)) {
      diag.error(case_expr->Range, ErrorCode::E0203,
                 "`--require-effect-free` requires an exhaustive match on " +
                     coverage->adt_name + "; missing constructor" +
                     (coverage->missing.size() == 1 ? " " : "s ") +
                     format_missing_constructors(coverage->missing));
      ok = false;
    }
    walk(case_expr->expr);
    for (auto *clause : case_expr->clauses) {
      if (!clause)
        continue;
      walk(clause->guard);
      walk(clause->body);
    }
    break;
  }
  case ast::AST_LET_EXPR: {
    auto *let_expr = static_cast<ast::LetExpr *>(node);
    for (auto *alias : let_expr->aliases) {
      if (auto *value = dynamic_cast<ast::ValueAlias *>(alias))
        walk(value->expr);
      else if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias))
        walk(lambda->lambda);
      else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(alias))
        walk(pattern->expr);
    }
    walk(let_expr->expr);
    break;
  }
  case ast::AST_IMPORT_EXPR:
    walk(static_cast<ast::ImportExpr *>(node)->expr);
    break;
  case ast::AST_IF_EXPR: {
    auto *if_expr = static_cast<ast::IfExpr *>(node);
    walk(if_expr->condition);
    walk(if_expr->thenExpr);
    walk(if_expr->elseExpr);
    break;
  }
  case ast::AST_DO_EXPR:
    for (auto *step : static_cast<ast::DoExpr *>(node)->steps)
      walk(step);
    break;
  case ast::AST_WITH_EXPR: {
    auto *with = static_cast<ast::WithExpr *>(node);
    walk(with->contextExpr);
    walk(with->bodyExpr);
    break;
  }
  case ast::AST_HANDLE_EXPR: {
    auto *handle = static_cast<ast::HandleExpr *>(node);
    walk(handle->body);
    for (auto *clause : handle->clauses)
      if (clause)
        walk(clause->body);
    break;
  }
  case ast::AST_TRY_CATCH_EXPR: {
    auto *try_catch = static_cast<ast::TryCatchExpr *>(node);
    walk(try_catch->tryExpr);
    for (auto *catch_pattern : try_catch->catchExpr->patterns) {
      if (!catch_pattern)
        continue;
      std::visit(
          [&](auto &body) {
            using Body = std::remove_cvref_t<decltype(body)>;
            if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
              walk(body->expr);
            } else {
              for (auto *guarded : body) {
                walk(guarded->guard);
                walk(guarded->expr);
              }
            }
          },
          catch_pattern->pattern);
    }
    break;
  }
  case ast::AST_APPLY_EXPR: {
    auto *apply = static_cast<ast::ApplyExpr *>(node);
    walk(apply->call);
    for (const auto &argument : apply->args)
      std::visit([&](auto *argument_node) { walk(argument_node); }, argument);
    if (apply->named_args)
      for (const auto &[_, argument] : *apply->named_args)
        std::visit([&](auto *argument_node) { walk(argument_node); }, argument);
    break;
  }
  case ast::AST_TUPLE_EXPR:
    for (auto *value : static_cast<ast::TupleExpr *>(node)->values)
      walk(value);
    break;
  case ast::AST_DICT_EXPR:
    for (const auto &[key, value] :
         static_cast<ast::DictExpr *>(node)->values) {
      walk(key);
      walk(value);
    }
    break;
  case ast::AST_VALUES_SEQUENCE_EXPR:
    for (auto *value : static_cast<ast::ValuesSequenceExpr *>(node)->values)
      walk(value);
    break;
  case ast::AST_RANGE_SEQUENCE_EXPR: {
    auto *range = static_cast<ast::RangeSequenceExpr *>(node);
    walk(range->start);
    walk(range->end);
    walk(range->step);
    break;
  }
  case ast::AST_SET_EXPR:
    for (auto *value : static_cast<ast::SetExpr *>(node)->values)
      walk(value);
    break;
  case ast::AST_RECORD_INSTANCE_EXPR:
    for (const auto &[_, value] :
         static_cast<ast::RecordInstanceExpr *>(node)->items)
      walk(value);
    break;
  case ast::AST_RECORD_LITERAL_EXPR:
    for (const auto &[_, value] :
         static_cast<ast::RecordLiteralExpr *>(node)->fields)
      walk(value);
    break;
  case ast::AST_FIELD_UPDATE_EXPR:
    for (const auto &[_, value] :
         static_cast<ast::FieldUpdateExpr *>(node)->updates)
      walk(value);
    break;
  case ast::AST_BINARY_OP_EXPR:
  case ast::AST_ADD_EXPR:
  case ast::AST_SUBTRACT_EXPR:
  case ast::AST_MULTIPLY_EXPR:
  case ast::AST_DIVIDE_EXPR:
  case ast::AST_MODULO_EXPR:
  case ast::AST_POWER_EXPR:
  case ast::AST_EQ_EXPR:
  case ast::AST_NEQ_EXPR:
  case ast::AST_LT_EXPR:
  case ast::AST_LTE_EXPR:
  case ast::AST_GT_EXPR:
  case ast::AST_GTE_EXPR:
  case ast::AST_LOGICAL_AND_EXPR:
  case ast::AST_LOGICAL_OR_EXPR:
  case ast::AST_PIPE_RIGHT_EXPR:
  case ast::AST_PIPE_LEFT_EXPR:
  case ast::AST_IN_EXPR:
  case ast::AST_CONS_LEFT_EXPR:
  case ast::AST_CONS_RIGHT_EXPR:
  case ast::AST_JOIN_EXPR:
  case ast::AST_REMOVE_EXPR:
  case ast::AST_LEFT_SHIFT_EXPR:
  case ast::AST_RIGHT_SHIFT_EXPR:
  case ast::AST_ZEROFILL_RIGHT_SHIFT_EXPR:
  case ast::AST_BITWISE_AND_EXPR:
  case ast::AST_BITWISE_OR_EXPR:
  case ast::AST_BITWISE_XOR_EXPR: {
    auto *binary = static_cast<ast::BinaryOpExpr *>(node);
    walk(binary->left);
    walk(binary->right);
    break;
  }
  case ast::AST_LOGICAL_NOT_OP_EXPR:
    walk(static_cast<ast::LogicalNotOpExpr *>(node)->expr);
    break;
  case ast::AST_BINARY_NOT_OP_EXPR:
    walk(static_cast<ast::BinaryNotOpExpr *>(node)->expr);
    break;
  default:
    break;
  }
  return ok;
}

static string
termination_repair_note(const termination_analysis::Failure &failure) {
  if (failure.reason == "recursive component members have incompatible arity") {
    return "Repair: give every function in the recursive component the same "
           "number of parameters, "
           "or reshape the recursive component into SCCs with compatible arity";
  }
  if (failure.reason.find("no provable lexicographic structural descent") !=
      string::npos) {
    return "Repair: recurse on a constructor field or non-empty sequence tail "
           "bound by an unguarded "
           "`case` arm, preserving the decreasing parameter position across "
           "the cycle "
           "(for example, `Succ rest -> loop rest`)";
  }
  return "Repair: expose direct local recursive calls with a statically "
         "visible structural decrease; "
         "opaque helper and higher-order calls are not inspected by this proof";
}

static bool require_structural_termination(ast::AstNode *root,
                                           DiagnosticEngine &diag) {
  if (!root)
    return true;
  const auto result = termination_analysis::analyze(*root);
  for (const auto &failure : result.failures) {
    diag.error(failure.call_location, ErrorCode::E0203,
               "`--require-effect-free` cannot prove structural termination "
               "for recursive component '" +
                   failure.component + "' at call '" + failure.caller + " -> " +
                   failure.callee + "': " + failure.reason + ". " +
                   termination_repair_note(failure));
  }
  return result.failures.empty();
}

static bool require_effect_free(ast::AstNode *root, DiagnosticEngine &diag,
                                typechecker::TypeChecker &tc,
                                Codegen &codegen) {
  bool ok = true;
  if (auto *mod = dynamic_cast<ast::ModuleDecl *>(root)) {
    for (auto *func : mod->functions) {
      if (!func || tc.is_effect_free(tc.type_of(func)))
        continue;
      diag.error(func->Range, ErrorCode::E0203,
                 "`--require-effect-free` requires '" + func->name +
                     "' to have a closed empty effect row. Repair: remove or "
                     "handle the open effect source; "
                     "opaque and higher-order calls must have a closed empty "
                     "row independently of recursion shape");
      ok = false;
    }
  }
  for (const auto &loc : tc.unhandled_effect_locations()) {
    diag.error(loc, ErrorCode::E0203,
               "`--require-effect-free` rejects an unhandled effect operation");
    ok = false;
  }
  if (tc.has_unknown_effect_rows()) {
    diag.error(SourceRange::unknown(), ErrorCode::E0203,
               "`--require-effect-free` cannot prove an imported function's "
               "effect row; "
               "rebuild its interface to record `effects -`");
    ok = false;
  }
  if (!collect_incomplete_cases(root, codegen, diag))
    ok = false;
  if (!require_structural_termination(root, diag))
    ok = false;
  return ok;
}

static vector<filesystem::path> discoverSysroots(const char *argv0,
                                                 const string &sysroot_opt) {
  return yona::toolchain::discoverSysroots(argv0, sysroot_opt);
}

int main(int argc, char *argv[]) {
  // Keep compiler failures diagnosable on every native platform. In particular,
  // Windows otherwise reports a bare "Access violation" without the LLVM/C++
  // frames needed to repair a code-generation failure.
  llvm::sys::PrintStackTraceOnErrorSignal(argc > 0 ? argv[0] : "yonac");

  CLI::App app{"yonac — Yona compiler"};

  string input_file;
  string output_file;
  bool emit_ir = false;
  bool emit_obj = false;
  bool emit_typed_core = false;
  bool check_style = false;
  bool emit_accelerator_report = false;
  bool emit_accelerator_report_with_types = false;
  bool no_accelerator_lowering = false;
  bool strict_accelerator = false;
  bool flag_wall = false;
  bool flag_wextra = false;
  bool flag_werror = false;
  bool flag_w = false;
  bool flag_incomplete_patterns = false;
  bool flag_overlapping_patterns = false;
  bool flag_no_refinement = false;
  bool flag_no_linear = false;
  bool flag_no_linear_leak = false;
  bool flag_require_effect_free = false;
  bool flag_debug = false;
  int opt_level = 2;
  vector<string> include_paths;
  string last_include_path;
  string sysroot_path;
  string explain_code;
  string linker_mode_opt;

  app.set_version_flag("--version", YONA_VERSION_STRING);
  app.add_option("input", input_file, "Input .yona file, or - to read stdin");
  app.add_option("-o,--output", output_file, "Output file");
  app.add_option("-I,--include", last_include_path,
                 "Module search paths (for .yonai files)")
      ->take_last()
      ->each([&include_paths](string path) {
        include_paths.push_back(std::move(path));
      });
  app.add_option(
      "--sysroot", sysroot_path,
      "Yona distribution root (used to find lib/ and the runtime archive)");
  app.add_option("--linker-mode", linker_mode_opt,
                 "Linker mode: auto|bundled|system|inprocess (also via "
                 "YONAC_LINKER_MODE)");
  app.add_option("-O", opt_level, "Optimization level (0-3, default 2)")
      ->check(CLI::Range(0, 3));
  app.add_flag("--emit-ir", emit_ir, "Print LLVM IR instead of compiling");
  app.add_flag("--emit-obj", emit_obj, "Emit object file only (don't link)");
  app.add_flag("--emit-typed-core", emit_typed_core,
               "Print a typed-core dump (resolved names, types, effects, "
               "linearity, spans) and exit without LLVM codegen");
  app.add_flag("--check-style", check_style,
               "Parse the input, check Yona identifier naming, and exit");
  app.add_flag(
      "--emit-accelerator-report", emit_accelerator_report,
      "Print JSON of Std\\Gpu-shaped call sites and exit (no codegen): "
      "expression programs after typecheck; modules from AST scan by default");
  app.add_flag(
      "--emit-accelerator-report-with-types",
      emit_accelerator_report_with_types,
      "With --emit-accelerator-report on a module, run the typechecker first "
      "(JSON report_kind \"module\", optional inferred_type per site)");
  app.add_flag(
      "--no-accelerator-lowering", no_accelerator_lowering,
      "Keep IntArray/FloatArray map/filter/foldl on the host closure path "
      "(do not rewrite recognized kernels to the Std\\Gpu ABI)");
  app.add_flag(
      "--strict-accelerator", strict_accelerator,
      "Error (E0700) on IntArray/FloatArray map/filter/foldl lambdas "
      "outside the fixed Std\\Gpu kernel library (no silent host fallback)");
  app.add_flag("--Wall", flag_wall, "Enable common warnings");
  app.add_flag("--Wextra", flag_wextra, "Enable all warnings");
  app.add_flag("--Werror", flag_werror, "Treat warnings as errors");
  app.add_flag("-w", flag_w, "Suppress all warnings");
  app.add_flag("--Wincomplete-patterns", flag_incomplete_patterns,
               "Warn when a finite ADT case misses constructors");
  app.add_flag(
      "--Woverlapping-patterns", flag_overlapping_patterns,
      "Warn when a case arm is unreachable after an earlier unguarded arm");
  app.add_flag("--Wno-refinement", flag_no_refinement,
               "Skip refinement checking (E0500 nonempty/nonzero proofs)");
  app.add_flag("--Wno-linear", flag_no_linear,
               "Skip linearity checking (E0600/E0601/E0602)");
  app.add_flag("--Wno-linear-leak", flag_no_linear_leak,
               "Disable E0602 resource-leak warnings (-Wlinear-leak)");
  app.add_flag(
      "--require-effect-free", flag_require_effect_free,
      "Require closed empty effect rows, finite case coverage, and "
      "conservative structural size-change "
      "proofs for local recursive SCCs (not a global termination proof)");
  app.add_flag("-g,--debug", flag_debug, "Emit DWARF debug information");
  app.add_option("--explain", explain_code,
                 "Show detailed explanation for an error code (e.g., E0100)");

  CLI11_PARSE(app, argc, argv);

  if (emit_typed_core && emit_ir) {
    cerr << "Error: --emit-typed-core cannot be combined with --emit-ir"
         << endl;
    return 1;
  }
  if (emit_typed_core && emit_obj) {
    cerr << "Error: --emit-typed-core cannot be combined with --emit-obj"
         << endl;
    return 1;
  }
  if (emit_typed_core && emit_accelerator_report) {
    cerr << "Error: --emit-typed-core cannot be combined with "
            "--emit-accelerator-report"
         << endl;
    return 1;
  }
  if (emit_accelerator_report && emit_ir) {
    cerr << "Error: --emit-accelerator-report cannot be combined with --emit-ir"
         << endl;
    return 1;
  }
  if (emit_accelerator_report && emit_obj) {
    cerr
        << "Error: --emit-accelerator-report cannot be combined with --emit-obj"
        << endl;
    return 1;
  }
  if (emit_accelerator_report_with_types && !emit_accelerator_report) {
    cerr << "Error: --emit-accelerator-report-with-types requires "
            "--emit-accelerator-report"
         << endl;
    return 1;
  }
  if (check_style &&
      (emit_ir || emit_obj || emit_typed_core || emit_accelerator_report)) {
    cerr << "Error: --check-style cannot be combined with an emission mode"
         << endl;
    return 1;
  }

  // --explain: print explanation and exit
  if (!explain_code.empty()) {
    auto code = compiler::parse_error_code(explain_code);
    if (code) {
      string explanation = compiler::error_explanation(*code);
      if (!explanation.empty()) {
        cout << explanation << endl;
        return 0;
      }
    }
    cerr << "Unknown error code: " << explain_code << endl;
    return 1;
  }

  // Get source code
  string source;
  string filename;
  if (input_file == "-") {
    stringstream buf;
    buf << cin.rdbuf();
    source = buf.str();
    filename = "<stdin>";
  } else if (!input_file.empty()) {
    ifstream file(input_file);
    if (!file.is_open()) {
      cerr << "Error: cannot open " << input_file << endl;
      return 1;
    }
    stringstream buf;
    buf << file.rdbuf();
    source = buf.str();
    filename = input_file;
  } else {
    cerr << "Error: no input. Use 'yonac file.yona' or 'yonac -' (stdin)"
         << endl;
    return 1;
  }

  bool is_module = is_module_source(source);
  if (emit_accelerator_report_with_types && !is_module) {
    cerr << "Error: --emit-accelerator-report-with-types is only for module "
            "sources"
         << endl;
    return 1;
  }

  // Set default output
  if (output_file.empty()) {
    if (is_module || emit_obj) {
      if (!input_file.empty() && input_file != "-")
        output_file = filesystem::path(input_file).stem().string() + ".o";
      else
        output_file = "a.o";
    } else {
#ifdef _WIN32
      output_file = "a.exe";
#else
      output_file = "a.out";
#endif
    }
  }

  // Set up diagnostics
  DiagnosticEngine diag;
  if (flag_w)
    diag.suppress_all_warnings();
  if (flag_wall)
    diag.enable_wall();
  if (flag_wextra)
    diag.enable_wextra();
  if (flag_incomplete_patterns)
    diag.enable_warning(WarningFlag::IncompletePatterns);
  if (flag_overlapping_patterns)
    diag.enable_warning(WarningFlag::OverlappingPatterns);
  if (flag_werror)
    diag.set_warnings_as_errors(true);
  if (flag_no_linear_leak)
    diag.disable_warning(WarningFlag::LinearLeak);

  if (check_style) {
    parser::Parser parser;
    if (is_module) {
      auto result = parser.parseModule(source, filename);
      if (!result.has_value()) {
        if (!result.error().empty())
          diag.setSources(result.error().front().Sources);
        for (auto &error : result.error())
          diag.error(error.Range, compiler::ErrorCode::E0301, error.Message);
        return 1;
      }
      diag.setSources(result->Sources);
    } else {
      auto result = parser.parseExpression(source, filename);
      if (!result || !result->Expression) {
        if (!result.error().empty())
          diag.setSources(result.error().front().Sources);
        if (!result.has_value()) {
          for (auto &error : result.error())
            diag.error(error.Range, compiler::ErrorCode::E0301, error.Message);
        } else {
          diag.error(SourceRange::unknown(), compiler::ErrorCode::E0301,
                     "parse error");
        }
        return 1;
      }
      diag.setSources(result->Sources);
    }

    auto result = yona::syntax::checkYonaStyle(source, filename);
    if (!result.has_value()) {
      for (const auto &error : result.error())
        cerr << error.format() << endl;
      return 1;
    }
    for (const auto &finding : *result) {
      cerr << finding.Sources->format(finding.Location)
           << ": style error: " << finding.Message << endl;
    }
    return result->empty() ? 0 : 1;
  }

  // Codegen
  string module_name = is_module ? "yona_module" : "yona_program";
  Codegen codegen(module_name, &diag);

  if (flag_debug)
    codegen.set_debug_info(true, filename);
  codegen.set_opt_level(opt_level);
  codegen.set_accelerator_lowering(!no_accelerator_lowering);
  codegen.set_strict_accelerator(strict_accelerator &&
                                 !no_accelerator_lowering);

  vector<filesystem::path> sysroots =
      discoverSysroots(argc > 0 ? argv[0] : nullptr, sysroot_path);
  yona::toolchain::LinkerPlan linker_selection;
  string linker_mode_raw = linker_mode_opt;
  if (linker_mode_raw.empty()) {
    if (const char *env_mode = getenv("YONAC_LINKER_MODE")) {
      if (*env_mode)
        linker_mode_raw = env_mode;
    }
  }
  string linker_error;
  if (!yona::toolchain::resolveLinkerPlan(linker_mode_raw, sysroots,
                                          linker_selection, linker_error)) {
    cerr << "Error: " << linker_error << endl;
    return 1;
  }
  const bool require_inprocess = yona::toolchain::requireInProcessLldFromEnv();
  if (linker_selection.UseInProcessLld &&
      !yona::toolchain::inProcessLldAvailable()) {
    if (require_inprocess) {
      cerr << "Error: inprocess linker mode required but unavailable: "
           << yona::toolchain::inProcessLldUnavailableReason() << endl;
      return 1;
    }
    cerr << "Warning: inprocess linker mode requested but unavailable: "
         << yona::toolchain::inProcessLldUnavailableReason()
         << ". Falling back to external linker path." << endl;
  }

  // Set module search paths for import resolution.
  unordered_set<string> module_seen;
  auto add_module_path = [&](const filesystem::path &p) {
    auto c = canonical_if_exists(p);
    if (c.empty())
      return;
    string s = c.string();
    if (module_seen.insert(s).second)
      codegen.ModulePaths.push_back(s);
  };
  for (const auto &inc : include_paths)
    add_module_path(inc);
#ifdef _WIN32
  const char yona_path_sep = ';';
#else
  const char yona_path_sep = ':';
#endif
  if (const char *yp = getenv("YONA_PATH"); yp && *yp) {
    string cur;
    auto flush_yp = [&]() {
      if (!cur.empty())
        add_module_path(cur);
      cur.clear();
    };
    for (const char *c = yp; *c; ++c) {
      if (*c == yona_path_sep)
        flush_yp();
      else
        cur.push_back(*c);
    }
    flush_yp();
  }
  if (!input_file.empty()) {
    auto parent = filesystem::path(input_file).parent_path();
    if (!parent.empty())
      add_module_path(parent);
  }
  add_module_path(".");
  for (const auto &root : sysroots) {
    add_module_path(root / "lib");
    add_module_path(root / "share" / "yona" / "lib");
  }
  // Relative installation probing.
  for (auto &candidate : {"lib", "../lib", "../../lib", "../../../lib"}) {
    auto c = canonical_if_exists(filesystem::path(candidate));
    if (!c.empty() && filesystem::exists(c / "Prelude.yonai")) {
      add_module_path(c);
      break;
    }
  }

  if (emit_typed_core) {
    vector<const char *> tc_paths;
    tc_paths.reserve(codegen.ModulePaths.size());
    for (const auto &p : codegen.ModulePaths)
      tc_paths.push_back(p.c_str());
    YonaTypedCoreModule *tc = YonaTypedCoreAnalyze(
        source.c_str(), filename.c_str(),
        tc_paths.empty() ? nullptr : tc_paths.data(), tc_paths.size());
    if (!tc) {
      cerr << "Error: typed-core analysis failed" << endl;
      return 1;
    }
    char *text = YonaTypedCorePrettyPrint(tc);
    if (text) {
      cout << text;
      YonaTypedCoreDisposeString(text);
    }
    YonaTypedCoreDisposeModule(tc);
    return 0;
  }

  llvm::Module *llvm_mod = nullptr;

  if (is_module) {
    parser::Parser parser;
    if (emit_accelerator_report && emit_accelerator_report_with_types) {
      typechecker::TypeChecker type_checker(diag);
      codegen.loadPrelude();
      semantics::InterfaceCatalog Interfaces(codegen.ModulePaths);
      Interfaces.appendEnvironmentSearchPaths();
      const auto PreludeInstalled =
          Interfaces.installPrelude(parser, type_checker);
      if (!PreludeInstalled || !*PreludeInstalled) {
        cerr << "Error: unable to load Prelude interface" << endl;
        return 1;
      }
      type_checker.set_import_type_source(&Interfaces);
      auto result = parser.parseModule(source, filename);
      if (!result.has_value()) {
        if (!result.error().empty())
          diag.setSources(result.error().front().Sources);
        for (auto &e : result.error())
          diag.error(e.Range, compiler::ErrorCode::E0301, e.Message);
        return 1;
      }
      diag.setSources(result->Sources);
      if (!typecheck_module_for_accelerator_report(result.value().get(),
                                                   type_checker))
        return 1;
      emit_accelerator_diagnostic_report_for_module(
          std::cout, result.value().get(), filename, &type_checker);
      return 0;
    }
    typechecker::TypeChecker type_checker(diag);
    type_checker.set_require_effect_free(flag_require_effect_free);
    codegen.loadPrelude();
    semantics::InterfaceCatalog Interfaces(codegen.ModulePaths);
    Interfaces.appendEnvironmentSearchPaths();
    const auto PreludeInstalled =
        Interfaces.installPrelude(parser, type_checker);
    if (!PreludeInstalled || !*PreludeInstalled) {
      cerr << "Error: unable to load Prelude interface" << endl;
      return 1;
    }
    for (auto &p : codegen.ModulePaths)
      type_checker.add_module_path(p);
    type_checker.set_import_type_source(&Interfaces);
    auto result = parser.parseModule(source, filename);
    if (!result.has_value()) {
      if (!result.error().empty())
        diag.setSources(result.error().front().Sources);
      for (auto &e : result.error())
        diag.error(e.Range, compiler::ErrorCode::E0301, e.Message);
      return 1;
    }
    diag.setSources(result->Sources);
    if (emit_accelerator_report) {
      emit_accelerator_diagnostic_report_for_module(
          std::cout, result.value().get(), filename);
      return 0;
    }
    type_checker.check_module(result.value().get());
    if (!type_checker.solve_constraints() || type_checker.has_errors())
      return 1;
    if (!run_overlay_checkers(result.value().get(), diag, type_checker,
                              flag_no_refinement, flag_no_linear))
      return 1;
    codegen.set_type_checker(&type_checker);
    llvm_mod = codegen.compile_module(result.value().get());
    // compile_module registers declarations local to this module, allowing the
    // strict totality gate to cover both prelude/imported and local finite
    // ADTs.
    if (flag_require_effect_free &&
        !require_effect_free(result.value().get(), diag, type_checker, codegen))
      return 1;
    if (llvm_mod)
      codegen.populate_interface_effect_rows(result.value().get(),
                                             type_checker);
  } else {
    parser::Parser parser;
    typechecker::TypeChecker type_checker(diag);
    type_checker.set_require_effect_free(flag_require_effect_free);
    codegen.loadPrelude();
    semantics::InterfaceCatalog Interfaces(codegen.ModulePaths);
    Interfaces.appendEnvironmentSearchPaths();
    const auto PreludeInstalled =
        Interfaces.installPrelude(parser, type_checker);
    if (!PreludeInstalled || !*PreludeInstalled) {
      cerr << "Error: unable to load Prelude interface" << endl;
      return 1;
    }
    for (auto &p : codegen.ModulePaths)
      type_checker.add_module_path(p);

    auto parse_result = parser.parseExpression(source, filename);
    if (!parse_result || !parse_result->Expression) {
      if (!parse_result.error().empty())
        diag.setSources(parse_result.error().front().Sources);
      if (!parse_result.has_value()) {
        for (auto &e : parse_result.error())
          diag.error(e.Range, compiler::ErrorCode::E0301, e.Message);
      } else {
        diag.error(SourceRange::unknown(), compiler::ErrorCode::E0301,
                   "parse error");
      }
      return 1;
    }
    diag.setSources(parse_result->Sources);

    type_checker.set_import_type_source(&Interfaces);
    auto *checked_type = type_checker.check(parse_result->Expression.get());
    if (flag_require_effect_free) {
      bool gate_ok = require_effect_free(parse_result->Expression.get(), diag,
                                         type_checker, codegen);
      if (!type_checker.is_effect_free(checked_type) &&
          !type_checker.has_unknown_effect_rows()) {
        diag.error(
            parse_result->Expression->Range, ErrorCode::E0203,
            "`--require-effect-free` requires a closed empty effect row");
        gate_ok = false;
      }
      if (!gate_ok)
        return 1;
    }
    if (type_checker.has_direct_errors()) {
      return 1;
    }
    codegen.set_type_checker(&type_checker);

    if (!type_checker.solve_constraints() || type_checker.has_errors())
      return 1;

    if (emit_accelerator_report) {
      emit_accelerator_diagnostic_report(
          std::cout, parse_result->Expression.get(), &type_checker, filename);
      return 0;
    }

    if (!run_overlay_checkers(parse_result->Expression.get(), diag,
                              type_checker, flag_no_refinement, flag_no_linear))
      return 1;

    llvm_mod = codegen.compile(parse_result->Expression.get());
  }

  if (!llvm_mod) {
    // Errors already printed by DiagnosticEngine
    return 1;
  }
  // Codegen may still produce a verifiable module after E0104/etc.; do not
  // link a binary that would return the wrong result with exit 0.
  if (codegen.errorCount() > 0 || diag.has_errors())
    return 1;

  // Print summary if there were warnings
  if (diag.warning_count() > 0) {
    cerr << diag.warning_count() << " warning"
         << (diag.warning_count() != 1 ? "s" : "") << " generated." << endl;
  }

  if (emit_ir) {
    cout << codegen.emit_ir();
    return 0;
  }

  // Emit object file
  string obj_file =
      (is_module || emit_obj) ? output_file : (output_file + ".o");
  if (!codegen.emit_object_file(obj_file)) {
    diag.error(SourceRange::unknown(), compiler::ErrorCode::E0400,
               "failed to emit object file");
    return 1;
  }
  if (codegen.errorCount() > 0 || diag.has_errors())
    return 1;

  // For modules, also emit interface file (.yonai)
  if (is_module) {
    auto yonai_path = filesystem::path(output_file).replace_extension(".yonai");
    if (!codegen.emit_interface_file(yonai_path.string()) ||
        codegen.errorCount() > 0 || diag.has_errors())
      return 1;
    return 0;
  }

  if (emit_obj)
    return 0;

  // Link expression into executable.
  string rt_obj;
  filesystem::path runtime_sysroot;
#ifdef _WIN32
  constexpr const char *RuntimeArchiveName = "yona_runtime.lib";
#else
  constexpr const char *RuntimeArchiveName = "libyona_runtime.a";
#endif
  auto findRuntimeArchive = [&]() -> bool {
    for (const auto &root : sysroots) {
      for (const auto &base :
           {root / "runtime", root / "lib" / "yona" / "runtime"}) {
        auto archive = canonical_if_exists(base / RuntimeArchiveName);
        if (!archive.empty()) {
          rt_obj = archive.string();
          runtime_sysroot = root;
          return true;
        }
      }
    }
    return false;
  };

  if (!findRuntimeArchive()) {
    diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
               "canonical Yona runtime archive was not found under the "
               "selected sysroot; build or install yona_runtime");
    return 1;
  }

  // An active build artifact belongs to the selected runtime and must win over
  // incidental Prelude objects in the module search path. Packaged and custom
  // sysroots keep their existing module-path lookup as the fallback.
  string prelude_obj =
      find_active_build_prelude_object(runtime_sysroot).string();
  if (prelude_obj.empty()) {
#ifdef _WIN32
    constexpr const char *PreludeObjectName = "Prelude.obj";
#else
    constexpr const char *PreludeObjectName = "Prelude.o";
#endif
    for (const auto &dir : codegen.ModulePaths) {
      auto candidate =
          canonical_if_exists(filesystem::path(dir) / PreludeObjectName);
      if (!candidate.empty()) {
        prelude_obj = candidate.string();
        break;
      }
    }
  }

  // Unix: -rdynamic exports symbols for backtrace_symbols() stack traces.
  auto append_link_objects = [&](auto &&append_one) {
    append_one(obj_file);
    append_one(rt_obj);
    if (!prelude_obj.empty())
      append_one(prelude_obj);
  };

  int link_result = 1;
  bool used_inprocess = false;
  if (linker_selection.UseInProcessLld &&
      yona::toolchain::inProcessLldAvailable()) {
    vector<string> lld_args;
#ifdef _WIN32
    lld_args.push_back("lld-link");
    lld_args.push_back("/NOLOGO");
    append_link_objects([&](const string &s) { lld_args.push_back(s); });
    lld_args.push_back("/OUT:" + filesystem::path(output_file).string());
    for (const auto &a : yona::toolchain::inProcessLldAfterInputArgs())
      lld_args.push_back(a);
    {
      string vk_lib = yona_windows_vulkan_import_lib_path();
      if (!vk_lib.empty())
        lld_args.push_back(vk_lib);
    }
#else
#ifdef __APPLE__
    lld_args.push_back("ld64.lld");
#else
    lld_args.push_back("ld.lld");
#endif
    for (const auto &a : yona::toolchain::inProcessLldBeforeInputArgs())
      lld_args.push_back(a);
    append_link_objects([&](const string &s) { lld_args.push_back(s); });
    lld_args.push_back("-o");
    lld_args.push_back(filesystem::path(output_file).string());
    for (const auto &a : yona::toolchain::inProcessLldAfterInputArgs())
      lld_args.push_back(a);
#endif
    if (!append_runtime_link_manifest(lld_args, rt_obj, true)) {
      diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
                 "runtime linker manifest is missing beside yona_runtime");
      return 1;
    }
#ifdef YONAC_EXE_LINK_POSIX_VULKAN
    {
      string vk_dir = yona_posix_vulkan_lib_dir();
      if (!vk_dir.empty()) {
        lld_args.push_back("-L" + vk_dir);
#ifdef __APPLE__
        lld_args.push_back("-rpath");
        lld_args.push_back(vk_dir);
#endif
      }
    }
    lld_args.push_back("-lvulkan");
#endif
    yona::toolchain::InProcessLldResult lld_res;
    used_inprocess = true;
    if (yona::toolchain::run_inprocess_lld(lld_args, lld_res)) {
      link_result = 0;
    } else {
      if (require_inprocess) {
        diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
                   "in-process LLD link failed and fallback is disabled");
        if (!lld_res.diagnostic_text().empty())
          cerr << lld_res.diagnostic_text() << endl;
        return 1;
      }
      cerr << "Warning: in-process LLD link failed, falling back to external "
              "linker path.";
      if (!lld_res.diagnostic_text().empty())
        cerr << " details: " << lld_res.diagnostic_text();
      cerr << endl;
      link_result = 1;
    }
  }

  if (!used_inprocess || link_result != 0) {
    const char *cc_link = yonac_cc_exe();
    vector<string> link_args = {obj_file};
    if (linker_selection.UseBundledLld) {
      link_args.push_back("-fuse-ld=lld");
      link_args.push_back(
          "-B" + linker_selection.BundledLldPath.parent_path().string());
    }
    link_args.push_back(rt_obj);
    if (!prelude_obj.empty())
      link_args.push_back(prelude_obj);
#ifdef _WIN32
    link_args.push_back("-o");
    link_args.push_back(output_file);
    link_args.push_back("-lws2_32");
    link_args.push_back("-ldbghelp");
    if (!append_runtime_link_manifest(link_args, rt_obj, false)) {
      diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
                 "runtime linker manifest is missing beside yona_runtime");
      return 1;
    }
    {
      string vk_lib = yona_windows_vulkan_import_lib_path();
      if (!vk_lib.empty())
        link_args.push_back(vk_lib);
    }
#else
#ifdef __APPLE__
    link_args.push_back("-lpthread");
    link_args.push_back("-Wl,-U,_yona_regex_free_code");
#else
    link_args.push_back("-lm");
    link_args.push_back("-lpthread");
    link_args.push_back("-rdynamic");
#endif
    if (!append_runtime_link_manifest(link_args, rt_obj, false)) {
      diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
                 "runtime linker manifest is missing beside yona_runtime");
      return 1;
    }
#ifdef YONAC_EXE_LINK_POSIX_VULKAN
    {
      string vk_dir = yona_posix_vulkan_lib_dir();
      if (!vk_dir.empty()) {
        link_args.push_back("-L" + vk_dir);
#ifdef __APPLE__
        link_args.push_back("-Wl,-rpath," + vk_dir);
#endif
      }
    }
    link_args.push_back("-lvulkan");
#endif
    link_args.push_back("-o");
    link_args.push_back(output_file);
#endif
    link_result = runProcess(cc_link, link_args);
  }
  filesystem::remove(obj_file);

  if (link_result != 0) {
    diag.error(SourceRange::unknown(), compiler::ErrorCode::E0401,
               "linking failed");
    return 1;
  }

  return 0;
}
