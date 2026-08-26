#include "analysis/BorrowEscapeAnalysis.h"

#include "ast.h"

#include <algorithm>
#include <variant>

namespace yona::compiler::analysis {
using namespace yona::ast;

static int count_identifier_refs_impl(AstNode* node, const std::string& name,
                                      bool include_call_targets) {
    if (!node) return 0;
    auto ty = node->get_type();

    if (ty == AST_IDENTIFIER_EXPR)
        return static_cast<IdentifierExpr*>(node)->name->value == name ? 1 : 0;

    if (dynamic_cast<BinaryOpExpr*>(static_cast<AstNode*>(node))) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return count_identifier_refs_impl(b->left, name, include_call_targets)
            + count_identifier_refs_impl(b->right, name, include_call_targets);
    }

    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return count_identifier_refs_impl(e->condition, name, include_call_targets)
            + count_identifier_refs_impl(e->thenExpr, name, include_call_targets)
            + count_identifier_refs_impl(e->elseExpr, name, include_call_targets);
    }
    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        int c = 0;
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                c += count_identifier_refs_impl(va->expr, name, include_call_targets);
            } else if (auto* la = dynamic_cast<LambdaAlias*>(a)) {
                c += count_identifier_refs_impl(la->lambda, name, include_call_targets);
            }
        }
        return c + count_identifier_refs_impl(e->expr, name, include_call_targets);
    }
    if (ty == AST_IMPORT_EXPR) {
        // Imports only extend the lexical environment; ownership and last-use
        // analysis must see through them to the wrapped expression.
        return count_identifier_refs_impl(static_cast<ImportExpr*>(node)->expr,
                                          name, include_call_targets);
    }
    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        int c = count_identifier_refs_impl(e->expr, name, include_call_targets);
        for (auto* clause : e->clauses)
            c += count_identifier_refs_impl(clause->body, name, include_call_targets);
        return c;
    }
    if (ty == AST_APPLY_EXPR) {
        auto* e = static_cast<ApplyExpr*>(node);
        int c = 0;
        if (auto* nc = dynamic_cast<NameCall*>(e->call)) {
            c += include_call_targets && nc->name->value == name ? 1 : 0;
        } else if (auto* ec = dynamic_cast<ExprCall*>(e->call)) {
            if (ec->expr)
                c += count_identifier_refs_impl(ec->expr, name,
                                                include_call_targets);
        }
        for (auto& arg : e->args) {
            if (std::holds_alternative<ExprNode*>(arg))
                c += count_identifier_refs_impl(std::get<ExprNode*>(arg), name,
                                                include_call_targets);
            else
                c += count_identifier_refs_impl(std::get<ValueExpr*>(arg), name,
                                                include_call_targets);
        }
        return c;
    }
    if (ty == AST_TUPLE_EXPR) {
        auto* e = static_cast<TupleExpr*>(node);
        int c = 0;
        for (auto* v : e->values)
            c += count_identifier_refs_impl(v, name, include_call_targets);
        return c;
    }
    if (ty == AST_VALUES_SEQUENCE_EXPR) {
        auto* e = static_cast<ValuesSequenceExpr*>(node);
        int c = 0;
        for (auto* v : e->values)
            c += count_identifier_refs_impl(v, name, include_call_targets);
        return c;
    }
    if (ty == AST_SEQ_GENERATOR_EXPR) {
        auto* g = static_cast<SeqGeneratorExpr*>(node);
        auto* ext = static_cast<ValueCollectionExtractorExpr*>(g->collectionExtractor);
        int c = count_identifier_refs_impl(ext->collection, name,
                                           include_call_targets);
        std::string var;
        if (auto* id = std::get_if<IdentifierExpr*>(&ext->expr))
            var = (*id)->name->value;
        if (var != name) {
            c += count_identifier_refs_impl(g->reducerExpr, name,
                                            include_call_targets);
            if (ext->condition)
                c += count_identifier_refs_impl(ext->condition, name,
                                                include_call_targets);
        }
        return c;
    }
    if (ty == AST_FUNCTION_EXPR) {
        auto* f = static_cast<FunctionExpr*>(node);
        for (auto& p : f->patterns) {
            if (p->get_type() == AST_PATTERN_VALUE) {
                auto* pv = static_cast<PatternValue*>(p);
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    if ((*id)->name->value == name) return 0;
            }
        }
        int c = 0;
        for (auto* body : f->bodies)
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                c += count_identifier_refs_impl(bwg->expr, name,
                                                include_call_targets);
        return c;
    }
    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        int c = 0;
        for (auto* s : e->steps)
            c += count_identifier_refs_impl(s, name, include_call_targets);
        return c;
    }
    return 0;
}

