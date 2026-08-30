#ifndef YONA_TOOLCHAIN_LINKERPLAN_H
#define YONA_TOOLCHAIN_LINKERPLAN_H

/// Linker planning and discovery contracts:
///
/// Ownership:
/// - Inputs are borrowed for each call. Returned strings, vectors, paths, and
///   `LinkerPlan` values own their data.
///
/// Failure:
/// - Mode parsing returns false; full plan resolution additionally supplies an
///   error string. Discovery represents missing optional tools with empty paths
///   or omitted arguments; exceptions explicitly noted below may propagate.
///
/// Thread safety:
/// - Value-only operations are safe to call concurrently. Environment,
///   filesystem, SDK, and compiler discovery require those external resources
///   to remain stable for the duration described below.

#include <filesystem>
#include <string>
#include <vector>

namespace yona::toolchain {

enum class LinkerMode {
  Auto,
  Bundled,
  System,
  InProcess,
};

struct LinkerPlan {
  LinkerMode RequestedMode = LinkerMode::Auto;
  bool UseBundledLld = false;
  bool UseInProcessLld = false;
  std::filesystem::path BundledLldPath;
};

/// Return an owned canonical spelling. Unknown enum values map to `auto`.
std::string linkerModeName(LinkerMode Mode);

/// Parse a case-insensitive mode. On failure, `OutMode` is unchanged.
bool parseLinkerMode(const std::string &Raw, LinkerMode &OutMode);

/// Return the platform candidate names as independently owned strings.
std::vector<std::string> bundledLldCandidateNames();

/// Return the first existing candidate below the borrowed roots, or an empty
/// path. Filesystem inspection errors are treated as missing candidates.
std::filesystem::path
discoverBundledLld(const std::vector<std::filesystem::path> &Sysroots);

/// Resolve a linker selection without retaining any input.
///
/// An empty mode means `auto`. An invalid mode leaves `OutPlan` unchanged and
/// sets `Error`. Other calls reset `OutPlan`; requesting a missing bundled LLD
/// returns false with the partially resolved plan. `Error` is meaningful only
/// after a false return and is not cleared on success.
bool resolveLinkerPlan(const std::string &ModeRaw,
                       const std::vector<std::filesystem::path> &Sysroots,
                       LinkerPlan &OutPlan, std::string &Error);

bool inProcessLldAvailable();

/// Return an owned reason, or an empty string when embedded LLD is available.
std::string inProcessLldUnavailableReason();

/// Read `YONAC_REQUIRE_INPROCESS_LLD`; true accepts 1/true/yes/on,
/// case-insensitively. Do not mutate the process environment concurrently.
bool requireInProcessLldFromEnv();

// Extra args the clang/MSVC driver would inject that raw in-process LLD does
// not. Split so Darwin/ELF options that must precede inputs (arch, CRT start
// files) are not appended after object files.
//
// The returned argument vectors own their strings. Discovery may inspect the
// process environment, filesystem, platform SDK, and C compiler. Missing
// optional components are omitted rather than reported. On ELF platforms the
// first result is cached for the process. Calls are safe to run concurrently
// while the process environment and platform tool configuration are stable.
std::vector<std::string> inProcessLldBeforeInputArgs();
std::vector<std::string> inProcessLldAfterInputArgs();
std::vector<std::string> inProcessLldSystemArgs();

// Prefixes that contain lib/, runtime/, src/, include/ (packaged sysroot).
// `SysrootOpt` is `--sysroot`; empty means unset. Also honors YONA_HOME and
// Homebrew / distro layouts next to the executable.
//
// Inputs are borrowed for the call and the result owns its paths. Missing or
// inaccessible candidates are skipped. Resolving the current working directory
// may throw `std::filesystem::filesystem_error`. Do not mutate the process
// environment or working directory concurrently with this call.
std::vector<std::filesystem::path>
discoverSysroots(const char *Argv0, const std::string &SysrootOpt = {});

} // namespace yona::toolchain

#endif /* YONA_TOOLCHAIN_LINKERPLAN_H */
