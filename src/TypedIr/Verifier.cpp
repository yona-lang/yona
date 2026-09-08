#include "yona/TypedIr/Verifier.h"

#include "yona/Model/CanonicalEncoding.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace yona::typed_ir {
namespace {

using Issues = std::vector<VerificationIssue>;

void issue(Issues &Out, SourceRange Range, VerificationRule Rule,
           std::string Message) {
  Out.push_back({Range, Rule, std::move(Message)});
}

bool validRange(const Module &M, SourceRange Range) {
  if (!M.Sources || !Range.isValid() || Range.Source != M.Source)
    return false;
  try {
    const auto Text = M.Sources->text(Range.Source);
    return Range.Offset <= Text.size() &&
           Range.Length <= Text.size() - Range.Offset;
  } catch (const std::out_of_range &) {
    return false;
  }
}

const model::FunctionType *functionType(const Module &M, model::TypeId Id) {
  if (!M.Types || !M.Types->valid(Id))
    return nullptr;
  return std::get_if<model::FunctionType>(&M.Types->at(Id));
}

bool isPrimitive(const Module &M, model::TypeId Id,
                 model::PrimitiveType Expected) {
  if (!M.Types || !M.Types->valid(Id))
    return false;
  const auto *Actual = std::get_if<model::PrimitiveType>(&M.Types->at(Id));
  return Actual && *Actual == Expected;
}

bool validUtf8(std::string_view Text) {
  for (std::size_t I = 0; I < Text.size();) {
    const auto Lead = static_cast<unsigned char>(Text[I]);
    std::uint32_t Codepoint = 0;
    std::size_t Extra = 0;
    if (Lead < 0x80) {
      ++I;
      continue;
    }
    if ((Lead & 0xe0) == 0xc0) {
      Codepoint = Lead & 0x1f;
      Extra = 1;
      if (Codepoint < 2)
        return false;
    } else if ((Lead & 0xf0) == 0xe0) {
      Codepoint = Lead & 0x0f;
      Extra = 2;
    } else if ((Lead & 0xf8) == 0xf0) {
      Codepoint = Lead & 0x07;
      Extra = 3;
    } else
      return false;
    if (I + Extra >= Text.size())
      return false;
    for (std::size_t J = 1; J <= Extra; ++J) {
      const auto Byte = static_cast<unsigned char>(Text[I + J]);
      if ((Byte & 0xc0) != 0x80)
        return false;
      Codepoint = (Codepoint << 6) | (Byte & 0x3f);
    }
    if ((Extra == 2 && Codepoint < 0x800) ||
        (Extra == 3 && Codepoint < 0x10000) || Codepoint > 0x10ffff ||
        (Codepoint >= 0xd800 && Codepoint <= 0xdfff))
      return false;
    I += Extra + 1;
  }
  return true;
}

OwnershipKind ownershipFor(model::ParameterOwnership Value) {
  switch (Value) {
  case model::ParameterOwnership::Trivial:
    return OwnershipKind::Trivial;
  case model::ParameterOwnership::Borrow:
    return OwnershipKind::Borrowed;
  case model::ParameterOwnership::Consume:
    return OwnershipKind::Owned;
  }
  return OwnershipKind::Trivial;
}

OwnershipKind ownershipFor(model::ResultOwnership Value) {
  return Value == model::ResultOwnership::Owned ? OwnershipKind::Owned
                                                : OwnershipKind::Trivial;
}

bool validFunctionId(const Module &M, FunctionId Id) {
  return M.MutationDomain && Id.isValid() &&
         Id.domain() == M.MutationDomain->identity() &&
         Id.value() < M.Functions.size();
}

std::vector<model::CanonicalBinderDeclaration>
canonicalBinders(const Module &M) {
  std::vector<model::CanonicalBinderDeclaration> Result;
  std::unordered_set<model::TypeParameterId, model::StrongIdHasher>
      TypeParameters;
  std::unordered_set<model::EffectVariableId, model::StrongIdHasher>
      EffectVariables;
  auto Add = [&](model::BinderOwnerKind Kind,
                 model::CanonicalBinderOwnerIdentity Identity,
                 std::vector<model::TypeParameterId> Types,
                 std::vector<model::EffectVariableDeclaration> Effects) {
    std::erase_if(Types,
                  [&](auto Id) { return !TypeParameters.insert(Id).second; });
    std::erase_if(Effects, [&](const auto &Item) {
      return !EffectVariables.insert(Item.Variable).second;
    });
    if (!Types.empty() || !Effects.empty())
      Result.push_back(
          {Kind, std::move(Identity), std::move(Types), std::move(Effects)});
  };
  for (const auto &Nominal : M.Nominals)
    Add(model::BinderOwnerKind::Nominal, Nominal.Key, Nominal.TypeParameters,
        {});
  for (const auto &Resource : M.Resources)
    Add(model::BinderOwnerKind::Resource, Resource.Key, Resource.TypeParameters,
        {});
  for (const auto &Operation : M.Operations)
    Add(model::BinderOwnerKind::Operation, Operation.Key,
        Operation.TypeParameters, Operation.EffectVariables);
  for (const auto &Function : M.Functions) {
    if (!Function.GenericBinders)
      continue;
    auto Types = Function.GenericBinders->InheritedTypeParameters;
    Types.insert(Types.end(),
                 Function.GenericBinders->DeclaredTypeParameters.begin(),
                 Function.GenericBinders->DeclaredTypeParameters.end());
    auto Effects = Function.GenericBinders->InheritedEffectVariables;
    Effects.insert(Effects.end(),
                   Function.GenericBinders->DeclaredEffectVariables.begin(),
                   Function.GenericBinders->DeclaredEffectVariables.end());
    model::FunctionDeclarationIdentity Identity =
        model::LocalFunctionIdentity{M.Identity, {Function.Id.value()}};
    if (M.Types && M.Types->valid(Function.Signature))
      if (const auto *Signature = std::get_if<model::FunctionType>(
              &M.Types->at(Function.Signature));
          Signature && Signature->SourceIdentity)
        Identity = *Signature->SourceIdentity;
    Add(model::BinderOwnerKind::Function, std::move(Identity), std::move(Types),
        std::move(Effects));
  }
  return Result;
}

struct FunctionVerifier {
  const Module &M;
  const Function &F;
  std::size_t FunctionIndex;
  const model::FunctionType *Signature = nullptr;
  Issues &Out;
  std::vector<std::optional<std::size_t>> DefBlock;
  std::vector<std::size_t> DefPosition;
  std::vector<std::vector<std::size_t>> Predecessors;
  std::vector<std::unordered_set<std::size_t>> Dominators;
  std::vector<bool> Reachable;

  SourceRange range() const { return F.Range; }
  bool validValue(ValueId Id) const {
    return F.LocalDomain && Id.isValid() &&
           Id.domain() == F.LocalDomain->identity() &&
           Id.value() < F.Values.size();
  }
  bool validBlock(BlockId Id) const {
    return F.LocalDomain && Id.isValid() &&
           Id.domain() == F.LocalDomain->identity() &&
           Id.value() < F.Blocks.size();
  }

  bool isHiddenParameter(ValueId Id) const {
    return (F.ExecutionContextParameter &&
            Id == *F.ExecutionContextParameter) ||
           (F.CallableEnvironmentParameter &&
            Id == *F.CallableEnvironmentParameter) ||
           (F.BoundaryContextParameter && Id == *F.BoundaryContextParameter);
  }

