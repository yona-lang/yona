/// TypeChecker — core HM inference for Yona.
///
/// Walks the AST and infers types for every node. Supports:
/// - Literals (Int, Float, String, Bool, Symbol, Unit)
/// - Identifiers (env lookup with instantiation)
/// - Let bindings (with let-polymorphism / generalization)
/// - Functions (parameter inference from usage)
/// - Application (unify callee with Arrow(arg, result))
/// - If expressions (condition must be Bool, branches must unify)
/// - Binary operators (dispatched via env lookup)
/// - Tuples, sequences, do-blocks

#include "typechecker/TypeChecker.h"

#include "analysis/BorrowEscapeAnalysis.h"

#include <variant>

namespace yona::compiler::typechecker {
using namespace yona::ast;

/// Simple edit distance for "did you mean?" suggestions.
static size_t edit_distance(const std::string& a, const std::string& b) {
    if (a.empty()) return b.size();
    if (b.empty()) return a.size();
    std::vector<std::vector<size_t>> dp(a.size() + 1, std::vector<size_t>(b.size() + 1));
    for (size_t i = 0; i <= a.size(); i++) dp[i][0] = i;
    for (size_t j = 0; j <= b.size(); j++) dp[0][j] = j;
    for (size_t i = 1; i <= a.size(); i++)
        for (size_t j = 1; j <= b.size(); j++)
            dp[i][j] = std::min({dp[i-1][j] + 1, dp[i][j-1] + 1,
                                  dp[i-1][j-1] + (a[i-1] != b[j-1] ? 1u : 0u)});
    return dp[a.size()][b.size()];
}

TypeChecker::TypeChecker(DiagnosticEngine& diag)
    : unifier_(arena_, uf_, diag), diag_(diag) {
    root_env_ = std::make_shared<TypeEnv>();
    register_builtins(*root_env_, arena_);
}

MonoTypePtr TypeChecker::check(AstNode* node) {
    return infer(node, root_env_, 0);
}

MonoTypePtr TypeChecker::type_of(AstNode* node) const {
    auto it = type_map_.find(node);
    return (it != type_map_.end()) ? it->second : nullptr;
}

void TypeChecker::record(AstNode* node, MonoTypePtr type) {
    type_map_[node] = type;
}

MonoTypePtr TypeChecker::zonk(MonoTypePtr type) {
    type = unifier_.resolve(type);
    if (!type) return nullptr;
    switch (type->tag) {
        case MonoType::Var: return type; // unresolved var
        case MonoType::Con: return type;
        case MonoType::Arrow:
            return arena_.make_arrow(zonk(type->param_type), zonk(type->return_type));
        case MonoType::App: {
            std::vector<MonoTypePtr> args;
            for (auto* a : type->args) args.push_back(zonk(a));
            return arena_.make_app(type->type_name, args);
        }
        case MonoType::MTuple: {
            std::vector<MonoTypePtr> elems;
            for (auto* e : type->elements) elems.push_back(zonk(e));
            return arena_.make_tuple(elems);
        }
        case MonoType::MRecord: {
            std::vector<std::pair<std::string, MonoTypePtr>> fields;
            for (auto& [name, ft] : type->record_fields) fields.push_back({name, zonk(ft)});
            MonoTypePtr rest = type->row_rest ? zonk(type->row_rest) : nullptr;
            return arena_.make_record(fields, rest);
        }
        default: return type;
    }
}

// ===== Main Dispatch =====

MonoTypePtr TypeChecker::infer(AstNode* node, std::shared_ptr<TypeEnv> env, int level) {
    if (!node) return arena_.make_con(TyCon::Unit);

    MonoTypePtr result = nullptr;
    auto ty = node->get_type();

    switch (ty) {
        case AST_INTEGER_EXPR:
            result = infer_integer(node); break;
        case AST_FLOAT_EXPR:
            result = infer_float(node); break;
        case AST_STRING_EXPR:
            result = infer_string(node); break;
        case AST_TRUE_LITERAL_EXPR:
        case AST_FALSE_LITERAL_EXPR:
            result = infer_bool(node); break;
        case AST_SYMBOL_EXPR:
            result = infer_symbol(node); break;
        case AST_LITERAL_EXPR: {
            // LiteralExpr<T> — determine type from the actual literal value type
            if (dynamic_cast<LiteralExpr<float>*>(node))
                result = infer_float(node);
            else if (dynamic_cast<LiteralExpr<bool>*>(node))
                result = infer_bool(node);
            else if (dynamic_cast<LiteralExpr<std::string>*>(node))
                result = infer_string(node);
            else if (dynamic_cast<LiteralExpr<int>*>(node))
                result = infer_integer(node);
            else
                result = arena_.make_con(TyCon::Unit);
            break;
        }
        case AST_UNIT_EXPR:
            result = arena_.make_con(TyCon::Unit); break;
        case AST_MAIN:
            result = infer(static_cast<MainNode*>(node)->node, env, level); break;
        case AST_IDENTIFIER_EXPR:
            result = infer_identifier(static_cast<IdentifierExpr*>(node), env, level); break;
        case AST_LET_EXPR:
            result = infer_let(static_cast<LetExpr*>(node), env, level); break;
        case AST_FUNCTION_EXPR:
            result = infer_function(static_cast<FunctionExpr*>(node), env, level); break;
        case AST_APPLY_EXPR:
            result = infer_apply(static_cast<ApplyExpr*>(node), env, level); break;
        case AST_IF_EXPR:
            result = infer_if(static_cast<IfExpr*>(node), env, level); break;
        case AST_TUPLE_EXPR:
            result = infer_tuple(static_cast<TupleExpr*>(node), env, level); break;
        case AST_VALUES_SEQUENCE_EXPR:
            result = infer_seq(static_cast<ValuesSequenceExpr*>(node), env, level); break;
        case AST_DO_EXPR:
            result = infer_do(static_cast<DoExpr*>(node), env, level); break;
        case AST_CASE_EXPR:
            result = infer_case(static_cast<CaseExpr*>(node), env, level); break;
        case AST_CONS_LEFT_EXPR:
            result = infer_cons(static_cast<ConsLeftExpr*>(node), env, level); break;
        case AST_RECORD_LITERAL_EXPR: {
            auto* rec = static_cast<RecordLiteralExpr*>(node);
            std::vector<std::pair<std::string, MonoTypePtr>> fields;
            for (auto& [name, expr] : rec->fields) {
                auto* field_type = infer(expr, env, level);
                fields.push_back({name, field_type});
            }
            result = arena_.make_record(fields);
            break;
        }
        case AST_FIELD_ACCESS_EXPR: {
            auto* fa = static_cast<FieldAccessExpr*>(node);
            auto* obj_type = infer(fa->identifier, env, level);
            // Constrain obj to be a record with this field
            auto* field_var = arena_.fresh_var(level);
            uf_.add_var(field_var->var_id, level);
            auto* row_var = arena_.fresh_var(level);
            uf_.add_var(row_var->var_id, level);
            auto* expected_record = arena_.make_record(
                {{fa->name->value, field_var}}, row_var);
            unifier_.unify(obj_type, expected_record, node->source_context,
                           "in field access '." + fa->name->value + "'");
            result = unifier_.resolve(field_var);
            break;
        }
        case AST_PERFORM_EXPR:
            result = infer_perform(static_cast<PerformExpr*>(node), env, level); break;
        case AST_HANDLE_EXPR:
            result = infer_handle(static_cast<HandleExpr*>(node), env, level); break;

        // === Phase 1: Quick wins ===

        case AST_BYTE_EXPR:
            result = arena_.make_con(TyCon::Int); break;  // byte as Int
        case AST_CHARACTER_EXPR:
            result = arena_.make_con(TyCon::Int); break;  // char code as Int

        case AST_RANGE_SEQUENCE_EXPR:
            // [start..end] or [start..end..step] — all Int, returns Seq Int
            result = arena_.make_app("Seq", {arena_.make_con(TyCon::Int)}); break;

        case AST_LOGICAL_NOT_OP_EXPR: {
            auto* e = static_cast<LogicalNotOpExpr*>(node);
            auto* t = infer(e->expr, env, level);
            unifier_.unify(t, arena_.make_con(TyCon::Bool), node->source_context, "in logical not");
            result = arena_.make_con(TyCon::Bool);
            break;
        }

        case AST_RAISE_EXPR: {
            // raise expr — type of the raise is a fresh var (bottom/never returns)
            auto* re = static_cast<RaiseExpr*>(node);
            infer(re->value, env, level);
            result = arena_.fresh_var(level);
            uf_.add_var(result->var_id, level);
            break;
        }

        case AST_IMPORT_EXPR: {
            auto* imp = static_cast<ImportExpr*>(node);
            auto import_env = env->child();
            // Bind imported names as fresh type variables
            for (auto* clause : imp->clauses) {
                if (auto* fi = dynamic_cast<FunctionsImport*>(clause)) {
                    for (auto* fa : fi->aliases) {
                        // Use alias name if set, otherwise original name
                        std::string bind_name = (fa->alias && !fa->alias->value.empty())
                            ? fa->alias->value : fa->name->value;
                        auto* v = arena_.fresh_var(level);
                        uf_.add_var(v->var_id, level);
                        import_env->bind(bind_name, v);
                    }
                }
                // Wildcard module import: bind nothing specific (names resolved at codegen)
            }
            result = infer(imp->expr, import_env, level);
            break;
        }

        case AST_EXTERN_DECL: {
            // extern name : Type in body — type is body type
            auto* ext = static_cast<ExternDeclExpr*>(node);
            auto child_env = env->child();
            // Bind the extern name as a fresh polymorphic var
            auto* extern_var = arena_.fresh_var(level);
            uf_.add_var(extern_var->var_id, level);
            child_env->bind(ext->name, extern_var);
            result = infer(ext->body, child_env, level);
            break;
        }

        // === Phase 1: Generators ===

        case AST_SEQ_GENERATOR_EXPR: {
            auto* gen = static_cast<SeqGeneratorExpr*>(node);
            auto gen_env = env->child();
            bind_collection_extractor(gen->collectionExtractor, gen_env, level);
            auto* body_type = infer(gen->reducerExpr, gen_env, level);
            result = arena_.make_app("Seq", {body_type});
            break;
        }

        case AST_SET_GENERATOR_EXPR: {
            auto* gen = static_cast<SetGeneratorExpr*>(node);
            auto gen_env = env->child();
            bind_collection_extractor(gen->collectionExtractor, gen_env, level);
            auto* body_type = infer(gen->reducerExpr, gen_env, level);
            result = arena_.make_app("Set", {body_type});
            break;
        }

        case AST_DICT_GENERATOR_EXPR: {
            auto* gen = static_cast<DictGeneratorExpr*>(node);
            auto gen_env = env->child();
            bind_collection_extractor(gen->collectionExtractor, gen_env, level);
            auto* key_type = infer(gen->reducerExpr->key, gen_env, level);
            auto* val_type = infer(gen->reducerExpr->value, gen_env, level);
            result = arena_.make_app("Dict", {key_type, val_type});
            break;
        }

        case AST_SET_EXPR: {
            auto* se = static_cast<SetExpr*>(node);
            if (se->values.empty()) {
                result = arena_.make_app("Set", {arena_.fresh_var(level)});
            } else {
                auto* elem_type = infer(se->values[0], env, level);
                for (size_t i = 1; i < se->values.size(); i++) {
                    auto* t = infer(se->values[i], env, level);
                    unifier_.unify(elem_type, t, node->source_context,
                                   "in set literal (all elements must have same type)");
                }
                result = arena_.make_app("Set", {unifier_.resolve(elem_type)});
            }
            break;
        }

        case AST_DICT_EXPR: {
            auto* de = static_cast<DictExpr*>(node);
            auto* key_var = arena_.fresh_var(level);
            uf_.add_var(key_var->var_id, level);
            auto* val_var = arena_.fresh_var(level);
            uf_.add_var(val_var->var_id, level);
            for (auto& [k, v] : de->values) {
                auto* kt = infer(k, env, level);
                auto* vt = infer(v, env, level);
                unifier_.unify(key_var, kt, node->source_context, "in dict literal key");
                unifier_.unify(val_var, vt, node->source_context, "in dict literal value");
            }
            result = arena_.make_app("Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
            break;
        }

        // === Phase 2: Control flow completions ===

        case AST_WITH_EXPR: {
            auto* we = static_cast<WithExpr*>(node);
            infer(we->contextExpr, env, level);
            auto child_env = env->child();
            auto* ctx_var = arena_.fresh_var(level);
            uf_.add_var(ctx_var->var_id, level);
            child_env->bind(we->name->value, ctx_var);
            result = infer(we->bodyExpr, child_env, level);
            break;
        }

        case AST_TRY_CATCH_EXPR: {
            auto* tc = static_cast<TryCatchExpr*>(node);
            auto* try_type = infer(tc->tryExpr, env, level);
            if (tc->catchExpr) {
                for (auto* cp : tc->catchExpr->patterns) {
                    // Extract body from the catch pattern's variant
                    if (auto* pwog = std::get_if<PatternWithoutGuards*>(&cp->pattern)) {
                        if (*pwog && (*pwog)->expr) {
                            auto* catch_type = infer((*pwog)->expr, env, level);
                            unifier_.unify(try_type, catch_type, node->source_context,
                                           "in try/catch (all branches must have same type)");
                        }
                    }
                }
            }
            result = unifier_.resolve(try_type);
            break;
        }

        case AST_CONS_RIGHT_EXPR: {
            // Same as cons left but reversed: seq :> elem
            auto* cr = static_cast<ConsRightExpr*>(node);
            auto* seq_type = infer(cr->left, env, level);
            auto* elem_type = infer(cr->right, env, level);
            auto* expected_seq = arena_.make_app("Seq", {elem_type});
            unifier_.unify(seq_type, expected_seq, node->source_context, "in cons right (:>)");
            result = unifier_.resolve(expected_seq);
            break;
        }

        case AST_FIELD_UPDATE_EXPR: {
            auto* fu = static_cast<FieldUpdateExpr*>(node);
            auto* obj_type = infer(fu->identifier, env, level);
            for (auto& [name, expr] : fu->updates)
                infer(expr, env, level);
            result = obj_type; // update returns same type as original
            break;
        }

        default:
            // Binary operators
            if (auto* binop = dynamic_cast<BinaryOpExpr*>(node)) {
                result = infer_binary(binop, env, level);
                break;
            }
            // Fallback: return a fresh variable
            result = arena_.fresh_var(level);
            uf_.add_var(result->var_id, level);
            break;
    }

    if (result) record(node, result);
    return result;
}

// ===== Literals =====

MonoTypePtr TypeChecker::infer_integer(AstNode*) { return arena_.make_con(TyCon::Int); }
MonoTypePtr TypeChecker::infer_float(AstNode*)   { return arena_.make_con(TyCon::Float); }
MonoTypePtr TypeChecker::infer_string(AstNode*)  { return arena_.make_con(TyCon::String); }
MonoTypePtr TypeChecker::infer_bool(AstNode*)    { return arena_.make_con(TyCon::Bool); }
MonoTypePtr TypeChecker::infer_symbol(AstNode*)  { return arena_.make_con(TyCon::Symbol); }

// ===== Identifier =====

MonoTypePtr TypeChecker::infer_identifier(IdentifierExpr* node,
                                           std::shared_ptr<TypeEnv> env, int level) {
    auto scheme = env->lookup(node->name->value);
    if (!scheme) {
        std::string msg = "undefined variable '" + node->name->value + "'";
        // Suggest closest match
        auto names = env->all_names();
        std::string best;
        size_t best_dist = 4; // max distance to suggest
        for (auto& n : names) {
            auto d = edit_distance(node->name->value, n);
            if (d < best_dist) { best_dist = d; best = n; }
        }
        if (!best.empty())
            msg += "; did you mean '" + best + "'?";
        diag_.error(node->source_context, ErrorCode::E0103, msg);
        error_count_++;
        return arena_.fresh_var(level);
    }
    return instantiate(*scheme, level);
}

// ===== Let Binding =====

/// Pre-scan nested let expressions and bind all lambda alias names with fresh
/// type vars. This enables mutual recursion across nested let blocks —
/// `let f = ... g ... in let g = ... f ... in expr` — matching the codegen's
/// deferred compilation behavior.
static void prescan_let_lambdas(AstNode* node, std::shared_ptr<TypeEnv> env,
                                 TypeArena& arena, UnionFind& uf, int level) {
    auto* let_node = dynamic_cast<LetExpr*>(node);
    if (!let_node) return;

    for (auto* alias : let_node->aliases) {
        if (auto* la = dynamic_cast<LambdaAlias*>(alias)) {
            // Only pre-bind if not already bound (avoids overwriting)
            if (!env->lookup(la->name->value)) {
                auto* v = arena.fresh_var(level + 1);
                uf.add_var(v->var_id, level + 1);
                env->bind(la->name->value, v);
            }
        }
    }
    // Recurse into the body to find nested lets
    prescan_let_lambdas(let_node->expr, env, arena, uf, level);
}

MonoTypePtr TypeChecker::infer_let(LetExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    auto child_env = env->child();

    // Pass 0: Pre-scan this and nested let blocks for all lambda names.
    // Enables mutual recursion across nested lets.
    prescan_let_lambdas(node, child_env, arena_, uf_, level);

    // Pass 1: Pre-bind all LambdaAlias names with fresh type vars.
    // This enables mutual recursion within the same let block.
    struct LambdaPrelim { LambdaAlias* la; MonoTypePtr var; };
    std::vector<LambdaPrelim> lambda_prelims;
    for (auto* alias : node->aliases) {
        if (auto* la = dynamic_cast<LambdaAlias*>(alias)) {
            // Retrieve the pre-scanned binding
            auto existing = child_env->lookup(la->name->value);
            auto* self_var = existing ? existing->body : arena_.fresh_var(level + 1);
            if (!existing) uf_.add_var(self_var->var_id, level + 1);
            child_env->bind(la->name->value, self_var);
            lambda_prelims.push_back({la, self_var});
        }
    }

    // Pass 2: Infer all alias types.
    size_t lambda_idx = 0;
    for (auto* alias : node->aliases) {
        if (auto* va = dynamic_cast<ValueAlias*>(alias)) {
            auto* rhs_type = infer(va->expr, child_env, level + 1);
            auto scheme = generalize(rhs_type, level);
            child_env->bind_scheme(va->identifier->name->value, scheme);
        } else if (auto* pa = dynamic_cast<PatternAlias*>(alias)) {
            auto* rhs_type = infer(pa->expr, child_env, level + 1);
            auto* pat_type = infer_pattern(pa->pattern, child_env, level + 1);
            unifier_.unify(rhs_type, pat_type, pa->source_context,
                           "in pattern destructuring");
        } else if (dynamic_cast<LambdaAlias*>(alias)) {
            auto& prelim = lambda_prelims[lambda_idx++];
            auto* fn_type = infer(prelim.la->lambda, child_env, level + 1);
            unifier_.unify(prelim.var, fn_type, prelim.la->lambda->source_context,
                           "in recursive function '" + prelim.la->name->value + "'");
            auto scheme = generalize(fn_type, level);
            child_env->bind_scheme(prelim.la->name->value, scheme);
        }
    }

    return infer(node->expr, child_env, level);
}

// ===== Function =====

MonoTypePtr TypeChecker::infer_function(FunctionExpr* node,
                                         std::shared_ptr<TypeEnv> env, int level) {
    auto fn_env = env->child();

    // Create fresh type variables for parameters
    std::vector<MonoTypePtr> param_types;
    for (auto* pat : node->patterns) {
        auto* param_var = arena_.fresh_var(level);
        uf_.add_var(param_var->var_id, level);
        param_types.push_back(param_var);

        // Bind pattern variable name
        if (pat->get_type() == AST_PATTERN_VALUE) {
            auto* pv = static_cast<PatternValue*>(pat);
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                fn_env->bind((*id)->name->value, param_var);
        }
    }

    // Infer body type
    MonoTypePtr body_type = nullptr;
    for (auto* body : node->bodies) {
        if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
            auto* bt = infer(bwg->expr, fn_env, level);
            if (!body_type) body_type = bt;
            else unifier_.unify(body_type, bt, node->source_context, "in function body");
        }
    }
    if (!body_type) body_type = arena_.make_con(TyCon::Unit);

    // Build curried arrow type: a -> b -> c -> ret
    MonoTypePtr fn_type = body_type;
    for (int i = (int)param_types.size() - 1; i >= 0; i--)
        fn_type = arena_.make_arrow(param_types[i], fn_type);

    check_param_borrow_annotations(node);
    return fn_type;
}

void TypeChecker::check_param_borrow_annotations(FunctionExpr* node) {
    if (node->param_borrow.empty()) return;

    for (size_t i = 0; i < node->patterns.size(); ++i) {
        bool want = i < node->param_borrow.size() && node->param_borrow[i];
        if (!want) continue;

        auto* pat = node->patterns[i];
        if (pat->get_type() != AST_PATTERN_VALUE) {
            diag_.error(pat->source_context, ErrorCode::E0603,
                        "`@borrow` is only allowed on simple identifier parameters");
            error_count_++;
            continue;
        }
        auto* pv = static_cast<PatternValue*>(pat);
        if (!std::holds_alternative<IdentifierExpr*>(pv->expr)) {
            diag_.error(pat->source_context, ErrorCode::E0603,
                        "`@borrow` is only allowed on simple identifier parameters");
            error_count_++;
            continue;
        }
        const std::string& pname = std::get<IdentifierExpr*>(pv->expr)->name->value;

        bool escapes = false;
        for (auto* body : node->bodies) {
            if (auto* bwg = dynamic_cast<BodyWithoutGuards*>(body)) {
                if (compiler::analysis::heap_param_may_escape(bwg->expr, pname, true))
                    escapes = true;
            } else if (auto* g = dynamic_cast<BodyWithGuards*>(body)) {
                if (compiler::analysis::heap_param_may_escape(g->guard, pname, false)
                    || compiler::analysis::heap_param_may_escape(g->expr, pname, true))
                    escapes = true;
            }
        }
        if (escapes) {
            diag_.error(pat->source_context, ErrorCode::E0603,
                        "borrowed parameter '" + pname + "' must not escape "
                        "(return, store in literal, capture in closure, or be a case scrutinee)");
            error_count_++;
        }
    }
}

// ===== Application =====

MonoTypePtr TypeChecker::infer_apply(ApplyExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    // Infer callee
    MonoTypePtr callee_type = nullptr;
    if (auto* nc = dynamic_cast<NameCall*>(node->call)) {
        auto scheme = env->lookup(nc->name->value);
        if (scheme)
            callee_type = instantiate(*scheme, level);
        else {
            std::string msg = "undefined function '" + nc->name->value + "'";
            auto names = env->all_names();
            std::string best;
            size_t best_dist = 4;
            for (auto& n : names) {
                auto d = edit_distance(nc->name->value, n);
                if (d < best_dist) { best_dist = d; best = n; }
            }
            if (!best.empty())
                msg += "; did you mean '" + best + "'?";
            diag_.error(node->source_context, ErrorCode::E0104, msg);
            error_count_++;
            return arena_.fresh_var(level);
        }
    } else if (auto* ec = dynamic_cast<ExprCall*>(node->call)) {
        callee_type = infer(ec->expr, env, level);
    } else {
        callee_type = arena_.fresh_var(level);
        uf_.add_var(callee_type->var_id, level);
    }

    // Apply each argument
    MonoTypePtr result_type = callee_type;
    for (auto& arg_variant : node->args) {
        AstNode* arg_node = std::holds_alternative<ExprNode*>(arg_variant)
            ? static_cast<AstNode*>(std::get<ExprNode*>(arg_variant))
            : static_cast<AstNode*>(std::get<ValueExpr*>(arg_variant));

        auto* arg_type = infer(arg_node, env, level);

        auto* result_var = arena_.fresh_var(level);
        uf_.add_var(result_var->var_id, level);
        auto* expected_fn = arena_.make_arrow(arg_type, result_var);

        if (!unifier_.unify(result_type, expected_fn, node->source_context,
                            "in function application"))
            return result_var;

        result_type = result_var;
    }

    return unifier_.resolve(result_type);
}

// ===== If =====

MonoTypePtr TypeChecker::infer_if(IfExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    auto* cond_type = infer(node->condition, env, level);
    unifier_.unify(cond_type, arena_.make_con(TyCon::Bool), node->source_context,
                   "in if condition (expected Bool)");

    auto* then_type = infer(node->thenExpr, env, level);
    auto* else_type = infer(node->elseExpr, env, level);
    unifier_.unify(then_type, else_type, node->source_context,
                   "in if branches (then and else must have same type)");

    return unifier_.resolve(then_type);
}

// ===== Binary Operators =====

std::string TypeChecker::op_name(AstNodeType type) {
    switch (type) {
        case AST_ADD_EXPR: return "+";
        case AST_SUBTRACT_EXPR: return "-";
        case AST_MULTIPLY_EXPR: return "*";
        case AST_DIVIDE_EXPR: return "/";
        case AST_MODULO_EXPR: return "%";
        case AST_POWER_EXPR: return "**";
        case AST_EQ_EXPR: return "==";
        case AST_NEQ_EXPR: return "!=";
        case AST_LT_EXPR: return "<";
        case AST_GT_EXPR: return ">";
        case AST_LTE_EXPR: return "<=";
        case AST_GTE_EXPR: return ">=";
        case AST_LOGICAL_AND_EXPR: return "&&";
        case AST_LOGICAL_OR_EXPR: return "||";
        case AST_JOIN_EXPR: return "++";
        case AST_CONS_LEFT_EXPR: return "::";
        case AST_PIPE_LEFT_EXPR: return "<|";
        case AST_PIPE_RIGHT_EXPR: return "|>";
        default: return "?";
    }
}

MonoTypePtr TypeChecker::infer_binary(BinaryOpExpr* node,
                                       std::shared_ptr<TypeEnv> env, int level) {
    auto* left_type = infer(node->left, env, level);
    auto* right_type = infer(node->right, env, level);

    std::string name = op_name(node->get_type());
    auto scheme = env->lookup(name);
    if (!scheme) {
        // Fallback: assume left_type -> left_type -> left_type
        unifier_.unify(left_type, right_type, node->source_context, "in operator " + name);
        return unifier_.resolve(left_type);
    }

    // Instantiate operator type and unify with arguments
    auto* op_type = instantiate(*scheme, level);
    auto* result_var = arena_.fresh_var(level);
    uf_.add_var(result_var->var_id, level);

    auto* expected = arena_.make_arrow(left_type, arena_.make_arrow(right_type, result_var));
    unifier_.unify(op_type, expected, node->source_context, "in operator " + name);

    return unifier_.resolve(result_var);
}

// ===== Tuple =====

MonoTypePtr TypeChecker::infer_tuple(TupleExpr* node,
                                      std::shared_ptr<TypeEnv> env, int level) {
    std::vector<MonoTypePtr> elem_types;
    for (auto* v : node->values)
        elem_types.push_back(infer(v, env, level));
    return arena_.make_tuple(elem_types);
}

// ===== Sequence =====

MonoTypePtr TypeChecker::infer_seq(ValuesSequenceExpr* node,
                                    std::shared_ptr<TypeEnv> env, int level) {
    if (node->values.empty())
        return arena_.make_app("Seq", {arena_.fresh_var(level)});

    auto* elem_type = infer(node->values[0], env, level);
    for (size_t i = 1; i < node->values.size(); i++) {
        auto* t = infer(node->values[i], env, level);
        unifier_.unify(elem_type, t, node->source_context,
                       "in sequence literal (all elements must have same type)");
    }
    return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
}

// ===== Do Block =====

MonoTypePtr TypeChecker::infer_do(DoExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    MonoTypePtr last_type = arena_.make_con(TyCon::Unit);
    for (auto* step : node->steps)
        last_type = infer(step, env, level);
    return last_type;
}

// ===== Generalization =====

void TypeChecker::collect_free_vars(MonoTypePtr type, int level, std::vector<TypeId>& vars) {
    type = unifier_.resolve(type);
    if (!type) return;
    if (type->tag == MonoType::Var) {
        if (uf_.level(type->var_id) > level) {
            // Check not already collected
            for (auto id : vars) if (id == type->var_id) return;
            vars.push_back(type->var_id);
        }
        return;
    }
    if (type->tag == MonoType::Arrow) {
        collect_free_vars(type->param_type, level, vars);
        collect_free_vars(type->return_type, level, vars);
    }
    if (type->tag == MonoType::App)
        for (auto* a : type->args) collect_free_vars(a, level, vars);
    if (type->tag == MonoType::MTuple)
        for (auto* e : type->elements) collect_free_vars(e, level, vars);
    if (type->tag == MonoType::MRecord) {
        for (auto& [_, ft] : type->record_fields) collect_free_vars(ft, level, vars);
        if (type->row_rest) collect_free_vars(type->row_rest, level, vars);
    }
}

TypeScheme TypeChecker::generalize(MonoTypePtr type, int level) {
    std::vector<TypeId> free_vars;
    collect_free_vars(type, level, free_vars);
    return TypeScheme(free_vars, type);
}

MonoTypePtr TypeChecker::substitute(MonoTypePtr type,
                                     const std::unordered_map<TypeId, MonoTypePtr>& subst) {
    type = unifier_.resolve(type);
    if (!type) return nullptr;
    if (type->tag == MonoType::Var) {
        auto it = subst.find(type->var_id);
        if (it != subst.end()) return it->second;
        return type;
    }
    if (type->tag == MonoType::Con) return type;
    if (type->tag == MonoType::Arrow)
        return arena_.make_arrow(substitute(type->param_type, subst),
                                  substitute(type->return_type, subst));
    if (type->tag == MonoType::App) {
        std::vector<MonoTypePtr> new_args;
        for (auto* a : type->args) new_args.push_back(substitute(a, subst));
        return arena_.make_app(type->type_name, new_args);
    }
    if (type->tag == MonoType::MTuple) {
        std::vector<MonoTypePtr> new_elems;
        for (auto* e : type->elements) new_elems.push_back(substitute(e, subst));
        return arena_.make_tuple(new_elems);
    }
    if (type->tag == MonoType::MRecord) {
        std::vector<std::pair<std::string, MonoTypePtr>> new_fields;
        for (auto& [name, ft] : type->record_fields)
            new_fields.push_back({name, substitute(ft, subst)});
        MonoTypePtr new_rest = type->row_rest ? substitute(type->row_rest, subst) : nullptr;
        return arena_.make_record(new_fields, new_rest);
    }
    return type;
}

MonoTypePtr TypeChecker::instantiate(const TypeScheme& scheme, int level) {
    if (scheme.quantified_vars.empty() && scheme.constraints.empty())
        return scheme.body;

    std::unordered_map<TypeId, MonoTypePtr> subst;
    for (auto id : scheme.quantified_vars) {
        auto* fresh = arena_.fresh_var(level);
        uf_.add_var(fresh->var_id, level);
        subst[id] = fresh;
    }

    // Record deferred constraints (substituted)
    for (auto& c : scheme.constraints) {
        auto* subst_type = substitute(c.type, subst);
        deferred_constraints_.push_back({c.trait_name, subst_type, {}, ""});
    }

    return substitute(scheme.body, subst);
}

// ===== Case Expression =====

MonoTypePtr TypeChecker::infer_case(CaseExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    auto* scrut_type = infer(node->expr, env, level);

    MonoTypePtr result_type = nullptr;

    for (auto* clause : node->clauses) {
        auto clause_env = env->child();

        // Infer pattern and bind variables
        auto* pat_type = infer_pattern(clause->pattern, clause_env, level);
        unifier_.unify(scrut_type, pat_type, clause->source_context, "in case pattern");

        // Infer body
        auto* body_type = infer(clause->body, clause_env, level);

        if (!result_type)
            result_type = body_type;
        else
            unifier_.unify(result_type, body_type, clause->source_context,
                           "in case branches (all must have same type)");
    }

    return result_type ? unifier_.resolve(result_type) : arena_.make_con(TyCon::Unit);
}

// ===== Pattern Inference =====

MonoTypePtr TypeChecker::infer_pattern(PatternNode* pat, std::shared_ptr<TypeEnv> env, int level) {
    if (!pat) return arena_.fresh_var(level);

    switch (pat->get_type()) {
        case AST_UNDERSCORE_PATTERN: {
            // Wildcard: matches anything
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            return v;
        }

        case AST_PATTERN_VALUE: {
            auto* pv = static_cast<PatternValue*>(pat);
            // Identifier binding: fresh var, bind in env
            if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr)) {
                auto* v = arena_.fresh_var(level);
                uf_.add_var(v->var_id, level);
                env->bind((*id)->name->value, v);
                return v;
            }
            // Symbol literal
            if (std::get_if<SymbolExpr*>(&pv->expr))
                return arena_.make_con(TyCon::Symbol);
            // Integer literal
            if (auto* lit = std::get_if<LiteralExpr<void*>*>(&pv->expr)) {
                auto* an = reinterpret_cast<AstNode*>(*lit);
                if (an->get_type() == AST_INTEGER_EXPR) return arena_.make_con(TyCon::Int);
                if (an->get_type() == AST_FLOAT_EXPR) return arena_.make_con(TyCon::Float);
                if (an->get_type() == AST_STRING_EXPR) return arena_.make_con(TyCon::String);
            }
            // Fallback
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            return v;
        }

