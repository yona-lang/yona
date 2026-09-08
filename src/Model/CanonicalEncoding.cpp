#include "yona/Model/CanonicalEncoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace yona::model {
namespace {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

using Bytes = std::vector<std::byte>;
using Result = std::expected<void, CanonicalEncodingError>;
template <class T> using ValueResult = std::expected<T, CanonicalEncodingError>;

CanonicalEncodingError error(CanonicalEncodingErrorCode Code, std::string Path,
                             std::string Message);

class Writer final {
public:
  void u8(std::uint8_t Value) {
    Bytes_.push_back(static_cast<std::byte>(Value));
  }
  void u32(std::uint32_t Value) {
    for (unsigned Shift = 0; Shift != 32; Shift += 8)
      u8(static_cast<std::uint8_t>((Value >> Shift) & 0xffu));
  }
  void u64(std::uint64_t Value) {
    for (unsigned Shift = 0; Shift != 64; Shift += 8)
      u8(static_cast<std::uint8_t>((Value >> Shift) & 0xffu));
  }
  void raw(std::span<const std::byte> Value) {
    Bytes_.insert(Bytes_.end(), Value.begin(), Value.end());
  }
  void text(std::string_view Value) {
    u64(Value.size());
    for (const unsigned char Byte : Value)
      u8(Byte);
  }
  [[nodiscard]] Bytes take() && { return std::move(Bytes_); }

private:
  Bytes Bytes_;
};

bool byteLess(const Bytes &Left, const Bytes &Right);
bool validIdentifier(std::string_view Value);
Result checkCount(std::size_t Count, std::string Path);
Result validateNominalKey(const NominalTypeKey &Key, std::string Path);
Result validateSymbol(const SymbolIdentity &Identity, std::string Path);
Result validateFunctionIdentity(const FunctionDeclarationIdentity &Identity,
                                std::string Path);
Result validateOperationKey(const EffectOperationKey &Key, std::string Path);
void writeModule(Writer &Out, const ModuleIdentity &Module);
void writeNominalKey(Writer &Out, const NominalTypeKey &Key);
void writeSymbol(Writer &Out, const SymbolIdentity &Identity);
void writeFunctionIdentity(Writer &Out,
                           const FunctionDeclarationIdentity &Identity);
void writeOperationKey(Writer &Out, const EffectOperationKey &Key);

struct EffectBinding {
  CanonicalBinderRef Ref;
  EffectVariableKind Kind;
};
struct BindingMaps {
  std::unordered_map<TypeParameterId, CanonicalBinderRef, StrongIdHasher> Types;
  std::unordered_map<EffectVariableId, EffectBinding, StrongIdHasher> Effects;
};
struct OwnerRecord {
  BinderOwnerKind Kind;
  CanonicalBinderOwnerIdentity Identity;
  std::uint32_t TypeParameterCount;
  std::vector<EffectVariableKind> EffectVariableKinds;
};

class GraphCodec final {
public:
  GraphCodec(const TypeTable &Types, const BindingMaps &Bindings,
             const std::vector<OwnerRecord> *Owners,
             CanonicalEncodingErrorCode MissingBinderCode)
      : Types_(Types), Bindings_(Bindings), Owners_(Owners),
        MissingBinderCode_(MissingBinderCode), TypeState_(Types.typeCount()),
        EffectState_(Types.effectCount()), TypeKeys_(Types.typeCount()),
        EffectKeys_(Types.effectCount()) {}

  [[nodiscard]] ValueResult<const Bytes *> expandedType(TypeId Id,
                                                        std::string Path);
  [[nodiscard]] ValueResult<const Bytes *> expandedEffect(EffectRowId Id,
                                                          std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  typeRecord(TypeId Id, std::span<const std::uint32_t> TypeIndices,
             std::span<const std::uint32_t> EffectIndices, std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  effectRecord(EffectRowId Id, std::span<const std::uint32_t> TypeIndices,
               std::span<const std::uint32_t> EffectIndices, std::string Path);
  [[nodiscard]] Result collectType(TypeId Id, std::vector<bool> &Types,
                                   std::vector<bool> &Effects,
                                   std::string Path);
  [[nodiscard]] Result collectEffect(EffectRowId Id, std::vector<bool> &Types,
                                     std::vector<bool> &Effects,
                                     std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  expandedConstraint(const TypeConstraint &Constraint, std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  expandedApplication(const EffectOperationApplication &Application,
                      std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  constraintRecord(const TypeConstraint &Constraint,
                   std::span<const std::uint32_t> TypeIndices,
                   std::string Path) const;
  [[nodiscard]] ValueResult<Bytes>
  applicationRecord(const EffectOperationApplication &Application,
                    std::span<const std::uint32_t> TypeIndices,
                    std::span<const std::uint32_t> EffectIndices,
                    std::string Path) const;
  [[nodiscard]] ValueResult<Bytes>
  operationSignatureRecord(const EffectOperationSignature &Signature,
                           std::span<const std::uint32_t> TypeIndices,
                           std::span<const std::uint32_t> EffectIndices,
                           std::string Path);
  [[nodiscard]] ValueResult<Bytes>
  closedOperationRecord(const ClosedEffectOperation &Operation,
                        std::span<const std::uint32_t> TypeIndices,
                        std::span<const std::uint32_t> EffectIndices,
                        std::string Path);
  [[nodiscard]] Result collect(const ClosedEffectOperation &Operation,
                               std::vector<bool> &Types,
                               std::vector<bool> &Effects, std::string Path);

private:
  [[nodiscard]] ValueResult<CanonicalBinderRef> typeRef(TypeParameterId Id,
                                                        std::string Path) const;
  [[nodiscard]] ValueResult<EffectBinding> effectRef(EffectVariableId Id,
                                                     std::string Path) const;
  [[nodiscard]] Result validateFunctionBinders(const FunctionType &Type,
                                               std::string Path) const;
  [[nodiscard]] Result
  validateOperationBinders(const EffectOperationSignature &Signature,
                           std::string Path) const;
  [[nodiscard]] ValueResult<Bytes> expandedTypePayload(TypeId Id,
                                                       std::string Path);
  [[nodiscard]] ValueResult<Bytes> expandedEffectPayload(EffectRowId Id,
                                                         std::string Path);

  const TypeTable &Types_;
  const BindingMaps &Bindings_;
  const std::vector<OwnerRecord> *Owners_;
  CanonicalEncodingErrorCode MissingBinderCode_;
  std::vector<std::uint8_t> TypeState_;
  std::vector<std::uint8_t> EffectState_;
  std::vector<std::optional<Bytes>> TypeKeys_;
  std::vector<std::optional<Bytes>> EffectKeys_;
};

struct CanonicalGraphOrder {
  std::vector<TypeId> Types;
  std::vector<EffectRowId> Effects;
  std::vector<std::uint32_t> TypeIndices;
  std::vector<std::uint32_t> EffectIndices;
};

ValueResult<CanonicalGraphOrder>
makeGraphOrder(const TypeTable &Table, GraphCodec &Codec,
               const std::vector<bool> &ReachableTypes,
               const std::vector<bool> &ReachableEffects);

bool validUtf8(std::span<const std::byte> BytesValue) {
  std::size_t Index = 0;
  while (Index < BytesValue.size()) {
    const auto First = std::to_integer<std::uint8_t>(BytesValue[Index++]);
    if (First <= 0x7f)
      continue;
    std::uint32_t CodePoint = 0;
    std::size_t Remaining = 0;
    if ((First & 0xe0u) == 0xc0u) {
      CodePoint = First & 0x1fu;
      Remaining = 1;
      if (CodePoint < 2)
        return false;
    } else if ((First & 0xf0u) == 0xe0u) {
      CodePoint = First & 0x0fu;
      Remaining = 2;
    } else if ((First & 0xf8u) == 0xf0u) {
      CodePoint = First & 0x07u;
      Remaining = 3;
    } else {
      return false;
    }
    if (Remaining > BytesValue.size() - Index)
      return false;
    for (std::size_t Count = 0; Count < Remaining; ++Count) {
      const auto Continuation =
          std::to_integer<std::uint8_t>(BytesValue[Index++]);
      if ((Continuation & 0xc0u) != 0x80u)
        return false;
      CodePoint = (CodePoint << 6u) | (Continuation & 0x3fu);
    }
    if ((Remaining == 2 && CodePoint < 0x800u) ||
        (Remaining == 3 && CodePoint < 0x10000u) || CodePoint > 0x10ffffu ||
        (CodePoint >= 0xd800u && CodePoint <= 0xdfffu))
      return false;
  }
  return true;
}

class Reader final {
public:
  Reader(std::span<const std::byte> BytesValue, std::string Path,
         CanonicalDecodeBudget *Budget = nullptr)
      : Bytes_(BytesValue), Path_(std::move(Path)), Budget_(Budget) {}

  [[nodiscard]] ValueResult<std::uint8_t> u8(std::string_view Field) {
    if (Offset_ >= Bytes_.size())
      return truncated<std::uint8_t>(Field);
    return std::to_integer<std::uint8_t>(Bytes_[Offset_++]);
  }
  [[nodiscard]] ValueResult<std::uint32_t> u32(std::string_view Field) {
    if (Bytes_.size() - Offset_ < 4)
      return truncated<std::uint32_t>(Field);
    std::uint32_t ResultValue = 0;
    for (unsigned Index = 0; Index != 4; ++Index)
      ResultValue |= std::to_integer<std::uint32_t>(Bytes_[Offset_++])
                     << (Index * 8u);
    return ResultValue;
  }
  [[nodiscard]] ValueResult<std::uint64_t> u64(std::string_view Field) {
    if (Bytes_.size() - Offset_ < 8)
      return truncated<std::uint64_t>(Field);
    std::uint64_t ResultValue = 0;
    for (unsigned Index = 0; Index != 8; ++Index)
      ResultValue |= std::to_integer<std::uint64_t>(Bytes_[Offset_++])
                     << (Index * 8u);
    return ResultValue;
  }
  [[nodiscard]] ValueResult<std::uint32_t> count(std::string_view Field) {
    auto Count = u64(Field);
    if (!Count)
      return std::unexpected(Count.error());
    if (*Count > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                   fieldPath(Field),
                                   "count exceeds UINT32_MAX"));
    if (Budget_)
      if (auto Claimed = Budget_->claim(*Count, fieldPath(Field)); !Claimed)
        return std::unexpected(Claimed.error());
    return static_cast<std::uint32_t>(*Count);
  }
  [[nodiscard]] ValueResult<std::uint32_t> u32Count(std::string_view Field) {
    auto Count = u32(Field);
    if (!Count)
      return std::unexpected(Count.error());
    if (Budget_)
      if (auto Claimed = Budget_->claim(*Count, fieldPath(Field)); !Claimed)
        return std::unexpected(Claimed.error());
    return *Count;
  }
  [[nodiscard]] ValueResult<std::string> text(std::string_view Field) {
    auto Size = count(Field);
    if (!Size)
      return std::unexpected(Size.error());
    if (*Size > Bytes_.size() - Offset_)
      return truncated<std::string>(Field);
    const auto Value = Bytes_.subspan(Offset_, *Size);
    if (!validUtf8(Value))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                   fieldPath(Field), "invalid UTF-8"));
    Offset_ += *Size;
    return std::string(reinterpret_cast<const char *>(Value.data()),
                       Value.size());
  }
  [[nodiscard]] ValueResult<Reader> subreader(std::uint64_t Size,
                                              std::string_view Field) {
    if (Size > Bytes_.size() - Offset_)
      return truncated<Reader>(Field);
    Reader ResultReader(Bytes_.subspan(Offset_, static_cast<std::size_t>(Size)),
                        fieldPath(Field), Budget_);
    Offset_ += static_cast<std::size_t>(Size);
    return ResultReader;
  }
  [[nodiscard]] Result finish() const {
    if (Offset_ != Bytes_.size())
      return std::unexpected(
          error(CanonicalEncodingErrorCode::NonCanonicalOrder, Path_,
                "trailing bytes"));
    return {};
  }
  [[nodiscard]] std::size_t position() const noexcept { return Offset_; }
  [[nodiscard]] Bytes slice(std::size_t Begin, std::size_t End) const {
    return Bytes(Bytes_.begin() + static_cast<std::ptrdiff_t>(Begin),
                 Bytes_.begin() + static_cast<std::ptrdiff_t>(End));
  }

private:
  template <class T>
  [[nodiscard]] ValueResult<T> truncated(std::string_view Field) const {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 fieldPath(Field), "truncated payload"));
  }
  [[nodiscard]] std::string fieldPath(std::string_view Field) const {
    return Field.empty() ? Path_ : Path_ + "." + std::string(Field);
  }

  std::span<const std::byte> Bytes_;
  std::string Path_;
  CanonicalDecodeBudget *Budget_ = nullptr;
  std::size_t Offset_ = 0;
};

ValueResult<ModuleIdentity> readModule(Reader &In, std::string_view Path) {
  auto Name = In.text(Path);
  if (!Name)
    return std::unexpected(Name.error());
  try {
    ModuleIdentity Identity(*Name);
    if (Identity.fqn() != *Name)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                   std::string(Path),
                                   "noncanonical module identity"));
    return Identity;
  } catch (const std::invalid_argument &Exception) {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                 std::string(Path), Exception.what()));
  }
}

ValueResult<NominalTypeKey> readNominalKey(Reader &In, std::string_view Path) {
  auto Module = readModule(In, "module");
  if (!Module)
    return std::unexpected(Module.error());
  auto Name = In.text("name");
  if (!Name)
    return std::unexpected(Name.error());
  NominalTypeKey Key{std::move(*Module), std::move(*Name)};
  if (auto Valid = validateNominalKey(Key, std::string(Path)); !Valid)
    return std::unexpected(Valid.error());
  return Key;
}

ValueResult<SymbolIdentity> readSymbol(Reader &In, std::string_view Path) {
  auto Module = readModule(In, "module");
  if (!Module)
    return std::unexpected(Module.error());
  auto Name = In.text("name");
  if (!Name)
    return std::unexpected(Name.error());
  SymbolIdentity Identity{std::move(*Module), std::move(*Name)};
  if (auto Valid = validateSymbol(Identity, std::string(Path)); !Valid)
    return std::unexpected(Valid.error());
  return Identity;
}

ValueResult<FunctionDeclarationIdentity>
readFunctionIdentity(Reader &In, std::string_view Path) {
  auto Kind = In.u8("kind");
  if (!Kind)
    return std::unexpected(Kind.error());
  if (*Kind == 0) {
    auto Symbol = readSymbol(In, Path);
    if (!Symbol)
      return std::unexpected(Symbol.error());
    return FunctionDeclarationIdentity{std::move(*Symbol)};
  }
  if (*Kind == 1) {
    auto Module = readModule(In, "module");
    if (!Module)
      return std::unexpected(Module.error());
    auto Count = In.count("lexicalPath");
    if (!Count)
      return std::unexpected(Count.error());
    if (*Count == 0)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                   std::string(Path),
                                   "empty local lexical path"));
    std::vector<std::uint32_t> Parts;
    Parts.reserve(*Count);
    for (std::uint32_t Index = 0; Index < *Count; ++Index) {
      auto Part = In.u32("lexicalPath");
      if (!Part)
        return std::unexpected(Part.error());
      Parts.push_back(*Part);
    }
    return FunctionDeclarationIdentity{
        LocalFunctionIdentity{std::move(*Module), std::move(Parts)}};
  }
  return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                               std::string(Path),
                               "unknown function identity discriminant"));
}

ValueResult<EffectOperationKey> readOperationKey(Reader &In,
                                                 std::string_view Path) {
  auto Effect = readNominalKey(In, "effect");
  if (!Effect)
    return std::unexpected(Effect.error());
  auto Operation = In.text("operation");
  if (!Operation)
    return std::unexpected(Operation.error());
  EffectOperationKey Key{std::move(*Effect), std::move(*Operation)};
  if (auto Valid = validateOperationKey(Key, std::string(Path)); !Valid)
    return std::unexpected(Valid.error());
  return Key;
}

struct DecodedLayout {
  std::vector<DecodedBinderDeclaration> Binders;
  BindingMaps Bindings;
  std::vector<OwnerRecord> Owners;
  std::vector<DecodedTypeParameterMapping> TypeMappings;
  std::vector<DecodedEffectVariableMapping> EffectMappings;
};

