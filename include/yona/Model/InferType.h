#ifndef YONA_MODEL_INFERTYPE_H
#define YONA_MODEL_INFERTYPE_H
/// Type representations for Hindley-Milner inference.
///
/// MonoType — monomorphic type (may contain unification variables).
/// TypeScheme — polymorphic type (forall a b. constraints => body).
/// All MonoType nodes are arena-allocated via TypeArena. Pointers in nodes,
/// constraints, and schemes are borrowed and remain valid only while their
/// arena lives; copying one of these values does not copy the type graph.

#include "yona/Model/EffectSolver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace yona::compiler::typechecker {

using TypeId = uint32_t;

/// Built-in type constructors.
enum class TyCon {
  Int,
  Float,
  Bool,
  String,
  Char,
  Byte,
  Symbol,
  Unit,
  Seq,
  Set,
  Dict,
  Tuple,
  Function,
  Promise,
  ByteArray
};

/// A monomorphic type node in the inference system.
/// Allocated by TypeArena, referenced by pointer (stable, never moved). Child
/// pointers and effect_solver are non-owning and must outlive this node. Valid
/// Arrow nodes require non-null parameter, result, and solver pointers plus an
/// EffectRef owned by that solver.
struct MonoType {
  enum Tag {
    Var,     ///< Unification variable
    Con,     ///< Built-in type constructor
    App,     ///< Named type application: App("Option", [Int])
    Arrow,   ///< Function type: Arrow(param, ret)
    MTuple,  ///< Product type: Tuple([Int, String])
    MRecord, ///< Record type: { name : String, age : Int | r }
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
  /// Latent-effect expression. Every arena-created Arrow owns a valid
  /// reference in `effect_solver`.
  EffectRef arrow_effect;
  const EffectSolver *effect_solver = nullptr;

  // MTuple
  std::vector<const MonoType *> elements;

  // MRecord: sorted (name, type) pairs + optional row rest variable
  std::vector<std::pair<std::string, const MonoType *>> record_fields;
  const MonoType *row_rest =
      nullptr; // row variable (Var) or nullptr (closed row)

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
  static MonoType make_arrow(const MonoType *p, const MonoType *r,
                             EffectRef effect, const EffectSolver *solver) {
    MonoType t;
    t.tag = Arrow;
    t.param_type = p;
    t.return_type = r;
    t.arrow_effect = effect;
    t.effect_solver = solver;
    return t;
  }
  /// Create an App type
  static MonoType make_app(const std::string &name,
                           std::vector<const MonoType *> a) {
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
  static MonoType
  make_record(std::vector<std::pair<std::string, const MonoType *>> fields,
              const MonoType *rest = nullptr) {
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

  Constraint(std::string name, MonoTypePtr type)
      : trait_name(std::move(name)), types{type} {}
  Constraint(std::string name, std::vector<MonoTypePtr> arguments)
      : trait_name(std::move(name)), types(std::move(arguments)) {}
};

/// Polymorphic type scheme: forall vars. constraints => body.
///
/// body and all constraint pointers borrow one TypeArena. effect_graph, when
/// present, owns an immutable effect snapshot independently of that arena.
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
  TypeScheme(std::vector<TypeId> qv, MonoTypePtr b)
      : quantified_vars(std::move(qv)), body(b) {}
  TypeScheme(std::vector<TypeId> qv, std::vector<Constraint> c, MonoTypePtr b)
      : quantified_vars(std::move(qv)), constraints(std::move(c)), body(b) {}
};

/// Pretty-print a borrowed monotype for error messages.
///
/// A null root renders as `?`. All reachable pointers must otherwise satisfy
/// the MonoType lifetime invariants. Concurrent calls are safe when the arena
/// and referenced EffectSolver are not being mutated.
std::string pretty_print(MonoTypePtr type);

} // namespace yona::compiler::typechecker

#endif /* YONA_MODEL_INFERTYPE_H */
