#ifndef YONA_SEMANTICS_TYPECHECKER_H
#define YONA_SEMANTICS_TYPECHECKER_H
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

#include "yona/Model/InferType.h"
#include "yona/Model/TypeArena.h"
#include "yona/Model/TypeEnv.h"
#include "yona/Model/Types.h"
#include "yona/Semantics/Unification.h"
#include "yona/Semantics/UnionFind.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Ast.h"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yona::interface {
struct Function;
}

namespace yona::compiler::typechecker {

/// `.yonai` FN overlay for the type checker. CType/ABI stays on the codegen
/// side; this carries `Linear` plus structural tags (`SEQ` vs `ADT`) so a
/// Seq is not accepted where a Stream (or other ADT) is required.
struct ImportedFnSig {
  /// Canonical `.yonai` descriptors (`VAR(a)`, `ADT(FileHandle)`,
  /// `TUPLE(...)`, and so on). They are mandatory and structural.
  std::vector<std::string> param_descriptors;
  std::string return_descriptor;
  /// Canonical normalized effect graph for every arrow in the imported
  /// source type. Empty means the row has only its normalized summary.
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

/// Supplies canonical imported function and instance contracts to semantics.
///
/// Callback results are owned values. Implementations retain no caller-owned
/// arguments beyond a call unless their own contract says otherwise, and must
/// document any failure or synchronization policy not represented here.
class ImportTypeSource {
public:
  virtual ~ImportTypeSource() = default;
  virtual std::optional<ImportedFnSig>
  imported_function_sig(const std::string &module_fqn,
                        const std::string &name) = 0;
  virtual std::vector<std::string>
  imported_module_exports(const std::string &module_fqn) = 0;
  virtual std::vector<ImportedInstanceSig>
  imported_instances(const std::string &module_fqn) = 0;
};

/// Stateful type and effect inference session.
///
/// The checker borrows its DiagnosticEngine for its whole lifetime and owns
/// all inferred MonoType nodes. AST pointers and an installed ImportTypeSource
/// are never owned: ASTs must remain alive while node-indexed facts are
/// queried, and the import source must outlive every import-dependent
/// operation. Returned MonoType pointers remain valid until checker
/// destruction.
///
/// Checking and registration report semantic failures through diagnostics or
/// documented result values; malformed structural descriptors may throw
/// std::invalid_argument. Every operation on one checker, including queries
/// that zonk or expose mutable arena state, must be externally serialized.
class TypeChecker {
public:
  /// Retain a reference to diag, which must outlive this checker.
  explicit TypeChecker(DiagnosticEngine &diag);

  /// When set, `ImportExpr` / wildcard bind `.yonai` Linear overlays. src is
  /// borrowed until replaced or reset with null.
  void set_import_type_source(ImportTypeSource *src) { import_src_ = src; }

  /// Type-check a borrowed top-level expression. Returns an arena-owned type,
  /// or nullptr on error. The AST must be immutable during the call and remain
  /// alive while its recorded facts are queried.
  MonoTypePtr check(ast::AstNode *node);

  /// Retrieve a borrowed arena-owned type assigned after checking, or nullptr
  /// when no type was recorded. node is used only as a non-owning identity.
  MonoTypePtr type_of(ast::AstNode *node) const;

  /// Resolve all union-find links in an arena-backed type. The result is
  /// arena-owned and the operation may update internal union-find state.
  MonoTypePtr zonk(MonoTypePtr type);

  struct SelectedTraitInstance {
    std::string trait_name;
    std::vector<std::string> type_names;
  };
  std::optional<SelectedTraitInstance>
  selected_trait_instance(const ast::ApplyExpr *application) const;

  /// Has errors? Includes both direct type checker errors and unifier errors.
  bool has_errors() const { return error_count_ > 0 || diag_.has_errors(); }

  /// Has direct type checker errors only? (undefined vars, missing traits)
  /// Does not include unifier errors from partial inference.
  bool has_direct_errors() const { return error_count_ > 0; }