        case AST_HEAD_TAILS_PATTERN: {
            auto* htp = static_cast<HeadTailsPattern*>(pat);
            auto* elem_type = arena_.fresh_var(level);
            uf_.add_var(elem_type->var_id, level);

            // Head patterns: each must match elem_type
            for (auto* head_pat : htp->heads) {
                auto* head_type = infer_pattern(head_pat, env, level);
                unifier_.unify(elem_type, head_type, pat->source_context, "in head-tail pattern");
            }

            // Tail: must be Seq(elem_type)
            if (htp->tail) {
                auto* tail_type = infer_pattern(htp->tail, env, level);
                auto* seq_type = arena_.make_app("Seq", {elem_type});
                unifier_.unify(tail_type, seq_type, pat->source_context, "in tail pattern");
            }

            return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
        }

        case AST_SEQ_PATTERN: {
            auto* sp = static_cast<SeqPattern*>(pat);
            auto* elem_type = arena_.fresh_var(level);
            uf_.add_var(elem_type->var_id, level);
            for (auto* sub : sp->patterns) {
                auto* sub_type = infer_pattern(sub, env, level);
                unifier_.unify(elem_type, sub_type, pat->source_context, "in sequence pattern");
            }
            return arena_.make_app("Seq", {unifier_.resolve(elem_type)});
        }

