#include "Support/StructuralTypeTestAccess.h"
#include "yona/Model/CanonicalEncoding.h"
#include "yona/Model/StructuralType.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

using yona::model::BinderOwnerKind;
using yona::model::CallingConvention;
using yona::model::CanonicalBinderDeclaration;
using yona::model::CanonicalEncodingErrorCode;
using yona::model::CanonicalRoot;
using yona::model::ClosedEffectOperation;
using yona::model::EffectOperationApplication;
using yona::model::EffectOperationKey;
using yona::model::EffectRow;
using yona::model::EffectTailProjection;
using yona::model::EffectVariableDeclaration;
using yona::model::EffectVariableKind;
using yona::model::FunctionType;
using yona::model::ModuleIdentity;
using yona::model::NominalTypeKey;
using yona::model::ParameterOwnership;
using yona::model::PrimitiveType;
using yona::model::ResultOwnership;
using yona::model::SymbolIdentity;
using yona::model::TypeParameterType;
using yona::model::TypeTable;
using yona::model::test::StructuralTypeTestAccess;

NominalTypeKey key(std::string Module, std::string Name) {
  return NominalTypeKey{ModuleIdentity{Module}, std::move(Name)};
}

CanonicalBinderDeclaration
functionBinder(std::string Name,
               std::vector<yona::model::TypeParameterId> TypeParameters,
               std::vector<EffectVariableDeclaration> EffectVariables) {
  return CanonicalBinderDeclaration{
      BinderOwnerKind::Function,
      yona::model::FunctionDeclarationIdentity{
          SymbolIdentity{ModuleIdentity{"Main"}, std::move(Name)}},
      std::move(TypeParameters), std::move(EffectVariables)};
}

std::vector<std::byte> bytes(std::string_view Hex) {
  const auto nibble = [](char Character) -> std::uint8_t {
    if (Character >= '0' && Character <= '9')
      return static_cast<std::uint8_t>(Character - '0');
    if (Character >= 'a' && Character <= 'f')
      return static_cast<std::uint8_t>(Character - 'a' + 10);
    throw std::invalid_argument("invalid hexadecimal fixture");
  };
  if (Hex.size() % 2 != 0)
    throw std::invalid_argument("odd hexadecimal fixture");
  std::vector<std::byte> Result;
  Result.reserve(Hex.size() / 2);
  for (std::size_t Index = 0; Index < Hex.size(); Index += 2)
    Result.push_back(static_cast<std::byte>((nibble(Hex[Index]) << 4u) |
                                            nibble(Hex[Index + 1])));
  return Result;
}

std::string hex(std::span<const std::byte> Bytes) {
  constexpr std::string_view Digits = "0123456789abcdef";
  std::string Result;
  Result.reserve(Bytes.size() * 2);
  for (const auto Byte : Bytes) {
    const auto Value = std::to_integer<std::uint8_t>(Byte);
    Result.push_back(Digits[Value >> 4u]);
    Result.push_back(Digits[Value & 0x0fu]);
  }
  return Result;
}

std::uint32_t readU32(std::span<const std::byte> Bytes, std::size_t Offset) {
  std::uint32_t Result = 0;
  for (unsigned Index = 0; Index != 4; ++Index)
    Result |= std::to_integer<std::uint32_t>(Bytes[Offset + Index])
              << (Index * 8u);
  return Result;
}

void writeU32(std::vector<std::byte> &Bytes, std::size_t Offset,
              std::uint32_t Value) {
  for (unsigned Index = 0; Index != 4; ++Index)
    Bytes[Offset + Index] =
        static_cast<std::byte>((Value >> (Index * 8u)) & 0xffu);
}

struct ClosedEncodingFixture {
  TypeTable Types;
  yona::model::TypeId Root;
};

ClosedEncodingFixture closedEncodingFixture(bool ReverseInsertion) {
  ClosedEncodingFixture Fixture;
  std::expected<yona::model::TypeId, std::string> Int;
  std::expected<yona::model::TypeId, std::string> String;
  if (ReverseInsertion) {
    String = Fixture.Types.intern(PrimitiveType::String);
    Int = Fixture.Types.intern(PrimitiveType::Int);
  } else {
    Int = Fixture.Types.intern(PrimitiveType::Int);
    String = Fixture.Types.intern(PrimitiveType::String);
  }
  if (!Int || !String)
    throw std::runtime_error("unable to construct canonical fixture");
  const auto EmptyEffects = Fixture.Types.internEffect({});
  const auto RecursiveNominal = Fixture.Types.intern(
      yona::model::NominalType{key("Main", "Tree"), {*Int}});
  if (!EmptyEffects || !RecursiveNominal)
    throw std::runtime_error("unable to construct canonical fixture");
  const auto Effects = Fixture.Types.internEffect(
      EffectRow{{EffectOperationApplication{{key("Std", "Console"), "write"},
                                            {*RecursiveNominal},
                                            {*EmptyEffects}}},
                {},
                true,
                false});
  if (!Effects)
    throw std::runtime_error("unable to construct canonical fixture");
  const auto Root = Fixture.Types.intern(
      FunctionType{{},
                   {},
                   {},
                   {{*RecursiveNominal, ParameterOwnership::Consume}},
                   *String,
                   ResultOwnership::Owned,
                   *Effects,
                   CallingConvention::DirectYona,
                   yona::model::FunctionDeclarationIdentity{
                       SymbolIdentity{ModuleIdentity{"Main"}, "render"}},
                   std::nullopt});
  if (!Root)
    throw std::runtime_error("unable to construct canonical fixture");
  Fixture.Root = *Root;
  return Fixture;
}

struct OpenOperationFixture {
  TypeTable Types;
  yona::model::TypeId OpenType;
  yona::model::EffectOperationSignature Signature;
  CanonicalBinderDeclaration Binder;
};

OpenOperationFixture openOperationFixture(std::uint32_t Noise) {
  TypeTable Types;
  for (std::uint32_t Index = 0; Index != Noise; ++Index) {
    static_cast<void>(Types.allocateTypeParameter());
    static_cast<void>(Types.allocateEffectVariable());
  }
  const auto Parameter = Types.allocateTypeParameter();
  const auto PhantomParameter = Types.allocateTypeParameter();
  const auto Effect = Types.allocateEffectVariable();
  const auto PhantomEffect = Types.allocateEffectVariable();
  const auto Open = Types.intern(TypeParameterType{Parameter});
  if (!Open)
    throw std::runtime_error("unable to construct open operation fixture");
  const EffectOperationKey Operation{key("Main", "State"), "update"};
  const auto Row = Types.internEffect(EffectRow{
      {},
      {{Effect, EffectVariableKind::Flexible, {{Operation, {*Open}, {}}}}},
      false,
      false});
  if (!Row)
    throw std::runtime_error("unable to construct open operation fixture");
  return {std::move(Types),
          *Open,
          {Operation,
           {Parameter, PhantomParameter},
           {{Effect, EffectVariableKind::Flexible},
            {PhantomEffect, EffectVariableKind::Flexible}},
           {{Parameter, key("Std", "Eq"), {*Open}}},
           {{*Open, ParameterOwnership::Borrow}},
           *Open,
           ResultOwnership::Owned,
           *Row},
          {BinderOwnerKind::Operation,
           Operation,
           {Parameter, PhantomParameter},
           {{Effect, EffectVariableKind::Flexible},
            {PhantomEffect, EffectVariableKind::Flexible}}}};
}

std::vector<CanonicalBinderDeclaration>
decodedCanonicalBinders(const yona::model::DecodedInterfaceTables &Decoded) {
  std::vector<CanonicalBinderDeclaration> Result;
  Result.reserve(Decoded.Binders.size());
  for (const auto &Binder : Decoded.Binders) {
    std::optional<yona::model::CanonicalBinderOwnerIdentity> Identity;
    if (const auto *Nominal = std::get_if<NominalTypeKey>(&Binder.Identity))
      Identity.emplace(*Nominal);
    else if (const auto *Function =
                 std::get_if<yona::model::FunctionDeclarationIdentity>(
                     &Binder.Identity))
      Identity.emplace(*Function);
    else if (const auto *Operation =
                 std::get_if<EffectOperationKey>(&Binder.Identity))
      Identity.emplace(*Operation);
    else
      throw std::invalid_argument(
          "instance binders require their schema identity to re-encode");
    Result.push_back({Binder.Kind, std::move(*Identity), Binder.TypeParameters,
                      Binder.EffectVariables});
  }
  return Result;
}

} // namespace

