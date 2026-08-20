//
// Last-Use Analysis for Perceus-style Reference Counting
//
// Backward walk over the AST to determine which reference to each variable
// is the last one. The first reference encountered during backward traversal
// is the last use in execution order.
//
// For branching (if/case): a variable is "last use" in a branch only if it
// is NOT used after the branch point. Conservatively: if a variable appears
// in multiple branches, each branch's use gets DUP'd (non-last), and the
// variable is not consumed by the branch — it's consumed at the merge point
// or by a later use.
//

#include "LastUseAnalysis.h"
#include <iostream>

namespace yona::compiler::codegen {

using namespace yona::ast;

// Internal state for the analysis
struct LastUseAnalyzer {
    const std::unordered_set<std::string>& owned;
    // Variables already seen (from the end of the expression backward).
    // If a variable is in `seen`, any earlier reference is NOT the last use.
    std::unordered_set<std::string> seen;
    // The result: AST nodes that are last uses
    LastUseSet last_uses;

    LastUseAnalyzer(const std::unordered_set<std::string>& owned) : owned(owned) {}

    // Walk an expression in REVERSE execution order.
    // Returns the set of variables used in this expression (for branch merging).
    std::unordered_set<std::string> walk(AstNode* node) {
        if (!node) return {};
        std::unordered_set<std::string> used;

        switch (node->get_type()) {
        case AST_IDENTIFIER_EXPR: {
            auto* id = static_cast<IdentifierExpr*>(node);
            auto& name = id->name->value;
            if (owned.count(name)) {
                used.insert(name);
                if (!seen.count(name)) {
                    // First time seeing this variable (from the end) → last use
                    last_uses.insert(node);
                    seen.insert(name);
                }
                // else: already seen → non-last use (needs DUP)
            }
            break;
        }

        // Binary operations: right is evaluated after left, so walk right first
        case AST_ADD_EXPR: case AST_SUBTRACT_EXPR: case AST_MULTIPLY_EXPR:
        case AST_DIVIDE_EXPR: case AST_MODULO_EXPR:
        case AST_EQ_EXPR: case AST_NEQ_EXPR:
        case AST_LT_EXPR: case AST_GT_EXPR: case AST_LTE_EXPR: case AST_GTE_EXPR:
        case AST_LOGICAL_AND_EXPR: case AST_LOGICAL_OR_EXPR:
        case AST_CONS_LEFT_EXPR: case AST_CONS_RIGHT_EXPR: case AST_JOIN_EXPR:
        case AST_IN_EXPR: case AST_REMOVE_EXPR:
        case AST_PIPE_LEFT_EXPR: case AST_PIPE_RIGHT_EXPR: {
            auto* bin = static_cast<BinaryOpExpr*>(node);
            // Walk right first (it executes second, so its uses are "later")
            auto r_used = walk(bin->right);
            auto l_used = walk(bin->left);
            used.insert(r_used.begin(), r_used.end());
            used.insert(l_used.begin(), l_used.end());
            break;
        }

        case AST_IF_EXPR: {
            auto* e = static_cast<IfExpr*>(node);
            // Branches are alternatives — save state, analyze each, merge
            auto saved_seen = seen;

            // Walk then branch
            auto then_seen_start = seen;
            auto then_used = walk(e->thenExpr);

            // Restore and walk else branch
            auto then_seen_end = seen;
            seen = saved_seen;
            auto else_used = walk(e->elseExpr);
            auto else_seen_end = seen;

            // Merge: a variable is "seen" if it was seen in EITHER branch.
            // This is conservative: if a variable is used in both branches,
            // both uses get DUP'd (neither is the "last" use from the
            // perspective of code after the if).
            seen = saved_seen;
            for (auto& v : then_seen_end) seen.insert(v);
            for (auto& v : else_seen_end) seen.insert(v);

            // But: for variables used in only ONE branch, that branch's use
            // could be the last use. We already marked it above during walk.
            // The issue: if the variable is also used BEFORE the if (earlier
            // in execution), that earlier use would be marked as non-last
            // because `seen` now includes it. This is correct behavior.

            // Now walk the condition (executed before branches)
            auto cond_used = walk(e->condition);

            used.insert(then_used.begin(), then_used.end());
            used.insert(else_used.begin(), else_used.end());
            used.insert(cond_used.begin(), cond_used.end());
            break;
        }

        case AST_LET_EXPR: {
            auto* e = static_cast<LetExpr*>(node);
            // Walk body first (it executes after bindings)
            auto body_used = walk(e->expr);
            used.insert(body_used.begin(), body_used.end());

            // Walk aliases in reverse order
            for (int i = (int)e->aliases.size() - 1; i >= 0; i--) {
                auto* alias = e->aliases[i];
                if (auto* va = dynamic_cast<ValueAlias*>(alias)) {
                    auto alias_used = walk(va->expr);
                    used.insert(alias_used.begin(), alias_used.end());
                } else if (auto* pa = dynamic_cast<PatternAlias*>(alias)) {
                    auto alias_used = walk(pa->expr);
                    used.insert(alias_used.begin(), alias_used.end());
                }
                // LambdaAlias: the function body is a separate scope,
                // analyzed when the function is compiled. Skip here.
            }
            break;
        }

        case AST_CASE_EXPR: {
            auto* e = static_cast<CaseExpr*>(node);
            // Each clause is an alternative — same merge strategy as if
            auto saved_seen = seen;
            std::unordered_set<std::string> all_clause_seen;

            for (auto* clause : e->clauses) {
                seen = saved_seen;
                auto clause_used = walk(clause->body);
                used.insert(clause_used.begin(), clause_used.end());
                for (auto& v : seen) all_clause_seen.insert(v);
            }

            // Merge: union of all clause seen sets
            seen = saved_seen;
            for (auto& v : all_clause_seen) seen.insert(v);

            // Walk the scrutinee (executed before clauses)
            auto scrut_used = walk(e->expr);
            used.insert(scrut_used.begin(), scrut_used.end());
            break;
        }

        case AST_APPLY_EXPR: {
            auto* e = static_cast<ApplyExpr*>(node);
            // Walk args in reverse (last arg evaluated last)
            for (int i = (int)e->args.size() - 1; i >= 0; i--) {
                auto& arg = e->args[i];
                std::unordered_set<std::string> arg_used;
                if (std::holds_alternative<ExprNode*>(arg))
                    arg_used = walk(std::get<ExprNode*>(arg));
                else
                    arg_used = walk(std::get<ValueExpr*>(arg));
                used.insert(arg_used.begin(), arg_used.end());
            }
            // Walk callee
            if (auto* nc = dynamic_cast<NameCall*>(e->call)) {
                auto& name = nc->name->value;
                if (owned.count(name)) {
                    used.insert(name);
                    if (!seen.count(name)) {
                        last_uses.insert(nc); // NameCall node as proxy
                        seen.insert(name);
                    }
                }
            } else if (auto* ec = dynamic_cast<ExprCall*>(e->call)) {
                auto callee_used = walk(ec->expr);
                used.insert(callee_used.begin(), callee_used.end());
            }
            break;
        }

        case AST_FUNCTION_EXPR: {
            // Lambda body is a separate scope — don't walk into it.
            // But free variables referenced by the lambda count as uses.
            auto* fn = static_cast<FunctionExpr*>(node);
            // Collect free vars used in the lambda body
            std::unordered_set<std::string> bound;
            for (auto* pat : fn->patterns) {
                if (pat->get_type() == AST_PATTERN_VALUE) {
                    auto* pv = static_cast<PatternValue*>(pat);
                    if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                        bound.insert((*id)->name->value);
                }
            }
            // Walk the body to find uses of owned variables (they're captures)
            for (auto* body : fn->bodies) {
                if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
                    auto body_used = walk(bwg->expr);
                    // Only count variables from the outer scope (owned but not bound)
                    for (auto& v : body_used) {
                        if (owned.count(v) && !bound.count(v))
                            used.insert(v);
                    }
                }
            }
            break;
        }

        case AST_TUPLE_EXPR: {
            auto* e = static_cast<TupleExpr*>(node);
            for (int i = (int)e->values.size() - 1; i >= 0; i--) {
                auto elem_used = walk(e->values[i]);
                used.insert(elem_used.begin(), elem_used.end());
            }
            break;
        }

        case AST_VALUES_SEQUENCE_EXPR: {
            auto* e = static_cast<ValuesSequenceExpr*>(node);
            for (int i = (int)e->values.size() - 1; i >= 0; i--) {
                auto elem_used = walk(e->values[i]);
                used.insert(elem_used.begin(), elem_used.end());
            }
            break;
        }

        case AST_DO_EXPR: {
            auto* e = static_cast<DoExpr*>(node);
            for (int i = (int)e->steps.size() - 1; i >= 0; i--) {
                auto step_used = walk(e->steps[i]);
                used.insert(step_used.begin(), step_used.end());
            }
            break;
        }

        case AST_RAISE_EXPR: {
            auto* e = static_cast<RaiseExpr*>(node);
            auto val_used = walk(e->value);
            used.insert(val_used.begin(), val_used.end());
            break;
        }

        case AST_TRY_CATCH_EXPR: {
            auto* e = static_cast<TryCatchExpr*>(node);
            auto saved_seen = seen;

            auto try_used = walk(e->tryExpr);
            auto try_seen = seen;

            // Catch clauses are alternatives
            std::unordered_set<std::string> all_catch_seen;
            if (e->catchExpr) {
                for (auto* clause : e->catchExpr->patterns) {
                    seen = saved_seen;
                    if (auto* pwog = std::get_if<PatternWithoutGuards*>(&clause->pattern)) {
                        auto c_used = walk((*pwog)->expr);
                        used.insert(c_used.begin(), c_used.end());
                    }
                    for (auto& v : seen) all_catch_seen.insert(v);
                }
            }

            seen = saved_seen;
            for (auto& v : try_seen) seen.insert(v);
            for (auto& v : all_catch_seen) seen.insert(v);
            used.insert(try_used.begin(), try_used.end());
            break;
        }

        case AST_WITH_EXPR: {
            auto* e = static_cast<WithExpr*>(node);
            auto body_used = walk(e->bodyExpr);
            auto ctx_used = walk(e->contextExpr);
            used.insert(body_used.begin(), body_used.end());
            used.insert(ctx_used.begin(), ctx_used.end());
            break;
        }

        default:
            // Literals, symbols, etc. — no variable references
            break;
        }

        return used;
    }
};

LastUseSet analyze_last_uses(AstNode* expr, const std::unordered_set<std::string>& owned) {
    LastUseAnalyzer analyzer(owned);
    analyzer.walk(expr);
    return analyzer.last_uses;
}

} // namespace yona::compiler::codegen
