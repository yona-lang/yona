#ifndef YONA_TYPECHECKER_TYPE_CHECKER_H
#define YONA_TYPECHECKER_TYPE_CHECKER_H

/// Hindley-Milner type checker for Yona.
///
/// Walks the AST, infers types for every expression, and stores
/// the result in a type map (AST node → MonoType). The codegen
/// can then query this map instead of guessing types.
///
/// Usage:
///   TypeChecker checker(diag);
///   checker.check(root_node);
///   auto* ty = checker.type_of(some_node);

#include "InferType.h"
#include "TypeArena.h"
#include "UnionFind.h"
#include "Unification.h"
#include "TypeEnv.h"
#include "Diagnostic.h"
#include "ast.h"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yona::compiler::typechecker {

/// `.yonai` FN shape for the type checker. CType/ABI stays on the codegen
/// side; this overlay is how `Linear` (and products of Linear) cross modules.
struct ImportedFnSig {
    int arity = 0;
    bool return_linear = false;           ///< whole return is `Linear _`
    std::vector<char> tuple_elem_linear;  ///< per-element Linear on a tuple return
    std::vector<char> param_linear;       ///< per-parameter Linear (same length as arity, or empty)
    /// Latent `Effect.op` labels from `.yonai` `effects …` (sorted unique).
    std::vector<std::string> effect_labels;
    /// True when the result arrow is an open row (`|rN` in `.yonai`).
    bool effect_open = false;
    /// Per-parameter effect rows (index = param slot). Missing = not a function.
    std::vector<std::optional<SerializedEffectRow>> param_effect_rows;
    /// Full parsed `effects` trailer (result + params, shared rest ids).
    SerializedFnEffects effect_spec;
};

/// Loads imported function signatures from `.yonai` (implemented by Codegen).
class ImportTypeSource {
public:
    virtual ~ImportTypeSource() = default;
    virtual std::optional<ImportedFnSig> imported_function_sig(
        const std::string& module_fqn, const std::string& name) = 0;
    virtual std::vector<std::string> imported_module_exports(
        const std::string& module_fqn) = 0;
};

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticEngine& diag);

    /// When set, `ImportExpr` / FQN / wildcard bind `.yonai` types instead of
    /// unconstrained fresh variables.
    void set_import_type_source(ImportTypeSource* src) { import_src_ = src; }

    /// Type-check a top-level expression. Returns inferred type (nullptr on error).
    MonoTypePtr check(ast::AstNode* node);

    /// Retrieve the type assigned to an AST node after checking.
    MonoTypePtr type_of(ast::AstNode* node) const;

    /// Resolve all union-find links in a type (zonk).
    MonoTypePtr zonk(MonoTypePtr type);

    /// Has errors? Includes both direct type checker errors and unifier errors.
    bool has_errors() const { return error_count_ > 0 || diag_.has_errors(); }

    /// Has direct type checker errors only? (undefined vars, missing traits)
    /// Does not include unifier errors from partial inference.
    bool has_direct_errors() const { return error_count_ > 0; }

    /// Access arena (for tests).
    TypeArena& arena() { return arena_; }

    /// Register ADT definitions for constructor type inference.
    void register_adt(const std::string& type_name, const std::vector<std::string>& type_params,
                       const std::vector<std::pair<std::string, int>>& constructors);

    /// Register a trait method (binds as polymorphic with constraint).
    void register_trait_method(const std::string& trait_name, const std::string& method_name,
                                MonoTypePtr method_type);

    /// Register a trait instance for a concrete type.
    void register_instance(const std::string& trait_name, const std::string& type_name);

    /// Register an effect declaration with its operations.
    /// Each operation is (name, param_types, return_type).
    void register_effect(const std::string& effect_name, const std::string& type_param,
                          const std::vector<std::tuple<std::string, std::vector<MonoTypePtr>, MonoTypePtr>>& operations);

    /// Concrete escaping `Effect.op` labels from the last `check` (top-level ambient).
    const std::vector<std::string>& last_escaping_effects() const { return top_escaping_.labels; }

    /// Solve deferred trait constraints. Returns false on unsatisfied constraints.
    bool solve_constraints();

