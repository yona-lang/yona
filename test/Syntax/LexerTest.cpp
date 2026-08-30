#include "yona/Syntax/Lexer.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using yona::lexer::Lexer;
using yona::lexer::Token;
using yona::lexer::TokenType;
using yona::lexer::LexError;
using std::get;
using std::holds_alternative;
using std::pair;
using std::size_t;
using std::string;
using std::string_view;
using std::variant;
using std::vector;

static Lexer makeLexer(std::string_view Source) {
  auto Sources = std::make_shared<yona::SourceManager>();
  const auto SourceId =
      Sources->addSource("<lexer-test>", std::string(Source));
  return Lexer(std::move(Sources), SourceId);
}

struct LexerTest {
  void TestTokens(const string &input,
                  const vector<TokenType> &expected_types) {
    auto lexer = makeLexer(input);
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());

    auto tokens = result.value();
    REQUIRE(tokens.size() == expected_types.size());

    for (size_t i = 0; i < expected_types.size(); ++i) {
      CHECK(tokens[i].type == expected_types[i]);
    }
  }

  void TestTokenValues(
      const string &input,
      const vector<pair<TokenType, variant<int64_t, double, string>>>
          expected) {
    auto lexer = makeLexer(input);
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());

    auto tokens = result.value();
    REQUIRE(tokens.size() == expected.size() + 1); // +1 for EOF

    for (size_t i = 0; i < expected.size(); ++i) {
      CHECK(tokens[i].type == expected[i].first);

      if (holds_alternative<int64_t>(expected[i].second)) {
        CHECK(get<int64_t>(tokens[i].value) ==
              get<int64_t>(expected[i].second));
      } else if (holds_alternative<double>(expected[i].second)) {
        CHECK(get<double>(tokens[i].value) ==
              doctest::Approx(get<double>(expected[i].second)));
      } else if (holds_alternative<string>(expected[i].second)) {
        CHECK(get<string>(tokens[i].value) == get<string>(expected[i].second));
      }
    }
  }
};