int count_identifier_refs(AstNode* node, const std::string& name) {
    return count_identifier_refs_impl(node, name, true);
}

int count_identifier_value_refs(AstNode* node, const std::string& name) {
    return count_identifier_refs_impl(node, name, false);
}

int max_identifier_refs_on_path(AstNode* node, const std::string& name) {
    if (!node) return 0;
    const auto refs = [&](AstNode* child) {
        return max_identifier_refs_on_path(child, name);
    };
    if (node->get_type() == AST_IDENTIFIER_EXPR)
        return static_cast<IdentifierExpr*>(node)->name->value == name ? 1 : 0;
    if (auto* binary = dynamic_cast<BinaryOpExpr*>(node))
        return refs(binary->left) + refs(binary->right);
    if (node->get_type() == AST_IF_EXPR) {
        auto* expression = static_cast<IfExpr*>(node);
        return refs(expression->condition) +
            std::max(refs(expression->thenExpr), refs(expression->elseExpr));
    }
    if (node->get_type() == AST_CASE_EXPR) {
        auto* expression = static_cast<CaseExpr*>(node);
        int arm_max = 0;
        for (auto* clause : expression->clauses)
            arm_max = std::max(arm_max,
                refs(clause->guard) + refs(clause->body));
        return refs(expression->expr) + arm_max;
    }
    if (node->get_type() == AST_LET_EXPR) {
        auto* expression = static_cast<LetExpr*>(node);
        int total = 0;
        for (auto* alias : expression->aliases) {
            if (auto* value = dynamic_cast<ValueAlias*>(alias)) {
                total += refs(value->expr);
            } else if (auto* lambda = dynamic_cast<LambdaAlias*>(alias)) {
                total += refs(lambda->lambda);
            } else if (auto* pattern = dynamic_cast<PatternAlias*>(alias)) {
                total += refs(pattern->expr);
            }
        }
        return total + refs(expression->expr);
    }
    if (node->get_type() == AST_IMPORT_EXPR)
        return refs(static_cast<ImportExpr*>(node)->expr);
    if (node->get_type() == AST_APPLY_EXPR) {
        auto* expression = static_cast<ApplyExpr*>(node);
        int total = 0;
        if (auto* call = dynamic_cast<NameCall*>(expression->call))
            total += call->name->value == name ? 1 : 0;
        else if (auto* call = dynamic_cast<ExprCall*>(expression->call))
            total += refs(call->expr);
        for (auto& argument : expression->args)
            total += std::holds_alternative<ExprNode*>(argument)
                ? refs(std::get<ExprNode*>(argument))
                : refs(std::get<ValueExpr*>(argument));
        return total;
    }
    if (node->get_type() == AST_TUPLE_EXPR) {
        int total = 0;
        for (auto* value : static_cast<TupleExpr*>(node)->values)
            total += refs(value);
        return total;
    }
    if (node->get_type() == AST_VALUES_SEQUENCE_EXPR) {
        int total = 0;
        for (auto* value : static_cast<ValuesSequenceExpr*>(node)->values)
            total += refs(value);
        return total;
    }
    if (node->get_type() == AST_DO_EXPR) {
        int total = 0;
        for (auto* step : static_cast<DoExpr*>(node)->steps)
            total += refs(step);
        return total;
    }
    return count_identifier_refs(node, name);
}

