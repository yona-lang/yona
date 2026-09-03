#include "yona/Model/TypeArena.h"
#include "yona/Semantics/StructuralTypeProjection.h"
#include "yona/Syntax/Ast.h"

#include <doctest/doctest.h>

#include <array>
#include <variant>

namespace {

using yona::SourceId;
using yona::SourceRange;
using yona::compiler::typechecker::CallableTypeContract;
using yona::compiler::typechecker::DeclarationScopeProjection;
using yona::compiler::typechecker::EffectAtomId;
using yona::compiler::typechecker::FunctionSchemeProjection;
using yona::compiler::typechecker::ImportedDeclarationCatalogEntry;
using yona::compiler::typechecker::MayCancelEffectAtom;
using yona::compiler::typechecker::MayRaiseEffectAtom;
using yona::compiler::typechecker::MonoTypePtr;
using yona::compiler::typechecker::OperationEffectAtom;
using yona::compiler::typechecker::SemanticProjectionBatch;
using yona::compiler::typechecker::TypeArena;
using yona::compiler::typechecker::TypeScheme;
using yona::model::BinderOwnerKind;
using yona::model::CallingConvention;
using yona::model::EffectOperationKey;
using yona::model::FunctionType;
using yona::model::ModuleIdentity;
using yona::model::NominalTypeKey;
using yona::model::ParameterOwnership;
using yona::model::PrimitiveType;
using yona::model::ResultOwnership;
using yona::model::StructuralType;
using yona::model::TypeTable;
using yona::semantics::DeclarationVisibility;
using yona::semantics::StructuralProjectionErrorCode;
using yona::semantics::StructuralTypeProjection;

const SourceRange TestRange = SourceRange::unknown();

EffectAtomId commitAtom(TypeArena &Arena,
                        yona::compiler::typechecker::SolvedEffectAtom Atom) {
  auto Transaction = Arena.effect_atoms().beginAppend();
  const auto Result = Transaction.intern(std::move(Atom));
  REQUIRE(Transaction.prepareCommit().has_value());
  Transaction.commitPrepared();
  return Result;
}

TEST_CASE("Structural type projection closes values and rejects contract-free "
          "arrows") {
  TypeArena Arena;
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Int = Projection.freezeClosedMonotype(
      Arena.make_con(yona::compiler::typechecker::TyCon::Int), TestRange);
  REQUIRE(Int.has_value());
  CHECK(std::get<PrimitiveType>(Types.at(*Int)) == PrimitiveType::Int);

  const auto Arrow =
      Arena.make_arrow(Arena.make_con(yona::compiler::typechecker::TyCon::Int),
                       Arena.make_con(yona::compiler::typechecker::TyCon::Int));
  const auto Rejected = Projection.freezeClosedMonotype(Arrow, TestRange);
  REQUIRE_FALSE(Rejected.has_value());
  CHECK(Rejected.error().Code ==
        StructuralProjectionErrorCode::WrongArrowShape);
  CHECK(Rejected.error().Path == "type");
}

TEST_CASE("Structural type projection preserves an explicit Promise result "
          "and await-effect contract") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto Cancel = commitAtom(Arena, MayCancelEffectAtom{});
  const auto AwaitEffects = Arena.effect_solver().atoms({Cancel});
  const auto *Promise =
      Arena.make_promise(Int, ResultOwnership::Trivial, AwaitEffects);
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeClosedMonotype(Promise, TestRange);
  CAPTURE(Result ? std::string{}
                 : Result.error().Path + ": " + Result.error().Message);
  REQUIRE(Result.has_value());
  const auto &Projected = std::get<yona::model::PromiseType>(Types.at(*Result));
  CHECK(std::get<PrimitiveType>(Types.at(Projected.Element)) ==
        PrimitiveType::Int);
  CHECK(Projected.ResultContract == ResultOwnership::Trivial);
  CHECK(Types.effect(Projected.Effects).MayCancel);
}

TEST_CASE("Semantic model structural projection accepts a Promise-only "
          "quantified effect root") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto AwaitTail = Arena.effect_solver().flexible();
  const auto AwaitVariable = Arena.effect_solver().variable_id(AwaitTail);
  REQUIRE(AwaitVariable.has_value());
  const auto AwaitEffects = Arena.effect_solver().join(
      {Arena.effect_solver().atoms({commitAtom(Arena, MayCancelEffectAtom{})}),
       AwaitTail});
  const auto *Promise =
      Arena.make_promise(Int, ResultOwnership::Trivial, AwaitEffects);
  TypeScheme Scheme(Promise);
  Scheme.quantified_effect_variables.push_back(
      {*AwaitVariable, yona::model::EffectVariableKind::Flexible});
  Scheme.effect_graph = Arena.effect_solver().freeze({AwaitEffects});

  const std::array<yona::compiler::typechecker::QuantifiedEffectVariable, 1>
      Effects = {yona::compiler::typechecker::QuantifiedEffectVariable{
          *AwaitVariable, yona::model::EffectVariableKind::Flexible}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{
          1,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "awaited"}},
          {},
          Effects,
          {},
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.size() == 1);
  const auto &Function =
      std::get<FunctionType>(Types.at(Result->Functions.front().Signature));
  CHECK(std::holds_alternative<yona::model::PromiseType>(
      Types.at(Function.Result)));
}

TEST_CASE("Structural type projection normalizes checker-produced native "
          "container applications") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto IntArray = Projection.freezeClosedMonotype(
      Arena.make_app("IntArray", {}), TestRange);
  const auto FloatArray = Projection.freezeClosedMonotype(
      Arena.make_app("FloatArray", {}), TestRange);
  const auto Channel = Projection.freezeClosedMonotype(
      Arena.make_app("Channel", {Int}), TestRange);

  REQUIRE(IntArray.has_value());
  REQUIRE(FloatArray.has_value());
  REQUIRE(Channel.has_value());
  const auto &ProjectedInts =
      std::get<yona::model::ArrayType>(Types.at(*IntArray));
  const auto &ProjectedFloats =
      std::get<yona::model::ArrayType>(Types.at(*FloatArray));
  const auto &ProjectedChannel =
      std::get<yona::model::ChannelType>(Types.at(*Channel));
  CHECK(ProjectedInts.Mutable);
  CHECK(ProjectedFloats.Mutable);
  CHECK(std::get<PrimitiveType>(Types.at(ProjectedInts.Element)) ==
        PrimitiveType::Int);
  CHECK(std::get<PrimitiveType>(Types.at(ProjectedFloats.Element)) ==
        PrimitiveType::Float);
  CHECK(std::get<PrimitiveType>(Types.at(ProjectedChannel.Element)) ==
        PrimitiveType::Int);
}

TEST_CASE("Semantic model structural type projection preserves explicit "
          "callable contracts") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Arrow = Arena.make_arrow(Int, Int);
  TypeScheme Scheme(Arrow);
  Scheme.effect_roots.push_back({Arrow, Arrow->arrow_effect});
  Scheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{Arrow->arrow_effect});

  const std::array<ParameterOwnership, 1> BorrowParameter = {
      ParameterOwnership::Borrow};
  const std::array<ParameterOwnership, 1> ConsumeParameter = {
      ParameterOwnership::Consume};
  const std::array<CallableTypeContract, 1> BorrowContract = {
      CallableTypeContract{Arrow,
                           {BorrowParameter.begin(), BorrowParameter.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<CallableTypeContract, 1> ConsumeContract = {
      CallableTypeContract{Arrow,
                           {ConsumeParameter.begin(), ConsumeParameter.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};

  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{
          1,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "borrow"}},
          {},
          {},
          BorrowContract,
          TestRange},
      DeclarationScopeProjection{
          2,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "consume"}},
          {},
          {},
          ConsumeContract,
          TestRange}};
  const std::array<FunctionSchemeProjection, 2> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               BorrowParameter,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange},
      FunctionSchemeProjection{2,
                               Scheme,
                               ConsumeParameter,
                               ResultOwnership::Trivial,
                               CallingConvention::ClosureEntry,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  SemanticProjectionBatch Batch{Scopes, Functions, {}, {}, {}, {}, {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.size() == 2);
  CHECK(Result->Functions[0].Signature != Result->Functions[1].Signature);

  const auto &First =
      std::get<FunctionType>(Types.at(Result->Functions[0].Signature));
  const auto &Second =
      std::get<FunctionType>(Types.at(Result->Functions[1].Signature));
  CHECK(First.Parameters.front().Ownership == ParameterOwnership::Borrow);
  CHECK(Second.Parameters.front().Ownership == ParameterOwnership::Consume);
  CHECK(First.Convention == CallingConvention::DirectYona);
  CHECK(Second.Convention == CallingConvention::ClosureEntry);
}

TEST_CASE("Semantic model structural type projection rolls back an invalid "
          "callable batch") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Arrow = Arena.make_arrow(Int, Int);
  TypeScheme Scheme(Arrow);
  Scheme.effect_roots.push_back({Arrow, Arrow->arrow_effect});
  Scheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{Arrow->arrow_effect});
  const std::array<ParameterOwnership, 2> WrongArity = {
      ParameterOwnership::Trivial, ParameterOwnership::Trivial};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {WrongArity.begin(), WrongArity.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{
          1,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "invalid"}},
          {},
          {},
          Contracts,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               WrongArity,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  SemanticProjectionBatch Batch{Scopes, Functions, {}, {}, {}, {}, {}, {}};

  TypeTable Types;
  const auto BeforeTypes = Types.typeCount();
  const auto BeforeEffects = Types.effectCount();
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  REQUIRE_FALSE(Result.error().empty());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::WrongArity);
  CHECK(Types.typeCount() == BeforeTypes);
  CHECK(Types.effectCount() == BeforeEffects);
}

TEST_CASE("Semantic model structural projection rejects an unreachable "
          "callable contract row") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *UnusedArrow = Arena.make_arrow(Int, Int);
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{UnusedArrow,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{
          1,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "value"}},
          {},
          {},
          Contracts,
          TestRange}};
  const TypeScheme Scheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  REQUIRE_FALSE(Result.error().empty());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("callables") != std::string::npos);
  CHECK(Types.typeCount() == 0);
}

