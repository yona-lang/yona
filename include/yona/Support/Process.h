#ifndef YONA_SUPPORT_PROCESS_H
#define YONA_SUPPORT_PROCESS_H
#include "yona/Support/Export.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yona::support {

/// Result of executing a child process without involving a command shell.
struct ProcessResult {
  int ExitCode = -1;
  bool ExecutionFailed = false;
  std::string Error;
  std::string StandardOutput;
  std::string StandardError;
};

/// Controls child-process stream handling.
struct ProcessOptions {
  bool SuppressStderr = false;
  bool CaptureStdout = false;
  bool CaptureStderr = false;
  std::optional<std::string> StandardInput;
  std::vector<std::pair<std::string, std::string>> EnvironmentOverrides;
};

/// Executes \p Executable with exactly the supplied argument vector.
///
/// No shell parsing, interpolation, or quoting is performed. Relative program
/// names are resolved through PATH. When \p SuppressStderr is true, the child
/// process's standard error stream is disconnected portably, unless it is
/// captured. `StandardInput` supplies the child's complete standard input.
/// Environment overrides are applied to a snapshot of the inherited
/// environment. Captured output is owned by the returned result. Each
/// invocation is independent and safe to call concurrently; failures to start
/// or capture are reported in `Error`.
YONA_API ProcessResult executeProcess(const std::filesystem::path &Executable,
                                      const std::vector<std::string> &Arguments,
                                      const ProcessOptions &Options = {});

} // namespace yona::support

#endif /* YONA_SUPPORT_PROCESS_H */
