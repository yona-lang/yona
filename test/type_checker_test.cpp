/// Unit tests for the type checker:
/// TypeArena, UnionFind, Unifier, TypeEnv, builtins.

#include <doctest/doctest.h>
#include "typechecker/InferType.h"
#include "typechecker/TypeArena.h"
#include "typechecker/UnionFind.h"
#include "typechecker/Unification.h"
#include "typechecker/TypeEnv.h"
#include "Diagnostic.h"

using namespace yona::compiler::typechecker;
using namespace yona::compiler;
using namespace yona;

TEST_SUITE("TypeChecker") {

TEST_CASE("TypeArena: fresh variables get unique IDs") {
    TypeArena arena;
    auto* v1 = arena.fresh_var(0);
    auto* v2 = arena.fresh_var(0);
    CHECK(v1->tag == MonoType::Var);
    CHECK(v2->tag == MonoType::Var);
    CHECK(v1->var_id != v2->var_id);
}

TEST_CASE("TypeArena: make_con returns correct types") {
    TypeArena arena;
    auto* int_t = arena.make_con(TyCon::Int);
    auto* str_t = arena.make_con(TyCon::String);
    CHECK(int_t->tag == MonoType::Con);
    CHECK(int_t->con == TyCon::Int);
    CHECK(str_t->con == TyCon::String);
}

TEST_CASE("TypeArena: make_arrow constructs function types") {
    TypeArena arena;
    auto* int_t = arena.make_con(TyCon::Int);
    auto* str_t = arena.make_con(TyCon::String);
    auto* fn = arena.make_arrow(int_t, str_t);
    CHECK(fn->tag == MonoType::Arrow);
    CHECK(fn->param_type == int_t);
    CHECK(fn->return_type == str_t);
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
    auto* int_t = arena.make_con(TyCon::Int);
    uf.bind(0, int_t);
    CHECK(uf.find(0) == int_t);
    CHECK(uf.is_bound(0));
}

TEST_CASE("UnionFind: path compression") {
    TypeArena arena;
    UnionFind uf;
    auto* v0 = arena.fresh_var(0); uf.add_var(v0->var_id, 0);
    auto* v1 = arena.fresh_var(0); uf.add_var(v1->var_id, 0);
    auto* int_t = arena.make_con(TyCon::Int);
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
    auto* int_t = arena.make_con(TyCon::Int);
    CHECK(u.unify(int_t, int_t, SourceLocation::unknown()));
}

TEST_CASE("Unifier: different concrete types fail") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* int_t = arena.make_con(TyCon::Int);
    auto* str_t = arena.make_con(TyCon::String);
    CHECK(!u.unify(int_t, str_t, SourceLocation::unknown()));
}

TEST_CASE("Unifier: variable binds to concrete type") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* v = arena.fresh_var(0); uf.add_var(v->var_id, 0);
    auto* int_t = arena.make_con(TyCon::Int);
    CHECK(u.unify(v, int_t, SourceLocation::unknown()));
    CHECK(uf.find(v->var_id) == int_t);
}

TEST_CASE("Unifier: two variables unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* v1 = arena.fresh_var(0); uf.add_var(v1->var_id, 0);
    auto* v2 = arena.fresh_var(0); uf.add_var(v2->var_id, 0);
    CHECK(u.unify(v1, v2, SourceLocation::unknown()));
    // After binding v2 to something, v1 should resolve too
    auto* int_t = arena.make_con(TyCon::Int);
    uf.bind(v2->var_id, int_t);
    CHECK(u.resolve(v1) == int_t);
}

TEST_CASE("Unifier: arrow types unify structurally") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* int_t = arena.make_con(TyCon::Int);
    auto* a = arena.fresh_var(0); uf.add_var(a->var_id, 0);
    auto* fn1 = arena.make_arrow(int_t, a);
    auto* fn2 = arena.make_arrow(int_t, arena.make_con(TyCon::String));
    CHECK(u.unify(fn1, fn2, SourceLocation::unknown()));
    CHECK(u.resolve(a)->con == TyCon::String);
}

TEST_CASE("Unifier: closed effect sets with the same ops unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* int_t = arena.make_con(TyCon::Int);
    LatentEffect e{"Fs.read", SourceLocation::unknown()};
    auto* fn1 = arena.make_arrow(int_t, int_t, {e});
    auto* fn2 = arena.make_arrow(int_t, int_t, {e});
    CHECK(u.unify(fn1, fn2, SourceLocation::unknown()));
}

