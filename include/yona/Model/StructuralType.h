#ifndef YONA_MODEL_STRUCTURALTYPE_H
#define YONA_MODEL_STRUCTURALTYPE_H

#include "yona/Model/ModuleIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace yona::model {

using IdDomain = std::uint64_t;
inline constexpr IdDomain InvalidIdDomain = 0;

template <class Tag> class StrongId final {
public:
  constexpr StrongId() noexcept = default;
  constexpr StrongId(IdDomain Domain, std::uint32_t Value) noexcept
      : Domain_(Domain), Value_(Value) {}

  [[nodiscard]] constexpr bool isValid() const noexcept {
    return Domain_ != InvalidIdDomain &&
           Value_ != std::numeric_limits<std::uint32_t>::max();
  }
  [[nodiscard]] constexpr IdDomain domain() const noexcept { return Domain_; }
  [[nodiscard]] constexpr std::uint32_t value() const noexcept {
    return Value_;
  }

  friend constexpr bool operator==(StrongId, StrongId) noexcept = default;
  friend constexpr auto operator<=>(StrongId, StrongId) noexcept = default;

private:
  IdDomain Domain_ = InvalidIdDomain;
  std::uint32_t Value_ = std::numeric_limits<std::uint32_t>::max();
};

struct StrongIdHasher final {
  template <class Tag>
  [[nodiscard]] std::size_t operator()(StrongId<Tag> Id) const noexcept {
    auto Seed = std::hash<IdDomain>{}(Id.domain());
    Seed ^= std::hash<std::uint32_t>{}(Id.value()) +
            static_cast<std::size_t>(0x9e3779b9u) + (Seed << 6u) + (Seed >> 2u);
    return Seed;
  }
};

using TypeId = StrongId<struct TypeIdTag>;
using EffectRowId = StrongId<struct EffectRowIdTag>;
using TypeParameterId = StrongId<struct TypeParameterIdTag>;
using EffectVariableId = StrongId<struct EffectVariableIdTag>;

enum class PrimitiveType : std::uint8_t {
  Unit = 0,
  Bool = 1,
  Int = 2,
  Float = 3,
  Char = 4,
  String = 5,
  Symbol = 6,
  Byte = 7
};
enum class ParameterOwnership : std::uint8_t {
  Trivial = 0,
  Borrow = 1,
  Consume = 2
};
enum class ResultOwnership : std::uint8_t { Trivial = 0, Owned = 1 };
enum class CallingConvention : std::uint8_t {
  DirectYona = 0,
  ClosureEntry = 1,
  Continuation = 2,
  EffectOperation = 3,
  AsyncAdapter = 4,
  NativeExtern = 5,
  ExportedC = 6
};

struct NominalTypeKey {
  ModuleIdentity Module;
  std::string Name;
  friend bool operator==(const NominalTypeKey &,
                         const NominalTypeKey &) = default;
  friend auto operator<=>(const NominalTypeKey &,
                          const NominalTypeKey &) = default;
};
struct NominalTypeKeyHasher final {
  [[nodiscard]] std::size_t
  operator()(const NominalTypeKey &Key) const noexcept;
};

struct SymbolIdentity {
  ModuleIdentity Module;
  std::string Name;
  friend bool operator==(const SymbolIdentity &,
                         const SymbolIdentity &) = default;
  friend auto operator<=>(const SymbolIdentity &,
                          const SymbolIdentity &) = default;
};
struct SymbolIdentityHasher final {
  [[nodiscard]] std::size_t
  operator()(const SymbolIdentity &Identity) const noexcept;
};

struct LocalFunctionIdentity {
  ModuleIdentity Module;
  std::vector<std::uint32_t> LexicalDeclarationPath;
  friend bool operator==(const LocalFunctionIdentity &,
                         const LocalFunctionIdentity &) = default;
  friend auto operator<=>(const LocalFunctionIdentity &,
                          const LocalFunctionIdentity &) = default;
};
using FunctionDeclarationIdentity =
    std::variant<SymbolIdentity, LocalFunctionIdentity>;
struct FunctionDeclarationIdentityHasher final {
  [[nodiscard]] std::size_t
  operator()(const FunctionDeclarationIdentity &Identity) const noexcept;
};

