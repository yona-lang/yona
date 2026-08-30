#include "yona/Semantics/UnionFind.h"

namespace yona::compiler::typechecker {

void UnionFind::add_var(TypeId id, int level) {
  entries_[id] = {nullptr, level};
}

MonoTypePtr UnionFind::find(TypeId id) {
  auto it = entries_.find(id);
  if (it == entries_.end())
    return nullptr;
  auto &entry = it->second;
  if (!entry.bound_to)
    return nullptr;

  // Path compression: if bound to another variable, chase the chain
  if (entry.bound_to->tag == MonoType::Var) {
    auto resolved = find(entry.bound_to->var_id);
    if (resolved) {
      entry.bound_to = resolved;
      return resolved;
    }
  }
  return entry.bound_to;
}

void UnionFind::bind(TypeId id, MonoTypePtr type) {
  auto it = entries_.find(id);
  if (it != entries_.end()) {
    it->second.bound_to = type;
  }
}

int UnionFind::level(TypeId id) const {
  auto it = entries_.find(id);
  return (it != entries_.end()) ? it->second.level : 0;
}

void UnionFind::set_level(TypeId id, int new_level) {
  auto it = entries_.find(id);
  if (it != entries_.end())
    it->second.level = new_level;
}

bool UnionFind::is_bound(TypeId id) const {
  auto it = entries_.find(id);
  return it != entries_.end() && it->second.bound_to != nullptr;
}

} // namespace yona::compiler::typechecker
