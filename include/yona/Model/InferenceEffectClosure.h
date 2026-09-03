#ifndef YONA_MODEL_INFERENCEEFFECTCLOSURE_H
#define YONA_MODEL_INFERENCEEFFECTCLOSURE_H

#include "yona/Model/InferType.h"

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace yona::compiler::typechecker {

enum class InferenceEffectClosureErrorCode {
  ForeignEffect,
  MissingEffectAtom,
};

struct InferenceEffectClosureError {
  InferenceEffectClosureErrorCode Code;
  std::string Message;
};

struct InferenceEffectClosureRow {
  EffectRef Effect;
  EffectNormalForm Normal;
};

/// Deterministic closure of every type, Arrow effect, Promise effect, and
/// operation argument reachable from one inference scheme.
struct InferenceEffectClosure {
  std::vector<MonoTypePtr> Types;
  std::vector<SchemeEffectRoot> ArrowRoots;
  std::vector<InferenceEffectClosureRow> Effects;
  std::vector<EffectAtomId> Atoms;
};

using ResolveInferenceType = std::function<MonoTypePtr(MonoTypePtr)>;

/// Traverse a scheme's complete type/effect graph once. The resolver may be
/// empty for already-zonked graphs. Invalid/foreign effect references and
/// absent catalog atoms are returned as stable values rather than leaked
/// container or solver exceptions.
[[nodiscard]] std::expected<InferenceEffectClosure, InferenceEffectClosureError>
collectInferenceEffectClosure(MonoTypePtr Body,
                              std::span<const Constraint> Constraints,
                              const EffectSolver &Effects,
                              const EffectAtomCatalog &Atoms,
                              ResolveInferenceType Resolve = {});

} // namespace yona::compiler::typechecker

#endif /* YONA_MODEL_INFERENCEEFFECTCLOSURE_H */