TEST_CASE("Semantic model structural type projection rejects duplicate "
          "qualified declaration identities atomically") {
  TypeArena Arena;
  const yona::model::NominalTypeKey Key{ModuleIdentity{"Main"}, "Token"};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Nominal,
                                 Key,
                                 {},
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Nominal,
                                 Key,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  SemanticProjectionBatch Batch{Scopes, {}, {}, {}, {}, {}, {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
  CHECK(Result.error().front().Path.find("identity") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Semantic model structural type projection rejects missing scheme "
          "effect roots atomically") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Arrow = Arena.make_arrow(Int, Int);
  const TypeScheme Scheme(Arrow);
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Trivial};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {Ownerships.begin(), Ownerships.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{
          1,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "bad"}},
          {},
          {},
          Contracts,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               Ownerships,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::WrongArrowShape);
  CHECK(Result.error().front().Path.find("effect-roots") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Semantic model structural type projection rejects duplicate "
          "operation declarations atomically") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const yona::model::EffectOperationKey Key{
      yona::model::NominalTypeKey{ModuleIdentity{"Main"}, "State"}, "get"};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 Key,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 2>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
                        1,
                        Key,
                        {},
                        {},
                        {},
                        {},
                        {},
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange},
                    yona::compiler::typechecker::OperationSchemeProjection{
                        1,
                        Key,
                        {},
                        {},
                        {},
                        {},
                        {},
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange}};
  const SemanticProjectionBatch Batch{Scopes, {}, Operations, {},
                                      {},     {}, {},         {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("operations[1]") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Semantic model structural type projection preserves the qualified "
          "effect catalog and fact atoms") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const EffectOperationKey Key{NominalTypeKey{ModuleIdentity{"Main"}, "State"},
                               "get"};
  const auto OperationAtom =
      commitAtom(Arena, OperationEffectAtom{Key, {Int}, {}});
  const auto RaiseAtom = commitAtom(Arena, MayRaiseEffectAtom{});
  const auto CancelAtom = commitAtom(Arena, MayCancelEffectAtom{});
  const auto FlexibleEffects = Arena.effect_solver().flexible();
  const auto FlexibleVariable =
      Arena.effect_solver().variable_id(FlexibleEffects);
  REQUIRE(FlexibleVariable.has_value());
  const auto FunctionEffects = Arena.effect_solver().join(
      {Arena.effect_solver().atoms({OperationAtom, RaiseAtom, CancelAtom}),
       Arena.effect_solver().mask(FlexibleEffects, {OperationAtom})});
  const auto *Arrow = Arena.make_arrow(Int, Int, FunctionEffects);
  TypeScheme Scheme(Arrow);
  Scheme.quantified_effect_variables.push_back(
      {*FlexibleVariable, yona::model::EffectVariableKind::Flexible});
  Scheme.effect_roots.push_back({Arrow, Arrow->arrow_effect});
  Scheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{Arrow->arrow_effect});

  const std::array<yona::compiler::typechecker::TypeId, 1> OperationTypes = {
      41};
  const auto *OperationResult = Arena.alloc(
      yona::compiler::typechecker::MonoType::make_var(OperationTypes[0], 0));
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Trivial};
  const std::array<yona::compiler::typechecker::QuantifiedEffectVariable, 1>
      FunctionEffectVariables = {
          yona::compiler::typechecker::QuantifiedEffectVariable{
              *FlexibleVariable, yona::model::EffectVariableKind::Flexible}};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {Ownerships.begin(), Ownerships.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 Key,
                                 OperationTypes,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{
          2,
          std::nullopt,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}},
          {},
          FunctionEffectVariables,
          Contracts,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               Scheme,
                               Ownerships,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 1>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
          1,
          Key,
          OperationTypes,
          {},
          {},
          {},
          {},
          OperationResult,
          ResultOwnership::Trivial,
          DeclarationVisibility::Module,
          Arena.effect_solver().empty(),
          TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, Operations, {},
                                      {},     {},        {},         {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.size() == 1);
  REQUIRE(Result->Operations.size() == 1);
  REQUIRE(Result->InterfaceSeed.Operations.size() == 1);
  const auto &Function =
      std::get<FunctionType>(Types.at(Result->Functions.front().Signature));
  const auto &Effects = Types.effect(Function.Effects);
  REQUIRE(Effects.Operations.size() == 1);
  CHECK(Effects.Operations.front().Key == Key);
  REQUIRE(Effects.Operations.front().TypeArguments.size() == 1);
  CHECK(std::get<PrimitiveType>(
            Types.at(Effects.Operations.front().TypeArguments.front())) ==
        PrimitiveType::Int);
  CHECK(Effects.MayRaise);
  CHECK(Effects.MayCancel);
  REQUIRE(Effects.Tails.size() == 1);
  CHECK(Effects.Tails.front().Kind ==
        yona::model::EffectVariableKind::Flexible);
  REQUIRE(Effects.Tails.front().ExcludedOperations.size() == 1);
  CHECK(Effects.Tails.front().ExcludedOperations.front().Key == Key);
}

TEST_CASE("Semantic model structural type projection rejects a local effect "
          "atom absent from the declaration catalog") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const EffectOperationKey Missing{
      NominalTypeKey{ModuleIdentity{"Main"}, "Missing"}, "run"};
  const auto Atom = commitAtom(Arena, OperationEffectAtom{Missing, {}, {}});
  const auto Effects = Arena.effect_solver().atoms({Atom});
  const auto *Arrow = Arena.make_arrow(Int, Int, Effects);
  TypeScheme Scheme(Arrow);
  Scheme.effect_roots.push_back({Arrow, Effects});
  Scheme.effect_graph = Arena.effect_solver().freeze(std::vector{Effects});
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Trivial};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {Ownerships.begin(), Ownerships.end()},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 Contracts,
                                 TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               Ownerships,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
  CHECK(Result.error().front().Path.find("operation") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Semantic model structural type projection retains inherited and "
          "phantom declaration binders") {
  TypeArena Arena;
  const auto *Inherited =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(71, 0));
  const auto *Declared =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(72, 0));
  const auto *Arrow = Arena.make_arrow(Inherited, Declared);
  TypeScheme Scheme({Declared->var_id}, Arrow);
  Scheme.effect_roots.push_back({Arrow, Arrow->arrow_effect});
  Scheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{Arrow->arrow_effect});

  const std::array<yona::compiler::typechecker::TypeId, 1> OuterTypes = {
      Inherited->var_id};
  const std::array<yona::compiler::typechecker::TypeId, 1> InnerTypes = {
      Declared->var_id};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Borrow};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {Ownerships.begin(), Ownerships.end()},
                           ResultOwnership::Owned,
                           CallingConvention::DirectYona,
                           TestRange}};
  const NominalTypeKey Box{ModuleIdentity{"Main"}, "Box"};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{
          2,
          1,
          BinderOwnerKind::Function,
          yona::model::FunctionDeclarationIdentity{
              yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "unwrap"}},
          InnerTypes,
          {},
          Contracts,
          TestRange},
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Nominal,
                                 Box,
                                 OuterTypes,
                                 {},
                                 {},
                                 TestRange}};
  const std::array<MonoTypePtr, 1> ConstructorFields = {Inherited};
  const std::array<yona::compiler::typechecker::ConstructorProjection, 1>
      Constructors = {yona::compiler::typechecker::ConstructorProjection{
          "Box", 0, ConstructorFields, TestRange}};
  const std::array<yona::compiler::typechecker::NominalDeclarationProjection, 1>
      Nominals = {yona::compiler::typechecker::NominalDeclarationProjection{
          1, Box, Constructors, DeclarationVisibility::Public, false,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               Scheme,
                               Ownerships,
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               std::nullopt,
                               std::nullopt,
                               DeclarationVisibility::Module,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, Nominals,
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE(Result.has_value());
  REQUIRE(Result->InterfaceSeed.Scopes.size() == 2);
  CHECK(Result->InterfaceSeed.Scopes[0].Kind == BinderOwnerKind::Nominal);
  CHECK(Result->InterfaceSeed.Scopes[1].Kind == BinderOwnerKind::Function);
  REQUIRE(Result->Functions.size() == 1);
  CHECK(Result->Functions.front().InheritedTypeParameters ==
        Result->InterfaceSeed.Scopes[0].TypeParameters);
  CHECK(Result->Functions.front().DeclaredTypeParameters ==
        Result->InterfaceSeed.Scopes[1].TypeParameters);
  REQUIRE(Result->InterfaceSeed.Nominals.size() == 1);
  const auto Field = Result->InterfaceSeed.Nominals.front()
                         .Constructors.front()
                         .Fields.front();
  CHECK(std::get<yona::model::TypeParameterType>(Types.at(Field)).Parameter ==
        Result->Functions.front().InheritedTypeParameters.front());
}

TEST_CASE("Semantic model structural type projection rejects an ambiguous "
          "phantom scheme instantiation") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Phantom =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(91, 0));
  const TypeScheme GenericScheme({Phantom->var_id}, Phantom);
  const TypeScheme CallerScheme(Int);
  const auto TargetIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "generic"}};
  const auto CallerIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "caller"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      Phantom->var_id};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 TargetIdentity,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 CallerIdentity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      NodeFacts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(),
          yona::compiler::typechecker::SchemeInstantiationProjection{
              TargetIdentity, {}, {}, TestRange},
          std::nullopt, std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const std::array<FunctionSchemeProjection, 2> Functions = {
      FunctionSchemeProjection{1,
                               GenericScheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               TargetIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange},
      FunctionSchemeProjection{2,
                               CallerScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               CallerIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               NodeFacts,
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::AmbiguousInstantiation);
  CHECK(Result.error().front().Path.find("instantiation") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Semantic model structural type projection matches sum alternatives "
          "by normalized type identity") {
  TypeArena Arena;
  const auto *BodyInt = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *AlternativeInt =
      Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const auto *Sum = Arena.make_sum({BodyInt, String});
  const TypeScheme Scheme(BodyInt);
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "inject"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      NodeFacts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, BodyInt, Arena.effect_solver().empty(), std::nullopt,
          std::nullopt, std::nullopt, std::nullopt,
          yona::compiler::typechecker::SumInjectionProjection{AlternativeInt,
                                                              Sum},
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               NodeFacts,
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.front().NodeFacts.front().SumInjection.has_value());
  CHECK(Result->Functions.front().NodeFacts.front().SumInjection->Alternative !=
        Result->Functions.front().NodeFacts.front().SumInjection->Sum);

  auto MismatchedFacts = NodeFacts;
  MismatchedFacts.front().Type = String;
  auto MismatchedFunctions = Functions;
  MismatchedFunctions.front().NodeFacts = MismatchedFacts;
  const SemanticProjectionBatch MismatchedBatch{
      Scopes, MismatchedFunctions, {}, {}, {}, {}, {}, {}};
  TypeTable MismatchedTypes;
  StructuralTypeProjection MismatchedProjection(
      MismatchedTypes, ModuleIdentity{"Main"}, Arena.effect_solver(),
      Arena.effect_atoms());
  const auto Mismatched =
      MismatchedProjection.freezeSemanticBatch(MismatchedBatch);
  REQUIRE_FALSE(Mismatched.has_value());
  CHECK(Mismatched.error().front().Path.find("node-type") != std::string::npos);
  CHECK(MismatchedTypes.typeCount() == 0);
}

