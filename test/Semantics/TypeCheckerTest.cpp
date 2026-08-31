/// Unit tests for the type checker:
/// TypeArena, UnionFind, Unifier, TypeEnv, builtins.

#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Model/EffectSolver.h"
#include "yona/Model/InferType.h"
#include "yona/Model/TypeArena.h"
#include "yona/Model/TypeEnv.h"
#include "yona/Semantics/ModuleFunctionDependencies.h"
#include "yona/Semantics/RefinementChecker.h"
#include "yona/Semantics/Unification.h"
#include "yona/Semantics/UnionFind.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <chrono>
#include <stdexcept>
#include <type_traits>

using yona::compiler::DiagnosticEngine;
using yona::compiler::DiagLevel;
using yona::compiler::ErrorCode;
using yona::compiler::WarningFlag;
using yona::compiler::parse_error_code;
using yona::SourceRange;
using yona::compiler::typechecker::EffectConstraintResult;
using yona::compiler::typechecker::EffectNormalForm;
using yona::compiler::typechecker::EffectRef;
using yona::compiler::typechecker::EffectSolver;
using yona::compiler::typechecker::MonoType;
using yona::compiler::typechecker::MonoTypePtr;
using yona::compiler::typechecker::TyCon;
using yona::compiler::typechecker::TypeArena;
using yona::compiler::typechecker::TypeEnv;
using yona::compiler::typechecker::TypeScheme;
using yona::compiler::typechecker::UnionFind;
using yona::compiler::typechecker::Unifier;
using yona::compiler::typechecker::TypeChecker;
using yona::compiler::typechecker::FactEnv;
using yona::compiler::typechecker::Interval;
using yona::compiler::typechecker::module_function_components;
using yona::compiler::typechecker::RefinementChecker;
using yona::compiler::types::BuiltinType;
using yona::compiler::types::RefinePredicate;
using std::string;

