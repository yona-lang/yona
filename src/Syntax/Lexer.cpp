#include "yona/Syntax/Lexer.h"

#include <algorithm>
#include <expected>
#include <stdexcept>

namespace yona::lexer {

// Keyword lookup table - use function to avoid DLL boundary issues
const std::unordered_map<std::string_view, TokenType> &
Lexer::get_keywords() noexcept {
  static const std::unordered_map<std::string_view, TokenType> keywords = {
      {"module", TokenType::YMODULE},
      {"import", TokenType::YIMPORT},
      {"from", TokenType::YFROM},
      {"as", TokenType::YAS},
      {"export", TokenType::YEXPORT},
      {"let", TokenType::YLET},
      {"in", TokenType::YIN},
      {"if", TokenType::YIF},
      {"then", TokenType::YTHEN},
      {"else", TokenType::YELSE},
      {"case", TokenType::YCASE},
      {"of", TokenType::YOF},
      {"do", TokenType::YDO},
      {"end", TokenType::YEND},
      {"try", TokenType::YTRY},
      {"catch", TokenType::YCATCH},
      {"raise", TokenType::YRAISE},
      {"with", TokenType::YWITH},
      {"extern", TokenType::YEXTERN},
      {"for", TokenType::YFOR},
      {"effect", TokenType::YEFFECT},
      {"perform", TokenType::YPERFORM},
      {"handle", TokenType::YHANDLE},
      {"fun", TokenType::YFUN},
      {"lambda", TokenType::YLAMBDA},
      {"record", TokenType::YRECORD},
      {"type", TokenType::YTYPE},
      {"trait", TokenType::YTRAIT},
      {"instance", TokenType::YINSTANCE},
      {"deriving", TokenType::YDERIVING},
      {"true", TokenType::YTRUE},
      {"false", TokenType::YFALSE},
  };
  return keywords;
}

// Token methods
std::string Token::to_string() const {
  return std::format("[{}:{}:{} {} '{}']", Range.Line, Range.Column,
                     token_type_to_string(type), static_cast<int>(type),
                     lexeme);
}

bool Token::is_keyword() const noexcept {
  return type >= TokenType::YMODULE && type <= TokenType::YINSTANCE;
}

bool Token::is_operator() const noexcept {
  return type >= TokenType::YPLUS && type <= TokenType::YJOIN;
}

bool Token::is_literal() const noexcept {
  return type >= TokenType::YINTEGER && type <= TokenType::YUNIT;
}

// Character classification
bool Lexer::is_alpha(char32_t ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

bool Lexer::is_digit(char32_t ch) noexcept { return ch >= '0' && ch <= '9'; }

bool Lexer::is_alnum(char32_t ch) noexcept {
  return is_alpha(ch) || is_digit(ch);
}

bool Lexer::is_whitespace(char32_t ch) noexcept {
  // Newlines are NOT whitespace — they are significant tokens at bracket depth
  // 0. The lexer handles newlines explicitly in scan_token().
  return ch == ' ' || ch == '\t' || ch == '\r';
}

bool Lexer::is_identifier_start(char32_t ch) noexcept {
  return is_alpha(ch) || ch == '_' || ch > 127; // Allow Unicode identifiers
}

bool Lexer::is_identifier_continue(char32_t ch) noexcept {
  return is_alnum(ch) || ch == '_' || ch == '\'' || ch > 127;
}

bool Lexer::is_operator_char(char32_t ch) noexcept {
  return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' ||
         ch == '=' || ch == '!' || ch == '<' || ch == '>' || ch == '&' ||
         ch == '|' || ch == '^' || ch == '~' || ch == '@';
}

// Constructor
Lexer::Lexer(std::shared_ptr<const SourceManager> Sources, SourceId Source)
    : Sources(std::move(Sources)), Source(Source) {
  if (!this->Sources)
    throw std::invalid_argument("lexer requires a SourceManager");
  SourceText = this->Sources->text(Source);
}

// UTF-8 decoding
std::expected<char32_t, LexError> Lexer::peek_char() const {
  if (Current >= SourceText.length()) {
    return U'\0';
  }

  // Simple ASCII fast path
  unsigned char ch = SourceText[Current];
  if (ch < 0x80) {
    return static_cast<char32_t>(ch);
  }

  // UTF-8 decoding
  size_t len = 0;
  char32_t result = 0;

  if (ch >= 0xC2 && ch <= 0xDF) {
    len = 2;
    result = ch & 0x1F;
  } else if (ch >= 0xE0 && ch <= 0xEF) {
    len = 3;
    result = ch & 0x0F;
  } else if (ch >= 0xF0 && ch <= 0xF4) {
    len = 4;
    result = ch & 0x07;
  } else {
    return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER,
                                    "Invalid UTF-8 sequence", currentRange()});
  }

