#ifndef YONA_TYPEDIR_BUILDER_H
#define YONA_TYPEDIR_BUILDER_H
#include "yona/TypedIr/TypedIr.h"
namespace yona::typed_ir {
struct ModuleSpec {
  model::ModuleIdentity Identity;
  std::shared_ptr<model::TypeTable> Types;
  BuiltinTypeIds Builtins;
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
};
struct NominalSpec {
  model::NominalTypeKey Key;
  std::vector<model::TypeParameterId> TypeParameters;
};
struct ConstructorSpec {
  std::string Name;
  std::uint32_t Tag;
  std::vector<model::TypeId> Fields;
};
struct OperationSpec {
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
struct GlobalSpec {
  model::SymbolIdentity Symbol;
  model::TypeId Type;
  OwnershipKind Ownership;
};
struct FunctionSpec {
  std::string Name;
  SourceRange Range;
  model::TypeId Signature;
  FunctionLinkage Linkage;
};
struct BlockSpec {
  std::string DebugName;
  SourceRange Range;
};
struct ValueSpec {
  model::TypeId Type;
  OwnershipKind Ownership;
  std::optional<BorrowProvenance> Borrow;
  SourceRange Range;
  std::string DebugName;
};
class Builder final {
public:
  explicit Builder(ModuleSpec Spec);
  void addImport(model::ModuleIdentity Import);
  void addResource(model::ResourceDeclaration Declaration);
  NominalTypeId addNominal(NominalSpec Spec);
  ConstructorId addConstructor(NominalTypeId Type, ConstructorSpec Spec);
  OperationId addOperation(OperationSpec Spec);
  GlobalId addGlobal(GlobalSpec Spec);
  FunctionId addFunction(FunctionSpec Spec);
  BlockId createBlock(FunctionId Function, BlockSpec Spec);
  ValueId addParameter(FunctionId Function, ValueSpec Spec);
  ValueId addBlockArgument(FunctionId Function, BlockId Block, ValueSpec Spec);
  ValueId emit(FunctionId Function, BlockId Block, InstructionPayload Payload,
               ValueSpec Result);
  void emitEffect(FunctionId Function, BlockId Block,
                  InstructionPayload Payload, SourceRange Range);
  void setTerminator(FunctionId Function, BlockId Block, Terminator End,
                     SourceRange Range);
  Module finish(ModulePhase Phase);

private:
  [[nodiscard]] Function &function(FunctionId Id);
  [[nodiscard]] Block &block(Function &Owner, BlockId Id);
  [[nodiscard]] ValueId addValue(Function &Owner, ValueSpec Spec,
                                 ValueOrigin Origin);
  void assignEdges(Function &Owner, Terminator &End);
  void ensureMutable() const;
  Module Module_;
  bool Finished_ = false;
};
} // namespace yona::typed_ir
#endif
