//
// Created by Adam Kovari on 17.12.2024.
//
// Parser public facade + ParserImpl constructor/destructor and token helpers.
//

#include "parser/ParserImpl.h"

namespace yona::parser {

// --- ParserImpl constructor ---
ParserImpl::ParserImpl(ParserConfig config) : config_(config) {
    ast_pool_.reserve(config_.initial_ast_pool_size);
}

// --- ParserImpl public entry points ---
expected<unique_ptr<ModuleDecl>, vector<ParseError>>
ParserImpl::parse_module(string_view source, string_view filename) {
    // Tokenize
    Lexer lexer(source, filename);
    auto token_result = lexer.tokenize();
    if (!token_result) {
        // Convert lexer errors to parse errors
        vector<ParseError> parse_errors;
        for (const auto& lex_error : token_result.error()) {
            parse_errors.push_back(ParseError{
                ParseError::Type::INVALID_SYNTAX,
                lex_error.message,
                lex_error.location,
                {},
                {}
            });
        }
        return unexpected(std::move(parse_errors));
    }

    tokens_ = std::move(token_result.value());
    filename_ = filename;
    source_ = source;
    current_ = 0;
    errors_.clear();

    auto module = parse_module_internal();

    if (!errors_.empty()) {
        return unexpected(std::move(errors_));
    }

    return std::move(module);
}

expected<unique_ptr<ExprNode>, vector<ParseError>>
ParserImpl::parse_expression(string_view source, string_view filename) {
    // Tokenize
    Lexer lexer(source, filename);
    auto token_result = lexer.tokenize();
    if (!token_result) {
        // Convert lexer errors to parse errors
        vector<ParseError> parse_errors;
        for (const auto& lex_error : token_result.error()) {
            parse_errors.push_back(ParseError{
                ParseError::Type::INVALID_SYNTAX,
                lex_error.message,
                lex_error.location,
                {},
                {}
            });
        }
        return unexpected(std::move(parse_errors));
    }

    tokens_ = std::move(token_result.value());
    filename_ = filename;
    source_ = source;
    current_ = 0;
    errors_.clear();

    auto expr = parse_expr();

    skip_newlines();
    if (!is_at_end()) {
        error(ParseError::Type::UNEXPECTED_TOKEN, "Unexpected token after expression");
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

const Token& ParserImpl::peek(size_t offset) const {
    if (current_ + offset >= tokens_.size()) {
        return tokens_.back(); // EOF token
    }
    return tokens_[current_ + offset];
}

const Token& ParserImpl::previous() const {
    return tokens_[current_ - 1];
}

const Token& ParserImpl::advance() {
    if (!is_at_end()) current_++;
    return previous();
}

bool ParserImpl::check(TokenType type) const {
    if (is_at_end()) return false;
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
    while (check(TokenType::YNEWLINE)) advance();
}

bool ParserImpl::expect(TokenType type, const string& message) {
    if (check(type)) {
        advance();
        return true;
    }

    error(ParseError::Type::MISSING_TOKEN, message, type);
    return false;
}

void ParserImpl::error(ParseError::Type type, const string& message,
           optional<TokenType> expected) {
    errors_.push_back(ParseError{
        type,
        message,
        peek().location,
        expected,
        peek().type
    });
}

void ParserImpl::synchronize() {
    advance();

    while (!is_at_end()) {
        if (previous().type == TokenType::YSEMICOLON || previous().type == TokenType::YNEWLINE) return;

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

SourceLocation ParserImpl::token_location(const Token& token) const {
    return token.location;
}

SourceLocation ParserImpl::current_location() const {
    return token_location(peek());
}

SourceLocation ParserImpl::previous_location() const {
    if (current_ == 0) {
        return SourceLocation::unknown();
    }
    return token_location(previous());
}

const Token& ParserImpl::current() const {
    return peek();
}

bool ParserImpl::check_ahead(TokenType type) const {
    return peek(1).type == type;
}

// --- ParseError formatting ---
string ParseError::format() const {
    string result = location.to_string() + ": " + message;

    if (expected_token) {
        result += " (expected " + string(token_type_to_string(*expected_token)) + ")";
    }

    if (actual_token) {
        result += " (got " + string(token_type_to_string(*actual_token)) + ")";
    }

    return result;
}

// --- Parser public interface (facade) ---
Parser::Parser(ParserConfig config)
    : impl_(make_unique<ParserImpl>(std::move(config))) {}

Parser::~Parser() = default;

Parser::Parser(Parser&&) noexcept = default;
Parser& Parser::operator=(Parser&&) noexcept = default;

expected<unique_ptr<ModuleDecl>, vector<ParseError>>
Parser::parse_module(string_view source, string_view filename) {
    return impl_->parse_module(source, filename);
}

expected<unique_ptr<ExprNode>, vector<ParseError>>
Parser::parse_expression(string_view source, string_view filename) {
    return impl_->parse_expression(source, filename);
}

void Parser::register_constructor(const string& name, const string& type_name, int tag, int arity,
                                  const vector<string>& field_names) {
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

// Legacy interface implementation
ParseResult Parser::parse_input(const vector<string>& module_name) {
    string file_path = module_location(module_name);

    try {
        // Read file
        ifstream file(file_path);
        if (!file) {
            auto ast_ctx = AstContext();
            auto error = make_shared<yona_error>(
                SourceLocation::unknown(),
                yona_error::IO,
                "Could not open file: " + file_path
            );
            ast_ctx.addError(error);
            return ParseResult{false, nullptr, nullptr, ast_ctx};
        }

        stringstream buffer;
        buffer << file.rdbuf();
        string source = buffer.str();

        // Parse module
        auto result = parse_module(source, file_path);

        if (result) {
            auto& module = result.value();
            auto ast_ctx = AstContext();

            // Convert unique_ptr to shared_ptr for legacy interface
            shared_ptr<AstNode> node(module.release());

            // Type is inferred separately, not during parsing
            return ParseResult{!ast_ctx.hasErrors(), node, nullptr, ast_ctx};
        } else {
            // Convert parse errors to AstContext errors
            auto ast_ctx = AstContext();
            for (const auto& parse_error : result.error()) {
                auto error = make_shared<yona_error>(
                    parse_error.location,
                    yona_error::SYNTAX,
                    parse_error.message
                );
                ast_ctx.addError(error);
            }
            return ParseResult{false, nullptr, nullptr, ast_ctx};
        }
    } catch (const exception& e) {
        auto ast_ctx = AstContext();
        auto error = make_shared<yona_error>(
            SourceLocation::unknown(),
            yona_error::COMPILER,
            string("Parser exception: ") + e.what()
        );
        ast_ctx.addError(error);
        return ParseResult{false, nullptr, nullptr, ast_ctx};
    }
}

ParseResult Parser::parse_input(istream& stream) {
    try {
        // Read stream
        stringstream buffer;
        buffer << stream.rdbuf();
        string source = buffer.str();

        // Use this parser instance
        auto result = parse_expression(source, "<stream>");

        if (result) {
            auto ast_ctx = AstContext();

            // Extract the expression and its context before moving
            auto expr_ptr = result.value().release();
            auto source_ctx = expr_ptr->source_context;

            // Wrap expression in MainNode for evaluation
            auto main_node = new MainNode(source_ctx, expr_ptr);

            // Create shared_ptr directly
            shared_ptr<AstNode> node(main_node);
            return ParseResult{!ast_ctx.hasErrors(), node, nullptr, ast_ctx};
        } else {
            // Convert parse errors to AstContext errors
            auto ast_ctx = AstContext();
            for (const auto& parse_error : result.error()) {
                auto error = make_shared<yona_error>(
                    parse_error.location,
                    yona_error::SYNTAX,
                    parse_error.message
                );
                ast_ctx.addError(error);
            }
            return ParseResult{false, nullptr, nullptr, ast_ctx};
        }
    } catch (const exception& e) {
        auto ast_ctx = AstContext();
        auto error = make_shared<yona_error>(
            SourceLocation::unknown(),
            yona_error::COMPILER,
            string("Parser exception: ") + e.what()
        );
        ast_ctx.addError(error);
        return ParseResult{false, nullptr, nullptr, ast_ctx};
    }
}

} // namespace yona::parser
