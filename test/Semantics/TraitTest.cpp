#include "Support/RepoPaths.h"
#include "Support/SemanticSetup.h"
#include "Toolchain/YonaLinkUtil.h"
#include "yona/Codegen/Codegen.h"
#include "yona/Semantics/TypeChecker.h"
#include "yona/Support/Diagnostic.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using std::fstream;
using std::istringstream;
using std::size_t;
using std::string;
using std::vector;
using yona::compiler::DiagnosticEngine;
using yona::compiler::ErrorCode;
using yona::compiler::codegen::Codegen;
namespace parser = yona::parser;
namespace typechecker = yona::compiler::typechecker;
namespace fs = std::filesystem;

// ===== Helpers =====

static string compile_and_run_trait(const string &mod_source,
                                    const string &expr_source,
                                    const string &mod_name = "Test/Mod") {
  // Step 1: Compile the module
  parser::Parser p1;
  auto mod_result = p1.parseModule(mod_source, "trait_test.yona");
  if (!mod_result.has_value())
    return "MOD_PARSE_ERROR";

  Codegen mod_codegen("trait_mod");
  auto mod = mod_codegen.compile_module(mod_result.value().get());
  if (!mod)
    return "MOD_CODEGEN_ERROR";
  if (mod_codegen.errorCount() > 0)
    return "MOD_CODEGEN_ERRORS";
  DiagnosticEngine mod_diag;
  typechecker::TypeChecker mod_checker(mod_diag);
  mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                             mod_checker);
  if (mod_checker.has_errors())
    return "MOD_TYPE_ERROR";

  fs::path mod_obj = yona::test::link::scratch_root() / "trait_mod_test.o";
  if (!mod_codegen.emit_object_file(mod_obj.string()))
    return "MOD_EMIT_ERROR";

  fs::path trait_lib = yona::test::link::scratch_root() / "yona_trait_lib";
  fs::path iface_dir = trait_lib / fs::path(mod_name).parent_path();
  fs::create_directories(iface_dir);
  string iface = (trait_lib / mod_name).string() + ".yonai";
  if (!mod_codegen.emit_interface_file(iface))
    return "IFACE_EMIT_ERROR";

  // Step 2: Compile the expression
  parser::Parser p2;
  istringstream stream(expr_source);
  auto expr_result = p2.parseExpression(stream.str(), "<stream>");
  if (!expr_result || !expr_result->Expression)
    return "EXPR_PARSE_ERROR";

  Codegen expr_codegen("trait_test");
  expr_codegen.ModulePaths.push_back(trait_lib.string());
  auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
  if (!expr_mod)
    return "EXPR_CODEGEN_ERROR";

  fs::path expr_obj = yona::test::link::scratch_root() / "trait_expr_test.o";
  if (!expr_codegen.emit_object_file(expr_obj.string()))
    return "EXPR_EMIT_ERROR";

  std::vector<fs::path> trait_objs = {mod_obj, expr_obj};
  if (!yona::test::link::append_runtime_objects(trait_objs))
    return "RT_COMPILE_ERROR";

  fs::path exe_path = yona::test::link::scratch_root() /
                      ("trait_test_exe" + yona::test::link::exe_suffix());
  if (!yona::test::link::link_objs_to_exe(trait_objs, exe_path))
    return "LINK_ERROR";

  return yona::test::link::executeAndCapture(exe_path);
}

// ===== Trait Parsing Tests =====

