#include "yona/Model/CanonicalEncoding.h"
#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/Parser.h"
#include "yona/TypedIr/Printer.h"
#include "yona/TypedIr/Verifier.h"

#include <doctest/doctest.h>

#include <array>
#include <span>

namespace {

std::uint64_t declarationFingerprint(
    const yona::model::TypeTable &Types,
    const yona::model::EffectOperationSignature &Signature,
    std::span<const yona::model::CanonicalBinderDeclaration> Binders = {}) {
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

TEST_CASE("Typed IR text: canonical modules round trip byte-for-byte") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("empty.yona", "x");
  auto Types = std::make_shared<yona::model::TypeTable>();
  auto Primitive = [&](yona::model::PrimitiveType Type) {
    return Types->intern(Type).value();
  };
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
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
  (void)Primitive(yona::model::PrimitiveType::Unit);
  const auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
  CHECK(yona::typed_ir::verifyModule(*Parsed).has_value());
}

TEST_CASE("Typed IR text rejects malformed input with a located diagnostic") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("bad.tir", "not typed ir");
  auto Parsed = yona::typed_ir::parseModule("not typed ir", Sources);
  REQUIRE_FALSE(Parsed.has_value());
  REQUIRE(Parsed.error().size() == 1);
  CHECK(Parsed.error().front().Range.Source == Source);
}

