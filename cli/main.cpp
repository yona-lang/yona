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
//   yonac --emit-typed-core f.yona    # dump typed-core (no LLVM codegen)
//   yonac --emit-accelerator-report f.yona -I lib  # JSON: Std\GPU + transparent sites
//   yonac --no-accelerator-lowering f.yona         # keep host map/foldl closures
//   yonac --strict-accelerator f.yona              # E0700 on unlowerable column lambdas
//   yonac --emit-accelerator-report --emit-accelerator-report-with-types mod.yona -I lib  # module + types

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

#include "AcceleratorDiag.h"
#include "Codegen.h"
#include "ModuleSource.h"
#include "Diagnostic.h"
#include "InProcessLld.h"
#include "LinkerPlan.h"
#include "Parser.h"
#include "TerminationAnalysis.h"
#include "typechecker/LinearityChecker.h"
#include "typechecker/RefinementChecker.h"
#include "typechecker/TypeChecker.h"
#include "typed_core/abi.h"
#include "version.h"
#include "yona_vulkan_link_cfg.h"
#include <CLI/CLI.hpp>
#include <llvm/Support/Signals.h>

using namespace std;
using namespace yona;
using namespace yona::compiler;
using namespace yona::compiler::codegen;

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

static string shell_stderr_null() {
#ifdef _WIN32
  return " 2>nul";
#else
  return " 2>/dev/null";
#endif
}

static string q_cmd_path(const filesystem::path &p) { return "\"" + p.lexically_normal().generic_string() + "\""; }

// `system()` invokes cmd.exe on Windows. Its leading-quote rule strips the
// outer quotes unless the complete command is wrapped, so a compiler installed
// below "Program Files" would otherwise be executed as `C:/Program`.
static string system_command(string command) {
#ifdef _WIN32
  constexpr string_view stderr_null = " 2>nul";
  string redirect;
  if (command.size() >= stderr_null.size() &&
      command.compare(command.size() - stderr_null.size(), stderr_null.size(), stderr_null) == 0) {
    redirect = string(stderr_null);
    command.resize(command.size() - stderr_null.size());
  }
  if (!command.empty() && command.front() == '"')
    command = "\"" + command + "\"";
  return command + redirect;
#else
  return command;
#endif
}

static int run_system_command(string command) { return system(system_command(std::move(command)).c_str()); }

static string q_cmd_executable(const char *executable) {
  return q_cmd_path(filesystem::path(executable));
}

#ifndef _WIN32
/** Directory that contains libvulkan for -L / rpath.
 *  Prefer CMake-configured dir (same as Vulkan::Vulkan at configure time), else
 *  VULKAN_SDK/lib, else $HOMEBREW_PREFIX/lib. Never hardcode install prefixes. */
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
/** Full path to vulkan-1.lib for packaged yona_runtime (gpu_stub vk* imports).
 *  Prefer CMake-configured path (same as Vulkan::Vulkan at configure time), else VULKAN_SDK. */
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
  const char *cands[] = {"Lib/vulkan-1.lib", "Lib32/vulkan-1.lib", "lib/vulkan-1.lib"};
  for (const char *rel : cands) {
    path cand = path(sdk) / rel;
    if (exists(cand))
      return cand.string();
  }
  return {};
}
#endif

/** Optional GPU Vulkan compile flags for scratch compiled_runtime.c (env
 *  YONA_COMPILE_GPU_VULKAN=1 + VULKAN_SDK — matches test/yona_link_util).
 *  Packaged sysroot objects are built by CMake with -DYONA_HAS_VULKAN when
 *  find_package(Vulkan) succeeds; yonac links user exes with -L (configured
 *  lib dir) -lvulkan (Unix) or CMake-resolved / VULKAN_SDK vulkan-1.lib
 *  (Windows — see yona_vulkan_link_cfg.h). */
static string yona_runtime_vulkan_cflags() {
  const char *on = getenv("YONA_COMPILE_GPU_VULKAN");
  if (!on || string(on) == "0")
    return "";
  const char *sdk = getenv("VULKAN_SDK");
  if (!sdk || !*sdk)
    return "";
  std::filesystem::path inc = std::filesystem::path(sdk) / "Include";
  if (!std::filesystem::exists(inc / "vulkan" / "vulkan.h"))
    inc = std::filesystem::path(sdk) / "include";
  if (!std::filesystem::exists(inc / "vulkan" / "vulkan.h"))
    return "";
  return string(" -DYONA_COMPILE_GPU_VULKAN=1 -I") + q_cmd_path(inc);
}

static string llvm_link_executable() {
#ifdef _WIN32
  const char *tool = "llvm-link.exe";
#else
  const char *tool = "llvm-link";
#endif
  const char *cc = getenv("YONAC_CC");
  if (!cc || !*cc)
    return tool;
  filesystem::path cc_path(cc);
  if (!cc_path.has_parent_path())
    return tool;
  return (cc_path.parent_path() / tool).string();
}

static const char *const platform_runtime_sources[] = {
#ifdef _WIN32
    "file_windows.c",
    "net_windows.c",
    "os_windows.c",
#elif defined(__APPLE__)
    "kqueue_macos.c",
    "file_macos.c",
    "net_macos.c",
    "os_macos.c",
#else
    "uring_linux.c",
    "file_linux.c",
    "net_linux.c",
    "os_linux.c",
#endif
};

