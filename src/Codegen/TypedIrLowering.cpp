#include "yona/Codegen/TypedIrLowering.h"

#include "yona/TypedIr/Verifier.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace yona::compiler::codegen {
namespace {

llvm::Type *lowerScalarType(llvm::LLVMContext &Context,
                            const model::TypeTable &Types, model::TypeId Id,
                            bool AllowVoid) {
  if (!Types.valid(Id))
    throw std::invalid_argument("typed IR scalar type is invalid");
  const auto *Primitive = std::get_if<model::PrimitiveType>(&Types.at(Id));
  if (!Primitive)
    throw std::invalid_argument(
        "typed IR test lowering supports only primitive types");
  switch (*Primitive) {
  case model::PrimitiveType::Unit:
    if (!AllowVoid)
      throw std::invalid_argument("Unit cannot be a lowered parameter");
    return llvm::Type::getVoidTy(Context);
  case model::PrimitiveType::Bool:
    return llvm::Type::getInt1Ty(Context);
  case model::PrimitiveType::Int:
    return llvm::Type::getInt64Ty(Context);
  case model::PrimitiveType::Float:
    return llvm::Type::getDoubleTy(Context);
  default:
    throw std::invalid_argument(
        "primitive is outside the Task 3 test-lowering subset");
  }
}

llvm::Value *lowerConstant(llvm::LLVMContext &Context,
                           const typed_ir::ConstantInst &Constant) {
  return std::visit(
      [&](const auto &Literal) -> llvm::Value * {
        using T = std::decay_t<decltype(Literal)>;
        if constexpr (std::is_same_v<T, typed_ir::UnitLiteral>)
          return nullptr;
        if constexpr (std::is_same_v<T, typed_ir::BoolLiteral>)
          return llvm::ConstantInt::get(llvm::Type::getInt1Ty(Context),
                                        Literal.Value);
        if constexpr (std::is_same_v<T, typed_ir::IntLiteral>)
          return llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(Context),
                                              Literal.Value);
        if constexpr (std::is_same_v<T, typed_ir::FloatLiteral>)
          return llvm::ConstantFP::get(
              llvm::Type::getDoubleTy(Context),
              llvm::APFloat(llvm::APFloat::IEEEdouble(),
                            llvm::APInt(64, Literal.Ieee754Bits)));
        throw std::invalid_argument(
            "literal is outside the Task 3 test-lowering subset");
      },
      Constant.Value);
}

} // namespace

std::unique_ptr<llvm::Module> lowerTypedIrModule(const typed_ir::Module &Input,
                                                 llvm::LLVMContext &Context) {
  typed_ir::verifyOrThrow(Input);
  auto Result = std::make_unique<llvm::Module>(Input.Identity.fqn(), Context);
  std::vector<llvm::Function *> Functions(Input.Functions.size());
  for (const auto &F : Input.Functions) {
    const auto *Signature =
        std::get_if<model::FunctionType>(&Input.Types->at(F.Signature));
    std::vector<llvm::Type *> Parameters;
    for (const auto &P : Signature->Parameters)
      Parameters.push_back(
          lowerScalarType(Context, *Input.Types, P.Type, false));
    auto *Type = llvm::FunctionType::get(
        lowerScalarType(Context, *Input.Types, Signature->Result, true),
        Parameters, false);
    Functions[F.Id.value()] = llvm::Function::Create(
        Type, llvm::Function::ExternalLinkage, F.Name, Result.get());
  }
  for (const auto &F : Input.Functions) {
    if (F.Linkage.Kind != typed_ir::FunctionDefinitionKind::Definition)
      continue;
    if (F.Blocks.size() != 1 || !F.Blocks.front().End)
      throw std::invalid_argument(
          "Task 3 typed IR test lowering accepts one-block functions only");
    auto *Lowered = Functions[F.Id.value()];
    std::unordered_map<typed_ir::ValueId, llvm::Value *,
                       typed_ir::StrongIdHasher>
        Values;
    std::size_t Index = 0;
    for (auto &Argument : Lowered->args())
      Values.emplace(F.Parameters[Index++], &Argument);
    auto *Block =
        llvm::BasicBlock::Create(Context, F.Blocks.front().DebugName, Lowered);
    llvm::IRBuilder<> Builder(Block);
    for (const auto &Instruction : F.Blocks.front().Instructions) {
      if (!Instruction.Result)
        continue;
      const auto *Constant =
          std::get_if<typed_ir::ConstantInst>(&Instruction.Payload);
      if (!Constant)
        throw std::invalid_argument(
            "Task 3 typed IR test lowering accepts constants only");
      Values.emplace(*Instruction.Result, lowerConstant(Context, *Constant));
    }
    const auto *ReturnValue =
        std::get_if<typed_ir::Return>(&F.Blocks.front().End->Payload);
    if (!ReturnValue)
      throw std::invalid_argument(
          "Task 3 typed IR test lowering requires return terminators");
    if (!ReturnValue->Result)
      Builder.CreateRetVoid();
    else
      Builder.CreateRet(Values.at(*ReturnValue->Result));
  }
  std::string Error;
  llvm::raw_string_ostream Stream(Error);
  if (llvm::verifyModule(*Result, &Stream))
    throw std::runtime_error("invalid LLVM IR from typed IR: " + Stream.str());
  return Result;
}

} // namespace yona::compiler::codegen
