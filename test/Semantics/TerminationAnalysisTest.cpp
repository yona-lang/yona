#include "yona/Semantics/TerminationAnalysis.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

namespace {

yona::compiler::termination_analysis::Result
analyze_expression(std::string_view source) {
  yona::parser::Parser parser;
  auto parsed =
      parser.parseExpression(std::string(source), "<termination-test>");
  REQUIRE(parsed.has_value());
  return yona::compiler::termination_analysis::analyze(*parsed->Expression);
}

yona::compiler::termination_analysis::Result
analyze_module(std::string_view source) {
  yona::parser::Parser parser;
  auto parsed = parser.parseModule(std::string(source), "<termination-test>");
  REQUIRE(parsed.has_value());
  return yona::compiler::termination_analysis::analyze(*parsed->Module);
}

} // namespace

TEST_SUITE("TerminationAnalysis") {

  TEST_CASE("recursive calls nested under extern declarations are analyzed") {
    const auto result = analyze_expression(R"(
extern identity_int : Int -> Int in
let loop n = loop n in loop
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE("recursive calls nested in generator sources are analyzed") {
    const auto result = analyze_module(R"(
module Test\HiddenGenerator
export loop
loop n = [x for x = loop n]
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE(
      "same-named functions in separate lexical scopes do not form a cycle") {
    const auto result = analyze_expression(R"(
(
    let f n = g n, g n = n in f 0,
    let f n = n, g n = f n in g 0
)
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("same-named methods in separate instances do not form a cycle") {
    const auto result = analyze_module(R"(
module Test\InstanceScopes
trait Convert a
    f : a -> a
    g : a -> a
end
instance Convert Int
    f n = g n
    g n = n
end
instance Convert String
    f n = n
    g n = f n
end
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("actual guarded function bodies do not prove structural descent") {
    yona::parser::Parser parser;
    auto parsed = parser.parseModule(R"(
module Test\GuardedDescent
type Nat = Zero | Succ Nat
loop (Succ rest) = if true -> loop rest
)",
                                     "<termination-test>");

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->Module->functions.size() == 1);
    REQUIRE(parsed->Module->functions.front()->bodies.size() == 1);
    REQUIRE(dynamic_cast<yona::ast::BodyWithGuards *>(
                parsed->Module->functions.front()->bodies.front()) != nullptr);
    const auto result =
        yona::compiler::termination_analysis::analyze(*parsed->Module);

    REQUIRE_FALSE(result.failures.empty());
    for (const auto &failure : result.failures) {
      CHECK(failure.caller == "loop");
      CHECK(failure.callee == "loop");
    }
  }

  TEST_CASE(
      "lambda aliases resolve recursive calls by their lexical binding name") {
    const auto result = analyze_module(R"(
module Test\HiddenLambda
export wrapper
wrapper n = let f = \x -> f x in f n
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "f");
    CHECK(result.failures.front().callee == "f");
  }

  TEST_CASE("value aliases preserve local callable identity") {
    const auto result = analyze_module(R"(
module Test\CallableAlias
export loop
loop n = let f = loop in f n
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE("value aliases preserve mutual recursive callable identity") {
    const auto result = analyze_module(R"(
module Test\MutualCallableAlias
export left
export right
left n = let next = right in next n
right n = left n
)");

    REQUIRE_FALSE(result.failures.empty());
    for (const auto &failure : result.failures)
      CHECK((failure.caller == "left" || failure.caller == "right"));
  }

  TEST_CASE("nested functions inherit captured non-callable shadows") {
    const auto result = analyze_module(R"(
module Test\LexicalCapture
export outer
f n = outer n
outer n = let f = identity in let g x = f x in g n
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("nested functions inherit captured parameter shadows") {
    const auto result = analyze_module(R"(
module Test\ParameterCapture
export outer
f n = outer n
outer f = let g x = f x in g f
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("nested functions inherit captured import shadows") {
    const auto result = analyze_module(R"(
module Test\ImportCapture
export outer
f n = outer n
outer n = import f from Test\Other in let g x = f x in g n
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("recursive calls nested in perform arguments are analyzed") {
    const auto result = analyze_module(R"(
module Test\HiddenPerform
export loop
loop n = perform State.put (loop n)
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE("handler bindings shadow same-named local functions") {
    const auto result = analyze_expression(R"(
let resume n =
    handle n with
        State.get value resume -> resume n
    end
in resume
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("handler return bindings erase inherited descent facts") {
    const auto result = analyze_module(R"(
module Test\HandlerShadowDescent
export loop
type Nat = Zero | Succ Nat
loop n = case n of
    Zero -> ()
    Succ rest -> handle n with return rest -> loop rest end
end
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE("catch patterns shadow same-named local functions") {
    const auto result = analyze_expression(R"(
let caught n = try raise n catch caught -> caught n end in caught
)");

    CHECK(result.failures.empty());
  }

  TEST_CASE("generator bindings erase inherited descent facts") {
    const auto result = analyze_module(R"(
module Test\GeneratorShadowDescent
export loop
type Nat = Zero | Succ Nat
loop n = case n of
    Zero -> ()
    Succ rest -> let xs = [loop rest for rest = [n]] in ()
end
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

  TEST_CASE("generator guards are analyzed in binder scope") {
    const auto result = analyze_module(R"(
module Test\GeneratorGuard
export loop
loop n = [x for x = [n], if loop n]
)");

    REQUIRE(result.failures.size() == 1);
    CHECK(result.failures.front().caller == "loop");
    CHECK(result.failures.front().callee == "loop");
  }

} // TEST_SUITE("TerminationAnalysis")
