#include "yona/Model/StructuralType.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace yona::model {
namespace {

std::atomic<IdDomain> NextTypeTableDomain{1};

IdDomain allocateTypeTableDomain() {
  auto Current = NextTypeTableDomain.load(std::memory_order_relaxed);
  while (Current != InvalidIdDomain) {
    const auto Next = Current == std::numeric_limits<IdDomain>::max()
                          ? InvalidIdDomain
                          : Current + 1;
    if (NextTypeTableDomain.compare_exchange_weak(Current, Next,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed))
      return Current;
  }
  throw std::overflow_error("structural type table domain exhausted");
}

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

void hashCombine(std::size_t &Seed, std::size_t Value) noexcept {
  Seed ^= Value + static_cast<std::size_t>(0x9e3779b9u) + (Seed << 6u) +
          (Seed >> 2u);
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

std::expected<void, std::string> validateKey(const NominalTypeKey &Key,
                                             std::string_view Path) {
  if (!validIdentifier(Key.Name))
    return std::unexpected(std::string(Path) +
                           " has an invalid declaration name");
  return {};
}

std::expected<void, std::string> validateSymbol(const SymbolIdentity &Identity,
                                                std::string_view Path) {
  if (!validIdentifier(Identity.Name))
    return std::unexpected(std::string(Path) + " has an invalid symbol name");
  return {};
}

std::expected<void, std::string>
validateFunctionIdentity(const FunctionDeclarationIdentity &Identity,
                         std::string_view Path) {
  return std::visit(Overloaded{[&](const SymbolIdentity &Symbol) {
                                 return validateSymbol(Symbol, Path);
                               },
                               [&](const LocalFunctionIdentity &Local)
                                   -> std::expected<void, std::string> {
                                 if (Local.LexicalDeclarationPath.empty())
                                   return std::unexpected(
                                       std::string(Path) +
                                       " has an empty lexical path");
                                 return {};
                               }},
                    Identity);
}

class KeyWriter final {
public:
  void byte(std::uint8_t Value) { Bytes_.push_back(static_cast<char>(Value)); }
  void u32(std::uint32_t Value) {
    for (unsigned Shift = 0; Shift != 32; Shift += 8)
      Bytes_.push_back(static_cast<char>((Value >> Shift) & 0xffu));
  }
  void u64(std::uint64_t Value) {
    for (unsigned Shift = 0; Shift != 64; Shift += 8)
      Bytes_.push_back(static_cast<char>((Value >> Shift) & 0xffu));
  }
  void text(std::string_view Value) {
    u64(Value.size());
    Bytes_.append(Value);
  }
  void nested(const std::string &Value) {
    u64(Value.size());
    Bytes_.append(Value);
  }
  [[nodiscard]] std::string take() && { return std::move(Bytes_); }

private:
  std::string Bytes_;
};

void writeModule(KeyWriter &Writer, const ModuleIdentity &Module) {
  Writer.text(Module.fqn());
}
void writeNominalKey(KeyWriter &Writer, const NominalTypeKey &Key) {
  writeModule(Writer, Key.Module);
  Writer.text(Key.Name);
}
void writeSymbol(KeyWriter &Writer, const SymbolIdentity &Identity) {
  writeModule(Writer, Identity.Module);
  Writer.text(Identity.Name);
}
void writeFunctionIdentity(KeyWriter &Writer,
                           const FunctionDeclarationIdentity &Identity) {
  Writer.byte(static_cast<std::uint8_t>(Identity.index()));
  std::visit(Overloaded{[&](const SymbolIdentity &Symbol) {
                          writeSymbol(Writer, Symbol);
                        },
                        [&](const LocalFunctionIdentity &Local) {
                          writeModule(Writer, Local.Module);
                          Writer.u64(Local.LexicalDeclarationPath.size());
                          for (const auto Element :
                               Local.LexicalDeclarationPath)
                            Writer.u32(Element);
                        }},
             Identity);
}

std::string primitiveName(PrimitiveType Type) {
  switch (Type) {
  case PrimitiveType::Unit:
    return "Unit";
  case PrimitiveType::Bool:
    return "Bool";
  case PrimitiveType::Int:
    return "Int";
  case PrimitiveType::Float:
    return "Float";
  case PrimitiveType::Char:
    return "Char";
  case PrimitiveType::String:
    return "String";
  case PrimitiveType::Symbol:
    return "Symbol";
  case PrimitiveType::Byte:
    return "Byte";
  }
  return "<invalid primitive>";
}

std::expected<std::optional<ResultOwnership>, std::string>
classifyResultOwnership(const StructuralType &Type) {
  return std::visit(
      Overloaded{
          [](PrimitiveType Primitive)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return Primitive == PrimitiveType::String
                       ? ResultOwnership::Owned
                       : ResultOwnership::Trivial;
          },
          [](const TypeParameterType &)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return std::optional<ResultOwnership>{};
          },
          [](const AbiOpaqueType &)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return std::unexpected("ABI opaque type is not a legal result");
          },
          [](const OwnedSlotStateType &)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return std::unexpected(
                "owned-slot state is not a legal escaping result");
          },
          [](const OutcomeTaskGroupType &)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return std::unexpected(
                "outcome task group is not a legal escaping result");
          },
          [](const auto &)
              -> std::expected<std::optional<ResultOwnership>, std::string> {
            return ResultOwnership::Owned;
          }},
      Type);
}

} // namespace

std::size_t
NominalTypeKeyHasher::operator()(const NominalTypeKey &Key) const noexcept {
  auto Result = ModuleIdentityHasher{}(Key.Module);
  hashCombine(Result, std::hash<std::string>{}(Key.Name));
  return Result;
}

std::size_t SymbolIdentityHasher::operator()(
    const SymbolIdentity &Identity) const noexcept {
  auto Result = ModuleIdentityHasher{}(Identity.Module);
  hashCombine(Result, std::hash<std::string>{}(Identity.Name));
  return Result;
}

std::size_t FunctionDeclarationIdentityHasher::operator()(
    const FunctionDeclarationIdentity &Identity) const noexcept {
  std::size_t Result = Identity.index();
  std::visit(
      Overloaded{[&](const SymbolIdentity &Symbol) {
                   hashCombine(Result, SymbolIdentityHasher{}(Symbol));
                 },
                 [&](const LocalFunctionIdentity &Local) {
                   hashCombine(Result, ModuleIdentityHasher{}(Local.Module));
                   for (const auto Element : Local.LexicalDeclarationPath)
                     hashCombine(Result, std::hash<std::uint32_t>{}(Element));
                 }},
      Identity);
  return Result;
}

std::size_t EffectOperationKeyHasher::operator()(
    const EffectOperationKey &Key) const noexcept {
  auto Result = NominalTypeKeyHasher{}(Key.Effect);
  hashCombine(Result, std::hash<std::string>{}(Key.Operation));
  return Result;
}

thread_local TypeTable::TestHookState TypeTable::TestHooks_{};

bool TypeTable::faultPoint() noexcept {
  if (!TestHooks_.FailAfter)
    return false;
  if (*TestHooks_.FailAfter == 0)
    return true;
  --*TestHooks_.FailAfter;
  return false;
}

std::size_t TypeTable::hashKey(const std::string &Key) noexcept {
  if (TestHooks_.ConstantHash)
    return *TestHooks_.ConstantHash;
  return std::hash<std::string>{}(Key);
}

TypeTable::TypeTable() : Domain_(allocateTypeTableDomain()) {}

IdDomain TypeTable::idDomain() const noexcept { return Domain_; }

bool TypeTable::valid(TypeId Id) const noexcept {
  return Id.isValid() && Id.domain() == Domain_ && Id.value() < Types_.size();
}
bool TypeTable::valid(EffectRowId Id) const noexcept {
  return Id.isValid() && Id.domain() == Domain_ && Id.value() < Effects_.size();
}
bool TypeTable::valid(TypeParameterId Id) const noexcept {
  return Id.isValid() && Id.domain() == Domain_ &&
         Id.value() < TypeParameterCount_;
}
bool TypeTable::valid(EffectVariableId Id) const noexcept {
  return Id.isValid() && Id.domain() == Domain_ &&
         Id.value() < EffectVariableCount_;
}

const StructuralType &TypeTable::at(TypeId Id) const {
  if (!valid(Id))
    throw std::out_of_range("type ID does not belong to this table");
  return Types_[Id.value()]->Value;
}
const EffectRow &TypeTable::effect(EffectRowId Id) const {
  if (!valid(Id))
    throw std::out_of_range("effect row ID does not belong to this table");
  return Effects_[Id.value()]->Value;
}
std::size_t TypeTable::typeCount() const noexcept { return Types_.size(); }
std::size_t TypeTable::effectCount() const noexcept { return Effects_.size(); }

TypeParameterId TypeTable::allocateTypeParameter() {
  if (PreparedTransaction_ || faultPoint() || TypeParameterCount_ == UINT32_MAX)
    return {};
  const TypeParameterId Result{Domain_, TypeParameterCount_++};
  ++Generation_;
  return Result;
}
EffectVariableId TypeTable::allocateEffectVariable() {
  if (PreparedTransaction_ || faultPoint() ||
      EffectVariableCount_ == UINT32_MAX)
    return {};
  const EffectVariableId Result{Domain_, EffectVariableCount_++};
  ++Generation_;
  return Result;
}

struct TypeTableAppendTransaction::Impl {
  explicit Impl(TypeTable &Owner)
      : Table(&Owner), Domain(Owner.idDomain()), BaseTypes(Owner.typeCount()),
        BaseEffects(Owner.effectCount()),
        BaseTypeParameters(Owner.TypeParameterCount_),
        BaseEffectVariables(Owner.EffectVariableCount_),
        BaseGeneration(Owner.Generation_) {
    Owner.ActiveTransactions_.push_back(this);
  }

  ~Impl() { detach(); }

  TypeTable *Table;
  IdDomain Domain;
  std::size_t BaseTypes;
  std::size_t BaseEffects;
  std::uint32_t BaseTypeParameters;
  std::uint32_t BaseEffectVariables;
  std::uint64_t BaseGeneration;
  std::uint32_t AddedTypeParameters = 0;
  std::uint32_t AddedEffectVariables = 0;
  std::vector<std::optional<StructuralType>> StagedTypes;
  std::vector<std::optional<EffectRow>> StagedEffects;
  std::vector<bool> DefinedTypes;
  std::vector<bool> DefinedEffects;
  std::vector<std::optional<TypeId>> TypeAliases;
  std::vector<TypeId> TypeResolution;
  std::vector<EffectRowId> EffectResolution;
  std::list<TypeTable::StoredType> PreparedTypes;
  std::list<TypeTable::StoredEffect> PreparedEffects;
  std::vector<TypeTable::StoredType *> PreparedTypeIndex;
  std::vector<TypeTable::StoredEffect *> PreparedEffectIndex;
  std::string Failure;
  bool Prepared = false;
  bool Committed = false;

  std::vector<std::uint8_t> TypeState;
  std::vector<std::uint8_t> EffectState;
  std::vector<std::optional<std::string>> TypeKeys;
  std::vector<std::optional<std::string>> EffectKeys;

