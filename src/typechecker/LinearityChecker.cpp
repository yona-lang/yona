/// LinearityChecker — tracks Linear ADT values through the program.
///
/// `type Linear a = Linear a` is a built-in ADT that wraps resource handles.
/// The checker ensures each Linear value is pattern-matched exactly once:
///   - Construction: `Linear fd` or any expression whose type is `Linear _`
///     (including user-defined producers and products of Linear values)
///   - Consumption: `case x of Linear fd -> ...` (pattern match)
///   - Error: using a consumed value, or branch inconsistency
///   - Warning: dropping a live value at scope exit

#include "typechecker/LinearityChecker.h"
#include "typechecker/TypeChecker.h"

#include <algorithm>

namespace yona::compiler::typechecker {
using namespace yona::ast;

namespace {

bool is_linear_type(const MonoType* t) {
    return t && t->tag == MonoType::App && t->type_name == "Linear";
}

/// Callee name of a (possibly nested) application, or empty.
std::string apply_callee_name(AstNode* expr) {
    if (!expr || expr->get_type() != AST_APPLY_EXPR) return {};
    auto* cur = static_cast<ApplyExpr*>(expr);
    while (cur) {
        if (auto* nc = dynamic_cast<NameCall*>(cur->call))
            return nc->name->value;
        if (auto* ec = dynamic_cast<ExprCall*>(cur->call)) {
            if (ec->expr->get_type() == AST_APPLY_EXPR)
                cur = static_cast<ApplyExpr*>(ec->expr);
            else if (ec->expr->get_type() == AST_IDENTIFIER_EXPR)
                return static_cast<IdentifierExpr*>(ec->expr)->name->value;
            else
                return {};
        } else {
            return {};
        }
    }
    return {};
}

} // namespace

// ===== LinearEnv =====

void LinearEnv::create(const std::string& name, const SourceLocation& loc) {
    vars[name] = LinearStatus::Live;
    created_at[name] = loc;
}

bool LinearEnv::consume(const std::string& name, const SourceLocation& loc) {
    auto it = vars.find(name);
    if (it == vars.end()) return true; // not tracked
    if (it->second == LinearStatus::Consumed) return false; // already consumed
    it->second = LinearStatus::Consumed;
    consumed_at[name] = loc;
    return true;
}

bool LinearEnv::is_live(const std::string& name) const {
    auto it = vars.find(name);
    return it != vars.end() && it->second == LinearStatus::Live;
}

bool LinearEnv::is_consumed(const std::string& name) const {
    auto it = vars.find(name);
    return it != vars.end() && it->second == LinearStatus::Consumed;
}

bool LinearEnv::is_tracked(const std::string& name) const {
    return vars.count(name) > 0;
}

std::vector<std::string> LinearEnv::live_vars() const {
    std::vector<std::string> result;
    for (auto& [name, status] : vars)
        if (status == LinearStatus::Live)
            result.push_back(name);
    return result;
}

// ===== LinearityChecker =====

LinearityChecker::LinearityChecker(DiagnosticEngine& diag, TypeChecker* tc)
    : diag_(diag), tc_(tc) {}

const MonoType* LinearityChecker::type_of_expr(AstNode* expr) {
    if (!tc_ || !expr) return nullptr;
    return tc_->zonk(tc_->type_of(expr));
}

bool LinearityChecker::expr_produces_linear(AstNode* expr) {
    if (auto* t = type_of_expr(expr)) {
        if (is_linear_type(t)) return true;
        // Conclusive non-Linear type: not a producer (including unresolved-as-Int, etc.)
        if (t->tag != MonoType::Var) return false;
    }
    return is_linear_constructor(apply_callee_name(expr));
}

void LinearityChecker::track_linear_pattern(PatternNode* pat, const MonoType* ty,
                                            LinearEnv& env, const SourceLocation& loc) {
    if (!pat) return;
    if (is_linear_type(ty)) {
        if (pat->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(pat);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                env.create((*id)->name->value, loc);
        }
        return;
    }
    if (!ty || ty->tag != MonoType::MTuple) return;
    if (pat->get_type() != AST_TUPLE_PATTERN) return;
    auto* tp = static_cast<TuplePattern*>(pat);
    const size_t n = std::min(tp->patterns.size(), ty->elements.size());
    for (size_t i = 0; i < n; i++)
        track_linear_pattern(tp->patterns[i], ty->elements[i], env, loc);
}

void LinearityChecker::check(AstNode* node) {
    LinearEnv env;
    check_node(node, env);
    warn_unconsumed(env);
}

void LinearityChecker::warn_unconsumed(const LinearEnv& env) {
    for (auto& name : env.live_vars()) {
        auto it = env.created_at.find(name);
        SourceLocation loc = (it != env.created_at.end()) ? it->second : SourceLocation::unknown();
        diag_.warning(loc,
                      "linear value '" + name + "' not consumed — possible resource leak; "
                      "use `case " + name + " of Linear fd -> close fd end` to release",
                      WarningFlag::UnhandledEffect); // reuse existing warning flag for now
    }
}

void LinearityChecker::check_node(AstNode* node, LinearEnv& env) {
    if (!node) return;

    switch (node->get_type()) {
        case AST_MAIN:
            check_node(static_cast<MainNode*>(node)->node, env);
            break;
        case AST_LET_EXPR:
            check_let(static_cast<LetExpr*>(node), env);
            break;
        case AST_CASE_EXPR:
            check_case(static_cast<CaseExpr*>(node), env);
            break;
        case AST_IF_EXPR:
            check_if(static_cast<IfExpr*>(node), env);
            break;
        case AST_APPLY_EXPR:
            check_apply(static_cast<ApplyExpr*>(node), env);
            break;
        case AST_WITH_EXPR:
            check_with(static_cast<WithExpr*>(node), env);
            break;
        case AST_FUNCTION_EXPR:
            check_function(static_cast<FunctionExpr*>(node), env);
            break;
        case AST_IMPORT_EXPR:
            check_node(static_cast<ImportExpr*>(node)->expr, env);
            break;
        case AST_EXTERN_DECL:
            check_node(static_cast<ExternDeclExpr*>(node)->body, env);
            break;
        case AST_DO_EXPR: {
            auto* doex = static_cast<DoExpr*>(node);
            for (auto* step : doex->steps)
                check_node(step, env);
            break;
        }
        default:
            if (auto* binop = dynamic_cast<BinaryOpExpr*>(node))  {
                check_node(binop->left, env);
                check_node(binop->right, env);
            }
            break;
    }
}

void LinearityChecker::check_let(LetExpr* node, LinearEnv& env) {
    for (auto* alias : node->aliases) {
        if (auto* va = dynamic_cast<ValueAlias*>(alias)) {
            check_node(va->expr, env);
            std::string name = va->identifier->name->value;

            if (va->expr->get_type() == AST_IDENTIFIER_EXPR) {
                auto* id = static_cast<IdentifierExpr*>(va->expr);
                std::string src = id->name->value;
                if (env.is_live(src)) {
                    // Transfer: old name consumed, new name is live
                    env.consume(src, va->source_context);
                    env.create(name, va->source_context);
                } else if (env.is_consumed(src)) {
                    diag_.error(va->source_context, ErrorCode::E0600,
                                "linear value '" + src + "' was already consumed");
                    error_count_++;
                } else if (expr_produces_linear(va->expr)) {
                    env.create(name, va->source_context);
                }
            } else if (expr_produces_linear(va->expr)) {
                env.create(name, va->source_context);
            }
        } else if (auto* la = dynamic_cast<LambdaAlias*>(alias)) {
            check_node(la->lambda, env);
        } else if (auto* pa = dynamic_cast<PatternAlias*>(alias)) {
            check_node(pa->expr, env);
            track_linear_pattern(pa->pattern, type_of_expr(pa->expr), env,
                                 pa->source_context);
        }
    }
    check_node(node->expr, env);
}

void LinearityChecker::check_case(CaseExpr* node, LinearEnv& env) {
    check_node(node->expr, env);

    // Determine scrutinee variable name
    std::string scrut_name;
    if (node->expr->get_type() == AST_IDENTIFIER_EXPR)
        scrut_name = static_cast<IdentifierExpr*>(node->expr)->name->value;

    // Track which branches consume the scrutinee
    bool any_branch_consumes = false;
    bool any_branch_skips = false;

    for (auto* clause : node->clauses) {
        LinearEnv branch_env = env;

        if (!scrut_name.empty() && env.is_live(scrut_name)) {
            auto* pat = clause->pattern;

            // Constructor pattern `Linear fd` — this is the consumption point
            if (pat->get_type() == AST_CONSTRUCTOR_PATTERN) {
                auto* cp = static_cast<ConstructorPattern*>(pat);
                if (is_linear_constructor(cp->constructor_name)) {
                    branch_env.consume(scrut_name, clause->source_context);
                    any_branch_consumes = true;
                } else {
                    any_branch_skips = true;
                }
            } else {
                // Non-constructor patterns don't consume the linear value
                any_branch_skips = true;
            }
        }

        // Check for use-after-consume in branch body
        check_node(clause->body, branch_env);
    }

    // Branch consistency: if some branches consume and others don't, that's an issue
    // (but only if the scrutinee was linear)
    if (!scrut_name.empty() && env.is_live(scrut_name)) {
        if (any_branch_consumes && any_branch_skips) {
            diag_.error(node->source_context, ErrorCode::E0601,
                        "linear value '" + scrut_name +
                        "' consumed in some case branches but not all");
            error_count_++;
        }
        // If all branches consume, mark consumed in the outer env
        if (any_branch_consumes && !any_branch_skips) {
            env.consume(scrut_name, node->source_context);
        }
    }
}

void LinearityChecker::check_if(IfExpr* node, LinearEnv& env) {
    check_node(node->condition, env);

    LinearEnv then_env = env;
    LinearEnv else_env = env;

    check_node(node->thenExpr, then_env);
    check_node(node->elseExpr, else_env);

    // Check branch consistency for all tracked linear variables
    for (auto& [name, status] : env.vars) {
        if (status != LinearStatus::Live) continue;
        bool then_consumed = then_env.is_consumed(name);
        bool else_consumed = else_env.is_consumed(name);

        if (then_consumed && !else_consumed) {
            diag_.error(node->source_context, ErrorCode::E0601,
                        "linear value '" + name +
                        "' consumed in then-branch but not in else-branch");
            error_count_++;
        } else if (!then_consumed && else_consumed) {
            diag_.error(node->source_context, ErrorCode::E0601,
                        "linear value '" + name +
                        "' consumed in else-branch but not in then-branch");
            error_count_++;
        } else if (then_consumed && else_consumed) {
            env.consume(name, node->source_context);
        }
    }
}

void LinearityChecker::check_apply(ApplyExpr* node, LinearEnv& env) {
    // Check callee (may be a lambda FunctionExpr)
    if (auto* ec = dynamic_cast<ExprCall*>(node->call))
        check_node(ec->expr, env);

    // Check if any argument is a consumed linear variable
    for (auto& arg_variant : node->args) {
        AstNode* arg_node = std::holds_alternative<ExprNode*>(arg_variant)
            ? static_cast<AstNode*>(std::get<ExprNode*>(arg_variant))
            : static_cast<AstNode*>(std::get<ValueExpr*>(arg_variant));

        if (arg_node && arg_node->get_type() == AST_IDENTIFIER_EXPR) {
            auto* id = static_cast<IdentifierExpr*>(arg_node);
            std::string arg_name = id->name->value;
            if (env.is_consumed(arg_name)) {
                auto it = env.consumed_at.find(arg_name);
                std::string consumed_loc = (it != env.consumed_at.end())
                    ? " (consumed at " + it->second.to_string() + ")"
                    : "";
                diag_.error(node->source_context, ErrorCode::E0600,
                            "linear value '" + arg_name + "' was already consumed" + consumed_loc);
                error_count_++;
            }
        }
    }

    // Recurse into arguments
    for (auto& arg_variant : node->args) {
        AstNode* arg_node = std::holds_alternative<ExprNode*>(arg_variant)
            ? static_cast<AstNode*>(std::get<ExprNode*>(arg_variant))
            : static_cast<AstNode*>(std::get<ValueExpr*>(arg_variant));
        check_node(arg_node, env);
    }
}

void LinearityChecker::check_with(WithExpr* node, LinearEnv& env) {
    check_node(node->contextExpr, env);

    const std::string name = node->name->value;
    LinearEnv body_env = env;
    const bool resource_linear = expr_produces_linear(node->contextExpr);
    if (resource_linear)
        body_env.create(name, node->source_context);

    check_node(node->bodyExpr, body_env);

    // `with` always runs Closeable.close on the resource — that discharges the
    // Linear obligation for the bound name (idiomatic openFile / tcpConnect).
    if (resource_linear && body_env.is_live(name))
        body_env.consume(name, node->source_context);

    // Propagate consumption of outer linears that the body fully consumed.
    for (auto& [v, status] : env.vars) {
        if (status != LinearStatus::Live) continue;
        if (body_env.is_consumed(v))
            env.consume(v, node->source_context);
    }

    // Linears created inside the body (other than the with-bound resource)
    // that are still live are leaks at with-exit.
    for (auto& live : body_env.live_vars()) {
        if (live == name) continue;
        if (env.is_tracked(live)) continue; // outer; still live in outer too
        auto it = body_env.created_at.find(live);
        SourceLocation loc = (it != body_env.created_at.end())
            ? it->second : node->source_context;
        diag_.warning(loc,
                      "linear value '" + live + "' not consumed — possible resource leak; "
                      "use `case " + live + " of Linear fd -> close fd end` to release",
                      WarningFlag::UnhandledEffect);
    }
}

void LinearityChecker::check_function(FunctionExpr* node, LinearEnv& /*outer*/) {
    // Function bodies are a separate linear scope (v1: no linear captures).
    LinearEnv fn_env;

    // Track Linear parameters from the (zonked) function type when available.
    const MonoType* fn_ty = type_of_expr(node);
    std::vector<const MonoType*> param_tys;
    if (fn_ty) {
        const MonoType* cur = fn_ty;
        while (cur && cur->tag == MonoType::Arrow && param_tys.size() < node->patterns.size()) {
            param_tys.push_back(cur->param_type);
            cur = cur->return_type;
        }
    }
    for (size_t i = 0; i < node->patterns.size(); ++i) {
        const MonoType* pty = i < param_tys.size() ? param_tys[i] : nullptr;
        track_linear_pattern(node->patterns[i], pty, fn_env, node->source_context);
    }

    for (auto* body : node->bodies) {
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
            check_node(bwg->expr, fn_env);
        } else if (auto* g = dynamic_cast<BodyWithGuards*>(body)) {
            check_node(g->guard, fn_env);
            check_node(g->expr, fn_env);
        }
    }

    warn_unconsumed(fn_env);
}

} // namespace yona::compiler::typechecker
