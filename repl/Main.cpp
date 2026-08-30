// yona — Yona REPL (compile-and-run)
//
// Reads expressions, compiles to native code via yonac, runs, shows output.

#include "yona/Codegen/Codegen.h"
#include "yona/Support/Process.h"
#include "yona/Syntax/Parser.h"
#include "yona/Toolchain/InProcessLld.h"
#include "yona/Toolchain/LinkerPlan.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace fs = std::filesystem;
using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::flush;
using std::getline;
using std::string;
using std::vector;

static fs::path repl_temp_dir() {
  if (const char *t = getenv("TMPDIR"))
    return fs::path(t);
  if (const char *t = getenv("TEMP"))
    return fs::path(t);
  if (const char *t = getenv("TMP"))
    return fs::path(t);
  return fs::temp_directory_path();
}

static const char *repl_cc() {
  if (const char *e = getenv("YONAC_CC"))
    if (*e)
      return e;
#ifdef _WIN32
  return "clang";
#else
  return "cc";
#endif
}

static string exe_suffix() {
#ifdef _WIN32
  return ".exe";
#else
  return "";
#endif
}

static fs::path canonical_if_exists(const fs::path &p) {
  std::error_code ec;
  if (!fs::exists(p, ec))
    return {};
  auto c = fs::weakly_canonical(p, ec);
  return ec ? p : c;
}

static vector<fs::path> discoverSysroots(const char *argv0) {
  return yona::toolchain::discoverSysroots(argv0);
}

static string
compile_and_run(const string &expr, const fs::path &runtime_archive,
                const vector<fs::path> &runtime_dependencies,
                const yona::toolchain::LinkerPlan &linker_selection,
                bool require_inprocess) {
  Codegen codegen("yona_repl");
  parser::Parser parser;
  auto parse_result = parser.parseExpression(expr, "<repl>");
  if (!parse_result || !parse_result->Expression)
    return "Parse error";

  auto *llvm_mod = codegen.compile(parse_result->Expression.get());
  if (!llvm_mod)
    return ""; // errors already printed

  fs::path base = repl_temp_dir();
  fs::path obj = base / "yona_repl.o";
  fs::path exe = base / ("yona_repl" + exe_suffix());
  if (!codegen.emit_object_file(obj.string()))
    return "Codegen error";

  int link_result = 1;
  bool used_inprocess = false;
  if (linker_selection.UseInProcessLld &&
      yona::toolchain::inProcessLldAvailable()) {
    std::vector<std::string> lld_args;
#ifdef _WIN32
    lld_args.push_back("lld-link");
    lld_args.push_back("/NOLOGO");
    lld_args.push_back(obj.string());
    lld_args.push_back(runtime_archive.string());
    for (const auto &dependency : runtime_dependencies)
      lld_args.push_back(dependency.string());
    lld_args.push_back("/OUT:" + exe.string());
    for (const auto &a : yona::toolchain::inProcessLldAfterInputArgs())
      lld_args.push_back(a);
#else
#ifdef __APPLE__
    lld_args.push_back("ld64.lld");
#else
    lld_args.push_back("ld.lld");
#endif
    for (const auto &a : yona::toolchain::inProcessLldBeforeInputArgs())
      lld_args.push_back(a);
    lld_args.push_back(obj.string());
    lld_args.push_back(runtime_archive.string());
    for (const auto &dependency : runtime_dependencies)
      lld_args.push_back(dependency.string());
    lld_args.push_back("-o");
    lld_args.push_back(exe.string());
    for (const auto &a : yona::toolchain::inProcessLldAfterInputArgs())
      lld_args.push_back(a);
#endif
    yona::toolchain::InProcessLldResult lld_res;
    used_inprocess = true;
    if (yona::toolchain::run_inprocess_lld(lld_args, lld_res)) {
      link_result = 0;
    } else {
      if (require_inprocess) {
        if (!lld_res.diagnostic_text().empty())
          cerr << lld_res.diagnostic_text() << endl;
        return "Link error";
      }
      cerr << "Warning: in-process LLD link failed in REPL, falling back to "
              "external linker path.";
      if (!lld_res.diagnostic_text().empty())
        cerr << " details: " << lld_res.diagnostic_text();
      cerr << endl;
    }
  }
  if (!used_inprocess || link_result != 0) {
    vector<string> link_args = {obj.string(), runtime_archive.string()};
    if (linker_selection.UseBundledLld) {
      link_args.push_back("-fuse-ld=lld");
      link_args.push_back(
          "-B" + linker_selection.BundledLldPath.parent_path().string());
    }
    for (const auto &dependency : runtime_dependencies)
      link_args.push_back(dependency.string());
    link_args.push_back("-o");
    link_args.push_back(exe.string());
#ifdef _WIN32
    link_args.push_back("-lws2_32");
    link_args.push_back("-ldbghelp");
#elif defined(__APPLE__)
    link_args.push_back("-lpthread");
    link_args.push_back("-Wl,-U,_yona_regex_free_code");
#else
    link_args.push_back("-lm");
    link_args.push_back("-lpthread");
    link_args.push_back("-rdynamic");
#endif
    const auto link_result = yona::support::executeProcess(
        repl_cc(), link_args, {.SuppressStderr = true});
    if (link_result.ExecutionFailed || link_result.ExitCode != 0) {
      fs::remove(obj);
      return "Link error";
    }
  }
  fs::remove(obj);

  const auto run_result =
      yona::support::executeProcess(exe, {}, {.CaptureStdout = true});
  fs::remove(exe);
  if (run_result.ExecutionFailed || run_result.ExitCode != 0)
    return "Exec error";
  string output = run_result.StandardOutput;

  // Trim trailing newline
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
    output.pop_back();
  return output;
}