TEST_CASE("Semantic model structural type projection rejects a sum injection "
          "whose target normalizes to a singleton") {
  TypeArena Arena;
  const auto *BodyInt = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *DuplicateInt =
      Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Sum = Arena.make_sum({BodyInt, DuplicateInt});
  const TypeScheme Scheme(BodyInt);
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "inject"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      NodeFacts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, BodyInt, Arena.effect_solver().empty(), std::nullopt,
          std::nullopt, std::nullopt, std::nullopt,
          yona::compiler::typechecker::SumInjectionProjection{BodyInt, Sum},
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               NodeFacts,
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},
                                      {},     {},        {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("sum") != std::string::npos);
  CHECK(Types.typeCount() == 0);
}

TEST_CASE("Semantic model structural type projection owns qualified traits "
          "instances and derivations") {
  TypeArena Arena;
  const auto *NominalParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(101, 0));
  const auto *InstanceParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(102, 0));
  const NominalTypeKey Box{ModuleIdentity{"Main"}, "Box"};
  const NominalTypeKey Show{ModuleIdentity{"Main"}, "Show"};
  const auto *InstanceHead = Arena.make_app(Box, {InstanceParameter}, "Box");
  const std::array<yona::compiler::typechecker::TypeId, 1> NominalTypes = {
      NominalParameter->var_id};
  const std::array<yona::compiler::typechecker::TypeId, 1> TraitTypes = {103};
  const std::array<yona::compiler::typechecker::TypeId, 1> InstanceTypes = {
      InstanceParameter->var_id};
  const std::array<DeclarationScopeProjection, 3> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Nominal,
                                 Box,
                                 NominalTypes,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Trait,
                                 Show,
                                 TraitTypes,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{
          3,
          std::nullopt,
          BinderOwnerKind::Instance,
          yona::compiler::typechecker::InstanceScopeIdentityProjection{
              Show, {InstanceHead}, {}},
          InstanceTypes,
          {},
          {},
          TestRange}};
  const std::array<MonoTypePtr, 1> Fields = {NominalParameter};
  const std::array<yona::compiler::typechecker::ConstructorProjection, 1>
      Constructors = {yona::compiler::typechecker::ConstructorProjection{
          "Box", 0, Fields, TestRange}};
  const std::array<yona::compiler::typechecker::NominalDeclarationProjection, 1>
      Nominals = {yona::compiler::typechecker::NominalDeclarationProjection{
          1, Box, Constructors, DeclarationVisibility::Public, false,
          TestRange}};
  const std::array<yona::compiler::typechecker::TraitDeclarationProjection, 1>
      Traits = {yona::compiler::typechecker::TraitDeclarationProjection{
          2, Show, {}, {}, DeclarationVisibility::Public, TestRange, true}};
  const std::array<MonoTypePtr, 1> InstanceArguments = {InstanceHead};
  const std::array<yona::compiler::typechecker::TraitInstanceProjection, 1>
      Instances = {yona::compiler::typechecker::TraitInstanceProjection{
          3,
          Show,
          InstanceArguments,
          {},
          {},
          DeclarationVisibility::Module,
          TestRange}};
  const std::array<NominalTypeKey, 1> DerivedTraits = {Show};
  const std::array<yona::compiler::typechecker::DeriveRequestProjection, 1>
      Derivations = {yona::compiler::typechecker::DeriveRequestProjection{
          Box, DerivedTraits, TestRange}};
  const SemanticProjectionBatch Batch{Scopes, {},     {},        Nominals,
                                      {},     Traits, Instances, Derivations};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE(Result.has_value());
  CHECK(Result->InterfaceSeed.Nominals.size() == 1);
  CHECK(Result->InterfaceSeed.Traits.size() == 1);
  CHECK(Result->InterfaceSeed.Instances.size() == 1);
  REQUIRE(Result->InterfaceSeed.Derivations.size() == 1);
  CHECK(Result->InterfaceSeed.Derivations.front().Type == Box);
  CHECK(Result->InterfaceSeed.Derivations.front().Traits ==
        std::vector<NominalTypeKey>{Show});
}

