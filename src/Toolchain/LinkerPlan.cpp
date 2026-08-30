#include "yona/Toolchain/LinkerPlan.h"

#ifdef __APPLE__
#include "yona/Support/Process.h"
#elif !defined(_WIN32)
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <optional>
#endif
#include <string>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <string_view>
#endif
#include <system_error>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // IWYU pragma: keep
#if defined(_WIN32)
#include <libloaderapi.h>
#include <minwindef.h>
#endif
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace yona::toolchain {

static std::string lowercaseCopy(std::string S) {
  std::transform(S.begin(), S.end(), S.begin(), [](unsigned char Ch) {
    return static_cast<char>(std::tolower(Ch));
  });
  return S;
}

static std::filesystem::path canonicalIfExists(const std::filesystem::path &P) {
  std::error_code Ec;
  if (!std::filesystem::exists(P, Ec))
    return {};
  auto C = std::filesystem::weakly_canonical(P, Ec);
  return Ec ? P : C;
}

std::string linkerModeName(LinkerMode Mode) {
  switch (Mode) {
  case LinkerMode::Auto:
    return "auto";
  case LinkerMode::Bundled:
    return "bundled";
  case LinkerMode::System:
    return "system";
  case LinkerMode::InProcess:
    return "inprocess";
  }
  return "auto";
}

bool parseLinkerMode(const std::string &Raw, LinkerMode &OutMode) {
  std::string Mode = lowercaseCopy(Raw);
  if (Mode == "auto") {
    OutMode = LinkerMode::Auto;
    return true;
  }
  if (Mode == "bundled") {
    OutMode = LinkerMode::Bundled;
    return true;
  }
  if (Mode == "system") {
    OutMode = LinkerMode::System;
    return true;
  }
  if (Mode == "inprocess" || Mode == "in-process") {
    OutMode = LinkerMode::InProcess;
    return true;
  }
  return false;
}

std::vector<std::string> bundledLldCandidateNames() {
#ifdef _WIN32
  return {"lld-link.exe", "ld.lld.exe"};
#else
  return {"ld.lld", "lld"};
#endif
}

std::filesystem::path
discoverBundledLld(const std::vector<std::filesystem::path> &Sysroots) {
  const auto Names = bundledLldCandidateNames();
  for (const auto &Root : Sysroots) {
    for (const auto &Name : Names) {
      for (const auto &Rel : {std::filesystem::path("bin"),
                              std::filesystem::path("llvm") / "bin"}) {
        auto C = canonicalIfExists(Root / Rel / Name);
        if (!C.empty())
          return C;
      }
    }
  }
  return {};
}

bool resolveLinkerPlan(const std::string &ModeRaw,
                       const std::vector<std::filesystem::path> &Sysroots,
                       LinkerPlan &OutPlan, std::string &Error) {
  std::string Normalized = ModeRaw.empty() ? "auto" : lowercaseCopy(ModeRaw);

  LinkerMode Requested = LinkerMode::Auto;
  if (!parseLinkerMode(Normalized, Requested)) {
    Error = "invalid linker mode '" + ModeRaw +
            "' (expected auto|bundled|system|inprocess)";
    return false;
  }

  OutPlan = {};
  OutPlan.RequestedMode = Requested;
  OutPlan.BundledLldPath = discoverBundledLld(Sysroots);

  if (Requested == LinkerMode::Bundled) {
    if (OutPlan.BundledLldPath.empty()) {
      Error = "requested bundled linker mode but no bundled lld was found "
              "under sysroots";
      return false;
    }
    OutPlan.UseBundledLld = true;
    return true;
  }

  if (Requested == LinkerMode::System) {
    OutPlan.UseBundledLld = false;
    return true;
  }

  if (Requested == LinkerMode::InProcess) {
    OutPlan.UseInProcessLld = true;
    // External fallback policy mirrors auto mode.
    OutPlan.UseBundledLld = !OutPlan.BundledLldPath.empty();
    return true;
  }

  OutPlan.UseBundledLld = !OutPlan.BundledLldPath.empty();
  return true;
}

bool inProcessLldAvailable() {
#if defined(YONA_ENABLE_INPROCESS_LLD) && YONA_ENABLE_INPROCESS_LLD
  return true;
#else
  return false;
#endif
}

std::string inProcessLldUnavailableReason() {
  if (inProcessLldAvailable())
    return {};
  return "this build was compiled without embedded LLD support";
}

bool requireInProcessLldFromEnv() {
  const char *E = std::getenv("YONAC_REQUIRE_INPROCESS_LLD");
  if (!E || !*E)
    return false;
  std::string V = lowercaseCopy(std::string(E));
  return V == "1" || V == "true" || V == "yes" || V == "on";
}

#ifdef __APPLE__
static std::string runXcrun(const std::string &Argument) {
  support::ProcessOptions Options;
  Options.CaptureStdout = true;
  Options.SuppressStderr = true;
  const support::ProcessResult Result =
      support::executeProcess("xcrun", {Argument}, Options);
  if (Result.ExecutionFailed || Result.ExitCode != 0)
    return {};
  std::string Output = Result.StandardOutput;
  while (!Output.empty() && (Output.back() == '\n' || Output.back() == '\r' ||
                             Output.back() == ' '))
    Output.pop_back();
  return Output;
}

static std::string macOsSdkRoot() {
  if (const char *Environment = std::getenv("SDKROOT")) {
    if (*Environment)
      return std::string(Environment);
  }
  return runXcrun("--show-sdk-path");
}

static std::string macOsSdkVersion() {
  std::string Version = runXcrun("--show-sdk-version");
  return Version.empty() ? "11.0" : Version;
}

static std::string macOsDeploymentTarget() {
  if (const char *Environment = std::getenv("MACOSX_DEPLOYMENT_TARGET")) {
    if (*Environment)
      return std::string(Environment);
  }
  return "11.0";
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
static std::string trimCopy(std::string Value) {
  while (!Value.empty() &&
         (Value.back() == '\n' || Value.back() == '\r' || Value.back() == ' '))
    Value.pop_back();
  return Value;
}

static std::string resolveCCompiler() {
  std::string Compiler = "clang";
  if (const char *Environment = std::getenv("YONAC_CC");
      Environment && *Environment)
    Compiler = Environment;
  else if (const char *Environment = std::getenv("CC");
           Environment && *Environment)
    Compiler = Environment;
  if (llvm::sys::path::is_absolute(Compiler) &&
      llvm::sys::fs::can_execute(Compiler))
    return Compiler;
  if (auto Found = llvm::sys::findProgramByName(Compiler))
    return *Found;
  if (auto Found = llvm::sys::findProgramByName("clang"))
    return *Found;
  if (auto Found = llvm::sys::findProgramByName("cc"))
    return *Found;
  return {};
}

static std::string
runCompilerStdout(const std::vector<std::string> &ExtraArguments) {
  const std::string Compiler = resolveCCompiler();
  if (Compiler.empty())
    return {};
  llvm::SmallString<256> OutputPath;
  if (llvm::sys::fs::createTemporaryFile("yona-cc", "txt", OutputPath))
    return {};
  std::vector<llvm::StringRef> Arguments;
  Arguments.reserve(ExtraArguments.size() + 1);
  Arguments.push_back(Compiler);
  for (const auto &Argument : ExtraArguments)
    Arguments.push_back(Argument);
  llvm::SmallVector<std::optional<llvm::StringRef>, 3> Redirects;
  Redirects.push_back(llvm::StringRef(""));
  Redirects.push_back(llvm::StringRef(OutputPath));
  Redirects.push_back(llvm::StringRef(""));
  std::string Error;
  const int ExitCode = llvm::sys::ExecuteAndWait(
      Compiler, Arguments, std::nullopt, Redirects, 0, 0, &Error);
  std::string Output;
  if (auto Buffer = llvm::MemoryBuffer::getFile(OutputPath.str()))
    Output = trimCopy((*Buffer)->getBuffer().str());
  llvm::sys::fs::remove(OutputPath);
  if (ExitCode != 0)
    return {};
  return Output;
}

static std::string compilerPrintFileName(const std::string &Name) {
  const std::string Output = runCompilerStdout({"-print-file-name=" + Name});
  if (Output.empty() || Output == Name)
    return {};
  std::error_code ErrorCode;
  if (!std::filesystem::exists(Output, ErrorCode))
    return {};
  return Output;
}

static void appendColonDirectories(std::vector<std::string> &Directories,
                                   std::string_view Specification) {
  if (!Specification.empty() && Specification.front() == '=')
    Specification.remove_prefix(1);
  while (!Specification.empty()) {
    const auto Colon = Specification.find(':');
    const std::string_view Entry = Specification.substr(0, Colon);
    if (!Entry.empty()) {
      std::error_code ErrorCode;
      if (std::filesystem::is_directory(std::filesystem::path(Entry),
                                        ErrorCode))
        Directories.emplace_back(Entry);
    }
    if (Colon == std::string_view::npos)
      break;
    Specification.remove_prefix(Colon + 1);
  }
}

static std::vector<std::string> compilerLibraryDirectories() {
  std::vector<std::string> Directories;
  const std::string SearchDirectories =
      runCompilerStdout({"-print-search-dirs"});
  const auto Position = SearchDirectories.find("libraries:");
  if (Position != std::string::npos) {
    auto Line = std::string_view(SearchDirectories).substr(Position);
    const auto Newline = Line.find('\n');
    if (Newline != std::string_view::npos)
      Line = Line.substr(0, Newline);
    const auto Equals = Line.find('=');
    if (Equals != std::string_view::npos)
      appendColonDirectories(Directories, Line.substr(Equals + 1));
  }
  if (const char *LibraryPath = std::getenv("LIBRARY_PATH");
      LibraryPath && *LibraryPath)
    appendColonDirectories(Directories, LibraryPath);
  return Directories;
}

static std::string linuxDynamicLinker() {
#if defined(__aarch64__)
  const char *Soname = "ld-linux-aarch64.so.1";
  const char *Fallback = "/lib/ld-linux-aarch64.so.1";
#elif defined(__x86_64__)
  const char *Soname = "ld-linux-x86-64.so.2";
  const char *Fallback = "/lib64/ld-linux-x86-64.so.2";
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
  const char *Soname = "ld-linux-riscv64-lp64d.so.1";
  const char *Fallback = "/lib/ld-linux-riscv64-lp64d.so.1";
#else
  const char *Soname = nullptr;
  const char *Fallback = nullptr;
#endif
  if (Soname) {
    std::string Path = compilerPrintFileName(Soname);
    if (!Path.empty())
      return Path;
  }
  if (Fallback) {
    std::error_code ErrorCode;
    if (std::filesystem::exists(Fallback, ErrorCode))
      return Fallback;
  }
  return {};
}

struct ElfLldArgs {
  std::vector<std::string> Before;
  std::vector<std::string> After;
};

static ElfLldArgs makeElfLldArgs() {
  ElfLldArgs Result;
  Result.Before.push_back("--eh-frame-hdr");
  const std::string Interpreter = linuxDynamicLinker();
  if (!Interpreter.empty()) {
    Result.Before.push_back("-dynamic-linker");
    Result.Before.push_back(Interpreter);
  }
  const std::string Scrt1 = compilerPrintFileName("Scrt1.o");
  const std::string Crt1 = compilerPrintFileName("crt1.o");
  const std::string Crti = compilerPrintFileName("crti.o");
  const std::string CrtBeginShared = compilerPrintFileName("crtbeginS.o");
  const std::string CrtBegin = compilerPrintFileName("crtbegin.o");
  const std::string CrtEndShared = compilerPrintFileName("crtendS.o");
  const std::string CrtEnd = compilerPrintFileName("crtend.o");
  const std::string Crtn = compilerPrintFileName("crtn.o");
  std::vector<std::string> TerminationObjects;
  if (!Scrt1.empty()) {
    Result.Before.push_back("-pie");
    Result.Before.push_back(Scrt1);
    if (!Crti.empty())
      Result.Before.push_back(Crti);
    if (!CrtBeginShared.empty())
      Result.Before.push_back(CrtBeginShared);
    else if (!CrtBegin.empty())
      Result.Before.push_back(CrtBegin);
    if (!CrtEndShared.empty())
      TerminationObjects.push_back(CrtEndShared);
    else if (!CrtEnd.empty())
      TerminationObjects.push_back(CrtEnd);
    if (!Crtn.empty())
      TerminationObjects.push_back(Crtn);
  } else if (!Crt1.empty()) {
    Result.Before.push_back(Crt1);
    if (!Crti.empty())
      Result.Before.push_back(Crti);
    if (!CrtBegin.empty())
      Result.Before.push_back(CrtBegin);
    if (!CrtEnd.empty())
      TerminationObjects.push_back(CrtEnd);
    if (!Crtn.empty())
      TerminationObjects.push_back(Crtn);
  }
  for (const auto &Directory : compilerLibraryDirectories())
    Result.After.push_back("-L" + Directory);
  Result.After.push_back("--export-dynamic");
  Result.After.push_back("-lm");
  Result.After.push_back("-lpthread");
  const std::string LibGcc =
      trimCopy(runCompilerStdout({"-print-libgcc-file-name"}));
  if (!LibGcc.empty()) {
    std::error_code ErrorCode;
    if (std::filesystem::exists(LibGcc, ErrorCode))
      Result.After.push_back(LibGcc);
  }
  const std::string GccShared = compilerPrintFileName("libgcc_s.so.1");
  if (!GccShared.empty())
    Result.After.push_back(GccShared);
  Result.After.push_back("-lc");
  Result.After.insert(Result.After.end(), TerminationObjects.begin(),
                      TerminationObjects.end());
  return Result;
}

static const ElfLldArgs &elfLldArgs() {
  static const ElfLldArgs Cached = makeElfLldArgs();
  return Cached;
}
#endif

std::vector<std::string> inProcessLldBeforeInputArgs() {
#ifdef _WIN32
  return {};
#elif defined(__APPLE__)
  std::vector<std::string> Args;
#if defined(__aarch64__)
  Args.push_back("-arch");
  Args.push_back("arm64");
#else
  Args.push_back("-arch");
  Args.push_back("x86_64");
#endif
  Args.push_back("-platform_version");
  Args.push_back("macos");
  Args.push_back(macOsDeploymentTarget());
  Args.push_back(macOsSdkVersion());
  const std::string Sdk = macOsSdkRoot();
  if (!Sdk.empty()) {
    Args.push_back("-syslibroot");
    Args.push_back(Sdk);
  }
  return Args;
#else
  return elfLldArgs().Before;
#endif
}

std::vector<std::string> inProcessLldAfterInputArgs() {
#ifdef _WIN32
  return {"/SUBSYSTEM:CONSOLE", "oldnames.lib", "ws2_32.lib", "dbghelp.lib"};
#elif defined(__APPLE__)
  return {"-lSystem", "-U", "_YonaRegexDisposeCompiledCode"};
#else
  return elfLldArgs().After;
#endif
}

std::vector<std::string> inProcessLldSystemArgs() {
#ifdef _WIN32
  // Clang-as-linker adds oldnames.lib (POSIX open/read/write/close/isatty ->
  // _open/_read/...). Raw lld-link does not, so in-process COFF links fail.
  return inProcessLldAfterInputArgs();
#else
  auto Arguments = inProcessLldBeforeInputArgs();
  const auto TrailingArguments = inProcessLldAfterInputArgs();
  Arguments.insert(Arguments.end(), TrailingArguments.begin(),
                   TrailingArguments.end());
  return Arguments;
#endif
}

static std::filesystem::path discoverExecutableDir(const char *Argv0) {
#ifdef _WIN32
  wchar_t Wbuf[MAX_PATH];
  DWORD N = GetModuleFileNameW(nullptr, Wbuf, MAX_PATH);
  if (N > 0 && N < MAX_PATH) {
    auto C = canonicalIfExists(std::filesystem::path(Wbuf).parent_path());
    if (!C.empty())
      return C;
  }
#elif defined(__APPLE__)
  uint32_t Size = 1024;
  std::vector<char> Buffer(Size);
  if (_NSGetExecutablePath(Buffer.data(), &Size) != 0) {
    Buffer.assign(Size, '\0');
    if (_NSGetExecutablePath(Buffer.data(), &Size) != 0)
      Buffer.clear();
  }
  if (!Buffer.empty()) {
    auto Canonical =
        canonicalIfExists(std::filesystem::path(Buffer.data()).parent_path());
    if (!Canonical.empty())
      return Canonical;
  }
#else
  auto ExecutableFile =
      canonicalIfExists(std::filesystem::path("/proc/self/exe"));
  if (!ExecutableFile.empty()) {
    auto Canonical = canonicalIfExists(ExecutableFile.parent_path());
    if (!Canonical.empty())
      return Canonical;
  }
#endif
  if (Argv0 && *Argv0) {
    std::filesystem::path P(Argv0);
    if (P.has_parent_path()) {
      auto C = canonicalIfExists(P.parent_path());
      if (!C.empty())
        return C;
    }
  }
  return {};
}

std::vector<std::filesystem::path>
discoverSysroots(const char *Argv0, const std::string &SysrootOpt) {
  std::vector<std::filesystem::path> Roots;
  std::unordered_set<std::string> Seen;

  auto PushUnique = [&](const std::filesystem::path &P) {
    auto C = canonicalIfExists(P);
    if (C.empty())
      return;
    if (Seen.insert(C.string()).second)
      Roots.push_back(C);
  };

  if (!SysrootOpt.empty())
    PushUnique(std::filesystem::path(SysrootOpt));
  if (const char *H = std::getenv("YONA_HOME")) {
    if (*H)
      PushUnique(std::filesystem::path(H));
  }
  const auto Exe = discoverExecutableDir(Argv0);
  if (!Exe.empty()) {
    PushUnique(Exe);
    auto Prefix = Exe.parent_path();
    PushUnique(Prefix);
    // Homebrew / FHS: binaries in PREFIX/bin, sysroot in PREFIX/lib/yona
    PushUnique(Prefix / "lib" / "yona");
    PushUnique(Prefix / "lib64" / "yona");
  }
  PushUnique(std::filesystem::current_path());
  PushUnique(std::filesystem::current_path().parent_path());
  if (const char *Bp = std::getenv("HOMEBREW_PREFIX")) {
    if (*Bp)
      PushUnique(std::filesystem::path(Bp) / "lib" / "yona");
  }
  return Roots;
}

} // namespace yona::toolchain