        case AST_TUPLE_PATTERN: {
            auto* tp = static_cast<TuplePattern*>(pat);
            std::vector<MonoTypePtr> elem_types;
            for (auto* sub : tp->patterns)
                elem_types.push_back(infer_pattern(sub, env, level));
            return arena_.make_tuple(elem_types);
        }

        case AST_CONSTRUCTOR_PATTERN: {
            auto* cp = static_cast<ConstructorPattern*>(pat);
            auto ctor_it = constructor_registry_.find(cp->constructor_name);
            if (ctor_it != constructor_registry_.end()) {
                auto& info = ctor_it->second;
                // Instantiate ADT type params with fresh vars
                std::vector<MonoTypePtr> type_arg_vars;
                for (size_t i = 0; i < info.type_params.size(); i++) {
                    auto* v = arena_.fresh_var(level);
                    uf_.add_var(v->var_id, level);
                    type_arg_vars.push_back(v);
                }
                // Bind sub-patterns — each gets the corresponding type arg
                for (size_t i = 0; i < cp->sub_patterns.size(); i++) {
                    auto* sub_type = infer_pattern(cp->sub_patterns[i], env, level);
                    if (i < type_arg_vars.size())
                        unifier_.unify(sub_type, type_arg_vars[i], pat->source_context,
                                       "in constructor pattern '" + cp->constructor_name + "'");
                }
                return arena_.make_app(info.adt_name, type_arg_vars);
            }
            // Unknown constructor — fresh var
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            for (auto* sub : cp->sub_patterns)
                infer_pattern(sub, env, level);
            return v;
        }

