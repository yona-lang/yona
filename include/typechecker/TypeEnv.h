#ifndef YONA_TYPECHECKER_TYPE_ENV_H
#define YONA_TYPECHECKER_TYPE_ENV_H

/// Lexical type environment for Hindley-Milner inference.
///
/// TypeEnv is a scope chain: each scope holds local bindings and a pointer
/// to its parent. Lookup walks the chain from innermost to outermost.
///
/// Separate registries for ADTs, traits, and effects hold type-level
/// declarations that are visible throughout a module.

#include "InferType.h"
#include <memory>
#include <optional>
#include <unordered_map>

namespace yona::compiler::typechecker {

class TypeArena;

class TypeEnv : public std::enable_shared_from_this<TypeEnv> {
public:
    explicit TypeEnv(std::shared_ptr<TypeEnv> parent = nullptr);

    /// Look up a name in scope chain. Returns nullopt if not found.
    std::optional<TypeScheme> lookup(const std::string& name) const;

    /// Bind a name to a monomorphic type (no generalization).
    void bind(const std::string& name, MonoTypePtr type);

    /// Bind a name to a polymorphic scheme (after generalization).
    void bind_scheme(const std::string& name, TypeScheme scheme);

    /// Create a child scope.
    std::shared_ptr<TypeEnv> child() const;

    /// Get local bindings (for error messages / debugging).
    const std::unordered_map<std::string, TypeScheme>& locals() const { return bindings_; }

    /// Whether `name` is bound between this scope and `ancestor`, inclusive.
    /// Used to distinguish a lambda/task's locals from captured outer values.
    bool bound_through(const std::string& name, const TypeEnv* ancestor) const;

    /// Collect all visible names (for "did you mean?" suggestions).
    std::vector<std::string> all_names() const;

private:
    std::shared_ptr<TypeEnv> parent_;
    std::unordered_map<std::string, TypeScheme> bindings_;
};

/// Register all builtin types and operator type schemes into an env.
/// Call once at the start of type checking to populate the root env.
void register_builtins(TypeEnv& env, TypeArena& arena);

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_TYPE_ENV_H
