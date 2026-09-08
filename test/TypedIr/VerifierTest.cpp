#include "yona/Model/CanonicalEncoding.h"
#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/Verifier.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

namespace {

bool hasRule(const std::vector<yona::typed_ir::VerificationIssue> &Issues,
             yona::typed_ir::VerificationRule Rule) {
  return std::ranges::any_of(
      Issues, [Rule](const auto &Issue) { return Issue.Rule == Rule; });
}

struct VerifierFixture {
  std::shared_ptr<yona::SourceManager> Sources =
      std::make_shared<yona::SourceManager>();
  yona::SourceId Source = Sources->addSource("verify.yona", "xxxxxxxx");
  yona::SourceRange Range{Source, 1, 1, 0, 1};
  std::shared_ptr<yona::model::TypeTable> Types =
      std::make_shared<yona::model::TypeTable>();
  yona::model::EffectRowId Empty = Types->internEffect({}).value();
  yona::model::TypeId Unit =
      Types->intern(yona::model::PrimitiveType::Unit).value();
  yona::model::TypeId Bool =
      Types->intern(yona::model::PrimitiveType::Bool).value();
  yona::model::TypeId Int =
      Types->intern(yona::model::PrimitiveType::Int).value();
  yona::model::TypeId String =
      Types->intern(yona::model::PrimitiveType::String).value();
  yona::typed_ir::BuiltinTypeIds Builtins{
      opaque(yona::model::AbiOpaqueKind::ExceptionValue),
      opaque(yona::model::AbiOpaqueKind::EffectRequest),
      opaque(yona::model::AbiOpaqueKind::ControlOutcome),
      opaque(yona::model::AbiOpaqueKind::ExecutionContext),
      opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
      opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)};
  yona::model::TypeId IntToInt =
      functionType({{Int, yona::model::ParameterOwnership::Trivial}}, Int);
  yona::model::TypeId NullaryInt = functionType({}, Int);
  yona::model::TypeId NullaryUnit = functionType({}, Unit);

  yona::model::TypeId opaque(yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  }
  yona::model::TypeId
  functionType(std::vector<yona::model::FunctionParameter> Parameters,
               yona::model::TypeId Result) {
    return Types
        ->intern(yona::model::FunctionType{
            {},
            {},
            {},
            std::move(Parameters),
            Result,
            yona::model::ResultOwnership::Trivial,
            Empty,
            yona::model::CallingConvention::DirectYona,
            std::nullopt,
            std::nullopt})
        .value();
  }
  yona::typed_ir::Builder builder() {
    return yona::typed_ir::Builder({yona::model::ModuleIdentity("Verify"),
                                    Types, Builtins, Sources, Source});
  }
  yona::typed_ir::FunctionSpec definition(std::string Name,
                                          yona::model::TypeId Signature) {
    return {std::move(Name),
            Range,
            Signature,
            {yona::typed_ir::FunctionDefinitionKind::Definition,
             yona::typed_ir::FunctionVisibility::Private}};
  }
};

} // namespace

namespace {

std::uint64_t
declarationFingerprint(const yona::model::TypeTable &Types,
                       const yona::model::EffectOperationSignature &Signature) {
  std::vector<yona::model::CanonicalBinderDeclaration> Binders;
  if (!Signature.TypeParameters.empty() || !Signature.EffectVariables.empty())
    Binders.push_back({yona::model::BinderOwnerKind::Operation, Signature.Key,
                       Signature.TypeParameters, Signature.EffectVariables});
  const auto Bytes =
      yona::model::encodeOperationDeclarationGraph(Types, Signature, Binders);
  REQUIRE(Bytes.has_value());
  return yona::model::canonicalStructuralFingerprint(*Bytes);
}

std::uint64_t
runtimeFingerprint(const yona::model::TypeTable &Types,
                   const yona::model::ClosedEffectOperation &Operation) {
  const auto Bytes = yona::model::encodeClosedDescriptorGraph(
      Types, yona::model::CanonicalRoot{Operation});
  REQUIRE(Bytes.has_value());
  return yona::model::canonicalStructuralFingerprint(*Bytes);
}

} // namespace

