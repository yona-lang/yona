#include "yona/Model/EffectSolver.h"
#include "yona/Model/InferType.h"
#include "yona/Model/TypeArena.h"
#include "yona/Semantics/Unification.h"
#include "yona/Semantics/UnionFind.h"
#include "yona/Support/Diagnostic.h"

#include <doctest/doctest.h>

#include <expected>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using yona::compiler::DiagnosticEngine;
using yona::compiler::typechecker::EffectAtomId;
using yona::compiler::typechecker::EffectConstraintResult;
using yona::compiler::typechecker::EffectRef;
using yona::compiler::typechecker::EffectRefHasher;
using yona::compiler::typechecker::MayCancelEffectAtom;
using yona::compiler::typechecker::MayRaiseEffectAtom;
using yona::compiler::typechecker::OperationEffectAtom;
using yona::compiler::typechecker::TyCon;
using yona::compiler::typechecker::TypeArena;
using yona::compiler::typechecker::Unifier;
using yona::compiler::typechecker::UnionFind;
using yona::model::EffectOperationKey;
using yona::model::ModuleIdentity;
using yona::model::NominalTypeKey;

EffectOperationKey operation(std::string Name) {
  return EffectOperationKey{NominalTypeKey{ModuleIdentity{"Test"}, "Fx"},
                            std::move(Name)};
}

EffectAtomId commitAtom(TypeArena &Arena, OperationEffectAtom Atom) {
  auto Transaction = Arena.effect_atoms().beginAppend();
  const auto Id = Transaction.intern(std::move(Atom));
  REQUIRE(Transaction.prepareCommit().has_value());
  Transaction.commitPrepared();
  return Id;
}

} // namespace

