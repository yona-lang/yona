#include "yona/Semantics/StructuralTypeProjection.h"

#include "yona/Model/InferenceEffectClosure.h"
#include "yona/Syntax/Ast.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace yona::semantics {
namespace tc = compiler::typechecker;

namespace {

StructuralProjectionError error(StructuralProjectionErrorCode Code,
                                SourceRange Range, std::string Path,
                                std::string Message) {
  return {Code, Range, std::move(Path), std::move(Message)};
}

template <class T>
using ProjectionResult = std::expected<T, StructuralProjectionError>;

constexpr std::string_view InvalidEffectReferenceMessage =
    "effect reference is invalid or belongs to another solver";

std::optional<model::PrimitiveType> primitive(tc::TyCon Type) {
  switch (Type) {
  case tc::TyCon::Unit:
    return model::PrimitiveType::Unit;
  case tc::TyCon::Bool:
    return model::PrimitiveType::Bool;
  case tc::TyCon::Int:
    return model::PrimitiveType::Int;
  case tc::TyCon::Float:
    return model::PrimitiveType::Float;
  case tc::TyCon::Char:
    return model::PrimitiveType::Char;
  case tc::TyCon::String:
    return model::PrimitiveType::String;
  case tc::TyCon::Symbol:
    return model::PrimitiveType::Symbol;
  case tc::TyCon::Byte:
    return model::PrimitiveType::Byte;
  case tc::TyCon::Seq:
  case tc::TyCon::Set:
  case tc::TyCon::Dict:
  case tc::TyCon::Tuple:
  case tc::TyCon::Function:
  case tc::TyCon::Promise:
  case tc::TyCon::ByteArray:
    return std::nullopt;
  }
  return std::nullopt;
}

bool validVisibility(tc::DeclarationVisibility Visibility) {
  return std::to_underlying(Visibility) <=
         std::to_underlying(tc::DeclarationVisibility::Public);
}

bool containsRange(SourceRange Outer, SourceRange Inner) {
  return Outer.isValid() && Inner.isValid() && Outer.Source == Inner.Source &&
         Inner.Offset >= Outer.Offset && Inner.Length <= Outer.Length &&
         Inner.Offset - Outer.Offset <= Outer.Length - Inner.Length;
}

} // namespace

struct StructuralTypeProjection::ProjectionScope {
  tc::ProjectionDeclarationId Id = 0;
  const ProjectionScope *Parent = nullptr;
  model::BinderOwnerKind Kind = model::BinderOwnerKind::Function;
  std::optional<model::CanonicalBinderOwnerIdentity> Identity;
  std::vector<model::TypeParameterId> DeclaredTypes;
  std::vector<model::EffectVariableDeclaration> DeclaredEffects;
  std::unordered_map<tc::TypeId, model::TypeParameterId> TypeBinders;
  std::unordered_map<tc::EffectVarId, model::EffectVariableId> EffectBinders;
  std::unordered_map<tc::MonoTypePtr, const tc::CallableTypeContract *>
      CallableContracts;
  mutable std::unordered_set<tc::MonoTypePtr> ConsumedCallableContracts;
  std::unordered_map<const tc::MonoType *, model::TypeId> NonArrowMemo;
  std::unordered_map<const tc::MonoType *, model::TypeId> ArrowMemo;
  std::unordered_map<tc::EffectRef, model::EffectRowId, tc::EffectRefHasher>
      EffectMemo;
  std::unordered_set<const tc::MonoType *> ActiveTypes;
  std::unordered_set<tc::EffectRef, tc::EffectRefHasher> ActiveEffects;
  StructuralProjectionTransaction *Transaction = nullptr;

  [[nodiscard]] std::optional<model::TypeParameterId>
  typeBinder(tc::TypeId Source) const {
    for (auto *Scope = this; Scope; Scope = Scope->Parent)
      if (const auto Found = Scope->TypeBinders.find(Source);
          Found != Scope->TypeBinders.end())
        return Found->second;
    return std::nullopt;
  }

  [[nodiscard]] std::optional<model::EffectVariableDeclaration>
  effectBinder(tc::EffectVarId Source) const {
    for (auto *Scope = this; Scope; Scope = Scope->Parent)
      if (const auto Found = Scope->EffectBinders.find(Source);
          Found != Scope->EffectBinders.end())
        return model::EffectVariableDeclaration{
            Found->second, model::EffectVariableKind::Flexible};
    return std::nullopt;
  }

  [[nodiscard]] const tc::CallableTypeContract *
  callable(tc::MonoTypePtr Arrow) const {
    for (auto *Scope = this; Scope; Scope = Scope->Parent)
      if (const auto Found = Scope->CallableContracts.find(Arrow);
          Found != Scope->CallableContracts.end()) {
        Scope->ConsumedCallableContracts.insert(Arrow);
        return Found->second;
      }
    return nullptr;
  }
};

class StructuralTypeProjection::StructuralProjectionTransaction final {
public:
  explicit StructuralProjectionTransaction(StructuralTypeProjection &Owner)
      : Owner_(Owner), Types_(Owner.Types_.beginAppendTransaction()),
        Declarations_(Owner.Declarations_) {}