TEST_SUITE("TypeChecker") {

  TEST_CASE(
      "TypeChecker: recursive nominal function fields retain type arguments") {
    yona::parser::Parser parser;
    auto parsed = parser.parseModule(R"(
module Test\Stream

export singleton
export type Stream

type Stream a = Yield a (() -> Stream a) | Nil
singleton value = Yield value (\_ -> Nil)
)",
                                     "Stream.yona");
    REQUIRE(parsed.has_value());

    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    checker.check_module(parsed.value().get());
    CHECK_FALSE(checker.has_errors());
  }

  TEST_CASE("EffectSolver: joins are associative commutative and idempotent") {
    EffectSolver solver;
    const auto state = solver.labels({"State.get", "State.get"});
    const auto log = solver.labels({"Log.log"});

    const auto left = solver.join({state, solver.join({log, state})});
    const auto right = solver.join({log, state});

    CHECK(left == right);
    const auto summary = solver.summarize(left);
    CHECK(summary.known_labels ==
          std::vector<std::string>{"Log.log", "State.get"});
    CHECK_FALSE(summary.is_open());
  }

  TEST_CASE("EffectSolver: flexible equality binds an exact row") {
    EffectSolver solver;
    const auto flexible = solver.flexible();
    const auto state = solver.labels({"State.get"});

    CHECK(solver.summarize(flexible).is_open());
    CHECK(solver.equate(flexible, state) == EffectConstraintResult::Solved);
    CHECK(solver.summarize(flexible).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK_FALSE(solver.summarize(flexible).is_open());
    CHECK(solver.equate(flexible, solver.labels({"Log.log"})) ==
          EffectConstraintResult::Conflict);
  }

  TEST_CASE("EffectSolver: independent callback rows join without equality") {
    EffectSolver solver;
    const auto first = solver.flexible();
    const auto second = solver.flexible();
    const auto body = solver.derived();
    solver.include(first, body);
    solver.include(second, body);

    REQUIRE(solver.variable_id(first).has_value());
    REQUIRE(solver.variable_id(second).has_value());
    CHECK(solver.variable_id(first) != solver.variable_id(second));
    CHECK(solver.equate(first, solver.labels({"State.get"})) ==
          EffectConstraintResult::Solved);
    CHECK(solver.equate(second, solver.labels({"Log.log"})) ==
          EffectConstraintResult::Solved);

    const auto summary = solver.summarize(body);
    CHECK(summary.known_labels ==
          std::vector<std::string>{"Log.log", "State.get"});
    CHECK_FALSE(summary.is_open());
  }

  TEST_CASE("EffectSolver: derived cycles use the least fixed point") {
    EffectSolver solver;
    const auto left = solver.derived();
    const auto right = solver.derived();
    solver.include(right, left);
    solver.include(left, right);

    CHECK(solver.summarize(left).empty());
    CHECK(solver.summarize(right).empty());

    solver.add_label(right, "State.get");
    CHECK(solver.summarize(left).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK(solver.summarize(right).known_labels ==
          std::vector<std::string>{"State.get"});
  }

  TEST_CASE("EffectSolver: opaque leaves cannot be rebound or closed") {
    EffectSolver solver;
    const auto opaque = solver.opaque();

    const auto before = solver.summarize(opaque);
    REQUIRE(before.tails.size() == 1);
    CHECK(before.tails[0].opaque);
    CHECK(before.is_open());
    CHECK(solver.equate(opaque, solver.empty()) ==
          EffectConstraintResult::Deferred);
    CHECK(solver.summarize(opaque) == before);
    CHECK(solver.summarize(solver.empty()).empty());
  }

  TEST_CASE("EffectSolver: masks apply to effects learned after construction") {
    EffectSolver solver;
    const auto callback = solver.flexible();
    const auto source =
        solver.join({solver.labels({"State.get", "Log.log"}), callback});
    const auto masked = solver.mask(source, {"State.get"});

    const auto open_summary = solver.summarize(masked);
    CHECK(open_summary.known_labels == std::vector<std::string>{"Log.log"});
    REQUIRE(open_summary.tails.size() == 1);
    CHECK(open_summary.tails[0].excluded_labels ==
          std::vector<std::string>{"State.get"});

    CHECK(solver.equate(callback, solver.labels({"Net.post", "State.get"})) ==
          EffectConstraintResult::Solved);
    const auto closed_summary = solver.summarize(masked);
    CHECK(closed_summary.known_labels ==
          std::vector<std::string>{"Log.log", "Net.post"});
    CHECK_FALSE(closed_summary.is_open());
  }

  TEST_CASE(
      "EffectSolver: graph templates clone independent variables and cells") {
    EffectSolver solver;
    const auto first = solver.flexible();
    const auto second = solver.flexible();
    const auto body = solver.derived();
    solver.include(first, body);
    solver.include(second, body);

    const auto first_id = solver.variable_id(first).value();
    const auto second_id = solver.variable_id(second).value();
    const auto graph = solver.freeze(body);
    const auto one = solver.instantiate(graph);
    const auto two = solver.instantiate(graph);

    REQUIRE(one.variables.contains(first_id));
    REQUIRE(one.variables.contains(second_id));
    REQUIRE(two.variables.contains(first_id));
    REQUIRE(two.variables.contains(second_id));
    CHECK(one.variables.at(first_id) != two.variables.at(first_id));
    CHECK(one.variables.at(second_id) != two.variables.at(second_id));

    CHECK(solver.equate(one.variables.at(first_id),
                        solver.labels({"State.get"})) ==
          EffectConstraintResult::Solved);
    CHECK(solver.equate(one.variables.at(second_id),
                        solver.labels({"Log.log"})) ==
          EffectConstraintResult::Solved);
    CHECK(solver.summarize(one.root).known_labels ==
          std::vector<std::string>{"Log.log", "State.get"});

    solver.add_label(one.root, "Net.post");
    CHECK(solver.summarize(one.root).known_labels ==
          std::vector<std::string>{"Log.log", "Net.post", "State.get"});

    const auto untouched = solver.summarize(two.root);
    CHECK(untouched.known_labels.empty());
    CHECK(untouched.tails.size() == 2);
    CHECK(untouched.is_open());
  }

  TEST_CASE("EffectSolver: equality stays live as derived cells grow") {
    EffectSolver solver;
    const auto left = solver.derived();
    const auto right = solver.derived();

    CHECK(solver.equate(left, right) == EffectConstraintResult::Deferred);
    solver.add_label(left, "State.get");

    CHECK(solver.summarize(left).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK(solver.summarize(right).known_labels ==
          std::vector<std::string>{"State.get"});
  }

  TEST_CASE("EffectSolver: equality propagates opaque rows through templates") {
    EffectSolver solver;
    const auto derived = solver.derived();
    const auto opaque = solver.opaque();

    CHECK(solver.equate(derived, opaque) == EffectConstraintResult::Deferred);
    const auto original = solver.summarize(derived);
    REQUIRE(original.tails.size() == 1);
    CHECK(original.tails[0].opaque);
    CHECK(original.is_open());

    const auto graph = solver.freeze(derived);
    const auto clone = solver.instantiate(graph);
    const auto instantiated = solver.summarize(clone.root);
    REQUIRE(instantiated.tails.size() == 1);
    CHECK(instantiated.tails[0].opaque);
    CHECK(instantiated.is_open());
  }

  TEST_CASE("EffectSolver: deferred recursive equality rejects a contradictory "
            "binding") {
    EffectSolver solver;
    const auto row = solver.flexible();
    const auto row_id = solver.variable_id(row).value();
    const auto recursive = solver.join({row, solver.labels({"State.get"})});

    CHECK(solver.equate(row, recursive) == EffectConstraintResult::Deferred);
    CHECK(solver.equate(row, solver.labels({"Log.log"})) ==
          EffectConstraintResult::Conflict);

    const auto summary = solver.summarize(row);
    CHECK(summary.known_labels == std::vector<std::string>{"State.get"});
    CHECK(summary.is_open());

    const auto graph = solver.freeze(row);
    const auto clone = solver.instantiate(graph);
    REQUIRE(clone.variables.contains(row_id));
    CHECK(
        solver.equate(clone.variables.at(row_id), solver.labels({"Log.log"})) ==
        EffectConstraintResult::Conflict);
    CHECK(solver.summarize(clone.root).is_open());
  }

  TEST_CASE("EffectSolver: derived equality rejects incompatible callback "
            "binding in either order") {
    SUBCASE("constraint before callback binding") {
      EffectSolver solver;
      const auto callback = solver.flexible();
      const auto body = solver.derived();
      solver.include(callback, body);

      CHECK(solver.equate(body, solver.labels({"State.get"})) ==
            EffectConstraintResult::Deferred);
      CHECK(solver.equate(callback, solver.labels({"Log.log"})) ==
            EffectConstraintResult::Conflict);
      CHECK(solver.summarize(body).known_labels ==
            std::vector<std::string>{"State.get"});
      CHECK(solver.summarize(body).is_open());
    }

    SUBCASE("callback binding before constraint") {
      EffectSolver solver;
      const auto callback = solver.flexible();
      const auto body = solver.derived();
      solver.include(callback, body);

      CHECK(solver.equate(callback, solver.labels({"Log.log"})) ==
            EffectConstraintResult::Solved);
      CHECK(solver.equate(body, solver.labels({"State.get"})) ==
            EffectConstraintResult::Conflict);
    }
  }

  TEST_CASE(
      "EffectSolver: derived equality constraints remain live after cloning") {
    EffectSolver solver;
    const auto callback = solver.flexible();
    const auto callback_id = solver.variable_id(callback).value();
    const auto body = solver.derived();
    solver.include(callback, body);

    CHECK(solver.equate(body, solver.labels({"State.get"})) ==
          EffectConstraintResult::Deferred);
    const auto graph = solver.freeze(body);
    const auto clone = solver.instantiate(graph);

    REQUIRE(clone.variables.contains(callback_id));
    CHECK(solver.equate(clone.variables.at(callback_id),
                        solver.labels({"Log.log"})) ==
          EffectConstraintResult::Conflict);
    CHECK(solver.summarize(clone.root).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK(solver.summarize(clone.root).is_open());
  }

  TEST_CASE("EffectSolver: derived equality permits compatible direct and "
            "nested growth") {
    SUBCASE("direct derived cell absorbs a closed equality") {
      EffectSolver solver;
      const auto body = solver.derived();
      const auto state = solver.labels({"State.get"});

      CHECK(solver.equate(body, state) != EffectConstraintResult::Conflict);
      CHECK(solver.summarize(body) == solver.summarize(state));
    }

    SUBCASE("derived cell nested under a join can grow to equality") {
      EffectSolver solver;
      const auto body = solver.derived();
      const auto expression = solver.join({body, solver.labels({"Log.log"})});
      const auto expected = solver.labels({"Log.log", "State.get"});

      CHECK(solver.equate(expression, expected) ==
            EffectConstraintResult::Deferred);
      solver.add_label(body, "State.get");
      CHECK(solver.summarize(expression) == solver.summarize(expected));
    }
  }

  TEST_CASE(
      "EffectSolver: derived mutations reject conflicts transactionally") {
    SUBCASE("local label") {
      EffectSolver solver;
      const auto body = solver.derived();
      const auto state = solver.labels({"State.get"});
      REQUIRE(solver.equate(body, state) != EffectConstraintResult::Conflict);

      CHECK(solver.add_label(body, "Log.log") ==
            EffectConstraintResult::Conflict);
      CHECK(solver.summarize(body) == solver.summarize(state));
    }

    SUBCASE("inclusion edge") {
      EffectSolver solver;
      const auto body = solver.derived();
      const auto state = solver.labels({"State.get"});
      REQUIRE(solver.equate(body, state) != EffectConstraintResult::Conflict);

      CHECK(solver.include(solver.labels({"Log.log"}), body) ==
            EffectConstraintResult::Conflict);
      CHECK(solver.summarize(body) == solver.summarize(state));
    }
  }

  TEST_CASE(
      "EffectSolver: derived equality constraints are validated jointly") {
    EffectSolver solver;
    const auto body = solver.derived();
    const auto state = solver.labels({"State.get"});

    REQUIRE(solver.equate(body, state) == EffectConstraintResult::Deferred);
    const auto expression = solver.join({body, solver.labels({"Net.post"})});
    const auto incompatible =
        solver.labels({"Log.log", "Net.post", "State.get"});
    CHECK(solver.equate(expression, incompatible) ==
          EffectConstraintResult::Conflict);
    CHECK(solver.summarize(expression).known_labels ==
          std::vector<std::string>{"Net.post", "State.get"});

    const auto graph = solver.freeze(body);
    const auto clone = solver.instantiate(graph);
    const auto cloned_expression =
        solver.join({clone.root, solver.labels({"Net.post"})});
    CHECK(solver.equate(cloned_expression, incompatible) ==
          EffectConstraintResult::Conflict);
  }

  TEST_CASE(
      "EffectSolver: nested equality saturates derived facts and templates") {
    EffectSolver solver;
    const auto body = solver.derived();
    const auto expression = solver.join({body, solver.labels({"Log.log"})});
    const auto expected = solver.labels({"Log.log", "State.get"});

    REQUIRE(solver.equate(expression, expected) ==
            EffectConstraintResult::Deferred);
    CHECK(solver.summarize(body).known_labels ==
          std::vector<std::string>{"State.get"});

    const auto graph = solver.freeze(body);
    const auto clone = solver.instantiate(graph);
    CHECK(solver.summarize(clone.root).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK_FALSE(solver.summarize(clone.root).is_open());
  }

  TEST_CASE("EffectSolver: ambiguous derived equality has no least solution") {
    EffectSolver solver;
    const auto first = solver.derived();
    const auto second = solver.derived();
    const auto either = solver.join({first, second});
    const auto state = solver.labels({"State.get"});

    CHECK(solver.equate(either, state) == EffectConstraintResult::Conflict);
    CHECK(solver.summarize(first).empty());
    CHECK(solver.summarize(second).empty());

    REQUIRE(solver.add_label(first, "State.get") !=
            EffectConstraintResult::Conflict);
    CHECK(solver.equate(either, state) != EffectConstraintResult::Conflict);
  }

  TEST_CASE("EffectSolver: symbolic rows carry deferred derived choices "
            "through templates") {
    EffectSolver solver;
    const auto body = solver.derived();
    const auto callback = solver.flexible();
    const auto callback_id = solver.variable_id(callback).value();
    const auto expression = solver.join({body, callback});

    REQUIRE(solver.equate(expression, solver.labels({"State.get"})) ==
            EffectConstraintResult::Deferred);
    CHECK(solver.summarize(expression).is_open());

    const auto graph = solver.freeze(expression);
    EffectSolver clone_solver;
    const auto clone = clone_solver.instantiate(graph);
    REQUIRE(clone.variables.contains(callback_id));
    CHECK(clone_solver.summarize(clone.root).is_open());
    CHECK(clone_solver.equate(clone.variables.at(callback_id),
                              clone_solver.empty()) ==
          EffectConstraintResult::Deferred);
    CHECK(clone_solver.summarize(clone.root).known_labels ==
          std::vector<std::string>{"State.get"});
    CHECK_FALSE(clone_solver.summarize(clone.root).is_open());
  }

  TEST_CASE("EffectSolver: opaque rows carry deferred derived choices") {
    EffectSolver solver;
    const auto body = solver.derived();
    const auto imported = solver.opaque();
    const auto expression = solver.join({body, imported});

    REQUIRE(solver.equate(expression, solver.labels({"State.get"})) ==
            EffectConstraintResult::Deferred);
    const auto summary = solver.summarize(expression);
    REQUIRE(summary.tails.size() == 1);
    CHECK(summary.tails.front().opaque);

    const auto graph = solver.freeze(expression);
    EffectSolver clone_solver;
    const auto clone = clone_solver.instantiate(graph);
    const auto cloned_summary = clone_solver.summarize(clone.root);
    REQUIRE(cloned_summary.tails.size() == 1);
    CHECK(cloned_summary.tails.front().opaque);
  }

  TEST_CASE("EffectSolver: opaque equality constraints are validated jointly") {
    EffectSolver solver;
    const auto opaque = solver.opaque();
    const auto opaque_id = solver.variable_id(opaque).value();

    REQUIRE(solver.equate(opaque, solver.labels({"State.get"})) ==
            EffectConstraintResult::Deferred);
    CHECK(solver.equate(opaque, solver.labels({"Log.log"})) ==
          EffectConstraintResult::Conflict);

    const auto graph = solver.freeze(opaque);
    const auto clone = solver.instantiate(graph);
    REQUIRE(clone.variables.contains(opaque_id));
    CHECK(solver.equate(clone.root, solver.labels({"Log.log"})) ==
          EffectConstraintResult::Conflict);
    CHECK(solver.summarize(clone.root).is_open());
  }

  TEST_CASE("EffectSolver: foreign handles are rejected") {
    EffectSolver first_solver;
    EffectSolver second_solver;
    const auto local = first_solver.flexible();
    const auto foreign = second_solver.labels({"Log.log"});

    CHECK_THROWS_AS(first_solver.join({local, foreign}),
                    const std::invalid_argument &);
  }

  TEST_CASE("TypeArena: fresh variables get unique IDs") {
    TypeArena arena;
    auto *v1 = arena.fresh_var(0);
    auto *v2 = arena.fresh_var(0);
    CHECK(v1->tag == MonoType::Var);
    CHECK(v2->tag == MonoType::Var);
    CHECK(v1->var_id != v2->var_id);
  }

  TEST_CASE("TypeArena: make_con returns correct types") {
    TypeArena arena;
    auto *int_t = arena.make_con(TyCon::Int);
    auto *str_t = arena.make_con(TyCon::String);
    CHECK(int_t->tag == MonoType::Con);
    CHECK(int_t->con == TyCon::Int);
    CHECK(str_t->con == TyCon::String);
  }

  TEST_CASE("TypeArena: make_arrow constructs function types") {
    TypeArena arena;
    auto *int_t = arena.make_con(TyCon::Int);
    auto *str_t = arena.make_con(TyCon::String);
    auto *fn = arena.make_arrow(int_t, str_t);
    CHECK(fn->tag == MonoType::Arrow);
    CHECK(fn->param_type == int_t);
    CHECK(fn->return_type == str_t);
  }

  TEST_CASE("TypeArena: effect owner address remains stable") {
    CHECK_FALSE(std::is_move_constructible_v<TypeArena>);
    CHECK_FALSE(std::is_move_assignable_v<TypeArena>);
  }

  TEST_CASE("UnionFind: unbound variable returns nullptr") {
    UnionFind uf;
    uf.add_var(0, 0);
    CHECK(uf.find(0) == nullptr);
    CHECK(!uf.is_bound(0));
  }

  TEST_CASE("UnionFind: bind and find") {
    TypeArena arena;
    UnionFind uf;
    uf.add_var(0, 0);
    auto *int_t = arena.make_con(TyCon::Int);
    uf.bind(0, int_t);
    CHECK(uf.find(0) == int_t);
    CHECK(uf.is_bound(0));
  }

  TEST_CASE("UnionFind: path compression") {
    TypeArena arena;
    UnionFind uf;
    auto *v0 = arena.fresh_var(0);
    uf.add_var(v0->var_id, 0);
    auto *v1 = arena.fresh_var(0);
    uf.add_var(v1->var_id, 0);
    auto *int_t = arena.make_con(TyCon::Int);
    // v0 -> v1 -> Int
    uf.bind(v0->var_id, v1);
    uf.bind(v1->var_id, int_t);
    CHECK(uf.find(v0->var_id) == int_t); // path compressed
  }

  TEST_CASE("Unifier: identical types unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    CHECK(u.unify(int_t, int_t, SourceRange::unknown()));
  }

  TEST_CASE("Unifier: different concrete types fail") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    auto *str_t = arena.make_con(TyCon::String);
    CHECK(!u.unify(int_t, str_t, SourceRange::unknown()));
  }

  TEST_CASE("Unifier: variable binds to concrete type") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *v = arena.fresh_var(0);
    uf.add_var(v->var_id, 0);
    auto *int_t = arena.make_con(TyCon::Int);
    CHECK(u.unify(v, int_t, SourceRange::unknown()));
    CHECK(uf.find(v->var_id) == int_t);
  }

  TEST_CASE("Unifier: two variables unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *v1 = arena.fresh_var(0);
    uf.add_var(v1->var_id, 0);
    auto *v2 = arena.fresh_var(0);
    uf.add_var(v2->var_id, 0);
    CHECK(u.unify(v1, v2, SourceRange::unknown()));
    // After binding v2 to something, v1 should resolve too
    auto *int_t = arena.make_con(TyCon::Int);
    uf.bind(v2->var_id, int_t);
    CHECK(u.resolve(v1) == int_t);
  }

  TEST_CASE("Unifier: arrow types unify structurally") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    auto *a = arena.fresh_var(0);
    uf.add_var(a->var_id, 0);
    auto *fn1 = arena.make_arrow(int_t, a);
    auto *fn2 = arena.make_arrow(int_t, arena.make_con(TyCon::String));
    CHECK(u.unify(fn1, fn2, SourceRange::unknown()));
    CHECK(u.resolve(a)->con == TyCon::String);
  }

  TEST_CASE(
      "Unifier: arrow effect equality is delegated to the effect solver") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    const auto open_effect = arena.effect_solver().flexible();
    const auto state_effect = arena.effect_solver().labels({"State.get"});
    auto *open = arena.make_arrow(int_t, int_t, open_effect);
    auto *state = arena.make_arrow(int_t, int_t, state_effect);

    REQUIRE(u.unify(open, state, SourceRange::unknown()));
    CHECK(arena.effect_solver().summarize(open->arrow_effect) ==
          EffectNormalForm{{"State.get"}, {}});
    CHECK(arena.effect_solver().summarize(state->arrow_effect) ==
          EffectNormalForm{{"State.get"}, {}});
  }

  TEST_CASE("TypeArena: arrow effects must belong to its solver") {
    TypeArena first;
    TypeArena second;
    auto *integer = first.make_con(TyCon::Int);
    const auto foreign = second.effect_solver().labels({"State.get"});

    CHECK_THROWS_AS(first.make_arrow(integer, integer, foreign),
                    std::invalid_argument);
  }

  TEST_CASE("Unifier: incompatible closed arrow effects fail") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    auto *state = arena.make_arrow(int_t, int_t,
                                   arena.effect_solver().labels({"State.get"}));
    auto *log = arena.make_arrow(int_t, int_t,
                                 arena.effect_solver().labels({"Log.log"}));

    CHECK_FALSE(u.unify(state, log, SourceRange::unknown()));
    CHECK(diag.has_errors());
  }

  TEST_CASE("Unifier: closed effect sets with the same ops unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    auto *fn1 = arena.make_arrow(int_t, int_t,
                                 arena.effect_solver().labels({"Fs.read"}));
    auto *fn2 = arena.make_arrow(int_t, int_t,
                                 arena.effect_solver().labels({"Fs.read"}));
    CHECK(u.unify(fn1, fn2, SourceRange::unknown()));
  }

  TEST_CASE("Unifier: closed effect sets with different ops fail") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    auto *fn1 = arena.make_arrow(int_t, int_t,
                                 arena.effect_solver().labels({"Fs.read"}));
    auto *fn2 = arena.make_arrow(int_t, int_t,
                                 arena.effect_solver().labels({"Net.post"}));
    CHECK(!u.unify(fn1, fn2, SourceRange::unknown()));
  }

  TEST_CASE("Unifier: flexible effect absorbs a closed set") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *int_t = arena.make_con(TyCon::Int);
    const auto flexible = arena.effect_solver().flexible();
    auto *open = arena.make_arrow(int_t, int_t, flexible);
    auto *closed = arena.make_arrow(int_t, int_t,
                                    arena.effect_solver().labels({"Fs.read"}));
    REQUIRE(u.unify(open, closed, SourceRange::unknown()));
    CHECK(arena.effect_solver().summarize(flexible) ==
          EffectNormalForm{{"Fs.read"}, {}});
  }

  TEST_CASE("Unifier: occurs check prevents infinite types") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *a = arena.fresh_var(0);
    uf.add_var(a->var_id, 0);
    auto *seq_a = arena.make_app("Seq", {a});
    CHECK(!u.unify(a, seq_a, SourceRange::unknown())); // a ~ Seq a -> infinite
  }

  TEST_CASE("Unifier: App types unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *a = arena.fresh_var(0);
    uf.add_var(a->var_id, 0);
    auto *opt_int = arena.make_app("Option", {arena.make_con(TyCon::Int)});
    auto *opt_a = arena.make_app("Option", {a});
    CHECK(u.unify(opt_int, opt_a, SourceRange::unknown()));
    CHECK(u.resolve(a)->con == TyCon::Int);
  }

  TEST_CASE("Unifier: imported ADT wildcard unifies with named ADTs, not Seq") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *elem = arena.fresh_var(0);
    uf.add_var(elem->var_id, 0);
    auto *adt = arena.make_app("ADT", {elem});
    auto *stream = arena.make_app("Stream", {arena.make_con(TyCon::String)});
    auto *seq = arena.make_app("Seq", {arena.make_con(TyCon::String)});
    CHECK(u.unify(adt, stream, SourceRange::unknown()));
    CHECK(!u.unify(adt, seq, SourceRange::unknown()));
  }

  TEST_CASE("Unifier: tuple types unify element-wise") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *a = arena.fresh_var(0);
    uf.add_var(a->var_id, 0);
    auto *t1 = arena.make_tuple({arena.make_con(TyCon::Int), a});
    auto *t2 = arena.make_tuple(
        {arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(u.unify(t1, t2, SourceRange::unknown()));
    CHECK(u.resolve(a)->con == TyCon::String);
  }

  TEST_CASE("Unifier: tuple size mismatch fails") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto *t1 = arena.make_tuple({arena.make_con(TyCon::Int)});
    auto *t2 = arena.make_tuple(
        {arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(!u.unify(t1, t2, SourceRange::unknown()));
  }

  TEST_CASE("pretty_print formats types correctly") {
    TypeArena arena;
    CHECK(pretty_print(arena.make_con(TyCon::Int)) == "Int");
    CHECK(pretty_print(arena.make_con(TyCon::String)) == "String");
    auto *fn = arena.make_arrow(arena.make_con(TyCon::Int),
                                arena.make_con(TyCon::Bool));
    CHECK(pretty_print(fn) == "(Int -> Bool)");
    auto *eff = arena.make_arrow(arena.make_con(TyCon::Int),
                                 arena.make_con(TyCon::Bool),
                                 arena.effect_solver().labels({"Fs.read"}));
    CHECK(pretty_print(eff) == "(Int -> !{Fs.read} Bool)");
    auto *opt = arena.make_app("Option", {arena.make_con(TyCon::Int)});
    CHECK(pretty_print(opt) == "Option Int");
    auto *tup = arena.make_tuple(
        {arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(pretty_print(tup) == "(Int, String)");
  }

  TEST_CASE("pretty_print normalizes a multi-source effect row") {
    TypeArena arena;
    auto &effects = arena.effect_solver();
    const auto row = effects.join(
        {effects.flexible(), effects.labels({"Log.log", "Fs.read"}),
         effects.flexible(), effects.labels({"Fs.read"})});
    auto *fn = arena.make_arrow(arena.make_con(TyCon::Int),
                                arena.make_con(TyCon::Bool), row);

    CHECK(pretty_print(fn) == "(Int -> !{Fs.read,Log.log | e0 + e1} Bool)");
  }

  // ===== TypeEnv Tests =====

  TEST_CASE("TypeEnv: bind and lookup") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    auto *int_t = arena.make_con(TyCon::Int);
    env->bind("x", int_t);
    auto result = env->lookup("x");
    REQUIRE(result.has_value());
    CHECK(result->body->tag == MonoType::Con);
    CHECK(result->body->con == TyCon::Int);
  }

  TEST_CASE("TypeEnv: lookup not found returns nullopt") {
    auto env = std::make_shared<TypeEnv>();
    CHECK(!env->lookup("nonexistent").has_value());
  }

  TEST_CASE("TypeChecker: ADT registration requires canonical field shapes") {
    DiagnosticEngine Diagnostics;
    TypeChecker Checker(Diagnostics);

    CHECK_THROWS_AS(
        Checker.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}}, {}),
        std::invalid_argument);
  }

  TEST_CASE("TypeEnv: child scope inherits parent bindings") {
    TypeArena arena;
    auto parent = std::make_shared<TypeEnv>();
    parent->bind("x", arena.make_con(TyCon::Int));
    auto child = parent->child();
    auto result = child->lookup("x");
    REQUIRE(result.has_value());
    CHECK(result->body->con == TyCon::Int);
  }

  TEST_CASE("TypeEnv: child scope shadows parent") {
    TypeArena arena;
    auto parent = std::make_shared<TypeEnv>();
    parent->bind("x", arena.make_con(TyCon::Int));
    auto child = parent->child();
    child->bind("x", arena.make_con(TyCon::String));
    // Child sees String
    CHECK(child->lookup("x")->body->con == TyCon::String);
    // Parent still sees Int
    CHECK(parent->lookup("x")->body->con == TyCon::Int);
  }

  TEST_CASE("TypeEnv: polymorphic scheme binding") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    // forall a. a -> a
    auto *a = arena.fresh_var(1);
    auto *id_type = arena.make_arrow(a, a);
    env->bind_scheme("id", TypeScheme({a->var_id}, id_type));
    auto result = env->lookup("id");
    REQUIRE(result.has_value());
    CHECK(result->quantified_vars.size() == 1);
    CHECK(result->body->tag == MonoType::Arrow);
  }

  TEST_CASE("register_builtins: arithmetic operators have correct types") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    // + : Int -> Int -> Int
    auto plus = env->lookup("+");
    REQUIRE(plus.has_value());
    CHECK(plus->body->tag == MonoType::Arrow);
    CHECK(plus->body->param_type->con == TyCon::Int);
    CHECK(plus->body->return_type->tag == MonoType::Arrow);
    CHECK(plus->body->return_type->return_type->con == TyCon::Int);
  }

  TEST_CASE("register_builtins: comparison operators return Bool") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    auto eq = env->lookup("==");
    REQUIRE(eq.has_value());
    CHECK(eq->body->return_type->return_type->con == TyCon::Bool);
  }

  TEST_CASE("register_builtins: cons is polymorphic") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    auto cons = env->lookup("::");
    REQUIRE(cons.has_value());
    CHECK(cons->quantified_vars.size() == 1);
    // :: : a -> Seq a -> Seq a
    CHECK(cons->body->tag == MonoType::Arrow);
  }

  TEST_CASE("register_builtins: pipe is polymorphic") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    auto pipe = env->lookup("|>");
    REQUIRE(pipe.has_value());
    CHECK(pipe->quantified_vars.size() == 2);
  }

  TEST_CASE("register_builtins: string concat") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    auto concat = env->lookup("++");
    REQUIRE(concat.has_value());
    CHECK(concat->body->param_type->con == TyCon::String);
  }

  TEST_CASE("register_builtins: type names available") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    register_builtins(*env, arena);

    CHECK(env->lookup("Int").has_value());
    CHECK(env->lookup("Float").has_value());
    CHECK(env->lookup("Bool").has_value());
    CHECK(env->lookup("String").has_value());
    CHECK(env->lookup("true").has_value());
    CHECK(env->lookup("false").has_value());
  }

  // ===== Core Inference Tests =====
  // These tests parse Yona code and run the type checker.

} // close TypeChecker suite temporarily