TEST_SUITE("Trait") {

  TEST_CASE("Trait declaration parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Show

export showInt

trait Show a
    show : a -> Int
end

instance Show Int
    show x = x
end

showInt x = show x
)";
    auto result = parser.parseModule(source, "show.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->trait_declarations.size() == 1);
    CHECK(mod->trait_declarations[0]->name == "Show");
    CHECK(mod->trait_declarations[0]->type_params == vector<string>{"a"});
    CHECK(mod->trait_declarations[0]->methods.size() == 1);
    CHECK(mod->trait_declarations[0]->methods[0].name == "show");
  }

  TEST_CASE("Instance declaration parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Show

export showInt

trait Show a
    show : a -> Int
end

instance Show Int
    show x = x
end

showInt x = show x
)";
    auto result = parser.parseModule(source, "show.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->instance_declarations.size() == 1);
    CHECK(mod->instance_declarations[0]->trait_name == "Show");
    CHECK(mod->instance_declarations[0]->type_names == vector<string>{"Int"});
    CHECK(mod->instance_declarations[0]->methods.size() == 1);
    CHECK(mod->instance_declarations[0]->methods[0]->name == "show");
  }

  TEST_CASE("Export trait parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Show

export trait Show

trait Show a
    show : a -> String
end

instance Show Int
    show x = 42
end
)";
    auto result = parser.parseModule(source, "show.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->exported_traits.size() == 1);
    CHECK(mod->exported_traits[0] == "Show");
  }

  TEST_CASE("Multiple trait methods parse") {
    parser::Parser parser;
    string source = R"(
module Test\Eq

trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
end

instance Eq Int
    eq x y = x == y
    neq x y = x != y
end
)";
    auto result = parser.parseModule(source, "eq.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->trait_declarations.size() == 1);
    CHECK(mod->trait_declarations[0]->methods.size() == 2);
    CHECK(mod->trait_declarations[0]->methods[0].name == "eq");
    CHECK(mod->trait_declarations[0]->methods[1].name == "neq");
    CHECK(mod->instance_declarations.size() == 1);
    CHECK(mod->instance_declarations[0]->methods.size() == 2);
  }

  TEST_CASE("Duplicate visible trait instances are rejected coherently") {
    parser::Parser parser;
    auto parsed = parser.parseModule(R"(
module Test\DuplicateInstance

trait Label a
    label : a -> String
end

instance Label Int
    label value = "first"
end

instance Label Int
    label value = "second"
end
)",
                                     "duplicate_instance.yona");
    REQUIRE(parsed.has_value());

    DiagnosticEngine diag;
    typechecker::TypeChecker checker(diag);
    checker.check_module(parsed.value().get());

    size_t coherence_errors = 0;
    for (const auto &record : diag.records())
      if (record.code == ErrorCode::E0400 &&
          record.message.find("duplicate visible trait instance") !=
              std::string::npos)
        ++coherence_errors;
    CHECK(coherence_errors == 1);
    CHECK(diag.error_count() == 1);
  }

  TEST_CASE("Trait method static dispatch - Int instance") {
    // Module with trait + instance, call trait method with Int arg
    string mod_source = R"(
module Test\Dbl

export dbl

trait Doubler a
    double : a -> a
end

instance Doubler Int
    double x = x * 2
end

dbl x = double x
)";
    string expr_source = R"(
import Test\Dbl in dbl 21
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/Dbl");
    CHECK(result == "42");
  }

  TEST_CASE("Multiple instances, type-directed dispatch") {
    // Module with two instances, check correct dispatch
    string mod_source = R"(
module Test\Inc

export incInt, incFloat

trait Inc a
    inc : a -> a
end

instance Inc Int
    inc x = x + 1
end

incInt x = inc x
incFloat x = x
)";
    string expr_source = R"(
import Test\Inc in incInt 41
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/Inc");
    CHECK(result == "42");
  }

  TEST_CASE("Trait with identity method") {
    string mod_source = R"(
module Test\Id

export idInt

trait Identity a
    ident : a -> a
end

instance Identity Int
    ident x = x
end

idInt x = ident x
)";
    string expr_source = R"(
import Test\Id in idInt 99
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/Id");
    CHECK(result == "99");
  }

  // ===== Phase 2: Constrained Instances =====

  TEST_CASE("Parse constrained instance declaration") {
    parser::Parser parser;
    string source = R"(
module Test\ShowOpt

type Option a = Some a | None

trait Show a
    show : a -> Int
end

instance Show Int
    show x = x
end

instance Show a => Show (Option a)
    show opt = case opt of
        Some x -> show x
        None -> 0
    end
end
)";
    auto result = parser.parseModule(source, "showopt.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);

    // Should have two instances
    CHECK(mod->instance_declarations.size() == 2);

    // Second instance should have constraints
    auto *constrained = mod->instance_declarations[1];
    CHECK(constrained->trait_name == "Show");
    CHECK(constrained->type_names == vector<string>{"Option"});
    CHECK(constrained->constraints.size() == 1);
    CHECK(constrained->constraints[0].first == "Show");
    CHECK(constrained->constraints[0].second == "a");
    CHECK(constrained->type_params.size() == 1);
    CHECK(constrained->type_params[0] == "a");
  }

  TEST_CASE("Constrained instance - show (Some 42) resolves through Show "
            "Option + Show Int") {
    string mod_source = R"(
module Test\ShowOpt2

export type Option
export showOpt

type Option a = Some a | None

trait Stringify a
    stringify : a -> Int
end

instance Stringify Int
    stringify x = x * 10
end

instance Stringify a => Stringify (Option a)
    stringify opt = case opt of
        Some x -> stringify x
        None -> 0
    end
end

showOpt opt = case opt of
    Some x -> stringify opt
    None -> stringify opt
end
)";
    string expr_source = R"(
