#include "yona/Support/Process.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#if !defined(_WIN32)
extern char **environ;
#endif

namespace yona::support {

namespace {

bool environmentNamesMatch(std::string_view Left, std::string_view Right) {
#if defined(_WIN32)
  return Left.size() == Right.size() &&
         std::ranges::equal(Left, Right, [](char Lhs, char Rhs) {
           return std::tolower(static_cast<unsigned char>(Lhs)) ==
                  std::tolower(static_cast<unsigned char>(Rhs));
         });
#else
  return Left == Right;
#endif
}

std::vector<std::string> inheritedEnvironment() {
  std::vector<std::string> Environment;
#if defined(_WIN32)
  char **Entry = *__p__environ();
#else
  char **Entry = ::environ;
#endif
  for (; Entry != nullptr && *Entry != nullptr; ++Entry)
    Environment.emplace_back(*Entry);
  return Environment;
}

bool applyEnvironmentOverrides(
    std::vector<std::string> &Environment,
    const std::vector<std::pair<std::string, std::string>> &Overrides,
    std::string &Error) {
  for (const auto &[Name, Value] : Overrides) {
    if (Name.empty() || Name.find('=') != std::string::npos) {
      Error = "invalid child environment variable name '" + Name + "'";
      return false;
    }

    const auto Existing =
        std::ranges::find_if(Environment, [&](const std::string &Entry) {
          const std::size_t Separator = Entry.find('=');
          return Separator != std::string::npos &&
                 environmentNamesMatch(
                     std::string_view(Entry).substr(0, Separator), Name);
        });
    const std::string Assignment = Name + "=" + Value;
    if (Existing == Environment.end())
      Environment.push_back(Assignment);
    else
      *Existing = Assignment;
  }
  return true;
}

} // namespace

ProcessResult executeProcess(const std::filesystem::path &Executable,
                             const std::vector<std::string> &Arguments,
                             const ProcessOptions &Options) {
  ProcessResult Result;
  const std::string RequestedProgram = Executable.string();
  if (RequestedProgram.empty()) {
    Result.ExecutionFailed = true;
    Result.Error = "child process executable is empty";
    return Result;
  }

  std::string Program = RequestedProgram;
  if (!Executable.has_parent_path()) {
    auto Resolved = llvm::sys::findProgramByName(RequestedProgram);
    if (!Resolved) {
      Result.ExecutionFailed = true;
      Result.Error = "unable to find executable '" + RequestedProgram +
                     "': " + Resolved.getError().message();
      return Result;
    }
    Program = std::move(*Resolved);
  }

  std::vector<std::string> ArgumentStorage;
  ArgumentStorage.reserve(Arguments.size() + 1);
  ArgumentStorage.push_back(Program);
  ArgumentStorage.insert(ArgumentStorage.end(), Arguments.begin(),
                         Arguments.end());

  llvm::SmallVector<llvm::StringRef, 16> ArgumentRefs;
  ArgumentRefs.reserve(ArgumentStorage.size());
  for (const std::string &Argument : ArgumentStorage)
    ArgumentRefs.push_back(Argument);

  llvm::SmallVector<std::optional<llvm::StringRef>, 3> Redirects;
  llvm::SmallString<128> StandardInputPath;
  llvm::SmallString<128> CapturedOutputPath;
  llvm::SmallString<128> CapturedErrorPath;
  const auto RemoveTemporaryFiles = [&] {
    if (!StandardInputPath.empty())
      llvm::sys::fs::remove(StandardInputPath);
    if (!CapturedOutputPath.empty())
      llvm::sys::fs::remove(CapturedOutputPath);
    if (!CapturedErrorPath.empty())
      llvm::sys::fs::remove(CapturedErrorPath);
  };

  if (Options.StandardInput) {
    if (const std::error_code InputError = llvm::sys::fs::createTemporaryFile(
            "yona-process", "stdin", StandardInputPath)) {
      Result.ExecutionFailed = true;
      Result.Error = "unable to create stdin file: " + InputError.message();
      return Result;
    }
    std::ofstream Input(StandardInputPath.c_str(),
                        std::ios::binary | std::ios::trunc);
    Input.write(Options.StandardInput->data(),
                static_cast<std::streamsize>(Options.StandardInput->size()));
    Input.close();
    if (!Input) {
      Result.ExecutionFailed = true;
      Result.Error = "unable to write child process stdin";
      RemoveTemporaryFiles();
      return Result;
    }
  }
  if (Options.CaptureStdout) {
    if (const std::error_code CaptureError = llvm::sys::fs::createTemporaryFile(
            "yona-process", "stdout", CapturedOutputPath)) {
      Result.ExecutionFailed = true;
      Result.Error =
          "unable to create stdout capture file: " + CaptureError.message();
      RemoveTemporaryFiles();
      return Result;
    }
  }
  if (Options.CaptureStderr) {
    if (const std::error_code CaptureError = llvm::sys::fs::createTemporaryFile(
            "yona-process", "stderr", CapturedErrorPath)) {
      Result.ExecutionFailed = true;
      Result.Error =
          "unable to create stderr capture file: " + CaptureError.message();
      RemoveTemporaryFiles();
      return Result;
    }
  }
  if (Options.StandardInput || Options.CaptureStdout || Options.CaptureStderr ||
      Options.SuppressStderr) {
    Redirects.push_back(Options.StandardInput
                            ? std::optional<llvm::StringRef>(StandardInputPath)
                            : std::nullopt);
    Redirects.push_back(Options.CaptureStdout
                            ? std::optional<llvm::StringRef>(CapturedOutputPath)
                            : std::nullopt);
    std::optional<llvm::StringRef> StandardErrorRedirect;
    if (Options.CaptureStderr)
      StandardErrorRedirect = CapturedErrorPath;
    else if (Options.SuppressStderr)
      StandardErrorRedirect = llvm::StringRef("");
    Redirects.push_back(StandardErrorRedirect);
  }

  std::vector<std::string> EnvironmentStorage;
  llvm::SmallVector<llvm::StringRef, 32> EnvironmentRefs;
  std::optional<llvm::ArrayRef<llvm::StringRef>> Environment;
  if (!Options.EnvironmentOverrides.empty()) {
    EnvironmentStorage = inheritedEnvironment();
    if (!applyEnvironmentOverrides(
            EnvironmentStorage, Options.EnvironmentOverrides, Result.Error)) {
      Result.ExecutionFailed = true;
      RemoveTemporaryFiles();
      return Result;
    }
    EnvironmentRefs.reserve(EnvironmentStorage.size());
    for (const std::string &Entry : EnvironmentStorage)
      EnvironmentRefs.push_back(Entry);
    Environment = llvm::ArrayRef<llvm::StringRef>(EnvironmentRefs);
  }

  std::string Error;
  bool ExecutionFailed = false;
  Result.ExitCode =
      llvm::sys::ExecuteAndWait(Program, ArgumentRefs, Environment, Redirects,
                                0, 0, &Error, &ExecutionFailed);
  Result.ExecutionFailed = ExecutionFailed || Result.ExitCode < 0;
  Result.Error = std::move(Error);
  if (Options.CaptureStdout) {
    auto CapturedOutput = llvm::MemoryBuffer::getFile(CapturedOutputPath);
    if (CapturedOutput)
      Result.StandardOutput = CapturedOutput.get()->getBuffer().str();
    else if (!Result.ExecutionFailed) {
      Result.ExecutionFailed = true;
      Result.Error = "unable to read captured stdout: " +
                     CapturedOutput.getError().message();
    }
  }
  if (Options.CaptureStderr) {
    auto CapturedError = llvm::MemoryBuffer::getFile(CapturedErrorPath);
    if (CapturedError)
      Result.StandardError = CapturedError.get()->getBuffer().str();
    else if (!Result.ExecutionFailed) {
      Result.ExecutionFailed = true;
      Result.Error = "unable to read captured stderr: " +
                     CapturedError.getError().message();
    }
  }
  RemoveTemporaryFiles();
  return Result;
}

} // namespace yona::support