  /// Borrow the checker-owned arena. The reference is invalidated by checker
  /// destruction; direct mutation shares all checker synchronization rules.
  TypeArena &arena() { return arena_; }

  /// Register ADT definitions for constructor type inference.
  void
  register_adt(const std::string &type_name,
               const std::vector<std::string> &type_params,
               const std::vector<std::pair<std::string, int>> &constructors,
               const std::vector<std::vector<ast::FieldType>> &field_types,
               const std::vector<std::vector<std::string>> &field_names = {});

  /// Register a trait method (binds as polymorphic with constraint).
  void register_trait(const std::string &trait_name,
                      std::vector<std::string> type_params);
  void register_trait_method(const std::string &trait_name,
                             const std::string &method_name,
                             MonoTypePtr method_type);
  void register_trait_method_descriptor(const std::string &trait_name,
                                        const std::string &method_name,
                                        const std::string &descriptor);
  /// Register a polymorphic compiler/Prelude function without inventing a
  /// trait obligation. This is distinct from `register_trait_method`:
  /// ordinary imported functions are not members of a synthetic trait.
  void register_builtin_function(const std::string &function_name,
                                 MonoTypePtr function_type);

  /// Register one canonical interface function in the implicit root scope.
  /// The function contract is copied into an owned type scheme; no reference
  /// to Function is retained.
  void register_interface_function(const interface::Function &Function);
  void register_trait_superclass(const std::string &trait_name,
                                 const std::string &superclass_name);

  /// Register a trait instance for a concrete type.
  void register_instance(
      const std::string &trait_name, std::vector<std::string> type_names,
      std::vector<std::string> type_params = {},
      std::vector<std::pair<std::string, std::string>> constraints = {});

  /// Register an effect declaration with its operations.
  /// Each operation is (name, param_types, return_type).
  void register_effect(
      const std::string &effect_name, const std::string &type_param,
      const std::vector<std::tuple<std::string, std::vector<MonoTypePtr>,
                                   MonoTypePtr>> &operations);

  /// Solve deferred trait constraints. Returns false on unsatisfied
  /// constraints.
  bool solve_constraints();

  /// Search path for `.yonai` FN effect rows on `import` (same dirs as
  /// codegen).
  void add_module_path(std::string path);

  /// Type-check a module as a unit so sibling functions see each other.
  /// The module is borrowed and must remain immutable during the call. Failure
  /// is recorded in diagnostics; inspect has_direct_errors() when needed.
  void check_module(ast::ModuleDecl *mod);

  /// Closed latent op keys on a function type (`Fs.read`). Empty if none.
  std::vector<std::string> closed_effect_ops(MonoTypePtr type);

  /// Closed ops plus whether the row is open and the first param is an arrow
  /// (HOF).
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

  /// Deterministic normal-form graph for all arrows in a type.
  /// Used exclusively by `.yonai`; ordinary diagnostics consume summaries.
  std::string serialize_effect_scheme(MonoTypePtr type);
  /// Overlay a decoded `.yonai` effect scheme on a structural function type.
  /// Malformed input is rejected conservatively as an opaque effect graph.
  MonoTypePtr apply_effect_scheme(MonoTypePtr type, std::string_view encoded);

  /// Locations of direct top-level `perform`s with no covering handler.
  /// Kept independently of warning configuration for strict tooling modes.
  /// The container reference remains valid until checker destruction; later
  /// analysis may change its contents and invalidate iterators and element
  /// references.
  const std::vector<SourceRange> &unhandled_effect_locations() const {
    return unhandled_effect_locations_;
  }

  void set_require_effect_free(bool value) { require_effect_free_ = value; }
  bool has_unknown_effect_rows() const { return has_unknown_effect_rows_; }

private:
  /// Main recursive inference. Returns inferred monotype.
  MonoTypePtr infer(ast::AstNode *node, std::shared_ptr<TypeEnv> env,
                    int level);