import Test\ShowOpt2 in showOpt (Some 42)
)";
    auto result =
        compile_and_run_trait(mod_source, expr_source, "Test/ShowOpt2");
    CHECK(result == "420");
  }

  TEST_CASE("Constrained instance - None case") {
    string mod_source = R"(
module Test\ShowNone

export type Option
export showNone

type Option a = Some a | None

trait Stringify a
    stringify : a -> Int
end

instance Stringify Int
    stringify x = x * 10
end

instance Stringify a => Stringify (Option a)
    stringify opt = case opt of
        Some x -> stringify x
        None -> 99
    end
end

showNone opt = case opt of
    Some x -> stringify opt
    None -> stringify opt
end
)";
    string expr_source = R"(
import Test\ShowNone in showNone None
)";
    auto result =
        compile_and_run_trait(mod_source, expr_source, "Test/ShowNone");
    CHECK(result == "99");
  }

  // ===== Phase 3: Default Methods =====

  TEST_CASE("Parse multi-method trait") {
    parser::Parser parser;
    string source = R"(
module Test\Eq

trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
end

instance Eq Int
    eq x y = x == y
    neq x y = x != y
end
)";
    auto result = parser.parseModule(source, "eq.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->trait_declarations[0]->methods.size() == 2);
    CHECK(mod->instance_declarations[0]->methods.size() == 2);
  }

  TEST_CASE(
      "Cross-module multi-parameter dispatch selects every instance head") {
    string mod_source = R"(
module Test\MultiHeadDispatch

export convertInt, convertFloat
export trait Into

trait Into target source
    into : target -> source -> target
end

instance Into String Int
    into _ _ = "int"
end


instance Into String Float
    into _ _ = "float"
end


convertInt : Int -> String
convertInt value = into "" value
convertFloat : Float -> String
convertFloat value = into "" value
)";
    string expr_source = R"(
import convertInt, convertFloat from Test\MultiHeadDispatch in
convertInt 1 ++ ":" ++ convertFloat 1.0
)";
    auto result = compile_and_run_trait(mod_source, expr_source,
                                        "Test/MultiHeadDispatch");
    CHECK(result == "int:float");
  }

  TEST_CASE("Parse default method in trait") {
    parser::Parser parser;
    string source = R"(
module Test\EqDef

trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true
end

instance Eq Int
    eq x y = x == y
end
)";
    auto result = parser.parseModule(source, "eqdef.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->trait_declarations[0]->methods.size() == 2);
    // neq has a default impl
    CHECK(mod->trait_declarations[0]->methods[1].default_impl != nullptr);
    CHECK(mod->trait_declarations[0]->methods[1].name == "neq");
    // Instance only provides eq (neq uses default)
    CHECK(mod->instance_declarations[0]->methods.size() == 1);
    CHECK(mod->instance_declarations[0]->methods[0]->name == "eq");
  }

  TEST_CASE("Default method dispatch - neq delegates to eq") {
    string mod_source = R"(
module Test\EqDef2

export eqTest, neqTest

trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true
end

instance Eq Int
    eq x y = x == y
end

eqTest x y = if eq x y then 1 else 0
neqTest x y = if neq x y then 1 else 0
)";
    string expr_source = R"(
import Test\EqDef2 in neqTest 3 4
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/EqDef2");
    CHECK(result == "1");
  }

  TEST_CASE("Default method dispatch - neq returns false when eq is true") {
    string mod_source = R"(
module Test\EqDef3

export neqSame

trait Eq a
    eq : a -> a -> Bool
    neq : a -> a -> Bool
    neq x y = if eq x y then false else true
end

instance Eq Int
    eq x y = x == y
end

neqSame x y = if neq x y then 1 else 0
)";
    string expr_source = R"(
