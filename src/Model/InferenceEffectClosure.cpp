#include "yona/Model/InferenceEffectClosure.h"

#include <unordered_set>

namespace yona::compiler::typechecker {

std::expected<InferenceEffectClosure, InferenceEffectClosureError>
collectInferenceEffectClosure(MonoTypePtr Body,
                              std::span<const Constraint> Constraints,
                              const EffectSolver &Effects,
                              const EffectAtomCatalog &Atoms,
                              ResolveInferenceType Resolve) {
  InferenceEffectClosure Result;
  std::unordered_set<MonoTypePtr> SeenTypes;
  std::unordered_set<EffectRef, EffectRefHasher> SeenEffects;
  std::unordered_set<EffectAtomId> SeenAtoms;

  const auto RememberEffect = [&](EffectRef Effect) {
    if (SeenEffects.insert(Effect).second)
      Result.Effects.push_back({Effect, {}});
  };
  const auto VisitType = [&](this const auto &Self, MonoTypePtr Type) -> void {
    if (Resolve)
      Type = Resolve(Type);
    if (!Type || !SeenTypes.insert(Type).second)
      return;
    Result.Types.push_back(Type);
    switch (Type->tag) {
    case MonoType::Arrow:
      Result.ArrowRoots.push_back({Type, Type->arrow_effect});
      RememberEffect(Type->arrow_effect);
      Self(Type->param_type);
      Self(Type->return_type);
      return;
    case MonoType::App:
      if (!Type->resolved_key && Type->type_name == "Promise" &&
          Type->promise_effects.valid())
        RememberEffect(Type->promise_effects);
      for (const auto *Argument : Type->args)
        Self(Argument);
      return;
    case MonoType::MTuple:
      for (const auto *Element : Type->elements)
        Self(Element);
      return;
    case MonoType::MRecord:
      for (const auto &[_, Field] : Type->record_fields)
        Self(Field);
      Self(Type->row_rest);
      return;
    case MonoType::Sum:
      for (const auto *Alternative : Type->sum_alternatives)
        Self(Alternative);
      return;
    case MonoType::Var:
    case MonoType::Con:
      return;
    }
  };

  VisitType(Body);
  for (const auto &Constraint : Constraints)
    for (const auto *Type : Constraint.types)
      VisitType(Type);

  for (std::size_t Index = 0; Index < Result.Effects.size(); ++Index) {
    EffectNormalForm Normal;
    try {
      Normal = Effects.summarize(Result.Effects[Index].Effect);
    } catch (const std::invalid_argument &) {
      return std::unexpected(InferenceEffectClosureError{
          InferenceEffectClosureErrorCode::ForeignEffect,
          "effect reference is invalid or belongs to another solver"});
    }
    Result.Effects[Index].Normal = Normal;
    const auto VisitAtom = [&](EffectAtomId RawAtom)
        -> std::expected<void, InferenceEffectClosureError> {
      EffectAtomId Atom;
      try {
        Atom = Effects.canonicalAtom(RawAtom);
      } catch (const std::invalid_argument &Failure) {
        return std::unexpected(InferenceEffectClosureError{
            InferenceEffectClosureErrorCode::MissingEffectAtom,
            Failure.what()});
      }
      if (!SeenAtoms.insert(Atom).second)
        return {};
      if (!Atoms.valid(Atom))
        return std::unexpected(InferenceEffectClosureError{
            InferenceEffectClosureErrorCode::MissingEffectAtom,
            "inference scheme references an absent effect atom"});
      Result.Atoms.push_back(Atom);
      if (const auto *Operation =
              std::get_if<OperationEffectAtom>(&Atoms.at(Atom))) {
        for (const auto *Argument : Operation->TypeArguments)
          VisitType(Argument);
        for (const auto Argument : Operation->EffectArguments)
          RememberEffect(Argument);
      }
      return {};
    };
    for (const auto Atom : Normal.known_atoms)
      if (auto Visited = VisitAtom(Atom); !Visited)
        return std::unexpected(Visited.error());
    for (const auto &Tail : Normal.tails)
      for (const auto Atom : Tail.excluded_atoms)
        if (auto Visited = VisitAtom(Atom); !Visited)
          return std::unexpected(Visited.error());
  }
  return Result;
}

} // namespace yona::compiler::typechecker