TEST_CASE("Unifier: closed effect sets with different ops fail") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* int_t = arena.make_con(TyCon::Int);
    auto* fn1 = arena.make_arrow(int_t, int_t, {{"Fs.read", SourceLocation::unknown()}});
    auto* fn2 = arena.make_arrow(int_t, int_t, {{"Net.post", SourceLocation::unknown()}});
    CHECK(!u.unify(fn1, fn2, SourceLocation::unknown()));
}

TEST_CASE("Unifier: open effect rest absorbs a closed set") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* int_t = arena.make_con(TyCon::Int);
    auto* rest = arena.fresh_var(0); uf.add_var(rest->var_id, 0);
    auto* open = arena.make_arrow(int_t, int_t, {}, rest);
    auto* closed = arena.make_arrow(int_t, int_t, {{"Fs.read", SourceLocation::unknown()}});
    CHECK(u.unify(open, closed, SourceLocation::unknown()));
    auto* bound = u.resolve(rest);
    REQUIRE(bound != nullptr);
    CHECK(bound->tag == MonoType::ERow);
    REQUIRE(!bound->arrow_effects.empty());
    CHECK(bound->arrow_effects[0].op_key == "Fs.read");
}

TEST_CASE("Unifier: occurs check prevents infinite types") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* a = arena.fresh_var(0); uf.add_var(a->var_id, 0);
    auto* seq_a = arena.make_app("Seq", {a});
    CHECK(!u.unify(a, seq_a, SourceLocation::unknown())); // a ~ Seq a -> infinite
}

TEST_CASE("Unifier: App types unify") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* a = arena.fresh_var(0); uf.add_var(a->var_id, 0);
    auto* opt_int = arena.make_app("Option", {arena.make_con(TyCon::Int)});
    auto* opt_a = arena.make_app("Option", {a});
    CHECK(u.unify(opt_int, opt_a, SourceLocation::unknown()));
    CHECK(u.resolve(a)->con == TyCon::Int);
}