#include "yona/Semantics/TypeChecker.h"
#include "yona/Syntax/Parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

static std::string check_expr_str(const std::string &source) {
  yona::parser::Parser parser;
  std::istringstream stream(source);
  auto result = parser.parseExpression(stream.str(), "<stream>");
  if (!result || !result->Expression)
    return "PARSE_ERROR";

  yona::compiler::DiagnosticEngine diag;
  yona::compiler::typechecker::TypeChecker checker(diag);
  auto *t = checker.check(result->Expression.get());
  if (!t)
    return "?";
  return yona::compiler::typechecker::pretty_print(checker.zonk(t));
}

class ScopedInterfaceTestDirectory {
public:
  explicit ScopedInterfaceTestDirectory(std::string_view name)
      : path_(std::filesystem::temp_directory_path() /
              ("yona_" + std::string(name) + "_" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    std::filesystem::create_directories(path_ / "Test");
  }

  ~ScopedInterfaceTestDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

TEST_SUITE("TypeChecker") { // reopen suite

  TEST_CASE("Inference: integer literal") {
    CHECK(check_expr_str("42") == "Int");
  }

  TEST_CASE("Inference: float literal") {
    CHECK(check_expr_str("3.14") == "Float");
  }

  TEST_CASE("Inference: string literal") {
    CHECK(check_expr_str("\"hello\"") == "String");
  }

  TEST_CASE("Inference: boolean literals") {
    CHECK(check_expr_str("true") == "Bool");
    CHECK(check_expr_str("false") == "Bool");
  }

  TEST_CASE("Inference: arithmetic produces Int") {
    CHECK(check_expr_str("1 + 2") == "Int");
    CHECK(check_expr_str("10 - 3") == "Int");
    CHECK(check_expr_str("4 * 5") == "Int");
  }

  TEST_CASE("Inference: comparison produces Bool") {
    CHECK(check_expr_str("1 < 2") == "Bool");
    CHECK(check_expr_str("3 == 4") == "Bool");
  }

  TEST_CASE("Inference: if expression unifies branches") {
    CHECK(check_expr_str("if true then 1 else 2") == "Int");
    CHECK(check_expr_str("if true then \"a\" else \"b\"") == "String");
  }

  TEST_CASE("Inference: let binding") {
    CHECK(check_expr_str("let x = 42 in x") == "Int");
    CHECK(check_expr_str("let x = 1 in x + 2") == "Int");
  }

  TEST_CASE("Inference: let with function") {
    CHECK(check_expr_str("let f x = x + 1 in f 5") == "Int");
  }

  TEST_CASE("Inference: tuple") {
    CHECK(check_expr_str("(1, \"hello\")") == "(Int, String)");
    CHECK(check_expr_str("(1, 2, 3)") == "(Int, Int, Int)");
  }

  TEST_CASE("Inference: sequence") {
    CHECK(check_expr_str("[1, 2, 3]") == "Seq Int");
  }

  TEST_CASE("Inference: string concat") {
    CHECK(check_expr_str("\"a\" ++ \"b\"") == "String");
  }

  TEST_CASE("Inference: sequence join preserves its element type") {
    CHECK(check_expr_str("[1, 2] ++ [3]") == "Seq Int");
    CHECK(check_expr_str("[\"left\"] ++ [\"right\"]") == "Seq String");
  }

  TEST_CASE("Inference: nested let") {
    CHECK(check_expr_str("let x = 1 in let y = x + 1 in y") == "Int");
  }

  TEST_CASE("Inference: lambda") {
    CHECK(check_expr_str("let f = \\x -> x + 1 in f 5") == "Int");
  }

  TEST_CASE("Parser retains all parameters after a parameterized signature") {
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\Annotated

check : String -> Bool -> String
check message condition = if condition then message else ""
)",
                                     "annotated.yona");
    REQUIRE(result.has_value());
    REQUIRE(result.value()->functions.size() == 1);
    CHECK(result.value()->functions[0]->patterns.size() == 2);
  }

  TEST_CASE(
      "Parser retains both Dict parameters in instance method signatures") {
    yona::parser::Parser parser;
    yona::compiler::codegen::Codegen bootstrap("dict_signature_bootstrap");
    bootstrap.ModulePaths.push_back(yona::test::lib_dir().string());
    YONA_TEST_INSTALL_PARSER_PRELUDE(bootstrap, parser);
    auto result = parser.parseModule(R"(
module Test\DictSignature

trait Merge a
    merge : a -> a -> a
end

instance Merge (Dict key value)
    merge : Dict key value -> Dict key value -> Dict key value
    merge left _ = left
end
)",
                                     "dict_signature.yona");
    REQUIRE(result.has_value());
    REQUIRE(result.value()->instance_declarations.size() == 1);
    REQUIRE(result.value()->instance_declarations[0]->methods.size() == 1);
    const auto &signature =
        result.value()->instance_declarations[0]->methods[0]->type_signature;
    REQUIRE(signature.has_value());
    const auto *first_arrow =
        std::get_if<std::shared_ptr<yona::compiler::types::FunctionType>>(
            &*signature);
    REQUIRE(first_arrow != nullptr);
    CHECK(std::holds_alternative<
          std::shared_ptr<yona::compiler::types::DictCollectionType>>(
        (*first_arrow)->argumentType));
  }

  TEST_CASE("Constructor pattern preserves a declared tuple as one field") {
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\PatternDiagnostic

export run

type Box = Box (Int, Int)

run _ =
    case Box (1, 2) of
        Box (first, second) -> first + second
    end
)",
                                     "pattern_diagnostic.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    size_t mismatch_count = 0;
    size_t correction_note_count = 0;
    for (const auto &record : diag.records()) {
      if (record.code == ErrorCode::E0100 &&
          record.message.find("constructor pattern 'Box' has 2 fields") !=
              std::string::npos &&
          record.message.find("declares 1 field of type (Int, Int)") !=
              std::string::npos)
        ++mismatch_count;
      if (record.level == DiagLevel::Note &&
          record.message.find("Box ((first, second))") != std::string::npos &&
          record.message.find("Box (first, second)") != std::string::npos)
        ++correction_note_count;
    }
    CHECK(mismatch_count == 1);
    CHECK(correction_note_count == 1);
    CHECK(diag.error_count() == 1);
    CHECK(diag.records().size() == 2);
  }

  TEST_CASE("Nested tuple constructor pattern reports its element mismatch") {
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\NestedPatternDiagnostic

export run

type Box = Box (Int, Int)

run _ =
    case Box (1, 2) of
        Box (("wrong", _)) -> 1
    end
)",
                                     "nested_pattern_diagnostic.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    size_t mismatch_count = 0;
    size_t tuple_parentheses_note_count = 0;
    for (const auto &record : diag.records()) {
      if (record.code == ErrorCode::E0100 &&
          record.message.find("field 1 of constructor pattern 'Box'") !=
              std::string::npos &&
          record.message.find("String") != std::string::npos &&
          record.message.find("Int") != std::string::npos)
        ++mismatch_count;
      if (record.level == DiagLevel::Note &&
          record.message.find("tuple field; match it with") !=
              std::string::npos)
        ++tuple_parentheses_note_count;
    }
    CHECK(mismatch_count == 1);
    CHECK(tuple_parentheses_note_count == 0);
    CHECK(diag.error_count() == 1);
    CHECK(diag.records().size() == 2);
  }

  TEST_CASE(
      "ADT constructor fields preserve parameterized sequence element types") {
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\ParameterizedField

export run

type Box = Box (Seq String)

run : Unit -> Box
run _ = Box ["value"]
)",
                                     "parameterized_field.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE("Module checking binds extern declarations and types constant "
            "definitions as values") {
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\ExternConstant

export backendName

extern raw_backendName : Int -> String = "test_backend_name"

backendName : String
backendName = raw_backendName 0
)",
                                     "extern_constant.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE("Unit parameter patterns match Unit function annotations") {
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\UnitParameter

export available

available : () -> Bool
available () = true
)",
                                     "unit_parameter.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  // ===== Case Expression + Pattern Inference =====

  TEST_CASE("Inference: case with integer patterns") {
    CHECK(check_expr_str("case 42 of 0 -> \"zero\"; _ -> \"other\" end") ==
          "String");
  }

  TEST_CASE("Inference: case with identifier binding") {
    CHECK(check_expr_str("case 42 of x -> x + 1 end") == "Int");
  }

  TEST_CASE("Inference: case with head-tail pattern") {
    CHECK(check_expr_str("case [1, 2, 3] of [h|t] -> h end") == "Int");
  }

  TEST_CASE("Inference: case with empty seq pattern") {
    CHECK(check_expr_str("case [1, 2] of [] -> 0; [h|t] -> h end") == "Int");
  }

  TEST_CASE("Inference: case with tuple pattern") {
    CHECK(check_expr_str("case (1, \"hello\") of (a, b) -> a end") == "Int");
  }

  TEST_CASE("Inference: case branches must unify") {
    // Both branches return Int
    CHECK(check_expr_str("case 1 of 0 -> 10; _ -> 20 end") == "Int");
  }

  TEST_CASE("Inference: cons operator") {
    CHECK(check_expr_str("1 :: [2, 3]") == "Seq Int");
  }

  TEST_CASE("Inference: recursive sum via case") {
    CHECK(check_expr_str("let foldl fn acc seq = case seq of [] -> acc; [h|t] "
                         "-> foldl fn (fn acc h) t end in "
                         "foldl (\\a b -> a + b) 0 [1, 2, 3]") == "Int");
  }

  // ===== Negative Tests (type errors) =====

  static bool check_has_error(const std::string &source) {
    yona::parser::Parser parser;
    std::istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    if (!result || !result->Expression)
      return true; // parse error counts

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(result->Expression.get());
    return checker.has_errors();
  }

  TEST_CASE("Type error: adding string to int") {
    CHECK(check_has_error("1 + \"hello\""));
  }

  TEST_CASE("Type error: join rejects mixed strings and sequences") {
    CHECK(check_has_error("\"left\" ++ [\"right\"]"));
    CHECK(check_has_error("[\"left\"] ++ \"right\""));
  }

  TEST_CASE("Type error: if condition not bool") {
    CHECK(check_has_error("if 42 then 1 else 2"));
  }

  TEST_CASE("Type error: if branches mismatch") {
    CHECK(check_has_error("if true then 1 else \"hello\""));
  }

  TEST_CASE("Type error: undefined variable") {
    CHECK(check_has_error("x + 1"));
  }

  TEST_CASE("Type error: heterogeneous sequence") {
    CHECK(check_has_error("[1, \"hello\", 3]"));
  }

  TEST_CASE("Type error: undefined variable suggests similar name") {
    // "tru" should suggest "true"
    CHECK(check_has_error("tru"));
  }

  TEST_CASE("Type error: typo in let binding suggests correct name") {
    CHECK(check_has_error("let myVariable = 42 in myVarible + 1"));
  }

  TEST_CASE("No error: valid arithmetic") {
    CHECK(!check_has_error("1 + 2 + 3"));
  }

  TEST_CASE("No error: valid let binding") {
    CHECK(!check_has_error("let x = 42 in x + 1"));
  }

  TEST_CASE("No error: valid case expression") {
    CHECK(!check_has_error("case 1 of 0 -> 10; _ -> 20 end"));
  }

  // ===== ADT Tests =====

} // close suite

static std::string check_with_adt(const std::string &source) {
  yona::parser::Parser parser;
  std::istringstream stream(source);
  auto result = parser.parseExpression(stream.str(), "<stream>");
  if (!result || !result->Expression)
    return "PARSE_ERROR";

  yona::compiler::DiagnosticEngine diag;
  yona::compiler::typechecker::TypeChecker checker(diag);
  checker.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}},
                       {{yona::ast::FieldType::simple("a")}, {}});
  auto *t = checker.check(result->Expression.get());
  if (!t)
    return "?";
  return yona::compiler::typechecker::pretty_print(checker.zonk(t));
}