ValueResult<DecodedLayout> readBinders(Reader &In, TypeTable &Types) {
  auto Count = In.u32Count("binderCount");
  if (!Count)
    return std::unexpected(Count.error());
  DecodedLayout Layout;
  Layout.Binders.reserve(*Count);
  Layout.Owners.reserve(*Count);
  std::optional<BinderOwnerKind> PreviousKind;
  std::optional<Bytes> PreviousIdentity;
  std::uint32_t ExpectedInstanceIndex = 0;
  for (std::uint32_t Index = 0; Index < *Count; ++Index) {
    auto KindByte = In.u8("binder.kind");
    if (!KindByte)
      return std::unexpected(KindByte.error());
    if (*KindByte > std::to_underlying(BinderOwnerKind::Resource))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   "binders", "unknown owner kind"));
    const auto Kind = static_cast<BinderOwnerKind>(*KindByte);
    if (PreviousKind && Kind < *PreviousKind)
      return std::unexpected(
          error(CanonicalEncodingErrorCode::NonCanonicalOrder, "binders",
                "binder kinds are not ordered"));

    DecodedBinderOwnerIdentity DecodedIdentity = std::uint32_t{0};
    CanonicalBinderOwnerIdentity OwnerIdentity =
        NominalTypeKey{ModuleIdentity{"Decoded"}, "Placeholder"};
    Writer IdentityWriter;
    if (Kind == BinderOwnerKind::Nominal || Kind == BinderOwnerKind::Trait ||
        Kind == BinderOwnerKind::Resource) {
      auto Identity = readNominalKey(In, "binder.identity");
      if (!Identity)
        return std::unexpected(Identity.error());
      writeNominalKey(IdentityWriter, *Identity);
      DecodedIdentity = *Identity;
      OwnerIdentity = *Identity;
    } else if (Kind == BinderOwnerKind::Function) {
      auto Identity = readFunctionIdentity(In, "binder.identity");
      if (!Identity)
        return std::unexpected(Identity.error());
      writeFunctionIdentity(IdentityWriter, *Identity);
      DecodedIdentity = *Identity;
      OwnerIdentity = *Identity;
    } else if (Kind == BinderOwnerKind::Operation) {
      auto Identity = readOperationKey(In, "binder.identity");
      if (!Identity)
        return std::unexpected(Identity.error());
      writeOperationKey(IdentityWriter, *Identity);
      DecodedIdentity = *Identity;
      OwnerIdentity = *Identity;
    } else {
      auto InstanceIndex = In.u32("binder.instanceIndex");
      if (!InstanceIndex)
        return std::unexpected(InstanceIndex.error());
      if (*InstanceIndex != ExpectedInstanceIndex++)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::NonCanonicalOrder, "binders",
                  "instance indices are not dense canonical ordinals"));
      IdentityWriter.u32(*InstanceIndex);
      DecodedIdentity = *InstanceIndex;
      OwnerIdentity = InstanceBinderOwner{
          NominalTypeKey{ModuleIdentity{"Decoded"}, "Instance"}, {}, {}};
    }
    auto IdentityBytes = std::move(IdentityWriter).take();
    if (Kind != BinderOwnerKind::Instance && PreviousKind &&
        Kind == *PreviousKind && PreviousIdentity &&
        !byteLess(*PreviousIdentity, IdentityBytes))
      return std::unexpected(
          error(CanonicalEncodingErrorCode::NonCanonicalOrder, "binders",
                "binder identities are not strictly ordered"));
    PreviousKind = Kind;
    PreviousIdentity = IdentityBytes;

    auto TypeCount = In.u32Count("binder.typeParameterCount");
    auto EffectCount = In.count("binder.effectVariableKinds");
    if (!TypeCount)
      return std::unexpected(TypeCount.error());
    if (!EffectCount)
      return std::unexpected(EffectCount.error());
    if (Kind == BinderOwnerKind::Function && *TypeCount == 0 &&
        *EffectCount == 0)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   "binders", "empty function binder"));
    if ((Kind == BinderOwnerKind::Nominal || Kind == BinderOwnerKind::Trait ||
         Kind == BinderOwnerKind::Instance ||
         Kind == BinderOwnerKind::Resource) &&
        *EffectCount != 0)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   "binders",
                                   "owner kind cannot declare effects"));
    std::vector<TypeParameterId> TypeParameters;
    TypeParameters.reserve(*TypeCount);
    for (std::uint32_t Ordinal = 0; Ordinal < *TypeCount; ++Ordinal) {
      const auto Parameter = Types.allocateTypeParameter();
      if (!Parameter.isValid())
        return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                     "binders",
                                     "unable to allocate type parameter"));
      TypeParameters.push_back(Parameter);
      const auto Ref = CanonicalBinderRef{Index, Ordinal};
      Layout.Bindings.Types.emplace(Parameter, Ref);
      Layout.TypeMappings.push_back({Ref, Parameter});
    }
    std::vector<EffectVariableDeclaration> EffectVariables;
    EffectVariables.reserve(*EffectCount);
    for (std::uint32_t Ordinal = 0; Ordinal < *EffectCount; ++Ordinal) {
      auto KindValue = In.u8("binder.effectVariableKind");
      if (!KindValue)
        return std::unexpected(KindValue.error());
      if (*KindValue > std::to_underlying(EffectVariableKind::Opaque))
        return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                     "binders",
                                     "unknown effect variable kind"));
      const auto Variable = Types.allocateEffectVariable();
      if (!Variable.isValid())
        return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                     "binders",
                                     "unable to allocate effect variable"));
      const auto VariableKind = static_cast<EffectVariableKind>(*KindValue);
      if (Kind == BinderOwnerKind::Operation &&
          VariableKind != EffectVariableKind::Flexible)
        return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                     "binders",
                                     "operation variable is not flexible"));
      EffectVariables.push_back({Variable, VariableKind});
      const auto Ref = CanonicalBinderRef{Index, Ordinal};
      Layout.Bindings.Effects.emplace(Variable,
                                      EffectBinding{Ref, VariableKind});
      Layout.EffectMappings.push_back({Ref, Variable});
    }
    std::vector<EffectVariableKind> EffectKinds;
    EffectKinds.reserve(EffectVariables.size());
    for (const auto &Variable : EffectVariables)
      EffectKinds.push_back(Variable.Kind);
    Layout.Owners.push_back({Kind, std::move(OwnerIdentity),
                             static_cast<std::uint32_t>(TypeParameters.size()),
                             std::move(EffectKinds)});
    Layout.Binders.push_back({Kind, std::move(DecodedIdentity),
                              std::move(TypeParameters),
                              std::move(EffectVariables)});
  }
  return Layout;
}

class DecodeContext final {
public:
  DecodeContext(TypeTableAppendTransaction &Transaction,
                const DecodedLayout &Layout, std::vector<TypeId> TypeIds,
                std::vector<EffectRowId> EffectIds)
      : Transaction_(Transaction), Layout_(Layout),
        TypeIds_(std::move(TypeIds)), EffectIds_(std::move(EffectIds)) {}

  [[nodiscard]] ValueResult<Bytes> readTypeRecord(Reader &In,
                                                  std::uint32_t Index);
  [[nodiscard]] ValueResult<Bytes> readEffectRecord(Reader &In,
                                                    std::uint32_t Index);
  [[nodiscard]] const std::vector<TypeId> &typeIds() const noexcept {
    return TypeIds_;
  }
  [[nodiscard]] const std::vector<EffectRowId> &effectIds() const noexcept {
    return EffectIds_;
  }

private:
  [[nodiscard]] ValueResult<TypeId> typeRef(Reader &In,
                                            std::string_view Path) const;
  [[nodiscard]] ValueResult<EffectRowId> effectRef(Reader &In,
                                                   std::string_view Path) const;
  [[nodiscard]] ValueResult<TypeParameterId>
  typeParameterRef(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<EffectVariableDeclaration>
  effectVariableRef(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<TypeConstraint>
  readConstraint(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<EffectOperationApplication>
  readApplication(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<std::vector<TypeId>>
  readTypeRefs(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<std::vector<EffectRowId>>
  readEffectRefs(Reader &In, std::string_view Path) const;
  [[nodiscard]] ValueResult<bool> boolean(Reader &In,
                                          std::string_view Path) const;

  TypeTableAppendTransaction &Transaction_;
  const DecodedLayout &Layout_;
  std::vector<TypeId> TypeIds_;
  std::vector<EffectRowId> EffectIds_;
};

ValueResult<TypeId> DecodeContext::typeRef(Reader &In,
                                           std::string_view Path) const {
  auto Index = In.u32(Path);
  if (!Index)
    return std::unexpected(Index.error());
  if (*Index >= TypeIds_.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::string(Path),
                                 "type index is out of range"));
  return TypeIds_[*Index];
}

ValueResult<EffectRowId> DecodeContext::effectRef(Reader &In,
                                                  std::string_view Path) const {
  auto Index = In.u32(Path);
  if (!Index)
    return std::unexpected(Index.error());
  if (*Index >= EffectIds_.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::string(Path),
                                 "effect index is out of range"));
  return EffectIds_[*Index];
}

ValueResult<TypeParameterId>
DecodeContext::typeParameterRef(Reader &In, std::string_view Path) const {
  auto Binder = In.u32("binderIndex");
  auto Ordinal = In.u32("ordinal");
  if (!Binder)
    return std::unexpected(Binder.error());
  if (!Ordinal)
    return std::unexpected(Ordinal.error());
  if (*Binder >= Layout_.Binders.size() ||
      *Ordinal >= Layout_.Binders[*Binder].TypeParameters.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::string(Path),
                                 "type parameter reference is out of range"));
  return Layout_.Binders[*Binder].TypeParameters[*Ordinal];
}

ValueResult<EffectVariableDeclaration>
DecodeContext::effectVariableRef(Reader &In, std::string_view Path) const {
  auto Binder = In.u32("binderIndex");
  auto Ordinal = In.u32("ordinal");
  if (!Binder)
    return std::unexpected(Binder.error());
  if (!Ordinal)
    return std::unexpected(Ordinal.error());
  if (*Binder >= Layout_.Binders.size() ||
      *Ordinal >= Layout_.Binders[*Binder].EffectVariables.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::string(Path),
                                 "effect variable reference is out of range"));
  return Layout_.Binders[*Binder].EffectVariables[*Ordinal];
}

ValueResult<bool> DecodeContext::boolean(Reader &In,
                                         std::string_view Path) const {
  auto Value = In.u8(Path);
  if (!Value)
    return std::unexpected(Value.error());
  if (*Value > 1)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::string(Path), "invalid boolean"));
  return *Value != 0;
}

ValueResult<std::vector<TypeId>>
DecodeContext::readTypeRefs(Reader &In, std::string_view Path) const {
  auto Count = In.count(Path);
  if (!Count)
    return std::unexpected(Count.error());
  std::vector<TypeId> ResultValues;
  ResultValues.reserve(*Count);
  for (std::uint32_t Index = 0; Index < *Count; ++Index) {
    auto Ref = typeRef(In, Path);
    if (!Ref)
      return std::unexpected(Ref.error());
    ResultValues.push_back(*Ref);
  }
  return ResultValues;
}

ValueResult<std::vector<EffectRowId>>
DecodeContext::readEffectRefs(Reader &In, std::string_view Path) const {
  auto Count = In.count(Path);
  if (!Count)
    return std::unexpected(Count.error());
  std::vector<EffectRowId> ResultValues;
  ResultValues.reserve(*Count);
  for (std::uint32_t Index = 0; Index < *Count; ++Index) {
    auto Ref = effectRef(In, Path);
    if (!Ref)
      return std::unexpected(Ref.error());
    ResultValues.push_back(*Ref);
  }
  return ResultValues;
}

ValueResult<TypeConstraint>
DecodeContext::readConstraint(Reader &In, std::string_view Path) const {
  auto Parameter = typeParameterRef(In, Path);
  if (!Parameter)
    return std::unexpected(Parameter.error());
  auto Trait = readNominalKey(In, Path);
  if (!Trait)
    return std::unexpected(Trait.error());
  auto Arguments = readTypeRefs(In, Path);
  if (!Arguments)
    return std::unexpected(Arguments.error());
  return TypeConstraint{*Parameter, std::move(*Trait), std::move(*Arguments)};
}

ValueResult<EffectOperationApplication>
DecodeContext::readApplication(Reader &In, std::string_view Path) const {
  auto Key = readOperationKey(In, Path);
  if (!Key)
    return std::unexpected(Key.error());
  auto Types = readTypeRefs(In, Path);
  if (!Types)
    return std::unexpected(Types.error());
  auto Effects = readEffectRefs(In, Path);
  if (!Effects)
    return std::unexpected(Effects.error());
  return EffectOperationApplication{std::move(*Key), std::move(*Types),
                                    std::move(*Effects)};
}