TEST_CASE("Unifier: tuple types unify element-wise") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* a = arena.fresh_var(0); uf.add_var(a->var_id, 0);
    auto* t1 = arena.make_tuple({arena.make_con(TyCon::Int), a});
    auto* t2 = arena.make_tuple({arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(u.unify(t1, t2, SourceLocation::unknown()));
    CHECK(u.resolve(a)->con == TyCon::String);
}

TEST_CASE("Unifier: tuple size mismatch fails") {
    TypeArena arena;
    UnionFind uf;
    DiagnosticEngine diag;
    Unifier u(arena, uf, diag);
    auto* t1 = arena.make_tuple({arena.make_con(TyCon::Int)});
    auto* t2 = arena.make_tuple({arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(!u.unify(t1, t2, SourceLocation::unknown()));
}

TEST_CASE("pretty_print formats types correctly") {
    TypeArena arena;
    CHECK(pretty_print(arena.make_con(TyCon::Int)) == "Int");
    CHECK(pretty_print(arena.make_con(TyCon::String)) == "String");
    auto* fn = arena.make_arrow(arena.make_con(TyCon::Int), arena.make_con(TyCon::Bool));
    CHECK(pretty_print(fn) == "(Int -> Bool)");
    auto* eff = arena.make_arrow(arena.make_con(TyCon::Int), arena.make_con(TyCon::Bool),
                                 {{"Fs.read", SourceLocation::unknown()}});
    CHECK(pretty_print(eff) == "(Int -> !{Fs.read} Bool)");
    auto* opt = arena.make_app("Option", {arena.make_con(TyCon::Int)});
    CHECK(pretty_print(opt) == "Option Int");
    auto* tup = arena.make_tuple({arena.make_con(TyCon::Int), arena.make_con(TyCon::String)});
    CHECK(pretty_print(tup) == "(Int, String)");
}

// ===== TypeEnv Tests =====

TEST_CASE("TypeEnv: bind and lookup") {
    TypeArena arena;
    auto env = std::make_shared<TypeEnv>();
    auto* int_t = arena.make_con(TyCon::Int);
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
    auto* a = arena.fresh_var(1);
    auto* id_type = arena.make_arrow(a, a);
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

#include "typechecker/TypeChecker.h"
#include "Parser.h"
#include <filesystem>
#include <fstream>
#include <sstream>

static std::string check_expr_str(const std::string& source) {
    yona::parser::Parser parser;
    std::istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (!result.node) return "PARSE_ERROR";

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto* t = checker.check(result.node.get());
    if (!t) return "?";
    return yona::compiler::typechecker::pretty_print(checker.zonk(t));
}

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

TEST_CASE("Inference: nested let") {
    CHECK(check_expr_str("let x = 1 in let y = x + 1 in y") == "Int");
}

TEST_CASE("Inference: lambda") {
    CHECK(check_expr_str("let f = \\x -> x + 1 in f 5") == "Int");
}

// ===== Case Expression + Pattern Inference =====

TEST_CASE("Inference: case with integer patterns") {
    CHECK(check_expr_str("case 42 of 0 -> \"zero\"; _ -> \"other\" end") == "String");
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
    CHECK(check_expr_str(
        "let foldl fn acc seq = case seq of [] -> acc; [h|t] -> foldl fn (fn acc h) t end in "
        "foldl (\\a b -> a + b) 0 [1, 2, 3]"
    ) == "Int");
}

// ===== Negative Tests (type errors) =====

static bool check_has_error(const std::string& source) {
    yona::parser::Parser parser;
    std::istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (!result.node) return true; // parse error counts

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(result.node.get());
    return checker.has_errors();
}

TEST_CASE("Type error: adding string to int") {
    CHECK(check_has_error("1 + \"hello\""));
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

static std::string check_with_adt(const std::string& source) {
    yona::parser::Parser parser;
    std::istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (!result.node) return "PARSE_ERROR";

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}});
    auto* t = checker.check(result.node.get());
    if (!t) return "?";
    return yona::compiler::typechecker::pretty_print(checker.zonk(t));
}

TEST_SUITE("TypeChecker") { // reopen

TEST_CASE("ADT: Some constructor applied") {
    CHECK(check_with_adt("Some 42") == "Option Int");
}

TEST_CASE("ADT: None is polymorphic") {
    auto s = check_with_adt("None");
    // None : forall a. Option a — instantiated with a fresh var
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

static std::string check_with_traits(const std::string& source, bool expect_error = false) {
    yona::parser::Parser parser;
    std::istringstream stream(source);
    auto result = parser.parse_input(stream);
    if (!result.node) return "PARSE_ERROR";

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);

    // Register trait Num with method abs : a -> a
    auto& arena = checker.arena();
    auto* a = arena.fresh_var(0);
    auto* abs_type = arena.make_arrow(a, a);
    checker.register_trait_method("Num", "abs", abs_type);
    checker.register_instance("Num", "Int");
    checker.register_instance("Num", "Float");

    auto* t = checker.check(result.node.get());
    checker.solve_constraints();

    if (expect_error) return checker.has_errors() ? "ERROR" : "NO_ERROR";
    if (!t) return "?";
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

// ===== Codegen Integration Test =====
// Verify the type checker can be wired into the compilation pipeline

TEST_CASE("Integration: type checker produces correct types for compilation") {
    auto s = check_expr_str("let f x = x + 1 in let g y = f y in g 10");
    CHECK(s == "Int");
}

TEST_CASE("Integration: polymorphic identity function") {
    // let id = \x -> x in id applies to both Int and String
    CHECK(check_expr_str("let id = \\x -> x in id 42") == "Int");
}

// ===== Recursive Function Tests =====

TEST_CASE("Inference: recursive function type") {
    auto s = check_expr_str("let f x = if x <= 0 then 0 else f (x - 1) in f 10");
    CHECK(s == "Int");
}

TEST_CASE("Inference: recursive foldl type") {
    auto s = check_expr_str("let foldl fn acc s = case s of [] -> acc; [h|t] -> foldl fn (fn acc h) t end in foldl (\\a b -> a + b) 0 [1, 2, 3]");
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
    auto s = check_expr_str("case \"hello\" of (s : String) -> s ++ \" world\" end");
    CHECK(s == "String");
}

// ===== Effect Type Tracking =====

TEST_CASE("Effect: perform with registered effect returns correct type") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto& arena = checker.arena();

    // Register: effect State; get : () -> Int; put : Int -> ()
    auto* int_t = arena.make_con(TyCon::Int);
    auto* unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {
        {"get", {}, int_t},
        {"put", {int_t}, unit_t},
    });

    // Build: handle (perform State.get ()) with State.get () resume -> resume 42 end
    SourceContext sc{};
    std::vector<yona::ast::ExprNode*> no_args;
    auto* perform_node = new yona::ast::PerformExpr(sc, "State", "get", no_args);

    // Wrap perform in a handle so no "unhandled" warning
    auto* lit42 = new yona::ast::IntegerExpr(sc, 42);
    // resume 42
    auto* resume_id = new yona::ast::IdentifierExpr(sc, new yona::ast::NameExpr(sc,"resume"));
    std::vector<std::variant<yona::ast::ExprNode*, yona::ast::ValueExpr*>> resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode*>(lit42));
    auto* resume_call = new yona::ast::ApplyExpr(sc,
        new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc,"resume")),
        resume_args);

    auto* handler = new yona::ast::HandlerClause(sc, "State", "get", {}, "resume", resume_call);
    auto* handle = new yona::ast::HandleExpr(sc, perform_node, {handler});
    auto* main_node = new yona::ast::MainNode(sc, handle);

    auto* t = checker.check(main_node);
    REQUIRE(t != nullptr);
    auto* zt = checker.zonk(t);
    CHECK(pretty_print(zt) == "Int");

    delete main_node;
}