  if (Current + len > SourceText.length()) {
    return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER,
                                    "Truncated UTF-8 sequence",
                                    currentRange()});
  }

  for (size_t i = 1; i < len; ++i) {
    unsigned char byte = SourceText[Current + i];
    if ((byte & 0xC0) != 0x80) {
      return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER,
                                      "Invalid UTF-8 continuation byte",
                                      currentRange()});
    }
    result = (result << 6) | (byte & 0x3F);
  }

  const bool non_minimal =
      (len == 3 && result < 0x800) || (len == 4 && result < 0x10000);
  const bool surrogate = result >= 0xD800 && result <= 0xDFFF;
  if (non_minimal || surrogate || result > 0x10FFFF) {
    return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER,
                                    "Invalid UTF-8 scalar value",
                                    currentRange()});
  }

  return result;
}

std::expected<char32_t, LexError> Lexer::advance_char() {
  if (isAtEnd())
    return U'\0';

  auto ch_result = peek_char();
  if (!ch_result) {
    return ch_result;
  }

  char32_t ch = ch_result.value();

  // Update position
  if (ch == '\n') {
    Line++;
    Column = 1;
  } else {
    Column++;
  }

  // Advance current position by the number of bytes in the UTF-8 sequence
  if (ch < 0x80) {
    Current++;
  } else if (ch < 0x800) {
    Current += 2;
  } else if (ch < 0x10000) {
    Current += 3;
  } else {
    Current += 4;
  }

  return ch;
}

void Lexer::skip_char() {
  if (auto result = advance_char(); !result) {
    // peek/advance leave Current unchanged on invalid UTF-8. Skip one
    // raw byte so tokenize() recovery cannot grow the error list forever.
    if (Current < SourceText.length()) {
      Current++;
      Column++;
    }
  }
}

// Token creation
Token Lexer::make_token(TokenType type) const {
  return Token{type,
               std::string(current_lexeme()),
               {Source, TokenStartLine, TokenStartColumn, TokenStart,
                Current - TokenStart},
               {}};
}

Token Lexer::make_token(TokenType type, Token::LiteralValue value) const {
  return Token{type,
               std::string(current_lexeme()),
               {Source, TokenStartLine, TokenStartColumn, TokenStart,
                Current - TokenStart},
               std::move(value)};
}

Token Lexer::make_error_token(const std::string &message) const {
  return Token{TokenType::YERROR,
               std::string(current_lexeme()),
               {Source, TokenStartLine, TokenStartColumn, TokenStart,
                Current - TokenStart},
               message};
}

// Skip whitespace and comments
void Lexer::skip_whitespace_and_comments() {
  while (!isAtEnd()) {
    auto ch_result = peek_char();
    if (!ch_result)
      return;

    char32_t ch = ch_result.value();

    if (is_whitespace(ch)) {
      skip_char();
    } else if (ch == '\n') {
      // Inside brackets, newlines are normally whitespace — but if we
      // are also inside a `case`/`do`/`with`/`handle` block, newlines
      // are clause separators and must reach the parser even when the
      // surrounding parens would otherwise suppress them.
      if (bracket_depth_ > 0 && block_depth_ == 0) {
        skip_char();
      } else {
        // At bracket depth 0 (or inside a block), stop — let scan_token()
        // handle the newline
        break;
      }
    } else if (ch == '#') {
      // Single-line comment — skip to end of line, leave \n for newline
      // handling
      skip_char();
      while (!isAtEnd()) {
        auto next = peek_char();
        if (!next || next.value() == '\n')
          break;
        skip_char();
      }
    } else if (ch == '/' && Current + 1 < SourceText.length() &&
               SourceText[Current + 1] == '*') {
      // Multi-line comment — consumes newlines inside it
      skip_char(); // /
      skip_char(); // *
      int depth = 1;

      while (!isAtEnd() && depth > 0) {
        auto next = peek_char();
        if (!next)
          break;

        if (next.value() == '/' && Current + 1 < SourceText.length() &&
            SourceText[Current + 1] == '*') {
          skip_char();
          skip_char();
          depth++;
        } else if (next.value() == '*' && Current + 1 < SourceText.length() &&
                   SourceText[Current + 1] == '/') {
          skip_char();
          skip_char();
          depth--;
        } else {
          skip_char();
        }
      }
    } else {
      break;
    }
  }
}

// Scan identifier
std::expected<Token, LexError> Lexer::scan_identifier() {
  while (!isAtEnd()) {
    auto ch_result = peek_char();
    if (!ch_result)
      return std::unexpected(ch_result.error());

    if (!is_identifier_continue(ch_result.value())) {
      break;
    }
    skip_char();
  }

  std::string_view lexeme = current_lexeme();

  // Check if it's a keyword
  if (auto it = get_keywords().find(lexeme); it != get_keywords().end()) {
    return make_token(it->second);
  }

  return make_token(TokenType::YIDENTIFIER);
}