import Test\EqDef3 in neqSame 5 5
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/EqDef3");
    CHECK(result == "0");
  }

  TEST_CASE("Parse superclass constraint on trait") {
    parser::Parser parser;
    string source = R"(
module Test\Ord

trait Eq a
    eq : a -> a -> Bool
end

trait Eq a => Ord a
    compare : a -> a -> Int
end

instance Eq Int
    eq x y = x == y
end

instance Ord Int
    compare x y = x - y
end
)";
    auto result = parser.parseModule(source, "ord.yona");
    REQUIRE(result.has_value());
    auto *mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->trait_declarations.size() == 2);
    // Ord should have superclass Eq
    auto *ord_trait = mod->trait_declarations[1];
    CHECK(ord_trait->name == "Ord");
    CHECK(ord_trait->superclasses.size() == 1);
    CHECK(ord_trait->superclasses[0].first == "Eq");
    CHECK(ord_trait->superclasses[0].second == "a");
  }

  TEST_CASE("Superclass trait - Ord uses Eq methods") {
    string mod_source = R"(
module Test\Ord2

export cmpTest

trait Eq a
    eq : a -> a -> Bool
end

trait Eq a => Ord a
    compare : a -> a -> Int
end

instance Eq Int
    eq x y = x == y
end

instance Ord Int
    compare x y = x - y
end

cmpTest x y = compare x y
)";
    string expr_source = R"(
import Test\Ord2 in cmpTest 10 3
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/Ord2");
    CHECK(result == "7");
  }

  // ===== Cross-module generics (GENFN monomorphization) =====

  TEST_CASE(
      "Cross-module generic: function re-compiled with different arg type") {
    // Module exports `describe` compiled with Int args (inferred from usage).
    // The expression calls it with an ADT (Option) arg â€” triggers GENFN
    // re-parse.
    string mod_source = R"(
module Test\GenLib

export describe
export type Option

type Option a = Some a | None

trait Stringify a
    stringify : a -> Int
end

instance Stringify Int
    stringify x = x * 100
end

instance Stringify a => Stringify (Option a)
    stringify opt = case opt of
        Some x -> stringify x
        None -> 0
    end
end

describe x = stringify x
)";
    string expr_source = R"(
import Test\GenLib in describe (Some 42)
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/GenLib");
    CHECK(result == "4200");
  }

  TEST_CASE("Cross-module generic: extern fallback when types match") {
    // Module exports `double_it` compiled with Int. Expression also calls with
    // Int. Should use extern path (no GENFN re-parse needed).
    string mod_source = R"(
module Test\GenLib2

export double_it

double_it x = x + x
)";
    string expr_source = R"(
import Test\GenLib2 in double_it 21
)";
    auto result =
        compile_and_run_trait(mod_source, expr_source, "Test/GenLib2");
    CHECK(result == "42");
  }

  TEST_CASE(
      "Cross-module generic: ADT pattern matching in re-parsed function") {
    // Module exports `unwrap_or` that pattern-matches on an ADT.
    // Expression calls with concrete ADT value.
    string mod_source = R"(
module Test\GenLib3

export unwrap_or
export type Maybe

type Maybe a = Just a | Nothing

unwrap_or default val = case val of
    Just x -> x
    Nothing -> default
end
)";
    string expr_source = R"(
import Test\GenLib3 in unwrap_or 0 (Just 99)
)";
    auto result =
        compile_and_run_trait(mod_source, expr_source, "Test/GenLib3");
    CHECK(result == "99");
  }

  TEST_CASE("Exported fn calling private module helper") {
    // GENFN re-parse of `doubledSquare` must resolve unexported `helper`
    // (or skip GENFN and call the precompiled export). Expected 50, not
    // E0104 + a binary that prints 0.
    string mod_source = R"(
module Test\Secret

export doubledSquare

helper x = x * x
doubledSquare x = 2 * helper x
)";
    string expr_source = R"(
