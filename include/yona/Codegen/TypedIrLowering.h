#ifndef YONA_CODEGEN_TYPEDIRLOWERING_H
#define YONA_CODEGEN_TYPEDIRLOWERING_H

#include "yona/TypedIr/TypedIr.h"

#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace yona::compiler::codegen {

/// Lower ownership-explicit typed IR directly to LLVM IR.
///
/// The bootstrap lowering accepts scalar Unit/Bool/Int/Float constants and
/// identity-style parameter results. Unsupported values fail explicitly; it
/// never reparses source or consults syntax nodes.
///
/// Ownership:
/// - Input is borrowed and remains unchanged.
/// - The returned LLVM module uniquely owns all emitted IR and borrows Context
///   for its lifetime.
///
/// Failure:
/// - Throws std::invalid_argument for unsupported or inconsistent typed IR.
/// - Throws std::runtime_error if LLVM verification fails.
///
/// Thread safety:
/// - Safe for concurrent calls using distinct LLVM contexts.
[[nodiscard]] std::unique_ptr<llvm::Module>
lowerTypedIrModule(const typed_ir::Module &Module, llvm::LLVMContext &Context);

} // namespace yona::compiler::codegen

#endif // YONA_CODEGEN_TYPEDIRLOWERING_H