TEST_CASE("Typed IR verifier rejects non-dominating values") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("dominance.yona", "x");
  const yona::SourceRange Range{Source, 1, 1, 0, 1};
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Empty = Types->internEffect({}).value();
  const auto Bool = Types->intern(yona::model::PrimitiveType::Bool).value();
  const auto Int = Types->intern(yona::model::PrimitiveType::Int).value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
  const auto Signature = Types
                             ->intern(yona::model::FunctionType{
                                 {},
                                 {},
                                 {},
                                 {},
                                 Int,
                                 yona::model::ResultOwnership::Trivial,
                                 Empty,
                                 yona::model::CallingConvention::DirectYona,
                                 std::nullopt,
                                 std::nullopt})
                             .value();
  yona::typed_ir::Builder Builder(
      {yona::model::ModuleIdentity("Main"),
       Types,
       {Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
        Opaque(yona::model::AbiOpaqueKind::EffectRequest),
        Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
        Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
        Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
        Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)},
       Sources,
       Source});
  const auto Function =
      Builder.addFunction({"bad",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  const auto Left =
      Builder.createBlock(Function, {.DebugName = "left", .Range = Range});
  const auto Right =
      Builder.createBlock(Function, {.DebugName = "right", .Range = Range});
  const auto Condition = Builder.emit(
      Function, Entry,
      yona::typed_ir::ConstantInst{yona::typed_ir::BoolLiteral{true}},
      {.Type = Bool,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Range,
       .DebugName = "condition"});
  const auto LeftValue =
      Builder.emit(Function, Left,
                   yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{1}},
                   {.Type = Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Range,
                    .DebugName = "left-value"});
  Builder.setTerminator(
      Function, Entry,
      yona::typed_ir::CondBranch{Condition,
                                 {yona::typed_ir::EdgeId{}, Left, {}},
                                 {yona::typed_ir::EdgeId{}, Right, {}}},
      Range);
  Builder.setTerminator(Function, Left, yona::typed_ir::Return{LeftValue},
                        Range);
  Builder.setTerminator(Function, Right, yona::typed_ir::Return{LeftValue},
                        Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto Result = yona::typed_ir::verifyModule(Module);
  REQUIRE_FALSE(Result.has_value());
  CHECK(hasRule(Result.error(), yona::typed_ir::VerificationRule::Dominance));
}

TEST_CASE(
    "Typed IR verifier reports malformed control flow deterministically") {
  yona::typed_ir::Module Module{yona::model::ModuleIdentity("Main"),
                                nullptr,
                                nullptr,
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
                                {},
                                nullptr,
                                yona::SourceId{},
                                yona::typed_ir::ModulePhase::Canonical};
  const auto Result = yona::typed_ir::verifyModule(Module);
  REQUIRE_FALSE(Result.has_value());
  REQUIRE(Result.error().size() >= 3);
  CHECK(Result.error().front().Rule ==
        yona::typed_ir::VerificationRule::InvalidId);
}

TEST_CASE("Typed IR verifier reports a missing type table without crashing") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Function = Builder.addFunction(
      Fixture.definition("missingTypes", Fixture.NullaryUnit));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{},
                        Fixture.Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  Module.Functions.front().GenericBinders =
      yona::model::GenericBinderEnvironment{};
  Module.Functions.front().GenericCaptures.push_back(
      {yona::semantics::BindingId(1), Fixture.Int,
       yona::model::ParameterOwnership::Trivial, "captured"});
  Module.Types.reset();

  std::optional<yona::typed_ir::VerificationResult> Result;
  CHECK_NOTHROW(Result.emplace(yona::typed_ir::verifyModule(Module)));
  REQUIRE(Result.has_value());
  REQUIRE_FALSE(Result->has_value());
  CHECK(hasRule(Result->error(), yona::typed_ir::VerificationRule::InvalidId));
}

