#ifndef YONA_MODEL_TYPEARENA_H
#define YONA_MODEL_TYPEARENA_H
/// Arena allocator for MonoType nodes.
///
/// All types created during inference are allocated here. The arena owns its
/// nodes and EffectSolver; returned pointers and solver references remain
/// stable until arena destruction. Child pointers supplied to make_* are
/// borrowed and must remain valid for the same interval, normally by coming
/// from this arena. Allocation and mutation are unsynchronized; concurrent
/// reads are valid only after construction has quiesced.

#include "yona/Model/InferType.h"

#include <cstddef>
#include <deque>

namespace yona::compiler::typechecker {

class TypeArena {
public:
  TypeArena() = default;
  TypeArena(const TypeArena &) = delete;
  TypeArena &operator=(const TypeArena &) = delete;
  TypeArena(TypeArena &&) = delete;
  TypeArena &operator=(TypeArena &&) = delete;

  /// Create a fresh unification variable at the given level.
  MonoTypePtr fresh_var(int level);

  /// Create a built-in type constructor.
  MonoTypePtr make_con(TyCon con);

  /// Create a function type. param and ret are borrowed, not adopted.
  MonoTypePtr make_arrow(MonoTypePtr param, MonoTypePtr ret);

  /// Create a function type with a solver-owned latent-effect expression.
  /// Throws std::invalid_argument when `effect` belongs to another solver.
  MonoTypePtr make_arrow(MonoTypePtr param, MonoTypePtr ret, EffectRef effect);

  /// Create a named type application (e.g., Option Int). Argument pointers are
  /// borrowed, while the vector and name are copied into the arena node.
  MonoTypePtr make_app(const std::string &name, std::vector<MonoTypePtr> args);

  /// Create a tuple type.
  MonoTypePtr make_tuple(std::vector<MonoTypePtr> elems);

  /// Create a record type (closed or open row).
  MonoTypePtr
  make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields,
              MonoTypePtr row_rest = nullptr);

  /// Move a node value into the arena and return a stable borrowed pointer.
  MonoTypePtr alloc(MonoType t);

  /// Number of types allocated.
  size_t size() const { return storage_.size(); }

  /// Borrow the arena-owned solver. The reference is invalidated by arena
  /// destruction and mutable access shares the arena's synchronization rules.
  EffectSolver &effect_solver() noexcept { return effect_solver_; }
  const EffectSolver &effect_solver() const noexcept { return effect_solver_; }

private:
  EffectSolver effect_solver_;
  std::deque<MonoType> storage_;
  TypeId next_id_ = 0;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_MODEL_TYPEARENA_H */