        case AST_TYPED_PATTERN: {
            auto* tp = static_cast<TypedPattern*>(pat);
            // Map type name to MonoType
            MonoTypePtr bound_type;
            if (tp->type_name == "Int")         bound_type = arena_.make_con(TyCon::Int);
            else if (tp->type_name == "Float")  bound_type = arena_.make_con(TyCon::Float);
            else if (tp->type_name == "Bool")   bound_type = arena_.make_con(TyCon::Bool);
            else if (tp->type_name == "String") bound_type = arena_.make_con(TyCon::String);
            else if (tp->type_name == "Symbol") bound_type = arena_.make_con(TyCon::Symbol);
            else if (tp->type_name == "ByteArray")  bound_type = arena_.make_con(TyCon::ByteArray);
            else {
                // Unknown or ADT type — use a named App type
                bound_type = arena_.make_app(tp->type_name, {});
            }
            env->bind(tp->binding_name, bound_type);
            // The pattern matches a sum type containing this alternative
            // Return the scrutinee type (sum) rather than the inner type
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            return v;
        }

        case AST_OR_PATTERN: {
            auto* op = static_cast<OrPattern*>(pat);
            MonoTypePtr or_type = nullptr;
            for (auto& alt : op->patterns) {
                auto* alt_type = infer_pattern(alt.get(), env, level);
                if (!or_type) or_type = alt_type;
                else unifier_.unify(or_type, alt_type, pat->source_context, "in or-pattern");
            }
            return or_type ? or_type : arena_.fresh_var(level);
        }

