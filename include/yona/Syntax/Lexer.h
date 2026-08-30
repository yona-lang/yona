#ifndef YONA_SYNTAX_LEXER_H
#define YONA_SYNTAX_LEXER_H

#include "yona/Support/Export.h"
#include "yona/Support/SourceManager.h"

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

// Disable warnings about STL types needing DLL interfaces
// These are safe when using consistent runtime libraries
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // class needs to have dll-interface
#endif

namespace yona::lexer {

// Token types
enum class TokenType {
  // Literals
  YINTEGER,
  YFLOAT,
  YSTRING,
  YCHARACTER,
  YBYTE,
  YSYMBOL,
  YTRUE,
  YFALSE,
  YUNIT,

  // Identifiers and keywords
  YIDENTIFIER,
  YMODULE,
  YIMPORT,
  YFROM,
  YAS,
  YEXPORT,
  YLET,
  YIN,
  YIF,
  YTHEN,
  YELSE,
  YCASE,
  YOF,
  YDO,
  YEND,
  YTRY,
  YCATCH,
  YRAISE,
  YWITH,
  YFUN,
  YLAMBDA,
  YRECORD,
  YTYPE,
  YTRAIT,
  YINSTANCE,
  YDERIVING, // deriving (auto-derive trait instances)
  YEXTERN,   // extern
  YFOR,      // for (generator comprehensions)
  YEFFECT,   // effect (effect declaration)
  YPERFORM,  // perform (effect operation invocation)
  YHANDLE,   // handle (effect handler installation)

  // Operators
  YPLUS,    // +
  YMINUS,   // -
  YSTAR,    // *
  YSLASH,   // /
  YPERCENT, // %
  YPOWER,   // **

  // Comparison
  YEQ,  // ==
  YNEQ, // !=
  YLT,  // <
  YGT,  // >
  YLTE, // <=
  YGTE, // >=

  // Logical
  YAND, // &&
  YOR,  // ||
  YNOT, // !

  // Bitwise
  YBIT_AND,               // &
  YBIT_OR,                // |
  YBIT_XOR,               // ^
  YBIT_NOT,               // ~
  YLEFT_SHIFT,            // <<
  YRIGHT_SHIFT,           // >>
  YZERO_FILL_RIGHT_SHIFT, // >>>

  // Assignment and binding
  YASSIGN,    // =
  YARROW,     // ->
  YFAT_ARROW, // =>

  // Delimiters
  YLPAREN,   // (
  YRPAREN,   // )
  YLBRACKET, // [
  YRBRACKET, // ]
  YLBRACE,   // {
  YRBRACE,   // }

  // Separators
  YCOMMA,      // ,
  YSEMICOLON,  // ;
  YCOLON,      // :
  YDOT,        // .
  YDOTDOT,     // ..
  YPIPE,       // |
  YAT,         // @
  YUNDERSCORE, // _
  YBACKSLASH,  // \ (for module paths)

  // List operations
  YCONS,       // ::
  YCONS_RIGHT, // :>
  YPIPE_LEFT,  // <|
  YPIPE_RIGHT, // |>
  YJOIN,       // ++
  YREMOVE,     // --
  YPREPEND,    // -|
  YAPPEND,     // |-

  // Special
  YEOF_TOKEN,
  YNEWLINE,
  YSTRING_PART, // String segment before/between/after interpolation
  YINTERP_END,  // } closing interpolation in string

  // Error
  YERROR
};

/// An owned token value.
///
/// Lexeme and literal strings are owned. Range contains a manager-local source
/// identity, so resolving it still requires the SourceManager retained by the
/// originating Lexer or accompanying error/result owner.
struct Token {
  TokenType type;
  std::string lexeme;
  SourceRange Range;

  // Value for literals
  using LiteralValue =
      std::variant<std::monostate,  // No value
                   int64_t,         // INTEGER
                   double,          // FLOAT
                   std::string,     // STRING (owned, as it may contain escapes)
                   char32_t,        // CHARACTER
                   uint8_t          // BYTE
                   >;
  LiteralValue value;

  [[nodiscard]] std::string to_string() const;
  [[nodiscard]] bool is_keyword() const noexcept;
  [[nodiscard]] bool is_operator() const noexcept;
  [[nodiscard]] bool is_literal() const noexcept;
};

/// Owned lexical failure plus shared ownership of its source storage.
struct LexError {
  enum class Type {
    INVALID_CHARACTER,
    UNTERMINATED_STRING,
    UNTERMINATED_COMMENT,
    INVALID_ESCAPE_SEQUENCE,
    INVALID_NUMBER_FORMAT,
    INVALID_CHARACTER_LITERAL,
    UNICODE_ERROR
  };

  Type type;
  std::string message;
  SourceRange Range;
  std::shared_ptr<const SourceManager> Sources;