int main(int argc, char *argv[]) {
  vector<fs::path> sysroots = discoverSysroots(argc > 0 ? argv[0] : nullptr);
  yona::toolchain::LinkerPlan linker_selection;
  string linker_error;
  string linker_mode_raw = "auto";
  if (const char *env_mode = getenv("YONAC_LINKER_MODE")) {
    if (*env_mode)
      linker_mode_raw = env_mode;
  }
  if (!yona::toolchain::resolveLinkerPlan(linker_mode_raw, sysroots,
                                            linker_selection, linker_error)) {
    cerr << "Error: " << linker_error << endl;
    return 1;
  }
  const bool require_inprocess =
      yona::toolchain::requireInProcessLldFromEnv();
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
  fs::path runtime_archive;
  vector<fs::path> runtime_dependencies;

#ifdef _WIN32
  constexpr const char *RuntimeArchiveName = "yona_runtime.lib";
  constexpr const char *Pcre2ArchiveName = "yona_pcre2.lib";
#else
  constexpr const char *RuntimeArchiveName = "libyona_runtime.a";
  constexpr const char *Pcre2ArchiveName = "yona_pcre2.a";
#endif

  // Consume only the canonical runtime archive from distribution roots.
  for (const auto &root : sysroots) {
    for (const auto &base :
         {root / "runtime", root / "lib" / "yona" / "runtime"}) {
      auto archive = canonical_if_exists(base / RuntimeArchiveName);
      if (!archive.empty()) {
        runtime_archive = archive;
        auto dependency = canonical_if_exists(base / Pcre2ArchiveName);
        if (!dependency.empty())
          runtime_dependencies.push_back(dependency);
      }
      if (!runtime_archive.empty())
        break;
    }
    if (!runtime_archive.empty())
      break;
  }

  if (runtime_archive.empty()) {
    cerr << "Error: canonical Yona runtime archive was not found; "
            "build or install yona_runtime"
         << endl;
    return 1;
  }

  cout << "Yona REPL (type expressions, Ctrl-D to exit)" << endl;

  string line;
  while (true) {
    cout << "yona> " << flush;
    if (!getline(cin, line))
      break;
    if (line.empty())
      continue;
    if (line == ":q" || line == ":quit")
      break;

    string result = compile_and_run(line, runtime_archive, runtime_dependencies,
                                    linker_selection, require_inprocess);
    if (!result.empty())
      cout << result << endl;
  }

  return 0;
}