  ProjectionResult<model::TypeId>
  projectType(tc::MonoTypePtr Type, ProjectionScope &Scope, SourceRange Range,
              std::string Path, bool AllowArrow = true) {
    if (!Type)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, std::move(Path),
                                   "type projection received a null type"));
    if (!AllowArrow && Type->tag == tc::MonoType::Arrow)
      return std::unexpected(
          error(StructuralProjectionErrorCode::WrongArrowShape, Range,
                std::move(Path),
                "closed value projection does not accept a callable type"));

    auto &Memo =
        Type->tag == tc::MonoType::Arrow ? Scope.ArrowMemo : Scope.NonArrowMemo;
    if (const auto Found = Memo.find(Type); Found != Memo.end())
      return Found->second;
    if (!Scope.ActiveTypes.insert(Type).second)
      return std::unexpected(error(
          StructuralProjectionErrorCode::OpenValueType, Range, std::move(Path),
          "inference type graph contains a structural cycle"));

    auto Finish = [&](ProjectionResult<model::TypeId> Result)
        -> ProjectionResult<model::TypeId> {
      Scope.ActiveTypes.erase(Type);
      if (Result)
        Memo.emplace(Type, *Result);
      return Result;
    };
    auto Intern =
        [&](model::StructuralType Value) -> ProjectionResult<model::TypeId> {
      auto Result = Types_.intern(std::move(Value));
      if (!Result)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range, Path,
                  Result.error()));
      return *Result;
    };

    switch (Type->tag) {
    case tc::MonoType::Var: {
      const auto Binder = Scope.typeBinder(Type->var_id);
      if (!Binder)
        return Finish(std::unexpected(error(
            StructuralProjectionErrorCode::UnboundBinder, Range,
            std::move(Path), "type variable is not owned by this scope")));
      return Finish(Intern(model::TypeParameterType{*Binder}));
    }
    case tc::MonoType::Con: {
      if (Type->con == tc::TyCon::ByteArray) {
        auto Byte = Types_.intern(model::PrimitiveType::Byte);
        if (!Byte)
          return Finish(
              std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                    Range, Path, Byte.error())));
        return Finish(Intern(model::ArrayType{*Byte, true}));
      }
      const auto Primitive = primitive(Type->con);
      if (!Primitive)
        return Finish(std::unexpected(error(
            StructuralProjectionErrorCode::OpenValueType, Range,
            std::move(Path), "bare parameterized type constructor is open")));
      return Finish(Intern(*Primitive));
    }
    case tc::MonoType::Arrow: {
      const auto *Contract = Scope.callable(Type);
      if (!Contract)
        return Finish(std::unexpected(error(
            StructuralProjectionErrorCode::MissingCallableContract, Range,
            std::move(Path),
            "callable type has no ownership and calling-convention contract")));
      return Finish(projectCallable(Type, *Contract, Scope, Range,
                                    std::move(Path), {}, {}, {}, std::nullopt,
                                    std::nullopt));
    }
    case tc::MonoType::App:
      return Finish(projectApplication(Type, Scope, Range, std::move(Path)));
    case tc::MonoType::MTuple: {
      model::TupleType Tuple;
      Tuple.Elements.reserve(Type->elements.size());
      for (std::size_t Index = 0; Index < Type->elements.size(); ++Index) {
        auto Element =
            projectType(Type->elements[Index], Scope, Range,
                        Path + "/element[" + std::to_string(Index) + "]");
        if (!Element)
          return Finish(std::unexpected(Element.error()));
        Tuple.Elements.push_back(*Element);
      }
      return Finish(Intern(std::move(Tuple)));
    }
    case tc::MonoType::MRecord: {
      model::RecordType Record;
      Record.Fields.reserve(Type->record_fields.size());
      std::string Previous;
      for (std::size_t Index = 0; Index < Type->record_fields.size(); ++Index) {
        const auto &[Name, FieldType] = Type->record_fields[Index];
        if (Index != 0 && Name <= Previous)
          return Finish(std::unexpected(
              error(StructuralProjectionErrorCode::InvalidRecordRow, Range,
                    Path + "/field[" + std::to_string(Index) + "]",
                    "record fields must be uniquely sorted")));
        Previous = Name;
        auto Field =
            projectType(FieldType, Scope, Range, Path + "/field[" + Name + "]");
        if (!Field)
          return Finish(std::unexpected(Field.error()));
        Record.Fields.push_back({Name, *Field});
      }
      if (Type->row_rest) {
        auto Rest = projectType(Type->row_rest, Scope, Range, Path + "/row");
        if (!Rest)
          return Finish(std::unexpected(Rest.error()));
        Record.RowRest = *Rest;
      }
      auto Result = Types_.intern(std::move(Record));
      if (!Result)
        return Finish(std::unexpected(
            error(StructuralProjectionErrorCode::InvalidRecordRow, Range,
                  std::move(Path), Result.error())));
      return Finish(*Result);
    }
    case tc::MonoType::Sum: {
      std::vector<model::TypeId> Alternatives;
      Alternatives.reserve(Type->sum_alternatives.size());
      for (std::size_t Index = 0; Index < Type->sum_alternatives.size();
           ++Index) {
        auto Alternative =
            projectType(Type->sum_alternatives[Index], Scope, Range,
                        Path + "/alternative[" + std::to_string(Index) + "]");
        if (!Alternative)
          return Finish(std::unexpected(Alternative.error()));
        Alternatives.push_back(*Alternative);
      }
      auto Result = Types_.internSum(Alternatives);
      if (!Result)
        return Finish(
            std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                  Range, std::move(Path), Result.error())));
      return Finish(*Result);
    }
    }
    return Finish(std::unexpected(
        error(StructuralProjectionErrorCode::InvalidInput, Range,
              std::move(Path), "unknown inference type discriminant")));
  }

  ProjectionResult<model::EffectRowId> projectEffect(tc::EffectRef Effect,
                                                     ProjectionScope &Scope,
                                                     SourceRange Range,
                                                     std::string Path) {
    if (Owner_.Effects_.atomDomain() != Owner_.EffectAtoms_.domain())
      return std::unexpected(error(
          StructuralProjectionErrorCode::ForeignEffect, Range, std::move(Path),
          "effect solver and atom catalog domains differ"));
    if (!Owner_.Effects_.owns(Effect))
      return std::unexpected(error(StructuralProjectionErrorCode::ForeignEffect,
                                   Range, std::move(Path),
                                   std::string(InvalidEffectReferenceMessage)));
    if (const auto Found = Scope.EffectMemo.find(Effect);
        Found != Scope.EffectMemo.end())
      return Found->second;
    if (!Scope.ActiveEffects.insert(Effect).second)
      return std::unexpected(error(
          StructuralProjectionErrorCode::ForeignEffect, Range, std::move(Path),
          "effect projection contains a recursive argument"));

    tc::EffectNormalForm Normal;
    try {
      Normal = Owner_.Effects_.summarize(Effect);
    } catch (const std::invalid_argument &) {
      Scope.ActiveEffects.erase(Effect);
      return std::unexpected(error(StructuralProjectionErrorCode::ForeignEffect,
                                   Range, std::move(Path),
                                   std::string(InvalidEffectReferenceMessage)));
    }

    model::EffectRow Row;
    for (const auto RawAtom : Normal.known_atoms) {
      tc::EffectAtomId Atom = RawAtom;
      try {
        Atom = Owner_.Effects_.canonicalAtom(RawAtom);
      } catch (const std::invalid_argument &Failure) {
        Scope.ActiveEffects.erase(Effect);
        return std::unexpected(
            error(StructuralProjectionErrorCode::MissingEffectAtom, Range, Path,
                  Failure.what()));
      }
      if (!Owner_.EffectAtoms_.valid(Atom)) {
        Scope.ActiveEffects.erase(Effect);
        return std::unexpected(
            error(StructuralProjectionErrorCode::MissingEffectAtom, Range, Path,
                  "effect atom is absent from the checker catalog"));
      }
      const auto &Value = Owner_.EffectAtoms_.at(Atom);
      if (const auto *Operation =
              std::get_if<tc::OperationEffectAtom>(&Value)) {
        auto Application =
            projectApplication(*Operation, Scope, Range, Path + "/operation");
        if (!Application) {
          Scope.ActiveEffects.erase(Effect);
          return std::unexpected(Application.error());
        }
        Row.Operations.push_back(std::move(*Application));
      } else if (std::holds_alternative<tc::MayRaiseEffectAtom>(Value)) {
        Row.MayRaise = true;
      } else if (std::holds_alternative<tc::MayCancelEffectAtom>(Value)) {
        Row.MayCancel = true;
      }
    }
    for (std::size_t Index = 0; Index < Normal.tails.size(); ++Index) {
      const auto &Tail = Normal.tails[Index];
      auto Binder = lookupEffectBinder(Scope, Tail.variable);
      if (!Binder) {
        Scope.ActiveEffects.erase(Effect);
        return std::unexpected(
            error(StructuralProjectionErrorCode::UnboundBinder, Range,
                  Path + "/tail[" + std::to_string(Index) + "]",
                  "effect variable is not owned by this scope"));
      }
      const auto ExpectedKind = Tail.opaque
                                    ? model::EffectVariableKind::Opaque
                                    : model::EffectVariableKind::Flexible;
      if (Binder->Kind != ExpectedKind) {
        Scope.ActiveEffects.erase(Effect);
        return std::unexpected(
            error(StructuralProjectionErrorCode::BinderKindMismatch, Range,
                  Path + "/tail[" + std::to_string(Index) + "]",
                  "effect variable kind differs from its declaration"));
      }
      model::EffectTailProjection Projected{Binder->Variable, Binder->Kind, {}};
      for (const auto RawAtom : Tail.excluded_atoms) {
        tc::EffectAtomId Atom = RawAtom;
        try {
          Atom = Owner_.Effects_.canonicalAtom(RawAtom);
        } catch (const std::invalid_argument &Failure) {
          Scope.ActiveEffects.erase(Effect);
          return std::unexpected(
              error(StructuralProjectionErrorCode::MissingEffectAtom, Range,
                    Path, Failure.what()));
        }
        if (!Owner_.EffectAtoms_.valid(Atom) ||
            !std::holds_alternative<tc::OperationEffectAtom>(
                Owner_.EffectAtoms_.at(Atom))) {
          Scope.ActiveEffects.erase(Effect);
          return std::unexpected(
              error(StructuralProjectionErrorCode::MissingEffectAtom, Range,
                    Path + "/tail[" + std::to_string(Index) + "]",
                    "only cataloged operation atoms may be masked"));
        }
        auto Application = projectApplication(
            std::get<tc::OperationEffectAtom>(Owner_.EffectAtoms_.at(Atom)),
            Scope, Range, Path + "/tail[" + std::to_string(Index) + "]");
        if (!Application) {
          Scope.ActiveEffects.erase(Effect);
          return std::unexpected(Application.error());
        }
        Projected.ExcludedOperations.push_back(std::move(*Application));
      }
      Row.Tails.push_back(std::move(Projected));
    }
    auto Result = Types_.internEffect(std::move(Row));
    Scope.ActiveEffects.erase(Effect);
    if (!Result)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, std::move(Path), Result.error()));
    Scope.EffectMemo.emplace(Effect, *Result);
    return *Result;
  }

  ProjectionResult<ProjectedSemanticBatch>
  project(const tc::SemanticProjectionBatch &Batch) {
    SourceRange BatchRange = SourceRange::unknown();
    const auto rememberRange = [&](SourceRange Range) {
      if (!BatchRange.isValid() && Range.isValid())
        BatchRange = Range;
    };
    for (const auto &Scope : Batch.Scopes)
      rememberRange(Scope.Range);
    for (const auto &Function : Batch.Functions)
      rememberRange(Function.Range);
    for (const auto &Operation : Batch.Operations)
      rememberRange(Operation.Range);
    if (auto Valid = validateBatch(Batch); !Valid)
      return std::unexpected(Valid.error());
    if (auto Indexed = indexDeclarations(Batch); !Indexed)
      return std::unexpected(Indexed.error());
    if (auto Built = buildScopes(Batch); !Built)
      return std::unexpected(Built.error());
    if (auto Imported = projectImportedDeclarations(Batch); !Imported)
      return std::unexpected(Imported.error());

    for (std::size_t Index = 0; Index < Batch.Operations.size(); ++Index) {
      auto Result = projectOperation(Batch.Operations[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (std::size_t Index = 0; Index < Batch.Nominals.size(); ++Index) {
      auto Result = projectNominal(Batch.Nominals[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (std::size_t Index = 0; Index < Batch.Resources.size(); ++Index) {
      auto Result = projectResource(Batch.Resources[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (std::size_t Index = 0; Index < Batch.Traits.size(); ++Index) {
      auto Result = projectTrait(Batch.Traits[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (std::size_t Index = 0; Index < Batch.Instances.size(); ++Index) {
      auto Result = projectInstance(Batch.Instances[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (std::size_t Index = 0; Index < Batch.Functions.size(); ++Index) {
      auto Result = projectFunction(Batch.Functions[Index], Index);
      if (!Result)
        return std::unexpected(Result.error());
    }
    for (const auto &Derivation : Batch.Derivations) {
      auto Result = projectDerivation(Derivation, Batch.SourceExtent);
      if (!Result)
        return std::unexpected(Result.error());
    }

    for (const auto &Scope : ScopeStorage_) {
      const auto Input = ScopeInputs_.find(Scope->Id);
      const auto Range =
          Input == ScopeInputs_.end() ? BatchRange : Input->second->Range;
      if (auto Valid = validateCallableContractClosure(
              *Scope, Range,
              "scopes[" + std::to_string(Scope->Id) + "]/callables");
          !Valid)
        return std::unexpected(Valid.error());
    }

    auto Prepared = Types_.prepareCommit();
    if (!Prepared)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   BatchRange, "batch/prepare",
                                   Prepared.error()));
    if (auto Valid = Types_.validatePreparedCatalog(Declarations_); !Valid)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   BatchRange, "batch/declaration-catalog",
                                   Valid.error()));
    if (auto Valid = validateSumInjections(); !Valid)
      return std::unexpected(Valid.error());
    if (auto Valid = validateKeyOperations(); !Valid)
      return std::unexpected(Valid.error());
    if (auto Rewritten = rewriteOutput(BatchRange); !Rewritten)
      return std::unexpected(Rewritten.error());
    Owner_.Declarations_ = std::move(Declarations_);
    Types_.commitPrepared();
    return std::move(Output_);
  }

  ProjectionResult<model::TypeId> projectClosed(tc::MonoTypePtr Type,
                                                SourceRange Range) {
    ProjectionScope Scope;
    Scope.Transaction = this;
    auto Projected = projectType(Type, Scope, Range, "type", false);
    if (!Projected)
      return std::unexpected(Projected.error());
    auto Prepared = Types_.prepareCommit();
    if (!Prepared)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, "type", Prepared.error()));
    auto Resolved = Types_.resolved(*Projected);
    if (!Resolved)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, "type", Resolved.error()));
    Types_.commitPrepared();
    return *Resolved;
  }

private:
  ProjectionResult<void>
  validateBatch(const tc::SemanticProjectionBatch &Batch) {
    const auto SameRange = [](SourceRange Left, SourceRange Right) {
      return Left.Source == Right.Source && Left.Line == Right.Line &&
             Left.Column == Right.Column && Left.Offset == Right.Offset &&
             Left.Length == Right.Length;
    };
    const auto Contains = [](SourceRange Outer, SourceRange Inner) {
      return Outer.isValid() && Inner.isValid() &&
             Outer.Source == Inner.Source && Inner.Offset >= Outer.Offset &&
             Inner.Length <= Outer.Length &&
             Inner.Offset - Outer.Offset <= Outer.Length - Inner.Length;
    };
    if (Batch.SourceRoot) {
      if (!Batch.SourceExtent.isValid() ||
          !Contains(Batch.SourceExtent, Batch.SourceRoot->Range))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput,
                  Batch.SourceExtent, "source/extent",
                  "batch source extent must contain its AST root range"));
    } else if (Batch.SourceExtent.isValid()) {
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Batch.SourceExtent,
                "source/root", "a source extent requires an owning AST root"));
    }
    std::unordered_map<tc::ProjectionDeclarationId,
                       const tc::DeclarationScopeProjection *>
        InputScopes;
    for (std::size_t Index = 0; Index < Batch.Scopes.size(); ++Index) {
      const auto &Scope = Batch.Scopes[Index];
      if (Scope.Id == 0 || !InputScopes.emplace(Scope.Id, &Scope).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Scope.Range,
                  "scopes[" + std::to_string(Index) + "]",
                  "declaration scope IDs must be nonzero and unique"));
      if (Scope.Parent && *Scope.Parent == Scope.Id)
        return std::unexpected(
            error(StructuralProjectionErrorCode::CyclicParent, Scope.Range,
                  "scopes[" + std::to_string(Index) + "]/parent",
                  "a declaration scope cannot parent itself"));
      const bool ForbidsEffects =
          Scope.Kind == model::BinderOwnerKind::Nominal ||
          Scope.Kind == model::BinderOwnerKind::Trait ||
          Scope.Kind == model::BinderOwnerKind::Instance ||
          Scope.Kind == model::BinderOwnerKind::Resource;
      if (ForbidsEffects && !Scope.EffectVariables.empty())
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Scope.Range,
            "scopes[" + std::to_string(Index) + "]/effect-binders",
            "this declaration kind cannot own effect binders"));
      if (Scope.Kind == model::BinderOwnerKind::Operation &&
          std::ranges::any_of(Scope.EffectVariables, [](const auto &Variable) {
            return Variable.Kind != model::EffectVariableKind::Flexible;
          }))
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Scope.Range,
            "scopes[" + std::to_string(Index) + "]/effect-binders",
            "operation effect binders must be flexible"));
    }
    for (std::size_t Index = 0; Index < Batch.Scopes.size(); ++Index) {
      const auto &Scope = Batch.Scopes[Index];
      if (Scope.Parent && !InputScopes.contains(*Scope.Parent))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidParent, Scope.Range,
                  "scopes[" + std::to_string(Index) + "]/parent",
                  "declaration parent is absent from the batch"));
      std::unordered_set<tc::ProjectionDeclarationId> Seen;
      for (auto *Current = &Scope; Current && Current->Parent;) {
        if (!Seen.insert(Current->Id).second)
          return std::unexpected(
              error(StructuralProjectionErrorCode::CyclicParent, Scope.Range,
                    "scopes[" + std::to_string(Index) + "]/parent",
                    "declaration scope forest contains a cycle"));
        Current = InputScopes.at(*Current->Parent);
      }
    }

    const auto RequireScope = [&](tc::ProjectionDeclarationId Id,
                                  model::BinderOwnerKind Kind,
                                  SourceRange Range, std::string Path)
        -> ProjectionResult<const tc::DeclarationScopeProjection *> {
      const auto Found = InputScopes.find(Id);
      if (Found == InputScopes.end())
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidParent, Range,
                  std::move(Path), "declaration scope is absent"));
      if (Found->second->Kind != Kind)
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Range,
            std::move(Path), "declaration scope has the wrong owner kind"));
      return Found->second;
    };
    std::unordered_set<tc::ProjectionDeclarationId> FunctionScopes;
    std::unordered_set<tc::ProjectionDeclarationId> OperationScopes;
    std::unordered_set<tc::ProjectionDeclarationId> NominalScopes;
    std::unordered_set<tc::ProjectionDeclarationId> ResourceScopes;
    std::unordered_set<tc::ProjectionDeclarationId> TraitScopes;
    std::unordered_set<tc::ProjectionDeclarationId> InstanceScopes;
    std::unordered_set<model::EffectOperationKey,
                       model::EffectOperationKeyHasher>
        OperationKeys;
    std::unordered_set<model::NominalTypeKey, model::NominalTypeKeyHasher>
        NominalKeys;
    std::unordered_set<model::NominalTypeKey, model::NominalTypeKeyHasher>
        ResourceKeys;
    std::unordered_set<model::NominalTypeKey, model::NominalTypeKeyHasher>
        TraitKeys;

    for (std::size_t Index = 0; Index < Batch.Operations.size(); ++Index) {
      const auto &Operation = Batch.Operations[Index];
      const auto Path = "operations[" + std::to_string(Index) + "]";
      auto Scope =
          RequireScope(Operation.Scope, model::BinderOwnerKind::Operation,
                       Operation.Range, Path + "/scope");
      if (!Scope)
        return std::unexpected(Scope.error());
      const auto *Identity =
          std::get_if<model::EffectOperationKey>(&(*Scope)->Identity);
      if (!Identity || *Identity != Operation.Key ||
          !OperationScopes.insert(Operation.Scope).second ||
          !OperationKeys.insert(Operation.Key).second)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Operation.Range,
            Path + "/key",
            "operation key and scope must identify one unique declaration"));
      if (Operation.Key.Effect.Module != Owner_.Module_)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Operation.Range,
            Path + "/key",
            "a batch operation declaration must belong to its owning module"));
      if (!std::ranges::equal(Operation.TypeParameters,
                              (*Scope)->TypeParameters) ||
          !std::ranges::equal(Operation.EffectVariables,
                              (*Scope)->EffectVariables))
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArity, Operation.Range,
                  Path + "/binders",
                  "operation binders differ from its declaration scope"));
      if (!validVisibility(Operation.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Operation.Range,
                  Path + "/visibility",
                  "operation visibility has an unknown discriminant"));
    }
    for (std::size_t Index = 0; Index < Batch.Nominals.size(); ++Index) {
      const auto &Nominal = Batch.Nominals[Index];
      const auto Path = "nominals[" + std::to_string(Index) + "]";
      auto Scope = RequireScope(Nominal.Scope, model::BinderOwnerKind::Nominal,
                                Nominal.Range, Path + "/scope");
      if (!Scope)
        return std::unexpected(Scope.error());
      const auto *Identity =
          std::get_if<model::NominalTypeKey>(&(*Scope)->Identity);
      if (!Identity || *Identity != Nominal.Key ||
          !NominalScopes.insert(Nominal.Scope).second ||
          !NominalKeys.insert(Nominal.Key).second)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Nominal.Range,
            Path + "/key",
            "nominal key and scope must identify one unique declaration"));
      if (Nominal.Key.Module != Owner_.Module_)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Nominal.Range,
            Path + "/key",
            "a batch nominal declaration must belong to its owning module"));
      if (!validVisibility(Nominal.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Nominal.Range,
                  Path + "/visibility",
                  "nominal visibility has an unknown discriminant"));
    }
    for (std::size_t Index = 0; Index < Batch.Resources.size(); ++Index) {
      const auto &Resource = Batch.Resources[Index];
      const auto Path = "resources[" + std::to_string(Index) + "]";
      auto Scope =
          RequireScope(Resource.Scope, model::BinderOwnerKind::Resource,
                       Resource.Range, Path + "/scope");
      if (!Scope)
        return std::unexpected(Scope.error());
      const auto *Identity =
          std::get_if<model::NominalTypeKey>(&(*Scope)->Identity);
      if (!Identity || *Identity != Resource.Key ||
          !ResourceScopes.insert(Resource.Scope).second ||
          !ResourceKeys.insert(Resource.Key).second)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Resource.Range,
            Path + "/key",
            "resource key and scope must identify one unique declaration"));
      if (Resource.Key.Module != Owner_.Module_)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Resource.Range,
            Path + "/key",
            "a batch resource declaration must belong to its owning module"));
      if (!std::ranges::equal(Resource.TypeParameters,
                              (*Scope)->TypeParameters))
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArity, Resource.Range,
                  Path + "/binders",
                  "resource binders differ from its declaration scope"));
      if (!validVisibility(Resource.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Resource.Range,
                  Path + "/visibility",
                  "resource visibility has an unknown discriminant"));
    }
    for (std::size_t Index = 0; Index < Batch.Traits.size(); ++Index) {
      const auto &Trait = Batch.Traits[Index];
      const auto Path = "traits[" + std::to_string(Index) + "]";
      auto Scope = RequireScope(Trait.Scope, model::BinderOwnerKind::Trait,
                                Trait.Range, Path + "/scope");
      if (!Scope)
        return std::unexpected(Scope.error());
      const auto *Identity =
          std::get_if<model::NominalTypeKey>(&(*Scope)->Identity);
      if (!Identity || *Identity != Trait.Key ||
          !TraitScopes.insert(Trait.Scope).second ||
          !TraitKeys.insert(Trait.Key).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Trait.Range,
                  Path + "/key",
                  "trait key and scope must identify one unique declaration"));
      if (Trait.Key.Module != Owner_.Module_)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Trait.Range,
            Path + "/key",
            "a batch trait declaration must belong to its owning module"));
      if (!validVisibility(Trait.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Trait.Range,
                  Path + "/visibility",
                  "trait visibility has an unknown discriminant"));
    }
    for (std::size_t Index = 0; Index < Batch.Instances.size(); ++Index) {
      const auto &Instance = Batch.Instances[Index];
      const auto Path = "instances[" + std::to_string(Index) + "]";
      auto Scope =
          RequireScope(Instance.Scope, model::BinderOwnerKind::Instance,
                       Instance.Range, Path + "/scope");
      if (!Scope)
        return std::unexpected(Scope.error());
      const auto *Identity =
          std::get_if<tc::InstanceScopeIdentityProjection>(&(*Scope)->Identity);
      if (!Identity || Identity->Trait != Instance.Trait ||
          !InstanceScopes.insert(Instance.Scope).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Instance.Range,
                  Path + "/trait",
                  "instance scope must identify one matching declaration"));
      if (!validVisibility(Instance.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Instance.Range,
                  Path + "/visibility",
                  "instance visibility has an unknown discriminant"));
    }

    std::unordered_set<const ast::AstNode *> Nodes;
    for (std::size_t FunctionIndex = 0; FunctionIndex < Batch.Functions.size();
         ++FunctionIndex) {
      const auto &Function = Batch.Functions[FunctionIndex];
      if (!FunctionScopes.insert(Function.Scope).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Function.Range,
                  "functions[" + std::to_string(FunctionIndex) + "]/scope",
                  "function scope is used by more than one declaration"));
      auto FunctionScope = RequireScope(
          Function.Scope, model::BinderOwnerKind::Function, Function.Range,
          "functions[" + std::to_string(FunctionIndex) + "]/scope");
      if (!FunctionScope)
        return std::unexpected(FunctionScope.error());
      const auto *Identity = std::get_if<model::FunctionDeclarationIdentity>(
          &(*FunctionScope)->Identity);
      if (!Identity ||
          (Function.SourceIdentity && *Function.SourceIdentity != *Identity))
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Function.Range,
            "functions[" + std::to_string(FunctionIndex) + "]/identity",
            "function identity differs from its declaration scope"));
      if (Identity &&
          std::visit(
              [&](const auto &Value) { return Value.Module != Owner_.Module_; },
              *Identity))
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Function.Range,
            "functions[" + std::to_string(FunctionIndex) + "]/identity",
            "a batch function declaration must belong to its owning module"));
      if (!validVisibility(Function.Access))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Function.Range,
                  "functions[" + std::to_string(FunctionIndex) + "]/visibility",
                  "function visibility has an unknown discriminant"));
      if (Batch.SourceRoot) {
        bool OwnerInSource = false;
        for (auto *Current = Function.SourceOwner; Current;
             Current = Current->parent)
          if (Current == Batch.SourceRoot) {
            OwnerInSource = true;
            break;
          }
        if (!Function.SourceOwner || !OwnerInSource ||
            !SameRange(Function.Range, Function.SourceOwner->Range) ||
            !Contains(Batch.SourceExtent, Function.Range))
          return std::unexpected(error(
              StructuralProjectionErrorCode::InvalidInput, Function.Range,
              "functions[" + std::to_string(FunctionIndex) + "]/owner",
              "function source owner and range must name one declaration in "
              "the batch source"));
      }
      for (std::size_t NodeIndex = 0; NodeIndex < Function.NodeFacts.size();
           ++NodeIndex) {
        const auto &Fact = Function.NodeFacts[NodeIndex];
        if (!Fact.Node || !Nodes.insert(Fact.Node).second)
          return std::unexpected(error(
              StructuralProjectionErrorCode::DuplicateNode, Fact.Range,
              "functions[" + std::to_string(FunctionIndex) + "]/nodes[" +
                  std::to_string(NodeIndex) + "]",
              "every semantic node must have exactly one owning function"));
        if (Batch.SourceRoot) {
          bool InSource = false;
          bool InFunction = false;
          for (auto *Current = Fact.Node; Current; Current = Current->parent) {
            if (Current == Batch.SourceRoot)
              InSource = true;
            if (Current == Function.SourceOwner)
              InFunction = true;
          }
          const ast::AstNode *NearestFunction = nullptr;
          for (auto *Current = Fact.Node; Current; Current = Current->parent)
            if (dynamic_cast<const ast::FunctionExpr *>(Current)) {
              NearestFunction = Current;
              break;
            }
          const bool ExactFunctionOwner =
              dynamic_cast<const ast::FunctionExpr *>(Function.SourceOwner)
                  ? NearestFunction == Function.SourceOwner
                  : Function.SourceOwner == Batch.SourceRoot;
          const auto Path = "functions[" + std::to_string(FunctionIndex) +
                            "]/nodes[" + std::to_string(NodeIndex) + "]";
          if (!Function.SourceOwner || !InSource || !InFunction ||
              !ExactFunctionOwner)
            return std::unexpected(
                error(StructuralProjectionErrorCode::DuplicateNode, Fact.Range,
                      Path + "/owner",
                      "semantic node is outside its declared function/source"));
          if (!SameRange(Fact.Range, Fact.Node->Range) ||
              !Contains(Batch.SourceExtent, Fact.Range))
            return std::unexpected(error(
                StructuralProjectionErrorCode::InvalidInput, Fact.Range,
                Path + "/range",
                "semantic node range differs from its AST/source extent"));
        }
      }
    }

    for (std::size_t Index = 0; Index < Batch.ImportedDeclarations.size();
         ++Index) {
      const auto &Import = Batch.ImportedDeclarations[Index];
      const auto Path = "imports[" + std::to_string(Index) + "]/identity";
      const bool NominalIdentity =
          std::holds_alternative<model::NominalTypeKey>(Import.Identity);
      const bool FunctionIdentity =
          std::holds_alternative<model::FunctionDeclarationIdentity>(
              Import.Identity);
      const bool OperationIdentity =
          std::holds_alternative<model::EffectOperationKey>(Import.Identity);
      const bool ValidIdentity =
          ((Import.Kind == model::BinderOwnerKind::Nominal ||
            Import.Kind == model::BinderOwnerKind::Resource ||
            Import.Kind == model::BinderOwnerKind::Trait) &&
           NominalIdentity) ||
          (Import.Kind == model::BinderOwnerKind::Function &&
           FunctionIdentity) ||
          (Import.Kind == model::BinderOwnerKind::Operation &&
           OperationIdentity);
      if (!ValidIdentity || Import.Kind == model::BinderOwnerKind::Instance)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Import.Range,
            Path, "imported declaration kind and qualified identity disagree"));
      if ((Import.Function &&
           Import.Kind != model::BinderOwnerKind::Function) ||
          (Import.Operation &&
           Import.Kind != model::BinderOwnerKind::Operation) ||
          (Import.Resource && Import.Kind != model::BinderOwnerKind::Resource))
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Import.Range, Path,
            "imported declaration contract does not match its owner kind"));
      const bool IsLocal = std::visit(
          [&](const auto &Identity) {
            using IdentityType = std::decay_t<decltype(Identity)>;
            if constexpr (std::is_same_v<IdentityType, model::NominalTypeKey>)
              return Identity.Module == Owner_.Module_;
            else if constexpr (std::is_same_v<
                                   IdentityType,
                                   model::FunctionDeclarationIdentity>)
              return std::visit(
                  [&](const auto &Function) {
                    return Function.Module == Owner_.Module_;
                  },
                  Identity);
            else if constexpr (std::is_same_v<IdentityType,
                                              model::EffectOperationKey>)
              return Identity.Effect.Module == Owner_.Module_;
            else
              return false;
          },
          Import.Identity);
      if (IsLocal)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Import.Range,
            Path,
            "an imported declaration must not belong to the batch module"));
      if ((Import.Kind == model::BinderOwnerKind::Nominal ||
           Import.Kind == model::BinderOwnerKind::Resource ||
           Import.Kind == model::BinderOwnerKind::Trait) &&
          Import.EffectArity != 0)
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArity, Import.Range,
                  "imports[" + std::to_string(Index) + "]/effect-arity",
                  "this imported declaration kind cannot own effect binders"));
      if (Import.Kind != model::BinderOwnerKind::Trait &&
          !Import.TraitMethods.empty())
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Import.Range,
                  "imports[" + std::to_string(Index) + "]/methods",
                  "only an imported trait can declare trait methods"));
      std::unordered_set<std::string> Methods;
      for (const auto &Method : Import.TraitMethods)
        if (Method.empty() || !Methods.insert(Method).second)
          return std::unexpected(
              error(StructuralProjectionErrorCode::InvalidInput, Import.Range,
                    "imports[" + std::to_string(Index) + "]/methods",
                    "imported trait method names must be nonempty and unique"));
    }
    return {};
  }

  ProjectionResult<void> buildScopes(const tc::SemanticProjectionBatch &Batch) {
    std::unordered_set<tc::ProjectionDeclarationId> Remaining;
    for (const auto &Scope : Batch.Scopes)
      Remaining.insert(Scope.Id);

    while (!Remaining.empty()) {
      bool Progress = false;
      for (const auto &Input : Batch.Scopes) {
        if (!Remaining.contains(Input.Id))
          continue;
        if (Input.Parent && !ScopeById_.contains(*Input.Parent)) {
          continue;
        }
        auto Scope = std::make_unique<ProjectionScope>();
        Scope->Id = Input.Id;
        Scope->Parent = Input.Parent ? ScopeById_.at(*Input.Parent) : nullptr;
        Scope->Kind = Input.Kind;
        Scope->Transaction = this;

        std::unordered_set<tc::TypeId> LocalTypes;
        for (const auto Raw : Input.TypeParameters) {
          if (!LocalTypes.insert(Raw).second || Scope->typeBinder(Raw))
            return std::unexpected(
                error(StructuralProjectionErrorCode::UnboundBinder, Input.Range,
                      "scopes[" + std::to_string(Input.Id) + "]/type-binders",
                      "type binder is duplicated in its effective scope"));
          const auto Destination = Types_.allocateTypeParameter();
          if (!Destination.isValid())
            return std::unexpected(
                error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                      "scopes[" + std::to_string(Input.Id) + "]/type-binders",
                      "unable to allocate a type binder"));
          Scope->DeclaredTypes.push_back(Destination);
          Scope->TypeBinders.emplace(Raw, Destination);
        }
        std::unordered_set<tc::EffectVarId> LocalEffects;
        for (const auto &Raw : Input.EffectVariables) {
          if (!LocalEffects.insert(Raw.Variable).second ||
              lookupEffectBinder(*Scope, Raw.Variable))
            return std::unexpected(
                error(StructuralProjectionErrorCode::UnboundBinder, Input.Range,
                      "scopes[" + std::to_string(Input.Id) + "]/effect-binders",
                      "effect binder is duplicated in its effective scope"));
          const auto Destination = Types_.allocateEffectVariable();
          if (!Destination.isValid())
            return std::unexpected(
                error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                      "scopes[" + std::to_string(Input.Id) + "]/effect-binders",
                      "unable to allocate an effect binder"));
          Scope->DeclaredEffects.push_back({Destination, Raw.Kind});
          Scope->EffectBinders.emplace(Raw.Variable, Destination);
          EffectKinds_.emplace(std::pair{Scope.get(), Raw.Variable}, Raw.Kind);
        }
        for (const auto &Contract : Input.CallableContracts) {
          if (!Contract.Arrow || Contract.Arrow->tag != tc::MonoType::Arrow)
            return std::unexpected(error(
                StructuralProjectionErrorCode::WrongArrowShape, Contract.Range,
                "scopes[" + std::to_string(Input.Id) + "]/callables",
                "callable contract must name an Arrow"));
          if (Scope->callable(Contract.Arrow) ||
              !Scope->CallableContracts.emplace(Contract.Arrow, &Contract)
                   .second)
            return std::unexpected(
                error(StructuralProjectionErrorCode::DuplicateCallableContract,
                      Contract.Range,
                      "scopes[" + std::to_string(Input.Id) + "]/callables",
                      "an Arrow has more than one contract in this scope"));
        }
        ProjectionScope *RawScope = Scope.get();
        ScopeStorage_.push_back(std::move(Scope));
        ScopeById_.emplace(Input.Id, RawScope);

        auto Identity = projectScopeIdentity(Input, *RawScope);
        if (!Identity)
          return std::unexpected(Identity.error());
        if (std::ranges::any_of(Output_.InterfaceSeed.Scopes,
                                [&](const ProjectedDeclarationScope &Existing) {
                                  return Existing.Kind == Input.Kind &&
                                         Existing.Identity == *Identity;
                                }))
          return std::unexpected(error(
              StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
              "scopes[" + std::to_string(Input.Id) + "]/identity",
              "canonical declaration owner identity is duplicated"));
        RawScope->Identity = *Identity;
        Output_.InterfaceSeed.Scopes.push_back(ProjectedDeclarationScope{
            Input.Kind, std::move(*Identity),
            Input.Parent ? std::optional<std::uint32_t>(
                               ScopeOutputIndex_.at(*Input.Parent))
                         : std::nullopt,
            RawScope->DeclaredTypes, RawScope->DeclaredEffects});
        const auto OutputIndex =
            static_cast<std::uint32_t>(Output_.InterfaceSeed.Scopes.size() - 1);
        ScopeOutputIndex_.emplace(Input.Id, OutputIndex);
        ScopeInputs_.emplace(Input.Id, &Input);

        Remaining.erase(Input.Id);
        Progress = true;
      }
      if (!Progress)
        return std::unexpected(
            error(StructuralProjectionErrorCode::CyclicParent,
                  SourceRange::unknown(), "scopes",
                  "declaration scope forest cannot be topologically ordered"));
    }

    return {};
  }

  ProjectionResult<model::CanonicalBinderOwnerIdentity>
  projectScopeIdentity(const tc::DeclarationScopeProjection &Input,
                       ProjectionScope &Scope) {
    switch (Input.Kind) {
    case model::BinderOwnerKind::Nominal:
    case model::BinderOwnerKind::Trait:
    case model::BinderOwnerKind::Resource: {
      const auto *Key = std::get_if<model::NominalTypeKey>(&Input.Identity);
      if (!Key)
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Input.Range,
            "scopes[" + std::to_string(Input.Id) + "]/identity",
            "declaration owner identity does not match its kind"));
      return *Key;
    }
    case model::BinderOwnerKind::Function: {
      const auto *Identity =
          std::get_if<model::FunctionDeclarationIdentity>(&Input.Identity);
      if (!Identity)
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Input.Range,
            "scopes[" + std::to_string(Input.Id) + "]/identity",
            "function scope requires a function identity"));
      return *Identity;
    }
    case model::BinderOwnerKind::Operation: {
      const auto *Key = std::get_if<model::EffectOperationKey>(&Input.Identity);
      if (!Key)
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Input.Range,
            "scopes[" + std::to_string(Input.Id) + "]/identity",
            "operation scope requires an operation identity"));
      return *Key;
    }
    case model::BinderOwnerKind::Instance: {
      const auto *Instance =
          std::get_if<tc::InstanceScopeIdentityProjection>(&Input.Identity);
      if (!Instance)
        return std::unexpected(error(
            StructuralProjectionErrorCode::BinderKindMismatch, Input.Range,
            "scopes[" + std::to_string(Input.Id) + "]/identity",
            "instance scope requires an instance identity"));
      model::InstanceBinderOwner Result{Instance->Trait, {}, {}};
      for (std::size_t Index = 0; Index < Instance->Arguments.size(); ++Index) {
        auto Argument = projectType(
            Instance->Arguments[Index], Scope, Input.Range,
            "scopes[" + std::to_string(Input.Id) + "]/identity/argument[" +
                std::to_string(Index) + "]");
        if (!Argument)
          return std::unexpected(Argument.error());
        Result.Arguments.push_back(*Argument);
      }
      auto Constraints = projectConstraints(
          Instance->Constraints, Scope, Input.Range,
          "scopes[" + std::to_string(Input.Id) + "]/identity/constraints");
      if (!Constraints)
        return std::unexpected(Constraints.error());
      Result.Constraints = std::move(*Constraints);
      return Result;
    }
    }
    return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                 Input.Range, "scope/identity",
                                 "unknown binder owner kind"));
  }

  ProjectionResult<model::TypeId> projectApplication(tc::MonoTypePtr Type,
                                                     ProjectionScope &Scope,
                                                     SourceRange Range,
                                                     std::string Path) {
    std::vector<model::TypeId> Arguments;
    Arguments.reserve(Type->args.size());
    for (std::size_t Index = 0; Index < Type->args.size(); ++Index) {
      auto Argument =
          projectType(Type->args[Index], Scope, Range,
                      Path + "/argument[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Arguments.push_back(*Argument);
    }
    auto Intern =
        [&](model::StructuralType Value) -> ProjectionResult<model::TypeId> {
      auto Result = Types_.intern(std::move(Value));
      if (!Result)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range, Path,
                  Result.error()));
      return *Result;
    };

    const auto Name = Type->type_name;
    const auto IsBuiltin = !Type->resolved_key;
    if (IsBuiltin && Name == "Seq") {
      if (Arguments.size() != 1)
        return wrongApplicationArity(Range, Path, Name, 1, Arguments.size());
      return Intern(model::SequenceType{Arguments[0]});
    }
    if (IsBuiltin && Name == "Set") {
      if (Arguments.size() != 1)
        return wrongApplicationArity(Range, Path, Name, 1, Arguments.size());
      return Intern(model::SetType{Arguments[0]});
    }
    if (IsBuiltin && Name == "Dict") {
      if (Arguments.size() != 2)
        return wrongApplicationArity(Range, Path, Name, 2, Arguments.size());
      return Intern(model::DictionaryType{Arguments[0], Arguments[1]});
    }
    if (IsBuiltin && Name == "Promise") {
      if (Arguments.size() != 1)
        return wrongApplicationArity(Range, Path, Name, 1, Arguments.size());
      if (!Type->promise_result_contract || !Type->promise_effects.valid() ||
          Type->promise_effect_solver != &Owner_.Effects_)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Range, std::move(Path),
            "Promise projection requires an explicit result and await-row "
            "contract"));
      auto Effects = projectEffect(Type->promise_effects, Scope, Range,
                                   Path + "/await-effects");
      if (!Effects)
        return std::unexpected(Effects.error());
      return Intern(model::PromiseType{
          Arguments.front(), *Type->promise_result_contract, *Effects});
    }
    if (IsBuiltin &&
        (Name == "ByteArray" || Name == "IntArray" || Name == "FloatArray")) {
      if (!Arguments.empty())
        return wrongApplicationArity(Range, Path, Name, 0, Arguments.size());
      const auto ElementType = Name == "ByteArray" ? model::PrimitiveType::Byte
                               : Name == "IntArray"
                                   ? model::PrimitiveType::Int
                                   : model::PrimitiveType::Float;
      auto Element = Types_.intern(ElementType);
      if (!Element)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range, Path,
                  Element.error()));
      return Intern(model::ArrayType{*Element, true});
    }
    if (IsBuiltin && Name == "Channel") {
      if (Arguments.size() != 1)
        return wrongApplicationArity(Range, Path, Name, 1, Arguments.size());
      return Intern(model::ChannelType{Arguments.front()});
    }
    if (!Type->resolved_key)
      return std::unexpected(error(
          StructuralProjectionErrorCode::UnresolvedIdentity, Range,
          std::move(Path), "named type has no qualified declaration identity"));
    if (const auto Found = NominalArities_.find(*Type->resolved_key);
        Found != NominalArities_.end()) {
      if (Found->second != Arguments.size())
        return wrongApplicationArity(Range, Path, Type->resolved_key->Name,
                                     Found->second, Arguments.size());
      return Intern(
          model::NominalType{*Type->resolved_key, std::move(Arguments)});
    }
    if (const auto Found = ResourceArities_.find(*Type->resolved_key);
        Found != ResourceArities_.end()) {
      if (Found->second != Arguments.size())
        return wrongApplicationArity(Range, Path, Type->resolved_key->Name,
                                     Found->second, Arguments.size());
      return Intern(
          model::ResourceType{*Type->resolved_key, std::move(Arguments)});
    }
    return std::unexpected(error(
        StructuralProjectionErrorCode::UnresolvedIdentity, Range,
        std::move(Path), "named type is absent from the declaration catalog"));
  }

  ProjectionResult<model::TypeId> wrongApplicationArity(SourceRange Range,
                                                        const std::string &Path,
                                                        const std::string &Name,
                                                        std::size_t Expected,
                                                        std::size_t Actual) {
    return std::unexpected(
        error(StructuralProjectionErrorCode::WrongArity, Range, Path,
              "type '" + Name + "' expects " + std::to_string(Expected) +
                  " argument(s), got " + std::to_string(Actual)));
  }

  ProjectionResult<model::EffectOperationApplication>
  projectApplication(const tc::OperationEffectAtom &Operation,
                     ProjectionScope &Scope, SourceRange Range,
                     std::string Path) {
    const auto Found = OperationArities_.find(Operation.Key);
    if (Found == OperationArities_.end())
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Range,
                std::move(Path),
                "effect operation is absent from the declaration catalog"));
    if (Found->second.first != Operation.TypeArguments.size() ||
        Found->second.second != Operation.EffectArguments.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Range, std::move(Path),
          "effect operation application has the wrong generic arity"));
    model::EffectOperationApplication Result{Operation.Key, {}, {}};
    for (std::size_t Index = 0; Index < Operation.TypeArguments.size();
         ++Index) {
      auto Argument =
          projectType(Operation.TypeArguments[Index], Scope, Range,
                      Path + "/type-argument[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.TypeArguments.push_back(*Argument);
    }
    for (std::size_t Index = 0; Index < Operation.EffectArguments.size();
         ++Index) {
      auto Argument = projectEffect(
          Operation.EffectArguments[Index], Scope, Range,
          Path + "/effect-argument[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.EffectArguments.push_back(*Argument);
    }
    return Result;
  }

  std::optional<model::EffectVariableDeclaration>
  lookupEffectBinder(const ProjectionScope &Scope,
                     tc::EffectVarId Source) const {
    for (auto *Current = &Scope; Current; Current = Current->Parent) {
      const auto Found = Current->EffectBinders.find(Source);
      if (Found == Current->EffectBinders.end())
        continue;
      const auto Kind = EffectKinds_.find(std::pair{Current, Source});
      if (Kind == EffectKinds_.end())
        return std::nullopt;
      return model::EffectVariableDeclaration{Found->second, Kind->second};
    }
    return std::nullopt;
  }

  struct ScopeEffectKeyHasher {
    std::size_t
    operator()(const std::pair<const ProjectionScope *, tc::EffectVarId> &Value)
        const noexcept {
      auto Seed = std::hash<const ProjectionScope *>{}(Value.first);
      Seed ^= std::hash<tc::EffectVarId>{}(Value.second) +
              static_cast<std::size_t>(0x9e3779b9u) + (Seed << 6u) +
              (Seed >> 2u);
      return Seed;
    }
  };

  ProjectionResult<std::vector<model::TypeConstraint>>
  projectConstraints(std::span<const tc::Constraint> Constraints,
                     ProjectionScope &Scope, SourceRange Range,
                     std::string Path) {
    std::vector<model::TypeConstraint> Result;
    Result.reserve(Constraints.size());
    for (std::size_t Index = 0; Index < Constraints.size(); ++Index) {
      const auto &Constraint = Constraints[Index];
      if (!Constraint.resolved_trait || Constraint.types.empty() ||
          !Constraint.types.front() ||
          Constraint.types.front()->tag != tc::MonoType::Var)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Range,
            Path + "[" + std::to_string(Index) + "]",
            "trait constraint requires a qualified trait and owning binder"));
      const auto Trait = TraitArities_.find(*Constraint.resolved_trait);
      if (Trait == TraitArities_.end())
        return std::unexpected(
            error(StructuralProjectionErrorCode::UnresolvedIdentity, Range,
                  Path + "[" + std::to_string(Index) + "]/trait",
                  "trait constraint is absent from the declaration catalog"));
      if (Trait->second != Constraint.types.size())
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArity, Range,
                  Path + "[" + std::to_string(Index) + "]/trait",
                  "trait constraint has the wrong declaration arity"));
      const auto Parameter = Scope.typeBinder(Constraint.types.front()->var_id);
      if (!Parameter)
        return std::unexpected(
            error(StructuralProjectionErrorCode::UnboundBinder, Range,
                  Path + "[" + std::to_string(Index) + "]/parameter",
                  "trait constraint parameter is not owned by this scope"));
      model::TypeConstraint Projected{
          *Parameter, *Constraint.resolved_trait, {}};
      for (std::size_t ArgumentIndex = 1;
           ArgumentIndex < Constraint.types.size(); ++ArgumentIndex) {
        auto Argument =
            projectType(Constraint.types[ArgumentIndex], Scope, Range,
                        Path + "[" + std::to_string(Index) + "]/argument[" +
                            std::to_string(ArgumentIndex - 1) + "]");
        if (!Argument)
          return std::unexpected(Argument.error());
        Projected.Arguments.push_back(*Argument);
      }
      Result.push_back(std::move(Projected));
    }
    return Result;
  }

  ProjectionResult<model::TypeId> projectCallable(
      tc::MonoTypePtr Arrow, const tc::CallableTypeContract &Contract,
      ProjectionScope &Scope, SourceRange Range, std::string Path,
      std::span<const model::TypeParameterId> TypeParameters,
      std::span<const model::EffectVariableDeclaration> EffectVariables,
      std::span<const tc::Constraint> Constraints,
      std::optional<model::FunctionDeclarationIdentity> SourceIdentity,
      std::optional<model::SymbolIdentity> ExportedSymbol) {
    if (Contract.Arrow != Arrow)
      return std::unexpected(
          error(StructuralProjectionErrorCode::MissingCallableContract, Range,
                std::move(Path), "callable contract names a different Arrow"));
    if (Contract.ParameterOwnerships.empty())
      return std::unexpected(
          error(StructuralProjectionErrorCode::WrongArity, Contract.Range,
                std::move(Path),
                "an Arrow contract must consume at least one arrow"));

    model::FunctionType Function;
    Function.TypeParameters.assign(TypeParameters.begin(),
                                   TypeParameters.end());
    Function.EffectVariables.assign(EffectVariables.begin(),
                                    EffectVariables.end());
    auto ProjectedConstraints =
        projectConstraints(Constraints, Scope, Range, Path + "/constraints");
    if (!ProjectedConstraints)
      return std::unexpected(ProjectedConstraints.error());
    Function.Constraints = std::move(*ProjectedConstraints);

    auto *Stage = Arrow;
    for (std::size_t Index = 0; Index < Contract.ParameterOwnerships.size();
         ++Index) {
      if (!Stage || Stage->tag != tc::MonoType::Arrow)
        return std::unexpected(error(
            StructuralProjectionErrorCode::WrongArity, Contract.Range,
            Path + "/parameter[" + std::to_string(Index) + "]",
            "callable contract has more parameters than the Arrow chain"));
      if (Stage->effect_solver != &Owner_.Effects_ ||
          Stage->effect_atoms != &Owner_.EffectAtoms_)
        return std::unexpected(
            error(StructuralProjectionErrorCode::ForeignEffect, Contract.Range,
                  Path + "/parameter[" + std::to_string(Index) + "]",
                  "Arrow carries an effect graph from another checker"));
      if (Index + 1 != Contract.ParameterOwnerships.size()) {
        tc::EffectNormalForm Intermediate;
        try {
          Intermediate = Owner_.Effects_.summarize(Stage->arrow_effect);
        } catch (const std::invalid_argument &) {
          return std::unexpected(error(
              StructuralProjectionErrorCode::ForeignEffect, Contract.Range,
              Path + "/parameter[" + std::to_string(Index) + "]",
              std::string(InvalidEffectReferenceMessage)));
        }
        if (!Intermediate.empty())
          return std::unexpected(error(
              StructuralProjectionErrorCode::WrongArrowShape, Contract.Range,
              Path + "/parameter[" + std::to_string(Index) + "]",
              "intermediate partial-application Arrow must be effect-free"));
      }
      auto Parameter =
          projectType(Stage->param_type, Scope, Contract.Range,
                      Path + "/parameter[" + std::to_string(Index) + "]");
      if (!Parameter)
        return std::unexpected(Parameter.error());
      Function.Parameters.push_back(
          {*Parameter, Contract.ParameterOwnerships[Index]});
      if (Index + 1 == Contract.ParameterOwnerships.size()) {
        auto Effects = projectEffect(Stage->arrow_effect, Scope, Contract.Range,
                                     Path + "/effects");
        if (!Effects)
          return std::unexpected(Effects.error());
        Function.Effects = *Effects;
      }
      Stage = Stage->return_type;
    }
    auto Result = projectType(Stage, Scope, Contract.Range, Path + "/result");
    if (!Result)
      return std::unexpected(Result.error());
    Function.Result = *Result;
    Function.ResultContract = Contract.ResultContract;
    Function.Convention = Contract.Convention;
    Function.SourceIdentity = std::move(SourceIdentity);
    Function.ExportedSymbol = std::move(ExportedSymbol);
    auto Interned = Types_.intern(std::move(Function));
    if (!Interned)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, std::move(Path), Interned.error()));
    return *Interned;
  }

  ProjectionResult<model::TypeId> projectFunctionSignature(
      const tc::TypeScheme &Scheme,
      std::span<const model::ParameterOwnership> Ownerships,
      model::ResultOwnership ResultContract,
      model::CallingConvention Convention,
      std::optional<model::FunctionDeclarationIdentity> SourceIdentity,
      std::optional<model::SymbolIdentity> ExportedSymbol,
      ProjectionScope &Scope, SourceRange Range, std::string Path) {
    if (!Scheme.body)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, std::move(Path),
                                   "function scheme has no body"));
    if (auto Roots =
            validateSchemeEffectRoots(Scheme, Range, Path + "/effect-roots");
        !Roots)
      return std::unexpected(Roots.error());
    if (Scheme.quantified_vars.size() != Scope.DeclaredTypes.size() ||
        Scheme.quantified_effect_variables.size() !=
            Scope.DeclaredEffects.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Range, std::move(Path),
          "function scheme binders differ from its declaration scope"));

    const auto *Contract = Scope.callable(Scheme.body);
    if (Scheme.body->tag == tc::MonoType::Arrow) {
      if (!Contract)
        return std::unexpected(
            error(StructuralProjectionErrorCode::MissingCallableContract, Range,
                  std::move(Path), "root Arrow has no callable contract"));
      if (Contract->ParameterOwnerships.size() != Ownerships.size() ||
          !std::ranges::equal(Contract->ParameterOwnerships, Ownerships) ||
          Contract->ResultContract != ResultContract ||
          Contract->Convention != Convention)
        return std::unexpected(error(
            StructuralProjectionErrorCode::DuplicateCallableContract, Range,
            std::move(Path), "function header and callable contract disagree"));
      return projectCallable(
          Scheme.body, *Contract, Scope, Range, std::move(Path),
          Scope.DeclaredTypes, Scope.DeclaredEffects, Scheme.constraints,
          std::move(SourceIdentity), std::move(ExportedSymbol));
    }
    if (!Ownerships.empty())
      return std::unexpected(
          error(StructuralProjectionErrorCode::WrongArrowShape, Range,
                std::move(Path), "non-Arrow function scheme has parameters"));
    auto ResultType = projectType(Scheme.body, Scope, Range, Path + "/result");
    if (!ResultType)
      return std::unexpected(ResultType.error());
    auto EmptyEffects =
        projectEffect(Owner_.Effects_.empty(), Scope, Range, Path + "/effects");
    if (!EmptyEffects)
      return std::unexpected(EmptyEffects.error());
    auto Constraints = projectConstraints(Scheme.constraints, Scope, Range,
                                          Path + "/constraints");
    if (!Constraints)
      return std::unexpected(Constraints.error());
    model::FunctionType Function{Scope.DeclaredTypes,
                                 Scope.DeclaredEffects,
                                 std::move(*Constraints),
                                 {},
                                 *ResultType,
                                 ResultContract,
                                 *EmptyEffects,
                                 Convention,
                                 std::move(SourceIdentity),
                                 std::move(ExportedSymbol)};
    auto Interned = Types_.intern(std::move(Function));
    if (!Interned)
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                   Range, std::move(Path), Interned.error()));
    return *Interned;
  }

  ProjectionResult<std::unique_ptr<ProjectionScope>> makeImportedScope(
      model::BinderOwnerKind Kind, std::span<const tc::TypeId> TypeParameters,
      std::span<const tc::QuantifiedEffectVariable> EffectVariables,
      std::span<const tc::CallableTypeContract> CallableContracts,
      SourceRange Range, const std::string &Path) {
    auto Scope = std::make_unique<ProjectionScope>();
    Scope->Kind = Kind;
    Scope->Transaction = this;
    std::unordered_set<tc::TypeId> SeenTypes;
    for (const auto Source : TypeParameters) {
      if (!SeenTypes.insert(Source).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::UnboundBinder, Range,
                  Path + "/type-binders",
                  "imported declaration contains a duplicate type binder"));
      const auto Destination = Types_.allocateTypeParameter();
      if (!Destination.isValid())
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range,
                  Path + "/type-binders", "unable to allocate a type binder"));
      Scope->DeclaredTypes.push_back(Destination);
      Scope->TypeBinders.emplace(Source, Destination);
    }
    std::unordered_set<tc::EffectVarId> SeenEffects;
    for (const auto &Source : EffectVariables) {
      if (Kind == model::BinderOwnerKind::Operation &&
          Source.Kind != model::EffectVariableKind::Flexible)
        return std::unexpected(
            error(StructuralProjectionErrorCode::BinderKindMismatch, Range,
                  Path + "/effect-binders",
                  "operation effect binders must be flexible"));
      if (!SeenEffects.insert(Source.Variable).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::UnboundBinder, Range,
                  Path + "/effect-binders",
                  "imported declaration contains a duplicate effect binder"));
      const auto Destination = Types_.allocateEffectVariable();
      if (!Destination.isValid())
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Range,
            Path + "/effect-binders", "unable to allocate an effect binder"));
      Scope->DeclaredEffects.push_back({Destination, Source.Kind});
      Scope->EffectBinders.emplace(Source.Variable, Destination);
      EffectKinds_.emplace(std::pair{Scope.get(), Source.Variable},
                           Source.Kind);
    }
    for (const auto &Contract : CallableContracts) {
      if (!Contract.Arrow || Contract.Arrow->tag != tc::MonoType::Arrow)
        return std::unexpected(error(
            StructuralProjectionErrorCode::WrongArrowShape, Contract.Range,
            Path + "/callables", "callable contract must name an Arrow"));
      if (!Scope->CallableContracts.emplace(Contract.Arrow, &Contract).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::DuplicateCallableContract,
                  Contract.Range, Path + "/callables",
                  "an Arrow has more than one imported callable contract"));
    }
    return Scope;
  }

  ProjectionResult<void>
  projectImportedDeclarations(const tc::SemanticProjectionBatch &Batch) {
    for (std::size_t Index = 0; Index < Batch.ImportedDeclarations.size();
         ++Index) {
      const auto &Import = Batch.ImportedDeclarations[Index];
      const auto Path = "imports[" + std::to_string(Index) + "]";
      if (Import.Function) {
        const auto &Contract = *Import.Function;
        auto Scope = makeImportedScope(
            model::BinderOwnerKind::Function, Contract.Scheme.quantified_vars,
            Contract.Scheme.quantified_effect_variables,
            Contract.CallableContracts, Import.Range, Path + "/function");
        if (!Scope)
          return std::unexpected(Scope.error());
        const auto &Identity =
            std::get<model::FunctionDeclarationIdentity>(Import.Identity);
        auto Signature = projectFunctionSignature(
            Contract.Scheme, Contract.ParameterOwnerships,
            Contract.ResultContract, Contract.Convention, Identity,
            std::nullopt, **Scope, Import.Range, Path + "/function/signature");
        if (!Signature)
          return std::unexpected(Signature.error());
        if (auto Valid = validateCallableContractClosure(
                **Scope, Import.Range, Path + "/function/callables");
            !Valid)
          return Valid;
        // Projection itself validates the complete imported callable contract
        // and interns its structural graph into the transaction's type table.
        // The declaration identity remains in ImportedFunctionDeclarations_
        // for occurrence validation; no second signature cache is required.
      }
      if (Import.Operation) {
        const auto &Contract = *Import.Operation;
        auto Scope = makeImportedScope(
            model::BinderOwnerKind::Operation, Contract.TypeParameters,
            Contract.EffectVariables, Contract.CallableContracts, Import.Range,
            Path + "/operation");
        if (!Scope)
          return std::unexpected(Scope.error());
        const auto &Key = std::get<model::EffectOperationKey>(Import.Identity);
        const tc::OperationSchemeProjection View{
            0,
            Key,
            Contract.TypeParameters,
            Contract.EffectVariables,
            Contract.Constraints,
            Contract.ParameterTypes,
            Contract.ParameterOwnerships,
            Contract.ResultType,
            Contract.ResultContract,
            tc::DeclarationVisibility::Private,
            Contract.Effects,
            Import.Range};
        auto Signature = projectOperationSignature(
            View, **Scope, Import.Range, Path + "/operation/signature");
        if (!Signature)
          return std::unexpected(Signature.error());
        if (auto Valid = validateCallableContractClosure(
                **Scope, Import.Range, Path + "/operation/callables");
            !Valid)
          return Valid;
        // Operation occurrences are checked against the source contract in
        // ImportedOperationDeclarations_. The projected value only needs to
        // participate in this transaction's validation and interning.
      }
      if (Import.Resource) {
        const auto &Contract = *Import.Resource;
        auto Scope = makeImportedScope(model::BinderOwnerKind::Resource,
                                       Contract.TypeParameters, {}, {},
                                       Import.Range, Path + "/resource");
        if (!Scope)
          return std::unexpected(Scope.error());
        const auto &Key = std::get<model::NominalTypeKey>(Import.Identity);
        model::ResourceDeclaration Declaration{Key,
                                               (*Scope)->DeclaredTypes,
                                               Contract.AbiKind,
                                               Contract.Shareability,
                                               Contract.TryRetainNativeSymbol,
                                               Contract.ReleaseNativeSymbol};
        if (auto Added = Declarations_.addResource(std::move(Declaration));
            !Added)
          return std::unexpected(
              error(StructuralProjectionErrorCode::InvalidInput, Import.Range,
                    Path + "/resource", Added.error()));
      }
    }
    return {};
  }

  ProjectionResult<void>
  validateCallableContractClosure(const ProjectionScope &Scope,
                                  SourceRange Range, std::string Path) const {
    if (Scope.ConsumedCallableContracts.size() ==
        Scope.CallableContracts.size())
      return {};
    SourceRange ErrorRange = Range;
    for (const auto &[Arrow, Contract] : Scope.CallableContracts)
      if (!Scope.ConsumedCallableContracts.contains(Arrow)) {
        ErrorRange = Contract->Range;
        break;
      }
    return std::unexpected(
        error(StructuralProjectionErrorCode::InvalidInput, ErrorRange,
              std::move(Path),
              "callable contract does not correspond to a reachable Arrow"));
  }

  ProjectionResult<void> validateSchemeEffectRoots(const tc::TypeScheme &Scheme,
                                                   SourceRange Range,
                                                   std::string Path) {
    auto Closure = tc::collectInferenceEffectClosure(
        Scheme.body, Scheme.constraints, Owner_.Effects_, Owner_.EffectAtoms_);
    if (!Closure) {
      const auto Code =
          Closure.error().Code ==
                  tc::InferenceEffectClosureErrorCode::ForeignEffect
              ? StructuralProjectionErrorCode::ForeignEffect
              : StructuralProjectionErrorCode::MissingEffectAtom;
      return std::unexpected(
          error(Code, Range, std::move(Path), Closure.error().Message));
    }

    if (Closure->ArrowRoots != Scheme.effect_roots)
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArrowShape, Range,
          std::move(Path),
          "scheme effect roots do not exactly cover every reachable Arrow"));
    if (!Closure->Effects.empty() && !Scheme.effect_graph)
      return std::unexpected(
          error(StructuralProjectionErrorCode::WrongArrowShape, Range,
                std::move(Path), "scheme effect closure has no frozen graph"));
    if (Closure->Effects.empty() && Scheme.effect_graph)
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArrowShape, Range,
          std::move(Path), "scheme has a frozen graph without effect roots"));
    return {};
  }

  ProjectionResult<void>
  projectFunction(const tc::FunctionSchemeProjection &Input,
                  std::size_t Index) {
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Function, Input.Range,
                     "functions[" + std::to_string(Index) + "]/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto &Scope = **ScopeResult;
    auto Signature = projectFunctionSignature(
        Input.Scheme, Input.ParameterOwnerships, Input.ResultContract,
        Input.Convention, Input.SourceIdentity, Input.ExportedSymbol, Scope,
        Input.Range, "functions[" + std::to_string(Index) + "]/signature");
    if (!Signature)
      return std::unexpected(Signature.error());

    ProjectedFunction Function;
    Function.Scope = Input.Scope;
    Function.Signature = *Signature;
    Function.Access = Input.Access;
    appendInheritedBinders(Scope, Function.InheritedTypeParameters,
                           Function.InheritedEffectVariables);
    Function.DeclaredTypeParameters = Scope.DeclaredTypes;
    Function.DeclaredEffectVariables = Scope.DeclaredEffects;
    for (std::size_t NodeIndex = 0; NodeIndex < Input.NodeFacts.size();
         ++NodeIndex) {
      auto Fact =
          projectNodeFact(Input.NodeFacts[NodeIndex], Scope,
                          "functions[" + std::to_string(Index) + "]/nodes[" +
                              std::to_string(NodeIndex) + "]");
      if (!Fact)
        return std::unexpected(Fact.error());
      Function.NodeFacts.push_back(std::move(*Fact));
    }
    Output_.Functions.push_back(std::move(Function));
    return {};
  }

  void appendInheritedBinders(
      const ProjectionScope &Scope, std::vector<model::TypeParameterId> &Types,
      std::vector<model::EffectVariableDeclaration> &Effects) {
    std::vector<const ProjectionScope *> Ancestors;
    for (auto *Current = Scope.Parent; Current; Current = Current->Parent)
      Ancestors.push_back(Current);
    for (auto Iterator = Ancestors.rbegin(); Iterator != Ancestors.rend();
         ++Iterator) {
      Types.insert(Types.end(), (*Iterator)->DeclaredTypes.begin(),
                   (*Iterator)->DeclaredTypes.end());
      Effects.insert(Effects.end(), (*Iterator)->DeclaredEffects.begin(),
                     (*Iterator)->DeclaredEffects.end());
    }
  }

  ProjectionResult<ProjectedNodeFact>
  projectNodeFact(const tc::NodeSemanticProjection &Input,
                  ProjectionScope &Scope, std::string Path) {
    auto Type = projectType(Input.Type, Scope, Input.Range, Path + "/type");
    if (!Type)
      return std::unexpected(Type.error());
    auto Effects =
        projectEffect(Input.Effects, Scope, Input.Range, Path + "/effects");
    if (!Effects)
      return std::unexpected(Effects.error());
    ProjectedNodeFact Result{Input.Node,   *Type,        *Effects,
                             std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt};
    if (Input.Instantiation) {
      auto Instantiation = projectInstantiation(*Input.Instantiation, Scope,
                                                Path + "/instantiation");
      if (!Instantiation)
        return std::unexpected(Instantiation.error());
      Result.Instantiation = std::move(*Instantiation);
    }
    if (Input.OperationOccurrence) {
      auto Occurrence = projectOccurrence(*Input.OperationOccurrence, Scope,
                                          Path + "/operation");
      if (!Occurrence)
        return std::unexpected(Occurrence.error());
      Result.OperationOccurrence = std::move(*Occurrence);
    }
    if (Input.TraitMethodUse) {
      auto Use =
          projectTraitUse(*Input.TraitMethodUse, Scope, Path + "/trait-method");
      if (!Use)
        return std::unexpected(Use.error());
      Result.TraitMethodUse = std::move(*Use);
    }
    if (Input.KeyOperations) {
      auto Hash = projectTraitUse(Input.KeyOperations->Hash, Scope,
                                  Path + "/key-operations/hash");
      if (!Hash)
        return std::unexpected(Hash.error());
      auto Equals = projectTraitUse(Input.KeyOperations->Equals, Scope,
                                    Path + "/key-operations/equals");
      if (!Equals)
        return std::unexpected(Equals.error());
      if (Input.KeyOperations->Hash.Trait.Name != "Hash" ||
          Input.KeyOperations->Hash.Method != "hash" ||
          Input.KeyOperations->Equals.Trait.Name != "Eq" ||
          Input.KeyOperations->Equals.Method != "eq" ||
          Hash->InstanceArguments.empty() || Equals->InstanceArguments.empty())
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Input.Range,
            Path + "/key-operations",
            "collection keys require exact Hash.hash and Eq.eq evidence"));
      if (!Input.Type || Input.Type->tag != tc::MonoType::App ||
          (Input.Type->type_name != "Set" && Input.Type->type_name != "Dict") ||
          Input.Type->args.empty())
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                  Path + "/key-operations",
                  "key-operation evidence belongs only to Set or Dict nodes"));
      auto Key = projectType(Input.Type->args.front(), Scope, Input.Range,
                             Path + "/key-operations/key");
      if (!Key)
        return std::unexpected(Key.error());
      PendingKeyOperations_.push_back({*Key, Hash->InstanceArguments.front(),
                                       Equals->InstanceArguments.front(),
                                       Input.Range, Path + "/key-operations"});
      Result.KeyOperations = ProjectedCollectionKeyOperations{
          std::move(*Hash), std::move(*Equals)};
    }
    if (Input.SumInjection) {
      auto Alternative = projectType(Input.SumInjection->Alternative, Scope,
                                     Input.Range, Path + "/sum/alternative");
      if (!Alternative)
        return std::unexpected(Alternative.error());
      auto Sum = projectType(Input.SumInjection->Sum, Scope, Input.Range,
                             Path + "/sum/target");
      if (!Sum)
        return std::unexpected(Sum.error());
      if (!Input.SumInjection->Sum ||
          Input.SumInjection->Sum->tag != tc::MonoType::Sum)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                  Path + "/sum", "sum injection target is not a sum type"));
      PendingSumInjections_.push_back(
          {*Type, *Alternative, *Sum, Input.Range, Path + "/sum"});
      Result.SumInjection = ProjectedSumInjection{*Alternative, *Sum};
    }
    return Result;
  }

  ProjectionResult<ProjectedSchemeInstantiation>
  projectInstantiation(const tc::SchemeInstantiationProjection &Input,
                       ProjectionScope &Scope, std::string Path) {
    if (const auto Found = SchemeArities_.find(Input.Scheme);
        Found != SchemeArities_.end()) {
      if (Found->second.first != Input.TypeArguments.size() ||
          Found->second.second != Input.EffectArguments.size())
        return std::unexpected(error(
            StructuralProjectionErrorCode::AmbiguousInstantiation, Input.Range,
            std::move(Path),
            "scheme instantiation does not provide every declared type and "
            "Flexible effect argument"));
    } else {
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
                std::move(Path),
                "scheme instantiation target is absent from the declaration "
                "catalog"));
    }
    if (std::ranges::any_of(
            Input.TypeArguments,
            [](tc::MonoTypePtr Type) { return Type == nullptr; }) ||
        std::ranges::any_of(Input.EffectArguments, [](tc::EffectRef Effect) {
          return !Effect.valid();
        }))
      return std::unexpected(
          error(StructuralProjectionErrorCode::AmbiguousInstantiation,
                Input.Range, std::move(Path),
                "scheme instantiation contains an unsolved explicit argument"));
    ProjectedSchemeInstantiation Result{Input.Scheme, {}, {}};
    for (std::size_t Index = 0; Index < Input.TypeArguments.size(); ++Index) {
      auto Argument =
          projectType(Input.TypeArguments[Index], Scope, Input.Range,
                      Path + "/type[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.TypeArguments.push_back(*Argument);
    }
    for (std::size_t Index = 0; Index < Input.EffectArguments.size(); ++Index) {
      auto Argument =
          projectEffect(Input.EffectArguments[Index], Scope, Input.Range,
                        Path + "/effect[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.EffectArguments.push_back(*Argument);
    }
    return Result;
  }

  struct InferenceComparisonSide {
    const std::unordered_map<tc::TypeId, tc::MonoTypePtr> *TypeSubstitutions =
        nullptr;
    const std::unordered_map<tc::EffectVarId, tc::EffectRef>
        *EffectSubstitutions = nullptr;
    std::function<const tc::CallableTypeContract *(tc::MonoTypePtr)> Callable;
    // A type supplied by a substitution is owned by the argument side, so any
    // Arrow nested in that graph must resolve its ABI and effects there.
    const InferenceComparisonSide *SubstitutionSource = nullptr;
  };

  struct InferenceComparisonResult {
    bool Equal = false;
    bool InvalidEffect = false;
  };

  [[nodiscard]] InferenceComparisonResult
  compareInferenceTypes(tc::MonoTypePtr Left, tc::MonoTypePtr Right,
                        const InferenceComparisonSide &LeftSide,
                        const InferenceComparisonSide &RightSide) const {
    bool InvalidEffect = false;
    using ActiveTypePair =
        std::tuple<tc::MonoTypePtr, const InferenceComparisonSide *,
                   tc::MonoTypePtr, const InferenceComparisonSide *>;
    std::set<ActiveTypePair> ActiveTypes;
    std::function<bool(tc::MonoTypePtr, tc::MonoTypePtr,
                       const InferenceComparisonSide &,
                       const InferenceComparisonSide &)>
        TypesEqual;
    std::function<bool(tc::EffectRef, tc::EffectRef,
                       const InferenceComparisonSide &,
                       const InferenceComparisonSide &)>
        EffectsEqual;

    struct ResolvedInferenceType {
      tc::MonoTypePtr Type = nullptr;
      const InferenceComparisonSide *Source = nullptr;
    };
    const auto Resolve = [](tc::MonoTypePtr Type,
                            const InferenceComparisonSide &InitialSide) {
      auto *Side = &InitialSide;
      std::set<std::pair<const InferenceComparisonSide *, tc::TypeId>> Seen;
      while (Type && Type->tag == tc::MonoType::Var &&
             Side->TypeSubstitutions &&
             Seen.emplace(Side, Type->var_id).second) {
        const auto Found = Side->TypeSubstitutions->find(Type->var_id);
        if (Found == Side->TypeSubstitutions->end())
          break;
        Type = Found->second;
        if (Side->SubstitutionSource)
          Side = Side->SubstitutionSource;
      }
      return ResolvedInferenceType{Type, Side};
    };
    using ActiveEffectPair =
        std::tuple<tc::EffectRef, const InferenceComparisonSide *,
                   tc::EffectRef, const InferenceComparisonSide *>;
    std::set<ActiveEffectPair> ActiveEffects;

    EffectsEqual =
        [&](tc::EffectRef LeftEffect, tc::EffectRef RightEffect,
            const InferenceComparisonSide &CurrentLeftSide,
            const InferenceComparisonSide &CurrentRightSide) -> bool {
      const auto Active = ActiveEffectPair{LeftEffect, &CurrentLeftSide,
                                           RightEffect, &CurrentRightSide};
      if (!ActiveEffects.insert(Active).second) {
        InvalidEffect = true;
        return false;
      }
      const auto FinishEffect = [&](bool Equal) {
        ActiveEffects.erase(Active);
        return Equal;
      };
      struct SourcedAtom {
        tc::EffectAtomId Atom = 0;
        const InferenceComparisonSide *Source = nullptr;
      };
      struct SourcedTail {
        tc::EffectProjection Tail;
        const InferenceComparisonSide *Source = nullptr;
      };
      struct ExpandedEffect {
        std::vector<SourcedAtom> KnownAtoms;
        std::vector<SourcedTail> Tails;
      };
      const auto AtomEqual = [&](const SourcedAtom &Left,
                                 const SourcedAtom &Right) {
        tc::EffectAtomId LeftAtom = Left.Atom;
        tc::EffectAtomId RightAtom = Right.Atom;
        try {
          LeftAtom = Owner_.Effects_.canonicalAtom(LeftAtom);
          RightAtom = Owner_.Effects_.canonicalAtom(RightAtom);
        } catch (const std::invalid_argument &) {
          InvalidEffect = true;
          return false;
        }
        if (!Owner_.EffectAtoms_.valid(LeftAtom) ||
            !Owner_.EffectAtoms_.valid(RightAtom)) {
          InvalidEffect = true;
          return false;
        }
        const auto &LeftValue = Owner_.EffectAtoms_.at(LeftAtom);
        const auto &RightValue = Owner_.EffectAtoms_.at(RightAtom);
        if (LeftValue.index() != RightValue.index())
          return false;
        const auto *LeftOperation =
            std::get_if<tc::OperationEffectAtom>(&LeftValue);
        if (!LeftOperation)
          return true;
        const auto &RightOperation =
            std::get<tc::OperationEffectAtom>(RightValue);
        if (LeftOperation->Key != RightOperation.Key ||
            LeftOperation->TypeArguments.size() !=
                RightOperation.TypeArguments.size() ||
            LeftOperation->EffectArguments.size() !=
                RightOperation.EffectArguments.size())
          return false;
        for (std::size_t Index = 0; Index < LeftOperation->TypeArguments.size();
             ++Index)
          if (!TypesEqual(LeftOperation->TypeArguments[Index],
                          RightOperation.TypeArguments[Index], *Left.Source,
                          *Right.Source))
            return false;
        for (std::size_t Index = 0;
             Index < LeftOperation->EffectArguments.size(); ++Index)
          if (!EffectsEqual(LeftOperation->EffectArguments[Index],
                            RightOperation.EffectArguments[Index], *Left.Source,
                            *Right.Source))
            return false;
        return true;
      };
      const auto Expand = [&](tc::EffectRef Effect,
                              const InferenceComparisonSide &InitialSide) {
        ExpandedEffect Expanded;
        if (!Owner_.Effects_.owns(Effect)) {
          InvalidEffect = true;
          return Expanded;
        }
        tc::EffectNormalForm Initial;
        try {
          Initial = Owner_.Effects_.summarize(Effect);
        } catch (const std::invalid_argument &) {
          InvalidEffect = true;
          return Expanded;
        }
        for (const auto Atom : Initial.known_atoms)
          Expanded.KnownAtoms.push_back({Atom, &InitialSide});
        std::function<void(
            tc::EffectProjection, const InferenceComparisonSide &,
            std::set<
                std::pair<const InferenceComparisonSide *, tc::EffectVarId>>)>
            ExpandTail;
        ExpandTail =
            [&](tc::EffectProjection Tail, const InferenceComparisonSide &Side,
                std::set<
                    std::pair<const InferenceComparisonSide *, tc::EffectVarId>>
                    ActiveTails) {
              const auto Replacement =
                  Side.EffectSubstitutions
                      ? Side.EffectSubstitutions->find(Tail.variable)
                      : std::unordered_map<tc::EffectVarId,
                                           tc::EffectRef>::const_iterator{};
              const bool HasReplacement =
                  Side.EffectSubstitutions &&
                  Replacement != Side.EffectSubstitutions->end();
              if (Tail.opaque || !HasReplacement) {
                Expanded.Tails.push_back({std::move(Tail), &Side});
                return;
              }
              if (!ActiveTails.emplace(&Side, Tail.variable).second) {
                InvalidEffect = true;
                return;
              }
              if (!Owner_.Effects_.owns(Replacement->second)) {
                InvalidEffect = true;
                return;
              }
              tc::EffectNormalForm Value;
              try {
                Value = Owner_.Effects_.summarize(Replacement->second);
              } catch (const std::invalid_argument &) {
                InvalidEffect = true;
                return;
              }
              const auto &ReplacementSide =
                  Side.SubstitutionSource ? *Side.SubstitutionSource : Side;
              for (const auto Atom : Value.known_atoms) {
                const SourcedAtom Candidate{Atom, &ReplacementSide};
                const bool Excluded = std::ranges::any_of(
                    Tail.excluded_atoms, [&](const auto Raw) {
                      return AtomEqual(SourcedAtom{Raw, &Side}, Candidate);
                    });
                if (!Excluded)
                  Expanded.KnownAtoms.push_back(Candidate);
              }
              for (auto Nested : Value.tails) {
                Nested.excluded_atoms.insert(Nested.excluded_atoms.end(),
                                             Tail.excluded_atoms.begin(),
                                             Tail.excluded_atoms.end());
                std::ranges::sort(Nested.excluded_atoms);
                Nested.excluded_atoms.erase(
                    std::ranges::unique(Nested.excluded_atoms).begin(),
                    Nested.excluded_atoms.end());
                ExpandTail(std::move(Nested), ReplacementSide, ActiveTails);
              }
            };
        for (auto &Tail : Initial.tails)
          ExpandTail(std::move(Tail), InitialSide, {});
        for (auto &Value : Expanded.KnownAtoms)
          try {
            Value.Atom = Owner_.Effects_.canonicalAtom(Value.Atom);
          } catch (const std::invalid_argument &) {
            InvalidEffect = true;
          }
        const auto LessAtom = [](const SourcedAtom &Left,
                                 const SourcedAtom &Right) {
          return std::tie(Left.Atom, Left.Source) <
                 std::tie(Right.Atom, Right.Source);
        };
        std::ranges::sort(Expanded.KnownAtoms, LessAtom);
        Expanded.KnownAtoms.erase(
            std::ranges::unique(Expanded.KnownAtoms, {},
                                [](const SourcedAtom &Value) {
                                  return std::pair{Value.Atom, Value.Source};
                                })
                .begin(),
            Expanded.KnownAtoms.end());
        for (auto &Value : Expanded.Tails) {
          for (auto &Atom : Value.Tail.excluded_atoms)
            try {
              Atom = Owner_.Effects_.canonicalAtom(Atom);
            } catch (const std::invalid_argument &) {
              InvalidEffect = true;
            }
          std::ranges::sort(Value.Tail.excluded_atoms);
          Value.Tail.excluded_atoms.erase(
              std::ranges::unique(Value.Tail.excluded_atoms).begin(),
              Value.Tail.excluded_atoms.end());
        }
        const auto LessTail = [](const SourcedTail &Left,
                                 const SourcedTail &Right) {
          return std::tie(Left.Tail.opaque, Left.Tail.variable,
                          Left.Tail.excluded_atoms, Left.Source) <
                 std::tie(Right.Tail.opaque, Right.Tail.variable,
                          Right.Tail.excluded_atoms, Right.Source);
        };
        std::ranges::sort(Expanded.Tails, LessTail);
        Expanded.Tails.erase(
            std::ranges::unique(Expanded.Tails, {},
                                [](const SourcedTail &Value) {
                                  return std::tuple{
                                      Value.Tail.opaque, Value.Tail.variable,
                                      Value.Tail.excluded_atoms, Value.Source};
                                })
                .begin(),
            Expanded.Tails.end());
        return Expanded;
      };
      const auto LeftForm = Expand(LeftEffect, CurrentLeftSide);
      const auto RightForm = Expand(RightEffect, CurrentRightSide);
      if (InvalidEffect)
        return FinishEffect(false);
      const auto AtomSetsEqual = [&](const auto &LeftAtoms,
                                     const auto &RightAtoms) {
        if (LeftAtoms.size() != RightAtoms.size())
          return false;
        std::vector<bool> Matched(RightAtoms.size());
        for (const auto &LeftAtom : LeftAtoms) {
          bool Found = false;
          for (std::size_t Index = 0; Index < RightAtoms.size(); ++Index)
            if (!Matched[Index] && AtomEqual(LeftAtom, RightAtoms[Index])) {
              Matched[Index] = true;
              Found = true;
              break;
            }
          if (!Found)
            return false;
        }
        return true;
      };
      if (!AtomSetsEqual(LeftForm.KnownAtoms, RightForm.KnownAtoms) ||
          LeftForm.Tails.size() != RightForm.Tails.size())
        return FinishEffect(false);
      std::vector<bool> Matched(RightForm.Tails.size());
      for (const auto &LeftValue : LeftForm.Tails) {
        bool Found = false;
        for (std::size_t Index = 0; Index < RightForm.Tails.size(); ++Index) {
          const auto &RightValue = RightForm.Tails[Index];
          if (!Matched[Index] &&
              LeftValue.Tail.opaque == RightValue.Tail.opaque &&
              LeftValue.Tail.variable == RightValue.Tail.variable) {
            std::vector<SourcedAtom> LeftExcluded;
            std::vector<SourcedAtom> RightExcluded;
            for (const auto Atom : LeftValue.Tail.excluded_atoms)
              LeftExcluded.push_back({Atom, LeftValue.Source});
            for (const auto Atom : RightValue.Tail.excluded_atoms)
              RightExcluded.push_back({Atom, RightValue.Source});
            if (!AtomSetsEqual(LeftExcluded, RightExcluded))
              continue;
            Matched[Index] = true;
            Found = true;
            break;
          }
        }
        if (!Found)
          return FinishEffect(false);
      }
      return FinishEffect(true);
    };

    TypesEqual = [&](tc::MonoTypePtr RawLeft, tc::MonoTypePtr RawRight,
                     const InferenceComparisonSide &CurrentLeftSide,
                     const InferenceComparisonSide &CurrentRightSide) -> bool {
      const auto LeftResolved = Resolve(RawLeft, CurrentLeftSide);
      const auto RightResolved = Resolve(RawRight, CurrentRightSide);
      const auto *LeftType = LeftResolved.Type;
      const auto *RightType = RightResolved.Type;
      if (!LeftType || !RightType)
        return LeftType == RightType;
      if (LeftType->tag != RightType->tag)
        return false;
      const auto Pair = ActiveTypePair{LeftType, LeftResolved.Source, RightType,
                                       RightResolved.Source};
      if (!ActiveTypes.insert(Pair).second)
        return true;
      const auto Finish = [&](bool Equal) {
        ActiveTypes.erase(Pair);
        return Equal;
      };
      switch (LeftType->tag) {
      case tc::MonoType::Var:
        return Finish(LeftType->var_id == RightType->var_id);
      case tc::MonoType::Con:
        return Finish(LeftType->con == RightType->con);
      case tc::MonoType::App: {
        if (LeftType->resolved_key || RightType->resolved_key) {
          if (!LeftType->resolved_key || !RightType->resolved_key ||
              LeftType->resolved_key != RightType->resolved_key)
            return Finish(false);
        } else if (LeftType->type_name != RightType->type_name) {
          return Finish(false);
        }
        if (LeftType->args.size() != RightType->args.size())
          return Finish(false);
        if (!LeftType->resolved_key && LeftType->type_name == "Promise" &&
            (LeftType->promise_result_contract !=
                 RightType->promise_result_contract ||
             !EffectsEqual(LeftType->promise_effects,
                           RightType->promise_effects, *LeftResolved.Source,
                           *RightResolved.Source)))
          return Finish(false);
        for (std::size_t Index = 0; Index < LeftType->args.size(); ++Index)
          if (!TypesEqual(LeftType->args[Index], RightType->args[Index],
                          *LeftResolved.Source, *RightResolved.Source))
            return Finish(false);
        return Finish(true);
      }
      case tc::MonoType::Arrow: {
        if (!TypesEqual(LeftType->param_type, RightType->param_type,
                        *LeftResolved.Source, *RightResolved.Source) ||
            !TypesEqual(LeftType->return_type, RightType->return_type,
                        *LeftResolved.Source, *RightResolved.Source) ||
            !EffectsEqual(LeftType->arrow_effect, RightType->arrow_effect,
                          *LeftResolved.Source, *RightResolved.Source))
          return Finish(false);
        const auto *LeftContract = LeftResolved.Source->Callable
                                       ? LeftResolved.Source->Callable(LeftType)
                                       : nullptr;
        const auto *RightContract =
            RightResolved.Source->Callable
                ? RightResolved.Source->Callable(RightType)
                : nullptr;
        if (!LeftContract || !RightContract)
          return Finish(false);
        struct NormalizedCallableContract {
          std::vector<model::ParameterOwnership> Parameters;
          model::ResultOwnership Result = model::ResultOwnership::Trivial;
          model::CallingConvention Convention =
              model::CallingConvention::DirectYona;
        };
        const auto NormalizeCallable = [&](const tc::CallableTypeContract &Raw,
                                           tc::MonoTypePtr Arrow,
                                           const InferenceComparisonSide &Side)
            -> std::optional<NormalizedCallableContract> {
          NormalizedCallableContract Result{
              {}, Raw.ResultContract, Raw.Convention};
          auto StageResolved = Resolve(Arrow, Side);
          auto *Stage = StageResolved.Type;
          auto *StageSide = StageResolved.Source;
          for (const auto Ownership : Raw.ParameterOwnerships) {
            if (!Stage || Stage->tag != tc::MonoType::Arrow)
              return std::nullopt;
            const auto ParameterResolved =
                Resolve(Stage->param_type, *StageSide);
            const auto *Parameter = ParameterResolved.Type;
            Result.Parameters.push_back(
                Parameter && Parameter->tag == tc::MonoType::Con &&
                        Parameter->con != tc::TyCon::String
                    ? model::ParameterOwnership::Trivial
                    : Ownership);
            StageResolved = Resolve(Stage->return_type, *StageSide);
            Stage = StageResolved.Type;
            StageSide = StageResolved.Source;
          }
          if (Stage && Stage->tag == tc::MonoType::Con &&
              Stage->con != tc::TyCon::String)
            Result.Result = model::ResultOwnership::Trivial;
          return Result;
        };
        const auto LeftAbi =
            NormalizeCallable(*LeftContract, LeftType, *LeftResolved.Source);
        const auto RightAbi =
            NormalizeCallable(*RightContract, RightType, *RightResolved.Source);
        return Finish(LeftAbi && RightAbi &&
                      LeftAbi->Parameters == RightAbi->Parameters &&
                      LeftAbi->Result == RightAbi->Result &&
                      LeftAbi->Convention == RightAbi->Convention);
      }
      case tc::MonoType::MTuple:
        if (LeftType->elements.size() != RightType->elements.size())
          return Finish(false);
        for (std::size_t Index = 0; Index < LeftType->elements.size(); ++Index)
          if (!TypesEqual(LeftType->elements[Index], RightType->elements[Index],
                          *LeftResolved.Source, *RightResolved.Source))
            return Finish(false);
        return Finish(true);
      case tc::MonoType::MRecord:
        if (LeftType->record_fields.size() != RightType->record_fields.size())
          return Finish(false);
        for (std::size_t Index = 0; Index < LeftType->record_fields.size();
             ++Index)
          if (LeftType->record_fields[Index].first !=
                  RightType->record_fields[Index].first ||
              !TypesEqual(LeftType->record_fields[Index].second,
                          RightType->record_fields[Index].second,
                          *LeftResolved.Source, *RightResolved.Source))
            return Finish(false);
        return Finish(TypesEqual(LeftType->row_rest, RightType->row_rest,
                                 *LeftResolved.Source, *RightResolved.Source));
      case tc::MonoType::Sum:
        if (LeftType->sum_alternatives.size() !=
            RightType->sum_alternatives.size())
          return Finish(false);
        for (std::size_t Index = 0; Index < LeftType->sum_alternatives.size();
             ++Index)
          if (!TypesEqual(LeftType->sum_alternatives[Index],
                          RightType->sum_alternatives[Index],
                          *LeftResolved.Source, *RightResolved.Source))
            return Finish(false);
        return Finish(true);
      }
      return Finish(false);
    };

    return {TypesEqual(Left, Right, LeftSide, RightSide), InvalidEffect};
  }

  ProjectionResult<ProjectedOperationApplication> projectResolvedApplication(
      const tc::ResolvedOperationApplicationProjection &Input,
      ProjectionScope &Scope, SourceRange Range, std::string Path) {
    const auto Declaration = OperationArities_.find(Input.Key);
    if (Declaration == OperationArities_.end())
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Range,
                std::move(Path),
                "operation occurrence is absent from the declaration catalog"));
    if (Declaration->second.first != Input.TypeArguments.size() ||
        Declaration->second.second != Input.EffectArguments.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Range, std::move(Path),
          "operation occurrence has the wrong generic arity"));
    if (Input.ParameterTypes.size() != Input.ParameterOwnerships.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Range, std::move(Path),
          "operation parameter ownership arity does not match its signature"));
    std::optional<tc::OperationSchemeProjection> ImportedView;
    const tc::ImportedOperationDeclarationProjection *ImportedContract =
        nullptr;
    const tc::OperationSchemeProjection *DeclaredSignature = nullptr;
    if (const auto Declared = OperationDeclarations_.find(Input.Key);
        Declared != OperationDeclarations_.end()) {
      DeclaredSignature = Declared->second;
    } else if (const auto Imported =
                   ImportedOperationDeclarations_.find(Input.Key);
               Imported != ImportedOperationDeclarations_.end()) {
      const auto &Contract = *Imported->second;
      ImportedContract = Imported->second;
      ImportedView.emplace(tc::OperationSchemeProjection{
          0, Input.Key, Contract.TypeParameters, Contract.EffectVariables,
          Contract.Constraints, Contract.ParameterTypes,
          Contract.ParameterOwnerships, Contract.ResultType,
          Contract.ResultContract, tc::DeclarationVisibility::Private,
          Contract.Effects, Range});
      DeclaredSignature = &*ImportedView;
    }
    if (DeclaredSignature) {
      const auto &Signature = *DeclaredSignature;
      if (Signature.ParameterTypes.size() != Input.ParameterTypes.size() ||
          !std::ranges::equal(Signature.ParameterOwnerships,
                              Input.ParameterOwnerships) ||
          Signature.ResultContract != Input.ResultContract)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Range, Path,
            "operation occurrence ownership or parameter shape differs from "
            "its declaration"));

      std::unordered_map<tc::TypeId, tc::MonoTypePtr> TypeSubstitutions;
      for (std::size_t Index = 0; Index < Signature.TypeParameters.size();
           ++Index)
        TypeSubstitutions.emplace(Signature.TypeParameters[Index],
                                  Input.TypeArguments[Index]);
      std::unordered_map<tc::EffectVarId, tc::EffectRef> EffectSubstitutions;
      for (std::size_t Index = 0; Index < Signature.EffectVariables.size();
           ++Index)
        EffectSubstitutions.emplace(Signature.EffectVariables[Index].Variable,
                                    Input.EffectArguments[Index]);

      const auto DeclaredCallable = [&](tc::MonoTypePtr Arrow) {
        if (Signature.Scope != 0) {
          const auto Found = ScopeById_.find(Signature.Scope);
          return Found == ScopeById_.end() ? nullptr
                                           : Found->second->callable(Arrow);
        }
        if (!ImportedContract)
          return static_cast<const tc::CallableTypeContract *>(nullptr);
        const auto Found =
            std::ranges::find(ImportedContract->CallableContracts, Arrow,
                              &tc::CallableTypeContract::Arrow);
        return Found == ImportedContract->CallableContracts.end() ? nullptr
                                                                  : &*Found;
      };
      const InferenceComparisonSide OccurrenceSide{
          nullptr, nullptr,
          [&](tc::MonoTypePtr Arrow) { return Scope.callable(Arrow); }};
      const InferenceComparisonSide DeclaredSide{
          &TypeSubstitutions, &EffectSubstitutions, DeclaredCallable,
          &OccurrenceSide};
      for (std::size_t Index = 0; Index < Signature.ParameterTypes.size();
           ++Index) {
        const auto Compared = compareInferenceTypes(
            Signature.ParameterTypes[Index], Input.ParameterTypes[Index],
            DeclaredSide, OccurrenceSide);
        if (!Compared.Equal) {
          if (Compared.InvalidEffect)
            return std::unexpected(
                error(StructuralProjectionErrorCode::ForeignEffect, Range,
                      Path + "/parameter[" + std::to_string(Index) + "]",
                      std::string(InvalidEffectReferenceMessage)));
          return std::unexpected(error(
              StructuralProjectionErrorCode::InvalidInput, Range,
              Path + "/parameter[" + std::to_string(Index) + "]",
              "operation parameter is not the declared generic substitution"));
        }
      }
      const auto ResultCompared = compareInferenceTypes(
          Signature.ResultType, Input.ResultType, DeclaredSide, OccurrenceSide);
      if (!ResultCompared.Equal) {
        if (ResultCompared.InvalidEffect)
          return std::unexpected(error(
              StructuralProjectionErrorCode::ForeignEffect, Range,
              Path + "/result", std::string(InvalidEffectReferenceMessage)));
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range,
                  Path + "/result",
                  "operation result is not the declared generic substitution"));
      }
    }
    ProjectedOperationApplication Result{Input.Key, {}, {},
                                         {},        {}, Input.ResultContract};
    for (std::size_t Index = 0; Index < Input.TypeArguments.size(); ++Index) {
      auto Value =
          projectType(Input.TypeArguments[Index], Scope, Range,
                      Path + "/type-argument[" + std::to_string(Index) + "]");
      if (!Value)
        return std::unexpected(Value.error());
      Result.TypeArguments.push_back(*Value);
    }
    for (std::size_t Index = 0; Index < Input.EffectArguments.size(); ++Index) {
      auto Value = projectEffect(Input.EffectArguments[Index], Scope, Range,
                                 Path + "/effect-argument[" +
                                     std::to_string(Index) + "]");
      if (!Value)
        return std::unexpected(Value.error());
      Result.EffectArguments.push_back(*Value);
    }
    for (std::size_t Index = 0; Index < Input.ParameterTypes.size(); ++Index) {
      auto Value =
          projectType(Input.ParameterTypes[Index], Scope, Range,
                      Path + "/parameter[" + std::to_string(Index) + "]");
      if (!Value)
        return std::unexpected(Value.error());
      Result.Parameters.push_back({*Value, Input.ParameterOwnerships[Index]});
    }
    auto ResultType =
        projectType(Input.ResultType, Scope, Range, Path + "/result");
    if (!ResultType)
      return std::unexpected(ResultType.error());
    Result.ResultType = *ResultType;
    return Result;
  }

  ProjectionResult<ProjectedOperationOccurrence>
  projectOccurrence(const tc::OperationOccurrenceProjection &Input,
                    ProjectionScope &Scope, std::string Path) {
    if (const auto *Perform = std::get_if<tc::PerformProjection>(&Input)) {
      auto Application = projectResolvedApplication(
          Perform->Application, Scope, Perform->Range, Path + "/application");
      if (!Application)
        return std::unexpected(Application.error());
      auto Continuation = projectType(Perform->ContinuationResultType, Scope,
                                      Perform->Range, Path + "/continuation");
      if (!Continuation)
        return std::unexpected(Continuation.error());
      auto Effects = projectEffect(Perform->ResumeEffects, Scope,
                                   Perform->Range, Path + "/resume-effects");
      if (!Effects)
        return std::unexpected(Effects.error());
      return ProjectedPerformFact{std::move(*Application), *Continuation,
                                  *Effects};
    }
    const auto &Handler = std::get<tc::HandlerClauseProjection>(Input);
    auto Application = projectResolvedApplication(
        Handler.Application, Scope, Handler.Range, Path + "/application");
    if (!Application)
      return std::unexpected(Application.error());
    auto Resume = projectType(Handler.SourceResumeType, Scope, Handler.Range,
                              Path + "/resume");
    if (!Resume)
      return std::unexpected(Resume.error());
    auto Effects = projectEffect(Handler.ReinstatedResumeEffects, Scope,
                                 Handler.Range, Path + "/resume-effects");
    if (!Effects)
      return std::unexpected(Effects.error());
    return ProjectedHandlerClauseFact{std::move(*Application), *Resume,
                                      *Effects};
  }

  ProjectionResult<ProjectedTraitMethodUseFact>
  projectTraitUse(const tc::TraitMethodSelectionProjection &Input,
                  ProjectionScope &Scope, std::string Path) {
    const auto Trait = TraitMethods_.find(Input.Trait);
    if (Trait == TraitMethods_.end() || !Trait->second.contains(Input.Method))
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
                std::move(Path),
                "trait method is absent from the declaration catalog"));
    if (TraitArities_.at(Input.Trait) != Input.InstanceArguments.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Input.Range,
          std::move(Path), "trait selection has the wrong instance arity"));
    model::TraitSelectionEvidence ProjectedEvidence =
        [&]() -> model::TraitSelectionEvidence {
      if (const auto *Resolved =
              std::get_if<tc::ResolvedTraitTargetProjection>(&Input.Evidence))
        return model::TraitTargetApplication{Resolved->SelectedTarget, {}, {}};
      const auto &Deferred =
          std::get<tc::DeferredTraitEvidenceProjection>(Input.Evidence);
      return model::DeferredTraitEvidence{
          Deferred.OwningScheme,
          model::TypeConstraint{
              model::TypeParameterId{},
              Deferred.Constraint.resolved_trait.value_or(Input.Trait),
              {}}};
    }();
    ProjectedTraitMethodUseFact Result{
        Input.Trait, Input.Method, {}, {}, {}, std::move(ProjectedEvidence)};
    for (std::size_t Index = 0; Index < Input.InstanceArguments.size();
         ++Index) {
      auto Argument =
          projectType(Input.InstanceArguments[Index], Scope, Input.Range,
                      Path + "/instance[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.InstanceArguments.push_back(*Argument);
    }
    for (std::size_t Index = 0; Index < Input.MethodTypeArguments.size();
         ++Index) {
      auto Argument =
          projectType(Input.MethodTypeArguments[Index], Scope, Input.Range,
                      Path + "/method-type[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.MethodTypeArguments.push_back(*Argument);
    }
    for (std::size_t Index = 0; Index < Input.MethodEffectArguments.size();
         ++Index) {
      auto Argument =
          projectEffect(Input.MethodEffectArguments[Index], Scope, Input.Range,
                        Path + "/method-effect[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.MethodEffectArguments.push_back(*Argument);
    }
    if (const auto *Deferred =
            std::get_if<tc::DeferredTraitEvidenceProjection>(&Input.Evidence)) {
      const auto Owner = SchemeConstraints_.find(Deferred->OwningScheme);
      const auto OwnerScope = SchemeScopes_.find(Deferred->OwningScheme);
      if (Owner == SchemeConstraints_.end() ||
          OwnerScope == SchemeScopes_.end() ||
          !ScopeById_.contains(OwnerScope->second))
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
            Path + "/evidence/owner",
            "deferred trait evidence names a scheme absent from the catalog"));
      bool InvalidEvidenceEffect = false;
      const auto &EvidenceScope = *ScopeById_.at(OwnerScope->second);
      const InferenceComparisonSide EvidenceSide{
          nullptr, nullptr,
          [&](tc::MonoTypePtr Arrow) { return EvidenceScope.callable(Arrow); }};
      const auto SameConstraint = [&](const tc::Constraint &Candidate) {
        if (Candidate.resolved_trait != Deferred->Constraint.resolved_trait ||
            Candidate.types.size() != Deferred->Constraint.types.size())
          return false;
        for (std::size_t Index = 0; Index < Candidate.types.size(); ++Index) {
          const auto Compared = compareInferenceTypes(
              Candidate.types[Index], Deferred->Constraint.types[Index],
              EvidenceSide, EvidenceSide);
          InvalidEvidenceEffect =
              InvalidEvidenceEffect || Compared.InvalidEffect;
          if (!Compared.Equal)
            return false;
        }
        return true;
      };
      if (std::ranges::count_if(*Owner->second, SameConstraint) != 1) {
        if (InvalidEvidenceEffect)
          return std::unexpected(
              error(StructuralProjectionErrorCode::ForeignEffect, Input.Range,
                    Path + "/evidence/constraint",
                    std::string(InvalidEffectReferenceMessage)));
        else
          return std::unexpected(
              error(StructuralProjectionErrorCode::UnresolvedIdentity,
                    Input.Range, Path + "/evidence/constraint",
                    "deferred trait evidence is not owned exactly once by its "
                    "scheme"));
      }
      auto Constraints = projectConstraints(
          std::span<const tc::Constraint>(&Deferred->Constraint, 1),
          *ScopeById_.at(OwnerScope->second), Input.Range, Path + "/evidence");
      if (!Constraints)
        return std::unexpected(Constraints.error());
      Result.Evidence = model::DeferredTraitEvidence{
          Deferred->OwningScheme, std::move(Constraints->front())};
    } else {
      const auto &Resolved =
          std::get<tc::ResolvedTraitTargetProjection>(Input.Evidence);
      auto Target =
          projectTarget(Resolved, Scope, Input.Range, Path + "/evidence");
      if (!Target)
        return std::unexpected(Target.error());
      Result.Evidence = std::move(*Target);
    }
    return Result;
  }

  ProjectionResult<model::TraitTargetApplication>
  projectTarget(const tc::ResolvedTraitTargetProjection &Input,
                ProjectionScope &Scope, SourceRange Range, std::string Path) {
    if (const auto Found = SchemeArities_.find(Input.SelectedTarget);
        Found != SchemeArities_.end()) {
      if (Found->second.first != Input.TargetTypeArguments.size() ||
          Found->second.second != Input.TargetEffectArguments.size())
        return std::unexpected(error(
            StructuralProjectionErrorCode::WrongArity, Range, std::move(Path),
            "trait target application has the wrong generic arity"));
    } else {
      return std::unexpected(error(
          StructuralProjectionErrorCode::UnresolvedIdentity, Range,
          std::move(Path), "trait target is absent from the function catalog"));
    }
    model::TraitTargetApplication Result{Input.SelectedTarget, {}, {}};
    for (std::size_t Index = 0; Index < Input.TargetTypeArguments.size();
         ++Index) {
      auto Argument =
          projectType(Input.TargetTypeArguments[Index], Scope, Range,
                      Path + "/type[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.TypeArguments.push_back(*Argument);
    }
    for (std::size_t Index = 0; Index < Input.TargetEffectArguments.size();
         ++Index) {
      auto Argument =
          projectEffect(Input.TargetEffectArguments[Index], Scope, Range,
                        Path + "/effect[" + std::to_string(Index) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.EffectArguments.push_back(*Argument);
    }
    return Result;
  }

  struct FunctionContractView {
    const tc::TypeScheme *Scheme = nullptr;
    std::span<const model::ParameterOwnership> ParameterOwnerships;
    model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
    model::CallingConvention Convention = model::CallingConvention::DirectYona;
    const ProjectionScope *Scope = nullptr;
    std::span<const tc::CallableTypeContract> CallableContracts;

    [[nodiscard]] const tc::CallableTypeContract *
    callable(tc::MonoTypePtr Arrow) const {
      if (Scope)
        return Scope->callable(Arrow);
      const auto Found = std::ranges::find(CallableContracts, Arrow,
                                           &tc::CallableTypeContract::Arrow);
      return Found == CallableContracts.end() ? nullptr : &*Found;
    }
  };

  std::optional<FunctionContractView>
  functionContract(const model::FunctionDeclarationIdentity &Identity) const {
    if (const auto Found = FunctionDeclarations_.find(Identity);
        Found != FunctionDeclarations_.end())
      return FunctionContractView{&Found->second->Scheme,
                                  Found->second->ParameterOwnerships,
                                  Found->second->ResultContract,
                                  Found->second->Convention,
                                  ScopeById_.contains(Found->second->Scope)
                                      ? ScopeById_.at(Found->second->Scope)
                                      : nullptr,
                                  {}};
    if (const auto Found = ImportedFunctionDeclarations_.find(Identity);
        Found != ImportedFunctionDeclarations_.end())
      return FunctionContractView{&Found->second->Scheme,
                                  Found->second->ParameterOwnerships,
                                  Found->second->ResultContract,
                                  Found->second->Convention,
                                  nullptr,
                                  Found->second->CallableContracts};
    return std::nullopt;
  }

  ProjectionResult<void>
  validateTargetSignature(const tc::TraitMethodProjection &Method,
                          const tc::ResolvedTraitTargetProjection &Target,
                          const model::NominalTypeKey &OwningTrait,
                          const std::unordered_map<tc::TypeId, tc::MonoTypePtr>
                              &ExpectedTypeSubstitutions,
                          const ProjectionScope &ArgumentScope,
                          SourceRange Range, const std::string &Path) const {
    const auto Actual = functionContract(Target.SelectedTarget);
    if (!Actual)
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Range, Path,
                "trait target is absent from the complete function catalog"));
    if (Method.ParameterOwnerships.size() !=
            Actual->ParameterOwnerships.size() ||
        Method.Convention != Actual->Convention)
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Range, Path,
                "trait target ownership or calling convention differs from its "
                "substituted method signature"));

    std::unordered_map<tc::TypeId, tc::MonoTypePtr> ActualTypeSubstitutions;
    for (std::size_t Index = 0; Index < Actual->Scheme->quantified_vars.size();
         ++Index)
      ActualTypeSubstitutions.emplace(Actual->Scheme->quantified_vars[Index],
                                      Target.TargetTypeArguments[Index]);
    std::unordered_map<tc::EffectVarId, tc::EffectRef>
        ActualEffectSubstitutions;
    std::size_t EffectArgument = 0;
    for (const auto &Variable : Actual->Scheme->quantified_effect_variables)
      if (Variable.Kind == model::EffectVariableKind::Flexible)
        ActualEffectSubstitutions.emplace(
            Variable.Variable, Target.TargetEffectArguments[EffectArgument++]);

    const auto SubstitutedExpected = [&](tc::MonoTypePtr Type) {
      std::unordered_set<tc::TypeId> Seen;
      while (Type && Type->tag == tc::MonoType::Var &&
             Seen.insert(Type->var_id).second) {
        const auto Found = ExpectedTypeSubstitutions.find(Type->var_id);
        if (Found == ExpectedTypeSubstitutions.end())
          break;
        Type = Found->second;
      }
      return Type;
    };
    auto *ExpectedStage = Method.Scheme.body;
    for (std::size_t Index = 0; Index < Method.ParameterOwnerships.size();
         ++Index) {
      if (!ExpectedStage || ExpectedStage->tag != tc::MonoType::Arrow)
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArrowShape, Range, Path,
                  "trait method ownership exceeds its Arrow chain"));
      const auto *Parameter = SubstitutedExpected(ExpectedStage->param_type);
      const auto ExpectedOwnership =
          Parameter && Parameter->tag == tc::MonoType::Con &&
                  Parameter->con != tc::TyCon::String
              ? model::ParameterOwnership::Trivial
              : Method.ParameterOwnerships[Index];
      if (ExpectedOwnership != Actual->ParameterOwnerships[Index])
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Range, Path,
            "trait target ownership or calling convention differs from its "
            "substituted method signature"));
      ExpectedStage = ExpectedStage->return_type;
    }
    const auto *ExpectedResult = SubstitutedExpected(ExpectedStage);
    const auto ExpectedResultContract =
        ExpectedResult && ExpectedResult->tag == tc::MonoType::Con &&
                ExpectedResult->con != tc::TyCon::String
            ? model::ResultOwnership::Trivial
            : Method.ResultContract;
    if (ExpectedResultContract != Actual->ResultContract)
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Range, Path,
                "trait target ownership or calling convention differs from its "
                "substituted method signature"));

    const auto MethodScope = ScopeById_.find(Method.Scope);
    if (MethodScope == ScopeById_.end())
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidParent, Range, Path,
                "trait method scope is absent from the projection forest"));
    const InferenceComparisonSide ArgumentSide{
        nullptr, nullptr,
        [&](tc::MonoTypePtr Arrow) { return ArgumentScope.callable(Arrow); }};
    const InferenceComparisonSide MethodSide{
        &ExpectedTypeSubstitutions, nullptr,
        [&](tc::MonoTypePtr Arrow) {
          return MethodScope->second->callable(Arrow);
        },
        &ArgumentSide};
    const InferenceComparisonSide TargetSide{
        &ActualTypeSubstitutions, &ActualEffectSubstitutions,
        [&](tc::MonoTypePtr Arrow) { return Actual->callable(Arrow); },
        &ArgumentSide};
    std::vector<const tc::Constraint *> ExpectedConstraints;
    std::vector<const tc::Constraint *> ActualConstraints;
    for (const auto &Constraint : Method.Scheme.constraints)
      if (Constraint.resolved_trait != OwningTrait)
        ExpectedConstraints.push_back(&Constraint);
    for (const auto &Constraint : Actual->Scheme->constraints)
      if (Constraint.resolved_trait != OwningTrait)
        ActualConstraints.push_back(&Constraint);
    if (ExpectedConstraints.size() != ActualConstraints.size())
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Range,
                Path + "/constraints",
                "trait target constraints differ from its substituted method "
                "signature"));
    std::vector<bool> MatchedConstraints(ActualConstraints.size());
    for (const auto *Expected : ExpectedConstraints) {
      bool Matched = false;
      for (std::size_t CandidateIndex = 0;
           CandidateIndex < ActualConstraints.size(); ++CandidateIndex) {
        if (MatchedConstraints[CandidateIndex])
          continue;
        const auto *Candidate = ActualConstraints[CandidateIndex];
        if (Expected->resolved_trait != Candidate->resolved_trait ||
            Expected->types.size() != Candidate->types.size())
          continue;
        bool Equal = true;
        for (std::size_t TypeIndex = 0; TypeIndex < Expected->types.size();
             ++TypeIndex) {
          const auto Compared = compareInferenceTypes(
              Expected->types[TypeIndex], Candidate->types[TypeIndex],
              MethodSide, TargetSide);
          if (Compared.InvalidEffect)
            return std::unexpected(
                error(StructuralProjectionErrorCode::ForeignEffect, Range,
                      Path + "/constraints",
                      std::string(InvalidEffectReferenceMessage)));
          if (!Compared.Equal) {
            Equal = false;
            break;
          }
        }
        if (Equal) {
          MatchedConstraints[CandidateIndex] = true;
          Matched = true;
          break;
        }
      }
      if (!Matched)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Range,
                  Path + "/constraints",
                  "trait target constraints differ from its substituted method "
                  "signature"));
    }
    const auto Compared = compareInferenceTypes(
        Method.Scheme.body, Actual->Scheme->body, MethodSide, TargetSide);
    if (!Compared.Equal) {
      if (Compared.InvalidEffect)
        return std::unexpected(
            error(StructuralProjectionErrorCode::ForeignEffect, Range, Path,
                  std::string(InvalidEffectReferenceMessage)));
      return std::unexpected(error(
          StructuralProjectionErrorCode::InvalidInput, Range, Path,
          "trait target type or effects differ from its substituted method "
          "signature"));
    }
    return {};
  }

  ProjectionResult<model::EffectOperationSignature>
  projectOperationSignature(const tc::OperationSchemeProjection &Input,
                            ProjectionScope &Scope, SourceRange Range,
                            const std::string &Path) {
    if (Input.TypeParameters.size() != Scope.DeclaredTypes.size() ||
        Input.EffectVariables.size() != Scope.DeclaredEffects.size() ||
        Input.ParameterTypes.size() != Input.ParameterOwnerships.size())
      return std::unexpected(error(
          StructuralProjectionErrorCode::WrongArity, Range, Path,
          "operation declaration arity differs from its scope or ownerships"));
    model::EffectOperationSignature Signature{Input.Key,
                                              Scope.DeclaredTypes,
                                              Scope.DeclaredEffects,
                                              {},
                                              {},
                                              {},
                                              Input.ResultContract,
                                              {}};
    auto Constraints = projectConstraints(Input.Constraints, Scope, Range,
                                          Path + "/constraints");
    if (!Constraints)
      return std::unexpected(Constraints.error());
    Signature.Constraints = std::move(*Constraints);
    for (std::size_t Parameter = 0; Parameter < Input.ParameterTypes.size();
         ++Parameter) {
      auto Type =
          projectType(Input.ParameterTypes[Parameter], Scope, Range,
                      Path + "/parameter[" + std::to_string(Parameter) + "]");
      if (!Type)
        return std::unexpected(Type.error());
      Signature.Parameters.push_back(
          {*Type, Input.ParameterOwnerships[Parameter]});
    }
    auto Result = projectType(Input.ResultType, Scope, Range, Path + "/result");
    if (!Result)
      return std::unexpected(Result.error());
    Signature.Result = *Result;
    Signature.ResultContract = Input.ResultContract;
    auto Effects =
        projectEffect(Input.Effects, Scope, Range, Path + "/effects");
    if (!Effects)
      return std::unexpected(Effects.error());
    Signature.Effects = *Effects;
    return Signature;
  }

  ProjectionResult<void>
  projectOperation(const tc::OperationSchemeProjection &Input,
                   std::size_t Index) {
    const auto Path = "operations[" + std::to_string(Index) + "]";
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Operation,
                     Input.Range, Path + "/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto Projected =
        projectOperationSignature(Input, **ScopeResult, Input.Range, Path);
    if (!Projected)
      return std::unexpected(Projected.error());
    auto Signature = std::move(*Projected);
    Output_.Operations.push_back(Signature);
    Output_.InterfaceSeed.Operations.push_back(
        {std::move(Signature), Input.Access});
    return {};
  }

  ProjectionResult<void>
  projectNominal(const tc::NominalDeclarationProjection &Input,
                 std::size_t Index) {
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Nominal, Input.Range,
                     "nominals[" + std::to_string(Index) + "]/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto &Scope = **ScopeResult;
    if (!Scope.Identity ||
        std::get<model::NominalTypeKey>(*Scope.Identity) != Input.Key)
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
                "nominals[" + std::to_string(Index) + "]/key",
                "nominal key differs from its binder owner"));
    if (auto Added = Declarations_.addNominal(model::NominalDeclarationArity{
            Input.Key, static_cast<std::uint32_t>(Scope.DeclaredTypes.size())});
        !Added)
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                "nominals[" + std::to_string(Index) + "]", Added.error()));
    ProjectedNominalDeclaration Result{
        Input.Key, Scope.DeclaredTypes, {}, Input.Access, Input.Opaque};
    std::unordered_set<std::string> Names;
    std::unordered_set<std::uint32_t> Tags;
    for (std::size_t ConstructorIndex = 0;
         ConstructorIndex < Input.Constructors.size(); ++ConstructorIndex) {
      const auto &Constructor = Input.Constructors[ConstructorIndex];
      if (!Names.insert(Constructor.Name).second ||
          !Tags.insert(Constructor.Tag).second)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Constructor.Range,
            "nominals[" + std::to_string(Index) + "]/constructors[" +
                std::to_string(ConstructorIndex) + "]",
            "constructor names and tags must be unique"));
      ProjectedConstructor Projected{Constructor.Name, Constructor.Tag, {}};
      for (std::size_t FieldIndex = 0; FieldIndex < Constructor.Fields.size();
           ++FieldIndex) {
        auto Field = projectType(
            Constructor.Fields[FieldIndex], Scope, Constructor.Range,
            "nominals[" + std::to_string(Index) + "]/constructors[" +
                std::to_string(ConstructorIndex) + "]/field[" +
                std::to_string(FieldIndex) + "]");
        if (!Field)
          return std::unexpected(Field.error());
        Projected.Fields.push_back(*Field);
      }
      Result.Constructors.push_back(std::move(Projected));
    }
    Output_.InterfaceSeed.Nominals.push_back(std::move(Result));
    return {};
  }

  ProjectionResult<void>
  projectResource(const tc::ResourceDeclarationProjection &Input,
                  std::size_t Index) {
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Resource, Input.Range,
                     "resources[" + std::to_string(Index) + "]/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto &Scope = **ScopeResult;
    if (Input.TypeParameters.size() != Scope.DeclaredTypes.size())
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                "resources[" + std::to_string(Index) + "]",
                "resource policy is incomplete or has the wrong binder arity"));
    model::ResourceDeclaration Declaration{Input.Key,
                                           Scope.DeclaredTypes,
                                           Input.AbiKind,
                                           Input.Shareability,
                                           Input.TryRetainNativeSymbol,
                                           Input.ReleaseNativeSymbol};
    if (auto Added = Declarations_.addResource(Declaration); !Added)
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                "resources[" + std::to_string(Index) + "]", Added.error()));
    Output_.InterfaceSeed.Resources.push_back(
        {Declaration.Key, Declaration.TypeParameters, Declaration.AbiKind,
         Declaration.Shareability, Declaration.TryRetainNativeSymbol,
         Declaration.ReleaseNativeSymbol, Input.Access});
    return {};
  }

  ProjectionResult<void>
  projectTrait(const tc::TraitDeclarationProjection &Input, std::size_t Index) {
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Trait, Input.Range,
                     "traits[" + std::to_string(Index) + "]/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto &Scope = **ScopeResult;
    ProjectedTraitDeclaration Result{
        Input.Key, Scope.DeclaredTypes, {}, {}, Input.Access};
    auto Superclasses = projectConstraints(
        Input.Superclasses, Scope, Input.Range,
        "traits[" + std::to_string(Index) + "]/superclasses");
    if (!Superclasses)
      return std::unexpected(Superclasses.error());
    Result.Superclasses = std::move(*Superclasses);
    std::unordered_set<std::string> MethodNames;
    for (std::size_t MethodIndex = 0; MethodIndex < Input.Methods.size();
         ++MethodIndex) {
      const auto &Method = Input.Methods[MethodIndex];
      if (!MethodNames.insert(Method.Name).second)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Method.Range,
                  "traits[" + std::to_string(Index) + "]/methods[" +
                      std::to_string(MethodIndex) + "]",
                  "trait method names must be unique"));
      auto MethodScope = requireScope(
          Method.Scope, model::BinderOwnerKind::Function, Method.Range,
          "traits[" + std::to_string(Index) + "]/methods[" +
              std::to_string(MethodIndex) + "]/scope");
      if (!MethodScope)
        return std::unexpected(MethodScope.error());
      auto Signature = projectFunctionSignature(
          Method.Scheme, Method.ParameterOwnerships, Method.ResultContract,
          Method.Convention, std::nullopt, std::nullopt, **MethodScope,
          Method.Range,
          "traits[" + std::to_string(Index) + "]/methods[" +
              std::to_string(MethodIndex) + "]/signature");
      if (!Signature)
        return std::unexpected(Signature.error());
      std::optional<model::TraitTargetApplication> Target;
      if (Method.DefaultTarget) {
        auto ProjectedTarget =
            projectTarget(*Method.DefaultTarget, **MethodScope, Method.Range,
                          "traits[" + std::to_string(Index) + "]/methods[" +
                              std::to_string(MethodIndex) + "]/default");
        if (!ProjectedTarget)
          return std::unexpected(ProjectedTarget.error());
        if (auto Valid = validateTargetSignature(
                Method, *Method.DefaultTarget, Input.Key, {}, **MethodScope,
                Method.Range,
                "traits[" + std::to_string(Index) + "]/methods[" +
                    std::to_string(MethodIndex) + "]/default/signature");
            !Valid)
          return std::unexpected(Valid.error());
        Target = std::move(*ProjectedTarget);
      }
      Result.Methods.push_back({Method.Name, *Signature, std::move(Target)});
    }
    Output_.InterfaceSeed.Traits.push_back(std::move(Result));
    return {};
  }

  ProjectionResult<void>
  projectInstance(const tc::TraitInstanceProjection &Input, std::size_t Index) {
    auto ScopeResult =
        requireScope(Input.Scope, model::BinderOwnerKind::Instance, Input.Range,
                     "instances[" + std::to_string(Index) + "]/scope");
    if (!ScopeResult)
      return std::unexpected(ScopeResult.error());
    auto &Scope = **ScopeResult;
    if (!TraitMethods_.contains(Input.Trait))
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
                "instances[" + std::to_string(Index) + "]/trait",
                "instance trait is absent from the declaration catalog"));
    if (TraitArities_.at(Input.Trait) != Input.Arguments.size())
      return std::unexpected(
          error(StructuralProjectionErrorCode::WrongArity, Input.Range,
                "instances[" + std::to_string(Index) + "]/trait",
                "instance head has the wrong trait arity"));
    ProjectedTraitInstance Result{Input.Trait, Scope.DeclaredTypes, {}, {},
                                  {},          Input.Access};
    for (std::size_t ArgumentIndex = 0; ArgumentIndex < Input.Arguments.size();
         ++ArgumentIndex) {
      auto Argument =
          projectType(Input.Arguments[ArgumentIndex], Scope, Input.Range,
                      "instances[" + std::to_string(Index) + "]/argument[" +
                          std::to_string(ArgumentIndex) + "]");
      if (!Argument)
        return std::unexpected(Argument.error());
      Result.Arguments.push_back(*Argument);
    }
    auto Constraints = projectConstraints(Input.Constraints, Scope, Input.Range,
                                          "instances[" + std::to_string(Index) +
                                              "]/constraints");
    if (!Constraints)
      return std::unexpected(Constraints.error());
    Result.Constraints = std::move(*Constraints);
    std::unordered_set<std::string> Methods;
    for (std::size_t MethodIndex = 0; MethodIndex < Input.Methods.size();
         ++MethodIndex) {
      const auto &Method = Input.Methods[MethodIndex];
      if (Method.InstanceScope != Input.Scope ||
          !Methods.insert(Method.Method).second)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Method.Range,
            "instances[" + std::to_string(Index) + "]/methods[" +
                std::to_string(MethodIndex) + "]",
            "instance method binding has invalid scope or duplicate name"));
      auto MethodScope = requireScope(
          Method.MethodScope, model::BinderOwnerKind::Function, Method.Range,
          "instances[" + std::to_string(Index) + "]/methods[" +
              std::to_string(MethodIndex) + "]/scope");
      if (!MethodScope)
        return std::unexpected(MethodScope.error());
      const auto DeclaredMethods = TraitMethodScopes_.find(Input.Trait);
      if (DeclaredMethods == TraitMethodScopes_.end() ||
          !DeclaredMethods->second.contains(Method.Method) ||
          DeclaredMethods->second.at(Method.Method) != Method.MethodScope)
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Method.Range,
            "instances[" + std::to_string(Index) + "]/methods[" +
                std::to_string(MethodIndex) + "]/scope",
            "instance binding does not reference its trait method scope"));

      ProjectionScope Composite;
      Composite.Parent = &Scope;
      Composite.Kind = model::BinderOwnerKind::Function;
      Composite.Transaction = this;
      for (const auto &[Source, Destination] : (*MethodScope)->TypeBinders) {
        if (Scope.typeBinder(Source))
          return std::unexpected(error(
              StructuralProjectionErrorCode::BinderKindMismatch, Method.Range,
              "instances[" + std::to_string(Index) + "]/methods[" +
                  std::to_string(MethodIndex) + "]/binders",
              "instance and trait-method type binder maps overlap"));
        Composite.TypeBinders.emplace(Source, Destination);
      }
      for (const auto &[Source, Destination] : (*MethodScope)->EffectBinders) {
        if (Scope.effectBinder(Source))
          return std::unexpected(error(
              StructuralProjectionErrorCode::BinderKindMismatch, Method.Range,
              "instances[" + std::to_string(Index) + "]/methods[" +
                  std::to_string(MethodIndex) + "]/binders",
              "instance and trait-method effect binder maps overlap"));
        Composite.EffectBinders.emplace(Source, Destination);
        const auto Kind = EffectKinds_.find(std::pair{*MethodScope, Source});
        if (Kind == EffectKinds_.end())
          return std::unexpected(error(
              StructuralProjectionErrorCode::BinderKindMismatch, Method.Range,
              "instances[" + std::to_string(Index) + "]/methods[" +
                  std::to_string(MethodIndex) + "]/binders",
              "trait-method effect binder has no declared kind"));
        EffectKinds_.emplace(std::pair{&Composite, Source}, Kind->second);
      }
      Composite.CallableContracts = (*MethodScope)->CallableContracts;
      auto Target =
          projectTarget(Method.Target, Composite, Method.Range,
                        "instances[" + std::to_string(Index) + "]/methods[" +
                            std::to_string(MethodIndex) + "]/target");
      if (!Target)
        return std::unexpected(Target.error());
      const auto TraitMethods = TraitMethodDeclarations_.find(Input.Trait);
      if (TraitMethods == TraitMethodDeclarations_.end() ||
          !TraitMethods->second.contains(Method.Method))
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Method.Range,
            "instances[" + std::to_string(Index) + "]/methods[" +
                std::to_string(MethodIndex) + "]/target/signature",
            "instance binding has no complete trait method declaration"));
      const auto *TraitScope = (*MethodScope)->Parent;
      const auto TraitScopeInput =
          TraitScope ? ScopeInputs_.find(TraitScope->Id) : ScopeInputs_.end();
      if (TraitScopeInput == ScopeInputs_.end() ||
          TraitScopeInput->second->TypeParameters.size() !=
              Input.Arguments.size())
        return std::unexpected(
            error(StructuralProjectionErrorCode::WrongArity, Method.Range,
                  "instances[" + std::to_string(Index) + "]/methods[" +
                      std::to_string(MethodIndex) + "]/target/signature",
                  "trait method owner and instance head have different arity"));
      std::unordered_map<tc::TypeId, tc::MonoTypePtr> TraitSubstitutions;
      for (std::size_t Argument = 0; Argument < Input.Arguments.size();
           ++Argument)
        TraitSubstitutions.emplace(
            TraitScopeInput->second->TypeParameters[Argument],
            Input.Arguments[Argument]);
      if (auto Valid = validateTargetSignature(
              *TraitMethods->second.at(Method.Method), Method.Target,
              Input.Trait, TraitSubstitutions, Composite, Method.Range,
              "instances[" + std::to_string(Index) + "]/methods[" +
                  std::to_string(MethodIndex) + "]/target/signature");
          !Valid)
        return std::unexpected(Valid.error());
      Result.Methods.push_back({Method.Method, std::move(*Target)});
    }
    Output_.InterfaceSeed.Instances.push_back(std::move(Result));
    return {};
  }

  ProjectionResult<void>
  projectDerivation(const tc::DeriveRequestProjection &Input,
                    SourceRange SourceExtent) {
    if (!NominalArities_.contains(Input.Type))
      return std::unexpected(error(
          StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
          "derivations/type", "derive target is not a projected nominal"));
    if (const auto DeclaredRange = NominalRanges_.find(Input.Type);
        DeclaredRange != NominalRanges_.end() && Input.Range.isValid() &&
        DeclaredRange->second.isValid() &&
        Input.Range.Source != DeclaredRange->second.Source)
      return std::unexpected(error(
          StructuralProjectionErrorCode::InvalidInput, Input.Range,
          "derivations/range",
          "derive request and target declaration belong to different sources"));
    if (SourceExtent.isValid() && !containsRange(SourceExtent, Input.Range))
      return std::unexpected(
          error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                "derivations/range",
                "derive request range lies outside the checked source extent"));
    std::vector<model::NominalTypeKey> Traits(Input.Traits.begin(),
                                              Input.Traits.end());
    std::ranges::sort(Traits);
    if (std::ranges::adjacent_find(Traits) != Traits.end())
      return std::unexpected(error(
          StructuralProjectionErrorCode::InvalidInput, Input.Range,
          "derivations/traits", "derive request contains a duplicate trait"));
    for (const auto &Trait : Traits)
      if (!TraitKeys_.contains(Trait))
        return std::unexpected(error(
            StructuralProjectionErrorCode::UnresolvedIdentity, Input.Range,
            "derivations/traits", "derive trait is not a projected trait"));
      else if (!DerivableTraits_.contains(Trait))
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Input.Range,
                  "derivations/traits",
                  "trait is not in the explicit derivation catalog"));
    Output_.InterfaceSeed.Derivations.push_back(
        {Input.Type, std::move(Traits), Input.Range});
    return {};
  }

  ProjectionResult<void> validateSumInjections() {
    for (const auto &Pending : PendingSumInjections_) {
      auto NodeType = Types_.resolved(Pending.NodeType);
      auto Alternative = Types_.resolved(Pending.Alternative);
      auto Sum = Types_.resolved(Pending.Sum);
      if (!NodeType || !Alternative || !Sum)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Pending.Range,
                  Pending.Path, "sum injection contains an unresolved type"));
      auto Stored = Types_.preparedType(Pending.Sum);
      if (!Stored || !std::holds_alternative<model::SumType>(*Stored))
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Pending.Range,
            Pending.Path, "sum injection target is not the stored SumType"));
      const auto &Members = std::get<model::SumType>(*Stored).Alternatives;
      if (Members.size() < 2 || *Sum == *Alternative)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, Pending.Range,
                  Pending.Path,
                  "sum injection target does not normalize to a genuine sum"));
      if (std::ranges::find(Members, *Alternative) == Members.end())
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Pending.Range,
            Pending.Path,
            "sum injection alternative is not a normalized target member"));
      if (*NodeType != *Alternative)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Pending.Range,
            Pending.Path + "/node-type",
            "sum injection alternative must equal the node's projected type"));
    }
    return {};
  }

  ProjectionResult<void> validateKeyOperations() {
    for (const auto &Pending : PendingKeyOperations_) {
      const auto Key = Types_.resolved(Pending.Key);
      const auto Hash = Types_.resolved(Pending.HashKey);
      const auto Equals = Types_.resolved(Pending.EqualsKey);
      if (!Key || !Hash || !Equals || *Key != *Hash || *Key != *Equals)
        return std::unexpected(error(
            StructuralProjectionErrorCode::InvalidInput, Pending.Range,
            Pending.Path,
            "Hash and Eq evidence must select the exact collection key type"));
    }
    return {};
  }

  ProjectionResult<ProjectionScope *>
  requireScope(tc::ProjectionDeclarationId Id, model::BinderOwnerKind Kind,
               SourceRange Range, std::string Path) {
    const auto Found = ScopeById_.find(Id);
    if (Found == ScopeById_.end())
      return std::unexpected(error(StructuralProjectionErrorCode::InvalidParent,
                                   Range, std::move(Path),
                                   "declaration scope is absent"));
    if (Found->second->Kind != Kind)
      return std::unexpected(
          error(StructuralProjectionErrorCode::BinderKindMismatch, Range,
                std::move(Path), "declaration scope has the wrong owner kind"));
    return Found->second;
  }

  ProjectionResult<void> rewriteOutput(SourceRange FallbackRange) {
    auto Type = [&](model::TypeId &Id,
                    std::string_view Path) -> ProjectionResult<void> {
      auto Resolved = Types_.resolved(Id);
      if (!Resolved)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, FallbackRange,
                  std::string(Path), Resolved.error()));
      Id = *Resolved;
      return {};
    };
    auto Effect = [&](model::EffectRowId &Id,
                      std::string_view Path) -> ProjectionResult<void> {
      auto Resolved = Types_.resolved(Id);
      if (!Resolved)
        return std::unexpected(
            error(StructuralProjectionErrorCode::InvalidInput, FallbackRange,
                  std::string(Path), Resolved.error()));
      Id = *Resolved;
      return {};
    };
    std::function<ProjectionResult<void>(model::TypeConstraint &)> Constraint;
    Constraint = [&](model::TypeConstraint &Value) -> ProjectionResult<void> {
      for (auto &Argument : Value.Arguments)
        if (auto Result = Type(Argument, "constraint/argument"); !Result)
          return Result;
      return {};
    };
    std::function<ProjectionResult<void>(model::TraitTargetApplication &)>
        Target;
    Target =
        [&](model::TraitTargetApplication &Value) -> ProjectionResult<void> {
      for (auto &Argument : Value.TypeArguments)
        if (auto Result = Type(Argument, "target/type-argument"); !Result)
          return Result;
      for (auto &Argument : Value.EffectArguments)
        if (auto Result = Effect(Argument, "target/effect-argument"); !Result)
          return Result;
      return {};
    };
    std::function<ProjectionResult<void>(model::TraitSelectionEvidence &)>
        Evidence;
    Evidence =
        [&](model::TraitSelectionEvidence &Value) -> ProjectionResult<void> {
      if (auto *Deferred = std::get_if<model::DeferredTraitEvidence>(&Value))
        return Constraint(Deferred->Constraint);
      return Target(std::get<model::TraitTargetApplication>(Value));
    };
    auto Signature =
        [&](model::EffectOperationSignature &Value) -> ProjectionResult<void> {
      for (auto &ConstraintValue : Value.Constraints)
        if (auto Result = Constraint(ConstraintValue); !Result)
          return Result;
      for (auto &Parameter : Value.Parameters)
        if (auto Result = Type(Parameter.Type, "operation/parameter"); !Result)
          return Result;
      if (auto Result = Type(Value.Result, "operation/result"); !Result)
        return Result;
      return Effect(Value.Effects, "operation/effects");
    };
    auto TraitUse =
        [&](ProjectedTraitMethodUseFact &Value) -> ProjectionResult<void> {
      for (auto &Argument : Value.InstanceArguments)
        if (auto Result = Type(Argument, "trait/instance"); !Result)
          return Result;
      for (auto &Argument : Value.MethodTypeArguments)
        if (auto Result = Type(Argument, "trait/method-type"); !Result)
          return Result;
      for (auto &Argument : Value.MethodEffectArguments)
        if (auto Result = Effect(Argument, "trait/method-effect"); !Result)
          return Result;
      return Evidence(Value.Evidence);
    };
    auto ProjectedApplication =
        [&](ProjectedOperationApplication &Value) -> ProjectionResult<void> {
      for (auto &Argument : Value.TypeArguments)
        if (auto Result = Type(Argument, "occurrence/type-argument"); !Result)
          return Result;
      for (auto &Argument : Value.EffectArguments)
        if (auto Result = Effect(Argument, "occurrence/effect-argument");
            !Result)
          return Result;
      for (auto &Parameter : Value.Parameters)
        if (auto Result = Type(Parameter.Type, "occurrence/parameter"); !Result)
          return Result;
      return Type(Value.ResultType, "occurrence/result");
    };

    for (auto &Scope : Output_.InterfaceSeed.Scopes) {
      if (auto *Instance =
              std::get_if<model::InstanceBinderOwner>(&Scope.Identity)) {
        for (auto &Argument : Instance->Arguments)
          if (auto Result = Type(Argument, "scope/instance/argument"); !Result)
            return Result;
        for (auto &Value : Instance->Constraints)
          if (auto Result = Constraint(Value); !Result)
            return Result;
      }
    }
    for (auto &Function : Output_.Functions) {
      if (auto Result = Type(Function.Signature, "function/signature"); !Result)
        return Result;
      for (auto &Fact : Function.NodeFacts) {
        if (auto Result = Type(Fact.Type, "node/type"); !Result)
          return Result;
        if (auto Result = Effect(Fact.Effects, "node/effects"); !Result)
          return Result;
        if (Fact.Instantiation) {
          for (auto &Argument : Fact.Instantiation->TypeArguments)
            if (auto Result = Type(Argument, "node/instantiation/type");
                !Result)
              return Result;
          for (auto &Argument : Fact.Instantiation->EffectArguments)
            if (auto Result = Effect(Argument, "node/instantiation/effect");
                !Result)
              return Result;
        }
        if (Fact.OperationOccurrence) {
          if (auto *Perform = std::get_if<ProjectedPerformFact>(
                  &*Fact.OperationOccurrence)) {
            if (auto Result = ProjectedApplication(Perform->Application);
                !Result)
              return Result;
            if (auto Result = Type(Perform->ContinuationResultType,
                                   "node/perform/continuation");
                !Result)
              return Result;
            if (auto Result =
                    Effect(Perform->ResumeEffects, "node/perform/effects");
                !Result)
              return Result;
          } else {
            auto &Handler =
                std::get<ProjectedHandlerClauseFact>(*Fact.OperationOccurrence);
            if (auto Result = ProjectedApplication(Handler.Application);
                !Result)
              return Result;
            if (auto Result =
                    Type(Handler.SourceResumeType, "node/handler/resume");
                !Result)
              return Result;
            if (auto Result = Effect(Handler.ReinstatedResumeEffects,
                                     "node/handler/effects");
                !Result)
              return Result;
          }
        }
        if (Fact.TraitMethodUse)
          if (auto Result = TraitUse(*Fact.TraitMethodUse); !Result)
            return Result;
        if (Fact.KeyOperations) {
          if (auto Result = TraitUse(Fact.KeyOperations->Hash); !Result)
            return Result;
          if (auto Result = TraitUse(Fact.KeyOperations->Equals); !Result)
            return Result;
        }
        if (Fact.SumInjection) {
          if (auto Result =
                  Type(Fact.SumInjection->Alternative, "node/sum/alternative");
              !Result)
            return Result;
          if (auto Result = Type(Fact.SumInjection->Sum, "node/sum/target");
              !Result)
            return Result;
        }
      }
    }
    for (auto &Value : Output_.Operations)
      if (auto Result = Signature(Value); !Result)
        return Result;
    for (auto &Value : Output_.InterfaceSeed.Operations)
      if (auto Result = Signature(Value.Signature); !Result)
        return Result;
    for (auto &Nominal : Output_.InterfaceSeed.Nominals)
      for (auto &Constructor : Nominal.Constructors)
        for (auto &Field : Constructor.Fields)
          if (auto Result = Type(Field, "nominal/constructor/field"); !Result)
            return Result;
    for (auto &Trait : Output_.InterfaceSeed.Traits) {
      for (auto &Value : Trait.Superclasses)
        if (auto Result = Constraint(Value); !Result)
          return Result;
      for (auto &Method : Trait.Methods) {
        if (auto Result = Type(Method.Signature, "trait/method/signature");
            !Result)
          return Result;
        if (Method.DefaultTarget)
          if (auto Result = Target(*Method.DefaultTarget); !Result)
            return Result;
      }
    }
    for (auto &Instance : Output_.InterfaceSeed.Instances) {
      for (auto &Argument : Instance.Arguments)
        if (auto Result = Type(Argument, "instance/argument"); !Result)
          return Result;
      for (auto &Value : Instance.Constraints)
        if (auto Result = Constraint(Value); !Result)
          return Result;
      for (auto &Method : Instance.Methods)
        if (auto Result = Target(Method.Target); !Result)
          return Result;
    }
    return {};
  }

  StructuralTypeProjection &Owner_;
  struct PendingSumInjection {
    model::TypeId NodeType;
    model::TypeId Alternative;
    model::TypeId Sum;
    SourceRange Range;
    std::string Path;
  };
  struct PendingKeyOperation {
    model::TypeId Key;
    model::TypeId HashKey;
    model::TypeId EqualsKey;
    SourceRange Range;
    std::string Path;
  };
  model::TypeTableAppendTransaction Types_;
  model::StructuralDeclarationCatalog Declarations_;
  ProjectedSemanticBatch Output_;
  std::vector<std::unique_ptr<ProjectionScope>> ScopeStorage_;
  std::unordered_map<tc::ProjectionDeclarationId, ProjectionScope *> ScopeById_;
  std::unordered_map<tc::ProjectionDeclarationId,
                     const tc::DeclarationScopeProjection *>
      ScopeInputs_;
  std::unordered_map<tc::ProjectionDeclarationId, std::uint32_t>
      ScopeOutputIndex_;
  std::unordered_map<std::pair<const ProjectionScope *, tc::EffectVarId>,
                     model::EffectVariableKind, ScopeEffectKeyHasher>
      EffectKinds_;
  std::unordered_map<model::NominalTypeKey, std::size_t,
                     model::NominalTypeKeyHasher>
      NominalArities_;
  std::unordered_map<model::NominalTypeKey, SourceRange,
                     model::NominalTypeKeyHasher>
      NominalRanges_;
  std::unordered_map<model::NominalTypeKey, std::size_t,
                     model::NominalTypeKeyHasher>
      ResourceArities_;
  std::unordered_set<model::NominalTypeKey, model::NominalTypeKeyHasher>
      TraitKeys_;
  std::unordered_set<model::NominalTypeKey, model::NominalTypeKeyHasher>
      DerivableTraits_;
  std::unordered_map<model::NominalTypeKey, std::size_t,
                     model::NominalTypeKeyHasher>
      TraitArities_;
  std::unordered_map<model::NominalTypeKey, std::unordered_set<std::string>,
                     model::NominalTypeKeyHasher>
      TraitMethods_;
  std::unordered_map<
      model::NominalTypeKey,
      std::unordered_map<std::string, tc::ProjectionDeclarationId>,
      model::NominalTypeKeyHasher>
      TraitMethodScopes_;
  std::unordered_map<
      model::NominalTypeKey,
      std::unordered_map<std::string, const tc::TraitMethodProjection *>,
      model::NominalTypeKeyHasher>
      TraitMethodDeclarations_;
  std::unordered_map<model::EffectOperationKey,
                     std::pair<std::size_t, std::size_t>,
                     model::EffectOperationKeyHasher>
      OperationArities_;
  std::unordered_map<model::EffectOperationKey,
                     const tc::OperationSchemeProjection *,
                     model::EffectOperationKeyHasher>
      OperationDeclarations_;
  std::unordered_map<model::EffectOperationKey,
                     const tc::ImportedOperationDeclarationProjection *,
                     model::EffectOperationKeyHasher>
      ImportedOperationDeclarations_;
  std::unordered_map<model::FunctionDeclarationIdentity,
                     std::pair<std::size_t, std::size_t>,
                     model::FunctionDeclarationIdentityHasher>
      SchemeArities_;
  std::unordered_map<model::FunctionDeclarationIdentity,
                     const std::vector<tc::Constraint> *,
                     model::FunctionDeclarationIdentityHasher>
      SchemeConstraints_;
  std::unordered_map<model::FunctionDeclarationIdentity,
                     tc::ProjectionDeclarationId,
                     model::FunctionDeclarationIdentityHasher>
      SchemeScopes_;
  std::unordered_map<model::FunctionDeclarationIdentity,
                     const tc::FunctionSchemeProjection *,
                     model::FunctionDeclarationIdentityHasher>
      FunctionDeclarations_;
  std::unordered_map<model::FunctionDeclarationIdentity,
                     const tc::ImportedFunctionDeclarationProjection *,
                     model::FunctionDeclarationIdentityHasher>
      ImportedFunctionDeclarations_;
  std::vector<PendingSumInjection> PendingSumInjections_;
  std::vector<PendingKeyOperation> PendingKeyOperations_;

  ProjectionResult<void>
  indexDeclarations(const tc::SemanticProjectionBatch &Batch) {
    const auto Conflict = [&](SourceRange Range, std::string Path,
                              std::string Message) -> ProjectionResult<void> {
      return std::unexpected(
          error(StructuralProjectionErrorCode::UnresolvedIdentity, Range,
                std::move(Path), std::move(Message)));
    };
    for (const auto &Nominal : Batch.Nominals) {
      const auto Scope =
          std::ranges::find_if(Batch.Scopes, [&](const auto &Value) {
            return Value.Id == Nominal.Scope;
          });
      if (Scope != Batch.Scopes.end()) {
        if (ResourceArities_.contains(Nominal.Key) ||
            TraitArities_.contains(Nominal.Key) ||
            !NominalArities_
                 .try_emplace(Nominal.Key, Scope->TypeParameters.size())
                 .second)
          return Conflict(Nominal.Range, "nominals/key",
                          "qualified declaration identity conflicts with "
                          "another declaration");
        NominalRanges_.emplace(Nominal.Key, Nominal.Range);
      }
    }
    for (const auto &Resource : Batch.Resources) {
      if (NominalArities_.contains(Resource.Key) ||
          TraitArities_.contains(Resource.Key) ||
          !ResourceArities_
               .try_emplace(Resource.Key, Resource.TypeParameters.size())
               .second)
        return Conflict(Resource.Range, "resources/key",
                        "qualified declaration identity conflicts with another "
                        "declaration");
    }
    for (const auto &Trait : Batch.Traits) {
      const auto Scope =
          std::ranges::find_if(Batch.Scopes, [&](const auto &Value) {
            return Value.Id == Trait.Scope;
          });
      const auto Arity =
          Scope == Batch.Scopes.end() ? 0 : Scope->TypeParameters.size();
      if (NominalArities_.contains(Trait.Key) ||
          ResourceArities_.contains(Trait.Key) ||
          !TraitArities_.try_emplace(Trait.Key, Arity).second)
        return Conflict(Trait.Range, "traits/key",
                        "qualified declaration identity conflicts with another "
                        "declaration");
      TraitKeys_.insert(Trait.Key);
      if (Trait.Derivable)
        DerivableTraits_.insert(Trait.Key);
      auto &Methods = TraitMethods_[Trait.Key];
      auto &MethodScopes = TraitMethodScopes_[Trait.Key];
      auto &MethodDeclarations = TraitMethodDeclarations_[Trait.Key];
      for (const auto &Method : Trait.Methods) {
        Methods.insert(Method.Name);
        MethodScopes.emplace(Method.Name, Method.Scope);
        MethodDeclarations.emplace(Method.Name, &Method);
      }
    }
    for (const auto &Operation : Batch.Operations)
      if (!OperationArities_
               .try_emplace(Operation.Key,
                            std::pair{Operation.TypeParameters.size(),
                                      Operation.EffectVariables.size()})
               .second)
        return Conflict(
            Operation.Range, "operations/key",
            "qualified operation identity conflicts with another declaration");
      else
        OperationDeclarations_.emplace(Operation.Key, &Operation);
    const auto RememberScheme = [&](tc::ProjectionDeclarationId ScopeId,
                                    const tc::TypeScheme &Scheme) {
      const auto Scope = std::ranges::find_if(
          Batch.Scopes, [&](const auto &Value) { return Value.Id == ScopeId; });
      if (Scope == Batch.Scopes.end())
        return;
      const auto *Identity =
          std::get_if<model::FunctionDeclarationIdentity>(&Scope->Identity);
      if (!Identity)
        return;
      const auto FlexibleEffects =
          static_cast<std::size_t>(std::ranges::count_if(
              Scheme.quantified_effect_variables, [](const auto &Variable) {
                return Variable.Kind == model::EffectVariableKind::Flexible;
              }));
      SchemeArities_.try_emplace(
          *Identity, std::pair{Scheme.quantified_vars.size(), FlexibleEffects});
      SchemeConstraints_.try_emplace(*Identity, &Scheme.constraints);
      SchemeScopes_.try_emplace(*Identity, ScopeId);
    };
    for (const auto &Function : Batch.Functions) {
      RememberScheme(Function.Scope, Function.Scheme);
      const auto Scope =
          std::ranges::find_if(Batch.Scopes, [&](const auto &Value) {
            return Value.Id == Function.Scope;
          });
      if (Scope != Batch.Scopes.end())
        if (const auto *Identity =
                std::get_if<model::FunctionDeclarationIdentity>(
                    &Scope->Identity))
          FunctionDeclarations_.try_emplace(*Identity, &Function);
    }
    for (const auto &Trait : Batch.Traits)
      for (const auto &Method : Trait.Methods)
        RememberScheme(Method.Scope, Method.Scheme);
    for (std::size_t Index = 0; Index < Batch.ImportedDeclarations.size();
         ++Index) {
      const auto &Import = Batch.ImportedDeclarations[Index];
      const auto Path = "imports[" + std::to_string(Index) + "]/identity";
      if (const auto *Key =
              std::get_if<model::NominalTypeKey>(&Import.Identity)) {
        if (NominalArities_.contains(*Key) || ResourceArities_.contains(*Key) ||
            TraitArities_.contains(*Key))
          return Conflict(Import.Range, Path,
                          "imported declaration conflicts with a local or "
                          "imported declaration");
        if (Import.Kind == model::BinderOwnerKind::Nominal) {
          if (Import.TypeArity > UINT32_MAX)
            return Conflict(Import.Range, Path,
                            "imported nominal arity exceeds the structural "
                            "catalog limit");
          NominalArities_.emplace(*Key, Import.TypeArity);
          if (auto Added =
                  Declarations_.addNominal(model::NominalDeclarationArity{
                      *Key, static_cast<std::uint32_t>(Import.TypeArity)});
              !Added)
            return Conflict(Import.Range, Path, Added.error());
        } else if (Import.Kind == model::BinderOwnerKind::Resource) {
          ResourceArities_.emplace(*Key, Import.TypeArity);
          if (Import.Resource) {
            if (Import.Resource->TypeParameters.size() != Import.TypeArity)
              return Conflict(
                  Import.Range, Path,
                  "imported resource contract disagrees with its binder arity");
          }
        } else {
          TraitKeys_.insert(*Key);
          TraitArities_.emplace(*Key, Import.TypeArity);
          if (Import.Derivable)
            DerivableTraits_.insert(*Key);
          TraitMethods_[*Key].insert(Import.TraitMethods.begin(),
                                     Import.TraitMethods.end());
        }
      } else if (const auto *Identity =
                     std::get_if<model::FunctionDeclarationIdentity>(
                         &Import.Identity)) {
        if (!SchemeArities_
                 .try_emplace(*Identity,
                              std::pair{Import.TypeArity, Import.EffectArity})
                 .second)
          return Conflict(Import.Range, Path,
                          "imported function conflicts with a local or "
                          "imported declaration");
        if (Import.Function) {
          const auto FlexibleEffects =
              static_cast<std::size_t>(std::ranges::count_if(
                  Import.Function->Scheme.quantified_effect_variables,
                  [](const auto &Variable) {
                    return Variable.Kind == model::EffectVariableKind::Flexible;
                  }));
          if (Import.Function->Scheme.quantified_vars.size() !=
                  Import.TypeArity ||
              FlexibleEffects != Import.EffectArity)
            return Conflict(
                Import.Range, Path,
                "imported function contract disagrees with its binder arity");
          ImportedFunctionDeclarations_.emplace(*Identity, &*Import.Function);
        }
      } else {
        const auto &Key = std::get<model::EffectOperationKey>(Import.Identity);
        if (!OperationArities_
                 .try_emplace(Key,
                              std::pair{Import.TypeArity, Import.EffectArity})
                 .second)
          return Conflict(Import.Range, Path,
                          "imported operation conflicts with a local or "
                          "imported declaration");
        if (Import.Operation) {
          const auto FlexibleEffects =
              static_cast<std::size_t>(std::ranges::count_if(
                  Import.Operation->EffectVariables, [](const auto &Variable) {
                    return Variable.Kind == model::EffectVariableKind::Flexible;
                  }));
          if (std::ranges::any_of(
                  Import.Operation->EffectVariables, [](const auto &Variable) {
                    return Variable.Kind != model::EffectVariableKind::Flexible;
                  }))
            return std::unexpected(error(
                StructuralProjectionErrorCode::BinderKindMismatch, Import.Range,
                "imports[" + std::to_string(Index) +
                    "]/operation/effect-binders",
                "imported operation effect binders must be flexible"));
          if (Import.Operation->TypeParameters.size() != Import.TypeArity ||
              FlexibleEffects != Import.EffectArity)
            return Conflict(
                Import.Range, Path,
                "imported operation contract disagrees with its binder arity");
          ImportedOperationDeclarations_.emplace(Key, &*Import.Operation);
        }
      }
    }
    return {};
  }
};