TEST_CASE("Structural projection rejects a trait default target with a "
          "different complete signature") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const auto *ExpectedArrow = Arena.make_arrow(Int, Int);
  const auto *TargetArrow = Arena.make_arrow(String, String);
  const NominalTypeKey TraitKey{ModuleIdentity{"Main"}, "Convert"};
  const yona::model::FunctionDeclarationIdentity MethodIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "Convert.convert"}};
  const yona::model::FunctionDeclarationIdentity TargetIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "convertDefault"}};
  const std::array<ParameterOwnership, 1> MethodOwnerships = {
      ParameterOwnership::Trivial};
  const std::array<ParameterOwnership, 1> TargetOwnerships = {
      ParameterOwnership::Consume};
  const std::array<CallableTypeContract, 1> MethodContracts = {
      CallableTypeContract{ExpectedArrow,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<CallableTypeContract, 1> TargetContracts = {
      CallableTypeContract{TargetArrow,
                           {ParameterOwnership::Consume},
                           ResultOwnership::Owned,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 3> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Trait,
                                 TraitKey,
                                 {},
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 1,
                                 BinderOwnerKind::Function,
                                 MethodIdentity,
                                 {},
                                 {},
                                 MethodContracts,
                                 TestRange},
      DeclarationScopeProjection{3,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 TargetIdentity,
                                 {},
                                 {},
                                 TargetContracts,
                                 TestRange}};
  TypeScheme MethodScheme(ExpectedArrow);
  MethodScheme.effect_roots.push_back(
      {ExpectedArrow, ExpectedArrow->arrow_effect});
  MethodScheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{ExpectedArrow->arrow_effect});
  TypeScheme TargetScheme(TargetArrow);
  TargetScheme.effect_roots.push_back({TargetArrow, TargetArrow->arrow_effect});
  TargetScheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{TargetArrow->arrow_effect});
  const std::array<yona::compiler::typechecker::TraitMethodProjection, 1>
      Methods = {yona::compiler::typechecker::TraitMethodProjection{
          2, "convert", MethodScheme, MethodOwnerships,
          ResultOwnership::Trivial, CallingConvention::DirectYona,
          yona::compiler::typechecker::ResolvedTraitTargetProjection{
              TargetIdentity, {}, {}},
          TestRange}};
  const std::array<yona::compiler::typechecker::TraitDeclarationProjection, 1>
      Traits = {yona::compiler::typechecker::TraitDeclarationProjection{
          1,
          TraitKey,
          {},
          Methods,
          DeclarationVisibility::Module,
          TestRange,
          false}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{3,
                               TargetScheme,
                               TargetOwnerships,
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               TargetIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, Traits, {}, {}});

  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("default/signature") !=
        std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection substitutes the instance head before "
          "validating a trait method target") {
  TypeArena Arena;
  const auto *TraitParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(901, 0));
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const auto *Bool = Arena.make_con(yona::compiler::typechecker::TyCon::Bool);
  const auto *ExpectedArrow = Arena.make_arrow(TraitParameter, Bool);
  const auto *TargetArrow = Arena.make_arrow(String, Bool);
  const NominalTypeKey TraitKey{ModuleIdentity{"Main"}, "Predicate"};
  const yona::model::FunctionDeclarationIdentity MethodIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "Predicate.test"}};
  const yona::model::FunctionDeclarationIdentity TargetIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "testInt"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TraitTypes = {
      TraitParameter->var_id};
  const std::array<MonoTypePtr, 1> InstanceArguments = {Int};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Trivial};
  const std::array<CallableTypeContract, 1> MethodContracts = {
      CallableTypeContract{ExpectedArrow,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<CallableTypeContract, 1> TargetContracts = {
      CallableTypeContract{TargetArrow,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 4> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Trait,
                                 TraitKey,
                                 TraitTypes,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 1,
                                 BinderOwnerKind::Function,
                                 MethodIdentity,
                                 {},
                                 {},
                                 MethodContracts,
                                 TestRange},
      DeclarationScopeProjection{
          3,
          std::nullopt,
          BinderOwnerKind::Instance,
          yona::compiler::typechecker::InstanceScopeIdentityProjection{
              TraitKey, {Int}, {}},
          {},
          {},
          {},
          TestRange},
      DeclarationScopeProjection{4,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 TargetIdentity,
                                 {},
                                 {},
                                 TargetContracts,
                                 TestRange}};
  TypeScheme MethodScheme(ExpectedArrow);
  MethodScheme.effect_roots.push_back(
      {ExpectedArrow, ExpectedArrow->arrow_effect});
  MethodScheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{ExpectedArrow->arrow_effect});
  TypeScheme TargetScheme(TargetArrow);
  TargetScheme.effect_roots.push_back({TargetArrow, TargetArrow->arrow_effect});
  TargetScheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{TargetArrow->arrow_effect});
  const std::array<yona::compiler::typechecker::TraitMethodProjection, 1>
      Methods = {yona::compiler::typechecker::TraitMethodProjection{
          2, "test", MethodScheme, Ownerships, ResultOwnership::Trivial,
          CallingConvention::DirectYona, std::nullopt, TestRange}};
  const std::array<yona::compiler::typechecker::TraitDeclarationProjection, 1>
      Traits = {yona::compiler::typechecker::TraitDeclarationProjection{
          1,
          TraitKey,
          {},
          Methods,
          DeclarationVisibility::Module,
          TestRange,
          false}};
  const std::array<yona::compiler::typechecker::TraitMethodBindingProjection, 1>
      Bindings = {yona::compiler::typechecker::TraitMethodBindingProjection{
          3, 2, "test",
          yona::compiler::typechecker::ResolvedTraitTargetProjection{
              TargetIdentity, {}, {}},
          TestRange}};
  const std::array<yona::compiler::typechecker::TraitInstanceProjection, 1>
      Instances = {yona::compiler::typechecker::TraitInstanceProjection{
          3,
          TraitKey,
          InstanceArguments,
          {},
          Bindings,
          DeclarationVisibility::Module,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{4,
                               TargetScheme,
                               Ownerships,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               TargetIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, Traits, Instances, {}});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("target/signature") !=
        std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection requires trait targets to preserve effects "
          "and calling convention") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const auto *MethodArrow = Arena.make_arrow(Int, Int);
  const auto Cancel = commitAtom(Arena, MayCancelEffectAtom{});
  const auto *NestedArrow = Arena.make_arrow(String, String);
  const auto *HigherOrderArrow = Arena.make_arrow(NestedArrow, Int);
  const auto *EffectfulNestedArrow =
      Arena.make_arrow(String, String, Arena.effect_solver().atoms({Cancel}));
  const auto *EffectfulHigherOrderArrow =
      Arena.make_arrow(EffectfulNestedArrow, Int);
  const auto *EffectfulTarget =
      Arena.make_arrow(Int, Int, Arena.effect_solver().atoms({Cancel}));
  const auto *DirectTarget = Arena.make_arrow(Int, Int);
  const auto *ConstraintParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(1199, 0));
  const auto *TargetConstraintParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(1200, 0));
  const NominalTypeKey TraitKey{ModuleIdentity{"Main"}, "Transform"};
  const NominalTypeKey Required{ModuleIdentity{"Prelude"}, "Required"};
  const NominalTypeKey AlsoRequired{ModuleIdentity{"Prelude"}, "AlsoRequired"};
  const yona::model::FunctionDeclarationIdentity MethodIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"},
                                  "Transform.transform"}};
  const yona::model::FunctionDeclarationIdentity TargetIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "transformDefault"}};
  enum class NestedMismatch {
    None,
    Parameter,
    Result,
    Convention,
    Effect,
    Constraint,
    ReorderedConstraints
  };
  const auto Project = [&](MonoTypePtr TargetArrow,
                           CallingConvention TargetConvention,
                           NestedMismatch Mismatch = NestedMismatch::None) {
    const bool HasNested = Mismatch == NestedMismatch::Parameter ||
                           Mismatch == NestedMismatch::Result ||
                           Mismatch == NestedMismatch::Convention ||
                           Mismatch == NestedMismatch::Effect;
    const bool HasConstraints =
        Mismatch == NestedMismatch::Constraint ||
        Mismatch == NestedMismatch::ReorderedConstraints;
    const auto *ExpectedArrow = HasNested ? HigherOrderArrow : MethodArrow;
    const std::vector<ParameterOwnership> RootOwnerships = {
        HasNested ? ParameterOwnership::Consume : ParameterOwnership::Trivial};
    std::vector<CallableTypeContract> MethodContracts = {CallableTypeContract{
        ExpectedArrow, RootOwnerships, ResultOwnership::Trivial,
        CallingConvention::DirectYona, TestRange}};
    std::vector<CallableTypeContract> TargetContracts = {CallableTypeContract{
        TargetArrow, RootOwnerships, ResultOwnership::Trivial, TargetConvention,
        TestRange}};
    const std::vector<yona::compiler::typechecker::TypeId>
        MethodTypeParameters =
            HasConstraints ? std::vector{ConstraintParameter->var_id}
                           : std::vector<yona::compiler::typechecker::TypeId>{};
    const std::vector<yona::compiler::typechecker::TypeId>
        TargetTypeParameters =
            Mismatch == NestedMismatch::ReorderedConstraints
                ? std::vector{TargetConstraintParameter->var_id}
                : std::vector<yona::compiler::typechecker::TypeId>{};
    if (HasNested) {
      const auto *TargetNestedArrow = Mismatch == NestedMismatch::Effect
                                          ? EffectfulNestedArrow
                                          : NestedArrow;
      MethodContracts.push_back(
          CallableTypeContract{NestedArrow,
                               {ParameterOwnership::Borrow},
                               ResultOwnership::Owned,
                               CallingConvention::ClosureEntry,
                               TestRange});
      TargetContracts.push_back(CallableTypeContract{
          TargetNestedArrow,
          {Mismatch == NestedMismatch::Parameter ? ParameterOwnership::Consume
                                                 : ParameterOwnership::Borrow},
          Mismatch == NestedMismatch::Result ? ResultOwnership::Trivial
                                             : ResultOwnership::Owned,
          Mismatch == NestedMismatch::Convention
              ? CallingConvention::Continuation
              : CallingConvention::ClosureEntry,
          TestRange});
    }
    const std::array<DeclarationScopeProjection, 3> Scopes = {
        DeclarationScopeProjection{1,
                                   std::nullopt,
                                   BinderOwnerKind::Trait,
                                   TraitKey,
                                   {},
                                   {},
                                   {},
                                   TestRange},
        DeclarationScopeProjection{2,
                                   1,
                                   BinderOwnerKind::Function,
                                   MethodIdentity,
                                   MethodTypeParameters,
                                   {},
                                   MethodContracts,
                                   TestRange},
        DeclarationScopeProjection{3,
                                   std::nullopt,
                                   BinderOwnerKind::Function,
                                   TargetIdentity,
                                   TargetTypeParameters,
                                   {},
                                   TargetContracts,
                                   TestRange}};
    TypeScheme MethodScheme(ExpectedArrow);
    if (HasConstraints)
      MethodScheme =
          TypeScheme({ConstraintParameter->var_id},
                     Mismatch == NestedMismatch::ReorderedConstraints
                         ? std::vector{yona::compiler::typechecker::Constraint(
                                           Required, ConstraintParameter),
                                       yona::compiler::typechecker::Constraint(
                                           AlsoRequired, ConstraintParameter)}
                         : std::vector{yona::compiler::typechecker::Constraint(
                               Required, ConstraintParameter)},
                     ExpectedArrow);
    MethodScheme.effect_roots.push_back(
        {ExpectedArrow, ExpectedArrow->arrow_effect});
    if (HasNested)
      MethodScheme.effect_roots.push_back(
          {NestedArrow, NestedArrow->arrow_effect});
    std::vector<yona::compiler::typechecker::EffectRef> MethodEffects;
    for (const auto &Root : MethodScheme.effect_roots)
      MethodEffects.push_back(Root.Effect);
    MethodScheme.effect_graph = Arena.effect_solver().freeze(MethodEffects);
    TypeScheme TargetScheme(TargetArrow);
    if (Mismatch == NestedMismatch::ReorderedConstraints)
      TargetScheme = TypeScheme({TargetConstraintParameter->var_id},
                                {yona::compiler::typechecker::Constraint(
                                     AlsoRequired, TargetConstraintParameter),
                                 yona::compiler::typechecker::Constraint(
                                     Required, TargetConstraintParameter)},
                                TargetArrow);
    TargetScheme.effect_roots.push_back(
        {TargetArrow, TargetArrow->arrow_effect});
    if (HasNested)
      TargetScheme.effect_roots.push_back(
          {Mismatch == NestedMismatch::Effect ? EffectfulNestedArrow
                                              : NestedArrow,
           (Mismatch == NestedMismatch::Effect ? EffectfulNestedArrow
                                               : NestedArrow)
               ->arrow_effect});
    std::vector<yona::compiler::typechecker::EffectRef> TargetEffects;
    for (const auto &Root : TargetScheme.effect_roots)
      TargetEffects.push_back(Root.Effect);
    TargetScheme.effect_graph = Arena.effect_solver().freeze(TargetEffects);
    const std::array<yona::compiler::typechecker::TraitMethodProjection, 1>
        Methods = {yona::compiler::typechecker::TraitMethodProjection{
            2, "transform", MethodScheme, RootOwnerships,
            ResultOwnership::Trivial, CallingConvention::DirectYona,
            yona::compiler::typechecker::ResolvedTraitTargetProjection{
                TargetIdentity,
                Mismatch == NestedMismatch::ReorderedConstraints
                    ? std::vector<MonoTypePtr>{ConstraintParameter}
                    : std::vector<MonoTypePtr>{},
                {}},
            TestRange}};
    const std::array<yona::compiler::typechecker::TraitDeclarationProjection, 1>
        Traits = {yona::compiler::typechecker::TraitDeclarationProjection{
            1,
            TraitKey,
            {},
            Methods,
            DeclarationVisibility::Module,
            TestRange,
            false}};
    const std::array<FunctionSchemeProjection, 1> Functions = {
        FunctionSchemeProjection{3,
                                 TargetScheme,
                                 RootOwnerships,
                                 ResultOwnership::Trivial,
                                 TargetConvention,
                                 TargetIdentity,
                                 std::nullopt,
                                 DeclarationVisibility::Private,
                                 {},
                                 TestRange}};
    const std::array<ImportedDeclarationCatalogEntry, 2> Imports = {
        ImportedDeclarationCatalogEntry{
            BinderOwnerKind::Trait, Required, 1, 0, {}, false, TestRange},
        ImportedDeclarationCatalogEntry{
            BinderOwnerKind::Trait, AlsoRequired, 1, 0, {}, false, TestRange}};
    TypeTable Types;
    StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                        Arena.effect_solver(),
                                        Arena.effect_atoms());
    auto Result = Projection.freezeSemanticBatch(
        {Scopes, Functions, {}, {}, {}, Traits, {}, {}, Imports});
    if (Mismatch != NestedMismatch::ReorderedConstraints) {
      CHECK(Types.typeCount() == 0);
      CHECK(Types.effectCount() == 0);
    }
    return Result;
  };

  SUBCASE("different closed effect row") {
    const auto Result = Project(EffectfulTarget, CallingConvention::DirectYona);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
    CHECK(Result.error().front().Path.find("default/signature") !=
          std::string::npos);
  }

  SUBCASE("different calling convention") {
    const auto Result = Project(DirectTarget, CallingConvention::AsyncAdapter);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
    CHECK(Result.error().front().Path.find("default/signature") !=
          std::string::npos);
  }

  SUBCASE("different nested callable ownership") {
    const auto Result = Project(HigherOrderArrow, CallingConvention::DirectYona,
                                NestedMismatch::Parameter);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
    CHECK(Result.error().front().Path.find("default/signature") !=
          std::string::npos);
  }

  SUBCASE("different nested callable result ownership") {
    const auto Result = Project(HigherOrderArrow, CallingConvention::DirectYona,
                                NestedMismatch::Result);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
  }

  SUBCASE("different nested callable convention") {
    const auto Result = Project(HigherOrderArrow, CallingConvention::DirectYona,
                                NestedMismatch::Convention);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
  }

  SUBCASE("different nested callable effect") {
    const auto Result =
        Project(EffectfulHigherOrderArrow, CallingConvention::DirectYona,
                NestedMismatch::Effect);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
  }

  SUBCASE("missing substituted constraint") {
    const auto Result = Project(DirectTarget, CallingConvention::DirectYona,
                                NestedMismatch::Constraint);
    CAPTURE(Result ? std::string{}
                   : Result.error().front().Path + ": " +
                         Result.error().front().Message);
    REQUIRE_FALSE(Result.has_value());
    CHECK(Result.error().front().Code ==
          StructuralProjectionErrorCode::InvalidInput);
    CHECK(Result.error().front().Path.find("default/signature") !=
          std::string::npos);
  }

  SUBCASE("equivalent substituted constraints may be reordered") {
    const auto Result = Project(DirectTarget, CallingConvention::DirectYona,
                                NestedMismatch::ReorderedConstraints);
    CAPTURE(Result ? std::string{}
                   : Result.error().front().Path + ": " +
                         Result.error().front().Message);
    REQUIRE(Result.has_value());
  }
}

