#ifndef YONA_SEMANTICS_UNIFICATION_H
#define YONA_SEMANTICS_UNIFICATION_H
/// Type unification for Hindley-Milner inference.
///
/// unify(a, b) makes types a and b equal by binding type variables.
/// Reports errors via DiagnosticEngine when types are incompatible.

#include "yona/Model/InferType.h"
#include "yona/Model/TypeArena.h"
#include "yona/Semantics/UnionFind.h"
#include "yona/Support/Diagnostic.h"

namespace yona::compiler::typechecker {

/// Mutable unification facade borrowing one arena, union-find, and diagnostic
/// engine for its entire lifetime.
///
/// All three dependencies must outlive this object. MonoType pointers must
/// belong to a live compatible arena. Calls mutate bindings and diagnostics,
/// including resolve() through path compression, and are not thread-safe.
class Unifier {
public:
  Unifier(TypeArena &arena, UnionFind &uf, DiagnosticEngine &diag);

  /// Unify two types. Returns false for an incompatible or null type and emits
  /// diagnostics for semantic incompatibilities; no rollback is promised for
  /// bindings established before a later mismatch.
  bool unify(MonoTypePtr a, MonoTypePtr b, const SourceRange &loc,
             const std::string &context = "");

  /// Resolve a borrowed type by chasing union-find links. The returned pointer
  /// remains arena-owned; a null input returns null.
  MonoTypePtr resolve(MonoTypePtr type);

private:
  bool unify_inner(MonoTypePtr a, MonoTypePtr b, const SourceRange &loc,
                   const std::string &context);
  bool unify_effects(EffectRef left, EffectRef right, const SourceRange &loc,
                     const std::string &context);
  bool occurs_in(TypeId var_id, MonoTypePtr type);
  void adjust_levels(MonoTypePtr type, int level);

  TypeArena &arena_;
  UnionFind &uf_;
  DiagnosticEngine &diag_;
};

} // namespace yona::compiler::typechecker

#endif /* YONA_SEMANTICS_UNIFICATION_H */