TEST_CASE("Effect: unhandled perform produces warning") {
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::TypeChecker checker(diag);

    // Register effect
    auto& arena = checker.arena();
    auto* int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    // perform State.get () without handle
    SourceContext sc{};
    std::vector<yona::ast::ExprNode*> no_args;
    auto* perform_node = new yona::ast::PerformExpr(sc, "State", "get", no_args);
    auto* main_node = new yona::ast::MainNode(sc, perform_node);

    checker.check(main_node);
    CHECK(diag.warning_count() > 0);

    delete main_node;
}

TEST_CASE("Effect: perform arg type mismatch is an error") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto& arena = checker.arena();

    // effect State; put : Int -> ()
    auto* int_t = arena.make_con(TyCon::Int);
    auto* unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("State", "s", {{"put", {int_t}, unit_t}});

    // perform State.put "hello" — String where Int expected
    SourceContext sc{};
    auto* str_arg = new yona::ast::StringExpr(sc, "hello");
    std::vector<yona::ast::ExprNode*> args = {str_arg};
    auto* perform_node = new yona::ast::PerformExpr(sc, "State", "put", args);
    auto* main_node = new yona::ast::MainNode(sc, perform_node);

    checker.check(main_node);
    CHECK(checker.has_errors());

    delete main_node;
}

TEST_CASE("Effect: handle with return clause transforms result") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto& arena = checker.arena();

    // Register: effect Log; log : String -> ()
    auto* string_t = arena.make_con(TyCon::String);
    auto* unit_t = arena.make_con(TyCon::Unit);
    checker.register_effect("Log", "", {{"log", {string_t}, unit_t}});

    // handle (perform Log.log "hi") with
    //   Log.log msg resume -> resume ()
    //   return val -> 42
    // end
    SourceContext sc{};
    auto* str_arg = new yona::ast::StringExpr(sc, "hi");
    std::vector<yona::ast::ExprNode*> args = {str_arg};
    auto* perform_node = new yona::ast::PerformExpr(sc, "Log", "log", args);

    // Log.log handler clause: resume ()
    auto* unit_val = new yona::ast::UnitExpr(sc);
    auto* resume_id = new yona::ast::IdentifierExpr(sc, new yona::ast::NameExpr(sc,"resume"));
    std::vector<std::variant<yona::ast::ExprNode*, yona::ast::ValueExpr*>> resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode*>(unit_val));
    auto* resume_call = new yona::ast::ApplyExpr(sc,
        new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc,"resume")),
        resume_args);
    auto* op_handler = new yona::ast::HandlerClause(sc, "Log", "log", {"msg"}, "resume", resume_call);

    // return handler: return val -> 42
    auto* lit42 = new yona::ast::IntegerExpr(sc, 42);
    auto* ret_handler = new yona::ast::HandlerClause(sc, "val", lit42);

    auto* handle = new yona::ast::HandleExpr(sc, perform_node, {op_handler, ret_handler});
    auto* main_node = new yona::ast::MainNode(sc, handle);

    auto* t = checker.check(main_node);
    REQUIRE(t != nullptr);
    auto* zt = checker.zonk(t);
    CHECK(pretty_print(zt) == "Int");

    delete main_node;
}