import doubledSquare from Test\Secret in doubledSquare 5
)";
    auto result = compile_and_run_trait(mod_source, expr_source, "Test/Secret");
    CHECK(result == "50");
  }

  // ===== Auto-derive tests =====

  // Helper that loads Prelude (needed for Show/Eq/Ord/Hash trait definitions)
  static string compile_and_run_derive(const string &mod_source,
                                       const string &expr_source,
                                       const string &mod_name = "Test/Mod") {
    parser::Parser p1;
    Codegen mod_codegen("trait_mod");
    // Load prelude so Show/Eq/Ord/Hash traits are available
    if (fs::exists(yona::test::lib_dir()))
      mod_codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"})
      if (fs::exists(dir)) {
        mod_codegen.ModulePaths.push_back(fs::canonical(dir).string());
        break;
      }
    DiagnosticEngine mod_diag;
    typechecker::TypeChecker mod_checker(mod_diag);
    YONA_TEST_INSTALL_PRELUDE(mod_codegen, p1, mod_checker);
    for (const auto &path : mod_codegen.ModulePaths)
      mod_checker.add_module_path(path);

    auto mod_result = p1.parseModule(mod_source, "derive_test.yona");
    if (!mod_result.has_value())
      return "MOD_PARSE_ERROR";

    mod_checker.check_module(mod_result.value().get());
    if (!mod_checker.solve_constraints() || mod_checker.has_errors())
      return "MOD_TYPE_ERROR";
    mod_codegen.set_type_checker(&mod_checker);

    auto mod = mod_codegen.compile_module(mod_result.value().get());
    if (!mod)
      return "MOD_CODEGEN_ERROR";
    if (mod_codegen.errorCount() > 0)
      return "MOD_CODEGEN_ERRORS";
    const auto diagnostics_before_interface = mod_diag.records().size();
    std::vector<typechecker::MonoTypePtr> facts_before_interface;
    facts_before_interface.reserve(mod_result.value()->functions.size());
    for (auto *function : mod_result.value()->functions)
      facts_before_interface.push_back(mod_checker.type_of(function));
    mod_codegen.populate_interface_effect_rows(mod_result.value().get(),
                                               mod_checker);
    if (mod_checker.has_errors())
      return "MOD_TYPE_ERROR";
    if (mod_diag.records().size() != diagnostics_before_interface)
      return "MOD_INTERFACE_RECHECK_DIAGNOSTICS";
    for (size_t index = 0; index < mod_result.value()->functions.size();
         ++index) {
      auto *function = mod_result.value()->functions[index];
      if (mod_checker.type_of(function) != facts_before_interface[index])
        return "MOD_INTERFACE_RECHECK_FACTS";
      const auto first = mod_checker.serialize_interface_signature(
          facts_before_interface[index], function->patterns.size());
      const auto second = mod_checker.serialize_interface_signature(
          facts_before_interface[index], function->patterns.size());
      if (first.parameter_descriptors != second.parameter_descriptors ||
          first.return_descriptor != second.return_descriptor ||
          first.effect_scheme != second.effect_scheme)
        return "MOD_INTERFACE_UNSTABLE_SIGNATURE";
    }

    fs::path trait_lib =
        yona::test::link::scratch_root() / "yona_trait_lib_derive";
    fs::path mod_obj =
        yona::test::link::scratch_root() / "trait_derive_mod_test.o";
    if (!mod_codegen.emit_object_file(mod_obj.string()))
      return "MOD_EMIT_ERROR";

    fs::path iface_dir = trait_lib / fs::path(mod_name).parent_path();
    fs::create_directories(iface_dir);
    string iface = (trait_lib / mod_name).string() + ".yonai";
    if (!mod_codegen.emit_interface_file(iface))
      return "IFACE_EMIT_ERROR";

    Codegen expr_codegen("trait_test");
    expr_codegen.ModulePaths.push_back(trait_lib.string());
    if (fs::exists(yona::test::lib_dir()))
      expr_codegen.ModulePaths.push_back(
          fs::canonical(yona::test::lib_dir()).string());
    for (auto &dir : {"lib", "../lib", "../../lib", "../../../lib"})
      if (fs::exists(dir)) {
        expr_codegen.ModulePaths.push_back(fs::canonical(dir).string());
        break;
      }
    parser::Parser p2;
    DiagnosticEngine expr_diag;
    typechecker::TypeChecker expr_checker(expr_diag);
    YONA_TEST_INSTALL_PRELUDE(expr_codegen, p2, expr_checker);
    for (const auto &path : expr_codegen.ModulePaths)
      expr_checker.add_module_path(path);
    istringstream stream(expr_source);
    auto expr_result = p2.parseExpression(stream.str(), "<stream>");
    if (!expr_result || !expr_result->Expression)
      return "EXPR_PARSE_ERROR";
    expr_checker.check(expr_result->Expression.get());
    if (!expr_checker.solve_constraints() || expr_checker.has_errors())
      return "EXPR_TYPE_ERROR";
    expr_codegen.set_type_checker(&expr_checker);
    auto expr_mod = expr_codegen.compile(expr_result->Expression.get());
    if (!expr_mod)
      return "EXPR_CODEGEN_ERROR";

    fs::path expr_obj =
        yona::test::link::scratch_root() / "trait_derive_expr_test.o";
    if (!expr_codegen.emit_object_file(expr_obj.string()))
      return "EXPR_EMIT_ERROR";

    if (!yona::test::link::ensure_runtime_objects())
      return "RT_COMPILE_ERROR";

    fs::path exe_path =
        yona::test::link::scratch_root() /
        ("yona_derive_test_exe" + yona::test::link::exe_suffix());
    vector<fs::path> objs = {expr_obj, mod_obj};
    if (!yona::test::link::append_prelude_object(objs))
      return "PRELUDE_OBJECT_ERROR";
    if (!yona::test::link::append_runtime_objects(objs))
      return "RT_COMPILE_ERROR";
    if (!yona::test::link::link_objs_to_exe(objs, exe_path))
      return "LINK_ERROR";

    return yona::test::link::executeAndCapture(exe_path);
  }

  TEST_CASE("Derive Show for enum type") {
    string mod_source = R"(
module Test\DeriveShow1

export type Color
export showRed

type Color = Red | Green | Blue
    deriving Show

showRed = show Red
)";
    string expr_source = R"(
