#ifndef YONA_TYPECHECKER_INFER_TYPE_H
#define YONA_TYPECHECKER_INFER_TYPE_H

/// Type representations for Hindley-Milner inference.
///
/// MonoType — monomorphic type (may contain unification variables).
/// TypeScheme — polymorphic type (forall a b. constraints => body).
/// All MonoType nodes are arena-allocated via TypeArena.

#include "SourceLocation.h"
#include "typechecker/EffectSolver.h"
#include <cstdint>
#include <string>
#include <vector>

namespace yona::compiler::typechecker {

using TypeId = uint32_t;

/// A concrete effect operation a function may perform (`Fs.read`), plus the
/// introducing `perform` location for E0202.
struct LatentEffect {
  std::string op_key;
  SourceLocation perform_loc;
};

/// Built-in type constructors.
enum class TyCon { Int, Float, Bool, String, Char, Byte, Symbol, Unit, Seq, Set, Dict, Tuple, Function, Promise, ByteArray };

/// A monomorphic type node in the inference system.
/// Allocated by TypeArena, referenced by pointer (stable, never moved).
struct MonoType {
  enum Tag {
    Var,     ///< Unification variable
    Con,     ///< Built-in type constructor
    App,     ///< Named type application: App("Option", [Int])
    Arrow,   ///< Function type: Arrow(param, ret)
    MTuple,  ///< Product type: Tuple([Int, String])
    MRecord, ///< Record type: { name : String, age : Int | r }
    ERow,    ///< Transitional legacy effect-row payload (not a value type)
  } tag;

  // Var
  TypeId var_id = 0;
  int level = 0;

  // Con
  TyCon con = TyCon::Int;

  // App
  std::string type_name;
  std::vector<const MonoType *> args;

  // Arrow
  const MonoType *param_type = nullptr;
  const MonoType *return_type = nullptr;
  /// Authoritative latent-effect expression. Every arena-created Arrow owns
  /// a valid reference in `effect_solver`.
  EffectRef arrow_effect;
  const EffectSolver *effect_solver = nullptr;

  /// Transitional compatibility view for the unconverted TypeChecker. New
  /// consumers must use `arrow_effect`; Task 3 removes these fields.
  bool has_legacy_effect_view = false;
  std::vector<LatentEffect> arrow_effects;
  const MonoType *effect_rest = nullptr;

  // MTuple
  std::vector<const MonoType *> elements;

  // MRecord: sorted (name, type) pairs + optional row rest variable
  std::vector<std::pair<std::string, const MonoType *>> record_fields;
  const MonoType *row_rest = nullptr; // row variable (Var) or nullptr (closed row)

  /// Create a Var type
  static MonoType make_var(TypeId id, int lvl) {
    MonoType t;
    t.tag = Var;
    t.var_id = id;
    t.level = lvl;
    return t;
  }
  /// Create a Con type
  static MonoType make_con(TyCon c) {
    MonoType t;
    t.tag = Con;
    t.con = c;
    return t;
  }
  /// Create an Arrow type
  static MonoType make_arrow(const MonoType *p, const MonoType *r, EffectRef effect,
                             const EffectSolver *solver,
                             bool has_legacy_effect_view,
                             std::vector<LatentEffect> legacy_effects = {},
                             const MonoType *legacy_rest = nullptr) {
    MonoType t;
    t.tag = Arrow;
    t.param_type = p;
    t.return_type = r;
    t.arrow_effect = effect;
    t.effect_solver = solver;
    t.has_legacy_effect_view = has_legacy_effect_view;
    t.arrow_effects = std::move(legacy_effects);
    t.effect_rest = legacy_rest;
    return t;
  }
  /// Create an effect-row payload (labels + optional rest).
  static MonoType make_erow(std::vector<LatentEffect> effects, const MonoType *rest, EffectRef effect, const EffectSolver *solver) {
    MonoType t;
    t.tag = ERow;
    t.arrow_effects = std::move(effects);
    t.effect_rest = rest;
    t.arrow_effect = effect;
    t.effect_solver = solver;
    t.has_legacy_effect_view = true;
    return t;
  }
  /// Create an App type
  static MonoType make_app(const std::string &name, std::vector<const MonoType *> a) {
    MonoType t;
    t.tag = App;
    t.type_name = name;
    t.args = std::move(a);
    return t;
  }
  /// Create a Tuple type
  static MonoType make_tuple(std::vector<const MonoType *> elems) {
    MonoType t;
    t.tag = MTuple;
    t.elements = std::move(elems);
    return t;
  }
  /// Create a Record type (closed or open row)
  static MonoType make_record(std::vector<std::pair<std::string, const MonoType *>> fields, const MonoType *rest = nullptr) {
    MonoType t;
    t.tag = MRecord;
    t.record_fields = std::move(fields);
    t.row_rest = rest;
    return t;
  }
};

using MonoTypePtr = const MonoType *;

/// A trait constraint: e.g., `Eq a` or `Foldable collection element`.
struct Constraint {
  std::string trait_name;
  std::vector<MonoTypePtr> types;

  Constraint(std::string name, MonoTypePtr type) : trait_name(std::move(name)), types{type} {}
  Constraint(std::string name, std::vector<MonoTypePtr> arguments) : trait_name(std::move(name)), types(std::move(arguments)) {}
};

/// Polymorphic type scheme: forall vars. constraints => body
struct TypeScheme {
  std::vector<TypeId> quantified_vars;
  std::vector<Constraint> constraints;
  MonoTypePtr body = nullptr;
  /// The complete solver graph reachable from every arrow in `body`.
  /// `effect_roots` records the corresponding original roots in deterministic
  /// traversal order so each instantiation can recreate the same sharing.
  std::optional<EffectGraphTemplate> effect_graph;
  std::vector<EffectRef> effect_roots;

  /// Monomorphic scheme (no quantification)
  explicit TypeScheme(MonoTypePtr t) : body(t) {}
  TypeScheme() = default;
  TypeScheme(std::vector<TypeId> qv, MonoTypePtr b) : quantified_vars(std::move(qv)), body(b) {}
  TypeScheme(std::vector<TypeId> qv, std::vector<Constraint> c, MonoTypePtr b) : quantified_vars(std::move(qv)), constraints(std::move(c)), body(b) {}
};

/// Pretty-print a monotype for error messages
std::string pretty_print(MonoTypePtr type);

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_INFER_TYPE_H