ValueResult<Bytes> DecodeContext::readTypeRecord(Reader &In,
                                                 std::uint32_t Index) {
  const auto Begin = In.position();
  auto Tag = In.u8("type.tag");
  auto PayloadSize = In.u64("type.payloadSize");
  if (!Tag)
    return std::unexpected(Tag.error());
  if (!PayloadSize)
    return std::unexpected(PayloadSize.error());
  if (*Tag > 18)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "types", "unknown structural type tag"));
  auto PayloadValue = In.subreader(*PayloadSize, "type.payload");
  if (!PayloadValue)
    return std::unexpected(PayloadValue.error());
  auto &Payload = *PayloadValue;
  std::optional<StructuralType> Decoded;
  switch (*Tag) {
  case 0: {
    auto Kind = Payload.u8("primitive");
    if (!Kind)
      return std::unexpected(Kind.error());
    if (*Kind > std::to_underlying(PrimitiveType::Byte))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "unknown primitive type"));
    Decoded.emplace(static_cast<PrimitiveType>(*Kind));
    break;
  }
  case 1: {
    auto Parameter = typeParameterRef(Payload, "typeParameter");
    if (!Parameter)
      return std::unexpected(Parameter.error());
    Decoded.emplace(TypeParameterType{*Parameter});
    break;
  }
  case 2: {
    auto Elements = readTypeRefs(Payload, "elements");
    if (!Elements)
      return std::unexpected(Elements.error());
    Decoded.emplace(TupleType{std::move(*Elements)});
    break;
  }
  case 3: {
    auto Element = typeRef(Payload, "element");
    if (!Element)
      return std::unexpected(Element.error());
    Decoded.emplace(SequenceType{*Element});
    break;
  }
  case 4: {
    auto Element = typeRef(Payload, "element");
    if (!Element)
      return std::unexpected(Element.error());
    Decoded.emplace(SetType{*Element});
    break;
  }
  case 5: {
    auto Key = typeRef(Payload, "key");
    auto Value = typeRef(Payload, "value");
    if (!Key)
      return std::unexpected(Key.error());
    if (!Value)
      return std::unexpected(Value.error());
    Decoded.emplace(DictionaryType{*Key, *Value});
    break;
  }
  case 6: {
    auto Count = Payload.count("fields");
    if (!Count)
      return std::unexpected(Count.error());
    std::vector<RecordField> Fields;
    Fields.reserve(*Count);
    for (std::uint32_t FieldIndex = 0; FieldIndex < *Count; ++FieldIndex) {
      auto Name = Payload.text("field.name");
      auto Type = typeRef(Payload, "field.type");
      if (!Name)
        return std::unexpected(Name.error());
      if (!Type)
        return std::unexpected(Type.error());
      if (!validIdentifier(*Name))
        return std::unexpected(
            error(CanonicalEncodingErrorCode::InvalidIdentity, "types",
                  "invalid record field name"));
      Fields.push_back({std::move(*Name), *Type});
    }
    auto Present = boolean(Payload, "rowRest.present");
    if (!Present)
      return std::unexpected(Present.error());
    std::optional<TypeId> Rest;
    if (*Present) {
      auto Type = typeRef(Payload, "rowRest");
      if (!Type)
        return std::unexpected(Type.error());
      Rest = *Type;
    }
    Decoded.emplace(RecordType{std::move(Fields), Rest});
    break;
  }
  case 7: {
    auto Key = readNominalKey(Payload, "nominal.key");
    auto Arguments = readTypeRefs(Payload, "nominal.arguments");
    if (!Key)
      return std::unexpected(Key.error());
    if (!Arguments)
      return std::unexpected(Arguments.error());
    Decoded.emplace(NominalType{std::move(*Key), std::move(*Arguments)});
    break;
  }
  case 8: {
    auto Element = typeRef(Payload, "array.element");
    auto Mutable = boolean(Payload, "array.mutable");
    if (!Element)
      return std::unexpected(Element.error());
    if (!Mutable)
      return std::unexpected(Mutable.error());
    Decoded.emplace(ArrayType{*Element, *Mutable});
    break;
  }
  case 9: {
    auto Element = typeRef(Payload, "channel.element");
    if (!Element)
      return std::unexpected(Element.error());
    Decoded.emplace(ChannelType{*Element});
    break;
  }
  case 10: {
    auto Element = typeRef(Payload, "promise.element");
    auto Contract = Payload.u8("promise.resultContract");
    auto Effects = effectRef(Payload, "promise.effects");
    if (!Element)
      return std::unexpected(Element.error());
    if (!Contract)
      return std::unexpected(Contract.error());
    if (*Contract > std::to_underlying(ResultOwnership::Owned))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "invalid promise ownership"));
    if (!Effects)
      return std::unexpected(Effects.error());
    Decoded.emplace(PromiseType{
        *Element, static_cast<ResultOwnership>(*Contract), *Effects});
    break;
  }
  case 11:
  case 12: {
    auto Element = typeRef(Payload, "cursor.element");
    auto Contract = Payload.u8("cursor.elementContract");
    if (!Element)
      return std::unexpected(Element.error());
    if (!Contract)
      return std::unexpected(Contract.error());
    if (*Contract > std::to_underlying(ResultOwnership::Owned))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "invalid cursor ownership"));
    if (*Tag == 11)
      Decoded.emplace(
          CursorType{*Element, static_cast<ResultOwnership>(*Contract)});
    else
      Decoded.emplace(
          CursorStepType{*Element, static_cast<ResultOwnership>(*Contract)});
    break;
  }
  case 13:
    Decoded.emplace(OutcomeTaskGroupType{});
    break;
  case 14: {
    auto Key = readNominalKey(Payload, "resource.key");
    auto Arguments = readTypeRefs(Payload, "resource.arguments");
    if (!Key)
      return std::unexpected(Key.error());
    if (!Arguments)
      return std::unexpected(Arguments.error());
    Decoded.emplace(ResourceType{std::move(*Key), std::move(*Arguments)});
    break;
  }
  case 15: {
    auto Kind = Payload.u8("opaque.kind");
    if (!Kind)
      return std::unexpected(Kind.error());
    if (*Kind > std::to_underlying(AbiOpaqueKind::ContinuationBoundaryContext))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "invalid ABI opaque kind"));
    Decoded.emplace(AbiOpaqueType{static_cast<AbiOpaqueKind>(*Kind)});
    break;
  }
  case 16: {
    auto Kind = Payload.u8("ownedSlot.kind");
    auto Count = Payload.count("ownedSlot.fields");
    if (!Kind)
      return std::unexpected(Kind.error());
    if (!Count)
      return std::unexpected(Count.error());
    if (*Kind > std::to_underlying(OwnedSlotStateKind::CleanupObligation))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "invalid owned-slot kind"));
    std::vector<OwnedSlotField> Fields;
    Fields.reserve(*Count);
    for (std::uint32_t FieldIndex = 0; FieldIndex < *Count; ++FieldIndex) {
      auto Type = typeRef(Payload, "ownedSlot.field.type");
      auto Ownership = Payload.u8("ownedSlot.field.ownership");
      if (!Type)
        return std::unexpected(Type.error());
      if (!Ownership)
        return std::unexpected(Ownership.error());
      if (*Ownership > std::to_underlying(ParameterOwnership::Consume) ||
          *Ownership == std::to_underlying(ParameterOwnership::Borrow))
        return std::unexpected(
            error(CanonicalEncodingErrorCode::InvalidReference, "types",
                  "invalid owned-slot field ownership"));
      Fields.push_back({*Type, static_cast<ParameterOwnership>(*Ownership)});
    }
    auto Drop = readFunctionIdentity(Payload, "ownedSlot.dropIdentity");
    if (!Drop)
      return std::unexpected(Drop.error());
    Decoded.emplace(OwnedSlotStateType{static_cast<OwnedSlotStateKind>(*Kind),
                                       std::move(Fields), std::move(*Drop)});
    break;
  }
  case 17: {
    auto TypeParameterCount = Payload.u32Count("function.typeParameterCount");
    auto EffectVariableCount = Payload.count("function.effectVariableKinds");
    if (!TypeParameterCount)
      return std::unexpected(TypeParameterCount.error());
    if (!EffectVariableCount)
      return std::unexpected(EffectVariableCount.error());
    std::vector<EffectVariableKind> EffectKinds;
    EffectKinds.reserve(*EffectVariableCount);
    for (std::uint32_t EffectIndex = 0; EffectIndex < *EffectVariableCount;
         ++EffectIndex) {
      auto Kind = Payload.u8("function.effectVariableKind");
      if (!Kind)
        return std::unexpected(Kind.error());
      if (*Kind > std::to_underlying(EffectVariableKind::Opaque))
        return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                     "types",
                                     "invalid function effect variable kind"));
      EffectKinds.push_back(static_cast<EffectVariableKind>(*Kind));
    }
    auto ConstraintCount = Payload.count("function.constraints");
    if (!ConstraintCount)
      return std::unexpected(ConstraintCount.error());
    std::vector<TypeConstraint> Constraints;
    Constraints.reserve(*ConstraintCount);
    for (std::uint32_t ConstraintIndex = 0; ConstraintIndex < *ConstraintCount;
         ++ConstraintIndex) {
      auto Constraint = readConstraint(Payload, "function.constraint");
      if (!Constraint)
        return std::unexpected(Constraint.error());
      Constraints.push_back(std::move(*Constraint));
    }
    auto ParameterCount = Payload.count("function.parameters");
    if (!ParameterCount)
      return std::unexpected(ParameterCount.error());
    std::vector<FunctionParameter> Parameters;
    Parameters.reserve(*ParameterCount);
    for (std::uint32_t ParameterIndex = 0; ParameterIndex < *ParameterCount;
         ++ParameterIndex) {
      auto Type = typeRef(Payload, "function.parameter.type");
      auto Ownership = Payload.u8("function.parameter.ownership");
      if (!Type)
        return std::unexpected(Type.error());
      if (!Ownership)
        return std::unexpected(Ownership.error());
      if (*Ownership > std::to_underlying(ParameterOwnership::Consume))
        return std::unexpected(
            error(CanonicalEncodingErrorCode::InvalidReference, "types",
                  "invalid function parameter ownership"));
      Parameters.push_back(
          {*Type, static_cast<ParameterOwnership>(*Ownership)});
    }
    auto ResultType = typeRef(Payload, "function.result");
    auto ResultContract = Payload.u8("function.resultContract");
    auto Effects = effectRef(Payload, "function.effects");
    auto Convention = Payload.u8("function.convention");
    if (!ResultType)
      return std::unexpected(ResultType.error());
    if (!ResultContract)
      return std::unexpected(ResultContract.error());
    if (*ResultContract > std::to_underlying(ResultOwnership::Owned))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types",
                                   "invalid function result ownership"));
    if (!Effects)
      return std::unexpected(Effects.error());
    if (!Convention)
      return std::unexpected(Convention.error());
    if (*Convention > std::to_underlying(CallingConvention::ExportedC))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "invalid calling convention"));
    auto SourcePresent = boolean(Payload, "function.sourceIdentity.present");
    if (!SourcePresent)
      return std::unexpected(SourcePresent.error());
    std::optional<FunctionDeclarationIdentity> Source;
    if (*SourcePresent) {
      auto Identity = readFunctionIdentity(Payload, "function.sourceIdentity");
      if (!Identity)
        return std::unexpected(Identity.error());
      Source = std::move(*Identity);
    }
    auto ExportPresent = boolean(Payload, "function.exportedSymbol.present");
    if (!ExportPresent)
      return std::unexpected(ExportPresent.error());
    std::optional<SymbolIdentity> Exported;
    if (*ExportPresent) {
      auto Symbol = readSymbol(Payload, "function.exportedSymbol");
      if (!Symbol)
        return std::unexpected(Symbol.error());
      Exported = std::move(*Symbol);
    }

    std::vector<TypeParameterId> TypeParameters;
    std::vector<EffectVariableDeclaration> EffectVariables;
    const DecodedBinderDeclaration *MatchingBinder = nullptr;
    if (Source) {
      for (const auto &Binder : Layout_.Binders) {
        if (Binder.Kind != BinderOwnerKind::Function ||
            !std::holds_alternative<FunctionDeclarationIdentity>(
                Binder.Identity))
          continue;
        if (std::get<FunctionDeclarationIdentity>(Binder.Identity) == *Source) {
          MatchingBinder = &Binder;
          break;
        }
      }
    }
    if (*TypeParameterCount != 0 || *EffectVariableCount != 0) {
      if (!Source || !MatchingBinder ||
          MatchingBinder->TypeParameters.size() != *TypeParameterCount ||
          MatchingBinder->EffectVariables.size() != *EffectVariableCount)
        return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                     "types",
                                     "function binder declaration mismatch"));
      for (std::size_t EffectIndex = 0; EffectIndex < EffectKinds.size();
           ++EffectIndex)
        if (MatchingBinder->EffectVariables[EffectIndex].Kind !=
            EffectKinds[EffectIndex])
          return std::unexpected(
              error(CanonicalEncodingErrorCode::InvalidBinder, "types",
                    "function effect variable kinds do not match binder"));
      TypeParameters = MatchingBinder->TypeParameters;
      EffectVariables = MatchingBinder->EffectVariables;
    } else if (MatchingBinder && (!MatchingBinder->TypeParameters.empty() ||
                                  !MatchingBinder->EffectVariables.empty())) {
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   "types",
                                   "function omitted declared binders"));
    }
    Decoded.emplace(
        FunctionType{std::move(TypeParameters), std::move(EffectVariables),
                     std::move(Constraints), std::move(Parameters), *ResultType,
                     static_cast<ResultOwnership>(*ResultContract), *Effects,
                     static_cast<CallingConvention>(*Convention),
                     std::move(Source), std::move(Exported)});
    break;
  }
  case 18: {
    auto Alternatives = readTypeRefs(Payload, "sum.alternatives");
    if (!Alternatives)
      return std::unexpected(Alternatives.error());
    if (Alternatives->size() < 2)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "types", "sum has fewer than two members"));
    Decoded.emplace(SumType{std::move(*Alternatives)});
    break;
  }
  default:
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "types", "unknown structural type tag"));
  }
  if (auto Complete = Payload.finish(); !Complete)
    return std::unexpected(Complete.error());
  auto Defined = Transaction_.define(TypeIds_[Index], std::move(*Decoded));
  if (!Defined)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "types[" + std::to_string(Index) + "]",
                                 Defined.error()));
  return In.slice(Begin, In.position());
}

ValueResult<Bytes> DecodeContext::readEffectRecord(Reader &In,
                                                   std::uint32_t Index) {
  const auto Begin = In.position();
  auto PayloadSize = In.u64("effect.payloadSize");
  if (!PayloadSize)
    return std::unexpected(PayloadSize.error());
  auto PayloadValue = In.subreader(*PayloadSize, "effect.payload");
  if (!PayloadValue)
    return std::unexpected(PayloadValue.error());
  auto &Payload = *PayloadValue;
  auto OperationCount = Payload.count("operations");
  if (!OperationCount)
    return std::unexpected(OperationCount.error());
  std::vector<EffectOperationApplication> Operations;
  Operations.reserve(*OperationCount);
  for (std::uint32_t OperationIndex = 0; OperationIndex < *OperationCount;
       ++OperationIndex) {
    auto Application = readApplication(Payload, "operation");
    if (!Application)
      return std::unexpected(Application.error());
    Operations.push_back(std::move(*Application));
  }
  auto TailCount = Payload.count("tails");
  if (!TailCount)
    return std::unexpected(TailCount.error());
  std::vector<EffectTailProjection> Tails;
  Tails.reserve(*TailCount);
  for (std::uint32_t TailIndex = 0; TailIndex < *TailCount; ++TailIndex) {
    auto Variable = effectVariableRef(Payload, "tail.source");
    if (!Variable)
      return std::unexpected(Variable.error());
    auto ExcludedCount = Payload.count("tail.excluded");
    if (!ExcludedCount)
      return std::unexpected(ExcludedCount.error());
    std::vector<EffectOperationApplication> Excluded;
    Excluded.reserve(*ExcludedCount);
    for (std::uint32_t ExcludedIndex = 0; ExcludedIndex < *ExcludedCount;
         ++ExcludedIndex) {
      auto Application = readApplication(Payload, "tail.exclusion");
      if (!Application)
        return std::unexpected(Application.error());
      Excluded.push_back(std::move(*Application));
    }
    Tails.push_back({Variable->Variable, Variable->Kind, std::move(Excluded)});
  }
  auto MayRaise = boolean(Payload, "mayRaise");
  auto MayCancel = boolean(Payload, "mayCancel");
  if (!MayRaise)
    return std::unexpected(MayRaise.error());
  if (!MayCancel)
    return std::unexpected(MayCancel.error());
  if (auto Complete = Payload.finish(); !Complete)
    return std::unexpected(Complete.error());
  auto Defined = Transaction_.defineEffect(
      EffectIds_[Index], EffectRow{std::move(Operations), std::move(Tails),
                                   *MayRaise, *MayCancel});
  if (!Defined)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "effects[" + std::to_string(Index) + "]",
                                 Defined.error()));
  return In.slice(Begin, In.position());
}

} // namespace

std::expected<void, CanonicalEncodingError>
CanonicalDecodeBudget::claim(std::uint64_t Units, std::string Path) {
  if (Units > Remaining_)
    return std::unexpected(CanonicalEncodingError{
        CanonicalEncodingErrorCode::SizeLimit, std::move(Path),
        "canonical decode budget exceeded"});
  Remaining_ -= static_cast<std::size_t>(Units);
  return {};
}

std::expected<DecodedInterfaceTables, CanonicalEncodingError>
decodeInterfaceTables(std::span<const std::byte> TypeSectionPayload,
                      std::span<const std::byte> EffectSectionPayload,
                      CanonicalDecodeBudget *Budget) {
  try {
    Reader TypeVersionReader(TypeSectionPayload, "typeSection");
    Reader EffectVersionReader(EffectSectionPayload, "effectSection");
    auto TypeVersion = TypeVersionReader.u32("encodingVersion");
    auto EffectVersion = EffectVersionReader.u32("encodingVersion");
    if (!TypeVersion)
      return std::unexpected(TypeVersion.error());
    if (!EffectVersion)
      return std::unexpected(EffectVersion.error());
    if (*TypeVersion != CanonicalStructuralEncodingVersion ||
        *EffectVersion != CanonicalStructuralEncodingVersion)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "encodingVersion",
                                   "unsupported canonical encoding version"));

    CanonicalDecodeBudget LocalBudget;
    auto *ActiveBudget = Budget ? Budget : &LocalBudget;
    Reader TypeReader(TypeSectionPayload, "typeSection", ActiveBudget);
    Reader EffectReader(EffectSectionPayload, "effectSection", ActiveBudget);
    static_cast<void>(TypeReader.u32("encodingVersion"));
    static_cast<void>(EffectReader.u32("encodingVersion"));
    TypeTable Types;
    auto Layout = readBinders(TypeReader, Types);
    if (!Layout)
      return std::unexpected(Layout.error());
    auto TypeCount = TypeReader.u32Count("typeCount");
    auto EffectCount = EffectReader.u32Count("effectCount");
    if (!TypeCount)
      return std::unexpected(TypeCount.error());
    if (!EffectCount)
      return std::unexpected(EffectCount.error());

    auto Transaction = Types.beginAppendTransaction();
    std::vector<TypeId> ProvisionalTypes;
    ProvisionalTypes.reserve(*TypeCount);
    for (std::uint32_t Index = 0; Index < *TypeCount; ++Index) {
      const auto Reserved = Transaction.reserveType();
      if (!Reserved.isValid())
        return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                     "types", "unable to reserve type record"));
      ProvisionalTypes.push_back(Reserved);
    }
    std::vector<EffectRowId> ProvisionalEffects;
    ProvisionalEffects.reserve(*EffectCount);
    for (std::uint32_t Index = 0; Index < *EffectCount; ++Index) {
      const auto Reserved = Transaction.reserveEffect();
      if (!Reserved.isValid())
        return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                     "effects",
                                     "unable to reserve effect record"));
      ProvisionalEffects.push_back(Reserved);
    }
    DecodeContext Context(Transaction, *Layout, ProvisionalTypes,
                          ProvisionalEffects);
    std::vector<Bytes> OriginalTypeRecords;
    OriginalTypeRecords.reserve(*TypeCount);
    for (std::uint32_t Index = 0; Index < *TypeCount; ++Index) {
      auto Record = Context.readTypeRecord(TypeReader, Index);
      if (!Record)
        return std::unexpected(Record.error());
      OriginalTypeRecords.push_back(std::move(*Record));
    }
    std::vector<Bytes> OriginalEffectRecords;
    OriginalEffectRecords.reserve(*EffectCount);
    for (std::uint32_t Index = 0; Index < *EffectCount; ++Index) {
      auto Record = Context.readEffectRecord(EffectReader, Index);
      if (!Record)
        return std::unexpected(Record.error());
      OriginalEffectRecords.push_back(std::move(*Record));
    }
    if (auto Finished = TypeReader.finish(); !Finished)
      return std::unexpected(Finished.error());
    if (auto Finished = EffectReader.finish(); !Finished)
      return std::unexpected(Finished.error());
    if (auto Prepared = Transaction.prepareCommit(); !Prepared)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   "tables", Prepared.error()));

    std::vector<TypeId> FinalTypes;
    FinalTypes.reserve(*TypeCount);
    std::unordered_set<TypeId, StrongIdHasher> UniqueTypes;
    for (const auto Id : Context.typeIds()) {
      auto Resolved = Transaction.resolved(Id);
      if (!Resolved)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::InvalidReference, "types",
                  Resolved.error()));
      if (!UniqueTypes.insert(*Resolved).second)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::DuplicateRecord, "types",
                  "duplicate canonical type"));
      FinalTypes.push_back(*Resolved);
    }
    std::vector<EffectRowId> FinalEffects;
    FinalEffects.reserve(*EffectCount);
    std::unordered_set<EffectRowId, StrongIdHasher> UniqueEffects;
    for (const auto Id : Context.effectIds()) {
      auto Resolved = Transaction.resolved(Id);
      if (!Resolved)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::InvalidReference, "effects",
                  Resolved.error()));
      if (!UniqueEffects.insert(*Resolved).second)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::DuplicateRecord, "effects",
                  "duplicate canonical effect"));
      FinalEffects.push_back(*Resolved);
    }
    Transaction.commitPrepared();

    if (Types.typeCount() != *TypeCount || Types.effectCount() != *EffectCount)
      return std::unexpected(
          error(CanonicalEncodingErrorCode::DuplicateRecord, "tables",
                "decoded table did not retain every record"));
    std::vector<std::uint32_t> TypeIndices(Types.typeCount(), UINT32_MAX);
    for (std::uint32_t Index = 0; Index < FinalTypes.size(); ++Index)
      TypeIndices[FinalTypes[Index].value()] = Index;
    std::vector<std::uint32_t> EffectIndices(Types.effectCount(), UINT32_MAX);
    for (std::uint32_t Index = 0; Index < FinalEffects.size(); ++Index)
      EffectIndices[FinalEffects[Index].value()] = Index;

    GraphCodec Codec(Types, Layout->Bindings, &Layout->Owners,
                     CanonicalEncodingErrorCode::UnboundParameter);
    std::vector<bool> AllTypes(Types.typeCount(), true);
    std::vector<bool> AllEffects(Types.effectCount(), true);
    auto Order = makeGraphOrder(Types, Codec, AllTypes, AllEffects);
    if (!Order)
      return std::unexpected(Order.error());
    if (Order->Types != FinalTypes || Order->Effects != FinalEffects)
      return std::unexpected(
          error(CanonicalEncodingErrorCode::NonCanonicalOrder, "tables",
                "records are not canonically ordered"));
    for (std::uint32_t Index = 0; Index < FinalTypes.size(); ++Index) {
      auto Reencoded =
          Codec.typeRecord(FinalTypes[Index], TypeIndices, EffectIndices,
                           "types[" + std::to_string(Index) + "]");
      if (!Reencoded)
        return std::unexpected(Reencoded.error());
      if (*Reencoded != OriginalTypeRecords[Index])
        return std::unexpected(
            error(CanonicalEncodingErrorCode::NonCanonicalOrder,
                  "types[" + std::to_string(Index) + "]",
                  "type record is not in canonical form"));
    }
    for (std::uint32_t Index = 0; Index < FinalEffects.size(); ++Index) {
      auto Reencoded =
          Codec.effectRecord(FinalEffects[Index], TypeIndices, EffectIndices,
                             "effects[" + std::to_string(Index) + "]");
      if (!Reencoded)
        return std::unexpected(Reencoded.error());
      if (*Reencoded != OriginalEffectRecords[Index])
        return std::unexpected(
            error(CanonicalEncodingErrorCode::NonCanonicalOrder,
                  "effects[" + std::to_string(Index) + "]",
                  "effect record is not in canonical form"));
    }

    DecodedInterfaceTables Decoded{std::move(Types),
                                   std::move(Layout->Binders),
                                   std::move(FinalTypes),
                                   std::move(FinalEffects),
                                   std::move(Layout->TypeMappings),
                                   std::move(Layout->EffectMappings)};
    return Decoded;
  } catch (const std::bad_alloc &) {
    return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit, "",
                                 "canonical decoding allocation failed"));
  } catch (const std::exception &Exception) {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "", Exception.what()));
  }
}