  std::optional<std::size_t> sourceParameterIndex(ValueId Id) const {
    std::size_t Index = 0;
    for (const auto Parameter : F.Parameters) {
      if (isHiddenParameter(Parameter))
        continue;
      if (Parameter == Id)
        return Index;
      ++Index;
    }
    return std::nullopt;
  }

  bool isRootlessParameterBorrow(ValueId Id, BorrowProvenanceKind Kind) const {
    if (!validValue(Id))
      return false;
    const auto &Value = F.Values[Id.value()];
    if (Value.Origin != ValueOrigin::Parameter ||
        Value.Ownership != OwnershipKind::Borrowed || !Value.Borrow ||
        Value.Borrow->Kind != Kind || Value.Borrow->Root ||
        std::ranges::find(F.Parameters, Id) == F.Parameters.end())
      return false;
    if (isHiddenParameter(Id))
      return true;
    const auto ParameterIndex = sourceParameterIndex(Id);
    return ParameterIndex && Signature &&
           *ParameterIndex < Signature->Parameters.size() &&
           Signature->Parameters[*ParameterIndex].Ownership ==
               model::ParameterOwnership::Borrow;
  }

  bool isScopedParameterCapability(ValueId Id) const {
    if (!validValue(Id))
      return false;
    const auto &Value = F.Values[Id.value()];
    if (!Value.Borrow ||
        Value.Borrow->Kind != BorrowProvenanceKind::ScopedUniqueLoanParameter)
      return false;
    return !Value.Borrow->Root ||
           isRootlessParameterBorrow(
               *Value.Borrow->Root,
               BorrowProvenanceKind::ScopedUniqueLoanParameter);
  }

  bool rootDominates(ValueId Borrow, ValueId Root) const {
    if (!validValue(Borrow) || !validValue(Root))
      return false;
    if (!DefBlock[Root.value()])
      return true;
    if (!DefBlock[Borrow.value()])
      return false;
    const auto RootBlock = *DefBlock[Root.value()];
    const auto BorrowBlock = *DefBlock[Borrow.value()];
    if (RootBlock == BorrowBlock)
      return DefPosition[Root.value()] < DefPosition[Borrow.value()];
    return BorrowBlock < Reachable.size() && RootBlock < Reachable.size() &&
           Reachable[BorrowBlock] && Reachable[RootBlock] &&
           Dominators[BorrowBlock].contains(RootBlock);
  }

  void collectDefinitions() {
    DefBlock.resize(F.Values.size());
    DefPosition.assign(F.Values.size(), 0);
    std::vector<bool> Defined(F.Values.size());
    auto Define = [&](ValueId Id, std::optional<std::size_t> BlockIndex,
                      std::size_t Position, ValueOrigin Expected,
                      SourceRange Range) {
      if (!validValue(Id)) {
        issue(Out, Range, VerificationRule::InvalidId,
              "value definition uses a foreign or invalid ID");
        return;
      }
      if (Defined[Id.value()]) {
        issue(Out, Range, VerificationRule::DuplicateDefinition,
              "SSA value is defined more than once");
        return;
      }
      Defined[Id.value()] = true;
      DefBlock[Id.value()] = BlockIndex;
      DefPosition[Id.value()] = Position;
      if (F.Values[Id.value()].Origin != Expected)
        issue(Out, Range, VerificationRule::DuplicateDefinition,
              "value origin disagrees with its defining site");
    };
    for (auto Id : F.Parameters)
      Define(Id, std::nullopt, 0, ValueOrigin::Parameter, F.Range);
    for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
      const auto &BlockValue = F.Blocks[B];
      for (auto Id : BlockValue.Arguments)
        Define(Id, B, 0, ValueOrigin::BlockArgument, BlockValue.Range);
      for (std::size_t I = 0; I < BlockValue.Instructions.size(); ++I)
        if (BlockValue.Instructions[I].Result)
          Define(*BlockValue.Instructions[I].Result, B, I + 1,
                 std::holds_alternative<ConstantInst>(
                     BlockValue.Instructions[I].Payload)
                     ? ValueOrigin::Constant
                     : ValueOrigin::Instruction,
                 BlockValue.Instructions[I].Range);
    }
    for (std::size_t I = 0; I < Defined.size(); ++I)
      if (!Defined[I])
        issue(Out, F.Values[I].Range, VerificationRule::DuplicateDefinition,
              "value storage entry has no definition");
  }