TEST_SUITE("TypeChecker") { // reopen

  TEST_CASE("ADT: Some constructor applied") {
    CHECK(check_with_adt("Some 42") == "Option Int");
  }

  TEST_CASE("ADT: None is polymorphic") {
    auto s = check_with_adt("None");
    // None : forall a. Option a â€” instantiated with a fresh var
    CHECK(s.substr(0, 6) == "Option");
  }

  TEST_CASE("ADT: constructor used in let binding") {
    CHECK(check_with_adt("let opt = Some 42 in opt") == "Option Int");
  }

  TEST_CASE("ADT: Some applied to string") {
    CHECK(check_with_adt("Some \"hello\"") == "Option String");
  }

  // ===== Trait Tests =====

} // close suite

static std::string check_with_traits(const std::string &source,
                                     bool expect_error = false) {
  yona::parser::Parser parser;
  std::istringstream stream(source);
  auto result = parser.parseExpression(stream.str(), "<stream>");
  if (!result || !result->Expression)
    return "PARSE_ERROR";

  yona::compiler::DiagnosticEngine diag;
  yona::compiler::typechecker::TypeChecker checker(diag);

  // Register trait Num with method abs : a -> a
  auto &arena = checker.arena();
  auto *a = arena.fresh_var(0);
  auto *abs_type = arena.make_arrow(a, a);
  checker.register_trait_method("Num", "abs", abs_type);
  checker.register_instance("Num", {"Int"});
  checker.register_instance("Num", {"Float"});

  auto *t = checker.check(result->Expression.get());
  checker.solve_constraints();

  if (expect_error)
    return checker.has_errors() ? "ERROR" : "NO_ERROR";
  if (!t)
    return "?";
  return yona::compiler::typechecker::pretty_print(checker.zonk(t));
}

TEST_SUITE("TypeChecker") { // reopen

  TEST_CASE("Trait: abs applied to Int") {
    CHECK(check_with_traits("abs 42") == "Int");
  }

  TEST_CASE("Trait: abs applied to Float") {
    CHECK(check_with_traits("abs 3.14") == "Float");
  }

  TEST_CASE("Trait error: abs applied to String") {
    CHECK(check_with_traits("abs \"hello\"", true) == "ERROR");
  }

  TEST_CASE("Trait: no error on valid usage") {
    CHECK(check_with_traits("abs 42", false) != "ERROR");
  }

  TEST_CASE("Equality operator requires an Eq instance") {
    yona::parser::Parser parser;
    std::istringstream stream("(\\x -> x) == (\\y -> y)");
    auto parsed = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression != nullptr);

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    CHECK(checker.check(parsed->Expression.get()) != nullptr);
    CHECK_FALSE(checker.solve_constraints());

    size_t missing_eq = 0;
    for (const auto &record : diag.records()) {
      if (record.code == yona::compiler::ErrorCode::E0105 ||
          record.code == yona::compiler::ErrorCode::E0106) {
        CHECK(record.message.find("Eq") != std::string::npos);
        ++missing_eq;
      }
    }
    CHECK(missing_eq == 1);
  }

  TEST_CASE("Ordering operator requires an Ord instance") {
    yona::parser::Parser parser;
    std::istringstream stream("(\\x -> x) < (\\y -> y)");
    auto parsed = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression != nullptr);

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    CHECK(checker.check(parsed->Expression.get()) != nullptr);
    CHECK_FALSE(checker.solve_constraints());

    size_t missing_ord = 0;
    for (const auto &record : diag.records()) {
      if (record.code == yona::compiler::ErrorCode::E0105 ||
          record.code == yona::compiler::ErrorCode::E0106) {
        CHECK(record.message.find("Ord") != std::string::npos);
        ++missing_ord;
      }
    }
    CHECK(missing_ord == 1);
  }

  TEST_CASE(
      "Lifted instance constraints are solved for concrete element types") {
    yona::parser::Parser parser;
    std::istringstream stream("[(\\x -> x)] == [(\\y -> y)]");
    auto parsed = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression != nullptr);

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *element = arena.fresh_var(0);
    checker.register_trait_method(
        "Eq", "eq",
        arena.make_arrow(
            element,
            arena.make_arrow(
                element,
                arena.make_con(yona::compiler::typechecker::TyCon::Bool))));
    checker.register_instance("Eq", {"Seq"}, {"element"}, {{"Eq", "element"}});

    CHECK(checker.check(parsed->Expression.get()) != nullptr);
    CHECK_FALSE(checker.solve_constraints());
    size_t nested_missing = 0;
    for (const auto &record : diag.records())
      if ((record.code == yona::compiler::ErrorCode::E0105 ||
           record.code == yona::compiler::ErrorCode::E0106) &&
          record.message.find("Eq") != std::string::npos)
        ++nested_missing;
    CHECK(nested_missing == 1);
  }

  TEST_CASE("Trait superclass obligations are solved transitively") {
    yona::parser::Parser parser;
    std::istringstream stream("1 < 2");
    auto parsed = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression != nullptr);

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.register_instance("Ord", {"Int"});
    checker.register_trait_superclass("Ord", "Eq");
    CHECK(checker.check(parsed->Expression.get()) != nullptr);
    CHECK_FALSE(checker.solve_constraints());

    size_t missing_eq = 0;
    for (const auto &record : diag.records())
      if ((record.code == yona::compiler::ErrorCode::E0105 ||
           record.code == yona::compiler::ErrorCode::E0106) &&
          record.message.find("Eq") != std::string::npos)
        ++missing_eq;
    CHECK(missing_eq == 1);
  }

  // ===== Codegen Integration Test =====
  // Verify the type checker can be wired into the compilation pipeline

  TEST_CASE(
      "Integration: type checker produces correct types for compilation") {
    auto s = check_expr_str("let f x = x + 1 in let g y = f y in g 10");
    CHECK(s == "Int");
  }

  TEST_CASE("Integration: polymorphic identity function") {
    // let id = \x -> x in id applies to both Int and String
    CHECK(check_expr_str("let id = \\x -> x in id 42") == "Int");
  }

  // ===== Recursive Function Tests =====

  TEST_CASE("Inference: recursive function type") {
    auto s =
        check_expr_str("let f x = if x <= 0 then 0 else f (x - 1) in f 10");
    CHECK(s == "Int");
  }

  TEST_CASE("Inference: recursive foldl type") {
    auto s = check_expr_str(
        "let foldl fn acc s = case s of [] -> acc; [h|t] -> foldl fn (fn acc "
        "h) t end in foldl (\\a b -> a + b) 0 [1, 2, 3]");
    CHECK(s == "Int");
  }

  // ===== Record / Row Type Tests =====

  TEST_CASE("Inference: record literal type") {
    auto s = check_expr_str("{ age = 42, name = 1 }");
    // Fields sorted: age first, name second
    CHECK(s.find("age : Int") != std::string::npos);
    CHECK(s.find("name : Int") != std::string::npos);
  }

  TEST_CASE("Inference: record field access infers field type") {
    auto s = check_expr_str("let r = { x = 42 } in r.x");
    CHECK(s == "Int");
  }

  TEST_CASE("Inference: record field access on multi-field record") {
    auto s = check_expr_str("let r = { a = 1, b = 2, c = 3 } in r.b");
    CHECK(s == "Int");
  }

  // ===== Sum Type / Typed Pattern Tests =====

  TEST_CASE("Inference: typed pattern binds correct type") {
    // case 42 of (n : Int) -> n + 1 end
    // Typed pattern binds n as Int
    auto s = check_expr_str("case 42 of (n : Int) -> n + 1 end");
    CHECK(s == "Int");
  }

  TEST_CASE("Inference: typed pattern with string type") {
    auto s =
        check_expr_str("case \"hello\" of (s : String) -> s ++ \" world\" end");
    CHECK(s == "String");
  }

  // ===== Effect Type Tracking =====

  TEST_CASE("Effect: perform with registered effect returns correct type") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();

    // Register: effect State; get : () -> Int; put : Int -> ()
    auto *int_t = arena.make_con(TyCon::Int);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s",
                            {
                                {"get", {}, int_t},
                                {"put", {int_t}, unit_t},
                            });

    // Build: handle (perform State.get ()) with State.get () resume -> resume
    // 42 end
    SourceRange sc{};
    std::vector<yona::ast::ExprNode *> no_args;
    auto *perform_node =
        new yona::ast::PerformExpr(sc, "State", "get", no_args);

    // Wrap perform in a handle so no "unhandled" warning
    auto *lit42 = new yona::ast::IntegerExpr(sc, 42);
    // resume 42
    auto *resume_id = new yona::ast::IdentifierExpr(
        sc, new yona::ast::NameExpr(sc, "resume"));
    std::vector<std::variant<yona::ast::ExprNode *, yona::ast::ValueExpr *>>
        resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode *>(lit42));
    auto *resume_call = new yona::ast::ApplyExpr(
        sc, new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc, "resume")),
        resume_args);

    auto *handler = new yona::ast::HandlerClause(sc, "State", "get", {},
                                                 "resume", resume_call);
    auto *handle = new yona::ast::HandleExpr(sc, perform_node, {handler});
    auto *main_node = new yona::ast::MainNode(sc, handle);

    auto *t = checker.check(main_node);
    REQUIRE(t != nullptr);
    auto *zt = checker.zonk(t);
    CHECK(pretty_print(zt) == "Int");

    delete main_node;
  }

  TEST_CASE("Effect: unhandled perform produces warning") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);

    // Register effect
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    // perform State.get () without handle
    SourceRange sc{};
    std::vector<yona::ast::ExprNode *> no_args;
    auto *perform_node =
        new yona::ast::PerformExpr(sc, "State", "get", no_args);
    auto *main_node = new yona::ast::MainNode(sc, perform_node);

    checker.check(main_node);
    CHECK(diag.warning_count() > 0);

    delete main_node;
  }

  TEST_CASE("Effect: perform arg type mismatch is an error") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();

    // effect State; put : Int -> ()
    auto *int_t = arena.make_con(TyCon::Int);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"put", {int_t}, unit_t}});

    // perform State.put "hello" â€” String where Int expected
    SourceRange sc{};
    auto *str_arg = new yona::ast::StringExpr(sc, "hello");
    std::vector<yona::ast::ExprNode *> args = {str_arg};
    auto *perform_node = new yona::ast::PerformExpr(sc, "State", "put", args);
    auto *main_node = new yona::ast::MainNode(sc, perform_node);

    checker.check(main_node);
    CHECK(checker.has_errors());

    delete main_node;
  }

  TEST_CASE("Effect: handle with return clause transforms result") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();

    // Register: effect Log; log : String -> ()
    auto *string_t = arena.make_con(TyCon::String);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("Log", "", {{"log", {string_t}, unit_t}});

    // handle (perform Log.log "hi") with
    //   Log.log msg resume -> resume ()
    //   return val -> 42
    // end
    SourceRange sc{};
    auto *str_arg = new yona::ast::StringExpr(sc, "hi");
    std::vector<yona::ast::ExprNode *> args = {str_arg};
    auto *perform_node = new yona::ast::PerformExpr(sc, "Log", "log", args);

    // Log.log handler clause: resume ()
    auto *unit_val = new yona::ast::UnitExpr(sc);
    auto *resume_id = new yona::ast::IdentifierExpr(
        sc, new yona::ast::NameExpr(sc, "resume"));
    std::vector<std::variant<yona::ast::ExprNode *, yona::ast::ValueExpr *>>
        resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode *>(unit_val));
    auto *resume_call = new yona::ast::ApplyExpr(
        sc, new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc, "resume")),
        resume_args);
    auto *op_handler = new yona::ast::HandlerClause(sc, "Log", "log", {"msg"},
                                                    "resume", resume_call);

    // return handler: return val -> 42
    auto *lit42 = new yona::ast::IntegerExpr(sc, 42);
    auto *ret_handler = new yona::ast::HandlerClause(sc, "val", lit42);

    auto *handle =
        new yona::ast::HandleExpr(sc, perform_node, {op_handler, ret_handler});
    auto *main_node = new yona::ast::MainNode(sc, handle);

    auto *t = checker.check(main_node);
    REQUIRE(t != nullptr);
    auto *zt = checker.zonk(t);
    CHECK(pretty_print(zt) == "Int");

    delete main_node;
  }

  TEST_CASE("Effect: applying unhandled perform lambda is E0202") {
    yona::parser::Parser parser;
    string source =
        R"(let plan = \() -> perform Fs.read "/etc/shadow" in plan ())";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    CHECK(checker.has_direct_errors());
    CHECK(diag.has_errors());
  }

  TEST_CASE("Effect: handle covers apply of perform lambda") {
    yona::parser::Parser parser;
    string source = R"(
handle
    let f = \x -> perform Fs.read x in
    f "ok"
with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
  }

  static bool check_source_has_direct_errors(const string &source) {
    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    return checker.has_direct_errors();
  }

  TEST_CASE("Effect: HOF apply of perform lambda is E0202") {
    CHECK(check_source_has_direct_errors(
        R"(let apply = \f x -> f x in apply (\() -> perform Fs.read "/etc/shadow") ())"));
  }

  TEST_CASE("Effect: handle covers HOF apply of perform lambda") {
    CHECK_FALSE(check_source_has_direct_errors(R"(
let apply = \f x -> f x in
handle apply (\x -> perform Fs.read x) "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)"));
  }

  TEST_CASE("Effect: wrapping perform lambda apply is E0202") {
    CHECK(check_source_has_direct_errors(R"(
let f = \() -> perform Fs.read "/etc/shadow" in
let g = \() -> f () in
g ()
)"));
  }

  TEST_CASE("Effect: handle covers wrapped perform lambda") {
    CHECK_FALSE(check_source_has_direct_errors(R"(
let f = \() -> perform Fs.read "/etc/shadow" in
let g = \() -> f () in
handle g () with
    Fs.read path resume -> resume path
    return val -> val
end
)"));
  }

  TEST_CASE("Effect: imported FN effects from .yonai are E0202") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_fx_e0202";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Fx.yonai");
      out << "MODULE Test\\Fx\n"
             "FN fetch 1 STRING -> STRING effects Fs.read\n";
    }
    yona::parser::Parser parser;
    string source = R"(import fetch from Test\Fx in fetch "/etc/shadow")";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    checker.check(parsed.value().get());
    CHECK(checker.has_direct_errors());
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("Effect: handle covers imported FN from .yonai") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_fx_handle";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Fx.yonai");
      out << "MODULE Test\\Fx\n"
             "FN fetch 1 STRING -> STRING effects Fs.read\n";
    }
    yona::parser::Parser parser;
    string source = R"(