TEST_CASE("Typed IR verifier rejects opcode type mismatches") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Function =
      Builder.addFunction(Fixture.definition("badBinary", Fixture.NullaryInt));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  const auto Left = Builder.emit(
      Function, Entry,
      yona::typed_ir::ConstantInst{yona::typed_ir::BoolLiteral{true}},
      {.Type = Fixture.Bool,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Fixture.Range,
       .DebugName = "left"});
  const auto Result =
      Builder.emit(Function, Entry,
                   yona::typed_ir::BinaryInst{
                       yona::typed_ir::BinaryOpcode::AddInt, Left, Left},
                   {.Type = Fixture.Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Fixture.Range,
                    .DebugName = "sum"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Result},
                        Fixture.Range);
  const auto Verified = yona::typed_ir::verifyModule(
      Builder.finish(yona::typed_ir::ModulePhase::Canonical));
  REQUIRE_FALSE(Verified.has_value());
  CHECK(hasRule(Verified.error(),
                yona::typed_ir::VerificationRule::OperationSignature));
}

TEST_CASE("Typed IR verifier rejects successor contract mismatches") {
  VerifierFixture Fixture;
  auto Make = [&](yona::typed_ir::OwnershipKind TargetOwnership,
                  bool SupplyArgument, bool WrongType) {
    auto Builder = Fixture.builder();
    const auto Function =
        Builder.addFunction(Fixture.definition("branch", Fixture.NullaryInt));
    const auto Entry = Builder.createBlock(
        Function, {.DebugName = "entry", .Range = Fixture.Range});
    const auto Merge = Builder.createBlock(
        Function, {.DebugName = "merge", .Range = Fixture.Range});
    const auto Argument =
        Builder.addBlockArgument(Function, Merge,
                                 {.Type = Fixture.Int,
                                  .Ownership = TargetOwnership,
                                  .Range = Fixture.Range,
                                  .DebugName = "argument"});
    const auto Value = Builder.emit(
        Function, Entry,
        yona::typed_ir::ConstantInst{
            WrongType
                ? yona::typed_ir::LiteralValue{yona::typed_ir::BoolLiteral{
                      true}}
                : yona::typed_ir::LiteralValue{yona::typed_ir::IntLiteral{1}}},
        {.Type = WrongType ? Fixture.Bool : Fixture.Int,
         .Ownership = yona::typed_ir::OwnershipKind::Trivial,
         .Range = Fixture.Range,
         .DebugName = "value"});
    Builder.setTerminator(
        Function, Entry,
        yona::typed_ir::Branch{{{},
                                Merge,
                                SupplyArgument
                                    ? std::vector{Value}
                                    : std::vector<yona::typed_ir::ValueId>{}}},
        Fixture.Range);
    Builder.setTerminator(Function, Merge, yona::typed_ir::Return{Argument},
                          Fixture.Range);
    return Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  };
  auto Arity = yona::typed_ir::verifyModule(
      Make(yona::typed_ir::OwnershipKind::Trivial, false, false));
  REQUIRE_FALSE(Arity);
  CHECK(
      hasRule(Arity.error(), yona::typed_ir::VerificationRule::SuccessorArity));
  auto Type = yona::typed_ir::verifyModule(
      Make(yona::typed_ir::OwnershipKind::Trivial, true, true));
  REQUIRE_FALSE(Type);
  CHECK(hasRule(Type.error(), yona::typed_ir::VerificationRule::SuccessorType));
  auto Ownership = yona::typed_ir::verifyModule(
      Make(yona::typed_ir::OwnershipKind::Owned, true, false));
  REQUIRE_FALSE(Ownership);
  CHECK(hasRule(Ownership.error(),
                yona::typed_ir::VerificationRule::SuccessorOwnership));
}