  [[nodiscard]] bool validTypeRef(TypeId Id) const noexcept {
    return Id.isValid() && Id.domain() == Domain &&
           Id.value() < BaseTypes + StagedTypes.size();
  }
  [[nodiscard]] bool validEffectRef(EffectRowId Id) const noexcept {
    return Id.isValid() && Id.domain() == Domain &&
           Id.value() < BaseEffects + StagedEffects.size();
  }
  [[nodiscard]] bool validTypeParameter(TypeParameterId Id) const noexcept {
    return Id.isValid() && Id.domain() == Domain &&
           Id.value() < BaseTypeParameters + AddedTypeParameters;
  }
  [[nodiscard]] bool validEffectVariable(EffectVariableId Id) const noexcept {
    return Id.isValid() && Id.domain() == Domain &&
           Id.value() < BaseEffectVariables + AddedEffectVariables;
  }

  [[nodiscard]] const StructuralType *lookupType(TypeId Id) const {
    if (!validTypeRef(Id))
      return nullptr;
    if (Id.value() < BaseTypes)
      return &Table->at(Id);
    const auto Index = Id.value() - BaseTypes;
    if (!StagedTypes[Index])
      return nullptr;
    return &*StagedTypes[Index];
  }
  [[nodiscard]] StructuralType *mutableType(TypeId Id) {
    if (!validTypeRef(Id) || Id.value() < BaseTypes)
      return nullptr;
    const auto Index = Id.value() - BaseTypes;
    if (!StagedTypes[Index])
      return nullptr;
    return &*StagedTypes[Index];
  }
  [[nodiscard]] const EffectRow *lookupEffect(EffectRowId Id) const {
    if (!validEffectRef(Id))
      return nullptr;
    if (Id.value() < BaseEffects)
      return &Table->effect(Id);
    const auto Index = Id.value() - BaseEffects;
    if (!StagedEffects[Index])
      return nullptr;
    return &*StagedEffects[Index];
  }
  [[nodiscard]] EffectRow *mutableEffect(EffectRowId Id) {
    if (!validEffectRef(Id) || Id.value() < BaseEffects)
      return nullptr;
    const auto Index = Id.value() - BaseEffects;
    if (!StagedEffects[Index])
      return nullptr;
    return &*StagedEffects[Index];
  }

  std::expected<std::string, std::string> typeKey(TypeId Id);
  std::expected<std::string, std::string> effectKey(EffectRowId Id);
  std::expected<std::string, std::string>
  operationKey(EffectOperationApplication &Application);
  std::expected<std::string, std::string>
  constraintKey(TypeConstraint &Constraint);
  std::expected<std::optional<ResultOwnership>, std::string>
  resultOwnership(TypeId Id);
  std::expected<void, std::string> prepare();
  void detach() noexcept {
    if (!Table)
      return;
    if (Table->PreparedTransaction_ == this)
      Table->PreparedTransaction_ = nullptr;
    const auto Position = std::find(Table->ActiveTransactions_.begin(),
                                    Table->ActiveTransactions_.end(), this);
    if (Position != Table->ActiveTransactions_.end())
      Table->ActiveTransactions_.erase(Position);
    Table = nullptr;
  }

  [[nodiscard]] TypeId finalType(TypeId Id) const {
    if (Id.value() < BaseTypes)
      return Id;
    return TypeResolution[Id.value() - BaseTypes];
  }
  [[nodiscard]] EffectRowId finalEffect(EffectRowId Id) const {
    if (Id.value() < BaseEffects)
      return Id;
    return EffectResolution[Id.value() - BaseEffects];
  }
  void rewrite(StructuralType &Type) const;
  void rewrite(EffectRow &Row) const;
};

TypeTable::TypeTable(TypeTable &&Other) noexcept
    : Domain_(std::exchange(Other.Domain_, InvalidIdDomain)),
      TypeStorage_(std::move(Other.TypeStorage_)),
      EffectStorage_(std::move(Other.EffectStorage_)),
      Types_(std::move(Other.Types_)), Effects_(std::move(Other.Effects_)),
      TypeParameterCount_(std::exchange(Other.TypeParameterCount_, 0)),
      EffectVariableCount_(std::exchange(Other.EffectVariableCount_, 0)),
      Generation_(std::exchange(Other.Generation_, 0)),
      ActiveTransactions_(std::move(Other.ActiveTransactions_)),
      PreparedTransaction_(std::exchange(Other.PreparedTransaction_, nullptr)) {
  for (auto *Transaction : ActiveTransactions_)
    static_cast<TypeTableAppendTransaction::Impl *>(Transaction)->Table = this;
  Other.ActiveTransactions_.clear();
}

TypeTable &TypeTable::operator=(TypeTable &&Other) noexcept {
  if (this == &Other)
    return *this;
  for (auto *Transaction : ActiveTransactions_) {
    auto *Existing =
        static_cast<TypeTableAppendTransaction::Impl *>(Transaction);
    Existing->Table = nullptr;
    Existing->Prepared = false;
    Existing->Failure = "type table was replaced while transaction was open";
  }
  ActiveTransactions_.clear();
  PreparedTransaction_ = nullptr;
  Domain_ = std::exchange(Other.Domain_, InvalidIdDomain);
  TypeStorage_ = std::move(Other.TypeStorage_);
  EffectStorage_ = std::move(Other.EffectStorage_);
  Types_ = std::move(Other.Types_);
  Effects_ = std::move(Other.Effects_);
  TypeParameterCount_ = std::exchange(Other.TypeParameterCount_, 0);
  EffectVariableCount_ = std::exchange(Other.EffectVariableCount_, 0);
  Generation_ = std::exchange(Other.Generation_, 0);
  ActiveTransactions_ = std::move(Other.ActiveTransactions_);
  PreparedTransaction_ = std::exchange(Other.PreparedTransaction_, nullptr);
  for (auto *Transaction : ActiveTransactions_)
    static_cast<TypeTableAppendTransaction::Impl *>(Transaction)->Table = this;
  Other.ActiveTransactions_.clear();
  return *this;
}

std::expected<std::optional<ResultOwnership>, std::string>
TypeTableAppendTransaction::Impl::resultOwnership(TypeId Id) {
  if (!validTypeRef(Id))
    return std::unexpected("result type does not belong to this table");
  while (Id.value() >= BaseTypes) {
    const auto Index = Id.value() - BaseTypes;
    if (Index >= TypeAliases.size() || !TypeAliases[Index])
      break;
    Id = *TypeAliases[Index];
  }
  const auto *Type = lookupType(Id);
  if (!Type)
    return std::unexpected("result type is undefined");
  return classifyResultOwnership(*Type);
}

std::expected<std::string, std::string>
TypeTableAppendTransaction::Impl::operationKey(
    EffectOperationApplication &Application) {
  if (const auto Valid = validateKey(Application.Key.Effect, "effect"); !Valid)
    return std::unexpected(Valid.error());
  if (!validIdentifier(Application.Key.Operation))
    return std::unexpected("effect operation has an invalid name");

  KeyWriter Writer;
  writeNominalKey(Writer, Application.Key.Effect);
  Writer.text(Application.Key.Operation);
  Writer.u64(Application.TypeArguments.size());
  for (const auto Argument : Application.TypeArguments) {
    auto Key = typeKey(Argument);
    if (!Key)
      return std::unexpected(Key.error());
    Writer.nested(*Key);
  }
  Writer.u64(Application.EffectArguments.size());
  for (const auto Argument : Application.EffectArguments) {
    auto Key = effectKey(Argument);
    if (!Key)
      return std::unexpected(Key.error());
    Writer.nested(*Key);
  }
  return std::move(Writer).take();
}

std::expected<std::string, std::string>
TypeTableAppendTransaction::Impl::constraintKey(TypeConstraint &Constraint) {
  if (!validTypeParameter(Constraint.Parameter))
    return std::unexpected("trait constraint has a foreign type parameter");
  if (const auto Valid = validateKey(Constraint.Trait, "trait"); !Valid)
    return std::unexpected(Valid.error());
  KeyWriter Writer;
  Writer.u32(Constraint.Parameter.value());
  writeNominalKey(Writer, Constraint.Trait);
  Writer.u64(Constraint.Arguments.size());
  for (const auto Argument : Constraint.Arguments) {
    auto Key = typeKey(Argument);
    if (!Key)
      return std::unexpected(Key.error());
    Writer.nested(*Key);
  }
  return std::move(Writer).take();
}

