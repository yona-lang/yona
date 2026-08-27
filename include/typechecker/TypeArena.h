#ifndef YONA_TYPECHECKER_TYPE_ARENA_H
#define YONA_TYPECHECKER_TYPE_ARENA_H

/// Arena allocator for MonoType nodes.
///
/// All types created during inference are allocated here.
/// Stable pointers: deque never invalidates existing elements.

#include "InferType.h"
#include <deque>
#include <optional>
#include <unordered_map>

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

  /// Create a function type.
  MonoTypePtr make_arrow(MonoTypePtr param, MonoTypePtr ret, std::vector<LatentEffect> effects = {}, MonoTypePtr rest = nullptr);

  /// Create an arrow from an already-built effect expression. This is the
  /// authoritative API used by effect-aware inference and unification.
  MonoTypePtr make_arrow_with_effect(MonoTypePtr param, MonoTypePtr ret, EffectRef effect);

  /// Create an effect-row payload (known labels + optional rest).
  MonoTypePtr make_erow(std::vector<LatentEffect> effects, MonoTypePtr rest = nullptr);

  /// Create a named type application (e.g., Option Int).
  MonoTypePtr make_app(const std::string &name, std::vector<MonoTypePtr> args);

  /// Create a tuple type.
  MonoTypePtr make_tuple(std::vector<MonoTypePtr> elems);

  /// Create a record type (closed or open row).
  MonoTypePtr make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields, MonoTypePtr row_rest = nullptr);

  /// Allocate and return a stable pointer to a MonoType.
  MonoTypePtr alloc(MonoType t);

  /// Number of types allocated.
  size_t size() const { return storage_.size(); }

  EffectSolver &effect_solver() noexcept { return effect_solver_; }
  const EffectSolver &effect_solver() const noexcept { return effect_solver_; }

  /// Bridge for Task 3: translate a legacy ERow/Var view into its solver
  /// reference without treating the row as a value type.
  std::optional<EffectRef> legacy_effect_ref(MonoTypePtr type);

private:
  EffectRef make_legacy_effect(const std::vector<LatentEffect> &effects, MonoTypePtr rest);

  EffectSolver effect_solver_;
  std::deque<MonoType> storage_;
  TypeId next_id_ = 0;
  std::unordered_map<TypeId, EffectRef> legacy_effect_vars_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_ARENA_H
