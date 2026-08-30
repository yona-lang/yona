#ifndef YONA_CODEGEN_LASTUSEANALYSIS_H
#define YONA_CODEGEN_LASTUSEANALYSIS_H
//
// Last-Use Analysis for Perceus-style Reference Counting
//
// Determines, for each variable reference in an expression tree, whether
// it is the LAST use of that variable. Under Perceus:
//   - Non-last uses: DUP (rc_inc) — we need the value to survive past this
//   point
//   - Last use: pass directly — ownership transferred, no DUP needed
//
// This eliminates the need for scope-exit RC and fixes temporary argument
// leaks.
//
// Reference: Perceus: Garbage Free Reference Counting with Reuse
//            (Reinking, Xie, de Moura, Leijen — MSR 2021)
//

#include "yona/Syntax/Ast.h"

#include <unordered_map>
#include <unordered_set>

namespace yona::compiler::codegen {

// Result of last-use analysis: the set of AST nodes (IdentifierExpr*)
// that represent the LAST use of their variable in the analyzed scope.
// Any IdentifierExpr NOT in this set is a non-last use and needs DUP.
using LastUseSet = std::unordered_set<ast::AstNode *>;

// Analyze an expression tree and determine last uses.
// The `owned` set contains variable names that are "owned" by this scope
// (i.e., let-bound or function parameters) and eligible for DUP/DROP tracking.
// Variables not in `owned` are assumed to be managed externally.
LastUseSet analyze_last_uses(ast::AstNode *expr,
                             const std::unordered_set<std::string> &owned);

} // namespace yona::compiler::codegen

#endif /* YONA_CODEGEN_LASTUSEANALYSIS_H */
