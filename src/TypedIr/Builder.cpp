#include "yona/TypedIr/Builder.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace yona::typed_ir {
namespace {

model::IdDomain freshDomain() {
  static std::atomic<model::IdDomain> Next{1};
  const auto Result = Next.fetch_add(1, std::memory_order_relaxed);
  if (Result == model::InvalidIdDomain ||
      Result == (std::numeric_limits<model::IdDomain>::max)())
    throw std::overflow_error("typed IR identity domain exhausted");
  return Result;
}

bool isCIdentifier(const std::string &Name) {
  if (Name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(Name[0])) || Name[0] == '_'))
    return false;
  return std::ranges::all_of(Name.substr(1), [](unsigned char Character) {
    return std::isalnum(Character) || Character == '_';
  });
}

void validateRange(const Module &ModuleValue, SourceRange Range,
                   std::string_view What) {
  if (!Range.isValid() || Range.Source != ModuleValue.Source)
    throw std::invalid_argument(std::string(What) +
                                " requires a primary-source range");
  const auto Text = ModuleValue.Sources->text(Range.Source);
  if (Range.Offset > Text.size() || Range.Length > Text.size() - Range.Offset)
    throw std::invalid_argument(std::string(What) +
                                " range exceeds its source buffer");
}

void validateType(const Module &ModuleValue, model::TypeId Id,
                  std::string_view What) {
  if (!ModuleValue.Types->valid(Id))
    throw std::invalid_argument(std::string(What) +
                                " references an invalid type");
}

template <class Id>
Id checkedId(model::IdDomain Domain, std::size_t Size, std::string_view What) {
  if (Size >= (std::numeric_limits<std::uint32_t>::max)())
    throw std::overflow_error(std::string(What) + " identity space exhausted");
  return Id(Domain, static_cast<std::uint32_t>(Size));
}

} // namespace

Builder::Builder(ModuleSpec Spec)
    : Module_{std::move(Spec.Identity),
              {},
              std::move(Spec.Types),
              Spec.Builtins,
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              {},
              std::move(Spec.Sources),
              Spec.Source,
              ModulePhase::Canonical} {
  if (!Module_.Types)
    throw std::invalid_argument("typed IR module requires a type table");
  if (!Module_.Sources)
    throw std::invalid_argument("typed IR module requires source ownership");
  if (!Module_.Source.isValid())
    throw std::invalid_argument("typed IR module requires a valid source");
  try {
    (void)Module_.Sources->text(Module_.Source);
  } catch (const std::out_of_range &) {
    throw std::invalid_argument("typed IR module source is foreign");
  }
  const model::TypeId Builtins[] = {
      Module_.Builtins.ExceptionValue,
      Module_.Builtins.EffectRequest,
      Module_.Builtins.ControlOutcome,
      Module_.Builtins.ExecutionContext,
      Module_.Builtins.CallableInvocationEnvironment,
      Module_.Builtins.ContinuationBoundaryContext};
  for (const auto Id : Builtins)
    validateType(Module_, Id, "typed IR builtin");
  Module_.MutationDomain = std::shared_ptr<const ModuleMutationDomain>(
      new ModuleMutationDomain(freshDomain()));
}

void Builder::ensureMutable() const {
  if (Finished_)
    throw std::logic_error("typed IR builder is already finished");
}

void Builder::addImport(model::ModuleIdentity Import) {
  ensureMutable();
  if (Import == Module_.Identity ||
      std::ranges::find(Module_.Imports, Import) != Module_.Imports.end())
    throw std::invalid_argument("typed IR imports must be unique and non-self");
  Module_.Imports.push_back(std::move(Import));
}

void Builder::addResource(model::ResourceDeclaration Declaration) {
  ensureMutable();
  if (!isCIdentifier(Declaration.ReleaseNativeSymbol))
    throw std::invalid_argument(
        "resource release symbol must be a C identifier");
  if (Declaration.Shareability == model::ResourceShareability::Linear &&
      Declaration.TryRetainNativeSymbol)
    throw std::invalid_argument(
        "linear resources cannot define a retain callback");
  if (Declaration.Shareability ==
          model::ResourceShareability::AlwaysShareable &&
      (!Declaration.TryRetainNativeSymbol ||
       !isCIdentifier(*Declaration.TryRetainNativeSymbol)))
    throw std::invalid_argument(
        "shareable resources require a C retain callback");
  std::unordered_set<model::TypeParameterId, model::StrongIdHasher> Parameters;
  for (auto Id : Declaration.TypeParameters)
    if (!Module_.Types->valid(Id) || !Parameters.insert(Id).second)
      throw std::invalid_argument(
          "resource has invalid or duplicate type parameters");
  for (const auto &Nominal : Module_.Nominals)
    if (Nominal.Key == Declaration.Key)
      throw std::invalid_argument("resource key collides with a nominal type");
  for (const auto &Resource : Module_.Resources)
    if (Resource.Key == Declaration.Key)
      throw std::invalid_argument("resource keys must be unique");
  Module_.Resources.push_back(std::move(Declaration));
}