import showRed from Test\DeriveShow1, length from Std\String in
length showRed
)";
    auto result =
        compile_and_run_derive(mod_source, expr_source, "Test/DeriveShow1");
    CHECK(result == "3");
  }

  TEST_CASE("Derive Show for constructor with fields") {
    string mod_source = R"(
module Test\DeriveShow2

export type Wrapper
export showIt

type Wrapper = Wrap Int
    deriving Show

showIt x = show (Wrap x)
)";
    string expr_source = R"(
import Test\DeriveShow2 in showIt 42
)";
    auto result =
        compile_and_run_derive(mod_source, expr_source, "Test/DeriveShow2");
    CHECK(result == "Wrap(42)");
  }

  TEST_CASE("Derive Eq for enum type") {
    string mod_source = R"(
module Test\DeriveEq1

export type Color
export eqTest

type Color = Red | Green | Blue
    deriving Eq

eqTest a b = if eq a b then 1 else 0
)";
    string expr_source = R"(
import Test\DeriveEq1 in eqTest Red Red
)";
    auto result =
        compile_and_run_derive(mod_source, expr_source, "Test/DeriveEq1");
    CHECK(result == "1");
  }

  TEST_CASE("Derive Eq returns false for different constructors") {
    string mod_source = R"(
module Test\DeriveEq2

export type Color
export eqTest

type Color = Red | Green | Blue
    deriving Eq

eqTest a b = if eq a b then 1 else 0
)";
    string expr_source = R"(
import Test\DeriveEq2 in eqTest Red Blue
)";
    auto result =
        compile_and_run_derive(mod_source, expr_source, "Test/DeriveEq2");
    CHECK(result == "0");
  }

  TEST_CASE("Equality operators dispatch through a derived Eq instance") {
    string mod_source = R"(
module Test\DerivedEqOperator

export type Box
export same, different

type Box = Box String | Empty
    deriving Eq

same a b = if a == b then 1 else 0
different a b = if a != b then 1 else 0
)";
    string expr_source = R"(