static vector<filesystem::path> embedded_runtime_sources(const filesystem::path &root) {
  vector<filesystem::path> sources = {
      root / "src" / "compiled_runtime.c",
      root / "src" / "runtime" / "seq.c",
      root / "src" / "runtime" / "hamt.c",
      root / "src" / "runtime" / "exceptions.c",
      root / "src" / "runtime" / "closures.c",
      root / "src" / "runtime" / "gpu_vulkan.c",
      root / "src" / "runtime" / "gpu_vulkan_device.c",
      root / "src" / "runtime" / "gpu_vulkan_compute.c",
      root / "src" / "runtime" / "gpu_vulkan_ops.c",
      root / "src" / "runtime" / "gpu_cpu.c",
#ifdef YONAC_EXE_LINK_PCRE2
      root / "src" / "runtime" / "regex.c",
#endif
#ifdef _WIN32
      root / "src" / "runtime" / "platform" / "async_win32.c",
      root / "src" / "runtime" / "platform" / "channel_win32.c",
#else
      root / "src" / "runtime" / "platform" / "async_posix.c",
      root / "src" / "runtime" / "platform" / "channel_posix.c",
#endif
  };
  for (const char *pf : platform_runtime_sources)
    sources.push_back(root / "src" / "runtime" / "platform" / pf);
  return sources;
}

/** A pinned PCRE2 fallback is copied next to the packaged runtime archive.
 * Keep this lookup relative to the selected sysroot so installed Windows
 * compilers never depend on a package manager or a build-tree path. */
static string find_packaged_pcre2_archive(const vector<filesystem::path> &sysroots) {
  for (const auto &root : sysroots) {
    for (const auto &base : {root / "runtime", root / "lib" / "yona" / "runtime"}) {
      for (const auto *name : {"yona_pcre2.lib", "yona_pcre2.a"}) {
        const auto candidate = base / name;
        std::error_code error;
        if (filesystem::exists(candidate, error) && !error)
          return filesystem::weakly_canonical(candidate, error).string();
      }
    }
  }
  return {};
}

static bool artifact_stale_against_sources(const filesystem::path &artifact, const vector<filesystem::path> &sources) {
  if (!filesystem::exists(artifact))
    return true;
  auto artifact_time = filesystem::last_write_time(artifact);
  for (const auto &source : sources) {
    if (filesystem::exists(source) && filesystem::last_write_time(source) > artifact_time)
      return true;
  }
  return false;
}

static filesystem::path canonical_if_exists(const filesystem::path &p) {
  std::error_code ec;
  if (!filesystem::exists(p, ec))
    return {};
  auto c = filesystem::weakly_canonical(p, ec);
  return ec ? p : c;
}

static bool run_overlay_checkers(ast::AstNode *root, DiagnosticEngine &diag, typechecker::TypeChecker &tc,
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
    if (i) names += ", ";
    names += missing[i];
  }
  return names;
}