  void buildCfg() {
    Predecessors.resize(F.Blocks.size());
    for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
      const auto &BlockValue = F.Blocks[B];
      if (!BlockValue.End)
        continue;
      forEachSuccessor(BlockValue.End->Payload, [&](SuccessorView Successor) {
        if (!validBlock(Successor.Block)) {
          issue(Out, BlockValue.End->Range, VerificationRule::InvalidId,
                "successor block is foreign or invalid");
          return;
        }
        if (!F.LocalDomain || !Successor.Edge.isValid() ||
            Successor.Edge.domain() != F.LocalDomain->identity())
          issue(Out, BlockValue.End->Range, VerificationRule::InvalidId,
                "successor edge is foreign or invalid");
        Predecessors[Successor.Block.value()].push_back(B);
      });
    }
    Reachable.assign(F.Blocks.size(), false);
    if (F.Entry && validBlock(*F.Entry)) {
      std::vector<std::size_t> Work{F.Entry->value()};
      Reachable[F.Entry->value()] = true;
      while (!Work.empty()) {
        const auto B = Work.back();
        Work.pop_back();
        if (!F.Blocks[B].End)
          continue;
        forEachSuccessor(F.Blocks[B].End->Payload, [&](SuccessorView S) {
          if (validBlock(S.Block) && !Reachable[S.Block.value()]) {
            Reachable[S.Block.value()] = true;
            Work.push_back(S.Block.value());
          }
        });
      }
    }
    Dominators.resize(F.Blocks.size());
    std::unordered_set<std::size_t> All;
    for (std::size_t I = 0; I < F.Blocks.size(); ++I)
      if (Reachable[I])
        All.insert(I);
    for (std::size_t I = 0; I < F.Blocks.size(); ++I) {
      if (!Reachable[I])
        Dominators[I].insert(I);
      else if (F.Entry && I == F.Entry->value())
        Dominators[I].insert(I);
      else
        Dominators[I] = All;
    }
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
        if (!Reachable[B] || (F.Entry && B == F.Entry->value()))
          continue;
        std::unordered_set<std::size_t> Next = All;
        bool Saw = false;
        for (auto P : Predecessors[B])
          if (Reachable[P]) {
            if (!Saw) {
              Next = Dominators[P];
              Saw = true;
            } else
              for (auto It = Next.begin(); It != Next.end();)
                if (!Dominators[P].contains(*It))
                  It = Next.erase(It);
                else
                  ++It;
          }
        Next.insert(B);
        if (Next != Dominators[B]) {
          Dominators[B] = std::move(Next);
          Changed = true;
        }
      }
    }
  }

  void checkUse(ValueId Id, std::size_t UseBlock, std::size_t UsePosition,
                SourceRange Range) {
    if (!validValue(Id)) {
      issue(Out, Range, VerificationRule::InvalidId,
            "operand is foreign or invalid");
      return;
    }
    if (!DefBlock[Id.value()])
      return;
    const auto DefinitionBlock = *DefBlock[Id.value()];
    if (DefinitionBlock == UseBlock) {
      if (DefPosition[Id.value()] >= UsePosition)
        issue(Out, Range, VerificationRule::DefinitionBeforeUse,
              "SSA value is used before its same-block definition");
      return;
    }
    if (!Reachable[UseBlock] || !Reachable[DefinitionBlock] ||
        !Dominators[UseBlock].contains(DefinitionBlock))
      issue(Out, Range, VerificationRule::Dominance,
            "SSA value definition does not dominate its use");
  }

  void verifySuccessors(std::size_t B, const RangedTerminator &End) {
    forEachSuccessor(End.Payload, [&](SuccessorView S) {
      if (!validBlock(S.Block))
        return;
      const auto &Target = F.Blocks[S.Block.value()];
      if (S.ProducedPrefix.size() + S.Arguments.size() !=
          Target.Arguments.size()) {
        issue(Out, End.Range, VerificationRule::SuccessorArity,
              "successor arguments do not cover target block arguments");
        return;
      }
      for (std::size_t I = 0; I < S.ProducedPrefix.size(); ++I) {
        const auto TargetId = Target.Arguments[I];
        if (!validValue(TargetId))
          continue;
        const auto &Expected = F.Values[TargetId.value()];
        if (!M.Types->valid(S.ProducedPrefix[I].Type) ||
            S.ProducedPrefix[I].Type != Expected.Type)
          issue(Out, End.Range, VerificationRule::SuccessorType,
                "produced successor type disagrees with block argument");
        if (S.ProducedPrefix[I].Ownership != Expected.Ownership)
          issue(Out, End.Range, VerificationRule::SuccessorOwnership,
                "produced successor ownership disagrees with block argument");
        if (S.ProducedPrefix[I].NominalIdentity != Expected.NominalIdentity)
          issue(Out, End.Range, VerificationRule::SuccessorType,
                "produced successor nominal identity disagrees with block "
                "argument");
      }
      for (std::size_t I = 0; I < S.Arguments.size(); ++I) {
        if (!validValue(S.Arguments[I]))
          continue;
        const auto &Actual = F.Values[S.Arguments[I].value()];
        const auto TargetId = Target.Arguments[S.ProducedPrefix.size() + I];
        if (!validValue(TargetId))
          continue;
        const auto &Expected = F.Values[TargetId.value()];
        if (Actual.Type != Expected.Type ||
            Actual.NominalIdentity != Expected.NominalIdentity)
          issue(Out, End.Range, VerificationRule::SuccessorType,
                "successor value type disagrees with block argument");
        if (Actual.Ownership != Expected.Ownership)
          issue(Out, End.Range, VerificationRule::SuccessorOwnership,
                "successor value ownership disagrees with block argument");
        if (isScopedParameterCapability(S.Arguments[I]))
          issue(Out, End.Range, VerificationRule::OperationSignature,
                "scoped unique loan parameter escapes through a CFG edge");
      }
    });
  }

  void verifyInstruction(std::size_t B, std::size_t I,
                         const Instruction &Inst) {
    if (!validRange(M, Inst.Range))
      issue(Out, Inst.Range, VerificationRule::InvalidId,
            "instruction range is invalid");
    forEachOperand(Inst.Payload,
                   [&](ValueId Id) { checkUse(Id, B, I + 1, Inst.Range); });
    const Value *Result = Inst.Result && validValue(*Inst.Result)
                              ? &F.Values[Inst.Result->value()]
                              : nullptr;
    std::visit(
        [&](const auto &Payload) {
          using T = std::decay_t<decltype(Payload)>;
          if constexpr (std::is_same_v<T, ConstantInst>) {
            if (!Result) {
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "constant requires a result");
              return;
            }
            bool Match = std::visit(
                [&](const auto &Literal) {
                  using L = std::decay_t<decltype(Literal)>;
                  if constexpr (std::is_same_v<L, UnitLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Unit) &&
                           Result->Ownership == OwnershipKind::Trivial;
                  if constexpr (std::is_same_v<L, BoolLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Bool) &&
                           Result->Ownership == OwnershipKind::Trivial;
                  if constexpr (std::is_same_v<L, ByteLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Byte) &&
                           Result->Ownership == OwnershipKind::Trivial;
                  if constexpr (std::is_same_v<L, CharLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Char) &&
                           Result->Ownership == OwnershipKind::Trivial &&
                           (Literal.Value <= 0xd7ff ||
                            (Literal.Value >= 0xe000 &&
                             Literal.Value <= 0x10ffff));
                  if constexpr (std::is_same_v<L, IntLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Int) &&
                           Result->Ownership == OwnershipKind::Trivial;
                  if constexpr (std::is_same_v<L, FloatLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Float) &&
                           Result->Ownership == OwnershipKind::Trivial;
                  if constexpr (std::is_same_v<L, StringLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::String) &&
                           Result->Ownership == OwnershipKind::Owned &&
                           validUtf8(Literal.Utf8);
                  if constexpr (std::is_same_v<L, SymbolLiteral>)
                    return isPrimitive(M, Result->Type,
                                       model::PrimitiveType::Symbol) &&
                           Result->Ownership == OwnershipKind::Trivial &&
                           validUtf8(Literal.Utf8);
                },
                Payload.Value);
            if (!Match)
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "constant literal does not match result type or ownership");
          } else if constexpr (std::is_same_v<T, UnaryInst>) {
            if (!Result || !validValue(Payload.Operand))
              return;
            const auto OperandType = F.Values[Payload.Operand.value()].Type;
            const bool Match =
                ((Payload.Opcode == UnaryOpcode::NegateInt ||
                  Payload.Opcode == UnaryOpcode::BitwiseNot) &&
                 isPrimitive(M, OperandType, model::PrimitiveType::Int) &&
                 Result->Type == OperandType) ||
                (Payload.Opcode == UnaryOpcode::NegateFloat &&
                 isPrimitive(M, OperandType, model::PrimitiveType::Float) &&
                 Result->Type == OperandType) ||
                (Payload.Opcode == UnaryOpcode::LogicalNot &&
                 isPrimitive(M, OperandType, model::PrimitiveType::Bool) &&
                 Result->Type == OperandType);
            if (!Match || Result->Ownership != OwnershipKind::Trivial)
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "unary opcode operand/result contract is invalid");
          } else if constexpr (std::is_same_v<T, BinaryInst>) {
            if (!Result || !validValue(Payload.Left) ||
                !validValue(Payload.Right))
              return;
            const auto LeftType = F.Values[Payload.Left.value()].Type;
            const auto RightType = F.Values[Payload.Right.value()].Type;
            bool Match = LeftType == RightType &&
                         Result->Ownership == OwnershipKind::Trivial;
            switch (Payload.Opcode) {
            case BinaryOpcode::AddInt:
            case BinaryOpcode::SubtractInt:
            case BinaryOpcode::MultiplyInt:
            case BinaryOpcode::DivideIntSigned:
            case BinaryOpcode::RemainderIntSigned:
              Match = Match &&
                      isPrimitive(M, LeftType, model::PrimitiveType::Int) &&
                      Result->Type == LeftType;
              break;
            case BinaryOpcode::AddFloat:
            case BinaryOpcode::SubtractFloat:
            case BinaryOpcode::MultiplyFloat:
            case BinaryOpcode::DivideFloat:
              Match = Match &&
                      isPrimitive(M, LeftType, model::PrimitiveType::Float) &&
                      Result->Type == LeftType;
              break;
            case BinaryOpcode::LessIntSigned:
            case BinaryOpcode::LessEqualIntSigned:
            case BinaryOpcode::GreaterIntSigned:
            case BinaryOpcode::GreaterEqualIntSigned:
              Match = Match &&
                      isPrimitive(M, LeftType, model::PrimitiveType::Int) &&
                      isPrimitive(M, Result->Type, model::PrimitiveType::Bool);
              break;
            case BinaryOpcode::LessFloatOrdered:
            case BinaryOpcode::LessEqualFloatOrdered:
            case BinaryOpcode::GreaterFloatOrdered:
            case BinaryOpcode::GreaterEqualFloatOrdered:
              Match = Match &&
                      isPrimitive(M, LeftType, model::PrimitiveType::Float) &&
                      isPrimitive(M, Result->Type, model::PrimitiveType::Bool);
              break;
            case BinaryOpcode::Equal:
            case BinaryOpcode::NotEqual:
              Match = Match &&
                      isPrimitive(M, Result->Type, model::PrimitiveType::Bool);
              break;
            }
            if (!Match)
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "binary opcode operand/result contract is invalid");
          } else if constexpr (std::is_same_v<T, DirectCallInst>) {
            for (const auto Argument : Payload.Arguments)
              if (isScopedParameterCapability(Argument))
                issue(Out, Inst.Range, VerificationRule::OperationSignature,
                      "scoped unique loan parameter escapes through an "
                      "ordinary call");
            if (Payload.CallableEnvironment &&
                isScopedParameterCapability(*Payload.CallableEnvironment))
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "scoped unique loan parameter escapes as a callable "
                    "environment");
            if (Payload.BoundaryContext &&
                isScopedParameterCapability(*Payload.BoundaryContext))
              issue(
                  Out, Inst.Range, VerificationRule::OperationSignature,
                  "scoped unique loan parameter escapes as a boundary context");
            if (!validFunctionId(M, Payload.Callee)) {
              issue(Out, Inst.Range, VerificationRule::InvalidId,
                    "direct call callee is foreign or invalid");
              return;
            }
            const auto *Called =
                functionType(M, M.Functions[Payload.Callee.value()].Signature);
            if (!Called ||
                Called->Parameters.size() != Payload.Arguments.size()) {
              issue(Out, Inst.Range, VerificationRule::CallSignature,
                    "direct call arity disagrees with callee signature");
              return;
            }
            for (std::size_t A = 0; A < Payload.Arguments.size(); ++A)
              if (validValue(Payload.Arguments[A])) {
                const auto &Actual = F.Values[Payload.Arguments[A].value()];
                if (Actual.Type != Called->Parameters[A].Type ||
                    Actual.Ownership !=
                        ownershipFor(Called->Parameters[A].Ownership))
                  issue(Out, Inst.Range, VerificationRule::CallSignature,
                        "direct call argument contract disagrees with callee "
                        "signature");
              }
            if (!Result || Result->Type != Called->Result ||
                Result->Ownership != ownershipFor(Called->ResultContract))
              issue(Out, Inst.Range, VerificationRule::CallSignature,
                    "direct call result disagrees with callee signature");
          } else if constexpr (std::is_same_v<T, InjectSumInst>) {
            if (!M.Types->valid(Payload.Sum) ||
                !std::holds_alternative<model::SumType>(
                    M.Types->at(Payload.Sum)) ||
                !Result || Result->Type != Payload.Sum)
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "sum injection has an invalid result contract");
          } else if constexpr (std::is_same_v<T, LexicalRefInst>) {
            if (!Payload.Binding.isValid())
              issue(Out, Inst.Range, VerificationRule::InvalidId,
                    "lexical reference binding is invalid");
          }
        },
        Inst.Payload);
  }

  void verifyTerminator(std::size_t B, const RangedTerminator &End) {
    if (!validRange(M, End.Range))
      issue(Out, End.Range, VerificationRule::InvalidId,
            "terminator range is invalid");
    forEachOperand(End.Payload, [&](ValueId Id) {
      checkUse(Id, B, F.Blocks[B].Instructions.size() + 1, End.Range);
    });
    verifySuccessors(B, End);
    std::visit(
        [&](const auto &Payload) {
          using T = std::decay_t<decltype(Payload)>;
          if constexpr (std::is_same_v<T, CondBranch>) {
            if (validValue(Payload.Condition) &&
                !isPrimitive(M, F.Values[Payload.Condition.value()].Type,
                             model::PrimitiveType::Bool))
              issue(Out, End.Range, VerificationRule::ConditionType,
                    "conditional branch requires Bool");
          } else if constexpr (std::is_same_v<T, Return>) {
            if (!Signature)
              return;
            if (!Payload.Result) {
              if (!isPrimitive(M, Signature->Result,
                               model::PrimitiveType::Unit))
                issue(Out, End.Range, VerificationRule::ReturnType,
                      "non-Unit function requires a return value");
            } else if (validValue(*Payload.Result)) {
              if (isScopedParameterCapability(*Payload.Result))
                issue(Out, End.Range, VerificationRule::OperationSignature,
                      "scoped unique loan parameter escapes through return");
              const auto &Actual = F.Values[Payload.Result->value()];
              if (Actual.Type != Signature->Result ||
                  Actual.Ownership != ownershipFor(Signature->ResultContract))
                issue(Out, End.Range, VerificationRule::ReturnType,
                      "return value disagrees with function signature");
            }
          } else if constexpr (std::is_same_v<T, Raise>) {
            if (validValue(Payload.Exception) &&
                F.Values[Payload.Exception.value()].Type !=
                    M.Builtins.ExceptionValue)
              issue(Out, End.Range, VerificationRule::OperationSignature,
                    "raise requires the builtin exception type");
          } else if constexpr (std::is_same_v<T, Perform>) {
            if (!M.Types->valid(Payload.ResumeEffects))
              issue(Out, End.Range, VerificationRule::InvalidId,
                    "perform resume effects are invalid");
            const std::vector<OperationParameter> *Parameters = nullptr;
            model::TypeId ResultType;
            model::ResultOwnership ResultContract =
                model::ResultOwnership::Trivial;
            std::visit(
                [&](const auto &Reference) {
                  using R = std::decay_t<decltype(Reference)>;
                  if constexpr (std::is_same_v<R, OperationUse>) {
                    if (!M.MutationDomain || !Reference.Declaration.isValid() ||
                        Reference.Declaration.domain() !=
                            M.MutationDomain->identity() ||
                        Reference.Declaration.value() >= M.Operations.size()) {
                      issue(Out, End.Range, VerificationRule::InvalidId,
                            "perform declaration is foreign or invalid");
                      return;
                    }
                    const auto &Declaration =
                        M.Operations[Reference.Declaration.value()];
                    if (Reference.TypeArguments.size() !=
                            Declaration.TypeParameters.size() ||
                        Reference.EffectArguments.size() !=
                            Declaration.EffectVariables.size())
                      issue(Out, End.Range,
                            VerificationRule::OperationSignature,
                            "perform generic argument arity is invalid");
                    for (auto Id : Reference.TypeArguments)
                      if (!M.Types->valid(Id))
                        issue(Out, End.Range, VerificationRule::InvalidId,
                              "perform type argument is foreign");
                    for (auto Id : Reference.EffectArguments)
                      if (!M.Types->valid(Id))
                        issue(Out, End.Range, VerificationRule::InvalidId,
                              "perform effect argument is foreign");
                    Parameters = &Declaration.Parameters;
                    ResultType = Declaration.ResultType;
                    ResultContract = Declaration.ResultContract;
                  } else {
                    if (!M.MutationDomain || !Reference.isValid() ||
                        Reference.domain() != M.MutationDomain->identity() ||
                        Reference.value() >= M.OperationInstances.size()) {
                      issue(Out, End.Range, VerificationRule::InvalidId,
                            "perform operation instance is foreign or invalid");
                      return;
                    }
                    const auto &Instance =
                        M.OperationInstances[Reference.value()];
                    Parameters = &Instance.Parameters;
                    ResultType = Instance.ResultType;
                    ResultContract = Instance.ResultContract;
                  }
                },
                Payload.Operation);
            if (!Parameters)
              return;
            if (Payload.Arguments.size() != Parameters->size())
              issue(
                  Out, End.Range, VerificationRule::OperationSignature,
                  "perform argument arity disagrees with operation signature");
            for (std::size_t I = 0;
                 I < std::min(Payload.Arguments.size(), Parameters->size());
                 ++I)
              if (validValue(Payload.Arguments[I])) {
                const auto &Actual = F.Values[Payload.Arguments[I].value()];
                if (Actual.Type != (*Parameters)[I].Type ||
                    Actual.Ownership !=
                        ownershipFor((*Parameters)[I].Ownership))
                  issue(Out, End.Range, VerificationRule::OperationSignature,
                        "perform argument contract disagrees with operation "
                        "signature");
              }
            if (Payload.ResumeTarget.ProducedPrefix.empty() ||
                Payload.ResumeTarget.ProducedPrefix.front().Type !=
                    ResultType ||
                Payload.ResumeTarget.ProducedPrefix.front().Ownership !=
                    ownershipFor(ResultContract))
              issue(
                  Out, End.Range, VerificationRule::OperationSignature,
                  "perform produced result disagrees with operation signature");
          }
        },
        End.Payload);
  }

  void run() {
    if (!validFunctionId(M, F.Id) || F.Id.value() != FunctionIndex)
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function ID is foreign or out of canonical order");
    if (!F.LocalDomain || F.LocalDomain->identity() == model::InvalidIdDomain)
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function local identity domain is missing");
    if (!validRange(M, F.Range))
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function range is invalid");
    Signature = functionType(M, F.Signature);
    if (!Signature)
      issue(Out, F.Range, VerificationRule::CallSignature,
            "function signature is not a structural function type");
    const bool Definition =
        F.Linkage.Kind == FunctionDefinitionKind::Definition;
    if (Definition) {
      if (!F.Entry || !validBlock(*F.Entry) || F.Blocks.empty())
        issue(Out, F.Range, VerificationRule::InvalidId,
              "definition requires a valid entry and at least one block");
      if (F.Linkage.Visibility == FunctionVisibility::Public &&
          !F.Linkage.Symbol)
        issue(Out, F.Range, VerificationRule::CallSignature,
              "public definition requires an exported symbol");
    } else {
      if (F.Entry || !F.Blocks.empty() || !F.Values.empty() ||
          !F.Parameters.empty())
        issue(Out, F.Range, VerificationRule::DuplicateDefinition,
              "declaration cannot contain a body");
      if (F.Linkage.Kind == FunctionDefinitionKind::Imported &&
          !F.Linkage.Symbol)
        issue(Out, F.Range, VerificationRule::CallSignature,
              "imported function requires a canonical symbol");
      if (F.Linkage.Kind == FunctionDefinitionKind::NativeExtern &&
          (!F.Linkage.NativeSymbol || F.Linkage.NativeSymbol->empty() ||
           !F.Linkage.AsyncKind || !F.Linkage.NativeRoute))
        issue(Out, F.Range, VerificationRule::CallSignature,
              "native extern requires explicit native linkage metadata");
    }
    if (F.Linkage.Kind != FunctionDefinitionKind::NativeExtern &&
        (F.Linkage.NativeSymbol || F.Linkage.AsyncKind ||
         F.Linkage.NativeRoute))
      issue(Out, F.Range, VerificationRule::CallSignature,
            "non-native function carries native linkage metadata");
    if (F.Linkage.Kind == FunctionDefinitionKind::NativeExtern &&
        F.Linkage.AsyncKind && F.Linkage.NativeRoute) {
      const bool Compatible =
          (*F.Linkage.NativeRoute == NativeBoundaryRoute::StableExternal &&
           *F.Linkage.AsyncKind == NativeAsyncKind::Synchronous) ||
          (*F.Linkage.NativeRoute == NativeBoundaryRoute::CheckedOutcomeV2 &&
           *F.Linkage.AsyncKind == NativeAsyncKind::DedicatedOutcome) ||
          (*F.Linkage.NativeRoute == NativeBoundaryRoute::CheckedDirectV2 &&
           *F.Linkage.AsyncKind != NativeAsyncKind::DedicatedOutcome);
      if (!Compatible)
        issue(Out, F.Range, VerificationRule::CallSignature,
              "native route and async kind are incompatible");
    }
    if (!Definition && (F.GenericBinders || !F.GenericCaptures.empty()))
      issue(Out, F.Range, VerificationRule::OperationSignature,
            "imported and native declarations cannot carry generic local "
            "context");
    if (static_cast<unsigned>(M.Phase) >=
            static_cast<unsigned>(ModulePhase::GenericPrepared) &&
        F.GenericBinders)
      issue(Out, F.Range, VerificationRule::IllegalPhaseOperation,
            "generic binders remain after generic preparation");
    if (Definition && !F.GenericCaptures.empty() && !F.GenericBinders)
      issue(Out, F.Range, VerificationRule::OperationSignature,
            "open nested definition is missing its generic binder environment");
    std::unordered_set<std::uint64_t> CaptureBindings;
    for (const auto &Capture : F.GenericCaptures) {
      if (!Capture.Binding.isValid() ||
          !CaptureBindings.insert(Capture.Binding.value()).second ||
          !M.Types->valid(Capture.Type))
        issue(Out, F.Range, VerificationRule::OperationSignature,
              "generic capture schema has an invalid or duplicate binding");
    }
    std::unordered_set<std::uint64_t> UsedCaptures;
    for (const auto &BlockValue : F.Blocks)
      for (const auto &Inst : BlockValue.Instructions)
        if (const auto *Lexical = std::get_if<LexicalRefInst>(&Inst.Payload)) {
          const auto It = std::ranges::find_if(
              F.GenericCaptures, [&](const GenericCaptureBinding &Capture) {
                return Capture.Binding == Lexical->Binding;
              });
          if (It == F.GenericCaptures.end()) {
            issue(
                Out, Inst.Range, VerificationRule::OperationSignature,
                "lexical reference is absent from the generic capture schema");
          } else {
            UsedCaptures.insert(It->Binding.value());
            if (!Inst.Result || !validValue(*Inst.Result) ||
                F.Values[Inst.Result->value()].Type != It->Type ||
                F.Values[Inst.Result->value()].Ownership !=
                    ownershipFor(It->Ownership))
              issue(Out, Inst.Range, VerificationRule::OperationSignature,
                    "lexical reference disagrees with its capture contract");
          }
        }
    for (const auto &Capture : F.GenericCaptures)
      if (!UsedCaptures.contains(Capture.Binding.value()))
        issue(Out, F.Range, VerificationRule::OperationSignature,
              "generic capture schema contains an unused binding");
    collectDefinitions();
    buildCfg();
    std::vector<ValueId> SourceParameters;
    for (auto Id : F.Parameters)
      if (!isHiddenParameter(Id))
        SourceParameters.push_back(Id);
    if (Signature && SourceParameters.size() != Signature->Parameters.size())
      issue(Out, F.Range, VerificationRule::CallSignature,
            "parameter count disagrees with function signature");
    for (std::size_t I = 0;
         Signature &&
         I < std::min(SourceParameters.size(), Signature->Parameters.size());
         ++I)
      if (validValue(SourceParameters[I])) {
        const auto &Actual = F.Values[SourceParameters[I].value()];
        if (Actual.Type != Signature->Parameters[I].Type ||
            Actual.Ownership !=
                ownershipFor(Signature->Parameters[I].Ownership))
          issue(Out, Actual.Range, VerificationRule::CallSignature,
                "parameter contract disagrees with function signature");
      }
    auto CheckHidden = [&](const std::optional<ValueId> &Id, model::TypeId Type,
                           std::string_view Name) {
      if (!Id)
        return;
      if (!validValue(*Id) ||
          std::ranges::find(F.Parameters, *Id) == F.Parameters.end()) {
        issue(Out, F.Range, VerificationRule::InvalidId,
              std::string(Name) + " is not a function parameter");
        return;
      }
      const auto &Value = F.Values[Id->value()];
      if (Value.Type != Type || Value.Ownership != OwnershipKind::Borrowed)
        issue(Out, Value.Range, VerificationRule::CallSignature,
              std::string(Name) + " has the wrong hidden parameter contract");
    };
    CheckHidden(F.ExecutionContextParameter, M.Builtins.ExecutionContext,
                "execution context");
    CheckHidden(F.CallableEnvironmentParameter,
                M.Builtins.CallableInvocationEnvironment,
                "callable environment");
    CheckHidden(F.BoundaryContextParameter,
                M.Builtins.ContinuationBoundaryContext, "boundary context");
    if (static_cast<unsigned>(M.Phase) >=
        static_cast<unsigned>(ModulePhase::OperationInstantiated)) {
      if (!F.RuntimeEffects)
        issue(Out, F.Range, VerificationRule::IllegalPhaseOperation,
              "runtime-reachable function is missing its runtime effect row");
    } else if (F.RuntimeEffects) {
      issue(Out, F.Range, VerificationRule::IllegalPhaseOperation,
            "runtime effect row appears before operation instantiation");
    }
    if (F.RuntimeEffects &&
        (!M.MutationDomain || !F.RuntimeEffects->isValid() ||
         F.RuntimeEffects->domain() != M.MutationDomain->identity() ||
         F.RuntimeEffects->value() >= M.RuntimeEffectRows.size()))
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function runtime effect row is foreign or invalid");
    if (F.BoundaryContextContract &&
        (!M.MutationDomain || !F.BoundaryContextContract->isValid() ||
         F.BoundaryContextContract->domain() != M.MutationDomain->identity() ||
         F.BoundaryContextContract->value() >=
             M.BoundaryContextContracts.size()))
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function boundary context contract is foreign or invalid");
    if (F.AmbientBoundaryRequirements &&
        (!M.MutationDomain || !F.AmbientBoundaryRequirements->isValid() ||
         F.AmbientBoundaryRequirements->domain() !=
             M.MutationDomain->identity() ||
         F.AmbientBoundaryRequirements->value() >=
             M.BoundaryRequirementSets.size()))
      issue(Out, F.Range, VerificationRule::InvalidId,
            "function ambient boundary requirements are foreign or invalid");
    std::unordered_map<ValueId, std::size_t, model::StrongIdHasher>
        ScopedLoanCounts;
    for (std::size_t I = 0; I < F.Values.size(); ++I) {
      const auto &Value = F.Values[I];
      if (!M.Types || !M.Types->valid(Value.Type))
        issue(Out, Value.Range, VerificationRule::InvalidId,
              "value type is foreign or invalid");
      if (!validRange(M, Value.Range))
        issue(Out, Value.Range, VerificationRule::InvalidId,
              "value range is invalid");
      if ((Value.Ownership == OwnershipKind::Borrowed) !=
          Value.Borrow.has_value())
        issue(Out, Value.Range, VerificationRule::OperationSignature,
              "borrow provenance presence disagrees with ownership");
      if (M.Phase == ModulePhase::Canonical &&
          Value.Representation != PhysicalRepresentation::Unselected)
        issue(Out, Value.Range, VerificationRule::Representation,
              "canonical value has a selected physical representation");
      if (Value.Borrow && Value.Borrow->Root &&
          !validValue(*Value.Borrow->Root))
        issue(Out, Value.Range, VerificationRule::InvalidId,
              "borrow root is foreign or invalid");
      if (Value.NominalIdentity &&
          (!M.MutationDomain || !Value.NominalIdentity->isValid() ||
           Value.NominalIdentity->domain() != M.MutationDomain->identity() ||
           Value.NominalIdentity->value() >= M.Nominals.size()))
        issue(Out, Value.Range, VerificationRule::InvalidId,
              "value nominal identity is foreign or invalid");
      if (Value.CallableBoundaryRequirements &&
          (!M.MutationDomain ||
           !Value.CallableBoundaryRequirements->isValid() ||
           Value.CallableBoundaryRequirements->domain() !=
               M.MutationDomain->identity() ||
           Value.CallableBoundaryRequirements->value() >=
               M.BoundaryRequirementSets.size()))
        issue(Out, Value.Range, VerificationRule::InvalidId,
              "value boundary requirements are foreign or invalid");
      if (!Value.Borrow || !F.LocalDomain)
        continue;
      const auto BorrowId = ValueId(F.LocalDomain->identity(), I);
      const auto Root = Value.Borrow->Root;
      if (Root && validValue(*Root) && !rootDominates(BorrowId, *Root))
        issue(Out, Value.Range, VerificationRule::Dominance,
              "borrow root does not dominate the derived borrow");
      switch (Value.Borrow->Kind) {
      case BorrowProvenanceKind::BorrowParameter:
        if ((!Root && !isRootlessParameterBorrow(
                          BorrowId, BorrowProvenanceKind::BorrowParameter)) ||
            (Root && !isRootlessParameterBorrow(
                         *Root, BorrowProvenanceKind::BorrowParameter)))
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "BorrowParameter provenance must name an actual Borrow "
                "parameter");
        break;
      case BorrowProvenanceKind::StaticLifetime:
        if (Root)
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "StaticLifetime provenance must be rootless");
        break;
      case BorrowProvenanceKind::OwnedValue:
        if (!Root || !validValue(*Root) ||
            F.Values[Root->value()].Ownership != OwnershipKind::Owned)
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "OwnedValue provenance requires a dominating Owned root");
        break;
      case BorrowProvenanceKind::ScopedUniqueLoan:
        if (!Root || !validValue(*Root) ||
            F.Values[Root->value()].Ownership != OwnershipKind::Owned) {
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "ScopedUniqueLoan provenance requires a dominating Owned root");
        } else if (++ScopedLoanCounts[*Root] != 1) {
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "an Owned root has more than one scoped unique loan");
        }
        break;
      case BorrowProvenanceKind::ScopedUniqueLoanParameter: {
        const bool Prepared =
            static_cast<unsigned>(M.Phase) >=
            static_cast<unsigned>(ModulePhase::EffectPrepared);
        if ((!Root && (!Prepared || SourceParameters.empty() ||
                       BorrowId != SourceParameters.front() ||
                       !isRootlessParameterBorrow(
                           BorrowId,
                           BorrowProvenanceKind::ScopedUniqueLoanParameter))) ||
            (Root &&
             !isRootlessParameterBorrow(
                 *Root, BorrowProvenanceKind::ScopedUniqueLoanParameter)))
          issue(Out, Value.Range, VerificationRule::OperationSignature,
                "ScopedUniqueLoanParameter provenance must derive from the "
                "prepared body's first Borrow parameter");
        break;
      }
      default:
        issue(Out, Value.Range, VerificationRule::OperationSignature,
              "borrow provenance kind is invalid");
        break;
      }
    }
    for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
      const auto &BlockValue = F.Blocks[B];
      if (!validRange(M, BlockValue.Range))
        issue(Out, BlockValue.Range, VerificationRule::InvalidId,
              "block range is invalid");
      if (BlockValue.ControlRegion &&
          (!F.LocalDomain || !BlockValue.ControlRegion->isValid() ||
           BlockValue.ControlRegion->domain() != F.LocalDomain->identity()))
        issue(Out, BlockValue.Range, VerificationRule::InvalidId,
              "block control region is foreign or invalid");
      if (!BlockValue.End)
        issue(Out, BlockValue.Range, VerificationRule::MissingTerminator,
              "block has no terminator");
      for (std::size_t I = 0; I < BlockValue.Instructions.size(); ++I)
        verifyInstruction(B, I, BlockValue.Instructions[I]);
      if (BlockValue.End)
        verifyTerminator(B, *BlockValue.End);
    }
  }
};

} // namespace