TEST_CASE("Semantic model structural type projection rejects an invalid "
          "resource policy atomically") {
  TypeArena Arena;
  const NominalTypeKey Handle{ModuleIdentity{"Main"}, "Handle"};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Resource,
                                 Handle,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const std::array<yona::compiler::typechecker::ResourceDeclarationProjection,
                   1>
      Resources = {yona::compiler::typechecker::ResourceDeclarationProjection{
          1,
          Handle,
          {},
          yona::model::ResourceAbiKind::GenericResource,
          yona::model::ResourceShareability::Linear,
          std::string{"retain_handle"},
          "release_handle",
          DeclarationVisibility::Module,
          TestRange}};
  const SemanticProjectionBatch Batch{Scopes,    {}, {}, {},
                                      Resources, {}, {}, {}};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("resources[0]") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection validates borrowed imported declaration "
          "catalog identities and arities atomically") {
  TypeArena Arena;
  const NominalTypeKey ForeignBox{ModuleIdentity{"Foreign"}, "Box"};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, ForeignBox, 1, 0, {}, false, TestRange}};
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Box = Arena.make_app(ForeignBox, {Int}, "Box");
  const TypeScheme Scheme(Box);
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "useBox"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},     {},
                                      {},     {},        {}, Imports};

  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  CAPTURE(Result ? std::string{} : Result.error().front().Message);
  REQUIRE(Result.has_value());

  const std::array<ImportedDeclarationCatalogEntry, 1> ResourceImports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Resource, ForeignBox, 1, 0, {}, false, TestRange}};
  const auto ResourceResult = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, ResourceImports});
  CAPTURE(ResourceResult ? std::string{}
                         : ResourceResult.error().front().Message);
  REQUIRE(ResourceResult.has_value());
  const auto &ResourceFunction = std::get<FunctionType>(
      Types.at(ResourceResult->Functions.front().Signature));
  CHECK(std::holds_alternative<yona::model::ResourceType>(
      Types.at(ResourceFunction.Result)));

  const auto Before = Types.typeCount();
  const std::array<ImportedDeclarationCatalogEntry, 2> ConflictingImports = {
      Imports.front(),
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Resource, ForeignBox, 1, 0, {}, false, TestRange}};
  const SemanticProjectionBatch ConflictBatch{
      {}, {}, {}, {}, {}, {}, {}, {}, ConflictingImports};
  const auto Conflict = Projection.freezeSemanticBatch(ConflictBatch);
  REQUIRE_FALSE(Conflict.has_value());
  CHECK(Conflict.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
  CHECK(Types.typeCount() == Before);

  const std::array<ImportedDeclarationCatalogEntry, 2> TraitConflict = {
      Imports.front(),
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, ForeignBox, 1, 0, {"map"}, false, TestRange}};
  const auto ConflictingTrait = Projection.freezeSemanticBatch(
      {{}, {}, {}, {}, {}, {}, {}, {}, TraitConflict});
  REQUIRE_FALSE(ConflictingTrait.has_value());
  CHECK(ConflictingTrait.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);

  const NominalTypeKey LocalKey{ModuleIdentity{"Main"}, "Local"};
  const std::array<ImportedDeclarationCatalogEntry, 1> MislabeledLocal = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, LocalKey, 0, 0, {}, false, TestRange}};
  const auto LocalConflict = Projection.freezeSemanticBatch(
      {{}, {}, {}, {}, {}, {}, {}, {}, MislabeledLocal});
  REQUIRE_FALSE(LocalConflict.has_value());
  CHECK(LocalConflict.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
}

TEST_CASE("Structural projection rejects unknown and wrong-arity imported "
          "nominals") {
  TypeArena Arena;
  const NominalTypeKey ForeignBox{ModuleIdentity{"Foreign"}, "Box"};
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Box = Arena.make_app(ForeignBox, {Int}, "Box");
  const TypeScheme Scheme(Box);
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "useBox"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const SemanticProjectionBatch Unknown{Scopes, Functions, {}, {}, {},
                                        {},     {},        {}, {}};
  const auto UnknownResult = Projection.freezeSemanticBatch(Unknown);
  REQUIRE_FALSE(UnknownResult.has_value());
  CHECK(UnknownResult.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);

  const std::array<ImportedDeclarationCatalogEntry, 1> WrongArity = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, ForeignBox, 2, 0, {}, false, TestRange}};
  const SemanticProjectionBatch Wrong{Scopes, Functions, {}, {},        {},
                                      {},     {},        {}, WrongArity};
  const auto WrongResult = Projection.freezeSemanticBatch(Wrong);
  REQUIRE_FALSE(WrongResult.has_value());
  CHECK(WrongResult.error().front().Code ==
        StructuralProjectionErrorCode::WrongArity);
  CHECK(Types.typeCount() == 0);

  const EffectOperationKey LocalGet{
      NominalTypeKey{ModuleIdentity{"Main"}, "State"}, "get"};
  const std::array<ImportedDeclarationCatalogEntry, 1> MislabeledLocal = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Operation, LocalGet, 0, 0, {}, false, TestRange}};
  const auto LocalConflict = Projection.freezeSemanticBatch(
      {{}, {}, {}, {}, {}, {}, {}, {}, MislabeledLocal});
  REQUIRE_FALSE(LocalConflict.has_value());
  CHECK(LocalConflict.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
}

TEST_CASE("Structural projection rejects imported operation identity and "
          "arity conflicts") {
  TypeArena Arena;
  const EffectOperationKey ForeignGet{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "State"}, "get"};
  const std::array<ImportedDeclarationCatalogEntry, 2> Duplicate = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Operation, ForeignGet, 0, 0, {}, false, TestRange},
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Operation, ForeignGet, 1, 0, {}, false, TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const SemanticProjectionBatch Batch{{}, {}, {}, {},       {},
                                      {}, {}, {}, Duplicate};
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
  CHECK(Types.typeCount() == 0);
}

TEST_CASE("Structural projection validates complete imported function "
          "contracts atomically") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Arrow = Arena.make_arrow(Int, Int);
  const yona::model::FunctionDeclarationIdentity Identity{
      yona::model::SymbolIdentity{ModuleIdentity{"Foreign"}, "borrow"}};
  TypeScheme ImportedScheme(Arrow);
  ImportedScheme.effect_roots.push_back({Arrow, Arrow->arrow_effect});
  ImportedScheme.effect_graph =
      Arena.effect_solver().freeze(std::vector{Arrow->arrow_effect});
  ImportedDeclarationCatalogEntry Import{
      BinderOwnerKind::Function, Identity, 0, 0, {}, false, TestRange};
  Import.Function =
      yona::compiler::typechecker::ImportedFunctionDeclarationProjection{
          std::move(ImportedScheme),
          {ParameterOwnership::Borrow, ParameterOwnership::Borrow},
          ResultOwnership::Trivial,
          CallingConvention::DirectYona,
          {CallableTypeContract{
              Arrow,
              {ParameterOwnership::Borrow, ParameterOwnership::Borrow},
              ResultOwnership::Trivial,
              CallingConvention::DirectYona,
              TestRange}}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      std::move(Import)};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result =
      Projection.freezeSemanticBatch({{}, {}, {}, {}, {}, {}, {}, {}, Imports});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::WrongArity);
  CHECK(Result.error().front().Path.find("imports[0]/function") !=
        std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection enforces a complete imported operation "
          "contract at every occurrence") {
  TypeArena Arena;
  const auto *Parameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(811, 0));
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "State"}, "put"};
  const yona::model::FunctionDeclarationIdentity FunctionIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};
  ImportedDeclarationCatalogEntry Import{
      BinderOwnerKind::Operation, Key, 1, 0, {}, false, TestRange};
  Import.Operation =
      yona::compiler::typechecker::ImportedOperationDeclarationProjection{
          {Parameter->var_id},
          {},
          {},
          {Parameter},
          {ParameterOwnership::Borrow},
          Parameter,
          ResultOwnership::Owned,
          Arena.effect_solver().empty(),
          {}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      std::move(Import)};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {Int},
                  {},
                  {String},
                  {ParameterOwnership::Borrow},
                  Int,
                  ResultOwnership::Owned};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("application") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection compares nested callable ABI contracts in "
          "imported operation occurrences") {
  TypeArena Arena;
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Callback = Arena.make_arrow(String, String);
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "Async"}, "submit"};
  const yona::model::FunctionDeclarationIdentity FunctionIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};

  ImportedDeclarationCatalogEntry Import{
      BinderOwnerKind::Operation, Key, 0, 0, {}, false, TestRange};
  Import.Operation =
      yona::compiler::typechecker::ImportedOperationDeclarationProjection{
          {},
          {},
          {},
          {Callback},
          {ParameterOwnership::Consume},
          Int,
          ResultOwnership::Trivial,
          Arena.effect_solver().empty(),
          {CallableTypeContract{Callback,
                                {ParameterOwnership::Borrow},
                                ResultOwnership::Owned,
                                CallingConvention::ClosureEntry,
                                TestRange}}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      std::move(Import)};
  const std::array<CallableTypeContract, 1> ActualCallbackContracts = {
      CallableTypeContract{Callback,
                           {ParameterOwnership::Consume},
                           ResultOwnership::Owned,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 ActualCallbackContracts,
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {},
                  {},
                  {Callback},
                  {ParameterOwnership::Consume},
                  Int,
                  ResultOwnership::Trivial};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("application") != std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection treats equal qualified nominal keys as "
          "authoritative over operation display aliases") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const NominalTypeKey Token{ModuleIdentity{"Foreign"}, "Token"};
  const auto *DeclaredToken = Arena.make_app(Token, {}, "DeclaredTokenAlias");
  const auto *OccurrenceToken =
      Arena.make_app(Token, {}, "OccurrenceTokenAlias");
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "Tokens"}, "inspect"};
  const yona::model::FunctionDeclarationIdentity FunctionIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};
  ImportedDeclarationCatalogEntry OperationImport{
      BinderOwnerKind::Operation, Key, 0, 0, {}, false, TestRange};
  OperationImport.Operation =
      yona::compiler::typechecker::ImportedOperationDeclarationProjection{
          {},
          {},
          {},
          {DeclaredToken},
          {ParameterOwnership::Consume},
          Int,
          ResultOwnership::Trivial,
          Arena.effect_solver().empty(),
          {}};
  const std::array<ImportedDeclarationCatalogEntry, 2> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, Token, 0, 0, {}, false, TestRange},
      std::move(OperationImport)};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {},
                  {},
                  {OccurrenceToken},
                  {ParameterOwnership::Consume},
                  Int,
                  ResultOwnership::Trivial};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});

  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.front().NodeFacts.front().OperationOccurrence);
  const auto &Perform = std::get<yona::semantics::ProjectedPerformFact>(
      *Result->Functions.front().NodeFacts.front().OperationOccurrence);
  REQUIRE(Perform.Application.Parameters.size() == 1);
  CHECK(std::get<yona::model::NominalType>(
            Types.at(Perform.Application.Parameters.front().Type))
            .Key == Token);
}