struct TypeParameterType {
  TypeParameterId Parameter;
  friend bool operator==(const TypeParameterType &,
                         const TypeParameterType &) = default;
};
struct SumType {
  std::vector<TypeId> Alternatives;
  friend bool operator==(const SumType &, const SumType &) = default;
};
struct TupleType {
  std::vector<TypeId> Elements;
  friend bool operator==(const TupleType &, const TupleType &) = default;
};
struct SequenceType {
  TypeId Element;
  friend bool operator==(const SequenceType &, const SequenceType &) = default;
};
struct SetType {
  TypeId Element;
  friend bool operator==(const SetType &, const SetType &) = default;
};
struct DictionaryType {
  TypeId Key;
  TypeId Value;
  friend bool operator==(const DictionaryType &,
                         const DictionaryType &) = default;
};
struct RecordField {
  std::string Name;
  TypeId Type;
  friend bool operator==(const RecordField &, const RecordField &) = default;
};
struct RecordType {
  std::vector<RecordField> Fields;
  std::optional<TypeId> RowRest;
  friend bool operator==(const RecordType &, const RecordType &) = default;
};
struct NominalType {
  NominalTypeKey Key;
  std::vector<TypeId> Arguments;
  friend bool operator==(const NominalType &, const NominalType &) = default;
};
struct ArrayType {
  TypeId Element;
  bool Mutable;
  friend bool operator==(const ArrayType &, const ArrayType &) = default;
};
struct ChannelType {
  TypeId Element;
  friend bool operator==(const ChannelType &, const ChannelType &) = default;
};
struct PromiseType {
  TypeId Element;
  ResultOwnership ResultContract;
  EffectRowId Effects;
  friend bool operator==(const PromiseType &, const PromiseType &) = default;
};
struct CursorType {
  TypeId Element;
  ResultOwnership ElementContract;
  friend bool operator==(const CursorType &, const CursorType &) = default;
};
struct CursorStepType {
  TypeId Element;
  ResultOwnership ElementContract;
  friend bool operator==(const CursorStepType &,
                         const CursorStepType &) = default;
};
struct OutcomeTaskGroupType {
  friend bool operator==(OutcomeTaskGroupType, OutcomeTaskGroupType) = default;
};
struct ResourceType {
  NominalTypeKey Key;
  std::vector<TypeId> Arguments;
  friend bool operator==(const ResourceType &, const ResourceType &) = default;
};
enum class ResourceAbiKind : std::uint8_t {
  GenericResource = 0,
  ChannelSender = 1,
  ChannelReceiver = 2
};
enum class ResourceShareability : std::uint8_t {
  Linear = 0,
  AlwaysShareable = 1
};
struct ResourceDeclaration {
  NominalTypeKey Key;
  std::vector<TypeParameterId> TypeParameters;
  ResourceAbiKind AbiKind;
  ResourceShareability Shareability;
  std::optional<std::string> TryRetainNativeSymbol;
  std::string ReleaseNativeSymbol;
  friend bool operator==(const ResourceDeclaration &,
                         const ResourceDeclaration &) = default;
};
struct NominalDeclarationArity {
  NominalTypeKey Key;
  std::uint32_t TypeParameterCount;
  friend bool operator==(const NominalDeclarationArity &,
                         const NominalDeclarationArity &) = default;
};