std::expected<std::string, std::string>
TypeTableAppendTransaction::Impl::typeKey(TypeId Id) {
  if (!validTypeRef(Id))
    return std::unexpected("type graph contains a foreign or invalid type ID");
  if (TypeState.empty()) {
    TypeState.resize(BaseTypes + StagedTypes.size());
    TypeKeys.resize(BaseTypes + StagedTypes.size());
  }
  if (TypeState[Id.value()] == 1)
    return std::unexpected("structural type/effect graph contains a cycle");
  if (TypeState[Id.value()] == 2)
    return *TypeKeys[Id.value()];

  const auto *Original = lookupType(Id);
  if (!Original)
    return std::unexpected("type graph references an undefined reservation");
  TypeState[Id.value()] = 1;
  StructuralType Normalized = *Original;

  auto fail =
      [&](std::string Message) -> std::expected<std::string, std::string> {
    TypeState[Id.value()] = 0;
    return std::unexpected(std::move(Message));
  };
  auto child = [&](KeyWriter &Writer,
                   TypeId Child) -> std::expected<void, std::string> {
    auto Key = typeKey(Child);
    if (!Key)
      return std::unexpected(Key.error());
    Writer.nested(*Key);
    return {};
  };
  auto effectChild =
      [&](KeyWriter &Writer,
          EffectRowId Child) -> std::expected<void, std::string> {
    auto Key = effectKey(Child);
    if (!Key)
      return std::unexpected(Key.error());
    Writer.nested(*Key);
    return {};
  };

  if (auto *Sum = std::get_if<SumType>(&Normalized)) {
    if (Sum->Alternatives.empty())
      return fail("sum type must contain at least one alternative");
    std::vector<std::pair<std::string, TypeId>> Alternatives;
    std::function<std::expected<void, std::string>(TypeId)> collect =
        [&](TypeId Alternative) -> std::expected<void, std::string> {
      auto Key = typeKey(Alternative);
      if (!Key)
        return std::unexpected(Key.error());
      TypeId ResolvedAlternative = Alternative;
      if (Alternative.value() >= BaseTypes) {
        const auto AliasIndex = Alternative.value() - BaseTypes;
        if (AliasIndex < TypeAliases.size() && TypeAliases[AliasIndex])
          ResolvedAlternative = *TypeAliases[AliasIndex];
      }
      const auto *AlternativeType = lookupType(ResolvedAlternative);
      if (AlternativeType) {
        if (const auto *Nested = std::get_if<SumType>(AlternativeType)) {
          for (const auto NestedAlternative : Nested->Alternatives) {
            auto Result = collect(NestedAlternative);
            if (!Result)
              return Result;
          }
          return {};
        }
      }
      Alternatives.emplace_back(std::move(*Key), ResolvedAlternative);
      return {};
    };
    for (const auto Alternative : Sum->Alternatives) {
      auto Result = collect(Alternative);
      if (!Result)
        return fail(Result.error());
    }
    std::sort(Alternatives.begin(), Alternatives.end(),
              [](const auto &Left, const auto &Right) {
                return Left.first < Right.first;
              });
    Alternatives.erase(std::unique(Alternatives.begin(), Alternatives.end(),
                                   [](const auto &Left, const auto &Right) {
                                     return Left.first == Right.first;
                                   }),
                       Alternatives.end());
    if (Alternatives.empty())
      return fail("sum type normalized to no alternatives");
    if (Alternatives.size() == 1) {
      if (Id.value() < BaseTypes)
        return fail("stored sum type normalized to a singleton");
      TypeAliases[Id.value() - BaseTypes] = Alternatives.front().second;
      TypeState[Id.value()] = 2;
      TypeKeys[Id.value()] = Alternatives.front().first;
      return Alternatives.front().first;
    }
    Sum->Alternatives.clear();
    for (const auto &[_, Alternative] : Alternatives)
      Sum->Alternatives.push_back(Alternative);
  }

  KeyWriter Writer;
  Writer.byte(static_cast<std::uint8_t>(Normalized.index()));
  auto Result = std::visit(
      Overloaded{
          [&](PrimitiveType Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type) >
                std::to_underlying(PrimitiveType::Byte))
              return std::unexpected("invalid primitive type discriminant");
            Writer.byte(std::to_underlying(Type));
            return {};
          },
          [&](TypeParameterType Type) -> std::expected<void, std::string> {
            if (!validTypeParameter(Type.Parameter))
              return std::unexpected(
                  "type parameter does not belong to this table");
            Writer.u32(Type.Parameter.value());
            return {};
          },
          [&](TupleType &Type) -> std::expected<void, std::string> {
            Writer.u64(Type.Elements.size());
            for (const auto Element : Type.Elements)
              if (auto Valid = child(Writer, Element); !Valid)
                return Valid;
            return {};
          },
          [&](SequenceType Type) { return child(Writer, Type.Element); },
          [&](SetType Type) { return child(Writer, Type.Element); },
          [&](DictionaryType Type) -> std::expected<void, std::string> {
            if (auto Valid = child(Writer, Type.Key); !Valid)
              return Valid;
            return child(Writer, Type.Value);
          },
          [&](RecordType &Type) -> std::expected<void, std::string> {
            while (Type.RowRest) {
              auto RestKey = typeKey(*Type.RowRest);
              if (!RestKey)
                return std::unexpected(RestKey.error());
              TypeId RestId = *Type.RowRest;
              if (RestId.value() >= BaseTypes) {
                const auto AliasIndex = RestId.value() - BaseTypes;
                if (AliasIndex < TypeAliases.size() && TypeAliases[AliasIndex])
                  RestId = *TypeAliases[AliasIndex];
              }
              const auto *Rest = lookupType(RestId);
              if (!Rest)
                return std::unexpected("record row rest is undefined");
              if (std::holds_alternative<TypeParameterType>(*Rest))
                break;
              const auto *RestRecord = std::get_if<RecordType>(Rest);
              if (!RestRecord)
                return std::unexpected(
                    "record row rest must be a type parameter or record");
              Type.Fields.insert(Type.Fields.end(), RestRecord->Fields.begin(),
                                 RestRecord->Fields.end());
              Type.RowRest = RestRecord->RowRest;
            }
            std::sort(Type.Fields.begin(), Type.Fields.end(),
                      [](const RecordField &Left, const RecordField &Right) {
                        return Left.Name < Right.Name;
                      });
            for (std::size_t Index = 1; Index < Type.Fields.size(); ++Index)
              if (Type.Fields[Index - 1].Name == Type.Fields[Index].Name)
                return std::unexpected("record has duplicate fields");
            Writer.u64(Type.Fields.size());
            for (const auto &Field : Type.Fields) {
              if (!validIdentifier(Field.Name))
                return std::unexpected("record has an invalid field name");
              Writer.text(Field.Name);
              if (auto Valid = child(Writer, Field.Type); !Valid)
                return Valid;
            }
            Writer.byte(Type.RowRest.has_value());
            if (Type.RowRest)
              return child(Writer, *Type.RowRest);
            return {};
          },
          [&](NominalType &Type) -> std::expected<void, std::string> {
            if (auto Valid = validateKey(Type.Key, "nominal type"); !Valid)
              return Valid;
            writeNominalKey(Writer, Type.Key);
            Writer.u64(Type.Arguments.size());
            for (const auto Argument : Type.Arguments)
              if (auto Valid = child(Writer, Argument); !Valid)
                return Valid;
            return {};
          },
          [&](ArrayType Type) -> std::expected<void, std::string> {
            if (auto Valid = child(Writer, Type.Element); !Valid)
              return Valid;
            Writer.byte(Type.Mutable);
            return {};
          },
          [&](ChannelType Type) { return child(Writer, Type.Element); },
          [&](PromiseType Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.ResultContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected("invalid promise result ownership");
            if (auto Valid = child(Writer, Type.Element); !Valid)
              return Valid;
            const auto Ownership = resultOwnership(Type.Element);
            if (!Ownership)
              return std::unexpected(Ownership.error());
            if (*Ownership && **Ownership != Type.ResultContract)
              return std::unexpected(
                  "promise result ownership does not match its element");
            Writer.byte(std::to_underlying(Type.ResultContract));
            if (auto Valid = effectChild(Writer, Type.Effects); !Valid)
              return Valid;
            const auto *Effects = lookupEffect(Type.Effects);
            if (!Effects || !Effects->MayCancel)
              return std::unexpected(
                  "promise effects must include cancellation");
            return {};
          },
          [&](CursorType Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.ElementContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected("invalid cursor element ownership");
            if (auto Valid = child(Writer, Type.Element); !Valid)
              return Valid;
            const auto Ownership = resultOwnership(Type.Element);
            if (!Ownership)
              return std::unexpected(Ownership.error());
            if (*Ownership && **Ownership != Type.ElementContract)
              return std::unexpected(
                  "cursor element ownership does not match its element");
            Writer.byte(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](CursorStepType Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.ElementContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected("invalid cursor-step element ownership");
            if (auto Valid = child(Writer, Type.Element); !Valid)
              return Valid;
            const auto Ownership = resultOwnership(Type.Element);
            if (!Ownership)
              return std::unexpected(Ownership.error());
            if (*Ownership && **Ownership != Type.ElementContract)
              return std::unexpected(
                  "cursor-step element ownership does not match its element");
            Writer.byte(std::to_underlying(Type.ElementContract));
            return {};
          },
          [&](OutcomeTaskGroupType) -> std::expected<void, std::string> {
            return {};
          },
          [&](ResourceType &Type) -> std::expected<void, std::string> {
            if (auto Valid = validateKey(Type.Key, "resource type"); !Valid)
              return Valid;
            writeNominalKey(Writer, Type.Key);
            Writer.u64(Type.Arguments.size());
            for (const auto Argument : Type.Arguments)
              if (auto Valid = child(Writer, Argument); !Valid)
                return Valid;
            return {};
          },
          [&](AbiOpaqueType Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.Kind) >
                std::to_underlying(AbiOpaqueKind::ContinuationBoundaryContext))
              return std::unexpected("invalid ABI opaque type discriminant");
            Writer.byte(std::to_underlying(Type.Kind));
            return {};
          },
          [&](OwnedSlotStateType &Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.Kind) >
                std::to_underlying(OwnedSlotStateKind::CleanupObligation))
              return std::unexpected("invalid owned-slot state discriminant");
            if (auto Valid = validateFunctionIdentity(Type.DropIdentity,
                                                      "drop identity");
                !Valid)
              return Valid;
            Writer.byte(std::to_underlying(Type.Kind));
            Writer.u64(Type.Fields.size());
            for (const auto &Field : Type.Fields) {
              if (Field.Ownership == ParameterOwnership::Borrow ||
                  std::to_underlying(Field.Ownership) >
                      std::to_underlying(ParameterOwnership::Consume))
                return std::unexpected(
                    "owned-slot field must be Trivial or Consume");
              if (auto Valid = child(Writer, Field.Type); !Valid)
                return Valid;
              Writer.byte(std::to_underlying(Field.Ownership));
            }
            writeFunctionIdentity(Writer, Type.DropIdentity);
            return {};
          },
          [&](FunctionType &Type) -> std::expected<void, std::string> {
            if (std::to_underlying(Type.ResultContract) >
                std::to_underlying(ResultOwnership::Owned))
              return std::unexpected("invalid function result ownership");
            if (std::to_underlying(Type.Convention) >
                std::to_underlying(CallingConvention::ExportedC))
              return std::unexpected("invalid calling convention");
            Writer.u64(Type.TypeParameters.size());
            for (const auto Parameter : Type.TypeParameters) {
              if (!validTypeParameter(Parameter))
                return std::unexpected("function has a foreign type parameter");
              Writer.u32(Parameter.value());
            }
            Writer.u64(Type.EffectVariables.size());
            for (const auto &Variable : Type.EffectVariables) {
              if (!validEffectVariable(Variable.Variable) ||
                  std::to_underlying(Variable.Kind) >
                      std::to_underlying(EffectVariableKind::Opaque))
                return std::unexpected(
                    "function has an invalid effect variable");
              Writer.u32(Variable.Variable.value());
              Writer.byte(std::to_underlying(Variable.Kind));
            }
            std::vector<std::pair<std::string, TypeConstraint>> Constraints;
            Constraints.reserve(Type.Constraints.size());
            for (auto Constraint : Type.Constraints) {
              auto Key = constraintKey(Constraint);
              if (!Key)
                return std::unexpected(Key.error());
              Constraints.emplace_back(std::move(*Key), std::move(Constraint));
            }
            std::sort(Constraints.begin(), Constraints.end(),
                      [](const auto &Left, const auto &Right) {
                        return Left.first < Right.first;
                      });
            for (std::size_t Index = 1; Index < Constraints.size(); ++Index)
              if (Constraints[Index - 1].first == Constraints[Index].first)
                return std::unexpected("function has duplicate constraints");
            Type.Constraints.clear();
            Writer.u64(Constraints.size());
            for (auto &[Key, Constraint] : Constraints) {
              Writer.nested(Key);
              Type.Constraints.push_back(std::move(Constraint));
            }
            Writer.u64(Type.Parameters.size());
            for (const auto &Parameter : Type.Parameters) {
              if (std::to_underlying(Parameter.Ownership) >
                  std::to_underlying(ParameterOwnership::Consume))
                return std::unexpected("invalid parameter ownership");
              if (auto Valid = child(Writer, Parameter.Type); !Valid)
                return Valid;
              Writer.byte(std::to_underlying(Parameter.Ownership));
            }
            if (auto Valid = child(Writer, Type.Result); !Valid)
              return Valid;
            const auto Ownership = resultOwnership(Type.Result);
            if (!Ownership)
              return std::unexpected(Ownership.error());
            if (*Ownership && **Ownership != Type.ResultContract)
              return std::unexpected(
                  "function result ownership does not match its result type");
            Writer.byte(std::to_underlying(Type.ResultContract));
            if (auto Valid = effectChild(Writer, Type.Effects); !Valid)
              return Valid;
            Writer.byte(std::to_underlying(Type.Convention));
            Writer.byte(Type.SourceIdentity.has_value());
            if (Type.SourceIdentity) {
              if (auto Valid = validateFunctionIdentity(*Type.SourceIdentity,
                                                        "source identity");
                  !Valid)
                return Valid;
              writeFunctionIdentity(Writer, *Type.SourceIdentity);
            }
            Writer.byte(Type.ExportedSymbol.has_value());
            if (Type.ExportedSymbol) {
              if (auto Valid =
                      validateSymbol(*Type.ExportedSymbol, "exported symbol");
                  !Valid)
                return Valid;
              writeSymbol(Writer, *Type.ExportedSymbol);
            }
            return {};
          },
          [&](SumType &Type) -> std::expected<void, std::string> {
            Writer.u64(Type.Alternatives.size());
            for (const auto Alternative : Type.Alternatives)
              if (auto Valid = child(Writer, Alternative); !Valid)
                return Valid;
            return {};
          }},
      Normalized);
  if (!Result)
    return fail(Result.error());

  auto Key = std::move(Writer).take();
  if (auto *Mutable = mutableType(Id))
    *Mutable = std::move(Normalized);
  TypeState[Id.value()] = 2;
  TypeKeys[Id.value()] = Key;
  return Key;
}