TEST_CASE("Structural type interning is canonical and nominal") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());
  CHECK(Types.intern(PrimitiveType::Int) == Int);

  const auto LocalOption =
      Types.intern(yona::model::NominalType{key("Main", "Option"), {*Int}});
  REQUIRE(LocalOption.has_value());
  const auto OtherOption =
      Types.intern(yona::model::NominalType{key("Other", "Option"), {*Int}});
  REQUIRE(OtherOption.has_value());
  CHECK(*LocalOption != *OtherOption);
  CHECK(Types.format(*LocalOption) == "Main\\Option Int");
}

TEST_CASE("Structural type record fields have deterministic identity") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto Bool = Types.intern(PrimitiveType::Bool);
  REQUIRE(Int.has_value());
  REQUIRE(Bool.has_value());
  const auto Left = Types.intern(
      yona::model::RecordType{{{"z", *Bool}, {"a", *Int}}, std::nullopt});
  const auto Right = Types.intern(
      yona::model::RecordType{{{"a", *Int}, {"z", *Bool}}, std::nullopt});
  REQUIRE(Left.has_value());
  REQUIRE(Right.has_value());
  CHECK(*Left == *Right);

  const auto &Record = std::get<yona::model::RecordType>(Types.at(*Left));
  REQUIRE(Record.Fields.size() == 2);
  CHECK(Record.Fields[0].Name == "a");
  CHECK(Record.Fields[1].Name == "z");
}

TEST_CASE("Structural type IDs are domain qualified and storage is stable") {
  TypeTable Left;
  TypeTable Right;
  const auto LeftInt = Left.intern(PrimitiveType::Int);
  const auto RightInt = Right.intern(PrimitiveType::Int);
  REQUIRE(LeftInt.has_value());
  REQUIRE(RightInt.has_value());

  CHECK(LeftInt->value() == 0);
  CHECK(RightInt->value() == 0);
  CHECK(LeftInt->domain() != RightInt->domain());
  CHECK_FALSE(Left.valid(*RightInt));
  CHECK_THROWS_AS(static_cast<void>(Left.at(*RightInt)), std::out_of_range);

  const auto *Stable = &Left.at(*LeftInt);
  for (std::uint32_t Index = 0; Index != 512; ++Index) {
    const auto Added = Left.intern(yona::model::NominalType{
        key("Main", "N" + std::to_string(Index)), {*LeftInt}});
    REQUIRE(Added.has_value());
  }
  CHECK(&Left.at(*LeftInt) == Stable);

  const auto Domain = Left.idDomain();
  TypeTable Moved = std::move(Left);
  CHECK(Moved.idDomain() == Domain);
  CHECK(&Moved.at(*LeftInt) == Stable);
}

TEST_CASE("Structural type identities use ordered hashable module values") {
  const ModuleIdentity First("Main");
  const ModuleIdentity Same("Main");
  const ModuleIdentity Later("Std\\Net");

  CHECK(First == Same);
  CHECK(First < Later);
  std::unordered_set<ModuleIdentity, yona::model::ModuleIdentityHasher>
      Identities;
  Identities.insert(First);
  Identities.insert(Same);
  Identities.insert(Later);
  CHECK(Identities.size() == 2);
}

TEST_CASE("Structural type append transaction publishes a forward DAG only "
          "after prepare") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());

  auto Transaction = Types.beginAppendTransaction();
  const auto Root = Transaction.reserveType();
  const auto Payload = Transaction.reserveType();
  const auto Effects = Transaction.reserveEffect();
  const auto Parameter = Transaction.allocateTypeParameter();
  const auto EffectVariable = Transaction.allocateEffectVariable();

  REQUIRE(
      Transaction
          .define(Root,
                  FunctionType{{Parameter},
                               {{EffectVariable, EffectVariableKind::Flexible}},
                               {},
                               {{Payload, ParameterOwnership::Borrow}},
                               *Int,
                               ResultOwnership::Trivial,
                               Effects,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt})
          .has_value());
  REQUIRE(
      Transaction
          .defineEffect(
              Effects,
              EffectRow{{EffectOperationApplication{
                            {key("Main", "Console"), "write"}, {Payload}, {}}},
                        {{EffectVariable, EffectVariableKind::Flexible, {}}},
                        false,
                        false})
          .has_value());
  REQUIRE(
      Transaction.define(Payload, yona::model::SequenceType{*Int}).has_value());

  CHECK_FALSE(Types.valid(Root));
  CHECK_FALSE(Types.valid(Payload));
  CHECK_FALSE(Types.valid(Effects));
  CHECK_FALSE(Transaction.resolved(Root).has_value());
  CHECK_THROWS_AS(static_cast<void>(Types.at(Root)), std::out_of_range);

  REQUIRE(Transaction.prepareCommit().has_value());
  const auto FinalRoot = Transaction.resolved(Root);
  const auto FinalPayload = Transaction.resolved(Payload);
  const auto FinalEffects = Transaction.resolved(Effects);
  REQUIRE(FinalRoot.has_value());
  REQUIRE(FinalPayload.has_value());
  REQUIRE(FinalEffects.has_value());
  CHECK_FALSE(Types.valid(*FinalRoot));

  Transaction.commitPrepared();
  CHECK(Types.valid(*FinalRoot));
  CHECK(Types.valid(*FinalPayload));
  CHECK(Types.valid(*FinalEffects));
  REQUIRE(Types.validate().has_value());

  const auto &Function = std::get<FunctionType>(Types.at(*FinalRoot));
  REQUIRE(Function.Parameters.size() == 1);
  CHECK(Function.Parameters[0].Type == *FinalPayload);
  CHECK(Function.Effects == *FinalEffects);
  const auto &Row = Types.effect(*FinalEffects);
  REQUIRE(Row.Operations.size() == 1);
  CHECK(Row.Operations[0].TypeArguments ==
        std::vector<yona::model::TypeId>{*FinalPayload});
  CHECK(Transaction.resolved(Root) == FinalRoot);
}

TEST_CASE("Structural type append transaction rejects invalid graphs without "
          "mutation") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());
  const auto Before = StructuralTypeTestAccess::snapshot(Types);
  const auto *Stable = &Types.at(*Int);

  SUBCASE("undefined reservation") {
    auto Transaction = Types.beginAppendTransaction();
    static_cast<void>(Transaction.reserveType());
    CHECK_FALSE(Transaction.prepareCommit().has_value());
  }

  SUBCASE("multiply defined reservation") {
    auto Transaction = Types.beginAppendTransaction();
    const auto Reserved = Transaction.reserveType();
    REQUIRE(Transaction.define(Reserved, PrimitiveType::Bool).has_value());
    CHECK_FALSE(
        Transaction.define(Reserved, PrimitiveType::String).has_value());
    CHECK_FALSE(Transaction.prepareCommit().has_value());
  }

  SUBCASE("direct structural cycle") {
    auto Transaction = Types.beginAppendTransaction();
    const auto A = Transaction.reserveType();
    const auto B = Transaction.reserveType();
    REQUIRE(Transaction.define(A, yona::model::SequenceType{B}).has_value());
    REQUIRE(Transaction.define(B, yona::model::TupleType{{A}}).has_value());
    CHECK_FALSE(Transaction.prepareCommit().has_value());
  }

  SUBCASE("type to effect to type cycle") {
    auto Transaction = Types.beginAppendTransaction();
    const auto A = Transaction.reserveType();
    const auto E = Transaction.reserveEffect();
    REQUIRE(
        Transaction
            .define(A,
                    yona::model::PromiseType{*Int, ResultOwnership::Trivial, E})
            .has_value());
    REQUIRE(
        Transaction
            .defineEffect(E, EffectRow{{EffectOperationApplication{
                                           {key("Main", "E"), "op"}, {A}, {}}},
                                       {},
                                       false,
                                       true})
            .has_value());
    CHECK_FALSE(Transaction.prepareCommit().has_value());
  }

  SUBCASE("foreign reference") {
    TypeTable Other;
    const auto Foreign = Other.intern(PrimitiveType::Int);
    REQUIRE(Foreign.has_value());
    auto Transaction = Types.beginAppendTransaction();
    const auto Reserved = Transaction.reserveType();
    REQUIRE(Transaction.define(Reserved, yona::model::SequenceType{*Foreign})
                .has_value());
    CHECK_FALSE(Transaction.prepareCommit().has_value());
  }

  CHECK(StructuralTypeTestAccess::snapshot(Types) == Before);
  CHECK(&Types.at(*Int) == Stable);
}

