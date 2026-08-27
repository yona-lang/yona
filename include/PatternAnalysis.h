#pragma once

#include "ast.h"

namespace yona::compiler::pattern_analysis {

/// Returns true only when every value matched by `candidate` is also matched
/// by `cover`. Unsupported forms conservatively return false.
bool covers(ast::PatternNode* cover, ast::PatternNode* candidate);

} // namespace yona::compiler::pattern_analysis