std::expected<std::string, std::string>
TypeTableAppendTransaction::Impl::effectKey(EffectRowId Id) {
  if (!validEffectRef(Id))
    return std::unexpected(
        "type graph contains a foreign or invalid effect row ID");
  if (EffectState.empty()) {
    EffectState.resize(BaseEffects + StagedEffects.size());
    EffectKeys.resize(BaseEffects + StagedEffects.size());
  }
  if (EffectState[Id.value()] == 1)
    return std::unexpected("structural type/effect graph contains a cycle");
  if (EffectState[Id.value()] == 2)
    return *EffectKeys[Id.value()];
  const auto *Original = lookupEffect(Id);
  if (!Original)
    return std::unexpected("effect graph references an undefined reservation");
  EffectState[Id.value()] = 1;
  EffectRow Normalized = *Original;
  auto fail =
      [&](std::string Message) -> std::expected<std::string, std::string> {
    EffectState[Id.value()] = 0;
    return std::unexpected(std::move(Message));
  };

  std::vector<std::pair<std::string, EffectOperationApplication>> Operations;
  Operations.reserve(Normalized.Operations.size());
  for (auto Application : Normalized.Operations) {
    auto Key = operationKey(Application);
    if (!Key)
      return fail(Key.error());
    Operations.emplace_back(std::move(*Key), std::move(Application));
  }
  std::sort(Operations.begin(), Operations.end(),
            [](const auto &Left, const auto &Right) {
              return Left.first < Right.first;
            });
  Operations.erase(std::unique(Operations.begin(), Operations.end(),
                               [](const auto &Left, const auto &Right) {
                                 return Left.first == Right.first;
                               }),
                   Operations.end());
  Normalized.Operations.clear();
  for (auto &[_, Operation] : Operations)
    Normalized.Operations.push_back(std::move(Operation));

  std::vector<std::pair<std::string, EffectTailProjection>> Tails;
  Tails.reserve(Normalized.Tails.size());
  for (auto Tail : Normalized.Tails) {
    if (!validEffectVariable(Tail.Variable) ||
        std::to_underlying(Tail.Kind) >
            std::to_underlying(EffectVariableKind::Opaque))
      return fail("effect tail has a foreign or invalid variable");
    std::vector<std::pair<std::string, EffectOperationApplication>> Excluded;
    for (auto Application : Tail.ExcludedOperations) {
      auto Key = operationKey(Application);
      if (!Key)
        return fail(Key.error());
      Excluded.emplace_back(std::move(*Key), std::move(Application));
    }
    std::sort(Excluded.begin(), Excluded.end(),
              [](const auto &Left, const auto &Right) {
                return Left.first < Right.first;
              });
    Excluded.erase(std::unique(Excluded.begin(), Excluded.end(),
                               [](const auto &Left, const auto &Right) {
                                 return Left.first == Right.first;
                               }),
                   Excluded.end());
    Tail.ExcludedOperations.clear();
    KeyWriter TailWriter;
    TailWriter.u32(Tail.Variable.value());
    TailWriter.byte(std::to_underlying(Tail.Kind));
    TailWriter.u64(Excluded.size());
    for (auto &[Key, Application] : Excluded) {
      TailWriter.nested(Key);
      Tail.ExcludedOperations.push_back(std::move(Application));
    }
    Tails.emplace_back(std::move(TailWriter).take(), std::move(Tail));
  }
  std::sort(Tails.begin(), Tails.end(),
            [](const auto &Left, const auto &Right) {
              return Left.first < Right.first;
            });
  Tails.erase(std::unique(Tails.begin(), Tails.end(),
                          [](const auto &Left, const auto &Right) {
                            return Left.first == Right.first;
                          }),
              Tails.end());
  Normalized.Tails.clear();
  for (auto &[_, Tail] : Tails)
    Normalized.Tails.push_back(std::move(Tail));

  KeyWriter Writer;
  Writer.u64(Operations.size());
  for (const auto &[Key, _] : Operations)
    Writer.nested(Key);
  Writer.u64(Tails.size());
  for (const auto &[Key, _] : Tails)
    Writer.nested(Key);
  Writer.byte(Normalized.MayRaise);
  Writer.byte(Normalized.MayCancel);
  auto Key = std::move(Writer).take();
  if (auto *Mutable = mutableEffect(Id))
    *Mutable = std::move(Normalized);
  EffectState[Id.value()] = 2;
  EffectKeys[Id.value()] = Key;
  return Key;
}

void TypeTableAppendTransaction::Impl::rewrite(StructuralType &Type) const {
  auto rewriteType = [&](TypeId &Id) { Id = finalType(Id); };
  auto rewriteEffect = [&](EffectRowId &Id) { Id = finalEffect(Id); };
  std::visit(
      Overloaded{[](PrimitiveType) {}, [](TypeParameterType &) {},
                 [&](TupleType &Value) {
                   for (auto &Element : Value.Elements)
                     rewriteType(Element);
                 },
                 [&](SequenceType &Value) { rewriteType(Value.Element); },
                 [&](SetType &Value) { rewriteType(Value.Element); },
                 [&](DictionaryType &Value) {
                   rewriteType(Value.Key);
                   rewriteType(Value.Value);
                 },
                 [&](RecordType &Value) {
                   for (auto &Field : Value.Fields)
                     rewriteType(Field.Type);
                   if (Value.RowRest)
                     rewriteType(*Value.RowRest);
                 },
                 [&](NominalType &Value) {
                   for (auto &Argument : Value.Arguments)
                     rewriteType(Argument);
                 },
                 [&](ArrayType &Value) { rewriteType(Value.Element); },
                 [&](ChannelType &Value) { rewriteType(Value.Element); },
                 [&](PromiseType &Value) {
                   rewriteType(Value.Element);
                   rewriteEffect(Value.Effects);
                 },
                 [&](CursorType &Value) { rewriteType(Value.Element); },
                 [&](CursorStepType &Value) { rewriteType(Value.Element); },
                 [](OutcomeTaskGroupType &) {},
                 [&](ResourceType &Value) {
                   for (auto &Argument : Value.Arguments)
                     rewriteType(Argument);
                 },
                 [](AbiOpaqueType &) {},
                 [&](OwnedSlotStateType &Value) {
                   for (auto &Field : Value.Fields)
                     rewriteType(Field.Type);
                 },
                 [&](FunctionType &Value) {
                   for (auto &Constraint : Value.Constraints)
                     for (auto &Argument : Constraint.Arguments)
                       rewriteType(Argument);
                   for (auto &Parameter : Value.Parameters)
                     rewriteType(Parameter.Type);
                   rewriteType(Value.Result);
                   rewriteEffect(Value.Effects);
                 },
                 [&](SumType &Value) {
                   for (auto &Alternative : Value.Alternatives)
                     rewriteType(Alternative);
                 }},
      Type);
}

void TypeTableAppendTransaction::Impl::rewrite(EffectRow &Row) const {
  auto rewriteApplication = [&](EffectOperationApplication &Application) {
    for (auto &Argument : Application.TypeArguments)
      Argument = finalType(Argument);
    for (auto &Argument : Application.EffectArguments)
      Argument = finalEffect(Argument);
  };
  for (auto &Application : Row.Operations)
    rewriteApplication(Application);
  for (auto &Tail : Row.Tails)
    for (auto &Application : Tail.ExcludedOperations)
      rewriteApplication(Application);
}