// Scan number
std::expected<Token, LexError> Lexer::scan_number() {
  bool has_dot = false;
  bool has_exp = false;

  while (!isAtEnd()) {
    auto ch_result = peek_char();
    if (!ch_result)
      return std::unexpected(ch_result.error());

    char32_t ch = ch_result.value();

    if (is_digit(ch)) {
      skip_char();
    } else if (ch == '.' && !has_dot && !has_exp) {
      // Look ahead to ensure it's not ".."
      if (Current + 1 < SourceText.length() && SourceText[Current + 1] == '.') {
        break;
      }
      has_dot = true;
      skip_char();
    } else if ((ch == 'e' || ch == 'E') && !has_exp) {
      has_exp = true;
      skip_char();

      // Handle optional sign
      auto next = peek_char();
      if (next && (next.value() == '+' || next.value() == '-')) {
        skip_char();
      }
    } else if (ch == '_') {
      // Allow underscores in numbers for readability
      skip_char();
    } else if ((ch == 'b' || ch == 'B') && !has_dot && !has_exp) {
      // Check if this is a byte suffix (must be at end of number)
      auto next = peek_char();
      if (!next || (!is_digit(next.value()) && next.value() != '_')) {
        skip_char(); // Consume the 'b' or 'B'
        break;       // Exit the loop, we'll handle byte conversion after
      }
      break;
    } else {
      break;
    }
  }

  std::string_view lexeme = current_lexeme();

  // Check for byte suffix
  bool is_byte = false;
  if (lexeme.size() > 1 && (lexeme.back() == 'b' || lexeme.back() == 'B')) {
    is_byte = true;
    lexeme = lexeme.substr(0, lexeme.size() - 1);
  }

  // Remove underscores for parsing
  std::string clean_lexeme;
  for (char c : lexeme) {
    if (c != '_')
      clean_lexeme += c;
  }

  if (is_byte) {
    // Parse as byte
    int64_t value;
    try {
      size_t idx;
      value = std::stoll(clean_lexeme, &idx);
      if (idx != clean_lexeme.size() || value < 0 || value > 255) {
        return std::unexpected(
            LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                     "Byte literal must be between 0 and 255", currentRange()});
      }
    } catch (const std::exception &) {
      return std::unexpected(LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                                      "Invalid byte literal", currentRange()});
    }
    return make_token(TokenType::YBYTE, static_cast<uint8_t>(value));
  } else if (has_dot || has_exp) {
    // Parse as float
    double value;
    try {
      size_t idx;
      value = std::stod(clean_lexeme, &idx);
      if (idx != clean_lexeme.size()) {
        return std::unexpected(LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                                        "Invalid floating-point number",
                                        currentRange()});
      }
    } catch (const std::exception &) {
      return std::unexpected(LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                                      "Invalid floating-point number",
                                      currentRange()});
    }
    return make_token(TokenType::YFLOAT, value);
  } else {
    // Parse as integer
    int64_t value;
    try {
      size_t idx;
      value = std::stoll(clean_lexeme, &idx);
      if (idx != clean_lexeme.size()) {
        return std::unexpected(LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                                        "Invalid integer", currentRange()});
      }
    } catch (const std::exception &) {
      return std::unexpected(LexError{LexError::Type::INVALID_NUMBER_FORMAT,
                                      "Invalid integer", currentRange()});
    }
    return make_token(TokenType::YINTEGER, value);
  }
}

// Parse escape sequence
std::expected<char32_t, LexError> Lexer::parse_escape_sequence() {
  auto ch_result = advance_char();
  if (!ch_result)
    return std::unexpected(ch_result.error());

  char32_t ch = ch_result.value();
  switch (ch) {
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  case '\\':
    return '\\';
  case '"':
    return '"';
  case '\'':
    return '\'';
  case '0':
    return '\0';
  case 'u':
    return parse_unicode_escape(4);
  case 'U':
    return parse_unicode_escape(8);
  default:
    return std::unexpected(LexError{
        LexError::Type::INVALID_ESCAPE_SEQUENCE,
        std::format("Invalid escape sequence '\\{}'", static_cast<char>(ch)),
        currentRange()});
  }
}

// Parse unicode escape
std::expected<char32_t, LexError> Lexer::parse_unicode_escape(int digits) {
  char32_t value = 0;

  for (int i = 0; i < digits; ++i) {
    auto ch_result = advance_char();
    if (!ch_result)
      return std::unexpected(ch_result.error());

    char32_t ch = ch_result.value();
    if (ch >= '0' && ch <= '9') {
      value = value * 16 + (ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      value = value * 16 + (ch - 'a' + 10);
    } else if (ch >= 'A' && ch <= 'F') {
      value = value * 16 + (ch - 'A' + 10);
    } else {
      return std::unexpected(LexError{LexError::Type::INVALID_ESCAPE_SEQUENCE,
                                      "Invalid hex digit in unicode escape",
                                      currentRange()});
    }
  }

  if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    return std::unexpected(LexError{LexError::Type::INVALID_ESCAPE_SEQUENCE,
                                    "Unicode escape is not a scalar value",
                                    currentRange()});
  }

  return value;
}

