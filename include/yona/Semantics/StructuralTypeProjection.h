#ifndef YONA_SEMANTICS_STRUCTURALTYPEPROJECTION_H
#define YONA_SEMANTICS_STRUCTURALTYPEPROJECTION_H

#include "yona/Model/InferType.h"
#include "yona/Model/StructuralType.h"
#include "yona/Support/SourceManager.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace yona::ast {
class AstNode;
}

namespace yona::compiler::typechecker {

struct CallableTypeContract {
  MonoTypePtr Arrow = nullptr;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  model::CallingConvention Convention = model::CallingConvention::DirectYona;
  SourceRange Range = SourceRange::unknown();
};

using SchemeIdentity = model::FunctionDeclarationIdentity;
struct SchemeInstantiationProjection {
  SchemeIdentity Scheme;
  std::vector<MonoTypePtr> TypeArguments;
  std::vector<EffectRef> EffectArguments;
  SourceRange Range = SourceRange::unknown();
};
struct ResolvedOperationApplicationProjection {
  model::EffectOperationKey Key;
  std::vector<MonoTypePtr> TypeArguments;
  std::vector<EffectRef> EffectArguments;
  std::vector<MonoTypePtr> ParameterTypes;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  MonoTypePtr ResultType = nullptr;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
};
struct PerformProjection {
  ResolvedOperationApplicationProjection Application;
  MonoTypePtr ContinuationResultType = nullptr;
  EffectRef ResumeEffects;
  SourceRange Range = SourceRange::unknown();
};
struct HandlerClauseProjection {
  ResolvedOperationApplicationProjection Application;
  MonoTypePtr SourceResumeType = nullptr;
  EffectRef ReinstatedResumeEffects;
  SourceRange Range = SourceRange::unknown();
};
using OperationOccurrenceProjection =
    std::variant<PerformProjection, HandlerClauseProjection>;
struct DeferredTraitEvidenceProjection {
  SchemeIdentity OwningScheme;
  yona::compiler::typechecker::Constraint Constraint;
};
struct ResolvedTraitTargetProjection {
  SchemeIdentity SelectedTarget;
  std::vector<MonoTypePtr> TargetTypeArguments;
  std::vector<EffectRef> TargetEffectArguments;
};
using TraitSelectionEvidenceProjection =
    std::variant<DeferredTraitEvidenceProjection,
                 ResolvedTraitTargetProjection>;
struct TraitMethodSelectionProjection {
  model::NominalTypeKey Trait;
  std::string Method;
  std::vector<MonoTypePtr> InstanceArguments;
  std::vector<MonoTypePtr> MethodTypeArguments;
  std::vector<EffectRef> MethodEffectArguments;
  TraitSelectionEvidenceProjection Evidence;
  SourceRange Range = SourceRange::unknown();
};
struct CollectionKeyOperationsProjection {
  TraitMethodSelectionProjection Hash;
  TraitMethodSelectionProjection Equals;
};
struct SumInjectionProjection {
  MonoTypePtr Alternative = nullptr;
  MonoTypePtr Sum = nullptr;
};
struct NodeSemanticProjection {
  const ast::AstNode *Node = nullptr;
  MonoTypePtr Type = nullptr;
  EffectRef Effects;
  std::optional<SchemeInstantiationProjection> Instantiation;
  std::optional<OperationOccurrenceProjection> OperationOccurrence;
  std::optional<TraitMethodSelectionProjection> TraitMethodUse;
  std::optional<CollectionKeyOperationsProjection> KeyOperations;
  std::optional<SumInjectionProjection> SumInjection;
  SourceRange Range = SourceRange::unknown();
};

using ProjectionDeclarationId = std::uint32_t;
struct InstanceScopeIdentityProjection {
  model::NominalTypeKey Trait;
  std::vector<MonoTypePtr> Arguments;
  std::vector<Constraint> Constraints;
};
using ProjectionDeclarationIdentity =
    std::variant<model::NominalTypeKey, model::FunctionDeclarationIdentity,
                 InstanceScopeIdentityProjection, model::EffectOperationKey>;
enum class DeclarationVisibility : std::uint8_t {
  Private = 0,
  Module = 1,
  Public = 2
};
struct DeclarationScopeProjection {
  ProjectionDeclarationId Id = 0;
  std::optional<ProjectionDeclarationId> Parent;
  model::BinderOwnerKind Kind = model::BinderOwnerKind::Function;
  ProjectionDeclarationIdentity Identity;
  std::span<const TypeId> TypeParameters;
  std::span<const QuantifiedEffectVariable> EffectVariables;
  std::span<const CallableTypeContract> CallableContracts;
  SourceRange Range = SourceRange::unknown();
};
struct FunctionSchemeProjection {
  ProjectionDeclarationId Scope = 0;
  const TypeScheme &Scheme;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  model::CallingConvention Convention = model::CallingConvention::DirectYona;
  std::optional<model::FunctionDeclarationIdentity> SourceIdentity;
  std::optional<model::SymbolIdentity> ExportedSymbol;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  std::span<const NodeSemanticProjection> NodeFacts;
  SourceRange Range = SourceRange::unknown();
  const ast::AstNode *SourceOwner = nullptr;
};
struct OperationSchemeProjection {
  ProjectionDeclarationId Scope = 0;
  model::EffectOperationKey Key;
  std::span<const TypeId> TypeParameters;
  std::span<const QuantifiedEffectVariable> EffectVariables;
  std::span<const Constraint> Constraints;
  std::span<const MonoTypePtr> ParameterTypes;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  MonoTypePtr ResultType = nullptr;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  EffectRef Effects;
  SourceRange Range = SourceRange::unknown();
};
struct ConstructorProjection {
  std::string Name;
  std::uint32_t Tag = 0;
  std::span<const MonoTypePtr> Fields;
  SourceRange Range = SourceRange::unknown();
};
struct NominalDeclarationProjection {
  ProjectionDeclarationId Scope = 0;
  model::NominalTypeKey Key;
  std::span<const ConstructorProjection> Constructors;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  bool Opaque = false;
  SourceRange Range = SourceRange::unknown();
};
struct ResourceDeclarationProjection {
  ProjectionDeclarationId Scope = 0;
  model::NominalTypeKey Key;
  std::span<const TypeId> TypeParameters;
  model::ResourceAbiKind AbiKind = model::ResourceAbiKind::GenericResource;
  model::ResourceShareability Shareability =
      model::ResourceShareability::Linear;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  SourceRange Range = SourceRange::unknown();
};
struct TraitMethodProjection {
  ProjectionDeclarationId Scope = 0;
  std::string Name;
  const TypeScheme &Scheme;
  std::span<const model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  model::CallingConvention Convention = model::CallingConvention::DirectYona;
  std::optional<ResolvedTraitTargetProjection> DefaultTarget;
  SourceRange Range = SourceRange::unknown();
};
struct TraitDeclarationProjection {
  ProjectionDeclarationId Scope = 0;
  model::NominalTypeKey Key;
  std::span<const Constraint> Superclasses;
  std::span<const TraitMethodProjection> Methods;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  SourceRange Range = SourceRange::unknown();
  bool Derivable = false;
};
struct TraitMethodBindingProjection {
  ProjectionDeclarationId InstanceScope = 0;
  ProjectionDeclarationId MethodScope = 0;
  std::string Method;
  ResolvedTraitTargetProjection Target;
  SourceRange Range = SourceRange::unknown();
};
struct TraitInstanceProjection {
  ProjectionDeclarationId Scope = 0;
  model::NominalTypeKey Trait;
  std::span<const MonoTypePtr> Arguments;
  std::span<const Constraint> Constraints;
  std::span<const TraitMethodBindingProjection> Methods;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  SourceRange Range = SourceRange::unknown();
};
struct DeriveRequestProjection {
  model::NominalTypeKey Type;
  std::span<const model::NominalTypeKey> Traits;
  SourceRange Range = SourceRange::unknown();
};
struct ImportedFunctionDeclarationProjection {
  TypeScheme Scheme;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  model::CallingConvention Convention = model::CallingConvention::DirectYona;
  std::vector<CallableTypeContract> CallableContracts;
};
struct ImportedOperationDeclarationProjection {
  std::vector<TypeId> TypeParameters;
  std::vector<QuantifiedEffectVariable> EffectVariables;
  std::vector<Constraint> Constraints;
  std::vector<MonoTypePtr> ParameterTypes;
  std::vector<model::ParameterOwnership> ParameterOwnerships;
  MonoTypePtr ResultType = nullptr;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
  EffectRef Effects;
  std::vector<CallableTypeContract> CallableContracts;
};
struct ImportedResourceDeclarationProjection {
  std::vector<TypeId> TypeParameters;
  model::ResourceAbiKind AbiKind = model::ResourceAbiKind::GenericResource;
  model::ResourceShareability Shareability =
      model::ResourceShareability::Linear;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
};
/// Borrowed declaration-domain seed for qualified declarations supplied by
/// imported interfaces. Each entry is owned by the TypeChecker storage and the
/// enclosing batch borrows only the entry span; structural projection copies
/// the values it needs before publishing a durable SemanticModel.
struct ImportedDeclarationCatalogEntry {
  model::BinderOwnerKind Kind = model::BinderOwnerKind::Nominal;
  ProjectionDeclarationIdentity Identity;
  std::size_t TypeArity = 0;
  std::size_t EffectArity = 0;
  std::vector<std::string> TraitMethods;
  bool Derivable = false;
  SourceRange Range = SourceRange::unknown();
  std::optional<ImportedFunctionDeclarationProjection> Function;
  std::optional<ImportedOperationDeclarationProjection> Operation;
  std::optional<ImportedResourceDeclarationProjection> Resource;
};
struct SemanticProjectionBatch {
  std::span<const DeclarationScopeProjection> Scopes;
  std::span<const FunctionSchemeProjection> Functions;
  std::span<const OperationSchemeProjection> Operations;
  std::span<const NominalDeclarationProjection> Nominals;
  std::span<const ResourceDeclarationProjection> Resources;
  std::span<const TraitDeclarationProjection> Traits;
  std::span<const TraitInstanceProjection> Instances;
  std::span<const DeriveRequestProjection> Derivations;
  std::span<const ImportedDeclarationCatalogEntry> ImportedDeclarations{};
  const ast::AstNode *SourceRoot = nullptr;
  SourceRange SourceExtent = SourceRange::unknown();
};

} // namespace yona::compiler::typechecker