static bool collect_incomplete_cases(ast::AstNode *node, Codegen &codegen,
                                     DiagnosticEngine &diag) {
  if (!node) return true;

  bool ok = true;
  const auto walk = [&](ast::AstNode *child) {
    if (!collect_incomplete_cases(child, codegen, diag)) ok = false;
  };

  switch (node->get_type()) {
  case ast::AST_MAIN:
    walk(static_cast<ast::MainNode *>(node)->node);
    break;
  case ast::AST_MODULE_DECL: {
    auto *module = static_cast<ast::ModuleDecl *>(node);
    for (auto *function : module->functions) walk(function);
    for (auto *trait : module->trait_declarations)
      for (const auto &method : trait->methods) walk(method.default_impl);
    for (auto *instance : module->instance_declarations)
      for (auto *method : instance->methods) walk(method);
    for (auto *external : module->extern_declarations) walk(external->body);
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
      diag.error(case_expr->source_context, ErrorCode::E0203,
                 "`--require-effect-free` requires an exhaustive match on " +
                     coverage->adt_name + "; missing constructor" +
                     (coverage->missing.size() == 1 ? " " : "s ") +
                     format_missing_constructors(coverage->missing));
      ok = false;
    }
    walk(case_expr->expr);
    for (auto *clause : case_expr->clauses) {
      if (!clause) continue;
      walk(clause->guard);
      walk(clause->body);
    }
    break;
  }
  case ast::AST_LET_EXPR: {
    auto *let_expr = static_cast<ast::LetExpr *>(node);
    for (auto *alias : let_expr->aliases) {
      if (auto *value = dynamic_cast<ast::ValueAlias *>(alias)) walk(value->expr);
      else if (auto *lambda = dynamic_cast<ast::LambdaAlias *>(alias)) walk(lambda->lambda);
      else if (auto *pattern = dynamic_cast<ast::PatternAlias *>(alias)) walk(pattern->expr);
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
    for (auto *step : static_cast<ast::DoExpr *>(node)->steps) walk(step);
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
      if (clause) walk(clause->body);
    break;
  }
  case ast::AST_TRY_CATCH_EXPR: {
    auto *try_catch = static_cast<ast::TryCatchExpr *>(node);
    walk(try_catch->tryExpr);
    for (auto *catch_pattern : try_catch->catchExpr->patterns) {
      if (!catch_pattern) continue;
      std::visit([&](auto &body) {
        using Body = std::remove_cvref_t<decltype(body)>;
        if constexpr (std::is_same_v<Body, ast::PatternWithoutGuards *>) {
          walk(body->expr);
        } else {
          for (auto *guarded : body) {
            walk(guarded->guard);
            walk(guarded->expr);
          }
        }
      }, catch_pattern->pattern);
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
    for (auto *value : static_cast<ast::TupleExpr *>(node)->values) walk(value);
    break;
  case ast::AST_DICT_EXPR:
    for (const auto &[key, value] : static_cast<ast::DictExpr *>(node)->values) {
      walk(key);
      walk(value);
    }
    break;
  case ast::AST_VALUES_SEQUENCE_EXPR:
    for (auto *value : static_cast<ast::ValuesSequenceExpr *>(node)->values) walk(value);
    break;
  case ast::AST_RANGE_SEQUENCE_EXPR: {
    auto *range = static_cast<ast::RangeSequenceExpr *>(node);
    walk(range->start);
    walk(range->end);
    walk(range->step);
    break;
  }
  case ast::AST_SET_EXPR:
    for (auto *value : static_cast<ast::SetExpr *>(node)->values) walk(value);
    break;
  case ast::AST_RECORD_INSTANCE_EXPR:
    for (const auto &[_, value] : static_cast<ast::RecordInstanceExpr *>(node)->items) walk(value);
    break;
  case ast::AST_RECORD_LITERAL_EXPR:
    for (const auto &[_, value] : static_cast<ast::RecordLiteralExpr *>(node)->fields) walk(value);
    break;
  case ast::AST_FIELD_UPDATE_EXPR:
    for (const auto &[_, value] : static_cast<ast::FieldUpdateExpr *>(node)->updates) walk(value);
    break;
  case ast::AST_BINARY_OP_EXPR:
  case ast::AST_ADD_EXPR: case ast::AST_SUBTRACT_EXPR: case ast::AST_MULTIPLY_EXPR:
  case ast::AST_DIVIDE_EXPR: case ast::AST_MODULO_EXPR: case ast::AST_POWER_EXPR:
  case ast::AST_EQ_EXPR: case ast::AST_NEQ_EXPR: case ast::AST_LT_EXPR: case ast::AST_LTE_EXPR:
  case ast::AST_GT_EXPR: case ast::AST_GTE_EXPR: case ast::AST_LOGICAL_AND_EXPR:
  case ast::AST_LOGICAL_OR_EXPR: case ast::AST_PIPE_RIGHT_EXPR: case ast::AST_PIPE_LEFT_EXPR:
  case ast::AST_IN_EXPR: case ast::AST_CONS_LEFT_EXPR: case ast::AST_CONS_RIGHT_EXPR:
  case ast::AST_JOIN_EXPR: case ast::AST_REMOVE_EXPR: case ast::AST_LEFT_SHIFT_EXPR:
  case ast::AST_RIGHT_SHIFT_EXPR: case ast::AST_ZEROFILL_RIGHT_SHIFT_EXPR:
  case ast::AST_BITWISE_AND_EXPR: case ast::AST_BITWISE_OR_EXPR: case ast::AST_BITWISE_XOR_EXPR: {
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

static string termination_repair_note(const termination_analysis::Failure &failure) {
  if (failure.reason == "recursive component members have incompatible arity") {
    return "Repair: give every function in the recursive component the same number of parameters, "
           "or reshape the recursive component into SCCs with compatible arity";
  }
  if (failure.reason.find("no provable lexicographic structural descent") != string::npos) {
    return "Repair: recurse on a constructor field or non-empty sequence tail bound by an unguarded "
           "`case` arm, preserving the decreasing parameter position across the cycle "
           "(for example, `Succ rest -> loop rest`)";
  }
  return "Repair: expose direct local recursive calls with a statically visible structural decrease; "
         "opaque helper and higher-order calls are not inspected by this proof";
}

static bool require_structural_termination(ast::AstNode *root, DiagnosticEngine &diag) {
  if (!root) return true;
  const auto result = termination_analysis::analyze(*root);
  for (const auto &failure : result.failures) {
    diag.error(failure.call_location, ErrorCode::E0203,
               "`--require-effect-free` cannot prove structural termination for recursive component '" +
                   failure.component + "' at call '" + failure.caller + " -> " + failure.callee + "': " +
                   failure.reason + ". " + termination_repair_note(failure));
  }
  return result.failures.empty();
}

static bool require_effect_free(ast::AstNode *root, DiagnosticEngine &diag,
                                typechecker::TypeChecker &tc, Codegen &codegen) {
  bool ok = true;
  if (auto *mod = dynamic_cast<ast::ModuleDecl *>(root)) {
    for (auto *func : mod->functions) {
      if (!func || tc.is_effect_free(tc.type_of(func))) continue;
      diag.error(func->source_context, ErrorCode::E0203,
                 "`--require-effect-free` requires '" + func->name +
                     "' to have a closed empty effect row. Repair: remove or handle the open effect source; "
                     "opaque and higher-order calls must have a closed empty row independently of recursion shape");
      ok = false;
    }
  }
  for (const auto &loc : tc.unhandled_effect_locations()) {
    diag.error(loc, ErrorCode::E0203,
               "`--require-effect-free` rejects an unhandled effect operation");
    ok = false;
  }
  if (tc.has_unknown_effect_rows()) {
    diag.error(SourceLocation::unknown(), ErrorCode::E0203,
               "`--require-effect-free` cannot prove an imported function's effect row; "
               "rebuild its interface to record `effects -`");
    ok = false;
  }
  if (!collect_incomplete_cases(root, codegen, diag)) ok = false;
  if (!require_structural_termination(root, diag)) ok = false;
  return ok;
}

static vector<filesystem::path> discover_sysroots(const char *argv0, const string &sysroot_opt) {
  return yona::toolchain::discover_sysroots(argv0, sysroot_opt);
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
  app.add_option("-I,--include", last_include_path, "Module search paths (for .yonai files)")
      ->take_last()
      ->each([&include_paths](string path) { include_paths.push_back(std::move(path)); });
  app.add_option("--sysroot", sysroot_path, "Yona distribution root (used to find lib/ and runtime objects)");
  app.add_option("--linker-mode", linker_mode_opt, "Linker mode: auto|bundled|system|inprocess (also via YONAC_LINKER_MODE)");
  app.add_option("-O", opt_level, "Optimization level (0-3, default 2)")->check(CLI::Range(0, 3));
  app.add_flag("--emit-ir", emit_ir, "Print LLVM IR instead of compiling");
  app.add_flag("--emit-obj", emit_obj, "Emit object file only (don't link)");
  app.add_flag("--emit-typed-core", emit_typed_core,
               "Print a typed-core dump (resolved names, types, effects, "
               "linearity, spans) and exit without LLVM codegen");
  app.add_flag("--emit-accelerator-report", emit_accelerator_report,
               "Print JSON of Std\\GPU-shaped call sites and exit (no codegen): "
               "expression programs after typecheck; modules from AST scan by default");
  app.add_flag("--emit-accelerator-report-with-types", emit_accelerator_report_with_types,
               "With --emit-accelerator-report on a module, run the typechecker first "
               "(JSON report_kind \"module\", optional inferred_type per site)");
  app.add_flag("--no-accelerator-lowering", no_accelerator_lowering,
               "Keep IntArray/FloatArray map/filter/foldl on the host closure path "
               "(do not rewrite recognized kernels to the Std\\GPU ABI)");
  app.add_flag("--strict-accelerator", strict_accelerator,
               "Error (E0700) on IntArray/FloatArray map/filter/foldl lambdas "
               "outside the fixed Std\\GPU kernel library (no silent host fallback)");
  app.add_flag("--Wall", flag_wall, "Enable common warnings");
  app.add_flag("--Wextra", flag_wextra, "Enable all warnings");
  app.add_flag("--Werror", flag_werror, "Treat warnings as errors");
  app.add_flag("-w", flag_w, "Suppress all warnings");
  app.add_flag("--Wincomplete-patterns", flag_incomplete_patterns,
               "Warn when a finite ADT case misses constructors");
  app.add_flag("--Woverlapping-patterns", flag_overlapping_patterns,
               "Warn when a case arm is unreachable after an earlier unguarded arm");
  app.add_flag("--Wno-refinement", flag_no_refinement,
               "Skip refinement checking (E0500 nonempty/nonzero proofs)");
  app.add_flag("--Wno-linear", flag_no_linear,
               "Skip linearity checking (E0600/E0601/E0602)");
  app.add_flag("--Wno-linear-leak", flag_no_linear_leak,
               "Disable E0602 resource-leak warnings (-Wlinear-leak)");
  app.add_flag("--require-effect-free", flag_require_effect_free,
               "Require closed empty effect rows, finite case coverage, and conservative structural size-change "
               "proofs for local recursive SCCs (not a global termination proof)");
  app.add_flag("-g,--debug", flag_debug, "Emit DWARF debug information");
  app.add_option("--explain", explain_code, "Show detailed explanation for an error code (e.g., E0100)");

  CLI11_PARSE(app, argc, argv);

  if (emit_typed_core && emit_ir) {
    cerr << "Error: --emit-typed-core cannot be combined with --emit-ir" << endl;
    return 1;
  }
  if (emit_typed_core && emit_obj) {
    cerr << "Error: --emit-typed-core cannot be combined with --emit-obj" << endl;
    return 1;
  }
  if (emit_typed_core && emit_accelerator_report) {
    cerr << "Error: --emit-typed-core cannot be combined with --emit-accelerator-report" << endl;
    return 1;
  }
  if (emit_accelerator_report && emit_ir) {
    cerr << "Error: --emit-accelerator-report cannot be combined with --emit-ir" << endl;
    return 1;
  }
  if (emit_accelerator_report && emit_obj) {
    cerr << "Error: --emit-accelerator-report cannot be combined with --emit-obj" << endl;
    return 1;
  }
  if (emit_accelerator_report_with_types && !emit_accelerator_report) {
    cerr << "Error: --emit-accelerator-report-with-types requires --emit-accelerator-report" << endl;
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
    cerr << "Error: no input. Use 'yonac file.yona' or 'yonac -' (stdin)" << endl;
    return 1;
  }

  bool is_module = is_module_source(source);
  if (emit_accelerator_report_with_types && !is_module) {
    cerr << "Error: --emit-accelerator-report-with-types is only for module sources" << endl;
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
  diag.set_source(source, filename);
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

  // Codegen
  string module_name = is_module ? "yona_module" : "yona_program";
  Codegen codegen(module_name, &diag);

  if (flag_debug)
    codegen.set_debug_info(true, filename);
  codegen.set_opt_level(opt_level);
  codegen.set_accelerator_lowering(!no_accelerator_lowering);
  codegen.set_strict_accelerator(strict_accelerator && !no_accelerator_lowering);

  vector<filesystem::path> sysroots = discover_sysroots(argc > 0 ? argv[0] : nullptr, sysroot_path);
  yona::toolchain::LinkerPlan linker_selection;
  string linker_mode_raw = linker_mode_opt;
  if (linker_mode_raw.empty()) {
    if (const char *env_mode = getenv("YONAC_LINKER_MODE")) {
      if (*env_mode)
        linker_mode_raw = env_mode;
    }
  }
  string linker_error;
  if (!yona::toolchain::resolve_linker_plan(linker_mode_raw, sysroots, linker_selection, linker_error)) {
    cerr << "Error: " << linker_error << endl;
    return 1;
  }
  const bool require_inprocess = yona::toolchain::require_inprocess_lld_from_env();
  if (linker_selection.use_inprocess_lld && !yona::toolchain::inprocess_lld_available()) {
    if (require_inprocess) {
      cerr << "Error: inprocess linker mode required but unavailable: " << yona::toolchain::inprocess_lld_unavailable_reason() << endl;
      return 1;
    }
    cerr << "Warning: inprocess linker mode requested but unavailable: " << yona::toolchain::inprocess_lld_unavailable_reason()
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
      codegen.module_paths_.push_back(s);
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
  // Backward-compatible relative probing.
  for (auto &candidate : {"lib", "../lib", "../../lib", "../../../lib"}) {
    auto c = canonical_if_exists(filesystem::path(candidate));
    if (!c.empty() && filesystem::exists(c / "Prelude.yonai")) {
      add_module_path(c);
      break;
    }
  }

  if (emit_typed_core) {
    vector<const char *> tc_paths;
    tc_paths.reserve(codegen.module_paths_.size());
    for (const auto &p : codegen.module_paths_)
      tc_paths.push_back(p.c_str());
    YonaTcModule *tc = yona_tc_analyze(source.c_str(), filename.c_str(),
                                       tc_paths.empty() ? nullptr : tc_paths.data(), tc_paths.size());
    if (!tc) {
      cerr << "Error: typed-core analysis failed" << endl;
      return 1;
    }
    char *text = yona_tc_pretty_print(tc);
    if (text) {
      cout << text;
      yona_tc_string_free(text);
    }
    yona_tc_module_free(tc);
    return 0;
  }

  llvm::Module *llvm_mod = nullptr;

  if (is_module) {
    parser::Parser parser;
    if (emit_accelerator_report && emit_accelerator_report_with_types) {
      typechecker::TypeChecker type_checker(diag);
      codegen.load_prelude(&parser, &type_checker);
      type_checker.set_import_type_source(&codegen.import_types_);
      auto result = parser.parse_module(source, filename);
      if (!result.has_value()) {
        for (auto &e : result.error())
          diag.error(e.location, compiler::ErrorCode::E0301, e.message);
        return 1;
      }
      if (!typecheck_module_for_accelerator_report(result.value().get(), type_checker))
        return 1;
      emit_accelerator_diagnostic_report_for_module(std::cout, result.value().get(), filename, &type_checker);
      return 0;
    }
    typechecker::TypeChecker type_checker(diag);
    type_checker.set_require_effect_free(flag_require_effect_free);
    codegen.load_prelude(&parser, &type_checker);
    for (auto &p : codegen.module_paths_)
      type_checker.add_module_path(p);
    type_checker.set_import_type_source(&codegen.import_types_);
    auto result = parser.parse_module(source, filename);
    if (!result.has_value()) {
      for (auto &e : result.error())
        diag.error(e.location, compiler::ErrorCode::E0301, e.message);
      return 1;
    }
    if (emit_accelerator_report) {
      emit_accelerator_diagnostic_report_for_module(std::cout, result.value().get(), filename);
      return 0;
    }
    type_checker.check_module(result.value().get());
    if (!type_checker.solve_constraints() || type_checker.has_errors())
      return 1;
    if (!run_overlay_checkers(result.value().get(), diag, type_checker, flag_no_refinement, flag_no_linear))
      return 1;
    codegen.set_type_checker(&type_checker);
    llvm_mod = codegen.compile_module(result.value().get());
    // compile_module registers declarations local to this module, allowing the
    // strict totality gate to cover both prelude/imported and local finite ADTs.
    if (flag_require_effect_free &&
        !require_effect_free(result.value().get(), diag, type_checker, codegen))
      return 1;
    if (llvm_mod)
      codegen.populate_interface_effect_rows(result.value().get(), type_checker);
  } else {
    parser::Parser parser;
    typechecker::TypeChecker type_checker(diag);
    type_checker.set_require_effect_free(flag_require_effect_free);
    codegen.load_prelude(&parser, &type_checker); // registers everything
    for (auto& p : codegen.module_paths_)
      type_checker.add_module_path(p);

    istringstream stream(source);
    auto parse_result = parser.parse_input(stream);
    if (!parse_result.node) {
      auto result = parser.parse_expression(source, filename);
      if (!result.has_value()) {
        for (auto &e : result.error())
          diag.error(e.location, compiler::ErrorCode::E0301, e.message);
      } else {
        diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0301, "parse error");
      }
      return 1;
    }

    type_checker.set_import_type_source(&codegen.import_types_);
    auto *checked_type = type_checker.check(parse_result.node.get());
    if (flag_require_effect_free) {
      bool gate_ok = require_effect_free(parse_result.node.get(), diag, type_checker, codegen);
      if (!type_checker.is_effect_free(checked_type) && !type_checker.has_unknown_effect_rows()) {
        diag.error(parse_result.node->source_context, ErrorCode::E0203,
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
      emit_accelerator_diagnostic_report(std::cout, parse_result.node.get(), &type_checker, filename);
      return 0;
    }

    if (!run_overlay_checkers(parse_result.node.get(), diag, type_checker, flag_no_refinement, flag_no_linear))
      return 1;

    llvm_mod = codegen.compile(parse_result.node.get());
  }

  if (!llvm_mod) {
    // Errors already printed by DiagnosticEngine
    return 1;
  }
  // Codegen may still produce a verifiable module after E0104/etc.; do not
  // link a binary that would return the wrong result with exit 0.
  if (codegen.error_count_ > 0 || diag.has_errors())
    return 1;

  // Print summary if there were warnings
  if (diag.warning_count() > 0) {
    cerr << diag.warning_count() << " warning" << (diag.warning_count() != 1 ? "s" : "") << " generated." << endl;
  }

  if (emit_ir) {
    cout << codegen.emit_ir();
    return 0;
  }

  // Emit object file
  string obj_file = (is_module || emit_obj) ? output_file : (output_file + ".o");
  if (!codegen.emit_object_file(obj_file)) {
    diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0400, "failed to emit object file");
    return 1;
  }
  if (codegen.error_count_ > 0 || diag.has_errors())
    return 1;

  // For modules, also emit interface file (.yonai)
  if (is_module) {
    auto yonai_path = filesystem::path(output_file).replace_extension(".yonai");
    if (!codegen.emit_interface_file(yonai_path.string()) ||
        codegen.error_count_ > 0 || diag.has_errors())
      return 1;
    return 0;
  }

  if (emit_obj)
    return 0;

  // Link expression into executable.
  auto exe_dir = canonical_if_exists(filesystem::path(argv[0]).parent_path());
  if (exe_dir.empty())
    exe_dir = filesystem::current_path();
  string rt_obj = (exe_dir / "compiled_runtime.o").string();
  string rt_bc = (exe_dir / "compiled_runtime.bc").string();
  bool rt_obj_is_archive = false;
  vector<string> rt_extra_objs; /* platform .o files linked alongside rt_obj */

  auto find_packaged_runtime_objects = [&]() -> bool {
    for (const auto &root : sysroots) {
      for (const auto &base : {root / "runtime", root / "lib" / "yona" / "runtime"}) {
        for (const auto &archive_name : {"yona_runtime.lib", "libyona_runtime.lib", "libyona_runtime.a"}) {
          auto archive = canonical_if_exists(base / archive_name);
          if (!archive.empty()) {
            rt_obj = archive.string();
            rt_obj_is_archive = true;
            rt_extra_objs.clear();
            return true;
          }
        }
        auto main_o = canonical_if_exists(base / "compiled_runtime.o");
        if (main_o.empty())
          continue;
        rt_obj = main_o.string();
        rt_obj_is_archive = false;
        rt_extra_objs.clear();
        for (const char *pf : platform_runtime_sources) {
          auto a = canonical_if_exists(base / ("crt_" + string(pf) + ".o"));
          auto b = canonical_if_exists(base / (string(pf) + ".o"));
          if (!a.empty())
            rt_extra_objs.push_back(a.string());
          else if (!b.empty())
            rt_extra_objs.push_back(b.string());
        }
        return true;
      }
    }
    return false;
  };

  bool have_packaged_runtime = find_packaged_runtime_objects();

#ifdef YONAC_EXE_LINK_PCRE2
  const string packaged_pcre2 = find_packaged_pcre2_archive(sysroots);
  // In-process LLD receives an argv vector, not a shell command: keep a raw
  // filesystem path here. The external compiler command below still needs its
  // separately quoted spelling.
  const string pcre2_lld_arg = packaged_pcre2.empty()
      ? "-lpcre2-8"
      : packaged_pcre2;
#ifdef _WIN32
  if (packaged_pcre2.empty()) {
    diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0401,
               "packaged PCRE2 archive is missing from the runtime sysroot; "
               "rebuild with the pinned Std\\Regex dependency");
    return 1;
  }
  const string pcre2_link_arg = q_cmd_path(filesystem::path(packaged_pcre2));
#else
  const string pcre2_link_arg = packaged_pcre2.empty()
      ? "-lpcre2-8"
      : q_cmd_path(filesystem::path(packaged_pcre2));
#endif
#endif

  // Find runtime source and compile to both .o (for linking) and .bc (for LTO) if needed.
  if (!have_packaged_runtime) {
    rt_obj_is_archive = false;
    for (const auto &root : sysroots) {
      auto candidate = root / "src" / "compiled_runtime.c";
      if (!filesystem::exists(candidate))
        continue;
      filesystem::path src_dir_p = root / "src";
      filesystem::path inc_dir_p = root / "include";
      string i_flags = " -I" + q_cmd_path(src_dir_p) + " -I" + q_cmd_path(inc_dir_p) + yona_runtime_vulkan_cflags();
#ifdef YONAC_EXE_LINK_PCRE2
      i_flags += " -DYONA_EMBEDDED_PCRE2=1";
#endif

      vector<string> plat_pf;
      vector<string> plat_obj_paths;
      for (const char *pf : platform_runtime_sources) {
        auto plat_src = root / "src" / "runtime" / "platform" / pf;
        if (filesystem::exists(plat_src)) {
          plat_pf.push_back(pf);
          plat_obj_paths.push_back((exe_dir / ("crt_" + string(pf) + ".o")).string());
        }
      }

      auto runtime_sources = embedded_runtime_sources(root);
      bool need_rt = artifact_stale_against_sources(filesystem::path(rt_obj), runtime_sources);
      for (size_t i = 0; i < plat_obj_paths.size(); ++i) {
        auto po = filesystem::path(plat_obj_paths[i]);
        auto ps = root / "src" / "runtime" / "platform" / plat_pf[i];
        if (!filesystem::exists(po) || (filesystem::exists(ps) && filesystem::last_write_time(ps) > filesystem::last_write_time(po))) {
          need_rt = true;
          break;
        }
      }

      if (need_rt) {
        const char *cc = yonac_cc_exe();
        string main_cmd = q_cmd_executable(cc) + " -c " + q_cmd_path(candidate) + i_flags + " -o " + q_cmd_path(filesystem::path(rt_obj)) + shell_stderr_null();
        if (run_system_command(std::move(main_cmd)) != 0) {
          cerr << "Error: failed to compile compiled_runtime.c (set YONAC_CC or install clang in PATH)" << endl;
          return 1;
        }
        for (size_t i = 0; i < plat_pf.size(); ++i) {
          auto plat_src = root / "src" / "runtime" / "platform" / plat_pf[i];
          string plat_cmd =
              q_cmd_executable(cc) + " -c " + q_cmd_path(plat_src) + i_flags + " -o " + q_cmd_path(filesystem::path(plat_obj_paths[i])) + shell_stderr_null();
          if (run_system_command(std::move(plat_cmd)) != 0) {
            cerr << "Error: failed to compile runtime platform " << plat_pf[i] << endl;
            return 1;
          }
        }
#ifndef _WIN32
        for (size_t i = 0; i < plat_obj_paths.size(); ++i) {
          string merged = rt_obj + ".merged";
          string merge_cmd = string(cc) + " -r " + q_cmd_path(filesystem::path(rt_obj)) + " " + q_cmd_path(filesystem::path(plat_obj_paths[i])) +
                             " -o " + q_cmd_path(filesystem::path(merged)) + shell_stderr_null();
          if (run_system_command(std::move(merge_cmd)) != 0) {
            cerr << "Error: failed to merge runtime objects" << endl;
            return 1;
          }
          filesystem::remove(rt_obj);
          filesystem::rename(merged, rt_obj);
          filesystem::remove(plat_obj_paths[i]);
        }
#else
        rt_extra_objs = plat_obj_paths;
#endif
      }
#ifdef _WIN32
      else {
        rt_extra_objs = plat_obj_paths;
      }
#endif

      // Compile to LLVM bitcode for LTO (enables runtime function inlining).
      // Merge all runtime sources (main + platform) into one bitcode.
      bool need_bc = artifact_stale_against_sources(filesystem::path(rt_bc), runtime_sources);
      if (need_bc) {
        string bc_main = rt_bc + ".main";
        string bc_cmd = q_cmd_executable(yonac_cc_exe()) + " -emit-llvm -O2 -c " + q_cmd_path(candidate) + i_flags + " -o " +
                        q_cmd_path(filesystem::path(bc_main)) + shell_stderr_null();
        run_system_command(std::move(bc_cmd));

        vector<string> bc_files = {bc_main};
        for (const char *pf : platform_runtime_sources) {
          auto plat_src = root / "src" / "runtime" / "platform" / pf;
          if (filesystem::exists(plat_src)) {
            string plat_bc = rt_bc + "." + string(pf) + ".bc";
            run_system_command(q_cmd_executable(yonac_cc_exe()) + " -emit-llvm -O2 -c " + q_cmd_path(plat_src) + i_flags + " -o " +
                               q_cmd_path(filesystem::path(plat_bc)) + shell_stderr_null());
            bc_files.push_back(plat_bc);
          }
        }
        string link_bc = llvm_link_executable();
        for (const auto &f : bc_files)
          link_bc += " " + q_cmd_path(filesystem::path(f));
        link_bc += " -o " + q_cmd_path(filesystem::path(rt_bc)) + shell_stderr_null();
        run_system_command(std::move(link_bc));
        for (const auto &f : bc_files)
          filesystem::remove(f);
      }
      break;
    }
  }

  // LTO: link runtime bitcode into the module before emitting object code.
  // This enables LLVM to inline seq_head, seq_tail, etc.
  bool lto_active = false;
  bool rt_bc_usable = filesystem::exists(rt_bc);
  if (rt_bc_usable && have_packaged_runtime && filesystem::exists(filesystem::path(rt_obj)) &&
      filesystem::last_write_time(filesystem::path(rt_obj)) > filesystem::last_write_time(filesystem::path(rt_bc))) {
    rt_bc_usable = false;
  }
  if (rt_bc_usable) {
    lto_active = codegen.link_runtime_bitcode(rt_bc);
    if (lto_active) {
      codegen.optimize();
      if (!codegen.emit_object_file(obj_file)) {
        diag.error(SourceLocation::unknown(), "failed to emit LTO object file");
        return 1;
      }
    }
  }

  // Find Prelude.o for linking
  string prelude_obj;
  for (auto &dir : codegen.module_paths_) {
    auto candidate = filesystem::path(dir) / "Prelude.o";
    if (filesystem::exists(candidate)) {
      prelude_obj = candidate.string();
      break;
    }
  }

  // When LTO merged the runtime, don't link rt_obj separately (avoid dups).
  // Unix: -rdynamic exports symbols for backtrace_symbols() stack traces.
  auto append_link_objects = [&](auto &&append_one) {
    append_one(obj_file);
    if (!lto_active) {
      append_one(rt_obj);
      if (!rt_obj_is_archive) {
#ifdef _WIN32
        for (const auto &ex : rt_extra_objs)
          append_one(ex);
#endif
      }
    }
    if (!prelude_obj.empty())
      append_one(prelude_obj);
  };

  int link_result = 1;
  bool used_inprocess = false;
  if (linker_selection.use_inprocess_lld && yona::toolchain::inprocess_lld_available()) {
    vector<string> lld_args;
#ifdef _WIN32
    lld_args.push_back("lld-link");
    lld_args.push_back("/NOLOGO");
    append_link_objects([&](const string &s) { lld_args.push_back(s); });
    lld_args.push_back("/OUT:" + filesystem::path(output_file).string());
    for (const auto &a : yona::toolchain::inprocess_lld_after_input_args())
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
    for (const auto &a : yona::toolchain::inprocess_lld_before_input_args())
      lld_args.push_back(a);
    append_link_objects([&](const string &s) { lld_args.push_back(s); });
    lld_args.push_back("-o");
    lld_args.push_back(filesystem::path(output_file).string());
    for (const auto &a : yona::toolchain::inprocess_lld_after_input_args())
      lld_args.push_back(a);
#endif
#ifdef YONAC_EXE_LINK_PCRE2
    lld_args.push_back(pcre2_lld_arg);
#endif
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
        diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0401, "in-process LLD link failed and fallback is disabled");
        if (!lld_res.diagnostic_text().empty())
          cerr << lld_res.diagnostic_text() << endl;
        return 1;
      }
      cerr << "Warning: in-process LLD link failed, falling back to external linker path.";
      if (!lld_res.diagnostic_text().empty())
        cerr << " details: " << lld_res.diagnostic_text();
      cerr << endl;
      link_result = 1;
    }
  }

  if (!used_inprocess || link_result != 0) {
    const char *cc_link = yonac_cc_exe();
    string link_cmd = q_cmd_executable(cc_link) + " " + q_cmd_path(filesystem::path(obj_file));
    if (linker_selection.use_bundled_lld) {
      link_cmd += " -fuse-ld=lld -B" + q_cmd_path(linker_selection.bundled_lld_path.parent_path());
    }
    if (!lto_active) {
      link_cmd += " " + q_cmd_path(filesystem::path(rt_obj));
      if (!rt_obj_is_archive) {
#ifdef _WIN32
        for (const auto &ex : rt_extra_objs)
          link_cmd += " " + q_cmd_path(filesystem::path(ex));
#endif
      }
    }
    if (!prelude_obj.empty())
      link_cmd += " " + q_cmd_path(filesystem::path(prelude_obj));
#ifdef _WIN32
    link_cmd += " -o " + q_cmd_path(filesystem::path(output_file)) + " -lws2_32 -ldbghelp";
#ifdef YONAC_EXE_LINK_PCRE2
    link_cmd += " " + pcre2_link_arg;
#endif
    {
      string vk_lib = yona_windows_vulkan_import_lib_path();
      if (!vk_lib.empty())
        link_cmd += " " + q_cmd_path(filesystem::path(vk_lib));
    }
#else
#ifdef __APPLE__
    link_cmd += " -lpthread -Wl,-U,_yona_regex_free_code";
#else
    link_cmd += " -lm -lpthread -rdynamic";
#endif
#ifdef YONAC_EXE_LINK_PCRE2
    link_cmd += " " + pcre2_link_arg;
#endif
#ifdef YONAC_EXE_LINK_POSIX_VULKAN
    {
      string vk_dir = yona_posix_vulkan_lib_dir();
      if (!vk_dir.empty()) {
        link_cmd += " -L" + q_cmd_path(filesystem::path(vk_dir));
#ifdef __APPLE__
        link_cmd += " -Wl,-rpath," + q_cmd_path(filesystem::path(vk_dir));
#endif
      }
    }
    link_cmd += " -lvulkan";
#endif
    link_cmd += " -o " + q_cmd_path(filesystem::path(output_file));
#endif
    link_result = run_system_command(std::move(link_cmd));
  }
  filesystem::remove(obj_file);

  if (link_result != 0) {
    diag.error(SourceLocation::unknown(), compiler::ErrorCode::E0401, "linking failed");
    return 1;
  }

  return 0;
}