  // --- Inference for specific node types ---
  MonoTypePtr infer_integer(ast::AstNode *node);
  MonoTypePtr infer_float(ast::AstNode *node);
  MonoTypePtr infer_string(ast::AstNode *node);
  MonoTypePtr infer_bool(ast::AstNode *node);
  MonoTypePtr infer_symbol(ast::AstNode *node);
  MonoTypePtr infer_identifier(ast::IdentifierExpr *node,
                               std::shared_ptr<TypeEnv> env, int level);
  MonoTypePtr infer_let(ast::LetExpr *node, std::shared_ptr<TypeEnv> env,
                        int level);
  MonoTypePtr infer_function(ast::FunctionExpr *node,
                             std::shared_ptr<TypeEnv> env, int level);
  void check_param_borrow_annotations(ast::FunctionExpr *node);
  MonoTypePtr infer_apply(ast::ApplyExpr *node, std::shared_ptr<TypeEnv> env,
                          int level);
  MonoTypePtr infer_if(ast::IfExpr *node, std::shared_ptr<TypeEnv> env,
                       int level);
  MonoTypePtr infer_binary(ast::BinaryOpExpr *node,
                           std::shared_ptr<TypeEnv> env, int level);
  MonoTypePtr infer_tuple(ast::TupleExpr *node, std::shared_ptr<TypeEnv> env,
                          int level);
  MonoTypePtr infer_seq(ast::ValuesSequenceExpr *node,
                        std::shared_ptr<TypeEnv> env, int level);
  MonoTypePtr infer_do(ast::DoExpr *node, std::shared_ptr<TypeEnv> env,
                       int level);
  MonoTypePtr infer_case(ast::CaseExpr *node, std::shared_ptr<TypeEnv> env,
                         int level);
  MonoTypePtr infer_cons(ast::ConsLeftExpr *node, std::shared_ptr<TypeEnv> env,
                         int level);
  MonoTypePtr infer_perform(ast::PerformExpr *node,
                            std::shared_ptr<TypeEnv> env, int level);
  MonoTypePtr infer_handle(ast::HandleExpr *node, std::shared_ptr<TypeEnv> env,
                           int level);

  /// Infer the type a pattern matches, binding variables in env.
  MonoTypePtr infer_pattern(ast::PatternNode *pat, std::shared_ptr<TypeEnv> env,
                            int level);

  /// Bind iteration variables from a collection extractor into env.
  void bind_collection_extractor(ast::CollectionExtractorExpr *ce,
                                 std::shared_ptr<TypeEnv> env, int level);

  // --- Generalization / Instantiation ---

  /// Generalize a type at the given level: free vars with level > given become
  /// quantified.
  TypeScheme generalize(MonoTypePtr type, int level);

  /// Instantiate a polymorphic scheme with fresh variables at the given level.
  MonoTypePtr instantiate(const TypeScheme &scheme, int level);

  /// Collect free type variables with level > given level.
  void collect_free_vars(MonoTypePtr type, int level,
                         std::vector<TypeId> &vars);

  /// Substitute type variables according to a mapping.
  MonoTypePtr substitute(
      MonoTypePtr type, const std::unordered_map<TypeId, MonoTypePtr> &subst,
      const std::vector<std::pair<EffectRef, EffectRef>> &effect_subst = {});

  // --- Helpers ---

  /// Return the effect expression carried by an Arrow.
  std::optional<EffectRef> callee_effect(MonoTypePtr callee);

  /// Union uncovered callee effects into the enclosing lambda, or E0202 at top
  /// level.
  void apply_callee_effects(MonoTypePtr callee, const SourceRange &apply_loc);

  void include_ambient_effect(EffectRef effect, const SourceRange &loc,
                              const std::string &context);
  void collect_effect_roots(MonoTypePtr type, std::vector<EffectRef> &roots);

  /// Record the inferred type for an AST node.
  void record(ast::AstNode *node, MonoTypePtr type);