enum class AbiOpaqueKind : std::uint8_t {
  ExceptionValue = 0,
  EffectRequest = 1,
  ControlOutcome = 2,
  ExecutionContext = 3,
  CallableInvocationEnvironment = 4,
  ContinuationBoundaryContext = 5
};
struct AbiOpaqueType {
  AbiOpaqueKind Kind;
  friend bool operator==(const AbiOpaqueType &,
                         const AbiOpaqueType &) = default;
};
enum class OwnedSlotStateKind : std::uint8_t {
  HandlerRouter = 0,
  TryBoundary = 1,
  CleanupObligation = 2
};
struct OwnedSlotField {
  TypeId Type;
  ParameterOwnership Ownership;
  friend bool operator==(const OwnedSlotField &,
                         const OwnedSlotField &) = default;
};
struct OwnedSlotStateType {
  OwnedSlotStateKind Kind;
  std::vector<OwnedSlotField> Fields;
  FunctionDeclarationIdentity DropIdentity;
  friend bool operator==(const OwnedSlotStateType &,
                         const OwnedSlotStateType &) = default;
};
struct FunctionParameter {
  TypeId Type;
  ParameterOwnership Ownership;
  friend bool operator==(const FunctionParameter &,
                         const FunctionParameter &) = default;
};
struct EffectOperationKey {
  NominalTypeKey Effect;
  std::string Operation;
  friend bool operator==(const EffectOperationKey &,
                         const EffectOperationKey &) = default;
  friend auto operator<=>(const EffectOperationKey &,
                          const EffectOperationKey &) = default;
};
struct EffectOperationKeyHasher final {
  [[nodiscard]] std::size_t
  operator()(const EffectOperationKey &Key) const noexcept;
};
struct EffectOperationApplication {
  EffectOperationKey Key;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
  friend bool operator==(const EffectOperationApplication &,
                         const EffectOperationApplication &) = default;
};
struct TypeConstraint {
  TypeParameterId Parameter;
  NominalTypeKey Trait;
  std::vector<TypeId> Arguments;
  friend bool operator==(const TypeConstraint &,
                         const TypeConstraint &) = default;
};
struct TraitMethodKey {
  NominalTypeKey Trait;
  std::string Method;
  friend bool operator==(const TraitMethodKey &,
                         const TraitMethodKey &) = default;
};
struct TraitTargetApplication {
  FunctionDeclarationIdentity Target;
  std::vector<TypeId> TypeArguments;
  std::vector<EffectRowId> EffectArguments;
  friend bool operator==(const TraitTargetApplication &,
                         const TraitTargetApplication &) = default;
};
struct DeferredTraitEvidence {
  FunctionDeclarationIdentity OwningScheme;
  TypeConstraint Constraint;
  friend bool operator==(const DeferredTraitEvidence &,
                         const DeferredTraitEvidence &) = default;
};
using TraitSelectionEvidence =
    std::variant<DeferredTraitEvidence, TraitTargetApplication>;
struct TraitResolutionRequest {
  TraitMethodKey Method;
  std::vector<TypeId> InstanceArguments;
  std::vector<TypeId> MethodTypeArguments;
  std::vector<EffectRowId> MethodEffectArguments;
  TraitSelectionEvidence Evidence;
  friend bool operator==(const TraitResolutionRequest &,
                         const TraitResolutionRequest &) = default;
};
enum class EffectVariableKind : std::uint8_t { Flexible = 0, Opaque = 1 };
struct EffectVariableDeclaration {
  EffectVariableId Variable;
  EffectVariableKind Kind;
  friend bool operator==(const EffectVariableDeclaration &,
                         const EffectVariableDeclaration &) = default;
};
struct GenericBinderEnvironment {
  std::vector<TypeParameterId> InheritedTypeParameters;
  std::vector<EffectVariableDeclaration> InheritedEffectVariables;
  std::vector<TypeParameterId> DeclaredTypeParameters;
  std::vector<EffectVariableDeclaration> DeclaredEffectVariables;
  friend bool operator==(const GenericBinderEnvironment &,
                         const GenericBinderEnvironment &) = default;
};
struct FunctionType {
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
  std::vector<TypeConstraint> Constraints;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
  CallingConvention Convention;
  std::optional<FunctionDeclarationIdentity> SourceIdentity;
  std::optional<SymbolIdentity> ExportedSymbol;
  friend bool operator==(const FunctionType &, const FunctionType &) = default;
};
struct EffectOperationSignature {
  EffectOperationKey Key;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
  std::vector<TypeConstraint> Constraints;
  std::vector<FunctionParameter> Parameters;
  TypeId Result;
  ResultOwnership ResultContract;
  EffectRowId Effects;
  friend bool operator==(const EffectOperationSignature &,
                         const EffectOperationSignature &) = default;
};

