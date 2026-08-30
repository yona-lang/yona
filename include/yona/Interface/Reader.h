#ifndef YONA_INTERFACE_READER_H
#define YONA_INTERFACE_READER_H

#include "yona/Interface/Module.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yona::interface {

struct ParseError {
  std::size_t Line = 0;
  std::size_t Column = 0;
  std::string Message;
};

using ParseResult = std::expected<InterfaceModule, std::vector<ParseError>>;
using SearchResult =
    std::expected<std::optional<InterfaceModule>, std::vector<ParseError>>;

/// Parse the one canonical, unversioned `.yonai` schema.
///
/// The returned module owns all data. Malformed input produces one or more
/// located errors and no partial module. The function uses no mutable global
/// state and is thread-safe.
[[nodiscard]] ParseResult parseModule(std::string_view Input);

/// Read and parse Path.
///
/// Open/read failures are returned as a line-zero ParseError. The function
/// does not retain Path or the file buffer and is thread-safe provided another
/// thread does not mutate the same file concurrently.
[[nodiscard]] ParseResult readModule(const std::filesystem::path &Path);

/// Find Identity below the supplied roots and parse the first matching file.
///
/// A missing file is a successful empty result. A present malformed file is an
/// error; later roots are not consulted. The parsed MODULE record must equal
/// Identity. All paths and results are caller-owned.
[[nodiscard]] SearchResult
readModuleFromSearchPaths(std::span<const std::string> Roots,
                          const model::ModuleIdentity &Identity);

/// Return a non-owning pointer to LocalName, or null when it is not exported.
/// The pointer remains valid until Module is mutated or destroyed.
[[nodiscard]] const Function *findFunction(const InterfaceModule &Module,
                                           std::string_view LocalName);

} // namespace yona::interface

#endif /* YONA_INTERFACE_READER_H */