TEST_CASE("Structural type append transaction detects stale generation") {
  TypeTable Types;
  auto Transaction = Types.beginAppendTransaction();
  const auto Reserved = Transaction.reserveType();
  REQUIRE(Transaction.define(Reserved, PrimitiveType::Int).has_value());
  REQUIRE(Types.intern(PrimitiveType::Bool).has_value());
  const auto BeforePrepare = StructuralTypeTestAccess::snapshot(Types);
  CHECK_FALSE(Transaction.prepareCommit().has_value());
  CHECK(StructuralTypeTestAccess::snapshot(Types) == BeforePrepare);
}

TEST_CASE("Structural type prepared transaction excludes mixed mutation") {
  TypeTable Types;
  auto Prepared = Types.beginAppendTransaction();
  const auto Reserved = Prepared.reserveType();
  REQUIRE(Prepared.define(Reserved, PrimitiveType::Int).has_value());
  REQUIRE(Prepared.prepareCommit().has_value());

  CHECK_FALSE(Types.intern(PrimitiveType::Bool).has_value());
  CHECK_FALSE(Types.allocateTypeParameter().isValid());
  Prepared.commitPrepared();
  CHECK(Types.intern(PrimitiveType::Bool).has_value());

  {
    auto Discarded = Types.beginAppendTransaction();
    const auto String = Discarded.reserveType();
    REQUIRE(Discarded.define(String, PrimitiveType::String).has_value());
    REQUIRE(Discarded.prepareCommit().has_value());
  }
  CHECK(Types.intern(PrimitiveType::String).has_value());
}

TEST_CASE("Structural type move preserves an in-flight prepared transaction") {
  TypeTable Source;
  auto Transaction = Source.beginAppendTransaction();
  const auto Reserved = Transaction.reserveType();
  REQUIRE(Transaction.define(Reserved, PrimitiveType::Int).has_value());
  REQUIRE(Transaction.prepareCommit().has_value());
  const auto Final = Transaction.resolved(Reserved);
  REQUIRE(Final.has_value());

  TypeTable Destination = std::move(Source);
  Transaction.commitPrepared();
  CHECK(Destination.valid(*Final));
  CHECK(std::get<PrimitiveType>(Destination.at(*Final)) == PrimitiveType::Int);
  CHECK(Destination.intern(PrimitiveType::Bool).has_value());
}

TEST_CASE("Structural type move retargets an unprepared transaction") {
  TypeTable Source;
  auto Transaction = Source.beginAppendTransaction();
  const auto Reserved = Transaction.reserveType();
  REQUIRE(Transaction.define(Reserved, PrimitiveType::Int).has_value());

  TypeTable Destination;
  Destination = std::move(Source);
  REQUIRE(Transaction.prepareCommit().has_value());
  const auto Final = Transaction.resolved(Reserved);
  REQUIRE(Final.has_value());
  Transaction.commitPrepared();
  CHECK(Destination.valid(*Final));
  CHECK(std::get<PrimitiveType>(Destination.at(*Final)) == PrimitiveType::Int);
}

TEST_CASE("Structural type sum normalization is associative commutative and "
          "idempotent") {
  TypeTable Types;
  const auto String = Types.intern(PrimitiveType::String);
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(String.has_value());
  REQUIRE(Int.has_value());

  const std::array LeftAlternatives{*Int, *String};
  const std::array RightAlternatives{*String, *Int, *String};
  const auto Left = Types.internSum(LeftAlternatives);
  const auto Right =
      Types.intern(yona::model::SumType{std::vector<yona::model::TypeId>(
          RightAlternatives.begin(), RightAlternatives.end())});
  REQUIRE(Left.has_value());
  REQUIRE(Right.has_value());
  CHECK(*Left == *Right);

  const std::array NestedAlternatives{*Right, *Int};
  const auto Nested = Types.internSum(NestedAlternatives);
  REQUIRE(Nested.has_value());
  CHECK(*Nested == *Left);

  const std::array SingletonAlternatives{*Int, *Int};
  const auto Singleton = Types.internSum(SingletonAlternatives);
  REQUIRE(Singleton.has_value());
  CHECK(*Singleton == *Int);
  CHECK_FALSE(
      Types.internSum(std::span<const yona::model::TypeId>{}).has_value());
  CHECK(Types.format(*Left) == "Int | String");
}

TEST_CASE("Structural type record rows flatten and reject invalid overlap") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto Bool = Types.intern(PrimitiveType::Bool);
  REQUIRE(Int.has_value());
  REQUIRE(Bool.has_value());
  const auto Rest =
      Types.intern(yona::model::RecordType{{{"b", *Bool}}, std::nullopt});
  REQUIRE(Rest.has_value());
  const auto Open = Types.intern(yona::model::RecordType{{{"a", *Int}}, *Rest});
  REQUIRE(Open.has_value());
  const auto &Flattened = std::get<yona::model::RecordType>(Types.at(*Open));
  REQUIRE(Flattened.Fields.size() == 2);
  CHECK(Flattened.Fields[0].Name == "a");
  CHECK(Flattened.Fields[1].Name == "b");
  CHECK_FALSE(Flattened.RowRest.has_value());

  CHECK_FALSE(
      Types.intern(yona::model::RecordType{{{"a", *Bool}}, *Open}).has_value());
  CHECK_FALSE(
      Types.intern(yona::model::RecordType{{{"a", *Bool}}, *Int}).has_value());
}

TEST_CASE(
    "Structural type promises require cancellable rows and legal payloads") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto String = Types.intern(PrimitiveType::String);
  const auto Group = Types.intern(yona::model::OutcomeTaskGroupType{});
  const auto Opaque = Types.intern(
      yona::model::AbiOpaqueType{yona::model::AbiOpaqueKind::ControlOutcome});
  const auto Synchronous = Types.internEffect({});
  const auto Asynchronous = Types.internEffect(EffectRow{{}, {}, false, true});
  REQUIRE(Int.has_value());
  REQUIRE(String.has_value());
  REQUIRE(Group.has_value());
  REQUIRE(Opaque.has_value());
  REQUIRE(Synchronous.has_value());
  REQUIRE(Asynchronous.has_value());

  CHECK_FALSE(Types
                  .intern(yona::model::PromiseType{
                      *Int, ResultOwnership::Trivial, *Synchronous})
                  .has_value());
  CHECK_FALSE(Types
                  .intern(yona::model::PromiseType{
                      *Group, ResultOwnership::Owned, *Asynchronous})
                  .has_value());
  CHECK(Types
            .intern(yona::model::PromiseType{*Int, ResultOwnership::Trivial,
                                             *Asynchronous})
            .has_value());

  CHECK_FALSE(Types
                  .intern(yona::model::PromiseType{*Int, ResultOwnership::Owned,
                                                   *Asynchronous})
                  .has_value());
  CHECK_FALSE(
      Types.intern(yona::model::CursorType{*Int, ResultOwnership::Owned})
          .has_value());
  CHECK_FALSE(Types
                  .intern(yona::model::CursorStepType{*String,
                                                      ResultOwnership::Trivial})
                  .has_value());

  const auto EmptyEffects = Types.internEffect({});
  REQUIRE(EmptyEffects.has_value());
  auto Function = [&](yona::model::TypeId Result, ResultOwnership Contract) {
    return Types.intern(FunctionType{{},
                                     {},
                                     {},
                                     {},
                                     Result,
                                     Contract,
                                     *EmptyEffects,
                                     CallingConvention::DirectYona,
                                     std::nullopt,
                                     std::nullopt});
  };
  CHECK(Function(*Int, ResultOwnership::Trivial).has_value());
  CHECK_FALSE(Function(*Int, ResultOwnership::Owned).has_value());
  CHECK_FALSE(Function(*Opaque, ResultOwnership::Owned).has_value());
}