namespace {

CanonicalEncodingError error(CanonicalEncodingErrorCode Code, std::string Path,
                             std::string Message) {
  return {Code, std::move(Path), std::move(Message)};
}

bool byteLess(const Bytes &Left, const Bytes &Right) {
  return std::lexicographical_compare(
      Left.begin(), Left.end(), Right.begin(), Right.end(),
      [](std::byte A, std::byte B) {
        return std::to_integer<unsigned>(A) < std::to_integer<unsigned>(B);
      });
}

bool validIdentifier(std::string_view Value) {
  if (Value.empty())
    return false;
  const auto First = static_cast<unsigned char>(Value.front());
  if (!std::isalpha(First) && Value.front() != '_')
    return false;
  return std::all_of(Value.begin() + 1, Value.end(), [](char Character) {
    const auto Byte = static_cast<unsigned char>(Character);
    return std::isalnum(Byte) || Character == '_';
  });
}

Result checkCount(std::size_t Count, std::string Path) {
  if (Count > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit,
                                 std::move(Path), "count exceeds UINT32_MAX"));
  return {};
}

Result validateNominalKey(const NominalTypeKey &Key, std::string Path) {
  if (!validIdentifier(Key.Name))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                 std::move(Path),
                                 "invalid nominal declaration name"));
  return {};
}

Result validateSymbol(const SymbolIdentity &Identity, std::string Path) {
  if (!validIdentifier(Identity.Name))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                 std::move(Path), "invalid symbol name"));
  return {};
}

Result validateFunctionIdentity(const FunctionDeclarationIdentity &Identity,
                                std::string Path) {
  return std::visit(
      Overloaded{[&](const SymbolIdentity &Symbol) {
                   return validateSymbol(Symbol, std::move(Path));
                 },
                 [&](const LocalFunctionIdentity &Local) -> Result {
                   if (Local.LexicalDeclarationPath.empty())
                     return std::unexpected(error(
                         CanonicalEncodingErrorCode::InvalidIdentity,
                         std::move(Path),
                         "local function identity has an empty lexical path"));
                   return checkCount(Local.LexicalDeclarationPath.size(),
                                     std::move(Path));
                 }},
      Identity);
}

Result validateOperationKey(const EffectOperationKey &Key, std::string Path) {
  if (auto Valid = validateNominalKey(Key.Effect, Path + ".effect"); !Valid)
    return Valid;
  if (!validIdentifier(Key.Operation))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidIdentity,
                                 std::move(Path),
                                 "invalid effect operation name"));
  return {};
}

Result validateResultContract(const TypeTable &Types, TypeId ResultType,
                              ResultOwnership Contract, std::string Path,
                              bool AllowOpenParameter = false) {
  auto Expected = resultOwnershipFor(Types, ResultType);
  if (!Expected && AllowOpenParameter && Types.valid(ResultType) &&
      std::holds_alternative<TypeParameterType>(Types.at(ResultType)))
    return {};
  if (!Expected)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path), Expected.error()));
  if (*Expected != Contract)
    return std::unexpected(
        error(CanonicalEncodingErrorCode::InvalidReference, std::move(Path),
              "result ownership does not match the structural result type"));
  return {};
}

void writeModule(Writer &Out, const ModuleIdentity &Module) {
  Out.text(Module.fqn());
}
void writeNominalKey(Writer &Out, const NominalTypeKey &Key) {
  writeModule(Out, Key.Module);
  Out.text(Key.Name);
}
void writeSymbol(Writer &Out, const SymbolIdentity &Identity) {
  writeModule(Out, Identity.Module);
  Out.text(Identity.Name);
}
void writeFunctionIdentity(Writer &Out,
                           const FunctionDeclarationIdentity &Identity) {
  Out.u8(static_cast<std::uint8_t>(Identity.index()));
  std::visit(Overloaded{[&](const SymbolIdentity &Symbol) {
                          writeSymbol(Out, Symbol);
                        },
                        [&](const LocalFunctionIdentity &Local) {
                          writeModule(Out, Local.Module);
                          Out.u64(Local.LexicalDeclarationPath.size());
                          for (const auto Part : Local.LexicalDeclarationPath)
                            Out.u32(Part);
                        }},
             Identity);
}
void writeOperationKey(Writer &Out, const EffectOperationKey &Key) {
  writeNominalKey(Out, Key.Effect);
  Out.text(Key.Operation);
}

struct BinderLayout {
  struct Record {
    const CanonicalBinderDeclaration *Declaration;
    std::size_t OriginalIndex;
    std::size_t OriginalInstanceIndex;
    Bytes IdentityKey;
    std::uint32_t CanonicalInstanceIndex = UINT32_MAX;
  };
  std::vector<Record> Records;
  BindingMaps Bindings;
  std::vector<OwnerRecord> Owners;
  std::vector<CanonicalTypeParameterMapping> TypeRemap;
  std::vector<CanonicalEffectVariableMapping> EffectRemap;
  std::vector<std::uint32_t> InstanceRemap;
};

ValueResult<CanonicalBinderRef> GraphCodec::typeRef(TypeParameterId Id,
                                                    std::string Path) const {
  const auto Found = Bindings_.Types.find(Id);
  if (Found == Bindings_.Types.end())
    return std::unexpected(error(MissingBinderCode_, std::move(Path),
                                 "type parameter has no canonical owner"));
  return Found->second;
}

ValueResult<EffectBinding> GraphCodec::effectRef(EffectVariableId Id,
                                                 std::string Path) const {
  const auto Found = Bindings_.Effects.find(Id);
  if (Found == Bindings_.Effects.end())
    return std::unexpected(error(MissingBinderCode_, std::move(Path),
                                 "effect variable has no canonical owner"));
  return Found->second;
}

Result GraphCodec::validateFunctionBinders(const FunctionType &Type,
                                           std::string Path) const {
  if (auto Valid = checkCount(Type.TypeParameters.size(), Path); !Valid)
    return Valid;
  if (auto Valid = checkCount(Type.EffectVariables.size(), Path); !Valid)
    return Valid;
  if (Type.TypeParameters.empty() && Type.EffectVariables.empty()) {
    if (!Type.SourceIdentity || !Owners_)
      return {};
    const auto Matching = std::find_if(
        Owners_->begin(), Owners_->end(), [&](const OwnerRecord &Record) {
          return Record.Kind == BinderOwnerKind::Function &&
                 std::holds_alternative<FunctionDeclarationIdentity>(
                     Record.Identity) &&
                 std::get<FunctionDeclarationIdentity>(Record.Identity) ==
                     *Type.SourceIdentity;
        });
    if (Matching != Owners_->end() && (Matching->TypeParameterCount != 0 ||
                                       !Matching->EffectVariableKinds.empty()))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "function omitted declared binders"));
    return {};
  }
  if (!Type.SourceIdentity)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "generic function has no source identity"));
  std::optional<std::uint32_t> Owner;
  for (std::size_t Index = 0; Index < Type.TypeParameters.size(); ++Index) {
    auto Ref = typeRef(Type.TypeParameters[Index], Path + ".typeParameters");
    if (!Ref)
      return std::unexpected(Ref.error());
    if (Ref->Ordinal != Index || (Owner && *Owner != Ref->BinderIndex))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "function type parameters do not match "
                                   "their declaration order"));
    Owner = Ref->BinderIndex;
  }
  for (std::size_t Index = 0; Index < Type.EffectVariables.size(); ++Index) {
    const auto &Variable = Type.EffectVariables[Index];
    auto Ref = effectRef(Variable.Variable, Path + ".effectVariables");
    if (!Ref)
      return std::unexpected(Ref.error());
    if (Ref->Ref.Ordinal != Index || Ref->Kind != Variable.Kind ||
        (Owner && *Owner != Ref->Ref.BinderIndex))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "function effect variables do not match "
                                   "their declaration order"));
    Owner = Ref->Ref.BinderIndex;
  }
  if (!Owner || !Owners_ || *Owner >= Owners_->size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "function binder owner is unavailable"));
  const auto &Record = (*Owners_)[*Owner];
  if (Record.Kind != BinderOwnerKind::Function ||
      !std::holds_alternative<FunctionDeclarationIdentity>(Record.Identity) ||
      std::get<FunctionDeclarationIdentity>(Record.Identity) !=
          *Type.SourceIdentity ||
      Record.TypeParameterCount != Type.TypeParameters.size() ||
      Record.EffectVariableKinds.size() != Type.EffectVariables.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "function binder identity does not match"));
  for (std::size_t Index = 0; Index < Type.EffectVariables.size(); ++Index)
    if (Record.EffectVariableKinds[Index] != Type.EffectVariables[Index].Kind)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "function binder kinds do not match"));
  return {};
}

Result
GraphCodec::validateOperationBinders(const EffectOperationSignature &Signature,
                                     std::string Path) const {
  if (auto Valid = checkCount(Signature.TypeParameters.size(), Path); !Valid)
    return Valid;
  if (auto Valid = checkCount(Signature.EffectVariables.size(), Path); !Valid)
    return Valid;
  if (Signature.TypeParameters.empty() && Signature.EffectVariables.empty()) {
    if (!Owners_)
      return {};
    const auto Matching = std::find_if(
        Owners_->begin(), Owners_->end(), [&](const OwnerRecord &Record) {
          return Record.Kind == BinderOwnerKind::Operation &&
                 std::holds_alternative<EffectOperationKey>(Record.Identity) &&
                 std::get<EffectOperationKey>(Record.Identity) == Signature.Key;
        });
    if (Matching != Owners_->end() && (Matching->TypeParameterCount != 0 ||
                                       !Matching->EffectVariableKinds.empty()))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "operation omitted declared binders"));
    return {};
  }
  std::optional<std::uint32_t> Owner;
  for (std::size_t Index = 0; Index < Signature.TypeParameters.size();
       ++Index) {
    auto Ref =
        typeRef(Signature.TypeParameters[Index], Path + ".typeParameters");
    if (!Ref)
      return std::unexpected(Ref.error());
    if (Ref->Ordinal != Index || (Owner && *Owner != Ref->BinderIndex))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "operation type parameters do not match "
                                   "their declaration order"));
    Owner = Ref->BinderIndex;
  }
  for (std::size_t Index = 0; Index < Signature.EffectVariables.size();
       ++Index) {
    const auto &Variable = Signature.EffectVariables[Index];
    auto Ref = effectRef(Variable.Variable, Path + ".effectVariables");
    if (!Ref)
      return std::unexpected(Ref.error());
    if (Ref->Ref.Ordinal != Index || Ref->Kind != Variable.Kind ||
        (Owner && *Owner != Ref->Ref.BinderIndex))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "operation effect variables do not match "
                                   "their declaration order"));
    Owner = Ref->Ref.BinderIndex;
  }
  if (!Owner || !Owners_ || *Owner >= Owners_->size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "operation binder owner is unavailable"));
  const auto &Record = (*Owners_)[*Owner];
  if (Record.Kind != BinderOwnerKind::Operation ||
      !std::holds_alternative<EffectOperationKey>(Record.Identity) ||
      std::get<EffectOperationKey>(Record.Identity) != Signature.Key ||
      Record.TypeParameterCount != Signature.TypeParameters.size() ||
      Record.EffectVariableKinds.size() != Signature.EffectVariables.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "operation binder identity does not match"));
  for (std::size_t Index = 0; Index < Signature.EffectVariables.size(); ++Index)
    if (Record.EffectVariableKinds[Index] !=
        Signature.EffectVariables[Index].Kind)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   std::move(Path),
                                   "operation binder kinds do not match"));
  return {};
}

ValueResult<Bytes>
GraphCodec::expandedConstraint(const TypeConstraint &Constraint,
                               std::string Path) {
  if (auto Valid = validateNominalKey(Constraint.Trait, Path + ".trait");
      !Valid)
    return std::unexpected(Valid.error());
  auto Ref = typeRef(Constraint.Parameter, Path + ".parameter");
  if (!Ref)
    return std::unexpected(Ref.error());
  if (auto Valid = checkCount(Constraint.Arguments.size(), Path); !Valid)
    return std::unexpected(Valid.error());
  Writer Out;
  Out.u32(Ref->BinderIndex);
  Out.u32(Ref->Ordinal);
  writeNominalKey(Out, Constraint.Trait);
  Out.u64(Constraint.Arguments.size());
  for (std::size_t Index = 0; Index < Constraint.Arguments.size(); ++Index) {
    auto Child =
        expandedType(Constraint.Arguments[Index],
                     Path + ".arguments[" + std::to_string(Index) + "]");
    if (!Child)
      return std::unexpected(Child.error());
    Out.raw(**Child);
  }
  return std::move(Out).take();
}

ValueResult<Bytes>
GraphCodec::expandedApplication(const EffectOperationApplication &Application,
                                std::string Path) {
  if (auto Valid = validateOperationKey(Application.Key, Path + ".key"); !Valid)
    return std::unexpected(Valid.error());
  if (auto Valid = checkCount(Application.TypeArguments.size(), Path); !Valid)
    return std::unexpected(Valid.error());
  if (auto Valid = checkCount(Application.EffectArguments.size(), Path); !Valid)
    return std::unexpected(Valid.error());
  Writer Out;
  writeOperationKey(Out, Application.Key);
  Out.u64(Application.TypeArguments.size());
  for (std::size_t Index = 0; Index < Application.TypeArguments.size();
       ++Index) {
    auto Child =
        expandedType(Application.TypeArguments[Index],
                     Path + ".typeArguments[" + std::to_string(Index) + "]");
    if (!Child)
      return std::unexpected(Child.error());
    Out.raw(**Child);
  }
  Out.u64(Application.EffectArguments.size());
  for (std::size_t Index = 0; Index < Application.EffectArguments.size();
       ++Index) {
    auto Child = expandedEffect(Application.EffectArguments[Index],
                                Path + ".effectArguments[" +
                                    std::to_string(Index) + "]");
    if (!Child)
      return std::unexpected(Child.error());
    Out.raw(**Child);
  }
  return std::move(Out).take();
}

ValueResult<Bytes>
GraphCodec::constraintRecord(const TypeConstraint &Constraint,
                             std::span<const std::uint32_t> TypeIndices,
                             std::string Path) const {
  auto Ref = typeRef(Constraint.Parameter, Path + ".parameter");
  if (!Ref)
    return std::unexpected(Ref.error());
  Writer Out;
  Out.u32(Ref->BinderIndex);
  Out.u32(Ref->Ordinal);
  writeNominalKey(Out, Constraint.Trait);
  Out.u64(Constraint.Arguments.size());
  for (const auto Argument : Constraint.Arguments) {
    if (!Types_.valid(Argument) || Argument.value() >= TypeIndices.size())
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   std::move(Path),
                                   "constraint type argument is invalid"));
    Out.u32(TypeIndices[Argument.value()]);
  }
  return std::move(Out).take();
}

ValueResult<Bytes>
GraphCodec::applicationRecord(const EffectOperationApplication &Application,
                              std::span<const std::uint32_t> TypeIndices,
                              std::span<const std::uint32_t> EffectIndices,
                              std::string Path) const {
  Writer Out;
  writeOperationKey(Out, Application.Key);
  Out.u64(Application.TypeArguments.size());
  for (const auto Argument : Application.TypeArguments) {
    if (!Types_.valid(Argument) || Argument.value() >= TypeIndices.size())
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   std::move(Path),
                                   "operation type argument is invalid"));
    Out.u32(TypeIndices[Argument.value()]);
  }
  Out.u64(Application.EffectArguments.size());
  for (const auto Argument : Application.EffectArguments) {
    if (!Types_.valid(Argument) || Argument.value() >= EffectIndices.size())
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   std::move(Path),
                                   "operation effect argument is invalid"));
    Out.u32(EffectIndices[Argument.value()]);
  }
  return std::move(Out).take();
}