TEST_CASE("Structural projection never dispatches a resolved nominal through "
          "a built-in display spelling") {
  TypeArena Arena;
  const NominalTypeKey UserSeq{ModuleIdentity{"Foreign"}, "UserSeq"};
  const auto *Type = Arena.make_app(UserSeq, {}, "Seq");
  const yona::model::FunctionDeclarationIdentity FunctionIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "value"}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const TypeScheme Scheme(Type);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, UserSeq, 0, 0, {}, false, TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});

  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
  const auto &Signature =
      std::get<FunctionType>(Types.at(Result->Functions.front().Signature));
  CHECK(std::get<yona::model::NominalType>(Types.at(Signature.Result)).Key ==
        UserSeq);
}

TEST_CASE("Structural projection rejects opaque binders on imported operation "
          "declarations") {
  TypeArena Arena;
  const auto Opaque = Arena.effect_solver().opaque();
  const auto Variable = Arena.effect_solver().variable_id(Opaque);
  REQUIRE(Variable.has_value());
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "State"}, "get"};
  ImportedDeclarationCatalogEntry Import{
      BinderOwnerKind::Operation, Key, 0, 1, {}, false, TestRange};
  Import.Operation =
      yona::compiler::typechecker::ImportedOperationDeclarationProjection{
          {},
          {{*Variable, yona::model::EffectVariableKind::Opaque}},
          {},
          {},
          {},
          Int,
          ResultOwnership::Trivial,
          Arena.effect_solver().empty(),
          {}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      std::move(Import)};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result =
      Projection.freezeSemanticBatch({{}, {}, {}, {}, {}, {}, {}, {}, Imports});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::BinderKindMismatch);
  CHECK(Result.error().front().Path.find("imports[0]/operation") !=
        std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection validates complete imported resource "
          "policies atomically") {
  TypeArena Arena;
  const NominalTypeKey Handle{ModuleIdentity{"Foreign"}, "Handle"};
  ImportedDeclarationCatalogEntry Import{
      BinderOwnerKind::Resource, Handle, 0, 0, {}, false, TestRange};
  Import.Resource =
      yona::compiler::typechecker::ImportedResourceDeclarationProjection{
          {},
          yona::model::ResourceAbiKind::GenericResource,
          yona::model::ResourceShareability::Linear,
          std::string{"retain_handle"},
          "release_handle"};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      std::move(Import)};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result =
      Projection.freezeSemanticBatch({{}, {}, {}, {}, {}, {}, {}, {}, Imports});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("imports[0]/resource") !=
        std::string::npos);
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection owns imported operation and resource facts "
          "after inference storage is destroyed") {
  const NominalTypeKey Handle{ModuleIdentity{"Foreign"}, "Handle"};
  const EffectOperationKey Read{
      NominalTypeKey{ModuleIdentity{"Foreign"}, "Console"}, "read"};
  const yona::model::FunctionDeclarationIdentity FunctionIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  TypeTable Types;
  yona::semantics::ProjectedSemanticBatch Saved;

  {
    TypeArena Arena;
    const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
    const auto *HandleType = Arena.make_app(Handle, {});
    ImportedDeclarationCatalogEntry OperationImport{
        BinderOwnerKind::Operation, Read, 0, 0, {}, false, TestRange};
    OperationImport.Operation =
        yona::compiler::typechecker::ImportedOperationDeclarationProjection{
            {},
            {},
            {},
            {Int},
            {ParameterOwnership::Trivial},
            Int,
            ResultOwnership::Trivial,
            Arena.effect_solver().empty(),
            {}};
    ImportedDeclarationCatalogEntry ResourceImport{
        BinderOwnerKind::Resource, Handle, 0, 0, {}, false, TestRange};
    ResourceImport.Resource =
        yona::compiler::typechecker::ImportedResourceDeclarationProjection{
            {},
            yona::model::ResourceAbiKind::GenericResource,
            yona::model::ResourceShareability::Linear,
            std::nullopt,
            "release_handle"};
    const std::array<ImportedDeclarationCatalogEntry, 2> Imports = {
        std::move(OperationImport), std::move(ResourceImport)};
    const std::array<DeclarationScopeProjection, 1> Scopes = {
        DeclarationScopeProjection{1,
                                   std::nullopt,
                                   BinderOwnerKind::Function,
                                   FunctionIdentity,
                                   {},
                                   {},
                                   {},
                                   TestRange}};
    const yona::compiler::typechecker::ResolvedOperationApplicationProjection
        Application{Read,
                    {},
                    {},
                    {Int},
                    {ParameterOwnership::Trivial},
                    Int,
                    ResultOwnership::Trivial};
    const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
        Facts = {yona::compiler::typechecker::NodeSemanticProjection{
            &Node, Int, Arena.effect_solver().empty(), std::nullopt,
            yona::compiler::typechecker::PerformProjection{
                Application, Int, Arena.effect_solver().empty(), TestRange},
            std::nullopt, std::nullopt, std::nullopt, TestRange}};
    const TypeScheme FunctionScheme(HandleType);
    const std::array<FunctionSchemeProjection, 1> Functions = {
        FunctionSchemeProjection{1,
                                 FunctionScheme,
                                 {},
                                 ResultOwnership::Owned,
                                 CallingConvention::DirectYona,
                                 FunctionIdentity,
                                 std::nullopt,
                                 DeclarationVisibility::Private,
                                 Facts,
                                 TestRange}};
    StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                        Arena.effect_solver(),
                                        Arena.effect_atoms());
    auto Result = Projection.freezeSemanticBatch(
        {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});
    CAPTURE(Result ? std::string{}
                   : Result.error().front().Path + ": " +
                         Result.error().front().Message);
    REQUIRE(Result.has_value());
    Saved = std::move(*Result);
  }

  REQUIRE(Saved.Functions.size() == 1);
  REQUIRE(std::holds_alternative<FunctionType>(
      Types.at(Saved.Functions[0].Signature)));
  const auto &Signature =
      std::get<FunctionType>(Types.at(Saved.Functions[0].Signature));
  REQUIRE(std::holds_alternative<yona::model::ResourceType>(
      Types.at(Signature.Result)));
  const auto &Resource =
      std::get<yona::model::ResourceType>(Types.at(Signature.Result));
  CHECK(Resource.Key == Handle);
  REQUIRE(Saved.Functions[0].NodeFacts.size() == 1);
  REQUIRE(Saved.Functions[0].NodeFacts[0].OperationOccurrence.has_value());
  const auto &Perform = std::get<yona::semantics::ProjectedPerformFact>(
      *Saved.Functions[0].NodeFacts[0].OperationOccurrence);
  CHECK(Perform.Application.Key == Read);
  REQUIRE(Perform.Application.Parameters.size() == 1);
  REQUIRE(std::holds_alternative<PrimitiveType>(
      Types.at(Perform.Application.Parameters[0].Type)));
  CHECK(std::get<PrimitiveType>(Types.at(
            Perform.Application.Parameters[0].Type)) == PrimitiveType::Int);
  REQUIRE(std::holds_alternative<PrimitiveType>(
      Types.at(Perform.Application.ResultType)));
  CHECK(std::get<PrimitiveType>(Types.at(Perform.Application.ResultType)) ==
        PrimitiveType::Int);
}

TEST_CASE("Structural projection translates foreign effect references into a "
          "stable transactional diagnostic") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto Empty = Arena.effect_solver().empty();
  const yona::compiler::typechecker::EffectRef InvalidEffect{Empty.owner,
                                                             UINT32_MAX - 1};
  const auto *Arrow =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_arrow(
          Int, Int, InvalidEffect, &Arena.effect_solver(),
          &Arena.effect_atoms()));
  TypeScheme Scheme(Arrow);
  Scheme.effect_roots.push_back({Arrow, InvalidEffect});
  const yona::model::FunctionDeclarationIdentity Identity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "invalidEffect"}};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Trivial};
  const std::array<CallableTypeContract, 1> Contracts = {
      CallableTypeContract{Arrow,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::DirectYona,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 {},
                                 {},
                                 Contracts,
                                 TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               Ownerships,
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());

  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}});

  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::ForeignEffect);
  CHECK(Result.error().front().Path == "functions[0]/signature/effect-roots");
  CHECK(Result.error().front().Message ==
        "effect reference is invalid or belongs to another solver");
  CHECK(Types.typeCount() == 0);
  CHECK(Types.effectCount() == 0);
}