VerificationResult verifyModule(const Module &M, VerifyOptions Options) {
  Issues Out;
  const auto SeedRange = SourceRange::unknown();
  if (!M.MutationDomain ||
      M.MutationDomain->identity() == model::InvalidIdDomain)
    issue(Out, SeedRange, VerificationRule::InvalidId,
          "module mutation identity domain is missing");
  if (!M.Types)
    issue(Out, SeedRange, VerificationRule::InvalidId,
          "module type table is missing");
  if (!M.Sources)
    issue(Out, SeedRange, VerificationRule::InvalidId,
          "module source manager is missing");
  if (!M.Source.isValid())
    issue(Out, SeedRange, VerificationRule::InvalidId,
          "module source is invalid");
  if (M.Types) {
    const model::TypeId Builtins[] = {M.Builtins.ExceptionValue,
                                      M.Builtins.EffectRequest,
                                      M.Builtins.ControlOutcome,
                                      M.Builtins.ExecutionContext,
                                      M.Builtins.CallableInvocationEnvironment,
                                      M.Builtins.ContinuationBoundaryContext};
    for (auto Id : Builtins)
      if (!M.Types->valid(Id))
        issue(Out, SeedRange, VerificationRule::InvalidId,
              "builtin type is foreign or invalid");
    if (auto Result = M.Types->validate(); !Result)
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "type table is invalid: " + Result.error());
  }
  if (M.Sources && M.Source.isValid())
    try {
      (void)M.Sources->text(M.Source);
    } catch (const std::out_of_range &) {
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "module source is foreign");
    }
  std::unordered_set<std::string> Imports;
  for (const auto &Import : M.Imports)
    if (Import == M.Identity || !Imports.insert(Import.fqn()).second)
      issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
            "duplicate or self import");
  std::unordered_set<std::string> TypeKeys;
  for (std::size_t I = 0; I < M.Nominals.size(); ++I) {
    const auto &N = M.Nominals[I];
    if (!M.MutationDomain || !N.Id.isValid() ||
        N.Id.domain() != M.MutationDomain->identity() || N.Id.value() != I)
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "nominal ID is foreign or out of canonical order");
    if (!TypeKeys.insert(N.Key.Module.fqn() + "\\" + N.Key.Name).second)
      issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
            "duplicate nominal key");
    std::unordered_set<std::string> Names;
    std::unordered_set<std::uint32_t> Tags;
    for (const auto &C : N.Constructors) {
      if (!Names.insert(C.Name).second || !Tags.insert(C.Tag).second)
        issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
              "duplicate constructor name or tag");
      for (auto Field : C.Fields)
        if (!M.Types || !M.Types->valid(Field))
          issue(Out, SeedRange, VerificationRule::InvalidId,
                "constructor field type is foreign or invalid");
    }
  }
  std::size_t ConstructorOrdinal = 0;
  for (const auto &N : M.Nominals)
    for (const auto &C : N.Constructors) {
      if (!M.MutationDomain || !C.Id.isValid() ||
          C.Id.domain() != M.MutationDomain->identity() ||
          C.Id.value() != ConstructorOrdinal)
        issue(Out, SeedRange, VerificationRule::InvalidId,
              "constructor ID is foreign or out of canonical order");
      ++ConstructorOrdinal;
    }
  std::optional<model::NominalTypeKey> PreviousResource;
  for (const auto &R : M.Resources) {
    if (!TypeKeys.insert(R.Key.Module.fqn() + "\\" + R.Key.Name).second)
      issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
            "duplicate or colliding resource key");
    if (PreviousResource && !(PreviousResource.value() < R.Key))
      issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
            "resource table is not in canonical key order");
    PreviousResource = R.Key;
    if (R.ReleaseNativeSymbol.empty() ||
        (R.Shareability == model::ResourceShareability::Linear &&
         R.TryRetainNativeSymbol) ||
        (R.Shareability == model::ResourceShareability::AlwaysShareable &&
         !R.TryRetainNativeSymbol))
      issue(Out, SeedRange, VerificationRule::OperationSignature,
            "resource callback policy is invalid");
    for (auto Parameter : R.TypeParameters)
      if (!M.Types || !M.Types->valid(Parameter))
        issue(Out, SeedRange, VerificationRule::InvalidId,
              "resource type parameter is foreign or invalid");
  }
  const auto Binders = M.Types
                           ? canonicalBinders(M)
                           : std::vector<model::CanonicalBinderDeclaration>{};
  for (std::size_t I = 0; I < M.Operations.size(); ++I) {
    const auto &O = M.Operations[I];
    if (!M.MutationDomain || !O.Id.isValid() ||
        O.Id.domain() != M.MutationDomain->identity() || O.Id.value() != I)
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "operation ID is foreign or out of canonical order");
    if (!M.Types || !M.Types->valid(O.ResultType) || !M.Types->valid(O.Effects))
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "operation result type or effects are foreign");
    for (const auto &P : O.Parameters)
      if (!M.Types || !M.Types->valid(P.Type))
        issue(Out, SeedRange, VerificationRule::InvalidId,
              "operation parameter type is foreign");
    if (M.Types) {
      std::vector<model::FunctionParameter> Parameters;
      Parameters.reserve(O.Parameters.size());
      for (const auto &Parameter : O.Parameters)
        Parameters.push_back({Parameter.Type, Parameter.Ownership});
      const model::EffectOperationSignature Signature{O.Key,
                                                      O.TypeParameters,
                                                      O.EffectVariables,
                                                      O.Constraints,
                                                      std::move(Parameters),
                                                      O.ResultType,
                                                      O.ResultContract,
                                                      O.Effects};
      const auto Bytes =
          model::encodeOperationDeclarationGraph(*M.Types, Signature, Binders);
      if (!Bytes) {
        issue(Out, SeedRange, VerificationRule::OperationSignature,
              "operation declaration cannot be canonically encoded: " +
                  Bytes.error().Message);
      } else if (model::canonicalStructuralFingerprint(*Bytes) !=
                 O.DeclarationFingerprint) {
        issue(Out, SeedRange, VerificationRule::OperationSignature,
              "operation declaration fingerprint does not match its "
              "canonical declaration bytes");
      }
    }
  }
  for (std::size_t I = 0; I < M.OperationInstances.size(); ++I) {
    const auto &O = M.OperationInstances[I];
    if (!M.MutationDomain || !O.Id.isValid() ||
        O.Id.domain() != M.MutationDomain->identity() || O.Id.value() != I ||
        !O.Declaration.isValid() ||
        O.Declaration.domain() != M.MutationDomain->identity() ||
        O.Declaration.value() >= M.Operations.size() ||
        !O.RuntimeEffects.isValid() ||
        O.RuntimeEffects.domain() != M.MutationDomain->identity() ||
        O.RuntimeEffects.value() >= M.RuntimeEffectRows.size())
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "operation instance identity or references are invalid");
    if (!M.Types || !M.Types->valid(O.ResultType) ||
        !M.Types->valid(O.SemanticEffects))
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "operation instance structural references are invalid");
    if (M.Types && O.Declaration.isValid() && M.MutationDomain &&
        O.Declaration.domain() == M.MutationDomain->identity() &&
        O.Declaration.value() < M.Operations.size()) {
      const auto &Declaration = M.Operations[O.Declaration.value()];
      std::vector<model::FunctionParameter> Parameters;
      Parameters.reserve(O.Parameters.size());
      for (const auto &Parameter : O.Parameters)
        Parameters.push_back({Parameter.Type, Parameter.Ownership});
      const model::ClosedEffectOperation Closed{
          Declaration.Key,       O.TypeArguments, O.EffectArguments,
          std::move(Parameters), O.ResultType,    O.ResultContract,
          O.SemanticEffects};
      const auto Bytes = model::encodeClosedDescriptorGraph(
          *M.Types, model::CanonicalRoot{Closed});
      if (!Bytes) {
        issue(Out, SeedRange, VerificationRule::OperationSignature,
              "closed operation instance cannot be canonically encoded: " +
                  Bytes.error().Message);
      } else if (model::canonicalStructuralFingerprint(*Bytes) !=
                 O.RuntimeFingerprint) {
        issue(Out, SeedRange, VerificationRule::OperationSignature,
              "operation runtime fingerprint does not match its canonical "
              "closed descriptor bytes");
      }
    }
  }
  for (std::size_t I = 0; I < M.RuntimeEffectRows.size(); ++I) {
    const auto &R = M.RuntimeEffectRows[I];
    if (!M.MutationDomain || !R.Id.isValid() ||
        R.Id.domain() != M.MutationDomain->identity() || R.Id.value() != I ||
        !M.Types || !M.Types->valid(R.SemanticRow))
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "runtime effect row identity or semantic row is invalid");
    for (auto Operation : R.Operations)
      if (!M.MutationDomain || !Operation.isValid() ||
          Operation.domain() != M.MutationDomain->identity() ||
          Operation.value() >= M.OperationInstances.size())
        issue(Out, SeedRange, VerificationRule::InvalidId,
              "runtime effect row references an invalid operation instance");
  }
  auto VerifyBoundaryRows = [&](const auto &Rows, std::string_view Name) {
    for (std::size_t I = 0; I < Rows.size(); ++I) {
      if (!M.MutationDomain || !Rows[I].Id.isValid() ||
          Rows[I].Id.domain() != M.MutationDomain->identity() ||
          Rows[I].Id.value() != I)
        issue(Out, SeedRange, VerificationRule::InvalidId,
              std::string(Name) + " ID is foreign or out of canonical order");
      const auto &Items = [&]() -> const auto & {
        if constexpr (std::is_same_v<std::decay_t<decltype(Rows[I])>,
                                     BoundaryContextContract>)
          return Rows[I].Guaranteed;
        else
          return Rows[I].Required;
      }();
      std::optional<std::pair<unsigned, std::uint32_t>> Previous;
      for (const auto &Item : Items) {
        const auto Key = std::visit(
            [&](auto Id) {
              using T = decltype(Id);
              if (!M.MutationDomain || !Id.isValid() ||
                  Id.domain() != M.MutationDomain->identity())
                issue(Out, SeedRange, VerificationRule::InvalidId,
                      std::string(Name) + " contains a foreign descriptor");
              return std::pair{
                  std::is_same_v<T, HandlerBoundaryDescriptorId> ? 0u : 1u,
                  Id.value()};
            },
            Item);
        if (Previous && !(Previous.value() < Key))
          issue(Out, SeedRange, VerificationRule::DuplicateDefinition,
                std::string(Name) +
                    " descriptors are not unique canonical IDs");
        Previous = Key;
      }
    }
  };
  VerifyBoundaryRows(M.BoundaryContextContracts, "boundary context contract");
  VerifyBoundaryRows(M.BoundaryRequirementSets, "boundary requirement set");
  for (std::size_t I = 0; I < M.Globals.size(); ++I) {
    const auto &G = M.Globals[I];
    if (!M.MutationDomain || !G.Id.isValid() ||
        G.Id.domain() != M.MutationDomain->identity() || G.Id.value() != I ||
        !M.Types || !M.Types->valid(G.Type))
      issue(Out, SeedRange, VerificationRule::InvalidId,
            "global identity or type is invalid");
  }
  std::unordered_set<std::string> FunctionNames;
  for (std::size_t I = 0; I < M.Functions.size(); ++I) {
    if (!FunctionNames.insert(M.Functions[I].Name).second)
      issue(Out, M.Functions[I].Range, VerificationRule::DuplicateDefinition,
            "duplicate function name");
    // Function verification is type-directed throughout. The missing arena is
    // already a seed error above; do not dereference it while diagnosing the
    // rest of a malformed module.
    if (M.Types)
      FunctionVerifier{M, M.Functions[I], I, nullptr, Out}.run();
  }
  if (Options.VerifyPhase && M.Phase == ModulePhase::Canonical &&
      (!M.OperationInstances.empty() || !M.RuntimeEffectRows.empty() ||
       !M.BoundaryContextContracts.empty() ||
       !M.BoundaryRequirementSets.empty()))
    issue(Out, SeedRange, VerificationRule::IllegalPhaseOperation,
          "canonical module contains post-canonical runtime records");
  if (Out.empty())
    return {};
  return std::unexpected(std::move(Out));
}

void verifyOrThrow(const Module &M, VerifyOptions Options) {
  auto Result = verifyModule(M, Options);
  if (Result)
    return;
  std::ostringstream Message;
  Message << "typed IR verification failed with " << Result.error().size()
          << " issue(s)";
  for (const auto &Item : Result.error())
    Message << "\n- " << Item.Message;
  throw std::invalid_argument(Message.str());
}

} // namespace yona::typed_ir