TEST_CASE("Structural type transaction fault and collision seams preserve "
          "atomicity") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());
  const auto Before = StructuralTypeTestAccess::snapshot(Types);
  const auto *Stable = &Types.at(*Int);

  {
    StructuralTypeTestAccess Failure({.FailAfter = 0,
                                      .ConstantHash = std::nullopt,
                                      .ConstantFingerprint = std::nullopt});
    CHECK_FALSE(Types.intern(PrimitiveType::Bool).has_value());
  }
  CHECK(StructuralTypeTestAccess::snapshot(Types) == Before);
  CHECK(&Types.at(*Int) == Stable);

  {
    StructuralTypeTestAccess FailureDuringPrepare(
        {.FailAfter = 1,
         .ConstantHash = std::nullopt,
         .ConstantFingerprint = std::nullopt});
    CHECK_FALSE(Types.intern(PrimitiveType::Bool).has_value());
  }
  CHECK(StructuralTypeTestAccess::snapshot(Types) == Before);
  CHECK(&Types.at(*Int) == Stable);

  {
    auto FailedTransaction = Types.beginAppendTransaction();
    REQUIRE(FailedTransaction.intern(PrimitiveType::Bool).has_value());
    REQUIRE(FailedTransaction.intern(PrimitiveType::String).has_value());
    {
      StructuralTypeTestAccess FailureAfterOneAllocation(
          {.FailAfter = 3,
           .ConstantHash = std::nullopt,
           .ConstantFingerprint = std::nullopt});
      CHECK_FALSE(FailedTransaction.prepareCommit().has_value());
    }
    CHECK_FALSE(FailedTransaction.prepareCommit().has_value());
  }
  CHECK(StructuralTypeTestAccess::snapshot(Types) == Before);

  {
    StructuralTypeTestAccess Collision({.FailAfter = std::nullopt,
                                        .ConstantHash = 7,
                                        .ConstantFingerprint = 11});
    const auto Bool = Types.intern(PrimitiveType::Bool);
    const auto String = Types.intern(PrimitiveType::String);
    REQUIRE(Bool.has_value());
    REQUIRE(String.has_value());
    CHECK(*Bool != *String);
    CHECK(Types.at(*Bool) != Types.at(*String));
  }
}

TEST_CASE("Structural type declaration catalog validates nominal and resource "
          "closure") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());
  const auto Option =
      Types.intern(yona::model::NominalType{key("Main", "Option"), {*Int}});
  const auto Sender =
      Types.intern(yona::model::ResourceType{key("Std", "Sender"), {*Int}});
  REQUIRE(Option.has_value());
  REQUIRE(Sender.has_value());

  yona::model::StructuralDeclarationCatalog Catalog;
  REQUIRE(Catalog.addNominal({key("Main", "Option"), 1}).has_value());
  const auto ResourceParameter = Types.allocateTypeParameter();
  REQUIRE(Catalog
              .addResource({key("Std", "Sender"),
                            {ResourceParameter},
                            yona::model::ResourceAbiKind::ChannelSender,
                            yona::model::ResourceShareability::AlwaysShareable,
                            std::string{"YonaRuntimeChannelTryRetain"},
                            "YonaRuntimeChannelRelease"})
              .has_value());
  REQUIRE(Catalog.validate(Types, std::array{*Option, *Sender}).has_value());
  CHECK(Catalog.nominalArity(key("Main", "Option")) == 1);
  REQUIRE(Catalog.resource(key("Std", "Sender")) != nullptr);
  const auto *StableResource = Catalog.resource(key("Std", "Sender"));
  REQUIRE(Catalog
              .addResource({key("Std", "Receiver"),
                            {},
                            yona::model::ResourceAbiKind::ChannelReceiver,
                            yona::model::ResourceShareability::AlwaysShareable,
                            std::string{"YonaRuntimeChannelTryRetain"},
                            "YonaRuntimeChannelRelease"})
              .has_value());
  CHECK(Catalog.resource(key("Std", "Sender")) == StableResource);

  SUBCASE("unknown declaration") {
    yona::model::StructuralDeclarationCatalog Empty;
    CHECK_FALSE(Empty.validate(Types).has_value());
  }

  SUBCASE("wrong nominal arity") {
    yona::model::StructuralDeclarationCatalog Wrong;
    REQUIRE(Wrong.addNominal({key("Main", "Option"), 2}).has_value());
    REQUIRE(
        Wrong
            .addResource({key("Std", "Sender"),
                          {ResourceParameter},
                          yona::model::ResourceAbiKind::ChannelSender,
                          yona::model::ResourceShareability::AlwaysShareable,
                          std::string{"YonaRuntimeChannelTryRetain"},
                          "YonaRuntimeChannelRelease"})
            .has_value());
    CHECK_FALSE(Wrong.validate(Types).has_value());
  }

  SUBCASE("conflicting declarations") {
    CHECK_FALSE(Catalog.addNominal({key("Main", "Option"), 2}).has_value());
    CHECK_FALSE(Catalog
                    .addResource({key("Std", "Sender"),
                                  {ResourceParameter},
                                  yona::model::ResourceAbiKind::GenericResource,
                                  yona::model::ResourceShareability::Linear,
                                  std::nullopt,
                                  "DifferentRelease"})
                    .has_value());
  }

  SUBCASE("invalid callback policy") {
    yona::model::StructuralDeclarationCatalog Invalid;
    CHECK_FALSE(Invalid
                    .addResource({key("Std", "Linear"),
                                  {},
                                  yona::model::ResourceAbiKind::GenericResource,
                                  yona::model::ResourceShareability::Linear,
                                  std::string{"retain"},
                                  "release"})
                    .has_value());
    CHECK_FALSE(Invalid
                    .addResource({key("Std", "Receiver"),
                                  {},
                                  yona::model::ResourceAbiKind::ChannelReceiver,
                                  yona::model::ResourceShareability::Linear,
                                  std::nullopt,
                                  "release"})
                    .has_value());
  }

  SUBCASE("resource policy is required only from runtime roots") {
    TypeTable ResourceTypes;
    const auto Payload = ResourceTypes.intern(PrimitiveType::Int);
    REQUIRE(Payload.has_value());
    const auto Unreachable = ResourceTypes.intern(
        yona::model::ResourceType{key("Main", "PrivateHandle"), {*Payload}});
    REQUIRE(Unreachable.has_value());
    yona::model::StructuralDeclarationCatalog NoResources;
    CHECK(NoResources.validate(ResourceTypes).has_value());
    CHECK_FALSE(NoResources.validate(ResourceTypes, std::array{*Unreachable})
                    .has_value());
  }
}

TEST_CASE("Structural type transaction deduplicates staged rows") {
  TypeTable Types;
  auto Transaction = Types.beginAppendTransaction();
  const auto Left = Transaction.intern(PrimitiveType::Int);
  const auto Right = Transaction.intern(PrimitiveType::Int);
  REQUIRE(Left.has_value());
  REQUIRE(Right.has_value());
  REQUIRE(Transaction.prepareCommit().has_value());
  const auto FinalLeft = Transaction.resolved(*Left);
  const auto FinalRight = Transaction.resolved(*Right);
  REQUIRE(FinalLeft.has_value());
  REQUIRE(FinalRight.has_value());
  CHECK(*FinalLeft == *FinalRight);
  Transaction.commitPrepared();
  CHECK(Types.typeCount() == 1);
}

TEST_CASE(
    "Structural type remapping is declaration seeded and preserves phantoms") {
  TypeTable Source;
  const auto SourceType = Source.allocateTypeParameter();
  const auto SourcePhantomType = Source.allocateTypeParameter();
  const auto SourceEffect = Source.allocateEffectVariable();
  const auto SourcePhantomEffect = Source.allocateEffectVariable();
  const auto OpenType = Source.intern(TypeParameterType{SourceType});
  const auto OpenEffect = Source.internEffect(EffectRow{
      {}, {{SourceEffect, EffectVariableKind::Flexible, {}}}, false, true});
  REQUIRE(OpenType.has_value());
  REQUIRE(OpenEffect.has_value());
  const auto OpenPromise = Source.intern(
      yona::model::PromiseType{*OpenType, ResultOwnership::Owned, *OpenEffect});
  REQUIRE(OpenPromise.has_value());

  TypeTable Destination;
  const auto DestinationType = Destination.allocateTypeParameter();
  const auto DestinationPhantomType = Destination.allocateTypeParameter();
  const auto DestinationEffect = Destination.allocateEffectVariable();
  const auto DestinationPhantomEffect = Destination.allocateEffectVariable();

  const auto SourceOwner =
      functionBinder("f", {SourceType, SourcePhantomType},
                     {{SourceEffect, EffectVariableKind::Flexible},
                      {SourcePhantomEffect, EffectVariableKind::Opaque}});
  const auto DestinationOwner =
      functionBinder("f", {DestinationType, DestinationPhantomType},
                     {{DestinationEffect, EffectVariableKind::Flexible},
                      {DestinationPhantomEffect, EffectVariableKind::Opaque}});
  const std::array Pairs{
      yona::model::BinderDeclarationPair{SourceOwner, DestinationOwner}};
  const auto Remap = Source.remapInto(Destination, Pairs);
  REQUIRE(Remap.has_value());
  REQUIRE(Remap->Types.size() == Source.typeCount());
  REQUIRE(Remap->Effects.size() == Source.effectCount());
  REQUIRE(Remap->TypeParameters.size() == 2);
  REQUIRE(Remap->EffectVariables.size() == 2);
  CHECK(Remap->TypeParameters[SourceType.value()] == DestinationType);
  CHECK(Remap->TypeParameters[SourcePhantomType.value()] ==
        DestinationPhantomType);
  CHECK(Remap->EffectVariables[SourceEffect.value()] == DestinationEffect);
  CHECK(Remap->EffectVariables[SourcePhantomEffect.value()] ==
        DestinationPhantomEffect);
  const auto &Promise = std::get<yona::model::PromiseType>(
      Destination.at(Remap->Types[OpenPromise->value()]));
  const auto &Parameter =
      std::get<TypeParameterType>(Destination.at(Promise.Element));
  CHECK(Parameter.Parameter == DestinationType);
  REQUIRE(Destination.effect(Promise.Effects).Tails.size() == 1);
  CHECK(Destination.effect(Promise.Effects).Tails[0].Variable ==
        DestinationEffect);

  TypeTable MissingDestination;
  const auto Before = StructuralTypeTestAccess::snapshot(MissingDestination);
  CHECK_FALSE(Source.remapInto(MissingDestination, {}).has_value());
  CHECK(StructuralTypeTestAccess::snapshot(MissingDestination) == Before);
}