std::expected<void, std::string> TypeTableAppendTransaction::Impl::prepare() {
  if (Prepared)
    return std::unexpected("transaction is already prepared");
  if (Committed)
    return std::unexpected("transaction is already committed");
  if (!Failure.empty())
    return std::unexpected(Failure);
  if (Table->PreparedTransaction_ && Table->PreparedTransaction_ != this)
    return std::unexpected("type table already has a prepared transaction");
  if (Table->Generation_ != BaseGeneration)
    return std::unexpected("type table changed while transaction was open");
  if (std::find(DefinedTypes.begin(), DefinedTypes.end(), false) !=
      DefinedTypes.end())
    return std::unexpected("transaction contains an undefined type slot");
  if (std::find(DefinedEffects.begin(), DefinedEffects.end(), false) !=
      DefinedEffects.end())
    return std::unexpected("transaction contains an undefined effect slot");

  TypeAliases.assign(StagedTypes.size(), std::nullopt);
  TypeState.assign(BaseTypes + StagedTypes.size(), 0);
  EffectState.assign(BaseEffects + StagedEffects.size(), 0);
  TypeKeys.assign(BaseTypes + StagedTypes.size(), std::nullopt);
  EffectKeys.assign(BaseEffects + StagedEffects.size(), std::nullopt);

  for (std::uint32_t Index = 0; Index < BaseTypes + StagedTypes.size();
       ++Index) {
    auto Key = typeKey(TypeId{Domain, Index});
    if (!Key)
      return std::unexpected(Key.error());
  }
  for (std::uint32_t Index = 0; Index < BaseEffects + StagedEffects.size();
       ++Index) {
    auto Key = effectKey(EffectRowId{Domain, Index});
    if (!Key)
      return std::unexpected(Key.error());
  }

  TypeResolution.assign(StagedTypes.size(), {});
  EffectResolution.assign(StagedEffects.size(), {});
  std::map<std::string, TypeId> KnownTypes;
  std::map<std::string, EffectRowId> KnownEffects;
  for (std::uint32_t Index = 0; Index < BaseTypes; ++Index)
    KnownTypes.emplace(*TypeKeys[Index], TypeId{Domain, Index});
  for (std::uint32_t Index = 0; Index < BaseEffects; ++Index)
    KnownEffects.emplace(*EffectKeys[Index], EffectRowId{Domain, Index});

  struct NewType {
    std::size_t StagedIndex;
    std::string Key;
    TypeId Final;
  };
  struct NewEffect {
    std::size_t StagedIndex;
    std::string Key;
    EffectRowId Final;
  };
  std::vector<NewType> NewTypes;
  std::vector<NewEffect> NewEffects;

  for (std::size_t Index = 0; Index < StagedTypes.size(); ++Index) {
    if (TypeAliases[Index])
      continue;
    const auto &Key = *TypeKeys[BaseTypes + Index];
    if (const auto Existing = KnownTypes.find(Key);
        Existing != KnownTypes.end()) {
      TypeResolution[Index] = Existing->second;
      continue;
    }
    if (BaseTypes + NewTypes.size() >= UINT32_MAX)
      return std::unexpected("type table size limit exceeded");
    const TypeId Final{Domain,
                       static_cast<std::uint32_t>(BaseTypes + NewTypes.size())};
    KnownTypes.emplace(Key, Final);
    TypeResolution[Index] = Final;
    NewTypes.push_back({Index, Key, Final});
  }
  for (std::size_t Index = 0; Index < StagedTypes.size(); ++Index) {
    if (!TypeAliases[Index])
      continue;
    const auto &Key = *TypeKeys[BaseTypes + Index];
    const auto Existing = KnownTypes.find(Key);
    if (Existing == KnownTypes.end())
      return std::unexpected("singleton sum target was not resolved");
    TypeResolution[Index] = Existing->second;
  }

  for (std::size_t Index = 0; Index < StagedEffects.size(); ++Index) {
    const auto &Key = *EffectKeys[BaseEffects + Index];
    if (const auto Existing = KnownEffects.find(Key);
        Existing != KnownEffects.end()) {
      EffectResolution[Index] = Existing->second;
      continue;
    }
    if (BaseEffects + NewEffects.size() >= UINT32_MAX)
      return std::unexpected("effect table size limit exceeded");
    const EffectRowId Final{
        Domain, static_cast<std::uint32_t>(BaseEffects + NewEffects.size())};
    KnownEffects.emplace(Key, Final);
    EffectResolution[Index] = Final;
    NewEffects.push_back({Index, Key, Final});
  }

  try {
    if (TypeTable::faultPoint())
      return std::unexpected("injected structural table reserve failure");
    PreparedTypeIndex = Table->Types_;
    PreparedTypeIndex.reserve(BaseTypes + NewTypes.size());
    if (TypeTable::faultPoint())
      return std::unexpected("injected structural table reserve failure");
    PreparedEffectIndex = Table->Effects_;
    PreparedEffectIndex.reserve(BaseEffects + NewEffects.size());
    for (const auto &New : NewTypes) {
      if (TypeTable::faultPoint())
        return std::unexpected("injected structural type allocation failure");
      auto Value = *StagedTypes[New.StagedIndex];
      rewrite(Value);
      PreparedTypes.push_back(TypeTable::StoredType{
          std::move(Value), New.Key, TypeTable::hashKey(New.Key)});
      PreparedTypeIndex.push_back(&PreparedTypes.back());
    }
    for (const auto &New : NewEffects) {
      if (TypeTable::faultPoint())
        return std::unexpected("injected structural effect allocation failure");
      auto Value = *StagedEffects[New.StagedIndex];
      rewrite(Value);
      PreparedEffects.push_back(TypeTable::StoredEffect{
          std::move(Value), New.Key, TypeTable::hashKey(New.Key)});
      PreparedEffectIndex.push_back(&PreparedEffects.back());
    }
  } catch (const std::bad_alloc &) {
    PreparedTypes.clear();
    PreparedEffects.clear();
    PreparedTypeIndex.clear();
    PreparedEffectIndex.clear();
    return std::unexpected("structural table allocation failed");
  }

  Table->PreparedTransaction_ = this;
  Prepared = true;
  return {};
}

TypeTableAppendTransaction::TypeTableAppendTransaction(TypeTable &Types)
    : Implementation_(std::make_unique<Impl>(Types)) {}
TypeTableAppendTransaction::TypeTableAppendTransaction(
    TypeTableAppendTransaction &&) noexcept = default;
TypeTableAppendTransaction &TypeTableAppendTransaction::operator=(
    TypeTableAppendTransaction &&) noexcept = default;
TypeTableAppendTransaction::~TypeTableAppendTransaction() = default;

TypeId TypeTableAppendTransaction::reserveType() {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || TypeTable::faultPoint() ||
      State.BaseTypes + State.StagedTypes.size() >= UINT32_MAX) {
    State.Failure = "unable to reserve structural type slot";
    return {};
  }
  const TypeId Result{
      State.Domain,
      static_cast<std::uint32_t>(State.BaseTypes + State.StagedTypes.size())};
  State.StagedTypes.emplace_back();
  State.DefinedTypes.push_back(false);
  return Result;
}

EffectRowId TypeTableAppendTransaction::reserveEffect() {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || TypeTable::faultPoint() ||
      State.BaseEffects + State.StagedEffects.size() >= UINT32_MAX) {
    State.Failure = "unable to reserve structural effect slot";
    return {};
  }
  const EffectRowId Result{State.Domain,
                           static_cast<std::uint32_t>(
                               State.BaseEffects + State.StagedEffects.size())};
  State.StagedEffects.emplace_back();
  State.DefinedEffects.push_back(false);
  return Result;
}

TypeParameterId TypeTableAppendTransaction::allocateTypeParameter() {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || TypeTable::faultPoint() ||
      State.BaseTypeParameters + State.AddedTypeParameters == UINT32_MAX) {
    State.Failure = "unable to allocate structural type parameter";
    return {};
  }
  return TypeParameterId{State.Domain, State.BaseTypeParameters +
                                           State.AddedTypeParameters++};
}

EffectVariableId TypeTableAppendTransaction::allocateEffectVariable() {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || TypeTable::faultPoint() ||
      State.BaseEffectVariables + State.AddedEffectVariables == UINT32_MAX) {
    State.Failure = "unable to allocate structural effect variable";
    return {};
  }
  return EffectVariableId{State.Domain, State.BaseEffectVariables +
                                            State.AddedEffectVariables++};
}

std::expected<void, std::string>
TypeTableAppendTransaction::define(TypeId Reserved, StructuralType Type) {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || !Reserved.isValid() ||
      Reserved.domain() != State.Domain || Reserved.value() < State.BaseTypes ||
      Reserved.value() >= State.BaseTypes + State.StagedTypes.size()) {
    State.Failure = "type definition does not name this transaction's slot";
    return std::unexpected(State.Failure);
  }
  const auto Index = Reserved.value() - State.BaseTypes;
  if (State.DefinedTypes[Index]) {
    State.Failure = "type reservation was defined more than once";
    return std::unexpected(State.Failure);
  }
  State.StagedTypes[Index] = std::move(Type);
  State.DefinedTypes[Index] = true;
  return {};
}

std::expected<void, std::string>
TypeTableAppendTransaction::defineEffect(EffectRowId Reserved, EffectRow Row) {
  auto &State = *Implementation_;
  if (State.Prepared || State.Committed || !Reserved.isValid() ||
      Reserved.domain() != State.Domain ||
      Reserved.value() < State.BaseEffects ||
      Reserved.value() >= State.BaseEffects + State.StagedEffects.size()) {
    State.Failure = "effect definition does not name this transaction's slot";
    return std::unexpected(State.Failure);
  }
  const auto Index = Reserved.value() - State.BaseEffects;
  if (State.DefinedEffects[Index]) {
    State.Failure = "effect reservation was defined more than once";
    return std::unexpected(State.Failure);
  }
  State.StagedEffects[Index] = std::move(Row);
  State.DefinedEffects[Index] = true;
  return {};
}

std::expected<TypeId, std::string>
TypeTableAppendTransaction::intern(StructuralType Type) {
  const auto Reserved = reserveType();
  if (!Reserved.isValid())
    return std::unexpected(Implementation_->Failure);
  if (auto Result = define(Reserved, std::move(Type)); !Result)
    return std::unexpected(Result.error());
  return Reserved;
}

std::expected<TypeId, std::string>
TypeTableAppendTransaction::internSum(std::span<const TypeId> Alternatives) {
  return intern(
      SumType{std::vector<TypeId>(Alternatives.begin(), Alternatives.end())});
}

std::expected<EffectRowId, std::string>
TypeTableAppendTransaction::internEffect(EffectRow Row) {
  const auto Reserved = reserveEffect();
  if (!Reserved.isValid())
    return std::unexpected(Implementation_->Failure);
  if (auto Result = defineEffect(Reserved, std::move(Row)); !Result)
    return std::unexpected(Result.error());
  return Reserved;
}

std::expected<void, std::string> TypeTableAppendTransaction::prepareCommit() {
  auto Result = Implementation_->prepare();
  if (!Result && Implementation_->Failure.empty())
    Implementation_->Failure = Result.error();
  return Result;
}

std::expected<TypeId, std::string>
TypeTableAppendTransaction::resolved(TypeId Provisional) const {
  const auto &State = *Implementation_;
  if (!State.Prepared)
    return std::unexpected("transaction has no prepared type mapping");
  if (!Provisional.isValid() || Provisional.domain() != State.Domain)
    return std::unexpected("type ID does not belong to this transaction");
  if (Provisional.value() < State.BaseTypes)
    return Provisional;
  const auto Index = Provisional.value() - State.BaseTypes;
  if (Index >= State.TypeResolution.size() || !State.DefinedTypes[Index])
    return std::unexpected("type reservation is undefined");
  return State.TypeResolution[Index];
}

std::expected<EffectRowId, std::string>
TypeTableAppendTransaction::resolved(EffectRowId Provisional) const {
  const auto &State = *Implementation_;
  if (!State.Prepared)
    return std::unexpected("transaction has no prepared effect mapping");
  if (!Provisional.isValid() || Provisional.domain() != State.Domain)
    return std::unexpected("effect ID does not belong to this transaction");
  if (Provisional.value() < State.BaseEffects)
    return Provisional;
  const auto Index = Provisional.value() - State.BaseEffects;
  if (Index >= State.EffectResolution.size() || !State.DefinedEffects[Index])
    return std::unexpected("effect reservation is undefined");
  return State.EffectResolution[Index];
}

void TypeTableAppendTransaction::commitPrepared() noexcept {
  auto &State = *Implementation_;
  if (!State.Prepared || State.Committed)
    return;
  State.Table->TypeStorage_.splice(State.Table->TypeStorage_.end(),
                                   State.PreparedTypes);
  State.Table->EffectStorage_.splice(State.Table->EffectStorage_.end(),
                                     State.PreparedEffects);
  State.Table->Types_.swap(State.PreparedTypeIndex);
  State.Table->Effects_.swap(State.PreparedEffectIndex);
  State.Table->TypeParameterCount_ += State.AddedTypeParameters;
  State.Table->EffectVariableCount_ += State.AddedEffectVariables;
  ++State.Table->Generation_;
  State.Committed = true;
  State.detach();
}

TypeTableAppendTransaction TypeTable::beginAppendTransaction() {
  return TypeTableAppendTransaction(*this);
}