// Scan string
std::expected<Token, LexError> Lexer::scan_string() {
  skip_char(); // Skip opening quote
  return scan_string_body();
}

std::expected<Token, LexError> Lexer::scan_string_body() {
  std::string value;

  while (!isAtEnd()) {
    auto ch_result = peek_char();
    if (!ch_result)
      return std::unexpected(ch_result.error());

    char32_t ch = ch_result.value();

    if (ch == '"') {
      skip_char();
      // If this string had no interpolation, return plain YSTRING
      // If it's a segment after interpolation, return YSTRING_PART
      return make_token(in_string_interp_ > 0 ? TokenType::YSTRING_PART
                                              : TokenType::YSTRING,
                        std::move(value));
    } else if (ch == '{') {
      // Braces have two literal forms inside strings.  `{{` escapes a
      // single opening brace, while the empty pair `{}` is retained as
      // text for placeholder-oriented APIs such as Std\Format.  Every
      // other opening brace starts Yona interpolation.
      if (Current + 1 < SourceText.size() && SourceText[Current + 1] == '{') {
        skip_char();
        skip_char();
        value += '{';
      } else if (Current + 1 < SourceText.size() &&
                 SourceText[Current + 1] == '}') {
        skip_char();
        skip_char();
        value += "{}";
      } else {
        skip_char(); // consume '{'
        in_string_interp_++;
        // Emit the string part before the interpolation
        return make_token(TokenType::YSTRING_PART, std::move(value));
      }
    } else if (ch == '}' && Current + 1 < SourceText.size() &&
               SourceText[Current + 1] == '}') {
      skip_char();
      skip_char();
      value += '}';
    } else if (ch == '\\') {
      skip_char();
      auto escaped = parse_escape_sequence();
      if (!escaped)
        return std::unexpected(escaped.error());

      // Convert char32_t to UTF-8
      if (escaped.value() < 0x80) {
        value += static_cast<char>(escaped.value());
      } else if (escaped.value() < 0x800) {
        value += static_cast<char>(0xC0 | (escaped.value() >> 6));
        value += static_cast<char>(0x80 | (escaped.value() & 0x3F));
      } else if (escaped.value() < 0x10000) {
        value += static_cast<char>(0xE0 | (escaped.value() >> 12));
        value += static_cast<char>(0x80 | ((escaped.value() >> 6) & 0x3F));
        value += static_cast<char>(0x80 | (escaped.value() & 0x3F));
      } else {
        value += static_cast<char>(0xF0 | (escaped.value() >> 18));
        value += static_cast<char>(0x80 | ((escaped.value() >> 12) & 0x3F));
        value += static_cast<char>(0x80 | ((escaped.value() >> 6) & 0x3F));
        value += static_cast<char>(0x80 | (escaped.value() & 0x3F));
      }
    } else if (ch == '\n') {
      return std::unexpected(LexError{LexError::Type::UNTERMINATED_STRING,
                                      "Unterminated string literal",
                                      currentRange()});
    } else {
      skip_char();
      // Add the character to the string
      size_t start = Current;
      if (ch < 0x80) {
        value += static_cast<char>(ch);
      } else {
        // Multi-byte UTF-8 character
        size_t len = ch < 0x800 ? 2 : (ch < 0x10000 ? 3 : 4);
        value.append(SourceText.substr(start - len, len));
      }
    }
  }

  return std::unexpected(LexError{LexError::Type::UNTERMINATED_STRING,
                                  "Unterminated string literal",
                                  currentRange()});
}

// Scan character literal
std::expected<Token, LexError> Lexer::scan_character() {
  skip_char(); // Skip opening quote

  auto ch_result = peek_char();
  if (!ch_result)
    return std::unexpected(ch_result.error());

  char32_t ch = ch_result.value();

  if (ch == '\\') {
    skip_char();
    auto escaped = parse_escape_sequence();
    if (!escaped)
      return std::unexpected(escaped.error());
    ch = escaped.value();
  } else if (ch == '\'') {
    return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER_LITERAL,
                                    "Empty character literal", currentRange()});
  } else {
    skip_char();
  }

  // Expect closing quote
  auto close_result = advance_char();
  if (!close_result || close_result.value() != '\'') {
    return std::unexpected(LexError{LexError::Type::INVALID_CHARACTER_LITERAL,
                                    "Unterminated character literal",
                                    currentRange()});
  }

  return make_token(TokenType::YCHARACTER, ch);
}

// Scan symbol
std::expected<Token, LexError> Lexer::scan_symbol() {
  skip_char(); // Skip colon

  // Symbol can be an identifier or operator
  auto ch_result = peek_char();
  if (!ch_result)
    return std::unexpected(ch_result.error());

  if (is_identifier_start(ch_result.value())) {
    while (!isAtEnd()) {
      auto next = peek_char();
      if (!next || !is_identifier_continue(next.value()))
        break;
      skip_char();
    }
  } else {
    // Allow operator symbols like :+, :==, etc.
    while (!isAtEnd()) {
      auto next = peek_char();
      if (!next || !is_operator_char(next.value()))
        break;
      skip_char();
    }
  }

  return make_token(TokenType::YSYMBOL);
}

