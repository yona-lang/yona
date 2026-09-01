#include "yona/Semantics/BorrowEscapeAnalysis.h"
#include "yona/Syntax/Parser.h"

#include <doctest/doctest.h>

#include <array>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using std::array;
using std::ostringstream;
using std::pair;
using std::string;
using yona::ast::ApplyExpr;
using yona::ast::AST_UNIT_EXPR;
using yona::ast::CaseExpr;
using yona::ast::CharacterExpr;
using yona::ast::ExprNode;
using yona::ast::ExternDeclExpr;
using yona::ast::ExternPromiseKind;
using yona::ast::FieldUpdateExpr;
using yona::ast::FqnExpr;
using yona::ast::FunctionExpr;
using yona::ast::FunctionsImport;
using yona::ast::ImportExpr;
using yona::ast::IntegerExpr;
using yona::ast::LiteralExpr;
using yona::ast::ModuleCall;
using yona::ast::ModuleImport;
using yona::ast::PatternValue;
using yona::ast::TuplePattern;
using yona::parser::ParsedExpression;
using yona::parser::ParseError;
using yona::parser::Parser;

namespace {

class GroupedHexPunct final : public std::numpunct<char> {
protected:
  char do_thousands_sep() const override { return '_'; }
  string do_grouping() const override { return "\1"; }
};

} // namespace

