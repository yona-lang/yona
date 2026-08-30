#ifndef YONA_TYPEDIR_BUILDER_H
#define YONA_TYPEDIR_BUILDER_H

#include "yona/TypedIr/TypedIr.h"

#include <string>

namespace yona::typed_ir {

/// Build a single-entry typed IR module from the root facts of a SemanticModel.
///
/// Ownership:
/// - The returned module owns all copied names and semantic facts.
/// - The SemanticModel is borrowed only for the duration of this call.
///
/// Failure:
/// - Throws std::invalid_argument when names are empty or root semantic facts
///   are unavailable/incomplete.
///
/// Thread safety:
/// - Safe to call concurrently for distinct output modules. SemanticModel is
///   immutable and is only read.
[[nodiscard]] Module buildEntryModule(const semantics::SemanticModel &Model,
                                      std::string ModuleName,
                                      std::string EntryName = "main");

} // namespace yona::typed_ir

#endif // YONA_TYPEDIR_BUILDER_H