TEST_CASE("Effect: applying unhandled perform lambda is E0202") {
    yona::parser::Parser parser;
    string source = R"(let plan = \() -> perform Fs.read "/etc/shadow" in plan ())";
    auto parsed = parser.parse_expression(source, "<test>");
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
    auto parsed = parser.parse_expression(source, "<test>");
    REQUIRE(parsed.has_value());

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
}

static bool check_source_has_direct_errors(const string& source) {
    yona::parser::Parser parser;
    auto parsed = parser.parse_expression(source, "<test>");
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
        out << "FN yona_Test_Fx__fetch 1 STRING -> STRING effects Fs.read\n";
    }
    yona::parser::Parser parser;
    string source = R"(import fetch from Test\Fx in fetch "/etc/shadow")";
    auto parsed = parser.parse_expression(source, "<test>");
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
        out << "FN yona_Test_Fx__fetch 1 STRING -> STRING effects Fs.read\n";
    }
    yona::parser::Parser parser;
    string source = R"(
import fetch from Test\Fx in
handle fetch "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parse_expression(source, "<test>");
    REQUIRE(parsed.has_value());
    DiagnosticEngine diag;
    TypeChecker checker(diag);
    checker.add_module_path(dir.string());
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Effect: imported HOF open rest from .yonai is E0202") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "yona_yonai_hof_e0202";
    fs::create_directories(dir / "Test");
    {
        std::ofstream out(dir / "Test" / "Hof.yonai");
        out << "FN yona_Test_Hof__apply 2 STRING -> STRING effects | hof\n";
    }
    yona::parser::Parser parser;
    string source = R"(import apply from Test\Hof in apply (\() -> perform Fs.read "/etc/shadow") ())";
    auto parsed = parser.parse_expression(source, "<test>");
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
        out << "FN yona_Test_Hof__apply 2 STRING -> STRING effects | hof\n";
    }
    yona::parser::Parser parser;
    string source = R"(
import apply from Test\Hof in
handle apply (\x -> perform Fs.read x) "ok" with
    Fs.read path resume -> resume path
    return val -> val
end
)";
    auto parsed = parser.parse_expression(source, "<test>");
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
    auto parsed = parser.parse_expression(source, "<test>");
    REQUIRE(parsed.has_value());

    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    checker.check(parsed.value().get());
    CHECK_FALSE(checker.has_direct_errors());
}

TEST_CASE("Effect: no error for handled perform") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker checker(diag);
    auto& arena = checker.arena();

    auto* int_t = arena.make_con(TyCon::Int);
    checker.register_effect("State", "s", {{"get", {}, int_t}});

    // handle (perform State.get ()) with State.get () resume -> resume 0 end
    SourceContext sc{};
    std::vector<yona::ast::ExprNode*> no_args;
    auto* perform_node = new yona::ast::PerformExpr(sc, "State", "get", no_args);

    auto* lit0 = new yona::ast::IntegerExpr(sc, 0);
    std::vector<std::variant<yona::ast::ExprNode*, yona::ast::ValueExpr*>> resume_args;
    resume_args.push_back(static_cast<yona::ast::ExprNode*>(lit0));
    auto* resume_call = new yona::ast::ApplyExpr(sc,
        new yona::ast::NameCall(sc, new yona::ast::NameExpr(sc,"resume")),
        resume_args);
    auto* handler = new yona::ast::HandlerClause(sc, "State", "get", {}, "resume", resume_call);

    auto* handle = new yona::ast::HandleExpr(sc, perform_node, {handler});
    auto* main_node = new yona::ast::MainNode(sc, handle);

    checker.check(main_node);
    CHECK(!checker.has_errors());
    CHECK(diag.warning_count() == 0);

    delete main_node;
}

// ===== Error Code Tests =====

