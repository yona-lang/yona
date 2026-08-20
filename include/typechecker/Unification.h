#ifndef YONA_TYPECHECKER_UNIFICATION_H
#define YONA_TYPECHECKER_UNIFICATION_H

/// Type unification for Hindley-Milner inference.
///
/// unify(a, b) makes types a and b equal by binding type variables.
/// Reports errors via DiagnosticEngine when types are incompatible.

#include "InferType.h"
#include "TypeArena.h"
#include "UnionFind.h"
#include "Diagnostic.h"

namespace yona::compiler::typechecker {

class Unifier {
public:
    Unifier(TypeArena& arena, UnionFind& uf, DiagnosticEngine& diag);

    /// Unify two types. Returns true on success, false on error.
    bool unify(MonoTypePtr a, MonoTypePtr b, const SourceLocation& loc,
               const std::string& context = "");

    /// Resolve a type by chasing union-find links.
    MonoTypePtr resolve(MonoTypePtr type);

private:
    bool unify_inner(MonoTypePtr a, MonoTypePtr b, const SourceLocation& loc,
                     const std::string& context);
    bool unify_effect_rows(MonoTypePtr a, MonoTypePtr b,
                           const SourceLocation& loc, const std::string& context);
    bool occurs_in(TypeId var_id, MonoTypePtr type);
    bool occurs_in_value(TypeId var_id, MonoTypePtr type);
    MonoTypePtr close_effect_occurs(TypeId var_id, MonoTypePtr row);
    bool bind_var(MonoTypePtr var, MonoTypePtr type, const SourceLocation& loc,
                  const std::string& context);
    void adjust_levels(MonoTypePtr type, int level);

    TypeArena& arena_;
    UnionFind& uf_;
    DiagnosticEngine& diag_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_UNIFICATION_H