// Match helper
bool Lexer::match(char32_t expected) {
  if (isAtEnd())
    return false;

  auto ch_result = peek_char();
  if (!ch_result || ch_result.value() != expected)
    return false;

  skip_char();
  return true;
}

// Scan operator
std::expected<Token, LexError> Lexer::scan_operator(char32_t first_char) {
  switch (first_char) {
  case '+':
    if (match('+'))
      return make_token(TokenType::YJOIN);
    return make_token(TokenType::YPLUS);

  case '-':
    if (match('>'))
      return make_token(TokenType::YARROW);
    if (match('-'))
      return make_token(TokenType::YREMOVE);
    if (match('|'))
      return make_token(TokenType::YPREPEND);
    return make_token(TokenType::YMINUS);

  case '*':
    if (match('*'))
      return make_token(TokenType::YPOWER);
    return make_token(TokenType::YSTAR);

  case '/':
    return make_token(TokenType::YSLASH);

  case '%':
    return make_token(TokenType::YPERCENT);

  case '=':
    if (match('='))
      return make_token(TokenType::YEQ);
    if (match('>'))
      return make_token(TokenType::YFAT_ARROW);
    return make_token(TokenType::YASSIGN);

  case '!':
    if (match('='))
      return make_token(TokenType::YNEQ);
    return make_token(TokenType::YNOT);

  case '<':
    if (match('='))
      return make_token(TokenType::YLTE);
    if (match('<'))
      return make_token(TokenType::YLEFT_SHIFT);
    if (match('|'))
      return make_token(TokenType::YPIPE_LEFT);
    return make_token(TokenType::YLT);

  case '>':
    if (match('='))
      return make_token(TokenType::YGTE);
    if (match('>')) {
      if (match('>'))
        return make_token(TokenType::YZERO_FILL_RIGHT_SHIFT);
      return make_token(TokenType::YRIGHT_SHIFT);
    }
    return make_token(TokenType::YGT);

  case '&':
    if (match('&'))
      return make_token(TokenType::YAND);
    return make_token(TokenType::YBIT_AND);

  case '|':
    if (match('|'))
      return make_token(TokenType::YOR);
    if (match('>'))
      return make_token(TokenType::YPIPE_RIGHT);
    if (match('-'))
      return make_token(TokenType::YAPPEND);
    return make_token(TokenType::YPIPE);

  case '^':
    return make_token(TokenType::YBIT_XOR);

  case '~':
    return make_token(TokenType::YBIT_NOT);

  case '@':
    return make_token(TokenType::YAT);

  default:
    return std::unexpected(
        LexError{LexError::Type::INVALID_CHARACTER,
                 std::format("Unexpected operator character '{}'",
                             static_cast<char>(first_char)),
                 currentRange()});
  }
}

// Check if a YNEWLINE should be suppressed after this token type.
// Binary operators and continuation tokens naturally expect more input.
static bool suppresses_following_newline(TokenType type) {
  switch (type) {
  // Binary operators
  case TokenType::YPLUS:
  case TokenType::YMINUS:
  case TokenType::YSTAR:
  case TokenType::YSLASH:
  case TokenType::YPERCENT:
  case TokenType::YPOWER:
  case TokenType::YEQ:
  case TokenType::YNEQ:
  case TokenType::YLT:
  case TokenType::YGT:
  case TokenType::YLTE:
  case TokenType::YGTE:
  case TokenType::YAND:
  case TokenType::YOR:
  case TokenType::YBIT_AND:
  case TokenType::YPIPE:
  case TokenType::YBIT_XOR:
  case TokenType::YBIT_NOT:
  case TokenType::YLEFT_SHIFT:
  case TokenType::YRIGHT_SHIFT:
  case TokenType::YZERO_FILL_RIGHT_SHIFT:
  case TokenType::YPIPE_LEFT:
  case TokenType::YPIPE_RIGHT:
  case TokenType::YJOIN:
  case TokenType::YCONS:
  case TokenType::YCONS_RIGHT:
  // Continuation tokens
  case TokenType::YARROW:
  case TokenType::YFAT_ARROW:
  case TokenType::YASSIGN:
  case TokenType::YCOMMA:
  case TokenType::YBACKSLASH:
  // Keywords that expect a following expression
  case TokenType::YIN:
  case TokenType::YTHEN:
  case TokenType::YELSE:
  case TokenType::YOF:
  case TokenType::YDO:
  // Start of input / already a newline
  case TokenType::YEOF_TOKEN:
  case TokenType::YNEWLINE:
    return true;
  default:
    return false;
  }
}

