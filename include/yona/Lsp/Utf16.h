#ifndef YONA_LSP_UTF16_H
#define YONA_LSP_UTF16_H

#include "yona/Lsp/Protocol.h"
#include "yona/Support/SourceManager.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace yona::lsp {

/// Stateless LSP coordinate and file-URI conversions.
///
/// Ownership:
/// - Inputs are borrowed only for each call and every return value is owned.
///
/// Failure:
/// - Conversion errors are not reported; clamping and mechanical decoding
///   behavior is detailed below.
///
/// Thread safety:
/// - The functions retain no state and may be called concurrently.
///
/// Convert a UTF-8 byte offset into an LSP (UTF-16) position.
///
/// The input is borrowed only for the call. Offsets past the buffer are
/// clamped to its end. UTF-8 is consumed without validation; unrecognized or
/// truncated lead bytes count as replacement characters.
Position offset_to_position(std::string_view utf8, std::size_t byte_offset);

/// Convert an LSP position to a UTF-8 byte offset (clamped to the document).
/// The input is borrowed only for the call. A position inside a surrogate pair
/// maps to the beginning of that UTF-8 scalar.
std::size_t position_to_offset(std::string_view utf8, Position pos);

/// Convert the source offsets to UTF-16 positions. Offsets are clamped by the
/// conversion routines. A zero-length range with line metadata uses the
/// one-based SourceRange line/column directly, without document validation.
Range source_to_range(std::string_view utf8, const SourceRange &Source);

/// Return an owned, percent-escaped file URI. No filesystem access occurs.
std::string file_uri(std::string_view path);

/// Return an owned path after removing an optional `file://` prefix and
/// decoding percent triplets. This is a mechanical conversion: it does not
/// validate the URI, escapes, path, or filesystem existence.
std::string uri_to_path(std::string_view uri);

} // namespace yona::lsp

#endif /* YONA_LSP_UTF16_H */