StructuralTypeProjection::StructuralTypeProjection(
    model::TypeTable &Types, const model::ModuleIdentity &Module,
    const tc::EffectSolver &Effects, const tc::EffectAtomCatalog &EffectAtoms)
    : Types_(Types), Module_(Module), Effects_(Effects),
      EffectAtoms_(EffectAtoms) {}

std::expected<model::TypeId, StructuralProjectionError>
StructuralTypeProjection::freezeClosedMonotype(tc::MonoTypePtr Type,
                                               SourceRange Range) {
  try {
    if (const auto Found = ClosedTypeMemo_.find(Type);
        Found != ClosedTypeMemo_.end() && Types_.valid(Found->second))
      return Found->second;
    StructuralProjectionTransaction Transaction(*this);
    auto Result = Transaction.projectClosed(Type, Range);
    if (Result)
      ClosedTypeMemo_.emplace(Type, *Result);
    return Result;
  } catch (const std::invalid_argument &) {
    return std::unexpected(error(StructuralProjectionErrorCode::ForeignEffect,
                                 Range, "type/projection",
                                 std::string(InvalidEffectReferenceMessage)));
  } catch (const std::exception &) {
    return std::unexpected(error(StructuralProjectionErrorCode::InvalidInput,
                                 Range, "type/projection",
                                 "structural type projection failed"));
  }
}

