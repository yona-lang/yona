//
// Created by Adam Kovari on 17.12.2024.
//

#ifndef YONA_SYNTAX_PARSER_H
#define YONA_SYNTAX_PARSER_H

#include "yona/Support/Export.h"
#include "yona/Support/SourceManager.h"
#include "yona/Syntax/Ast.h"
#include "yona/Syntax/Lexer.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Disable MSVC warning C4251 about STL types in exported class interfaces
// This is safe for our use case as both the DLL and clients use the same STL
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

namespace yona::parser {
using ast::ExprNode;
using ast::ModuleDecl;
using lexer::TokenType;
using std::expected;
using std::move;
using std::optional;
using std::shared_ptr;
using std::size_t;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;

// Parser configuration for performance tuning
struct ParserConfig {
  size_t max_lookahead = 3;            // Maximum tokens to look ahead
  size_t initial_ast_pool_size = 1024; // Pre-allocate AST nodes
  bool enable_error_recovery = true;   // Try to recover from parse errors
  bool enable_optimizations = true;    // Enable parsing optimizations
};

// Parse error with detailed information
struct ParseError {
  enum class Type {
    UNEXPECTED_TOKEN,
    UNEXPECTED_EOF,
    INVALID_SYNTAX,
    INVALID_NUMBER,
    INVALID_STRING,
    INVALID_PATTERN,
    MISSING_TOKEN,
    AMBIGUOUS_PARSE
  };

  Type ErrorType;
  string Message;
  SourceRange Range;
  optional<TokenType> ExpectedToken;
  optional<TokenType> ActualToken;
  shared_ptr<const SourceManager> Sources;

  [[nodiscard]] string format() const;
};

// Forward declaration for implementation
class ParserImpl;

/// A parsed module together with the immutable source storage referenced by
/// every SourceRange in its AST. The unique root owns all descendant nodes.
/// get() and operator->() return mutable borrowed pointers invalidated by move,
/// replacement, or destruction of this result.
struct ParsedModule final {
  shared_ptr<SourceManager> Sources;
  SourceId Source;
  unique_ptr<ModuleDecl> Module;

  [[nodiscard]] ModuleDecl *get() const noexcept { return Module.get(); }
  [[nodiscard]] ModuleDecl *operator->() const noexcept { return Module.get(); }
};

/// A parsed expression together with the immutable source storage referenced
/// by every SourceRange in its AST. The unique root owns all descendant nodes.
/// get() and operator->() return mutable borrowed pointers invalidated by move,
/// replacement, or destruction of this result.
struct ParsedExpression final {
  shared_ptr<SourceManager> Sources;
  SourceId Source;
  unique_ptr<ExprNode> Expression;

  [[nodiscard]] ExprNode *get() const noexcept { return Expression.get(); }
  [[nodiscard]] ExprNode *operator->() const noexcept {
    return Expression.get();
  }
};

/// Reusable, stateful recursive-descent parser.
///
/// Source and filename arguments are moved into a fresh SourceManager. A
/// successful result owns that manager and its AST; on failure ParseError
/// values retain the manager and no partial AST is exposed. Syntax failures are
/// returned through std::expected, while allocation failures may still throw.
/// Constructor registrations are copied into parser state. Calls on one Parser
/// must be serialized; distinct parsers have no shared mutable parser state. A
/// moved-from parser may only be destroyed or assigned a new parser value.
class YONA_API Parser {
private:
  unique_ptr<ParserImpl> impl_;

public:
  explicit Parser(ParserConfig config = {});
  ~Parser();

  // Disable copy but allow move
  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;
  Parser(Parser &&) noexcept;
  Parser &operator=(Parser &&) noexcept;

  [[nodiscard]] expected<ParsedModule, vector<ParseError>>
  parseModule(string source, string filename = "<input>");

  [[nodiscard]] expected<ParsedExpression, vector<ParseError>>
  parseExpression(string source, string filename = "<input>");

  /// Copy or replace ADT constructor metadata used by later parses.
  void register_constructor(const string &name, const string &type_name,
                            int tag, int arity,
                            const vector<string> &field_names = {});

  /// Register prelude ADT constructors (Linear, Option, Result).
  /// Call before parsing to make prelude types available for pattern matching.
  void register_prelude_constructors();
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace yona::parser

#endif /* YONA_SYNTAX_PARSER_H */