        case AST_DICT_PATTERN: {
            auto* dp = static_cast<DictPattern*>(pat);
            auto* key_var = arena_.fresh_var(level);
            uf_.add_var(key_var->var_id, level);
            auto* val_var = arena_.fresh_var(level);
            uf_.add_var(val_var->var_id, level);
            for (auto& [key_pat, val_pat] : dp->keyValuePairs) {
                auto* kt = infer_pattern(key_pat, env, level);
                auto* vt = infer_pattern(val_pat, env, level);
                unifier_.unify(key_var, kt, pat->source_context, "in dict pattern key");
                unifier_.unify(val_var, vt, pat->source_context, "in dict pattern value");
            }
            return arena_.make_app("Dict", {unifier_.resolve(key_var), unifier_.resolve(val_var)});
        }

        case AST_RECORD_PATTERN: {
            auto* rp = static_cast<RecordPattern*>(pat);
            for (auto& [name_expr, sub_pat] : rp->items) {
                auto* sub_type = infer_pattern(sub_pat, env, level);
                if (name_expr && sub_pat->get_type() == AST_PATTERN_VALUE) {
                    auto* pv = static_cast<PatternValue*>(sub_pat);
                    if (auto* id = std::get_if<IdentifierExpr*>(&pv->expr))
                        env->bind((*id)->name->value, sub_type);
                }
            }
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            return v;
        }