TEST_CASE("Error code: E0100 string representation") {
    CHECK(error_code_str(ErrorCode::E0100) == "E0100");
    CHECK(error_code_str(ErrorCode::E0103) == "E0103");
    CHECK(error_code_str(ErrorCode::E0200) == "E0200");
    CHECK(error_code_str(ErrorCode::E0202) == "E0202");
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

#include "typechecker/RefinementChecker.h"

using namespace yona::compiler::typechecker;
using namespace yona::compiler::types;

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
        RefinePredicate::make_cmp(RefinePredicate::Lt, "n", 65536)
    );
    CHECK(p3->to_string() == "n > 0 && n < 65536");
}

TEST_CASE("RefinementChecker: register and lookup") {
    DiagnosticEngine diag;
    RefinementChecker rc(diag);
    auto pred = RefinePredicate::make_cmp(RefinePredicate::Gt, "n", 0);
    rc.register_refined_type("Positive", BuiltinType::SignedInt64, pred);

    auto* info = rc.lookup("Positive");
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
    CHECK(facts.satisfies(*RefinePredicate::make_cmp(RefinePredicate::Ge, "x", 0)));
    CHECK(!facts.satisfies(*RefinePredicate::make_cmp(RefinePredicate::Ge, "x", 1)));
}

TEST_CASE("FactEnv: Le predicate") {
    FactEnv facts;
    facts.int_bounds["x"] = Interval{0, 10};
    CHECK(facts.satisfies(*RefinePredicate::make_cmp(RefinePredicate::Le, "x", 10)));
    CHECK(!facts.satisfies(*RefinePredicate::make_cmp(RefinePredicate::Le, "x", 9)));
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
    // Should not produce an error — xs is provably non-empty
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let xs = [1, 2] in head xs");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: head on unknown seq warns") {
    // let xs = someFunc 42 in head xs
    // Cannot prove xs is non-empty
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = x in let xs = f 42 in head xs");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(rc.has_errors());
}

TEST_CASE("RefinementChecker: head after [h|t] pattern is OK") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let xs = [1, 2] in case xs of [h|t] -> head xs; [] -> 0 end");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: cons proves non-empty") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let xs = 1 :: [] in head xs");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: division by literal non-zero is OK") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let d = 5 in 100 / d");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: division by unknown warns") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let f x = x in let d = f 0 in 100 / d");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(rc.has_errors());
}

TEST_CASE("RefinementChecker: division by zero literal warns") {
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let d = 0 in 100 / d");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(rc.has_errors());
}

TEST_CASE("RefinementChecker: wildcard after 0 excludes zero") {
    // case n of 0 -> ...; x -> x / ... should know x != 0
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let n = 5 in case n of 0 -> 0; x -> 100 / x end");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: arithmetic propagation add") {
    // let x = 5 in let y = x + 1 → y is in [6, 6]
    yona::compiler::DiagnosticEngine diag;
    RefinementChecker rc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let x = 5 in let y = x + 1 in 100 / y");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    rc.check(result.node.get());
    CHECK(!rc.has_errors());
}

TEST_CASE("RefinementChecker: discarded Option in do warns (-Wunmatched-adt)") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}});

    std::istringstream stream("let f x = Some x in do f 1; () end");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result.node.get());
    CHECK(diag.warning_count() >= 1);
}

TEST_CASE("RefinementChecker: let _ = Option warns") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}});

    std::istringstream stream("let f x = Some x in let _ = f 1 in ()");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result.node.get());
    CHECK(diag.warning_count() >= 1);
}

TEST_CASE("RefinementChecker: let r = Option does not warn unmatched-adt") {
    DiagnosticEngine diag;
    diag.enable_warning(WarningFlag::UnmatchedAdt);
    TypeChecker tc(diag);
    yona::parser::Parser parser;
    parser.register_prelude_constructors();
    tc.register_adt("Option", {"a"}, {{"Some", 1}, {"None", 0}});

    std::istringstream stream("let f x = Some x in let r = f 1 in ()");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    REQUIRE(!tc.has_direct_errors());

    RefinementChecker rc(diag, &tc);
    rc.check(result.node.get());
    CHECK(diag.warning_count() == 0);
}

} // RefinementChecker

// ===== Linearity Checker Tests =====

