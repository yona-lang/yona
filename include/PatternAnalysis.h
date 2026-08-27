#pragma once

#include "ast.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yona::compiler::pattern_analysis {

struct ConstructorInfo { std::string family; };
struct ConstructorCatalog {
    std::function<std::optional<ConstructorInfo>(std::string_view)> lookup;
    std::function<std::vector<std::string>(std::string_view)> members;
};
struct FiniteCoverage { std::string family; std::vector<std::string> missing; };
struct Result { std::vector<size_t> unreachable_clauses; std::optional<FiniteCoverage> incomplete; };

/// Returns true only when every value matched by `candidate` is also matched
/// by `cover`. Unsupported forms conservatively return false.
bool covers(ast::PatternNode* cover, ast::PatternNode* candidate);
Result analyze_case(const ast::CaseExpr& node, const ConstructorCatalog& constructors);

} // namespace yona::compiler::pattern_analysis
