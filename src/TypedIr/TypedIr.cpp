#include "yona/TypedIr/TypedIr.h"

#include <type_traits>

namespace yona::typed_ir {

std::string_view valueOwnershipKindName(OwnershipKind Kind) noexcept {
  switch (Kind) {
  case OwnershipKind::Trivial:
    return "trivial";
  case OwnershipKind::Borrowed:
    return "borrowed";
  case OwnershipKind::Owned:
    return "owned";
  }
  return "invalid";
}

void forEachOperand(const InstructionPayload &Payload,
                    const std::function<void(ValueId)> &Visit) {
  std::visit(
      [&](const auto &Inst) {
        using T = std::decay_t<decltype(Inst)>;
        if constexpr (std::is_same_v<T, UnaryInst>)
          Visit(Inst.Operand);
        else if constexpr (std::is_same_v<T, BinaryInst>) {
          Visit(Inst.Left);
          Visit(Inst.Right);
        } else if constexpr (std::is_same_v<T, InjectSumInst>)
          Visit(Inst.Alternative);
        else if constexpr (std::is_same_v<T, DirectCallInst>) {
          for (auto Id : Inst.Arguments)
            Visit(Id);
          if (Inst.CallableEnvironment)
            Visit(*Inst.CallableEnvironment);
          if (Inst.BoundaryContext)
            Visit(*Inst.BoundaryContext);
        } else {
          static_assert(std::is_same_v<T, ConstantInst> ||
                        std::is_same_v<T, LexicalRefInst>);
        }
      },
      Payload);
}

void forEachOperand(const Terminator &Payload,
                    const std::function<void(ValueId)> &Visit) {
  std::visit(
      [&](const auto &End) {
        using T = std::decay_t<decltype(End)>;
        auto VisitTarget = [&](const BranchTarget &Target) {
          for (auto Id : Target.Arguments)
            Visit(Id);
        };
        if constexpr (std::is_same_v<T, Branch>)
          VisitTarget(End.Target);
        else if constexpr (std::is_same_v<T, CondBranch>) {
          Visit(End.Condition);
          VisitTarget(End.TrueTarget);
          VisitTarget(End.FalseTarget);
        } else if constexpr (std::is_same_v<T, Switch>) {
          Visit(End.Scrutinee);
          for (const auto &Case : End.Cases)
            VisitTarget(Case.Target);
          VisitTarget(End.Default);
        } else if constexpr (std::is_same_v<T, Return>) {
          if (End.Result)
            Visit(*End.Result);
        } else if constexpr (std::is_same_v<T, Raise>)
          Visit(End.Exception);
        else if constexpr (std::is_same_v<T, Perform>) {
          for (auto Id : End.Arguments)
            Visit(Id);
          for (auto Id : End.ResumeTarget.Arguments)
            Visit(Id);
        } else if constexpr (std::is_same_v<T, Resume>) {
          Visit(End.Continuation);
          Visit(End.Argument);
        } else
          static_assert(std::is_same_v<T, Unreachable>);
      },
      Payload);
}

void forEachSuccessor(const Terminator &Payload,
                      const std::function<void(SuccessorView)> &Visit) {
  std::uint32_t Ordinal = 0;
  auto Ordinary = [&](const BranchTarget &Target) {
    Visit({Target.Edge, Ordinal++, Target.Block, {}, Target.Arguments});
  };
  std::visit(
      [&](const auto &End) {
        using T = std::decay_t<decltype(End)>;
        if constexpr (std::is_same_v<T, Branch>)
          Ordinary(End.Target);
        else if constexpr (std::is_same_v<T, CondBranch>) {
          Ordinary(End.TrueTarget);
          Ordinary(End.FalseTarget);
        } else if constexpr (std::is_same_v<T, Switch>) {
          for (const auto &Case : End.Cases)
            Ordinary(Case.Target);
          Ordinary(End.Default);
        } else if constexpr (std::is_same_v<T, Perform>) {
          Visit({End.ResumeTarget.Edge, Ordinal++, End.ResumeTarget.Block,
                 End.ResumeTarget.ProducedPrefix, End.ResumeTarget.Arguments});
        } else {
          static_assert(std::is_same_v<T, Return> || std::is_same_v<T, Raise> ||
                        std::is_same_v<T, Resume> ||
                        std::is_same_v<T, Unreachable>);
        }
      },
      Payload);
}

} // namespace yona::typed_ir