std::expected<ProjectedSemanticBatch, std::vector<StructuralProjectionError>>
StructuralTypeProjection::freezeSemanticBatch(
    const tc::SemanticProjectionBatch &Batch) {
  try {
    StructuralProjectionTransaction Transaction(*this);
    auto Result = Transaction.project(Batch);
    if (!Result)
      return std::unexpected(
          std::vector<StructuralProjectionError>{std::move(Result.error())});
    return std::move(*Result);
  } catch (const std::invalid_argument &) {
    return std::unexpected(std::vector<StructuralProjectionError>{
        error(StructuralProjectionErrorCode::ForeignEffect, Batch.SourceExtent,
              "batch/projection", std::string(InvalidEffectReferenceMessage))});
  } catch (const std::exception &) {
    return std::unexpected(std::vector<StructuralProjectionError>{
        error(StructuralProjectionErrorCode::InvalidInput, Batch.SourceExtent,
              "batch/projection", "structural semantic projection failed")});
  }
}

std::expected<model::EffectRowId, StructuralProjectionError>
StructuralTypeProjection::freezeEffect(tc::EffectRef Effect,
                                       ProjectionScope &Scope,
                                       SourceRange Range,
                                       std::string_view Path) {
  if (!Scope.Transaction)
    return std::unexpected(error(
        StructuralProjectionErrorCode::InvalidInput, Range, std::string(Path),
        "projection scope is detached from its transaction"));
  return Scope.Transaction->projectEffect(Effect, Scope, Range,
                                          std::string(Path));
}

} // namespace yona::semantics
