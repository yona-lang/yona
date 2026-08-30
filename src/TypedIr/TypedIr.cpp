#include "yona/TypedIr/TypedIr.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace yona::typed_ir {
namespace {

void validateFacts(const semantics::NodeSemantics &Facts, ValueKind Kind,
                   ValueOwnershipKind Ownership,
                   const std::optional<ScalarConstant> &Constant) {
  if (Facts.InferredType.empty())
    throw std::invalid_argument("typed IR values require an inferred type");
  if (Facts.Ownership == semantics::OwnershipKind::Unknown)
    throw std::invalid_argument("typed IR values require known type ownership");
  if (Facts.Ownership == semantics::OwnershipKind::Linear &&
      Ownership == ValueOwnershipKind::Trivial)
    throw std::invalid_argument("linear typed IR values cannot be trivial");
  if (Ownership == ValueOwnershipKind::Transferred)
    throw std::invalid_argument(
        "new typed IR values cannot start in the transferred state");
  if (Kind == ValueKind::Constant && !Constant)
    throw std::invalid_argument("typed IR constants require a payload");
  if (Kind != ValueKind::Constant && Constant)
    throw std::invalid_argument(
        "only typed IR constants may carry a constant payload");
  if (Constant && Ownership != ValueOwnershipKind::Trivial)
    throw std::invalid_argument("typed IR scalar constants must be trivial");
  if (!Constant)
    return;
  const bool MatchesType =
      (std::holds_alternative<std::monostate>(*Constant) &&
       (Facts.InferredType == "()" || Facts.InferredType == "Unit")) ||
      (std::holds_alternative<bool>(*Constant) &&
       Facts.InferredType == "Bool") ||
      (std::holds_alternative<std::int64_t>(*Constant) &&
       Facts.InferredType == "Int") ||
      (std::holds_alternative<double>(*Constant) &&
       Facts.InferredType == "Float");
  if (!MatchesType)
    throw std::invalid_argument(
        "typed IR constant payload does not match its inferred type");
}

} // namespace

std::string_view valueOwnershipKindName(ValueOwnershipKind Kind) noexcept {
  switch (Kind) {
  case ValueOwnershipKind::Trivial:
    return "trivial";
  case ValueOwnershipKind::Borrowed:
    return "borrowed";
  case ValueOwnershipKind::Owned:
    return "owned";
  case ValueOwnershipKind::Transferred:
    return "transferred";
  }
  return "trivial";
}

Value::Value(ValueId Id, ValueKind Kind, std::string Name,
             semantics::NodeSemantics Facts, ValueOwnershipKind Ownership,
             SourceRange Range, std::optional<ScalarConstant> Constant)
    : Id(Id), Kind(Kind), Name(std::move(Name)), Facts(std::move(Facts)),
      Ownership(Ownership), Range(Range), Constant(std::move(Constant)) {
  validateFacts(this->Facts, this->Kind, this->Ownership, this->Constant);
}

ValueId Value::id() const noexcept { return Id; }
ValueKind Value::kind() const noexcept { return Kind; }
std::string_view Value::name() const noexcept { return Name; }
std::string_view Value::type() const noexcept { return Facts.InferredType; }
std::string_view Value::effects() const noexcept { return Facts.Effects; }
semantics::OwnershipKind Value::typeOwnership() const noexcept {
  return Facts.Ownership;
}
ValueOwnershipKind Value::ownership() const noexcept { return Ownership; }
SourceRange Value::range() const noexcept { return Range; }
const ScalarConstant *Value::constant() const noexcept {
  return Constant ? &*Constant : nullptr;
}

Function::Function(std::string Name, std::string Type, std::string Effects,
                   SourceRange Range)
    : Name(std::move(Name)), Type(std::move(Type)), Effects(std::move(Effects)),
      Range(Range) {
  if (this->Name.empty())
    throw std::invalid_argument("typed IR functions require a name");
  if (this->Type.empty())
    throw std::invalid_argument("typed IR functions require a type");
}