TEST_SUITE("Lexer") {

  TEST_CASE("SimpleArithmetic") {
    LexerTest fixture;
    fixture.TestTokens("10 + 20", {TokenType::YINTEGER, TokenType::YPLUS,
                                   TokenType::YINTEGER, TokenType::YEOF_TOKEN});
  }

  TEST_CASE("IntegerLiterals") {
    LexerTest fixture;
    fixture.TestTokenValues("42 1000 1_000_000",
                            {{TokenType::YINTEGER, int64_t(42)},
                             {TokenType::YINTEGER, int64_t(1000)},
                             {TokenType::YINTEGER, int64_t(1000000)}});
  }

  TEST_CASE("FloatLiterals") {
    LexerTest fixture;
    fixture.TestTokenValues("3.14 2.0 1e10 1.5e-3",
                            {{TokenType::YFLOAT, 3.14},
                             {TokenType::YFLOAT, 2.0},
                             {TokenType::YFLOAT, 1e10},
                             {TokenType::YFLOAT, 1.5e-3}});
  }

  TEST_CASE("StringLiterals") {
    LexerTest fixture;
    fixture.TestTokenValues(R"("hello" "world\n" "quote:\"" "unicode:\u0041")",
                            {{TokenType::YSTRING, string("hello")},
                             {TokenType::YSTRING, string("world\n")},
                             {TokenType::YSTRING, string("quote:\"")},
                             {TokenType::YSTRING, string("unicode:A")}});
  }

  TEST_CASE("Empty braces remain literal for formatting placeholders") {
    LexerTest fixture;
    fixture.TestTokenValues(R"("{}")", {{TokenType::YSTRING, string("{}")}});
  }

  TEST_CASE("Doubled braces escape literal braces") {
    LexerTest fixture;
    fixture.TestTokenValues(R"("{{value}}")",
                            {{TokenType::YSTRING, string("{value}")}});
  }

  TEST_CASE("Brace escapes coexist with interpolation") {
    auto lexer = makeLexer(R"("{{{name}}}")");
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());
    const auto &tokens = result.value();
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].type == TokenType::YSTRING_PART);
    CHECK(get<string>(tokens[0].value) == "{");
    CHECK(tokens[1].type == TokenType::YIDENTIFIER);
    CHECK(tokens[1].lexeme == "name");
    CHECK(tokens[2].type == TokenType::YSTRING);
    CHECK(get<string>(tokens[2].value) == "}");
    CHECK(tokens[3].type == TokenType::YEOF_TOKEN);
  }

  TEST_CASE("Identifiers") {
    LexerTest fixture;
    fixture.TestTokens("foo bar_baz x' _test",
                       {TokenType::YIDENTIFIER, TokenType::YIDENTIFIER,
                        TokenType::YIDENTIFIER, TokenType::YIDENTIFIER,
                        TokenType::YEOF_TOKEN});
  }

  TEST_CASE("Keywords") {
    LexerTest fixture;
    fixture.TestTokens("let if then else true false",
                       {TokenType::YLET, TokenType::YIF, TokenType::YTHEN,
                        TokenType::YELSE, TokenType::YTRUE, TokenType::YFALSE,
                        TokenType::YEOF_TOKEN});
  }

  TEST_CASE("Operators") {
    LexerTest fixture;
    fixture.TestTokens(
        "+ - * / % ** == != < > <= >= && || ! & | ^ ~ << >> >>>",
        {TokenType::YPLUS,        TokenType::YMINUS,
         TokenType::YSTAR,        TokenType::YSLASH,
         TokenType::YPERCENT,     TokenType::YPOWER,
         TokenType::YEQ,          TokenType::YNEQ,
         TokenType::YLT,          TokenType::YGT,
         TokenType::YLTE,         TokenType::YGTE,
         TokenType::YAND,         TokenType::YOR,
         TokenType::YNOT,         TokenType::YBIT_AND,
         TokenType::YPIPE,        TokenType::YBIT_XOR,
         TokenType::YBIT_NOT,     TokenType::YLEFT_SHIFT,
         TokenType::YRIGHT_SHIFT, TokenType::YZERO_FILL_RIGHT_SHIFT,
         TokenType::YEOF_TOKEN});
  }

  TEST_CASE("Delimiters") {
    LexerTest fixture;
    fixture.TestTokens("( ) [ ] { } , ; : . .. = -> =>",
                       {TokenType::YLPAREN, TokenType::YRPAREN,
                        TokenType::YLBRACKET, TokenType::YRBRACKET,
                        TokenType::YLBRACE, TokenType::YRBRACE,
                        TokenType::YCOMMA,
                        TokenType::YNEWLINE, // semicolons emit as YNEWLINE
                        TokenType::YCOLON, TokenType::YDOT, TokenType::YDOTDOT,
                        TokenType::YASSIGN, TokenType::YARROW,
                        TokenType::YFAT_ARROW, TokenType::YEOF_TOKEN});
  }

  TEST_CASE("ListOperators") {
    LexerTest fixture;
    fixture.TestTokens(":: <| |> ++ @ _",
                       {TokenType::YCONS, TokenType::YPIPE_LEFT,
                        TokenType::YPIPE_RIGHT, TokenType::YJOIN,
                        TokenType::YAT, TokenType::YUNDERSCORE,
                        TokenType::YEOF_TOKEN});
  }

  TEST_CASE("YonaSequenceOperators") {
    LexerTest fixture;
    fixture.TestTokens("-- -| |-", {TokenType::YREMOVE, TokenType::YPREPEND,
                                    TokenType::YAPPEND, TokenType::YEOF_TOKEN});
  }

  TEST_CASE("Comments") {
    LexerTest fixture;
    fixture.TestTokens(R"(
        # Single line comment
        42 # Another comment
        /* Multi-line
           comment */
        43
        /* Nested /* comments */ work */
        44
    )",
                       {TokenType::YINTEGER, // 42
                        TokenType::YNEWLINE, // after 42's comment
                        TokenType::YINTEGER, // 43
                        TokenType::YNEWLINE, // after 43
                        TokenType::YINTEGER, // 44
                        TokenType::YNEWLINE, // after 44
                        TokenType::YEOF_TOKEN});
  }

  TEST_CASE("Symbols") {
    LexerTest fixture;
    auto lexer = makeLexer(":foo :+ :==");
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());
    auto tokens = result.value();
    REQUIRE(tokens.size() == 4); // 3 symbols + EOF

    CHECK(tokens[0].type == TokenType::YSYMBOL);
    CHECK(tokens[0].lexeme == ":foo");

    CHECK(tokens[1].type == TokenType::YSYMBOL);
    CHECK(tokens[1].lexeme == ":+");

    CHECK(tokens[2].type == TokenType::YSYMBOL);
    CHECK(tokens[2].lexeme == ":==");
  }

  TEST_CASE("ComplexExpression") {
    LexerTest fixture;
    fixture.TestTokens(
        "let x = if y > 0 then y * 2 else -y",
        {TokenType::YLET, TokenType::YIDENTIFIER, TokenType::YASSIGN,
         TokenType::YIF, TokenType::YIDENTIFIER, TokenType::YGT,
         TokenType::YINTEGER, TokenType::YTHEN, TokenType::YIDENTIFIER,
         TokenType::YSTAR, TokenType::YINTEGER, TokenType::YELSE,
         TokenType::YMINUS, TokenType::YIDENTIFIER, TokenType::YEOF_TOKEN});
  }

  TEST_CASE("ErrorHandling") {
    LexerTest fixture;
    auto lexer = makeLexer(R"("unterminated string)");
    auto result = lexer.tokenize();

    REQUIRE_FALSE(result.has_value());
    auto errors = result.error();
    REQUIRE(errors.size() >= 1);
    CHECK(errors[0].type == LexError::Type::UNTERMINATED_STRING);
  }

  TEST_CASE("InvalidUtf8DoesNotHang") {
    // Overlong / invalid 2-byte start (0xC0) plus a non-continuation byte.
    // Recovery must advance past the bad byte; otherwise tokenize() grows
    // the error vector forever.
    const char raw[] = {'\xc0', '\x15', '@'};
    auto lexer = makeLexer(std::string_view(raw, sizeof(raw)));
    auto result = lexer.tokenize();

    REQUIRE_FALSE(result.has_value());
    auto errors = result.error();
    REQUIRE(errors.size() >= 1);
    CHECK(errors[0].type == LexError::Type::INVALID_CHARACTER);
  }

  TEST_CASE("CharacterLiterals") {
    LexerTest fixture;
    auto lexer = makeLexer("'a' '\\n' '\\u0041'");
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());
    auto tokens = result.value();
    REQUIRE(tokens.size() == 4); // 3 chars + EOF

    CHECK(tokens[0].type == TokenType::YCHARACTER);
    CHECK(get<char32_t>(tokens[0].value) == U'a');

    CHECK(tokens[1].type == TokenType::YCHARACTER);
    CHECK(get<char32_t>(tokens[1].value) == U'\n');

    CHECK(tokens[2].type == TokenType::YCHARACTER);
    CHECK(get<char32_t>(tokens[2].value) == U'A');
  }

  TEST_CASE("UnicodeIdentifiers") {
    LexerTest fixture;
    fixture.TestTokens("λ пользователь 用户",
                       {TokenType::YIDENTIFIER, TokenType::YIDENTIFIER,
                        TokenType::YIDENTIFIER, TokenType::YEOF_TOKEN});
  }

  TEST_CASE("LocationTracking") {
    LexerTest fixture;
    auto lexer = makeLexer("foo\nbar");
    auto result = lexer.tokenize();

    REQUIRE(result.has_value());
    auto tokens = result.value();
    REQUIRE(tokens.size() >= 2);

    CHECK(tokens[0].Range.Line == 1);
    CHECK(tokens[0].Range.Column == 1);

    CHECK(tokens[1].Range.Line == 2);
    CHECK(tokens[1].Range.Column == 1);
  }

  TEST_CASE("TokenTextDoesNotBorrowCallerOrSourceManagerBuffers") {
    vector<Token> Tokens;
    string Source = "retainedIdentifier";
    {
      auto Sources = std::make_shared<yona::SourceManager>();
      const auto SourceId = Sources->addSource("owned-token.yona", Source);
      Lexer LexerInstance(std::move(Sources), SourceId);
      auto Result = LexerInstance.tokenize();
      REQUIRE(Result);
      Tokens = std::move(*Result);
    }
    Source.assign("overwritten");

    REQUIRE(Tokens.size() == 2);
    CHECK(Tokens.front().lexeme == "retainedIdentifier");
    CHECK(Tokens.front().Range.Source.isValid());
  }

} // TEST_SUITE("Lexer")
