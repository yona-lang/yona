#pragma once

#include <string>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler::analysis {

/// Count textual references to `name` in `node` (for Perceus / borrow analysis).
int count_identifier_refs(ast::AstNode* node, const std::string& name);

/// True if `name` may escape via return position, collection literal, cons,
/// closure capture, or case scrutinee use (same rules as callee-borrow inference).
bool heap_param_may_escape(ast::AstNode* node, const std::string& name,
                           bool is_return_position = false);

} // namespace yona::compiler::analysis
