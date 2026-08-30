#include "yona/Codegen/TypedIrLowering.h"
#include "yona/TypedIr/TypedIr.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>

TEST_CASE("Codegen lowers typed IR without syntax") {
  yona::semantics::NodeSemantics Facts;
  Facts.InferredType = "Int";
  Facts.Effects = "{}";
  Facts.Ownership = yona::semantics::OwnershipKind::Unrestricted;

  yona::typed_ir::Module Input("DirectTypedIr", yona::SourceId(0));
  yona::typed_ir::Function Entry("answer", "() -> Int", "{}");
  const auto Constant = Entry.appendConstant("answer", Facts, std::int64_t{42},
                                             yona::SourceRange::unknown());
  Entry.setResult(Constant);
  Input.addFunction(std::move(Entry));

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