TEST_CASE("Typed IR text preserves functions CFG and literal bytes") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("literals.yona", "literals");
  const yona::SourceRange Range{Source, 1, 1, 0, 8};
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Effects = Types->internEffect({}).value();
  auto Primitive = [&](yona::model::PrimitiveType Type) {
    return Types->intern(Type).value();
  };
  const auto Unit = Primitive(yona::model::PrimitiveType::Unit);
  const auto Bool = Primitive(yona::model::PrimitiveType::Bool);
  const auto Byte = Primitive(yona::model::PrimitiveType::Byte);
  const auto Char = Primitive(yona::model::PrimitiveType::Char);
  const auto Int = Primitive(yona::model::PrimitiveType::Int);
  const auto Float = Primitive(yona::model::PrimitiveType::Float);
  const auto String = Primitive(yona::model::PrimitiveType::String);
  const auto Symbol = Primitive(yona::model::PrimitiveType::Symbol);
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
                                 Effects,
                                 yona::model::CallingConvention::DirectYona,
                                 std::nullopt,
                                 std::nullopt})
                             .value();
  yona::typed_ir::Builder Builder(
      {yona::model::ModuleIdentity("Literals"),
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
      Builder.addFunction({"literals",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  auto Emit = [&](yona::typed_ir::LiteralValue Literal,
                  yona::model::TypeId Type,
                  yona::typed_ir::OwnershipKind Ownership, std::string Name) {
    return Builder.emit(Function, Entry,
                        yona::typed_ir::ConstantInst{std::move(Literal)},
                        {.Type = Type,
                         .Ownership = Ownership,
                         .Range = Range,
                         .DebugName = std::move(Name)});
  };
  Emit(yona::typed_ir::UnitLiteral{}, Unit,
       yona::typed_ir::OwnershipKind::Trivial, "unit");
  Emit(yona::typed_ir::BoolLiteral{true}, Bool,
       yona::typed_ir::OwnershipKind::Trivial, "bool");
  Emit(yona::typed_ir::ByteLiteral{0xff}, Byte,
       yona::typed_ir::OwnershipKind::Trivial, "byte");
  Emit(yona::typed_ir::CharLiteral{U'λ'}, Char,
       yona::typed_ir::OwnershipKind::Trivial, "char");
  const auto Result = Emit(yona::typed_ir::IntLiteral{-42}, Int,
                           yona::typed_ir::OwnershipKind::Trivial, "int");
  Emit(yona::typed_ir::FloatLiteral{0x7ff8000000000042ULL}, Float,
       yona::typed_ir::OwnershipKind::Trivial, "float");
  Emit(yona::typed_ir::StringLiteral{std::string("a\0b", 3)}, String,
       yona::typed_ir::OwnershipKind::Owned, "string");
  Emit(yona::typed_ir::SymbolLiteral{std::string("x\0y", 3)}, Symbol,
       yona::typed_ir::OwnershipKind::Trivial, "symbol");
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Result}, Range);
  const auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE("Typed IR text preserves canonical declaration catalogs") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("catalog.yona", "catalog");
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Empty = Types->internEffect({}).value();
  const auto Int = Types->intern(yona::model::PrimitiveType::Int).value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
  yona::typed_ir::Builder Builder(
      {yona::model::ModuleIdentity("Catalog"),
       Types,
       {Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
        Opaque(yona::model::AbiOpaqueKind::EffectRequest),
        Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
        Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
        Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
        Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)},
       Sources,
       Source});
  const auto Box =
      Builder.addNominal({{yona::model::ModuleIdentity("Catalog"), "Box"}, {}});
  Builder.addConstructor(Box, {"Box", 0, {Int}});
  Builder.addResource({{yona::model::ModuleIdentity("Catalog"), "Handle"},
                       {},
                       yona::model::ResourceAbiKind::GenericResource,
                       yona::model::ResourceShareability::Linear,
                       std::nullopt,
                       "release_handle"});
  const yona::model::EffectOperationSignature OperationSignature{
      {{yona::model::ModuleIdentity("Catalog"), "State"}, "get"},
      {},
      {},
      {},
      {{Int, yona::model::ParameterOwnership::Trivial}},
      Int,
      yona::model::ResultOwnership::Trivial,
      Empty};
  Builder.addOperation({OperationSignature.Key,
                        OperationSignature.TypeParameters,
                        OperationSignature.EffectVariables,
                        OperationSignature.Constraints,
                        {{Int, yona::model::ParameterOwnership::Trivial}},
                        OperationSignature.Result,
                        OperationSignature.ResultContract,
                        OperationSignature.Effects,
                        declarationFingerprint(*Types, OperationSignature)});
  Builder.addGlobal({{yona::model::ModuleIdentity("Catalog"), "answer"},
                     Int,
                     yona::typed_ir::OwnershipKind::Trivial});
  const auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE("Typed IR text preserves perform produced successor contracts") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("perform.yona", "perform");
  const yona::SourceRange Range{Source, 1, 1, 0, 7};
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Empty = Types->internEffect({}).value();
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
      {yona::model::ModuleIdentity("Effects"),
       Types,
       {Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
        Opaque(yona::model::AbiOpaqueKind::EffectRequest),
        Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
        Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
        Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
        Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)},
       Sources,
       Source});
  const yona::model::EffectOperationSignature OperationSignature{
      {{yona::model::ModuleIdentity("Effects"), "State"}, "get"},
      {},
      {},
      {},
      {},
      Int,
      yona::model::ResultOwnership::Trivial,
      Empty};
  const auto Operation = Builder.addOperation(
      {OperationSignature.Key,
       {},
       {},
       {},
       {},
       Int,
       yona::model::ResultOwnership::Trivial,
       Empty,
       declarationFingerprint(*Types, OperationSignature)});
  const auto Function =
      Builder.addFunction({"performGet",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  const auto Resume =
      Builder.createBlock(Function, {.DebugName = "resume", .Range = Range});
  const auto Result = Builder.addBlockArgument(
      Function, Resume,
      {.Type = Int,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Range,
       .DebugName = "result"});
  Builder.setTerminator(
      Function, Entry,
      yona::typed_ir::Perform{yona::typed_ir::OperationUse{Operation, {}, {}},
                              {},
                              {{},
                               Resume,
                               {{Int, yona::typed_ir::OwnershipKind::Trivial,
                                 std::nullopt, "result"}},
                               {}},
                              Empty},
      Range);
  Builder.setTerminator(Function, Resume, yona::typed_ir::Return{Result},
                        Range);
  const auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE(
    "Typed IR text preserves generic captures binders and borrow provenance") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("generic.yona", "generic");
  const yona::SourceRange Range{Source, 1, 1, 0, 7};
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Parameter = Types->allocateTypeParameter();
  const auto Empty = Types->internEffect({}).value();
  const auto Int = Types->intern(yona::model::PrimitiveType::Int).value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
  const auto Signature =
      Types
          ->intern(yona::model::FunctionType{
              {},
              {},
              {},
              {{Int, yona::model::ParameterOwnership::Borrow}},
              Int,
              yona::model::ResultOwnership::Trivial,
              Empty,
              yona::model::CallingConvention::DirectYona,
              std::nullopt,
              std::nullopt})
          .value();
  yona::typed_ir::Builder Builder(
      {yona::model::ModuleIdentity("Generic"),
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
      Builder.addFunction({"capturing",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  Builder.addParameter(
      Function, {.Type = Int,
                 .Ownership = yona::typed_ir::OwnershipKind::Borrowed,
                 .Borrow =
                     yona::typed_ir::BorrowProvenance{
                         yona::typed_ir::BorrowProvenanceKind::BorrowParameter,
                         std::nullopt},
                 .Range = Range,
                 .DebugName = "borrowed"});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  const auto Captured = Builder.emit(
      Function, Entry,
      yona::typed_ir::LexicalRefInst{yona::semantics::BindingId(17)},
      {.Type = Int,
       .Ownership = yona::typed_ir::OwnershipKind::Trivial,
       .Range = Range,
       .DebugName = "captured"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Captured},
                        Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  Module.Functions.front().GenericBinders =
      yona::model::GenericBinderEnvironment{{}, {}, {Parameter}, {}};
  Module.Functions.front().GenericCaptures.push_back(
      {yona::semantics::BindingId(17), Int,
       yona::model::ParameterOwnership::Trivial, "captured"});
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE(
    "Typed IR text preserves runtime rows boundaries and hidden parameters") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("runtime.yona", "runtime");
  const yona::SourceRange Range{Source, 1, 1, 0, 7};
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Empty = Types->internEffect({}).value();
  const auto Int = Types->intern(yona::model::PrimitiveType::Int).value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
  yona::typed_ir::BuiltinTypeIds Builtins{
      Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
      Opaque(yona::model::AbiOpaqueKind::EffectRequest),
      Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
      Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
      Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
      Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)};
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
  yona::typed_ir::Builder Builder({yona::model::ModuleIdentity("Runtime"),
                                   Types, Builtins, Sources, Source});
  const auto Marker = Builder.addNominal(
      {{yona::model::ModuleIdentity("Runtime"), "Marker"}, {}});
  const yona::model::EffectOperationSignature OperationSignature{
      {{yona::model::ModuleIdentity("Runtime"), "State"}, "get"},
      {},
      {},
      {},
      {},
      Int,
      yona::model::ResultOwnership::Trivial,
      Empty};
  const auto Operation = Builder.addOperation(
      {OperationSignature.Key,
       {},
       {},
       {},
       {},
       Int,
       yona::model::ResultOwnership::Trivial,
       Empty,
       declarationFingerprint(*Types, OperationSignature)});
  const auto Function =
      Builder.addFunction({"prepared",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  auto Hidden = [&](yona::model::TypeId Type, std::string Name) {
    return Builder.addParameter(
        Function,
        {.Type = Type,
         .Ownership = yona::typed_ir::OwnershipKind::Borrowed,
         .Borrow =
             yona::typed_ir::BorrowProvenance{
                 yona::typed_ir::BorrowProvenanceKind::BorrowParameter,
                 std::nullopt},
         .Range = Range,
         .DebugName = std::move(Name)});
  };
  const auto Execution = Hidden(Builtins.ExecutionContext, "execution");
  const auto Callable =
      Hidden(Builtins.CallableInvocationEnvironment, "callable");
  const auto Boundary =
      Hidden(Builtins.ContinuationBoundaryContext, "boundary");
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  const auto Result =
      Builder.emit(Function, Entry,
                   yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{1}},
                   {.Type = Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Range,
                    .DebugName = "result"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Result}, Range);
  auto Module = Builder.finish(yona::typed_ir::ModulePhase::EffectOutlined);
  const auto Domain = Module.MutationDomain->identity();
  const auto Instance = yona::typed_ir::OperationInstanceId(Domain, 0);
  const auto RuntimeRow = yona::typed_ir::RuntimeEffectRowId(Domain, 0);
  const yona::model::ClosedEffectOperation ClosedOperation{
      OperationSignature.Key,
      {},
      {},
      {},
      Int,
      yona::model::ResultOwnership::Trivial,
      Empty};
  Module.OperationInstances.push_back(
      {Instance,
       Operation,
       {},
       {},
       {},
       Int,
       yona::model::ResultOwnership::Trivial,
       Empty,
       RuntimeRow,
       runtimeFingerprint(*Types, ClosedOperation)});
  Module.RuntimeEffectRows.push_back(
      {RuntimeRow, Empty, {Instance}, false, false, 22});
  const auto Handler = yona::typed_ir::HandlerBoundaryDescriptorId(Domain, 0);
  const auto Contract = yona::typed_ir::BoundaryContextContractId(Domain, 0);
  const auto Requirements = yona::typed_ir::BoundaryRequirementSetId(Domain, 0);
  Module.BoundaryContextContracts.push_back({Contract, {Handler}});
  Module.BoundaryRequirementSets.push_back({Requirements, {Handler}});
  auto &Prepared = Module.Functions.front();
  Prepared.RuntimeEffects = RuntimeRow;
  Prepared.ExecutionContextParameter = Execution;
  Prepared.CallableEnvironmentParameter = Callable;
  Prepared.BoundaryContextParameter = Boundary;
  Prepared.BoundaryContextContract = Contract;
  Prepared.AmbientBoundaryRequirements = Requirements;
  Prepared.Values[Result.value()].Representation =
      yona::typed_ir::PhysicalRepresentation::I64;
  Prepared.Values[Result.value()].NominalIdentity = Marker;
  Prepared.Values[Result.value()].CallableBoundaryRequirements = Requirements;
  Prepared.Blocks.front().ControlRegion =
      yona::typed_ir::ControlRegionId(Prepared.LocalDomain->identity(), 0);
  CHECK_NOTHROW(yona::typed_ir::verifyOrThrow(Module));
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE("Typed IR text preserves generic declaration binders") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("generic-catalog.yona", "generic");
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto NominalParameter = Types->allocateTypeParameter();
  const auto ResourceParameter = Types->allocateTypeParameter();
  const auto OperationParameter = Types->allocateTypeParameter();
  const auto OperationEffect = Types->allocateEffectVariable();
  const auto NominalParameterType =
      Types->intern(yona::model::TypeParameterType{NominalParameter}).value();
  const auto OperationParameterType =
      Types->intern(yona::model::TypeParameterType{OperationParameter}).value();
  const auto Effects =
      Types
          ->internEffect({{},
                          {{OperationEffect,
                            yona::model::EffectVariableKind::Flexible,
                            {}}},
                          false,
                          false})
          .value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
  yona::typed_ir::Builder Builder(
      {yona::model::ModuleIdentity("GenericCatalog"),
       Types,
       {Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
        Opaque(yona::model::AbiOpaqueKind::EffectRequest),
        Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
        Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
        Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
        Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)},
       Sources,
       Source});
  const auto Box = Builder.addNominal(
      {{yona::model::ModuleIdentity("GenericCatalog"), "Box"},
       {NominalParameter}});
  Builder.addConstructor(Box, {"Box", 0, {NominalParameterType}});
  Builder.addResource(
      {{yona::model::ModuleIdentity("GenericCatalog"), "Handle"},
       {ResourceParameter},
       yona::model::ResourceAbiKind::GenericResource,
       yona::model::ResourceShareability::Linear,
       std::nullopt,
       "release_handle"});
  const yona::model::EffectOperationSignature OperationSignature{
      {{yona::model::ModuleIdentity("GenericCatalog"), "State"}, "get"},
      {OperationParameter},
      {{OperationEffect, yona::model::EffectVariableKind::Flexible}},
      {{OperationParameter,
        {yona::model::ModuleIdentity("Std\\Core"), "Eq"},
        {OperationParameterType}}},
      {{OperationParameterType, yona::model::ParameterOwnership::Trivial}},
      OperationParameterType,
      yona::model::ResultOwnership::Trivial,
      Effects};
  const std::array Binders{
      yona::model::CanonicalBinderDeclaration{
          yona::model::BinderOwnerKind::Nominal,
          yona::model::NominalTypeKey{
              yona::model::ModuleIdentity("GenericCatalog"), "Box"},
          {NominalParameter},
          {}},
      yona::model::CanonicalBinderDeclaration{
          yona::model::BinderOwnerKind::Resource,
          yona::model::NominalTypeKey{
              yona::model::ModuleIdentity("GenericCatalog"), "Handle"},
          {ResourceParameter},
          {}},
      yona::model::CanonicalBinderDeclaration{
          yona::model::BinderOwnerKind::Operation,
          OperationSignature.Key,
          {OperationParameter},
          OperationSignature.EffectVariables}};
  Builder.addOperation(
      {OperationSignature.Key,
       OperationSignature.TypeParameters,
       OperationSignature.EffectVariables,
       OperationSignature.Constraints,
       {{OperationParameterType, yona::model::ParameterOwnership::Trivial}},
       OperationSignature.Result,
       OperationSignature.ResultContract,
       OperationSignature.Effects,
       declarationFingerprint(*Types, OperationSignature, Binders)});
  const auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  const auto First = yona::typed_ir::printModule(Module);
  auto Parsed = yona::typed_ir::parseModule(First, Sources);
  REQUIRE(Parsed.has_value());
  REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
  CHECK(yona::typed_ir::printModule(*Parsed) == First);
}

TEST_CASE(
    "Typed IR text: malformed parser corpus returns located diagnostics") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("malformed.tir", "x");
  auto Types = std::make_shared<yona::model::TypeTable>();
  const auto Empty = Types->internEffect({}).value();
  const auto Unit = Types->intern(yona::model::PrimitiveType::Unit).value();
  auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
    return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
  };
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
  const auto Signature = Types
                             ->intern(yona::model::FunctionType{
                                 {},
                                 {},
                                 {},
                                 {},
                                 Unit,
                                 yona::model::ResultOwnership::Trivial,
                                 Empty,
                                 yona::model::CallingConvention::DirectYona,
                                 std::nullopt,
                                 std::nullopt})
                             .value();
  const yona::SourceRange Range{Source, 1, 1, 0, 1};
  const auto Function =
      Builder.addFunction({"malformed",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  Builder.addBlockArgument(Function, Entry,
                           {.Type = Unit,
                            .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                            .Range = Range,
                            .DebugName = "argument"});
  Builder.emit(Function, Entry,
               yona::typed_ir::ConstantInst{yona::typed_ir::UnitLiteral{}},
               {.Type = Unit,
                .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                .Range = Range,
                .DebugName = "result"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{}, Range);
  const auto Valid = yona::typed_ir::printModule(
      Builder.finish(yona::typed_ir::ModulePhase::Canonical));
  std::vector<std::string> Invalid = {"", "tir 999", "tir 1\nmodule zz",
                                      Valid + "trailing\n"};
  auto ForeignSource = Valid;
  ForeignSource.replace(ForeignSource.find("source 0"), 8, "source 999");
  Invalid.push_back(std::move(ForeignSource));
  auto BadModule = Valid;
  BadModule.replace(BadModule.find("module 4d61696e"), 15, "module 626164");
  Invalid.push_back(std::move(BadModule));
  auto OversizedValue = Valid;
  const auto Argument = OversizedValue.find("arg 0 ");
  REQUIRE(Argument != std::string::npos);
  OversizedValue.replace(Argument, 6, "arg 4294967295 ");
  Invalid.push_back(std::move(OversizedValue));
  auto OversizedResult = Valid;
  const auto Instruction = OversizedResult.find("inst 1 ");
  REQUIRE(Instruction != std::string::npos);
  OversizedResult.replace(Instruction, 7, "inst 4294967295 ");
  Invalid.push_back(std::move(OversizedResult));
  auto OversizedCount = Valid;
  const auto Captures = OversizedCount.find("captures 0");
  REQUIRE(Captures != std::string::npos);
  OversizedCount.replace(Captures, 10, "captures 1048577");
  Invalid.push_back(std::move(OversizedCount));
  auto OversizedTypeTable = Valid;
  const auto TypePayload = OversizedTypeTable.find("types ") + 6;
  REQUIRE(TypePayload >= 6);
  const auto TypePayloadEnd = OversizedTypeTable.find('\n', TypePayload);
  REQUIRE(TypePayloadEnd != std::string::npos);
  OversizedTypeTable.replace(TypePayload, TypePayloadEnd - TypePayload,
                             "0200000001001000");
  Invalid.push_back(std::move(OversizedTypeTable));
  auto OversizedEffectTable = Valid;
  const auto EffectPayload = OversizedEffectTable.find("effects ") + 8;
  REQUIRE(EffectPayload >= 8);
  const auto EffectPayloadEnd = OversizedEffectTable.find('\n', EffectPayload);
  REQUIRE(EffectPayloadEnd != std::string::npos);
  OversizedEffectTable.replace(EffectPayload, EffectPayloadEnd - EffectPayload,
                               "0200000001001000");
  Invalid.push_back(std::move(OversizedEffectTable));
  for (const auto &Text : Invalid) {
    CAPTURE(Text);
    std::optional<yona::typed_ir::ParseModuleResult> Parsed;
    CHECK_NOTHROW(Parsed.emplace(yona::typed_ir::parseModule(Text, Sources)));
    REQUIRE(Parsed.has_value());
    REQUIRE_FALSE(Parsed->has_value());
    REQUIRE_FALSE(Parsed->error().empty());
    CHECK(Parsed->error().front().Range.Source == Source);
    CHECK_FALSE(Parsed->error().front().Message.empty());
  }
}
