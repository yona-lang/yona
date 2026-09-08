#include "yona/Codegen/TypedIrLowering.h"
#include "yona/TypedIr/Builder.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <doctest/doctest.h>

TEST_CASE("Codegen lowers typed IR without syntax") {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto Source = Sources->addSource("direct.tir", "42");
  const yona::SourceRange Range{Source, 1, 1, 0, 2};
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
      {yona::model::ModuleIdentity("DirectTypedIr"),
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
      Builder.addFunction({"answer",
                           Range,
                           Signature,
                           {yona::typed_ir::FunctionDefinitionKind::Definition,
                            yona::typed_ir::FunctionVisibility::Private}});
  const auto Entry =
      Builder.createBlock(Function, {.DebugName = "entry", .Range = Range});
  const auto Constant =
      Builder.emit(Function, Entry,
                   yona::typed_ir::ConstantInst{yona::typed_ir::IntLiteral{42}},
                   {.Type = Int,
                    .Ownership = yona::typed_ir::OwnershipKind::Trivial,
                    .Range = Range,
                    .DebugName = "answer"});
  Builder.setTerminator(Function, Entry, yona::typed_ir::Return{Constant},
                        Range);
  auto Input = Builder.finish(yona::typed_ir::ModulePhase::Canonical);
  llvm::LLVMContext Context;
  auto Output = yona::compiler::codegen::lowerTypedIrModule(Input, Context);
  REQUIRE(Output);
  std::string Text;
  llvm::raw_string_ostream Stream(Text);
  Output->print(Stream, nullptr);
  Stream.flush();
  CHECK(Text.find("define i64 @answer()") != std::string::npos);
  CHECK(Text.find("ret i64 42") != std::string::npos);
}
