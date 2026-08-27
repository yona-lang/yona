#ifndef YONA_TYPECHECKER_UNIFICATION_H
#define YONA_TYPECHECKER_UNIFICATION_H

/// Type unification for Hindley-Milner inference.
///
/// unify(a, b) makes types a and b equal by binding type variables.
/// Reports errors via DiagnosticEngine when types are incompatible.

#include "Diagnostic.h"
#include "InferType.h"
#include "TypeArena.h"
#include "UnionFind.h"

namespace yona::compiler::typechecker {

class Unifier {
public:
  Unifier(TypeArena &arena, UnionFind &uf, DiagnosticEngine &diag);

  /// Unify two types. Returns true on success, false on error.
  bool unify(MonoTypePtr a, MonoTypePtr b, const SourceLocation &loc, const std::string &context = "");

  /// Resolve a type by chasing union-find links.
  MonoTypePtr resolve(MonoTypePtr type);

private:
  bool unify_inner(MonoTypePtr a, MonoTypePtr b, const SourceLocation &loc, const std::string &context);
  bool unify_effects(EffectRef left, EffectRef right, const SourceLocation &loc, const std::string &context);
  /// Temporary TypeChecker bridge. EffectSolver equality is authoritative;
  /// this only mirrors successful bindings into legacy row variables.
  bool sync_legacy_effect_rows(const std::vector<LatentEffect> &a_labs, MonoTypePtr a_rest, const std::vector<LatentEffect> &b_labs,
                               MonoTypePtr b_rest, const SourceLocation &loc, const std::string &context);
  void flatten_effect_row(MonoTypePtr rest, std::vector<LatentEffect> &labs, MonoTypePtr &out_rest);
  bool occurs_in(TypeId var_id, MonoTypePtr type);
  void adjust_levels(MonoTypePtr type, int level);

  TypeArena &arena_;
  UnionFind &uf_;
  DiagnosticEngine &diag_;
};

} // namespace yona::compiler::typechecker

#endif // YONA_TYPECHECKER_UNIFICATION_H
