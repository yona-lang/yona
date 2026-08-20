#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "SourceLocation.h"
#include "typed_core/Query.h"

namespace yona::lsp {

using Position = typed_core::Position;
using Range = typed_core::Range;

/// Convert a UTF-8 byte offset into an LSP (UTF-16) position.
Position offset_to_position(std::string_view utf8, std::size_t byte_offset);

/// Convert an LSP position to a UTF-8 byte offset (clamped to the document).
std::size_t position_to_offset(std::string_view utf8, Position pos);

Range source_to_range(std::string_view utf8, const SourceLocation& loc);

std::string file_uri(std::string_view path);
std::string uri_to_path(std::string_view uri);

} // namespace yona::lsp