TEST_CASE("Typed IR verifier rejects malformed linkage ranges and phases") {
  SUBCASE("imported declaration needs a symbol") {
    VerifierFixture Fixture;
    auto Builder = Fixture.builder();
    Builder.addFunction({"imported",
                         Fixture.Range,
                         Fixture.IntToInt,
                         {yona::typed_ir::FunctionDefinitionKind::Imported,
                          yona::typed_ir::FunctionVisibility::Public}});
    auto Result = yona::typed_ir::verifyModule(
        Builder.finish(yona::typed_ir::ModulePhase::Canonical));
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::CallSignature));
  }
  SUBCASE("missing ranges are rejected") {
    VerifierFixture Fixture;
    auto Builder = Fixture.builder();
    const auto Function =
        Builder.addFunction(Fixture.definition("ranged", Fixture.NullaryUnit));
    const auto Entry = Builder.createBlock(
        Function, {.DebugName = "entry", .Range = Fixture.Range});
    Builder.setTerminator(Function, Entry, yona::typed_ir::Return{},
                          Fixture.Range);
    auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
    Module.Functions.front().Blocks.front().Range =
        yona::SourceRange::unknown();
    auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(), yona::typed_ir::VerificationRule::InvalidId));
  }
  SUBCASE("post-canonical records are phase illegal") {
    VerifierFixture Fixture;
    auto Builder = Fixture.builder();
    auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
    Module.BoundaryRequirementSets.push_back({{}, {}});
    auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::IllegalPhaseOperation));
  }
}

TEST_CASE("Typed IR verifier accepts terminated unreachable blocks") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Function = Builder.addFunction(
      Fixture.definition("unreachableBlocks", Fixture.NullaryInt));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  const auto Dead = Builder.createBlock(
      Function, {.DebugName = "dead", .Range = Fixture.Range});
  const auto LiveValue =
      Builder.emit(Function, Entry,
                   yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{1}},
                   {.Type = Fixture.Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Fixture.Range,
                    .DebugName = "live"});
  const auto DeadValue =
      Builder.emit(Function, Dead,
                   yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{2}},
                   {.Type = Fixture.Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Fixture.Range,
                    .DebugName = "dead"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{LiveValue},
                        Fixture.Range);
  Builder.setTerminator(Function, Dead, yona::typed_ir::Return{DeadValue},
                        Fixture.Range);
  CHECK(yona::typed_ir::verifyModule(
            Builder.finish(yona::typed_ir::ModulePhase::Canonical))
            .has_value());
}

TEST_CASE("Typed IR builder rejects double terminators") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Function =
      Builder.addFunction(Fixture.definition("doubleEnd", Fixture.NullaryUnit));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{},
                        Fixture.Range);
  CHECK_THROWS_AS(Builder.setTerminator(Function, Entry,
                                        yona::typed_ir::Unreachable{},
                                        Fixture.Range),
                  std::invalid_argument);
}

TEST_CASE("Typed IR verifier rejects undeclared lexical captures") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Function =
      Builder.addFunction(Fixture.definition("capture", Fixture.NullaryInt));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  const auto Captured = Builder.emit(
      Function, Entry,
      yona::typed_ir::LexicalRefInst{yona::semantics::BindingId(7)},
      {.Type = Fixture.Int,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Fixture.Range,
       .DebugName = "capture"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Captured},
                        Fixture.Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  Module.Functions.front().GenericCaptures.push_back(
      {yona::semantics::BindingId(8), Fixture.Int,
       yona::model::ParameterOwnership::Trivial, "other"});
  const auto Result = yona::typed_ir::verifyModule(Module);
  REQUIRE_FALSE(Result);
  CHECK(hasRule(Result.error(),
                yona::typed_ir::VerificationRule::OperationSignature));
}

