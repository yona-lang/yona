#ifndef YONA_TOOLCHAIN_INPROCESSLLD_H
#define YONA_TOOLCHAIN_INPROCESSLLD_H

#include <string>
#include <vector>

namespace yona::toolchain {

/// Owned outcome and captured diagnostics from one embedded LLD invocation.
struct InProcessLldResult {
  bool ok = false;
  int ret_code = 1;
  bool can_run_again = true;
  std::string stdout_text;
  std::string stderr_text;

  /// Return an owned diagnostic string, with stderr before stdout when both
  /// streams contain text.
  std::string diagnostic_text() const {
    if (stderr_text.empty())
      return stdout_text;
    if (stdout_text.empty())
      return stderr_text;
    return stderr_text + "\n" + stdout_text;
  }
};

/// Invoke the platform's embedded LLD driver without a shell.
///
/// Ownership:
/// - `args` is borrowed only for the call. `result` is reset on entry and owns
///   all captured output on return.
///
/// Failure:
/// - Returns the same value stored in `result.ok`. Unavailable support,
///   unsupported platforms, and linker failures return false with a nonzero
///   code and/or diagnostic text.
/// - A false `result.can_run_again` is terminal: do not invoke embedded LLD
///   again in this process.
///
/// Thread safety:
/// - Embedded LLD uses process-global state. Serialize all invocations, and do
///   not overlap them with process-global linker configuration changes.
bool run_inprocess_lld(const std::vector<std::string> &args,
                       InProcessLldResult &result);

} // namespace yona::toolchain

#endif /* YONA_TOOLCHAIN_INPROCESSLLD_H */