// Main token scanning
std::expected<Token, LexError> Lexer::scan_token() {
  skip_whitespace_and_comments();

  // Handle newlines at bracket depth 0
  if (!isAtEnd()) {
    auto ch_peek = peek_char();
    if (ch_peek && ch_peek.value() == '\n') {
      // Consume all consecutive newlines and horizontal whitespace
      while (!isAtEnd()) {
        auto c = peek_char();
        if (!c)
          break;
        if (c.value() == '\n' || c.value() == ' ' || c.value() == '\t' ||
            c.value() == '\r') {
          skip_char();
        } else if (c.value() == '#') {
          // Skip comment on blank line
          skip_char();
          while (!isAtEnd()) {
            auto n = peek_char();
            if (!n || n.value() == '\n')
              break;
            skip_char();
          }
        } else {
          break;
        }
      }

      // Don't emit YNEWLINE if the previous token suppresses it,
      // or if the next non-whitespace, non-comment character begins
      // a binary continuation operator (`|>`, `<|`, `++`, `==`,
      // `&&`, `||`, `::`, `<=`, `>=`). This lets users break long
      // pipe / arithmetic chains over multiple lines.
      //
      // We avoid single-char operators like `+`, `-`, `*`, `/`,
      // `<`, `>`, `%` because:
      //   * `/` collides with the comment introducer.
      //   * `+`/`-` could begin a unary literal on the next line.
      //   * `<`/`>` could begin a different syntactic form later.
      // The compound forms above are unambiguous.
      bool next_is_continuation = false;
      if (!isAtEnd()) {
        size_t scan = Current;
        // Skip any comments that follow the newline run.
        while (scan < SourceText.length()) {
          char ch = SourceText[scan];
          if (ch == ' ' || ch == '\t') {
            scan++;
            continue;
          }
          if (ch == '#') {
            while (scan < SourceText.length() && SourceText[scan] != '\n')
              scan++;
            continue;
          }
          if (ch == '/' && scan + 1 < SourceText.length() &&
              SourceText[scan + 1] == '*') {
            scan += 2;
            int depth = 1;
            while (scan < SourceText.length() && depth > 0) {
              if (SourceText[scan] == '/' && scan + 1 < SourceText.length() &&
                  SourceText[scan + 1] == '*') {
                depth++;
                scan += 2;
              } else if (SourceText[scan] == '*' &&
                         scan + 1 < SourceText.length() &&
                         SourceText[scan + 1] == '/') {
                depth--;
                scan += 2;
              } else
                scan++;
            }
            continue;
          }
          break;
        }
        if (scan < SourceText.length()) {
          char c1 = SourceText[scan];
          char c2 = (scan + 1 < SourceText.length()) ? SourceText[scan + 1] : 0;
          if ((c1 == '|' && c2 == '>') || // |>
              (c1 == '<' && c2 == '|') || // <|
              (c1 == '+' && c2 == '+') || // ++
              (c1 == '=' && c2 == '=') || // ==
              (c1 == '!' && c2 == '=') || // !=
              (c1 == '&' && c2 == '&') || // &&
              (c1 == '|' && c2 == '|') || // ||
              (c1 == ':' && c2 == ':') || // ::
              (c1 == '<' && c2 == '=') || // <=
              (c1 == '>' && c2 == '='))   // >=
            next_is_continuation = true;
        }
      }
      if (!suppresses_following_newline(last_emitted_) &&
          !next_is_continuation) {
        mark_token_start();
        return make_token(TokenType::YNEWLINE);
      }
      // Otherwise, continue to scan the next real token
      skip_whitespace_and_comments();
    }
  }

  if (isAtEnd()) {
    mark_token_start(); // Mark position for EOF
    return make_token(TokenType::YEOF_TOKEN);
  }

  mark_token_start();

  auto ch_result = advance_char();
  if (!ch_result)
    return std::unexpected(ch_result.error());

  char32_t ch = ch_result.value();

  // Special case for single underscore
  if (ch == '_') {
    // Check if it's just a single underscore or part of an identifier
    if (isAtEnd() || !is_identifier_continue(peek_char().value_or(0))) {
      return make_token(TokenType::YUNDERSCORE);
    }
    // Otherwise, it's the start of an identifier
    return scan_identifier();
  }

  // Identifiers and keywords
  if (is_identifier_start(ch)) {
    return scan_identifier();
  }

  // Numbers
  if (is_digit(ch)) {
    Current = TokenStart + 1; // Reset to after first char (safe for digits
                              // which are always 1 byte)
    return scan_number();
  }

  // Helper to track state on token emission
  auto emit = [this](Token tok) -> std::expected<Token, LexError> {
    switch (tok.type) {
    case TokenType::YLPAREN:
    case TokenType::YLBRACKET:
    case TokenType::YLBRACE:
      bracket_depth_++;
      break;
    case TokenType::YRPAREN:
    case TokenType::YRBRACKET:
    case TokenType::YRBRACE:
      if (bracket_depth_ > 0)
        bracket_depth_--;
      break;
    default:
      break;
    }
    return tok;
  };

  auto rewind_to_token_start = [this] {
    Current = TokenStart;
    Line = TokenStartLine;
    Column = TokenStartColumn;
  };

  // Single character tokens
  switch (ch) {
  case '(':
    return emit(make_token(TokenType::YLPAREN));
  case ')':
    return emit(make_token(TokenType::YRPAREN));
  case '[':
    return emit(make_token(TokenType::YLBRACKET));
  case ']':
    return emit(make_token(TokenType::YRBRACKET));
  case '{':
    return emit(make_token(TokenType::YLBRACE));
  case '}':
    if (in_string_interp_ > 0) {
      // Closing interpolation — resume string scanning
      in_string_interp_--;
      mark_token_start();
      return scan_string_body();
    }
    return emit(make_token(TokenType::YRBRACE));
  case ',':
    return emit(make_token(TokenType::YCOMMA));
  case ';':
    return make_token(TokenType::YNEWLINE);
  case '.':
    if (match('.'))
      return make_token(TokenType::YDOTDOT);
    return make_token(TokenType::YDOT);
  case '\\':
    return make_token(TokenType::YBACKSLASH);
  case '"':
    rewind_to_token_start();
    return scan_string();
  case '\'':
    rewind_to_token_start();
    return scan_character();
  case ':':
    // Could be COLON, CONS (::), CONS_RIGHT (:>), or SYMBOL
    if (!isAtEnd()) {
      auto next = peek_char();
      if (next) {
        if (next.value() == ':') {
          skip_char();
          return make_token(TokenType::YCONS);
        } else if (next.value() == '>') {
          skip_char();
          return make_token(TokenType::YCONS_RIGHT);
        } else if (is_identifier_start(next.value()) ||
                   is_operator_char(next.value())) {
          Current = TokenStart; // Reset for symbol scanning
          return scan_symbol();
        }
      }
    }
    return make_token(TokenType::YCOLON);

  // Operators
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '=':
  case '!':
  case '<':
  case '>':
  case '&':
  case '|':
  case '^':
  case '~':
  case '@':
    Current = TokenStart + 1; // Reset to after first char
    return scan_operator(ch);

  default:
    return std::unexpected(LexError{
        LexError::Type::INVALID_CHARACTER,
        std::format("Unexpected character '{}'", static_cast<char>(ch)),
        currentRange()});
  }
}

