//
// ParserImpl.h — Private implementation header for the Parser PIMPL.
// NOT a public header. Only included by src/Parser.cpp and src/parser/*.cpp.
//

#pragma once

#include "Parser.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <functional>
#include <ranges>

#include "utils.h"

namespace yona::parser {

/// One function clause: parameter patterns, optional `@borrow` flags, and bodies.
struct ParsedFunctionClause {
    vector<PatternNode*> patterns;
    vector<bool> param_borrow;
    vector<unique_ptr<FunctionBody>> bodies;
};

// Binary operator precedence (higher = tighter binding)
enum class Precedence : int {
    LOWEST = 0,
    PIPE = 1,          // |>, <|
    LOGICAL_OR = 2,    // ||
    LOGICAL_AND = 3,   // &&
    YIN = 4,            // in
    BITWISE_OR = 5,    // |
    BITWISE_XOR = 6,   // ^
    BITWISE_AND = 7,   // &
    EQUALITY = 8,      // ==, !=
    COMPARISON = 9,    // <, >, <=, >=
    CONS = 10,         // ::, :>
    JOIN = 11,         // ++
    SHIFT = 12,        // <<, >>, >>>
    ADDITIVE = 13,     // +, -
    MULTIPLICATIVE = 14, // *, /, %
    POWER = 15,        // **
    UNARY = 16,        // !, ~, unary -
    CALL = 17,         // function call
    FIELD = 18,        // .
    HIGHEST = 19
};

// Parser implementation using Pratt parsing for expressions
class ParserImpl {
public:
    // --- Data members ---
    vector<Token> tokens_;
    size_t current_ = 0;
    vector<ParseError> errors_;
    ParserConfig config_;
    string_view filename_;
    string_view source_; // original source text for capturing function source

    // ADT constructor registry for pattern parsing
    struct ConstructorInfo {
        string type_name;
        int tag;
        int arity;
        vector<string> field_names; // for named field constructors
    };
    std::unordered_map<std::string, ConstructorInfo> constructor_registry_;

    // Performance optimization: cache for created AST nodes
    vector<unique_ptr<AstNode>> ast_pool_;

    // --- Constructor ---
    ParserImpl(ParserConfig config);

    // --- Public entry points (called by Parser facade) ---
    expected<unique_ptr<ModuleDecl>, vector<ParseError>>
    parse_module(string_view source, string_view filename);

    expected<unique_ptr<ExprNode>, vector<ParseError>>
    parse_expression(string_view source, string_view filename);

    // --- Token access helpers ---
    [[nodiscard]] bool is_at_end() const;
    [[nodiscard]] const Token& peek(size_t offset = 0) const;
    [[nodiscard]] const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);

    template<typename... Types>
    bool match(TokenType first, Types... rest) {
        return match(first) || match(rest...);
    }

    void skip_newlines();
    bool expect(TokenType type, const string& message);
    void error(ParseError::Type type, const string& message,
               optional<TokenType> expected = {});
    void synchronize();
    SourceLocation token_location(const Token& token) const;
    SourceLocation current_location() const;
    SourceLocation previous_location() const;
    [[nodiscard]] const Token& current() const;
    bool check_ahead(TokenType type) const;

    // --- Module parsing (ParserModule.cpp) ---
    unique_ptr<ModuleDecl> parse_module_internal();
    unique_ptr<PackageNameExpr> parse_module_name();
    void parse_export_list(vector<string>& exports, vector<ReExport>& re_exports);
    unique_ptr<FunctionExpr> parse_function();
    ParsedFunctionClause parse_function_clause_with_patterns(const string& expected_name);
    ParsedFunctionClause parse_function_clause_patterns_and_body();
    vector<unique_ptr<FunctionBody>> parse_function_clause(const string& expected_name);
    /// Consumes `@borrow` when present (two tokens: `@` and identifier `borrow`).
    bool try_consume_borrow_annotation();
    unique_ptr<AdtDeclNode> parse_adt_declaration();
    unique_ptr<TraitDeclNode> parse_trait_declaration();
    unique_ptr<InstanceDeclNode> parse_instance_declaration();
    unique_ptr<RecordNode> parse_record_definition();