  /// Map operator AST type to operator name string for env lookup.
  static std::string op_name(ast::AstNodeType type);

  TypeArena arena_;
  UnionFind uf_;
  Unifier unifier_;
  DiagnosticEngine &diag_;
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
  std::unordered_map<ast::AstNode *, MonoTypePtr> type_map_;

  struct InstanceContract {
    std::vector<std::string> type_names;
    std::vector<std::string> type_params;
    std::vector<std::pair<std::string, std::string>> constraints;
  };
  /// Trait name → deterministic set of complete visible instance heads.
  std::unordered_map<std::string, std::vector<InstanceContract>>
      trait_instances_;
  std::unordered_map<std::string, std::vector<std::string>> trait_superclasses_;
  std::unordered_map<std::string, std::vector<std::string>> trait_type_params_;
  std::unordered_map<std::string, std::vector<std::string>> adt_type_params_;

  /// Deferred trait constraints gathered during inference.
  struct DeferredConstraint {
    std::string trait_name;
    std::vector<MonoTypePtr> types;
    SourceRange loc;
    std::string context;
    const ast::ApplyExpr *origin = nullptr;

    DeferredConstraint(std::string name, MonoTypePtr type, SourceRange source,
                       std::string detail)
        : trait_name(std::move(name)), types{type}, loc(std::move(source)),
          context(std::move(detail)) {}
    DeferredConstraint(std::string name, std::vector<MonoTypePtr> arguments,
                       SourceRange source, std::string detail)
        : trait_name(std::move(name)), types(std::move(arguments)),
          loc(std::move(source)), context(std::move(detail)) {}
  };
  std::vector<DeferredConstraint> deferred_constraints_;
  std::unordered_map<const ast::ApplyExpr *, SelectedTraitInstance>
      selected_trait_instances_;

  enum class ConcurrencyBoundary { TaskSpawn, ChannelSend };
  std::unordered_map<std::string, ConcurrencyBoundary> concurrency_boundaries_;
  struct CaptureFrame {
    const TypeEnv *local_root = nullptr;
    std::vector<MonoTypePtr> types;
    std::unordered_set<std::string> names;
  };
  std::vector<CaptureFrame> capture_frames_;
  std::unordered_map<const ast::FunctionExpr *, std::vector<MonoTypePtr>>
      function_capture_types_;
  std::unordered_map<std::string, std::vector<MonoTypePtr>>
      named_function_capture_types_;

  void require_trait(const std::string &trait_name, MonoTypePtr type,
                     const SourceRange &loc, std::string context);
  void require_captures_shareable(const std::vector<MonoTypePtr> &captures,
                                  const SourceRange &loc,
                                  const std::string &context);
  void
  enforce_concurrency_boundary(ast::ApplyExpr *node,
                               const std::string &callee_name,
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
  std::unordered_map<std::string, SourceRange> effect_origins_;
  std::vector<SourceRange> unhandled_effect_locations_;
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
  std::unordered_map<const ast::FunctionExpr *, EffectRef>
      predeclared_function_body_effects_;

  std::vector<std::string> ModulePaths;
  ImportTypeSource *import_src_ = nullptr;

  void bind_import_name(std::shared_ptr<TypeEnv> env,
                        const std::string &module_fqn,
                        const std::string &func_name,
                        const std::string &bind_name, int level);
  MonoTypePtr mono_from_import_sig(const ImportedFnSig &sig, int level,
                                   std::unordered_map<std::string, MonoTypePtr>
                                       *descriptor_variables = nullptr);
  MonoTypePtr mono_from_interface_function(const interface::Function &Function,
                                           int Level);
  MonoTypePtr from_ast_type(const yona::compiler::types::Type &t, int level);
  MonoTypePtr
  from_ast_type_impl(const yona::compiler::types::Type &t, int level,
                     std::unordered_map<std::string, MonoTypePtr> &variables);
};

} // namespace yona::compiler::typechecker

#endif /* YONA_SEMANTICS_TYPECHECKER_H */