namespace yona::semantics {

using DeclarationVisibility = compiler::typechecker::DeclarationVisibility;
using ProjectionDeclarationId = compiler::typechecker::ProjectionDeclarationId;
using SchemeIdentity = compiler::typechecker::SchemeIdentity;

struct ProjectedSchemeInstantiation {
  SchemeIdentity Scheme;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
};
struct ProjectedOperationApplication {
  model::EffectOperationKey Key;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<model::FunctionParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract = model::ResultOwnership::Trivial;
};
struct ProjectedPerformFact {
  ProjectedOperationApplication Application;
  model::TypeId ContinuationResultType;
  model::EffectRowId ResumeEffects;
};
struct ProjectedHandlerClauseFact {
  ProjectedOperationApplication Application;
  model::TypeId SourceResumeType;
  model::EffectRowId ReinstatedResumeEffects;
};
using ProjectedOperationOccurrence =
    std::variant<ProjectedPerformFact, ProjectedHandlerClauseFact>;
struct ProjectedTraitMethodUseFact {
  model::NominalTypeKey Trait;
  std::string Method;
  std::vector<model::TypeId> InstanceArguments;
  std::vector<model::TypeId> MethodTypeArguments;
  std::vector<model::EffectRowId> MethodEffectArguments;
  model::TraitSelectionEvidence Evidence;
};
struct ProjectedCollectionKeyOperations {
  ProjectedTraitMethodUseFact Hash;
  ProjectedTraitMethodUseFact Equals;
};
struct ProjectedSumInjection {
  model::TypeId Alternative;
  model::TypeId Sum;
};
struct ProjectedNodeFact {
  const ast::AstNode *Node = nullptr;
  model::TypeId Type;
  model::EffectRowId Effects;
  std::optional<ProjectedSchemeInstantiation> Instantiation;
  std::optional<ProjectedOperationOccurrence> OperationOccurrence;
  std::optional<ProjectedTraitMethodUseFact> TraitMethodUse;
  std::optional<ProjectedCollectionKeyOperations> KeyOperations;
  std::optional<ProjectedSumInjection> SumInjection;
};
struct ProjectedFunction {
  ProjectionDeclarationId Scope = 0;
  model::TypeId Signature;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  std::vector<model::TypeParameterId> InheritedTypeParameters;
  std::vector<model::EffectVariableDeclaration> InheritedEffectVariables;
  std::vector<model::TypeParameterId> DeclaredTypeParameters;
  std::vector<model::EffectVariableDeclaration> DeclaredEffectVariables;
  std::vector<ProjectedNodeFact> NodeFacts;
};
struct ProjectedDeclarationScope {
  model::BinderOwnerKind Kind = model::BinderOwnerKind::Function;
  model::CanonicalBinderOwnerIdentity Identity;
  std::optional<std::uint32_t> Parent;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
};
struct ProjectedConstructor {
  std::string Name;
  std::uint32_t Tag = 0;
  std::vector<model::TypeId> Fields;
};
struct ProjectedNominalDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<ProjectedConstructor> Constructors;
  DeclarationVisibility Access = DeclarationVisibility::Private;
  bool Opaque = false;
};
struct ProjectedResourceDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  model::ResourceAbiKind AbiKind = model::ResourceAbiKind::GenericResource;
  model::ResourceShareability Shareability =
      model::ResourceShareability::Linear;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  DeclarationVisibility Access = DeclarationVisibility::Private;
};
struct ProjectedTraitMethod {
  std::string Name;
  model::TypeId Signature;
  std::optional<model::TraitTargetApplication> DefaultTarget;
};
struct ProjectedTraitDeclaration {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::TypeConstraint> Superclasses;
  std::vector<ProjectedTraitMethod> Methods;
  DeclarationVisibility Access = DeclarationVisibility::Private;
};
struct ProjectedTraitMethodBinding {
  std::string Method;
  model::TraitTargetApplication Target;
};
struct ProjectedTraitInstance {
  model::NominalTypeKey Trait;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::TypeId> Arguments;
  std::vector<model::TypeConstraint> Constraints;
  std::vector<ProjectedTraitMethodBinding> Methods;
  DeclarationVisibility Access = DeclarationVisibility::Private;
};
struct ProjectedOperationDeclaration {
  model::EffectOperationSignature Signature;
  DeclarationVisibility Access = DeclarationVisibility::Private;
};
struct ProjectedDeriveRequest {
  model::NominalTypeKey Type;
  std::vector<model::NominalTypeKey> Traits;
  SourceRange Range = SourceRange::unknown();
};
struct ProjectedInterfaceSeed {
  std::vector<ProjectedDeclarationScope> Scopes;
  std::vector<ProjectedNominalDeclaration> Nominals;
  std::vector<ProjectedResourceDeclaration> Resources;
  std::vector<ProjectedTraitDeclaration> Traits;
  std::vector<ProjectedTraitInstance> Instances;
  std::vector<ProjectedOperationDeclaration> Operations;
  std::vector<ProjectedDeriveRequest> Derivations;
};
struct ProjectedSemanticBatch {
  std::vector<ProjectedFunction> Functions;
  std::vector<model::EffectOperationSignature> Operations;
  ProjectedInterfaceSeed InterfaceSeed;
};