std::expected<TypeId, std::string> TypeTable::intern(StructuralType Type) {
  auto Transaction = beginAppendTransaction();
  auto Provisional = Transaction.intern(std::move(Type));
  if (!Provisional)
    return std::unexpected(Provisional.error());
  if (auto Prepared = Transaction.prepareCommit(); !Prepared)
    return std::unexpected(Prepared.error());
  auto Final = Transaction.resolved(*Provisional);
  if (!Final)
    return std::unexpected(Final.error());
  Transaction.commitPrepared();
  return Final;
}

std::expected<TypeId, std::string>
TypeTable::internSum(std::span<const TypeId> Alternatives) {
  return intern(
      SumType{std::vector<TypeId>(Alternatives.begin(), Alternatives.end())});
}

std::expected<EffectRowId, std::string> TypeTable::internEffect(EffectRow Row) {
  auto Transaction = beginAppendTransaction();
  auto Provisional = Transaction.internEffect(std::move(Row));
  if (!Provisional)
    return std::unexpected(Provisional.error());
  if (auto Prepared = Transaction.prepareCommit(); !Prepared)
    return std::unexpected(Prepared.error());
  auto Final = Transaction.resolved(*Provisional);
  if (!Final)
    return std::unexpected(Final.error());
  Transaction.commitPrepared();
  return Final;
}

std::expected<void, std::string> TypeTable::validate() const {
  auto &Mutable = const_cast<TypeTable &>(*this);
  TypeTableAppendTransaction Validation(Mutable);
  return Validation.prepareCommit();
}

std::string TypeTable::format(TypeId Id) const {
  if (!valid(Id))
    throw std::out_of_range("type ID does not belong to this table");
  std::function<std::string(TypeId)> formatType = [&](TypeId Current) {
    return std::visit(
        Overloaded{
            [&](PrimitiveType Type) { return primitiveName(Type); },
            [&](TypeParameterType Type) {
              return "t" + std::to_string(Type.Parameter.value());
            },
            [&](const TupleType &Type) {
              std::string Result = "(";
              for (std::size_t Index = 0; Index < Type.Elements.size();
                   ++Index) {
                if (Index)
                  Result += ", ";
                Result += formatType(Type.Elements[Index]);
              }
              return Result + ")";
            },
            [&](SequenceType Type) {
              return "Seq " + formatType(Type.Element);
            },
            [&](SetType Type) { return "Set " + formatType(Type.Element); },
            [&](DictionaryType Type) {
              return "Dict " + formatType(Type.Key) + " " +
                     formatType(Type.Value);
            },
            [&](const RecordType &Type) {
              std::string Result = "{";
              for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index) {
                if (Index)
                  Result += ", ";
                Result += Type.Fields[Index].Name + " : " +
                          formatType(Type.Fields[Index].Type);
              }
              if (Type.RowRest) {
                if (!Type.Fields.empty())
                  Result += " | ";
                Result += formatType(*Type.RowRest);
              }
              return Result + "}";
            },
            [&](const NominalType &Type) {
              std::string Result = Type.Key.Module.fqn() + "\\" + Type.Key.Name;
              for (const auto Argument : Type.Arguments)
                Result += " " + formatType(Argument);
              return Result;
            },
            [&](ArrayType Type) {
              return std::string(Type.Mutable ? "MutableArray " : "Array ") +
                     formatType(Type.Element);
            },
            [&](ChannelType Type) {
              return "Channel " + formatType(Type.Element);
            },
            [&](PromiseType Type) {
              return "Promise " + formatType(Type.Element);
            },
            [&](CursorType Type) {
              return "Cursor " + formatType(Type.Element);
            },
            [&](CursorStepType Type) {
              return "CursorStep " + formatType(Type.Element);
            },
            [&](OutcomeTaskGroupType) {
              return std::string("OutcomeTaskGroup");
            },
            [&](const ResourceType &Type) {
              std::string Result = Type.Key.Module.fqn() + "\\" + Type.Key.Name;
              for (const auto Argument : Type.Arguments)
                Result += " " + formatType(Argument);
              return Result;
            },
            [&](AbiOpaqueType) { return std::string("<abi-opaque>"); },
            [&](const OwnedSlotStateType &) {
              return std::string("<owned-slot-state>");
            },
            [&](const FunctionType &Type) {
              std::string Result = "(";
              for (std::size_t Index = 0; Index < Type.Parameters.size();
                   ++Index) {
                if (Index)
                  Result += ", ";
                Result += formatType(Type.Parameters[Index].Type);
              }
              return Result + ") -> " + formatType(Type.Result);
            },
            [&](const SumType &Type) {
              std::string Result;
              for (std::size_t Index = 0; Index < Type.Alternatives.size();
                   ++Index) {
                if (Index)
                  Result += " | ";
                Result += formatType(Type.Alternatives[Index]);
              }
              return Result;
            }},
        at(Current));
  };
  return formatType(Id);
}

std::expected<void, std::string>
StructuralDeclarationCatalog::addNominal(NominalDeclarationArity Declaration) {
  if (auto Valid = validateKey(Declaration.Key, "nominal declaration"); !Valid)
    return Valid;
  const auto Existing =
      std::find_if(Nominals_.begin(), Nominals_.end(), [&](const auto &Value) {
        return Value.Key == Declaration.Key;
      });
  if (Existing != Nominals_.end()) {
    if (*Existing == Declaration)
      return std::unexpected("duplicate nominal declaration");
    return std::unexpected("conflicting nominal declaration");
  }
  Nominals_.push_back(std::move(Declaration));
  return {};
}

std::expected<void, std::string>
StructuralDeclarationCatalog::addResource(ResourceDeclaration Declaration) {
  if (auto Valid = validateKey(Declaration.Key, "resource declaration"); !Valid)
    return Valid;
  if (std::to_underlying(Declaration.AbiKind) >
      std::to_underlying(ResourceAbiKind::ChannelReceiver))
    return std::unexpected("resource declaration has an invalid ABI kind");
  if (std::to_underlying(Declaration.Shareability) >
      std::to_underlying(ResourceShareability::AlwaysShareable))
    return std::unexpected("resource declaration has invalid shareability");
  if (!validIdentifier(Declaration.ReleaseNativeSymbol))
    return std::unexpected("resource declaration needs a valid release symbol");
  if (Declaration.Shareability == ResourceShareability::Linear &&
      Declaration.TryRetainNativeSymbol)
    return std::unexpected("linear resource cannot declare a retain callback");
  if (Declaration.Shareability == ResourceShareability::AlwaysShareable &&
      (!Declaration.TryRetainNativeSymbol ||
       !validIdentifier(*Declaration.TryRetainNativeSymbol)))
    return std::unexpected(
        "shareable resource needs a valid try-retain callback");
  if ((Declaration.AbiKind == ResourceAbiKind::ChannelSender ||
       Declaration.AbiKind == ResourceAbiKind::ChannelReceiver) &&
      Declaration.Shareability != ResourceShareability::AlwaysShareable)
    return std::unexpected("channel endpoint resources must be shareable");
  const auto Existing = std::find_if(
      Resources_.begin(), Resources_.end(),
      [&](const auto &Value) { return Value.Key == Declaration.Key; });
  if (Existing != Resources_.end()) {
    if (*Existing == Declaration)
      return std::unexpected("duplicate resource declaration");
    return std::unexpected("conflicting resource declaration policy");
  }
  Resources_.push_back(std::move(Declaration));
  return {};
}

std::optional<std::uint32_t> StructuralDeclarationCatalog::nominalArity(
    const NominalTypeKey &Key) const noexcept {
  const auto Found =
      std::find_if(Nominals_.begin(), Nominals_.end(),
                   [&](const auto &Value) { return Value.Key == Key; });
  if (Found == Nominals_.end())
    return std::nullopt;
  return Found->TypeParameterCount;
}

const ResourceDeclaration *StructuralDeclarationCatalog::resource(
    const NominalTypeKey &Key) const noexcept {
  const auto Found =
      std::find_if(Resources_.begin(), Resources_.end(),
                   [&](const auto &Value) { return Value.Key == Key; });
  return Found == Resources_.end() ? nullptr : &*Found;
}

std::expected<void, std::string> StructuralDeclarationCatalog::validate(
    const TypeTable &Types, std::span<const TypeId> RuntimeRoots) const {
  if (auto Valid = Types.validate(); !Valid)
    return Valid;
  for (const auto &Declaration : Resources_) {
    std::unordered_set<std::uint32_t> Seen;
    for (const auto Parameter : Declaration.TypeParameters) {
      if (!Types.valid(Parameter))
        return std::unexpected(
            "resource declaration contains a foreign type parameter");
      if (!Seen.insert(Parameter.value()).second)
        return std::unexpected(
            "resource declaration contains a duplicate type parameter");
    }
  }
  for (const auto Root : RuntimeRoots)
    if (!Types.valid(Root))
      return std::unexpected("runtime root does not belong to this type table");

  for (std::uint32_t Index = 0; Index < Types.typeCount(); ++Index) {
    const auto Id = TypeId{Types.idDomain(), Index};
    const auto Result = std::visit(
        Overloaded{
            [&](const NominalType &Type) -> std::expected<void, std::string> {
              const auto Arity = nominalArity(Type.Key);
              if (!Arity)
                return std::unexpected("unknown nominal declaration " +
                                       Type.Key.Module.fqn() + "\\" +
                                       Type.Key.Name);
              if (*Arity != Type.Arguments.size())
                return std::unexpected("wrong nominal type argument arity");
              return {};
            },
            [&](const ResourceType &Type) -> std::expected<void, std::string> {
              const auto *Declaration = resource(Type.Key);
              if (Declaration &&
                  Declaration->TypeParameters.size() != Type.Arguments.size())
                return std::unexpected("wrong resource type argument arity");
              return {};
            },
            [](const auto &) -> std::expected<void, std::string> {
              return {};
            }},
        Types.at(Id));
    if (!Result)
      return Result;
  }

  std::vector<bool> ReachableTypes(Types.typeCount());
  std::vector<bool> ReachableEffects(Types.effectCount());
  std::function<void(TypeId)> visitType;
  std::function<void(EffectRowId)> visitEffect;
  auto visitApplication = [&](const EffectOperationApplication &Application) {
    for (const auto Argument : Application.TypeArguments)
      visitType(Argument);
    for (const auto Argument : Application.EffectArguments)
      visitEffect(Argument);
  };
  visitEffect = [&](EffectRowId Id) {
    if (ReachableEffects[Id.value()])
      return;
    ReachableEffects[Id.value()] = true;
    const auto &Row = Types.effect(Id);
    for (const auto &Application : Row.Operations)
      visitApplication(Application);
    for (const auto &Tail : Row.Tails)
      for (const auto &Application : Tail.ExcludedOperations)
        visitApplication(Application);
  };
  visitType = [&](TypeId Id) {
    if (ReachableTypes[Id.value()])
      return;
    ReachableTypes[Id.value()] = true;
    auto child = [&](TypeId Child) { visitType(Child); };
    std::visit(
        Overloaded{[](PrimitiveType) {}, [](const TypeParameterType &) {},
                   [&](const TupleType &Type) {
                     for (const auto Element : Type.Elements)
                       child(Element);
                   },
                   [&](const SequenceType &Type) { child(Type.Element); },
                   [&](const SetType &Type) { child(Type.Element); },
                   [&](const DictionaryType &Type) {
                     child(Type.Key);
                     child(Type.Value);
                   },
                   [&](const RecordType &Type) {
                     for (const auto &Field : Type.Fields)
                       child(Field.Type);
                     if (Type.RowRest)
                       child(*Type.RowRest);
                   },
                   [&](const NominalType &Type) {
                     for (const auto Argument : Type.Arguments)
                       child(Argument);
                   },
                   [&](const ArrayType &Type) { child(Type.Element); },
                   [&](const ChannelType &Type) { child(Type.Element); },
                   [&](const PromiseType &Type) {
                     child(Type.Element);
                     visitEffect(Type.Effects);
                   },
                   [&](const CursorType &Type) { child(Type.Element); },
                   [&](const CursorStepType &Type) { child(Type.Element); },
                   [](const OutcomeTaskGroupType &) {},
                   [&](const ResourceType &Type) {
                     for (const auto Argument : Type.Arguments)
                       child(Argument);
                   },
                   [](const AbiOpaqueType &) {},
                   [&](const OwnedSlotStateType &Type) {
                     for (const auto &Field : Type.Fields)
                       child(Field.Type);
                   },
                   [&](const FunctionType &Type) {
                     for (const auto &Constraint : Type.Constraints)
                       for (const auto Argument : Constraint.Arguments)
                         child(Argument);
                     for (const auto &Parameter : Type.Parameters)
                       child(Parameter.Type);
                     child(Type.Result);
                     visitEffect(Type.Effects);
                   },
                   [&](const SumType &Type) {
                     for (const auto Alternative : Type.Alternatives)
                       child(Alternative);
                   }},
        Types.at(Id));
  };
  for (const auto Root : RuntimeRoots)
    visitType(Root);
  for (std::uint32_t Index = 0; Index < Types.typeCount(); ++Index) {
    if (!ReachableTypes[Index])
      continue;
    if (const auto *Resource = std::get_if<ResourceType>(
            &Types.at(TypeId{Types.idDomain(), Index}));
        Resource && !resource(Resource->Key))
      return std::unexpected(
          "runtime-reachable resource has no declaration policy");
  }
  return {};
}