import fetch from Test\Fx in
handle fetch "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("Imported zero-arity FN remains callable with its known effects") {
    ScopedInterfaceTestDirectory dir("yonai_zero_arity_effect");
    {
      std::ofstream out(dir.path() / "Test" / "Thunk.yonai");
      out << "MODULE Test\\Thunk\n"
             "FN thunk 0 -> INT effects Fs.read\n"
             "FN pureThunk 0 -> INT effects - effectscheme $##;$/r##\n";
    }

    yona::parser::Parser unhandled_parser;
    auto unhandled = unhandled_parser.parseExpression(
        R"(import thunk from Test\Thunk in thunk ())", "<test>");
    REQUIRE(unhandled.has_value());
    DiagnosticEngine unhandled_diag;
    TypeChecker unhandled_checker(unhandled_diag);
    unhandled_checker.add_module_path(dir.path().string());
    unhandled_checker.check(unhandled.value().get());
    CHECK(std::any_of(unhandled_diag.records().begin(),
                      unhandled_diag.records().end(), [](const auto &record) {
                        return record.level == DiagLevel::Error &&
                               record.code == ErrorCode::E0202;
                      }));
    CHECK_FALSE(std::any_of(
        unhandled_diag.records().begin(), unhandled_diag.records().end(),
        [](const auto &record) {
          return record.level == DiagLevel::Error &&
                 record.code == ErrorCode::E0100;
        }));

    yona::parser::Parser handled_parser;
    auto handled = handled_parser.parseExpression(R"(
import thunk from Test\Thunk in
handle thunk () with
    Fs.read path resume -> resume 0
    return val -> val
end
)",
                                                   "<test>");
    REQUIRE(handled.has_value());
    DiagnosticEngine handled_diag;
    TypeChecker handled_checker(handled_diag);
    handled_checker.add_module_path(dir.path().string());
    handled_checker.check(handled.value().get());
    CHECK_FALSE(handled_checker.has_direct_errors());

    yona::parser::Parser pure_parser;
    auto pure = pure_parser.parseExpression(
        R"(import pureThunk from Test\Thunk in pureThunk ())", "<test>");
    REQUIRE(pure.has_value());
    DiagnosticEngine pure_diag;
    TypeChecker pure_checker(pure_diag);
    pure_checker.add_module_path(dir.path().string());
    auto *pure_type = pure_checker.check(pure.value().get());
    REQUIRE(pure_type != nullptr);
    CHECK(pretty_print(pure_checker.zonk(pure_type)) == "Int");
    CHECK_FALSE(pure_checker.has_direct_errors());

  }

  TEST_CASE("Native zero-arity FN imports remain values") {
    if (!std::filesystem::exists(yona::test::lib_dir()))
      return;
    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        R"(import executablePath from Std\Process in executablePath)",
        "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(yona::test::lib_dir().string());
    auto *type = checker.check(parsed.value().get());
    REQUIRE(type != nullptr);
    CHECK(pretty_print(checker.zonk(type)) == "String");
    CHECK_FALSE(checker.has_direct_errors());
  }

  TEST_CASE("Zero-arity FUNCTION import remains a function-valued CAF") {
    ScopedInterfaceTestDirectory dir("yonai_function_caf");
    {
      std::ofstream out(dir.path() / "Test" / "FunctionCaf.yonai");
      out << "MODULE Test\\FunctionCaf\n"
             "FN value 0 -> FUNCTION(INT,INT) effects - effectscheme "
             "$##;$/r##\n";
    }

    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        R"(import value from Test\FunctionCaf in value 1)", "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    checker.add_module_path(dir.path().string());
    auto *type = checker.check(parsed.value().get());
    REQUIRE(type != nullptr);
    CHECK(pretty_print(checker.zonk(type)) == "Int");
    CHECK_FALSE(checker.has_direct_errors());
  }

  TEST_CASE("Zero-arity thunk scheme shifts nested effect paths") {
    ScopedInterfaceTestDirectory dir("yonai_nested_thunk_scheme");
    {
      std::ofstream out(dir.path() / "Test" / "NestedThunk.yonai");
      out << "MODULE Test\\NestedThunk\n"
             "FN thunk 0 -> FUNCTION(UNIT,FUNCTION(INT,INT)) effects - "
             "effectscheme "
             "$##;$/r#Fs.read#;$/r/r#Log.log#\n";
    }

    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        R"(import thunk from Test\NestedThunk in thunk)", "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    checker.add_module_path(dir.path().string());
    auto *type = checker.check(parsed.value().get());
    REQUIRE(type != nullptr);
    CHECK(pretty_print(checker.zonk(type)) ==
          "(() -> !{Fs.read} (Int -> !{Log.log} Int))");
    CHECK_FALSE(checker.has_direct_errors());
  }

  TEST_CASE("Non-entry scheme substrings do not create zero-arity thunks") {
    ScopedInterfaceTestDirectory dir("yonai_non_entry_scheme_substring");
    {
      std::ofstream out(dir.path() / "Test" / "NotThunk.yonai");
      out << "MODULE Test\\NotThunk\n"
             "FN value 0 -> INT effects - effectscheme $#$/r##\n";
    }

    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        R"(import value from Test\NotThunk in value ())", "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    checker.add_module_path(dir.path().string());
    checker.check(parsed.value().get());
    CHECK(diagnostics.has_errors());
    CHECK(std::any_of(diagnostics.records().begin(),
                      diagnostics.records().end(), [](const auto &record) {
                        return record.level == DiagLevel::Error &&
                               record.code == ErrorCode::E0100;
                      }));
  }

  TEST_CASE("Canonical imports preserve legacy wildcard descriptors") {
    ScopedInterfaceTestDirectory dir("yonai_legacy_shapes");
    {
      std::ofstream out(dir.path() / "Test" / "Shapes.yonai");
      out << "MODULE Test\\Shapes\n"
             "FN legacy 2 LINEAR TUPLE -> TUPLE\n"
             "FN structured 1 TUPLE(INT,STRING) -> LINEAR(INT)\n"
             "FN exact 1 INT -> INT\n";
    }

    SUBCASE("bare ABI-only descriptors are independent fresh wildcards") {
      yona::parser::Parser parser;
      auto parsed = parser.parseExpression(
          R"(import legacy from Test\Shapes in legacy "left" true)",
          "<test>");
      REQUIRE(parsed.has_value());
      DiagnosticEngine diag;
      TypeChecker checker(diag);
      checker.add_module_path(dir.path().string());
      CHECK(checker.check(parsed.value().get()) != nullptr);
      CHECK_FALSE(checker.has_direct_errors());
    }

    SUBCASE("parameterized descriptors remain structural") {
      yona::parser::Parser parser;
      auto parsed = parser.parseExpression(
          R"(import structured from Test\Shapes in structured (1, "two"))",
          "<test>");
      REQUIRE(parsed.has_value());
      DiagnosticEngine diag;
      TypeChecker checker(diag);
      checker.add_module_path(dir.path().string());
      auto *type = checker.check(parsed.value().get());
      REQUIRE(type != nullptr);
      CHECK(pretty_print(checker.zonk(type)) == "Linear Int");
      CHECK_FALSE(checker.has_direct_errors());
    }

    SUBCASE("parameterized descriptors reject nested mismatches") {
      yona::parser::Parser parser;
      auto parsed = parser.parseExpression(
          R"(import structured from Test\Shapes in structured (1, 2))",
          "<test>");
      REQUIRE(parsed.has_value());
      DiagnosticEngine diag;
      TypeChecker checker(diag);
      checker.add_module_path(dir.path().string());
      checker.check(parsed.value().get());
      CHECK(diag.has_errors());
    }

    SUBCASE("exact scalar atoms reject mismatches") {
      yona::parser::Parser parser;
      auto parsed = parser.parseExpression(
          R"(import exact from Test\Shapes in exact "not-an-int")",
          "<test>");
      REQUIRE(parsed.has_value());
      DiagnosticEngine diag;
      TypeChecker checker(diag);
      checker.add_module_path(dir.path().string());
      checker.check(parsed.value().get());
      CHECK(diag.has_errors());
    }
  }

  TEST_CASE("Effect: imported HOF open rest from .yonai is E0202") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_hof_e0202";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Hof.yonai");
      out << "MODULE Test\\Hof\n"
             "FN apply 2 FUNCTION UNIT -> STRING effects | hof\n";
    }
    yona::parser::Parser parser;
    string source =
        R"(import apply from Test\Hof in apply (\() -> perform Fs.read "/etc/shadow") ())";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    checker.check(parsed.value().get());
    CHECK(checker.has_direct_errors());
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("Effect: handle covers imported HOF from .yonai") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_hof_handle";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Hof.yonai");
      out << "MODULE Test\\Hof\n"
             "FN apply 2 FUNCTION UNIT -> STRING effects | hof\n";
    }
    yona::parser::Parser parser;
    string source = R"(
import apply from Test\Hof in
handle apply (\x -> perform Fs.read x) "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("Effect: handle subtracts covered op from enclosing row") {
    CHECK(check_source_has_direct_errors(R"(
let f = \x -> let a = perform Fs.read x, b = perform Net.post x in a in
let g = \x -> handle (f x) with
    Fs.read path resume -> resume path
    return val -> val
end in
g "ok"
)"));
  }

  TEST_CASE("Effect: handle covers apply of lambda defined outside handle") {
    yona::parser::Parser parser;
    string source = R"(
let f = \x -> perform Fs.read x in
handle f "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parseExpression(source, "<test>");
    REQUIRE(parsed.has_value());

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
  }

  TEST_CASE("Effect: no error for handled perform") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();

    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    // handle (perform State.get ()) with State.get () resume -> resume 0 end
    SourceRange sc{};
    std::vector<yona::ast::ExprNode *> no_args;
    auto *perform_node =
        new yona::ast::PerformExpr(sc, "State", "get", no_args);

    auto *lit0 = new yona::ast::IntegerExpr(sc, 0);
    std::vector<std::variant<yona::ast::ExprNode *, yona::ast::ValueExpr *>>
        resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode *>(lit0));
    auto *resume_call = new yona::ast::ApplyExpr(
        sc, new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc, "resume")),
        resume_args);
    auto *handler = new yona::ast::HandlerClause(sc, "State", "get", {},
                                                 "resume", resume_call);

    auto *handle = new yona::ast::HandleExpr(sc, perform_node, {handler});
    auto *main_node = new yona::ast::MainNode(sc, handle);

    checker.check(main_node);
    CHECK(!checker.has_errors());
    CHECK(diag.warning_count() == 0);

    delete main_node;
  }

  TEST_CASE("Canonical interface signatures cover representable type shapes") {
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    auto &arena = checker.arena();
    auto *integer = arena.make_con(TyCon::Int);
    auto *floating = arena.make_con(TyCon::Float);
    auto *boolean = arena.make_con(TyCon::Bool);
    auto *string = arena.make_con(TyCon::String);
    auto *byte_array = arena.make_con(TyCon::ByteArray);
    auto *box = arena.make_app("Box", {integer});
    auto *linear_box = arena.make_app("Linear", {box});
    auto *result = arena.make_tuple(
        {arena.make_app("Seq", {integer}), arena.make_app("Set", {string}),
         arena.make_app("Dict", {integer, boolean}),
         arena.make_app("Option", {floating}), byte_array});
    auto *function = arena.make_arrow(linear_box, result);

    const auto signature = checker.serialize_interface_signature(function, 1);
    REQUIRE(signature.parameter_descriptors.size() == 1);
    CHECK(signature.parameter_descriptors[0] == "LINEAR(ADT(Box,INT))");
    CHECK(signature.return_descriptor ==
          "TUPLE(Seq(INT),Set(STRING),Dict(INT,BOOL),ADT(Option,FLOAT),"
          "BYTE_ARRAY)");
    CHECK(signature.effect_scheme == "$##");

    auto *legacy_adt = arena.make_app("ADT", {integer});
    const auto forwarded_legacy_adt = checker.serialize_interface_signature(
        arena.make_arrow(legacy_adt, legacy_adt), 1);
    CHECK(forwarded_legacy_adt.parameter_descriptors ==
          std::vector<std::string>{"ADT"});
    CHECK(forwarded_legacy_adt.return_descriptor == "ADT");

    auto *record = arena.make_record({{"field", integer}});
    CHECK_THROWS_AS(checker.serialize_interface_signature(
                        arena.make_arrow(record, integer), 1),
                    std::invalid_argument);
  }

  TEST_CASE("Effect row: function with unhandled perform has latent row") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("(\\x -> perform State.get ())");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    CHECK(printed.find("!{State.get}") != std::string::npos);
    CHECK(!checker.has_direct_errors()); // captured on the arrow, not top-level
  }

  TEST_CASE("Effect row: handle subtracts covered ops from latent row") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("(\\x -> handle perform State.get () with "
                              "State.get () resume -> resume 1 end)");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    CHECK(printed.find("!{") == std::string::npos);
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: partial handler leaves remaining ops") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s",
                            {
                                {"get", {}, int_t},
                                {"put", {int_t}, unit_t},
                            });

    yona::parser::Parser parser;
    // handle only get; put still escapes
    std::istringstream stream(
        "(\\x -> handle\n"
        "  let _ = perform State.put 1 in perform State.get ()\n"
        "with\n"
        "  State.get () resume -> resume 0\n"
        "end)");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    CHECK(printed.find("State.put") != std::string::npos);
    CHECK(printed.find("State.get") == std::string::npos);
  }

  TEST_CASE("Effect row: applying effectful function without handle is E0202") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let f = (\\x -> perform State.get ()) in f 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
    CHECK(error_explanation(ErrorCode::E0202).find("E0202") !=
          std::string::npos);
  }

  TEST_CASE("Effect row: E0202 points at the introducing perform") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    // perform is on line 1; the escaping call is on line 2
    const char *src = "let f = (\\x -> perform State.get ()) in\n"
                      "f 0";
    yona::parser::Parser parser;
    std::istringstream stream(src);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    REQUIRE(checker.has_errors());

    const DiagnosticEngine::Record *e0202 = nullptr;
    for (auto &rec : diag.records()) {
      if (rec.level == DiagLevel::Error && rec.code == ErrorCode::E0202) {
        e0202 = &rec;
        break;
      }
    }
    REQUIRE(e0202 != nullptr);
    CHECK(e0202->Range.Line == 1);
    CHECK(e0202->message.find("State.get") != std::string::npos);

    bool saw_call_note = false;
    for (auto &rec : diag.records()) {
      if (rec.level == DiagLevel::Note && rec.Range.Line == 2)
        saw_call_note = true;
    }
    CHECK(saw_call_note);
  }

  TEST_CASE(
      "Effect row: applying effectful function inside matching handle is OK") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream(
        "let f = (\\x -> perform State.get ()) in\n"
        "handle f 0 with State.get () resume -> resume 7 end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: HOF apply threads an open rest") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    std::istringstream stream("(\\f x -> f x)");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto *zt = checker.zonk(t);
    REQUIRE(zt != nullptr);
    REQUIRE(zt->tag == MonoType::Arrow);
    REQUIRE(zt->param_type != nullptr);
    REQUIRE(zt->param_type->tag == MonoType::Arrow);
    REQUIRE(zt->return_type != nullptr);
    REQUIRE(zt->return_type->tag == MonoType::Arrow);
    // (a -> !{|r} b) -> a -> !{|r} b â€” both arrows retain the same
    // symbolic solver projection.
    CHECK(checker.effect_row_info(zt->param_type).open_rest);
    CHECK(checker.effect_row_info(zt->return_type).open_rest);
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: HOF apply of effectful function is E0202") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let apply = (\\f x -> f x) in\n"
                              "let g = (\\y -> perform State.get ()) in\n"
                              "apply g 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
  }

  TEST_CASE("Effect row: E0202 through a HOF points at perform") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    const char *src = "let apply = (\\f x -> f x) in\n"
                      "let g = (\\y -> perform State.get ()) in\n"
                      "apply g 0";
    yona::parser::Parser parser;
    std::istringstream stream(src);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    REQUIRE(checker.has_errors());

    const DiagnosticEngine::Record *e0202 = nullptr;
    for (auto &rec : diag.records()) {
      if (rec.level == DiagLevel::Error && rec.code == ErrorCode::E0202) {
        e0202 = &rec;
        break;
      }
    }
    REQUIRE(e0202 != nullptr);
    CHECK(e0202->Range.Line == 2);
    CHECK(e0202->message.find("State.get") != std::string::npos);
  }

  TEST_CASE("Effect row: HOF apply of effectful function inside handle is OK") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream(
        "let apply = (\\f x -> f x) in\n"
        "let g = (\\y -> perform State.get ()) in\n"
        "handle apply g 0 with State.get () resume -> resume 7 end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: HOF that returns an effectful function keeps effects "
            "on the inner arrow") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("(\\x -> (\\y -> perform State.get ()))");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    // a -> (b -> !{State.get} Int) â€” outer arrow is pure
    CHECK(printed.find("!{State.get}") != std::string::npos);
    // The first (outer) arrow should not itself carry the row.
    auto outer_end = printed.find("->");
    REQUIRE(outer_end != std::string::npos);
    CHECK(printed.substr(0, outer_end).find("!{") == std::string::npos);
    CHECK(!checker.has_direct_errors());
  }

  TEST_CASE("Effect row: applying a returned effectful function is E0202") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream(
        "let mk = (\\x -> (\\y -> perform State.get ())) in mk 0 1");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
  }

  TEST_CASE("Effect row: HOF own perform unions with applied open rest") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("(\\f x -> let _ = perform State.get () in f x)");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto *zt = checker.zonk(t);
    REQUIRE(zt != nullptr);
    REQUIRE(zt->tag == MonoType::Arrow);
    REQUIRE(zt->return_type != nullptr);
    REQUIRE(zt->return_type->tag == MonoType::Arrow);
    auto *result_arrow = zt->return_type;
    auto result_printed = pretty_print(result_arrow);
    CHECK(result_printed.find("State.get") != std::string::npos);
    CHECK(checker.effect_row_info(result_arrow).open_rest);
  }

  TEST_CASE("Effect row: two-function HOF unions distinct closed rows at the "
            "call site") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"get", {}, int_t}});
    checker.register_effect("Log", "", {{"log", {int_t}, unit_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let app2 = (\\f g x -> let _ = f x in g x) in\n"
                              "let get = (\\y -> perform State.get ()) in\n"
                              "let log = (\\z -> perform Log.log 1) in\n"
                              "app2 get log 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
  }

  TEST_CASE("Effect row: two-function HOF with both effects handled is OK") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    auto *unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"get", {}, int_t}});
    checker.register_effect("Log", "", {{"log", {int_t}, unit_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let app2 = (\\f g x -> let _ = f x in g x) in\n"
                              "let get = (\\y -> perform State.get ()) in\n"
                              "let log = (\\z -> perform Log.log 1) in\n"
                              "handle app2 get log 0 with\n"
                              "  State.get () resume -> resume 0\n"
                              "  Log.log n resume -> resume ()\n"
                              "end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: recursive function captures body effects, not an "
            "empty row") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let loop n = if n <= 0 then 0 else (perform "
                              "State.get ()) + loop (n - 1) in loop");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    CHECK(printed.find("!{State.get}") != std::string::npos);
    CHECK(printed.find("!{|") == std::string::npos);
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: recursive effectful function applied at top level is "
            "E0202") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream("let loop n = if n <= 0 then 0 else (perform "
                              "State.get ()) + loop (n - 1) in loop 3");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
  }

  TEST_CASE("Effect row: recursive effectful function inside handle is OK") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream(
        "let loop n = if n <= 0 then 0 else (perform State.get ()) + loop (n - "
        "1) in\n"
        "handle loop 3 with State.get () resume -> resume 0 end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(!checker.has_errors());
  }

  TEST_CASE("Effect row: recursive pure function does not keep an unsound open "
            "rest") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = if x <= 0 then 0 else f (x - 1) in f");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    auto printed = pretty_print(checker.zonk(t));
    CHECK(printed.find("!{") == std::string::npos);
    CHECK(!checker.has_errors());
  }

  TEST_CASE(
      "Effect row: pure mutually recursive module functions close their rows") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\MutualEffects