TEST_CASE("Typed IR verifier rejects perform signature mismatches") {
  VerifierFixture Fixture;
  auto Builder = Fixture.builder();
  const auto Operation = Builder.addOperation(
      {{{yona::model::ModuleIdentity("Verify"), "State"}, "put"},
       {},
       {},
       {},
       {{Fixture.Int, yona::model::ParameterOwnership::Trivial}},
       Fixture.Int,
       yona::model::ResultOwnership::Trivial,
       Fixture.Empty,
       1});
  const auto Function =
      Builder.addFunction(Fixture.definition("perform", Fixture.NullaryInt));
  const auto Entry = Builder.createBlock(
      Function, {.DebugName = "entry", .Range = Fixture.Range});
  const auto Resume = Builder.createBlock(
      Function, {.DebugName = "resume", .Range = Fixture.Range});
  const auto Result = Builder.addBlockArgument(
      Function, Resume,
      {.Type = Fixture.Int,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Fixture.Range,
       .DebugName = "result"});
  Builder.setTerminator(
      Function, Entry,
      yona::typed_ir::Perform{
          yona::typed_ir::OperationUse{Operation, {}, {}},
          {},
          {{},
           Resume,
           {{Fixture.Int, yona::typed_ir::OwnershipKind::Trivial, std::nullopt,
             "result"}},
           {}},
          Fixture.Empty},
      Fixture.Range);
  Builder.setTerminator(Function, Resume, yona::typed_ir::Return{Result},
                        Fixture.Range);
  const auto Verified = yona::typed_ir::verifyModule(
      Builder.finish(yona::typed_ir::ModulePhase::Canonical));
  REQUIRE_FALSE(Verified);
  CHECK(hasRule(Verified.error(),
                yona::typed_ir::VerificationRule::OperationSignature));
}

TEST_CASE("Typed IR verifier rejects every invalid borrow provenance shape") {
  auto makeModule = [] {
    VerifierFixture Fixture;
    const auto Signature = Fixture.functionType(
        {{Fixture.String, yona::model::ParameterOwnership::Consume},
         {Fixture.String, yona::model::ParameterOwnership::Borrow}},
        Fixture.Unit);
    auto Builder = Fixture.builder();
    const auto Function =
        Builder.addFunction(Fixture.definition("borrow", Signature));
    const auto Owner = Builder.addParameter(
        Function, {.Type = Fixture.String,
                   .Ownership = yona::typed_ir::OwnershipKind::Owned,
                   .Range = Fixture.Range,
                   .DebugName = "owner"});
    const auto Borrow = Builder.addParameter(
        Function,
        {.Type = Fixture.String,
         .Ownership = yona::typed_ir::OwnershipKind::Borrowed,
         .Borrow =
             yona::typed_ir::BorrowProvenance{
                 yona::typed_ir::BorrowProvenanceKind::BorrowParameter,
                 std::nullopt},
         .Range = Fixture.Range,
         .DebugName = "borrow"});
    const auto Entry = Builder.createBlock(
        Function, {.DebugName = "entry", .Range = Fixture.Range});
    Builder.setTerminator(Function, Entry, yona::typed_ir::Return{},
                          Fixture.Range);
    return std::tuple{Builder.finish(yona::typed_ir::ModulePhase::Canonical),
                      Owner, Borrow};
  };

  SUBCASE("BorrowParameter root names an actual Borrow parameter") {
    auto [Module, Owner, Borrow] = makeModule();
    Module.Functions.front().Values[Borrow.value()].Borrow->Root = Owner;
    const auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::OperationSignature));
  }
  SUBCASE("StaticLifetime is rootless") {
    auto [Module, Owner, Borrow] = makeModule();
    Module.Functions.front().Values[Borrow.value()].Borrow =
        yona::typed_ir::BorrowProvenance{
            yona::typed_ir::BorrowProvenanceKind::StaticLifetime, Owner};
    const auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::OperationSignature));
  }
  SUBCASE("OwnedValue root is Owned") {
    auto [Module, Owner, Borrow] = makeModule();
    Module.Functions.front().Values[Owner.value()].Ownership =
        yona::typed_ir::OwnershipKind::Trivial;
    Module.Functions.front().Values[Borrow.value()].Borrow =
        yona::typed_ir::BorrowProvenance{
            yona::typed_ir::BorrowProvenanceKind::OwnedValue, Owner};
    const auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::OperationSignature));
  }
  SUBCASE("rooted provenance requires a dominating root") {
    VerifierFixture Fixture;
    auto Builder = Fixture.builder();
    const auto Function = Builder.addFunction(
        Fixture.definition("dominance", Fixture.NullaryUnit));
    const auto Entry = Builder.createBlock(
        Function, {.DebugName = "entry", .Range = Fixture.Range});
    const auto Left = Builder.createBlock(
        Function, {.DebugName = "left", .Range = Fixture.Range});
    const auto Right = Builder.createBlock(
        Function, {.DebugName = "right", .Range = Fixture.Range});
    const auto Owner = Builder.emit(
        Function, Left,
        yona::typed_ir::ConstantInst{yona::typed_ir::StringLiteral{"owner"}},
        {.Type = Fixture.String,
         .Ownership = yona::typed_ir::OwnershipKind::Owned,
         .Range = Fixture.Range,
         .DebugName = "owner"});
    const auto Borrow = Builder.addBlockArgument(
        Function, Right,
        {.Type = Fixture.String,
         .Ownership = yona::typed_ir::OwnershipKind::Borrowed,
         .Borrow =
             yona::typed_ir::BorrowProvenance{
                 yona::typed_ir::BorrowProvenanceKind::OwnedValue, Owner},
         .Range = Fixture.Range,
         .DebugName = "borrow"});
    (void)Borrow;
    Builder.setTerminator(Function, Entry,
                          yona::typed_ir::Branch{{{}, Right, {}}},
                          Fixture.Range);
    Builder.setTerminator(Function, Left, yona::typed_ir::Return{},
                          Fixture.Range);
    Builder.setTerminator(Function, Right, yona::typed_ir::Return{},
                          Fixture.Range);
    const auto Result = yona::typed_ir::verifyModule(
        Builder.finish(yona::typed_ir::ModulePhase::Canonical));
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(), yona::typed_ir::VerificationRule::Dominance));
  }
}