// Public interface
std::expected<Token, LexError> Lexer::nextToken() {
  auto result = scan_token();
  if (!result) {
    result.error().Sources = Sources;
    return result;
  }
  if (result) {
    last_emitted_ = result.value().type;
    // Track block depth so newlines stay significant inside
    // case/do/with/handle bodies even within parens.
    switch (result.value().type) {
    case TokenType::YCASE:
    case TokenType::YDO:
    case TokenType::YWITH:
    case TokenType::YHANDLE:
      block_depth_++;
      break;
    case TokenType::YEND:
      if (block_depth_ > 0)
        block_depth_--;
      break;
    default:
      break;
    }
  }
  return result;
}

std::expected<Token, LexError> Lexer::peekToken() {
  // Save state
  size_t saved_current = Current;
  size_t saved_line = Line;
  size_t saved_column = Column;
  int saved_bracket_depth = bracket_depth_;
  int saved_block_depth = block_depth_;
  TokenType saved_last_emitted = last_emitted_;
  int saved_string_interp = in_string_interp_;

  auto token = scan_token();
  if (!token)
    token.error().Sources = Sources;

  // Restore state
  Current = saved_current;
  Line = saved_line;
  Column = saved_column;
  bracket_depth_ = saved_bracket_depth;
  block_depth_ = saved_block_depth;
  last_emitted_ = saved_last_emitted;
  in_string_interp_ = saved_string_interp;

  return token;
}

std::expected<std::vector<Token>, std::vector<LexError>> Lexer::tokenize() {
  std::vector<Token> tokens;
  std::vector<LexError> errors;

  while (true) {
    auto token_result = nextToken();
    if (token_result) {
      tokens.push_back(token_result.value());
      if (token_result.value().type == TokenType::YEOF_TOKEN) {
        break;
      }
    } else {
      errors.push_back(token_result.error());
      // Try to recover by skipping the problematic character
      if (!isAtEnd()) {
        skip_char();
      }
    }
  }

  if (!errors.empty()) {
    return std::unexpected(std::move(errors));
  }

  return tokens;
}