export even
export odd

even n = odd n
odd n = even n
)",
                                     "mutual_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 2);
    CHECK(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK(
        checker.is_effect_free(checker.type_of(result.value()->functions[1])));
  }

  TEST_CASE(
      "Effect row: pure mutually recursive local functions close their rows") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\LocalMutualEffects

export run

run n = let even k = case k of
    0 -> true
    _ -> odd k
end,
odd k = case k of
    0 -> false
    _ -> even k
end in even n
)",
                                     "local_mutual_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE_FALSE(diag.has_errors());
    REQUIRE(result.value()->functions.size() == 1);
    CHECK(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
  }

  TEST_CASE("Effect row: mutually recursive local functions retain callback "
            "effects") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\LocalMutualCallbackEffects

export run

run f n = let left k = right k,
    right k = f k
in left n
)",
                                     "local_mutual_callback_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE_FALSE(diag.has_errors());
    REQUIRE(result.value()->functions.size() == 1);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK(checker.effect_row_info(checker.type_of(result.value()->functions[0]))
              .open_rest);
  }

  TEST_CASE("Effect row: mutually recursive module functions retain concrete "
            "effects") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto *int_type = checker.arena().make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_type}});

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\MutualConcreteEffects

export first
export second

first n = second n
second n = (perform State.get ()) + first n
)",
                                     "mutual_concrete_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 2);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[1])));
    CHECK(checker.closed_effect_ops(
              checker.type_of(result.value()->functions[0])) ==
          std::vector<std::string>{"State.get"});
    CHECK(checker.closed_effect_ops(
              checker.type_of(result.value()->functions[1])) ==
          std::vector<std::string>{"State.get"});
  }

  TEST_CASE(
      "Effect row: module forwarding preserves a higher-order parameter rest") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\ForwardHigherOrderEffects

export forward
export apply

forward f x = apply f x
apply f x = f x
)",
                                     "forward_higher_order_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 2);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[1])));
  }

  TEST_CASE(
      "Effect row: structurally recursive HOF preserves its parameter rest") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\RecursiveHigherOrderEffects

export app
type Nat = Zero | Succ Nat

app f n = case n of Zero -> f n; Succ rest -> app f rest end
)",
                                     "recursive_higher_order_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 1);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
  }

  TEST_CASE("Effect row: recursive HOF preserves its parameter rest in either "
            "arm order") {
    const std::vector<std::pair<std::string, std::string>> modules = {
        {"callback-first", R"(
module Test\CallbackFirst

export app
type Nat = Zero | Succ Nat

app f n = case n of Zero -> f n; Succ rest -> app f rest end
)"},
        {"recursive-first", R"(
module Test\RecursiveFirst

export app
type Nat = Zero | Succ Nat

app f n = case n of Succ rest -> app f rest; Zero -> f n end
)"},
    };

    for (const auto &[order, source] : modules) {
      CAPTURE(order);
      yona::compiler::DiagnosticEngine diag;
      yona::compiler::typechecker::TypeChecker checker(diag);
      yona::parser::Parser parser;
      auto result = parser.parseModule(source, order + ".yona");
      REQUIRE(result.has_value());

      checker.check_module(result.value().get());

      REQUIRE_FALSE(checker.has_direct_errors());
      REQUIRE(result.value()->functions.size() == 1);
      CHECK_FALSE(checker.is_effect_free(
          checker.type_of(result.value()->functions[0])));
    }
  }

  TEST_CASE("Effect row: preliminary unification cannot erase an HOF rest") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\RankContamination

export a
export b
type Nat = Zero | Succ Nat

a f n = case n of Succ rest -> (a f) rest; Zero -> b f n end
b f n = case n of Succ rest -> (a f) rest; Zero -> f n end
)",
                                     "rank_contamination.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 2);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[1])));
  }

  TEST_CASE("Effect row: recursive calls do not equate returned callbacks") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_type = arena.make_con(TyCon::Int);
    auto *unit_type = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"get", {}, int_type}});
    checker.register_effect("Log", "", {{"log", {unit_type}, unit_type}});

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\PreliminaryIndependentRows

export result

left f g n = do use f g n; f end
right f g n = do use f g n; g end
use f g n = ((left f g n) n, (right f g n) n)
get x = do perform State.get (); x end
log x = do perform Log.log (); x end
result = use get log 0
)",
                                     "preliminary_independent_rows.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE(
      "Effect row: independent HOF callbacks retain all concrete effects") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_type = arena.make_con(TyCon::Int);
    auto *unit_type = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"get", {}, int_type}});
    checker.register_effect("Log", "", {{"log", {unit_type}, unit_type}});

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\IndependentCallbackEffects

export result

use f g n = (f n, g n)
get x = do perform State.get (); x end
log x = do perform Log.log (); x end
result = use get log 0
)",
                                     "independent_callback_effects.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE_FALSE(diag.has_errors());
    REQUIRE(result.value()->functions.size() == 4);
    CHECK(checker.closed_effect_ops(
              checker.type_of(result.value()->functions[3])) ==
          std::vector<std::string>{"Log.log", "State.get"});
  }

  TEST_CASE("Effect row: three independent callbacks form a "
            "source-order-invariant union") {
    const std::vector<std::pair<std::string, std::string>> bodies = {
        {"forward", "use f g h n = (f n, g n, h n)"},
        {"reordered", "use f g h n = (h n, f n, g n)"},
    };
    for (const auto &[name, use_body] : bodies) {
      CAPTURE(name);
      yona::compiler::DiagnosticEngine diag;
      yona::compiler::typechecker::TypeChecker checker(diag);
      auto &arena = checker.arena();
      auto *int_type = arena.make_con(TyCon::Int);
      checker.register_effect("State", "s", {{"get", {}, int_type}});
      checker.register_effect("Log", "", {{"log", {}, int_type}});
      checker.register_effect("Net", "", {{"ping", {}, int_type}});

      yona::parser::Parser parser;
      auto result =
          parser.parseModule("module Test\\ThreeCallback" + name +
                                 "\n\n"
                                 "export result\n\n" +
                                 use_body +
                                 "\n"
                                 "get x = do perform State.get (); x end\n"
                                 "log x = do perform Log.log (); x end\n"
                                 "ping x = do perform Net.ping (); x end\n"
                                 "result = use get log ping 0\n",
                             "three_callback_" + name + ".yona");
      REQUIRE(result.has_value());

      checker.check_module(result.value().get());

      REQUIRE_FALSE(checker.has_direct_errors());
      REQUIRE_FALSE(diag.has_errors());
      REQUIRE(result.value()->functions.size() == 5);
      CHECK(checker.closed_effect_ops(
                checker.type_of(result.value()->functions[4])) ==
            std::vector<std::string>{"Log.log", "Net.ping", "State.get"});
    }
  }

  TEST_CASE("Effect row: a handler symbolically masks a callback effect after "
            "instantiation") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto *int_type = checker.arena().make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_type}});

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\SymbolicHandlerMask

export run

use f n = f n
get x = do perform State.get (); x end
run n = handle use get n with State.get () resume -> resume 0 end
)",
                                     "symbolic_handler_mask.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE_FALSE(diag.has_errors());
    REQUIRE(result.value()->functions.size() == 3);
    CHECK(
        checker.closed_effect_ops(checker.type_of(result.value()->functions[2]))
            .empty());
    CHECK(
        checker.is_effect_free(checker.type_of(result.value()->functions[2])));
  }

  TEST_CASE("Effect scheme: normalized interface graph preserves independent "
            "callback projections") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\EffectScheme

export use

use f g n = (f n, g n)
)",
                                     "effect_scheme.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());
    REQUIRE_FALSE(checker.has_direct_errors());
    auto *original = checker.type_of(result.value()->functions[0]);
    REQUIRE(original != nullptr);

    const auto encoded =
        checker.serialize_effect_scheme(checker.zonk(original));
    CHECK(encoded.starts_with("$#"));
    CHECK(encoded == checker.serialize_effect_scheme(checker.zonk(original)));

    auto *restored =
        checker.apply_effect_scheme(checker.zonk(original), encoded);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->tag == MonoType::Arrow);
    // A curried function only evaluates its body after the final source
    // argument.  The interface must therefore preserve the two independent
    // callback projections on that final arrow, while the outer partial
    // application stages remain pure.
    const auto final_arrow = [&](MonoTypePtr current) {
      MonoTypePtr result = nullptr;
      while (current && current->tag == MonoType::Arrow) {
        result = current;
        current = current->return_type;
      }
      return result;
    };
    const auto original_summary = checker.arena().effect_solver().summarize(
        final_arrow(original)->arrow_effect);
    const auto restored_summary = checker.arena().effect_solver().summarize(
        final_arrow(restored)->arrow_effect);
    CHECK(original_summary.known_labels == restored_summary.known_labels);
    CHECK(checker.effect_row_info(original).open_rest);
    CHECK(checker.effect_row_info(restored).open_rest);
    REQUIRE(restored_summary.tails.size() == 2);
    CHECK_FALSE(restored_summary.tails[0].opaque);
    CHECK_FALSE(restored_summary.tails[1].opaque);
    CHECK(restored_summary.tails[0].variable !=
          restored_summary.tails[1].variable);
  }

  TEST_CASE("Effect row: module forwarding preserves an imported open rest") {
    namespace fs = std::filesystem;
    const auto dir =
        fs::temp_directory_path() / "yona_module_forward_open_effect";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Open.yonai");
      out << "MODULE Test\\Open\n"
             "FN run 1 INT -> INT effects |\n";
    }

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.add_module_path(dir.string());

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\ForwardImportedOpen

export forward

forward n = sibling n
sibling n = import run from Test\Open in run n
)",
                                     "forward_imported_open.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    REQUIRE_FALSE(checker.has_direct_errors());
    REQUIRE(result.value()->functions.size() == 2);
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[0])));
    CHECK_FALSE(
        checker.is_effect_free(checker.type_of(result.value()->functions[1])));

    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("Canonical interface INT descriptors are exact") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "yona_exact_int_interface";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "ExactInt.yonai");
      out << "MODULE Test\\ExactInt\n"
             "FN identity 1 INT -> INT\n";
    }

    DiagnosticEngine Diagnostics;
    TypeChecker Checker(Diagnostics);
    Checker.add_module_path(dir.string());
    yona::parser::Parser Parser;
    auto Result = Parser.parseModule(R"(
module Test\ExactIntConsumer

export value

value = import identity from Test\ExactInt in identity "not an Int"
)",
                                     "exact_int_consumer.yona");
    REQUIRE(Result.has_value());

    Checker.check_module(Result.value().get());

    CHECK(Checker.has_errors());
    CHECK(Diagnostics.has_errors());

    std::error_code Error;
    fs::remove_all(dir, Error);
  }

  TEST_CASE("Module recursive-group inference preserves sibling polymorphism") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\SiblingPolymorphism

export both

id value = value
both = (id 1, id "two")
)",
                                     "sibling_polymorphism.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE("Module sibling forwarding preserves effect polymorphism") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_type = arena.make_con(TyCon::Int);
    auto *unit_type = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"get", {}, int_type}});
    checker.register_effect("Log", "", {{"log", {int_type}, unit_type}});

    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\SiblingEffectPolymorphism

export both