bool heap_param_may_escape(AstNode* node, const std::string& name, bool is_return_position) {
    if (!node) return false;
    auto ty = node->get_type();

    if (ty == AST_IDENTIFIER_EXPR) {
        if (static_cast<IdentifierExpr*>(node)->name->value == name)
            return is_return_position;
        return false;
    }

    if (ty == AST_VALUES_SEQUENCE_EXPR) {
        auto* e = static_cast<ValuesSequenceExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }
    if (ty == AST_TUPLE_EXPR) {
        auto* e = static_cast<TupleExpr*>(node);
        for (auto* v : e->values)
            if (count_identifier_refs(v, name) > 0) return true;
        return false;
    }

    if (ty == AST_CONS_LEFT_EXPR) {
        auto* e = static_cast<ConsLeftExpr*>(node);
        if (count_identifier_refs(e->left, name) > 0) return true;
        if (count_identifier_refs(e->right, name) > 0) return true;
        return false;
    }

    if (ty == AST_FUNCTION_EXPR) {
        auto* f = static_cast<FunctionExpr*>(node);
        for (auto* pat : f->patterns) {
            if (auto* pv = dynamic_cast<PatternValue*>(pat)) {
                if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                    if ((*id)->name->value == name) return false;
            }
        }
        for (auto* body : f->bodies)
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body))
                if (count_identifier_refs(bwg->expr, name) > 0) return true;
        return false;
    }

    if (ty == AST_REMOVE_EXPR) {
        // Set difference follows the Perceus callee-owns ABI for its left
        // operand.  At this syntax-only stage the operand may still be
        // polymorphic, so conservatively require ownership whenever the
        // parameter is used on the left.  The right operand is borrowed.
        auto* e = static_cast<RemoveExpr*>(node);
        if (count_identifier_refs(e->left, name) > 0) return true;
        return heap_param_may_escape(e->right, name, false);
    }

    if (dynamic_cast<BinaryOpExpr*>(node)) {
        auto* b = static_cast<BinaryOpExpr*>(node);
        return heap_param_may_escape(b->left, name, false) || heap_param_may_escape(b->right, name, false);
    }

    if (ty == AST_IF_EXPR) {
        auto* e = static_cast<IfExpr*>(node);
        return heap_param_may_escape(e->condition, name, false)
            || heap_param_may_escape(e->thenExpr, name, is_return_position)
            || heap_param_may_escape(e->elseExpr, name, is_return_position);
    }

    if (ty == AST_LET_EXPR) {
        auto* e = static_cast<LetExpr*>(node);
        for (auto* a : e->aliases) {
            if (auto* va = dynamic_cast<ValueAlias*>(a)) {
                if (heap_param_may_escape(va->expr, name, false)) return true;
                if (va->identifier->name->value == name) return false;
            } else if (auto* la = dynamic_cast<LambdaAlias*>(a)) {
                if (heap_param_may_escape(la->lambda, name, false)) return true;
                if (la->name->value == name) return false;
            }
        }
        return heap_param_may_escape(e->expr, name, is_return_position);
    }

    if (ty == AST_IMPORT_EXPR)
        return heap_param_may_escape(static_cast<ImportExpr*>(node)->expr,
                                     name, is_return_position);

    if (ty == AST_CASE_EXPR) {
        auto* e = static_cast<CaseExpr*>(node);
        if (count_identifier_refs(e->expr, name) > 0) return true;
        for (auto* clause : e->clauses)
            if (heap_param_may_escape(clause->body, name, is_return_position)) return true;
        return false;
    }

    if (ty == AST_APPLY_EXPR) {
        return false;
    }

    if (ty == AST_DO_EXPR) {
        auto* e = static_cast<DoExpr*>(node);
        for (size_t i = 0; i < e->steps.size(); i++) {
            bool last = (i == e->steps.size() - 1);
            if (heap_param_may_escape(e->steps[i], name, last && is_return_position)) return true;
        }
        return false;
    }

    return count_identifier_refs(node, name) > 0;
}

} // namespace yona::compiler::analysis