ValueResult<Bytes> GraphCodec::expandedTypePayload(TypeId Id,
                                                   std::string Path) {
  const auto &Value = Types_.at(Id);
  Writer Out;
  auto typeChild = [&](TypeId Child, std::string ChildPath) -> Result {
    auto Expanded = expandedType(Child, std::move(ChildPath));
    if (!Expanded)
      return std::unexpected(Expanded.error());
    Out.raw(**Expanded);
    return {};
  };
  auto effectChild = [&](EffectRowId Child, std::string ChildPath) -> Result {
    auto Expanded = expandedEffect(Child, std::move(ChildPath));
    if (!Expanded)
      return std::unexpected(Expanded.error());
    Out.raw(**Expanded);
    return {};
  };
  auto ResultValue = std::visit(
      Overloaded{
          [&](PrimitiveType Type) -> Result {
            if (std::to_underlying(Type) >
                std::to_underlying(PrimitiveType::Byte))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "unknown primitive type discriminant"));
            Out.u8(std::to_underlying(Type));
            return {};
          },
          [&](const TypeParameterType &Type) -> Result {
            auto Ref = typeRef(Type.Parameter, Path + ".parameter");
            if (!Ref)
              return std::unexpected(Ref.error());
            Out.u32(Ref->BinderIndex);
            Out.u32(Ref->Ordinal);
            return {};
          },
          [&](const TupleType &Type) -> Result {
            if (auto Valid = checkCount(Type.Elements.size(), Path); !Valid)
              return Valid;
            Out.u64(Type.Elements.size());
            for (std::size_t Index = 0; Index < Type.Elements.size(); ++Index)
              if (auto Valid = typeChild(Type.Elements[Index],
                                         Path + ".elements[" +
                                             std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const SequenceType &Type) {
            return typeChild(Type.Element, Path + ".element");
          },
          [&](const SetType &Type) {
            return typeChild(Type.Element, Path + ".element");
          },
          [&](const DictionaryType &Type) -> Result {
            if (auto Valid = typeChild(Type.Key, Path + ".key"); !Valid)
              return Valid;
            return typeChild(Type.Value, Path + ".value");
          },
          [&](const RecordType &Type) -> Result {
            if (auto Valid = checkCount(Type.Fields.size(), Path); !Valid)
              return Valid;
            std::vector<const RecordField *> Fields;
            Fields.reserve(Type.Fields.size());
            for (const auto &Field : Type.Fields) {
              if (!validIdentifier(Field.Name))
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::InvalidIdentity, Path,
                          "record contains an invalid field name"));
              Fields.push_back(&Field);
            }
            std::sort(Fields.begin(), Fields.end(),
                      [](const auto *Left, const auto *Right) {
                        return Left->Name < Right->Name;
                      });
            for (std::size_t Index = 1; Index < Fields.size(); ++Index)
              if (Fields[Index - 1]->Name == Fields[Index]->Name)
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::DuplicateRecord, Path,
                          "record contains duplicate fields"));
            Out.u64(Fields.size());
            for (const auto *Field : Fields) {
              Out.text(Field->Name);
              if (auto Valid = typeChild(Field->Type,
                                         Path + ".field[" + Field->Name + "]");
                  !Valid)
                return Valid;
            }
            Out.u8(Type.RowRest.has_value());
            if (Type.RowRest)
              return typeChild(*Type.RowRest, Path + ".rowRest");
            return {};
          },
          [&](const NominalType &Type) -> Result {
            if (auto Valid = validateNominalKey(Type.Key, Path + ".key");
                !Valid)
              return Valid;
            if (auto Valid = checkCount(Type.Arguments.size(), Path); !Valid)
              return Valid;
            writeNominalKey(Out, Type.Key);
            Out.u64(Type.Arguments.size());
            for (std::size_t Index = 0; Index < Type.Arguments.size(); ++Index)
              if (auto Valid = typeChild(Type.Arguments[Index],
                                         Path + ".arguments[" +
                                             std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const ArrayType &Type) -> Result {
            if (auto Valid = typeChild(Type.Element, Path + ".element"); !Valid)
              return Valid;
            Out.u8(Type.Mutable);
            return {};
          },
          [&](const ChannelType &Type) {
            return typeChild(Type.Element, Path + ".element");
          },
          [&](const PromiseType &Type) -> Result {
            if (std::to_underlying(Type.ResultContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid promise result ownership"));
            if (auto Valid = typeChild(Type.Element, Path + ".element"); !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.ResultContract));
            return effectChild(Type.Effects, Path + ".effects");
          },
          [&](const CursorType &Type) -> Result {
            if (std::to_underlying(Type.ElementContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid cursor element ownership"));
            if (auto Valid = typeChild(Type.Element, Path + ".element"); !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](const CursorStepType &Type) -> Result {
            if (std::to_underlying(Type.ElementContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid cursor-step element ownership"));
            if (auto Valid = typeChild(Type.Element, Path + ".element"); !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](const OutcomeTaskGroupType &) -> Result { return {}; },
          [&](const ResourceType &Type) -> Result {
            if (auto Valid = validateNominalKey(Type.Key, Path + ".key");
                !Valid)
              return Valid;
            if (auto Valid = checkCount(Type.Arguments.size(), Path); !Valid)
              return Valid;
            writeNominalKey(Out, Type.Key);
            Out.u64(Type.Arguments.size());
            for (std::size_t Index = 0; Index < Type.Arguments.size(); ++Index)
              if (auto Valid = typeChild(Type.Arguments[Index],
                                         Path + ".arguments[" +
                                             std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const AbiOpaqueType &Type) -> Result {
            if (std::to_underlying(Type.Kind) >
                std::to_underlying(AbiOpaqueKind::ContinuationBoundaryContext))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid ABI opaque kind"));
            Out.u8(std::to_underlying(Type.Kind));
            return {};
          },
          [&](const OwnedSlotStateType &Type) -> Result {
            if (std::to_underlying(Type.Kind) >
                std::to_underlying(OwnedSlotStateKind::CleanupObligation))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid owned-slot state kind"));
            if (auto Valid = validateFunctionIdentity(Type.DropIdentity,
                                                      Path + ".dropIdentity");
                !Valid)
              return Valid;
            if (auto Valid = checkCount(Type.Fields.size(), Path); !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.Kind));
            Out.u64(Type.Fields.size());
            for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index) {
              const auto &Field = Type.Fields[Index];
              if (Field.Ownership == ParameterOwnership::Borrow ||
                  std::to_underlying(Field.Ownership) >
                      std::to_underlying(ParameterOwnership::Consume))
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::InvalidReference, Path,
                          "invalid owned-slot field ownership"));
              if (auto Valid =
                      typeChild(Field.Type, Path + ".fields[" +
                                                std::to_string(Index) + "]");
                  !Valid)
                return Valid;
              Out.u8(std::to_underlying(Field.Ownership));
            }
            writeFunctionIdentity(Out, Type.DropIdentity);
            return {};
          },
          [&](const FunctionType &Type) -> Result {
            if (auto Valid = validateFunctionBinders(Type, Path); !Valid)
              return Valid;
            if (std::to_underlying(Type.ResultContract) >
                    std::to_underlying(ResultOwnership::Owned) ||
                std::to_underlying(Type.Convention) >
                    std::to_underlying(CallingConvention::ExportedC))
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "invalid function discriminant"));
            Out.u32(static_cast<std::uint32_t>(Type.TypeParameters.size()));
            Out.u64(Type.EffectVariables.size());
            for (const auto &Variable : Type.EffectVariables) {
              if (std::to_underlying(Variable.Kind) >
                  std::to_underlying(EffectVariableKind::Opaque))
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::InvalidBinder, Path,
                          "invalid function effect variable kind"));
              Out.u8(std::to_underlying(Variable.Kind));
            }
            std::vector<Bytes> Constraints;
            Constraints.reserve(Type.Constraints.size());
            for (std::size_t Index = 0; Index < Type.Constraints.size();
                 ++Index) {
              auto Encoded = expandedConstraint(
                  Type.Constraints[Index],
                  Path + ".constraints[" + std::to_string(Index) + "]");
              if (!Encoded)
                return std::unexpected(Encoded.error());
              Constraints.push_back(std::move(*Encoded));
            }
            std::sort(Constraints.begin(), Constraints.end(), byteLess);
            for (std::size_t Index = 1; Index < Constraints.size(); ++Index)
              if (Constraints[Index - 1] == Constraints[Index])
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::DuplicateRecord, Path,
                          "function contains duplicate constraints"));
            Out.u64(Constraints.size());
            for (const auto &Constraint : Constraints)
              Out.raw(Constraint);
            if (auto Valid = checkCount(Type.Parameters.size(), Path); !Valid)
              return Valid;
            Out.u64(Type.Parameters.size());
            for (std::size_t Index = 0; Index < Type.Parameters.size();
                 ++Index) {
              const auto &Parameter = Type.Parameters[Index];
              if (std::to_underlying(Parameter.Ownership) >
                  std::to_underlying(ParameterOwnership::Consume))
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::InvalidReference, Path,
                          "invalid function parameter ownership"));
              if (auto Valid = typeChild(Parameter.Type,
                                         Path + ".parameters[" +
                                             std::to_string(Index) + "]");
                  !Valid)
                return Valid;
              Out.u8(std::to_underlying(Parameter.Ownership));
            }
            if (auto Valid = typeChild(Type.Result, Path + ".result"); !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.ResultContract));
            if (auto Valid = effectChild(Type.Effects, Path + ".effects");
                !Valid)
              return Valid;
            Out.u8(std::to_underlying(Type.Convention));
            Out.u8(Type.SourceIdentity.has_value());
            if (Type.SourceIdentity) {
              if (auto Valid = validateFunctionIdentity(
                      *Type.SourceIdentity, Path + ".sourceIdentity");
                  !Valid)
                return Valid;
              writeFunctionIdentity(Out, *Type.SourceIdentity);
            }
            Out.u8(Type.ExportedSymbol.has_value());
            if (Type.ExportedSymbol) {
              if (auto Valid = validateSymbol(*Type.ExportedSymbol,
                                              Path + ".exportedSymbol");
                  !Valid)
                return Valid;
              writeSymbol(Out, *Type.ExportedSymbol);
            }
            return {};
          },
          [&](const SumType &Type) -> Result {
            if (Type.Alternatives.size() < 2)
              return std::unexpected(
                  error(CanonicalEncodingErrorCode::InvalidReference, Path,
                        "stored sum must contain at least two alternatives"));
            std::vector<Bytes> Alternatives;
            Alternatives.reserve(Type.Alternatives.size());
            for (std::size_t Index = 0; Index < Type.Alternatives.size();
                 ++Index) {
              auto Expanded = expandedType(Type.Alternatives[Index],
                                           Path + ".alternatives[" +
                                               std::to_string(Index) + "]");
              if (!Expanded)
                return std::unexpected(Expanded.error());
              Alternatives.push_back(**Expanded);
            }
            std::sort(Alternatives.begin(), Alternatives.end(), byteLess);
            for (std::size_t Index = 1; Index < Alternatives.size(); ++Index)
              if (Alternatives[Index - 1] == Alternatives[Index])
                return std::unexpected(
                    error(CanonicalEncodingErrorCode::DuplicateRecord, Path,
                          "sum contains duplicate alternatives"));
            Out.u64(Alternatives.size());
            for (const auto &Alternative : Alternatives)
              Out.raw(Alternative);
            return {};
          }},
      Value);
  if (!ResultValue)
    return std::unexpected(ResultValue.error());
  return std::move(Out).take();
}

ValueResult<const Bytes *> GraphCodec::expandedType(TypeId Id,
                                                    std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path),
                                 "type reference is foreign or out of range"));
  if (TypeState_[Id.value()] == 1)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path),
                                 "structural graph contains a cycle"));
  if (TypeState_[Id.value()] == 2)
    return &*TypeKeys_[Id.value()];
  TypeState_[Id.value()] = 1;
  auto Payload = expandedTypePayload(Id, std::move(Path));
  if (!Payload) {
    TypeState_[Id.value()] = 0;
    return std::unexpected(Payload.error());
  }
  Writer Out;
  Out.u8(static_cast<std::uint8_t>(Types_.at(Id).index()));
  Out.u64(Payload->size());
  Out.raw(*Payload);
  TypeKeys_[Id.value()] = std::move(Out).take();
  TypeState_[Id.value()] = 2;
  return &*TypeKeys_[Id.value()];
}

ValueResult<Bytes> GraphCodec::expandedEffectPayload(EffectRowId Id,
                                                     std::string Path) {
  const auto &Row = Types_.effect(Id);
  std::vector<Bytes> Operations;
  Operations.reserve(Row.Operations.size());
  for (std::size_t Index = 0; Index < Row.Operations.size(); ++Index) {
    auto Encoded = expandedApplication(Row.Operations[Index],
                                       Path + ".operations[" +
                                           std::to_string(Index) + "]");
    if (!Encoded)
      return std::unexpected(Encoded.error());
    Operations.push_back(std::move(*Encoded));
  }
  std::sort(Operations.begin(), Operations.end(), byteLess);
  for (std::size_t Index = 1; Index < Operations.size(); ++Index)
    if (Operations[Index - 1] == Operations[Index])
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   Path,
                                   "effect row contains duplicate operations"));

  std::vector<Bytes> Tails;
  Tails.reserve(Row.Tails.size());
  for (std::size_t Index = 0; Index < Row.Tails.size(); ++Index) {
    const auto &Tail = Row.Tails[Index];
    auto Binding = effectRef(Tail.Variable,
                             Path + ".tails[" + std::to_string(Index) + "]");
    if (!Binding)
      return std::unexpected(Binding.error());
    if (Binding->Kind != Tail.Kind)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   Path,
                                   "effect tail kind disagrees with its "
                                   "binder declaration"));
    std::vector<Bytes> Excluded;
    Excluded.reserve(Tail.ExcludedOperations.size());
    for (std::size_t ExcludedIndex = 0;
         ExcludedIndex < Tail.ExcludedOperations.size(); ++ExcludedIndex) {
      auto Encoded = expandedApplication(
          Tail.ExcludedOperations[ExcludedIndex],
          Path + ".tails[" + std::to_string(Index) + "].excluded[" +
              std::to_string(ExcludedIndex) + "]");
      if (!Encoded)
        return std::unexpected(Encoded.error());
      Excluded.push_back(std::move(*Encoded));
    }
    std::sort(Excluded.begin(), Excluded.end(), byteLess);
    for (std::size_t ExcludedIndex = 1; ExcludedIndex < Excluded.size();
         ++ExcludedIndex)
      if (Excluded[ExcludedIndex - 1] == Excluded[ExcludedIndex])
        return std::unexpected(
            error(CanonicalEncodingErrorCode::DuplicateRecord, Path,
                  "effect tail contains duplicate exclusions"));
    Writer TailWriter;
    TailWriter.u32(Binding->Ref.BinderIndex);
    TailWriter.u32(Binding->Ref.Ordinal);
    TailWriter.u8(std::to_underlying(Tail.Kind));
    TailWriter.u64(Excluded.size());
    for (const auto &Application : Excluded)
      TailWriter.raw(Application);
    Tails.push_back(std::move(TailWriter).take());
  }
  std::sort(Tails.begin(), Tails.end(), byteLess);
  for (std::size_t Index = 1; Index < Tails.size(); ++Index)
    if (Tails[Index - 1] == Tails[Index])
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   Path,
                                   "effect row contains duplicate tails"));

  Writer Out;
  Out.u64(Operations.size());
  for (const auto &Operation : Operations)
    Out.raw(Operation);
  Out.u64(Tails.size());
  for (const auto &Tail : Tails)
    Out.raw(Tail);
  Out.u8(Row.MayRaise);
  Out.u8(Row.MayCancel);
  return std::move(Out).take();
}

ValueResult<const Bytes *> GraphCodec::expandedEffect(EffectRowId Id,
                                                      std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(
        error(CanonicalEncodingErrorCode::InvalidReference, std::move(Path),
              "effect reference is foreign or out of range"));
  if (EffectState_[Id.value()] == 1)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path),
                                 "structural graph contains a cycle"));
  if (EffectState_[Id.value()] == 2)
    return &*EffectKeys_[Id.value()];
  EffectState_[Id.value()] = 1;
  auto Payload = expandedEffectPayload(Id, std::move(Path));
  if (!Payload) {
    EffectState_[Id.value()] = 0;
    return std::unexpected(Payload.error());
  }
  Writer Out;
  Out.u64(Payload->size());
  Out.raw(*Payload);
  EffectKeys_[Id.value()] = std::move(Out).take();
  EffectState_[Id.value()] = 2;
  return &*EffectKeys_[Id.value()];
}

