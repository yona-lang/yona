#include "yona/TypedIr/Printer.h"

#include "yona/Model/CanonicalEncoding.h"
#include "yona/TypedIr/Verifier.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
namespace yona::typed_ir {
namespace {
std::string hex(std::string_view Text) {
  std::ostringstream Out;
  Out << std::hex << std::setfill('0');
  for (unsigned char C : Text)
    Out << std::setw(2) << static_cast<unsigned>(C);
  return Out.str();
}
std::string hex(std::span<const std::byte> Bytes) {
  std::ostringstream Out;
  Out << std::hex << std::setfill('0');
  for (auto B : Bytes)
    Out << std::setw(2) << std::to_integer<unsigned>(B);
  return Out.str();
}
void range(std::ostream &Out, SourceRange R) {
  Out << R.Line << ' ' << R.Column << ' ' << R.Offset << ' ' << R.Length;
}
template <class Id>
void optionalId(std::ostream &Out, const std::optional<Id> &Value) {
  if (Value)
    Out << Value->value();
  else
    Out << '-';
}
void symbol(std::ostream &Out,
            const std::optional<model::SymbolIdentity> &Value) {
  if (!Value) {
    Out << "- -";
    return;
  }
  Out << hex(Value->Module.fqn()) << ' ' << hex(Value->Name);
}
struct Writer {
  const Module &M;
  const model::CanonicalInterfaceTables &Tables;
  std::ostringstream Out;
  std::uint32_t type(model::TypeId Id) const {
    return Tables.TypeRemap.at(Id.value());
  }
  std::uint32_t effect(model::EffectRowId Id) const {
    return Tables.EffectRemap.at(Id.value());
  }
  model::CanonicalBinderRef typeParameter(model::TypeParameterId Id) const {
    const auto It =
        std::ranges::find_if(Tables.TypeParameterRemap, [&](const auto &Item) {
          return Item.Source == Id;
        });
    if (It == Tables.TypeParameterRemap.end())
      throw std::invalid_argument("unmapped typed IR type parameter");
    return It->Target;
  }
  model::CanonicalBinderRef effectVariable(model::EffectVariableId Id) const {
    const auto It =
        std::ranges::find_if(Tables.EffectVariableRemap, [&](const auto &Item) {
          return Item.Source == Id;
        });
    if (It == Tables.EffectVariableRemap.end())
      throw std::invalid_argument("unmapped typed IR effect variable");
    return It->Target;
  }
  void value(const Value &V) {
    Out << type(V.Type) << ' ' << static_cast<unsigned>(V.Ownership) << ' ';
    if (!V.Borrow)
      Out << "- -";
    else {
      Out << static_cast<unsigned>(V.Borrow->Kind) << ' ';
      optionalId(Out, V.Borrow->Root);
    }
    Out << ' ' << static_cast<unsigned>(V.Representation) << ' ';
    optionalId(Out, V.NominalIdentity);
    Out << ' ';
    optionalId(Out, V.CallableBoundaryRequirements);
    Out << ' ';
    range(Out, V.Range);
    Out << ' ' << hex(V.DebugName);
  }
  void target(const BranchTarget &T) {
    Out << T.Edge.value() << ' ' << T.Block.value() << ' '
        << T.Arguments.size();
    for (auto Id : T.Arguments)
      Out << ' ' << Id.value();
  }
  void produced(const ProducedBranchTarget &T) {
    Out << T.Edge.value() << ' ' << T.Block.value() << ' '
        << T.ProducedPrefix.size();
    for (const auto &P : T.ProducedPrefix) {
      Out << ' ' << type(P.Type) << ' ' << static_cast<unsigned>(P.Ownership)
          << ' ';
      optionalId(Out, P.NominalIdentity);
      Out << ' ' << hex(P.DebugName);
    }
    Out << ' ' << T.Arguments.size();
    for (auto Id : T.Arguments)
      Out << ' ' << Id.value();
  }
  void instruction(const Instruction &I) {
    Out << "inst ";
    optionalId(Out, I.Result);
    Out << ' ';
    range(Out, I.Range);
    if (I.Result) {
      Out << " value ";
      value(M.Functions.front().Values.front());
    }
    std::visit(
        [&](const auto &P) {
          using T = std::decay_t<decltype(P)>;
          if constexpr (std::is_same_v<T, ConstantInst>) {
            Out << " constant ";
            std::visit(
                [&](const auto &L) {
                  using V = std::decay_t<decltype(L)>;
                  if constexpr (std::is_same_v<V, UnitLiteral>)
                    Out << "unit";
                  else if constexpr (std::is_same_v<V, BoolLiteral>)
                    Out << "bool " << L.Value;
                  else if constexpr (std::is_same_v<V, ByteLiteral>)
                    Out << "byte " << static_cast<unsigned>(L.Value);
                  else if constexpr (std::is_same_v<V, CharLiteral>)
                    Out << "char " << static_cast<std::uint32_t>(L.Value);
                  else if constexpr (std::is_same_v<V, IntLiteral>)
                    Out << "int " << L.Value;
                  else if constexpr (std::is_same_v<V, FloatLiteral>)
                    Out << "float " << std::hex << std::setw(16)
                        << std::setfill('0') << L.Ieee754Bits << std::dec;
                  else if constexpr (std::is_same_v<V, StringLiteral>)
                    Out << "string " << hex(L.Utf8);
                  else if constexpr (std::is_same_v<V, SymbolLiteral>)
                    Out << "symbol " << hex(L.Utf8);
                },
                P.Value);
          } else if constexpr (std::is_same_v<T, UnaryInst>)
            Out << " unary " << static_cast<unsigned>(P.Opcode) << ' '
                << P.Operand.value();
          else if constexpr (std::is_same_v<T, BinaryInst>)
            Out << " binary " << static_cast<unsigned>(P.Opcode) << ' '
                << P.Left.value() << ' ' << P.Right.value();
          else if constexpr (std::is_same_v<T, InjectSumInst>)
            Out << " inject " << P.Alternative.value() << ' ' << type(P.Sum);
          else if constexpr (std::is_same_v<T, DirectCallInst>) {
            Out << " call " << P.Callee.value() << ' ' << P.Arguments.size();
            for (auto Id : P.Arguments)
              Out << ' ' << Id.value();
            Out << ' ';
            optionalId(Out, P.CallableEnvironment);
            Out << ' ';
            optionalId(Out, P.BoundaryContext);
          } else if constexpr (std::is_same_v<T, LexicalRefInst>)
            Out << " lexical " << P.Binding.value();
        },
        I.Payload);
    Out << '\n';
  }
};

std::vector<model::CanonicalBinderDeclaration> binders(const Module &M) {
  std::vector<model::CanonicalBinderDeclaration> Result;
  std::unordered_set<model::TypeParameterId, model::StrongIdHasher> Types;
  std::unordered_set<model::EffectVariableId, model::StrongIdHasher> Effects;
  auto Add =
      [&](model::BinderOwnerKind Kind,
          model::CanonicalBinderOwnerIdentity Identity,
          std::vector<model::TypeParameterId> TypeParameters,
          std::vector<model::EffectVariableDeclaration> EffectVariables) {
        std::erase_if(TypeParameters,
                      [&](auto Id) { return !Types.insert(Id).second; });
        std::erase_if(EffectVariables, [&](const auto &Item) {
          return !Effects.insert(Item.Variable).second;
        });
        if (!TypeParameters.empty() || !EffectVariables.empty())
          Result.push_back({Kind, std::move(Identity),
                            std::move(TypeParameters),
                            std::move(EffectVariables)});
      };
  for (const auto &N : M.Nominals)
    Add(model::BinderOwnerKind::Nominal, N.Key, N.TypeParameters, {});
  for (const auto &R : M.Resources)
    Add(model::BinderOwnerKind::Resource, R.Key, R.TypeParameters, {});
  for (const auto &O : M.Operations)
    Add(model::BinderOwnerKind::Operation, O.Key, O.TypeParameters,
        O.EffectVariables);
  for (const auto &F : M.Functions)
    if (F.GenericBinders) {
      std::vector<model::TypeParameterId> TypeParameters =
          F.GenericBinders->InheritedTypeParameters;
      TypeParameters.insert(TypeParameters.end(),
                            F.GenericBinders->DeclaredTypeParameters.begin(),
                            F.GenericBinders->DeclaredTypeParameters.end());
      std::vector<model::EffectVariableDeclaration> EffectVariables =
          F.GenericBinders->InheritedEffectVariables;
      EffectVariables.insert(EffectVariables.end(),
                             F.GenericBinders->DeclaredEffectVariables.begin(),
                             F.GenericBinders->DeclaredEffectVariables.end());
      model::FunctionDeclarationIdentity Identity =
          model::LocalFunctionIdentity{M.Identity, {F.Id.value()}};
      if (const auto *Signature =
              std::get_if<model::FunctionType>(&M.Types->at(F.Signature));
          Signature && Signature->SourceIdentity)
        Identity = *Signature->SourceIdentity;
      Add(model::BinderOwnerKind::Function, std::move(Identity),
          std::move(TypeParameters), std::move(EffectVariables));
    }
  return Result;
}
} // namespace

std::string printModule(const Module &M) {
  verifyOrThrow(M);
  const auto BinderDeclarations = binders(M);
  const auto Tables =
      model::encodeInterfaceTables(*M.Types, BinderDeclarations);
  if (!Tables)
    throw std::invalid_argument("cannot encode typed IR type table: " +
                                Tables.error().Message);
  Writer W{M, *Tables};
  W.Out << "tir 1\nmodule " << hex(M.Identity.fqn()) << "\nsource "
        << M.Source.value() << "\nphase " << static_cast<unsigned>(M.Phase)
        << "\ntypes " << hex(Tables->TypeSectionPayload) << "\neffects "
        << hex(Tables->EffectSectionPayload) << "\nbuiltins "
        << W.type(M.Builtins.ExceptionValue) << ' '
        << W.type(M.Builtins.EffectRequest) << ' '
        << W.type(M.Builtins.ControlOutcome) << ' '
        << W.type(M.Builtins.ExecutionContext) << ' '
        << W.type(M.Builtins.CallableInvocationEnvironment) << ' '
        << W.type(M.Builtins.ContinuationBoundaryContext) << "\nimports "
        << M.Imports.size() << '\n';
  for (const auto &Import : M.Imports)
    W.Out << "import " << hex(Import.fqn()) << '\n';
  W.Out << "nominals " << M.Nominals.size() << '\n';
  for (const auto &N : M.Nominals) {
    W.Out << "nominal " << N.Id.value() << ' ' << hex(N.Key.Module.fqn()) << ' '
          << hex(N.Key.Name) << ' ' << N.TypeParameters.size();
    for (auto Id : N.TypeParameters) {
      const auto Ref = W.typeParameter(Id);
      W.Out << ' ' << Ref.BinderIndex << ' ' << Ref.Ordinal;
    }
    W.Out << ' ' << N.Constructors.size() << '\n';
    for (const auto &C : N.Constructors) {
      W.Out << "constructor " << C.Id.value() << ' ' << hex(C.Name) << ' '
            << C.Tag << ' ' << C.Fields.size();
      for (auto Field : C.Fields)
        W.Out << ' ' << W.type(Field);
      W.Out << '\n';
    }
  }
  W.Out << "resources " << M.Resources.size() << '\n';
  for (const auto &R : M.Resources) {
    W.Out << "resource " << hex(R.Key.Module.fqn()) << ' ' << hex(R.Key.Name)
          << ' ' << R.TypeParameters.size();
    for (auto Id : R.TypeParameters) {
      const auto Ref = W.typeParameter(Id);
      W.Out << ' ' << Ref.BinderIndex << ' ' << Ref.Ordinal;
    }
    W.Out << ' ' << static_cast<unsigned>(R.AbiKind) << ' '
          << static_cast<unsigned>(R.Shareability) << ' '
          << (R.TryRetainNativeSymbol ? hex(*R.TryRetainNativeSymbol) : "-")
          << ' ' << hex(R.ReleaseNativeSymbol) << '\n';
  }
  W.Out << "operations " << M.Operations.size() << '\n';
  for (const auto &O : M.Operations) {
    W.Out << "operation " << O.Id.value() << ' '
          << hex(O.Key.Effect.Module.fqn()) << ' ' << hex(O.Key.Effect.Name)
          << ' ' << hex(O.Key.Operation) << ' ' << O.TypeParameters.size();
    for (auto Id : O.TypeParameters) {
      const auto Ref = W.typeParameter(Id);
      W.Out << ' ' << Ref.BinderIndex << ' ' << Ref.Ordinal;
    }
    W.Out << ' ' << O.EffectVariables.size();
    for (const auto &V : O.EffectVariables) {
      const auto Ref = W.effectVariable(V.Variable);
      W.Out << ' ' << Ref.BinderIndex << ' ' << Ref.Ordinal << ' '
            << static_cast<unsigned>(V.Kind);
    }
    W.Out << ' ' << O.Constraints.size();
    for (const auto &C : O.Constraints) {
      const auto Ref = W.typeParameter(C.Parameter);
      W.Out << ' ' << Ref.BinderIndex << ' ' << Ref.Ordinal << ' '
            << hex(C.Trait.Module.fqn()) << ' ' << hex(C.Trait.Name) << ' '
            << C.Arguments.size();
      for (auto Id : C.Arguments)
        W.Out << ' ' << W.type(Id);
    }
    W.Out << ' ' << O.Parameters.size();
    for (const auto &P : O.Parameters)
      W.Out << ' ' << W.type(P.Type) << ' '
            << static_cast<unsigned>(P.Ownership);
    W.Out << ' ' << W.type(O.ResultType) << ' '
          << static_cast<unsigned>(O.ResultContract) << ' '
          << W.effect(O.Effects) << ' ' << O.DeclarationFingerprint << '\n';
  }
  W.Out << "operationinstances " << M.OperationInstances.size() << '\n';
  for (const auto &O : M.OperationInstances) {
    W.Out << "operationinstance " << O.Id.value() << ' '
          << O.Declaration.value() << ' ' << O.TypeArguments.size();
    for (auto Id : O.TypeArguments)
      W.Out << ' ' << W.type(Id);
    W.Out << ' ' << O.EffectArguments.size();
    for (auto Id : O.EffectArguments)
      W.Out << ' ' << W.effect(Id);
    W.Out << ' ' << O.Parameters.size();
    for (const auto &P : O.Parameters)
      W.Out << ' ' << W.type(P.Type) << ' '
            << static_cast<unsigned>(P.Ownership);
    W.Out << ' ' << W.type(O.ResultType) << ' '
          << static_cast<unsigned>(O.ResultContract) << ' '
          << W.effect(O.SemanticEffects) << ' ' << O.RuntimeEffects.value()
          << ' ' << O.RuntimeFingerprint << '\n';
  }
  W.Out << "runtimeeffects " << M.RuntimeEffectRows.size() << '\n';
  for (const auto &R : M.RuntimeEffectRows) {
    W.Out << "runtimeeffect " << R.Id.value() << ' ' << W.effect(R.SemanticRow)
          << ' ' << R.Operations.size();
    for (auto Id : R.Operations)
      W.Out << ' ' << Id.value();
    W.Out << ' ' << R.MayRaise << ' ' << R.MayCancel << ' '
          << R.StructuralFingerprint << '\n';
  }
  auto PrintBoundary = [&](const ContinuationBoundaryDescriptorId &Id) {
    std::visit(
        [&](auto Value) {
          using T = decltype(Value);
          W.Out << (std::is_same_v<T, HandlerBoundaryDescriptorId> ? 'h' : 't')
                << ' ' << Value.value();
        },
        Id);
  };
  W.Out << "boundarycontracts " << M.BoundaryContextContracts.size() << '\n';
  for (const auto &C : M.BoundaryContextContracts) {
    W.Out << "boundarycontract " << C.Id.value() << ' ' << C.Guaranteed.size();
    for (const auto &Id : C.Guaranteed) {
      W.Out << ' ';
      PrintBoundary(Id);
    }
    W.Out << '\n';
  }
  W.Out << "boundaryrequirements " << M.BoundaryRequirementSets.size() << '\n';
  for (const auto &R : M.BoundaryRequirementSets) {
    W.Out << "boundaryrequirement " << R.Id.value() << ' ' << R.Required.size();
    for (const auto &Id : R.Required) {
      W.Out << ' ';
      PrintBoundary(Id);
    }
    W.Out << '\n';
  }
  W.Out << "globals " << M.Globals.size() << '\n';
  for (const auto &G : M.Globals)
    W.Out << "global " << G.Id.value() << ' ' << hex(G.Symbol.Module.fqn())
          << ' ' << hex(G.Symbol.Name) << ' ' << W.type(G.Type) << ' '
          << static_cast<unsigned>(G.Ownership) << '\n';
  W.Out << "functions " << M.Functions.size() << '\n';
  for (const auto &F : M.Functions) {
    W.Out << "function " << F.Id.value() << ' ' << hex(F.Name) << ' ';
    range(W.Out, F.Range);
    W.Out << ' ' << W.type(F.Signature) << ' '
          << static_cast<unsigned>(F.EntryKind) << ' '
          << static_cast<unsigned>(F.Linkage.Kind) << ' '
          << static_cast<unsigned>(F.Linkage.Visibility) << ' ';
    symbol(W.Out, F.Linkage.Symbol);
    W.Out << ' '
          << (F.Linkage.NativeSymbol ? hex(*F.Linkage.NativeSymbol) : "-")
          << ' ';
    if (F.Linkage.AsyncKind)
      W.Out << static_cast<unsigned>(*F.Linkage.AsyncKind);
    else
      W.Out << '-';
    W.Out << ' ';
    if (F.Linkage.NativeRoute)
      W.Out << static_cast<unsigned>(*F.Linkage.NativeRoute);
    else
      W.Out << '-';
    W.Out << ' ';
    optionalId(W.Out, F.Entry);
    W.Out << ' ' << F.Parameters.size() << ' ' << F.Blocks.size() << '\n';
    for (auto Id : F.Parameters) {
      W.Out << "param " << Id.value() << ' ';
      W.value(F.Values.at(Id.value()));
      W.Out << '\n';
    }
    for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
      const auto &Block = F.Blocks[B];
      W.Out << "block " << B << ' ' << hex(Block.DebugName) << ' ';
      range(W.Out, Block.Range);
      W.Out << ' ' << Block.Arguments.size() << ' ';
      optionalId(W.Out, Block.ControlRegion);
      W.Out << '\n';
      for (auto Id : Block.Arguments) {
        W.Out << "arg " << Id.value() << ' ';
        W.value(F.Values.at(Id.value()));
        W.Out << '\n';
      }
    }
    W.Out << "functionmeta ";
    optionalId(W.Out, F.RuntimeEffects);
    W.Out << ' ';
    optionalId(W.Out, F.ExecutionContextParameter);
    W.Out << ' ';
    optionalId(W.Out, F.CallableEnvironmentParameter);
    W.Out << ' ';
    optionalId(W.Out, F.BoundaryContextParameter);
    W.Out << ' ';
    optionalId(W.Out, F.BoundaryContextContract);
    W.Out << ' ';
    optionalId(W.Out, F.AmbientBoundaryRequirements);
    W.Out << '\n';
    W.Out << "binders " << (F.GenericBinders ? 1 : 0);
    if (F.GenericBinders) {
      const auto &G = *F.GenericBinders;
      W.Out << ' ' << G.InheritedTypeParameters.size();
      for (auto Id : G.InheritedTypeParameters) {
        const auto R = W.typeParameter(Id);
        W.Out << ' ' << R.BinderIndex << ' ' << R.Ordinal;
      }
      W.Out << ' ' << G.InheritedEffectVariables.size();
      for (const auto &V : G.InheritedEffectVariables) {
        const auto R = W.effectVariable(V.Variable);
        W.Out << ' ' << R.BinderIndex << ' ' << R.Ordinal << ' '
              << static_cast<unsigned>(V.Kind);
      }
      W.Out << ' ' << G.DeclaredTypeParameters.size();
      for (auto Id : G.DeclaredTypeParameters) {
        const auto R = W.typeParameter(Id);
        W.Out << ' ' << R.BinderIndex << ' ' << R.Ordinal;
      }
      W.Out << ' ' << G.DeclaredEffectVariables.size();
      for (const auto &V : G.DeclaredEffectVariables) {
        const auto R = W.effectVariable(V.Variable);
        W.Out << ' ' << R.BinderIndex << ' ' << R.Ordinal << ' '
              << static_cast<unsigned>(V.Kind);
      }
    }
    W.Out << '\n' << "captures " << F.GenericCaptures.size() << '\n';
    for (const auto &C : F.GenericCaptures)
      W.Out << "capture " << C.Binding.value() << ' ' << W.type(C.Type) << ' '
            << static_cast<unsigned>(C.Ownership) << ' ' << hex(C.DebugName)
            << '\n';
  }
  for (const auto &F : M.Functions) {
    for (std::size_t B = 0; B < F.Blocks.size(); ++B) {
      const auto &Block = F.Blocks[B];
      W.Out << "body " << F.Id.value() << ' ' << B << ' '
            << Block.Instructions.size() << '\n';
      for (const auto &I : Block.Instructions) {
        W.Out << "inst ";
        optionalId(W.Out, I.Result);
        W.Out << ' ';
        range(W.Out, I.Range);
        if (I.Result) {
          W.Out << " value ";
          W.value(F.Values.at(I.Result->value()));
        }
        std::visit(
            [&](const auto &P) {
              using T = std::decay_t<decltype(P)>;
              if constexpr (std::is_same_v<T, ConstantInst>) {
                W.Out << " constant ";
                std::visit(
                    [&](const auto &L) {
                      using V = std::decay_t<decltype(L)>;
                      if constexpr (std::is_same_v<V, UnitLiteral>)
                        W.Out << "unit";
                      else if constexpr (std::is_same_v<V, BoolLiteral>)
                        W.Out << "bool " << L.Value;
                      else if constexpr (std::is_same_v<V, ByteLiteral>)
                        W.Out << "byte " << static_cast<unsigned>(L.Value);
                      else if constexpr (std::is_same_v<V, CharLiteral>)
                        W.Out << "char " << static_cast<std::uint32_t>(L.Value);
                      else if constexpr (std::is_same_v<V, IntLiteral>)
                        W.Out << "int " << L.Value;
                      else if constexpr (std::is_same_v<V, FloatLiteral>)
                        W.Out << "float " << std::hex << std::setw(16)
                              << std::setfill('0') << L.Ieee754Bits << std::dec;
                      else if constexpr (std::is_same_v<V, StringLiteral>)
                        W.Out << "string " << hex(L.Utf8);
                      else if constexpr (std::is_same_v<V, SymbolLiteral>)
                        W.Out << "symbol " << hex(L.Utf8);
                    },
                    P.Value);
              } else if constexpr (std::is_same_v<T, UnaryInst>)
                W.Out << " unary " << static_cast<unsigned>(P.Opcode) << ' '
                      << P.Operand.value();
              else if constexpr (std::is_same_v<T, BinaryInst>)
                W.Out << " binary " << static_cast<unsigned>(P.Opcode) << ' '
                      << P.Left.value() << ' ' << P.Right.value();
              else if constexpr (std::is_same_v<T, InjectSumInst>)
                W.Out << " inject " << P.Alternative.value() << ' '
                      << W.type(P.Sum);
              else if constexpr (std::is_same_v<T, DirectCallInst>) {
                W.Out << " call " << P.Callee.value() << ' '
                      << P.Arguments.size();
                for (auto Id : P.Arguments)
                  W.Out << ' ' << Id.value();
                W.Out << ' ';
                optionalId(W.Out, P.CallableEnvironment);
                W.Out << ' ';
                optionalId(W.Out, P.BoundaryContext);
              } else if constexpr (std::is_same_v<T, LexicalRefInst>)
                W.Out << " lexical " << P.Binding.value();
            },
            I.Payload);
        W.Out << '\n';
      }
      W.Out << "term ";
      range(W.Out, Block.End->Range);
      std::visit(
          [&](const auto &T) {
            using E = std::decay_t<decltype(T)>;
            if constexpr (std::is_same_v<E, Branch>) {
              W.Out << " branch ";
              W.target(T.Target);
            } else if constexpr (std::is_same_v<E, CondBranch>) {
              W.Out << " cond " << T.Condition.value() << ' ';
              W.target(T.TrueTarget);
              W.Out << ' ';
              W.target(T.FalseTarget);
            } else if constexpr (std::is_same_v<E, Switch>) {
              W.Out << " switch " << T.Scrutinee.value() << ' '
                    << T.Cases.size();
              for (const auto &C : T.Cases) {
                W.Out << ' ' << C.Key << ' ';
                W.target(C.Target);
              }
              W.Out << ' ';
              W.target(T.Default);
            } else if constexpr (std::is_same_v<E, Return>) {
              W.Out << " return ";
              optionalId(W.Out, T.Result);
            } else if constexpr (std::is_same_v<E, Raise>)
              W.Out << " raise " << T.Exception.value();
            else if constexpr (std::is_same_v<E, Perform>) {
              W.Out << " perform ";
              std::visit(
                  [&](const auto &O) {
                    using R = std::decay_t<decltype(O)>;
                    if constexpr (std::is_same_v<R, OperationUse>) {
                      W.Out << "decl " << O.Declaration.value() << ' '
                            << O.TypeArguments.size();
                      for (auto Id : O.TypeArguments)
                        W.Out << ' ' << W.type(Id);
                      W.Out << ' ' << O.EffectArguments.size();
                      for (auto Id : O.EffectArguments)
                        W.Out << ' ' << W.effect(Id);
                    } else
                      W.Out << "instance " << O.value();
                  },
                  T.Operation);
              W.Out << ' ' << T.Arguments.size();
              for (auto Id : T.Arguments)
                W.Out << ' ' << Id.value();
              W.Out << ' ';
              W.produced(T.ResumeTarget);
              W.Out << ' ' << W.effect(T.ResumeEffects);
            } else if constexpr (std::is_same_v<E, Resume>)
              W.Out << " resume " << T.Continuation.value() << ' '
                    << T.Argument.value();
            else
              W.Out << " unreachable";
          },
          Block.End->Payload);
      W.Out << '\n';
    }
  }
  W.Out << "end\n";
  return W.Out.str();
}
} // namespace yona::typed_ir