    // --- Type parsing (ParserType.cpp) ---
    unique_ptr<compiler::types::Type> parse_type();
    unique_ptr<compiler::types::Type> parse_function_type();
    unique_ptr<compiler::types::Type> parse_sum_type();
    unique_ptr<compiler::types::Type> parse_product_type();
    unique_ptr<compiler::types::Type> parse_collection_type();
    shared_ptr<compiler::types::RefinePredicate> parse_refinement_predicate(const string& default_var);
    shared_ptr<compiler::types::RefinePredicate> parse_refinement_atom(const string& default_var);
    unique_ptr<compiler::types::Type> parse_primary_type();
    unique_ptr<TypeNameNode> parse_type_name_node();
    unique_ptr<TypeDefinition> parse_type_definition();
    unique_ptr<TypeNameNode> parse_type_name();

    // --- Pattern parsing (ParserPattern.cpp) ---
    unique_ptr<PatternNode> parse_pattern();
    unique_ptr<PatternNode> parse_pattern_no_or();
    unique_ptr<PatternNode> parse_pattern_or();
    unique_ptr<PatternNode> parse_pattern_primary();

    // --- Expression parsing (ParserExpr.cpp) ---
    bool could_be_argument(TokenType type) const;
    bool could_start_expr(TokenType type) const;
    unique_ptr<ExprNode> parse_juxtaposition_apply(unique_ptr<ExprNode> func);
    unique_ptr<ExprNode> parse_expr(Precedence min_prec = Precedence::LOWEST);
    unique_ptr<ExprNode> parse_expr_until_pattern_end();
    unique_ptr<ExprNode> parse_expr_until_in();
    unique_ptr<ExprNode> parse_prefix_expr_until_in();
    unique_ptr<ExprNode> parse_prefix_expr();
    unique_ptr<ExprNode> parse_infix_expr(unique_ptr<ExprNode> left, Precedence prec);
    unique_ptr<ExprNode> parse_range_bound();
    Precedence get_infix_precedence(TokenType type) const;
    Precedence next_precedence(Precedence prec) const;

    // Control flow
    unique_ptr<ExprNode> parse_if_expr(bool stop_at_in = false);
    unique_ptr<ExprNode> parse_let_expr();
    unique_ptr<AliasExpr> parse_alias();
    unique_ptr<ExprNode> parse_case_expr();
    unique_ptr<CaseClause> parse_case_clause();
    unique_ptr<ExprNode> parse_do_expr();
    unique_ptr<ExprNode> parse_try_expr();
    unique_ptr<ExprNode> parse_raise_expr();
    unique_ptr<ExprNode> parse_with_expr();
    unique_ptr<ExprNode> parse_perform_expr();
    unique_ptr<ExprNode> parse_handle_expr();
    unique_ptr<ExprNode> parse_do_remaining(vector<ExprNode*>& exprs);
    unique_ptr<ExprNode> parse_record_expr();
    unique_ptr<ExprNode> parse_string_interpolation();
    unique_ptr<ExprNode> parse_lambda_expr(bool stop_at_in = false);

    // Generators
    unique_ptr<ExprNode> parse_sequence_generator(SourceLocation loc, unique_ptr<ExprNode> expr);
    unique_ptr<ExprNode> parse_set_generator(SourceLocation loc, unique_ptr<ExprNode> expr);
    unique_ptr<CollectionExtractorExpr> parse_collection_extractor();
    unique_ptr<ExprNode> parse_dict(SourceLocation loc, unique_ptr<ExprNode> first_key);

    // Import / extern
    unique_ptr<ExprNode> parse_extern_decl();
    unique_ptr<ExprNode> parse_import_expr();
    unique_ptr<ImportClauseExpr> parse_import_clause();
    unique_ptr<FunctionsImport> parse_functions_import();
    bool check_fqn_start();
    unique_ptr<FqnExpr> parse_fqn();
    unique_ptr<NameExpr> parse_name();
};

} // namespace yona::parser