ValueResult<Bytes>
GraphCodec::typeRecord(TypeId Id, std::span<const std::uint32_t> TypeIndices,
                       std::span<const std::uint32_t> EffectIndices,
                       std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path), "invalid type record ID"));
  const auto &Value = Types_.at(Id);
  Writer Payload;
  auto typeIndex = [&](TypeId Child,
                       std::string ChildPath) -> ValueResult<std::uint32_t> {
    if (!Types_.valid(Child) || Child.value() >= TypeIndices.size())
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   std::move(ChildPath),
                                   "invalid type reference"));
    return TypeIndices[Child.value()];
  };
  auto effectIndex = [&](EffectRowId Child,
                         std::string ChildPath) -> ValueResult<std::uint32_t> {
    if (!Types_.valid(Child) || Child.value() >= EffectIndices.size())
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   std::move(ChildPath),
                                   "invalid effect reference"));
    return EffectIndices[Child.value()];
  };
  auto WriteResult = std::visit(
      Overloaded{
          [&](PrimitiveType Type) -> Result {
            Payload.u8(std::to_underlying(Type));
            return {};
          },
          [&](const TypeParameterType &Type) -> Result {
            auto Ref = typeRef(Type.Parameter, Path + ".parameter");
            if (!Ref)
              return std::unexpected(Ref.error());
            Payload.u32(Ref->BinderIndex);
            Payload.u32(Ref->Ordinal);
            return {};
          },
          [&](const TupleType &Type) -> Result {
            Payload.u64(Type.Elements.size());
            for (std::size_t Index = 0; Index < Type.Elements.size(); ++Index) {
              auto Child =
                  typeIndex(Type.Elements[Index],
                            Path + ".elements[" + std::to_string(Index) + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
            }
            return {};
          },
          [&](const SequenceType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            return {};
          },
          [&](const SetType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            return {};
          },
          [&](const DictionaryType &Type) -> Result {
            auto Key = typeIndex(Type.Key, Path + ".key");
            if (!Key)
              return std::unexpected(Key.error());
            auto Child = typeIndex(Type.Value, Path + ".value");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Key);
            Payload.u32(*Child);
            return {};
          },
          [&](const RecordType &Type) -> Result {
            std::vector<const RecordField *> Fields;
            Fields.reserve(Type.Fields.size());
            for (const auto &Field : Type.Fields)
              Fields.push_back(&Field);
            std::sort(Fields.begin(), Fields.end(),
                      [](const auto *Left, const auto *Right) {
                        return Left->Name < Right->Name;
                      });
            Payload.u64(Fields.size());
            for (const auto *Field : Fields) {
              Payload.text(Field->Name);
              auto Child =
                  typeIndex(Field->Type, Path + ".field[" + Field->Name + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
            }
            Payload.u8(Type.RowRest.has_value());
            if (Type.RowRest) {
              auto Rest = typeIndex(*Type.RowRest, Path + ".rowRest");
              if (!Rest)
                return std::unexpected(Rest.error());
              Payload.u32(*Rest);
            }
            return {};
          },
          [&](const NominalType &Type) -> Result {
            writeNominalKey(Payload, Type.Key);
            Payload.u64(Type.Arguments.size());
            for (std::size_t Index = 0; Index < Type.Arguments.size();
                 ++Index) {
              auto Child =
                  typeIndex(Type.Arguments[Index],
                            Path + ".arguments[" + std::to_string(Index) + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
            }
            return {};
          },
          [&](const ArrayType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            Payload.u8(Type.Mutable);
            return {};
          },
          [&](const ChannelType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            return {};
          },
          [&](const PromiseType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            auto Effects = effectIndex(Type.Effects, Path + ".effects");
            if (!Effects)
              return std::unexpected(Effects.error());
            Payload.u32(*Child);
            Payload.u8(std::to_underlying(Type.ResultContract));
            Payload.u32(*Effects);
            return {};
          },
          [&](const CursorType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            Payload.u8(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](const CursorStepType &Type) -> Result {
            auto Child = typeIndex(Type.Element, Path + ".element");
            if (!Child)
              return std::unexpected(Child.error());
            Payload.u32(*Child);
            Payload.u8(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](const OutcomeTaskGroupType &) -> Result { return {}; },
          [&](const ResourceType &Type) -> Result {
            writeNominalKey(Payload, Type.Key);
            Payload.u64(Type.Arguments.size());
            for (std::size_t Index = 0; Index < Type.Arguments.size();
                 ++Index) {
              auto Child =
                  typeIndex(Type.Arguments[Index],
                            Path + ".arguments[" + std::to_string(Index) + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
            }
            return {};
          },
          [&](const AbiOpaqueType &Type) -> Result {
            Payload.u8(std::to_underlying(Type.Kind));
            return {};
          },
          [&](const OwnedSlotStateType &Type) -> Result {
            Payload.u8(std::to_underlying(Type.Kind));
            Payload.u64(Type.Fields.size());
            for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index) {
              auto Child =
                  typeIndex(Type.Fields[Index].Type,
                            Path + ".fields[" + std::to_string(Index) + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
              Payload.u8(std::to_underlying(Type.Fields[Index].Ownership));
            }
            writeFunctionIdentity(Payload, Type.DropIdentity);
            return {};
          },
          [&](const FunctionType &Type) -> Result {
            Payload.u32(static_cast<std::uint32_t>(Type.TypeParameters.size()));
            Payload.u64(Type.EffectVariables.size());
            for (const auto &Variable : Type.EffectVariables)
              Payload.u8(std::to_underlying(Variable.Kind));
            std::vector<std::pair<Bytes, const TypeConstraint *>> Constraints;
            Constraints.reserve(Type.Constraints.size());
            for (std::size_t Index = 0; Index < Type.Constraints.size();
                 ++Index) {
              auto Key = expandedConstraint(Type.Constraints[Index],
                                            Path + ".constraints[" +
                                                std::to_string(Index) + "]");
              if (!Key)
                return std::unexpected(Key.error());
              Constraints.emplace_back(std::move(*Key),
                                       &Type.Constraints[Index]);
            }
            std::sort(Constraints.begin(), Constraints.end(),
                      [](const auto &Left, const auto &Right) {
                        return byteLess(Left.first, Right.first);
                      });
            Payload.u64(Constraints.size());
            for (const auto &[_, Constraint] : Constraints) {
              auto Encoded = constraintRecord(*Constraint, TypeIndices, Path);
              if (!Encoded)
                return std::unexpected(Encoded.error());
              Payload.raw(*Encoded);
            }
            Payload.u64(Type.Parameters.size());
            for (std::size_t Index = 0; Index < Type.Parameters.size();
                 ++Index) {
              auto Child = typeIndex(Type.Parameters[Index].Type,
                                     Path + ".parameters[" +
                                         std::to_string(Index) + "]");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
              Payload.u8(std::to_underlying(Type.Parameters[Index].Ownership));
            }
            auto ResultType = typeIndex(Type.Result, Path + ".result");
            if (!ResultType)
              return std::unexpected(ResultType.error());
            auto Effects = effectIndex(Type.Effects, Path + ".effects");
            if (!Effects)
              return std::unexpected(Effects.error());
            Payload.u32(*ResultType);
            Payload.u8(std::to_underlying(Type.ResultContract));
            Payload.u32(*Effects);
            Payload.u8(std::to_underlying(Type.Convention));
            Payload.u8(Type.SourceIdentity.has_value());
            if (Type.SourceIdentity)
              writeFunctionIdentity(Payload, *Type.SourceIdentity);
            Payload.u8(Type.ExportedSymbol.has_value());
            if (Type.ExportedSymbol)
              writeSymbol(Payload, *Type.ExportedSymbol);
            return {};
          },
          [&](const SumType &Type) -> Result {
            std::vector<std::pair<Bytes, TypeId>> Alternatives;
            Alternatives.reserve(Type.Alternatives.size());
            for (std::size_t Index = 0; Index < Type.Alternatives.size();
                 ++Index) {
              auto Key = expandedType(Type.Alternatives[Index],
                                      Path + ".alternatives[" +
                                          std::to_string(Index) + "]");
              if (!Key)
                return std::unexpected(Key.error());
              Alternatives.emplace_back(**Key, Type.Alternatives[Index]);
            }
            std::sort(Alternatives.begin(), Alternatives.end(),
                      [](const auto &Left, const auto &Right) {
                        return byteLess(Left.first, Right.first);
                      });
            Payload.u64(Alternatives.size());
            for (const auto &[_, Alternative] : Alternatives) {
              auto Child = typeIndex(Alternative, Path + ".alternatives");
              if (!Child)
                return std::unexpected(Child.error());
              Payload.u32(*Child);
            }
            return {};
          }},
      Value);
  if (!WriteResult)
    return std::unexpected(WriteResult.error());
  auto PayloadBytes = std::move(Payload).take();
  Writer Record;
  Record.u8(static_cast<std::uint8_t>(Value.index()));
  Record.u64(PayloadBytes.size());
  Record.raw(PayloadBytes);
  return std::move(Record).take();
}

ValueResult<Bytes> GraphCodec::effectRecord(
    EffectRowId Id, std::span<const std::uint32_t> TypeIndices,
    std::span<const std::uint32_t> EffectIndices, std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path), "invalid effect record ID"));
  const auto &Row = Types_.effect(Id);
  std::vector<std::pair<Bytes, const EffectOperationApplication *>> Operations;
  Operations.reserve(Row.Operations.size());
  for (std::size_t Index = 0; Index < Row.Operations.size(); ++Index) {
    auto Key = expandedApplication(Row.Operations[Index],
                                   Path + ".operations[" +
                                       std::to_string(Index) + "]");
    if (!Key)
      return std::unexpected(Key.error());
    Operations.emplace_back(std::move(*Key), &Row.Operations[Index]);
  }
  std::sort(Operations.begin(), Operations.end(),
            [](const auto &Left, const auto &Right) {
              return byteLess(Left.first, Right.first);
            });

  struct TailRecord {
    Bytes Key;
    const EffectTailProjection *Tail;
    EffectBinding Binding;
    std::vector<std::pair<Bytes, const EffectOperationApplication *>> Excluded;
  };
  std::vector<TailRecord> Tails;
  Tails.reserve(Row.Tails.size());
  for (std::size_t Index = 0; Index < Row.Tails.size(); ++Index) {
    const auto &Tail = Row.Tails[Index];
    auto Binding = effectRef(Tail.Variable,
                             Path + ".tails[" + std::to_string(Index) + "]");
    if (!Binding)
      return std::unexpected(Binding.error());
    std::vector<std::pair<Bytes, const EffectOperationApplication *>> Excluded;
    for (std::size_t ExcludedIndex = 0;
         ExcludedIndex < Tail.ExcludedOperations.size(); ++ExcludedIndex) {
      auto Key = expandedApplication(Tail.ExcludedOperations[ExcludedIndex],
                                     Path + ".excluded");
      if (!Key)
        return std::unexpected(Key.error());
      Excluded.emplace_back(std::move(*Key),
                            &Tail.ExcludedOperations[ExcludedIndex]);
    }
    std::sort(Excluded.begin(), Excluded.end(),
              [](const auto &Left, const auto &Right) {
                return byteLess(Left.first, Right.first);
              });
    Writer Key;
    Key.u32(Binding->Ref.BinderIndex);
    Key.u32(Binding->Ref.Ordinal);
    Key.u8(std::to_underlying(Tail.Kind));
    Key.u64(Excluded.size());
    for (const auto &[Expanded, _] : Excluded)
      Key.raw(Expanded);
    Tails.push_back(
        {std::move(Key).take(), &Tail, *Binding, std::move(Excluded)});
  }
  std::sort(Tails.begin(), Tails.end(),
            [](const auto &Left, const auto &Right) {
              return byteLess(Left.Key, Right.Key);
            });

  Writer Payload;
  Payload.u64(Operations.size());
  for (const auto &[_, Operation] : Operations) {
    auto Encoded =
        applicationRecord(*Operation, TypeIndices, EffectIndices, Path);
    if (!Encoded)
      return std::unexpected(Encoded.error());
    Payload.raw(*Encoded);
  }
  Payload.u64(Tails.size());
  for (const auto &Tail : Tails) {
    Payload.u32(Tail.Binding.Ref.BinderIndex);
    Payload.u32(Tail.Binding.Ref.Ordinal);
    Payload.u64(Tail.Excluded.size());
    for (const auto &[_, Application] : Tail.Excluded) {
      auto Encoded =
          applicationRecord(*Application, TypeIndices, EffectIndices, Path);
      if (!Encoded)
        return std::unexpected(Encoded.error());
      Payload.raw(*Encoded);
    }
  }
  Payload.u8(Row.MayRaise);
  Payload.u8(Row.MayCancel);
  auto PayloadBytes = std::move(Payload).take();
  Writer Record;
  Record.u64(PayloadBytes.size());
  Record.raw(PayloadBytes);
  return std::move(Record).take();
}

Result GraphCodec::collectEffect(EffectRowId Id, std::vector<bool> &Types,
                                 std::vector<bool> &Effects, std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path), "invalid effect reference"));
  if (Effects[Id.value()])
    return {};
  Effects[Id.value()] = true;
  auto collectApplication = [&](const EffectOperationApplication &Application,
                                std::string ApplicationPath) -> Result {
    if (auto Valid =
            validateOperationKey(Application.Key, ApplicationPath + ".key");
        !Valid)
      return Valid;
    for (std::size_t Index = 0; Index < Application.TypeArguments.size();
         ++Index)
      if (auto Valid =
              collectType(Application.TypeArguments[Index], Types, Effects,
                          ApplicationPath + ".typeArguments[" +
                              std::to_string(Index) + "]");
          !Valid)
        return Valid;
    for (std::size_t Index = 0; Index < Application.EffectArguments.size();
         ++Index)
      if (auto Valid =
              collectEffect(Application.EffectArguments[Index], Types, Effects,
                            ApplicationPath + ".effectArguments[" +
                                std::to_string(Index) + "]");
          !Valid)
        return Valid;
    return {};
  };
  const auto &Row = Types_.effect(Id);
  for (std::size_t Index = 0; Index < Row.Operations.size(); ++Index)
    if (auto Valid = collectApplication(Row.Operations[Index],
                                        Path + ".operations[" +
                                            std::to_string(Index) + "]");
        !Valid)
      return Valid;
  for (std::size_t Index = 0; Index < Row.Tails.size(); ++Index) {
    const auto &Tail = Row.Tails[Index];
    auto Binding = effectRef(Tail.Variable,
                             Path + ".tails[" + std::to_string(Index) + "]");
    if (!Binding)
      return std::unexpected(Binding.error());
    if (Binding->Kind != Tail.Kind)
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   Path, "effect tail kind mismatch"));
    for (std::size_t Excluded = 0; Excluded < Tail.ExcludedOperations.size();
         ++Excluded)
      if (auto Valid = collectApplication(
              Tail.ExcludedOperations[Excluded],
              Path + ".tails[" + std::to_string(Index) + "].excluded[" +
                  std::to_string(Excluded) + "]");
          !Valid)
        return Valid;
  }
  return {};
}

