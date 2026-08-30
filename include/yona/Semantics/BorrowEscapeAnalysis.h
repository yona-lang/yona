#ifndef YONA_SEMANTICS_BORROWESCAPEANALYSIS_H
#define YONA_SEMANTICS_BORROWESCAPEANALYSIS_H

#include <string>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler::analysis {

/// These stateless analyses borrow the AST and name only for each call. A null
/// root produces the conservative zero/false result. They are safe to run in
/// parallel while the complete AST is alive and immutable.

/// Count textual references to `name` in `node` (for Perceus / borrow
/// analysis).
int count_identifier_refs(ast::AstNode *node, const std::string &name);

/// Count references that use `name` as a value. Calling `name` does not pass
/// the closure itself to the surrounding expression, so call-target-only
/// references are excluded. Argument references remain included.
int count_identifier_value_refs(ast::AstNode *node, const std::string &name);

/// Maximum number of references evaluated on any single control-flow path.
/// Mutually exclusive `if` and `case` arms contribute their maximum rather
/// than their textual sum.
int max_identifier_refs_on_path(ast::AstNode *node, const std::string &name);

/// True if `name` may escape via return position, collection literal, cons,
/// closure capture, or case scrutinee use (same rules as callee-borrow
/// inference).
bool heap_param_may_escape(ast::AstNode *node, const std::string &name,
                           bool is_return_position = false);

} // namespace yona::compiler::analysis

#endif /* YONA_SEMANTICS_BORROWESCAPEANALYSIS_H */
