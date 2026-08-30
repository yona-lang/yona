#ifndef YONA_SEMANTICS_UNIONFIND_H
#define YONA_SEMANTICS_UNIONFIND_H
/// Union-find (disjoint set) for type variable unification.
///
/// Each type variable starts as its own representative.
/// unify_var(id, type) binds a variable to a concrete type.
/// find(id) returns the current binding (chasing links with path compression).

#include "yona/Model/InferType.h"

#include <unordered_map>

namespace yona::compiler::typechecker {

/// Owns type-variable entries but not the MonoType nodes stored in them.
///
/// Bound pointers must remain valid, normally by outliving this object in a
/// TypeArena. find() can mutate entries through path compression, so no method
/// is safe to overlap with another access without external synchronization.
class UnionFind {
public:
  /// Register a fresh type variable.
  void add_var(TypeId id, int level);

  /// Find the representative type for a variable.
  /// If missing or unbound, returns nullptr. The result is borrowed from its
  /// type arena.
  /// Uses path compression for amortized O(α(n)).
  MonoTypePtr find(TypeId id);

  /// Bind a variable to a borrowed type. A missing id is ignored.
  void bind(TypeId id, MonoTypePtr type);

  /// Get the level of a variable (for generalization). Missing ids return 0.
  int level(TypeId id) const;

  /// Set the level of a variable. A missing id is ignored.
  void set_level(TypeId id, int new_level);

  /// Check if a variable is bound.
  bool is_bound(TypeId id) const;

private:
  struct Entry {
    MonoTypePtr bound_to = nullptr; ///< nullptr = unbound root
    int level = 0;
  };
  std::unordered_map<TypeId, Entry> entries_;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_SEMANTICS_UNIONFIND_H */