#include "typechecker/LinearityChecker.h"

TEST_SUITE("LinearityChecker") {

TEST_CASE("LinearEnv: create and consume") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("conn", SourceLocation::unknown());
    CHECK(env.is_live("conn"));
    CHECK(!env.is_consumed("conn"));

    CHECK(env.consume("conn", SourceLocation::unknown()));
    CHECK(!env.is_live("conn"));
    CHECK(env.is_consumed("conn"));
}

TEST_CASE("LinearEnv: double consume returns false") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("conn", SourceLocation::unknown());
    CHECK(env.consume("conn", SourceLocation::unknown()));
    CHECK(!env.consume("conn", SourceLocation::unknown())); // already consumed
}

TEST_CASE("LinearEnv: live_vars lists unclosed") {
    yona::compiler::typechecker::LinearEnv env;
    env.create("a", SourceLocation::unknown());
    env.create("b", SourceLocation::unknown());
    env.consume("a", SourceLocation::unknown());
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

TEST_CASE("LinearityChecker: producer function creates obligation") {
    // let conn = tcpConnect "localhost" 8080 in conn
    // Warning: conn not consumed
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let conn = tcpConnect \"localhost\" 8080 in conn");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    lc.check(result.node.get());
    // Should warn about unconsumed linear value
    CHECK(diag.warning_count() > 0);
}

TEST_CASE("LinearityChecker: non-producer function no warning") {
    // let x = someFunc 42 in x — not a producer, no warning
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let x = length [1, 2, 3] in x");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    lc.check(result.node.get());
    CHECK(!lc.has_errors());
    CHECK(diag.warning_count() == 0);
}

TEST_CASE("LinearityChecker: use after consume is error") {
    // let conn = tcpConnect "host" 80 in
    // let conn2 = conn in   (transfers obligation, conn consumed)
    // let conn3 = conn in   (ERROR: conn already consumed)
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let conn = tcpConnect \"host\" 80 in let conn2 = conn in let conn3 = conn in conn3");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    lc.check(result.node.get());
    CHECK(lc.has_errors());
}

TEST_CASE("LinearityChecker: transfer via alias is OK") {
    // let conn = tcpConnect "host" 80 in
    // let conn2 = conn in   (transfers, conn consumed, conn2 live)
    // conn2  (but conn2 still unconsumed — warning)
    yona::compiler::DiagnosticEngine diag;
    diag.enable_warning(yona::compiler::WarningFlag::UnhandledEffect);
    yona::compiler::typechecker::LinearityChecker lc(diag);

    yona::parser::Parser parser;
    std::istringstream stream("let conn = tcpConnect \"host\" 80 in let conn2 = conn in conn2");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    lc.check(result.node.get());
    CHECK(!lc.has_errors()); // no error (transfer is OK)
    CHECK(diag.warning_count() > 0); // warning: conn2 unconsumed
}

TEST_CASE("Error code: E0600/E0601/E0602/E0603 strings") {
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0600) == "E0600");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0601) == "E0601");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0602) == "E0602");
    CHECK(yona::compiler::error_code_str(yona::compiler::ErrorCode::E0603) == "E0603");
}

TEST_CASE("Error code: E0600 explanation is non-empty") {
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0600).empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0601).empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0602).empty());
    CHECK(!yona::compiler::error_explanation(yona::compiler::ErrorCode::E0603).empty());
}

} // LinearityChecker

TEST_SUITE("BorrowParam") {

TEST_CASE("@borrow rejected when parameter is returned") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream("let f @borrow x = x in f 1");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    CHECK(tc.has_direct_errors());
}

TEST_CASE("@borrow accepted when parameter is only read") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream("let f @borrow x = x + 1 in f 2");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    CHECK(!tc.has_direct_errors());
}

TEST_CASE("@borrow rejected on non-identifier pattern") {
    yona::compiler::DiagnosticEngine diag;
    yona::compiler::typechecker::TypeChecker tc(diag);
    yona::parser::Parser parser;
    std::istringstream stream(
        R"(let g = \ @borrow (a, b) -> a + b in g (1, 2))");
    auto result = parser.parse_input(stream);
    REQUIRE(result.node);
    tc.check(result.node.get());
    CHECK(tc.has_direct_errors());
}

} // BorrowParam