TEST_SUITE("Std Format parser regressions") {

  TEST_CASE("format accepts an empty-brace placeholder string") {
    Parser parser;
    auto result = parser.parseExpression(
        R"(import format from Std\Format in format "{}" ["ok"])",
        "<format-test>");

    CHECK(result.has_value());
  }

  TEST_CASE("interpolated strings can be function arguments") {
    Parser parser;
    auto result =
        parser.parseExpression(R"(identity "hello {name}")", "<format-test>");

    CHECK(result.has_value());
  }

  TEST_CASE("existing string interpolation remains valid") {
    Parser parser;
    auto result = parser.parseExpression(
        R"(let name = "Yona" in "hello {name}")", "<format-test>");

    CHECK(result.has_value());
  }

  TEST_CASE(
      "wildcard module imports compose with following selective imports") {
    Parser parser;
    auto result = parser.parseExpression(
        R"(import Std\Channel, alloc from Std\ByteArray in alloc 1)",
        "<import-test>");

    REQUIRE(result.has_value());
    auto *import = dynamic_cast<ImportExpr *>(result->get());
    REQUIRE(import);
    REQUIRE(import->clauses.size() == 2);
    CHECK(dynamic_cast<ModuleImport *>(import->clauses[0]) != nullptr);
    CHECK(dynamic_cast<FunctionsImport *>(import->clauses[1]) != nullptr);
  }

  TEST_CASE("package-qualified module calls preserve their canonical FQN") {
    Parser parser;
    auto result = parser.parseExpression(R"(Std\Gpu::available ())",
                                         "<module-call-test>");

    REQUIRE(result.has_value());
    auto *apply = dynamic_cast<ApplyExpr *>(result.value().get());
    REQUIRE(apply);
    auto *call = dynamic_cast<ModuleCall *>(apply->call);
    REQUIRE(call);
    auto *fqn = std::get_if<FqnExpr *>(&call->fqn);
    REQUIRE(fqn);
    REQUIRE(*fqn);
    CHECK((*fqn)->to_string() == R"(Std\Gpu)");
    CHECK(call->funName->value == "available");
    REQUIRE(apply->args.size() == 1);
    auto *argument = std::get_if<ExprNode *>(&apply->args.front());
    REQUIRE(argument);
    REQUIRE(*argument);
    CHECK((*argument)->get_type() == AST_UNIT_EXPR);
  }

  TEST_CASE("expression externs separate local and contractual names") {
    Parser parser;
    auto result = parser.parseExpression(
        R"(extern async testSlowAdd : Int -> Int = "YonaTestSlowAdd" in testSlowAdd 1)",
        "<extern-test>");

    REQUIRE(result.has_value());
    auto *declaration = dynamic_cast<ExternDeclExpr *>(result.value().get());
    REQUIRE(declaration);
    CHECK(declaration->name == "testSlowAdd");
    CHECK(declaration->c_symbol == "YonaTestSlowAdd");
    CHECK(declaration->extern_promise == ExternPromiseKind::ThreadPool);
  }

  TEST_CASE("externs preserve explicit borrowed parameter contracts") {
    Parser parser;
    auto result = parser.parseExpression(
        R"(extern inspect : String -> Int borrow "1" in inspect "value")",
        "<extern-borrow-test>");

    REQUIRE(result.has_value());
    auto *declaration = dynamic_cast<ExternDeclExpr *>(result.value().get());
    REQUIRE(declaration);
    CHECK(declaration->borrowed_params == std::vector<bool>{true});

    auto module = parser.parseModule(R"(
module Test\ExternBorrow
extern inspect : String -> Int borrow "1"
)",
                                     "<extern-borrow-module-test>");
    REQUIRE(module.has_value());
    REQUIRE(module.value()->extern_declarations.size() == 1);
    CHECK(module.value()->extern_declarations.front()->borrowed_params ==
          std::vector<bool>{true});
  }

  TEST_CASE("parenthesized lambda parameter is one tuple pattern") {
    Parser parser;
    auto result = parser.parseExpression(R"(\(left, right) -> left + right)",
                                         "<lambda-test>");

    REQUIRE(result.has_value());
    auto *lambda = dynamic_cast<FunctionExpr *>(result->get());
    REQUIRE(lambda);
    REQUIRE(lambda->patterns.size() == 1);
    auto *tuple = dynamic_cast<TuplePattern *>(lambda->patterns[0]);
    REQUIRE(tuple);
    CHECK(tuple->patterns.size() == 2);
  }

  TEST_CASE("juxtaposed lambda parameters remain separate") {
    Parser parser;
    auto result = parser.parseExpression(R"(\left right -> left + right)",
                                         "<lambda-test>");

    REQUIRE(result.has_value());
    auto *lambda = dynamic_cast<FunctionExpr *>(result->get());
    REQUIRE(lambda);
    CHECK(lambda->patterns.size() == 2);
  }

  TEST_CASE("instance methods preserve consecutive borrowed parameters") {
    Parser parser;
    auto result = parser.parseModule(R"(module Test
trait Same a
    same : a -> a -> Bool
end
instance Same Int
    same @borrow left @borrow right = left == right
end
)",
                                     "<borrow-test>");

    REQUIRE(result.has_value());
    REQUIRE(result.value()->instance_declarations.size() == 1);
    const auto *instance = result.value()->instance_declarations.front();
    REQUIRE(instance->methods.size() == 1);
    const auto *method = instance->methods.front();
    REQUIRE(method->patterns.size() == 2);
    REQUIRE(method->param_borrow.size() == 2);
    CHECK(method->param_borrow[0]);
    CHECK(method->param_borrow[1]);
  }

  TEST_CASE("pattern literals use their polymorphic literal base") {
    static_assert(std::is_base_of_v<LiteralExpr<void *>, IntegerExpr>);

    Parser parser;
    auto result = parser.parseExpression(
        R"(case input of 42 -> 1; binding -> binding end)",
        "<pattern-literal-test>");

    REQUIRE(result.has_value());
    auto *case_expr = dynamic_cast<CaseExpr *>(result.value().get());
    REQUIRE(case_expr);
    REQUIRE(case_expr->clauses.size() == 2);
    auto *pattern =
        dynamic_cast<PatternValue *>(case_expr->clauses[0]->pattern);
    REQUIRE(pattern);
    auto *literal = std::get_if<LiteralExpr<void *> *>(&pattern->expr);
    REQUIRE(literal);
    REQUIRE(*literal);
    CHECK((*literal)->parent == pattern);
    auto *integer = dynamic_cast<IntegerExpr *>(*literal);
    REQUIRE(integer);
    CHECK(integer->value == 42);
  }

  TEST_CASE("character expressions preserve non-ASCII scalar values") {
    Parser parser;
    auto result = parser.parseExpression(R"('\u03BB')", "<character-test>");

    REQUIRE(result.has_value());
    auto *character = dynamic_cast<CharacterExpr *>(result.value().get());
    REQUIRE(character);
    CHECK(static_cast<char32_t>(character->value) == char32_t{0x03BB});
  }

  TEST_CASE("character patterns preserve non-BMP scalar values") {
    Parser parser;
    auto result =
        parser.parseExpression(R"(case input of '\U0001F600' -> 1; _ -> 0 end)",
                               "<character-pattern-test>");

    REQUIRE(result.has_value());
    auto *case_expr = dynamic_cast<CaseExpr *>(result.value().get());
    REQUIRE(case_expr);
    REQUIRE(case_expr->clauses.size() == 2);
    auto *pattern =
        dynamic_cast<PatternValue *>(case_expr->clauses[0]->pattern);
    REQUIRE(pattern);
    auto *literal = std::get_if<LiteralExpr<void *> *>(&pattern->expr);
    REQUIRE(literal);
    REQUIRE(*literal);
    auto *character = dynamic_cast<CharacterExpr *>(*literal);
    REQUIRE(character);
    CHECK(static_cast<char32_t>(character->value) == char32_t{0x1F600});
  }

  TEST_CASE("printed character expressions round-trip through the lexer") {
    const array<pair<char32_t, string>, 9> cases{{
        {char32_t{0x03BB}, R"('\u03BB')"},
        {char32_t{0x1F600}, R"('\U0001F600')"},
        {char32_t{0x10FFFF}, R"('\U0010FFFF')"},
        {U'A', R"('A')"},
        {U'\'', R"('\'')"},
        {U'\\', R"('\\')"},
        {U'\n', R"('\n')"},
        {U'\0', R"('\0')"},
        {char32_t{0x0008}, R"('\u0008')"},
    }};

    for (const auto &[scalar, expected] : cases) {
      CAPTURE(scalar);
      CAPTURE(expected);
      CharacterExpr character(yona::SourceRange::unknown(),
                              static_cast<wchar_t>(scalar));
      ostringstream printed;
      printed << character;
      CHECK(printed.str() == expected);

      Parser parser;
      auto reparsed =
          parser.parseExpression(printed.str(), "<printed-character>");
      REQUIRE(reparsed.has_value());
      auto *round_tripped =
          dynamic_cast<CharacterExpr *>(reparsed.value().get());
      REQUIRE(round_tripped);
      CHECK(static_cast<char32_t>(round_tripped->value) == scalar);
    }
  }

  TEST_CASE("character printing preserves caller stream formatting") {
    CharacterExpr character(yona::SourceRange::unknown(),
                            static_cast<wchar_t>(char32_t{0x03BB}));
    ostringstream printed;
    printed.imbue(std::locale(std::locale::classic(), new GroupedHexPunct));
    printed << std::hex << std::showbase << std::nouppercase
            << std::setfill('*');
    printed.precision(7);
    printed.width(19);
    const auto flags = printed.flags();
    const auto fill = printed.fill();
    const auto precision = printed.precision();
    const auto width = printed.width();
    const auto locale = printed.getloc();

    printed << character;

    CHECK(printed.str() == R"('\u03BB')");
    CHECK(printed.flags() == flags);
    CHECK(printed.fill() == fill);
    CHECK(printed.precision() == precision);
    CHECK(printed.width() == width);
    CHECK(printed.getloc() == locale);

    Parser parser;
    auto reparsed = parser.parseExpression(printed.str(), "<styled-character>");
    REQUIRE(reparsed.has_value());
    auto *round_tripped = dynamic_cast<CharacterExpr *>(reparsed.value().get());
    REQUIRE(round_tripped);
    CHECK(static_cast<char32_t>(round_tripped->value) == char32_t{0x03BB});
  }

  TEST_CASE("field update validates its target before transferring ownership") {
    Parser parser;
    auto valid =
        parser.parseExpression("person { field = 1 }", "<field-update-test>");
    REQUIRE(valid.has_value());
    CHECK(dynamic_cast<FieldUpdateExpr *>(valid.value().get()) != nullptr);

    auto invalid = parser.parseExpression("(identity person) { field = 1 }",
                                          "<field-update-test>");
    CHECK_FALSE(invalid.has_value());
  }

  TEST_CASE("borrow traversal includes pattern alias RHS and guards") {
    Parser parser;

    auto alias = parser.parseExpression("let (head, tail) = pair in head",
                                        "<borrow-analysis-test>");
    REQUIRE(alias.has_value());
    CHECK(yona::compiler::analysis::count_identifier_refs(alias.value().get(),
                                                          "pair") == 1);

    auto guarded_case = parser.parseExpression(
        "case subject of item if check observed -> item end",
        "<borrow-analysis-test>");
    REQUIRE(guarded_case.has_value());
    CHECK(yona::compiler::analysis::count_identifier_refs(
              guarded_case.value().get(), "observed") == 1);

    auto escaping_alias = parser.parseExpression(
        "let (head, tail) = [observed] in 0", "<borrow-analysis-test>");
    REQUIRE(escaping_alias.has_value());
    CHECK(yona::compiler::analysis::heap_param_may_escape(
        escaping_alias.value().get(), "observed", false));

    auto escaping_guard = parser.parseExpression(
        "case subject of _ if [observed] == [] -> 0; _ -> 0 end",
        "<borrow-analysis-test>");
    REQUIRE(escaping_guard.has_value());
    CHECK(yona::compiler::analysis::heap_param_may_escape(
        escaping_guard.value().get(), "observed", false));
  }

  TEST_CASE(
      "parsed expression owns temporary source after parser destruction") {
    std::optional<ParsedExpression> Parsed;
    std::string Source = "retainedName";
    {
      Parser Parser;
      auto Result = Parser.parseExpression(Source, "owned-expression.yona");
      REQUIRE(Result);
      Parsed.emplace(std::move(*Result));
    }
    Source.assign("overwritten");

    REQUIRE(Parsed);
    REQUIRE(Parsed->Expression);
    CHECK(Parsed->Sources->text(Parsed->Source) == "retainedName");
    CHECK(Parsed->Sources->format(Parsed->Expression->Range) ==
          "owned-expression.yona:1:1");
  }

  TEST_CASE("parse errors retain source text and filename") {
    std::vector<ParseError> Errors;
    {
      Parser Parser;
      std::string Source = "let value = in value";
      auto Result = Parser.parseExpression(Source, "broken-expression.yona");
      REQUIRE_FALSE(Result);
      Errors = std::move(Result.error());
      Source.assign("overwritten");
    }

    REQUIRE_FALSE(Errors.empty());
    REQUIRE(Errors.front().Sources);
    CHECK(Errors.front().Sources->text(Errors.front().Range.Source) ==
          "let value = in value");
    CHECK(Errors.front().format().find("broken-expression.yona:") == 0);
  }

} // TEST_SUITE