Result GraphCodec::collectType(TypeId Id, std::vector<bool> &Types,
                               std::vector<bool> &Effects, std::string Path) {
  if (!Types_.valid(Id))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path), "invalid type reference"));
  if (Types[Id.value()])
    return {};
  Types[Id.value()] = true;
  auto child = [&](TypeId Child, std::string ChildPath) {
    return collectType(Child, Types, Effects, std::move(ChildPath));
  };
  auto effect = [&](EffectRowId Child, std::string ChildPath) {
    return collectEffect(Child, Types, Effects, std::move(ChildPath));
  };
  const auto &Value = Types_.at(Id);
  return std::visit(
      Overloaded{
          [](PrimitiveType) -> Result { return {}; },
          [&](const TypeParameterType &Type) -> Result {
            auto Ref = typeRef(Type.Parameter, Path + ".parameter");
            if (!Ref)
              return std::unexpected(Ref.error());
            return {};
          },
          [&](const TupleType &Type) -> Result {
            for (std::size_t Index = 0; Index < Type.Elements.size(); ++Index)
              if (auto Valid =
                      child(Type.Elements[Index],
                            Path + ".elements[" + std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const SequenceType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [&](const SetType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [&](const DictionaryType &Type) -> Result {
            if (auto Valid = child(Type.Key, Path + ".key"); !Valid)
              return Valid;
            return child(Type.Value, Path + ".value");
          },
          [&](const RecordType &Type) -> Result {
            for (const auto &Field : Type.Fields)
              if (auto Valid =
                      child(Field.Type, Path + ".field[" + Field.Name + "]");
                  !Valid)
                return Valid;
            if (Type.RowRest)
              return child(*Type.RowRest, Path + ".rowRest");
            return {};
          },
          [&](const NominalType &Type) -> Result {
            for (std::size_t Index = 0; Index < Type.Arguments.size(); ++Index)
              if (auto Valid =
                      child(Type.Arguments[Index],
                            Path + ".arguments[" + std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const ArrayType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [&](const ChannelType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [&](const PromiseType &Type) -> Result {
            if (auto Valid = child(Type.Element, Path + ".element"); !Valid)
              return Valid;
            return effect(Type.Effects, Path + ".effects");
          },
          [&](const CursorType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [&](const CursorStepType &Type) {
            return child(Type.Element, Path + ".element");
          },
          [](const OutcomeTaskGroupType &) -> Result { return {}; },
          [&](const ResourceType &Type) -> Result {
            for (std::size_t Index = 0; Index < Type.Arguments.size(); ++Index)
              if (auto Valid =
                      child(Type.Arguments[Index],
                            Path + ".arguments[" + std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [](const AbiOpaqueType &) -> Result { return {}; },
          [&](const OwnedSlotStateType &Type) -> Result {
            for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index)
              if (auto Valid =
                      child(Type.Fields[Index].Type,
                            Path + ".fields[" + std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          },
          [&](const FunctionType &Type) -> Result {
            if (auto Valid = validateFunctionBinders(Type, Path); !Valid)
              return Valid;
            for (const auto &Constraint : Type.Constraints)
              for (const auto Argument : Constraint.Arguments)
                if (auto Valid = child(Argument, Path + ".constraint"); !Valid)
                  return Valid;
            for (std::size_t Index = 0; Index < Type.Parameters.size(); ++Index)
              if (auto Valid = child(Type.Parameters[Index].Type,
                                     Path + ".parameters[" +
                                         std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            if (auto Valid = child(Type.Result, Path + ".result"); !Valid)
              return Valid;
            return effect(Type.Effects, Path + ".effects");
          },
          [&](const SumType &Type) -> Result {
            for (std::size_t Index = 0; Index < Type.Alternatives.size();
                 ++Index)
              if (auto Valid = child(Type.Alternatives[Index],
                                     Path + ".alternatives[" +
                                         std::to_string(Index) + "]");
                  !Valid)
                return Valid;
            return {};
          }},
      Value);
}

Result GraphCodec::collect(const ClosedEffectOperation &Operation,
                           std::vector<bool> &Types, std::vector<bool> &Effects,
                           std::string Path) {
  if (auto Valid = validateOperationKey(Operation.Key, Path + ".key"); !Valid)
    return Valid;
  if (std::to_underlying(Operation.ResultContract) >
      std::to_underlying(ResultOwnership::Owned))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path),
                                 "invalid operation result ownership"));
  if (auto Valid = validateResultContract(Types_, Operation.Result,
                                          Operation.ResultContract,
                                          Path + ".resultContract");
      !Valid)
    return Valid;
  for (std::size_t Index = 0; Index < Operation.TypeArguments.size(); ++Index)
    if (auto Valid =
            collectType(Operation.TypeArguments[Index], Types, Effects,
                        Path + ".typeArguments[" + std::to_string(Index) + "]");
        !Valid)
      return Valid;
  for (std::size_t Index = 0; Index < Operation.EffectArguments.size(); ++Index)
    if (auto Valid = collectEffect(
            Operation.EffectArguments[Index], Types, Effects,
            Path + ".effectArguments[" + std::to_string(Index) + "]");
        !Valid)
      return Valid;
  for (std::size_t Index = 0; Index < Operation.Parameters.size(); ++Index) {
    if (std::to_underlying(Operation.Parameters[Index].Ownership) >
        std::to_underlying(ParameterOwnership::Consume))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   Path,
                                   "invalid operation parameter ownership"));
    if (auto Valid =
            collectType(Operation.Parameters[Index].Type, Types, Effects,
                        Path + ".parameters[" + std::to_string(Index) + "]");
        !Valid)
      return Valid;
  }
  if (auto Valid =
          collectType(Operation.Result, Types, Effects, Path + ".result");
      !Valid)
    return Valid;
  return collectEffect(Operation.Effects, Types, Effects, Path + ".effects");
}

ValueResult<Bytes>
GraphCodec::closedOperationRecord(const ClosedEffectOperation &Operation,
                                  std::span<const std::uint32_t> TypeIndices,
                                  std::span<const std::uint32_t> EffectIndices,
                                  std::string) {
  Writer Payload;
  writeOperationKey(Payload, Operation.Key);
  Payload.u64(Operation.TypeArguments.size());
  for (const auto Id : Operation.TypeArguments)
    Payload.u32(TypeIndices[Id.value()]);
  Payload.u64(Operation.EffectArguments.size());
  for (const auto Id : Operation.EffectArguments)
    Payload.u32(EffectIndices[Id.value()]);
  Payload.u64(Operation.Parameters.size());
  for (const auto &Parameter : Operation.Parameters) {
    Payload.u32(TypeIndices[Parameter.Type.value()]);
    Payload.u8(std::to_underlying(Parameter.Ownership));
  }
  Payload.u32(TypeIndices[Operation.Result.value()]);
  Payload.u8(std::to_underlying(Operation.ResultContract));
  Payload.u32(EffectIndices[Operation.Effects.value()]);
  auto BytesValue = std::move(Payload).take();
  Writer Record;
  Record.u64(BytesValue.size());
  Record.raw(BytesValue);
  return std::move(Record).take();
}

ValueResult<Bytes> GraphCodec::operationSignatureRecord(
    const EffectOperationSignature &Signature,
    std::span<const std::uint32_t> TypeIndices,
    std::span<const std::uint32_t> EffectIndices, std::string Path) {
  if (auto Valid = validateOperationKey(Signature.Key, Path + ".key"); !Valid)
    return std::unexpected(Valid.error());
  if (auto Valid = validateOperationBinders(Signature, Path); !Valid)
    return std::unexpected(Valid.error());
  if (std::to_underlying(Signature.ResultContract) >
      std::to_underlying(ResultOwnership::Owned))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 std::move(Path),
                                 "invalid operation result ownership"));
  if (auto Valid = validateResultContract(Types_, Signature.Result,
                                          Signature.ResultContract,
                                          Path + ".resultContract", true);
      !Valid)
    return std::unexpected(Valid.error());
  Writer Payload;
  writeOperationKey(Payload, Signature.Key);
  Payload.u32(static_cast<std::uint32_t>(Signature.TypeParameters.size()));
  Payload.u64(Signature.EffectVariables.size());
  for (const auto &Variable : Signature.EffectVariables)
    Payload.u8(std::to_underlying(Variable.Kind));
  std::vector<std::pair<Bytes, const TypeConstraint *>> Constraints;
  for (std::size_t Index = 0; Index < Signature.Constraints.size(); ++Index) {
    auto Key = expandedConstraint(Signature.Constraints[Index],
                                  Path + ".constraints[" +
                                      std::to_string(Index) + "]");
    if (!Key)
      return std::unexpected(Key.error());
    Constraints.emplace_back(std::move(*Key), &Signature.Constraints[Index]);
  }
  std::sort(Constraints.begin(), Constraints.end(),
            [](const auto &Left, const auto &Right) {
              return byteLess(Left.first, Right.first);
            });
  for (std::size_t Index = 1; Index < Constraints.size(); ++Index)
    if (Constraints[Index - 1].first == Constraints[Index].first)
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   Path,
                                   "operation has duplicate constraints"));
  Payload.u64(Constraints.size());
  for (const auto &[_, Constraint] : Constraints) {
    auto Encoded = constraintRecord(*Constraint, TypeIndices, Path);
    if (!Encoded)
      return std::unexpected(Encoded.error());
    Payload.raw(*Encoded);
  }
  Payload.u64(Signature.Parameters.size());
  for (const auto &Parameter : Signature.Parameters) {
    if (!Types_.valid(Parameter.Type) ||
        Parameter.Type.value() >= TypeIndices.size() ||
        std::to_underlying(Parameter.Ownership) >
            std::to_underlying(ParameterOwnership::Consume))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                   Path, "invalid operation parameter"));
    Payload.u32(TypeIndices[Parameter.Type.value()]);
    Payload.u8(std::to_underlying(Parameter.Ownership));
  }
  if (!Types_.valid(Signature.Result) ||
      Signature.Result.value() >= TypeIndices.size() ||
      !Types_.valid(Signature.Effects) ||
      Signature.Effects.value() >= EffectIndices.size())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 Path, "invalid operation result or effects"));
  Payload.u32(TypeIndices[Signature.Result.value()]);
  Payload.u8(std::to_underlying(Signature.ResultContract));
  Payload.u32(EffectIndices[Signature.Effects.value()]);
  auto BytesValue = std::move(Payload).take();
  Writer Record;
  Record.u64(BytesValue.size());
  Record.raw(BytesValue);
  return std::move(Record).take();
}

Result validateBinderShape(const TypeTable &Types,
                           const CanonicalBinderDeclaration &Declaration,
                           std::string Path) {
  if (std::to_underlying(Declaration.Kind) >
      std::to_underlying(BinderOwnerKind::Resource))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path), "unknown binder owner kind"));
  const bool IdentityMatches =
      ((Declaration.Kind == BinderOwnerKind::Nominal ||
        Declaration.Kind == BinderOwnerKind::Trait ||
        Declaration.Kind == BinderOwnerKind::Resource) &&
       std::holds_alternative<NominalTypeKey>(Declaration.Identity)) ||
      (Declaration.Kind == BinderOwnerKind::Function &&
       std::holds_alternative<FunctionDeclarationIdentity>(
           Declaration.Identity)) ||
      (Declaration.Kind == BinderOwnerKind::Instance &&
       std::holds_alternative<InstanceBinderOwner>(Declaration.Identity)) ||
      (Declaration.Kind == BinderOwnerKind::Operation &&
       std::holds_alternative<EffectOperationKey>(Declaration.Identity));
  if (!IdentityMatches)
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "owner kind does not match its identity"));
  auto IdentityValid = std::visit(
      Overloaded{[&](const NominalTypeKey &Key) {
                   return validateNominalKey(Key, Path + ".identity");
                 },
                 [&](const FunctionDeclarationIdentity &Identity) {
                   return validateFunctionIdentity(Identity,
                                                   Path + ".identity");
                 },
                 [&](const InstanceBinderOwner &Instance) -> Result {
                   if (auto Valid =
                           validateNominalKey(Instance.Trait, Path + ".trait");
                       !Valid)
                     return Valid;
                   return {};
                 },
                 [&](const EffectOperationKey &Operation) {
                   return validateOperationKey(Operation, Path + ".identity");
                 }},
      Declaration.Identity);
  if (!IdentityValid)
    return IdentityValid;
  if (auto Valid = checkCount(Declaration.TypeParameters.size(), Path); !Valid)
    return Valid;
  if (auto Valid = checkCount(Declaration.EffectVariables.size(), Path); !Valid)
    return Valid;
  if ((Declaration.Kind == BinderOwnerKind::Nominal ||
       Declaration.Kind == BinderOwnerKind::Trait ||
       Declaration.Kind == BinderOwnerKind::Instance ||
       Declaration.Kind == BinderOwnerKind::Resource) &&
      !Declaration.EffectVariables.empty())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "owner kind cannot declare effect variables"));
  if (Declaration.Kind == BinderOwnerKind::Operation &&
      std::any_of(Declaration.EffectVariables.begin(),
                  Declaration.EffectVariables.end(), [](const auto &Variable) {
                    return Variable.Kind != EffectVariableKind::Flexible;
                  }))
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "operation variables must be flexible"));
  if (Declaration.Kind == BinderOwnerKind::Function &&
      Declaration.TypeParameters.empty() && Declaration.EffectVariables.empty())
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                 std::move(Path),
                                 "function binder must declare a parameter"));
  for (const auto Parameter : Declaration.TypeParameters)
    if (!Types.valid(Parameter))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   Path,
                                   "type parameter is invalid or foreign"));
  for (const auto &Variable : Declaration.EffectVariables)
    if (!Types.valid(Variable.Variable) ||
        std::to_underlying(Variable.Kind) >
            std::to_underlying(EffectVariableKind::Opaque))
      return std::unexpected(error(CanonicalEncodingErrorCode::InvalidBinder,
                                   Path,
                                   "effect variable is invalid or foreign"));
  return {};
}

ValueResult<Bytes>
instanceIdentityKey(const TypeTable &Types,
                    const CanonicalBinderDeclaration &Declaration,
                    std::string Path) {
  BindingMaps Local;
  for (std::size_t Index = 0; Index < Declaration.TypeParameters.size();
       ++Index)
    Local.Types.emplace(
        Declaration.TypeParameters[Index],
        CanonicalBinderRef{0, static_cast<std::uint32_t>(Index)});
  GraphCodec Codec(Types, Local, nullptr,
                   CanonicalEncodingErrorCode::UnboundParameter);
  const auto &Instance = std::get<InstanceBinderOwner>(Declaration.Identity);
  Writer Out;
  writeNominalKey(Out, Instance.Trait);
  Out.u64(Instance.Arguments.size());
  for (std::size_t Index = 0; Index < Instance.Arguments.size(); ++Index) {
    auto Expanded =
        Codec.expandedType(Instance.Arguments[Index],
                           Path + ".arguments[" + std::to_string(Index) + "]");
    if (!Expanded)
      return std::unexpected(Expanded.error());
    Out.raw(**Expanded);
  }
  std::vector<Bytes> Constraints;
  for (std::size_t Index = 0; Index < Instance.Constraints.size(); ++Index) {
    auto Expanded = Codec.expandedConstraint(Instance.Constraints[Index],
                                             Path + ".constraints[" +
                                                 std::to_string(Index) + "]");
    if (!Expanded)
      return std::unexpected(Expanded.error());
    Constraints.push_back(std::move(*Expanded));
  }
  std::sort(Constraints.begin(), Constraints.end(), byteLess);
  for (std::size_t Index = 1; Index < Constraints.size(); ++Index)
    if (Constraints[Index - 1] == Constraints[Index])
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   std::move(Path),
                                   "instance contains duplicate constraints"));
  Out.u64(Constraints.size());
  for (const auto &Constraint : Constraints)
    Out.raw(Constraint);
  return std::move(Out).take();
}

ValueResult<Bytes>
directIdentityKey(const CanonicalBinderDeclaration &Declaration) {
  Writer Out;
  std::visit(
      Overloaded{[&](const NominalTypeKey &Key) { writeNominalKey(Out, Key); },
                 [&](const FunctionDeclarationIdentity &Identity) {
                   writeFunctionIdentity(Out, Identity);
                 },
                 [](const InstanceBinderOwner &) {},
                 [&](const EffectOperationKey &Operation) {
                   writeOperationKey(Out, Operation);
                 }},
      Declaration.Identity);
  return std::move(Out).take();
}

ValueResult<BinderLayout>
makeBinderLayout(const TypeTable &Types,
                 std::span<const CanonicalBinderDeclaration> Binders) {
  if (auto Valid = checkCount(Binders.size(), "binders"); !Valid)
    return std::unexpected(Valid.error());
  BinderLayout Layout;
  Layout.Records.reserve(Binders.size());
  std::size_t InstanceOrdinal = 0;
  for (std::size_t Index = 0; Index < Binders.size(); ++Index) {
    const auto Path = "binders[" + std::to_string(Index) + "]";
    if (auto Valid = validateBinderShape(Types, Binders[Index], Path); !Valid)
      return std::unexpected(Valid.error());
    Bytes IdentityKey;
    if (Binders[Index].Kind == BinderOwnerKind::Instance) {
      auto Key = instanceIdentityKey(Types, Binders[Index], Path);
      if (!Key)
        return std::unexpected(Key.error());
      IdentityKey = std::move(*Key);
    } else {
      auto Key = directIdentityKey(Binders[Index]);
      if (!Key)
        return std::unexpected(Key.error());
      IdentityKey = std::move(*Key);
    }
    Layout.Records.push_back({&Binders[Index], Index,
                              Binders[Index].Kind == BinderOwnerKind::Instance
                                  ? InstanceOrdinal++
                                  : std::numeric_limits<std::size_t>::max(),
                              std::move(IdentityKey)});
  }
  std::sort(Layout.Records.begin(), Layout.Records.end(),
            [](const auto &Left, const auto &Right) {
              if (Left.Declaration->Kind != Right.Declaration->Kind)
                return Left.Declaration->Kind < Right.Declaration->Kind;
              return byteLess(Left.IdentityKey, Right.IdentityKey);
            });
  for (std::size_t Index = 1; Index < Layout.Records.size(); ++Index)
    if (Layout.Records[Index - 1].Declaration->Kind ==
            Layout.Records[Index].Declaration->Kind &&
        Layout.Records[Index - 1].IdentityKey ==
            Layout.Records[Index].IdentityKey)
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   "binders", "duplicate binder owner"));

  Layout.InstanceRemap.resize(InstanceOrdinal);
  std::uint32_t CanonicalInstance = 0;
  for (auto &Record : Layout.Records)
    if (Record.Declaration->Kind == BinderOwnerKind::Instance) {
      Record.CanonicalInstanceIndex = CanonicalInstance;
      Layout.InstanceRemap[Record.OriginalInstanceIndex] = CanonicalInstance++;
    }

  Layout.Owners.reserve(Layout.Records.size());
  for (std::size_t BinderIndex = 0; BinderIndex < Layout.Records.size();
       ++BinderIndex) {
    const auto &Declaration = *Layout.Records[BinderIndex].Declaration;
    std::vector<EffectVariableKind> EffectKinds;
    EffectKinds.reserve(Declaration.EffectVariables.size());
    for (const auto &Variable : Declaration.EffectVariables)
      EffectKinds.push_back(Variable.Kind);
    Layout.Owners.push_back(
        {Declaration.Kind, Declaration.Identity,
         static_cast<std::uint32_t>(Declaration.TypeParameters.size()),
         std::move(EffectKinds)});
    for (std::size_t Ordinal = 0; Ordinal < Declaration.TypeParameters.size();
         ++Ordinal) {
      const auto Ref =
          CanonicalBinderRef{static_cast<std::uint32_t>(BinderIndex),
                             static_cast<std::uint32_t>(Ordinal)};
      const auto [_, Inserted] = Layout.Bindings.Types.emplace(
          Declaration.TypeParameters[Ordinal], Ref);
      if (!Inserted)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::MultiplyOwnedParameter, "binders",
                  "type parameter is owned by multiple binders"));
      Layout.TypeRemap.push_back({Declaration.TypeParameters[Ordinal], Ref});
    }
    for (std::size_t Ordinal = 0; Ordinal < Declaration.EffectVariables.size();
         ++Ordinal) {
      const auto Ref =
          CanonicalBinderRef{static_cast<std::uint32_t>(BinderIndex),
                             static_cast<std::uint32_t>(Ordinal)};
      const auto &Variable = Declaration.EffectVariables[Ordinal];
      const auto [_, Inserted] = Layout.Bindings.Effects.emplace(
          Variable.Variable, EffectBinding{Ref, Variable.Kind});
      if (!Inserted)
        return std::unexpected(
            error(CanonicalEncodingErrorCode::MultiplyOwnedParameter, "binders",
                  "effect variable is owned by multiple binders"));
      Layout.EffectRemap.push_back({Variable.Variable, Ref});
    }
  }
  std::sort(Layout.TypeRemap.begin(), Layout.TypeRemap.end(),
            [](const auto &Left, const auto &Right) {
              return Left.Source < Right.Source;
            });
  std::sort(Layout.EffectRemap.begin(), Layout.EffectRemap.end(),
            [](const auto &Left, const auto &Right) {
              return Left.Source < Right.Source;
            });
  return Layout;
}

