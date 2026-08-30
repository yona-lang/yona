#ifndef YONA_INTERFACE_WRITER_H
#define YONA_INTERFACE_WRITER_H

#include "yona/Interface/Module.h"

#include <expected>
#include <filesystem>
#include <string>

namespace yona::interface {

/// Serialize Module in deterministic canonical order.
///
/// Invalid in-memory models return an explanatory error and produce no text.
/// The returned string is owned by the caller, uses LF newlines, and always has
/// a final newline. This operation has no mutable global state.
[[nodiscard]] std::expected<std::string, std::string>
serializeModule(const InterfaceModule &Module);

/// Serialize and write Path with the canonical bytes.
///
/// Validation and I/O failures are returned as strings. No stream or buffer is
/// retained. The file is opened with truncation after validation; a later I/O
/// failure may therefore leave a partial file. Calls targeting the same path
/// require external synchronization.
[[nodiscard]] std::expected<void, std::string>
writeModule(const std::filesystem::path &Path, const InterfaceModule &Module);

} // namespace yona::interface

#endif /* YONA_INTERFACE_WRITER_H */