namespace {

std::expected<void, std::string>
validateBinderIdentityShape(const CanonicalBinderDeclaration &Declaration) {
  const auto Kind = Declaration.Kind;
  if (std::to_underlying(Kind) > std::to_underlying(BinderOwnerKind::Resource))
    return std::unexpected("invalid binder owner kind");
  const bool IdentityMatches =
      ((Kind == BinderOwnerKind::Nominal || Kind == BinderOwnerKind::Trait ||
        Kind == BinderOwnerKind::Resource) &&
       std::holds_alternative<NominalTypeKey>(Declaration.Identity)) ||
      (Kind == BinderOwnerKind::Function &&
       std::holds_alternative<FunctionDeclarationIdentity>(
           Declaration.Identity)) ||
      (Kind == BinderOwnerKind::Instance &&
       std::holds_alternative<InstanceBinderOwner>(Declaration.Identity)) ||
      (Kind == BinderOwnerKind::Operation &&
       std::holds_alternative<EffectOperationKey>(Declaration.Identity));
  if (!IdentityMatches)
    return std::unexpected("binder owner kind does not match its identity");
  auto IdentityValid = std::visit(
      Overloaded{
          [](const NominalTypeKey &Key) {
            return validateKey(Key, "binder owner");
          },
          [](const FunctionDeclarationIdentity &Identity) {
            return validateFunctionIdentity(Identity, "binder owner");
          },
          [](const InstanceBinderOwner &Instance)
              -> std::expected<void, std::string> {
            if (auto Valid = validateKey(Instance.Trait, "instance trait");
                !Valid)
              return Valid;
            for (const auto &Constraint : Instance.Constraints)
              if (auto Valid =
                      validateKey(Constraint.Trait, "instance constraint");
                  !Valid)
                return Valid;
            return {};
          },
          [](const EffectOperationKey &Operation)
              -> std::expected<void, std::string> {
            if (auto Valid = validateKey(Operation.Effect, "operation effect");
                !Valid)
              return Valid;
            if (!validIdentifier(Operation.Operation))
              return std::unexpected(
                  "binder owner has an invalid operation name");
            return {};
          }},
      Declaration.Identity);
  if (!IdentityValid)
    return IdentityValid;
  if (std::any_of(Declaration.EffectVariables.begin(),
                  Declaration.EffectVariables.end(), [](const auto &Variable) {
                    return std::to_underlying(Variable.Kind) >
                           std::to_underlying(EffectVariableKind::Opaque);
                  }))
    return std::unexpected("binder owner has an invalid effect variable kind");
  if ((Kind == BinderOwnerKind::Nominal || Kind == BinderOwnerKind::Trait ||
       Kind == BinderOwnerKind::Instance ||
       Kind == BinderOwnerKind::Resource) &&
      !Declaration.EffectVariables.empty())
    return std::unexpected("binder owner kind cannot declare effect variables");
  if (Kind == BinderOwnerKind::Operation &&
      std::any_of(Declaration.EffectVariables.begin(),
                  Declaration.EffectVariables.end(), [](const auto &Variable) {
                    return Variable.Kind != EffectVariableKind::Flexible;
                  }))
    return std::unexpected(
        "operation binder can declare only flexible effect variables");
  return {};
}

bool binderIdentitiesCorrespond(const CanonicalBinderDeclaration &Source,
                                const CanonicalBinderDeclaration &Destination) {
  if (Source.Kind != Destination.Kind ||
      Source.Identity.index() != Destination.Identity.index())
    return false;
  if (const auto *SourceKey = std::get_if<NominalTypeKey>(&Source.Identity))
    return *SourceKey == std::get<NominalTypeKey>(Destination.Identity);
  if (const auto *SourceFunction =
          std::get_if<FunctionDeclarationIdentity>(&Source.Identity))
    return *SourceFunction ==
           std::get<FunctionDeclarationIdentity>(Destination.Identity);
  if (const auto *SourceOperation =
          std::get_if<EffectOperationKey>(&Source.Identity))
    return *SourceOperation ==
           std::get<EffectOperationKey>(Destination.Identity);
  const auto &SourceInstance = std::get<InstanceBinderOwner>(Source.Identity);
  const auto &DestinationInstance =
      std::get<InstanceBinderOwner>(Destination.Identity);
  return SourceInstance.Trait == DestinationInstance.Trait &&
         SourceInstance.Arguments.size() ==
             DestinationInstance.Arguments.size() &&
         SourceInstance.Constraints.size() ==
             DestinationInstance.Constraints.size();
}

void sortConstraints(std::vector<TypeConstraint> &Constraints) {
  std::sort(Constraints.begin(), Constraints.end(),
            [](const TypeConstraint &Left, const TypeConstraint &Right) {
              if (Left.Parameter != Right.Parameter)
                return Left.Parameter < Right.Parameter;
              if (Left.Trait != Right.Trait)
                return Left.Trait < Right.Trait;
              return std::lexicographical_compare(
                  Left.Arguments.begin(), Left.Arguments.end(),
                  Right.Arguments.begin(), Right.Arguments.end());
            });
}

} // namespace