struct RawBinderUses {
  std::unordered_set<TypeParameterId, StrongIdHasher> Types;
  std::unordered_set<EffectVariableId, StrongIdHasher> Effects;
};

void gatherBinderUses(const StructuralType &Type, RawBinderUses &Uses) {
  std::visit(
      [&](const auto &Value) {
        using ValueType = std::decay_t<decltype(Value)>;
        if constexpr (std::is_same_v<ValueType, TypeParameterType>) {
          Uses.Types.insert(Value.Parameter);
        } else if constexpr (std::is_same_v<ValueType, FunctionType>) {
          Uses.Types.insert(Value.TypeParameters.begin(),
                            Value.TypeParameters.end());
          for (const auto &Variable : Value.EffectVariables)
            Uses.Effects.insert(Variable.Variable);
          for (const auto &Constraint : Value.Constraints)
            Uses.Types.insert(Constraint.Parameter);
        }
      },
      Type);
}

void gatherBinderUses(const EffectRow &Row, RawBinderUses &Uses) {
  for (const auto &Tail : Row.Tails)
    Uses.Effects.insert(Tail.Variable);
}

void gatherBinderUses(const EffectOperationSignature &Signature,
                      RawBinderUses &Uses) {
  Uses.Types.insert(Signature.TypeParameters.begin(),
                    Signature.TypeParameters.end());
  for (const auto &Variable : Signature.EffectVariables)
    Uses.Effects.insert(Variable.Variable);
  for (const auto &Constraint : Signature.Constraints)
    Uses.Types.insert(Constraint.Parameter);
}

std::vector<CanonicalBinderDeclaration>
selectUsedBinders(const TypeTable &Types,
                  const EffectOperationSignature &Signature,
                  std::span<const CanonicalBinderDeclaration> Binders,
                  const std::vector<bool> &ReachableTypes,
                  const std::vector<bool> &ReachableEffects) {
  RawBinderUses Uses;
  gatherBinderUses(Signature, Uses);
  for (std::uint32_t Index = 0; Index < ReachableTypes.size(); ++Index)
    if (ReachableTypes[Index])
      gatherBinderUses(Types.at(TypeId{Types.idDomain(), Index}), Uses);
  for (std::uint32_t Index = 0; Index < ReachableEffects.size(); ++Index)
    if (ReachableEffects[Index])
      gatherBinderUses(Types.effect(EffectRowId{Types.idDomain(), Index}),
                       Uses);

  std::vector<CanonicalBinderDeclaration> Selected;
  for (const auto &Binder : Binders) {
    const bool UsesType = std::any_of(
        Binder.TypeParameters.begin(), Binder.TypeParameters.end(),
        [&](TypeParameterId Id) { return Uses.Types.contains(Id); });
    const bool UsesEffect = std::any_of(
        Binder.EffectVariables.begin(), Binder.EffectVariables.end(),
        [&](const EffectVariableDeclaration &Variable) {
          return Uses.Effects.contains(Variable.Variable);
        });
    if (UsesType || UsesEffect)
      Selected.push_back(Binder);
  }
  return Selected;
}

void writeBinderRecords(Writer &Out, const BinderLayout &Layout) {
  Out.u32(static_cast<std::uint32_t>(Layout.Records.size()));
  for (const auto &Record : Layout.Records) {
    const auto &Declaration = *Record.Declaration;
    Out.u8(std::to_underlying(Declaration.Kind));
    if (Declaration.Kind == BinderOwnerKind::Instance) {
      Out.u32(Record.CanonicalInstanceIndex);
    } else {
      std::visit(Overloaded{[&](const NominalTypeKey &Key) {
                              writeNominalKey(Out, Key);
                            },
                            [&](const FunctionDeclarationIdentity &Identity) {
                              writeFunctionIdentity(Out, Identity);
                            },
                            [](const InstanceBinderOwner &) {},
                            [&](const EffectOperationKey &Operation) {
                              writeOperationKey(Out, Operation);
                            }},
                 Declaration.Identity);
    }
    Out.u32(static_cast<std::uint32_t>(Declaration.TypeParameters.size()));
    Out.u64(Declaration.EffectVariables.size());
    for (const auto &Variable : Declaration.EffectVariables)
      Out.u8(std::to_underlying(Variable.Kind));
  }
}

ValueResult<CanonicalGraphOrder>
makeGraphOrder(const TypeTable &Table, GraphCodec &Codec,
               const std::vector<bool> &ReachableTypes,
               const std::vector<bool> &ReachableEffects) {
  CanonicalGraphOrder Order;
  Order.TypeIndices.assign(Table.typeCount(), UINT32_MAX);
  Order.EffectIndices.assign(Table.effectCount(), UINT32_MAX);
  std::vector<std::pair<Bytes, TypeId>> Types;
  for (std::uint32_t Index = 0; Index < Table.typeCount(); ++Index) {
    if (!ReachableTypes.empty() && !ReachableTypes[Index])
      continue;
    const auto Id = TypeId{Table.idDomain(), Index};
    auto Key = Codec.expandedType(Id, "types[" + std::to_string(Index) + "]");
    if (!Key)
      return std::unexpected(Key.error());
    Types.emplace_back(**Key, Id);
  }
  std::sort(Types.begin(), Types.end(),
            [](const auto &Left, const auto &Right) {
              return byteLess(Left.first, Right.first);
            });
  for (std::size_t Index = 1; Index < Types.size(); ++Index)
    if (Types[Index - 1].first == Types[Index].first)
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   "types",
                                   "distinct types have identical canonical "
                                   "records"));
  for (std::size_t Index = 0; Index < Types.size(); ++Index) {
    Order.Types.push_back(Types[Index].second);
    Order.TypeIndices[Types[Index].second.value()] =
        static_cast<std::uint32_t>(Index);
  }

  std::vector<std::pair<Bytes, EffectRowId>> Effects;
  for (std::uint32_t Index = 0; Index < Table.effectCount(); ++Index) {
    if (!ReachableEffects.empty() && !ReachableEffects[Index])
      continue;
    const auto Id = EffectRowId{Table.idDomain(), Index};
    auto Key =
        Codec.expandedEffect(Id, "effects[" + std::to_string(Index) + "]");
    if (!Key)
      return std::unexpected(Key.error());
    Effects.emplace_back(**Key, Id);
  }
  std::sort(Effects.begin(), Effects.end(),
            [](const auto &Left, const auto &Right) {
              return byteLess(Left.first, Right.first);
            });
  for (std::size_t Index = 1; Index < Effects.size(); ++Index)
    if (Effects[Index - 1].first == Effects[Index].first)
      return std::unexpected(error(CanonicalEncodingErrorCode::DuplicateRecord,
                                   "effects",
                                   "distinct effects have identical canonical "
                                   "records"));
  for (std::size_t Index = 0; Index < Effects.size(); ++Index) {
    Order.Effects.push_back(Effects[Index].second);
    Order.EffectIndices[Effects[Index].second.value()] =
        static_cast<std::uint32_t>(Index);
  }
  return Order;
}

Result writeTypeRecords(Writer &Out, GraphCodec &Codec,
                        const CanonicalGraphOrder &Order) {
  Out.u32(static_cast<std::uint32_t>(Order.Types.size()));
  for (std::size_t Index = 0; Index < Order.Types.size(); ++Index) {
    auto Record = Codec.typeRecord(Order.Types[Index], Order.TypeIndices,
                                   Order.EffectIndices,
                                   "types[" + std::to_string(Index) + "]");
    if (!Record)
      return std::unexpected(Record.error());
    Out.raw(*Record);
  }
  return {};
}

Result writeEffectRecords(Writer &Out, GraphCodec &Codec,
                          const CanonicalGraphOrder &Order) {
  Out.u32(static_cast<std::uint32_t>(Order.Effects.size()));
  for (std::size_t Index = 0; Index < Order.Effects.size(); ++Index) {
    auto Record = Codec.effectRecord(Order.Effects[Index], Order.TypeIndices,
                                     Order.EffectIndices,
                                     "effects[" + std::to_string(Index) + "]");
    if (!Record)
      return std::unexpected(Record.error());
    Out.raw(*Record);
  }
  return {};
}

CanonicalEncodingError tableValidationError(std::string Message) {
  return error(CanonicalEncodingErrorCode::InvalidReference, "types",
               std::move(Message));
}

} // namespace

std::uint64_t
canonicalStructuralFingerprint(std::span<const std::byte> BytesValue) noexcept {
  std::uint64_t Hash = UINT64_C(14695981039346656037);
  for (const auto Byte : BytesValue) {
    Hash ^= std::to_integer<std::uint8_t>(Byte);
    Hash *= UINT64_C(1099511628211);
  }
  return Hash;
}

CanonicalBytesResult encodeClosedDescriptorGraph(const TypeTable &Types,
                                                 const CanonicalRoot &Root) {
  try {
    if (auto Valid = Types.validate(); !Valid)
      return std::unexpected(tableValidationError(Valid.error()));
    const BindingMaps EmptyBindings;
    const std::vector<OwnerRecord> EmptyOwners;
    GraphCodec Codec(Types, EmptyBindings, &EmptyOwners,
                     CanonicalEncodingErrorCode::OpenRuntimeDescriptor);
    std::vector<bool> ReachableTypes(Types.typeCount());
    std::vector<bool> ReachableEffects(Types.effectCount());
    auto Collected = std::visit(
        Overloaded{[&](TypeId Id) {
                     return Codec.collectType(Id, ReachableTypes,
                                              ReachableEffects, "root.type");
                   },
                   [&](EffectRowId Id) {
                     return Codec.collectEffect(
                         Id, ReachableTypes, ReachableEffects, "root.effect");
                   },
                   [&](const ClosedEffectOperation &Operation) {
                     return Codec.collect(Operation, ReachableTypes,
                                          ReachableEffects, "root.operation");
                   }},
        Root);
    if (!Collected)
      return std::unexpected(Collected.error());
    auto Order = makeGraphOrder(Types, Codec, ReachableTypes, ReachableEffects);
    if (!Order)
      return std::unexpected(Order.error());

    Writer Out;
    Out.u32(CanonicalStructuralEncodingVersion);
    Out.u32(0);
    if (auto Written = writeTypeRecords(Out, Codec, *Order); !Written)
      return std::unexpected(Written.error());
    if (auto Written = writeEffectRecords(Out, Codec, *Order); !Written)
      return std::unexpected(Written.error());
    if (std::holds_alternative<ClosedEffectOperation>(Root)) {
      Out.u32(1);
      auto Operation = Codec.closedOperationRecord(
          std::get<ClosedEffectOperation>(Root), Order->TypeIndices,
          Order->EffectIndices, "root.operation");
      if (!Operation)
        return std::unexpected(Operation.error());
      Out.raw(*Operation);
    } else {
      Out.u32(0);
    }
    Out.u8(static_cast<std::uint8_t>(Root.index()));
    if (const auto *Type = std::get_if<TypeId>(&Root))
      Out.u32(Order->TypeIndices[Type->value()]);
    else if (const auto *Effect = std::get_if<EffectRowId>(&Root))
      Out.u32(Order->EffectIndices[Effect->value()]);
    else
      Out.u32(0);
    return std::move(Out).take();
  } catch (const std::bad_alloc &) {
    return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit, "",
                                 "canonical encoding allocation failed"));
  } catch (const std::exception &Exception) {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "", Exception.what()));
  }
}

CanonicalBytesResult encodeOperationDeclarationGraph(
    const TypeTable &Types, const EffectOperationSignature &Signature,
    std::span<const CanonicalBinderDeclaration> Binders) {
  try {
    if (auto Valid = Types.validate(); !Valid)
      return std::unexpected(tableValidationError(Valid.error()));
    auto CompleteLayout = makeBinderLayout(Types, Binders);
    if (!CompleteLayout)
      return std::unexpected(CompleteLayout.error());
    GraphCodec CompleteCodec(Types, CompleteLayout->Bindings,
                             &CompleteLayout->Owners,
                             CanonicalEncodingErrorCode::UnboundParameter);
    std::vector<bool> ReachableTypes(Types.typeCount());
    std::vector<bool> ReachableEffects(Types.effectCount());
    for (const auto &Constraint : Signature.Constraints)
      for (const auto Argument : Constraint.Arguments)
        if (auto Valid = CompleteCodec.collectType(Argument, ReachableTypes,
                                                   ReachableEffects,
                                                   "root.operation.constraint");
            !Valid)
          return std::unexpected(Valid.error());
    for (const auto &Parameter : Signature.Parameters)
      if (auto Valid = CompleteCodec.collectType(Parameter.Type, ReachableTypes,
                                                 ReachableEffects,
                                                 "root.operation.parameter");
          !Valid)
        return std::unexpected(Valid.error());
    if (auto Valid = CompleteCodec.collectType(Signature.Result, ReachableTypes,
                                               ReachableEffects,
                                               "root.operation.result");
        !Valid)
      return std::unexpected(Valid.error());
    if (auto Valid = CompleteCodec.collectEffect(
            Signature.Effects, ReachableTypes, ReachableEffects,
            "root.operation.effects");
        !Valid)
      return std::unexpected(Valid.error());
    auto UsedBinders = selectUsedBinders(Types, Signature, Binders,
                                         ReachableTypes, ReachableEffects);
    auto Layout = makeBinderLayout(Types, UsedBinders);
    if (!Layout)
      return std::unexpected(Layout.error());
    GraphCodec Codec(Types, Layout->Bindings, &Layout->Owners,
                     CanonicalEncodingErrorCode::UnboundParameter);
    auto Order = makeGraphOrder(Types, Codec, ReachableTypes, ReachableEffects);
    if (!Order)
      return std::unexpected(Order.error());
    Writer Out;
    Out.u32(CanonicalStructuralEncodingVersion);
    writeBinderRecords(Out, *Layout);
    if (auto Written = writeTypeRecords(Out, Codec, *Order); !Written)
      return std::unexpected(Written.error());
    if (auto Written = writeEffectRecords(Out, Codec, *Order); !Written)
      return std::unexpected(Written.error());
    Out.u32(1);
    auto Operation = Codec.operationSignatureRecord(
        Signature, Order->TypeIndices, Order->EffectIndices, "root.operation");
    if (!Operation)
      return std::unexpected(Operation.error());
    Out.raw(*Operation);
    Out.u8(std::to_underlying(CanonicalRootKind::Operation));
    Out.u32(0);
    return std::move(Out).take();
  } catch (const std::bad_alloc &) {
    return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit, "",
                                 "canonical encoding allocation failed"));
  } catch (const std::exception &Exception) {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "", Exception.what()));
  }
}

std::expected<CanonicalInterfaceTables, CanonicalEncodingError>
encodeInterfaceTables(const TypeTable &Types,
                      std::span<const CanonicalBinderDeclaration> Binders) {
  try {
    if (auto Valid = Types.validate(); !Valid)
      return std::unexpected(tableValidationError(Valid.error()));
    auto Layout = makeBinderLayout(Types, Binders);
    if (!Layout)
      return std::unexpected(Layout.error());
    GraphCodec Codec(Types, Layout->Bindings, &Layout->Owners,
                     CanonicalEncodingErrorCode::UnboundParameter);
    std::vector<bool> AllTypes(Types.typeCount(), true);
    std::vector<bool> AllEffects(Types.effectCount(), true);
    auto Order = makeGraphOrder(Types, Codec, AllTypes, AllEffects);
    if (!Order)
      return std::unexpected(Order.error());
    CanonicalInterfaceTables ResultTables;
    Writer TypeSection;
    TypeSection.u32(CanonicalStructuralEncodingVersion);
    writeBinderRecords(TypeSection, *Layout);
    if (auto Written = writeTypeRecords(TypeSection, Codec, *Order); !Written)
      return std::unexpected(Written.error());
    ResultTables.TypeSectionPayload = std::move(TypeSection).take();
    Writer EffectSection;
    EffectSection.u32(CanonicalStructuralEncodingVersion);
    if (auto Written = writeEffectRecords(EffectSection, Codec, *Order);
        !Written)
      return std::unexpected(Written.error());
    ResultTables.EffectSectionPayload = std::move(EffectSection).take();
    ResultTables.TypeRemap = std::move(Order->TypeIndices);
    ResultTables.EffectRemap = std::move(Order->EffectIndices);
    ResultTables.InstanceRemap = std::move(Layout->InstanceRemap);
    ResultTables.TypeParameterRemap = std::move(Layout->TypeRemap);
    ResultTables.EffectVariableRemap = std::move(Layout->EffectRemap);
    return ResultTables;
  } catch (const std::bad_alloc &) {
    return std::unexpected(error(CanonicalEncodingErrorCode::SizeLimit, "",
                                 "canonical encoding allocation failed"));
  } catch (const std::exception &Exception) {
    return std::unexpected(error(CanonicalEncodingErrorCode::InvalidReference,
                                 "", Exception.what()));
  }
}

} // namespace yona::model