private:
    struct EscapingEffects {
        std::vector<std::string> labels;
        std::unordered_map<std::string, SourceLocation> origins;
        void add(const std::string& label, const SourceLocation& loc) {
            if (std::find(labels.begin(), labels.end(), label) != labels.end()) return;
            labels.push_back(label);
            std::sort(labels.begin(), labels.end());
            origins.emplace(label, loc);
        }
        void add_all(const std::vector<std::string>& ls, const SourceLocation& loc) {
            for (auto& l : ls) add(l, loc);
        }
        void subtract(const std::vector<std::string>& handled) {
            std::unordered_set<std::string> h(handled.begin(), handled.end());
            std::vector<std::string> kept;
            for (auto& l : labels) {
                if (!h.count(l)) kept.push_back(l);
                else origins.erase(l);
            }
            labels = std::move(kept);
        }
        /// Open effect-row tails from applied function parameters (HOF).
        std::vector<MonoTypePtr> open_rests;
        void add_rest(MonoTypePtr rest) {
            if (rest) open_rests.push_back(rest);
        }
    };
    /// Main recursive inference. Returns inferred monotype.
    MonoTypePtr infer(ast::AstNode* node, std::shared_ptr<TypeEnv> env, int level);

    // --- Inference for specific node types ---
    MonoTypePtr infer_integer(ast::AstNode* node);
    MonoTypePtr infer_float(ast::AstNode* node);
    MonoTypePtr infer_string(ast::AstNode* node);
    MonoTypePtr infer_bool(ast::AstNode* node);
    MonoTypePtr infer_symbol(ast::AstNode* node);
    MonoTypePtr infer_identifier(ast::IdentifierExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_let(ast::LetExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_function(ast::FunctionExpr* node, std::shared_ptr<TypeEnv> env, int level);
    void check_param_borrow_annotations(ast::FunctionExpr* node);
    MonoTypePtr infer_apply(ast::ApplyExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_if(ast::IfExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_binary(ast::BinaryOpExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_tuple(ast::TupleExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_seq(ast::ValuesSequenceExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_do(ast::DoExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_case(ast::CaseExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_cons(ast::ConsLeftExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_perform(ast::PerformExpr* node, std::shared_ptr<TypeEnv> env, int level);
    MonoTypePtr infer_handle(ast::HandleExpr* node, std::shared_ptr<TypeEnv> env, int level);

    /// Infer the type a pattern matches, binding variables in env.
    MonoTypePtr infer_pattern(ast::PatternNode* pat, std::shared_ptr<TypeEnv> env, int level);

    /// Bind iteration variables from a collection extractor into env.
    void bind_collection_extractor(ast::CollectionExtractorExpr* ce,
                                    std::shared_ptr<TypeEnv> env, int level);

    /// Join latent effects of an applied arrow into the ambient escaping row.
    void join_arrow_effects(MonoTypePtr arrow, const SourceLocation& loc);

    /// After inferring a recursive function, close open rests that came only
    /// from self-application (not from a function parameter).
    void close_recursive_self_rests(MonoTypePtr fn_type);

    /// True if `Effect.op` is covered by the current handler stack.
    bool is_effect_handled(const std::string& op_key) const;

    /// Reject ambient/callee effects not covered by handlers (E0202).
    void check_effects_covered(const std::vector<std::string>& labels,
                               const SourceLocation& call_loc,
                               const std::unordered_map<std::string, SourceLocation>* origins);

    // --- Generalization / Instantiation ---

    /// Generalize a type at the given level: free vars with level > given become quantified.
    TypeScheme generalize(MonoTypePtr type, int level);

    /// Instantiate a polymorphic scheme with fresh variables at the given level.
    MonoTypePtr instantiate(const TypeScheme& scheme, int level);

    /// Collect free type variables with level > given level.
    void collect_free_vars(MonoTypePtr type, int level, std::vector<TypeId>& vars);

    /// Substitute type variables according to a mapping.
    MonoTypePtr substitute(MonoTypePtr type, const std::unordered_map<TypeId, MonoTypePtr>& subst);

    // --- Helpers ---

    /// Record the inferred type for an AST node.
    void record(ast::AstNode* node, MonoTypePtr type);

    /// Map operator AST type to operator name string for env lookup.
    static std::string op_name(ast::AstNodeType type);

    /// Build a curried function type from a `.yonai` import signature.
    MonoTypePtr mono_from_import_sig(const ImportedFnSig& sig, int level);

    /// Convert a parsed Yona type annotation (extern decls) to a MonoType.
    MonoTypePtr from_ast_type(const yona::compiler::types::Type& t, int level);

    /// Bind an imported name from `.yonai`, or a fresh var if unknown.
    void bind_import_name(std::shared_ptr<TypeEnv> env, const std::string& module_fqn,
                          const std::string& func_name, const std::string& bind_name,
                          int level);

    ImportTypeSource* import_src_ = nullptr;

    TypeArena arena_;
    UnionFind uf_;
    Unifier unifier_;
    DiagnosticEngine& diag_;
    int error_count_ = 0;

    /// Root environment with builtins.
    std::shared_ptr<TypeEnv> root_env_;

    /// ADT constructor registry: constructor name → (ADT name, arity, type param names)
    struct ConstructorInfo {
        std::string adt_name;
        int arity;
        std::vector<std::string> type_params; ///< from the ADT definition
    };
    std::unordered_map<std::string, ConstructorInfo> constructor_registry_;

    /// Type map: AST node → inferred monotype.
    std::unordered_map<ast::AstNode*, MonoTypePtr> type_map_;

    /// Trait instance registry: "TraitName" → set of concrete type names with instances.
    std::unordered_map<std::string, std::vector<std::string>> trait_instances_;

    /// Deferred trait constraints gathered during inference.
    struct DeferredConstraint {
        std::string trait_name;
        MonoTypePtr type;
        SourceLocation loc;
        std::string context;
    };
    std::vector<DeferredConstraint> deferred_constraints_;

    /// Effect operation registry: "Effect.op" → (param_types, return_type)
    struct EffectOpInfo {
        std::string effect_name;
        std::vector<MonoTypePtr> param_types;
        MonoTypePtr return_type;
    };
    std::unordered_map<std::string, EffectOpInfo> effect_ops_;

    /// Handler scope stack: each entry lists the effect operations handled at that level.
    std::vector<std::vector<std::string>> handler_scope_stack_;

    /// Ambient escaping effects for the expression currently being inferred.
    EscapingEffects* ambient_effects_ = nullptr;
    EscapingEffects top_escaping_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_CHECKER_H
