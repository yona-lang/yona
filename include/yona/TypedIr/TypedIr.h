#ifndef YONA_TYPEDIR_TYPEDIR_H
#define YONA_TYPEDIR_TYPEDIR_H
#include "yona/TypedIr/Instruction.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace yona::typed_ir {
enum class ModulePhase {
  Canonical,
  GenericPrepared,
  PatternCanonical,
  AsyncPlanned,
  GeneratorLowered,
  ControlFlow,
  AsyncPrepared,
  CleanupPrepared,
  EffectPrepared,
  ClosureConverted,
  OperationInstantiated,
  EffectOutlined,
  TailCallsLowered,
  AcceleratorSelected,
  ControlOutcomeLowered,
  RepresentationSelected,
  OwnershipLowered,
  CleanupLowered,
  LlvmReady
};
enum class ValueOrigin { Parameter, BlockArgument, Constant, Instruction };
struct Value {
  model::TypeId Type;
  PhysicalRepresentation Representation = PhysicalRepresentation::Unselected;
  SourceRange Range;
  OwnershipKind Ownership;
  std::optional<BorrowProvenance> Borrow;
  std::optional<NominalTypeId> NominalIdentity;
  std::optional<BoundaryRequirementSetId> CallableBoundaryRequirements;
  ValueOrigin Origin;
  std::string DebugName;
};
struct Instruction {
  std::optional<ValueId> Result;
  SourceRange Range;
  InstructionPayload Payload;
};
struct RangedTerminator {
  SourceRange Range;
  Terminator Payload;
};
struct Block {
  std::string DebugName;
  SourceRange Range;
  std::vector<ValueId> Arguments;
  std::vector<Instruction> Instructions;
  std::optional<RangedTerminator> End;
  std::optional<ControlRegionId> ControlRegion;
};
struct ConstructorDeclaration {
  ConstructorId Id;
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct NominalDeclaration {
  NominalTypeId Id;
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<ConstructorDeclaration> Constructors;
};
struct OperationParameter {
  model::TypeId Type;
  model::ParameterOwnership Ownership;
};
struct OperationDeclaration {
  OperationId Id;
  model::EffectOperationKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
  std::vector<model::EffectVariableDeclaration> EffectVariables;
  std::vector<model::TypeConstraint> Constraints;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId Effects;
  std::uint64_t DeclarationFingerprint;
};
struct OperationInstance {
  OperationInstanceId Id;
  OperationId Declaration;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId SemanticEffects;
  RuntimeEffectRowId RuntimeEffects;
  std::uint64_t RuntimeFingerprint;
};
struct RuntimeEffectRow {
  RuntimeEffectRowId Id;
  model::EffectRowId SemanticRow;
  std::vector<OperationInstanceId> Operations;
  bool MayRaise;
  bool MayCancel;
  std::uint64_t StructuralFingerprint;
};
struct Global {
  GlobalId Id;
  model::SymbolIdentity Symbol;
  model::TypeId Type;
  OwnershipKind Ownership;
};
struct BuiltinTypeIds {
  model::TypeId ExceptionValue;
  model::TypeId EffectRequest;
  model::TypeId ControlOutcome;
  model::TypeId ExecutionContext;
  model::TypeId CallableInvocationEnvironment;
  model::TypeId ContinuationBoundaryContext;
};
enum class FunctionDefinitionKind { Definition, Imported, NativeExtern };
enum class FunctionVisibility { Private, Module, Public };
enum class FunctionEntryKind {
  Typed = 0,
  OutcomeRouter = 1,
  CleanupDrop = 2,
  KeyHashAdapter = 3,
  KeyEqualsAdapter = 4
};
enum class NativeAsyncKind : std::uint8_t {
  Synchronous = 0,
  ThreadPool = 1,
  DedicatedOutcome = 2
};
enum class NativeBoundaryRoute : std::uint8_t {
  CheckedDirectV2 = 0,
  CheckedOutcomeV2 = 1,
  StableExternal = 2
};
struct FunctionLinkage {
  FunctionDefinitionKind Kind;
  FunctionVisibility Visibility;
  std::optional<model::SymbolIdentity> Symbol;
  std::optional<std::string> NativeSymbol;
  std::optional<NativeAsyncKind> AsyncKind;
  std::optional<NativeBoundaryRoute> NativeRoute;
};
struct GenericCaptureBinding {
  semantics::BindingId Binding;
  model::TypeId Type;
  model::ParameterOwnership Ownership;
  std::string DebugName;
};
struct BoundaryContextContract {
  BoundaryContextContractId Id;
  std::vector<ContinuationBoundaryDescriptorId> Guaranteed;
};
struct BoundaryRequirementSet {
  BoundaryRequirementSetId Id;
  std::vector<ContinuationBoundaryDescriptorId> Required;
};
struct Function {
  FunctionId Id;
  std::shared_ptr<const FunctionLocalDomain> LocalDomain;
  std::string Name;
  SourceRange Range;
  model::TypeId Signature;
  FunctionEntryKind EntryKind = FunctionEntryKind::Typed;
  FunctionLinkage Linkage;
  std::optional<model::GenericBinderEnvironment> GenericBinders;
  std::vector<GenericCaptureBinding> GenericCaptures;
  std::optional<RuntimeEffectRowId> RuntimeEffects;
  std::optional<ValueId> ExecutionContextParameter;
  std::optional<ValueId> CallableEnvironmentParameter;
  std::optional<ValueId> BoundaryContextParameter;
  std::optional<BoundaryContextContractId> BoundaryContextContract;
  std::optional<BoundaryRequirementSetId> AmbientBoundaryRequirements;
  std::vector<ValueId> Parameters;
  std::vector<Value> Values;
  std::vector<Block> Blocks;
  std::optional<BlockId> Entry;
};
struct Module {
  model::ModuleIdentity Identity;
  std::shared_ptr<const ModuleMutationDomain> MutationDomain;
  std::shared_ptr<model::TypeTable> Types;
  BuiltinTypeIds Builtins;
  std::vector<model::ModuleIdentity> Imports;
  std::vector<NominalDeclaration> Nominals;
  std::vector<model::ResourceDeclaration> Resources;
  std::vector<OperationDeclaration> Operations;
  std::vector<OperationInstance> OperationInstances;
  std::vector<RuntimeEffectRow> RuntimeEffectRows;
  std::vector<BoundaryContextContract> BoundaryContextContracts;
  std::vector<BoundaryRequirementSet> BoundaryRequirementSets;
  std::vector<Global> Globals;
  std::vector<Function> Functions;
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
  ModulePhase Phase;
};
} // namespace yona::typed_ir
#endif