TEST_CASE("Structural projection rejects operation occurrence types that are "
          "not the declared generic substitution") {
  TypeArena Arena;
  const auto *Parameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(501, 0));
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *String =
      Arena.make_con(yona::compiler::typechecker::TyCon::String);
  const EffectOperationKey Key{NominalTypeKey{ModuleIdentity{"Main"}, "State"},
                               "put"};
  const auto FunctionIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "run"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      Parameter->var_id};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 Key,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 {},
                                 TestRange}};
  const std::array<MonoTypePtr, 1> DeclaredParameters = {Parameter};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Borrow};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 1>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
          1,
          Key,
          TypeParameters,
          {},
          {},
          DeclaredParameters,
          Ownerships,
          Parameter,
          ResultOwnership::Owned,
          DeclarationVisibility::Module,
          Arena.effect_solver().empty(),
          TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const std::array<MonoTypePtr, 1> TypeArguments = {Int};
  const std::array<MonoTypePtr, 1> WrongParameters = {String};
  yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {TypeArguments.begin(), TypeArguments.end()},
                  {},
                  {WrongParameters.begin(), WrongParameters.end()},
                  {Ownerships.begin(), Ownerships.end()},
                  Int,
                  ResultOwnership::Owned};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, Operations, {},
                                      {},     {},        {},         {}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(Result.error().front().Path.find("application") != std::string::npos);
  CHECK(Types.typeCount() == 0);
}

TEST_CASE("Structural projection takes callable ABI provenance from a generic "
          "operation type argument") {
  TypeArena Arena;
  const auto *Parameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(551, 0));
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *Callback = Arena.make_arrow(Int, Int);
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Main"}, "Callbacks"}, "roundTrip"};
  const auto FunctionIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "runCallback"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      Parameter->var_id};
  const std::array<CallableTypeContract, 1> CallbackContracts = {
      CallableTypeContract{Callback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 2> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 Key,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 CallbackContracts,
                                 TestRange}};
  const std::array<MonoTypePtr, 1> DeclaredParameters = {Parameter};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Borrow};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 1>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
          1,
          Key,
          TypeParameters,
          {},
          {},
          DeclaredParameters,
          Ownerships,
          Parameter,
          ResultOwnership::Owned,
          DeclarationVisibility::Module,
          Arena.effect_solver().empty(),
          TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {Callback},
                  {},
                  {Callback},
                  {ParameterOwnership::Borrow},
                  Callback,
                  ResultOwnership::Owned};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, Operations, {}, {}, {}, {}, {}});

  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
  REQUIRE(Result->Functions.front().NodeFacts.front().OperationOccurrence);
}

TEST_CASE("Structural projection canonicalizes aliased generic operation "
          "effect substitutions") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto FirstEffect = Arena.effect_solver().flexible();
  const auto SecondEffect = Arena.effect_solver().flexible();
  const auto DeclaredEffects =
      Arena.effect_solver().join({FirstEffect, SecondEffect});
  const auto *NestedCallback = Arena.make_arrow(Int, Int);
  const EffectOperationKey MarkerKey{
      NominalTypeKey{ModuleIdentity{"Main"}, "Callbacks"}, "marker"};
  const auto MarkerAtom =
      commitAtom(Arena, OperationEffectAtom{MarkerKey, {NestedCallback}, {}});
  const auto ActualEffects = Arena.effect_solver().atoms({MarkerAtom});
  const auto *DeclaredCallback = Arena.make_arrow(Int, Int, DeclaredEffects);
  const auto *ActualCallback = Arena.make_arrow(Int, Int, ActualEffects);
  const auto FirstVariable =
      Arena.effect_solver().summarize(FirstEffect).tails.front().variable;
  const auto SecondVariable =
      Arena.effect_solver().summarize(SecondEffect).tails.front().variable;
  const std::array<yona::compiler::typechecker::QuantifiedEffectVariable, 2>
      EffectVariables = {
          yona::compiler::typechecker::QuantifiedEffectVariable{
              FirstVariable, yona::model::EffectVariableKind::Flexible},
          yona::compiler::typechecker::QuantifiedEffectVariable{
              SecondVariable, yona::model::EffectVariableKind::Flexible}};
  const EffectOperationKey Key{
      NominalTypeKey{ModuleIdentity{"Main"}, "Callbacks"}, "invoke"};
  const auto FunctionIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "invokeCallback"}};
  const std::array<CallableTypeContract, 1> DeclaredContracts = {
      CallableTypeContract{DeclaredCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const std::array<CallableTypeContract, 2> ActualContracts = {
      CallableTypeContract{ActualCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange},
      CallableTypeContract{NestedCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const auto *MarkerParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(552, 0));
  const std::array<yona::compiler::typechecker::TypeId, 1> MarkerParameters = {
      MarkerParameter->var_id};
  const std::array<DeclarationScopeProjection, 3> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 Key,
                                 {},
                                 EffectVariables,
                                 DeclaredContracts,
                                 TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 ActualContracts,
                                 TestRange},
      DeclarationScopeProjection{3,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 MarkerKey,
                                 MarkerParameters,
                                 {},
                                 {},
                                 TestRange}};
  const std::array<MonoTypePtr, 1> DeclaredParameters = {DeclaredCallback};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Borrow};
  const std::array<MonoTypePtr, 0> NoParameters = {};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 2>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
                        1,
                        Key,
                        {},
                        EffectVariables,
                        {},
                        DeclaredParameters,
                        Ownerships,
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange},
                    yona::compiler::typechecker::OperationSchemeProjection{
                        3,
                        MarkerKey,
                        MarkerParameters,
                        {},
                        {},
                        NoParameters,
                        {},
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{Key,
                  {},
                  {ActualEffects, ActualEffects},
                  {ActualCallback},
                  {ParameterOwnership::Borrow},
                  Int,
                  ResultOwnership::Trivial};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme FunctionScheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               FunctionScheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, Operations, {}, {}, {}, {}, {}});

  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
}

TEST_CASE("Structural projection applies generic operation masks after type "
          "substitution") {
  TypeArena Arena;
  const auto *Int = Arena.make_con(yona::compiler::typechecker::TyCon::Int);
  const auto *TypeParameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(553, 0));
  const auto Effects = Arena.effect_solver().flexible();
  const auto EffectVariable =
      Arena.effect_solver().summarize(Effects).tails.front().variable;
  const auto *NestedCallback = Arena.make_arrow(Int, Int);
  const EffectOperationKey MarkerKey{
      NominalTypeKey{ModuleIdentity{"Main"}, "Callbacks"}, "marker"};
  const auto GenericMarker =
      commitAtom(Arena, OperationEffectAtom{MarkerKey, {TypeParameter}, {}});
  const auto ConcreteMarker =
      commitAtom(Arena, OperationEffectAtom{MarkerKey, {NestedCallback}, {}});
  const auto *DeclaredCallback = Arena.make_arrow(
      Int, Int, Arena.effect_solver().mask(Effects, {GenericMarker}));
  const auto *ActualCallback = Arena.make_arrow(Int, Int);
  const EffectOperationKey InvokeKey{
      NominalTypeKey{ModuleIdentity{"Main"}, "Callbacks"}, "invokeMasked"};
  const auto FunctionIdentity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "invokeMasked"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      TypeParameter->var_id};
  const std::array<yona::compiler::typechecker::QuantifiedEffectVariable, 1>
      EffectParameters = {yona::compiler::typechecker::QuantifiedEffectVariable{
          EffectVariable, yona::model::EffectVariableKind::Flexible}};
  const std::array<CallableTypeContract, 1> DeclaredContracts = {
      CallableTypeContract{DeclaredCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const std::array<CallableTypeContract, 2> ActualContracts = {
      CallableTypeContract{ActualCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange},
      CallableTypeContract{NestedCallback,
                           {ParameterOwnership::Trivial},
                           ResultOwnership::Trivial,
                           CallingConvention::ClosureEntry,
                           TestRange}};
  const std::array<DeclarationScopeProjection, 3> Scopes = {
      DeclarationScopeProjection{1, std::nullopt, BinderOwnerKind::Operation,
                                 InvokeKey, TypeParameters, EffectParameters,
                                 DeclaredContracts, TestRange},
      DeclarationScopeProjection{2,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 FunctionIdentity,
                                 {},
                                 {},
                                 ActualContracts,
                                 TestRange},
      DeclarationScopeProjection{3,
                                 std::nullopt,
                                 BinderOwnerKind::Operation,
                                 MarkerKey,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange}};
  const std::array<MonoTypePtr, 1> InvokeParameters = {DeclaredCallback};
  const std::array<ParameterOwnership, 1> Ownerships = {
      ParameterOwnership::Borrow};
  const std::array<MonoTypePtr, 0> NoParameters = {};
  const std::array<yona::compiler::typechecker::OperationSchemeProjection, 2>
      Operations = {yona::compiler::typechecker::OperationSchemeProjection{
                        1,
                        InvokeKey,
                        TypeParameters,
                        EffectParameters,
                        {},
                        InvokeParameters,
                        Ownerships,
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange},
                    yona::compiler::typechecker::OperationSchemeProjection{
                        3,
                        MarkerKey,
                        TypeParameters,
                        {},
                        {},
                        NoParameters,
                        {},
                        Int,
                        ResultOwnership::Trivial,
                        DeclarationVisibility::Module,
                        Arena.effect_solver().empty(),
                        TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const yona::compiler::typechecker::ResolvedOperationApplicationProjection
      Application{InvokeKey,
                  {NestedCallback},
                  {Arena.effect_solver().atoms({ConcreteMarker})},
                  {ActualCallback},
                  {ParameterOwnership::Borrow},
                  Int,
                  ResultOwnership::Trivial};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Int, Arena.effect_solver().empty(), std::nullopt,
          yona::compiler::typechecker::PerformProjection{
              Application, Int, Arena.effect_solver().empty(), TestRange},
          std::nullopt, std::nullopt, std::nullopt, TestRange}};
  const TypeScheme Scheme(Int);
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{2,
                               Scheme,
                               {},
                               ResultOwnership::Trivial,
                               CallingConvention::DirectYona,
                               FunctionIdentity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, Operations, {}, {}, {}, {}, {}});
  CAPTURE(Result ? std::string{}
                 : Result.error().front().Path + ": " +
                       Result.error().front().Message);
  REQUIRE(Result.has_value());
}