std::string_view Function::name() const noexcept { return Name; }
std::string_view Function::type() const noexcept { return Type; }
std::string_view Function::effects() const noexcept { return Effects; }
SourceRange Function::range() const noexcept { return Range; }
std::span<const Value> Function::values() const noexcept { return Values; }
std::span<const ValueId> Function::parameters() const noexcept {
  return Parameters;
}
std::optional<ValueId> Function::result() const noexcept { return Result; }

ValueId Function::appendValue(ValueKind Kind, std::string Name,
                              semantics::NodeSemantics Facts,
                              ValueOwnershipKind Ownership, SourceRange Range) {
  if (Kind == ValueKind::Constant)
    throw std::invalid_argument(
        "use appendConstant to create a typed IR constant");
  if (Values.size() >= (std::numeric_limits<std::uint32_t>::max)())
    throw std::overflow_error("typed IR function has too many values");
  const ValueId Id(static_cast<std::uint32_t>(Values.size()));
  Values.push_back(Value(Id, Kind, std::move(Name), std::move(Facts), Ownership,
                         Range, std::nullopt));
  if (Kind == ValueKind::Parameter)
    Parameters.push_back(Id);
  return Id;
}

ValueId Function::appendConstant(std::string Name,
                                 semantics::NodeSemantics Facts,
                                 ScalarConstant Constant, SourceRange Range) {
  if (Values.size() >= (std::numeric_limits<std::uint32_t>::max)())
    throw std::overflow_error("typed IR function has too many values");
  const ValueId Id(static_cast<std::uint32_t>(Values.size()));
  Values.push_back(Value(Id, ValueKind::Constant, std::move(Name),
                         std::move(Facts), ValueOwnershipKind::Trivial, Range,
                         std::move(Constant)));
  return Id;
}

ValueId Function::appendSemanticValue(ValueKind Kind, std::string Name,
                                      const semantics::SemanticModel &Model,
                                      const ast::AstNode *Node,
                                      ValueOwnershipKind Ownership,
                                      SourceRange Range) {
  const auto *Facts = Model.factsFor(Node);
  if (!Facts)
    throw std::invalid_argument(
        "typed IR value has no facts in the SemanticModel");
  return appendValue(Kind, std::move(Name), *Facts, Ownership, Range);
}

void Function::transfer(ValueId Id) {
  auto *Value = findMutableValue(Id);
  if (!Value)
    throw std::invalid_argument("cannot transfer an unknown typed IR value");
  if (Value->Ownership != ValueOwnershipKind::Owned)
    throw std::invalid_argument("only owned typed IR values can transfer");
  if (Result && *Result == Id)
    throw std::invalid_argument("a function result cannot be transferred");
  Value->Ownership = ValueOwnershipKind::Transferred;
}

void Function::setResult(ValueId Id) {
  const auto *Value = findValue(Id);
  if (!Value)
    throw std::invalid_argument("typed IR result must name a function value");
  if (Value->ownership() == ValueOwnershipKind::Transferred)
    throw std::invalid_argument("a transferred value cannot be a result");
  Result = Id;
}

const Value *Function::findValue(ValueId Id) const noexcept {
  if (!Id.isValid() || Id.value() >= Values.size())
    return nullptr;
  return &Values[Id.value()];
}

Value *Function::findMutableValue(ValueId Id) noexcept {
  return const_cast<Value *>(std::as_const(*this).findValue(Id));
}

Module::Module(std::string Name, SourceId Source)
    : Name(std::move(Name)), Source(Source) {
  if (this->Name.empty())
    throw std::invalid_argument("typed IR modules require a name");
  if (!this->Source.isValid())
    throw std::invalid_argument("typed IR modules require a valid SourceId");
}

std::string_view Module::name() const noexcept { return Name; }
SourceId Module::source() const noexcept { return Source; }
std::span<const Function> Module::functions() const noexcept {
  return Functions;
}

Function &Module::addFunction(Function FunctionValue) {
  if (findFunction(FunctionValue.name()))
    throw std::invalid_argument("typed IR function names must be unique");
  Functions.push_back(std::move(FunctionValue));
  return Functions.back();
}

const Function *Module::findFunction(std::string_view Name) const noexcept {
  for (const auto &FunctionValue : Functions)
    if (FunctionValue.name() == Name)
      return &FunctionValue;
  return nullptr;
}

} // namespace yona::typed_ir