        case AST_AS_DATA_STRUCTURE_PATTERN: {
            auto* asp = static_cast<AsDataStructurePattern*>(pat);
            auto* inner_type = infer_pattern(static_cast<PatternNode*>(asp->pattern), env, level);
            if (asp->identifier)
                env->bind(asp->identifier->name->value, inner_type);
            return inner_type;
        }

        default: {
            auto* v = arena_.fresh_var(level);
            uf_.add_var(v->var_id, level);
            return v;
        }
    }
}

// ===== Collection Extractor Binding =====

void TypeChecker::bind_collection_extractor(CollectionExtractorExpr* ce,
                                             std::shared_ptr<TypeEnv> env, int level) {
    if (!ce) return;

    if (auto* vce = dynamic_cast<ValueCollectionExtractorExpr*>(ce)) {
        // Infer collection type to get element type
        auto* elem_type = arena_.fresh_var(level);
        uf_.add_var(elem_type->var_id, level);
        if (vce->collection) {
            auto* col_type = infer(vce->collection, env, level);
            // Collection should be Seq(elem_type) — unify
            auto* expected = arena_.make_app("Seq", {elem_type});
            unifier_.unify(col_type, expected, ce->source_context,
                           "in generator collection");
        }
        // Bind the iteration variable
        if (auto* id = std::get_if<IdentifierExpr*>(&vce->expr))
            env->bind((*id)->name->value, elem_type);
        // Infer guard condition if present
        if (vce->condition)
            infer(vce->condition, env, level);
    } else if (auto* kvce = dynamic_cast<KeyValueCollectionExtractorExpr*>(ce)) {
        auto* key_type = arena_.fresh_var(level);
        uf_.add_var(key_type->var_id, level);
        auto* val_type = arena_.fresh_var(level);
        uf_.add_var(val_type->var_id, level);
        if (kvce->collection) {
            auto* col_type = infer(kvce->collection, env, level);
            auto* expected = arena_.make_app("Dict", {key_type, val_type});
            unifier_.unify(col_type, expected, ce->source_context,
                           "in dict generator collection");
        }
        if (auto* id = std::get_if<IdentifierExpr*>(&kvce->keyExpr))
            env->bind((*id)->name->value, key_type);
        if (auto* id = std::get_if<IdentifierExpr*>(&kvce->valueExpr))
            env->bind((*id)->name->value, val_type);
        if (kvce->condition)
            infer(kvce->condition, env, level);
    }
}

