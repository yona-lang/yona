#include "yona/TypedIr/Parser.h"

#include "yona/Model/CanonicalEncoding.h"
#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/Verifier.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <type_traits>
namespace yona::typed_ir {
namespace {
std::expected<std::string, std::string> unhex(std::string_view Text) {
  if (Text == "-")
    return std::string{};
  if (Text.size() % 2)
    return std::unexpected("hex field has odd length");
  std::string Result(Text.size() / 2, '\0');
  for (std::size_t I = 0; I < Result.size(); ++I) {
    unsigned Value = 0;
    const auto *Begin = Text.data() + I * 2;
    auto [End, Error] = std::from_chars(Begin, Begin + 2, Value, 16);
    if (Error != std::errc{} || End != Begin + 2)
      return std::unexpected("invalid hex field");
    Result[I] = static_cast<char>(Value);
  }
  return Result;
}
std::vector<std::byte> bytes(const std::string &Text) {
  std::vector<std::byte> Result;
  Result.reserve(Text.size());
  for (unsigned char Byte : Text)
    Result.push_back(static_cast<std::byte>(Byte));
  return Result;
}
template <class T>
bool number(std::string_view Token, T &Value, int Base = 10) {
  auto [End, Error] =
      std::from_chars(Token.data(), Token.data() + Token.size(), Value, Base);
  return Error == std::errc{} && End == Token.data() + Token.size();
}
struct FunctionState {
  struct ValueMetadata {
    PhysicalRepresentation Representation = PhysicalRepresentation::Unselected;
    std::optional<NominalTypeId> NominalIdentity;
    std::optional<std::uint32_t> BoundaryRequirements;
  };
  FunctionId Actual;
  std::vector<ValueId> Values;
  std::vector<ValueMetadata> ValueMetadataByOrdinal;
  std::vector<BlockId> Blocks;
  std::vector<std::optional<std::uint32_t>> ControlRegions;
  FunctionEntryKind EntryKind = FunctionEntryKind::Typed;
  std::optional<model::GenericBinderEnvironment> GenericBinders;
  std::vector<GenericCaptureBinding> GenericCaptures;
  std::optional<std::uint32_t> RuntimeEffects;
  std::optional<ValueId> ExecutionContext;
  std::optional<ValueId> CallableEnvironment;
  std::optional<ValueId> BoundaryContext;
  std::optional<std::uint32_t> BoundaryContract;
  std::optional<std::uint32_t> AmbientRequirements;
};
struct RawOperationInstance {
  std::uint32_t Declaration;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
  std::vector<OperationParameter> Parameters;
  model::TypeId ResultType;
  model::ResultOwnership ResultContract;
  model::EffectRowId SemanticEffects;
  std::uint32_t RuntimeEffects;
  std::uint64_t RuntimeFingerprint;
};
struct RawRuntimeEffectRow {
  model::EffectRowId SemanticRow;
  std::vector<std::uint32_t> Operations;
  bool MayRaise;
  bool MayCancel;
  std::uint64_t StructuralFingerprint;
};
using RawBoundary = std::pair<char, std::uint32_t>;
} // namespace

ParseModuleResult parseModule(std::string_view Text,
                              std::shared_ptr<const SourceManager> Sources) {
  // Bound every serialized collection and dense local ordinal before it can
  // control allocation. The budget is aggregate across the complete module,
  // so individually legal nested collections cannot multiply memory use. It
  // is a parser resource limit, not a format limit; writers may reject larger
  // in-memory modules explicitly in the future.
  constexpr std::size_t MaxRecords = model::DefaultCanonicalDecodeUnits;
  model::CanonicalDecodeBudget DecodeBudget(MaxRecords);
  const auto claimRecords = [&](std::size_t Count) {
    return DecodeBudget.claim(Count, "tirf.records").has_value();
  };
  const SourceId DiagnosticSource =
      Sources && Sources->size() ? SourceId(0) : SourceId{};
  const SourceRange DiagnosticRange{DiagnosticSource, 1, 1, 0,
                                    Text.empty() ? 0u : 1u};
  auto Fail = [&](std::string Message) -> ParseModuleResult {
    return std::unexpected(
        std::vector<ParseError>{{DiagnosticRange, std::move(Message)}});
  };
  if (!Sources)
    return Fail("typed IR parser requires a source manager");
  std::istringstream In{std::string(Text)};
  std::string Token;
  auto need = [&](std::string_view Expected) {
    return (In >> Token) && Token == Expected;
  };
  unsigned Version;
  if (!need("tir") || !(In >> Version) || Version != 1)
    return Fail("expected 'tir 1' header");
  if (!need("module") || !(In >> Token))
    return Fail("expected module identity");
  auto ModuleName = unhex(Token);
  if (!ModuleName)
    return Fail(ModuleName.error());
  std::uint32_t SourceOrdinal;
  unsigned Phase;
  if (!need("source") || !(In >> SourceOrdinal) ||
      SourceOrdinal >= Sources->size())
    return Fail("module source identity is foreign");
  if (!need("phase") || !(In >> Phase) ||
      Phase > static_cast<unsigned>(ModulePhase::LlvmReady))
    return Fail("invalid module phase");
  std::string TypeHex, EffectHex;
  if (!need("types") || !(In >> TypeHex) || !need("effects") ||
      !(In >> EffectHex))
    return Fail("missing canonical type tables");
  if ((TypeHex.size() & 1u) != 0 || (EffectHex.size() & 1u) != 0 ||
      !claimRecords(TypeHex.size() / 2u) ||
      !claimRecords(EffectHex.size() / 2u))
    return Fail("canonical type tables exceed the decode budget");
  auto TypeText = unhex(TypeHex);
  auto EffectText = unhex(EffectHex);
  if (!TypeText || !EffectText)
    return Fail("invalid canonical table hex");
  auto Decoded = model::decodeInterfaceTables(
      bytes(*TypeText), bytes(*EffectText), &DecodeBudget);
  if (!Decoded)
    return Fail("invalid canonical type table: " + Decoded.error().Message);
  auto TypeAt = [&](std::uint32_t Index) -> std::optional<model::TypeId> {
    if (Index >= Decoded->TypeIdsByCanonicalIndex.size())
      return std::nullopt;
    return Decoded->TypeIdsByCanonicalIndex[Index];
  };
  auto EffectAt =
      [&](std::uint32_t Index) -> std::optional<model::EffectRowId> {
    if (Index >= Decoded->EffectIdsByCanonicalIndex.size())
      return std::nullopt;
    return Decoded->EffectIdsByCanonicalIndex[Index];
  };
  auto TypeParameterAt =
      [&](std::uint32_t Binder,
          std::uint32_t Ordinal) -> std::optional<model::TypeParameterId> {
    const auto It = std::ranges::find_if(
        Decoded->TypeParametersByCanonicalRef, [&](const auto &Item) {
          return Item.Source.BinderIndex == Binder &&
                 Item.Source.Ordinal == Ordinal;
        });
    if (It == Decoded->TypeParametersByCanonicalRef.end())
      return std::nullopt;
    return It->Target;
  };
  auto EffectVariableAt =
      [&](std::uint32_t Binder,
          std::uint32_t Ordinal) -> std::optional<model::EffectVariableId> {
    const auto It = std::ranges::find_if(
        Decoded->EffectVariablesByCanonicalRef, [&](const auto &Item) {
          return Item.Source.BinderIndex == Binder &&
                 Item.Source.Ordinal == Ordinal;
        });
    if (It == Decoded->EffectVariablesByCanonicalRef.end())
      return std::nullopt;
    return It->Target;
  };
  std::uint32_t BI[6];
  if (!need("builtins"))
    return Fail("missing builtin types");
  for (auto &I : BI)
    if (!(In >> I) || !TypeAt(I))
      return Fail("builtin type index is out of range");
  try {
    auto Types = std::make_shared<model::TypeTable>(std::move(Decoded->Types));
    Builder Build({model::ModuleIdentity(*ModuleName),
                   Types,
                   {*TypeAt(BI[0]), *TypeAt(BI[1]), *TypeAt(BI[2]),
                    *TypeAt(BI[3]), *TypeAt(BI[4]), *TypeAt(BI[5])},
                   Sources,
                   SourceId(SourceOrdinal)});
    std::size_t Count;
    if (!need("imports") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized imports section");
    for (std::size_t I = 0; I < Count; ++I) {
      if (!need("import") || !(In >> Token))
        return Fail("invalid import record");
      auto Name = unhex(Token);
      if (!Name)
        return Fail(Name.error());
      Build.addImport(model::ModuleIdentity(*Name));
    }
    if (!need("nominals") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized nominals section");
    std::vector<NominalTypeId> Nominals(Count);
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t Old;
      std::size_t TypeParameterCount, ConstructorCount;
      std::string ModuleHex, NameHex;
      if (!need("nominal") ||
          !(In >> Old >> ModuleHex >> NameHex >> TypeParameterCount) ||
          Old != I || !claimRecords(TypeParameterCount))
        return Fail("invalid nominal declaration");
      auto Module = unhex(ModuleHex);
      auto Name = unhex(NameHex);
      if (!Module || !Name)
        return Fail("invalid nominal identity");
      std::vector<model::TypeParameterId> Parameters;
      for (std::size_t P = 0; P < TypeParameterCount; ++P) {
        std::uint32_t B, O;
        if (!(In >> B >> O) || !TypeParameterAt(B, O))
          return Fail("invalid nominal type parameter");
        Parameters.push_back(*TypeParameterAt(B, O));
      }
      if (!(In >> ConstructorCount) || !claimRecords(ConstructorCount))
        return Fail("invalid nominal constructor count");
      const auto Nominal = Build.addNominal(
          {{model::ModuleIdentity(*Module), *Name}, std::move(Parameters)});
      Nominals[I] = Nominal;
      for (std::size_t C = 0; C < ConstructorCount; ++C) {
        std::uint32_t ConstructorId, Tag;
        std::size_t FieldCount;
        std::string ConstructorName;
        if (!need("constructor") ||
            !(In >> ConstructorId >> ConstructorName >> Tag >> FieldCount) ||
            !claimRecords(FieldCount))
          return Fail("invalid constructor declaration");
        auto DecodedName = unhex(ConstructorName);
        if (!DecodedName)
          return Fail(DecodedName.error());
        std::vector<model::TypeId> Fields;
        for (std::size_t F = 0; F < FieldCount; ++F) {
          std::uint32_t Index;
          if (!(In >> Index) || !TypeAt(Index))
            return Fail("invalid constructor field type");
          Fields.push_back(*TypeAt(Index));
        }
        const auto Actual = Build.addConstructor(
            Nominal, {*DecodedName, Tag, std::move(Fields)});
        if (Actual.value() != ConstructorId)
          return Fail("constructor IDs are not canonical");
      }
    }
    if (!need("resources") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized resources section");
    for (std::size_t I = 0; I < Count; ++I) {
      std::string ModuleHex, NameHex, RetainHex, ReleaseHex;
      std::size_t ParameterCount;
      unsigned Abi, Share;
      if (!need("resource") ||
          !(In >> ModuleHex >> NameHex >> ParameterCount) ||
          !claimRecords(ParameterCount))
        return Fail("invalid resource declaration");
      std::vector<model::TypeParameterId> ResourceParameters;
      for (std::size_t P = 0; P < ParameterCount; ++P) {
        std::uint32_t B, O;
        if (!(In >> B >> O) || !TypeParameterAt(B, O))
          return Fail("invalid resource type parameter");
        ResourceParameters.push_back(*TypeParameterAt(B, O));
      }
      if (!(In >> Abi >> Share >> RetainHex >> ReleaseHex) ||
          Abi >
              static_cast<unsigned>(model::ResourceAbiKind::ChannelReceiver) ||
          Share > static_cast<unsigned>(
                      model::ResourceShareability::AlwaysShareable))
        return Fail("invalid resource declaration");
      auto Module = unhex(ModuleHex);
      auto Name = unhex(NameHex);
      auto Release = unhex(ReleaseHex);
      if (!Module || !Name || !Release)
        return Fail("invalid resource spelling");
      std::optional<std::string> Retain;
      if (RetainHex != "-") {
        auto Value = unhex(RetainHex);
        if (!Value)
          return Fail(Value.error());
        Retain = *Value;
      }
      Build.addResource({{model::ModuleIdentity(*Module), *Name},
                         std::move(ResourceParameters),
                         static_cast<model::ResourceAbiKind>(Abi),
                         static_cast<model::ResourceShareability>(Share),
                         std::move(Retain),
                         *Release});
    }
    if (!need("operations") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized operations section");
    std::vector<OperationId> Operations(Count);
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t Old;
      std::size_t TypeParams, EffectVars, Constraints, ParameterCount;
      std::string ModuleHex, EffectNameHex, OperationNameHex;
      if (!need("operation") ||
          !(In >> Old >> ModuleHex >> EffectNameHex >> OperationNameHex >>
            TypeParams) ||
          Old != I || !claimRecords(TypeParams))
        return Fail("invalid operation declaration");
      auto Module = unhex(ModuleHex);
      auto EffectName = unhex(EffectNameHex);
      auto OperationName = unhex(OperationNameHex);
      if (!Module || !EffectName || !OperationName)
        return Fail("invalid operation key");
      std::vector<model::TypeParameterId> OperationTypeParameters;
      for (std::size_t P = 0; P < TypeParams; ++P) {
        std::uint32_t B, O;
        if (!(In >> B >> O) || !TypeParameterAt(B, O))
          return Fail("invalid operation type parameter");
        OperationTypeParameters.push_back(*TypeParameterAt(B, O));
      }
      if (!(In >> EffectVars) || !claimRecords(EffectVars))
        return Fail("invalid operation effect variables");
      std::vector<model::EffectVariableDeclaration> OperationEffectVariables;
      for (std::size_t E = 0; E < EffectVars; ++E) {
        std::uint32_t B, O;
        unsigned K;
        if (!(In >> B >> O >> K) || !EffectVariableAt(B, O) ||
            K > static_cast<unsigned>(model::EffectVariableKind::Opaque))
          return Fail("invalid operation effect variable");
        OperationEffectVariables.push_back(
            {*EffectVariableAt(B, O),
             static_cast<model::EffectVariableKind>(K)});
      }
      if (!(In >> Constraints) || !claimRecords(Constraints))
        return Fail("invalid operation constraints");
      std::vector<model::TypeConstraint> OperationConstraints;
      for (std::size_t C = 0; C < Constraints; ++C) {
        std::uint32_t B, O;
        std::string TM, TN;
        std::size_t A;
        if (!(In >> B >> O >> TM >> TN >> A) || !claimRecords(A) ||
            !TypeParameterAt(B, O))
          return Fail("invalid operation constraint");
        auto TraitModule = unhex(TM);
        auto TraitName = unhex(TN);
        if (!TraitModule || !TraitName)
          return Fail("invalid operation trait key");
        std::vector<model::TypeId> Args;
        for (std::size_t X = 0; X < A; ++X) {
          std::uint32_t T;
          if (!(In >> T) || !TypeAt(T))
            return Fail("invalid constraint argument");
          Args.push_back(*TypeAt(T));
        }
        OperationConstraints.push_back(
            {*TypeParameterAt(B, O),
             {model::ModuleIdentity(*TraitModule), *TraitName},
             std::move(Args)});
      }
      if (!(In >> ParameterCount) || !claimRecords(ParameterCount))
        return Fail("invalid operation parameter count");
      std::vector<OperationParameter> Parameters;
      for (std::size_t P = 0; P < ParameterCount; ++P) {
        std::uint32_t TypeIndex;
        unsigned Ownership;
        if (!(In >> TypeIndex >> Ownership) || !TypeAt(TypeIndex) ||
            Ownership >
                static_cast<unsigned>(model::ParameterOwnership::Consume))
          return Fail("invalid operation parameter");
        Parameters.push_back(
            {*TypeAt(TypeIndex),
             static_cast<model::ParameterOwnership>(Ownership)});
      }
      std::uint32_t ResultType, Effects;
      unsigned ResultOwnership;
      std::uint64_t Fingerprint;
      if (!(In >> ResultType >> ResultOwnership >> Effects >> Fingerprint) ||
          !TypeAt(ResultType) || !EffectAt(Effects) ||
          ResultOwnership >
              static_cast<unsigned>(model::ResultOwnership::Owned))
        return Fail("invalid operation result");
      const auto Actual = Build.addOperation(
          {{{model::ModuleIdentity(*Module), *EffectName}, *OperationName},
           std::move(OperationTypeParameters),
           std::move(OperationEffectVariables),
           std::move(OperationConstraints),
           std::move(Parameters),
           *TypeAt(ResultType),
           static_cast<model::ResultOwnership>(ResultOwnership),
           *EffectAt(Effects),
           Fingerprint});
      if (Actual.value() != Old)
        return Fail("operation IDs are not canonical");
      Operations[I] = Actual;
    }
    if (!need("operationinstances") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized operation instances section");
    std::vector<RawOperationInstance> OperationInstances;
    OperationInstances.reserve(Count);
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t Old, Declaration;
      std::size_t N;
      if (!need("operationinstance") || !(In >> Old >> Declaration >> N) ||
          Old != I || Declaration >= Operations.size() || !claimRecords(N))
        return Fail("invalid operation instance");
      RawOperationInstance Item{};
      Item.Declaration = Declaration;
      for (std::size_t X = 0; X < N; ++X) {
        std::uint32_t V;
        if (!(In >> V) || !TypeAt(V))
          return Fail("invalid instance type argument");
        Item.TypeArguments.push_back(*TypeAt(V));
      }
      if (!(In >> N) || !claimRecords(N))
        return Fail("invalid instance effect arguments");
      for (std::size_t X = 0; X < N; ++X) {
        std::uint32_t V;
        if (!(In >> V) || !EffectAt(V))
          return Fail("invalid instance effect argument");
        Item.EffectArguments.push_back(*EffectAt(V));
      }
      if (!(In >> N) || !claimRecords(N))
        return Fail("invalid instance parameters");
      for (std::size_t X = 0; X < N; ++X) {
        std::uint32_t V;
        unsigned O;
        if (!(In >> V >> O) || !TypeAt(V) ||
            O > static_cast<unsigned>(model::ParameterOwnership::Consume))
          return Fail("invalid instance parameter");
        Item.Parameters.push_back(
            {*TypeAt(V), static_cast<model::ParameterOwnership>(O)});
      }
      std::uint32_t ResultType, SemanticEffects;
      unsigned ResultContract;
      if (!(In >> ResultType >> ResultContract >> SemanticEffects >>
            Item.RuntimeEffects >> Item.RuntimeFingerprint) ||
          !TypeAt(ResultType) || !EffectAt(SemanticEffects) ||
          ResultContract > static_cast<unsigned>(model::ResultOwnership::Owned))
        return Fail("invalid operation instance result");
      Item.ResultType = *TypeAt(ResultType);
      Item.ResultContract = static_cast<model::ResultOwnership>(ResultContract);
      Item.SemanticEffects = *EffectAt(SemanticEffects);
      OperationInstances.push_back(std::move(Item));
    }
    if (!need("runtimeeffects") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized runtime effects section");
    std::vector<RawRuntimeEffectRow> RuntimeEffects;
    RuntimeEffects.reserve(Count);
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t Old, Semantic;
      std::size_t N;
      unsigned Raises, Cancels;
      RawRuntimeEffectRow Item{};
      if (!need("runtimeeffect") || !(In >> Old >> Semantic >> N) || Old != I ||
          !EffectAt(Semantic) || !claimRecords(N))
        return Fail("invalid runtime effect row");
      Item.SemanticRow = *EffectAt(Semantic);
      for (std::size_t X = 0; X < N; ++X) {
        std::uint32_t V;
        if (!(In >> V) || V >= OperationInstances.size())
          return Fail("invalid runtime operation reference");
        Item.Operations.push_back(V);
      }
      if (!(In >> Raises >> Cancels >> Item.StructuralFingerprint) ||
          Raises > 1 || Cancels > 1)
        return Fail("invalid runtime effect flags");
      Item.MayRaise = Raises;
      Item.MayCancel = Cancels;
      RuntimeEffects.push_back(std::move(Item));
    }
    auto ReadBoundaries = [&](std::string_view Section, std::string_view Record,
                              std::vector<std::vector<RawBoundary>> &Rows) {
      std::size_t RowCount;
      if (!need(Section) || !(In >> RowCount) || !claimRecords(RowCount))
        return false;
      Rows.resize(RowCount);
      for (std::size_t I = 0; I < RowCount; ++I) {
        std::uint32_t Old;
        std::size_t N;
        if (!need(Record) || !(In >> Old >> N) || Old != I || !claimRecords(N))
          return false;
        for (std::size_t X = 0; X < N; ++X) {
          char Kind;
          std::uint32_t Value;
          if (!(In >> Kind >> Value) || (Kind != 'h' && Kind != 't'))
            return false;
          Rows[I].push_back({Kind, Value});
        }
      }
      return true;
    };
    std::vector<std::vector<RawBoundary>> BoundaryContracts,
        BoundaryRequirements;
    if (!ReadBoundaries("boundarycontracts", "boundarycontract",
                        BoundaryContracts) ||
        !ReadBoundaries("boundaryrequirements", "boundaryrequirement",
                        BoundaryRequirements))
      return Fail("invalid boundary table");
    if (!need("globals") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized globals section");
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t Old, TypeIndex;
      unsigned Ownership;
      std::string ModuleHex, NameHex;
      if (!need("global") ||
          !(In >> Old >> ModuleHex >> NameHex >> TypeIndex >> Ownership) ||
          Old != I || !TypeAt(TypeIndex) ||
          Ownership > static_cast<unsigned>(OwnershipKind::Owned))
        return Fail("invalid global declaration");
      auto Module = unhex(ModuleHex);
      auto Name = unhex(NameHex);
      if (!Module || !Name)
        return Fail("invalid global symbol");
      const auto Actual =
          Build.addGlobal({{model::ModuleIdentity(*Module), *Name},
                           *TypeAt(TypeIndex),
                           static_cast<OwnershipKind>(Ownership)});
      if (Actual.value() != Old)
        return Fail("global IDs are not canonical");
    }
    if (!need("functions") || !(In >> Count) || !claimRecords(Count))
      return Fail("missing or oversized functions section");
    std::vector<FunctionState> Functions(Count);
    auto readRange = [&](SourceRange &R) {
      R.Source = SourceId(SourceOrdinal);
      return static_cast<bool>(In >> R.Line >> R.Column >> R.Offset >>
                               R.Length);
    };
    auto parseOptionalOrdinal = [&](std::optional<std::uint32_t> &Value) {
      if (!(In >> Token))
        return false;
      if (Token == "-") {
        Value.reset();
        return true;
      }
      std::uint32_t Parsed;
      if (!number(Token, Parsed))
        return false;
      Value = Parsed;
      return true;
    };
    auto readValue = [&](FunctionState &State, std::uint32_t Old,
                         ValueSpec &Spec) {
      if (Old >= MaxRecords || Old != State.ValueMetadataByOrdinal.size())
        return false;
      std::uint32_t TypeIndex;
      unsigned Ownership, Representation;
      std::string BorrowKind, BorrowRoot, Nominal, Requirements, Name;
      if (!(In >> TypeIndex >> Ownership >> BorrowKind >> BorrowRoot >>
            Representation >> Nominal >> Requirements))
        return false;
      auto Type = TypeAt(TypeIndex);
      if (!Type || Ownership > static_cast<unsigned>(OwnershipKind::Owned))
        return false;
      if (Representation >
          static_cast<unsigned>(PhysicalRepresentation::AbiValue))
        return false;
      Spec.Type = *Type;
      Spec.Ownership = static_cast<OwnershipKind>(Ownership);
      if (BorrowKind != "-") {
        unsigned Kind;
        if (!number(BorrowKind, Kind) ||
            Kind > static_cast<unsigned>(
                       BorrowProvenanceKind::ScopedUniqueLoanParameter))
          return false;
        std::optional<ValueId> Root;
        if (BorrowRoot != "-") {
          std::uint32_t Old;
          if (!number(BorrowRoot, Old) || Old >= State.Values.size() ||
              !State.Values[Old].isValid())
            return false;
          Root = State.Values[Old];
        }
        Spec.Borrow =
            BorrowProvenance{static_cast<BorrowProvenanceKind>(Kind), Root};
      }
      if (!readRange(Spec.Range) || !(In >> Name))
        return false;
      auto DecodedName = unhex(Name);
      if (!DecodedName)
        return false;
      Spec.DebugName = std::move(*DecodedName);
      State.ValueMetadataByOrdinal.emplace_back();
      auto &Metadata = State.ValueMetadataByOrdinal.back();
      Metadata.Representation =
          static_cast<PhysicalRepresentation>(Representation);
      if (Nominal != "-") {
        std::uint32_t Index;
        if (!number(Nominal, Index) || Index >= Nominals.size())
          return false;
        Metadata.NominalIdentity = Nominals[Index];
      }
      if (Requirements != "-") {
        std::uint32_t Index;
        if (!number(Requirements, Index))
          return false;
        Metadata.BoundaryRequirements = Index;
      }
      return true;
    };
    for (std::size_t I = 0; I < Count; ++I) {
      std::uint32_t OldFunction, SignatureIndex;
      unsigned EntryKind, Kind, Visibility;
      SourceRange Range;
      std::string NameHex, SymbolModule, SymbolName, Native, Async, Route,
          Entry;
      std::size_t Parameters, Blocks;
      if (!need("function") || !(In >> OldFunction >> NameHex) ||
          !readRange(Range) ||
          !(In >> SignatureIndex >> EntryKind >> Kind >> Visibility >>
            SymbolModule >> SymbolName >> Native >> Async >> Route >> Entry >>
            Parameters >> Blocks) ||
          OldFunction != I || !TypeAt(SignatureIndex) ||
          EntryKind >
              static_cast<unsigned>(FunctionEntryKind::KeyEqualsAdapter) ||
          Kind > static_cast<unsigned>(FunctionDefinitionKind::NativeExtern) ||
          Visibility > static_cast<unsigned>(FunctionVisibility::Public) ||
          !claimRecords(Parameters) || !claimRecords(Blocks))
        return Fail("invalid function declaration");
      auto Name = unhex(NameHex);
      if (!Name)
        return Fail(Name.error());
      FunctionLinkage Linkage{static_cast<FunctionDefinitionKind>(Kind),
                              static_cast<FunctionVisibility>(Visibility)};
      if (SymbolModule != "-" || SymbolName != "-") {
        auto SM = unhex(SymbolModule);
        auto SN = unhex(SymbolName);
        if (!SM || !SN)
          return Fail("invalid function symbol");
        Linkage.Symbol = model::SymbolIdentity{model::ModuleIdentity(*SM), *SN};
      }
      if (Native != "-") {
        auto N = unhex(Native);
        if (!N)
          return Fail(N.error());
        Linkage.NativeSymbol = *N;
      }
      unsigned EnumValue;
      if (Async != "-") {
        if (!number(Async, EnumValue) ||
            EnumValue >
                static_cast<unsigned>(NativeAsyncKind::DedicatedOutcome))
          return Fail("invalid native async kind");
        Linkage.AsyncKind = static_cast<NativeAsyncKind>(EnumValue);
      }
      if (Route != "-") {
        if (!number(Route, EnumValue) ||
            EnumValue >
                static_cast<unsigned>(NativeBoundaryRoute::StableExternal))
          return Fail("invalid native route");
        Linkage.NativeRoute = static_cast<NativeBoundaryRoute>(EnumValue);
      }
      Functions[I].Actual = Build.addFunction(
          {*Name, Range, *TypeAt(SignatureIndex), std::move(Linkage)});
      Functions[I].EntryKind = static_cast<FunctionEntryKind>(EntryKind);
      Functions[I].Values.resize(Parameters);
      for (std::size_t P = 0; P < Parameters; ++P) {
        std::uint32_t Old;
        ValueSpec Spec;
        if (!need("param") || !(In >> Old) || Old != P ||
            Old >= Functions[I].Values.size() ||
            !readValue(Functions[I], Old, Spec))
          return Fail("invalid parameter record");
        Functions[I].Values[Old] =
            Build.addParameter(Functions[I].Actual, std::move(Spec));
      }
      Functions[I].Blocks.resize(Blocks);
      for (std::size_t B = 0; B < Blocks; ++B) {
        std::uint32_t OldBlock;
        std::string BlockName, ControlRegion;
        SourceRange BlockRange;
        std::size_t Arguments;
        if (!need("block") || !(In >> OldBlock >> BlockName) || OldBlock != B ||
            !readRange(BlockRange) || !(In >> Arguments >> ControlRegion) ||
            !claimRecords(Arguments))
          return Fail("invalid block declaration");
        auto Name = unhex(BlockName);
        if (!Name)
          return Fail(Name.error());
        Functions[I].Blocks[B] =
            Build.createBlock(Functions[I].Actual, {*Name, BlockRange});
        Functions[I].ControlRegions.resize(Blocks);
        if (ControlRegion != "-") {
          std::uint32_t Old;
          if (!number(ControlRegion, Old))
            return Fail("invalid control region");
          Functions[I].ControlRegions[B] = Old;
        }
        for (std::size_t A = 0; A < Arguments; ++A) {
          std::uint32_t Old;
          ValueSpec Spec;
          if (!need("arg") || !(In >> Old))
            return Fail("invalid block argument record");
          if (Old != Functions[I].Values.size() || Old >= MaxRecords ||
              !readValue(Functions[I], Old, Spec))
            return Fail("invalid block argument value");
          const auto Actual = Build.addBlockArgument(
              Functions[I].Actual, Functions[I].Blocks[B], std::move(Spec));
          if (Actual.value() != Old)
            return Fail("block argument IDs are not canonical");
          Functions[I].Values.push_back(Actual);
        }
      }
      if ((Kind == static_cast<unsigned>(FunctionDefinitionKind::Definition)) !=
          (Entry != "-"))
        return Fail("function entry disagrees with definition kind");
      if (Entry != "-" && Entry != "0")
        return Fail("canonical function entry must be its first block");
      if (!need("functionmeta"))
        return Fail("missing function metadata");
      std::optional<std::uint32_t> OldRuntime, OldExecution, OldCallable,
          OldBoundary, OldContract, OldAmbient;
      if (!parseOptionalOrdinal(OldRuntime) ||
          !parseOptionalOrdinal(OldExecution) ||
          !parseOptionalOrdinal(OldCallable) ||
          !parseOptionalOrdinal(OldBoundary) ||
          !parseOptionalOrdinal(OldContract) ||
          !parseOptionalOrdinal(OldAmbient))
        return Fail("invalid function metadata");
      Functions[I].RuntimeEffects = OldRuntime;
      auto MapValue = [&](const std::optional<std::uint32_t> &Old,
                          std::optional<ValueId> &Output) {
        if (!Old)
          return true;
        if (*Old >= Functions[I].Values.size() ||
            !Functions[I].Values[*Old].isValid())
          return false;
        Output = Functions[I].Values[*Old];
        return true;
      };
      if (!MapValue(OldExecution, Functions[I].ExecutionContext) ||
          !MapValue(OldCallable, Functions[I].CallableEnvironment) ||
          !MapValue(OldBoundary, Functions[I].BoundaryContext))
        return Fail("hidden function parameter is invalid");
      Functions[I].BoundaryContract = OldContract;
      Functions[I].AmbientRequirements = OldAmbient;
      unsigned HasBinders;
      if (!need("binders") || !(In >> HasBinders) || HasBinders > 1)
        return Fail("invalid generic binder metadata");
      if (HasBinders) {
        model::GenericBinderEnvironment Environment;
        std::size_t N;
        if (!(In >> N) || !claimRecords(N))
          return Fail("invalid inherited type binders");
        for (std::size_t X = 0; X < N; ++X) {
          std::uint32_t B, O;
          if (!(In >> B >> O) || !TypeParameterAt(B, O))
            return Fail("invalid inherited type binder");
          Environment.InheritedTypeParameters.push_back(*TypeParameterAt(B, O));
        }
        if (!(In >> N) || !claimRecords(N))
          return Fail("invalid inherited effect binders");
        for (std::size_t X = 0; X < N; ++X) {
          std::uint32_t B, O;
          unsigned K;
          if (!(In >> B >> O >> K) || !EffectVariableAt(B, O) ||
              K > static_cast<unsigned>(model::EffectVariableKind::Opaque))
            return Fail("invalid inherited effect binder");
          Environment.InheritedEffectVariables.push_back(
              {*EffectVariableAt(B, O),
               static_cast<model::EffectVariableKind>(K)});
        }
        if (!(In >> N) || !claimRecords(N))
          return Fail("invalid declared type binders");
        for (std::size_t X = 0; X < N; ++X) {
          std::uint32_t B, O;
          if (!(In >> B >> O) || !TypeParameterAt(B, O))
            return Fail("invalid declared type binder");
          Environment.DeclaredTypeParameters.push_back(*TypeParameterAt(B, O));
        }
        if (!(In >> N) || !claimRecords(N))
          return Fail("invalid declared effect binders");
        for (std::size_t X = 0; X < N; ++X) {
          std::uint32_t B, O;
          unsigned K;
          if (!(In >> B >> O >> K) || !EffectVariableAt(B, O) ||
              K > static_cast<unsigned>(model::EffectVariableKind::Opaque))
            return Fail("invalid declared effect binder");
          Environment.DeclaredEffectVariables.push_back(
              {*EffectVariableAt(B, O),
               static_cast<model::EffectVariableKind>(K)});
        }
        Functions[I].GenericBinders = std::move(Environment);
      }
      std::size_t CaptureCount;
      if (!need("captures") || !(In >> CaptureCount) ||
          !claimRecords(CaptureCount))
        return Fail("missing generic captures");
      for (std::size_t C = 0; C < CaptureCount; ++C) {
        std::uint64_t Binding;
        std::uint32_t TypeIndex;
        unsigned Ownership;
        std::string CaptureName;
        if (!need("capture") ||
            !(In >> Binding >> TypeIndex >> Ownership >> CaptureName) ||
            !TypeAt(TypeIndex) ||
            Ownership >
                static_cast<unsigned>(model::ParameterOwnership::Consume))
          return Fail("invalid generic capture");
        auto DecodedName = unhex(CaptureName);
        if (!DecodedName)
          return Fail(DecodedName.error());
        Functions[I].GenericCaptures.push_back(
            {semantics::BindingId(Binding), *TypeAt(TypeIndex),
             static_cast<model::ParameterOwnership>(Ownership), *DecodedName});
      }
    }
    auto valueId = [&](FunctionState &F,
                       std::uint32_t Old) -> std::optional<ValueId> {
      if (Old >= F.Values.size() || !F.Values[Old].isValid())
        return std::nullopt;
      return F.Values[Old];
    };
    auto readTarget = [&](FunctionState &F, BranchTarget &Target) {
      std::uint32_t Edge, Block;
      std::size_t Args;
      if (!(In >> Edge >> Block >> Args) || Block >= F.Blocks.size() ||
          !claimRecords(Args))
        return false;
      Target.Block = F.Blocks[Block];
      for (std::size_t A = 0; A < Args; ++A) {
        std::uint32_t Old;
        if (!(In >> Old) || !valueId(F, Old))
          return false;
        Target.Arguments.push_back(*valueId(F, Old));
      }
      return true;
    };
    auto readProducedTarget = [&](FunctionState &F,
                                  ProducedBranchTarget &Target) {
      std::uint32_t Edge, Block;
      std::size_t ProducedCount;
      if (!(In >> Edge >> Block >> ProducedCount) || Block >= F.Blocks.size() ||
          !claimRecords(ProducedCount))
        return false;
      Target.Block = F.Blocks[Block];
      for (std::size_t P = 0; P < ProducedCount; ++P) {
        std::uint32_t TypeIndex;
        unsigned Ownership;
        std::string Nominal, NameHex;
        if (!(In >> TypeIndex >> Ownership >> Nominal >> NameHex) ||
            !TypeAt(TypeIndex) ||
            Ownership > static_cast<unsigned>(OwnershipKind::Owned))
          return false;
        std::optional<NominalTypeId> NominalId;
        if (Nominal != "-") {
          std::uint32_t Old;
          if (!number(Nominal, Old) || Old >= Nominals.size())
            return false;
          NominalId = Nominals[Old];
        }
        auto Name = unhex(NameHex);
        if (!Name)
          return false;
        Target.ProducedPrefix.push_back({*TypeAt(TypeIndex),
                                         static_cast<OwnershipKind>(Ownership),
                                         NominalId, *Name});
      }
      std::size_t ArgumentCount;
      if (!(In >> ArgumentCount) || !claimRecords(ArgumentCount))
        return false;
      for (std::size_t A = 0; A < ArgumentCount; ++A) {
        std::uint32_t Old;
        if (!(In >> Old) || !valueId(F, Old))
          return false;
        Target.Arguments.push_back(*valueId(F, Old));
      }
      return true;
    };
    for (std::size_t Body = 0; Body < Count; ++Body) {
      auto &F = Functions[Body];
      const auto BlockCount = F.Blocks.size();
      for (std::size_t B = 0; B < BlockCount; ++B) {
        std::uint32_t OldFunction, OldBlock;
        std::size_t Instructions;
        if (!need("body") || !(In >> OldFunction >> OldBlock >> Instructions) ||
            OldFunction != Body || OldBlock != B || !claimRecords(Instructions))
          return Fail("invalid function body header");
        for (std::size_t I = 0; I < Instructions; ++I) {
          std::optional<std::uint32_t> OldResult;
          SourceRange InstRange;
          if (!need("inst") || !parseOptionalOrdinal(OldResult) ||
              !readRange(InstRange))
            return Fail("invalid instruction header");
          ValueSpec ResultSpec;
          if (OldResult && (*OldResult != F.Values.size() || !need("value") ||
                            !readValue(F, *OldResult, ResultSpec)))
            return Fail("invalid instruction result");
          if (!(In >> Token))
            return Fail("missing instruction opcode");
          InstructionPayload Payload;
          if (Token == "constant") {
            std::string Kind, Data;
            if (!(In >> Kind))
              return Fail("missing literal kind");
            if (Kind == "unit")
              Payload = ConstantInst{UnitLiteral{}};
            else {
              if (!(In >> Data))
                return Fail("missing literal payload");
              if (Kind == "bool") {
                unsigned V;
                if (!number(Data, V) || V > 1)
                  return Fail("invalid Bool literal");
                Payload = ConstantInst{BoolLiteral{V != 0}};
              } else if (Kind == "byte") {
                unsigned V;
                if (!number(Data, V) || V > 255)
                  return Fail("invalid Byte literal");
                Payload =
                    ConstantInst{ByteLiteral{static_cast<std::uint8_t>(V)}};
              } else if (Kind == "char") {
                std::uint32_t V;
                if (!number(Data, V))
                  return Fail("invalid Char literal");
                Payload = ConstantInst{CharLiteral{static_cast<char32_t>(V)}};
              } else if (Kind == "int") {
                std::int64_t V;
                if (!number(Data, V))
                  return Fail("invalid Int literal");
                Payload = ConstantInst{IntLiteral{V}};
              } else if (Kind == "float") {
                std::uint64_t V;
                if (!number(Data, V, 16))
                  return Fail("invalid Float bits");
                Payload = ConstantInst{FloatLiteral{V}};
              } else if (Kind == "string" || Kind == "symbol") {
                auto V = unhex(Data);
                if (!V)
                  return Fail(V.error());
                Payload =
                    Kind == "string"
                        ? InstructionPayload{ConstantInst{StringLiteral{*V}}}
                        : InstructionPayload{ConstantInst{SymbolLiteral{*V}}};
              } else
                return Fail("unknown literal kind");
            }
          } else if (Token == "unary") {
            unsigned Op;
            std::uint32_t V;
            if (!(In >> Op >> V) ||
                Op > static_cast<unsigned>(UnaryOpcode::BitwiseNot) ||
                !valueId(F, V))
              return Fail("invalid unary instruction");
            Payload = UnaryInst{static_cast<UnaryOpcode>(Op), *valueId(F, V)};
          } else if (Token == "binary") {
            unsigned Op;
            std::uint32_t L, R;
            if (!(In >> Op >> L >> R) ||
                Op > static_cast<unsigned>(
                         BinaryOpcode::GreaterEqualFloatOrdered) ||
                !valueId(F, L) || !valueId(F, R))
              return Fail("invalid binary instruction");
            Payload = BinaryInst{static_cast<BinaryOpcode>(Op), *valueId(F, L),
                                 *valueId(F, R)};
          } else if (Token == "inject") {
            std::uint32_t V, T;
            if (!(In >> V >> T) || !valueId(F, V) || !TypeAt(T))
              return Fail("invalid sum injection");
            Payload = InjectSumInst{*valueId(F, V), *TypeAt(T)};
          } else if (Token == "call") {
            std::uint32_t C;
            std::size_t Args;
            if (!(In >> C >> Args) || C >= Functions.size() ||
                !claimRecords(Args))
              return Fail("invalid direct call");
            DirectCallInst Call{Functions[C].Actual};
            for (std::size_t A = 0; A < Args; ++A) {
              std::uint32_t V;
              if (!(In >> V) || !valueId(F, V))
                return Fail("invalid call argument");
              Call.Arguments.push_back(*valueId(F, V));
            }
            std::optional<std::uint32_t> O;
            if (!parseOptionalOrdinal(O))
              return Fail("invalid call environment");
            if (O) {
              if (!valueId(F, *O))
                return Fail("invalid call environment");
              Call.CallableEnvironment = *valueId(F, *O);
            }
            if (!parseOptionalOrdinal(O))
              return Fail("invalid boundary context");
            if (O) {
              if (!valueId(F, *O))
                return Fail("invalid boundary context");
              Call.BoundaryContext = *valueId(F, *O);
            }
            Payload = std::move(Call);
          } else if (Token == "lexical") {
            std::uint64_t V;
            if (!(In >> V))
              return Fail("invalid lexical reference");
            Payload = LexicalRefInst{semantics::BindingId(V)};
          } else
            return Fail("unknown instruction opcode");
          if (OldResult) {
            const auto Actual =
                Build.emit(F.Actual, F.Blocks[B], std::move(Payload),
                           std::move(ResultSpec));
            if (Actual.value() != *OldResult)
              return Fail("instruction result IDs are not canonical");
            F.Values.push_back(Actual);
          } else
            Build.emitEffect(F.Actual, F.Blocks[B], std::move(Payload),
                             InstRange);
        }
        SourceRange TermRange;
        if (!need("term") || !readRange(TermRange) || !(In >> Token))
          return Fail("invalid terminator header");
        Terminator End;
        if (Token == "return") {
          std::optional<std::uint32_t> V;
          if (!parseOptionalOrdinal(V))
            return Fail("invalid return");
          if (V && !valueId(F, *V))
            return Fail("invalid return value");
          End = Return{V ? std::optional<ValueId>{*valueId(F, *V)}
                         : std::nullopt};
        } else if (Token == "branch") {
          BranchTarget T;
          if (!readTarget(F, T))
            return Fail("invalid branch target");
          End = Branch{std::move(T)};
        } else if (Token == "cond") {
          std::uint32_t C;
          BranchTarget T, Fals;
          if (!(In >> C) || !valueId(F, C) || !readTarget(F, T) ||
              !readTarget(F, Fals))
            return Fail("invalid conditional branch");
          End = CondBranch{*valueId(F, C), std::move(T), std::move(Fals)};
        } else if (Token == "switch") {
          std::uint32_t S;
          std::size_t Cases;
          if (!(In >> S >> Cases) || !valueId(F, S) || !claimRecords(Cases))
            return Fail("invalid switch");
          Switch Sw{*valueId(F, S)};
          for (std::size_t C = 0; C < Cases; ++C) {
            std::int64_t K;
            BranchTarget T;
            if (!(In >> K) || !readTarget(F, T))
              return Fail("invalid switch case");
            Sw.Cases.push_back({K, std::move(T)});
          }
          if (!readTarget(F, Sw.Default))
            return Fail("invalid switch default");
          End = std::move(Sw);
        } else if (Token == "raise") {
          std::uint32_t V;
          if (!(In >> V) || !valueId(F, V))
            return Fail("invalid raise");
          End = Raise{*valueId(F, V)};
        } else if (Token == "perform") {
          std::string ReferenceKind;
          if (!(In >> ReferenceKind))
            return Fail("invalid perform operation");
          OperationReference Reference;
          if (ReferenceKind == "decl") {
            std::uint32_t Declaration;
            std::size_t TypeCount, EffectCount;
            if (!(In >> Declaration >> TypeCount) ||
                Declaration >= Operations.size() || !claimRecords(TypeCount))
              return Fail("invalid perform declaration");
            OperationUse Use{Operations[Declaration]};
            for (std::size_t T = 0; T < TypeCount; ++T) {
              std::uint32_t Index;
              if (!(In >> Index) || !TypeAt(Index))
                return Fail("invalid perform type argument");
              Use.TypeArguments.push_back(*TypeAt(Index));
            }
            if (!(In >> EffectCount) || !claimRecords(EffectCount))
              return Fail("invalid perform effect arguments");
            for (std::size_t E = 0; E < EffectCount; ++E) {
              std::uint32_t Index;
              if (!(In >> Index) || !EffectAt(Index))
                return Fail("invalid perform effect argument");
              Use.EffectArguments.push_back(*EffectAt(Index));
            }
            Reference = std::move(Use);
          } else if (ReferenceKind == "instance") {
            std::uint32_t Instance;
            if (!(In >> Instance) || Instance >= OperationInstances.size())
              return Fail("invalid perform operation instance");
            Reference = OperationInstanceId(F.Actual.domain(), Instance);
          } else
            return Fail("invalid perform operation reference kind");
          std::size_t ArgumentCount;
          if (!(In >> ArgumentCount) || !claimRecords(ArgumentCount))
            return Fail("invalid perform arguments");
          std::vector<ValueId> Arguments;
          for (std::size_t A = 0; A < ArgumentCount; ++A) {
            std::uint32_t Old;
            if (!(In >> Old) || !valueId(F, Old))
              return Fail("invalid perform argument");
            Arguments.push_back(*valueId(F, Old));
          }
          ProducedBranchTarget ResumeTarget;
          std::uint32_t ResumeEffects;
          if (!readProducedTarget(F, ResumeTarget) || !(In >> ResumeEffects) ||
              !EffectAt(ResumeEffects))
            return Fail("invalid perform resume target");
          End = Perform{std::move(Reference), std::move(Arguments),
                        std::move(ResumeTarget), *EffectAt(ResumeEffects)};
        } else if (Token == "resume") {
          std::uint32_t C, A;
          if (!(In >> C >> A) || !valueId(F, C) || !valueId(F, A))
            return Fail("invalid resume");
          End = Resume{*valueId(F, C), *valueId(F, A)};
        } else if (Token == "unreachable")
          End = Unreachable{};
        else
          return Fail("unsupported terminator opcode");
        Build.setTerminator(F.Actual, F.Blocks[B], std::move(End), TermRange);
      }
    }
    if (!need("end") || (In >> Token))
      return Fail("expected end of module");
    auto Result = Build.finish(static_cast<ModulePhase>(Phase));
    const auto ModuleDomain = Result.MutationDomain->identity();
    for (std::size_t I = 0; I < OperationInstances.size(); ++I) {
      auto &R = OperationInstances[I];
      Result.OperationInstances.push_back(
          {OperationInstanceId(ModuleDomain, I), Operations[R.Declaration],
           std::move(R.TypeArguments), std::move(R.EffectArguments),
           std::move(R.Parameters), R.ResultType, R.ResultContract,
           R.SemanticEffects,
           RuntimeEffectRowId(ModuleDomain, R.RuntimeEffects),
           R.RuntimeFingerprint});
    }
    for (std::size_t I = 0; I < RuntimeEffects.size(); ++I) {
      auto &R = RuntimeEffects[I];
      std::vector<OperationInstanceId> Ops;
      for (auto V : R.Operations)
        Ops.emplace_back(ModuleDomain, V);
      Result.RuntimeEffectRows.push_back(
          {RuntimeEffectRowId(ModuleDomain, I), R.SemanticRow, std::move(Ops),
           R.MayRaise, R.MayCancel, R.StructuralFingerprint});
    }
    auto RemapBoundaries = [&](const std::vector<RawBoundary> &Raw) {
      std::vector<ContinuationBoundaryDescriptorId> Out;
      for (auto [K, V] : Raw) {
        if (K == 'h')
          Out.emplace_back(HandlerBoundaryDescriptorId(ModuleDomain, V));
        else
          Out.emplace_back(TryBoundaryDescriptorId(ModuleDomain, V));
      }
      return Out;
    };
    for (std::size_t I = 0; I < BoundaryContracts.size(); ++I)
      Result.BoundaryContextContracts.push_back(
          {BoundaryContextContractId(ModuleDomain, I),
           RemapBoundaries(BoundaryContracts[I])});
    for (std::size_t I = 0; I < BoundaryRequirements.size(); ++I)
      Result.BoundaryRequirementSets.push_back(
          {BoundaryRequirementSetId(ModuleDomain, I),
           RemapBoundaries(BoundaryRequirements[I])});
    for (std::size_t I = 0; I < Functions.size(); ++I) {
      auto &Destination = Result.Functions[I];
      const auto &Parsed = Functions[I];
      Destination.EntryKind = Parsed.EntryKind;
      Destination.GenericBinders = Parsed.GenericBinders;
      Destination.GenericCaptures = Parsed.GenericCaptures;
      if (Parsed.RuntimeEffects)
        Destination.RuntimeEffects = RuntimeEffectRowId(
            Result.MutationDomain->identity(), *Parsed.RuntimeEffects);
      Destination.ExecutionContextParameter = Parsed.ExecutionContext;
      Destination.CallableEnvironmentParameter = Parsed.CallableEnvironment;
      Destination.BoundaryContextParameter = Parsed.BoundaryContext;
      if (Parsed.BoundaryContract)
        Destination.BoundaryContextContract = BoundaryContextContractId(
            Result.MutationDomain->identity(), *Parsed.BoundaryContract);
      if (Parsed.AmbientRequirements)
        Destination.AmbientBoundaryRequirements = BoundaryRequirementSetId(
            Result.MutationDomain->identity(), *Parsed.AmbientRequirements);
      for (std::size_t V = 0; V < Parsed.ValueMetadataByOrdinal.size() &&
                              V < Destination.Values.size();
           ++V) {
        const auto &Metadata = Parsed.ValueMetadataByOrdinal[V];
        Destination.Values[V].Representation = Metadata.Representation;
        Destination.Values[V].NominalIdentity = Metadata.NominalIdentity;
        if (Metadata.BoundaryRequirements)
          Destination.Values[V].CallableBoundaryRequirements =
              BoundaryRequirementSetId(Result.MutationDomain->identity(),
                                       *Metadata.BoundaryRequirements);
      }
      for (std::size_t B = 0; B < Parsed.ControlRegions.size(); ++B)
        if (Parsed.ControlRegions[B])
          Destination.Blocks[B].ControlRegion = ControlRegionId(
              Destination.LocalDomain->identity(), *Parsed.ControlRegions[B]);
    }
    auto Verified = verifyModule(Result);
    if (!Verified)
      return Fail("parsed module failed verification: " +
                  Verified.error().front().Message);
    return Result;
  } catch (const std::exception &Error) {
    return Fail(Error.what());
  }
}
} // namespace yona::typed_ir
