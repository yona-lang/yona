//
// Created by Adam Kovari on 17.12.2024.
//
// Parser public facade + ParserImpl constructor/destructor and token helpers.
//

#include "ParserImpl.h"

namespace yona::parser {

// --- ParserImpl constructor ---
ParserImpl::ParserImpl(ParserConfig config) : config_(config) {
  ast_pool_.reserve(config_.initial_ast_pool_size);
}

// --- ParserImpl public entry points ---
expected<unique_ptr<ModuleDecl>, vector<ParseError>>
ParserImpl::parseModule(shared_ptr<SourceManager> Sources, SourceId Source) {
  sources_ = std::move(Sources);
  source_id_ = Source;
  source_ = sources_->text(source_id_);
  current_ = 0;
  errors_.clear();

  // Tokenize
  Lexer lexer(sources_, source_id_);
  auto token_result = lexer.tokenize();
  if (!token_result) {
    // Convert lexer errors to parse errors
    vector<ParseError> parse_errors;
    for (const auto &lex_error : token_result.error()) {
      parse_errors.push_back(ParseError{ParseError::Type::INVALID_SYNTAX,
                                        lex_error.message,
                                        lex_error.Range,
                                        {},
                                        {},
                                        sources_});
    }
    return unexpected(std::move(parse_errors));
  }

  tokens_ = std::move(token_result.value());

  auto module = parse_module_internal();

  if (!errors_.empty()) {
    return unexpected(std::move(errors_));
  }

  return std::move(module);
}

expected<unique_ptr<ExprNode>, vector<ParseError>>
ParserImpl::parseExpression(shared_ptr<SourceManager> Sources,
                            SourceId Source) {
  sources_ = std::move(Sources);
  source_id_ = Source;
  source_ = sources_->text(source_id_);
  current_ = 0;
  errors_.clear();

  // Tokenize
  Lexer lexer(sources_, source_id_);
  auto token_result = lexer.tokenize();
  if (!token_result) {
    // Convert lexer errors to parse errors
    vector<ParseError> parse_errors;
    for (const auto &lex_error : token_result.error()) {
      parse_errors.push_back(ParseError{ParseError::Type::INVALID_SYNTAX,
                                        lex_error.message,
                                        lex_error.Range,
                                        {},
                                        {},
                                        sources_});
    }
    return unexpected(std::move(parse_errors));
  }

  tokens_ = std::move(token_result.value());

  auto expr = parse_expr();

  skip_newlines();
  if (!is_at_end()) {
    error(ParseError::Type::UNEXPECTED_TOKEN,
          "Unexpected token after expression");
  }

  if (!expr || !errors_.empty()) {
    return unexpected(std::move(errors_));
  }

  return std::move(expr);
}

// --- Token access helpers ---
bool ParserImpl::is_at_end() const {
  return current_ >= tokens_.size() || peek().type == TokenType::YEOF_TOKEN;
}

const Token &ParserImpl::peek(size_t offset) const {
  if (current_ + offset >= tokens_.size()) {
    return tokens_.back(); // EOF token
  }
  return tokens_[current_ + offset];
}

const Token &ParserImpl::previous() const { return tokens_[current_ - 1]; }

const Token &ParserImpl::advance() {
  if (!is_at_end())
    current_++;
  return previous();
}

bool ParserImpl::check(TokenType type) const {
  if (is_at_end())
    return false;
  return peek().type == type;
}

bool ParserImpl::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

void ParserImpl::skip_newlines() {
  while (check(TokenType::YNEWLINE))
    advance();
}

bool ParserImpl::expect(TokenType type, const string &message) {
  if (check(type)) {
    advance();
    return true;
  }

  error(ParseError::Type::MISSING_TOKEN, message, type);
  return false;
}

void ParserImpl::error(ParseError::Type type, const string &message,
                       optional<TokenType> expected) {
  errors_.push_back(
      ParseError{type, message, peek().Range, expected, peek().type, sources_});
}

void ParserImpl::synchronize() {
  advance();

  while (!is_at_end()) {
    if (previous().type == TokenType::YSEMICOLON ||
        previous().type == TokenType::YNEWLINE)
      return;

    switch (peek().type) {
    case TokenType::YMODULE:
    case TokenType::YLET:
    case TokenType::YIF:
    case TokenType::YFUN:
    case TokenType::YCASE:
    case TokenType::YTRY:
    case TokenType::YDO:
    case TokenType::YTYPE:
      return;
    default:
      advance();
    }
  }
}

SourceRange ParserImpl::token_location(const Token &token) const {
  return token.Range;
}

SourceRange ParserImpl::current_location() const {
  return token_location(peek());
}

SourceRange ParserImpl::previous_location() const {
  if (current_ == 0) {
    return SourceRange::unknown();
  }
  return token_location(previous());
}

const Token &ParserImpl::current() const { return peek(); }

bool ParserImpl::check_ahead(TokenType type) const {
  return peek(1).type == type;
}

// --- ParseError formatting ---
string ParseError::format() const {
  string result =
      (Sources ? Sources->format(Range) : string("<unknown>:0:0")) + ": " +
      Message;

  if (ExpectedToken) {
    result +=
        " (expected " + string(token_type_to_string(*ExpectedToken)) + ")";
  }

  if (ActualToken) {
    result += " (got " + string(token_type_to_string(*ActualToken)) + ")";
  }

  return result;
}

// --- Parser public interface (facade) ---
Parser::Parser(ParserConfig config)
    : impl_(make_unique<ParserImpl>(std::move(config))) {}

Parser::~Parser() = default;

Parser::Parser(Parser &&) noexcept = default;
Parser &Parser::operator=(Parser &&) noexcept = default;

expected<ParsedModule, vector<ParseError>>
Parser::parseModule(string source, string filename) {
  auto Sources = make_shared<SourceManager>();
  const SourceId Source =
      Sources->addSource(std::move(filename), std::move(source));
  auto Result = impl_->parseModule(Sources, Source);
  if (!Result)
    return unexpected(std::move(Result.error()));
  return ParsedModule{std::move(Sources), Source, std::move(*Result)};
}

expected<ParsedExpression, vector<ParseError>>
Parser::parseExpression(string source, string filename) {
  auto Sources = make_shared<SourceManager>();
  const SourceId Source =
      Sources->addSource(std::move(filename), std::move(source));
  auto Result = impl_->parseExpression(Sources, Source);
  if (!Result)
    return unexpected(std::move(Result.error()));
  return ParsedExpression{std::move(Sources), Source, std::move(*Result)};
}

void Parser::register_constructor(const string &name, const string &type_name,
                                  int tag, int arity,
                                  const vector<string> &field_names) {
  impl_->constructor_registry_[name] = {type_name, tag, arity, field_names};
}

void Parser::register_prelude_constructors() {
  register_constructor("Linear", "Linear", 0, 1);
  register_constructor("Some", "Option", 0, 1);
  register_constructor("None", "Option", 1, 0);
  register_constructor("Ok", "Result", 0, 1);
  register_constructor("Err", "Result", 1, 1);
  register_constructor("Iterator", "Iterator", 0, 1);
}

} // namespace yona::parser
