#ifndef YONA_TYPEDIR_VERIFIER_H
#define YONA_TYPEDIR_VERIFIER_H
#include "yona/TypedIr/TypedIr.h"

#include <expected>
namespace yona::typed_ir {
enum class VerificationRule {
  InvalidId,
  MissingTerminator,
  DuplicateDefinition,
  DefinitionBeforeUse,
  Dominance,
  SuccessorArity,
  SuccessorType,
  SuccessorOwnership,
  ConditionType,
  ReturnType,
  CallSignature,
  Representation,
  OperationSignature,
  IllegalPhaseOperation
};
struct VerificationIssue {
  SourceRange Range;
  VerificationRule Rule;
  std::string Message;
};
struct PassDiagnostic {
  SourceRange Range;
  std::string Pass;
  std::string Message;
};
using PassResult = std::expected<void, std::vector<PassDiagnostic>>;
struct VerifyOptions {
  bool VerifyDominance = true;
  bool VerifyPhase = true;
};
using VerificationResult = std::expected<void, std::vector<VerificationIssue>>;
VerificationResult verifyModule(const Module &, VerifyOptions = {});
void verifyOrThrow(const Module &, VerifyOptions = {});
} // namespace yona::typed_ir
#endif