NominalTypeId Builder::addNominal(NominalSpec Spec) {
  ensureMutable();
  for (const auto &Nominal : Module_.Nominals)
    if (Nominal.Key == Spec.Key)
      throw std::invalid_argument("nominal keys must be unique");
  for (const auto &Resource : Module_.Resources)
    if (Resource.Key == Spec.Key)
      throw std::invalid_argument("nominal key collides with a resource");
  for (auto Id : Spec.TypeParameters)
    if (!Module_.Types->valid(Id))
      throw std::invalid_argument("nominal has invalid type parameter");
  const auto Id = checkedId<NominalTypeId>(Module_.MutationDomain->identity(),
                                           Module_.Nominals.size(), "nominal");
  Module_.Nominals.push_back(
      {Id, std::move(Spec.Key), std::move(Spec.TypeParameters), {}});
  return Id;
}

ConstructorId Builder::addConstructor(NominalTypeId Type,
                                      ConstructorSpec Spec) {
  ensureMutable();
  if (!Type.isValid() || Type.domain() != Module_.MutationDomain->identity() ||
      Type.value() >= Module_.Nominals.size())
    throw std::invalid_argument("constructor owner is not a module nominal");
  auto &Nominal = Module_.Nominals[Type.value()];
  for (const auto &Existing : Nominal.Constructors)
    if (Existing.Name == Spec.Name || Existing.Tag == Spec.Tag)
      throw std::invalid_argument("constructor names and tags must be unique");
  for (auto Field : Spec.Fields)
    validateType(Module_, Field, "constructor field");
  std::size_t Count = 0;
  for (const auto &Item : Module_.Nominals)
    Count += Item.Constructors.size();
  const auto Id = checkedId<ConstructorId>(Module_.MutationDomain->identity(),
                                           Count, "constructor");
  Nominal.Constructors.push_back(
      {Id, std::move(Spec.Name), Spec.Tag, std::move(Spec.Fields)});
  return Id;
}

OperationId Builder::addOperation(OperationSpec Spec) {
  ensureMutable();
  for (const auto &Operation : Module_.Operations)
    if (Operation.Key == Spec.Key)
      throw std::invalid_argument("operation keys must be unique");
  for (auto Id : Spec.TypeParameters)
    if (!Module_.Types->valid(Id))
      throw std::invalid_argument("operation has invalid type parameter");
  for (const auto &Variable : Spec.EffectVariables)
    if (!Module_.Types->valid(Variable.Variable))
      throw std::invalid_argument("operation has invalid effect variable");
  for (const auto &Parameter : Spec.Parameters)
    validateType(Module_, Parameter.Type, "operation parameter");
  validateType(Module_, Spec.ResultType, "operation result");
  if (!Module_.Types->valid(Spec.Effects))
    throw std::invalid_argument("operation has invalid effects");
  const auto Id =
      checkedId<OperationId>(Module_.MutationDomain->identity(),
                             Module_.Operations.size(), "operation");
  Module_.Operations.push_back(
      {Id, std::move(Spec.Key), std::move(Spec.TypeParameters),
       std::move(Spec.EffectVariables), std::move(Spec.Constraints),
       std::move(Spec.Parameters), Spec.ResultType, Spec.ResultContract,
       Spec.Effects, Spec.DeclarationFingerprint});
  return Id;
}

GlobalId Builder::addGlobal(GlobalSpec Spec) {
  ensureMutable();
  validateType(Module_, Spec.Type, "global");
  for (const auto &GlobalValue : Module_.Globals)
    if (GlobalValue.Symbol == Spec.Symbol)
      throw std::invalid_argument("global symbols must be unique");
  const auto Id = checkedId<GlobalId>(Module_.MutationDomain->identity(),
                                      Module_.Globals.size(), "global");
  Module_.Globals.push_back(
      {Id, std::move(Spec.Symbol), Spec.Type, Spec.Ownership});
  return Id;
}

