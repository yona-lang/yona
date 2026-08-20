#ifndef YONA_TYPECHECKER_TYPE_ARENA_H
#define YONA_TYPECHECKER_TYPE_ARENA_H

/// Arena allocator for MonoType nodes.
///
/// All types created during inference are allocated here.
/// Stable pointers: deque never invalidates existing elements.

#include "InferType.h"
#include <deque>
#include <unordered_map>

namespace yona::compiler::typechecker {

class TypeArena {
public:
    /// Create a fresh unification variable at the given level.
    MonoTypePtr fresh_var(int level);

    /// Create a built-in type constructor.
    MonoTypePtr make_con(TyCon con);

    /// Create a function type (optional latent effect row).
    MonoTypePtr make_arrow(MonoTypePtr param, MonoTypePtr ret,
                           std::vector<std::string> effect_labels = {},
                           MonoTypePtr effect_rest = nullptr,
                           std::unordered_map<std::string, SourceLocation> origins = {});

    /// Create a named type application (e.g., Option Int).
    MonoTypePtr make_app(const std::string& name, std::vector<MonoTypePtr> args);

    /// Create a tuple type.
    MonoTypePtr make_tuple(std::vector<MonoTypePtr> elems);

    /// Create a record type (closed or open row).
    MonoTypePtr make_record(std::vector<std::pair<std::string, MonoTypePtr>> fields,
                             MonoTypePtr row_rest = nullptr);

    /// Create an effect-row binder (open-row unification).
    /// `extra_rests` are additional open tails (union of row variables).
    MonoTypePtr make_effect_row(std::vector<std::string> labels,
                                MonoTypePtr rest = nullptr,
                                std::vector<MonoTypePtr> extra_rests = {},
                                std::unordered_map<std::string, SourceLocation> origins = {});

    /// One rest pointer for a function arrow: nullptr, a var, or a union row.
    MonoTypePtr pack_effect_rest(const std::vector<MonoTypePtr>& open_rests);

    /// Allocate and return a stable pointer to a MonoType.
    MonoTypePtr alloc(MonoType t);

    /// Number of types allocated.
    size_t size() const { return storage_.size(); }

private:
    std::deque<MonoType> storage_;
    TypeId next_id_ = 0;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_ARENA_H