import Test\DerivedEqOperator in
same (Box "same") (Box "same") +
different (Box "left") (Box "right") +
different Empty (Box "value")
)";
    auto result = compile_and_run_derive(mod_source, expr_source,
                                         "Test/DerivedEqOperator");
    CHECK(result == "3");
  }

  TEST_CASE("Equality remains valid for nested heap fields inside a callback") {
    string mod_source = R"(
module Test\NestedEqCallback

export type Envelope
export evaluate

type Envelope = Envelope String
    deriving Eq

evaluate : (Envelope -> Int) -> Int
evaluate callback = callback (Envelope "alpha-beta")
)";
    string expr_source = R"(
import Test\NestedEqCallback in
evaluate (\actual -> if actual == Envelope "alpha-beta" then 1 else 0)
)";
    auto result = compile_and_run_derive(mod_source, expr_source,
                                         "Test/NestedEqCallback");
    CHECK(result == "1");
  }

  TEST_CASE("Derived equality specializes every parameter of a generic ADT") {
    string mod_source = R"(
module Test\GenericEqOperator

export type Pair
export same

type Pair a b = First a | Second b
    deriving Eq

same : Pair (Int, String) -> Pair (Int, String) -> Bool
same left right = left == right
)";
    string expr_source = R"(
import Test\GenericEqOperator in
let left = "same" ++ "", right = "same" ++ "" in
(if same (Second left) (Second right) then 1 else 0) +
(if same (First 42) (First 42) then 2 else 0) +
(if same (First 42) (Second "42") then 0 else 4)
)";
    auto result = compile_and_run_derive(mod_source, expr_source,
                                         "Test/GenericEqOperator");
    CHECK(result == "7");
  }

  TEST_CASE("Ordering operators and compare dispatch through a derived Ord "
            "instance") {
    string mod_source = R"(
module Test\DerivedOrdOperator

export type Priority
export checkOrder

type Priority = Low | Normal String | High
    deriving Eq, Ord

checkOrder _ =
    if Low < Normal "a" &&
       Normal "a" < Normal "b" &&
       High >= Normal "z"
    then case compare High Low of
        Greater -> 1
        _ -> 0
    end
    else 0
)";
    string expr_source = R"(
import checkOrder from Test\DerivedOrdOperator in checkOrder ()
)";
    auto result = compile_and_run_derive(mod_source, expr_source,
                                         "Test/DerivedOrdOperator");
    CHECK(result == "1");
  }

  TEST_CASE("Derive Hash for enum type") {
    string mod_source = R"(
module Test\DeriveHash1

export type Color
export hashTest

type Color = Red | Green | Blue
    deriving Eq, Hash

hashTest c = hash c
)";
    string expr_source = R"(
import Test\DeriveHash1 in hashTest Green
)";
    auto result =
        compile_and_run_derive(mod_source, expr_source, "Test/DeriveHash1");
    CHECK(result == "1");
  }

  TEST_CASE("Derived Hash folds every field and agrees for equal values") {
    string mod_source = R"(
module Test\DeriveHashFields

export type Pair
export hashPair, samePair

type Pair = Pair Int Int
    deriving Eq, Hash

hashPair value = hash value
samePair left right = left == right
)";
    string expr_source = R"(
import Test\DeriveHashFields in
let first = Pair 4 9, equal = Pair 4 9, changed = Pair 4 10 in
if samePair first equal &&
   hashPair first == hashPair equal &&
   hashPair first != hashPair changed
then 1 else 0
)";
    auto result = compile_and_run_derive(mod_source, expr_source,
                                         "Test/DeriveHashFields");
    CHECK(result == "1");
  }

  TEST_CASE("Structural derives reject non-lawful function fields once") {
    parser::Parser parser;
    auto parsed = parser.parseModule(R"(
module Test\InvalidDerive

type CallbackBox = CallbackBox (Int -> Int)
    deriving Eq
)",
                                     "invalid_derive.yona");
    REQUIRE(parsed.has_value());

    DiagnosticEngine diagnostics;
    typechecker::TypeChecker checker(diagnostics);
    checker.check_module(parsed.value().get());
    CHECK(checker.has_errors());
    size_t derive_errors = 0;
    for (const auto &record : diagnostics.records())
      if (record.code == ErrorCode::E0400 &&
          record.message.find("cannot derive Eq") != string::npos)
        ++derive_errors;
    CHECK(derive_errors == 1);
  }

} // TEST_SUITE
