#ifndef YONA_TEST_TOOLCHAIN_YONALINKUTIL_H
#define YONA_TEST_TOOLCHAIN_YONALINKUTIL_H

/* Portable link helpers for generated Yona fixtures. CMake builds the runtime
 * once into the canonical archive used by every generated program. */

#include "Support/RepoPaths.h"
#include "yona/Runtime/Generated/VulkanLinkConfig.h"
#include "yona/Support/Process.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yona::test::link {

inline std::filesystem::path scratch_root() {
  auto p = yona::test::repo_root() / "test" / "_scratch";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p;
}

inline const char *cc() {
  const char *e = std::getenv("YONAC_CC");
  if (e && *e)
    return e;
#ifdef _WIN32
  return "clang";
#else
  return "cc";
#endif
}

inline std::string trim_cmd_line(std::string s) {
  while (!s.empty() &&
         (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
    s.pop_back();
  return s;
}

inline std::string discovered_homebrew_prefix() {
  if (const char *p = std::getenv("HOMEBREW_PREFIX"); p && *p)
    return p;
#ifndef _WIN32
  const auto Result = yona::support::executeProcess(
      "brew", {"--prefix"}, {.SuppressStderr = true, .CaptureStdout = true});
  if (!Result.ExecutionFailed && Result.ExitCode == 0)
    return trim_cmd_line(Result.StandardOutput);
  return "";
#else
  return "";
#endif
}

/* Path to the runtime archive supplied by the build graph. */
inline std::filesystem::path runtime_archive_path() {
#ifdef YONA_TEST_RUNTIME_ARCHIVE
  return std::filesystem::path(YONA_TEST_RUNTIME_ARCHIVE);
#else
  return {};
#endif
}

inline void runtime_object_paths(std::vector<std::filesystem::path> &out) {
  out.clear();
  const auto archive = runtime_archive_path();
  if (!archive.empty())
    out.push_back(archive);
}

inline bool ensure_runtime_objects() {
  const auto archive = runtime_archive_path();
  return !archive.empty() && std::filesystem::is_regular_file(archive);
}

inline bool append_runtime_objects(std::vector<std::filesystem::path> &objs) {
  if (!ensure_runtime_objects())
    return false;
  std::vector<std::filesystem::path> rt;
  runtime_object_paths(rt);
  objs.insert(objs.end(), rt.begin(), rt.end());
  return true;
}

inline bool append_prelude_object(std::vector<std::filesystem::path> &objs) {
  const auto prelude = yona::test::prelude_object();
  if (!std::filesystem::exists(prelude))
    return false;
  objs.push_back(prelude);
  return true;
}

inline std::string exe_suffix() {
#ifdef _WIN32
  return ".exe";
#else
  return "";
#endif
}

inline void append_vulkan_link_arguments(std::vector<std::string> &args) {
#if YONA_HAVE_CONFIGURED_VULKAN_IMPORT_LIB
  args.emplace_back(YONA_CONFIGURED_VULKAN_IMPORT_LIB_PATH);
#elif YONA_HAVE_CONFIGURED_VULKAN_LIB_DIR
  const std::string loader_dir = YONA_CONFIGURED_VULKAN_LIB_DIR;
  args.push_back("-L" + loader_dir);
#if defined(__APPLE__)
  args.push_back("-Wl,-rpath," + loader_dir);
#endif
  args.emplace_back("-lvulkan");
#endif
}

inline bool link_objs_to_exe(const std::vector<std::filesystem::path> &objs,
                             const std::filesystem::path &exe_out,
                             const std::vector<std::string> &extra_args = {}) {
  std::vector<std::string> args;
  args.reserve(objs.size() + extra_args.size() + 10);
  for (const auto &o : objs)
    args.push_back(o.string());
  args.push_back("-o");
  args.push_back(exe_out.string());
#ifdef _WIN32
  /* lld-link (Clang's default Windows linker) requires an explicit subsystem.

   * * Embed an asInvoker manifest as well: without requestedExecutionLevel,

   * * Windows' installer-detection heuristic can classify ordinary generated

   * * fixture executables as requiring elevation based on their code/data. */
  args.insert(args.end(),
              {"-lws2_32", "-ldbghelp", "-Xlinker", "/SUBSYSTEM:CONSOLE",
               "-Xlinker", "/MANIFEST:EMBED", "-Xlinker",
               "/MANIFESTUAC:level='asInvoker' uiAccess='false'"});
#elif defined(__APPLE__)
  /* ELF treats undefined weak symbols as NULL; ld64 needs an explicit allow. */
  args.insert(args.end(), {"-lpthread", "-Wl,-U,_yona_regex_free_code"});
#else
  args.insert(args.end(), {"-lm", "-lpthread", "-rdynamic"});
#endif
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  append_vulkan_link_arguments(args);

  const auto QuietResult =
      yona::support::executeProcess(cc(), args, {.SuppressStderr = true});
  if (!QuietResult.ExecutionFailed && QuietResult.ExitCode == 0)
    return true;

  // Fixture failures used to be reported only as LINK_ERROR because the first
  // invocation intentionally hides noisy linker output.  Keep successful
  // runs quiet, but make the failed command actionable on every platform.
  std::cerr << "Yona fixture link failed; rerunning with linker diagnostics:\n";
  (void)yona::support::executeProcess(cc(), args);
  return false;
}

inline std::vector<std::string> splitFlags(std::string_view text) {
  std::vector<std::string> args;
  std::string current;
  char quote = '\0';
  bool escaped = false;
  for (char c : text) {
    if (escaped) {
      current.push_back(c);
      escaped = false;
    } else if (c == '\\' && quote != '\'') {
      escaped = true;
    } else if (quote != '\0') {
      if (c == quote)
        quote = '\0';
      else
        current.push_back(c);
    } else if (c == '\'' || c == '"') {
      quote = c;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!current.empty()) {
        args.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (escaped)
    current.push_back('\\');
  if (!current.empty())
    args.push_back(std::move(current));
  return args;
}

inline std::vector<std::string> pcreLinkArguments() {
  const std::filesystem::path configured_archive(YONA_TEST_PCRE2_ARCHIVE);
  if (!configured_archive.empty() &&
      std::filesystem::exists(configured_archive))
    return {configured_archive.string()};
  const auto PkgConfig = yona::support::executeProcess(
      "pkg-config", {"--libs", "libpcre2-8"},
      {.SuppressStderr = true, .CaptureStdout = true});
  if (!PkgConfig.ExecutionFailed && PkgConfig.ExitCode == 0 &&
      !PkgConfig.StandardOutput.empty())
    return splitFlags(PkgConfig.StandardOutput);
  const std::string brew = discovered_homebrew_prefix();
  if (!brew.empty()) {
    auto lib = std::filesystem::path(brew) / "lib";
    if (std::filesystem::exists(lib / "libpcre2-8.dylib") ||
        std::filesystem::exists(lib / "libpcre2-8.so") ||
        std::filesystem::exists(lib / "libpcre2-8.a"))
      return {std::string("-L") + lib.string(), "-lpcre2-8"};
  }
  return {"-lpcre2-8"};
}

inline std::string path_for_yona_literal(const std::filesystem::path &p) {
  return p.lexically_normal().generic_string();
}

inline void rewrite_codegen_fixture_tmp_paths(std::string &s) {
  namespace fs = std::filesystem;
  const fs::path base = scratch_root();
  static const struct {
    const char *from;
    const char *rel;
  } repl[] = {
      {"/tmp/yona_iter_gen_lines_test.txt", "yona_iter_gen_lines_test.txt"},
      {"/tmp/yona_binary_test.bin", "yona_binary_test.bin"},
      {"/tmp/yona_binary_chunks.bin", "yona_binary_chunks.bin"},
      {"/tmp/yona_binary_seek.bin", "yona_binary_seek.bin"},
      {"/tmp/yona_test_write.txt", "yona_test_write.txt"},
      {"/tmp/yona_test_file.txt", "yona_test_file.txt"},
      {"/tmp/yona_stdlib_file_contract.txt", "yona_stdlib_file_contract.txt"},
      {"/tmp/yona_readexact_prereq.txt", "yona_readexact_prereq.txt"},
      {"/tmp/yona_linear_file_case.txt", "yona_linear_file_case.txt"},
  };
  for (const auto &r : repl) {
    const std::string to = path_for_yona_literal(base / r.rel);
    for (size_t pos = 0; (pos = s.find(r.from, pos)) != std::string::npos;) {
      s.replace(pos, std::strlen(r.from), to);
      pos += to.size();
    }
  }
#if !defined(__linux__)
  {
    const char *from = "/etc/os-release";
    const std::string to =
        path_for_yona_literal(base / "yona_stub_os_release.txt");
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
      s.replace(pos, std::strlen(from), to);
      pos += to.size();
    }
  }
#endif
#if defined(_WIN32)
  const std::pair<const char *, const char *> process_replacements[] = {
      {R"(exec "echo" ["async exec"])",
       R"(exec "cmd.exe" ["/c", "echo", "async exec"])"},
      {R"(f "echo" ["async exec"])",
       R"(f "cmd.exe" ["/c", "echo", "async exec"])"},
      {R"(spawn "sleep" ["0"])", R"(spawn "cmd.exe" ["/c", "exit", "0"])"},
      {R"(spawn "cat" [])", R"(spawn "cmd.exe" ["/c", "findstr", "/R", "."])"},
      {R"(spawn "echo" ["hello world"])",
       R"(spawn "cmd.exe" ["/c", "echo", "hello world"])"},
      {R"(spawn "printf" ["line1\nline2\nline3\n"])",
       R"(spawn "cmd.exe" ["/c", "echo line1&&echo line2&&echo line3"])"},
      {R"(spawn "/bin/sh" ["-c", "exit 42"])",
       R"(spawn "cmd.exe" ["/c", "exit", "42"])"},
      {R"(run "/bin/sh" ["-c", "exit 7"])",
       R"(run "cmd.exe" ["/c", "exit", "7"])"},
  };
  for (const auto &[from, to] : process_replacements) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos;) {
      s.replace(pos, std::strlen(from), to);
      pos += std::strlen(to);
    }
  }
#endif
}

inline std::string
executeAndCapture(const std::filesystem::path &Executable,
                  const std::vector<std::string> &Arguments = {},
                  yona::support::ProcessOptions Options = {}) {
  Options.CaptureStdout = true;
  if (!Options.CaptureStderr)
    Options.SuppressStderr = true;
  const auto Result =
      yona::support::executeProcess(Executable, Arguments, Options);
  if (Result.ExecutionFailed)
    return "RUN_ERROR";
  std::string Output = Result.StandardOutput;
  if (Options.CaptureStderr)
    Output += Result.StandardError;
  while (!Output.empty() && (Output.back() == '\n' || Output.back() == '\r'))
    Output.pop_back();
  return Output;
}

inline std::string
executeWithAllocationStats(const std::filesystem::path &Executable) {
  return executeAndCapture(
      Executable, {},
      {.CaptureStderr = true,
       .EnvironmentOverrides = {{"YONA_ALLOC_STATS", "1"}}});
}

} // namespace yona::test::link

#endif /* YONA_TEST_TOOLCHAIN_YONALINKUTIL_H */