TEST_SUITE("EffectSolver") {

  TEST_CASE("EffectSolver: atom identities are domain-bound and hashable") {
    TypeArena First;
    TypeArena Second;

    CHECK(First.effect_solver().atomDomain() == First.effect_atoms().domain());
    CHECK(First.effect_solver().atomDomain() !=
          Second.effect_solver().atomDomain());

    std::unordered_map<EffectRef, int, EffectRefHasher> References;
    References.emplace(First.effect_solver().empty(), 7);
    CHECK(References.at(First.effect_solver().empty()) == 7);

    const auto Graph =
        First.effect_solver().freeze(First.effect_solver().flexible());
    CHECK_THROWS_AS(Second.effect_solver().instantiate(Graph),
                    std::invalid_argument);
  }

  TEST_CASE("EffectSolver: catalog interns solved operation structure") {
    TypeArena Arena;
    const auto First = commitAtom(
        Arena, OperationEffectAtom{
                   operation("same"), {Arena.make_con(TyCon::Int)}, {}});
    const auto Second = commitAtom(
        Arena, OperationEffectAtom{
                   operation("same"), {Arena.make_con(TyCon::Int)}, {}});
    CHECK(First == Second);
    CHECK(Arena.effect_atoms().size() == 1);
  }

  TEST_CASE("EffectSolver: masks follow canonical atom representatives") {
    TypeArena Arena;
    auto *Integer = Arena.make_con(TyCon::Int);
    auto *String = Arena.make_con(TyCon::String);
    const auto IntegerOp =
        commitAtom(Arena, OperationEffectAtom{operation("op"), {Integer}, {}});
    const auto StringOp =
        commitAtom(Arena, OperationEffectAtom{operation("op"), {String}, {}});
    REQUIRE(IntegerOp != StringOp);

    auto &Effects = Arena.effect_solver();
    const auto Row = Effects.atoms({IntegerOp, StringOp});
    const auto Masked = Effects.mask(Row, {IntegerOp});
    CHECK(Effects.summarize(Masked).known_atoms ==
          std::vector<EffectAtomId>{StringOp});

    REQUIRE(Effects.equateAtoms(IntegerOp, StringOp) ==
            EffectConstraintResult::Solved);
    CHECK(Effects.canonicalAtom(IntegerOp) == Effects.canonicalAtom(StringOp));
    CHECK(Effects.summarize(Row).known_atoms.size() == 1);
    CHECK(Effects.summarize(Masked).empty());
  }

  TEST_CASE("EffectSolver: solved type arguments unify operation atoms") {
    TypeArena Arena;
    UnionFind Types;
    DiagnosticEngine Diagnostics;
    Unifier Unify(Arena, Types, Diagnostics);
    auto *Parameter = Arena.fresh_var(0);
    Types.add_var(Parameter->var_id, 0);
    auto *Integer = Arena.make_con(TyCon::Int);
    const auto Generic = commitAtom(
        Arena, OperationEffectAtom{operation("typed"), {Parameter}, {}});
    const auto Concrete = commitAtom(
        Arena, OperationEffectAtom{operation("typed"), {Integer}, {}});
    REQUIRE(Generic != Concrete);

    CHECK(Unify.unify(Parameter, Integer, yona::SourceRange::unknown()));
    CHECK(Arena.effect_solver().canonicalAtom(Generic) ==
          Arena.effect_solver().canonicalAtom(Concrete));
  }

  TEST_CASE("EffectSolver: catalog and graph instantiation commit together") {
    TypeArena Arena;
    auto *Parameter = Arena.make_con(TyCon::Int);
    const auto ArgumentEffects = Arena.effect_solver().flexible();
    const auto OriginalAtom = commitAtom(
        Arena, OperationEffectAtom{
                   operation("generic"), {Parameter}, {ArgumentEffects}});
    const auto Root = Arena.effect_solver().mask(
        Arena.effect_solver().join(
            {Arena.effect_solver().atoms({OriginalAtom}), ArgumentEffects}),
        {OriginalAtom});
    const auto Frozen = Arena.effect_solver().freeze({Root, ArgumentEffects});

    auto CatalogTransaction = Arena.effect_atoms().beginAppend();
    auto SolverTransaction = Arena.effect_solver().beginInstantiation(
        Frozen,
        [&](EffectAtomId Source,
            const yona::compiler::typechecker::EffectReferenceMap &References)
            -> std::expected<EffectAtomId, std::string> {
          const auto *Operation = std::get_if<OperationEffectAtom>(
              &Arena.effect_atoms().at(Source));
          if (!Operation)
            return std::unexpected("expected operation atom");
          auto Clone = *Operation;
          Clone.TypeArguments = {Arena.make_con(TyCon::String)};
          for (auto &Effect : Clone.EffectArguments)
            Effect = References.at(Effect);
          return CatalogTransaction.intern(std::move(Clone));
        });
    REQUIRE(SolverTransaction.has_value());
    REQUIRE(CatalogTransaction.prepareCommit().has_value());
    REQUIRE(
        SolverTransaction
            ->prepareCommit(
                [&](EffectAtomId Id) { return CatalogTransaction.valid(Id); },
                [&](EffectAtomId Id) {
                  return CatalogTransaction.validForMask(Id);
                })
            .has_value());
    CatalogTransaction.commitPrepared();
    SolverTransaction->commitPrepared();

    const auto &Instantiation = SolverTransaction->result();
    REQUIRE(Instantiation.roots.size() == 2);
    const auto Summary =
        Arena.effect_solver().summarize(Instantiation.roots.front());
    REQUIRE(Summary.tails.size() == 1);
    REQUIRE(Summary.tails.front().excluded_atoms.size() == 1);
    const auto InstantiatedAtom = Summary.tails.front().excluded_atoms.front();
    CHECK(InstantiatedAtom != OriginalAtom);
    const auto *Operation = std::get_if<OperationEffectAtom>(
        &Arena.effect_atoms().at(InstantiatedAtom));
    REQUIRE(Operation);
    REQUIRE(Operation->TypeArguments.size() == 1);
    CHECK(Operation->TypeArguments.front()->con == TyCon::String);
    REQUIRE(Operation->EffectArguments.size() == 1);
    CHECK(Operation->EffectArguments.front() == Instantiation.roots.back());
  }

  TEST_CASE(
      "EffectSolver: failed coordinated instantiation publishes nothing") {
    TypeArena Arena;
    const auto OriginalAtom =
        commitAtom(Arena, OperationEffectAtom{operation("rollback"), {}, {}});
    const auto Root = Arena.effect_solver().atoms({OriginalAtom});
    const auto Frozen = Arena.effect_solver().freeze(Root);
    const auto CatalogSize = Arena.effect_atoms().size();
    const auto Before = Arena.effect_solver().summarize(Root);

    {
      auto CatalogTransaction = Arena.effect_atoms().beginAppend();
      const auto Staged = CatalogTransaction.intern(
          OperationEffectAtom{operation("staged"), {}, {}});
      CHECK_FALSE(Arena.effect_atoms().valid(Staged));
      auto SolverTransaction = Arena.effect_solver().beginInstantiation(
          Frozen,
          [](EffectAtomId,
             const auto &) -> std::expected<EffectAtomId, std::string> {
            return std::unexpected("injected atom clone failure");
          });
      CHECK_FALSE(SolverTransaction.has_value());
    }

    CHECK(Arena.effect_atoms().size() == CatalogSize);
    CHECK(Arena.effect_solver().summarize(Root) == Before);
  }

  TEST_CASE("EffectSolver: fact atoms cannot appear in mask exclusions") {
    TypeArena Arena;
    const auto Open = Arena.effect_solver().flexible();
    CHECK_THROWS_AS(Arena.effect_solver().mask(Open, {0}),
                    std::invalid_argument);
    auto Transaction = Arena.effect_atoms().beginAppend();
    const auto Raise = Transaction.intern(MayRaiseEffectAtom{});
    const auto Cancel = Transaction.intern(MayCancelEffectAtom{});
    REQUIRE(Transaction.prepareCommit().has_value());
    Transaction.commitPrepared();

    CHECK_THROWS_AS(Arena.effect_solver().mask(Open, {Raise}),
                    std::invalid_argument);
    CHECK_THROWS_AS(Arena.effect_solver().mask(Open, {Cancel}),
                    std::invalid_argument);
  }

  TEST_CASE("EffectSolver: instantiation rejects fact mask exclusions") {
    TypeArena Arena;
    const auto Operation =
        commitAtom(Arena, OperationEffectAtom{operation("masked"), {}, {}});
    const auto Open = Arena.effect_solver().flexible();
    const auto Frozen = Arena.effect_solver().freeze(
        Arena.effect_solver().mask(Open, {Operation}));
    auto CatalogTransaction = Arena.effect_atoms().beginAppend();
    const auto Raise = CatalogTransaction.intern(MayRaiseEffectAtom{});

    auto Transaction = Arena.effect_solver().beginInstantiation(
        Frozen,
        [Raise](EffectAtomId, const auto &)
            -> std::expected<EffectAtomId, std::string> { return Raise; });
    REQUIRE(Transaction.has_value());
    REQUIRE(CatalogTransaction.prepareCommit().has_value());
    CHECK_FALSE(Transaction
                    ->prepareCommit(
                        [&](EffectAtomId Atom) {
                          return CatalogTransaction.valid(Atom);
                        },
                        [&](EffectAtomId Atom) {
                          return CatalogTransaction.validForMask(Atom);
                        })
                    .has_value());
    CHECK_FALSE(Arena.effect_atoms().valid(Raise));
  }

  TEST_CASE("TypeArena: sums collapse arrows with equated effects") {
    TypeArena Arena;
    const auto LeftEffect = Arena.effect_solver().flexible();
    const auto RightEffect = Arena.effect_solver().flexible();
    REQUIRE(Arena.effect_solver().equate(LeftEffect, RightEffect) ==
            EffectConstraintResult::Solved);
    auto *Integer = Arena.make_con(TyCon::Int);
    auto *Left = Arena.make_arrow(Integer, Integer, LeftEffect);
    auto *Right = Arena.make_arrow(Integer, Integer, RightEffect);

    CHECK(Arena.make_sum({Left, Right}) == Left);
  }
}