  [[nodiscard]] std::string format() const {
    const std::string Location =
        Sources ? Sources->format(Range) : std::string("<unknown>:0:0");
    return std::format("{}: Lexical error: {}", Location, message);
  }
};

/// Stateful cursor over one immutable source buffer.
///
/// Construction retains shared ownership of Sources and borrows a string view
/// from it. A null manager throws std::invalid_argument; an invalid or foreign
/// SourceId throws std::out_of_range. Calls on one Lexer must be serialized.
/// Separate lexers may share the immutable manager concurrently.
class YONA_API Lexer {
public:
  explicit Lexer(std::shared_ptr<const SourceManager> Sources,
                 SourceId Source);

  /// Consume from the current cursor through EOF. On lexical failure all
  /// recovered errors are returned and the partial token vector is discarded.
  [[nodiscard]] std::expected<std::vector<Token>, std::vector<LexError>>
  tokenize();

  /// Advance by one token, or return one owned error retaining Sources.
  [[nodiscard]] std::expected<Token, LexError> nextToken();

  /// Return the next token or error while restoring all cursor state.
  [[nodiscard]] std::expected<Token, LexError> peekToken();

  // Check if at end of input
  [[nodiscard]] bool isAtEnd() const noexcept {
    return Current >= SourceText.length();
  }

  // Get the current source range.
  [[nodiscard]] SourceRange currentRange() const noexcept {
    return {Source, Line, Column, Current, 0};
  }

private:
  std::shared_ptr<const SourceManager> Sources;
  SourceId Source;
  std::string_view SourceText;
  size_t Current = 0;
  size_t Line = 1;
  size_t Column = 1;
  size_t TokenStart = 0;
  size_t TokenStartLine = 1;
  size_t TokenStartColumn = 1;
  int bracket_depth_ = 0;
  int block_depth_ = 0; // case/do/with/handle nesting; newlines stay
                        // significant inside these blocks even when the
                        // surrounding bracket_depth_ would suppress them
  TokenType last_emitted_ = TokenType::YEOF_TOKEN;
  int in_string_interp_ = 0; // > 0 when inside {expr} in a string

  // Keyword lookup table - use function to avoid DLL boundary issues
  [[nodiscard]] static const std::unordered_map<std::string_view, TokenType> &
  get_keywords() noexcept;

  // Character classification
  [[nodiscard]] static bool is_alpha(char32_t ch) noexcept;
  [[nodiscard]] static bool is_digit(char32_t ch) noexcept;
  [[nodiscard]] static bool is_alnum(char32_t ch) noexcept;
  [[nodiscard]] static bool is_whitespace(char32_t ch) noexcept;
  [[nodiscard]] static bool is_identifier_start(char32_t ch) noexcept;
  [[nodiscard]] static bool is_identifier_continue(char32_t ch) noexcept;
  [[nodiscard]] static bool is_operator_char(char32_t ch) noexcept;

  // UTF-8 handling
  [[nodiscard]] std::expected<char32_t, LexError> peek_char() const;
  [[nodiscard]] std::expected<char32_t, LexError> advance_char();
  void skip_char();

  // Token creation
  [[nodiscard]] Token make_token(TokenType type) const;
  [[nodiscard]] Token make_token(TokenType type,
                                 Token::LiteralValue value) const;
  [[nodiscard]] Token make_error_token(const std::string &message) const;

  // Lexing methods
  void skip_whitespace_and_comments();
  [[nodiscard]] std::expected<Token, LexError> scan_token();
  [[nodiscard]] std::expected<Token, LexError> scan_identifier();
  [[nodiscard]] std::expected<Token, LexError> scan_number();
  [[nodiscard]] std::expected<Token, LexError> scan_string();
  [[nodiscard]] std::expected<Token, LexError> scan_string_body();
  [[nodiscard]] std::expected<Token, LexError> scan_character();
  [[nodiscard]] std::expected<Token, LexError> scan_symbol();
  [[nodiscard]] std::expected<Token, LexError>
  scan_operator(char32_t first_char);

  // Helper methods
  [[nodiscard]] bool match(char32_t expected);
  [[nodiscard]] bool match_sequence(std::string_view sequence);
  [[nodiscard]] std::expected<char32_t, LexError> parse_escape_sequence();
  [[nodiscard]] std::expected<char32_t, LexError>
  parse_unicode_escape(int digits);

  // Mark token start position
  void mark_token_start() {
    TokenStart = Current;
    TokenStartLine = Line;
    TokenStartColumn = Column;
  }

  // Get current lexeme
  [[nodiscard]] std::string_view current_lexeme() const {
    return SourceText.substr(TokenStart, Current - TokenStart);
  }
};

// Token type to string conversion for debugging
[[nodiscard]] std::string_view token_type_to_string(TokenType type) noexcept;

} // namespace yona::lexer

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif /* YONA_SYNTAX_LEXER_H */