std::expected<TypeRemap, std::string>
TypeTable::remapInto(TypeTable &Destination,
                     std::span<const BinderDeclarationPair> BinderPairs) const {
  if (this == &Destination)
    return std::unexpected("cannot remap a type table into itself");
  if (auto Valid = validate(); !Valid)
    return std::unexpected(Valid.error());

  TypeRemap Remap;
  Remap.Types.resize(typeCount());
  Remap.Effects.resize(effectCount());
  Remap.TypeParameters.resize(TypeParameterCount_);
  Remap.EffectVariables.resize(EffectVariableCount_);
  std::unordered_set<std::uint32_t> DestinationTypeParameters;
  std::unordered_set<std::uint32_t> DestinationEffectVariables;

  for (const auto &Pair : BinderPairs) {
    if (auto Valid = validateBinderIdentityShape(Pair.Source); !Valid)
      return std::unexpected(Valid.error());
    if (auto Valid = validateBinderIdentityShape(Pair.Destination); !Valid)
      return std::unexpected(Valid.error());
    if (!binderIdentitiesCorrespond(Pair.Source, Pair.Destination))
      return std::unexpected("binder owners do not correspond");
    if (Pair.Source.TypeParameters.size() !=
            Pair.Destination.TypeParameters.size() ||
        Pair.Source.EffectVariables.size() !=
            Pair.Destination.EffectVariables.size())
      return std::unexpected("binder declaration arity mismatch");

    if (const auto *SourceInstance =
            std::get_if<InstanceBinderOwner>(&Pair.Source.Identity)) {
      const auto &DestinationInstance =
          std::get<InstanceBinderOwner>(Pair.Destination.Identity);
      for (const auto Argument : SourceInstance->Arguments)
        if (!valid(Argument))
          return std::unexpected(
              "source instance owner contains a foreign type ID");
      for (const auto Argument : DestinationInstance.Arguments)
        if (!Destination.valid(Argument))
          return std::unexpected(
              "destination instance owner contains a foreign type ID");
      for (const auto &Constraint : SourceInstance->Constraints) {
        if (!valid(Constraint.Parameter))
          return std::unexpected(
              "source instance owner contains a foreign type parameter");
        for (const auto Argument : Constraint.Arguments)
          if (!valid(Argument))
            return std::unexpected(
                "source instance constraint contains a foreign type ID");
      }
      for (const auto &Constraint : DestinationInstance.Constraints) {
        if (!Destination.valid(Constraint.Parameter))
          return std::unexpected(
              "destination instance owner contains a foreign type parameter");
        for (const auto Argument : Constraint.Arguments)
          if (!Destination.valid(Argument))
            return std::unexpected(
                "destination instance constraint contains a foreign type ID");
      }
    }
    for (std::size_t Index = 0; Index < Pair.Source.TypeParameters.size();
         ++Index) {
      const auto SourceId = Pair.Source.TypeParameters[Index];
      const auto DestinationId = Pair.Destination.TypeParameters[Index];
      if (!valid(SourceId) || !Destination.valid(DestinationId))
        return std::unexpected("binder declaration contains a foreign type "
                               "parameter");
      if (Remap.TypeParameters[SourceId.value()].isValid() ||
          !DestinationTypeParameters.insert(DestinationId.value()).second)
        return std::unexpected("duplicate or conflicting type binder seed");
      Remap.TypeParameters[SourceId.value()] = DestinationId;
    }
    for (std::size_t Index = 0; Index < Pair.Source.EffectVariables.size();
         ++Index) {
      const auto &SourceVariable = Pair.Source.EffectVariables[Index];
      const auto &DestinationVariable = Pair.Destination.EffectVariables[Index];
      if (SourceVariable.Kind != DestinationVariable.Kind)
        return std::unexpected("effect binder kind mismatch");
      if (!valid(SourceVariable.Variable) ||
          !Destination.valid(DestinationVariable.Variable))
        return std::unexpected("binder declaration contains a foreign effect "
                               "variable");
      if (Remap.EffectVariables[SourceVariable.Variable.value()].isValid() ||
          !DestinationEffectVariables
               .insert(DestinationVariable.Variable.value())
               .second)
        return std::unexpected("duplicate or conflicting effect binder seed");
      Remap.EffectVariables[SourceVariable.Variable.value()] =
          DestinationVariable.Variable;
    }
  }

  auto mapTypeParameter = [&](TypeParameterId Source)
      -> std::expected<TypeParameterId, std::string> {
    if (!valid(Source) || !Remap.TypeParameters[Source.value()].isValid())
      return std::unexpected("encountered an unseeded type binder");
    return Remap.TypeParameters[Source.value()];
  };
  auto mapEffectVariable = [&](EffectVariableId Source)
      -> std::expected<EffectVariableId, std::string> {
    if (!valid(Source) || !Remap.EffectVariables[Source.value()].isValid())
      return std::unexpected("encountered an unseeded effect binder");
    return Remap.EffectVariables[Source.value()];
  };

  auto Transaction = Destination.beginAppendTransaction();
  std::vector<std::uint8_t> TypeState(typeCount());
  std::vector<std::uint8_t> EffectState(effectCount());
  std::string Failure;
  std::function<bool(TypeId)> copyType;
  std::function<bool(EffectRowId)> copyEffect;
  auto copyApplication = [&](EffectOperationApplication &Application) {
    for (auto &Argument : Application.TypeArguments) {
      if (!copyType(Argument))
        return false;
      Argument = Remap.Types[Argument.value()];
    }
    for (auto &Argument : Application.EffectArguments) {
      if (!copyEffect(Argument))
        return false;
      Argument = Remap.Effects[Argument.value()];
    }
    return true;
  };

  copyType = [&](TypeId SourceId) -> bool {
    if (!valid(SourceId)) {
      Failure = "source graph contains a foreign type ID";
      return false;
    }
    if (TypeState[SourceId.value()] == 2)
      return true;
    if (TypeState[SourceId.value()] == 1) {
      Failure = "source graph contains a type/effect cycle";
      return false;
    }
    TypeState[SourceId.value()] = 1;
    const auto Provisional = Transaction.reserveType();
    if (!Provisional.isValid()) {
      Failure = "destination could not reserve a type";
      return false;
    }
    Remap.Types[SourceId.value()] = Provisional;
    auto Value = at(SourceId);
    const auto Success = std::visit(
        Overloaded{[](PrimitiveType) { return true; },
                   [&](TypeParameterType &Type) {
                     auto Mapped = mapTypeParameter(Type.Parameter);
                     if (!Mapped) {
                       Failure = Mapped.error();
                       return false;
                     }
                     Type.Parameter = *Mapped;
                     return true;
                   },
                   [&](TupleType &Type) {
                     for (auto &Element : Type.Elements) {
                       const auto Original = Element;
                       if (!copyType(Original))
                         return false;
                       Element = Remap.Types[Original.value()];
                     }
                     return true;
                   },
                   [&](SequenceType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [&](SetType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [&](DictionaryType &Type) {
                     const auto Key = Type.Key;
                     const auto ValueId = Type.Value;
                     if (!copyType(Key) || !copyType(ValueId))
                       return false;
                     Type.Key = Remap.Types[Key.value()];
                     Type.Value = Remap.Types[ValueId.value()];
                     return true;
                   },
                   [&](RecordType &Type) {
                     for (auto &Field : Type.Fields) {
                       const auto Original = Field.Type;
                       if (!copyType(Original))
                         return false;
                       Field.Type = Remap.Types[Original.value()];
                     }
                     if (Type.RowRest) {
                       const auto Original = *Type.RowRest;
                       if (!copyType(Original))
                         return false;
                       Type.RowRest = Remap.Types[Original.value()];
                     }
                     return true;
                   },
                   [&](NominalType &Type) {
                     for (auto &Argument : Type.Arguments) {
                       const auto Original = Argument;
                       if (!copyType(Original))
                         return false;
                       Argument = Remap.Types[Original.value()];
                     }
                     return true;
                   },
                   [&](ArrayType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [&](ChannelType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [&](PromiseType &Type) {
                     const auto Element = Type.Element;
                     const auto Effects = Type.Effects;
                     if (!copyType(Element) || !copyEffect(Effects))
                       return false;
                     Type.Element = Remap.Types[Element.value()];
                     Type.Effects = Remap.Effects[Effects.value()];
                     return true;
                   },
                   [&](CursorType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [&](CursorStepType &Type) {
                     const auto Original = Type.Element;
                     if (!copyType(Original))
                       return false;
                     Type.Element = Remap.Types[Original.value()];
                     return true;
                   },
                   [](OutcomeTaskGroupType &) { return true; },
                   [&](ResourceType &Type) {
                     for (auto &Argument : Type.Arguments) {
                       const auto Original = Argument;
                       if (!copyType(Original))
                         return false;
                       Argument = Remap.Types[Original.value()];
                     }
                     return true;
                   },
                   [](AbiOpaqueType &) { return true; },
                   [&](OwnedSlotStateType &Type) {
                     for (auto &Field : Type.Fields) {
                       const auto Original = Field.Type;
                       if (!copyType(Original))
                         return false;
                       Field.Type = Remap.Types[Original.value()];
                     }
                     return true;
                   },
                   [&](FunctionType &Type) {
                     for (auto &Parameter : Type.TypeParameters) {
                       auto Mapped = mapTypeParameter(Parameter);
                       if (!Mapped) {
                         Failure = Mapped.error();
                         return false;
                       }
                       Parameter = *Mapped;
                     }
                     for (auto &Variable : Type.EffectVariables) {
                       auto Mapped = mapEffectVariable(Variable.Variable);
                       if (!Mapped) {
                         Failure = Mapped.error();
                         return false;
                       }
                       Variable.Variable = *Mapped;
                     }
                     for (auto &Constraint : Type.Constraints) {
                       auto Mapped = mapTypeParameter(Constraint.Parameter);
                       if (!Mapped) {
                         Failure = Mapped.error();
                         return false;
                       }
                       Constraint.Parameter = *Mapped;
                       for (auto &Argument : Constraint.Arguments) {
                         const auto Original = Argument;
                         if (!copyType(Original))
                           return false;
                         Argument = Remap.Types[Original.value()];
                       }
                     }
                     for (auto &Parameter : Type.Parameters) {
                       const auto Original = Parameter.Type;
                       if (!copyType(Original))
                         return false;
                       Parameter.Type = Remap.Types[Original.value()];
                     }
                     const auto Result = Type.Result;
                     const auto Effects = Type.Effects;
                     if (!copyType(Result) || !copyEffect(Effects))
                       return false;
                     Type.Result = Remap.Types[Result.value()];
                     Type.Effects = Remap.Effects[Effects.value()];
                     return true;
                   },
                   [&](SumType &Type) {
                     for (auto &Alternative : Type.Alternatives) {
                       const auto Original = Alternative;
                       if (!copyType(Original))
                         return false;
                       Alternative = Remap.Types[Original.value()];
                     }
                     return true;
                   }},
        Value);
    if (!Success)
      return false;
    if (auto Defined = Transaction.define(Provisional, std::move(Value));
        !Defined) {
      Failure = Defined.error();
      return false;
    }
    TypeState[SourceId.value()] = 2;
    return true;
  };

  copyEffect = [&](EffectRowId SourceId) -> bool {
    if (!valid(SourceId)) {
      Failure = "source graph contains a foreign effect ID";
      return false;
    }
    if (EffectState[SourceId.value()] == 2)
      return true;
    if (EffectState[SourceId.value()] == 1) {
      Failure = "source graph contains a type/effect cycle";
      return false;
    }
    EffectState[SourceId.value()] = 1;
    const auto Provisional = Transaction.reserveEffect();
    if (!Provisional.isValid()) {
      Failure = "destination could not reserve an effect";
      return false;
    }
    Remap.Effects[SourceId.value()] = Provisional;
    auto Row = effect(SourceId);
    for (auto &Application : Row.Operations)
      if (!copyApplication(Application))
        return false;
    for (auto &Tail : Row.Tails) {
      auto Mapped = mapEffectVariable(Tail.Variable);
      if (!Mapped) {
        Failure = Mapped.error();
        return false;
      }
      Tail.Variable = *Mapped;
      for (auto &Application : Tail.ExcludedOperations)
        if (!copyApplication(Application))
          return false;
    }
    if (auto Defined = Transaction.defineEffect(Provisional, std::move(Row));
        !Defined) {
      Failure = Defined.error();
      return false;
    }
    EffectState[SourceId.value()] = 2;
    return true;
  };

  for (std::uint32_t Index = 0; Index < typeCount(); ++Index)
    if (!copyType(TypeId{Domain_, Index}))
      return std::unexpected(Failure);
  for (std::uint32_t Index = 0; Index < effectCount(); ++Index)
    if (!copyEffect(EffectRowId{Domain_, Index}))
      return std::unexpected(Failure);
  if (auto Prepared = Transaction.prepareCommit(); !Prepared)
    return std::unexpected(Prepared.error());
  for (auto &Type : Remap.Types) {
    auto Resolved = Transaction.resolved(Type);
    if (!Resolved)
      return std::unexpected(Resolved.error());
    Type = *Resolved;
  }
  for (auto &Effect : Remap.Effects) {
    auto Resolved = Transaction.resolved(Effect);
    if (!Resolved)
      return std::unexpected(Resolved.error());
    Effect = *Resolved;
  }

  for (const auto &Pair : BinderPairs) {
    const auto *SourceInstance =
        std::get_if<InstanceBinderOwner>(&Pair.Source.Identity);
    if (!SourceInstance)
      continue;
    const auto &DestinationInstance =
        std::get<InstanceBinderOwner>(Pair.Destination.Identity);
    for (std::size_t Index = 0; Index < SourceInstance->Arguments.size();
         ++Index) {
      if (Remap.Types[SourceInstance->Arguments[Index].value()] !=
          DestinationInstance.Arguments[Index])
        return std::unexpected("instance binder owners do not correspond");
    }

    auto SourceConstraints = SourceInstance->Constraints;
    for (auto &Constraint : SourceConstraints) {
      Constraint.Parameter = Remap.TypeParameters[Constraint.Parameter.value()];
      if (!Constraint.Parameter.isValid())
        return std::unexpected(
            "instance owner contains an unseeded type binder");
      for (auto &Argument : Constraint.Arguments)
        Argument = Remap.Types[Argument.value()];
    }
    auto DestinationConstraints = DestinationInstance.Constraints;
    sortConstraints(SourceConstraints);
    sortConstraints(DestinationConstraints);
    if (SourceConstraints != DestinationConstraints)
      return std::unexpected("instance binder owners do not correspond");
  }
  Transaction.commitPrepared();
  return Remap;
}

std::expected<ResultOwnership, std::string>
resultOwnershipFor(const TypeTable &Types, TypeId Type) {
  if (!Types.valid(Type))
    return std::unexpected("result type does not belong to this table");
  const auto Ownership = classifyResultOwnership(Types.at(Type));
  if (!Ownership)
    return std::unexpected(Ownership.error());
  if (!*Ownership)
    return std::unexpected("open type parameter has no fixed result ownership");
  return **Ownership;
}

} // namespace yona::model