forward f x = apply f x
apply f x = f x
get value = perform State.get ()
log value = do perform Log.log value; value end
both = (forward get 0, forward log 1)
)",
                                     "sibling_effect_polymorphism.yona");
    REQUIRE(result.has_value());

    checker.check_module(result.value().get());

    CHECK_FALSE(checker.has_direct_errors());
    CHECK_FALSE(diag.has_errors());
  }

  TEST_CASE(
      "Module dependency SCCs include function values and respect binders") {
    yona::parser::Parser parser;
    auto result = parser.parseModule(R"(
module Test\LexicalFunctionDependencies

shadow target x = target x
target x = shadow identity x
saved = target
consumer x = saved x
)",
                                     "lexical_function_dependencies.yona");
    REQUIRE(result.has_value());

    const auto components = module_function_components(result.value().get());

    REQUIRE(components.size() == 4);
    for (const auto &component : components) {
      CHECK(component.functions.size() == 1);
      CHECK_FALSE(component.recursive);
    }
    CHECK(components[0].functions[0]->name == "shadow");
    CHECK(components[1].functions[0]->name == "target");
    CHECK(components[2].functions[0]->name == "saved");
    CHECK(components[3].functions[0]->name == "consumer");
  }

  TEST_CASE("Effect row: closed empty row is an effect-freedom fact") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = x + 1 in f");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    auto *t = checker.check(result->Expression.get());
    REQUIRE(t != nullptr);
    CHECK(checker.is_effect_free(t));
  }

  TEST_CASE("Effect row: recursive HOF still threads the parameter rest") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto &arena = checker.arena();
    auto *int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    yona::parser::Parser parser;
    std::istringstream stream(
        "let app f n = if n <= 0 then f n else app f (n - 1) in\n"
        "let g = (\\y -> perform State.get ()) in\n"
        "app g 3");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression != nullptr);
    checker.check(result->Expression.get());
    CHECK(checker.has_errors());
  }

  TEST_CASE("Error code: E0202 string and explanation") {
    CHECK(error_code_str(ErrorCode::E0202) == "E0202");
    CHECK(parse_error_code("E0202").value_or(ErrorCode::E0100) ==
          ErrorCode::E0202);
    CHECK(!error_explanation(ErrorCode::E0202).empty());
  }

  TEST_CASE("Error code: E0203 string and explanation") {
    CHECK(error_code_str(ErrorCode::E0203) == "E0203");
    CHECK(parse_error_code("E0203").value_or(ErrorCode::E0100) ==
          ErrorCode::E0203);
    CHECK(!error_explanation(ErrorCode::E0203).empty());
  }

  // ===== Error Code Tests =====

  TEST_CASE("Error code: E0100 string representation") {
    CHECK(error_code_str(ErrorCode::E0100) == "E0100");
    CHECK(error_code_str(ErrorCode::E0103) == "E0103");
    CHECK(error_code_str(ErrorCode::E0200) == "E0200");
    CHECK(error_code_str(ErrorCode::E0202) == "E0202");
  }

  TEST_CASE("Error code: E0100 explains constructor-pattern tuple fixes") {
    const auto explanation = error_explanation(ErrorCode::E0100);
    CHECK(explanation.find("constructor pattern") != std::string::npos);
    CHECK(explanation.find("Box ((value, _))") != std::string::npos);
    CHECK(explanation.find("declared field shape") != std::string::npos);
  }

  TEST_CASE("Error code: parse_error_code round-trips") {
    auto code = parse_error_code("E0100");
    REQUIRE(code.has_value());
    CHECK(*code == ErrorCode::E0100);
    auto e0202 = parse_error_code("E0202");
    REQUIRE(e0202.has_value());
    CHECK(*e0202 == ErrorCode::E0202);
    auto e0603 = parse_error_code("E0603");
    REQUIRE(e0603.has_value());
    CHECK(*e0603 == ErrorCode::E0603);
    CHECK(!parse_error_code("E9999").has_value());
    CHECK(!parse_error_code("INVALID").has_value());
  }

  TEST_CASE("Error code: explanations are non-empty") {
    CHECK(!error_explanation(ErrorCode::E0100).empty());
    CHECK(!error_explanation(ErrorCode::E0101).empty());
    CHECK(!error_explanation(ErrorCode::E0103).empty());
    CHECK(!error_explanation(ErrorCode::E0105).empty());
    CHECK(!error_explanation(ErrorCode::E0200).empty());
    CHECK(!error_explanation(ErrorCode::E0202).empty());
    CHECK(!error_explanation(ErrorCode::E0300).empty());
    CHECK(!error_explanation(ErrorCode::E0400).empty());
    CHECK(!error_explanation(ErrorCode::E0404).empty());
  }

  // ===== Refinement Checker Tests =====

} // close TypeChecker suite

#include "yona/Semantics/RefinementChecker.h"

using yona::compiler::typechecker::TypeChecker;
using yona::compiler::types::Type;

TEST_SUITE("RefinementChecker") {

  TEST_CASE("FactEnv: integer bound satisfies Gt predicate") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval::exact(42);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Gt, "x", 0);
    CHECK(facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: integer bound does not satisfy Gt when equal") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval::exact(0);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Gt, "x", 0);
    CHECK(!facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: non-empty satisfies LengthGt 0") {
    FactEnv facts;
    facts.non_empty_seqs.insert("xs");
    auto pred = RefinePredicate::make_length_gt("xs", 0);
    CHECK(facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: unknown seq does not satisfy LengthGt") {
    FactEnv facts;
    auto pred = RefinePredicate::make_length_gt("xs", 0);
    CHECK(!facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: And predicate requires both") {
    FactEnv facts;
    facts.int_bounds["n"] = Interval{1, 100};
    auto gt0 = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    auto lt65536 = RefinePredicate::make_cmp(RefinePredicate::Lt, "n", 65536);
    auto both = RefinePredicate::make_and(gt0, lt65536);
    CHECK(facts.satisfies(*both));
  }

  TEST_CASE("FactEnv: And fails when one side fails") {
    FactEnv facts;
    facts.int_bounds["n"] = Interval{-5, 100};
    auto gt0 = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    auto lt65536 = RefinePredicate::make_cmp(RefinePredicate::Lt, "n", 65536);
    auto both = RefinePredicate::make_and(gt0, lt65536);
    CHECK(!facts.satisfies(*both));
  }

  TEST_CASE("FactEnv: Ne predicate satisfied when value is excluded") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval{1, 100};
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Ne, "x", 0);
    CHECK(facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: with_int_bound intersects") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval{0, 100};
    auto narrowed = facts.with_int_bound("x", Interval::above(10));
    CHECK(narrowed.int_bounds["x"].lo == 10);
    CHECK(narrowed.int_bounds["x"].hi == 100);
  }

  TEST_CASE("RefinePredicate: to_string") {
    auto p = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    CHECK(p->to_string() == "n > 0");

    auto p2 = RefinePredicate::make_length_gt("xs", 0);
    CHECK(p2->to_string() == "length xs > 0");

    auto p3 = RefinePredicate::make_and(
        RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0),
        RefinePredicate::make_cmp(RefinePredicate::Lt, "n", 65536));
    CHECK(p3->to_string() == "n > 0 && n < 65536");
  }

  TEST_CASE("RefinementChecker: register and lookup") {
    DiagnosticEngine diag;
    RefinementChecker rc(diag);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    rc.register_refined_type("Positive", BuiltinType::SignedInt64, pred);

    auto *info = rc.lookup("Positive");
    REQUIRE(info != nullptr);
    CHECK(info->name == "Positive");
    CHECK(info->predicate->to_string() == "n > 0");
    CHECK(rc.lookup("Unknown") == nullptr);
  }

  // ===== Interval Arithmetic =====

  TEST_CASE("Interval: add shifts bounds") {
    auto i = Interval{5, 10};
    auto shifted = i.add(3);
    CHECK(shifted.lo == 8);
    CHECK(shifted.hi == 13);
  }

  TEST_CASE("Interval: sub shifts bounds") {
    auto i = Interval{5, 10};
    auto shifted = i.sub(2);
    CHECK(shifted.lo == 3);
    CHECK(shifted.hi == 8);
  }

  TEST_CASE("Interval: add preserves infinity") {
    auto i = Interval::above(5);
    auto shifted = i.add(3);
    CHECK(shifted.lo == 8);
    CHECK(shifted.hi == std::numeric_limits<int64_t>::max());
  }

  // ===== Excluded Values =====

  TEST_CASE("FactEnv: Ne satisfied via excluded_values") {
    FactEnv facts;
    facts.excluded_values["x"].insert(0);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Ne, "x", 0);
    CHECK(facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: Ne not satisfied when value not excluded") {
    FactEnv facts;
    facts.excluded_values["x"].insert(5);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Ne, "x", 0);
    CHECK(!facts.satisfies(*pred));
  }

  TEST_CASE("FactEnv: with_excluded adds value") {
    FactEnv facts;
    auto f2 = facts.with_excluded("n", 0);
    CHECK(f2.excluded_values["n"].count(0) == 1);
  }

  // ===== Var Substitution =====

  TEST_CASE("FactEnv: satisfies with var substitution") {
    FactEnv facts;
    facts.int_bounds["myVar"] = Interval{1, 100};
    // Predicate says "n > 0" but we check against "myVar"
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    CHECK(facts.satisfies(*pred, "myVar"));
    CHECK(!facts.satisfies(*pred, "unknownVar"));
  }

  // ===== Ge/Le Predicates =====

  TEST_CASE("FactEnv: Ge predicate") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval{0, 10};
    CHECK(facts.satisfies(
        *RefinePredicate::make_cmp(RefinePredicate::Ge, "x", 0)));
    CHECK(!facts.satisfies(
        *RefinePredicate::make_cmp(RefinePredicate::Ge, "x", 1)));
  }

  TEST_CASE("FactEnv: Le predicate") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval{0, 10};
    CHECK(facts.satisfies(
        *RefinePredicate::make_cmp(RefinePredicate::Le, "x", 10)));
    CHECK(!facts.satisfies(
        *RefinePredicate::make_cmp(RefinePredicate::Le, "x", 9)));
  }

  // ===== Or Predicate =====

  TEST_CASE("FactEnv: Or predicate satisfied if one side holds") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval::exact(0);
    auto eq0 = RefinePredicate::make_cmp(RefinePredicate::Eq, "x", 0);
    auto eq1 = RefinePredicate::make_cmp(RefinePredicate::Eq, "x", 1);
    auto either = RefinePredicate::make_or(eq0, eq1);
    CHECK(facts.satisfies(*either));
  }

  TEST_CASE("FactEnv: Or predicate fails if neither holds") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval::exact(5);
    auto eq0 = RefinePredicate::make_cmp(RefinePredicate::Eq, "x", 0);
    auto eq1 = RefinePredicate::make_cmp(RefinePredicate::Eq, "x", 1);
    auto either = RefinePredicate::make_or(eq0, eq1);
    CHECK(!facts.satisfies(*either));
  }

  // ===== Built-in Refined Functions =====

  TEST_CASE("RefinementChecker: head on non-empty seq is OK") {
    // let xs = [1, 2] in head xs
    // Should not produce an error â€” xs is provably non-empty
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let xs = [1, 2] in head xs");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE("RefinementChecker: head on unknown seq warns") {
    // let xs = someFunc 42 in head xs
    // Cannot prove xs is non-empty
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = x in let xs = f 42 in head xs");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(rc.has_errors());
  }

  TEST_CASE("RefinementChecker: head after [h|t] pattern is OK") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream(
        "let xs = [1, 2] in case xs of [h|t] -> head xs; [] -> 0 end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE("RefinementChecker: cons proves non-empty") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let xs = 1 :: [] in head xs");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE("RefinementChecker: division by literal non-zero is OK") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let d = 5 in 100 / d");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE("RefinementChecker: division by unknown warns") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = x in let d = f 0 in 100 / d");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(rc.has_errors());
  }

  TEST_CASE("RefinementChecker: division by zero literal warns") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let d = 0 in 100 / d");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(rc.has_errors());
  }

  TEST_CASE("RefinementChecker: wildcard after 0 excludes zero") {
    // case n of 0 -> ...; x -> x / ... should know x != 0
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream(
        "let n = 5 in case n of 0 -> 0; x -> 100 / x end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE("RefinementChecker: arithmetic propagation add") {
    // let x = 5 in let y = x + 1 â†’ y is in [6, 6]
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let x = 5 in let y = x + 1 in 100 / y");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    rc.check(result->Expression.get());
    CHECK(!rc.has_errors());
  }

  TEST_CASE(
      "RefinementChecker: discarded Option in do warns (-Wunmatched-adt)") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}},
                    {{yona::ast::FieldType::simple("a")}, {}});

    std::istringstream stream("let f x = Some x in do f 1; () end");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result->Expression.get());
    CHECK(diag.warning_count() >= 1);
  }

  TEST_CASE("RefinementChecker: let _ = Option warns") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}},
                    {{yona::ast::FieldType::simple("a")}, {}});

    std::istringstream stream("let f x = Some x in let _ = f 1 in ()");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result->Expression.get());
    CHECK(diag.warning_count() >= 1);
  }

  TEST_CASE("RefinementChecker: module function head on unknown seq is E0500") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    RefinementChecker rc(diag, &tc);

    yona::parser::Parser parser;
    auto result = parser.parseModule(
        "module Test\\RefineMod\n\nexport bad\n\nbad xs = head xs\n",
        "RefineMod.yona");
    REQUIRE(result.has_value());
    tc.check_module(result.value().get());
    rc.check(result.value().get());
    CHECK(rc.has_errors());
    bool saw_e0500 = false;
    for (auto &rec : diag.records()) {
      if (rec.code && *rec.code == ErrorCode::E0500)
        saw_e0500 = true;
    }
    CHECK(saw_e0500);
  }

  TEST_CASE("RefinementChecker: let r = Option does not warn unmatched-adt") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}},
                    {{yona::ast::FieldType::simple("a")}, {}});

    std::istringstream stream("let f x = Some x in let r = f 1 in ()");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result->Expression.get());
    CHECK(diag.warning_count() == 0);
  }

} // RefinementChecker

// ===== Linearity Checker Tests =====

#include "Support/RepoPaths.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/LinearityChecker.h"

#include <filesystem>