TEST_CASE("Structural projection rejects deferred trait evidence not owned by "
          "the named scheme") {
  TypeArena Arena;
  const auto *Parameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(601, 0));
  const NominalTypeKey Eq{ModuleIdentity{"Prelude"}, "Eq"};
  const yona::compiler::typechecker::Constraint Constraint(Eq, Parameter);
  TypeScheme Scheme({Parameter->var_id}, Parameter);
  Scheme.constraints.push_back(Constraint);
  const auto Owner = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "owner"}};
  const auto Missing = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Foreign"}, "missing"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      Parameter->var_id};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Owner,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange}};
  yona::ast::IntegerExpr Node(TestRange, 1);
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      Facts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Parameter, Arena.effect_solver().empty(), std::nullopt,
          std::nullopt,
          yona::compiler::typechecker::TraitMethodSelectionProjection{
              Eq,
              "eq",
              {Parameter},
              {},
              {},
              yona::compiler::typechecker::DeferredTraitEvidenceProjection{
                  Missing, Constraint},
              TestRange},
          std::nullopt, std::nullopt, TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Owner,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               Facts,
                               TestRange}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, Eq, 1, 0, {"eq"}, true, TestRange}};
  const SemanticProjectionBatch Batch{Scopes, Functions, {}, {},     {},
                                      {},     {},        {}, Imports};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(Batch);
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::UnresolvedIdentity);
  CHECK(Result.error().front().Path.find("evidence") != std::string::npos);
  CHECK(Types.typeCount() == 0);

  auto ValidUse = *Facts.front().TraitMethodUse;
  ValidUse.Evidence =
      yona::compiler::typechecker::DeferredTraitEvidenceProjection{Owner,
                                                                   Constraint};
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      ValidFacts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Parameter, Arena.effect_solver().empty(), std::nullopt,
          std::nullopt, std::move(ValidUse), std::nullopt, std::nullopt,
          TestRange}};
  const std::array<FunctionSchemeProjection, 1> ValidFunctions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Owner,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               ValidFacts,
                               TestRange}};
  const auto Valid = Projection.freezeSemanticBatch(
      {Scopes, ValidFunctions, {}, {}, {}, {}, {}, {}, Imports});
  CAPTURE(Valid ? std::string{} : Valid.error().front().Message);
  REQUIRE(Valid.has_value());

  const NominalTypeKey Relation{ModuleIdentity{"Prelude"}, "Relation"};
  const NominalTypeKey Token{ModuleIdentity{"Foreign"}, "Token"};
  const auto *ExpectedToken = Arena.make_app(Token, {}, "ExpectedAlias");
  const auto *EvidenceToken = Arena.make_app(Token, {}, "EvidenceAlias");
  const yona::compiler::typechecker::Constraint ExpectedConstraint(
      Relation, std::vector<MonoTypePtr>{Parameter, ExpectedToken});
  const yona::compiler::typechecker::Constraint EvidenceConstraint(
      Relation, std::vector<MonoTypePtr>{Parameter, EvidenceToken});
  TypeScheme AliasedScheme({Parameter->var_id}, Parameter);
  AliasedScheme.constraints.push_back(ExpectedConstraint);
  const std::array<yona::compiler::typechecker::NodeSemanticProjection, 1>
      AliasedFacts = {yona::compiler::typechecker::NodeSemanticProjection{
          &Node, Parameter, Arena.effect_solver().empty(), std::nullopt,
          std::nullopt,
          yona::compiler::typechecker::TraitMethodSelectionProjection{
              Relation,
              "relate",
              {Parameter, EvidenceToken},
              {},
              {},
              yona::compiler::typechecker::DeferredTraitEvidenceProjection{
                  Owner, EvidenceConstraint},
              TestRange},
          std::nullopt, std::nullopt, TestRange}};
  const std::array<FunctionSchemeProjection, 1> AliasedFunctions = {
      FunctionSchemeProjection{1,
                               AliasedScheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Owner,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               AliasedFacts,
                               TestRange}};
  const std::array<ImportedDeclarationCatalogEntry, 2> AliasedImports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, Relation, 2, 0, {"relate"}, false, TestRange},
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Nominal, Token, 0, 0, {}, false, TestRange}};
  TypeTable AliasedTypes;
  StructuralTypeProjection AliasedProjection(
      AliasedTypes, ModuleIdentity{"Main"}, Arena.effect_solver(),
      Arena.effect_atoms());
  const auto Aliased = AliasedProjection.freezeSemanticBatch(
      {Scopes, AliasedFunctions, {}, {}, {}, {}, {}, {}, AliasedImports});
  CAPTURE(Aliased ? std::string{} : Aliased.error().front().Message);
  REQUIRE(Aliased.has_value());
}

TEST_CASE("Structural projection rejects imported trait arity mismatches") {
  TypeArena Arena;
  const auto *Parameter =
      Arena.alloc(yona::compiler::typechecker::MonoType::make_var(701, 0));
  const NominalTypeKey Eq{ModuleIdentity{"Prelude"}, "Eq"};
  TypeScheme Scheme({Parameter->var_id}, Parameter);
  Scheme.constraints.emplace_back(Eq, Parameter);
  const auto Identity = yona::model::FunctionDeclarationIdentity{
      yona::model::SymbolIdentity{ModuleIdentity{"Main"}, "owner"}};
  const std::array<yona::compiler::typechecker::TypeId, 1> TypeParameters = {
      Parameter->var_id};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Function,
                                 Identity,
                                 TypeParameters,
                                 {},
                                 {},
                                 TestRange}};
  const std::array<FunctionSchemeProjection, 1> Functions = {
      FunctionSchemeProjection{1,
                               Scheme,
                               {},
                               ResultOwnership::Owned,
                               CallingConvention::DirectYona,
                               Identity,
                               std::nullopt,
                               DeclarationVisibility::Private,
                               {},
                               TestRange}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, Eq, 2, 0, {"eq"}, true, TestRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto Result = Projection.freezeSemanticBatch(
      {Scopes, Functions, {}, {}, {}, {}, {}, {}, Imports});
  REQUIRE_FALSE(Result.has_value());
  CHECK(Result.error().front().Code ==
        StructuralProjectionErrorCode::WrongArity);
  CHECK(Types.typeCount() == 0);
}

TEST_CASE("Structural projection validates derivation eligibility and source "
          "provenance") {
  TypeArena Arena;
  const NominalTypeKey Box{ModuleIdentity{"Main"}, "Box"};
  const NominalTypeKey Custom{ModuleIdentity{"Foreign"}, "Custom"};
  const SourceRange DeclarationRange{SourceId{1}, 1, 1, 0, 4};
  const SourceRange OtherRange{SourceId{2}, 1, 1, 0, 4};
  const std::array<DeclarationScopeProjection, 1> Scopes = {
      DeclarationScopeProjection{1,
                                 std::nullopt,
                                 BinderOwnerKind::Nominal,
                                 Box,
                                 {},
                                 {},
                                 {},
                                 DeclarationRange}};
  const std::array<yona::compiler::typechecker::NominalDeclarationProjection, 1>
      Nominals = {yona::compiler::typechecker::NominalDeclarationProjection{
          1, Box, {}, DeclarationVisibility::Private, false, DeclarationRange}};
  const std::array<ImportedDeclarationCatalogEntry, 1> Imports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, Custom, 1, 0, {}, false, TestRange}};
  const std::array<NominalTypeKey, 1> Traits = {Custom};
  const std::array<yona::compiler::typechecker::DeriveRequestProjection, 1>
      Unsupported = {yona::compiler::typechecker::DeriveRequestProjection{
          Box, Traits, DeclarationRange}};
  TypeTable Types;
  StructuralTypeProjection Projection(Types, ModuleIdentity{"Main"},
                                      Arena.effect_solver(),
                                      Arena.effect_atoms());
  const auto UnsupportedResult = Projection.freezeSemanticBatch(
      {Scopes, {}, {}, Nominals, {}, {}, {}, Unsupported, Imports});
  REQUIRE_FALSE(UnsupportedResult.has_value());
  CHECK(UnsupportedResult.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);

  const std::array<ImportedDeclarationCatalogEntry, 1> DerivableImports = {
      ImportedDeclarationCatalogEntry{
          BinderOwnerKind::Trait, Custom, 1, 0, {}, true, TestRange}};
  const std::array<yona::compiler::typechecker::DeriveRequestProjection, 1>
      WrongSource = {yona::compiler::typechecker::DeriveRequestProjection{
          Box, Traits, OtherRange}};
  const auto SourceResult = Projection.freezeSemanticBatch(
      {Scopes, {}, {}, Nominals, {}, {}, {}, WrongSource, DerivableImports});
  REQUIRE_FALSE(SourceResult.has_value());
  CHECK(SourceResult.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(SourceResult.error().front().Path == "derivations/range");
  CHECK(Types.typeCount() == 0);

  const SourceRange SourceExtent{SourceId{1}, 1, 1, 0, 10};
  const SourceRange OutsideExtent{SourceId{1}, 2, 1, 20, 1};
  yona::ast::IntegerExpr SourceRoot(SourceExtent, 0);
  const std::array<yona::compiler::typechecker::DeriveRequestProjection, 1>
      Outside = {yona::compiler::typechecker::DeriveRequestProjection{
          Box, Traits, OutsideExtent}};
  TypeTable OutsideTypes;
  StructuralTypeProjection OutsideProjection(
      OutsideTypes, ModuleIdentity{"Main"}, Arena.effect_solver(),
      Arena.effect_atoms());
  const auto OutsideResult =
      OutsideProjection.freezeSemanticBatch({Scopes,
                                             {},
                                             {},
                                             Nominals,
                                             {},
                                             {},
                                             {},
                                             Outside,
                                             DerivableImports,
                                             &SourceRoot,
                                             SourceExtent});
  REQUIRE_FALSE(OutsideResult.has_value());
  CHECK(OutsideResult.error().front().Code ==
        StructuralProjectionErrorCode::InvalidInput);
  CHECK(OutsideResult.error().front().Path == "derivations/range");
  CHECK(OutsideTypes.typeCount() == 0);
}

} // namespace