FunctionId Builder::addFunction(FunctionSpec Spec) {
  ensureMutable();
  if (Spec.Name.empty())
    throw std::invalid_argument("typed IR functions require a name");
  validateRange(Module_, Spec.Range, "function");
  validateType(Module_, Spec.Signature, "function signature");
  for (const auto &Existing : Module_.Functions)
    if (Existing.Name == Spec.Name)
      throw std::invalid_argument("function names must be unique");
  const auto Id = checkedId<FunctionId>(Module_.MutationDomain->identity(),
                                        Module_.Functions.size(), "function");
  const auto Local = std::shared_ptr<const FunctionLocalDomain>(
      new FunctionLocalDomain(freshDomain()));
  Module_.Functions.push_back({Id, Local, std::move(Spec.Name), Spec.Range,
                               Spec.Signature, FunctionEntryKind::Typed,
                               std::move(Spec.Linkage)});
  return Id;
}

Function &Builder::function(FunctionId Id) {
  ensureMutable();
  if (!Id.isValid() || Id.domain() != Module_.MutationDomain->identity() ||
      Id.value() >= Module_.Functions.size())
    throw std::invalid_argument("function ID is foreign or invalid");
  return Module_.Functions[Id.value()];
}

Block &Builder::block(Function &Owner, BlockId Id) {
  if (!Id.isValid() || Id.domain() != Owner.LocalDomain->identity() ||
      Id.value() >= Owner.Blocks.size())
    throw std::invalid_argument("block ID is foreign or invalid");
  return Owner.Blocks[Id.value()];
}

BlockId Builder::createBlock(FunctionId FunctionValue, BlockSpec Spec) {
  auto &Owner = function(FunctionValue);
  if (Owner.Linkage.Kind != FunctionDefinitionKind::Definition)
    throw std::invalid_argument("declarations cannot contain blocks");
  validateRange(Module_, Spec.Range, "block");
  const auto Id = checkedId<BlockId>(Owner.LocalDomain->identity(),
                                     Owner.Blocks.size(), "block");
  Owner.Blocks.push_back({std::move(Spec.DebugName), Spec.Range});
  if (!Owner.Entry)
    Owner.Entry = Id;
  return Id;
}

ValueId Builder::addValue(Function &Owner, ValueSpec Spec, ValueOrigin Origin) {
  validateType(Module_, Spec.Type, "value");
  validateRange(Module_, Spec.Range, "value");
  if ((Spec.Ownership == OwnershipKind::Borrowed) != Spec.Borrow.has_value())
    throw std::invalid_argument(
        "borrow provenance must be present exactly for borrowed values");
  const auto Id = checkedId<ValueId>(Owner.LocalDomain->identity(),
                                     Owner.Values.size(), "value");
  Owner.Values.push_back({Spec.Type, PhysicalRepresentation::Unselected,
                          Spec.Range, Spec.Ownership, std::move(Spec.Borrow),
                          std::nullopt, std::nullopt, Origin,
                          std::move(Spec.DebugName)});
  return Id;
}

ValueId Builder::addParameter(FunctionId FunctionValue, ValueSpec Spec) {
  auto &Owner = function(FunctionValue);
  const auto Id = addValue(Owner, std::move(Spec), ValueOrigin::Parameter);
  Owner.Parameters.push_back(Id);
  return Id;
}

ValueId Builder::addBlockArgument(FunctionId FunctionValue, BlockId BlockValue,
                                  ValueSpec Spec) {
  auto &Owner = function(FunctionValue);
  auto &Target = block(Owner, BlockValue);
  const auto Id = addValue(Owner, std::move(Spec), ValueOrigin::BlockArgument);
  Target.Arguments.push_back(Id);
  return Id;
}

ValueId Builder::emit(FunctionId FunctionValue, BlockId BlockValue,
                      InstructionPayload Payload, ValueSpec Result) {
  auto &Owner = function(FunctionValue);
  auto &Target = block(Owner, BlockValue);
  forEachOperand(Payload, [&](ValueId Id) {
    if (!Id.isValid() || Id.domain() != Owner.LocalDomain->identity() ||
        Id.value() >= Owner.Values.size())
      throw std::invalid_argument("instruction operand is foreign or invalid");
  });
  if (const auto *Call = std::get_if<DirectCallInst>(&Payload)) {
    if (!Call->Callee.isValid() ||
        Call->Callee.domain() != Module_.MutationDomain->identity() ||
        Call->Callee.value() >= Module_.Functions.size())
      throw std::invalid_argument("direct call callee is foreign or invalid");
  }
  if (const auto *Injection = std::get_if<InjectSumInst>(&Payload))
    validateType(Module_, Injection->Sum, "sum injection");
  if (const auto *Lexical = std::get_if<LexicalRefInst>(&Payload);
      Lexical && !Lexical->Binding.isValid())
    throw std::invalid_argument("lexical reference binding is invalid");
  const auto Origin = std::holds_alternative<ConstantInst>(Payload)
                          ? ValueOrigin::Constant
                          : ValueOrigin::Instruction;
  const auto Id = addValue(Owner, std::move(Result), Origin);
  Target.Instructions.push_back(
      {Id, Owner.Values[Id.value()].Range, std::move(Payload)});
  return Id;
}