TEST_SUITE("LinearityChecker") {

  TEST_CASE("LinearEnv: create and consume") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("conn", SourceRange::unknown());
    CHECK(env.is_live("conn"));
    CHECK(!env.is_consumed("conn"));

    CHECK(env.consume("conn", SourceRange::unknown()));
    CHECK(!env.is_live("conn"));
    CHECK(env.is_consumed("conn"));
  }

  TEST_CASE("LinearEnv: double consume returns false") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("conn", SourceRange::unknown());
    CHECK(env.consume("conn", SourceRange::unknown()));
    CHECK(!env.consume("conn", SourceRange::unknown())); // already consumed
  }

  TEST_CASE("LinearEnv: live_vars lists unclosed") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("a", SourceRange::unknown());
    env.create("b", SourceRange::unknown());
    env.consume("a", SourceRange::unknown());
    auto live = env.live_vars();
    CHECK(live.size() == 1);
    CHECK(live[0] == "b");
  }

  TEST_CASE("LinearEnv: untracked variable is not tracked") {
    yona::compiler::typechecker::LinearEnv env;
    CHECK(!env.is_tracked("x"));
    CHECK(!env.is_live("x"));
    CHECK(!env.is_consumed("x"));
  }

  static void register_linear_adt(yona::compiler::typechecker::TypeChecker &
                                  tc) {
    tc.register_adt("Linear", {"a"}, {{"Linear", 1}},
                    {{yona::ast::FieldType::simple("a")}});
  }

  static bool diag_has_code(const yona::compiler::DiagnosticEngine &diag,
                            yona::compiler::ErrorCode code) {
    for (auto &rec : diag.records()) {
      if (rec.code && *rec.code == code)
        return true;
    }
    return false;
  }

  TEST_CASE("LinearityChecker: Linear constructor creates obligation") {
    // let conn = Linear 0 in conn â€” constructor path (no TypeChecker)
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let conn = Linear 0 in conn");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
    CHECK(diag_has_code(diag, yona::compiler::ErrorCode::E0602));
  }

  TEST_CASE("LinearityChecker: user-defined Linear-returning function creates "
            "obligation") {
    // Non-stdlib producer: makeHandle : a -> Linear a
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    std::istringstream stream(
        "let makeHandle x = Linear x, h = makeHandle 0 in h");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: tuple of Linear from user function is tracked") {
    // channel-shaped producer without a C++ name allowlist
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    std::istringstream stream(
        "let mkCh n = (Linear n, Linear n), (a, b) = mkCh 16 in 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: non-producer function no warning") {
    // let x = someFunc 42 in x â€” not a producer, no warning
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let x = length [1, 2, 3] in x");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(!lc.has_errors());
    CHECK(diag.warning_count() == 0);
  }

  TEST_CASE("LinearityChecker: use after consume is error") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    std::istringstream stream("let makeHandle x = Linear x, conn = makeHandle "
                              "0, conn2 = conn, conn3 = conn in conn3");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(lc.has_errors());
  }

  TEST_CASE("LinearityChecker: transfer via alias is OK") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    std::istringstream stream("let makeHandle x = Linear x, conn = makeHandle "
                              "0, conn2 = conn in conn2");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(!lc.has_errors());         // no error (transfer is OK)
    CHECK(diag.warning_count() > 0); // warning: conn2 unconsumed
  }

  static bool imported_linear_leaks(const std::string &source) {
    using yona::compiler::codegen::Codegen;
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    Codegen codegen("lin_import");
    if (std::filesystem::exists(yona::test::lib_dir()))
      codegen.ModulePaths.push_back(
          std::filesystem::canonical(yona::test::lib_dir()).string());
    YONA_TEST_INSTALL_PRELUDE(codegen, parser, tc);
    std::istringstream stream(source);
    auto result = parser.parseExpression(stream.str(), "<stream>");
    if (!result || !result->Expression)
      return false;
    tc.check(result->Expression.get());
    if (tc.has_direct_errors())
      return false;
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);
    lc.check(result->Expression.get());
    return diag.warning_count() > 0;
  }

  TEST_CASE("LinearityChecker: imported openFile creates obligation") {
    CHECK(imported_linear_leaks(
        "import openFile from Std\\File in let h = openFile \"f\" Read in h"));
  }

  TEST_CASE("TypeChecker: imported Linear return preserves its ADT payload") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_linear_payload";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Resource.yonai");
      out << "MODULE Test\\Resource\n"
             "ADT FileHandle 1 0 opaque\n"
             "FN open 1 UNIT -> LINEAR(ADT(FileHandle))\n";
    }
    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        "import open from Test\\Resource in open ()", "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    yona::compiler::codegen::Codegen codegen("linear_payload");
    codegen.ModulePaths.push_back(dir.string());
    auto *ty = checker.check(parsed.value().get());
    REQUIRE(ty != nullptr);
    ty = checker.zonk(ty);
    REQUIRE(ty->tag == MonoType::App);
    REQUIRE(ty->type_name == "Linear");
    REQUIRE(ty->args.size() == 1);
    CHECK(ty->args[0]->tag == MonoType::App);
    CHECK(ty->args[0]->type_name == "FileHandle");
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  static std::vector<DiagnosticEngine::Record> concurrency_diagnostics(
      const std::string &source) {
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    yona::parser::Parser parser;
    yona::compiler::codegen::Codegen bootstrap("concurrency_markers");
    bootstrap.ModulePaths.push_back(yona::test::lib_dir().string());
    YONA_TEST_INSTALL_PRELUDE(bootstrap, parser, checker);
    std::istringstream input(source);
    auto parsed = parser.parseExpression(input.str(), "<stream>");
    if (!parsed || !parsed->Expression)
      return diagnostics.records();
    checker.check(parsed->Expression.get());
    checker.solve_constraints();
    return diagnostics.records();
  }

  TEST_CASE("Send and Shareable accept immutable spawn captures and results") {
    const auto records = concurrency_diagnostics(R"(
import spawn from Std\Task in
let message = "immutable" in spawn (\_ -> if message == "immutable" then 1 else 0)
)");
    CHECK(std::none_of(records.begin(), records.end(), [](const auto &record) {
      return record.level == DiagLevel::Error;
    }));
  }

  TEST_CASE("Spawn rejects mutable native captures with actionable marker "
            "diagnostics") {
    const auto records = concurrency_diagnostics(R"(
import spawn from Std\Task, alloc from Std\ByteArray in
let bytes = alloc 4 in spawn (\_ -> bytes)
)");
    CHECK(std::count_if(records.begin(), records.end(), [](const auto &record) {
            return record.level == DiagLevel::Error &&
                   (record.message.find("Shareable ByteArray") !=
                        std::string::npos ||
                    record.message.find("Send ByteArray") != std::string::npos);
          }) >= 1);
    CHECK(std::any_of(records.begin(), records.end(), [](const auto &record) {
      return record.level == DiagLevel::Note &&
             record.message.find("mutable native arrays require a snapshot") !=
                 std::string::npos;
    }));
  }

  TEST_CASE("Parallel comprehensions reject mutable captures and results") {
    const auto records = concurrency_diagnostics(R"(
import alloc from Std\ByteArray in
let bytes = alloc 4 in [| bytes for value = [1, 2] ]
)");
    CHECK(std::any_of(records.begin(), records.end(), [](const auto &record) {
      return record.level == DiagLevel::Error &&
             (record.message.find("Shareable ByteArray") != std::string::npos ||
              record.message.find("Send ByteArray") != std::string::npos);
    }));
  }

  TEST_CASE("Channel send requires Send for its payload") {
    const auto accepted = concurrency_diagnostics(R"(
import channel, send from Std\Channel in case channel 1 of
    (Linear sender, Linear receiver) -> send sender "immutable"
end
)");
    CHECK(
        std::none_of(accepted.begin(), accepted.end(), [](const auto &record) {
          return record.level == DiagLevel::Error;
        }));

    const auto moved_array = concurrency_diagnostics(R"(
import channel, send from Std\Channel, alloc from Std\ByteArray in
case channel 1 of
    (Linear sender, Linear receiver) -> send sender (alloc 4)
end
)");
    CHECK(std::none_of(
        moved_array.begin(), moved_array.end(),
        [](const auto &record) { return record.level == DiagLevel::Error; }));

    const auto rejected = concurrency_diagnostics(R"(
import channel, send from Std\Channel in
case channel 1 of
    (Linear sender, Linear receiver) -> send sender (\value -> value)
end
)");
    CHECK(std::any_of(rejected.begin(), rejected.end(), [](const auto &record) {
      return record.level == DiagLevel::Error &&
             record.message.find("Send") != std::string::npos &&
             record.message.find("->") != std::string::npos;
    }));
  }

  TEST_CASE("Marker derivation is structural and emits no methods") {
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    yona::parser::Parser parser;
    yona::compiler::codegen::Codegen bootstrap("marker_derive");
    bootstrap.ModulePaths.push_back(yona::test::lib_dir().string());
    YONA_TEST_INSTALL_PRELUDE(bootstrap, parser, checker);
    auto parsed = parser.parseModule(R"(
module Test\MarkerDerive
type Packet value = Packet value deriving Send, Shareable
)",
                                     "marker_derive.yona");
    REQUIRE(parsed.has_value());
    checker.check_module(parsed.value().get());
    CHECK(!diagnostics.has_errors());
  }

  TEST_CASE("Shareable derivation rejects mutable native fields") {
    DiagnosticEngine diagnostics;
    TypeChecker checker(diagnostics);
    yona::parser::Parser parser;
    yona::compiler::codegen::Codegen bootstrap("marker_reject");
    bootstrap.ModulePaths.push_back(yona::test::lib_dir().string());
    YONA_TEST_INSTALL_PRELUDE(bootstrap, parser, checker);
    auto parsed = parser.parseModule(R"(
module Test\MarkerReject
type MutablePacket = MutablePacket ByteArray deriving Send, Shareable
)",
                                     "marker_reject.yona");
    REQUIRE(parsed.has_value());
    checker.check_module(parsed.value().get());
    CHECK(std::any_of(diagnostics.records().begin(),
                      diagnostics.records().end(), [](const auto &record) {
                        return record.level == DiagLevel::Error &&
                               record.message.find("cannot derive Shareable") !=
                                   std::string::npos;
                      }));
  }

  TEST_CASE("TypeChecker: imported Linear payloads remain distinct") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_linear_payload_distinct";
    fs::create_directories(dir / "Test");
    {
      std::ofstream out(dir / "Test" / "Resource.yonai");
      out << "MODULE Test\\Resource\n"
             "ADT FileHandle 1 0 opaque\n"
             "ADT Process 1 0 opaque\n"
             "FN open 1 UNIT -> LINEAR(ADT(FileHandle))\n"
             "FN needProcess 1 LINEAR(ADT(Process)) -> "
             "UNIT\n";
    }
    yona::parser::Parser parser;
    auto parsed = parser.parseExpression(
        "import open, needProcess from Test\\Resource in needProcess (open ())",
        "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    yona::compiler::codegen::Codegen codegen("linear_payload_distinct");
    codegen.ModulePaths.push_back(dir.string());
    checker.check(parsed.value().get());
    CHECK(diag.error_count() > 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  TEST_CASE("LinearityChecker: wildcard openFile creates obligation") {
    CHECK(imported_linear_leaks(
        "import Std\\File in let h = openFile \"f\" Read in h"));
  }

  TEST_CASE("LinearityChecker: imported channel tuple is tracked") {
    CHECK(imported_linear_leaks(
        "import channel from Std\\Channel in let (a, b) = channel 16 in 0"));
  }

  TEST_CASE(
      "LinearityChecker: nested cases consume imported channel endpoints") {
    CHECK(!imported_linear_leaks(R"(
import channel from Std\Channel in
let (senderLinear, receiverLinear) = channel 16 in
case senderLinear of Linear sender ->
case receiverLinear of Linear receiver -> 0
end end
)"));
  }

  TEST_CASE(
      "LinearityChecker: nested consumption must agree across case branches") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::LinearityChecker checker(diag);
    yona::parser::Parser parser;
    std::istringstream stream(R"(
let resource = Linear 0 in
case True of
    True -> case resource of Linear value -> 1 end
    False -> 0
end
)");
    auto parsed = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(parsed);
    REQUIRE(parsed->Expression);

    checker.check(parsed->Expression.get());
    CHECK(diag_has_code(diag, yona::compiler::ErrorCode::E0601));
  }

  TEST_CASE("LinearityChecker: imported non-linear File function has no leak") {
    CHECK(
        !imported_linear_leaks("import exists from Std\\File in exists \"f\""));
  }

  TEST_CASE("LinearityChecker: extern Linear return creates obligation") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    std::istringstream stream("extern mk : Int -> Linear in let h = mk 0 in h");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: with binds Linear and discharges at exit") {
    // `with` is the Closeable cleanup path: track the binding, then consume it
    // when the with scope ends (no leak warning for the resource name).
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("with h = Linear 0 in 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(!lc.has_errors());
    CHECK(diag.warning_count() == 0);
  }

  TEST_CASE("LinearityChecker: with body unconsumed Linear warns") {
    // Walking the with body is required to see the inner leak.
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("with h = Linear 0 in let x = Linear 1 in 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: with use-after-consume in body is error") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    // Transfer then re-bind the same Linear â€” second use is E0600.
    std::istringstream stream("with h = Linear 0 in let a = h, b = h in 0");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    lc.check(result->Expression.get());
    CHECK(lc.has_errors());
  }

  TEST_CASE("LinearityChecker: FunctionExpr body unconsumed Linear warns") {
    // Nested lambda / local fn must be walked; otherwise this stays silent.
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f = \\_ -> let h = Linear 0 in 0 in f");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: named local fn body unconsumed Linear warns") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f _ = let h = Linear 0 in 0 in f");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: Linear function parameter must be consumed") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::LinearLeak);
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    // Direct lambda application keeps the param monomorphic (Linear), so the
    // FunctionExpr arrow type is Linear -> _ after typecheck.
    yona::parser::Parser parser;
    std::istringstream stream("(\\h -> 0) (Linear 0)");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result->Expression.get());
    CHECK(diag.warning_count() > 0);
  }

  TEST_CASE("LinearityChecker: module function use-after-consume is E0600") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    register_linear_adt(tc);
    yona::compiler::typechecker::LinearityChecker lc(diag, &tc);

    yona::parser::Parser parser;
    auto result =
        parser.parseModule("module Test\\LinMod\n\nexport bad\n\n"
                           "bad x =\n"
                           "  let makeHandle y = Linear y, conn = makeHandle "
                           "x, conn2 = conn, conn3 = conn in conn3\n",
                           "LinMod.yona");
    REQUIRE(result.has_value());
    tc.check_module(result.value().get());
    REQUIRE(!tc.has_direct_errors());
    lc.check(result.value().get());
    CHECK(lc.has_errors());
    CHECK(diag_has_code(diag, yona::compiler::ErrorCode::E0600));
  }

  TEST_CASE("Error code: E0600/E0601/E0602/E0603 strings") {
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0600) ==
          "E0600");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0601) ==
          "E0601");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0602) ==
          "E0602");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0603) ==
          "E0603");
  }

  TEST_CASE("Error code: E0600 explanation is non-empty") {
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0600)
               .empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0601)
               .empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0602)
               .empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0603)
               .empty());
  }

} // LinearityChecker

TEST_SUITE("BorrowParam") {

  TEST_CASE("@borrow rejected when parameter is returned") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream("let f @borrow x = x in f 1");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    CHECK(tc.has_direct_errors());
  }

  TEST_CASE("@borrow accepted when parameter is only read") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream("let f @borrow x = x + 1 in f 2");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    CHECK(!tc.has_direct_errors());
  }

  TEST_CASE("consecutive @borrow parameters remain distinct identifiers") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream(
        "let same @borrow left @borrow right = left == right in same 1 1");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    CHECK(!tc.has_direct_errors());
  }

  TEST_CASE("@borrow rejected on non-identifier pattern") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream(
        R"(let g = \ @borrow (a, b) -> a + b in g (1, 2))");
    auto result = parser.parseExpression(stream.str(), "<stream>");
    REQUIRE(result);
    REQUIRE(result->Expression);
    tc.check(result->Expression.get());
    CHECK(tc.has_direct_errors());
  }

} // BorrowParam