using StructuralType =
    std::variant<PrimitiveType, TypeParameterType, TupleType, SequenceType,
                 SetType, DictionaryType, RecordType, NominalType, ArrayType,
                 ChannelType, PromiseType, CursorType, CursorStepType,
                 OutcomeTaskGroupType, ResourceType, AbiOpaqueType,
                 OwnedSlotStateType, FunctionType, SumType>;

struct EffectTailProjection {
  EffectVariableId Variable;
  EffectVariableKind Kind;
  std::vector<EffectOperationApplication> ExcludedOperations;
  friend bool operator==(const EffectTailProjection &,
                         const EffectTailProjection &) = default;
};
struct EffectRow {
  std::vector<EffectOperationApplication> Operations;
  std::vector<EffectTailProjection> Tails;
  bool MayRaise = false;
  bool MayCancel = false;
  friend bool operator==(const EffectRow &, const EffectRow &) = default;
};
enum class BinderOwnerKind : std::uint8_t {
  Nominal = 0,
  Trait = 1,
  Function = 2,
  Instance = 3,
  Operation = 4,
  Resource = 5
};
struct InstanceBinderOwner {
  NominalTypeKey Trait;
  std::vector<TypeId> Arguments;
  std::vector<TypeConstraint> Constraints;
  friend bool operator==(const InstanceBinderOwner &,
                         const InstanceBinderOwner &) = default;
};
using CanonicalBinderOwnerIdentity =
    std::variant<NominalTypeKey, FunctionDeclarationIdentity,
                 InstanceBinderOwner, EffectOperationKey>;
struct CanonicalBinderDeclaration {
  BinderOwnerKind Kind;
  CanonicalBinderOwnerIdentity Identity;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableDeclaration> EffectVariables;
  friend bool operator==(const CanonicalBinderDeclaration &,
                         const CanonicalBinderDeclaration &) = default;
};
struct BinderDeclarationPair {
  CanonicalBinderDeclaration Source;
  CanonicalBinderDeclaration Destination;
};
struct TypeRemap {
  std::vector<TypeId> Types;
  std::vector<EffectRowId> Effects;
  std::vector<TypeParameterId> TypeParameters;
  std::vector<EffectVariableId> EffectVariables;
};

class TypeTable;
class StructuralDeclarationCatalog final {
public:
  [[nodiscard]] std::expected<void, std::string>
  addNominal(NominalDeclarationArity Declaration);
  [[nodiscard]] std::expected<void, std::string>
  addResource(ResourceDeclaration Declaration);
  [[nodiscard]] std::optional<std::uint32_t>
  nominalArity(const NominalTypeKey &Key) const noexcept;
  [[nodiscard]] const ResourceDeclaration *
  resource(const NominalTypeKey &Key) const noexcept;
  [[nodiscard]] std::expected<void, std::string>
  validate(const TypeTable &Types,
           std::span<const TypeId> RuntimeRoots = {}) const;

private:
  std::deque<NominalDeclarationArity> Nominals_;
  std::deque<ResourceDeclaration> Resources_;
};

namespace test {
class StructuralTypeTestAccess;
}

class TypeTableAppendTransaction;
class TypeTable final {
public:
  TypeTable();
  ~TypeTable() = default;
  TypeTable(TypeTable &&Other) noexcept;
  TypeTable &operator=(TypeTable &&Other) noexcept;
  TypeTable(const TypeTable &) = delete;
  TypeTable &operator=(const TypeTable &) = delete;