TEST_CASE("Structural type remapping rejects a mismatched instance owner "
          "atomically") {
  TypeTable Source;
  const auto SourceInt = Source.intern(PrimitiveType::Int);
  REQUIRE(SourceInt.has_value());

  TypeTable Destination;
  const auto DestinationBool = Destination.intern(PrimitiveType::Bool);
  REQUIRE(DestinationBool.has_value());
  const auto Before = StructuralTypeTestAccess::snapshot(Destination);

  const auto Trait = key("Main", "Eq");
  const CanonicalBinderDeclaration SourceOwner{
      BinderOwnerKind::Instance,
      yona::model::InstanceBinderOwner{Trait, {*SourceInt}, {}},
      {},
      {}};
  const CanonicalBinderDeclaration DestinationOwner{
      BinderOwnerKind::Instance,
      yona::model::InstanceBinderOwner{Trait, {*DestinationBool}, {}},
      {},
      {}};
  const std::array Pairs{
      yona::model::BinderDeclarationPair{SourceOwner, DestinationOwner}};
  CHECK_FALSE(Source.remapInto(Destination, Pairs).has_value());
  CHECK(StructuralTypeTestAccess::snapshot(Destination) == Before);
}

TEST_CASE("Structural type rejects foreign effect and binder identities") {
  TypeTable Left;
  TypeTable Right;
  const auto ForeignParameter = Right.allocateTypeParameter();
  const auto ForeignEffectVariable = Right.allocateEffectVariable();
  const auto Int = Left.intern(PrimitiveType::Int);
  const auto ForeignInt = Right.intern(PrimitiveType::Int);
  const auto LeftEffects = Left.internEffect({});
  const auto ForeignEffects = Right.internEffect({});
  REQUIRE(Int.has_value());
  REQUIRE(ForeignInt.has_value());
  REQUIRE(LeftEffects.has_value());
  REQUIRE(ForeignEffects.has_value());

  CHECK(Int->value() == ForeignInt->value());
  CHECK(LeftEffects->value() == ForeignEffects->value());
  CHECK_THROWS_AS(static_cast<void>(Left.effect(*ForeignEffects)),
                  std::out_of_range);
  CHECK_FALSE(Left.intern(FunctionType{{},
                                       {},
                                       {},
                                       {},
                                       *Int,
                                       ResultOwnership::Trivial,
                                       *ForeignEffects,
                                       CallingConvention::DirectYona,
                                       std::nullopt,
                                       std::nullopt})
                  .has_value());

  auto Resolver = Left.beginAppendTransaction();
  const auto LocalType = Resolver.intern(PrimitiveType::Bool);
  REQUIRE(LocalType.has_value());
  REQUIRE(Resolver.prepareCommit().has_value());
  CHECK_FALSE(Resolver.resolved(*ForeignInt).has_value());
  CHECK_FALSE(Resolver.resolved(*ForeignEffects).has_value());
  Resolver.commitPrepared();

  CHECK_FALSE(Left.intern(TypeParameterType{ForeignParameter}).has_value());
  CHECK_FALSE(
      Left
          .internEffect(EffectRow{
              {}, {{ForeignEffectVariable, EffectVariableKind::Flexible, {}}}})
          .has_value());

  TypeTable Destination;
  const CanonicalBinderDeclaration InvalidName{
      BinderOwnerKind::Nominal, key("Main", "not-valid"), {}, {}};
  const std::array InvalidNamePair{
      yona::model::BinderDeclarationPair{InvalidName, InvalidName}};
  CHECK_FALSE(Left.remapInto(Destination, InvalidNamePair).has_value());

  const auto LeftEffect = Left.allocateEffectVariable();
  const auto DestinationEffect = Destination.allocateEffectVariable();
  const auto InvalidKind = static_cast<EffectVariableKind>(255);
  const auto LeftFunction =
      functionBinder("invalid", {}, {{LeftEffect, InvalidKind}});
  const auto DestinationFunction =
      functionBinder("invalid", {}, {{DestinationEffect, InvalidKind}});
  const std::array InvalidKindPair{
      yona::model::BinderDeclarationPair{LeftFunction, DestinationFunction}};
  CHECK_FALSE(Left.remapInto(Destination, InvalidKindPair).has_value());
}

TEST_CASE("Structural type canonical closed encoding has exact framing and "
          "fingerprint") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());

  const auto Encoded =
      yona::model::encodeClosedDescriptorGraph(Types, CanonicalRoot{*Int});
  REQUIRE(Encoded.has_value());
  const auto Expected = bytes("02000000" // encoding version
                              "00000000" // binder count
                              "01000000" // type count
                              "00"       // Primitive record tag
                              "0100000000000000"
                              "02"       // PrimitiveType::Int
                              "00000000" // effect count
                              "00000000" // operation count
                              "00"       // type root
                              "00000000");
  CHECK(*Encoded == Expected);
  CHECK(yona::model::canonicalStructuralFingerprint(*Encoded) ==
        UINT64_C(0xb7e0ec0ef8adc489));
}

TEST_CASE("Structural type canonical rooted encoding ignores raw insertion "
          "order") {
  auto Left = closedEncodingFixture(false);
  auto Right = closedEncodingFixture(true);
  const auto LeftBytes = yona::model::encodeClosedDescriptorGraph(
      Left.Types, CanonicalRoot{Left.Root});
  const auto RightBytes = yona::model::encodeClosedDescriptorGraph(
      Right.Types, CanonicalRoot{Right.Root});
  REQUIRE(LeftBytes.has_value());
  REQUIRE(RightBytes.has_value());
  CHECK(*LeftBytes == *RightBytes);
  CHECK(yona::model::canonicalStructuralFingerprint(*LeftBytes) ==
        yona::model::canonicalStructuralFingerprint(*RightBytes));

  const auto Expected =
      bytes("0200000000000000040000000001000000000000000200010000000000000005"
            "07240000000000000004000000000000004d61696e0400000000000000547265"
            "6501000000000000000000000011480000000000000000000000000000000000"
            "0000000000000000000001000000000000000200000002010000000101000000"
            "00010004000000000000004d61696e060000000000000072656e646572000200"
            "000012000000000000000000000000000000000000000000000000005100"
            "0000000000000100000000000000030000000000000053746407000000000000"
            "00436f6e736f6c65050000000000000077726974650100000000000000020000"
            "0001000000000000000000000000000000000000000100000000000003000000");
  INFO("actual canonical graph: " << hex(*LeftBytes));
  CHECK(*LeftBytes == Expected);
  CHECK(yona::model::canonicalStructuralFingerprint(*LeftBytes) ==
        UINT64_C(0xe4ee544481259936));

  TypeTable Unrelated;
  REQUIRE(Unrelated.intern(PrimitiveType::Bool).has_value());
  const auto UnrelatedInt = Unrelated.intern(PrimitiveType::Int);
  REQUIRE(UnrelatedInt.has_value());
  const auto UnrelatedBytes = yona::model::encodeClosedDescriptorGraph(
      Unrelated, CanonicalRoot{*UnrelatedInt});
  REQUIRE(UnrelatedBytes.has_value());
  TypeTable OnlyInt;
  const auto OnlyIntId = OnlyInt.intern(PrimitiveType::Int);
  REQUIRE(OnlyIntId.has_value());
  const auto OnlyIntBytes = yona::model::encodeClosedDescriptorGraph(
      OnlyInt, CanonicalRoot{*OnlyIntId});
  REQUIRE(OnlyIntBytes.has_value());
  CHECK(*UnrelatedBytes == *OnlyIntBytes);
}

