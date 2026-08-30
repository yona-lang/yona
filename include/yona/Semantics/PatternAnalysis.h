#ifndef YONA_SEMANTICS_PATTERNANALYSIS_H
#define YONA_SEMANTICS_PATTERNANALYSIS_H

#include "yona/Syntax/Ast.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::compiler::pattern_analysis {

struct ConstructorInfo {
  std::string family;
};
struct ConstructorCatalog {
  /// Synchronously invoked, non-owning semantic lookup callbacks.
  std::function<std::optional<ConstructorInfo>(std::string_view)> lookup;
  std::function<std::vector<std::string>(std::string_view)> members;
};
struct FiniteCoverage {
  std::string family;
  std::vector<std::string> missing;
};
struct Result {
  std::vector<size_t> unreachable_clauses;
  std::optional<FiniteCoverage> incomplete;
};

/// Returns true only when every value matched by `candidate` is also matched
/// by `cover`. Unsupported or null forms conservatively return false. Both AST
/// pointers are borrowed for the call. The function owns no mutable state and
/// supports concurrent traversal of immutable trees.
bool covers(ast::PatternNode *cover, ast::PatternNode *candidate);
/// Analyze borrowed case patterns and return an owned summary. Constructor
/// callbacks are called only when their information is needed; an empty needed
/// callback throws std::bad_function_call and callback exceptions propagate.
/// Thread safety follows the immutable AST and callback implementations.
Result analyze_case(const ast::CaseExpr &node,
                    const ConstructorCatalog &constructors);

} // namespace yona::compiler::pattern_analysis

#endif /* YONA_SEMANTICS_PATTERNANALYSIS_H */
