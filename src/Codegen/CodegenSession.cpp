#include "yona/Codegen/CodegenSession.h"

#include "yona/Codegen/DeriveEngine.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <iostream>
#include <mutex>
#include <utility>

namespace yona::compiler::codegen {
namespace {

void initializeLlvmTargets() {
  static std::once_flag Once;
  std::call_once(Once, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
  });
}

} // namespace

CodegenSession::CodegenSession(std::string ModuleName,
                               compiler::DiagnosticEngine *Diagnostics)
    : Diagnostics(Diagnostics), Derivations(std::make_unique<DeriveEngine>()),
      Context(std::make_unique<llvm::LLVMContext>()),
      ModuleValue(
          std::make_unique<llvm::Module>(std::move(ModuleName), *Context)),
      Builder(std::make_unique<llvm::IRBuilder<>>(*Context)) {
  if (!this->Diagnostics) {
    OwnedDiagnostics = std::make_unique<compiler::DiagnosticEngine>();
    this->Diagnostics = OwnedDiagnostics.get();
  }
  initializeTarget();
}

CodegenSession::~CodegenSession() = default;

llvm::LLVMContext &CodegenSession::context() noexcept { return *Context; }

const llvm::LLVMContext &CodegenSession::context() const noexcept {
  return *Context;
}

llvm::Module &CodegenSession::module() noexcept { return *ModuleValue; }

const llvm::Module &CodegenSession::module() const noexcept {
  return *ModuleValue;
}

llvm::IRBuilder<> &CodegenSession::builder() noexcept { return *Builder; }

const llvm::IRBuilder<> &CodegenSession::builder() const noexcept {
  return *Builder;
}

llvm::TargetMachine *CodegenSession::targetMachine() noexcept {
  return TargetMachineValue.get();
}

const llvm::TargetMachine *CodegenSession::targetMachine() const noexcept {
  return TargetMachineValue.get();
}

compiler::DiagnosticEngine &CodegenSession::diagnostics() noexcept {
  return *Diagnostics;
}

const compiler::DiagnosticEngine &CodegenSession::diagnostics() const noexcept {
  return *Diagnostics;
}

DeriveEngine &CodegenSession::derivations() noexcept { return *Derivations; }

const DeriveEngine &CodegenSession::derivations() const noexcept {
  return *Derivations;
}

void CodegenSession::initializeTarget() {
  initializeLlvmTargets();

  const auto TripleName = llvm::sys::getDefaultTargetTriple();
  const llvm::Triple Triple(TripleName);
  ModuleValue->setTargetTriple(Triple);

  std::string Error;
  const auto *Target = llvm::TargetRegistry::lookupTarget(Triple, Error);
  if (!Target) {
    std::cerr << "Target error: " << Error << '\n';
    return;
  }

  llvm::TargetOptions Options;
  TargetMachineValue.reset(Target->createTargetMachine(
      Triple, "generic", "", Options, llvm::Reloc::PIC_));
  if (!TargetMachineValue) {
    std::cerr << "Target error: failed to create target machine for "
              << Triple.str() << '\n';
    return;
  }
  ModuleValue->setDataLayout(TargetMachineValue->createDataLayout());
}

} // namespace yona::compiler::codegen