// ===== Cons =====

MonoTypePtr TypeChecker::infer_cons(ConsLeftExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    auto* elem_type = infer(node->left, env, level);
    auto* seq_type = infer(node->right, env, level);
    auto* expected_seq = arena_.make_app("Seq", {elem_type});
    unifier_.unify(seq_type, expected_seq, node->source_context, "in cons (::) operator");
    return unifier_.resolve(expected_seq);
}

// ===== ADT Registration =====

void TypeChecker::register_adt(const std::string& type_name,
                                const std::vector<std::string>& type_params,
                                const std::vector<std::pair<std::string, int>>& constructors) {
    for (auto& [ctor_name, arity] : constructors) {
        constructor_registry_[ctor_name] = {type_name, arity, type_params};

        // Register constructor as a function in root env:
        // For arity 0: constructor is a value of type ADT
        // For arity N: constructor is a function a1 -> ... -> aN -> ADT(params...)
        std::vector<TypeId> quant_vars;
        std::vector<MonoTypePtr> param_vars;
        for (auto& tp : type_params) {
            auto* v = arena_.fresh_var(0);
            uf_.add_var(v->var_id, 0);
            quant_vars.push_back(v->var_id);
            param_vars.push_back(v);
        }

        MonoTypePtr result_type = param_vars.empty()
            ? arena_.make_app(type_name, {})
            : arena_.make_app(type_name, param_vars);

        if (arity == 0) {
            root_env_->bind_scheme(ctor_name, TypeScheme(quant_vars, result_type));
        } else {
            // Build curried function: a -> b -> ... -> ADT(params)
            // Each constructor arg gets a fresh var (polymorphic)
            MonoTypePtr fn_type = result_type;
            for (int i = arity - 1; i >= 0; i--) {
                MonoTypePtr arg_type;
                if (i < (int)param_vars.size())
                    arg_type = param_vars[i];
                else {
                    arg_type = arena_.fresh_var(0);
                    uf_.add_var(arg_type->var_id, 0);
                    quant_vars.push_back(arg_type->var_id);
                }
                fn_type = arena_.make_arrow(arg_type, fn_type);
            }
            root_env_->bind_scheme(ctor_name, TypeScheme(quant_vars, fn_type));
        }
    }
}

// ===== Trait Registration =====

void TypeChecker::register_trait_method(const std::string& trait_name,
                                         const std::string& method_name,
                                         MonoTypePtr method_type) {
    std::vector<TypeId> qvars;
    collect_free_vars(method_type, -1, qvars);

    std::vector<Constraint> constraints;
    // Add trait constraint on the first free var (the type param)
    if (!qvars.empty()) {
        // Find the first var that appears as a param type
        auto* resolved = unifier_.resolve(method_type);
        MonoTypePtr constrained = nullptr;
        if (resolved && resolved->tag == MonoType::Arrow)
            constrained = unifier_.resolve(resolved->param_type);
        if (!constrained) {
            auto* v = arena_.fresh_var(0);
            uf_.add_var(v->var_id, 0);
            constrained = v;
            qvars.push_back(v->var_id);
        }
        constraints.push_back({trait_name, constrained});
    }

    root_env_->bind_scheme(method_name, TypeScheme(qvars, constraints, method_type));
}

