#include "yona/TypedIr/Builder.h"
#include "yona/TypedIr/Parser.h"
#include "yona/TypedIr/Printer.h"
#include "yona/TypedIr/Verifier.h"

#include <doctest/doctest.h>

TEST_CASE("Typed IR property: seeded well-typed modules round trip") {
  for (std::uint32_t Seed = 0; Seed != 256; ++Seed) {
    CAPTURE(Seed);
    auto Sources = std::make_shared<yona::SourceManager>();
    const auto Source = Sources->addSource("property.yona", "property");
    const yona::SourceRange Range{Source, 1, 1, 0, 8};
    auto Types = std::make_shared<yona::model::TypeTable>();
    const auto Empty = Types->internEffect({}).value();
    const auto Bool = Types->intern(yona::model::PrimitiveType::Bool).value();
    const auto Int = Types->intern(yona::model::PrimitiveType::Int).value();
    auto Opaque = [&](yona::model::AbiOpaqueKind Kind) {
      return Types->intern(yona::model::AbiOpaqueType{Kind}).value();
    };
    std::vector<yona::model::FunctionParameter> Parameters;
    if (Seed & 1)
      Parameters.push_back({Int, yona::model::ParameterOwnership::Trivial});
    const auto Signature = Types
                               ->intern(yona::model::FunctionType{
                                   {},
                                   {},
                                   {},
                                   Parameters,
                                   Int,
                                   yona::model::ResultOwnership::Trivial,
                                   Empty,
                                   yona::model::CallingConvention::DirectYona,
                                   std::nullopt,
                                   std::nullopt})
                               .value();
    yona::typed_ir::Builder Builder(
        {yona::model::ModuleIdentity("Property"),
         Types,
         {Opaque(yona::model::AbiOpaqueKind::ExceptionValue),
          Opaque(yona::model::AbiOpaqueKind::EffectRequest),
          Opaque(yona::model::AbiOpaqueKind::ControlOutcome),
          Opaque(yona::model::AbiOpaqueKind::ExecutionContext),
          Opaque(yona::model::AbiOpaqueKind::CallableInvocationEnvironment),
          Opaque(yona::model::AbiOpaqueKind::ContinuationBoundaryContext)},
         Sources,
         Source});
    const auto Function = Builder.addFunction(
        {"seed" + std::to_string(Seed),
         Range,
         Signature,
         {yona::typed_ir::FunctionDefinitionKind::Definition,
          yona::typed_ir::FunctionVisibility::Private}});
    std::optional<yona::typed_ir::ValueId> Parameter;
    if (Seed & 1)
      Parameter = Builder.addParameter(
          Function, {.Type = Int,
                     .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                     .Range = Range,
                     .DebugName = "input"});
    const auto Entry =
        Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
    if (Seed % 3 == 0) {
      auto Result = Parameter;
      if (!Result)
        Result = Builder.emit(
            Function, Entry,
            yona::typed_ir::ConstantInst{
                yona::typed_ir::IntLiteral{static_cast<std::int64_t>(Seed)}},
            {.Type = Int,
             .Ownership = yona::typed_ir::OwnershipKind::Trivial,
             .Range = Range,
             .DebugName = "constant"});
      Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Result},
                            Range);
    } else {
      const auto Then =
          Builder.createBlock(Function, {.DebugName = "then", .Range = Range});
      const auto Otherwise = Builder.createBlock(
          Function, {.DebugName = "otherwise", .Range = Range});
      const auto Condition =
          Builder.emit(Function, Entry,
                       yona::typed_ir::ConstantInst{
                           yona::typed_ir::BoolLiteral{(Seed & 2) != 0}},
                       {.Type = Bool,
                        .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                        .Range = Range,
                        .DebugName = "condition"});
      const auto Left =
          Builder.emit(Function, Then,
                       yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{
                           static_cast<std::int64_t>(Seed)}},
                       {.Type = Int,
                        .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                        .Range = Range,
                        .DebugName = "left"});
      const auto Right =
          Builder.emit(Function, Otherwise,
                       yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{
                           -static_cast<std::int64_t>(Seed)}},
                       {.Type = Int,
                        .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                        .Range = Range,
                        .DebugName = "right"});
      Builder.setTerminator(Function, Entry,
                            yona::typed_ir::CondBranch{
                                Condition, {{}, Then, {}}, {{}, Otherwise, {}}},
                            Range);
      Builder.setTerminator(Function, Then, yona::typed_ir::Return{Left},
                            Range);
      Builder.setTerminator(Function, Otherwise, yona::typed_ir::Return{Right},
                            Range);
    }
    auto Module = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
    REQUIRE(yona::typed_ir::verifyModule(Module).has_value());
    const auto First = yona::typed_ir::printModule(Module);
    auto Parsed = yona::typed_ir::parseModule(First, Sources);
    REQUIRE(Parsed.has_value());
    REQUIRE(yona::typed_ir::verifyModule(*Parsed).has_value());
    const auto Second = yona::typed_ir::printModule(*Parsed);
    CHECK(Second == First);
    auto ParsedAgain = yona::typed_ir::parseModule(Second, Sources);
    REQUIRE(ParsedAgain.has_value());
    CHECK(yona::typed_ir::printModule(*ParsedAgain) == First);
  }
}