void Builder::emitEffect(FunctionId FunctionValue, BlockId BlockValue,
                         InstructionPayload Payload, SourceRange Range) {
  auto &Owner = function(FunctionValue);
  auto &Target = block(Owner, BlockValue);
  validateRange(Module_, Range, "instruction");
  forEachOperand(Payload, [&](ValueId Id) {
    if (!Id.isValid() || Id.domain() != Owner.LocalDomain->identity() ||
        Id.value() >= Owner.Values.size())
      throw std::invalid_argument("instruction operand is foreign or invalid");
  });
  Target.Instructions.push_back({std::nullopt, Range, std::move(Payload)});
}

void Builder::assignEdges(Function &Owner, Terminator &End) {
  std::size_t EdgeCount = 0;
  for (const auto &Candidate : Owner.Blocks)
    if (Candidate.End)
      forEachSuccessor(Candidate.End->Payload,
                       [&](SuccessorView) { ++EdgeCount; });
  auto Next = [&]() {
    return checkedId<EdgeId>(Owner.LocalDomain->identity(), EdgeCount++,
                             "edge");
  };
  std::visit(
      [&](auto &Value) {
        using T = std::decay_t<decltype(Value)>;
        auto Assign = [&](BranchTarget &Target) { Target.Edge = Next(); };
        if constexpr (std::is_same_v<T, Branch>)
          Assign(Value.Target);
        else if constexpr (std::is_same_v<T, CondBranch>) {
          Assign(Value.TrueTarget);
          Assign(Value.FalseTarget);
        } else if constexpr (std::is_same_v<T, Switch>) {
          for (auto &Case : Value.Cases)
            Assign(Case.Target);
          Assign(Value.Default);
        } else if constexpr (std::is_same_v<T, Perform>)
          Value.ResumeTarget.Edge = Next();
      },
      End);
}

void Builder::setTerminator(FunctionId FunctionValue, BlockId BlockValue,
                            Terminator End, SourceRange Range) {
  auto &Owner = function(FunctionValue);
  auto &Target = block(Owner, BlockValue);
  if (Target.End)
    throw std::invalid_argument("block already has a terminator");
  validateRange(Module_, Range, "terminator");
  forEachOperand(End, [&](ValueId Id) {
    if (!Id.isValid() || Id.domain() != Owner.LocalDomain->identity() ||
        Id.value() >= Owner.Values.size())
      throw std::invalid_argument("terminator operand is foreign or invalid");
  });
  forEachSuccessor(End, [&](SuccessorView Successor) {
    if (!Successor.Block.isValid() ||
        Successor.Block.domain() != Owner.LocalDomain->identity() ||
        Successor.Block.value() >= Owner.Blocks.size())
      throw std::invalid_argument("terminator successor is foreign or invalid");
    for (const auto &Produced : Successor.ProducedPrefix)
      validateType(Module_, Produced.Type, "produced successor value");
  });
  if (const auto *Effect = std::get_if<Perform>(&End)) {
    if (!Module_.Types->valid(Effect->ResumeEffects))
      throw std::invalid_argument("perform resume effects are invalid");
    std::visit(
        [&](const auto &Reference) {
          using T = std::decay_t<decltype(Reference)>;
          if constexpr (std::is_same_v<T, OperationUse>) {
            if (!Reference.Declaration.isValid() ||
                Reference.Declaration.domain() !=
                    Module_.MutationDomain->identity() ||
                Reference.Declaration.value() >= Module_.Operations.size())
              throw std::invalid_argument(
                  "perform declaration is foreign or invalid");
            for (auto Id : Reference.TypeArguments)
              validateType(Module_, Id, "perform type argument");
            for (auto Id : Reference.EffectArguments)
              if (!Module_.Types->valid(Id))
                throw std::invalid_argument(
                    "perform effect argument is foreign or invalid");
          } else if (!Reference.isValid() ||
                     Reference.domain() != Module_.MutationDomain->identity()) {
            throw std::invalid_argument(
                "perform operation instance is foreign or invalid");
          }
        },
        Effect->Operation);
  }
  assignEdges(Owner, End);
  Target.End = RangedTerminator{Range, std::move(End)};
}

Module Builder::finish(ModulePhase Phase) {
  ensureMutable();
  std::ranges::sort(Module_.Imports);
  std::ranges::sort(Module_.Resources, {}, &model::ResourceDeclaration::Key);
  Module_.Phase = Phase;
  Finished_ = true;
  return std::move(Module_);
}

} // namespace yona::typed_ir