void TypeChecker::register_instance(const std::string& trait_name, const std::string& type_name) {
    trait_instances_[trait_name].push_back(type_name);
}

bool TypeChecker::solve_constraints() {
    bool all_ok = true;
    for (auto& dc : deferred_constraints_) {
        auto* resolved = zonk(dc.type);
        if (resolved->tag == MonoType::Var) continue; // unsolved var

        std::string type_name = pretty_print(resolved);

        auto it = trait_instances_.find(dc.trait_name);
        if (it == trait_instances_.end()) {
            diag_.error(dc.loc, ErrorCode::E0106, "no instances for trait '" + dc.trait_name + "'");
            all_ok = false;
            continue;
        }
        bool found = false;
        for (auto& inst : it->second)
            if (inst == type_name) { found = true; break; }
        if (!found) {
            diag_.error(dc.loc, ErrorCode::E0105, "no instance for '" + dc.trait_name + " " + type_name + "'");
            all_ok = false;
        }
    }
    return all_ok;
}

// ===== Effect Registration =====

void TypeChecker::register_effect(const std::string& effect_name, const std::string& type_param,
                                   const std::vector<std::tuple<std::string, std::vector<MonoTypePtr>, MonoTypePtr>>& operations) {
    (void)type_param; // type param is used for documentation; operations already carry concrete types
    for (auto& [op_name, param_types, return_type] : operations) {
        std::string key = effect_name + "." + op_name;
        effect_ops_[key] = {effect_name, param_types, return_type};
    }
}

// ===== Perform =====

MonoTypePtr TypeChecker::infer_perform(PerformExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    std::string op_key = node->effect_name + "." + node->operation_name;

    // Check handler scope — is this perform inside a matching handle?
    bool handled = false;
    for (auto it = handler_scope_stack_.rbegin(); it != handler_scope_stack_.rend(); ++it) {
        for (auto& handled_op : *it) {
            if (handled_op == op_key) { handled = true; break; }
        }
        if (handled) break;
    }
    if (!handled) {
        diag_.warning(node->source_context,
                      "effect operation '" + op_key + "' may not be handled; "
                      "ensure a 'handle...with' block provides a handler for " + node->effect_name,
                      WarningFlag::UnhandledEffect);
    }

    // Look up the operation's type signature
    auto it = effect_ops_.find(op_key);
    if (it != effect_ops_.end()) {
        auto& info = it->second;
        // Check argument count
        // Filter out unit args from the perform call (matching codegen behavior)
        size_t expected = info.param_types.size();
        size_t actual = node->args.size();
        if (actual != expected) {
            diag_.error(node->source_context, ErrorCode::E0201,
                        "effect operation '" + op_key + "' expects " +
                        std::to_string(expected) + " argument(s), got " + std::to_string(actual));
            error_count_++;
        }
        // Type-check arguments
        for (size_t i = 0; i < node->args.size() && i < info.param_types.size(); i++) {
            auto* arg_type = infer(node->args[i], env, level);
            unifier_.unify(arg_type, info.param_types[i], node->source_context,
                           "in argument " + std::to_string(i + 1) + " of perform " + op_key);
        }
        return info.return_type;
    }

    // Unknown effect — infer args, return fresh var
    for (auto* arg : node->args)
        infer(arg, env, level);

    auto* v = arena_.fresh_var(level);
    uf_.add_var(v->var_id, level);
    return v;
}

// ===== Handle =====

MonoTypePtr TypeChecker::infer_handle(HandleExpr* node, std::shared_ptr<TypeEnv> env, int level) {
    // Collect which operations this handle block covers
    std::vector<std::string> handled_ops;
    for (auto* clause : node->clauses) {
        if (!clause->is_return_clause) {
            handled_ops.push_back(clause->effect_name + "." + clause->operation_name);
        }
    }

    // Push handler scope, then infer body
    handler_scope_stack_.push_back(handled_ops);
    auto* body_type = infer(node->body, env, level);
    handler_scope_stack_.pop_back();

    // The result type of the whole handle expression.
    // If there's a return clause, it transforms the body result — so result_type
    // may differ from body_type. Start with a fresh var.
    auto* result_type = arena_.fresh_var(level);
    uf_.add_var(result_type->var_id, level);

    // Check for a return clause first to establish the result type
    bool has_return = false;
    for (auto* clause : node->clauses) {
        if (clause->is_return_clause) {
            has_return = true;
            auto clause_env = env->child();
            clause_env->bind(clause->return_binding, body_type);
            auto* clause_type = infer(clause->body, clause_env, level);
            unifier_.unify(result_type, clause_type, clause->source_context,
                           "in return handler clause");
            break;
        }
    }
    // No return clause → result is body type
    if (!has_return)
        unifier_.unify(result_type, body_type, node->body->source_context, "in handle body");

    result_type = unifier_.resolve(result_type);

    // Infer operation handler clauses
    for (auto* clause : node->clauses) {
        if (clause->is_return_clause) continue;

        auto clause_env = env->child();
        std::string op_key = clause->effect_name + "." + clause->operation_name;

        // Bind operation argument names
        auto op_it = effect_ops_.find(op_key);
        for (size_t i = 0; i < clause->arg_names.size(); i++) {
            MonoTypePtr arg_type;
            if (op_it != effect_ops_.end() && i < op_it->second.param_types.size())
                arg_type = op_it->second.param_types[i];
            else {
                arg_type = arena_.fresh_var(level);
                uf_.add_var(arg_type->var_id, level);
            }
            clause_env->bind(clause->arg_names[i], arg_type);
        }

        // Bind resume: function from op's return type to result type
        if (!clause->resume_name.empty()) {
            MonoTypePtr resume_param;
            if (op_it != effect_ops_.end())
                resume_param = op_it->second.return_type;
            else {
                resume_param = arena_.fresh_var(level);
                uf_.add_var(resume_param->var_id, level);
            }
            auto* resume_type = arena_.make_arrow(resume_param, result_type);
            clause_env->bind(clause->resume_name, resume_type);
        }

        auto* clause_type = infer(clause->body, clause_env, level);
        unifier_.unify(result_type, clause_type, clause->source_context,
                       "in handler clause for " + op_key);
        result_type = unifier_.resolve(result_type);
    }

    return result_type;
}

} // namespace yona::compiler::typechecker
