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
#include <unordered_map>
#include <vector>

namespace yona::compiler::typechecker {

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticEngine& diag);

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

    /// Solve deferred trait constraints. Returns false on unsatisfied constraints.
    bool solve_constraints();

    /// Search path for `.yonai` FN effect rows on `import` (same dirs as codegen).
    void add_module_path(std::string path);

    /// Type-check a module as a unit so sibling functions see each other.
    /// Does not fail the caller — inspect `has_direct_errors()` if needed.
    void check_module(ast::ModuleDecl* mod);

    /// Closed latent op keys on a function type (`Fs.read`). Empty if none.
    std::vector<std::string> closed_effect_ops(MonoTypePtr type);

    /// Closed ops plus whether the row is open and the first param is an arrow (HOF).
    struct EffectRowInfo {
        std::vector<std::string> ops;
        bool open_rest = false;
        bool hof = false;
    };
    EffectRowInfo effect_row_info(MonoTypePtr type);

private:
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

    /// Collect known labels and open rest from an Arrow / ERow (chasing rest).
    void flatten_callee_effects(MonoTypePtr callee, std::vector<LatentEffect>& known,
                                MonoTypePtr& rest);

    /// Union uncovered callee effects into the enclosing lambda, or E0202 at top level.
    void apply_callee_effects(MonoTypePtr callee, const SourceLocation& apply_loc);

    bool is_effect_handled(const std::string& op_key) const;

    /// Record the inferred type for an AST node.
    void record(ast::AstNode* node, MonoTypePtr type);

    /// Map operator AST type to operator name string for env lookup.
    static std::string op_name(ast::AstNodeType type);

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

    /// Collectors for latent effects while inferring function bodies.
    struct CollectedRow {
        std::vector<LatentEffect> known;
        MonoTypePtr rest = nullptr;
    };
    std::vector<CollectedRow> latent_effect_stack_;

    std::vector<std::string> module_paths_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_CHECKER_H
