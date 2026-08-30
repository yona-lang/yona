#ifndef YONA_CODEGEN_CODEGENSESSION_H
#define YONA_CODEGEN_CODEGENSESSION_H

#include "yona/Support/Diagnostic.h"

#include <llvm/IR/IRBuilder.h>

#include <memory>
#include <string>

namespace llvm {
class LLVMContext;
class Module;
class TargetMachine;
} // namespace llvm

namespace yona::compiler::codegen {

class DeriveEngine;

/// Owns the LLVM and diagnostic state for one compilation.
///
/// A session is intentionally non-copyable: every LLVM value emitted by a
/// Codegen instance is tied to this session's context and module. Destroying
/// the session releases the target machine, module, builder, context, and any
/// fallback diagnostic engine together, along with the session-local
/// derivation registry. An injected diagnostic engine remains caller-owned and
/// must outlive the session.
///
/// A session is not internally synchronized and is consumed on one thread.
/// Independent sessions share no mutable compilation state and may be used by
/// separate threads. If LLVM cannot create a native target machine,
/// targetMachine() returns nullptr and object emission fails normally.
class CodegenSession final {
public:
  explicit CodegenSession(std::string ModuleName = "yona_module",
                          compiler::DiagnosticEngine *Diagnostics = nullptr);
  ~CodegenSession();

  CodegenSession(const CodegenSession &) = delete;
  CodegenSession &operator=(const CodegenSession &) = delete;
  CodegenSession(CodegenSession &&) = delete;
  CodegenSession &operator=(CodegenSession &&) = delete;

  [[nodiscard]] llvm::LLVMContext &context() noexcept;
  [[nodiscard]] const llvm::LLVMContext &context() const noexcept;
  [[nodiscard]] llvm::Module &module() noexcept;
  [[nodiscard]] const llvm::Module &module() const noexcept;
  [[nodiscard]] llvm::IRBuilder<> &builder() noexcept;
  [[nodiscard]] const llvm::IRBuilder<> &builder() const noexcept;
  [[nodiscard]] llvm::TargetMachine *targetMachine() noexcept;
  [[nodiscard]] const llvm::TargetMachine *targetMachine() const noexcept;
  [[nodiscard]] compiler::DiagnosticEngine &diagnostics() noexcept;
  [[nodiscard]] const compiler::DiagnosticEngine &diagnostics() const noexcept;
  [[nodiscard]] DeriveEngine &derivations() noexcept;
  [[nodiscard]] const DeriveEngine &derivations() const noexcept;

  [[nodiscard]] int errorCount() const noexcept { return ErrorCount; }
  void recordError() noexcept { ++ErrorCount; }

private:
  void initializeTarget();

  std::unique_ptr<compiler::DiagnosticEngine> OwnedDiagnostics;
  compiler::DiagnosticEngine *Diagnostics;
  std::unique_ptr<DeriveEngine> Derivations;
  std::unique_ptr<llvm::LLVMContext> Context;
  std::unique_ptr<llvm::Module> ModuleValue;
  std::unique_ptr<llvm::IRBuilder<>> Builder;
  std::unique_ptr<llvm::TargetMachine> TargetMachineValue;
  int ErrorCount = 0;
};

} // namespace yona::compiler::codegen

#endif // YONA_CODEGEN_CODEGENSESSION_H