// Token type to string conversion
std::string_view token_type_to_string(TokenType type) noexcept {
  switch (type) {
  case TokenType::YINTEGER:
    return "INTEGER";
  case TokenType::YFLOAT:
    return "FLOAT";
  case TokenType::YSTRING:
    return "STRING";
  case TokenType::YCHARACTER:
    return "CHARACTER";
  case TokenType::YSYMBOL:
    return "SYMBOL";
  case TokenType::YTRUE:
    return "TRUE";
  case TokenType::YFALSE:
    return "FALSE";
  case TokenType::YUNIT:
    return "UNIT";
  case TokenType::YIDENTIFIER:
    return "IDENTIFIER";
  case TokenType::YMODULE:
    return "MODULE";
  case TokenType::YIMPORT:
    return "IMPORT";
  case TokenType::YFROM:
    return "FROM";
  case TokenType::YAS:
    return "AS";
  case TokenType::YEXPORT:
    return "EXPORT";
  case TokenType::YLET:
    return "LET";
  case TokenType::YIN:
    return "IN";
  case TokenType::YIF:
    return "IF";
  case TokenType::YTHEN:
    return "THEN";
  case TokenType::YELSE:
    return "ELSE";
  case TokenType::YCASE:
    return "CASE";
  case TokenType::YOF:
    return "OF";
  case TokenType::YDO:
    return "DO";
  case TokenType::YEND:
    return "END";
  case TokenType::YTRY:
    return "TRY";
  case TokenType::YCATCH:
    return "CATCH";
  case TokenType::YRAISE:
    return "RAISE";
  case TokenType::YWITH:
    return "WITH";
  case TokenType::YEXTERN:
    return "EXTERN";
  case TokenType::YFUN:
    return "FUN";
  case TokenType::YLAMBDA:
    return "LAMBDA";
  case TokenType::YRECORD:
    return "RECORD";
  case TokenType::YTYPE:
    return "TYPE";
  case TokenType::YTRAIT:
    return "TRAIT";
  case TokenType::YINSTANCE:
    return "INSTANCE";
  case TokenType::YPLUS:
    return "PLUS";
  case TokenType::YMINUS:
    return "MINUS";
  case TokenType::YSTAR:
    return "STAR";
  case TokenType::YSLASH:
    return "SLASH";
  case TokenType::YPERCENT:
    return "PERCENT";
  case TokenType::YPOWER:
    return "POWER";
  case TokenType::YEQ:
    return "EQ";
  case TokenType::YNEQ:
    return "NEQ";
  case TokenType::YLT:
    return "LT";
  case TokenType::YGT:
    return "GT";
  case TokenType::YLTE:
    return "LTE";
  case TokenType::YGTE:
    return "GTE";
  case TokenType::YAND:
    return "AND";
  case TokenType::YOR:
    return "OR";
  case TokenType::YNOT:
    return "NOT";
  case TokenType::YBIT_AND:
    return "BIT_AND";
  case TokenType::YBIT_OR:
    return "BIT_OR";
  case TokenType::YBIT_XOR:
    return "BIT_XOR";
  case TokenType::YBIT_NOT:
    return "BIT_NOT";
  case TokenType::YLEFT_SHIFT:
    return "LEFT_SHIFT";
  case TokenType::YRIGHT_SHIFT:
    return "RIGHT_SHIFT";
  case TokenType::YZERO_FILL_RIGHT_SHIFT:
    return "ZERO_FILL_RIGHT_SHIFT";
  case TokenType::YASSIGN:
    return "ASSIGN";
  case TokenType::YARROW:
    return "ARROW";
  case TokenType::YFAT_ARROW:
    return "FAT_ARROW";
  case TokenType::YLPAREN:
    return "LPAREN";
  case TokenType::YRPAREN:
    return "RPAREN";
  case TokenType::YLBRACKET:
    return "LBRACKET";
  case TokenType::YRBRACKET:
    return "RBRACKET";
  case TokenType::YLBRACE:
    return "LBRACE";
  case TokenType::YRBRACE:
    return "RBRACE";
  case TokenType::YCOMMA:
    return "COMMA";
  case TokenType::YSEMICOLON:
    return "SEMICOLON";
  case TokenType::YCOLON:
    return "COLON";
  case TokenType::YDOT:
    return "DOT";
  case TokenType::YDOTDOT:
    return "DOTDOT";
  case TokenType::YPIPE:
    return "PIPE";
  case TokenType::YAT:
    return "AT";
  case TokenType::YUNDERSCORE:
    return "UNDERSCORE";
  case TokenType::YBACKSLASH:
    return "BACKSLASH";
  case TokenType::YCONS:
    return "CONS";
  case TokenType::YPIPE_LEFT:
    return "PIPE_LEFT";
  case TokenType::YPIPE_RIGHT:
    return "PIPE_RIGHT";
  case TokenType::YJOIN:
    return "JOIN";
  case TokenType::YREMOVE:
    return "REMOVE";
  case TokenType::YPREPEND:
    return "PREPEND";
  case TokenType::YAPPEND:
    return "APPEND";
  case TokenType::YEOF_TOKEN:
    return "EOF";
  case TokenType::YNEWLINE:
    return "NEWLINE";
  case TokenType::YSTRING_PART:
    return "STRING_PART";
  case TokenType::YINTERP_END:
    return "INTERP_END";
  case TokenType::YERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

} // namespace yona::lexer
