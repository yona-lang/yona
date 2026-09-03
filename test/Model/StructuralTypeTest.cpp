#include "Support/StructuralTypeTestAccess.h"
#include "yona/Model/StructuralType.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {

using yona::model::BinderOwnerKind;
using yona::model::CallingConvention;
using yona::model::CanonicalBinderDeclaration;
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