enum class StructuralProjectionErrorCode : std::uint8_t {
  InvalidInput,
  WrongArity,
  WrongArrowShape,
  ForeignEffect,
  MissingEffectAtom,
  UnboundBinder,
  BinderKindMismatch,
  MissingCallableContract,
  DuplicateCallableContract,
  OpenValueType,
  InvalidRecordRow,
  InvalidParent,
  CyclicParent,
  DuplicateNode,
  UnresolvedIdentity,
  AmbiguousInstantiation
};
struct StructuralProjectionError {
  StructuralProjectionErrorCode Code =
      StructuralProjectionErrorCode::InvalidInput;
  SourceRange Range = SourceRange::unknown();
  std::string Path;
  std::string Message;
};

class StructuralTypeProjection final {
public:
  StructuralTypeProjection(
      model::TypeTable &Types, const model::ModuleIdentity &Module,
      const compiler::typechecker::EffectSolver &Effects,
      const compiler::typechecker::EffectAtomCatalog &EffectAtoms);

  [[nodiscard]] std::expected<model::TypeId, StructuralProjectionError>
  freezeClosedMonotype(compiler::typechecker::MonoTypePtr Type,
                       SourceRange Range);
  [[nodiscard]] std::expected<ProjectedSemanticBatch,
                              std::vector<StructuralProjectionError>>
  freezeSemanticBatch(
      const compiler::typechecker::SemanticProjectionBatch &Batch);

private:
  struct ProjectionScope;
  class StructuralProjectionTransaction;

  [[nodiscard]] std::expected<model::EffectRowId, StructuralProjectionError>
  freezeEffect(compiler::typechecker::EffectRef Effect, ProjectionScope &Scope,
               SourceRange Range, std::string_view Path);

  model::TypeTable &Types_;
  model::ModuleIdentity Module_;
  const compiler::typechecker::EffectSolver &Effects_;
  const compiler::typechecker::EffectAtomCatalog &EffectAtoms_;
  model::StructuralDeclarationCatalog Declarations_;
  std::unordered_map<const compiler::typechecker::MonoType *, model::TypeId>
      ClosedTypeMemo_;
};

} // namespace yona::semantics

#endif /* YONA_SEMANTICS_STRUCTURALTYPEPROJECTION_H */