  [[nodiscard]] IdDomain idDomain() const noexcept;
  TypeParameterId allocateTypeParameter();
  EffectVariableId allocateEffectVariable();
  [[nodiscard]] std::expected<TypeId, std::string>
  internSum(std::span<const TypeId> Alternatives);
  [[nodiscard]] std::expected<TypeId, std::string> intern(StructuralType Type);
  [[nodiscard]] std::expected<EffectRowId, std::string>
  internEffect(EffectRow Row);
  [[nodiscard]] TypeTableAppendTransaction beginAppendTransaction();
  [[nodiscard]] const StructuralType &at(TypeId Id) const;
  [[nodiscard]] const EffectRow &effect(EffectRowId Id) const;
  [[nodiscard]] bool valid(TypeId Id) const noexcept;
  [[nodiscard]] bool valid(EffectRowId Id) const noexcept;
  [[nodiscard]] bool valid(TypeParameterId Id) const noexcept;
  [[nodiscard]] bool valid(EffectVariableId Id) const noexcept;
  [[nodiscard]] std::size_t typeCount() const noexcept;
  [[nodiscard]] std::size_t effectCount() const noexcept;
  [[nodiscard]] std::expected<TypeRemap, std::string>
  remapInto(TypeTable &Destination,
            std::span<const BinderDeclarationPair> BinderPairs) const;
  [[nodiscard]] std::string format(TypeId Id) const;
  [[nodiscard]] std::expected<void, std::string> validate() const;

private:
  struct StoredType {
    StructuralType Value;
    std::string Key;
    std::size_t Hash = 0;
  };
  struct StoredEffect {
    EffectRow Value;
    std::string Key;
    std::size_t Hash = 0;
  };
  struct TestHookState {
    std::optional<std::size_t> FailAfter;
    std::optional<std::size_t> ConstantHash;
    std::optional<std::uint64_t> ConstantFingerprint;
  };

  [[nodiscard]] static bool faultPoint() noexcept;
  [[nodiscard]] static std::size_t hashKey(const std::string &Key) noexcept;

  IdDomain Domain_ = InvalidIdDomain;
  std::list<StoredType> TypeStorage_;
  std::list<StoredEffect> EffectStorage_;
  std::vector<StoredType *> Types_;
  std::vector<StoredEffect *> Effects_;
  std::uint32_t TypeParameterCount_ = 0;
  std::uint32_t EffectVariableCount_ = 0;
  std::uint64_t Generation_ = 0;
  std::vector<void *> ActiveTransactions_;
  void *PreparedTransaction_ = nullptr;

  static thread_local TestHookState TestHooks_;

  friend class TypeTableAppendTransaction;
  friend class StructuralDeclarationCatalog;
  friend class test::StructuralTypeTestAccess;
};

class TypeTableAppendTransaction final {
public:
  TypeTableAppendTransaction(TypeTableAppendTransaction &&) noexcept;
  TypeTableAppendTransaction &operator=(TypeTableAppendTransaction &&) noexcept;
  TypeTableAppendTransaction(const TypeTableAppendTransaction &) = delete;
  TypeTableAppendTransaction &
  operator=(const TypeTableAppendTransaction &) = delete;
  ~TypeTableAppendTransaction();

  TypeParameterId allocateTypeParameter();
  EffectVariableId allocateEffectVariable();
  TypeId reserveType();
  EffectRowId reserveEffect();
  [[nodiscard]] std::expected<void, std::string> define(TypeId Reserved,
                                                        StructuralType Type);
  [[nodiscard]] std::expected<void, std::string>
  defineEffect(EffectRowId Reserved, EffectRow Row);
  [[nodiscard]] std::expected<TypeId, std::string>
  internSum(std::span<const TypeId> Alternatives);
  [[nodiscard]] std::expected<TypeId, std::string> intern(StructuralType Type);
  [[nodiscard]] std::expected<EffectRowId, std::string>
  internEffect(EffectRow Row);
  [[nodiscard]] std::expected<void, std::string> prepareCommit();
  [[nodiscard]] std::expected<TypeId, std::string>
  resolved(TypeId Provisional) const;
  [[nodiscard]] std::expected<EffectRowId, std::string>
  resolved(EffectRowId Provisional) const;
  [[nodiscard]] std::expected<StructuralType, std::string>
  preparedType(TypeId Provisional) const;
  [[nodiscard]] std::expected<void, std::string>
  validatePreparedCatalog(const StructuralDeclarationCatalog &Catalog,
                          std::span<const TypeId> RuntimeRoots = {}) const;
  void commitPrepared() noexcept;

private:
  struct Impl;
  explicit TypeTableAppendTransaction(TypeTable &Types);
  std::unique_ptr<Impl> Implementation_;
  friend class TypeTable;
};

[[nodiscard]] std::expected<ResultOwnership, std::string>
resultOwnershipFor(const TypeTable &Types, TypeId Type);

} // namespace yona::model

#endif /* YONA_MODEL_STRUCTURALTYPE_H */