TEST_CASE("Typed IR verifier recomputes canonical operation fingerprints") {
  VerifierFixture Fixture;
  const yona::model::EffectOperationSignature Signature{
      {{yona::model::ModuleIdentity("Verify"), "State"}, "get"},
      {},
      {},
      {},
      {},
      Fixture.Int,
      yona::model::ResultOwnership::Trivial,
      Fixture.Empty};
  auto Builder = Fixture.builder();
  const auto Declaration =
      Builder.addOperation({Signature.Key,
                            Signature.TypeParameters,
                            Signature.EffectVariables,
                            Signature.Constraints,
                            {},
                            Signature.Result,
                            Signature.ResultContract,
                            Signature.Effects,
                            declarationFingerprint(*Fixture.Types, Signature)});
  auto Module =
      Builder.finish(yona::typed_ir::ModulePhase::OperationInstantiated);
  const auto Domain = Module.MutationDomain->identity();
  const auto InstanceId = yona::typed_ir::OperationInstanceId(Domain, 0);
  const auto RowId = yona::typed_ir::RuntimeEffectRowId(Domain, 0);
  const yona::model::ClosedEffectOperation Closed{
      Signature.Key,    {}, {}, {}, Signature.Result, Signature.ResultContract,
      Signature.Effects};
  Module.OperationInstances.push_back(
      {InstanceId,
       Declaration,
       {},
       {},
       {},
       Signature.Result,
       Signature.ResultContract,
       Signature.Effects,
       RowId,
       runtimeFingerprint(*Fixture.Types, Closed)});
  Module.RuntimeEffectRows.push_back(
      {RowId, Signature.Effects, {InstanceId}, false, false, 0});
  REQUIRE(yona::typed_ir::verifyModule(Module).has_value());

  SUBCASE("declaration bytes") {
    Module.Operations.front().DeclarationFingerprint ^= 1;
    const auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::OperationSignature));
  }
  SUBCASE("closed runtime descriptor bytes") {
    Module.OperationInstances.front().RuntimeFingerprint ^= 1;
    const auto Result = yona::typed_ir::verifyModule(Module);
    REQUIRE_FALSE(Result);
    CHECK(hasRule(Result.error(),
                  yona::typed_ir::VerificationRule::OperationSignature));
  }
}