TEST_CASE("Structural type canonical operation declarations alpha normalize "
          "binders and phantoms") {
  auto Left = openOperationFixture(0);
  auto Right = openOperationFixture(7);
  const std::array LeftBinders{Left.Binder};
  const std::array RightBinders{Right.Binder};
  const auto LeftBytes = yona::model::encodeOperationDeclarationGraph(
      Left.Types, Left.Signature, LeftBinders);
  const auto RightBytes = yona::model::encodeOperationDeclarationGraph(
      Right.Types, Right.Signature, RightBinders);
  REQUIRE(LeftBytes.has_value());
  REQUIRE(RightBytes.has_value());
  CHECK(*LeftBytes == *RightBytes);
  CHECK(yona::model::canonicalStructuralFingerprint(*LeftBytes) ==
        yona::model::canonicalStructuralFingerprint(*RightBytes));

  const auto UnrelatedParameter = Left.Types.allocateTypeParameter();
  const std::array WithUnrelatedBinder{
      Left.Binder, CanonicalBinderDeclaration{BinderOwnerKind::Nominal,
                                              key("Main", "Unrelated"),
                                              {UnrelatedParameter},
                                              {}}};
  const auto UnrelatedBinderBytes =
      yona::model::encodeOperationDeclarationGraph(Left.Types, Left.Signature,
                                                   WithUnrelatedBinder);
  REQUIRE(UnrelatedBinderBytes.has_value());
  CHECK(*UnrelatedBinderBytes == *LeftBytes);

  const auto Closed = yona::model::encodeClosedDescriptorGraph(
      Left.Types, CanonicalRoot{Left.OpenType});
  REQUIRE_FALSE(Closed.has_value());
  CHECK(Closed.error().Code ==
        CanonicalEncodingErrorCode::OpenRuntimeDescriptor);

  const auto MissingBinder = yona::model::encodeOperationDeclarationGraph(
      Left.Types, Left.Signature, {});
  REQUIRE_FALSE(MissingBinder.has_value());
  CHECK(MissingBinder.error().Code ==
        CanonicalEncodingErrorCode::UnboundParameter);

  auto DuplicateBinder = Left.Binder;
  DuplicateBinder.Identity = yona::model::FunctionDeclarationIdentity{
      SymbolIdentity{ModuleIdentity{"Main"}, "otherOwner"}};
  DuplicateBinder.Kind = BinderOwnerKind::Function;
  const std::array MultiplyOwned{Left.Binder, DuplicateBinder};
  const auto Duplicated = yona::model::encodeOperationDeclarationGraph(
      Left.Types, Left.Signature, MultiplyOwned);
  REQUIRE_FALSE(Duplicated.has_value());
  CHECK(Duplicated.error().Code ==
        CanonicalEncodingErrorCode::MultiplyOwnedParameter);

  auto MissingPhantoms = Left.Signature;
  MissingPhantoms.TypeParameters.pop_back();
  MissingPhantoms.EffectVariables.pop_back();
  const auto ShapeMismatched = yona::model::encodeOperationDeclarationGraph(
      Left.Types, MissingPhantoms, LeftBinders);
  REQUIRE_FALSE(ShapeMismatched.has_value());
  CHECK(ShapeMismatched.error().Code ==
        CanonicalEncodingErrorCode::InvalidBinder);

  TypeTable ConcreteTypes;
  const auto ConcreteInt = ConcreteTypes.intern(PrimitiveType::Int);
  const auto ConcreteEffects = ConcreteTypes.internEffect({});
  REQUIRE(ConcreteInt.has_value());
  REQUIRE(ConcreteEffects.has_value());
  const yona::model::EffectOperationSignature WrongResultContract{
      {key("Main", "State"), "concrete"},
      {},
      {},
      {},
      {},
      *ConcreteInt,
      ResultOwnership::Owned,
      *ConcreteEffects};
  const auto ContractMismatched = yona::model::encodeOperationDeclarationGraph(
      ConcreteTypes, WrongResultContract, {});
  REQUIRE_FALSE(ContractMismatched.has_value());
  CHECK(ContractMismatched.error().Code ==
        CanonicalEncodingErrorCode::InvalidReference);
}

TEST_CASE("Structural type canonical binders reject unallocated phantom IDs") {
  TypeTable Types;
  SUBCASE("type parameter") {
    const yona::model::TypeParameterId Forged{Types.idDomain(), 41};
    const std::array Binders{CanonicalBinderDeclaration{
        BinderOwnerKind::Nominal, key("Main", "Box"), {Forged}, {}}};
    const auto Encoded = yona::model::encodeInterfaceTables(Types, Binders);
    REQUIRE_FALSE(Encoded.has_value());
    CHECK(Encoded.error().Code == CanonicalEncodingErrorCode::InvalidBinder);
  }
  SUBCASE("effect variable") {
    const yona::model::EffectVariableId Forged{Types.idDomain(), 41};
    const EffectOperationKey Operation{key("Main", "State"), "get"};
    const std::array Binders{
        CanonicalBinderDeclaration{BinderOwnerKind::Operation,
                                   Operation,
                                   {},
                                   {{Forged, EffectVariableKind::Flexible}}}};
    const auto Encoded = yona::model::encodeInterfaceTables(Types, Binders);
    REQUIRE_FALSE(Encoded.has_value());
    CHECK(Encoded.error().Code == CanonicalEncodingErrorCode::InvalidBinder);
  }
}

TEST_CASE("Structural type canonical closed operations retain explicit phantom "
          "arguments") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto String = Types.intern(PrimitiveType::String);
  const auto Effects = Types.internEffect({});
  REQUIRE(Int.has_value());
  REQUIRE(String.has_value());
  REQUIRE(Effects.has_value());
  const EffectOperationKey Key{key("Main", "State"), "update"};
  const auto operation = [&](yona::model::TypeId Phantom) {
    return ClosedEffectOperation{
        Key,        {*Int, Phantom},
        {*Effects}, {{*Int, ParameterOwnership::Borrow}},
        *Int,       ResultOwnership::Trivial,
        *Effects};
  };
  const auto IntEncoding = yona::model::encodeClosedDescriptorGraph(
      Types, CanonicalRoot{operation(*Int)});
  const auto StringEncoding = yona::model::encodeClosedDescriptorGraph(
      Types, CanonicalRoot{operation(*String)});
  REQUIRE(IntEncoding.has_value());
  REQUIRE(StringEncoding.has_value());
  CHECK(*IntEncoding != *StringEncoding);
  CHECK(yona::model::canonicalStructuralFingerprint(*IntEncoding) !=
        yona::model::canonicalStructuralFingerprint(*StringEncoding));

  auto WrongResultContract = operation(*Int);
  WrongResultContract.ResultContract = ResultOwnership::Owned;
  const auto Rejected = yona::model::encodeClosedDescriptorGraph(
      Types, CanonicalRoot{WrongResultContract});
  REQUIRE_FALSE(Rejected.has_value());
  CHECK(Rejected.error().Code == CanonicalEncodingErrorCode::InvalidReference);
}

