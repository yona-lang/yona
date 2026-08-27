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

#include "Diagnostic.h"
#include "InferType.h"
#include "TypeArena.h"
#include "TypeEnv.h"
#include "Unification.h"
#include "UnionFind.h"
#include "ast.h"
#include "types.h"
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yona::compiler::typechecker {

/// `.yonai` FN overlay for the type checker. CType/ABI stays on the codegen
/// side; this carries `Linear` plus structural tags (`SEQ` vs `ADT`) so a
/// Seq is not accepted where a Stream (or other ADT) is required.
struct ImportedFnSig {
    int arity = 0;
    bool return_linear = false;
    std::vector<char> tuple_elem_linear;
    std::vector<char> param_linear;
    /// Structural `.yonai` tags (`SEQ`, `ADT`, `FUNCTION`, …). `INT`/empty
    /// may be unconstrained because legacy generic ABIs snapshot variables as
    /// INT; the other scalar tags are exact.
    std::vector<std::string> param_tags;
    std::string return_tag;
    /// Recursive `.yonai` descriptors (`ADT(FileHandle)`, `TUPLE(...)`, …).
    std::vector<std::string> param_descriptors;
    std::string return_descriptor;
    /// Named payload for a `LINEAR` return, carried by `.yonai` `retadt`.
    std::string return_linear_adt_name;
    /// Versioned normalized effect graph for every arrow in the imported
    /// source type. Empty means a legacy interface row.
    std::string effect_scheme;
};

/// Complete imported instance head. `type_names` are the concrete head
/// constructors in declaration order; `type_params` and `constraints`
/// describe a generic lifted instance such as `Eq a => Eq (Seq a)`.
struct ImportedInstanceSig {
    std::string trait_name;
    std::vector<std::string> type_names;
    std::vector<std::string> type_params;
    std::vector<std::pair<std::string, std::string>> constraints;
};

/// Loads imported function signatures from `.yonai` (implemented by Codegen).
class ImportTypeSource {
public:
    virtual ~ImportTypeSource() = default;
    virtual std::optional<ImportedFnSig> imported_function_sig(
        const std::string& module_fqn, const std::string& name) = 0;
    virtual std::vector<std::string> imported_module_exports(
        const std::string& module_fqn) = 0;
    virtual std::vector<ImportedInstanceSig> imported_instances(
        const std::string& module_fqn) = 0;
};

class TypeChecker {
public:
    explicit TypeChecker(DiagnosticEngine& diag);

    /// When set, `ImportExpr` / wildcard bind `.yonai` Linear overlays.
    void set_import_type_source(ImportTypeSource* src) { import_src_ = src; }

    /// Type-check a top-level expression. Returns inferred type (nullptr on error).
    MonoTypePtr check(ast::AstNode* node);

    /// Retrieve the type assigned to an AST node after checking.
    MonoTypePtr type_of(ast::AstNode* node) const;

    /// Resolve all union-find links in a type (zonk).
    MonoTypePtr zonk(MonoTypePtr type);

    struct SelectedTraitInstance {
        std::string trait_name;
        std::vector<std::string> type_names;
    };
    std::optional<SelectedTraitInstance> selected_trait_instance(
        const ast::ApplyExpr* application) const;

    /// Has errors? Includes both direct type checker errors and unifier errors.
    bool has_errors() const { return error_count_ > 0 || diag_.has_errors(); }

    /// Has direct type checker errors only? (undefined vars, missing traits)
    /// Does not include unifier errors from partial inference.
    bool has_direct_errors() const { return error_count_ > 0; }

    /// Access arena (for tests).
    TypeArena& arena() { return arena_; }

    /// Register ADT definitions for constructor type inference.
    void register_adt(const std::string& type_name, const std::vector<std::string>& type_params,
                      const std::vector<std::pair<std::string, int>>& constructors,
                      const std::vector<std::vector<ast::FieldType>>& field_types = {},
                      const std::vector<std::vector<std::string>>& field_names = {});

    /// Register a trait method (binds as polymorphic with constraint).
    void register_trait(const std::string& trait_name,
                        std::vector<std::string> type_params);
    void register_trait_method(const std::string& trait_name, const std::string& method_name,
                                MonoTypePtr method_type);
    void register_trait_method_descriptor(const std::string& trait_name,
                                          const std::string& method_name,
                                          const std::string& descriptor);
    /// Register a polymorphic compiler/Prelude function without inventing a
    /// trait obligation. This is distinct from `register_trait_method`:
    /// ordinary imported functions are not members of a synthetic trait.
    void register_builtin_function(const std::string& function_name,
                                   MonoTypePtr function_type);
    void register_trait_superclass(const std::string& trait_name,
                                   const std::string& superclass_name);

    /// Register a trait instance for a concrete type.
    void register_instance(const std::string& trait_name, const std::string& type_name,
                           std::vector<std::string> type_params = {},
                           std::vector<std::pair<std::string, std::string>> constraints = {},
                           std::vector<std::string> type_names = {});

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

    /// True only for a known, closed empty effect row. This is the semantic
    /// fact consumed by --require-effect-free; a missing interface row is not
    /// treated as pure.
    bool is_effect_free(MonoTypePtr type);

    /// Versioned, deterministic normal-form graph for all arrows in a type.
    /// Used exclusively by `.yonai`; ordinary diagnostics consume summaries.
    std::string serialize_effect_scheme(MonoTypePtr type);
    /// Overlay a decoded `.yonai` effect scheme on a structural function type.
    /// Legacy or malformed input is handled conservatively by the importer.
    MonoTypePtr apply_effect_scheme(MonoTypePtr type, std::string_view encoded);

    /// Locations of direct top-level `perform`s with no covering handler.
    /// Kept independently of warning configuration for strict tooling modes.
    const std::vector<SourceLocation>& unhandled_effect_locations() const {
        return unhandled_effect_locations_;
    }

    void set_require_effect_free(bool value) { require_effect_free_ = value; }
    bool has_unknown_effect_rows() const { return has_unknown_effect_rows_; }

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
    MonoTypePtr substitute(
        MonoTypePtr type, const std::unordered_map<TypeId, MonoTypePtr>& subst,
        const std::vector<std::pair<EffectRef, EffectRef>>& effect_subst = {});

    // --- Helpers ---

    /// Return the authoritative effect expression on an Arrow/legacy row.
    std::optional<EffectRef> callee_effect(MonoTypePtr callee);

    /// Union uncovered callee effects into the enclosing lambda, or E0202 at top level.
    void apply_callee_effects(MonoTypePtr callee, const SourceLocation& apply_loc);

    void include_ambient_effect(EffectRef effect,
                                const SourceLocation& loc,
                                const std::string& context);
    void collect_effect_roots(MonoTypePtr type,
                              std::vector<EffectRef>& roots);

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

    /// ADT constructor registry, including declared field shapes when known.
    struct ConstructorInfo {
        std::string adt_name;
        int arity;
        std::vector<std::string> type_params; ///< from the ADT definition
        std::vector<ast::FieldType> field_types;
        std::vector<std::string> field_names;
    };
    std::unordered_map<std::string, ConstructorInfo> constructor_registry_;

    /// Type map: AST node → inferred monotype.
    std::unordered_map<ast::AstNode*, MonoTypePtr> type_map_;

    struct InstanceContract {
        std::string type_name;
        std::vector<std::string> type_names;
        std::vector<std::string> type_params;
        std::vector<std::pair<std::string, std::string>> constraints;
    };
    /// Trait name → deterministic set of complete visible instance heads.
    std::unordered_map<std::string, std::vector<InstanceContract>> trait_instances_;
    std::unordered_map<std::string, std::vector<std::string>> trait_superclasses_;
    std::unordered_map<std::string, std::vector<std::string>> trait_type_params_;
    std::unordered_map<std::string, std::vector<std::string>> adt_type_params_;

    /// Deferred trait constraints gathered during inference.
    struct DeferredConstraint {
        std::string trait_name;
        std::vector<MonoTypePtr> types;
        SourceLocation loc;
        std::string context;
        const ast::ApplyExpr* origin = nullptr;

        DeferredConstraint(std::string name, MonoTypePtr type,
                           SourceLocation source, std::string detail)
            : trait_name(std::move(name)), types{type}, loc(std::move(source)),
              context(std::move(detail)) {}
        DeferredConstraint(std::string name, std::vector<MonoTypePtr> arguments,
                           SourceLocation source, std::string detail)
            : trait_name(std::move(name)), types(std::move(arguments)),
              loc(std::move(source)), context(std::move(detail)) {}
    };
    std::vector<DeferredConstraint> deferred_constraints_;
    std::unordered_map<const ast::ApplyExpr*, SelectedTraitInstance>
        selected_trait_instances_;

    enum class ConcurrencyBoundary { TaskSpawn, ChannelSend };
    std::unordered_map<std::string, ConcurrencyBoundary> concurrency_boundaries_;
    struct CaptureFrame {
        const TypeEnv* local_root = nullptr;
        std::vector<MonoTypePtr> types;
        std::unordered_set<std::string> names;
    };
    std::vector<CaptureFrame> capture_frames_;
    std::unordered_map<const ast::FunctionExpr*, std::vector<MonoTypePtr>>
        function_capture_types_;
    std::unordered_map<std::string, std::vector<MonoTypePtr>>
        named_function_capture_types_;

    void require_trait(const std::string& trait_name, MonoTypePtr type,
                       const SourceLocation& loc, std::string context);
    void require_captures_shareable(const std::vector<MonoTypePtr>& captures,
                                    const SourceLocation& loc,
                                    const std::string& context);
    void enforce_concurrency_boundary(ast::ApplyExpr* node,
                                      const std::string& callee_name,
                                      std::optional<ConcurrencyBoundary> boundary);

    /// Effect operation registry: "Effect.op" → (param_types, return_type)
    struct EffectOpInfo {
        std::string effect_name;
        std::vector<MonoTypePtr> param_types;
        MonoTypePtr return_type;
    };
    std::unordered_map<std::string, EffectOpInfo> effect_ops_;

    /// Derived cells collecting effects while inferring function and handler
    /// bodies. A derived cell is the only locally-defaultable effect node.
    struct CollectedRow {
        EffectRef effect;
    };
    std::vector<CollectedRow> latent_effect_stack_;
    std::unordered_map<std::string, SourceLocation> effect_origins_;
    std::vector<SourceLocation> unhandled_effect_locations_;
    bool require_effect_free_ = false;
    bool has_unknown_effect_rows_ = false;

    /// While checking a recursive binding, self calls are equations already
    /// represented by its derived body cell.  Recording them again through a
    /// provisional flexible arrow would create `D includes F; F = D`, leaving
    /// an artificial open projection instead of the least fixed point.
    struct RecursiveSelfContext {
        MonoTypePtr preliminary = nullptr;
        std::vector<MonoTypePtr> continuations;
    };
    std::vector<RecursiveSelfContext> recursive_self_contexts_;

    /// Recursive SCC predeclarations are value variables, but their latent
    /// effects are known derived cells immediately.  This prevents a call to
    /// a sibling inferred later in the SCC from inventing a flexible proxy
    /// that would survive as an artificial open row.
    std::unordered_map<const ast::FunctionExpr*, EffectRef>
        predeclared_function_body_effects_;

    std::vector<std::string> module_paths_;
    ImportTypeSource* import_src_ = nullptr;

    void bind_import_name(std::shared_ptr<TypeEnv> env, const std::string& module_fqn,
                          const std::string& func_name, const std::string& bind_name,
                          int level);
    MonoTypePtr mono_from_import_sig(
        const ImportedFnSig& sig, int level,
        std::unordered_map<std::string, MonoTypePtr>* descriptor_variables = nullptr);
    MonoTypePtr from_ast_type(const yona::compiler::types::Type& t, int level);
    MonoTypePtr from_ast_type_impl(
        const yona::compiler::types::Type& t, int level,
        std::unordered_map<std::string, MonoTypePtr>& variables);
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_CHECKER_H
