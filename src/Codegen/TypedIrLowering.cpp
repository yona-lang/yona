#include "yona/Codegen/TypedIrLowering.h"

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
#include <string_view>
#include <vector>

namespace yona::compiler::codegen {
namespace {

llvm::Type *lowerScalarType(llvm::LLVMContext &Context, std::string_view Type,
                            bool AllowVoid) {
  if (Type == "()" || Type == "Unit") {
    if (!AllowVoid)
      throw std::invalid_argument(
          "Unit is not a supported typed IR parameter type");
    return llvm::Type::getVoidTy(Context);
  }
  if (Type == "Bool")
    return llvm::Type::getInt1Ty(Context);
  if (Type == "Int")
    return llvm::Type::getInt64Ty(Context);
  if (Type == "Float")
    return llvm::Type::getDoubleTy(Context);
  throw std::invalid_argument("unsupported typed IR scalar type: " +
                              std::string(Type));
}

llvm::Value *lowerConstant(llvm::LLVMContext &Context,
                           const typed_ir::Value &Value) {
  const auto *Constant = Value.constant();
  if (!Constant)
    throw std::invalid_argument("typed IR constant is missing its payload");
  if (const auto *Boolean = std::get_if<bool>(Constant))
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(Context), *Boolean);
  if (const auto *Integer = std::get_if<std::int64_t>(Constant))
    return llvm::ConstantInt::getSigned(llvm::Type::getInt64Ty(Context),
                                        *Integer);
  if (const auto *Float = std::get_if<double>(Constant))
    return llvm::ConstantFP::get(llvm::Type::getDoubleTy(Context), *Float);
  if (std::holds_alternative<std::monostate>(*Constant))
    return nullptr;
  throw std::invalid_argument("unsupported typed IR constant payload");
}

} // namespace

std::unique_ptr<llvm::Module> lowerTypedIrModule(const typed_ir::Module &Module,
                                                 llvm::LLVMContext &Context) {
  auto Result =
      std::make_unique<llvm::Module>(std::string(Module.name()), Context);

  for (const auto &FunctionValue : Module.functions()) {
    if (!FunctionValue.result())
      throw std::invalid_argument(
          "typed IR lowering requires a function result");
    const auto *ReturnValue = FunctionValue.findValue(*FunctionValue.result());
    if (!ReturnValue)
      throw std::invalid_argument("typed IR function result is unknown");
    if (ReturnValue->ownership() != typed_ir::ValueOwnershipKind::Trivial)
      throw std::invalid_argument(
          "bootstrap typed IR lowering supports only trivial results");

    std::vector<llvm::Type *> ParameterTypes;
    ParameterTypes.reserve(FunctionValue.parameters().size());
    for (const auto ParameterId : FunctionValue.parameters()) {
      const auto *Parameter = FunctionValue.findValue(ParameterId);
      if (!Parameter)
        throw std::invalid_argument("typed IR parameter is unknown");
      if (Parameter->ownership() != typed_ir::ValueOwnershipKind::Trivial)
        throw std::invalid_argument(
            "bootstrap typed IR lowering supports only trivial parameters");
      ParameterTypes.push_back(
          lowerScalarType(Context, Parameter->type(), false));
    }

    auto *ReturnType = lowerScalarType(Context, ReturnValue->type(), true);
    auto *Signature =
        llvm::FunctionType::get(ReturnType, ParameterTypes, false);
    auto *Function =
        llvm::Function::Create(Signature, llvm::Function::ExternalLinkage,
                               std::string(FunctionValue.name()), Result.get());

    std::size_t ParameterIndex = 0;
    for (auto &Argument : Function->args()) {
      const auto *Parameter =
          FunctionValue.findValue(FunctionValue.parameters()[ParameterIndex]);
      if (Parameter && !Parameter->name().empty())
        Argument.setName(Parameter->name());
      ++ParameterIndex;
    }

    auto *Block = llvm::BasicBlock::Create(Context, "entry", Function);
    llvm::IRBuilder<> Builder(Block);
    if (ReturnType->isVoidTy()) {
      if (ReturnValue->kind() != typed_ir::ValueKind::Constant ||
          !ReturnValue->constant() ||
          !std::holds_alternative<std::monostate>(*ReturnValue->constant()))
        throw std::invalid_argument(
            "Unit typed IR results require a Unit constant");
      Builder.CreateRetVoid();
      continue;
    }

    llvm::Value *LoweredResult = nullptr;
    if (ReturnValue->kind() == typed_ir::ValueKind::Constant) {
      LoweredResult = lowerConstant(Context, *ReturnValue);
    } else if (ReturnValue->kind() == typed_ir::ValueKind::Parameter) {
      for (std::size_t Index = 0; Index < FunctionValue.parameters().size();
           ++Index) {
        if (FunctionValue.parameters()[Index] == ReturnValue->id()) {
          LoweredResult = Function->getArg(static_cast<unsigned>(Index));
          break;
        }
      }
    }
    if (!LoweredResult)
      throw std::invalid_argument(
          "bootstrap typed IR lowering supports constants and parameter "
          "results only");
    Builder.CreateRet(LoweredResult);
  }

  std::string VerificationError;
  llvm::raw_string_ostream ErrorStream(VerificationError);
  if (llvm::verifyModule(*Result, &ErrorStream))
    throw std::runtime_error("invalid LLVM IR from typed IR: " +
                             ErrorStream.str());
  return Result;
}

} // namespace yona::compiler::codegen