TEST_CASE("Structural type canonical interface tables round trip into a fresh "
          "domain") {
  TypeTable Types;
  const auto SymbolParameter = Types.allocateTypeParameter();
  const auto SymbolEffect = Types.allocateEffectVariable();
  const auto LocalParameter = Types.allocateTypeParameter();
  const auto SymbolType = Types.intern(TypeParameterType{SymbolParameter});
  const auto LocalType = Types.intern(TypeParameterType{LocalParameter});
  const auto EmptyEffects = Types.internEffect({});
  const auto SymbolEffects = Types.internEffect(EffectRow{
      {}, {{SymbolEffect, EffectVariableKind::Flexible, {}}}, true, false});
  REQUIRE(SymbolType.has_value());
  REQUIRE(LocalType.has_value());
  REQUIRE(EmptyEffects.has_value());
  REQUIRE(SymbolEffects.has_value());

  const yona::model::FunctionDeclarationIdentity SymbolIdentityValue =
      SymbolIdentity{ModuleIdentity{"Main"}, "top"};
  const yona::model::FunctionDeclarationIdentity LocalIdentityValue =
      yona::model::LocalFunctionIdentity{ModuleIdentity{"Main"}, {0, 0}};
  const auto SymbolFunction = Types.intern(
      FunctionType{{SymbolParameter},
                   {{SymbolEffect, EffectVariableKind::Flexible}},
                   {{SymbolParameter, key("Std", "Eq"), {*SymbolType}}},
                   {{*SymbolType, ParameterOwnership::Borrow}},
                   *SymbolType,
                   ResultOwnership::Owned,
                   *SymbolEffects,
                   CallingConvention::DirectYona,
                   SymbolIdentityValue,
                   std::nullopt});
  const auto LocalFunction =
      Types.intern(FunctionType{{LocalParameter},
                                {},
                                {},
                                {},
                                *LocalType,
                                ResultOwnership::Owned,
                                *EmptyEffects,
                                CallingConvention::ClosureEntry,
                                LocalIdentityValue,
                                std::nullopt});
  REQUIRE(SymbolFunction.has_value());
  REQUIRE(LocalFunction.has_value());

  const std::array Binders{
      CanonicalBinderDeclaration{
          BinderOwnerKind::Function, LocalIdentityValue, {LocalParameter}, {}},
      CanonicalBinderDeclaration{
          BinderOwnerKind::Function,
          SymbolIdentityValue,
          {SymbolParameter},
          {{SymbolEffect, EffectVariableKind::Flexible}}}};
  const auto Encoded = yona::model::encodeInterfaceTables(Types, Binders);
  REQUIRE(Encoded.has_value());
  CHECK(readU32(Encoded->TypeSectionPayload, 0) ==
        yona::model::CanonicalStructuralEncodingVersion);
  CHECK(readU32(Encoded->TypeSectionPayload, 4) == 2);
  CHECK(readU32(Encoded->EffectSectionPayload, 0) ==
        yona::model::CanonicalStructuralEncodingVersion);
  CHECK(readU32(Encoded->EffectSectionPayload, 4) == Types.effectCount());
  CHECK(Encoded->TypeRemap.size() == Types.typeCount());
  CHECK(Encoded->EffectRemap.size() == Types.effectCount());
  REQUIRE(Encoded->TypeParameterRemap.size() == 2);
  CHECK(Encoded->TypeParameterRemap[0].Source == SymbolParameter);
  CHECK(Encoded->TypeParameterRemap[1].Source == LocalParameter);

  const auto Decoded = yona::model::decodeInterfaceTables(
      Encoded->TypeSectionPayload, Encoded->EffectSectionPayload);
  REQUIRE(Decoded.has_value());
  CHECK(Decoded->Types.idDomain() != Types.idDomain());
  CHECK(Decoded->Types.typeCount() == Types.typeCount());
  CHECK(Decoded->Types.effectCount() == Types.effectCount());
  REQUIRE(Decoded->Binders.size() == 2);
  bool SawSymbol = false;
  bool SawLocal = false;
  for (const auto &Binder : Decoded->Binders) {
    REQUIRE(std::holds_alternative<yona::model::FunctionDeclarationIdentity>(
        Binder.Identity));
    const auto &Identity =
        std::get<yona::model::FunctionDeclarationIdentity>(Binder.Identity);
    SawSymbol |= std::holds_alternative<SymbolIdentity>(Identity);
    SawLocal |=
        std::holds_alternative<yona::model::LocalFunctionIdentity>(Identity);
  }
  CHECK(SawSymbol);
  CHECK(SawLocal);
  for (const auto Id : Decoded->TypeIdsByCanonicalIndex)
    CHECK(Decoded->Types.valid(Id));
  for (const auto Id : Decoded->EffectIdsByCanonicalIndex)
    CHECK(Decoded->Types.valid(Id));

  const auto Reencoded = yona::model::encodeInterfaceTables(
      Decoded->Types, decodedCanonicalBinders(*Decoded));
  REQUIRE(Reencoded.has_value());
  CHECK(Reencoded->TypeSectionPayload == Encoded->TypeSectionPayload);
  CHECK(Reencoded->EffectSectionPayload == Encoded->EffectSectionPayload);

  SUBCASE("rejects a corrupt nested-local identity discriminant") {
    auto CorruptTypes = Encoded->TypeSectionPayload;
    REQUIRE(CorruptTypes.size() > 47);
    CorruptTypes[47] = std::byte{2};
    const auto Corrupt = yona::model::decodeInterfaceTables(
        CorruptTypes, Encoded->EffectSectionPayload);
    REQUIRE_FALSE(Corrupt.has_value());
    CHECK(Corrupt.error().Code == CanonicalEncodingErrorCode::InvalidIdentity);
  }
  SUBCASE("rejects a truncated nested-local lexical path") {
    auto TruncatedTypes = Encoded->TypeSectionPayload;
    REQUIRE(TruncatedTypes.size() > 70);
    TruncatedTypes.resize(70);
    const auto Truncated = yona::model::decodeInterfaceTables(
        TruncatedTypes, Encoded->EffectSectionPayload);
    REQUIRE_FALSE(Truncated.has_value());
    CHECK(Truncated.error().Code ==
          CanonicalEncodingErrorCode::InvalidReference);
  }
}

TEST_CASE("Structural type canonical interface tables cover every type tag") {
  TypeTable Types;
  const auto RowParameter = Types.allocateTypeParameter();
  const auto TraitPhantom = Types.allocateTypeParameter();
  const auto ResourcePhantom = Types.allocateTypeParameter();
  const auto TailVariable = Types.allocateEffectVariable();
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto String = Types.intern(PrimitiveType::String);
  const auto Parameter = Types.intern(TypeParameterType{RowParameter});
  REQUIRE(Int.has_value());
  REQUIRE(String.has_value());
  REQUIRE(Parameter.has_value());
  const auto EmptyEffects = Types.internEffect({});
  REQUIRE(EmptyEffects.has_value());
  const EffectOperationKey Operation{key("Main", "State"), "get"};
  const auto OpenEffects = Types.internEffect(EffectRow{
      {EffectOperationApplication{Operation, {*Int}, {*EmptyEffects}}},
      {{TailVariable,
        EffectVariableKind::Flexible,
        {EffectOperationApplication{Operation, {*String}, {*EmptyEffects}}}}},
      true,
      true});
  REQUIRE(OpenEffects.has_value());

  std::vector<std::expected<yona::model::TypeId, std::string>> Added;
  Added.push_back(Types.intern(yona::model::TupleType{{*Int, *String}}));
  Added.push_back(Types.intern(yona::model::SequenceType{*Int}));
  Added.push_back(Types.intern(yona::model::SetType{*String}));
  Added.push_back(Types.intern(yona::model::DictionaryType{*String, *Int}));
  Added.push_back(Types.intern(
      yona::model::RecordType{{{"z", *String}, {"a", *Int}}, *Parameter}));
  Added.push_back(
      Types.intern(yona::model::NominalType{key("Main", "Option"), {*Int}}));
  Added.push_back(Types.intern(yona::model::ArrayType{*Int, true}));
  Added.push_back(Types.intern(yona::model::ChannelType{*String}));
  Added.push_back(Types.intern(
      yona::model::PromiseType{*Int, ResultOwnership::Trivial, *OpenEffects}));
  Added.push_back(
      Types.intern(yona::model::CursorType{*Int, ResultOwnership::Trivial}));
  Added.push_back(Types.intern(
      yona::model::CursorStepType{*String, ResultOwnership::Owned}));
  Added.push_back(Types.intern(yona::model::OutcomeTaskGroupType{}));
  Added.push_back(
      Types.intern(yona::model::ResourceType{key("Std", "Handle"), {*Int}}));
  Added.push_back(Types.intern(yona::model::AbiOpaqueType{
      yona::model::AbiOpaqueKind::ExecutionContext}));
  Added.push_back(Types.intern(yona::model::OwnedSlotStateType{
      yona::model::OwnedSlotStateKind::CleanupObligation,
      {{*Int, ParameterOwnership::Trivial}},
      yona::model::FunctionDeclarationIdentity{
          SymbolIdentity{ModuleIdentity{"Main"}, "dropState"}}}));
  Added.push_back(
      Types.intern(FunctionType{{},
                                {},
                                {},
                                {{*Int, ParameterOwnership::Trivial}},
                                *Int,
                                ResultOwnership::Trivial,
                                *EmptyEffects,
                                CallingConvention::DirectYona,
                                std::nullopt,
                                std::nullopt}));
  Added.push_back(Types.internSum(std::array{*Int, *String}));
  for (const auto &Type : Added)
    REQUIRE(Type.has_value());

  const std::array Binders{
      CanonicalBinderDeclaration{
          BinderOwnerKind::Nominal, key("Main", "Row"), {RowParameter}, {}},
      CanonicalBinderDeclaration{
          BinderOwnerKind::Trait, key("Main", "Show"), {TraitPhantom}, {}},
      CanonicalBinderDeclaration{
          BinderOwnerKind::Operation,
          Operation,
          {},
          {{TailVariable, EffectVariableKind::Flexible}}},
      CanonicalBinderDeclaration{BinderOwnerKind::Resource,
                                 key("Std", "Handle"),
                                 {ResourcePhantom},
                                 {}}};
  const auto Encoded = yona::model::encodeInterfaceTables(Types, Binders);
  REQUIRE(Encoded.has_value());
  const auto Decoded = yona::model::decodeInterfaceTables(
      Encoded->TypeSectionPayload, Encoded->EffectSectionPayload);
  REQUIRE(Decoded.has_value());
  std::array<bool, 19> SawTag{};
  for (const auto Id : Decoded->TypeIdsByCanonicalIndex)
    SawTag[Decoded->Types.at(Id).index()] = true;
  CHECK(std::all_of(SawTag.begin(), SawTag.end(),
                    [](bool Value) { return Value; }));
  const auto Reencoded = yona::model::encodeInterfaceTables(
      Decoded->Types, decodedCanonicalBinders(*Decoded));
  REQUIRE(Reencoded.has_value());
  CHECK(Reencoded->TypeSectionPayload == Encoded->TypeSectionPayload);
  CHECK(Reencoded->EffectSectionPayload == Encoded->EffectSectionPayload);
}

