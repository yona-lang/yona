#ifndef YONA_CODEGEN_ESCAPEANALYSIS_H
#define YONA_CODEGEN_ESCAPEANALYSIS_H

#include "yona/Syntax/Ast.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace yona::compiler::codegen {

// Determines which let-bound variables' values do NOT escape their scope.
// A value "escapes" if it is:
//   - Returned from the function
//   - Captured by a closure (appears in a lambda's free variables)
//   - Passed to an extern/opaque function
//   - Stored into an escaping data structure
//
// Non-escaping values can be arena-allocated instead of heap-allocated,
// avoiding individual RC overhead and enabling bulk deallocation.
class EscapeAnalysis {
public:
  // Analyze a function body. Returns the set of variable names whose
  // values are proven NOT to escape.
  static std::unordered_set<std::string>
  analyze(ast::FunctionExpr *func,
          const std::unordered_set<std::string> &known_local_functions);

  // Analyze an expression (e.g., main body). Returns non-escaping vars.
  static std::unordered_set<std::string>
  analyze_expr(ast::ExprNode *expr,
               const std::unordered_set<std::string> &known_local_functions);

private:
  // Collect all variables that appear in escaping positions
  static void
  collect_escaping(ast::AstNode *node,
                   std::unordered_set<std::string> &escaping,
                   const std::unordered_set<std::string> &let_bound,
                   const std::unordered_set<std::string> &known_local_functions,
                   bool in_return_position);
};

} // namespace yona::compiler::codegen

#endif /* YONA_CODEGEN_ESCAPEANALYSIS_H */
