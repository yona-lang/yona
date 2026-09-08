#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/Verifier.h"

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

namespace {

struct ScalarFixture {
  std::shared_ptr<yona::SourceManager> Sources =
      std::make_shared<yona::SourceManager>();
  yona::SourceId Source = Sources->addSource("scalar.yona", "x");
  yona::SourceRange Range{Source, 1, 1, 0, 1};
  std::shared_ptr<yona::model::TypeTable> Types =
      std::make_shared<yona::model::TypeTable>();
  yona::model::EffectRowId EmptyEffects = Types->internEffect({}).value();
  yona::model::TypeId Unit =
      Types->intern(yona::model::PrimitiveType::Unit).value();
  yona::model::TypeId Bool =
      Types->intern(yona::model::PrimitiveType::Bool).value();
  yona::model::TypeId Int =
      Types->intern(yona::model::PrimitiveType::Int).value();
  yona::model::TypeId Exception =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::ExceptionValue})
          .value();
  yona::model::TypeId EffectRequest =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::EffectRequest})
          .value();
  yona::model::TypeId ControlOutcome =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::ControlOutcome})
          .value();
  yona::model::TypeId ExecutionContext =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::ExecutionContext})
          .value();
  yona::model::TypeId CallableEnvironment =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::CallableInvocationEnvironment})
          .value();
  yona::model::TypeId ContinuationContext =
      Types
          ->intern(yona::model::AbiOpaqueType{
              yona::model::AbiOpaqueKind::ContinuationBoundaryContext})
          .value();
  yona::model::TypeId IntToInt =
      Types
          ->intern(yona::model::FunctionType{
              {},
              {},
              {},
              {{Int, yona::model::ParameterOwnership::Trivial}},
              Int,
              yona::model::ResultOwnership::Trivial,
              EmptyEffects,
              yona::model::CallingConvention::DirectYona,
              std::nullopt,
              std::nullopt})
          .value();

  yona::typed_ir::ModuleSpec moduleSpec() const {
    return {yona::model::ModuleIdentity("Main"),
            Types,
            {Exception, EffectRequest, ControlOutcome, ExecutionContext,
             CallableEnvironment, ContinuationContext},
            Sources,
            Source};
  }

  yona::typed_ir::FunctionSpec intToIntFunction() const {
    return {"identity",
            Range,
            IntToInt,
            {yona::typed_ir::FunctionDefinitionKind::Definition,
             yona::typed_ir::FunctionVisibility::Private, std::nullopt,
             std::nullopt, std::nullopt, std::nullopt}};
  }
};

template <class Enum> constexpr std::size_t enumCount();
template <> constexpr std::size_t enumCount<yona::typed_ir::OwnershipKind>() {
  return 3;
}

} // namespace

TEST_CASE("Typed IR core: strong IDs reject cross-domain use") {
  struct TestTag;
  static_assert(std::is_same_v<yona::typed_ir::StrongId<TestTag>,
                               yona::model::StrongId<TestTag>>);
  static_assert(std::is_same_v<yona::typed_ir::StrongIdHasher,
                               yona::model::StrongIdHasher>);
  static_assert(!std::is_convertible_v<yona::typed_ir::FunctionId,
                                       yona::typed_ir::BlockId>);

  ScalarFixture Left;
  ScalarFixture Right;
  yona::typed_ir::Builder Builder(Left.moduleSpec());
  const auto Function = Builder.addFunction(Left.intToIntFunction());
  CHECK_THROWS_AS(Builder.createBlock(
                      yona::typed_ir::FunctionId(Right.Types->idDomain(), 0),
                      {.DebugName = "foreign", .Range = Left.Range}),
                  std::invalid_argument);
  CHECK(Function.isValid());
}

TEST_CASE("Typed IR builder: block arguments are typed phi values") {
  ScalarFixture Fixture;
  yona::typed_ir::Builder Builder(Fixture.moduleSpec());
  const auto Function = Builder.addFunction(Fixture.intToIntFunction());
  const auto Parameter = Builder.addParameter(
      Function, {.Type = Fixture.Int,
                 .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                 .Range = Fixture.Range,
                 .DebugName = "value"});
  const auto Merge = Builder.createBlock(
      Function, {.DebugName = "merge", .Range = Fixture.Range});
  const auto Result = Builder.addBlockArgument(
      Function, Merge,
      {.Type = Fixture.Int,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Fixture.Range,
       .DebugName = "result"});
  Builder.setTerminator(Function, Merge, yona::typed_ir::Return{Result},
                        Fixture.Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  CHECK(yona::typed_ir::verifyModule(Module).has_value());
  CHECK(Module.Functions.front().Parameters ==
        std::vector<yona::typed_ir::ValueId>{Parameter});
}

TEST_CASE("Typed IR builder seeds a complete module") {
  ScalarFixture Fixture;
  yona::typed_ir::Builder Builder(Fixture.moduleSpec());
  const auto Function = Builder.addFunction(Fixture.intToIntFunction());
  const auto Parameter = Builder.addParameter(
      Function, {.Type = Fixture.Int,
                 .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                 .Range = Fixture.Range,
                 .DebugName = "value"});
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Parameter},
                        Fixture.Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  REQUIRE(Module.MutationDomain);
  REQUIRE(Module.Types == Fixture.Types);
  REQUIRE(Module.Functions.size() == 1);
  CHECK(yona::typed_ir::verifyModule(Module).has_value());
  CHECK_THROWS_AS(Builder.finish(yona::typed_ir::ModulePhase::Canonical),
                  std::logic_error);
}

TEST_CASE("Typed IR verifier: transferred is not a value ownership kind") {
  CHECK(yona::typed_ir::valueOwnershipKindName(
            yona::typed_ir::OwnershipKind::Owned) == "owned");
  CHECK(enumCount<yona::typed_ir::OwnershipKind>() == 3);
}

TEST_CASE(
    "Typed IR builder rejects foreign function-local IDs before mutation") {
  ScalarFixture Fixture;
  yona::typed_ir::Builder Builder(Fixture.moduleSpec());
  const auto Left = Builder.addFunction(Fixture.intToIntFunction());
  auto RightSpec = Fixture.intToIntFunction();
  RightSpec.Name = "right";
  const auto Right = Builder.addFunction(std::move(RightSpec));
  const auto LeftBlock =
      Builder.createBlock(Left, {.DebugName = "left", .Range = Fixture.Range});
  const auto RightBlock = Builder.createBlock(
      Right, {.DebugName = "right", .Range = Fixture.Range});
  const auto LeftParameter = Builder.addParameter(
      Left, {.Type = Fixture.Int,
             .Ownership = yona::typed_ir::OwnershipKind::Trivial,
             .Range = Fixture.Range,
             .DebugName = "left"});
  Builder.addParameter(Right,
                       {.Type = Fixture.Int,
                        .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                        .Range = Fixture.Range,
                        .DebugName = "right"});
  CHECK_THROWS_AS(Builder.addBlockArgument(
                      Right, LeftBlock,
                      {.Type = Fixture.Int,
                       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                       .Range = Fixture.Range,
                       .DebugName = "foreign-block"}),
                  std::invalid_argument);
  CHECK_THROWS_AS(Builder.setTerminator(Right, RightBlock,
                                        yona::typed_ir::Return{LeftParameter},
                                        Fixture.Range),
                  std::invalid_argument);
}
