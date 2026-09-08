#ifndef YONA_TYPEDIR_INSTRUCTION_H
#define YONA_TYPEDIR_INSTRUCTION_H
#include "yona/Semantics/SemanticModel.h"
#include "yona/Support/SourceManager.h"
#include "yona/TypedIr/Representation.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>
namespace yona::typed_ir {
enum class UnaryOpcode { NegateInt, NegateFloat, LogicalNot, BitwiseNot };
enum class BinaryOpcode {
  AddInt,
  AddFloat,
  SubtractInt,
  SubtractFloat,
  MultiplyInt,
  MultiplyFloat,
  DivideIntSigned,
  DivideFloat,
  RemainderIntSigned,
  Equal,
  NotEqual,
  LessIntSigned,
  LessFloatOrdered,
  LessEqualIntSigned,
  LessEqualFloatOrdered,
  GreaterIntSigned,
  GreaterFloatOrdered,
  GreaterEqualIntSigned,
  GreaterEqualFloatOrdered
};
struct UnitLiteral {
  friend bool operator==(UnitLiteral, UnitLiteral) = default;
};
struct BoolLiteral {
  bool Value;
  friend bool operator==(BoolLiteral, BoolLiteral) = default;
};
struct ByteLiteral {
  std::uint8_t Value;
  friend bool operator==(ByteLiteral, ByteLiteral) = default;
};
struct CharLiteral {
  char32_t Value;
  friend bool operator==(CharLiteral, CharLiteral) = default;
};
struct IntLiteral {
  std::int64_t Value;
  friend bool operator==(IntLiteral, IntLiteral) = default;
};
struct FloatLiteral {
  std::uint64_t Ieee754Bits;
  friend bool operator==(FloatLiteral, FloatLiteral) = default;
};
struct StringLiteral {
  std::string Utf8;
  friend bool operator==(const StringLiteral &,
                         const StringLiteral &) = default;
};
struct SymbolLiteral {
  std::string Utf8;
  friend bool operator==(const SymbolLiteral &,
                         const SymbolLiteral &) = default;
};
using LiteralValue =
    std::variant<UnitLiteral, BoolLiteral, ByteLiteral, CharLiteral, IntLiteral,
                 FloatLiteral, StringLiteral, SymbolLiteral>;
struct ConstantInst {
  LiteralValue Value;
};
struct UnaryInst {
  UnaryOpcode Opcode;
  ValueId Operand;
};
struct BinaryInst {
  BinaryOpcode Opcode;
  ValueId Left;
  ValueId Right;
};
struct InjectSumInst {
  ValueId Alternative;
  model::TypeId Sum;
};
struct DirectCallInst {
  FunctionId Callee;
  std::vector<ValueId> Arguments;
  std::optional<ValueId> CallableEnvironment;
  std::optional<ValueId> BoundaryContext;
};
struct LexicalRefInst {
  semantics::BindingId Binding;
};
struct BranchTarget {
  EdgeId Edge;
  BlockId Block;
  std::vector<ValueId> Arguments;
};
struct EdgeResultContract {
  model::TypeId Type;
  OwnershipKind Ownership;
  std::optional<NominalTypeId> NominalIdentity;
  std::string DebugName;
};
struct ProducedBranchTarget {
  EdgeId Edge;
  BlockId Block;
  std::vector<EdgeResultContract> ProducedPrefix;
  std::vector<ValueId> Arguments;
};
struct TrapCompilerFailure {
  BranchTarget CleanupThenTrap;
};
using RuntimeFailureDisposition = TrapCompilerFailure;
struct Branch {
  BranchTarget Target;
};
struct CondBranch {
  ValueId Condition;
  BranchTarget TrueTarget;
  BranchTarget FalseTarget;
};
struct SwitchCase {
  std::int64_t Key;
  BranchTarget Target;
};
struct Switch {
  ValueId Scrutinee;
  std::vector<SwitchCase> Cases;
  BranchTarget Default;
};
struct Return {
  std::optional<ValueId> Result;
};
struct Raise {
  ValueId Exception;
};
struct OperationUse {
  OperationId Declaration;
  std::vector<model::TypeId> TypeArguments;
  std::vector<model::EffectRowId> EffectArguments;
};
using OperationReference = std::variant<OperationUse, OperationInstanceId>;
struct Perform {
  OperationReference Operation;
  std::vector<ValueId> Arguments;
  ProducedBranchTarget ResumeTarget;
  model::EffectRowId ResumeEffects;
};
struct Resume {
  ValueId Continuation;
  ValueId Argument;
};
struct Unreachable {};
using InstructionPayload =
    std::variant<ConstantInst, UnaryInst, BinaryInst, InjectSumInst,
                 DirectCallInst, LexicalRefInst>;
using Terminator = std::variant<Branch, CondBranch, Switch, Return, Raise,
                                Perform, Resume, Unreachable>;
void forEachOperand(const InstructionPayload &,
                    const std::function<void(ValueId)> &Visit);
void forEachOperand(const Terminator &,
                    const std::function<void(ValueId)> &Visit);
struct SuccessorView {
  EdgeId Edge;
  std::uint32_t Ordinal;
  BlockId Block;
  std::span<const EdgeResultContract> ProducedPrefix;
  std::span<const ValueId> Arguments;
};
void forEachSuccessor(const Terminator &,
                      const std::function<void(SuccessorView)> &Visit);
} // namespace yona::typed_ir
#endif