TEST_CASE("Structural type canonical instance binders use expanded identity "
          "order") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto String = Types.intern(PrimitiveType::String);
  REQUIRE(Int.has_value());
  REQUIRE(String.has_value());
  const auto Eq = key("Std", "Eq");
  const CanonicalBinderDeclaration IntInstance{
      BinderOwnerKind::Instance,
      yona::model::InstanceBinderOwner{Eq, {*Int}, {}},
      {},
      {}};
  const CanonicalBinderDeclaration StringInstance{
      BinderOwnerKind::Instance,
      yona::model::InstanceBinderOwner{Eq, {*String}, {}},
      {},
      {}};
  const std::array Forward{IntInstance, StringInstance};
  const std::array Reverse{StringInstance, IntInstance};
  const auto ForwardEncoding =
      yona::model::encodeInterfaceTables(Types, Forward);
  const auto ReverseEncoding =
      yona::model::encodeInterfaceTables(Types, Reverse);
  REQUIRE(ForwardEncoding.has_value());
  REQUIRE(ReverseEncoding.has_value());
  CHECK(ForwardEncoding->TypeSectionPayload ==
        ReverseEncoding->TypeSectionPayload);
  CHECK(ForwardEncoding->EffectSectionPayload ==
        ReverseEncoding->EffectSectionPayload);
  REQUIRE(ForwardEncoding->InstanceRemap.size() == 2);
  CHECK(ForwardEncoding->InstanceRemap[0] != ForwardEncoding->InstanceRemap[1]);

  const auto Decoded =
      yona::model::decodeInterfaceTables(ForwardEncoding->TypeSectionPayload,
                                         ForwardEncoding->EffectSectionPayload);
  REQUIRE(Decoded.has_value());
  REQUIRE(Decoded->Binders.size() == 2);
  CHECK(std::get<std::uint32_t>(Decoded->Binders[0].Identity) == 0);
  CHECK(std::get<std::uint32_t>(Decoded->Binders[1].Identity) == 1);
}

TEST_CASE("Structural type canonical decoder accepts dense instance ordinals "
          "past one byte") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  REQUIRE(Int.has_value());
  std::vector<CanonicalBinderDeclaration> Binders;
  Binders.reserve(257);
  for (std::uint32_t Index = 0; Index != 257; ++Index)
    Binders.push_back(
        {BinderOwnerKind::Instance,
         yona::model::InstanceBinderOwner{
             key("Main", "Trait" + std::to_string(Index)), {*Int}, {}},
         {},
         {}});

  const auto Encoded = yona::model::encodeInterfaceTables(Types, Binders);
  REQUIRE(Encoded.has_value());
  const auto Decoded = yona::model::decodeInterfaceTables(
      Encoded->TypeSectionPayload, Encoded->EffectSectionPayload);

  REQUIRE(Decoded.has_value());
  REQUIRE(Decoded->Binders.size() == 257);
  CHECK(std::get<std::uint32_t>(Decoded->Binders.back().Identity) == 256);
}

TEST_CASE("Structural type canonical decoder rejects corrupt and "
          "noncanonical payloads") {
  TypeTable Types;
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto Bool = Types.intern(PrimitiveType::Bool);
  REQUIRE(Int.has_value());
  REQUIRE(Bool.has_value());
  const auto Encoded = yona::model::encodeInterfaceTables(Types, {});
  REQUIRE(Encoded.has_value());

  SUBCASE("mismatched version") {
    auto Effects = Encoded->EffectSectionPayload;
    writeU32(Effects, 0, yona::model::CanonicalStructuralEncodingVersion + 1);
    const auto Result = yona::model::decodeInterfaceTables(
        Encoded->TypeSectionPayload, Effects);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().Code == CanonicalEncodingErrorCode::InvalidReference);
  }

  SUBCASE("trailing byte") {
    auto Effects = Encoded->EffectSectionPayload;
    Effects.push_back(std::byte{0});
    const auto Result = yona::model::decodeInterfaceTables(
        Encoded->TypeSectionPayload, Effects);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().Code == CanonicalEncodingErrorCode::NonCanonicalOrder);
  }

  SUBCASE("unknown type tag") {
    auto TypeBytes = Encoded->TypeSectionPayload;
    REQUIRE(TypeBytes.size() > 12);
    TypeBytes[12] = std::byte{255};
    const auto Result = yona::model::decodeInterfaceTables(
        TypeBytes, Encoded->EffectSectionPayload);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().Code == CanonicalEncodingErrorCode::InvalidReference);
  }

  SUBCASE("noncanonical type order") {
    auto TypeBytes = Encoded->TypeSectionPayload;
    REQUIRE(TypeBytes.size() == 32);
    std::swap_ranges(TypeBytes.begin() + 12, TypeBytes.begin() + 22,
                     TypeBytes.begin() + 22);
    const auto Result = yona::model::decodeInterfaceTables(
        TypeBytes, Encoded->EffectSectionPayload);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().Code == CanonicalEncodingErrorCode::NonCanonicalOrder);
  }

  SUBCASE("out of range child index") {
    TypeTable ReferenceTypes;
    const auto Element = ReferenceTypes.intern(PrimitiveType::Int);
    REQUIRE(Element.has_value());
    REQUIRE(
        ReferenceTypes.intern(yona::model::SequenceType{*Element}).has_value());
    const auto ReferenceEncoding =
        yona::model::encodeInterfaceTables(ReferenceTypes, {});
    REQUIRE(ReferenceEncoding.has_value());
    auto TypeBytes = ReferenceEncoding->TypeSectionPayload;
    REQUIRE(TypeBytes.size() == 35);
    writeU32(TypeBytes, 31, 99);
    const auto Result = yona::model::decodeInterfaceTables(
        TypeBytes, ReferenceEncoding->EffectSectionPayload);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().Code == CanonicalEncodingErrorCode::InvalidReference);
  }
}

TEST_CASE("Structural type canonical tables remain collision safe") {
  TypeTable Types;
  StructuralTypeTestAccess Collision(
      {.FailAfter = std::nullopt, .ConstantHash = 1, .ConstantFingerprint = 1});
  const auto Int = Types.intern(PrimitiveType::Int);
  const auto String = Types.intern(PrimitiveType::String);
  REQUIRE(Int.has_value());
  REQUIRE(String.has_value());
  const auto Encoded = yona::model::encodeInterfaceTables(Types, {});
  REQUIRE(Encoded.has_value());
  REQUIRE(Encoded->TypeRemap.size() == 2);
  CHECK(Encoded->TypeRemap[Int->value()] !=
        Encoded->TypeRemap[String->value()]);
  const auto Decoded = yona::model::decodeInterfaceTables(
      Encoded->TypeSectionPayload, Encoded->EffectSectionPayload);
  REQUIRE(Decoded.has_value());
  REQUIRE(Decoded->TypeIdsByCanonicalIndex.size() == 2);
  CHECK(Decoded->Types.at(Decoded->TypeIdsByCanonicalIndex[0]) !=
        Decoded->Types.at(Decoded->TypeIdsByCanonicalIndex[1]));
}
