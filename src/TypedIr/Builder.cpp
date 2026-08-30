#include "yona/TypedIr/Builder.h"

#include <stdexcept>
#include <utility>

namespace yona::typed_ir {
namespace {

ValueOwnershipKind runtimeOwnership(semantics::OwnershipKind Ownership) {
  switch (Ownership) {
  case semantics::OwnershipKind::Unrestricted:
    return ValueOwnershipKind::Trivial;
  case semantics::OwnershipKind::Linear:
    return ValueOwnershipKind::Owned;
  case semantics::OwnershipKind::Unknown:
    throw std::invalid_argument(
        "typed IR entry requires known semantic ownership");
  }
  throw std::invalid_argument("typed IR entry has invalid semantic ownership");
}

} // namespace

Module buildEntryModule(const semantics::SemanticModel &Model,
                        std::string ModuleName, std::string EntryName) {
  const auto *Facts = Model.rootFacts();
  if (!Facts || Facts->InferredType.empty())
    throw std::invalid_argument(
        "typed IR entry requires inferred root semantic facts");

  Module Result(std::move(ModuleName), Model.sourceId());
  const std::string Effects = Facts->Effects.empty() ? "{}" : Facts->Effects;
  Function Entry(std::move(EntryName), "() -> " + Facts->InferredType, Effects,
                 Model.rootRange());
  const auto ResultValue =
      Entry.appendValue(ValueKind::Instruction, "result", *Facts,
                        runtimeOwnership(Facts->Ownership), Model.rootRange());
  Entry.setResult(ResultValue);
  Result.addFunction(std::move(Entry));
  return Result;
}

} // namespace yona::typed_ir
